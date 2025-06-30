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

struct Printf
{
	Printf(const char* fmt, ...)
	{
		va_list args;
	    va_start(args, fmt);
	    int result = vsnprintf(buf, 128, fmt, args);
	    va_end(args);
	}

	operator const char*()
	{
		return buf;
	}

	char buf[128];
};

void DynamicData_view_impl(const DynamicData* pData)
{
	switch (pData->type) {
	case DynamicData::Type_Object:
	{
		DDObject* pObject = pData->pObject;
		u64 size = DynamicData_size(pData);
		for (u64 i = 0; i < size; ++i)
		{
			const char* name = string_repository_get(pObject->names[i]);
			if (ImGui::TreeNode(name))
			{
				DynamicData_view(pObject->values[i]);
				ImGui::TreePop();
			}
		}
		break;
	}
	case DynamicData::Type_ObjectRef:
		break;
	case DynamicData::Type_Array:
	{
		DDArray* pArray = pData->pArray;
		u64 size = DynamicData_size(pData);
		for (u64 i = 0; i < size; ++i)
		{
			DynamicData_view(pArray->values[i]);
		}
		break;
	}
	case DynamicData::Type_Instance:
		break;
	case DynamicData::Type_Integer:
	{
		bool r = ImGui::TreeNodeEx(Printf("Integer : %lld", pData->integer), ImGuiTreeNodeFlags_Leaf);
		if (r)
		{
			ImGui::TreePop();
		}
		break;
	}
	case DynamicData::Type_Number:
	{
		bool r = ImGui::TreeNodeEx(Printf("Number : %f", pData->number), ImGuiTreeNodeFlags_Leaf);
		if (r)
		{
			ImGui::TreePop();
		}
		break;
	}
	case DynamicData::Type_String:
	{

		bool r = ImGui::TreeNodeEx(Printf("String : %s", pData->string), ImGuiTreeNodeFlags_Leaf);
		if (r)
		{
			ImGui::TreePop();
		}
		break;
	}
	}
}

static eastl::unordered_map<u64, const char*> s_string_repository;

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

void DynamicData_view(const DynamicData* pData)
{
	ImGui::Begin("DynamicDataWindow");

	if (pData)
	{
		DynamicData_view_impl(pData);
	}

	ImGui::End();
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
