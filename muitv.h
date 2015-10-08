#pragma once

#include <cstdint>

extern "C" __declspec(dllimport) void* muitv_alloc(size_t size);
extern "C" __declspec(dllimport) void muitv_free(void* ptr);

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
