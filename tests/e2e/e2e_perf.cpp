// Load measurement for many big windows in one pile (no mouse input). Needs WindowPiles NOT running.
// Uses its own data folder (WINDOWPILES_DATA_DIR), never the user's piles.
//
//   1. 40 large windows saved as one folded pile → start the app   → time until all are put away
//   2. click the card (posted)                                     → time until all are back
//   3. click the top tab (posted)                                  → time until all are put away
//   4. logoff (WM_QUERYENDSESSION/WM_ENDSESSION)                    → time until all are back + exit
// While each step runs, the app's UI thread is pinged (WM_NULL) to measure how long it stays busy.
// Prints a result table; exit code 0 unless something did not complete.
#include "poc_common.h"

#include <psapi.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace poc;

namespace {

int g_fail = 0;
std::wstring g_bin;
HANDLE g_app = nullptr;
DWORD g_appPid = 0;

std::wstring DataDir() { return g_bin + L"\\e2e-data"; }

HANDLE Launch(const std::wstring& exe, const std::wstring& args, DWORD* pid = nullptr) {
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return nullptr;
    CloseHandle(pi.hThread);
    if (pid) *pid = pi.dwProcessId;
    return pi.hProcess;
}

HWND AppWindow(const wchar_t* cls) {
    struct Ctx {
        const wchar_t* cls;
        HWND found;
    } ctx{cls, nullptr};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = (Ctx*)lp;
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == g_appPid && IsWindowVisible(h) && WindowClass(h) == c->cls) {
            c->found = h;
            return FALSE;
        }
        return TRUE;
    }, (LPARAM)&ctx);
    return ctx.found;
}

std::string Member(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    WINDOWPLACEMENT wp{sizeof(wp)};
    GetWindowPlacement(h, &wp);
    const RECT& r = wp.rcNormalPosition;
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"hwnd\":\"%llu\",\"pid\":%lu,\"createTime\":\"%llu\",\"class\":\"TestWindowsClass\",\"exe\":\"\","
             "\"title\":\"%s\",\"hidden\":false,\"showCmd\":1,\"flags\":0,\"normal\":[%ld,%ld,%ld,%ld],\"maxPos\":[-1,-1]}",
             (unsigned long long)(uintptr_t)h, pid, (unsigned long long)ProcessCreateTime(pid),
             Utf8(WindowTitle(h)).c_str(), r.left, r.top, r.right, r.bottom);
    return buf;
}

int CountVisible(const std::vector<HWND>& ws) {
    int n = 0;
    for (HWND h : ws) n += IsWindowVisible(h) ? 1 : 0;
    return n;
}

struct Step {
    double ms = -1;      // until the condition held (-1: timed out)
    double maxBusy = 0;  // longest the app's UI thread took to answer a ping
};

// Runs until `done` holds (or timeout), pinging the app's main window to measure UI stalls.
template <class F>
Step Measure(F done, double timeoutMs) {
    Step s;
    HWND main = FindWindowW(L"WindowPilesMain", nullptr);
    double t0 = NowMs();
    while (NowMs() - t0 < timeoutMs) {
        if (done()) {
            s.ms = NowMs() - t0;
            break;
        }
        double p = NowMs();
        DWORD_PTR r = 0;
        if (main && IsWindow(main)) SendMessageTimeoutW(main, WM_NULL, 0, 0, SMTO_NORMAL, 5000, &r);
        s.maxBusy = std::max(s.maxBusy, NowMs() - p);
        PumpFor(5);
    }
    return s;
}

void Report(const char* what, const Step& s) {
    if (s.ms < 0) {
        Print("| %-34s | 시간 초과 | UI 최대 지연 %6.0f ms |\n", what, s.maxBusy);
        ++g_fail;
    } else {
        Print("| %-34s | %7.0f ms | UI 최대 지연 %6.0f ms |\n", what, s.ms, s.maxBusy);
    }
}

void Memory(const char* when) {
    PROCESS_MEMORY_COUNTERS_EX pmc{sizeof(pmc)};
    if (g_app && GetProcessMemoryInfo(g_app, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
        Print("| %-34s | private %6.1f MB | 최대 %6.1f MB |\n", when, pmc.PrivateUsage / 1048576.0,
              pmc.PeakPagefileUsage / 1048576.0);
}

void Click(HWND h) {
    RECT r;
    GetClientRect(h, &r);
    // Lower middle of the card: the stacked previews (not the title, not the "reopen" list).
    LPARAM pt = MAKELPARAM((r.right - r.left) / 2, (r.bottom - r.top) * 2 / 3);
    PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, pt);
    PostMessageW(h, WM_LBUTTONUP, 0, pt);
}

}  // namespace

// e2e_perf [count] [path\to\WindowPiles.exe]   (default: 40 windows, the exe next to this test)
int wmain(int argc, wchar_t** argv) {
    InitProcess();
    int count = argc > 1 ? _wtoi(argv[1]) : 40;
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    g_bin = self;
    g_bin = g_bin.substr(0, g_bin.find_last_of(L'\\'));
    std::wstring appExe = argc > 2 ? std::wstring(argv[2]) : g_bin + L"\\WindowPiles.exe";
    SetEnvironmentVariableW(L"WINDOWPILES_NO_RELAUNCH", L"1");
    SetEnvironmentVariableW(L"WINDOWPILES_DATA_DIR", DataDir().c_str());
    CreateDirectoryW(DataDir().c_str(), nullptr);
    CloseHandle(CreateFileW((DataDir() + L"\\firstrun.done").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr));
    DeleteFileW((DataDir() + L"\\recovery.json").c_str());
    if (FindWindowW(L"WindowPilesMain", nullptr)) {
        Print("WindowPiles is running; quit it first.\n");
        return 2;
    }

    HANDLE tw = Launch(g_bin + L"\\TestWindows.exe",
                       L"--normal " + std::to_wstring(count) + L" --size 1100 700 --static --seconds 300");
    std::vector<HWND> ws(count);
    PumpUntil([&] {
        for (int i = 0; i < count; ++i)
            if (!ws[i]) ws[i] = FindTopLevelByTitle(L"TW-Normal-" + std::to_wstring(i + 1));
        return std::all_of(ws.begin(), ws.end(), [](HWND h) { return h != nullptr; });
    }, 15000);
    if (std::any_of(ws.begin(), ws.end(), [](HWND h) { return h == nullptr; })) {
        Print("test windows did not appear\n");
        return 2;
    }
    PumpFor(1000);

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    UINT dpi = GetDpiForSystem();
    std::string members;
    for (int i = 0; i < count; ++i) members += (i ? "," : "") + Member(ws[i]);
    char head[160];
    snprintf(head, sizeof(head), "\"x\":%ld,\"y\":%ld", work.right - MulDiv(340, dpi, 96), work.top + MulDiv(80, dpi, 96));
    std::string state = std::string("{\"schema\":1,\"piles\":[{\"id\":1,\"name\":\"부하 테스트\",\"customName\":true,"
                                    "\"color\":11107891,") + head + ",\"folded\":true,\"members\":[" + members +
                        "],\"missing\":[]}]}";
    HANDLE f = CreateFileW((DataDir() + L"\\piles.json").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD n;
    WriteFile(f, state.data(), (DWORD)state.size(), &n, nullptr);
    CloseHandle(f);

    Print("창 %d개 (1100x700 DIP, 배율 %u%%)\n\n", count, dpi * 100 / 96);
    Print("| 단계                               | 걸린 시간  | 앱 응답                |\n");
    Print("|---|---|---|\n");

    // 1) start: the saved pile folds every window again
    double t0 = NowMs();
    g_app = Launch(appExe, L"--autostart", &g_appPid);
    Step s1 = Measure([&] { return FindWindowW(L"WindowPilesMain", nullptr) && CountVisible(ws) == 0; }, 30000);
    if (s1.ms >= 0) s1.ms = NowMs() - t0;
    Report("1) 시작 + 창 전부 접기", s1);
    PumpFor(1500);
    Memory("   접힌 상태 메모리");

    // 2) open the pile
    HWND card = nullptr;
    PumpUntil([&] { return (card = AppWindow(L"WindowPilesPile")) != nullptr; }, 5000);
    if (card) Click(card);
    Report("2) 펼치기 (카드 클릭)", Measure([&] { return CountVisible(ws) == count; }, 30000));
    PumpFor(1500);
    Memory("   펼친 상태 메모리");

    // 3) fold it again from the top tab
    HWND tab = nullptr;
    PumpUntil([&] { return (tab = AppWindow(L"WindowPilesFrameTab")) != nullptr; }, 5000);
    if (tab) PostMessageW(tab, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
    Report("3) 접기 (이름표 클릭)", Measure([&] { return CountVisible(ws) == 0; }, 30000));
    PumpFor(1500);
    Memory("   다시 접은 상태 메모리");

    // 4) logoff: everything comes back and the app exits
    HWND main = FindWindowW(L"WindowPilesMain", nullptr);
    DWORD_PTR res = 0;
    double t4 = NowMs();
    SendMessageTimeoutW(main, WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF, SMTO_ABORTIFHUNG, 10000, &res);
    PostMessageW(main, WM_ENDSESSION, TRUE, ENDSESSION_LOGOFF);
    Step s4 = Measure([&] { return CountVisible(ws) == count && WaitForSingleObject(g_app, 0) == WAIT_OBJECT_0; },
                      30000);
    if (s4.ms >= 0) s4.ms = NowMs() - t4;
    Report("4) 로그아웃: 전부 복원 + 종료", s4);

    CloseHandle(g_app);
    TerminateProcess(tw, 0);
    CloseHandle(tw);
    Print("\n%s (%d failed)\n", g_fail ? "PERF FAILED" : "PERF DONE", g_fail);
    return g_fail ? 1 : 0;
}
