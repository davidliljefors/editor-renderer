#include "Scene.h"
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
}


void Scene::updateThread()
{
	static float current_time = 0.0f;
	current_time += 0.001f;
    int id = 0;
}

void Scene::renderThread()
{
	renderFrame(m_renderer, m_instances);
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