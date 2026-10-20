#include "app_icon.h"

#include <cstring>

#include "common/canvas.h"

namespace wp {

HICON MakeAppIcon(int size) {
    Canvas c(size, size);
    int s = size;
    auto R = [s](double l, double t, double r, double b) {
        return RECT{(LONG)(l * s), (LONG)(t * s), (LONG)(r * s), (LONG)(b * s)};
    };
    int radius = s / 8;
    c.FillRoundRect(R(0.34, 0.06, 0.96, 0.52), radius, RGB(52, 168, 83));
    c.FillRoundRect(R(0.20, 0.22, 0.82, 0.68), radius, RGB(245, 124, 0));
    c.FillRoundRect(R(0.04, 0.38, 0.66, 0.94), radius, RGB(66, 133, 244));
    c.StrokeRoundRect(R(0.04, 0.38, 0.66, 0.94), radius, 1, RGB(255, 255, 255), 220);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    memcpy(bits, c.pixels(), (size_t)size * size * 4);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

}  // namespace wp
