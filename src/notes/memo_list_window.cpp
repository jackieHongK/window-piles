#include "memo_list_window.h"

#include <shellapi.h>

#include <string>

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

}  // namespace

void ShowMemoList(POINT anchor) {
    ListPopupSpec spec;
    spec.windowClass = L"WindowPilesMemoList";
    spec.title = L"메모";
    spec.button = L"폴더 열기";
    spec.empty = L"아직 메모가 없어요. 오거나이저의 디스펜서에서 한 장 뽑아 시작해 보세요.";
    spec.rows = [] {
        std::vector<ListRow> rows;
        for (const MemoInfo& m : ListMemos()) {
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
    ShowListPopup(spec, anchor);
}

}  // namespace wp
