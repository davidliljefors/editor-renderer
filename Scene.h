#pragma once

#include "Editor.h"
#include "Core/Types.h"
#include "EditorRenderer.h"
#include "Core/Array.h"
#include "Math.h"
#include "Core/HashMap.h"
#include "TruthMap.h"

class Truth;
struct EditorRenderer;
struct EditorMesh;
struct Camera;
struct SceneInstance;
struct Instance;

//inline i32 min(i32 a, i32 min)
//{
//	return a < min ? a : min;
//}
//
//inline i32 max(i32 a, i32 max)
//{
//	return a > max ? a : max;
//}
//
//inline f32 min(f32 a, f32 min)
//{
//	return a < min ? a : min;
//}
//
//inline f32 max(f32 a, f32 max)
//{
//	return a > max ? a : max;
//}
//
//	
//class Scene;
//
//struct SceneViewport : IViewport
//{
//	DrawList getDrawList() override;
//	void update() override;
//
//	const char* name;
//	Scene* scene;
//	EditorRenderer* renderer;
//	bool dragging = false;
//	u64 lastHover = 0;
//};
//
//class UndoStackWindow : public IEditorWindow
//{
//public:
//	UndoStackWindow(Truth* truth);
//	void update() override;
//
//	Truth* m_truth;
//};

class OutlinerWindow : public IEditorWindow
{
public:
	OutlinerWindow(Truth* truth, truth::Key root);
	void update() override;

	Truth* m_truth;
	truth::Key m_root;
	truth::Key m_selected;
};


class AssetBrowserWindow
{
public:
	AssetBrowserWindow();

	void update(truth::Key* outClicked);
	static void registerRoot(truth::Key id);
	Array<truth::Key> roots;
};

//
//class Scene
//{
//
//public:
//	Scene(EditorRenderer* renderer);
//
//	void update();
//
//	void addInstance(u64 id, float3 pos);
//	void updateInstance(u64 id, float3 pos, float3 color);
//	void popInstance(u64 id);
//
//	void addViewport(const char* name);
//
//	void rebuildDrawList();
//	DrawList getDrawList();
//
//private:
//	ReadOnlySnapshot state;
//	EditorRenderer* m_renderer;
//	UndoStackWindow* m_undoWindow;
//	OutlinerWindow* m_outlinerWindow;
//	Array<SceneViewport*> m_viewports;
//	HashMap<Instance> m_instances;
//	DrawList m_lists[2];
//	int m_writeSlot = 0;
//	int m_readSlot = 1;
//};