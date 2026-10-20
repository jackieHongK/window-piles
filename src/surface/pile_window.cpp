#include "pile_window.h"

#include <windowsx.h>

#include <algorithm>
#include <string>

#include "common/win_util.h"

namespace wp {

namespace {

constexpr wchar_t kClass[] = L"WindowPilesPile";

// Layout in DIPs.
constexpr int kPad = 8;
constexpr int kThumbW = 240, kThumbH = 135;
constexpr int kStep = 10;  // overlap offset between stacked previews (spec: 12~16px at 100%)
constexpr int kLabelH = 32;
constexpr int kChipW = 270, kChipH = 48;

constexpr COLORREF kCardBg = RGB(250, 250, 252);
constexpr COLORREF kThumbBg = RGB(40, 42, 48);
constexpr COLORREF kHot = RGB(30, 115, 240);

void RegisterOnce() {
    static bool done = false;
    if (done) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;  // replaced per instance via GWLP_USERDATA dispatch below
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);
    done = true;
}

std::wstring CountText(const Pile& p) {
    int n = (int)p.members.size(), hidden = p.HiddenCount();
    if (p.folded) {
        if (hidden == n) return L"창 " + std::to_wstring(n) + L"개";
        return std::to_wstring(n) + L"개 중 " + std::to_wstring(n - hidden) + L"개 열림";
    }
    return std::to_wstring(n - hidden) + L"개 열림";
}

}  // namespace

SIZE PileWindow::CardSize(UINT dpi) {
    return {Scale(kPad * 2 + kThumbW + kStep * 2, dpi), Scale(kPad * 3 + kThumbH + kStep * 2 + kLabelH, dpi)};
}

PileWindow::PileWindow(Pile& pile, PileHost& host) : pile_(pile), host_(host) {
    RegisterOnce();
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"Window Pile",
                            WS_POPUP, pile.pos.x, pile.pos.y, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, (LONG_PTR)this);
    SetWindowLongPtrW(hwnd_, GWLP_WNDPROC, (LONG_PTR)&PileWindow::WndProc);
    Render();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

PileWindow::~PileWindow() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
}

RECT PileWindow::ScreenRect() const { return {pile_.pos.x, pile_.pos.y, pile_.pos.x + size_.cx, pile_.pos.y + size_.cy}; }

void PileWindow::SetHot(bool hot) {
    if (hot == hot_) return;
    hot_ = hot;
    Render();
}

void PileWindow::SnapToWorkArea(bool magnet) {
    RECT r = ScreenRect();
    RECT work = WorkAreaForRect(r);
    UINT dpi = DpiForPoint(pile_.pos);
    int margin = Scale(8, dpi), pull = Scale(20, dpi);
    int w = size_.cx, h = size_.cy;
    int x = pile_.pos.x, y = pile_.pos.y;
    if (magnet) {
        if (x - work.left < pull) x = work.left + margin;
        if (work.right - (x + w) < pull) x = work.right - w - margin;
        if (y - work.top < pull) y = work.top + margin;
        if (work.bottom - (y + h) < pull) y = work.bottom - h - margin;
    }
    x = std::clamp<int>(x, work.left, std::max<int>(work.left, work.right - w));
    y = std::clamp<int>(y, work.top, std::max<int>(work.top, work.bottom - h));
    pile_.pos = {x, y};
}

void PileWindow::Render() {
    UINT dpi = DpiForPoint(pile_.pos);
    auto S = [dpi](int v) { return Scale(v, dpi); };
    const int textPx = S(14);

    if (pile_.folded) {
        size_ = CardSize(dpi);
        SnapToWorkArea(false);
        Canvas c(size_.cx, size_.cy);
        RECT card{S(2), S(2), size_.cx - S(2), size_.cy - S(2)};
        RECT shadow = card;
        OffsetRect(&shadow, 0, S(2));
        c.FillRoundRect(shadow, S(14), RGB(0, 0, 0), 40);
        c.FillRoundRect(card, S(14), kCardBg, 250);
        c.StrokeRoundRect(card, S(14), 1, RGB(0, 0, 0), 30);

        // Up to 3 most recently added windows; the newest is in front (bottom-left).
        int n = (int)pile_.members.size();
        int shown = std::min(n, 3);
        for (int k = shown - 1; k >= 0; --k) {  // back to front
            const Member& m = pile_.members[n - 1 - k];
            int depth = k;  // 0 = front
            int x = S(kPad + kStep * depth), y = S(kPad + kStep * (2 - depth));
            RECT box{x, y, x + S(kThumbW), y + S(kThumbH)};
            c.FillRoundRect(box, S(6), kThumbBg);
            if (!m.snapshot.empty()) c.DrawImage(m.snapshot, FitRect(m.snapshot.w, m.snapshot.h, box), S(6));
            c.StrokeRoundRect(box, S(6), 1, RGB(255, 255, 255), 200);
            if (!m.hidden) c.FillRoundRect(box, S(6), RGB(255, 255, 255), 110);  // this one is open
        }
        if (n > 3) {
            RECT badge{size_.cx - S(kPad + 52), S(kPad + 4), size_.cx - S(kPad + 4), S(kPad + 28)};
            c.FillRoundRect(badge, S(12), RGB(20, 20, 24), 230);
            c.Text(L"+" + std::to_wstring(n - 3), badge, RGB(255, 255, 255), S(13), true,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        RECT label{S(kPad), size_.cy - S(kPad + kLabelH), size_.cx - S(kPad), size_.cy - S(kPad)};
        c.FillRoundRect(label, S(10), pile_.color);
        RECT nameR{label.left + S(10), label.top, label.right - S(110), label.bottom};
        c.Text(pile_.name, nameR, RGB(255, 255, 255), textPx, true,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT countR{label.right - S(110), label.top, label.right - S(10), label.bottom};
        c.Text(CountText(pile_), countR, RGB(255, 255, 255), S(13), false, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        if (hot_) c.StrokeRoundRect(card, S(14), S(3), kHot);
        c.Present(hwnd_, pile_.pos);
    } else {
        size_ = {S(kChipW), S(kChipH)};
        SnapToWorkArea(false);
        Canvas c(size_.cx, size_.cy);
        RECT chip{S(2), S(2), size_.cx - S(2), size_.cy - S(2)};
        RECT shadow = chip;
        OffsetRect(&shadow, 0, S(2));
        c.FillRoundRect(shadow, S(22), RGB(0, 0, 0), 40);
        c.FillRoundRect(chip, S(22), pile_.color);
        RECT nameR{chip.left + S(16), chip.top, chip.right - S(96), chip.bottom};
        c.Text(pile_.name, nameR, RGB(255, 255, 255), textPx, true,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT right{chip.right - S(96), chip.top, chip.right - S(14), chip.bottom};
        c.Text(CountText(pile_) + L" ▲", right, RGB(255, 255, 255), S(12), false,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        if (hot_) c.StrokeRoundRect(chip, S(22), S(3), kHot);
        c.Present(hwnd_, pile_.pos);
    }
}

LRESULT CALLBACK PileWindow::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (PileWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    return self->Handle(msg, wp, lp);
}

LRESULT PileWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;  // clicking a pile never steals focus from the user's work
        case WM_LBUTTONDOWN: {
            pressed_ = true;
            dragging_ = false;
            GetCursorPos(&pressCursor_);
            pressPos_ = pile_.pos;
            SetCapture(hwnd_);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!pressed_) return 0;
            POINT c;
            GetCursorPos(&c);
            int dx = c.x - pressCursor_.x, dy = c.y - pressCursor_.y;
            if (!dragging_ && (abs(dx) > GetSystemMetrics(SM_CXDRAG) || abs(dy) > GetSystemMetrics(SM_CYDRAG)))
                dragging_ = true;
            if (dragging_) {
                pile_.pos = {pressPos_.x + dx, pressPos_.y + dy};
                SetWindowPos(hwnd_, nullptr, pile_.pos.x, pile_.pos.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (!pressed_) return 0;
            pressed_ = false;
            ReleaseCapture();
            if (dragging_) {
                dragging_ = false;
                SnapToWorkArea(true);
                Render();  // DPI may differ on the new monitor
                host_.OnPileMoved(pile_);
            } else {
                // A double click is two clicks: treat it as one toggle (plan §1.1).
                DWORD now = GetTickCount();
                if (now - lastClickTick_ > GetDoubleClickTime()) {
                    lastClickTick_ = now;
                    host_.OnPileClick(pile_);
                }
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (pressed_ && (HWND)lp != hwnd_) {
                pressed_ = false;
                if (dragging_) {
                    dragging_ = false;
                    SnapToWorkArea(true);
                    Render();
                    host_.OnPileMoved(pile_);
                }
            }
            return 0;
        case WM_RBUTTONUP: {
            POINT c;
            GetCursorPos(&c);
            host_.OnPileMenu(pile_, c);
            return 0;
        }
        case WM_DPICHANGED:
            Render();
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace wp
