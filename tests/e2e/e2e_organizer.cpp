// Edge organizer (no mouse input). Needs WindowPiles NOT running.
// Uses its own data folder (WINDOWPILES_DATA_DIR), never the user's piles.
//
//   1. pile A (W1, W2) is kept in the organizer, pile B (W3) is a desktop card → only B's card shows,
//      plus the bookmark on the screen edge
//   2. click the bookmark                    → the tray opens with A in it
//   3. click A in the tray                   → A opens (W1, W2 shown)
//   4. fold A from its top tab               → its windows go away and no card appears on the desktop
//   5. quit and start again                  → still kept in the organizer (saved)
#include "poc_common.h"

#include <string>
#include <vector>

using namespace poc;

namespace {

int g_fail = 0;
std::wstring g_bin;

void Check(bool ok, const char* what) {
    Print("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

std::wstring DataDir() { return g_bin + L"\\e2e-data"; }

HANDLE Launch(const std::wstring& exe, const std::wstring& args) {
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return nullptr;
    CloseHandle(pi.hThread);
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

std::string ReadState() {
    HANDLE f = CreateFileW((DataDir() + L"\\piles.json").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    std::string s(1 << 20, '\0');
    DWORD n = 0;
    ReadFile(f, s.data(), (DWORD)s.size(), &n, nullptr);
    CloseHandle(f);
    s.resize(n);
    return s;
}

void Click(HWND h, LPARAM pt) {
    PostMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, pt);
    PostMessageW(h, WM_LBUTTONUP, 0, pt);
}

int Visible(const wchar_t* cls) {
    struct Ctx {
        const wchar_t* cls;
        int n;
    } ctx{cls, 0};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = (Ctx*)lp;
        if (IsWindowVisible(h) && WindowClass(h) == c->cls) ++c->n;
        return TRUE;
    }, (LPARAM)&ctx);
    return ctx.n;
}

}  // namespace

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
    DeleteFileW((DataDir() + L"\\organizer.ini").c_str());  // the bookmark starts at its default place
    DeleteFileW((DataDir() + L"\\organizer.off").c_str());
    if (FindWindowW(L"WindowPilesMain", nullptr)) {
        Print("WindowPiles is running; quit it first.\n");
        return 2;
    }

    HANDLE tw = Launch(g_bin + L"\\TestWindows.exe", L"--normal 3 --static --seconds 120");
    HWND w[3] = {};
    PumpUntil([&] {
        for (int i = 0; i < 3; ++i)
            if (!w[i]) w[i] = FindTopLevelByTitle(L"TW-Normal-" + std::to_wstring(i + 1));
        return w[0] && w[1] && w[2];
    }, 6000);
    if (!w[2]) return 2;

    std::string state = "{\"schema\":1,\"piles\":["
                        "{\"id\":1,\"name\":\"A\",\"customName\":true,\"color\":11107891,\"x\":200,\"y\":120,\"folded\":true,"
                        "\"docked\":true,\"members\":[" + Member(w[0]) + "," + Member(w[1]) + "],\"missing\":[]},"
                        "{\"id\":2,\"name\":\"B\",\"customName\":true,\"color\":6390596,\"x\":200,\"y\":420,\"folded\":true,"
                        "\"members\":[" + Member(w[2]) + "],\"missing\":[]}]}";
    HANDLE f = CreateFileW((DataDir() + L"\\piles.json").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD n;
    WriteFile(f, state.data(), (DWORD)state.size(), &n, nullptr);
    CloseHandle(f);

    HANDLE app = Launch(appExe, L"--autostart");
    PumpUntil([&] { return FindWindowW(L"WindowPilesMain", nullptr) && !IsWindowVisible(w[0]) && !IsWindowVisible(w[2]); }, 8000);
    PumpFor(800);

    // 1) A lives in the organizer: only B's card on the desktop, and the bookmark
    HWND handle = FindWindowW(L"WindowPilesOrganizerHandle", nullptr);
    Check(handle && IsWindowVisible(handle), "1) 화면 가장자리에 오거나이저 막대가 보임");
    Check(Visible(L"WindowPilesPile") == 1, "1) 오거나이저에 꽂힌 더미 A는 바탕화면에 카드가 없음 (B만 보임)");

    // 2) a click on the bookmark opens the tray
    if (handle) Click(handle, MAKELPARAM(4, 40));
    HWND tray = nullptr;
    PumpUntil([&] { return (tray = FindWindowW(L"WindowPilesOrganizerTray", nullptr)) && IsWindowVisible(tray); }, 3000);
    Check(tray && IsWindowVisible(tray), "2) 막대를 누르면 카드가 쌓인 트레이가 열림");

    // 3) clicking A in the tray opens it
    if (tray) {
        UINT dpi = GetDpiForWindow(tray);
        RECT tr;
        GetClientRect(tray, &tr);
        Click(tray, MAKELPARAM(tr.right / 2, MulDiv(12 + 24 + 33, dpi, 96)));  // padding + label + half a card
    }
    bool opened = PumpUntil([&] { return IsWindowVisible(w[0]) && IsWindowVisible(w[1]); }, 4000);
    Check(opened, "3) 트레이의 카드를 누르면 더미 A가 펼쳐짐");
    Check(!tray || !IsWindowVisible(tray), "3) 펼치면 트레이는 닫힘");

    // 4) fold A from the top tab: windows go away, and A stays in the organizer (no desktop card)
    HWND tab = FindWindowW(L"WindowPilesFrameTab", nullptr);
    if (tab) PostMessageW(tab, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
    bool folded = PumpUntil([&] { return !IsWindowVisible(w[0]) && !IsWindowVisible(w[1]); }, 4000);
    PumpFor(600);
    Check(folded, "4) 이름표를 누르면 다시 접힘");
    Check(Visible(L"WindowPilesPile") == 1, "4) 접은 뒤에도 A는 오거나이저에 남음 (바탕화면 카드 없음)");

    // 5) restart: still in the organizer
    PostMessageW(FindWindowW(L"WindowPilesMain", nullptr), WM_ENDSESSION, TRUE, 0);
    WaitForSingleObject(app, 5000);
    CloseHandle(app);
    Check(ReadState().find("\"docked\":true") != std::string::npos, "5) 오거나이저에 꽂힌 상태가 저장됨");
    app = Launch(appExe, L"--autostart");
    PumpUntil([&] { return FindWindowW(L"WindowPilesMain", nullptr) && !IsWindowVisible(w[0]); }, 8000);
    PumpFor(1000);
    Check(Visible(L"WindowPilesPile") == 1 && FindWindowW(L"WindowPilesOrganizerHandle", nullptr),
          "5) 다시 시작해도 A는 오거나이저에 있음");

    PostMessageW(FindWindowW(L"WindowPilesMain", nullptr), WM_ENDSESSION, TRUE, 0);
    WaitForSingleObject(app, 5000);
    CloseHandle(app);
    TerminateProcess(tw, 0);
    CloseHandle(tw);
    Print("\n%s (%d failed)\n", g_fail ? "ORGANIZER FAILED" : "ORGANIZER PASSED", g_fail);
    return g_fail ? 1 : 0;
}
