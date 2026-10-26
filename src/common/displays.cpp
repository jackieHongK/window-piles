#include "displays.h"

#include <algorithm>

#include "common/canvas.h"
#include "common/win_util.h"

namespace wp {

std::vector<DisplayInfo> ListDisplays() {
    std::vector<DisplayInfo> out;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM lp) -> BOOL {
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(m, &mi))
            ((std::vector<DisplayInfo>*)lp)->push_back({mi.szDevice, mi.rcMonitor, mi.rcWork, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0});
        return TRUE;
    }, (LPARAM)&out);
    std::sort(out.begin(), out.end(), [](const DisplayInfo& a, const DisplayInfo& b) {
        return a.monitor.left != b.monitor.left ? a.monitor.left < b.monitor.left : a.monitor.top < b.monitor.top;
    });
    return out;
}

std::wstring PrimaryDisplay() {
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    return mi.szDevice;
}

namespace {

constexpr wchar_t kIdentifyClass[] = L"WindowPilesIdentify";
constexpr UINT_PTR kTimerGone = 1;

LRESULT CALLBACK IdentifyProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == kTimerGone) {
        DestroyWindow(h);
        return 0;
    }
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

void IdentifyDisplays(DWORD ms) {
    WNDCLASSW wc{};
    if (!GetClassInfoW(GetModuleHandleW(nullptr), kIdentifyClass, &wc)) {
        wc = {};
        wc.lpfnWndProc = IdentifyProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kIdentifyClass;
        RegisterClassW(&wc);
    }
    std::vector<DisplayInfo> displays = ListDisplays();
    for (size_t i = 0; i < displays.size(); ++i) {
        const DisplayInfo& d = displays[i];
        UINT dpi = DpiForPoint({(d.monitor.left + d.monitor.right) / 2, (d.monitor.top + d.monitor.bottom) / 2});
        int side = Scale(168, dpi), r = Scale(28, dpi);
        Canvas c(side, side);
        c.FillRoundRect({0, 0, side, side}, r, RGB(33, 27, 22), 235);  // ink, a little see-through
        c.FillRoundRect({Scale(6, dpi), Scale(6, dpi), side - Scale(6, dpi), side - Scale(6, dpi)}, r - Scale(4, dpi), RGB(33, 27, 22));
        c.Text(std::to_wstring(i + 1), {0, 0, side, side - Scale(26, dpi)}, RGB(250, 249, 246), Scale(92, dpi), true,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        c.Text(d.primary ? L"주 디스플레이" : L"디스플레이", {0, side - Scale(48, dpi), side, side - Scale(16, dpi)},
               RGB(214, 209, 200), Scale(14, dpi), false, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        HWND h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                                 kIdentifyClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!h) continue;
        POINT at{(d.work.left + d.work.right) / 2 - side / 2, (d.work.top + d.work.bottom) / 2 - side / 2};
        c.Present(h, at);
        ShowWindow(h, SW_SHOWNOACTIVATE);
        SetTimer(h, kTimerGone, ms, nullptr);
    }
}

}  // namespace wp
