#pragma once

#include <malloc.h>
#include <memory>

template <typename Ty>
class FixedPool
{
public:
	FixedPool()
	{
		m_maxItems = 0;
		m_buffer = nullptr;
		m_ptrs = nullptr;
		m_index = 0;
	}

	bool create(int _bucketSize)
	{
		m_maxItems = _bucketSize;

		size_t total_sz = sizeof(Ty)*m_maxItems + sizeof(Ty*)*m_maxItems;
		uint8_t* buff = (uint8_t*)malloc(total_sz);
		if(!buff)
			return false;
		memset(buff, 0x00, total_sz);

		m_buffer = (Ty*)buff;
		buff += sizeof(Ty)*m_maxItems;
		m_ptrs = (Ty**)buff;

		// Assign pointer references
		for(int i = 0, c = m_maxItems; i < c; i++)
			m_ptrs[c - i - 1] = &m_buffer[i];
		m_index = m_maxItems;

		return true;
	}

	void destroy()
	{
		if(m_buffer)
		{
			free(m_buffer);
			m_buffer = nullptr;
			m_ptrs = nullptr;
		}
		m_maxItems = 0;
		m_index = 0;
	}

	template <typename ... _ConstructorParams>
	Ty* newInstance(_ConstructorParams ... params)
	{
		if(m_index > 0)
			return new(m_ptrs[--m_index]) Ty(params ...);
		else
			return nullptr;
	}

	void deleteInstance(Ty* _inst)
	{
		m_ptrs[m_index++] = _inst;
	}

	void clear()
	{
		int bucketSize = m_maxItems;
		for(int i = 0; i < bucketSize; i++)
			m_ptrs[bucketSize - i - 1] = &m_buffer[i];
		m_index = bucketSize;
	}

	int getMaxItems() const { return m_maxItems; }

private:
	Ty* m_buffer;
	Ty** m_ptrs;
	int m_maxItems;
	int m_index;
};

template <typename Ty>
class Pool
{
public:
	Pool()
	{
		m_firstBucket = nullptr;
		m_numBuckets = 0;
		m_maxItemsPerBucket = 0;
	}

	bool create(int _bucketSize)
	{
		m_maxItemsPerBucket = _bucketSize;

		if(!createBucket())
			return false;

		return true;
	}

	void destroy()
	{
		Bucket* b = m_firstBucket;
		while(b)
		{
			Bucket* next = b->next;
			destroyBucket(b);
			b = next;
		}
	}

	template <typename ... _ConstructorParams>
	Ty* newInstance(_ConstructorParams ... params)
	{
		Bucket* b = m_firstBucket;
		while(b)
		{
			if(b->iter > 0)
				return new(b->ptrs[--b->iter]) Ty(params ...);
			b = b->next;
		}

		b = createBucket();
		if(!b)
			return nullptr;

		return new(b->ptrs[--b->iter]) Ty(params ...);
	}

	void deleteInstance(Ty* _inst)
	{
		_inst->~Ty();

		Bucket* b = m_firstBucket;
		size_t buffer_sz = sizeof(Ty)*m_maxItemsPerBucket;
		uint8_t* u8ptr = (uint8_t*)_inst;

		while(b)
		{
			if(u8ptr >= b->buffer && u8ptr < (b->buffer + buffer_sz))
			{
				b->ptrs[b->iter++] = _inst;
				return;
			}

			b = b->next;
		}
	}

	void clear()
	{
		int bucket_size = m_maxItemsPerBucket;

		Bucket* b = m_firstBucket;
		while(b)
		{
			// Re-assign pointer references
			for(int i = 0; i < bucket_size; i++)
				b->ptrs[bucket_size - i - 1] = (Ty*)b->buffer + i;
			b->iter = bucket_size;

			b = b->next;
		}
	}

	int getLeakCount() const
	{
		int count = 0;
		Bucket* b = m_firstBucket;
		int items_max = m_maxItemsPerBucket;
		while(b)
		{
			count += (items_max - b->iter);
			b = b->next;
		}
		return count;
	}

private:
	struct Bucket
	{
		Bucket* prev;
		Bucket* next;
		uint8_t* buffer;
		Ty** ptrs;
		int iter;
	};

private:
	Bucket * createBucket()
	{
		size_t total_sz = sizeof(Bucket) + sizeof(Ty)*m_maxItemsPerBucket + sizeof(void*)*m_maxItemsPerBucket;
		uint8_t* buff = (uint8_t*)malloc(total_sz);
		if(!buff)
			return nullptr;
		memset(buff, 0x00, total_sz);

		Bucket* bucket = (Bucket*)buff;
		buff += sizeof(Bucket);
		bucket->buffer = buff;
		buff += sizeof(Ty)*m_maxItemsPerBucket;
		bucket->ptrs = (Ty**)buff;

		// Assign pointer references
		for(int i = 0, c = m_maxItemsPerBucket; i < c; i++)
			bucket->ptrs[c - i - 1] = (Ty*)bucket->buffer + i;
		bucket->iter = m_maxItemsPerBucket;

		// Add to linked-list
		bucket->next = bucket->prev = nullptr;
		if(m_firstBucket)
		{
			m_firstBucket->prev = bucket;
			bucket->next = m_firstBucket;
		}
		m_firstBucket = bucket;

		m_numBuckets++;
		return bucket;
	}

	void destroyBucket(Bucket* bucket)
	{
		// Remove from linked-list
		if(m_firstBucket != bucket)
		{
			if(bucket->next)
				bucket->next->prev = bucket->prev;
			if(bucket->prev)
				bucket->prev->next = bucket->next;
		}
		else
		{
			if(bucket->next)
				bucket->next->prev = nullptr;
			m_firstBucket = bucket->next;
		}

		//
		free(bucket);
		m_numBuckets--;
	}

private:
	int m_maxItemsPerBucket;
	int m_numBuckets;
	Bucket* m_firstBucket;
};