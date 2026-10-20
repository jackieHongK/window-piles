#include "work_frame.h"

#include <algorithm>
#include <string>

#include "common/canvas.h"
#include "common/win_util.h"
#include "ui/theme.h"

namespace wp {

namespace {

constexpr int kEdge = 4;      // frame thickness (DIP)
constexpr int kTabH = 30;     // tab height (DIP)
constexpr int kTabMargin = 10;  // shadow room around the tab (DIP)

void RegisterClasses() {
    static bool done = false;
    if (done) return;
    WNDCLASSW e{};
    e.lpfnWndProc = DefWindowProcW;
    e.hInstance = GetModuleHandleW(nullptr);
    e.lpszClassName = L"WindowPilesFrameEdge";
    RegisterClassW(&e);
    WNDCLASSW t{};
    t.lpfnWndProc = DefWindowProcW;
    t.hInstance = GetModuleHandleW(nullptr);
    t.hCursor = LoadCursorW(nullptr, IDC_HAND);
    t.lpszClassName = L"WindowPilesFrameTab";
    RegisterClassW(&t);
    WNDCLASSW g{};
    g.style = CS_HREDRAW | CS_VREDRAW;
    g.lpfnWndProc = DefWindowProcW;
    g.hInstance = GetModuleHandleW(nullptr);
    g.lpszClassName = L"WindowPilesGhost";
    RegisterClassW(&g);
    done = true;
}

HWND MakeEdge(COLORREF color) {
    HWND h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             L"WindowPilesFrameEdge", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)color);
    SetLayeredWindowAttributes(h, 0, 235, LWA_ALPHA);
    return h;
}

void PaintSolid(HWND h, COLORREF color, COLORREF border = CLR_INVALID, int borderPx = 0) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    HBRUSH b = CreateSolidBrush(color);
    FillRect(dc, &rc, b);
    DeleteObject(b);
    if (border != CLR_INVALID && borderPx > 0) {
        HBRUSH bb = CreateSolidBrush(border);
        for (int i = 0; i < borderPx; ++i) {
            RECT r{rc.left + i, rc.top + i, rc.right - i, rc.bottom - i};
            FrameRect(dc, &r, bb);
        }
        DeleteObject(bb);
    }
    EndPaint(h, &ps);
}

}  // namespace

WorkFrame::WorkFrame(Pile& pile, PileHost& host) : pile_(pile), host_(host) {
    RegisterClasses();
    Build();
}

WorkFrame::~WorkFrame() { Destroy(); }

void WorkFrame::Destroy() {
    for (auto& p : parts_) {
        for (HWND e : p.edges)
            if (e) DestroyWindow(e);
        if (p.tab) {
            SetWindowLongPtrW(p.tab, GWLP_USERDATA, 0);
            DestroyWindow(p.tab);
        }
    }
    parts_.clear();
}

void WorkFrame::Build() {
    Destroy();
    // Every monitor that shows one of the pile's windows; the pile's own monitor as a fallback.
    std::vector<HMONITOR> mons;
    for (auto& m : pile_.members) {
        if (m.hidden || !IsWindowVisible(m.hwnd) || IsIconic(m.hwnd)) continue;
        HMONITOR mon = MonitorFromWindow(m.hwnd, MONITOR_DEFAULTTONEAREST);
        if (std::find(mons.begin(), mons.end(), mon) == mons.end()) mons.push_back(mon);
    }
    if (mons.empty()) mons.push_back(MonitorFromPoint(pile_.pos, MONITOR_DEFAULTTONEAREST));

    for (HMONITOR mon : mons) {
        Part p;
        p.monitor = mon;
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(mon, &mi);
        p.monitorRect = mi.rcMonitor;
        p.dpi = DpiForPoint({mi.rcMonitor.left + 1, mi.rcMonitor.top + 1});
        for (auto& e : p.edges) {
            e = MakeEdge(pile_.color);
            SetWindowLongPtrW(e, GWLP_WNDPROC, (LONG_PTR)&WorkFrame::EdgeProc);
        }
        p.tab = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                L"WindowPilesFrameTab", L"작업 중인 더미", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
        SetWindowLongPtrW(p.tab, GWLP_USERDATA, (LONG_PTR)this);
        SetWindowLongPtrW(p.tab, GWLP_WNDPROC, (LONG_PTR)&WorkFrame::TabProc);
        parts_.push_back(p);
    }
    Update();
}

void WorkFrame::Update() {
    // Rebuild when the set of monitors changed (a window moved to another screen).
    std::vector<HMONITOR> mons;
    for (auto& m : pile_.members)
        if (!m.hidden && IsWindowVisible(m.hwnd) && !IsIconic(m.hwnd)) {
            HMONITOR mon = MonitorFromWindow(m.hwnd, MONITOR_DEFAULTTONEAREST);
            if (std::find(mons.begin(), mons.end(), mon) == mons.end()) mons.push_back(mon);
        }
    bool same = !mons.empty() && mons.size() == parts_.size();
    for (auto& p : parts_)
        if (std::find(mons.begin(), mons.end(), p.monitor) == mons.end()) same = false;
    if (!mons.empty() && !same) {
        Build();
        return;
    }

    for (auto& p : parts_) {
        const RECT& r = p.monitorRect;
        int t = Scale(kEdge, p.dpi);
        RECT rects[4] = {{r.left, r.top, r.right, r.top + t},
                         {r.left, r.bottom - t, r.right, r.bottom},
                         {r.left, r.top + t, r.left + t, r.bottom - t},
                         {r.right - t, r.top + t, r.right, r.bottom - t}};
        for (int i = 0; i < 4; ++i) {
            SetWindowLongPtrW(p.edges[i], GWLP_USERDATA, (LONG_PTR)pile_.color);
            SetWindowPos(p.edges[i], HWND_TOPMOST, rects[i].left, rects[i].top, rects[i].right - rects[i].left,
                         rects[i].bottom - rects[i].top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            InvalidateRect(p.edges[i], nullptr, TRUE);
        }
        RenderTab(p);
        SetWindowPos(p.tab, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

void WorkFrame::RenderTab(Part& p) {
    using namespace theme;
    auto S = [&](int v) { return Scale(v, p.dpi); };
    int open = 0;
    for (auto& m : pile_.members) open += !m.hidden;
    std::wstring name = pile_.name;
    std::wstring status = L"작업 중 · 창 " + std::to_wstring(open) + L"개";
    std::wstring fold = L"접기 ▴";
    int nameW = std::min<int>(MeasureUiText(name, S(12), true).cx, S(260));
    int statusW = MeasureUiText(status, S(12), false).cx;
    int foldW = MeasureUiText(fold, S(12), true).cx + S(18);
    int w = S(14) + S(8) + S(8) + nameW + S(10) + statusW + S(12) + foldW + S(6);
    int h = S(kTabH), m = S(kTabMargin);

    Canvas c(w + 2 * m, h + m);
    int radius = S(10);
    RECT tab{m, -radius, m + w, h};  // extends above the screen: only the bottom corners are round
    COLORREF bg = hover_ ? Mix(pile_.color, RGB(0, 0, 0), 0.12) : pile_.color;
    c.Shadow({tab.left, 0, tab.right, tab.bottom}, radius, S(8), S(2), 70);
    c.FillRoundRect(tab, radius, bg);
    if (hot_) c.StrokeRoundRect({tab.left, tab.top, tab.right, tab.bottom}, radius, S(2), kPrimaryFg);
    COLORREF fg = ContrastText(pile_.color);
    int x = tab.left + S(14);
    // status dot
    c.FillRoundRect({x, h / 2 - S(4), x + S(8), h / 2 + S(4)}, S(4), fg, 230);
    x += S(8) + S(8);
    c.Text(name, {x, 0, x + nameW, h}, fg, S(12), true, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    x += nameW + S(10);
    c.Text(status, {x, 0, x + statusW, h}, fg, S(12), false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    x += statusW + S(12);
    RECT foldR{x, S(5), x + foldW, h - S(5)};
    c.FillRoundRect(foldR, S(8), fg == kForeground ? RGB(0, 0, 0) : RGB(255, 255, 255), hover_ ? 70 : 45);
    c.Text(fold, foldR, fg, S(12), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    const RECT& r = p.monitorRect;
    int tx = (r.left + r.right - w) / 2;
    p.tabRect = {tx, r.top, tx + w, r.top + h};
    p.tabPos = {tx - m, r.top};
    c.Present(p.tab, p.tabPos);
}

bool WorkFrame::HitTab(POINT pt) const {
    for (auto& p : parts_)
        if (PtInRect(&p.tabRect, pt)) return true;
    return false;
}

void WorkFrame::SetHot(bool hot) {
    if (hot == hot_) return;
    hot_ = hot;
    for (auto& p : parts_) RenderTab(p);
}

LRESULT CALLBACK WorkFrame::EdgeProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        PaintSolid(h, (COLORREF)GetWindowLongPtrW(h, GWLP_USERDATA));
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK WorkFrame::TabProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (WorkFrame*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_MOUSEMOVE:
            if (!self->hover_) {
                self->hover_ = true;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
                TrackMouseEvent(&tme);
                for (auto& p : self->parts_) self->RenderTab(p);
            }
            return 0;
        case WM_MOUSELEAVE:
            self->hover_ = false;
            for (auto& p : self->parts_) self->RenderTab(p);
            return 0;
        case WM_LBUTTONUP: {
            DWORD now = GetTickCount();
            if (now - self->lastClickTick_ > GetDoubleClickTime()) {
                self->lastClickTick_ = now;
                // May destroy this frame: nothing may touch `self` afterwards.
                self->host_.OnPileClick(self->pile_);
            }
            return 0;
        }
        case WM_RBUTTONUP: {
            POINT c;
            GetCursorPos(&c);
            self->host_.OnPileMenu(self->pile_, c);
            return 0;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------------------------------------

namespace {

LRESULT CALLBACK GhostProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        COLORREF c = (COLORREF)GetWindowLongPtrW(h, GWLP_USERDATA);
        PaintSolid(h, theme::Mix(c, RGB(255, 255, 255), 0.82), c, 2);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

void AnimateRects(const std::vector<RECT>& from, const std::vector<RECT>& to, COLORREF color, int durationMs) {
    BOOL animate = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0);
    if (!animate || from.empty() || from.size() != to.size()) return;
    RegisterClasses();
    std::vector<HWND> ghosts;
    for (size_t i = 0; i < from.size(); ++i) {
        HWND g = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                 L"WindowPilesGhost", L"", WS_POPUP, from[i].left, from[i].top,
                                 from[i].right - from[i].left, from[i].bottom - from[i].top, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
        SetWindowLongPtrW(g, GWLP_USERDATA, (LONG_PTR)color);
        SetWindowLongPtrW(g, GWLP_WNDPROC, (LONG_PTR)&GhostProc);
        SetLayeredWindowAttributes(g, 0, 165, LWA_ALPHA);
        ShowWindow(g, SW_SHOWNOACTIVATE);
        ghosts.push_back(g);
    }
    double start = NowMs();
    for (;;) {
        double t = std::min(1.0, (NowMs() - start) / durationMs);
        double e = 1 - (1 - t) * (1 - t) * (1 - t);  // ease-out cubic
        HDWP dwp = BeginDeferWindowPos((int)ghosts.size());
        for (size_t i = 0; i < ghosts.size(); ++i) {
            auto L = [e](LONG a, LONG b) { return (int)(a + (b - a) * e); };
            int l = L(from[i].left, to[i].left), tp = L(from[i].top, to[i].top);
            int r = L(from[i].right, to[i].right), b = L(from[i].bottom, to[i].bottom);
            if (dwp) dwp = DeferWindowPos(dwp, ghosts[i], HWND_TOPMOST, l, tp, std::max(1, r - l), std::max(1, b - tp),
                                          SWP_NOACTIVATE);
        }
        if (dwp) EndDeferWindowPos(dwp);
        for (HWND g : ghosts) UpdateWindow(g);
        if (t >= 1) break;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 12, QS_PAINT);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, WM_PAINT, WM_PAINT, PM_REMOVE)) DispatchMessageW(&msg);
    }
    for (HWND g : ghosts) DestroyWindow(g);
}

}  // namespace wp
