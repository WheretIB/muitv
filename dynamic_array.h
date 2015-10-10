#pragma once

#include <assert.h>

#include "internal.h"

namespace muitv
{
	template<typename T>
	struct dynamic_array
	{
		dynamic_array()
		{
			data = 0;
			count = 0;
			limit = 0;
		}

		~dynamic_array()
		{
			detail::free(data);
		}

		T* begin()
		{
			return data;
		}

		T* end()
		{
			return data + count;
		}

		bool empty()
		{
			return count == 0;
		}

		size_t size()
		{
			return count;
		}

		T& operator[](size_t index)
		{
			assert(index < count);

			return data[index];
		}

		void push_back(const T& elem)
		{
			if(count == limit)
				grow(count);

			data[count++] = elem;
		}

		void grow(size_t nextLimit)
		{
			if(limit + (limit >> 1) > nextLimit)
				nextLimit = limit + (limit >> 1);
			else
				nextLimit += 4;

			T* nextData = detail::alloc<T>(nextLimit);

			memcpy(nextData, data, limit * sizeof(T));

			detail::free(data);

			data = nextData;
			limit = nextLimit;
		}

		T *data;
		size_t count;
		size_t limit;

	private:
		dynamic_array(const dynamic_array& rhs);
		dynamic_array& operator=(const dynamic_array& rhs);
	};
}
