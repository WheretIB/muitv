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

#include "hash_map.h"

#include "function_info.h"
#include "source_info.h"
#include "symbol_info.h"
#include "stack_element.h"
#include "memory_stats.h"
#include "stack_info.h"
#include "memory_block.h"

namespace muitv
{
	namespace detail
	{
		char* formatted_string(const char* src, ...)
		{
			static char temp[512];

			va_list args;
			va_start(args, src);

			vsnprintf(temp, 512, src, args);

			temp[511] = '\0';

			return temp;
		}

		LRESULT CALLBACK window_proc(HWND hWnd, DWORD message, WPARAM wParam, LPARAM lParam);
	}

	struct memory_dashboard
	{
		memory_dashboard()
		{
			InitializeCriticalSectionAndSpinCount(&cs, 1024);

			heap = HeapCreate(0, 8 * 1024 * 1024, 0);

			root = stackElementPool.allocate();
			
			root->pos = stackElements.size();
			stackElements.push_back(root);

			HINSTANCE instance = GetModuleHandle(0);

			WNDCLASSEX wcex;
			memset(&wcex, 0, sizeof(WNDCLASSEX));

			wcex.cbSize = sizeof(WNDCLASSEX); 
			wcex.lpfnWndProc	= (WNDPROC)detail::window_proc;
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

		~memory_dashboard()
		{
			HeapDestroy(heap);

			DeleteCriticalSection(&cs);
		}

		static memory_dashboard& instance()
		{
			static memory_dashboard inst;

			return inst;
		}

		void* malloc(size_t size)
		{
			EnterCriticalSection(&cs);

			const size_t maxStackTraceDepth = 64;
			void* stackBuf[maxStackTraceDepth];
			size_t stackSize = RtlCaptureStackBackTrace(0, maxStackTraceDepth, stackBuf, 0);

			char* ptr = (char*)HeapAlloc(heap, HEAP_ZERO_MEMORY, size + (sizeof(void*) * stackSize) + sizeof(memory_block));

			if(!ptr)
				return 0;

			memory_block* block = (memory_block*)(ptr + (sizeof(void*) * stackSize));

			block->blockNum = stats.lastBlockNum++;
			block->blockSize = size;
			block->blockStart = ptr + sizeof(memory_block) + (sizeof(void*) * stackSize);
			block->blockInfoStart = ptr;

			block->stackInfo.stackSize = stackSize;
			block->stackInfo.stackInfo = (void**)(ptr);

			memcpy(block->stackInfo.stackInfo, stackBuf, sizeof(void*) * stackSize);

			stats.bytesCount += size;
			stats.blocksCount++;
			stats.memopsCount++;
			stats.allocCount++;

			if(block->stackInfo.stackSize > skipBegin + skipEnd)
				insert_block_to_tree(root, block->stackInfo.stackInfo + skipBegin, block->stackInfo.stackSize - (skipBegin + skipEnd), block->blockSize, true);

			LeaveCriticalSection(&cs);

			ptr += sizeof(memory_block) + (sizeof(void*) * stackSize);

			return ptr;
		}

		void free(void *ptr)
		{
			if(!ptr)
				return;

			EnterCriticalSection(&cs);

			char* cPtr = (char*)(ptr);

			cPtr -= sizeof(memory_block);

			memory_block* block = (memory_block*)cPtr;

			int size = block->blockSize;

			stats.bytesCount -= size;
			stats.blocksCount--;
			stats.memopsCount++;
			stats.freeCount++;

			if(block->stackInfo.stackSize > skipBegin + skipEnd)
				insert_block_to_tree(root, block->stackInfo.stackInfo + skipBegin, block->stackInfo.stackSize - (skipBegin + skipEnd), block->blockSize, false);

			HeapFree(heap, 0, block->blockInfoStart);

			LeaveCriticalSection(&cs);
		}

		void insert_block_to_tree(stack_element *node, void** addresses, unsigned count, unsigned size, bool isAllocation)
		{
			if(isAllocation)
			{
				node->objectCount++;
				node->objectSize += size;

				node->allocCount++;
				node->allocSize += size;
			}
			else
			{
				node->objectCount--;
				node->objectSize -= size;

				node->freeCount++;
			}

			if(count)
			{
				function_info *fInfo = symbol_info::instance().get_function_info(addresses[count - 1]);

				bool found = false;

				for(unsigned i = 0; i < node->children.size(); i++)
				{
					if(node->children[i]->fInfo == fInfo)
					{
						insert_block_to_tree(node->children[i], addresses, count - 1, size, isAllocation);
						found = true;
						break;
					}
				}

				if(!found)
				{
					stack_element *element = stackElementPool.allocate();

					element->pos = stackElements.size();
					stackElements.push_back(element);

					element->fInfo = fInfo;
					insert_block_to_tree(element, addresses, count - 1, size, isAllocation);

					node->children.push_back(element);
				}
			}
		}
		
		void update_tree_display(HTREEITEM parent)
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

			stack_element *node = stackElements[item.lParam];

			item.mask = TVIF_TEXT;
			item.pszText = detail::formatted_string("%s (x%d for %dkb)", node->get_name(), node->objectCount, node->objectSize / 1024);

			TreeView_SetItem(tree, &item);

			HTREEITEM child = TreeView_GetChild(tree, parent);

			while(child)
			{
				update_tree_display(child);

				child = TreeView_GetNextSibling(tree, child);
			}
		}

		LRESULT window_message_handle(HWND hWnd, DWORD message, WPARAM wParam, LPARAM lParam)
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
						stack_element *parent = stackElements[info->itemNew.lParam];

						parent->sort_children(compare_object_size);

						for(unsigned i = 0; i < parent->children.size(); i++)
						{
							stack_element *elem = parent->children[i];

							TVINSERTSTRUCT helpInsert;

							helpInsert.hParent = info->itemNew.hItem;
							helpInsert.hInsertAfter = TVI_LAST;
							helpInsert.item.cchTextMax = 0;
							helpInsert.item.mask = TVIF_TEXT | TVIF_CHILDREN | TVIF_PARAM;
							helpInsert.item.pszText = detail::formatted_string("%s (x%d for %dkb)", elem->get_name(), elem->objectCount, elem->objectSize / 1024);
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
					item.pszText = detail::formatted_string("Root (x%d for %dkb)", stats.blocksCount, stats.bytesCount / 1024);
					item.hItem = TreeView_GetRoot(tree);

					TreeView_SetItem(tree, &item);

					EnterCriticalSection(&cs);

					update_tree_display(TreeView_GetChild(tree, item.hItem));

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

		memory_stats stats;

		stack_element *root;

		block_pool<stack_element, 1024> stackElementPool;

		std::vector<stack_element*> stackElements;

		// Display
		HWND window;

		HWND tree;
	};

	LRESULT CALLBACK detail::window_proc(HWND hWnd, DWORD message, WPARAM wParam, LPARAM lParam)
	{
		memory_dashboard *memoryMan = (memory_dashboard*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

		return memoryMan->window_message_handle(hWnd, message, wParam, lParam);
	}
}

extern "C" __declspec(dllexport) void* muitv_alloc(size_t size)
{
	return muitv::memory_dashboard::instance().malloc(size);
}

extern "C" __declspec(dllexport) void muitv_free(void* ptr)
{
	muitv::memory_dashboard::instance().free(ptr);
}
