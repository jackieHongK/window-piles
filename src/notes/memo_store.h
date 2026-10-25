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
    int color = 0;  // its flag's color: index into the organizer's Post-it colors
};

std::wstring MemoDir();                // created if missing
std::vector<MemoInfo> ListMemos();     // in the order the user arranged them; ones not placed yet on top, newest first
void SaveMemoOrder(const std::vector<MemoInfo>& memos);  // remembered by file name in organizer.ini ([memo] order)
std::wstring NewMemo();                // creates an empty memo file ("2026-10-25 메모.txt"), returns its path ("" on failure)
std::wstring ReadMemo(const std::wstring& path);
bool WriteMemo(const std::wstring& path, const std::wstring& text);  // replaces the file safely
bool RecycleMemo(const std::wstring& path);                          // to the Recycle Bin (can be restored)
void SetMemoColor(const std::wstring& path, int color);              // kept by file name in organizer.ini
// Renames the file after its title ("2026-10-25 장보기.txt", the date it was made), keeping its place in the
// order and its color. Returns the path it has now (the old one if nothing changed or it couldn't be renamed).
std::wstring RenameMemoForTitle(const std::wstring& path, const std::wstring& title);

}  // namespace wp
