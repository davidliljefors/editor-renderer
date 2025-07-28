#pragma once

#include <cstdarg>

#include "Math.h"
#include "TruthMap.h"
#include "TruthView.h"
#include "Core/Array.h"
#include "Core/HashMap.h"


#include <vector>
#include <unordered_map>
#include <string>


#define _CRT_SECURE_NO_WARNINGS 1

#define DD_ARRAY_COUNT(a) (sizeof(a) / sizeof(a[0]))


struct DynamicEditorPath;

struct Printf
{
	Printf() = default;

	Printf(const char* fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		int result = vsnprintf(buf, 256, fmt, args);
		va_end(args);
	}

	void write(const char* fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		int result = vsnprintf(buf, 256, fmt, args);
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

	char buf[256];
};

u64 next_obj_id();

namespace eastl = std;

struct DDInstance;
struct DynamicData;

void Debug_register_root_object(DynamicData root);


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

struct DynamicObject;
struct DynamicSet;

DynamicObject* lookup_obj(u64 hObject);
DynamicSet* lookup_set(u64 hSet);

enum class MemberStatus
{
	Owned,
	Inherited,
	Instantiated,
	Overridden,
	Removed,
	None
};

struct DynamicData
{
	enum Type : u8
	{
		Type_Null,
		Type_Object,
		Type_Set,
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

	DynamicObject* asObject()
	{
		return type == Type_Object ? lookup_obj(hObject) : nullptr;
	}

	const DynamicObject* asObject() const
	{
		return type == Type_Object ? lookup_obj(hObject) : nullptr;
	}

	DynamicSet* asSet()
	{
		return type == Type_Set ? pSet : nullptr;
	}

	u64 id() const
	{
		if (type == Type_Object)
		{
			return hObject;
		}

		return 0ull;
	}

	bool isContainer()
	{
		return type == Type_Set || type == Type_Object;
	}

	bool isPod()
	{
		return type == Type_Integer || type == Type_Number || type == Type_String;
	}

	union
	{
		u64 hObject;
		DynamicSet* pSet;
		i64 integer;
		f64 number;
		char* string;
	};

	Type type;
	u8 _pad[7];
};


bool DDObject_is_up_to_date(DynamicObject* pObject, DynamicObject* pPrototype);

DynamicData DynamicData_obj_new();
DynamicData DynamicData_new_from_prototype(DynamicData* pPrototype);
DynamicData DynamicData_instantiate_subobject(DynamicData* pValue, u64 hName);
void	    DynamicData_clear_instantiated_subobject(DynamicData* pValue, u64 hName);
DynamicData DynamicData_set_new();
DynamicData DynamicData_str_new();
DynamicData DynamicData_int_new();
DynamicData DynamicData_num_new();

DynamicData DynamicData_make_int(i64 integer);
DynamicData DynamicData_make_str(const char* str);
DynamicData DynamicData_make_null();
DynamicData DynamicData_make_num(f64 number);

// set operations
DynamicData DyancmiData_instantiate_subobject_from_set(DynamicData* pValue, u64 hSetMember, DynamicData item);

void DynamicData_add_to_subobject_set(DynamicData* pValue, u64 hSetMember, DynamicData item);
void DynamicData_remove_from_subobject_set(DynamicData* pValue, u64 hSetMember, DynamicData item);

void DynamicData_remove_from_prototype_subobject_set(DynamicData* pValue, u64 hMember, DynamicData item);
void DynamicData_cancel_remove_from_prototype_subobject_set(DynamicData* pValue, u64 hSetMember, DynamicData item);

// todo api return temp allocated arrays
eastl::vector<DynamicData> DynamicData_get_subobject_set(DynamicData* pValue, u64 hSetMember);
eastl::vector<DynamicData> DynamicData_get_subobject_set_locally_removed(DynamicData* pValue, u64 hSetName);

bool DynamicData_obj_is_editable(DynamicData* pValue, u64 hName);

void DynamicData_instantiate_path(DynamicEditorPath* pPath, DynamicData value);

enum DynamicData_MemberStatus
{
	MemberStatus_Owned,
	MemberStatus_Added,
	MemberStatus_Removed,
	MemberStatus_Inherited,
	MemberStatus_Instantiated,
	MemberStatus_None
};

const char* to_string(MemberStatus status);
const char* to_string(DynamicData_MemberStatus status);


DynamicData DynamicData_clone(DynamicData* src);

DynamicData DynamicData_get_prototype(DynamicData* pValue);

DynamicData DynamicData_obj_get(DynamicData* pValue, u64 hName);

MemberStatus DynamicData_get_member_status(DynamicData* pValue, u64 hMember);

void DynamicData_assign_root(DynamicData* newRoot, DynamicData* value);

void DynamicData_obj_add(DynamicData* target, u64 hName, DynamicData add);

void DynamicData_obj_set(DynamicData* object, u64 hName, DynamicData value);

void DynamicData_obj_clear_override(DynamicData* pValue, u64 hMember);

struct ArrayEditor
{
	struct Instance
	{
		eastl::vector<DynamicData> flatValues;
		//DDObject::Edits* pEdits;
	};


	Instance instance;

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
		//DDObject::Edits* pEdits;
	};

	struct Owned
	{
		DynamicObject* pObject;
	};

	void set(DynamicData value, u64 hName);
	DynamicData get(u64 hName);
};

ArrayEditor DynamicData_edit_array(DynamicData* object, u64 hName);
ObjectEditor DynamicData_edit_object(DynamicData* object, u64 hName);

u64 string_repository_hash(const char* str);

const char* string_repository_get(u64 hName);

struct DebugValuePair
{
	eastl::string name;
	MemberStatus status;
	DynamicData value;
};

struct DynamicObjectDebugView
{
	eastl::vector<DebugValuePair> values;
};

DynamicObjectDebugView DynamicData_DebugExpression(u64 hObject);

bool float_almost_equal(float a, float b);

constexpr const char* s_typeNameKey = "type_name";
constexpr const char* s_typeIdKey = "__type_id";

constexpr static u64 s_hFields = TM_STATIC_HASH("fields", 0xfeae1f7e5ced00a4ULL);

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

	void addChild(DynamicData child);
	void removeChild(u64 id);

	const char* name;
	eastl::vector<DynamicData> children;
	eastl::vector<DynamicData> components;

	struct AddedChild
	{
		u64 id;
		DynamicData data;
	};

	eastl::vector<AddedChild> addedChildren;
	eastl::vector<u64> removedChildren;

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

struct DynamicDataPropertyDef
{
	const char* name;
	DynamicData::Type type;
	u64 typeHash;
};

u64 DynamicData_register_type(const char* name, const DynamicDataPropertyDef* properties, u32 num_properties);

DynamicData* DynamicData_get_template(u64 id);

eastl::vector<u64> DynamicData_get_all_types();

DynamicData DynamicData_create_from_template(u64 hName);

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

