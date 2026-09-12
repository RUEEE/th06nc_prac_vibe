#pragma once

bool InstallCollisionCaptureHook();
void DrawCapturedHitboxes();
void DrawHitboxSettingsUi();
const char* CollisionCaptureStatus();
bool IsHitboxDisplayEnabled();
bool IsHitboxDisplayActive();
void SetHitboxDisplayEnabled(bool enabled);
// Draws the compact persistent offset/scale/color controls shown immediately
// below the full-screen menu's hitbox checkbox.
void DrawHitboxDisplayControlsUi();
