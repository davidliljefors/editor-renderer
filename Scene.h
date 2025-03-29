#pragma once

#include "EditorRenderer.h"
#include "Array.h"
#include "Math.h"
#include "HashMap.h"
#include "TruthMap.h"

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

	bool dragging = false;
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

	DrawList getDrawList();

private:
	TruthMap* state = nullptr;
	EditorRenderer* m_renderer;
	Array<SceneViewport*> m_viewports;
	HashMap<Instance> m_instances;
	DrawList m_lists[2];
	int writeSlot = 0;
};