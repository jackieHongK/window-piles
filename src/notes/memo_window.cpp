#include "memo_window.h"

#include <richedit.h>
#include <richole.h>
#include <shellapi.h>
#include <tom.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
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
constexpr UINT kSaveDelayMs = 600;
// Text sizes in twips (1/20 pt), by MemoLine: body, h1, h2, h3, bullet, number.
constexpr LONG kTwips[] = {220, 340, 280, 240, 220, 220};
const COLORREF kPaper = RGB(255, 252, 240);  // a note's warm paper
const COLORREF kBand = RGB(246, 238, 228);    // cream, as on the organizer

struct Memo {
    std::wstring path;
    std::wstring saved;  // the text as last written (RichEdit form: paragraphs end in \r)
    HWND hwnd = nullptr, edit = nullptr;
    ITextDocument* doc = nullptr;  // to keep restyling out of the undo history
    UINT dpi = 96;
    int hoverBtn = 0;  // 1 show in folder, 2 close
    bool styling = false, swallowCr = false;
};

std::vector<Memo*> g_memos;
WNDPROC g_richProc = nullptr;

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

void StyleLine(Memo& m, LONG a, LONG b, const MemoLineInfo& info) {
    bool heading = info.kind == MemoLine::H1 || info.kind == MemoLine::H2 || info.kind == MemoLine::H3;
    CHARRANGE r{a, b + 1};  // with the paragraph mark, so an empty heading line is already big
    SendMessageW(m.edit, EM_EXSETSEL, 0, (LPARAM)&r);
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_SIZE | CFM_BOLD | CFM_COLOR | CFM_FACE;
    cf.yHeight = kTwips[(int)info.kind];
    cf.dwEffects = heading ? CFE_BOLD : 0;
    cf.crTextColor = theme::kForeground;
    wcscpy_s(cf.szFaceName, theme::kFont);
    SendMessageW(m.edit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_SPACEBEFORE | PFM_SPACEAFTER;
    pf.dySpaceBefore = heading && a > 0 ? 160 : 0;
    pf.dySpaceAfter = heading ? 60 : 0;
    SendMessageW(m.edit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    if (info.markerLen) {  // the marker stays in the text (it is the file format), drawn quieter
        r = {a, a + info.markerLen};
        SendMessageW(m.edit, EM_EXSETSEL, 0, (LPARAM)&r);
        cf.dwMask = CFM_COLOR;
        cf.crTextColor = heading ? theme::kBorderStrong : theme::kMutedFg;
        SendMessageW(m.edit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    }
}

// ponytail: restyles every line on each change; fine for note-sized text, per-line if memos get long.
void Restyle(Memo& m) {
    if (m.styling) return;
    m.styling = true;
    std::wstring text = GetText(m.edit);
    if (m.doc) m.doc->Undo(tomSuspend, nullptr);
    LRESULT mask = SendMessageW(m.edit, EM_SETEVENTMASK, 0, 0);
    SendMessageW(m.edit, WM_SETREDRAW, FALSE, 0);
    CHARRANGE sel;
    SendMessageW(m.edit, EM_EXGETSEL, 0, (LPARAM)&sel);
    POINT scroll;
    SendMessageW(m.edit, EM_GETSCROLLPOS, 0, (LPARAM)&scroll);
    for (size_t pos = 0;;) {
        size_t end = std::min(text.find(L'\r', pos), text.size());
        StyleLine(m, (LONG)pos, (LONG)end, ClassifyMemoLine(std::wstring_view(text).substr(pos, end - pos)));
        if (end == text.size()) break;
        pos = end + 1;
    }
    SendMessageW(m.edit, EM_EXSETSEL, 0, (LPARAM)&sel);
    SendMessageW(m.edit, EM_SETSCROLLPOS, 0, (LPARAM)&scroll);
    SendMessageW(m.edit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(m.edit, nullptr, TRUE);
    SendMessageW(m.edit, EM_SETEVENTMASK, 0, mask);
    if (m.doc) m.doc->Undo(tomResume, nullptr);
    m.styling = false;
}

// Enter in a list: the next line gets the next marker; on a line with only its marker, the list ends.
bool OnEnter(Memo& m) {
    CHARRANGE sel;
    SendMessageW(m.edit, EM_EXGETSEL, 0, (LPARAM)&sel);
    if (sel.cpMin != sel.cpMax) return false;
    std::wstring text = GetText(m.edit);
    size_t caret = std::min<size_t>((size_t)sel.cpMin, text.size());
    size_t start = caret ? text.rfind(L'\r', caret - 1) : std::wstring::npos;
    start = start == std::wstring::npos ? 0 : start + 1;
    size_t end = std::min(text.find(L'\r', caret), text.size());
    std::wstring_view line = std::wstring_view(text).substr(start, end - start);
    if (caret < start + ClassifyMemoLine(line).markerLen) return false;  // before the marker: a plain break
    MemoEnter e = MemoOnEnter(line);
    if (e.endList) {
        CHARRANGE r{(LONG)start, (LONG)end};
        SendMessageW(m.edit, EM_EXSETSEL, 0, (LPARAM)&r);
        SendMessageW(m.edit, EM_REPLACESEL, TRUE, (LPARAM)L"");
        return true;
    }
    if (e.nextPrefix.empty()) return false;
    std::wstring ins = L"\r" + e.nextPrefix;
    SendMessageW(m.edit, EM_REPLACESEL, TRUE, (LPARAM)ins.c_str());
    return true;
}

LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (Memo* m = From(GetParent(h))) {
        if (msg == WM_KEYDOWN) m->swallowCr = false;
        if (msg == WM_KEYDOWN && wp == VK_RETURN && GetKeyState(VK_SHIFT) >= 0 && GetKeyState(VK_CONTROL) >= 0 &&
            OnEnter(*m)) {
            m->swallowCr = true;  // the WM_CHAR that TranslateMessage already queued for this Enter
            return 0;
        }
        if (msg == WM_CHAR && wp == L'\r' && m->swallowCr) {
            m->swallowCr = false;
            return 0;
        }
    }
    return CallWindowProcW(g_richProc, h, msg, wp, lp);
}

std::wstring Text(Memo& m) {
    std::wstring t = GetText(m.edit);
    std::replace(t.begin(), t.end(), L'\v', L'\r');  // Shift+Enter's soft break is a plain line in the file
    return t;
}

void Save(Memo& m) {
    KillTimer(m.hwnd, kTimerSave);
    std::wstring text = Text(m);
    if (text == m.saved) return;
    if (!WriteMemo(m.path, text)) {
        Log("memo: could not save %s", Utf8(m.path).c_str());
        return;
    }
    m.saved = text;
    // The file is named after the memo: "2026-10-25 장보기.txt", so it is easy to find outside the app too.
    if (text.find_first_not_of(L" \t\r\n") != std::wstring::npos) m.path = RenameMemoForTitle(m.path, MemoTitle(text));
}

void UpdateTitle(Memo& m) {
    std::wstring t = MemoTitle(Text(m));
    SetWindowTextW(m.hwnd, t.empty() ? L"새 메모" : t.c_str());
    RECT band{0, 0, 1 << 15, Scale(kHeaderH, m.dpi)};
    InvalidateRect(m.hwnd, &band, FALSE);
}

void Close(Memo& m) {
    Save(m);
    if (m.saved.find_first_not_of(L" \t\r\n") == std::wstring::npos) DeleteFileW(m.path.c_str());  // nothing written
    DestroyWindow(m.hwnd);
}

RECT ButtonRect(const Memo& m, int which) {  // 1 show in folder, 2 close
    RECT rc;
    GetClientRect(m.hwnd, &rc);
    int b = Scale(kBtn, m.dpi), top = (Scale(kHeaderH, m.dpi) - b) / 2;
    int right = rc.right - Scale(4, m.dpi) - (which == 1 ? b + Scale(2, m.dpi) : 0);
    return {right - b, top, right, top + b};
}

int ButtonAt(const Memo& m, POINT p) {
    for (int i = 1; i <= 2; ++i) {
        RECT r = ButtonRect(m, i);
        if (PtInRect(&r, p)) return i;
    }
    return 0;
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
    RECT tr{S(kPad), 0, ButtonRect(m, 1).left - S(6), band.bottom};
    c.Text(title, tr, kSubtleFg, S(12), true, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    const wchar_t glyphs[] = {0xE838, 0xE8BB};  // FolderOpen, ChromeClose
    for (int i = 1; i <= 2; ++i) {
        RECT b = ButtonRect(m, i);
        c.FillRoundRect(b, S(kRadiusSm), m.hoverBtn == i ? Mix(kBand, kForeground, 0.08) : kBand);
        c.Glyph(glyphs[i - 1], b, kSubtleFg, S(i == 1 ? 13 : 10));
    }
    if (!HasDwmRoundedCorners()) c.StrokeRoundRect(rc, 0, 1, kBorder);
    c.Blit(dc);
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
        case WM_SIZE:
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
            m->hoverBtn = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            int b = ButtonAt(*m, {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (b == 1) {
                Save(*m);
                RevealInExplorer(m->path);
            } else if (b == 2) {
                Close(*m);
            }
            return 0;
        }
        case WM_COMMAND:
            if ((HWND)lp == m->edit && HIWORD(wp) == EN_CHANGE && !m->styling) {
                Restyle(*m);
                UpdateTitle(*m);
                SetTimer(hwnd, kTimerSave, kSaveDelayMs, nullptr);
            }
            return 0;
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

HWND OpenMemoWindow(const std::wstring& path, POINT anchor) {
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
    RECT r = ClampToWorkArea({x, y, x + w, y + h}, anchor);
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

    SetText(m->edit, ReadMemo(path));
    m->saved = Text(*m);  // the file as the editor holds it, so opening alone never rewrites it
    Restyle(*m);
    UpdateTitle(*m);
    SendMessageW(m->edit, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(m->edit, EM_SETSEL, (WPARAM)-1, -1);
    SendMessageW(m->edit, EM_SETEVENTMASK, 0, ENM_CHANGE);
    RECT rc;
    GetClientRect(m->hwnd, &rc);
    SendMessageW(m->hwnd, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
    ShowWindow(m->hwnd, SW_SHOW);
    SetForegroundWindow(m->hwnd);
    SetFocus(m->edit);
    return m->hwnd;
}

void CloseAllMemoWindows() {
    for (Memo* m : std::vector<Memo*>(g_memos)) Close(*m);
}

void RevealInExplorer(const std::wstring& path) {
    std::wstring args = L"/select,\"" + path + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

}  // namespace wp
