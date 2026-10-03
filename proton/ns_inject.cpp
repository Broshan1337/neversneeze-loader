// ns_inject.exe - the Windows-side half of the Neversnooze Loader's Proton support.
// Runs INSIDE the game's Proton/Wine prefix (launched through the same Proton wine the game
// uses) and injects a DLL into a running Windows process via the classic
// OpenProcess -> VirtualAllocEx -> WriteProcessMemory -> CreateRemoteThread(LoadLibraryW) chain.
//
// Usage:
//   ns_inject.exe list
//       prints one "PID <n> <exe>" line per Windows process (the Loader parses this)
//   ns_inject.exe inject <target.exe> <Z:\path\to\mod.dll>
//       injects into the first process whose exe name matches (case-insensitive)
// Output contract for the Loader: "OK" on success, "ERR <reason>" on any failure.
//
// Deliberately single-file, no CRT surprises: -static -municode, kernel32/user32 only.

#include <windows.h>
#include <tlhelp32.h>
#include <shlwapi.h>

#include <cstdio>
#include <cwchar>

static int listProcesses()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        std::fwprintf(stderr, L"ERR snapshot failed\n");
        return 1;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            std::fwprintf(stdout, L"PID %lu %ls\n", entry.th32ProcessID, entry.szExeFile);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return 0;
}

// Case-insensitive compare: name equals the target or ends with "\target" (full path entries).
static bool exeMatches(const wchar_t *name, const wchar_t *target)
{
    const std::size_t nameLen = std::wcslen(name);
    const std::size_t targetLen = std::wcslen(target);
    if (targetLen == 0 || nameLen < targetLen)
        return false;
    if (_wcsnicmp(name + (nameLen - targetLen), target, targetLen) != 0)
        return false;
    // the char before the match must be the end of a path component
    if (nameLen == targetLen)
        return true;
    const wchar_t before = name[nameLen - targetLen - 1];
    return before == L'\\' || before == L'/';
}

static int inject(const wchar_t *target, const wchar_t *dllPath)
{
    // Fail fast on unreadable paths WITHOUT loading the DLL here: a real game mod's DllMain
    // can crash any process that loads it, and ns_inject is not the target (live-verified:
    // the old LoadLibraryW probe access-violated on a 26MB mod -> silent exit 5).
    if (GetFileAttributesW(dllPath) == INVALID_FILE_ATTRIBUTES) {
        std::fwprintf(stderr, L"ERR cannot read dll (err=%lu)\n", GetLastError());
        return 1;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        std::fwprintf(stderr, L"ERR snapshot failed\n");
        return 1;
    }

    DWORD pid = 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (exeMatches(entry.szExeFile, target)) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (pid == 0) {
        std::fwprintf(stderr, L"ERR target not found: %ls\n", target);
        return 1;
    }

    HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!process) {
        std::fwprintf(stderr, L"ERR OpenProcess failed (err=%lu)\n", GetLastError());
        return 1;
    }

    const SIZE_T pathBytes = (std::wcslen(dllPath) + 1) * sizeof(wchar_t);
    void *remote = VirtualAllocEx(process, nullptr, pathBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        std::fwprintf(stderr, L"ERR VirtualAllocEx failed (err=%lu)\n", GetLastError());
        CloseHandle(process);
        return 1;
    }
    if (!WriteProcessMemory(process, remote, dllPath, pathBytes, nullptr)) {
        std::fwprintf(stderr, L"ERR WriteProcessMemory failed (err=%lu)\n", GetLastError());
        CloseHandle(process);
        return 1;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32) {
        std::fwprintf(stderr, L"ERR no kernel32\n");
        CloseHandle(process);
        return 1;
    }
    const auto loadLibraryW = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        reinterpret_cast<void *>(GetProcAddress(kernel32, "LoadLibraryW")));
    if (!loadLibraryW) {
        std::fwprintf(stderr, L"ERR no LoadLibraryW\n");
        CloseHandle(process);
        return 1;
    }

    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibraryW, remote, 0, nullptr);
    if (!thread) {
        std::fwprintf(stderr, L"ERR CreateRemoteThread failed (err=%lu)\n", GetLastError());
        CloseHandle(process);
        return 1;
    }
    WaitForSingleObject(thread, 10000);

    DWORD exitCode = 0;
    GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);

    // LoadLibraryW returns the module handle (non-null) on success
    if (exitCode == 0) {
        std::fwprintf(stderr, L"ERR remote LoadLibraryW returned null (dll rejected or already loaded)\n");
        return 1;
    }
    std::fwprintf(stdout, L"OK injected into %lu\n", pid);
    return 0;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc >= 2 && std::wcscmp(argv[1], L"list") == 0)
        return listProcesses();
    if (argc >= 4 && std::wcscmp(argv[1], L"inject") == 0)
        return inject(argv[2], argv[3]);
    std::fwprintf(stderr, L"ERR usage: ns_inject.exe list | inject <target.exe> <Z:\\...\\mod.dll>\n");
    return 1;
}
