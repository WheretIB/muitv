#pragma once

#include <new>

namespace muitv
{
	template<typename T, unsigned countInBlock>
	class blockpool
	{
		union SmallBlock
		{
			char data[sizeof(T)];
			SmallBlock *next;
		};

		struct LargeBlock
		{
			SmallBlock page[countInBlock];
			LargeBlock *next;
		};

	public:
		blockpool()
		{
			freeBlocks = 0;
			activePages = 0;
			lastNum = countInBlock;
		}

		~blockpool()
		{
			while(activePages)
			{
				LargeBlock *following = activePages->next;

				delete activePages;

				activePages = following;
			}
		}

		T* Allocate()
		{
			SmallBlock *result = 0;

			if(freeBlocks)
			{
				result = freeBlocks;
				freeBlocks = freeBlocks->next;
			}
			else
			{
				if(lastNum == countInBlock)
				{
					LargeBlock *newPage = new LargeBlock();

					newPage->next = activePages;
					activePages = newPage;

					lastNum = 0;
				}

				result = &activePages->page[lastNum++];
			}

			return new (result) T;
		}

		void Deallocate(T* ptr)
		{
			if(!ptr)
				return;

			SmallBlock *freedBlock = (SmallBlock*)(void*)ptr;

			ptr->~T();

			freedBlock->next = freeBlocks;
			freeBlocks = freedBlock;
		}

	private:
		SmallBlock *freeBlocks;
		LargeBlock *activePages;
		unsigned lastNum;
	};
}
