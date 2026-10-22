// Stacking order is kept (no mouse input). Needs WindowPiles NOT running.
// Uses its own data folder (WINDOWPILES_DATA_DIR), never the user's piles.
//
//   1. pile A holds W1..W3 (open, not active), pile B holds W4 and its card overlaps A's from above
//      W2 is put in front, then W1, then W3 at the back; A is folded
//   2. A is opened (card click)      → W2 is in front again, then W1, then W3
//   3. A is folded from the top tab  → A's card goes back under B's, not on top of it
//   4. quit, shuffle, start again, open A → still W2, W1, W3 (the order was saved)
#include "poc_common.h"

#include <string>
#include <vector>

using namespace poc;

namespace {

int g_fail = 0;
std::wstring g_bin;
DWORD g_appPid = 0;

void Check(bool ok, const char* what) {
    Print("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

std::wstring DataDir() { return g_bin + L"\\e2e-data"; }

HANDLE Launch(const std::wstring& exe, const std::wstring& args, DWORD* pid = nullptr) {
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return nullptr;
    CloseHandle(pi.hThread);
    if (pid) *pid = pi.dwProcessId;
    return pi.hProcess;
}

std::string Member(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    WINDOWPLACEMENT wp{sizeof(wp)};
    GetWindowPlacement(h, &wp);
    const RECT& r = wp.rcNormalPosition;
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"hwnd\":\"%llu\",\"pid\":%lu,\"createTime\":\"%llu\",\"class\":\"TestWindowsClass\",\"exe\":\"\","
             "\"title\":\"%s\",\"hidden\":false,\"showCmd\":1,\"flags\":0,\"normal\":[%ld,%ld,%ld,%ld],\"maxPos\":[-1,-1]}",
             (unsigned long long)(uintptr_t)h, pid, (unsigned long long)ProcessCreateTime(pid),
             Utf8(WindowTitle(h)).c_str(), r.left, r.top, r.right, r.bottom);
    return buf;
}

// Position in the z-order (0 = front-most top-level window).
int ZIndex(HWND target) {
    struct Ctx {
        HWND target;
        int i, found;
    } ctx{target, 0, -1};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = (Ctx*)lp;
        if (h == c->target) {
            c->found = c->i;
            return FALSE;
        }
        ++c->i;
        return TRUE;
    }, (LPARAM)&ctx);
    return ctx.found;
}

// The app's visible window of a class whose top is closest to `top`.
HWND AppWindowNear(const wchar_t* cls, LONG top) {
    struct Ctx {
        const wchar_t* cls;
        LONG top;
        HWND best;
        LONG dist;
    } ctx{cls, top, nullptr, LONG_MAX};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = (Ctx*)lp;
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid != g_appPid || !IsWindowVisible(h) || WindowClass(h) != c->cls) return TRUE;
        RECT r;
        GetWindowRect(h, &r);
        LONG d = labs(r.top - c->top);
        if (d < c->dist) c->dist = d, c->best = h;
        return TRUE;
    }, (LPARAM)&ctx);
    return ctx.best;
}

void ClickCard(HWND card) {
    RECT r;
    GetClientRect(card, &r);
    LPARAM pt = MAKELPARAM((r.right - r.left) / 2, (r.bottom - r.top) * 2 / 3);
    PostMessageW(card, WM_LBUTTONDOWN, MK_LBUTTON, pt);
    PostMessageW(card, WM_LBUTTONUP, 0, pt);
}

void Raise(HWND h) { SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); }

bool AllHidden(const std::vector<HWND>& ws) {
    for (HWND h : ws)
        if (IsWindowVisible(h)) return false;
    return true;
}
bool AllShown(const std::vector<HWND>& ws) {
    for (HWND h : ws)
        if (!IsWindowVisible(h)) return false;
    return true;
}

}  // namespace

// e2e_order [path\to\WindowPiles.exe]   (default: the exe next to this test)
int wmain(int argc, wchar_t** argv) {
    InitProcess();
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    g_bin = self;
    g_bin = g_bin.substr(0, g_bin.find_last_of(L'\\'));
    std::wstring appExe = argc > 1 ? std::wstring(argv[1]) : g_bin + L"\\WindowPiles.exe";
    SetEnvironmentVariableW(L"WINDOWPILES_NO_RELAUNCH", L"1");
    SetEnvironmentVariableW(L"WINDOWPILES_DATA_DIR", DataDir().c_str());
    CreateDirectoryW(DataDir().c_str(), nullptr);
    CloseHandle(CreateFileW((DataDir() + L"\\firstrun.done").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr));
    DeleteFileW((DataDir() + L"\\recovery.json").c_str());
    if (FindWindowW(L"WindowPilesMain", nullptr)) {
        Print("WindowPiles is running; quit it first.\n");
        return 2;
    }

    HANDLE tw = Launch(g_bin + L"\\TestWindows.exe", L"--normal 4 --static --seconds 180");
    HWND w[4] = {};
    PumpUntil([&] {
        for (int i = 0; i < 4; ++i)
            if (!w[i]) w[i] = FindTopLevelByTitle(L"TW-Normal-" + std::to_wstring(i + 1));
        return w[0] && w[1] && w[2] && w[3];
    }, 6000);
    if (!w[3]) return 2;
    std::vector<HWND> a = {w[0], w[1], w[2]};

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    UINT dpi = GetDpiForSystem();
    LONG x = work.right - MulDiv(340, dpi, 96), yA = work.top + MulDiv(80, dpi, 96), yB = yA + MulDiv(44, dpi, 96);
    char head[96];
    snprintf(head, sizeof(head), "\"x\":%ld,\"y\":%ld", x, yA);
    std::string pa = std::string("{\"id\":1,\"name\":\"A\",\"customName\":true,\"color\":11107891,") + head +
                     ",\"folded\":false,\"members\":[" + Member(w[0]) + "," + Member(w[1]) + "," + Member(w[2]) +
                     "],\"missing\":[]}";
    snprintf(head, sizeof(head), "\"x\":%ld,\"y\":%ld", x, yB);
    std::string pb = std::string("{\"id\":2,\"name\":\"B\",\"customName\":true,\"color\":6390596,") + head +
                     ",\"folded\":true,\"members\":[" + Member(w[3]) + "],\"missing\":[]}";
    std::string state = "{\"schema\":1,\"piles\":[" + pa + "," + pb + "]}";  // B is above A
    HANDLE f = CreateFileW((DataDir() + L"\\piles.json").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD n;
    WriteFile(f, state.data(), (DWORD)state.size(), &n, nullptr);
    CloseHandle(f);

    HANDLE app = Launch(appExe, L"--autostart", &g_appPid);
    PumpUntil([&] { return FindWindowW(L"WindowPilesMain", nullptr) && !IsWindowVisible(w[3]); }, 8000);
    PumpFor(800);

    // 1) W2 in front, then W1, then W3; fold A
    Raise(w[2]);
    Raise(w[0]);
    Raise(w[1]);
    PumpFor(200);
    bool arranged = ZIndex(w[1]) < ZIndex(w[0]) && ZIndex(w[0]) < ZIndex(w[2]);
    Print("    z before fold: W1=%d W2=%d W3=%d, folded=%d\n", ZIndex(w[0]), ZIndex(w[1]), ZIndex(w[2]),
          !IsWindowVisible(w[0]));
    HWND cardA = AppWindowNear(L"WindowPilesPile", yA);
    if (cardA) ClickCard(cardA);
    bool folded = PumpUntil([&] { return AllHidden(a); }, 6000);
    Check(arranged && folded, "1) W2 > W1 > W3 순서로 겹친 창들을 더미 A에 접음");

    // 2) open A → same order
    PumpFor(GetDoubleClickTime() + 100);
    cardA = AppWindowNear(L"WindowPilesPile", yA);
    if (cardA) ClickCard(cardA);
    PumpUntil([&] { return AllShown(a); }, 6000);
    PumpFor(1200);  // restores land, then the windows are raised in order
    Print("    z after open: W1=%d W2=%d W3=%d\n", ZIndex(w[0]), ZIndex(w[1]), ZIndex(w[2]));
    Check(ZIndex(w[1]) < ZIndex(w[0]) && ZIndex(w[0]) < ZIndex(w[2]), "2) 펼치면 접기 전 순서 그대로 (W2가 맨 앞, W3가 맨 뒤)");

    // 3) fold A from the top tab → its card goes back under B's
    PumpFor(GetDoubleClickTime() + 100);
    HWND tab = AppWindowNear(L"WindowPilesFrameTab", work.top);
    if (tab) PostMessageW(tab, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
    PumpUntil([&] { return AllHidden(a); }, 6000);
    PumpFor(800);
    cardA = AppWindowNear(L"WindowPilesPile", yA);
    HWND cardB = AppWindowNear(L"WindowPilesPile", yB);
    Check(cardA && cardB && cardA != cardB && ZIndex(cardB) < ZIndex(cardA),
          "3) 접으면 A 카드가 원래 층(B 카드 아래)으로 돌아옴");

    // 4) quit (windows come back), shuffle them, start again, open A → order from before
    PostMessageW(FindWindowW(L"WindowPilesMain", nullptr), WM_ENDSESSION, TRUE, 0);
    WaitForSingleObject(app, 6000);
    CloseHandle(app);
    PumpUntil([&] { return AllShown(a); }, 5000);
    Raise(w[1]);
    Raise(w[0]);
    Raise(w[2]);  // now W3 in front
    app = Launch(appExe, L"--autostart", &g_appPid);
    PumpUntil([&] { return FindWindowW(L"WindowPilesMain", nullptr) && AllHidden(a); }, 8000);
    PumpFor(800);
    cardA = AppWindowNear(L"WindowPilesPile", yA);
    if (cardA) ClickCard(cardA);
    PumpUntil([&] { return AllShown(a); }, 6000);
    PumpFor(1200);
    Check(ZIndex(w[1]) < ZIndex(w[0]) && ZIndex(w[0]) < ZIndex(w[2]), "4) 다시 시작해도 저장된 순서로 펼침");

    PostMessageW(FindWindowW(L"WindowPilesMain", nullptr), WM_ENDSESSION, TRUE, 0);
    WaitForSingleObject(app, 6000);
    CloseHandle(app);
    TerminateProcess(tw, 0);
    CloseHandle(tw);
    Print("\n%s (%d failed)\n", g_fail ? "ORDER FAILED" : "ORDER PASSED", g_fail);
    return g_fail ? 1 : 0;
}
