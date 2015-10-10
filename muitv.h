#pragma once

#include <cstdint>
#include <cstdlib>

extern "C" __declspec(dllimport) void* muitv_alloc(size_t size);
extern "C" __declspec(dllimport) void* muitv_calloc(size_t count, size_t size);
extern "C" __declspec(dllimport) void* muitv_realloc(void* ptr, size_t size);
extern "C" __declspec(dllimport) void muitv_free(void* ptr);
extern "C" __declspec(dllimport) size_t muitv_get_size(void* ptr);
extern "C" __declspec(dllimport) void muitv_add_call_stack_to_tree(unsigned size);

#if !defined(MUITV_MANUAL_ONLY)
void* operator new(size_t size)
{
	return muitv_alloc(size);
}

void* operator new[](size_t size)
{
	return muitv_alloc(size);
}

void operator delete(void* ptr)
{
	muitv_free(ptr);
}

void operator delete[](void* ptr)
{
	muitv_free(ptr);
}
#endif

#if defined(OVERRIDE_CLIB_MALLOC)
extern "C" _declspec(dllimport) int __stdcall VirtualProtect(void* lpAddress, size_t dwSize, unsigned long flNewProtect, unsigned long* lpflOldProtect);

namespace muitv
{
	static inline void patch_with_jump(void* dest, void* address)
	{
		// get offset for relative jmp
		unsigned int offset = (unsigned int)((char*)address - (char*)dest - 5);

		// unprotect memory
		unsigned long old_protect;
		VirtualProtect(dest, 5, 0x04, &old_protect);

		// write jmp
		*(unsigned char*)dest = 0xe9;
		*(unsigned int*)((char*)dest + 1) = offset;

		// protect memory
		VirtualProtect(dest, 5, old_protect, &old_protect);
	}

	unsigned muitv_patch_clib_functions()
	{
		patch_with_jump((void*)malloc, (void*)muitv_alloc);
		patch_with_jump((void*)calloc, (void*)muitv_calloc);
		patch_with_jump((void*)realloc, (void*)muitv_realloc);
		patch_with_jump((void*)free, (void*)muitv_free);

		return 0;
	}

	static const unsigned muitv_patch_clib_functions_ready = muitv_patch_clib_functions();
}
#endif
