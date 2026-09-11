#pragma once

#include <windows.h>

bool InstallBootstrapHook();
DWORD WINAPI OverlayWorker(void* module);
void ClearStagePlayfieldBlack();

bool IsGameStretchModeEnabled();
void SetGameStretchModeEnabled(bool enabled);
