#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <dbghelp.h>

#include <cstring>

#include "hash_map.h"
#include "function_info.h"
#include "source_info.h"

namespace muitv
{
	namespace detail
	{
		inline unsigned d64Hash(const DWORD64& value)
		{
			return unsigned(value >> 4);
		}

		inline bool d64Compare(const DWORD64& lhs, const DWORD64& rhs)
		{
			return lhs == rhs;
		}
	}

	struct symbol_info
	{
		symbol_info()
		{
			process = GetCurrentProcess();

			SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
	
			SymInitialize(process, 0, true);
		}

		~symbol_info()
		{
			SymCleanup(process);
		}

		static symbol_info& instance()
		{
			static symbol_info inst;

			return inst;
		}

		function_info* get_function_info(void* ptr)
		{
			DWORD64 w = reinterpret_cast<DWORD64>(ptr);

			if(DWORD64 *remap = addressRemap.find(w))
			{
				if(function_info *target = functionMap.find(*remap))
					return target;
			}

			function_info info;
			memset(info.functionInfo, 0, function_info::SYMBOLBUFFERSIZE);
			info.functionInfo->SizeOfStruct = sizeof(SYMBOL_INFO);
			info.functionInfo->MaxNameLen = function_info::MAXSYMBOLNAMELENGTH;

			if(SymFromAddr(process, w, &info.displacement64, info.functionInfo))
			{
				DWORD64 remap = info.functionInfo->Address;

				addressRemap.insert(w, remap);

				if(function_info *target = functionMap.find(remap))
					return target;

				functionMap.insert(remap, info);

				return functionMap.find(remap);
			}

			return 0;
		}

		source_info* get_source_info(void* ptr)
		{
			DWORD64 w = reinterpret_cast<DWORD64>(ptr);

			if(source_info *info = sourceMap.find(w))
				return info;

			source_info info;
			memset(&info.sourceInfo, 0, sizeof(IMAGEHLP_LINE64));
			info.sourceInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

			if(SymGetLineFromAddr64(process, w, &info.displacement, &info.sourceInfo))
			{
				sourceMap.insert(w, info);
			}

			return sourceMap.find(w);
		}

		HANDLE process;

		hash_map<DWORD64, DWORD64, detail::d64Hash, detail::d64Compare, 32 * 1024> addressRemap;
		hash_map<DWORD64, function_info, detail::d64Hash, detail::d64Compare, 32 * 1024> functionMap;
		hash_map<DWORD64, source_info, detail::d64Hash, detail::d64Compare, 32 * 1024> sourceMap;
	};
}
