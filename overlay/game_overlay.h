#pragma once

#include <cstdint>

// Handles the Backspace/F1-F7 hotkeys, applies the corresponding runtime
// patches, and draws the compact upper-right overlay when it is open.
bool InstallGameOverlayHook();
void UpdateAndDrawGameOverlayUi();
bool IsGameOverlayVisible();
bool IsBombInputSuppressed();

// BgmLoad integration used by the Practice stage-load hook. A nonnegative
// return value is the position to restore after the native reload completes.
int64_t BeginEverlastingBgmLoad(void* audioState, const char* path);
void EndEverlastingBgmLoad(const char* path, int64_t resumePosition);
