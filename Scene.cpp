#include "Scene.h"

#include <stdlib.h>

#include "ScratchAllocator.h"
#include "EditorRenderer.h"
#include "imgui.h"
#include "TruthView.h"

struct Xoshiro256
{
	static uint64_t rotl(const uint64_t x, int k) {
		return (x << k) | (x >> (64 - k));
	}

	uint64_t next()
	{
		const uint64_t result = rotl(s[0] + s[3], 23) + s[0];
		const uint64_t t = s[1] << 17;

		s[2] ^= s[0];
		s[3] ^= s[1];
		s[1] ^= s[2];
		s[0] ^= s[3];

		s[2] ^= t;

		s[3] = rotl(s[3], 45);

		return result;
	}

	uint64_t s[4]{ 0x180ec6d33cfd0aba, 0xd5a61266f0c9392c, 0xa9582618e03fc9aa, 0x39abdc4529b1661c };
};

Xoshiro256 g_rand;

MallocAllocator ma;

Truth* g_truth;

truth::Key nextKey()
{
	truth::Key key;
	key.asU64 = g_rand.next();
	return key;
}

truth::Key getNameHash(const char* str)
{
	truth::Key key;
	key.asU64 = MetroHash64::Hash(str, strlen(str));
	return key;
}

const truth::Key rootKey = getNameHash("root_node");


Scene::Scene(EditorRenderer* renderer)
	: m_viewports(ma)
	, m_instances(ma)
{
	PROF_SCOPE(Scene_Constructor);

	m_renderer = renderer;

	g_truth = alloc<Truth>(ma, ma);
	state = g_truth->m_head;

	Transaction tx = g_truth->openTransaction();
	Entity* rootEntity = alloc<Entity>(ma, ma);
	g_truth->add(tx, rootKey, rootEntity);

	for (int x = -10; x < 10; x++)
	{
		for (int y = -10; y < 10; y++)
		{
			for (int z = -10; z < 10; z++)
			{
				truth::Key childKey = nextKey();
				rootEntity->m_children.push_back(childKey);
				Entity* child = alloc<Entity>(ma, ma);
				child->position = { (float)x * 5.0f, (float)y * 5.0f, (float)z * 5.0f };
				g_truth->add(tx, childKey, child);
			}
		}
	}

	g_truth->tryCommit(tx);

	m_lists[0].data = (Instance*)malloc(sizeof(Instance) * 1024);
	m_lists[1].data = (Instance*)malloc(sizeof(Instance) * 1024);
	m_lists[0].capacity = 1024;
	m_lists[1].capacity = 1024;

	addViewport("View 1");
	addViewport("View 2");
}

void Scene::update()
{
	TempAllocator ta;
	Array<KeyEntry> adds(ta);
	Array<KeyEntry> removes(ta);
	Array<KeyEntry> edits(ta);

	diff(state, g_truth->m_head, adds, edits, removes);

	for (i32 i = 0; i < adds.size(); ++i)
	{
		Entity* e = (Entity*)adds[i].value;
		u64 key = adds[i].key.asU64;
		(void)key;
		addInstance(adds[i].key.asU64, e->position);
	}

	for (KeyEntry& edit : edits)
	{
		if (edit.key != rootKey)
		{
			Entity* i = (Entity*)edit.value;
			updateInstance(edit.key.asU64, i->position);
		}
	}

	for (KeyEntry& remove : removes)
	{
		if (remove.key != rootKey)
		{
			popInstance(remove.key.asU64);
		}
	}

	state = g_truth->m_head;
}

void Scene::addInstance(u64 key, float3 pos)
{
    m_instances.insert_or_assign(key, Instance{ pos, 0});
}

void Scene::updateInstance(u64 id, float3 pos)
{
    Instance* instance = m_instances.find(id);
    if(instance)
    {
        instance->pos = pos;
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
	
	i32 count = m_instances.size();
	
	if(count > drawList.capacity)
	{
		drawList.data = (Instance*)realloc(drawList.data, sizeof(Instance) * count);
		drawList.capacity = count;
	}

	drawList.count = count;
	u64 idx = 0;

	for(Instance& instance : m_instances)
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