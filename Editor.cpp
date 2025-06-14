#include "Editor.h"

#include <stdio.h>

#include "EditorRenderer.h"
#include "imgui.h"
#include "mh64.h"
#include "pch.h"
#include "Scene.h"
#include "Core/HashMap.h"

#include <wincrypt.h>

#include "Entity.h"
#include "Core/TempAllocator.h"

#pragma comment(lib, "advapi32.lib")

static EditorApp* s_app;

struct Xoshiro256
{
	static uint64_t rotl(const uint64_t x, int k) 
	{
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

truth::Key nextKey()
{
	truth::Key key;
	key.asU64 = g_rand.next();
	return key;
}

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

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM uint, LPARAM long_);

LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
	{
		return 0;
	}

    if (msg == WM_CLOSE)
    {
        PostQuitMessage(0);
    }

	if (msg == WM_SIZE)
	{
		UINT width = LOWORD(lParam);
		UINT height = HIWORD(lParam);
		
		s_app->onResize(width, height);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

HWND createWindow(int w, int h)
{
    WNDCLASSEX wc = {};
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.lpfnWndProc = EditorWndProc;
	wc.hInstance = GetModuleHandle(nullptr);
	wc.lpszClassName = TEXT("EditorRendererClass");
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	RegisterClassEx(&wc);

    DWORD windowStyle = WS_OVERLAPPEDWINDOW;
	DWORD windowExStyle = WS_EX_APPWINDOW;

    HWND hwnd = CreateWindowExA(
		windowExStyle,
		"EditorRendererClass",
		"Editor Renderer",
		windowStyle,
		0, 0,
		w, h,
		nullptr,
		nullptr,
		GetModuleHandle(nullptr),
		nullptr
	);

    return hwnd;
}

DrawList EditorViewport::getDrawList()
{
	return tab->getDrawList();
}

void EditorViewport::update()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0,0});

	char buf[64];
	sprintf_s(buf, "Viewport id %llu", id);

	ImGui::SetNextWindowSize({400, 400}, ImGuiCond_FirstUseEver);

	ImGui::Begin(buf);
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
		lastHover = id;
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

EditorTab* EditorTab::openEmpty(Allocator* a, EditorRenderer* renderer, i32 id)
{
    EditorTab* tab = alloc<EditorTab>(a);

	tab->m_instances.set_allocator(a);
	tab->m_viewports.set_allocator(a);
	tab->m_windows.set_allocator(a);


	tab->m_state = g_truth->head();
	tab->m_renderer = renderer;

	sprintf_s(tab->m_name, "New (%d)", id);

	tab->addViewport();

	truth::Key root = nextKey();
	AssetBrowserWindow::registerRoot(root);

	tab->m_root = root;
	Allocator* ta = g_truth->allocator();
	Entity* rootEntity = Entity::create(ta);
	rootEntity->root = root;

	g_truth->set(tab->m_root, rootEntity);

	OutlinerWindow* window = create<OutlinerWindow>(GLOBAL_HEAP, g_truth, root);
	tab->m_windows.push_back(window);

    return tab;
}

EditorTab* EditorTab::openExisting(Allocator* a, const char* name, truth::Key root, EditorRenderer* renderer)
{
    EditorTab* tab = alloc<EditorTab>(a);

	tab->m_instances.set_allocator(a);
	tab->m_viewports.set_allocator(a);
	tab->m_windows.set_allocator(a);

	sprintf_s(tab->m_name, "%s", name);
    return tab;
}

void EditorTab::save()
{

}

bool isReferenced(const Entity* referee, const TruthObject* candidate)
{
	return referee->instantiatedRoots.contains(candidate->root.asU64);
}

void EditorTab::update()
{
	ReadOnlySnapshot newHead = g_truth->head();

	if (m_state.s != newHead.s)
	{
		const Entity* rootEntity = (const Entity*)g_truth->read(m_state, m_root);

		TempAllocator ta;
		Array<KeyEntry> adds(&ta);
		Array<KeyEntry> removes(&ta);
		Array<KeyEntry> edits(&ta);

		diff(m_state.s, newHead.s, adds, edits, removes);


		for (auto& add : adds)
		{
			if (add.value->root == m_root)
			{
				float3 pos = get_position(newHead, add.key).float3();
				addInstance(add.key.asU64, pos);

				const Entity* added = (const Entity*)add.value;

				if (added->prototype.asU64 != 0)
				{
					const Entity* proto = (const Entity*)g_truth->read(newHead, added->prototype);

					for (truth::Key childKey : proto->children)
					{
						float3 cpos = get_position(newHead, childKey).float3();
						addInstance(childKey.asU64, cpos);
					}
				}
			}
			else if (isReferenced(rootEntity, add.value))
			{
				float3 pos = get_position(newHead, add.key).float3();
				addInstance(add.key.asU64, pos);
			}
		}

		for (const KeyEntry& edit : edits)
		{
			if (edit.value->root == m_root)
			{
				constexpr float3 defaultColor =	{0.5f, 0.5f, 0.5f};
				float3 newPos = get_position(newHead, edit.key).float3();
				updateInstance(edit.key.asU64, newPos, defaultColor);
			}
			else if (isReferenced(rootEntity, edit.value))
			{
				constexpr float3 defaultColor =	{0.5f, 0.5f, 0.5f};

				auto instantiations = rootEntity->instantiatedRoots.find(edit.key.asU64);
				if (instantiations)
				{
					for (truth::Key instantiated : *instantiations)
					{
						float3 newPos = get_position(newHead, instantiated).float3();
						updateInstance(instantiated.asU64, newPos, defaultColor);
					}
				}

				updateInstance(edit.key.asU64, get_position(newHead, edit.key).float3(), defaultColor);
			}
		}

		for (const KeyEntry& remove : removes)
		{
			if (remove.value->root == m_root || isReferenced(rootEntity, remove.value))
			{
				popInstance(remove.key.asU64);
				Entity* i = (Entity*)remove.value;
				for (auto& instantiated : i->instantiatedRoots)
				{
					
				}
			}
		}

		buildDrawList();

		m_state = newHead;
	}

    for (EditorViewport* vp : m_viewports)
    {
        vp->update();
    }

    for (IEditorWindow* w : m_windows)
    {
		ImGui::Begin("Outliner");
        w->update();
		ImGui::End();
    }
}

void EditorTab::addViewport()
{
	static u64 s_nextViewportId = 0;

    EditorViewport* vp = create<EditorViewport>(GLOBAL_HEAP);
	vp->tab = this;
	vp->renderer = m_renderer;
	vp->id = s_nextViewportId++;
	vp->camera.m_position.x = -15;
	vp->camera.m_position.y = 3;
	vp->camera.m_position.z = -15;
	vp->camera.m_yaw = 0.8f;
	vp->camera.m_pitch = 0.25f;

	registerViewport(m_renderer, vp);

	m_viewports.push_back(vp);
}

DrawList EditorTab::getDrawList()
{
	return m_drawList;
}

void addPrototypeInstances(Transaction& tx, Entity* instantiated, const Entity* prototype)
{
	for (truth::Key childKey : prototype->children)
	{
	}
}

void EditorTab::addPrototype(truth::Key parent, truth::Key prototype)
{
	const Entity* prototypeEntity = (const Entity*)g_truth->read(m_state, prototype);

	if (prototypeEntity->root == m_root)
	{
		return;
	}

	Transaction tx = g_truth->openTransaction();
	Entity* parentEntity = (Entity*)g_truth->edit(tx, parent);

	Entity* instantiatedPrototype = Entity::createFromPrototype(g_truth->allocator(), prototype);
	instantiatedPrototype->root = m_root;

	truth::Key instantiatedPrototypeId = nextKey();

	Array<truth::Key>* ids = parentEntity->instantiatedRoots.find(prototype.asU64);
	
	if (!ids)
	{
		ids = &parentEntity->instantiatedRoots[prototype.asU64];
		ids->set_allocator(g_truth->allocator());
	}

	ids->push_back(instantiatedPrototypeId);
	parentEntity->children.push_back(instantiatedPrototypeId);

	//addPrototypeInstances(instantiatedPrototype, prototypeEntity);

	g_truth->add(tx, instantiatedPrototypeId, instantiatedPrototype);

	g_truth->commit(tx);
}

void EditorTab::instantiatePrototypeChild(truth::Key parent, truth::Key child)
{
	const Entity* parentEntity = (const Entity*)g_truth->read(m_state, parent);

	if (parentEntity->prototype.asU64 == 0)
	{
		return;
	}

	Transaction tx = g_truth->openTransaction();

	parentEntity->

}

void EditorTab::addEntity(ReadOnlySnapshot from, ReadOnlySnapshot to, const KeyEntry* entity)
{
	if (add.value->root == m_root)
	{
		float3 pos = get_position(newHead, add.key).float3();
		addInstance(add.key.asU64, pos);

		const Entity* added = (const Entity*)add.value;

		if (added->prototype.asU64 != 0)
		{
			const Entity* proto = (const Entity*)g_truth->read(newHead, added->prototype);

			for (truth::Key childKey : proto->children)
			{
				float3 cpos = get_position(newHead, childKey).float3();
				addInstance(childKey.asU64, cpos);
			}
		}
	}
}

void EditorTab::updateEntity(const KeyEntry* entity)
{

}

void EditorTab::popEntity(const KeyEntry* entity)
{

}

void EditorTab::addInstance(u64 id, float3 pos)
{
	m_instances.insert_or_assign(id, Instance{ {pos.x, pos.y, pos.z}, {0.5f, 0.5f, 0.5f}, 0, id });
}

void EditorTab::updateInstance(u64 id, float3 pos, float3 color)
{
	if (Instance* instance = m_instances.find(id))
	{
		instance->pos = pos;
		instance->color = color;
	}
}

void EditorTab::popInstance(u64 id)
{
	m_instances.erase(id);
}

void EditorTab::buildDrawList()
{
	i32 count = m_instances.size();

	if (count > m_drawList.capacity)
	{
		m_drawList.data = (Instance*)realloc(m_drawList.data, sizeof(Instance) * count);
		m_drawList.capacity = count;
	}

	m_drawList.count = count;
	u64 idx = 0;

	for (auto& entry : m_instances)
	{
		m_drawList.data[idx++] = entry.value;
	}
}

void EditorApp::run()
{
    MSG msg = {};
	bool running = true;
	while (running)
	{
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				running = false;
			}
			if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
			{
				running = false;
				break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		preRenderSync(m_renderer);
        update();
		renderFrame(m_renderer);
        present(m_renderer);
	}
}

void EditorApp::update()
{
	ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags window_flags = 
        ImGuiWindowFlags_NoTitleBar | 
        ImGuiWindowFlags_NoResize | 
        ImGuiWindowFlags_NoMove | 
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::Begin("MainWindow", nullptr, window_flags);


    EditorTab** focusedTabFind = m_openTabs.find(m_hFocusedTab);
	EditorTab* focusedTab = focusedTabFind ? *focusedTabFind : nullptr;

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("   File   "))
        {

            if (ImGui::MenuItem("New")) 
            {
				static int nextId = 0;

				int newId = nextId++;

                m_openTabs[newId] = EditorTab::openEmpty(GLOBAL_HEAP, m_renderer, newId);
                m_hFocusedTab = newId;
            }

            if (ImGui::MenuItem("Open")) 
            {

            }

            ImGui::BeginDisabled(focusedTab == nullptr);
            if (ImGui::MenuItem("Save")) 
            {
                focusedTab->save();
            }
            ImGui::EndDisabled();

            ImGui::EndMenu();
        }


    	if (ImGui::BeginMenu("   Tabs   "))
		{
			for (i32 i = 0; i<m_openTabs.size(); ++i)
			{
				char buf[64];
				sprintf_s(buf, "%s", m_openTabs.data()[i].value->m_name);
                if (ImGui::MenuItem(buf))
                {
					m_hFocusedTab = m_openTabs.data()[i].key;
				}
			}
            ImGui::EndMenu();
        }

		if (ImGui::BeginMenu("   Scene   "))
		{
            if (focusedTab && ImGui::MenuItem("Add Viewport"))
            {
				focusedTab->addViewport();
			}
            ImGui::EndMenu();
        }
        
        ImGui::EndMenuBar();
    }

    ImVec2 available_size = ImGui::GetContentRegionAvail();

    ImGui::BeginChild("MainContent", available_size, true);
    {
        ImGuiID dockspace_id = ImGui::GetID("MainContentDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
    }
    ImGui::EndChild();


	ImGui::End();

	truth::Key clicked{};
	m_assetWindow->update(&clicked);

    if (focusedTab)
    {
		if (clicked.asU64 != 0)
		{
			focusedTab->addPrototype(focusedTab->m_root, clicked);
		}

        focusedTab->update();
    }
}

void EditorApp::onResize(u32 w, u32 h)
{
    onWindowResize(m_renderer, w, h);

    preRenderSync(m_renderer);
    update();
	renderFrame(m_renderer);
    present(m_renderer);
}

Truth* g_truth;

EditorApp::EditorApp(Allocator* a)
{
	InitializeRandomContext(&g_rand);
	m_openTabs.set_allocator(a);
	m_assetWindow = create<AssetBrowserWindow>(a);
    m_renderer = nullptr;
    m_hFocusedTab = 0;

	g_truth = create<Truth>(GLOBAL_HEAP, GLOBAL_HEAP);
    
	i32 x = GetSystemMetrics(SM_CXSCREEN) - 60;
	i32 y = GetSystemMetrics(SM_CYSCREEN) - 60;

	m_hwnd = createWindow(x, y);

    initRenderer(m_hwnd, x, y, m_renderer);

    s_app = this;
    ShowWindow(m_hwnd, SW_SHOW);
	UpdateWindow(m_hwnd);
}
