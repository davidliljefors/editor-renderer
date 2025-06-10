#include "Scene.h"
#include "EditorRenderer.h"

#include "ScratchAllocator.h"
#include "TruthMap.h"
#include "mh64.h"

#pragma comment(lib, "user32.lib")
#define WIN32_LEAN_AND_MEAN
#include <cstdio>
#include <windows.h>

#include "TruthView.h"

struct ThreadData
{
    EditorRenderer* renderer;
    Scene* scene;
    bool shouldExit;
};


ThreadData g_threadData;
HANDLE g_renderThread;
HANDLE g_updateThread;
struct ThreadSignals
{
    alignas (64) u64 currentFrame = 0;
    u64 _pad0[7];
    alignas (64) u64 renderdThreadCompletedFrame = 0;
    u64 _pad1[7];
    alignas (64) u64 updateThreadCompletedFrame = 0;
    u64 _pad2[7];
    alignas (64) u64 wantQuit = 0;
    u64 _pad3[7];
};

ThreadSignals signals;

struct Win32Event
{
    HWND hwnd;
    UINT msg;
    WPARAM wParam;
    LPARAM lParam;
};

Array<Win32Event>* g_writeEvents = nullptr;
Array<Win32Event>* g_readEvents = nullptr;

volatile bool synchronized = false;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT SyncWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if(msg == WM_USER + 1)
    {
        signals.wantQuit = true;
    }

    if(ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
    {
        return 0;
    }

    if(msg == WM_QUIT || msg == WM_CLOSE)
    {
        PostQuitMessage(0);
    }

    if(msg == WM_SIZE)
    {
        UINT width = LOWORD(lParam);
        UINT height = HIWORD(lParam);
        if(g_threadData.renderer)
        {
            onWindowResize(g_threadData.renderer, width, height);
            return 0;
        }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if(synchronized)
    {
        return SyncWndProc(hwnd, msg, wParam, lParam);
    }
    g_writeEvents->push_back({hwnd, msg, wParam, lParam});
    return 0;
}


void ProcessEvents()
{
    Array<Win32Event>& events = *g_readEvents;
    for(auto& event : events)
    {
        SyncWndProc(event.hwnd, event.msg, event.wParam, event.lParam);
    }

    Array<Win32Event>* tmp = g_readEvents;
    g_readEvents = g_writeEvents;
    g_writeEvents = tmp;
}


DWORD WINAPI UpdateThreadProc(LPVOID lpParameter)
{
    ThreadData* data = (ThreadData*)lpParameter;

    u64 frame = 0;
    while (!data->shouldExit)
    {
        u64 nextFrame = signals.currentFrame;
        if(nextFrame > frame)
        {
            signals.updateThreadCompletedFrame = nextFrame;
            data->scene->update();
            frame = nextFrame;
        }
        else 
        {
            Sleep(0);
        }
    }
    return 0;
}

DWORD WINAPI RenderThreadProc(LPVOID lpParameter)
{
    ThreadData* data = (ThreadData*)lpParameter;
    EditorRenderer* rend = data->renderer;

    u64 frame = 0;
    while (!data->shouldExit)
    {
        u64 nextFrame = signals.currentFrame;
        if(nextFrame > frame)
        {
            preRender(rend);
            renderFrame(rend);
            postRender(rend);
            signals.renderdThreadCompletedFrame = nextFrame;          
            frame = nextFrame;
        }
        else 
        {
            Sleep(0);
        }
    }
    return 0;
}

truth::Key getKey(u64 entity, const char* name)
{
    MetroHash64 hasher;
    hasher.Update((const u8*)&entity, sizeof(u64));
    hasher.Update((const u8*)name, strlen(name));
    truth::Key key;
    hasher.Finalize((u8*)&key.asU64);
    return key;
}

enum class TypeCode
{
    Uint8,
    Uint16,
    Uint32,
    Uint64,
    
    Int8,
    Int16,
    Int32,
    Int64,

    Float32,
    Float64,

    Vec2,
    Vec3,
    Vec4,

    Array,

    ValueType,
};


struct TypeInfo
{
    TypeCode typeCode;
    
};

struct FieldInfo
{
    u64 fieldName;
    TypeInfo* fieldType;
};

struct ArrayTypeInfo
{
    TypeInfo base;
    TypeInfo elementType;
};

struct ValueTypeInfo
{
    u64 nameHash;
    Array<FieldInfo*> fields;
};

struct BoxedValue
{
    TypeInfo* type;
    void* data;
};

MallocAllocator gMalloc;

void LoadWindowSize(const char* configFile, int* width, int* height) {
    FILE* file;
	int ret = fopen_s(&file, configFile, "r");
    if (file) {
        ret = fscanf_s(file, "%d %d", width, height);
        ret = fclose(file);
    }
    (void)ret;
}

void SaveWindowSize(const char* configFile, int width, int height) {
    FILE* file;
    if (width < 1 || height < 1)
    {
	    return;
    }

	int ret = fopen_s(&file, configFile, "w");
    if (file) {
        ret = fprintf(file, "%d %d", width, height);
        ret = fclose(file);
    }
}

int main()
{
    HashMap < int > hm(gMalloc);

    hm.add(1, 1);
    hm.add(2, 2);
    hm.add(3, 3);
    hm.erase(1);

    block_memory_init();
    synchronized = true;

    g_writeEvents = new Array<Win32Event>(gMalloc);
    g_readEvents = new Array<Win32Event>(gMalloc);

    bool isFullscreen = false;

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = EditorWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = TEXT("EditorRendererClass");
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    
    RegisterClassEx(&wc);

    int screen_width = 1600;
    int screen_height = 900;

    LoadWindowSize("window_config.txt", &screen_width, &screen_height);

    DWORD windowStyle = isFullscreen ? WS_VISIBLE|WS_POPUP : (WS_OVERLAPPEDWINDOW);
    DWORD windowExStyle = isFullscreen ? WS_EX_APPWINDOW : WS_EX_APPWINDOW;

    if (isFullscreen)
    {
        screen_width = GetSystemMetrics(SM_CXSCREEN);
        screen_height = GetSystemMetrics(SM_CYSCREEN);
    }

    HWND hwnd = CreateWindowExA(
        windowExStyle,
        "EditorRendererClass",
        "Editor Renderer",
        windowStyle,
        0, 0,
        screen_width, screen_height,
        nullptr,
        nullptr,
        GetModuleHandle(nullptr),
        nullptr
    );

    EditorRenderer* rend = nullptr;
    initRenderer(hwnd, screen_width, screen_height, rend);
    Scene scene = {rend};

    g_threadData.renderer = rend;
    g_threadData.scene = &scene;
    g_threadData.shouldExit = false;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    g_renderThread = CreateThread(
        nullptr,
        0,
        RenderThreadProc,
        &g_threadData,
        0,
        nullptr
    );
    SetThreadDescription(g_renderThread, L"RenderThread");

    g_updateThread = CreateThread(
        nullptr,
        0,
        UpdateThreadProc,
        &g_threadData,
        0,
        nullptr
    );
    SetThreadDescription(g_updateThread, L"UpdateThread");

    MSG msg = {};
    while (true)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT || signals.wantQuit)
            {
                g_threadData.shouldExit = true;
                WaitForSingleObject(g_renderThread, INFINITE);
                WaitForSingleObject(g_updateThread, INFINITE);
                CloseHandle(g_renderThread);
                CloseHandle(g_updateThread);
                RECT rect;
                GetWindowRect(hwnd, &rect);
                SaveWindowSize("window_config.txt", rect.right, rect.bottom);
                return static_cast<int>(msg.wParam);
            }
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
            {
                PostQuitMessage(0);
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        preRenderSync(rend);

        ProcessEvents();    
        synchronized = false;

        u64 nextFrame = signals.currentFrame + 1;
        signals.currentFrame = nextFrame;

        while(
            signals.renderdThreadCompletedFrame != nextFrame || 
            signals.updateThreadCompletedFrame != nextFrame
            )
        {
            Sleep(0);
        }
        synchronized = true;

        renderSynchronize(rend);
    }

    block_memory_shutdown();

    return 0;
}