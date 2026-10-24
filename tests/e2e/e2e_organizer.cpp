// Edge organizer (no mouse input). Needs WindowPiles NOT running.
// Uses its own data folder (WINDOWPILES_DATA_DIR), never the user's piles.
//
//   1. pile A (W1, W2) is kept in the organizer, pile B (W3) is a desktop card → only B's card shows,
//      plus the bookmark on the screen edge
//   2. click the bookmark                    → the tray opens with A in it
//   3. click A in the tray                   → A opens (W1, W2 shown)
//   4. fold A from its top tab               → its windows go away and no card appears on the desktop
//   5. quit and start again                  → still kept in the organizer (saved)
//   6. "＋ 새 메모" at the bottom of the tray   → a memo window and a .txt file in the memo folder; what is
//      typed is in the file after closing; a memo closed empty leaves no file
//   7. drag a memo card below the next one   → their order is remembered; drag one onto the trash can
//      (what "＋ 새 메모" becomes while a memo is carried) → its file is gone
//   8. with A and B both in the organizer, drag A below B → the new order is saved
#include "poc_common.h"

#include <cstring>
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

std::vector<std::wstring> Memos() {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((DataDir() + L"\\memos\\*.txt").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return out;
    do out.push_back(DataDir() + L"\\memos\\" + fd.cFileName);
    while (FindNextFileW(f, &fd));
    FindClose(f);
    return out;
}

std::string ReadAll(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    std::string s(1 << 16, '\0');
    DWORD n = 0;
    ReadFile(f, s.data(), (DWORD)s.size(), &n, nullptr);
    CloseHandle(f);
    s.resize(n);
    return s;
}

// Opens the tray from the bookmark and clicks "＋ 새 메모" (the last thing in it); returns the memo window.
HWND OpenTray() {
    HWND handle = FindWindowW(L"WindowPilesOrganizerHandle", nullptr), tray = nullptr;
    if (!handle) return nullptr;
    Click(handle, MAKELPARAM(4, 40));
    PumpUntil([&] { return (tray = FindWindowW(L"WindowPilesOrganizerTray", nullptr)) && IsWindowVisible(tray); }, 3000);
    return tray && IsWindowVisible(tray) ? tray : nullptr;
}

// Presses at `from`, moves past the drag threshold to `to` (twice: starting the drag, then over the target),
// and lets go there; tray client coordinates in DIPs.
void DragInTray(HWND tray, POINT from, POINT to) {
    UINT dpi = GetDpiForWindow(tray);
    auto at = [&](POINT p) { return MAKELPARAM(MulDiv(p.x, dpi, 96), MulDiv(p.y, dpi, 96)); };
    PostMessageW(tray, WM_LBUTTONDOWN, MK_LBUTTON, at(from));
    PumpFor(80);
    PostMessageW(tray, WM_MOUSEMOVE, MK_LBUTTON, at(to));
    PumpFor(80);
    PostMessageW(tray, WM_MOUSEMOVE, MK_LBUTTON, at(to));
    PumpFor(80);
    PostMessageW(tray, WM_LBUTTONUP, 0, at(to));
    PumpFor(300);
}

std::wstring MemoOrder() {
    wchar_t buf[4096] = {};
    GetPrivateProfileStringW(L"memo", L"order", L"", buf, 4096, (DataDir() + L"\\organizer.ini").c_str());
    return buf;
}

std::wstring FileName(const std::wstring& path) { return path.substr(path.find_last_of(L'\\') + 1); }

HWND NewMemoFromTray() {
    HWND tray = OpenTray();
    if (!tray) return nullptr;
    RECT tr;
    GetClientRect(tray, &tr);
    Click(tray, MAKELPARAM(tr.right / 2, tr.bottom - MulDiv(12 + 18, GetDpiForWindow(tray), 96)));  // padding + half the button
    HWND memo = nullptr;
    PumpUntil([&] { return (memo = FindWindowW(L"WindowPilesMemo", nullptr)) && IsWindowVisible(memo); }, 3000);
    return memo;
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
    if (tray) {
        RECT tr;
        GetWindowRect(tray, &tr);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(tray, MONITOR_DEFAULTTONEAREST), &mi);
        Check(tr.bottom - tr.top <= (mi.rcWork.bottom - mi.rcWork.top) * 60 / 100 + 2, "2) 트레이는 화면 높이의 60%를 넘지 않음");
    }

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
    // The tab comes up once the opening animation (240 ms) is over.
    HWND tab = nullptr;
    PumpUntil([&] { return (tab = FindWindowW(L"WindowPilesFrameTab", nullptr)) && IsWindowVisible(tab); }, 3000);
    PumpFor(300);
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

    // 6) memos
    for (const auto& m : Memos()) DeleteFileW(m.c_str());
    HWND memo = NewMemoFromTray();
    Check(memo != nullptr, "6) 트레이의 ＋ 버튼을 누르면 메모 창이 열림");
    Check(Memos().size() == 1, "6) 메모 폴더에 .txt 파일이 생김");
    if (memo) {
        HWND edit = FindWindowExW(memo, nullptr, L"RICHEDIT50W", nullptr);
        SendMessageW(edit, WM_SETTEXT, 0, (LPARAM)L"# 장보기\r- 우유\r- 계란");
        PostMessageW(memo, WM_CLOSE, 0, 0);
        PumpUntil([&] { return !IsWindow(memo); }, 3000);
    }
    std::vector<std::wstring> files = Memos();
    std::string text = files.size() == 1 ? ReadAll(files[0]) : "";
    Check(text == Utf8(L"# 장보기\r\n- 우유\r\n- 계란"), "6) 쓴 내용이 그대로 .txt에 저장됨 (줄바꿈은 CRLF)");
    memo = NewMemoFromTray();
    if (memo) {
        PostMessageW(memo, WM_CLOSE, 0, 0);
        PumpUntil([&] { return !IsWindow(memo); }, 3000);
    }
    Check(memo && Memos().size() == 1, "6) 아무것도 안 쓰고 닫은 메모는 파일이 남지 않음");

    // 7) a second memo (newest, so on top), then: drag the top one below the other, and one into the trash
    std::wstring first = files.size() == 1 ? files[0] : L"";
    memo = NewMemoFromTray();
    if (memo) {
        SendMessageW(FindWindowExW(memo, nullptr, L"RICHEDIT50W", nullptr), WM_SETTEXT, 0, (LPARAM)L"할 일");
        PostMessageW(memo, WM_CLOSE, 0, 0);
        PumpUntil([&] { return !IsWindow(memo); }, 3000);
    }
    PumpFor(400);
    // Tray layout (DIPs, from the bottom): 12 padding, the 36-high button, then memo rows 44 high every 52.
    tray = OpenTray();
    RECT tr{};
    if (tray) GetClientRect(tray, &tr);
    int bottom = tray ? MulDiv(tr.bottom, 96, GetDpiForWindow(tray)) : 0, mid = tray ? MulDiv(tr.right / 2, 96, GetDpiForWindow(tray)) : 0;
    int plusY = bottom - 12 - 18, memo2Y = bottom - 12 - 36 - 52 + 22, memo1Y = memo2Y - 52;
    if (tray) DragInTray(tray, {mid, memo1Y}, {mid, memo2Y});
    Check(!first.empty() && MemoOrder().rfind(FileName(first) + L"|", 0) == 0, "7) 메모 카드를 끌어 순서를 바꾸면 그 순서가 기억됨");
    if (tray) DragInTray(tray, {mid, memo1Y}, {mid, plusY});
    PumpFor(900);  // the crumple animation
    files = Memos();
    Check(files.size() == 1 && files[0] != first, "7) 메모 카드를 휴지통(새 메모 자리)에 끌어 넣으면 버려짐");

    // 8) both piles in the organizer: drag A (top) below B
    PostMessageW(FindWindowW(L"WindowPilesMain", nullptr), WM_ENDSESSION, TRUE, 0);
    WaitForSingleObject(app, 5000);
    CloseHandle(app);
    PumpUntil([&] { return (state = ReadState()).find("\"id\":2,") != std::string::npos; }, 3000);
    size_t b = state.find("\"id\":2,");
    size_t d = b == std::string::npos ? b : state.find("\"docked\":false", b);
    if (d != std::string::npos) {  // B goes into the organizer too
        state.replace(d, std::strlen("\"docked\":false"), "\"docked\":true");
        f = CreateFileW((DataDir() + L"\\piles.json").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        WriteFile(f, state.data(), (DWORD)state.size(), &n, nullptr);
        CloseHandle(f);
    }
    app = Launch(appExe, L"--autostart");
    PumpUntil([&] { return FindWindowW(L"WindowPilesMain", nullptr) && !IsWindowVisible(w[2]); }, 8000);
    PumpFor(1000);
    tray = OpenTray();
    if (tray) DragInTray(tray, {mid, 12 + 24 + 33}, {mid, 12 + 24 + 33 + 74});
    PostMessageW(FindWindowW(L"WindowPilesMain", nullptr), WM_ENDSESSION, TRUE, 0);
    WaitForSingleObject(app, 5000);
    CloseHandle(app);
    PumpUntil([&] { return (state = ReadState()).find("\"id\":1,") != std::string::npos; }, 3000);
    size_t a = state.find("\"id\":1,");
    b = state.find("\"id\":2,");
    bool reordered = a != std::string::npos && b != std::string::npos &&
                     state.substr(a, b > a ? b - a : std::string::npos).find("\"dockOrder\":1") != std::string::npos &&
                     state.find("\"dockOrder\":0", b) != std::string::npos;
    if (!reordered) Print("   state: %s\n", state.c_str());
    Check(d != std::string::npos && reordered, "8) 오거나이저 안에서 더미 카드를 끌어 순서를 바꾸면 저장됨");
    app = Launch(appExe, L"--autostart");
    PumpUntil([&] { return FindWindowW(L"WindowPilesMain", nullptr) != nullptr; }, 8000);
    PumpFor(500);

    PostMessageW(FindWindowW(L"WindowPilesMain", nullptr), WM_ENDSESSION, TRUE, 0);
    WaitForSingleObject(app, 5000);
    CloseHandle(app);
    TerminateProcess(tw, 0);
    CloseHandle(tw);
    Print("\n%s (%d failed)\n", g_fail ? "ORGANIZER FAILED" : "ORGANIZER PASSED", g_fail);
    return g_fail ? 1 : 0;
}
