#include "DynamicData.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>

#include "Core/HashMap.h"
#include "Core/Array.h"
#include "Core/TempAllocator.h"

#include "imgui.h"
#include "murmurhash.inl"
#include "Random.h"

#include "yyjson.h"

static u64 s_object_id = 0;

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

bool findId(const Array<u64>& values, u64 id, i32* outIndex)
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

bool findValue(const Array<DynamicData>& values, DynamicData* pValue, i32* outIndex)
{
	for (i32 i = 0; i < values.size(); ++i)
	{
		if (values[i].type == pValue->type && values[i].hObject == pValue->hObject)
		{
			*outIndex = i;
			return true;
		}
	}
	return false;
}


struct DynamicObject
{
	u64 id;
	u64 hRoot;

	Guid guid;
	DynamicData prototype;

	struct Members
	{
		u64* names;
		DynamicData* values;
		MemberStatus* statuses;
	};

	i32 typeId;
	i32 numMembers;
	Members members;
	u64 version;
};

struct DynamicSet
{
	struct Added
	{
		Array<DynamicData> values;
	};

	struct Removed
	{
		Array<DynamicData> values;
	};

	struct Instantiated
	{
		Array<u64> ids;
		Array<DynamicData> values;
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

	i32 numProperties;
	u64* nameHashToProperty;
	DynamicDataPropertyDef* properties;
};

struct ObjectBlock
{
	constexpr static u32 NUM_ENTRIES = 65536;
	DynamicObject objects[NUM_ENTRIES];
	ObjectBlock* prev;
};

struct ObjectAllocator
{
	ObjectBlock* pCurrentBlock;
	u32 cursor;
};

DynamicObject* ObjectAllocator_allocate(ObjectAllocator* a)
{
	if (a->cursor == ObjectBlock::NUM_ENTRIES)
	{
		a->cursor = 0;
		ObjectBlock* pNext = new(DD_ALLOCATOR) ObjectBlock();
		memset(pNext, 0, sizeof ObjectBlock);
		pNext->prev = a->pCurrentBlock;
		a->pCurrentBlock = pNext;
	}

	u32 index = a->cursor++;
	return &a->pCurrentBlock->objects[index];
}

struct SetBlock
{
	constexpr static u32 NUM_ENTRIES = 65536;
	DynamicSet objects[NUM_ENTRIES];
	SetBlock* prev;
};

struct SetAllocator
{
	SetBlock* pCurrentBlock;
	u32 cursor;
};

DynamicSet* SetAllocator_allocate(SetAllocator* a)
{
	if (a->cursor == SetBlock::NUM_ENTRIES)
	{
		a->cursor = 0;
		SetBlock* pNext = new(DD_ALLOCATOR) SetBlock();
		memset(pNext, 0, sizeof SetBlock);
		pNext->prev = a->pCurrentBlock;
		a->pCurrentBlock = pNext;
	}

	u32 index = a->cursor++;
	DynamicSet* pset = &a->pCurrentBlock->objects[index];
	pset->instantiated.ids.set_allocator(DD_ALLOCATOR);
	pset->instantiated.values.set_allocator(DD_ALLOCATOR);
	pset->added.values.set_allocator(DD_ALLOCATOR);
	pset->removed.values.set_allocator(DD_ALLOCATOR);

	return &a->pCurrentBlock->objects[index];
}

static HashMap<const char*> s_string_repository;
static HashMap<i32> s_typeNameToTypeId;
static Array<DynamicType*> s_types;
static HashMap<DynamicObject*> s_objects;
static HashMap<DynamicData> s_guidToObject;
static ObjectAllocator* s_objectAllocator;
static SetAllocator* s_setAllocator;

DynamicType* DynamicData_get_type_from_id(i32 typeId)
{
	return s_types[typeId];
}

DynamicData* DynamicData_get_from_guid(Guid guid)
{
	u64 hGuid = murmur_hash(&guid, 16, 0);
	return s_guidToObject.find(hGuid);
}

DynamicObject* lookup_obj(u64 hObject)
{
	auto find = s_objects.find(hObject);
	return find ? *find : nullptr;
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

	s_guidToObject.reserve(1 << 23);
	s_objects.reserve(1 << 23);

	s_objectAllocator = new(DD_ALLOCATOR) ObjectAllocator();
	s_setAllocator = new(DD_ALLOCATOR) SetAllocator();

	ObjectBlock* pBlock = new(DD_ALLOCATOR) ObjectBlock();
	pBlock->prev = nullptr;
	s_objectAllocator->pCurrentBlock = pBlock;
	s_objectAllocator->cursor = 0;

	SetBlock* pSetBlock = new(DD_ALLOCATOR) SetBlock();
	pSetBlock->prev = nullptr;
	s_setAllocator->pCurrentBlock = pSetBlock;
	s_setAllocator->cursor = 0;

	s_object_id = 1;

	DynamicData_register_type("Null Object Type", nullptr, 0);
}

void DynamicData_shutdown()
{
	s_typeNameToTypeId.reset();
	s_string_repository.reset();
	s_types.reset();
	s_objects.reset();
	s_guidToObject.reset();
}

DynamicData DynamicData_obj_new_with_guid(Guid guid)
{
	DynamicData value;

	DynamicObject* pObject = ObjectAllocator_allocate(s_objectAllocator);

	u64 id = next_obj_id();

	value.type = DynamicData::Type_Object;
	value.hObject = id;

	pObject->version = 1;
	pObject->id = id;
	pObject->guid = guid;

	s_objects.add(id, pObject);

	u64 hGuid = murmur_hash(&guid, 16, 0);
	s_guidToObject.add(hGuid, value);

	return value;
}

DynamicData DynamicData_obj_new()
{
	return DynamicData_obj_new_with_guid(Random_guid());
}

DynamicData DynamicData_set_new()
{
	DynamicData value;

	value.type = DynamicData::Type_Set;
	value.pSet = SetAllocator_allocate(s_setAllocator);

	return value;
}

DynamicData DynamicData_str_new()
{
	DynamicData value;

	value.type = DynamicData::Type_String;
	value.string = nullptr;

	return value;
}

DynamicData DynamicData_int_new()
{
	DynamicData value;

	value.type = DynamicData::Type_Integer;
	value.integer = 0;

	return value;
}

DynamicData DynamicData_num_new()
{
	DynamicData value;

	value.type = DynamicData::Type_Number;
	value.number = 0.0;

	return value;
}

DynamicData DynamicData_make_int(i64 integer)
{
	DynamicData value = DynamicData_int_new();
	value.integer = integer;
	return value;
}

DynamicData DynamicData_make_str(const char* str)
{
	DynamicData value = DynamicData_str_new();

	u64 capacity = strlen(str) + 1;
	value.string = (char*)DD_ALLOCATOR->alloc(capacity);
	strcpy_s(value.string, capacity, str);

	return value;
}

DynamicData DynamicData_make_null()
{
	DynamicData data;
	data.type = DynamicData::Type_Null;
	data.integer = 0;
	return data;
}

DynamicData DynamicData_make_num(f64 number)
{
	DynamicData value = DynamicData_num_new();
	value.number = number;
	return value;
}

DynamicData DynamicData_instantiate(DynamicData* pPrototype)
{
	DynamicData value = DynamicData_make_null();

	if (pPrototype->type == DynamicData::Type_Object)
	{
		DynamicObject* pProtoObject = pPrototype->asObject();
		value = DynamicData_create_from_type(pProtoObject->typeId);
		DynamicObject* pObject = value.asObject();

		for (i32 i = 0; i < pProtoObject->numMembers; ++i)
		{
			if (pProtoObject->members.values[i].type == DynamicData::Type_Set)
			{
				pObject->members.values[i] = DynamicData_set_new();
				pObject->members.values[i].asSet()->typeId = pProtoObject->members.values[i].asSet()->typeId;
				pObject->members.statuses[i] = MemberStatus::Set;
			}
			else
			{
				pObject->members.values[i] = pProtoObject->members.values[i];
				pObject->members.statuses[i] = MemberStatus::Inherited;
			}
		}

		pObject->prototype = *pPrototype;
	}
	else
	{
		DYNAMIC_DATA_ERROR("Only objects can be instanced");
	}

	return value;
}

DynamicData DynamicData_instantiate_member_impl(DynamicObject* pObject, u64 hName)
{
	i32 i;
	DynamicData created = DynamicData_make_null();
	if (findName(pObject->members.names, pObject->numMembers, hName, &i))
	{
		if (pObject->members.statuses[i] == MemberStatus::Inherited)
		{
			DynamicData* pPrototype = &pObject->members.values[i];

			created = DynamicData_instantiate(pPrototype);
			pObject->members.values[i] = created;
			pObject->members.statuses[i] = MemberStatus::Instantiated;
		}
		else
		{
			DYNAMIC_DATA_ERROR("Instantiate something that is not inherited is not valid");
		}
	}

	return created;
}

DynamicData DynamicData_instantiate_subobject(DynamicData* pValue, u64 hMember)
{
	DynamicData created = DynamicData_make_null();

	if (DynamicObject* pObject = pValue->asObject())
	{
		if (DynamicObject* pPrototype = pObject->prototype.asObject())
		{
			i32 i;
			if (findName(pObject->members.names, pObject->numMembers, hMember, &i))
			{
				if (pObject->members.statuses[i] == MemberStatus::Inherited)
				{
					if (pPrototype->members.statuses[i] == MemberStatus::Instantiated || pPrototype->members.statuses[i] == MemberStatus::Owned)
					{
						created = DynamicData_instantiate(&pPrototype->members.values[i]);
						pObject->members.values[i] = created;
						pObject->members.statuses[i] = MemberStatus::Instantiated;
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
		}
	}

	return created;
}

void DynamicData_clear_instantiated_subobject(DynamicData* pValue, u64 hMember)
{
	if (pValue->type == DynamicData::Type_Object)
	{
		DynamicObject* pObject = pValue->asObject();
		i32 i;
		if (findName(pObject->members.names, pObject->numMembers, hMember, &i))
		{
			if (pObject->members.statuses[i] == MemberStatus::Instantiated)
			{
				pObject->members.values[i] = pObject->members.values[i].asObject()->prototype;
				pObject->members.statuses[i] = MemberStatus::Inherited;
			}
		}
	}
}

DynamicData DynamicData_instantiate_subobject_from_set(DynamicData* pParent, u64 hSetMember, DynamicData* pValue)
{
	if (DynamicObject* pObject = pParent->asObject())
	{
		if (pObject->prototype.id() == 0)
		{
			DYNAMIC_DATA_ERROR("Instantiate only valid on objects with a prototype");
		}

		i32 i;
		if (findName(pObject->members.names, pObject->numMembers, hSetMember, &i))
		{

			if (pObject->members.statuses[i] == MemberStatus::Set)
			{
				if (DynamicSet* pSet = pObject->members.values[i].asSet())
				{
					if (findId(pSet->instantiated.ids, pValue->id(), &i))
					{
						DYNAMIC_DATA_ERROR("value is already instantiated");
					}
					else
					{
						// todo do we need to get set here
						TempAllocator ta;
						Array<DynamicData> set = DynamicData_get_subobject_set(pParent, hSetMember, &ta);
						if (findValue(set, pValue, &i))
						{
							pSet->instantiated.ids.push_back(pValue->id());
							DynamicData instantiated = DynamicData_instantiate(pValue);
							pSet->instantiated.values.push_back(instantiated);
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
			DYNAMIC_DATA_ERROR(Printf("Object does not contain member [%s]", string_repository_get(hSetMember)).cstr());
		}
	}

	return DynamicData_make_null();
}

void DynamicData_remove_instantiated_subobject_from_set(DynamicData* pParent, u64 hSetMember, DynamicData* pValue)
{
	if (DynamicObject* pObject = pParent->asObject())
	{
		if (pObject->prototype.id() == 0)
		{
			DYNAMIC_DATA_ERROR("Instantiate only valid on objects with a prototype");
		}

		i32 i;
		if (findName(pObject->members.names, pObject->numMembers, hSetMember, &i))
		{
			if (pObject->members.statuses[i] == MemberStatus::Set)
			{
				if (DynamicSet* pSet = pObject->members.values[i].asSet())
				{
					if (findValue(pSet->instantiated.values, pValue, &i))
					{
						pSet->instantiated.ids.erase(i);
						pSet->instantiated.values.erase(i);
					}
					else
					{
						DYNAMIC_DATA_ERROR("value is not instantiated in set");
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
			DYNAMIC_DATA_ERROR(Printf("Object does not contain member [%s]", string_repository_get(hSetMember)).cstr());
		}
	}
}

void DynamicData_add_to_subobject_set(DynamicData* pParent, u64 hSetMember, DynamicData* pValue)
{
	if (DynamicObject* pObject = pParent->asObject())
	{
		i32 i;
		if (findName(pObject->members.names, pObject->numMembers, hSetMember, &i))
		{
			MemberStatus membersStatus = pObject->members.statuses[i];
			if (membersStatus == MemberStatus::Set || membersStatus == MemberStatus::Owned)
			{
				if (DynamicSet* pSet = pObject->members.values[i].asSet())
				{
					pSet->added.values.push_back(*pValue);
				}
			}
			else
			{
				DYNAMIC_DATA_ERROR("errors");
			}
		}
		else
		{
			DYNAMIC_DATA_ERROR(Printf("Object does not contain member [%s]", string_repository_get(hSetMember)).cstr());
		}
	}
	else
	{
		DYNAMIC_DATA_ERROR("pValue is not of type Object");
	}
}

void DynamicData_remove_from_subobject_set(DynamicData* pParent, u64 hSetMember, DynamicData* pValue)
{
	if (DynamicObject* pObject = pParent->asObject())
	{
		i32 i;
		if (findName(pObject->members.names, pObject->numMembers, hSetMember, &i))
		{
			MemberStatus status = pObject->members.statuses[i];
			if (status == MemberStatus::Owned || status == MemberStatus::Set)
			{
				if (DynamicSet* pSet = pObject->members.values[i].asSet())
				{
					if (findValue(pSet->added.values, pValue, &i))
					{
						pSet->added.values.erase(i);
					}
					else
					{
						DYNAMIC_DATA_ERROR("object not in the local set");
					}
				}
				else
				{
					DYNAMIC_DATA_ERROR("member is not of type set");
				}
			}
			else
			{
				DYNAMIC_DATA_ERROR("member is not owned/instantiated");
			}
		}
		else
		{
			DYNAMIC_DATA_ERROR("didnt find key");
		}
	}
}

void DynamicData_remove_from_prototype_subobject_set(DynamicData* pParent, u64 hSetMember, DynamicData* pValue)
{
	if (DynamicObject* pObject = pParent->asObject())
	{
		if (pObject->prototype.id() == 0)
		{
			DYNAMIC_DATA_ERROR("remove only valid on objects with a prototype");
		}

		i32 i;
		if (findName(pObject->members.names, pObject->numMembers, hSetMember, &i))
		{
			if (pObject->members.statuses[i] == MemberStatus::Set)
			{
				if (DynamicSet* pSet = pObject->members.values[i].asSet())
				{
					if (findValue(pSet->removed.values, pValue, &i))
					{
						DYNAMIC_DATA_ERROR("value is already removed");
					}
					else
					{
						pSet->removed.values.push_back(*pValue);
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
			DYNAMIC_DATA_ERROR(Printf("Object does not contain member [%s]", string_repository_get(hSetMember)).cstr());
		}
	}
}

void DynamicData_cancel_remove_from_prototype_subobject_set(DynamicData* pParent, u64 hSetMember, DynamicData* pValue)
{
	if (DynamicObject* pObject = pParent->asObject())
	{
		if (pObject->prototype.id() == 0)
		{
			DYNAMIC_DATA_ERROR("Instantiate only valid on objects with a prototype");
		}

		i32 i;
		if (findName(pObject->members.names, pObject->numMembers, hSetMember, &i))
		{
			if (pObject->members.statuses[i] == MemberStatus::Set)
			{
				if (DynamicSet* pSet = pObject->members.values[i].asSet())
				{
					if (findValue(pSet->removed.values, pValue, &i))
					{
						pSet->removed.values.erase(i);
					}
					else
					{
						DYNAMIC_DATA_ERROR("value is not instantiated in set");
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
			DYNAMIC_DATA_ERROR(Printf("Object does not contain member [%s]", string_repository_get(hSetMember)).cstr());
		}
	}
}

void DynamicData_set_compose(DynamicObject* pObject, u64 setMemberIndex, Array<u64>& ids, Array<DynamicData>& values)
{
	if (pObject->members.values[setMemberIndex].type != DynamicData::Type_Set)
	{
		DYNAMIC_DATA_ERROR("Member is not a set");
		return;
	}

	DynamicObject* pPrototype = pObject->prototype.id() != 0 ? lookup_obj(pObject->prototype.id()) : nullptr;

	if (pPrototype)
	{
		DynamicData_set_compose(pPrototype, setMemberIndex, ids, values);
	}

	DynamicSet* pSet = pObject->members.values[setMemberIndex].asSet();

	for (DynamicData& add : pSet->added.values)
	{
		ids.push_back(add.id());
		values.push_back(add);
	}

	for (DynamicData& remove : pSet->removed.values)
	{
		i32 index;
		findId(ids, remove.id(), &index);

		ids.erase(index);
		values.erase(index);
	}

	for (i32 i = 0; i < pSet->instantiated.ids.size(); ++i)
	{
		u64 id = pSet->instantiated.ids[i];
		DynamicData val = pSet->instantiated.values[i];

		i32 index;
		findId(ids, id, &index);
		ids[index] = val.id();
		values[index] = val;
	}
}

Array<DynamicData> DynamicData_get_subobject_set(DynamicData* pValue, u64 hSetMember, Allocator* a)
{
	Array<u64> ids(a);
	Array<DynamicData> values(a);

	if (DynamicObject* pObject = pValue->asObject())
	{
		i32 index;
		findName(pObject->members.names, pObject->numMembers, hSetMember, &index);
		DynamicData_set_compose(pObject, index, ids, values);
	}
	else
	{
		DYNAMIC_DATA_ERROR("value is not an object");
	}

	return values;
}

Array<DynamicData> DynamicData_get_subobject_set_locally_removed(DynamicData* pValue, u64 hSetName, Allocator* a)
{
	Array<DynamicData> setMembers(a);

	if (DynamicObject* pObject = pValue->asObject())
	{
		i32 i;
		if (findName(pObject->members.names, pObject->numMembers, hSetName, &i))
		{
			if (DynamicSet* pSet = pObject->members.values[i].asSet())
			{
				setMembers = pSet->removed.values.clone();
			}
		}
	}

	return setMembers;
}

DynamicData DynamicData_create_from_type_with_guid(i32 typeId, Guid guid, bool createSubobjects)
{
	DynamicType* pType = DynamicData_get_type_from_id(typeId);

	if (pType == nullptr)
	{
		DYNAMIC_DATA_ERROR("Could not find type");
		return DynamicData_make_null();
	}

	DynamicData value = DynamicData_obj_new_with_guid(guid);
	DynamicObject* pObject = value.asObject();

	pObject->numMembers = pType->numProperties;
	pObject->typeId = typeId;

	u64 membersTotalSize = 
		sizeof u64 * pType->numProperties +
		sizeof DynamicData * pType->numProperties +
		sizeof MemberStatus * pType->numProperties;

	uintptr_t pMembers = (uintptr_t)DD_ALLOCATOR->alloc(membersTotalSize);

	pObject->members.names = (u64*)pMembers;
	pMembers += sizeof u64 * pType->numProperties;
	pObject->members.values = (DynamicData*)pMembers;
	pMembers += sizeof DynamicData * pType->numProperties;
	pObject->members.statuses = (MemberStatus*)pMembers;

	for (i32 i = 0; i < pType->numProperties; ++i)
	{
		DynamicDataPropertyDef* pDef = &pType->properties[i];
		pObject->members.names[i] = pDef->nameHash;
		pObject->members.statuses[i] = MemberStatus::Owned;
		DynamicData* pMember = &pObject->members.values[i];

		switch (pDef->type)
		{
		case DynamicData::Type_Null:
			DYNAMIC_DATA_ERROR("Null type definition");
			*pMember = DynamicData_make_null();
			break;
		case DynamicData::Type_Object:
		{
			if (createSubobjects)
			{
				*pMember = DynamicData_create_from_type(pDef->typeId);
			}
			else
			{
				*pMember = DynamicData_make_null();
				pMember->type = DynamicData::Type_Object;
			}
			break;
		}
		case DynamicData::Type_Set:
		{
			*pMember = DynamicData_set_new();
			pMember->asSet()->typeId = pDef->typeId;
			break;
		}
		case DynamicData::Type_Integer:
		{
			*pMember = DynamicData_int_new();
			break;
		}
		case DynamicData::Type_Number:
		{
			*pMember = DynamicData_num_new();
			break;
		}
		case DynamicData::Type_String:
		{
			*pMember = DynamicData_str_new();
			break;
		}
		}
	}

	return value;
}

DynamicData DynamicData_create_from_type(i32 typeId)
{
	return DynamicData_create_from_type_with_guid(typeId, Random_guid(), true);
}

void DynamicData_clone_internal(DynamicData* srcObject, DynamicData* dstObject)
{
	DynamicObject* pSrcObj = srcObject->asObject();
	DynamicObject* pDstObj = dstObject->asObject();

	u64 size = pSrcObj->numMembers;

	pDstObj->members.names = (u64*) DD_ALLOCATOR->alloc(sizeof u64 * size);
	pDstObj->members.values = (DynamicData*)DD_ALLOCATOR->alloc(sizeof DynamicData * size);
	pDstObj->members.statuses = (MemberStatus*)DD_ALLOCATOR->alloc(sizeof MemberStatus * size);

	for (u64 i = 0; i < size; ++i)
	{
		DynamicData* pMember = &pSrcObj->members.values[i];
		DynamicData* pClone = &pDstObj->members.values[i];

		pDstObj->members.names[i] = pSrcObj->members.names[i];
		pDstObj->members.statuses[i] = MemberStatus::Owned;

		switch (pMember->type)
		{
		case DynamicData::Type_Null:
			break;
		case DynamicData::Type_Object:
		{
			*pClone = DynamicData_obj_new();
			DynamicData_clone_internal(pMember, pClone);
			break;
		}
		case DynamicData::Type_Set:
		{
			TempAllocator ta;	
			Array<DynamicData> setMembers = DynamicData_get_subobject_set(srcObject, pSrcObj->members.names[i], &ta);
			*pClone = DynamicData_set_new();
			DynamicSet* pSet = pClone->asSet();
			pSet->typeId = pSrcObj->members.values[i].asSet()->typeId;
			pSet->added.values.resize(setMembers.size());
			for (i32 ii = 0; ii < setMembers.size(); ++ii)
			{
				DynamicData_clone_internal(&setMembers[ii], &pSet->added.values[ii]);
			}
			break;
		}
		case DynamicData::Type_Integer:
		{
			*pClone = DynamicData_int_new();
			pClone->integer = pMember->integer;
			break;
		}
		case DynamicData::Type_Number:
		{

			*pClone = DynamicData_num_new();
			pClone->number = pMember->number;
			break;
		}
		case DynamicData::Type_String:
		{
			*pClone = DynamicData_str_new();
			u64 capacity = pMember->string ? (strlen(pMember->string) + 1) : 0;
			if (capacity)
			{
				pClone->string = (char*)malloc(capacity);
				memcpy(pClone->string, pMember->string, capacity);
			}
			break;
		}
		}
	}
}

DynamicData DynamicData_clone(DynamicData* pValue)
{
	if (pValue->type == DynamicData::Type_Object)
	{
		DynamicData dst = DynamicData_obj_new();
		DynamicData_clone_internal(pValue, &dst);
		return dst;
	}
	else
	{
		DYNAMIC_DATA_ERROR("Can only clone an object");
		return DynamicData_make_null();
	}
}

DynamicData DynamicData_obj_get_impl(DynamicObject* pObject, u64 hName)
{
	i32 i;
	if (findName(pObject->members.names, pObject->numMembers, hName, &i))
	{
		if (pObject->members.statuses[i] == MemberStatus::Inherited)
		{
			return DynamicData_obj_get_impl(pObject->prototype.asObject(), hName);
		}
		else
		{
			return pObject->members.values[i];
		}
	}

	return DynamicData_make_null();
}

DynamicData DynamicData_obj_get(DynamicData* pValue, u64 hMember)
{
	if (pValue->type == DynamicData::Type_Object)
	{
		DynamicObject* pObject = pValue->asObject();
		return DynamicData_obj_get_impl(pObject, hMember);
	}

	return DynamicData_make_null();
}

MemberStatus DynamicData_get_member_status(DynamicData* pValue, u64 hMember)
{
	if (pValue->type == DynamicData::Type_Object)
	{
		DynamicObject* pObject = pValue->asObject();
		i32 i = 0;
		if (findName(pObject->members.names, pObject->numMembers, hMember, &i))
		{
			return pObject->members.statuses[i];
		}
	}

	return MemberStatus::None;
}

MemberStatus DynamicData_get_member_relation(DynamicData* pParent, u64 hMember, DynamicData* pValue)
{
	if (DynamicObject* pObject = pParent->asObject())
	{
		i32 i;
		if (findName(pObject->members.names, pObject->numMembers, hMember, &i))
		{
			DynamicData member = pObject->members.values[i];
			if (member.type == DynamicData::Type_Set)
			{
				DynamicSet* pSet = member.asSet();
				if (findValue(pSet->added.values, pValue, &i))
				{
					return MemberStatus::Added;
				}
				if (findValue(pSet->removed.values, pValue, &i))
				{
					return MemberStatus::Removed;
				}
				if (findValue(pSet->instantiated.values, pValue, &i))
				{
					return MemberStatus::Instantiated;
				}

				// todo make this not awfully slow
				TempAllocator ta;
				Array<DynamicData> inherited = DynamicData_get_subobject_set(pParent, hMember, &ta);
				if (findValue(inherited, pValue, &i))
				{
					return MemberStatus::Inherited;
				}
			}
			else if (member.type == DynamicData::Type_Object)
			{
				return pObject->members.statuses[i];
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
	}
	else
	{
		DYNAMIC_DATA_ERROR("pParent is not object");
	}

	return MemberStatus::None;
}

void DynamicData_assign_root(DynamicData* newRoot, DynamicData* value)
{
	u64 rootId = newRoot->id();

	if (rootId)
	{
		if (DynamicObject* pObject = value->asObject())
		{
			pObject->hRoot = newRoot->id();
		}
	}
}

void DynamicData_obj_set(DynamicData* object, u64 hMember, DynamicData value)
{
	if (value.type == DynamicData::Type_Object || value.type == DynamicData::Type_Set)
	{
		DYNAMIC_DATA_ERROR("assigning a set or object");
		return;
	}

	if (DynamicObject* pObject = object->asObject())
	{
		i32 i;
		if (findName(pObject->members.names, pObject->numMembers, hMember, &i))
		{
			MemberStatus status = pObject->members.statuses[i];
			if (status == MemberStatus::Inherited)
			{
				pObject->members.values[i] = value;
				pObject->members.statuses[i] = MemberStatus::Overridden;
			}
			else if (status == MemberStatus::Owned || status == MemberStatus::Overridden)
			{
				pObject->members.values[i] = value;
			}
		}
		else
		{
			DYNAMIC_DATA_ERROR(Printf("Object does not contain memeber %s", string_repository_get(hMember)).cstr());
		}
	}
}

void DynamicData_obj_clear_override(DynamicData* pValue, u64 hMember)
{
	if (DynamicObject* pObject = pValue->asObject())
	{
		i32 i;
		if (findName(pObject->members.names, pObject->numMembers, hMember, &i))
		{
			MemberStatus status = pObject->members.statuses[i];
			if (status == MemberStatus::Overridden)
			{
				pObject->members.statuses[i] = MemberStatus::Inherited;
			}
		}
		else
		{
			DYNAMIC_DATA_ERROR(Printf("Object does not contain memeber %s", string_repository_get(hMember)).cstr());
		}
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
		return s_types[(i32)*find];
	}

	DYNAMIC_DATA_ERROR("Could not find type");

	return nullptr;
}

i32 DynamicData_register_type(const char* typeName, const DynamicDataPropertyDef* properties, i32 numProperties)
{
	DynamicType* type = new (DD_ALLOCATOR) DynamicType();

	u64 typeNameHash = string_repository_hash(typeName);

	i32 typeId = s_types.size();
	s_types.push_back(type);
	s_typeNameToTypeId.add(typeNameHash, typeId);

	type->nameHashToProperty = (u64*)DD_ALLOCATOR->alloc(sizeof u64 * numProperties);
	type->properties = (DynamicDataPropertyDef*)DD_ALLOCATOR->alloc(sizeof DynamicDataPropertyDef * numProperties);
	type->typeNameHash = typeNameHash;
	type->typeName = string_repository_own(typeName);
	type->typeId = typeId;
	type->numProperties = numProperties;

	memcpy(type->properties, properties, numProperties * sizeof DynamicDataPropertyDef);

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

DynamicData DynamicData_create_from_type_name(u64 hTypeNameHash)
{
	i32 id = DynamicData_get_type_id_from_name(hTypeNameHash);
	return DynamicData_create_from_type(id);
}

struct DebugValuePair
{
	char name[64];
	MemberStatus status;
	DynamicData value;
};

struct DynamicDataObjectDebugView
{
	const char* typeName;
	DynamicData prototype;
	Array<DebugValuePair> values;
};

DynamicDataObjectDebugView DynamicData_DebugExpressionObject(const DynamicObject* pObject)
{
	DynamicDataObjectDebugView debugData;

	debugData.values.set_allocator(GLOBAL_HEAP);
	debugData.typeName = DynamicData_get_type_from_id(pObject->typeId)->typeName;
	debugData.prototype = pObject->prototype;

	for (i32 i = 0; i < pObject->numMembers; ++i)
	{
		DebugValuePair& pair = debugData.values.push_back();
		strcpy_s(pair.name, 64, string_repository_get(pObject->members.names[i]));
		pair.value = pObject->members.values[i];
		pair.status = pObject->members.statuses[i];
	}

	return debugData;
}

DynamicDataObjectDebugView DynamicData_DebugExpression(u64 hObject)
{
	if (DynamicObject* pObject = lookup_obj(hObject))
	{
		return DynamicData_DebugExpressionObject(pObject);
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

void DynamicData_format_value(Printf& buf, DynamicData value)
{
	switch (value.type) {
	case DynamicData::Type_Null:
		buf.write("%s", "Null");
		break;
	case DynamicData::Type_Object:
		buf.write("Object[%d]", value.asObject()->numMembers);
		break;
	case DynamicData::Type_Set:
	{
		buf.write("s", "Set");
		break;
	}
	case DynamicData::Type_Integer:
		buf.write("%lld", value.integer);
		break;
	case DynamicData::Type_Number:
		buf.write("%f", value.number);
		break;
	case DynamicData::Type_String:
		buf.write("%s", value.string);
		break;
	}
}

void DynamicData_view_draw_object_id(DynamicObject* pObject)
{
	if (pObject->prototype.id() == 0)
	{
		if (ImGui::TreeNodeEx(Printf("Object ID : %llu", pObject->id), ImGuiTreeNodeFlags_Leaf))
		{
			ImGui::TreePop();
		}
	}
	else
	{
		if (ImGui::TreeNodeEx(Printf("Object ID : %llu [Prototype ID : %llu]", pObject->id, pObject->prototype.id()), ImGuiTreeNodeFlags_Leaf))
		{
			ImGui::TreePop();
		}
	}
}


void DynamicData_view_object_context_menu(DynamicData* pValue, u64 hMember, bool parentInherited)
{
	if (parentInherited)
		return;

	if (ImGui::BeginPopupContextItem())
	{
		DynamicData member = DynamicData_obj_get(pValue, hMember);
		MemberStatus status = DynamicData_get_member_status(pValue, hMember);

		if (!parentInherited && status == MemberStatus::Overridden)
		{
			if (ImGui::MenuItem("Clear override"))
			{
				DynamicData_obj_clear_override(pValue, hMember);
			}
		}

		if (!parentInherited && status != MemberStatus::Inherited && member.type == DynamicData::Type_Object && ImGui::MenuItem("Create instance of"))
		{
			DynamicData instance = DynamicData_instantiate(&member);

			constexpr u64 hNameField = TM_STATIC_HASH("name", 0xd4c943cba60c270bULL);
			DynamicData name = DynamicData_obj_get(&member, hNameField);
			if (name.type == DynamicData::Type_String)
			{
				DynamicData_obj_set(&instance, hNameField, DynamicData_make_str(Printf("Instance of [%s]", name.asString())));
			}

			Debug_register_root_object(instance);
		}

		if (!parentInherited && status == MemberStatus::Instantiated && (member.type == DynamicData::Type_Set))
		{
			if (ImGui::MenuItem("Reset to prototype"))
			{
				DynamicData val = DynamicData_obj_get(pValue, hMember);
				DynamicData_obj_set(pValue, hMember, val);
			}
		}

		if (!parentInherited && (member.type == DynamicData::Type_Set) && (status == MemberStatus::Owned || status == MemberStatus::Set))
		{
			i32 typeId = member.asSet()->typeId;
			if (typeId != 0)
			{
				const char* type_name = DynamicData_get_type_from_id(typeId)->typeName;

				if (ImGui::MenuItem(Printf("Add new %s", type_name)))
				{
					static int addcount = 0;
					DynamicData added = DynamicData_create_from_type(typeId);
					++addcount;
					DynamicData_obj_set(&added, TM_STATIC_HASH("name", 0xd4c943cba60c270bULL), DynamicData_make_str(Printf("%s %d", type_name, addcount)));
					DynamicData_add_to_subobject_set(pValue, hMember, &added);
				}

				if (ImGui::MenuItem(Printf("Add new %s * 100k", type_name)))
				{
					static int addcount = 0;

					for (i32 i = 0; i < 100'000; ++i)
					{
						DynamicData added = DynamicData_create_from_type(typeId);
						++addcount;
						DynamicData_obj_set(&added, TM_STATIC_HASH("name", 0xd4c943cba60c270bULL), DynamicData_make_str(Printf("%s %d", type_name, addcount)));
						DynamicData_add_to_subobject_set(pValue, hMember, &added);
					}
				}
				 
				if (ImGui::MenuItem(Printf("Add new %s * 1M", type_name)))
				{
					static int addcount = 0;

					for (i32 i = 0; i < 1'000'000; ++i)
					{
						DynamicData added = DynamicData_create_from_type(typeId);
						++addcount;
						DynamicData_obj_set(&added, TM_STATIC_HASH("name", 0xd4c943cba60c270bULL), DynamicData_make_str(Printf("%s %d", type_name, addcount)));
						DynamicData_add_to_subobject_set(pValue, hMember, &added);
					}
				}
			}
		}

		if (!parentInherited && (member.type != DynamicData::Type_Object && member.type != DynamicData::Type_Set) && status == MemberStatus::Inherited)
		{
			if (ImGui::MenuItem("Override value"))
			{
				DynamicData val = DynamicData_obj_get(pValue, hMember);
				DynamicData_obj_set(pValue, hMember, val);
			}
		}

		if (!parentInherited && (member.type == DynamicData::Type_Object) && status == MemberStatus::Inherited)
		{
			if (ImGui::MenuItem("Instantiate subobject"))
			{
				DynamicData_instantiate_subobject(pValue, hMember);
			}
		}

		if (!parentInherited && (member.type == DynamicData::Type_Object || member.type == DynamicData::Type_Set) && status == MemberStatus::Instantiated)
		{
			if (ImGui::MenuItem("Reset to prototype"))
			{
				DynamicData_clear_instantiated_subobject(pValue, hMember);
			}
		}

		if (!parentInherited && member.type == DynamicData::Type_Object)
		{
			if (ImGui::MenuItem("Serialize JSON"))
			{
				constexpr u64 hNameField = TM_STATIC_HASH("name", 0xd4c943cba60c270bULL);
				DynamicData name = DynamicData_obj_get(&member, hNameField);
				DynamicData_serialize_json_file(name.asString(), &member);
			}
		}

		ImGui::EndPopup();
	}
}

void DynamicData_view_impl(DynamicData* pValue, u64 hMember, bool isInherited);

void DynamicData_view_object_set_context_menu(DynamicData* pParent, u64 hMember, DynamicData* pValue, MemberStatus status, bool parentInherited)
{
	if (parentInherited)
		return;

	if (ImGui::BeginPopupContextItem())
	{
		if (status == MemberStatus::Added)
		{
			if (ImGui::MenuItem("Remove from set"))
			{
				DynamicData_remove_from_subobject_set(pParent, hMember, pValue);
			}
		}

		if (status == MemberStatus::Inherited)
		{
			if (ImGui::MenuItem("Instantiate set member"))
			{
				DynamicData_instantiate_subobject_from_set(pParent, hMember, pValue);
			}

			if (ImGui::MenuItem("Remove from prototype set"))
			{
				DynamicData_remove_from_prototype_subobject_set(pParent, hMember, pValue);
			}
		}

		if (status == MemberStatus::Instantiated)
		{
			if (ImGui::MenuItem("Revert to prototype"))
			{
				DynamicData_remove_instantiated_subobject_from_set(pParent, hMember, pValue);
			}
		}

		if (status == MemberStatus::Removed)
		{
			if (ImGui::MenuItem("Cancel remove"))
			{
				DynamicData_cancel_remove_from_prototype_subobject_set(pParent, hMember, pValue);
			}
		}

		ImGui::EndPopup();
	}
}

void DynamicData_view_draw_object(DynamicData* pValue, bool parentInherited)
{
	DynamicObject* pObject = pValue->asObject();
	for (i32 i = 0; i < pObject->numMembers; ++i)
	{
		u64 hMemberName = pObject->members.names[i];
		bool isContainer = pObject->members.values[i].isContainer();
		MemberStatus memberStatus = pObject->members.statuses[i];
		memberStatus = parentInherited ? MemberStatus::Inherited : memberStatus;

		const char* memberName = string_repository_get(hMemberName);

		if (!isContainer)
		{
			Printf buf;
			DynamicData element = DynamicData_obj_get(pValue, hMemberName);
			DynamicData_format_value(buf, element);

			PushStatusStyle(memberStatus);
			bool open = ImGui::TreeNodeEx(Printf("%s : %s", memberName, buf.cstr()), ImGuiTreeNodeFlags_Leaf);
			PopStatusStyle();

			if (open)
			{
				ImGui::TreePop();
			}

			DynamicData_view_object_context_menu(pValue, hMemberName, parentInherited);

			if (element.type == DynamicData::Type_Number && ImGui::IsItemClicked() && !parentInherited)
			{
				DynamicData newPos = DynamicData_make_num(element.asNumber() + 0.5);
				DynamicData_obj_set(pValue, hMemberName, newPos);
			}

			if (element.type == DynamicData::Type_Integer && ImGui::IsItemClicked() && !parentInherited)
			{
				DynamicData newPos = DynamicData_make_int(element.asInt() + 1);
				DynamicData_obj_set(pValue, hMemberName, newPos);
			}
		}
		else
		{
			PushStatusStyle(memberStatus);
			bool childOpen = ImGui::TreeNode(memberName);
			PopStatusStyle();

			DynamicData_view_object_context_menu(pValue, hMemberName, parentInherited);

			if (childOpen)
			{
				DynamicData_view_impl(pValue, hMemberName, memberStatus == MemberStatus::Inherited);
				ImGui::TreePop();
			}
		}
	}
}

void DynamicData_view_draw_object_set(DynamicData* pParent, u64 hSetName, bool parentInherited)
{
	TempAllocator ta;
	Array<DynamicData> members = DynamicData_get_subobject_set(pParent, hSetName, &ta);
	Array<DynamicData> removed = DynamicData_get_subobject_set_locally_removed(pParent, hSetName, &ta);

	const char* setName = string_repository_get(hSetName);

	for (auto& r : removed)
	{
		members.push_back(r);
	}

	for (i32 i = 0; i < members.size(); ++i)
	{
		DynamicData* pValue = &members[i];
		DynamicData memberName = DynamicData_obj_get(pValue, TM_STATIC_HASH("name", 0xd4c943cba60c270bULL));

		Printf genericName = Printf("%s [%d]", setName, (int)i);
		const char* name = memberName.isString() ? memberName.asString() : genericName.cstr();

		ImGui::PushID(name);

		MemberStatus status = DynamicData_get_member_relation(pParent, hSetName, pValue);
		status = parentInherited ? MemberStatus::Inherited : status;

		ImVec2 cursorPos = ImGui::GetCursorScreenPos();

		PushStatusStyle(status);
		ImGuiTreeNodeFlags flags = status == MemberStatus::Removed ? ImGuiTreeNodeFlags_Leaf : 0;

		bool open = ImGui::TreeNodeEx(name, flags);
		PopStatusStyle();

		DynamicData_view_object_set_context_menu(pParent, hSetName, pValue, status, parentInherited);

		if (open)
		{
			if (status != MemberStatus::Removed)
			{
				DynamicData_view_draw_object(pValue, status == MemberStatus::Inherited);
			}
			ImGui::TreePop();
		}

		if (status == MemberStatus::Removed)
		{
			ImVec2 textSize = ImGui::CalcTextSize(genericName.cstr());
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

void DynamicData_view_draw_root_object(DynamicData* pRoot)
{
	DynamicData name = DynamicData_obj_get(pRoot, TM_STATIC_HASH("name", 0xd4c943cba60c270bULL));

	const char* displayName = name.type == DynamicData::Type_String ? name.asString() : "Root";

	PushStatusStyle(pRoot->asObject()->prototype.id() != 0 ? MemberStatus::Instantiated : MemberStatus::Owned);
	bool open = ImGui::TreeNode(displayName);
	PopStatusStyle();
	if (ImGui::BeginPopupContextItem("root_ctx_menu"))
	{
		if (ImGui::MenuItem("Create instance of"))
		{
			DynamicData instance = DynamicData_instantiate(pRoot);

			constexpr u64 hNameField = TM_STATIC_HASH("name", 0xd4c943cba60c270bULL);
			if (name.type == DynamicData::Type_String)
			{
				DynamicData_obj_set(&instance, hNameField, DynamicData_make_str(Printf("Instance of [%s]", name.asString())));
			}
			Debug_register_root_object(instance);
		}

		if (ImGui::MenuItem("Serialize JSON"))
		{
			DynamicData_serialize_json_file(displayName, pRoot);
		}

		ImGui::EndPopup();
	}

	if (open)
	{
		DynamicData_view_draw_object(pRoot, false);
		ImGui::TreePop();
	}
}


void DynamicData_view_impl(DynamicData* pValue, u64 hMember, bool isInherited)
{
	DynamicData value = DynamicData_obj_get(pValue, hMember);
	MemberStatus status = DynamicData_get_member_status(pValue, hMember);

	status = isInherited ? MemberStatus::Inherited : status;

	switch (value.type)
	{
	case DynamicData::Type_Object:
	{
		DynamicData_view_draw_object(&value, isInherited);
		break;
	}
	case DynamicData::Type_Set:
	{
		DynamicData_view_draw_object_set(pValue, hMember, isInherited);
		break;
	}
	case DynamicData::Type_Integer:
	case DynamicData::Type_Number:
	case DynamicData::Type_String:
	case DynamicData::Type_Null:
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

void DynamicData_view(DynamicData* pData)
{
	DynamicData_view_draw_root_object(pData);
}

yyjson_mut_val* yyjson_mut_guid(yyjson_mut_doc* jDoc, Guid guid)
{
	static constexpr char hex_lookup[] = "0123456789abcdef";

	u8 bytes[16];
	char buffer[37];

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
	buffer[36] = '\0';  // Null terminate

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

void DynamicData_deserialize_json_value(yyjson_val* jValue, DynamicObject* pObject, i32 memberIndex, Array<Unresolved>* inoutUnresolved, bool isInstance);
void DynamicData_deserialize_json_subobject(yyjson_val* jObject, DynamicObject* pObject, Array<Unresolved>* inoutUnresolved, bool isInstance);

void DynamicData_deserialize_json_set(yyjson_val* jArray, DynamicSet* pSet, Array<Unresolved>* inoutUnresolved);
void DynamicData_deserialize_json_set_removed(yyjson_val* jArray, DynamicSet* pSet, Array<Unresolved>* inoutUnresolved);
void DynamicData_deserialize_json_set_instantiated(yyjson_val* jArray, DynamicSet* pSet, Array<Unresolved>* inoutUnresolved);

void DynamicData_serialize_json_value(yyjson_mut_doc* jDoc, yyjson_mut_val* into, DynamicObject* pObject, u64 hMember);
void DynamicData_serialize_json_subobject(yyjson_mut_doc* jDoc, yyjson_mut_val* jObject, DynamicObject* pObject);
void DynamicData_serialize_json_set(yyjson_mut_doc* jDoc, yyjson_mut_val* into, DynamicObject* pObject, i32 setIndex);

void DynamicData_deserialize_json_value(yyjson_val* jValue, DynamicObject* pObject, i32 memberIndex, Array<Unresolved>* inoutUnresolved, bool isInstance)
{
	DynamicData* pValue = &pObject->members.values[memberIndex];
	MemberStatus* pStatus = &pObject->members.statuses[memberIndex];

	switch (pValue->type)
	{
	case DynamicData::Type_Null:
	{
		DYNAMIC_DATA_ERROR("Null type def");
		break;
	}
	case DynamicData::Type_Object:
	{
		if (yyjson_is_obj(jValue))
		{
			DynamicType* pParentType = DynamicData_get_type_from_id(pObject->typeId);
			i32 subobjectTypeId = pParentType->properties[memberIndex].typeId;

			const char* typeName = yyjson_get_str(yyjson_obj_get(jValue, "#type"));
			u64 typeNameHash = murmur_hash_string(typeName);
			i32 typeId = DynamicData_get_type_id_from_name(typeNameHash);

			if (typeId == subobjectTypeId)
			{
				Guid guid = yyjson_get_guid(yyjson_obj_get(jValue, "#guid"));
				yyjson_val* jPrototypeGuid = yyjson_obj_get(jValue, "#prototype_guid");

				*pValue = DynamicData_create_from_type_with_guid(typeId, guid, false);

				if (jPrototypeGuid)
				{
					Guid prototypeGuid = yyjson_get_guid(jPrototypeGuid);
					Unresolved& r = inoutUnresolved->push_back();
					r.object.hObject = pValue->id();
					r.object.prototype = prototypeGuid;
					r.isSet = false;
				}
				*pStatus = isInstance ? MemberStatus::Instantiated : MemberStatus::Owned;

				DynamicData_deserialize_json_subobject(jValue, pValue->asObject(), inoutUnresolved, jPrototypeGuid != nullptr);
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
	case DynamicData::Type_Set:
	{
		break;
	}
	case DynamicData::Type_Integer:
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
	case DynamicData::Type_Number:
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
	case DynamicData::Type_String:
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

void DynamicData_deserialize_json_subobject(yyjson_val* jObject, DynamicObject* pObject, Array<Unresolved>* inoutUnresolved, bool isInstance)
{
	constexpr const char* kInstantiated = "#instantiated";
	constexpr const char* kRemoved = "#removed";

	for (i32 i = 0; i < pObject->numMembers; ++i)
	{
		if (pObject->members.values[i].type == DynamicData::Type_Set)
		{
			pObject->members.statuses[i] = isInstance ? MemberStatus::Set : MemberStatus::Owned;
		}
		else
		{
			pObject->members.statuses[i] = isInstance ? MemberStatus::Inherited : MemberStatus::Owned;
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
				if (findName(pObject->members.names, pObject->numMembers, hName, &i))
				{
					DynamicData_deserialize_json_set_instantiated(jValue, pObject->members.values[i].asSet(), inoutUnresolved);
				}
			}
			if (MatchStringSuffix(yyjson_get_str(jKey), kRemoved, &sp))
			{
				u64 hName = murmur_hash(sp.data, (u32)sp.len, 0);
				i32 i;
				if (findName(pObject->members.names, pObject->numMembers, hName, &i))
				{
					DynamicData_deserialize_json_set_removed(jValue, pObject->members.values[i].asSet(), inoutUnresolved);
				}
			}
			else
			{
				u64 hName = string_repository_hash(yyjson_get_str(jKey));
				i32 i;
				if (findName(pObject->members.names, pObject->numMembers, hName, &i))
				{
					DynamicData_deserialize_json_set(jValue, pObject->members.values[i].asSet(), inoutUnresolved);
				}
			}
		}

		u64 hName = string_repository_hash(yyjson_get_str(jKey));
		i32 i;
		if (findName(pObject->members.names, pObject->numMembers, hName, &i))
		{
			DynamicData_deserialize_json_value(jValue, pObject, i, inoutUnresolved, isInstance);
		}
	}
}

void DynamicData_deserialize_json_set(yyjson_val* jArray, DynamicSet* pSet, Array<Unresolved>* inoutUnresolved)
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
				DynamicData setValue = DynamicData_create_from_type_with_guid(typeId, guid, false);
				DynamicData_deserialize_json_subobject(jValue, setValue.asObject(), inoutUnresolved, false);
				pSet->added.values.push_back(setValue);
			}
		}
	}
}

void DynamicData_deserialize_json_set_removed(yyjson_val* jArray, DynamicSet* pSet, Array<Unresolved>* inoutUnresolved)
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
			Unresolved& r = inoutUnresolved->push_back();
			r.isSet = true;

			r.set.pSet = pSet;
			r.set.guid = guid;
			r.set.index = pSet->removed.values.size();
			r.set.isRemove = true;
			pSet->removed.values.push_back(DynamicData_make_null());
		}
	}
}

void DynamicData_deserialize_json_set_instantiated(yyjson_val* jArray, DynamicSet* pSet, Array<Unresolved>* inoutUnresolved)
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
				DynamicData setValue = DynamicData_create_from_type_with_guid(typeId, guid, false);
				DynamicData_deserialize_json_subobject(jValue, setValue.asObject(), inoutUnresolved, true);

				{
					Unresolved& r = inoutUnresolved->push_back();
					r.object.hObject = setValue.id();
					r.object.prototype = prototypeGuid;
					r.isSet = false;
				}

				{
					Unresolved& r = inoutUnresolved->push_back();
					r.isSet = true;

					r.set.guid = prototypeGuid;
					r.set.index = pSet->instantiated.values.size();
					r.set.isRemove = false;
					r.set.pSet = pSet;

					pSet->instantiated.ids.push_back();
					pSet->instantiated.values.push_back(setValue);
				}
			}
		}
	}
}

void DynamicData_serialize_json_set(yyjson_mut_doc* jDoc, yyjson_mut_val* into, DynamicObject* pObject, i32 setIndex)
{
	u64 hSetName = pObject->members.names[setIndex];
	DynamicSet* pSet = pObject->members.values[setIndex].asSet();
	const char* setName = string_repository_get(hSetName);

	{
		yyjson_mut_val* jArr = yyjson_mut_obj_add_arr(jDoc, into, setName);
		for (DynamicData value : pSet->added.values)
		{
			yyjson_mut_val* jElement = yyjson_mut_arr_add_obj(jDoc, jArr);
			DynamicData_serialize_json_subobject(jDoc, jElement, value.asObject());
		}
	}

	if (!pSet->instantiated.values.empty())
	{
		char buf[128];
		snprintf(buf, 128, "%s#instantiated", setName);
		
		yyjson_mut_val* jKey = yyjson_mut_strcpy(jDoc, buf);
		yyjson_mut_val* jArrInstantiated = yyjson_mut_arr(jDoc);

		for (DynamicData value : pSet->instantiated.values)
		{
			yyjson_mut_val* jElement = yyjson_mut_arr_add_obj(jDoc, jArrInstantiated);
			DynamicData_serialize_json_subobject(jDoc, jElement, value.asObject());
		}

		yyjson_mut_obj_add(into, jKey, jArrInstantiated);
	}

	if (!pSet->removed.values.empty())
	{
		char buf[128];
		snprintf(buf, 128, "%s#removed", setName);

		yyjson_mut_val* jKey = yyjson_mut_strcpy(jDoc, buf);
		yyjson_mut_val* jArrRemoved = yyjson_mut_arr(jDoc);

		for (DynamicData value : pSet->removed.values)
		{
			yyjson_mut_arr_add_val(jArrRemoved, yyjson_mut_guid(jDoc, value.asObject()->guid));
		}

		yyjson_mut_obj_add(into, jKey, jArrRemoved);
	}
}

void DynamicData_serialize_json_subobject(yyjson_mut_doc* jDoc, yyjson_mut_val* jObject, DynamicObject* pObject)
{
	DynamicType* pType = DynamicData_get_type_from_id(pObject->typeId);

	yyjson_mut_obj_add(jObject, yyjson_mut_str(jDoc, "#type"), yyjson_mut_str(jDoc, pType->typeName));
	yyjson_mut_obj_add(jObject, yyjson_mut_str(jDoc, "#guid"), yyjson_mut_guid(jDoc, pObject->guid));

	if (DynamicObject* pPrototype = pObject->prototype.asObject())
	{
		yyjson_mut_obj_add(jObject, yyjson_mut_str(jDoc, "#prototype_guid"), yyjson_mut_guid(jDoc, pPrototype->guid));
	}

	for (i32 i = 0; i < pType->numProperties; ++i)
	{
		const DynamicDataPropertyDef* pDef = &pType->properties[i];
		DynamicData_serialize_json_value(jDoc, jObject, pObject, pDef->nameHash);
	}
}

void DynamicData_serialize_json_value(yyjson_mut_doc* jDoc, yyjson_mut_val* into, DynamicObject* pObject, u64 hMember)
{
	i32 i;
	if (findName(pObject->members.names, pObject->numMembers, hMember, &i))
	{
		DynamicData value = pObject->members.values[i];
		MemberStatus status = pObject->members.statuses[i];

		if (status == MemberStatus::Inherited)
		{
			return;
		}

		switch (value.type)
		{
		case DynamicData::Type_Null:
		{
			DYNAMIC_DATA_ERROR("Type Error: cant serialize Null");
			yyjson_mut_val* jKey = yyjson_mut_str(jDoc, string_repository_get(hMember));
			yyjson_mut_val* jValue = yyjson_mut_null(jDoc);
			yyjson_mut_obj_add(into, jKey, jValue);
			break;
		}
		case DynamicData::Type_Object:
		{
			yyjson_mut_val* jKey = yyjson_mut_str(jDoc, string_repository_get(hMember));
			yyjson_mut_val* jValue = yyjson_mut_obj(jDoc);
			DynamicData_serialize_json_subobject(jDoc, jValue, value.asObject());
			yyjson_mut_obj_add(into, jKey, jValue);
			break;
		}
		case DynamicData::Type_Set:
		{	
			DynamicData_serialize_json_set(jDoc, into, pObject, i);
			break;
		}
		case DynamicData::Type_Integer:
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
		case DynamicData::Type_Number:
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
		case DynamicData::Type_String:
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

void DynamicData_serialize_json_file(const char* name, DynamicData* pValue)
{
	yyjson_mut_doc* jDoc = yyjson_mut_doc_new(nullptr);

	yyjson_mut_val* jRoot = yyjson_mut_obj(jDoc);
	yyjson_mut_doc_set_root(jDoc, jRoot);

	DynamicData_serialize_json_subobject(jDoc, jRoot, pValue->asObject());

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

bool DynamicData_deserialize_json_file(const char* path, DynamicData* outData, Array<Unresolved>* inoutUnresolved)
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
			*outData = DynamicData_create_from_type_with_guid(typeId, guid, false);
			if (jPrototypeGuid)
			{
				Guid prototypeGuid = yyjson_get_guid(jPrototypeGuid);
				Unresolved& r = inoutUnresolved->push_back();
				r.object.hObject = outData->id();
				r.object.prototype = prototypeGuid;
				r.isSet = false;
			}
			DynamicData_deserialize_json_subobject(jRoot, outData->asObject(), inoutUnresolved, jPrototypeGuid != nullptr);
		}
	}

	yyjson_doc_free(jDoc);

	return true;
}

void DynamicData_resolve_unresolved(Array<Unresolved>* unresolveds)
{
	for (Unresolved& r : *unresolveds)
	{
		if (r.isSet)
		{
			if (r.set.isRemove)
			{
				DynamicData* pRemovedObject = DynamicData_get_from_guid(r.set.guid);
				if (pRemovedObject)
				{
					r.set.pSet->removed.values[r.set.index] = *pRemovedObject;
				}
				else
				{
					DYNAMIC_DATA_ERROR("Cannot resolve object");
				}
			}
			else
			{
				DynamicData* pPrototype = DynamicData_get_from_guid(r.set.guid);
				if (pPrototype)
				{
					r.set.pSet->instantiated.ids[r.set.index] = pPrototype->id();
				}
				else
				{
					DYNAMIC_DATA_ERROR("Cannot resolve object");
				}
			}
		}
		else
		{
			DynamicObject* pObject = lookup_obj(r.object.hObject);
			DynamicData* pPrototype = DynamicData_get_from_guid(r.object.prototype);

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

