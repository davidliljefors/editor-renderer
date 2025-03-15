#include "Scene.h"

#include <stdlib.h>

#include "EditorRenderer.h"
#include "imgui.h"

MallocAllocator ma;

Scene::Scene(EditorRenderer* renderer)
	:m_viewports(ma)
{
	m_renderer = renderer;

	for (int x = -10; x < 10; x++)
	{
		for (int y = -10; y < 10; y++)
		{
			for (int z = -10; z < 10; z++)
			{
				addInstance({ x * 5.0f, y * 5.0f, z * 5.0f });
			}
		}
	}
	addInstance({ 5.0f, 5.0f, 5.0f });

	m_lists[0].data = (Instance*)malloc(sizeof(Instance) * 1024);
	m_lists[1].data = (Instance*)malloc(sizeof(Instance) * 1024);
	m_lists[0].capacity = 1024;
	m_lists[1].capacity = 1024;

	addViewport("View 1");
	addViewport("View 2");
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

void Scene::addViewport(const char* name)
{
	SceneViewport* scv = new SceneViewport();
	scv->scene = this;
	scv->name = name;
	m_viewports.push_back(scv);
	::addViewport(m_renderer, scv);
}

DrawList Scene::getDrawList()
{
	DrawList& drawList = m_lists[writeSlot];
	
	u64 count = m_instances.size;
	
	if(count > drawList.capacity)
	{
		drawList.data = (Instance*)realloc(drawList.data, sizeof(Instance) * count);
		drawList.capacity = count;
	}

	drawList.count = count;
	u64 idx = 0;

	for(auto& instance : m_instances)
	{
		drawList.data[idx++] = instance;
	}

	return m_lists[writeSlot];
}

void SceneViewport::onGui()
{
	ImGui::Begin(name);
	ImGui::Image(ViewportTex, ImVec2(size.x, size.y));
	ImGuiIO& io = ImGui::GetIO();
	static ImVec2 initialCursorPos = ImVec2(0, 0);
	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) 
	{
		if (!dragging) 
		{
			initialCursorPos = ImGui::GetMousePos();
			dragging = true;
		}
	}
	if (dragging) 
	{
		float2 mouseDelta(io.MouseDelta.x, io.MouseDelta.y);
		float sensitivity = 0.3f * io.DeltaTime;
		camera.m_yaw += mouseDelta.x * sensitivity;
		camera.m_pitch += mouseDelta.y * sensitivity;
		camera.m_pitch = clamp(camera.m_pitch, -3.14f / 2.0f, 3.14f / 2.0f);
	}

	if (!ImGui::IsMouseDown(1) && dragging) {
		dragging = false;
		ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
	}

	if(ImGui::IsItemHovered())
	{
		bool w = ImGui::IsKeyDown(ImGuiKey_W);
		bool s = ImGui::IsKeyDown(ImGuiKey_S);
		bool a = ImGui::IsKeyDown(ImGuiKey_A);
		bool d = ImGui::IsKeyDown(ImGuiKey_D);
		bool boost = ImGui::IsKeyDown(ImGuiKey_LeftShift);

		camera.update_movement(w, a, s, d, boost, io.DeltaTime);
	}

	ImVec2 windowSize = ImGui::GetWindowSize();
	size.x = windowSize.x;
	size.y = windowSize.y;
	ImGui::End();
}

DrawList SceneViewport::getDrawList()
{
	return scene->getDrawList();
}