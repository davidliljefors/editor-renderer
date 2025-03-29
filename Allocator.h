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

template <typename T, typename... Args>
T* alloc(Allocator& allocator, Args&&... args)
{
	void* mem = allocator.alloc(sizeof(T));
	return new (mem) T(args...);
}

template <typename T>
void free(Allocator& allocator, T* obj)
{
	if (obj)
	{
		obj->~T();
		allocator.free(obj);
	}
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