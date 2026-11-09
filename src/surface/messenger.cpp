#include "messenger.h"

#include <appmodel.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <propsys.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>

#include "common/win_util.h"

namespace wp {

namespace {

std::wstring IniPath() { return AppDataDir() + L"\\organizer.ini"; }

bool Same(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

std::wstring FileStem(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/") + 1, dot = path.find_last_of(L'.');
    return path.substr(slash, dot == std::wstring::npos || dot < slash ? std::wstring::npos : dot - slash);
}

// The program's own name ("Slack", "KakaoTalk"), else its file name.
std::wstring ProgramName(const std::wstring& exe) {
    DWORD size = GetFileVersionInfoSizeW(exe.c_str(), nullptr);
    if (size) {
        std::vector<BYTE> data(size);
        struct Lang {
            WORD lang, cp;
        }* langs = nullptr;
        UINT len = 0;
        if (GetFileVersionInfoW(exe.c_str(), 0, size, data.data()) &&
            VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", (void**)&langs, &len) && len >= sizeof(Lang)) {
            wchar_t key[64];
            for (const wchar_t* field : {L"FileDescription", L"ProductName"}) {
                swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\%s", langs[0].lang, langs[0].cp, field);
                wchar_t* value = nullptr;
                if (VerQueryValueW(data.data(), key, (void**)&value, &len) && value && *value) return value;
            }
        }
    }
    return FileStem(exe);
}

// A Store app's id from its window (System.AppUserModel.ID); empty for ordinary programs.
std::wstring WindowAumid(HWND hwnd) {
    static const PROPERTYKEY kAumid = {{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};
    IPropertyStore* store = nullptr;
    std::wstring id;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store)))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(store->GetValue(kAumid, &v)) && v.vt == VT_LPWSTR && v.pwszVal) id = v.pwszVal;
        PropVariantClear(&v);
        store->Release();
    }
    return id;
}

// A packaged app's program (new Outlook, Teams) lives in WindowsApps\<package full name>\...: that folder
// name, and the path inside it. False for ordinary programs.
bool PackagePart(const std::wstring& exe, std::wstring& fullName, std::wstring& rest) {
    std::wstring lower = exe;
    for (wchar_t& ch : lower) ch = towlower(ch);
    size_t at = lower.find(L"\\windowsapps\\");
    if (at == std::wstring::npos) return false;
    at += 13;
    size_t end = exe.find(L'\\', at);
    if (end == std::wstring::npos) return false;
    fullName = exe.substr(at, end - at);
    rest = exe.substr(end);
    return true;
}

std::wstring PackageFamily(const std::wstring& fullName) {
    wchar_t family[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1];
    UINT32 n = ARRAYSIZE(family);
    return PackageFamilyNameFromFullName(fullName.c_str(), &n, family) == ERROR_SUCCESS ? family : L"";
}

// What tells an app's processes apart: its program, or for a packaged app its family and the program in
// it, which stay the same when an update moves it to a new folder.
std::wstring AppKey(const std::wstring& exe) {
    std::wstring full, rest, family;
    if (PackagePart(exe, full, rest) && !(family = PackageFamily(full)).empty()) return family + rest;
    return exe;
}

// The app in a package's manifest that runs `rest` (its program, "\olk.exe") and is in the Start menu: a
// package may hold helpers running the same program (Teams), kept out of the Start menu.
std::wstring ManifestAppId(const std::wstring& packagePath, const std::wstring& rest) {
    std::ifstream f(packagePath + L"\\AppxManifest.xml", std::ios::binary);
    std::string xml((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string exe;  // the program as the manifest names it: relative, '\' separated (ASCII in practice)
    for (wchar_t ch : rest.substr(1)) exe += (char)ch;
    auto attr = [](const std::string& tag, const char* name) {
        std::string key = std::string(" ") + name + "=\"";
        size_t at = tag.find(key);
        if (at == std::string::npos) return std::string();
        at += key.size();
        return tag.substr(at, tag.find('"', at) - at);
    };
    std::string fallback;
    for (size_t at = xml.find("<Application "); at != std::string::npos; at = xml.find("<Application ", at + 1)) {
        size_t end = xml.find("</Application>", at);
        std::string block = xml.substr(at, end == std::string::npos ? std::string::npos : end - at);
        std::string tag = block.substr(0, block.find('>'));
        if (_stricmp(attr(tag, "Executable").c_str(), exe.c_str()) != 0) continue;
        std::string id = attr(tag, "Id");
        if (block.find("AppListEntry=\"none\"") == std::string::npos) return std::wstring(id.begin(), id.end());
        if (fallback.empty()) fallback = id;
    }
    return std::wstring(fallback.begin(), fallback.end());
}

// A packaged app can't be started from its program file, and an update moves it: point at the installed
// version and fill in the app id it is started by. True if anything changed.
bool Repair(Messenger& m) {
    std::wstring full, rest, family;
    if (!PackagePart(m.exe, full, rest) || (family = PackageFamily(full)).empty()) return false;
    UINT32 count = 0, len = 0;
    if (GetPackagesByPackageFamily(family.c_str(), &count, nullptr, &len, nullptr) != ERROR_INSUFFICIENT_BUFFER || !count)
        return false;  // uninstalled: left as it was
    std::vector<PWSTR> names(count);
    std::vector<wchar_t> buf(len);
    if (GetPackagesByPackageFamily(family.c_str(), &count, names.data(), &len, buf.data()) != ERROR_SUCCESS || !count)
        return false;
    bool changed = false;
    wchar_t path[MAX_PATH];
    UINT32 plen = MAX_PATH;
    if (GetPackagePathByFullName(names[0], &plen, path) != ERROR_SUCCESS) return false;
    if (!Same(path + rest, m.exe)) {
        m.exe = path + rest;
        changed = true;
    }
    if (m.aumid.empty()) {
        std::wstring id = ManifestAppId(path, rest);
        if (!id.empty()) {
            m.aumid = family + L"!" + id;
            changed = true;
        }
    }
    return changed;
}

bool AppWindow(HWND h) {
    if (GetWindow(h, GW_OWNER) || (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return false;
    return IsWindowVisible(h) || IsIconic(h);
}

}  // namespace

std::vector<Messenger> LoadMessengers() {
    std::vector<Messenger> out;
    int n = (int)GetPrivateProfileIntW(L"messengers", L"count", 0, IniPath().c_str());
    for (int i = 0; i < std::min(n, kMessengerMax); ++i) {
        wchar_t buf[2048] = {};
        GetPrivateProfileStringW(L"messengers", std::to_wstring(i).c_str(), L"", buf, 2048, IniPath().c_str());
        // exe \t shortcut \t app id \t name
        std::wstring s = buf, part[4];
        for (int k = 0, at = 0; k < 4; ++k) {
            size_t tab = s.find(L'\t', at);
            part[k] = s.substr(at, tab == std::wstring::npos ? std::wstring::npos : tab - at);
            if (tab == std::wstring::npos) break;
            at = (int)tab + 1;
        }
        if (!part[0].empty()) out.push_back({part[0], part[1], part[2], part[3].empty() ? FileStem(part[0]) : part[3]});
    }
    bool repaired = false;
    for (Messenger& m : out) repaired |= Repair(m);
    if (repaired) SaveMessengers(out);
    return out;
}

void SaveMessengers(const std::vector<Messenger>& list) {
    WritePrivateProfileStringW(L"messengers", nullptr, nullptr, IniPath().c_str());  // the section, afresh
    WritePrivateProfileStringW(L"messengers", L"count", std::to_wstring(list.size()).c_str(), IniPath().c_str());
    for (size_t i = 0; i < list.size(); ++i) {
        const Messenger& m = list[i];
        std::wstring line = m.exe + L"\t" + m.link + L"\t" + m.aumid + L"\t" + m.name;
        WritePrivateProfileStringW(L"messengers", std::to_wstring(i).c_str(), line.c_str(), IniPath().c_str());
    }
}

bool MessengerFromWindow(HWND hwnd, Messenger& out) {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetAncestor(hwnd, GA_ROOT), &pid);
    std::wstring exe = ProcessExePath(pid);
    if (exe.empty()) return false;
    out = {exe, L"", WindowAumid(hwnd), ProgramName(exe)};
    if (out.aumid.empty()) {  // a packaged program's windows don't say it; its process does
        if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
            wchar_t id[APPLICATION_USER_MODEL_ID_MAX_LENGTH];
            UINT32 n = ARRAYSIZE(id);
            if (GetApplicationUserModelId(p, &n, id) == ERROR_SUCCESS) out.aumid = id;
            CloseHandle(p);
        }
    }
    Repair(out);
    return true;
}

bool MessengerFromFile(const std::wstring& path, Messenger& out) {
    std::wstring ext = path.size() > 4 ? path.substr(path.size() - 4) : L"";
    if (Same(ext, L".exe")) {
        out = {path, L"", L"", ProgramName(path)};
        Repair(out);
        return true;
    }
    if (!Same(ext, L".lnk")) return false;
    // A shortcut: started as it is; its target tells which windows are the app's.
    IShellLinkW* link = nullptr;
    std::wstring target;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
            wchar_t buf[MAX_PATH] = {};
            if (SUCCEEDED(file->Load(path.c_str(), STGM_READ)) && SUCCEEDED(link->GetPath(buf, MAX_PATH, nullptr, 0)))
                target = buf;
            file->Release();
        }
        link->Release();
    }
    if (target.empty() || !Same(target.size() > 4 ? target.substr(target.size() - 4) : L"", L".exe")) return false;
    out = {target, path, L"", FileStem(path)};
    return true;
}

std::vector<HWND> MessengerWindows(const Messenger& m) {
    struct Ctx {
        std::wstring key;
        std::map<DWORD, bool> mine;  // per process: is it this app
        std::vector<HWND> out;
    } ctx{AppKey(m.exe), {}, {}};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {  // top to bottom: frontmost first
        auto* c = (Ctx*)lp;
        if (!AppWindow(h)) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        auto it = c->mine.find(pid);
        if (it == c->mine.end()) it = c->mine.emplace(pid, Same(AppKey(ProcessExePath(pid)), c->key)).first;
        if (it->second) c->out.push_back(h);
        return TRUE;
    }, (LPARAM)&ctx);
    return ctx.out;
}

UnreadInfo MessengerUnread(const Messenger& m) {
    UnreadInfo total;
    for (HWND h : MessengerWindows(m)) {
        UnreadInfo u = ParseUnread(WindowTitle(h));
        total.count += u.count;
        total.marker |= u.marker;
    }
    return total;
}

HICON MessengerIcon(const Messenger& m) {
    static std::map<std::wstring, HICON> cache;  // ponytail: kept for the app's life; a handful of icons
    auto it = cache.find(m.exe);
    if (it != cache.end() && it->second) return it->second;
    HICON icon = nullptr;
    SHFILEINFOW sfi{};
    // An app with an id: its Start menu entry's icon (a packaged program file gives none).
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (!m.aumid.empty()) SHParseDisplayName((L"shell:AppsFolder\\" + m.aumid).c_str(), nullptr, &pidl, 0, nullptr);
    DWORD_PTR found = pidl ? SHGetFileInfoW((LPCWSTR)pidl, 0, &sfi, sizeof(sfi), SHGFI_PIDL | SHGFI_SYSICONINDEX)
                           : SHGetFileInfoW((m.link.empty() ? m.exe : m.link).c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX);
    CoTaskMemFree(pidl);
    if (found) {
        IImageList* list = nullptr;
        if (SUCCEEDED(SHGetImageList(SHIL_EXTRALARGE, IID_PPV_ARGS(&list)))) {
            list->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &icon);
            list->Release();
        }
    }
    // None found: an open window of it may have one.
    if (!icon)
        for (HWND h : MessengerWindows(m)) {
            DWORD_PTR res = 0;
            if (SendMessageTimeoutW(h, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 100, &res) && res) {
                icon = CopyIcon((HICON)res);
                break;
            }
        }
    if (icon) cache[m.exe] = icon;
    return icon;
}

void ToggleMessenger(const Messenger& m) {
    std::vector<HWND> windows = MessengerWindows(m);
    // The tray never takes the focus, so the window in front is still the one that was there before the click.
    if (!windows.empty() && !IsIconic(windows.front()) && GetForegroundWindow() == windows.front()) {
        PostMessageW(windows.front(), WM_CLOSE, 0, 0);
        return;
    }
    OpenMessenger(m);
}

void OpenMessenger(const Messenger& m) {
    std::vector<HWND> windows = MessengerWindows(m);
    if (!windows.empty()) {
        HWND h = windows.front();
        if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
        SetForegroundWindow(h);
        return;
    }
    // Not open (or only in the notification area): start it; single-instance apps bring their window up.
    if (!m.link.empty()) ShellExecuteW(nullptr, L"open", m.link.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    else if (!m.aumid.empty())
        ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"shell:AppsFolder\\" + m.aumid).c_str(), nullptr, SW_SHOWNORMAL);
    else ShellExecuteW(nullptr, L"open", m.exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace wp
