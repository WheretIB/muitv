#pragma once

#include <cstdint>

namespace muitv
{
	struct memory_stats
	{
		memory_stats()
		{
			allocCount = 0;
			blocksCount = 0;
			bytesCount = 0;
			freeCount = 0;
			lastBlockNum = 0;
			memopsCount = 0;
		}

		size_t allocCount;
		size_t blocksCount;
		size_t bytesCount;
		size_t freeCount;
		size_t lastBlockNum;
		size_t memopsCount;
	};
}
