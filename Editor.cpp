#include "Editor.h"

#include <stdio.h>

#include "EditorRenderer.h"
#include "imgui.h"
#include "mh64.h"
#include "pch.h"
#include "Scene.h"
#include "Core/HashMap.h"

static EditorApp* s_app;

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

EditorTab* EditorTab::openEmpty(Allocator& a, const char* name, EditorRenderer* renderer)
{
    EditorTab* tab = create<EditorTab>(a, a, renderer);
    tab->m_name = name;
    return tab;
}

EditorTab* EditorTab::openExisting(Allocator& a, const char* name, truth::Key root, EditorRenderer* renderer)
{
    EditorTab* tab = create<EditorTab>(a, a, renderer);
    tab->m_name = name;
    return tab;
}

void EditorTab::save()
{

}

void EditorTab::update()
{
	buildDrawList();

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

    EditorViewport* vp = create<EditorViewport>(*GLOBAL_HEAP);
	vp->tab = this;
	vp->renderer = m_renderer;
	vp->id = s_nextViewportId++;
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

	for (Instance& instance : m_instances)
	{
		m_drawList.data[idx++] = instance;
	}
}

EditorTab::EditorTab(Allocator& a, EditorRenderer* renderer)
	: m_windows(a), m_viewports(a), m_instances(a)
{
    m_root = {};
    m_renderer = renderer;
    m_name = "";

	addViewport();

	addInstance(0, {0.0f, 0.0f, 0.0f});
	addInstance(1, {5.0f, 0.0f, 5.0f});
	addInstance(2, {-5.0f, 0.0f, -5.0f});

	OutlinerWindow* window = create<OutlinerWindow>(*GLOBAL_HEAP, g_truth, m_root);

	m_windows.push_back(window);
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

    EditorTab** focusedTab = m_openTabs.find(m_hFocusedTab);

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("   File   "))
        {

            if (ImGui::MenuItem("New")) 
            {
				static int nextDocuemntId = 0;

				int newDocumentId = nextDocuemntId++;

                m_openTabs[newDocumentId] = EditorTab::openEmpty(*GLOBAL_HEAP, "New", m_renderer);
                m_hFocusedTab = newDocumentId;
            }

            if (ImGui::MenuItem("Open")) 
            {

            }

            ImGui::BeginDisabled(focusedTab == nullptr);
            if (ImGui::MenuItem("Save")) 
            {
                (*focusedTab)->save();
            }
            ImGui::EndDisabled();

            ImGui::EndMenu();
        }


    	if (ImGui::BeginMenu("   Tabs   "))
		{
            for (auto& tab : m_openTabs)
            {
                ImGui::MenuItem("Some Tab");
            }
            ImGui::EndMenu();
        }

		if (ImGui::BeginMenu("   Scene   "))
		{
            if (focusedTab && ImGui::MenuItem("Add Viewport"))
            {
				(*focusedTab)->addViewport();
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

    if (focusedTab)
    {
        (*focusedTab)->update();
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

EditorApp::EditorApp(Allocator& a)
	:m_openTabs(a)
{
    m_renderer = nullptr;
    m_hFocusedTab = 0;

	g_truth = create<Truth>(*GLOBAL_HEAP, *GLOBAL_HEAP);
    
	u32 x = GetSystemMetrics(SM_CXSCREEN) - 60;
	u32 y = GetSystemMetrics(SM_CYSCREEN) - 60;

	m_hwnd = createWindow(x, y);

    initRenderer(m_hwnd, x, y, m_renderer);

    s_app = this;
    ShowWindow(m_hwnd, SW_SHOW);
	UpdateWindow(m_hwnd);
}
