// Shared helpers for the P0 proof-of-concept programs.
// Kept dependency-free (Win32 only) so the same code can move into src/common later.
#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace poc {

// ---- process / console ------------------------------------------------------
void InitProcess();  // per-monitor-v2 DPI awareness + UTF-8 console output
std::string Utf8(const std::wstring& s);
std::wstring Widen(const std::string& s);
void Print(const char* fmt, ...);        // printf to stdout (UTF-8) + flush
void AppendFile(const std::wstring& path, const std::string& text);
std::wstring LocalAppDataDir(const wchar_t* sub);  // creates %LOCALAPPDATA%\<sub>
std::wstring OsVersionString();         // e.g. "Windows 11 Pro 24H2 (build 26200.1234)"
DWORD OsBuild();

// ---- timing / memory --------------------------------------------------------
double NowMs();
size_t WorkingSetBytes();
size_t PrivateBytes();
double ProcessCpuMs();  // user+kernel time of this process

// Pumps this thread's messages for `ms` milliseconds (hook callbacks need this).
void PumpFor(int ms);
// Pumps until pred() is true or timeout; returns pred() result.
bool PumpUntil(const std::function<bool()>& pred, int timeoutMs);

// ---- windows ----------------------------------------------------------------
struct WindowInfo {
    HWND hwnd = nullptr;
    DWORD pid = 0;
    DWORD tid = 0;
    std::wstring title;
    std::wstring className;
    std::wstring exeName;
};
WindowInfo Describe(HWND hwnd);
std::wstring WindowTitle(HWND hwnd);
std::wstring WindowClass(HWND hwnd);
std::wstring ProcessExeName(DWORD pid);
uint64_t ProcessCreateTime(DWORD pid);  // FILETIME as uint64, 0 if unknown
std::vector<HWND> FindTopLevelByTitlePrefix(const std::wstring& prefix);
HWND FindTopLevelByTitle(const std::wstring& title);
// TestWindows titles are "TW-<Kind>-<index>"; returns index or -1.
int HarnessIndexFromTitle(const std::wstring& title);
// Distinct, saturated color for harness window #index (left half of its client).
COLORREF HarnessColor(int index);

// ---- screen sampling (reads the DWM-composed screen through GDI) ------------
struct Sample {
    double bgFraction = 0;  // fraction of pixels within tolerance of `bg`
    COLORREF avg = 0;       // average color
    double luminance = 0;   // average luma 0..255
};
Sample SampleScreen(const RECT& screenRect, COLORREF bg, int tol = 40);
int ColorDistance(COLORREF a, COLORREF b);  // max channel delta

// ---- formatting -------------------------------------------------------------
std::string Hex(uint64_t v);
std::string RectStr(const RECT& r);

}  // namespace poc
