#include "Scene.h"

#include <cstdio>
#include <stdlib.h>

#include <windows.h>
#include <wincrypt.h>

#include "ScratchAllocator.h"
#include "EditorRenderer.h"
#include "imgui.h"
#include "TruthView.h"

#include "yyjson.h"

#include "ppltasks.h"

#pragma comment(lib, "advapi32.lib")

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

int InitializeRandomContext(Xoshiro256* rand)
{
	HCRYPTPROV hCryptProv = 0;

	if (!CryptAcquireContext(
		&hCryptProv,
		NULL,
		NULL,
		PROV_RSA_FULL,
		CRYPT_VERIFYCONTEXT))
	{
		return 1;
	}

	if (!CryptGenRandom(hCryptProv, sizeof(Xoshiro256), reinterpret_cast<u8*>(rand)))
	{
		CryptReleaseContext(hCryptProv, 0);
		return 1;
	}

	if (!CryptReleaseContext(hCryptProv, 0))
	{
		return 1;
	}

	return 0;
}

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

const char* kRootEntityKey = "root_entity";
const char* kEntitiesKey = "entities";
const char* kEntityIdKey = "entity_id";
const char* kEntityNameKey = "entity_name";
const char* kChildrenKey = "children";
const char* kPositionKey = "position";
const char* kColorKey = "color";

void writeEntity(yyjson_mut_doc* jDoc, yyjson_mut_val* jArray, Truth* truth, ReadOnlySnapshot snap, truth::Key key)
{
	const Entity* entity = (const Entity*)truth->read(snap, key);

	if (entity)
	{
		yyjson_mut_val* jObject = yyjson_mut_arr_add_obj(jDoc, jArray);
		yyjson_mut_obj_add_uint(jDoc, jObject, kEntityIdKey, key.asU64);
		yyjson_mut_obj_add_str(jDoc, jObject, kEntityNameKey, entity->getName());

		yyjson_mut_val* jChildren = yyjson_mut_obj_add_arr(jDoc, jObject, kChildrenKey);
		for (truth::Key child : entity->m_children)
		{
			yyjson_mut_arr_add_uint(jDoc, jChildren, child.asU64);
		}

		yyjson_mut_val* jPosition = yyjson_mut_obj_add_arr(jDoc, jObject, kPositionKey);
		yyjson_mut_arr_add_float(jDoc, jPosition, entity->position.x);
		yyjson_mut_arr_add_float(jDoc, jPosition, entity->position.y);
		yyjson_mut_arr_add_float(jDoc, jPosition, entity->position.z);

		yyjson_mut_val* jColor = yyjson_mut_obj_add_arr(jDoc, jObject, kColorKey);
		yyjson_mut_arr_add_float(jDoc, jColor, entity->color.x);
		yyjson_mut_arr_add_float(jDoc, jColor, entity->color.y);
		yyjson_mut_arr_add_float(jDoc, jColor, entity->color.z);

		for (truth::Key child : entity->m_children)
		{
			writeEntity(jDoc, jArray, truth, snap, child);
		}
	}
}

void writeJsonFile(Truth* truth, ReadOnlySnapshot snap, truth::Key rootNode)
{
	yyjson_mut_doc* jDoc = yyjson_mut_doc_new(nullptr);
	yyjson_mut_val* jRoot = yyjson_mut_obj(jDoc);
	yyjson_mut_doc_set_root(jDoc, jRoot);

	yyjson_mut_obj_add_uint(jDoc, jRoot, kRootEntityKey, rootNode.asU64);
	yyjson_mut_val* jArray = yyjson_mut_obj_add_arr(jDoc, jRoot, kEntitiesKey);
	writeEntity(jDoc, jArray, truth, snap, rootNode);


	yyjson_write_flag flg = YYJSON_WRITE_PRETTY | YYJSON_WRITE_ESCAPE_UNICODE;
	yyjson_write_err err;
	yyjson_mut_write_file("level.json", jDoc, flg, nullptr, &err);

	if (err.code) 
	{
		printf("write error (%u): %s\n", err.code, err.msg);
	}

	yyjson_mut_doc_free(jDoc);
}


float3 readFloat3(yyjson_val* val)
{
	float3 value{};
	if (yyjson_is_arr(val))
	{
		if (yyjson_arr_size(val) == 3)
		{
			yyjson_val* xval = yyjson_arr_get(val, 0);
			yyjson_val* yval = yyjson_arr_get(val, 1);
			yyjson_val* zval = yyjson_arr_get(val, 2);

			value.x = (float)(yyjson_is_real(xval) ? yyjson_get_real(xval) : 0.0);
			value.y = (float)(yyjson_is_real(yval) ? yyjson_get_real(yval) : 0.0);
			value.z = (float)(yyjson_is_real(zval) ? yyjson_get_real(zval) : 0.0);
		}
	}
	return value;
}

void loadJsonFile(Truth* truth)
{
	yyjson_read_flag flg = YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS;
	yyjson_read_err err;
	yyjson_doc* jDoc = yyjson_read_file("level.json", flg, nullptr, &err);

	Transaction tx = truth->openTransaction();
	Allocator& a = truth->allocator();

	if (jDoc)
	{
		yyjson_val* jRoot = yyjson_doc_get_root(jDoc);

		yyjson_val* jArray = yyjson_obj_get(jRoot, kEntitiesKey);
		yyjson_arr_iter iter = yyjson_arr_iter_with(jArray);
		yyjson_val* jEntity;
		while ((jEntity = yyjson_arr_iter_next(&iter)))
		{
			if (yyjson_is_obj(jEntity))
			{
				u64 entityId = yyjson_get_uint(yyjson_obj_get(jEntity, kEntityIdKey));
				Entity* entity = alloc<Entity>(a, a);
				entity->setName(yyjson_get_str(yyjson_obj_get(jEntity, kEntityNameKey)));
				entity->position = readFloat3(yyjson_obj_get(jEntity, kPositionKey));
				entity->color = readFloat3(yyjson_obj_get(jEntity, kColorKey));

				yyjson_val* jChildren = yyjson_obj_get(jEntity, kChildrenKey);
				u64 childCount = yyjson_arr_size(jChildren);
				entity->m_children.reserve((i32)childCount);

				for (u64 y = 0; y < childCount; ++y)
				{
					yyjson_val* jChild = yyjson_arr_get(jChildren, y);
					u64 childId = yyjson_get_uint(jChild);

					entity->m_children.push_back(truth::Key{childId});
				}

				truth->add(tx, truth::Key{entityId}, entity);
			}
		}

		truth->commit(tx);
	}
}

void save()
{
	concurrency::create_task([]
	{
		writeJsonFile(g_truth, g_truth->snap(), rootKey);
	});
}

void load()
{
	concurrency::create_task([]
	{
		loadJsonFile(g_truth);
	});

}

Scene::Scene(EditorRenderer* renderer)
	: m_viewports(ma)
	, m_instances(ma)
{
	PROF_SCOPE(Scene_Constructor);

	m_renderer = renderer;

	InitializeRandomContext(&g_rand);

	g_truth = alloc<Truth>(ma, ma);
	state = g_truth->head();

	m_undoWindow = alloc<UndoStackWindow>(ma, g_truth);
	m_outlinerWindow = alloc<OutlinerWindow>(ma, g_truth);
	addEditorWindow(renderer, m_undoWindow);
	addEditorWindow(renderer, m_outlinerWindow);

	Entity* rootEntity = alloc<Entity>(ma, ma);
	rootEntity->position = {3.0f, 3.0f, 3.0f};
	g_truth->set(rootKey, rootEntity);

	Transaction tx = g_truth->openTransaction();
	for (int x = -30; x < 30; x++)
	{
		for (int y = -30; y < 30; y++)
		{
			for (int z = -30; z < 30; z++)
			{
				truth::Key childKey = nextKey();
				rootEntity->m_children.push_back(childKey);
				Entity* child = alloc<Entity>(ma, ma);
				child->position = { (float)x * 5.0f, (float)y * 5.0f, (float)z * 5.0f };
				g_truth->add(tx, childKey, child);
			}
		}
	}

	g_truth->commit(tx);
	g_truth->dropHistory();

	m_lists[0].data = (Instance*)malloc(sizeof(Instance) * 1024);
	m_lists[1].data = (Instance*)malloc(sizeof(Instance) * 1024);
	m_lists[0].capacity = 1024;
	m_lists[1].capacity = 1024;

	addViewport("View 1");
}

void Scene::update()
{
	if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
	{
		if (ImGui::GetIO().KeyShift)
		{
			g_truth->redo();
		}
		g_truth->undo();
	}

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

void recursiveErase(Transaction& tx, truth::Key id)
{
	const Entity* e = (const Entity*)g_truth->read(tx.base, id);
	if (e)
	{
		g_truth->erase(tx, id);
		for (truth::Key childKey : e->m_children)
		{
			recursiveErase(tx, childKey);
		}
	}
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
			recursiveErase(tx, truth::Key{id});
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

void DrawEntityHierarchy(Truth* truth, ReadOnlySnapshot snap, truth::Key key, truth::Key* selected)
{
	const Entity* entity = (const Entity*)truth->read(snap, key);

	if (!entity)
	{
		return;
	}

	ImGui::PushID(key.asU64);

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
	if (entity->m_children.empty())
	{
		flags |= ImGuiTreeNodeFlags_Leaf;
	}

	if (key == *selected)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}

	char buf[64];
	if (entity->m_children.empty())
	{
		sprintf_s(buf, "%s", entity->getName());
	}
	else
	{
		sprintf_s(buf, "%s [%d]", entity->getName(), entity->m_children.size());
	}
	if (ImGui::TreeNodeEx(buf, flags))
	{
		if (ImGui::IsItemClicked())
		{
			*selected = key;
		}

		for (truth::Key child : entity->m_children)
		{
			DrawEntityHierarchy(truth, snap, child, selected);
		}

		ImGui::TreePop();
	}

	ImGui::PopID();
}

void OutlinerWindow::onGui()
{
	ReadOnlySnapshot snapshot = m_truth->head();

	ImGui::Begin("Outliner");
	ImGui::Text("Outliner");
	DrawEntityHierarchy(m_truth, snapshot, rootKey, &m_selected);
	ImGui::End();

	ImGui::Begin("Inspector");
	ImGui::Text("Inspector");
	const TruthElement* selectedElement = m_truth->read(snapshot, m_selected);
	if (selectedElement)
	{
		if (selectedElement->typeId() == Entity::kTypeId)
		{
			const Entity* e = (const Entity*)selectedElement;
			float3 value = e->position;
			ImGui::DragFloat3("Entity Position", &value.x);
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				Transaction tx = m_truth->openTransaction();
				Entity* writable = (Entity*)m_truth->write(tx, m_selected);
				writable->position = value;
				m_truth->commit(tx);
			}
		}
	}
	ImGui::End();
}

DrawList SceneViewport::getDrawList()
{
	return scene->getDrawList();
}
