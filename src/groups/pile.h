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
    std::wstring title;
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};  // where to bring it back
    bool hidden = false;
    Image snapshot;  // shown in the folded card (snapshot or icon card)
};

struct Pile {
    int id = 0;
    std::wstring name;
    COLORREF color = RGB(66, 133, 244);
    bool folded = true;  // folded = card with previews; open = small chip
    POINT pos{};         // top-left of the pile surface, screen px
    std::vector<Member> members;
    std::unique_ptr<PileWindow> view;

    int HiddenCount() const {
        int n = 0;
        for (auto& m : members) n += m.hidden;
        return n;
    }
    Member* Find(HWND h) {
        for (auto& m : members)
            if (m.hwnd == h) return &m;
        return nullptr;
    }
};

}  // namespace wp
