#include "activity_log.h"

#include <algorithm>
#include <cstdio>

#include "common/win_util.h"

namespace wp::activity {

namespace {

const char* const kKinds[] = {"start", "open", "close", "pause", "resume"};

std::wstring OffFlag() { return AppDataDir() + L"\\activity.off"; }

std::wstring FileFor(const SYSTEMTIME& day) {
    wchar_t name[32];
    swprintf_s(name, L"\\%04d-%02d-%02d.tsv", day.wYear, day.wMonth, day.wDay);
    return Folder() + name;
}

SYSTEMTIME LocalFromUtc(uint64_t utc) {
    FILETIME ft{(DWORD)utc, (DWORD)(utc >> 32)};
    SYSTEMTIME st{}, local{};
    FileTimeToSystemTime(&ft, &st);
    SystemTimeToTzSpecificLocalTime(nullptr, &st, &local);
    return local;
}

std::string Clean(const std::wstring& s) {
    std::wstring t = s;
    for (auto& c : t)
        if (c == L'\t' || c == L'\r' || c == L'\n') c = L' ';
    return Utf8(t);
}

}  // namespace

bool Enabled() { return GetFileAttributesW(OffFlag().c_str()) == INVALID_FILE_ATTRIBUTES; }

void SetEnabled(bool on) {
    if (on) {
        DeleteFileW(OffFlag().c_str());
    } else {
        HANDLE f = CreateFileW(OffFlag().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    }
}

std::wstring Folder() {
    std::wstring dir = AppDataDir() + L"\\activity";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

uint64_t NowUtc() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

void Append(Kind kind, int pileId, COLORREF color, const std::wstring& text, uint64_t utc) {
    if (!Enabled()) return;
    SYSTEMTIME t = LocalFromUtc(utc ? utc : NowUtc());
    char line[600];
    int n = snprintf(line, sizeof(line), "%02d:%02d:%02d\t%s\t%d\t%06lX\t%s\r\n", t.wHour, t.wMinute, t.wSecond,
                     kKinds[(int)kind], pileId, (unsigned long)color, Clean(text).c_str());
    if (n <= 0) return;
    n = std::min(n, (int)sizeof(line) - 1);
    HANDLE f = CreateFileW(FileFor(t).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, line, (DWORD)n, &written, nullptr);  // no flush: a lost last line costs little
    CloseHandle(f);
}

SYSTEMTIME Today() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    return AddDays(t, 0);
}

SYSTEMTIME AddDays(const SYSTEMTIME& day, int days) {
    SYSTEMTIME noon{};
    noon.wYear = day.wYear;
    noon.wMonth = day.wMonth;
    noon.wDay = day.wDay;
    noon.wHour = 12;
    FILETIME ft;
    SystemTimeToFileTime(&noon, &ft);
    uint64_t v = (((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime) + (int64_t)days * 864000000000LL;
    ft = {(DWORD)v, (DWORD)(v >> 32)};
    SYSTEMTIME out{};
    FileTimeToSystemTime(&ft, &out);  // calendar arithmetic only: no time zone involved
    out.wHour = out.wMinute = out.wSecond = out.wMilliseconds = 0;
    return out;
}

bool SameDay(const SYSTEMTIME& a, const SYSTEMTIME& b) {
    return a.wYear == b.wYear && a.wMonth == b.wMonth && a.wDay == b.wDay;
}

bool ParseLine(const std::string& s, Event& e) {
    int h = 0, m = 0, sec = 0;
    if (sscanf_s(s.c_str(), "%d:%d:%d", &h, &m, &sec) != 3) return false;
    std::vector<std::string> cols;
    size_t start = 0;
    for (;;) {
        size_t tab = s.find('\t', start);
        cols.push_back(s.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    if (cols.size() < 2) return false;
    int kind = -1;
    for (int k = 0; k < 5; ++k)
        if (cols[1] == kKinds[k]) kind = k;
    if (kind < 0) return false;
    e = {};
    e.sec = h * 3600 + m * 60 + sec;
    e.kind = (Kind)kind;
    if (cols.size() > 2) e.pileId = atoi(cols[2].c_str());
    if (cols.size() > 3) e.color = (COLORREF)strtoul(cols[3].c_str(), nullptr, 16);
    if (cols.size() > 4) e.text = Widen(cols[4]);
    return true;
}

std::vector<Event> ReadDay(const SYSTEMTIME& day) {
    std::vector<Event> out;
    HANDLE f = CreateFileW(FileFor(day).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return out;
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    std::string text((size_t)std::min<LONGLONG>(size.QuadPart, 4 << 20), '\0');
    DWORD read = 0;
    ReadFile(f, text.data(), (DWORD)text.size(), &read, nullptr);
    CloseHandle(f);
    text.resize(read);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        std::string line = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        Event e;
        if (ParseLine(line, e)) out.push_back(std::move(e));
        if (eol == std::string::npos) break;
        pos = eol + 1;
    }
    // Times can be a little out of order (an idle pause is dated back to the last input).
    std::stable_sort(out.begin(), out.end(), [](const Event& a, const Event& b) { return a.sec < b.sec; });
    return out;
}

namespace {

struct State {
    bool open = false;  // a pile is being worked on
    bool running = true;  // not paused (lock, sleep, idle, exit)
    int pileId = 0;
    COLORREF color = 0;
    std::wstring name;
    int since = 0;
};

void Step(State& s, const Event& e, std::vector<Span>* out) {
    auto end = [&](int t) {
        if (out && s.open && s.running && t > s.since) out->push_back({s.pileId, s.color, s.name, s.since, t});
    };
    switch (e.kind) {
        case Kind::Open:
            end(e.sec);
            s.open = true;
            s.running = true;
            s.pileId = e.pileId;
            s.color = e.color;
            s.name = e.text;
            s.since = e.sec;
            break;
        case Kind::Close:
            end(e.sec);
            s.open = false;
            break;
        case Kind::Pause:
            end(e.sec);
            s.running = false;
            break;
        case Kind::Resume:
            if (!s.running) {
                s.running = true;
                s.since = e.sec;
            }
            break;
        case Kind::Start:
            // The app started without having recorded how the previous run ended (power loss): that
            // last stretch has no known end, so it is dropped rather than guessed.
            s.open = false;
            s.running = true;
            break;
    }
}

}  // namespace

std::vector<Span> ComputeDay(const std::vector<Event>& previousDay, const std::vector<Event>& day, int endSec) {
    State s;
    for (const Event& e : previousDay) Step(s, e, nullptr);
    s.since = 0;  // still open at midnight: it continues from 00:00
    std::vector<Span> spans;
    for (const Event& e : day) {
        if (e.sec > endSec) break;
        Step(s, e, &spans);
    }
    Step(s, Event{endSec, Kind::Close}, &spans);
    return spans;
}

std::vector<Total> Totals(const std::vector<Span>& spans) {
    std::vector<Total> out;
    for (const Span& sp : spans) {
        auto it = std::find_if(out.begin(), out.end(), [&](const Total& t) { return t.pileId == sp.pileId; });
        if (it == out.end()) it = out.insert(out.end(), Total{sp.pileId, sp.color, sp.name, 0});
        it->seconds += sp.end - sp.start;
        it->color = sp.color;  // the latest name and color win
        it->name = sp.name;
    }
    std::stable_sort(out.begin(), out.end(), [](const Total& a, const Total& b) { return a.seconds > b.seconds; });
    return out;
}

}  // namespace wp::activity
