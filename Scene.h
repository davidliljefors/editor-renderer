#pragma once

#include "SwissTable.h"
#include "Math.h"

struct EditorRenderer;
struct EditorMesh;
struct Camera;
struct SceneInstance;
struct Instance;

struct DrawList
{
	Instance* data = nullptr;
	u64 size = 0;
	u64 cap = 0;
};

class Scene
{

public:
	Scene(EditorRenderer* renderer);

	u64 addInstance(float3 pos);
	void updateInstance(u64 id, float3 pos);
	void popInstance(u64 instanceId);


	void buildDrawList();
	bool getDrawList(DrawList& outDrawList);

private:
	EditorRenderer* m_renderer;

	SwissTable<Instance> m_instances;
	u64 m_nextInstance = 0;
	DrawList m_lists[2];
	int writeSlot;
	int readSlot;
};