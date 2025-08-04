#pragma once

using i32 = int;

struct Allocator
{
	virtual ~Allocator() = default;

	virtual void* alloc(size_t size) = 0;
	virtual void free(void* block) = 0;
	virtual void freeSizeKnown(void* block, i32 size) = 0;
};

#ifndef __PLACEMENT_NEW_INLINE
#define __PLACEMENT_NEW_INLINE
inline void* operator new(size_t, void* ptr) noexcept 
{
    return ptr;
}
#endif

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

class HeapAllocator : public Allocator
{
public:
	HeapAllocator() = default;
	~HeapAllocator() override = default;

	void* alloc(size_t size) override;
	void free(void* block) override;
	void freeSizeKnown(void* block, i32) override;
};

extern Allocator* GLOBAL_HEAP;