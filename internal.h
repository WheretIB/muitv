#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace muitv
{
	namespace detail
	{
		struct internal_heap
		{
			internal_heap()
			{
				heap = HeapCreate(0, 1024 * 1024, 0);
			}

			~internal_heap()
			{
				HeapDestroy(heap);
			}

			static internal_heap& instance()
			{
				static internal_heap inst;

				return inst;
			}

			HANDLE heap;
		};

		template<typename T>
		T* alloc()
		{
			return (T*)HeapAlloc(internal_heap::instance().heap, HEAP_ZERO_MEMORY, sizeof(T));
		}

		template<typename T>
		T* alloc(size_t count)
		{
			return (T*)HeapAlloc(internal_heap::instance().heap, HEAP_ZERO_MEMORY, sizeof(T) * count);
		}

		void free(void* ptr)
		{
			HeapFree(internal_heap::instance().heap, 0, ptr);
		}
	}
}
