#include "win_util.h"

#include <shellscalingapi.h>
#include <shlobj.h>

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

std::wstring ProcessExeName(DWORD pid) {
    std::wstring name;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return name;
    wchar_t buf[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(p, 0, buf, &size)) {
        name = buf;
        size_t slash = name.find_last_of(L'\\');
        if (slash != std::wstring::npos) name = name.substr(slash + 1);
    }
    CloseHandle(p);
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

std::wstring AppDataDir() {
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
