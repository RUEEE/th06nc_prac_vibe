#pragma once

#include <cstdint>

// Handles the Backspace/F1-F8 hotkeys, applies the corresponding runtime
// patches, and draws the compact upper-right overlay when it is open.
bool InstallGameOverlayHook();
// Polls Backspace/F1-F8 and applies continuous helper state. This must run
// before renderer visibility is tested so a closed overlay can open itself.
void UpdateGameOverlayState();
void UpdateAndDrawGameOverlayUi();
bool IsGameOverlayVisible();
bool IsBombInputSuppressed();
bool IsEverlastingBgmEnabled();
// Sets the game's native KeepBgm byte immediately before player/game
// initialization. It is enabled only for an enhanced-practice retry.
void PrepareEverlastingBgmForInitialization(bool enhancedRetry);
