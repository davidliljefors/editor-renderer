#pragma once

#include "Core.h"
#include "Allocator.h"

struct Block
{
	static constexpr i32 PAGE_SIZE = 4096;
	static constexpr i32 BLOCK_SIZE = 16 * 256 * PAGE_SIZE;
	struct Header
	{
		Block* prev;
	};

	Header header;

	u8 data[BLOCK_SIZE-sizeof(Header)];
};

void block_memory_init();
void block_memory_shutdown();

struct TempAllocator : public Allocator
{
	TempAllocator();
	~TempAllocator() override;

	TempAllocator(const TempAllocator&) = delete;
	TempAllocator(TempAllocator&&) = delete;

	TempAllocator& operator=(const TempAllocator&) = delete;
	TempAllocator& operator=(TempAllocator&&) = delete;

	void* alloc(i32 size) override;
	void free(void* block) override;
	void freeSizeKnown(void* block, i32 size) override;
private:
	Block* m_current;
	i32 m_pos;
};
