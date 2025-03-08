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
		return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, m_position.x, m_position.y, m_position.z, 1};
	}
};

struct Constants
{
	matrix view;
	matrix projection;
	float3 lightvector;
};

struct EditorRenderer;

void renderFrame(EditorRenderer* rend, const SwissTable<Instance>& instances);

void initRenderer(HWND hwnd, u32 w, u32 h, EditorRenderer*& rend);
void createDevice(EditorRenderer* rend);
void createAssetResources(EditorRenderer* rend);
void createResources(EditorRenderer* rend, u32 w, u32 h);

void onWindowResize(EditorRenderer* rend, u32 w, u32 h);
void onDeviceLost(EditorRenderer* rend);