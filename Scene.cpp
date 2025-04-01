#include "Scene.h"

#include <cstdio>
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
	state = g_truth->head();

	m_undoWindow = alloc<UndoStackWindow>(ma, g_truth);
	m_outlinerWindow = alloc<OutlinerWindow>(ma, g_truth);
	addEditorWindow(renderer, m_undoWindow);
	addEditorWindow(renderer, m_outlinerWindow);

	Entity* rootEntity = alloc<Entity>(ma, ma);
	g_truth->set(rootKey, rootEntity);

	for (int x = -10; x < 10; x++)
	{
		for (int y = -10; y < 10; y++)
		{
			Transaction tx = g_truth->openTransaction();
			for (int z = -10; z < 10; z++)
			{
				truth::Key childKey = nextKey();
				rootEntity->m_children.push_back(childKey);
				Entity* child = alloc<Entity>(ma, ma);
				child->position = { (float)x * 5.0f, (float)y * 5.0f, (float)z * 5.0f };
				g_truth->add(tx, childKey, child);
			}
			g_truth->commit(tx);
		}
	}

	m_lists[0].data = (Instance*)malloc(sizeof(Instance) * 1024);
	m_lists[1].data = (Instance*)malloc(sizeof(Instance) * 1024);
	m_lists[0].capacity = 1024;
	m_lists[1].capacity = 1024;

	addViewport("View 1");
	addViewport("View 2");
}

void Scene::update()
{
	ReadOnlySnapshot newHead = g_truth->head();
	if (state.s != newHead.s)
	{
		PROF_SCOPE(Update_Scene_Instances);
		{
			TempAllocator ta;
			Array<KeyEntry> adds(ta);
			Array<KeyEntry> removes(ta);
			Array<KeyEntry> edits(ta);

			diff(state.s, newHead.s, adds, edits, removes);

			for (i32 i = 0; i < adds.size(); ++i)
			{
				Entity* e = (Entity*)adds[i].value;
				addInstance(adds[i].key.asU64, e->position);
			}

			for (const KeyEntry& edit : edits)
			{
				Entity* i = (Entity*)edit.value;
				updateInstance(edit.key.asU64, i->position);
			}

			for (const KeyEntry& remove : removes)
			{
				popInstance(remove.key.asU64);
			}

			rebuildDrawList();

			state = newHead;
		}
	}
}

void Scene::addInstance(u64 key, float3 pos)
{
	m_instances.insert_or_assign(key, Instance{ {pos.x, pos.y, pos.z, 1.0f}, 0, key });
}

void Scene::updateInstance(u64 id, float3 pos)
{
	if (Instance* instance = m_instances.find(id))
	{
		instance->pos.xyz() = pos;
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
	scv->renderer = m_renderer;
	m_viewports.push_back(scv);
	::addViewport(m_renderer, scv);
}

void Scene::rebuildDrawList()
{
	PROF_SCOPE(rebuildDrawList);

	DrawList& drawList = m_lists[m_writeSlot];
	
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

	int oldReadSlot = m_readSlot;
	m_readSlot = m_writeSlot;
	m_writeSlot = oldReadSlot;
}

DrawList Scene::getDrawList()
{
	return m_lists[m_readSlot];
}

void SceneViewport::onGui()
{
	ImGui::Begin(name);
	ImVec2 textureCoords = ImVec2(-1, -1); // Default to invalid coords
	ImGui::Image(ViewportTex, ImVec2(size.x, size.y));

	if (ImGui::IsItemHovered()) {
        // Get the top-left corner of the image in screen space
        ImVec2 imagePos = ImGui::GetItemRectMin();

        // Get the current mouse position in screen space
        ImVec2 mousePos = ImGui::GetMousePos();

        // Calculate mouse position relative to the image (0,0 at top-left)
        textureCoords.x = mousePos.x - imagePos.x;
        textureCoords.y = mousePos.y - imagePos.y;
    }
	if (ImGui::IsItemClicked())
	{
		u64 id = readId(renderer, this, (u32)textureCoords.x, (u32)textureCoords.y);

		if (id != 0)
		{
			auto tx = g_truth->openTransaction();
			g_truth->erase(tx, truth::Key{ id });
			g_truth->commit(tx);
		}
	}

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

UndoStackWindow::UndoStackWindow(Truth* truth)
	:m_truth(truth)
{

}

void UndoStackWindow::onGui()
{
	ImGui::Begin("Undo Stack Window");

	i32 history_size = m_truth->undoUnits() - 1;
	i32 read_index = m_truth->getReadIndex();

	if (ImGui::Button("Zero"))
	{
		m_truth->setReadIndex(0);
	}
	ImGui::SameLine();
	if (ImGui::SliderInt("History", &read_index, 0, history_size))
	{
		m_truth->setReadIndex(read_index);
	}

	ImGui::End();
}

OutlinerWindow::OutlinerWindow(Truth* truth)
	:m_truth(truth)
{

}

void DrawEntityHierarchy(Truth* truth, ReadOnlySnapshot snap, truth::Key key)
{
	const Entity* entity = (const Entity*)truth->read(snap, key);

	if (!entity)
	{
		return;
	}

	ImGui::PushID(key.asU64);
	if (ImGui::TreeNode("Entity"))
	{
		float3 editPosition = entity->position;
		if (ImGui::DragFloat3("Position", &editPosition.x))
		{
			auto tx = truth->openTransaction();
			Entity* editEntity = (Entity*)truth->write(tx, key);
			editEntity->position = editPosition;
			truth->commit(tx);
		}

		for (truth::Key child : entity->m_children)
		{
			DrawEntityHierarchy(truth, snap, child);
		}

		ImGui::TreePop();
	}

	ImGui::PopID();
}

void OutlinerWindow::onGui()
{
	ReadOnlySnapshot snapshot = m_truth->head();

	DrawEntityHierarchy(m_truth, snapshot, rootKey);
}

DrawList SceneViewport::getDrawList()
{
	return scene->getDrawList();
}
