// Windows gone after a reboot come back as they were (no mouse input). Needs WindowPiles NOT running.
// Uses its own data folder (WINDOWPILES_DATA_DIR), never the user's piles.
//
//   1. a pile holds one live window and a "다시 열어야 할 창" entry remembered with its command line
//      (here: TestWindows started with its own arguments, as a document would be)
//   2. opening the pile starts that entry with its arguments → the very window comes back and joins
//   3. the entry leaves the list, and the window is remembered with its command line for next time
#include "poc_common.h"

#include <string>

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

std::string Esc(const std::wstring& s) {
    std::string u = Utf8(s), o;
    for (char c : u) {
        if (c == '\\' || c == '"') o += '\\';
        o += c;
    }
    return o;
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

HWND AppCard() {
    struct Ctx {
        HWND found = nullptr;
    } ctx;
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == g_appPid && IsWindowVisible(h) && WindowClass(h) == L"WindowPilesPile") {
            ((Ctx*)lp)->found = h;
            return FALSE;
        }
        return TRUE;
    }, (LPARAM)&ctx);
    return ctx.found;
}

}  // namespace

int wmain() {
    InitProcess();
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    g_bin = self;
    g_bin = g_bin.substr(0, g_bin.find_last_of(L'\\'));
    SetEnvironmentVariableW(L"WINDOWPILES_NO_RELAUNCH", L"1");
    SetEnvironmentVariableW(L"WINDOWPILES_DATA_DIR", DataDir().c_str());
    CreateDirectoryW(DataDir().c_str(), nullptr);
    CloseHandle(CreateFileW((DataDir() + L"\\firstrun.done").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr));
    CloseHandle(CreateFileW((DataDir() + L"\\organizer.off").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr));
    DeleteFileW((DataDir() + L"\\recovery.json").c_str());
    if (FindWindowW(L"WindowPilesMain", nullptr)) {
        Print("WindowPiles is running; quit it first.\n");
        return 2;
    }

    // The live window of the pile, and the one that is "gone": remembered with its command line.
    std::wstring tw = g_bin + L"\\TestWindows.exe";
    HANDLE live = Launch(tw, L"--fixed --seconds 120");
    HWND w0 = nullptr;
    PumpUntil([&] { return (w0 = FindTopLevelByTitle(L"TW-Fixed-1")) != nullptr; }, 5000);
    if (!w0) return 2;
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    std::wstring args = L"--normal 1 --origin " + std::to_wstring(work.left + 60) + L" " + std::to_wstring(work.top + 300) +
                        L" --seconds 60";
    std::string missing = "{\"exe\":\"" + Esc(tw) + "\",\"app\":\"TestWindows\",\"title\":\"TW-Normal-1\",\"launch\":\"" +
                          Esc(L"\"" + tw + L"\" " + args) + "\",\"aumid\":\"\"}";
    char head[96];
    snprintf(head, sizeof(head), "\"x\":%ld,\"y\":%ld", work.right - 340, work.top + 80);
    std::string state = std::string("{\"schema\":1,\"piles\":[{\"id\":1,\"name\":\"R\",\"customName\":true,\"color\":11107891,") +
                        head + ",\"folded\":true,\"members\":[" + Member(w0) + "],\"missing\":[" + missing + "]}]}";
    HANDLE f = CreateFileW((DataDir() + L"\\piles.json").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD n;
    WriteFile(f, state.data(), (DWORD)state.size(), &n, nullptr);
    CloseHandle(f);

    HANDLE app = Launch(g_bin + L"\\WindowPiles.exe", L"", &g_appPid);  // a plain start: not "after a reboot"
    PumpUntil([&] { return FindWindowW(L"WindowPilesMain", nullptr) && !IsWindowVisible(w0); }, 8000);
    PumpFor(800);
    Check(FindTopLevelByTitle(L"TW-Normal-1") == nullptr, "1) 다시 열어야 할 창은 아직 없음");

    // 2) open the pile: the entry is started with its own arguments, and that window joins
    HWND card = AppCard();
    if (card) {
        RECT r;
        GetClientRect(card, &r);
        LPARAM pt = MAKELPARAM((r.right - r.left) / 2, MulDiv(70, GetDpiForSystem(), 96));  // the previews, not the list
        PostMessageW(card, WM_LBUTTONDOWN, MK_LBUTTON, pt);
        PostMessageW(card, WM_LBUTTONUP, 0, pt);
    }
    HWND back = nullptr;
    bool came = PumpUntil([&] { return (back = FindTopLevelByTitle(L"TW-Normal-1")) != nullptr && IsWindowVisible(back); }, 8000);
    Check(came, "2) 더미를 펼치면 기억한 명령줄(인수 포함)로 그 창이 다시 열림");
    bool joined = back && PumpUntil([&] {
        std::string st = ReadState();
        return st.find(std::to_string((uint64_t)(uintptr_t)back)) != std::string::npos && st.find("\"missing\":[]") != std::string::npos;
    }, 8000);
    Check(joined, "3) 다시 열린 창이 더미에 들어가고 목록에서 빠짐");
    std::string st = ReadState();
    Check(st.find("--normal 1") != std::string::npos, "3) 그 창의 명령줄을 다음을 위해 기억함");

    PostMessageW(FindWindowW(L"WindowPilesMain", nullptr), WM_ENDSESSION, TRUE, 0);
    WaitForSingleObject(app, 6000);
    CloseHandle(app);
    if (back) {
        DWORD pid = 0;
        GetWindowThreadProcessId(back, &pid);
        HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (p) TerminateProcess(p, 0), CloseHandle(p);
    }
    TerminateProcess(live, 0);
    CloseHandle(live);
    DeleteFileW((DataDir() + L"\\organizer.off").c_str());
    Print("\n%s (%d failed)\n", g_fail ? "RELAUNCH E2E FAILED" : "RELAUNCH E2E PASSED", g_fail);
    return g_fail ? 1 : 0;
}
