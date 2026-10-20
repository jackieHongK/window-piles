#include "popup_base.h"

#include <dwmapi.h>

#include "common/win_util.h"

namespace wp {

const wchar_t* RegisterPopupClass(const wchar_t* name, WNDPROC proc) {
    WNDCLASSW wc{};
    if (GetClassInfoW(GetModuleHandleW(nullptr), name, &wc)) return name;
    wc = {};
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = name;
    RegisterClassW(&wc);
    return name;
}

bool HasDwmRoundedCorners() {
    static bool win11 = IsWindows11();
    return win11;
}

void ApplyRoundedCorners(HWND hwnd, bool compact) {
    if (!HasDwmRoundedCorners()) return;
    // DWMWA_WINDOW_CORNER_PREFERENCE = 33; DWMWCP_ROUND = 2, DWMWCP_ROUNDSMALL = 3
    DWORD pref = compact ? 3 : 2;
    DwmSetWindowAttribute(hwnd, 33, &pref, sizeof(pref));
    // DWMWA_BORDER_COLOR = 34: zinc-200 hairline like shadcn cards
    COLORREF border = RGB(228, 228, 231);
    DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));
}

RECT ClampToWorkArea(RECT r, POINT anchor) {
    RECT work = WorkAreaForPoint(anchor);
    int w = r.right - r.left, h = r.bottom - r.top;
    if (r.right > work.right) OffsetRect(&r, work.right - r.right, 0);
    if (r.bottom > work.bottom) OffsetRect(&r, 0, work.bottom - r.bottom);
    if (r.left < work.left) OffsetRect(&r, work.left - r.left, 0);
    if (r.top < work.top) OffsetRect(&r, 0, work.top - r.top);
    r.right = r.left + w;
    r.bottom = r.top + h;
    return r;
}

}  // namespace wp
