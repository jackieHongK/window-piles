// PoC-04 — recovery journal + watchdog process that restores hidden windows after a manager crash.
//
//   poc_watchdog selftest [--prefix TW-Normal-]   end-to-end: spawn manager, kill it, verify recovery
//   poc_watchdog manager  [--prefix ..] [--hold-sec N] [--clean]
//                         spawns the watchdog, journals + hides windows, then waits (or exits cleanly)
//   poc_watchdog watchdog <managerPid>            waits on the manager process handle (0 CPU)
//   poc_watchdog recover                          next-start recovery when both processes died
//   poc_watchdog journalbench                     journal write cost with and without FlushFileBuffers
//
// Journal: %LOCALAPPDATA%\WindowPilesPoC\recovery.json, written as tmp file + MoveFileEx(REPLACE),
// with schema version and an FNV-1a checksum over everything before the "checksum" field.
// Every entry is re-validated before restoring (IsWindow, PID, process creation time, class name),
// so a reused HWND/PID never receives a show command meant for another app.
#include "poc_common.h"

#include <psapi.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace poc;

namespace {

constexpr int kSchema = 1;

struct Entry {
    uint64_t hwnd = 0;
    DWORD pid = 0;
    uint64_t createTime = 0;
    std::wstring className;
    WINDOWPLACEMENT wp{sizeof(WINDOWPLACEMENT)};
    std::string note;  // test annotation only
};

struct Journal {
    DWORD managerPid = 0;
    bool clean = false;
    std::vector<Entry> entries;
};

std::wstring JournalPath() { return LocalAppDataDir(L"WindowPilesPoC") + L"\\recovery.json"; }
std::wstring ResultPath() { return LocalAppDataDir(L"WindowPilesPoC") + L"\\watchdog-result.txt"; }

uint64_t Fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
    return h;
}

std::string JsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += c;
        else if ((unsigned char)c < 0x20) {
            char b[8];
            snprintf(b, sizeof(b), "\\u%04x", c);
            o += b;
        } else o += c;
    }
    return o;
}

std::string Serialize(const Journal& j) {
    std::string s = "{\"schema\":" + std::to_string(kSchema) + ",\"managerPid\":" + std::to_string(j.managerPid) +
                    ",\"clean\":" + (j.clean ? "true" : "false") + ",\"entries\":[";
    for (size_t i = 0; i < j.entries.size(); ++i) {
        const Entry& e = j.entries[i];
        const RECT& r = e.wp.rcNormalPosition;
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "%s{\"hwnd\":%llu,\"pid\":%lu,\"createTime\":%llu,\"class\":\"%s\",\"showCmd\":%u,\"flags\":%u,"
                 "\"normal\":[%ld,%ld,%ld,%ld],\"minPos\":[%ld,%ld],\"maxPos\":[%ld,%ld]}",
                 i ? "," : "", (unsigned long long)e.hwnd, e.pid, (unsigned long long)e.createTime,
                 JsonEscape(Utf8(e.className)).c_str(), e.wp.showCmd, e.wp.flags, r.left, r.top, r.right, r.bottom,
                 e.wp.ptMinPosition.x, e.wp.ptMinPosition.y, e.wp.ptMaxPosition.x, e.wp.ptMaxPosition.y);
        s += buf;
    }
    s += "]";
    char cs[64];
    snprintf(cs, sizeof(cs), ",\"checksum\":\"%016llx\"}", (unsigned long long)Fnv1a(s));
    return s + cs;
}

// ---- minimal parser for the journal's own fixed shape (no general JSON needed) -------
struct Cursor {
    const std::string& s;
    size_t p = 0;
    void ws() {
        while (p < s.size() && isspace((unsigned char)s[p])) ++p;
    }
    bool eat(char c) {
        ws();
        if (p < s.size() && s[p] == c) {
            ++p;
            return true;
        }
        return false;
    }
    bool str(std::string& out) {
        ws();
        if (p >= s.size() || s[p] != '"') return false;
        ++p;
        out.clear();
        while (p < s.size() && s[p] != '"') {
            if (s[p] == '\\' && p + 1 < s.size()) {
                ++p;
                if (s[p] == 'u' && p + 4 < s.size()) {
                    out += (char)strtol(s.substr(p + 1, 4).c_str(), nullptr, 16);
                    p += 4;
                } else out += s[p];
            } else out += s[p];
            ++p;
        }
        return eat('"');
    }
    bool num(long long& v) {
        ws();
        char* end = nullptr;
        v = strtoll(s.c_str() + p, &end, 10);
        if (end == s.c_str() + p) return false;
        p = end - s.c_str();
        return true;
    }
    bool unum(unsigned long long& v) {
        ws();
        char* end = nullptr;
        v = strtoull(s.c_str() + p, &end, 10);
        if (end == s.c_str() + p) return false;
        p = end - s.c_str();
        return true;
    }
    bool boolean(bool& v) {
        ws();
        if (s.compare(p, 4, "true") == 0) return v = true, p += 4, true;
        if (s.compare(p, 5, "false") == 0) return v = false, p += 5, true;
        return false;
    }
    bool nums(long long* out, int n) {
        if (!eat('[')) return false;
        for (int i = 0; i < n; ++i) {
            if (i && !eat(',')) return false;
            if (!num(out[i])) return false;
        }
        return eat(']');
    }
};

bool Parse(const std::string& text, Journal& j, std::string& err) {
    size_t cpos = text.rfind(",\"checksum\":\"");
    if (cpos == std::string::npos) return err = "checksum missing", false;
    unsigned long long stored = strtoull(text.c_str() + cpos + 13, nullptr, 16);
    if (stored != Fnv1a(text.substr(0, cpos))) return err = "checksum mismatch", false;

    Cursor c{text};
    if (!c.eat('{')) return err = "not an object", false;
    std::string key;
    while (c.str(key)) {
        if (!c.eat(':')) return err = "bad key", false;
        if (key == "schema") {
            long long v;
            if (!c.num(v) || v != kSchema) return err = "schema mismatch", false;
        } else if (key == "managerPid") {
            long long v;
            c.num(v);
            j.managerPid = (DWORD)v;
        } else if (key == "clean") {
            c.boolean(j.clean);
        } else if (key == "checksum") {
            std::string ignored;
            c.str(ignored);
        } else if (key == "entries") {
            if (!c.eat('[')) return err = "entries", false;
            while (c.eat('{')) {
                Entry e;
                std::string k;
                while (c.str(k)) {
                    c.eat(':');
                    long long a[4]{};
                    unsigned long long u = 0;
                    if (k == "hwnd") c.unum(u), e.hwnd = u;
                    else if (k == "pid") c.unum(u), e.pid = (DWORD)u;
                    else if (k == "createTime") c.unum(u), e.createTime = u;
                    else if (k == "class") {
                        std::string v;
                        c.str(v);
                        e.className = Widen(v);
                    } else if (k == "showCmd") c.unum(u), e.wp.showCmd = (UINT)u;
                    else if (k == "flags") c.unum(u), e.wp.flags = (UINT)u;
                    else if (k == "normal") {
                        c.nums(a, 4);
                        e.wp.rcNormalPosition = {(LONG)a[0], (LONG)a[1], (LONG)a[2], (LONG)a[3]};
                    } else if (k == "minPos") {
                        c.nums(a, 2);
                        e.wp.ptMinPosition = {(LONG)a[0], (LONG)a[1]};
                    } else if (k == "maxPos") {
                        c.nums(a, 2);
                        e.wp.ptMaxPosition = {(LONG)a[0], (LONG)a[1]};
                    } else return err = "unknown entry key " + k, false;
                    c.eat(',');
                }
                if (!c.eat('}')) return err = "entry end", false;
                j.entries.push_back(e);
                c.eat(',');
            }
            if (!c.eat(']')) return err = "entries end", false;
        } else return err = "unknown key " + key, false;
        c.eat(',');
    }
    return true;
}

// Atomic replace: write tmp, (optionally flush), rename over the old file.
bool WriteJournal(const Journal& j, bool flush = false) {
    std::wstring path = JournalPath(), tmp = path + L".tmp";
    std::string text = Serialize(j);
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, text.data(), (DWORD)text.size(), &written, nullptr) && written == text.size();
    if (ok && flush) ok = FlushFileBuffers(f) != FALSE;
    CloseHandle(f);
    if (!ok) return false;
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | (flush ? MOVEFILE_WRITE_THROUGH : 0)) !=
           FALSE;
}

bool ReadJournal(Journal& j, std::string& err) {
    HANDLE f = CreateFileW(JournalPath().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return err = "no journal", false;
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    std::string text((size_t)size.QuadPart, '\0');
    DWORD read = 0;
    ReadFile(f, text.data(), (DWORD)text.size(), &read, nullptr);
    CloseHandle(f);
    text.resize(read);
    return Parse(text, j, err);
}

// ---- recovery (shared by watchdog and next-start recovery) ---------------------------
std::string ValidateIdentity(const Entry& e) {
    HWND h = (HWND)(uintptr_t)e.hwnd;
    if (!IsWindow(h)) return "창이 더 이상 없음";
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != e.pid) return "PID 불일치(HWND 재사용)";
    if (ProcessCreateTime(pid) != e.createTime) return "프로세스 생성 시각 불일치(PID 재사용)";
    if (WindowClass(h) != e.className) return "클래스명 불일치";
    return {};
}

std::string Recover(const Journal& j, int& restored, int& skipped) {
    std::string log;
    restored = skipped = 0;
    for (const Entry& e : j.entries) {
        HWND h = (HWND)(uintptr_t)e.hwnd;
        std::string why = ValidateIdentity(e);
        if (!why.empty()) {
            ++skipped;
            log += "SKIP " + Hex(e.hwnd) + " : " + why + "\n";
            continue;
        }
        if (IsHungAppWindow(h)) {
            // Never block on a hung app: async show only, placement later.
            ShowWindowAsync(h, SW_SHOWNA);
        } else {
            WINDOWPLACEMENT wp = e.wp;
            if (wp.showCmd == SW_SHOWNORMAL || wp.showCmd == SW_SHOW) wp.showCmd = SW_SHOWNOACTIVATE;
            if (wp.showCmd == SW_SHOWMINIMIZED) wp.showCmd = SW_SHOWMINNOACTIVE;
            ShowWindowAsync(h, SW_SHOWNA);
            SetWindowPlacement(h, &wp);
        }
        ++restored;
        log += "RESTORE " + Hex(e.hwnd) + " " + Utf8(WindowTitle(h)) + "\n";
    }
    return log;
}

// ---- modes ------------------------------------------------------------------------------
int RunWatchdog(DWORD managerPid) {
    HANDLE mgr = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, managerPid);
    if (!mgr) return 1;
    WaitForSingleObject(mgr, INFINITE);  // zero CPU until the manager exits
    double t0 = NowMs();
    DWORD code = 0;
    GetExitCodeProcess(mgr, &code);
    CloseHandle(mgr);

    Journal j;
    std::string err;
    std::string out = "managerExitCode=" + std::to_string(code) + "\n";
    if (!ReadJournal(j, err)) {
        out += "journal: " + err + "\n";
    } else if (j.clean) {
        out += "clean shutdown: nothing to do\n";
    } else {
        int restored = 0, skipped = 0;
        out += Recover(j, restored, skipped);
        out += "restored=" + std::to_string(restored) + " skipped=" + std::to_string(skipped) + "\n";
        j.entries.clear();
        j.clean = true;
        WriteJournal(j);
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "recoveryMs=%.1f\n", NowMs() - t0);
    out += buf;
    DeleteFileW(ResultPath().c_str());
    AppendFile(ResultPath(), out);
    return 0;
}

DWORD SpawnSelf(const std::wstring& args, HANDLE* process = nullptr) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" " + args;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return 0;
    CloseHandle(pi.hThread);
    if (process) *process = pi.hProcess;
    else CloseHandle(pi.hProcess);
    return pi.dwProcessId;
}

int RunManager(const std::wstring& prefix, int holdSec, bool clean) {
    SpawnSelf(L"watchdog " + std::to_wstring(GetCurrentProcessId()));

    Journal j;
    j.managerPid = GetCurrentProcessId();
    std::vector<HWND> targets = FindTopLevelByTitlePrefix(prefix);
    for (HWND h : targets) {
        Entry e;
        e.hwnd = (uint64_t)(uintptr_t)h;
        GetWindowThreadProcessId(h, &e.pid);
        e.createTime = ProcessCreateTime(e.pid);
        e.className = WindowClass(h);
        GetWindowPlacement(h, &e.wp);
        j.entries.push_back(e);
    }
    // Decoys that must be rejected by identity validation:
    if (!targets.empty()) {
        Entry wrongTime = j.entries[0];  // real live window, but "a different process instance"
        wrongTime.createTime += 1;
        j.entries.push_back(wrongTime);
    }
    Entry stale;
    stale.hwnd = 0x7FFF1234;  // not a window
    stale.className = L"Nope";
    j.entries.push_back(stale);

    // Journal first, hide second (spec §4.4 step 4 → 6).
    if (!WriteJournal(j)) return 1;
    for (HWND h : targets) ShowWindowAsync(h, SW_HIDE);
    PumpUntil(
        [&] {
            for (HWND h : targets)
                if (IsWindowVisible(h)) return false;
            return true;
        },
        2000);
    Print("HIDDEN %zu\n", targets.size());

    if (clean) {
        int r = 0, s = 0;
        Recover(j, r, s);
        j.entries.clear();
        j.clean = true;
        WriteJournal(j);
        return 0;
    }
    PumpFor(holdSec * 1000);
    return 0;
}

int RunRecover() {
    Journal j;
    std::string err;
    if (!ReadJournal(j, err)) {
        Print("journal: %s\n", err.c_str());
        return 1;
    }
    if (j.clean) {
        Print("clean journal, nothing to recover\n");
        return 0;
    }
    int r = 0, s = 0;
    Print("%s", Recover(j, r, s).c_str());
    j.entries.clear();
    j.clean = true;
    WriteJournal(j);
    return 0;
}

int RunJournalBench(std::string& md) {
    Journal j;
    j.managerPid = GetCurrentProcessId();
    for (int i = 0; i < 30; ++i) {  // 30 windows, as in the spec's performance target
        Entry e;
        e.hwnd = 0x10000 + i;
        e.pid = 1000 + i;
        e.createTime = 133000000000000000ull + i;
        e.className = L"Chrome_WidgetWin_1";
        e.wp.rcNormalPosition = {10 * i, 10 * i, 800 + i, 600 + i};
        j.entries.push_back(e);
    }
    auto bench = [&](bool flush) {
        std::vector<double> t;
        for (int i = 0; i < 50; ++i) {
            double s = NowMs();
            WriteJournal(j, flush);
            t.push_back(NowMs() - s);
        }
        std::sort(t.begin(), t.end());
        double sum = 0;
        for (double v : t) sum += v;
        char buf[160];
        snprintf(buf, sizeof(buf), "| %s | %.2f | %.2f | %.2f |\n", flush ? "FlushFileBuffers + WRITE_THROUGH" : "flush 생략",
                 sum / t.size(), t[t.size() * 95 / 100], t.back());
        return std::string(buf);
    };
    md += "\n### 저널 쓰기 비용 (창 30개, " + std::to_string(Serialize(j).size()) +
          " bytes, 50회)\n\n| 방식 | 평균 ms | P95 ms | 최대 ms |\n|---|---|---|---|\n";
    md += bench(false);
    md += bench(true);
    // Round-trip + corruption checks.
    Journal back;
    std::string err;
    bool ok = ReadJournal(back, err) && back.entries.size() == j.entries.size() &&
              back.entries[5].wp.rcNormalPosition.right == j.entries[5].wp.rcNormalPosition.right;
    md += std::string("\n- 직렬화 왕복: ") + (ok ? "일치" : "불일치 " + err) + "\n";
    std::string text = Serialize(j);
    text[text.size() / 2] ^= 1;
    Journal bad;
    std::string err2;
    md += std::string("- 1바이트 손상 감지: ") + (!Parse(text, bad, err2) ? "감지됨 (" + err2 + ")" : "감지 실패") + "\n";
    DeleteFileW(JournalPath().c_str());
    return 0;
}

int RunSelfTest(const std::wstring& prefix, std::string& md) {
    std::vector<HWND> targets = FindTopLevelByTitlePrefix(prefix);
    if (targets.empty()) {
        md += "- 대상 창 없음 (TestWindows.exe 먼저 실행)\n";
        return 2;
    }
    DeleteFileW(ResultPath().c_str());

    // 1) crash: manager hides windows, then gets TerminateProcess'd (like taskkill /F).
    HANDLE mgr = nullptr;
    SpawnSelf(L"manager --prefix " + prefix + L" --hold-sec 30", &mgr);
    bool hidden = PumpUntil(
        [&] {
            for (HWND h : targets)
                if (IsWindowVisible(h)) return false;
            return true;
        },
        5000);
    PumpFor(300);
    double tKill = NowMs();
    TerminateProcess(mgr, 0xDEAD);
    CloseHandle(mgr);
    bool visibleAgain = PumpUntil(
        [&] {
            for (HWND h : targets)
                if (!IsWindowVisible(h)) return false;
            return true;
        },
        5000);
    double tVisible = NowMs() - tKill;
    PumpUntil([&] { return GetFileAttributesW(ResultPath().c_str()) != INVALID_FILE_ATTRIBUTES; }, 3000);
    PumpFor(200);

    std::string result;
    {
        HANDLE f = CreateFileW(ResultPath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            char buf[4096];
            DWORD n = 0;
            ReadFile(f, buf, sizeof(buf) - 1, &n, nullptr);
            buf[n] = 0;
            result = buf;
            CloseHandle(f);
        }
    }
    char line[256];
    snprintf(line, sizeof(line),
             "\n### 강제 종료 복구 (AC-07)\n\n- 대상 %zu개 숨김: %s\n- 매니저 TerminateProcess 후 모든 창 다시 보임: %s (%.1f ms)\n",
             targets.size(), hidden ? "성공" : "실패", visibleAgain ? "성공" : "실패", tVisible);
    md += line;
    md += "- 감시자 로그:\n```\n" + result + "```\n";

    // 2) clean exit: manager restores and marks the journal clean; watchdog must do nothing.
    DeleteFileW(ResultPath().c_str());
    HANDLE mgr2 = nullptr;
    SpawnSelf(L"manager --prefix " + prefix + L" --clean", &mgr2);
    WaitForSingleObject(mgr2, 10000);
    CloseHandle(mgr2);
    PumpUntil([&] { return GetFileAttributesW(ResultPath().c_str()) != INVALID_FILE_ATTRIBUTES; }, 3000);
    bool allVisible = true;
    for (HWND h : targets) allVisible &= IsWindowVisible(h) != FALSE;
    std::string r2;
    {
        HANDLE f = CreateFileW(ResultPath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            char buf[1024];
            DWORD n = 0;
            ReadFile(f, buf, sizeof(buf) - 1, &n, nullptr);
            buf[n] = 0;
            r2 = buf;
            CloseHandle(f);
        }
    }
    md += std::string("\n### 정상 종료 (AC-13)\n\n- 매니저가 직접 복원 후 저널 clean 표시 → 모든 창 보임: ") +
          (allVisible ? "성공" : "실패") + "\n- 감시자 로그:\n```\n" + r2 + "```\n";

    // 3) both dead: manager + watchdog killed → next start recovers from the journal.
    HANDLE mgr3 = nullptr;
    DWORD mgrPid = SpawnSelf(L"manager --prefix " + prefix + L" --hold-sec 30", &mgr3);
    PumpUntil(
        [&] {
            for (HWND h : targets)
                if (IsWindowVisible(h)) return false;
            return true;
        },
        5000);
    PumpFor(300);
    // Kill the watchdog first (find it by command line is overkill: it's our only child of mgr3's pid).
    // We kill every poc_watchdog process except ourselves.
    {
        DWORD self = GetCurrentProcessId();
        DWORD pids[1024], bytes = 0;
        EnumProcesses(pids, sizeof(pids), &bytes);
        for (DWORD i = 0; i < bytes / sizeof(DWORD); ++i) {
            if (pids[i] == self || pids[i] == mgrPid) continue;
            if (ProcessExeName(pids[i]) == L"poc_watchdog.exe") {
                HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, pids[i]);
                if (p) TerminateProcess(p, 0xDEAD), CloseHandle(p);
            }
        }
    }
    PumpFor(200);
    TerminateProcess(mgr3, 0xDEAD);
    CloseHandle(mgr3);
    PumpFor(1000);
    bool stillHidden = true;
    for (HWND h : targets) stillHidden &= !IsWindowVisible(h);
    double tr = NowMs();
    HANDLE rec = nullptr;
    SpawnSelf(L"recover", &rec);
    WaitForSingleObject(rec, 10000);
    CloseHandle(rec);
    bool recovered = PumpUntil(
        [&] {
            for (HWND h : targets)
                if (!IsWindowVisible(h)) return false;
            return true;
        },
        3000);
    snprintf(line, sizeof(line),
             "\n### 매니저+감시자 모두 강제 종료 → 다음 실행 시 저널 복구\n\n- 둘 다 죽은 직후 창이 숨김 상태로 남음: %s\n"
             "- `recover` 실행 후 모든 창 다시 보임: %s (%.1f ms)\n",
             stillHidden ? "예(예상대로)" : "아니오", recovered ? "성공" : "실패", NowMs() - tr);
    md += line;
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    InitProcess();
    std::wstring mode = argc > 1 ? argv[1] : L"selftest";
    std::wstring prefix = L"TW-Normal-";
    std::wstring outPath;
    int holdSec = 30;
    bool clean = false;
    for (int i = 2; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--prefix" && i + 1 < argc) prefix = argv[++i];
        else if (a == L"--hold-sec" && i + 1 < argc) holdSec = _wtoi(argv[++i]);
        else if (a == L"--clean") clean = true;
        else if (a == L"--out" && i + 1 < argc) outPath = argv[++i];
    }

    if (mode == L"watchdog" && argc > 2) return RunWatchdog((DWORD)_wtoi(argv[2]));
    if (mode == L"manager") return RunManager(prefix, holdSec, clean);
    if (mode == L"recover") return RunRecover();

    std::string md = "## PoC-04 — 복구 저널 + 감시자 프로세스\n\n- OS: " + Utf8(OsVersionString()) + "\n";
    if (mode == L"journalbench" || mode == L"selftest") RunJournalBench(md);
    if (mode == L"selftest") RunSelfTest(prefix, md);
    Print("%s\n", md.c_str());
    if (!outPath.empty()) AppendFile(outPath, md + "\n");
    return 0;
}
