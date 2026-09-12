#pragma once

#include <cstdint>

bool InstallReplaySupportHooks();
const char* ReplaySupportHookStatus();

// Called from the already-installed common initializer detour.
bool PreparePracticeReplayPlayback();
void CapturePracticeReplayStart();

// Called from the renderer. The game-thread hook consumes actions atomically.
void DrawPracticePauseUi();
bool IsPracticePauseUiVisible();
