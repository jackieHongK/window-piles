#include "app_icon.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "common/canvas.h"

namespace wp {

namespace {

// Geometry on a 64-unit grid (scaled to the icon size).
constexpr float kRimY = 20, kLeft = 14, kRight = 50, kBottom = 49, kCorner = 7;

struct Pt {
    float x, y;
};

// The tray's U: left wall down, rounded corner, bottom, rounded corner, right wall up (open top).
std::vector<Pt> TrayPath() {
    std::vector<Pt> p;
    p.push_back({kLeft, kRimY});
    const int n = 8;
    for (int i = 0; i <= n; ++i) {  // bottom-left corner, center (21, 42)
        float a = 3.14159265f * (1.0f - 0.5f * i / n);
        p.push_back({kLeft + kCorner + kCorner * std::cos(a), kBottom - kCorner + kCorner * std::sin(a)});
    }
    for (int i = 0; i <= n; ++i) {  // bottom-right corner, center (43, 42)
        float a = 3.14159265f * (0.5f - 0.5f * i / n);
        p.push_back({kRight - kCorner + kCorner * std::cos(a), kBottom - kCorner + kCorner * std::sin(a)});
    }
    p.push_back({kRight, kRimY});
    return p;
}

float DistToSegment(float px, float py, Pt a, Pt b) {
    float vx = b.x - a.x, vy = b.y - a.y, wx = px - a.x, wy = py - a.y;
    float len2 = vx * vx + vy * vy;
    float t = len2 > 0 ? std::clamp((wx * vx + wy * vy) / len2, 0.0f, 1.0f) : 0.0f;
    float dx = px - (a.x + t * vx), dy = py - (a.y + t * vy);
    return std::sqrt(dx * dx + dy * dy);
}

uint32_t Premul(COLORREF c, int a) {
    uint32_t r = GetRValue(c) * a / 255, g = GetGValue(c) * a / 255, b = GetBValue(c) * a / 255;
    return ((uint32_t)a << 24) | (r << 16) | (g << 8) | b;
}

uint32_t Over(uint32_t src, uint32_t dst) {
    uint32_t sa = src >> 24, inv = 255 - sa;
    auto ch = [&](int shift) { return ((src >> shift) & 255) + (((dst >> shift) & 255) * inv) / 255; };
    return (ch(24) << 24) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// Anti-aliased stroke of the U path, `width` in px, into canvas pixels at (ox, oy), scale s/64.
void StrokeTray(Canvas& c, int ox, int oy, int s, float width, COLORREF color, BYTE alpha = 255) {
    std::vector<Pt> path = TrayPath();
    float k = s / 64.0f, half = width / 2;
    int x0 = std::max(0, ox + (int)(kLeft * k - width)), x1 = std::min(c.width(), ox + (int)(kRight * k + width) + 1);
    int y0 = std::max(0, oy + (int)(kRimY * k - width)), y1 = std::min(c.height(), oy + (int)(kBottom * k + width) + 1);
    uint32_t* px = c.pixels();
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            float gx = (x - ox + 0.5f) / k, gy = (y - oy + 0.5f) / k, d = 1e9f;
            for (size_t i = 0; i + 1 < path.size(); ++i) d = std::min(d, DistToSegment(gx, gy, path[i], path[i + 1]));
            float cov = std::clamp(half + 0.5f - d * k, 0.0f, 1.0f);
            if (cov <= 0) continue;
            uint32_t& dst = px[(size_t)y * c.width() + x];
            dst = Over(Premul(color, (int)(alpha * cov + 0.5f)), dst);
        }
}

struct Card {
    float y;
    COLORREF color;
    BYTE alpha;
};

void DrawCards(Canvas& c, int ox, int oy, int s, const std::vector<Card>& cards, float h) {
    float k = s / 64.0f;
    for (const Card& card : cards) {
        RECT r{ox + (LONG)std::lround(20 * k), oy + (LONG)std::lround(card.y * k), ox + (LONG)std::lround(44 * k),
               oy + (LONG)std::lround((card.y + h) * k)};
        if (r.bottom <= r.top) r.bottom = r.top + 1;
        c.FillRoundRect(r, std::max(1, (int)std::lround(2 * k)), card.color, card.alpha);
    }
}

HICON ToIcon(const Canvas& c, int size) {
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
    memcpy(bits, const_cast<Canvas&>(c).pixels(), (size_t)size * size * 4);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

bool Small(int size) { return size <= 20; }

}  // namespace

void DrawMark(Canvas& c, int x, int y, int s) {
    float k = s / 64.0f;
    RECT tile{x + (LONG)std::lround(2 * k), y + (LONG)std::lround(2 * k), x + (LONG)std::lround(62 * k),
              y + (LONG)std::lround(62 * k)};
    c.FillRoundRectV(tile, (int)std::lround(14 * k), RGB(0x7E, 0x55, 0x36), RGB(0x4A, 0x2E, 0x1D));
    if (Small(s)) {
        DrawCards(c, x, y, s, {{37, RGB(255, 255, 255), 255}, {26, RGB(0x5B, 0x9A, 0xC4), 255}}, 8);
        StrokeTray(c, x, y, s, std::max(2.0f, 4.5f * k), RGB(0xF3, 0xE9, 0xDE));
    } else {
        DrawCards(c, x, y, s,
                  {{40, RGB(255, 255, 255), 255}, {32.5f, RGB(0xBF, 0xE3, 0xCF), 255}, {25, RGB(0x5B, 0x9A, 0xC4), 255}},
                  6);
        StrokeTray(c, x, y, s, 4.5f * k, RGB(0xF3, 0xE9, 0xDE));
    }
}

HICON MakeAppIcon(int size) {
    Canvas c(size, size);
    DrawMark(c, 0, 0, size);
    return ToIcon(c, size);
}

HICON MakeTrayIcon(int size, bool lightTaskbar) {
    Canvas c(size, size);
    COLORREF fg = lightTaskbar ? RGB(32, 30, 28) : RGB(255, 255, 255);
    float k = size / 64.0f;
    // The mark without its tile, slightly enlarged to fill the notification-area square.
    if (Small(size)) {
        DrawCards(c, 0, 0, size, {{37, fg, 255}, {26, fg, 115}}, 8);
        StrokeTray(c, 0, 0, size, std::max(2.0f, 5.0f * k), fg);
    } else {
        DrawCards(c, 0, 0, size, {{40, fg, 255}, {32.5f, fg, 200}, {25, fg, 115}}, 6);
        StrokeTray(c, 0, 0, size, 5.0f * k, fg);
    }
    return ToIcon(c, size);
}

bool TaskbarIsLight() {
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS &&
           value != 0;
}

}  // namespace wp
