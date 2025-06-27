#pragma once

#include "Math.h"
#include "mh64.h"
#include "TruthMap.h"
#include "TruthView.h"
#include "Core/Array.h"
#include "Core/HashMap.h"


struct TTInstance;
struct TTValue;

struct Position
{
	constexpr static u64 hType = TM_STATIC_HASH("Position", 0x3c52e3a1cb90e8d5ULL);

	bool inheritsX;
	bool inheritsY;
	bool inheritsZ;

	float x;
	float y;
	float z;


	float3 float3() const
	{
		return {x,y,z};
	}
};

Position get_position(ReadOnlySnapshot snap, truth::Key objectId);
void set_position(Transaction& tx, truth::Key objectId, Position p);

struct TTObject
{
	Array<u64> names;
	Array<TTValue> values;
};

struct TTArray
{
	Array<TTValue> values;
};


TTObject* clone_object(Allocator* a, const TTObject* pObject);

TTObject* create_from_template(Allocator* a, const TTObject* pTemplate);

struct TTInstance
{
	u64 hTemplate;
	TTObject* pOverride;
};

struct TTValue
{
	enum Type : u8
	{
		Type_Object,
		Type_ObjectRef,
		Type_Array,
		Type_Instance,
		Type_Integer,
		Type_Number
	};

	f64 asNumber()
	{
		return type == Type_Number ? number : 0.0;
	}

	union
	{
		TTInstance instance;
		TTObject* pObject;
		TTArray* pArray;
		u64 objectRef;
		i64 integer;
		f64 number;
	};

	Type type;
};


TTValue* lookup(u64 ref)
{
	return nullptr;
}

TTValue* DynamicValue_clone(Allocator* a, const TTValue* src)
{
	
}

void DynamicValue_obj_add(TTValue* pObjectValue, u64 hName, TTValue value)
{
	if (pObjectValue->type == TTValue::Type_Object)
	{
		TTObject* pObject = pObjectValue->pObject;

		for (i32 i = 0; i < pObject->names.size(); ++i)
		{
			if (hName == pObject->names[i])
			{
				pObjectValue[i].
			}
		}

		pObject->names.push_back(hName);
		pObject->values.push_back(value);
	}

	if (value.type == TTValue::Type_Instance)
	{
		TTInstance instance = value.instance;

		if (instance.pOverride)
		{
			TTObject* pOverride = instance.pOverride;

			for (i32 i = 0; i < pOverride->names.size(); ++i)
			{
				
			}
		}

		TTObject* pTemplate = lookup(instance.hTemplate)->pObject;

		for (i32 i = 0; i < pTemplate->names.size(); ++i)
		{
			if (hName == pTemplate->names[i])
			{

				//pTemplate->values[i].asNumber();
			}
		}
	}
}

f64 DynamicValue_read_f64(u64 hName, const TTValue* value)
{
	if (value->type == TTValue::Type_Object)
	{
		TTObject* pObject = value->pObject;

		for (i32 i = 0; i < pObject->names.size(); ++i)
		{
			if (hName == pObject->names[i])
			{
				return pObject->values[i].asNumber();
			}
		}
	}

	if (value->type == TTValue::Type_Instance)
	{
		TTInstance instance = value->instance;

		if (instance.pOverride)
		{
			TTObject* pOverride = instance.pOverride;

			for (i32 i = 0; i < pOverride->names.size(); ++i)
			{
				if (hName == pOverride->names[i])
				{
					return pOverride->values[i].asNumber();
				}
			}
		}

		TTObject* pTemplate = lookup(instance.hTemplate)->pObject;

		for (i32 i = 0; i < pTemplate->names.size(); ++i)
		{
			if (hName == pTemplate->names[i])
			{
				return pTemplate->values[i].asNumber();
			}
		}
	}

	return 0.0;
}

TTValue* make_default_entity(Allocator* a)
{
	TTValue* pValue = (TTValue*)a->alloc(sizeof(TTValue));
	memset(pValue, 0, sizeof(TTValue));



}

struct Entity : TruthElement
{
	constexpr static const char* kName = "Entity";
	constexpr static u64 kTypeId = TM_STATIC_HASH("Entity", 0x11fef190dc0c34a1ULL);

	static Entity* create(Allocator* a);
	static Entity* createFromPrototype(Allocator* a, truth::Key prototype);

	~Entity() override = default;

	u64 typeId() const override
	{
		return kTypeId;
	}

	TruthElement* clone(Allocator* a) const override;

	Array<truth::Key> children;
	HashMap<Array<truth::Key>> instantiatedRoots;

	truth::Key prototype;

	char name[64];

	
	Position position = {};
};

