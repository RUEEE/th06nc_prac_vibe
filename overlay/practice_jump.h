#pragma once

#include <map>
#include <vector>

enum Bosstype {
    MID_BOSS_NONSPELL,
    MID_BOSS_SPELL,
    BOSS_NONSPELL,
    BOSS_SPELL,
};

enum JumpEnum {
    TH06NC_JUMP_NONE,
    TH06NC_ST1_MID1, TH06NC_ST1_MID2,
    TH06NC_ST1_BOSS1, TH06NC_ST1_BOSS2, TH06NC_ST1_BOSS3, TH06NC_ST1_BOSS4,
    TH06NC_ST2_MID1,
    TH06NC_ST2_BOSS1, TH06NC_ST2_BOSS2, TH06NC_ST2_BOSS3, TH06NC_ST2_BOSS4,
    TH06NC_ST2_BOSS5,
    TH06NC_ST3_MID1, TH06NC_ST3_MID2,
    TH06NC_ST3_BOSS1, TH06NC_ST3_BOSS2, TH06NC_ST3_BOSS3, TH06NC_ST3_BOSS4,
    TH06NC_ST3_BOSS5, TH06NC_ST3_BOSS6, TH06NC_ST3_BOSS7,
    TH06NC_ST4_MID1,
    TH06NC_ST4_BOSS1, TH06NC_ST4_BOSS2, TH06NC_ST4_BOSS3, TH06NC_ST4_BOSS4,
    TH06NC_ST4_BOSS5, TH06NC_ST4_BOSS6, TH06NC_ST4_BOSS7,
    TH06NC_ST5_MID1, TH06NC_ST5_MID2,
    TH06NC_ST5_BOSS1, TH06NC_ST5_BOSS2, TH06NC_ST5_BOSS3, TH06NC_ST5_BOSS4,
    TH06NC_ST5_BOSS5, TH06NC_ST5_BOSS6,
    TH06NC_ST6_MID1, TH06NC_ST6_MID2,
    TH06NC_ST6_BOSS1, TH06NC_ST6_BOSS2, TH06NC_ST6_BOSS3, TH06NC_ST6_BOSS4,
    TH06NC_ST6_BOSS5, TH06NC_ST6_BOSS6, TH06NC_ST6_BOSS7, TH06NC_ST6_BOSS8,
    TH06NC_ST6_BOSS9,
    TH06NC_ST7_MID1, TH06NC_ST7_MID2, TH06NC_ST7_MID3,
    TH06NC_ST7_BOSS1, TH06NC_ST7_BOSS2, TH06NC_ST7_BOSS3, TH06NC_ST7_BOSS4,
    TH06NC_ST7_BOSS5, TH06NC_ST7_BOSS6, TH06NC_ST7_BOSS7, TH06NC_ST7_BOSS8,
    TH06NC_ST7_BOSS9, TH06NC_ST7_BOSS10, TH06NC_ST7_BOSS11, TH06NC_ST7_BOSS12,
    TH06NC_ST7_BOSS13, TH06NC_ST7_BOSS14, TH06NC_ST7_BOSS15, TH06NC_ST7_BOSS16,
    TH06NC_ST7_BOSS17, TH06NC_ST7_BOSS18,
    TH06NC_ST7_BOSS19, TH06NC_ST7_BOSS20, TH06NC_ST7_BOSS21,
};

struct BossJump {
    Bosstype type;
    JumpEnum jumpname;
    int stage;
    int diff;
};

const std::map<int, std::vector<int>>& StageChapterTimes();
const std::vector<BossJump>& BossJumps();

// Queued on the practice-select update thread and consumed once, after the
// selected stage's ECL has been loaded by the gameplay update thread.
void QueueStagePracticeJump(int stage, int timelineTime);
void QueueStage4BooksPracticeJump(int timelineTime, unsigned fixedMask,
    const int* x, const int* y);
void QueueBossPracticeJump(int stage, JumpEnum jump, bool dialogue,
    int fakeShot = 0);
void ClearQueuedPracticeJump();

bool InstallPracticeJumpHook();
const char* PracticeJumpHookStatus();
