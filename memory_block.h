#pragma once

#include "stack_info.h"

namespace muitv
{
	struct memory_block
	{
		size_t blockNum;
		size_t blockSize;
		stack_info stackInfo;
		void* blockInfoStart;
	};
}
