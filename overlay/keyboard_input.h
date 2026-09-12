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
// True while the F9-F12 key-binding UI is waiting for a physical key. All
// gameplay and trainer hotkeys should ignore keyboard input in this state.
bool IsKeyBindingCaptureActive();
bool IsRetryKeyPressed();
bool IsExitKeyPressed();
void DrawKeyBindingUi();
void AppendKeyBindingGlyphText(std::string& output);
