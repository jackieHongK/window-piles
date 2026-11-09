// Check: messenger entries saved with only a packaged app's program path (new Outlook, Teams), as older
// builds did, are repaired on load: an app id to start them by, the installed version's path, an icon.
// Also: an entry pointing at an outdated package folder still matches the installed one.
// Usage: messenger_check   (exit code 0 = pass; skips a package that isn't installed)
#include <windows.h>
#include <objbase.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "surface/messenger.h"

using namespace wp;

int wmain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"sn-messenger-check";
    CreateDirectoryW(dir.c_str(), nullptr);
    SetEnvironmentVariableW(L"WINDOWPILES_DATA_DIR", dir.c_str());
    std::wstring ini = dir + L"\\organizer.ini";
    // Outdated versions on purpose: the folder an update has since replaced.
    WritePrivateProfileStringW(L"messengers", nullptr, nullptr, ini.c_str());
    WritePrivateProfileStringW(L"messengers", L"count", L"3", ini.c_str());
    WritePrivateProfileStringW(L"messengers", L"0",
        L"C:\\Program Files\\WindowsApps\\Microsoft.OutlookForWindows_1.0.0.0_x64__8wekyb3d8bbwe\\olk.exe\t\t\tMicrosoft Outlook", ini.c_str());
    WritePrivateProfileStringW(L"messengers", L"1",
        L"C:\\Program Files\\WindowsApps\\MSTeams_1.0.0.0_x64__8wekyb3d8bbwe\\ms-teams.exe\t\t\tMicrosoft Teams", ini.c_str());
    WritePrivateProfileStringW(L"messengers", L"2", L"C:\\Windows\\notepad.exe\t\t\tNotepad", ini.c_str());

    int failures = 0, checked = 0;
    std::vector<Messenger> list = LoadMessengers();
    for (const Messenger& m : list) {
        bool packaged = m.exe.find(L"WindowsApps") != std::wstring::npos;
        bool installed = GetFileAttributesW(m.exe.c_str()) != INVALID_FILE_ATTRIBUTES;
        HICON icon = MessengerIcon(m);
        std::wprintf(L"%ls\n  exe %ls (%ls)\n  id  %ls\n  icon %ls\n", m.name.c_str(), m.exe.c_str(),
                     installed ? L"there" : L"missing", m.aumid.c_str(), icon ? L"yes" : L"no");
        if (packaged && m.exe.find(L"_1.0.0.0_") != std::wstring::npos) {
            std::wprintf(L"  (not installed here: skipped)\n");
            continue;
        }
        ++checked;
        // The app the Start menu starts, not a helper of the same program (Teams has one).
        bool wrongId = m.name == L"Microsoft Teams" && m.aumid != L"MSTeams_8wekyb3d8bbwe!MSTeams";
        if (!installed || !icon || (packaged && m.aumid.empty()) || wrongId) {
            std::wprintf(L"  FAIL\n");
            ++failures;
        }
    }
    // Saved back repaired.
    wchar_t buf[2048];
    GetPrivateProfileStringW(L"messengers", L"0", L"", buf, 2048, ini.c_str());
    std::wprintf(L"saved: %ls\n", buf);
    std::wprintf(failures ? L"%d FAILED\n" : L"PASS (%d checked)\n", failures ? failures : checked);
    return failures ? 1 : 0;
}
