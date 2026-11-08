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
    size_t tabs = 0;
    while (tabs < line.size() && line[tabs] == L'\t') ++tabs;
    std::wstring_view rest = line.substr(tabs);
    auto list = [&](MemoLine kind, size_t len) {
        info.kind = kind;
        info.markerLen = (int)(tabs + len);
        info.indent = (int)tabs;
        return info;
    };
    if (rest.size() >= 2 && (rest[0] == kMemoTodo || rest[0] == kMemoDone) && rest[1] == L' ')
        return list(rest[0] == kMemoTodo ? MemoLine::Task : MemoLine::Done, 2);
    if (rest.size() >= 2 && (rest[0] == L'-' || rest[0] == L'*' || rest[0] == kMemoDot) && rest[1] == L' ')
        return list(MemoLine::Bullet, 2);
    size_t digits = 0;
    while (digits < rest.size() && digits < 4 && iswdigit(rest[digits])) ++digits;
    if (digits > 0 && digits + 1 < rest.size() && rest[digits] == L'.' && rest[digits + 1] == L' ') {
        info.number = std::stoi(std::wstring(rest.substr(0, digits)));
        return list(MemoLine::Number, digits + 2);
    }
    return info;
}

MemoEnter MemoOnEnter(std::wstring_view line) {
    MemoEnter e;
    MemoLineInfo info = ClassifyMemoLine(line);
    bool task = info.kind == MemoLine::Task || info.kind == MemoLine::Done;
    if (!IsMemoList(info.kind)) return e;
    bool onlyMarker = line.find_first_not_of(L' ', info.markerLen) == std::wstring_view::npos;
    if (onlyMarker) {
        (info.indent > 0 ? e.outdent : e.endList) = true;
        return e;
    }
    e.nextPrefix = std::wstring((size_t)info.indent, L'\t') +
                   (task                           ? std::wstring{kMemoTodo, L' '}  // the next one is still to do
                    : info.kind == MemoLine::Bullet ? std::wstring(line.substr(info.indent, 2))
                                                    : std::to_wstring(info.number + 1) + L". ");
    return e;
}

size_t MemoTaskShortcut(std::wstring_view line, std::wstring* marker) {
    struct Form {
        std::wstring_view text;
        bool done;
    };
    static constexpr Form kForms[] = {{L"[] ", false},   {L"[ ] ", false},   {L"[x] ", true},   {L"[X] ", true},
                                      {L"- [ ] ", false}, {L"- [x] ", true}, {L"- [X] ", true},
                                      {L"• [ ] ", false}, {L"• [x] ", true}, {L"• [X] ", true}};
    for (const Form& f : kForms)
        if (line.substr(0, f.text.size()) == f.text) {
            if (marker) *marker = std::wstring{f.done ? kMemoDone : kMemoTodo, L' '};
            return f.text.size();
        }
    return 0;
}

std::wstring MemoBulletsShown(std::wstring text, bool round) {
    for (size_t pos = 0; pos < text.size();) {
        size_t end = std::min(text.find_first_of(L"\r\n\v", pos), text.size());
        MemoLineInfo info = ClassifyMemoLine(std::wstring_view(text).substr(pos, end - pos));
        wchar_t& c = text[pos + (size_t)info.indent];
        if (info.kind == MemoLine::Bullet && (round || c == kMemoDot)) c = round ? kMemoDot : L'-';  // "* " stays, unless shown round
        pos = end + 1;
    }
    return text;
}

size_t MemoPrettify(std::wstring_view before, std::wstring* with) {
    struct Swap {
        std::wstring_view from, to;
    };
    // Longer ones first: "<->" is "<-" made "←" with ">" typed after it, "<=>" likewise.
    static constexpr Swap kSwaps[] = {
        {L"←>", L"↔"},  // <-> ↔
        {L"≤>", L"⇔"},  // <=> ⇔
        {L"...", L"…"},      // …
        {L"->", L"→"},       // →
        {L"<-", L"←"},       // ←
        {L"=>", L"⇒"},       // ⇒
        {L"<=", L"≤"},       // ≤
        {L">=", L"≥"},       // ≥
        {L"!=", L"≠"},       // ≠
        {L"+-", L"±"},       // ±
        {L"~=", L"≈"},       // ≈
    };
    for (const Swap& s : kSwaps)
        if (before.size() >= s.from.size() && before.substr(before.size() - s.from.size()) == s.from) {
            if (with) *with = s.to;
            return s.from.size();
        }
    return 0;
}

std::vector<std::pair<size_t, size_t>> FindMemoLinks(std::wstring_view line) {
    std::vector<std::pair<size_t, size_t>> out;
    auto starts = [&](size_t i, std::wstring_view p) {
        if (line.size() - i < p.size()) return false;
        for (size_t k = 0; k < p.size(); ++k)
            if (towlower(line[i + k]) != p[k]) return false;
        return true;
    };
    for (size_t i = 0; i < line.size();) {
        bool boundary = i == 0 || iswspace(line[i - 1]) || std::wstring_view(L"([<\"'").find(line[i - 1]) != std::wstring_view::npos;
        size_t head = !boundary ? 0 : starts(i, L"https://") ? 8 : starts(i, L"http://") ? 7 : starts(i, L"www.") ? 4 : 0;
        if (!head) {
            ++i;
            continue;
        }
        size_t end = i;
        while (end < line.size() && !iswspace(line[end]) && std::wstring_view(L"<>\"").find(line[end]) == std::wstring_view::npos) ++end;
        while (end > i + head && std::wstring_view(L".,;:!?)]}'").find(line[end - 1]) != std::wstring_view::npos) --end;
        if (end > i + head) out.push_back({i, end});
        i = std::max(end, i + 1);
    }
    return out;
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
