// Pile model: a group of real top-level windows. Membership and visibility are separate, so a
// pile can be partly open (plan §2.5).
#pragma once

#include <windows.h>

#include <algorithm>
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
    // How it overlapped the other windows when it was put away: higher = nearer the front. Unfolding
    // brings the windows back in this order, so the one on top before is on top again.
    uint64_t stamp = 0;
    Image snapshot;  // shown in the folded card (snapshot or icon card); never written to disk
};

// A window that belonged to the pile but no longer exists (its app was closed, or the PC
// restarted). Listed in the card so the user can reopen it; removed when that app's window
// is put back into the pile.
struct MissingWindow {
    std::wstring exePath;
    std::wstring appName;
    std::wstring title;
    bool launched = false;  // reopened from the card this session (not saved): don't launch it again
};

struct Pile {
    int id = 0;
    std::wstring name;
    bool customName = false;  // the user renamed it: stop auto-naming
    COLORREF color = RGB(51, 126, 169);
    int material = 0;     // card body below the title bar: index into kMaterials (ui/material.h)
    bool docked = false;  // kept in the edge organizer: no card on the desktop
    bool folded = true;   // folded = windows put away in the card
    bool active = false;  // open and "being worked on": card hidden, screen frame + top tab shown
    POINT pos{};          // top-left of the pile card window, screen px (stays put while open)
    std::vector<Member> members;
    std::vector<MissingWindow> missing;
    std::vector<std::wstring> files;  // files and folders kept with the pile (paths): opened when it opens
    std::unique_ptr<PileWindow> view;

    int HiddenCount() const {
        int n = 0;
        for (auto& m : members) n += m.hidden;
        return n;
    }
    bool Empty() const { return members.empty() && missing.empty() && files.empty(); }
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
    // The card shows the kPreviews front-most members (by stamp, then the most recently added) as
    // strips of their top part. Only those need a preview image, a capture or an animation ghost, so
    // a pile of 50 windows costs the same as one of 3.
    static constexpr int kPreviews = 3;
    static constexpr int kPreviewW = 240, kPreviewH = 89;  // DIP (the card is 151 tall with these)
    static bool InFrontOf(const Member& a, size_t ia, const Member& b, size_t ib) {
        return a.stamp != b.stamp ? a.stamp > b.stamp : ia > ib;
    }
    bool ShowsPreview(const Member& m) const {
        size_t i = (size_t)(&m - members.data());
        if (i >= members.size()) return false;
        int ahead = 0;
        for (size_t k = 0; k < members.size(); ++k)
            if (k != i && InFrontOf(members[k], k, m, i) && ++ahead >= kPreviews) return false;
        return true;
    }
    // Members front-most first (the first `n`, or all).
    std::vector<const Member*> FrontToBack(size_t n = SIZE_MAX) const {
        std::vector<size_t> idx(members.size());
        for (size_t k = 0; k < idx.size(); ++k) idx[k] = k;
        std::sort(idx.begin(), idx.end(),
                  [&](size_t a, size_t b) { return InFrontOf(members[a], a, members[b], b); });
        std::vector<const Member*> out;
        for (size_t k = 0; k < idx.size() && k < n; ++k) out.push_back(&members[idx[k]]);
        return out;
    }
    std::vector<Member*> FrontToBack(size_t n = SIZE_MAX) {
        std::vector<Member*> out;
        for (const Member* m : static_cast<const Pile&>(*this).FrontToBack(n)) out.push_back(&members[m - members.data()]);
        return out;
    }
};

}  // namespace wp
