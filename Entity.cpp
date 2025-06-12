#include "Entity.h"

#include <stdio.h>

#include "Editor.h"

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
