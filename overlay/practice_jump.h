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
    TH06NC_ST4_BOOKS,TH06NC_ST4_MID1,
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

    TH06NC_BOOKS,
    TH06NC_FIRE_1,
    TH06NC_FIRE_2,
    TH06NC_FIRE_3,
    TH06NC_WOOD_1,
    TH06NC_WOOD_2,
    TH06NC_WOOD_3,
    TH06NC_EARTH_1,
    TH06NC_EARTH_2,
    TH06NC_EARTH_3,
    TH06NC_WATER_1,
    TH06NC_WATER_2,
    TH06NC_METAL_1,
    TH06NC_METAL_2,
    TH06NC_FIRE_EARTH,
    TH06NC_WATER_WOOD,
    TH06NC_WOOD_FIRE,
    TH06NC_EARTH_METAL,
    TH06NC_METAL_WATER,
};

struct BossJump {
    Bosstype type;
    JumpEnum jumpname;
    int stage;
    int diff;
};

const std::map<int, std::pair<std::vector<int>, std::vector<int>>>& StageChapterTimes();
int GetChapterTime(int stage, int chapter);
const std::vector<BossJump>& BossJumps();

// Queued during practice selection and consumed once by a later timeline
// callback, after the selected stage's ECL has been loaded.
void QueueStagePracticeJump(int stage, int timelineTime);
void QueueStage4BooksPracticeJump(int timelineTime, unsigned fixedMask,
    const int* x, const int* y);
void QueueBossPracticeJump(int stage, JumpEnum jump, bool dialogue,
    int fakeShot = 0, int stage5Boss6Mode = 0, unsigned fixedMask = 0, const int* x = nullptr, const int* y = nullptr);
void ClearQueuedPracticeJump();

bool InstallPracticeJumpHook();
const char* PracticeJumpHookStatus();
