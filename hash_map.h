#pragma once

#include <assert.h>

#include "internal.h"
#include "block_pool.h"

namespace muitv
{
	template<typename key, typename value, unsigned (*HashFunction)(const key&), bool (*CompareFunction)(const key&, const key&), unsigned buckets>
	class hash_map
	{
		static const unsigned bucketCount = buckets;
		static const unsigned bucketMask = bucketCount - 1;

	public:
		struct node
		{
			key key;
			unsigned hash;
			value value;
			node *next;
		};

		hash_map()
		{
			entries = detail::alloc<node*>(bucketCount);
			memset(entries, 0, sizeof(node*) * bucketCount);
			count = 0;
		}

		~hash_map()
		{
			detail::free(entries);
		}

		value* insert(const key& key, value value)
		{
			unsigned hash = HashFunction(key);
			unsigned bucket = hash & bucketMask;

			node *n = nodePool.allocate();

			n->key = key;
			n->value = value;
			n->hash = hash;
			n->next = entries[bucket];

			entries[bucket] = n;

			count++;

			return &n->value;
		}

		void remove(const key& key)
		{
			unsigned hash = HashFunction(key);
			unsigned bucket = hash & bucketMask;

			node *curr = entries[bucket], *prev = 0;

			while(curr)
			{
				if(curr->hash == hash && CompareFunction(curr->key, key))
					break;
				prev = curr;
				curr = curr->next;
			}

			assert(curr);

			if(prev)
				prev->next = curr->next;
			else
				entries[bucket] = curr->next;

			nodePool.free(curr);

			count--;
		}

		value* find(const key& key)
		{
			node *n = first(key);

			return n ? &n->value : 0;
		}

		node* first(const key& key)
		{
			unsigned hash = HashFunction(key);
			unsigned bucket = hash & bucketMask;

			node *curr = entries[bucket];

			while(curr)
			{
				if(curr->hash == hash && CompareFunction(curr->key, key))
					return curr;
				curr = curr->next;
			}

			return 0;
		}

		node* next(node* curr)
		{
			unsigned hash = curr->hash;
			const Key &key = curr->key;

			curr = curr->next;

			while(curr)
			{
				if(curr->hash == hash && CompareFunction(curr->key, key))
					return curr;
				curr = curr->next;
			}

			return 0;
		}

		unsigned size()
		{
			return count;
		}

	private:
		node **entries;
		unsigned count;

		block_pool<node, buckets> nodePool;
	};
}
