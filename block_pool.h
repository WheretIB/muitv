#pragma once

#include <new>
#include <cstdint>

#include "internal.h"

namespace muitv
{
	template<typename T, unsigned countInBlock>
	class block_pool
	{
		union small_block
		{
			char data[sizeof(T)];
			small_block *next;
		};

		struct large_block
		{
			small_block page[countInBlock];
			large_block *next;
		};

	public:
		block_pool()
		{
			freeBlocks = 0;
			activePages = 0;
			lastNum = countInBlock;
		}

		~block_pool()
		{
			while(activePages)
			{
				large_block *following = activePages->next;

				detail::free(activePages);

				activePages = following;
			}
		}

		T* allocate()
		{
			small_block *result = 0;

			if(freeBlocks)
			{
				result = freeBlocks;
				freeBlocks = freeBlocks->next;
			}
			else
			{
				if(lastNum == countInBlock)
				{
					large_block *nextPage = detail::alloc<large_block>();

					nextPage->next = activePages;
					activePages = nextPage;

					lastNum = 0;
				}

				result = &activePages->page[lastNum++];
			}

			return new (result) T;
		}

		void free(T* ptr)
		{
			if(!ptr)
				return;

			small_block *freedBlock = (small_block*)(void*)ptr;

			ptr->~T();

			freedBlock->next = freeBlocks;
			freeBlocks = freedBlock;
		}

	private:
		small_block *freeBlocks;
		large_block *activePages;
		size_t lastNum;
	};
}
