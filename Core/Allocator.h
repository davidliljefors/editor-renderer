#pragma once


#include <new>
#include <stdlib.h>

using i32 = int;

struct Allocator
{
	virtual ~Allocator() = default;

	virtual void* alloc(size_t size) = 0;
	virtual void free(void* block) = 0;
	virtual void freeSizeKnown(void* block, i32 size) = 0;
};

inline void* operator new(size_t size, Allocator* allocator)
{
	void* ptr = allocator->alloc((i32)size);
	return ptr;
}

inline void operator delete(void* ptr, Allocator* allocator) noexcept
{
	allocator->free(ptr);
}

inline void* operator new(size_t size, Allocator& allocator)
{
	void* ptr = allocator.alloc((i32)size);
	return ptr;
}

inline void operator delete(void* ptr, Allocator& allocator) noexcept
{
	allocator.free(ptr);
}

template <typename T, typename... Args>
T* alloc(Allocator* allocator)
{
	void* mem = allocator->alloc(sizeof(T));
	return new (mem) T();
}

template <typename T, typename... Args>
T* create(Allocator* allocator, Args&&... args)
{
	void* mem = allocator->alloc(sizeof(T));
	return new (mem) T(args...);
}

template <typename T>
void destroy(Allocator& allocator, T* obj)
{
	if (obj)
	{
		obj->~T();
		allocator.freeSizeKnown(obj, sizeof(T));
	}
}

class HeapAllocator : public Allocator
{
public:
	HeapAllocator() = default;
	~HeapAllocator() override = default;

	void* alloc(size_t size) override
	{
		return ::malloc(size);
	}

	void free(void* block) override
	{
		return ::free(block);
	}

	void freeSizeKnown(void* block, i32) override
	{
		return ::free(block);
	}
};

extern Allocator* GLOBAL_HEAP;