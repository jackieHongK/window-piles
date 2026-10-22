// End-to-end check of WindowPiles.exe with real mouse/keyboard input (SendInput moves the cursor!).
// Needs `TestWindows --normal 3` running and NO WindowPiles instance (this test starts/stops it).
// Touches only TestWindows windows. Saves screenshots (BMP) with --shots <dir>.
//
//   1. drag TW-Normal-1 onto the top drop bar      → a bar on every monitor; window hidden; 1 card
//   2. drag TW-Normal-2 onto the card              → hidden; journal lists both
//   3. click the card                              → windows back at their rects; card hidden;
//                                                     screen frame + top tab shown
//   4. click the top tab                           → windows gather back; card at the same place
//   5. right-click card → "이름·색 바꾸기…" → type name → Enter   → name saved in piles.json
//   6. launch the exe again                        → the running instance shows the guide
//   7. quit (WM_ENDSESSION) and relaunch           → piles restored and windows put away again
//   8. close TW-Normal-1 while hidden              → listed as "다시 열어야 할 창"
//   9. drag a new TW-Normal-1 onto the card        → entry removed from the list
//  10. kill WindowPiles (crash)                    → watchdog brings the hidden windows back
#include "poc_common.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace poc;

namespace {

int g_fail = 0;
std::wstring g_shots, g_bin;
UINT g_dpi = 96;
int S(int v) { return MulDiv(v, (int)g_dpi, 96); }

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
void Button(bool down, bool right = false) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = right ? (down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP)
                          : (down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
    SendInput(1, &in, sizeof(in));
}
void Key(WORD vk) {
    INPUT in[2]{};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}
void TypeText(const std::wstring& s) {
    for (wchar_t ch : s) {
        INPUT in[2]{};
        in[0].type = in[1].type = INPUT_KEYBOARD;
        in[0].ki.wScan = in[1].ki.wScan = ch;
        in[0].ki.dwFlags = KEYEVENTF_UNICODE;
        in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
        PumpFor(20);
    }
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

void Drag(HWND h, POINT to, const std::function<void()>& beforeRelease = nullptr) {
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
    PumpFor(1100);
}

void Click(POINT p, bool right = false) {
    MoveMouse(p.x, p.y);
    PumpFor(200);
    Button(true, right);
    PumpFor(60);
    Button(false, right);
    PumpFor(900);
}

std::vector<HWND> VisibleOfClass(const wchar_t* cls) {
    struct Ctx {
        const wchar_t* cls;
        std::vector<HWND> out;
    } ctx{cls, {}};
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            auto* c = (Ctx*)lp;
            if (IsWindowVisible(h) && WindowClass(h) == c->cls) c->out.push_back(h);
            return TRUE;
        },
        (LPARAM)&ctx);
    return ctx.out;
}

RECT Rect(HWND h) {
    RECT r{};
    GetWindowRect(h, &r);
    return r;
}
POINT Center(const RECT& r) { return {(r.left + r.right) / 2, (r.top + r.bottom) / 2}; }
bool SameRect(const RECT& a, const RECT& b, int tol = 2) {
    return abs(a.left - b.left) <= tol && abs(a.top - b.top) <= tol && abs(a.right - b.right) <= tol &&
           abs(a.bottom - b.bottom) <= tol;
}

void SaveShot(const wchar_t* name, RECT r) {
    if (g_shots.empty()) return;
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;
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
    HANDLE f = CreateFileW((g_shots + L"\\" + name + L".bmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0,
                           nullptr);
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

RECT Inflate(RECT r, int d) { return {r.left - d, r.top - d, r.right + d, r.bottom + d}; }

std::string ReadFileUtf8(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    std::string s(1 << 20, '\0');
    DWORD n = 0;
    ReadFile(f, s.data(), (DWORD)s.size(), &n, nullptr);
    CloseHandle(f);
    s.resize(n);
    return s;
}
// The app under test uses this folder instead of %LOCALAPPDATA%\WindowPiles (set in wmain).
std::wstring DataDir() {
    wchar_t buf[MAX_PATH] = {};
    GetEnvironmentVariableW(L"WINDOWPILES_DATA_DIR", buf, MAX_PATH);
    return buf;
}
bool FileHas(const wchar_t* file, const std::string& needle) {
    return ReadFileUtf8(DataDir() + L"\\" + file).find(needle) != std::string::npos;
}

HANDLE Launch(const std::wstring& exe, const std::wstring& args) {
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return nullptr;
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

HWND WaitMain(int ms) {
    HWND h = nullptr;
    PumpUntil([&] { return (h = FindWindowW(L"WindowPilesMain", nullptr)) != nullptr; }, ms);
    return h;
}

int MonitorCount() {
    int n = 0;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR, HDC, LPRECT, LPARAM lp) -> BOOL { ++*(int*)lp; return TRUE; },
                        (LPARAM)&n);
    return n;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    InitProcess();
    SetEnvironmentVariableW(L"WINDOWPILES_NO_RELAUNCH", L"1");  // keep the app under test as our child
    for (int i = 1; i < argc; ++i)
        if (std::wstring(argv[i]) == L"--shots" && i + 1 < argc) g_shots = argv[++i];
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    g_bin = self;
    g_bin = g_bin.substr(0, g_bin.find_last_of(L'\\'));
    // Isolated data folder: never touch the user's real piles.
    SetEnvironmentVariableW(L"WINDOWPILES_DATA_DIR", (g_bin + L"\\e2e-data").c_str());
    CreateDirectoryW((g_bin + L"\\e2e-data").c_str(), nullptr);
    // Not a first run: the first-run guide would cover the test windows (step 6 checks the guide).
    CloseHandle(CreateFileW((g_bin + L"\\e2e-data\\firstrun.done").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr));
    std::wstring appExe = g_bin + L"\\WindowPiles.exe";
    g_dpi = GetDpiForSystem();

    HWND w1 = FindTopLevelByTitle(L"TW-Normal-1"), w2 = FindTopLevelByTitle(L"TW-Normal-2");
    if (!w1 || !w2 || FindWindowW(L"WindowPilesMain", nullptr)) {
        Print("Need `TestWindows --normal 3` running and WindowPiles NOT running.\n");
        return 2;
    }
    DeleteFileW((DataDir() + L"\\piles.json").c_str());
    HANDLE app = Launch(appExe, L"");
    HWND mainWnd = WaitMain(5000);
    Check(mainWnd != nullptr, "0) 앱 실행");
    PumpFor(800);
    // First run shows the guide; close it so it doesn't cover the test windows.
    for (HWND g : VisibleOfClass(L"WindowPilesGuide")) PostMessageW(g, WM_KEYDOWN, VK_ESCAPE, 0);
    PumpFor(300);

    RECT r1 = Rect(w1), r2 = Rect(w2);
    POINT cursor0;
    GetCursorPos(&cursor0);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    // 1) onto the drop bar at the top center of the primary work area
    POINT bar{(work.left + work.right) / 2, work.top + S(48 + 34)};
    Drag(w1, bar, [&] {
        auto bars = VisibleOfClass(L"WindowPilesDropBar");
        char buf[96];
        snprintf(buf, sizeof(buf), "1) 끄는 중 모든 모니터에 새 더미 바 표시 (%zu/%d)", bars.size(), MonitorCount());
        Check((int)bars.size() == MonitorCount(), buf);
        RECT barCard{bar.x - S(150), bar.y - S(28), bar.x + S(150), bar.y + S(28)};
        Sample s = SampleScreen(barCard, RGB(24, 24, 27), 30);
        Check(s.bgFraction > 0.5, "1) 바 위에 올리면 진한 강조색으로 바뀜");
        SaveShot(L"1_bar_hot", Inflate(barCard, S(40)));
    });
    Check(!IsWindowVisible(w1), "1) 바에 놓은 창이 숨겨짐");
    auto cards = VisibleOfClass(L"WindowPilesPile");
    Check(cards.size() == 1, "1) 더미 카드 1개 생성");
    if (cards.size() != 1) return 1;
    HWND card = cards[0];

    // 2) onto the card
    Drag(w2, Center(Rect(card)));
    Check(!IsWindowVisible(w2), "2) 카드에 놓은 두 번째 창도 숨겨짐");
    DWORD appPid = 0;
    GetWindowThreadProcessId(mainWnd, &appPid);
    Check(FileHas(L"recovery.json", "\"managerPid\":" + std::to_string(appPid)) &&
              FileHas(L"recovery.json", std::to_string((uint64_t)(uintptr_t)w2)),
          "2) 복구 저널에 숨긴 창이 기록됨");
    RECT cardRect = Rect(card);
    SaveShot(L"2_card", Inflate(cardRect, S(4)));

    // 3) click → active: windows back, card hidden, frame + tab
    Click(Center(cardRect));
    Check(IsWindowVisible(w1) && IsWindowVisible(w2), "3) 카드 클릭 → 두 창 다시 보임");
    Check(SameRect(Rect(w1), r1) && SameRect(Rect(w2), r2), "3) 끌기 전 원래 위치·크기로 복귀");
    Check(!IsWindowVisible(card), "3) 작업 중에는 카드가 숨겨짐");
    auto tabs = VisibleOfClass(L"WindowPilesFrameTab");
    auto edges = VisibleOfClass(L"WindowPilesFrameEdge");
    Check(!tabs.empty() && edges.size() >= 4, "3) 화면 테두리 + 최상단 이름표 표시");
    if (!tabs.empty()) {
        RECT tr = Rect(tabs[0]);
        SaveShot(L"3_tab", {tr.left - S(60), tr.top, tr.right + S(60), tr.bottom + S(30)});
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(tabs[0], MONITOR_DEFAULTTONEAREST), &mi);
        // The frame must really be on screen, not just exist: sample the top and left strips.
        RECT top{mi.rcMonitor.left + S(200), mi.rcMonitor.top, mi.rcMonitor.left + S(260), mi.rcMonitor.top + S(2)};
        RECT left{mi.rcMonitor.left, mi.rcMonitor.top + S(300), mi.rcMonitor.left + S(2), mi.rcMonitor.top + S(360)};
        COLORREF pileColor = RGB(51, 126, 169);  // first new pile = 파랑
        Sample st = SampleScreen(top, pileColor, 40), sl = SampleScreen(left, pileColor, 40);
        char fb[128];
        snprintf(fb, sizeof(fb), "3) 화면 테두리가 실제로 보임 (위 %.0f%%, 왼쪽 %.0f%%)", st.bgFraction * 100,
                 sl.bgFraction * 100);
        Check(st.bgFraction > 0.8 && sl.bgFraction > 0.8, fb);
        for (HWND e : edges) {
            RECT er = Rect(e);
            Print("    edge %s\n", RectStr(er).c_str());
        }
        SaveShot(L"3_frame_corner", {mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.left + S(200), mi.rcMonitor.top + S(120)});

        // 3b) drag a member of the open pile: "빼기" zone next to "새 더미", tab grows into a target
        POINT newBar{(work.left + work.right) / 2, work.top + S(48 + 34)};
        POINT removeBar{newBar.x + S(160) + S(12) + S(130), newBar.y};
        Drag(w2, removeBar, [&] {
            auto bars = VisibleOfClass(L"WindowPilesDropBar");
            char b2[96];
            snprintf(b2, sizeof(b2), "3b) 더미 소속 창을 끌면 '새 더미'+'빼기' 영역 표시 (%zu/%d)", bars.size(),
                     2 * MonitorCount());
            Check((int)bars.size() == 2 * MonitorCount(), b2);
            auto t = VisibleOfClass(L"WindowPilesFrameTab");
            RECT big = t.empty() ? RECT{} : Rect(t[0]);
            Check(!t.empty() && (big.bottom - big.top) >= (tr.bottom - tr.top) + S(6) && (big.right - big.left) > (tr.right - tr.left),
                  "3b) 끄는 동안 맨 위 이름표가 커져서 넣는 자리로 바뀜");
            SaveShot(L"3b_drag_targets", {work.left + (work.right - work.left) / 2 - S(420), work.top,
                                          work.left + (work.right - work.left) / 2 + S(500), work.top + S(130)});
        });
        PumpFor(800);
        Check(IsWindowVisible(w2) && SameRect(Rect(w2), r2), "3b) '빼기'에 놓으면 창은 원래 자리에 그대로 남음");
        Check(!FileHas(L"piles.json", "\"hwnd\":\"" + std::to_string((uint64_t)(uintptr_t)w2) + "\""),
              "3b) 더미에서 빠짐 (piles.json)");
        // ...and back in by dropping it on the enlarged tab
        Drag(w2, {(tr.left + tr.right) / 2, tr.top + S(20)});
        PumpFor(800);
        Check(FileHas(L"piles.json", "\"hwnd\":\"" + std::to_string((uint64_t)(uintptr_t)w2) + "\""),
              "3b) 커진 이름표에 놓으면 다시 더미에 들어감");
        Check(IsWindowVisible(w2) && SameRect(Rect(w2), r2), "3b) 펼친 더미에 넣은 창은 원래 자리에 열린 채로 있음");
        tabs = VisibleOfClass(L"WindowPilesFrameTab");
        if (!tabs.empty()) tr = Rect(tabs[0]);

        // 4) click the tab → fold back into the card at the same place
        PumpFor(GetDoubleClickTime());
        Click({(tr.left + tr.right) / 2, tr.top + S(15)});
        // Folding waits for the preview capture, which can take a second or more on a busy PC.
        Check(PumpUntil([&] { return !IsWindowVisible(w1) && !IsWindowVisible(w2); }, 5000), "4) 이름표 클릭 → 두 창 다시 접힘");
        PumpUntil([&] { return IsWindowVisible(card) != FALSE; }, 1000);
        Check(IsWindowVisible(card) && SameRect(Rect(card), cardRect), "4) 카드가 원래 자리로 돌아옴");
        Print("    card before %s, after %s, visible=%d\n", RectStr(cardRect).c_str(), RectStr(Rect(card)).c_str(),
              IsWindowVisible(card));
        Check(VisibleOfClass(L"WindowPilesFrameTab").empty() && VisibleOfClass(L"WindowPilesFrameEdge").empty(),
              "4) 테두리와 이름표가 사라짐");
    }

    // 4b) quick rename: hover the title → pencil; click → inline input
    PumpFor(GetDoubleClickTime());
    {
        RECT cr = Rect(card);
        POINT title{cr.left + S(12) + S(14) + S(20), cr.top + S(12) + S(18)};  // margin + padding + into the text
        MoveMouse(title.x - S(5), title.y);
        PumpFor(150);
        MoveMouse(title.x, title.y);
        PumpFor(400);
        SaveShot(L"4b_title_hover", {cr.left, cr.top, cr.right, cr.top + S(70)});
        Click(title);
        auto rn = VisibleOfClass(L"WindowPilesRename");
        Check(rn.size() == 1, "4b) 타이틀 이름 클릭 → 그 자리에 이름 입력창");
        Check(IsWindowVisible(card) && !IsWindowVisible(w1), "4b) 이름을 눌러도 더미가 펼쳐지지 않음");
        if (!rn.empty()) {
            TypeText(L"빠른 이름");
            PumpFor(200);
            SaveShot(L"4b_inline", Inflate(Rect(rn[0]), S(12)));
            Key(VK_RETURN);
            PumpFor(1200);
            Check(FileHas(L"piles.json", Utf8(L"\"빠른 이름\"")), "4b) Enter → 새 이름 저장");
        }
    }

    // 5) rename via the menu + editor
    PumpFor(GetDoubleClickTime());
    POINT rc = Center(cardRect);
    Click(rc, true);
    auto menus = VisibleOfClass(L"WindowPilesMenu");
    Check(menus.size() == 1, "5) 우클릭 → 메뉴 표시");
    if (!menus.empty()) {
        RECT mr = Rect(menus[0]);
        SaveShot(L"5_menu", Inflate(mr, S(16)));
        Click({mr.left + S(60), mr.top + S(5) + S(34) + S(17)});  // 2nd item: 이름·색 바꾸기…
        std::vector<HWND> eds;
        PumpUntil([&] { return (eds = VisibleOfClass(L"WindowPilesEditor")).size() == 1; }, 3000);
        Check(eds.size() == 1, "5) 이름·색 바꾸기 팝오버 표시");
        if (!eds.empty()) {
            TypeText(L"프로젝트 A");
            PumpFor(300);
            SaveShot(L"5_editor", Inflate(Rect(eds[0]), S(16)));
            Key(VK_RETURN);
            PumpFor(1200);
            Check(FileHas(L"piles.json", Utf8(L"\"프로젝트 A\"")), "5) 새 이름이 저장됨 (piles.json)");
        }
    }

    // 6) second launch → guide in the running instance
    HANDLE second = Launch(appExe, L"");
    if (second) WaitForSingleObject(second, 5000), CloseHandle(second);
    PumpFor(800);
    auto guides = VisibleOfClass(L"WindowPilesGuide");
    Check(guides.size() == 1, "6) 다시 실행하면 사용법 창이 뜸 (중복 실행 없음)");
    if (!guides.empty()) {
        SaveShot(L"6_guide", Inflate(Rect(guides[0]), S(20)));
        PostMessageW(guides[0], WM_KEYDOWN, VK_ESCAPE, 0);
        PumpFor(300);
    }

    // 7) quit and relaunch → restored and put away again
    PostMessageW(mainWnd, WM_ENDSESSION, TRUE, 0);
    WaitForSingleObject(app, 5000);
    CloseHandle(app);
    PumpFor(500);
    Check(IsWindowVisible(w1) && IsWindowVisible(w2), "7) 종료하면 숨긴 창이 모두 나타남");
    app = Launch(appExe, L"--autostart");
    mainWnd = WaitMain(5000);
    PumpUntil([&] { return VisibleOfClass(L"WindowPilesPile").size() == 1 && !IsWindowVisible(w1) && !IsWindowVisible(w2); },
              8000);
    PumpFor(500);
    cards = VisibleOfClass(L"WindowPilesPile");
    Check(cards.size() == 1, "7) 재실행 → 더미가 복원됨");
    Check(!IsWindowVisible(w1) && !IsWindowVisible(w2), "7) 재실행 → 창들이 다시 더미로 들어감");
    if (cards.empty()) return 1;
    card = cards[0];
    SaveShot(L"7_restored", Inflate(Rect(card), S(4)));

    // 8) the app behind a hidden member quits → "다시 열어야 할 창"
    PostMessageW(w1, WM_CLOSE, 0, 0);
    PumpUntil([&] { return FileHas(L"piles.json", "\"missing\":[{") && FileHas(L"piles.json", "TW-Normal-1"); }, 5000);
    Check(FileHas(L"piles.json", "\"missing\":[{") && FileHas(L"piles.json", "TW-Normal-1"),
          "8) 숨겨진 창이 닫히면 '다시 열어야 할 창' 목록에 기록");
    SaveShot(L"8_missing", Inflate(Rect(card), S(4)));

    // 9) a new window of that app goes back in → removed from the list
    HANDLE tw = Launch(g_bin + L"\\TestWindows.exe", L"--normal 1 --origin " + std::to_wstring(work.left + S(40)) + L" " +
                                                         std::to_wstring(work.top + S(300)) + L" --seconds 60");
    HWND nw = nullptr;
    PumpUntil([&] { return (nw = FindTopLevelByTitle(L"TW-Normal-1")) != nullptr && IsWindowVisible(nw); }, 4000);
    if (nw) {
        PumpFor(400);
        Drag(nw, Center(Rect(card)));
        Check(PumpUntil([&] { return !IsWindowVisible(nw); }, 5000), "9) 같은 앱의 새 창을 카드에 넣음");
        PumpFor(800);
        Check(FileHas(L"piles.json", "\"missing\":[]"), "9) '다시 열어야 할 창' 목록에서 제거됨");
    } else {
        Check(false, "9) 새 TestWindows 창을 찾지 못함");
    }

    // 10) crash → watchdog
    HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, [&] {
        DWORD pid = 0;
        GetWindowThreadProcessId(mainWnd, &pid);
        return pid;
    }());
    TerminateProcess(p, 0xDEAD);
    CloseHandle(p);
    bool back = PumpUntil([&] { return IsWindowVisible(w2) && (!nw || IsWindowVisible(nw)); }, 4000);
    Check(back, "10) 앱 강제 종료 → 감시자가 숨긴 창 복구");
    if (nw) PostMessageW(nw, WM_CLOSE, 0, 0);
    if (tw) CloseHandle(tw);
    if (app) CloseHandle(app);

    MoveMouse(cursor0.x, cursor0.y);
    Print("\n%s (%d failed)\n", g_fail ? "E2E FAILED" : "E2E PASSED", g_fail);
    return g_fail ? 1 : 0;
}
