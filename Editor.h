#pragma once


#include "Core/Array.h"
#include "EditorRenderer.h"
#include "TruthMap.h"
#include "TruthView.h"
#include "Core/HashMap.h"

class EditorTab;

class IEditorWindow
{
public:
	virtual ~IEditorWindow() = default;

	virtual void update() = 0;
};

struct EditorViewport : IViewport
{
	DrawList getDrawList() override;
	void update() override;

	EditorTab* tab = nullptr;
	EditorRenderer* renderer = nullptr;
	bool dragging = false;
	u64 lastHover = 0;
	u64 id = 0;
};

class EditorTab
{
public:
	EditorTab(Allocator& a, EditorRenderer* renderer);

	static EditorTab* openEmpty(Allocator& a, const char* name, EditorRenderer* renderer);
	static EditorTab* openExisting(Allocator& a, const char* name, truth::Key root, EditorRenderer* renderer);

	void save();
	void update();

	void addViewport();
	DrawList getDrawList();


	void addInstance(u64 id, float3 pos);
	void updateInstance(u64 id, float3 pos, float3 color);
	void popInstance(u64 id);

	const char* name() const { return m_name; }

private:
	void buildDrawList();

	EditorRenderer* m_renderer;
	Array<IEditorWindow*> m_windows;
	Array<EditorViewport*> m_viewports;

	HashMap<Instance> m_instances;
	DrawList m_drawList;

	const char* m_name;
	truth::Key m_root;
};

class EditorApp
{
public:
	EditorApp(Allocator& a);

	void run();

	void update();

	void onResize(u32 w, u32 h);

private:
	EditorRenderer* m_renderer;
	HashMap<EditorTab*> m_openTabs;
	u64 m_hFocusedTab;

	HWND m_hwnd;
};

extern Truth* g_truth;

void registerWindow(IEditorWindow* window);

void editorUpdate();