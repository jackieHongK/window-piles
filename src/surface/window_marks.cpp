#include "surface/window_marks.h"

#include <dwmapi.h>

#include <algorithm>

#include "common/canvas.h"
#include "common/win_util.h"
#include "ui/theme.h"

namespace wp {

namespace {

constexpr wchar_t kClass[] = L"WindowPilesMark";
constexpr UINT_PTR kTimerPlaceAll = 1;
constexpr int kTabW = 22, kTabH = 6, kTabInset = 14, kTabRadius = 3;  // DIP
constexpr BYTE kTabAlpha = 235;

void RegisterOnce() {
    static bool done = false;
    if (done) return;
    done = true;
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;  // replaced per window below (one class for marks and the timer host)
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);
}

}  // namespace

WindowMarks* WindowMarks::instance_ = nullptr;

WindowMarks::WindowMarks() {
    instance_ = this;
    RegisterOnce();
    host_ = CreateWindowExW(0, kClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (host_) SetWindowLongPtrW(host_, GWLP_WNDPROC, (LONG_PTR)Proc);
    // A virtual desktop switch cloaks the windows of the other desktops: their marks go with them.
    cloakHook_ = SetWinEventHook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr, OnEvent, 0, 0,
                                 WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

WindowMarks::~WindowMarks() {
    if (cloakHook_) UnhookWinEvent(cloakHook_);
    for (auto& [pid, hooks] : hooks_)
        for (HWINEVENTHOOK h : hooks) UnhookWinEvent(h);
    for (auto& [target, m] : marks_)
        if (m.hwnd) DestroyWindow(m.hwnd);
    if (host_) DestroyWindow(host_);
    if (instance_ == this) instance_ = nullptr;
}

LRESULT CALLBACK WindowMarks::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST: return HTTRANSPARENT;  // clicks go to the window underneath
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_TIMER:
            if (wp == kTimerPlaceAll && instance_ && h == instance_->host_) {
                KillTimer(h, kTimerPlaceAll);
                std::vector<HWND> targets;
                for (auto& [target, m] : instance_->marks_) targets.push_back(target);
                for (HWND t : targets) instance_->Place(t);
            }
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void CALLBACK WindowMarks::OnEvent(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    // Moves, hides and cloaks of the marked windows themselves (not of their parts, carets or cursors).
    if (!instance_ || idObject != OBJID_WINDOW || idChild != CHILDID_SELF || !hwnd) return;
    if (instance_->Has(hwnd)) instance_->Place(hwnd);
}

void WindowMarks::Set(const std::vector<Want>& windows) {
    for (auto it = marks_.begin(); it != marks_.end();) {
        bool wanted = std::any_of(windows.begin(), windows.end(), [&](const Want& w) { return w.hwnd == it->first; });
        if (wanted) {
            ++it;
            continue;
        }
        if (it->second.hwnd) DestroyWindow(it->second.hwnd);
        it = marks_.erase(it);
    }
    for (const Want& w : windows) {
        auto [it, added] = marks_.try_emplace(w.hwnd);
        Mark& m = it->second;
        if (added) {
            m.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"",
                                     WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (m.hwnd) SetWindowLongPtrW(m.hwnd, GWLP_WNDPROC, (LONG_PTR)Proc);
        }
        if (added || m.color != w.color) {
            m.color = w.color;
            m.dpi = 0;  // draw again
        }
        Place(w.hwnd);
    }
    UpdateHooks();
}

void WindowMarks::PlaceAllSoon() {
    if (host_ && !marks_.empty()) SetTimer(host_, kTimerPlaceAll, 150, nullptr);
}

void WindowMarks::Hide(Mark& m) {
    if (!m.shown || !m.hwnd) return;
    ShowWindow(m.hwnd, SW_HIDE);
    m.shown = false;
}

void WindowMarks::Place(HWND target) {
    auto it = marks_.find(target);
    if (it == marks_.end() || !it->second.hwnd) return;
    Mark& m = it->second;
    if (!IsWindow(target) || !IsWindowVisible(target) || IsIconic(target)) return Hide(m);
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(target, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return Hide(m);
    RECT r{};  // what is seen of the window (no invisible resize borders)
    if (FAILED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(target, &r);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi);
    // Full screen (a video, a slideshow): stay out of the way.
    if (!IsZoomed(target) && r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right &&
        r.bottom >= mi.rcMonitor.bottom)
        return Hide(m);

    UINT dpi = GetDpiForWindow(target);
    if (!dpi) dpi = 96;
    POINT pos{r.left + Scale(kTabInset, dpi), std::max(r.top, mi.rcMonitor.top)};
    if (dpi != m.dpi) {
        // A tab hanging from the window's top edge: square on top, rounded below, a darker rim for light windows.
        m.dpi = dpi;
        m.size = {Scale(kTabW, dpi), Scale(kTabH, dpi)};
        int w = m.size.cx, h = m.size.cy, rad = Scale(kTabRadius, dpi);
        Canvas c(w, h);
        c.Clear();
        c.FillRoundRect({0, -h, w, h}, rad, theme::Mix(m.color, RGB(0, 0, 0), 0.35));
        c.FillRoundRect({1, -h, w - 1, h - 1}, std::max(1, rad - 1), m.color);
        c.Present(m.hwnd, pos, kTabAlpha);
        m.pos = pos;
    }

    // Right above its window: under every window that covers it, like a label stuck to that window.
    HWND above = GetWindow(target, GW_HWNDPREV);
    bool topmost = (GetWindowLongPtrW(target, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    bool markTopmost = (GetWindowLongPtrW(m.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    if (m.shown && above == m.hwnd && pos.x == m.pos.x && pos.y == m.pos.y && topmost == markTopmost) return;
    if (topmost != markTopmost)
        SetWindowPos(m.hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    UINT flags = SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW;
    HWND after = above ? above : (topmost ? HWND_TOPMOST : HWND_TOP);
    // The top window of the normal band has the always-on-top band above it: the top of its own band then.
    if (above && !topmost && (GetWindowLongPtrW(above, GWL_EXSTYLE) & WS_EX_TOPMOST)) after = HWND_TOP;
    if (above == m.hwnd) flags |= SWP_NOZORDER;
    SetWindowPos(m.hwnd, after, pos.x, pos.y, 0, 0, flags);
    m.pos = pos;
    m.shown = true;
}

void WindowMarks::UpdateHooks() {
    // One pair of hooks per process that owns a marked window, scoped to that process: its windows moving
    // (also minimized and restored) and hiding. Windows only delivers that process's events.
    std::vector<DWORD> want;
    for (auto& [target, m] : marks_) {
        DWORD pid = 0;
        GetWindowThreadProcessId(target, &pid);
        if (pid && std::find(want.begin(), want.end(), pid) == want.end()) want.push_back(pid);
    }
    for (auto it = hooks_.begin(); it != hooks_.end();) {
        if (std::find(want.begin(), want.end(), it->first) != want.end()) {
            ++it;
            continue;
        }
        for (HWINEVENTHOOK h : it->second) UnhookWinEvent(h);
        it = hooks_.erase(it);
    }
    for (DWORD pid : want) {
        if (hooks_.count(pid)) continue;
        std::vector<HWINEVENTHOOK>& hooks = hooks_[pid];
        for (DWORD e : {(DWORD)EVENT_OBJECT_LOCATIONCHANGE, (DWORD)EVENT_OBJECT_HIDE})
            if (HWINEVENTHOOK h = SetWinEventHook(e, e, nullptr, OnEvent, pid, 0, WINEVENT_OUTOFCONTEXT)) hooks.push_back(h);
    }
}

}  // namespace wp
