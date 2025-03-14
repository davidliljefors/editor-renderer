#pragma once

#include "Math.h"
#include "SwissTable.h"

struct HWND__;
using HWND = HWND__*;

struct Instance
{
	float3 m_position;
	int m_model_id;

	matrix get_model_matrix() const
	{
		return matrix{
			float4{1, 0, 0, 0},
			float4{0, 1, 0, 0},
			float4{0, 0, 1, 0},
			float4{m_position.x, m_position.y, m_position.z, 1}
		};
	}
};

struct DrawList
{
	Instance* data = nullptr;
	u64 count = 0;
	u64 capacity = 0;
};

struct CBufferCpu
{
	matrix view;
	matrix projection;
	
	float3 lightvector;
	float _pad0;
	
	float3 lightColor;
	float _pad1;
	

	float ambientStr;
	float specularStr;
	float specularPow;

	float _pad2;
};

struct EditorRenderer;

class IViewport
{
public:
	virtual DrawList getDrawList() = 0;
	virtual void onGui() = 0;

	Camera camera;
	bool open = true;
	u64 ViewportTex;
	float2 size = {100, 100};
};

void addViewport(EditorRenderer* rend, IViewport* viewport);
void preRenderSync(EditorRenderer* rend);
void preRender(EditorRenderer* rend);
void renderFrame(EditorRenderer* rend);
void renderImgui(EditorRenderer* rend);
void postRender(EditorRenderer* rend);
void renderSynchronize(EditorRenderer* rend);

void initRenderer(HWND hwnd, u32 w, u32 h, EditorRenderer*& rend);
void createDevice(EditorRenderer* rend);
void createAssetResources(EditorRenderer* rend);
void createResources(EditorRenderer* rend, u32 w, u32 h);

void onWindowResize(EditorRenderer* rend, u32 w, u32 h);
void onDeviceLost(EditorRenderer* rend);