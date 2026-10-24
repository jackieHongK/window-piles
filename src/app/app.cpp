#include "app.h"

#include <dwmapi.h>
#include <wtsapi32.h>

#include <algorithm>
#include <cwctype>
#include <map>

#include "activity/activity_log.h"
#include "app/app_icon.h"
#include "common/win_util.h"
#include "groups/pile_store.h"
#include "recovery/journal.h"
#include "groups/unread.h"
#include "ui/inline_rename.h"
#include "ui/menu.h"
#include "ui/pile_editor.h"
#include "ui/theme.h"

namespace wp {

namespace {

constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT WM_APP_MEMBER_GONE = WM_APP + 2;
constexpr UINT WM_APP_MEMBER_SHOWN = WM_APP + 3;
constexpr UINT_PTR kTimerVerify = 1;
constexpr UINT_PTR kTimerIdle = 2;
constexpr UINT_PTR kTimerPersist = 3;
constexpr UINT_PTR kTimerGoneRetry = 4;
constexpr UINT_PTR kTimerToFront = 5;
constexpr UINT_PTR kTimerReclaimNote = 6;
constexpr UINT_PTR kTimerReclaimFold = 7;
constexpr UINT_PTR kTimerRevalidate = 8;
constexpr UINT_PTR kTimerActivity = 9;
constexpr UINT WM_APP_RECLAIM = WM_APP + 4;
constexpr UINT WM_APP_TITLE = WM_APP + 5;
constexpr UINT WM_APP_FLUSH = WM_APP + 6;  // deferred redraws and journal write (one per burst)
// How long after start-up windows reopened by their apps are put back into their piles.
constexpr double kReclaimWindowMs = 15 * 60 * 1000;
constexpr UINT kTrayId = 1;
constexpr int kAnimMs = 240;  // brand motion: 240 ms ease-out (+ a 2 px settle when folding)
constexpr DWORD kIdleMs = 5 * 60 * 1000;  // no input for this long: not counted as work time
constexpr DWORD kFlashGraceMs = 3000;      // flashes this soon after folding are ignored for badges
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"WindowPiles";

enum MenuId : UINT {
    kMenuToggle = 100,
    kMenuStyle,
    kMenuDissolve,
    kMenuClearMissing,
    kMenuRestoreAll,
    kMenuAutostart,
    kMenuHelp,
    kMenuQuit,
    kMenuStats,
};

std::wstring FileName(const std::wstring& path) {
    size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring Lower(std::wstring s) {
    for (auto& ch : s) ch = (wchar_t)towlower(ch);
    return s;
}

std::wstring FriendlyAppName(const std::wstring& exe) {
    static const std::map<std::wstring, std::wstring> names = {
        {L"chrome.exe", L"크롬"},        {L"msedge.exe", L"엣지"},         {L"explorer.exe", L"탐색기"},
        {L"notepad.exe", L"메모장"},     {L"excel.exe", L"엑셀"},          {L"winword.exe", L"워드"},
        {L"powerpnt.exe", L"파워포인트"}, {L"code.exe", L"VS Code"},        {L"windowsterminal.exe", L"터미널"},
        {L"kakaotalk.exe", L"카카오톡"},  {L"firefox.exe", L"파이어폭스"}, {L"outlook.exe", L"아웃룩"},
        {L"olk.exe", L"아웃룩"},          {L"hwp.exe", L"한글"},            {L"applicationframehost.exe", L"앱"},
    };
    auto it = names.find(Lower(exe));
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

void CopyTrunc(wchar_t* dst, size_t cap, const std::wstring& src) { wcsncpy_s(dst, cap, src.c_str(), _TRUNCATE); }

bool HasArea(const RECT& r) { return r.right > r.left && r.bottom > r.top; }

// Where a hidden member will appear again (screen px), for the spreading animation.
RECT RestoreRectOf(const Member& m) {
    if (m.placement.showCmd == SW_SHOWMINIMIZED) return {};
    if (HasArea(m.lastRect)) return m.lastRect;
    return m.placement.rcNormalPosition;
}

JournalEntry EntryOf(const Member& m) {
    JournalEntry e;
    e.hwnd = (uint64_t)(uintptr_t)m.hwnd;
    e.pid = m.pid;
    e.createTime = m.createTime;
    e.className = m.className;
    e.wp = m.placement;
    return e;
}

// `windows` sorted front-most first, as they overlap on screen right now (one pass over the
// top-level windows, which EnumWindows lists in z-order).
std::vector<HWND> FrontToBack(const std::vector<HWND>& windows) {
    struct Ctx {
        const std::vector<HWND>* want;
        std::vector<HWND> out;
    } ctx{&windows, {}};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* c = (Ctx*)lp;
        if (std::find(c->want->begin(), c->want->end(), h) != c->want->end()) c->out.push_back(h);
        return c->out.size() < c->want->size();
    }, (LPARAM)&ctx);
    for (HWND h : windows)  // not top-level any more (shouldn't happen): keep them, at the back
        if (std::find(ctx.out.begin(), ctx.out.end(), h) == ctx.out.end()) ctx.out.push_back(h);
    return ctx.out;
}

// rcNormalPosition is in workspace coordinates (relative to the primary monitor's work area);
// the offset to screen coordinates is the taskbar's room on that side.
POINT WorkspaceOffset() {
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    return {mi.rcWork.left - mi.rcMonitor.left, mi.rcWork.top - mi.rcMonitor.top};
}

void MoveHiddenTo(HWND h, const WINDOWPLACEMENT& wp) {
    if (wp.showCmd == SW_SHOWMINIMIZED) return;
    RECT r = wp.rcNormalPosition;
    if (r.right <= r.left || r.bottom <= r.top) return;
    POINT o = WorkspaceOffset();
    SetWindowPos(h, nullptr, r.left + o.x, r.top + o.y, r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
}

// A window must come back mostly on screen: if less than half of it would be visible (a monitor was
// unplugged, the resolution changed, it was put away hanging off an edge), move it — and shrink it if
// needed — into the nearest work area.
void KeepOnScreen(WINDOWPLACEMENT& wp) {
    POINT o = WorkspaceOffset();
    RECT s = wp.rcNormalPosition;
    OffsetRect(&s, o.x, o.y);
    int w = s.right - s.left, h = s.bottom - s.top;
    if (w <= 0 || h <= 0) return;
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromRect(&s, MONITOR_DEFAULTTONEAREST), &mi);
    RECT vis{};
    IntersectRect(&vis, &s, &mi.rcWork);
    if ((long long)(vis.right - vis.left) * (vis.bottom - vis.top) * 2 >= (long long)w * h) return;
    const RECT& work = mi.rcWork;
    w = std::min<int>(w, work.right - work.left);
    h = std::min<int>(h, work.bottom - work.top);
    int x = std::clamp<int>(s.left, work.left, work.right - w), y = std::clamp<int>(s.top, work.top, work.bottom - h);
    wp.rcNormalPosition = {x - o.x, y - o.y, x - o.x + w, y - o.y + h};
}

// The "reopen" list keeps the most recent entries only: it would otherwise grow with every reboot.
constexpr size_t kMaxMissing = 20;
void TrimMissing(Pile& pile) {
    if (pile.missing.size() > kMaxMissing)
        pile.missing.erase(pile.missing.begin(), pile.missing.end() - kMaxMissing);
}

std::wstring FlagPath(const wchar_t* name) { return AppDataDir() + L"\\" + name; }
bool FlagExists(const wchar_t* name) { return GetFileAttributesW(FlagPath(name).c_str()) != INVALID_FILE_ATTRIBUTES; }
void SetFlag(const wchar_t* name, bool on) {
    if (on) {
        HANDLE f = CreateFileW(FlagPath(name).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    } else {
        DeleteFileW(FlagPath(name).c_str());
    }
}

}  // namespace

App* App::instance_ = nullptr;

App::App() { instance_ = this; }

App::~App() {
    if (objectHook_) UnhookWinEvent(objectHook_);
    if (nameHook_) UnhookWinEvent(nameHook_);
    for (auto& [pid, hook] : titleHooks_) UnhookWinEvent(hook);
    if (main_) DeregisterShellHookWindow(main_);
    drag_.reset();
    frame_.reset();
    guide_.reset();
    stats_.reset();
    piles_.clear();
    if (nid_.cbSize) Shell_NotifyIconW(NIM_DELETE, &nid_);
    if (main_) DestroyWindow(main_);
    if (icon_) DestroyIcon(icon_);
    if (trayIcon_) DestroyIcon(trayIcon_);
    instance_ = nullptr;
}

int App::Run() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kMainWindowClass;
    RegisterClassW(&wc);
    // A hidden (never shown) top-level window: tray callbacks, menus, deferred work.
    main_ = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Stack Neverflow", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                            wc.hInstance, nullptr);
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    // Explorer tells shell-hook windows when an app flashes its taskbar button (new message…).
    // It's pushed to us; nothing is polled.
    shellHookMsg_ = RegisterWindowMessageW(L"SHELLHOOK");
    RegisterShellHookWindow(main_);
    icon_ = MakeAppIcon(GetSystemMetrics(SM_CXSMICON) * 2);
    trayIcon_ = MakeAppIcon(GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem()));
    Log("run: main window %p", main_);
    AddTrayIcon();
    Log("run: tray icon added");
    guide_ = std::make_unique<GuideWindow>();

    // Start with Windows unless the user turned it off; refresh the path (the exe may have moved
    // with an update).
    if (!FlagExists(L"autostart.off") && !UsingTestDataDir()) SetAutostart(true);

    startedMs_ = NowMs();
    activity::Append(activity::Kind::Start);
    int recovered = RestoreSavedPiles();
    Log("run: %zu piles restored, %d leftover windows recovered", piles_.size(), recovered);

    drag_ = std::make_unique<DragWatcher>(*this);
    // Member windows closing or showing themselves. Out-of-context: no injection.
    objectHook_ = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_SHOW, nullptr, OnObjectEvent, 0, 0,
                                  WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    SetTimer(main_, kTimerIdle, 10000, nullptr);
    // Resume from sleep / unlock: re-check the piles (event-driven, no polling).
    WTSRegisterSessionNotification(main_, NOTIFY_FOR_THIS_SESSION);
    SaveJournalNow();  // also tells a waiting watchdog (relaunch after a crash) that we are up
    UpdateReclaim();

    if (recovered > 0)
        Notify(L"창을 되찾았어요", L"지난번에 숨겨져 있던 창 " + std::to_wstring(recovered) + L"개를 다시 보이게 했어요.");
    if (!FlagExists(L"firstrun.done")) {
        ShowGuide();
        SetFlag(L"firstrun.done", true);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------------------------

int App::RestoreSavedPiles() {
    std::vector<SavedPile> saved;
    bool loaded = LoadPileState(saved);
    Log("restore: piles.json %s, %zu piles", loaded ? "loaded" : "missing/unreadable", saved.size());
    // What the last run left hidden, read before this run writes its own journal over it.
    Journal lastRun;
    std::string journalErr;
    bool haveLastRun = ReadJournal(lastRun, journalErr);
    // Pile ids stay the same across restarts (the activity log refers to them).
    for (const SavedPile& sp : saved) nextId_ = std::max(nextId_, sp.id + 1);
    std::vector<uint64_t> claimed;
    for (const SavedPile& sp : saved) {
        auto p = std::make_unique<Pile>();
        p->id = sp.id > 0 && !FindPile(sp.id) ? sp.id : nextId_++;
        p->name = sp.name;
        p->customName = sp.customName;
        p->color = sp.color;
        p->pos = sp.pos;
        p->missing = sp.missing;
        for (const SavedMember& sm : sp.members) {
            JournalEntry e;
            e.hwnd = sm.hwnd;
            e.pid = sm.pid;
            e.createTime = sm.createTime;
            e.className = sm.className;
            if (!ValidateIdentity(e).empty()) {
                // The window is gone (app closed, or the PC restarted): remember it in the list.
                p->missing.push_back({sm.exePath, FriendlyAppName(FileName(sm.exePath)), sm.title});
                continue;
            }
            Member m;
            m.hwnd = (HWND)(uintptr_t)sm.hwnd;
            m.pid = sm.pid;
            m.createTime = sm.createTime;
            m.className = sm.className;
            m.exePath = sm.exePath;
            m.title = WindowTitle(m.hwnd);
            m.placement = sm.wp;
            m.placement.length = sizeof(WINDOWPLACEMENT);
            m.stamp = sm.stamp;
            nextStamp_ = std::max(nextStamp_, sm.stamp + 1);
            m.hidden = !IsWindowVisible(m.hwnd);  // previews for the cards in front: Refresh below
            claimed.push_back(sm.hwnd);
            p->members.push_back(std::move(m));
        }
        TrimMissing(*p);
        if (p->Empty()) continue;
        p->folded = sp.folded || p->members.empty();
        p->view = std::make_unique<PileWindow>(*p, *this);
        piles_.push_back(std::move(p));
    }

    // After an app restart/update the windows of folded piles are visible again: put them all back
    // in one go — one capture for the previews in front, journal once, no per-pile animation.
    double t0 = NowMs();
    std::vector<HWND> toFold, shoot;
    for (auto& p : piles_)
        if (p->folded)
            for (auto& m : p->members)
                if (!m.hidden && IsWindowVisible(m.hwnd)) {
                    toFold.push_back(m.hwnd);
                    if (p->ShowsPreview(m)) shoot.push_back(m.hwnd);
                }
    if (!toFold.empty()) {
        UINT dpi = GetDpiForSystem();
        std::vector<Image> shots = snapshots_.Capture(shoot, Scale(Pile::kPreviewW, dpi));
        for (size_t i = 0; i < shoot.size(); ++i)
            if (!shots[i].empty()) PileOf(shoot[i])->Find(shoot[i])->snapshot = std::move(shots[i]);
        double t1 = NowMs();
        for (HWND h : toFold) {
            Member* m = PileOf(h)->Find(h);
            GetWindowRect(h, &m->lastRect);
            m->hidden = true;
            m->hiddenAt = GetTickCount();
            m->attention = false;
            m->unread = ParseUnread(WindowTitle(h)).count;
        }
        SaveJournalNow();  // journal before hiding
        for (HWND h : toFold) ShowWindowAsync(h, SW_HIDE);
        SetTimer(main_, kTimerVerify, 500, nullptr);
        Log("restore: %zu windows put away again (previews %.0f ms, total %.0f ms)", toFold.size(), t1 - t0,
            NowMs() - t0);
    }
    for (auto& p : piles_) Refresh(*p);

    // Anything the last run hid that no pile claims (e.g. both processes were killed): show it.
    int recovered = 0;
    if (haveLastRun && !lastRun.clean) {
        Journal rest;
        for (auto& e : lastRun.entries)
            if (std::find(claimed.begin(), claimed.end(), e.hwnd) == claimed.end()) rest.entries.push_back(e);
        recovered = RecoverFromJournal(rest, nullptr);
    }
    PersistNow();
    return recovered;
}

void App::PersistSoon() {
    if (stateFrozen_ || !main_) return;
    SetTimer(main_, kTimerPersist, 400, nullptr);  // coalesce bursts of changes into one write
}

void App::PersistNow(bool background) {
    if (main_) KillTimer(main_, kTimerPersist);
    if (stateFrozen_) return;
    std::vector<SavedPile> out;
    for (auto& p : piles_) {
        SavedPile sp;
        sp.id = p->id;
        sp.name = p->name;
        sp.customName = p->customName;
        sp.color = p->color;
        sp.pos = p->pos;
        sp.folded = p->folded;
        sp.missing = p->missing;
        for (auto& m : p->members) {
            SavedMember sm;
            sm.hwnd = (uint64_t)(uintptr_t)m.hwnd;
            sm.pid = m.pid;
            sm.createTime = m.createTime;
            sm.className = m.className;
            sm.exePath = m.exePath;
            sm.title = IsWindow(m.hwnd) ? WindowTitle(m.hwnd) : m.title;
            sm.wp = m.placement;
            sm.hidden = m.hidden;
            sm.stamp = m.stamp;
            sp.members.push_back(sm);
        }
        out.push_back(std::move(sp));
    }
    SavePileState(out, background);
}

void App::AddMissing(Pile& pile, const Member& m) {
    pile.missing.push_back({m.exePath, FriendlyAppName(FileName(m.exePath)), m.title});
    TrimMissing(pile);
}

void App::ClaimMissing(Pile& pile, const Member& m) {
    // The same app's window came back into this pile: drop one matching "reopen" entry,
    // preferring the one with the same title.
    std::wstring exe = Lower(m.exePath);
    auto sameExe = [&](const MissingWindow& w) { return !exe.empty() && Lower(w.exePath) == exe; };
    auto it = std::find_if(pile.missing.begin(), pile.missing.end(),
                           [&](const MissingWindow& w) { return sameExe(w) && w.title == m.title; });
    if (it == pile.missing.end()) it = std::find_if(pile.missing.begin(), pile.missing.end(), sameExe);
    if (it != pile.missing.end()) pile.missing.erase(it);
}

bool App::AutostartEnabled() {
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) ==
           ERROR_SUCCESS;
}

void App::SetAutostart(bool on) {
    if (on) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --autostart";
        RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, REG_SZ, cmd.c_str(),
                        (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue);
    }
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

std::wstring App::PileNameOf(HWND hwnd) {
    Pile* p = PileOf(hwnd);
    return p ? p->name : std::wstring();
}

void App::OnDragTargetsShown(HWND) {
    if (frame_) frame_->SetDropMode(true);  // the top tab grows into a "drop here to add" target
}

void App::OnDragHover(HWND, POINT cursor) {
    Pile* over = PileAt(cursor);
    for (auto& p : piles_) p->view->SetHot(p.get() == over && !p->active);
    if (frame_) frame_->SetHot(over && over->active);
}

void App::OnDragEnd() {
    for (auto& p : piles_) p->view->SetHot(false);
    if (frame_) frame_->SetDropMode(false);
}

void App::TakeOut(HWND hwnd, const WINDOWPLACEMENT& before) {
    Pile* p = PileOf(hwnd);
    if (!p) return;
    auto& ms = p->members;
    ms.erase(std::remove_if(ms.begin(), ms.end(), [&](const Member& m) { return m.hwnd == hwnd; }), ms.end());
    RestoreWindow(hwnd, before, false);  // the window stays open, back where it was before the drag
    if (p->members.empty() && p->active) {
        p->active = false;
        p->folded = true;
        frame_.reset();
        ShowCard(*p);
    }
    if (p->Empty()) RemovePile(*p);
    else Refresh(*p);
    SaveJournal();
}

void App::OnDragDrop(HWND hwnd, const WINDOWPLACEMENT& before, POINT cursor, DropZone zone) {
    if (zone == DropZone::RemoveFromPile) {
        TakeOut(hwnd, before);
        return;
    }
    bool canPile = CanPile(hwnd);
    bool onBar = zone == DropZone::NewPile;
    Pile* target = onBar ? nullptr : PileAt(cursor);
    Log("drop: zone=%d can=%d cursor=%ld,%ld target=%d frame=%d", (int)zone, canPile, cursor.x, cursor.y,
        target ? target->id : -1, frame_ ? 1 : 0);
    if (!canPile) return;
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
        if (owner->Empty()) RemovePile(*owner);
        else Refresh(*owner);
    }
    if (!target) target = &CreatePile(cursor);

    Member m;
    m.hwnd = hwnd;
    GetWindowThreadProcessId(hwnd, &m.pid);
    m.createTime = ProcessCreateTime(m.pid);
    m.className = WindowClass(hwnd);
    m.exePath = ProcessExePath(m.pid);
    m.title = WindowTitle(hwnd);
    m.placement = before;  // come back to where it was before the drag, not to the drop spot
    ClaimMissing(*target, m);
    target->members.push_back(std::move(m));

    if (target->folded) {
        HideMembers(*target, {hwnd}, false);
    } else {
        RestoreWindow(hwnd, before, false);  // joins the open pile, stays visible
        Refresh(*target);
        SaveJournal();
    }
}

// ---------------------------------------------------------------------------------------------
// Pile callbacks
// ---------------------------------------------------------------------------------------------

void App::OnPileClick(Pile& pile) {
    if (!pile.folded && !pile.members.empty()) {
        Fold(pile);
        return;
    }
    int id = pile.id;
    if (!pile.members.empty()) Activate(pile);
    // Opening also reopens the apps whose windows were closed (after the unfold, which may add some).
    if (Pile* p = FindPile(id)) {
        if (p->members.empty() && p->folded) {  // only "reopen" entries: their windows will stay open
            p->folded = false;
            Refresh(*p);
        }
        RelaunchMissing(*p);
    }
}

void App::OnPileMoved(Pile& pile) {
    // The card the user just put down is the top one now (piles_ is the stacking order, bottom first).
    auto it = std::find_if(piles_.begin(), piles_.end(), [&](const auto& p) { return p.get() == &pile; });
    if (it != piles_.end()) std::rotate(it, it + 1, piles_.end());
    PersistSoon();
}

void App::ShowCard(Pile& pile) {
    // Back in its own layer: right under the nearest visible card above it, not on top of everything.
    HWND above = HWND_TOPMOST;
    auto it = std::find_if(piles_.begin(), piles_.end(), [&](const auto& p) { return p.get() == &pile; });
    for (auto up = it == piles_.end() ? it : it + 1; up != piles_.end(); ++up)
        if ((*up)->view->visible()) {
            above = (*up)->view->hwnd();
            break;
        }
    pile.view->SetVisible(true, above);
}

void App::OnMissingClick(Pile& pile, int index) {
    if (index < 0 || index >= (int)pile.missing.size()) return;
    const MissingWindow& w = pile.missing[index];
    if (w.exePath.empty() || Lower(FileName(w.exePath)) == L"applicationframehost.exe") return;
    LaunchIntoPile(pile, w.exePath);
}

void App::RelaunchMissing(Pile& pile) {
    std::vector<std::wstring> launched;
    for (MissingWindow& w : pile.missing) {
        std::wstring exe = Lower(w.exePath);
        // Store apps run inside ApplicationFrameHost: launching that opens nothing.
        if (w.launched || exe.empty() || Lower(FileName(exe)) == L"applicationframehost.exe") continue;
        w.launched = true;
        if (std::find(launched.begin(), launched.end(), exe) != launched.end()) continue;  // once per app
        launched.push_back(exe);
        LaunchIntoPile(pile, w.exePath);
    }
}

void App::LaunchIntoPile(Pile& pile, const std::wstring& exePath) {
    // Opens the app without arguments (we don't know which document it had open). Its next window
    // joins this pile and takes the entry off the list (TryReclaim).
    ShellExecuteW(nullptr, L"open", exePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    launchJoin_[Lower(exePath)] = pile.id;
    launchJoinUntil_ = NowMs() + 60 * 1000;  // ponytail: fixed wait, slow apps starting later aren't joined
    UpdateReclaim();
}

void App::OnPileMenu(Pile& pile, POINT pt) {
    int hidden = 0;
    for (auto& p : piles_) hidden += p->HiddenCount();
    std::vector<MenuItem> items;
    if (!pile.members.empty()) items.push_back({kMenuToggle, pile.folded ? L"펼치기" : L"접기", L"클릭"});
    items.push_back({kMenuStyle, L"이름·색 바꾸기…"});
    if (!pile.members.empty()) items.push_back({kMenuDissolve, L"더미 풀기", L"창은 그대로"});
    if (!pile.missing.empty())
        items.push_back({kMenuClearMissing, L"닫힌 창 목록 지우기", std::to_wstring(pile.missing.size()) + L"개"});
    items.push_back({});
    items.push_back({kMenuRestoreAll, L"모든 창 꺼내기", hidden ? std::to_wstring(hidden) + L"개" : L"", hidden == 0});
    items.push_back({kMenuStats, L"작업 기록", L"오늘 쓴 시간"});
    items.push_back({kMenuHelp, L"사용법"});
    items.push_back({});
    items.push_back({kMenuQuit, L"Neverflow 종료", L"창을 모두 꺼내요"});

    int pileId = pile.id;  // the pile may be gone after the menu closes
    UINT id = ShowMenu(items, pt);
    Pile* p = FindPile(pileId);
    switch (id) {
        case kMenuToggle:
            if (p) OnPileClick(*p);
            break;
        case kMenuStyle:
            if (p) EditStyle(*p, pt);
            break;
        case kMenuDissolve:
            if (p) Dissolve(*p);
            break;
        case kMenuClearMissing:
            if (p) {
                p->missing.clear();
                if (p->Empty()) RemovePile(*p);
                else Refresh(*p);
                PersistSoon();
            }
            break;
        case kMenuRestoreAll: RestoreAll(); break;
        case kMenuStats: ShowStats(); break;
        case kMenuHelp: ShowGuide(); break;
        case kMenuQuit: Quit(); break;
    }
}

void App::OnPileRename(Pile& pile) {
    int pileId = pile.id;
    std::wstring before = pile.name;
    bool wasCustom = pile.customName;
    auto preview = [this, pileId](const std::wstring& name) {
        if (Pile* p = FindPile(pileId)) {
            p->name = name;
            p->view->Render();
        }
    };
    std::wstring result;
    bool saved = InlineRename(pile.view->RenameRect(), pile.name, DpiForPoint(pile.pos), preview, result);
    Pile* p = FindPile(pileId);
    if (!p) return;
    if (saved) {
        p->customName = !result.empty();  // an empty name goes back to the automatic one
        if (p->customName) p->name = result;
    } else {
        p->name = before;
        p->customName = wasCustom;
    }
    Refresh(*p);
    PersistSoon();
}

void App::EditStyle(Pile& pile, POINT anchor) {
    int pileId = pile.id;
    PileStyle current{pile.name, pile.color}, result;
    bool wasCustom = pile.customName;
    auto preview = [this, pileId](const PileStyle& s) {
        if (Pile* p = FindPile(pileId)) {
            p->color = s.color;
            if (!s.name.empty()) p->name = s.name;
            p->view->Render();
            if (p->active && frame_) frame_->Update();
        }
    };
    bool saved = EditPileStyle(current, anchor, preview, result);
    Pile* p = FindPile(pileId);
    if (!p) return;
    if (saved) {
        p->color = result.color;
        p->customName = !result.name.empty();
        if (p->customName) p->name = result.name;
    } else {
        p->color = current.color;
        p->name = current.name;
        p->customName = wasCustom;
    }
    Refresh(*p);
    PersistSoon();
}

// ---------------------------------------------------------------------------------------------
// Pile operations
// ---------------------------------------------------------------------------------------------

Pile* App::PileAt(POINT pt) {
    for (auto it = piles_.rbegin(); it != piles_.rend(); ++it) {
        Pile& p = **it;
        if (p.active) {
            if (frame_ && frame_->HitTab(pt)) return &p;
            continue;
        }
        RECT r = p.view->CardRect();
        if (p.view->visible() && PtInRect(&r, pt)) return &p;
    }
    return nullptr;
}

Pile* App::PileOf(HWND hwnd) {
    for (auto& p : piles_)
        if (p->Find(hwnd)) return p.get();
    return nullptr;
}

Pile* App::FindPile(int id) {
    for (auto& p : piles_)
        if (p->id == id) return p.get();
    return nullptr;
}

Pile* App::ActivePile() {
    for (auto& p : piles_)
        if (p->active) return p.get();
    return nullptr;
}

Pile& App::CreatePile(POINT nearPt) {
    auto p = std::make_unique<Pile>();
    p->id = nextId_++;
    int idx = theme::kNewPileOrder[colorIndex_++ % (sizeof(theme::kNewPileOrder) / sizeof(int))];
    p->color = theme::kPresets[idx].color;
    p->folded = true;
    p->pos = NextSlot(nearPt);
    p->name = L"새 더미";
    p->view = std::make_unique<PileWindow>(*p, *this);
    piles_.push_back(std::move(p));
    return *piles_.back();
}

void App::RemovePile(Pile& pile) {
    if (pile.active) frame_.reset();
    piles_.erase(std::remove_if(piles_.begin(), piles_.end(), [&](const auto& p) { return p.get() == &pile; }),
                 piles_.end());
    SaveJournal();
}

std::wstring App::AutoName(const Pile& pile) const {
    std::wstring first;
    size_t total = pile.members.size() + pile.missing.size();
    if (!pile.members.empty()) first = FriendlyAppName(FileName(pile.members.front().exePath));
    else if (!pile.missing.empty()) first = pile.missing.front().appName;
    else return L"빈 더미";
    if (total <= 1) return first;
    return first + L" 외 " + std::to_wstring(total - 1) + L"개";
}

void App::Refresh(Pile& pile) {
    if (!pile.customName) pile.name = AutoName(pile);
    UpdatePreviews(pile);
    pile.view->Render();
    if (pile.active && frame_) frame_->Update();
}

void App::RefreshSoon(Pile& pile) {
    if (std::find(refreshIds_.begin(), refreshIds_.end(), pile.id) == refreshIds_.end()) refreshIds_.push_back(pile.id);
    PostFlush();
}

void App::UpdatePreviews(Pile& pile) {
    // Keep images only for the cards in front; one that just moved to the front without a snapshot
    // gets an icon card (cheap, no capture).
    UINT dpi = DpiForPoint(pile.pos);
    for (auto& m : pile.members) {
        if (!pile.ShowsPreview(m)) {
            if (!m.snapshot.empty()) m.snapshot = Image{};
        } else if (m.snapshot.empty() && IsWindow(m.hwnd)) {
            m.snapshot = MakeIconCard(m.hwnd, Scale(Pile::kPreviewW, dpi), Scale(Pile::kPreviewH, dpi));
        }
    }
}

void App::HideMembers(Pile& pile, const std::vector<HWND>& hwnds, bool readPlacement, bool animate) {
    UINT dpi = DpiForPoint(pile.pos);
    std::vector<HWND> visible, shoot;
    for (HWND h : hwnds) {
        Member* m = pile.Find(h);
        if (!m || m->hidden) continue;
        if (readPlacement) {
            m->placement.length = sizeof(WINDOWPLACEMENT);
            GetWindowPlacement(h, &m->placement);
        }
        visible.push_back(h);
    }
    int pileId = pile.id;
    if (!visible.empty()) {
        // 0) remember how they overlap right now (front-most gets the highest stamp; put away now =
        //    in front of the ones put away earlier), so unfolding restores the same stacking order
        std::vector<HWND> order = FrontToBack(visible);
        for (size_t i = 0; i < order.size(); ++i) pile.Find(order[i])->stamp = nextStamp_ + (order.size() - i);
        nextStamp_ += order.size() + 1;
        for (HWND h : visible)
            if (pile.ShowsPreview(*pile.Find(h))) shoot.push_back(h);
        // 1) previews while the windows are still visible (spec §4.3) — only for the cards in front
        std::vector<Image> shots = snapshots_.Capture(shoot, Scale(Pile::kPreviewW, dpi));
        for (size_t i = 0; i < shoot.size(); ++i)
            if (!shots[i].empty()) pile.Find(shoot[i])->snapshot = std::move(shots[i]);
        std::vector<RECT> from;
        for (HWND h : visible) {
            Member* m = pile.Find(h);
            m->title = WindowTitle(h);
            if (IsWindowVisible(h) && !IsIconic(h)) {
                GetWindowRect(h, &m->lastRect);
                if (pile.ShowsPreview(*m)) from.push_back(m->lastRect);  // a few ghosts say it all
            }
            m->hidden = true;
            m->hiddenAt = GetTickCount();
            // Unread already shown in the title counts (like an app icon badge); a new flash raises "!".
            m->attention = false;
            m->unread = ParseUnread(m->title).count;
        }
        // 2) journal before hiding (spec §4.4 step 4 → 6)
        SaveJournalNow();
        // 3) hide the originals (async: a hung app can't block us) and gather them into the card
        for (HWND h : visible) {
            ShowWindowAsync(h, SW_HIDE);
            // A window dropped onto the card was carried next to it. Once hidden, move it back to
            // where it was before the drag (queued after the hide, so it never flashes): even if the
            // app is too busy for a full restore later, it reappears in its own place.
            if (!readPlacement) MoveHiddenTo(h, pile.Find(h)->placement);
        }
        if (pile.active) {
            pile.active = false;
            frame_.reset();
        }
        pile.folded = true;
        pile.view->Render();  // computes the card rect at the card's own position
        RECT card = pile.view->CardRect();
        if (animate) AnimateRects(from, std::vector<RECT>(from.size(), card), pile.color, kAnimMs, true);
        SetTimer(main_, kTimerVerify, 500, nullptr);
    }
    Pile* p = FindPile(pileId);
    if (!p) return;
    if (p->active) frame_.reset();
    p->folded = true;
    p->active = false;
    ShowCard(*p);
    Refresh(*p);
    SaveJournal();
}

void App::Fold(Pile& pile) {
    std::vector<HWND> all;
    for (auto& m : pile.members) all.push_back(m.hwnd);
    HideMembers(pile, all, true);
}

void App::Activate(Pile& pile) {
    int id = pile.id;
    // One pile is "being worked on" at a time: switching tasks puts the previous one away.
    if (Pile* current = ActivePile(); current && current->id != id) Fold(*current);
    if (Pile* p = FindPile(id)) Expand(*p, true);
}

void App::Expand(Pile& pile, bool makeActive) {
    for (auto it = pile.members.begin(); it != pile.members.end();) {
        if (it->hidden && !ValidateIdentity(EntryOf(*it)).empty()) {
            // Closed or replaced meanwhile: never touch another app; offer it for reopening instead.
            AddMissing(pile, *it);
            it = pile.members.erase(it);
            continue;
        }
        ++it;
    }
    if (pile.members.empty()) {
        pile.folded = true;
        Refresh(pile);
        SaveJournal();
        return;
    }
    // Back-most first, as they overlapped when put away: raising them in this order (kTimerToFront)
    // leaves the one that was on top on top again.
    std::vector<Member*> toShow;
    for (Member* m : pile.FrontToBack())
        if (m->hidden) toShow.insert(toShow.begin(), m);
    double t0 = NowMs(), tAnim = t0, tRestore = t0, tFrame = t0;

    if (makeActive) {
        // Spread out of the card, then the card steps aside for the frame + top tab.
        RECT card = pile.view->CardRect();
        std::vector<RECT> to;
        for (Member* m : toShow) {
            RECT r = RestoreRectOf(*m);
            if (HasArea(r) && pile.ShowsPreview(*m)) to.push_back(r);
        }
        pile.view->SetVisible(false);
        tAnim = NowMs();
        AnimateRects(std::vector<RECT>(to.size(), card), to, pile.color, kAnimMs);
    }
    tRestore = NowMs();

    // Restored front-most first: those are the windows the user sees and reaches for first.
    std::vector<HWND> shown;
    for (auto it = toShow.rbegin(); it != toShow.rend(); ++it) {
        Member* m = *it;
        m->hidden = false;
        m->attention = false;  // the user is looking at it now
        KeepOnScreen(m->placement);
        RestoreWindow(m->hwnd, m->placement, false, stateFrozen_);  // exiting: all at once, bounded wait
        shown.insert(shown.begin(), m->hwnd);
    }
    pile.folded = false;
    pile.active = makeActive;
    tFrame = NowMs();
    if (makeActive) {
        // Restores run asynchronously; raise the windows and refresh the frame's monitors once
        // they have landed.
        toFront_ = shown;
        toFrontWaits_ = 0;
        SetTimer(main_, kTimerToFront, 250, nullptr);
        if (!pile.customName) pile.name = AutoName(pile);
        frame_ = std::make_unique<WorkFrame>(pile, *this);
    } else {
        ShowCard(pile);
    }
    double tRefresh = NowMs();
    Refresh(pile);
    SaveJournal();
    double end = NowMs();
    if (end - t0 > 400)  // diagnostics: what made opening this pile slow
        Log("expand: %zu windows took %.0f ms (card %.0f, animation %.0f, restores %.0f, frame %.0f, redraw %.0f)",
            toShow.size(), end - t0, tAnim - t0, tRestore - tAnim, tFrame - tRestore, tRefresh - tFrame, end - tRefresh);
}

void App::Dissolve(Pile& pile) {
    int id = pile.id;
    if (pile.HiddenCount() > 0) Expand(pile, false);
    if (Pile* p = FindPile(id)) RemovePile(*p);
}

void App::RestoreAll() {
    frame_.reset();
    std::vector<int> ids;
    for (auto& p : piles_) ids.push_back(p->id);
    for (int id : ids) {
        Pile* p = FindPile(id);
        if (!p) continue;
        p->active = false;
        if (p->HiddenCount() > 0) {
            Expand(*p, false);
        } else {
            ShowCard(*p);
            Refresh(*p);
        }
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
        if (changed) Refresh(*p);
    }
    if (failed) {
        SaveJournal();
        Notify(L"일부 창을 넣지 못했어요", L"창 " + std::to_wstring(failed) + L"개는 숨겨지지 않아 그대로 두었어요.");
    }
}

void App::SaveJournal() {
    // A burst of changes (an app closing 30 hidden windows, "모든 창 꺼내기") ends in one write.
    // Only showing windows is deferred this way; hiding always writes first (SaveJournalNow), so a
    // crash can never leave a window hidden that the journal doesn't know about.
    journalDirty_ = true;
    PostFlush();
}

void App::SaveJournalNow() {
    journalDirty_ = false;
    Journal j;
    j.managerPid = GetCurrentProcessId();
    for (auto& p : piles_)
        for (auto& m : p->members)
            if (m.hidden) j.entries.push_back(EntryOf(m));
    j.clean = j.entries.empty();
    WriteJournal(j);
    PersistSoon();
    UpdateTitleHooks();  // the set of hidden windows may have changed
    TrackActivity();     // which pile is being worked on may have changed
}

void App::PostFlush() {
    if (flushPosted_ || !main_) return;
    flushPosted_ = true;
    PostMessageW(main_, WM_APP_FLUSH, 0, 0);
}

void App::Flush() {
    flushPosted_ = false;
    std::vector<int> ids;
    ids.swap(refreshIds_);
    for (int id : ids)
        if (Pile* p = FindPile(id)) Refresh(*p);
    if (journalDirty_) SaveJournalNow();
}

void App::UpdateTitleHooks() {
    // One hook per process that owns a hidden window, scoped to that process: Windows only delivers
    // that process's title changes, so the rest of the system costs us nothing.
    std::vector<DWORD> want;
    for (auto& p : piles_)
        for (auto& m : p->members)
            if (m.hidden && std::find(want.begin(), want.end(), m.pid) == want.end()) want.push_back(m.pid);
    for (auto it = titleHooks_.begin(); it != titleHooks_.end();) {
        if (std::find(want.begin(), want.end(), it->first) == want.end()) {
            UnhookWinEvent(it->second);
            it = titleHooks_.erase(it);
        } else {
            ++it;
        }
    }
    for (DWORD pid : want)
        if (!titleHooks_.count(pid))
            if (HWINEVENTHOOK h = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, nullptr, OnTitleEvent,
                                                  pid, 0, WINEVENT_OUTOFCONTEXT))
                titleHooks_[pid] = h;
}

void CALLBACK App::OnTitleEvent(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    if (!instance_ || idObject != OBJID_WINDOW || idChild != CHILDID_SELF || !hwnd) return;
    Pile* p = instance_->PileOf(hwnd);
    if (!p) return;
    Member* m = p->Find(hwnd);
    if (m && m->hidden) PostMessageW(instance_->main_, WM_APP_TITLE, (WPARAM)hwnd, 0);
}

void App::OnTitleChanged(HWND hwnd) {
    Pile* p = PileOf(hwnd);
    if (!p) return;
    Member* m = p->Find(hwnd);
    if (!m || !m->hidden) return;
    std::wstring title = WindowTitle(hwnd);
    UnreadInfo u = ParseUnread(title);
    bool attention = m->attention || (u.marker && title != m->title);
    if (u.count == m->unread && attention == m->attention) return;
    Log("badge: unread %d -> %d, attention %d (pile %d)", m->unread, u.count, attention, p->id);
    m->unread = u.count;
    m->attention = attention;
    m->title = title;
    p->view->Render();
}

void App::OnAttention(HWND hwnd) {
    Pile* p = PileOf(GetAncestor(hwnd, GA_ROOT));
    if (!p) return;
    Member* m = p->Find(GetAncestor(hwnd, GA_ROOT));
    if (!m || !m->hidden || m->attention) return;
    // Windows flashes a taskbar button when a window may not come to the front (e.g. one we just
    // restored, then folded again). That is not a message from the app.
    if (GetTickCount() - m->hiddenAt < kFlashGraceMs) return;
    Log("badge: flash from a hidden window (pile %d)", p->id);
    m->attention = true;
    p->view->Render();
}

void App::OnMemberGone(HWND hwnd) {
    Pile* p = PileOf(hwnd);
    if (!p) return;
    if (IsWindow(hwnd)) {
        // Out-of-context events are asynchronous: the window may not be fully gone yet. Check again
        // shortly (the idle sweep is the final safety net).
        if (std::find(pendingGone_.begin(), pendingGone_.end(), hwnd) == pendingGone_.end()) pendingGone_.push_back(hwnd);
        SetTimer(main_, kTimerGoneRetry, 150, nullptr);
        return;
    }
    auto& ms = p->members;
    for (auto it = ms.begin(); it != ms.end(); ++it)
        if (it->hwnd == hwnd) {
            // A put-away window that vanished (its app quit or crashed) is kept as "reopen";
            // a window the user closed while it was open simply leaves the pile.
            if (it->hidden) AddMissing(*p, *it);
            ms.erase(it);
            break;
        }
    if (p->Empty()) {
        RemovePile(*p);
        return;
    }
    if (p->members.empty() && p->active) {
        p->active = false;
        p->folded = true;
        frame_.reset();
        ShowCard(*p);
    }
    RefreshSoon(*p);  // an app closing many windows at once: one redraw and one write for all
    SaveJournal();
}

void App::OnMemberShown(HWND hwnd) {
    Pile* p = PileOf(hwnd);
    if (!p) return;
    Member* m = p->Find(hwnd);
    if (!m->hidden || !IsWindowVisible(hwnd)) return;
    m->hidden = false;  // the app brought itself back (e.g. a messenger): respect that
    m->attention = false;
    Refresh(*p);
    SaveJournal();
}

void CALLBACK App::OnObjectEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    if (!instance_ || idObject != OBJID_WINDOW || idChild != CHILDID_SELF || !hwnd) return;
    // Cheap filters; the real work is deferred so it never runs in the middle of another operation.
    if (instance_->PileOf(hwnd)) {
        if (event == EVENT_OBJECT_NAMECHANGE) return;
        PostMessageW(instance_->main_, event == EVENT_OBJECT_DESTROY ? WM_APP_MEMBER_GONE : WM_APP_MEMBER_SHOWN,
                     (WPARAM)hwnd, 0);
        return;
    }
    if (instance_->reclaimActive_ && (event == EVENT_OBJECT_SHOW || event == EVENT_OBJECT_NAMECHANGE) &&
        GetAncestor(hwnd, GA_ROOT) == hwnd)
        PostMessageW(instance_->main_, WM_APP_RECLAIM, (WPARAM)hwnd, 0);
}

// ---------------------------------------------------------------------------------------------
// Shell integration
// ---------------------------------------------------------------------------------------------

POINT App::NextSlot(POINT nearPt) {
    RECT work = WorkAreaForPoint(nearPt);
    UINT dpi = DpiForPoint(nearPt);
    SIZE win = PileWindow::WindowSize(dpi);
    int m = Scale(12, dpi), edge = Scale(6, dpi) - m;  // card 6px from the edge; the shadow hangs over
    int stepX = win.cx - m, stepY = win.cy - m;
    for (int x = work.right - win.cx - edge; x >= work.left - m; x -= stepX) {
        for (int y = work.top + edge; y + win.cy <= work.bottom + m; y += stepY) {
            RECT cand{x + m, y + m, x + win.cx - m, y + win.cy - m};
            bool free = true;
            for (auto& p : piles_) {
                RECT r = p->view->CardRect(), tmp;
                if (IntersectRect(&tmp, &r, &cand)) free = false;
            }
            if (free) return {x, y};
        }
    }
    return {work.right - win.cx - edge, work.top + edge};
}

void App::AddTrayIcon() {
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = main_;
    nid_.uID = kTrayId;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid_.uCallbackMessage = WM_APP_TRAY;
    nid_.hIcon = trayIcon_ ? trayIcon_ : icon_;
    CopyTrunc(nid_.szTip, ARRAYSIZE(nid_.szTip), L"Stack Neverflow · 누르면 사용법");
    Shell_NotifyIconW(NIM_ADD, &nid_);
    nid_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);
}

void App::Notify(const std::wstring& title, const std::wstring& text) {
    NOTIFYICONDATAW n = nid_;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    n.hBalloonIcon = icon_;
    CopyTrunc(n.szInfoTitle, ARRAYSIZE(n.szInfoTitle), title);
    CopyTrunc(n.szInfo, ARRAYSIZE(n.szInfo), text);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void App::ShowGuide() { guide_->Show(); }

void App::ShowTrayMenu() {
    POINT pt;
    GetCursorPos(&pt);
    int hidden = 0;
    for (auto& p : piles_) hidden += p->HiddenCount();
    bool autostart = AutostartEnabled();
    std::vector<MenuItem> items = {
        {kMenuRestoreAll, L"모든 창 꺼내기", hidden ? std::to_wstring(hidden) + L"개" : L"", hidden == 0},
        {kMenuStats, L"작업 기록", L"오늘 쓴 시간"},
        {kMenuAutostart, L"Windows 시작 시 자동 실행", autostart ? L"켜짐 ✓" : L"꺼짐"},
        {kMenuHelp, L"사용법"},
        {},
        {kMenuQuit, L"Neverflow 종료", L"창을 모두 꺼내요"},
    };
    UINT id = ShowMenu(items, pt);
    if (id == kMenuRestoreAll) RestoreAll();
    else if (id == kMenuStats) ShowStats();
    else if (id == kMenuAutostart) {
        SetAutostart(!autostart);
        SetFlag(L"autostart.off", autostart);  // remember the user's choice across launches
    } else if (id == kMenuHelp) ShowGuide();
    else if (id == kMenuQuit) Quit();
}

void App::Quit() {
    if (quitting_) return;
    PauseActivity(L"exit");
    quitting_ = true;
    // Remember the piles exactly as they are (folded stays folded) for the next start/update,
    // then bring every window back so nothing stays hidden while the app is not running.
    PersistNow();
    stateFrozen_ = true;
    RestoreAll();
    if (!WaitForRestores(3000)) ShowStillHidden();  // bounded: a frozen app can't keep us alive
    frame_.reset();
    piles_.clear();
    Journal j;
    j.managerPid = GetCurrentProcessId();
    j.clean = true;
    WriteJournal(j);  // the watchdog sees a clean exit and does nothing
    PostQuitMessage(0);
}

void App::EndSessionNow() {
    Log("end session: frozen=%d", stateFrozen_);
    PauseActivity(L"exit");
    // Logoff, shutdown, restart or an installer closing us (Restart Manager). The piles were saved
    // in WM_QUERYENDSESSION and every window was handed back; finish within Windows' time limit and
    // skip any cleanup that talks to other processes (Explorer may already be gone or frozen).
    if (!stateFrozen_) {  // WM_QUERYENDSESSION never came: do its work now
        PersistNow();
        stateFrozen_ = true;
        RestoreAll();
    }
    if (!WaitForRestores(2000)) ShowStillHidden();
    Journal j;
    j.managerPid = GetCurrentProcessId();
    j.clean = true;
    WriteJournal(j);
    ExitProcess(0);
}

void App::ShowStillHidden() {
    // A restore still stuck on a frozen app dies with our process. A posted show request does not:
    // the app handles it as soon as it runs again, so no window is ever left hidden.
    for (auto& p : piles_)
        for (auto& m : p->members)
            if (IsWindow(m.hwnd) && !IsWindowVisible(m.hwnd)) ShowWindowAsync(m.hwnd, SW_SHOWNA);
}

void App::Revalidate() {
    std::vector<HWND> dead;
    for (auto& p : piles_)
        for (auto& m : p->members)
            if (!IsWindow(m.hwnd)) dead.push_back(m.hwnd);
    for (HWND h : dead) OnMemberGone(h);
    for (auto& p : piles_) p->view->Render();
    if (frame_) frame_->Update();
}

void App::UpdateReclaim() {
    bool anyMissing = false;
    for (auto& p : piles_) anyMissing |= !p->missing.empty();
    if (NowMs() >= launchJoinUntil_) launchJoin_.clear();
    bool want = anyMissing && (NowMs() - startedMs_ < kReclaimWindowMs || !launchJoin_.empty());
    if (want == reclaimActive_) return;
    reclaimActive_ = want;
    if (want) {
        // Apps often show a window first and set its real title a moment later (a browser loading
        // its tabs), so also listen to title changes — but only during this short window.
        nameHook_ = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, nullptr, OnObjectEvent, 0, 0,
                                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    } else if (nameHook_) {
        UnhookWinEvent(nameHook_);
        nameHook_ = nullptr;
        reclaimExe_.clear();
    }
}

void App::TryReclaim(HWND hwnd) {
    // Runs for title changes all over the system while reclaiming, so the cheap checks go first and
    // the process lookups (cached per process) only for windows that could match.
    if (!reclaimActive_ || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || PileOf(hwnd)) return;
    std::wstring title = WindowTitle(hwnd);
    if (title.empty()) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    auto cached = reclaimExe_.find(pid);
    if (cached == reclaimExe_.end()) cached = reclaimExe_.emplace(pid, ProcessExePath(pid)).first;
    const std::wstring exe = Lower(cached->second);
    if (exe.empty()) return;
    // Same app, same window title as before the restart: it belongs in that pile.
    Pile* target = nullptr;
    for (auto& p : piles_)
        if (std::any_of(p->missing.begin(), p->missing.end(),
                        [&](const MissingWindow& w) { return w.title == title && Lower(w.exePath) == exe; })) {
            target = p.get();
            break;
        }
    // Or the first window of an app reopened from a card (usually a new title: no document open).
    auto join = launchJoin_.find(exe);
    if (!target && join != launchJoin_.end()) target = FindPile(join->second);
    if (!target || !CanPile(hwnd)) return;
    if (join != launchJoin_.end() && join->second == target->id) launchJoin_.erase(join);  // one window per launch
    Member m;
    m.hwnd = hwnd;
    m.pid = pid;
    m.createTime = ProcessCreateTime(pid);
    m.className = WindowClass(hwnd);
    m.exePath = cached->second;
    m.title = title;
    GetWindowPlacement(hwnd, &m.placement);
    ClaimMissing(*target, m);  // the same-title entry if there is one, else one of the same app
    target->members.push_back(std::move(m));
    ++reclaimed_;
    SetTimer(main_, kTimerReclaimNote, 2500, nullptr);  // one note for a burst of windows
    // A browser restoring its session opens many windows at once: put them away together.
    reclaimPending_.push_back(hwnd);
    SetTimer(main_, kTimerReclaimFold, 500, nullptr);
    UpdateReclaim();
}

void App::FoldReclaimed() {
    KillTimer(main_, kTimerReclaimFold);
    std::vector<HWND> pending;
    pending.swap(reclaimPending_);
    for (auto& p : piles_) {
        std::vector<HWND> mine;
        for (HWND h : pending)
            if (p->Find(h)) mine.push_back(h);
        if (mine.empty()) continue;
        if (p->folded) HideMembers(*p, mine, false, false);  // no animation: nobody asked for this
        else {
            Refresh(*p);
            SaveJournal();
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Activity log
// ---------------------------------------------------------------------------------------------

void App::TrackActivity() {
    if (stateFrozen_ || quitting_) return;
    Pile* active = ActivePile();
    int id = active ? active->id : 0;
    if (id == trackedPile_) return;
    if (active) {
        activity::Append(activity::Kind::Open, id, active->color, active->name);
        idlePaused_ = activityPaused_ = false;  // opening a pile: the user is clearly here
    } else {
        activity::Append(activity::Kind::Close, trackedPile_);
    }
    trackedPile_ = id;
    if (id) SetTimer(main_, kTimerActivity, 60 * 1000, nullptr);
    else KillTimer(main_, kTimerActivity);
}

void App::CheckIdle() {
    if (!trackedPile_ || activityPaused_) return;
    LASTINPUTINFO li{sizeof(li)};
    if (!GetLastInputInfo(&li)) return;
    DWORD idleMs = GetTickCount() - li.dwTime;
    uint64_t lastInput = activity::NowUtc() - (uint64_t)idleMs * 10000;
    // Away for a while counts from the last key press / mouse move, back from the latest one.
    if (!idlePaused_ && idleMs >= kIdleMs) {
        activity::Append(activity::Kind::Pause, 0, 0, L"idle", lastInput);
        idlePaused_ = true;
    } else if (idlePaused_ && idleMs < kIdleMs) {
        activity::Append(activity::Kind::Resume, 0, 0, {}, lastInput);
        idlePaused_ = false;
    }
}

void App::PauseActivity(const wchar_t* reason) {
    if (!trackedPile_ || activityPaused_) return;
    CheckIdle();  // an idle stretch before a lock or sleep ends at the last input, not now
    if (!idlePaused_) activity::Append(activity::Kind::Pause, 0, 0, reason);
    activityPaused_ = true;
}

void App::ResumeActivity() {
    if (!activityPaused_) return;
    activityPaused_ = idlePaused_ = false;
    if (trackedPile_) activity::Append(activity::Kind::Resume);
}

void App::SetActivityEnabled(bool on) {
    if (!on) {
        if (trackedPile_ && !activityPaused_ && !idlePaused_) activity::Append(activity::Kind::Pause, 0, 0, L"off");
        activity::SetEnabled(false);
        return;
    }
    activity::SetEnabled(true);
    activity::Append(activity::Kind::Start);  // start over from what is open right now
    trackedPile_ = 0;
    idlePaused_ = activityPaused_ = false;
    TrackActivity();
}

void App::ShowStats() {
    if (!stats_)
        stats_ = std::make_unique<StatsWindow>(
            [this](int id, std::wstring& name, COLORREF& color) {
                Pile* p = FindPile(id);
                if (!p) return false;
                name = p->name;
                color = p->color;
                return true;
            },
            [this](bool on) { SetActivityEnabled(on); });
    stats_->Show();
}

LRESULT CALLBACK App::MainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (instance_ && instance_->main_ == h) return instance_->HandleMain(msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT App::HandleMain(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == taskbarCreatedMsg_ && taskbarCreatedMsg_) {  // Explorer restarted
        AddTrayIcon();
        RegisterShellHookWindow(main_);
        return 0;
    }
    if (msg == shellHookMsg_ && shellHookMsg_) {
        if (wp == (HSHELL_REDRAW | HSHELL_HIGHBIT)) OnAttention((HWND)lp);  // HSHELL_FLASH
        return 0;
    }
    switch (msg) {
        case WM_APP_TRAY:
            switch (LOWORD(lp)) {
                case WM_CONTEXTMENU:
                case WM_RBUTTONUP: ShowTrayMenu(); break;
                case NIN_SELECT:
                case NIN_KEYSELECT:
                case WM_LBUTTONUP: ShowGuide(); break;
            }
            return 0;
        case WM_APP_SHOW_GUIDE: ShowGuide(); return 0;
        case WM_APP_SHOW_STATS: ShowStats(); return 0;
        case WM_APP_MEMBER_GONE: OnMemberGone((HWND)wp); return 0;
        case WM_APP_MEMBER_SHOWN: OnMemberShown((HWND)wp); return 0;
        case WM_APP_RECLAIM: TryReclaim((HWND)wp); return 0;
        case WM_APP_TITLE: OnTitleChanged((HWND)wp); return 0;
        case WM_APP_FLUSH: Flush(); return 0;
        case WM_TIMER:
            if (wp == kTimerVerify) VerifyHidden();
            else if (wp == kTimerIdle) {
                snapshots_.ReleaseIfIdle(30000);
                // Safety net for missed destroy events: drop members whose window no longer exists.
                std::vector<HWND> dead;
                for (auto& p : piles_)
                    for (auto& m : p->members)
                        if (!IsWindow(m.hwnd)) dead.push_back(m.hwnd);
                for (HWND h : dead) OnMemberGone(h);
                UpdateReclaim();  // ends the after-restart window when its time is up
            } else if (wp == kTimerToFront) {
                // A window shown by a late restore would land on top: wait (briefly) until they're in.
                if (RestoresPending() && ++toFrontWaits_ < 20) {
                    SetTimer(main_, kTimerToFront, 100, nullptr);
                    return 0;
                }
                KillTimer(main_, kTimerToFront);
                toFrontWaits_ = 0;
                // Front-most to the top, then each one right behind the one in front of it. Every app
                // handles its own (asynchronous) move, in any order, and the chain still comes out right.
                HWND above = HWND_TOP;
                for (auto it = toFront_.rbegin(); it != toFront_.rend(); ++it)
                    if (IsWindow(*it)) {
                        SetWindowPos(*it, above, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
                        above = *it;
                    }
                // We received the click moments ago, so the foreground lock allows this. Best effort.
                if (!toFront_.empty() && IsWindow(toFront_.back()) && !IsIconic(toFront_.back()))
                    SetForegroundWindow(toFront_.back());
                toFront_.clear();
                if (frame_) frame_->Update();  // monitors of the now-visible windows
            } else if (wp == kTimerReclaimNote) {
                KillTimer(main_, kTimerReclaimNote);
                if (reclaimed_ > 0)
                    Notify(L"더미를 다시 묶었어요", L"다시 열린 창 " + std::to_wstring(reclaimed_) + L"개를 원래 더미로 돌려놨어요.");
                reclaimed_ = 0;
            } else if (wp == kTimerReclaimFold) FoldReclaimed();
            else if (wp == kTimerRevalidate) {
                KillTimer(main_, kTimerRevalidate);
                Revalidate();
            } else if (wp == kTimerActivity) CheckIdle();
            else if (wp == kTimerPersist) PersistNow(true);  // the disk write runs off the UI thread
            else if (wp == kTimerGoneRetry) {
                KillTimer(main_, kTimerGoneRetry);
                std::vector<HWND> pending;
                pending.swap(pendingGone_);
                for (HWND h : pending) OnMemberGone(h);  // re-queues itself if still alive
            }
            return 0;
        // Resume, unlock and a display change often arrive together: re-check once, a moment later.
        case WM_DISPLAYCHANGE:
            SetTimer(main_, kTimerRevalidate, 300, nullptr);
            return 0;
        case WM_POWERBROADCAST:
            if (wp == PBT_APMSUSPEND) PauseActivity(L"sleep");
            if (wp == PBT_APMRESUMESUSPEND && !sessionLocked_) ResumeActivity();  // a user woke it up
            if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) SetTimer(main_, kTimerRevalidate, 300, nullptr);
            return TRUE;
        case WM_WTSSESSION_CHANGE:
            if (wp == WTS_SESSION_LOCK) {
                sessionLocked_ = true;
                PauseActivity(L"lock");
            } else if (wp == WTS_SESSION_UNLOCK) {
                sessionLocked_ = false;
                ResumeActivity();
            }
            if (wp == WTS_SESSION_UNLOCK || wp == WTS_REMOTE_CONNECT || wp == WTS_CONSOLE_CONNECT)
                SetTimer(main_, kTimerRevalidate, 300, nullptr);
            return 0;
        case WM_QUERYENDSESSION:
            // Logoff, shutdown, restart, or an installer/updater closing us: save the piles as they
            // are (they come back at the next start), then hand every window back (non-blocking) so
            // its "save changes?" prompt is visible.
            PauseActivity(L"exit");
            PersistNow();
            stateFrozen_ = true;
            RestoreAll();
            return TRUE;
        case WM_ENDSESSION:
            if (wp) EndSessionNow();
            else stateFrozen_ = false;  // shutdown was cancelled
            return 0;
    }
    return DefWindowProcW(main_, msg, wp, lp);
}

}  // namespace wp
