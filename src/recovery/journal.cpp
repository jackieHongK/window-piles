#include "journal.h"

#include <atomic>
#include <cctype>
#include <cstdio>

#include "common/win_util.h"

namespace wp {

namespace {

constexpr int kSchema = 1;

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
        const JournalEntry& e = j.entries[i];
        const RECT& r = e.wp.rcNormalPosition;
        char buf[640];
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

// Minimal reader for the journal's own fixed shape.
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
            long long v = 0;
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
                JournalEntry e;
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

}  // namespace

std::wstring JournalPath() { return AppDataDir() + L"\\recovery.json"; }

bool WriteJournal(const Journal& j) { return WriteFileReplacing(JournalPath(), Serialize(j), false); }

bool ReadJournal(Journal& j, std::string& err) {
    HANDLE f = CreateFileW(JournalPath().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return err = "no journal", false;
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    if (size.QuadPart > (1 << 22)) {
        CloseHandle(f);
        return err = "journal too large", false;
    }
    std::string text((size_t)size.QuadPart, '\0');
    DWORD read = 0;
    ReadFile(f, text.data(), (DWORD)text.size(), &read, nullptr);
    CloseHandle(f);
    text.resize(read);
    return Parse(text, j, err);
}

std::string ValidateIdentity(const JournalEntry& e) {
    HWND h = (HWND)(uintptr_t)e.hwnd;
    if (!IsWindow(h)) return "window gone";
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != e.pid) return "pid mismatch (hwnd reused)";
    if (ProcessCreateTime(pid) != e.createTime) return "process start time mismatch (pid reused)";
    if (WindowClass(h) != e.className) return "class mismatch";
    return {};
}

namespace {

std::atomic<int> g_pendingRestores{0};

struct RestoreJob {
    HWND hwnd;
    WINDOWPLACEMENT wp;
};

void CALLBACK RunRestore(PTP_CALLBACK_INSTANCE, void* param) {
    auto* job = (RestoreJob*)param;
    SetWindowPlacement(job->hwnd, &job->wp);
    if (!IsWindowVisible(job->hwnd)) ShowWindowAsync(job->hwnd, SW_SHOWNA);
    delete job;
    --g_pendingRestores;
}

}  // namespace

void RestoreWindow(HWND hwnd, const WINDOWPLACEMENT& saved, bool activate) {
    if (IsHungAppWindow(hwnd)) {
        // Never block on a hung app: async show only.
        ShowWindowAsync(hwnd, activate ? SW_SHOW : SW_SHOWNA);
        return;
    }
    WINDOWPLACEMENT wp = saved;
    wp.length = sizeof(wp);
    if (wp.showCmd == SW_SHOWMINIMIZED || wp.showCmd == SW_MINIMIZE) wp.showCmd = SW_SHOWMINNOACTIVE;
    else if (wp.showCmd != SW_SHOWMAXIMIZED) wp.showCmd = activate ? SW_SHOWNORMAL : SW_SHOWNOACTIVATE;
    // SetWindowPlacement on another process's window is a synchronous round trip. If that app is
    // frozen (suspended in Modern Standby, busy at logoff) but not yet flagged as hung, the call can
    // block for a long time — it once froze the whole app until Windows killed it. Run it on a pool
    // thread so the caller never waits; WaitForRestores() bounds the wait at exit.
    auto* job = new RestoreJob{hwnd, wp};
    ++g_pendingRestores;
    if (!TrySubmitThreadpoolCallback(RunRestore, job, nullptr)) {
        delete job;
        --g_pendingRestores;
        ShowWindowAsync(hwnd, SW_SHOWNA);
    }
}

bool WaitForRestores(DWORD timeoutMs) {
    DWORD start = GetTickCount();
    while (g_pendingRestores > 0) {
        if (GetTickCount() - start > timeoutMs) return false;
        Sleep(10);
    }
    return true;
}

int RecoverFromJournal(const Journal& j, int* skipped) {
    int restored = 0, skip = 0;
    for (const JournalEntry& e : j.entries) {
        if (!ValidateIdentity(e).empty()) {
            ++skip;
            continue;
        }
        HWND h = (HWND)(uintptr_t)e.hwnd;
        if (!IsWindowVisible(h)) RestoreWindow(h, e.wp, false);
        ++restored;
    }
    // Last resort for any window whose app is too frozen to answer in time: queue a plain show.
    if (!WaitForRestores(3000))
        for (const JournalEntry& e : j.entries)
            if (ValidateIdentity(e).empty()) ShowWindowAsync((HWND)(uintptr_t)e.hwnd, SW_SHOWNA);
    if (skipped) *skipped = skip;
    return restored;
}

namespace {

// A crash (access violation, stack overflow…) or a hang kill — not "End task" (exit code 1) or a
// normal exit. Only then is bringing the app straight back what the user wants.
bool LooksLikeCrash(DWORD code) { return code >= 0xC0000000u || code == 0xCFFFFFFFu; }

bool RelaunchManager() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --autostart --restarted";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    // Ready = the new manager has written its own journal (it claims the hidden windows first).
    DWORD newPid = pi.dwProcessId;
    bool ready = false;
    for (int i = 0; i < 50 && !ready; ++i) {
        if (WaitForSingleObject(pi.hProcess, 200) == WAIT_OBJECT_0) break;  // died again
        Journal j;
        std::string err;
        ready = ReadJournal(j, err) && j.managerPid == newPid;
    }
    CloseHandle(pi.hProcess);
    return ready;
}

}  // namespace

int RunWatchdog(DWORD managerPid) {
    HANDLE mgr = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, managerPid);
    if (!mgr) return 1;
    WaitForSingleObject(mgr, INFINITE);  // zero CPU until the manager exits
    DWORD code = 0;
    GetExitCodeProcess(mgr, &code);
    FILETIME created{}, exited{}, k{}, u{};
    GetProcessTimes(mgr, &created, &exited, &k, &u);
    CloseHandle(mgr);
    auto ticks = [](FILETIME f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; };
    double uptimeSec = (double)(ticks(exited) - ticks(created)) / 1e7;

    Journal j;
    std::string err;
    bool read = ReadJournal(j, err);
    Log("watchdog: manager %lu exited code=0x%08lX uptime=%.0fs journal=%s clean=%d",
        managerPid, code, uptimeSec, read ? "ok" : err.c_str(), read ? j.clean : -1);
    if (!read || j.clean || j.managerPid != managerPid) return 0;

    // The app crashed or hung: start it again so its piles come back folded exactly as they were
    // (the new instance re-claims the hidden windows from piles.json). If it ran only briefly
    // (a crash loop) or can't come up, fall back to showing every hidden window.
    if (LooksLikeCrash(code) && uptimeSec > 20 && RelaunchManager()) return 0;

    RecoverFromJournal(j, nullptr);
    j.entries.clear();
    j.clean = true;
    WriteJournal(j);
    return 0;
}

}  // namespace wp
