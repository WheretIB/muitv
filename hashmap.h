#pragma once

#include <assert.h>

#include "blockpool.h"

namespace muitv
{
	template<typename Key, typename Value, unsigned (*HashFunction)(const Key&), bool (*CompareFunction)(const Key&, const Key&), unsigned buckets>
	class hashmap
	{
		static const unsigned bucketCount = buckets;
		static const unsigned bucketMask = bucketCount - 1;

	public:
		struct Node
		{
			Key key;
			unsigned hash;
			Value value;
			Node *next;
		};

		hashmap()
		{
			entries = new Node*[bucketCount];
			memset(entries, 0, sizeof(Node*) * bucketCount);
			count = 0;
		}

		~hashmap()
		{
			delete[] entries;
		}

		Value* insert(const Key& key, Value value)
		{
			unsigned hash = HashFunction(key);
			unsigned bucket = hash & bucketMask;

			Node *n = nodePool.Allocate();
			n->key = key;
			n->value = value;
			n->hash = hash;
			n->next = entries[bucket];
			entries[bucket] = n;

			count++;

			return &n->value;
		}

		void remove(const Key& key)
		{
			unsigned hash = HashFunction(key);
			unsigned bucket = hash & bucketMask;

			Node *curr = entries[bucket], *prev = 0;

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

			nodePool.Deallocate(curr);

			count--;
		}

		Value* find(const Key& key)
		{
			Node *n = first(key);
			return n ? &n->value : 0;
		}

		Node* first(const Key& key)
		{
			unsigned hash = HashFunction(key);
			unsigned bucket = hash & bucketMask;
			Node *curr = entries[bucket];
			while(curr)
			{
				if(curr->hash == hash && CompareFunction(curr->key, key))
					return curr;
				curr = curr->next;
			}
			return 0;
		}

		Node* next(Node* curr)
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
		Node **entries;
		unsigned count;

		blockpool<Node, buckets> nodePool;
	};
}
