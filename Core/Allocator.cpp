#include "Allocator.h"

#include "stdlib.h"

void* HeapAllocator::alloc(size_t size)
{
	return malloc(size);
}

void HeapAllocator::free(void* block)
{
	free(block);
}

void HeapAllocator::freeSizeKnown(void* block, i32)
{
	free(block);
}
