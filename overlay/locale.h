#pragma once

#include <array>
#include <map>
#include <string>

enum class Language : int {
    Chinese,
    English,
    Japanese,
};

enum class LocaleText {
    Language,
    Version,
    Licenses,
    BaseTitle,
    InjectionActive,
    Renderer,
    ProcessId,
    MenuHotkeyHint,
    ShowDemo,
    AutoShoot,
    KeyBindings,
    KeyUp,
    KeyDown,
    KeyLeft,
    KeyRight,
    KeySlow,
    KeyShoot,
    KeyBomb,
    KeySkip,
    KeyAutoShoot,
    KeyRetry,
    KeyExit,
    KeyConfirm,
    ArrowKeyPreset,
    WasdKeyPreset,
    SocdMode,
    SocdNone,
    SocdLastInput,
    SocdFirstInput,
    SocdNeutral,
    CurrentKey,
    ChooseKey,
    PressAKey,
    ShowHitboxes,
    SquareHitboxes,
    HitboxOffset,
    HitboxScale,
    HitboxColor,
    ApplyStretch,
    StretchHelp,
    GameSpeed,
    Ok,
    Invincible,
    LockLives,
    LockBombs,
    LockPower,
    LockTime,
    AutoBomb,
    EverlastingBgm,
    DisableBomb,
    PatchUnsupported,
    PauseMenu,
    Resume,
    Restart,
    SaveReplayAndExit,
    ExitWithoutReplay,
    ReplaySaveHint,
    ReplayFileHint,
    ReplayHook,
    ReimuA,
    ReimuB,
    MarisaA,
    MarisaB,
    PracticeSetup,
    Mode,
    Original,
    Enhanced,
    Stage,
    Stage1,
    Stage2,
    Stage3,
    Stage4,
    Stage5,
    Stage6,
    ExtraStage,
    WarpTo,
    None,
    StagePortion,
    BossMidboss,
    Type,
    Boss,
    Midboss,
    MidbossNonspell,
    MidbossSpell,
    BossNonspell,
    BossSpell,
    Nonspell,
    Spell,
    Frame,
    Chapter,
    Chapter_1,
    Chapter_2,
    TimelineTime,
    NoJump,
    Jump,
    Dialogue,
    Lives,
    Bombs,
    Score,
    Power,
    Graze,
    Point,
    FakeShot,
    Raging495,
    DefaultPattern,
    FastPattern,
    SlowPattern,
    Stage4Books,
    Fixed,
    MirrorLastThree,
    MirrorAll,
    RotateBooks,
    RandomizeBookX,
    ResetBookY,
    CopyBookConfig,
    PasteBookConfig,
    Start,
    NavigationHelp,
    JumpHook,
    WarpHelp,
    StatusActive,
    StatusForegroundOnly,
    StatusUnsupportedExecutable,
    StatusAllocationFailed,
    StatusX64AllocationFailed,
    StatusPatchFailed,
    StatusNotInstalled,
    StatusPracticePartial,
};

struct LocalizedText {
    const char* chinese;
    const char* english;
    const char* japanese;
};

class Locale {
public:
    static Locale& Instance();

    Language GetLanguage() const noexcept;
    void SetLanguage(Language language) noexcept;
    const char* Get(LocaleText text) const;
    const char* GetJump(int key, int difficulty) const;
    const char* GetJump(int key, int difficulty, int fakeShot) const;
    void AppendAllGlyphText(std::string& output) const;

private:
    Locale();
    const char* Select(const LocalizedText& text) const noexcept;

    Language language_;
    std::map<LocaleText, LocalizedText> text_;
    std::map<int, std::array<LocalizedText, 5>> jumps_;
};

#define S(id) (::Locale::Instance().Get(LocaleText::id))
