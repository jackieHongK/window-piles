// PoC-03 — Windows Graphics Capture one-shot snapshot pipeline (the MVP's guaranteed preview path).
//
//   visible window --WGC 1 frame--> GPU texture --GenerateMips + CPU box filter--> 240px BGRA
//   --> hide original (SW_HIDE) --> show snapshot in our own per-pixel-alpha layered window
//   --> verify on screen --> restore original
//
// Also measures: first-frame latency (sequential and parallel), hardware vs WARP device,
// memory before/after creating and releasing D3D, minimized / capture-excluded windows,
// and whether the yellow capture border can be disabled on this OS.
//
// Usage: poc_wgc [--prefix TW-] [--hwnd 0x..]... [--warp-only] [--out results.md]
#include "poc_common.h"

#include <unknwn.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

using namespace poc;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wd3d = winrt::Windows::Graphics::DirectX::Direct3D11;
using winrt::com_ptr;

namespace {

constexpr int kThumbWidth = 240;

struct Device {
    com_ptr<ID3D11Device> d3d;
    com_ptr<ID3D11DeviceContext> ctx;
    wd3d::IDirect3DDevice winrtDevice{nullptr};
    bool warp = false;
    bool mipsSupported = false;
};

bool CreateDevice(bool warp, Device& out) {
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, out.d3d.put(),
                                   &fl, out.ctx.put());
    if (FAILED(hr)) return false;
    // Frame callbacks arrive on a worker thread; protect the immediate context.
    if (auto mt = out.d3d.try_as<ID3D10Multithread>()) mt->SetMultithreadProtected(TRUE);
    auto dxgi = out.d3d.as<IDXGIDevice>();
    com_ptr<::IInspectable> insp;
    if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), insp.put()))) return false;
    out.winrtDevice = insp.as<wd3d::IDirect3DDevice>();
    out.warp = warp;
    UINT support = 0;
    out.d3d->CheckFormatSupport(DXGI_FORMAT_B8G8R8A8_UNORM, &support);
    out.mipsSupported = (support & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN) != 0;
    return true;
}

struct Snapshot {
    bool ok = false;
    std::string failReason;
    double setupMs = 0;       // CreateForWindow + frame pool + session -> StartCapture
    double firstFrameMs = 0;  // StartCapture -> first frame
    double readyMs = 0;       // item creation -> CPU pixels ready
    double closeMs = 0;       // session + frame pool Close()
    double totalMs = 0;       // item creation -> CPU pixels ready, session closed
    int srcW = 0, srcH = 0;
    int w = 0, h = 0;
    std::vector<uint32_t> bgra;  // premultiplied, opaque
    double avgLuma = 0;
};

// GPU: copy content into a mip-mapped texture, GenerateMips, read back the smallest level that is
// still >= target width; CPU: area-average down to exactly kThumbWidth.
bool Downscale(Device& dev, ID3D11Texture2D* frame, int cw, int ch, int targetW, Snapshot& snap) {
    int level = 0;
    com_ptr<ID3D11Texture2D> source;
    int lw = cw, lh = ch;
    if (dev.mipsSupported) {
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
        com_ptr<ID3D11Texture2D> mip;
        if (FAILED(dev.d3d->CreateTexture2D(&d, nullptr, mip.put()))) return false;
        D3D11_BOX box{0, 0, 0, (UINT)cw, (UINT)ch, 1};
        dev.ctx->CopySubresourceRegion(mip.get(), 0, 0, 0, 0, frame, 0, &box);
        com_ptr<ID3D11ShaderResourceView> srv;
        if (FAILED(dev.d3d->CreateShaderResourceView(mip.get(), nullptr, srv.put()))) return false;
        dev.ctx->GenerateMips(srv.get());
        while ((lw >> 1) >= targetW && (lh >> 1) >= 1) {
            lw >>= 1;
            lh >>= 1;
            ++level;
        }
        D3D11_TEXTURE2D_DESC md;
        mip->GetDesc(&md);
        level = std::min(level, (int)md.MipLevels - 1);
        source = mip;
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
    if (source) {
        dev.ctx->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, source.get(), level, nullptr);
    } else {
        D3D11_BOX box{0, 0, 0, (UINT)cw, (UINT)ch, 1};
        dev.ctx->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, frame, 0, &box);
    }
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(dev.ctx->Map(staging.get(), 0, D3D11_MAP_READ, 0, &map))) return false;

    int tw = std::min(targetW, lw);
    int th = std::max(1, (int)((double)lh * tw / lw + 0.5));
    snap.w = tw;
    snap.h = th;
    snap.bgra.assign((size_t)tw * th, 0);
    double lumaSum = 0;
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
            r /= n, g /= n, b /= n;
            lumaSum += 0.299 * r + 0.587 * g + 0.114 * b;
            snap.bgra[(size_t)y * tw + x] = 0xFF000000u | (r << 16) | (g << 8) | b;  // force opaque
        }
    }
    dev.ctx->Unmap(staging.get(), 0);
    snap.avgLuma = lumaSum / ((double)tw * th);
    return true;
}

// One capture in flight: item + frame pool + session, closed right after the first frame.
struct CaptureJob {
    HWND hwnd = nullptr;
    Snapshot snap;
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    winrt::event_token token{};
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::atomic<bool> handled{false};
    double tStart = 0, tCapture = 0;
    ~CaptureJob() { CloseHandle(done); }
};

bool g_borderlessGranted = false;

void Close(CaptureJob& job) {
    if (job.pool) {
        job.pool.FrameArrived(job.token);
        job.pool.Close();
        job.pool = nullptr;
    }
    if (job.session) {
        job.session.Close();
        job.session = nullptr;
    }
    job.item = nullptr;
}

void StartCapture(Device& dev, CaptureJob& job) {
    job.tStart = NowMs();
    try {
        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        HRESULT hr = interop->CreateForWindow(job.hwnd, winrt::guid_of<wgc::GraphicsCaptureItem>(),
                                              winrt::put_abi(job.item));
        if (FAILED(hr) || !job.item) {
            job.snap.failReason = "CreateForWindow hr=" + Hex((uint32_t)hr);
            SetEvent(job.done);
            return;
        }
        auto size = job.item.Size();
        job.snap.srcW = size.Width;
        job.snap.srcH = size.Height;
        if (size.Width <= 0 || size.Height <= 0) {
            job.snap.failReason = "item size 0";
            SetEvent(job.done);
            return;
        }
        job.pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
            dev.winrtDevice, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, size);
        job.session = job.pool.CreateCaptureSession(job.item);
        try {
            job.session.IsCursorCaptureEnabled(false);
        } catch (...) {
        }
        if (g_borderlessGranted) {
            try {
                job.session.IsBorderRequired(false);
            } catch (...) {
            }
        }
        job.token = job.pool.FrameArrived([&job, &dev](wgc::Direct3D11CaptureFramePool const& sender, auto&&) {
            auto frame = sender.TryGetNextFrame();
            if (!frame || job.handled.exchange(true)) return;
            job.snap.firstFrameMs = NowMs() - job.tCapture;
            auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            com_ptr<ID3D11Texture2D> tex;
            access->GetInterface(IID_PPV_ARGS(tex.put()));
            auto cs = frame.ContentSize();
            D3D11_TEXTURE2D_DESC td;
            tex->GetDesc(&td);
            int cw = std::min<int>(cs.Width, td.Width), ch = std::min<int>(cs.Height, td.Height);
            job.snap.ok = Downscale(dev, tex.get(), cw, ch, kThumbWidth, job.snap);
            if (!job.snap.ok) job.snap.failReason = "downscale failed";
            job.snap.readyMs = NowMs() - job.tStart;
            SetEvent(job.done);
        });
        job.tCapture = NowMs();
        job.snap.setupMs = job.tCapture - job.tStart;
        job.session.StartCapture();
    } catch (winrt::hresult_error const& e) {
        job.snap.failReason = "WinRT error " + Hex((uint32_t)e.code()) + " " + Utf8(std::wstring(e.message()));
        SetEvent(job.done);
    }
}

void FinishCapture(CaptureJob& job, int timeoutMs) {
    if (WaitForSingleObject(job.done, timeoutMs) != WAIT_OBJECT_0) {
        job.handled = true;
        job.snap.failReason = "no frame within " + std::to_string(timeoutMs) + " ms";
    }
    double tc = NowMs();
    Close(job);
    job.snap.closeMs = NowMs() - tc;
    job.snap.totalMs = NowMs() - job.tStart;
}

// ---- snapshot display: per-pixel-alpha layered window (planned Pile surface) ----------
HWND ShowSnapshot(const Snapshot& s, int x, int y) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"PocSnapshot";
        RegisterClassW(&wc);
        reg = true;
    }
    HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"PocSnapshot",
                                L"snapshot", WS_POPUP, x, y, s.w, s.h, nullptr, nullptr, GetModuleHandleW(nullptr),
                                nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = s.w;
    bi.bmiHeader.biHeight = -s.h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC sdc = GetDC(nullptr);
    HDC mdc = CreateCompatibleDC(sdc);
    HBITMAP bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    memcpy(bits, s.bgra.data(), s.bgra.size() * 4);
    HGDIOBJ old = SelectObject(mdc, bmp);
    POINT pt{x, y}, src{0, 0};
    SIZE sz{s.w, s.h};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd, sdc, &pt, &sz, mdc, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(nullptr, sdc);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    return hwnd;
}

std::string Fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

std::string SnapRow(const std::string& label, const Snapshot& s, int harnessIndex, const std::string& extra = "") {
    std::string colorCheck = "-";
    if (s.ok && harnessIndex >= 0) {
        // Left quarter of the snapshot should be the harness color.
        uint64_t r = 0, g = 0, b = 0, n = 0;
        for (int y = s.h * 4 / 10; y < s.h * 9 / 10; ++y)
            for (int x = s.w / 10; x < s.w * 4 / 10; ++x) {
                uint32_t p = s.bgra[(size_t)y * s.w + x];
                r += (p >> 16) & 255, g += (p >> 8) & 255, b += p & 255, ++n;
            }
        if (n) {
            int d = ColorDistance(RGB(r / n, g / n, b / n), HarnessColor(harnessIndex));
            colorCheck = d <= 60 ? Fmt("일치(Δ%d)", d) : Fmt("불일치(Δ%d)", d);
        }
    }
    if (!s.ok)
        return "| " + label + " | 실패: " + s.failReason + " | - | - | - | " + Fmt("%.1f", s.closeMs) + " | " +
               Fmt("%.1f", s.totalMs) + " | - | - |" + extra + "\n";
    return "| " + label + " | 성공 " + Fmt("%dx%d → %dx%d", s.srcW, s.srcH, s.w, s.h) + " | " +
           Fmt("%.1f", s.setupMs) + " | " + Fmt("%.1f", s.firstFrameMs) + " | " + Fmt("%.1f", s.readyMs) + " | " +
           Fmt("%.1f", s.closeMs) + " | " + Fmt("%.1f", s.totalMs) + " | " + Fmt("%.0f", s.avgLuma) + " | " +
           colorCheck + " |" + extra + "\n";
}

struct Target {
    HWND hwnd;
    std::string label;
    int harnessIndex;
};

std::string RunPass(bool warp, std::vector<Target>& targets, bool roundTrip) {
    std::string md;
    size_t mem0 = PrivateBytes();
    double t0 = NowMs();
    Device dev;
    if (!CreateDevice(warp, dev)) return Fmt("- %s 디바이스 생성 실패\n", warp ? "WARP" : "하드웨어");
    double tDev = NowMs() - t0;
    md += Fmt("\n### %s 디바이스 (생성 %.1f ms, mip 자동생성 %s)\n\n", warp ? "WARP(소프트웨어)" : "하드웨어 GPU", tDev,
              dev.mipsSupported ? "지원" : "미지원→CPU 축소");

    // 1) sequential captures
    md += "| 대상 | 결과 | 준비 ms | 첫 프레임 ms | 픽셀 준비까지 ms | Close ms | 전체 ms | 평균 밝기 | 왼쪽 색 |\n"
          "|---|---|---|---|---|---|---|---|---|\n";
    std::vector<Snapshot> snaps;
    for (auto& t : targets) {
        auto job = std::make_unique<CaptureJob>();
        job->hwnd = t.hwnd;
        StartCapture(dev, *job);
        FinishCapture(*job, 1000);
        md += SnapRow(t.label, job->snap, t.harnessIndex);
        snaps.push_back(job->snap);
    }
    size_t memCapture = PrivateBytes();

    // 2) all at once (fold of a whole group)
    {
        std::vector<std::unique_ptr<CaptureJob>> jobs;
        double ts = NowMs();
        for (auto& t : targets) {
            if (IsIconic(t.hwnd) || !IsWindowVisible(t.hwnd)) continue;
            auto job = std::make_unique<CaptureJob>();
            job->hwnd = t.hwnd;
            StartCapture(dev, *job);
            jobs.push_back(std::move(job));
        }
        int ok = 0;
        double lastReady = 0;
        for (auto& j : jobs) {
            double left = 300 - (NowMs() - ts);
            FinishCapture(*j, (int)std::max(1.0, left));
            if (j->snap.ok) {
                ++ok;
                lastReady = std::max(lastReady, j->tStart + j->snap.readyMs - ts);
            }
        }
        md += Fmt("\n- 동시 캡처: 보이는 창 %zu개 중 %d개 성공. 성공분 픽셀 준비 완료까지 %.1f ms, "
                  "실패분 타임아웃·정리 포함 전체 %.1f ms (예산 300 ms)\n",
                  jobs.size(), ok, lastReady, NowMs() - ts);
    }

    // 3) round trip on the first visible harness window: hide -> show snapshot -> verify -> restore
    if (roundTrip) {
        for (size_t i = 0; i < targets.size(); ++i) {
            auto& t = targets[i];
            if (!snaps[i].ok || t.harnessIndex < 0 || IsIconic(t.hwnd)) continue;
            WINDOWPLACEMENT wp{sizeof(wp)};
            GetWindowPlacement(t.hwnd, &wp);
            double th = NowMs();
            ShowWindowAsync(t.hwnd, SW_HIDE);
            bool hidden = PumpUntil([&] { return !IsWindowVisible(t.hwnd); }, 1000);
            double hideMs = NowMs() - th;
            RECT work;
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            HWND sw = ShowSnapshot(snaps[i], work.left + 40, work.top + 40);
            PumpFor(300);
            RECT on{work.left + 40 + snaps[i].w / 10, work.top + 40 + snaps[i].h * 4 / 10,
                    work.left + 40 + snaps[i].w * 4 / 10, work.top + 40 + snaps[i].h * 9 / 10};
            Sample s = SampleScreen(on, RGB(255, 0, 255));
            int d = ColorDistance(s.avg, HarnessColor(t.harnessIndex));
            PumpFor(700);
            DestroyWindow(sw);
            double tr = NowMs();
            ShowWindowAsync(t.hwnd, SW_SHOWNA);
            if (wp.showCmd == SW_SHOWNORMAL) wp.showCmd = SW_SHOWNOACTIVATE;
            SetWindowPlacement(t.hwnd, &wp);
            bool restored = PumpUntil([&] { return IsWindowVisible(t.hwnd) != FALSE; }, 1000);
            double restoreMs = NowMs() - tr;
            md += Fmt("- 왕복(%s): 숨김 %s %.1f ms → 스냅샷 창 화면 표시 색 %s(Δ%d) → 복원 %s %.1f ms\n", t.label.c_str(),
                      hidden ? "성공" : "실패", hideMs, d <= 60 ? "일치" : "불일치", d, restored ? "성공" : "실패",
                      restoreMs);
            break;
        }
    }

    dev = Device{};  // release D3D + WinRT device
    PumpFor(200);
    size_t memAfter = PrivateBytes();
    md += Fmt("- private bytes: 시작 %.1f MB → 캡처 후 %.1f MB → 디바이스 해제 후 %.1f MB\n", mem0 / 1048576.0,
              memCapture / 1048576.0, memAfter / 1048576.0);
    return md;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    InitProcess();
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    std::wstring prefix = L"TW-";
    std::wstring outPath;
    std::vector<HWND> explicitHwnds;
    bool warpOnly = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--prefix" && i + 1 < argc) prefix = argv[++i];
        else if (a == L"--hwnd" && i + 1 < argc) explicitHwnds.push_back((HWND)(uintptr_t)wcstoull(argv[++i], nullptr, 0));
        else if (a == L"--out" && i + 1 < argc) outPath = argv[++i];
        else if (a == L"--warp-only") warpOnly = true;
    }

    std::string md = "## PoC-03 — WGC 스냅샷\n\n- OS: " + Utf8(OsVersionString()) + "\n";
    if (!wgc::GraphicsCaptureSession::IsSupported()) {
        md += "- GraphicsCaptureSession::IsSupported() == false → WGC 불가 (아이콘 카드 폴백만 가능)\n";
        Print("%s", md.c_str());
        if (!outPath.empty()) AppendFile(outPath, md);
        return 3;
    }

    // Yellow border: Windows 11 lets apps ask for borderless capture.
    try {
        auto status = wgc::GraphicsCaptureAccess::RequestAccessAsync(wgc::GraphicsCaptureAccessKind::Borderless).get();
        g_borderlessGranted =
            status == winrt::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus::Allowed;
        md += std::string("- 테두리 없는 캡처(Borderless) 요청: ") + (g_borderlessGranted ? "허용" : "거부") + "\n";
    } catch (winrt::hresult_error const& e) {
        md += "- 테두리 없는 캡처 API 없음/실패 (" + Hex((uint32_t)e.code()) + ") → 캡처 중 노란 테두리 표시됨\n";
    }

    std::vector<HWND> hwnds = explicitHwnds.empty() ? FindTopLevelByTitlePrefix(prefix) : explicitHwnds;
    std::vector<Target> targets;
    HWND minimizedProbe = nullptr;
    for (HWND h : hwnds) {
        if (!IsWindowVisible(h)) continue;
        WindowInfo wi = Describe(h);
        if (wi.title.find(L"OwnedDialog") != std::wstring::npos) continue;
        targets.push_back({h, Utf8(wi.title.empty() ? wi.className : wi.title), HarnessIndexFromTitle(wi.title)});
        if (!minimizedProbe && wi.title.rfind(L"TW-Normal-", 0) == 0) minimizedProbe = h;
    }
    if (targets.empty()) {
        Print("No target windows (start TestWindows.exe --all first).\n");
        return 2;
    }

    // Make one normal harness window minimized to measure that case explicitly.
    WINDOWPLACEMENT minWp{sizeof(minWp)};
    if (minimizedProbe && targets.size() > 1 && explicitHwnds.empty()) {
        // Use the last Normal window so the round trip still has a visible one.
        for (auto it = targets.rbegin(); it != targets.rend(); ++it)
            if (it->label.rfind("TW-Normal-", 0) == 0) {
                minimizedProbe = it->hwnd;
                it->label += " [최소화 상태]";
                break;
            }
        GetWindowPlacement(minimizedProbe, &minWp);
        ShowWindowAsync(minimizedProbe, SW_SHOWMINNOACTIVE);
        PumpUntil([&] { return IsIconic(minimizedProbe) != FALSE; }, 1000);
    } else {
        minimizedProbe = nullptr;
    }

    if (!warpOnly) md += RunPass(false, targets, true);
    md += RunPass(true, targets, warpOnly);

    if (minimizedProbe) {
        ShowWindowAsync(minimizedProbe, SW_SHOWNOACTIVATE);
        if (minWp.showCmd == SW_SHOWNORMAL) minWp.showCmd = SW_SHOWNOACTIVATE;
        SetWindowPlacement(minimizedProbe, &minWp);
    }
    md += Fmt("\n- 종료 시 working set %.1f MB, private %.1f MB\n", WorkingSetBytes() / 1048576.0,
              PrivateBytes() / 1048576.0);
    Print("%s\n", md.c_str());
    if (!outPath.empty()) AppendFile(outPath, md + "\n");
    return 0;
}
