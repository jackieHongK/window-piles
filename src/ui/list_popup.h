// A calm list window (the memo list, the clipboard history): a header with its count, one button and close,
// and rows to pick from (a color bar or a thumbnail, a title, a line under it, a date or time on the right).
// One window per kind; showing it again brings it forward with its rows read afresh.
#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "common/canvas.h"

namespace wp {

struct ListRow {
    std::wstring title, excerpt, meta;  // meta: a date or time, on the right
    COLORREF bar = CLR_INVALID;         // a color bar at the left edge (CLR_INVALID: none)
    Image thumb;                        // or a square thumbnail at the left (premultiplied)
    bool thumbAlpha = false;            // the thumbnail has transparent parts
    bool mutedTitle = false;            // e.g. "빈 메모"
};

struct ListPopupSpec {
    const wchar_t* windowClass;
    std::wstring title;   // the header says "<title> · N개"
    std::wstring button;  // the header button's label
    std::wstring empty;   // shown when there are no rows
    std::function<std::vector<ListRow>()> rows;  // read whenever it is shown or brought forward
    std::function<void(int row, HWND popup)> open;  // a row clicked
    std::function<void(HWND popup)> press;          // the header button
};

void ShowListPopup(const ListPopupSpec& spec, POINT anchor);  // near `anchor` (screen)
void RefreshListPopup(HWND popup);                            // rows read again (after a row or the button acted)

}  // namespace wp
