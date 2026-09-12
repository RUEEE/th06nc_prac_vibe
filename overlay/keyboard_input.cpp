#include "keyboard_input.h"
#include "game_addresses.h"
#include "game_overlay.h"
#include "locale.h"
#include "imgui.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr size_t kKeyboardStateSize = 0x100;
constexpr size_t kKeyboardMetadataSize = 9;

constexpr size_t kKeyboardUpdatePrologueSize = 5;
constexpr unsigned char kExpectedKeyboardUpdatePrologue[kKeyboardUpdatePrologueSize] = {
    0x48, 0x89, 0x5C, 0x24, 0x10 // mov [rsp+10h], rbx
};
constexpr size_t kActionInputUpdatePrologueSize = 5;
constexpr unsigned char kExpectedActionInputUpdatePrologue[kActionInputUpdatePrologueSize] = {
    0x48, 0x89, 0x74, 0x24, 0x20 // mov [rsp+20h], rsi
};
constexpr size_t kKeyboardActionMergePrologueSize = 6;
constexpr unsigned char kExpectedKeyboardActionMergePrologue[
    kKeyboardActionMergePrologueSize] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x60 // push rbx / sub rsp,60h
};
constexpr uint32_t kBombAction = 0x2;
constexpr uint32_t kShootAction = 0x1;
constexpr uint32_t kMenuConfirmAction = 0x100;

enum class BindableAction : size_t {
    Up, Down, Left, Right, Slow, Shoot, Bomb, Skip, AutoShoot,
    Retry, Exit, Confirm, Count
};

enum class SocdMode : int {
    None,
    LastInputWins,
    FirstInputWins,
    Neutral,
};

struct AxisSocdState {
    bool negativeWasDown = false;
    bool positiveWasDown = false;
    int winner = 0; // -1 = negative, +1 = positive, 0 = neither.
};

constexpr size_t kBindableActionCount =
    static_cast<size_t>(BindableAction::Count);
constexpr size_t kNativeBindableActionCount =
    static_cast<size_t>(BindableAction::AutoShoot);
constexpr uint32_t kActionMasks[kNativeBindableActionCount] = {
    0x10, 0x20, 0x40, 0x80, 0x4, 0x8101, 0x202, 0x100
};
constexpr int kDefaultKeyBindings[kBindableActionCount] = {
    VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_SHIFT, 'Z', 'X', VK_CONTROL, 'V',
    'R', 'Q', VK_RETURN,
};
int g_keyBindings[kBindableActionCount] = {
    VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_SHIFT, 'Z', 'X', VK_CONTROL, 'V',
    'R', 'Q', VK_RETURN,
};

constexpr int kArrowKeyPreset[kNativeBindableActionCount] = {
    VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_SHIFT, 'Z', 'X', VK_CONTROL
};
constexpr int kWasdKeyPreset[kNativeBindableActionCount] = {
    'W', 'S', 'A', 'D', 'L', 'J', 'K', VK_OEM_1
};

enum class KeyboardHookStatusValue : LONG {
    NotInstalled,
    Installed,
    UnsupportedExecutable,
    AllocationFailed,
    PatchFailed,
};

using KeyboardUpdateFn = int(__fastcall*)(int forceUpdate);
using ActionInputUpdateFn = uint32_t(__fastcall*)();
using KeyboardActionMergeFn = uint32_t(__fastcall*)(uint32_t keyboardActions);

KeyboardUpdateFn g_originalKeyboardUpdate = nullptr;
ActionInputUpdateFn g_originalActionInputUpdate = nullptr;
KeyboardActionMergeFn g_originalKeyboardActionMerge = nullptr;
bool g_autoShootEnabled{false};
bool g_autoShooting{false};
int g_capturingBinding = -1;
SocdMode g_socdMode{SocdMode::None};
std::once_flag g_inputConfigLoadFlag;
KeyboardHookStatusValue g_keyboardHookStatus{
    KeyboardHookStatusValue::NotInstalled};
std::byte* g_moduleBase = nullptr;

const wchar_t* const kBindingConfigNames[kBindableActionCount] = {
    L"Up", L"Down", L"Left", L"Right", L"Focus", L"Shoot", L"Bomb", L"Skip",
    L"AutoShootToggle", L"Retry", L"Exit", L"Confirm",
};

int NormalizeBindingKey(int virtualKey);
bool IsAllowedBindingKey(int virtualKey);

std::wstring InputConfigPath()
{
    wchar_t appData[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData,
        static_cast<DWORD>(_countof(appData)));
    if (length == 0 || length >= _countof(appData))
        return {};

    std::wstring root(appData, length);
    const std::wstring publisher = root + L"\\shanghaialice";
    const std::wstring game = publisher + L"\\th06nc";
    CreateDirectoryW(publisher.c_str(), nullptr);
    CreateDirectoryW(game.c_str(), nullptr);
    return game + L"\\input.ini";
}

void SaveInputConfig()
{
    const std::wstring path = InputConfigPath();
    if (path.empty())
        return;

    wchar_t value[32]{};
    for (size_t i = 0; i < kBindableActionCount; ++i) {
        _snwprintf_s(value, _countof(value), _TRUNCATE, L"%d",
            g_keyBindings[i]);
        WritePrivateProfileStringW(L"Keys", kBindingConfigNames[i], value,
            path.c_str());
    }
    WritePrivateProfileStringW(L"Options", L"AutoShootEnabled", g_autoShootEnabled ? L"1" : L"0", path.c_str());
    _snwprintf_s(value, _countof(value), _TRUNCATE, L"%d", static_cast<int>(g_socdMode));
    WritePrivateProfileStringW(L"Options", L"SOCD", value, path.c_str());
}

void LoadInputConfig()
{
    std::call_once(g_inputConfigLoadFlag, [] {
        const std::wstring path = InputConfigPath();
        if (path.empty())
            return;
        for (size_t i = 0; i < kBindableActionCount; ++i) {
            const int fallback = kDefaultKeyBindings[i];
            const int loaded = GetPrivateProfileIntW(L"Keys",
                kBindingConfigNames[i], fallback, path.c_str());
            const int key = NormalizeBindingKey(loaded);
            g_keyBindings[i] = (IsAllowedBindingKey(key) ? key : fallback);
        }
        g_autoShootEnabled = (GetPrivateProfileIntW(L"Options",
            L"AutoShootEnabled", 0, path.c_str()) != 0);
        const int socd = GetPrivateProfileIntW(L"Options", L"SOCD", 0,
            path.c_str());
        if (socd >= static_cast<int>(SocdMode::None) &&
            socd <= static_cast<int>(SocdMode::Neutral))
            g_socdMode = (static_cast<SocdMode>(socd));
        // Materialize every field and replace any unsupported legacy value
        // with its per-action arrow-layout default immediately.
        SaveInputConfig();
    });
}

bool IsBindingDown(BindableAction action)
{
    const int key = g_keyBindings[static_cast<size_t>(action)];
    return IsGameProcessForeground() && key >= 0 && key < 256 &&
        (GetAsyncKeyState(key) & 0x8000) != 0;
}

bool IsBindingPressed(BindableAction action)
{
    // Do not use GetAsyncKeyState's low-order "pressed since last query" bit:
    // the game and other injected components poll the same keys and may
    // consume that process-global indication before the Pause UI sees it.
    // Track an ordinary high-bit rising edge independently for each action.
    static bool wasDown[kBindableActionCount]{};
    const size_t index = static_cast<size_t>(action);
    const bool down = IsBindingDown(action);
    const bool pressed = down && !wasDown[index];
    wasDown[index] = down;
    return pressed;
}

uint32_t ResolveSocdAxis(bool negativeDown, bool positiveDown,
    uint32_t negativeMask, uint32_t positiveMask, SocdMode mode,
    AxisSocdState& state)
{
    const bool negativePressed = negativeDown && !state.negativeWasDown;
    const bool positivePressed = positiveDown && !state.positiveWasDown;
    uint32_t result = 0;

    if (!negativeDown && !positiveDown) {
        state.winner = 0;
    } else if (negativeDown && !positiveDown) {
        state.winner = -1;
        result = negativeMask;
    } else if (!negativeDown && positiveDown) {
        state.winner = 1;
        result = positiveMask;
    } else {
        switch (mode) {
        case SocdMode::None:
            result = negativeMask | positiveMask;
            break;
        case SocdMode::LastInputWins:
            if (negativePressed != positivePressed)
                state.winner = negativePressed ? -1 : 1;
            if (state.winner < 0)
                result = negativeMask;
            else if (state.winner > 0)
                result = positiveMask;
            break;
        case SocdMode::FirstInputWins:
            if (state.winner == 0 && negativePressed != positivePressed)
                state.winner = negativePressed ? -1 : 1;
            if (state.winner < 0)
                result = negativeMask;
            else if (state.winner > 0)
                result = positiveMask;
            break;
        case SocdMode::Neutral:
            state.winner = 0;
            break;
        }
    }

    state.negativeWasDown = negativeDown;
    state.positiveWasDown = positiveDown;
    return result;
}

uint32_t __fastcall FilterKeyboardActionMerge(uint32_t keyboardActions)
{
    if (!g_originalKeyboardActionMerge)
        return keyboardActions;
    const auto* replayPlayback =
        ResolveGameAddress<uint8_t>(GameAddress::ReplayPlaybackFlag);
    if (replayPlayback && *replayPlayback != 0)
        return g_originalKeyboardActionMerge(keyboardActions);

    constexpr uint32_t remappedMask = 0x10 | 0x20 | 0x40 | 0x80 |
        0x4 | 0x8101 | 0x202 | 0x100;
    keyboardActions &= ~remappedMask;
    if (g_capturingBinding >= 0)
        return g_originalKeyboardActionMerge(keyboardActions);
    static AxisSocdState verticalState;
    static AxisSocdState horizontalState;
    static SocdMode previousMode = SocdMode::None;
    const SocdMode mode = g_socdMode;
    if (mode != previousMode) {
        verticalState = {};
        horizontalState = {};
        previousMode = mode;
    }
    keyboardActions |= ResolveSocdAxis(
        IsBindingDown(BindableAction::Up),
        IsBindingDown(BindableAction::Down),
        kActionMasks[static_cast<size_t>(BindableAction::Up)],
        kActionMasks[static_cast<size_t>(BindableAction::Down)], mode,
        verticalState);
    keyboardActions |= ResolveSocdAxis(
        IsBindingDown(BindableAction::Left),
        IsBindingDown(BindableAction::Right),
        kActionMasks[static_cast<size_t>(BindableAction::Left)],
        kActionMasks[static_cast<size_t>(BindableAction::Right)], mode,
        horizontalState);
    for (size_t i = static_cast<size_t>(BindableAction::Slow);
         i < kNativeBindableActionCount; ++i) {
        if (IsBindingDown(static_cast<BindableAction>(i)))
            keyboardActions |= kActionMasks[i];
    }
    // Confirm is an additional menu-only binding. Inject the same logical
    // 0x100 bit carried by Z/Shoot, without adding the gameplay Shoot bit.
    // This makes the configured key work in every native menu and in the
    // enhanced-practice Pause menu through the game's normal input state.
    if (IsBindingDown(BindableAction::Confirm))
        keyboardActions |= kMenuConfirmAction;
    // The original function now receives the replacement keyboard word and
    // still performs all of its native controller/joystick merging.
    return g_originalKeyboardActionMerge(keyboardActions);
}

void ClearGameKeyboardState()
{
    if (!g_moduleBase)
        return;

    // AE180 writes the raw 256-key state here. A9F20 consumes this buffer
    // immediately after the update call, so publishing zeroes represents a
    // successful frame with no keys held and also prevents sticky keys.
    std::memset(g_moduleBase + GameRva(GameAddress::KeyboardState), 0,
        kKeyboardStateSize);

    // Clear the cached modifier/keyboard-valid fields maintained by AE180's
    // fallback path as well. This keeps every keyboard consumer consistent.
    std::memset(g_moduleBase + GameRva(GameAddress::KeyboardMetadata), 0,
        kKeyboardMetadataSize);
}

int __fastcall FilterKeyboardUpdate(int forceUpdate)
{
    if (IsGameProcessForeground())
        return g_originalKeyboardUpdate(forceUpdate);

    ClearGameKeyboardState();
    return 1;
}

uint32_t __fastcall FilterActionInputUpdate()
{
    uint32_t actions = g_originalActionInputUpdate();
    const auto* replayPlayback =
        ResolveGameAddress<uint8_t>(GameAddress::ReplayPlaybackFlag);
    const bool isReplayPlayback = replayPlayback && *replayPlayback != 0;

    static bool autoShootKeyWasDown = false;
    const bool foreground = IsGameProcessForeground();
    const bool capturingBinding = g_capturingBinding >= 0;
    const bool autoShootKeyDown = IsBindingDown(BindableAction::AutoShoot);
    const bool autoShootKeyPressed =
        !capturingBinding && autoShootKeyDown && !autoShootKeyWasDown;
    autoShootKeyWasDown = autoShootKeyDown;

    if (!g_autoShootEnabled || isReplayPlayback) {
        g_autoShooting = false;
    } else {
        bool autoShooting = g_autoShooting;
        if (autoShootKeyPressed)
            autoShooting = !autoShooting;

        // A real press of the configured Shoot or Bomb key immediately
        // returns control to the player. The configured toggle key acts on
        // its rising edge and remains independent from the seven game keys.
        if (autoShooting && foreground &&
            (IsBindingDown(BindableAction::Shoot) ||
                IsBindingDown(BindableAction::Bomb)))
            autoShooting = false;

        g_autoShooting = autoShooting;
        if (autoShooting)
            actions |= kShootAction;
    }

    // Remove only live gameplay's logical Bomb bit. Menu use of the physical
    // key is untouched, and replay playback retains recorded Bomb input.
    if (IsBombInputSuppressed() && !isReplayPlayback)
        actions &= ~kBombAction;

    return actions;
}

void* AllocateNearAddress(void* target, size_t size)
{
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const uintptr_t targetAddress = reinterpret_cast<uintptr_t>(target);
    const uintptr_t minimumApplication =
        reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
    const uintptr_t maximumApplication =
        reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
    const uintptr_t reach = static_cast<uintptr_t>(std::numeric_limits<int32_t>::max()) - 0x10000;
    const uintptr_t low = targetAddress > reach
        ? (targetAddress - reach > minimumApplication ? targetAddress - reach : minimumApplication)
        : minimumApplication;
    const uintptr_t high = targetAddress + reach < maximumApplication
        ? targetAddress + reach
        : maximumApplication;
    const uintptr_t granularity = info.dwAllocationGranularity;

    uintptr_t cursor = low;
    while (cursor < high) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &memory, sizeof(memory)))
            break;
        const uintptr_t regionBase = reinterpret_cast<uintptr_t>(memory.BaseAddress);
        const uintptr_t regionEnd = regionBase + memory.RegionSize;
        if (memory.State == MEM_FREE) {
            uintptr_t candidate = cursor > regionBase ? cursor : regionBase;
            candidate = (candidate + granularity - 1) & ~(granularity - 1);
            if (candidate <= high && candidate <= regionEnd && size <= regionEnd - candidate) {
                if (void* block = VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE))
                    return block;
            }
        }
        if (regionEnd <= cursor)
            break;
        cursor = regionEnd;
    }
    return nullptr;
}

void WriteAbsoluteJump(unsigned char* destination, const void* target)
{
    // jmp qword ptr [rip+0], followed by the full 64-bit destination.
    destination[0] = 0xFF;
    destination[1] = 0x25;
    *reinterpret_cast<uint32_t*>(destination + 2) = 0;
    *reinterpret_cast<uintptr_t*>(destination + 6) = reinterpret_cast<uintptr_t>(target);
}

bool InstallKeyboardDetour(void* target)
{
    auto* targetBytes = static_cast<unsigned char*>(target);
    if (std::memcmp(targetBytes, kExpectedKeyboardUpdatePrologue,
            kKeyboardUpdatePrologueSize) != 0)
        return false;

    auto* block = static_cast<unsigned char*>(AllocateNearAddress(target, 64));
    if (!block) {
        g_keyboardHookStatus = KeyboardHookStatusValue::AllocationFailed;
        return false;
    }

    unsigned char* relay = block;
    unsigned char* trampoline = block + 16;
    WriteAbsoluteJump(relay, reinterpret_cast<void*>(FilterKeyboardUpdate));
    std::memcpy(trampoline, targetBytes, kKeyboardUpdatePrologueSize);
    WriteAbsoluteJump(trampoline + kKeyboardUpdatePrologueSize,
        targetBytes + kKeyboardUpdatePrologueSize);

    DWORD oldBlockProtection = 0;
    if (!VirtualProtect(block, 64, PAGE_EXECUTE_READ, &oldBlockProtection)) {
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), block, 64);

    const intptr_t relative = reinterpret_cast<intptr_t>(relay) -
        (reinterpret_cast<intptr_t>(targetBytes) + 5);
    if (relative < std::numeric_limits<int32_t>::min() ||
        relative > std::numeric_limits<int32_t>::max()) {
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }

    DWORD oldTargetProtection = 0;
    if (!VirtualProtect(targetBytes, kKeyboardUpdatePrologueSize,
            PAGE_EXECUTE_READWRITE, &oldTargetProtection)) {
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }

    // Publish the callable original before the detour can be reached.
    g_originalKeyboardUpdate = reinterpret_cast<KeyboardUpdateFn>(trampoline);
    targetBytes[0] = 0xE9;
    *reinterpret_cast<int32_t*>(targetBytes + 1) = static_cast<int32_t>(relative);
    FlushInstructionCache(GetCurrentProcess(), targetBytes, kKeyboardUpdatePrologueSize);
    DWORD ignored = 0;
    VirtualProtect(targetBytes, kKeyboardUpdatePrologueSize, oldTargetProtection, &ignored);
    return true;
}

bool InstallActionInputDetour(void* target)
{
    auto* targetBytes = static_cast<unsigned char*>(target);
    if (std::memcmp(targetBytes, kExpectedActionInputUpdatePrologue,
            kActionInputUpdatePrologueSize) != 0)
        return false;

    auto* block = static_cast<unsigned char*>(AllocateNearAddress(target, 64));
    if (!block) {
        g_keyboardHookStatus = KeyboardHookStatusValue::AllocationFailed;
        return false;
    }

    unsigned char* relay = block;
    unsigned char* trampoline = block + 16;
    WriteAbsoluteJump(relay, reinterpret_cast<void*>(FilterActionInputUpdate));
    std::memcpy(trampoline, targetBytes, kActionInputUpdatePrologueSize);
    WriteAbsoluteJump(trampoline + kActionInputUpdatePrologueSize,
        targetBytes + kActionInputUpdatePrologueSize);

    DWORD oldBlockProtection = 0;
    if (!VirtualProtect(block, 64, PAGE_EXECUTE_READ, &oldBlockProtection)) {
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), block, 64);

    const intptr_t relative = reinterpret_cast<intptr_t>(relay) -
        (reinterpret_cast<intptr_t>(targetBytes) + 5);
    if (relative < std::numeric_limits<int32_t>::min() ||
        relative > std::numeric_limits<int32_t>::max()) {
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }

    DWORD oldTargetProtection = 0;
    if (!VirtualProtect(targetBytes, kActionInputUpdatePrologueSize,
            PAGE_EXECUTE_READWRITE, &oldTargetProtection)) {
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }

    g_originalActionInputUpdate =
        reinterpret_cast<ActionInputUpdateFn>(trampoline);
    targetBytes[0] = 0xE9;
    *reinterpret_cast<int32_t*>(targetBytes + 1) = static_cast<int32_t>(relative);
    FlushInstructionCache(GetCurrentProcess(), targetBytes,
        kActionInputUpdatePrologueSize);
    DWORD ignored = 0;
    VirtualProtect(targetBytes, kActionInputUpdatePrologueSize,
        oldTargetProtection, &ignored);
    return true;
}

bool InstallKeyboardActionMergeDetour(void* target)
{
    auto* targetBytes = static_cast<unsigned char*>(target);
    if (std::memcmp(targetBytes, kExpectedKeyboardActionMergePrologue,
            kKeyboardActionMergePrologueSize) != 0)
        return false;

    auto* block = static_cast<unsigned char*>(AllocateNearAddress(target, 64));
    if (!block) {
        g_keyboardHookStatus = KeyboardHookStatusValue::AllocationFailed;
        return false;
    }
    unsigned char* relay = block;
    unsigned char* trampoline = block + 16;
    WriteAbsoluteJump(relay,
        reinterpret_cast<void*>(FilterKeyboardActionMerge));
    std::memcpy(trampoline, targetBytes, kKeyboardActionMergePrologueSize);
    WriteAbsoluteJump(trampoline + kKeyboardActionMergePrologueSize,
        targetBytes + kKeyboardActionMergePrologueSize);

    DWORD blockProtection = 0;
    if (!VirtualProtect(block, 64, PAGE_EXECUTE_READ, &blockProtection)) {
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }
    const intptr_t relative = reinterpret_cast<intptr_t>(relay) -
        (reinterpret_cast<intptr_t>(targetBytes) + 5);
    if (relative < std::numeric_limits<int32_t>::min() ||
        relative > std::numeric_limits<int32_t>::max()) {
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }
    DWORD targetProtection = 0;
    if (!VirtualProtect(targetBytes, kKeyboardActionMergePrologueSize,
            PAGE_EXECUTE_READWRITE, &targetProtection)) {
        VirtualFree(block, 0, MEM_RELEASE);
        return false;
    }
    g_originalKeyboardActionMerge =
        reinterpret_cast<KeyboardActionMergeFn>(trampoline);
    targetBytes[0] = 0xE9;
    *reinterpret_cast<int32_t*>(targetBytes + 1) =
        static_cast<int32_t>(relative);
    FlushInstructionCache(GetCurrentProcess(), targetBytes,
        kKeyboardActionMergePrologueSize);
    DWORD ignored = 0;
    VirtualProtect(targetBytes, kKeyboardActionMergePrologueSize,
        targetProtection, &ignored);
    return true;
}


std::map<int,const char*> keyBindDefine = {
{  VK_ESCAPE,         "esc"},
{  VK_SHIFT,          "shift"},
{  VK_CONTROL,        "ctrl"},
{  '1',               "key_1"},
{  '2',               "key_2"},
{  '3',               "key_3"},
{  '4',               "key_4"},
{  '5',               "key_5"},
{  '6',               "key_6"},
{  '7',               "key_7"},
{  '8',               "key_8"},
{  '9',               "key_9"},
{  '0',               "key_0"},
{  VK_OEM_MINUS,      "minus"},
{  VK_OEM_PLUS,       "equals"},
{  VK_BACK,           "backspace"},
{  VK_TAB,            "tab"},
{  'Q',               "key_Q"},
{  'W',               "key_W"},
{  'E',               "key_E"},
{  'R',               "key_R"},
{  'T',               "key_T"},
{  'Y',               "key_Y"},
{  'U',               "key_U"},
{  'I',               "key_I"},
{  'O',               "key_O"},
{  'P',               "key_P"},
{  VK_OEM_4,          "lbracket"},
{  VK_OEM_6,          "rbracket"},
{  VK_RETURN,         "enter"},
{  VK_LCONTROL,       "lcontrol"},
{  'A',               "key_A"},
{  'S',               "key_S"},
{  'D',               "key_D"},
{  'F',               "key_F"},
{  'G',               "key_G"},
{  'H',               "key_H"},
{  'J',               "key_J"},
{  'K',               "key_K"},
{  'L',               "key_L"},
{  VK_OEM_1,          "semicolon"},
{  VK_OEM_7,          "apostrophe"},
{  VK_OEM_3,          "grave"},
{  VK_LSHIFT,         "lshift"},
{  VK_OEM_5,          "backslash"},
{  'Z',               "key_Z"},
{  'X',               "key_X"},
{  'C',               "key_C"},
{  'V',               "key_V"},
{  'B',               "key_B"},
{  'N',               "key_N"},
{  'M',               "key_M"},
{  VK_OEM_COMMA,      "comma"},
{  VK_OEM_PERIOD,     "period"},
{  VK_OEM_2,          "slash"},
{  VK_RSHIFT,         "rshift"},
{  VK_MULTIPLY,       "multiply"},
{  VK_LMENU,          "lmenu"},
{  VK_SPACE,          "space"},
{  VK_CAPITAL,        "capital"},
{  VK_F1,             "key_F1"},
{  VK_F2,             "key_F2"},
{  VK_F3,             "key_F3"},
{  VK_F4,             "key_F4"},
{  VK_F5,             "key_F5"},
{  VK_F6,             "key_F6"},
{  VK_F7,             "key_F7"},
{  VK_F8,             "key_F8"},
{  VK_F9,             "key_F9"},
{  VK_F10,            "key_F10"},
{  VK_NUMLOCK,        "numlock"},
{  VK_SCROLL,         "scroll"},
{  VK_NUMPAD7,        "numpad_7"},
{  VK_NUMPAD8,        "numpad_8"},
{  VK_NUMPAD9,        "numpad_9"},
{  VK_SUBTRACT,       "subtract"},
{  VK_NUMPAD4,        "numpad_4"},
{  VK_NUMPAD5,        "numpad_5"},
{  VK_NUMPAD6,        "numpad_6"},
{  VK_ADD,            "add"},
{  VK_NUMPAD1,        "numpad_1"},
{  VK_NUMPAD2,        "numpad_2"},
{  VK_NUMPAD3,        "numpad_3"},
{  VK_NUMPAD0,        "numpad_0"},
{  VK_DECIMAL,        "decimal"},
{  VK_OEM_102,        "oem_102"},
{  VK_F11,            "key_F11"},
{  VK_F12,            "key_F12"},
{  VK_F13,            "key_F13"},
{  VK_F14,            "key_F14"},
{  VK_F15,            "key_F15"},
{  VK_KANA,           "kana"},
{  VK_CONVERT,        "convert"},
{  VK_NONCONVERT,     "nonconvert"},
{  VK_RETURN,         "numpad_enter"},
{  VK_RCONTROL,       "rcontrol"},
{  VK_DIVIDE,         "divide"},
{  VK_RMENU,          "rmenu"},
{  VK_HOME,           "home"},
{  VK_UP,             "up"},
{  VK_PRIOR,          "prior"},
{  VK_LEFT,           "left"},
{  VK_RIGHT,          "right"},
{  VK_END,            "end"},
{  VK_DOWN,           "down"},
{  VK_NEXT,           "next"},
{  VK_INSERT,         "insert"},
{  VK_DELETE,         "delete"},
{  VK_LWIN,           "lwin"},
{  VK_RWIN,           "rwin"},
{  VK_APPS,           "apps"},
};

int NormalizeBindingKey(int virtualKey)
{
    if (virtualKey == VK_LSHIFT || virtualKey == VK_RSHIFT)
        return VK_SHIFT;
    if (virtualKey == VK_LCONTROL || virtualKey == VK_RCONTROL)
        return VK_CONTROL;
    return virtualKey;
}

bool IsAllowedBindingKey(int virtualKey)
{
    return keyBindDefine.contains(NormalizeBindingKey(virtualKey));
}


std::string KeyName(int virtualKey)
{
    if (keyBindDefine.contains(virtualKey)) {
        return std::string(keyBindDefine[virtualKey]);
    }
    char fallback[16]{};
    sprintf_s(fallback, "VK 0x%02X", virtualKey);
    return fallback;
}

struct KeyChoice {
    int virtualKey;
    std::string name;
};

const std::vector<KeyChoice>& KeyboardKeys()
{
    static const std::vector<KeyChoice> keys = [] {
        std::vector<KeyChoice> result;
        for (const auto& [key, name] : keyBindDefine) {
            // Generic Shift/Ctrl represent either physical side. Do not show
            // their left/right aliases as separate, misleading bindings.
            if (NormalizeBindingKey(key) != key)
                continue;
            result.push_back({key, name});
        }
        return result;
    }();
    return keys;
}

} // namespace

bool IsGameProcessForeground()
{
    const HWND foreground = GetForegroundWindow();
    if (!foreground)
        return false;

    DWORD foregroundProcessId = 0;
    GetWindowThreadProcessId(foreground, &foregroundProcessId);
    return foregroundProcessId == GetCurrentProcessId();
}

bool InstallKeyboardInputHook()
{
    LoadInputConfig();
    if (g_keyboardHookStatus == KeyboardHookStatusValue::Installed)
        return true;

    auto* base = GameModuleBase();
    if (!base) {
        g_keyboardHookStatus = (KeyboardHookStatusValue::UnsupportedExecutable);
        return false;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        g_keyboardHookStatus = (KeyboardHookStatusValue::UnsupportedExecutable);
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const size_t requiredImageSize =
        GameRva(GameAddress::KeyboardMetadata) + kKeyboardMetadataSize;
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.SizeOfImage < requiredImageSize ||
        GameRva(GameAddress::KeyboardUpdate) + kKeyboardUpdatePrologueSize >
            nt->OptionalHeader.SizeOfImage ||
        GameRva(GameAddress::ActionInputUpdate) + kActionInputUpdatePrologueSize >
            nt->OptionalHeader.SizeOfImage ||
        GameRva(GameAddress::KeyboardActionMerge) +
                kKeyboardActionMergePrologueSize >
            nt->OptionalHeader.SizeOfImage ||
        GameRva(GameAddress::ReplayPlaybackFlag) + sizeof(uint8_t) >
            nt->OptionalHeader.SizeOfImage) {
        g_keyboardHookStatus = (KeyboardHookStatusValue::UnsupportedExecutable);
        return false;
    }

    void* target = base + GameRva(GameAddress::KeyboardUpdate);
    void* actionTarget = base + GameRva(GameAddress::ActionInputUpdate);
    void* mergeTarget = base + GameRva(GameAddress::KeyboardActionMerge);
    if (std::memcmp(target, kExpectedKeyboardUpdatePrologue,
            kKeyboardUpdatePrologueSize) != 0 ||
        std::memcmp(actionTarget, kExpectedActionInputUpdatePrologue,
            kActionInputUpdatePrologueSize) != 0 ||
        std::memcmp(mergeTarget, kExpectedKeyboardActionMergePrologue,
            kKeyboardActionMergePrologueSize) != 0) {
        g_keyboardHookStatus = (KeyboardHookStatusValue::UnsupportedExecutable);
        return false;
    }

    g_moduleBase = base;
    if (!InstallKeyboardDetour(target)) {
        if (g_keyboardHookStatus != KeyboardHookStatusValue::AllocationFailed)
            g_keyboardHookStatus = (KeyboardHookStatusValue::PatchFailed);
        g_moduleBase = nullptr;
        return false;
    }
    if (!InstallActionInputDetour(actionTarget)) {
        if (g_keyboardHookStatus != KeyboardHookStatusValue::AllocationFailed)
            g_keyboardHookStatus = (KeyboardHookStatusValue::PatchFailed);
        return false;
    }
    if (!InstallKeyboardActionMergeDetour(mergeTarget)) {
        if (g_keyboardHookStatus != KeyboardHookStatusValue::AllocationFailed)
            g_keyboardHookStatus = (KeyboardHookStatusValue::PatchFailed);
        return false;
    }

    g_keyboardHookStatus = (KeyboardHookStatusValue::Installed);
    return true;
}

const char* KeyboardInputHookStatus()
{
    switch (g_keyboardHookStatus ) {
    case KeyboardHookStatusValue::Installed: return S(StatusForegroundOnly);
    case KeyboardHookStatusValue::UnsupportedExecutable:
        return S(StatusUnsupportedExecutable);
    case KeyboardHookStatusValue::AllocationFailed:
        return S(StatusX64AllocationFailed);
    case KeyboardHookStatusValue::PatchFailed: return S(StatusPatchFailed);
    default: return S(StatusNotInstalled);
    }
}

bool IsAutoShootEnabled()
{
    return g_autoShootEnabled ;
}

void SetAutoShootEnabled(bool enabled)
{
    LoadInputConfig();
    g_autoShootEnabled = (enabled);
    if (!enabled)
        g_autoShooting = (false);
    SaveInputConfig();
}

bool IsAutoShooting()
{
    return g_autoShootEnabled  && g_autoShooting ;
}

bool IsKeyBindingCaptureActive()
{
    return g_capturingBinding >= 0;
}

bool IsRetryKeyPressed()
{
    return !IsKeyBindingCaptureActive() &&
        IsBindingPressed(BindableAction::Retry);
}

bool IsExitKeyPressed()
{
    return !IsKeyBindingCaptureActive() &&
        IsBindingPressed(BindableAction::Exit);
}

void DrawKeyBindingUi()
{
    if (!ImGui::CollapsingHeader(S(KeyBindings)))
        return;

    constexpr LocaleText labels[kBindableActionCount] = {
        LocaleText::KeyUp, LocaleText::KeyDown, LocaleText::KeyLeft,
        LocaleText::KeyRight, LocaleText::KeySlow, LocaleText::KeyShoot,
        LocaleText::KeyBomb, LocaleText::KeySkip, LocaleText::KeyAutoShoot,
        LocaleText::KeyRetry, LocaleText::KeyExit, LocaleText::KeyConfirm,
    };
    const auto& keys = KeyboardKeys();
    static std::array<bool, 256> captureKeyWasDown{};

    const char* socdItems[] = {
        S(SocdNone), S(SocdLastInput), S(SocdFirstInput), S(SocdNeutral),
    };
    int socd = static_cast<int>(g_socdMode );
    ImGui::SetNextItemWidth(300.0f);
    if (ImGui::Combo(S(SocdMode), &socd, socdItems,
            static_cast<int>(_countof(socdItems)))) {
        g_socdMode = (static_cast<SocdMode>(socd));
        SaveInputConfig();
    }

    const auto applyPreset = [&](const int* preset) {
        for (size_t i = 0; i < kNativeBindableActionCount; ++i)
            g_keyBindings[i] = (preset[i]);
        g_capturingBinding = -1;
        SaveInputConfig();
    };
    if (ImGui::Button(S(ArrowKeyPreset)))
        applyPreset(kArrowKeyPreset);
    ImGui::SameLine();
    if (ImGui::Button(S(WasdKeyPreset)))
        applyPreset(kWasdKeyPreset);

    const int capturing = g_capturingBinding;
    if (capturing >= 0) {
        for (const KeyChoice& choice : keys) {
            const bool down =
                (GetAsyncKeyState(choice.virtualKey) & 0x8000) != 0;
            const bool pressed = !down &&
                captureKeyWasDown[choice.virtualKey];
            captureKeyWasDown[choice.virtualKey] = down;
            if (pressed) {
                bool is_dul = false;
                for (int i = 0; i < kBindableActionCount;i++) {
                    if (i == capturing)continue;
                    if (g_keyBindings[i] == NormalizeBindingKey(choice.virtualKey)) {
                        is_dul = true; 
                        break;
                    }
                }
                if (!is_dul)
                {
                    g_keyBindings[capturing] = NormalizeBindingKey(choice.virtualKey);
                    g_capturingBinding = -1;
                    SaveInputConfig();
                    break;
                }
            }
        }
    }

    for (size_t action = 0; action < kBindableActionCount; ++action) {
        ImGui::PushID(static_cast<int>(action));
        const int currentKey = g_keyBindings[action];
        const std::string currentName = KeyName(currentKey);
        ImGui::Text("%s", Locale::Instance().Get(labels[action]));
        ImGui::SameLine(400.0f);
        ImGui::TextDisabled(S(CurrentKey), currentName.c_str());
        ImGui::SameLine(740.0f);
        ImGui::SetNextItemWidth(230.0f);
        if (ImGui::BeginCombo("##key-combo", currentName.c_str())) {
            for (const KeyChoice& choice : keys) {
                const bool selected = choice.virtualKey == currentKey;
                if (ImGui::Selectable(choice.name.c_str(), selected)) {
                    g_keyBindings[action] = NormalizeBindingKey(choice.virtualKey);
                    SaveInputConfig();
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        const bool thisCapture =
            g_capturingBinding == static_cast<int>(action);
        if (ImGui::Button(thisCapture ? S(PressAKey) : S(ChooseKey))) {
            if (thisCapture) {
                g_capturingBinding = -1;
            } else {
                g_capturingBinding = static_cast<int>(action);
                captureKeyWasDown.fill(false);
                for (const KeyChoice& choice : keys) {
                    captureKeyWasDown[choice.virtualKey] =
                        (GetAsyncKeyState(choice.virtualKey) & 0x8000) != 0;
                }
            }
        }
        ImGui::PopID();
    }
}

void AppendKeyBindingGlyphText(std::string& output)
{
    for (const KeyChoice& choice : KeyboardKeys()) {
        output.append(choice.name);
        output.push_back('\n');
    }
}
