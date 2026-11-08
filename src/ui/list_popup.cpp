#include "list_popup.h"

#include <windowsx.h>

#include <algorithm>

#include "common/win_util.h"
#include "ui/popup_base.h"
#include "ui/theme.h"

namespace wp {

namespace {

constexpr int kW = 380, kMaxH = 560, kPad = 18, kHeader = 52, kRow = 62, kRowGap = 6, kBtn = 28, kThumb = 46;

struct List {
    ListPopupSpec spec;
    HWND hwnd = nullptr;
    UINT dpi = 96;
    std::vector<ListRow> rows;
    int scroll = 0;  // px the list is scrolled down
    int hover = -1;  // a row, or -2 the header button, -3 close, -4 the scroll bar's thumb, -5 its track
    int reported = -1;  // the row spec.hover was last told about
    bool dragging = false;  // the thumb is held
    int dragY = 0, dragScroll = 0;
    int S(int v) const { return Scale(v, dpi); }
};

List* Of(HWND h) { return (List*)GetWindowLongPtrW(h, GWLP_USERDATA); }

int ContentH(const List& l);

int MaxScroll(const List& l) {
    RECT rc;
    GetClientRect(l.hwnd, &rc);
    return std::max<int>(0, ContentH(l) - (rc.bottom - l.S(kHeader)));
}

// The scroll bar, in the right margin beside the rows: its track and the thumb on it (empty if all rows fit).
RECT TrackRect(const List& l) {
    RECT rc;
    GetClientRect(l.hwnd, &rc);
    return {rc.right - l.S(11), l.S(kHeader), rc.right - l.S(5), rc.bottom - l.S(8)};
}

RECT ThumbRect(const List& l) {
    int max = MaxScroll(l);
    if (!max) return {};
    RECT t = TrackRect(l);
    int th = t.bottom - t.top, view = th + l.S(8);
    int h = std::clamp(MulDiv(th, view, view + max), l.S(28), th);
    int y = t.top + MulDiv(th - h, l.scroll, max);
    return {t.left, y, t.right, y + h};
}

void ScrollTo(List& l, int y) {
    y = std::clamp(y, 0, MaxScroll(l));
    if (y == l.scroll) return;
    l.scroll = y;
    InvalidateRect(l.hwnd, nullptr, FALSE);
}

RECT CloseRect(const List& l, int w) { return {w - l.S(12) - l.S(kBtn), l.S(12), w - l.S(12), l.S(12) + l.S(kBtn)}; }

RECT ButtonRect(const List& l, int w) {
    int bw = MeasureUiText(l.spec.button, l.S(12), true).cx + l.S(22);
    RECT c = CloseRect(l, w);
    return {c.left - l.S(8) - bw, c.top, c.left - l.S(8), c.bottom};
}

RECT RowRect(const List& l, int i, int w) {
    int y = l.S(kHeader) + i * l.S(kRow + kRowGap) - l.scroll;
    return {l.S(kPad), y, w - l.S(kPad), y + l.S(kRow)};
}

int ContentH(const List& l) { return (int)l.rows.size() * l.S(kRow + kRowGap) + l.S(kPad); }

void Paint(List& l, HDC dc) {
    using namespace theme;
    RECT rc;
    GetClientRect(l.hwnd, &rc);
    Canvas c(rc.right, rc.bottom);
    c.FillRoundRect(rc, 0, kBackground);
    if (!HasDwmRoundedCorners()) c.StrokeRoundRect(rc, 0, 1, kBorder);
    // The rows under the header (they scroll under it).
    for (int i = 0; i < (int)l.rows.size(); ++i) {
        RECT r = RowRect(l, i, rc.right);
        if (r.bottom < l.S(kHeader) || r.top > rc.bottom) continue;
        const ListRow& row = l.rows[i];
        c.FillRoundRect(r, l.S(kRadius), l.hover == i ? kMuted : kBackground);
        c.StrokeRoundRect(r, l.S(kRadius), 1, kBorder);
        int left = r.left + l.S(16);
        if (row.bar != CLR_INVALID) c.FillRoundRect({r.left, r.top, r.left + l.S(5), r.bottom}, 0, row.bar);
        if (!row.thumb.empty()) {
            int t = l.S(kThumb), y = r.top + (l.S(kRow) - t) / 2;
            RECT tr{r.left + l.S(8), y, r.left + l.S(8) + t, y + t};
            if (row.thumbAlpha) {  // transparent: on a checkerboard, so its shape shows
                for (int cy = 0; cy < 6; ++cy)
                    for (int cx = 0; cx < 6; ++cx)
                        c.FillRoundRect({tr.left + cx * t / 6, tr.top + cy * t / 6, tr.left + (cx + 1) * t / 6, tr.top + (cy + 1) * t / 6}, 0,
                                        (cx + cy) % 2 ? RGB(232, 232, 232) : RGB(250, 250, 250));
                c.BlendImage(row.thumb, tr.left, tr.top);
            } else {
                c.DrawImage(row.thumb, tr, l.S(4));
            }
            left = tr.right + l.S(12);
        }
        RECT text{left, r.top + l.S(10), r.right - l.S(70), r.top + l.S(30)};
        c.Text(row.title, text, row.mutedTitle ? kMutedFg : kForeground, l.S(14), true,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        c.Text(row.excerpt, {text.left, text.bottom + l.S(2), r.right - l.S(16), r.bottom - l.S(8)}, kMutedFg, l.S(12), false,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        c.Text(row.meta, {r.right - l.S(80), r.top + l.S(10), r.right - l.S(14), r.top + l.S(30)}, kMutedFg, l.S(11), false,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    if (RECT th = ThumbRect(l); !IsRectEmpty(&th)) {  // the scroll bar, only when there is more than fits
        RECT t = TrackRect(l);
        int r = (t.right - t.left) / 2;
        c.FillRoundRect(t, r, kMuted);
        c.FillRoundRect(th, r, l.dragging || l.hover == -4 ? kMutedFg : kBorderStrong);
    }
    if (l.rows.empty())
        c.Text(l.spec.empty, {l.S(kPad), l.S(kHeader), rc.right - l.S(kPad), l.S(kHeader) + l.S(60)}, kMutedFg, l.S(13), false,
               DT_LEFT | DT_VCENTER | DT_WORDBREAK);
    // Header: the title, the button and close.
    c.FillRoundRect({0, 0, rc.right, l.S(kHeader) - l.S(6)}, 0, kBackground);
    c.Text(l.spec.title + L" · " + std::to_wstring(l.rows.size()) + L"개", {l.S(kPad), 0, rc.right - l.S(160), l.S(kHeader) - l.S(6)},
           kForeground, l.S(16), true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT b = ButtonRect(l, rc.right), x = CloseRect(l, rc.right);
    c.FillRoundRect(b, l.S(kRadiusSm), l.hover == -2 ? kMuted : kBackground);
    c.StrokeRoundRect(b, l.S(kRadiusSm), 1, kBorder);
    c.Text(l.spec.button, b, kForeground, l.S(12), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (l.hover == -3) c.FillRoundRect(x, l.S(kRadiusSm), kMuted);
    c.Text(L"✕", x, l.hover == -3 ? kForeground : kMutedFg, l.S(13), false, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    c.Blit(dc);
}

int HitAt(const List& l, POINT p) {
    RECT rc;
    GetClientRect(l.hwnd, &rc);
    RECT b = ButtonRect(l, rc.right), x = CloseRect(l, rc.right);
    if (PtInRect(&b, p)) return -2;
    if (PtInRect(&x, p)) return -3;
    if (p.y < l.S(kHeader)) return -1;
    if (RECT th = ThumbRect(l); !IsRectEmpty(&th)) {
        RECT t = TrackRect(l);
        t.left -= l.S(4);  // a little easier to hit than it is wide
        t.right = rc.right;
        if (p.y >= th.top && p.y < th.bottom && PtInRect(&t, p)) return -4;
        if (PtInRect(&t, p)) return -5;
    }
    for (int i = 0; i < (int)l.rows.size(); ++i) {
        RECT r = RowRect(l, i, rc.right);
        if (PtInRect(&r, p)) return i;
    }
    return -1;
}

// Tells spec.hover when the row under the pointer changed.
void Report(List& l) {
    int row = l.hover >= 0 ? l.hover : -1;
    if (row == l.reported || !l.spec.hover) {
        l.reported = row;
        return;
    }
    l.reported = row;
    RECT rc, r{};
    GetClientRect(l.hwnd, &rc);
    if (row >= 0) {
        r = RowRect(l, row, rc.right);
        r.top = std::max<LONG>(r.top, l.S(kHeader));  // partly under the header: what shows of it
        MapWindowPoints(l.hwnd, nullptr, (POINT*)&r, 2);
    }
    l.spec.hover(row, r, l.hwnd);
}

// The pointer's row again (the rows moved under it: scrolled).
void HoverAgain(List& l) {
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(l.hwnd, &p);
    RECT rc;
    GetClientRect(l.hwnd, &rc);
    int h = PtInRect(&rc, p) ? HitAt(l, p) : -1;
    if (l.dragging) h = -4;
    if (h != l.hover) {
        l.hover = h;
        InvalidateRect(l.hwnd, nullptr, FALSE);
    }
    Report(l);
}

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    List* lp_ = Of(hwnd);
    if (!lp_) return DefWindowProcW(hwnd, msg, wp, lp);
    List& l = *lp_;
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            Paint(l, dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ACTIVATE:  // things may have changed meanwhile
            if (LOWORD(wp) != WA_INACTIVE) RefreshListPopup(hwnd);
            return 0;
        case WM_NCHITTEST: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            return p.y < l.S(kHeader) && HitAt(l, p) == -1 ? HTCAPTION : HTCLIENT;  // dragged by its header
        }
        case WM_MOUSEMOVE: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (l.dragging) {  // the thumb follows the pointer; the list goes along in proportion
                RECT t = TrackRect(l), th = ThumbRect(l);
                int room = (t.bottom - t.top) - (th.bottom - th.top);
                if (room > 0) ScrollTo(l, l.dragScroll + MulDiv(p.y - l.dragY, MaxScroll(l), room));
                return 0;
            }
            int h = HitAt(l, p);
            if (h != l.hover) {
                l.hover = h;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            Report(l);
            SetCursor(LoadCursorW(nullptr, h == -1 || h <= -4 ? IDC_ARROW : IDC_HAND));
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (l.dragging) return 0;
            l.hover = -1;
            Report(l);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEWHEEL:  // a notch is a row; a touchpad's small steps add up smoothly
            ScrollTo(l, l.scroll - MulDiv(GET_WHEEL_DELTA_WPARAM(wp), l.S(kRow + kRowGap), WHEEL_DELTA));
            HoverAgain(l);
            return 0;
        case WM_LBUTTONDOWN: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int h = HitAt(l, p);
            if (h == -4) {
                l.dragging = true;
                l.dragY = p.y;
                l.dragScroll = l.scroll;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (h == -5) {  // above or below the thumb: a page that way
                RECT th = ThumbRect(l), rc;
                GetClientRect(hwnd, &rc);
                int page = rc.bottom - l.S(kHeader) - l.S(kRow);
                ScrollTo(l, l.scroll + (p.y < th.top ? -page : page));
                HoverAgain(l);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (l.dragging) {
                l.dragging = false;
                ReleaseCapture();
                HoverAgain(l);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            int h = HitAt(l, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (h == -2 && l.spec.press) l.spec.press(hwnd);  // may close it: nothing may touch it afterwards
            else if (h == -3) DestroyWindow(hwnd);
            else if (h >= 0 && l.spec.open) l.spec.open(h, hwnd);
            return 0;
        }
        case WM_CAPTURECHANGED:
            l.dragging = false;
            return 0;
        case WM_KEYDOWN: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int row = l.S(kRow + kRowGap), page = std::max<int>(row, rc.bottom - l.S(kHeader) - l.S(kRow));
            if (wp == VK_ESCAPE) {
                DestroyWindow(hwnd);
                return 0;
            }
            if (wp == VK_UP || wp == VK_DOWN) ScrollTo(l, l.scroll + (wp == VK_DOWN ? row : -row));
            else if (wp == VK_PRIOR || wp == VK_NEXT) ScrollTo(l, l.scroll + (wp == VK_NEXT ? page : -page));
            else if (wp == VK_HOME || wp == VK_END) ScrollTo(l, wp == VK_END ? MaxScroll(l) : 0);
            else return 0;
            HoverAgain(l);
            return 0;
        }
        case WM_NCDESTROY:
            l.hover = -1;
            Report(l);  // a peek beside it goes away with it
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            delete &l;
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

void RefreshListPopup(HWND popup) {
    List* l = IsWindow(popup) ? Of(popup) : nullptr;
    if (!l) return;
    if (l->reported >= 0 && l->spec.hover) l->spec.hover(-1, RECT{}, popup);  // the rows may be others now
    l->reported = -1;
    l->rows = l->spec.rows ? l->spec.rows() : std::vector<ListRow>{};
    l->scroll = std::clamp(l->scroll, 0, MaxScroll(*l));
    InvalidateRect(popup, nullptr, FALSE);
}

void ShowListPopup(const ListPopupSpec& spec, POINT anchor) {
    if (HWND open = FindWindowW(spec.windowClass, nullptr); open && Of(open)) {
        RefreshListPopup(open);
        SetForegroundWindow(open);
        return;
    }
    auto* l = new List;
    l->spec = spec;
    l->dpi = DpiForPoint(anchor);
    l->rows = spec.rows ? spec.rows() : std::vector<ListRow>{};
    int w = l->S(kW), h = std::min(l->S(kMaxH), l->S(kHeader) + ContentH(*l));
    h = std::max(h, l->S(kHeader) + l->S(80));
    RECT work = WorkAreaForPoint(anchor);
    // Beside the organizer, toward the middle of the screen.
    int x = anchor.x > (work.left + work.right) / 2 ? anchor.x - w - l->S(16) : anchor.x + l->S(16);
    RECT r = ClampToWorkArea({x, anchor.y - h / 2, x + w, anchor.y - h / 2 + h}, anchor);
    l->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, RegisterPopupClass(spec.windowClass, Proc), spec.title.c_str(), WS_POPUP, r.left,
                              r.top, r.right - r.left, r.bottom - r.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!l->hwnd) {
        delete l;
        return;
    }
    SetWindowLongPtrW(l->hwnd, GWLP_USERDATA, (LONG_PTR)l);
    ApplyRoundedCorners(l->hwnd);
    ShowWindow(l->hwnd, SW_SHOW);
    SetForegroundWindow(l->hwnd);
    // Not let into the foreground (the click went to the tray, which never takes it): at least on top of it.
    if (GetForegroundWindow() != l->hwnd) {
        SetWindowPos(l->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(l->hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

}  // namespace wp
