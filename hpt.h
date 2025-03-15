#pragma once

#include "Core.h"
#include "Allocator.h"
#include "Array.h"

namespace hpt
{
constexpr static int BlockBits = 8;
constexpr static int NumBlocks = (1 << BlockBits);

constexpr static int EntryBits = 8;
constexpr static int NumEntries = (1 << EntryBits);

struct Key
{
	union
	{
		struct
		{
			u64 Index : 64 - (BlockBits + EntryBits);
			u64 Entry : EntryBits;
			u64 Block : BlockBits;
		};
		u64 asU64;
	};
};

}



struct KeyEntry
{
	hpt::Key key;
	void* value;

	bool operator==(const KeyEntry& other) const { return value == other.value; }
	bool operator<(const KeyEntry& other) const { return key.Index < other.key.Index; }
};

 inline u32 lower_bound(const KeyEntry *begin, const KeyEntry *end, const KeyEntry &key) 
 {
	u32 left = 0;
	u32 right = end - begin;

	while (left < right) 
	{
		u32 mid = left + ((right - left) >> 1);
		if (begin[mid] < key) 
		{
			left = mid + 1;
		} 
		else 
		{
			right = mid;
		}
	}
	return left;
}

class HierarchicalPageTable
{
public:
	struct InlineArray
	{
		u32 size;
		u32 _pad;

		KeyEntry data[0];

		KeyEntry* begin() { return data; }
		KeyEntry* end() { return data + size; }

		static InlineArray* alloc(Allocator& arena, u32 capacity)
		{
			u32 structSize = sizeof(InlineArray);
			u32 dynamicArraySize = sizeof(KeyEntry*) * capacity;
			InlineArray* arr = (InlineArray*)arena.alloc(structSize + dynamicArraySize);
			arr->size = 0;
			return arr;
		}
	};

	static_assert(sizeof(InlineArray) == 8, "InlineArray size must be 8 bytes");

	struct Block
	{
		InlineArray* entries[hpt::NumEntries]{};
	};

	struct BigBlock
	{
		Block* blocks[hpt::NumBlocks]{};
	};

	static void diff(
		HierarchicalPageTable* from,
		HierarchicalPageTable* to,
		Array<Block*>& outBlocks,
		Array<InlineArray*>& outEntryArrays,
		Array<KeyEntry>& outEntries)
	{
		outBlocks.clear();
		outEntryArrays.clear();
		outEntries.clear();

		if (from->m_root != to->m_root)
		{
			Block** arrFrom = from->m_root->blocks;
			Block** arrTo = to->m_root->blocks;
			for (u32 i = 0; i < hpt::NumBlocks; ++i)
			{
				if (arrFrom[i] != arrTo[i])
				{
					diff(arrFrom[i], arrTo[i], outEntryArrays, outEntries);
					outBlocks.push_back(arrFrom[i]);
				}
			}
		}
	}

	static HierarchicalPageTable* create(Allocator& allocator)
	{
		HierarchicalPageTable* instance = new (allocator) HierarchicalPageTable(allocator);

		instance->m_root = new (allocator) BigBlock();

		for (Block*& block : instance->m_root->blocks)
		{
			block = new (allocator) Block();
		}

		return instance;
	}

	static HierarchicalPageTable* lookupForWrite(
		const HierarchicalPageTable* base,
		HierarchicalPageTable* head,
		hpt::Key key,
		void** outEntry)
	{
		InlineArray* array;
		u32 slot;
		HierarchicalPageTable* update = getWritableEntryArray(base, head, key, false, &array, &slot);

		*outEntry = &array->data[slot].value;

		return update;
	}

	static HierarchicalPageTable* erase(
		const HierarchicalPageTable* base,
		HierarchicalPageTable* head,
		hpt::Key key)
	{
		InlineArray* array;
		u32 slot;
		HierarchicalPageTable* update = getWritableEntryArray(base, head, key, true, &array, &slot);

		// keep array in sorted order
		array->data[slot] = {};
		for (u32 i = slot; i < (array->size - 1); ++i)
		{
			array->data[i] = array->data[i + 1];
		}
		array->size -= 1;

		return update;
	}

	const void* find(hpt::Key key)
	{
		InlineArray* entries = getEntries(key);

		if (entries)
		{
			// todo binary search
			for (u32 i = 0; i < entries->size; ++i)
			{
				if (entries->data[i].key.Index == key.Index)
				{
					return &entries->data[i].value;
				}
			}
		}

		return nullptr;
	}

	u32 size() const { return m_size; }

	BigBlock* root() const
	{
		return m_root;
	}

private:
	static HierarchicalPageTable* getWritableEntryArray(
		const HierarchicalPageTable* base,
		HierarchicalPageTable* head,
		hpt::Key key,
		bool erasing,
		InlineArray** outArray,
		u32* outSlot)
	{
		HierarchicalPageTable* updated = head;
		Allocator& allocator = head->m_allocator;

		if (updated == base)
		{
			updated = new (allocator) HierarchicalPageTable(allocator);
			updated->m_size = head->size();
			updated->m_root = head->m_root;
		}

		BigBlock* bigBlockUpdate = updated->m_root;
		const BigBlock* baseBigBlock = base->m_root;
		if (bigBlockUpdate == baseBigBlock)
		{
			bigBlockUpdate = new (allocator) BigBlock();
			memcpy(bigBlockUpdate, baseBigBlock, sizeof(BigBlock));
			updated->m_root = bigBlockUpdate;
		}

		Block* blockUpdate = bigBlockUpdate->blocks[key.Block];
		const Block* baseBlock = baseBigBlock->blocks[key.Block];
		if (blockUpdate == baseBlock)
		{
			blockUpdate = new (allocator) Block();
			memcpy(blockUpdate, baseBlock, sizeof(Block));
			bigBlockUpdate->blocks[key.Block] = blockUpdate;
		}

		///
		InlineArray* entriesUpdate = blockUpdate->entries[key.Entry];
		const InlineArray* baseEntries = baseBlock->entries[key.Entry];

		// if array didnt exist we just put yourself in
		if (entriesUpdate == nullptr)
		{
			entriesUpdate = InlineArray::alloc(allocator, 1);
			blockUpdate->entries[key.Entry] = entriesUpdate;

			KeyEntry toInsert{};
			toInsert.key = key;

			entriesUpdate->data[0] = toInsert;
			entriesUpdate->size = 1;

			*outSlot = 0;
			*outArray = entriesUpdate;
			++updated->m_size;

			return updated;
		}

		u32 slotUpdate = u32(-1);
		u32 baseSlot = u32(-1);

		// todo binary serach
		if (baseEntries)
		{
			for (u32 i = 0; i < baseEntries->size; ++i)
			{
				if (baseEntries->data[i].key.Index == key.Index)
				{
					baseSlot = i;
					break;
				}
			}
		}

		// todo binary serach
		for (u32 i = 0; i < entriesUpdate->size; ++i)
		{
			if (entriesUpdate->data[i].key.Index == key.Index)
			{
				slotUpdate = i;
				break;
			}
		}

		// if we dont exist in the array. allocate space for us
		const KeyEntry baseEntry = baseSlot == u32(-1) ? KeyEntry{} : baseEntries->data[baseSlot];

		// it already exists in entriesUpdate
		if (slotUpdate != u32(-1))
		{
			// need to aquire write access to entriesUpdate
			if (entriesUpdate == baseEntries)
			{
				u32 newCapacity = baseEntries->size;
				entriesUpdate = InlineArray::alloc(allocator, newCapacity);
				entriesUpdate->size = baseEntries->size;
				memcpy(entriesUpdate->data, baseEntries->data, baseEntries->size * sizeof(KeyEntry*));
				blockUpdate->entries[key.Entry] = entriesUpdate;
			}

			// need to aquire ownership of KeyEntry if we are not removing it from the list
			if (entriesUpdate->data[slotUpdate] == baseEntry && !erasing)
			{
				entriesUpdate->data[slotUpdate] = {};
				entriesUpdate->data[slotUpdate].key = key;
			}
			*outSlot = slotUpdate;
			*outArray = entriesUpdate;

			return updated;
		}
		// it does not exist in entriesUpdate
		else if (slotUpdate == u32(-1))
		{
			// need to aquire write access to entriesUpdate
			if (entriesUpdate == baseEntries)
			{
				u32 baseSize = baseEntries->size;
				u32 newCapacity = baseSize + 1;
				entriesUpdate = InlineArray::alloc(allocator, newCapacity);
				entriesUpdate->size = baseSize;
				memcpy(entriesUpdate->data, baseEntries->data, baseSize * sizeof(KeyEntry*));
				blockUpdate->entries[key.Entry] = entriesUpdate;
			}
			else
			{
				// entriesUpdate is not committed yet but we need to make space for our element
				// must copy to new array and free

				InlineArray* temp = InlineArray::alloc(allocator, entriesUpdate->size + 1);
				temp->size = entriesUpdate->size;
				memcpy(temp->data, entriesUpdate->data, entriesUpdate->size * sizeof(KeyEntry*));
				allocator.free(entriesUpdate);
				entriesUpdate = temp;
				blockUpdate->entries[key.Entry] = entriesUpdate;
			}

			KeyEntry toInsert{};
			toInsert.key = key;

			++updated->m_size;

			u32 desiredSlot = lower_bound(entriesUpdate->begin(), entriesUpdate->end(), toInsert);

			if (desiredSlot == entriesUpdate->size)
			{
				entriesUpdate->data[desiredSlot] = toInsert;
			}
			else
			{
				u32 i = entriesUpdate->size + 1;
				while (i > desiredSlot)
				{
					entriesUpdate[i] = entriesUpdate[i - 1];
					--i;
				}
				entriesUpdate->data[desiredSlot] = toInsert;
			}

			entriesUpdate->size += 1;
			*outArray = entriesUpdate;
			*outSlot = desiredSlot;
		}

		return updated;
	}

	InlineArray* getEntries(hpt::Key key)
	{
		Block& block = *m_root->blocks[key.Block];
		return block.entries[key.Entry];
	}

	static void diff(InlineArray* from, InlineArray* to, Array<KeyEntry>& outEntries)
	{
		if (to == nullptr)
		{
			for (KeyEntry e : *from)
			{
				outEntries.push_back(e);
			}
		}

		KeyEntry* old_it = from->begin();
		KeyEntry* old_end = from->end();

		KeyEntry* new_it = to->begin();
		KeyEntry* new_end = to->end();

		while (old_it != old_end && new_it != new_end)
		{
			KeyEntry oldData = *old_it;
			KeyEntry newData = *new_it;

			if (oldData.key.Index < newData.key.Index)
			{
				outEntries.push_back(oldData);
				++old_it;
			}
			else if (oldData.key.Index > newData.key.Index)
			{
				++new_it;
			}
			else
			{
				if (oldData.value != newData.value)
				{
					outEntries.push_back(oldData);
				}
				++new_it;
				++old_it;
			}
		}

		while (old_it != old_end)
		{
			outEntries.push_back(*old_it);
			++old_it;
		}
	}

	static void diff(
		Block* from,
		Block* to,
		Array<InlineArray*>& outEntryArrays,
		Array<KeyEntry>& outEntries)
	{
		if (from != to)
		{
			InlineArray** arrFrom = from->entries;
			InlineArray** arrTo = to->entries;

			for (u32 i = 0; i < hpt::NumEntries; ++i)
			{
				if (arrFrom[i] != arrTo[i])
				{
					if (arrFrom[i] != nullptr)
					{
						diff(arrFrom[i], arrTo[i], outEntries);
						outEntryArrays.push_back(arrFrom[i]);
					}
				}
			}
		}
	}

	explicit HierarchicalPageTable(Allocator& allocator)
		: m_allocator(allocator)
	{}

	BigBlock* m_root = nullptr;
	u32 m_size = 0;
	Allocator& m_allocator;

	struct EntryComparer
	{
		bool operator()(const KeyEntry* lhs, const KeyEntry* rhs) const { return lhs->key.Index < rhs->key.Index; }
	};
};



inline void diff(HierarchicalPageTable* base, HierarchicalPageTable* compare, 
Array<KeyEntry>& adds,
Array<KeyEntry>& edits,
Array<KeyEntry>& removes)
{
	if (base != compare)
	{
		auto* baseBig = base->root();
		auto* compareBig = compare->root();
		if (baseBig != compareBig)
		{
			for (u32 iBlock = 0; iBlock < hpt::NumBlocks; ++iBlock)
			{
				auto* baseBlock = baseBig->blocks[iBlock];
				auto* compareBlock = compareBig->blocks[iBlock];

				if (baseBlock == compareBlock)
				{
					continue;
				}

				for (u32 iEntry = 0; iEntry < hpt::NumEntries; ++iEntry)
				{
					auto* baseEntries = baseBig->blocks[iBlock]->entries[iEntry];
					auto* compareEntries = compareBig->blocks[iBlock]->entries[iEntry];

					// Not in BASE, only in COMPARE
					if (!baseEntries && compareEntries)
					{
						for (KeyEntry entry : *compareEntries)
						{
							adds.push_back(entry);
						}
					}

					// Only in BASE, not in COMPARE
					else if (baseEntries && !compareEntries)
					{
						for (KeyEntry entry : *baseEntries)
						{
							removes.push_back(entry);
						}
					}

					// Both entries exist. Compare Element wise
					else if (baseEntries && compareEntries)
					{
						KeyEntry* baseIterator = baseEntries->begin();
						KeyEntry* baseEnd = baseEntries->end();

						KeyEntry* compareIterator = compareEntries->begin();
						KeyEntry* compareEnd = compareEntries->end();

						while (baseIterator != baseEnd && compareIterator != compareEnd)
						{
							KeyEntry baseData = *baseIterator;
							KeyEntry compareData = *compareIterator;

							if (baseData.key.Index < compareData.key.Index)
							{
								removes.push_back(baseData);
								++baseIterator;
							}
							else if (baseData.key.Index > compareData.key.Index)
							{
								adds.push_back(compareData);
								++compareIterator;
							}
							else  // baseKey == compareKey
							{
								if (baseData.value != compareData.value)
								{
									edits.push_back(compareData);
								}
								++compareIterator;
								++baseIterator;
							}
						}

						while (baseIterator != baseEnd)
						{
							removes.push_back(*baseIterator);
							++baseIterator;
						}

						while (compareIterator != compareEnd)
						{
							adds.push_back(*compareIterator);
							++compareIterator;
						}
					}
				}
			}
		}
	}
}
