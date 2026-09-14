#include "spell_rate.h"

#include "game_addresses.h"
#include "locale.h"
#include "imgui.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

SpellRate allSpells[kSpellRateSpellCount][kSpellRateShotCount]{};

#define _E 0
#define _N 1
#define _H 2
#define _L 3
#define _X 4
SpellInfo spellInfo[kSpellRateSpellCount] = {
    {0, _H, TH06NC_ST1_MID2},
    {1, _L, TH06NC_ST1_MID2},

    {2, _N, TH06NC_ST1_BOSS2},
    {3, _H, TH06NC_ST1_BOSS2},
    {4, _L, TH06NC_ST1_BOSS2},

    {5, _E, TH06NC_ST1_BOSS4},
    {6, _N, TH06NC_ST1_BOSS4},
    {7, _H, TH06NC_ST1_BOSS4},
    {8, _L, TH06NC_ST1_BOSS4},

    {9,  _E, TH06NC_ST2_BOSS2},
    {10, _N, TH06NC_ST2_BOSS2},
    {11, _H, TH06NC_ST2_BOSS2},
    {12, _L, TH06NC_ST2_BOSS2},

    {13, _E, TH06NC_ST2_BOSS4},
    {14, _N, TH06NC_ST2_BOSS4},
    {15, _H, TH06NC_ST2_BOSS4},
    {16, _L, TH06NC_ST2_BOSS4},

    {17, _N, TH06NC_ST2_BOSS5},
    {18, _H, TH06NC_ST2_BOSS5},
    {19, _L, TH06NC_ST2_BOSS5},

    {20, _E, TH06NC_ST3_MID1},
    {21, _N, TH06NC_ST3_MID1},
    {22, _H, TH06NC_ST3_MID1},
    {23, _L, TH06NC_ST3_MID1},

    {24, _E, TH06NC_ST3_BOSS2},
    {25, _N, TH06NC_ST3_BOSS2},
    {26, _H, TH06NC_ST3_BOSS2},
    {27, _L, TH06NC_ST3_BOSS2},

    {28, _H, TH06NC_ST3_BOSS4},
    {29, _L, TH06NC_ST3_BOSS4},

    {30, _E, TH06NC_ST3_BOSS6},
    {31, _N, TH06NC_ST3_BOSS6},
    {32, _H, TH06NC_ST3_BOSS6},
    {33, _L, TH06NC_ST3_BOSS6},

    {34, _N, TH06NC_ST3_BOSS7},
    {35, _H, TH06NC_ST3_BOSS7},
    {36, _L, TH06NC_ST3_BOSS7},

    {37, _E, TH06NC_FIRE_1},
    {38, _N, TH06NC_FIRE_1},

    {39, _N, TH06NC_FIRE_2},
    {40, _H, TH06NC_FIRE_2},
    {41, _L, TH06NC_FIRE_2},

    {42, _H, TH06NC_FIRE_3},
    {43, _L, TH06NC_FIRE_3},

    {44, _E, TH06NC_WATER_1},
    {45, _N, TH06NC_WATER_1},
    {46, _H, TH06NC_WATER_1},
    {47, _L, TH06NC_WATER_1},

    {48, _E, TH06NC_WOOD_1},
    {49, _N, TH06NC_WOOD_1},

    {50, _N, TH06NC_WOOD_2},
    {51, _H, TH06NC_WOOD_2},
    {52, _L, TH06NC_WOOD_2},

    {53, _H, TH06NC_WOOD_3},
    {54, _L, TH06NC_WOOD_3},

    {55, _E, TH06NC_EARTH_1},
    {56, _N, TH06NC_EARTH_1},

    {57, _N, TH06NC_EARTH_2},
    {58, _H, TH06NC_EARTH_2},
    {59, _L, TH06NC_EARTH_2},

    {60, _H, TH06NC_EARTH_3},
    {61, _L, TH06NC_EARTH_3},

    {62, _N, TH06NC_METAL_1},
    {63, _H, TH06NC_METAL_1},
    {64, _L, TH06NC_METAL_1},

    {65, _E, TH06NC_FIRE_EARTH},
    {66, _N, TH06NC_FIRE_EARTH},
    {67, _H, TH06NC_FIRE_EARTH},
    {68, _L, TH06NC_FIRE_EARTH},

    {69, _E, TH06NC_WOOD_FIRE},
    {70, _N, TH06NC_WOOD_FIRE},
    {71, _H, TH06NC_WOOD_FIRE},
    {72, _L, TH06NC_WOOD_FIRE},

    {73, _E, TH06NC_WATER_WOOD },
    {74, _N, TH06NC_WATER_WOOD },
    {75, _H, TH06NC_WATER_WOOD },
    {76, _L, TH06NC_WATER_WOOD },

    {77, _N, TH06NC_METAL_WATER },
    {78, _H, TH06NC_METAL_WATER },
    {79, _L, TH06NC_METAL_WATER },

    {80, _E, TH06NC_EARTH_METAL },
    {81, _N, TH06NC_EARTH_METAL },
    {82, _H, TH06NC_EARTH_METAL },
    {83, _L, TH06NC_EARTH_METAL },

    {84, _E, TH06NC_ST5_MID2},
    {85, _N, TH06NC_ST5_MID2},
    {86, _H, TH06NC_ST5_MID2},
    {87, _L, TH06NC_ST5_MID2},

    {88, _E, TH06NC_ST5_BOSS2},
    {89, _N, TH06NC_ST5_BOSS2},
    {90, _H, TH06NC_ST5_BOSS2},
    {91, _L, TH06NC_ST5_BOSS2},

    {92, _E, TH06NC_ST5_BOSS4},
    {93, _N, TH06NC_ST5_BOSS4},
    {94, _H, TH06NC_ST5_BOSS4},
    {95, _L, TH06NC_ST5_BOSS4},

    {96, _E, TH06NC_ST5_BOSS6},
    {97, _N, TH06NC_ST5_BOSS6},
    {98, _H, TH06NC_ST5_BOSS6},
    {99, _L, TH06NC_ST5_BOSS6},

    {100, _N, TH06NC_ST6_MID2},
    {101, _H, TH06NC_ST6_MID2},
    {102, _L, TH06NC_ST6_MID2},

    {103, _N, TH06NC_ST6_BOSS2},
    {104, _H, TH06NC_ST6_BOSS2},
    {105, _L, TH06NC_ST6_BOSS2},

    {106, _N, TH06NC_ST6_BOSS4},
    {107, _H, TH06NC_ST6_BOSS4},
    {108, _L, TH06NC_ST6_BOSS4},

    {109, _N, TH06NC_ST6_BOSS6},
    {110, _H, TH06NC_ST6_BOSS6},
    {111, _L, TH06NC_ST6_BOSS6},

    {112, _N, TH06NC_ST6_BOSS8},
    {113, _H, TH06NC_ST6_BOSS8},
    {114, _L, TH06NC_ST6_BOSS8},

    {115, _N, TH06NC_ST6_BOSS9},
    {116, _H, TH06NC_ST6_BOSS9},
    {117, _L, TH06NC_ST6_BOSS9},

    {118, _X, TH06NC_ST7_MID1},
    {119, _X, TH06NC_ST7_MID2},
    {120, _X, TH06NC_ST7_MID3},
    {121, _X, TH06NC_ST7_BOSS2},
    {122, _X, TH06NC_ST7_BOSS4},
    {123, _X, TH06NC_ST7_BOSS6},
    {124, _X, TH06NC_ST7_BOSS8},
    {125, _X, TH06NC_ST7_BOSS10},
    {126, _X, TH06NC_ST7_BOSS12},
    {127, _X, TH06NC_ST7_BOSS14},
    {128, _X, TH06NC_ST7_BOSS16},
    {129, _X, TH06NC_ST7_BOSS17},
    {130, _X, TH06NC_ST7_BOSS18},
    {131, _X, TH06NC_ST7_BOSS19},
    {132, _X, TH06NC_ST7_BOSS20},
    {133, _X, TH06NC_ST7_BOSS21},

    {134, _E, TH06NC_BOOKS },
    {135, _N, TH06NC_BOOKS },
    {136, _H, TH06NC_BOOKS },
    {137, _L, TH06NC_BOOKS },

};

#undef _E
#undef _N
#undef _H
#undef _L
#undef _X

namespace {

constexpr int32_t kSpellCaptureFileVersion = 1;
constexpr ptrdiff_t kTimelineTimeOffset = 0x10C0BC;
constexpr ptrdiff_t kTimelineInstructionOffset = 0x10C128;
constexpr uint16_t kEnemyCreateRandomOpcode = 4;
constexpr uint16_t kBooksStartTime = 3378;
constexpr int kBooksEndTime = 4058;
constexpr int kStage4Index = 4;
constexpr size_t kNativeSpellRecordSize = 0x180;

enum class CallbackArgument {
    None,
    R14,
    Rbx,
};

struct SpellRateRuntime {
    BooksInfo books{};
    int lastTimelineTime = -1;
    int spellListDisplayId = -1;
    int resultDisplayId = -1;
    bool dataLoaded = false;
    bool hooksInstalled = false;
    std::wstring dataPath;
};

SpellRateRuntime g_spellRate{};

static_assert(sizeof(SpellRate) == sizeof(int32_t) * 2);
static_assert(std::is_trivially_copyable_v<SpellRate>);

std::wstring SpellCapturePath()
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
    return game + L"\\spell_capture.dat";
}

bool ReadExact(HANDLE file, void* destination, DWORD size)
{
    DWORD read = 0;
    return ReadFile(file, destination, size, &read, nullptr) && read == size;
}

bool WriteExact(HANDLE file, const void* source, DWORD size)
{
    DWORD written = 0;
    return WriteFile(file, source, size, &written, nullptr) && written == size;
}

void SaveSpellRates()
{
    if (!g_spellRate.dataLoaded || g_spellRate.dataPath.empty())
        return;
    const HANDLE file = CreateFileW(g_spellRate.dataPath.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    const bool written = WriteExact(file, &kSpellCaptureFileVersion,
        sizeof(kSpellCaptureFileVersion)) &&
        WriteExact(file, allSpells, sizeof(allSpells));
    if (written)
        FlushFileBuffers(file);
    CloseHandle(file);
}

void LoadLegacyBooksRates()
{
    if (g_spellRate.dataPath.empty())
        return;
    const size_t separator = g_spellRate.dataPath.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return;
    const std::wstring iniPath =
        g_spellRate.dataPath.substr(0, separator + 1) + L"input.ini";
    constexpr const wchar_t* shotNames[4] = {
        L"ReimuA", L"ReimuB", L"MarisaA", L"MarisaB"};
    constexpr const wchar_t* difficulties[4] = {L"E", L"N", L"H", L"L"};
    wchar_t key[64]{};
    for (int shot = 0; shot < 4; ++shot) {
        for (int difficulty = 0; difficulty < 4; ++difficulty) {
            _snwprintf_s(key, _countof(key), _TRUNCATE,
                L"%ls_%ls_Attempts", shotNames[shot], difficulties[difficulty]);
            const int attempts = static_cast<int>(GetPrivateProfileIntW(
                L"Books", key, 0, iniPath.c_str()));
            _snwprintf_s(key, _countof(key), _TRUNCATE,
                L"%ls_%ls_Passes", shotNames[shot], difficulties[difficulty]);
            const int captured = static_cast<int>(GetPrivateProfileIntW(
                L"Books", key, 0, iniPath.c_str()));
            SpellRate& rate = allSpells[kBooksSpellBase + difficulty][shot];
            rate.attempt = std::max(0, attempts);
            rate.captured = std::clamp(captured, 0, rate.attempt);
        }
    }
}

void LoadSpellRates()
{
    if (g_spellRate.dataLoaded)
        return;
    g_spellRate.dataPath = SpellCapturePath();
    bool loaded = false;
    if (!g_spellRate.dataPath.empty()) {
        const HANDLE file = CreateFileW(g_spellRate.dataPath.c_str(),
            GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            int32_t version = 0;
            if (ReadExact(file, &version, sizeof(version))) {
                switch (version) {
                case 1:
                    loaded = ReadExact(file, allSpells, sizeof(allSpells));
                    break;
                default:
                    break;
                }
            }
            CloseHandle(file);
        }
    }
    if (!loaded) {
        std::memset(allSpells, 0, sizeof(allSpells));
        LoadLegacyBooksRates();
    }
    for (auto& spell : allSpells) {
        for (SpellRate& rate : spell) {
            rate.attempt = std::max(0, rate.attempt);
            rate.captured = std::clamp(rate.captured, 0, rate.attempt);
        }
    }
    g_spellRate.dataLoaded = true;
    SaveSpellRates();
}

void IncrementAttempt(int spellId, int shot)
{
    if (spellId < 0 || spellId >= kSpellRateSpellCount ||
        shot < 0 || shot >= kSpellRateShotCount)
        return;
    SpellRate& rate = allSpells[spellId][shot];
    if (rate.attempt < INT_MAX)
        ++rate.attempt;
    SaveSpellRates();
}

void IncrementCaptured(int spellId, int shot)
{
    if (spellId < 0 || spellId >= kSpellRateSpellCount ||
        shot < 0 || shot >= kSpellRateShotCount)
        return;
    SpellRate& rate = allSpells[spellId][shot];
    if (rate.captured < INT_MAX)
        ++rate.captured;
    SaveSpellRates();
}

void RecordNormalSpellAttempt()
{
    const auto* spellId = ResolveGameAddress<int>(GameAddress::SpellCardId);
    if (spellId)
        IncrementAttempt(*spellId, CurrentSpellRateShot());
}

void RecordNormalSpellCapture()
{
    const auto* spellId = ResolveGameAddress<int>(GameAddress::SpellCardId);
    if (spellId)
        IncrementCaptured(*spellId, CurrentSpellRateShot());
}

int SpellIdFromRecord(const void* record)
{
    const auto* base = ResolveGameAddress<std::byte>(GameAddress::SpellCardInfo);
    if (!base || !record)
        return -1;
    const ptrdiff_t offset = static_cast<const std::byte*>(record) - base;
    if (offset < 0 || offset % kNativeSpellRecordSize != 0)
        return -1;
    const int id = static_cast<int>(offset / kNativeSpellRecordSize);
    return id >= 0 && id < kNativeSpellCount ? id : -1;
}

void CaptureSpellListDisplayId(const void* record)
{
    g_spellRate.spellListDisplayId = SpellIdFromRecord(record);
}

void CaptureResultDisplayId(uintptr_t recordOffset)
{
    if (recordOffset % kNativeSpellRecordSize != 0) {
        g_spellRate.resultDisplayId = -1;
        return;
    }
    const int id = static_cast<int>(recordOffset / kNativeSpellRecordSize);
    g_spellRate.resultDisplayId =
        id >= 0 && id < kNativeSpellCount ? id : -1;
}

int __fastcall FormatSpellListRate(char* destination, size_t capacity,
    const char*, const char* label, int nativeCaptured, int nativeAttempt)
{
    const SpellRate& rate = GetSpellRate(
        g_spellRate.spellListDisplayId, CurrentSpellRateShot());
    return _snprintf_s(destination, capacity, _TRUNCATE,
        "%s %d/%d     %d/%d", label ? label : "", rate.captured,
        rate.attempt, nativeCaptured, nativeAttempt);
}

using AsciiPrintfFn = void(__fastcall*)(void*, const void*, const char*, ...);

void __fastcall DrawResultSpellRate(void* ascii, const void* position,
    const char*, int nativeCaptured, int nativeAttempt)
{
    const SpellRate& rate = GetSpellRate(
        g_spellRate.resultDisplayId, CurrentSpellRateShot());
    const auto asciiPrintf = reinterpret_cast<AsciiPrintfFn>(
        ResolveGameAddress<void>(GameAddress::AsciiPrintf));
    if (asciiPrintf) {
        asciiPrintf(ascii, position, "%d/%d(%d/%d)", rate.captured,
            rate.attempt, nativeCaptured, nativeAttempt);
    }
}

void* AllocateNearAddress(void* target, size_t size)
{
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const uintptr_t targetAddress = reinterpret_cast<uintptr_t>(target);
    const uintptr_t minimum =
        reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
    const uintptr_t maximum =
        reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
    const uintptr_t reach =
        static_cast<uintptr_t>(std::numeric_limits<int32_t>::max()) - 0x10000;
    const uintptr_t low = targetAddress > reach
        ? std::max(targetAddress - reach, minimum) : minimum;
    const uintptr_t high = std::min(targetAddress + reach, maximum);
    const uintptr_t granularity = info.dwAllocationGranularity;
    uintptr_t cursor = low;
    while (cursor < high) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &memory,
                sizeof(memory)))
            break;
        const uintptr_t regionBase =
            reinterpret_cast<uintptr_t>(memory.BaseAddress);
        const uintptr_t regionEnd = regionBase + memory.RegionSize;
        if (memory.State == MEM_FREE) {
            uintptr_t candidate = std::max(cursor, regionBase);
            candidate = (candidate + granularity - 1) & ~(granularity - 1);
            if (candidate <= high && candidate <= regionEnd &&
                size <= regionEnd - candidate) {
                if (void* block = VirtualAlloc(reinterpret_cast<void*>(candidate),
                        size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE))
                    return block;
            }
        }
        if (regionEnd <= cursor)
            break;
        cursor = regionEnd;
    }
    return nullptr;
}

void Append(std::vector<uint8_t>& code, std::initializer_list<uint8_t> bytes)
{
    code.insert(code.end(), bytes.begin(), bytes.end());
}

template <typename T>
void AppendValue(std::vector<uint8_t>& code, T value)
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    code.insert(code.end(), bytes, bytes + sizeof(value));
}

void AppendAbsoluteJump(std::vector<uint8_t>& code, const void* target)
{
    Append(code, {0xFF, 0x25, 0, 0, 0, 0});
    AppendValue(code, target);
}

void AppendPreservedCallback(std::vector<uint8_t>& code,
    const void* callback, CallbackArgument argument)
{
    Append(code, {0x9C, 0x50, 0x51, 0x52, 0x41, 0x50,
        0x41, 0x51, 0x41, 0x52, 0x41, 0x53});
    Append(code, {0x48, 0x81, 0xEC, 0x80, 0, 0, 0});
    constexpr uint8_t xmmModRm[6] = {0x44, 0x4C, 0x54, 0x5C, 0x64, 0x6C};
    for (int xmm = 0; xmm < 6; ++xmm)
        Append(code, {0x0F, 0x11, xmmModRm[xmm], 0x24,
            static_cast<uint8_t>(0x20 + xmm * 0x10)});
    if (argument == CallbackArgument::R14)
        Append(code, {0x4C, 0x89, 0xF1});
    else if (argument == CallbackArgument::Rbx)
        Append(code, {0x48, 0x89, 0xD9});
    Append(code, {0x48, 0xB8});
    AppendValue(code, callback);
    Append(code, {0xFF, 0xD0});
    for (int xmm = 0; xmm < 6; ++xmm)
        Append(code, {0x0F, 0x10, xmmModRm[xmm], 0x24,
            static_cast<uint8_t>(0x20 + xmm * 0x10)});
    Append(code, {0x48, 0x81, 0xC4, 0x80, 0, 0, 0,
        0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58,
        0x5A, 0x59, 0x58, 0x9D});
}

bool WriteRelativeBranch(std::byte* site, size_t patchSize,
    const std::byte* expected, std::byte* relay, uint8_t opcode)
{
    if (!site || patchSize < 5 ||
        std::memcmp(site, expected, patchSize) != 0)
        return false;
    const intptr_t displacement = relay - (site + 5);
    if (displacement < INT32_MIN || displacement > INT32_MAX)
        return false;
    DWORD oldProtection = 0;
    if (!VirtualProtect(site, patchSize, PAGE_EXECUTE_READWRITE,
            &oldProtection))
        return false;
    site[0] = static_cast<std::byte>(opcode);
    *reinterpret_cast<int32_t*>(site + 1) =
        static_cast<int32_t>(displacement);
    std::memset(site + 5, 0x90, patchSize - 5);
    FlushInstructionCache(GetCurrentProcess(), site, patchSize);
    DWORD ignored = 0;
    VirtualProtect(site, patchSize, oldProtection, &ignored);
    return true;
}

bool InstallCallbackPatch(GameAddress address, const uint8_t* expected,
    size_t patchSize, const uint8_t* original, size_t originalSize,
    const void* callback, CallbackArgument argument, GameAddress continuation)
{
    auto* site = ResolveGameAddress<std::byte>(address);
    if (!site || !IsGameAddressRangeValid(address, patchSize) ||
        std::memcmp(site, expected, patchSize) != 0)
        return false;
    std::vector<uint8_t> code;
    code.reserve(256);
    if (original && originalSize)
        code.insert(code.end(), original, original + originalSize);
    AppendPreservedCallback(code, callback, argument);
    AppendAbsoluteJump(code, ResolveGameAddress<void>(continuation));
    auto* relay = static_cast<std::byte*>(AllocateNearAddress(site, code.size()));
    if (!relay)
        return false;
    std::memcpy(relay, code.data(), code.size());
    DWORD oldProtection = 0;
    if (!VirtualProtect(relay, code.size(), PAGE_EXECUTE_READ, &oldProtection) ||
        !WriteRelativeBranch(site, patchSize,
            reinterpret_cast<const std::byte*>(expected), relay, 0xE9)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    return true;
}

bool InstallCallPatch(GameAddress address, const uint8_t (&expected)[5],
    const void* replacement)
{
    auto* site = ResolveGameAddress<std::byte>(address);
    if (!site || !IsGameAddressRangeValid(address, sizeof(expected)) ||
        std::memcmp(site, expected, sizeof(expected)) != 0)
        return false;
    std::vector<uint8_t> code;
    AppendAbsoluteJump(code, replacement);
    auto* relay = static_cast<std::byte*>(AllocateNearAddress(site, code.size()));
    if (!relay)
        return false;
    std::memcpy(relay, code.data(), code.size());
    DWORD oldProtection = 0;
    if (!VirtualProtect(relay, code.size(), PAGE_EXECUTE_READ, &oldProtection) ||
        !WriteRelativeBranch(site, sizeof(expected),
            reinterpret_cast<const std::byte*>(expected), relay, 0xE8)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    return true;
}

bool InstallNativeSpellRatePatches()
{
    constexpr uint8_t attemptAfterIncrement[5] =
        {0xE9, 0xEA, 0xE1, 0xFF, 0xFF};
    constexpr uint8_t captureStore[9] =
        {0x66, 0x41, 0x89, 0x84, 0x0A, 0x06, 0x28, 0x4F, 0x00};
    constexpr uint8_t listIdRead[5] =
        {0x45, 0x0F, 0xB7, 0x7E, 0x3C};
    constexpr uint8_t resultIdRead[9] =
        {0x42, 0x0F, 0xB7, 0x84, 0x3B, 0x04, 0x28, 0x4F, 0x00};
    constexpr uint8_t listFormatCall[5] =
        {0xE8, 0xA0, 0x11, 0xFC, 0xFF};
    constexpr uint8_t resultDrawCall[5] =
        {0xE8, 0x95, 0x69, 0xF9, 0xFF};

    return InstallCallbackPatch(GameAddress::SpellAttemptAfterIncrement,
               attemptAfterIncrement, sizeof(attemptAfterIncrement), nullptr, 0,
               reinterpret_cast<void*>(RecordNormalSpellAttempt),
               CallbackArgument::None, GameAddress::SpellAttemptContinuation) &&
        InstallCallbackPatch(GameAddress::SpellCaptureStore, captureStore,
            sizeof(captureStore), captureStore, sizeof(captureStore),
            reinterpret_cast<void*>(RecordNormalSpellCapture),
            CallbackArgument::None, GameAddress::SpellCaptureContinuation) &&
        InstallCallbackPatch(GameAddress::SpellListRateRead, listIdRead,
            sizeof(listIdRead), listIdRead, sizeof(listIdRead),
            reinterpret_cast<void*>(CaptureSpellListDisplayId),
            CallbackArgument::R14, GameAddress::SpellListRateReadContinuation) &&
        InstallCallPatch(GameAddress::SpellListRateFormatCall, listFormatCall,
            reinterpret_cast<void*>(FormatSpellListRate)) &&
        InstallCallbackPatch(GameAddress::SpellResultRateRead, resultIdRead,
            sizeof(resultIdRead), resultIdRead, sizeof(resultIdRead),
            reinterpret_cast<void*>(CaptureResultDisplayId),
            CallbackArgument::Rbx, GameAddress::SpellResultRateReadContinuation) &&
        InstallCallPatch(GameAddress::SpellResultRateDrawCall, resultDrawCall,
            reinterpret_cast<void*>(DrawResultSpellRate));
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
    BooksInfo& books = g_spellRate.books;
    if (books.isInBooks)
        return;
    if (*ResolveGameAddress<uint8_t>(GameAddress::ReplayModeFlag))
        return;
    const auto* misses = ResolveGameAddress<int32_t>(GameAddress::MissCount);
    const auto* bombs = ResolveGameAddress<int32_t>(GameAddress::BombUseCount);
    const auto* difficulty = ResolveGameAddress<int>(GameAddress::CurrentDifficulty);
    const int shot = CurrentSpellRateShot();
    if (!misses || !bombs || !difficulty || shot < 0 ||
        *difficulty < 0 || *difficulty >= 4)
        return;
    books.isInBooks = true;
    books.lastMiss = *misses;
    books.lastBomb = *bombs;
    books.activeShot = shot;
    books.activeDifficulty = *difficulty;
    IncrementAttempt(kBooksSpellBase + *difficulty, shot);
}

void FinishBooksAttempt()
{
    BooksInfo& books = g_spellRate.books;
    if (!books.isInBooks)
        return;
    if (*ResolveGameAddress<uint8_t>(GameAddress::ReplayModeFlag))
        return;
    const int shot = books.activeShot;
    const int difficulty = books.activeDifficulty;
    const auto* misses = ResolveGameAddress<int32_t>(GameAddress::MissCount);
    const auto* bombs = ResolveGameAddress<int32_t>(GameAddress::BombUseCount);
    const bool passed = misses && bombs &&
        *misses <= books.lastMiss && *bombs <= books.lastBomb;
    ResetBooksAttempt();
    if (passed && difficulty >= 0 && difficulty < 4)
        IncrementCaptured(kBooksSpellBase + difficulty, shot);
}

} // namespace

bool InstallSpellRateHooks()
{
    LoadSpellRates();
    if (!g_spellRate.hooksInstalled)
        g_spellRate.hooksInstalled = InstallNativeSpellRatePatches();
    return g_spellRate.hooksInstalled;
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
    if (g_spellRate.lastTimelineTime >= 0 &&
        timelineTime < g_spellRate.lastTimelineTime)
        ResetBooksAttempt();
    g_spellRate.lastTimelineTime = timelineTime;
    const auto* instruction = *reinterpret_cast<std::byte* const*>(
        manager + kTimelineInstructionOffset);
    if (IsInstruction(instruction, kEnemyCreateRandomOpcode, kBooksStartTime))
        BeginBooksAttempt();
    else if (timelineTime == kBooksEndTime)
        FinishBooksAttempt();
}

void ResetBooksAttempt()
{
    g_spellRate.books.isInBooks = false;
    g_spellRate.books.activeShot = -1;
    g_spellRate.books.activeDifficulty = -1;
    g_spellRate.lastTimelineTime = -1;
}

const BooksInfo& GetBooksInfo()
{
    return g_spellRate.books;
}

int CurrentSpellRateShot()
{
    const auto* character = ResolveGameAddress<uint8_t>(
        GameAddress::CurrentCharacter);
    const auto* shotType = ResolveGameAddress<uint8_t>(
        GameAddress::CurrentShotType);
    if (!character || !shotType)
        return -1;
    const int shot = static_cast<int>(*shotType) +
        static_cast<int>(*character) * 2;
    return shot >= 0 && shot < kSpellRateShotCount ? shot : -1;
}

const SpellRate& GetSpellRate(int spellId, int shot)
{
    static const SpellRate empty{};
    if (spellId < 0 || spellId >= kSpellRateSpellCount ||
        shot < 0 || shot >= kSpellRateShotCount)
        return empty;
    return allSpells[spellId][shot];
}

void DrawSpellRateTableUi()
{
    static int selectedDifficulty = 0;
    static int selectedShot = 0;
    constexpr const char* difficulties[] = {"Easy", "Normal", "Hard", "Lunatic", "Extra"};
    const char* shots[] = {S(ReimuA), S(ReimuB),S(MarisaA),S(MarisaB) };

    if (!ImGui::CollapsingHeader(S(SpellRateTable)))
        return;

    if (ImGui::BeginTabBar("##spell-rate-difficulty")) {
        for (int difficulty = 0; difficulty < IM_ARRAYSIZE(difficulties);
             ++difficulty) {
            if (ImGui::BeginTabItem(difficulties[difficulty])) {
                selectedDifficulty = difficulty;
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    if (ImGui::BeginTabBar("##spell-rate-shot")) {
        for (int shot = 0; shot < IM_ARRAYSIZE(shots); ++shot) {
            if (ImGui::BeginTabItem(shots[shot])) {
                selectedShot = shot;
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg  |
        ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##spell-rate-table", 5, flags))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(S(SpellId), ImGuiTableFlags_Resizable, 70.0f);
    ImGui::TableSetupColumn(S(SpellName), ImGuiTableFlags_Resizable, 120.0f);
    ImGui::TableSetupColumn(S(Captured), ImGuiTableFlags_Resizable, 90.0f);
    ImGui::TableSetupColumn(S(Attempt), ImGuiTableFlags_Resizable,  90.0f);
    ImGui::TableSetupColumn(S(Percent), ImGuiTableFlags_Resizable,  90.0f);
    ImGui::TableHeadersRow();
    for (const SpellInfo& info : spellInfo) {
        if (info.id < 0 || info.id >= kSpellRateSpellCount ||
            info.diff != selectedDifficulty)
            continue;
        const SpellRate& rate = GetSpellRate(info.id, selectedShot);
        const double percent = rate.attempt > 0
            ? static_cast<double>(rate.captured) * 100.0 / rate.attempt : 0.0;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%d", info.id);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(Locale::Instance().GetJump( static_cast<int>(info.nameLocale), info.diff));
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%d", rate.captured);
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%d", rate.attempt);
        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%.2f%%", percent);
    }
    ImGui::EndTable();
}
