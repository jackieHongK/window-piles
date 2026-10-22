// WindowPiles.exe
//   (no args) / --autostart   the app: tray icon + piles (--autostart: launched at sign-in)
//   --watchdog <pid>           recovery watchdog started by the app itself (same exe, no UI)
//   --stats                    opens "작업 기록" in the running instance
#include <windows.h>
#include <appmodel.h>
#include <shellapi.h>

#include <winrt/base.h>

#include <string>

#include "app/app.h"
#include "common/win_util.h"
#include "recovery/journal.h"

namespace {

// True when our AppData writes are silently redirected into some MSIX package's private copy
// (%LOCALAPPDATA%\Packages\<pkg>\LocalCache\Local\…). That happens to programs started from inside a
// packaged app even when they have no package identity of their own, so GetCurrentPackageFullName
// alone can't tell. Probe: create a temp file and look for it in the packages' private copies.
bool AppDataIsVirtualized() {
    wchar_t local[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) return false;
    std::wstring dir = wp::AppDataDir();
    std::wstring name = L".vprobe-" + std::to_wstring(GetCurrentProcessId());
    HANDLE probe = CreateFileW((dir + L"\\" + name).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (probe == INVALID_HANDLE_VALUE) return false;
    std::wstring packages = std::wstring(local) + L"\\Packages\\";
    bool redirected = false;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileExW((packages + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories,
                                   nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
            std::wstring copy = packages + fd.cFileName + L"\\LocalCache\\Local\\WindowPiles\\" + name;
            if (GetFileAttributesW(copy.c_str()) != INVALID_FILE_ATTRIBUTES) redirected = true;
        } while (!redirected && FindNextFileW(find, &fd));
        FindClose(find);
    }
    CloseHandle(probe);  // deleted on close
    return redirected;
}

void SpawnWatchdog() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --watchdog " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    // Break away from any job the app was started in (a launcher/terminal that kills its whole
    // process tree on exit must not take the watchdog down with the app). Not every job allows it.
    DWORD flags = CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS;
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, flags | CREATE_BREAKAWAY_FROM_JOB, nullptr,
                        nullptr, &si, &pi) &&
        !CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi))
        return;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 3 && std::wstring(argv[1]) == L"--watchdog") {
        DWORD pid = (DWORD)_wtoi(argv[2]);
        LocalFree(argv);
        return wp::RunWatchdog(pid);
    }
    bool automatic = false;  // started by Windows / the watchdog, not by the user
    bool stats = false;      // open "작업 기록" in the running instance
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--autostart" || a == L"--restarted") automatic = true;
        if (a == L"--stats") stats = true;
    }
    LocalFree(argv);
    if (stats)
        if (HWND existing = FindWindowW(wp::kMainWindowClass, nullptr)) {
            PostMessageW(existing, wp::WM_APP_SHOW_STATS, 0, 0);
            return 0;
        }

    // Started from inside another app's MSIX package (a packaged IDE, terminal or assistant)? Then
    // Windows virtualizes our AppData per package — we'd save the piles into that app's private copy
    // while the autostart instance reads the real one — and we'd die whenever that app updates.
    // Re-launch through the shell so the app always runs as a plain desktop app.
    UINT32 pkgLen = 0;
    wchar_t noRelaunch[8] = {};
    bool mayRelaunch = !GetEnvironmentVariableW(L"WINDOWPILES_NO_RELAUNCH", noRelaunch, 8) && !wp::UsingTestDataDir();
    // Automatic launches come from the Run key, Restart Manager or our watchdog — never from inside
    // another app — so they skip the probe (it walks every package folder: slow on a cold disk at sign-in).
    bool inPackage = mayRelaunch && (GetCurrentPackageFullName(&pkgLen, nullptr) != APPMODEL_ERROR_NO_PACKAGE ||
                                     (!automatic && AppDataIsVirtualized()));
    if (inPackage) {
        HANDLE once = CreateEventW(nullptr, TRUE, FALSE, L"Local\\WindowPiles.ShellRelaunch");
        bool already = GetLastError() == ERROR_ALREADY_EXISTS;  // guard against a relaunch loop
        if (!already) {
            wchar_t exe[MAX_PATH];
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            std::wstring quoted = L"\"" + std::wstring(exe) + L"\"";
            wp::Log("start: inside a package context, relaunching through the shell");
            ShellExecuteW(nullptr, L"open", L"explorer.exe", quoted.c_str(), nullptr, SW_SHOWNORMAL);
            Sleep(3000);  // keep the guard alive while the shell starts the new instance
            CloseHandle(once);
            return 0;
        }
        CloseHandle(once);
    }

    wchar_t cwd[MAX_PATH] = {};
    GetCurrentDirectoryW(MAX_PATH, cwd);
    wp::Log("start: automatic=%d package=%d cwd=%s", automatic, inPackage, wp::Utf8(cwd).c_str());
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\WindowPiles.Instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        wp::Log("start: another instance is running, exiting");
        // Already running. A user launch opens the guide in the running instance; an automatic one
        // (e.g. Windows and the watchdog both restarting after a crash) just steps aside quietly.
        if (!automatic)
            if (HWND existing = FindWindowW(wp::kMainWindowClass, nullptr)) PostMessageW(existing, wp::WM_APP_SHOW_GUIDE, 0, 0);
        return 0;
    }

    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    // Let Windows bring the app back by itself after a crash/hang, an update (Restart Manager) and
    // a Windows Update reboot. Costs nothing at runtime; a normal exit is never restarted.
    RegisterApplicationRestart(L"--autostart --restarted", 0);
    SpawnWatchdog();

    int rc;
    {
        wp::App app;
        rc = app.Run();
    }
    if (mutex) CloseHandle(mutex);
    return rc;
}
