#include "practice_jump.h"

#include "game_addresses.h"
#include "locale.h"
#include "practice_menu.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

constexpr int kMainDifficulties = 1 | 2 | 4 | 8;
constexpr int kExtraDifficulty = 16;
constexpr ptrdiff_t kEnemyManagerTimelineTime = 0x10c0bc;
constexpr size_t kTimelineUpdatePrologueSize = 6;
constexpr unsigned char kExpectedTimelineUpdatePrologue[kTimelineUpdatePrologueSize] = {
    0x48, 0x8b, 0xc4, 0x53, 0x56, 0x57
};
constexpr size_t kFinalSpellRagePrologueSize = 17;
constexpr unsigned char kExpectedFinalSpellRagePrologue[kFinalSpellRagePrologueSize] = {
    0x48, 0x83, 0xec, 0x18, 0x33, 0xc0,
    0x0f, 0x29, 0x34, 0x24,
    0x81, 0x79, 0x04, 0x20, 0x1c, 0x00, 0x00,
};

enum class RequestKind : int { None, Stage, Stage4Books, Boss };
enum class HookStatus : int {
    NotInstalled, Installed, UnsupportedExecutable, AllocationFailed, PatchFailed
};

struct StageEclIdentity {
    uint16_t subCount;
    uint32_t timelineOffset;
    size_t size;
};

constexpr StageEclIdentity kStageEclIdentity[7] = {
    {38, 0x5074, 26936}, {49, 0x3fd8, 29460}, {61, 0x65cc, 38436},
    {119, 0xe6e8, 70296}, {75, 0x71f0, 35672}, {87, 0x87d8, 38436},
    {145, 0x10974, 77684},
};

struct PracticeJumpRequest {
    RequestKind kind = RequestKind::None;
    int stage = 0;
    int timelineTime = 0;
    JumpEnum jump = TH06NC_JUMP_NONE;
    bool dialogue = true;
    int fakeShot = 0;
    int stage5Boss6Mode = 0;
    unsigned bookFixedMask = 0;
    std::array<int, 6> bookX{};
    std::array<int, 6> bookY{};
};

struct PracticeJumpRuntime {
    PracticeJumpRequest pending{};
    HookStatus hookStatus = HookStatus::NotInstalled;
};

PracticeJumpRuntime g_practiceJumpRuntime{};

using TimelineUpdateFn = int(__fastcall*)(void* enemyManager);
TimelineUpdateFn g_originalTimelineUpdate = nullptr;
using FinalSpellRageFn = void(__fastcall*)(void* enemy, void* instruction);
FinalSpellRageFn g_originalFinalSpellRage = nullptr;

struct PatchPair {
    size_t offset;
    uint32_t value;
    uint8_t size;

    template <typename T>
        requires std::is_integral_v<T>
    PatchPair(size_t patchOffset, T patchValue)
        : offset(patchOffset), value(static_cast<uint32_t>(patchValue)),
          size(sizeof(T) == 2 ? 2 : 4) {}

    PatchPair(size_t patchOffset, float patchValue)
        : offset(patchOffset), value(std::bit_cast<uint32_t>(patchValue)),
          size(4) {}
};

class EclWriter {
public:
    EclWriter(std::byte* data, size_t size) : data_(data), size_(size) {}

    EclWriter& operator<<(const PatchPair& patch)
    {
        if (!valid_ || patch.offset > size_ || patch.size > size_ - patch.offset) {
            valid_ = false;
            return *this;
        }
        if (patch.size == 2)
            *reinterpret_cast<uint16_t*>(data_ + patch.offset) =
                static_cast<uint16_t>(patch.value);
        else
            *reinterpret_cast<uint32_t*>(data_ + patch.offset) = patch.value;
        return *this;
    }

    bool valid() const { return valid_; }

private:
    std::byte* data_;
    size_t size_;
    bool valid_ = true;
};

using pair = PatchPair;

void ECLSetHealth(EclWriter& ecl, size_t offset, uint32_t time, uint32_t health)
{
    ecl << pair{offset, time} << pair{offset + 4, 0x0010006f}
        << pair{offset + 8, 0x00ffff00} << pair{offset + 12, health};
}

void ECLSetTime(EclWriter& ecl, size_t offset, uint32_t time, uint32_t value)
{
    ecl << pair{offset, time} << pair{offset + 4, 0x00100073}
        << pair{offset + 8, 0x00ffff00} << pair{offset + 12, value};
}

void ECLStall(EclWriter& ecl, size_t offset)
{
    ecl << pair{offset, 0x99999} << pair{offset + 4, 0x000c0000}
        << pair{offset + 8, 0x0000ff00};
}


#define J(type_, name_, stage_, diff_) \
    {type_, name_, stage_, diff_}

const std::vector<BossJump> kBossJumps = {
J(MID_BOSS_NONSPELL, TH06NC_ST1_MID1, 1, kMainDifficulties),
J(MID_BOSS_SPELL, TH06NC_ST1_MID2, 1, 12),
J(BOSS_NONSPELL, TH06NC_ST1_BOSS1, 1, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST1_BOSS2, 1, kMainDifficulties),
J(BOSS_NONSPELL, TH06NC_ST1_BOSS3, 1, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST1_BOSS4, 1, kMainDifficulties),

J(MID_BOSS_NONSPELL, TH06NC_ST2_MID1, 2, kMainDifficulties),
J(BOSS_NONSPELL, TH06NC_ST2_BOSS1, 2, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST2_BOSS2, 2, kMainDifficulties),
J(BOSS_NONSPELL, TH06NC_ST2_BOSS3, 2, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST2_BOSS4, 2, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST2_BOSS5, 2, 14),

J(MID_BOSS_NONSPELL, TH06NC_ST3_MID1, 3, kMainDifficulties),
J(MID_BOSS_SPELL, TH06NC_ST3_MID2, 3, kMainDifficulties),
J(BOSS_NONSPELL, TH06NC_ST3_BOSS1, 3, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST3_BOSS2, 3, kMainDifficulties),
J(BOSS_NONSPELL, TH06NC_ST3_BOSS3, 3, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST3_BOSS4, 3, 12),
J(BOSS_NONSPELL, TH06NC_ST3_BOSS5, 3, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST3_BOSS6, 3, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST3_BOSS7, 3, 14),

J(MID_BOSS_NONSPELL, TH06NC_ST4_BOOKS, 4, kMainDifficulties),
J(MID_BOSS_NONSPELL, TH06NC_ST4_MID1, 4, kMainDifficulties),

J(BOSS_NONSPELL, TH06NC_ST4_BOSS1, 4, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST4_BOSS2, 4, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST4_BOSS3, 4, kMainDifficulties),
J(BOSS_NONSPELL, TH06NC_ST4_BOSS4, 4, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST4_BOSS5, 4, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST4_BOSS6, 4, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST4_BOSS7, 4, kMainDifficulties),

J(MID_BOSS_NONSPELL, TH06NC_ST5_MID1, 5, kMainDifficulties),
J(MID_BOSS_SPELL, TH06NC_ST5_MID2, 5, kMainDifficulties),
J(BOSS_NONSPELL, TH06NC_ST5_BOSS1, 5, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST5_BOSS2, 5, kMainDifficulties),
J(BOSS_NONSPELL, TH06NC_ST5_BOSS3, 5, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST5_BOSS4, 5, kMainDifficulties),
J(BOSS_NONSPELL, TH06NC_ST5_BOSS5, 5, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST5_BOSS6, 5, kMainDifficulties),

J(MID_BOSS_NONSPELL, TH06NC_ST6_MID1, 6, kMainDifficulties),
J(MID_BOSS_SPELL, TH06NC_ST6_MID2, 6, 14),
J(BOSS_NONSPELL, TH06NC_ST6_BOSS1, 6, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST6_BOSS2, 6, kMainDifficulties),
J(BOSS_NONSPELL, TH06NC_ST6_BOSS3, 6, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST6_BOSS4, 6, kMainDifficulties),
J(BOSS_NONSPELL, TH06NC_ST6_BOSS5, 6, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST6_BOSS6, 6, kMainDifficulties),
J(BOSS_NONSPELL, TH06NC_ST6_BOSS7, 6, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST6_BOSS8, 6, kMainDifficulties),
J(BOSS_SPELL, TH06NC_ST6_BOSS9, 6, kMainDifficulties),

J(MID_BOSS_SPELL, TH06NC_ST7_MID1, 7, kExtraDifficulty),
J(MID_BOSS_SPELL, TH06NC_ST7_MID2, 7, kExtraDifficulty),
J(MID_BOSS_SPELL, TH06NC_ST7_MID3, 7, kExtraDifficulty),
J(BOSS_NONSPELL, TH06NC_ST7_BOSS1, 7, kExtraDifficulty),
J(BOSS_SPELL, TH06NC_ST7_BOSS2, 7, kExtraDifficulty),
J(BOSS_NONSPELL, TH06NC_ST7_BOSS3, 7, kExtraDifficulty),
J(BOSS_SPELL, TH06NC_ST7_BOSS4, 7, kExtraDifficulty),
J(BOSS_NONSPELL, TH06NC_ST7_BOSS5, 7, kExtraDifficulty),
J(BOSS_SPELL, TH06NC_ST7_BOSS6, 7, kExtraDifficulty),
J(BOSS_NONSPELL, TH06NC_ST7_BOSS7, 7, kExtraDifficulty),
J(BOSS_SPELL, TH06NC_ST7_BOSS8, 7, kExtraDifficulty),
J(BOSS_NONSPELL, TH06NC_ST7_BOSS9, 7, kExtraDifficulty),
J(BOSS_SPELL, TH06NC_ST7_BOSS10, 7, kExtraDifficulty),
J(BOSS_NONSPELL, TH06NC_ST7_BOSS11, 7, kExtraDifficulty),
J(BOSS_SPELL, TH06NC_ST7_BOSS12, 7, kExtraDifficulty),
J(BOSS_NONSPELL, TH06NC_ST7_BOSS13, 7, kExtraDifficulty),
J(BOSS_SPELL, TH06NC_ST7_BOSS14, 7, kExtraDifficulty),
J(BOSS_NONSPELL, TH06NC_ST7_BOSS15, 7, kExtraDifficulty),
J(BOSS_SPELL, TH06NC_ST7_BOSS16, 7, kExtraDifficulty),
J(BOSS_SPELL, TH06NC_ST7_BOSS17, 7, kExtraDifficulty),
J(BOSS_SPELL, TH06NC_ST7_BOSS18, 7, kExtraDifficulty),
J(BOSS_SPELL, TH06NC_ST7_BOSS19, 7, kExtraDifficulty),
J(BOSS_SPELL, TH06NC_ST7_BOSS20, 7, kExtraDifficulty),
J(BOSS_SPELL, TH06NC_ST7_BOSS21, 7, kExtraDifficulty),
};

#undef J

bool ApplyBossPatch(EclWriter& ecl, JumpEnum section, bool dialogue,
    int fakeShot, int stage5Boss6Mode, unsigned fixedMask, const int* bookX, const int* bookY, int& timelineTime)
{
    auto ECLWarp = [&](int time) { timelineTime = time; };
    auto s2b_nd = [&]() {
        ECLWarp(0x1706);
        ecl << pair{0x192c, 0x0} << pair{0x194c, 0x0} << pair{0x195c, 0x0}
            << pair{0x197c, 0x0} << pair{0x199c, 0x0} << pair{0x19bc, 0x0}
            << pair{0x19d0, 0x0};
    };
    auto s3b_n1 = [&]() {
        ecl << pair{0x12f4, int16_t{0}} << pair{0x1360, int16_t{0}};
        ECLWarp(0x1864);
        ecl << pair{0x95cc, int16_t{0x1864}} << pair{0x2264, 0x1}
            << pair{0x2284, 0x0} << pair{0x95d2, int16_t{0x24}}
            << pair{0x23f8, 0x0} << pair{0x2418, 0x0} << pair{0x2438, 0x0}
            << pair{0x2458, 0x1e} << pair{0x246c, 0x1e} << pair{0x24a0, 0x1e}
            << pair{0x2494, 150};
    };
    auto s4b_time = [&]() {
        ecl << pair{0x2890, 0x0} << pair{0x28b0, 0x0} << pair{0x28d0, 0x0}
            << pair{0x28f0, 0x0} << pair{0x2910, 0x0}
            // This animation instruction only exists in nc_ecl, but belongs
            // to the same loop header and must be back-dated with it.
            << pair{0x2924, 0x0} << pair{0x2934, 0x0}
            << pair{0x294c, 0x0} << pair{0x2960, 0x0};
    };
    auto s4_fake_shot = [&]() {
        if (fakeShot < 1 || fakeShot > 4)
            return;
        // The first two spells branch directly on $PLAYER_SHOT in
        // Sub27/Sub37. Replacing the special-variable operand with an
        // immediate shot index selects the matching callback sub.
        constexpr size_t earlySpellOperands[] = {
            0x24cc, 0x2548, 0x25c4, 0x26a8, 0x2724, 0x27a0,
            0x6fe8, 0x7064, 0x70e0, 0x71c4, 0x7240, 0x72bc,
            0x73a0, 0x741c, 0x7498,
        };
        for (const size_t operand : earlySpellOperands)
            ecl << pair{operand, fakeShot - 1};

        // The last three spells do not read $PLAYER_SHOT. ex_ins_call(3, 0)
        // fills $I1/$I2/$I3 with one of five element-combination indices,
        // then Sub39/Sub40/Sub41 dispatch through five call_equ instructions.
        // These are the same values used by classic thprac's
        // PATCHOULI_SHOTTYPE_VARS table, expressed directly in ECL.
        constexpr int lateSpellSelectors[4][3] = {
            {0, 3, 1}, // Reimu A: fire/earth, metal/water, wood/fire
            {2, 3, 4}, // Reimu B: water/wood, metal/water, earth/metal
            {1, 4, 0}, // Marisa A: wood/fire, earth/metal, fire/earth
            {4, 2, 3}, // Marisa B: earth/metal, water/wood, metal/water
        };
        constexpr size_t lateSpellOperands[3][5] = {
            {0x7c74, 0x7c94, 0x7cb4, 0x7cd4, 0x7cf4}, // $I1
            {0x7ea8, 0x7ec8, 0x7ee8, 0x7f08, 0x7f28}, // $I2
            {0x809c, 0x80bc, 0x80dc, 0x80fc, 0x811c}, // $I3
        };
        for (int spell = 0; spell < 3; ++spell) {
            const int selector = lateSpellSelectors[fakeShot - 1][spell];
            for (const size_t operand : lateSpellOperands[spell])
                ecl << pair{operand, selector};
        }
    };
    auto s7b_call = [&]() {
        ecl << pair{0x36be, 0x0} << pair{0x36c2, 0x00180023}
            << pair{0x36c6, 0x00ffff00} << pair{0x36ca, 0x0}
            << pair{0x36ce, 0x0} << pair{0x36d2, 0x0};
        ECLStall(ecl, 0x36d6);
    };
    auto s7b_n1 = [&]() {
        ECLWarp(0x212e);
        ecl << pair{0x360e, 0x0} << pair{0x361e, 0x0} << pair{0x363e, 0x0}
            << pair{0x365e, 0x0} << pair{0x367e, 0x0} << pair{0x369e, 0x0};
    };

    switch (section) {
    case TH06NC_ST1_MID1:
        ECLWarp(0x75a);
        ecl << pair{0xbec, 0x3c} << pair{0xc0c, 0x3c};
        break;
    case TH06NC_ST1_MID2:
        ECLWarp(0x75a);
        ecl << pair{0xbec, 0x3c} << pair{0xc0c, 0x3c};
        ECLSetHealth(ecl, 0xc2c, 0x3c, 0x1f3);
        break;
    case TH06NC_ST1_BOSS1:
        if (dialogue)
            ECLWarp(0x13e4);
        else {
            ECLWarp(0x13e5);
            ecl << pair{0x18dc, 0} << pair{0x18fc, 0} << pair{0x191c, 0x50};
        }
        break;
    case TH06NC_ST1_BOSS2:
        ECLWarp(0x13e5);
        ecl << pair{0x18dc, 0} << pair{0x18fc, 0} << pair{0x191c, 0x50};
        ECLSetTime(ecl, 0x191c, 0, 0);
        ECLStall(ecl, 0x192c);
        break;
    case TH06NC_ST1_BOSS3:
    case TH06NC_ST1_BOSS4:
        ECLWarp(0x13e5);
        ecl << pair{0x18dc, 0} << pair{0x18fc, 0} << pair{0x191c, 0x50}
            << pair{0x1928, 0x13} << pair{0x2b70, 0} << pair{0x2b80, 0}
            << pair{0x2b54, int16_t{0}};
        if (section == TH06NC_ST1_BOSS4) {
            ECLSetTime(ecl, 0x2b80, 0, 0);
            ECLStall(ecl, 0x2b90);
        }
        break;

    case TH06NC_ST2_MID1:
        ECLWarp(0x9c2);
        break;
    case TH06NC_ST2_BOSS1:
        ECLWarp(dialogue ? 0x1705 : 0x1706);
        break;
    case TH06NC_ST2_BOSS2:
        s2b_nd();
        ECLSetTime(ecl, 0x19d0, 0, 0);
        ECLStall(ecl, 0x19e0);
        break;
    case TH06NC_ST2_BOSS3:
    case TH06NC_ST2_BOSS4:
    case TH06NC_ST2_BOSS5:
        s2b_nd();
        ecl << pair{0x19dc, 0x19} << pair{0x2158, 0x0} << pair{0x2168, 0x60}
            << pair{0x2130, int16_t{0}};
        if (section == TH06NC_ST2_BOSS3)
            break;
        if (section == TH06NC_ST2_BOSS4) {
            ECLSetTime(ecl, 0x2168, 0x30, 0);
            ECLStall(ecl, 0x2178);
            break;
        }
        ecl << pair{0x2168, 0x0} << pair{0x2174, 0x27}
            << pair{0x3780, int16_t{0}} << pair{0x3758, int16_t{0}}
            << pair{0x3770, int16_t{0}} << pair{0x20b0, 0x578}
            << pair{0x20d0, 0xffffffff} << pair{0x20e0, 0xffffffff}
            << pair{0x2110, 0x1c};
        break;

    case TH06NC_ST3_MID1:
        ECLWarp(0xd92);
        break;
    case TH06NC_ST3_MID2:
        ECLWarp(0xd92);
        ecl << pair{0x12f4, int16_t{0}} << pair{0x1360, int16_t{0}}
            << pair{0x1098, 0x0};
        ECLSetHealth(ecl, 0x115c, 0x1e, 0x513);
        ECLStall(ecl, 0x116c);
        break;
    case TH06NC_ST3_BOSS1:
        if (dialogue)
            ECLWarp(0x1864);
        else
            s3b_n1();
        break;
    case TH06NC_ST3_BOSS2:
        s3b_n1();
        ecl << pair{0x2458, 0x0};
        ECLSetTime(ecl, 0x246c, 0, 0);
        ECLStall(ecl, 0x247c);
        break;
    case TH06NC_ST3_BOSS3:
    case TH06NC_ST3_BOSS4:
        s3b_n1();
        ecl << pair{0x2458, 0x0} << pair{0x246c, 0x0} << pair{0x2478, 0x18}
            << pair{0x2950, int16_t{0}};
        if (section == TH06NC_ST3_BOSS4) {
            ECLSetTime(ecl, 0x29d8, 0, 0);
            ECLStall(ecl, 0x29e8);
        }
        break;
    case TH06NC_ST3_BOSS5:
    case TH06NC_ST3_BOSS6:
    case TH06NC_ST3_BOSS7:
        s3b_n1();
        ecl << pair{0x2458, 0x0} << pair{0x246c, 0x0} << pair{0x2478, 0x1e}
            << pair{0x3544, int16_t{0}};
        if (section == TH06NC_ST3_BOSS5)
            break;
        ECLSetTime(ecl, 0x35c8, 0, 0);
        ECLStall(ecl, 0x35d8);
        if (section == TH06NC_ST3_BOSS7) {
            ecl << pair{0x34dc, 0x7d0} << pair{0x351c, 0x2d}
                << pair{0x352c, 0x2d} << pair{0x52f8, int16_t{0}}
                << pair{0x5380, int16_t{0}};
        }
        break;
    case TH06NC_ST4_BOOKS:
        ECLWarp(3378 - 30);
        {
            constexpr size_t kFirstBookArguments = 0xf2f8;
            constexpr size_t kBookInstructionSize = 0x1c;
            for (int book = 0; book < 6; ++book) {
                if ((fixedMask & (1u << book)) == 0)
                    continue;
                const size_t offset = kFirstBookArguments +
                    kBookInstructionSize * book;
                // enemy_create_random stores X/Y/Z as three consecutive
                // floats. A fixed book must replace the complete -999.0f
                // random-X sentinel, not merely its low 16 bits.
                ecl << pair{ offset, static_cast<float>(
                        bookX[book] + 192) }
                    << pair{ offset + 4, static_cast<float>(
                        bookY[book]) };
            }
        }

        break;
    case TH06NC_ST4_MID1:
        ECLWarp(0xfda);
        break;
    case TH06NC_ST4_BOSS1:
        s4_fake_shot();
        ECLWarp(dialogue ? 0x290e : 0x290f);
        break;
    case TH06NC_ST4_BOSS2:
        s4_fake_shot();
        ECLWarp(0x290f);
        s4b_time();
        ECLSetTime(ecl, 0x2910, 0, 0);
        ECLStall(ecl, 0x2920);
        break;
    case TH06NC_ST4_BOSS3:
    case TH06NC_ST4_BOSS4:
        s4_fake_shot();
        ECLWarp(0x290f);
        s4b_time();
        ecl << pair{0x2964, int16_t{0x23}} << pair{0x296c, 0x25}
            << pair{0x6ec8, int16_t{0}};
        if (section == TH06NC_ST4_BOSS4) {
            ECLSetTime(ecl, 0x7568, 0, 0);
            ECLStall(ecl, 0x7578);
        }
        break;
    case TH06NC_ST4_BOSS5:
    case TH06NC_ST4_BOSS6:
    case TH06NC_ST4_BOSS7:
        s4_fake_shot();
        ECLWarp(0x290f);
        s4b_time();
        ecl << pair{0x2964, int16_t{0x23}} << pair{0x296c, 0x27}
            << pair{0x7aa0, int16_t{0}};
        if (section == TH06NC_ST4_BOSS5) {
            ecl << pair{0x7c4c, 0x0} << pair{0x7c5c, 0x0}
                << pair{0x7c7c, 0x0} << pair{0x7c9c, 0x0}
                << pair{0x7cbc, 0x0} << pair{0x7cdc, 0x0};
            break;
        }
        ECLSetHealth(ecl, 0x7c4c, 0, 1699);
        if (section == TH06NC_ST4_BOSS6) {
            ECLSetHealth(ecl, 0x7c5c, 0, 3399);
            ECLStall(ecl, 0x7c6c);
            ecl << pair{0x7c54, int16_t{0x0200}}
                << pair{0x7c64, int16_t{0x0c00}}
                << pair{0x7d30, int16_t{0}} << pair{0x7e5c, int16_t{0}}
                << pair{0x7e74, int16_t{0}} << pair{0x7e80, 0x0}
                << pair{0x7e90, 0x0} << pair{0x7eb0, 0x0}
                << pair{0x7ed0, 0x0} << pair{0x7ef0, 0x0}
                << pair{0x7f10, 0x0};
            break;
        }
        ECLStall(ecl, 0x7c5c);
        ecl << pair{0x7bc4, int16_t{41}} << pair{0x7be4, int16_t{41}}
            << pair{0x7ba4, int16_t{1700}} << pair{0x7bb4, int16_t{1700}}
            << pair{0x7f64, int16_t{0}} << pair{0x8050, int16_t{0}}
            << pair{0x8068, int16_t{0}} << pair{0x8074, 0x0}
            << pair{0x8084, 0x0} << pair{0x80a4, 0x0}
            << pair{0x80c4, 0x0} << pair{0x80e4, 0x0}
            << pair{0x8104, 0x0};
        break;

    case TH06NC_ST5_MID1:
        ECLWarp(0xcc8);
        if (!dialogue)
            ecl << pair{0x7948, uint16_t{13}};
        break;
    case TH06NC_ST5_MID2:
        ECLWarp(0xcc8);
        ecl << pair{0x7944, int16_t{0}};
        ECLSetHealth(ecl, 0x1534, 0x1e, 0x2c5);
        ECLStall(ecl, 0x1544);
        break;
    case TH06NC_ST5_BOSS1:
    case TH06NC_ST5_BOSS2:
    case TH06NC_ST5_BOSS3:
    case TH06NC_ST5_BOSS4:
    case TH06NC_ST5_BOSS5:
    case TH06NC_ST5_BOSS6:
        ECLWarp(0x1db4);
        if (section == TH06NC_ST5_BOSS1 && dialogue)
            break;
        ecl << pair{0x8b1c, int16_t{0}} << pair{0x24a4, 0x0}
            << pair{0x24c4, 0x0} << pair{0x24e4, 0x0}
            << pair{0x2504, 0x0} << pair{0x2524, 0x0}
            << pair{0x23f4, int16_t{0}};
        if (section == TH06NC_ST5_BOSS1)
            break;
        if (section == TH06NC_ST5_BOSS2) {
            ECLSetTime(ecl, 0x2524, 0, 0);
            ECLStall(ecl, 0x2534);
            break;
        }
        if (section == TH06NC_ST5_BOSS3 || section == TH06NC_ST5_BOSS4) {
            // nc_ecl inserted anm_set_main(148) immediately before this
            // call.  Its time must move with the call or the interpreter
            // waits at time 100 and never reaches the back-dated call.
            ecl << pair{0x2538, 0x0} << pair{0x2548, 0x0}
                << pair{0x2554, 0x24}
                << pair{0x3984, int16_t{0}} << pair{0x3a34, int16_t{0}};
            if (section == TH06NC_ST5_BOSS4) {
                ECLSetTime(ecl, 0x3ac4, 0, 0);
                ECLStall(ecl, 0x3ad4);
            }
            break;
        }
        ecl << pair{0x2538, 0x1e} << pair{0x2548, 0x1e}
            << pair{0x2554, 0x2b}
            << pair{0x490c, int16_t{0}} << pair{0x49bc, int16_t{0}};
        if (section == TH06NC_ST5_BOSS6) {
            ecl << pair{0x2538, 0x0} << pair{0x2548, 0x0};
            ECLSetTime(ecl, 0x4a2c, 0, 0);
            ECLStall(ecl, 0x4a3c);
            // Sub62 movement-loop timing. Fast makes the unconditional jump
            // occur at the earlier boundary; Slow delays the conditional
            // branch to the later boundary. Default preserves both values.
            if (stage5Boss6Mode == 1)
            {
                ecl << pair{0x6794, 114}; // jump(154, Sub62_512)
            }
            else if (stage5Boss6Mode == 2)
            {
                ecl << pair{0x6770, 154}; // jump_geq(114, Sub62_496)
                ecl << pair{ 0x679C, 154 }; // jump(154, Sub62_512)
            }
        }
        break;

    case TH06NC_ST6_MID1:
        ECLWarp(0x9bd);
        if (!dialogue) {
            ecl << pair{0x9584, int16_t{0}} << pair{0xa8c, 0x1};
        }
        break;
    case TH06NC_ST6_MID2:
        ECLWarp(0x9bd);
        ecl << pair{0x9584, int16_t{0}}
            // nc_ecl inserted anm_set_main(139) at time 20 immediately
            // before the classic time-30 interactable instruction. Both
            // must be back-dated or the ECL time order becomes 20 -> 0.
            << pair{0xdd0, 0x0} << pair{0xde0, 0x0};
        {
            const auto* character = ResolveGameAddress<uint8_t>(GameAddress::CurrentCharacter);
            const auto* shotType = ResolveGameAddress<uint8_t>(GameAddress::CurrentShotType);
            const int shot = character && shotType ? *character * 2 + *shotType : 2;
            const int health = shot == 0 ? 749 : shot == 1 ? 999 : 1099;
            ECLSetHealth(ecl, 0xdf0, 0, health);
        }
        ECLStall(ecl, 0xe00);
        break;
    case TH06NC_ST6_BOSS1:
        if (dialogue) {
            ECLWarp(0xc18);
            break;
        }
        [[fallthrough]];
    case TH06NC_ST6_BOSS2:
    case TH06NC_ST6_BOSS3:
    case TH06NC_ST6_BOSS4:
    case TH06NC_ST6_BOSS5:
    case TH06NC_ST6_BOSS6:
    case TH06NC_ST6_BOSS7:
    case TH06NC_ST6_BOSS8:
    case TH06NC_ST6_BOSS9:
        ECLWarp(0xc1a);
        ecl << pair{0x1834, 0x0} << pair{0x1854, 0x0}
            << pair{0x1874, 0x0} << pair{0x1894, 0x0}
            << pair{0x1784, int16_t{0}};
        if (section == TH06NC_ST6_BOSS1)
            break;
        if (section == TH06NC_ST6_BOSS2) {
            ECLSetTime(ecl, 0x18b4, 0, 0);
            ECLStall(ecl, 0x18c4);
            break;
        }
        ecl << pair{0x18b4, 0x0};
        if (section == TH06NC_ST6_BOSS3 || section == TH06NC_ST6_BOSS4) {
            ecl << pair{0x18c8, 0x0} << pair{0x18d4, 0x15}
                << pair{0x1d3c, int16_t{0}} << pair{0x1e04, int16_t{0}};
            if (section == TH06NC_ST6_BOSS4) {
                ECLSetTime(ecl, 0x1eb8, 0, 0);
                ECLStall(ecl, 0x1ec8);
            }
            break;
        }
        if (section == TH06NC_ST6_BOSS5 || section == TH06NC_ST6_BOSS6) {
            ecl << pair{0x18c8, 0x1e} << pair{0x18d4, 0x19}
                << pair{0x2b20, int16_t{0}} << pair{0x2be8, int16_t{0}};
            if (section == TH06NC_ST6_BOSS6) {
                ecl << pair{0x18c8, 0x0};
                ECLSetTime(ecl, 0x2c78, 0, 0);
                ECLStall(ecl, 0x2c88);
            }
            break;
        }
        ecl << pair{0x18c8, 0x0};
        if (section == TH06NC_ST6_BOSS7 || section == TH06NC_ST6_BOSS8) {
            ecl << pair{0x18d4, 0x1c} << pair{0x2fe4, int16_t{0}}
                << pair{0x30ac, int16_t{0}};
            if (section == TH06NC_ST6_BOSS8) {
                ECLSetTime(ecl, 0x3154, 0, 0);
                ECLStall(ecl, 0x3164);
            }
            break;
        }
        ecl << pair{0x18e0, 0x0} << pair{0x18d4, 0x3d}
            << pair{0x18d0, int16_t{0x0300}} << pair{0x18e4, int16_t{0x23}}
            << pair{0x18e8, int16_t{0x0c00}} << pair{0x18ec, 0x40}
            << pair{0x6724, int16_t{0}} << pair{0x6e28, int16_t{0}}
            << pair{0x17d0, 0xffffffff};
        break;

    case TH06NC_ST7_MID1:
        ECLWarp(0x1220);
        if (!dialogue)
            ecl << pair{0x11800, int16_t{0}};
        break;
    case TH06NC_ST7_MID2:
        ECLWarp(0x1220);
        ecl << pair{0x11800, int16_t{0}} << pair{0x1c40, 0x12}
            << pair{0x1d58, int16_t{0}};
        break;
    case TH06NC_ST7_MID3:
        ECLWarp(0x1220);
        ecl << pair{0x11800, int16_t{0}} << pair{0x1c40, 0x13}
            << pair{0x1ea8, int16_t{0}};
        break;
    case TH06NC_ST7_BOSS1:
        if (dialogue)
            ECLWarp(0x212d);
        else
            s7b_n1();
        break;
    case TH06NC_ST7_BOSS2:
        s7b_n1();
        ECLSetTime(ecl, 0x36be, 0, 0);
        break;
    case TH06NC_ST7_BOSS3:
    case TH06NC_ST7_BOSS4:
    case TH06NC_ST7_BOSS5:
    case TH06NC_ST7_BOSS6:
    case TH06NC_ST7_BOSS7:
    case TH06NC_ST7_BOSS8:
    case TH06NC_ST7_BOSS9:
    case TH06NC_ST7_BOSS10:
    case TH06NC_ST7_BOSS11:
    case TH06NC_ST7_BOSS12:
    case TH06NC_ST7_BOSS13:
    case TH06NC_ST7_BOSS14:
    case TH06NC_ST7_BOSS15:
    case TH06NC_ST7_BOSS16:
    case TH06NC_ST7_BOSS17:
    case TH06NC_ST7_BOSS18:
        s7b_n1();
        s7b_call();
        switch (section) {
        case TH06NC_ST7_BOSS3:
        case TH06NC_ST7_BOSS4:
            ecl << pair{0x36ca, 0x2b} << pair{0x44ec, int16_t{0}};
            if (section == TH06NC_ST7_BOSS4) {
                ecl << pair{0x44d8, 0x0} << pair{0x44e8, 0x0};
                ECLSetTime(ecl, 0x44f8, 0, 0);
                ECLStall(ecl, 0x4508);
            }
            break;
        case TH06NC_ST7_BOSS5:
        case TH06NC_ST7_BOSS6:
            ecl << pair{0x36ca, 0x30} << pair{0x506a, int16_t{0}};
            if (section == TH06NC_ST7_BOSS6) {
                ecl << pair{0x5056, 0x0} << pair{0x5066, 0x0};
                ECLSetTime(ecl, 0x5076, 0, 0);
                ECLStall(ecl, 0x5086);
            }
            break;
        case TH06NC_ST7_BOSS7:
        case TH06NC_ST7_BOSS8:
            ecl << pair{0x36ca, 0x37} << pair{0x5e40, int16_t{0}};
            if (section == TH06NC_ST7_BOSS8) {
                ecl << pair{0x5e2c, 0x0} << pair{0x5e3c, 0x0};
                ECLSetTime(ecl, 0x5e4c, 0, 0);
                ECLStall(ecl, 0x5e5c);
            }
            break;
        case TH06NC_ST7_BOSS9:
        case TH06NC_ST7_BOSS10:
            ecl << pair{0x36ca, 0x3d} << pair{0x6882, int16_t{0}};
            if (section == TH06NC_ST7_BOSS10) {
                ecl << pair{0x686e, 0x0} << pair{0x687e, 0x0};
                ECLSetTime(ecl, 0x688e, 0, 0);
                ECLStall(ecl, 0x689e);
            }
            break;
        case TH06NC_ST7_BOSS11:
        case TH06NC_ST7_BOSS12:
            ecl << pair{0x36ca, 0x41} << pair{0x7068, int16_t{0}};
            if (section == TH06NC_ST7_BOSS12) {
                ecl << pair{0x7054, 0x0} << pair{0x7064, 0x0};
                ECLSetTime(ecl, 0x7074, 0, 0);
                ECLStall(ecl, 0x7084);
            }
            break;
        case TH06NC_ST7_BOSS13:
        case TH06NC_ST7_BOSS14:
            ecl << pair{0x36ca, 0x47} << pair{0x7e62, int16_t{0}};
            if (section == TH06NC_ST7_BOSS14) {
                ecl << pair{0x7e4e, 0x0} << pair{0x7e5e, 0x0};
                ECLSetTime(ecl, 0x7e6e, 0, 0);
                ECLStall(ecl, 0x7e7e);
            }
            break;
        case TH06NC_ST7_BOSS15:
        case TH06NC_ST7_BOSS16:
            ecl << pair{0x36ca, 0x4c} << pair{0x8b2c, int16_t{0}};
            if (section == TH06NC_ST7_BOSS16) {
                ecl << pair{0x8b18, 0x0} << pair{0x8b28, 0x0};
                ECLSetTime(ecl, 0x8b38, 0, 0);
                ECLStall(ecl, 0x8b48);
            }
            break;
        case TH06NC_ST7_BOSS17:
            ecl << pair{0x36ca, 0x51} << pair{0x9a9a, int16_t{0}}
                << pair{0x9a86, 0x0} << pair{0x9a96, 0x0}
                << pair{0x9aa6, 0x0} << pair{0x9ab2, 0x0}
                << pair{0x9aca, 0x0} << pair{0x9af6, 0x0}
                << pair{0x9b02, 0x0} << pair{0x9b12, 0x0};
            break;
        case TH06NC_ST7_BOSS18:
            ecl << pair{0x36ca, 0x5b} << pair{0xc590, int16_t{0}}
                << pair{0xc58c, 0x0} << pair{0xc59c, 0x0}
                << pair{0xc5bc, 0x0} << pair{0xc5dc, 0x0}
                << pair{0xc5fc, 0x0} << pair{0xc61c, 0x0}
                << pair{0xc63c, 0x0} << pair{0xc648, 0x0}
                << pair{0xc660, 0x0} << pair{0xc68c, 0x0}
                << pair{0xc698, 0x0} << pair{0xc6a8, 0x0};
            // Classic 0xbe90 (enemy_flag_can_take_damage(1)) was removed
            // from nc_ecl Sub91, so intentionally no corresponding write.
            break;
        default:
            break;
        }
        break;
    case TH06NC_ST7_BOSS19:
        // Timeline time 8500 creates the nc-only Sub97 boss. Its interrupt
        // enters Sub101, which starts EXEX1 through Sub102/Sub103.
        ECLWarp(0x2134);
        ecl << pair{0xccda, 0x65};
        break;
    case TH06NC_ST7_BOSS20:
        // Redirect Sub97's interrupt to the second-phase initializer Sub106.
        ECLWarp(0x2134);
        ecl << pair{0xccda, 0x6a}
            << pair{0xde1c, 0x0} << pair{0xde2c, 0x0}
            << pair{0xde3c, 0x0}
            // Preserve the original 0x14-byte jump instruction and postpone
            // only its timestamp, preventing repeated Sub107 calls.
            << pair{0xde54, 0x99999};
        break;
    case TH06NC_ST7_BOSS21:
        // Redirect Sub97's interrupt to the third-phase initializer Sub112.
        ECLWarp(0x2134);
        ecl << pair{ 0xccda, 0x70 }
            << pair{ 0xe8d2, 0x0010007c }
            << pair{ 0xe8da, 0x3 }
            << pair{ 0xe8f2, 0x0 }
            << pair{ 0xe902, 0x0 }
        << pair{ 0xe91a, 0x99999 };
        break;
    default:
        return false;
    }
    return ecl.valid() && timelineTime >= 0;
}

bool IsReadableWritable(const void* address, size_t size)
{
    uintptr_t cursor = reinterpret_cast<uintptr_t>(address);
    const uintptr_t end = cursor + size;
    if (end < cursor)
        return false;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &memory, sizeof(memory)) ||
            memory.State != MEM_COMMIT ||
            (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
            !(memory.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            return false;
        const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(memory.BaseAddress) +
            memory.RegionSize;
        if (regionEnd <= cursor)
            return false;
        cursor = std::min(regionEnd, end);
    }
    return true;
}

bool ValidateLoadedEcl(std::byte* ecl, int stage)
{
    if (!ecl || stage < 1 || stage > 7)
        return false;
    const auto& identity = kStageEclIdentity[stage - 1];
    if (!IsReadableWritable(ecl, identity.size))
        return false;
    return *reinterpret_cast<const uint16_t*>(ecl) == identity.subCount &&
        *reinterpret_cast<const uint32_t*>(ecl + 4) == identity.timelineOffset;
}

int __fastcall HookedTimelineUpdate(void* enemyManager)
{
    const PracticeJumpRequest request = std::exchange(
        g_practiceJumpRuntime.pending, PracticeJumpRequest{});
    const RequestKind kind = request.kind;
    if (kind != RequestKind::None && enemyManager) {
        const int stage = request.stage;
        int timelineTime = request.timelineTime;
        bool ready = kind == RequestKind::Stage;
        if (kind == RequestKind::Stage4Books) {
            auto** eclSlot = ResolveGameAddress<std::byte*>(GameAddress::LoadedEclFile);
            std::byte* eclData = eclSlot ? *eclSlot : nullptr;
            if (ValidateLoadedEcl(eclData, 4)) {
                EclWriter writer(eclData, kStageEclIdentity[3].size);
                constexpr size_t kFirstBookArguments = 0xf2f8;
                constexpr size_t kBookInstructionSize = 0x1c;
                const unsigned fixedMask = request.bookFixedMask;
                for (int book = 0; book < 6; ++book) {
                    if ((fixedMask & (1u << book)) == 0)
                        continue;
                    const size_t offset = kFirstBookArguments +
                        kBookInstructionSize * book;
                    // enemy_create_random stores X/Y/Z as three consecutive
                    // floats. A fixed book must replace the complete -999.0f
                    // random-X sentinel, not merely its low 16 bits.
                    writer << pair{offset, static_cast<float>(
                            request.bookX[book] + 192)}
                        << pair{offset + 4, static_cast<float>(
                            request.bookY[book])};
                }
                ready = writer.valid();
            } else {
                g_practiceJumpRuntime.pending = request;
            }
        } else if (kind == RequestKind::Boss) {
            auto** eclSlot = ResolveGameAddress<std::byte*>(GameAddress::LoadedEclFile);
            std::byte* eclData = eclSlot ? *eclSlot : nullptr;
            if (ValidateLoadedEcl(eclData, stage)) {
                EclWriter writer(eclData, kStageEclIdentity[stage - 1].size);
                timelineTime = -1;
                ready = ApplyBossPatch(writer,
                    request.jump, request.dialogue, request.fakeShot,
                    request.stage5Boss6Mode, request.bookFixedMask, request.bookX.data(), request.bookY.data(),
                    timelineTime);
            } else {
                // A transition can reach this hook once while the old ECL is
                // still being torn down. Keep the request for the first update
                // that exposes the selected stage's validated ECL buffer.
                g_practiceJumpRuntime.pending = request;
            }
        }
        if (ready)
            *reinterpret_cast<int*>(static_cast<std::byte*>(enemyManager) +
                kEnemyManagerTimelineTime) = timelineTime;
    }
    return g_originalTimelineUpdate(enemyManager);
}

void __fastcall HookedFinalSpellRage(void* enemy, void* instruction)
{
    if (!g_originalFinalSpellRage)
        return;
    if (!enemy || !IsRaging495PracticeActive()) {
        g_originalFinalSpellRage(enemy, instruction);
        return;
    }

    // The stock callback selects QED's phase from its enemy-local age. Feed
    // it the final-phase threshold for this call only; ECL continues to own
    // and advance the real timer after the phase has been selected.
    auto* age = reinterpret_cast<int*>(static_cast<std::byte*>(enemy) + 4);
    const int originalAge = *age;
    *age = 7200;
    g_originalFinalSpellRage(enemy, instruction);
    *age = originalAge;
}

void* AllocateNearAddress(void* target, size_t size)
{
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const uintptr_t targetAddress = reinterpret_cast<uintptr_t>(target);
    const uintptr_t minimum = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
    const uintptr_t maximum = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
    const uintptr_t reach = static_cast<uintptr_t>(std::numeric_limits<int32_t>::max()) - 0x10000;
    const uintptr_t low = targetAddress > reach ? std::max(minimum, targetAddress - reach) : minimum;
    const uintptr_t high = std::min(maximum, targetAddress + reach);
    const uintptr_t granularity = info.dwAllocationGranularity;
    uintptr_t cursor = low;
    while (cursor < high) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &memory, sizeof(memory)))
            break;
        const uintptr_t base = reinterpret_cast<uintptr_t>(memory.BaseAddress);
        const uintptr_t end = base + memory.RegionSize;
        if (memory.State == MEM_FREE) {
            uintptr_t candidate = std::max(cursor, base);
            candidate = (candidate + granularity - 1) & ~(granularity - 1);
            if (candidate <= high && candidate <= end && size <= end - candidate) {
                if (void* block = VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE))
                    return block;
            }
        }
        if (end <= cursor)
            break;
        cursor = end;
    }
    return nullptr;
}

void WriteAbsoluteJump(unsigned char* destination, const void* target)
{
    destination[0] = 0xff;
    destination[1] = 0x25;
    *reinterpret_cast<uint32_t*>(destination + 2) = 0;
    *reinterpret_cast<uintptr_t*>(destination + 6) = reinterpret_cast<uintptr_t>(target);
}

bool InstallFinalSpellRageHook()
{
    auto* target = ResolveGameAddress<unsigned char>(GameAddress::FinalSpellRage);
    if (!target || std::memcmp(target, kExpectedFinalSpellRagePrologue,
            kFinalSpellRagePrologueSize) != 0)
        return false;

    constexpr size_t trampolineSize = kFinalSpellRagePrologueSize + 14;
    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(nullptr,
        trampolineSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!trampoline)
        return false;
    std::memcpy(trampoline, target, kFinalSpellRagePrologueSize);
    WriteAbsoluteJump(trampoline + kFinalSpellRagePrologueSize,
        target + kFinalSpellRagePrologueSize);

    DWORD trampolineProtection = 0;
    if (!VirtualProtect(trampoline, trampolineSize, PAGE_EXECUTE_READ,
            &trampolineProtection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    DWORD targetProtection = 0;
    if (!VirtualProtect(target, kFinalSpellRagePrologueSize,
            PAGE_EXECUTE_READWRITE, &targetProtection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    g_originalFinalSpellRage =
        reinterpret_cast<FinalSpellRageFn>(trampoline);
    WriteAbsoluteJump(target, reinterpret_cast<void*>(HookedFinalSpellRage));
    std::memset(target + 14, 0x90, kFinalSpellRagePrologueSize - 14);
    FlushInstructionCache(GetCurrentProcess(), target,
        kFinalSpellRagePrologueSize);
    DWORD ignored = 0;
    VirtualProtect(target, kFinalSpellRagePrologueSize,
        targetProtection, &ignored);
    return true;
}

} // namespace

std::map<int, std::pair<std::vector<int>, std::vector<int>>> kStageChapterTimes = {
       {1, {{100 - 60, 594 - 60, 1174 - 60, 1554 - 60},{2312 - 60, 4372 - 60} }},
       {2, {{240 - 60, 894 - 60},{ 3498 - 60, 4533 - 60} }},
       {3, {{200 - 60, 910 - 60, 1530 - 60, 2622 - 60}, {3536 - 40, 3898 - 60, 4190 - 60, 5054 - 60, 5334 - 60, 5614 - 60} }},
       {4, {{330 - 60, 1430 - 60, 2304 - 60, 3088 - 60}, {4858 - 50, 5698 - 60, 7380 - 60, 8300 - 60, 9730 - 60} }},
       {5, {{310 - 60, 942 - 60, 2252 - 60}, {3774 - 60, 6734 - 60} }},
       {6, {{340 - 60, 1459 - 60,2493 - 60},{}}},
       {7, {{340 - 60, 1260 - 60, 2560 - 60, 3640 - 60}, {4763 - 60, 5893 - 60, 7273 - 60} }},
};

const std::map<int, std::pair<std::vector<int>, std::vector<int>>>& StageChapterTimes()
{
    return kStageChapterTimes;
}

int GetChapterTime(int stage, int chapter)
{
    std::pair<std::vector<int>, std::vector<int>>& chapters = kStageChapterTimes[stage];
    if (chapter > chapters.first.size()){
        return chapters.second[chapter - chapters.first.size() - 1];
    }
    return chapters.first[chapter - 1];
}

const std::vector<BossJump>& BossJumps()
{
    return kBossJumps;
}

void QueueStagePracticeJump(int stage, int timelineTime)
{
    g_practiceJumpRuntime.pending = {
        .kind = RequestKind::Stage,
        .stage = stage,
        .timelineTime = timelineTime,
    };
}

void QueueStage4BooksPracticeJump(int timelineTime, unsigned fixedMask,
    const int* x, const int* y)
{
    PracticeJumpRequest request{
        .kind = RequestKind::Stage4Books,
        .stage = 4,
        .timelineTime = timelineTime,
        .bookFixedMask = fixedMask,
    };
    for (int book = 0; book < 6; ++book) {
        request.bookX[book] = x[book];
        request.bookY[book] = y[book];
    }
    g_practiceJumpRuntime.pending = request;
}

void QueueBossPracticeJump(int stage, JumpEnum jump, bool dialogue,
    int fakeShot, int stage5Boss6Mode, unsigned fixedMask, const int* x, const int* y)
{
    PracticeJumpRequest request{
        .kind = RequestKind::Boss,
        .stage = stage,
        .jump = jump,
        .dialogue = dialogue,
        .fakeShot = fakeShot,
        .stage5Boss6Mode = stage5Boss6Mode,
        .bookFixedMask = fixedMask,
    };
    if(x && y)
        for (int book = 0; book < 6; ++book) {
            request.bookX[book] = x[book];
            request.bookY[book] = y[book];
        }
    g_practiceJumpRuntime.pending = request;
}

void ClearQueuedPracticeJump()
{
    g_practiceJumpRuntime.pending = {};
}

bool InstallPracticeJumpHook()
{
    if (g_practiceJumpRuntime.hookStatus == HookStatus::Installed)
        return true;
    auto* target = ResolveGameAddress<unsigned char>(GameAddress::EnemyTimelineUpdate);
    auto* rageTarget = ResolveGameAddress<unsigned char>(GameAddress::FinalSpellRage);
    if (!target || !rageTarget ||
        std::memcmp(target, kExpectedTimelineUpdatePrologue,
            kTimelineUpdatePrologueSize) != 0 ||
        std::memcmp(rageTarget, kExpectedFinalSpellRagePrologue,
            kFinalSpellRagePrologueSize) != 0) {
        g_practiceJumpRuntime.hookStatus = HookStatus::UnsupportedExecutable;
        return false;
    }
    auto* block = static_cast<unsigned char*>(AllocateNearAddress(target, 64));
    if (!block) {
        g_practiceJumpRuntime.hookStatus = HookStatus::AllocationFailed;
        return false;
    }
    if (!InstallFinalSpellRageHook()) {
        VirtualFree(block, 0, MEM_RELEASE);
        g_practiceJumpRuntime.hookStatus = HookStatus::PatchFailed;
        return false;
    }
    unsigned char* relay = block;
    unsigned char* trampoline = block + 16;
    WriteAbsoluteJump(relay, reinterpret_cast<void*>(HookedTimelineUpdate));
    std::memcpy(trampoline, target, kTimelineUpdatePrologueSize);
    WriteAbsoluteJump(trampoline + kTimelineUpdatePrologueSize,
        target + kTimelineUpdatePrologueSize);
    DWORD oldBlockProtection = 0;
    if (!VirtualProtect(block, 64, PAGE_EXECUTE_READ, &oldBlockProtection)) {
        VirtualFree(block, 0, MEM_RELEASE);
        g_practiceJumpRuntime.hookStatus = HookStatus::PatchFailed;
        return false;
    }
    const intptr_t relative = reinterpret_cast<intptr_t>(relay) -
        (reinterpret_cast<intptr_t>(target) + 5);
    if (relative < std::numeric_limits<int32_t>::min() ||
        relative > std::numeric_limits<int32_t>::max()) {
        VirtualFree(block, 0, MEM_RELEASE);
        g_practiceJumpRuntime.hookStatus = HookStatus::PatchFailed;
        return false;
    }
    DWORD oldTargetProtection = 0;
    if (!VirtualProtect(target, kTimelineUpdatePrologueSize,
            PAGE_EXECUTE_READWRITE, &oldTargetProtection)) {
        VirtualFree(block, 0, MEM_RELEASE);
        g_practiceJumpRuntime.hookStatus = HookStatus::PatchFailed;
        return false;
    }
    g_originalTimelineUpdate = reinterpret_cast<TimelineUpdateFn>(trampoline);
    target[0] = 0xe9;
    *reinterpret_cast<int32_t*>(target + 1) = static_cast<int32_t>(relative);
    std::memset(target + 5, 0x90, kTimelineUpdatePrologueSize - 5);
    FlushInstructionCache(GetCurrentProcess(), target, kTimelineUpdatePrologueSize);
    DWORD ignored = 0;
    VirtualProtect(target, kTimelineUpdatePrologueSize, oldTargetProtection, &ignored);
    g_practiceJumpRuntime.hookStatus = HookStatus::Installed;
    return true;
}

const char* PracticeJumpHookStatus()
{
    switch (g_practiceJumpRuntime.hookStatus) {
    case HookStatus::Installed: return S(StatusActive);
    case HookStatus::UnsupportedExecutable: return S(StatusUnsupportedExecutable);
    case HookStatus::AllocationFailed: return S(StatusAllocationFailed);
    case HookStatus::PatchFailed: return S(StatusPatchFailed);
    default: return S(StatusNotInstalled);
    }
}
