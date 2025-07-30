#pragma once

#include "Core/Array.h"
#include "Core/Types.h"

#define DD_ARRAY_COUNT(a) (sizeof(a) / sizeof(a[0]))

struct DynamicEditorPath;

struct Printf
{
	Printf() = default;

	Printf(const char* fmt, ...);

	void write(const char* fmt, ...);

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

void Debug_register_root_object(struct DynamicData root);

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
	Added,
	Set, // todo remove
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

	bool isString()
	{
		return type == Type_String;
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

struct DynamicDataPropertyDef
{
	const char* name;
	DynamicData::Type type;
	u64 typeNameHash;

	u64 nameHash;
	i32 typeId;
};

inline DynamicDataPropertyDef makeProperty(const char* name, DynamicData::Type type, u64 typeNameHash = 0)
{
	DynamicDataPropertyDef def;

	def.name = name;
	def.type = type;
	def.typeNameHash = typeNameHash;

	def.nameHash = 0;
	def.typeId = 0;

	return def;
}

bool DDObject_is_up_to_date(DynamicObject* pObject, DynamicObject* pPrototype);

void DynamicData_initialize(Allocator* a);
void DynamicData_shutdown();

DynamicData DynamicData_obj_new();
DynamicData DynamicData_new_from_prototype(DynamicData* pPrototype);
DynamicData DynamicData_instantiate_subobject(DynamicData* pValue, u64 hMember);
void	    DynamicData_clear_instantiated_subobject(DynamicData* pValue, u64 hMember);
DynamicData DynamicData_set_new();
DynamicData DynamicData_str_new();
DynamicData DynamicData_int_new();
DynamicData DynamicData_num_new();

DynamicData DynamicData_make_int(i64 integer);
DynamicData DynamicData_make_str(const char* str);
DynamicData DynamicData_make_null();
DynamicData DynamicData_make_num(f64 number);

// set operations
DynamicData DynamicData_instantiate_subobject_from_set(DynamicData* pParent, u64 hSetMember, DynamicData* pValue);
void DynamicData_remove_instantiated_subobject_from_set(DynamicData* pParent, u64 hSetMember, DynamicData* pValue);

void DynamicData_add_to_subobject_set(DynamicData* pParent, u64 hSetMember, DynamicData* pValue);
void DynamicData_remove_from_subobject_set(DynamicData* pParent, u64 hSetMember, DynamicData* pValue);

void DynamicData_remove_from_prototype_subobject_set(DynamicData* pParent, u64 hSetMember, DynamicData* pValue);
void DynamicData_cancel_remove_from_prototype_subobject_set(DynamicData* pParent, u64 hSetMember, DynamicData* pValue);

Array<DynamicData> DynamicData_get_subobject_set(DynamicData* pValue, u64 hSetMember, Allocator* a);
Array<DynamicData> DynamicData_get_subobject_set_locally_removed(DynamicData* pValue, u64 hSetName, Allocator* a);

DynamicData DynamicData_create_from_type(i32 typeId);
DynamicData DynamicData_clone(DynamicData* pValue);
DynamicData DynamicData_obj_get(DynamicData* pValue, u64 hMember);

MemberStatus DynamicData_get_member_status(DynamicData* pValue, u64 hMember);

MemberStatus DynamicData_get_member_relation(DynamicData* pParent, u64 hMember, DynamicData* pValue);

void DynamicData_assign_root(DynamicData* newRoot, DynamicData* value);

void DynamicData_obj_set(DynamicData* object, u64 hMember, DynamicData value);

void DynamicData_obj_clear_override(DynamicData* pValue, u64 hMember);

u64 string_repository_hash(const char* str);

const char* string_repository_own(const char* str);

const char* string_repository_get(u64 hName);

i32 DynamicData_register_type(const char* typeName, const DynamicDataPropertyDef* properties, i32 numProperties);

DynamicData DynamicData_create_from_type_name(u64 hTypeNameHash);

void DynamicData_view(DynamicData* pData);
