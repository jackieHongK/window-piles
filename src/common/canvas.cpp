#include "canvas.h"

#include <algorithm>
#include <cmath>

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

void Canvas::FillRoundRect(const RECT& r, int radius, COLORREF color, BYTE alpha) {
    int x0 = std::max<int>(0, r.left), y0 = std::max<int>(0, r.top);
    int x1 = std::min<int>(w_, r.right), y1 = std::min<int>(h_, r.bottom);
    for (int y = y0; y < y1; ++y) {
        uint32_t* row = px_ + (size_t)y * w_;
        for (int x = x0; x < x1; ++x) {
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
        for (int x = x0; x < x1; ++x) {
            float outer = RoundRectCoverage(x + 0.5f, y + 0.5f, r, (float)radius);
            float in = RoundRectCoverage(x + 0.5f, y + 0.5f, inner, (float)std::max(0, radius - t));
            float cov = outer - in;
            if (cov <= 0) continue;
            row[x] = Over(Premul(color, (int)(alpha * cov + 0.5f)), row[x]);
        }
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

void Canvas::Text(const std::wstring& text, const RECT& r, COLORREF color, int sizePx, bool bold, UINT format) {
    ProtectAlpha(r, [&] {
        HFONT font = CreateFontW(-sizePx, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                 CLEARTYPE_QUALITY, 0, L"Malgun Gothic");
        HGDIOBJ old = SelectObject(dc_, font);
        SetBkMode(dc_, TRANSPARENT);
        SetTextColor(dc_, color);
        RECT tr = r;
        DrawTextW(dc_, text.c_str(), -1, &tr, format | DT_NOPREFIX);
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
