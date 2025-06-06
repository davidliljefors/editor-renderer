#pragma once

#include "Core/Array.h"
#include "Math.h"
#include "mh64.h"
#include "TruthMap.h"

inline void alloc_str(Array<char>& a, const char* input)
{
	i32 len = strlen(input);
	a.resize(len+1);
	strcpy_s(a.data(), len+1, input);
}

enum EntityFlag : u32
{
	Hovered = 1 << 0,
	Selected = 1 << 1,
};

struct Entity : TruthElement
{
	inline static const char* kName = "Entity";
	inline static const u64 kTypeId = MetroHash64::Hash(kName, strlen(kName));

	Entity(Allocator& a)
		: m_children(a)
		, m_name(a)
	{
		alloc_str(m_name, "New Entity");
	}

	~Entity() override = default;

	u64 typeId() override
	{
		return kTypeId;
	}

	TruthElement* clone(Allocator& a) const override
	{
		Entity* entityClone = create<Entity>(a, a);
		entityClone->m_children = m_children.clone();
		entityClone->position = position;
		entityClone->flags = flags;
		return entityClone;
	}

	void setFlag(EntityFlag flag, bool set)
	{
		if (set)
		{
			flags |= (u32)flag;
		}
		else
		{
			flags &= ~flag;
		}
	}

	bool isFlagSet(EntityFlag flag) const
	{
		return (flags & flag) != 0;
	}

	Array<truth::Key> m_children;
	Array<char> m_name;
	float3 position = {};

	u32 flags = 0;
};

struct Transaction
{
	ReadOnlySnapshot base;
	Snapshot uncommitted;
};

class Truth
{
public:

	struct Conflict
	{
		
	};

	explicit Truth(Allocator& allocator)
		: m_allocator(allocator)
		, m_history(allocator)
	{
		TruthMap* emptyState = TruthMap::makeRoot(allocator);
		m_history.push_back(Snapshot{ emptyState }.asImmutable());
		m_readIndex = 0;
	}

	/// Single shot API

	const TruthElement* get(truth::Key key);
	bool set(truth::Key key, TruthElement* element);
	bool erase(truth::Key key);

	ReadOnlySnapshot snap();

	/// Transaction API

	Transaction openTransaction();
	bool commit(Transaction& tx);
	void drop(Transaction& tx);

	void getConflicts(Transaction& tx, Array<Conflict>& outConflicts);

	const TruthElement* read(ReadOnlySnapshot snap, truth::Key key);
	const TruthElement* read(Transaction& tx, truth::Key key);
	void add(Transaction& tx, truth::Key key, TruthElement* element);
	TruthElement* write(Transaction& tx, truth::Key key);
	void erase(Transaction& tx, truth::Key key);


	Allocator& allocator() const { return m_allocator; }

	/// Undo Stack API
	
	i32 undoUnits()
	{
		return m_history.size();
	}


	i32 getReadIndex()
	{
		return m_readIndex;
	}

	void setReadIndex(i32 index)
	{
		m_readIndex = index;
	}

	bool canUndo()
	{
		return m_readIndex > 0;
	}

	void undo()
	{
		if (canUndo())
		{
			--m_readIndex;
		}
	}

	ReadOnlySnapshot head()
	{
		// todo little lock
		i32 readIndex = m_readIndex;

		return m_history[readIndex];
	}

	void push(Snapshot snapshot)
	{
		// todo hold a little lock

		if (m_readIndex == (m_history.size() - 1))
		{
			m_history.push_back(snapshot.asImmutable());
		}
		else
		{
			m_history.resize(m_readIndex + 1);
			m_history.push_back(snapshot.asImmutable());
		}

		++m_readIndex;
	}

private:
	Allocator& m_allocator;
	Array<ReadOnlySnapshot> m_history;
	i32 m_readIndex = 0;
};

inline const TruthElement* Truth::get(truth::Key key)
{
	ReadOnlySnapshot snap = head();

	return snap.s->find(key);
}

inline bool Truth::set(truth::Key key, TruthElement* element)
{
	Transaction tx = openTransaction();
	tx.uncommitted.s = TruthMap::writeValue(tx.base.s, tx.uncommitted.s, key, element);

	// todo: handle dropping

	return commit(tx);
}

inline bool Truth::erase(truth::Key key)
{
	Transaction tx = openTransaction();
	erase(tx, key);

	// todo: handle dropping

	return commit(tx);
}

inline ReadOnlySnapshot Truth::snap()
{
	return head();
}

inline Transaction Truth::openTransaction()
{
	ReadOnlySnapshot current_head = head();
	
	// Note: Initialize transaction with non const head. all write ops on compares base and head and wont overwrite values owned by base
	return Transaction{ current_head, Snapshot{current_head.s} };
}

inline bool Truth::commit(Transaction& tx)
{
	ReadOnlySnapshot current_head = head();
	assert(tx.uncommitted.s != nullptr);

	// todo need exclusive head 
	if (current_head.s == tx.base.s)
	{
		push(tx.uncommitted);
		tx.uncommitted.s = nullptr;
		tx.base.s = nullptr;
		return true;
	}
	else
	{
		return false;
	}
}

inline const TruthElement* Truth::read(ReadOnlySnapshot snap, truth::Key key)
{
	return snap.s->find(key);
}

inline const TruthElement* Truth::read(Transaction& tx, truth::Key key)
{
	return tx.uncommitted.s->find(key);
}

inline void Truth::add(Transaction& tx, truth::Key key, TruthElement* element)
{
	tx.uncommitted.s = TruthMap::writeValue(tx.base.s, tx.uncommitted.s, key, element);
}

inline TruthElement* Truth::write(Transaction& tx, truth::Key key)
{
	TruthElement* element;
	tx.uncommitted.s = TruthMap::lookupForWrite(tx.base.s, tx.uncommitted.s, key, &element);
	return element;
}

inline void Truth::erase(Transaction& tx, truth::Key key)
{
	tx.uncommitted.s = TruthMap::erase(tx.base.s, tx.uncommitted.s, key);
}
