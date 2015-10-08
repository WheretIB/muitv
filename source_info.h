#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <dbghelp.h>

#include <cstring>

namespace muitv
{
	struct source_info
	{
		source_info()
		{
			displacement = 0;

			memset(&sourceInfo, 0, sizeof(sourceInfo));
		}

		DWORD displacement;
		IMAGEHLP_LINE64 sourceInfo;
	};
}
