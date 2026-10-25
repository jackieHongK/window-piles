#include "canvas.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace wp {

namespace {

inline uint32_t Premul(COLORREF c, int a) {
    uint32_t r = GetRValue(c) * a / 255, g = GetGValue(c) * a / 255, b = GetBValue(c) * a / 255;
    return ((uint32_t)a << 24) | (r << 16) | (g << 8) | b;
}

inline uint32_t Over(uint32_t src, uint32_t dst) {
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

// Coverage (0..1) of pixel center (px,py) inside a rounded rect, via its signed distance.
inline float RoundRectCoverage(float px, float py, const RECT& r, float radius) {
    float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
    float hx = (r.right - r.left) * 0.5f, hy = (r.bottom - r.top) * 0.5f;
    radius = std::min(radius, std::min(hx, hy));
    float qx = std::fabs(px - cx) - (hx - radius), qy = std::fabs(py - cy) - (hy - radius);
    float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    float d = std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
    return std::clamp(0.5f - d, 0.0f, 1.0f);
}

}  // namespace

RECT FitRect(int w, int h, const RECT& box) {
    int bw = box.right - box.left, bh = box.bottom - box.top;
    if (w <= 0 || h <= 0) return box;
    double s = std::min((double)bw / w, (double)bh / h);
    int fw = std::max(1, (int)(w * s + 0.5)), fh = std::max(1, (int)(h * s + 0.5));
    int x = box.left + (bw - fw) / 2, y = box.top + (bh - fh) / 2;
    return {x, y, x + fw, y + fh};
}

Canvas::Canvas(int w, int h) : w_(std::max(1, w)), h_(std::max(1, h)) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w_;
    bi.bmiHeader.biHeight = -h_;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    HDC screen = GetDC(nullptr);
    dc_ = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    void* bits = nullptr;
    bmp_ = CreateDIBSection(dc_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    px_ = (uint32_t*)bits;
    old_ = SelectObject(dc_, bmp_);
    Clear();
}

Canvas::~Canvas() {
    SelectObject(dc_, old_);
    DeleteObject(bmp_);
    DeleteDC(dc_);
}

void Canvas::Clear() { std::fill(px_, px_ + (size_t)w_ * h_, 0u); }

namespace {

// The part of row y that lies fully inside rounded rect r (coverage 1), as [left, right): every
// pixel at least the corner radius away from the corners. Most of a large shape is in this span, so
// the distance math only runs near the edges and corners.
struct Span {
    int left, right;
};
Span SolidSpan(const RECT& r, int radius, int y) {
    if (y < r.top || y >= r.bottom || r.right <= r.left) return {0, 0};
    float rad = std::min((float)radius, std::min(r.right - r.left, r.bottom - r.top) * 0.5f);
    int inset = (int)std::ceil(std::max(0.0f, rad)) + 1;
    bool straight = y >= r.top + inset && y < r.bottom - inset;
    return straight ? Span{r.left, r.right} : Span{r.left + inset, r.right - inset};
}

}  // namespace

void Canvas::FillRoundRect(const RECT& r, int radius, COLORREF color, BYTE alpha) {
    int x0 = std::max<int>(0, r.left), y0 = std::max<int>(0, r.top);
    int x1 = std::min<int>(w_, r.right), y1 = std::min<int>(h_, r.bottom);
    const uint32_t solid = Premul(color, alpha);
    for (int y = y0; y < y1; ++y) {
        uint32_t* row = px_ + (size_t)y * w_;
        Span s = SolidSpan(r, radius, y);
        for (int x = x0; x < x1; ++x) {
            if (x >= s.left && x < s.right) {
                row[x] = Over(solid, row[x]);
                continue;
            }
            float cov = RoundRectCoverage(x + 0.5f, y + 0.5f, r, (float)radius);
            if (cov <= 0) continue;
            row[x] = Over(Premul(color, (int)(alpha * cov + 0.5f)), row[x]);
        }
    }
}

void Canvas::StrokeRoundRect(const RECT& r, int radius, int t, COLORREF color, BYTE alpha) {
    RECT inner{r.left + t, r.top + t, r.right - t, r.bottom - t};
    int x0 = std::max<int>(0, r.left), y0 = std::max<int>(0, r.top);
    int x1 = std::min<int>(w_, r.right), y1 = std::min<int>(h_, r.bottom);
    for (int y = y0; y < y1; ++y) {
        uint32_t* row = px_ + (size_t)y * w_;
        Span hole = SolidSpan(inner, std::max(0, radius - t), y);  // fully inside the inner edge: no stroke
        for (int x = x0; x < x1; ++x) {
            if (x >= hole.left && x < hole.right) {
                x = hole.right - 1;
                continue;
            }
            float outer = RoundRectCoverage(x + 0.5f, y + 0.5f, r, (float)radius);
            float in = RoundRectCoverage(x + 0.5f, y + 0.5f, inner, (float)std::max(0, radius - t));
            float cov = outer - in;
            if (cov <= 0) continue;
            row[x] = Over(Premul(color, (int)(alpha * cov + 0.5f)), row[x]);
        }
    }
}

void Canvas::DashedRoundRect(const RECT& r, int radius, int t, int dash, int gap, COLORREF color, BYTE alpha) {
    RECT inner{r.left + t, r.top + t, r.right - t, r.bottom - t};
    int period = std::max(1, dash + gap);
    int x0 = std::max<int>(0, r.left), y0 = std::max<int>(0, r.top);
    int x1 = std::min<int>(w_, r.right), y1 = std::min<int>(h_, r.bottom);
    for (int y = y0; y < y1; ++y) {
        uint32_t* row = px_ + (size_t)y * w_;
        Span hole = SolidSpan(inner, std::max(0, radius - t), y);
        for (int x = x0; x < x1; ++x) {
            if (x >= hole.left && x < hole.right) {
                x = hole.right - 1;
                continue;
            }
            float outer = RoundRectCoverage(x + 0.5f, y + 0.5f, r, (float)radius);
            float in = RoundRectCoverage(x + 0.5f, y + 0.5f, inner, (float)std::max(0, radius - t));
            float cov = outer - in;
            if (cov <= 0) continue;
            bool horizontalEdge = x >= r.left + radius && x < r.right - radius;
            bool verticalEdge = y >= r.top + radius && y < r.bottom - radius;
            if (horizontalEdge && ((x - r.left) % period) >= dash) continue;
            if (verticalEdge && ((y - r.top) % period) >= dash) continue;
            row[x] = Over(Premul(color, (int)(alpha * cov + 0.5f)), row[x]);
        }
    }
}

void Canvas::Shadow(const RECT& r, int radius, int blur, int offsetY, BYTE alpha, COLORREF color) {
    // Concentric rounded rects with falling alpha approximate a gaussian blur well enough at UI sizes.
    blur = std::max(1, blur);
    for (int i = blur; i >= 1; --i) {
        RECT s{r.left - i, r.top - i + offsetY, r.right + i, r.bottom + i + offsetY};
        double f = 1.0 - (double)i / (blur + 1);
        int a = (int)(alpha * f * f * 2.2 / blur + 0.5);
        if (a > 0) FillRoundRect(s, radius + i, color, (BYTE)std::min(255, a));
    }
}

void Canvas::FillRoundRectV(const RECT& r, int radius, COLORREF top, COLORREF bottom) {
    // Vertical gradient: each row is the shape's row in its own color (same coverage as FillRoundRect).
    int h = std::max<int>(1, r.bottom - r.top - 1);
    int y0 = std::max<int>(0, r.top), y1 = std::min<int>(h_, r.bottom);
    int x0 = std::max<int>(0, r.left), x1 = std::min<int>(w_, r.right);
    for (int y = y0; y < y1; ++y) {
        double t = (double)(y - r.top) / h;
        COLORREF c = RGB((int)(GetRValue(top) + (GetRValue(bottom) - GetRValue(top)) * t),
                         (int)(GetGValue(top) + (GetGValue(bottom) - GetGValue(top)) * t),
                         (int)(GetBValue(top) + (GetBValue(bottom) - GetBValue(top)) * t));
        uint32_t* row = px_ + (size_t)y * w_;
        for (int x = x0; x < x1; ++x) {
            float cov = RoundRectCoverage(x + 0.5f, y + 0.5f, r, (float)radius);
            if (cov <= 0) continue;
            row[x] = Over(Premul(c, (int)(255 * cov + 0.5f)), row[x]);
        }
    }
}

void Canvas::FillPolygons(const std::vector<std::vector<PtF>>& contours, COLORREF color, BYTE alpha) {
    struct Edge {
        float x0, y0, x1, y1;
        int dir;
    };
    std::vector<Edge> edges;
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (const auto& c : contours)
        for (size_t i = 0; i < c.size(); ++i) {
            PtF p = c[i], q = c[(i + 1) % c.size()];
            minX = std::min(minX, p.x), maxX = std::max(maxX, p.x), minY = std::min(minY, p.y), maxY = std::max(maxY, p.y);
            if (p.y != q.y) edges.push_back(p.y < q.y ? Edge{p.x, p.y, q.x, q.y, 1} : Edge{q.x, q.y, p.x, p.y, -1});
        }
    int x0 = std::max(0, (int)std::floor(minX)), x1 = std::min(w_, (int)std::ceil(maxX) + 1);
    int y0 = std::max(0, (int)std::floor(minY)), y1 = std::min(h_, (int)std::ceil(maxY) + 1);
    if (x0 >= x1 || y0 >= y1) return;
    constexpr int kSub = 4;  // sub-scanlines per pixel row; horizontal coverage is exact
    std::vector<float> cov(x1 - x0);
    std::vector<std::pair<float, int>> hits;
    for (int y = y0; y < y1; ++y) {
        std::fill(cov.begin(), cov.end(), 0.0f);
        for (int s = 0; s < kSub; ++s) {
            float sy = y + (s + 0.5f) / kSub;
            hits.clear();
            for (const Edge& e : edges)
                if (sy >= e.y0 && sy < e.y1) hits.push_back({e.x0 + (sy - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0), e.dir});
            std::sort(hits.begin(), hits.end());
            int winding = 0;
            for (size_t i = 0; i + 1 < hits.size(); ++i) {
                winding += hits[i].second;
                if (winding == 0) continue;
                float a = std::max(hits[i].first, (float)x0), b = std::min(hits[i + 1].first, (float)x1);
                for (int x = (int)std::floor(a); x < b && x < x1; ++x)
                    cov[x - x0] += (std::min(b, x + 1.0f) - std::max(a, (float)x)) / kSub;
            }
        }
        uint32_t* row = px_ + (size_t)y * w_;
        for (int x = x0; x < x1; ++x)
            if (float c = std::min(cov[x - x0], 1.0f); c > 0) row[x] = Over(Premul(color, (int)(alpha * c + 0.5f)), row[x]);
    }
}

void Canvas::DrawImage(const Image& img, const RECT& dst, int radius) {
    if (img.empty()) return;
    int dw = dst.right - dst.left, dh = dst.bottom - dst.top;
    if (dw <= 0 || dh <= 0) return;
    const int top = dst.top, left = dst.left;
    for (int y = std::max(0, top); y < std::min<int>(h_, dst.bottom); ++y) {
        int sy0 = (y - top) * img.h / dh, sy1 = std::max(sy0 + 1, (y - top + 1) * img.h / dh);
        uint32_t* row = px_ + (size_t)y * w_;
        for (int x = std::max(0, left); x < std::min<int>(w_, dst.right); ++x) {
            int sx0 = (x - left) * img.w / dw, sx1 = std::max(sx0 + 1, (x - left + 1) * img.w / dw);
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int sy = sy0; sy < sy1 && sy < img.h; ++sy)
                for (int sx = sx0; sx < sx1 && sx < img.w; ++sx) {
                    uint32_t p = img.px[(size_t)sy * img.w + sx];
                    r += (p >> 16) & 255, g += (p >> 8) & 255, b += p & 255, ++n;
                }
            if (!n) continue;
            uint32_t c = 0xFF000000u | ((r / n) << 16) | ((g / n) << 8) | (b / n);
            if (radius > 0) {
                float cov = RoundRectCoverage(x + 0.5f, y + 0.5f, dst, (float)radius);
                if (cov <= 0) continue;
                if (cov < 1) {
                    int a = (int)(255 * cov);
                    c = Premul(RGB((c >> 16) & 255, (c >> 8) & 255, c & 255), a);
                    row[x] = Over(c, row[x]);
                    continue;
                }
            }
            row[x] = c;
        }
    }
}

template <typename F>
void Canvas::ProtectAlpha(const RECT& rIn, F&& draw) {
    RECT r{std::max<LONG>(0, rIn.left), std::max<LONG>(0, rIn.top), std::min<LONG>(w_, rIn.right),
           std::min<LONG>(h_, rIn.bottom)};
    if (r.right <= r.left || r.bottom <= r.top) return;
    int rw = r.right - r.left;
    std::vector<uint32_t> saved((size_t)rw * (r.bottom - r.top));
    for (int y = r.top; y < r.bottom; ++y)
        std::copy(px_ + (size_t)y * w_ + r.left, px_ + (size_t)y * w_ + r.right, saved.begin() + (size_t)(y - r.top) * rw);
    GdiFlush();
    draw();
    GdiFlush();
    // GDI zeroes the alpha byte of pixels it touches. Keep its output only where the background was
    // fully opaque; restore every other pixel so transparent areas never get stray color.
    for (int y = r.top; y < r.bottom; ++y) {
        uint32_t* row = px_ + (size_t)y * w_;
        for (int x = r.left; x < r.right; ++x) {
            uint32_t before = saved[(size_t)(y - r.top) * rw + (x - r.left)];
            if ((before >> 24) == 255) row[x] |= 0xFF000000u;
            else row[x] = before;
        }
    }
}

HFONT CreateUiFont(int sizePx, bool bold) {
    return CreateFontW(-sizePx, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                       0, L"Malgun Gothic");
}

SIZE MeasureUiText(const std::wstring& text, int sizePx, bool bold, int wrapWidth) {
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = CreateUiFont(sizePx, bold);
    HGDIOBJ old = SelectObject(dc, font);
    RECT r{0, 0, wrapWidth > 0 ? wrapWidth : 100000, 0};
    DrawTextW(dc, text.c_str(), -1, &r, DT_CALCRECT | DT_NOPREFIX | (wrapWidth > 0 ? DT_WORDBREAK : DT_SINGLELINE));
    SelectObject(dc, old);
    DeleteObject(font);
    DeleteDC(dc);
    return {r.right - r.left, r.bottom - r.top};
}

SIZE Canvas::MeasureText(const std::wstring& text, int sizePx, bool bold, int wrapWidth) const {
    return MeasureUiText(text, sizePx, bold, wrapWidth);
}

void Canvas::Text(const std::wstring& text, const RECT& r, COLORREF color, int sizePx, bool bold, UINT format) {
    RECT cr{std::max<LONG>(0, r.left), std::max<LONG>(0, r.top), std::min<LONG>(w_, r.right), std::min<LONG>(h_, r.bottom)};
    if (cr.right <= cr.left || cr.bottom <= cr.top) return;
    bool seeThrough = false;
    for (int y = cr.top; y < cr.bottom && !seeThrough; ++y)
        for (int x = cr.left; x < cr.right; ++x)
            if ((px_[(size_t)y * w_ + x] >> 24) != 255) {
                seeThrough = true;
                break;
            }
    if (seeThrough) {
        // On glass GDI's ClearType has nothing solid to blend with: draw the text as a coverage mask
        // (grayscale anti-aliasing) and lay the color over the pixels by it, keeping them see-through.
        int w = cr.right - cr.left, h = cr.bottom - cr.top;
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
        void* bits = nullptr;
        HDC mdc = CreateCompatibleDC(dc_);
        HBITMAP bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp) {
            DeleteDC(mdc);
            return;
        }
        HGDIOBJ oldBmp = SelectObject(mdc, bmp);
        memset(bits, 0, (size_t)w * h * 4);
        HFONT font = CreateFontW(-sizePx, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                 ANTIALIASED_QUALITY, 0, L"Malgun Gothic");
        HGDIOBJ oldFont = SelectObject(mdc, font);
        SetBkMode(mdc, TRANSPARENT);
        SetTextColor(mdc, RGB(255, 255, 255));
        RECT tr{r.left - cr.left, r.top - cr.top, r.right - cr.left, r.bottom - cr.top};
        DrawTextW(mdc, text.c_str(), -1, &tr, format | DT_NOPREFIX);
        GdiFlush();
        const uint32_t* mask = (const uint32_t*)bits;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                uint32_t cov = (mask[(size_t)y * w + x] >> 8) & 255;  // green: the grayscale coverage
                if (!cov) continue;
                uint32_t& p = px_[(size_t)(cr.top + y) * w_ + cr.left + x];
                auto over = [&](uint32_t src, int shift) {
                    return (src * cov + ((p >> shift) & 255) * (255 - cov)) / 255;
                };
                p = (over(255, 24) << 24) | (over(GetRValue(color), 16) << 16) | (over(GetGValue(color), 8) << 8) |
                    over(GetBValue(color), 0);
            }
        SelectObject(mdc, oldFont);
        DeleteObject(font);
        SelectObject(mdc, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mdc);
        return;
    }
    ProtectAlpha(r, [&] {
        HFONT font = CreateUiFont(sizePx, bold);
        HGDIOBJ old = SelectObject(dc_, font);
        SetBkMode(dc_, TRANSPARENT);
        SetTextColor(dc_, color);
        RECT tr = r;
        DrawTextW(dc_, text.c_str(), -1, &tr, format | DT_NOPREFIX);
        SelectObject(dc_, old);
        DeleteObject(font);
    });
}

void Canvas::Glyph(wchar_t glyph, const RECT& r, COLORREF color, int sizePx) {
    static const wchar_t* face = [] {
        // Windows 11 ships Segoe Fluent Icons; Windows 10 has Segoe MDL2 Assets (same code points).
        LOGFONTW lf{};
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, L"Segoe Fluent Icons");
        bool found = false;
        HDC dc = GetDC(nullptr);
        EnumFontFamiliesExW(dc, &lf, [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM p) -> int {
            *(bool*)p = true;
            return 0;
        }, (LPARAM)&found, 0);
        ReleaseDC(nullptr, dc);
        return found ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    }();
    ProtectAlpha(r, [&] {
        HFONT font = CreateFontW(-sizePx, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0,
                                 face);
        HGDIOBJ old = SelectObject(dc_, font);
        SetBkMode(dc_, TRANSPARENT);
        SetTextColor(dc_, color);
        RECT tr = r;
        wchar_t s[2] = {glyph, 0};
        DrawTextW(dc_, s, 1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc_, old);
        DeleteObject(font);
    });
}

void Canvas::DrawIcon(HICON icon, int x, int y, int size) {
    if (!icon) return;
    RECT r{x, y, x + size, y + size};
    ProtectAlpha(r, [&] { DrawIconEx(dc_, x, y, icon, size, size, 0, nullptr, DI_NORMAL); });
}

Image Canvas::ToImage() const {
    Image img;
    img.w = w_;
    img.h = h_;
    img.px.assign(px_, px_ + (size_t)w_ * h_);
    for (auto& p : img.px) p |= 0xFF000000u;
    return img;
}

void Canvas::Present(HWND hwnd, POINT pt) const {
    POINT src{0, 0};
    SIZE size{w_, h_};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd, nullptr, &pt, &size, dc_, &src, 0, &bf, ULW_ALPHA);
}

}  // namespace wp
