#pragma once
#include "imgui.h"

#include "Array.h"
#include "HashMap.h"
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

	Allocator& m_allocator;
	Array<TruthMap*> m_nodes;
	TruthMap* m_head = nullptr;
};

inline const TruthElement* Truth::get(truth::Key key)
{
	return m_head->find(key);
}

inline void Truth::set(truth::Key key, TruthElement* element)
{
	m_nodes.push_back(m_head);
	m_head = TruthMap::writeValue(m_head, m_head, key, element);
}

inline void Truth::erase(truth::Key key)
{
	m_nodes.push_back(m_head);
	m_head = TruthMap::erase(m_head, m_head, key);
}

inline Transaction Truth::openTransaction()
{
	return Transaction{ m_head, m_head };
}

inline bool Truth::tryCommit(Transaction& tx)
{
	if (m_head == tx.base)
	{
		m_nodes.push_back(m_head);
		m_head = tx.uncommitted;
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
	if (key.asU64 == 14829735431805717965ull)
	{
		__debugbreak();
	}
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
