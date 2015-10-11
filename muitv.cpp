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
#include "dynamic_array.h"

namespace muitv
{
	const unsigned memory_alignment = 16;

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

		inline unsigned ptrHash(void* const& value)
		{
			return unsigned(uintptr_t(value) >> 4);
		}

		DWORD CALLBACK window_thread(LPVOID lpThreadParameter);

		LRESULT CALLBACK window_proc(HWND hWnd, DWORD message, WPARAM wParam, LPARAM lParam);

		int CALLBACK tree_node_compare_func(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);

		enum display_info
		{
			display_size,
			display_objects,
			display_alloc,
			display_free,
			display_alltime,
		};
	}

	struct memory_dashboard
	{
		memory_dashboard()
		{
			detail::free(detail::alloc<int>());

			symbol_info::instance();

			InitializeCriticalSectionAndSpinCount(&cs, 1024);

			heap = HeapCreate(0, 8 * 1024 * 1024, 0);

			root = stackElementPool.allocate();
			
			root->pos = stackElements.size();
			stackElements.push_back(root);

			dashboardExit = false;

			thread = CreateThread(NULL, 0, detail::window_thread, this, 0, 0);

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

			displayMode = detail::display_size;
		}

		~memory_dashboard()
		{
			// Join dashboard thread
			dashboardExit = true;

			for(;;)
			{
				Sleep(16);

				DWORD exitCode = 0;

				if(GetExitCodeThread(thread, &exitCode) == 0)
					break;

				if(exitCode != STILL_ACTIVE)
					break;
			}

			HeapDestroy(heap);

			DeleteCriticalSection(&cs);
		}

		static memory_dashboard& instance()
		{
			static memory_dashboard inst;

			return inst;
		}

		void* malloc(size_t size, bool fake = false)
		{
			EnterCriticalSection(&cs);

			const size_t maxStackTraceDepth = 64;
			void* stackBuf[maxStackTraceDepth];
			size_t stackSize = RtlCaptureStackBackTrace(0, maxStackTraceDepth, stackBuf, 0);

			char *ptr = (char*)HeapAlloc(heap, HEAP_ZERO_MEMORY, (fake ? 0 : size) + (sizeof(void*) * stackSize) + sizeof(memory_block) + memory_alignment);

			if(!ptr)
			{
				LeaveCriticalSection(&cs);

				return 0;
			}

			uintptr_t currStart = uintptr_t(ptr + (sizeof(void*) * stackSize) + sizeof(memory_block));

			size_t memoryOffset = memory_alignment - currStart % memory_alignment;

			memory_block* block = (memory_block*)(ptr + (sizeof(void*) * stackSize) + memoryOffset);

			block->blockNum = stats.lastBlockNum++;
			block->blockSize = size;
			block->blockInfoStart = ptr;

			block->stackInfo.stackSize = stackSize;
			block->stackInfo.stackInfo = (void**)(ptr);

			memcpy(block->stackInfo.stackInfo, stackBuf, sizeof(void*) * stackSize);

			stats.bytesCount += block->blockSize;
			stats.blocksCount++;
			stats.memopsCount++;
			stats.allocCount++;

			if(block->stackInfo.stackSize > skipBegin + skipEnd)
				insert_block_to_tree(root, block->stackInfo.stackInfo + skipBegin, block->stackInfo.stackSize - (skipBegin + skipEnd), block->blockSize, true);

			LeaveCriticalSection(&cs);

			ptr += (sizeof(void*) * stackSize) + memoryOffset + sizeof(memory_block);

			return ptr;
		}

		void* calloc(size_t count, size_t size)
		{
			void* ptr = malloc(count * size);

			if(ptr)
				memset(ptr, 0, count * size);

			return ptr;
		}

		void* realloc(void* ptr, size_t size)
		{
			if(!ptr)
				return malloc(size);

			if(!size)
			{
				free(ptr);
				return 0;
			}

			size_t oldSize = get_size(ptr);

			void* result = malloc(size);

			if(result)
			{
				memcpy(result, ptr, (size < oldSize ? size : oldSize));
				free(ptr);
			}

			return result;
		}

		void free(void *ptr)
		{
			if(!ptr)
				return;

			memory_block* block = (memory_block*)((char*)ptr - sizeof(memory_block));

			EnterCriticalSection(&cs);

			stats.bytesCount -= block->blockSize;
			stats.blocksCount--;
			stats.memopsCount++;
			stats.freeCount++;

			if(block->stackInfo.stackSize > skipBegin + skipEnd)
				insert_block_to_tree(root, block->stackInfo.stackInfo + skipBegin, block->stackInfo.stackSize - (skipBegin + skipEnd), block->blockSize, false);

			HeapFree(heap, 0, block->blockInfoStart);

			LeaveCriticalSection(&cs);
		}

		size_t get_size(void *ptr)
		{
			if(!ptr)
				return 0;

			memory_block* block = (memory_block*)((char*)ptr - sizeof(memory_block));

			EnterCriticalSection(&cs);

			size_t size = block->blockSize;

			LeaveCriticalSection(&cs);

			return size;
		}

		void add_call_stack_to_tree(size_t size)
		{
			EnterCriticalSection(&cs);

			const size_t maxStackTraceDepth = 64;
			void* stackBuf[maxStackTraceDepth];
			size_t stackSize = RtlCaptureStackBackTrace(0, maxStackTraceDepth, stackBuf, 0);

			stats.bytesCount += size;
			stats.blocksCount++;
			stats.memopsCount++;
			stats.allocCount++;

			if(stackSize > skipBegin + skipEnd)
				insert_block_to_tree(root, stackBuf + skipBegin, stackSize - (skipBegin + skipEnd), size, true);

			LeaveCriticalSection(&cs);
		}

		void add_object(void* ptr, size_t size)
		{
			EnterCriticalSection(&cs);

			void *handle = malloc(size, true);

			manualMap.insert(ptr, handle);

			LeaveCriticalSection(&cs);
		}

		void remove_object(void* ptr)
		{
			EnterCriticalSection(&cs);

			if(void **handle = manualMap.find(ptr))
			{
				free(*handle);

				manualMap.remove(ptr);
			}

			LeaveCriticalSection(&cs);
		}

		void insert_block_to_tree(stack_element *node, void** addresses, size_t count, size_t size, bool isAllocation)
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

				for(size_t i = 0; i < node->children.size(); i++)
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

		detail::display_info get_display_mode()
		{
			if(!!Button_GetCheck(sortingObjects))
				return detail::display_objects;

			if(!!Button_GetCheck(sortingAlloc))
				return detail::display_alloc;

			if(!!Button_GetCheck(sortingFree))
				return detail::display_free;

			if(!!Button_GetCheck(sortingTotalAllocated))
				return detail::display_alltime;

			return detail::display_size;
		}

		char* get_display_info(stack_element *node, detail::display_info mode)
		{
			if(mode == detail::display_alloc)
				return detail::formatted_string("%s (%Iu)", node->get_name(), node->allocCount);

			if(mode == detail::display_free)
				return detail::formatted_string("%s (%Iu)", node->get_name(), node->freeCount);

			if(mode == detail::display_alltime)
				return detail::formatted_string("%s (x%Iu %lldkb)", node->get_name(), node->allocCount, node->allocSize / 1024);

			return detail::formatted_string("%s (x%Iu for %Iukb)", node->get_name(), node->objectCount, node->objectSize / 1024);
		}

		stack_element::sort_func get_display_sort(detail::display_info mode)
		{
			if(mode == detail::display_objects)
				return compare_object_count;

			if(mode == detail::display_alloc)
				return compare_alloc_count;

			if(mode == detail::display_free)
				return compare_free_count;

			if(mode == detail::display_alltime)
				return compare_alloc_size;

			return compare_object_size;
		}

		stack_element* update_tree_display(HTREEITEM parent, detail::display_info mode)
		{
			if(!parent)
				return 0;

			TVITEM item;

			item.mask = TVIF_TEXT | TVIF_PARAM;
			item.cchTextMax = 0;
			item.hItem = parent;

			TreeView_GetItem(tree, &item);

			if(stackElements.empty())
				return 0;

			stack_element *node = stackElements[item.lParam];

			item.mask = TVIF_TEXT;
			item.pszText = get_display_info(node, mode);

			TreeView_SetItem(tree, &item);

			node->sort_children(get_display_sort(mode));

			unsigned childrenCount = 0;
			bool outdated = false;

			static size_t nextUpdateMark = 0;

			size_t currentUpdateMark = nextUpdateMark++;

			HTREEITEM child = TreeView_GetChild(tree, parent);

			while(child)
			{
				stack_element *stackChild = update_tree_display(child, mode);

				stackChild->updateMark = currentUpdateMark;

				if(childrenCount < node->children.size() && stackChild != node->children[childrenCount])
					outdated = true;

				childrenCount++;

				child = TreeView_GetNextSibling(tree, child);
			}

			// Append missing children
			if(childrenCount && childrenCount < node->children.size())
			{
				for(unsigned i = 0; i < node->children.size(); i++)
				{
					stack_element *elem = node->children[i];

					if(elem->updateMark != currentUpdateMark)
					{
						TVINSERTSTRUCT helpInsert;

						helpInsert.hParent = parent;
						helpInsert.hInsertAfter = TVI_LAST;
						helpInsert.item.cchTextMax = 0;
						helpInsert.item.mask = TVIF_TEXT | TVIF_CHILDREN | TVIF_PARAM;
						helpInsert.item.pszText = get_display_info(elem, mode);
						helpInsert.item.cChildren = I_CHILDRENCALLBACK;
						helpInsert.item.lParam = elem->pos;

						TreeView_InsertItem(tree, &helpInsert);
					}
				}
			}

			// Fix-up the element sorting order
			if(outdated)
			{
				TVSORTCB sort;

				sort.hParent = parent;
				sort.lpfnCompare = detail::tree_node_compare_func;
				sort.lParam = (LPARAM)this;

				TreeView_SortChildrenCB(tree, &sort, 0);
			}

			return node;
		}

		void expand_tree_nodes(HTREEITEM parent)
		{
			TVITEM item;

			item.mask = TVIF_TEXT | TVIF_PARAM;
			item.cchTextMax = 0;
			item.hItem = parent;

			TreeView_GetItem(tree, &item);

			if((size_t)item.lParam < stackElements.size())
			{
				stack_element *node = stackElements[item.lParam];

				const char *name = node->get_name();

				if(strstr(name, "std::") == name || strstr(name, "operator new") == name)
					return;
			}

			TreeView_Expand(tree, parent, TVE_EXPAND);

			HTREEITEM child = TreeView_GetChild(tree, parent);

			while(child)
			{
				expand_tree_nodes(child);

				child = TreeView_GetNextSibling(tree, child);
			}
		}

		int node_compare_func(size_t leftPos, size_t rightPos)
		{
			stack_element *left = stackElements[leftPos];
			stack_element *right = stackElements[rightPos];

			stack_element::sort_func sort = get_display_sort(displayMode);

			if(sort(left, right))
				return -1;

			if(sort(right, left))
				return 1;

			return 0;
		}

		void window_create()
		{
			HINSTANCE instance = GetModuleHandle(0);

			RECT windowRect = { 0, 0, 500, 500 };

			unsigned width = windowRect.right - windowRect.left;
			unsigned height = windowRect.bottom - windowRect.top;

			AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, false);

			unsigned adjustedWidth = windowRect.right - windowRect.left;
			unsigned adjustedHeight = windowRect.bottom - windowRect.top;

			window = CreateWindow("MUITV_DASHBOARD", "muitv - Dashboard", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, adjustedWidth, adjustedHeight, 0, 0, instance, this);

			SetWindowLongPtr(window, GWLP_USERDATA, (uintptr_t)this);

			INITCOMMONCONTROLSEX commControlTypes;
			commControlTypes.dwSize = sizeof(INITCOMMONCONTROLSEX);
			commControlTypes.dwICC = ICC_TREEVIEW_CLASSES;
			InitCommonControlsEx(&commControlTypes);

			tree = CreateWindow(WC_TREEVIEW, "", WS_CHILD | WS_BORDER | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_EDITLABELS, 5, 135, width - 10, height - 140, window, 0, instance, 0);

			TreeView_SetExtendedStyle(tree, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);

			TVINSERTSTRUCT helpInsert;

			helpInsert.hParent = 0;
			helpInsert.hInsertAfter = TVI_ROOT;
			helpInsert.item.mask = TVIF_TEXT | TVIF_CHILDREN | TVIF_PARAM;
			helpInsert.item.pszText = "Root";
			helpInsert.item.cChildren = I_CHILDRENCALLBACK;
			helpInsert.item.lParam = root->pos;

			TreeView_InsertItem(tree, &helpInsert);

			labelAllocCount = CreateWindow("STATIC", "", WS_VISIBLE | WS_CHILD, 5 + (width - 10) / 3 * 0, 5, (width - 10) / 3 - 5, 20, window, 0, instance, 0);
			labelFreeCount = CreateWindow("STATIC", "", WS_VISIBLE | WS_CHILD, 5 + (width - 10) / 3 * 1, 5, (width - 10) / 3 - 5, 20, window, 0, instance, 0);
			labelOperationCount = CreateWindow("STATIC", "", WS_VISIBLE | WS_CHILD, 5 + (width - 10) / 3 * 2, 5, (width - 10) / 3 - 5, 20, window, 0, instance, 0);

			labelBlockCount = CreateWindow("STATIC", "", WS_VISIBLE | WS_CHILD, 5 + (width - 10) / 3 * 0, 30, (width - 10) / 3 - 5, 20, window, 0, instance, 0);
			labelByteCount = CreateWindow("STATIC", "", WS_VISIBLE | WS_CHILD, 5 + (width - 10) / 3 * 1, 30, (width - 10) / 3 - 5, 20, window, 0, instance, 0);

			// Sorting style
			labelSorting = CreateWindow("STATIC", "Sorting:", WS_VISIBLE | WS_CHILD, 5 + (width - 10) / 3 * 0, 55, (width - 10) / 3 - 5, 20, window, 0, instance, 0);

			sortingSize = CreateWindowA("BUTTON", "Size", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP, 5 + (width - 10) / 5 * 0, 80, (width - 10) / 5 - 5, 20, window, 0, instance, 0);
			sortingObjects = CreateWindowA("BUTTON", "Objects", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, 5 + (width - 10) / 5 * 1, 80, (width - 10) / 5 - 5, 20, window, 0, instance, 0);
			sortingAlloc = CreateWindowA("BUTTON", "Alloc", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, 5 + (width - 10) / 5 * 2, 80, (width - 10) / 5 - 5, 20, window, 0, instance, 0);
			sortingFree = CreateWindowA("BUTTON", "Free", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, 5 + (width - 10) / 5 * 3, 80, (width - 10) / 5 - 5, 20, window, 0, instance, 0);
			sortingTotalAllocated = CreateWindowA("BUTTON", "All-time", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, 5 + (width - 10) / 5 * 4, 80, (width - 10) / 5 - 5, 20, window, 0, instance, 0);

			Button_SetCheck(sortingSize, 1);

			buttonExpandAll = CreateWindowA("BUTTON", "Expand all", WS_VISIBLE | WS_CHILD, 5 + (width - 10) / 4 * 0, 105, (width - 10) / 4 - 5, 25, window, 0, instance, 0);
			buttonCollapseAll = CreateWindowA("BUTTON", "Collapse all", WS_VISIBLE | WS_CHILD, 5 + (width - 10) / 4 * 1, 105, (width - 10) / 4 - 5, 25, window, 0, instance, 0);

			checkboxRefresh = CreateWindowA("BUTTON", "Refresh tree", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 5 + (width - 10) / 4 * 2, 105, (width - 10) / 4 - 5, 25, window, 0, instance, 0);

			Button_SetCheck(checkboxRefresh, 1);

			SetTimer(window, 10001, 200, 0);

			UpdateWindow(window);

			while(!dashboardExit)
			{
				MSG msg;
				while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
				{
					TranslateMessage(&msg);
					DispatchMessage(&msg);
				}

				Sleep(16);
			}
		}

		LRESULT window_message_handle(HWND hWnd, DWORD message, WPARAM wParam, LPARAM lParam)
		{
			switch(message)
			{
			case WM_COMMAND:
				if((HWND)lParam == buttonExpandAll && HIWORD(wParam) == BN_CLICKED)
				{
					EnterCriticalSection(&cs);

					HTREEITEM treeRoot = TreeView_GetRoot(tree);

					expand_tree_nodes(treeRoot);

					LeaveCriticalSection(&cs);
				}
				else if((HWND)lParam == buttonCollapseAll && HIWORD(wParam) == BN_CLICKED)
				{
					EnterCriticalSection(&cs);

					HTREEITEM treeRoot = TreeView_GetRoot(tree);

					TreeView_Expand(tree, treeRoot, TVE_COLLAPSE);

					while(HTREEITEM child = TreeView_GetChild(tree, root))
						TreeView_DeleteItem(tree, child);

					LeaveCriticalSection(&cs);
				}
				break;
			case WM_NOTIFY:
				if(((LPNMHDR)lParam)->code == TVN_GETDISPINFO && ((LPNMHDR)lParam)->hwndFrom == tree)
				{
					LPNMTVDISPINFO info = (LPNMTVDISPINFO)lParam;

					EnterCriticalSection(&cs);

					if(info->item.mask & TVIF_CHILDREN)
					{
						if(size_t(info->item.lParam) < stackElements.size())
							info->item.cChildren = (int)stackElements[info->item.lParam]->children.size();
						else
							info->item.cChildren = 0;
					}

					LeaveCriticalSection(&cs);
				}
				else if(((LPNMHDR)lParam)->code == TVN_ITEMEXPANDING && ((LPNMHDR)lParam)->hwndFrom == tree)
				{
					LPNMTREEVIEW info = (LPNMTREEVIEW)lParam;

					detail::display_info mode = get_display_mode();

					EnterCriticalSection(&cs);

					// If the element has a valid position and is not expanded yet
					if(size_t(info->itemNew.lParam) < stackElements.size() && TreeView_GetChild(tree, info->itemNew.hItem) == 0)
					{
						stack_element *parent = stackElements[info->itemNew.lParam];

						parent->sort_children(get_display_sort(mode));

						for(size_t i = 0; i < parent->children.size(); i++)
						{
							stack_element *elem = parent->children[i];

							TVINSERTSTRUCT helpInsert;

							helpInsert.hParent = info->itemNew.hItem;
							helpInsert.hInsertAfter = TVI_LAST;
							helpInsert.item.cchTextMax = 0;
							helpInsert.item.mask = TVIF_TEXT | TVIF_CHILDREN | TVIF_PARAM;
							helpInsert.item.pszText = get_display_info(elem, mode);
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
					EnterCriticalSection(&cs);

					Static_SetText(labelAllocCount, detail::formatted_string("Alloc: %Iu", stats.allocCount));
					Static_SetText(labelFreeCount, detail::formatted_string("Free: %Iu", stats.freeCount));
					Static_SetText(labelOperationCount, detail::formatted_string("Total: %Iu", stats.memopsCount));

					Static_SetText(labelBlockCount, detail::formatted_string("Blocks: %Iu", stats.blocksCount));
					Static_SetText(labelByteCount, detail::formatted_string("Size: %.3fmb", stats.bytesCount / 1024.0 / 1024.0));

					displayMode = get_display_mode();

					if(!!Button_GetCheck(checkboxRefresh))
					{
						HTREEITEM treeRoot = TreeView_GetRoot(tree);

						update_tree_display(treeRoot, displayMode);
					}

					LeaveCriticalSection(&cs);

					return 0;
				}
				break;
			case WM_SIZE:
				{
					unsigned width = LOWORD(lParam);
					unsigned height = HIWORD(lParam);

					SetWindowPos(labelAllocCount, HWND_TOP, 5 + (width - 10) / 3 * 0, 5, (width - 10) / 3 - 5, 20, 0);
					SetWindowPos(labelFreeCount, HWND_TOP, 5 + (width - 10) / 3 * 1, 5, (width - 10) / 3 - 5, 20, 0);
					SetWindowPos(labelOperationCount, HWND_TOP, 5 + (width - 10) / 3 * 2, 5, (width - 10) / 3 - 5, 20, 0);

					SetWindowPos(labelBlockCount, HWND_TOP, 5 + (width - 10) / 3 * 0, 30, (width - 10) / 3 - 5, 20, 0);
					SetWindowPos(labelByteCount, HWND_TOP, 5 + (width - 10) / 3 * 1, 30, (width - 10) / 3 - 5, 20, 0);

					SetWindowPos(labelSorting, HWND_TOP, 5 + (width - 10) / 3 * 0, 55, (width - 10) / 3 - 5, 20, 0);

					SetWindowPos(sortingSize, HWND_TOP, 5 + (width - 10) / 5 * 0, 80, (width - 10) / 5 - 5, 20, 0);
					SetWindowPos(sortingObjects, HWND_TOP, 5 + (width - 10) / 5 * 1, 80, (width - 10) / 5 - 5, 20, 0);
					SetWindowPos(sortingAlloc, HWND_TOP, 5 + (width - 10) / 5 * 2, 80, (width - 10) / 5 - 5, 20, 0);
					SetWindowPos(sortingFree, HWND_TOP, 5 + (width - 10) / 5 * 3, 80, (width - 10) / 5 - 5, 20, 0);
					SetWindowPos(sortingTotalAllocated, HWND_TOP, 5 + (width - 10) / 5 * 4, 80, (width - 10) / 5 - 5, 20, 0);

					SetWindowPos(buttonExpandAll, HWND_TOP, 5 + (width - 10) / 4 * 0, 105, (width - 10) / 4 - 5, 25, 0);
					SetWindowPos(buttonCollapseAll, HWND_TOP, 5 + (width - 10) / 4 * 1, 105, (width - 10) / 4 - 5, 25, 0);

					SetWindowPos(checkboxRefresh, HWND_TOP, 5 + (width - 10) / 4 * 2, 105, (width - 10) / 4 - 5, 25, 0);

					SetWindowPos(tree, HWND_TOP, 5, 135, width - 10, height - 140, 0);
				}
				break;
			}

			return DefWindowProc(hWnd, message, wParam, lParam);
		}

		static const size_t skipBegin = 1;
		static const size_t skipEnd = 4;

		CRITICAL_SECTION cs;

		HANDLE heap;

		memory_stats stats;

		stack_element *root;

		block_pool<stack_element, 1024> stackElementPool;

		dynamic_array<stack_element*> stackElements;

		hash_map<void*, void*, detail::ptrHash, 8192u> manualMap;

		// Display
		HANDLE thread;

		HWND window;

		HWND labelAllocCount;
		HWND labelFreeCount;
		HWND labelOperationCount;

		HWND labelBlockCount;
		HWND labelByteCount;

		HWND labelSorting;

		HWND sortingSize;
		HWND sortingObjects;
		HWND sortingAlloc;
		HWND sortingFree;
		HWND sortingTotalAllocated;

		HWND buttonExpandAll;
		HWND buttonCollapseAll;

		HWND checkboxRefresh;

		HWND tree;

		detail::display_info displayMode;

		volatile bool dashboardExit;
	};

	DWORD CALLBACK detail::window_thread(LPVOID lpThreadParameter)
	{
		memory_dashboard *memoryMan = (memory_dashboard*)lpThreadParameter;

		memoryMan->window_create();

		return 0;
	}

	LRESULT CALLBACK detail::window_proc(HWND hWnd, DWORD message, WPARAM wParam, LPARAM lParam)
	{
		memory_dashboard *memoryMan = (memory_dashboard*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

		if(memoryMan)
			return memoryMan->window_message_handle(hWnd, message, wParam, lParam);

		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	int CALLBACK detail::tree_node_compare_func(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
	{
		memory_dashboard *memoryMan = (memory_dashboard*)lParamSort;

		return memoryMan->node_compare_func(lParam1, lParam2);
	}
}

extern "C" __declspec(dllexport) void* muitv_alloc(size_t size)
{
	return muitv::memory_dashboard::instance().malloc(size);
}

extern "C" __declspec(dllexport) void* muitv_calloc(size_t count, size_t size)
{
	return muitv::memory_dashboard::instance().calloc(count, size);
}

extern "C" __declspec(dllexport) void* muitv_realloc(void* ptr, size_t size)
{
	return muitv::memory_dashboard::instance().realloc(ptr, size);
}

extern "C" __declspec(dllexport) void muitv_free(void* ptr)
{
	muitv::memory_dashboard::instance().free(ptr);
}

extern "C" __declspec(dllexport) size_t muitv_get_size(void* ptr)
{
	return muitv::memory_dashboard::instance().get_size(ptr);
}

extern "C" __declspec(dllexport) void muitv_add_call_stack_to_tree(size_t size)
{
	muitv::memory_dashboard::instance().add_call_stack_to_tree(size);
}

extern "C" __declspec(dllexport) void muitv_add_object(void* ptr, size_t size)
{
	muitv::memory_dashboard::instance().add_object(ptr, size);
}

extern "C" __declspec(dllexport) void muitv_remove_object(void* ptr)
{
	muitv::memory_dashboard::instance().remove_object(ptr);
}
