// Quick rename right on the pile's title bar: a small input placed over the title text.
// Enter or clicking elsewhere saves, Esc cancels. Color changes stay in the "이름·색 바꾸기" popover.
#pragma once

#include <windows.h>

#include <functional>
#include <string>

namespace wp {

// `screenRect`: where the input goes (the title text area). Blocks until done.
// `preview` gets the text as it is typed. Returns true and fills `out` (trimmed) when saved.
bool InlineRename(const RECT& screenRect, const std::wstring& current, UINT dpi,
                  const std::function<void(const std::wstring&)>& preview, std::wstring& out);

}  // namespace wp
