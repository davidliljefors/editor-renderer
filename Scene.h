#pragma once

#include "EditorRenderer.h"
#include "Array.h"
#include "Math.h"
#include "HashTable.h"


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

	u64 addInstance(float3 pos);
	void updateInstance(u64 id, float3 pos);
	void popInstance(u64 instanceId);

	void addViewport(const char* name);

	DrawList getDrawList();

private:
	EditorRenderer* m_renderer;
	Hashtable<Instance> m_instances;
	Array<SceneViewport*> m_viewports;
	u64 m_nextInstance = 0;
	DrawList m_lists[2];
	int writeSlot = 0;
};