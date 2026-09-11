#pragma once

bool InstallCollisionCaptureHook();
void DrawCapturedHitboxes();
void DrawHitboxSettingsUi();
const char* CollisionCaptureStatus();
bool IsHitboxDisplayEnabled();
void SetHitboxDisplayEnabled(bool enabled);
