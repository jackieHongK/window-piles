// Check: an app whose every window is put away steps back (low memory priority, EcoQoS) and gets its own
// settings back when one of its windows is out again, and on ReleaseAll; an app at work (CPU, memory
// growing) is left as it is. Uses TestWindows.exe next to it, and itself as a windowless busy/idle app.
// Usage: background_check   (exit code 0 = pass)
#include <windows.h>
#include <objbase.h>

#include <cstdio>
#include <string>
#include <vector>

#include "app/background.h"

using namespace wp;

static int failures = 0;
#define CHECK(c)                                          \
    do {                                                  \
        if (!(c)) {                                       \
            std::printf("FAIL line %d: %s\n", __LINE__, #c); \
            ++failures;                                   \
        }                                                 \
    } while (0)

static ULONG MemoryOf(HANDLE p) {
    MEMORY_PRIORITY_INFORMATION m{};
    GetProcessInformation(p, ProcessMemoryPriority, &m, sizeof(m));
    return m.MemoryPriority;
}

static bool EcoOf(HANDLE p) {
    PROCESS_POWER_THROTTLING_STATE s{PROCESS_POWER_THROTTLING_CURRENT_VERSION};
    if (!GetProcessInformation(p, ProcessPowerThrottling, &s, sizeof(s))) return false;
    return (s.ControlMask & s.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) != 0;
}

// A windowless helper: --spin burns a core, --grow takes 64 MB more memory every second, --idle sleeps.
static int Helper(const wchar_t* mode) {
    ULONGLONG end = GetTickCount64() + 30000;
    std::vector<char*> blocks;
    while (GetTickCount64() < end) {
        if (!wcscmp(mode, L"--spin")) continue;
        if (!wcscmp(mode, L"--grow")) {
            char* b = (char*)VirtualAlloc(nullptr, 64 << 20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (b) memset(b, 1, 64 << 20), blocks.push_back(b);
        }
        Sleep(1000);
    }
    return 0;
}

static PROCESS_INFORMATION StartHelper(const std::wstring& exe, const wchar_t* mode) {
    std::wstring cmd = L"\"" + exe + L"\" " + mode;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    return pi;
}

int wmain(int argc, wchar_t** argv) {
    if (argc > 1) return Helper(argv[1]);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir.resize(dir.find_last_of(L'\\') + 1);
    std::wstring cmd = L"\"" + dir + L"TestWindows.exe\" --normal 2 --seconds 60";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        std::printf("FAIL: TestWindows.exe not started\n");
        return 1;
    }
    HWND w[2] = {};
    for (int i = 0; i < 100 && !(w[0] && w[1]); ++i) {
        Sleep(50);
        w[0] = FindWindowW(nullptr, L"TW-Normal-1");
        w[1] = FindWindowW(nullptr, L"TW-Normal-2");
    }
    CHECK(w[0] && w[1]);
    DWORD pid = pi.dwProcessId;
    ULONG before = MemoryOf(pi.hProcess);
    std::printf("before: memory %lu, eco %d\n", before, EcoOf(pi.hProcess));

    BackgroundApps bg;
    // One window put away, the other still out: in use, stays as it is.
    ShowWindow(w[0], SW_HIDE);
    bg.Update({pid});
    CHECK(!bg.Has(pid));
    CHECK(MemoryOf(pi.hProcess) == before);

    // Every window put away: measured first (it might be at work), then it steps back.
    ShowWindow(w[1], SW_HIDE);
    bg.Update({pid});
    CHECK(!bg.Has(pid));
    Sleep(2100);
    bg.Update({pid});
    CHECK(bg.Has(pid));
    CHECK(MemoryOf(pi.hProcess) == MEMORY_PRIORITY_LOW);
    CHECK(EcoOf(pi.hProcess));
    std::printf("hidden: memory %lu, eco %d\n", MemoryOf(pi.hProcess), EcoOf(pi.hProcess));

    // A window out again: its own settings back.
    ShowWindow(w[1], SW_SHOWNA);
    bg.Update({pid});
    CHECK(!bg.Has(pid));
    CHECK(MemoryOf(pi.hProcess) == before);
    CHECK(!EcoOf(pi.hProcess));

    // Minimized counts as in use (it is on the taskbar).
    ShowWindow(w[1], SW_SHOWMINNOACTIVE);
    bg.Update({pid});
    CHECK(!bg.Has(pid));

    // Put away again, then this app exits: given back.
    ShowWindow(w[1], SW_HIDE);
    bg.Update({pid});
    CHECK(bg.Has(pid));
    bg.ReleaseAll();
    CHECK(!bg.Has(pid));
    CHECK(MemoryOf(pi.hProcess) == before);
    CHECK(!EcoOf(pi.hProcess));

    // Nothing hidden: nothing to do.
    bg.Update({});
    CHECK(!bg.Has(pid));

    // Windowless apps "put away": the idle one steps back, the ones at work don't.
    PROCESS_INFORMATION idle = StartHelper(exe, L"--idle"), spin = StartHelper(exe, L"--spin"),
                        grow = StartHelper(exe, L"--grow");
    std::vector<DWORD> helpers{idle.dwProcessId, spin.dwProcessId, grow.dwProcessId};
    Sleep(300);
    bg.Update(helpers);
    Sleep(2100);
    bg.Update(helpers);
    CHECK(bg.Has(idle.dwProcessId));
    CHECK(!bg.Has(spin.dwProcessId));
    CHECK(!bg.Has(grow.dwProcessId));
    std::printf("helpers: idle %d, spin %d, grow %d\n", bg.Has(idle.dwProcessId), bg.Has(spin.dwProcessId),
                bg.Has(grow.dwProcessId));
    bg.ReleaseAll();
    for (const PROCESS_INFORMATION& h : {idle, spin, grow}) {
        TerminateProcess(h.hProcess, 0);
        CloseHandle(h.hProcess);
        CloseHandle(h.hThread);
    }

    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    std::printf(failures ? "%d FAILED\n" : "PASS\n", failures);
    return failures ? 1 : 0;
}
