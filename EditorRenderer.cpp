#include "EditorRenderer.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <dxgi1_6.h>

#include "ScratchAllocator.h"
#include "Array.h"
#include "Math.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

struct Mesh
{
	ID3D11Buffer* m_p_vertex_buffer;
	ID3D11Buffer* m_p_index_buffer;
	UINT m_index_count;
};

struct Model
{
	ID3D11VertexShader* m_p_vertex_shader;
	ID3D11PixelShader* m_p_pixel_shader;
	ID3D11InputLayout* m_p_input_layout;
	ID3D11SamplerState* m_p_sampler_state;

	ID3D11Texture2D* m_p_texture;
	ID3D11ShaderResourceView* m_p_texture_srv;
	ID3D11Buffer* m_p_instance_buffer;

	Mesh m_mesh;
};

static const int MAX_INSTANCES = 512; // 16K instances per batch - good balance between memory and draw call overhead

struct EditorRenderer
{
	HWND hwnd = nullptr;
	IDXGISwapChain1* swapChain = nullptr;
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;
    
    ID3D11Texture2D* renderTargetTexture = nullptr;
	ID3D11RenderTargetView* renderTargetView = nullptr;
    ID3D11ShaderResourceView* shaderResourceView = nullptr;
	ID3D11DepthStencilView* depthStencilView = nullptr;

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
};



const char* g_shaderSrc = R"(
cbuffer constants : register(b0)
{
    row_major float4x4 view;
    row_major float4x4 projection;
    float3 lightvector;
    float3 lightcolor;          // Light color (e.g., float3(1, 1, 1) for white)
    float ambientStrength;      // Base illumination (e.g., 0.2)
    float specularStrength;     // Specular intensity (e.g., 0.5)
    float specularPower;        // Shininess (e.g., 32.0)
}

struct vertexdesc
{
    float3 position : POS;
    float3 normal : NOR;
    float2 texcoord : TEX;
    float3 color : COL;
};

struct instancedesc
{
    float4 position : INSTANCE_POS;
};

struct pixeldesc
{
    float4 position     : SV_POSITION;
    float2 texcoord     : TEX;
    float4 color        : COL;
    float3 worldnormal  : NORMAL;
};

Texture2D mytexture : register(t0);
SamplerState mysampler : register(s0);

pixeldesc vs_main(vertexdesc vertex, instancedesc instance)
{
    float4x4 model = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        instance.position.x, instance.position.y, instance.position.z, 1
    };
    
    float4 worldPos = mul(float4(vertex.position, 1.0f), model);
    float3 worldNormal = mul(vertex.normal, (float3x3)model);
    
    pixeldesc output;
    output.position = mul(worldPos, mul(view, projection));
    output.texcoord = vertex.texcoord;
    output.worldnormal = worldNormal;
    
    float light = clamp(dot(normalize(worldNormal), normalize(-lightvector)), 0.0f, 1.0f) * 0.8f;
    output.color = float4(vertex.color * light, 1.0f);

    return output;
}

float4 ps_main(pixeldesc pixel) : SV_TARGET
{
    float light = clamp(dot(normalize(pixel.worldnormal), normalize(-lightvector)), 0.0f, 1.0f) * 0.8f + 0.6f;
    return mytexture.Sample(mysampler, pixel.texcoord) * pixel.color * light;
}
)";

#define TEXTURE_WIDTH  2
#define TEXTURE_HEIGHT 2

UINT texturedata[] = // 2x2 pixel checkerboard pattern, 0xAARRGGBB
{
    0xffffffff, 0xff7f7f7f,
    0xff7f7f7f, 0xffffffff,
};

void load_default_shaders(ID3D11Device* p_device, Model* p_model)
{
    ID3DBlob* p_vertex_shader_cso;
    ID3DBlob* p_error_blob = nullptr;
    D3DCompile(g_shaderSrc, strlen(g_shaderSrc), nullptr, nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &p_vertex_shader_cso, &p_error_blob);

    if (p_error_blob)
    {
        OutputDebugStringA((char*)p_error_blob->GetBufferPointer());
        p_error_blob->Release();
    }

    p_device->CreateVertexShader(p_vertex_shader_cso->GetBufferPointer(), p_vertex_shader_cso->GetBufferSize(), nullptr, &p_model->m_p_vertex_shader);

    D3D11_INPUT_ELEMENT_DESC input_element_desc[] =
    {
        { "POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEX", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "INSTANCE_POS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
    };

    p_device->CreateInputLayout(input_element_desc, ARRAYSIZE(input_element_desc), p_vertex_shader_cso->GetBufferPointer(), p_vertex_shader_cso->GetBufferSize(), &p_model->m_p_input_layout);

    p_vertex_shader_cso->Release();

    ID3DBlob* p_pixel_shader_cso;
    D3DCompile(g_shaderSrc, strlen(g_shaderSrc), nullptr, nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &p_pixel_shader_cso, &p_error_blob);

    if (p_error_blob)
    {
        OutputDebugStringA((char*)p_error_blob->GetBufferPointer());
        p_error_blob->Release();
    }

    p_device->CreatePixelShader(p_pixel_shader_cso->GetBufferPointer(), p_pixel_shader_cso->GetBufferSize(), nullptr, &p_model->m_p_pixel_shader);

    p_pixel_shader_cso->Release();

    D3D11_SAMPLER_DESC sampler_desc = {};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;

    p_device->CreateSamplerState(&sampler_desc, &p_model->m_p_sampler_state);

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

    p_device->CreateTexture2D(&texture_desc, &texture_srd, &p_model->m_p_texture);
    p_device->CreateShaderResourceView(p_model->m_p_texture, nullptr, &p_model->m_p_texture_srv);

    float4 initial_instance_data[MAX_INSTANCES];
    for (int i = 0; i < MAX_INSTANCES; i++)
    {
        initial_instance_data[i] = float4{0.0f, 0.0f, 0.0f, 1.0f};
    }

    D3D11_BUFFER_DESC instance_buffer_desc = {};
    instance_buffer_desc.ByteWidth = sizeof(float4) * MAX_INSTANCES;
    instance_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    instance_buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    instance_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    instance_buffer_desc.MiscFlags = 0;
    instance_buffer_desc.StructureByteStride = sizeof(float4);

    D3D11_SUBRESOURCE_DATA instance_buffer_data = {};
    instance_buffer_data.pSysMem = initial_instance_data;
    instance_buffer_data.SysMemPitch = 0;
    instance_buffer_data.SysMemSlicePitch = 0;

    p_device->CreateBuffer(&instance_buffer_desc, &instance_buffer_data, &p_model->m_p_instance_buffer);
}

inline void generate_sphere_mesh(ID3D11Device* p_device, Mesh* p_mesh, int latitude_count = 8, int longitude_count = 8)
{
    ScratchPadAllocator ta;
    
    Array<float> vertices(ta);
    Array<UINT> indices(ta);
    
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
    
    p_device->CreateBuffer(&vertex_buffer_desc, &vertex_buffer_srd, &p_mesh->m_p_vertex_buffer);
    
    D3D11_BUFFER_DESC index_buffer_desc = {};
    index_buffer_desc.ByteWidth = indices.size() * sizeof(UINT);
    index_buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
    index_buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA index_buffer_srd ={};
    index_buffer_srd.pSysMem = indices.data();
    
    p_device->CreateBuffer(&index_buffer_desc, &index_buffer_srd, &p_mesh->m_p_index_buffer);
    
    p_mesh->m_index_count = indices.size();
}

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

void initRenderer(HWND hwnd, u32 w, u32 h, EditorRenderer*& rend)
{
    if(!rend)
    {
        rend = (EditorRenderer*)malloc(sizeof(EditorRenderer));
        memset(rend, 0, sizeof(EditorRenderer));
        rend->hwnd = hwnd;
    }

    createDevice(rend);
    createAssetResources(rend);
    createResources(rend, w, h);

    ImGui::CreateContext();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(rend->device, rend->context);
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


#ifndef NDEBUG
    ID3D11Debug* d3dDebug;
    HRESULT hr = device->QueryInterface(__uuidof(ID3D11Debug), (void**)&d3dDebug);
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
        generate_sphere_mesh(rend->device, &model.m_mesh);
    }
    
    rend->m_camera.m_position = float3{0.0f, 0.0f, -4.0f};
    rend->m_camera.m_yaw = 0.0f;
    rend->m_camera.m_pitch = 0.0f;
    rend->m_right_mouse_down = false;
    rend->m_last_frame_time = (float)GetTickCount64() / 1000.0f;
}

void createResources(EditorRenderer* rend, u32 w, u32 h)
{
    rend->context->OMSetRenderTargets(0, nullptr, nullptr);

    ID3D11ShaderResourceView* nullSRVs[1] = { nullptr };
    rend->context->VSSetShaderResources(0, 1, nullSRVs);
    rend->context->PSSetShaderResources(0, 1, nullSRVs);
    rend->context->CSSetShaderResources(0, 1, nullSRVs);

    if(rend->renderTargetView)
        rend->renderTargetView->Release();

    if(rend->depthStencilView)
        rend->depthStencilView->Release();

    if(rend->shaderResourceView)
        rend->shaderResourceView->Release();

    if(rend->renderTargetTexture)
        rend->renderTargetTexture->Release();

    if(rend->imGuirenderTargetView)
        rend->imGuirenderTargetView->Release();


    rend->context->Flush();

    DXGI_FORMAT backBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
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

    D3D11_TEXTURE2D_DESC texDesc = { 0 };
    texDesc.Width = w;
    texDesc.Height = h;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    auto hr = rend->device->CreateTexture2D(&texDesc, nullptr, &rend->renderTargetTexture);
    breakIfFailed(hr, rend->device);
    hr = rend->device->CreateRenderTargetView(rend->renderTargetTexture, nullptr, &rend->renderTargetView);
    breakIfFailed(hr, rend->device);
    hr =rend->device->CreateShaderResourceView(rend->renderTargetTexture, nullptr, &rend->shaderResourceView);
    breakIfFailed(hr, rend->device);

    ID3D11Texture2D* backBuffer;
    rend->swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    hr = rend->device->CreateRenderTargetView(backBuffer, nullptr, &rend->imGuirenderTargetView);
    breakIfFailed(hr, rend->device);
    backBuffer->Release();

    CD3D11_TEXTURE2D_DESC depthStencilDesc(depthBufferFormat, w, h, 1, 1, D3D11_BIND_DEPTH_STENCIL);
    ID3D11Texture2D* depthStencil;
    hr = rend->device->CreateTexture2D(&depthStencilDesc, nullptr, &depthStencil);
    breakIfFailed(hr, rend->device);
    hr = rend->device->CreateDepthStencilView(depthStencil, nullptr, &rend->depthStencilView);
    breakIfFailed(hr, rend->device);

    depthStencil->Release();
}

void preRender(EditorRenderer* )
{

}

void renderFrame(EditorRenderer* rend, const SwissTable<Instance>& instances)
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

    UINT stride[2] = { 11 * sizeof(float), sizeof(float4) };
    UINT offset[2] = { 0, 0 };

    D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)rend->width, (float)rend->height, 0.0f, 1.0f };
    
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
   
    auto& context = rend->context;
    auto& constantbuffer = rend->constantBuffer;

    context->ClearRenderTargetView(rend->renderTargetView, clearcolor);
    context->ClearDepthStencilView(rend->depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);
    context->RSSetViewports(1, &viewport);
    context->VSSetConstantBuffers(0, 1, &constantbuffer);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->OMSetRenderTargets(1, &rend->renderTargetView, rend->depthStencilView);
    context->OMSetBlendState(nullptr, nullptr, 0xffffffff);

    auto& model = rend->m_models[0];
    
    context->IASetInputLayout(model.m_p_input_layout);
    ID3D11Buffer* buffers[2] = { model.m_mesh.m_p_vertex_buffer, model.m_p_instance_buffer };
    context->IASetVertexBuffers(0, 2, buffers, stride, offset);
    context->IASetIndexBuffer(model.m_mesh.m_p_index_buffer, DXGI_FORMAT_R32_UINT, 0);
    context->VSSetShader(model.m_p_vertex_shader, nullptr, 0);
    context->PSSetShader(model.m_p_pixel_shader, nullptr, 0);
    context->PSSetShaderResources(0, 1, &model.m_p_texture_srv);
    context->PSSetSamplers(0, 1, &model.m_p_sampler_state);

    D3D11_MAPPED_SUBRESOURCE constantbufferMSR;
    context->Map(constantbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &constantbufferMSR);
    {
        CBufferCpu* constants = (CBufferCpu*)constantbufferMSR.pData;
        matrix view = rend->m_camera.get_view_matrix();
        constants->view = view;
        constants->projection = proj;
        constants->lightvector = float3{1.0f, -1.0f, 1.0f};
    }
    context->Unmap(constantbuffer, 0);

    auto it = instances.begin();
    auto end = instances.end();

    while (it != end)
    {
        D3D11_MAPPED_SUBRESOURCE instanceBufferMSR;
        context->Map(model.m_p_instance_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &instanceBufferMSR);
        {
            float4* instance_data = (float4*)instanceBufferMSR.pData;
            int batch_instance_count = 0;

            while (it != end && batch_instance_count < MAX_INSTANCES)
            {
                const Instance& instance = *it;
                instance_data[batch_instance_count++] = float4{instance.m_position.x, instance.m_position.y, instance.m_position.z, 1.0f};
                ++it;
            }

            context->Unmap(model.m_p_instance_buffer, 0);
            context->DrawIndexedInstanced(model.m_mesh.m_index_count, batch_instance_count, 0, 0, 0);
        }
    }

    renderImgui(rend);
}

void renderImgui(EditorRenderer *rend)
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Make ImGui cover the full window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(rend->width+20, rend->height+20));
    ImGui::Begin("FullScreenWindow", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Add some ImGui UI elements
    ImGui::Text("This is the full-screen ImGui background!");
    ImGui::Button("Example Button");

    // Display the DX11 render target in a sub-window
    ImGui::BeginChild("DX11Buffer", ImVec2(rend->width, rend->height), true);
    ImGui::Image((ImTextureID)rend->shaderResourceView, ImVec2(rend->width, rend->height));
    ImGui::EndChild();

    ImGui::End();

    // Render ImGui to the swap chain
    rend->context->OMSetRenderTargets(1, &rend->imGuirenderTargetView, nullptr);
    float clearColor[] = { 0.2f, 0.2f, 0.2f, 1.0f }; // Dark gray for ImGui background
    rend->context->ClearRenderTargetView(rend->imGuirenderTargetView, clearColor);
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    HRESULT hr = rend->swapChain->Present(1, 0);
    breakIfFailed(hr, rend->device);
}

void postRender(EditorRenderer*)
{

}

void onWindowResize(EditorRenderer *rend, u32 w, u32 h)
{
    createResources(rend, w, h);
}

void onDeviceLost(EditorRenderer *renderer)
{
    (void)renderer;
}