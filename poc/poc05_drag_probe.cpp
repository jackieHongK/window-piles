// PoC-05 — detect window drags of other apps without injection or global mouse hooks,
// and prototype the "drop bar" that appears only while a window is being dragged.
//
//   * SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART..END, OUTOFCONTEXT) — no DLL injection.
//   * While (and only while) a drag is in progress: a 30 Hz timer reads GetCursorPos.
//   * The bar shows after 250 ms and 4×SM_CXDRAG of movement, never for resizes.
//   * On MOVESIZEEND: cancelled (Esc → window back at start rect) vs moved vs dropped on bar.
//
// Usage:
//   poc_drag --selftest [--out results.md]   drives TestWindows windows with SendInput (moves the mouse!)
//   poc_drag --seconds 60                     manual: drag real app windows, get a per-app table
#include "poc_common.h"

#include <atomic>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace poc;

namespace {

constexpr UINT_PTR kTimerId = 1;
constexpr int kTickMs = 33;
constexpr int kShowDelayMs = 250;
constexpr COLORREF kBarIdle = RGB(245, 246, 250);
constexpr COLORREF kBarHot = RGB(40, 120, 240);

struct DragRecord {
    int scenario = -1;  // self-test scenario active when the drag ended
    std::string app, cls, title;
    RECT startRect{}, endRect{};
    double durationMs = 0;
    bool barShown = false;
    bool resize = false;
    bool cancelled = false;  // ended at the start rect (Esc or dropped in place)
    bool escDown = false;
    bool droppedOnBar = false;
    bool barSeenOnScreen = false;  // sampled the hot bar color on screen while hovering
};

struct DragState {
    bool active = false;
    HWND hwnd = nullptr;
    RECT startRect{};
    POINT startCursor{};
    double tStart = 0;
    bool barShown = false;
    bool hot = false;
    bool resize = false;
    bool barSeen = false;
};

HWND g_msgWnd = nullptr;
HWND g_bar = nullptr;
RECT g_barRect{};
bool g_barHot = false;
DragState g_drag;
std::vector<DragRecord> g_records;
std::atomic<int> g_events{0};
std::atomic<int> g_scenario{-1};
bool g_sampleBar = false;

// Idle cost must be measured with a pure GetMessage loop (PumpFor polls every 15 ms).
void IdleFor(int ms) {
    UINT_PTR t = SetTimer(nullptr, 0, ms, nullptr);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_TIMER && msg.hwnd == nullptr && msg.wParam == t) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    KillTimer(nullptr, t);
}

bool SameSize(const RECT& a, const RECT& b) {
    return (a.right - a.left) == (b.right - b.left) && (a.bottom - a.top) == (b.bottom - b.top);
}

LRESULT CALLBACK BarProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HBRUSH b = CreateSolidBrush(g_barHot ? kBarHot : kBarIdle);
        FillRect(dc, &rc, b);
        DeleteObject(b);
        UINT dpi = GetDpiForWindow(h);
        HFONT f = CreateFontW(-MulDiv(20, dpi, 96), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                              0, L"Malgun Gothic");
        HGDIOBJ old = SelectObject(dc, f);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, g_barHot ? RGB(255, 255, 255) : RGB(40, 40, 50));
        // Center band only: keep the sampled area (left/right quarter) a flat color.
        RECT tr = rc;
        DrawTextW(dc, g_barHot ? L"놓으면 새 더미가 생겨요" : L"＋ 새 더미", -1, &tr,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, old);
        DeleteObject(f);
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h, m, w, l);
}

void ShowBar(POINT cursor) {
    HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    UINT dpiX = 96, dpiY = 96;
    // GetDpiForMonitor lives in shcore; approximate with the system DPI for the PoC.
    dpiX = dpiY = GetDpiForSystem();
    int w = MulDiv(360, dpiX, 96), h = MulDiv(84, dpiY, 96), margin = MulDiv(24, dpiY, 96);
    int x = (mi.rcWork.left + mi.rcWork.right - w) / 2, y = mi.rcWork.bottom - h - margin;
    g_barRect = {x, y, x + w, y + h};
    if (!g_bar) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = BarProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"PocDropBar";
        RegisterClassW(&wc);
        g_bar = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                                L"PocDropBar", L"drop bar", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
        SetLayeredWindowAttributes(g_bar, 0, 240, LWA_ALPHA);
    }
    int radius = MulDiv(16, dpiX, 96);
    SetWindowRgn(g_bar, CreateRoundRectRgn(0, 0, w + 1, h + 1, radius, radius), FALSE);
    g_barHot = false;
    SetWindowPos(g_bar, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void HideBar() {
    if (g_bar) ShowWindow(g_bar, SW_HIDE);
    g_barHot = false;
}

void EndDrag(HWND hwnd) {
    if (!g_drag.active || hwnd != g_drag.hwnd) return;
    KillTimer(g_msgWnd, kTimerId);
    DragRecord r;
    r.scenario = g_scenario;
    WindowInfo wi = Describe(hwnd);
    r.app = Utf8(wi.exeName);
    r.cls = Utf8(wi.className);
    r.title = Utf8(wi.title);
    r.startRect = g_drag.startRect;
    GetWindowRect(hwnd, &r.endRect);
    r.durationMs = NowMs() - g_drag.tStart;
    r.barShown = g_drag.barShown;
    r.resize = g_drag.resize || !SameSize(r.startRect, r.endRect);
    r.cancelled = EqualRect(&r.startRect, &r.endRect) != FALSE;
    r.escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8001) != 0;
    POINT c;
    GetCursorPos(&c);
    r.droppedOnBar = g_drag.barShown && !r.cancelled && !r.resize && PtInRect(&g_barRect, c);
    r.barSeenOnScreen = g_drag.barSeen;
    g_records.push_back(r);
    Print("[drag end] %s | %s | bar=%d resize=%d cancelled=%d esc=%d dropOnBar=%d barOnScreen=%d %.0fms\n",
          r.app.c_str(), r.title.c_str(), r.barShown, r.resize, r.cancelled, r.escDown, r.droppedOnBar,
          r.barSeenOnScreen, r.durationMs);
    g_drag = DragState{};
    HideBar();
}

void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    ++g_events;
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF || !hwnd) return;
    hwnd = GetAncestor(hwnd, GA_ROOT);
    if (event == EVENT_SYSTEM_MOVESIZESTART) {
        g_drag = DragState{};
        g_drag.active = true;
        g_drag.hwnd = hwnd;
        GetWindowRect(hwnd, &g_drag.startRect);
        GetCursorPos(&g_drag.startCursor);
        g_drag.tStart = NowMs();
        SetTimer(g_msgWnd, kTimerId, kTickMs, nullptr);
    } else if (event == EVENT_SYSTEM_MOVESIZEEND) {
        EndDrag(hwnd);
    }
}

void OnTick() {
    if (!g_drag.active) return;
    if (!IsWindow(g_drag.hwnd)) {
        KillTimer(g_msgWnd, kTimerId);
        g_drag = DragState{};
        HideBar();
        return;
    }
    RECT now;
    GetWindowRect(g_drag.hwnd, &now);
    if (!SameSize(now, g_drag.startRect)) {
        g_drag.resize = true;  // resizing, not moving: never offer the drop bar
        if (g_drag.barShown) HideBar(), g_drag.barShown = false;
        return;
    }
    POINT c;
    GetCursorPos(&c);
    int dx = c.x - g_drag.startCursor.x, dy = c.y - g_drag.startCursor.y;
    int threshold = 4 * GetSystemMetrics(SM_CXDRAG);
    if (!g_drag.barShown && !g_drag.resize && NowMs() - g_drag.tStart >= kShowDelayMs &&
        dx * dx + dy * dy >= threshold * threshold) {
        ShowBar(c);
        g_drag.barShown = true;
    }
    if (g_drag.barShown) {
        bool hot = PtInRect(&g_barRect, c) != FALSE;
        if (hot != g_barHot) {
            g_barHot = hot;
            InvalidateRect(g_bar, nullptr, TRUE);
            UpdateWindow(g_bar);
        }
        if (hot && g_sampleBar && !g_drag.barSeen) {
            int qw = (g_barRect.right - g_barRect.left) / 8;
            RECT probe{g_barRect.left + qw / 2, g_barRect.top + 8, g_barRect.left + qw, g_barRect.bottom - 8};
            Sample s = SampleScreen(probe, kBarHot, 50);
            g_drag.barSeen = s.bgFraction > 0.8;
        }
    }
}

LRESULT CALLBACK MsgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TIMER && w == kTimerId) {
        OnTick();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---- self-test input driver (runs on a worker thread so the hook thread keeps pumping) --
void MoveMouse(int x, int y) {
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = (LONG)(((double)(x - vx) * 65535.0) / (vw - 1));
    in.mi.dy = (LONG)(((double)(y - vy) * 65535.0) / (vh - 1));
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &in, sizeof(in));
}
void Button(bool down) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof(in));
}
void Key(WORD vk, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(in));
}
void Glide(POINT a, POINT b, int steps, int totalMs) {
    for (int i = 1; i <= steps; ++i) {
        MoveMouse(a.x + (b.x - a.x) * i / steps, a.y + (b.y - a.y) * i / steps);
        Sleep(totalMs / steps);
    }
}

POINT CaptionPoint(HWND h) {
    RECT r;
    GetWindowRect(h, &r);
    UINT dpi = GetDpiForWindow(h);
    int y = r.top + GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi) +
            GetSystemMetricsForDpi(SM_CYCAPTION, dpi) / 2;
    return {r.left + MulDiv(60, dpi, 96), y};
}

std::string HitTestName(HWND h, POINT p) {
    DWORD_PTR res = 0;
    SendMessageTimeoutW(h, WM_NCHITTEST, 0, MAKELPARAM(p.x, p.y), SMTO_ABORTIFHUNG, 500, &res);
    switch (res) {
        case HTCAPTION: return "HTCAPTION";
        case HTCLIENT: return "HTCLIENT";
        case HTBOTTOMRIGHT: return "HTBOTTOMRIGHT";
        default: return std::to_string(res);
    }
}

struct Scenario {
    const char* name;
    const char* expect;
};

}  // namespace

int wmain(int argc, wchar_t** argv) {
    InitProcess();
    bool selftest = false;
    int seconds = 60;
    std::wstring outPath;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--selftest") selftest = true;
        else if (a == L"--seconds" && i + 1 < argc) seconds = _wtoi(argv[++i]);
        else if (a == L"--out" && i + 1 < argc) outPath = argv[++i];
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = MsgProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"PocDragMsg";
    RegisterClassW(&wc);
    g_msgWnd = CreateWindowExW(0, L"PocDragMsg", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);

    HWINEVENTHOOK hook = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND, nullptr, OnWinEvent, 0, 0,
                                         WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    std::string md = "## PoC-05 — 창 끌기 감지 + 놓는 곳 바\n\n- OS: " + Utf8(OsVersionString()) + "\n";
    char buf[512];

    // Idle cost with the hook installed and no drags happening.
    {
        double cpu0 = ProcessCpuMs(), t0 = NowMs();
        ULONG64 cyc0 = 0, cyc1 = 0;
        QueryProcessCycleTime(GetCurrentProcess(), &cyc0);
        int ev0 = g_events;
        IdleFor(selftest ? 10000 : 3000);
        QueryProcessCycleTime(GetCurrentProcess(), &cyc1);
        double wall = NowMs() - t0, cpu = ProcessCpuMs() - cpu0;
        snprintf(buf, sizeof(buf),
                 "- 대기 중 비용(훅 설치, 끌기 없음): %.1f초 동안 CPU %.2f ms (%.3f%%, 측정 단위 15.6 ms), CPU 사이클 %.2f M, "
                 "훅 이벤트 %d건, working set %.1f MB\n",
                 wall / 1000, cpu, cpu / wall * 100, (cyc1 - cyc0) / 1e6, g_events - ev0, WorkingSetBytes() / 1048576.0);
        md += buf;
    }

    if (selftest) {
        g_sampleBar = true;
        std::vector<HWND> wins = FindTopLevelByTitlePrefix(L"TW-Normal-");
        if (wins.size() < 2) {
            Print("Need TestWindows --normal 3 running.\n");
            return 2;
        }
        std::vector<WINDOWPLACEMENT> saved(wins.size(), WINDOWPLACEMENT{sizeof(WINDOWPLACEMENT)});
        for (size_t i = 0; i < wins.size(); ++i) GetWindowPlacement(wins[i], &saved[i]);
        POINT cursor0;
        GetCursorPos(&cursor0);

        HWND a = wins[0], b = wins[1];
        md += "- 제목줄 히트 테스트: " + HitTestName(a, CaptionPoint(a)) + "\n";
        const Scenario scenarios[] = {
            {"A. 창을 끌어 바에 놓기", "바 표시 O, 화면에 보임 O, 바에 놓음 O"},
            {"B. 끄는 중 Esc", "취소 O(시작 위치 복귀), 바에 놓음 X"},
            {"C. 3px만 살짝 끌기", "바 표시 X (이벤트 자체가 없어도 정상)"},
            {"D. 오른쪽 아래 모서리로 크기 조절", "크기 조절 O, 바 표시 X"},
            {"E. 일반 이동(바 밖에 놓기)", "바 표시 O, 바에 놓음 X"},
        };

        std::thread driver([&] {
            auto bringUp = [](HWND h) {
                SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                Sleep(150);
            };
            RECT work;
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            UINT dpi = GetDpiForSystem();
            POINT barCenter{(work.left + work.right) / 2, work.bottom - MulDiv(24 + 42, dpi, 96)};

            // A: drag to the bar
            g_scenario = 0;
            bringUp(a);
            POINT p = CaptionPoint(a);
            MoveMouse(p.x, p.y);
            Sleep(120);
            Button(true);
            Sleep(80);
            Glide(p, barCenter, 25, 600);
            Sleep(250);
            Button(false);
            Sleep(500);

            // B: start dragging, press Esc, release
            g_scenario = 1;
            bringUp(b);
            p = CaptionPoint(b);
            MoveMouse(p.x, p.y);
            Sleep(120);
            Button(true);
            Sleep(80);
            Glide(p, {p.x - 200, p.y + 150}, 15, 400);
            Sleep(200);
            Key(VK_ESCAPE, true);
            Sleep(60);
            Key(VK_ESCAPE, false);
            Sleep(100);
            Button(false);
            Sleep(500);

            // C: tiny drag
            g_scenario = 2;
            bringUp(b);
            p = CaptionPoint(b);
            MoveMouse(p.x, p.y);
            Sleep(120);
            Button(true);
            Sleep(80);
            Glide(p, {p.x + 3, p.y}, 3, 400);
            Sleep(300);
            Button(false);
            Sleep(500);

            // D: resize from the bottom-right corner
            g_scenario = 3;
            bringUp(b);
            RECT r;
            GetWindowRect(b, &r);
            POINT corner{r.right - 3, r.bottom - 3};
            MoveMouse(corner.x, corner.y);
            Sleep(150);
            Button(true);
            Sleep(80);
            Glide(corner, {corner.x + 120, corner.y + 80}, 15, 500);
            Sleep(200);
            Button(false);
            Sleep(500);

            // E: normal move, dropped away from the bar
            g_scenario = 4;
            bringUp(a);
            p = CaptionPoint(a);
            MoveMouse(p.x, p.y);
            Sleep(120);
            Button(true);
            Sleep(80);
            Glide(p, {p.x - 150, p.y + 60}, 15, 500);
            Sleep(300);
            Button(false);
            Sleep(400);
            MoveMouse(cursor0.x, cursor0.y);
        });
        while (driver.joinable()) {
            PumpFor(50);
            if (WaitForSingleObject(driver.native_handle(), 0) == WAIT_OBJECT_0) {
                PumpFor(600);
                driver.join();
            }
        }
        for (size_t i = 0; i < wins.size(); ++i) {
            WINDOWPLACEMENT wp = saved[i];
            if (wp.showCmd == SW_SHOWNORMAL) wp.showCmd = SW_SHOWNOACTIVATE;
            SetWindowPlacement(wins[i], &wp);
        }

        md += "\n| 시나리오 | 기대 | 바 표시 | 화면에 보임 | 크기 조절 | 취소(제자리) | Esc 감지 | 바에 놓음 | 소요 ms |\n"
              "|---|---|---|---|---|---|---|---|---|\n";
        for (size_t i = 0; i < std::size(scenarios); ++i) {
            const DragRecord* rec = nullptr;
            for (auto& r : g_records)
                if (r.scenario == (int)i) rec = &r;
            if (!rec) {
                md += std::string("| ") + scenarios[i].name + " | " + scenarios[i].expect +
                      " | 이벤트 없음(OS가 이동 루프를 시작하지 않음) | | | | | | |\n";
                continue;
            }
            const DragRecord& r = *rec;
            auto ox = [](bool v) { return v ? "O" : "X"; };
            snprintf(buf, sizeof(buf), "| %s | %s | %s | %s | %s | %s | %s | %s | %.0f |\n", scenarios[i].name,
                     scenarios[i].expect, ox(r.barShown), ox(r.barSeenOnScreen), ox(r.resize), ox(r.cancelled),
                     ox(r.escDown), ox(r.droppedOnBar), r.durationMs);
            md += buf;
        }
        snprintf(buf, sizeof(buf), "\n- 기록된 끌기 %zu건, 훅 이벤트 총 %d건\n", g_records.size(), (int)g_events);
        md += buf;
    } else {
        Print("Drag windows of any apps for %d seconds...\n", seconds);
        PumpFor(seconds * 1000);
        std::map<std::string, std::pair<int, int>> perApp;  // app -> (drags, bar shown)
        md += "\n| 앱 | 클래스 | 바 표시 | 크기 조절 | 취소 | 바에 놓음 |\n|---|---|---|---|---|---|\n";
        for (auto& r : g_records) {
            snprintf(buf, sizeof(buf), "| %s | %s | %d | %d | %d | %d |\n", r.app.c_str(), r.cls.c_str(), r.barShown,
                     r.resize, r.cancelled, r.droppedOnBar);
            md += buf;
        }
    }

    UnhookWinEvent(hook);
    if (g_bar) DestroyWindow(g_bar);
    Print("\n%s\n", md.c_str());
    if (!outPath.empty()) AppendFile(outPath, md + "\n");
    return 0;
}
