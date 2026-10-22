// Notification badges on pile cards (no mouse input). Needs WindowPiles NOT running.
// Uses its own data folder (WINDOWPILES_DATA_DIR), never the user's piles.
//
//   1. a "mail" window (title gets "(3) …") and a "messenger" window (only flashes) are folded
//      into two piles → no badge yet
//   2. both apps get a new message while hidden → card A shows a red "3", card B a red dot
//   3. the mail window comes back on screen     → card A's badge disappears
#include "poc_common.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace poc;

namespace {

int g_fail = 0;
std::wstring g_bin;
UINT g_dpi = 96;
int S(int v) { return MulDiv(v, (int)g_dpi, 96); }

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

std::vector<HWND> Cards() {
    std::vector<HWND> out;
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        if (IsWindowVisible(h) && WindowClass(h) == L"WindowPilesPile") ((std::vector<HWND>*)lp)->push_back(h);
        return TRUE;
    }, (LPARAM)&out);
    std::sort(out.begin(), out.end(), [](HWND a, HWND b) {
        RECT ra, rb;
        GetWindowRect(a, &ra);
        GetWindowRect(b, &rb);
        return ra.top < rb.top;
    });
    return out;
}

// Red pixels at the badge on the card's top-right corner: a small patch left of the center, so it
// lies inside the dot and beside (not on) a white digit.
double BadgeRed(HWND card) {
    RECT r;
    GetWindowRect(card, &r);
    int m = S(12);
    POINT c{r.right - m - S(6), r.top + m + S(6)};
    RECT probe{c.x - S(8), c.y - S(2), c.x - S(5), c.y + S(2)};
    return SampleScreen(probe, RGB(239, 68, 68), 30).bgFraction;
}

}  // namespace

// e2e_badges [path\to\WindowPiles.exe]   (default: the exe next to this test)
int wmain(int argc, wchar_t** argv) {
    InitProcess();
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    g_bin = self;
    g_bin = g_bin.substr(0, g_bin.find_last_of(L'\\'));
    std::wstring appExe = argc > 1 ? std::wstring(argv[1]) : g_bin + L"\\WindowPiles.exe";
    g_dpi = GetDpiForSystem();
    SetEnvironmentVariableW(L"WINDOWPILES_NO_RELAUNCH", L"1");
    SetEnvironmentVariableW(L"WINDOWPILES_DATA_DIR", DataDir().c_str());
    CreateDirectoryW(DataDir().c_str(), nullptr);
    CloseHandle(CreateFileW((DataDir() + L"\\firstrun.done").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr));
    DeleteFileW((DataDir() + L"\\recovery.json").c_str());
    if (FindWindowW(L"WindowPilesMain", nullptr)) {
        Print("WindowPiles is running; quit it first.\n");
        return 2;
    }

    HANDLE tw = Launch(g_bin + L"\\TestWindows.exe", L"--notify --flash --notify-after 7000 --seconds 90");
    HWND mail = nullptr, chat = nullptr;
    PumpUntil([&] { return (mail = FindTopLevelByTitle(L"TW-Notify-1")) && (chat = FindTopLevelByTitle(L"TW-Flash-2")); }, 5000);
    if (!mail || !chat) return 2;
    double t0 = NowMs();

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    char head[256];
    snprintf(head, sizeof(head), "\"x\":%ld,\"y\":%ld", work.right - S(340), work.top + S(80));
    std::string a = std::string("{\"id\":1,\"name\":\"메일\",\"customName\":true,\"color\":11107891,") + head +
                    ",\"folded\":true,\"members\":[" + Member(mail) + "],\"missing\":[]}";
    snprintf(head, sizeof(head), "\"x\":%ld,\"y\":%ld", work.right - S(340), work.top + S(420));
    // green title bar (a red one would look like a badge to the sampler)
    std::string b = std::string("{\"id\":2,\"name\":\"메신저\",\"customName\":true,\"color\":6390596,") + head +
                    ",\"folded\":true,\"members\":[" + Member(chat) + "],\"missing\":[]}";
    std::string state = "{\"schema\":1,\"piles\":[" + a + "," + b + "]}";
    HANDLE f = CreateFileW((DataDir() + L"\\piles.json").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD n;
    WriteFile(f, state.data(), (DWORD)state.size(), &n, nullptr);
    CloseHandle(f);

    HANDLE app = Launch(appExe, L"--autostart");
    bool folded = PumpUntil([&] { return !IsWindowVisible(mail) && !IsWindowVisible(chat); }, 6000);
    PumpFor(600);
    auto cards = Cards();
    Check(folded && cards.size() == 2, "1) 메일·메신저 창을 각각 더미로 접음");
    if (cards.size() != 2) return 1;
    Check(BadgeRed(cards[0]) < 0.2 && BadgeRed(cards[1]) < 0.2, "1) 아직 알림 없음 → 배지 없음");

    // 2) new messages arrive while the windows are hidden
    PumpUntil([&] { return NowMs() - t0 > 7500; }, 9000);
    bool aRed = PumpUntil([&] { return BadgeRed(cards[0]) > 0.6; }, 3000);
    bool bRed = PumpUntil([&] { return BadgeRed(cards[1]) > 0.6; }, 3000);
    Check(aRed, "2) 숨긴 메일 창 제목에 (3) → 더미에 빨간 숫자 배지");
    Check(bRed, "2) 숨긴 메신저 창이 작업 표시줄 깜빡임 요청 → 더미에 빨간 점 배지");
    Check(WindowTitle(mail).rfind(L"(3)", 0) == 0, "2) (확인) 메일 창 제목이 실제로 바뀜");

    // 3) the mail window is back on screen → its badge is cleared
    ShowWindow(mail, SW_SHOWNA);
    bool cleared = PumpUntil([&] { return BadgeRed(cards[0]) < 0.2; }, 3000);
    Check(cleared, "3) 창을 다시 보면 그 더미의 배지가 사라짐");

    PostMessageW(FindWindowW(L"WindowPilesMain", nullptr), WM_ENDSESSION, TRUE, 0);
    WaitForSingleObject(app, 5000);
    CloseHandle(app);
    TerminateProcess(tw, 0);
    CloseHandle(tw);
    Print("\n%s (%d failed)\n", g_fail ? "BADGES FAILED" : "BADGES PASSED", g_fail);
    return g_fail ? 1 : 0;
}
