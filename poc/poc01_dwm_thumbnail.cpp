// PoC-01 / PoC-02 — DWM thumbnails of other processes' windows inside our own top-level window.
//
// PoC-01: register thumbnails of several sources into one destination window, overlapped,
//         and check that they render and which one ends up on top.
// PoC-02: put the sources into different states (visible / occluded / minimized / SW_HIDE /
//         off-screen) and measure, by sampling the composed screen, whether each thumbnail
//         is still visible and still live. Also checks layered destination windows and
//         repeated register/unregister for handle leaks.
//
// Sources default to TestWindows "TW-Normal-*" windows, whose left half has a known color and
// whose right half blinks every 500 ms. Real apps: --hwnd 0x1234 (repeatable); for those only
// "visible" can be judged (no known color), "live" depends on the content.
//
// Usage: poc_dwm [--prefix TW-Normal-] [--hwnd 0x..]... [--hold-hidden-sec 30] [--out results.md]
#include "poc_common.h"

#include <dwmapi.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace poc;

namespace {

constexpr COLORREF kBg = RGB(255, 0, 255);  // destination background (magenta)
constexpr int kBoxW = 240, kBoxH = 150, kGap = 20;

struct Source {
    HWND hwnd;
    std::string label;
    int harnessIndex;  // -1 for real apps
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    HTHUMBNAIL thumb = nullptr;
    RECT dest{};  // client coords of the destination window
};

LRESULT CALLBACK DestProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h, m, w, l);
}

HWND CreateDest(int x, int y, int w, int h, DWORD extraEx) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DestProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hbrBackground = CreateSolidBrush(kBg);
        wc.lpszClassName = L"PocDwmDest";
        RegisterClassW(&wc);
        registered = true;
    }
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | extraEx, L"PocDwmDest",
                                L"PoC DWM destination", WS_POPUP, x, y, w, h, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    return hwnd;
}

// Letterbox the source aspect ratio into a box.
RECT FitInBox(SIZE src, RECT box) {
    int bw = box.right - box.left, bh = box.bottom - box.top;
    if (src.cx <= 0 || src.cy <= 0) return box;
    double s = std::min((double)bw / src.cx, (double)bh / src.cy);
    int w = (int)(src.cx * s), h = (int)(src.cy * s);
    int x = box.left + (bw - w) / 2, y = box.top + (bh - h) / 2;
    return {x, y, x + w, y + h};
}

HRESULT SetThumb(HTHUMBNAIL t, RECT dest, BOOL visible = TRUE, BYTE opacity = 255) {
    DWM_THUMBNAIL_PROPERTIES p{};
    p.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
    p.rcDestination = dest;
    p.fVisible = visible;
    p.opacity = opacity;
    p.fSourceClientAreaOnly = TRUE;
    return DwmUpdateThumbnailProperties(t, &p);
}

RECT ToScreen(HWND dest, RECT r) {
    POINT a{r.left, r.top}, b{r.right, r.bottom};
    ClientToScreen(dest, &a);
    ClientToScreen(dest, &b);
    return {a.x, a.y, b.x, b.y};
}

RECT SubRect(RECT r, double x0, double y0, double x1, double y1) {
    int w = r.right - r.left, h = r.bottom - r.top;
    return {r.left + (int)(w * x0), r.top + (int)(h * y0), r.left + (int)(w * x1), r.top + (int)(h * y1)};
}

struct Measure {
    double bgFraction = 1;
    int colorDist = 999;
    double lumMin = 0, lumMax = 0;
    SIZE srcSize{};
    bool visible() const { return bgFraction < 0.2; }
    bool live() const { return lumMax - lumMin > 80; }
};

Measure MeasureThumb(HWND dest, const Source& s) {
    Measure m;
    DwmQueryThumbnailSourceSize(s.thumb, &m.srcSize);
    RECT scr = ToScreen(dest, s.dest);
    RECT left = SubRect(scr, 0.10, 0.35, 0.40, 0.90);
    RECT right = SubRect(scr, 0.60, 0.35, 0.90, 0.90);
    m.lumMin = 1e9;
    m.lumMax = -1;
    for (int i = 0; i < 3; ++i) {
        if (i) PumpFor(350);
        Sample l = SampleScreen(left, kBg);
        Sample r = SampleScreen(right, kBg);
        if (i == 0) {
            m.bgFraction = l.bgFraction;
            m.colorDist = s.harnessIndex >= 0 ? ColorDistance(l.avg, HarnessColor(s.harnessIndex)) : -1;
        }
        m.lumMin = std::min(m.lumMin, r.luminance);
        m.lumMax = std::max(m.lumMax, r.luminance);
    }
    return m;
}

std::string Verdict(const Measure& m, int harnessIndex) {
    std::string v = m.visible() ? "보임" : "안 보임";
    if (m.visible() && harnessIndex >= 0) v += m.colorDist <= 60 ? "(색 일치)" : "(색 불일치)";
    v += m.live() ? " / 실시간" : " / 정지";
    char buf[96];
    snprintf(buf, sizeof(buf), " [bg=%.2f, Δlum=%.0f, src=%ldx%ld]", m.bgFraction, m.lumMax - m.lumMin,
             m.srcSize.cx, m.srcSize.cy);
    return v + buf;
}

void WaitAsync(int ms = 800) { PumpFor(ms); }

}  // namespace

int wmain(int argc, wchar_t** argv) {
    InitProcess();
    std::wstring prefix = L"TW-Normal-";
    std::wstring outPath;
    std::vector<HWND> explicitHwnds;
    int holdHiddenSec = 30;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--hold-hidden-sec" && i + 1 < argc) holdHiddenSec = _wtoi(argv[++i]);
        if (a == L"--prefix" && i + 1 < argc) prefix = argv[++i];
        else if (a == L"--hwnd" && i + 1 < argc) explicitHwnds.push_back((HWND)(uintptr_t)wcstoull(argv[++i], nullptr, 0));
        else if (a == L"--out" && i + 1 < argc) outPath = argv[++i];
    }

    std::vector<HWND> hwnds = explicitHwnds.empty() ? FindTopLevelByTitlePrefix(prefix) : explicitHwnds;
    if (hwnds.size() > 3) hwnds.resize(3);
    if (hwnds.empty()) {
        Print("No source windows found (start TestWindows.exe first, or pass --hwnd).\n");
        return 2;
    }

    std::vector<Source> sources;
    for (HWND h : hwnds) {
        WindowInfo wi = Describe(h);
        Source s{h, Utf8(wi.title.empty() ? wi.className : wi.title) + " (" + Utf8(wi.exeName) + ")",
                 HarnessIndexFromTitle(wi.title)};
        GetWindowPlacement(h, &s.placement);
        sources.push_back(s);
    }

    std::string md;
    md += "## PoC-01/02 — DWM 썸네일\n\n";
    md += "- OS: " + Utf8(OsVersionString()) + "\n";
    md += "- 소스: ";
    for (auto& s : sources) md += s.label + "; ";
    md += "\n\n";

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int destW = kGap + 3 * (kBoxW + kGap), destH = 2 * (kBoxH + kGap) + kGap + 40;
    HWND dest = CreateDest(work.left + 20, work.top + 20, destW, destH, 0);
    ShowWindow(dest, SW_SHOWNOACTIVATE);
    UpdateWindow(dest);

    // ---- PoC-01: register, separate layout -------------------------------------------
    double t0 = NowMs();
    for (size_t i = 0; i < sources.size(); ++i) {
        auto& s = sources[i];
        HRESULT hr = DwmRegisterThumbnail(dest, s.hwnd, &s.thumb);
        if (FAILED(hr)) {
            md += "- DwmRegisterThumbnail 실패: " + s.label + " hr=" + Hex((uint32_t)hr) + "\n";
            s.thumb = nullptr;
            continue;
        }
        SIZE sz{};
        DwmQueryThumbnailSourceSize(s.thumb, &sz);
        RECT box{kGap + (int)i * (kBoxW + kGap), kGap, kGap + (int)i * (kBoxW + kGap) + kBoxW, kGap + kBoxH};
        s.dest = FitInBox(sz, box);
        SetThumb(s.thumb, s.dest);
    }
    double regMs = NowMs() - t0;
    char buf[256];
    snprintf(buf, sizeof(buf), "- 썸네일 %zu개 등록+속성 설정: %.2f ms\n\n", sources.size(), regMs);
    md += buf;

    // ---- PoC-02: source state matrix --------------------------------------------------
    struct StateStep {
        const char* name;
        void (*apply)(std::vector<Source>&, HWND& cover);
    };
    HWND cover = nullptr;
    const StateStep steps[] = {
        {"1. 원본 표시", [](std::vector<Source>&, HWND&) {}},
        {"2. 다른 창에 가림",
         [](std::vector<Source>& ss, HWND& cov) {
             RECT u{};
             for (auto& s : ss) {
                 RECT r;
                 GetWindowRect(s.hwnd, &r);
                 UnionRect(&u, &u, &r);
             }
             WNDCLASSW wc{};
             wc.lpfnWndProc = DefWindowProcW;
             wc.hInstance = GetModuleHandleW(nullptr);
             wc.hbrBackground = (HBRUSH)GetStockObject(GRAY_BRUSH);
             wc.lpszClassName = L"PocCover";
             RegisterClassW(&wc);
             cov = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"PocCover", L"cover", WS_POPUP, u.left - 10,
                                   u.top - 10, u.right - u.left + 20, u.bottom - u.top + 20, nullptr, nullptr,
                                   GetModuleHandleW(nullptr), nullptr);
             SetWindowPos(cov, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
         }},
        {"3. 최소화",
         [](std::vector<Source>& ss, HWND& cov) {
             if (cov) DestroyWindow(cov), cov = nullptr;
             for (auto& s : ss) ShowWindowAsync(s.hwnd, SW_SHOWMINNOACTIVE);
         }},
        {"4. SW_HIDE (숨김)",
         [](std::vector<Source>& ss, HWND&) {
             for (auto& s : ss) {
                 ShowWindowAsync(s.hwnd, SW_SHOWNOACTIVATE);
             }
             PumpFor(600);
             for (auto& s : ss) ShowWindowAsync(s.hwnd, SW_HIDE);
         }},
        {"5. 화면 밖으로 이동",
         [](std::vector<Source>& ss, HWND&) {
             int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
             for (auto& s : ss) {
                 ShowWindowAsync(s.hwnd, SW_SHOWNOACTIVATE);
             }
             PumpFor(400);
             for (auto& s : ss)
                 SetWindowPos(s.hwnd, nullptr, vx - 5000, vy - 5000, 0, 0,
                              SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
         }},
    };

    md += "| 원본 상태 |";
    for (auto& s : sources) md += " " + s.label + " |";
    md += "\n|---|";
    for (size_t i = 0; i < sources.size(); ++i) md += "---|";
    md += "\n";

    for (const auto& step : steps) {
        step.apply(sources, cover);
        WaitAsync();
        md += std::string("| ") + step.name + " |";
        for (auto& s : sources) {
            if (!s.thumb) {
                md += " (등록 실패) |";
                continue;
            }
            Measure m = MeasureThumb(dest, s);
            // Record the source's actual state so a "visible thumbnail" can't be a hide that silently failed.
            std::string actual = IsWindowVisible(s.hwnd) ? (IsIconic(s.hwnd) ? " {원본:최소화}" : " {원본:보임}")
                                                          : " {원본:숨김}";
            md += " " + Verdict(m, s.harnessIndex) + actual + " |";
            Print("[%s] %s: %s%s\n", step.name, s.label.c_str(), Verdict(m, s.harnessIndex).c_str(), actual.c_str());
        }
        md += "\n";
    }

    // Long hide: does DWM keep the last frame of a hidden source, or release it after a while?
    if (holdHiddenSec > 0) {
        for (auto& s : sources) ShowWindowAsync(s.hwnd, SW_SHOWNOACTIVATE);
        PumpFor(600);
        for (auto& s : sources) ShowWindowAsync(s.hwnd, SW_HIDE);
        md += "\n### 숨긴 상태 장시간 유지 (" + std::to_string(holdHiddenSec) + "초)\n\n| 경과 | ";
        for (auto& s : sources) md += s.label + " | ";
        md += "\n|---|";
        for (size_t i = 0; i < sources.size(); ++i) md += "---|";
        md += "\n";
        for (int t = 0; t <= holdHiddenSec; t += std::max(1, holdHiddenSec / 3)) {
            if (t) PumpFor(std::max(1, holdHiddenSec / 3) * 1000 - 700);
            md += "| " + std::to_string(t) + "s |";
            for (auto& s : sources) {
                Measure m = MeasureThumb(dest, s);
                md += " " + Verdict(m, s.harnessIndex) + (IsWindowVisible(s.hwnd) ? " {원본:보임}" : " {원본:숨김}") + " |";
            }
            md += "\n";
            Print("hold %ds measured\n", t);
        }
    }

    // Restore every source to where it was.
    for (auto& s : sources) {
        ShowWindowAsync(s.hwnd, SW_SHOWNOACTIVATE);
    }
    PumpFor(400);
    for (auto& s : sources) {
        WINDOWPLACEMENT wp = s.placement;
        if (wp.showCmd == SW_SHOWNORMAL) wp.showCmd = SW_SHOWNOACTIVATE;
        SetWindowPlacement(s.hwnd, &wp);
    }
    WaitAsync();

    // ---- destination variants (our Pile surface will be a layered window) ------------
    md += "\n### 대상(destination) 창 종류별 표시 여부 (원본 표시 상태)\n\n| 대상 창 | 결과 |\n|---|---|\n";
    {
        Measure base = MeasureThumb(dest, sources[0]);
        md += "| 일반 WS_POPUP + TOPMOST + NOACTIVATE | " + Verdict(base, sources[0].harnessIndex) + " |\n";

        // LWA layered window (constant alpha).
        LONG_PTR ex = GetWindowLongPtrW(dest, GWL_EXSTYLE);
        SetWindowLongPtrW(dest, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        SetLayeredWindowAttributes(dest, 0, 255, LWA_ALPHA);
        RedrawWindow(dest, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        WaitAsync(600);
        Measure lwa = MeasureThumb(dest, sources[0]);
        md += "| WS_EX_LAYERED (SetLayeredWindowAttributes α=255) | " + Verdict(lwa, sources[0].harnessIndex) + " |\n";
        SetWindowLongPtrW(dest, GWL_EXSTYLE, ex);
        RedrawWindow(dest, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);

        // Per-pixel-alpha layered window (UpdateLayeredWindow) — the planned Pile surface.
        int w = kBoxW + 2 * kGap, h = kBoxH + 2 * kGap;
        HWND ulw = CreateDest(work.left + 20, work.top + 20 + destH + 20, w, h, WS_EX_LAYERED);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HDC sdc = GetDC(nullptr);
        HDC mdc = CreateCompatibleDC(sdc);
        HBITMAP bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        for (int i = 0; i < w * h; ++i) ((uint32_t*)bits)[i] = 0xFFFF00FF;  // opaque magenta (premultiplied)
        HGDIOBJ old = SelectObject(mdc, bmp);
        POINT ptDst{work.left + 20, work.top + 20 + destH + 20}, ptSrc{0, 0};
        SIZE sz{w, h};
        BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(ulw, sdc, &ptDst, &sz, mdc, &ptSrc, 0, &bf, ULW_ALPHA);
        ShowWindow(ulw, SW_SHOWNOACTIVATE);
        Source s2 = sources[0];
        s2.thumb = nullptr;
        HRESULT hr = DwmRegisterThumbnail(ulw, s2.hwnd, &s2.thumb);
        if (SUCCEEDED(hr)) {
            SIZE ssz{};
            DwmQueryThumbnailSourceSize(s2.thumb, &ssz);
            s2.dest = FitInBox(ssz, {kGap, kGap, kGap + kBoxW, kGap + kBoxH});
            SetThumb(s2.thumb, s2.dest);
            WaitAsync(600);
            Measure m = MeasureThumb(ulw, s2);
            md += "| WS_EX_LAYERED (UpdateLayeredWindow, per-pixel α) | " + Verdict(m, s2.harnessIndex) + " |\n";
            DwmUnregisterThumbnail(s2.thumb);
        } else {
            md += "| WS_EX_LAYERED (UpdateLayeredWindow) | 등록 실패 hr=" + Hex((uint32_t)hr) + " |\n";
        }
        SelectObject(mdc, old);
        DeleteObject(bmp);
        DeleteDC(mdc);
        ReleaseDC(nullptr, sdc);
        DestroyWindow(ulw);
    }

    // ---- PoC-01: overlapped thumbnails, which one is on top? --------------------------
    if (sources.size() >= 2) {
        int baseY = kGap + kBoxH + kGap;
        for (size_t i = 0; i < sources.size(); ++i) {
            int off = 14 * (int)i;
            RECT r{kGap + off, baseY + off, kGap + off + kBoxW, baseY + off + kBoxH};
            SetThumb(sources[i].thumb, r);
        }
        WaitAsync(600);
        int last = (int)sources.size() - 1;
        RECT probe{kGap + 14 * last + 10, baseY + 14 * last + 50, kGap + 14 * last + 40, baseY + 14 * last + 80};
        Sample p = SampleScreen(ToScreen(dest, probe), kBg);
        int best = -1, bestDist = 999;
        for (size_t i = 0; i < sources.size(); ++i) {
            if (sources[i].harnessIndex < 0) continue;
            int d = ColorDistance(p.avg, HarnessColor(sources[i].harnessIndex));
            if (d < bestDist) bestDist = d, best = (int)i;
        }
        md += "\n### 겹친 썸네일 (12~16px 어긋나게 3장)\n\n";
        if (best >= 0)
            md += "- 겹친 영역 맨 위에 보이는 것: 등록 순서 #" + std::to_string(best + 1) + " (" + sources[best].label +
                  ", 색 거리 " + std::to_string(bestDist) + ")\n";
        else
            md += "- 판정 불가(하네스 창이 아님)\n";
    }

    // ---- leak check: 500 register/unregister cycles -----------------------------------
    {
        DWORD gdi0 = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        DWORD usr0 = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        DWORD h0 = 0, h1 = 0;
        GetProcessHandleCount(GetCurrentProcess(), &h0);
        double ts = NowMs();
        int fails = 0;
        for (int i = 0; i < 500; ++i) {
            HTHUMBNAIL t = nullptr;
            if (FAILED(DwmRegisterThumbnail(dest, sources[0].hwnd, &t))) {
                ++fails;
                continue;
            }
            SetThumb(t, {0, 0, 10, 10}, FALSE);
            DwmUnregisterThumbnail(t);
        }
        double te = NowMs() - ts;
        GetProcessHandleCount(GetCurrentProcess(), &h1);
        DWORD gdi1 = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        DWORD usr1 = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        snprintf(buf, sizeof(buf),
                 "\n### 등록/해제 500회 반복\n\n- 소요: %.1f ms (회당 %.3f ms), 실패 %d\n- 핸들 %lu→%lu, GDI %lu→%lu, USER %lu→%lu\n",
                 te, te / 500, fails, h0, h1, gdi0, gdi1, usr0, usr1);
        md += buf;
    }

    for (auto& s : sources)
        if (s.thumb) DwmUnregisterThumbnail(s.thumb);
    DestroyWindow(dest);

    snprintf(buf, sizeof(buf), "\n- 프로세스 working set: %.1f MB\n", WorkingSetBytes() / 1048576.0);
    md += buf;
    Print("\n%s\n", md.c_str());
    if (!outPath.empty()) AppendFile(outPath, md + "\n");
    return 0;
}
