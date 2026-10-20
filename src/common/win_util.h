// Small Win32 helpers shared by the app modules.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace wp {

std::string Utf8(const std::wstring& s);
std::wstring Widen(const std::string& s);
double NowMs();

std::wstring WindowTitle(HWND hwnd);
std::wstring WindowClass(HWND hwnd);
std::wstring ProcessExeName(DWORD pid);
std::wstring ProcessExePath(DWORD pid);
uint64_t ProcessCreateTime(DWORD pid);  // FILETIME as uint64, 0 if unknown

// %LOCALAPPDATA%\WindowPiles (created on demand)
std::wstring AppDataDir();

// Writes `data` to a temp file and renames it over `path`. Retries the rename (antivirus scanners
// briefly lock freshly written files) and, as a last resort, writes `path` directly.
bool WriteFileReplacing(const std::wstring& path, const std::string& data, bool flush);

inline int Scale(int dip, UINT dpi) { return MulDiv(dip, (int)dpi, 96); }
UINT DpiForPoint(POINT pt);
RECT WorkAreaForPoint(POINT pt);
RECT WorkAreaForRect(const RECT& r);

bool IsWindows11();

}  // namespace wp
