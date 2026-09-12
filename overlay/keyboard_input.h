#pragma once

#include <string>

// Installs the game's central keyboard-update detour at
// GameAddress::KeyboardUpdate.
// Keep input transformations in this module so remapping/macros can be added
// later without changing the renderer hooks.
bool InstallKeyboardInputHook();
const char* KeyboardInputHookStatus();
bool IsGameProcessForeground();

// Auto-shoot is injected into the logical action word so generated shots are
// recorded as ordinary Z input. Replay playback is never modified.
bool IsAutoShootEnabled();
void SetAutoShootEnabled(bool enabled);
bool IsAutoShooting();
bool IsRetryKeyPressed();
bool IsExitKeyPressed();
bool IsConfirmKeyPressed();
void DrawKeyBindingUi();
void AppendKeyBindingGlyphText(std::string& output);
