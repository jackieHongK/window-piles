// TestWindows.exe — deterministic test windows for PoCs and integration tests.
//
// Every window paints its left half with HarnessColor(index) and toggles its right half
// between white and black every 500 ms, so a sampler can tell "visible" (left color matches)
// from "live" (right half keeps changing). Titles are "TW-<Kind>-<index>".
// Each window runs on its own thread, so the "hung" window never blocks the others.
//
// Usage: TestWindows [--normal N] [--fixed] [--owned] [--reshow] [--nocapture] [--hung]
//                    [--maximized] [--notify] [--flash] [--notify-after MS] [--all]
//                    [--origin X Y] [--size W H] [--static] [--seconds S]
//   --notify : after MS (default 6000) the title becomes "(3) TW-Notify-N" and the window flashes,
//              like a messenger/mail app with 3 unread items
//   --flash  : after MS the window only flashes its taskbar button (no count in the title)
// Quit:  signal the named event "Local\TestWindowsQuit" or close all windows.
#include "poc_common.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace {

enum class Kind { Normal, Fixed, Owned, Reshow, NoCapture, Hung, Maximized, Notify, Flash };

const wchar_t* KindName(Kind k) {
    switch (k) {
        case Kind::Notify: return L"Notify";
        case Kind::Flash: return L"Flash";
        case Kind::Normal: return L"Normal";
        case Kind::Fixed: return L"Fixed";
        case Kind::Owned: return L"Owned";
        case Kind::Reshow: return L"Reshow";
        case Kind::NoCapture: return L"NoCapture";
        case Kind::Hung: return L"Hung";
        case Kind::Maximized: return L"Maximized";
    }
    return L"?";
}

struct WinState {
    Kind kind;
    int index;
    bool toggle = false;
    SIZE fixedSize{};
};

constexpr UINT_PTR kTimerToggle = 1;
constexpr UINT_PTR kTimerHang = 2;
constexpr UINT_PTR kTimerReshow = 3;
constexpr UINT_PTR kTimerNotify = 4;
constexpr int kReshowDelayMs = 3000;
constexpr int kHangMs = 20000;
int g_notifyMs = 6000;
bool g_static = false;  // --static: no periodic repaint (load tests: like idle app windows)

std::atomic<int> g_alive{0};
std::vector<HWND> g_windows;
CRITICAL_SECTION g_lock;

COLORREF HarnessColorSafe(const WinState* st) { return poc::HarnessColor(st ? st->index : 0); }

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = (WinState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = (CREATESTRUCTW*)lp;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            break;
        }
        case WM_TIMER:
            if (!st) break;
            if (wp == kTimerToggle) {
                st->toggle = !st->toggle;
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (wp == kTimerHang) {
                Sleep(kHangMs);  // deliberately unresponsive; the timer fires again afterwards
            } else if (wp == kTimerReshow) {
                KillTimer(hwnd, kTimerReshow);
                ShowWindow(hwnd, SW_SHOWNOACTIVATE);  // the app "brings itself back" like a messenger
            } else if (wp == kTimerNotify) {
                KillTimer(hwnd, kTimerNotify);
                // A new message arrived: unread count in the title (web/mail apps) and/or a flashing
                // taskbar button (desktop messengers) — whether or not the window is visible.
                if (st->kind == Kind::Notify) {
                    std::wstring t = L"(3) TW-Notify-" + std::to_wstring(st->index);
                    SetWindowTextW(hwnd, t.c_str());
                }
                FLASHWINFO fi{sizeof(fi), hwnd, FLASHW_ALL | FLASHW_TIMERNOFG, 0, 0};
                FlashWindowEx(&fi);
                poc::Print("NOTIFIED %s\n", poc::Hex((uint64_t)(uintptr_t)hwnd).c_str());
            }
            return 0;
        case WM_SHOWWINDOW:
            if (st && st->kind == Kind::Reshow && !wp) SetTimer(hwnd, kTimerReshow, kReshowDelayMs, nullptr);
            break;
        case WM_GETMINMAXINFO:
            if (st && st->kind == Kind::Fixed && st->fixedSize.cx) {
                auto* mmi = (MINMAXINFO*)lp;
                mmi->ptMinTrackSize = {st->fixedSize.cx, st->fixedSize.cy};
                mmi->ptMaxTrackSize = {st->fixedSize.cx, st->fixedSize.cy};
                return 0;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int mid = (rc.right - rc.left) / 2;
            RECT left = {rc.left, rc.top, mid, rc.bottom};
            RECT right = {mid, rc.top, rc.right, rc.bottom};
            HBRUSH bl = CreateSolidBrush(HarnessColorSafe(st));
            HBRUSH br = CreateSolidBrush(st && st->toggle ? RGB(0, 0, 0) : RGB(255, 255, 255));
            FillRect(dc, &left, bl);
            FillRect(dc, &right, br);
            DeleteObject(bl);
            DeleteObject(br);
            wchar_t title[128];
            GetWindowTextW(hwnd, title, 128);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            RECT tr = left;
            tr.top += 8;
            tr.left += 8;
            DrawTextW(dc, title, -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

struct Placement {
    int x, y, w, h;
};

void RunWindowThread(Kind kind, int index, Placement pl) {
    WinState st{kind, index};
    std::wstring title = std::wstring(L"TW-") + KindName(kind) + L"-" + std::to_wstring(index);

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (kind == Kind::Fixed) {
        style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;  // not resizable
        st.fixedSize = {pl.w, pl.h};
    }
    HWND hwnd = CreateWindowExW(0, L"TestWindowsClass", title.c_str(), style, pl.x, pl.y, pl.w, pl.h,
                                nullptr, nullptr, GetModuleHandleW(nullptr), &st);
    if (!hwnd) return;

    HWND owned = nullptr;
    if (kind == Kind::Owned) {
        // An owned popup ("dialog") of the main window; discovery must exclude it as its own item.
        std::wstring dlgTitle = L"TW-OwnedDialog-" + std::to_wstring(index);
        owned = CreateWindowExW(WS_EX_DLGMODALFRAME, L"TestWindowsClass", dlgTitle.c_str(),
                                WS_POPUP | WS_CAPTION | WS_SYSMENU, pl.x + 40, pl.y + 60, pl.w / 2, pl.h / 2,
                                hwnd, nullptr, GetModuleHandleW(nullptr), &st);
    }
    if (kind == Kind::NoCapture) {
        if (!SetWindowDisplayAffinity(hwnd, 0x00000011 /*WDA_EXCLUDEFROMCAPTURE*/))
            SetWindowDisplayAffinity(hwnd, WDA_MONITOR);
    }
    if (!g_static) SetTimer(hwnd, kTimerToggle, 500, nullptr);
    if (kind == Kind::Hung) SetTimer(hwnd, kTimerHang, 3000, nullptr);
    if (kind == Kind::Notify || kind == Kind::Flash) SetTimer(hwnd, kTimerNotify, g_notifyMs, nullptr);

    ShowWindow(hwnd, kind == Kind::Maximized ? SW_SHOWMAXIMIZED : SW_SHOWNOACTIVATE);
    if (owned) ShowWindow(owned, SW_SHOWNOACTIVATE);

    EnterCriticalSection(&g_lock);
    g_windows.push_back(hwnd);
    LeaveCriticalSection(&g_lock);
    poc::Print("%s %s\n", poc::Hex((uint64_t)(uintptr_t)hwnd).c_str(), poc::Utf8(title).c_str());

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    --g_alive;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    poc::InitProcess();
    InitializeCriticalSection(&g_lock);

    int normal = 0, seconds = 0;
    bool fixed = false, owned = false, reshow = false, nocapture = false, hung = false, maximized = false;
    bool notify = false, flash = false;
    int ox = -1, oy = -1, sizeW = 0, sizeH = 0;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--notify") notify = true;
        else if (a == L"--flash") flash = true;
        else if (a == L"--notify-after" && i + 1 < argc) g_notifyMs = _wtoi(argv[++i]);
        else if (a == L"--normal" && i + 1 < argc) normal = _wtoi(argv[++i]);
        else if (a == L"--fixed") fixed = true;
        else if (a == L"--owned") owned = true;
        else if (a == L"--reshow") reshow = true;
        else if (a == L"--nocapture") nocapture = true;
        else if (a == L"--hung") hung = true;
        else if (a == L"--maximized") maximized = true;
        else if (a == L"--all") { normal = std::max(normal, 3); fixed = owned = reshow = nocapture = true; }
        else if (a == L"--origin" && i + 2 < argc) { ox = _wtoi(argv[++i]); oy = _wtoi(argv[++i]); }
        else if (a == L"--seconds" && i + 1 < argc) seconds = _wtoi(argv[++i]);
        else if (a == L"--size" && i + 2 < argc) { sizeW = _wtoi(argv[++i]); sizeH = _wtoi(argv[++i]); }
        else if (a == L"--static") g_static = true;
    }
    if (!normal && !fixed && !owned && !reshow && !nocapture && !hung && !maximized && !notify && !flash) normal = 3;

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"TestWindowsClass";
    RegisterClassW(&wc);

    // Tile windows on the right part of the primary work area, away from the PoC surfaces
    // that are placed at the top-left.
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    UINT dpi = GetDpiForSystem();
    int w = MulDiv(360, dpi, 96), h = MulDiv(240, dpi, 96), gap = MulDiv(16, dpi, 96);
    if (ox < 0) ox = work.left + (work.right - work.left) / 2;
    if (oy < 0) oy = work.top + MulDiv(60, dpi, 96);

    std::vector<std::pair<Kind, int>> plan;
    int index = 1;
    for (int i = 0; i < normal; ++i) plan.push_back({Kind::Normal, index++});
    if (fixed) plan.push_back({Kind::Fixed, index++});
    if (owned) plan.push_back({Kind::Owned, index++});
    if (reshow) plan.push_back({Kind::Reshow, index++});
    if (nocapture) plan.push_back({Kind::NoCapture, index++});
    if (hung) plan.push_back({Kind::Hung, index++});
    if (maximized) plan.push_back({Kind::Maximized, index++});
    if (notify) plan.push_back({Kind::Notify, index++});
    if (flash) plan.push_back({Kind::Flash, index++});

    // --size W H (DIP): big windows, cascaded so they all stay on screen (load tests).
    bool cascade = sizeW > 0 && sizeH > 0;
    if (cascade) {
        w = MulDiv(sizeW, dpi, 96);
        h = MulDiv(sizeH, dpi, 96);
        ox = work.left + MulDiv(40, dpi, 96);
    }
    int cols = std::max(1, (int)((work.right - ox) / (w + gap)));
    std::vector<std::thread> threads;
    for (size_t i = 0; i < plan.size(); ++i) {
        int step = MulDiv(24, dpi, 96) * (int)(i % 12);
        Placement pl = cascade ? Placement{ox + step, oy + step, w, h}
                               : Placement{ox + (int)(i % cols) * (w + gap), oy + (int)(i / cols) * (h + gap), w, h};
        ++g_alive;
        threads.emplace_back(RunWindowThread, plan[i].first, plan[i].second, pl);
    }

    HANDLE quit = CreateEventW(nullptr, TRUE, FALSE, L"Local\\TestWindowsQuit");
    ResetEvent(quit);
    const size_t expected = plan.size();
    poc::PumpUntil(
        [expected] {
            EnterCriticalSection(&g_lock);
            size_t n = g_windows.size();
            LeaveCriticalSection(&g_lock);
            return n >= expected;
        },
        5000);
    poc::Print("READY %d\n", (int)plan.size());

    double deadline = seconds > 0 ? poc::NowMs() + seconds * 1000.0 : 0;
    while (g_alive > 0) {
        if (WaitForSingleObject(quit, 100) == WAIT_OBJECT_0) break;
        if (deadline && poc::NowMs() > deadline) break;
    }

    EnterCriticalSection(&g_lock);
    for (HWND hwnd : g_windows)
        if (IsWindow(hwnd)) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    LeaveCriticalSection(&g_lock);

    // A hung window will not process WM_CLOSE; don't wait for it forever.
    double giveUp = poc::NowMs() + 2000;
    while (g_alive > 0 && poc::NowMs() < giveUp) Sleep(20);
    if (g_alive > 0) ExitProcess(0);
    for (auto& t : threads) t.join();
    return 0;
}
