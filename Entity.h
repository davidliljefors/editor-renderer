#pragma once

#include <cstdarg>

#include "Math.h"
#include "TruthMap.h"
#include "TruthView.h"
#include "Core/Array.h"
#include "Core/HashMap.h"
#include <vector>


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
	u64 typeHash;
};

bool DDObject_is_up_to_date(DynamicObject* pObject, DynamicObject* pPrototype);

void DynamicData_initialize(Allocator* a);
void DynamicData_shutdown();

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
DynamicData DynamicData_instantiate_subobject_from_set(DynamicData* pParent, u64 hSetMember, DynamicData* pValue);
void DynamicData_remove_instantiated_subobject_from_set(DynamicData* pParent, u64 hSetMember, DynamicData* pValue);

void DynamicData_add_to_subobject_set(DynamicData* pParent, u64 hSetMember, DynamicData* pValue);
void DynamicData_remove_from_subobject_set(DynamicData* pParent, u64 hSetMember, DynamicData* pValue);

void DynamicData_remove_from_prototype_subobject_set(DynamicData* pParent, u64 hSetMember, DynamicData* pValue);
void DynamicData_cancel_remove_from_prototype_subobject_set(DynamicData* pParent, u64 hSetMember, DynamicData* pValue);

// todo api return temp allocated arrays
eastl::vector<DynamicData> DynamicData_get_subobject_set(DynamicData* pValue, u64 hSetMember);
eastl::vector<DynamicData> DynamicData_get_subobject_set_locally_removed(DynamicData* pValue, u64 hSetName);

bool DynamicData_obj_is_editable(DynamicData* pValue, u64 hName);

void DynamicData_instantiate_path(DynamicEditorPath* pPath, DynamicData value);

const char* to_string(MemberStatus status);

DynamicData DynamicData_clone(DynamicData* src);

DynamicData DynamicData_get_prototype(DynamicData* pValue);

DynamicData DynamicData_obj_get(DynamicData* pValue, u64 hName);

MemberStatus DynamicData_get_member_status(DynamicData* pValue, u64 hMember);

MemberStatus DynamicData_get_member_relation(DynamicData* pParent, u64 hMember, DynamicData* pValue);

void DynamicData_assign_root(DynamicData* newRoot, DynamicData* value);

void DynamicData_obj_add(DynamicData* target, u64 hName, DynamicData add);

void DynamicData_obj_set(DynamicData* object, u64 hName, DynamicData value);

void DynamicData_obj_clear_override(DynamicData* pValue, u64 hMember);

u64 string_repository_hash(const char* str);

const char* string_repository_get(u64 hName);

bool float_almost_equal(float a, float b);



u64 DynamicData_register_type(const char* name, const DynamicDataPropertyDef* properties, u32 num_properties);

DynamicData* DynamicData_get_template(u64 id);

eastl::vector<u64> DynamicData_get_all_types();

DynamicData DynamicData_create_from_template(u64 hName);

void DynamicData_view(DynamicData* pData);
