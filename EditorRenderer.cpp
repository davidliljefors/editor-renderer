#include "EditorRenderer.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <dxgi1_6.h>

#include "Core/Array.h"
#include "Math.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "Core/TempAllocator.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

struct Mesh
{
	ID3D11Buffer* vertexBuffer;
	ID3D11Buffer* indexBuffer;
	UINT indices;
};

struct alignas(16) GpuInstance
{
	float4 pos;
	float4 color;
};

struct alignas(16) PickingInstance
{
	float4 pos;
	u32 idHigh;
	u32 idLow;
	u32 padding[2];
};

struct alignas(16) CBufferCpu
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

struct Model
{
	ID3D11VertexShader* vertexShader;
	ID3D11PixelShader* pixelShader;
	ID3D11InputLayout* inputLayout;
	ID3D11SamplerState* samplerState;

    struct Picking
    {
	    ID3D11VertexShader* vertexShader;
		ID3D11PixelShader* pixelShader;
        ID3D11Buffer* instanceBuffer;
        ID3D11InputLayout* inputLayout;
    } picking;

	ID3D11Texture2D* texture;
	ID3D11ShaderResourceView* textureSrv;
	ID3D11Buffer* instanceBuffer;

	Mesh mesh;
};

static const int MAX_INSTANCES = 512;

struct ViewportData
{
    float2 txSize;
    ID3D11Texture2D* renderTargetTexture = nullptr;
	ID3D11RenderTargetView* renderTargetView = nullptr;
    ID3D11DepthStencilView* depthStencilView = nullptr;
    ID3D11ShaderResourceView* shaderResourceView = nullptr;

    ID3D11Texture2D* idRenderTargetTexture;
    ID3D11RenderTargetView* idRenderTargetView;
    ID3D11Texture2D* idStagingTexture;
    
    IViewport* usr;
};

struct EditorRenderer
{
	HWND hwnd = nullptr;
	IDXGISwapChain1* swapChain = nullptr;
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;

    ID3D11RenderTargetView* imGuirenderTargetView = nullptr;

	ID3D11Buffer* constantBuffer = nullptr;

	static const int MAX_MODELS = 1024;
	Model m_models[MAX_MODELS];
	int m_model_count;

	Instance m_instances[MAX_INSTANCES];
	int m_instance_count;

	Camera m_camera;

	bool m_right_mouse_down = false;
	POINT m_last_mouse_pos;
	float m_last_frame_time;

	UINT width;
	UINT height;

    UINT newWidth;
    UINT newHeight;

    ViewportData viewports[1024];
    u32 numViewports = 0;
};

#define TEXTURE_WIDTH  2
#define TEXTURE_HEIGHT 2

UINT texturedata[] = // 2x2 pixel checkerboard pattern, 0xAARRGGBB
{
    0xffffffff, 0xff7f7f7f,
    0xff7f7f7f, 0xffffffff,
};


void breakIfFailed(HRESULT hr, ID3D11Device* device)
{
	if (FAILED(hr))
	{
		// If we have a debug device, query the info queue for detailed messages
		if (device)
		{
			ID3D11InfoQueue* infoQueue = nullptr;
			HRESULT hrInfo = device->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&infoQueue);
			if (SUCCEEDED(hrInfo) && infoQueue)
			{
				UINT64 messageCount = infoQueue->GetNumStoredMessages();
				OutputDebugStringA("Debug Layer Messages:\n");

				for (UINT64 i = 0; i < messageCount; ++i)
				{
					SIZE_T messageLength = 0;
					infoQueue->GetMessage(i, nullptr, &messageLength);	// Get size first

					D3D11_MESSAGE* d3dMessage = (D3D11_MESSAGE*)malloc(messageLength);
					if (d3dMessage)
					{
						infoQueue->GetMessage(i, d3dMessage, &messageLength);
						OutputDebugStringA(d3dMessage->pDescription);
						OutputDebugStringA("\n");
						free(d3dMessage);
					}
				}
				infoQueue->Release();
			}
		}
		__debugbreak();
	}
}

void load_default_shaders(ID3D11Device* device, Model* model)
{
    HRESULT hr;
    ID3DBlob* p_vertex_shader_cso;
    ID3DBlob* p_error_blob = nullptr;
    hr = D3DCompileFromFile(TEXT("../../main.hlsl"), nullptr, nullptr, "VS_Main", "vs_5_0", 0, 0, &p_vertex_shader_cso, &p_error_blob);
    breakIfFailed(hr, device);

    if (p_error_blob)
    {
        OutputDebugStringA((char*)p_error_blob->GetBufferPointer());
        p_error_blob->Release();
    }

    hr = device->CreateVertexShader(p_vertex_shader_cso->GetBufferPointer(), p_vertex_shader_cso->GetBufferSize(), nullptr, &model->vertexShader);
    breakIfFailed(hr, device);

    D3D11_INPUT_ELEMENT_DESC input_element_desc[] =
    {
        { "POS", 0,             DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NOR", 0,             DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEX", 0,             DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COL", 0,             DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "INSTANCE_POS", 0,    DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "INSTANCE_COLOR", 0,  DXGI_FORMAT_R32G32B32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
    };

    hr = device->CreateInputLayout(input_element_desc, ARRAYSIZE(input_element_desc), p_vertex_shader_cso->GetBufferPointer(), p_vertex_shader_cso->GetBufferSize(), &model->inputLayout);
    breakIfFailed(hr, device);

    p_vertex_shader_cso->Release();

    ID3DBlob* p_pixel_shader_cso;
    hr = D3DCompileFromFile(TEXT("../../main.hlsl"), nullptr, nullptr, "PS_Main", "ps_5_0", 0, 0, &p_pixel_shader_cso, &p_error_blob);
    breakIfFailed(hr, device);

    if (p_error_blob)
    {
        OutputDebugStringA((char*)p_error_blob->GetBufferPointer());
        p_error_blob->Release();
    }

    hr = device->CreatePixelShader(p_pixel_shader_cso->GetBufferPointer(), p_pixel_shader_cso->GetBufferSize(), nullptr, &model->pixelShader);
    breakIfFailed(hr, device);

    p_pixel_shader_cso->Release();

    D3D11_SAMPLER_DESC sampler_desc = {};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;

    hr = device->CreateSamplerState(&sampler_desc, &model->samplerState);
    breakIfFailed(hr, device);

    D3D11_TEXTURE2D_DESC texture_desc = {};
    texture_desc.Width = TEXTURE_WIDTH;
    texture_desc.Height = TEXTURE_HEIGHT;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_IMMUTABLE;
    texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA texture_srd = {};
    texture_srd.pSysMem = texturedata;
    texture_srd.SysMemPitch = TEXTURE_WIDTH * sizeof(UINT);

    hr = device->CreateTexture2D(&texture_desc, &texture_srd, &model->texture);
    breakIfFailed(hr, device);

    hr =device->CreateShaderResourceView(model->texture, nullptr, &model->textureSrv);
    breakIfFailed(hr, device);

    GpuInstance initial_instance_data[MAX_INSTANCES] = {};

    D3D11_BUFFER_DESC instance_buffer_desc = {};
    instance_buffer_desc.ByteWidth = sizeof(GpuInstance) * MAX_INSTANCES;
    instance_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    instance_buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    instance_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    instance_buffer_desc.MiscFlags = 0;
    instance_buffer_desc.StructureByteStride = sizeof(GpuInstance);

    D3D11_SUBRESOURCE_DATA instance_buffer_data = {};
    instance_buffer_data.pSysMem = initial_instance_data;
    instance_buffer_data.SysMemPitch = 0;
    instance_buffer_data.SysMemSlicePitch = 0;

    hr = device->CreateBuffer(&instance_buffer_desc, &instance_buffer_data, &model->instanceBuffer);
    breakIfFailed(hr, device);
}

void load_picking_shaders(ID3D11Device* device, Model::Picking* picking)
{
    HRESULT hr;
    ID3DBlob* p_vertex_shader_cso;
    ID3DBlob* p_error_blob = nullptr;
    hr = D3DCompileFromFile(TEXT("../../picking.hlsl"), nullptr, nullptr, "VS_Picking", "vs_5_0", 0, 0, &p_vertex_shader_cso, &p_error_blob);
    breakIfFailed(hr, device);

    if (p_error_blob)
    {
        OutputDebugStringA((char*)p_error_blob->GetBufferPointer());
        p_error_blob->Release();
    }

    hr = device->CreateVertexShader(p_vertex_shader_cso->GetBufferPointer(), p_vertex_shader_cso->GetBufferSize(), nullptr, &picking->vertexShader);
    breakIfFailed(hr, device);

    ID3D11ShaderReflection* reflector;
	hr = D3DReflect(p_vertex_shader_cso->GetBufferPointer(), p_vertex_shader_cso->GetBufferSize(), __uuidof(ID3D11ShaderReflection), (void**)&reflector);
    breakIfFailed(hr, device);
	
    D3D11_SHADER_DESC desc;
	reflector->GetDesc(&desc);

	printf("Shader Inputs:\n");
	for (UINT i = 0; i < desc.InputParameters; i++) {
	    D3D11_SIGNATURE_PARAMETER_DESC param;
	    reflector->GetInputParameterDesc(i, &param);
	    printf("Input %u: Semantic=%s%d, Register=%u, ComponentType=%d, Mask=%u\n",
	           i, param.SemanticName, param.SemanticIndex, param.Register,
	           param.ComponentType, param.Mask);
}

    D3D11_INPUT_ELEMENT_DESC input_element_desc[] =
	{
		{ "POS", 0,             DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,      D3D11_INPUT_PER_VERTEX_DATA,    0 },
		{ "INSTANCE_POS", 0,    DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,   D3D11_INPUT_PER_INSTANCE_DATA,  1 },
		{ "IDHIGH", 0,          DXGI_FORMAT_R32_UINT, 1, 16,            D3D11_INPUT_PER_INSTANCE_DATA,  1 },
		{ "IDLOW", 0,           DXGI_FORMAT_R32_UINT, 1, 20,            D3D11_INPUT_PER_INSTANCE_DATA,  1 },
	};

    hr = device->CreateInputLayout(input_element_desc, ARRAYSIZE(input_element_desc), p_vertex_shader_cso->GetBufferPointer(), p_vertex_shader_cso->GetBufferSize(), &picking->inputLayout);
    breakIfFailed(hr, device);

    p_vertex_shader_cso->Release();

    ID3DBlob* p_pixel_shader_cso;
    hr = D3DCompileFromFile(TEXT("../../picking.hlsl"), nullptr, nullptr, "PS_Picking", "ps_5_0", 0, 0, &p_pixel_shader_cso, &p_error_blob);
    breakIfFailed(hr, device);

    if (p_error_blob)
    {
        OutputDebugStringA((char*)p_error_blob->GetBufferPointer());
        p_error_blob->Release();
    }

    hr = device->CreatePixelShader(p_pixel_shader_cso->GetBufferPointer(), p_pixel_shader_cso->GetBufferSize(), nullptr, &picking->pixelShader);
    breakIfFailed(hr, device);

    p_pixel_shader_cso->Release();

    PickingInstance initial_instance_data[MAX_INSTANCES];
    for (int i = 0; i < MAX_INSTANCES; i++)
    {
        initial_instance_data[i] = {};
    }

    D3D11_BUFFER_DESC instance_buffer_desc = {};
    instance_buffer_desc.ByteWidth = sizeof(PickingInstance) * MAX_INSTANCES;
    instance_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    instance_buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    instance_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    instance_buffer_desc.MiscFlags = 0;
    instance_buffer_desc.StructureByteStride = sizeof(PickingInstance);

    D3D11_SUBRESOURCE_DATA instance_buffer_data = {};
    instance_buffer_data.pSysMem = initial_instance_data;
    instance_buffer_data.SysMemPitch = 0;
    instance_buffer_data.SysMemSlicePitch = 0;

    hr = device->CreateBuffer(&instance_buffer_desc, &instance_buffer_data, &picking->instanceBuffer);
    breakIfFailed(hr, device);
}

inline void generate_sphere_mesh(ID3D11Device* device, Mesh* mesh, int latitude_count = 8, int longitude_count = 8)
{
    TempAllocator ta;
    
    Array<float> vertices(&ta);
    Array<UINT> indices(&ta);
    
    // Generate vertices
    for(int lat = 0; lat <= latitude_count; lat++) 
    {
        float theta = lat * 3.14159f / latitude_count;
        float sin_theta = sinf(theta);
        float cos_theta = cosf(theta);
        
        for(int lon = 0; lon <= longitude_count; lon++) 
        {
            float phi = lon * 2.0f * 3.14159f / longitude_count;
            float sin_phi = sinf(phi);
            float cos_phi = cosf(phi);
            
            float x = cos_phi * sin_theta;
            float y = cos_theta;
            float z = sin_phi * sin_theta;
            
            // Position
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            
            // Normal (same as position for sphere)
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            
            // Texcoord
            vertices.push_back(lon / (float)longitude_count);
            vertices.push_back(lat / (float)latitude_count);
            
            // Color (white)
            vertices.push_back(1.0f);
            vertices.push_back(1.0f);
            vertices.push_back(1.0f);
        }
    }
    
    for(int lat = 0; lat < latitude_count; lat++) 
    {
        for(int lon = 0; lon < longitude_count; lon++) 
        {
            int first = lat * (longitude_count + 1) + lon;
            int second = first + longitude_count + 1;
            
            // First triangle
            indices.push_back(first);
            indices.push_back(first + 1);
            indices.push_back(second);
            
            // Second triangle
            indices.push_back(second);
            indices.push_back(first + 1);
            indices.push_back(second + 1);
        }
    }
    
    D3D11_BUFFER_DESC vertex_buffer_desc = {};
    vertex_buffer_desc.ByteWidth = vertices.size() * sizeof(float);
    vertex_buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
    vertex_buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vertex_buffer_srd = {};
    vertex_buffer_srd.pSysMem = vertices.data();
    
    device->CreateBuffer(&vertex_buffer_desc, &vertex_buffer_srd, &mesh->vertexBuffer);
    
    D3D11_BUFFER_DESC index_buffer_desc = {};
    index_buffer_desc.ByteWidth = indices.size() * sizeof(UINT);
    index_buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
    index_buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA index_buffer_srd ={};
    index_buffer_srd.pSysMem = indices.data();
    
    device->CreateBuffer(&index_buffer_desc, &index_buffer_srd, &mesh->indexBuffer);
    
    mesh->indices = indices.size();
}


void initRenderer(HWND hwnd, u32 w, u32 h, EditorRenderer*& rend)
{
    if(!rend)
    {
        rend = (EditorRenderer*)malloc(sizeof(EditorRenderer));
        memset(rend, 0, sizeof(EditorRenderer));
        rend->hwnd = hwnd;
    }

    rend->newHeight = h;
    rend->newWidth = w;

    createDevice(rend);
    createAssetResources(rend);
    createResources(rend, w, h);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    //io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(rend->device, rend->context);

    ImGuiStyle& style = ImGui::GetStyle();
	auto& colors = style.Colors;

	style.Alpha = 1.0f;
	style.FrameRounding = 3.0f;
	style.ChildRounding = 3.0f;
	style.PopupRounding = 3.0f;
	style.WindowRounding = 3.0f;
	style.GrabRounding = 3.0f;
	style.GrabMinSize = 20.0f;
	style.TabRounding = 3.0f;

	colors[ImGuiCol_WindowBg] = { 0.11f, 0.10f, 0.10f, 1.0f };
	colors[ImGuiCol_NavCursor] = ImVec4(0.63f, 0.91f, 0.60f, 1.00f);

	colors[ImGuiCol_Header] = ImVec4(0.26f, 0.39f, 0.26f, 0.69f);
	colors[ImGuiCol_HeaderHovered] = { 0.4f, 0.5f, 0.4f, 1.0f };
	colors[ImGuiCol_HeaderActive] = { 0.5f, 0.6f, 0.5f, 1.0f };


	colors[ImGuiCol_Button] = { 0.3f, 0.4f, 0.3f, 1.0f };
	colors[ImGuiCol_ButtonActive] = { 0.4f, 0.5f, 0.4f, 1.0f };
	colors[ImGuiCol_ButtonHovered] = { 0.5f, 0.6f, 0.5f, 1.0f };
	colors[ImGuiCol_SliderGrab] = { 0.4f, 0.7f, 0.3f, 1.0f };
	colors[ImGuiCol_SliderGrabActive] = { 0.6f, 0.9f, 0.2f, 1.0f };
	colors[ImGuiCol_FrameBg] = { 0.22f, 0.33f, 0.22f, 1.0f };
	colors[ImGuiCol_FrameBgHovered] = { 0.33f, 0.44f, 0.33f, 1.0f };
	colors[ImGuiCol_FrameBgActive] = { 0.33f, 0.44f, 0.33f, 1.0f };
	colors[ImGuiCol_MenuBarBg] = { 0.08f, 0.08f, 0.08f, 1.0f };
	colors[ImGuiCol_HeaderActive] = { 0.08f, 0.08f, 0.08f, 1.0f };
	colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.53f, 0.84f, 0.56f, 0.78f);

	// radio
	colors[ImGuiCol_CheckMark] = { 0.6f, 0.9f, 0.2f, 1.0f };

	// floating window title
	colors[ImGuiCol_TitleBg] = { 0.22f, 0.28f, 0.24f, 1.0f };
	colors[ImGuiCol_TitleBgActive] = { 0.26f, 0.32f, 0.28f, 1.0f };
	colors[ImGuiCol_TitleBgCollapsed] = { 0.44f, 0.44f, 0.44f, 1.0f };

	// item size
	style.FramePadding = ImVec2(4.0f, 4.0f);

	// item padding
	style.ItemSpacing = { 4, 4 };
	style.ScrollbarSize = 20.0f;
	style.ScrollbarRounding = 3.0f;

	style.CellPadding = { 2, 3 };

    io.Fonts->AddFontFromFileTTF("NotoSans-Medium.ttf", 18.0f);
}

void createDevice(EditorRenderer* rend)
{
    UINT creationFlags = 0;

#ifdef _DEBUG
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    static const D3D_FEATURE_LEVEL featureLevels [] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1,
    };

    ID3D11Device* device;
    ID3D11DeviceContext* context;
    D3D_FEATURE_LEVEL featureLevel;
    D3D11CreateDevice(
        nullptr,                            // specify nullptr to use the default adapter
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        creationFlags,
        featureLevels,
        (UINT)(ARRAYSIZE(featureLevels)),
        D3D11_SDK_VERSION,
        &device,                            // returns the Direct3D device created
        &featureLevel,                      // returns feature level of device created
        &context                            // returns the device immediate context
    );
    rend->device = device;
    rend->context = context;


    HRESULT hr;

#ifndef NDEBUG
    ID3D11Debug* d3dDebug;
    hr = device->QueryInterface(__uuidof(ID3D11Debug), (void**)&d3dDebug);
    if (SUCCEEDED(hr))
    {
        ID3D11InfoQueue* d3dInfoQueue;
        hr = d3dDebug->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&d3dInfoQueue);
        if (SUCCEEDED(hr))
        {
#ifdef _DEBUG
            d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
            d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
#endif
            D3D11_MESSAGE_ID hide [] =
            {
                D3D11_MESSAGE_ID_SETPRIVATEDATA_CHANGINGPARAMS,
            };
            D3D11_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumIDs = ARRAYSIZE(hide);
            filter.DenyList.pIDList = hide;
            d3dInfoQueue->AddStorageFilterEntries(&filter);
        }
        if(d3dInfoQueue)
            d3dInfoQueue->Release();
    }
    if(d3dDebug)
        d3dDebug->Release();
#endif

    D3D11_BUFFER_DESC constantbufferdesc = {};
    constantbufferdesc.ByteWidth      = sizeof(CBufferCpu) + 0xf & 0xfffffff0;   // ensure constant buffer size is multiple of 16 bytes
    constantbufferdesc.Usage          = D3D11_USAGE_DYNAMIC;                    // will be updated from CPU every frame
    constantbufferdesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    constantbufferdesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = rend->device->CreateBuffer(&constantbufferdesc, nullptr, &rend->constantBuffer);
	breakIfFailed(hr, rend->device);
}

void createAssetResources(EditorRenderer* rend)
{
    rend->m_model_count = 0;
    rend->m_instance_count = 0;

    {
        Model& model = rend->m_models[rend->m_model_count++];
        load_default_shaders(rend->device, &model);
        load_picking_shaders(rend->device, &model.picking);
        generate_sphere_mesh(rend->device, &model.mesh);
    }
    
    rend->m_camera.m_position = float3{0.0f, 0.0f, -4.0f};
    rend->m_camera.m_yaw = 0.0f;
    rend->m_camera.m_pitch = 0.0f;
    rend->m_right_mouse_down = false;
    rend->m_last_frame_time = (float)GetTickCount64() / 1000.0f;
}

void destroyViewport(EditorRenderer*, ViewportData* data)
{
    if(data->renderTargetView)
        data->renderTargetView->Release();

    if(data->depthStencilView)
        data->depthStencilView->Release();

    if(data->shaderResourceView)
        data->shaderResourceView->Release();

    if(data->renderTargetTexture)
        data->renderTargetTexture->Release();

    if (data->idRenderTargetTexture)
        data->idRenderTargetTexture->Release();

    if (data->idRenderTargetView)
        data->idRenderTargetView->Release();

    if (data->idStagingTexture)
        data->idStagingTexture->Release();
}

void createViewportResources(EditorRenderer* rend, ViewportData* vp)
{
    DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = (u32)vp->usr->size.x;
    texDesc.Height = (u32)vp->usr->size.y;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr;
    
    hr = rend->device->CreateTexture2D(&texDesc, nullptr, &vp->renderTargetTexture);
    breakIfFailed(hr, rend->device);
    hr = rend->device->CreateRenderTargetView(vp->renderTargetTexture, nullptr, &vp->renderTargetView);
    breakIfFailed(hr, rend->device);
    hr =rend->device->CreateShaderResourceView(vp->renderTargetTexture, nullptr, &vp->shaderResourceView);
    breakIfFailed(hr, rend->device);

    CD3D11_TEXTURE2D_DESC depthStencilDesc(depthBufferFormat, UINT(vp->usr->size.x), UINT(vp->usr->size.y), 1, 1, D3D11_BIND_DEPTH_STENCIL);
    ID3D11Texture2D* depthStencil;
    hr = rend->device->CreateTexture2D(&depthStencilDesc, nullptr, &depthStencil);
    breakIfFailed(hr, rend->device);
    hr = rend->device->CreateDepthStencilView(depthStencil, nullptr, &vp->depthStencilView);
    breakIfFailed(hr, rend->device);
    depthStencil->Release();
    vp->txSize = vp->usr->size;
    vp->usr->ViewportTex = (u64)vp->shaderResourceView;


	D3D11_TEXTURE2D_DESC rtDesc = {};
	rtDesc.Width = (u32)vp->usr->size.x;
	rtDesc.Height = (u32)vp->usr->size.y;
	rtDesc.MipLevels = 1;
	rtDesc.ArraySize = 1;
	rtDesc.Format = DXGI_FORMAT_R32G32_UINT;
	rtDesc.SampleDesc.Count = 1;
	rtDesc.Usage = D3D11_USAGE_DEFAULT;
	rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

    hr = rend->device->CreateTexture2D(&rtDesc, nullptr, &vp->idRenderTargetTexture);
    breakIfFailed(hr, rend->device);
	hr = rend-> device->CreateRenderTargetView(vp->idRenderTargetTexture, nullptr, &vp->idRenderTargetView);
    breakIfFailed(hr, rend->device);

    D3D11_TEXTURE2D_DESC stagingDesc = rtDesc;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	hr = rend->device->CreateTexture2D(&stagingDesc, nullptr, &vp->idStagingTexture);
    breakIfFailed(hr, rend->device);
}

void createResources(EditorRenderer* rend, u32 w, u32 h)
{
    rend->context->OMSetRenderTargets(0, nullptr, nullptr);

    ID3D11ShaderResourceView* nullSRVs[1] = { nullptr };
    rend->context->VSSetShaderResources(0, 1, nullSRVs);
    rend->context->PSSetShaderResources(0, 1, nullSRVs);
    rend->context->CSSetShaderResources(0, 1, nullSRVs);

    for(u32 i = 0; i < rend->numViewports; ++i)
    {
        ViewportData* vp = &rend->viewports[i];
        destroyViewport(rend, vp);
    }

    if(rend->imGuirenderTargetView)
        rend->imGuirenderTargetView->Release();

    rend->context->Flush();

    DXGI_FORMAT backBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    u32 backBufferCount = 2;

    rend->width = w;
    rend->height = h;
    

    if (rend->swapChain)
    {
        HRESULT hr = rend->swapChain->ResizeBuffers(backBufferCount, w, h, backBufferFormat, 0);

        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
            // If the device was removed for any reason, a new device and swap chain will need to be created.
            onDeviceLost(rend);
            // Everything is set up now. Do not continue execution of this method. OnDeviceLost will reenter this method 
            // and correctly set up the new device.
            return;
        }
        else
        {
            breakIfFailed(hr, rend->device);
        }
    }
    else
    {
        // First, retrieve the underlying DXGI Device from the D3D Device.
        IDXGIDevice1* dxgiDevice;
        HRESULT hr = rend->device->QueryInterface(__uuidof(IDXGIDevice1), (void**)&dxgiDevice);
        if (hr != S_OK)
            return;

        // Identify the physical adapter (GPU or card) this device is running on.
        IDXGIAdapter* dxgiAdapter;
        hr = dxgiDevice->GetAdapter(&dxgiAdapter);
        if (hr != S_OK)
        {
            dxgiDevice->Release();
            return;
        }

        // And obtain the factory object that created it.
        IDXGIFactory2* dxgiFactory;
        hr = dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), (void**)&dxgiFactory);
        if (hr != S_OK)
        {
            dxgiAdapter->Release();
            dxgiDevice->Release();
            return;
        }

        // Create a descriptor for the swap chain.
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.Width = w;
        swapChainDesc.Height = h;
        swapChainDesc.Format = backBufferFormat;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = backBufferCount;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {};
        fsSwapChainDesc.Windowed = TRUE;

        // Create a SwapChain from a Win32 window.
        hr = dxgiFactory->CreateSwapChainForHwnd(
            rend->device,
            rend->hwnd,
            &swapChainDesc,
            &fsSwapChainDesc,
            nullptr,
            &rend->swapChain
            );
        if (hr != S_OK)
        {
            dxgiFactory->Release();
            dxgiAdapter->Release();
            dxgiDevice->Release();
            return;
        }

        dxgiFactory->Release();
        dxgiAdapter->Release();
        dxgiDevice->Release();
    }

    HRESULT hr;
    for(u32 i = 0; i < rend->numViewports; ++i)
    {
        ViewportData* vp = &rend->viewports[i];
        createViewportResources(rend, vp);
    }

    ID3D11Texture2D* backBuffer;
    rend->swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    hr = rend->device->CreateRenderTargetView(backBuffer, nullptr, &rend->imGuirenderTargetView);
    breakIfFailed(hr, rend->device);
    backBuffer->Release();
}

u64 readId(EditorRenderer* rend, IViewport* vp, u32 x, u32 y)
{
    ViewportData* vpd = nullptr;
    for (u32 i = 0; i < rend->numViewports; ++i)
    {
        if (rend->viewports[i].usr == vp)
        {
            vpd = &rend->viewports[i];
        }
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = rend->context->Map(vpd->idStagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
    breakIfFailed(hr, rend->device);

    struct uint2
    {
        u32 x;
        u32 y;
    };

    uint2* pixels = (uint2*)mapped.pData;
    UINT rowPitch = mapped.RowPitch / sizeof(uint2);
    uint2 idParts = pixels[y * rowPitch + x];
    uint64_t fullId = ((uint64_t)idParts.x << 32) | idParts.y;

    rend->context->Unmap(vpd->idStagingTexture, 0);

    return fullId;
}

void registerViewport(EditorRenderer* rend, IViewport* viewport)
{
    u32 slot = rend->numViewports;
    rend->numViewports += 1;
    rend->viewports[slot].usr = viewport;

    createViewportResources(rend, &rend->viewports[slot]);
}

void preRenderSync(EditorRenderer* rend)
{
    if(rend->width != rend->newWidth || rend->height != rend->newHeight)
    {
        createResources(rend, rend->newWidth, rend->newHeight);
    }

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) 
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    for(u32 i = 0; i < rend->numViewports; ++i)
    {
        ViewportData* vpd = &rend->viewports[i];
        if(vpd->txSize != vpd->usr->size)
        {
            destroyViewport(rend, vpd);
            createViewportResources(rend, vpd);
        }
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void renderFrame(EditorRenderer* rend)
{
    float current_time = (float)GetTickCount64() / 1000.0f;
    float delta_time = current_time - rend->m_last_frame_time;
    rend->m_last_frame_time = current_time;
    
    {
	    bool w = GetAsyncKeyState('W') & 0x8000;
	    bool a = GetAsyncKeyState('A') & 0x8000;
	    bool s = GetAsyncKeyState('S') & 0x8000;
	    bool d = GetAsyncKeyState('D') & 0x8000;
	    bool boost = GetAsyncKeyState(VK_LSHIFT) & 0x8000;
	    
	    rend->m_camera.update_movement(w, a, s, d, boost, delta_time);
    }
    
    FLOAT clearcolor[4] = { 0.015f, 0.015f, 0.315f, 1.0f };

    UINT stride[2] = { 11 * sizeof(float), sizeof(GpuInstance) };
    UINT idStride[2] = { 11 * sizeof(float), sizeof(PickingInstance) };
    UINT offset[2] = { 0, 0 };
   
    auto& context = rend->context;
    auto& constantbuffer = rend->constantBuffer;

    for(u32 vp = 0; vp<rend->numViewports; ++vp)
    {
        ViewportData* vpd = &rend->viewports[vp];
        D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)vpd->txSize.x, (float)vpd->txSize.y, 0.0f, 1.0f };
        float aspect_ratio = viewport.Width / viewport.Height;
        float fov_y = 3.14159f * 0.5f;
        float f = 1000.0f;
        float n = 0.1f;
        
        float tan_half_fov = tanf(fov_y * 0.5f);
        matrix proj = matrix{ 
            float4{1.0f / (aspect_ratio * tan_half_fov), 0, 0, 0},
            float4{0, 1.0f / tan_half_fov, 0, 0},
            float4{0, 0, f / (f - n), 1},
            float4{0, 0, -(n * f) / (f - n), 0}
        };

        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->OMSetRenderTargets(1, &vpd->renderTargetView, vpd->depthStencilView);
        context->RSSetViewports(1, &viewport);
        context->VSSetConstantBuffers(0, 1, &constantbuffer);

        context->ClearRenderTargetView(vpd->renderTargetView, clearcolor);
        context->ClearDepthStencilView(vpd->depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);
        context->OMSetBlendState(nullptr, nullptr, 0xffffffff);

        auto& model = rend->m_models[0];
        
        context->IASetInputLayout(model.inputLayout);
        ID3D11Buffer* buffers[2] = { model.mesh.vertexBuffer, model.instanceBuffer };
        context->IASetVertexBuffers(0, 2, buffers, stride, offset);
        context->IASetIndexBuffer(model.mesh.indexBuffer, DXGI_FORMAT_R32_UINT, 0);
        context->VSSetShader(model.vertexShader, nullptr, 0);
        context->PSSetShader(model.pixelShader, nullptr, 0);
        context->PSSetShaderResources(0, 1, &model.textureSrv);
        context->PSSetSamplers(0, 1, &model.samplerState);

        D3D11_MAPPED_SUBRESOURCE constantbufferMSR;
        context->Map(constantbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &constantbufferMSR);
        {
            CBufferCpu* constants = (CBufferCpu*)constantbufferMSR.pData;
            matrix view = vpd->usr->camera.get_view_matrix();
            constants->view = view;
            constants->projection = proj;
            constants->lightvector = float3{1.0f, -1.0f, 1.0f};
        }
        context->Unmap(constantbuffer, 0);

        DrawList list = vpd->usr->getDrawList();
        int instancesDrawn = 0;
        int count = int(list.count);

        while (instancesDrawn != count)
        {
            D3D11_MAPPED_SUBRESOURCE instanceBufferMSR;
            context->Map(model.instanceBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &instanceBufferMSR);
            {
                GpuInstance* instance_data = (GpuInstance*)instanceBufferMSR.pData;
                int batch_instance_count = 0;

                while (instancesDrawn != count && batch_instance_count < MAX_INSTANCES)
                {
                    const Instance& instance = list.data[instancesDrawn];
                    instance_data[batch_instance_count++] = GpuInstance{as_float4(instance.pos, 1.0f), as_float4(instance.color, 1.0f)};
                    ++instancesDrawn;
                }

                context->Unmap(model.instanceBuffer, 0);
                context->DrawIndexedInstanced(model.mesh.indices, batch_instance_count, 0, 0, 0);
            }
        }


        context->IASetInputLayout(model.picking.inputLayout);
        ID3D11Buffer* idBuffers[2] = { model.mesh.vertexBuffer, model.picking.instanceBuffer };
        context->IASetVertexBuffers(0, 2, idBuffers, idStride, offset);

        context->VSSetShader(model.picking.vertexShader, nullptr, 0);
        context->PSSetShader(model.picking.pixelShader, nullptr, 0);

		ID3D11ShaderResourceView* nullSRV = nullptr;
		context->PSSetShaderResources(0, 1, &nullSRV);
		ID3D11SamplerState* nullSampler = nullptr;
		context->PSSetSamplers(0, 1, &nullSampler);

        // draw picking content
        context->OMSetRenderTargets(1, &vpd->idRenderTargetView, vpd->depthStencilView);
        UINT idClearColor[2] = { 0, 0 };
        context->ClearRenderTargetView(vpd->idRenderTargetView, (float*)idClearColor);
        context->ClearDepthStencilView(vpd->depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

        instancesDrawn = 0;
        while (instancesDrawn != count)
        {
            D3D11_MAPPED_SUBRESOURCE instanceBufferMSR;
            context->Map(model.picking.instanceBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &instanceBufferMSR);
            {
                PickingInstance* instance_data = (PickingInstance*)instanceBufferMSR.pData;
                int batch_instance_count = 0;

                while (instancesDrawn != count && batch_instance_count < MAX_INSTANCES)
                {
                    const Instance& instance = list.data[instancesDrawn];
                    PickingInstance& pickingInstance = instance_data[batch_instance_count++];
                    pickingInstance.pos = as_float4(instance.pos, 1.0f);
                    pickingInstance.idHigh = (u32)(instance.key >> 32);
                    pickingInstance.idLow = (u32)(instance.key & 0xFFFFFFFF);
                    ++instancesDrawn;
                }

                context->Unmap(model.picking.instanceBuffer, 0);
                context->DrawIndexedInstanced(model.mesh.indices, batch_instance_count, 0, 0, 0);
            }
        }

        context->CopyResource(vpd->idStagingTexture, vpd->idRenderTargetTexture);
    }

    renderImgui(rend);
}

void renderImgui(EditorRenderer *rend)
{
    // Render ImGui to the swap chain
    rend->context->OMSetRenderTargets(1, &rend->imGuirenderTargetView, nullptr);
    float clearColor[] = { 0.2f, 0.2f, 0.2f, 1.0f }; // Dark gray for ImGui background
    rend->context->ClearRenderTargetView(rend->imGuirenderTargetView, clearColor);
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void present(EditorRenderer* rend)
{
    HRESULT hr = rend->swapChain->Present(1, 0);
    breakIfFailed(hr, rend->device);
}

void onWindowResize(EditorRenderer *rend, u32 w, u32 h)
{
    rend->newWidth = w;
    rend->newHeight = h;
}

void onDeviceLost(EditorRenderer *renderer)
{
    (void)renderer;
}