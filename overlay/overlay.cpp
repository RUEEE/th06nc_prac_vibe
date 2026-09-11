#include "overlay.h"
#include "game_addresses.h"
#include "game_overlay.h"
#include "hitbox_capture.h"
#include "keyboard_input.h"
#include "practice_menu.h"
#include "practice_jump.h"
#include "ui.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

#include <d3d9.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

namespace {

enum class Renderer : LONG { None, D3D9, D3D11 };

using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
using CreateDXGIFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
using CreateDXGIFactory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
using D3D11CreateDeviceFn = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
using Direct3DCreate9Fn = IDirect3D9*(WINAPI*)(UINT);
using Direct3DCreate9ExFn = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);

using FactoryCreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*,
    DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using FactoryCreateSwapChainForHwndFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND,
    const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
using FactoryCreateSwapChainForCoreWindowFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, IUnknown*,
    const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**);
using FactoryCreateSwapChainForCompositionFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*,
    const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**);
using SwapChainPresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using SwapChainResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

using D3D9CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
    D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using D3D9CreateDeviceExFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD,
    D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*, IDirect3DDevice9Ex**);
using D3D9PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using D3D9ResetFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

GetProcAddressFn g_realGetProcAddress = nullptr;
CreateDXGIFactoryFn g_realCreateDXGIFactory = nullptr;
CreateDXGIFactoryFn g_realCreateDXGIFactory1 = nullptr;
CreateDXGIFactory2Fn g_realCreateDXGIFactory2 = nullptr;
D3D11CreateDeviceFn g_realD3D11CreateDevice = nullptr;
Direct3DCreate9Fn g_realDirect3DCreate9 = nullptr;
Direct3DCreate9ExFn g_realDirect3DCreate9Ex = nullptr;

FactoryCreateSwapChainFn g_realFactoryCreateSwapChain = nullptr;
FactoryCreateSwapChainForHwndFn g_realFactoryCreateSwapChainForHwnd = nullptr;
FactoryCreateSwapChainForCoreWindowFn g_realFactoryCreateSwapChainForCoreWindow = nullptr;
FactoryCreateSwapChainForCompositionFn g_realFactoryCreateSwapChainForComposition = nullptr;
SwapChainPresentFn g_realSwapChainPresent = nullptr;
SwapChainResizeBuffersFn g_realSwapChainResizeBuffers = nullptr;

D3D9CreateDeviceFn g_realD3D9CreateDevice = nullptr;
D3D9CreateDeviceExFn g_realD3D9CreateDeviceEx = nullptr;
D3D9PresentFn g_realD3D9Present = nullptr;
D3D9ResetFn g_realD3D9Reset = nullptr;

SRWLOCK g_uiLock = SRWLOCK_INIT;
std::atomic<Renderer> g_renderer{Renderer::None};
std::atomic<bool> g_visible{false};
std::atomic<bool> g_gameStretchEnabled{false};
HWND g_window = nullptr;
WNDPROC g_oldWndProc = nullptr;

IDXGISwapChain* g_dx11SwapChain = nullptr;
ID3D11Device* g_dx11Device = nullptr;
ID3D11DeviceContext* g_dx11Context = nullptr;
ID3D11RenderTargetView* g_dx11RenderTarget = nullptr;
ID3D11Texture2D* g_stretchSource = nullptr;
ID3D11ShaderResourceView* g_stretchSourceView = nullptr;
ID3D11VertexShader* g_stretchVertexShader = nullptr;
ID3D11PixelShader* g_stretchPixelShader = nullptr;
ID3D11SamplerState* g_stretchSampler = nullptr;
ID3D11RasterizerState* g_stretchRasterizer = nullptr;
ID3D11DepthStencilState* g_stretchDepthState = nullptr;
UINT g_stretchWidth = 0;
UINT g_stretchHeight = 0;
DXGI_FORMAT g_stretchFormat = DXGI_FORMAT_UNKNOWN;
IDirect3DDevice9* g_dx9Device = nullptr;
HANDLE g_readyEvent = nullptr;

void DebugMessage(const wchar_t* text)
{
    OutputDebugStringW(L"[th06nc_test] ");
    OutputDebugStringW(text);
    OutputDebugStringW(L"\n");
}

bool PatchPointer(void** slot, void* replacement, void** original)
{
    if (!slot || !replacement)
        return false;
    if (*slot == replacement)
        return true;

    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect))
        return false;
    void* previous = InterlockedExchangePointer(slot, replacement);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    if (original && !*original && previous != replacement)
        *original = previous;
    return true;
}

bool PatchVtable(void* object, size_t index, void* replacement, void** original)
{
    if (!object)
        return false;
    void** table = *reinterpret_cast<void***>(object);
    return table && PatchPointer(&table[index], replacement, original);
}

bool PatchMainImport(const char* importedName, void* replacement, void** original)
{
    auto* base = GameModuleBase();
    if (!base)
        return false;
    const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return false;
    const DWORD importRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!importRva)
        return false;

    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importRva);
    for (; descriptor->Name; ++descriptor) {
        const DWORD namesRva = descriptor->OriginalFirstThunk;
        if (!namesRva)
            continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + namesRva);
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))
                continue;
            const auto* import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(import->Name), importedName) == 0)
                return PatchPointer(reinterpret_cast<void**>(&addresses->u1.Function), replacement, original);
        }
    }
    return false;
}

bool PatchCachedMainPointer(void* oldAddress, void* replacement)
{
    if (!oldAddress || !replacement || oldAddress == replacement)
        return false;
    auto* base = GameModuleBase();
    const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    auto* section = IMAGE_FIRST_SECTION(nt);
    bool changed = false;

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_WRITE))
            continue;
        const size_t length = section->Misc.VirtualSize;
        auto* begin = base + section->VirtualAddress;
        for (size_t offset = 0; offset + sizeof(void*) <= length; offset += alignof(void*)) {
            auto** slot = reinterpret_cast<void**>(begin + offset);
            if (*slot == oldAddress)
                changed |= PatchPointer(slot, replacement, nullptr);
        }
    }
    return changed;
}

bool IsMouseMessage(UINT message)
{
    return (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
        message == WM_NCMOUSEMOVE || message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONUP ||
        message == WM_NCRBUTTONDOWN || message == WM_NCRBUTTONUP;
}

bool IsKeyboardMessage(UINT message)
{
    return (message >= WM_KEYFIRST && message <= WM_KEYLAST) || message == WM_CHAR;
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (g_renderer.load() != Renderer::None &&
        (g_visible.load() || IsPracticeMenuReplacementActive() ||
            IsGameOverlayVisible())) {
        ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
        const ImGuiIO& io = ImGui::GetIO();
        if ((IsMouseMessage(message) && io.WantCaptureMouse) ||
            (IsKeyboardMessage(message) && io.WantCaptureKeyboard))
            return 1;
    }
    return g_oldWndProc ? CallWindowProcW(g_oldWndProc, hwnd, message, wParam, lParam)
                        : DefWindowProcW(hwnd, message, wParam, lParam);
}

bool InitializeWindow(HWND window)
{
    if (!window)
        return false;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    constexpr float referenceHeight = 1440.0f;
    constexpr float referenceScale = 2.5f;
    RECT clientRect{};
    const bool hasClientSize = GetClientRect(window, &clientRect) != FALSE &&
        clientRect.bottom > clientRect.top;
    const float clientHeight = hasClientSize
        ? static_cast<float>(clientRect.bottom - clientRect.top)
        : referenceHeight;
    const float uiScale = referenceScale * clientHeight / referenceHeight;
    ImGui::GetStyle().ScaleAllSizes(uiScale);

    ImFontConfig font_cfg;
    font_cfg.OversampleH = 2;
    font_cfg.OversampleV = 2;
    ImGuiIO& io = ImGui::GetIO();

    ImFont* font = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\msyh.ttc",
        26.0f,
        &font_cfg,
        io.Fonts->GetGlyphRangesChineseFull()
    );

    if (!font)
    {
        ImGui::DestroyContext();
        return false;
    }

    io.FontGlobalScale = uiScale;

    if (!ImGui_ImplWin32_Init(window)) {
        ImGui::DestroyContext();
        return false;
    }

    g_window = window;
    SetLastError(0);
    g_oldWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(OverlayWndProc)));
    if (!g_oldWndProc && GetLastError() != 0) {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_window = nullptr;
        return false;
    }
    return true;
}

void UndoWindowInitialization()
{
    if (g_window && g_oldWndProc)
        SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_oldWndProc));
    g_window = nullptr;
    g_oldWndProc = nullptr;
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

bool CreateD3D11RenderTarget(IDXGISwapChain* swapChain)
{
    if (g_dx11RenderTarget)
        return true;
    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
        return false;
    const HRESULT result = g_dx11Device->CreateRenderTargetView(backBuffer, nullptr, &g_dx11RenderTarget);
    backBuffer->Release();
    return SUCCEEDED(result);
}

template <typename T>
void ReleaseCom(T*& object)
{
    if (object) {
        object->Release();
        object = nullptr;
    }
}

void ReleaseStretchSource()
{
    ReleaseCom(g_stretchSourceView);
    ReleaseCom(g_stretchSource);
    g_stretchWidth = 0;
    g_stretchHeight = 0;
    g_stretchFormat = DXGI_FORMAT_UNKNOWN;
}

void ReleaseStretchPipeline()
{
    ReleaseStretchSource();
    ReleaseCom(g_stretchDepthState);
    ReleaseCom(g_stretchRasterizer);
    ReleaseCom(g_stretchSampler);
    ReleaseCom(g_stretchPixelShader);
    ReleaseCom(g_stretchVertexShader);
}

bool CreateStretchPipeline()
{
    if (g_stretchVertexShader && g_stretchPixelShader && g_stretchSampler &&
        g_stretchRasterizer && g_stretchDepthState)
        return true;

    static constexpr char shaderSource[] = R"(
struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VertexOutput VertexMain(uint vertexId : SV_VertexID)
{
    static const float2 positions[3] = {
        float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
    };
    VertexOutput output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.uv = float2((positions[vertexId].x + 1.0) * 0.5,
                       (1.0 - positions[vertexId].y) * 0.5);
    return output;
}

Texture2D gameTexture : register(t0);
SamplerState linearClamp : register(s0);

float4 PixelMain(VertexOutput input) : SV_TARGET
{
    // Keep the right edge fixed. Only the rightmost 75% of the completed game
    // image remains visible, making its X extent 4/3 as wide.
    input.uv.x = 0.25 + input.uv.x * 0.75;
    return gameTexture.Sample(linearClamp, input.uv);
}
)";

    ID3DBlob* vertexBlob = nullptr;
    ID3DBlob* pixelBlob = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT result = D3DCompile(shaderSource, sizeof(shaderSource) - 1, nullptr, nullptr, nullptr,
        "VertexMain", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vertexBlob, &errors);
    if (FAILED(result)) {
        if (errors)
            OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        ReleaseCom(errors);
        return false;
    }
    ReleaseCom(errors);

    result = D3DCompile(shaderSource, sizeof(shaderSource) - 1, nullptr, nullptr, nullptr,
        "PixelMain", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pixelBlob, &errors);
    if (FAILED(result)) {
        if (errors)
            OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        ReleaseCom(errors);
        ReleaseCom(vertexBlob);
        return false;
    }
    ReleaseCom(errors);

    result = g_dx11Device->CreateVertexShader(vertexBlob->GetBufferPointer(),
        vertexBlob->GetBufferSize(), nullptr, &g_stretchVertexShader);
    if (SUCCEEDED(result))
        result = g_dx11Device->CreatePixelShader(pixelBlob->GetBufferPointer(),
            pixelBlob->GetBufferSize(), nullptr, &g_stretchPixelShader);
    ReleaseCom(pixelBlob);
    ReleaseCom(vertexBlob);

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (SUCCEEDED(result))
        result = g_dx11Device->CreateSamplerState(&sampler, &g_stretchSampler);

    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = TRUE;
    if (SUCCEEDED(result))
        result = g_dx11Device->CreateRasterizerState(&rasterizer, &g_stretchRasterizer);

    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.StencilEnable = FALSE;
    if (SUCCEEDED(result))
        result = g_dx11Device->CreateDepthStencilState(&depth, &g_stretchDepthState);

    if (FAILED(result)) {
        ReleaseStretchPipeline();
        DebugMessage(L"Could not create the Direct3D 11 stretch pipeline");
        return false;
    }
    return true;
}

bool EnsureStretchSource(const D3D11_TEXTURE2D_DESC& backBufferDesc)
{
    if (backBufferDesc.SampleDesc.Count != 1)
        return false;
    if (g_stretchSource && g_stretchWidth == backBufferDesc.Width &&
        g_stretchHeight == backBufferDesc.Height && g_stretchFormat == backBufferDesc.Format)
        return true;

    ReleaseStretchSource();
    D3D11_TEXTURE2D_DESC sourceDesc = backBufferDesc;
    sourceDesc.MipLevels = 1;
    sourceDesc.ArraySize = 1;
    sourceDesc.SampleDesc.Count = 1;
    sourceDesc.SampleDesc.Quality = 0;
    sourceDesc.Usage = D3D11_USAGE_DEFAULT;
    sourceDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    sourceDesc.CPUAccessFlags = 0;
    sourceDesc.MiscFlags = 0;
    if (FAILED(g_dx11Device->CreateTexture2D(&sourceDesc, nullptr, &g_stretchSource)) ||
        FAILED(g_dx11Device->CreateShaderResourceView(g_stretchSource, nullptr, &g_stretchSourceView))) {
        ReleaseStretchSource();
        return false;
    }
    g_stretchWidth = backBufferDesc.Width;
    g_stretchHeight = backBufferDesc.Height;
    g_stretchFormat = backBufferDesc.Format;
    return true;
}

void ApplyGameStretchPostProcess(IDXGISwapChain* swapChain)
{
    if (!g_gameStretchEnabled.load() || !g_dx11Context || !g_dx11RenderTarget ||
        !CreateStretchPipeline())
        return;

    ID3D11Texture2D* backBuffer = nullptr;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
        return;
    D3D11_TEXTURE2D_DESC backBufferDesc{};
    backBuffer->GetDesc(&backBufferDesc);
    if (!EnsureStretchSource(backBufferDesc)) {
        backBuffer->Release();
        return;
    }
    g_dx11Context->CopyResource(g_stretchSource, backBuffer);

    ID3D11RenderTargetView* oldTarget = nullptr;
    ID3D11DepthStencilView* oldDepthView = nullptr;
    ID3D11BlendState* oldBlend = nullptr;
    FLOAT oldBlendFactor[4]{};
    UINT oldSampleMask = 0;
    ID3D11DepthStencilState* oldDepthState = nullptr;
    UINT oldStencilRef = 0;
    ID3D11RasterizerState* oldRasterizer = nullptr;
    D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT oldViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ID3D11InputLayout* oldInputLayout = nullptr;
    ID3D11Buffer* oldVertexBuffer = nullptr;
    UINT oldVertexStride = 0;
    UINT oldVertexOffset = 0;
    ID3D11Buffer* oldIndexBuffer = nullptr;
    DXGI_FORMAT oldIndexFormat = DXGI_FORMAT_UNKNOWN;
    UINT oldIndexOffset = 0;
    D3D11_PRIMITIVE_TOPOLOGY oldTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11VertexShader* oldVertexShader = nullptr;
    ID3D11PixelShader* oldPixelShader = nullptr;
    ID3D11GeometryShader* oldGeometryShader = nullptr;
    ID3D11HullShader* oldHullShader = nullptr;
    ID3D11DomainShader* oldDomainShader = nullptr;
    ID3D11ShaderResourceView* oldShaderResource = nullptr;
    ID3D11SamplerState* oldSampler = nullptr;

    g_dx11Context->OMGetRenderTargets(1, &oldTarget, &oldDepthView);
    g_dx11Context->OMGetBlendState(&oldBlend, oldBlendFactor, &oldSampleMask);
    g_dx11Context->OMGetDepthStencilState(&oldDepthState, &oldStencilRef);
    g_dx11Context->RSGetState(&oldRasterizer);
    g_dx11Context->RSGetViewports(&oldViewportCount, oldViewports);
    g_dx11Context->IAGetInputLayout(&oldInputLayout);
    g_dx11Context->IAGetVertexBuffers(0, 1, &oldVertexBuffer, &oldVertexStride, &oldVertexOffset);
    g_dx11Context->IAGetIndexBuffer(&oldIndexBuffer, &oldIndexFormat, &oldIndexOffset);
    g_dx11Context->IAGetPrimitiveTopology(&oldTopology);
    g_dx11Context->VSGetShader(&oldVertexShader, nullptr, nullptr);
    g_dx11Context->PSGetShader(&oldPixelShader, nullptr, nullptr);
    g_dx11Context->GSGetShader(&oldGeometryShader, nullptr, nullptr);
    g_dx11Context->HSGetShader(&oldHullShader, nullptr, nullptr);
    g_dx11Context->DSGetShader(&oldDomainShader, nullptr, nullptr);
    g_dx11Context->PSGetShaderResources(0, 1, &oldShaderResource);
    g_dx11Context->PSGetSamplers(0, 1, &oldSampler);

    const FLOAT blendFactor[4]{};
    const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<FLOAT>(backBufferDesc.Width),
        static_cast<FLOAT>(backBufferDesc.Height), 0.0f, 1.0f};
    ID3D11Buffer* noBuffer = nullptr;
    UINT zero = 0;
    g_dx11Context->OMSetRenderTargets(1, &g_dx11RenderTarget, nullptr);
    g_dx11Context->OMSetBlendState(nullptr, blendFactor, 0xffffffffu);
    g_dx11Context->OMSetDepthStencilState(g_stretchDepthState, 0);
    g_dx11Context->RSSetState(g_stretchRasterizer);
    g_dx11Context->RSSetViewports(1, &viewport);
    g_dx11Context->IASetInputLayout(nullptr);
    g_dx11Context->IASetVertexBuffers(0, 1, &noBuffer, &zero, &zero);
    g_dx11Context->IASetIndexBuffer(nullptr, DXGI_FORMAT_R16_UINT, 0);
    g_dx11Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_dx11Context->VSSetShader(g_stretchVertexShader, nullptr, 0);
    g_dx11Context->PSSetShader(g_stretchPixelShader, nullptr, 0);
    g_dx11Context->GSSetShader(nullptr, nullptr, 0);
    g_dx11Context->HSSetShader(nullptr, nullptr, 0);
    g_dx11Context->DSSetShader(nullptr, nullptr, 0);
    g_dx11Context->PSSetShaderResources(0, 1, &g_stretchSourceView);
    g_dx11Context->PSSetSamplers(0, 1, &g_stretchSampler);
    g_dx11Context->Draw(3, 0);

    ID3D11ShaderResourceView* noResource = nullptr;
    g_dx11Context->PSSetShaderResources(0, 1, &noResource);
    g_dx11Context->OMSetRenderTargets(1, &oldTarget, oldDepthView);
    g_dx11Context->OMSetBlendState(oldBlend, oldBlendFactor, oldSampleMask);
    g_dx11Context->OMSetDepthStencilState(oldDepthState, oldStencilRef);
    g_dx11Context->RSSetState(oldRasterizer);
    g_dx11Context->RSSetViewports(oldViewportCount, oldViewports);
    g_dx11Context->IASetInputLayout(oldInputLayout);
    g_dx11Context->IASetVertexBuffers(0, 1, &oldVertexBuffer, &oldVertexStride, &oldVertexOffset);
    g_dx11Context->IASetIndexBuffer(oldIndexBuffer, oldIndexFormat, oldIndexOffset);
    g_dx11Context->IASetPrimitiveTopology(oldTopology);
    g_dx11Context->VSSetShader(oldVertexShader, nullptr, 0);
    g_dx11Context->PSSetShader(oldPixelShader, nullptr, 0);
    g_dx11Context->GSSetShader(oldGeometryShader, nullptr, 0);
    g_dx11Context->HSSetShader(oldHullShader, nullptr, 0);
    g_dx11Context->DSSetShader(oldDomainShader, nullptr, 0);
    g_dx11Context->PSSetShaderResources(0, 1, &oldShaderResource);
    g_dx11Context->PSSetSamplers(0, 1, &oldSampler);

    ReleaseCom(oldSampler);
    ReleaseCom(oldShaderResource);
    ReleaseCom(oldDomainShader);
    ReleaseCom(oldHullShader);
    ReleaseCom(oldGeometryShader);
    ReleaseCom(oldPixelShader);
    ReleaseCom(oldVertexShader);
    ReleaseCom(oldIndexBuffer);
    ReleaseCom(oldVertexBuffer);
    ReleaseCom(oldInputLayout);
    ReleaseCom(oldRasterizer);
    ReleaseCom(oldDepthState);
    ReleaseCom(oldBlend);
    ReleaseCom(oldDepthView);
    ReleaseCom(oldTarget);
    backBuffer->Release();
}

bool InitializeD3D11(IDXGISwapChain* swapChain)
{
    if (g_renderer.load() == Renderer::D3D11)
        return true;
    if (g_renderer.load() != Renderer::None)
        return false;

    AcquireSRWLockExclusive(&g_uiLock);
    if (g_renderer.load() != Renderer::None) {
        const bool result = g_renderer.load() == Renderer::D3D11;
        ReleaseSRWLockExclusive(&g_uiLock);
        return result;
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swapChain->GetDesc(&desc)) || !desc.OutputWindow ||
        FAILED(swapChain->GetDevice(IID_PPV_ARGS(&g_dx11Device)))) {
        ReleaseSRWLockExclusive(&g_uiLock);
        return false;
    }
    g_dx11Device->GetImmediateContext(&g_dx11Context);
    g_dx11SwapChain = swapChain;
    g_dx11SwapChain->AddRef();

    bool ok = InitializeWindow(desc.OutputWindow);
    bool rendererInitialized = false;
    if (ok) {
        rendererInitialized = ImGui_ImplDX11_Init(g_dx11Device, g_dx11Context);
        ok = rendererInitialized;
    }
    if (ok)
        ok = CreateD3D11RenderTarget(swapChain);
    if (ok) {
        g_renderer.store(Renderer::D3D11);
        DebugMessage(L"Direct3D 11 ImGui backend initialized");
    } else {
        if (rendererInitialized)
            ImGui_ImplDX11_Shutdown();
        if (ImGui::GetCurrentContext())
            UndoWindowInitialization();
        ReleaseStretchPipeline();
        if (g_dx11SwapChain) { g_dx11SwapChain->Release(); g_dx11SwapChain = nullptr; }
        if (g_dx11Context) { g_dx11Context->Release(); g_dx11Context = nullptr; }
        if (g_dx11Device) { g_dx11Device->Release(); g_dx11Device = nullptr; }
    }
    ReleaseSRWLockExclusive(&g_uiLock);
    return ok;
}

bool InitializeD3D9(IDirect3DDevice9* device)
{
    if (g_renderer.load() == Renderer::D3D9)
        return true;
    if (g_renderer.load() != Renderer::None)
        return false;

    AcquireSRWLockExclusive(&g_uiLock);
    if (g_renderer.load() != Renderer::None) {
        const bool result = g_renderer.load() == Renderer::D3D9;
        ReleaseSRWLockExclusive(&g_uiLock);
        return result;
    }

    D3DDEVICE_CREATION_PARAMETERS parameters{};
    bool ok = SUCCEEDED(device->GetCreationParameters(&parameters)) && parameters.hFocusWindow;
    if (ok)
        ok = InitializeWindow(parameters.hFocusWindow);
    bool rendererInitialized = false;
    if (ok) {
        rendererInitialized = ImGui_ImplDX9_Init(device);
        ok = rendererInitialized;
    }
    if (ok) {
        g_dx9Device = device;
        g_dx9Device->AddRef();
        g_renderer.store(Renderer::D3D9);
        DebugMessage(L"Direct3D 9 ImGui backend initialized");
    } else {
        if (rendererInitialized)
            ImGui_ImplDX9_Shutdown();
        if (ImGui::GetCurrentContext())
            UndoWindowInitialization();
    }
    ReleaseSRWLockExclusive(&g_uiLock);
    return ok;
}

void BeginUiFrame(const char* rendererName)
{
    if (GetAsyncKeyState(VK_F10) & 1)
        g_visible.store(!g_visible.load());
    const bool practiceMenuActive = IsPracticeMenuReplacementActive();
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = g_visible.load() || practiceMenuActive ||
        IsGameOverlayVisible();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    UpdateAndDrawGameOverlayUi();
    DrawCapturedHitboxes();
    if (g_visible.load()) {
        // Draw this full-screen window translucently. Keeping the alpha scoped
        // here leaves the Practice UI fully opaque when both are open.
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.78f);
        DrawPracticeBaseUi(rendererName);
        ImGui::PopStyleVar();
    }
    if (practiceMenuActive)
        DrawPracticeMenuReplacementUi();
    ImGui::EndFrame();
    ImGui::Render();
}

void RenderD3D11(IDXGISwapChain* swapChain)
{
    if (!InitializeD3D11(swapChain) || !CreateD3D11RenderTarget(swapChain))
        return;
    ApplyGameStretchPostProcess(swapChain);
    ImGui_ImplDX11_NewFrame();
    BeginUiFrame("Direct3D 11 / x64");

    ID3D11RenderTargetView* previousTarget = nullptr;
    ID3D11DepthStencilView* previousDepth = nullptr;
    g_dx11Context->OMGetRenderTargets(1, &previousTarget, &previousDepth);
    g_dx11Context->OMSetRenderTargets(1, &g_dx11RenderTarget, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_dx11Context->OMSetRenderTargets(1, &previousTarget, previousDepth);
    if (previousDepth) previousDepth->Release();
    if (previousTarget) previousTarget->Release();
}

void RenderD3D9(IDirect3DDevice9* device)
{
    if (!InitializeD3D9(device))
        return;
    ImGui_ImplDX9_NewFrame();
    BeginUiFrame("Direct3D 9 / x64");
    if (SUCCEEDED(device->BeginScene())) {
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        device->EndScene();
    }
}

void HookSwapChain(IDXGISwapChain* swapChain);
void HookFactory(IUnknown* factory);
void HookD3D9Interface(IDirect3D9* d3d9, bool extended);
void HookD3D9Device(IDirect3DDevice9* device);

HRESULT STDMETHODCALLTYPE HookSwapChainPresent(IDXGISwapChain* self, UINT syncInterval, UINT flags)
{
    RenderD3D11(self);
    return g_realSwapChainPresent(self, syncInterval, flags);
}

HRESULT STDMETHODCALLTYPE HookSwapChainResizeBuffers(IDXGISwapChain* self, UINT bufferCount, UINT width,
    UINT height, DXGI_FORMAT format, UINT flags)
{
    if (self == g_dx11SwapChain) {
        ReleaseCom(g_dx11RenderTarget);
        ReleaseStretchSource();
    }
    return g_realSwapChainResizeBuffers(self, bufferCount, width, height, format, flags);
}

void HookSwapChain(IDXGISwapChain* swapChain)
{
    if (!swapChain)
        return;
    PatchVtable(swapChain, 8, reinterpret_cast<void*>(HookSwapChainPresent),
        reinterpret_cast<void**>(&g_realSwapChainPresent));
    PatchVtable(swapChain, 13, reinterpret_cast<void*>(HookSwapChainResizeBuffers),
        reinterpret_cast<void**>(&g_realSwapChainResizeBuffers));
}

HRESULT STDMETHODCALLTYPE HookFactoryCreateSwapChain(IDXGIFactory* self, IUnknown* device,
    DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swapChain)
{
    const HRESULT result = g_realFactoryCreateSwapChain(self, device, desc, swapChain);
    if (SUCCEEDED(result) && swapChain)
        HookSwapChain(*swapChain);
    return result;
}

HRESULT STDMETHODCALLTYPE HookFactoryCreateSwapChainForHwnd(IDXGIFactory2* self, IUnknown* device, HWND window,
    const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,
    IDXGIOutput* output, IDXGISwapChain1** swapChain)
{
    const HRESULT result = g_realFactoryCreateSwapChainForHwnd(self, device, window, desc, fullscreen, output, swapChain);
    if (SUCCEEDED(result) && swapChain)
        HookSwapChain(*swapChain);
    return result;
}

HRESULT STDMETHODCALLTYPE HookFactoryCreateSwapChainForCoreWindow(IDXGIFactory2* self, IUnknown* device,
    IUnknown* window, const DXGI_SWAP_CHAIN_DESC1* desc, IDXGIOutput* output, IDXGISwapChain1** swapChain)
{
    const HRESULT result = g_realFactoryCreateSwapChainForCoreWindow(self, device, window, desc, output, swapChain);
    if (SUCCEEDED(result) && swapChain)
        HookSwapChain(*swapChain);
    return result;
}

HRESULT STDMETHODCALLTYPE HookFactoryCreateSwapChainForComposition(IDXGIFactory2* self, IUnknown* device,
    const DXGI_SWAP_CHAIN_DESC1* desc, IDXGIOutput* output, IDXGISwapChain1** swapChain)
{
    const HRESULT result = g_realFactoryCreateSwapChainForComposition(self, device, desc, output, swapChain);
    if (SUCCEEDED(result) && swapChain)
        HookSwapChain(*swapChain);
    return result;
}

void HookFactory(IUnknown* unknown)
{
    if (!unknown)
        return;
    IDXGIFactory* factory = nullptr;
    if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&factory)))) {
        PatchVtable(factory, 10, reinterpret_cast<void*>(HookFactoryCreateSwapChain),
            reinterpret_cast<void**>(&g_realFactoryCreateSwapChain));
        factory->Release();
    }

    IDXGIFactory2* factory2 = nullptr;
    if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&factory2)))) {
        PatchVtable(factory2, 15, reinterpret_cast<void*>(HookFactoryCreateSwapChainForHwnd),
            reinterpret_cast<void**>(&g_realFactoryCreateSwapChainForHwnd));
        PatchVtable(factory2, 16, reinterpret_cast<void*>(HookFactoryCreateSwapChainForCoreWindow),
            reinterpret_cast<void**>(&g_realFactoryCreateSwapChainForCoreWindow));
        PatchVtable(factory2, 24, reinterpret_cast<void*>(HookFactoryCreateSwapChainForComposition),
            reinterpret_cast<void**>(&g_realFactoryCreateSwapChainForComposition));
        factory2->Release();
    }
}

HRESULT WINAPI HookCreateDXGIFactory(REFIID iid, void** factory)
{
    const HRESULT result = g_realCreateDXGIFactory(iid, factory);
    if (SUCCEEDED(result) && factory)
        HookFactory(static_cast<IUnknown*>(*factory));
    return result;
}

HRESULT WINAPI HookCreateDXGIFactory1(REFIID iid, void** factory)
{
    const HRESULT result = g_realCreateDXGIFactory1(iid, factory);
    if (SUCCEEDED(result) && factory)
        HookFactory(static_cast<IUnknown*>(*factory));
    return result;
}

HRESULT WINAPI HookCreateDXGIFactory2(UINT flags, REFIID iid, void** factory)
{
    const HRESULT result = g_realCreateDXGIFactory2(flags, iid, factory);
    if (SUCCEEDED(result) && factory)
        HookFactory(static_cast<IUnknown*>(*factory));
    return result;
}

HRESULT WINAPI HookD3D11CreateDevice(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driverType, HMODULE software,
    UINT flags, const D3D_FEATURE_LEVEL* levels, UINT levelCount, UINT sdkVersion, ID3D11Device** device,
    D3D_FEATURE_LEVEL* selectedLevel, ID3D11DeviceContext** context)
{
    return g_realD3D11CreateDevice(adapter, driverType, software, flags, levels, levelCount,
        sdkVersion, device, selectedLevel, context);
}

HRESULT STDMETHODCALLTYPE HookD3D9Present(IDirect3DDevice9* self, const RECT* source, const RECT* destination,
    HWND windowOverride, const RGNDATA* dirtyRegion)
{
    RenderD3D9(self);
    return g_realD3D9Present(self, source, destination, windowOverride, dirtyRegion);
}

HRESULT STDMETHODCALLTYPE HookD3D9Reset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* parameters)
{
    if (g_renderer.load() == Renderer::D3D9)
        ImGui_ImplDX9_InvalidateDeviceObjects();
    const HRESULT result = g_realD3D9Reset(self, parameters);
    if (SUCCEEDED(result) && g_renderer.load() == Renderer::D3D9)
        ImGui_ImplDX9_CreateDeviceObjects();
    return result;
}

void HookD3D9Device(IDirect3DDevice9* device)
{
    if (!device)
        return;
    PatchVtable(device, 16, reinterpret_cast<void*>(HookD3D9Reset),
        reinterpret_cast<void**>(&g_realD3D9Reset));
    PatchVtable(device, 17, reinterpret_cast<void*>(HookD3D9Present),
        reinterpret_cast<void**>(&g_realD3D9Present));
}

HRESULT STDMETHODCALLTYPE HookD3D9CreateDevice(IDirect3D9* self, UINT adapter, D3DDEVTYPE deviceType,
    HWND focusWindow, DWORD flags, D3DPRESENT_PARAMETERS* parameters, IDirect3DDevice9** device)
{
    const HRESULT result = g_realD3D9CreateDevice(self, adapter, deviceType, focusWindow, flags, parameters, device);
    if (SUCCEEDED(result) && device)
        HookD3D9Device(*device);
    return result;
}

HRESULT STDMETHODCALLTYPE HookD3D9CreateDeviceEx(IDirect3D9Ex* self, UINT adapter, D3DDEVTYPE deviceType,
    HWND focusWindow, DWORD flags, D3DPRESENT_PARAMETERS* parameters, D3DDISPLAYMODEEX* fullscreen,
    IDirect3DDevice9Ex** device)
{
    const HRESULT result = g_realD3D9CreateDeviceEx(self, adapter, deviceType, focusWindow, flags,
        parameters, fullscreen, device);
    if (SUCCEEDED(result) && device)
        HookD3D9Device(*device);
    return result;
}

void HookD3D9Interface(IDirect3D9* d3d9, bool extended)
{
    if (!d3d9)
        return;
    PatchVtable(d3d9, 16, reinterpret_cast<void*>(HookD3D9CreateDevice),
        reinterpret_cast<void**>(&g_realD3D9CreateDevice));
    if (extended)
        PatchVtable(d3d9, 20, reinterpret_cast<void*>(HookD3D9CreateDeviceEx),
            reinterpret_cast<void**>(&g_realD3D9CreateDeviceEx));
}

IDirect3D9* WINAPI HookDirect3DCreate9(UINT sdkVersion)
{
    IDirect3D9* result = g_realDirect3DCreate9(sdkVersion);
    HookD3D9Interface(result, false);
    return result;
}

HRESULT WINAPI HookDirect3DCreate9Ex(UINT sdkVersion, IDirect3D9Ex** d3d9)
{
    const HRESULT result = g_realDirect3DCreate9Ex(sdkVersion, d3d9);
    if (SUCCEEDED(result) && d3d9)
        HookD3D9Interface(*d3d9, true);
    return result;
}

FARPROC WINAPI HookGetProcAddress(HMODULE module, LPCSTR name)
{
    const FARPROC real = g_realGetProcAddress(module, name);
    if (!real || !name || reinterpret_cast<uintptr_t>(name) <= 0xFFFF)
        return real;

    if (std::strcmp(name, "CreateDXGIFactory") == 0) {
        g_realCreateDXGIFactory = reinterpret_cast<CreateDXGIFactoryFn>(real);
        return reinterpret_cast<FARPROC>(HookCreateDXGIFactory);
    }
    if (std::strcmp(name, "CreateDXGIFactory1") == 0) {
        g_realCreateDXGIFactory1 = reinterpret_cast<CreateDXGIFactoryFn>(real);
        return reinterpret_cast<FARPROC>(HookCreateDXGIFactory1);
    }
    if (std::strcmp(name, "CreateDXGIFactory2") == 0) {
        g_realCreateDXGIFactory2 = reinterpret_cast<CreateDXGIFactory2Fn>(real);
        return reinterpret_cast<FARPROC>(HookCreateDXGIFactory2);
    }
    if (std::strcmp(name, "D3D11CreateDevice") == 0) {
        g_realD3D11CreateDevice = reinterpret_cast<D3D11CreateDeviceFn>(real);
        return reinterpret_cast<FARPROC>(HookD3D11CreateDevice);
    }
    if (std::strcmp(name, "Direct3DCreate9") == 0) {
        g_realDirect3DCreate9 = reinterpret_cast<Direct3DCreate9Fn>(real);
        return reinterpret_cast<FARPROC>(HookDirect3DCreate9);
    }
    if (std::strcmp(name, "Direct3DCreate9Ex") == 0) {
        g_realDirect3DCreate9Ex = reinterpret_cast<Direct3DCreate9ExFn>(real);
        return reinterpret_cast<FARPROC>(HookDirect3DCreate9Ex);
    }
    return real;
}

template <typename T>
void HookLoadedExport(const wchar_t* moduleName, const char* exportName, T& original, T replacement)
{
    const HMODULE module = GetModuleHandleW(moduleName);
    if (!module || !g_realGetProcAddress)
        return;
    const FARPROC address = g_realGetProcAddress(module, exportName);
    if (!address)
        return;
    original = reinterpret_cast<T>(address);
    PatchCachedMainPointer(reinterpret_cast<void*>(address), reinterpret_cast<void*>(replacement));
}

void RecoverAlreadyResolvedExports()
{
    HookLoadedExport(L"dxgi.dll", "CreateDXGIFactory", g_realCreateDXGIFactory,
        static_cast<CreateDXGIFactoryFn>(HookCreateDXGIFactory));
    HookLoadedExport(L"dxgi.dll", "CreateDXGIFactory1", g_realCreateDXGIFactory1,
        static_cast<CreateDXGIFactoryFn>(HookCreateDXGIFactory1));
    HookLoadedExport(L"dxgi.dll", "CreateDXGIFactory2", g_realCreateDXGIFactory2,
        static_cast<CreateDXGIFactory2Fn>(HookCreateDXGIFactory2));
    HookLoadedExport(L"d3d11.dll", "D3D11CreateDevice", g_realD3D11CreateDevice,
        static_cast<D3D11CreateDeviceFn>(HookD3D11CreateDevice));
    HookLoadedExport(L"d3d9.dll", "Direct3DCreate9", g_realDirect3DCreate9,
        static_cast<Direct3DCreate9Fn>(HookDirect3DCreate9));
    HookLoadedExport(L"d3d9.dll", "Direct3DCreate9Ex", g_realDirect3DCreate9Ex,
        static_cast<Direct3DCreate9ExFn>(HookDirect3DCreate9Ex));
}

} // namespace

bool IsGameStretchModeEnabled()
{
    return g_gameStretchEnabled.load();
}

void SetGameStretchModeEnabled(bool enabled)
{
    g_gameStretchEnabled.store(enabled);
}

void ClearStagePlayfieldBlack()
{
    constexpr float referenceWidth = 640.0f;
    constexpr float referenceHeight = 480.0f;
    constexpr float stageLeft = 128.0f;
    constexpr float stageTop = 16.0f;
    constexpr float stageRight = 512.0f;
    constexpr float stageBottom = 464.0f;
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    if (g_renderer.load() == Renderer::D3D11 && g_dx11Context) {
        ID3D11RenderTargetView* target = nullptr;
        ID3D11Resource* resource = nullptr;
        ID3D11Texture2D* texture = nullptr;
        ID3D11DeviceContext1* context1 = nullptr;
        g_dx11Context->OMGetRenderTargets(1, &target, nullptr);
        if (target)
            target->GetResource(&resource);
        if (resource)
            resource->QueryInterface(IID_PPV_ARGS(&texture));
        if (target && SUCCEEDED(g_dx11Context->QueryInterface(IID_PPV_ARGS(&context1))) && texture) {
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            const float scale = std::min(desc.Width / referenceWidth, desc.Height / referenceHeight);
            const float offsetX = (desc.Width - referenceWidth * scale) * 0.5f;
            const float offsetY = (desc.Height - referenceHeight * scale) * 0.5f;
            const RECT rect = {
                static_cast<LONG>(offsetX + stageLeft * scale),
                static_cast<LONG>(offsetY + stageTop * scale),
                static_cast<LONG>(offsetX + stageRight * scale),
                static_cast<LONG>(offsetY + stageBottom * scale),
            };
            context1->ClearView(target, black, &rect, 1);
        }
        if (context1) context1->Release();
        if (texture) texture->Release();
        if (resource) resource->Release();
        if (target) target->Release();
        return;
    }

    if (g_renderer.load() == Renderer::D3D9 && g_dx9Device) {
        IDirect3DSurface9* target = nullptr;
        if (SUCCEEDED(g_dx9Device->GetRenderTarget(0, &target)) && target) {
            D3DSURFACE_DESC desc{};
            if (SUCCEEDED(target->GetDesc(&desc))) {
                const float scale = std::min(desc.Width / referenceWidth, desc.Height / referenceHeight);
                const float offsetX = (desc.Width - referenceWidth * scale) * 0.5f;
                const float offsetY = (desc.Height - referenceHeight * scale) * 0.5f;
                const D3DRECT rect = {
                    static_cast<LONG>(offsetX + stageLeft * scale),
                    static_cast<LONG>(offsetY + stageTop * scale),
                    static_cast<LONG>(offsetX + stageRight * scale),
                    static_cast<LONG>(offsetY + stageBottom * scale),
                };
                g_dx9Device->Clear(1, &rect, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
            }
            target->Release();
        }
    }
}

bool InstallBootstrapHook()
{
    if (g_realGetProcAddress)
        return true;
    const bool installed = PatchMainImport("GetProcAddress", reinterpret_cast<void*>(HookGetProcAddress),
        reinterpret_cast<void**>(&g_realGetProcAddress));
    DebugMessage(installed ? L"GetProcAddress bootstrap hook installed" : L"GetProcAddress IAT hook failed");
    return installed;
}

DWORD WINAPI OverlayWorker(void*)
{
    InstallPracticeJumpHook();
    InstallPracticeMenuHook();
    InstallKeyboardInputHook();
    InstallGameOverlayHook();
    InstallCollisionCaptureHook();
    for (int i = 0; i < 400 && g_renderer.load() == Renderer::None; ++i) {
        RecoverAlreadyResolvedExports();
        Sleep(25);
    }

    wchar_t eventName[96]{};
    wsprintfW(eventName, L"Local\\th06nc_test_ready_%lu", GetCurrentProcessId());
    g_readyEvent = CreateEventW(nullptr, TRUE, TRUE, eventName);
    return 0;
}
