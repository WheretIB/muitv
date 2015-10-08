#pragma once

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

		unsigned allocCount;
		unsigned blocksCount;
		unsigned bytesCount;
		unsigned freeCount;
		unsigned lastBlockNum;
		unsigned memopsCount;
	};
}
