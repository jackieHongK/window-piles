// Checks app/relaunch on this PC, without touching the user's piles:
//   1. how each visible browser window would be started again (its relaunch command / profile)
//   2. a File Explorer window opened here: its folder is remembered (then it's closed again)
//   3. Store app ids read from package manifests for the exe paths given on the command line
//   4. --chrome-new-window: starting Chrome with a profile that already has a window opens a NEW window
//      (the window that appears is closed again)
// Usage: relaunch_check [--chrome-new-window] [packaged exe paths…]
#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "app/relaunch.h"
#include "common/win_util.h"

using namespace wp;

namespace {

int g_fail = 0;
void Check(bool ok, const char* what) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

std::vector<HWND> TopLevel(const wchar_t* exeName, const wchar_t* cls) {
    struct Ctx {
        const wchar_t* exe;
        const wchar_t* cls;
        std::vector<HWND> out;
    } ctx{exeName, cls, {}};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = (Ctx*)lp;
        if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER) || WindowTitle(h).empty()) return TRUE;
        if (c->cls && WindowClass(h) != c->cls) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (_wcsicmp(ProcessExeName(pid).c_str(), c->exe) == 0) c->out.push_back(h);
        return TRUE;
    }, (LPARAM)&ctx);
    return ctx.out;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool chromeTest = false;
    std::vector<std::wstring> packaged;
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--chrome-new-window") chromeTest = true;
        else packaged.push_back(argv[i]);
    }

    // 1. browsers
    std::vector<HWND> chrome = TopLevel(L"chrome.exe", L"Chrome_WidgetWin_1");
    LaunchInfo first;
    std::wstring chromeExe;
    for (HWND h : chrome) {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        std::wstring exe = ProcessExePath(pid);
        LaunchInfo info = CaptureLaunch(h, pid, exe);
        printf("chrome window: command=[%s] aumid=[%s]\n", Utf8(info.command).c_str(), Utf8(info.aumid).c_str());
        if (first.command.empty() && chromeExe.empty()) first = info, chromeExe = exe;
    }
    if (!chrome.empty())
        Check(!first.command.empty() && first.command.find(L"--profile-directory") != std::wstring::npos,
              "1) 크롬 창에서 프로필이 담긴 다시 실행 명령을 읽음");

    // 2. File Explorer: open C:\Windows, read its folder back, close it
    std::vector<HWND> before = TopLevel(L"explorer.exe", L"CabinetWClass");
    ShellExecuteW(nullptr, L"open", L"explorer.exe", L"C:\\Windows", nullptr, SW_SHOWNOACTIVATE);
    HWND opened = nullptr;
    for (int t = 0; t < 50 && !opened; ++t) {
        Sleep(100);
        for (HWND h : TopLevel(L"explorer.exe", L"CabinetWClass"))
            if (std::find(before.begin(), before.end(), h) == before.end()) opened = h;
    }
    if (opened) {
        Sleep(800);
        DWORD pid = 0;
        GetWindowThreadProcessId(opened, &pid);
        LaunchInfo info = CaptureLaunch(opened, pid, ProcessExePath(pid));
        printf("explorer window: command=[%s]\n", Utf8(info.command).c_str());
        Check(info.command.find(L"C:\\Windows") != std::wstring::npos, "2) 탐색기 창의 폴더 경로를 기억함");
        PostMessageW(opened, WM_CLOSE, 0, 0);
    } else {
        Check(false, "2) 탐색기 창이 열리지 않음");
    }

    // 3. Store app ids
    for (const std::wstring& exe : packaged) {
        std::wstring id = PackagedAppId(exe);
        printf("packaged: %s -> [%s]\n", Utf8(exe).c_str(), Utf8(id).c_str());
        Check(id.find(L'!') != std::wstring::npos, "3) 설치 폴더 매니페스트에서 Store 앱 ID를 찾음");
    }

    // 4. Chrome: a profile that already has a window still gets a new one
    if (chromeTest && !chrome.empty()) {
        std::vector<HWND> was = TopLevel(L"chrome.exe", L"Chrome_WidgetWin_1");
        Check(LaunchWindow(chromeExe, first), "4) 크롬 다시 실행 명령 시작");
        HWND fresh = nullptr;
        for (int t = 0; t < 60 && !fresh; ++t) {
            Sleep(100);
            for (HWND h : TopLevel(L"chrome.exe", L"Chrome_WidgetWin_1"))
                if (std::find(was.begin(), was.end(), h) == was.end()) fresh = h;
        }
        Check(fresh != nullptr, "4) 같은 프로필 창이 열려 있어도 새 창이 뜸");
        if (fresh) {
            Sleep(500);
            PostMessageW(fresh, WM_CLOSE, 0, 0);  // the test's own new window
        }
    }
    printf("\n%s (%d failed)\n", g_fail ? "RELAUNCH FAILED" : "RELAUNCH PASSED", g_fail);
    return g_fail;
}
