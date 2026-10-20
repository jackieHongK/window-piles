// "더미 꾸미기" popover: rename a pile and pick its title-bar color (presets or any color).
#pragma once

#include <windows.h>

#include <functional>
#include <string>

namespace wp {

struct PileStyle {
    std::wstring name;
    COLORREF color = 0;
};

// Blocks until saved or cancelled. `preview` gets live changes so the pile can update as you edit.
// Returns true and fills `out` when saved.
bool EditPileStyle(const PileStyle& current, POINT anchor, const std::function<void(const PileStyle&)>& preview,
                   PileStyle& out);

}  // namespace wp
