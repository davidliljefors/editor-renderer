#pragma once

#include "Core/Array.h"
#include "Core/Types.h"

#define DD_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

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

void Debug_register_root_object(struct dd_id_t root);

struct DynamicObject;
struct DynamicSet;

// an id to an object,
// use DynamicData_[read/edit]_object()
// to aquire read/write access to the object
struct dd_id_t
{
	u64 as_u64;

	friend bool operator==(const dd_id_t& lhs, const dd_id_t& rhs) { return lhs.as_u64 == rhs.as_u64; }
};

// opaque struct representing an object
struct dd_obj;

const dd_obj* DynamicData_read_object(dd_id_t obj_id);
dd_obj* DynamicData_edit_object(dd_id_t obj_id);

enum class MemberStatus : u8
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

struct DynamicValue
{
	enum Type : u8
	{
		Type_Null,
		Type_Object,
		Type_Set,
		Type_Integer,
		Type_Number,
		Type_String,
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

	DynamicSet* asSet()
	{
		return type == Type_Set ? pSet : nullptr;
	}

	dd_id_t id() const
	{
		if (type == Type_Object)
		{
			return obj_id;
		}

		return {0ull};
	}

	bool isString()
	{
		return type == Type_String;
	}

	union
	{
		dd_id_t obj_id;
		DynamicSet* pSet;
		i64 integer;
		f64 number;
		const char* string;
	};

	i32 obj_type;
	Type type;

	friend bool operator==(const DynamicValue& lhs, const DynamicValue& rhs) { return lhs.obj_id.as_u64 == rhs.obj_id.as_u64 && lhs.type == rhs.type; }
	friend bool operator< (const DynamicValue& lhs, const DynamicValue& rhs)
	{
		return lhs.type == rhs.type ? lhs.obj_id.as_u64 < rhs.obj_id.as_u64 : lhs.type < rhs.type;
	}
};

struct DynamicDataPropertyDef
{
	const char* name;
	DynamicValue::Type type;
	u64 typeNameHash;

	u64 nameHash;
	i32 typeId;
};

DynamicDataPropertyDef makeProperty(const char* name, DynamicValue::Type type, u64 typeNameHash = 0);

void DynamicData_initialize(Allocator* a);
void DynamicData_shutdown();

dd_id_t DynamicData_instantiate_subobject(dd_obj* obj, u64 hMember);
void DynamicData_clear_instantiated_subobject(dd_obj* obj, u64 hMember);

f64 DynamicData_get_float(const dd_obj* object, u64 hMember);
i64 DynamicData_get_int(const dd_obj* object, u64 hMember);
const char* DynamicData_get_string(const dd_obj* object, u64 hMember);
dd_id_t DynamicData_get_subobject(const dd_obj* object, u64 hMember);

void DynamicData_set_float(dd_obj* object, u64 hMember, f64 value);
void DynamicData_set_int(dd_obj* object, u64 hMember, i64 value);
void DynamicData_set_string(dd_obj* object, u64 hMember, const char* value);

// set operations
dd_id_t DynamicData_instantiate_subobject_from_set(dd_obj* obj, u64 hMember, dd_id_t subobject);
void DynamicData_remove_instantiated_subobject_from_set(dd_obj* obj, u64 hMember, dd_id_t subobject);

void DynamicData_add_to_subobject_set(dd_obj* obj, u64 hMember, dd_id_t subobject);
void DynamicData_remove_from_subobject_set(dd_obj* obj, u64 hMember, dd_id_t subobject);

void DynamicData_remove_from_prototype_subobject_set(dd_obj* obj, u64 hMember, dd_id_t subobject);
void DynamicData_cancel_remove_from_prototype_subobject_set(dd_obj* obj, u64 hMember, dd_id_t subobject);

Array<dd_id_t> DynamicData_get_subobject_set(const dd_obj* obj, u64 hMember, Allocator* a);
Array<dd_id_t> DynamicData_get_subobject_set_locally_removed(const dd_obj* obj, u64 hMember, Allocator* a);

dd_id_t DynamicData_create_from_type(i32 typeId);
dd_id_t DynamicData_clone(dd_id_t obj);

MemberStatus DynamicData_get_member_status(const dd_obj* obj, u64 hMember);

MemberStatus DynamicData_get_member_relation(dd_id_t parent, u64 hMember, dd_id_t obj);

void DynamicData_obj_assign(dd_obj* object, u64 hMember, DynamicValue value);

void DynamicData_obj_clear_override(dd_obj* object, u64 hMember);

u64 string_repository_hash(const char* str);

const char* string_repository_own(const char* str);

const char* string_repository_get(u64 hName);

i32 DynamicData_register_type(const char* typeName, const char* uiName, const DynamicDataPropertyDef* properties, i32 numProperties);

dd_id_t DynamicData_create_from_type_name(u64 hTypeNameHash);

void DynamicData_view(dd_id_t object);

void DynamicData_view_draw_type_registry();

// Serialization

void DynamicData_serialize_json_file(const char* name, const dd_obj* object);

struct DynamicData_UnresolvedObject
{
	union
	{
		struct
		{
			DynamicSet* pSet;
			Guid guid;

			dd_id_t instantiated;
			bool isRemove;
		} set;

		struct
		{
			Guid prototype;
			dd_id_t object_id;
		} object;
	};

	bool isSet;
};

bool DynamicData_deserialize_json_file(const char* path, dd_id_t* outCreated, Array<DynamicData_UnresolvedObject>* inoutUnresolved);

void DynamicData_resolve_unresolved(Array<DynamicData_UnresolvedObject>* unresolveds);