#pragma once

#include "Math.h"
#include "mh64.h"
#include "TruthMap.h"
#include "TruthView.h"
#include "Core/Array.h"
#include "Core/HashMap.h"



#include <vector>
#include <unordered_map>
#include <string>


#define _CRT_SECURE_NO_WARNINGS 1

u64 random_u64();

namespace eastl = std;

struct DDInstance;
struct DynamicData;

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



struct DDObject
{
	std::vector<u64> names;
	std::vector<DynamicData*> values;
};

struct DDArray
{
	eastl::vector<DynamicData*> values;
};


DDObject* clone_object(Allocator* a, const DDObject* pObject);

DDObject* create_from_template(Allocator* a, const DDObject* pTemplate);


struct DDInstance
{
	u64 hTemplate;
	DDObject* pOverride;
};

struct DynamicData
{
	DynamicData(const DynamicData&) = delete;

	enum Type : u8
	{
		Type_Object,
		Type_ObjectRef,
		Type_Array,
		Type_Instance,
		Type_Integer,
		Type_Number,
		Type_String
	};

	f64 asNumber()
	{
		return type == Type_Number ? number : 0.0;
	}

	const char* asString()
	{
		return type == Type_String ? string : "";
	}

	union
	{
		DDInstance instance;
		DDObject* pObject;
		DDArray* pArray;
		u64 objectRef;
		i64 integer;
		f64 number;
		char* string;
	};

	Type type;
};

using Prototype = DynamicData;

inline std::unordered_map<u64, DynamicData*> g_lookup;
inline std::unordered_map<u64, DynamicData*> g_templates;


inline DynamicData* DynamicData_value_new()
{
	void* mem = malloc(sizeof(DynamicData));
	memset(mem, 0, sizeof(DynamicData));
	return (DynamicData*)mem;
}

inline DynamicData* DynamicData_obj_new()
{
	DynamicData* pValue = DynamicData_value_new();

	void* mem = malloc(sizeof(DDObject));
	memset(mem, 0, sizeof(DDObject));

	pValue->type = DynamicData::Type_Object;
	pValue->pObject = (DDObject*)mem;

	return pValue;
}

inline DynamicData* DynamicData_array_new()
{
	DynamicData* pValue = DynamicData_value_new();

	void* mem = malloc(sizeof(DDArray));
	memset(mem, 0, sizeof(DDArray));

	pValue->type = DynamicData::Type_Array;
	pValue->pArray = (DDArray*)mem;

	return pValue;
}

inline DynamicData* DynamicData_str_new()
{
	DynamicData* pValue = DynamicData_value_new();

	pValue->type = DynamicData::Type_String;

	return pValue;
}

inline DynamicData* DynamicData_int_new()
{
	DynamicData* pValue = DynamicData_value_new();

	pValue->type = DynamicData::Type_Integer;

	return pValue;
}

inline DynamicData* DynamicData_make_str(const char* str)
{
	DynamicData* pData = DynamicData_str_new();

	u64 capacity = strlen(str) + 1;
	pData->string = (char*)malloc(capacity);
	strcpy_s(pData->string, capacity, str);

	return pData;
}

inline DynamicData* DynamicData_num_new()
{
	DynamicData* pValue = DynamicData_value_new();

	pValue->type = DynamicData::Type_Integer;

	return pValue;
}

inline u64 DynamicData_size(const DynamicData* value)
{
	if (value->type == DynamicData::Type_Object)
	{
		return value->pObject->names.size();
	}

	if (value->type == DynamicData::Type_Array)
	{
		return value->pArray->values.size();
	}

	if (value->type == DynamicData::Type_Instance)
	{
		
	}

	return 0;
}

inline DynamicData* lookup(u64 ref)
{
	auto find = g_lookup.find(ref);
	return find != g_lookup.end() ? find->second : nullptr;
}

inline void TTValue_clone_internal(const DynamicData* src, DynamicData** dst)
{
	switch (src->type)
	{
	case DynamicData::Type_Object:
	{
		*dst = DynamicData_obj_new();
		u64 size = DynamicData_size(src);
		(*dst)->pObject->names.resize(size);
		(*dst)->pObject->values.resize(size);

		for (u64 i = 0; i < size; ++i)
		{
			(*dst)->pObject->names[i] = src->pObject->names[i];
			TTValue_clone_internal(src->pObject->values[i], &(*dst)->pObject->values[i]);
		}

		break;
	}
	case DynamicData::Type_ObjectRef:
		break;
	case DynamicData::Type_Array:
	{
		*dst = DynamicData_array_new();
		u64 size = DynamicData_size(src);
		(*dst)->pArray->values.resize(size);

		for (u64 i = 0; i < size; ++i)
		{
			TTValue_clone_internal(src->pArray->values[i], &(*dst)->pArray->values[i]);
		}

		break;
	}
	case DynamicData::Type_Instance:
		break;
	case DynamicData::Type_Integer:
		*dst = DynamicData_int_new();
		(*dst)->integer = src->integer;
		break;
	case DynamicData::Type_Number:
		*dst = DynamicData_num_new();
		(*dst)->number = src->number;
		break;
	case DynamicData::Type_String:
		*dst = DynamicData_str_new();
		u64 capacity = strlen(src->string) + 1;
		(*dst)->string = (char*)malloc(capacity);
		strcpy_s((*dst)->string, capacity, src->string);
		break;
	}
}

inline u64 DynamicData_clone(const DynamicData* src)
{
	u64 id = random_u64();

	DynamicData** pValue = nullptr;

	TTValue_clone_internal(src, pValue);

	g_lookup[id] = *pValue;

	return id;
}


inline const DynamicData* DynamicData_obj_find(DynamicData* pValue, u64 hName)
{
	if (pValue->type == DynamicData::Type_Object)
	{
		DDObject* pObject = pValue->pObject;

		for (i32 i = 0; i < pObject->names.size(); ++i)
		{
			if (hName == pObject->names[i])
			{
				return pObject->values[i];
			}
		}
	}

	return nullptr;
}

inline void DynamicData_obj_add(DynamicData* pObj, u64 hName, DynamicData* pValue)
{
	if (pObj->type == DynamicData::Type_Object)
	{
		DDObject* pObject = pObj->pObject;

		for (i32 i = 0; i < pObject->names.size(); ++i)
		{
			if (hName == pObject->names[i])
			{
				// todo: api add existing is error?
				pObject->values[i] = pValue;
				return;
			}
		}

		pObject->names.push_back(hName);
		pObject->values.push_back(pValue);
	}
}

inline void DynamicData_array_add(DynamicData* pArr, DynamicData* pValue)
{
	if (pArr->type == DynamicData::Type_Array)
	{
		DDArray* pArray = pArr->pArray;
		pArray->values.push_back(pValue);
	}
}

inline void DynamicData_array_insert(DynamicData* pObj, DynamicData* pValue, i32 index)
{
	if (pObj->type == DynamicData::Type_Array)
	{
		DDArray* pArray = pObj->pArray;
		pArray->values.insert(pArray->values.begin() + index, pObj);
	}
}

//f64 DynamicData_read_f64(u64 hName, const DynamicData* value)
//{
//	if (value->type == DynamicData::Type_Object)
//	{
//		DDObject* pObject = value->pObject;
//
//		for (i32 i = 0; i < pObject->names.size(); ++i)
//		{
//			if (hName == pObject->names[i])
//			{
//				return pObject->values[i]->asNumber();
//			}
//		}
//	}
//
//	if (value->type == DynamicData::Type_Instance)
//	{
//		DDInstance instance = value->instance;
//
//		if (instance.pOverride)
//		{
//			DDObject* pOverride = instance.pOverride;
//
//			for (i32 i = 0; i < pOverride->names.size(); ++i)
//			{
//				if (hName == pOverride->names[i])
//				{
//					return pOverride->values[i]->asNumber();
//				}
//			}
//		}
//
//		DDObject* pTemplate = lookup(instance.hTemplate)->pObject;
//
//		for (i32 i = 0; i < pTemplate->names.size(); ++i)
//		{
//			if (hName == pTemplate->names[i])
//			{
//				return pTemplate->values[i]->asNumber();
//			}
//		}
//	}
//
//	return 0.0;
//}


u64 string_repository_hash(const char* str);

const char* string_repository_get(u64 hName);

constexpr const char* s_typeNameKey = "type_name";
constexpr const char* s_fieldsKey = "fields";

struct DDEntity
{
	eastl::string name;
	eastl::vector<u64> children;
};

struct DDEntityReader
{
	const DDEntity* ref;

	const eastl::vector<u64>& readChildren();
	const eastl::string& readName();
};

struct DDEntityEditor
{
	DDEntity* ref;

	eastl::vector<u64>& editChildren();
	eastl::string& editName();

	const eastl::vector<u64>& readChildren();
	const eastl::string& readName();
};

struct DDTransformComponent
{
	float3 position;
};

constexpr u64 ENTITY_TYPE_ID = TM_STATIC_HASH("ENTITY_TYPE_ID", 0x5bf6f54407a5c834ULL);

inline void register_entity_type()
{
	DynamicData* root = DynamicData_obj_new();

	u64 hTypeName = string_repository_hash(s_typeNameKey);
	u64 hFields = string_repository_hash(s_fieldsKey);

	DynamicData* entityName = DynamicData_make_str("entity");
	DynamicData_obj_add(root, hTypeName, entityName);

	DynamicData* arrFields = DynamicData_array_new();

	DynamicData* field_children = DynamicData_obj_new();
	DynamicData_obj_add(field_children, string_repository_hash("children"), DynamicData_make_str("array"));
	DynamicData_array_add(arrFields, field_children);

	DynamicData_obj_add(root, hFields, arrFields);

	g_templates[ENTITY_TYPE_ID] = root;
}

void DynamicData_view(const DynamicData* pData);

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

