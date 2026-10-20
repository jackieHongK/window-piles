#include "journal.h"

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

bool WriteJournal(const Journal& j) {
    std::wstring path = JournalPath(), tmp = path + L".tmp";
    std::string text = Serialize(j);
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, text.data(), (DWORD)text.size(), &written, nullptr) && written == text.size();
    CloseHandle(f);
    if (!ok) return false;
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

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
    SetWindowPlacement(hwnd, &wp);
    if (!IsWindowVisible(hwnd)) ShowWindowAsync(hwnd, SW_SHOWNA);
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
    if (skipped) *skipped = skip;
    return restored;
}

int RunWatchdog(DWORD managerPid) {
    HANDLE mgr = OpenProcess(SYNCHRONIZE, FALSE, managerPid);
    if (!mgr) return 1;
    WaitForSingleObject(mgr, INFINITE);  // zero CPU until the manager exits
    CloseHandle(mgr);

    Journal j;
    std::string err;
    if (!ReadJournal(j, err) || j.clean || j.managerPid != managerPid) return 0;
    RecoverFromJournal(j, nullptr);
    j.entries.clear();
    j.clean = true;
    WriteJournal(j);
    return 0;
}

}  // namespace wp
