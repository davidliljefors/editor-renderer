#include "Scene.h"

#include <stdlib.h>

#include "EditorRenderer.h"

Scene::Scene(EditorRenderer* renderer)
{
	m_renderer = renderer;

	for (int x = 0; x < 100; x++)
	{
		for (int y = 0; y < 100; y++)
		{
			for (int z = 0; z < 100; z++)
			{
				addInstance({ x * 5.0f, y * 5.0f, z * 5.0f });
			}
		}
	}


	addInstance({ 5.0f, 5.0f, 5.0f });

	m_lists[0].data = (Instance*)malloc(sizeof(Instance) * 1024);
	m_lists[1].data = (Instance*)malloc(sizeof(Instance) * 1024);
	m_lists[0].cap = 1024;
	m_lists[1].cap = 1024;
}


void Scene::buildDrawList()
{
	DrawList& drawList = m_lists[writeSlot];
	u64 count = m_instances.size;
	
	if(count > drawList.cap)
	{
		drawList.data = (Instance*)realloc(drawList.data, sizeof(Instance) * count);
		drawList.cap = count;
	}

	drawList.size = count;
	u64 idx = 0;
	for(auto& instance : m_instances)
	{
		drawList.data[idx++] = instance;
	}

	
}

bool Scene::getDrawList(DrawList& outDrawList)
{
	outDrawList = m_lists[readSlot];
	return true;
}

u64 Scene::addInstance(float3 pos)
{
    u64 id = m_nextInstance++;
    m_instances.insert(id, { pos, 0});
    return id;
}

void Scene::updateInstance(u64 id, float3 pos)
{
    Instance* instance = m_instances.find(id);
    if(instance)
    {
        instance->m_position = pos;
    }
}

void Scene::popInstance(u64 id)
{
	m_instances.erase(id);
}