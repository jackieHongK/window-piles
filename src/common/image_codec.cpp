#include "image_codec.h"

namespace wp {

IWICImagingFactory* Wic() {
    static IWICImagingFactory* f = [] {
        IWICImagingFactory* p = nullptr;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&p));
        return p;
    }();
    return f;
}

bool DecodeImage(const void* data, size_t size, Image& out) {
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

}  // namespace wp
