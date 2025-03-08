#pragma once

#include "SwissTable.h"
#include "Math.h"

struct EditorRenderer;
struct EditorMesh;
struct Camera;
struct SceneInstance;
struct Instance;

class Scene
{

public:
	Scene(EditorRenderer* renderer);

	void updateThread();
	void renderThread();

	u64 addInstance(float3 pos);
	void updateInstance(u64 id, float3 pos);
	void popInstance(u64 instanceId);

private:
	EditorRenderer* m_renderer;
	SwissTable<Instance> m_instances;
	u64 m_nextInstance = 0;
};