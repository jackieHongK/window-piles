// Writes the app mark (DrawMark) as a multi-size .ico for the exe's icon resource, so Explorer,
// shortcuts and pinned items show the same mark the app draws at runtime. Run by the build.
//   16, 20, 24, 32, 40, 48, 64 px: 32bpp BMP entries; 256 px: PNG entry (keeps the exe small)
// Usage: make_icon <out.ico>
#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "app/app_icon.h"
#include "common/canvas.h"

using namespace wp;

namespace {

using Bytes = std::vector<unsigned char>;

void Append(Bytes& b, const void* p, size_t n) { b.insert(b.end(), (const unsigned char*)p, (const unsigned char*)p + n); }

// The mark in straight (non-premultiplied) BGRA, top-down. Canvas pixels are premultiplied.
std::vector<uint32_t> Mark(int size) {
    Canvas c(size, size);
    DrawMark(c, 0, 0, size);
    std::vector<uint32_t> px(c.pixels(), c.pixels() + (size_t)size * size);
    for (uint32_t& p : px) {
        uint32_t a = p >> 24;
        if (a == 0 || a == 255) continue;
        auto ch = [&](int shift) { return std::min<uint32_t>(255, (((p >> shift) & 255) * 255 + a / 2) / a); };
        p = (a << 24) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
    }
    return px;
}

Bytes BmpEntry(int size) {
    std::vector<uint32_t> px = Mark(size);
    BITMAPINFOHEADER bh{};
    bh.biSize = sizeof(bh);
    bh.biWidth = size;
    bh.biHeight = size * 2;  // color + AND mask
    bh.biPlanes = 1;
    bh.biBitCount = 32;
    Bytes b;
    Append(b, &bh, sizeof(bh));
    for (int y = size - 1; y >= 0; --y) Append(b, &px[(size_t)y * size], (size_t)size * 4);  // bottom-up
    b.resize(b.size() + (size_t)((size + 31) / 32 * 4) * size, 0);  // AND mask unused with 32bpp alpha
    return b;
}

Bytes PngEntry(int size) {
    std::vector<uint32_t> px = Mark(size);
    Bytes out;
    IWICImagingFactory* factory = nullptr;
    IStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    HGLOBAL mem = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) &&
        SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->SetSize(size, size)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
        format == GUID_WICPixelFormat32bppBGRA &&
        SUCCEEDED(frame->WritePixels(size, size * 4, (UINT)px.size() * 4, (BYTE*)px.data())) &&
        SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()) && SUCCEEDED(GetHGlobalFromStream(stream, &mem))) {
        STATSTG st{};
        stream->Stat(&st, STATFLAG_NONAME);
        Append(out, GlobalLock(mem), (size_t)st.cbSize.QuadPart);
        GlobalUnlock(mem);
    }
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    return out;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        fwprintf(stderr, L"usage: make_icon <out.ico>\n");
        return 2;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const int sizes[] = {16, 20, 24, 32, 40, 48, 64, 256};
    const int n = ARRAYSIZE(sizes);
    std::vector<Bytes> images;
    for (int s : sizes) images.push_back(s == 256 ? PngEntry(s) : BmpEntry(s));
    CoUninitialize();
    if (images.back().empty()) {
        fwprintf(stderr, L"make_icon: PNG encoding failed\n");
        return 1;
    }

    // ICONDIR + ICONDIRENTRY[n], then the images.
    Bytes ico;
    WORD dir[3] = {0, 1, (WORD)n};
    Append(ico, dir, sizeof(dir));
    DWORD offset = (DWORD)(sizeof(dir) + 16 * n);
    for (int i = 0; i < n; ++i) {
        BYTE wh = sizes[i] >= 256 ? 0 : (BYTE)sizes[i];
        BYTE head[4] = {wh, wh, 0, 0};
        WORD planesBpp[2] = {1, 32};
        DWORD sizeOff[2] = {(DWORD)images[i].size(), offset};
        Append(ico, head, 4);
        Append(ico, planesBpp, 4);
        Append(ico, sizeOff, 8);
        offset += sizeOff[0];
    }
    for (const Bytes& b : images) Append(ico, b.data(), b.size());

    FILE* f = nullptr;
    if (_wfopen_s(&f, argv[1], L"wb") || !f) {
        fwprintf(stderr, L"make_icon: cannot write %ls\n", argv[1]);
        return 1;
    }
    fwrite(ico.data(), 1, ico.size(), f);
    fclose(f);
    wprintf(L"wrote %ls (%zu bytes)\n", argv[1], ico.size());
    return 0;
}
