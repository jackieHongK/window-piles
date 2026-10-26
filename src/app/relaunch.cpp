#include "relaunch.h"

#include <ole2.h>      // COM/OLE automation (WIN32_LEAN_AND_MEAN leaves it out; exdisp.h needs it)
#include <initguid.h>  // defines the PKEY_* below in this file

#include <appmodel.h>
#include <exdisp.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <winternl.h>

#include <algorithm>
#include <cwctype>
#include <vector>

#include "common/win_util.h"

namespace wp {

namespace {

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L'\\');
    return Lower(slash == std::wstring::npos ? path : path.substr(slash + 1));
}

bool IsChromium(const std::wstring& exe) {
    static const wchar_t* names[] = {L"chrome.exe", L"msedge.exe", L"brave.exe", L"whale.exe",
                                     L"vivaldi.exe", L"opera.exe", L"chromium.exe"};
    std::wstring n = FileNameOf(exe);
    return std::any_of(std::begin(names), std::end(names), [&](const wchar_t* x) { return n == x; });
}

bool IsFirefox(const std::wstring& exe) { return FileNameOf(exe) == L"firefox.exe"; }

bool IsPackagedPath(const std::wstring& exe) { return Lower(exe).find(L"\\windowsapps\\") != std::wstring::npos; }

std::wstring Quoted(const std::wstring& s) { return L"\"" + s + L"\""; }

// The window's own AppUserModel properties (browsers set the relaunch command per profile).
void FromPropertyStore(HWND hwnd, LaunchInfo& info) {
    IPropertyStore* store = nullptr;
    if (FAILED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store))) || !store) return;
    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_RelaunchCommand, &v)) && v.vt == VT_LPWSTR && v.pwszVal)
        info.command = v.pwszVal;
    PropVariantClear(&v);
    if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &v)) && v.vt == VT_LPWSTR && v.pwszVal && info.aumid.empty())
        info.aumid = v.pwszVal;
    PropVariantClear(&v);
    store->Release();
}

// The command line the process was started with (no reading of its memory: the documented info class).
std::wstring ProcessCommandLine(HANDLE process) {
    using Query = NTSTATUS(NTAPI*)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
    static Query query = (Query)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");
    if (!query) return {};
    const PROCESSINFOCLASS kCommandLine = (PROCESSINFOCLASS)60;  // ProcessCommandLineInformation (8.1+)
    ULONG len = 0;
    query(process, kCommandLine, nullptr, 0, &len);
    if (!len || len > 64 * 1024) return {};
    std::vector<BYTE> buf(len);
    if (query(process, kCommandLine, buf.data(), len, &len) < 0) return {};
    auto* us = (UNICODE_STRING*)buf.data();
    return std::wstring(us->Buffer, us->Length / sizeof(wchar_t));
}

// Started by COM or as a background server: launching that again would open no window.
bool UselessCommandLine(const std::wstring& cmd) {
    std::wstring l = Lower(cmd);
    static const wchar_t* marks[] = {L"-embedding", L"/embedding", L"-servername:", L"/automation", L"--type="};
    return std::any_of(std::begin(marks), std::end(marks), [&](const wchar_t* m) { return l.find(m) != std::wstring::npos; });
}

// The arguments after the program in a command line ("" when there are none).
std::wstring ArgsOf(const std::wstring& cmd, std::wstring* program = nullptr) {
    size_t i = 0;
    while (i < cmd.size() && iswspace(cmd[i])) ++i;
    size_t end;
    if (i < cmd.size() && cmd[i] == L'"') {
        end = cmd.find(L'"', i + 1);
        if (end == std::wstring::npos) end = cmd.size();
        if (program) *program = cmd.substr(i + 1, end - i - 1);
        ++end;
    } else {
        end = i;
        while (end < cmd.size() && !iswspace(cmd[end])) ++end;
        if (program) *program = cmd.substr(i, end - i);
    }
    while (end < cmd.size() && iswspace(cmd[end])) ++end;
    return end < cmd.size() ? cmd.substr(end) : std::wstring();
}

// The folder a File Explorer window shows (one pass over the shell's windows).
std::wstring ExplorerFolder(HWND hwnd) {
    std::wstring path;
    IShellWindows* windows = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(ShellWindows), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows))) || !windows)
        return path;
    long count = 0;
    windows->get_Count(&count);
    for (long i = 0; i < count && path.empty(); ++i) {
        VARIANT index;
        VariantInit(&index);
        index.vt = VT_I4;
        index.lVal = i;
        IDispatch* disp = nullptr;
        if (FAILED(windows->Item(index, &disp)) || !disp) continue;
        IWebBrowserApp* app = nullptr;
        if (SUCCEEDED(disp->QueryInterface(IID_PPV_ARGS(&app))) && app) {
            SHANDLE_PTR h = 0;
            if (SUCCEEDED(app->get_HWND(&h)) && (HWND)h == hwnd) {
                BSTR url = nullptr;
                if (SUCCEEDED(app->get_LocationURL(&url)) && url && *url) {
                    wchar_t buf[MAX_PATH * 2];
                    DWORD n = ARRAYSIZE(buf);
                    if (SUCCEEDED(PathCreateFromUrlW(url, buf, &n, 0))) path = buf;
                }
                SysFreeString(url);
            }
            app->Release();
        }
        disp->Release();
    }
    windows->Release();
    return path;
}

// A Store app's exe path: …\WindowsApps\<Name>_<Version>_<Arch>__<PublisherId>\<rel>. Its package family
// ("<Name>_<PublisherId>") and the exe's path inside the package stay the same across app updates; the
// version folder doesn't (an updated app lives in a new folder and the old one is removed).
bool SplitPackagePath(const std::wstring& exe, std::wstring& family, std::wstring& rel, std::wstring& root) {
    std::wstring l = Lower(exe);
    size_t at = l.find(L"\\windowsapps\\");
    if (at == std::wstring::npos) return false;
    size_t start = at + 13, end = exe.find(L'\\', start);
    if (end == std::wstring::npos) return false;
    std::wstring folder = exe.substr(start, end - start);
    size_t name = folder.find(L'_'), pub = folder.rfind(L"__");
    if (name == std::wstring::npos || pub == std::wstring::npos || pub < name) return false;
    family = folder.substr(0, name) + L"_" + folder.substr(pub + 2);
    rel = l.substr(end + 1);
    root = exe.substr(0, end);
    return true;
}

// Where the package of that family is installed now (the newest version), or "".
std::wstring CurrentPackageRoot(const std::wstring& family) {
    UINT32 count = 0, len = 0;
    if (GetPackagesByPackageFamily(family.c_str(), &count, nullptr, &len, nullptr) != ERROR_INSUFFICIENT_BUFFER || !count)
        return {};
    std::vector<PWSTR> names(count);
    std::vector<wchar_t> buf(len);
    if (GetPackagesByPackageFamily(family.c_str(), &count, names.data(), &len, buf.data()) != ERROR_SUCCESS || !count)
        return {};
    UINT32 plen = 0;
    GetPackagePathByFullName(names[count - 1], &plen, nullptr);
    if (!plen) return {};
    std::vector<wchar_t> path(plen);
    if (GetPackagePathByFullName(names[count - 1], &plen, path.data()) != ERROR_SUCCESS) return {};
    return path.data();
}

// For a Store app's exe: its app id, "<Name>_<PublisherId>!<ApplicationId>", from the manifest of the package as
// installed now (the Application whose Executable is this exe, or the first one).
std::wstring AumidFromPackagePath(const std::wstring& exe) {
    std::wstring family, rel, root;
    if (!SplitPackagePath(exe, family, rel, root)) return {};
    if (GetFileAttributesW((root + L"\\AppxManifest.xml").c_str()) == INVALID_FILE_ATTRIBUTES) root = CurrentPackageRoot(family);
    if (root.empty()) return {};
    HANDLE f = CreateFileW((root + L"\\AppxManifest.xml").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    std::string xml(512 * 1024, '\0');
    DWORD read = 0;
    ReadFile(f, xml.data(), (DWORD)xml.size(), &read, nullptr);
    CloseHandle(f);
    xml.resize(read);
    const std::wstring orig = Widen(xml), w = Lower(orig);
    std::wstring firstId, matchId;
    for (size_t p = w.find(L"<application "); p != std::wstring::npos; p = w.find(L"<application ", p + 1)) {
        size_t close = w.find(L'>', p);
        std::wstring tag = w.substr(p, close == std::wstring::npos ? std::wstring::npos : close - p);
        auto attr = [&](const wchar_t* key) {
            size_t k = tag.find(key);
            if (k == std::wstring::npos) return std::wstring();
            size_t q1 = tag.find(L'"', k), q2 = q1 == std::wstring::npos ? q1 : tag.find(L'"', q1 + 1);
            if (q2 == std::wstring::npos) return std::wstring();
            return orig.substr(p + q1 + 1, q2 - q1 - 1);  // original case
        };
        std::wstring id = attr(L" id="), executable = Lower(attr(L" executable="));
        if (firstId.empty()) firstId = id;
        if (!executable.empty() && executable == rel) {
            matchId = id;
            break;
        }
    }
    std::wstring id = matchId.empty() ? firstId : matchId;
    return id.empty() ? std::wstring() : family + L"!" + id;
}

bool ActivatePackaged(const std::wstring& aumid) {
    IApplicationActivationManager* mgr = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(ApplicationActivationManager), nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&mgr))) ||
        !mgr)
        return false;
    DWORD pid = 0;
    HRESULT hr = mgr->ActivateApplication(aumid.c_str(), nullptr, AO_NONE, &pid);
    mgr->Release();
    if (FAILED(hr)) Log("relaunch: activating %s failed (0x%08lX)", Utf8(aumid).c_str(), (unsigned long)hr);
    return SUCCEEDED(hr);
}

bool Run(const std::wstring& file, const std::wstring& params) {
    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = file.c_str();
    sei.lpParameters = params.empty() ? nullptr : params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    bool ok = ShellExecuteExW(&sei) != FALSE;
    if (!ok) Log("relaunch: starting %s failed (%lu)", Utf8(file).c_str(), GetLastError());
    return ok;
}

}  // namespace

bool IsBrowser(const std::wstring& exePath) { return IsChromium(exePath) || IsFirefox(exePath); }

std::wstring PackagedAppId(const std::wstring& exePath) { return AumidFromPackagePath(exePath); }

std::wstring AppKey(const std::wstring& exePath) {
    std::wstring family, rel, root;
    if (SplitPackagePath(exePath, family, rel, root)) return L"package:" + Lower(family) + L"\\" + rel;
    return Lower(exePath);
}

LaunchInfo CaptureLaunch(HWND hwnd, DWORD pid, const std::wstring& exePath) {
    LaunchInfo info;
    if (!IsWindow(hwnd)) return info;
    FromPropertyStore(hwnd, info);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process) {
        wchar_t aumid[APPLICATION_USER_MODEL_ID_MAX_LENGTH] = {};
        UINT32 len = ARRAYSIZE(aumid);
        if (GetApplicationUserModelId(process, &len, aumid) == ERROR_SUCCESS) info.aumid = aumid;  // a Store app
        if (info.command.empty() && !IsBrowser(exePath) && FileNameOf(exePath) != L"explorer.exe") {
            std::wstring cmd = ProcessCommandLine(process);
            // Only worth remembering when it carries something (a document) and would open a window.
            if (!ArgsOf(cmd).empty() && !UselessCommandLine(cmd)) info.command = cmd;
        }
        CloseHandle(process);
    }
    if (FileNameOf(exePath) == L"explorer.exe") {
        wchar_t cls[64] = {};
        GetClassNameW(hwnd, cls, 64);
        if (wcscmp(cls, L"CabinetWClass") == 0) {
            std::wstring folder = ExplorerFolder(hwnd);
            if (!folder.empty()) info.command = Quoted(exePath) + L" " + Quoted(folder);
        }
    }
    // A Store app's window may carry a desktop-style AUMID of its own; only packaged ids are activated.
    if (!IsPackagedPath(exePath) && info.command.empty() && !info.aumid.empty() && info.aumid.find(L'!') == std::wstring::npos)
        info.aumid.clear();
    return info;
}

bool LaunchWindow(const std::wstring& exePath, const LaunchInfo& info) {
    // Store apps: by their app id (their exe can't be started directly).
    std::wstring aumid = info.aumid;
    if (aumid.empty() && IsPackagedPath(exePath)) aumid = AumidFromPackagePath(exePath);
    if (!aumid.empty() && aumid.find(L'!') != std::wstring::npos && (IsPackagedPath(exePath) || info.command.empty()))
        return ActivatePackaged(aumid);

    std::wstring program, args = info.command.empty() ? std::wstring() : ArgsOf(info.command, &program);
    if (program.empty()) program = exePath;
    if (program.empty()) return false;
    std::wstring la = Lower(args);
    // A browser whose profile already has a window only brings that one forward: ask for a new window.
    if (IsChromium(exePath) && la.find(L"--new-window") == std::wstring::npos && la.find(L"--app") == std::wstring::npos)
        args = args.empty() ? L"--new-window" : args + L" --new-window";
    else if (IsFirefox(exePath) && la.find(L"-new-window") == std::wstring::npos)
        args = args.empty() ? L"-new-window" : args + L" -new-window";
    Log("relaunch: %s (%zu chars of arguments)", Utf8(FileNameOf(program)).c_str(), args.size());  // no paths in the log
    return Run(program, args);
}

}  // namespace wp
