#include "menu.h"

#include <algorithm>

#include "common/canvas.h"
#include "common/win_util.h"
#include "ui/popup_base.h"
#include "ui/theme.h"

namespace wp {

namespace {

constexpr int kItemH = 34, kSepH = 9, kPad = 5, kMinW = 230, kTextPx = 13;

struct MenuState {
    const std::vector<MenuItem>* items = nullptr;
    std::vector<RECT> rects;
    int hover = -1;
    UINT result = 0;
    bool done = false;
    UINT dpi = 96;
};

MenuState* g_menu = nullptr;

int HitItem(const MenuState& s, POINT client) {
    for (size_t i = 0; i < s.rects.size(); ++i)
        if (!(*s.items)[i].separator() && !(*s.items)[i].disabled && PtInRect(&s.rects[i], client)) return (int)i;
    return -1;
}

void MoveHover(MenuState& s, int dir) {
    int n = (int)s.items->size();
    int i = s.hover;
    for (int step = 0; step < n; ++step) {
        i = (i + dir + n) % n;
        if (!(*s.items)[i].separator() && !(*s.items)[i].disabled) {
            s.hover = i;
            return;
        }
    }
}

LRESULT CALLBACK MenuProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    MenuState* s = g_menu;
    if (!s) return DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            using namespace theme;
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            auto S = [s](int v) { return Scale(v, s->dpi); };
            Canvas c(rc.right, rc.bottom);
            c.FillRoundRect(rc, 0, kBackground);
            if (!HasDwmRoundedCorners()) c.StrokeRoundRect(rc, 0, 1, kBorder);
            for (size_t i = 0; i < s->items->size(); ++i) {
                const MenuItem& it = (*s->items)[i];
                RECT r = s->rects[i];
                if (it.separator()) {
                    int y = (r.top + r.bottom) / 2;
                    c.FillRoundRect({r.left - S(kPad) + S(1), y, r.right + S(kPad) - S(1), y + 1}, 0, kBorder);
                    continue;
                }
                if ((int)i == s->hover) c.FillRoundRect(r, S(kRadiusSm), kMuted);
                COLORREF fg = it.disabled ? kBorderStrong : kForeground;
                RECT tr{r.left + S(10), r.top, r.right - S(10), r.bottom};
                c.Text(it.text, tr, fg, S(kTextPx), false, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                if (!it.hint.empty()) c.Text(it.hint, tr, kMutedFg, S(12), false, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
            c.Blit(dc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
            int hit = HitItem(*s, p);
            if (hit != s->hover) {
                s->hover = hit;
                InvalidateRect(h, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE:
            s->hover = -1;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
            int hit = HitItem(*s, p);
            if (hit >= 0) {
                s->result = (*s->items)[hit].id;
                s->done = true;
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) s->done = true;
            else if (wp == VK_DOWN) MoveHover(*s, 1), InvalidateRect(h, nullptr, FALSE);
            else if (wp == VK_UP) MoveHover(*s, -1), InvalidateRect(h, nullptr, FALSE);
            else if (wp == VK_RETURN && s->hover >= 0) s->result = (*s->items)[s->hover].id, s->done = true;
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) s->done = true;  // clicked somewhere else
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

UINT ShowMenu(const std::vector<MenuItem>& items, POINT pt) {
    if (g_menu) return 0;  // no nested menus
    MenuState s;
    s.items = &items;
    s.dpi = DpiForPoint(pt);
    auto S = [&](int v) { return Scale(v, s.dpi); };

    int w = S(kMinW);
    for (auto& it : items) {
        if (it.separator()) continue;
        int tw = MeasureUiText(it.text, S(kTextPx), false).cx + S(20);
        if (!it.hint.empty()) tw += MeasureUiText(it.hint, S(12), false).cx + S(24);
        w = std::max(w, tw + 2 * S(kPad));
    }
    int y = S(kPad);
    for (auto& it : items) {
        int ih = it.separator() ? S(kSepH) : S(kItemH);
        s.rects.push_back({S(kPad), y, w - S(kPad), y + ih});
        y += ih;
    }
    int h = y + S(kPad);
    RECT r = ClampToWorkArea({pt.x, pt.y, pt.x + w, pt.y + h}, pt);
    if (r.top < pt.y - 2 && pt.y - h >= WorkAreaForPoint(pt).top) r = {r.left, pt.y - h, r.right, pt.y};  // open upward

    g_menu = &s;
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, RegisterPopupClass(L"WindowPilesMenu", MenuProc),
                                L"메뉴", WS_POPUP, r.left, r.top, w, h, nullptr, nullptr, GetModuleHandleW(nullptr),
                                nullptr);
    ApplyRoundedCorners(hwnd, true);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    MSG msg;
    while (!s.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // A click on any other window of ours (piles are non-activating) also closes the menu.
        if ((msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_NCLBUTTONDOWN) &&
            msg.hwnd != hwnd) {
            s.done = true;
            break;
        }
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_menu = nullptr;
    DestroyWindow(hwnd);
    return s.result;
}

}  // namespace wp
