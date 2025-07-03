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

struct DDObject;
struct DDArray;
struct DDEdits;

struct DDInstance
{
	u64 hPrototype;
	u64 hEdits;
};

DDObject* lookup_obj(u64 hObject);
DDEdits* lookup_edits(u64 hEdits);

struct DynamicData
{
	enum Type : u8
	{
		Type_Null,
		Type_Object,
		Type_Array,
		Type_Instance,
		Type_Integer,
		Type_Number,
		Type_String
	};

	u64 asUint()
	{
		return type == Type_Integer ? integer : 0;
	}

	i64 asInt()
	{
		return type == Type_Integer ? integer : 0;
	}

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

	const DDObject* asObject() const
	{
		return type == Type_Object ? lookup_obj(hObject) : nullptr;
	}

	DDArray* asArray()
	{
		return type == Type_Array ? pArray : nullptr;
	}

	const DDArray* asArray() const
	{
		return type == Type_Array ? pArray : nullptr;
	}

	u64 id() const
	{
		if (type == Type_Object)
			return hObject;
		return 0ull;
	}

	union
	{
		DDInstance instance;
		u64 hObject;
		DDArray* pArray;
		i64 integer;
		f64 number;
		char* string;
	};

	Type type;
};

struct DynamicEdit
{
	enum Type : u8
	{
		Type_ArrayAppend,
		Type_ArrayPop,
		Type_ArraySet,

		Type_ObjectAdd,
		Type_ObjectRemove,
		Type_ObjectSet,
	};

	struct ArrayAppend
	{
		DynamicData value;
	};

	struct ArraySet
	{
		i32 index;
		DynamicData value;
	};

	struct ObjectAdd
	{
		DynamicData value;
	};

	struct ObjectSet
	{
		DynamicData value;
	};

	union
	{
		ObjectSet objectSet;
		ObjectAdd objectAdd;
		ArrayAppend arrayAppend;
		ArraySet arraySet;
	};

	Type type;
};

struct DDObject
{
	u64 hRoot;
	u64 hPrototype;

	struct Owned
	{
		eastl::vector<u64> names;
		eastl::vector<DynamicData> values;
	};

	struct Edits
	{
		eastl::vector<u64> names;
		eastl::vector<eastl::vector<DynamicEdit>> edits;
	};

	struct Flattened
	{
		eastl::vector<u64> names;
		eastl::vector<DynamicData> values;
		u64 basedOnVersion;
		bool dirty;
	};

	struct Instantiated
	{
		eastl::vector<u64> names;
		eastl::vector<DynamicData> values;
	};

	Owned owned;
	Edits edits;
	Instantiated instantiated;

	Flattened flattened;

	u64 version;
};

bool DDObject_is_up_to_date(DDObject* pObject, DDObject* pPrototype);

inline DynamicEdit DynamicEdit_arrayAdd(DynamicData value)
{
	DynamicEdit e;
	e.type = DynamicEdit::Type_ArrayAppend;
	e.arrayAppend.value = value;
	return e;
}

inline DynamicEdit DynamicEdit_arrayPop()
{
	DynamicEdit e;
	e.type = DynamicEdit::Type_ArrayPop;
	return e;
}

struct DDEdits
{
	eastl::vector<u64> names;
	eastl::vector<eastl::vector<DynamicEdit>> edits;
};

bool DynamicEdits_find(DDObject::Edits* edits, u64 hName, u64* outIndex);

struct DDArray
{
	u64 hRoot;
	eastl::vector<DynamicData> values;
};

enum class PropertyRelation : u8
{
	Owned,
	Override,
	Inherited,
	None
};

PropertyRelation DynamicData_get_relation(DynamicData* pValue, u64 hName);

DynamicData DynamicData_obj_new();
DynamicData DynamicData_new_from_prototype(DynamicData* pPrototype);
DynamicData DynamicData_instantiate_member(DynamicData* pValue, u64 hName);
DynamicData DynamicData_array_new();
DynamicData DynamicData_str_new();
DynamicData DynamicData_int_new();
DynamicData DynamicData_num_new();

DynamicData DynamicData_make_int(i64 integer);
DynamicData DynamicData_make_str(const char* str);
DynamicData DynamicData_make_null();
DynamicData DynamicData_make_num(f64 number);

u64 DynamicData_size(DynamicData* value);

void DynamicData_clone_internal(DynamicData* src, DynamicData* dst);

DynamicData DynamicData_clone(DynamicData* src);

DynamicData DynamicData_obj_find(DynamicData* pValue, u64 hName);

void DynamicData_assign_root(DynamicData* newRoot, DynamicData* value);

void DynamicData_obj_add(DynamicData* target, u64 hName, DynamicData add);

DynamicData DynamicData_obj_get(DynamicData* target, u64 hName);

void DynamicData_obj_set(DynamicData* target, u64 hName, DynamicData value);

void DynamicData_obj_arr_push(DynamicData* object, u64 hArrayName, DynamicData value);

//void DynamicData_array_add(DynamicData* array, DynamicData value);

void DynamicData_array_pop(DynamicData* array, DynamicData value);

void DynamicData_obj_before_read(DDObject* pObject);

eastl::vector<DynamicData> DynamicData_array_compose(DynamicData* object, u64 hName);

struct ArrayEditor
{
	struct Instance
	{
		eastl::vector<DynamicData> flatValues;
		DDObject::Edits* pEdits;
	};

	struct Owned
	{
		DDArray* pArray;
	};

	Instance instance;
	Owned owned;

	u64 hName;
	bool isInstanced;

	void push(DynamicData value);
	void pop();
	DynamicData get(u64 index);

	u64 size();
};

struct ObjectEditor
{
	struct Instance
	{
		eastl::vector<u64> flatNames;
		eastl::vector<DynamicData> flatValues;
		DDObject::Edits* pEdits;
	};

	struct Owned
	{
		DDObject* pObject;
	};

	void set(DynamicData value, u64 hName);
	DynamicData get(u64 hName);
};

ArrayEditor DynamicData_edit_array(DynamicData* object, u64 hName);
ObjectEditor DynamicData_edit_object(DynamicData* object, u64 hName);

u64 string_repository_hash(const char* str);

const char* string_repository_get(u64 hName);

bool float_almost_equal(float a, float b);

constexpr const char* s_typeNameKey = "type_name";
constexpr const char* s_fieldsKey = "fields";
constexpr const char* s_typeIdKey = "__type_id";

struct DDEntity
{
	enum
	{
		FieldMask_Name =  (1<<0),
		FieldMask_Children =  (1<<1),
		FieldMask_Components =  (1<<2),
	};

	u64 editedMask;
	u64 id;

	const char* name;
	eastl::vector<DynamicData> children;
	eastl::vector<DynamicData> components;
	f64 test_number;
};

struct DDEntityReader
{
	const DDEntity* ref;

	const eastl::vector<DynamicData>& readComponents() { return ref->components; }
	const eastl::vector<DynamicData>& readChildren() { return ref->children; }
	const char* readName() { return ref->name; }
};

struct DDEntityEditor
{
	DDEntity* ref;

	eastl::vector<DynamicData>& editChildren()
	{
		ref->editedMask |= DDEntity::FieldMask_Children;
		return ref->children;
	}

	void setName(const char* newName)
	{
		ref->editedMask |= DDEntity::FieldMask_Name;
		ref->name = newName;
	}

	eastl::vector<DynamicData>& editComponents()
	{
		ref->editedMask |= DDEntity::FieldMask_Components;
		return ref->components;
	}

	const eastl::vector<DynamicData>& readChildren() { return ref->children; }
	const char* readName() { return ref->name; }
};

struct DDTransformComponent
{
	enum : u8
	{
		FieldMask_X = (1 << 0),
		FieldMask_Y = (1 << 1),
		FieldMask_Z = (1 << 2),
	};

	u64 editedMask;
	u64 id;

	float x;
	float y;
	float z;
};

struct DDTransformComponentReader
{
	const DDTransformComponent* ref;

	float3 readPosition()
	{
		float3 val;
		val.x = ref->x;
		val.y = ref->y;
		val.z = ref->z;

		return val;
	}
};

struct DDTransformComponentEditor
{
	DDTransformComponent* ref;

	float3 readPosition()
	{
		float3 val;
		val.x = ref->x;
		val.y = ref->y;
		val.z = ref->z;

		return val;
	}

	void setPosition(float3 val)
	{
		if (!float_almost_equal(val.x, ref->x))
		{
			ref->editedMask |= DDTransformComponent::FieldMask_X;
			ref->x = val.x;
		}

		if (!float_almost_equal(val.y, ref->y))
		{
			ref->editedMask |= DDTransformComponent::FieldMask_Y;
			ref->y = val.y;
		}

		if (!float_almost_equal(val.z, ref->z))
		{
			ref->editedMask |= DDTransformComponent::FieldMask_Z;
			ref->z = val.z;
		}
	}
};

struct DynamicDataParser_i
{
	void (*parse)(DynamicData* value, void* target);
	void (*write_back)(DynamicData* value, void* data);
};

void DynamicData_registerParser(u64 hType, DynamicDataParser_i parser);

DDEntity DynamicData_readEntity(DynamicData* pEntity);
DDTransformComponent DynamicData_readTransform(DynamicData* pEntity);

void DynamicData_writeBack(DynamicData* pData, void* pValue);

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

