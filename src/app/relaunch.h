// Bringing a window back after its app is gone (closed, crashed, a Windows Update reboot): what exactly
// to start so that window — not just "the app" — comes back, remembered while the window was there.
//   - Browsers set a relaunch command on each window (with its profile): that, plus "new window", since
//     starting a browser whose profile already has a window would only bring that window forward.
//   - Store apps (new Outlook, Claude, …) can't be started from their exe: they are activated by their
//     app id, read from the process or, for entries saved before this existed, from the package manifest.
//   - File Explorer: the folder the window showed. Other apps: their command line (with the document
//     they were started with), unless it is a COM/automation start that would open nothing.
#pragma once

#include <windows.h>

#include <string>

namespace wp {

struct LaunchInfo {
    std::wstring command;  // full command line, empty: just the exe
    std::wstring aumid;    // packaged app id (AppUserModelID), empty for ordinary desktop apps
};

// Read from a window that is still there (cheap: a property store, the process's command line, and for
// File Explorer one pass over the shell windows).
LaunchInfo CaptureLaunch(HWND hwnd, DWORD pid, const std::wstring& exePath);

// Starts it. Returns false if nothing could be started.
bool LaunchWindow(const std::wstring& exePath, const LaunchInfo& info);

// A browser opens one window per start (with "new window"): a pile with two of its windows starts it twice.
bool IsBrowser(const std::wstring& exePath);

// A Store app's id from its exe path (…\WindowsApps\<package>\…): read from the manifest of the package as
// installed now (the version folder in a saved path is gone once the app has updated).
std::wstring PackagedAppId(const std::wstring& exePath);

// Which app an exe path is, for "same app?" checks: the lower-case path, except for Store apps, whose
// path changes with every update — those are their package family plus the exe inside the package.
std::wstring AppKey(const std::wstring& exePath);

}  // namespace wp
