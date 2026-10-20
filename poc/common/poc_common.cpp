#include "poc_common.h"

#include <psapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace poc {

void InitProcess() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetConsoleOutputCP(CP_UTF8);
}

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

void Print(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

void AppendFile(const std::wstring& path, const std::string& text) {
    HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, text.data(), (DWORD)text.size(), &written, nullptr);
    CloseHandle(f);
}

std::wstring LocalAppDataDir(const wchar_t* sub) {
    PWSTR base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) {
        dir = base;
        CoTaskMemFree(base);
    }
    dir += L"\\";
    dir += sub;
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

static std::wstring RegString(const wchar_t* name) {
    wchar_t buf[256] = {};
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", name,
                     RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
        return {};
    return buf;
}

DWORD OsBuild() { return (DWORD)_wtoi(RegString(L"CurrentBuildNumber").c_str()); }

std::wstring OsVersionString() {
    std::wstring product = RegString(L"ProductName");
    DWORD build = OsBuild();
    // ProductName still says "Windows 10" on Windows 11; the build number is authoritative.
    if (build >= 22000) {
        size_t p = product.find(L"Windows 10");
        if (p != std::wstring::npos) product.replace(p, 10, L"Windows 11");
    }
    DWORD ubr = 0, size = sizeof(ubr);
    RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR",
                 RRF_RT_REG_DWORD, nullptr, &ubr, &size);
    return product + L" " + RegString(L"DisplayVersion") + L" (build " + std::to_wstring(build) + L"." +
           std::to_wstring(ubr) + L")";
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

size_t WorkingSetBytes() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
    return pmc.WorkingSetSize;
}

size_t PrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
    return pmc.PrivateUsage;
}

double ProcessCpuMs() {
    FILETIME c, e, k, u;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    auto toMs = [](FILETIME ft) {
        return (double)(((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000.0;
    };
    return toMs(k) + toMs(u);
}

void PumpFor(int ms) {
    PumpUntil([] { return false; }, ms);
}

bool PumpUntil(const std::function<bool()>& pred, int timeoutMs) {
    double end = NowMs() + timeoutMs;
    for (;;) {
        if (pred()) return true;
        double left = end - NowMs();
        if (left <= 0) return pred();
        MsgWaitForMultipleObjects(0, nullptr, FALSE, (DWORD)(left < 15 ? left : 15), QS_ALLINPUT);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

std::wstring WindowTitle(HWND hwnd) {
    wchar_t buf[512] = {};
    // GetWindowText does not block on hung windows of other processes (it reads the cached caption).
    GetWindowTextW(hwnd, buf, 512);
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
    if (!p) return L"<access denied>";
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

WindowInfo Describe(HWND hwnd) {
    WindowInfo w;
    w.hwnd = hwnd;
    w.tid = GetWindowThreadProcessId(hwnd, &w.pid);
    w.title = WindowTitle(hwnd);
    w.className = WindowClass(hwnd);
    w.exeName = ProcessExeName(w.pid);
    return w;
}

std::vector<HWND> FindTopLevelByTitlePrefix(const std::wstring& prefix) {
    struct Ctx {
        const std::wstring* prefix;
        std::vector<HWND> out;
    } ctx{&prefix, {}};
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            auto* c = (Ctx*)lp;
            std::wstring t = WindowTitle(h);
            if (t.rfind(*c->prefix, 0) == 0) c->out.push_back(h);
            return TRUE;
        },
        (LPARAM)&ctx);
    // EnumWindows returns Z-order; sort by harness index so runs are deterministic.
    std::sort(ctx.out.begin(), ctx.out.end(), [](HWND a, HWND b) {
        return HarnessIndexFromTitle(WindowTitle(a)) < HarnessIndexFromTitle(WindowTitle(b));
    });
    return ctx.out;
}

HWND FindTopLevelByTitle(const std::wstring& title) {
    for (HWND h : FindTopLevelByTitlePrefix(title))
        if (WindowTitle(h) == title) return h;
    return nullptr;
}

int HarnessIndexFromTitle(const std::wstring& title) {
    if (title.rfind(L"TW-", 0) != 0) return -1;
    size_t dash = title.find_last_of(L'-');
    if (dash == std::wstring::npos || dash + 1 >= title.size()) return -1;
    return _wtoi(title.c_str() + dash + 1);
}

COLORREF HarnessColor(int index) {
    static const COLORREF palette[] = {
        RGB(230, 40, 40),   RGB(40, 170, 60),  RGB(40, 90, 230),  RGB(240, 170, 20),
        RGB(150, 50, 200),  RGB(20, 180, 190), RGB(230, 100, 160), RGB(120, 120, 30),
    };
    if (index < 0) index = 0;
    return palette[index % (sizeof(palette) / sizeof(palette[0]))];
}

int ColorDistance(COLORREF a, COLORREF b) {
    int dr = abs(GetRValue(a) - GetRValue(b));
    int dg = abs(GetGValue(a) - GetGValue(b));
    int db = abs(GetBValue(a) - GetBValue(b));
    return std::max(dr, std::max(dg, db));
}

Sample SampleScreen(const RECT& r, COLORREF bg, int tol) {
    Sample s;
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return s;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
    GdiFlush();
    const uint8_t* p = (const uint8_t*)bits;
    uint64_t sr = 0, sg = 0, sb = 0, nbg = 0;
    size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; ++i) {
        uint8_t b = p[i * 4 + 0], g = p[i * 4 + 1], rr = p[i * 4 + 2];
        sr += rr;
        sg += g;
        sb += b;
        if (ColorDistance(RGB(rr, g, b), bg) <= tol) ++nbg;
    }
    s.bgFraction = (double)nbg / (double)n;
    s.avg = RGB((int)(sr / n), (int)(sg / n), (int)(sb / n));
    s.luminance = 0.299 * GetRValue(s.avg) + 0.587 * GetGValue(s.avg) + 0.114 * GetBValue(s.avg);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return s;
}

std::string Hex(uint64_t v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)v);
    return buf;
}

std::string RectStr(const RECT& r) {
    char buf[96];
    snprintf(buf, sizeof(buf), "(%ld,%ld %ldx%ld)", r.left, r.top, r.right - r.left, r.bottom - r.top);
    return buf;
}

}  // namespace poc
