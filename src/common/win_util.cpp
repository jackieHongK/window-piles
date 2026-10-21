#include "win_util.h"

#include <shellscalingapi.h>
#include <shlobj.h>

#include <cstdarg>
#include <cstdio>

namespace wp {

std::string Utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

double NowMs() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)freq.QuadPart;
}

std::wstring WindowTitle(HWND hwnd) {
    wchar_t buf[512] = {};
    GetWindowTextW(hwnd, buf, 512);  // reads the cached caption; does not block on hung apps
    return buf;
}

std::wstring WindowClass(HWND hwnd) {
    wchar_t buf[256] = {};
    GetClassNameW(hwnd, buf, 256);
    return buf;
}

std::wstring ProcessExePath(DWORD pid) {
    std::wstring path;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return path;
    wchar_t buf[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(p, 0, buf, &size)) path = buf;
    CloseHandle(p);
    return path;
}

std::wstring ProcessExeName(DWORD pid) {
    std::wstring name = ProcessExePath(pid);
    size_t slash = name.find_last_of(L'\\');
    if (slash != std::wstring::npos) name = name.substr(slash + 1);
    return name;
}

uint64_t ProcessCreateTime(DWORD pid) {
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return 0;
    FILETIME c{}, e{}, k{}, u{};
    uint64_t t = 0;
    if (GetProcessTimes(p, &c, &e, &k, &u)) t = ((uint64_t)c.dwHighDateTime << 32) | c.dwLowDateTime;
    CloseHandle(p);
    return t;
}

bool UsingTestDataDir() {
    wchar_t buf[8];
    return GetEnvironmentVariableW(L"WINDOWPILES_DATA_DIR", buf, 8) > 0;
}

std::wstring AppDataDir() {
    // Tests point the app at their own folder so they never read or overwrite the user's piles.
    wchar_t over[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"WINDOWPILES_DATA_DIR", over, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        CreateDirectoryW(over, nullptr);
        return over;
    }
    PWSTR base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) {
        dir = base;
        CoTaskMemFree(base);
    }
    dir += L"\\WindowPiles";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

namespace {
bool WriteWhole(const std::wstring& path, const std::string& data, bool flush) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size();
    if (ok && flush) ok = FlushFileBuffers(f) != FALSE;
    CloseHandle(f);
    return ok;
}
}  // namespace

void Log(const char* fmt, ...) {
    static std::wstring path = AppDataDir() + L"\\app.log";
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[1200];
    int n = snprintf(line, sizeof(line), "%04d-%02d-%02d %02d:%02d:%02d.%03d [%lu] %s\r\n", t.wYear, t.wMonth, t.wDay,
                     t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentProcessId(), msg);
    HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(f, &size) && size.QuadPart > 256 * 1024) {  // keep it small: start over
        CloseHandle(f);
        f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return;
    }
    DWORD written = 0;
    WriteFile(f, line, (DWORD)n, &written, nullptr);
    CloseHandle(f);
}

bool WriteFileReplacing(const std::wstring& path, const std::string& data, bool flush) {
    std::wstring tmp = path + L".tmp";
    if (WriteWhole(tmp, data, flush)) {
        DWORD flags = MOVEFILE_REPLACE_EXISTING | (flush ? MOVEFILE_WRITE_THROUGH : 0);
        for (int attempt = 0; attempt < 6; ++attempt) {
            if (MoveFileExW(tmp.c_str(), path.c_str(), flags)) return true;
            Log("rename %s failed (attempt %d): error %lu", Utf8(path).c_str(), attempt + 1, GetLastError());
            Sleep(15 * (attempt + 1));
        }
    } else {
        Log("write %s.tmp failed: error %lu", Utf8(path).c_str(), GetLastError());
    }
    // Rename keeps failing: a direct write is less atomic but far better than losing the data.
    bool ok = false;
    for (int attempt = 0; attempt < 3 && !ok; ++attempt) {
        ok = WriteWhole(path, data, flush);
        if (!ok) Sleep(20);
    }
    DeleteFileW(tmp.c_str());
    return ok;
}

UINT DpiForPoint(POINT pt) {
    UINT x = 96, y = 96;
    if (FAILED(GetDpiForMonitor(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &x, &y)))
        return GetDpiForSystem();
    return x;
}

RECT WorkAreaForPoint(POINT pt) {
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    return mi.rcWork;
}

RECT WorkAreaForRect(const RECT& r) {
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi);
    return mi.rcWork;
}

bool IsWindows11() {
    wchar_t buf[32] = {};
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber",
                     RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
        return false;
    return _wtoi(buf) >= 22000;
}

}  // namespace wp
