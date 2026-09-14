#pragma once

#include "practice_jump.h"

constexpr int kSpellRateSpellCount = 138;
constexpr int kSpellRateShotCount = 4;
constexpr int kNativeSpellCount = 134;
constexpr int kBooksSpellBase = 134;

struct SpellRate {
    int attempt = 0;
    int captured = 0;
};

struct SpellInfo {
    int id = -1;
    int diff = -1; // E=0, N=1, H=2, L=3, EX=4.
    JumpEnum nameLocale = TH06NC_JUMP_NONE;
};

extern SpellRate allSpells[kSpellRateSpellCount][kSpellRateShotCount];
// Intentionally sparse for now. Fill entries with {spell id, difficulty,
// localized JumpEnum}; unused entries retain id == -1.
extern SpellInfo spellInfo[kSpellRateSpellCount];

struct BooksInfo {
    bool isInBooks = false;
    int lastMiss = 0;
    int lastBomb = 0;
    int activeShot = -1;
    int activeDifficulty = -1;
};

// Loads spell_capture.dat and installs the normal-play increment and native
// rate-display hooks.
bool InstallSpellRateHooks();

// Called by the existing EnemyTimelineUpdate detour.
void ObserveBooksTimeline(void* enemyManager);
void ResetBooksAttempt();
const BooksInfo& GetBooksInfo();

int CurrentSpellRateShot();
const SpellRate& GetSpellRate(int spellId, int shot);
void DrawSpellRateTableUi();
