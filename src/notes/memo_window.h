// A memo's window (Sticky Notes, a little more): a RichEdit that styles "# " headings and "- " / "1. "
// lists as you type, continues a list on Enter, and saves the .txt file shortly after every change.
// The file is the memo: closing an empty memo deletes it, nothing else is kept anywhere.
#pragma once

#include <windows.h>

#include <string>

namespace wp {

// Opens the memo at `path` near `anchor` (screen), or brings its window forward if it is already open.
HWND OpenMemoWindow(const std::wstring& path, POINT anchor);  // nullptr if it could not open
// Saves and closes every memo window (quitting).
void CloseAllMemoWindows();
// Shows the file selected in Explorer.
void RevealInExplorer(const std::wstring& path);

}  // namespace wp
