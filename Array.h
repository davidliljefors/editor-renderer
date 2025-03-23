#pragma once

#include <new>
#include <string.h>

#include "Core.h"
#include "Allocator.h"

struct ArrayBase
{
	template<typename T>
	void push_back(Allocator& alloc, T val)
	{
		if (m_size == m_capacity)
			grow<T>(alloc);

		T* dataPtr = (T*)m_data;
		new (&dataPtr[m_size]) T(val);
		++m_size;
	}

	template<typename T>
	T* push_back_uninit()
	{
		if (m_size == m_capacity)
			grow<T>();

		T* dataPtr = (T*)m_data;
		return &m_data[dataPtr++];
	}

	template<typename T>
	void resize(Allocator& alloc, i32 new_size)
	{
		if (new_size > m_capacity)
		{
			reserve<T>(alloc, new_size);
		}

		if (new_size > m_size)
		{
			for (i32 i = m_size; i < new_size; ++i)
			{
				T* dataPtr = (T*)m_data;
				new (&dataPtr[i]) T();
			}
		}
		else if (new_size < m_size)
		{
			for (i32 i = new_size; i < m_size; ++i)
			{
				T* dataPtr = (T*)m_data;
				dataPtr[i].~T();
			}
		}

		m_size = new_size;
	}

	template<typename T>
	void reserve(Allocator& allocator, i32 new_capacity, i32 element_size)
	{
		if (new_capacity > m_capacity)
		{
			void* new_data = allocator.alloc(new_capacity * element_size);

			if (m_size > 0)
			{
				memcpy(new_data, m_data, m_size * element_size);
			}

			if (m_data)
			{
				allocator.freeSizeKnown(m_data, m_capacity * element_size);
			}

			m_data = new_data;
			m_capacity = new_capacity;
		}
	}

	template<typename T>
	void clear()
	{

	}

	template<typename T>
	void grow(Allocator& allocator)
	{
		i32 new_capacity = m_capacity < 1 ? 1 : (m_capacity * 2);
		void* new_data = allocator.alloc(new_capacity * sizeof(T));

		if (m_size > 0)
		{
			memcpy(new_data, m_data, m_size * sizeof(T));
		}
		if (m_data)
		{
			allocator.freeSizeKnown(m_data, m_capacity * sizeof(T));
		}

		m_data = new_data;
		m_capacity = new_capacity;
	}

	i32 size() const
	{
		return m_size;
	}

	template<typename T>
	T* data()
	{
		return (T*)m_data;
	}

	template<typename T>
	T& at(i32 i)
	{
		T* dataPtr = (T*)m_data;
		return dataPtr[i];
	}

	i32 m_size = 0;
	i32 m_capacity = 0;
	void* m_data = nullptr;
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

	void swap(Array<T>& other);

	void clear();
	bool empty() const;

	T& operator[](i32 i);
	const T& operator[](i32 i) const;

	T& at(i32 i);

	i32 size() const { return m_size; }
	i32 capacity() const { return m_capacity; }

	T* data() { return m_data; }
	
	T* begin();
	T* end();

	Allocator& get_allocator();

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
		m_allocator.freeSizeKnown(m_data, m_capacity * sizeof(T));
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
		T* new_data = (T*)m_allocator.alloc(sizeof(T) * new_capacity);

		if (m_size > 0)
		{
			memcpy(new_data, m_data, sizeof(T) * m_size);
		}

		if (m_data)
		{
			m_allocator.freeSizeKnown(m_data, sizeof(T) * m_capacity);
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
Allocator& Array<T>::get_allocator()
{
	return m_allocator;
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
	if (m_data)
	{
		m_allocator.freeSizeKnown(m_data, sizeof(T) * m_capacity);
	}
	m_data = new_data;
	m_capacity = new_capacity;
}
