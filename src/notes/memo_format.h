// The memo's few formatting rules, kept as plain text so the .txt files read fine anywhere:
//   "# ", "## ", "### "  headings 1-3      "- " or "* "  bullet list      "1. "  numbered list
// Everything else is body text.
#pragma once

#include <string>
#include <string_view>

namespace wp {

enum class MemoLine { Body, H1, H2, H3, Bullet, Number };

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

// The memo's name in lists: its first non-empty line without markers (max 40 chars), and the next one.
std::wstring MemoTitle(std::wstring_view text);
std::wstring MemoExcerpt(std::wstring_view text);

// A memo's file name: "2026-10-25 장보기.txt" — the date, then its title with anything but letters, digits,
// spaces and - _ . , ( ) [ ] taken out (so every file system and sync service takes it), at most 50
// characters; "메모" if nothing is left.
std::wstring MemoFileName(std::wstring_view date, std::wstring_view title);

}  // namespace wp
