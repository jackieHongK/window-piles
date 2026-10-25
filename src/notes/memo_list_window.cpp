#include "memo_list_window.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

#include "common/canvas.h"
#include "common/win_util.h"
#include "notes/memo_store.h"
#include "notes/memo_window.h"
#include "ui/popup_base.h"
#include "ui/theme.h"

namespace wp {

namespace {

constexpr int kW = 380, kMaxH = 560, kPad = 18, kHeader = 52, kRow = 62, kRowGap = 6, kBtn = 28;

struct List {
    HWND hwnd = nullptr;
    UINT dpi = 96;
    std::vector<MemoInfo> memos;
    int scroll = 0;  // px the list is scrolled down
    int hover = -1;  // a row, or -2 folder button, -3 close
};

List* g_list = nullptr;

int S(int v) { return Scale(v, g_list ? g_list->dpi : 96); }

RECT CloseRect(int w) { return {w - S(12) - S(kBtn), S(12), w - S(12), S(12) + S(kBtn)}; }

RECT FolderRect(int w) {
    int fw = MeasureUiText(L"폴더 열기", S(12), true).cx + S(22);
    RECT c = CloseRect(w);
    return {c.left - S(8) - fw, c.top, c.left - S(8), c.bottom};
}

RECT RowRect(int i, int w) {
    int y = S(kHeader) + i * S(kRow + kRowGap) - g_list->scroll;
    return {S(kPad), y, w - S(kPad), y + S(kRow)};
}

int ContentH() { return (int)g_list->memos.size() * S(kRow + kRowGap) + S(kPad); }

// "2026-10-25 장보기.txt" → "10월 25일"; otherwise when the file was made.
std::wstring DateOf(const MemoInfo& m) {
    std::wstring name = m.path.substr(m.path.find_last_of(L'\\') + 1);
    int mo = 0, d = 0;
    if (name.size() > 10 && swscanf_s(name.c_str(), L"%*4d-%2d-%2d", &mo, &d) == 2) return std::to_wstring(mo) + L"월 " + std::to_wstring(d) + L"일";
    SYSTEMTIME t;
    FILETIME local;
    FileTimeToLocalFileTime(&m.created, &local);
    FileTimeToSystemTime(&local, &t);
    return std::to_wstring(t.wMonth) + L"월 " + std::to_wstring(t.wDay) + L"일";
}

void Paint(HDC dc) {
    using namespace theme;
    List& l = *g_list;
    RECT rc;
    GetClientRect(l.hwnd, &rc);
    Canvas c(rc.right, rc.bottom);
    c.FillRoundRect(rc, 0, kBackground);
    if (!HasDwmRoundedCorners()) c.StrokeRoundRect(rc, 0, 1, kBorder);
    // The rows under the header (they scroll under it).
    for (int i = 0; i < (int)l.memos.size(); ++i) {
        RECT r = RowRect(i, rc.right);
        if (r.bottom < S(kHeader) || r.top > rc.bottom) continue;
        const MemoInfo& m = l.memos[i];
        c.FillRoundRect(r, S(kRadius), l.hover == i ? kMuted : kBackground);
        c.StrokeRoundRect(r, S(kRadius), 1, kBorder);
        c.FillRoundRect({r.left, r.top, r.left + S(5), r.bottom}, 0, MemoColorOf(m));  // its flag's color
        RECT text{r.left + S(16), r.top + S(10), r.right - S(70), r.top + S(30)};
        c.Text(m.title.empty() ? L"빈 메모" : m.title, text, m.title.empty() ? kMutedFg : kForeground, S(14), true,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        c.Text(m.excerpt, {text.left, text.bottom + S(2), r.right - S(16), r.bottom - S(8)}, kMutedFg, S(12), false,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        c.Text(DateOf(m), {r.right - S(80), r.top + S(10), r.right - S(14), r.top + S(30)}, kMutedFg, S(11), false,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    if (l.memos.empty())
        c.Text(L"아직 메모가 없어요. 오거나이저의 '새 메모 뽑기'로 시작해 보세요.", {S(kPad), S(kHeader), rc.right - S(kPad), S(kHeader) + S(60)},
               kMutedFg, S(13), false, DT_LEFT | DT_VCENTER | DT_WORDBREAK);
    // Header: the title, the folder button and close.
    c.FillRoundRect({0, 0, rc.right, S(kHeader) - S(6)}, 0, kBackground);
    c.Text(L"메모 · " + std::to_wstring(l.memos.size()) + L"개", {S(kPad), 0, rc.right - S(160), S(kHeader) - S(6)}, kForeground,
           S(16), true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT f = FolderRect(rc.right), x = CloseRect(rc.right);
    c.FillRoundRect(f, S(kRadiusSm), l.hover == -2 ? kMuted : kBackground);
    c.StrokeRoundRect(f, S(kRadiusSm), 1, kBorder);
    c.Text(L"폴더 열기", f, kForeground, S(12), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (l.hover == -3) c.FillRoundRect(x, S(kRadiusSm), kMuted);
    c.Text(L"✕", x, l.hover == -3 ? kForeground : kMutedFg, S(13), false, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    c.Blit(dc);
}

int HitAt(POINT p) {
    RECT rc;
    GetClientRect(g_list->hwnd, &rc);
    RECT f = FolderRect(rc.right), x = CloseRect(rc.right);
    if (PtInRect(&f, p)) return -2;
    if (PtInRect(&x, p)) return -3;
    if (p.y < S(kHeader)) return -1;
    for (int i = 0; i < (int)g_list->memos.size(); ++i) {
        RECT r = RowRect(i, rc.right);
        if (PtInRect(&r, p)) return i;
    }
    return -1;
}

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!g_list || g_list->hwnd != hwnd) return DefWindowProcW(hwnd, msg, wp, lp);
    List& l = *g_list;
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            Paint(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ACTIVATE:  // memos may have been written, renamed or thrown away meanwhile
            if (LOWORD(wp) != WA_INACTIVE) {
                l.memos = ListMemos();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_NCHITTEST: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            return p.y < S(kHeader) && HitAt(p) == -1 ? HTCAPTION : HTCLIENT;  // dragged by its header
        }
        case WM_MOUSEMOVE: {
            int h = HitAt({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (h != l.hover) {
                l.hover = h;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            SetCursor(LoadCursorW(nullptr, h == -1 ? IDC_ARROW : IDC_HAND));
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE:
            l.hover = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEWHEEL: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int max = std::max<int>(0, ContentH() - (rc.bottom - S(kHeader)));
            l.scroll = std::clamp(l.scroll - GET_WHEEL_DELTA_WPARAM(wp) * S(kRow) / WHEEL_DELTA, 0, max);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP: {
            int h = HitAt({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (h == -2) ShellExecuteW(nullptr, L"open", MemoDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            else if (h == -3) DestroyWindow(hwnd);
            else if (h >= 0) {
                RECT r;
                GetWindowRect(hwnd, &r);
                OpenMemoWindow(l.memos[h].path, {r.left, (r.top + r.bottom) / 2});
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) DestroyWindow(hwnd);
            return 0;
        case WM_NCDESTROY:
            delete g_list;
            g_list = nullptr;
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

void ShowMemoList(POINT anchor) {
    if (g_list) {
        g_list->memos = ListMemos();
        InvalidateRect(g_list->hwnd, nullptr, FALSE);
        SetForegroundWindow(g_list->hwnd);
        return;
    }
    g_list = new List;
    g_list->dpi = DpiForPoint(anchor);
    g_list->memos = ListMemos();
    int w = S(kW), h = std::min(S(kMaxH), S(kHeader) + ContentH());
    h = std::max(h, S(kHeader) + S(80));
    RECT work = WorkAreaForPoint(anchor);
    // Beside the organizer, toward the middle of the screen.
    int x = anchor.x > (work.left + work.right) / 2 ? anchor.x - w - S(16) : anchor.x + S(16);
    RECT r = ClampToWorkArea({x, anchor.y - h / 2, x + w, anchor.y - h / 2 + h}, anchor);
    g_list->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, RegisterPopupClass(L"WindowPilesMemoList", Proc), L"메모 모아 보기",
                                   WS_POPUP, r.left, r.top, r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                                   GetModuleHandleW(nullptr), nullptr);
    if (!g_list->hwnd) {
        delete g_list;
        g_list = nullptr;
        return;
    }
    ApplyRoundedCorners(g_list->hwnd);
    ShowWindow(g_list->hwnd, SW_SHOW);
    SetForegroundWindow(g_list->hwnd);
}

}  // namespace wp
