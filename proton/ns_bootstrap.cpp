// ns_bootstrap.dll - the persistent-thread half of the Proton injection chain.
//
// A CreateRemoteThread(LoadLibraryW) call loads the target DLL on a thread that EXITS as soon
// as LoadLibraryW returns - and any window/UI the mod's DllMain created on that thread is
// destroyed with it ("menu flashed for a split second, never comes back" - live-verified on
// PlayGTAV.exe 2026-09-14). The fix: inject THIS tiny bootstrap instead. Its DllMain spawns a
// worker thread that reads the real DLL path from a handoff file, loads it, and then sleeps
// FOREVER on that same thread - the mod's load thread never exits, so load-thread UI survives.
//
// Handoff contract with ns_inject.exe: the target DLL path is written (UTF-16LE wide chars)
// to Z:\tmp\ns_bootstrap_target.txt BEFORE the bootstrap is injected; the worker deletes it
// after reading. Poll-retry loop covers the file being flushed late.

#include <windows.h>
#include <cstdio>

static DWORD WINAPI worker(LPVOID)
{
    // wait up to 10s for the handoff file (ns_inject writes it just before injecting us)
    for (int attempt = 0; attempt < 100; ++attempt) {
        FILE *f = nullptr;
        if (_wfopen_s(&f, L"Z:\\tmp\\ns_bootstrap_target.txt", L"rb") == 0 && f) {
            wchar_t path[512]{};
            const std::size_t read = std::fread(path, sizeof(wchar_t), 511, f);
            std::fclose(f);
            DeleteFileW(L"Z:\\tmp\\ns_bootstrap_target.txt");
            if (read > 0) {
                path[read] = L'\0';
                LoadLibraryW(path);
                // hold THIS thread forever: the mod was loaded on it, and any UI the mod's
                // DllMain created on its loading thread must outlive the load
                for (;;)
                    Sleep(60000);
            }
        }
        Sleep(100);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        CreateThread(nullptr, 0, &worker, nullptr, 0, nullptr);
    return TRUE;
}
