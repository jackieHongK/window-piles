// Unit test: the fast paths in Canvas::FillRoundRect / StrokeRoundRect must draw exactly the same
// pixels as the plain per-pixel coverage loops they replace. Exit code = failures.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "common/canvas.h"

using namespace wp;

namespace {

int g_fail = 0;

// --- reference: the original, unoptimized loops --------------------------------------------------
uint32_t Premul(COLORREF c, int a) {
    uint32_t r = GetRValue(c) * a / 255, g = GetGValue(c) * a / 255, b = GetBValue(c) * a / 255;
    return ((uint32_t)a << 24) | (r << 16) | (g << 8) | b;
}
uint32_t Over(uint32_t src, uint32_t dst) {
    uint32_t sa = src >> 24;
    if (sa == 255) return src;
    if (sa == 0) return dst;
    uint32_t inv = 255 - sa;
    uint32_t a = sa + ((dst >> 24) * inv) / 255;
    uint32_t r = ((src >> 16) & 255) + (((dst >> 16) & 255) * inv) / 255;
    uint32_t g = ((src >> 8) & 255) + (((dst >> 8) & 255) * inv) / 255;
    uint32_t b = (src & 255) + ((dst & 255) * inv) / 255;
    return (a << 24) | (r << 16) | (g << 8) | b;
}
float Coverage(float px, float py, const RECT& r, float radius) {
    float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
    float hx = (r.right - r.left) * 0.5f, hy = (r.bottom - r.top) * 0.5f;
    radius = std::min(radius, std::min(hx, hy));
    float qx = std::fabs(px - cx) - (hx - radius), qy = std::fabs(py - cy) - (hy - radius);
    float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    float d = std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
    return std::clamp(0.5f - d, 0.0f, 1.0f);
}
void RefFill(std::vector<uint32_t>& px, int w, int h, const RECT& r, int radius, COLORREF color, BYTE alpha) {
    for (int y = std::max<int>(0, r.top); y < std::min<int>(h, r.bottom); ++y)
        for (int x = std::max<int>(0, r.left); x < std::min<int>(w, r.right); ++x) {
            float cov = Coverage(x + 0.5f, y + 0.5f, r, (float)radius);
            if (cov <= 0) continue;
            px[(size_t)y * w + x] = Over(Premul(color, (int)(alpha * cov + 0.5f)), px[(size_t)y * w + x]);
        }
}
void RefStroke(std::vector<uint32_t>& px, int w, int h, const RECT& r, int radius, int t, COLORREF color, BYTE alpha) {
    RECT inner{r.left + t, r.top + t, r.right - t, r.bottom - t};
    for (int y = std::max<int>(0, r.top); y < std::min<int>(h, r.bottom); ++y)
        for (int x = std::max<int>(0, r.left); x < std::min<int>(w, r.right); ++x) {
            float cov = Coverage(x + 0.5f, y + 0.5f, r, (float)radius) -
                        Coverage(x + 0.5f, y + 0.5f, inner, (float)std::max(0, radius - t));
            if (cov <= 0) continue;
            px[(size_t)y * w + x] = Over(Premul(color, (int)(alpha * cov + 0.5f)), px[(size_t)y * w + x]);
        }
}

}  // namespace

int wmain() {
    const int W = 260, H = 200;
    struct Shape {
        RECT r;
        int radius;
    };
    const Shape shapes[] = {
        {{10, 10, 250, 190}, 12}, {{0, 0, 260, 200}, 0},    {{30, 40, 56, 66}, 13},  // circle
        {{-20, -15, 120, 90}, 24}, {{100, 50, 300, 260}, 8},  // clipped by the canvas edges
        {{5, 5, 200, 31}, 13},     {{40, 40, 41, 120}, 4},    {{60, 60, 60, 90}, 6},  // pill, 1px wide, empty
        {{12, 20, 190, 110}, 200}, {{7, 9, 151, 140}, 3},
    };
    const BYTE alphas[] = {255, 160, 9};
    int cases = 0;
    for (const Shape& s : shapes)
        for (BYTE a : alphas)
            for (int stroke = 0; stroke <= 2; ++stroke) {
                Canvas c(W, H);
                std::vector<uint32_t> ref((size_t)W * H, 0);
                // a non-empty background, so "over" blending is exercised too
                c.FillRoundRect({0, 0, W, H}, 0, RGB(30, 120, 200), 90);
                RefFill(ref, W, H, {0, 0, W, H}, 0, RGB(30, 120, 200), 90);
                if (stroke == 0) {
                    c.FillRoundRect(s.r, s.radius, RGB(212, 76, 71), a);
                    RefFill(ref, W, H, s.r, s.radius, RGB(212, 76, 71), a);
                } else {
                    c.StrokeRoundRect(s.r, s.radius, stroke, RGB(24, 24, 27), a);
                    RefStroke(ref, W, H, s.r, s.radius, stroke, RGB(24, 24, 27), a);
                }
                int diff = 0;
                for (size_t i = 0; i < ref.size(); ++i) diff += c.pixels()[i] != ref[i];
                ++cases;
                if (diff) {
                    ++g_fail;
                    wprintf(L"[FAIL] rect(%ld,%ld,%ld,%ld) r=%d alpha=%d stroke=%d: %d pixels differ\n", s.r.left, s.r.top,
                            s.r.right, s.r.bottom, s.radius, a, stroke, diff);
                }
            }
    wprintf(L"%d cases\n%ls (%d failed)\n", cases, g_fail ? L"CANVAS FAILED" : L"CANVAS PASSED", g_fail);
    return g_fail;
}
