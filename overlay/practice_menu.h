#pragma once

#include <cstdint>

// The parameter list is the single source of truth for ordinary practice
// fields. It declares PracticeParam and also drives the replay key/value map.
// Keep field names stable: they are part of the on-disk replay format. The
// fourth argument is the first protocol that understands a non-default value.
#define TH06NC_PRACTICE_PARAM_FIELDS(X) \
    X(int32_t, stage, 0, 4)              \
    X(int32_t, difficulty, 0, 4)         \
    X(int32_t, practiceMode, 1, 4)       \
    X(int32_t, warpTarget, 0, 4)         \
    X(int32_t, chapter, 1, 4)            \
    X(int32_t, timelineFrame, 0, 4)      \
    X(int32_t, bossJump, 0, 4)           \
    X(int32_t, dialogue, 0, 4)           \
    X(int32_t, lives, 8, 4)              \
    X(int32_t, bombs, 8, 4)              \
    X(int64_t, score, 0, 4)              \
    X(int32_t, power, 128, 4)            \
    X(int32_t, graze, 0, 4)              \
    X(int32_t, pointItems, 0, 4)         \
    X(int32_t, fakeShot, 0, 4)           \
    X(int32_t, raging495, 0, 4)          \
    X(int32_t, stage5Boss6Mode, 0, 5)    \
    X(uint32_t, bookFixedMask, 0x0, 4)

struct PracticeParam {
#define TH06NC_DECLARE_PRACTICE_FIELD(type_, name_, default_, protocol_) \
    type_ name_ = default_;
    TH06NC_PRACTICE_PARAM_FIELDS(TH06NC_DECLARE_PRACTICE_FIELD)
#undef TH06NC_DECLARE_PRACTICE_FIELD

    int32_t bookX[6] = {-180, -116, -61, 41, 112, 180};
    int32_t bookY[6] = {32, 128, 144, 64, 80, 96};
};

using PracticeReplayConfig = PracticeParam;

bool InstallPracticeMenuHook();
const char* PracticeMenuHookStatus();
bool IsPracticeMenuReplacementActive();
bool IsEnhancedPracticeRunActive();
void EndEnhancedPracticeRun();
bool MarkEnhancedPracticeRestartPending();
bool IsRaging495PracticeActive();
bool ExportPracticeReplayConfig(PracticeReplayConfig& config);
bool ImportPracticeReplayConfig(const PracticeReplayConfig& config);

// Called once from the renderer's ImGui frame. All replacement-menu drawing is
// deliberately kept in this one function.
void DrawPracticeMenuReplacementUi();
struct PausedPracticeUiResult {
    bool hovered = false;
    bool leaveUp = false;
    bool leaveDown = false;
};

// Reuses the complete pre-run editor from the in-game pause menu. Navigation
// uses -1/0/+1 vertical and horizontal deltas captured from logical input.
PausedPracticeUiResult DrawPausedPracticeConfigurationUi(int vertical,
    int horizontal, bool focused, bool resetNavigation,
    bool resetToLast = false);
