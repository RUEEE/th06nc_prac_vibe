#include "practice_menu.h"

#include "game_addresses.h"
#include "game_overlay.h"
#include "keyboard_input.h"
#include "locale.h"
#include "practice_jump.h"
#include "replay_support.h"
#include "imgui.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr unsigned char kExpectedStageSelectorCall[5] = {
    0xE8, 0x39, 0x12, 0x00, 0x00 // call th06nc.exe + 0x4D1F0
};
constexpr unsigned char kExpectedStageSelectorPrologue[5] = {
    0x48, 0x89, 0x5C, 0x24, 0x08 // mov [rsp+8], rbx
};
constexpr unsigned char kExpectedPracticeConfirmTransitionCall[5] = {
    0xE8, 0xF8, 0x18, 0x00, 0x00 // call th06nc.exe + 0x4DA80
};
constexpr unsigned char kExpectedPracticeStageScoreDrawCall[5] = {
    0xE8, 0x3B, 0x4C, 0xFB, 0xFF // call th06nc.exe + 0x9AA0
};
constexpr size_t kPlayerInitializePrologueSize = 15;
constexpr unsigned char kExpectedPlayerInitializePrologue[kPlayerInitializePrologueSize] = {
    0x48, 0x89, 0x5C, 0x24, 0x10,
    0x48, 0x89, 0x6C, 0x24, 0x18,
    0x48, 0x89, 0x74, 0x24, 0x20,
};
constexpr unsigned char kExpectedStageBgmLoadCall[5] = {
    0xE8, 0x60, 0x06, 0x04, 0x00 // call th06nc.exe + 0x7BC80
};
constexpr unsigned char kExpectedStageBackgroundFastForwardPrologue[5] = {
    0x48, 0x89, 0x5C, 0x24, 0x08 // mov [rsp+8], rbx
};
constexpr unsigned char kExpectedStageBackgroundPracticeCheck[9] = {
    0x80, 0xBF, 0x55, 0x09, 0x00, 0x00, 0x00, // cmp byte ptr [rdi+955], 0
    0x74, 0x05                                // je past fast-forward call
};
constexpr unsigned char kExpectedStageBackgroundFastForwardCall[5] = {
    0xE8, 0x94, 0xC0, 0x03, 0x00 // call th06nc.exe + 0x77550
};
constexpr unsigned char kExpectedStageBackgroundSpecialPracticeCheck[9] = {
    0x80, 0xBF, 0x55, 0x09, 0x00, 0x00, 0x00, // cmp byte ptr [rdi+955], 0
    0x74, 0x7C                                // je past Stage 6/Extra special setup
};
constexpr unsigned char kExpectedSpellPracticeGuiCheck[7] = {
    0x80, 0x3D, 0x57, 0x40, 0x4B, 0x00, 0x00 // cmp byte ptr [+4F27B5], 0
};
constexpr unsigned char kExpectedStageTitlePracticeCheck[9] = {
    0x80, 0x3D, 0x4D, 0x3D, 0x4B, 0x00, 0x00, // cmp byte ptr [+4F27B5], 0
    0x74, 0x34                                // je +0x34
};
constexpr unsigned char kExpectedPlayerEntryPracticeCheck[7] = {
    0x80, 0x3D, 0x92, 0xA3, 0x48, 0x00, 0x00 // cmp byte ptr [+4F27B5], 0
};
constexpr unsigned char kExpectedPlayerStateDispatchRead[7] = {
    0x0F, 0xB6, 0x87, 0x98, 0x78, 0x00, 0x00 // movzx eax, byte ptr [rdi+7898]
};
constexpr ptrdiff_t kPlayerStateOffset = 0x7898;
constexpr ptrdiff_t kPlayerStateTimerOffset = 0x7858;
constexpr ptrdiff_t kStageToBossBgmFieldOffset = 0x80;
constexpr ULONGLONG kMenuHeartbeatTimeoutMs = 350;
constexpr int kMaximumKnownStageCount = 7;
constexpr uint32_t kMenuConfirmMask = 0x100; // Z / menu-confirm bit.
constexpr uint32_t kMenuUpMask = 0x10;
constexpr uint32_t kMenuDownMask = 0x20;
constexpr uint32_t kMenuLeftMask = 0x40;
constexpr uint32_t kMenuRightMask = 0x80;
constexpr uint32_t kNavigateUp = 1u << 0;
constexpr uint32_t kNavigateDown = 1u << 1;
constexpr uint32_t kNavigateLeft = 1u << 2;
constexpr uint32_t kNavigateRight = 1u << 3;

enum class PracticeHookStatusValue : LONG {
    NotInstalled,
    Installed,
    StageUiOnly,
    UnsupportedExecutable,
    AllocationFailed,
    PatchFailed,
};

using NativeStageSelectorFn = int(__fastcall*)(void* menu, int stageCount);
using NativePracticeMenuTransitionFn = void(__fastcall*)(void* menu, int nextState,
    int transitionFlag);
using PlayerInitializeFn = int(__fastcall*)(void* player);
using BgmLoadFn = int(__fastcall*)(void* audioState, const char* path);

struct PracticeHooks {
    NativeStageSelectorFn stageSelector = nullptr;
    NativePracticeMenuTransitionFn menuTransition = nullptr;
    PlayerInitializeFn playerInitialize = nullptr;
    BgmLoadFn bgmLoad = nullptr;
};

struct PracticeRuntime {
    PracticeHookStatusValue hookStatus = PracticeHookStatusValue::NotInstalled;
    std::byte* menuObject = nullptr;
    ULONGLONG lastMenuUpdateTick = 0;
    bool bypassNextPracticeConfirmation = false;
    bool startRequested = false;
    uint32_t navigationActions = 0;
    bool selectionInitialized = false;
    bool enhancedSessionActive = false;
    bool enhancedInitialLoadPending = false;
    bool enhancedRestartPending = false;
    bool restartBgmCompatible = false;
    int runningStage = 0;
    bool runningBossBgm = false;
    bool clearInitialPlayerStatePending = false;
};

PracticeParam g_practiceParam{};
PracticeRuntime g_practiceRuntime{};
PracticeHooks g_practiceHooks{};

template <typename T>
T Exchange(T& target, T replacement)
{
    const T previous = target;
    target = replacement;
    return previous;
}

template <typename T>
T& MenuField(std::byte* menu, PracticeMenuField field)
{
    return *reinterpret_cast<T*>(menu + GameField(field));
}

template <typename T>
const T& MenuField(const std::byte* menu, PracticeMenuField field)
{
    return *reinterpret_cast<const T*>(menu + GameField(field));
}

int ClampStage(int stage, int stageCount)
{
    return std::clamp(stage, 0, std::max(1, stageCount) - 1);
}

int WrapSelection(int value, int delta, int count)
{
    if (count <= 0)
        return 0;
    value = (value + delta) % count;
    return value < 0 ? value + count : value;
}
bool BossTypeMatches(int filter, Bosstype type)
{
    switch (filter) {
    case 2: return type == MID_BOSS_NONSPELL || type == MID_BOSS_SPELL;
    case 3: return type == BOSS_NONSPELL || type == BOSS_SPELL;
    case 4: return type == MID_BOSS_NONSPELL || type == BOSS_NONSPELL;
    case 5: return type == MID_BOSS_SPELL || type == BOSS_SPELL;
    default: return false;
    }
}

bool IsActiveEnhancedPracticeRun()
{
    if (!g_practiceRuntime.enhancedSessionActive)
        return false;
    const auto* nativeSpellFlag =
        ResolveGameAddress<uint8_t>(GameAddress::NativeSpellPracticeFlag);
    // +4F27B4 is written too late for several first-load paths (stage title,
    // player entry and background pre-advance). The enhanced session is
    // latched at confirmation and cleared before a later non-practice init.
    return !nativeSpellFlag || *nativeSpellFlag == 0;
}

bool IsMainBossJumpSelected()
{
    const int target = g_practiceParam.warpTarget;
    if (target < 2 || target > 5)
        return false;

    const JumpEnum selected = static_cast<JumpEnum>(g_practiceParam.bossJump);
    const auto& jumps = BossJumps();
    const auto found = std::find_if(jumps.begin(), jumps.end(),
        [selected](const BossJump& jump) { return jump.jumpname == selected; });
    return found != jumps.end() &&
        (found->type == BOSS_NONSPELL || found->type == BOSS_SPELL);
}

bool IsMainBossPracticeSelected()
{
    return IsActiveEnhancedPracticeRun() && IsMainBossJumpSelected();
}

uint8_t __fastcall ShouldUseNativePracticePresentation()
{
    const auto* nativeSpellFlag =
        ResolveGameAddress<uint8_t>(GameAddress::NativeSpellPracticeFlag);
    if (nativeSpellFlag && *nativeSpellFlag != 0)
        return 1;
    return IsActiveEnhancedPracticeRun() ? 1 : 0;
}

uint8_t __fastcall ReadPlayerStateForDispatch(void* player)
{
    if (!player)
        return 0;
    auto* bytes = static_cast<std::byte*>(player);
    auto* state = reinterpret_cast<uint8_t*>(bytes + kPlayerStateOffset);
    if (IsActiveEnhancedPracticeRun() &&
        Exchange(g_practiceRuntime.clearInitialPlayerStatePending, false)) {
        // The new executable can restore the entry state after its native
        // initialization. Correct it at the first following state dispatch,
        // then leave death/respawn and every later state transition untouched.
        *state = 0;
        *reinterpret_cast<int*>(bytes + kPlayerStateTimerOffset) = 0;
    }
    return *state;
}

int __fastcall HookedStageBgmLoad(void* audioState, const char* stageBgmPath)
{
    // Each stage asset record stores its stage and Boss BGM paths 0x80 bytes
    // apart.  The native spell-practice flag selects the latter; enhanced
    // Boss practice has no such flag, so make the same selection here.
    const char* selectedPath = stageBgmPath;
    if (stageBgmPath && IsMainBossPracticeSelected())
        selectedPath += kStageToBossBgmFieldOffset;
    return g_practiceHooks.bgmLoad(audioState, selectedPath);
}

void QueueConfiguredPracticeJump()
{
    const int stage = g_practiceParam.stage + 1;
    if (g_practiceParam.warpTarget == 1) {
        const auto found = StageChapterTimes().find(stage);
        if (found != StageChapterTimes().end() && ((!found->second.first.empty()) || (!found->second.second.empty()))) {
            const int chapter = std::clamp(g_practiceParam.chapter, 1,
                static_cast<int>(found->second.first.size() + found->second.second.size() ));
            if (stage == 4 && chapter == 4) {
                int x[6]{};
                int y[6]{};
                for (int book = 0; book < 6; ++book) {
                    x[book] = g_practiceParam.bookX[book];
                    y[book] = g_practiceParam.bookY[book];
                }
                QueueStage4BooksPracticeJump(GetChapterTime(stage,chapter),   g_practiceParam.bookFixedMask, x, y);
            } else {
                QueueStagePracticeJump(stage, GetChapterTime(stage, chapter));
            }
        }
    } else if (g_practiceParam.warpTarget >= 2 &&
        g_practiceParam.warpTarget <= 5 &&
        g_practiceParam.bossJump != TH06NC_JUMP_NONE) {
        if (stage == 4 && g_practiceParam.bossJump == TH06NC_ST4_BOOKS) {
            int x[6]{};
            int y[6]{};
            for (int book = 0; book < 6; ++book) {
                x[book] = g_practiceParam.bookX[book];
                y[book] = g_practiceParam.bookY[book];
            }
            QueueBossPracticeJump(stage,
                static_cast<JumpEnum>(g_practiceParam.bossJump),
                g_practiceParam.dialogue != 0, g_practiceParam.fakeShot,
                g_practiceParam.stage5Boss6Mode, g_practiceParam.bookFixedMask, x, y);
        }else {
            QueueBossPracticeJump(stage,
                static_cast<JumpEnum>(g_practiceParam.bossJump),
                g_practiceParam.dialogue != 0, g_practiceParam.fakeShot,
                g_practiceParam.stage5Boss6Mode);
        }
    } else if (g_practiceParam.warpTarget == 6) {
        QueueStagePracticeJump(stage,
            std::max(g_practiceParam.timelineFrame, 0));
    }
}

void ApplyConfiguredPracticeResources()
{
    auto* lives = ResolveGameAddress<uint8_t>(GameAddress::CurrentLives);
    auto* bombs = ResolveGameAddress<uint8_t>(GameAddress::CurrentBombs);
    auto* initialLives = ResolveGameAddress<uint8_t>(GameAddress::InitialLivesBackup);
    auto* initialBombs = ResolveGameAddress<uint8_t>(GameAddress::InitialBombsBackup);
    auto* score = ResolveGameAddress<int64_t>(GameAddress::CurrentScore);
    auto* score2 = ResolveGameAddress<int64_t>(GameAddress::CurrentRenderScore);
    auto* power = ResolveGameAddress<uint16_t>(GameAddress::CurrentPower);
    auto* graze = ResolveGameAddress<int32_t>(GameAddress::CurrentGraze);
    auto* pointItems = ResolveGameAddress<uint16_t>(GameAddress::CurrentPointItems);
    if (lives) *lives = static_cast<uint8_t>(g_practiceParam.lives);
    if (bombs) *bombs = static_cast<uint8_t>(g_practiceParam.bombs);
    if (initialLives) *initialLives = static_cast<uint8_t>(g_practiceParam.lives);
    if (initialBombs) *initialBombs = static_cast<uint8_t>(g_practiceParam.bombs);
    if (score) *score = * score2 = g_practiceParam.score;
    if (power) *power = static_cast<uint16_t>(g_practiceParam.power);
    if (graze) *graze = g_practiceParam.graze;
    if (pointItems)
        *pointItems = static_cast<uint16_t>(g_practiceParam.pointItems);
}

uint8_t __fastcall ShouldPreAdvanceStageBackground()
{
    const auto* nativeSpellFlag =
        ResolveGameAddress<uint8_t>(GameAddress::NativeSpellPracticeFlag);
    if (nativeSpellFlag && *nativeSpellFlag != 0)
        return 1;
    return IsMainBossPracticeSelected() ? 1 : 0;
}

uint8_t SelectNativeBossPresentationSpellId()
{
    using LoadSpellMetadataFn = void(__fastcall*)();
    auto loadMetadata = reinterpret_cast<LoadSpellMetadataFn>(
        ResolveGameAddress<void>(GameAddress::LoadSpellMetadata));
    if (loadMetadata)
        loadMetadata();

    const auto* count = ResolveGameAddress<int>(GameAddress::SpellMetadataCount);
    auto** tableAddress =
        ResolveGameAddress<std::byte*>(GameAddress::SpellMetadataTable);
    const auto* stage = ResolveGameAddress<int>(GameAddress::CurrentStage);
    const auto* difficulty =
        ResolveGameAddress<int>(GameAddress::CurrentDifficulty);
    if (!count || !tableAddress || !*tableAddress || !stage || !difficulty)
        return 0xFF;

    // The native Stage 6/Extra setup at +0x3B4DF loads eff06/eff07 only for
    // cards whose metadata special flag is clear. Select such a real card from
    // the active stage/difficulty rather than guessing a global spell ID.
    const std::byte* table = *tableAddress;
    for (int i = 0; i < *count; ++i) {
        const auto* meta = table + static_cast<size_t>(i) * 0x28;
        if (static_cast<uint8_t>(meta[1]) == *stage &&
            static_cast<uint8_t>(meta[4]) == *difficulty &&
            static_cast<uint8_t>(meta[6]) == 0)
            return static_cast<uint8_t>(meta[0]);
    }
    return 0xFF;
}

void __fastcall HookedStageBackgroundFastForward()
{
    using FastForwardFn = void(__fastcall*)();
    auto fastForward = reinterpret_cast<FastForwardFn>(
        ResolveGameAddress<void>(GameAddress::StageBackgroundFastForward));
    auto* spellId = ResolveGameAddress<uint8_t>(GameAddress::CurrentSpellId);
    if (!fastForward)
        return;

    // Execute at the original +0x3B4B7 call site. The native implementation
    // uses both CurrentSpellId here and the following +0x3B4DF special setup;
    // keep the selected presentation ID in place for that second phase.
    if (spellId && IsMainBossPracticeSelected())
        *spellId = SelectNativeBossPresentationSpellId();
    fastForward();
}

int __fastcall HookedPlayerInitialize(void* player)
{
    // ReplayPath and ReplayModeFlag are already populated before the common
    // initializer. Restore the enhanced-practice parameters before any of its
    // BGM/background decisions execute.
    const bool restoredPracticeReplay = PreparePracticeReplayPlayback();
    if (restoredPracticeReplay) {
        if (auto* stage = ResolveGameAddress<int>(GameAddress::CurrentStage))
            *stage = g_practiceParam.stage;
        if (auto* difficulty =
                ResolveGameAddress<int>(GameAddress::CurrentDifficulty))
            *difficulty = g_practiceParam.stage == 6
                ? 4 : std::clamp(g_practiceParam.difficulty, 0, 3);
        if (auto* practice =
                ResolveGameAddress<uint8_t>(GameAddress::PracticeModeFlag))
            *practice = 1;
        if (auto* spellPractice = ResolveGameAddress<uint8_t>(
                GameAddress::NativeSpellPracticeFlag))
            *spellPractice = 0;
    }
    const bool initialEnhancedLoad =
        Exchange(g_practiceRuntime.enhancedInitialLoadPending, false);
    const bool enhancedRestart =
        Exchange(g_practiceRuntime.enhancedRestartPending, false);
    if (g_practiceRuntime.enhancedSessionActive && !initialEnhancedLoad &&
        !enhancedRestart) {
        const auto* practiceFlag =
            ResolveGameAddress<uint8_t>(GameAddress::PracticeModeFlag);
        const auto* nativeSpellFlag =
            ResolveGameAddress<uint8_t>(GameAddress::NativeSpellPracticeFlag);
        // This runs before BGM/background/player initialization. End a stale
        // enhanced session here so a following normal or native-spell run is
        // never treated as enhanced.
        if ((practiceFlag && *practiceFlag == 0) ||
            (nativeSpellFlag && *nativeSpellFlag != 0)) {
            g_practiceRuntime.enhancedSessionActive = false;
            g_practiceRuntime.clearInitialPlayerStatePending = false;
        }
    }

    // Match the game's native practice-retry mechanism. KeepBgm must be set
    // before the common initializer makes its audio teardown/load decision;
    // first entry and every non-enhanced initialization explicitly clear it.
    PrepareEverlastingBgmForInitialization(
        g_practiceRuntime.enhancedSessionActive && enhancedRestart &&
            g_practiceRuntime.restartBgmCompatible);

    if (g_practiceRuntime.enhancedSessionActive && enhancedRestart) {
        // State 12 may restore the old stage between the pause action and this
        // callback. Reassert the edited selection immediately before native
        // stage/ECL loading, and make the request visible to the earliest
        // timeline callback created by that initialization.
        if (auto* stage = ResolveGameAddress<int>(GameAddress::CurrentStage))
            *stage = g_practiceParam.stage;
        if (auto* difficulty =
                ResolveGameAddress<int>(GameAddress::CurrentDifficulty))
            *difficulty = g_practiceParam.stage == 6
                ? 4 : std::clamp(g_practiceParam.difficulty, 0, 3);
        ClearQueuedPracticeJump();
        QueueConfiguredPracticeJump();
    }

    const int result = g_practiceHooks.playerInitialize(player);
    if (result != 0) {
        g_practiceRuntime.clearInitialPlayerStatePending = false;
        return result;
    }

    if (IsActiveEnhancedPracticeRun()) {
        // Run after the whole +0x3A9C0 initializer. This is later than every
        // native score/power/graze/point write and is shared by first entry
        // and the in-game restart path through +0x3A810.
        ApplyConfiguredPracticeResources();
        QueueConfiguredPracticeJump();
        g_practiceRuntime.runningStage = g_practiceParam.stage;
        g_practiceRuntime.runningBossBgm = IsMainBossJumpSelected();
        CapturePracticeReplayStart();
        g_practiceRuntime.clearInitialPlayerStatePending = true;
    } else {
        // A request normally gets consumed on the first timeline update. If
        // the player exits during the transition, discard it here before a
        // normal run or the game's own spell-practice ECL can see it.
        // For a non-replay normal/native-practice initialization this also
        // clears any unsaved Enhanced-Practice replay snapshot.
        CapturePracticeReplayStart();
        g_practiceRuntime.clearInitialPlayerStatePending = false;
        ClearQueuedPracticeJump();
    }
    return result;
}

// This hook runs on the game's update thread. It must not call ImGui because
// the ImGui frame is owned by Present; it only synchronizes the one large UI
// function below with the native menu and then reuses the original selector.
int __fastcall HookedPracticeMenuUi(void* rawMenu, int nativeStageCount)
{
    auto* menu = static_cast<std::byte*>(rawMenu);
    if (!menu || !g_practiceHooks.stageSelector)
        return 0;

    const ULONGLONG now = GetTickCount64();
    const ULONGLONG previousTick =
        Exchange(g_practiceRuntime.lastMenuUpdateTick, now);
    std::byte* previousMenu = Exchange(g_practiceRuntime.menuObject, menu);
    const int stageCount = kMaximumKnownStageCount;

    if (previousMenu != menu || now - previousTick > kMenuHeartbeatTimeoutMs) {
        g_practiceRuntime.navigationActions = 0;
        g_practiceRuntime.bypassNextPracticeConfirmation = false;
        if (!Exchange(g_practiceRuntime.selectionInitialized, true)) {
            g_practiceParam.stage = ClampStage(
                MenuField<int>(menu, PracticeMenuField::SelectedStage), stageCount);
        }
        if (const auto* difficulty = ResolveGameAddress<int>(GameAddress::CurrentDifficulty);
            difficulty && *difficulty >= 0 && *difficulty <= 3) {
            // Stage 7 temporarily forces this global to Extra (4). Preserve
            // the last main-game difficulty instead of letting Extra leak
            // into the next stage selection and BossJump difficulty mask.
            g_practiceParam.difficulty = *difficulty;
        }
    }

    // Consume the game's logical menu word, not physical virtual keys. This
    // preserves the player's bindings and makes controller/D-pad navigation
    // follow the exact same edge/repeat behavior as the native selector.
    auto* current = ResolveGameAddress<uint32_t>(GameAddress::MenuInputCurrent);
    auto* previous = ResolveGameAddress<uint32_t>(GameAddress::MenuInputPrevious);
    const auto* repeat = ResolveGameAddress<uint16_t>(GameAddress::MenuInputRepeat);
    uint32_t navigation = 0;
    const auto logicalNavigation = [&](uint32_t mask) {
        return current && previous && (*current & mask) != 0 &&
            (((*current ^ *previous) & mask) != 0 || (repeat && *repeat != 0));
    };
    if (logicalNavigation(kMenuUpMask)) navigation |= kNavigateUp;
    if (logicalNavigation(kMenuDownMask)) navigation |= kNavigateDown;
    if (logicalNavigation(kMenuLeftMask)) navigation |= kNavigateLeft;
    if (logicalNavigation(kMenuRightMask)) navigation |= kNavigateRight;
    if (navigation != 0)
        g_practiceRuntime.navigationActions |= navigation;

    MenuField<int>(menu, PracticeMenuField::SelectedStage) =
        ClampStage(g_practiceParam.stage, stageCount);

    // The code immediately following the patched CALL still owns Z/X handling,
    // fade timing, sound, and stage load.  Do not accept the native selector's
    // stage result: arrows now edit whichever replacement-UI row is selected.
    (void)nativeStageCount;
    const int result =
        g_practiceHooks.stageSelector(menu, kMaximumKnownStageCount);
    MenuField<int>(menu, PracticeMenuField::SelectedStage) =
        ClampStage(g_practiceParam.stage, stageCount);

    // The ImGui Start button injects the same confirm edge consumed directly
    // after +0x4BFB2. Every device mapped to logical Confirm follows this path.
    if (current && previous) {
        if (Exchange(g_practiceRuntime.startRequested, false)) {
            *current |= kMenuConfirmMask;
            *previous &= ~kMenuConfirmMask;
        }
        const bool confirming = (*current & kMenuConfirmMask) != 0 &&
            (*current & kMenuConfirmMask) != (*previous & kMenuConfirmMask);
        if (confirming) {
            if (auto* difficulty = ResolveGameAddress<int>(GameAddress::CurrentDifficulty)) {
                *difficulty = g_practiceParam.stage == 6
                    ? 4
                    : g_practiceParam.difficulty;
            }
            const bool enhanced = g_practiceParam.practiceMode == 1;
            g_practiceRuntime.enhancedSessionActive = enhanced;
            g_practiceRuntime.enhancedInitialLoadPending = enhanced;
            g_practiceRuntime.clearInitialPlayerStatePending = false;
            // Both Original and Enhanced selections use the replacement UI;
            // bypass the native second confirmation for either mode.
            g_practiceRuntime.bypassNextPracticeConfirmation = true;
            ClearQueuedPracticeJump();
            if (g_practiceRuntime.enhancedSessionActive)
                QueueConfiguredPracticeJump();
            // Establish replay ownership before gameplay starts. The common
            // initializer refreshes this snapshot after it has successfully
            // applied the selected configuration.
            CapturePracticeReplayStart();
        }
    }
    return result;
}

void __fastcall HookedPracticeConfirmTransition(
    void* rawMenu, int nextState, int transitionFlag)
{
    auto* menu = static_cast<std::byte*>(rawMenu);
    if (!menu || !Exchange(
            g_practiceRuntime.bypassNextPracticeConfirmation, false)) {
        if (g_practiceHooks.menuTransition)
            g_practiceHooks.menuTransition(rawMenu, nextState, transitionFlag);
        return;
    }

    // The native first Z press transitions into state 0x16 and waits for a
    // second confirmation. Reproduce the second-Z path at +0x4C420 directly so
    // its sound/fade bookkeeping is retained without ever showing that screen.
    using QueueSoundFn = void(__fastcall*)(void* soundState, int soundId);
    using FlushSoundsFn = void(__fastcall*)(void* soundState);
    using StartFadeFn = void(__fastcall*)(int color, int frames);
    auto queueSound = reinterpret_cast<QueueSoundFn>(
        ResolveGameAddress<void>(GameAddress::MenuQueueSound));
    auto flushSounds = reinterpret_cast<FlushSoundsFn>(
        ResolveGameAddress<void>(GameAddress::MenuFlushSounds));
    auto startFade = reinterpret_cast<StartFadeFn>(
        ResolveGameAddress<void>(GameAddress::MenuStartFade));
    void* soundState = ResolveGameAddress<void>(GameAddress::MenuSoundState);

    if (!queueSound || !flushSounds || !startFade || !soundState) {
        if (g_practiceHooks.menuTransition)
            g_practiceHooks.menuTransition(rawMenu, nextState, transitionFlag);
        return;
    }

    queueSound(soundState, 0x0A);
    flushSounds(soundState);
    startFade(0, 0x1E);

    if (auto* exitFlag = ResolveGameAddress<uint8_t>(GameAddress::MenuExitFlag))
        *exitFlag = 0;
    const auto* specialFlag =
        ResolveGameAddress<uint8_t>(GameAddress::MenuSoundSpecialFlag);
    if (specialFlag && *specialFlag != 0) {
        if (auto* pendingFlag =
                ResolveGameAddress<uint8_t>(GameAddress::MenuSoundPendingFlag))
            *pendingFlag = 1;
        *reinterpret_cast<int*>(soundState) = 0x1E;
        *reinterpret_cast<int*>(static_cast<std::byte*>(soundState) + 4) = 0x1E;
    }

    MenuField<int>(menu, PracticeMenuField::State) = 0x19;
    MenuField<int>(menu, PracticeMenuField::TransitionFrame) = 0;
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
            if (candidate <= high && candidate <= regionEnd && size <= regionEnd - candidate) {
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
    destination[0] = 0xFF;
    destination[1] = 0x25;
    *reinterpret_cast<uint32_t*>(destination + 2) = 0;
    *reinterpret_cast<uintptr_t*>(destination + 6) = reinterpret_cast<uintptr_t>(target);
}

bool PatchPlayerInitialize()
{
    auto* target = ResolveGameAddress<unsigned char>(GameAddress::PlayerInitialize);
    if (!target || std::memcmp(target, kExpectedPlayerInitializePrologue,
            sizeof(kExpectedPlayerInitializePrologue)) != 0)
        return false;

    auto* block = static_cast<unsigned char*>(AllocateNearAddress(target, 64));
    if (!block)
        return false;
    unsigned char* relay = block;
    unsigned char* trampoline = block + 16;
    WriteAbsoluteJump(relay, reinterpret_cast<void*>(HookedPlayerInitialize));
    std::memcpy(trampoline, target, kPlayerInitializePrologueSize);
    WriteAbsoluteJump(trampoline + kPlayerInitializePrologueSize,
        target + kPlayerInitializePrologueSize);

    DWORD oldBlockProtection = 0;
    if (!VirtualProtect(block, 64, PAGE_EXECUTE_READ, &oldBlockProtection)) {
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }
    const intptr_t relative = reinterpret_cast<intptr_t>(relay) -
        (reinterpret_cast<intptr_t>(target) + 5);
    if (relative < std::numeric_limits<int32_t>::min() ||
        relative > std::numeric_limits<int32_t>::max()) {
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }

    DWORD oldTargetProtection = 0;
    if (!VirtualProtect(target, kPlayerInitializePrologueSize,
            PAGE_EXECUTE_READWRITE, &oldTargetProtection)) {
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }
    g_practiceHooks.playerInitialize =
        reinterpret_cast<PlayerInitializeFn>(trampoline);
    target[0] = 0xE9;
    *reinterpret_cast<int32_t*>(target + 1) = static_cast<int32_t>(relative);
    std::memset(target + 5, 0x90, kPlayerInitializePrologueSize - 5);
    FlushInstructionCache(GetCurrentProcess(), target, kPlayerInitializePrologueSize);
    DWORD ignored = 0;
    VirtualProtect(target, kPlayerInitializePrologueSize, oldTargetProtection, &ignored);
    return true;
}

bool PatchStageBgmLoadCall()
{
    auto* callSite = ResolveGameAddress<unsigned char>(GameAddress::StageBgmLoadCall);
    auto* nativeLoad = ResolveGameAddress<unsigned char>(GameAddress::BgmLoad);
    if (!callSite || !nativeLoad ||
        std::memcmp(callSite, kExpectedStageBgmLoadCall,
            sizeof(kExpectedStageBgmLoadCall)) != 0)
        return false;

    auto* relay = static_cast<unsigned char*>(AllocateNearAddress(callSite, 32));
    if (!relay)
        return false;
    WriteAbsoluteJump(relay, reinterpret_cast<void*>(HookedStageBgmLoad));

    DWORD oldRelayProtection = 0;
    if (!VirtualProtect(relay, 32, PAGE_EXECUTE_READ, &oldRelayProtection)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, 32);

    const intptr_t relative = reinterpret_cast<intptr_t>(relay) -
        (reinterpret_cast<intptr_t>(callSite) + 5);
    if (relative < std::numeric_limits<int32_t>::min() ||
        relative > std::numeric_limits<int32_t>::max()) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }

    DWORD oldCallProtection = 0;
    if (!VirtualProtect(callSite, sizeof(kExpectedStageBgmLoadCall),
            PAGE_EXECUTE_READWRITE, &oldCallProtection)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    g_practiceHooks.bgmLoad = reinterpret_cast<BgmLoadFn>(nativeLoad);
    callSite[0] = 0xE8;
    *reinterpret_cast<int32_t*>(callSite + 1) = static_cast<int32_t>(relative);
    FlushInstructionCache(GetCurrentProcess(), callSite,
        sizeof(kExpectedStageBgmLoadCall));
    DWORD ignored = 0;
    VirtualProtect(callSite, sizeof(kExpectedStageBgmLoadCall),
        oldCallProtection, &ignored);
    return true;
}

bool PatchStageBackgroundPracticePath()
{
    auto* check = ResolveGameAddress<unsigned char>(
        GameAddress::StageBackgroundPracticeCheck);
    auto* callSite = ResolveGameAddress<unsigned char>(
        GameAddress::StageBackgroundFastForwardCall);
    if (!check || !callSite || callSite != check + 9 ||
        std::memcmp(check, kExpectedStageBackgroundPracticeCheck,
            sizeof(kExpectedStageBackgroundPracticeCheck)) != 0 ||
        std::memcmp(callSite, kExpectedStageBackgroundFastForwardCall,
            sizeof(kExpectedStageBackgroundFastForwardCall)) != 0)
        return false;

    auto* block = static_cast<unsigned char*>(AllocateNearAddress(check, 64));
    if (!block)
        return false;
    unsigned char* checkRelay = block;
    unsigned char* callRelay = block + 16;
    WriteAbsoluteJump(checkRelay,
        reinterpret_cast<void*>(ShouldPreAdvanceStageBackground));
    WriteAbsoluteJump(callRelay,
        reinterpret_cast<void*>(HookedStageBackgroundFastForward));

    DWORD oldRelayProtection = 0;
    if (!VirtualProtect(block, 64, PAGE_EXECUTE_READ, &oldRelayProtection)) {
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), block, 64);

    const intptr_t checkRelative = reinterpret_cast<intptr_t>(checkRelay) -
        (reinterpret_cast<intptr_t>(check) + 5);
    const intptr_t callRelative = reinterpret_cast<intptr_t>(callRelay) -
        (reinterpret_cast<intptr_t>(callSite) + 5);
    if (checkRelative < std::numeric_limits<int32_t>::min() ||
        checkRelative > std::numeric_limits<int32_t>::max() ||
        callRelative < std::numeric_limits<int32_t>::min() ||
        callRelative > std::numeric_limits<int32_t>::max()) {
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }

    constexpr size_t patchSize =
        sizeof(kExpectedStageBackgroundPracticeCheck) +
        sizeof(kExpectedStageBackgroundFastForwardCall);
    DWORD oldProtection = 0;
    if (!VirtualProtect(check, patchSize, PAGE_EXECUTE_READWRITE,
            &oldProtection)) {
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }

    // Preserve the native branch shape, but admit enhanced main-Boss practice.
    check[0] = 0xE8;
    *reinterpret_cast<int32_t*>(check + 1) =
        static_cast<int32_t>(checkRelative);
    check[5] = 0x84;
    check[6] = 0xC0;
    check[7] = 0x74;
    check[8] = 0x05;
    callSite[0] = 0xE8;
    *reinterpret_cast<int32_t*>(callSite + 1) =
        static_cast<int32_t>(callRelative);
    FlushInstructionCache(GetCurrentProcess(), check, patchSize);
    DWORD ignored = 0;
    VirtualProtect(check, patchSize, oldProtection, &ignored);
    return true;
}

bool PatchStageBackgroundSpecialPracticeCheck()
{
    auto* check = ResolveGameAddress<unsigned char>(
        GameAddress::StageBackgroundSpecialPracticeCheck);
    if (!check || std::memcmp(check,
            kExpectedStageBackgroundSpecialPracticeCheck,
            sizeof(kExpectedStageBackgroundSpecialPracticeCheck)) != 0)
        return false;

    auto* relay = static_cast<unsigned char*>(AllocateNearAddress(check, 32));
    if (!relay)
        return false;
    WriteAbsoluteJump(relay,
        reinterpret_cast<void*>(ShouldPreAdvanceStageBackground));

    DWORD oldRelayProtection = 0;
    if (!VirtualProtect(relay, 32, PAGE_EXECUTE_READ, &oldRelayProtection)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, 32);

    const intptr_t relative = reinterpret_cast<intptr_t>(relay) -
        (reinterpret_cast<intptr_t>(check) + 5);
    if (relative < std::numeric_limits<int32_t>::min() ||
        relative > std::numeric_limits<int32_t>::max()) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }

    DWORD oldProtection = 0;
    if (!VirtualProtect(check,
            sizeof(kExpectedStageBackgroundSpecialPracticeCheck),
            PAGE_EXECUTE_READWRITE, &oldProtection)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }

    // Preserve the native +0x3B4DF branch, including its Stage 6/Extra
    // eff06/eff07 loader, while treating enhanced Boss practice as native
    // spell practice for presentation initialization only.
    check[0] = 0xE8;
    *reinterpret_cast<int32_t*>(check + 1) = static_cast<int32_t>(relative);
    check[5] = 0x84;
    check[6] = 0xC0;
    check[7] = 0x74;
    check[8] = 0x7C;
    FlushInstructionCache(GetCurrentProcess(), check,
        sizeof(kExpectedStageBackgroundSpecialPracticeCheck));
    DWORD ignored = 0;
    VirtualProtect(check,
        sizeof(kExpectedStageBackgroundSpecialPracticeCheck),
        oldProtection, &ignored);
    return true;
}

bool PatchStageTitlePracticeCheck()
{
    auto* check = ResolveGameAddress<unsigned char>(
        GameAddress::StageTitlePracticeCheck);
    if (!check || std::memcmp(check, kExpectedStageTitlePracticeCheck,
            sizeof(kExpectedStageTitlePracticeCheck)) != 0)
        return false;

    auto* relay = static_cast<unsigned char*>(AllocateNearAddress(check, 32));
    if (!relay)
        return false;
    WriteAbsoluteJump(relay,
        reinterpret_cast<void*>(ShouldUseNativePracticePresentation));

    DWORD oldRelayProtection = 0;
    if (!VirtualProtect(relay, 32, PAGE_EXECUTE_READ, &oldRelayProtection)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, 32);

    const intptr_t relative = reinterpret_cast<intptr_t>(relay) -
        (reinterpret_cast<intptr_t>(check) + 5);
    if (relative < std::numeric_limits<int32_t>::min() ||
        relative > std::numeric_limits<int32_t>::max()) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }

    DWORD oldProtection = 0;
    if (!VirtualProtect(check, sizeof(kExpectedStageTitlePracticeCheck),
            PAGE_EXECUTE_READWRITE, &oldProtection)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }

    // Use the native spell-practice title fast-forward branch.
    check[0] = 0xE8;
    *reinterpret_cast<int32_t*>(check + 1) = static_cast<int32_t>(relative);
    check[5] = 0x84;
    check[6] = 0xC0;
    check[7] = 0x74;
    check[8] = 0x34;
    FlushInstructionCache(GetCurrentProcess(), check,
        sizeof(kExpectedStageTitlePracticeCheck));
    DWORD ignored = 0;
    VirtualProtect(check, sizeof(kExpectedStageTitlePracticeCheck),
        oldProtection, &ignored);
    return true;
}

bool PatchSpellPracticeGuiCheck()
{
    auto* check = ResolveGameAddress<unsigned char>(
        GameAddress::SpellPracticeGuiCheck);
    if (!check || std::memcmp(check, kExpectedSpellPracticeGuiCheck,
            sizeof(kExpectedSpellPracticeGuiCheck)) != 0)
        return false;

    auto* relay = static_cast<unsigned char*>(AllocateNearAddress(check, 32));
    if (!relay)
        return false;
    WriteAbsoluteJump(relay,
        reinterpret_cast<void*>(ShouldUseNativePracticePresentation));

    DWORD oldRelayProtection = 0;
    if (!VirtualProtect(relay, 32, PAGE_EXECUTE_READ, &oldRelayProtection)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, 32);

    const intptr_t relative = reinterpret_cast<intptr_t>(relay) -
        (reinterpret_cast<intptr_t>(check) + 5);
    if (relative < std::numeric_limits<int32_t>::min() ||
        relative > std::numeric_limits<int32_t>::max()) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }

    DWORD oldProtection = 0;
    if (!VirtualProtect(check, sizeof(kExpectedSpellPracticeGuiCheck),
            PAGE_EXECUTE_READWRITE, &oldProtection)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }

    // Keep the original near JE at +7: false takes the normal GUI path,
    // true enters the native spell-practice GUI initialization path.
    check[0] = 0xE8;
    *reinterpret_cast<int32_t*>(check + 1) = static_cast<int32_t>(relative);
    check[5] = 0x84;
    check[6] = 0xC0;
    FlushInstructionCache(GetCurrentProcess(), check,
        sizeof(kExpectedSpellPracticeGuiCheck));
    DWORD ignored = 0;
    VirtualProtect(check, sizeof(kExpectedSpellPracticeGuiCheck),
        oldProtection, &ignored);
    return true;
}

bool PatchPlayerEntryPracticeCheck()
{
    auto* check = ResolveGameAddress<unsigned char>(
        GameAddress::PlayerEntryPracticeCheck);
    if (!check || std::memcmp(check, kExpectedPlayerEntryPracticeCheck,
            sizeof(kExpectedPlayerEntryPracticeCheck)) != 0)
        return false;

    auto* relay = static_cast<unsigned char*>(AllocateNearAddress(check, 32));
    if (!relay)
        return false;
    WriteAbsoluteJump(relay,
        reinterpret_cast<void*>(ShouldUseNativePracticePresentation));

    DWORD oldRelayProtection = 0;
    if (!VirtualProtect(relay, 32, PAGE_EXECUTE_READ, &oldRelayProtection)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, 32);

    const intptr_t relative = reinterpret_cast<intptr_t>(relay) -
        (reinterpret_cast<intptr_t>(check) + 5);
    if (relative < std::numeric_limits<int32_t>::min() ||
        relative > std::numeric_limits<int32_t>::max()) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }

    DWORD oldProtection = 0;
    if (!VirtualProtect(check, sizeof(kExpectedPlayerEntryPracticeCheck),
            PAGE_EXECUTE_READWRITE, &oldProtection)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }

    // Select the native spell-practice state=0/timer=0 entry path. The
    // original JE immediately following these seven bytes remains intact.
    check[0] = 0xE8;
    *reinterpret_cast<int32_t*>(check + 1) = static_cast<int32_t>(relative);
    check[5] = 0x84;
    check[6] = 0xC0;
    FlushInstructionCache(GetCurrentProcess(), check,
        sizeof(kExpectedPlayerEntryPracticeCheck));
    DWORD ignored = 0;
    VirtualProtect(check, sizeof(kExpectedPlayerEntryPracticeCheck),
        oldProtection, &ignored);
    return true;
}

bool PatchPlayerStateDispatchRead()
{
    auto* read = ResolveGameAddress<unsigned char>(
        GameAddress::PlayerStateDispatchRead);
    if (!read || std::memcmp(read, kExpectedPlayerStateDispatchRead,
            sizeof(kExpectedPlayerStateDispatchRead)) != 0)
        return false;

    auto* relay = static_cast<unsigned char*>(AllocateNearAddress(read, 32));
    if (!relay)
        return false;
    // The native player pointer is held in RDI at +0x68AD2. Adapt it to the
    // Win64 first-argument register before tail-jumping into the C++ helper.
    relay[0] = 0x48;
    relay[1] = 0x8B;
    relay[2] = 0xCF; // mov rcx, rdi
    WriteAbsoluteJump(relay + 3,
        reinterpret_cast<void*>(ReadPlayerStateForDispatch));

    DWORD oldRelayProtection = 0;
    if (!VirtualProtect(relay, 32, PAGE_EXECUTE_READ, &oldRelayProtection)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, 32);

    const intptr_t relative = reinterpret_cast<intptr_t>(relay) -
        (reinterpret_cast<intptr_t>(read) + 5);
    if (relative < std::numeric_limits<int32_t>::min() ||
        relative > std::numeric_limits<int32_t>::max()) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }

    DWORD oldProtection = 0;
    if (!VirtualProtect(read, sizeof(kExpectedPlayerStateDispatchRead),
            PAGE_EXECUTE_READWRITE, &oldProtection)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    read[0] = 0xE8;
    *reinterpret_cast<int32_t*>(read + 1) = static_cast<int32_t>(relative);
    read[5] = 0x90;
    read[6] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), read,
        sizeof(kExpectedPlayerStateDispatchRead));
    DWORD ignored = 0;
    VirtualProtect(read, sizeof(kExpectedPlayerStateDispatchRead),
        oldProtection, &ignored);
    return true;
}

bool PatchNativePracticeUiSuppression()
{
    auto* callSite = ResolveGameAddress<unsigned char>(
        GameAddress::PracticeStageScoreDrawCall);
    if (!callSite || std::memcmp(callSite, kExpectedPracticeStageScoreDrawCall,
            sizeof(kExpectedPracticeStageScoreDrawCall)) != 0)
        return false;

    DWORD oldProtection = 0;
    if (!VirtualProtect(callSite, sizeof(kExpectedPracticeStageScoreDrawCall),
            PAGE_EXECUTE_READWRITE, &oldProtection))
        return false;

    // This is the sole text-renderer call used by the state-0x16 loop that
    // prints "STAGE %d  %.9llu". Removing only this call preserves every
    // background, frame, title, cursor, and transition draw performed nearby.
    std::memset(callSite, 0x90, sizeof(kExpectedPracticeStageScoreDrawCall));
    FlushInstructionCache(GetCurrentProcess(), callSite,
        sizeof(kExpectedPracticeStageScoreDrawCall));
    DWORD ignored = 0;
    VirtualProtect(callSite, sizeof(kExpectedPracticeStageScoreDrawCall),
        oldProtection, &ignored);
    return true;
}

bool PatchStageSelectorCall()
{
    auto* callSite = ResolveGameAddress<unsigned char>(
        GameAddress::PracticeStageSelectorCall);
    auto* nativeSelector = ResolveGameAddress<unsigned char>(
        GameAddress::PracticeStageSelector);
    if (!callSite || !nativeSelector ||
        std::memcmp(callSite, kExpectedStageSelectorCall, sizeof(kExpectedStageSelectorCall)) != 0 ||
        std::memcmp(nativeSelector, kExpectedStageSelectorPrologue,
            sizeof(kExpectedStageSelectorPrologue)) != 0)
        return false;

    auto* relay = static_cast<unsigned char*>(AllocateNearAddress(callSite, 32));
    if (!relay) {
        g_practiceRuntime.hookStatus = PracticeHookStatusValue::AllocationFailed;
        return false;
    }
    WriteAbsoluteJump(relay, reinterpret_cast<void*>(HookedPracticeMenuUi));

    DWORD oldRelayProtection = 0;
    if (!VirtualProtect(relay, 32, PAGE_EXECUTE_READ, &oldRelayProtection)) {
        g_practiceRuntime.hookStatus = PracticeHookStatusValue::PatchFailed;
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, 32);

    const intptr_t relative = reinterpret_cast<intptr_t>(relay) -
        (reinterpret_cast<intptr_t>(callSite) + 5);
    if (relative < std::numeric_limits<int32_t>::min() ||
        relative > std::numeric_limits<int32_t>::max()) {
        g_practiceRuntime.hookStatus = PracticeHookStatusValue::PatchFailed;
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }

    DWORD oldCallProtection = 0;
    if (!VirtualProtect(callSite, sizeof(kExpectedStageSelectorCall),
            PAGE_EXECUTE_READWRITE, &oldCallProtection)) {
        g_practiceRuntime.hookStatus = PracticeHookStatusValue::PatchFailed;
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }

    g_practiceHooks.stageSelector =
        reinterpret_cast<NativeStageSelectorFn>(nativeSelector);
    callSite[0] = 0xE8;
    *reinterpret_cast<int32_t*>(callSite + 1) = static_cast<int32_t>(relative);
    FlushInstructionCache(GetCurrentProcess(), callSite, sizeof(kExpectedStageSelectorCall));
    DWORD ignored = 0;
    VirtualProtect(callSite, sizeof(kExpectedStageSelectorCall), oldCallProtection, &ignored);
    return true;
}

bool PatchPracticeConfirmTransitionCall()
{
    auto* callSite = ResolveGameAddress<unsigned char>(
        GameAddress::PracticeConfirmTransitionCall);
    auto* nativeTransition = ResolveGameAddress<unsigned char>(
        GameAddress::PracticeMenuTransition);
    if (!callSite || !nativeTransition ||
        std::memcmp(callSite, kExpectedPracticeConfirmTransitionCall,
            sizeof(kExpectedPracticeConfirmTransitionCall)) != 0)
        return false;

    auto* relay = static_cast<unsigned char*>(AllocateNearAddress(callSite, 32));
    if (!relay)
        return false;
    WriteAbsoluteJump(relay,
        reinterpret_cast<void*>(HookedPracticeConfirmTransition));

    DWORD oldRelayProtection = 0;
    if (!VirtualProtect(relay, 32, PAGE_EXECUTE_READ, &oldRelayProtection)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, 32);

    const intptr_t relative = reinterpret_cast<intptr_t>(relay) -
        (reinterpret_cast<intptr_t>(callSite) + 5);
    if (relative < std::numeric_limits<int32_t>::min() ||
        relative > std::numeric_limits<int32_t>::max()) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }

    DWORD oldCallProtection = 0;
    if (!VirtualProtect(callSite, sizeof(kExpectedPracticeConfirmTransitionCall),
            PAGE_EXECUTE_READWRITE, &oldCallProtection)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    g_practiceHooks.menuTransition =
        reinterpret_cast<NativePracticeMenuTransitionFn>(nativeTransition);
    callSite[0] = 0xE8;
    *reinterpret_cast<int32_t*>(callSite + 1) = static_cast<int32_t>(relative);
    FlushInstructionCache(GetCurrentProcess(), callSite,
        sizeof(kExpectedPracticeConfirmTransitionCall));
    DWORD ignored = 0;
    VirtualProtect(callSite, sizeof(kExpectedPracticeConfirmTransitionCall),
        oldCallProtection, &ignored);
    return true;
}

} // namespace

bool InstallPracticeMenuHook()
{
    if (g_practiceRuntime.hookStatus == PracticeHookStatusValue::Installed)
        return true;
    if (!IsGameAddressRangeValid(GameAddress::PracticeStageSelectorCall,
            sizeof(kExpectedStageSelectorCall)) ||
        !IsGameAddressRangeValid(GameAddress::PracticeStageSelector,
            sizeof(kExpectedStageSelectorPrologue)) ||
        !IsGameAddressRangeValid(GameAddress::PracticeConfirmTransitionCall,
            sizeof(kExpectedPracticeConfirmTransitionCall)) ||
        !IsGameAddressRangeValid(GameAddress::StageBgmLoadCall,
            sizeof(kExpectedStageBgmLoadCall)) ||
        !IsGameAddressRangeValid(GameAddress::BgmLoad, 1) ||
        !IsGameAddressRangeValid(GameAddress::StageBackgroundFastForward,
            sizeof(kExpectedStageBackgroundFastForwardPrologue)) ||
        !IsGameAddressRangeValid(GameAddress::StageBackgroundPracticeCheck,
            sizeof(kExpectedStageBackgroundPracticeCheck)) ||
        !IsGameAddressRangeValid(GameAddress::StageBackgroundFastForwardCall,
            sizeof(kExpectedStageBackgroundFastForwardCall)) ||
        !IsGameAddressRangeValid(
            GameAddress::StageBackgroundSpecialPracticeCheck,
            sizeof(kExpectedStageBackgroundSpecialPracticeCheck)) ||
        !IsGameAddressRangeValid(GameAddress::SpellPracticeGuiCheck,
            sizeof(kExpectedSpellPracticeGuiCheck)) ||
        !IsGameAddressRangeValid(GameAddress::StageTitlePracticeCheck,
            sizeof(kExpectedStageTitlePracticeCheck)) ||
        !IsGameAddressRangeValid(GameAddress::PlayerEntryPracticeCheck,
            sizeof(kExpectedPlayerEntryPracticeCheck)) ||
        !IsGameAddressRangeValid(GameAddress::PlayerStateDispatchRead,
            sizeof(kExpectedPlayerStateDispatchRead)) ||
        !IsGameAddressRangeValid(GameAddress::PracticeStageScoreDrawCall,
            sizeof(kExpectedPracticeStageScoreDrawCall))) {
        g_practiceRuntime.hookStatus =
            PracticeHookStatusValue::UnsupportedExecutable;
        return false;
    }
    const auto* backgroundFastForward = ResolveGameAddress<unsigned char>(
        GameAddress::StageBackgroundFastForward);
    if (!backgroundFastForward ||
        std::memcmp(backgroundFastForward,
            kExpectedStageBackgroundFastForwardPrologue,
            sizeof(kExpectedStageBackgroundFastForwardPrologue)) != 0) {
        g_practiceRuntime.hookStatus =
            PracticeHookStatusValue::UnsupportedExecutable;
        return false;
    }
    if (!PatchStageSelectorCall()) {
        const PracticeHookStatusValue failure = g_practiceRuntime.hookStatus;
        if (failure != PracticeHookStatusValue::AllocationFailed &&
            failure != PracticeHookStatusValue::PatchFailed)
            g_practiceRuntime.hookStatus =
                PracticeHookStatusValue::UnsupportedExecutable;
        return false;
    }
    const bool confirmBypassInstalled = PatchPracticeConfirmTransitionCall();
    const bool nativeUiSuppressionInstalled = PatchNativePracticeUiSuppression();
    const bool playerInitializeInstalled = PatchPlayerInitialize();
    const bool stageBgmLoadInstalled = PatchStageBgmLoadCall();
    const bool stageBackgroundPathInstalled =
        PatchStageBackgroundPracticePath();
    const bool stageBackgroundSpecialInstalled =
        PatchStageBackgroundSpecialPracticeCheck();
    const bool spellPracticeGuiInstalled = PatchSpellPracticeGuiCheck();
    const bool stageTitleCheckInstalled = PatchStageTitlePracticeCheck();
    const bool playerEntryCheckInstalled = PatchPlayerEntryPracticeCheck();
    const bool playerStateDispatchInstalled = PatchPlayerStateDispatchRead();
    g_practiceRuntime.hookStatus =
        confirmBypassInstalled && nativeUiSuppressionInstalled &&
            playerInitializeInstalled && stageBgmLoadInstalled &&
            stageBackgroundPathInstalled && stageBackgroundSpecialInstalled &&
            spellPracticeGuiInstalled && stageTitleCheckInstalled &&
            playerEntryCheckInstalled &&
            playerStateDispatchInstalled
        ? PracticeHookStatusValue::Installed
        : PracticeHookStatusValue::StageUiOnly;
    return true;
}

const char* PracticeMenuHookStatus()
{
    switch (g_practiceRuntime.hookStatus) {
    case PracticeHookStatusValue::Installed: return S(StatusActive);
    case PracticeHookStatusValue::StageUiOnly:
        return S(StatusPracticePartial);
    case PracticeHookStatusValue::UnsupportedExecutable:
        return S(StatusUnsupportedExecutable);
    case PracticeHookStatusValue::AllocationFailed:
        return S(StatusX64AllocationFailed);
    case PracticeHookStatusValue::PatchFailed: return S(StatusPatchFailed);
    default: return S(StatusNotInstalled);
    }
}

bool IsPracticeMenuReplacementActive()
{
    const ULONGLONG lastUpdate = g_practiceRuntime.lastMenuUpdateTick;
    const PracticeHookStatusValue status = g_practiceRuntime.hookStatus;
    return (status == PracticeHookStatusValue::Installed ||
            status == PracticeHookStatusValue::StageUiOnly) &&
        g_practiceRuntime.menuObject != nullptr && lastUpdate != 0 &&
        GetTickCount64() - lastUpdate <= kMenuHeartbeatTimeoutMs;
}

bool IsEnhancedPracticeRunActive()
{
    return IsActiveEnhancedPracticeRun();
}

void EndEnhancedPracticeRun()
{
    PrepareEverlastingBgmForInitialization(false);
    g_practiceRuntime.enhancedSessionActive = false;
    g_practiceRuntime.enhancedInitialLoadPending = false;
    g_practiceRuntime.enhancedRestartPending = false;
    g_practiceRuntime.clearInitialPlayerStatePending = false;
    if (auto* practiceFlag =
            ResolveGameAddress<uint8_t>(GameAddress::PracticeModeFlag))
        *practiceFlag = 0;
}

bool MarkEnhancedPracticeRestartPending()
{
    if (!g_practiceRuntime.enhancedSessionActive)
        return false;
    const bool compatible =
        g_practiceRuntime.runningStage == g_practiceParam.stage &&
        g_practiceRuntime.runningBossBgm == IsMainBossJumpSelected();
    g_practiceRuntime.restartBgmCompatible = compatible;
    g_practiceRuntime.enhancedRestartPending = true;
    if (auto* stage = ResolveGameAddress<int>(GameAddress::CurrentStage))
        *stage = g_practiceParam.stage;
    if (auto* difficulty =
            ResolveGameAddress<int>(GameAddress::CurrentDifficulty))
        *difficulty = g_practiceParam.stage == 6
            ? 4 : std::clamp(g_practiceParam.difficulty, 0, 3);
    return compatible;
}

bool IsRaging495PracticeActive()
{
    return IsActiveEnhancedPracticeRun() && g_practiceParam.raging495 != 0 &&
        g_practiceParam.stage == 6 &&
        g_practiceParam.bossJump == TH06NC_ST7_BOSS18;
}

bool ExportPracticeReplayConfig(PracticeReplayConfig& config)
{
    if (!g_practiceRuntime.enhancedSessionActive)
        return false;
    config = g_practiceParam;
    return true;
}

bool ImportPracticeReplayConfig(const PracticeReplayConfig& config)
{
    if (config.stage < 0 || config.stage > 6 ||
        config.difficulty < 0 || config.difficulty > 4 ||
        config.warpTarget < 0 || config.warpTarget > 6 ||
        config.chapter < 1 || config.timelineFrame < 0 ||
        config.timelineFrame > 999999 ||
        config.dialogue < 0 || config.dialogue > 1 ||
        config.lives < 0 || config.lives > 8 ||
        config.bombs < 0 || config.bombs > 8 ||
        config.score < 0 || config.score > 9999999990LL ||
        config.power < 0 || config.power > 128 ||
        config.graze < 0 || config.graze > 99999 ||
        config.pointItems < 0 || config.pointItems > 9999 ||
        config.fakeShot < 0 || config.fakeShot > 4 ||
        config.raging495 < 0 || config.raging495 > 1 ||
        config.stage5Boss6Mode < 0 || config.stage5Boss6Mode > 2 ||
        (config.bookFixedMask & ~0x3fu) != 0)
        return false;
    for (int i = 0; i < 6; ++i) {
        if (config.bookX[i] < -192 || config.bookX[i] > 192 ||
            config.bookY[i] < -50 || config.bookY[i] > 448)
            return false;
    }

    const auto chapterTimes = StageChapterTimes().find(config.stage + 1);
    if (config.warpTarget == 1 &&
        (chapterTimes == StageChapterTimes().end() ||
            config.chapter > static_cast<int>(chapterTimes->second.first.size() + chapterTimes->second.second.size())))
        return false;

    JumpEnum jump = static_cast<JumpEnum>(config.bossJump);
    if (config.warpTarget >= 2 && config.warpTarget <= 5) {
        if (jump == TH06NC_JUMP_NONE)
            return false;
        const auto& jumps = BossJumps();
        const int difficulty = config.stage == 6 ? 4 : config.difficulty;
        const bool known = std::any_of(jumps.begin(), jumps.end(),
            [jump, &config, difficulty](const BossJump& candidate) {
                return candidate.jumpname == jump &&
                    candidate.stage == config.stage + 1 &&
                    (candidate.diff & (1 << difficulty)) != 0;
            });
        if (!known)
            return false;
    } else
        jump = TH06NC_JUMP_NONE;

    g_practiceParam = config;
    g_practiceParam.difficulty = config.stage == 6 ? 4 : config.difficulty;
    g_practiceParam.practiceMode = 1;
    g_practiceParam.bossJump = static_cast<int32_t>(jump);

    g_practiceRuntime.enhancedSessionActive = true;
    g_practiceRuntime.enhancedInitialLoadPending = true;
    g_practiceRuntime.clearInitialPlayerStatePending = false;
    ClearQueuedPracticeJump();
    QueueConfiguredPracticeJump();
    return true;
}

PausedPracticeUiResult DrawPracticeConfigurationEditorUi(bool pausedEditor,
    uint32_t suppliedNavigation, bool embedded, bool navigationActive,
    bool resetNavigation)
{
    PausedPracticeUiResult interaction{};
    int mode = g_practiceParam.practiceMode;
    int warpTarget = g_practiceParam.warpTarget;
    JumpEnum selectedBossJump =
        static_cast<JumpEnum>(g_practiceParam.bossJump);
    int chapter = g_practiceParam.chapter;
    int timelineFrame = g_practiceParam.timelineFrame;
    bool dialogue = g_practiceParam.dialogue != 0;
    int lives = g_practiceParam.lives;
    int bombs = g_practiceParam.bombs;
    int power = g_practiceParam.power;
    int64_t score = g_practiceParam.score;
    int graze = g_practiceParam.graze;
    int pointItems = g_practiceParam.pointItems;
    int fakeShot = g_practiceParam.fakeShot;
    int spellNameShot = fakeShot;
    if (spellNameShot == 0) {
        const auto* character =
            ResolveGameAddress<uint8_t>(GameAddress::CurrentCharacter);
        const auto* shotType =
            ResolveGameAddress<uint8_t>(GameAddress::CurrentShotType);
        if (character && shotType && *character <= 1 && *shotType <= 1)
            spellNameShot = static_cast<int>(*character) * 2 +
                static_cast<int>(*shotType) + 1;
    }
    bool raging495 = g_practiceParam.raging495 != 0;
    int stage5Boss6Mode = g_practiceParam.stage5Boss6Mode;
    unsigned bookFixedMask = g_practiceParam.bookFixedMask;
    int bookX[6]{};
    int bookY[6]{};
    for (int book = 0; book < 6; ++book) {
        bookX[book] = g_practiceParam.bookX[book];
        bookY[book] = g_practiceParam.bookY[book];
    }
    const char* stageNames[kMaximumKnownStageCount] = {
        S(Stage1), S(Stage2), S(Stage3), S(Stage4),
        S(Stage5), S(Stage6), S(ExtraStage),
    };
    const char* modeNames[] = {S(Original), S(Enhanced)};
    const char* warpTargets[] = {
        S(None), S(StagePortion), S(Midboss), S(Boss),
        S(Nonspell), S(Spell), S(Frame),
    };
    const char* fakeShotNames[] = {
        S(None), S(ReimuA), S(ReimuB), S(MarisaA), S(MarisaB),
    };
    const char* stage5Boss6ModeNames[] = {
        S(DefaultPattern), S(FastPattern), S(SlowPattern),
    };

    if (!embedded) {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.65f,io.DisplaySize.y * 0.5f),
            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(
            ImVec2(io.DisplaySize.x * 0.4f, io.DisplaySize.y * 0.90f),
            ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg,
            ImVec4(0.035f, 0.035f, 0.045f, 1.0f));
    }

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoNavInputs;
    const std::string practiceWindowTitle =
        std::string(S(PracticeSetup)) +
        (pausedEditor ? "###paused-practice-setup" : "###practice-setup");
    if (!embedded)
        ImGui::Begin(practiceWindowTitle.c_str(), nullptr, flags);

    static int menuNavigationRow = 1;
    static int menuNavigationRowCount = 2;
    static int pauseNavigationRow = 0;
    static int pauseNavigationRowCount = 2;
    int& navigationRow = pausedEditor
        ? pauseNavigationRow : menuNavigationRow;
    int& navigationRowCount = pausedEditor
        ? pauseNavigationRowCount : menuNavigationRowCount;
    if (resetNavigation)
        navigationRow = 0;
    const uint32_t navigation = pausedEditor
        ? suppliedNavigation
        : Exchange(g_practiceRuntime.navigationActions, uint32_t{0});
    if ((navigation & kNavigateUp) != 0) {
        if (pausedEditor && navigationActive && navigationRow == 0)
            interaction.leaveUp = true;
        else
            navigationRow = WrapSelection(navigationRow, -1,
                navigationRowCount);
    }
    if ((navigation & kNavigateDown) != 0) {
        if (pausedEditor && navigationActive &&
            navigationRow == navigationRowCount - 1)
            interaction.leaveDown = true;
        else
            navigationRow = WrapSelection(navigationRow, 1,
                navigationRowCount);
    }
    const int horizontal = ((navigation & kNavigateRight) != 0 ? 1 : 0) -
        ((navigation & kNavigateLeft) != 0 ? 1 : 0);
    const bool movedVertically =
        (navigation & (kNavigateUp | kNavigateDown)) != 0;
    int row = 0;
    auto beginRow = [&](int rowIndex) {
        const bool selected = navigationActive &&
            navigationRow == rowIndex;
        ImGui::AlignTextToFramePadding();
        if (selected)
            ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.25f, 1.0f), " >");
        else
            ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 0.0f), " >");
        ImGui::SameLine();
        return selected;
    };
    auto finishRow = [&](int rowIndex) {
       //if (ImGui::IsItemHovered()) 
       if (ImGui::IsItemClicked())
       {
                interaction.hovered = true;
            navigationRow = rowIndex;
       }
        if (movedVertically && navigationRow == rowIndex)
            ImGui::SetScrollHereY(0.5f);
    };

    if (!pausedEditor)
    {
        const int modeRow = row++;
        const bool modeFocused = beginRow(modeRow);
        if (modeFocused && horizontal != 0)
            mode = WrapSelection(mode, horizontal, IM_ARRAYSIZE(modeNames));
        ImGui::Combo(S(Mode), &mode, modeNames, IM_ARRAYSIZE(modeNames));
        finishRow(modeRow);
    }

    const int stageCount = kMaximumKnownStageCount;
    int selectedStage = ClampStage(g_practiceParam.stage, stageCount);
    const int oldStage = selectedStage;
    const int stageRow = row++;
    const bool stageFocused = beginRow(stageRow);
    if (stageFocused && horizontal != 0)
        selectedStage = WrapSelection(selectedStage, horizontal, stageCount);
    const bool stageClicked = ImGui::Combo(
        S(Stage), &selectedStage, stageNames, stageCount);
    finishRow(stageRow);
    if (stageClicked || selectedStage != oldStage) {
        g_practiceParam.stage = selectedStage;
        chapter = 1;
        selectedBossJump = TH06NC_JUMP_NONE;
    }

    if (mode == 1) {
        const int warpRow = row++;
        const bool warpFocused = beginRow(warpRow);
        const int oldWarpTarget = warpTarget;
        if (warpFocused && horizontal != 0)
            warpTarget = WrapSelection(warpTarget, horizontal,
                IM_ARRAYSIZE(warpTargets));
        const bool warpClicked = ImGui::Combo(S(WarpTo), &warpTarget,
            warpTargets, IM_ARRAYSIZE(warpTargets));
        finishRow(warpRow);
        if (warpClicked || warpTarget != oldWarpTarget) {
            chapter = 1;
            selectedBossJump = TH06NC_JUMP_NONE;
        }

        switch (warpTarget) {
        case 1: {
            const auto found = StageChapterTimes().find(selectedStage + 1);
            const int chapterMaximum = found == StageChapterTimes().end()
                ? 1 : static_cast<int>(found->second.first.size() + found->second.second.size());
            chapter = std::clamp(chapter, 1, std::max(chapterMaximum, 1));
            const int chapterRow = row++;
            const bool chapterFocused = beginRow(chapterRow);
            if (chapterFocused && horizontal != 0)
                chapter = WrapSelection(chapter - 1, horizontal,
                    std::max(chapterMaximum, 1)) + 1;
            static char chapter_desc[100] = "";
            if (chapter <= found->second.first.size()){
                sprintf_s(chapter_desc, "%s #%d ##chapter", S(Chapter_1), chapter);
            } else {
                sprintf_s(chapter_desc, "%s #%d ##chapter", S(Chapter_2),
                    static_cast<int>(chapter - found->second.first.size()));
            }
            ImGui::SliderInt(S(Chapter), &chapter, 1, std::max(chapterMaximum, 1), chapter_desc);
            finishRow(chapterRow);
            if (found != StageChapterTimes().end() && ((!found->second.first.empty() || !found->second.second.empty())))
                ImGui::TextDisabled(S(TimelineTime), GetChapterTime(selectedStage + 1,chapter));
            break;
        }
        case 2:
        case 3:
        case 4:
        case 5: {
            const int difficulty = selectedStage == 6
                ? 4 : std::clamp(g_practiceParam.difficulty, 0, 3);
            const int difficultyMask = 1 << difficulty;
            std::vector<const BossJump*> choices;
            for (const BossJump& jump : BossJumps()) {
                if (jump.stage == selectedStage + 1 &&
                    BossTypeMatches(warpTarget, jump.type) &&
                    (jump.diff & difficultyMask) != 0)
                    choices.push_back(&jump);
            }
            if (!choices.empty() && std::none_of(choices.begin(), choices.end(),
                    [&](const BossJump* jump) {
                        return jump->jumpname == selectedBossJump;
                    }))
                selectedBossJump = choices.front()->jumpname;

            const int jumpRow = row++;
            const bool jumpFocused = beginRow(jumpRow);
            if (jumpFocused && horizontal != 0 && !choices.empty()) {
                auto current = std::find_if(choices.begin(), choices.end(),
                    [&](const BossJump* jump) {
                        return jump->jumpname == selectedBossJump;
                    });
                const int currentIndex = current == choices.end()
                    ? 0 : static_cast<int>(current - choices.begin());
                selectedBossJump = choices[WrapSelection(currentIndex, horizontal,
                    static_cast<int>(choices.size()))]->jumpname;
            }

            const char* preview = S(NoJump);
            for (const BossJump* jump : choices) {
                if (jump->jumpname == selectedBossJump) {
                    preview = Locale::Instance().GetJump(
                        static_cast<int>(jump->jumpname), difficulty,
                        spellNameShot);
                    break;
                }
            }
            if (ImGui::BeginCombo(S(Jump), preview)) {
                for (const BossJump* jump : choices) {
                    const bool selected = jump->jumpname == selectedBossJump;
                    const char* localizedName =
                        Locale::Instance().GetJump(
                            static_cast<int>(jump->jumpname), difficulty,
                            spellNameShot);
                    if (ImGui::Selectable(localizedName, selected))
                        selectedBossJump = jump->jumpname;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            finishRow(jumpRow);

            if (selectedStage == 3 && warpTarget >= 2 && warpTarget <= 5) {
                const int fakeShotRow = row++;
                const bool fakeShotFocused = beginRow(fakeShotRow);
                if (fakeShotFocused && horizontal != 0)
                    fakeShot = WrapSelection(fakeShot, horizontal,
                        IM_ARRAYSIZE(fakeShotNames));
                ImGui::SetNextItemWidth(280.0f);
                ImGui::Combo(S(FakeShot), &fakeShot, fakeShotNames,
                    IM_ARRAYSIZE(fakeShotNames));
                finishRow(fakeShotRow);
            }

            if (selectedBossJump == TH06NC_ST5_BOSS6) {
                const int patternModeRow = row++;
                const bool patternModeFocused = beginRow(patternModeRow);
                if (patternModeFocused && horizontal != 0)
                    stage5Boss6Mode = WrapSelection(stage5Boss6Mode,
                        horizontal, IM_ARRAYSIZE(stage5Boss6ModeNames));
                ImGui::Combo(S(Mode), &stage5Boss6Mode,
                    stage5Boss6ModeNames,
                    IM_ARRAYSIZE(stage5Boss6ModeNames));
                finishRow(patternModeRow);
            }

            if (selectedStage == 6 &&
                selectedBossJump == TH06NC_ST7_BOSS18) {
                const int rageRow = row++;
                const bool rageFocused = beginRow(rageRow);
                if (rageFocused && horizontal != 0)
                    raging495 = !raging495;
                ImGui::Checkbox(S(Raging495), &raging495);
                finishRow(rageRow);
            }

            const int dialogueRow = row++;
            const bool dialogueFocused = beginRow(dialogueRow);
            if (dialogueFocused && horizontal != 0)
                dialogue = !dialogue;
            ImGui::Checkbox(S(Dialogue), &dialogue);
            finishRow(dialogueRow);
            break;
        }
        case 6: {
            const int frameRow = row++;
            const bool frameFocused = beginRow(frameRow);
            if (frameFocused && horizontal != 0)
                timelineFrame = std::max(timelineFrame + horizontal, 0);
            ImGui::DragInt(S(Frame), &timelineFrame, 1.0f, 0, 999999);
            timelineFrame = std::clamp(timelineFrame, 0, 999999);
            finishRow(frameRow);
            break;
        }
        default:
            break;
        }

        const int livesRow = row++;
        const bool livesFocused = beginRow(livesRow);
        if (livesFocused && horizontal != 0)
            lives = std::clamp(lives + horizontal, 0, 8);
        ImGui::SliderInt(S(Lives), &lives, 0, 8);
        finishRow(livesRow);

        const int bombsRow = row++;
        const bool bombsFocused = beginRow(bombsRow);
        if (bombsFocused && horizontal != 0)
            bombs = std::clamp(bombs + horizontal, 0, 8);
        ImGui::SliderInt(S(Bombs), &bombs, 0, 8);
        finishRow(bombsRow);

        constexpr int64_t minimumScore = 0;
        constexpr int64_t maximumScore = 9999999990LL;
        const int scoreRow = row++;
        const bool scoreFocused = beginRow(scoreRow);
        if (scoreFocused && horizontal != 0)
            score = std::clamp(score + horizontal * 1000LL,
                minimumScore, maximumScore);
        ImGui::DragScalar(S(Score), ImGuiDataType_S64, &score, 1000.0f,
            &minimumScore, &maximumScore, "%lld");
        finishRow(scoreRow);

        const int powerRow = row++;
        const bool powerFocused = beginRow(powerRow);
        if (powerFocused && horizontal != 0)
            power = std::clamp(power + horizontal, 0, 128);
        ImGui::SliderInt(S(Power), &power, 0, 128);
        finishRow(powerRow);

        const int grazeRow = row++;
        const bool grazeFocused = beginRow(grazeRow);
        if (grazeFocused && horizontal != 0)
            graze = std::clamp(graze + horizontal, 0, 99999);
        ImGui::DragInt(S(Graze), &graze, 1.0f, 0, 99999);
        finishRow(grazeRow);

        const int pointRow = row++;
        const bool pointFocused = beginRow(pointRow);
        if (pointFocused && horizontal != 0)
            pointItems = std::clamp(pointItems + horizontal, 0, 9999);
        ImGui::DragInt(S(Point), &pointItems, 1.0f, 0, 9999);
        finishRow(pointRow);

        if (selectedStage == 3 && 
            ((warpTarget == 1 && chapter == 4) || (selectedBossJump == TH06NC_ST4_BOOKS))
            && ImGui::TreeNode(S(Stage4Books))) {
                bool bookFixed[6]{};
                for (int book = 0; book < 6; ++book) {
                    bookFixed[book] = (bookFixedMask & (1u << book)) != 0;
                    ImGui::PushID(book);
                    if (ImGui::Checkbox(S(Fixed), &bookFixed[book])) {
                        if (bookFixed[book])
                            bookFixedMask |= 1u << book;
                        else
                            bookFixedMask &= ~(1u << book);
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(115.0f);
                    ImGui::DragInt("X", &bookX[book], 1.0f, -192, 192);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(115.0f);
                    ImGui::DragInt("Y", &bookY[book], 1.0f, -50, 448);
                    ImGui::PopID();
                }
                if (ImGui::Button(S(MirrorLastThree))) {
                    bookX[3] = -bookX[2];
                    bookX[4] = -bookX[1];
                    bookX[5] = -bookX[0];
                }
                ImGui::SameLine();
                if (ImGui::Button(S(MirrorAll))) {
                    for (int& x : bookX)
                        x = -x;
                }
                ImGui::SameLine();
                if (ImGui::Button(S(RotateBooks))) {
                    const int last = bookX[5];
                    for (int book = 5; book > 0; --book)
                        bookX[book] = bookX[book - 1];
                    bookX[0] = last;
                }
                if (ImGui::Button(S(RandomizeBookX))) {
                    static std::mt19937 randomEngine(GetTickCount());
                    static std::uniform_int_distribution<int> randomX(-192, 192);
                    for (int& x : bookX)
                        x = randomX(randomEngine);
                }
                ImGui::SameLine();
                if (ImGui::Button(S(ResetBookY))) {
                    constexpr int defaults[6] = {32, 128, 144, 64, 80, 96};
                    std::copy(std::begin(defaults), std::end(defaults), bookY);
                }
                if (ImGui::Button(S(CopyBookConfig))) {
                    char settings[192]{};
                    std::snprintf(settings, sizeof(settings),
                        "(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d)",
                        bookX[0], bookY[0], bookX[1], bookY[1],
                        bookX[2], bookY[2], bookX[3], bookY[3],
                        bookX[4], bookY[4], bookX[5], bookY[5]);
                    ImGui::SetClipboardText(settings);
                }
                ImGui::SameLine();
                if (ImGui::Button(S(PasteBookConfig))) {
                    const char* settings = ImGui::GetClipboardText();
                    int pastedX[6]{};
                    int pastedY[6]{};
                    if (settings && sscanf_s(settings,
                            " (%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d),(%d,%d)",
                            &pastedX[0], &pastedY[0], &pastedX[1], &pastedY[1],
                            &pastedX[2], &pastedY[2], &pastedX[3], &pastedY[3],
                            &pastedX[4], &pastedY[4], &pastedX[5], &pastedY[5]) == 12) {
                        bool valid = true;
                        for (int book = 0; book < 6; ++book) {
                            valid = valid && pastedX[book] >= -192 &&
                                pastedX[book] <= 192 && pastedY[book] >= -50 &&
                                pastedY[book] <= 448;
                        }
                        if (valid) {
                            std::copy(std::begin(pastedX), std::end(pastedX), bookX);
                            std::copy(std::begin(pastedY), std::end(pastedY), bookY);
                        }
                    }
                }
                ImGui::TreePop();
        }
    }

    navigationRowCount = std::max(row, 1);
    navigationRow = std::clamp(navigationRow, 0, navigationRowCount - 1);

    g_practiceParam.practiceMode = mode;
    g_practiceParam.warpTarget = warpTarget;
    g_practiceParam.chapter = chapter;
    g_practiceParam.timelineFrame = timelineFrame;
    g_practiceParam.bossJump = static_cast<int32_t>(selectedBossJump);
    g_practiceParam.dialogue = dialogue ? 1 : 0;
    g_practiceParam.lives = std::clamp(lives, 0, 8);
    g_practiceParam.bombs = std::clamp(bombs, 0, 8);
    g_practiceParam.score =
        std::clamp(score, int64_t{0}, int64_t{9999999990LL});
    g_practiceParam.power = std::clamp(power, 0, 128);
    g_practiceParam.graze = std::clamp(graze, 0, 99999);
    g_practiceParam.pointItems = std::clamp(pointItems, 0, 9999);
    g_practiceParam.fakeShot = fakeShot;
    g_practiceParam.raging495 = raging495 ? 1 : 0;
    g_practiceParam.stage5Boss6Mode = stage5Boss6Mode;
    g_practiceParam.bookFixedMask = bookFixedMask;
    for (int book = 0; book < 6; ++book) {
        g_practiceParam.bookX[book] = bookX[book];
        g_practiceParam.bookY[book] = bookY[book];
    }

    if (!pausedEditor) {
        ImGui::Separator();
        float button_width = 150.0f;
        float avail = ImGui::GetContentRegionAvail().x;
        float cursor_x = ImGui::GetCursorPosX();
        ImGui::SetCursorPosX(cursor_x + (avail - button_width) * 0.5f);
        if (ImGui::Button(S(Start), ImVec2(button_width, 0.0f)))
            g_practiceRuntime.startRequested = true;
    }
    if (!embedded) {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }
    return interaction;
}

void DrawPracticeMenuReplacementUi()
{
    if (IsPracticeMenuReplacementActive())
        DrawPracticeConfigurationEditorUi(false, 0, false, true, false);
}

PausedPracticeUiResult DrawPausedPracticeConfigurationUi(int vertical,
    int horizontal, bool focused, bool resetNavigation)
{
    uint32_t navigation = 0;
    if (vertical < 0) navigation |= kNavigateUp;
    if (vertical > 0) navigation |= kNavigateDown;
    if (horizontal < 0) navigation |= kNavigateLeft;
    if (horizontal > 0) navigation |= kNavigateRight;
    return DrawPracticeConfigurationEditorUi(true, navigation, true,
        focused, resetNavigation);
}
