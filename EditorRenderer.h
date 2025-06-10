#pragma once

#include "Math.h"
#include "Core/Types.h"

struct HWND__;
using HWND = HWND__*;

struct Instance
{
	float3 pos;
	float3 color;
	int model_id;
	u64 key;

	matrix get_model_matrix() const
	{
		return matrix{
			float4{1, 0, 0, 0},
			float4{0, 1, 0, 0},
			float4{0, 0, 1, 0},
			float4{pos.x, pos.y, pos.z, 1}
		};
	}
};

struct DrawList
{
	Instance* data = nullptr;
	u64 count = 0;
	u64 capacity = 0;
};

struct EditorRenderer;

class IViewport
{
public:
	virtual ~IViewport() = default;
	virtual DrawList getDrawList() = 0;
	
	virtual void update() = 0;

	Camera camera;
	bool open = true;
	u64 ViewportTex;
	float2 size = {100, 100};
};

u64 readId(EditorRenderer* rend, IViewport* vp, u32 x, u32 y);

void registerViewport(EditorRenderer* rend, IViewport* viewport);

void preRenderSync(EditorRenderer* rend);
void renderFrame(EditorRenderer* rend);
void renderImgui(EditorRenderer* rend);
void present(EditorRenderer* rend);

void initRenderer(HWND hwnd, u32 w, u32 h, EditorRenderer*& rend);

void createDevice(EditorRenderer* rend);
void createAssetResources(EditorRenderer* rend);
void createResources(EditorRenderer* rend, u32 w, u32 h);

void onWindowResize(EditorRenderer* rend, u32 w, u32 h);
void onDeviceLost(EditorRenderer* rend);