#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

// Module-relative addresses for the supported 64-bit Steam build of th06nc.
// Values here are RVAs, never preferred-base virtual addresses.
enum class GameAddress : uintptr_t {
    BulletManagerUpdate = 0x10870,       // Captures the active bullet-manager pointer.
    CollisionTest = 0x6A980,             // Player-versus-bullet/laser collision test.
    LaserCollisionTest = 0x6ABA0,        // Rotated laser-versus-player collision test.
    StageDrawHigh = 0x78290,             // Upper stage-background draw layer.
    StageDrawLow = 0x78390,              // Lower stage-background draw layer.
    ActionInputUpdate = 0x12BE0,         // Builds the per-frame logical action bitfield.
    KeyboardUpdate = 0xAE180,            // Central 256-key polling/update routine.
    EnemyTimelineUpdate = 0x36260,       // Advances/dispatches the active ECL timeline.
    PlayerInitialize = 0x3A9C0,         // Final common player/resource initialization.
    PlayerUpdate = 0x68820,              // Per-frame player update.
    AutoBombInputCheck = 0x689ED,        // Native Bomb input-edge check; auto-Bomb diverts here.
    AutoBombInputContinue = 0x689F4,     // Continuation after the overwritten input load.
    DeathBombBranch = 0x68A11,           // Native deathbomb path after the X-key edge checks.
    StageBgmLoadCall = 0x3B61B,          // Loads the selected stage/Boss BGM during initialization.
    BgmLoad = 0x7BC80,                   // Opens the named BGM and starts streaming playback.
    PauseBgmStopCall = 0x3A313,          // Stops the BGM channel when ESC opens Pause.
    BgmGetPosition = 0xC4A10,            // Returns the current audio position for a handle.
    BgmSeek = 0xC9930,                   // Seeks an audio handle to a saved position.
    StageBackgroundFastForward = 0x77550,// Native spell-practice background/title pre-advance.
    StageBackgroundPracticeCheck = 0x3B4AE, // Gates the native background pre-advance call.
    StageBackgroundFastForwardCall = 0x3B4B7, // Native call site after ECL timeline setup.
    StageBackgroundSpecialPracticeCheck = 0x3B4DF, // Gates the native Stage 6/Extra eff06/eff07 setup.
    SpellPracticeGuiCheck = 0x3E757,    // Enters the native spell-practice GUI initialization path.
    StageTitlePracticeCheck = 0x3EA61,   // Native spell-practice check that skips the stage title.
    PlayerEntryPracticeCheck = 0x6841C,  // Selects state 0/timer 0 instead of the 120-frame entry clear.
    PlayerStateDispatchRead = 0x68AD2,   // Per-frame player-state read used by the respawn/entry effect.
    PlayerDieStoreBullet = 0x6AAF1,      // Bullet collision writes player state DIE (2).
    PlayerDieStoreLaser = 0x6AD3E,       // Laser collision writes player state DIE (2).
    BombDecrement = 0x68A70,             // dec cl before CurrentBombs is stored.
    PowerLossFirstStore = 0x68BAF,       // First death-path CurrentPower store.
    PowerLossClampStore = 0x68BBE,       // Optional minimum-power store after death.
    PowerLossSecondStore = 0x68D16,      // Alternate death-path CurrentPower store.
    LivesGameOverBranch = 0x68E6F,       // jg skips the game-over flag when lives remain.
    LivesDecrement = 0x68E86,            // dec al before CurrentLives is stored.

    PracticeStageSelectorCall = 0x4BFB2, // CALL used by the native Practice stage menu.
    PracticeStageSelector = 0x4D1F0,     // Native up/down stage selector routine.
    PracticeConfirmTransitionCall = 0x4C183, // Enters the native two-step confirmation screen.
    PracticeMenuTransition = 0x4DA80,    // Native menu transition used outside enhanced Practice.
    PracticeStageScoreDrawCall = 0x54E60, // Draws the upper-right STAGE/high-score rows.
    MenuStartFade = 0x76050,             // Starts the 30-frame transition into gameplay.
    MenuFlushSounds = 0x76A30,           // Flushes queued menu sounds before gameplay.
    MenuQueueSound = 0x76AF0,            // Queues a native menu sound by id.

    CurrentCharacter = 0x4F1E80,         // 0 = Reimu, 1 = Marisa.
    CurrentShotType = 0x4F1E81,          // 0 = A, 1 = B.
    CurrentStage = 0x4F1E84,             // Active zero-based stage/group index.
    CurrentPower = 0x4F1E88,             // Current power, stored as a 16-bit value.
    PracticeModeFlag = 0x4F27B4,         // Nonzero while a native Practice run is active.
    NativeSpellPracticeFlag = 0x4F27B5,  // Nonzero only for the game's own spell practice.
    CurrentSpellId = 0x4F27B8,           // Native spell-practice target ID.
    CurrentPointItems = 0x4F27BC,        // Current point items, stored as a 16-bit value.
    CurrentDifficulty = 0x4F27C0,        // Active difficulty index; Extra is 4.
    ReplayPlaybackFlag = 0x4F27C4,       // Nonzero while replay input is being played back.
    CurrentScore = 0x4F2798,             // Current score, stored as a 64-bit value.
    CurrentGraze = 0x4FF0CC,             // Current graze, stored as a 32-bit value.
    CurrentLives = 0x4FF0F0,             // Current lives byte.
    CurrentBombs = 0x4FF0F1,             // Current bombs byte.
    PlayerPosition = 0x506AD0,           // Current player stage position, two floats.
    PlayerHitboxRadius = 0x506AEC,       // Current player collision radius, float.
    CurrentPlayerState = 0x506C38,       // Player state byte: normal 0, entry 1, DIE 2, respawn 3.
    KeyboardState = 0x54343C,            // Raw 256-byte keyboard state.
    KeyboardMetadata = 0x545540,         // Cached valid/modifier keyboard fields.
    MenuInputCurrent = 0xA6EC60,         // Current menu action bitfield.
    MenuInputPrevious = 0xA6EC64,        // Previous menu action bitfield.
    LoadedEclFile = 0xA6EB78,            // Pointer to the writable loaded ecldataN buffer.
    MenuSoundState = 0x509660,           // Native menu sound queue/state object.
    MenuSoundPendingFlag = 0x509668,     // Pending sound transition flag.
    MenuSoundSpecialFlag = 0x50967C,     // Selects the alternate sound transition path.
    BgmHandle = 0x50966C,                // Active BGM audio handle, or -1.
    BgmCurrentPath = 0xC21C0C,           // Current BGM path cached by BgmLoad.

    InitialLivesBackup = 0xC21DE0,       // Value later restored into CurrentLives.
    InitialBombsBackup = 0xC21DE1,       // Value later restored into CurrentBombs.
    SpellMetadataCount = 0xC2208C,       // Number of 0x28-byte native SpellMeta records.
    SpellMetadataTable = 0xC221C0,       // Pointer to the native SpellMeta array.
    LoadSpellMetadata = 0x76B90,         // Lazily loads the native SpellMeta table.
    MenuExitFlag = 0xC21C0C,             // Cleared when leaving the menu for gameplay.

    GameTimerFrequency = 0xC220F8,       // Source value used by the FPS limiter.
    GameTimerPeriod = 0xC22100,          // Effective FPS limiter period.
};

// Fields inside the native main-menu object passed at +0x4BFB2.
enum class PracticeMenuField : ptrdiff_t {
    SelectedStage = 0x28,                // Zero-based stage selected by the menu.
    TransitionFrame = 0x9638,            // Native menu fade/transition frame.
    State = 0x168B0,                     // Native main-menu state; 0x16 is stage select.
};

// Fields in runtime game objects rather than module-relative globals.
enum class GameObjectField : ptrdiff_t {
    PlayerCollisionRadius = 0x774C,       // Collision context's player radius, float.
};

constexpr uintptr_t GameRva(GameAddress address) noexcept
{
    return static_cast<uintptr_t>(address);
}

constexpr ptrdiff_t GameField(PracticeMenuField field) noexcept
{
    return static_cast<ptrdiff_t>(field);
}

constexpr ptrdiff_t GameField(GameObjectField field) noexcept
{
    return static_cast<ptrdiff_t>(field);
}

std::byte* GameModuleBase() noexcept;
size_t GameModuleImageSize() noexcept;
bool IsGameAddressRangeValid(GameAddress address, size_t size) noexcept;

template <typename T = void>
T* ResolveGameAddress(GameAddress address) noexcept
{
    std::byte* base = GameModuleBase();
    return base ? reinterpret_cast<T*>(base + GameRva(address)) : nullptr;
}
