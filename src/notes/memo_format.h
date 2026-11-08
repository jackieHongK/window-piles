// The memo's few formatting rules, kept as plain text so the .txt files read fine anywhere:
//   "# ", "## ", "### "  headings 1-3      "- " or "* "  bullet list      "1. "  numbered list
//   "☐ ", "☑ "           a to-do and a done one (typed as "[] " or "[x] ", or by the memo's checklist button)
// A list line or a to-do may start with tabs: one per level it is nested ("\t- " is one level in, as Markdown
// reads it too). The editor may show a bullet as "• " (round bullets): the file always gets "- ".
// Everything else is body text. Web addresses in any line are links.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wp {

enum class MemoLine { Body, H1, H2, H3, Bullet, Number, Task, Done };

inline constexpr wchar_t kMemoTodo = 0x2610, kMemoDone = 0x2611;  // ☐ ☑
inline constexpr wchar_t kMemoDot = 0x2022;                         // • a round bullet, as the editor shows it
inline constexpr int kMemoMaxIndent = 6;                            // levels a list can go in

struct MemoLineInfo {
    MemoLine kind = MemoLine::Body;
    int markerLen = 0;  // "## " → 3, "- " → 2, "12. " → 4, "\t\t- " → 4 (with the tabs before it)
    int number = 0;     // for numbered lines
    int indent = 0;     // the tabs before a list's or a to-do's marker: how far in it is nested
};

inline bool IsMemoList(MemoLine k) {
    return k == MemoLine::Bullet || k == MemoLine::Number || k == MemoLine::Task || k == MemoLine::Done;
}

MemoLineInfo ClassifyMemoLine(std::wstring_view line);

// What Enter does at the end of `line`: continue a list with the next marker, or, on a list line that
// holds only its marker, end the list (clear the marker, no new line).
struct MemoEnter {
    bool endList = false;
    bool outdent = false;     // an empty nested item: it goes one level out instead
    std::wstring nextPrefix;  // to type after the line break ("- ", "4. "); empty for plain lines
};
MemoEnter MemoOnEnter(std::wstring_view line);

// A line that starts with "[] ", "[ ] ", "[x] " (or "- [ ] ", "- [x] "): how much of it is that shortcut, and
// the to-do marker it stands for ("☐ " or "☑ "); 0 if it doesn't start with one.
size_t MemoTaskShortcut(std::wstring_view line, std::wstring* marker);

// Every list line's bullet as the file has it ("- ") or as the editor shows it with round bullets ("• "):
// "- " and "* " become "• ", and "• " back to "- ". Lines may end in \r, \n or \v. Only marker characters change, one for
// one, so positions in the text stay where they were.
std::wstring MemoBulletsShown(std::wstring text, bool round);

// What the end of `before` (the line up to the caret, with the character just typed) turns into: "->" → "→",
// "=>" → "⇒", "<=" → "≤", "!=" → "≠", "..." → "…" and the like. How many characters at its end to replace,
// and with what; 0 if nothing.
size_t MemoPrettify(std::wstring_view before, std::wstring* with);

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
