#include "replay_support.h"

#include "game_addresses.h"
#include "game_overlay.h"
#include "keyboard_input.h"
#include "locale.h"
#include "practice_menu.h"
#include "imgui.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

namespace {

constexpr uint32_t kReplayMagic = 0x5052434Eu; // "NCRP"
constexpr uint32_t kReplayProtocol = 5;
constexpr uint32_t kMinimumReplayProtocol = 4;
constexpr uint32_t kEscapeInput = 0x400;
constexpr uint32_t kMenuConfirmInput = 0x100;
constexpr uint32_t kMenuUpInput = 0x10;
constexpr uint32_t kMenuDownInput = 0x20;
constexpr uint32_t kMenuLeftInput = 0x40;
constexpr uint32_t kMenuRightInput = 0x80;
constexpr size_t kMaximumReplayBytes = 64u * 1024u * 1024u;
constexpr size_t kMaximumPracticePayloadBytes = 64u * 1024u;

constexpr unsigned char kGameUpdatePrologue[14] = {
    0x48, 0x89, 0x5C, 0x24, 0x10,
    0x48, 0x89, 0x6C, 0x24, 0x18,
    0x56, 0x57, 0x41, 0x56,
};
constexpr unsigned char kResultInitializePrologue[15] = {
    0x48, 0x89, 0x5C, 0x24, 0x18,
    0x48, 0x89, 0x4C, 0x24, 0x08,
    0x55, 0x56, 0x57, 0x41, 0x54,
};
constexpr unsigned char kReplayWritePrologue[16] = {
    0x48, 0x89, 0x5C, 0x24, 0x20,
    0x55, 0x56, 0x57,
    0x41, 0x54, 0x41, 0x55,
    0x41, 0x56, 0x41, 0x57,
};
enum class PauseAction : int {
    None,
    Resume,
    SaveAndExit,
    ExitWithoutReplay,
    Restart,
};

#pragma pack(push, 1)
struct PracticeReplayFooter {
    uint32_t magic = kReplayMagic;
    uint32_t protocol = kReplayProtocol;
    uint32_t trailerSize = 0;
    uint32_t payloadSize = 0;
    uint64_t nativeReplaySize = 0;
    uint64_t nativeReplayDigest = 0;
};
#pragma pack(pop)

struct ReplaySaveRequest {
    std::string path;
    std::vector<uint8_t> nativeReplay;
    PracticeReplayConfig config{};
};

using GameCallback = int64_t(__fastcall*)(void*);
using ReplayWriter = void(__fastcall*)(const char*, void*, size_t);

GameCallback g_nativeGameUpdate = nullptr;
GameCallback g_nativeResultInitialize = nullptr;
ReplayWriter g_nativeReplayWrite = nullptr;
std::atomic<bool> g_pauseVisible{false};
std::atomic<PauseAction> g_pauseAction{PauseAction::None};
std::atomic<bool> g_pauseEscapeReleased{false};
std::atomic<int> g_pauseNavigation{0};
std::atomic<int> g_pauseHorizontal{0};
std::atomic<bool> g_pauseConfirm{false};
std::atomic<bool> g_hooksInstalled{false};
std::atomic<bool> g_replayConfigPrepared{false};
PracticeReplayConfig g_recordedConfig{};
bool g_haveRecordedConfig = false;

using PracticeValueMap = std::map<std::string, std::string>;

template <typename T>
std::string IntegerToString(T value)
{
    static_assert(std::is_integral_v<T>);
    char buffer[32]{};
    const auto converted = std::to_chars(std::begin(buffer), std::end(buffer),
        value);
    return converted.ec == std::errc{}
        ? std::string(buffer, converted.ptr) : std::string{};
}

template <typename T>
bool StringToInteger(std::string_view text, T& value)
{
    static_assert(std::is_integral_v<T>);
    T parsed{};
    const auto converted = std::from_chars(text.data(),
        text.data() + text.size(), parsed);
    if (converted.ec != std::errc{} || converted.ptr != text.data() + text.size())
        return false;
    value = parsed;
    return true;
}

PracticeValueMap PracticeConfigToMap(const PracticeReplayConfig& config)
{
    PracticeValueMap values;
#define TH06NC_WRITE_PRACTICE_FIELD(type_, name_, default_, protocol_) \
    values.emplace(#name_, IntegerToString(config.name_));
    TH06NC_PRACTICE_PARAM_FIELDS(TH06NC_WRITE_PRACTICE_FIELD)
#undef TH06NC_WRITE_PRACTICE_FIELD
    for (size_t i = 0; i < std::size(config.bookX); ++i) {
        values.emplace("bookX." + std::to_string(i),
            IntegerToString(config.bookX[i]));
        values.emplace("bookY." + std::to_string(i),
            IntegerToString(config.bookY[i]));
    }
    return values;
}

std::string EncodePracticeValueMap(const PracticeValueMap& values)
{
    std::string payload;
    for (const auto& [name, value] : values) {
        payload.append(name);
        payload.push_back('=');
        payload.append(value);
        payload.push_back('\n');
    }
    return payload;
}

bool DecodePracticeValueMap(std::string_view payload,
    PracticeValueMap& values)
{
    values.clear();
    while (!payload.empty()) {
        const size_t newline = payload.find('\n');
        const std::string_view line = payload.substr(0, newline);
        payload = newline == std::string_view::npos
            ? std::string_view{} : payload.substr(newline + 1);
        if (line.empty())
            continue;
        const size_t separator = line.find('=');
        if (separator == std::string_view::npos || separator == 0 ||
            separator + 1 == line.size())
            return false;
        std::string name(line.substr(0, separator));
        std::string value(line.substr(separator + 1));
        if (!values.emplace(std::move(name), std::move(value)).second)
            return false;
    }
    return !values.empty();
}

bool PracticeConfigFromMap(const PracticeValueMap& values,
    PracticeReplayConfig& config)
{
    PracticeReplayConfig parsed{};
#define TH06NC_READ_PRACTICE_FIELD(type_, name_, default_, protocol_)      \
    if (const auto found = values.find(#name_); found != values.end() &&  \
        !StringToInteger(found->second, parsed.name_))                     \
        return false;
    TH06NC_PRACTICE_PARAM_FIELDS(TH06NC_READ_PRACTICE_FIELD)
#undef TH06NC_READ_PRACTICE_FIELD
    for (size_t i = 0; i < std::size(parsed.bookX); ++i) {
        const std::string xName = "bookX." + std::to_string(i);
        const std::string yName = "bookY." + std::to_string(i);
        if (const auto found = values.find(xName); found != values.end() &&
            !StringToInteger(found->second, parsed.bookX[i]))
            return false;
        if (const auto found = values.find(yName); found != values.end() &&
            !StringToInteger(found->second, parsed.bookY[i]))
            return false;
    }
    config = parsed;
    return true;
}

uint32_t RequiredReplayProtocol(const PracticeReplayConfig& config)
{
    uint32_t protocol = kMinimumReplayProtocol;
#define TH06NC_REQUIRE_FIELD_PROTOCOL(type_, name_, default_, protocol_)  \
    static_assert(protocol_ <= kReplayProtocol,                           \
        "Bump kReplayProtocol for the newly introduced practice field"); \
    if (config.name_ != static_cast<type_>(default_))                     \
        protocol = std::max(protocol, uint32_t{protocol_});
    TH06NC_PRACTICE_PARAM_FIELDS(TH06NC_REQUIRE_FIELD_PROTOCOL)
#undef TH06NC_REQUIRE_FIELD_PROTOCOL
    return protocol;
}

void WriteAbsoluteJump(unsigned char* destination, const void* target)
{
    destination[0] = 0xFF;
    destination[1] = 0x25;
    *reinterpret_cast<uint32_t*>(destination + 2) = 0;
    *reinterpret_cast<const void**>(destination + 6) = target;
}

template <size_t Size, typename Function>
bool InstallDetour(GameAddress address,
    const unsigned char (&expected)[Size], const void* replacement,
    Function& original)
{
    static_assert(Size >= 14);
    auto* target = ResolveGameAddress<unsigned char>(address);
    if (!target || !IsGameAddressRangeValid(address, Size) ||
        std::memcmp(target, expected, Size) != 0)
        return false;

    constexpr size_t trampolineSize = Size + 14;
    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(nullptr,
        trampolineSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!trampoline)
        return false;
    std::memcpy(trampoline, target, Size);
    WriteAbsoluteJump(trampoline + Size, target + Size);

    DWORD trampolineProtection = 0;
    if (!VirtualProtect(trampoline, trampolineSize, PAGE_EXECUTE_READ,
            &trampolineProtection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    DWORD targetProtection = 0;
    if (!VirtualProtect(target, Size, PAGE_EXECUTE_READWRITE,
            &targetProtection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    WriteAbsoluteJump(target, replacement);
    if constexpr (Size > 14)
        std::memset(target + 14, 0x90, Size - 14);
    FlushInstructionCache(GetCurrentProcess(), target, Size);
    DWORD ignored = 0;
    VirtualProtect(target, Size, targetProtection, &ignored);

    original = reinterpret_cast<Function>(trampoline);
    return true;
}

template <size_t Size>
bool CanInstallDetour(GameAddress address,
    const unsigned char (&expected)[Size])
{
    const auto* target = ResolveGameAddress<unsigned char>(address);
    return target && IsGameAddressRangeValid(address, Size) &&
        std::memcmp(target, expected, Size) == 0;
}

uint64_t Digest(const void* data, size_t size)
{
    uint64_t value = 14695981039346656037ull;
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
        value = (value ^ bytes[i]) * 1099511628211ull;
    return value;
}

uint64_t NativeReplayDigest(const void* data, size_t size)
{
    // The TH06NC checksum at [12, 16) changes after a trailer is appended. Omit
    // those four bytes from our own identity digest so updating the native
    // checksum does not create a circular dependency on the trailer digest.
    uint64_t value = 14695981039346656037ull;
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        const uint8_t byte = i >= 12 && i < 16 ? 0 : bytes[i];
        value = (value ^ byte) * 1099511628211ull;
    }
    return value;
}

bool ReadFileLimited(const std::string& path, std::vector<uint8_t>& output,
    size_t maximumSize)
{
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER length{};
    bool ok = GetFileSizeEx(file, &length) && length.QuadPart >= 0 &&
        static_cast<uint64_t>(length.QuadPart) <= maximumSize;
    DWORD read = 0;
    if (ok) {
        output.resize(static_cast<size_t>(length.QuadPart));
        ok = output.empty() ||
            (ReadFile(file, output.data(), static_cast<DWORD>(output.size()),
                 &read, nullptr) && read == output.size());
    }
    CloseHandle(file);
    return ok;
}

bool IsReplayPath(const char* path)
{
    if (!path)
        return false;
    const size_t length = strnlen_s(path, MAX_PATH);
    return length >= 4 && length < MAX_PATH &&
        _stricmp(path + length - 4, ".rpy") == 0;
}

bool UpdateTh06ReplayChecksum(HANDLE file,
    const std::vector<uint8_t>& replay)
{
    // Physical bytes "T6RP" are the little-endian replay magic. The NC reader
    // at +0x6AF60 decrypts bytes 0x13..EOF using the rolling key at 0x12, then
    // +0x6B110 sums decrypted bytes 0x12..EOF from seed 0x2F10A329 and compares
    // the result with the DWORD at 0x0C. Appended plaintext therefore has to
    // participate in that exact transform before the native list/load path
    // will accept the extended file.
    constexpr uint32_t kTh06ReplayFileMagic = 0x50523654u;
    constexpr uint32_t kInitialChecksum = 0x2F10A329u;
    if (replay.size() < 19)
        return false;

    uint32_t magic = 0;
    std::memcpy(&magic, replay.data(), sizeof(magic));
    if (magic != kTh06ReplayFileMagic)
        return false;

    uint32_t checksum = kInitialChecksum + replay[18];
    uint8_t key = replay[18];
    for (size_t i = 19; i < replay.size(); ++i) {
        checksum += static_cast<uint8_t>(replay[i] - key);
        key = static_cast<uint8_t>(key + 7);
    }

    LARGE_INTEGER checksumOffset{};
    checksumOffset.QuadPart = 12;
    DWORD written = 0;
    return SetFilePointerEx(file, checksumOffset, nullptr, FILE_BEGIN) &&
        WriteFile(file, &checksum, sizeof(checksum), &written, nullptr) &&
        written == sizeof(checksum);
}

bool AppendPracticeReplayTrailer(const char* replayPath,
    const PracticeReplayConfig& config)
{
    if (!IsReplayPath(replayPath))
        return false;

    // The native writer must receive exactly its own buffer and size. Once it
    // has returned and closed the file, append metadata to that same .rpy.
    // Passing an enlarged buffer into the native serializer corrupts its
    // internal format and can crash the result screen.
    std::vector<uint8_t> nativeReplay;
    if (!ReadFileLimited(replayPath, nativeReplay, kMaximumReplayBytes))
        return false;

    const std::string payload =
        EncodePracticeValueMap(PracticeConfigToMap(config));
    if (payload.empty() || payload.size() > kMaximumPracticePayloadBytes)
        return false;

    PracticeReplayFooter footer{};
    footer.protocol = RequiredReplayProtocol(config);
    footer.payloadSize = static_cast<uint32_t>(payload.size());
    footer.trailerSize = footer.payloadSize + sizeof(footer);
    footer.nativeReplaySize = nativeReplay.size();
    footer.nativeReplayDigest =
        NativeReplayDigest(nativeReplay.data(), nativeReplay.size());

    std::vector<uint8_t> completeReplay = nativeReplay;
    const auto* payloadBytes = reinterpret_cast<const uint8_t*>(payload.data());
    completeReplay.insert(completeReplay.end(), payloadBytes,
        payloadBytes + payload.size());
    const auto* footerBytes = reinterpret_cast<const uint8_t*>(&footer);
    completeReplay.insert(completeReplay.end(), footerBytes,
        footerBytes + sizeof(footer));

    HANDLE file = CreateFileA(replayPath, GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER end{};
    DWORD written = 0;
    const bool ok = SetFilePointerEx(file, end, nullptr, FILE_END) &&
        WriteFile(file, payload.data(), static_cast<DWORD>(payload.size()),
            &written, nullptr) &&
        written == payload.size() &&
        WriteFile(file, &footer, sizeof(footer), &written, nullptr) &&
        written == sizeof(footer) &&
        UpdateTh06ReplayChecksum(file, completeReplay) &&
        FlushFileBuffers(file);
    CloseHandle(file);
    return ok;
}

bool LoadPracticeReplayTrailer(const char* replayPath,
    PracticeReplayConfig& config)
{
    if (!IsReplayPath(replayPath))
        return false;
    std::vector<uint8_t> replay;
    if (!ReadFileLimited(replayPath, replay,
            kMaximumReplayBytes + kMaximumPracticePayloadBytes +
                sizeof(PracticeReplayFooter)) ||
        replay.size() < sizeof(PracticeReplayFooter))
        return false;

    PracticeReplayFooter footer{};
    const size_t footerOffset = replay.size() - sizeof(footer);
    std::memcpy(&footer, replay.data() + footerOffset, sizeof(footer));
    if (footer.protocol < kMinimumReplayProtocol ||
        footer.protocol > kReplayProtocol ||
        footer.payloadSize == 0 ||
        footer.payloadSize > kMaximumPracticePayloadBytes ||
        footer.trailerSize != footer.payloadSize + sizeof(footer) ||
        footer.trailerSize > replay.size() ||
        footer.nativeReplaySize != replay.size() - footer.trailerSize)
        return false;
    const size_t nativeSize = static_cast<size_t>(footer.nativeReplaySize);
    const uint64_t expectedDigest =
        NativeReplayDigest(replay.data(), nativeSize);
    if (footer.magic != kReplayMagic ||
        (footer.nativeReplayDigest != 0 &&
            footer.nativeReplayDigest != expectedDigest))
        return false;
    PracticeValueMap values;
    const auto* payload = reinterpret_cast<const char*>(
        replay.data() + nativeSize);
    return DecodePracticeValueMap(
               std::string_view(payload, footer.payloadSize), values) &&
        PracticeConfigFromMap(values, config);
}

bool NativeReplayHasReachedDisk(const ReplaySaveRequest& request)
{
    std::vector<uint8_t> onDisk;
    return ReadFileLimited(request.path, onDisk, kMaximumReplayBytes) &&
        onDisk.size() == request.nativeReplay.size() &&
        (onDisk.empty() || std::memcmp(onDisk.data(),
            request.nativeReplay.data(), onDisk.size()) == 0);
}

DWORD WINAPI FinishPracticeReplaySave(void* rawRequest)
{
    std::unique_ptr<ReplaySaveRequest> request(
        static_cast<ReplaySaveRequest*>(rawRequest));
    // +0x39610 queues the file operation and can return while the previous
    // file is still visible. Match the exact buffer supplied to that writer
    // before extending the file; size-only polling races when a slot is
    // overwritten by another replay of the same length.
    for (int attempt = 0; attempt < 500; ++attempt) {
        if (NativeReplayHasReachedDisk(*request) &&
            AppendPracticeReplayTrailer(request->path.c_str(),
                request->config))
            return 0;
        Sleep(10);
    }
    return 1;
}

int64_t __fastcall HookedGameUpdate(void* game)
{
    if (!g_nativeGameUpdate)
        return 0;
    const auto* replay = ResolveGameAddress<uint8_t>(GameAddress::ReplayModeFlag);
    if (replay && *replay != 0) {
        if (!g_replayConfigPrepared.load() && PreparePracticeReplayPlayback())
            g_replayConfigPrepared.store(true);
    } else {
        g_replayConfigPrepared.store(false);
    }
    auto* currentState = ResolveGameAddress<int>(GameAddress::CurrentGameState);
    auto* nextState = ResolveGameAddress<int>(GameAddress::NextGameState);
    auto* paused = ResolveGameAddress<uint8_t>(GameAddress::PausedFlag);
    const auto* gameOver = ResolveGameAddress<uint8_t>(GameAddress::GameOverFlag);
    auto* input = ResolveGameAddress<uint32_t>(GameAddress::MenuInputCurrent);
    auto* previous = ResolveGameAddress<uint32_t>(GameAddress::MenuInputPrevious);
    const bool canOwnPause = IsEnhancedPracticeRunActive() &&
        (!replay || *replay == 0) && currentState && nextState &&
        *currentState == 2 && *nextState == 2;

    if (!canOwnPause) {
        g_pauseVisible.store(false);
        g_pauseAction.store(PauseAction::None);
        return g_nativeGameUpdate(game);
    }

    if (!g_pauseVisible.load() && paused && (!gameOver || *gameOver == 0) &&
        input && previous && (*input & kEscapeInput) != 0 &&
        (*previous & kEscapeInput) == 0) {
        g_pauseVisible.store(true);
        g_pauseEscapeReleased.store(false);
        g_pauseNavigation.store(0);
        g_pauseHorizontal.store(0);
        g_pauseConfirm.store(false);
    }

    if (!g_pauseVisible.load())
        return g_nativeGameUpdate(game);

    // Capture logical menu input on the game-update thread, before the native
    // paused callback advances current/previous state. The renderer may run
    // after that advancement, so reading the edge there loses keyboard and
    // controller navigation intermittently.
    const auto* repeat =
        ResolveGameAddress<uint16_t>(GameAddress::MenuInputRepeat);
    const auto pressed = [&](uint32_t mask, bool allowRepeat) {
        return input && previous && (*input & mask) != 0 &&
            (((*input ^ *previous) & mask) != 0 ||
                (allowRepeat && repeat && *repeat != 0));
    };
    if (pressed(kMenuUpInput, true))
        g_pauseNavigation.fetch_sub(1);
    if (pressed(kMenuDownInput, true))
        g_pauseNavigation.fetch_add(1);
    if (pressed(kMenuLeftInput, true))
        g_pauseHorizontal.fetch_sub(1);
    if (pressed(kMenuRightInput, true))
        g_pauseHorizontal.fetch_add(1);
    if (pressed(kMenuConfirmInput, false))
        g_pauseConfirm.store(true);

    const bool escapeDown = input && (*input & kEscapeInput) != 0;
    if (!escapeDown) {
        g_pauseEscapeReleased.store(true);
    } else if (g_pauseEscapeReleased.exchange(false)) {
        g_pauseAction.store(PauseAction::Resume);
    }

    const PauseAction action = g_pauseAction.exchange(PauseAction::None);
    if (action != PauseAction::None) {
        g_pauseVisible.store(false);
        if (previous)
            *previous |= kEscapeInput;
        if (action == PauseAction::SaveAndExit) {
            PrepareEverlastingBgmForInitialization(false);
            *nextState = 7;
            return 3;
        }
        if (action == PauseAction::ExitWithoutReplay) {
            // Native Pause exits a non-replay run through supervisor state 1.
            // State 8 is the Replay menu and was never the direct-exit target.
            EndEnhancedPracticeRun();
            *nextState = 1;
            return 3;
        }
        if (action == PauseAction::Restart) {
            // KeepBgm is consumed while state 12 tears down the old run, well
            // before PlayerInitialize. Set it at the retry request itself.
            const bool keepBgm = MarkEnhancedPracticeRestartPending();
            PrepareEverlastingBgmForInitialization(keepBgm);
            *nextState = 12;
            return 3;
        }
        return g_nativeGameUpdate(game);
    }

    if (!paused)
        return g_nativeGameUpdate(game);
    // Do not let the same Escape edge enter the game's own Pause state while
    // this overlay owns it. Keeping current/previous equal preserves every
    // other logical input bit and works for keyboard and controller alike.
    if (input && previous && (*input & kEscapeInput) != 0)
        *previous |= kEscapeInput;
    const uint8_t oldPaused = *paused;
    *paused = 1;
    const int64_t result = g_nativeGameUpdate(game);
    *paused = oldPaused;
    return result;
}

int64_t __fastcall HookedResultInitialize(void* resultMenu)
{
    if (!g_nativeResultInitialize)
        return 0;
    const auto* replay = ResolveGameAddress<uint8_t>(GameAddress::ReplayModeFlag);
    const auto* nextState = ResolveGameAddress<int>(GameAddress::NextGameState);
    const bool practiceResult = IsEnhancedPracticeRunActive() &&
        (!replay || *replay == 0) && nextState && *nextState == 7;
    auto* bytes = static_cast<std::byte*>(resultMenu);
    if (practiceResult && bytes)
        *reinterpret_cast<int*>(bytes + 0x9E94) = 9;
    const int64_t result = g_nativeResultInitialize(resultMenu);
    if (practiceResult && result == 0 && bytes) {
        *reinterpret_cast<int*>(bytes + 0x9E94) = 10;
        *reinterpret_cast<int*>(bytes + 0x4450) = 0;
        std::memset(bytes + 0x9E88, ' ', 8);
        *reinterpret_cast<char*>(bytes + 0x9E90) = '\0';
    }
    return result;
}

void __fastcall HookedReplayWrite(const char* path, void* data, size_t size)
{
    if (!g_nativeReplayWrite)
        return;
    // Snapshot ownership and the path before native code runs. The save menu
    // may advance its own state during/after the writer call, but that must not
    // make an Enhanced-Practice replay lose its metadata trailer.
    // CapturePracticeReplayStart snapshots the configuration at the actual
    // run initialization. Result-screen flags are no longer authoritative,
    // so the presence of that snapshot is the sole ownership token here.
    const bool practiceReplay = g_haveRecordedConfig && data && size != 0 &&
        size <= kMaximumReplayBytes;
    std::unique_ptr<ReplaySaveRequest> request;
    if (practiceReplay && IsReplayPath(path)) {
        request = std::make_unique<ReplaySaveRequest>();
        request->path = path;
        const auto* bytes = static_cast<const uint8_t*>(data);
        request->nativeReplay.assign(bytes, bytes + size);
        request->config = g_recordedConfig;
    }
    // Complete the game's own serialization first. Practice metadata is a
    // file trailer, not part of the native writer's input buffer.
    g_nativeReplayWrite(path, data, size);
    if (request) {
        HANDLE worker = CreateThread(nullptr, 0, FinishPracticeReplaySave,
            request.get(), 0, nullptr);
        if (worker) {
            request.release();
            CloseHandle(worker);
        }
        g_haveRecordedConfig = false;
    }
}

} // namespace

bool InstallReplaySupportHooks()
{
    if (g_hooksInstalled.load())
        return true;
    if (!CanInstallDetour(GameAddress::GameUpdate, kGameUpdatePrologue) ||
        !CanInstallDetour(GameAddress::ResultInitialize,
            kResultInitializePrologue) ||
        !CanInstallDetour(GameAddress::ReplayWrite, kReplayWritePrologue))
        return false;

    // Install the state-owning update hook last. If an unlikely allocation or
    // protection failure occurs, a partial installation cannot take over ESC.
    const bool writer = InstallDetour(GameAddress::ReplayWrite,
        kReplayWritePrologue, HookedReplayWrite, g_nativeReplayWrite);
    const bool result = InstallDetour(GameAddress::ResultInitialize,
        kResultInitializePrologue, HookedResultInitialize,
        g_nativeResultInitialize);
    const bool update = writer && result &&
        InstallDetour(GameAddress::GameUpdate,
            kGameUpdatePrologue, HookedGameUpdate, g_nativeGameUpdate);
    g_hooksInstalled.store(update && result && writer);
    return g_hooksInstalled.load();
}

const char* ReplaySupportHookStatus()
{
    return g_hooksInstalled.load() ? S(StatusActive) : S(StatusNotInstalled);
}

bool PreparePracticeReplayPlayback()
{
    const auto* replay = ResolveGameAddress<uint8_t>(GameAddress::ReplayModeFlag);
    const auto* path = ResolveGameAddress<char>(GameAddress::ReplayPath);
    if (!replay || *replay == 0 || !path)
        return false;
    const size_t length = strnlen_s(path, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
        return false;

    PracticeReplayConfig config{};
    if (!LoadPracticeReplayTrailer(path, config))
        return false;
    const bool imported = ImportPracticeReplayConfig(config);
    if (imported)
        g_replayConfigPrepared.store(true);
    return imported;
}

void CapturePracticeReplayStart()
{
    const auto* replay = ResolveGameAddress<uint8_t>(GameAddress::ReplayModeFlag);
    if (replay && *replay != 0)
        return;
    g_haveRecordedConfig = ExportPracticeReplayConfig(g_recordedConfig);
}

void DrawPracticePauseUi()
{
    static bool wasVisible = false;
    static bool settingsFocused = false;
    if (!g_pauseVisible.load()) {
        wasVisible = false;
        settingsFocused = false;
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    // Size against the framebuffer instead of fixed ImGui units. Fonts are
    // scaled from the window height globally, so a fixed 420x385 panel became
    // cramped at the 1440p reference scale and nearly unusable above it.
    const ImVec2 windowSize(
        std::clamp(io.DisplaySize.x * 0.60f, 720.0f, 1200.0f),
        io.DisplaySize.y * 0.90f);
    ImGui::SetNextWindowPos(
        ImVec2((io.DisplaySize.x - windowSize.x) * 0.5f,
            (io.DisplaySize.y - windowSize.y) * 0.5f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88f);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;
    const int navigation = g_pauseNavigation.exchange(0);
    const int horizontal =
        std::clamp(g_pauseHorizontal.exchange(0), -1, 1);
    if (ImGui::Begin(S(PauseMenu), nullptr, flags)) {
        static int selected = 0;
        if (!wasVisible) {
            selected = 0;
            ImGui::SetScrollY(0.0f);
        }
        wasVisible = true;
        constexpr int kPauseRowCount = 5;
        bool enteredSettings = false;
        if (!settingsFocused) {
            selected = (selected + navigation % kPauseRowCount +
                kPauseRowCount) % kPauseRowCount;
            if (selected == 4) {
                settingsFocused = true;
                enteredSettings = true;
            }
        }
        const bool movedVertically = navigation != 0;
        if (!settingsFocused && horizontal != 0)
            selected = (selected + horizontal + 4) % 4;
        if (IsRetryKeyPressed())
            g_pauseAction.store(PauseAction::Restart);
        if (IsExitKeyPressed())
            g_pauseAction.store(PauseAction::ExitWithoutReplay);

        const float width = windowSize.x * 0.72f;
        const float buttonHeight = std::clamp(windowSize.y * 0.105f,
            58.0f, 84.0f);
        bool actionHovered = false;
        const auto drawAction = [&](int index, const char* label,
                                    PauseAction action) {
            ImGui::SetCursorPosX((windowSize.x - width) * 0.5f);
            const bool keyboardHighlighted = selected == index;
            if (keyboardHighlighted)
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
            const bool clicked = ImGui::Button(label,
                ImVec2(width, buttonHeight));
            if (ImGui::IsItemHovered()) {
                actionHovered = true;
                selected = index;
                settingsFocused = false;
            }
            if (keyboardHighlighted)
                ImGui::PopStyleColor();
            if (clicked)
                g_pauseAction.store(action);
            if (movedVertically && selected == index)
                ImGui::SetScrollHereY(0.5f);
        };
        drawAction(0, S(Resume), PauseAction::Resume);
        drawAction(1, S(Restart), PauseAction::Restart);
        drawAction(2, S(SaveReplayAndExit), PauseAction::SaveAndExit);
        drawAction(3, S(ExitWithoutReplay), PauseAction::ExitWithoutReplay);

        const bool confirmed =
            g_pauseConfirm.exchange(false) || IsConfirmKeyPressed();
        if (!settingsFocused && confirmed && selected < 4) {
            constexpr PauseAction actions[] = {
                PauseAction::Resume, PauseAction::Restart,
                PauseAction::SaveAndExit, PauseAction::ExitWithoutReplay};
            g_pauseAction.store(actions[selected]);
        }
        if (settingsFocused && confirmed) {
            settingsFocused = false;
            selected = 3;
        }

        ImGui::Separator();
        ImGui::TextUnformatted(S(PracticeSetup));
        if (enteredSettings)
            ImGui::SetScrollHereY(0.0f);
        const PausedPracticeUiResult editorInteraction =
            DrawPausedPracticeConfigurationUi(
            settingsFocused && !enteredSettings
                ? std::clamp(navigation, -1, 1) : 0,
            settingsFocused ? horizontal : 0,
            settingsFocused, enteredSettings,
            enteredSettings && navigation < 0);
        if (editorInteraction.leaveUp) {
            settingsFocused = false;
            selected = 3;
            // The editor has already submitted its last item, so
            // SetScrollHereY would target that item rather than the action
            // buttons rendered above. Scroll the shared window explicitly.
            ImGui::SetScrollY(0.0f);
        } else if (editorInteraction.leaveDown) {
            settingsFocused = false;
            selected = 0;
            ImGui::SetScrollY(0.0f);
        } else if (!actionHovered && editorInteraction.hovered) {
            settingsFocused = true;
            selected = 4;
        }
    }
    ImGui::End();

}

bool IsPracticePauseUiVisible()
{
    return g_pauseVisible.load();
}
