#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "commctrl.h"
#pragma comment(lib, "comctl32.lib")
#include <windowsx.h>

#include <dbghelp.h>

#include <assert.h>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "kernel32.lib")

#pragma warning(disable: 4996)

extern "C"
{
	NTSYSAPI USHORT NTAPI RtlCaptureStackBackTrace(DWORD FramesToSkip, DWORD FramesToCapture, PVOID *BackTrace, _Out_opt_ PDWORD BackTraceHash);
};

#include <vector>
#include <algorithm>

#include "hashmap.h"

// remove C++11
// lowercase classes
// unsigned -> size_t
// less winapi

namespace muitv
{
	unsigned d64Hash(const DWORD64& value)
	{
		return unsigned(value >> 4);
	}

	bool d64Compare(const DWORD64& lhs, const DWORD64& rhs)
	{
		return lhs == rhs;
	}

	char* FormattedString(const char* src, ...)
	{
		static char temp[512];

		va_list args;
		va_start(args, src);

		vsnprintf(temp, 512, src, args);

		temp[511] = '\0';

		return temp;
	}

	struct FunctionInfo
	{
		FunctionInfo()
		{
			displacement64 = 0;

			functionInfo = (SYMBOL_INFO*)symbolBuffer;
		}

		FunctionInfo(const FunctionInfo& rhs)
		{
			memcpy(this, &rhs, sizeof(FunctionInfo));

			functionInfo = (SYMBOL_INFO*)symbolBuffer;
		}

		FunctionInfo& operator=(const FunctionInfo& rhs)
		{
			memcpy(this, &rhs, sizeof(FunctionInfo));

			functionInfo = (SYMBOL_INFO*)symbolBuffer;

			return *this;
		}

		static const size_t MAXSYMBOLNAMELENGTH = 512;
		static const size_t SYMBOLBUFFERSIZE = sizeof(SYMBOL_INFO) + MAXSYMBOLNAMELENGTH - 1;

		unsigned char symbolBuffer[SYMBOLBUFFERSIZE];

		DWORD64 displacement64;
		SYMBOL_INFO *functionInfo;
	};

	struct SourceInfo
	{
		SourceInfo()
		{
			displacement = 0;

			memset(&sourceInfo, 0, sizeof(sourceInfo));
		}

		DWORD displacement;
		IMAGEHLP_LINE64 sourceInfo;
	};

	struct SymbolInfo
	{
		SymbolInfo()
		{
			process = GetCurrentProcess();

			SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
	
			SymInitialize(process, 0, true);
		}

		~SymbolInfo()
		{
			SymCleanup(process);
		}

		static SymbolInfo& instance()
		{
			static SymbolInfo inst;

			return inst;
		}

		FunctionInfo* GetFunctionInfo(void* ptr)
		{
			DWORD64 w = reinterpret_cast<DWORD64>(ptr);

			if(DWORD64 *remap = addressRemap.find(w))
			{
				if(FunctionInfo *target = functionMap.find(*remap))
					return target;
			}

			FunctionInfo info;
			memset(info.functionInfo, 0, FunctionInfo::SYMBOLBUFFERSIZE);
			info.functionInfo->SizeOfStruct = sizeof(SYMBOL_INFO);
			info.functionInfo->MaxNameLen = FunctionInfo::MAXSYMBOLNAMELENGTH;

			if(SymFromAddr(process, w, &info.displacement64, info.functionInfo))
			{
				DWORD64 remap = info.functionInfo->Address;

				addressRemap.insert(w, remap);

				if(FunctionInfo *target = functionMap.find(remap))
					return target;

				functionMap.insert(remap, info);

				return functionMap.find(remap);
			}

			return 0;
		}

		SourceInfo* GetSourceInfo(void* ptr)
		{
			DWORD64 w = reinterpret_cast<DWORD64>(ptr);

			if(SourceInfo *info = sourceMap.find(w))
				return info;

			SourceInfo info;
			memset(&info.sourceInfo, 0, sizeof(IMAGEHLP_LINE64));
			info.sourceInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

			if(SymGetLineFromAddr64(process, w, &info.displacement, &info.sourceInfo))
			{
				sourceMap.insert(w, info);
			}

			return sourceMap.find(w);
		}

		HANDLE process;

		hashmap<DWORD64, DWORD64, d64Hash, d64Compare, 32 * 1024> addressRemap;
		hashmap<DWORD64, FunctionInfo, d64Hash, d64Compare, 32 * 1024> functionMap;
		hashmap<DWORD64, SourceInfo, d64Hash, d64Compare, 32 * 1024> sourceMap;
	};

	struct StackElement;

	std::vector<StackElement*> stackElements;

	struct StackElement
	{
		StackElement()
		{
			pos = ~0u;

			fInfo = 0;

			objectCount = 0;
			objectSize = 0;

			allocCount = 0;
			deallocCount = 0;

			allocSum = 0;
		}

		unsigned pos; // Position in the global array

		FunctionInfo *fInfo;

		unsigned objectCount; // How many objects are alive at this path segment
		unsigned objectSize; // How much memory is alive at this path segment

		// How many operations were performed at this path segment (note that an object is deallocated from the same path it was allocated
		unsigned allocCount;
		unsigned deallocCount;

		// How much memory in total was allocated at this path segment
		unsigned long long allocSum;

		std::vector<StackElement*> children;

		const char* GetName()
		{
			if(fInfo)
				return fInfo->functionInfo->Name;

			return "`unknown`";
		}

		void InsertBlockToTree(void** addresses, unsigned count, unsigned size, bool isAllocation)
		{
			if(isAllocation)
			{
				objectCount++;
				objectSize += size;

				allocCount++;
				allocSum += size;
			}
			else
			{
				objectCount--;
				objectSize -= size;

				deallocCount++;
			}

			if(count)
			{
				FunctionInfo *fInfo = SymbolInfo::instance().GetFunctionInfo(addresses[count - 1]);

				bool found = false;

				for(unsigned i = 0; i < children.size(); i++)
				{
					if(children[i]->fInfo == fInfo)
					{
						children[i]->InsertBlockToTree(addresses, count - 1, size, isAllocation);
						found = true;
						break;
					}
				}

				if(!found)
				{
					StackElement *element = new StackElement();

					element->pos = stackElements.size();
					stackElements.push_back(element);

					element->fInfo = fInfo;
					element->InsertBlockToTree(addresses, count - 1, size, isAllocation);

					children.push_back(element);
				}
			}
		}

		void SortChildren(bool (*SortFunc)(const StackElement* lhs, const StackElement* rhs))
		{
			std::sort(children.begin(), children.end(), SortFunc);

			for(unsigned i = 0; i < children.size(); i++)
				children[i]->SortChildren(SortFunc);
		}

		friend bool SortObjectCount(const StackElement* lhs, const StackElement* rhs)
		{
			return lhs->objectCount > rhs->objectCount;
		}

		friend bool SortObjectSize(const StackElement* lhs, const StackElement* rhs)
		{
			return lhs->objectSize > rhs->objectSize;
		}

		friend bool SortAllocCount(const StackElement* lhs, const StackElement* rhs)
		{
			return lhs->allocCount > rhs->allocCount;
		}

		friend bool SortAllocSize(const StackElement* lhs, const StackElement* rhs)
		{
			return lhs->allocSum > rhs->allocSum;
		}

		friend bool SortDeallocCount(const StackElement* lhs, const StackElement* rhs)
		{
			return lhs->deallocCount > rhs->deallocCount;
		}
	};

	struct MemoryStats
	{
		MemoryStats()
		{
			allocCount = 0;
			blocksCount = 0;
			bytesCount = 0;
			freeCount = 0;
			lastBlockNum = 0;
			memopsCount = 0;
		}

		unsigned allocCount;
		unsigned blocksCount;
		unsigned bytesCount;
		unsigned freeCount;
		unsigned lastBlockNum;
		unsigned memopsCount;
	};

	struct StackInfo
	{
		StackInfo()
		{
			stackSize = 0;
			stackInfo = 0;
		}

		unsigned stackSize;
		void** stackInfo;
	};

	struct MemoryBlock
	{
		size_t blockNum;
		size_t blockSize;
		void* blockStart;
		StackInfo stackInfo;
		void* blockInfoStart;
	};

	LRESULT CALLBACK WndProc(HWND hWnd, DWORD message, WPARAM wParam, LPARAM lParam);

	struct MemoryMan
	{
		MemoryMan()
		{
			InitializeCriticalSectionAndSpinCount(&cs, 1024);

			heap = HeapCreate(0, 8 * 1024 * 1024, 0);

			root = new StackElement();
			
			root->pos = stackElements.size();
			stackElements.push_back(root);

			HINSTANCE instance = GetModuleHandle(0);

			WNDCLASSEX wcex;
			memset(&wcex, 0, sizeof(WNDCLASSEX));

			wcex.cbSize = sizeof(WNDCLASSEX); 
			wcex.lpfnWndProc	= (WNDPROC)WndProc;
			wcex.hInstance		= instance;
			wcex.hCursor		= LoadCursor(0, IDC_ARROW);
			wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW);
			wcex.lpszClassName	= "MUITV_DASHBOARD";

			RegisterClassEx(&wcex);

			RECT windowRect = { 100, 100, 500, 500 };
			AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, false);

			window = CreateWindow("MUITV_DASHBOARD", "muitv - Dashboard", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, 0, 0, instance, this);

			SetWindowLongPtr(window, GWLP_USERDATA, (uintptr_t)this);

			INITCOMMONCONTROLSEX commControlTypes;
			commControlTypes.dwSize = sizeof(INITCOMMONCONTROLSEX);
			commControlTypes.dwICC = ICC_TREEVIEW_CLASSES;
			InitCommonControlsEx(&commControlTypes);

			tree = CreateWindow(WC_TREEVIEW, "", WS_CHILD | WS_BORDER | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_EDITLABELS, 5, 50, 490, 445, window, 0, instance, 0);

			TreeView_SetExtendedStyle(tree, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);

			TVINSERTSTRUCT helpInsert;

			helpInsert.hParent = 0;
			helpInsert.hInsertAfter = TVI_ROOT;
			helpInsert.item.mask = TVIF_TEXT | TVIF_CHILDREN | TVIF_PARAM;
			helpInsert.item.pszText = "Root";
			helpInsert.item.cChildren = I_CHILDRENCALLBACK;
			helpInsert.item.lParam = root->pos;

			TreeView_InsertItem(tree, &helpInsert);

			SetTimer(window, 10001, 200, 0);
		}

		~MemoryMan()
		{
			HeapDestroy(heap);

			DeleteCriticalSection(&cs);
		}

		static MemoryMan& instance()
		{
			static MemoryMan inst;

			return inst;
		}

		void* malloc(size_t size)
		{
			EnterCriticalSection(&cs);

			const size_t maxStackTraceDepth = 64;
			void* stackBuf[maxStackTraceDepth];
			size_t stackSize = RtlCaptureStackBackTrace(0, maxStackTraceDepth, stackBuf, 0);

			char* ptr = (char*)HeapAlloc(heap, HEAP_ZERO_MEMORY, size + (sizeof(void*) * stackSize) + sizeof(MemoryBlock));

			if(!ptr)
				return 0;

			MemoryBlock* block = (MemoryBlock*)(ptr + (sizeof(void*) * stackSize));

			block->blockNum = stats.lastBlockNum++;
			block->blockSize = size;
			block->blockStart = ptr + sizeof(MemoryBlock) + (sizeof(void*) * stackSize);
			block->blockInfoStart = ptr;

			block->stackInfo.stackSize = stackSize;
			block->stackInfo.stackInfo = (void**)(ptr);

			memcpy(block->stackInfo.stackInfo, stackBuf, sizeof(void*) * stackSize);

			stats.bytesCount += size;
			stats.blocksCount++;
			stats.memopsCount++;
			stats.allocCount++;

			if(block->stackInfo.stackSize > skipBegin + skipEnd)
				root->InsertBlockToTree(block->stackInfo.stackInfo + skipBegin, block->stackInfo.stackSize - (skipBegin + skipEnd), block->blockSize, true);

			LeaveCriticalSection(&cs);

			ptr += sizeof(MemoryBlock) + (sizeof(void*) * stackSize);

			return ptr;
		}

		void free(void *ptr)
		{
			if(!ptr)
				return;

			EnterCriticalSection(&cs);

			char* cPtr = (char*)(ptr);

			cPtr -= sizeof(MemoryBlock);

			MemoryBlock* block = (MemoryBlock*)cPtr;

			int size = block->blockSize;

			stats.bytesCount -= size;
			stats.blocksCount--;
			stats.memopsCount++;
			stats.freeCount++;

			if(block->stackInfo.stackSize > skipBegin + skipEnd)
				root->InsertBlockToTree(block->stackInfo.stackInfo + skipBegin, block->stackInfo.stackSize - (skipBegin + skipEnd), block->blockSize, false);

			HeapFree(heap, 0, block->blockInfoStart);

			LeaveCriticalSection(&cs);
		}
		
		void UpdateTree(HTREEITEM parent)
		{
			if(!parent)
				return;

			TVITEM item;

			item.mask = TVIF_TEXT | TVIF_PARAM;
			item.cchTextMax = 0;
			item.hItem = parent;

			TreeView_GetItem(tree, &item);

			if(stackElements.empty())
				return;

			StackElement *node = stackElements[item.lParam];

			item.mask = TVIF_TEXT;
			item.pszText = FormattedString("%s (x%d for %dkb)", node->GetName(), node->allocCount, node->allocSum / 1024);

			TreeView_SetItem(tree, &item);

			HTREEITEM child = TreeView_GetChild(tree, parent);

			while(child)
			{
				UpdateTree(child);

				child = TreeView_GetNextSibling(tree, child);
			}
		}

		LRESULT Proc(HWND hWnd, DWORD message, WPARAM wParam, LPARAM lParam)
		{
			switch(message)
			{
			case WM_NOTIFY:
				if(((LPNMHDR)lParam)->code == TVN_GETDISPINFO && ((LPNMHDR)lParam)->hwndFrom == tree)
				{
					LPNMTVDISPINFO info = (LPNMTVDISPINFO)lParam;

					EnterCriticalSection(&cs);

					if(info->item.mask & TVIF_CHILDREN)
					{
						if(size_t(info->item.lParam) < stackElements.size())
							info->item.cChildren = stackElements[info->item.lParam]->children.size();
						else
							info->item.cChildren = 0;
					}

					LeaveCriticalSection(&cs);
				}
				else if(((LPNMHDR)lParam)->code == TVN_ITEMEXPANDING && ((LPNMHDR)lParam)->hwndFrom == tree)
				{
					LPNMTREEVIEW info = (LPNMTREEVIEW)lParam;

					while(HTREEITEM child = TreeView_GetChild(tree, info->itemNew.hItem))
						TreeView_DeleteItem(tree, child);

					EnterCriticalSection(&cs);

					if(size_t(info->itemNew.lParam) < stackElements.size())
					{
						StackElement *parent = stackElements[info->itemNew.lParam];

						parent->SortChildren(SortAllocSize);

						for(unsigned i = 0; i < parent->children.size(); i++)
						{
							StackElement *elem = parent->children[i];

							TVINSERTSTRUCT helpInsert;

							helpInsert.hParent = info->itemNew.hItem;
							helpInsert.hInsertAfter = TVI_LAST;
							helpInsert.item.cchTextMax = 0;
							helpInsert.item.mask = TVIF_TEXT | TVIF_CHILDREN | TVIF_PARAM;
							helpInsert.item.pszText = FormattedString("%s (x%d for %dkb)", elem->GetName(), elem->allocCount, elem->allocSum / 1024);
							helpInsert.item.cChildren = I_CHILDRENCALLBACK;
							helpInsert.item.lParam = elem->pos;
							
							TreeView_InsertItem(tree, &helpInsert);
						}
					}

					LeaveCriticalSection(&cs);
				}
				break;
			case WM_TIMER:
				if(wParam == 10001)
				{
					TVITEM item;

					item.mask = TVIF_TEXT;
					item.cchTextMax = 0;
					item.pszText = FormattedString("Root (x%d for %dkb)", stats.blocksCount, stats.bytesCount / 1024);
					item.hItem = TreeView_GetRoot(tree);

					TreeView_SetItem(tree, &item);

					EnterCriticalSection(&cs);

					UpdateTree(TreeView_GetChild(tree, item.hItem));

					LeaveCriticalSection(&cs);

					return 0;
				}
				break;
			}

			return DefWindowProc(hWnd, message, wParam, lParam);
		}

		static const size_t skipBegin = 2;
		static const size_t skipEnd = 4;

		CRITICAL_SECTION cs;

		HANDLE heap;

		MemoryStats stats;

		StackElement *root;

		// Display
		HWND window;

		HWND tree;
	};

	LRESULT CALLBACK WndProc(HWND hWnd, DWORD message, WPARAM wParam, LPARAM lParam)
	{
		MemoryMan *memoryMan = (MemoryMan*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

		return memoryMan->Proc(hWnd, message, wParam, lParam);
	}
}

extern "C" __declspec(dllexport) void* muitv_alloc(size_t size)
{
	return muitv::MemoryMan::instance().malloc(size);
}

extern "C" __declspec(dllexport) void muitv_free(void* ptr)
{
	muitv::MemoryMan::instance().free(ptr);
}
