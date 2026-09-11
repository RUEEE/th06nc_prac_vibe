#include "hitbox_capture.h"
#include "game_addresses.h"
#include "overlay.h"
#include "practice_menu.h"

#include "imgui.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
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
    float playerRadius;
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
std::atomic<void*> g_bulletManager{nullptr};
std::atomic<bool> g_poolHookInstalled{false};
std::atomic<CaptureStatus> g_status{CaptureStatus::NotInstalled};
std::atomic<BackgroundStatus> g_backgroundStatus{BackgroundStatus::NotInstalled};
std::atomic<bool> g_disableStageBackground{false};
SRWLOCK g_hitboxLock = SRWLOCK_INIT;
std::vector<Hitbox> g_pendingHitboxes;
std::vector<Hitbox> g_renderHitboxes;

std::atomic<bool> g_showHitboxes{false};
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
float g_outlineColor[4] = {1.0f, 0.25f, 0.25f, 0.90f};
float g_fillColor[4] = {1.0f, 0.19f, 0.19f, 0.18f};

bool IsFiniteHitbox(const Hitbox& hitbox)
{
    return std::isfinite(hitbox.position.x) && std::isfinite(hitbox.position.y) &&
        std::isfinite(hitbox.size.x) && std::isfinite(hitbox.size.y) &&
        std::isfinite(hitbox.rotationPivot.x) &&
        std::isfinite(hitbox.rotationPivot.y) &&
        std::isfinite(hitbox.rotation) && std::isfinite(hitbox.playerRadius) &&
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
        hitbox.playerRadius = *reinterpret_cast<const float*>(
            static_cast<const std::byte*>(playerContext) +
            GameField(GameObjectField::PlayerCollisionRadius));
        hitbox.circular = circular;
        valid = IsFiniteHitbox(hitbox);
    }

    // The standard circular bullets are read from the complete pool at Present.
    // Keep this hook for rectangular/laser checks and as a fallback on game updates.
    if (valid && g_showHitboxes.load(std::memory_order_relaxed) &&
        IsPracticeRunActive() && (!circular || !g_poolHookInstalled.load()) &&
        TryAcquireSRWLockExclusive(&g_hitboxLock)) {
        if (g_pendingHitboxes.size() < g_pendingHitboxes.capacity())
            g_pendingHitboxes.push_back(hitbox);
        ReleaseSRWLockExclusive(&g_hitboxLock);
    }

    return g_originalCollision(playerContext, position, size, circular);
}

int __fastcall CaptureLaserCollision(void* context, const Float2* center,
    const Float2* size, const Float2* rotationPivot, float rotation,
    bool enableGraze)
{
    if (center && size && rotationPivot &&
        g_showHitboxes.load(std::memory_order_relaxed) &&
        IsPracticeRunActive()) {
        Hitbox hitbox{};
        hitbox.position = *center;
        hitbox.size = *size;
        hitbox.rotationPivot = *rotationPivot;
        hitbox.rotation = rotation;
        hitbox.rotated = true;
        const auto* playerRadius =
            ResolveGameAddress<float>(GameAddress::PlayerHitboxRadius);
        hitbox.playerRadius = playerRadius ? *playerRadius : 0.0f;
        if (IsFiniteHitbox(hitbox) &&
            TryAcquireSRWLockExclusive(&g_hitboxLock)) {
            if (g_pendingHitboxes.size() < g_pendingHitboxes.capacity())
                g_pendingHitboxes.push_back(hitbox);
            ReleaseSRWLockExclusive(&g_hitboxLock);
        }
    }
    return g_originalLaserCollision(context, center, size, rotationPivot,
        rotation, enableGraze);
}

int __fastcall CaptureBulletManager(void* bulletManager)
{
    g_bulletManager.store(bulletManager, std::memory_order_release);
    return g_originalBulletUpdate(bulletManager);
}

int __fastcall FilterStageDrawHigh(void* stage)
{
    if (g_disableStageBackground.load(std::memory_order_relaxed)) {
        ClearStagePlayfieldBlack();
        return 1;
    }
    return g_originalStageDrawHigh(stage);
}

int __fastcall FilterStageDrawLow(void* stage)
{
    if (g_disableStageBackground.load(std::memory_order_relaxed))
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
        g_status.store(CaptureStatus::AllocationFailed);
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
    AcquireSRWLockExclusive(&g_hitboxLock);
    g_renderHitboxes.clear();
    g_renderHitboxes.swap(g_pendingHitboxes);
    ReleaseSRWLockExclusive(&g_hitboxLock);
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
    auto* manager = static_cast<const std::byte*>(
        g_bulletManager.load(std::memory_order_acquire));
    const size_t poolBytes = 8 + kBulletCount * kBulletStride;
    if (!manager || !IsReadable(manager, poolBytes))
        return;

    const auto* moduleBase = GameModuleBase();
    float playerRadius = 0.0f;
    if (moduleBase)
        playerRadius = *reinterpret_cast<const float*>(
            moduleBase + GameRva(GameAddress::PlayerHitboxRadius));
    if (!std::isfinite(playerRadius) || playerRadius < 0.0f || playerRadius > 1000.0f)
        playerRadius = 0.0f;

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
        hitbox.playerRadius = playerRadius;
        hitbox.circular = true; // The standard bullet call site loads r9d from r12d, where r12d == 1.
        if (IsFiniteHitbox(hitbox) && g_renderHitboxes.size() < g_renderHitboxes.capacity())
            g_renderHitboxes.push_back(hitbox);
    }
}

struct ScreenTransform {
    float scale;
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
    return {
        scale,
        ImVec2(letterbox.x + g_stageOriginX * scale + g_pixelOffsetX,
            letterbox.y + g_stageOriginY * scale + g_pixelOffsetY)
    };
}

ImVec2 ToScreen(const Float2& position, const ScreenTransform& transform)
{
    const float y = g_flipY ? 448.0f - position.y : position.y;
    return ImVec2(transform.origin.x + position.x * transform.scale,
        transform.origin.y + y * transform.scale);
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
    if (!moduleBase ||
        !IsReadable(moduleBase + GameRva(GameAddress::PlayerPosition), sizeof(Float2)) ||
        !IsReadable(moduleBase + GameRva(GameAddress::PlayerHitboxRadius), sizeof(float)))
        return false;

    position = *reinterpret_cast<const Float2*>(
        moduleBase + GameRva(GameAddress::PlayerPosition));
    radius = *reinterpret_cast<const float*>(
        moduleBase + GameRva(GameAddress::PlayerHitboxRadius));
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
    if (g_status.load() == CaptureStatus::Installed)
        return true;

    auto* base = GameModuleBase();
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        g_status.store(CaptureStatus::UnsupportedExecutable);
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
        g_status.store(CaptureStatus::UnsupportedExecutable);
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
    g_poolHookInstalled.store(updateInstalled);

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
        g_backgroundStatus.store(BackgroundStatus::Installed);
    else if (!stageDrawHighMatches || !stageDrawLowMatches)
        g_backgroundStatus.store(BackgroundStatus::UnsupportedExecutable);
    else
        g_backgroundStatus.store(BackgroundStatus::PatchFailed);

    if (updateInstalled && laserCollisionInstalled)
        g_status.store(CaptureStatus::Installed);
    else if (updateInstalled)
        g_status.store(CaptureStatus::PoolOnly);
    else if (collisionInstalled || laserCollisionInstalled)
        g_status.store(CaptureStatus::CollisionFallbackOnly);
    else if (!updateMatches && !collisionMatches && !laserCollisionMatches)
        g_status.store(CaptureStatus::UnsupportedExecutable);
    else
        g_status.store(CaptureStatus::PatchFailed);
    return updateInstalled || collisionInstalled || laserCollisionInstalled;
}

const char* CollisionCaptureStatus()
{
    switch (g_status.load()) {
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
    if (!g_showHitboxes.load(std::memory_order_relaxed) ||
        !IsPracticeRunActive())
        return;
    AppendCompleteBulletPool();

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ScreenTransform transform = CalculateTransform();
    const ImU32 outline = ImGui::ColorConvertFloat4ToU32(ImVec4(
        g_outlineColor[0], g_outlineColor[1], g_outlineColor[2], g_outlineColor[3]));
    const ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImVec4(
        g_fillColor[0], g_fillColor[1], g_fillColor[2], g_fillColor[3]));
    const ImU32 center = IM_COL32(255, 255, 255, 220);
    const ImU32 labelColor = IM_COL32(255, 255, 255, 245);

    for (const Hitbox& hitbox : g_renderHitboxes) {
        const Float2 worldCenter = hitbox.rotated
            ? RotateAround(hitbox.position, hitbox.rotationPivot,
                hitbox.rotation)
            : hitbox.position;
        const ImVec2 position = ToScreen(worldCenter, transform);
        // Collision() expands the bullet shape by the player radius. Draw that
        // effective collision area, while labels below retain the raw bullet size.
        const float playerRadius = std::max(0.0f, hitbox.playerRadius);
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
            const float radiusWorld = rawRadius + playerRadius;
            const float radius = radiusWorld * transform.scale;
            if (radius > 0.0f) {
                if (g_fillHitboxes)
                    draw->AddCircleFilled(position, radius, fill, 24);
                draw->AddCircle(position, radius, outline, 24, g_lineThickness);
            }
            labelTop = position.y - radius;
            std::snprintf(sizeText, sizeof(sizeText), "%.3f", rawRadius);
        } else {
            const float halfWidth = rawHalfWidth + playerRadius;
            const float halfHeight = rawHalfHeight + playerRadius;
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
                    position.x - halfWidth * transform.scale,
                    position.y - halfHeight * transform.scale);
                const ImVec2 maximum(
                    position.x + halfWidth * transform.scale,
                    position.y + halfHeight * transform.scale);
                const float rounding = playerRadius * transform.scale;
                if (g_fillHitboxes)
                    draw->AddRectFilled(minimum, maximum, fill, rounding);
                draw->AddRect(minimum, maximum, outline, rounding, 0,
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
        const float radius = playerRadius * transform.scale;
        if (g_fillHitboxes)
            draw->AddCircleFilled(position, radius, fill, 24);
        draw->AddCircle(position, radius, outline, 24, g_lineThickness);
        if (g_showSizeLabels) {
            char sizeText[32]{};
            std::snprintf(sizeText, sizeof(sizeText), "%.1f", playerRadius);
            DrawSizeLabel(draw, position, position.y - radius, sizeText, labelColor);
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
    switch (g_backgroundStatus.load()) {
    case BackgroundStatus::Installed: backgroundStatus = "active"; break;
    case BackgroundStatus::UnsupportedExecutable: backgroundStatus = "disabled: executable/prologue mismatch"; break;
    case BackgroundStatus::PatchFailed: backgroundStatus = "disabled: code patch failed"; break;
    default: break;
    }
    ImGui::Text("Background hook: %s", backgroundStatus);
    bool disableStageBackground = g_disableStageBackground.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Black stage background", &disableStageBackground))
        g_disableStageBackground.store(disableStageBackground, std::memory_order_relaxed);

    ImGui::Separator();
    ImGui::TextUnformatted("Bullet collision overlay");
    ImGui::Text("Capture: %s", CollisionCaptureStatus());
    ImGui::Text("Captured this render frame: %d", static_cast<int>(g_renderHitboxes.size()));
    ImGui::Text("Game shape flags: circle %d | rectangle %d",
        static_cast<int>(circleCount), static_cast<int>(rectangleCount));
    bool showHitboxes = g_showHitboxes.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Show hitboxes", &showHitboxes))
        g_showHitboxes.store(showHitboxes, std::memory_order_relaxed);
    ImGui::SameLine();
    ImGui::Checkbox("Fill", &g_fillHitboxes);
    ImGui::Checkbox("Show centers", &g_showCenters);
    ImGui::SameLine();
    ImGui::Checkbox("Show size labels", &g_showSizeLabels);
    ImGui::Checkbox("Flip stage Y", &g_flipY);
    constexpr ImGuiColorEditFlags colorFlags =
        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;
    ImGui::ColorEdit4("Hitbox color", g_outlineColor, colorFlags);
    ImGui::ColorEdit4("Fill color", g_fillColor, colorFlags);
    ImGui::TextDisabled("Bullet outlines include the player radius; labels show raw bullet radii/half-extents.");
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
    return g_showHitboxes.load(std::memory_order_relaxed);
}

void SetHitboxDisplayEnabled(bool enabled)
{
    g_showHitboxes.store(enabled, std::memory_order_relaxed);
}
