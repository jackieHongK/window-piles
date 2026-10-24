// Memos are plain .txt files (UTF-8) in a folder the user can see, by default
// Documents\Stack Neverflow 메모. The app keeps no copy and no index: it lists that folder, so the notes
// stay safe and readable in any editor even if the app breaks or its data is deleted. The folder is
// remembered in organizer.ini (key memoDir) and can be pointed elsewhere there.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace wp {

struct MemoInfo {
    std::wstring path;
    std::wstring title;    // first line (markers removed); empty for an empty memo
    std::wstring excerpt;  // second line
    FILETIME created{};
};

std::wstring MemoDir();                // created if missing
std::vector<MemoInfo> ListMemos();     // in the order the user arranged them; ones not placed yet on top, newest first
void SaveMemoOrder(const std::vector<MemoInfo>& memos);  // remembered by file name in organizer.ini ([memo] order)
std::wstring NewMemo();                // creates an empty memo file, returns its path ("" on failure)
std::wstring ReadMemo(const std::wstring& path);
bool WriteMemo(const std::wstring& path, const std::wstring& text);  // replaces the file safely
bool RecycleMemo(const std::wstring& path);                          // to the Recycle Bin (can be restored)

}  // namespace wp
