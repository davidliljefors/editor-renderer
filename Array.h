#pragma once

#include <string.h>
#include <new>

#include "Core.h"

struct Allocator
{
	virtual ~Allocator() = default;

	virtual void* alloc(i32 size) = 0;
	virtual void free(void* block, i32 size) = 0;
};

class MallocAllocator : public Allocator
{
public:
	MallocAllocator() = default;
	~MallocAllocator() override = default;

	void* alloc(i32 size) override
	{
		return ::malloc(size);
	}

	void free(void* block, i32 size) override
	{
		return ::free(block);
	}
};


template<typename T>
class Array
{
public:
	explicit Array(Allocator& allocator);
	~Array();

	void push_back(T val);
	T& back();

	void* push_back_uninit();

	void resize(i32 new_size);
	void reserve(i32 new_capacity);


	void clear();

	T& operator[](i32 i);
	const T& operator[](i32 i) const;


	i32 size() const { return m_size; }
	T* data() { return m_data; }

	T* begin();
	T* end();

private:
	void grow();
private:
	Allocator& m_allocator;

	T* m_data = nullptr;
	i32 m_size = 0;
	i32 m_capacity = 0;
};

template <typename T>
Array<T>::Array(Allocator& allocator)
	: m_allocator(allocator)
{

}

template <typename T>
Array<T>::~Array()
{
	for (i32 i = 0; i < m_size; ++i)
	{
		m_data[i].~T();
	}

	if (m_data)
	{
		m_allocator.free(m_data, m_capacity * sizeof(T));
	}

	m_capacity = 0;
	m_size = 0;
}

template <typename T>
void Array<T>::push_back(T val)
{
	if (m_size == m_capacity)
		grow();

	new (&m_data[m_size]) T(val);
	++m_size;
}

template <typename T>
T& Array<T>::back()
{
	return m_data[m_size-1];
}

template <typename T>
void* Array<T>::push_back_uninit()
{
	if (m_size == m_capacity)
		grow();

	return &m_data[m_size++];
}

template <typename T>
void Array<T>::resize(i32 new_size)
{
	if (new_size > m_capacity)
	{
		reserve(new_size);
	}

	if (new_size > m_size)
	{
		for (i32 i = m_size; i < new_size; ++i)
		{
			new (&m_data[i]) T();
		}
	}
	else if (new_size < m_size)
	{
		for (i32 i = new_size; i < m_size; ++i)
		{
			m_data[i].~T();	
		}
	}

	m_size = new_size;
}

template <typename T>
void Array<T>::reserve(i32 new_capacity)
{
	if (new_capacity > m_capacity)
	{
		T* new_data = (T*)m_allocator.alloc(sizeof(T) * new_capacity);

		if (m_size > 0)
		{
			memcpy(new_data, m_data, sizeof(T) * m_size);
		}

		if(m_data)
		{
			m_allocator.free(m_data, sizeof(T) * m_capacity);
		}

		m_data = new_data;
		m_capacity = new_capacity;
	}
}

template <typename T>
void Array<T>::clear()
{
	for (i32 i = 0; i < m_size; ++i)
	{
		m_data[i].~T();
	}

	m_size = 0;
}

template <typename T>
T& Array<T>::operator[](i32 i)
{
	return m_data[i];
}

template <typename T>
const T& Array<T>::operator[](i32 i) const
{
	return m_data[i];
}

template <typename T>
T* Array<T>::begin()
{
	return m_data;
}

template <typename T>
T* Array<T>::end()
{
	return m_data + m_size;
}

template <typename T>
void Array<T>::grow()
{
	i32 new_capacity = m_capacity < 1 ? 1 : (m_capacity * 2);
	
	T* new_data = (T*)m_allocator.alloc(sizeof(T) * new_capacity);

	if (m_size > 0)
	{
		memcpy(new_data, m_data, sizeof(T) * m_size);
	}
	if(m_data)
	{
		m_allocator.free(m_data, sizeof(T) * m_capacity);
	}
	m_data = new_data;
	m_capacity = new_capacity;
}
