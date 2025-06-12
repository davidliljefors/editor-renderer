#pragma once

#include <assert.h>
#include <new>
#include <string.h>

#include "Allocator.h"
template<typename T>
class Array
{
public:
	template<typename U>
	static auto test_clone(U* p) -> decltype(p->clone(), char(0)) { return 0; }
	static char(&test_clone(...))[2] { static char arr[2] = {}; return arr; }

	static constexpr bool has_clone = sizeof(test_clone((T*)0)) == 1;

	explicit Array();
	explicit Array(Allocator* allocator);
	~Array();

	Array(Array&& r)
		:m_allocator(r.m_allocator)
	{
		m_data = r.m_data;
		m_size = r.m_size;
		m_capacity = r.m_capacity;

		r.m_data = nullptr;
		r.m_size = 0;
		r.m_capacity = 0;
	}

	Array& operator=(Array&& rhs)
	{ 
		if (&rhs != this)
		{
			m_data = rhs.m_data;
			m_size = rhs.m_size;
			m_capacity = rhs.m_capacity;
			m_allocator = rhs.m_allocator;

			rhs.m_data = nullptr;
			rhs.m_allocator = nullptr;
			rhs.m_size = 0;
			rhs.m_capacity = 0;
		}

		return *this;
	}

	void set_allocator(Allocator* a)
	{
		assert(m_allocator == nullptr);

		m_allocator = a;
	}

	Array clone() const;

	void push_back(T val);
	T& back();

	void* push_back_uninit();

	void resize(i32 new_size);
	void reserve(i32 new_capacity);

	void swap(Array<T>& other);

	void clear();
	bool empty() const;

	T& operator[](i32 i);
	const T& operator[](i32 i) const;

	T& at(i32 i);

	i32 size() const { return m_size; }
	i32 capacity() const { return m_capacity; }

	T* data() { return m_data; }
	const T* data() const { return m_data; }
	
	T* begin();
	T* end();

	const T* begin() const;
	const T* end() const;

	Allocator* get_allocator() const;

private:
	void grow();
private:
	Allocator* m_allocator = nullptr;

	T* m_data = nullptr;
	i32 m_size = 0;
	i32 m_capacity = 0;
};

template <typename T>
Array<T>::Array()
{

}

template <typename T>
Array<T>::Array(Allocator* allocator)
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
		m_allocator->freeSizeKnown(m_data, m_capacity * sizeof(T));
	}

	m_capacity = 0;
	m_size = 0;
}

template <typename T>
Array<T> Array<T>::clone() const
{
	Array<T> copy(m_allocator);
	copy.resize(size());

	for (i32 i = 0; i < size(); ++i)
	{
		if constexpr(has_clone)
		{
			copy.m_data[i] = m_data[i].clone();
		}
		else
		{
			copy.m_data[i] = m_data[i];
		}
	}

	return copy;
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
	return m_data[m_size - 1];
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
		T* new_data = (T*)m_allocator->alloc(sizeof(T) * new_capacity);

		if (m_size > 0)
		{
			memcpy(new_data, m_data, sizeof(T) * m_size);
		}

		if (m_data)
		{
			m_allocator->freeSizeKnown(m_data, sizeof(T) * m_capacity);
		}

		m_data = new_data;
		m_capacity = new_capacity;
	}
}

template <typename T>
void Array<T>::swap(Array<T>& other)
{
	T* temp_data = m_data;
	m_data = other.m_data;
	other.m_data = temp_data;

	i32 temp_size = m_size;
	m_size = other.m_size;
	other.m_size = temp_size;

	i32 temp_capacity = m_capacity;
	m_capacity = other.m_capacity;
	other.m_capacity = temp_capacity;
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
bool Array<T>::empty() const
{
	return m_size == 0;
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
T& Array<T>::at(i32 i)
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
const T* Array<T>::begin() const
{
	return m_data;
}

template <typename T>
const T* Array<T>::end() const
{
	return m_data + m_size;
}

template <typename T>
Allocator* Array<T>::get_allocator() const
{
	return m_allocator;
}

template <typename T>
void Array<T>::grow()
{
	i32 new_capacity = m_capacity < 1 ? 1 : (m_capacity * 2);

	T* new_data = (T*)m_allocator->alloc(sizeof(T) * new_capacity);

	if (m_size > 0)
	{
		memcpy(new_data, m_data, sizeof(T) * m_size);
	}
	if (m_data)
	{
		m_allocator->freeSizeKnown(m_data, sizeof(T) * m_capacity);
	}
	m_data = new_data;
	m_capacity = new_capacity;
}
