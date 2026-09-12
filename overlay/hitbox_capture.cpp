#include "hitbox_capture.h"
#include "game_addresses.h"
#include "locale.h"
#include "overlay.h"
#include "practice_menu.h"

#include "imgui.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr size_t kBulletUpdatePrologueSize = 8;
constexpr unsigned char kExpectedBulletUpdatePrologue[kBulletUpdatePrologueSize] = {
    0x48, 0x8B, 0xC4,             // mov rax, rsp
    0x53,                         // push rbx
    0x41, 0x54,                   // push r12
    0x41, 0x57                    // push r15
};
constexpr size_t kCollisionPrologueSize = 6;
constexpr unsigned char kExpectedCollisionPrologue[kCollisionPrologueSize] = {
    0x40, 0x53,                   // push rbx
    0x48, 0x83, 0xEC, 0x70       // sub rsp, 70h
};
constexpr size_t kLaserCollisionPrologueSize = 7;
constexpr unsigned char kExpectedLaserCollisionPrologue[kLaserCollisionPrologueSize] = {
    0x48, 0x8B, 0xC4,             // mov rax, rsp
    0x48, 0x89, 0x58, 0x08        // mov [rax+8], rbx
};
constexpr size_t kStageDrawPrologueSize = 5;
constexpr unsigned char kExpectedStageDrawPrologue[kStageDrawPrologueSize] = {
    0x48, 0x89, 0x5C, 0x24, 0x08 // mov [rsp+8], rbx
};
constexpr size_t kMaximumHitboxesPerFrame = 32768;
constexpr size_t kBulletCount = 0x280;
constexpr size_t kBulletStride = 0x620;

struct Float2 {
    float x;
    float y;
};

struct Hitbox {
    Float2 position;
    Float2 size;
    Float2 rotationPivot;
    float rotation;
    bool circular;
    bool rotated;
};

enum class CaptureStatus : LONG {
    NotInstalled,
    Installed,
    PoolOnly,
    CollisionFallbackOnly,
    UnsupportedExecutable,
    AllocationFailed,
    PatchFailed,
};

enum class BackgroundStatus : LONG {
    NotInstalled,
    Installed,
    UnsupportedExecutable,
    PatchFailed,
};

using CollisionFn = int(__fastcall*)(void* playerContext, const Float2* position,
    const Float2* size, bool circular);
using LaserCollisionFn = int(__fastcall*)(void* context, const Float2* center,
    const Float2* size, const Float2* rotationPivot, float rotation,
    bool enableGraze);
using BulletUpdateFn = int(__fastcall*)(void* bulletManager);
using StageDrawFn = int(__fastcall*)(void* stage);

CollisionFn g_originalCollision = nullptr;
LaserCollisionFn g_originalLaserCollision = nullptr;
BulletUpdateFn g_originalBulletUpdate = nullptr;
StageDrawFn g_originalStageDrawHigh = nullptr;
StageDrawFn g_originalStageDrawLow = nullptr;
void* g_bulletManager = nullptr;
bool g_poolHookInstalled = false;
CaptureStatus g_status = CaptureStatus::NotInstalled;
BackgroundStatus g_backgroundStatus = BackgroundStatus::NotInstalled;
bool g_disableStageBackground = false;
std::vector<Hitbox> g_pendingHitboxes;
std::vector<Hitbox> g_renderHitboxes;

bool g_showHitboxes = false;
bool g_fillHitboxes = true;
bool g_showCenters = false;
bool g_showSizeLabels = false;
bool g_flipY = false;
float g_stageOriginX = 128.0f;
float g_stageOriginY = 16.0f;
float g_pixelOffsetX = 4.0f;
float g_pixelOffsetY = 0.0f;
float g_scaleMultiplier = 1.0f;
float g_lineThickness = 1.5f;
int g_shapeDisplayMode = 0;
float g_hitboxColor[4] = {1.0f, 0.25f, 0.25f, 0.96f};
bool g_hitboxConfigLoaded = false;

std::wstring HitboxConfigPath()
{
    wchar_t appData[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(
        L"APPDATA", appData, static_cast<DWORD>(_countof(appData)));
    if (!length || length >= _countof(appData))
        return {};
    const std::wstring publisher =
        std::wstring(appData, length) + L"\\shanghaialice";
    const std::wstring game = publisher + L"\\th06nc";
    CreateDirectoryW(publisher.c_str(), nullptr);
    CreateDirectoryW(game.c_str(), nullptr);
    return game + L"\\input.ini";
}

float ReadConfigFloat(const std::wstring& path, const wchar_t* key,
    float fallback)
{
    wchar_t fallbackText[32]{};
    wchar_t value[64]{};
    swprintf_s(fallbackText, L"%.9g", fallback);
    GetPrivateProfileStringW(L"Hitbox", key, fallbackText, value,
        static_cast<DWORD>(_countof(value)), path.c_str());
    wchar_t* end = nullptr;
    const float result = wcstof(value, &end);
    return end != value && std::isfinite(result) ? result : fallback;
}

void WriteConfigFloat(const std::wstring& path, const wchar_t* key,
    float value)
{
    wchar_t text[32]{};
    swprintf_s(text, L"%.9g", value);
    WritePrivateProfileStringW(L"Hitbox", key, text, path.c_str());
}

void SaveHitboxConfig()
{
    const std::wstring path = HitboxConfigPath();
    if (path.empty())
        return;
    WriteConfigFloat(path, L"OffsetX", g_pixelOffsetX);
    WriteConfigFloat(path, L"OffsetY", g_pixelOffsetY);
    WriteConfigFloat(path, L"Scale", g_scaleMultiplier);
    WriteConfigFloat(path, L"ColorR", g_hitboxColor[0]);
    WriteConfigFloat(path, L"ColorG", g_hitboxColor[1]);
    WriteConfigFloat(path, L"ColorB", g_hitboxColor[2]);
    WriteConfigFloat(path, L"ColorA", g_hitboxColor[3]);
}

void LoadHitboxConfig()
{
    if (g_hitboxConfigLoaded)
        return;
    g_hitboxConfigLoaded = true;
    const std::wstring path = HitboxConfigPath();
    if (path.empty())
        return;
    g_pixelOffsetX = std::clamp(
        ReadConfigFloat(path, L"OffsetX", 4.0f), -1000.0f, 1000.0f);
    g_pixelOffsetY = std::clamp(
        ReadConfigFloat(path, L"OffsetY", 0.0f), -1000.0f, 1000.0f);
    g_scaleMultiplier = std::clamp(
        ReadConfigFloat(path, L"Scale", 1.0f), 0.1f, 5.0f);
    constexpr float defaults[4] = {1.0f, 0.25f, 0.25f, 0.90f};
    constexpr const wchar_t* keys[4] = {
        L"ColorR", L"ColorG", L"ColorB", L"ColorA"};
    for (size_t i = 0; i < 4; ++i)
        g_hitboxColor[i] = std::clamp(
            ReadConfigFloat(path, keys[i], defaults[i]), 0.0f, 1.0f);
    SaveHitboxConfig();
}

bool IsFiniteHitbox(const Hitbox& hitbox)
{
    return std::isfinite(hitbox.position.x) && std::isfinite(hitbox.position.y) &&
        std::isfinite(hitbox.size.x) && std::isfinite(hitbox.size.y) &&
        std::isfinite(hitbox.rotationPivot.x) &&
        std::isfinite(hitbox.rotationPivot.y) &&
        std::isfinite(hitbox.rotation) &&
        std::abs(hitbox.position.x) < 100000.0f && std::abs(hitbox.position.y) < 100000.0f &&
        std::abs(hitbox.size.x) < 100000.0f && std::abs(hitbox.size.y) < 100000.0f;
}

bool IsPracticeRunActive()
{
    const auto* practiceFlag =
        ResolveGameAddress<uint8_t>(GameAddress::PracticeModeFlag);
    return IsEnhancedPracticeRunActive() ||
        (practiceFlag && *practiceFlag != 0);
}

int __fastcall CaptureCollision(void* playerContext, const Float2* position,
    const Float2* size, bool circular)
{
    Hitbox hitbox{};
    bool valid = playerContext && position && size;
    if (valid) {
        hitbox.position = *position;
        hitbox.size = *size;
        hitbox.circular = circular;
        valid = IsFiniteHitbox(hitbox);
    }

    // The standard circular bullets are read from the complete pool at Present.
    // Keep this hook for rectangular/laser checks and as a fallback on game updates.
    if (valid && g_showHitboxes && IsPracticeRunActive() &&
        (!circular || !g_poolHookInstalled)) {
        if (g_pendingHitboxes.size() < g_pendingHitboxes.capacity())
            g_pendingHitboxes.push_back(hitbox);
    }

    return g_originalCollision(playerContext, position, size, circular);
}

int __fastcall CaptureLaserCollision(void* context, const Float2* center,
    const Float2* size, const Float2* rotationPivot, float rotation,
    bool enableGraze)
{
    if (center && size && rotationPivot &&
        g_showHitboxes &&
        IsPracticeRunActive()) {
        Hitbox hitbox{};
        hitbox.position = *center;
        hitbox.size = *size;
        hitbox.rotationPivot = *rotationPivot;
        hitbox.rotation = rotation;
        hitbox.rotated = true;
        if (IsFiniteHitbox(hitbox)) {
            if (g_pendingHitboxes.size() < g_pendingHitboxes.capacity())
                g_pendingHitboxes.push_back(hitbox);
        }
    }
    return g_originalLaserCollision(context, center, size, rotationPivot,
        rotation, enableGraze);
}

int __fastcall CaptureBulletManager(void* bulletManager)
{
    g_bulletManager = bulletManager;
    return g_originalBulletUpdate(bulletManager);
}

int __fastcall FilterStageDrawHigh(void* stage)
{
    if (g_disableStageBackground) {
        ClearStagePlayfieldBlack();
        return 1;
    }
    return g_originalStageDrawHigh(stage);
}

int __fastcall FilterStageDrawLow(void* stage)
{
    if (g_disableStageBackground)
        return 1;
    return g_originalStageDrawLow(stage);
}

void* AllocateNearAddress(void* target, size_t size)
{
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const uintptr_t targetAddress = reinterpret_cast<uintptr_t>(target);
    const uintptr_t minimumApplication = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
    const uintptr_t maximumApplication = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
    const uintptr_t reach = static_cast<uintptr_t>(std::numeric_limits<int32_t>::max()) - 0x10000;
    const uintptr_t low = targetAddress > reach
        ? std::max(minimumApplication, targetAddress - reach)
        : minimumApplication;
    const uintptr_t high = std::min(maximumApplication, targetAddress + reach);
    const uintptr_t granularity = info.dwAllocationGranularity;

    uintptr_t cursor = low;
    while (cursor < high) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &memory, sizeof(memory)))
            break;
        const uintptr_t regionBase = reinterpret_cast<uintptr_t>(memory.BaseAddress);
        const uintptr_t regionEnd = regionBase + memory.RegionSize;
        if (memory.State == MEM_FREE) {
            uintptr_t candidate = std::max(cursor, regionBase);
            candidate = (candidate + granularity - 1) & ~(granularity - 1);
            if (candidate <= high && size <= regionEnd - candidate) {
                if (void* block = VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE))
                    return block;
            }
        }
        if (regionEnd <= cursor)
            break;
        cursor = regionEnd;
    }
    return nullptr;
}

void WriteAbsoluteJump(unsigned char* destination, const void* target)
{
    // jmp qword ptr [rip+0]; followed by the full 64-bit destination.
    destination[0] = 0xFF;
    destination[1] = 0x25;
    *reinterpret_cast<uint32_t*>(destination + 2) = 0;
    *reinterpret_cast<uintptr_t*>(destination + 6) = reinterpret_cast<uintptr_t>(target);
}

bool InstallKnownPrologueDetour(void* target, void* replacement, void** original,
    const unsigned char* expectedPrologue, size_t prologueSize)
{
    auto* targetBytes = static_cast<unsigned char*>(target);
    if (prologueSize < 5 || prologueSize > 16 ||
        std::memcmp(targetBytes, expectedPrologue, prologueSize) != 0)
        return false;

    auto* block = static_cast<unsigned char*>(AllocateNearAddress(target, 64));
    if (!block) {
        g_status = CaptureStatus::AllocationFailed;
        return false;
    }

    unsigned char* relay = block;
    unsigned char* trampoline = block + 16;
    WriteAbsoluteJump(relay, replacement);
    std::memcpy(trampoline, targetBytes, prologueSize);
    WriteAbsoluteJump(trampoline + prologueSize, targetBytes + prologueSize);

    DWORD oldBlockProtection = 0;
    if (!VirtualProtect(block, 64, PAGE_EXECUTE_READ, &oldBlockProtection)) {
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }

    const intptr_t relative = reinterpret_cast<intptr_t>(relay) -
        (reinterpret_cast<intptr_t>(targetBytes) + 5);
    if (relative < std::numeric_limits<int32_t>::min() || relative > std::numeric_limits<int32_t>::max()) {
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }

    DWORD oldTargetProtection = 0;
    if (!VirtualProtect(targetBytes, prologueSize, PAGE_EXECUTE_READWRITE, &oldTargetProtection)) {
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }
    // Publish the trampoline before making the detour reachable.
    *original = trampoline;
    targetBytes[0] = 0xE9;
    *reinterpret_cast<int32_t*>(targetBytes + 1) = static_cast<int32_t>(relative);
    std::memset(targetBytes + 5, 0x90, prologueSize - 5);
    FlushInstructionCache(GetCurrentProcess(), targetBytes, prologueSize);
    DWORD ignored = 0;
    VirtualProtect(targetBytes, prologueSize, oldTargetProtection, &ignored);

    return true;
}

void ConsumeHitboxes()
{
    g_renderHitboxes.clear();
    g_renderHitboxes.swap(g_pendingHitboxes);
}

bool IsReadable(const void* address, size_t size)
{
    const auto start = reinterpret_cast<uintptr_t>(address);
    const auto end = start + size;
    uintptr_t cursor = start;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &memory, sizeof(memory)) ||
            memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;
        const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize;
        if (regionEnd <= cursor)
            return false;
        cursor = std::min(regionEnd, end);
    }
    return true;
}

void AppendCompleteBulletPool()
{
    auto* manager = static_cast<const std::byte*>(g_bulletManager);
    const size_t poolBytes = 8 + kBulletCount * kBulletStride;
    if (!manager || !IsReadable(manager, poolBytes))
        return;

    const std::byte* bullet = manager + 8;
    for (size_t i = 0; i < kBulletCount; ++i, bullet += kBulletStride) {
        const uint16_t state = *reinterpret_cast<const uint16_t*>(bullet + 0x44);
        // Only state 1 reaches the normal collision path. +0x618 is the
        // "graze phase completed" flag, not a collision-enable flag; filtering
        // by it would hide every bullet until the player grazed it once.
        if (state != 1)
            continue;

        Hitbox hitbox{};
        hitbox.position = *reinterpret_cast<const Float2*>(bullet + 0x30);
        hitbox.size = *reinterpret_cast<const Float2*>(bullet + 0x5F4);
        hitbox.circular = true; // The standard bullet call site loads r9d from r12d, where r12d == 1.
        if (IsFiniteHitbox(hitbox) && g_renderHitboxes.size() < g_renderHitboxes.capacity())
            g_renderHitboxes.push_back(hitbox);
    }
}

struct ScreenTransform {
    float scaleX;
    float scaleY;
    ImVec2 origin;
};

ScreenTransform CalculateTransform()
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float baseScale = std::min(display.x / 640.0f, display.y / 480.0f);
    const float scale = std::max(0.001f, baseScale * g_scaleMultiplier);
    const ImVec2 letterbox(
        (display.x - 640.0f * baseScale) * 0.5f,
        (display.y - 480.0f * baseScale) * 0.5f);
    ImVec2 origin(letterbox.x + g_stageOriginX * scale + g_pixelOffsetX,
        letterbox.y + g_stageOriginY * scale + g_pixelOffsetY);
    float scaleX = scale;
    if (IsGameStretchModeEnabled()) {
        constexpr float stretch = 4.0f / 3.0f;
        // The post-process maps output UV x to 0.25 + 0.75*x. Its inverse
        // keeps the right edge fixed and moves every source point/extent by
        // the same 4/3 horizontal transform.
        origin.x = display.x + (origin.x - display.x) * stretch;
        scaleX *= stretch;
    }
    return {scaleX, scale, origin};
}

ImVec2 ToScreen(const Float2& position, const ScreenTransform& transform)
{
    const float y = g_flipY ? 448.0f - position.y : position.y;
    return ImVec2(transform.origin.x + position.x * transform.scaleX,
        transform.origin.y + y * transform.scaleY);
}

void DrawEllipse(ImDrawList* draw, const ImVec2& center, float radiusX,
    float radiusY, ImU32 outline, ImU32 fill, bool filled, float thickness)
{
    if (radiusX <= 0.0f || radiusY <= 0.0f)
        return;
    constexpr int segments = 32;
    ImVec2 points[segments]{};
    constexpr float twoPi = 6.2831853071795864769f;
    for (int i = 0; i < segments; ++i) {
        const float angle = twoPi * static_cast<float>(i) /
            static_cast<float>(segments);
        points[i] = ImVec2(center.x + std::cos(angle) * radiusX,
            center.y + std::sin(angle) * radiusY);
    }
    if (filled)
        draw->AddConvexPolyFilled(points, segments, fill);
    draw->AddPolyline(points, segments, outline, ImDrawFlags_Closed,
        thickness);
}

Float2 RotateAround(const Float2& point, const Float2& pivot, float angle)
{
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);
    const float x = point.x - pivot.x;
    const float y = point.y - pivot.y;
    return {
        pivot.x + cosine * x - sine * y,
        pivot.y + sine * x + cosine * y,
    };
}

bool ReadPlayerHitbox(Float2& position, float& radius)
{
    const auto* moduleBase = GameModuleBase();
    const auto* player = moduleBase
        ? moduleBase + GameRva(GameAddress::PlayerObject) : nullptr;
    if (!player ||
        !IsReadable(player + GameField(GameObjectField::PlayerPosition),
            sizeof(Float2)) ||
        !IsReadable(player + GameField(GameObjectField::PlayerCollisionRadius),
            sizeof(float)))
        return false;

    position = *reinterpret_cast<const Float2*>(
        player + GameField(GameObjectField::PlayerPosition));
    radius = *reinterpret_cast<const float*>(
        player + GameField(GameObjectField::PlayerCollisionRadius));
    return std::isfinite(position.x) && std::isfinite(position.y) &&
        std::isfinite(radius) && std::abs(position.x) < 100000.0f &&
        std::abs(position.y) < 100000.0f && radius > 0.0f && radius < 1000.0f;
}

void DrawSizeLabel(ImDrawList* draw, const ImVec2& center, float top,
    const char* text, ImU32 color)
{
    ImVec2 textSize = ImGui::CalcTextSize(text);
    textSize.x *= 2;
    textSize.y *= 2;
    const ImVec2 textPosition(center.x - textSize.x * 0.5f, top - textSize.y - 2.0f);
    draw->AddText(ImVec2(textPosition.x + 1.0f, textPosition.y + 1.0f),
        IM_COL32(0, 0, 0, 220), text);
    draw->AddText(textPosition, color, text);
}

} // namespace

bool InstallCollisionCaptureHook()
{
    LoadHitboxConfig();
    if (g_status == CaptureStatus::Installed)
        return true;

    auto* base = GameModuleBase();
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        g_status = CaptureStatus::UnsupportedExecutable;
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.SizeOfImage < std::max(
            std::max(
                std::max(GameRva(GameAddress::CollisionTest) + kCollisionPrologueSize,
                    GameRva(GameAddress::LaserCollisionTest) +
                        kLaserCollisionPrologueSize),
                GameRva(GameAddress::BulletManagerUpdate) + kBulletUpdatePrologueSize),
            GameRva(GameAddress::StageDrawLow) + kStageDrawPrologueSize)) {
        g_status = CaptureStatus::UnsupportedExecutable;
        return false;
    }
    
    g_pendingHitboxes.reserve(kMaximumHitboxesPerFrame);
    g_renderHitboxes.reserve(kMaximumHitboxesPerFrame);
    void* updateTarget = base + GameRva(GameAddress::BulletManagerUpdate);
    void* collisionTarget = base + GameRva(GameAddress::CollisionTest);
    void* laserCollisionTarget = base + GameRva(GameAddress::LaserCollisionTest);
    void* stageDrawHighTarget = base + GameRva(GameAddress::StageDrawHigh);
    void* stageDrawLowTarget = base + GameRva(GameAddress::StageDrawLow);
    const bool updateMatches = std::memcmp(updateTarget, kExpectedBulletUpdatePrologue,
        kBulletUpdatePrologueSize) == 0;
    const bool collisionMatches = std::memcmp(collisionTarget, kExpectedCollisionPrologue,
        kCollisionPrologueSize) == 0;
    const bool laserCollisionMatches = std::memcmp(laserCollisionTarget,
        kExpectedLaserCollisionPrologue, kLaserCollisionPrologueSize) == 0;

    const bool updateInstalled = updateMatches && InstallKnownPrologueDetour(
        updateTarget, reinterpret_cast<void*>(CaptureBulletManager),
        reinterpret_cast<void**>(&g_originalBulletUpdate), kExpectedBulletUpdatePrologue,
        kBulletUpdatePrologueSize);
    g_poolHookInstalled = updateInstalled;

    const bool collisionInstalled = collisionMatches && InstallKnownPrologueDetour(
        collisionTarget, reinterpret_cast<void*>(CaptureCollision),
        reinterpret_cast<void**>(&g_originalCollision), kExpectedCollisionPrologue,
        kCollisionPrologueSize);
    const bool laserCollisionInstalled = laserCollisionMatches &&
        InstallKnownPrologueDetour(laserCollisionTarget,
            reinterpret_cast<void*>(CaptureLaserCollision),
            reinterpret_cast<void**>(&g_originalLaserCollision),
            kExpectedLaserCollisionPrologue, kLaserCollisionPrologueSize);

    const bool stageDrawHighMatches = std::memcmp(stageDrawHighTarget,
        kExpectedStageDrawPrologue, kStageDrawPrologueSize) == 0;
    const bool stageDrawLowMatches = std::memcmp(stageDrawLowTarget,
        kExpectedStageDrawPrologue, kStageDrawPrologueSize) == 0;
    const bool stageDrawHighInstalled = stageDrawHighMatches && InstallKnownPrologueDetour(
        stageDrawHighTarget, reinterpret_cast<void*>(FilterStageDrawHigh),
        reinterpret_cast<void**>(&g_originalStageDrawHigh), kExpectedStageDrawPrologue,
        kStageDrawPrologueSize);
    const bool stageDrawLowInstalled = stageDrawLowMatches && InstallKnownPrologueDetour(
        stageDrawLowTarget, reinterpret_cast<void*>(FilterStageDrawLow),
        reinterpret_cast<void**>(&g_originalStageDrawLow), kExpectedStageDrawPrologue,
        kStageDrawPrologueSize);
    if (stageDrawHighInstalled && stageDrawLowInstalled)
        g_backgroundStatus = BackgroundStatus::Installed;
    else if (!stageDrawHighMatches || !stageDrawLowMatches)
        g_backgroundStatus = BackgroundStatus::UnsupportedExecutable;
    else
        g_backgroundStatus = BackgroundStatus::PatchFailed;

    if (updateInstalled && laserCollisionInstalled)
        g_status = CaptureStatus::Installed;
    else if (updateInstalled)
        g_status = CaptureStatus::PoolOnly;
    else if (collisionInstalled || laserCollisionInstalled)
        g_status = CaptureStatus::CollisionFallbackOnly;
    else if (!updateMatches && !collisionMatches && !laserCollisionMatches)
        g_status = CaptureStatus::UnsupportedExecutable;
    else
        g_status = CaptureStatus::PatchFailed;
    return updateInstalled || collisionInstalled || laserCollisionInstalled;
}

const char* CollisionCaptureStatus()
{
    switch (g_status) {
    case CaptureStatus::Installed: return "active: full bullet pool + rotated lasers";
    case CaptureStatus::PoolOnly: return "partial: full bullet pool; laser hook unavailable";
    case CaptureStatus::CollisionFallbackOnly: return "partial: collision-call capture only";
    case CaptureStatus::UnsupportedExecutable: return "disabled: executable/prologue mismatch";
    case CaptureStatus::AllocationFailed: return "disabled: x64 relay allocation failed";
    case CaptureStatus::PatchFailed: return "disabled: code patch failed";
    default: return "not installed";
    }
}

void DrawCapturedHitboxes()
{
    ConsumeHitboxes();
    if (!g_showHitboxes ||
        !IsPracticeRunActive())
        return;
    AppendCompleteBulletPool();

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ScreenTransform transform = CalculateTransform();
    const ImU32 outline = ImGui::ColorConvertFloat4ToU32(ImVec4(
        g_hitboxColor[0], g_hitboxColor[1], g_hitboxColor[2],
        g_hitboxColor[3]));
    const ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImVec4(
        g_hitboxColor[0], g_hitboxColor[1], g_hitboxColor[2],
        g_hitboxColor[3] * 0.20f));
    const ImU32 center = IM_COL32(255, 255, 255, 220);
    const ImU32 labelColor = IM_COL32(255, 255, 255, 245);

    for (const Hitbox& hitbox : g_renderHitboxes) {
        const Float2 worldCenter = hitbox.rotated
            ? RotateAround(hitbox.position, hitbox.rotationPivot,
                hitbox.rotation)
            : hitbox.position;
        const ImVec2 position = ToScreen(worldCenter, transform);
        const float width = std::abs(hitbox.size.x);
        const float height = std::abs(hitbox.size.y);
        const float rawHalfWidth = width * 0.5f;
        const float rawHalfHeight = height * 0.5f;
        const float equalityTolerance = std::max(0.01f, std::max(width, height) * 0.01f);
        const bool visuallyCircular = !hitbox.rotated &&
            (hitbox.circular || g_shapeDisplayMode == 2 ||
                (g_shapeDisplayMode == 1 &&
                    std::abs(width - height) <= equalityTolerance));
        float labelTop = position.y;
        char sizeText[64]{};
        if (visuallyCircular) {
            const float rawRadius = std::min(rawHalfWidth, rawHalfHeight);
            const float radiusX = rawRadius * transform.scaleX;
            const float radiusY = rawRadius * transform.scaleY;
            DrawEllipse(draw, position, radiusX, radiusY, outline, fill,
                g_fillHitboxes, g_lineThickness);
            labelTop = position.y - radiusY;
            std::snprintf(sizeText, sizeof(sizeText), "%.3f", rawRadius);
        } else {
            const float halfWidth = rawHalfWidth;
            const float halfHeight = rawHalfHeight;
            if (hitbox.rotated) {
                const Float2 cornersWorld[4] = {
                    RotateAround({hitbox.position.x - halfWidth,
                                     hitbox.position.y - halfHeight},
                        hitbox.rotationPivot, hitbox.rotation),
                    RotateAround({hitbox.position.x + halfWidth,
                                     hitbox.position.y - halfHeight},
                        hitbox.rotationPivot, hitbox.rotation),
                    RotateAround({hitbox.position.x + halfWidth,
                                     hitbox.position.y + halfHeight},
                        hitbox.rotationPivot, hitbox.rotation),
                    RotateAround({hitbox.position.x - halfWidth,
                                     hitbox.position.y + halfHeight},
                        hitbox.rotationPivot, hitbox.rotation),
                };
                ImVec2 corners[4]{};
                labelTop = std::numeric_limits<float>::max();
                for (size_t i = 0; i < 4; ++i) {
                    corners[i] = ToScreen(cornersWorld[i], transform);
                    labelTop = std::min(labelTop, corners[i].y);
                }
                if (g_fillHitboxes)
                    draw->AddConvexPolyFilled(corners, 4, fill);
                draw->AddPolyline(corners, 4, outline,
                    ImDrawFlags_Closed, g_lineThickness);
            } else {
                const ImVec2 minimum(
                    position.x - halfWidth * transform.scaleX,
                    position.y - halfHeight * transform.scaleY);
                const ImVec2 maximum(
                    position.x + halfWidth * transform.scaleX,
                    position.y + halfHeight * transform.scaleY);
                if (g_fillHitboxes)
                    draw->AddRectFilled(minimum, maximum, fill, 0.0f);
                draw->AddRect(minimum, maximum, outline, 0.0f, 0,
                    g_lineThickness);
                labelTop = minimum.y;
            }
            std::snprintf(sizeText, sizeof(sizeText), "(%.3f,%.3f)",
                rawHalfWidth, rawHalfHeight);
        }

        if (g_showSizeLabels)
            DrawSizeLabel(draw, position, labelTop, sizeText, labelColor);

        if (g_showCenters) {
            draw->AddLine(ImVec2(position.x - 3.0f, position.y), ImVec2(position.x + 3.0f, position.y), center);
            draw->AddLine(ImVec2(position.x, position.y - 3.0f), ImVec2(position.x, position.y + 3.0f), center);
        }
    }

    Float2 playerPosition{};
    float playerRadius = 0.0f;
    if (ReadPlayerHitbox(playerPosition, playerRadius)) {
        const ImVec2 position = ToScreen(playerPosition, transform);
        const float radiusX = playerRadius * transform.scaleX;
        const float radiusY = playerRadius * transform.scaleY;
        const float grazeRadius = playerRadius + 20.0f;
        DrawEllipse(draw, position, grazeRadius * transform.scaleX,
            grazeRadius * transform.scaleY, IM_COL32(255, 255, 255, 255),
            0, false, 2.0f);
        DrawEllipse(draw, position, radiusX, radiusY, outline, fill,
            g_fillHitboxes, g_lineThickness);
        if (g_showSizeLabels) {
            char sizeText[32]{};
            std::snprintf(sizeText, sizeof(sizeText), "%.1f", playerRadius);
            DrawSizeLabel(draw, position, position.y - radiusY, sizeText, labelColor);
        }
        if (g_showCenters) {
            draw->AddLine(ImVec2(position.x - 3.0f, position.y), ImVec2(position.x + 3.0f, position.y), center);
            draw->AddLine(ImVec2(position.x, position.y - 3.0f), ImVec2(position.x, position.y + 3.0f), center);
        }
    }
}

void DrawHitboxSettingsUi()
{
    size_t circleCount = 0;
    for (const Hitbox& hitbox : g_renderHitboxes)
        circleCount += hitbox.circular ? 1u : 0u;
    const size_t rectangleCount = g_renderHitboxes.size() - circleCount;

    ImGui::Separator();
    ImGui::TextUnformatted("Stage visibility");
    const char* backgroundStatus = "not installed";
    switch (g_backgroundStatus) {
    case BackgroundStatus::Installed: backgroundStatus = "active"; break;
    case BackgroundStatus::UnsupportedExecutable: backgroundStatus = "disabled: executable/prologue mismatch"; break;
    case BackgroundStatus::PatchFailed: backgroundStatus = "disabled: code patch failed"; break;
    default: break;
    }
    ImGui::Text("Background hook: %s", backgroundStatus);
    bool disableStageBackground = g_disableStageBackground;
    if (ImGui::Checkbox("Black stage background", &disableStageBackground))
        g_disableStageBackground = disableStageBackground;

    ImGui::Separator();
    ImGui::TextUnformatted("Bullet collision overlay");
    ImGui::Text("Capture: %s", CollisionCaptureStatus());
    ImGui::Text("Captured this render frame: %d", static_cast<int>(g_renderHitboxes.size()));
    ImGui::Text("Game shape flags: circle %d | rectangle %d",
        static_cast<int>(circleCount), static_cast<int>(rectangleCount));
    bool showHitboxes = g_showHitboxes;
    if (ImGui::Checkbox("Show hitboxes", &showHitboxes))
        g_showHitboxes = showHitboxes;
    ImGui::SameLine();
    ImGui::Checkbox("Fill", &g_fillHitboxes);
    ImGui::Checkbox("Show centers", &g_showCenters);
    ImGui::SameLine();
    ImGui::Checkbox("Show size labels", &g_showSizeLabels);
    ImGui::Checkbox("Flip stage Y", &g_flipY);
    constexpr ImGuiColorEditFlags colorFlags =
        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;
    ImGui::ColorEdit4("Hitbox color", g_hitboxColor, colorFlags);
    ImGui::TextDisabled("Outlines show raw bullet radii/half-extents without adding the player radius.");
    const char* shapeModes[] = {
        "Exact game branch",
        "Equal width/height as circle",
        "Force all as circles",
    };
    ImGui::Combo("Display shape", &g_shapeDisplayMode, shapeModes, IM_ARRAYSIZE(shapeModes));
    if (g_shapeDisplayMode != 0)
        ImGui::TextDisabled("Visualization override; collision still uses the game's original branch.");

    if (ImGui::TreeNode("Position calibration")) {
        ImGui::TextDisabled("Default: 640x480 reference, stage origin (128,16), pixel X +4");
        ImGui::DragFloat("Stage origin X", &g_stageOriginX, 0.25f, -640.0f, 640.0f, "%.2f");
        ImGui::DragFloat("Stage origin Y", &g_stageOriginY, 0.25f, -480.0f, 480.0f, "%.2f");
        ImGui::DragFloat("Pixel offset X", &g_pixelOffsetX, 0.25f, -1000.0f, 1000.0f, "%.2f");
        ImGui::DragFloat("Pixel offset Y", &g_pixelOffsetY, 0.25f, -1000.0f, 1000.0f, "%.2f");
        ImGui::SliderFloat("Scale multiplier", &g_scaleMultiplier, 0.1f, 5.0f, "%.3f");
        ImGui::SliderFloat("Line thickness", &g_lineThickness, 0.5f, 5.0f, "%.1f");
        ImGui::TreePop();
    }
}

bool IsHitboxDisplayEnabled()
{
    return g_showHitboxes;
}

bool IsHitboxDisplayActive()
{
    return g_showHitboxes && IsPracticeRunActive();
}

void SetHitboxDisplayEnabled(bool enabled)
{
    LoadHitboxConfig();
    if (g_showHitboxes == enabled)
        return;
    g_showHitboxes = enabled;
    SaveHitboxConfig();
}

void DrawHitboxDisplayControlsUi()
{
    LoadHitboxConfig();
    bool changed = false;
    float offset[2] = {g_pixelOffsetX, g_pixelOffsetY};
    ImGui::SetNextItemWidth(300.0f);
    if (ImGui::DragFloat2(S(HitboxOffset), offset, 0.25f,
            -1000.0f, 1000.0f, "%.2f")) {
        g_pixelOffsetX = offset[0];
        g_pixelOffsetY = offset[1];
        changed = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    changed |= ImGui::DragFloat(S(HitboxScale), &g_scaleMultiplier,
        0.005f, 0.1f, 5.0f, "%.3f");

    constexpr ImGuiColorEditFlags colorFlags =
        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;
    ImGui::SetNextItemWidth(420.0f);
    changed |= ImGui::ColorEdit4(S(HitboxColor), g_hitboxColor, colorFlags);
    if (changed)
        SaveHitboxConfig();
}
