#pragma once

#include <string.h>

using u8 = unsigned char;
using u32 = unsigned int;
using u64 = unsigned long long;

struct forward_iterator_tag {};

constexpr u64 next_power_of_2(u64 n)
{
	if (n == 0) return 1;

	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n |= n >> 32;

	return n+1;
}

constexpr u64 power_of_2(u64 n)
{
	if (n == 0) return 1;

	--n;

	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n |= n >> 32;

	return n+1;
}

enum ControlByte : u8
{
	EMPTY = 0x80,
	DELETED = 0xFE,
};

template<typename T>
class SwissTable
{
	struct Entry
	{
		u64 key;
		T value;
	};

	struct Iterator
	{
		Entry* m_data;
		u8* m_control;
		u32 m_index;
		u32 m_capacity;

		Iterator(Entry* pCurrent, u8* pControl, u32 index, u32 capacity)
			: m_data(pCurrent)
			, m_control(pControl)
			, m_index(index)
			, m_capacity(capacity)
		{

		}

		void advance()
		{
			u32 index = m_index + 1;
			while (index < m_capacity)
			{
				if (m_control[index] != EMPTY && m_control[index] != DELETED)
				{
					m_index = index;
					return;
				}
				index++;
			}
            m_index = m_capacity;
		}

		Iterator& operator++() { advance(); return *this; }
		T& operator*() { return m_data[m_index].value; }
		bool operator!=(const Iterator& r) const { return m_data != r.m_data && m_index != r.m_index; }
	};

	constexpr static float MAX_LOAD_FACTOR = 0.7f;

public:
	explicit SwissTable(u32 initialCapacity = 16);
	~SwissTable();

	SwissTable(const SwissTable& r) = delete;
	SwissTable(SwissTable&& r);

	SwissTable& operator=(const SwissTable& r) = delete;
	SwissTable& operator=(SwissTable&& r);

	void insert(u64 key, const T& value);

	void* insert_uninitialized(u64 key);

	void insert_or_assign(u64 key, const T& value);

	T* find(u64 key);

	void erase(u64 key);

	Iterator begin()
	{
        u32 index = 0;
        while (index < capacity)
        {
            if (control[index] != EMPTY && control[index] != DELETED)
            {
                break;
            }
            index++;
        }

        return Iterator(data, control, index, capacity);
	}

	Iterator end()
	{
		return Iterator(data, control, capacity, capacity);
	}

	Iterator begin() const
	{
		for (u32 i = 0; i < capacity; ++i)
		{
			if (control[i] != EMPTY && control[i] != DELETED)
			{
				return Iterator(&data[i], control, i, capacity);
			}
		}
		return end();
	}

	Iterator end() const
	{
		return Iterator(nullptr, control, capacity, capacity);
	}

private:
	void init(u32 newCapacity);

	__forceinline u32 hash(u64 key) const;

	__forceinline u32 probe(u32 index, u32 step) const;

	u32 find_slot(u64 key) const;

	void rehash(u32 newCapacity);

public:
	u8* control;
	Entry* data;
	u32 size;
	u32 capacity;
};

template <typename T>
SwissTable<T>::SwissTable(u32 initialCapacity)
{
	init(power_of_2(initialCapacity));
}

template <typename T>
SwissTable<T>::~SwissTable()
{
	delete [] data;
	delete [] control;
}

template <typename T>
SwissTable<T>::SwissTable(SwissTable&& r)
{
	if (this != &r)
	{
		data = r.data;
		control = r.control;
		capacity = r.capacity;
		size = r.size;


		r.control = nullptr;
		r.data = nullptr;
		r.size = 0;
		r.capacity = 0;
	}
}

template <typename T>
SwissTable<T>& SwissTable<T>::operator=(SwissTable&& r)
{
	if (this != &r)
	{
		delete [] control;
		delete [] data;

		data = r.data;
		control = r.control;
		capacity = r.capacity;
		size = r.size;


		r.control = nullptr;
		r.data = nullptr;
		r.size = 0;
		r.capacity = 0;
	}

	return *this;
}

template <typename T>
void SwissTable<T>::insert(u64 key, const T& value)
{
	if (size >= capacity * MAX_LOAD_FACTOR)
		rehash(next_power_of_2(capacity));

	u32 index = find_slot(key);

	if (control[index] == EMPTY || control[index] == DELETED)
	{
		control[index] = key & 0x7F;
		data[index] = { key, value };
		size++;
	}
}

template <typename T>
void* SwissTable<T>::insert_uninitialized(u64 key)
{
	if (size >= capacity * MAX_LOAD_FACTOR)
		rehash(next_power_of_2(capacity));

	u32 index = find_slot(key);

	if (control[index] == EMPTY || control[index] == DELETED)
	{
		control[index] = key & 0x7F;
		size++;
		return &data[index];
	}

	return nullptr;
}

template <typename T>
void SwissTable<T>::insert_or_assign(u64 key, const T& value)
{
	if (size >= capacity * 0.7)
		rehash(next_power_of_2(capacity));

	u32 index = find_slot(key);

	if (control[index] == EMPTY || control[index] == DELETED)
	{
		control[index] = key & 0x7F;
		data[index] = { key, value };
		size++;
	}
	else
	{
		data[index] = { key, value };
	}
}

template <typename T>
T* SwissTable<T>::find(u64 key)
{
	u32 index = find_slot(key);
	
	if (control[index] != EMPTY && data[index].key == key)
	{
		return &data[index].value;
	}

	return nullptr;
}

template <typename T>
void SwissTable<T>::erase(u64 key)
{
	u32 index = find_slot(key);
	if (control[index] != EMPTY && data[index].key == key)
	{
		control[index] = DELETED;
		size--;
	}
}

template <typename T>
void SwissTable<T>::init(u32 newCapacity)
{
	size = 0;
	capacity = newCapacity;
	control = new u8[capacity];
	memset(control, EMPTY, capacity);
	data = new Entry[capacity];
}

template <typename T>
u32 SwissTable<T>::hash(u64 key) const
{
	return key & (capacity - 1);
}

template <typename T>
u32 SwissTable<T>::probe(u32 index, u32 step) const
{
	return (index + step * step) & (capacity - 1);
}

template <typename T>
u32 SwissTable<T>::find_slot(u64 key) const
{
	u32 index = hash(key);
	u32 step = 1;

	while (control[index] != EMPTY)
	{
		if (control[index] != DELETED && data[index].key == key)
			return index;

		index = probe(index, step++);
	}
	return index;
}

template <typename T>
void SwissTable<T>::rehash(u32 newCapacity)
{
	u8* oldControl = control;
	Entry* oldData = data;
	u32 oldCapacity = capacity;

	capacity = newCapacity;
	control = new u8[capacity]{ EMPTY };
	memset(control, EMPTY, capacity);
	data = new Entry[capacity];

	u32 sizeCopy = size;

	for (u32 i = 0; i < oldCapacity; i++)
	{
		if (oldControl[i] != EMPTY && oldControl[i] != DELETED)
		{
			insert(oldData[i].key, oldData[i].value);
		}
	}

	size = sizeCopy;

	delete [] oldControl;
	delete [] oldData;
}
