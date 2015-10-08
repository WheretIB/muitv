#pragma once

#include <new>

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

				delete activePages;

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
					large_block *newPage = new large_block();

					newPage->next = activePages;
					activePages = newPage;

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
		unsigned lastNum;
	};
}
