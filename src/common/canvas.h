// CPU-side 32bpp premultiplied-alpha drawing for layered windows (UpdateLayeredWindow).
// Deliberately GDI/CPU only: works the same on PCs without a usable GPU, in VMs and over RDP,
// and costs nothing between state changes because surfaces are only redrawn when something changes.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wp {

// Opaque BGRA image (alpha = 255), e.g. a window snapshot.
struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
    bool empty() const { return w <= 0 || h <= 0 || px.empty(); }
};

class Canvas {
public:
    Canvas(int w, int h);
    ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    int width() const { return w_; }
    int height() const { return h_; }
    HDC dc() const { return dc_; }
    uint32_t* pixels() { return px_; }

    void Clear();
    // Anti-aliased rounded rectangle, source-over with `alpha` (0..255).
    void FillRoundRect(const RECT& r, int radius, COLORREF color, BYTE alpha = 255);
    void StrokeRoundRect(const RECT& r, int radius, int thickness, COLORREF color, BYTE alpha = 255);
    // Dashed outline; straight edges are dashed, corners solid.
    void DashedRoundRect(const RECT& r, int radius, int thickness, int dash, int gap, COLORREF color, BYTE alpha = 255);
    // Soft drop shadow under a rounded rect (layered approximation of a blur).
    void Shadow(const RECT& r, int radius, int blur, int offsetY, BYTE alpha, COLORREF color = RGB(0, 0, 0));
    // Rounded rect filled with a vertical gradient (opaque).
    void FillRoundRectV(const RECT& r, int radius, COLORREF top, COLORREF bottom);
    // Scales `img` into `dst` (box filter down, nearest up), clipped to the rounded rect `radius`.
    void DrawImage(const Image& img, const RECT& dst, int radius = 0);
    // GDI text. Only pixels that were already opaque keep the text, so text must sit on an opaque fill.
    void Text(const std::wstring& text, const RECT& r, COLORREF color, int sizePx, bool bold, UINT format);
    void DrawIcon(HICON icon, int x, int y, int size);  // icon must sit on an opaque fill
    // A Segoe Fluent/MDL2 icon glyph (e.g. 0xE70F = Edit) centered in `r`; must sit on an opaque fill.
    void Glyph(wchar_t glyph, const RECT& r, COLORREF color, int sizePx);
    Image ToImage() const;                               // forces alpha 255

    // Text measurement with the same font Text() uses.
    SIZE MeasureText(const std::wstring& text, int sizePx, bool bold, int wrapWidth = 0) const;

    // Pushes the canvas to a layered window at screen position `pt`.
    void Present(HWND hwnd, POINT pt) const;
    // Copies the canvas into a normal (non-layered) window's DC; the canvas must be opaque.
    void Blit(HDC dst) const { BitBlt(dst, 0, 0, w_, h_, dc_, 0, 0, SRCCOPY); }

private:
    int w_, h_;
    HDC dc_ = nullptr;
    HBITMAP bmp_ = nullptr;
    HGDIOBJ old_ = nullptr;
    uint32_t* px_ = nullptr;

    template <typename F>
    void ProtectAlpha(const RECT& r, F&& drawWithGdi);
};

// Fits an image of size (w,h) into box, preserving aspect ratio (centered).
RECT FitRect(int w, int h, const RECT& box);

// UI font (Malgun Gothic) at a pixel size; caller deletes.
HFONT CreateUiFont(int sizePx, bool bold);
// Measures text without a Canvas (e.g. to size a window before drawing it).
SIZE MeasureUiText(const std::wstring& text, int sizePx, bool bold, int wrapWidth = 0);

}  // namespace wp
