#include "inline_rename.h"

#include <dwmapi.h>

#include <cwctype>

#include "common/canvas.h"
#include "common/win_util.h"
#include "ui/popup_base.h"
#include "ui/theme.h"

namespace wp {

namespace {

constexpr int kMaxName = 40;

struct RenameState {
    HWND hwnd = nullptr, edit = nullptr;
    HBRUSH brush = nullptr;
    std::function<void(const std::wstring&)> preview;
    bool done = false, saved = false;
};

RenameState* g_rn = nullptr;

std::wstring Text(HWND edit) {
    wchar_t buf[kMaxName + 1] = {};
    GetWindowTextW(edit, buf, kMaxName + 1);
    return buf;
}

LRESULT CALLBACK RenameProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    RenameState* s = g_rn;
    if (!s) return DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            Canvas c(rc.right, rc.bottom);
            c.FillRoundRect(rc, 0, theme::kBackground);
            if (!HasDwmRoundedCorners()) c.StrokeRoundRect(rc, 0, 1, theme::kForeground);
            c.Blit(dc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_CTLCOLOREDIT:
            SetTextColor((HDC)wp, theme::kForeground);
            SetBkColor((HDC)wp, theme::kBackground);
            return (LRESULT)s->brush;
        case WM_COMMAND:
            if ((HWND)lp == s->edit && HIWORD(wp) == EN_CHANGE && s->preview) s->preview(Text(s->edit));
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) s->saved = s->done = true;  // clicked elsewhere: keep it
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

bool InlineRename(const RECT& r, const std::wstring& current, UINT dpi,
                  const std::function<void(const std::wstring&)>& preview, std::wstring& out) {
    if (g_rn) return false;
    RenameState s;
    s.preview = preview;
    g_rn = &s;
    int w = r.right - r.left, h = r.bottom - r.top;
    s.hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, RegisterPopupClass(L"WindowPilesRename", RenameProc),
                             L"이름 바꾸기", WS_POPUP, r.left, r.top, w, h, nullptr, nullptr, GetModuleHandleW(nullptr),
                             nullptr);
    ApplyRoundedCorners(s.hwnd, true);
    COLORREF focus = theme::kForeground;  // input focus outline, like shadcn's ring
    DwmSetWindowAttribute(s.hwnd, 34 /*DWMWA_BORDER_COLOR*/, &focus, sizeof(focus));

    s.brush = CreateSolidBrush(theme::kBackground);
    HFONT font = CreateUiFont(Scale(14, dpi), true);
    int eh = MeasureUiText(L"가Ag", Scale(14, dpi), true).cy + Scale(2, dpi);
    s.edit = CreateWindowExW(0, L"EDIT", current.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, Scale(10, dpi),
                             (h - eh) / 2, w - Scale(20, dpi), eh, s.hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(s.edit, WM_SETFONT, (WPARAM)font, FALSE);
    SendMessageW(s.edit, EM_SETLIMITTEXT, kMaxName, 0);
    SendMessageW(s.edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    SendMessageW(s.edit, EM_SETSEL, 0, -1);

    ShowWindow(s.hwnd, SW_SHOW);
    SetForegroundWindow(s.hwnd);
    SetFocus(s.edit);

    MSG msg;
    while (!s.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (FilterNameEdit(msg, s.edit)) continue;
        if (msg.message == WM_KEYDOWN && msg.hwnd == s.edit) {
            if (msg.wParam == VK_RETURN) {
                s.saved = s.done = true;
                break;
            }
            if (msg.wParam == VK_ESCAPE) {
                s.done = true;
                break;
            }
        }
        // A click on another of our windows (piles don't activate) also ends editing.
        if ((msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN) && msg.hwnd != s.edit &&
            msg.hwnd != s.hwnd) {
            s.saved = s.done = true;
            break;
        }
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    std::wstring name = Text(s.edit);
    g_rn = nullptr;
    DestroyWindow(s.hwnd);
    DeleteObject(font);
    DeleteObject(s.brush);
    while (!name.empty() && iswspace(name.back())) name.pop_back();
    while (!name.empty() && iswspace(name.front())) name.erase(name.begin());
    if (!s.saved) return false;
    out = name;
    return true;
}

}  // namespace wp
