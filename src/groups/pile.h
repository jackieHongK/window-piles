// Pile model: a group of real top-level windows. Membership and visibility are separate, so a
// pile can be partly open (plan §2.5).
#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/canvas.h"

namespace wp {

class PileWindow;

struct Member {
    HWND hwnd = nullptr;
    DWORD pid = 0;
    uint64_t createTime = 0;
    std::wstring className;
    std::wstring exePath;  // to tell the user which app to reopen if the window is gone later
    std::wstring title;
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};  // where to bring it back
    RECT lastRect{};  // screen rect when last visible (for the gather animation)
    bool hidden = false;
    // Notifications while put away (its taskbar button is gone, so the pile has to show them):
    int unread = 0;          // unread count read from the window title
    bool attention = false;  // the app flashed its taskbar button / marked the title
    DWORD hiddenAt = 0;      // GetTickCount() when put away (a flash right then is not news)
    Image snapshot;  // shown in the folded card (snapshot or icon card); never written to disk
};

// A window that belonged to the pile but no longer exists (its app was closed, or the PC
// restarted). Listed in the card so the user can reopen it; removed when that app's window
// is put back into the pile.
struct MissingWindow {
    std::wstring exePath;
    std::wstring appName;
    std::wstring title;
};

struct Pile {
    int id = 0;
    std::wstring name;
    bool customName = false;  // the user renamed it: stop auto-naming
    COLORREF color = RGB(51, 126, 169);
    bool folded = true;   // folded = windows put away in the card
    bool active = false;  // open and "being worked on": card hidden, screen frame + top tab shown
    POINT pos{};          // top-left of the pile card window, screen px (stays put while open)
    std::vector<Member> members;
    std::vector<MissingWindow> missing;
    std::unique_ptr<PileWindow> view;

    int HiddenCount() const {
        int n = 0;
        for (auto& m : members) n += m.hidden;
        return n;
    }
    bool Empty() const { return members.empty() && missing.empty(); }
    // Badge for the card: unread total and "something needs attention" over hidden windows.
    int BadgeCount() const {
        int n = 0;
        for (auto& m : members) n += m.hidden ? m.unread : 0;
        return n;
    }
    bool BadgeAttention() const {
        for (auto& m : members)
            if (m.hidden && m.attention) return true;
        return false;
    }
    Member* Find(HWND h) {
        for (auto& m : members)
            if (m.hwnd == h) return &m;
        return nullptr;
    }
    // The card shows the last kPreviews members (newest in front). Only those need a preview image,
    // a capture or an animation ghost, so a pile of 50 windows costs the same as one of 3.
    static constexpr int kPreviews = 3;
    bool ShowsPreview(const Member& m) const {
        size_t i = (size_t)(&m - members.data());
        return i < members.size() && i + kPreviews >= members.size();
    }
};

}  // namespace wp
