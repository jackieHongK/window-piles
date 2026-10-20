// WindowPiles.exe
//   (no args)            the app: tray icon + piles
//   --watchdog <pid>     recovery watchdog started by the app itself (same exe, no UI)
#include <windows.h>
#include <shellapi.h>

#include <winrt/base.h>

#include <string>

#include "app/app.h"
#include "common/win_util.h"
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

// The previous run died together with its watchdog: bring its hidden windows back now.
int RecoverPreviousRun() {
    wp::Journal j;
    std::string err;
    if (!wp::ReadJournal(j, err) || j.clean) return 0;
    int restored = wp::RecoverFromJournal(j, nullptr);
    wp::Journal clean;
    clean.clean = true;
    wp::WriteJournal(clean);
    return restored;
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
        MessageBoxW(nullptr, L"창 더미가 이미 실행 중이에요.\n화면 오른쪽 아래 트레이 아이콘을 확인해 주세요.", L"창 더미",
                    MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    int recovered = RecoverPreviousRun();
    SpawnWatchdog();

    int rc;
    {
        wp::App app;
        rc = app.Run(recovered);
    }
    if (mutex) CloseHandle(mutex);
    return rc;
}
