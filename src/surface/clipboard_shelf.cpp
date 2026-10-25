#include "clipboard_shelf.h"

#include <ole2.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

#include "common/win_util.h"

namespace wp {

namespace {

constexpr int kMaxSide = 2048;

UINT Format(const wchar_t* name) { return RegisterClipboardFormatW(name); }

// Password managers and other private content say so on the clipboard; such content isn't kept.
bool Private() {
    if (IsClipboardFormatAvailable(Format(L"ExcludeClipboardContentFromMonitorProcessing")) ||
        IsClipboardFormatAvailable(Format(L"Clipboard Viewer Ignore")))
        return true;
    UINT can = Format(L"CanIncludeInClipboardHistory");
    if (HANDLE h = IsClipboardFormatAvailable(can) ? GetClipboardData(can) : nullptr) {
        if (auto* v = (const DWORD*)GlobalLock(h)) {
            bool no = *v == 0;
            GlobalUnlock(h);
            if (no) return true;
        }
    }
    return false;
}

IWICImagingFactory* Wic() {
    static IWICImagingFactory* f = [] {
        IWICImagingFactory* p = nullptr;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&p));
        return p;
    }();
    return f;
}

// Scaled down (box filter) so neither side is over `side`.
Image Shrink(Image img, int side) {
    if (img.w <= side && img.h <= side) return img;
    double k = std::min((double)side / img.w, (double)side / img.h);
    Image out;
    out.w = std::max(1, (int)(img.w * k));
    out.h = std::max(1, (int)(img.h * k));
    out.px.resize((size_t)out.w * out.h);
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x) {
            int sx0 = x * img.w / out.w, sx1 = std::max(sx0 + 1, (x + 1) * img.w / out.w);
            int sy0 = y * img.h / out.h, sy1 = std::max(sy0 + 1, (y + 1) * img.h / out.h);
            uint64_t a = 0, r = 0, g = 0, b = 0, n = 0;
            for (int sy = sy0; sy < sy1; ++sy)
                for (int sx = sx0; sx < sx1; ++sx) {
                    uint32_t p = img.px[(size_t)sy * img.w + sx];
                    a += p >> 24, r += (p >> 16) & 255, g += (p >> 8) & 255, b += p & 255, ++n;
                }
            out.px[(size_t)y * out.w + x] = (uint32_t)(a / n) << 24 | (uint32_t)(r / n) << 16 | (uint32_t)(g / n) << 8 | (uint32_t)(b / n);
        }
    return out;
}

// A PNG on the clipboard (browsers, design tools): the one format that keeps transparency.
bool DecodePng(const void* data, size_t size, Image& out) {
    IWICImagingFactory* f = Wic();
    if (!f) return false;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    bool ok = SUCCEEDED(f->CreateStream(&stream)) &&
              SUCCEEDED(stream->InitializeFromMemory((BYTE*)data, (DWORD)size)) &&
              SUCCEEDED(f->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &dec)) &&
              SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(f->CreateFormatConverter(&conv)) &&
              SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                         WICBitmapPaletteTypeCustom));
    UINT w = 0, h = 0;
    if (ok) ok = SUCCEEDED(conv->GetSize(&w, &h)) && w && h && w * h < 64u * 1024 * 1024;
    if (ok) {
        out.w = (int)w;
        out.h = (int)h;
        out.px.resize((size_t)w * h);
        ok = SUCCEEDED(conv->CopyPixels(nullptr, w * 4, (UINT)(out.px.size() * 4), (BYTE*)out.px.data()));
    }
    for (IUnknown* u : {(IUnknown*)conv, (IUnknown*)frame, (IUnknown*)dec, (IUnknown*)stream})
        if (u) u->Release();
    return ok;
}

// A device-independent bitmap (what most apps copy): 24- or 32-bit, either way up.
bool ReadDib(const BYTE* p, size_t size, Image& out, bool& alpha) {
    auto* bi = (const BITMAPINFOHEADER*)p;
    if (size < sizeof(BITMAPINFOHEADER) || (bi->biBitCount != 32 && bi->biBitCount != 24)) return false;
    if (bi->biCompression != BI_RGB && bi->biCompression != BI_BITFIELDS) return false;
    int w = bi->biWidth, h = std::abs(bi->biHeight);
    if (w <= 0 || h <= 0 || (size_t)w * h > 64u * 1024 * 1024) return false;
    size_t offset = bi->biSize + (bi->biCompression == BI_BITFIELDS && bi->biSize == sizeof(BITMAPINFOHEADER) ? 12 : 0);
    size_t stride = ((size_t)w * bi->biBitCount / 8 + 3) & ~(size_t)3;
    if (offset + stride * h > size) return false;
    out.w = w;
    out.h = h;
    out.px.resize((size_t)w * h);
    bool anyAlpha = false;
    for (int y = 0; y < h; ++y) {
        const BYTE* row = p + offset + stride * (bi->biHeight > 0 ? h - 1 - y : y);
        for (int x = 0; x < w; ++x) {
            const BYTE* s = row + x * (bi->biBitCount / 8);
            uint32_t a = bi->biBitCount == 32 ? s[3] : 255;
            anyAlpha |= a != 0;
            out.px[(size_t)y * w + x] = a << 24 | (uint32_t)s[2] << 16 | (uint32_t)s[1] << 8 | s[0];
        }
    }
    // A 32-bit bitmap with every alpha at 0 means "no alpha"; otherwise its colors still need premultiplying.
    alpha = false;
    for (uint32_t& px : out.px) {
        uint32_t a = anyAlpha ? px >> 24 : 255;
        if (a < 255) alpha = true;
        if (a == 255) {
            px |= 0xFF000000u;
            continue;
        }
        auto m = [&](int s) { return ((px >> s) & 255) * a / 255; };
        px = a << 24 | m(16) << 16 | m(8) << 8 | m(0);
    }
    return true;
}

HGLOBAL Global(const void* data, size_t size) {
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!h) return nullptr;
    memcpy(GlobalLock(h), data, size);
    GlobalUnlock(h);
    return h;
}

// The image as a 32-bit top-down DIB (alpha straight, as CF_DIBV5 readers expect).
HGLOBAL MakeDib(const Image& img) {
    BITMAPV5HEADER bh{};
    bh.bV5Size = sizeof(bh);
    bh.bV5Width = img.w;
    bh.bV5Height = -img.h;
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_BITFIELDS;
    bh.bV5RedMask = 0x00FF0000, bh.bV5GreenMask = 0x0000FF00, bh.bV5BlueMask = 0x000000FF, bh.bV5AlphaMask = 0xFF000000;
    bh.bV5CSType = LCS_sRGB;
    std::vector<BYTE> buf(sizeof(bh) + img.px.size() * 4);
    memcpy(buf.data(), &bh, sizeof(bh));
    auto* dst = (uint32_t*)(buf.data() + sizeof(bh));
    for (size_t i = 0; i < img.px.size(); ++i) {
        uint32_t p = img.px[i], a = p >> 24;
        auto u = [&](int s) { return a ? std::min<uint32_t>(255, ((p >> s) & 255) * 255 / a) : 0u; };
        dst[i] = a << 24 | u(16) << 16 | u(8) << 8 | u(0);
    }
    return Global(buf.data(), buf.size());
}

// The image as a PNG (premultiplied in, straight out via WIC).
std::string EncodePng(const Image& img) {
    std::string out;
    IWICImagingFactory* f = Wic();
    if (!f) return out;
    IStream* stream = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IWICBitmap* bmp = nullptr;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
    bool ok = SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) &&
              SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
              SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) &&
              SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(img.w, img.h)) &&
              SUCCEEDED(f->CreateBitmapFromMemory(img.w, img.h, GUID_WICPixelFormat32bppPBGRA, img.w * 4,
                                                  (UINT)(img.px.size() * 4), (BYTE*)img.px.data(), &bmp)) &&
              SUCCEEDED(frame->SetPixelFormat(&fmt)) && SUCCEEDED(frame->WriteSource(bmp, nullptr)) &&
              SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
    HGLOBAL h = nullptr;
    if (ok && SUCCEEDED(GetHGlobalFromStream(stream, &h))) {
        STATSTG st{};
        stream->Stat(&st, STATFLAG_NONAME);
        out.assign((const char*)GlobalLock(h), (size_t)st.cbSize.QuadPart);
        GlobalUnlock(h);
    }
    for (IUnknown* u : {(IUnknown*)bmp, (IUnknown*)frame, (IUnknown*)enc, (IUnknown*)stream})
        if (u) u->Release();
    return out;
}

}  // namespace

bool ClipItem::Same(const ClipItem& o) const {
    if (!text.empty() || !o.text.empty()) return text == o.text;
    return image.w == o.image.w && image.h == o.image.h && image.px == o.image.px;
}

bool ReadClipboard(HWND owner, ClipItem& out) {
    if (!OpenClipboard(owner)) return false;
    out = {};
    if (!Private()) {
        UINT png = Format(L"PNG");
        if (HANDLE h = IsClipboardFormatAvailable(png) ? GetClipboardData(png) : nullptr) {
            if (const void* p = GlobalLock(h)) {
                if (DecodePng(p, GlobalSize(h), out.image))
                    out.alpha = std::any_of(out.image.px.begin(), out.image.px.end(), [](uint32_t px) { return (px >> 24) < 255; });
                GlobalUnlock(h);
            }
        }
        if (out.image.empty())
            if (HANDLE h = IsClipboardFormatAvailable(CF_DIB) ? GetClipboardData(CF_DIB) : nullptr) {
                if (auto* p = (const BYTE*)GlobalLock(h)) {
                    if (!ReadDib(p, GlobalSize(h), out.image, out.alpha)) out.image = {};
                    GlobalUnlock(h);
                }
            }
        if (out.image.empty())
            if (HANDLE h = IsClipboardFormatAvailable(CF_UNICODETEXT) ? GetClipboardData(CF_UNICODETEXT) : nullptr) {
                if (auto* p = (const wchar_t*)GlobalLock(h)) {
                    out.text = p;
                    GlobalUnlock(h);
                }
            }
    }
    CloseClipboard();
    if (!out.image.empty()) out.image = Shrink(std::move(out.image), kMaxSide);
    if (out.text.find_first_not_of(L" \t\r\n") == std::wstring::npos) out.text.clear();
    return !out.empty();
}

void CopyToClipboard(HWND owner, const ClipItem& item) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    if (!item.text.empty()) {
        if (HGLOBAL h = Global(item.text.c_str(), (item.text.size() + 1) * sizeof(wchar_t))) SetClipboardData(CF_UNICODETEXT, h);
    } else {
        if (HGLOBAL h = MakeDib(item.image)) SetClipboardData(CF_DIBV5, h);
        std::string png = EncodePng(item.image);
        if (!png.empty())
            if (HGLOBAL h = Global(png.data(), png.size())) SetClipboardData(Format(L"PNG"), h);
    }
    CloseClipboard();
}

void DragClipOut(const ClipItem& item) {
    // An image also goes as a PNG file, for apps that take dropped files (chat, mail).
    std::wstring file;
    if (!item.image.empty()) {
        std::string png = EncodePng(item.image);
        wchar_t dir[MAX_PATH];
        GetTempPathW(MAX_PATH, dir);
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t name[64];
        swprintf_s(name, L"Neverflow %04u%02u%02u-%02u%02u%02u.png", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        file = std::wstring(dir) + name;
        if (png.empty() || !WriteFileReplacing(file, png, false)) file.clear();
    }
    DWORD ui = GetCurrentThreadId();
    std::thread([item, file, ui] {
        // OLE drag and drop needs a single-threaded apartment, which the app's own thread isn't (WinRT capture).
        if (FAILED(OleInitialize(nullptr))) return;
        // The button went down on the app's thread: sharing its input state, the drag sees it still held.
        AttachThreadInput(GetCurrentThreadId(), ui, TRUE);
        IDataObject* data = nullptr;
        if (!file.empty()) {
            IShellItem* si = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(file.c_str(), nullptr, IID_PPV_ARGS(&si)))) {
                si->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data));
                si->Release();
            }
        }
        if (!data) SHCreateDataObject(nullptr, 0, nullptr, nullptr, IID_PPV_ARGS(&data));
        if (data) {
            FORMATETC fe{0, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            STGMEDIUM sm{TYMED_HGLOBAL};
            if (!item.text.empty()) {
                fe.cfFormat = CF_UNICODETEXT;
                sm.hGlobal = Global(item.text.c_str(), (item.text.size() + 1) * sizeof(wchar_t));
                if (sm.hGlobal && FAILED(data->SetData(&fe, &sm, TRUE))) GlobalFree(sm.hGlobal);
            } else {
                fe.cfFormat = CF_DIBV5;
                sm.hGlobal = MakeDib(item.image);
                if (sm.hGlobal && FAILED(data->SetData(&fe, &sm, TRUE))) GlobalFree(sm.hGlobal);
            }
            DWORD effect = 0;
            SHDoDragDrop(nullptr, data, nullptr, DROPEFFECT_COPY, &effect);
            data->Release();
        }
        AttachThreadInput(GetCurrentThreadId(), ui, FALSE);
        OleUninitialize();
    }).detach();
}

Image ClipThumb(const ClipItem& item, int w, int h) {
    Image out;
    if (item.image.empty()) return out;
    const Image& src = item.image;
    // Transparent: fitted inside, its transparency kept. Otherwise it fills the tile, cropped to fit.
    double k = item.alpha ? std::min((double)w / src.w, (double)h / src.h) : std::max((double)w / src.w, (double)h / src.h);
    // Rounded up, so a filled tile has no empty edge; a small image is enlarged (nearest pixel).
    int sw = std::max(1, (int)std::ceil(src.w * k)), sh = std::max(1, (int)std::ceil(src.h * k));
    Image scaled = Shrink(src, std::max(sw, sh));
    out.w = w;
    out.h = h;
    out.px.assign((size_t)w * h, 0);
    int ox = (w - sw) / 2, oy = (h - sh) / 2;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int dx = x - ox, dy = y - oy;
            if (dx < 0 || dy < 0 || dx >= sw || dy >= sh) continue;
            int sx = std::min(scaled.w - 1, dx * scaled.w / sw), sy = std::min(scaled.h - 1, dy * scaled.h / sh);
            out.px[(size_t)y * w + x] = scaled.px[(size_t)sy * scaled.w + sx];
        }
    return out;
}

}  // namespace wp
