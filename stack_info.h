#pragma once

#include <cstdint>

namespace muitv
{
	struct stack_info
	{
		stack_info()
		{
			stackSize = 0;
			stackInfo = 0;
		}

		size_t stackSize;
		void** stackInfo;
	};
}
