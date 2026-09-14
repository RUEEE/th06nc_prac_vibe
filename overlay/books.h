#pragma once

struct BooksInfo {
    bool isInBooks = false;
    int lastMiss = 0;
    int lastBomb = 0;
    int activeShot = -1;
    int activeDifficulty = -1;
    int attemptCount[4][4]{};
    int passCount[4][4]{};
};

// Loads the persistent counters before the ECL timeline hook becomes active.
void InitializeBooksTracking();

// Called by the existing EnemyTimelineUpdate detour. The interpreter's
// current instruction is inspected without installing another mid-function
// hook over the opcode dispatch switch.
void ObserveBooksTimeline(void* enemyManager);

// Abandons an unfinished attempt on restart, exit, or a new run.
void ResetBooksAttempt();

const BooksInfo& GetBooksInfo();
