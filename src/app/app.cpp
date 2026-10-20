#include "app.h"

#include <dwmapi.h>

#include <algorithm>
#include <cwctype>
#include <map>

#include "app/app_icon.h"
#include "common/win_util.h"
#include "recovery/journal.h"

namespace wp {

namespace {

constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT WM_APP_MEMBER_GONE = WM_APP + 2;
constexpr UINT WM_APP_MEMBER_SHOWN = WM_APP + 3;
constexpr UINT_PTR kTimerVerify = 1;
constexpr UINT_PTR kTimerIdle = 2;
constexpr UINT kTrayId = 1;

enum MenuId : UINT {
    kMenuToggle = 100,
    kMenuDissolve,
    kMenuRestoreAll,
    kMenuHelp,
    kMenuQuit,
};

const COLORREF kPalette[] = {RGB(66, 133, 244), RGB(234, 67, 53),  RGB(52, 168, 83),
                             RGB(245, 124, 0),  RGB(142, 68, 173), RGB(0, 150, 136)};

const wchar_t kHelpText[] =
    L"창 제목줄을 잡고 끌면 화면 위쪽에 '＋ 새 더미'가 나타나요. 거기에 놓으면 창이 더미에 들어가요.\n"
    L"더미를 한 번 누르면 펼쳐지고, 다시 누르면 접혀요. 다른 창을 더미 위에 놓으면 같이 들어가요.";

std::wstring FriendlyAppName(const std::wstring& exe) {
    static const std::map<std::wstring, std::wstring> names = {
        {L"chrome.exe", L"크롬"},        {L"msedge.exe", L"엣지"},         {L"explorer.exe", L"탐색기"},
        {L"notepad.exe", L"메모장"},     {L"excel.exe", L"엑셀"},          {L"winword.exe", L"워드"},
        {L"powerpnt.exe", L"파워포인트"}, {L"code.exe", L"VS Code"},        {L"windowsterminal.exe", L"터미널"},
        {L"kakaotalk.exe", L"카카오톡"},  {L"firefox.exe", L"파이어폭스"}, {L"outlook.exe", L"아웃룩"},
        {L"olk.exe", L"아웃룩"},          {L"hwp.exe", L"한글"},            {L"applicationframehost.exe", L"앱"},
    };
    std::wstring lower = exe;
    for (auto& ch : lower) ch = (wchar_t)towlower(ch);
    auto it = names.find(lower);
    if (it != names.end()) return it->second;
    std::wstring base = exe;
    size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos) base = base.substr(0, dot);
    return base.empty() ? L"창" : base;
}

bool IsShellWindow(HWND h) {
    static const wchar_t* classes[] = {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd",
                                       L"Windows.UI.Core.CoreWindow", L"XamlExplorerHostIslandWindow"};
    std::wstring cls = WindowClass(h);
    for (auto* c : classes)
        if (cls == c) return true;
    return false;
}

DWORD IntegrityLevel(HANDLE token) {
    DWORD size = 0;
    GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
    std::vector<BYTE> buf(size);
    if (!size || !GetTokenInformation(token, TokenIntegrityLevel, buf.data(), size, &size)) return 0;
    auto* tml = (TOKEN_MANDATORY_LABEL*)buf.data();
    return *GetSidSubAuthority(tml->Label.Sid, *GetSidSubAuthorityCount(tml->Label.Sid) - 1);
}

// True when we can't safely control the process (elevated/protected): UIPI would block us anyway.
bool IsHigherIntegrity(DWORD pid) {
    static DWORD ours = [] {
        HANDLE t = nullptr;
        DWORD il = 0;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) il = IntegrityLevel(t), CloseHandle(t);
        return il;
    }();
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return true;
    HANDLE t = nullptr;
    bool higher = true;
    if (OpenProcessToken(p, TOKEN_QUERY, &t)) {
        higher = IntegrityLevel(t) > ours;
        CloseHandle(t);
    }
    CloseHandle(p);
    return higher;
}

void TrimNul(wchar_t* dst, size_t cap, const std::wstring& src) {
    wcsncpy_s(dst, cap, src.c_str(), _TRUNCATE);
}

}  // namespace

App* App::instance_ = nullptr;

App::App() { instance_ = this; }

App::~App() {
    if (objectHook_) UnhookWinEvent(objectHook_);
    drag_.reset();
    piles_.clear();
    if (nid_.cbSize) Shell_NotifyIconW(NIM_DELETE, &nid_);
    if (main_) DestroyWindow(main_);
    if (icon_) DestroyIcon(icon_);
    instance_ = nullptr;
}

int App::Run(int recoveredAtStart) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"WindowPilesMain";
    RegisterClassW(&wc);
    // A hidden (never shown) top-level window: tray callbacks, menus, deferred work.
    main_ = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Window Piles", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                            wc.hInstance, nullptr);
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    icon_ = MakeAppIcon(GetSystemMetrics(SM_CXSMICON) * 2);
    AddTrayIcon();

    drag_ = std::make_unique<DragWatcher>(*this);
    // Member windows closing or showing themselves. Out-of-context: no injection.
    objectHook_ = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_SHOW, nullptr, OnObjectEvent, 0, 0,
                                  WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    SetTimer(main_, kTimerIdle, 10000, nullptr);
    SaveJournal();

    std::wstring flag = AppDataDir() + L"\\firstrun.done";
    if (recoveredAtStart > 0) {
        Notify(L"창을 되찾았어요", L"지난번에 숨겨져 있던 창 " + std::to_wstring(recoveredAtStart) + L"개를 다시 보이게 했어요.");
    } else if (GetFileAttributesW(flag.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Notify(L"창 더미가 켜졌어요", kHelpText);
        HANDLE f = CreateFileW(flag.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    } else {
        Notify(L"창 더미가 켜졌어요", L"창을 끌어서 화면 위쪽 '＋ 새 더미'에 놓아 보세요.");
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Eligibility and drag callbacks
// ---------------------------------------------------------------------------------------------

bool App::CanPile(HWND h) {
    if (!IsWindow(h) || !IsWindowVisible(h) || GetAncestor(h, GA_ROOT) != h) return false;
    if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;
    HWND owner = GetWindow(h, GW_OWNER);
    if (owner && IsWindowVisible(owner)) return false;  // dialogs belong to their main window
    if (IsShellWindow(h)) return false;
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId()) return false;
    if (IsHigherIntegrity(pid)) return false;  // e.g. apps running as administrator
    return true;
}

void App::OnDragHover(HWND, POINT cursor) {
    Pile* over = PileAt(cursor);
    for (auto& p : piles_) p->view->SetHot(p.get() == over);
}

void App::OnDragEnd() {
    for (auto& p : piles_) p->view->SetHot(false);
}

void App::OnDragDrop(HWND hwnd, const WINDOWPLACEMENT& before, POINT cursor, bool onBar) {
    Pile* target = onBar ? nullptr : PileAt(cursor);
    if (!onBar && !target) return;  // an ordinary window move
    Pile* owner = PileOf(hwnd);
    if (owner && owner == target) {
        // Dropped back onto its own pile: put it back where it was / away again.
        Member* m = owner->Find(hwnd);
        m->placement = before;
        if (owner->folded) HideMembers(*owner, {hwnd}, false);
        else RestoreWindow(hwnd, before, false);
        return;
    }
    if (owner) {
        auto& ms = owner->members;
        ms.erase(std::remove_if(ms.begin(), ms.end(), [&](const Member& m) { return m.hwnd == hwnd; }), ms.end());
        if (ms.empty()) RemovePile(*owner);
        else Refresh(*owner);
    }
    if (!target) target = &CreatePile(cursor);

    Member m;
    m.hwnd = hwnd;
    GetWindowThreadProcessId(hwnd, &m.pid);
    m.createTime = ProcessCreateTime(m.pid);
    m.className = WindowClass(hwnd);
    m.title = WindowTitle(hwnd);
    m.placement = before;  // come back to where it was before the drag, not to the drop spot
    target->members.push_back(std::move(m));

    if (target->folded) {
        HideMembers(*target, {hwnd}, false);
    } else {
        RestoreWindow(hwnd, before, false);
        Refresh(*target);
    }
}

// ---------------------------------------------------------------------------------------------
// Pile callbacks
// ---------------------------------------------------------------------------------------------

void App::OnPileClick(Pile& pile) {
    if (pile.folded) Expand(pile, true);
    else Fold(pile);
}

void App::OnPileMoved(Pile&) {}

void App::OnPileMenu(Pile& pile, POINT pt) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuToggle, pile.folded ? L"펼치기" : L"접기");
    AppendMenuW(menu, MF_STRING, kMenuDissolve, L"더미 풀기 (창은 모두 꺼내 두기)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuRestoreAll, L"모든 창 꺼내기");
    AppendMenuW(menu, MF_STRING, kMenuHelp, L"사용법");
    AppendMenuW(menu, MF_STRING, kMenuQuit, L"종료 (창을 모두 꺼내고 끝내기)");
    SetForegroundWindow(main_);
    int id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, main_, nullptr);
    PostMessageW(main_, WM_NULL, 0, 0);
    DestroyMenu(menu);
    int pileId = pile.id;  // the pile may be gone after an operation
    auto findPile = [&]() -> Pile* {
        for (auto& p : piles_)
            if (p->id == pileId) return p.get();
        return nullptr;
    };
    switch (id) {
        case kMenuToggle:
            if (Pile* p = findPile()) OnPileClick(*p);
            break;
        case kMenuDissolve:
            if (Pile* p = findPile()) Dissolve(*p);
            break;
        case kMenuRestoreAll: RestoreAll(); break;
        case kMenuHelp: Notify(L"사용법", kHelpText); break;
        case kMenuQuit: Quit(); break;
    }
}

// ---------------------------------------------------------------------------------------------
// Pile operations
// ---------------------------------------------------------------------------------------------

Pile* App::PileAt(POINT pt) {
    for (auto it = piles_.rbegin(); it != piles_.rend(); ++it) {
        RECT r = (*it)->view->ScreenRect();
        if (PtInRect(&r, pt)) return it->get();
    }
    return nullptr;
}

Pile* App::PileOf(HWND hwnd) {
    for (auto& p : piles_)
        if (p->Find(hwnd)) return p.get();
    return nullptr;
}

Pile& App::CreatePile(POINT nearPt) {
    auto p = std::make_unique<Pile>();
    p->id = nextId_++;
    p->color = kPalette[colorIndex_++ % (sizeof(kPalette) / sizeof(kPalette[0]))];
    p->folded = true;
    p->pos = NextSlot(nearPt);
    p->name = L"새 더미";
    p->view = std::make_unique<PileWindow>(*p, *this);
    piles_.push_back(std::move(p));
    return *piles_.back();
}

void App::RemovePile(Pile& pile) {
    piles_.erase(std::remove_if(piles_.begin(), piles_.end(), [&](const auto& p) { return p.get() == &pile; }),
                 piles_.end());
    SaveJournal();
}

std::wstring App::AutoName(const Pile& pile) const {
    if (pile.members.empty()) return L"빈 더미";
    std::wstring first = FriendlyAppName(ProcessExeName(pile.members.front().pid));
    if (pile.members.size() == 1) return first;
    return first + L" 외 " + std::to_wstring(pile.members.size() - 1) + L"개";
}

void App::Refresh(Pile& pile) {
    pile.name = AutoName(pile);
    pile.view->Render();
}

void App::HideMembers(Pile& pile, const std::vector<HWND>& hwnds, bool readPlacement) {
    UINT dpi = DpiForPoint(pile.pos);
    std::vector<HWND> visible;
    for (HWND h : hwnds) {
        Member* m = pile.Find(h);
        if (!m || m->hidden) continue;
        if (readPlacement) {
            m->placement.length = sizeof(WINDOWPLACEMENT);
            GetWindowPlacement(h, &m->placement);
        }
        visible.push_back(h);
    }
    if (visible.empty()) {
        pile.folded = true;
        Refresh(pile);
        return;
    }
    // 1) previews while the windows are still visible (spec §4.3)
    std::vector<Image> shots = snapshots_.Capture(visible, Scale(240, dpi));
    for (size_t i = 0; i < visible.size(); ++i) {
        Member* m = pile.Find(visible[i]);
        if (!shots[i].empty()) m->snapshot = std::move(shots[i]);
        else if (m->snapshot.empty()) m->snapshot = MakeIconCard(visible[i], Scale(240, dpi), Scale(135, dpi));
        m->title = WindowTitle(visible[i]);
        m->hidden = true;
    }
    // 2) journal before hiding (spec §4.4 step 4 → 6)
    SaveJournal();
    // 3) show the pile, then hide the originals (async: a hung app can't block us)
    pile.folded = true;
    Refresh(pile);
    for (HWND h : visible) ShowWindowAsync(h, SW_HIDE);
    SetTimer(main_, kTimerVerify, 500, nullptr);
}

void App::Fold(Pile& pile) {
    std::vector<HWND> all;
    for (auto& m : pile.members) all.push_back(m.hwnd);
    HideMembers(pile, all, true);
}

void App::Expand(Pile& pile, bool bringToFront) {
    std::vector<HWND> shown;
    for (auto it = pile.members.begin(); it != pile.members.end();) {
        Member& m = *it;
        if (!m.hidden) {
            ++it;
            continue;
        }
        JournalEntry e;
        e.hwnd = (uint64_t)(uintptr_t)m.hwnd;
        e.pid = m.pid;
        e.createTime = m.createTime;
        e.className = m.className;
        if (!ValidateIdentity(e).empty()) {  // closed or replaced meanwhile: never touch another app
            it = pile.members.erase(it);
            continue;
        }
        m.hidden = false;
        RestoreWindow(m.hwnd, m.placement, false);
        shown.push_back(m.hwnd);
        ++it;
    }
    if (pile.members.empty()) {
        RemovePile(pile);
        return;
    }
    pile.folded = false;
    SaveJournal();
    Refresh(pile);
    if (bringToFront && !shown.empty()) {
        for (HWND h : shown)
            SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
        // We just received the click, so the foreground lock allows this. Best effort only.
        if (!IsIconic(shown.back())) SetForegroundWindow(shown.back());
    }
}

void App::Dissolve(Pile& pile) {
    Expand(pile, true);
    for (auto& p : piles_)
        if (p.get() == &pile) {
            RemovePile(pile);
            return;
        }
}

void App::RestoreAll() {
    std::vector<int> ids;
    for (auto& p : piles_) ids.push_back(p->id);
    for (int id : ids)
        for (auto& p : piles_)
            if (p->id == id) {
                if (p->HiddenCount() > 0) Expand(*p, false);
                break;
            }
    SaveJournal();
}

void App::VerifyHidden() {
    KillTimer(main_, kTimerVerify);
    int failed = 0;
    for (auto& p : piles_) {
        bool changed = false;
        for (auto& m : p->members)
            if (m.hidden && IsWindow(m.hwnd) && IsWindowVisible(m.hwnd)) {
                m.hidden = false;  // the app refused or re-showed itself: treat as taken out
                changed = true;
                ++failed;
            }
        if (changed) {
            if (p->HiddenCount() == 0) p->folded = false;
            Refresh(*p);
        }
    }
    if (failed) {
        SaveJournal();
        Notify(L"일부 창을 넣지 못했어요", L"창 " + std::to_wstring(failed) + L"개는 숨겨지지 않아 그대로 두었어요.");
    }
}

void App::SaveJournal() {
    Journal j;
    j.managerPid = GetCurrentProcessId();
    for (auto& p : piles_)
        for (auto& m : p->members)
            if (m.hidden) {
                JournalEntry e;
                e.hwnd = (uint64_t)(uintptr_t)m.hwnd;
                e.pid = m.pid;
                e.createTime = m.createTime;
                e.className = m.className;
                e.wp = m.placement;
                j.entries.push_back(e);
            }
    j.clean = j.entries.empty();
    WriteJournal(j);
}

void App::OnMemberGone(HWND hwnd) {
    Pile* p = PileOf(hwnd);
    if (!p || IsWindow(hwnd)) return;
    auto& ms = p->members;
    ms.erase(std::remove_if(ms.begin(), ms.end(), [&](const Member& m) { return m.hwnd == hwnd; }), ms.end());
    if (ms.empty()) {
        RemovePile(*p);
        return;
    }
    if (p->HiddenCount() == 0) p->folded = false;
    Refresh(*p);
    SaveJournal();
}

void App::OnMemberShown(HWND hwnd) {
    Pile* p = PileOf(hwnd);
    if (!p) return;
    Member* m = p->Find(hwnd);
    if (!m->hidden || !IsWindowVisible(hwnd)) return;
    m->hidden = false;  // the app brought itself back (e.g. a messenger): respect that
    if (p->HiddenCount() == 0) p->folded = false;
    Refresh(*p);
    SaveJournal();
}

void CALLBACK App::OnObjectEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    if (!instance_ || idObject != OBJID_WINDOW || idChild != CHILDID_SELF || !hwnd) return;
    // Cheap filter; the real work is deferred so it never runs in the middle of another operation.
    if (!instance_->PileOf(hwnd)) return;
    PostMessageW(instance_->main_, event == EVENT_OBJECT_DESTROY ? WM_APP_MEMBER_GONE : WM_APP_MEMBER_SHOWN,
                 (WPARAM)hwnd, 0);
}

// ---------------------------------------------------------------------------------------------
// Shell integration
// ---------------------------------------------------------------------------------------------

POINT App::NextSlot(POINT nearPt) {
    RECT work = WorkAreaForPoint(nearPt);
    UINT dpi = DpiForPoint(nearPt);
    SIZE card = PileWindow::CardSize(dpi);
    int margin = Scale(12, dpi), gap = Scale(10, dpi);
    for (int x = work.right - card.cx - margin; x >= work.left; x -= card.cx + gap) {
        for (int y = work.top + margin; y + card.cy <= work.bottom; y += card.cy + gap) {
            RECT cand{x, y, x + card.cx, y + card.cy};
            bool free = true;
            for (auto& p : piles_) {
                RECT r = p->view->ScreenRect(), tmp;
                if (IntersectRect(&tmp, &r, &cand)) free = false;
            }
            if (free) return {x, y};
        }
    }
    return {work.right - card.cx - margin, work.top + margin};
}

void App::AddTrayIcon() {
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = main_;
    nid_.uID = kTrayId;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid_.uCallbackMessage = WM_APP_TRAY;
    nid_.hIcon = icon_;
    TrimNul(nid_.szTip, ARRAYSIZE(nid_.szTip), L"창 더미 — 창을 끌어 화면 위쪽 '＋ 새 더미'에 놓으세요");
    Shell_NotifyIconW(NIM_ADD, &nid_);
    nid_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);
}

void App::Notify(const std::wstring& title, const std::wstring& text) {
    NOTIFYICONDATAW n = nid_;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    n.hBalloonIcon = icon_;
    TrimNul(n.szInfoTitle, ARRAYSIZE(n.szInfoTitle), title);
    TrimNul(n.szInfo, ARRAYSIZE(n.szInfo), text);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void App::ShowTrayMenu() {
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    int hidden = 0;
    for (auto& p : piles_) hidden += p->HiddenCount();
    std::wstring restore = L"모든 창 꺼내기";
    if (hidden) restore += L" (" + std::to_wstring(hidden) + L"개)";
    AppendMenuW(menu, MF_STRING | (hidden ? 0 : MF_GRAYED), kMenuRestoreAll, restore.c_str());
    AppendMenuW(menu, MF_STRING, kMenuHelp, L"사용법");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuQuit, L"종료 (창을 모두 꺼내고 끝내기)");
    SetForegroundWindow(main_);
    int id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, main_, nullptr);
    PostMessageW(main_, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (id == kMenuRestoreAll) RestoreAll();
    else if (id == kMenuHelp) Notify(L"사용법", kHelpText);
    else if (id == kMenuQuit) Quit();
}

void App::Quit() {
    if (quitting_) return;
    quitting_ = true;
    RestoreAll();
    piles_.clear();
    Journal j;
    j.managerPid = GetCurrentProcessId();
    j.clean = true;
    WriteJournal(j);  // the watchdog sees a clean exit and does nothing
    PostQuitMessage(0);
}

LRESULT CALLBACK App::MainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (instance_ && instance_->main_ == h) return instance_->HandleMain(msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT App::HandleMain(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == taskbarCreatedMsg_ && taskbarCreatedMsg_) {  // Explorer restarted
        AddTrayIcon();
        return 0;
    }
    switch (msg) {
        case WM_APP_TRAY:
            switch (LOWORD(lp)) {
                case WM_CONTEXTMENU:
                case WM_RBUTTONUP: ShowTrayMenu(); break;
                case NIN_SELECT:
                case NIN_KEYSELECT:
                case WM_LBUTTONUP: Notify(L"사용법", kHelpText); break;
            }
            return 0;
        case WM_APP_MEMBER_GONE: OnMemberGone((HWND)wp); return 0;
        case WM_APP_MEMBER_SHOWN: OnMemberShown((HWND)wp); return 0;
        case WM_TIMER:
            if (wp == kTimerVerify) VerifyHidden();
            else if (wp == kTimerIdle) snapshots_.ReleaseIfIdle(30000);
            return 0;
        case WM_QUERYENDSESSION:
            // Shutting down/logging off: bring every window back so its "save changes?" prompt is visible.
            RestoreAll();
            return TRUE;
        case WM_ENDSESSION:
            if (wp) Quit();
            return 0;
    }
    return DefWindowProcW(main_, msg, wp, lp);
}

}  // namespace wp
