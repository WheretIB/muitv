#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <dbghelp.h>

#include <cstring>

namespace muitv
{
	struct function_info
	{
		function_info()
		{
			displacement64 = 0;

			functionInfo = (SYMBOL_INFO*)symbolBuffer;
		}

		function_info(const function_info& rhs)
		{
			memcpy(this, &rhs, sizeof(function_info));

			functionInfo = (SYMBOL_INFO*)symbolBuffer;
		}

		function_info& operator=(const function_info& rhs)
		{
			memcpy(this, &rhs, sizeof(function_info));

			functionInfo = (SYMBOL_INFO*)symbolBuffer;

			return *this;
		}

		static const size_t MAXSYMBOLNAMELENGTH = 512;
		static const size_t SYMBOLBUFFERSIZE = sizeof(SYMBOL_INFO) + MAXSYMBOLNAMELENGTH - 1;

		unsigned char symbolBuffer[SYMBOLBUFFERSIZE];

		DWORD64 displacement64;
		SYMBOL_INFO *functionInfo;
	};
}
