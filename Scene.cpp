#include "Scene.h"

#include <cstdio>
#include <stdlib.h>

#include "TempAllocator.h"
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

HeapAllocator ma;

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

	g_truth = create<Truth>(ma, ma);
	state = g_truth->head();

	m_undoWindow = create<UndoStackWindow>(ma, g_truth);
	m_outlinerWindow = create<OutlinerWindow>(ma, g_truth);
	addEditorWindow(renderer, m_undoWindow);
	addEditorWindow(renderer, m_outlinerWindow);

	Entity* rootEntity = create<Entity>(ma, ma);
	g_truth->set(rootKey, rootEntity);

	for (int x = -1; x < 2; x++)
	{
		for (int y = -1; y < 2; y++)
		{
			for (int z = -1; z < 2; z++)
			{
				Transaction tx = g_truth->openTransaction();
				truth::Key childKey = nextKey();
				rootEntity->m_children.push_back(childKey);
				Entity* child = create<Entity>(ma, ma);
				child->position = { (float)x * 5.0f, (float)y * 5.0f, (float)z * 5.0f };
				g_truth->add(tx, childKey, child);
				g_truth->commit(tx);
			}
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
				constexpr float3 defaultColor =		{0.5f, 0.5f, 0.5f};
				constexpr float3 hoverColor =		{1.0f, 1.0f, 0.7f};
				constexpr float3 selectedColor =	{1.0f, 1.0f, 1.0f};

				updateInstance(edit.key.asU64, i->position, i->isFlagSet(Selected) ? selectedColor : i->isFlagSet(Hovered) ? hoverColor : defaultColor);
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
	m_instances.insert_or_assign(key, Instance{ {pos.x, pos.y, pos.z}, {0.5f, 0.5f, 0.5f}, 0, key });
}

void Scene::updateInstance(u64 id, float3 pos, float3 color)
{
	if (Instance* instance = m_instances.find(id))
	{
		instance->pos = pos;
		instance->color = color;
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

	if (count > drawList.capacity)
	{
		drawList.data = (Instance*)realloc(drawList.data, sizeof(Instance) * count);
		drawList.capacity = count;
	}

	drawList.count = count;
	u64 idx = 0;

	for (Instance& instance : m_instances)
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
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0,0});

	ImGui::Begin(name);
	ImVec2 textureCoords = ImVec2(-1, -1);

	ImVec2 viewportSize = ImGui::GetContentRegionAvail();
	size.x = clamp(viewportSize.x, 1.0f, 4096.0f);
	size.y = clamp(viewportSize.y, 1.0f, 4096.0f);
	ImGui::Image(ViewportTex, ImVec2(size.x, size.y));

	if (ImGui::IsItemHovered()) 
	{
		ImVec2 imagePos = ImGui::GetItemRectMin();
		ImVec2 mousePos = ImGui::GetMousePos();

		textureCoords.x = mousePos.x - imagePos.x;
		textureCoords.y = mousePos.y - imagePos.y;
		u64 id = readId(renderer, this, (u32)textureCoords.x, (u32)textureCoords.y);
		if (lastHover != id && lastHover != 0)
		{
			auto tx = g_truth->openTransaction();
			Entity* oldHover = (Entity*)g_truth->write(tx, truth::Key{ lastHover });
			oldHover->setFlag(Hovered, false);
			g_truth->commit(tx);
		}
		if (id != 0 && lastHover != id)
		{
			auto tx = g_truth->openTransaction();
			Entity* newHover = (Entity*)g_truth->write(tx, truth::Key{ id });
			newHover->setFlag(Hovered, true);
			g_truth->commit(tx);
		}

		lastHover = id;

		if (ImGui::IsItemClicked())
		{
			if (id != 0)
			{
				auto tx = g_truth->openTransaction();
				Entity* entity = (Entity*)g_truth->write(tx, truth::Key{ id });
				entity->setFlag(EntityFlag::Selected, true);
				g_truth->commit(tx);
			}
		}
	}


	ImGuiIO& io = ImGui::GetIO();
	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1))
	{
		if (!dragging)
		{
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

	if (!ImGui::IsMouseDown(1) && dragging) 
	{
		dragging = false;
	}

	if (ImGui::IsItemHovered())
	{
		bool w = ImGui::IsKeyDown(ImGuiKey_W);
		bool s = ImGui::IsKeyDown(ImGuiKey_S);
		bool a = ImGui::IsKeyDown(ImGuiKey_A);
		bool d = ImGui::IsKeyDown(ImGuiKey_D);
		bool boost = ImGui::IsKeyDown(ImGuiKey_LeftShift);

		camera.update_movement(w, a, s, d, boost, io.DeltaTime);
	}


	ImGui::End();

	ImGui::PopStyleVar();
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

static char nameBuffer[256] = "";
static bool isRenaming = false;

void DoEntityContextMenu(Truth* truth, truth::Key key, const Entity* entity) {

	if (ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Rename"))
		{
			strncpy_s(nameBuffer, entity->m_name.data(), sizeof(nameBuffer) - 1);
			nameBuffer[sizeof(nameBuffer) - 1] = '\0';
			isRenaming = true;
			ImGui::OpenPopup("Rename Entity");
		}
		ImGui::EndPopup();
	}

	if (isRenaming) 
	{
		ImGui::SetNextWindowSize(ImVec2(300, 150), ImGuiCond_Appearing);

		if (ImGui::BeginPopupModal("Rename Entity", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) 
		{
			ImGui::Text("Enter new name:");
			ImGui::InputText("##Name", nameBuffer, sizeof(nameBuffer));

			if (isRenaming && ImGui::IsWindowAppearing())
			{
				ImGui::SetKeyboardFocusHere(-1); // Focus on the InputText
			}

			if (ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::IsItemDeactivated())
			{
				auto tx = truth->openTransaction();
				Entity* writing = (Entity*)truth->write(tx, key);
				alloc_str(writing->m_name, nameBuffer); // Update the entity name

				truth->commit(tx);
				isRenaming = false;
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::Button("OK", ImVec2(120, 0)))
			{
				auto tx = truth->openTransaction();
				Entity* writing = (Entity*)truth->write(tx, key);

				alloc_str(writing->m_name, nameBuffer); // Update the entity name
				truth->commit(tx);
				isRenaming = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("Cancel", ImVec2(120, 0))) 
			{
				isRenaming = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}
}

void DrawEntityHierarchy(Truth* truth, ReadOnlySnapshot snap, truth::Key key)
{
	const Entity* entity = (const Entity*)truth->read(snap, key);

	if (!entity)
	{
		return;
	}

	ImGui::PushID(key.asU64);
	if (ImGui::TreeNode(entity->m_name.begin()))
	{
		DoEntityContextMenu(truth, key, entity);

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
