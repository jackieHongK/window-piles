// Writes the app mark and the tray icons as one BMP sheet for review:
//   row 1: color mark at 16, 20, 24, 32, 48, 64, 128 px
//   row 2: tray icon on a dark taskbar, row 3: on a light taskbar (16, 20, 24, 32 px)
// Usage: icon_preview <out.bmp>
#include <windows.h>

#include <cstdio>

#include "app/app_icon.h"
#include "common/canvas.h"

using namespace wp;

namespace {

// Draws an HICON (with alpha) into the canvas over its current pixels.
void Blit(Canvas& c, HICON icon, int x, int y, int size) { c.DrawIcon(icon, x, y, size); }

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const wchar_t* out = argc > 1 ? argv[1] : L"icons.bmp";
    const int sizes[] = {16, 20, 24, 32, 48, 64, 128};
    const int tray[] = {16, 20, 24, 32};
    const int W = 520, H = 260;
    Canvas c(W, H);
    c.FillRoundRect({0, 0, W, H}, 0, RGB(246, 244, 239));
    int x = 12;
    for (int s : sizes) {
        DrawMark(c, x, 12, s);
        x += s + 14;
    }
    c.FillRoundRect({0, 150, W, 200}, 0, RGB(32, 32, 32));   // dark taskbar
    c.FillRoundRect({0, 205, W, 255}, 0, RGB(238, 238, 238)); // light taskbar
    x = 12;
    for (int s : tray) {
        HICON dark = MakeTrayIcon(s, false), light = MakeTrayIcon(s, true);
        Blit(c, dark, x, 175 - s / 2, s);
        Blit(c, light, x, 230 - s / 2, s);
        DestroyIcon(dark);
        DestroyIcon(light);
        x += s + 24;
    }

    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER bh{};
    bh.biSize = sizeof(bh);
    bh.biWidth = W;
    bh.biHeight = -H;
    bh.biPlanes = 1;
    bh.biBitCount = 32;
    DWORD bytes = (DWORD)W * H * 4;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(bh);
    fh.bfSize = fh.bfOffBits + bytes;
    FILE* f = nullptr;
    if (_wfopen_s(&f, out, L"wb") || !f) return 1;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&bh, sizeof(bh), 1, f);
    fwrite(c.pixels(), 1, bytes, f);
    fclose(f);
    wprintf(L"wrote %ls\n", out);
    return 0;
}
