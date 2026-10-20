#include "snapshot.h"

#include <unknwn.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>

#include <algorithm>
#include <atomic>

#include "common/win_util.h"

namespace wp {

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wd3d = winrt::Windows::Graphics::DirectX::Direct3D11;
using winrt::com_ptr;

namespace {

// Budget after the last capture session has started; a frame normally arrives within ~20 ms.
constexpr int kFrameTimeoutMs = 150;

struct Device {
    com_ptr<ID3D11Device> d3d;
    com_ptr<ID3D11DeviceContext> ctx;
    wd3d::IDirect3DDevice winrt{nullptr};
    bool mips = false;
};

bool CreateDevice(D3D_DRIVER_TYPE type, Device& out) {
    D3D_FEATURE_LEVEL fl;
    if (FAILED(D3D11CreateDevice(nullptr, type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, out.d3d.put(), &fl, out.ctx.put())))
        return false;
    if (auto mt = out.d3d.try_as<ID3D10Multithread>()) mt->SetMultithreadProtected(TRUE);
    com_ptr<::IInspectable> insp;
    if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(out.d3d.as<IDXGIDevice>().get(), insp.put()))) return false;
    out.winrt = insp.as<wd3d::IDirect3DDevice>();
    UINT support = 0;
    out.d3d->CheckFormatSupport(DXGI_FORMAT_B8G8R8A8_UNORM, &support);
    out.mips = (support & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN) != 0;
    return true;
}

// GPU mip reduction to the smallest level >= target width, then CPU area-average to exactly target.
bool Downscale(Device& dev, ID3D11Texture2D* frame, int cw, int ch, int targetW, Image& out) {
    int level = 0, lw = cw, lh = ch;
    com_ptr<ID3D11Texture2D> mip;
    if (dev.mips) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = cw;
        d.Height = ch;
        d.MipLevels = 0;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        if (FAILED(dev.d3d->CreateTexture2D(&d, nullptr, mip.put()))) return false;
        D3D11_BOX box{0, 0, 0, (UINT)cw, (UINT)ch, 1};
        dev.ctx->CopySubresourceRegion(mip.get(), 0, 0, 0, 0, frame, 0, &box);
        com_ptr<ID3D11ShaderResourceView> srv;
        if (FAILED(dev.d3d->CreateShaderResourceView(mip.get(), nullptr, srv.put()))) return false;
        dev.ctx->GenerateMips(srv.get());
        D3D11_TEXTURE2D_DESC md;
        mip->GetDesc(&md);
        while ((lw >> 1) >= targetW && (lh >> 1) >= 1 && level + 1 < (int)md.MipLevels) {
            lw >>= 1;
            lh >>= 1;
            ++level;
        }
    }
    D3D11_TEXTURE2D_DESC sd{};
    sd.Width = lw;
    sd.Height = lh;
    sd.MipLevels = 1;
    sd.ArraySize = 1;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    com_ptr<ID3D11Texture2D> staging;
    if (FAILED(dev.d3d->CreateTexture2D(&sd, nullptr, staging.put()))) return false;
    if (mip) {
        dev.ctx->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, mip.get(), level, nullptr);
    } else {
        D3D11_BOX box{0, 0, 0, (UINT)cw, (UINT)ch, 1};
        dev.ctx->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, frame, 0, &box);
    }
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(dev.ctx->Map(staging.get(), 0, D3D11_MAP_READ, 0, &map))) return false;
    int tw = std::min(targetW, lw);
    int th = std::max(1, (int)((double)lh * tw / lw + 0.5));
    out.w = tw;
    out.h = th;
    out.px.assign((size_t)tw * th, 0);
    for (int y = 0; y < th; ++y) {
        int y0 = y * lh / th, y1 = std::max(y0 + 1, (y + 1) * lh / th);
        for (int x = 0; x < tw; ++x) {
            int x0 = x * lw / tw, x1 = std::max(x0 + 1, (x + 1) * lw / tw);
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int yy = y0; yy < y1; ++yy) {
                const uint8_t* row = (const uint8_t*)map.pData + (size_t)yy * map.RowPitch;
                for (int xx = x0; xx < x1; ++xx) {
                    b += row[xx * 4 + 0];
                    g += row[xx * 4 + 1];
                    r += row[xx * 4 + 2];
                    ++n;
                }
            }
            out.px[(size_t)y * tw + x] = 0xFF000000u | ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    }
    dev.ctx->Unmap(staging.get(), 0);
    return true;
}

struct Job {
    HWND hwnd = nullptr;
    int targetW = 240;
    Image image;
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    winrt::event_token token{};
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::atomic<bool> handled{false};
    ~Job() { CloseHandle(done); }
};

}  // namespace

struct SnapshotEngine::Impl {
    Device dev;
    bool haveDevice = false;
    bool borderless = false;
    bool borderlessAsked = false;
    double lastUse = 0;

    bool EnsureDevice() {
        if (haveDevice) return true;
        // WARP first: same latency as the GPU for one-shot captures but ~1.5 MB instead of ~43 MB
        // (PoC-03), and independent of GPU driver quality on low-end PCs.
        haveDevice = CreateDevice(D3D_DRIVER_TYPE_WARP, dev) || CreateDevice(D3D_DRIVER_TYPE_HARDWARE, dev);
        return haveDevice;
    }

    void AskBorderless() {
        if (borderlessAsked) return;
        borderlessAsked = true;
        try {
            auto status =
                wgc::GraphicsCaptureAccess::RequestAccessAsync(wgc::GraphicsCaptureAccessKind::Borderless).get();
            borderless = status ==
                         winrt::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus::Allowed;
        } catch (...) {
            borderless = false;  // Windows 10: the yellow capture border is shown briefly
        }
    }

    void Start(const std::shared_ptr<Job>& jobPtr) {
        Job& job = *jobPtr;
        try {
            auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
            if (FAILED(interop->CreateForWindow(job.hwnd, winrt::guid_of<wgc::GraphicsCaptureItem>(),
                                                winrt::put_abi(job.item))) ||
                !job.item) {
                SetEvent(job.done);
                return;
            }
            auto size = job.item.Size();
            if (size.Width <= 0 || size.Height <= 0) {
                SetEvent(job.done);
                return;
            }
            job.pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
                dev.winrt, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, size);
            job.session = job.pool.CreateCaptureSession(job.item);
            try {
                job.session.IsCursorCaptureEnabled(false);
            } catch (...) {
            }
            if (borderless) {
                try {
                    job.session.IsBorderRequired(false);
                } catch (...) {
                }
            }
            Device* d = &dev;
            // The handler holds its own reference: a frame that arrives after the timeout must not
            // touch a destroyed job. Close() revokes the handler, which breaks the reference cycle.
            job.token = job.pool.FrameArrived([jobPtr, d](wgc::Direct3D11CaptureFramePool const& sender, auto&&) {
                Job& job = *jobPtr;
                auto frame = sender.TryGetNextFrame();
                if (!frame || job.handled.exchange(true)) return;
                auto access =
                    frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
                com_ptr<ID3D11Texture2D> tex;
                access->GetInterface(IID_PPV_ARGS(tex.put()));
                auto cs = frame.ContentSize();
                D3D11_TEXTURE2D_DESC td;
                tex->GetDesc(&td);
                int cw = std::min<int>(cs.Width, td.Width), ch = std::min<int>(cs.Height, td.Height);
                if (!Downscale(*d, tex.get(), cw, ch, job.targetW, job.image)) job.image = Image{};
                SetEvent(job.done);
            });
            job.session.StartCapture();
        } catch (...) {
            SetEvent(job.done);
        }
    }

    static void Close(Job& job) {
        try {
            if (job.pool) {
                job.pool.FrameArrived(job.token);
                job.pool.Close();
            }
            if (job.session) job.session.Close();
        } catch (...) {
        }
        job.pool = nullptr;
        job.session = nullptr;
        job.item = nullptr;
    }
};

SnapshotEngine::SnapshotEngine() : impl_(std::make_unique<Impl>()) {}
SnapshotEngine::~SnapshotEngine() = default;

std::vector<Image> SnapshotEngine::Capture(const std::vector<HWND>& windows, int targetWidth) {
    std::vector<Image> result(windows.size());
    std::vector<std::shared_ptr<Job>> jobs(windows.size());
    bool any = false;
    for (HWND h : windows)
        if (IsWindowVisible(h) && !IsIconic(h)) any = true;
    if (!any || !wgc::GraphicsCaptureSession::IsSupported()) return result;
    impl_->AskBorderless();
    if (!impl_->EnsureDevice()) return result;

    for (size_t i = 0; i < windows.size(); ++i) {
        if (!IsWindowVisible(windows[i]) || IsIconic(windows[i])) continue;  // no frame would arrive
        jobs[i] = std::make_shared<Job>();
        jobs[i]->hwnd = windows[i];
        jobs[i]->targetW = targetWidth;
        impl_->Start(jobs[i]);
    }
    double deadline = NowMs() + kFrameTimeoutMs;
    for (size_t i = 0; i < jobs.size(); ++i) {
        if (!jobs[i]) continue;
        double left = deadline - NowMs();
        bool got = WaitForSingleObject(jobs[i]->done, (DWORD)std::max(0.0, left)) == WAIT_OBJECT_0;
        // Claim the job; if a late frame is mid-flight it either finished (done set) or will bail out.
        if (!got && !jobs[i]->handled.exchange(true)) got = false;
        else if (!got) got = WaitForSingleObject(jobs[i]->done, 50) == WAIT_OBJECT_0;
        Impl::Close(*jobs[i]);
        if (got) result[i] = std::move(jobs[i]->image);
    }
    impl_->lastUse = NowMs();
    return result;
}

void SnapshotEngine::ReleaseIfIdle(double idleMs) {
    if (impl_->haveDevice && NowMs() - impl_->lastUse > idleMs) {
        impl_->dev = Device{};
        impl_->haveDevice = false;
    }
}

Image MakeIconCard(HWND hwnd, int w, int h) {
    Canvas c(w, h);
    c.FillRoundRect({0, 0, w, h}, 0, RGB(52, 54, 62));
    HICON icon = nullptr;
    DWORD_PTR res = 0;
    if (SendMessageTimeoutW(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &res) && res)
        icon = (HICON)res;
    if (!icon) icon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICON);
    if (!icon) icon = LoadIconW(nullptr, IDI_APPLICATION);
    int is = std::min(w, h) * 2 / 5;
    c.DrawIcon(icon, (w - is) / 2, h / 2 - is + h / 10, is);
    std::wstring title = WindowTitle(hwnd);
    RECT tr{8, h / 2 + h / 8, w - 8, h - 6};
    c.Text(title, tr, RGB(235, 235, 240), std::max(10, h / 9), false, DT_CENTER | DT_END_ELLIPSIS | DT_SINGLELINE);
    return c.ToImage();
}

}  // namespace wp
