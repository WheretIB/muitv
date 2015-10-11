#pragma once

#include <algorithm>

#include "function_info.h"
#include "dynamic_array.h"

namespace muitv
{
	struct function_info;

	struct stack_element
	{
		stack_element()
		{
			pos = ~0u;

			updateMark = ~0u;

			fInfo = 0;

			objectCount = 0;
			objectSize = 0;

			allocCount = 0;
			freeCount = 0;

			allocSize = 0;
		}

		size_t pos; // Position in the global array

		size_t updateMark;

		function_info *fInfo;

		size_t objectCount; // How many objects are alive at this path segment
		size_t objectSize; // How much memory is alive at this path segment

		// How many operations were performed at this path segment (note that an object is deallocated from the same path it was allocated
		size_t allocCount;
		size_t freeCount;

		// How much memory in total was allocated at this path segment
		unsigned long long allocSize;

		dynamic_array<stack_element*> children;

		const char* get_name()
		{
			if(fInfo)
				return fInfo->functionInfo->Name;

			return "`unknown`";
		}

		typedef bool (*sort_func)(const stack_element* lhs, const stack_element* rhs);

		void sort_children(sort_func sortFunction)
		{
			std::sort(children.begin(), children.end(), sortFunction);

			for(size_t i = 0; i < children.size(); i++)
				children[i]->sort_children(sortFunction);
		}

		friend bool compare_object_count(const stack_element* lhs, const stack_element* rhs)
		{
			return lhs->objectCount > rhs->objectCount;
		}

		friend bool compare_object_size(const stack_element* lhs, const stack_element* rhs)
		{
			return lhs->objectSize > rhs->objectSize;
		}

		friend bool compare_alloc_count(const stack_element* lhs, const stack_element* rhs)
		{
			return lhs->allocCount > rhs->allocCount;
		}

		friend bool compare_alloc_size(const stack_element* lhs, const stack_element* rhs)
		{
			return lhs->allocSize > rhs->allocSize;
		}

		friend bool compare_free_count(const stack_element* lhs, const stack_element* rhs)
		{
			return lhs->freeCount > rhs->freeCount;
		}
	};
}
