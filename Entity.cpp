#include "Entity.h"

#include <stdio.h>

#include "Editor.h"
#include "imgui.h"

static i32 s_nextId = 0;


Position get_position(ReadOnlySnapshot s, truth::Key objectId)
{
	const Entity* entity = (const Entity*)g_truth->read(s, objectId);

	Position res = entity->position;

	if (entity->prototype.asU64 != 0 && (entity->position.inheritsX || entity->position.inheritsY || entity->position.inheritsZ))
	{
		Position prototypePosition = get_position(s, entity->prototype);

		if (entity->position.inheritsX)
		{
			res.x = prototypePosition.x;

		}

		if (entity->position.inheritsY)
		{
			res.y = prototypePosition.y;
		}

		if (entity->position.inheritsZ)
		{
			res.z = prototypePosition.z;
		}
	}

	return res;
}


bool float_almost_equal(float a, float b) {
    const float epsilon = 0.0001f;
    return fabs(a - b) < epsilon;
}

void set_position(Transaction& tx, truth::Key objectId, Position p)
{
	Entity* entity = (Entity*)g_truth->edit(tx, objectId);
	Position current = get_position(tx.uncommitted.asImmutable(), objectId);

	if (entity->prototype.asU64 != 0)
	{
		if (entity->position.inheritsX && !float_almost_equal(current.x, p.x))
		{
			entity->position.inheritsX = false;
		}

		if (entity->position.inheritsY && !float_almost_equal(current.y,p.y))
		{
			entity->position.inheritsY = false;
		}

		if (entity->position.inheritsZ && !float_almost_equal(current.z, p.z))
		{
			entity->position.inheritsZ = false;
		}
	}

	entity->position.x = p.x;
	entity->position.y = p.y;
	entity->position.z = p.z;
}

void DynamicData_format_value(Printf& buf, DynamicData value)
{
	switch (value.type) {
	case DynamicData::Type_Null:
		buf.write("%s", "Null");
		break;
	case DynamicData::Type_Object:
		buf.write("Object[%d]", DynamicData_size(&value));
		break;
	case DynamicData::Type_Array:
		buf.write("Array[%d]", DynamicData_size(&value));
		break;
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

void DynamicData_view_impl(DynamicData* pData)
{
	switch (pData->type) {
	case DynamicData::Type_Object:
	{
		DDObject* pObject = pData->asObject();
		u64 size = DynamicData_size(pData);

		for (u64 i = 0; i < size; ++i)
		{
			const char* name = string_repository_get(pObject->names[i]);

			if (DynamicData_size(&pObject->values[i]) == 0)
			{
				Printf buf;
				DynamicData_format_value(buf, pObject->values[i]);
				if (ImGui::TreeNodeEx(Printf("%s : %s", name, buf.cstr()), ImGuiTreeNodeFlags_Leaf))
				{
					ImGui::TreePop();
				}
			}
			else 
			{
				if (ImGui::TreeNode(name))
				{
					DynamicData_view_impl(&pObject->values[i]);
					ImGui::TreePop();
				}
			}
		}
		break;
	}
	case DynamicData::Type_Array:
	{
		DDArray* pArray = pData->pArray;
		u64 size = DynamicData_size(pData);
		for (u64 i = 0; i < size; ++i)
		{
			if (ImGui::TreeNode(Printf("Element %d", i)))
			{
				DynamicData_view_impl(&pArray->values[i]);
				ImGui::TreePop();
			}
		}
		break;
	}
	case DynamicData::Type_Integer:
	case DynamicData::Type_Number:
	case DynamicData::Type_String:
	case DynamicData::Type_Null:
	{
		Printf buf;
		DynamicData_format_value(buf, *pData);
		bool r = ImGui::TreeNodeEx(buf.cstr(), ImGuiTreeNodeFlags_Leaf);
		if (r)
		{
			ImGui::TreePop();
		}
		break;
	}
	}
}

static eastl::unordered_map<u64, const char*> s_string_repository;
static std::unordered_map<u64, DynamicData> g_templates;
static std::unordered_map<u64, DynamicDataParser_i> g_parsers;
static std::unordered_map<u64, DDObject*> g_objects;

DDObject* lookup_obj(u64 hObject)
{
	auto find = g_objects.find(hObject);
	return find != g_objects.end() ? find->second : nullptr;
}

DynamicData DynamicData_obj_new()
{
	DynamicData value;

	void* mem = malloc(sizeof(DDObject));
	memset(mem, 0, sizeof(DDObject));

	u64 id = random_u64();

	g_objects[id] = (DDObject*)mem;

	value.type = DynamicData::Type_Object;
	value.hObject = id;

	return value;
}

DynamicData DynamicData_obj_from_type(u64 typeId)
{
	DynamicData value;

	void* mem = malloc(sizeof(DDObject));
	memset(mem, 0, sizeof(DDObject));

	u64 id = random_u64();

	DDObject* pObject = (DDObject*)mem;
	g_objects[id] = pObject;

	pObject->typeId = typeId;
	value.type = DynamicData::Type_Object;
	value.hObject = id;


	return value;
}

DynamicData DynamicData_array_new()
{
	DynamicData value;

	void* mem = malloc(sizeof(DDArray));
	memset(mem, 0, sizeof(DDArray));

	value.type = DynamicData::Type_Array;
	value.pArray = (DDArray*)mem;

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
	value.string = (char*)malloc(capacity);
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

DynamicData DynamicData_num_new()
{
	DynamicData value;

	value.type = DynamicData::Type_Number;
	value.number = 0.0;

	return value;
}

u64 DynamicData_size(const DynamicData* value)
{
	if (const DDObject* pObject = value->asObject())
	{
		return pObject->values.size();
	}

	if (const DDArray* pArray = value->asArray())
	{
		return pArray->values.size();
	}

	return 0;
}

void DynamicData_clone_internal(const DynamicData* src, DynamicData* dst)
{
	switch (src->type)
	{
	case DynamicData::Type_Object:
	{
		*dst = DynamicData_obj_new();
		u64 size = DynamicData_size(src);
		DDObject* pObject = dst->asObject();
		const DDObject* pSrcObject = src->asObject();

		pObject->names.resize(size);
		pObject->values.resize(size);

		for (u64 i = 0; i < size; ++i)
		{
			pObject->names[i] = pSrcObject->names[i];
			DynamicData_clone_internal(&pSrcObject->values[i], &pObject->values[i]);
		}

		break;
	}
	case DynamicData::Type_Array:
	{
		*dst = DynamicData_array_new();
		u64 size = DynamicData_size(src);
		DDArray* pArray = dst->asArray();
		pArray->values.resize(size);
		const DDArray* srcArray = src->asArray();

		for (u64 i = 0; i < size; ++i)
		{
			DynamicData_clone_internal(&srcArray->values[i], &pArray->values[i]);
		}

		break;
	}
	case DynamicData::Type_Integer:
	{
		*dst = DynamicData_int_new();
		dst->integer = src->integer;
		break;
	}
	case DynamicData::Type_Number:
	{
			
		*dst = DynamicData_num_new();
		dst->number = src->number;
		break;
	}
	case DynamicData::Type_String:
	{
		*dst = DynamicData_str_new();
		u64 capacity = src->string ? (strlen(src->string) + 1) : 0;
		if (capacity)
		{
			dst->string = (char*)malloc(capacity);
			strcpy_s(dst->string, capacity, src->string);
		}
		break;
	}
	case DynamicData::Type_Null:
		break;
	}
}

DynamicData DynamicData_clone(const DynamicData* src)
{
	DynamicData value;
	DynamicData_clone_internal(src, &value);
	return value;
}

u64 string_repository_hash(const char* str)
{
	u64 key = MetroHash64::HashStr(str);
	auto find = s_string_repository.find(key);

	if (find == s_string_repository.end())
	{
		s_string_repository[key] = str;
	}

	return key;
}

const char* string_repository_get(u64 hName)
{
	auto find = s_string_repository.find(hName);

	if (find != s_string_repository.end())
	{
		return find->second;
	}

	return "Invalid String";
}

void DynamicData_registerParser(u64 hType, DynamicDataParser_i parser)
{
	g_parsers[hType] = parser;
}

DDEntity DynamicData_readEntity(DynamicData* value)
{
	DDEntity entity;


}

void registerEntityTemplate()
{
	DynamicData root = DynamicData_obj_new();

	u64 hTypeName = string_repository_hash(s_typeNameKey);
	u64 hFields = string_repository_hash(s_fieldsKey);

	DynamicData entityName = DynamicData_make_str("entity");
	DynamicData_obj_add(root, hTypeName, entityName);


	DynamicData field_children = DynamicData_obj_new();
	DynamicData_obj_add(field_children, string_repository_hash("name"), DynamicData_str_new());
	DynamicData_obj_add(field_children, string_repository_hash("children"), DynamicData_array_new());
	DynamicData_obj_add(field_children, string_repository_hash("components"), DynamicData_array_new());
	DynamicData_obj_add(root, hFields, field_children);

	DynamicDataParser_i parser;
	parser.parse = [](DynamicData* value)
	{
		DDEntity* pEntity = (DDEntity*)malloc(sizeof DDEntity);

		u64 hNameField = MetroHash64::HashStr("name");
		u64 hChildrenField = MetroHash64::HashStr("children");
		u64 hComponentsField = MetroHash64::HashStr("components");

		pEntity->name = DynamicData_obj_find(value, hNameField).asString();
		pEntity->children = DynamicData_obj_find(value, hChildrenField).asArray()->values;
		pEntity->components = DynamicData_obj_find(value, hComponentsField).asArray()->values;

		return (void*)pEntity;
	};

	DynamicData_registerParser(ENTITY_TYPE_ID, parser);

	g_templates[ENTITY_TYPE_ID] = root;
}

//void registerComponentTemplate()
//{
//	DynamicData root = DynamicData_obj_new();
//
//	u64 hTypeName = string_repository_hash(s_typeNameKey);
//	u64 hFields = string_repository_hash(s_fieldsKey);
//
//	DynamicData componentName = DynamicData_make_str("component");
//	DynamicData_obj_add(root, hTypeName, componentName);
//
//	DynamicData arrFields = DynamicData_array_new();
//
//	DynamicData field_children = DynamicData_obj_new();
//	DynamicData_obj_add(field_children, string_repository_hash("field_name"), DynamicData_make_str("fields"));
//	DynamicData_obj_add(field_children, string_repository_hash("field_type"), DynamicData_make_str("array"));
//	DynamicData_obj_add(field_children, string_repository_hash("field_element_type"), DynamicData_make_str("object"));
//	DynamicData_array_add(arrFields, field_children);
//
//	DynamicData_obj_add(root, hFields, arrFields);
//
//	g_templates[COMPONENT_TYPE_ID] = root;
//}

void registerComponent_TransformTemplate()
{
	DynamicData root = DynamicData_obj_new();

	u64 hTypeName = string_repository_hash(s_typeNameKey);
	u64 hFields = string_repository_hash(s_fieldsKey);

	DynamicData componentName = DynamicData_make_str("component_transform");
	DynamicData_obj_add(root, hTypeName, componentName);

	DynamicData field_children = DynamicData_obj_new();

	DynamicData_obj_add(field_children, string_repository_hash("x"), DynamicData_num_new());
	DynamicData_obj_add(field_children, string_repository_hash("y"), DynamicData_num_new());
	DynamicData_obj_add(field_children, string_repository_hash("z"), DynamicData_num_new());

	DynamicData_obj_add(root, hFields, field_children);

	DynamicDataParser_i parser;

	parser.parse = [](DynamicData* value)
	{
		DDTransformComponent* pComponent = (DDTransformComponent*)malloc(sizeof DDTransformComponent);

		u64 hXField = MetroHash64::HashStr("x");
		u64 hYField = MetroHash64::HashStr("y");
		u64 hZField = MetroHash64::HashStr("z");

		pComponent->x = (f32)DynamicData_obj_find(value, hXField).asNumber();
		pComponent->y = (f32)DynamicData_obj_find(value, hYField).asNumber();
		pComponent->z = (f32)DynamicData_obj_find(value, hZField).asNumber();

		return (void*)pComponent;
	};

	DynamicData_registerParser(COMPONENT_ID_TRANSFORM, parser);
	g_templates[COMPONENT_ID_TRANSFORM] = root;
}

DynamicData* DynamicData_getTemplate(u64 id)
{
	return &g_templates[id];
}

DynamicData DynamicData_createFromTemplate(u64 hTemplate)
{
	DynamicData value = DynamicData_obj_new();

	DynamicData* pTemplate = DynamicData_getTemplate(hTemplate);
	
	u64 hType = string_repository_hash(s_typeNameKey);
	u64 hTypeId = string_repository_hash(s_typeIdKey);

	const char* typeName = DynamicData_obj_find(pTemplate, hType).asString();

	DynamicData_obj_add(value, hType, DynamicData_make_str(typeName));
	DynamicData_obj_add(value, hTypeId, DynamicData_make_int((i64)hTemplate));

	u64 hFields = string_repository_hash(s_fieldsKey);

	if (DDObject* fields = DynamicData_obj_find(pTemplate, hFields).asObject())
	{
		u64 numFields = fields->values.size();
		for (u64 i = 0; i < numFields; ++i)
		{
			DynamicData valueToAdd;
			DynamicData_clone_internal(&fields->values[i], &valueToAdd);
			DynamicData_obj_add(value, fields->names[i], valueToAdd);
		}
	}

	return value;
}

void DynamicData_view(DynamicData* pData)
{
	DynamicData_view_impl(pData);
}

Entity* Entity::create(Allocator* a)
{
	Entity* entity = alloc<Entity>(a);
	entity->children.set_allocator(a);
	entity->instantiatedRoots.set_allocator(a);
	sprintf_s(entity->name, "New Entity (%d)", s_nextId++);

	return entity;
}

Entity* Entity::createFromPrototype(Allocator* a, truth::Key prototype)
{
	Entity* entity = alloc<Entity>(a);
	const Entity* prototypeEntity  = (const Entity*)g_truth->read(g_truth->snap(), prototype);

	entity->position = prototypeEntity->position;
	entity->prototype = prototype;
	entity->position.inheritsX = true;
	entity->position.inheritsY = true;
	entity->position.inheritsZ = true;

	entity->children.set_allocator(a);
	entity->instantiatedRoots.set_allocator(a);
	sprintf_s(entity->name, "Instance of prototype (%s) ", prototypeEntity->name);

	return entity;
}


TruthElement* Entity::clone(Allocator* a) const
{
	Entity* entityClone = alloc<Entity>(a);
	entityClone->root = root;

	entityClone->children = children.clone();
	entityClone->instantiatedRoots = instantiatedRoots.clone();
	entityClone->position = position;
	entityClone->prototype = prototype;

	memcpy(entityClone->name, name, sizeof(name));
	return entityClone;
}
