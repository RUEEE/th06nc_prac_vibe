#include "game_overlay.h"

#include "game_addresses.h"
#include "keyboard_input.h"
#include "locale.h"
#include "practice_menu.h"
#include "imgui.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>

namespace {

template <size_t Size>
struct PatchSite {
    GameAddress address;
    std::array<unsigned char, Size> original;
    std::array<unsigned char, Size> replacement;
};

constexpr PatchSite<2> kLivesBranch{
    GameAddress::LivesGameOverBranch, {0x7F, 0x0C}, {0xEB, 0x0C}};
constexpr PatchSite<2> kLivesDecrement{
    GameAddress::LivesDecrement, {0xFE, 0xC8}, {0x90, 0x90}};
constexpr PatchSite<2> kBombDecrement{
    GameAddress::BombDecrement, {0xFE, 0xC9}, {0x90, 0x90}};
constexpr PatchSite<7> kPowerFirstStore{
    GameAddress::PowerLossFirstStore,
    {0x66, 0x89, 0x05, 0xD2, 0x92, 0x48, 0x00},
    {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90}};
constexpr PatchSite<8> kPowerClampStore{
    GameAddress::PowerLossClampStore,
    {0x66, 0x44, 0x89, 0x3D, 0xC2, 0x92, 0x48, 0x00},
    {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90}};
constexpr PatchSite<7> kPowerSecondStore{
    GameAddress::PowerLossSecondStore,
    {0x66, 0x89, 0x35, 0x6B, 0x91, 0x48, 0x00},
    {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90}};
constexpr PatchSite<7> kBulletDieStateStore{
    GameAddress::PlayerDieStoreBullet,
    {0xC6, 0x83, 0x98, 0x78, 0x00, 0x00, 0x02},
    {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90}};
constexpr PatchSite<7> kLaserDieStateStore{
    GameAddress::PlayerDieStoreLaser,
    {0xC6, 0x05, 0xF3, 0xBE, 0x49, 0x00, 0x02},
    {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90}};

constexpr size_t kAutoBombInputCheckSize = 7;
constexpr unsigned char kExpectedAutoBombInputCheck[kAutoBombInputCheckSize] = {
    0x44, 0x8B, 0x1D, 0x6C, 0x62, 0xA0, 0x00, // mov r11d,[MenuInputCurrent]
};

bool g_windowVisible = false;
bool g_invincible = false;
bool g_lockLives = false;
bool g_lockBombs = false;
bool g_lockPower = false;
bool g_lockTime = false;
bool g_autoBomb = false;
bool g_everlastingBgm = false;
bool g_noBomb = false;
bool g_patchError = false;
void* g_autoBombRelay = nullptr;
void* g_enemyUpdateTrampoline = nullptr;

template <size_t Size>
bool SiteHasBytes(const PatchSite<Size>& site,
    const std::array<unsigned char, Size>& bytes)
{
    const auto* target = ResolveGameAddress<unsigned char>(site.address);
    return target && IsGameAddressRangeValid(site.address, Size) &&
        std::memcmp(target, bytes.data(), Size) == 0;
}

template <size_t Size>
bool WriteSite(const PatchSite<Size>& site, bool enabled)
{
    auto* target = ResolveGameAddress<unsigned char>(site.address);
    if (!target || !IsGameAddressRangeValid(site.address, Size))
        return false;

    const auto& desired = enabled ? site.replacement : site.original;
    if (std::memcmp(target, desired.data(), Size) == 0)
        return true;
    const auto& expected = enabled ? site.original : site.replacement;
    if (std::memcmp(target, expected.data(), Size) != 0)
        return false;

    DWORD oldProtection = 0;
    if (!VirtualProtect(target, Size, PAGE_EXECUTE_READWRITE, &oldProtection))
        return false;
    std::memcpy(target, desired.data(), Size);
    FlushInstructionCache(GetCurrentProcess(), target, Size);
    DWORD ignored = 0;
    VirtualProtect(target, Size, oldProtection, &ignored);
    return true;
}

template <typename... Sites>
bool SetPatchGroup(bool enabled, const Sites&... sites)
{
    const bool alreadyDesired =
        (SiteHasBytes(sites, enabled ? sites.replacement : sites.original) && ...);
    if (alreadyDesired)
        return true;

    const bool allRecognized =
        ((SiteHasBytes(sites, sites.original) ||
             SiteHasBytes(sites, sites.replacement)) && ...);
    if (!allRecognized)
        return false;
    return (WriteSite(sites, enabled) && ...);
}

void ToggleRequested(bool& setting, bool requested,
    bool (*apply)(bool))
{
    if (!requested)
        return;
    const bool desired = !setting;
    if (apply(desired))
        setting = desired;
    else
        g_patchError = true;
}

bool ApplyLives(bool enabled)
{
    // The unconditional branch lets zero lives take the ordinary death path;
    // removing DEC then stores zero back instead of wrapping or setting game over.
    return SetPatchGroup(enabled, kLivesBranch, kLivesDecrement);
}

bool ApplyInvincible(bool enabled)
{
    // These are the two DIE-state writes reached by bullet and laser
    // collisions. Leave every other player state transition intact.
    return SetPatchGroup(enabled, kBulletDieStateStore, kLaserDieStateStore);
}

bool ApplyBombs(bool enabled)
{
    return SetPatchGroup(enabled, kBombDecrement);
}

bool ApplyPower(bool enabled)
{
    return SetPatchGroup(enabled, kPowerFirstStore, kPowerClampStore,
        kPowerSecondStore);
}

using EnemyUpdateFn = int64_t(__fastcall*)(void*);

int64_t __fastcall HookedEnemyUpdate(void* enemyManager)
{
    auto original = reinterpret_cast<EnemyUpdateFn>(g_enemyUpdateTrampoline);
    if (!original || !enemyManager)
        return original ? original(enemyManager) : 0;

    constexpr ptrdiff_t kFirstEnemy = 8;
    constexpr ptrdiff_t kEnemyStride = 0x10B0;
    constexpr ptrdiff_t kTimer = 4;
    constexpr ptrdiff_t kActiveFlags = 0xBC;
    constexpr int kEnemyCount = 256;
    std::array<int, kEnemyCount> timers{};
    auto* base = static_cast<std::byte*>(enemyManager);
    auto* timeline = ResolveGameAddress<int>(GameAddress::TimelineFrame);
    const bool locked = g_lockTime && timeline &&
        IsEnhancedPracticeRunActive();
    const int timelineBefore = locked ? *timeline : 0;
    if (locked) {
        for (int i = 0; i < kEnemyCount; ++i)
            timers[i] = *reinterpret_cast<int*>(base + kFirstEnemy +
                i * kEnemyStride + kTimer);
    }

    const int64_t result = original(enemyManager);
    if (locked) {
        *timeline = timelineBefore;
        for (int i = 0; i < kEnemyCount; ++i) {
            std::byte* enemy = base + kFirstEnemy + i * kEnemyStride;
            if ((*reinterpret_cast<uint8_t*>(enemy + kActiveFlags) & 0x80) != 0)
                *reinterpret_cast<int*>(enemy + kTimer) = timers[i];
        }

        // The original game's midboss waits need the global timeline to reach
        // their interrupt point. This is the same exception used by the
        // zxxsmart implementation; without it, Lock Time can strand a
        // midboss introduction forever. Values are the mapped nc_ecl frames.
        constexpr int starts[] = {1882, 2498, 0, 4058, 3274};
        constexpr int lengths[] = {48 * 60, 32 * 60, 0, 40 * 60, 70 * 60};
        constexpr int waits[] = {4 * 60, 15 * 60, 0, 12 * 60, 5 * 60};
        const auto* stageAddress =
            ResolveGameAddress<uint8_t>(GameAddress::CurrentStage);
        const int stage = stageAddress ? static_cast<int>(*stageAddress) : -1;
        if (stage >= 0 && stage < 5 && stage != 2 &&
            timelineBefore >= starts[stage] &&
            timelineBefore < starts[stage] + lengths[stage]) {
            for (int i = 0; i < kEnemyCount; ++i) {
                std::byte* enemy = base + kFirstEnemy + i * kEnemyStride;
                const uint8_t active =
                    *reinterpret_cast<uint8_t*>(enemy + kActiveFlags);
                const uint8_t type =
                    *reinterpret_cast<uint8_t*>(enemy + kActiveFlags + 1);
                if ((active & 0x80) != 0 && (type & 0x08) != 0) {
                    *timeline = std::max(timelineBefore,
                        starts[stage] + waits[stage]);
                    break;
                }
            }
        }
    }
    return result;
}

void DrawAutoShootIndicator()
{
    if (!IsAutoShooting())
        return;

    const ImGuiIO& io = ImGui::GetIO();
    const float size = 32.0f * io.DisplaySize.x / 1280.0f;
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    drawList->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(size, size),
        IM_COL32(221, 221, 255, 255));
    const ImVec2 textSize = ImGui::CalcTextSize("A");
    drawList->AddText(ImVec2((size - textSize.x) * 0.5f,
                          (size - textSize.y) * 0.5f),
        IM_COL32(34, 34, 204, 255), "A");
}

void* AllocateNearAddress(void* target, size_t size)
{
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const uintptr_t targetAddress = reinterpret_cast<uintptr_t>(target);
    const uintptr_t minimumApplication =
        reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
    const uintptr_t maximumApplication =
        reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
    const uintptr_t reach =
        static_cast<uintptr_t>(std::numeric_limits<int32_t>::max()) - 0x10000;
    const uintptr_t low = targetAddress > reach
        ? std::max(targetAddress - reach, minimumApplication)
        : minimumApplication;
    const uintptr_t high = std::min(targetAddress + reach, maximumApplication);
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
            if (candidate <= high && candidate <= regionEnd &&
                size <= regionEnd - candidate) {
                if (void* block = VirtualAlloc(reinterpret_cast<void*>(candidate),
                        size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE))
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
    destination[0] = 0xFF;
    destination[1] = 0x25;
    *reinterpret_cast<uint32_t*>(destination + 2) = 0;
    *reinterpret_cast<const void**>(destination + 6) = target;
}

bool InstallEnemyUpdateHook()
{
    if (g_enemyUpdateTrampoline)
        return true;

    constexpr std::array<unsigned char, 15> expected{
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x48, 0x08,
        0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55};
    auto* target = ResolveGameAddress<unsigned char>(GameAddress::EnemyUpdate);
    if (!target || !IsGameAddressRangeValid(GameAddress::EnemyUpdate,
            expected.size()) ||
        std::memcmp(target, expected.data(), expected.size()) != 0)
        return false;

    constexpr size_t kTrampolineSize = 15 + 14;
    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(nullptr,
        kTrampolineSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!trampoline)
        return false;
    std::memcpy(trampoline, target, expected.size());
    WriteAbsoluteJump(trampoline + expected.size(), target + expected.size());

    DWORD trampolineProtection = 0;
    if (!VirtualProtect(trampoline, kTrampolineSize, PAGE_EXECUTE_READ,
            &trampolineProtection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    DWORD targetProtection = 0;
    if (!VirtualProtect(target, expected.size(), PAGE_EXECUTE_READWRITE,
            &targetProtection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    g_enemyUpdateTrampoline = trampoline;
    WriteAbsoluteJump(target, reinterpret_cast<void*>(HookedEnemyUpdate));
    target[14] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), target, expected.size());
    DWORD ignored = 0;
    VirtualProtect(target, expected.size(), targetProtection, &ignored);
    return true;
}

} // namespace

bool InstallGameOverlayHook()
{
    const bool enemyInstalled = InstallEnemyUpdateHook();
    if (g_autoBombRelay)
        return enemyInstalled;

    if (!enemyInstalled)
        g_patchError = true;

    auto* target = ResolveGameAddress<unsigned char>(
        GameAddress::AutoBombInputCheck);
    auto* continuation = ResolveGameAddress<unsigned char>(
        GameAddress::AutoBombInputContinue);
    auto* deathBomb = ResolveGameAddress<unsigned char>(
        GameAddress::DeathBombBranch);
    auto* actions = ResolveGameAddress<uint32_t>(GameAddress::MenuInputCurrent);
    auto* playerState = ResolveGameAddress<uint8_t>(
        GameAddress::CurrentPlayerState);
    auto* replayPlayback = ResolveGameAddress<uint8_t>(
        GameAddress::ReplayPlaybackFlag);
    if (!target || !continuation || !deathBomb || !actions || !playerState ||
        !replayPlayback ||
        !IsGameAddressRangeValid(GameAddress::AutoBombInputCheck,
            kAutoBombInputCheckSize) ||
        std::memcmp(target, kExpectedAutoBombInputCheck,
            kAutoBombInputCheckSize) != 0)
        return false;

    constexpr size_t kRelaySize = 96;
    auto* relay = static_cast<unsigned char*>(
        AllocateNearAddress(target, kRelaySize));
    if (!relay)
        return false;

    size_t cursor = 0;
    auto emitByte = [&](unsigned char value) { relay[cursor++] = value; };
    auto emitBytes = [&](std::initializer_list<unsigned char> values) {
        for (const unsigned char value : values)
            emitByte(value);
    };
    auto emitPointer = [&](const void* value) {
        const uintptr_t pointer = reinterpret_cast<uintptr_t>(value);
        std::memcpy(relay + cursor, &pointer, sizeof(pointer));
        cursor += sizeof(pointer);
    };
    auto emitRelativeJump = [&](const void* destination) {
        emitByte(0xE9);
        const intptr_t relative = reinterpret_cast<const unsigned char*>(destination) -
            (relay + cursor + sizeof(int32_t));
        const int32_t displacement = static_cast<int32_t>(relative);
        std::memcpy(relay + cursor, &displacement, sizeof(displacement));
        cursor += sizeof(displacement);
    };

    // Keep the native stack frame and register setup intact. At the game's
    // Bomb-input check, divert a dying player directly to the native
    // deathbomb branch. Normal play and replay playback execute the exact
    // overwritten input load and continue through the stock edge detection.
    std::array<size_t, 4> originalBranchDisplacements{};
    size_t branchCount = 0;
    auto emitFlagCheck = [&](const void* flag, bool branchWhenNonzero) {
        emitBytes({0x48, 0xB8});             // mov rax, flag
        emitPointer(flag);
        emitBytes({0x80, 0x38, 0x00});       // cmp byte ptr [rax], 0
        emitByte(branchWhenNonzero ? 0x75 : 0x74);
        originalBranchDisplacements[branchCount++] = cursor++;
    };
    emitFlagCheck(&g_autoBomb, false);
    emitFlagCheck(&g_noBomb, true);
    emitFlagCheck(replayPlayback, true);
    emitBytes({0x48, 0xB8});                 // mov rax, playerState
    emitPointer(playerState);
    emitBytes({0x80, 0x38, 0x02, 0x75});     // cmp byte ptr [rax], 2 / jne stock
    originalBranchDisplacements[branchCount++] = cursor++;
    emitRelativeJump(deathBomb);

    const size_t originalPath = cursor;
    for (size_t i = 0; i < branchCount; ++i) {
        const ptrdiff_t displacement = static_cast<ptrdiff_t>(originalPath) -
            static_cast<ptrdiff_t>(originalBranchDisplacements[i] + 1);
        relay[originalBranchDisplacements[i]] =
            static_cast<unsigned char>(static_cast<int8_t>(displacement));
    }

    emitBytes({0x48, 0xB8});                  // mov rax, actions
    emitPointer(actions);
    emitBytes({0x44, 0x8B, 0x18});           // mov r11d,[rax]
    emitRelativeJump(continuation);

    DWORD oldBlockProtection = 0;
    if (!VirtualProtect(relay, kRelaySize, PAGE_EXECUTE_READ,
            &oldBlockProtection)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, kRelaySize);

    const intptr_t relative = reinterpret_cast<intptr_t>(relay) -
        (reinterpret_cast<intptr_t>(target) + 5);
    if (relative < std::numeric_limits<int32_t>::min() ||
        relative > std::numeric_limits<int32_t>::max()) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }

    DWORD oldProtection = 0;
    if (!VirtualProtect(target, kAutoBombInputCheckSize,
            PAGE_EXECUTE_READWRITE, &oldProtection)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    target[0] = 0xE9;
    *reinterpret_cast<int32_t*>(target + 1) = static_cast<int32_t>(relative);
    target[5] = 0x90;
    target[6] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), target, kAutoBombInputCheckSize);
    DWORD ignored = 0;
    VirtualProtect(target, kAutoBombInputCheckSize, oldProtection, &ignored);
    g_autoBombRelay = relay;
    return enemyInstalled;
}

void UpdateAndDrawGameOverlayUi()
{
    const bool foreground = IsGameProcessForeground();
    if (foreground && (GetAsyncKeyState(VK_BACK) & 1))
        g_windowVisible = !g_windowVisible;

    ToggleRequested(g_invincible,
        foreground && (GetAsyncKeyState(VK_F1) & 1), ApplyInvincible);
    if (foreground && (GetAsyncKeyState(VK_F2) & 1))
        g_lockLives = !g_lockLives;
    ToggleRequested(g_lockBombs,
        foreground && (GetAsyncKeyState(VK_F3) & 1), ApplyBombs);
    ToggleRequested(g_lockPower,
        foreground && (GetAsyncKeyState(VK_F4) & 1), ApplyPower);
    if (foreground && (GetAsyncKeyState(VK_F5) & 1))
        g_lockTime = !g_lockTime;
    if (foreground && (GetAsyncKeyState(VK_F6) & 1))
        g_autoBomb = !g_autoBomb;
    if (foreground && (GetAsyncKeyState(VK_F7) & 1))
        g_everlastingBgm = !g_everlastingBgm;
    if (foreground && (GetAsyncKeyState(VK_F8) & 1))
        g_noBomb = !g_noBomb;

    // Match thprac's "lock lives / no continue" mode: retain the stock death
    // and life-decrement path while any lives remain, and intervene only once
    // the counter is already zero and the next death would cause game over.
    const auto* lives = ResolveGameAddress<uint8_t>(GameAddress::CurrentLives);
    const bool livesPatchWanted = g_lockLives && lives && *lives == 0;
    if (!ApplyLives(livesPatchWanted))
        g_patchError = true;

    DrawAutoShootIndicator();

    if (!g_windowVisible)
        return;

    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f),
        ImGuiCond_Always, ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.50f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##th06nc-game-overlay", nullptr, flags)) {
        const auto drawState = [](bool enabled, const char* label) {
            if (enabled)
                ImGui::TextColored(ImVec4(0.20f, 1.0f, 0.20f, 1.0f),
                    "%s", label);
            else
                ImGui::TextUnformatted(label);
        };
        drawState(g_invincible, S(Invincible));
        drawState(g_lockLives, S(LockLives));
        drawState(g_lockBombs, S(LockBombs));
        drawState(g_lockPower, S(LockPower));
        drawState(g_lockTime, S(LockTime));
        drawState(g_autoBomb, S(AutoBomb));
        drawState(g_everlastingBgm, S(EverlastingBgm));
        drawState(g_noBomb, S(DisableBomb));
        if (g_patchError)
            ImGui::TextDisabled("%s", S(PatchUnsupported));
    }
    ImGui::End();
}

bool IsGameOverlayVisible()
{
    return g_windowVisible;
}

bool IsBombInputSuppressed()
{
    return g_noBomb;
}

void PrepareEverlastingBgmForInitialization(bool enhancedRetry)
{
    if (auto* keepBgm = ResolveGameAddress<uint8_t>(GameAddress::KeepBgm))
        *keepBgm = static_cast<uint8_t>(
            enhancedRetry && g_everlastingBgm);
}
