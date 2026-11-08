// A memo at a glance: its first lines as they read in its window (headings bigger, list markers as bullets),
// on a sheet of paper with its flag's color along the top. Unfolded beside the organizer's flags and beside
// the rows of the memo list while the pointer rests on one.
#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include "common/canvas.h"

namespace wp {

inline constexpr int kMemoPeekW = 300, kMemoPeekMaxH = 380;  // DIPs; it is as tall as its lines, up to the max

std::unique_ptr<Canvas> RenderMemoPeek(const std::wstring& path, COLORREF color, UINT dpi);

}  // namespace wp
