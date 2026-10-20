// WindowPiles.exe
//   (no args) / --autostart   the app: tray icon + piles (--autostart: launched at sign-in)
//   --watchdog <pid>           recovery watchdog started by the app itself (same exe, no UI)
#include <windows.h>
#include <shellapi.h>

#include <winrt/base.h>

#include <string>

#include "app/app.h"
#include "recovery/journal.h"

namespace {

void SpawnWatchdog() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --watchdog " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                       nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
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
    LocalFree(argv);

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\WindowPiles.Instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Already running: launching it again just opens the guide in the running instance.
        if (HWND existing = FindWindowW(wp::kMainWindowClass, nullptr)) PostMessageW(existing, wp::WM_APP_SHOW_GUIDE, 0, 0);
        return 0;
    }

    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    SpawnWatchdog();

    int rc;
    {
        wp::App app;
        rc = app.Run();
    }
    if (mutex) CloseHandle(mutex);
    return rc;
}
