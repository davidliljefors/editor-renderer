#pragma once

#include "Math.h"
#include "mh64.h"
#include "TruthMap.h"
#include "Core/Array.h"
#include "Core/HashMap.h"

struct Entity : TruthElement
{
	inline static const char* kName = "Entity";
	inline static const u64 kTypeId = MetroHash64::Hash(kName, strlen(kName));

	static Entity* create(Allocator* a);

	~Entity() override = default;

	u64 typeId() const override
	{
		return kTypeId;
	}

	TruthElement* clone(Allocator* a) const override;

	Array<truth::Key> children;

	HashMap<Array<truth::Key>> instantiatedRoots;
	char name[32];
	float3 position = {};
};