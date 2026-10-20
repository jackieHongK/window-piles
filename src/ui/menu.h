// shadcn-style dropdown menu (replaces the classic Win32 popup menu).
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace wp {

struct MenuItem {
    UINT id = 0;  // 0 = separator
    std::wstring text;
    std::wstring hint;  // muted text on the right
    bool disabled = false;
    bool separator() const { return id == 0; }
};

// Shows the menu at `pt` (screen) and blocks until an item is chosen or it is dismissed.
// Returns the chosen id, or 0.
UINT ShowMenu(const std::vector<MenuItem>& items, POINT pt);

}  // namespace wp
