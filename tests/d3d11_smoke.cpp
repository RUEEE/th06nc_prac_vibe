#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cstdio>

using CreateFactory1Fn = HRESULT(WINAPI*)(REFIID, void**);
using CreateDeviceFn = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

LRESULT CALLBACK SmokeWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(window, message, wParam, lParam);
}

int wmain()
{
    // Leave time for the attach-mode smoke test to inject before graphics resolution.
    Sleep(500);

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = SmokeWndProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"th06nc_test_smoke";
    RegisterClassW(&windowClass);
    HWND window = CreateWindowW(windowClass.lpszClassName, L"th06nc test smoke",
        WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!window)
        return 10;

    const HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    const HMODULE d3d11 = LoadLibraryW(L"d3d11.dll");
    const auto createFactory = reinterpret_cast<CreateFactory1Fn>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
    const auto createDevice = reinterpret_cast<CreateDeviceFn>(GetProcAddress(d3d11, "D3D11CreateDevice"));
    if (!createFactory || !createDevice)
        return 11;

    IDXGIFactory1* factory = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    if (FAILED(createFactory(IID_PPV_ARGS(&factory))) ||
        FAILED(createDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, &context)))
        return 12;

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = 640;
    desc.BufferDesc.Height = 480;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swapChain = nullptr;
    if (FAILED(factory->CreateSwapChain(device, &desc, &swapChain)))
        return 13;

    for (int frame = 0; frame < 60; ++frame) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            DispatchMessageW(&message);
        swapChain->Present(0, 0);
        Sleep(10);
    }

    wchar_t eventName[96]{};
    wsprintfW(eventName, L"Local\\th06nc_test_ready_%lu", GetCurrentProcessId());
    const HANDLE ready = OpenEventW(SYNCHRONIZE, FALSE, eventName);
    if (ready)
        CloseHandle(ready);

    swapChain->Release();
    context->Release();
    device->Release();
    factory->Release();
    DestroyWindow(window);
    return ready ? 0 : 14;
}

