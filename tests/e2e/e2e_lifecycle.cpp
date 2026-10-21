// Lifecycle check of WindowPiles.exe: restart, crash, logoff/shutdown with a frozen app, reboot.
// No mouse input. Needs WindowPiles NOT running; starts its own TestWindows. Back up
// %LOCALAPPDATA%\WindowPiles\piles.json first if it holds real piles (this test overwrites it).
//
//   1. piles.json says "W1, W2 folded" → start the app      → both hidden again, card shown
//   2. the app crashes (after 20 s)                          → watchdog relaunches it, windows stay put away
//   3. logoff/shutdown while the windows' app is frozen      → app exits promptly (no hang),
//      (threads suspended, like Modern Standby)                windows come back once the app thaws
//   4. "reboot": windows gone, app starts                    → listed as "다시 열어야 할 창";
//      the app reopens windows with the same titles          → put back into the pile automatically
//   5. "End task" (exit code 1)                              → windows shown, app NOT relaunched
#include "poc_common.h"

#include <tlhelp32.h>

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

// The app under test uses this folder instead of %LOCALAPPDATA%\WindowPiles (set in wmain).
std::wstring DataDir() {
    wchar_t buf[MAX_PATH] = {};
    GetEnvironmentVariableW(L"WINDOWPILES_DATA_DIR", buf, MAX_PATH);
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

HANDLE Launch(const std::wstring& exe, const std::wstring& args, DWORD* pid = nullptr) {
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return nullptr;
    CloseHandle(pi.hThread);
    if (pid) *pid = pi.dwProcessId;
    return pi.hProcess;
}

HWND MainWnd() { return FindWindowW(L"WindowPilesMain", nullptr); }
DWORD PidOf(HWND h) {
    DWORD pid = 0;
    if (h) GetWindowThreadProcessId(h, &pid);
    return pid;
}

int VisibleCards() {
    int n = 0;
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        if (IsWindowVisible(h) && WindowClass(h) == L"WindowPilesPile") ++*(int*)lp;
        return TRUE;
    }, (LPARAM)&n);
    return n;
}

void SuspendProcess(DWORD pid, bool suspend) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{sizeof(te)};
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE t = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!t) continue;
        if (suspend) SuspendThread(t);
        else ResumeThread(t);
        CloseHandle(t);
    }
    CloseHandle(snap);
}

std::string SavedMember(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    WINDOWPLACEMENT wp{sizeof(wp)};
    GetWindowPlacement(h, &wp);
    const RECT& r = wp.rcNormalPosition;
    std::string exe = Utf8(g_bin + L"\\TestWindows.exe"), esc;
    for (char c : exe) esc += (c == '\\') ? std::string("\\\\") : std::string(1, c);
    char buf[1024];
    snprintf(buf, sizeof(buf),
             "{\"hwnd\":\"%llu\",\"pid\":%lu,\"createTime\":\"%llu\",\"class\":\"TestWindowsClass\",\"exe\":\"%s\","
             "\"title\":\"%s\",\"hidden\":false,\"showCmd\":1,\"flags\":0,\"normal\":[%ld,%ld,%ld,%ld],\"maxPos\":[-1,-1]}",
             (unsigned long long)(uintptr_t)h, pid, (unsigned long long)ProcessCreateTime(pid), esc.c_str(),
             Utf8(WindowTitle(h)).c_str(), r.left, r.top, r.right, r.bottom);
    return buf;
}

}  // namespace

int wmain() {
    InitProcess();
    SetEnvironmentVariableW(L"WINDOWPILES_NO_RELAUNCH", L"1");  // keep the app under test as our child
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    g_bin = self;
    g_bin = g_bin.substr(0, g_bin.find_last_of(L'\\'));
    // Isolated data folder: never touch the user's real piles.
    SetEnvironmentVariableW(L"WINDOWPILES_DATA_DIR", (g_bin + L"\\e2e-data").c_str());
    CreateDirectoryW((g_bin + L"\\e2e-data").c_str(), nullptr);
    DeleteFileW((DataDir() + L"\\recovery.json").c_str());
    std::wstring app = g_bin + L"\\WindowPiles.exe", tw = g_bin + L"\\TestWindows.exe";
    if (MainWnd()) {
        Print("WindowPiles is running; quit it first.\n");
        return 2;
    }

    DWORD twPid = 0;
    HANDLE twProc = Launch(tw, L"--normal 2 --seconds 300", &twPid);
    HWND w1 = nullptr, w2 = nullptr;
    PumpUntil([&] { return (w1 = FindTopLevelByTitle(L"TW-Normal-1")) && (w2 = FindTopLevelByTitle(L"TW-Normal-2")); }, 5000);
    if (!w1 || !w2) return 2;

    // 1) state file from a previous run: both windows folded in one pile
    std::string state = "{\"schema\":1,\"piles\":[{\"id\":1,\"name\":\"테스트 더미\",\"customName\":true,\"color\":11107891,"
                        "\"x\":1500,\"y\":40,\"folded\":true,\"members\":[" + SavedMember(w1) + "," + SavedMember(w2) +
                        "],\"missing\":[]}]}";
    HANDLE f = CreateFileW((DataDir() + L"\\piles.json").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD n;
    WriteFile(f, state.data(), (DWORD)state.size(), &n, nullptr);
    CloseHandle(f);
    HANDLE appProc = Launch(app, L"--autostart");
    PumpUntil([&] { return MainWnd() != nullptr; }, 5000);
    bool folded = PumpUntil([&] { return !IsWindowVisible(w1) && !IsWindowVisible(w2); }, 5000);
    Check(folded && VisibleCards() == 1, "1) 재시작: 저장된 더미대로 창 2개를 다시 접음");

    // 2) crash after 20 s → watchdog relaunch, windows stay folded
    Print("    (앱 가동 시간 20초 대기…)\n");
    PumpFor(21000);
    DWORD oldPid = PidOf(MainWnd());
    TerminateProcess(appProc, 0xC0000005);  // access violation exit code
    CloseHandle(appProc);
    bool relaunched = PumpUntil([&] { HWND m = MainWnd(); return m && PidOf(m) != oldPid; }, 10000);
    PumpFor(1500);
    Check(relaunched, "2) 크래시 → 감시자가 앱을 다시 실행");
    Check(!IsWindowVisible(w1) && !IsWindowVisible(w2) && VisibleCards() == 1, "2) 다시 실행된 앱이 더미를 그대로 유지");

    // 3) logoff/shutdown while the windows' app is frozen
    DWORD appPid = PidOf(MainWnd());
    HANDLE ap = OpenProcess(SYNCHRONIZE, FALSE, appPid);
    SuspendProcess(twPid, true);
    DWORD_PTR res = 0;
    double t0 = NowMs();
    SendMessageTimeoutW(MainWnd(), WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF, SMTO_ABORTIFHUNG, 8000, &res);
    PostMessageW(MainWnd(), WM_ENDSESSION, TRUE, ENDSESSION_LOGOFF);
    bool exited = WaitForSingleObject(ap, 8000) == WAIT_OBJECT_0;
    CloseHandle(ap);
    char buf[160];
    snprintf(buf, sizeof(buf), "3) 다른 앱이 얼어 있어도 로그아웃 처리가 멈추지 않음 (%.1f초 만에 종료)", (NowMs() - t0) / 1000);
    Check(exited && NowMs() - t0 < 6000, buf);
    SuspendProcess(twPid, false);
    bool shown = PumpUntil([&] { return IsWindowVisible(w1) && IsWindowVisible(w2); }, 5000);
    Check(shown, "3) 앱이 풀리면 숨겼던 창이 다시 나타남");
    std::string st = ReadState();
    Check(st.find("\"folded\":true") != std::string::npos && st.find(std::to_string((uint64_t)(uintptr_t)w1)) != std::string::npos,
          "3) 로그아웃 전 더미 상태(접힘·소속 창)가 저장됨");
    PumpFor(1500);  // the watchdog must NOT relaunch after a clean session end
    Check(MainWnd() == nullptr, "3) 정상 종료 후에는 다시 실행하지 않음");

    // 4) "reboot": the windows are gone; the app reopens them with the same titles
    HANDLE quit = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\TestWindowsQuit");
    if (quit) SetEvent(quit), CloseHandle(quit);
    WaitForSingleObject(twProc, 5000);
    CloseHandle(twProc);
    appProc = Launch(app, L"--autostart");
    PumpUntil([&] { return MainWnd() != nullptr; }, 5000);
    PumpFor(1500);
    st = ReadState();
    Check(st.find("\"missing\":[{") != std::string::npos, "4) 재부팅 후: 사라진 창이 '다시 열어야 할 창'으로 남음");
    twProc = Launch(tw, L"--normal 2 --seconds 120", &twPid);
    HWND n1 = nullptr, n2 = nullptr;
    PumpUntil([&] { return (n1 = FindTopLevelByTitle(L"TW-Normal-1")) && (n2 = FindTopLevelByTitle(L"TW-Normal-2")); }, 5000);
    bool reclaimed = n1 && n2 && PumpUntil([&] { return !IsWindowVisible(n1) && !IsWindowVisible(n2); }, 5000);
    PumpFor(1200);
    st = ReadState();
    Check(reclaimed, "4) 같은 앱이 같은 제목의 창을 다시 열면 자동으로 원래 더미에 접힘");
    Check(st.find("\"missing\":[]") != std::string::npos, "4) '다시 열어야 할 창' 목록이 비워짐");

    // 5) "End task" from Task Manager (exit code 1): show the windows, don't come back
    HANDLE ap2 = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, PidOf(MainWnd()));
    TerminateProcess(ap2, 1);
    WaitForSingleObject(ap2, 3000);
    CloseHandle(ap2);
    bool back = n1 && n2 && PumpUntil([&] { return IsWindowVisible(n1) && IsWindowVisible(n2); }, 6000);
    Check(back, "5) 작업 끝내기 → 감시자가 숨긴 창을 복구");
    PumpFor(2000);
    Check(MainWnd() == nullptr, "5) 작업 끝내기 후에는 다시 실행하지 않음");

    if (appProc) CloseHandle(appProc);
    TerminateProcess(twProc, 0);
    CloseHandle(twProc);
    Print("\n%s (%d failed)\n", g_fail ? "LIFECYCLE FAILED" : "LIFECYCLE PASSED", g_fail);
    return g_fail ? 1 : 0;
}
