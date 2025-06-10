#pragma once

#include "Types.h"
#include "Allocator.h"

class LinearAllocator :  public Allocator
{
public:
	LinearAllocator(void* mem, i64 size)
	{
		m_mem = (uintptr_t)mem;
		m_size = size;
		m_cur = size;
	}

	~LinearAllocator() override = default;
	
	void* alloc(i32 size) override
	{
		size = (size + 7) & ~7;
		m_cur -= size;
		
		if(m_cur < 0)
		{
			__debugbreak();
		}

		return (void*)(m_mem + m_cur);
	}

	void free(void*) override
	{
		
	}

	void freeSizeKnown(void*, i32) override
	{

	}

	void reset()
	{
		m_cur = m_size;
	}
private:
	uintptr_t m_mem;
	i64 m_cur;
	i64 m_size;
};
