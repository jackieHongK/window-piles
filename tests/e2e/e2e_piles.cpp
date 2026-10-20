// End-to-end check of the real app with real mouse input (SendInput moves the cursor!).
// Needs WindowPiles.exe and `TestWindows --normal 3` running. Touches only TestWindows windows.
//
//   1. drag TW-Normal-1 onto the top drop bar   → hidden, one pile appears
//   2. drag TW-Normal-2 onto that pile          → hidden, still one pile
//   3. click the pile                           → both back at their original rects
//   4. click the pile (chip) again              → both hidden
//   5. kill WindowPiles.exe (crash)             → watchdog brings both back
//
// Usage: e2e_piles [--shots <dir>]   (saves BMP screenshots of the bar and the pile)
#include "poc_common.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace poc;

namespace {

int g_fail = 0;

void Check(bool ok, const char* what) {
    Print("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

void MoveMouse(int x, int y) {
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = (LONG)(((double)(x - vx) * 65535.0) / (vw - 1));
    in.mi.dy = (LONG)(((double)(y - vy) * 65535.0) / (vh - 1));
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &in, sizeof(in));
}
void Button(bool down) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof(in));
}
void Glide(POINT a, POINT b, int steps, int ms) {
    for (int i = 1; i <= steps; ++i) {
        MoveMouse(a.x + (b.x - a.x) * i / steps, a.y + (b.y - a.y) * i / steps);
        PumpFor(ms / steps);
    }
}

POINT CaptionPoint(HWND h) {
    RECT r;
    GetWindowRect(h, &r);
    UINT dpi = GetDpiForWindow(h);
    int y = r.top + GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi) +
            GetSystemMetricsForDpi(SM_CYCAPTION, dpi) / 2;
    return {r.left + MulDiv(80, dpi, 96), y};
}

void BringUp(HWND h) {
    SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    PumpFor(150);
}

void Drag(HWND h, POINT to, std::function<void()> beforeRelease = nullptr) {
    BringUp(h);
    POINT p = CaptionPoint(h);
    MoveMouse(p.x, p.y);
    PumpFor(150);
    Button(true);
    PumpFor(100);
    Glide(p, to, 30, 700);
    PumpFor(300);
    if (beforeRelease) beforeRelease();
    Button(false);
    PumpFor(900);
}

void Click(POINT p) {
    MoveMouse(p.x, p.y);
    PumpFor(150);
    Button(true);
    PumpFor(60);
    Button(false);
    PumpFor(900);
}

std::vector<HWND> PileWindows() {
    std::vector<HWND> out;
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            if (WindowClass(h) == L"WindowPilesPile" && IsWindowVisible(h)) ((std::vector<HWND>*)lp)->push_back(h);
            return TRUE;
        },
        (LPARAM)&out);
    return out;
}

POINT Center(const RECT& r) { return {(r.left + r.right) / 2, (r.top + r.bottom) / 2}; }

bool SameRect(const RECT& a, const RECT& b) {
    return abs(a.left - b.left) <= 2 && abs(a.top - b.top) <= 2 && abs(a.right - b.right) <= 2 &&
           abs(a.bottom - b.bottom) <= 2;
}

void SaveShot(const std::wstring& path, const RECT& r) {
    int w = r.right - r.left, h = r.bottom - r.top;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
    GdiFlush();
    BITMAPFILEHEADER fh{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(BITMAPINFOHEADER);
    fh.bfSize = fh.bfOffBits + w * h * 4;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD n;
    WriteFile(f, &fh, sizeof(fh), &n, nullptr);
    WriteFile(f, &bi.bmiHeader, sizeof(BITMAPINFOHEADER), &n, nullptr);
    WriteFile(f, bits, w * h * 4, &n, nullptr);
    CloseHandle(f);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    InitProcess();
    std::wstring shots;
    for (int i = 1; i < argc; ++i)
        if (std::wstring(argv[i]) == L"--shots" && i + 1 < argc) shots = argv[++i];

    HWND mainWnd = FindWindowW(L"WindowPilesMain", nullptr);
    HWND w1 = FindTopLevelByTitle(L"TW-Normal-1"), w2 = FindTopLevelByTitle(L"TW-Normal-2");
    if (!mainWnd || !w1 || !w2) {
        Print("Start WindowPiles.exe and `TestWindows --normal 3` first.\n");
        return 2;
    }
    RECT r1, r2;
    GetWindowRect(w1, &r1);
    GetWindowRect(w2, &r2);
    POINT cursor0;
    GetCursorPos(&cursor0);

    // 1) onto the drop bar at the top center of the work area
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    UINT dpi = GetDpiForSystem();
    POINT bar{(work.left + work.right) / 2, work.top + MulDiv(56 + 38, dpi, 96)};
    Drag(w1, bar, [&] {
        RECT barRect{bar.x - MulDiv(180, dpi, 96), bar.y - MulDiv(38, dpi, 96), bar.x + MulDiv(180, dpi, 96),
                     bar.y + MulDiv(38, dpi, 96)};
        Sample s = SampleScreen(barRect, RGB(30, 115, 240), 40);
        Check(s.bgFraction > 0.5, "끌기 중 화면 상단에 놓는 곳 바가 (파란 강조로) 보임");
        if (!shots.empty()) SaveShot(shots + L"\\bar.bmp", {barRect.left - 40, barRect.top - 30, barRect.right + 40, barRect.bottom + 30});
    });
    Check(!IsWindowVisible(w1), "1) 바에 놓은 창이 숨겨짐");
    auto piles = PileWindows();
    Check(piles.size() == 1, "1) 더미 1개가 생김");
    if (piles.empty()) return 1;
    RECT pr;
    GetWindowRect(piles[0], &pr);

    // 2) onto the pile
    Drag(w2, Center(pr));
    Check(!IsWindowVisible(w2), "2) 더미에 놓은 두 번째 창도 숨겨짐");
    Check(PileWindows().size() == 1, "2) 더미는 여전히 1개");
    GetWindowRect(piles[0], &pr);
    if (!shots.empty()) SaveShot(shots + L"\\pile_folded.bmp", {pr.left - 10, pr.top - 10, pr.right + 10, pr.bottom + 10});

    // 3) click → expand
    Click(Center(pr));
    RECT n1, n2;
    GetWindowRect(w1, &n1);
    GetWindowRect(w2, &n2);
    Check(IsWindowVisible(w1) && IsWindowVisible(w2), "3) 더미 클릭 → 두 창 모두 다시 보임");
    Check(SameRect(n1, r1) && SameRect(n2, r2), "3) 두 창 모두 끌기 전 원래 위치·크기로 돌아옴");
    GetWindowRect(piles[0], &pr);
    if (!shots.empty()) SaveShot(shots + L"\\pile_open.bmp", {pr.left - 10, pr.top - 10, pr.right + 10, pr.bottom + 10});

    // 4) click the chip → fold
    PumpFor(GetDoubleClickTime() + 100);
    Click(Center(pr));
    Check(!IsWindowVisible(w1) && !IsWindowVisible(w2), "4) 다시 클릭 → 두 창 모두 접힘");

    // 5) crash the app → watchdog restores
    DWORD pid = 0;
    GetWindowThreadProcessId(mainWnd, &pid);
    HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    double t0 = NowMs();
    TerminateProcess(p, 0xDEAD);
    CloseHandle(p);
    bool back = PumpUntil([&] { return IsWindowVisible(w1) && IsWindowVisible(w2); }, 4000);
    char buf[128];
    snprintf(buf, sizeof(buf), "5) 앱 강제 종료 → 감시자가 숨긴 창 2개 복구 (%.0f ms)", NowMs() - t0);
    Check(back, buf);
    GetWindowRect(w1, &n1);
    Check(SameRect(n1, r1), "5) 복구된 창이 원래 위치");
    Check(PileWindows().empty(), "5) 앱이 죽으면 더미 표시도 사라짐");

    MoveMouse(cursor0.x, cursor0.y);
    Print("\n%s (%d failed)\n", g_fail ? "E2E FAILED" : "E2E PASSED", g_fail);
    return g_fail ? 1 : 0;
}
