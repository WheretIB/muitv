#pragma once

#include <cstdint>

extern "C" __declspec(dllimport) void* muitv_alloc(size_t size);
extern "C" __declspec(dllimport) void* muitv_calloc(size_t count, size_t size);
extern "C" __declspec(dllimport) void* muitv_realloc(void* ptr, size_t size);
extern "C" __declspec(dllimport) void muitv_free(void* ptr);
extern "C" __declspec(dllimport) size_t muitv_get_size(void* ptr);

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
