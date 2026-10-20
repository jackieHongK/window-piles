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
    // Scales `img` into `dst` (box filter down, nearest up), clipped to the rounded rect `radius`.
    void DrawImage(const Image& img, const RECT& dst, int radius = 0);
    // GDI text. Only pixels that were already opaque keep the text, so text must sit on an opaque fill.
    void Text(const std::wstring& text, const RECT& r, COLORREF color, int sizePx, bool bold, UINT format);
    void DrawIcon(HICON icon, int x, int y, int size);  // icon must sit on an opaque fill
    Image ToImage() const;                               // forces alpha 255

    // Pushes the canvas to a layered window at screen position `pt`.
    void Present(HWND hwnd, POINT pt) const;

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

}  // namespace wp
