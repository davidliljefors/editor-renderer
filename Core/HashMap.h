#pragma once

#include <assert.h>

#include "Allocator.h"
#include "Array.h"

using u32 = unsigned int;
using u64 = unsigned long long;

template<typename T>
struct HashMap
{
private:
constexpr static u32 END_OF_CHAIN = u32(-1);

struct HashFind
{
	u32 hashIndex;
	u32 dataPrev;
	u32 dataIndex;
};

struct Entry
{
	template<typename U>
	static auto test_clone(U* p) -> decltype(p->clone(), char(0)) { return 0; }
	static char(&test_clone(...))[2] { static char arr[2] = {}; return arr; }

	static constexpr bool has_clone = sizeof(test_clone((T*)0)) == 1;

	u64 key;
	u32 next;
	T value;

	Entry& operator=(const Entry& e)
	{
		if constexpr(has_clone)
		{
			key = e.key;
			value = e.value.clone();
			next = e.next;
		}
		else
		{
			key = e.key;
			value = e.value;
			next = e.next;
		}

		return *this;
	}
};

public:
	struct Iterator
	{
		Iterator(Array<Entry>* ref, i32 index)
			: ref(ref)
			, index(index)
		{

		}

		void operator++()
		{
			++index;
		}

		bool operator!=(const Iterator& other)
		{
			return index != other.index;
		}

		T& operator*()
		{
			return ref->at(index).value;
		}

		T* operator->()
		{
			return &ref->at(index).value;
		}

		Array<Entry>* ref;
		i32 index;
	};	

	explicit HashMap(Allocator* allocator)
		: m_hash(allocator)
		, m_data(allocator)
	{

	}

	explicit HashMap()
	{

	}

	void set_allocator(Allocator* a);

	T* find(u64 key);

	bool contains(u64 key) const;

	void add(u64 key, T value);

	void insert_or_assign(u64 key, const T& value);

	void insert_or_assign(u64 key, T&& value);

	void erase(u64 key);

	T& operator[](u64 key);

	i32 size() const;

	Entry* begin();

	Entry* end();

	Entry* data();

	HashMap clone() const;

private:
	HashFind find_impl(u64 key);

	void erase_impl(HashFind find);

	i32 add_entry(u64 key);

	i32 find_or_make(u64 key);

	bool is_full();

	void grow();

	void rehash(i32 new_size);

	Array<u32> m_hash;
	Array<Entry> m_data;
};

template <typename T>
void HashMap<T>::set_allocator(Allocator* a)
{
	assert(m_data.get_allocator() == nullptr);
	assert(m_hash.get_allocator() == nullptr);

	m_data.set_allocator(a);
	m_hash.set_allocator(a);
}

template <typename T>
T* HashMap<T>::find(u64 key)
{
	HashFind find = find_impl(key);
	if (find.dataIndex == END_OF_CHAIN)
	{
		return nullptr;
	}

	return &m_data[find.dataIndex].value;
}

template <typename T>
bool HashMap<T>::contains(u64 key) const
{
	HashMap* self = (HashMap*)this;
	HashFind find = self->find_impl(key);
	return find.dataIndex != END_OF_CHAIN;
}

template <typename T>
void HashMap<T>::add(u64 key, T value)
{
	if (m_hash.empty())
		grow();

	const HashFind fr = find_impl(key);

	assert(fr.dataIndex == END_OF_CHAIN && "Cannot add same key twice");

	u32 i = add_entry(key);
	if (fr.dataPrev == END_OF_CHAIN)
	{
		m_hash[fr.hashIndex] = i;
	}
	else
	{
		m_data[fr.dataPrev].next = i;
	}

	memcpy(&m_data[i].value, &value, sizeof(T));
	memset(&value, 0, sizeof(T));

	if (is_full())
	{
		grow();
	}
}

template <typename T>
void HashMap<T>::insert_or_assign(u64 key, const T& value)
{
	T copy = value;
	insert_or_assign(key, (T&&)copy);
}

template <typename T>
void HashMap<T>::insert_or_assign(u64 key, T&& value)
{
	if (m_hash.empty())
		grow();

	const u32 i = find_or_make(key);

	memcpy(&m_data[i].value, &value, sizeof(T));
	memset(&value, 0, sizeof(T));

	if (is_full())
	{
		grow();
	}
}

template <typename T>
void HashMap<T>::erase(u64 key)
{
	const HashFind find = find_impl(key);
	if (find.dataIndex != END_OF_CHAIN)
		erase_impl(find);
}

template <typename T>
T& HashMap<T>::operator[](u64 key)
{
	if (m_hash.empty())
		grow();

	i32 i = find_or_make(key);

	if (is_full())
	{
		grow();
		i = find_or_make(key);
	}

	return m_data[i].value;
}

template <typename T>
i32 HashMap<T>::size() const
{
	return m_data.size();
}

template <typename T>
typename HashMap<T>::Entry* HashMap<T>::begin()
{
	return m_data.begin();
}

template <typename T>
typename HashMap<T>::Entry* HashMap<T>::end()
{
	return m_data.end();
}

template <typename T>
typename HashMap<T>::Entry* HashMap<T>::data()
{
	return m_data.data();
}

template <typename T>
HashMap<T> HashMap<T>::clone() const
{
	HashMap clone(m_hash.get_allocator());
	clone.m_data = m_data.clone();
	clone.m_hash = m_hash.clone();

	return clone;
}


template <typename T>
typename HashMap<T>::HashFind HashMap<T>::find_impl(u64 key)
{
	HashFind find;
	find.hashIndex = END_OF_CHAIN;
	find.dataPrev = END_OF_CHAIN;
	find.dataIndex = END_OF_CHAIN;

	if (m_hash.empty())
		return find;

	find.hashIndex = key & (m_hash.size() - 1);
	find.dataIndex = m_hash[find.hashIndex];
	while (find.dataIndex != END_OF_CHAIN)
	{
		if (m_data[find.dataIndex].key == key)
		{
			return find;
		}
		find.dataPrev = find.dataIndex;
		find.dataIndex = m_data[find.dataIndex].next;
	}

	return find;
}

template <typename T>
void HashMap<T>::erase_impl(HashFind find)
{
	if (find.dataPrev == END_OF_CHAIN)
		m_hash[find.hashIndex] = m_data[find.dataIndex].next;
	else
		m_data[find.dataPrev].next = m_data[find.dataIndex].next;

	if (find.dataIndex == u32(m_data.size() - 1))
	{
		m_data.resize(m_data.size() - 1);
		return;
	}

	m_data[find.dataIndex] = m_data[m_data.size() - 1];
	HashFind last = find_impl(m_data[find.dataIndex].key);

	if (last.dataPrev != END_OF_CHAIN)
	{
		m_data[last.dataPrev].next = find.dataIndex;
	}
	else
	{
		m_hash[last.hashIndex] = find.dataIndex;
	}

	m_data.resize(m_data.size() - 1);
}

template <typename T>
i32 HashMap<T>::add_entry(u64 key)
{
	u32 ei = m_data.size();

	HashMap<T>::Entry* e = new (m_data.push_back_uninit()) HashMap<T>::Entry();
	e->key = key;
	e->next = END_OF_CHAIN;

	return ei;
}

template <typename T>
i32 HashMap<T>::find_or_make(u64 key)
{
	const HashFind fr = find_impl(key);
	if (fr.dataIndex != END_OF_CHAIN)
		return fr.dataIndex;

	u32 i = add_entry(key);
	if (fr.dataPrev == END_OF_CHAIN)
	{
		m_hash[fr.hashIndex] = i;
	}
	else
	{
		m_data[fr.dataPrev].next = i;
	}

	return i;
}

template <typename T>
bool HashMap<T>::is_full()
{
	constexpr float kMaxLoadFactor = 0.7f;
	return (float)m_data.size() >= (float)m_hash.size() * kMaxLoadFactor;
}

template <typename T>
void HashMap<T>::grow()
{
	if (m_hash.size() == 0)
	{
		rehash(16);
	}
	else
	{
		const u32 newSize = m_hash.size() * 2;
		rehash(newSize);
	}
}

template <typename T>
void HashMap<T>::rehash(i32 new_size)
{
	HashMap<T> nh(m_data.get_allocator());

	nh.m_hash.resize(new_size);
	nh.m_hash.reserve(m_data.size());

	for (i32 i = 0; i < new_size; ++i)
	{
		nh.m_hash[i] = END_OF_CHAIN;
	}

	for (i32 i = 0; i < m_data.size(); ++i)
	{
		auto& e = m_data[i];
		nh.insert_or_assign(e.key, static_cast<T&&>(e.value));
	}

	if (m_hash.empty())
	{
		m_hash.swap(nh.m_hash);
	}
	else
	{
		m_data.swap(nh.m_data);
		m_hash.swap(nh.m_hash);
	}
}
