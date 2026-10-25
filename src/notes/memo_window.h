// A memo's window (Sticky Notes, a little more): a RichEdit that styles "# " headings and "- " / "1. "
// lists as you type, continues a list on Enter, and saves the .txt file shortly after every change.
// The file is the memo: closing an empty memo deletes it, nothing else is kept anywhere.
#pragma once

#include <windows.h>

#include <functional>
#include <string>

namespace wp {

// Opens the memo at `path` near `anchor` (screen), or exactly at `at` when given, or brings its window forward
// if it is already open. nullptr if it could not open.
HWND OpenMemoWindow(const std::wstring& path, POINT anchor, const RECT* at = nullptr);
SIZE MemoWindowSize(UINT dpi);  // a new memo window's size
int OpenMemoCount();
// Called after a memo window saved its file (a title may have changed: its flag shows it right away).
void OnMemoSaved(std::function<void()> handler);
void RefreshMemoTabs();  // a memo's flag color changed: the open windows' color tabs follow
// Saves and closes every memo window (quitting).
void CloseAllMemoWindows();
// Shows the file selected in Explorer.
void RevealInExplorer(const std::wstring& path);

}  // namespace wp
