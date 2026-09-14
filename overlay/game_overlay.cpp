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

struct AuxiliaryRuntime {
    bool windowVisible = false;
    bool invincible = false;
    bool lockLives = false;
    bool lockBombs = false;
    bool lockPower = false;
    bool lockTime = false;
    bool autoBomb = false;
    bool everlastingBgm = false;
    bool noBomb = false;
    bool patchError = false;
    void* autoBombRelay = nullptr;
    void* enemyUpdateTrampoline = nullptr;
    void* hudDrawTrampoline = nullptr;
};

AuxiliaryRuntime g_auxiliary{};

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
        g_auxiliary.patchError = true;
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
    auto original = reinterpret_cast<EnemyUpdateFn>(
        g_auxiliary.enemyUpdateTrampoline);
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
    const bool locked = g_auxiliary.lockTime && timeline &&
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
    if (g_auxiliary.enemyUpdateTrampoline)
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
    g_auxiliary.enemyUpdateTrampoline = trampoline;
    WriteAbsoluteJump(target, reinterpret_cast<void*>(HookedEnemyUpdate));
    target[14] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), target, expected.size());
    DWORD ignored = 0;
    VirtualProtect(target, expected.size(), targetProtection, &ignored);
    return true;
}

using HudDrawFn = void(__fastcall*)(void*);
using AsciiPrintfFn = void(__fastcall*)(void*, const void*, const char*, ...);

struct NativeFloat3 {
    float x;
    float y;
    float z;
};

struct NativeAsciiState {
    float scaleX;
    float scaleY;
    std::byte reserved[0x52C0];
    uint8_t projectionMode;
    uint8_t alternateGlyphs;
    uint8_t padding[2];
    uint32_t color;
};

static_assert(offsetof(NativeAsciiState, color) == 0x52CC);

void DrawNativePracticeCounters(void* hud)
{
    auto asciiPrintf = reinterpret_cast<AsciiPrintfFn>(
        ResolveGameAddress<void>(GameAddress::AsciiPrintf));
    auto* asciiState = ResolveGameAddress<NativeAsciiState>(
        GameAddress::AsciiManager);
    const auto* misses = ResolveGameAddress<int32_t>(GameAddress::MissCount);
    const auto* bombsUsed = ResolveGameAddress<int32_t>(
        GameAddress::BombUseCount);
    const auto* infiniteLives = ResolveGameAddress<uint8_t>(
        GameAddress::InfiniteLivesModeFlag);
    if (!hud || !asciiPrintf || !asciiState || !misses ||
        !bombsUsed || !infiniteLives)
        return;

    // These are the stock replay-counter coordinates. The native life icons
    // use y=118 and Bomb icons use y=142, while their text is offset upward by
    // one pixel. The native infinite-lives mode hides miss usage, while Bomb
    // usage remains useful. F2 Lock Lives is a separate helper and must not
    // change counter visibility.

    constexpr NativeFloat3 missPosition{ 824.0f, 117.0f, 0.49f };
    constexpr NativeFloat3 bombPosition{ 824.0f, 141.0f, 0.49f };
    constexpr float kCounterScale = 1.0f;
    constexpr uint32_t kMissColor = 0xFFFF8080; // native ARGB: light red
    constexpr uint32_t kBombColor = 0xFF80FF80; // native ARGB: light green
    const float previousScaleX = asciiState->scaleX;
    const float previousScaleY = asciiState->scaleY;
    const uint32_t previousColor = asciiState->color;
    asciiState->scaleX *= kCounterScale;
    asciiState->scaleY *= kCounterScale;
    // The stock HUD already draws misses when the native infinite-lives flag
    // is set, so only add the missing counter in ordinary finite-lives mode.
    if (*infiniteLives == 0) {
        asciiState->color = kMissColor;
        asciiPrintf(asciiState,&missPosition, "%d", std::clamp(*misses,0,99));
    }
    asciiState->color = kBombColor;
    asciiPrintf(asciiState, &bombPosition, "%d", std::clamp(*bombsUsed,0,99));
    asciiState->scaleX = previousScaleX;
    asciiState->scaleY = previousScaleY;
    asciiState->color = previousColor;

    // spell history
    // IsInSpellCard = 0xBADF60,
    // IsInSpellPracticeMode = 0x4F27B5,
    // SpellCardInfo = 0x4F27C8,
    if (*ResolveGameAddress<bool>(GameAddress::IsInSpellCard))
    {
        int spellid = *ResolveGameAddress<int>(GameAddress::SpellCardId);
        int* spellcardhis = (ResolveGameAddress<int>(GameAddress::SpellCardInfo)) + (96 * spellid);

        int attempt = 0;
        int captured = 0;
        if (*ResolveGameAddress<bool>(GameAddress::IsInSpellPracticeMode))
        {
            int offset =
                (*ResolveGameAddress<unsigned __int8>(GameAddress::CurrentShotType))
                + 2 * (*ResolveGameAddress<unsigned __int8>(GameAddress::CurrentCharacter))
                + 4 * (*ResolveGameAddress<unsigned __int8>(GameAddress::CurrentDifficulty));
            attempt = spellcardhis[offset + 0x38];
            captured = spellcardhis[offset + 0x4C];
        } else {
            attempt = *(reinterpret_cast<short*>(spellcardhis) + 30);
            captured = *(reinterpret_cast<short*>(spellcardhis) + 31);
        }
        constexpr NativeFloat3 spell_his_position{ 522.0f, 48.0, 0.49f };
        constexpr float kCounterScale = 0.5f;
        float playerY = ResolveGameAddress<float>(GameAddress::PlayerPosition)[1];
        uint32_t spellcard_color = 0xFFFFFFFF;
        if(playerY < 100.0f)
            spellcard_color = 0x50FFFFFF;
        const float previousScaleX = asciiState->scaleX;
        const float previousScaleY = asciiState->scaleY;
        const uint32_t previousColor = asciiState->color;
        asciiState->scaleX *= kCounterScale;
        asciiState->scaleY *= kCounterScale;
        asciiState->color = spellcard_color;

        char temp[32];
        char buf[32];
        sprintf_s(temp, "%d/%d", captured, attempt);
        asciiPrintf(asciiState, &spell_his_position, "%10s", temp);
        asciiState->scaleX = previousScaleX;
        asciiState->scaleY = previousScaleY;
        asciiState->color = previousColor;
    }
  
}

void __fastcall HookedHudDraw(void* hud)
{
    const auto original = reinterpret_cast<HudDrawFn>(
        g_auxiliary.hudDrawTrampoline);
    if (original)
        original(hud);
    DrawNativePracticeCounters(hud);
}

bool InstallHudDrawHook()
{
    if (g_auxiliary.hudDrawTrampoline)
        return true;

    constexpr std::array<unsigned char, 19> expected{
        0x4C, 0x8B, 0xDC,                         // mov r11,rsp
        0x55, 0x53,                               // push rbp / push rbx
        0x49, 0x8D, 0xAB, 0x48, 0xFC, 0xFF, 0xFF,// lea rbp,[r11-3b8h]
        0x48, 0x81, 0xEC, 0xA8, 0x04, 0x00, 0x00 // sub rsp,4a8h
    };
    auto* target = ResolveGameAddress<unsigned char>(GameAddress::HudDraw);
    if (!target || !IsGameAddressRangeValid(GameAddress::HudDraw,
            expected.size()) ||
        std::memcmp(target, expected.data(), expected.size()) != 0)
        return false;

    constexpr size_t kJumpSize = 14;
    constexpr size_t kTrampolineSize = expected.size() + kJumpSize;
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
    g_auxiliary.hudDrawTrampoline = trampoline;
    WriteAbsoluteJump(target, reinterpret_cast<void*>(HookedHudDraw));
    std::fill(target + kJumpSize, target + expected.size(),
        static_cast<unsigned char>(0x90));
    FlushInstructionCache(GetCurrentProcess(), target, expected.size());
    DWORD ignored = 0;
    VirtualProtect(target, expected.size(), targetProtection, &ignored);
    return true;
}

} // namespace

bool InstallGameOverlayHook()
{
    const bool enemyInstalled = InstallEnemyUpdateHook();
    const bool hudInstalled = InstallHudDrawHook();
    if (!enemyInstalled || !hudInstalled)
        g_auxiliary.patchError = true;
    if (g_auxiliary.autoBombRelay)
        return enemyInstalled && hudInstalled;

    auto* target = ResolveGameAddress<unsigned char>(
        GameAddress::AutoBombInputCheck);
    auto* continuation = ResolveGameAddress<unsigned char>(
        GameAddress::AutoBombInputContinue);
    auto* deathBomb = ResolveGameAddress<unsigned char>(
        GameAddress::DeathBombBranch);
    auto* actions = ResolveGameAddress<uint32_t>(GameAddress::MenuInputCurrent);
    auto* playerState = ResolveGameAddress<uint8_t>(
        GameAddress::CurrentPlayerState);
    auto* replayMode = ResolveGameAddress<uint8_t>(
        GameAddress::ReplayModeFlag);
    if (!target || !continuation || !deathBomb || !actions || !playerState ||
        !replayMode ||
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
    emitFlagCheck(&g_auxiliary.autoBomb, false);
    emitFlagCheck(&g_auxiliary.noBomb, true);
    emitFlagCheck(replayMode, true);
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
    g_auxiliary.autoBombRelay = relay;
    return enemyInstalled && hudInstalled;
}

void UpdateGameOverlayState()
{
    const bool foreground = IsGameProcessForeground();
    const bool hotkeysEnabled = foreground && !IsKeyBindingCaptureActive();
    if (!hotkeysEnabled) {
        // Drain the shared low-order edge bits while capture owns the
        // keyboard, otherwise a captured F-key/Backspace could trigger here
        // immediately after capture ends.
        GetAsyncKeyState(VK_BACK);
        for (int key = VK_F1; key <= VK_F8; ++key)
            GetAsyncKeyState(key);
    }
    if (hotkeysEnabled && (GetAsyncKeyState(VK_BACK) & 1))
        g_auxiliary.windowVisible = !g_auxiliary.windowVisible;

    ToggleRequested(g_auxiliary.invincible,
        hotkeysEnabled && (GetAsyncKeyState(VK_F1) & 1), ApplyInvincible);
    if (hotkeysEnabled && (GetAsyncKeyState(VK_F2) & 1))
        g_auxiliary.lockLives = !g_auxiliary.lockLives;
    ToggleRequested(g_auxiliary.lockBombs,
        hotkeysEnabled && (GetAsyncKeyState(VK_F3) & 1), ApplyBombs);
    ToggleRequested(g_auxiliary.lockPower,
        hotkeysEnabled && (GetAsyncKeyState(VK_F4) & 1), ApplyPower);
    if (hotkeysEnabled && (GetAsyncKeyState(VK_F5) & 1))
        g_auxiliary.lockTime = !g_auxiliary.lockTime;
    if (hotkeysEnabled && (GetAsyncKeyState(VK_F6) & 1))
        g_auxiliary.autoBomb = !g_auxiliary.autoBomb;
    if (hotkeysEnabled && (GetAsyncKeyState(VK_F7) & 1))
        g_auxiliary.everlastingBgm = !g_auxiliary.everlastingBgm;
    if (hotkeysEnabled && (GetAsyncKeyState(VK_F8) & 1))
        g_auxiliary.noBomb = !g_auxiliary.noBomb;

    // Match thprac's "lock lives / no continue" mode: retain the stock death
    // and life-decrement path while any lives remain, and intervene only once
    // the counter is already zero and the next death would cause game over.
    const auto* lives = ResolveGameAddress<uint8_t>(GameAddress::CurrentLives);
    const bool livesPatchWanted = g_auxiliary.lockLives && lives && *lives == 0;
    if (!ApplyLives(livesPatchWanted))
        g_auxiliary.patchError = true;
}

void UpdateAndDrawGameOverlayUi()
{
    DrawAutoShootIndicator();

    if (!g_auxiliary.windowVisible)
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
        drawState(g_auxiliary.invincible, S(Invincible));
        drawState(g_auxiliary.lockLives, S(LockLives));
        drawState(g_auxiliary.lockBombs, S(LockBombs));
        drawState(g_auxiliary.lockPower, S(LockPower));
        drawState(g_auxiliary.lockTime, S(LockTime));
        drawState(g_auxiliary.autoBomb, S(AutoBomb));
        drawState(g_auxiliary.everlastingBgm, S(EverlastingBgm));
        drawState(g_auxiliary.noBomb, S(DisableBomb));
        if (g_auxiliary.patchError)
            ImGui::TextDisabled("%s", S(PatchUnsupported));
    }
    ImGui::End();
}

bool IsGameOverlayVisible()
{
    return g_auxiliary.windowVisible;
}

bool IsBombInputSuppressed()
{
    return g_auxiliary.noBomb;
}

bool IsEverlastingBgmEnabled()
{
    return g_auxiliary.everlastingBgm;
}

void PrepareEverlastingBgmForInitialization(bool enhancedRetry)
{
    if (auto* keepBgm = ResolveGameAddress<uint8_t>(GameAddress::KeepBgm))
        *keepBgm = static_cast<uint8_t>(
            enhancedRetry && g_auxiliary.everlastingBgm);
}
