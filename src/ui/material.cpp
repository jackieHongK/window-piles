#include "material.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/canvas.h"
#include "common/win_util.h"

namespace wp {

namespace {

// Deterministic hash of a pixel position → [0, 1).
float Hash(int x, int y, unsigned seed) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0f;
}

// Smooth value noise with cells of cw × ch px → [0, 1).
float ValueNoise(int x, int y, int cw, int ch, unsigned seed) {
    cw = std::max(1, cw), ch = std::max(1, ch);
    int cx = x / cw, cy = y / ch;
    float fx = (x % cw) / (float)cw, fy = (y % ch) / (float)ch;
    fx = fx * fx * (3 - 2 * fx), fy = fy * fy * (3 - 2 * fy);
    float a = Hash(cx, cy, seed), b = Hash(cx + 1, cy, seed), c = Hash(cx, cy + 1, seed), d = Hash(cx + 1, cy + 1, seed);
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}

// Three octaves of value noise → about [0, 1).
float Fbm(int x, int y, int cell, unsigned seed) {
    return ValueNoise(x, y, cell, cell, seed) * 0.57f + ValueNoise(x, y, cell / 2, cell / 2, seed + 1) * 0.29f +
           ValueNoise(x, y, cell / 4, cell / 4, seed + 2) * 0.14f;
}

// Generic texture: fine grain, mottling (cells mx × my px; stretched wide it becomes brushed metal),
// dark/light specks and a top-to-bottom sheen, as fractions of the base brightness.
struct Texture {
    float grain, mottle;
    int mx, my;
    float darkSpecks, darkAmount, lightSpecks, lightAmount;
    float sheen;
};

float Generic(const Texture& t, int x, int y, float k) {
    float n = (Hash(x, y, 1) - 0.5f) * 2 * t.grain +
              (ValueNoise(x, y, (int)(t.mx * k), (int)(t.my * k), 2) - 0.5f) * 2 * t.mottle;
    float speck = Hash(x, y, 3);
    if (speck < t.darkSpecks) n -= t.darkAmount * (0.5f + Hash(x, y, 4));
    else if (speck > 1 - t.lightSpecks) n += t.lightAmount;
    return n;
}

// The scanned walnut veneer (scripts/gen_walnut.py), as the 3D unboxing uses it.
#include "ui/walnut_data.inc"

// Walnut at canvas pixel (x, y): one tile pixel per DIP, bilinear, colored along the scan's dark-to-light ramp.
COLORREF Walnut(int x, int y, float k) {
    float u = x / k, v = y / k;
    int x0 = (int)u, y0 = (int)v;
    float fx = u - x0, fy = v - y0;
    auto at = [](int tx, int ty) { return (float)kWalnut[(size_t)(ty % kWalnutH) * kWalnutW + tx % kWalnutW]; };
    float t = ((at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) +
               (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy) / 255;
    auto mix = [t](int shift) {
        int a = (kWalnutDark >> shift) & 255, b = (kWalnutLight >> shift) & 255;
        return (int)(a + (b - a) * t + 0.5f);
    };
    return RGB(mix(0), mix(8), mix(16));
}

COLORREF Hue(float h) {  // h in [0, 1): a soft pastel rainbow
    float r = 0.5f + 0.5f * std::cos(6.2832f * (h + 0.00f)), g = 0.5f + 0.5f * std::cos(6.2832f * (h + 0.33f)),
          b = 0.5f + 0.5f * std::cos(6.2832f * (h + 0.67f));
    return RGB((int)(255 * r), (int)(255 * g), (int)(255 * b));
}

// Brightness change for material `id` at (x, y); `tint` may be set to blend a color in (hologram).
float Pattern(const char* id, int x, int y, float k, COLORREF& tint, float& tintAmount) {
    auto is = [&](const char* s) { return std::strcmp(id, s) == 0; };
    if (is("white")) return Generic({0.008f, 0.006f, 10, 10, 0, 0, 0, 0, 0}, x, y, k);
    if (is("ivory")) return Generic({0.014f, 0.018f, 9, 9, 0, 0, 0.004f, 0.04f, 0}, x, y, k);
    if (is("kraft")) return Generic({0.045f, 0.055f, 7, 7, 0.022f, 0.20f, 0.010f, 0.10f, 0}, x, y, k);
    if (is("leather")) return Generic({0.030f, 0.120f, 3, 3, 0, 0, 0, 0, 0}, x, y, k);
    if (is("aluminum")) return Generic({0.012f, 0.050f, 90, 1, 0, 0, 0, 0, 0}, x, y, k);
    if (is("titanium")) return Generic({0.035f, 0.012f, 6, 6, 0, 0, 0, 0, 0}, x, y, k);
    if (is("ceramic")) return Generic({0.002f, 0.004f, 24, 24, 0, 0, 0, 0, 0}, x, y, k);
    if (is("felt")) return Generic({0.070f, 0.040f, 4, 4, 0.01f, 0.10f, 0.01f, 0.10f, 0}, x, y, k);
    if (is("cork")) return Generic({0.050f, 0.140f, 2, 2, 0.06f, 0.30f, 0.03f, 0.12f, 0}, x, y, k);
    if (is("concrete")) return Generic({0.030f, 0.080f, 14, 14, 0.015f, 0.35f, 0.004f, 0.08f, 0}, x, y, k);
    if (is("brass")) return Generic({0.010f, 0.060f, 120, 1, 0, 0, 0, 0, 0}, x, y, k);
    if (is("marble")) {
        // Veins: thin dark lines where a turbulent diagonal wave crosses zero, plus soft clouding.
        float w = std::sin((x * 0.020f + y * 0.012f) / k + 7.0f * Fbm(x, y, (int)(48 * k), 5));
        float vein = std::pow(1.0f - std::fabs(w), 14.0f);
        return -0.30f * vein + (Fbm(x, y, (int)(32 * k), 9) - 0.5f) * 0.05f + (Hash(x, y, 1) - 0.5f) * 0.01f;
    }
    if (is("carbon")) {
        // 2x2 twill: tows alternate horizontal/vertical along diagonals; each tow is lit across its width.
        int cell = std::max(2, (int)std::lround(5 * k));
        int cx = x / cell, cy = y / cell;
        bool horizontal = ((cx + cy) & 3) < 2;
        float u = (horizontal ? y % cell : x % cell) / (float)cell;
        return (horizontal ? 0.10f : -0.06f) + 0.16f * std::sin(3.1416f * u) - 0.05f + (Hash(x, y, 1) - 0.5f) * 0.03f;
    }
    if (is("linen")) {
        // Woven threads: each row and column of threads has its own thickness; slubs here and there.
        int t = std::max(1, (int)std::lround(2 * k));
        float row = Hash(0, y / t, 11) - 0.5f, col = Hash(x / t, 0, 12) - 0.5f;
        bool over = ((x / t) + (y / t)) & 1;
        return (over ? row : col) * 0.10f + (ValueNoise(x, y, (int)(30 * k), (int)(3 * k), 13) - 0.5f) * 0.06f;
    }
    if (is("oak")) {
        // Long grain lines along x, wavy, with darker latewood bands.
        float g = std::sin(y / (2.2f * k) + 9.0f * Fbm(x / 6, y, (int)(40 * k), 21));
        return 0.06f * g - 0.08f * std::pow(std::max(0.0f, g), 8.0f) +
               (ValueNoise(x, y, (int)(80 * k), 1, 22) - 0.5f) * 0.05f;
    }
    if (is("hologram")) {
        // Iridescent foil: a diagonal rainbow, bent by noise, over brushed silver.
        tint = Hue((x + y) / (420.0f * k) + 0.25f * Fbm(x, y, (int)(60 * k), 31));
        tintAmount = 0.38f;
        return (ValueNoise(x, y, (int)(70 * k), 1, 32) - 0.5f) * 0.06f;
    }
    return 0;  // liquid glass: handled as a whole in FillMaterial
}

}  // namespace

void FillMaterial(Canvas& c, const RECT& r, int radius, int m, UINT dpi) {
    m = ClampMaterial(m);
    const Material& mat = kMaterials[m];
    float k = dpi / 96.0f;
    c.FillRoundRect(r, radius, mat.base);
    uint32_t* px = c.pixels();
    int y0 = std::max<int>(0, r.top), y1 = std::min<int>(c.height(), r.bottom);
    int x0 = std::max<int>(0, r.left), x1 = std::min<int>(c.width(), r.right);
    float h = (float)std::max<LONG>(1, r.bottom - r.top);

    if (mat.translucent) {
        // Liquid glass: the desktop shows through a cool, light pane; brighter toward the top, with a
        // lit rim. Scaling premultiplied pixels keeps the rounded edge anti-aliased.
        for (int y = y0; y < y1; ++y) {
            float t = (y - r.top) / h, keep = 0.62f + 0.18f * (1 - t);
            for (int x = x0; x < x1; ++x) {
                uint32_t& p = px[(size_t)y * c.width() + x];
                auto ch = [&](int shift) { return (uint32_t)(((p >> shift) & 255) * keep); };
                p = (ch(24) << 24) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
            }
        }
        c.StrokeRoundRect(r, radius, std::max(1, (int)std::lround(1.5f * k)), RGB(255, 255, 255), 220);
        RECT inner{r.left + 2, r.top + 2, r.right - 2, r.bottom - 2};
        c.StrokeRoundRect(inner, std::max(0, radius - 2), 1, RGB(170, 190, 215), 60);
        return;
    }
    if (std::strcmp(mat.id, "walnut") == 0) {  // the scan itself, not a pattern over the base color
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                uint32_t& p = px[(size_t)y * c.width() + x];
                uint32_t a = p >> 24;
                if (!a) continue;
                COLORREF w = Walnut(x, y, k);
                p = (a << 24) | (GetRValue(w) * a / 255 << 16) | (GetGValue(w) * a / 255 << 8) | (GetBValue(w) * a / 255);
            }
        return;
    }

    float sheen = std::strcmp(mat.id, "aluminum") == 0 || std::strcmp(mat.id, "brass") == 0 ? 0.05f
                  : std::strcmp(mat.id, "leather") == 0 || std::strcmp(mat.id, "hologram") == 0 ? 0.06f
                  : std::strcmp(mat.id, "titanium") == 0 || std::strcmp(mat.id, "ceramic") == 0 ? 0.04f
                                                                                                    : 0.0f;
    for (int y = y0; y < y1; ++y) {
        float sh = sheen * (0.5f - (y - r.top) / h) * 2;
        for (int x = x0; x < x1; ++x) {
            uint32_t& p = px[(size_t)y * c.width() + x];
            int a = p >> 24;
            if (!a) continue;
            COLORREF tint = 0;
            float tintAmount = 0;
            float n = sh + Pattern(mat.id, x, y, k, tint, tintAmount);
            auto ch = [&](int shift, int tintChannel) {
                float v = (float)((p >> shift) & 255);
                if (tintAmount > 0) v += (tintChannel * a / 255.0f - v) * tintAmount;
                return (uint32_t)std::clamp(v * (1 + n), 0.0f, (float)a);  // premultiplied: never above alpha
            };
            p = ((uint32_t)a << 24) | (ch(16, GetRValue(tint)) << 16) | (ch(8, GetGValue(tint)) << 8) |
                ch(0, GetBValue(tint));
        }
    }
    if (std::strcmp(mat.id, "leather") == 0) {  // saddle stitching just inside the edge, in a lighter thread
        int in = Scale(5, dpi);
        RECT s{r.left + in, r.top + in, r.right - in, r.bottom - in};
        c.DashedRoundRect(s, std::max(0, radius - in), std::max(1, Scale(1, dpi)), Scale(4, dpi), Scale(3, dpi),
                          RGB(232, 205, 170), 170);
    }
}

int MaterialFromId(const char* id) {
    for (int i = 0; i < kMaterialCount; ++i)
        if (id && std::strcmp(id, kMaterials[i].id) == 0) return i;
    return 0;
}

}  // namespace wp
