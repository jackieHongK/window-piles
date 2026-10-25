// The organizer's messenger dock (like the phone stand on a monitor memo board): apps that don't belong to
// any one task (chat, mail) kept as icons below the memos, to bring up from wherever you are working. A
// click brings the app's window forward, or starts it; a badge shows what its window titles say is unread.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "groups/unread.h"

namespace wp {

struct Messenger {
    std::wstring exe;    // the program its windows belong to
    std::wstring link;   // the shortcut it was added from, started instead of the exe (may be empty)
    std::wstring aumid;  // a Store app's id (shell:AppsFolder\<id>), from its window (may be empty)
    std::wstring name;
};

constexpr int kMessengerMax = 5;

std::vector<Messenger> LoadMessengers();  // organizer.ini [messengers]
void SaveMessengers(const std::vector<Messenger>& list);
// From a window being dragged in, or a shortcut / program dropped from Explorer. False if it isn't an app.
bool MessengerFromWindow(HWND hwnd, Messenger& out);
bool MessengerFromFile(const std::wstring& path, Messenger& out);
// Its top-level windows that can be brought forward (shown or minimized), frontmost first.
std::vector<HWND> MessengerWindows(const Messenger& m);
UnreadInfo MessengerUnread(const Messenger& m);  // added up over its window titles
HICON MessengerIcon(const Messenger& m);          // large, cached; nullptr if there is none
void OpenMessenger(const Messenger& m);           // its window forward (restored if minimized), else started
// A click on its tile: its window already in front is closed (as its own close button would); otherwise opened.
void ToggleMessenger(const Messenger& m);

}  // namespace wp
