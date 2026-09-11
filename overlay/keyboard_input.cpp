#include "keyboard_input.h"
#include "game_addresses.h"
#include "game_overlay.h"
#include "locale.h"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

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
constexpr uint32_t kBombAction = 0x2;
constexpr uint32_t kShootAction = 0x1;

enum class KeyboardHookStatusValue : LONG {
    NotInstalled,
    Installed,
    UnsupportedExecutable,
    AllocationFailed,
    PatchFailed,
};

using KeyboardUpdateFn = int(__fastcall*)(int forceUpdate);
using ActionInputUpdateFn = uint32_t(__fastcall*)();

KeyboardUpdateFn g_originalKeyboardUpdate = nullptr;
ActionInputUpdateFn g_originalActionInputUpdate = nullptr;
std::atomic<bool> g_autoShootEnabled{false};
std::atomic<bool> g_autoShooting{false};
std::atomic<KeyboardHookStatusValue> g_keyboardHookStatus{
    KeyboardHookStatusValue::NotInstalled};
std::byte* g_moduleBase = nullptr;

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

    static bool vWasDown = false;
    const bool foreground = IsGameProcessForeground();
    const bool vDown = foreground && (GetAsyncKeyState('V') & 0x8000) != 0;
    const bool vPressed = vDown && !vWasDown;
    vWasDown = vDown;

    if (!g_autoShootEnabled.load() || isReplayPlayback) {
        g_autoShooting.store(false);
    } else {
        bool autoShooting = g_autoShooting.load();
        if (vPressed)
            autoShooting = !autoShooting;

        // Match thprac's input behavior: a real Z or X press immediately
        // returns control to the player. V itself toggles on its rising edge.
        if (autoShooting && foreground &&
            ((GetAsyncKeyState('Z') & 0x8000) != 0 ||
                (GetAsyncKeyState('X') & 0x8000) != 0))
            autoShooting = false;

        g_autoShooting.store(autoShooting);
        if (autoShooting)
            actions |= kShootAction;
    }

    // Filter the logical Bomb action rather than the physical X key. Thus X
    // remains available to cancel menus, while replay recording never sees a
    // live Bomb action. Replay playback is deliberately left untouched.
    if (IsBombInputSuppressed() && replayPlayback && !isReplayPlayback)
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
        g_keyboardHookStatus.store(KeyboardHookStatusValue::AllocationFailed);
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
        g_keyboardHookStatus.store(KeyboardHookStatusValue::AllocationFailed);
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
    if (g_keyboardHookStatus.load() == KeyboardHookStatusValue::Installed)
        return true;

    auto* base = GameModuleBase();
    if (!base) {
        g_keyboardHookStatus.store(KeyboardHookStatusValue::UnsupportedExecutable);
        return false;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        g_keyboardHookStatus.store(KeyboardHookStatusValue::UnsupportedExecutable);
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
        GameRva(GameAddress::ReplayPlaybackFlag) + sizeof(uint8_t) >
            nt->OptionalHeader.SizeOfImage) {
        g_keyboardHookStatus.store(KeyboardHookStatusValue::UnsupportedExecutable);
        return false;
    }

    void* target = base + GameRva(GameAddress::KeyboardUpdate);
    void* actionTarget = base + GameRva(GameAddress::ActionInputUpdate);
    if (std::memcmp(target, kExpectedKeyboardUpdatePrologue,
            kKeyboardUpdatePrologueSize) != 0 ||
        std::memcmp(actionTarget, kExpectedActionInputUpdatePrologue,
            kActionInputUpdatePrologueSize) != 0) {
        g_keyboardHookStatus.store(KeyboardHookStatusValue::UnsupportedExecutable);
        return false;
    }

    g_moduleBase = base;
    if (!InstallKeyboardDetour(target)) {
        if (g_keyboardHookStatus.load() != KeyboardHookStatusValue::AllocationFailed)
            g_keyboardHookStatus.store(KeyboardHookStatusValue::PatchFailed);
        g_moduleBase = nullptr;
        return false;
    }
    if (!InstallActionInputDetour(actionTarget)) {
        if (g_keyboardHookStatus.load() != KeyboardHookStatusValue::AllocationFailed)
            g_keyboardHookStatus.store(KeyboardHookStatusValue::PatchFailed);
        return false;
    }

    g_keyboardHookStatus.store(KeyboardHookStatusValue::Installed);
    return true;
}

const char* KeyboardInputHookStatus()
{
    switch (g_keyboardHookStatus.load()) {
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
    return g_autoShootEnabled.load();
}

void SetAutoShootEnabled(bool enabled)
{
    g_autoShootEnabled.store(enabled);
    if (!enabled)
        g_autoShooting.store(false);
}

bool IsAutoShooting()
{
    return g_autoShootEnabled.load() && g_autoShooting.load();
}
