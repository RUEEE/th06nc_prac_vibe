#pragma once

bool InstallPracticeMenuHook();
const char* PracticeMenuHookStatus();
bool IsPracticeMenuReplacementActive();
bool IsEnhancedPracticeRunActive();

// Called once from the renderer's ImGui frame. All replacement-menu drawing is
// deliberately kept in this one function.
void DrawPracticeMenuReplacementUi();
