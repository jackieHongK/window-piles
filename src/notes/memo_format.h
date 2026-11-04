// The memo's few formatting rules, kept as plain text so the .txt files read fine anywhere:
//   "# ", "## ", "### "  headings 1-3      "- " or "* "  bullet list      "1. "  numbered list
//   "☐ ", "☑ "           a to-do and a done one (typed as "[] " or "[x] ", or by the memo's checklist button)
// Everything else is body text. Web addresses in any line are links.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wp {

enum class MemoLine { Body, H1, H2, H3, Bullet, Number, Task, Done };

inline constexpr wchar_t kMemoTodo = 0x2610, kMemoDone = 0x2611;  // ☐ ☑

struct MemoLineInfo {
    MemoLine kind = MemoLine::Body;
    int markerLen = 0;  // "## " → 3, "- " → 2, "12. " → 4
    int number = 0;     // for numbered lines
};

MemoLineInfo ClassifyMemoLine(std::wstring_view line);

// What Enter does at the end of `line`: continue a list with the next marker, or, on a list line that
// holds only its marker, end the list (clear the marker, no new line).
struct MemoEnter {
    bool endList = false;
    std::wstring nextPrefix;  // to type after the line break ("- ", "4. "); empty for plain lines
};
MemoEnter MemoOnEnter(std::wstring_view line);

// A line that starts with "[] ", "[ ] ", "[x] " (or "- [ ] ", "- [x] "): how much of it is that shortcut, and
// the to-do marker it stands for ("☐ " or "☑ "); 0 if it doesn't start with one.
size_t MemoTaskShortcut(std::wstring_view line, std::wstring* marker);

// The web addresses in a line ("https://…", "http://…", "www.…"), as [start, end) in it: up to a space or
// bracket, without the punctuation a sentence puts right after one.
std::vector<std::pair<size_t, size_t>> FindMemoLinks(std::wstring_view line);

// The memo's name in lists: its first non-empty line without markers (max 40 chars), and the next one.
std::wstring MemoTitle(std::wstring_view text);
std::wstring MemoExcerpt(std::wstring_view text);

// A memo's file name: "2026-10-25 장보기.txt" — the date, then its title with anything but letters, digits,
// spaces and - _ . , ( ) [ ] taken out (so every file system and sync service takes it), at most 50
// characters; "메모" if nothing is left.
std::wstring MemoFileName(std::wstring_view date, std::wstring_view title);

}  // namespace wp
