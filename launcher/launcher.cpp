#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include "../version.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kGameExeName[] = L"th06nc.exe";
constexpr wchar_t kOverlayDllName[] = L"th06nc_test.dll";
constexpr wchar_t kSteamUrl[] = L"steam://rungameid/4659620";

struct TargetSignature {
    const wchar_t* name;
    uintptr_t rva;
    std::array<unsigned char, 15> bytes;
    size_t size;
};

// These are also checked by the corresponding overlay hooks. Checking a few
// independent locations here prevents an unrelated x64 executable renamed to
// th06nc.exe from receiving the DLL in the first place.
constexpr TargetSignature kTargetSignatures[] = {
    {L"action-input update", 0x12BE0,
        {0x48, 0x89, 0x74, 0x24, 0x20}, 5},
    {L"player initialization", 0x3A9C0,
        {0x48, 0x89, 0x5C, 0x24, 0x10,
         0x48, 0x89, 0x6C, 0x24, 0x18,
         0x48, 0x89, 0x74, 0x24, 0x20}, 15},
    {L"keyboard update", 0xAE180,
        {0x48, 0x89, 0x5C, 0x24, 0x10}, 5},
};

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
    result += L" (Win32 error " + std::to_wstring(error) + L")";
    return result;
}

std::wstring HexAddress(uintptr_t value)
{
    std::wostringstream stream;
    stream << L"0x" << std::hex << std::uppercase << value;
    return stream.str();
}

bool QueryProcessImagePath(HANDLE process, std::wstring& imagePath)
{
    std::wstring buffer(32768, L'\0');
    DWORD length = static_cast<DWORD>(buffer.size());
    if (!QueryFullProcessImageNameW(process, 0, buffer.data(), &length))
        return false;
    buffer.resize(length);
    imagePath = std::move(buffer);
    return true;
}

std::wstring QueryProcessImagePath(DWORD pid)
{
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    std::wstring imagePath;
    QueryProcessImagePath(process, imagePath);
    CloseHandle(process);
    return imagePath;
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

std::optional<uintptr_t> RemoteModuleBase(DWORD pid, const wchar_t* moduleName,
    int maximumAttempts = 1, DWORD* failureError = nullptr, HANDLE process = nullptr)
{
    DWORD lastError = ERROR_MOD_NOT_FOUND;
    for (int attempt = 0; attempt < maximumAttempts; ++attempt) {
        if (process) {
            DWORD exitCode = STILL_ACTIVE;
            if (GetExitCodeProcess(process, &exitCode) && exitCode != STILL_ACTIVE) {
                lastError = ERROR_PROCESS_ABORTED;
                break;
            }
        }

        const HANDLE snapshot = CreateToolhelp32Snapshot(
            TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snapshot == INVALID_HANDLE_VALUE) {
            lastError = GetLastError();
        } else {
            MODULEENTRY32W module{};
            module.dwSize = sizeof(module);
            if (Module32FirstW(snapshot, &module)) {
                do {
                    if (_wcsicmp(module.szModule, moduleName) == 0) {
                        const uintptr_t result =
                            reinterpret_cast<uintptr_t>(module.modBaseAddr);
                        CloseHandle(snapshot);
                        if (failureError)
                            *failureError = ERROR_SUCCESS;
                        return result;
                    }
                } while (Module32NextW(snapshot, &module));
                lastError = ERROR_MOD_NOT_FOUND;
            } else {
                lastError = GetLastError();
            }
            CloseHandle(snapshot);
        }

        if (attempt + 1 < maximumAttempts)
            Sleep(25);
    }

    if (failureError)
        *failureError = lastError;
    return std::nullopt;
}

bool IsAlreadyInjected(DWORD pid)
{
    return RemoteModuleBase(pid, kOverlayDllName, 3).has_value();
}

bool ValidateTargetExecutable(HANDLE process, DWORD pid, std::wstring& imagePath,
    std::wstring& error)
{
    if (!QueryProcessImagePath(process, imagePath)) {
        error = L"QueryFullProcessImageNameW: " + Win32Error();
        return false;
    }

    if (_wcsicmp(std::filesystem::path(imagePath).filename().c_str(), kGameExeName) != 0) {
        error = L"target image name is not " + std::wstring(kGameExeName);
        return false;
    }
    if (!IsTarget64Bit(process)) {
        error = L"target process is not the supported 64-bit game";
        return false;
    }

    DWORD moduleError = ERROR_SUCCESS;
    const auto moduleBase = RemoteModuleBase(
        pid, kGameExeName, 40, &moduleError, process);
    if (!moduleBase) {
        DWORD exitCode = STILL_ACTIVE;
        if (GetExitCodeProcess(process, &exitCode) && exitCode != STILL_ACTIVE) {
            error = L"target process exited while waiting for its main module "
                L"(exit code " + std::to_wstring(exitCode) + L")";
        } else {
            error = L"main module " + std::wstring(kGameExeName) +
                L" did not become enumerable within 1 second: " +
                Win32Error(moduleError);
        }
        return false;
    }

    for (const TargetSignature& signature : kTargetSignatures) {
        std::array<unsigned char, 15> actual{};
        SIZE_T bytesRead = 0;
        const void* address = reinterpret_cast<const void*>(*moduleBase + signature.rva);
        if (!ReadProcessMemory(process, address, actual.data(), signature.size, &bytesRead) ||
            bytesRead != signature.size) {
            error = L"ReadProcessMemory failed while checking " +
                std::wstring(signature.name) + L" at RVA " + HexAddress(signature.rva) +
                L": " + Win32Error();
            return false;
        }
        if (!std::equal(actual.begin(), actual.begin() + signature.size,
                signature.bytes.begin())) {
            error = L"unsupported or incorrect th06nc.exe: " +
                std::wstring(signature.name) + L" signature does not match at RVA " +
                HexAddress(signature.rva);
            return false;
        }
    }
    return true;
}

bool InjectDll(DWORD pid, const std::filesystem::path& dllPath, std::wstring& targetPath,
    std::wstring& error)
{
    targetPath = QueryProcessImagePath(pid);
    const DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE;
    const HANDLE process = OpenProcess(access, FALSE, pid);
    if (!process) {
        error = L"OpenProcess: " + Win32Error();
        return false;
    }

    if (!ValidateTargetExecutable(process, pid, targetPath, error)) {
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
    DWORD ownerError = ERROR_SUCCESS;
    const auto remoteOwner = RemoteModuleBase(
        pid, ownerName.c_str(), 20, &ownerError, process);
    if (!remoteOwner) {
        error = L"could not find " + ownerName +
            L" in the target process: " + Win32Error(ownerError);
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

struct InjectionFailure {
    DWORD pid = 0;
    std::wstring targetPath;
    std::wstring reason;

    explicit operator bool() const noexcept { return pid != 0; }
};

void PrintInjectionFailure(DWORD pid, const std::wstring& targetPath,
    const std::filesystem::path& dllPath, const std::wstring& error)
{
    const std::wstring fingerprint = targetPath + L"\n" + error;
    static std::map<DWORD, std::wstring> reportedFailures;
    if (const auto found = reportedFailures.find(pid);
        found != reportedFailures.end() && found->second == fingerprint)
        return;
    reportedFailures[pid] = fingerprint;

    std::wcerr <<
        L"Injection attempt failed.\n"
        L"  Target PID:        " << pid << L"\n"
        L"  Target executable: " << (targetPath.empty() ? L"<unavailable>" : targetPath) << L"\n"
        L"  Expected filename: " << kGameExeName << L"\n"
        L"  Injection DLL:     " << std::filesystem::absolute(dllPath).wstring() << L"\n"
        L"  Reason:            " << error << L"\n";
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

bool TryAttach(const std::filesystem::path& dllPath, DWORD onlyPid = 0,
    bool reportFailures = true, InjectionFailure* lastFailure = nullptr)
{
    const std::vector<DWORD> candidates = onlyPid ?
        std::vector<DWORD>{onlyPid} : FindGameProcesses();
    std::vector<InjectionFailure> failures;
    for (const DWORD pid : candidates) {
        if (IsAlreadyInjected(pid)) {
            const std::wstring targetPath = QueryProcessImagePath(pid);
            std::wcout << L"th06nc_test is already loaded.\n"
                L"  Target PID:        " << pid << L"\n"
                L"  Target executable: " <<
                (targetPath.empty() ? L"<unavailable>" : targetPath) << L"\n"
                L"  Injection DLL:     " << std::filesystem::absolute(dllPath).wstring() << L"\n";
            return true;
        }

        std::wstring targetPath;
        std::wstring error;
        if (InjectDll(pid, dllPath, targetPath, error)) {
            std::wcout << L"Injection succeeded.\n"
                L"  Target PID:        " << pid << L"\n"
                L"  Target executable: " << targetPath << L"\n"
                L"  Injection DLL:     " << std::filesystem::absolute(dllPath).wstring() << L"\n";
            std::wcout << L"Press F9-F12 in the game to show or hide the practice window.\n";
            return true;
        }
        failures.push_back({pid, std::move(targetPath), std::move(error)});
    }

    if (!failures.empty() && lastFailure)
        *lastFailure = failures.back();
    if (reportFailures) {
        for (const InjectionFailure& failure : failures)
            PrintInjectionFailure(
                failure.pid, failure.targetPath, dllPath, failure.reason);
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

    std::wstring targetPath;
    std::wstring error;
    const bool injected = InjectDll(process.dwProcessId, dllPath, targetPath, error);
    if (injected) {
        ResumeThread(process.hThread);
        std::wcout << L"Started and injected th06nc.exe (PID " << process.dwProcessId << L").\n";
        std::wcout << L"Press F9-F12 in the game to show or hide the practice window.\n";
    } else {
        PrintInjectionFailure(process.dwProcessId, targetPath, dllPath, error);
        TerminateProcess(process.hProcess, ERROR_DLL_INIT_FAILED);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return injected;
}

struct PauseConsoleOnExit {
    ~PauseConsoleOnExit()
    {
        const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode = 0;
        if (input == INVALID_HANDLE_VALUE || !GetConsoleMode(input, &mode))
            return;
        std::wcout << L"\nPress Enter to close this window..." << std::flush;
        std::wstring ignored;
        std::getline(std::wcin, ignored);
    }
};

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
    PauseConsoleOnExit pauseConsole;
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
    bool launchedLocally = false;
    if (std::filesystem::is_regular_file(localGamePath)) {
        // std::wcout << L"Found local th06nc.exe next to the launcher; starting it directly.\n";
        // return LaunchDirect(localGamePath, dllPath) ? 0 : 1;
        const HINSTANCE shellResult = ShellExecuteW(nullptr, L"open", localGamePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(shellResult) <= 32) {
            std::wcerr << L"Could not open the local game executable.\n"
                L"  Target executable: " << std::filesystem::absolute(localGamePath).wstring() << L"\n"
                L"  ShellExecute code: " << reinterpret_cast<INT_PTR>(shellResult) << L"\n";
            return 1;
        }
        launchedLocally = true;
    } else {
        const HINSTANCE shellResult = ShellExecuteW(nullptr, L"open", kSteamUrl,
            nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(shellResult) <= 32) {
            std::wcerr << L"Could not request the Steam game launch.\n"
                L"  Steam URL:         " << kSteamUrl << L"\n"
                L"  ShellExecute code: " << reinterpret_cast<INT_PTR>(shellResult) << L"\n";
            return 1;
        }
    }

    std::wcout << (launchedLocally ?
        L"Local game launch requested; waiting for th06nc.exe...\n" :
        L"Steam game launch requested; waiting for th06nc.exe...\n");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    InjectionFailure waitFailure;
    while (std::chrono::steady_clock::now() < deadline) {
        if (TryAttach(dllPath, 0, false, &waitFailure))
            return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (waitFailure)
        PrintInjectionFailure(
            waitFailure.pid, waitFailure.targetPath, dllPath, waitFailure.reason);
    std::wcerr << L"Timed out waiting for th06nc.exe. Is Steam running and the game installed?\n";
    return 1;
}
