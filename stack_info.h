#pragma once

namespace muitv
{
	struct stack_info
	{
		stack_info()
		{
			stackSize = 0;
			stackInfo = 0;
		}

		unsigned stackSize;
		void** stackInfo;
	};
}
