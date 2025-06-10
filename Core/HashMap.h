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
	u64 key;
	u32 next;
	T value;
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

	explicit HashMap(Allocator& allocator)
		: hash(allocator)
		, data(allocator)
	{

	}


	T* find(u64 key);

	void add(u64 key, T value);

	void insert_or_assign(u64 key, const T& value);

	void insert_or_assign(u64 key, T&& value);

	void erase(u64 key);

	T& operator[](u64 key);

	i32 size() const;

	Iterator begin();

	Iterator end();

private:
	HashFind find_impl(u64 key);

	void erase_impl(HashFind find);

	i32 add_entry(u64 key);

	i32 find_or_make(u64 key);

	bool is_full();

	void grow();

	void rehash(i32 new_size);

	Array<u32> hash;
	Array<Entry> data;
};

template <typename T>
T* HashMap<T>::find(u64 key)
{
	HashFind find = find_impl(key);
	if (find.dataIndex == END_OF_CHAIN)
	{
		return nullptr;
	}

	return &data[find.dataIndex].value;
}

template <typename T>
void HashMap<T>::add(u64 key, T value)
{
	if (hash.empty())
		grow();

	const HashFind fr = find_impl(key);

	assert(fr.dataIndex == END_OF_CHAIN && "Cannot add same key twice");

	u32 i = add_entry(key);
	if (fr.dataPrev == END_OF_CHAIN)
	{
		hash[fr.hashIndex] = i;
	}
	else
	{
		data[fr.dataPrev].next = i;
	}

	memcpy(&data[i].value, &value, sizeof(T));
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
	if (hash.empty())
		grow();

	const u32 i = find_or_make(key);

	memcpy(&data[i].value, &value, sizeof(T));
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
	if (hash.empty())
		grow();

	i32 i = find_or_make(key);

	if (is_full())
	{
		grow();
		i = find_or_make(key);
	}

	return data[i].value;
}

template <typename T>
i32 HashMap<T>::size() const
{
	return data.size();
}

template <typename T>
typename HashMap<T>::Iterator HashMap<T>::begin()
{
	return Iterator{&data, 0};
}

template <typename T>
typename HashMap<T>::Iterator HashMap<T>::end()
{
	return Iterator{&data, data.size()};
}


template <typename T>
typename HashMap<T>::HashFind HashMap<T>::find_impl(u64 key)
{
	HashFind find;
	find.hashIndex = END_OF_CHAIN;
	find.dataPrev = END_OF_CHAIN;
	find.dataIndex = END_OF_CHAIN;

	if (hash.empty())
		return find;

	find.hashIndex = key & (hash.size() - 1);
	find.dataIndex = hash[find.hashIndex];
	while (find.dataIndex != END_OF_CHAIN)
	{
		if (data[find.dataIndex].key == key)
		{
			return find;
		}
		find.dataPrev = find.dataIndex;
		find.dataIndex = data[find.dataIndex].next;
	}

	return find;
}

template <typename T>
void HashMap<T>::erase_impl(HashFind find)
{
	if (find.dataPrev == END_OF_CHAIN)
		hash[find.hashIndex] = data[find.dataIndex].next;
	else
		data[find.dataPrev].next = data[find.dataIndex].next;

	if (find.dataIndex == u32(data.size() - 1))
	{
		data.resize(data.size() - 1);
		return;
	}

	data[find.dataIndex] = data[data.size() - 1];
	HashFind last = find_impl(data[find.dataIndex].key);

	if (last.dataPrev != END_OF_CHAIN)
	{
		data[last.dataPrev].next = find.dataIndex;
	}
	else
	{
		hash[last.hashIndex] = find.dataIndex;
	}

	data.resize(data.size() - 1);
}

template <typename T>
i32 HashMap<T>::add_entry(u64 key)
{
	typename HashMap<T>::Entry e;
	e.key = key;
	e.next = END_OF_CHAIN;
	u32 ei = data.size();
	data.push_back(e);
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
		hash[fr.hashIndex] = i;
	}
	else
	{
		data[fr.dataPrev].next = i;
	}

	return i;
}

template <typename T>
bool HashMap<T>::is_full()
{
	constexpr float kMaxLoadFactor = 0.7f;
	return (float)data.size() >= (float)hash.size() * kMaxLoadFactor;
}

template <typename T>
void HashMap<T>::grow()
{
	if (hash.size() == 0)
	{
		rehash(16);
	}
	else
	{
		const u32 newSize = hash.size() * 2;
		rehash(newSize);
	}
}

template <typename T>
void HashMap<T>::rehash(i32 new_size)
{
	HashMap<T> nh(data.get_allocator());

	nh.hash.resize(new_size);
	nh.data.reserve(data.size());

	for (i32 i = 0; i < new_size; ++i)
	{
		nh.hash[i] = END_OF_CHAIN;
	}

	for (i32 i = 0; i < data.size(); ++i)
	{
		auto& e = data[i];
		nh.insert_or_assign(e.key, static_cast<T&&>(e.value));
	}

	if (hash.empty())
	{
		hash.swap(nh.hash);
	}
	else
	{
		data.swap(nh.data);
		hash.swap(nh.hash);
	}
}
