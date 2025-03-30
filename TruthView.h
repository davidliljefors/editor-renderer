#pragma once

#include "Array.h"
#include "Math.h"
#include "mh64.h"
#include "TruthMap.h"


struct Entity : TruthElement
{
	inline static const char* kName = "Entity";
	inline static const u64 kTypeId = MetroHash64::Hash(kName, strlen(kName));

	Entity(Allocator& a)
		: m_children(a)
	{

	}

	~Entity() override = default;

	u64 typeId() override
	{
		return kTypeId;
	}

	TruthElement* clone(Allocator& a) override
	{
		Entity* entityClone = alloc<Entity>(a, a);
		entityClone->m_children = m_children.clone();
		entityClone->position = position;
		return entityClone;
	}

	Array<truth::Key> m_children;
	float3 position = {};
};

struct Transaction
{
	const TruthMap* base;
	TruthMap* uncommitted;
};

class Truth
{
public:

	struct Conflict
	{
		
	};

	explicit Truth(Allocator& allocator)
		: m_allocator(allocator)
		, m_nodes(allocator)
	{
		TruthMap* emptyState = TruthMap::create(allocator);
		m_nodes.push_back(emptyState);
		m_head = emptyState;
		m_readIndex = 0;
	}

	/// Single shot API

	const TruthElement* get(truth::Key key);
	void set(truth::Key key, TruthElement* element);
	void erase(truth::Key key);

	/// Transaction API

	Transaction openTransaction();
	bool tryCommit(Transaction& tx);
	void getConflicts(Transaction& tx, Array<Conflict>& outConflicts);

	const TruthElement* read(Transaction& tx, truth::Key key);
	void add(Transaction& tx, truth::Key key, TruthElement* element);
	TruthElement* write(Transaction& tx, truth::Key key);
	void erase(Transaction& tx, truth::Key key);

	Allocator& allocator() const { return m_allocator; }

	/// Undo Stack API
	
	i32 undoUnits()
	{
		return m_nodes.size();
	}


	i32 getReadIndex()
	{
		return m_readIndex;
	}

	void setReadIndex(i32 index)
	{
		m_head = m_nodes[index];
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

	TruthMap* head()
	{
		return m_head;
	}

	void push(TruthMap* node)
	{
		// todo hold a little lock

		if (m_readIndex == (m_nodes.size() - 1))
		{
			m_nodes.push_back(node);
		}
		else
		{
			m_nodes.resize(m_readIndex + 1);
			m_nodes[m_readIndex + 1] = node;
		}

		++m_readIndex;
		m_head = m_nodes[m_readIndex];
	}

private:
	Allocator& m_allocator;
	Array<TruthMap*> m_nodes;
	i32 m_readIndex = 0;
	TruthMap* m_head = nullptr;
};

inline const TruthElement* Truth::get(truth::Key key)
{
	return m_head->find(key);
}

inline void Truth::set(truth::Key key, TruthElement* element)
{
	TruthMap* newHead = TruthMap::writeValue(m_head, m_head, key, element);
	push(newHead);
}

inline void Truth::erase(truth::Key key)
{
	TruthMap* newHead = TruthMap::erase(m_head, m_head, key);
	push(newHead);
}

inline Transaction Truth::openTransaction()
{
	TruthMap* current_head = head();

	return Transaction{ current_head, current_head };
}

inline bool Truth::tryCommit(Transaction& tx)
{
	// todo need exclusive head 
	if (m_head == tx.base)
	{
		push(tx.uncommitted);
		return true;
	}
	else
	{
		return false;
	}
}

inline const TruthElement* Truth::read(Transaction& tx, truth::Key key)
{
	return tx.uncommitted->find(key);
}

inline void Truth::add(Transaction& tx, truth::Key key, TruthElement* element)
{
	tx.uncommitted = TruthMap::writeValue(tx.base, tx.uncommitted, key, element);
}

inline TruthElement* Truth::write(Transaction& tx, truth::Key key)
{
	TruthElement* element;
	tx.uncommitted = TruthMap::lookupForWrite(tx.base, tx.uncommitted, key, &element);
	return element;
}

inline void Truth::erase(Transaction& tx, truth::Key key)
{
	tx.uncommitted = TruthMap::erase(tx.base, tx.uncommitted, key);
}
