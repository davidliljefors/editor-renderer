#include "ScratchAllocator.h"

#include <string.h>

static Block* s_freeBlocks = nullptr;

void return_block(Block* block)
{
    Block* last = block;
    
    for(;;)
    {
        if(last->header.prev)
        {
            last = last->header.prev;
        }
        else
        {
            break;
        }
    }

    last->header.prev = s_freeBlocks;
    s_freeBlocks = block;
}

Block* get_block()
{
    Block* block = s_freeBlocks;
    if(block)
    {
        s_freeBlocks = block->header.prev;
        block->header.prev = nullptr;
        return block;
    }
    else 
    {
        block = (Block*)::malloc(sizeof(Block));
        block->header.prev = nullptr;
        return block;
    }
}

void block_memory_init()
{
    for(i32 i = 0; i < 8; i++)
    {
        Block* block = (Block*)::malloc(sizeof(Block));
        memset(block, 0, sizeof(Block));
        block->header.prev = s_freeBlocks;
        s_freeBlocks = block;
    }
}

void block_memory_shutdown()
{
    Block* block = s_freeBlocks;
    while(block)
    {
        Block* next = block->header.prev;
        ::free(block);
        block = next;
    }
}

TempAllocator::TempAllocator()
{
    m_current = get_block();
    m_pos = 0;
}

TempAllocator::~TempAllocator()
{
    return_block(m_current);
}

void* TempAllocator::alloc(i32 size)
{
   i32 size_with_alignment = size + 16 - (size & 15);

    if(size_with_alignment > Block::BLOCK_SIZE)
    {
        return nullptr;
    }

    if(size_with_alignment + m_pos > Block::BLOCK_SIZE)
    {
        Block* next = get_block();
        next->header.prev = m_current;
        m_current = next;
        m_pos = 0;
        return alloc(size);
    }
    else
    {
        i32 pos = m_pos;
        m_pos += size_with_alignment;
        return (void*)&m_current->data[pos];
    }
}

void TempAllocator::free(void*)
{
    // Do nothing
}


void TempAllocator::freeSizeKnown(void*, i32)
{
    // Do nothing
}