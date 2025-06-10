#pragma once

#include "EditorRenderer.h"
#include "Array.h"
#include "Math.h"
#include "HashMap.h"
#include "TruthMap.h"

class Truth;
struct EditorRenderer;
struct EditorMesh;
struct Camera;
struct SceneInstance;
struct Instance;

class Scene;

struct SceneViewport : IViewport
{
	virtual DrawList getDrawList();
	virtual void onGui();

	const char* name;
	Scene* scene;
	EditorRenderer* renderer;
	bool dragging = false;
};

class UndoStackWindow : public IEditorWindow
{
public:
	UndoStackWindow(Truth* truth);
	void onGui() override;

	Truth* m_truth;
};

class OutlinerWindow : public IEditorWindow
{
public:
	OutlinerWindow(Truth* truth);
	void onGui() override;

	Truth* m_truth;
	truth::Key m_selected;
};

class Scene
{

public:
	Scene(EditorRenderer* renderer);

	void update();

	void addInstance(u64 id, float3 pos);
	void updateInstance(u64 id, float3 pos);
	void popInstance(u64 id);

	void addViewport(const char* name);

	void rebuildDrawList();
	DrawList getDrawList();

private:
	ReadOnlySnapshot state;
	EditorRenderer* m_renderer;
	UndoStackWindow* m_undoWindow;
	OutlinerWindow* m_outlinerWindow;
	Array<SceneViewport*> m_viewports;
	HashMap<Instance> m_instances;
	DrawList m_lists[2];
	int m_writeSlot = 0;
	int m_readSlot = 1;
};