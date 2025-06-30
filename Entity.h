#pragma once

#include <cstdarg>

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

struct Printf
{
	Printf() = default;

	Printf(const char* fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		int result = vsnprintf(buf, 128, fmt, args);
		va_end(args);
	}

	void write(const char* fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		int result = vsnprintf(buf, 128, fmt, args);
		va_end(args);
	}

	operator const char* ()
	{
		return buf;
	}

	const char* cstr()
	{
		return buf;
	}

	char buf[128];
};

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
	u64 typeId;
	std::vector<u64> names;
	std::vector<DynamicData> values;
};

struct DDArray
{
	eastl::vector<DynamicData> values;
};

struct DDInstance
{
	u64 hTemplate;
	DDObject* pOverride;
};

DDObject* lookup_obj(u64 hObject);

struct DynamicData
{
	enum Type : u8
	{
		Type_Null,
		Type_Object,
		Type_Array,
		//Type_Instance,
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

	DDObject* asObject()
	{
		return type == Type_Object ? lookup_obj(hObject) : nullptr;
	}

	DDArray* asArray()
	{
		return type == Type_Array ? pArray : nullptr;
	}

	const DDObject* asObject() const
	{
		return type == Type_Object ? lookup_obj(hObject) : nullptr;
	}

	const DDArray* asArray() const
	{
		return type == Type_Array ? pArray : nullptr;
	}

	u64 id() const
	{
		return type == Type_Object ? hObject : 0ull;
	}

	union
	{
		//u64 hIns;
		u64 hObject;
		DDArray* pArray;
		i64 integer;
		f64 number;
		char* string;
	};

	Type type;
};

using Prototype = DynamicData;

void DynamicData_register_obj(const DynamicData* pObject, u64 id);

DynamicData DynamicData_obj_new();

DynamicData DynamicData_obj_from_type(u64 typeId);

DynamicData DynamicData_array_new();

DynamicData DynamicData_str_new();

DynamicData DynamicData_int_new();

DynamicData DynamicData_make_int(i64 integer);

DynamicData DynamicData_make_str(const char* str);


DynamicData DynamicData_make_null();

DynamicData DynamicData_num_new();

u64 DynamicData_size(const DynamicData* value);


void DynamicData_clone_internal(const DynamicData* src, DynamicData* dst);

DynamicData DynamicData_clone(const DynamicData* src);

inline DynamicData DynamicData_obj_find(DynamicData* pValue, u64 hName)
{
	if (pValue->type == DynamicData::Type_Object)
	{
		DDObject* pObject = pValue->asObject();

		for (i32 i = 0; i < pObject->names.size(); ++i)
		{
			if (hName == pObject->names[i])
			{
				return pObject->values[i];
			}
		}
	}

	return DynamicData_make_null();
}

inline void DynamicData_obj_add(DynamicData target, u64 hName, DynamicData add)
{
	if (DDObject* pObject = target.asObject())
	{
		for (i32 i = 0; i < pObject->names.size(); ++i)
		{
			if (hName == pObject->names[i])
			{
				// todo: api add existing is error?
				pObject->values[i] = add;
				return;
			}
		}

		pObject->names.push_back(hName);
		pObject->values.push_back(add);
	}
}

inline void DynamicData_array_add(DynamicData array, DynamicData value)
{
	if (array.type == DynamicData::Type_Array)
	{
		DDArray* pArray = array.pArray;
		pArray->values.push_back(value);
	}
}

u64 string_repository_hash(const char* str);

const char* string_repository_get(u64 hName);

constexpr const char* s_typeNameKey = "type_name";
constexpr const char* s_fieldsKey = "fields";
constexpr const char* s_typeIdKey = "__type_id";

struct DDEntity
{
	const char* name;
	eastl::vector<DynamicData> children;
	eastl::vector<DynamicData> components;
};

struct DDEntityReader
{
	const DDEntity* ref;

	const eastl::vector<u64>& readChildren();
	const char* readName();
};

struct DDEntityEditor
{
	DDEntity* ref;

	eastl::vector<u64>& editChildren();
	//eastl::string& editName();

	const eastl::vector<u64>& readChildren();
	const char* readName();
};

struct DDTransformComponent
{

	float x;
	float y;
	float z;
};

struct DynamicDataParser_i
{
	void (*parse)(DynamicData* value, void* target);
};

void DynamicData_registerParser(u64 hType, DynamicDataParser_i parser);

DDEntity DynamicData_readEntity(DynamicData* value);

constexpr u64 ENTITY_TYPE_ID = TM_STATIC_HASH("ENTITY_TYPE_ID", 0x5bf6f54407a5c834ULL);
constexpr u64 COMPONENT_ID_TRANSFORM = TM_STATIC_HASH("COMPONENT_ID_TRANSFORM", 0x24e93d7df3c9e6f0ULL);

void registerEntityTemplate();
void registerComponent_TransformTemplate();

DynamicData* DynamicData_getTemplate(u64 id);

DynamicData DynamicData_createFromTemplate(u64 hTemplate);

void DynamicData_view(DynamicData* pData);

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

