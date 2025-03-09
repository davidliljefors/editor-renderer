#include "Scene.h"
#include "EditorRenderer.h"

#include "ScratchAllocator.h"

#pragma comment(lib, "user32.lib")
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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
    alignas (64) u64 renderdThreadCompletedFrame = 0;
    alignas (64) u64 updateThreadCompletedFrame = 0;
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
    if(ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
    {
        return 0;
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
    SwissTable<Instance> instances {};
    ThreadData* data = (ThreadData*)lpParameter;
    EditorRenderer* rend = data->renderer;

    u64 frame = 0;
    while (!data->shouldExit)
    {
        u64 nextFrame = signals.currentFrame;
        if(nextFrame > frame)
        {
            preRender(rend);
            renderFrame(rend, instances);
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

int main()
{
    block_memory_init();
    MallocAllocator gMalloc;
    synchronized = true;

    g_writeEvents = new Array<Win32Event>(gMalloc);
    g_readEvents = new Array<Win32Event>(gMalloc);

    bool isFullscreen = true;

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = EditorWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "EditorRendererClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    RegisterClassEx(&wc);

    int screen_width = 1600;
    int screen_height = 900;

    DWORD windowStyle = isFullscreen ? WS_VISIBLE|WS_POPUP : (WS_OVERLAPPEDWINDOW | WS_VISIBLE);
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
            if (msg.message == WM_QUIT)
            {
                g_threadData.shouldExit = true;
                WaitForSingleObject(g_renderThread, INFINITE);
                WaitForSingleObject(g_updateThread, INFINITE);
                CloseHandle(g_renderThread);
                CloseHandle(g_updateThread);
                return static_cast<int>(msg.wParam);
            }
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
            {
                PostQuitMessage(0);
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