#include "drag_watcher.h"

#include "common/win_util.h"

namespace wp {

namespace {

constexpr UINT_PTR kTimerId = 1;
constexpr int kTickMs = 33;        // ~30 Hz, only while a drag is in progress
constexpr int kShowDelayMs = 250;  // short clicks/drags never flash the bar
constexpr int kBarW = 360, kBarH = 76;
// Distance from the top of the work area. Kept clear of the very top edge: dragging a window to
// the top edge triggers Windows' maximize / Snap Layouts, which must not be confused with our bar.
constexpr int kBarTop = 56;

constexpr COLORREF kIdleBg = RGB(245, 246, 250), kIdleFg = RGB(40, 40, 50);
constexpr COLORREF kHotBg = RGB(30, 115, 240), kHotFg = RGB(255, 255, 255);
constexpr COLORREF kBlockedBg = RGB(235, 235, 238), kBlockedFg = RGB(150, 60, 60);

}  // namespace

DragWatcher* DragWatcher::instance_ = nullptr;

DragWatcher::DragWatcher(DragHost& host) : host_(host) {
    instance_ = this;
    WNDCLASSW wc{};
    wc.lpfnWndProc = MsgProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"WindowPilesDragMsg";
    RegisterClassW(&wc);
    msgWnd_ = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);

    WNDCLASSW bc{};
    bc.lpfnWndProc = BarProc;
    bc.hInstance = GetModuleHandleW(nullptr);
    bc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    bc.lpszClassName = L"WindowPilesDropBar";
    RegisterClassW(&bc);
    bar_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                           bc.lpszClassName, L"Drop bar", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
    SetLayeredWindowAttributes(bar_, 0, 242, LWA_ALPHA);

    hook_ = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND, nullptr, OnWinEvent, 0, 0,
                            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

DragWatcher::~DragWatcher() {
    if (hook_) UnhookWinEvent(hook_);
    if (bar_) DestroyWindow(bar_);
    if (msgWnd_) DestroyWindow(msgWnd_);
    instance_ = nullptr;
}

void CALLBACK DragWatcher::OnWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD,
                                      DWORD) {
    if (!instance_ || idObject != OBJID_WINDOW || idChild != CHILDID_SELF || !hwnd) return;
    hwnd = GetAncestor(hwnd, GA_ROOT);
    if (event == EVENT_SYSTEM_MOVESIZESTART) instance_->Start(hwnd);
    else if (event == EVENT_SYSTEM_MOVESIZEEND) instance_->End(hwnd);
}

LRESULT CALLBACK DragWatcher::MsgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TIMER && w == kTimerId && instance_) {
        instance_->Tick();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void DragWatcher::Start(HWND hwnd) {
    active_ = true;
    dragged_ = hwnd;
    eligible_ = host_.CanPile(hwnd);
    GetWindowRect(hwnd, &startRect_);
    startPlacement_.length = sizeof(startPlacement_);
    GetWindowPlacement(hwnd, &startPlacement_);
    GetCursorPos(&startCursor_);
    startMs_ = NowMs();
    barShown_ = false;
    // Resize vs move: a resize starts with the cursor on the window border. (Comparing sizes does
    // not work: dragging a maximized or snapped window restores its normal size.)
    UINT dpi = GetDpiForWindow(hwnd);
    int border = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    RECT inner{startRect_.left + border, startRect_.top + border, startRect_.right - border,
               startRect_.bottom - border};
    resize_ = !IsZoomed(hwnd) && !PtInRect(&inner, startCursor_);
    SetTimer(msgWnd_, kTimerId, kTickMs, nullptr);
}

void DragWatcher::End(HWND hwnd) {
    if (!active_ || hwnd != dragged_) return;
    KillTimer(msgWnd_, kTimerId);
    RECT endRect;
    GetWindowRect(hwnd, &endRect);
    POINT c;
    GetCursorPos(&c);
    bool resized = resize_;
    bool cancelled = EqualRect(&startRect_, &endRect) != FALSE;  // Esc, or dropped where it started
    bool wasShown = barShown_;
    bool onBar = barShown_ && PtInRect(&barRect_, c);
    bool wasEligible = eligible_;
    active_ = false;
    dragged_ = nullptr;
    HideBar();
    // Only a deliberate drag (the bar was showing) can put a window into a pile; a quick flick
    // that happens to end over a pile does nothing.
    if (!resized && !cancelled && wasEligible && wasShown) host_.OnDragDrop(hwnd, startPlacement_, c, onBar);
    host_.OnDragEnd();
}

void DragWatcher::Tick() {
    if (!active_) return;
    if (!IsWindow(dragged_)) {
        KillTimer(msgWnd_, kTimerId);
        active_ = false;
        HideBar();
        host_.OnDragEnd();
        return;
    }
    if (resize_) return;  // resizing, not moving: never offer the drop bar
    POINT c;
    GetCursorPos(&c);
    int dx = c.x - startCursor_.x, dy = c.y - startCursor_.y;
    int threshold = 4 * GetSystemMetrics(SM_CXDRAG);
    if (!barShown_ && NowMs() - startMs_ >= kShowDelayMs && dx * dx + dy * dy >= threshold * threshold) {
        ShowBar(c);
        barShown_ = true;
    }
    if (!barShown_) return;
    if (!eligible_) {
        SetBarState(BarState::Blocked);
        return;
    }
    SetBarState(PtInRect(&barRect_, c) ? BarState::Hot : BarState::Idle);
    host_.OnDragHover(dragged_, c);
}

void DragWatcher::ShowBar(POINT cursor) {
    RECT work = WorkAreaForPoint(cursor);
    UINT dpi = DpiForPoint(cursor);
    int w = Scale(kBarW, dpi), h = Scale(kBarH, dpi);
    int x = (work.left + work.right - w) / 2, y = work.top + Scale(kBarTop, dpi);
    barRect_ = {x, y, x + w, y + h};
    int radius = Scale(18, dpi);
    SetWindowRgn(bar_, CreateRoundRectRgn(0, 0, w + 1, h + 1, radius, radius), FALSE);
    barState_ = BarState::Idle;
    SetWindowPos(bar_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(bar_, nullptr, TRUE);
}

void DragWatcher::HideBar() {
    ShowWindow(bar_, SW_HIDE);
    barShown_ = false;
}

void DragWatcher::SetBarState(BarState s) {
    if (s == barState_) return;
    barState_ = s;
    InvalidateRect(bar_, nullptr, TRUE);
    UpdateWindow(bar_);
}

LRESULT CALLBACK DragWatcher::BarProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCHITTEST) return HTTRANSPARENT;
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT && instance_) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        BarState s = instance_->barState_;
        COLORREF bg = s == BarState::Hot ? kHotBg : s == BarState::Blocked ? kBlockedBg : kIdleBg;
        COLORREF fg = s == BarState::Hot ? kHotFg : s == BarState::Blocked ? kBlockedFg : kIdleFg;
        const wchar_t* text = s == BarState::Hot       ? L"놓으면 새 더미가 생겨요"
                              : s == BarState::Blocked ? L"이 창은 더미에 넣을 수 없어요"
                                                       : L"＋  새 더미";
        HBRUSH b = CreateSolidBrush(bg);
        FillRect(dc, &rc, b);
        DeleteObject(b);
        UINT dpi = GetDpiForWindow(h);
        HFONT f = CreateFontW(-Scale(s == BarState::Idle ? 22 : 18, dpi), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                              0, 0, CLEARTYPE_QUALITY, 0, L"Malgun Gothic");
        HGDIOBJ old = SelectObject(dc, f);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, fg);
        DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, old);
        DeleteObject(f);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

}  // namespace wp
