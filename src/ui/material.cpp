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
    if (is("felt") || is("felt-gray")) return Generic({0.070f, 0.040f, 4, 4, 0.01f, 0.10f, 0.01f, 0.10f, 0}, x, y, k);
    if (is("acrylic")) return Generic({0.010f, 0.018f, 26, 26, 0, 0, 0.002f, 0.03f, 0}, x, y, k);  // a fine frost
    if (is("acrylic-cobalt")) return Generic({0.004f, 0.010f, 40, 40, 0, 0, 0, 0, 0}, x, y, k);    // solid and glossy
    if (is("quartz")) {
        // White quartz with glitter: a soft cloud, and here and there a fleck that catches the light, some
        // of them in a color.
        float n = Generic({0.012f, 0.045f, 12, 12, 0.004f, 0.12f, 0, 0, 0}, x, y, k);
        float s = Hash(x, y, 71);
        if (s > 0.985f) n += 0.18f + 0.30f * (s - 0.985f) / 0.015f;
        if (s > 0.996f) {
            tint = Hue(Hash(x, y, 72));
            tintAmount = 0.45f;
        }
        return n;
    }
    if (is("nacre")) {
        // Mother-of-pearl: a pastel rainbow drifting in waves over a pearly white.
        tint = Hue(0.55f + 0.30f * Fbm(x, y, (int)(70 * k), 81) + 0.08f * std::sin((x + y) / (22.0f * k)));
        tintAmount = 0.22f;
        return 0.05f * std::sin(y / (5.0f * k) + 6.0f * Fbm(x, y, (int)(50 * k), 82)) + (Hash(x, y, 83) - 0.5f) * 0.02f;
    }
    if (is("burl")) {
        // Burl: grain swirled around knots and eyes.
        float g = std::sin(55.0f * Fbm(x, y, (int)(28 * k), 41));
        return 0.10f * g - 0.14f * std::pow(std::max(0.0f, g), 6.0f) + (Hash(x, y, 42) - 0.5f) * 0.03f;
    }
    if (is("onyx")) {
        // Banded onyx: honey and cream layers flowing across the slab.
        float g = std::sin(y * 0.045f / k + 7.0f * Fbm(x, y, (int)(60 * k), 51));
        return 0.12f * g + (Fbm(x, y, (int)(20 * k), 52) - 0.5f) * 0.06f;
    }
    if (is("damascus")) {
        // Damascus steel: folded layers rippling through the blade.
        float g = std::sin((x * 0.05f + y * 0.02f) / k + 10.0f * Fbm(x, y, (int)(50 * k), 61));
        return 0.16f * g * std::fabs(g) + (Hash(x, y, 62) - 0.5f) * 0.02f;
    }
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
    if (is("oak") || is("black-ash") || is("ebony")) {
        // Long grain lines along x, wavy, with darker latewood bands (ash: the stain gathers in them; ebony:
        // fine streaks in near-black).
        float g = std::sin(y / (2.2f * k) + 9.0f * Fbm(x / 6, y, (int)(40 * k), 21));
        float bands = is("black-ash") ? 0.22f : is("ebony") ? 0.16f : 0.08f;
        return 0.06f * g - bands * std::pow(std::max(0.0f, g), 8.0f) + (ValueNoise(x, y, (int)(80 * k), 1, 22) - 0.5f) * 0.05f;
    }
    if (is("hologram")) {
        // Iridescent foil: a diagonal rainbow, bent by noise, over brushed silver.
        tint = Hue((x + y) / (420.0f * k) + 0.25f * Fbm(x, y, (int)(60 * k), 31));
        tintAmount = 0.38f;
        return (ValueNoise(x, y, (int)(70 * k), 1, 32) - 0.5f) * 0.06f;
    }
    return 0;  // liquid glass: handled as a whole in FillMaterial
}

// Shades the material over every drawn pixel in `area` (filled with its base color just before), as if the
// whole thing were `ref`: sheen and glass fade along `ref`'s height, so parts of one piece match. `rims`:
// the glass edges, for a single rounded rect (`ref`, `radius`).
void Shade(Canvas& c, const RECT& area, const Material& mat, UINT dpi, const RECT& ref, bool rims, int radius) {
    float k = dpi / 96.0f;
    uint32_t* px = c.pixels();
    int y0 = std::max<int>(0, area.top), y1 = std::min<int>(c.height(), area.bottom);
    int x0 = std::max<int>(0, area.left), x1 = std::min<int>(c.width(), area.right);
    float h = (float)std::max<LONG>(1, ref.bottom - ref.top);

    if (mat.translucent) {
        // Glass: the desktop shows through. Liquid glass is a cool, light pane, brighter toward the top;
        // clear glass is barely there, with a soft diagonal reflection. Scaling premultiplied pixels keeps
        // the rounded edges anti-aliased.
        bool clear = std::strcmp(mat.id, "clear-glass") == 0;
        for (int y = y0; y < y1; ++y) {
            float t = (y - ref.top) / h, keep = clear ? 0.38f : 0.62f + 0.18f * (1 - t);
            for (int x = x0; x < x1; ++x) {
                uint32_t& p = px[(size_t)y * c.width() + x];
                if (!(p >> 24)) continue;
                float shine = 0;
                if (clear) {  // a band of light across the pane, from canvas position so pieces line up
                    float d = std::fmod((x + y * 0.55f) / (240.0f * k), 1.0f) - 0.3f;
                    shine = 0.22f * std::exp(-d * d / 0.004f);
                }
                auto ch = [&](int shift) {
                    float v = ((p >> shift) & 255) * keep;
                    return (uint32_t)std::min(255.0f, v + (255 - v) * shine);
                };
                p = (ch(24) << 24) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
            }
        }
        if (rims) {
            c.StrokeRoundRect(ref, radius, std::max(1, (int)std::lround(1.5f * k)), RGB(255, 255, 255), 220);
            RECT inner{ref.left + 2, ref.top + 2, ref.right - 2, ref.bottom - 2};
            c.StrokeRoundRect(inner, std::max(0, radius - 2), 1, RGB(170, 190, 215), 60);
        }
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

    auto is = [&](const char* s) { return std::strcmp(mat.id, s) == 0; };
    float sheen = is("aluminum") || is("brass") || is("damascus")              ? 0.05f
                  : is("leather") || is("hologram")                          ? 0.06f
                  : is("titanium") || is("ceramic") || is("acrylic") || is("nacre") ? 0.04f
                  : is("acrylic-cobalt")                                     ? 0.12f
                                                                             : 0.0f;
    for (int y = y0; y < y1; ++y) {
        float sh = sheen * (0.5f - (y - ref.top) / h) * 2;
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
}

}  // namespace

void FillMaterial(Canvas& c, const RECT& r, int radius, int m, UINT dpi) {
    const Material& mat = kMaterials[ClampMaterial(m)];
    c.FillRoundRect(r, radius, mat.base);
    Shade(c, r, mat, dpi, r, true, radius);
    if (std::strcmp(mat.id, "leather") == 0) {  // saddle stitching just inside the edge, in a lighter thread
        int in = Scale(5, dpi);
        RECT s{r.left + in, r.top + in, r.right - in, r.bottom - in};
        c.DashedRoundRect(s, std::max(0, radius - in), std::max(1, Scale(1, dpi)), Scale(4, dpi), Scale(3, dpi),
                          RGB(232, 205, 170), 170);
    }
}

void FillMaterialShape(Canvas& c, std::initializer_list<MaterialPart> parts, const RECT& shading, int m, UINT dpi) {
    const Material& mat = kMaterials[ClampMaterial(m)];
    RECT area{};
    for (const MaterialPart& part : parts) {
        c.FillRoundRect(part.r, part.radius, mat.base);
        UnionRect(&area, &area, &part.r);
    }
    Shade(c, area, mat, dpi, shading, false, 0);
}

int MaterialFromId(const char* id) {
    for (int i = 0; i < kMaterialCount; ++i)
        if (id && std::strcmp(id, kMaterials[i].id) == 0) return i;
    return 0;
}

}  // namespace wp
