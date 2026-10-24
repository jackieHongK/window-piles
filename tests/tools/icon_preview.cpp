// Writes the app mark as one BMP sheet for review:
//   row 1: the mark at 16, 20, 24, 32, 48, 64, 128 px
//   row 2: tray sizes (16, 20, 24, 32 px) on a light overflow flyout and on a dark taskbar,
//          then 24 and 32 px magnified 4× (nearest) to judge the pixels
//   row 3: the lockup (mark + outlined wordmark) as in the usage window, on paper and on ink
// Usage: icon_preview <out.bmp>
#include <windows.h>

#include <cstdio>

#include "app/app_icon.h"
#include "common/canvas.h"
#include "ui/wordmark.h"

using namespace wp;

namespace {

// Nearest-neighbour copy of the size×size block at (sx, sy) to (dx, dy), `zoom` times larger.
void Magnify(Canvas& c, int sx, int sy, int size, int dx, int dy, int zoom) {
    uint32_t* px = c.pixels();
    for (int y = 0; y < size * zoom; ++y)
        for (int x = 0; x < size * zoom; ++x)
            px[(size_t)(dy + y) * c.width() + dx + x] = px[(size_t)(sy + y / zoom) * c.width() + sx + x / zoom];
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const wchar_t* out = argc > 1 ? argv[1] : L"icons.bmp";
    const int sizes[] = {16, 20, 24, 32, 48, 64, 128};
    const int tray[] = {16, 20, 24, 32};
    const int W = 640, H = 420;
    Canvas c(W, H);
    c.FillRoundRect({0, 0, W, H}, 0, RGB(246, 244, 239));
    int x = 12;
    for (int s : sizes) {
        DrawMark(c, x, 12, s);
        x += s + 14;
    }
    c.FillRoundRect({0, 150, 200, 225}, 0, RGB(243, 243, 243));  // light overflow flyout
    c.FillRoundRect({0, 225, 200, 300}, 0, RGB(32, 32, 32));     // dark taskbar
    x = 12;
    int at24 = 0, at32 = 0;
    for (int s : tray) {
        DrawMark(c, x, 187 - s / 2, s);
        DrawMark(c, x, 262 - s / 2, s);
        if (s == 24) at24 = x;
        if (s == 32) at32 = x;
        x += s + 14;
    }
    Magnify(c, at24, 187 - 12, 24, 220, 160, 4);
    Magnify(c, at32, 187 - 16, 32, 330, 160, 4);
    Magnify(c, at32, 262 - 16, 32, 470, 160, 4);
    c.FillRoundRect({0, 360, W, H}, 0, RGB(26, 25, 23));  // ink
    // Usage window at 100% and 150%: 40 px mark, 17 px wordmark; and a large one.
    DrawMark(c, 12, 310, 40);
    DrawWordmark(c, 64, 322, 17, RGB(26, 25, 23));
    DrawMark(c, 260, 302, 60);
    DrawWordmark(c, 338, 320, 26, RGB(26, 25, 23));
    DrawMark(c, 12, 370, 40);
    DrawWordmark(c, 64, 382, 17, RGB(243, 239, 232));

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
