#include "drag_watcher.h"

#include "common/canvas.h"
#include "common/win_util.h"
#include "ui/theme.h"

namespace wp {

namespace {

constexpr UINT_PTR kTimerId = 1;
constexpr int kTickMs = 33;        // ~30 Hz, only while a drag is in progress
constexpr int kShowDelayMs = 250;  // short clicks/drags never flash the bar
constexpr int kBarW = 320, kRemoveW = 260, kBarH = 68, kGap = 12;
constexpr int kMargin = 14;  // room for the shadow around a bar
// Distance from the top of each monitor's work area. Kept clear of the very top edge: dragging a
// window to the top edge triggers Windows' maximize / Snap Layouts, which must not be confused
// with our bars.
constexpr int kBarTop = 48;
constexpr COLORREF kDestructive = RGB(220, 38, 38);  // shadcn destructive (red-600)

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
    bc.lpfnWndProc = DefWindowProcW;
    bc.hInstance = GetModuleHandleW(nullptr);
    bc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    bc.lpszClassName = L"WindowPilesDropBar";
    RegisterClassW(&bc);

    hook_ = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND, nullptr, OnWinEvent, 0, 0,
                            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

DragWatcher::~DragWatcher() {
    if (hook_) UnhookWinEvent(hook_);
    for (auto& b : bars_)
        if (b.hwnd) DestroyWindow(b.hwnd);
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
    pileName_ = host_.PileNameOf(hwnd);
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
    bool cancelled = EqualRect(&startRect_, &endRect) != FALSE;  // Esc, or dropped where it started
    bool wasShown = barShown_;
    DropZone zone = barShown_ ? ZoneAt(c) : DropZone::None;
    if (zone == DropZone::NewPile && !eligible_) zone = DropZone::None;
    bool wasEligible = eligible_ || !pileName_.empty();
    active_ = false;
    dragged_ = nullptr;
    HideBars();
    // Only a deliberate drag (the zones were showing) can change a pile; a quick flick that happens
    // to end over a pile does nothing.
    if (!resize_ && !cancelled && wasEligible && wasShown) host_.OnDragDrop(hwnd, startPlacement_, c, zone);
    host_.OnDragEnd();
}

DropZone DragWatcher::ZoneAt(POINT pt) const {
    for (auto& b : bars_)
        if (b.shown && PtInRect(&b.card, pt)) return b.zone;
    return DropZone::None;
}

void DragWatcher::Tick() {
    if (!active_) return;
    if (!IsWindow(dragged_)) {
        KillTimer(msgWnd_, kTimerId);
        active_ = false;
        HideBars();
        host_.OnDragEnd();
        return;
    }
    if (resize_) return;  // resizing, not moving: never offer the drop zones
    POINT c;
    GetCursorPos(&c);
    int dx = c.x - startCursor_.x, dy = c.y - startCursor_.y;
    int threshold = 4 * GetSystemMetrics(SM_CXDRAG);
    if (!barShown_ && NowMs() - startMs_ >= kShowDelayMs && dx * dx + dy * dy >= threshold * threshold) {
        ShowBars();
        barShown_ = true;
        if (eligible_) host_.OnDragTargetsShown(dragged_);
    }
    if (!barShown_) return;
    for (auto& b : bars_) {
        if (!b.shown) continue;
        bool over = PtInRect(&b.card, c) != FALSE;
        BarState s = (b.zone == DropZone::NewPile && !eligible_) ? BarState::Blocked
                     : over                                     ? BarState::Hot
                                                                : BarState::Idle;
        if (s != b.state) {
            b.state = s;
            RenderBar(b);
        }
    }
    if (eligible_) host_.OnDragHover(dragged_, c);
}

DragWatcher::Bar& DragWatcher::BarAt(size_t index) {
    while (bars_.size() <= index) {
        Bar b;
        b.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                                 L"WindowPilesDropBar", L"새 더미", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
        bars_.push_back(b);
    }
    return bars_[index];
}

void DragWatcher::ShowBars() {
    // On every monitor, so the user can act on whichever screen they are working on.
    std::vector<RECT> works;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR mon, HDC, LPRECT, LPARAM lp) -> BOOL {
            MONITORINFO mi{sizeof(mi)};
            GetMonitorInfoW(mon, &mi);
            ((std::vector<RECT>*)lp)->push_back(mi.rcWork);
            return TRUE;
        },
        (LPARAM)&works);
    bool withRemove = !pileName_.empty();
    size_t used = 0;
    for (const RECT& work : works) {
        POINT probe{(work.left + work.right) / 2, work.top + 1};
        UINT dpi = DpiForPoint(probe);
        int h = Scale(kBarH, dpi), m = Scale(kMargin, dpi), y = work.top + Scale(kBarTop, dpi);
        int newW = Scale(kBarW, dpi), remW = Scale(kRemoveW, dpi), gap = Scale(kGap, dpi);
        // "새 더미" stays centered where it always is; "빼기" sits to its right.
        int newX = (work.left + work.right - newW) / 2;
        auto place = [&](DropZone zone, int x, int w) {
            Bar& b = BarAt(used++);
            b.zone = zone;
            b.dpi = dpi;
            b.card = {x, y, x + w, y + h};
            b.pos = {x - m, y - m};
            b.state = (zone == DropZone::NewPile && !eligible_) ? BarState::Blocked : BarState::Idle;
            b.shown = true;
            RenderBar(b);
            SetWindowPos(b.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        };
        place(DropZone::NewPile, newX, newW);
        if (withRemove) place(DropZone::RemoveFromPile, newX + newW + gap, remW);
    }
    for (size_t i = used; i < bars_.size(); ++i) {
        bars_[i].shown = false;
        ShowWindow(bars_[i].hwnd, SW_HIDE);
    }
}

void DragWatcher::HideBars() {
    for (auto& b : bars_) {
        b.shown = false;
        if (b.hwnd) ShowWindow(b.hwnd, SW_HIDE);
    }
    barShown_ = false;
}

void DragWatcher::RenderBar(Bar& b) {
    using namespace theme;
    auto S = [&](int v) { return Scale(v, b.dpi); };
    int w = b.card.right - b.card.left, h = b.card.bottom - b.card.top, m = S(kMargin);
    Canvas c(w + 2 * m, h + 2 * m);
    RECT card{m, m, m + w, m + h};
    int radius = S(kRadiusLg);
    bool remove = b.zone == DropZone::RemoveFromPile;

    std::wstring title, sub;
    COLORREF bg, fg, subFg;
    if (b.state == BarState::Hot) {
        bg = remove ? kDestructive : kPrimary, fg = kPrimaryFg, subFg = remove ? RGB(254, 226, 226) : kPrimarySubtleFg;
        title = remove ? L"－  여기에 놓아 빼기" : L"＋  여기에 놓기";
        sub = remove ? L"손을 떼면 더미에서 빠져요" : L"손을 떼면 새 더미가 생겨요";
    } else if (b.state == BarState::Blocked) {
        bg = kMuted, fg = kMutedFg, subFg = kMutedFg;
        title = L"이 창은 넣을 수 없어요";
        sub = L"관리자 권한 창이나 대화상자는 제외돼요";
    } else {
        bg = kBackground, fg = remove ? kDestructive : kForeground, subFg = kMutedFg;
        title = remove ? L"－  이 더미에서 빼기" : L"＋  새 더미";
        sub = remove ? L"'" + pileName_ + L"'에서 꺼내요" : L"창을 여기에 놓으면 새 더미가 생겨요";
    }
    c.Shadow(card, radius, S(12), S(4), b.state == BarState::Hot ? 90 : 60);
    c.FillRoundRect(card, radius, bg);
    if (b.state == BarState::Idle) {
        // Dashed outline reads as "drop here" and keeps the white bar visible on white windows.
        RECT inset{card.left + S(5), card.top + S(5), card.right - S(5), card.bottom - S(5)};
        c.StrokeRoundRect(card, radius, 1, kBorder);
        c.DashedRoundRect(inset, S(kRadius), std::max(1, S(1) + S(1) / 2), S(6), S(4),
                          remove ? RGB(252, 165, 165) : kBorderStrong);
    } else if (b.state == BarState::Blocked) {
        c.StrokeRoundRect(card, radius, 1, kBorder);
    }
    RECT tr{card.left + S(12), card.top + S(13), card.right - S(12), card.top + S(13) + S(22)};
    c.Text(title, tr, fg, S(15), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT sr{card.left + S(12), tr.bottom + S(1), card.right - S(12), tr.bottom + S(1) + S(18)};
    c.Text(sub, sr, subFg, S(12), false, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    c.Present(b.hwnd, b.pos);
}

}  // namespace wp
