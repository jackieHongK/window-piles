#include "memo_window.h"

#include <richedit.h>
#include <richole.h>
#include <shellapi.h>
#include <tom.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <vector>

#include "common/canvas.h"
#include "common/win_util.h"
#include "notes/memo_format.h"
#include "notes/memo_store.h"
#include "ui/popup_base.h"
#include "ui/theme.h"

namespace wp {

namespace {

// Layout in DIPs.
constexpr int kW = 340, kH = 380, kMinW = 220, kMinH = 160, kHeaderH = 36, kPad = 14, kEdge = 6, kBtn = 28;
constexpr UINT_PTR kTimerSave = 1;
constexpr UINT kSaveDelayMs = 800;          // after typing stops
constexpr UINT kMsgRestyle = WM_APP + 1;    // a composition (Korean, Japanese, …) ended: style what it typed
constexpr int kTabW = 24, kTabH = 26;  // the color tab, as big as the memo's flag tip on the organizer
// Text sizes in twips (1/20 pt), by MemoLine: body, h1, h2, h3, bullet, number, to-do, done.
constexpr LONG kTwips[] = {220, 340, 280, 240, 220, 220, 220, 220};
constexpr LONG kBoxTwips = 260;  // a to-do's box, a little bigger than the text so it is easy to hit
const COLORREF kPaper = RGB(255, 252, 240);  // a note's warm paper
const COLORREF kBand = RGB(246, 238, 228);    // cream, as on the organizer
const COLORREF kLink = RGB(51, 126, 169);     // the blue of the pile colors
constexpr wchar_t kSymbolFont[] = L"Segoe UI Symbol";

enum Button { kBtnNone, kBtnFolder, kBtnClose, kBtnTasks };

struct Memo {
    std::wstring path;
    std::wstring saved;  // the text as last written (RichEdit form: paragraphs end in \r)
    std::wstring shown;  // the text as last styled: what changed since is what gets styled next
    std::wstring title;  // as shown in the title bar and on the organizer's flag
    HWND hwnd = nullptr, edit = nullptr, tab = nullptr;
    COLORREF color = CLR_INVALID;  // its flag's color
    ITextDocument* doc = nullptr;  // to keep restyling out of the undo history
    UINT dpi = 96;
    int hoverBtn = kBtnNone;
    bool styling = false, composing = false;
    wchar_t swallow = 0;  // the WM_CHAR a key handled here has already queued (Enter, Ctrl+Enter, Ctrl+V)
};

std::vector<Memo*> g_memos;
WNDPROC g_richProc = nullptr;
std::function<void()> g_onSaved;

// The memo's flag color tab, sticking out of the window's left side beside its title bar (an owned window,
// so it goes along when the memo is minimized, hidden or closed).
void PlaceTab(Memo& m) {
    if (!m.tab) return;
    if (!IsWindowVisible(m.hwnd) || IsIconic(m.hwnd)) {
        ShowWindow(m.tab, SW_HIDE);
        return;
    }
    if (m.color == CLR_INVALID) {
        m.color = kMemoColors[0].color;
        for (const MemoInfo& i : ListMemos())
            if (_wcsicmp(i.path.c_str(), m.path.c_str()) == 0) m.color = MemoColorOf(i);
    }
    auto S = [&](int v) { return Scale(v, m.dpi); };
    RECT r;
    GetWindowRect(m.hwnd, &r);
    int w = S(kTabW), h = S(kTabH);
    Canvas c(w, h);
    c.FillRoundRect({0, 0, w + S(4), h}, S(2), m.color);  // rounded on the outside only
    POINT at{r.left - w, r.top + (S(kHeaderH) - h) / 2};
    c.Present(m.tab, at);
    SetWindowPos(m.tab, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

HWND MakeTab(HWND owner) {
    WNDCLASSW wc{};
    if (!GetClassInfoW(GetModuleHandleW(nullptr), L"WindowPilesMemoTab", &wc)) {
        wc = {};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"WindowPilesMemoTab";
        RegisterClassW(&wc);
    }
    return CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, L"WindowPilesMemoTab", L"",
                           WS_POPUP, 0, 0, 1, 1, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
}

Memo* From(HWND hwnd) { return (Memo*)GetWindowLongPtrW(hwnd, GWLP_USERDATA); }

std::wstring GetText(HWND edit) {
    GETTEXTLENGTHEX gl{GTL_NUMCHARS | GTL_PRECISE, 1200};
    LONG n = (LONG)SendMessageW(edit, EM_GETTEXTLENGTHEX, (WPARAM)&gl, 0);
    std::wstring s((size_t)std::max<LONG>(0, n) + 1, L'\0');
    GETTEXTEX gt{(DWORD)(s.size() * sizeof(wchar_t)), GT_DEFAULT, 1200, nullptr, nullptr};
    s.resize((size_t)SendMessageW(edit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)s.data()));
    return s;
}

// Streamed in as plain text, so a memo that happens to start with "{\rtf" is not read as RTF.
void SetText(HWND edit, const std::wstring& text) {
    struct Src {
        const std::wstring* s;
        size_t pos;
    } src{&text, 0};
    EDITSTREAM es{};
    es.dwCookie = (DWORD_PTR)&src;
    es.pfnCallback = [](DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG* got) -> DWORD {
        auto* s = (Src*)cookie;
        size_t bytes = std::min<size_t>((size_t)cb & ~(size_t)1, (s->s->size() - s->pos) * sizeof(wchar_t));
        memcpy(buf, s->s->data() + s->pos, bytes);
        s->pos += bytes / sizeof(wchar_t);
        *got = (LONG)bytes;
        return 0;
    };
    SendMessageW(edit, EM_STREAMIN, SF_TEXT | SF_UNICODE, (LPARAM)&es);
}

void Format(Memo& m, LONG a, LONG b, const CHARFORMAT2W& cf) {
    CHARRANGE r{a, b};
    SendMessageW(m.edit, EM_EXSETSEL, 0, (LPARAM)&r);
    SendMessageW(m.edit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

void StyleLine(Memo& m, LONG a, LONG b, std::wstring_view line) {
    using namespace theme;
    MemoLineInfo info = ClassifyMemoLine(line);
    bool heading = info.kind == MemoLine::H1 || info.kind == MemoLine::H2 || info.kind == MemoLine::H3;
    bool task = info.kind == MemoLine::Task || info.kind == MemoLine::Done, done = info.kind == MemoLine::Done;
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_SIZE | CFM_BOLD | CFM_COLOR | CFM_FACE | CFM_LINK | CFM_UNDERLINE | CFM_STRIKEOUT;
    cf.yHeight = kTwips[(int)info.kind];
    cf.dwEffects = (heading ? CFE_BOLD : 0) | (done ? CFE_STRIKEOUT : 0);  // a done to-do: struck through, quieter
    cf.crTextColor = done ? kMutedFg : kForeground;
    wcscpy_s(cf.szFaceName, kFont);
    Format(m, a, b + 1, cf);  // with the paragraph mark, so an empty heading line is already big
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_SPACEBEFORE | PFM_SPACEAFTER;
    pf.dySpaceBefore = heading && a > 0 ? 160 : 0;
    pf.dySpaceAfter = heading ? 60 : 0;
    SendMessageW(m.edit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    if (task) {  // the box: a symbol a little bigger than the text, walnut once checked
        CHARFORMAT2W box{};
        box.cbSize = sizeof(box);
        box.dwMask = CFM_SIZE | CFM_COLOR | CFM_FACE | CFM_STRIKEOUT;
        box.yHeight = kBoxTwips;
        box.crTextColor = done ? kBrand : kSubtleFg;
        wcscpy_s(box.szFaceName, kSymbolFont);
        Format(m, a, a + 1, box);
    } else if (info.markerLen) {  // the marker stays in the text (it is the file format), drawn quieter
        cf.dwMask = CFM_COLOR;
        cf.crTextColor = heading ? kBorderStrong : kMutedFg;
        Format(m, a, a + info.markerLen, cf);
    }
    for (auto [s, e] : FindMemoLinks(line)) {  // web addresses: blue, underlined, opened by a click
        CHARFORMAT2W link{};
        link.cbSize = sizeof(link);
        link.dwMask = CFM_LINK | CFM_UNDERLINE | CFM_COLOR;
        link.dwEffects = CFE_LINK | CFE_UNDERLINE;
        link.crTextColor = kLink;
        Format(m, a + (LONG)s, a + (LONG)e, link);
    }
}

// Styles the lines holding text[from, to) (a change, or all of it): only those, so typing in a long memo costs
// what typing in a short one does. Kept out of the undo history; the selection and scroll stay as they were.
void Restyle(Memo& m, const std::wstring& text, size_t from, size_t to) {
    if (m.styling) return;
    m.styling = true;
    if (m.doc) m.doc->Undo(tomSuspend, nullptr);
    LRESULT mask = SendMessageW(m.edit, EM_SETEVENTMASK, 0, 0);
    SendMessageW(m.edit, WM_SETREDRAW, FALSE, 0);
    CHARRANGE sel;
    SendMessageW(m.edit, EM_EXGETSEL, 0, (LPARAM)&sel);
    POINT scroll;
    SendMessageW(m.edit, EM_GETSCROLLPOS, 0, (LPARAM)&scroll);
    from = std::min(from, text.size());
    size_t pos = from ? text.rfind(L'\r', from - 1) : std::wstring::npos;
    pos = pos == std::wstring::npos ? 0 : pos + 1;
    size_t first = pos;
    for (;;) {
        size_t end = std::min(text.find(L'\r', pos), text.size());
        StyleLine(m, (LONG)pos, (LONG)end, std::wstring_view(text).substr(pos, end - pos));
        if (end >= to || end == text.size()) break;
        pos = end + 1;
    }
    SendMessageW(m.edit, EM_EXSETSEL, 0, (LPARAM)&sel);
    SendMessageW(m.edit, EM_SETSCROLLPOS, 0, (LPARAM)&scroll);
    SendMessageW(m.edit, WM_SETREDRAW, TRUE, 0);
    // Painted again from the first of those lines down (a heading's size moves what is below it), not erased
    // first, so nothing flickers.
    POINTL at{};
    SendMessageW(m.edit, EM_POSFROMCHAR, (WPARAM)&at, (LPARAM)first);
    RECT rc;
    GetClientRect(m.edit, &rc);
    rc.top = std::clamp<LONG>(at.y - 4, 0, rc.bottom);
    InvalidateRect(m.edit, &rc, FALSE);
    SendMessageW(m.edit, EM_SETEVENTMASK, 0, mask);
    if (m.doc) m.doc->Undo(tomResume, nullptr);
    m.styling = false;
}

// Where `now` differs from `before`: [from, to) in `now`.
void ChangedRange(const std::wstring& before, const std::wstring& now, size_t& from, size_t& to) {
    size_t n = std::min(before.size(), now.size()), a = 0, b = 0;
    while (a < n && before[a] == now[a]) ++a;
    while (b < n - a && before[before.size() - 1 - b] == now[now.size() - 1 - b]) ++b;
    from = a;
    to = now.size() - b;
}

// The line around `cp`: [start, end) in `text`.
void LineAt(const std::wstring& text, size_t cp, size_t& start, size_t& end) {
    cp = std::min(cp, text.size());
    start = cp ? text.rfind(L'\r', cp - 1) : std::wstring::npos;
    start = start == std::wstring::npos ? 0 : start + 1;
    end = std::min(text.find(L'\r', cp), text.size());
}

void Replace(Memo& m, LONG a, LONG b, const std::wstring& with) {
    CHARRANGE r{a, b};
    SendMessageW(m.edit, EM_EXSETSEL, 0, (LPARAM)&r);
    SendMessageW(m.edit, EM_REPLACESEL, TRUE, (LPARAM)with.c_str());
}

// "[] " typed at the start of a line: it becomes a to-do box ("[x] " a checked one). True if it did.
bool TaskShortcut(Memo& m, const std::wstring& text) {
    CHARRANGE sel;
    SendMessageW(m.edit, EM_EXGETSEL, 0, (LPARAM)&sel);
    if (sel.cpMin != sel.cpMax) return false;
    size_t start, end;
    LineAt(text, (size_t)sel.cpMin, start, end);
    std::wstring marker;
    size_t n = MemoTaskShortcut(std::wstring_view(text).substr(start, end - start), &marker);
    if (!n || (size_t)sel.cpMin < start + n) return false;  // only once its space is typed
    Replace(m, (LONG)start, (LONG)(start + n), marker);
    LONG caret = sel.cpMin - (LONG)(n - marker.size());
    CHARRANGE back{caret, caret};
    SendMessageW(m.edit, EM_EXSETSEL, 0, (LPARAM)&back);
    return true;
}

std::wstring Text(Memo& m) {
    std::wstring t = GetText(m.edit);
    std::replace(t.begin(), t.end(), L'\v', L'\r');  // Shift+Enter's soft break is a plain line in the file
    return t;
}

void UpdateTitle(Memo& m, std::wstring text) {
    std::replace(text.begin(), text.end(), L'\v', L'\r');
    std::wstring t = MemoTitle(text);
    if (t == m.title && !t.empty()) return;  // most keys don't change it: the title bar stays as it is
    m.title = t;
    SetWindowTextW(m.hwnd, t.empty() ? L"새 메모" : t.c_str());
    RECT band{0, 0, 1 << 15, Scale(kHeaderH, m.dpi)};
    InvalidateRect(m.hwnd, &band, FALSE);
}

// What changed is styled (not while a composition is going on: what it types is styled once it's done), the
// title follows, and it is saved a moment after typing stops.
void OnChange(Memo& m) {
    std::wstring text = GetText(m.edit);
    if (!m.composing) {
        if (TaskShortcut(m, text)) text = GetText(m.edit);
        if (text != m.shown) {
            size_t from, to;
            ChangedRange(m.shown, text, from, to);
            Restyle(m, text, from, to);
            m.shown = text;
        }
    }
    UpdateTitle(m, text);
    SetTimer(m.hwnd, kTimerSave, kSaveDelayMs, nullptr);
}

// Enter in a list: the next line gets the next marker; on a line with only its marker, the list ends.
bool OnEnter(Memo& m) {
    CHARRANGE sel;
    SendMessageW(m.edit, EM_EXGETSEL, 0, (LPARAM)&sel);
    if (sel.cpMin != sel.cpMax) return false;
    std::wstring text = GetText(m.edit);
    size_t caret = std::min<size_t>((size_t)sel.cpMin, text.size()), start, end;
    LineAt(text, caret, start, end);
    std::wstring_view line = std::wstring_view(text).substr(start, end - start);
    if (caret < start + ClassifyMemoLine(line).markerLen) return false;  // before the marker: a plain break
    MemoEnter e = MemoOnEnter(line);
    if (e.endList) {
        Replace(m, (LONG)start, (LONG)end, L"");
        return true;
    }
    if (e.nextPrefix.empty()) return false;
    std::wstring ins = L"\r" + e.nextPrefix;
    SendMessageW(m.edit, EM_REPLACESEL, TRUE, (LPARAM)ins.c_str());
    return true;
}

// Checks a to-do or unchecks it: its box at `start`, the beginning of its line.
void ToggleTask(Memo& m, LONG start, wchar_t box) {
    CHARRANGE sel;
    SendMessageW(m.edit, EM_EXGETSEL, 0, (LPARAM)&sel);
    Replace(m, start, start + 1, std::wstring(1, box == kMemoTodo ? kMemoDone : kMemoTodo));
    SendMessageW(m.edit, EM_EXSETSEL, 0, (LPARAM)&sel);  // one character for one: the caret stays put
}

// The checklist button (and Ctrl+Enter on a line that isn't one): the selected lines become to-dos, a list's
// marker turned into a box; when the first of them is a to-do already, they all go back to plain lines.
void MakeTasks(Memo& m) {
    std::wstring text = GetText(m.edit);
    CHARRANGE sel;
    SendMessageW(m.edit, EM_EXGETSEL, 0, (LPARAM)&sel);
    std::vector<size_t> starts;
    size_t start, end;
    LineAt(text, (size_t)sel.cpMin, start, end);
    for (;;) {
        starts.push_back(start);
        if (end >= (size_t)sel.cpMax || end >= text.size()) break;
        start = end + 1;
        end = std::min(text.find(L'\r', start), text.size());
    }
    auto kindAt = [&](size_t s) {
        size_t e = std::min(text.find(L'\r', s), text.size());
        return ClassifyMemoLine(std::wstring_view(text).substr(s, e - s));
    };
    MemoLine first = kindAt(starts[0]).kind;
    bool undo = first == MemoLine::Task || first == MemoLine::Done;
    const std::wstring box{kMemoTodo, L' '};
    LONG shift = 0, total = 0;  // how far the selection's start (on the first line) and end move
    for (size_t i = starts.size(); i-- > 0;) {  // from the last line up, so the earlier positions still hold
        MemoLineInfo info = kindAt(starts[i]);
        bool task = info.kind == MemoLine::Task || info.kind == MemoLine::Done;
        bool list = info.kind == MemoLine::Bullet || info.kind == MemoLine::Number;
        LONG a = (LONG)starts[i], b = a + ((undo && task) || (!undo && list) ? info.markerLen : 0);
        if (undo && !task) continue;
        if (!undo && task) continue;
        std::wstring with = undo ? L"" : box;
        Replace(m, a, b, with);
        total += (LONG)with.size() - (b - a);
        if (i == 0) shift = (LONG)with.size() - (b - a);
    }
    LONG caret = std::max<LONG>((LONG)starts[0], sel.cpMin + shift);
    CHARRANGE back{caret, sel.cpMin == sel.cpMax ? caret : sel.cpMax + total};
    SendMessageW(m.edit, EM_EXSETSEL, 0, (LPARAM)&back);
}

// The to-do box under a point in the editor (client coords): its position, or -1.
LONG BoxAt(HWND edit, POINT p, wchar_t* box) {
    POINTL pl{p.x, p.y};
    LONG cp = (LONG)SendMessageW(edit, EM_CHARFROMPOS, 0, (LPARAM)&pl);
    if (cp < 0) return -1;
    wchar_t two[3] = {};
    TEXTRANGEW tr{{std::max<LONG>(0, cp - 1), cp + 1}, two};
    LONG got = (LONG)SendMessageW(edit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
    // The character there, and the one before it, which must be a line break (or nothing): a box starts its line.
    wchar_t at = cp == 0 ? two[0] : got >= 2 ? two[1] : 0, before = cp == 0 ? L'\r' : two[0];
    if ((at != kMemoTodo && at != kMemoDone) || (before != L'\r' && before != L'\v')) return -1;
    // Only on the box itself, not anywhere a click lands nearest to it.
    POINTL o{};
    SendMessageW(edit, EM_POSFROMCHAR, (WPARAM)&o, cp);
    if (p.x < o.x - 2 || p.x > o.x + MulDiv(kBoxTwips, GetDpiForWindow(edit), 1440) + 2) return -1;
    if (box) *box = at;
    return cp;
}

LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (Memo* m = From(GetParent(h))) {
        bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
        switch (msg) {
            case WM_KEYDOWN:
                m->swallow = 0;
                if (wp == VK_RETURN && ctrl && !shift) {  // Ctrl+Enter: the to-do checked or unchecked, or made one
                    std::wstring text = GetText(h);
                    CHARRANGE sel;
                    SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&sel);
                    size_t start, end;
                    LineAt(text, (size_t)sel.cpMin, start, end);
                    MemoLine kind = ClassifyMemoLine(std::wstring_view(text).substr(start, end - start)).kind;
                    if (kind == MemoLine::Task || kind == MemoLine::Done) ToggleTask(*m, (LONG)start, text[start]);
                    else MakeTasks(*m);
                    m->swallow = L'\n';
                    return 0;
                }
                if (wp == VK_RETURN && !shift && !ctrl && OnEnter(*m)) {
                    m->swallow = L'\r';  // the WM_CHAR that TranslateMessage already queued for this Enter
                    return 0;
                }
                if ((wp == 'V' && ctrl && !shift) || (wp == VK_INSERT && shift && !ctrl)) {
                    SendMessageW(h, EM_PASTESPECIAL, CF_UNICODETEXT, 0);  // plain text: no fonts or colors from elsewhere
                    m->swallow = 0x16;
                    return 0;
                }
                break;
            case WM_CHAR:
                if (m->swallow && (wchar_t)wp == m->swallow) {
                    m->swallow = 0;
                    return 0;
                }
                break;
            case WM_PASTE:
                SendMessageW(h, EM_PASTESPECIAL, CF_UNICODETEXT, 0);
                return 0;
            case WM_IME_STARTCOMPOSITION:
                m->composing = true;
                break;
            case WM_IME_ENDCOMPOSITION:
                m->composing = false;
                PostMessageW(m->hwnd, kMsgRestyle, 0, 0);
                break;
            case WM_LBUTTONDOWN: {  // a click on a to-do's box checks it (the caret stays where it was)
                wchar_t box = 0;
                LONG at = BoxAt(h, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}, &box);
                if (at >= 0) {
                    SetFocus(h);
                    ToggleTask(*m, at, box);
                    return 0;
                }
                break;
            }
            case WM_SETCURSOR: {
                POINT p;
                GetCursorPos(&p);
                ScreenToClient(h, &p);
                if (LOWORD(lp) == HTCLIENT && BoxAt(h, p, nullptr) >= 0) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
                break;
            }
        }
    }
    return CallWindowProcW(g_richProc, h, msg, wp, lp);
}

void Save(Memo& m) {
    KillTimer(m.hwnd, kTimerSave);
    std::wstring text = Text(m);
    if (text == m.saved) return;
    if (!WriteMemo(m.path, text)) {
        Log("memo: could not save %s", Utf8(m.path).c_str());
        return;
    }
    std::wstring before = MemoTitle(m.saved);
    m.saved = text;
    std::wstring title = MemoTitle(text);
    // The file is named after the memo: "2026-10-25 장보기.txt", so it is easy to find outside the app too.
    if (text.find_first_not_of(L" \t\r\n") != std::wstring::npos) m.path = RenameMemoForTitle(m.path, title);
    // The organizer's flag shows the new title right away; only then, since reading the memos again for it
    // costs more than a save.
    if (title != before && g_onSaved) g_onSaved();
}

void Close(Memo& m) {
    Save(m);
    if (m.saved.find_first_not_of(L" \t\r\n") == std::wstring::npos) {  // nothing written
        DeleteFileW(m.path.c_str());
        if (g_onSaved) g_onSaved();
    }
    DestroyWindow(m.hwnd);
}

RECT ButtonRect(const Memo& m, int which) {  // kBtnTasks, kBtnFolder, kBtnClose from the left
    RECT rc;
    GetClientRect(m.hwnd, &rc);
    int b = Scale(kBtn, m.dpi), top = (Scale(kHeaderH, m.dpi) - b) / 2, step = b + Scale(2, m.dpi);
    int right = rc.right - Scale(4, m.dpi) - (which == kBtnFolder ? step : which == kBtnTasks ? 2 * step : 0);
    return {right - b, top, right, top + b};
}

int ButtonAt(const Memo& m, POINT p) {
    for (int i : {kBtnFolder, kBtnClose, kBtnTasks}) {
        RECT r = ButtonRect(m, i);
        if (PtInRect(&r, p)) return i;
    }
    return kBtnNone;
}

void Paint(Memo& m, HDC dc) {
    using namespace theme;
    auto S = [&](int v) { return Scale(v, m.dpi); };
    RECT rc;
    GetClientRect(m.hwnd, &rc);
    Canvas c(rc.right, rc.bottom);
    c.FillRoundRect(rc, 0, kPaper);
    RECT band{0, 0, rc.right, S(kHeaderH)};
    c.FillRoundRect(band, 0, kBand);
    wchar_t title[128] = {};
    GetWindowTextW(m.hwnd, title, 128);
    RECT tr{S(kPad), 0, ButtonRect(m, kBtnTasks).left - S(6), band.bottom};
    c.Text(title, tr, kSubtleFg, S(12), true, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    struct {
        int which;
        wchar_t glyph;
        int size;
    } buttons[] = {{kBtnTasks, 0xE73A, 13}, {kBtnFolder, 0xE838, 13}, {kBtnClose, 0xE8BB, 10}};  // CheckboxComposite, FolderOpen, ChromeClose
    for (auto& btn : buttons) {
        RECT b = ButtonRect(m, btn.which);
        c.FillRoundRect(b, S(kRadiusSm), m.hoverBtn == btn.which ? Mix(kBand, kForeground, 0.08) : kBand);
        c.Glyph(btn.glyph, b, kSubtleFg, S(btn.size));
    }
    if (!HasDwmRoundedCorners()) c.StrokeRoundRect(rc, 0, 1, kBorder);
    c.Blit(dc);
}

// Ctrl+click on a web address in the memo opens it (a plain click puts the caret there, to edit it).
void OpenLink(Memo& m, const ENLINK& el) {
    std::wstring url((size_t)std::max<LONG>(0, el.chrg.cpMax - el.chrg.cpMin) + 1, L'\0');
    TEXTRANGEW tr{el.chrg, url.data()};
    url.resize((size_t)SendMessageW(m.edit, EM_GETTEXTRANGE, 0, (LPARAM)&tr));
    auto links = FindMemoLinks(url);  // only what the memo itself marks as a web address
    if (links.empty() || links[0].first != 0) return;
    url = url.substr(0, links[0].second);
    if (_wcsnicmp(url.c_str(), L"www.", 4) == 0) url = L"https://" + url;
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Memo* m = From(hwnd);
    if (!m) return DefWindowProcW(hwnd, msg, wp, lp);
    auto S = [&](int v) { return Scale(v, m->dpi); };
    switch (msg) {
        case WM_NCHITTEST: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int e = S(kEdge);
            bool l = p.x < e, r = p.x >= rc.right - e, t = p.y < e, b = p.y >= rc.bottom - e;
            if (t && l) return HTTOPLEFT;
            if (t && r) return HTTOPRIGHT;
            if (b && l) return HTBOTTOMLEFT;
            if (b && r) return HTBOTTOMRIGHT;
            if (l) return HTLEFT;
            if (r) return HTRIGHT;
            if (t) return HTTOP;
            if (b) return HTBOTTOM;
            if (p.y < S(kHeaderH) && !ButtonAt(*m, p)) return HTCAPTION;
            return HTCLIENT;
        }
        case WM_GETMINMAXINFO:
            ((MINMAXINFO*)lp)->ptMinTrackSize = {S(kMinW), S(kMinH)};
            return 0;
        case WM_MOVE:
            PlaceTab(*m);
            return 0;
        case WM_SHOWWINDOW:
            PostMessageW(hwnd, WM_MOVE, 0, 0);  // the tab follows once the window is shown or hidden
            break;
        case WM_SIZE:
            PlaceTab(*m);
            MoveWindow(m->edit, S(kPad), S(kHeaderH) + S(8), std::max(0, LOWORD(lp) - S(kPad) - S(kEdge)),
                       std::max(0, HIWORD(lp) - S(kHeaderH) - S(8) - S(kEdge) - S(4)), TRUE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            Paint(*m, dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            int b = ButtonAt(*m, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (b != m->hoverBtn) {
                m->hoverBtn = b;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            m->hoverBtn = kBtnNone;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            int b = ButtonAt(*m, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (b == kBtnFolder) {
                Save(*m);
                RevealInExplorer(m->path);
            } else if (b == kBtnClose) {
                Close(*m);
            } else if (b == kBtnTasks) {
                SetFocus(m->edit);
                MakeTasks(*m);
            }
            return 0;
        }
        case WM_COMMAND:
            if ((HWND)lp == m->edit && HIWORD(wp) == EN_CHANGE && !m->styling) OnChange(*m);
            return 0;
        case kMsgRestyle:
            if (!m->composing) OnChange(*m);
            return 0;
        case WM_NOTIFY: {
            auto* nm = (NMHDR*)lp;
            if (nm->hwndFrom != m->edit || nm->code != EN_LINK) return 0;
            const ENLINK& el = *(ENLINK*)lp;
            bool ctrl = GetKeyState(VK_CONTROL) < 0;
            if (el.msg == WM_SETCURSOR) {  // the hand only while Ctrl is held: otherwise it is text like any other
                if (ctrl) return 0;
                SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
                return 1;
            }
            if (ctrl && el.msg == WM_LBUTTONDOWN) {  // opened on the press, before Ctrl+click would select a word
                OpenLink(*m, el);
                return 1;
            }
            if (ctrl && el.msg == WM_LBUTTONUP) return 1;
            return 0;
        }
        case WM_TIMER:
            if (wp == kTimerSave) Save(*m);
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) != WA_INACTIVE) SetFocus(m->edit);
            return 0;
        case WM_CLOSE:
            Close(*m);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            g_memos.erase(std::remove(g_memos.begin(), g_memos.end(), m), g_memos.end());
            if (m->doc) m->doc->Release();
            delete m;
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND OpenMemoWindow(const std::wstring& path, POINT anchor, const RECT* at) {
    for (Memo* m : g_memos)
        if (_wcsicmp(m->path.c_str(), path.c_str()) == 0) {
            SetForegroundWindow(m->hwnd);
            return m->hwnd;
        }
    static HMODULE richEdit = LoadLibraryW(L"Msftedit.dll");  // RichEdit 4.1+, part of Windows
    if (!richEdit) return nullptr;
    auto* m = new Memo;
    m->path = path;
    m->dpi = DpiForPoint(anchor);
    int w = Scale(kW, m->dpi), h = Scale(kH, m->dpi), gap = Scale(24, m->dpi);
    RECT work = WorkAreaForPoint(anchor);
    // Beside the tray, toward the middle of the screen; a few open at once step down a little.
    int x = anchor.x > (work.left + work.right) / 2 ? anchor.x - w - gap : anchor.x + gap;
    int y = anchor.y - h / 2 + (int)g_memos.size() * gap;
    RECT r = at ? *at : ClampToWorkArea({x, y, x + w, y + h}, anchor);
    m->hwnd = CreateWindowExW(WS_EX_APPWINDOW, RegisterPopupClass(L"WindowPilesMemo", Proc), L"새 메모",
                              WS_POPUP | WS_CLIPCHILDREN | WS_MINIMIZEBOX, r.left, r.top, r.right - r.left,
                              r.bottom - r.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!m->hwnd) {
        delete m;
        return nullptr;
    }
    ApplyRoundedCorners(m->hwnd);
    m->edit = CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL,
                              0, 0, 1, 1, m->hwnd, (HMENU)1, GetModuleHandleW(nullptr), nullptr);
    if (!g_richProc) g_richProc = (WNDPROC)GetWindowLongPtrW(m->edit, GWLP_WNDPROC);
    SetWindowLongPtrW(m->edit, GWLP_WNDPROC, (LONG_PTR)EditProc);
    SendMessageW(m->edit, EM_EXLIMITTEXT, 0, 4 << 20);  // the default is 32K characters
    SendMessageW(m->edit, EM_SETBKGNDCOLOR, 0, kPaper);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_SIZE | CFM_COLOR | CFM_FACE;
    cf.yHeight = kTwips[0];
    cf.crTextColor = theme::kForeground;
    wcscpy_s(cf.szFaceName, theme::kFont);
    SendMessageW(m->edit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    IRichEditOle* ole = nullptr;
    if (SendMessageW(m->edit, EM_GETOLEINTERFACE, 0, (LPARAM)&ole) && ole) {
        ole->QueryInterface(__uuidof(ITextDocument), (void**)&m->doc);
        ole->Release();
    }
    SetWindowLongPtrW(m->hwnd, GWLP_USERDATA, (LONG_PTR)m);
    g_memos.push_back(m);
    m->tab = MakeTab(m->hwnd);

    SetText(m->edit, ReadMemo(path));
    m->saved = Text(*m);  // the file as the editor holds it, so opening alone never rewrites it
    m->shown = GetText(m->edit);
    Restyle(*m, m->shown, 0, m->shown.size());
    UpdateTitle(*m, m->shown);
    SendMessageW(m->edit, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(m->edit, EM_SETSEL, (WPARAM)-1, -1);
    SendMessageW(m->edit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_LINK);
    RECT rc;
    GetClientRect(m->hwnd, &rc);
    SendMessageW(m->hwnd, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
    ShowWindow(m->hwnd, SW_SHOW);
    PlaceTab(*m);
    SetForegroundWindow(m->hwnd);
    SetFocus(m->edit);
    return m->hwnd;
}

SIZE MemoWindowSize(UINT dpi) { return {Scale(kW, dpi), Scale(kH, dpi)}; }

int OpenMemoCount() { return (int)g_memos.size(); }

void OnMemoSaved(std::function<void()> handler) { g_onSaved = std::move(handler); }

void RefreshMemoTabs() {
    for (Memo* m : g_memos) {
        m->color = CLR_INVALID;  // read again
        PlaceTab(*m);
    }
}

void CloseAllMemoWindows() {
    for (Memo* m : std::vector<Memo*>(g_memos)) Close(*m);
}

void RevealInExplorer(const std::wstring& path) {
    std::wstring args = L"/select,\"" + path + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

}  // namespace wp
