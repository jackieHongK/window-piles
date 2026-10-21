#include "unread.h"

#include <cwctype>

namespace wp {

namespace {

// Reads a number at `i` — up to 6 digits, thousands separators allowed ("1,234"), optionally
// followed by '+' ("99+"); returns -1 if there is none.
int ReadNumber(const std::wstring& s, size_t& i) {
    size_t start = i;
    int v = 0, digits = 0;
    while (i < s.size() && digits < 6) {
        if (iswdigit(s[i])) {
            v = v * 10 + (s[i++] - L'0');
            ++digits;
        } else if (s[i] == L',' && digits > 0 && i + 3 < s.size() && iswdigit(s[i + 1]) && iswdigit(s[i + 2]) &&
                   iswdigit(s[i + 3])) {
            ++i;  // thousands separator
        } else {
            break;
        }
    }
    if (i == start) return -1;
    if (i < s.size() && s[i] == L'+') ++i;
    return v;
}

// "(N)" or "[N]" starting at i.
int Bracketed(const std::wstring& s, size_t i) {
    if (i >= s.size() || (s[i] != L'(' && s[i] != L'[')) return -1;
    wchar_t close = s[i] == L'(' ? L')' : L']';
    size_t j = i + 1;
    int v = ReadNumber(s, j);
    return (v >= 0 && j < s.size() && s[j] == close) ? v : -1;
}

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

}  // namespace

UnreadInfo ParseUnread(const std::wstring& title) {
    UnreadInfo r;
    size_t i = 0;
    while (i < title.size() && iswspace(title[i])) ++i;

    // 1) leading "(N)" / "[N]" — the web convention (chat, social, many web apps)
    if (int v = Bracketed(title, i); v > 0) {
        r.count = v;
        return r;
    }
    // 2) Slack-style prefix markers: "! " mention, "* " / "• " unread
    if (i + 1 < title.size() && (title[i] == L'!' || title[i] == L'*' || title[i] == L'\x2022') &&
        iswspace(title[i + 1])) {
        r.marker = true;
        return r;
    }
    // 3) mailbox names followed by "(N)"
    std::wstring low = Lower(title);
    static const wchar_t* boxes[] = {L"inbox", L"받은편지함", L"받은 편지함", L"메일함"};
    for (const wchar_t* box : boxes) {
        size_t p = low.find(box);
        if (p == std::wstring::npos) continue;
        size_t j = p + wcslen(box);
        while (j < low.size() && iswspace(low[j])) ++j;
        if (int v = Bracketed(low, j); v > 0) {
            r.count = v;
            return r;
        }
    }
    // 4) "N new" / "N unread"
    for (size_t k = 0; k < low.size(); ++k) {
        if (!iswdigit(low[k]) || (k > 0 && iswalnum(low[k - 1]))) continue;
        size_t j = k;
        int v = ReadNumber(low, j);
        if (v <= 0) continue;
        while (j < low.size() && iswspace(low[j])) ++j;
        if (low.compare(j, 3, L"new") == 0 || low.compare(j, 6, L"unread") == 0) {
            r.count = v;
            return r;
        }
        k = j;
    }
    return r;
}

}  // namespace wp
