#pragma once

#include "Core.h"

#include <new>
#include <stdlib.h>

struct Allocator
{
	virtual ~Allocator() = default;

	virtual void* alloc(i32 size) = 0;
	virtual void free(void* block) = 0;
	virtual void freeSizeKnown(void* block, i32 size) = 0;
};

inline void* operator new(size_t size, Allocator& allocator)
{
    return allocator.alloc(static_cast<i32>(size));
}

inline void* operator new[](size_t size, Allocator& allocator)
{
    return allocator.alloc(static_cast<i32>(size));
}

inline void operator delete(void* ptr, Allocator& allocator, size_t size)
{
    allocator.freeSizeKnown(ptr, static_cast<i32>(size));
}

inline void operator delete[](void* ptr, Allocator& allocator, size_t size)
{
    allocator.freeSizeKnown(ptr, static_cast<i32>(size));
}

class MallocAllocator : public Allocator
{
public:
	MallocAllocator() = default;
	~MallocAllocator() override = default;

	void* alloc(i32 size) override
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