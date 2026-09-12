#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include "../overlay/d3d11_shaders.h"

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

    ID3D11VertexShader* imguiVertexShader = nullptr;
    ID3D11PixelShader* imguiPixelShader = nullptr;
    ID3D11VertexShader* stretchVertexShader = nullptr;
    ID3D11PixelShader* stretchPixelShader = nullptr;
    ID3D11InputLayout* imguiInputLayout = nullptr;
    const D3D11_INPUT_ELEMENT_DESC imguiLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
            D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
            D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16,
            D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    const bool shadersValid =
        SUCCEEDED(device->CreateVertexShader(
            EmbeddedD3D11Shaders::kImGuiVertexShader,
            sizeof(EmbeddedD3D11Shaders::kImGuiVertexShader), nullptr,
            &imguiVertexShader)) &&
        SUCCEEDED(device->CreatePixelShader(
            EmbeddedD3D11Shaders::kImGuiPixelShader,
            sizeof(EmbeddedD3D11Shaders::kImGuiPixelShader), nullptr,
            &imguiPixelShader)) &&
        SUCCEEDED(device->CreateVertexShader(
            EmbeddedD3D11Shaders::kStretchVertexShader,
            sizeof(EmbeddedD3D11Shaders::kStretchVertexShader), nullptr,
            &stretchVertexShader)) &&
        SUCCEEDED(device->CreatePixelShader(
            EmbeddedD3D11Shaders::kStretchPixelShader,
            sizeof(EmbeddedD3D11Shaders::kStretchPixelShader), nullptr,
            &stretchPixelShader)) &&
        SUCCEEDED(device->CreateInputLayout(imguiLayout, 3,
            EmbeddedD3D11Shaders::kImGuiVertexShader,
            sizeof(EmbeddedD3D11Shaders::kImGuiVertexShader),
            &imguiInputLayout));
    if (imguiVertexShader) imguiVertexShader->Release();
    if (imguiPixelShader) imguiPixelShader->Release();
    if (stretchVertexShader) stretchVertexShader->Release();
    if (stretchPixelShader) stretchPixelShader->Release();
    if (imguiInputLayout) imguiInputLayout->Release();
    if (!shadersValid)
        return 15;

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
