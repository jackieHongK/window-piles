#include "memo_format.h"

#include <cwctype>
#include <vector>

namespace wp {

MemoLineInfo ClassifyMemoLine(std::wstring_view line) {
    MemoLineInfo info;
    size_t hashes = 0;
    while (hashes < line.size() && line[hashes] == L'#') ++hashes;
    if (hashes >= 1 && hashes <= 3 && hashes < line.size() && line[hashes] == L' ') {
        info.kind = hashes == 1 ? MemoLine::H1 : hashes == 2 ? MemoLine::H2 : MemoLine::H3;
        info.markerLen = (int)hashes + 1;
        return info;
    }
    if (line.size() >= 2 && (line[0] == L'-' || line[0] == L'*') && line[1] == L' ') {
        info.kind = MemoLine::Bullet;
        info.markerLen = 2;
        return info;
    }
    size_t digits = 0;
    while (digits < line.size() && digits < 4 && iswdigit(line[digits])) ++digits;
    if (digits > 0 && digits + 1 < line.size() && line[digits] == L'.' && line[digits + 1] == L' ') {
        info.kind = MemoLine::Number;
        info.markerLen = (int)digits + 2;
        info.number = std::stoi(std::wstring(line.substr(0, digits)));
    }
    return info;
}

MemoEnter MemoOnEnter(std::wstring_view line) {
    MemoEnter e;
    MemoLineInfo info = ClassifyMemoLine(line);
    if (info.kind != MemoLine::Bullet && info.kind != MemoLine::Number) return e;
    bool onlyMarker = line.find_first_not_of(L' ', info.markerLen) == std::wstring_view::npos;
    if (onlyMarker) {
        e.endList = true;
        return e;
    }
    e.nextPrefix = info.kind == MemoLine::Bullet ? std::wstring(line.substr(0, 2)) : std::to_wstring(info.number + 1) + L". ";
    return e;
}

namespace {

// Non-empty lines with their markers and surrounding blanks removed.
std::vector<std::wstring> ContentLines(std::wstring_view text, size_t want) {
    std::vector<std::wstring> out;
    size_t pos = 0;
    while (pos <= text.size() && out.size() < want) {
        size_t end = text.find_first_of(L"\r\n", pos);
        if (end == std::wstring_view::npos) end = text.size();
        std::wstring_view line = text.substr(pos, end - pos);
        line.remove_prefix(ClassifyMemoLine(line).markerLen);
        size_t a = line.find_first_not_of(L" \t"), b = line.find_last_not_of(L" \t");
        if (a != std::wstring_view::npos) out.emplace_back(line.substr(a, b - a + 1));
        pos = end + 1;
        if (end == text.size()) break;
    }
    return out;
}

std::wstring Clip(std::wstring s) {
    if (s.size() > 40) s = s.substr(0, 39) + L"…";
    return s;
}

}  // namespace

namespace {

bool NameChar(wchar_t c) {
    if ((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z')) return true;
    if (std::wstring_view(L" -_.,()[]").find(c) != std::wstring_view::npos) return true;
    return (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7)  // Latin letters with accents
           || (c >= 0x370 && c <= 0x52F)                           // Greek, Cyrillic
           || (c >= 0x3040 && c <= 0x30FF)                         // kana
           || (c >= 0x3130 && c <= 0x318F)                         // Hangul jamo
           || (c >= 0x4E00 && c <= 0x9FFF)                         // CJK ideographs
           || (c >= 0xAC00 && c <= 0xD7A3);                        // Hangul syllables
}

}  // namespace

std::wstring MemoFileName(std::wstring_view date, std::wstring_view title) {
    std::wstring name;
    for (wchar_t c : title) {
        if (!NameChar(c)) c = L' ';
        if (c == L' ' && (name.empty() || name.back() == L' ')) continue;  // no leading or doubled spaces
        name += c;
    }
    if (name.size() > 50) name.resize(50);
    // Windows drops trailing dots and spaces from names, which would make the file hard to find again.
    while (!name.empty() && (name.back() == L' ' || name.back() == L'.')) name.pop_back();
    if (name.empty()) name = L"메모";
    return std::wstring(date) + L" " + name + L".txt";
}

std::wstring MemoTitle(std::wstring_view text) {
    auto lines = ContentLines(text, 1);
    return lines.empty() ? L"" : Clip(lines[0]);
}

std::wstring MemoExcerpt(std::wstring_view text) {
    auto lines = ContentLines(text, 2);
    return lines.size() < 2 ? L"" : Clip(lines[1]);
}

}  // namespace wp
