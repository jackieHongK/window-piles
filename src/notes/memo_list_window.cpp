#include "memo_list_window.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "common/canvas.h"
#include "common/win_util.h"
#include "notes/memo_peek.h"
#include "notes/memo_store.h"
#include "notes/memo_window.h"
#include "ui/list_popup.h"

namespace wp {

namespace {

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

// The memos as the list shows them (read with its rows): what a hovered row peeks at.
std::vector<MemoInfo> g_shown;

// Hovering a row: the memo unfolds beside the list (on its left when there is room, else its right), sliding
// out from under its edge level with the row, as the organizer's flags do. Never takes the pointer or focus.
constexpr wchar_t kPeekClass[] = L"WindowPilesMemoListPeek";
constexpr UINT_PTR kTimerSlide = 1;
constexpr double kSlideMs = 160;
struct Peek {
    HWND hwnd = nullptr, list = nullptr;
    int row = -1;
    std::unique_ptr<Canvas> canvas;
    RECT to{};      // screen, where it unfolds to
    bool left = true;
    double start = 0;
} g_peek;

void PresentPeek() {
    Peek& p = g_peek;
    if (!p.canvas || !IsWindow(p.list)) return;
    double t = std::min(1.0, (NowMs() - p.start) / kSlideMs), e = 1 - std::pow(1 - t, 3);
    int w = p.to.right - p.to.left, off = (int)std::lround((1 - e) * w);
    POINT at{p.to.left + (p.left ? off : -off), p.to.top};
    p.canvas->Present(p.hwnd, at);
    // Just under the list, so the part still to come out is hidden under its edge.
    SetWindowPos(p.hwnd, p.list, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (t >= 1) KillTimer(p.hwnd, kTimerSlide);
}

LRESULT CALLBACK PeekProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (msg == WM_TIMER && wp == kTimerSlide) {
        PresentPeek();
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void HidePeek() {
    g_peek.row = -1;
    g_peek.canvas.reset();
    if (g_peek.hwnd) {
        KillTimer(g_peek.hwnd, kTimerSlide);
        ShowWindow(g_peek.hwnd, SW_HIDE);
    }
}

void PeekAt(int row, RECT rowRect, HWND list) {
    if (row < 0 || row >= (int)g_shown.size()) {
        HidePeek();
        return;
    }
    if (row == g_peek.row && list == g_peek.list) return;
    Peek& p = g_peek;
    if (!p.hwnd) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = PeekProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kPeekClass;
        RegisterClassW(&wc);
        p.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, kPeekClass, L"", WS_POPUP, 0, 0, 1,
                                 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!p.hwnd) return;
    }
    UINT dpi = GetDpiForWindow(list);
    const MemoInfo& m = g_shown[row];
    p.canvas = RenderMemoPeek(m.path, MemoColorOf(m), dpi);
    p.row = row;
    p.list = list;
    int w = p.canvas->width(), h = p.canvas->height(), pad = Scale(8, dpi);
    RECT lr;
    GetWindowRect(list, &lr);
    RECT work = WorkAreaForPoint({lr.left, (lr.top + lr.bottom) / 2});
    p.left = lr.left - w >= work.left || lr.right + w > work.right;
    int top = std::clamp<int>(rowRect.top - pad, work.top + pad, std::max<int>(work.top + pad, work.bottom - pad - h));
    p.to = p.left ? RECT{lr.left - w, top, lr.left, top + h} : RECT{lr.right, top, lr.right + w, top + h};
    p.start = NowMs();
    SetTimer(p.hwnd, kTimerSlide, 10, nullptr);
    PresentPeek();
}

}  // namespace

void ShowMemoList(POINT anchor) {
    ListPopupSpec spec;
    spec.windowClass = L"WindowPilesMemoList";
    spec.title = L"메모";
    spec.button = L"폴더 열기";
    spec.empty = L"아직 메모가 없어요. 오거나이저의 디스펜서에서 한 장 뽑아 시작해 보세요.";
    spec.rows = [] {
        std::vector<ListRow> rows;
        g_shown = ListMemos();
        for (const MemoInfo& m : g_shown) {
            ListRow r;
            r.title = m.title.empty() ? L"빈 메모" : m.title;
            r.mutedTitle = m.title.empty();
            r.excerpt = m.excerpt;
            r.meta = DateOf(m);
            r.bar = MemoColorOf(m);
            rows.push_back(std::move(r));
        }
        return rows;
    };
    spec.open = [](int row, HWND popup) {
        std::vector<MemoInfo> memos = ListMemos();
        if (row >= (int)memos.size()) return;
        RECT r;
        GetWindowRect(popup, &r);
        OpenMemoWindow(memos[row].path, {r.left, (r.top + r.bottom) / 2});
    };
    spec.press = [](HWND) { ShellExecuteW(nullptr, L"open", MemoDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); };
    spec.hover = [](int row, RECT r, HWND popup) { PeekAt(row, r, popup); };
    ShowListPopup(spec, anchor);
}

}  // namespace wp
