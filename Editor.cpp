#include "Editor.h"

#include <stdio.h>

#include "EditorRenderer.h"
#include "imgui.h"
#include "pch.h"
#include "Scene.h"
#include "Core/HashMap.h"


#include "DynamicTypes.h"
#include "Core/TempAllocator.h"

#include "Random.h"

#include <windows.h>

#include "DynamicData.h"


static EditorApp* s_app;

DynamicValue s_clipboard;


truth::Key nextKey()
{
	truth::Key key;
	key.asU64 = Random_u64();
	return key;
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
		0, 5,
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
		u64 hovered_id = readId(renderer, this, (u32)textureCoords.x, (u32)textureCoords.y);
		lastHover = hovered_id;
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
    EditorTab* tab = new (a) EditorTab();

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
	OutlinerWindow* window = new (GLOBAL_HEAP) OutlinerWindow(g_truth, root);
	tab->m_windows.push_back(window);

    return tab;
}

EditorTab* EditorTab::openExisting(Allocator* a, const char* name, truth::Key, EditorRenderer*)
{
    EditorTab* tab = new (a) EditorTab();

	tab->m_instances.set_allocator(a);
	tab->m_viewports.set_allocator(a);
	tab->m_windows.set_allocator(a);

	sprintf_s(tab->m_name, "%s", name);
    return tab;
}

void EditorTab::save()
{

}

void EditorTab::update()
{
	ReadOnlySnapshot newHead = g_truth->head();

	if (m_state.s != newHead.s)
	{
		//const Entity* rootEntity = (const Entity*)g_truth->read(m_state, m_root);

		//TempAllocator ta;
		//Array<KeyEntry> adds(&ta);
		//Array<KeyEntry> removes(&ta);
		//Array<KeyEntry> edits(&ta);

		//diff(m_state.s, newHead.s, adds, edits, removes);

		//for (auto& add : adds)
		//{
		//	if (add.value->root == m_root)
		//	{
		//		float3 pos = get_position(newHead, add.key).float3();
		//		addInstance(add.key.asU64, pos);

		//		const Entity* added = (const Entity*)add.value;

		//		if (added->prototype.asU64 != 0)
		//		{
		//			const Entity* proto = (const Entity*)g_truth->read(newHead, added->prototype);

		//			for (truth::Key childKey : proto->children)
		//			{
		//				float3 cpos = get_position(newHead, childKey).float3();
		//				addInstance(childKey.asU64, cpos);
		//			}
		//		}
		//	}
		//	else if (isReferenced(rootEntity, add.value))
		//	{
		//		float3 pos = get_position(newHead, add.key).float3();
		//		addInstance(add.key.asU64, pos);
		//	}
		//}

		//for (const KeyEntry& edit : edits)
		//{
		//	if (edit.value->root == m_root)
		//	{
		//		constexpr float3 defaultColor =	{0.5f, 0.5f, 0.5f};
		//		float3 newPos = get_position(newHead, edit.key).float3();
		//		updateInstance(edit.key.asU64, newPos, defaultColor);
		//	}
		//	else if (isReferenced(rootEntity, edit.value))
		//	{
		//		constexpr float3 defaultColor =	{0.5f, 0.5f, 0.5f};

		//		auto instantiations = rootEntity->instantiatedRoots.find(edit.key.asU64);
		//		if (instantiations)
		//		{
		//			for (truth::Key instantiated : *instantiations)
		//			{
		//				float3 newPos = get_position(newHead, instantiated).float3();
		//				updateInstance(instantiated.asU64, newPos, defaultColor);
		//			}
		//		}

		//		updateInstance(edit.key.asU64, get_position(newHead, edit.key).float3(), defaultColor);
		//	}
		//}

		//for (const KeyEntry& remove : removes)
		//{
		//	if (remove.value->root == m_root || isReferenced(rootEntity, remove.value))
		//	{
		//		popInstance(remove.key.asU64);
		//		Entity* i = (Entity*)remove.value;
		//		for (auto& instantiated : i->instantiatedRoots)
		//		{
		//			
		//		}
		//	}
		//}

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

    EditorViewport* vp = new (GLOBAL_HEAP) EditorViewport();
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
	PostMessage(GetConsoleWindow(), WM_CLOSE, 0, 0);
}

void Debug_register_root_object(dd_id_t root)
{
	s_app->addRoot(root);
}

void Debug_register_component_type(i32 typeId)
{
	s_app->registerComponent(typeId);
}

Array<i32>* Debug_get_component_ids()
{
	return &s_app->m_componentTypes;
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

	DynamicData_view_draw_type_registry();



	ImGui::Begin("Object Roots");

	static char entity_name_buf[64];
	static bool entering_name = false;

	if (!entering_name && ImGui::Button("Add new root entity"))
	{
		entering_name = true;
	}

	if (entering_name)
	{
		if (ImGui::InputText("Entity name", entity_name_buf, 64, ImGuiInputTextFlags_EnterReturnsTrue))
		{
			dd_id_t entity = DynamicData_create_from_type_name(TM_STATIC_HASH("entity", 0x9831ca893b0d087dULL));
			dd_obj* entity_w = DynamicData_edit_object(entity);
			DynamicData_set_string(entity_w, TM_STATIC_HASH("name", 0xd4c943cba60c270bULL), entity_name_buf);
			m_roots.push_back(entity);
			entering_name = false;
			ZeroMemory(entity_name_buf, 64);
		}
		if (ImGui::Button("Cancel"))
		{
			entering_name = false;
			ZeroMemory(entity_name_buf, 64);
		}
	}

	int num = 0;
	for (dd_id_t root : m_roots)
	{
		ImGui::PushID((int)root.as_u64);
		if (ImGui::CollapsingHeader(Printf("Object Root %d", num++)))
		{
			DynamicData_view(root);
		}
		ImGui::PopID();
	}

	ImGui::End();

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
}

void EditorApp::onResize(u32 w, u32 h)
{
    onWindowResize(m_renderer, w, h);

    preRenderSync(m_renderer);
    update();
	renderFrame(m_renderer);
    present(m_renderer);
}


void EditorApp::addRoot(dd_id_t root)
{
	m_roots.push_back(root);
}

void EditorApp::registerComponent(i32 id)
{
	m_componentTypes.push_back(id);
}

Truth* g_truth;

EditorApp::EditorApp(Allocator* a)
{
	m_openTabs.set_allocator(a);
	m_assetWindow = new (a) AssetBrowserWindow();
    m_renderer = nullptr;
    m_hFocusedTab = 0;
	m_roots.set_allocator(a);
	m_componentTypes.set_allocator(a);

	g_truth = new (GLOBAL_HEAP) Truth(GLOBAL_HEAP);

	i32 screen_x = GetSystemMetrics(SM_CXSCREEN);
	i32 screen_y = GetSystemMetrics(SM_CYSCREEN);

	i32 main_window_x = screen_x / 2;
	i32 main_window_y = screen_y - 600;

	m_hwnd = createWindow(main_window_x, main_window_y);

	HWND consoleWindow = GetConsoleWindow();
	SetWindowPos(consoleWindow, HWND_TOP, 0, main_window_y, main_window_x, screen_y-main_window_y-45, SWP_SHOWWINDOW);

    initRenderer(m_hwnd, main_window_x, main_window_y, m_renderer);

    s_app = this;
    ShowWindow(m_hwnd, SW_SHOW);
	UpdateWindow(m_hwnd);
}
