#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include "../version.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr wchar_t kGameExeName[] = L"th06nc.exe";
constexpr wchar_t kOverlayDllName[] = L"th06nc_test.dll";
constexpr wchar_t kSteamUrl[] = L"steam://rungameid/4659620";

std::wstring Win32Error(DWORD error = GetLastError())
{
    wchar_t* text = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(flags, nullptr, error, 0,
        reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::wstring result = length && text ? text : L"unknown error";
    if (text)
        LocalFree(text);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n'))
        result.pop_back();
    return result;
}

std::filesystem::path ModuleDirectory()
{
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
        static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

bool IsTarget64Bit(HANDLE process)
{
    using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    const auto fn = reinterpret_cast<IsWow64Process2Fn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
    if (fn) {
        USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        return fn(process, &processMachine, &nativeMachine) &&
            processMachine == IMAGE_FILE_MACHINE_UNKNOWN &&
            (nativeMachine == IMAGE_FILE_MACHINE_AMD64 || nativeMachine == IMAGE_FILE_MACHINE_ARM64);
    }

    BOOL wow64 = FALSE;
    return IsWow64Process(process, &wow64) && !wow64 && sizeof(void*) == 8;
}

std::optional<uintptr_t> RemoteModuleBase(DWORD pid, const wchar_t* moduleName)
{
    HANDLE snapshot = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 5; ++attempt) {
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snapshot != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH)
            break;
        Sleep(10);
    }
    if (snapshot == INVALID_HANDLE_VALUE)
        return std::nullopt;

    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    std::optional<uintptr_t> result;
    if (Module32FirstW(snapshot, &module)) {
        do {
            if (_wcsicmp(module.szModule, moduleName) == 0) {
                result = reinterpret_cast<uintptr_t>(module.modBaseAddr);
                break;
            }
        } while (Module32NextW(snapshot, &module));
    }
    CloseHandle(snapshot);
    return result;
}

bool IsAlreadyInjected(DWORD pid)
{
    return RemoteModuleBase(pid, kOverlayDllName).has_value();
}

bool InjectDll(DWORD pid, const std::filesystem::path& dllPath, std::wstring& error)
{
    const DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
    const HANDLE process = OpenProcess(access, FALSE, pid);
    if (!process) {
        error = L"OpenProcess: " + Win32Error();
        return false;
    }

    if (!IsTarget64Bit(process)) {
        error = L"target process is not 64-bit";
        CloseHandle(process);
        return false;
    }

    const std::wstring dll = std::filesystem::absolute(dllPath).wstring();
    const SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remoteText = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteText) {
        error = L"VirtualAllocEx: " + Win32Error();
        CloseHandle(process);
        return false;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remoteText, dll.c_str(), bytes, &written) || written != bytes) {
        error = L"WriteProcessMemory: " + Win32Error();
        VirtualFreeEx(process, remoteText, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    const HMODULE localKernel32 = GetModuleHandleW(L"kernel32.dll");
    const auto localLoadLibrary = reinterpret_cast<uintptr_t>(
        GetProcAddress(localKernel32, "LoadLibraryW"));
    HMODULE localOwner = nullptr;
    if (!localLoadLibrary || !GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(localLoadLibrary), &localOwner)) {
        error = L"could not resolve remote LoadLibraryW";
        VirtualFreeEx(process, remoteText, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    wchar_t ownerPath[32768]{};
    if (!GetModuleFileNameW(localOwner, ownerPath, static_cast<DWORD>(std::size(ownerPath)))) {
        error = L"could not identify the module containing LoadLibraryW";
        VirtualFreeEx(process, remoteText, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }
    const std::wstring ownerName = std::filesystem::path(ownerPath).filename().wstring();
    const auto remoteOwner = RemoteModuleBase(pid, ownerName.c_str());
    if (!remoteOwner) {
        error = L"could not find " + ownerName + L" in the target process";
        VirtualFreeEx(process, remoteText, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    const uintptr_t loadLibraryRva = localLoadLibrary - reinterpret_cast<uintptr_t>(localOwner);
    const auto remoteLoadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(*remoteOwner + loadLibraryRva);
    const HANDLE thread = CreateRemoteThread(process, nullptr, 0, remoteLoadLibrary, remoteText, 0, nullptr);
    if (!thread) {
        error = L"CreateRemoteThread: " + Win32Error();
        VirtualFreeEx(process, remoteText, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    const DWORD wait = WaitForSingleObject(thread, 10000);
    DWORD remoteResult = 0;
    GetExitCodeThread(thread, &remoteResult);
    CloseHandle(thread);
    VirtualFreeEx(process, remoteText, 0, MEM_RELEASE);
    CloseHandle(process);

    if (wait != WAIT_OBJECT_0) {
        error = L"remote LoadLibraryW timed out";
        return false;
    }
    if (remoteResult == 0) {
        error = L"LoadLibraryW failed in the game process (check DLL dependencies)";
        return false;
    }
    return true;
}

std::vector<DWORD> FindGameProcesses()
{
    std::vector<DWORD> result;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return result;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, kGameExeName) == 0)
                result.push_back(entry.th32ProcessID);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

bool TryAttach(const std::filesystem::path& dllPath, DWORD onlyPid = 0)
{
    for (const DWORD pid : FindGameProcesses()) {
        if (onlyPid && pid != onlyPid)
            continue;
        if (IsAlreadyInjected(pid)) {
            std::wcout << L"th06nc_test is already loaded in PID " << pid << L".\n";
            return true;
        }

        std::wstring error;
        if (InjectDll(pid, dllPath, error)) {
            std::wcout << L"Injected th06nc_test.dll into th06nc.exe (PID " << pid << L").\n";
            std::wcout << L"Press F1 in the game to show or hide the practice window.\n";
            return true;
        }
        std::wcerr << L"Injection attempt for PID " << pid << L" failed: " << error << L"\n";
    }
    return false;
}

bool LaunchDirect(const std::filesystem::path& exePath, const std::filesystem::path& dllPath)
{
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring exe = std::filesystem::absolute(exePath).wstring();
    const std::wstring cwd = std::filesystem::absolute(exePath).parent_path().wstring();

    if (!CreateProcessW(exe.c_str(), nullptr, nullptr, nullptr, FALSE, CREATE_SUSPENDED,
            nullptr, cwd.c_str(), &startup, &process)) {
        std::wcerr << L"CreateProcessW failed: " << Win32Error() << L"\n";
        return false;
    }

    std::wstring error;
    const bool injected = InjectDll(process.dwProcessId, dllPath, error);
    if (injected) {
        ResumeThread(process.hThread);
        std::wcout << L"Started and injected th06nc.exe (PID " << process.dwProcessId << L").\n";
        std::wcout << L"Press F1 in the game to show or hide the practice window.\n";
    } else {
        std::wcerr << L"Injection failed: " << error << L"\n";
        TerminateProcess(process.hProcess, ERROR_DLL_INIT_FAILED);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return injected;
}

void PrintUsage()
{
    std::wcout <<
        L"th06nc_test launcher (x64)\n"
        L"  th06nc_test_launcher.exe                Launch local th06nc.exe if present, otherwise Steam\n"
        L"  th06nc_test_launcher.exe --attach       Attach to a running th06nc.exe\n"
        L"  th06nc_test_launcher.exe --pid <id>     Attach to a specific process\n"
        L"  th06nc_test_launcher.exe --direct <exe> Start a local exe suspended, inject, resume\n";
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    std::wcout << L"th06nc_prac_vibe version "
               << Th06ncPracVersion::WideText << L"\n";
    const std::filesystem::path dllPath = ModuleDirectory() / kOverlayDllName;
    if (!std::filesystem::is_regular_file(dllPath)) {
        std::wcerr << L"Missing overlay DLL: " << dllPath << L"\n";
        return 2;
    }

    if (argc >= 2 && (_wcsicmp(argv[1], L"--help") == 0 || _wcsicmp(argv[1], L"-h") == 0)) {
        PrintUsage();
        return 0;
    }
    if (argc >= 3 && _wcsicmp(argv[1], L"--direct") == 0)
        return LaunchDirect(argv[2], dllPath) ? 0 : 1;
    if (argc >= 3 && _wcsicmp(argv[1], L"--pid") == 0) {
        const DWORD pid = wcstoul(argv[2], nullptr, 10);
        return pid && TryAttach(dllPath, pid) ? 0 : 1;
    }
    if (argc >= 2 && _wcsicmp(argv[1], L"--attach") != 0) {
        PrintUsage();
        return 2;
    }

    const bool attachOnly = argc >= 2;
    if (TryAttach(dllPath))
        return 0;
    if (attachOnly) {
        std::wcerr << L"No injectable th06nc.exe process was found.\n";
        return 1;
    }

    const std::filesystem::path localGamePath = ModuleDirectory() / kGameExeName;
    if (std::filesystem::is_regular_file(localGamePath)) {
        // std::wcout << L"Found local th06nc.exe next to the launcher; starting it directly.\n";
        // return LaunchDirect(localGamePath, dllPath) ? 0 : 1;
        const HINSTANCE shellResult = ShellExecuteW(nullptr, L"open", localGamePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(shellResult) <= 32) {
            std::wcerr << L"Could not open " << kSteamUrl << L" (ShellExecute error "
                << reinterpret_cast<INT_PTR>(shellResult) << L").\n";
            return 1;
        }
    }else{
        const HINSTANCE shellResult = ShellExecuteW(nullptr, L"open", kSteamUrl,
            nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(shellResult) <= 32) {
            std::wcerr << L"Could not open " << kSteamUrl << L" (ShellExecute error "
                << reinterpret_cast<INT_PTR>(shellResult) << L").\n";
            return 1;
        }
    }

    
    std::wcout << L"Steam launch requested; waiting for th06nc.exe...\n";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    while (std::chrono::steady_clock::now() < deadline) {
        if (TryAttach(dllPath))
            return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    std::wcerr << L"Timed out waiting for th06nc.exe. Is Steam running and the game installed?\n";
    return 1;
}
