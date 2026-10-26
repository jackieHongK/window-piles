// The connected displays, numbered the way people see them (left to right, then top to bottom), and an
// "식별" flash that puts each display's number on it — so a setting can say "디스플레이 2".
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace wp {

struct DisplayInfo {
    std::wstring device;  // \\.\DISPLAY1 …: what settings remember
    RECT monitor{}, work{};
    bool primary = false;
};

// Left to right (then top to bottom): display N is element N-1.
std::vector<DisplayInfo> ListDisplays();
// The device of the primary display.
std::wstring PrimaryDisplay();
// Shows each display's number large in its middle for `ms`, then removes them by itself.
void IdentifyDisplays(DWORD ms = 2000);

}  // namespace wp
