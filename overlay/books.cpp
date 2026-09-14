#include "books.h"

#include "game_addresses.h"

#include <windows.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

constexpr ptrdiff_t kTimelineTimeOffset = 0x10C0BC;
constexpr ptrdiff_t kTimelineInstructionOffset = 0x10C128;
constexpr uint16_t kEnemyCreateRandomOpcode = 4;
constexpr uint16_t kBooksStartTime = 3378;
constexpr int kStage4Index = 4;

struct BooksRuntime {
    BooksInfo info{};
    int lastTimelineTime = -1;
    bool configurationLoaded = false;
    std::wstring configurationPath;
};

BooksRuntime g_books{};

constexpr const wchar_t* kShotNames[4] = {
    L"ReimuA", L"ReimuB", L"MarisaA", L"MarisaB"};
constexpr const wchar_t* kDifficultyNames[4] = {
    L"E", L"N", L"H", L"L"};

bool IsValidGroup(int shot, int difficulty)
{
    return shot >= 0 && shot < 4 && difficulty >= 0 && difficulty < 4;
}

void MakeCounterKey(wchar_t* output, size_t capacity, int shot,
    int difficulty, const wchar_t* counter)
{
    _snwprintf_s(output, capacity, _TRUNCATE, L"%ls_%ls_%ls",
        kShotNames[shot], kDifficultyNames[difficulty], counter);
}

std::wstring InputConfigPath()
{
    wchar_t appData[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData,
        static_cast<DWORD>(_countof(appData)));
    if (length == 0 || length >= _countof(appData))
        return {};

    const std::wstring publisher =
        std::wstring(appData, length) + L"\\shanghaialice";
    const std::wstring game = publisher + L"\\th06nc";
    CreateDirectoryW(publisher.c_str(), nullptr);
    CreateDirectoryW(game.c_str(), nullptr);
    return game + L"\\input.ini";
}

void SaveBooksCounters(int shot, int difficulty)
{
    if (g_books.configurationPath.empty() ||
        !IsValidGroup(shot, difficulty))
        return;
    wchar_t key[64]{};
    wchar_t value[32]{};
    MakeCounterKey(key, _countof(key), shot, difficulty, L"Attempts");
    _snwprintf_s(value, _countof(value), _TRUNCATE, L"%d",
        g_books.info.attemptCount[shot][difficulty]);
    WritePrivateProfileStringW(L"Books", key, value,
        g_books.configurationPath.c_str());
    MakeCounterKey(key, _countof(key), shot, difficulty, L"Passes");
    _snwprintf_s(value, _countof(value), _TRUNCATE, L"%d",
        g_books.info.passCount[shot][difficulty]);
    WritePrivateProfileStringW(L"Books", key, value,
        g_books.configurationPath.c_str());
}

void SaveAllBooksCounters()
{
    for (int shot = 0; shot < 4; ++shot)
        for (int difficulty = 0; difficulty < 4; ++difficulty)
            SaveBooksCounters(shot, difficulty);
}

bool IsInstruction(const std::byte* instruction, uint16_t opcode,
    uint16_t time)
{
    return instruction &&
        *reinterpret_cast<const uint16_t*>(instruction) == time &&
        *reinterpret_cast<const uint16_t*>(instruction + 4) == opcode;
}

void BeginBooksAttempt()
{
    if (g_books.info.isInBooks)
        return;
    const auto* misses = ResolveGameAddress<int32_t>(GameAddress::MissCount);
    const auto* bombs = ResolveGameAddress<int32_t>(GameAddress::BombUseCount);
    const auto* character = ResolveGameAddress<uint8_t>(
        GameAddress::CurrentCharacter);
    const auto* shotType = ResolveGameAddress<uint8_t>(
        GameAddress::CurrentShotType);
    const auto* difficulty = ResolveGameAddress<int>(
        GameAddress::CurrentDifficulty);
    if (!misses || !bombs || !character || !shotType || !difficulty)
        return;
    const int shot = static_cast<int>(*shotType) +
        static_cast<int>(*character) * 2;
    if (!IsValidGroup(shot, *difficulty))
        return;
    g_books.info.isInBooks = true;
    g_books.info.lastMiss = *misses;
    g_books.info.lastBomb = *bombs;
    g_books.info.activeShot = shot;
    g_books.info.activeDifficulty = *difficulty;
    int& attempts = g_books.info.attemptCount[shot][*difficulty];
    if (attempts < INT_MAX)
        ++attempts;
    SaveBooksCounters(shot, *difficulty);
}

void FinishBooksAttempt()
{
    if (!g_books.info.isInBooks)
        return;
    const int shot = g_books.info.activeShot;
    const int difficulty = g_books.info.activeDifficulty;
    const auto* misses = ResolveGameAddress<int32_t>(GameAddress::MissCount);
    const auto* bombs = ResolveGameAddress<int32_t>(GameAddress::BombUseCount);
    const bool passed = misses && bombs &&
        *misses <= g_books.info.lastMiss &&
        *bombs <= g_books.info.lastBomb;
    g_books.info.isInBooks = false;
    g_books.info.activeShot = -1;
    g_books.info.activeDifficulty = -1;
    if (passed && IsValidGroup(shot, difficulty)) {
        int& passes = g_books.info.passCount[shot][difficulty];
        if (passes < INT_MAX)
            ++passes;
        SaveBooksCounters(shot, difficulty);
    }
}

} // namespace

void InitializeBooksTracking()
{
    if (g_books.configurationLoaded)
        return;
    g_books.configurationLoaded = true;
    g_books.configurationPath = InputConfigPath();
    if (g_books.configurationPath.empty())
        return;
    wchar_t key[64]{};
    for (int shot = 0; shot < 4; ++shot) {
        for (int difficulty = 0; difficulty < 4; ++difficulty) {
            MakeCounterKey(key, _countof(key), shot, difficulty,
                L"Attempts");
            const int attempts = static_cast<int>(GetPrivateProfileIntW(
                L"Books", key, 0, g_books.configurationPath.c_str()));
            MakeCounterKey(key, _countof(key), shot, difficulty, L"Passes");
            const int passes = static_cast<int>(GetPrivateProfileIntW(
                L"Books", key, 0, g_books.configurationPath.c_str()));
            g_books.info.attemptCount[shot][difficulty] =
                std::max(0, attempts);
            g_books.info.passCount[shot][difficulty] = std::clamp(passes, 0,
                g_books.info.attemptCount[shot][difficulty]);
        }
    }
    // Materialize all 32 editable values, including groups not played yet.
    SaveAllBooksCounters();
}

void ObserveBooksTimeline(void* enemyManager)
{
    if (!enemyManager)
        return;
    const auto* stage = ResolveGameAddress<int>(GameAddress::CurrentStage);
    auto* manager = static_cast<std::byte*>(enemyManager);
    const int timelineTime = *reinterpret_cast<const int*>(
        manager + kTimelineTimeOffset);
    if (!stage || *stage != kStage4Index) {
        ResetBooksAttempt();
        return;
    }

    if (g_books.lastTimelineTime >= 0 &&
        timelineTime < g_books.lastTimelineTime)
        ResetBooksAttempt();

    g_books.lastTimelineTime = timelineTime;
    const auto* instruction = *reinterpret_cast<std::byte* const*>(
        manager + kTimelineInstructionOffset);

    if (IsInstruction(instruction, kEnemyCreateRandomOpcode, kBooksStartTime)) {
        BeginBooksAttempt();
    }  else if (timelineTime == 4058) {
        FinishBooksAttempt();
    }
}

void ResetBooksAttempt()
{
    g_books.info.isInBooks = false;
    g_books.info.activeShot = -1;
    g_books.info.activeDifficulty = -1;
    g_books.lastTimelineTime = -1;
}

const BooksInfo& GetBooksInfo()
{
    return g_books.info;
}
