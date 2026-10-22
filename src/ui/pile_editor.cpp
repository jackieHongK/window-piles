#include "pile_editor.h"

#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "common/canvas.h"
#include "common/win_util.h"
#include "ui/popup_base.h"
#include "ui/theme.h"

namespace wp {

namespace {

constexpr int kWidth = 344, kPad = 20, kSwatch = 28, kSwatchGap = 8, kMaxName = 40;

struct EditorState {
    PileStyle style;
    std::function<void(const PileStyle&)> preview;
    HWND hwnd = nullptr, edit = nullptr;
    HFONT editFont = nullptr;
    HBRUSH editBrush = nullptr;
    UINT dpi = 96;
    SIZE size{};
    RECT previewRect{}, inputRect{}, cancelRect{}, saveRect{};
    std::vector<RECT> swatches;  // kPresetCount presets + 1 custom
    int nameLabelY = 0, colorLabelY = 0, colorNameY = 0;
    int hoverSwatch = -1;
    int hoverButton = 0;  // 1 cancel, 2 save
    bool choosing = false;
    bool done = false, saved = false;
};

EditorState* g_ed = nullptr;
COLORREF g_customColors[16] = {};

int PresetIndex(COLORREF c) {
    for (int i = 0; i < theme::kPresetCount; ++i)
        if (theme::kPresets[i].color == c) return i;
    return -1;
}

std::wstring ColorName(COLORREF c) {
    int i = PresetIndex(c);
    if (i >= 0) return theme::kPresets[i].name;
    wchar_t buf[48];
    swprintf_s(buf, L"직접 고른 색 · #%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

void Layout(EditorState& s) {
    auto S = [&](int v) { return Scale(v, s.dpi); };
    int w = S(kWidth), pad = S(kPad);
    int y = pad;
    y += MeasureUiText(L"더미 꾸미기", S(16), true).cy + S(4);
    y += MeasureUiText(L"가", S(12), false).cy + S(16);
    s.previewRect = {pad, y, w - pad, y + S(36)};
    y = s.previewRect.bottom + S(18);
    s.nameLabelY = y;
    y += MeasureUiText(L"이름", S(13), true).cy + S(8);
    s.inputRect = {pad, y, w - pad, y + S(38)};
    y = s.inputRect.bottom + S(18);
    s.colorLabelY = y;
    y += MeasureUiText(L"색상", S(13), true).cy + S(10);
    s.swatches.clear();
    int perRow = std::max(1, (w - 2 * pad + S(kSwatchGap)) / (S(kSwatch) + S(kSwatchGap)));
    for (int i = 0; i <= theme::kPresetCount; ++i) {
        int col = i % perRow, row = i / perRow;
        int x = pad + col * (S(kSwatch) + S(kSwatchGap)), yy = y + row * (S(kSwatch) + S(kSwatchGap));
        s.swatches.push_back({x, yy, x + S(kSwatch), yy + S(kSwatch)});
    }
    y = s.swatches.back().bottom + S(10);
    s.colorNameY = y;
    y += MeasureUiText(L"가", S(12), false).cy + S(22);
    int bh = S(36);
    int saveW = MeasureUiText(L"저장", S(13), true).cx + S(32);
    int cancelW = MeasureUiText(L"취소", S(13), true).cx + S(32);
    s.saveRect = {w - pad - saveW, y, w - pad, y + bh};
    s.cancelRect = {s.saveRect.left - S(8) - cancelW, y, s.saveRect.left - S(8), y + bh};
    s.size = {w, s.saveRect.bottom + pad};
}

void Paint(EditorState& s, HDC dc) {
    using namespace theme;
    auto S = [&](int v) { return Scale(v, s.dpi); };
    Canvas c(s.size.cx, s.size.cy);
    int pad = S(kPad), w = s.size.cx;
    c.FillRoundRect({0, 0, w, s.size.cy}, 0, kBackground);
    if (!HasDwmRoundedCorners()) c.StrokeRoundRect({0, 0, w, s.size.cy}, 0, 1, kBorder);

    int y = pad;
    SIZE t = c.MeasureText(L"더미 꾸미기", S(16), true);
    c.Text(L"더미 꾸미기", {pad, y, w - pad, y + t.cy}, kForeground, S(16), true, DT_LEFT | DT_TOP | DT_SINGLELINE);
    y += t.cy + S(4);
    c.Text(L"이름과 색을 바꿔서 한눈에 알아보게 하세요.", {pad, y, w - pad, y + S(20)}, kMutedFg, S(12), false,
           DT_LEFT | DT_TOP | DT_SINGLELINE);

    // Live preview of the title bar.
    c.FillRoundRect(s.previewRect, S(kRadius), s.style.color);
    COLORREF on = ContrastText(s.style.color);
    RECT pr{s.previewRect.left + S(14), s.previewRect.top, s.previewRect.right - S(14), s.previewRect.bottom};
    c.Text(s.style.name.empty() ? L"이름 없음" : s.style.name, pr, on, S(14), true,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    c.Text(L"이름", {pad, s.nameLabelY, w - pad, s.nameLabelY + S(20)}, kForeground, S(13), true,
           DT_LEFT | DT_TOP | DT_SINGLELINE);
    RECT ring{s.inputRect.left - S(3), s.inputRect.top - S(3), s.inputRect.right + S(3), s.inputRect.bottom + S(3)};
    c.FillRoundRect(ring, S(kRadius) + S(3), kMuted);  // focus ring
    c.FillRoundRect(s.inputRect, S(kRadius), kBackground);
    c.StrokeRoundRect(s.inputRect, S(kRadius), 1, kBorderStrong);

    c.Text(L"색상", {pad, s.colorLabelY, w - pad, s.colorLabelY + S(20)}, kForeground, S(13), true,
           DT_LEFT | DT_TOP | DT_SINGLELINE);
    int selected = PresetIndex(s.style.color);
    for (size_t i = 0; i < s.swatches.size(); ++i) {
        RECT r = s.swatches[i];
        bool custom = i == (size_t)kPresetCount;
        bool isSel = custom ? selected < 0 : (int)i == selected;
        if (isSel) {
            RECT o{r.left - S(3), r.top - S(3), r.right + S(3), r.bottom + S(3)};
            c.StrokeRoundRect(o, S(kRadiusSm) + S(3), S(2), kForeground);
        } else if ((int)i == s.hoverSwatch) {
            RECT o{r.left - S(3), r.top - S(3), r.right + S(3), r.bottom + S(3)};
            c.StrokeRoundRect(o, S(kRadiusSm) + S(3), S(2), kBorder);
        }
        if (custom) {
            if (selected < 0) {
                c.FillRoundRect(r, S(kRadiusSm), s.style.color);
                c.Text(L"✓", r, ContrastText(s.style.color), S(13), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                c.FillRoundRect(r, S(kRadiusSm), kBackground);
                c.DashedRoundRect(r, S(kRadiusSm), 1, S(3), S(3), kBorderStrong);
                c.Text(L"+", r, kSubtleFg, S(15), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        } else {
            COLORREF col = kPresets[i].color;
            c.FillRoundRect(r, S(kRadiusSm), col);
            if (isSel) c.Text(L"✓", r, ContrastText(col), S(13), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
    std::wstring hint = s.hoverSwatch == kPresetCount   ? L"원하는 색을 직접 고르기"
                        : s.hoverSwatch >= 0 ? kPresets[s.hoverSwatch].name
                                             : ColorName(s.style.color);
    c.Text(hint, {pad, s.colorNameY, w - pad, s.colorNameY + S(20)}, kMutedFg, S(12), false,
           DT_LEFT | DT_TOP | DT_SINGLELINE);

    c.FillRoundRect(s.cancelRect, S(kRadius), s.hoverButton == 1 ? kMuted : kBackground);
    c.StrokeRoundRect(s.cancelRect, S(kRadius), 1, kBorder);
    c.Text(L"취소", s.cancelRect, kForeground, S(13), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    c.FillRoundRect(s.saveRect, S(kRadius), s.hoverButton == 2 ? kPrimaryHover : kPrimary);
    c.Text(L"저장", s.saveRect, kPrimaryFg, S(13), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    c.Blit(dc);
}

void ReadName(EditorState& s) {
    wchar_t buf[kMaxName + 1] = {};
    GetWindowTextW(s.edit, buf, kMaxName + 1);
    s.style.name = buf;
}

void PickCustom(EditorState& s) {
    CHOOSECOLORW cc{sizeof(cc)};
    cc.hwndOwner = s.hwnd;
    cc.rgbResult = s.style.color;
    cc.lpCustColors = g_customColors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
    s.choosing = true;
    if (ChooseColorW(&cc)) {
        s.style.color = cc.rgbResult;
        if (s.preview) s.preview(s.style);
    }
    s.choosing = false;
    SetForegroundWindow(s.hwnd);
    SetFocus(s.edit);
    InvalidateRect(s.hwnd, nullptr, FALSE);
}

LRESULT CALLBACK EditorProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    EditorState* s = g_ed;
    if (!s) return DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            Paint(*s, dc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wp;
            SetTextColor(dc, theme::kForeground);
            SetBkColor(dc, theme::kBackground);
            return (LRESULT)s->editBrush;
        }
        case WM_COMMAND:
            if ((HWND)lp == s->edit && HIWORD(wp) == EN_CHANGE) {
                ReadName(*s);
                if (s->preview) s->preview(s->style);
                InvalidateRect(h, &s->previewRect, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int hs = -1;
            for (size_t i = 0; i < s->swatches.size(); ++i)
                if (PtInRect(&s->swatches[i], p)) hs = (int)i;
            int hb = PtInRect(&s->cancelRect, p) ? 1 : PtInRect(&s->saveRect, p) ? 2 : 0;
            if (hs != s->hoverSwatch || hb != s->hoverButton) {
                s->hoverSwatch = hs;
                s->hoverButton = hb;
                InvalidateRect(h, nullptr, FALSE);
            }
            SetCursor(LoadCursorW(nullptr, (hs >= 0 || hb) ? IDC_HAND : IDC_ARROW));
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE:
            s->hoverSwatch = -1;
            s->hoverButton = 0;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            for (size_t i = 0; i < s->swatches.size(); ++i)
                if (PtInRect(&s->swatches[i], p)) {
                    if (i == (size_t)theme::kPresetCount) {
                        PickCustom(*s);
                    } else {
                        s->style.color = theme::kPresets[i].color;
                        if (s->preview) s->preview(s->style);
                        InvalidateRect(h, nullptr, FALSE);
                    }
                    return 0;
                }
            if (PtInRect(&s->saveRect, p)) s->saved = s->done = true;
            else if (PtInRect(&s->cancelRect, p)) s->done = true;
            return 0;
        }
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && !s->choosing) {
                // Clicked elsewhere: keep what was typed (like Notion), unless the name is empty.
                s->saved = true;
                s->done = true;
            }
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

bool EditPileStyle(const PileStyle& current, POINT anchor, const std::function<void(const PileStyle&)>& preview,
                   PileStyle& out) {
    if (g_ed) return false;
    EditorState s;
    s.style = current;
    s.preview = preview;
    s.dpi = DpiForPoint(anchor);
    Layout(s);
    auto S = [&](int v) { return Scale(v, s.dpi); };
    RECT r = ClampToWorkArea({anchor.x + S(8), anchor.y + S(8), anchor.x + S(8) + s.size.cx, anchor.y + S(8) + s.size.cy},
                             anchor);
    g_ed = &s;
    s.hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, RegisterPopupClass(L"WindowPilesEditor", EditorProc),
                             L"더미 꾸미기", WS_POPUP, r.left, r.top, s.size.cx, s.size.cy, nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    ApplyRoundedCorners(s.hwnd);

    s.editFont = CreateUiFont(S(14), false);
    s.editBrush = CreateSolidBrush(theme::kBackground);
    int eh = MeasureUiText(L"가Ag", S(14), false).cy + S(2);
    RECT in = s.inputRect;
    s.edit = CreateWindowExW(0, L"EDIT", current.name.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                             in.left + S(12), in.top + (in.bottom - in.top - eh) / 2, in.right - in.left - S(24), eh,
                             s.hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(s.edit, WM_SETFONT, (WPARAM)s.editFont, FALSE);
    SendMessageW(s.edit, EM_SETLIMITTEXT, kMaxName, 0);
    SendMessageW(s.edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    SendMessageW(s.edit, EM_SETSEL, 0, -1);

    ShowWindow(s.hwnd, SW_SHOW);
    SetForegroundWindow(s.hwnd);
    SetFocus(s.edit);

    MSG msg;
    while (!s.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && (msg.hwnd == s.edit || msg.hwnd == s.hwnd)) {
            if (msg.wParam == VK_RETURN) {
                s.saved = s.done = true;
                break;
            }
            if (msg.wParam == VK_ESCAPE) {
                s.done = true;
                break;
            }
        }
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    ReadName(s);
    g_ed = nullptr;
    DestroyWindow(s.hwnd);
    DeleteObject(s.editFont);
    DeleteObject(s.editBrush);

    // Trim; an empty name falls back to the automatic one.
    std::wstring name = s.style.name;
    while (!name.empty() && iswspace(name.back())) name.pop_back();
    while (!name.empty() && iswspace(name.front())) name.erase(name.begin());
    s.style.name = name;
    if (!s.saved) {
        if (preview) preview(current);  // undo the live preview
        return false;
    }
    out = s.style;
    return true;
}

}  // namespace wp
