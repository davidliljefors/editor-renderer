#include "DynamicData.h"

#include <cstdarg>
#include <cstdio>

#include "imgui.h"
#include "yyjson.h"

#include "Core/HashMap.h"
#include "Core/Array.h"
#include "Core/TempAllocator.h"


#include "murmurhash.inl"
#include "Random.h"

static u64 s_object_id = 0;


extern Array<i32>* Debug_get_component_ids();

Printf::Printf(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	int result = vsnprintf(buf, 256, fmt, args);
	(void)result;
	va_end(args);
}

void Printf::write(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	int result = vsnprintf(buf, 256, fmt, args);
	(void)result;
	va_end(args);
}

template<auto N>
void write_guid_str(char(&buffer)[N], const Guid& guid)
{
	static_assert(N >= 36);

	static constexpr char hex_lookup[] = "0123456789abcdef";
	u8 bytes[16];

	for (int i = 0; i < 8; i++)
	{
		bytes[i] = (guid.a >> ((7 - i) * 8)) & 0xFF;
	}

	for (int i = 0; i < 8; i++)
	{
		bytes[i + 8] = (guid.b >> ((7 - i) * 8)) & 0xFF;
	}

	int buf_idx = 0;
	for (int i = 0; i < 16; i++)
	{
		if (i == 4 || i == 6 || i == 8 || i == 10)
		{
			buffer[buf_idx++] = '-';
		}
		buffer[buf_idx++] = hex_lookup[bytes[i] >> 4];
		buffer[buf_idx++] = hex_lookup[bytes[i] & 0xF];
	}

	if constexpr (N > 36)
	{
		buffer[36] = '\0';
	}
}

u64 next_obj_id()
{
	return s_object_id++;
}

#define BREAK_ON_ERROR 1
#define DYNAMIC_DATA_ERROR(msg) printf("error: %s. line:%d\n", msg, __LINE__); if(BREAK_ON_ERROR)__debugbreak()

Allocator* DD_ALLOCATOR;
Allocator* STRING_REPOSITORY_ALLOCATOR;


bool findName(const u64* pNames, i32 count, u64 hName, i32* outIndex)
{
	for (i32 i = 0; i < count; ++i)
	{
		if (hName == pNames[i])
		{
			*outIndex = i;
			return true;
		}
	}
	return false;
}



bool findId(const Array<dd_id_t>& values, dd_id_t id, i32* outIndex)
{
	for (i32 i = 0; i < values.size(); ++i)
	{
		if (id == values[i])
		{
			*outIndex = i;
			return true;
		}
	}
	return false;
}

bool findValue(const Array<DynamicValue>& values, DynamicValue* pValue, i32* outIndex)
{
	for (i32 i = 0; i < values.size(); ++i)
	{
		if (values[i].type == pValue->type && values[i].obj_id.as_u64 == pValue->obj_id.as_u64)
		{
			*outIndex = i;
			return true;
		}
	}
	return false;
}

constexpr dd_id_t INVALID_OBJECT_ID {};

dd_id_t id_from_key(u64 key)
{
	return {key};
}

struct dd_obj
{
	struct Members
	{
		DynamicValue* values;
		MemberStatus* statuses;
	};

	dd_id_t id;
	Guid guid;
	u64 version;

	Members members;
	dd_id_t prototype;
	i32 typeId;
};

struct DynamicSet
{
	struct Added
	{
		HashMap<dd_id_t> values;
	};

	struct Removed
	{
		HashMap<dd_id_t> values;
	};

	struct Instantiated
	{
		HashMap<dd_id_t> instance_to_id;
		HashMap<dd_id_t> id_to_instance;

		bool contains_id(dd_id_t id)
		{
			return id_to_instance.contains(id.as_u64);
		}

		bool contains_instance(dd_id_t instance)
		{
			return instance_to_id.contains(instance.as_u64);
		}
	};

	i32 typeId; // type of subobjects stored in the set
	Added added;
	Removed removed;
	Instantiated instantiated;
};

struct DynamicType
{
	i32 typeId;
	u64 typeNameHash;
	const char* typeName;
	const char* uiName;

	i32 numProperties;
	u64* nameHashToProperty;
	DynamicDataPropertyDef* properties;
};

bool findProperty(const DynamicType* pType, u64 hName, i32* outIndex)
{
	for (i32 i = 0; i < pType->numProperties; ++i)
	{
		if (hName == pType->properties[i].nameHash)
		{
			*outIndex = i;
			return true;
		}
	}
	return false;
}

static HashMap<const char*> s_string_repository;
static HashMap<i32> s_typeNameToTypeId;
static Array<DynamicType*> s_types;

static HashMap<dd_obj*> s_objects;
static HashMap<dd_id_t> s_guidToObject;

DynamicType* DynamicData_get_type_from_id(i32 typeId)
{
	return s_types[typeId];
}

dd_id_t* DynamicData_get_from_guid(Guid guid)
{
	u64 key = guid.a ^ guid.b;
	return s_guidToObject.find(key);
}

const dd_obj* DynamicData_read_object(dd_id_t obj_id)
{
	auto find = s_objects.find(obj_id.as_u64);
	return find ? *find : nullptr;
}

dd_obj* DynamicData_edit_object(dd_id_t obj_id)
{
	auto find = s_objects.find(obj_id.as_u64);
	return find ? *find : nullptr;
}



DynamicDataPropertyDef makeProperty(const char* name, DynamicValue::Type type, u64 typeNameHash)
{
	DynamicDataPropertyDef def;

	def.name = name;
	def.type = type;
	def.typeNameHash = typeNameHash;

	def.nameHash = 0;
	def.typeId = 0;

	return def;
}

void DynamicData_initialize(Allocator* a)
{
	DD_ALLOCATOR = a;
	STRING_REPOSITORY_ALLOCATOR = a;

	s_typeNameToTypeId.set_allocator(a);
	s_string_repository.set_allocator(a);
	s_types.set_allocator(a);
	s_objects.set_allocator(a);
	s_guidToObject.set_allocator(a);
	s_object_id = 1;

	DynamicData_register_type("null", "Null Object Type", nullptr, 0);
}

void DynamicData_shutdown()
{
	s_typeNameToTypeId.reset();
	s_string_repository.reset();
	s_types.reset();
	s_objects.reset();
	s_guidToObject.reset();
}

dd_obj* DynamicData_obj_new_with_guid(Guid guid)
{
	dd_obj* obj = new (DD_ALLOCATOR) dd_obj();

	dd_id_t id = id_from_key(next_obj_id());

	obj->version = 1;
	obj->id = id;
	obj->guid = guid;

	s_objects.add(id.as_u64, obj);

	u64 key = guid.a ^ guid.b;
	s_guidToObject.add(key, id);

	return obj;
}

dd_obj* DynamicData_obj_new()
{
	return DynamicData_obj_new_with_guid(Random_guid());
}

DynamicValue DynamicData_set_new()
{
	DynamicValue value;

	value.type = DynamicValue::Type_Set;
	value.pSet = new (DD_ALLOCATOR) DynamicSet();

	value.pSet->instantiated.id_to_instance.set_allocator(DD_ALLOCATOR);
	value.pSet->instantiated.instance_to_id.set_allocator(DD_ALLOCATOR);
	value.pSet->added.values.set_allocator(DD_ALLOCATOR);
	value.pSet->removed.values.set_allocator(DD_ALLOCATOR);
	value.obj_type = 0;

	return value;
}

DynamicValue DynamicData_str_new()
{
	DynamicValue value;

	value.type = DynamicValue::Type_String;
	value.string = "";
	value.obj_type = 0;

	return value;
}

DynamicValue DynamicData_int_new()
{
	DynamicValue value;

	value.type = DynamicValue::Type_Integer;
	value.integer = 0;
	value.obj_type = 0;

	return value;
}

DynamicValue DynamicData_num_new()
{
	DynamicValue value;

	value.type = DynamicValue::Type_Number;
	value.number = 0.0;
	value.obj_type = 0;

	return value;
}

DynamicValue DynamicData_make_int(i64 integer)
{
	DynamicValue value = DynamicData_int_new();
	value.integer = integer;
	return value;
}

DynamicValue DynamicData_make_str(const char* str)
{
	DynamicValue value = DynamicData_str_new();

	u64 capacity = strlen(str) + 1;
	char* str_copy = (char*)DD_ALLOCATOR->alloc(capacity);
	strcpy_s(str_copy, capacity, str);
	value.string = str_copy;

	return value;
}

DynamicValue DynamicData_make_null()
{
	DynamicValue value;
	value.type = DynamicValue::Type_Null;
	value.integer = 0;
	value.obj_type = 0;

	return value;
}

DynamicValue DynamicData_make_num(f64 number)
{
	DynamicValue value = DynamicData_num_new();
	value.number = number;

	return value;
}

DynamicValue DynamicData_obj_get(const dd_obj* obj, u64 hMember)
{
	DynamicType* pType = s_types[obj->typeId];

	i32 i;
	if (findProperty(pType, hMember, &i))
	{
		if (obj->members.statuses[i] == MemberStatus::Inherited)
		{
			return DynamicData_obj_get(DynamicData_read_object(obj->prototype), hMember);
		}
		else
		{
			return obj->members.values[i];
		}
	}

	return DynamicData_make_null();
}

f64 DynamicData_get_float(const dd_obj* object, u64 hMember)
{
	DynamicValue val = DynamicData_obj_get(object, hMember);
	return val.type == DynamicValue::Type_Number ? val.number : 0.0;
}

i64 DynamicData_get_int(const dd_obj* object, u64 hMember)
{
	DynamicValue val = DynamicData_obj_get(object, hMember);
	return val.type == DynamicValue::Type_Integer ? val.integer : 0;
}

const char* DynamicData_get_string(const dd_obj* object, u64 hMember)
{
	DynamicValue val = DynamicData_obj_get(object, hMember);
	return val.type == DynamicValue::Type_String ? val.string : "";
}

dd_id_t DynamicData_get_subobject(const dd_obj* object, u64 hMember)
{
	DynamicValue val = DynamicData_obj_get(object, hMember);
	return val.type == DynamicValue::Type_Object ? val.obj_id : INVALID_OBJECT_ID;
}

void DynamicData_set_float(dd_obj* object, u64 hMember, f64 value)
{
	DynamicData_obj_assign(object, hMember, DynamicData_make_num(value));
}

void DynamicData_set_int(dd_obj* object, u64 hMember, i64 value)
{
	DynamicData_obj_assign(object, hMember, DynamicData_make_int(value));
}

void DynamicData_set_string(dd_obj* object, u64 hMember, const char* value)
{
	DynamicData_obj_assign(object, hMember, DynamicData_make_str(value));
}

dd_id_t DynamicData_instantiate(dd_id_t prototype)
{
	const dd_obj* proto_obj = DynamicData_read_object(prototype);

	DynamicType* pType = s_types[proto_obj->typeId];
	dd_id_t created = DynamicData_create_from_type(proto_obj->typeId);
	dd_obj* obj = DynamicData_edit_object(created);

	for (i32 i = 0; i < pType->numProperties; ++i)
	{
		if (proto_obj->members.values[i].type == DynamicValue::Type_Set)
		{
			obj->members.values[i] = DynamicData_set_new();
			obj->members.values[i].asSet()->typeId = proto_obj->members.values[i].asSet()->typeId;
			obj->members.statuses[i] = MemberStatus::Set;
		}
		else
		{
			obj->members.values[i] = proto_obj->members.values[i];
			obj->members.statuses[i] = MemberStatus::Inherited;
		}
	}

	obj->prototype = prototype;

	return created;
}

dd_id_t DynamicData_instantiate_member_impl(dd_obj* obj, u64 hName)
{
	i32 i;
	DynamicType* pType = s_types[obj->typeId];
	dd_id_t created = INVALID_OBJECT_ID;

	if (findProperty(pType, hName, &i))
	{
		if (obj->members.statuses[i] == MemberStatus::Inherited)
		{
			dd_id_t prototypeId = obj->members.values[i].obj_id;

			created = DynamicData_instantiate(prototypeId);
			obj->members.values[i].obj_id = created;
			obj->members.statuses[i] = MemberStatus::Instantiated;
		}
		else
		{
			DYNAMIC_DATA_ERROR("Instantiate something that is not inherited is not valid");
		}
	}

	return created;
}

dd_id_t DynamicData_instantiate_subobject(dd_obj* obj, u64 hMember)
{
	dd_id_t created = INVALID_OBJECT_ID;

	const dd_obj* prototype = DynamicData_read_object(obj->prototype);

	DynamicType* pType = s_types[obj->typeId];

	i32 i;
	if (findProperty(pType, hMember, &i))
	{
		if (obj->members.statuses[i] == MemberStatus::Inherited)
		{
			if (prototype->members.statuses[i] == MemberStatus::Instantiated || prototype->members.statuses[i] == MemberStatus::Owned)
			{
				created = DynamicData_instantiate(prototype->members.values[i].obj_id);
				obj->members.values[i].obj_id = created;
				obj->members.statuses[i] = MemberStatus::Instantiated;
			}
			else
			{
				DYNAMIC_DATA_ERROR("Need to Instantiate chain");
			}
		}
		else
		{
			DYNAMIC_DATA_ERROR("Instantiate something that is not inherited is not valid");
		}
	}

	return created;
}

void DynamicData_clear_instantiated_subobject(dd_obj* obj, u64 hMember)
{
	DynamicType* pType = s_types[obj->typeId];
	i32 i;
	if (findProperty(pType, hMember, &i))
	{
		if (obj->members.statuses[i] == MemberStatus::Instantiated)
		{
			obj->members.statuses[i] = MemberStatus::Inherited;
		}
	}
}

dd_id_t DynamicData_instantiate_subobject_from_set(dd_obj* obj, u64 hMember, dd_id_t subobject)
{
	if (obj->prototype == INVALID_OBJECT_ID)
	{
		DYNAMIC_DATA_ERROR("Instantiate only valid on objects with a prototype");
		return INVALID_OBJECT_ID;
	}

	DynamicType* pType = s_types[obj->typeId];

	i32 i;
	if (findProperty(pType, hMember, &i))
	{
		if (obj->members.statuses[i] == MemberStatus::Set)
		{
			if (DynamicSet* pSet = obj->members.values[i].asSet())
			{
				if (pSet->instantiated.id_to_instance.contains(subobject.as_u64))
				{
					DYNAMIC_DATA_ERROR("value is already instantiated");
				}
				else
				{
					// todo do we need to get set here
					TempAllocator ta;
					Array<dd_id_t> set = DynamicData_get_subobject_set(obj, hMember, &ta);
					if (findId(set, subobject, &i))
					{
						dd_id_t instantiated_subobject = DynamicData_instantiate(subobject);
						pSet->instantiated.id_to_instance.add(subobject.as_u64, instantiated_subobject);
						pSet->instantiated.instance_to_id.add(instantiated_subobject.as_u64, subobject);
						return instantiated_subobject;
					}
				}
			}
			else
			{
				DYNAMIC_DATA_ERROR("set is not instantiated");
			}
		}
	}
	else
	{
		DYNAMIC_DATA_ERROR(Printf("Object does not contain member [%s]", string_repository_get(hMember)).cstr());
	}

	return INVALID_OBJECT_ID;
}

void DynamicData_remove_instantiated_subobject_from_set(dd_obj* obj, u64 hMember, dd_id_t subobject)
{
	if (obj->prototype == INVALID_OBJECT_ID)
	{
		DYNAMIC_DATA_ERROR("Instantiate only valid on objects with a prototype");
		return;
	}

	DynamicType* pType = s_types[obj->typeId];

	i32 i;
	if (findProperty(pType, hMember, &i))
	{
		if (DynamicSet* pSet = obj->members.values[i].asSet())
		{
			dd_id_t* id = pSet->instantiated.instance_to_id.find(subobject.as_u64);
			if (id)
			{
				pSet->instantiated.instance_to_id.erase(subobject.as_u64);
				pSet->instantiated.id_to_instance.erase(id->as_u64);
			}
			else
			{
				DYNAMIC_DATA_ERROR("value is not instantiated in set");
			}
		}
	}
	else
	{
		DYNAMIC_DATA_ERROR(Printf("Object does not contain member [%s]", string_repository_get(hMember)).cstr());
	}
}

void DynamicData_add_to_subobject_set(dd_obj* obj, u64 hMember, dd_id_t subobject)
{
	DynamicType* pType = s_types[obj->typeId];

	i32 i;
	if (findProperty(pType, hMember, &i) && pType->properties[i].type == DynamicValue::Type_Set)
	{
		if (DynamicSet* pSet = obj->members.values[i].asSet())
		{
			pSet->added.values.add(subobject.as_u64, subobject);
		}
	}
	else
	{
		DYNAMIC_DATA_ERROR(Printf("Object does not contain member [%s]", string_repository_get(hMember)).cstr());
	}
}

void DynamicData_remove_from_subobject_set(dd_obj* obj, u64 hMember, dd_id_t subobject)
{
	DynamicType* pType = s_types[obj->typeId];

	i32 i;
	if (findProperty(pType, hMember, &i) && pType->properties[i].type == DynamicValue::Type_Set)
	{
		DynamicSet* pSet = obj->members.values[i].asSet();
		bool removed = pSet->added.values.erase(subobject.as_u64);

		if (!removed)
		{
			DYNAMIC_DATA_ERROR("object not in the local set");
		}
	}
	else
	{
		DYNAMIC_DATA_ERROR(Printf("Object does not contain member [%s]", string_repository_get(hMember)).cstr());
	}
}

void DynamicData_remove_from_prototype_subobject_set(dd_obj* obj, u64 hMember, dd_id_t subobject)
{
	if (obj->prototype == INVALID_OBJECT_ID)
	{
		DYNAMIC_DATA_ERROR("remove only valid on objects with a prototype");
		return;
	}

	DynamicType* pType = s_types[obj->typeId];
	i32 i;
	if (findProperty(pType, hMember, &i) && pType->properties[i].type == DynamicValue::Type_Set)
	{
		DynamicSet* pSet = obj->members.values[i].asSet();
		pSet->removed.values.add(subobject.as_u64, subobject);
	}
	else
	{
		DYNAMIC_DATA_ERROR(Printf("Object does not contain member [%s]", string_repository_get(hMember)).cstr());
	}
}

void DynamicData_cancel_remove_from_prototype_subobject_set(dd_obj* obj, u64 hMember, dd_id_t subobject)
{
	if (obj->prototype == INVALID_OBJECT_ID)
	{
		DYNAMIC_DATA_ERROR("remove only valid on objects with a prototype");
		return;
	}

	DynamicType* pType = s_types[obj->typeId];
	i32 i;
	if (findProperty(pType, hMember, &i) && pType->properties[i].type == DynamicValue::Type_Set)
	{
		DynamicSet* pSet = obj->members.values[i].asSet();
		bool removed = pSet->removed.values.erase(subobject.as_u64);

		if (!removed)
		{
			DYNAMIC_DATA_ERROR("value is not instantiated in set");
		}
	}
	else
	{
		DYNAMIC_DATA_ERROR(Printf("Object does not contain member [%s]", string_repository_get(hMember)).cstr());
	}
}

void DynamicData_set_compose(const dd_obj* obj, u64 index, Array<dd_id_t>& ids, Array<dd_id_t>& values)
{
	if (obj->members.values[index].type != DynamicValue::Type_Set)
	{
		DYNAMIC_DATA_ERROR("Member is not a set");
		return;
	}

	const dd_obj* prototype = DynamicData_read_object(obj->prototype);

	if (prototype)
	{
		DynamicData_set_compose(prototype, index, ids, values);
	}

	DynamicSet* pSet = obj->members.values[index].asSet();

	for (auto& add : pSet->added.values)
	{
		ids.push_back(id_from_key(add.key));
		values.push_back(add.value);
	}

	for (auto& remove : pSet->removed.values)
	{
		i32 index;
		findId(ids, {remove.key}, &index);

		ids.erase(index);
		values.erase(index);
	}

	for (const auto& entry : pSet->instantiated.id_to_instance)
	{
		dd_id_t id = id_from_key(entry.key);
		dd_id_t val = entry.value;

		i32 index;
		findId(ids, id, &index);
		ids[index] = val;
		values[index] = val;
	}
}

Array<dd_id_t> DynamicData_get_subobject_set(const dd_obj* obj, u64 hMember, Allocator* a)
{
	Array<dd_id_t> ids(a);
	Array<dd_id_t> values(a);

	DynamicType* pType = s_types[obj->typeId];

	i32 index;
	findProperty(pType, hMember, &index);
	DynamicData_set_compose(obj, index, ids, values);


	return values;
}

Array<dd_id_t> DynamicData_get_subobject_set_locally_removed(const dd_obj* obj, u64 hMember, Allocator* a)
{
	Array<dd_id_t> setMembers(a);

	DynamicType* pType = s_types[obj->typeId];

	i32 i;
	if (findProperty(pType, hMember, &i))
	{
		if (pType->properties[i].type == DynamicValue::Type_Set)
		{
			DynamicSet* pSet = obj->members.values[i].asSet();
			setMembers.reserve(pSet->removed.values.size());
			for (auto& data : pSet->removed.values)
			{
				setMembers.push_back(data.value);
			}
		}
		else
		{
			DYNAMIC_DATA_ERROR("Get set on non set");
		}
	}

	return setMembers;
}

dd_obj* DynamicData_create_from_type_with_guid(i32 typeId, Guid guid, bool createSubobjects)
{
	DynamicType* pType = DynamicData_get_type_from_id(typeId);

	if (pType == nullptr)
	{
		DYNAMIC_DATA_ERROR("Could not find type");
		// todo api return "nil" object that is valid to deference?
		return nullptr;
	}

	dd_obj* obj = DynamicData_obj_new_with_guid(guid);

	obj->typeId = typeId;

	u64 membersTotalSize =
		sizeof (DynamicValue) * pType->numProperties +
		sizeof (MemberStatus) * pType->numProperties;

	uintptr_t pMembers = (uintptr_t)DD_ALLOCATOR->alloc(membersTotalSize);

	obj->members.values = (DynamicValue*)pMembers;
	pMembers += sizeof (DynamicValue) * pType->numProperties;
	obj->members.statuses = (MemberStatus*)pMembers;

	for (i32 i = 0; i < pType->numProperties; ++i)
	{
		DynamicDataPropertyDef* pDef = &pType->properties[i];
		obj->members.statuses[i] = MemberStatus::Owned;
		DynamicValue* pMember = &obj->members.values[i];

		switch (pDef->type)
		{
		case DynamicValue::Type_Null:
			DYNAMIC_DATA_ERROR("Null type definition");
			*pMember = DynamicData_make_null();
			break;
		case DynamicValue::Type_Object:
		{
			if (createSubobjects)
			{
				pMember->obj_id = DynamicData_create_from_type(pDef->typeId);
				pMember->obj_type = pDef->typeId;
				pMember->type = DynamicValue::Type_Object;
			}
			else
			{
				*pMember = DynamicData_make_null();
				pMember->obj_type = pDef->typeId;
				pMember->type = DynamicValue::Type_Object;
			}
			break;
		}
		case DynamicValue::Type_Set:
		{
			*pMember = DynamicData_set_new();
			pMember->asSet()->typeId = pDef->typeId;
			break;
		}
		case DynamicValue::Type_Integer:
		{
			*pMember = DynamicData_int_new();
			break;
		}
		case DynamicValue::Type_Number:
		{
			*pMember = DynamicData_num_new();
			break;
		}
		case DynamicValue::Type_String:
		{
			*pMember = DynamicData_str_new();
			break;
		}
		}
	}

	return obj;
}

dd_id_t DynamicData_create_from_type(i32 typeId)
{
	return DynamicData_create_from_type_with_guid(typeId, Random_guid(), true)->id;
}

void DynamicData_clone_internal(const dd_obj* srcObject, dd_obj* dstObject)
{
	DynamicType* pType = s_types[srcObject->typeId];
	u64 size = pType->numProperties;

	dstObject->members.values = (DynamicValue*)DD_ALLOCATOR->alloc(sizeof (DynamicValue) * size);
	dstObject->members.statuses = (MemberStatus*)DD_ALLOCATOR->alloc(sizeof (MemberStatus) * size);
	dstObject->typeId = srcObject->typeId;

	for (u64 i = 0; i < size; ++i)
	{
		DynamicValue* pMember = &srcObject->members.values[i];
		DynamicValue* pClone = &dstObject->members.values[i];

		dstObject->members.statuses[i] = MemberStatus::Owned;

		switch (pMember->type)
		{
		case DynamicValue::Type_Null:
			break;
		case DynamicValue::Type_Object:
		{
			dd_obj* obj_clone = DynamicData_obj_new();
			DynamicData_clone_internal(DynamicData_read_object(pMember->obj_id), obj_clone);
			pClone->obj_id = obj_clone->id;
			pClone->obj_type = obj_clone->typeId;
			break;
		}
		case DynamicValue::Type_Set:
		{
			TempAllocator ta;
			Array<dd_id_t> setMembers = DynamicData_get_subobject_set(srcObject, pType->properties[i].nameHash, &ta);
			*pClone = DynamicData_set_new();
			DynamicSet* pSet = pClone->asSet();
			pSet->typeId = srcObject->members.values[i].asSet()->typeId;
			for (i32 ii = 0; ii < setMembers.size(); ++ii)
			{
				dd_obj* obj_clone = DynamicData_obj_new();
				DynamicData_clone_internal(DynamicData_read_object(setMembers[ii]), obj_clone);
				pSet->added.values.add(obj_clone->id.as_u64, obj_clone->id);
			}
			break;
		}
		case DynamicValue::Type_Integer:
		{
			*pClone = DynamicData_int_new();
			pClone->integer = pMember->integer;
			break;
		}
		case DynamicValue::Type_Number:
		{

			*pClone = DynamicData_num_new();
			pClone->number = pMember->number;
			break;
		}
		case DynamicValue::Type_String:
		{
			*pClone = DynamicData_str_new();
			u64 capacity = pMember->string ? (strlen(pMember->string) + 1) : 0;
			if (capacity)
			{
				char* str = (char*)malloc(capacity);
				pClone->string = str;
				memcpy(str, pMember->string, capacity);
			}
			break;
		}
		}
	}
}

dd_id_t DynamicData_clone(dd_id_t obj)
{
	dd_obj* clone_obj = DynamicData_obj_new();
	DynamicData_clone_internal(DynamicData_read_object(obj), clone_obj);
	return clone_obj->id;
}

MemberStatus DynamicData_get_member_status(const dd_obj* obj, u64 hMember)
{
	DynamicType* pType = s_types[obj->typeId];

	i32 i = 0;
	if (findProperty(pType, hMember, &i))
	{
		return obj->members.statuses[i];
	}

	return MemberStatus::None;
}

MemberStatus DynamicData_get_member_relation(dd_id_t parent, u64 hMember, dd_id_t obj)
{
	const dd_obj* parent_obj = DynamicData_read_object(parent);
	DynamicType* pType = s_types[parent_obj->typeId];

	i32 i;
	if (findProperty(pType, hMember, &i))
	{
		DynamicValue member = parent_obj->members.values[i];
		if (member.type == DynamicValue::Type_Set)
		{
			DynamicSet* pSet = member.asSet();
			if (pSet->added.values.contains(obj.as_u64))
			{
				return MemberStatus::Added;
			}
			if (pSet->removed.values.contains(obj.as_u64))
			{
				return MemberStatus::Removed;
			}
			if (pSet->instantiated.contains_instance(obj))
			{
				return MemberStatus::Instantiated;
			}

			// todo make this not awfully slow
			TempAllocator ta;
			const dd_obj* parent_prototype = DynamicData_read_object(parent_obj->prototype);
			Array<dd_id_t> inherited = DynamicData_get_subobject_set(parent_prototype, hMember, &ta);
			if (findId(inherited, obj, &i))
			{
				return MemberStatus::Inherited;
			}
		}
		else if (member.type == DynamicValue::Type_Object)
		{
			return parent_obj->members.statuses[i];
		}
		else
		{
			DYNAMIC_DATA_ERROR("Member relation on non object");
		}
	}
	else
	{
		DYNAMIC_DATA_ERROR("hMember not in Object");
	}

	return MemberStatus::None;
}

void DynamicData_obj_assign(dd_obj* object, u64 hMember, DynamicValue value)
{
	if (value.type == DynamicValue::Type_Object || value.type == DynamicValue::Type_Set)
	{
		DYNAMIC_DATA_ERROR("assigning a set or object");
		return;
	}

	DynamicType* pType = s_types[object->typeId];

	i32 i;
	if (findProperty(pType, hMember, &i) && object->members.values[i].type == pType->properties[i].type)
	{
		MemberStatus status = object->members.statuses[i];
		if (status == MemberStatus::Inherited)
		{
			object->members.values[i] = value;
			object->members.statuses[i] = MemberStatus::Overridden;
		}
		else if (status == MemberStatus::Owned || status == MemberStatus::Overridden)
		{
			object->members.values[i] = value;
		}
	}
	else
	{
		DYNAMIC_DATA_ERROR("assigning value type mismatch");
	}
}

void DynamicData_obj_clear_override(dd_obj* object, u64 hMember)
{
	DynamicType* pType = s_types[object->typeId];

	i32 i;
	if (findProperty(pType, hMember, &i))
	{
		MemberStatus status = object->members.statuses[i];
		if (status == MemberStatus::Overridden)
		{
			object->members.statuses[i] = MemberStatus::Inherited;
		}
	}
	else
	{
		DYNAMIC_DATA_ERROR(Printf("Object does not contain memeber %s", string_repository_get(hMember)).cstr());
	}
}

u64 string_repository_hash(const char* str)
{
	u64 key = murmur_hash_string(str);
	const char** find = s_string_repository.find(key);

	if (find == nullptr)
	{
		i32 size = (i32)strlen(str);
		char* copy_str = (char*) STRING_REPOSITORY_ALLOCATOR->alloc(size + 1);
		memcpy(copy_str, str, size+1);

		s_string_repository.add(key, str);
	}

	return key;
}

const char* string_repository_own(const char* str)
{
	u64 key = murmur_hash_string(str);
	const char** find = s_string_repository.find(key);

	if (find == nullptr)
	{
		i32 size = (i32)strlen(str);
		char* copy_str = (char*)STRING_REPOSITORY_ALLOCATOR->alloc(size + 1);
		memcpy(copy_str, str, size + 1);
		s_string_repository.add(key, str);
		return copy_str;
	}

	return *find;
}

const char* string_repository_get(u64 hName)
{
	auto find = s_string_repository.find(hName);

	if (find != nullptr)
	{
		return *find;
	}

	return "Invalid String";
}

i32 DynamicData_get_type_id_from_name(u64 hTypeNameHash)
{
	auto find = s_typeNameToTypeId.find(hTypeNameHash);

	if (find)
	{
		return *find;
	}

	DYNAMIC_DATA_ERROR("Could not find type");

	return i32(-1);
}

DynamicType* DynamicData_get_type_from_name(u64 hName)
{
	auto find = s_typeNameToTypeId.find(hName);

	if (find)
	{
		return s_types[*find];
	}

	DYNAMIC_DATA_ERROR("Could not find type");

	return nullptr;
}

i32 DynamicData_register_type(const char* typeName, const char* uiName, const DynamicDataPropertyDef* properties, i32 numProperties)
{
	DynamicType* type = new (DD_ALLOCATOR) DynamicType();

	u64 typeNameHash = string_repository_hash(typeName);

	i32 typeId = s_types.size();
	s_types.push_back(type);
	s_typeNameToTypeId.add(typeNameHash, typeId);

	type->nameHashToProperty = (u64*)DD_ALLOCATOR->alloc(sizeof (u64) * numProperties);
	type->properties = (DynamicDataPropertyDef*)DD_ALLOCATOR->alloc(sizeof (DynamicDataPropertyDef) * numProperties);
	type->typeNameHash = typeNameHash;
	type->typeName = string_repository_own(typeName);
	type->typeId = typeId;
	type->numProperties = numProperties;
	type->uiName = uiName;

	memcpy(type->properties, properties, numProperties * sizeof (DynamicDataPropertyDef));

	for (i32 i = 0; i < numProperties; ++i)
	{
		DynamicDataPropertyDef* pDef = &type->properties[i];
		pDef->name = string_repository_own(pDef->name);
		pDef->nameHash = string_repository_hash(pDef->name);
		type->nameHashToProperty[i] = string_repository_hash(properties[i].name);
		pDef->typeId = pDef->typeNameHash != 0u ? DynamicData_get_type_id_from_name(pDef->typeNameHash) : 0;
	}

	return typeId;
}

dd_id_t DynamicData_create_from_type_name(u64 hTypeNameHash)
{
	i32 id = DynamicData_get_type_id_from_name(hTypeNameHash);
	return DynamicData_create_from_type(id);
}

struct DebugValuePair
{
	char name[64];
	MemberStatus status;
	DynamicValue value;
};

struct DynamicDataObjectDebugView
{
	const char* typeName;
	const dd_obj* prototype;
	Array<DebugValuePair> values;
};

DynamicDataObjectDebugView DynamicData_DebugExpressionObject(const dd_obj* object)
{
	DynamicDataObjectDebugView debugData;

	debugData.values.set_allocator(GLOBAL_HEAP);
	debugData.typeName = DynamicData_get_type_from_id(object->typeId)->typeName;
	debugData.prototype = DynamicData_read_object(object->prototype);

	DynamicType* pType = s_types[object->typeId];

	for (i32 i = 0; i < pType->numProperties; ++i)
	{
		DebugValuePair& pair = debugData.values.push_back();
		strcpy_s(pair.name, 64, pType->properties[i].name);
		pair.value = object->members.values[i];
		pair.status = object->members.statuses[i];
	}

	return debugData;
}

DynamicDataObjectDebugView DynamicData_DebugExpression(dd_id_t object_id)
{
	if (const dd_obj* object = DynamicData_read_object(object_id))
	{
		return DynamicData_DebugExpressionObject(object);
	}

	return DynamicDataObjectDebugView{};
}


void PushStatusStyle(MemberStatus status)
{
	constexpr u32 COLOR_OWNED = IM_COL32(255, 255, 255, 255);
	constexpr u32 COLOR_INHERIT = IM_COL32(100, 100, 100, 255);
	constexpr u32 COLOR_INSTANTIATED = IM_COL32(255, 255, 180, 255);
	constexpr u32 COLOR_OVERRIDDEN = IM_COL32(180, 180, 255, 255);
	constexpr u32 COLOR_REMOVED = IM_COL32(255, 180, 180, 255);
	constexpr u32 COLOR_ADDED = IM_COL32(255, 255, 255, 255);
	constexpr u32 COLOR_SET = IM_COL32(255, 255, 255, 255);
	constexpr u32 COLOR_ERROR = IM_COL32(255, 0, 0, 255);

	u32 styles[]
	{
		COLOR_OWNED,
		COLOR_INHERIT,
		COLOR_INSTANTIATED,
		COLOR_OVERRIDDEN,
		COLOR_REMOVED,
		COLOR_ADDED,
		COLOR_SET,
		COLOR_ERROR,
	};

	ImGui::PushStyleColor(ImGuiCol_Text, styles[(int)status]);
}

void PopStatusStyle()
{
	ImGui::PopStyleColor();
}

void DynamicData_format_value(Printf& buf, DynamicValue value)
{
	switch (value.type) {
	case DynamicValue::Type_Null:
		buf.write("%s", "Null");
		break;
	case DynamicValue::Type_Object:
		buf.write("%s", "Object");
		break;
	case DynamicValue::Type_Set:
		buf.write("%s", "Set");
		break;
	case DynamicValue::Type_Integer:
		buf.write("%lld", value.integer);
		break;
	case DynamicValue::Type_Number:
		buf.write("%f", value.number);
		break;
	case DynamicValue::Type_String:
		buf.write("%s", value.string);
		break;
	}
}

void DynamicData_view_draw_object_id(const dd_obj* pObject)
{
	if (pObject->prototype == INVALID_OBJECT_ID)
	{
		if (ImGui::TreeNodeEx(Printf("Object ID : %llu", pObject->id.as_u64), ImGuiTreeNodeFlags_Leaf))
		{
			ImGui::TreePop();
		}
	}
	else
	{
		if (ImGui::TreeNodeEx(Printf("Object ID : %llu [Prototype ID : %llu]", pObject->id, pObject->prototype.as_u64), ImGuiTreeNodeFlags_Leaf))
		{
			ImGui::TreePop();
		}
	}
}

void DynamicData_rename_modal(const dd_obj* object, u64 hMember)
{
	ImGui::PushID((i32)(object->id.as_u64 ^ hMember));

	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::BeginPopupModal("Rename Item", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		static char input_buffer[128] = "";
		if (ImGui::IsWindowAppearing())
		{
			const char* name = DynamicData_get_string(object, hMember);
			strncpy_s(input_buffer, name, sizeof(input_buffer) - 1);
			input_buffer[sizeof(input_buffer) - 1] = '\0';
		}

		ImGui::Text("Enter new name:");
		if (ImGui::InputText("##rename_input", input_buffer, sizeof(input_buffer), ImGuiInputTextFlags_EnterReturnsTrue))
		{
			DynamicData_set_string(DynamicData_edit_object(object->id), hMember, input_buffer);
			ImGui::CloseCurrentPopup();
		}

		if (ImGui::Button("OK", ImVec2(120, 0)))
		{
			DynamicData_set_string(DynamicData_edit_object(object->id), hMember, input_buffer);
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0)))
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	ImGui::PopID();
}

void DynamicData_view_object_context_menu(const dd_obj* object, u64 hMember, bool parentInherited)
{
	if (parentInherited)
		return;

	bool opened_rename = false;

	if (ImGui::BeginPopupContextItem())
	{
		DynamicValue member = DynamicData_obj_get(object, hMember);
		MemberStatus status = DynamicData_get_member_status(object, hMember);

		if (!parentInherited && status == MemberStatus::Overridden)
		{
			if (ImGui::MenuItem("Clear override"))
			{
				dd_obj* object_w = DynamicData_edit_object(object->id);
				DynamicData_obj_clear_override(object_w, hMember);
			}
		}

		if (!parentInherited && status != MemberStatus::Inherited && member.type == DynamicValue::Type_Object && ImGui::MenuItem("Create instance of"))
		{
			dd_id_t instance = DynamicData_instantiate(object->id);
			dd_obj* object_w = DynamicData_edit_object(object->id);

			constexpr u64 hNameField = TM_STATIC_HASH("name", 0xd4c943cba60c270bULL);
			DynamicValue name = DynamicData_obj_get(object_w, hNameField);
			if (name.type == DynamicValue::Type_String)
			{
				DynamicData_obj_assign(object_w, hNameField, DynamicData_make_str(Printf("Instance of [%s]", name.asString())));
			}

			Debug_register_root_object(instance);
		}

		if (!parentInherited && status == MemberStatus::Instantiated && (member.type == DynamicValue::Type_Set))
		{
			if (ImGui::MenuItem("Reset to prototype"))
			{
				DYNAMIC_DATA_ERROR("fix this");
				dd_obj* object_w = DynamicData_edit_object(object->id);
				DynamicValue val = DynamicData_obj_get(object_w, hMember);
				DynamicData_obj_assign(object_w, hMember, val);
			}
		}

		if (!parentInherited && (member.type == DynamicValue::Type_Set) && (status == MemberStatus::Owned || status == MemberStatus::Set))
		{
			i32 typeId = member.asSet()->typeId;
			if (typeId != 0)
			{
				const char* type_name = DynamicData_get_type_from_id(typeId)->uiName;

				if (ImGui::MenuItem(Printf("Add new %s", type_name)))
				{
					static int addcount = 0;
					dd_id_t added = DynamicData_create_from_type(typeId);
					dd_obj* object_w = DynamicData_edit_object(object->id);
					++addcount;
					DynamicData_add_to_subobject_set(object_w, hMember, added);
				}
			}

			if (hMember == TM_STATIC_HASH("components", 0xe71d1687374e5a54ULL))
			{
				Array<i32>* comp_types = Debug_get_component_ids();

				for (i32 id : *comp_types)
				{
					DynamicType* pType = s_types[id];

					if (ImGui::MenuItem(Printf("Add %s", pType->uiName)))
					{
						static int addcount = 0;
						dd_id_t added = DynamicData_create_from_type(id);
						dd_obj* object_w = DynamicData_edit_object(object->id);
						++addcount;
						DynamicData_add_to_subobject_set(object_w, hMember, added);
					}
				}
			}
		}

		if (!parentInherited && (member.type != DynamicValue::Type_Object && member.type != DynamicValue::Type_Set) && status == MemberStatus::Inherited)
		{
			if (ImGui::MenuItem("Override value"))
			{
				dd_obj* object_w = DynamicData_edit_object(object->id);
				DynamicValue val = DynamicData_obj_get(object_w, hMember);
				DynamicData_obj_assign(object_w, hMember, val);
			}
		}

		if (!parentInherited && (member.type == DynamicValue::Type_String) && (status == MemberStatus::Owned || status == MemberStatus::Overridden))
		{
			if (ImGui::MenuItem("Edit string"))
			{
				opened_rename = true;
			}
		}

		if (!parentInherited && (member.type == DynamicValue::Type_Object) && status == MemberStatus::Inherited)
		{
			if (ImGui::MenuItem("Instantiate subobject"))
			{
				dd_obj* object_w = DynamicData_edit_object(object->id);
				DynamicData_instantiate_subobject(object_w, hMember);
			}
		}

		if (!parentInherited && (member.type == DynamicValue::Type_Object || member.type == DynamicValue::Type_Set) && status == MemberStatus::Instantiated)
		{
			if (ImGui::MenuItem("Reset to prototype"))
			{
				dd_obj* object_w = DynamicData_edit_object(object->id);
				DynamicData_clear_instantiated_subobject(object_w, hMember);
			}
		}

		if (!parentInherited && member.type == DynamicValue::Type_Object)
		{
			if (ImGui::MenuItem("Serialize JSON"))
			{
				const dd_obj* object_r = DynamicData_read_object(object->id);
				constexpr u64 hNameField = TM_STATIC_HASH("name", 0xd4c943cba60c270bULL);
				DynamicValue name = DynamicData_obj_get(object_r, hNameField);
				DynamicData_serialize_json_file(name.asString(), object_r);
			}
		}

		ImGui::EndPopup();
	}

	if (opened_rename)
	{
		ImGui::PushID((i32)(object->id.as_u64^ hMember));
		ImGui::OpenPopup("Rename Item");
		ImGui::PopID();
	}

	DynamicData_rename_modal(object, hMember);
}

void DynamicData_view_impl(const dd_obj* object, u64 hMember, bool isInherited);

void DynamicData_view_object_set_context_menu(const dd_obj* parent_object, u64 hMember, dd_id_t object, MemberStatus status, bool parentInherited)
{
	if (parentInherited)
		return;

	if (ImGui::BeginPopupContextItem())
	{
		if (status == MemberStatus::Added)
		{
			if (ImGui::MenuItem("Remove from set"))
			{
				dd_obj* parent_w = DynamicData_edit_object(parent_object->id);
				DynamicData_remove_from_subobject_set(parent_w, hMember, object);
			}
		}

		if (status == MemberStatus::Inherited)
		{
			if (ImGui::MenuItem("Instantiate set member"))
			{
				dd_obj* parent_w = DynamicData_edit_object(parent_object->id);
				DynamicData_instantiate_subobject_from_set(parent_w, hMember, object);
			}

			if (ImGui::MenuItem("Remove from prototype set"))
			{
				dd_obj* parent_w = DynamicData_edit_object(parent_object->id);
				DynamicData_remove_from_prototype_subobject_set(parent_w, hMember, object);
			}
		}

		if (status == MemberStatus::Instantiated)
		{
			if (ImGui::MenuItem("Revert to prototype"))
			{
				dd_obj* parent_w = DynamicData_edit_object(parent_object->id);
				DynamicData_remove_instantiated_subobject_from_set(parent_w, hMember, object);
			}
		}

		if (status == MemberStatus::Removed)
		{
			if (ImGui::MenuItem("Cancel remove"))
			{
				dd_obj* parent_w = DynamicData_edit_object(parent_object->id);
				DynamicData_cancel_remove_from_prototype_subobject_set(parent_w, hMember, object);
			}
		}

		ImGui::EndPopup();
	}
}

bool is_container(const DynamicValue& val)
{
	return val.type == DynamicValue::Type_Object || val.type == DynamicValue::Type_Set;
}

void DynamicData_view_draw_object(const dd_obj* object, bool parentInherited)
{
	DynamicType* pType = s_types[object->typeId];

	for (i32 i = 0; i < pType->numProperties; ++i)
	{
		u64 hMemberName = pType->properties[i].nameHash;
		bool isContainer = is_container(object->members.values[i]);
		MemberStatus memberStatus = object->members.statuses[i];
		memberStatus = parentInherited ? MemberStatus::Inherited : memberStatus;

		const char* memberName = pType->properties[i].name;

		if (!isContainer)
		{
			Printf buf;
			DynamicValue element = DynamicData_obj_get(object, hMemberName);
			DynamicData_format_value(buf, element);

			PushStatusStyle(memberStatus);
			bool open = ImGui::TreeNodeEx(Printf("%s : %s", memberName, buf.cstr()), ImGuiTreeNodeFlags_Leaf);
			PopStatusStyle();

			if (open)
			{
				ImGui::TreePop();
			}

			DynamicData_view_object_context_menu(object, hMemberName, parentInherited);

			if (element.type == DynamicValue::Type_Number && ImGui::IsItemClicked() && !parentInherited)
			{
				dd_obj* object_w = DynamicData_edit_object(object->id);
				DynamicValue newPos = DynamicData_make_num(element.asNumber() + 0.5);
				DynamicData_obj_assign(object_w, hMemberName, newPos);
			}

			if (element.type == DynamicValue::Type_Integer && ImGui::IsItemClicked() && !parentInherited)
			{
				dd_obj* object_w = DynamicData_edit_object(object->id);
				DynamicValue newPos = DynamicData_make_int(element.asInt() + 1);
				DynamicData_obj_assign(object_w, hMemberName, newPos);
			}
		}
		else
		{
			PushStatusStyle(memberStatus);
			bool childOpen = ImGui::TreeNode(memberName);
			PopStatusStyle();

			DynamicData_view_object_context_menu(object, hMemberName, parentInherited);

			if (childOpen)
			{
				DynamicData_view_impl(object, hMemberName, memberStatus == MemberStatus::Inherited);
				ImGui::TreePop();
			}
		}
	}
}

void DynamicData_view_draw_object_set(const dd_obj* object, u64 hSetName, bool parentInherited)
{
	HeapAllocator alloc;
	Array<dd_id_t> members = DynamicData_get_subobject_set(object, hSetName, &alloc);
	Array<dd_id_t> removed = DynamicData_get_subobject_set_locally_removed(object, hSetName, &alloc);

	for (auto& r : removed)
	{
		members.push_back(r);
	}

	for (i32 i = 0; i < members.size(); ++i)
	{
		const dd_obj* member_object = DynamicData_read_object(members[i]);
		DynamicType* pType = s_types[member_object->typeId];

		Printf item_name = Printf("%s [%d]", pType->uiName, i);

		ImGui::PushID(item_name);

		MemberStatus status = DynamicData_get_member_relation(object->id, hSetName, member_object->id);
		status = parentInherited ? MemberStatus::Inherited : status;

		ImVec2 cursorPos = ImGui::GetCursorScreenPos();

		PushStatusStyle(status);
		ImGuiTreeNodeFlags flags = status == MemberStatus::Removed ? ImGuiTreeNodeFlags_Leaf : 0;

		bool open = ImGui::TreeNodeEx(item_name, flags);
		PopStatusStyle();

		DynamicData_view_object_set_context_menu(object, hSetName, member_object->id, status, parentInherited);

		if (open)
		{
			if (status != MemberStatus::Removed)
			{
				DynamicData_view_draw_object(member_object, status == MemberStatus::Inherited);
			}
			ImGui::TreePop();
		}

		if (status == MemberStatus::Removed)
		{
			ImVec2 textSize = ImGui::CalcTextSize(item_name.cstr());
			ImVec2 start = ImVec2(cursorPos.x + 24, cursorPos.y);
			ImVec2 end = ImVec2(cursorPos.x + textSize.x + 2, cursorPos.y);
			float textHeight = textSize.y;
			start.y += textHeight * 0.5f;
			end.y += textHeight * 0.5f;
			ImGui::GetWindowDrawList()->AddLine(start, end, IM_COL32(255, 180, 180, 255), 1.0f);
		}

		ImGui::PopID();
	}
}

void DynamicData_view_draw_root_object(const dd_obj* object)
{
	DynamicValue name = DynamicData_obj_get(object, TM_STATIC_HASH("name", 0xd4c943cba60c270bULL));

	const char* displayName = name.type == DynamicValue::Type_String ? name.asString() : "Root";

	PushStatusStyle(object->prototype == INVALID_OBJECT_ID ? MemberStatus::Owned : MemberStatus::Instantiated);
	bool open = ImGui::TreeNode(displayName);
	PopStatusStyle();
	if (ImGui::BeginPopupContextItem("root_ctx_menu"))
	{
		if (ImGui::MenuItem("Create instance of"))
		{
			dd_id_t instance = DynamicData_instantiate(object->id);
			dd_obj* instance_w = DynamicData_edit_object(instance);

			constexpr u64 hNameField = TM_STATIC_HASH("name", 0xd4c943cba60c270bULL);
			if (name.type == DynamicValue::Type_String)
			{
				DynamicData_obj_assign(instance_w, hNameField, DynamicData_make_str(Printf("Instance of [%s]", name.asString())));
			}
			Debug_register_root_object(instance);
		}

		if (ImGui::MenuItem("Serialize JSON"))
		{
			DynamicData_serialize_json_file(displayName, object);
		}

		ImGui::EndPopup();
	}

	if (open)
	{
		DynamicData_view_draw_object(object, false);
		ImGui::TreePop();
	}
}


void DynamicData_view_impl(const dd_obj* object, u64 hMember, bool isInherited)
{
	DynamicValue value = DynamicData_obj_get(object, hMember);
	MemberStatus status = DynamicData_get_member_status(object, hMember);

	status = isInherited ? MemberStatus::Inherited : status;

	switch (value.type)
	{
	case DynamicValue::Type_Object:
	{
		DynamicData_view_draw_object(DynamicData_read_object(value.obj_id), isInherited);
		break;
	}
	case DynamicValue::Type_Set:
	{
		DynamicData_view_draw_object_set(object, hMember, isInherited);
		break;
	}
	case DynamicValue::Type_Integer:
	case DynamicValue::Type_Number:
	case DynamicValue::Type_String:
	case DynamicValue::Type_Null:
	{
		Printf buf;
		PushStatusStyle(status);
		DynamicData_format_value(buf, value);
		bool r = ImGui::TreeNodeEx(buf.cstr(), ImGuiTreeNodeFlags_Leaf);
		PopStatusStyle();
		if (r)
		{
			ImGui::TreePop();
		}
		break;
	}
	}
}

void DynamicData_view(dd_id_t object)
{
	const dd_obj* object_r = DynamicData_read_object(object);
	DynamicData_view_draw_root_object(object_r);
}

void DynamicData_view_draw_type_registry()
{
	if (ImGui::Begin("Dynamic Data Type Registry"))
	{


	for (DynamicType* pType : s_types)
	{
		if (ImGui::TreeNode(pType->uiName))
		{
			if (ImGui::TreeNodeEx(Printf("Runtime Type ID %d", pType->typeId), ImGuiTreeNodeFlags_Leaf))
			{
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("members"))
			{
				for (i32 i = 0; i < pType->numProperties; ++i)
				{
					const DynamicDataPropertyDef& def = pType->properties[i];
					char buf[64];
					switch (def.type)
					{
					case DynamicValue::Type_Null:
						sprintf_s(buf, "%s : %s", def.name, "null");
						break;
					case DynamicValue::Type_Object:
						sprintf_s(buf, "%s : %s", def.name, s_types[def.typeId]->uiName);
						break;
					case DynamicValue::Type_Set:
						sprintf_s(buf, "%s : Set [%s]", def.name, s_types[def.typeId]->uiName);
						break;
					case DynamicValue::Type_Integer:
						sprintf_s(buf, "%s : %s", def.name, "integer");
						break;
					case DynamicValue::Type_Number:
						sprintf_s(buf, "%s : %s", def.name, "number");
						break;
					case DynamicValue::Type_String:
						sprintf_s(buf, "%s : %s", def.name, "string");
						break;
					}

					if (ImGui::TreeNodeEx(buf, ImGuiTreeNodeFlags_Leaf))
					{
						ImGui::TreePop();
					}
				}

				ImGui::TreePop();
			}
			ImGui::TreePop();
		}
	}

	ImGui::End();
	}

}


yyjson_mut_val* yyjson_mut_guid(yyjson_mut_doc* jDoc, Guid guid)
{
	char buffer[36];
	write_guid_str(buffer, guid);
	return yyjson_mut_strncpy(jDoc, buffer, 36);
}

Guid yyjson_get_guid(yyjson_val* jVal) {
	Guid guid{};
	const char* str = yyjson_get_str(jVal);
	u64 len = yyjson_get_len(jVal);

	if (len < 36)
	{
		return guid;
	}

	if (!str || str[36] != '\0' || str[8] != '-' || str[13] != '-' || str[18] != '-' || str[23] != '-')
	{
		return guid;
	}

	static const u8 hex_to_val[256] =
	{
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0xFF,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0xFF,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
		0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
	};

	u8 bytes[16];
	int byte_idx = 0;

	for (int i = 0; i < 36 && byte_idx < 16; i++)
	{
		if (str[i] == '-') continue;

		u8 high = hex_to_val[(u8)str[i]];
		u8 low = hex_to_val[(u8)str[i + 1]];

		if (high == 0xFF || low == 0xFF) return guid;

		bytes[byte_idx++] = (unsigned char)((high << 4) | low);
		i++;
	}

	guid.a = 0;
	guid.b = 0;
	for (int i = 0; i < 8; i++)
	{
		guid.a |= (u64)bytes[i] << ((7 - i) * 8);
	}
	for (int i = 0; i < 8; i++)
	{
		guid.b |= (u64)bytes[i + 8] << ((7 - i) * 8);
	}

	return guid;
}

struct StringPiece
{
	const char* data;
	u64 len;
};

bool MatchStringSuffix(const char* input, const char* suffix, StringPiece* outResult)
{
	u64 suffix_len = strlen(suffix);
	u64 input_len = strlen(input);

	outResult->data = input;
	outResult->len = input_len;

	if (input_len >= suffix_len && strcmp(input + input_len - suffix_len, suffix) == 0)
	{
		outResult->len = input_len - suffix_len;
		return true;
	}

	return false;
}

void DynamicData_deserialize_json_value(yyjson_val* jValue, dd_obj* object, i32 memberIndex, Array<DynamicData_UnresolvedObject>* inoutUnresolved, bool isInstance);
void DynamicData_deserialize_json_subobject(yyjson_val* jObject, dd_obj* object, Array<DynamicData_UnresolvedObject>* inoutUnresolved, bool isInstance);

void DynamicData_deserialize_json_set(yyjson_val* jArray, DynamicSet* pSet, Array<DynamicData_UnresolvedObject>* inoutUnresolved);
void DynamicData_deserialize_json_set_removed(yyjson_val* jArray, DynamicSet* pSet, Array<DynamicData_UnresolvedObject>* inoutUnresolved);
void DynamicData_deserialize_json_set_instantiated(yyjson_val* jArray, DynamicSet* pSet, Array<DynamicData_UnresolvedObject>* inoutUnresolved);

void DynamicData_serialize_json_value(yyjson_mut_doc* jDoc, yyjson_mut_val* into, const dd_obj* object, u64 hMember);
void DynamicData_serialize_json_subobject(yyjson_mut_doc* jDoc, yyjson_mut_val* jObject, const dd_obj* object);
void DynamicData_serialize_json_set(yyjson_mut_doc* jDoc, yyjson_mut_val* into, const dd_obj* object, i32 setIndex);

void DynamicData_deserialize_json_value(yyjson_val* jValue, dd_obj* object, i32 memberIndex, Array<DynamicData_UnresolvedObject>* inoutUnresolved, bool isInstance)
{
	DynamicValue* pValue = &object->members.values[memberIndex];
	MemberStatus* pStatus = &object->members.statuses[memberIndex];

	switch (pValue->type)
	{
	case DynamicValue::Type_Null:
	{
		DYNAMIC_DATA_ERROR("Null type def");
		break;
	}
	case DynamicValue::Type_Object:
	{
		if (yyjson_is_obj(jValue))
		{
			DynamicType* pParentType = DynamicData_get_type_from_id(object->typeId);
			i32 subobjectTypeId = pParentType->properties[memberIndex].typeId;

			const char* typeName = yyjson_get_str(yyjson_obj_get(jValue, "#type"));
			u64 typeNameHash = murmur_hash_string(typeName);
			i32 typeId = DynamicData_get_type_id_from_name(typeNameHash);

			if (typeId == subobjectTypeId)
			{
				Guid guid = yyjson_get_guid(yyjson_obj_get(jValue, "#guid"));
				yyjson_val* jPrototypeGuid = yyjson_obj_get(jValue, "#prototype_guid");

				dd_obj* subobject_w = DynamicData_create_from_type_with_guid(typeId, guid, false);
				pValue->type = DynamicValue::Type_Object;
				pValue->obj_type = subobjectTypeId;
				pValue->obj_id = subobject_w->id;

				if (jPrototypeGuid)
				{
					Guid prototypeGuid = yyjson_get_guid(jPrototypeGuid);
					DynamicData_UnresolvedObject& r = inoutUnresolved->push_back();
					r.object.object_id = pValue->id();
					r.object.prototype = prototypeGuid;
					r.isSet = false;
				}
				*pStatus = isInstance ? MemberStatus::Instantiated : MemberStatus::Owned;

				DynamicData_deserialize_json_subobject(jValue, subobject_w, inoutUnresolved, jPrototypeGuid != nullptr);
			}
			else
			{
				DYNAMIC_DATA_ERROR("Deserialize subobject type mismatch from disk to TypeRegistry");
			}
		}
		else
		{
			DYNAMIC_DATA_ERROR("Deserialize object on disk is not subobject in type.");
		}
		break;
	}
	case DynamicValue::Type_Set:
	{
		break;
	}
	case DynamicValue::Type_Integer:
	{
		if (yyjson_is_int(jValue))
		{
			pValue->integer = unsafe_yyjson_get_int(jValue);
			*pStatus = isInstance ? MemberStatus::Overridden : MemberStatus::Owned;
		}
		else
		{
			*pStatus = isInstance ? MemberStatus::Inherited : MemberStatus::Owned;
		}
		break;
	}
	case DynamicValue::Type_Number:
	{
		if (yyjson_is_real(jValue))
		{
			pValue->number = unsafe_yyjson_get_real(jValue);
			*pStatus = isInstance ? MemberStatus::Overridden : MemberStatus::Owned;
		}
		else
		{
			*pStatus = isInstance ? MemberStatus::Inherited : MemberStatus::Owned;
		}
		break;
	}
	case DynamicValue::Type_String:
	{
		if (yyjson_is_str(jValue))
		{
			const char* str = yyjson_get_str(jValue);
			*pValue = DynamicData_make_str(str);
			*pStatus = isInstance ? MemberStatus::Overridden : MemberStatus::Owned;
		}
		else
		{
			*pStatus = isInstance ? MemberStatus::Inherited : MemberStatus::Owned;
		}
		break;
	}
	}
}

void DynamicData_deserialize_json_subobject(yyjson_val* jObject, dd_obj* object, Array<DynamicData_UnresolvedObject>* inoutUnresolved, bool isInstance)
{
	constexpr const char* kInstantiated = "#instantiated";
	constexpr const char* kRemoved = "#removed";

	DynamicType* pType = s_types[object->typeId];

	for (i32 i = 0; i < pType->numProperties; ++i)
	{
		if (object->members.values[i].type == DynamicValue::Type_Set)
		{
			object->members.statuses[i] = isInstance ? MemberStatus::Set : MemberStatus::Owned;
		}
		else
		{
			object->members.statuses[i] = isInstance ? MemberStatus::Inherited : MemberStatus::Owned;
		}
	}

	yyjson_obj_iter jIter;
	yyjson_obj_iter_init(jObject, &jIter);
	yyjson_val* jKey;
	while ((jKey = yyjson_obj_iter_next(&jIter)))
	{
		if (yyjson_get_str(jKey)[0] == '#')
		{
			continue;
		}

		yyjson_val* jValue = yyjson_obj_iter_get_val(jKey);

		// arrays are special separated into 3 fiels, "added" named normamml, removed and instantiated suffixed by _removed, _instantiated
		if (yyjson_is_arr(jValue))
		{
			StringPiece sp;
			if (MatchStringSuffix(yyjson_get_str(jKey), kInstantiated, &sp))
			{
				u64 hName = murmur_hash(sp.data, (u32)sp.len, 0);
				i32 i;
				if (findProperty(pType, hName, &i))
				{
					DynamicData_deserialize_json_set_instantiated(jValue, object->members.values[i].asSet(), inoutUnresolved);
				}
			}
			if (MatchStringSuffix(yyjson_get_str(jKey), kRemoved, &sp))
			{
				u64 hName = murmur_hash(sp.data, (u32)sp.len, 0);
				i32 i;
				if (findProperty(pType, hName, &i))
				{
					DynamicData_deserialize_json_set_removed(jValue, object->members.values[i].asSet(), inoutUnresolved);
				}
			}
			else
			{
				u64 hName = string_repository_hash(yyjson_get_str(jKey));
				i32 i;
				if (findProperty(pType, hName, &i))
				{
					DynamicData_deserialize_json_set(jValue, object->members.values[i].asSet(), inoutUnresolved);
				}
			}
		}

		u64 hName = string_repository_hash(yyjson_get_str(jKey));
		i32 i;
		if (findProperty(pType, hName, &i))
		{
			DynamicData_deserialize_json_value(jValue, object, i, inoutUnresolved, isInstance);
		}
	}
}

void DynamicData_deserialize_json_set(yyjson_val* jArray, DynamicSet* pSet, Array<DynamicData_UnresolvedObject>* inoutUnresolved)
{
	yyjson_arr_iter jIter;
	yyjson_arr_iter_init(jArray, &jIter);
	yyjson_val* jValue;

	while ((jValue = yyjson_arr_iter_next(&jIter)))
	{
		const char* typeName = yyjson_get_str(yyjson_obj_get(jValue, "#type"));
		Guid guid = yyjson_get_guid(yyjson_obj_get(jValue, "#guid"));

		u64 typeNameHash = murmur_hash_string(typeName);
		i32 typeId = DynamicData_get_type_id_from_name(typeNameHash);

		if (guid.a == 0 && guid.b == 0)
		{
			DYNAMIC_DATA_ERROR("Deserialize Error");
		}
		else
		{
			if (pSet->typeId != 0 && typeId != pSet->typeId)
			{
				DYNAMIC_DATA_ERROR("Type mismatch");
			}
			else
			{
				dd_obj* setValue = DynamicData_create_from_type_with_guid(typeId, guid, false);
				DynamicData_deserialize_json_subobject(jValue, setValue, inoutUnresolved, false);
				pSet->added.values.add(setValue->id.as_u64, setValue->id);
			}
		}
	}
}

void DynamicData_deserialize_json_set_removed(yyjson_val* jArray, DynamicSet* pSet, Array<DynamicData_UnresolvedObject>* inoutUnresolved)
{
	yyjson_arr_iter jIter;
	yyjson_arr_iter_init(jArray, &jIter);
	yyjson_val* jValue;

	while ((jValue = yyjson_arr_iter_next(&jIter)))
	{
		Guid guid = yyjson_get_guid(jValue);

		if (guid.a == 0 && guid.b == 0)
		{
			DYNAMIC_DATA_ERROR("Deserialize Error");
		}
		else
		{
			DynamicData_UnresolvedObject& r = inoutUnresolved->push_back();
			r.isSet = true;

			r.set.pSet = pSet;
			r.set.guid = guid;
			r.set.isRemove = true;
		}
	}
}

void DynamicData_deserialize_json_set_instantiated(yyjson_val* jArray, DynamicSet* pSet, Array<DynamicData_UnresolvedObject>* inoutUnresolved)
{
	yyjson_arr_iter jIter;
	yyjson_arr_iter_init(jArray, &jIter);
	yyjson_val* jValue;

	while ((jValue = yyjson_arr_iter_next(&jIter)))
	{
		const char* typeName = yyjson_get_str(yyjson_obj_get(jValue, "#type"));
		Guid guid = yyjson_get_guid(yyjson_obj_get(jValue, "#guid"));
		Guid prototypeGuid = yyjson_get_guid(yyjson_obj_get(jValue, "#prototype_guid"));

		u64 typeNameHash = murmur_hash_string(typeName);
		i32 typeId = DynamicData_get_type_id_from_name(typeNameHash);

		if ((guid.a == 0 && guid.b == 0) || (prototypeGuid.a == 0 && prototypeGuid.b == 0))
		{
			DYNAMIC_DATA_ERROR("Deserialize Error");
		}
		else
		{
			if (pSet->typeId != 0 && typeId != pSet->typeId)
			{
				DYNAMIC_DATA_ERROR("Type mismatch");
			}
			else
			{
				dd_obj* set_object = DynamicData_create_from_type_with_guid(typeId, guid, false);
				DynamicData_deserialize_json_subobject(jValue, set_object, inoutUnresolved, true);

				{
					DynamicData_UnresolvedObject& r = inoutUnresolved->push_back();
					r.object.object_id = set_object->id;
					r.object.prototype = prototypeGuid;
					r.isSet = false;
				}

				{
					DynamicData_UnresolvedObject& r = inoutUnresolved->push_back();
					r.isSet = true;
					r.set.guid = prototypeGuid;
					r.set.isRemove = false;
					r.set.pSet = pSet;
					r.set.instantiated = set_object->id;
				}
			}
		}
	}
}

void DynamicData_serialize_json_set(yyjson_mut_doc* jDoc, yyjson_mut_val* into, const dd_obj* object, i32 setIndex)
{
	DynamicType* pType = s_types[object->typeId];

	u64 hSetName = pType->properties[setIndex].nameHash;
	DynamicSet* pSet = object->members.values[setIndex].asSet();
	const char* setName = string_repository_get(hSetName);

	{
		yyjson_mut_val* jArr = yyjson_mut_obj_add_arr(jDoc, into, setName);
		for (auto& it : pSet->added.values)
		{
			yyjson_mut_val* jElement = yyjson_mut_arr_add_obj(jDoc, jArr);
			DynamicData_serialize_json_subobject(jDoc, jElement, DynamicData_read_object(it.value));
		}
	}

	if (!pSet->instantiated.instance_to_id.empty())
	{
		char buf[128];
		snprintf(buf, 128, "%s#instantiated", setName);

		yyjson_mut_val* jKey = yyjson_mut_strcpy(jDoc, buf);
		yyjson_mut_val* jArrInstantiated = yyjson_mut_arr(jDoc);

		for (auto& it : pSet->instantiated.id_to_instance)
		{
			yyjson_mut_val* jElement = yyjson_mut_arr_add_obj(jDoc, jArrInstantiated);
			DynamicData_serialize_json_subobject(jDoc, jElement, DynamicData_read_object(it.value));
		}

		yyjson_mut_obj_add(into, jKey, jArrInstantiated);
	}

	if (!pSet->removed.values.empty())
	{
		char buf[128];
		snprintf(buf, 128, "%s#removed", setName);

		yyjson_mut_val* jKey = yyjson_mut_strcpy(jDoc, buf);
		yyjson_mut_val* jArrRemoved = yyjson_mut_arr(jDoc);

		for (auto& it : pSet->removed.values)
		{
			yyjson_mut_arr_add_val(jArrRemoved, yyjson_mut_guid(jDoc, DynamicData_read_object(it.value)->guid));
		}

		yyjson_mut_obj_add(into, jKey, jArrRemoved);
	}
}

void DynamicData_serialize_json_subobject(yyjson_mut_doc* jDoc, yyjson_mut_val* jObject, const dd_obj* pObject)
{
	DynamicType* pType = DynamicData_get_type_from_id(pObject->typeId);

	yyjson_mut_obj_add(jObject, yyjson_mut_str(jDoc, "#type"), yyjson_mut_str(jDoc, pType->typeName));
	yyjson_mut_obj_add(jObject, yyjson_mut_str(jDoc, "#guid"), yyjson_mut_guid(jDoc, pObject->guid));

	if (const dd_obj* prototype_object = DynamicData_read_object(pObject->prototype))
	{
		yyjson_mut_obj_add(jObject, yyjson_mut_str(jDoc, "#prototype_guid"), yyjson_mut_guid(jDoc, prototype_object->guid));
	}

	for (i32 i = 0; i < pType->numProperties; ++i)
	{
		const DynamicDataPropertyDef* pDef = &pType->properties[i];
		DynamicData_serialize_json_value(jDoc, jObject, pObject, pDef->nameHash);
	}
}

void DynamicData_serialize_json_value(yyjson_mut_doc* jDoc, yyjson_mut_val* into, const dd_obj* object, u64 hMember)
{
	DynamicType* pType = s_types[object->typeId];

	i32 i;
	if (findProperty(pType, hMember, &i))
	{
		DynamicValue value = object->members.values[i];
		MemberStatus status = object->members.statuses[i];

		if (status == MemberStatus::Inherited)
		{
			return;
		}

		switch (value.type)
		{
		case DynamicValue::Type_Null:
		{
			DYNAMIC_DATA_ERROR("Type Error: cant serialize Null");
			yyjson_mut_val* jKey = yyjson_mut_str(jDoc, string_repository_get(hMember));
			yyjson_mut_val* jValue = yyjson_mut_null(jDoc);
			yyjson_mut_obj_add(into, jKey, jValue);
			break;
		}
		case DynamicValue::Type_Object:
		{
			yyjson_mut_val* jKey = yyjson_mut_str(jDoc, string_repository_get(hMember));
			yyjson_mut_val* jValue = yyjson_mut_obj(jDoc);
			DynamicData_serialize_json_subobject(jDoc, jValue, DynamicData_read_object(value.obj_id));
			yyjson_mut_obj_add(into, jKey, jValue);
			break;
		}
		case DynamicValue::Type_Set:
		{
			DynamicData_serialize_json_set(jDoc, into, object, i);
			break;
		}
		case DynamicValue::Type_Integer:
		{
			bool writeDataValue = status == MemberStatus::Overridden;
			if (status == MemberStatus::Owned)
			{
				writeDataValue = value.integer != 0;
			}

			if (writeDataValue)
			{
				yyjson_mut_val* jKey = yyjson_mut_str(jDoc, string_repository_get(hMember));
				yyjson_mut_val* jValue = yyjson_mut_int(jDoc, value.integer);
				yyjson_mut_obj_add(into, jKey, jValue);
			}
			break;
		}
		case DynamicValue::Type_Number:
		{
			bool writeDataValue = status == MemberStatus::Overridden;
			if (status == MemberStatus::Owned)
			{
				writeDataValue = value.number != 0.0;
			}

			if (writeDataValue)
			{
				yyjson_mut_val* jKey = yyjson_mut_str(jDoc, string_repository_get(hMember));
				yyjson_mut_val* jValue = yyjson_mut_real(jDoc, value.number);
				yyjson_mut_obj_add(into, jKey, jValue);
			}
			break;
		}
		case DynamicValue::Type_String:
		{
			bool writeDataValue = status == MemberStatus::Overridden;
			if (status == MemberStatus::Owned)
			{
				writeDataValue = value.string != nullptr;
			}

			if (writeDataValue)
			{
				yyjson_mut_val* jKey = yyjson_mut_str(jDoc, string_repository_get(hMember));
				yyjson_mut_val* jValue = yyjson_mut_str(jDoc, value.string);
				yyjson_mut_obj_add(into, jKey, jValue);
			}
			break;
		}
		}
	}
}

void DynamicData_serialize_json_file(const char* name, const dd_obj* object)
{
	yyjson_mut_doc* jDoc = yyjson_mut_doc_new(nullptr);

	yyjson_mut_val* jRoot = yyjson_mut_obj(jDoc);
	yyjson_mut_doc_set_root(jDoc, jRoot);

	DynamicData_serialize_json_subobject(jDoc, jRoot, object);

	char buf[128];
	sprintf_s(buf, "entities/%s.json", name);

	yyjson_write_flag flg = YYJSON_WRITE_PRETTY | YYJSON_WRITE_ESCAPE_UNICODE;
	yyjson_write_err err;
	yyjson_mut_write_file(buf, jDoc, flg, nullptr, &err);

	if (err.code)
	{
		printf("write error (%u): %s\n", err.code, err.msg);
		DYNAMIC_DATA_ERROR("Could not write file");
	}

	yyjson_mut_doc_free(jDoc);
}

bool DynamicData_deserialize_json_file(const char* path, dd_id_t* outCreated, Array<DynamicData_UnresolvedObject>* inoutUnresolved)
{
	yyjson_read_err err;
	yyjson_doc* jDoc = yyjson_read_file(path, 0, nullptr, &err);
	yyjson_val* jRoot = yyjson_doc_get_root(jDoc);

	if (err.code)
	{
		printf("read error (%u): %s\n", err.code, err.msg);
		DYNAMIC_DATA_ERROR("Could not read file");
		return false;
	}
	else
	{
		const char* typeName = yyjson_get_str(yyjson_obj_get(jRoot, "#type"));
		Guid guid = yyjson_get_guid(yyjson_obj_get(jRoot, "#guid"));
		yyjson_val* jPrototypeGuid = yyjson_obj_get(jRoot, "#prototype_guid");

		u64 typeNameHash = murmur_hash_string(typeName);
		i32 typeId = DynamicData_get_type_id_from_name(typeNameHash);

		if (guid.a == 0 && guid.b == 0)
		{
			DYNAMIC_DATA_ERROR("Deserialize Error");
			yyjson_doc_free(jDoc);
			return false;
		}
		else
		{
			dd_obj* object_w = DynamicData_create_from_type_with_guid(typeId, guid, false);
			*outCreated = object_w->id;

			if (jPrototypeGuid)
			{
				Guid prototypeGuid = yyjson_get_guid(jPrototypeGuid);
				DynamicData_UnresolvedObject& r = inoutUnresolved->push_back();
				r.object.object_id = *outCreated;
				r.object.prototype = prototypeGuid;
				r.isSet = false;
			}

			DynamicData_deserialize_json_subobject(jRoot, object_w, inoutUnresolved, jPrototypeGuid != nullptr);
		}
	}

	yyjson_doc_free(jDoc);

	return true;
}

void DynamicData_resolve_unresolved(Array<DynamicData_UnresolvedObject>* unresolveds)
{
	for (DynamicData_UnresolvedObject& r : *unresolveds)
	{
		if (r.isSet)
		{
			if (r.set.isRemove)
			{
				dd_id_t* pRemovedObject = DynamicData_get_from_guid(r.set.guid);
				if (pRemovedObject)
				{
					r.set.pSet->removed.values.add(pRemovedObject->as_u64, *pRemovedObject);
				}
				else
				{
					DYNAMIC_DATA_ERROR("Cannot resolve object");
				}
			}
			else
			{
				dd_id_t* pPrototype = DynamicData_get_from_guid(r.set.guid);
				if (pPrototype)
				{
					r.set.pSet->instantiated.id_to_instance.add(pPrototype->as_u64, r.set.instantiated);
					r.set.pSet->instantiated.instance_to_id.add(r.set.instantiated.as_u64, *pPrototype);
				}
				else
				{
					DYNAMIC_DATA_ERROR("Cannot resolve object");
				}
			}
		}
		else
		{
			dd_obj* pObject = DynamicData_edit_object(r.object.object_id);
			dd_id_t* pPrototype = DynamicData_get_from_guid(r.object.prototype);

			if (pPrototype)
			{
				pObject->prototype = *pPrototype;
			}
			else
			{
				DYNAMIC_DATA_ERROR("Cannot resolve object");
			}
		}
	}
}
