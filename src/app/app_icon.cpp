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

// A paper card in the tray: flat color with volume — lighter top, darker bottom, a highlight on its top
// edge and a soft shadow below, so it reads as a solid block, not a line.
struct Card {
    float y, h;
    COLORREF top, bottom;
};

void DrawCards(Canvas& c, int ox, int oy, int s, const std::vector<Card>& cards, float left, float right) {
    float k = s / 64.0f;
    int px = std::max(1, (int)std::lround(k));  // ~1 grid unit, at least 1 px
    int radius = std::max(1, (int)std::lround(2 * k));
    for (const Card& card : cards) {  // bottom card first: each one's shadow falls on the one below
        RECT r{ox + (LONG)std::lround(left * k), oy + (LONG)std::lround(card.y * k), ox + (LONG)std::lround(right * k),
               oy + (LONG)std::lround((card.y + card.h) * k)};
        if (r.bottom - r.top < 2) r.bottom = r.top + 2;
        c.FillRoundRect({r.left, r.top + px, r.right, r.bottom + px}, radius, RGB(0x26, 0x16, 0x0C), 110);
        c.FillRoundRectV(r, radius, card.top, card.bottom);
        if (r.bottom - r.top >= 4)
            c.FillRoundRect({r.left + px, r.top, r.right - px, r.top + px}, radius, RGB(255, 255, 255), 150);
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

// Card colors: brand blue, note mint, paper (top → bottom of each card).
constexpr COLORREF kBlueTop = RGB(0x78, 0xB4, 0xDC), kBlueBottom = RGB(0x46, 0x82, 0xAE);
constexpr COLORREF kMintTop = RGB(0xD6, 0xF0, 0xE2), kMintBottom = RGB(0xA6, 0xD2, 0xBA);
constexpr COLORREF kPaperTop = RGB(0xFF, 0xFF, 0xFF), kPaperBottom = RGB(0xEC, 0xE2, 0xD4);

}  // namespace

void DrawMark(Canvas& c, int x, int y, int s) {
    float k = s / 64.0f;
    int px = std::max(1, (int)std::lround(k));
    RECT tile{x + (LONG)std::lround(2 * k), y + (LONG)std::lround(2 * k), x + (LONG)std::lround(62 * k),
              y + (LONG)std::lround(62 * k)};
    c.FillRoundRectV(tile, (int)std::lround(14 * k), RGB(0x9A, 0x6A, 0x44), RGB(0x46, 0x2B, 0x1B));
    // Cards sit inside the tray with a gap to its floor and walls. At icon sizes (≤ 32 px: tray, small
    // icons) a thicker tray and two wide cards, mint below so it doesn't merge with the cream floor.
    bool small = s <= 32;
    if (small)
        DrawCards(c, x, y, s, {{33.5f, 8.5f, kMintTop, kMintBottom}, {24, 8.5f, kBlueTop, kBlueBottom}}, 19, 45);
    else
        DrawCards(c, x, y, s,
                  {{38, 5.5f, kPaperTop, kPaperBottom}, {30.5f, 5.5f, kMintTop, kMintBottom},
                   {23, 5.5f, kBlueTop, kBlueBottom}},
                  20, 44);
    float stroke = std::max(2.0f, (small ? 5.5f : 4.5f) * k);
    StrokeTray(c, x, y + px, s, stroke, RGB(0x26, 0x16, 0x0C), 120);  // the tray's shadow on the tile
    StrokeTray(c, x, y, s, stroke, RGB(0xF6, 0xEE, 0xE4));
}

HICON MakeAppIcon(int size) {
    Canvas c(size, size);
    DrawMark(c, 0, 0, size);
    return ToIcon(c, size);
}

}  // namespace wp
