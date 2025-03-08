#include "Scene.h"
#include "EditorRenderer.h"

#include "ScratchAllocator.h"

#pragma comment(lib, "user32.lib")
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct ResizeEvent
{
    u32 w;
    u32 h;
};

struct ThreadData
{
    EditorRenderer* renderer;
    Scene* scene;
    bool shouldExit;
};

ResizeEvent* wantResize = nullptr;
ThreadData g_threadData;
HANDLE g_renderThread;
HANDLE g_updateThread;

LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param)
{
    switch (msg)
    {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
        {
            u32 h = HIWORD(l_param);
            u32 w = LOWORD(l_param);
            if(h != 0 && w != 0)
            {
                ResizeEvent* newEvent = new ResizeEvent{w, h};
                ResizeEvent* oldEvent = (ResizeEvent*)InterlockedExchangePointer((PVOID*)&wantResize, newEvent);
                delete oldEvent;
            }
        }
        break;
    }
    return DefWindowProc(hwnd, msg, w_param, l_param);
}

DWORD WINAPI UpdateThreadProc(LPVOID lpParameter)
{
    ThreadData* data = (ThreadData*)lpParameter;
    while (!data->shouldExit)
    {
        data->scene->updateThread();
    }
    return 0;
}

DWORD WINAPI RenderThreadProc(LPVOID lpParameter)
{
    ThreadData* data = (ThreadData*)lpParameter;
    while (!data->shouldExit)
    {
        ResizeEvent* resizeEvent = (ResizeEvent*)InterlockedExchangePointer((PVOID*)&wantResize, nullptr);
        if(resizeEvent)
        {
            onWindowResize(data->renderer, resizeEvent->w, resizeEvent->h);
            delete resizeEvent;
        }
        data->scene->renderThread();
    }
    return 0;
}

int main()
{
    block_memory_init();

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = EditorWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "EditorRendererClass";
    
    RegisterClassEx(&wc);

    int screen_width = 900;
    int screen_height = 600;

    HWND hwnd = CreateWindowExA(
        WS_EX_APPWINDOW,
        "EditorRendererClass",
        "Editor Renderer",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
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
        Sleep(0);
    }

    block_memory_shutdown();

    return 0;
}