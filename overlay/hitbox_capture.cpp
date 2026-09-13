#include "hitbox_capture.h"
#include "game_addresses.h"
#include "locale.h"
#include "overlay.h"
#include "practice_menu.h"

#include "imgui.h"

#include <windows.h>

#include <algorithm>
#include <array>
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
constexpr unsigned char kRoundGrazeDistanceCombine[4] = {
    0xF3, 0x0F, 0x58, 0xC1 // addss xmm0, xmm1
};
constexpr unsigned char kSquareGrazeDistanceCombine[4] = {
    0xF3, 0x0F, 0x5F, 0xC1 // maxss xmm0, xmm1
};

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

struct HitboxHooks {
    CollisionFn originalCollision = nullptr;
    LaserCollisionFn originalLaserCollision = nullptr;
    BulletUpdateFn originalBulletUpdate = nullptr;
    StageDrawFn originalStageDrawHigh = nullptr;
    StageDrawFn originalStageDrawLow = nullptr;
    void* bulletManager = nullptr;
    bool poolHookInstalled = false;
    CaptureStatus status = CaptureStatus::NotInstalled;
    BackgroundStatus backgroundStatus = BackgroundStatus::NotInstalled;
};

struct HitboxSettings {
    bool show = false;
    bool square = false;
    bool fill = true;
    bool showCenters = false;
    bool showSizeLabels = false;
    bool flipY = false;
    bool disableStageBackground = false;
    float stageOriginX = 128.0f;
    float stageOriginY = 16.0f;
    float pixelOffsetX = 4.0f;
    float pixelOffsetY = 0.0f;
    float scaleMultiplier = 1.0f;
    float lineThickness = 1.5f;
    int shapeDisplayMode = 0;
    std::array<float, 4> color{1.0f, 0.25f, 0.25f, 0.96f};
    bool configLoaded = false;
};

struct HitboxRuntime {
    HitboxHooks hooks{};
    HitboxSettings settings{};
    std::vector<Hitbox> pending{};
    std::vector<Hitbox> render{};
};

HitboxRuntime g_hitbox{};

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
    WriteConfigFloat(path, L"OffsetX", g_hitbox.settings.pixelOffsetX);
    WriteConfigFloat(path, L"OffsetY", g_hitbox.settings.pixelOffsetY);
    WriteConfigFloat(path, L"Scale", g_hitbox.settings.scaleMultiplier);
    WriteConfigFloat(path, L"ColorR", g_hitbox.settings.color[0]);
    WriteConfigFloat(path, L"ColorG", g_hitbox.settings.color[1]);
    WriteConfigFloat(path, L"ColorB", g_hitbox.settings.color[2]);
    WriteConfigFloat(path, L"ColorA", g_hitbox.settings.color[3]);
}

void LoadHitboxConfig()
{
    if (g_hitbox.settings.configLoaded)
        return;
    g_hitbox.settings.configLoaded = true;
    const std::wstring path = HitboxConfigPath();
    if (path.empty())
        return;
    g_hitbox.settings.pixelOffsetX = std::clamp(
        ReadConfigFloat(path, L"OffsetX", 4.0f), -1000.0f, 1000.0f);
    g_hitbox.settings.pixelOffsetY = std::clamp(
        ReadConfigFloat(path, L"OffsetY", 0.0f), -1000.0f, 1000.0f);
    g_hitbox.settings.scaleMultiplier = std::clamp(
        ReadConfigFloat(path, L"Scale", 1.0f), 0.1f, 5.0f);
    constexpr float defaults[4] = {1.0f, 0.25f, 0.25f, 0.90f};
    constexpr const wchar_t* keys[4] = {
        L"ColorR", L"ColorG", L"ColorB", L"ColorA"};
    for (size_t i = 0; i < 4; ++i)
        g_hitbox.settings.color[i] = std::clamp(
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

bool ApplyBulletGrazeSquarePatch(bool enabled)
{
    auto* target = ResolveGameAddress<unsigned char>(
        GameAddress::BulletGrazeDistanceCombine);
    if (!target)
        return false;
    const auto& wanted = enabled
        ? kSquareGrazeDistanceCombine
        : kRoundGrazeDistanceCombine;
    if (std::memcmp(target, wanted, sizeof(wanted)) == 0)
        return true;
    const auto& expected = enabled
        ? kRoundGrazeDistanceCombine
        : kSquareGrazeDistanceCombine;
    if (std::memcmp(target, expected, sizeof(expected)) != 0)
        return false;

    DWORD oldProtection = 0;
    if (!VirtualProtect(target, sizeof(wanted), PAGE_EXECUTE_READWRITE,
            &oldProtection))
        return false;
    std::memcpy(target, wanted, sizeof(wanted));
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(wanted));
    DWORD ignored = 0;
    VirtualProtect(target, sizeof(wanted), oldProtection, &ignored);
    return true;
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
    if (valid && g_hitbox.settings.show && IsPracticeRunActive() &&
        (!circular || !g_hitbox.hooks.poolHookInstalled)) {
        if (g_hitbox.pending.size() < g_hitbox.pending.capacity())
            g_hitbox.pending.push_back(hitbox);
    }

    // Square mode changes only the player-versus-axis-aligned-shape test.
    // Keep calling the native routine so its hit/death/deathbomb and return
    // value behavior remains intact. The native circle tests are a subset of
    // the requested AABB test, so only an AABB hit in a formerly rounded
    // corner needs a temporarily enlarged player radius.
    float* playerRadius = valid
        ? reinterpret_cast<float*>(static_cast<std::byte*>(playerContext) +
              GameField(GameObjectField::PlayerCollisionRadius))
        : nullptr;
    if (g_hitbox.settings.square && playerRadius &&
        std::isfinite(*playerRadius) && *playerRadius >= 0.0f) {
        const auto* playerPosition = reinterpret_cast<const Float2*>(
            static_cast<const std::byte*>(playerContext) +
            GameField(GameObjectField::PlayerPosition));
        if (std::isfinite(playerPosition->x) &&
            std::isfinite(playerPosition->y)) {
            const float dx = std::abs(playerPosition->x - position->x);
            const float dy = std::abs(playerPosition->y - position->y);
            const float originalRadius = *playerRadius;
            const float objectHalfWidth = std::abs(size->x) * 0.5f;
            const float objectHalfHeight = std::abs(size->y) * 0.5f;
            const float squareHalfWidth = circular
                ? std::min(objectHalfWidth, objectHalfHeight)
                : objectHalfWidth;
            const float squareHalfHeight = circular
                ? squareHalfWidth
                : objectHalfHeight;
            const bool squareHit =
                dx < squareHalfWidth + originalRadius &&
                dy < squareHalfHeight + originalRadius;
            if (squareHit) {
                float requiredRadius = originalRadius;
                if (circular) {
                    requiredRadius = std::max(0.0f,
                        std::hypot(dx, dy) - squareHalfWidth);
                } else {
                    const float nearestDx =
                        std::max(0.0f, dx - objectHalfWidth);
                    const float nearestDy =
                        std::max(0.0f, dy - objectHalfHeight);
                    requiredRadius = std::hypot(nearestDx, nearestDy);
                }
                if (requiredRadius >= originalRadius)
                    *playerRadius = std::nextafter(requiredRadius,
                        std::numeric_limits<float>::infinity());
                const int result = g_hitbox.hooks.originalCollision(
                    playerContext, position, size, circular);
                *playerRadius = originalRadius;
                return result;
            }
        }
    }

    return g_hitbox.hooks.originalCollision(
        playerContext, position, size, circular);
}

int __fastcall CaptureLaserCollision(void* context, const Float2* center,
    const Float2* size, const Float2* rotationPivot, float rotation,
    bool enableGraze)
{
    if (center && size && rotationPivot &&
        g_hitbox.settings.show &&
        IsPracticeRunActive()) {
        Hitbox hitbox{};
        hitbox.position = *center;
        hitbox.size = *size;
        hitbox.rotationPivot = *rotationPivot;
        hitbox.rotation = rotation;
        hitbox.rotated = true;
        if (IsFiniteHitbox(hitbox)) {
            if (g_hitbox.pending.size() < g_hitbox.pending.capacity())
                g_hitbox.pending.push_back(hitbox);
        }
    }
    return g_hitbox.hooks.originalLaserCollision(context, center, size, rotationPivot,
        rotation, enableGraze);
}

int __fastcall CaptureBulletManager(void* bulletManager)
{
    g_hitbox.hooks.bulletManager = bulletManager;
    return g_hitbox.hooks.originalBulletUpdate(bulletManager);
}

int __fastcall FilterStageDrawHigh(void* stage)
{
    if (g_hitbox.settings.disableStageBackground) {
        ClearStagePlayfieldBlack();
        return 1;
    }
    return g_hitbox.hooks.originalStageDrawHigh(stage);
}

int __fastcall FilterStageDrawLow(void* stage)
{
    if (g_hitbox.settings.disableStageBackground)
        return 1;
    return g_hitbox.hooks.originalStageDrawLow(stage);
}

void* AllocateNearAddress(void* target, size_t size)
{
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const uintptr_t targetAddress = reinterpret_cast<uintptr_t>(target);
    const uintptr_t minimumApplication = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
    const uintptr_t maximumApplication = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
    constexpr uintptr_t reach = static_cast<uintptr_t>(std::numeric_limits<int32_t>::max()) - 0x10000;
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
        g_hitbox.hooks.status = CaptureStatus::AllocationFailed;
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
    g_hitbox.render.clear();
    g_hitbox.render.swap(g_hitbox.pending);
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
    auto* manager = static_cast<const std::byte*>(g_hitbox.hooks.bulletManager);
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
        if (IsFiniteHitbox(hitbox) &&
            g_hitbox.render.size() < g_hitbox.render.capacity())
            g_hitbox.render.push_back(hitbox);
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
    const float scale = std::max(
        0.001f, baseScale * g_hitbox.settings.scaleMultiplier);
    const ImVec2 letterbox(
        (display.x - 640.0f * baseScale) * 0.5f,
        (display.y - 480.0f * baseScale) * 0.5f);
    ImVec2 origin(
        letterbox.x + g_hitbox.settings.stageOriginX * scale +
            g_hitbox.settings.pixelOffsetX,
        letterbox.y + g_hitbox.settings.stageOriginY * scale +
            g_hitbox.settings.pixelOffsetY);
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
    const float y = g_hitbox.settings.flipY ? 448.0f - position.y : position.y;
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
    if (g_hitbox.hooks.status == CaptureStatus::Installed)
        return true;

    auto* base = GameModuleBase();
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        g_hitbox.hooks.status = CaptureStatus::UnsupportedExecutable;
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
        g_hitbox.hooks.status = CaptureStatus::UnsupportedExecutable;
        return false;
    }
    
    g_hitbox.pending.reserve(kMaximumHitboxesPerFrame);
    g_hitbox.render.reserve(kMaximumHitboxesPerFrame);
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
        reinterpret_cast<void**>(&g_hitbox.hooks.originalBulletUpdate),
        kExpectedBulletUpdatePrologue,
        kBulletUpdatePrologueSize);
    g_hitbox.hooks.poolHookInstalled = updateInstalled;

    const bool collisionInstalled = collisionMatches && InstallKnownPrologueDetour(
        collisionTarget, reinterpret_cast<void*>(CaptureCollision),
        reinterpret_cast<void**>(&g_hitbox.hooks.originalCollision),
        kExpectedCollisionPrologue,
        kCollisionPrologueSize);
    const bool laserCollisionInstalled = laserCollisionMatches &&
        InstallKnownPrologueDetour(laserCollisionTarget,
            reinterpret_cast<void*>(CaptureLaserCollision),
            reinterpret_cast<void**>(&g_hitbox.hooks.originalLaserCollision),
            kExpectedLaserCollisionPrologue, kLaserCollisionPrologueSize);

    const bool stageDrawHighMatches = std::memcmp(stageDrawHighTarget,
        kExpectedStageDrawPrologue, kStageDrawPrologueSize) == 0;
    const bool stageDrawLowMatches = std::memcmp(stageDrawLowTarget,
        kExpectedStageDrawPrologue, kStageDrawPrologueSize) == 0;
    const bool stageDrawHighInstalled = stageDrawHighMatches && InstallKnownPrologueDetour(
        stageDrawHighTarget, reinterpret_cast<void*>(FilterStageDrawHigh),
        reinterpret_cast<void**>(&g_hitbox.hooks.originalStageDrawHigh),
        kExpectedStageDrawPrologue,
        kStageDrawPrologueSize);
    const bool stageDrawLowInstalled = stageDrawLowMatches && InstallKnownPrologueDetour(
        stageDrawLowTarget, reinterpret_cast<void*>(FilterStageDrawLow),
        reinterpret_cast<void**>(&g_hitbox.hooks.originalStageDrawLow),
        kExpectedStageDrawPrologue,
        kStageDrawPrologueSize);
    if (stageDrawHighInstalled && stageDrawLowInstalled)
        g_hitbox.hooks.backgroundStatus = BackgroundStatus::Installed;
    else if (!stageDrawHighMatches || !stageDrawLowMatches)
        g_hitbox.hooks.backgroundStatus = BackgroundStatus::UnsupportedExecutable;
    else
        g_hitbox.hooks.backgroundStatus = BackgroundStatus::PatchFailed;

    if (updateInstalled && laserCollisionInstalled)
        g_hitbox.hooks.status = CaptureStatus::Installed;
    else if (updateInstalled)
        g_hitbox.hooks.status = CaptureStatus::PoolOnly;
    else if (collisionInstalled || laserCollisionInstalled)
        g_hitbox.hooks.status = CaptureStatus::CollisionFallbackOnly;
    else if (!updateMatches && !collisionMatches && !laserCollisionMatches)
        g_hitbox.hooks.status = CaptureStatus::UnsupportedExecutable;
    else
        g_hitbox.hooks.status = CaptureStatus::PatchFailed;
    return updateInstalled || collisionInstalled || laserCollisionInstalled;
}

const char* CollisionCaptureStatus()
{
    switch (g_hitbox.hooks.status) {
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
    if (!g_hitbox.settings.show ||
        !IsPracticeRunActive())
        return;
    AppendCompleteBulletPool();

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ScreenTransform transform = CalculateTransform();
    const ImU32 outline = ImGui::ColorConvertFloat4ToU32(ImVec4(
        g_hitbox.settings.color[0], g_hitbox.settings.color[1],
        g_hitbox.settings.color[2], g_hitbox.settings.color[3]));
    const ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImVec4(
        g_hitbox.settings.color[0], g_hitbox.settings.color[1],
        g_hitbox.settings.color[2], g_hitbox.settings.color[3] * 0.20f));
    const ImU32 center = IM_COL32(255, 255, 255, 220);
    const ImU32 labelColor = IM_COL32(255, 255, 255, 245);

    for (const Hitbox& hitbox : g_hitbox.render) {
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
        const bool squareCollisionDisplay =
            g_hitbox.settings.square && hitbox.circular && !hitbox.rotated;
        const bool visuallyCircular = !squareCollisionDisplay &&
            !hitbox.rotated &&
            (hitbox.circular || g_hitbox.settings.shapeDisplayMode == 2 ||
                (g_hitbox.settings.shapeDisplayMode == 1 &&
                    std::abs(width - height) <= equalityTolerance));
        float labelTop = position.y;
        char sizeText[64]{};
        if (visuallyCircular) {
            const float rawRadius = std::min(rawHalfWidth, rawHalfHeight);
            const float radiusX = rawRadius * transform.scaleX;
            const float radiusY = rawRadius * transform.scaleY;
            DrawEllipse(draw, position, radiusX, radiusY, outline, fill,
                g_hitbox.settings.fill, g_hitbox.settings.lineThickness);
            labelTop = position.y - radiusY;
            std::snprintf(sizeText, sizeof(sizeText), "%.3f", rawRadius);
        } else {
            const float squareRadius =
                squareCollisionDisplay
                ? std::min(rawHalfWidth, rawHalfHeight)
                : 0.0f;
            const float halfWidth = squareCollisionDisplay
                ? squareRadius
                : rawHalfWidth;
            const float halfHeight = squareCollisionDisplay
                ? squareRadius
                : rawHalfHeight;
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
                if (g_hitbox.settings.fill)
                    draw->AddConvexPolyFilled(corners, 4, fill);
                draw->AddPolyline(corners, 4, outline,
                    ImDrawFlags_Closed, g_hitbox.settings.lineThickness);
            } else {
                const ImVec2 minimum(
                    position.x - halfWidth * transform.scaleX,
                    position.y - halfHeight * transform.scaleY);
                const ImVec2 maximum(
                    position.x + halfWidth * transform.scaleX,
                    position.y + halfHeight * transform.scaleY);
                if (g_hitbox.settings.fill)
                    draw->AddRectFilled(minimum, maximum, fill, 0.0f);
                draw->AddRect(minimum, maximum, outline, 0.0f, 0,
                    g_hitbox.settings.lineThickness);
                labelTop = minimum.y;
            }
            std::snprintf(sizeText, sizeof(sizeText), "(%.3f,%.3f)",
                rawHalfWidth, rawHalfHeight);
        }

        if (g_hitbox.settings.showSizeLabels)
            DrawSizeLabel(draw, position, labelTop, sizeText, labelColor);

        if (g_hitbox.settings.showCenters) {
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
        const float grazeRadiusX = grazeRadius * transform.scaleX;
        const float grazeRadiusY = grazeRadius * transform.scaleY;
        if (g_hitbox.settings.square) {
            draw->AddRect(
                ImVec2(position.x - grazeRadiusX,
                    position.y - grazeRadiusY),
                ImVec2(position.x + grazeRadiusX,
                    position.y + grazeRadiusY),
                IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);
        } else {
            DrawEllipse(draw, position, grazeRadiusX, grazeRadiusY,
                IM_COL32(255, 255, 255, 255), 0, false, 2.0f);
        }
        if (g_hitbox.settings.square) {
            const ImVec2 minimum(position.x - radiusX, position.y - radiusY);
            const ImVec2 maximum(position.x + radiusX, position.y + radiusY);
            if (g_hitbox.settings.fill)
                draw->AddRectFilled(minimum, maximum, fill, 0.0f);
            draw->AddRect(minimum, maximum, outline, 0.0f, 0,
                g_hitbox.settings.lineThickness);
        } else {
            DrawEllipse(draw, position, radiusX, radiusY, outline, fill,
                g_hitbox.settings.fill, g_hitbox.settings.lineThickness);
        }
        if (g_hitbox.settings.showSizeLabels) {
            char sizeText[32]{};
            std::snprintf(sizeText, sizeof(sizeText), "%.1f", playerRadius);
            DrawSizeLabel(draw, position, position.y - radiusY, sizeText, labelColor);
        }
        if (g_hitbox.settings.showCenters) {
            draw->AddLine(ImVec2(position.x - 3.0f, position.y), ImVec2(position.x + 3.0f, position.y), center);
            draw->AddLine(ImVec2(position.x, position.y - 3.0f), ImVec2(position.x, position.y + 3.0f), center);
        }
    }
}

void DrawHitboxSettingsUi()
{
    size_t circleCount = 0;
    for (const Hitbox& hitbox : g_hitbox.render)
        circleCount += hitbox.circular ? 1u : 0u;
    const size_t rectangleCount = g_hitbox.render.size() - circleCount;

    ImGui::Separator();
    ImGui::TextUnformatted("Stage visibility");
    const char* backgroundStatus = "not installed";
    switch (g_hitbox.hooks.backgroundStatus) {
    case BackgroundStatus::Installed: backgroundStatus = "active"; break;
    case BackgroundStatus::UnsupportedExecutable: backgroundStatus = "disabled: executable/prologue mismatch"; break;
    case BackgroundStatus::PatchFailed: backgroundStatus = "disabled: code patch failed"; break;
    default: break;
    }
    ImGui::Text("Background hook: %s", backgroundStatus);
    bool disableStageBackground = g_hitbox.settings.disableStageBackground;
    if (ImGui::Checkbox("Black stage background", &disableStageBackground))
        g_hitbox.settings.disableStageBackground = disableStageBackground;

    ImGui::Separator();
    ImGui::TextUnformatted("Bullet collision overlay");
    ImGui::Text("Capture: %s", CollisionCaptureStatus());
    ImGui::Text("Captured this render frame: %d",
        static_cast<int>(g_hitbox.render.size()));
    ImGui::Text("Game shape flags: circle %d | rectangle %d",
        static_cast<int>(circleCount), static_cast<int>(rectangleCount));
    bool showHitboxes = g_hitbox.settings.show;
    if (ImGui::Checkbox("Show hitboxes", &showHitboxes))
        g_hitbox.settings.show = showHitboxes;
    ImGui::SameLine();
    ImGui::Checkbox("Fill", &g_hitbox.settings.fill);
    ImGui::Checkbox("Show centers", &g_hitbox.settings.showCenters);
    ImGui::SameLine();
    ImGui::Checkbox("Show size labels", &g_hitbox.settings.showSizeLabels);
    ImGui::Checkbox("Flip stage Y", &g_hitbox.settings.flipY);
    constexpr ImGuiColorEditFlags colorFlags =
        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;
    ImGui::ColorEdit4("Hitbox color", g_hitbox.settings.color.data(), colorFlags);
    ImGui::TextDisabled("Outlines show raw bullet radii/half-extents without adding the player radius.");
    const char* shapeModes[] = {
        "Exact game branch",
        "Equal width/height as circle",
        "Force all as circles",
    };
    ImGui::Combo("Display shape", &g_hitbox.settings.shapeDisplayMode,
        shapeModes, IM_ARRAYSIZE(shapeModes));
    if (g_hitbox.settings.shapeDisplayMode != 0)
        ImGui::TextDisabled("Visualization override; collision still uses the game's original branch.");

    if (ImGui::TreeNode("Position calibration")) {
        ImGui::TextDisabled("Default: 640x480 reference, stage origin (128,16), pixel X +4");
        ImGui::DragFloat("Stage origin X", &g_hitbox.settings.stageOriginX,
            0.25f, -640.0f, 640.0f, "%.2f");
        ImGui::DragFloat("Stage origin Y", &g_hitbox.settings.stageOriginY,
            0.25f, -480.0f, 480.0f, "%.2f");
        ImGui::DragFloat("Pixel offset X", &g_hitbox.settings.pixelOffsetX,
            0.25f, -1000.0f, 1000.0f, "%.2f");
        ImGui::DragFloat("Pixel offset Y", &g_hitbox.settings.pixelOffsetY,
            0.25f, -1000.0f, 1000.0f, "%.2f");
        ImGui::SliderFloat("Scale multiplier",
            &g_hitbox.settings.scaleMultiplier, 0.1f, 5.0f, "%.3f");
        ImGui::SliderFloat("Line thickness",
            &g_hitbox.settings.lineThickness, 0.5f, 5.0f, "%.1f");
        ImGui::TreePop();
    }
}

bool IsHitboxDisplayEnabled()
{
    return g_hitbox.settings.show;
}

bool IsHitboxDisplayActive()
{
    return g_hitbox.settings.show && IsPracticeRunActive();
}

void SetHitboxDisplayEnabled(bool enabled)
{
    LoadHitboxConfig();
    if (g_hitbox.settings.show == enabled)
        return;
    g_hitbox.settings.show = enabled;
    SaveHitboxConfig();
}

bool IsSquareHitboxModeEnabled()
{
    return g_hitbox.settings.square;
}

void SetSquareHitboxModeEnabled(bool enabled)
{
    if (g_hitbox.settings.square == enabled)
        return;
    // Ordinary bullet graze is inlined in BulletManagerUpdate rather than
    // routed through CollisionTest. Refuse a partial mode change if this exact
    // instruction is not present in the supported executable.
    if (ApplyBulletGrazeSquarePatch(enabled))
        g_hitbox.settings.square = enabled;
}

void DrawHitboxDisplayControlsUi()
{
    LoadHitboxConfig();
    bool changed = false;
    float offset[2] = {
        g_hitbox.settings.pixelOffsetX, g_hitbox.settings.pixelOffsetY};
    ImGui::SetNextItemWidth(300.0f);
    if (ImGui::DragFloat2(S(HitboxOffset), offset, 0.25f,
            -1000.0f, 1000.0f, "%.2f")) {
        g_hitbox.settings.pixelOffsetX = offset[0];
        g_hitbox.settings.pixelOffsetY = offset[1];
        changed = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    changed |= ImGui::DragFloat(S(HitboxScale),
        &g_hitbox.settings.scaleMultiplier,
        0.005f, 0.1f, 5.0f, "%.3f");

    constexpr ImGuiColorEditFlags colorFlags =
        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;
    ImGui::SetNextItemWidth(420.0f);
    changed |= ImGui::ColorEdit4(S(HitboxColor),
        g_hitbox.settings.color.data(), colorFlags);
    if (changed)
        SaveHitboxConfig();
}
