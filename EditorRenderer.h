#pragma once

#include "Math.h"

struct HWND__;
using HWND = HWND__*;

struct Win32Timer
{
	Win32Timer(const char* name);
	~Win32Timer();

	u64 freq;
	u64 start;
	const char* name;
};

void save();
void load();

#define PROF_SCOPE(name) Win32Timer timer_##name(#name)

struct Instance
{
	float4 pos;
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

struct alignas(16) PickingInstance
{
	float4 pos;
	u32 idHigh;
	u32 idLow;
	u32 padding[2];
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
	virtual ~IViewport() = default;
	virtual DrawList getDrawList() = 0;
	virtual void onGui() = 0;

	Camera camera {{-15.0f, 0.0f, -5.0f}, 45.0f, 0.0f};
	bool open = true;
	u64 ViewportTex;
	float2 size = {100, 100};
};

class IEditorWindow
{
public:
	virtual ~IEditorWindow() = default;

	virtual void onGui() = 0;
};

void addViewport(EditorRenderer* rend, IViewport* viewport);
void addEditorWindow(EditorRenderer* rend, IEditorWindow* window);

u64 readId(EditorRenderer* rend, IViewport* vp, u32 x, u32 y);

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