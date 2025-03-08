#include "EditorRenderer.h"

#include <windows.h>
#include <comdef.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include "Math.h"
#include <dxgi1_6.h>

#include <vector>

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

	Mesh m_mesh;
};

struct EditorRenderer
{
	HWND hwnd = nullptr;
	IDXGISwapChain1* swapChain = nullptr;
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;
    
	ID3D11RenderTargetView* renderTargetView;
	ID3D11DepthStencilView* depthStencilView;

	ID3D11Buffer* constantBuffer = nullptr;

	static const int MAX_MODELS = 1024;
	Model m_models[MAX_MODELS];
	int m_model_count;

	static const int MAX_INSTANCES = 1024;
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
    row_major float4x4 model;
    row_major float4x4 view;
    row_major float4x4 projection;
    float3 lightvector;
}

struct vertexdesc
{
    float3 position : POS;
    float3 normal : NOR;
    float2 texcoord : TEX;
    float3 color : COL;
};

struct pixeldesc
{
    float4 position : SV_POSITION;
    float2 texcoord : TEX;
    float4 color : COL;
};

Texture2D mytexture : register(t0);
SamplerState mysampler : register(s0);

pixeldesc vs_main(vertexdesc vertex)
{
    float4 worldPos = mul(float4(vertex.position, 1.0f), model);
    float3 worldNormal = mul(vertex.normal, (float3x3)model);
    
    float light = clamp(dot(normalize(worldNormal), normalize(-lightvector)), 0.0f, 1.0f) * 0.8f + 0.2f;
    
    pixeldesc output;
    output.position = mul(worldPos, mul(view, projection));
    output.texcoord = vertex.texcoord;
    output.color = float4(vertex.color * light, 1.0f);

    return output;
}

float4 ps_main(pixeldesc pixel) : SV_TARGET
{
    return mytexture.Sample(mysampler, pixel.texcoord) * pixel.color;
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

    p_device->CreateVertexShader(p_vertex_shader_cso->GetBufferPointer(), p_vertex_shader_cso->GetBufferSize(), nullptr, &p_model->m_p_vertex_shader);

    D3D11_INPUT_ELEMENT_DESC input_element_desc[] =
    {
        { "POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEX", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    p_device->CreateInputLayout(input_element_desc, ARRAYSIZE(input_element_desc), p_vertex_shader_cso->GetBufferPointer(), p_vertex_shader_cso->GetBufferSize(), &p_model->m_p_input_layout);

    ID3DBlob* p_pixel_shader_cso;
    D3DCompile(g_shaderSrc, strlen(g_shaderSrc), nullptr, nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &p_pixel_shader_cso, &p_error_blob);

    p_device->CreatePixelShader(p_pixel_shader_cso->GetBufferPointer(), p_pixel_shader_cso->GetBufferSize(), nullptr, &p_model->m_p_pixel_shader);

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
}

inline void generate_sphere_mesh(ID3D11Device* p_device, Mesh* p_mesh, int latitude_count = 8, int longitude_count = 8)
{
    std::vector<float> vertices;
    std::vector<UINT> indices;
    
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
    D3D11_SUBRESOURCE_DATA vertex_buffer_srd = { vertices.data() };
    
    p_device->CreateBuffer(&vertex_buffer_desc, &vertex_buffer_srd, &p_mesh->m_p_vertex_buffer);
    
    D3D11_BUFFER_DESC index_buffer_desc = {};
    index_buffer_desc.ByteWidth = indices.size() * sizeof(UINT);
    index_buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
    index_buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA index_buffer_srd = { indices.data() };
    
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
        d3dInfoQueue->Release();
    }
    d3dDebug->Release();
#endif

    D3D11_BUFFER_DESC constantbufferdesc = {};
    constantbufferdesc.ByteWidth      = sizeof(Constants) + 0xf & 0xfffffff0;   // ensure constant buffer size is multiple of 16 bytes
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

    // Obtain the backbuffer for this window which will be the final 3D rendertarget.
    ID3D11Texture2D* backBuffer;
    rend->swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    auto hr = rend->device->CreateRenderTargetView(backBuffer, nullptr, &rend->renderTargetView);
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
    
    POINT current_mouse_pos;
    GetCursorPos(&current_mouse_pos);
    
    bool right_mouse = GetAsyncKeyState(VK_RBUTTON) & 0x8000;
    
    if (right_mouse && !rend->m_right_mouse_down) {
        rend->m_last_mouse_pos = current_mouse_pos;
        rend->m_right_mouse_down = true;
        ShowCursor(FALSE);
        RECT window_rect;
        GetWindowRect(rend->hwnd, &window_rect);
        SetCursorPos((window_rect.left + window_rect.right) / 2, (window_rect.top + window_rect.bottom) / 2);
    }
    else if (!right_mouse && rend->m_right_mouse_down) {
        rend->m_right_mouse_down = false;
        ShowCursor(TRUE);
        SetCursorPos(rend->m_last_mouse_pos.x, rend->m_last_mouse_pos.y);
    }
    
    if (rend->m_right_mouse_down) {
        RECT window_rect;
        GetWindowRect(rend->hwnd, &window_rect);
        POINT center_pos = { (window_rect.left + window_rect.right) / 2, (window_rect.top + window_rect.bottom) / 2 };
        
        GetCursorPos(&current_mouse_pos);
        float delta_x = (float)(current_mouse_pos.x - center_pos.x);
        float delta_y = (float)(current_mouse_pos.y - center_pos.y);
        
        if (delta_x != 0.0f || delta_y != 0.0f) {
            rend->m_camera.update_rotation(delta_x, -delta_y);
            SetCursorPos(center_pos.x, center_pos.y);
        }
    }
    
    FLOAT clearcolor[4] = { 0.015f, 0.015f, 0.015f, 1.0f };

    UINT stride = 11 * sizeof(float); // vertex size (11 floats: float3 position, float3 normal, float2 texcoord, float3 color)
    UINT offset = 0;

    D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)rend->width, (float)rend->height, 0.0f, 1.0f };
    
    ///////////////////////////////////////////////////////////////////////////////////////////////

    float aspect_ratio = viewport.Width / viewport.Height;
    float fov_y = 3.14159f * 0.5f;  // 90 degrees in radians
    float f = 1000.0f;                 // far plane
    float n = 0.1f;                   // near plane
    
    // Perspective projection matrix with explicit FOV
    float tan_half_fov = tanf(fov_y * 0.5f);
    matrix proj = { 
        1.0f / (aspect_ratio * tan_half_fov), 0, 0, 0,
        0, 1.0f / tan_half_fov, 0, 0,
        0, 0, f / (f - n), 1,
        0, 0, -(n * f) / (f - n), 0 
    };

    ///////////////////////////////////////////////////////////////////////////////////////////
   
    auto& context = rend->context;
    auto& constantbuffer = rend->constantBuffer;

    context->ClearRenderTargetView(rend->renderTargetView, clearcolor);
    context->ClearDepthStencilView(rend->depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);
    context->RSSetViewports(1, &viewport);
    context->VSSetConstantBuffers(0, 1, &constantbuffer);

    // Common render state setup
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->OMSetRenderTargets(1, &rend->renderTargetView, rend->depthStencilView);
    context->OMSetBlendState(nullptr, nullptr, 0xffffffff);

    // Group instances by model to minimize state changes
    auto& model = rend->m_models[0];
    
    // Set model-specific state
    context->IASetInputLayout(model.m_p_input_layout);
    context->IASetVertexBuffers(0, 1, &model.m_mesh.m_p_vertex_buffer, &stride, &offset);
    context->IASetIndexBuffer(model.m_mesh.m_p_index_buffer, DXGI_FORMAT_R32_UINT, 0);
    context->VSSetShader(model.m_p_vertex_shader, nullptr, 0);
    context->PSSetShader(model.m_p_pixel_shader, nullptr, 0);
    context->PSSetShaderResources(0, 1, &model.m_p_texture_srv);
    context->PSSetSamplers(0, 1, &model.m_p_sampler_state);
	for (const auto& instance : instances)
	{

        // Render all instances using this model
        D3D11_MAPPED_SUBRESOURCE constantbufferMSR;
        context->Map(constantbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &constantbufferMSR);
        {
            Constants* constants = (Constants*)constantbufferMSR.pData;
            
            matrix model_matrix = instance.get_model_matrix();
            matrix view = rend->m_camera.get_view_matrix();
            constants->model = model_matrix;
            constants->view = view;
            constants->projection = proj;
			constants->lightvector = float3{1.0f, -1.0f, 1.0f};
        }

        context->Unmap(constantbuffer, 0);
        context->DrawIndexed(model.m_mesh.m_index_count, 0, 0);
    }

    HRESULT hr = rend->swapChain->Present(1, 0);
    breakIfFailed(hr, rend->device);
}

void onWindowResize(EditorRenderer *rend, u32 w, u32 h)
{
    createResources(rend, w, h);
}

void onDeviceLost(EditorRenderer *renderer)
{

}