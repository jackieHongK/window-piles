#include "app.h"

#include <dwmapi.h>

#include <algorithm>
#include <cwctype>
#include <map>

#include "app/app_icon.h"
#include "common/win_util.h"
#include "groups/pile_store.h"
#include "recovery/journal.h"
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
constexpr UINT kTrayId = 1;
constexpr int kAnimMs = 220;
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
    drag_.reset();
    frame_.reset();
    guide_.reset();
    piles_.clear();
    if (nid_.cbSize) Shell_NotifyIconW(NIM_DELETE, &nid_);
    if (main_) DestroyWindow(main_);
    if (icon_) DestroyIcon(icon_);
    instance_ = nullptr;
}

int App::Run() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kMainWindowClass;
    RegisterClassW(&wc);
    // A hidden (never shown) top-level window: tray callbacks, menus, deferred work.
    main_ = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Window Piles", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                            wc.hInstance, nullptr);
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    icon_ = MakeAppIcon(GetSystemMetrics(SM_CXSMICON) * 2);
    AddTrayIcon();
    guide_ = std::make_unique<GuideWindow>();

    // Start with Windows unless the user turned it off; refresh the path (the exe may have moved
    // with an update).
    if (!FlagExists(L"autostart.off")) SetAutostart(true);

    int recovered = RestoreSavedPiles();

    drag_ = std::make_unique<DragWatcher>(*this);
    // Member windows closing or showing themselves. Out-of-context: no injection.
    objectHook_ = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_SHOW, nullptr, OnObjectEvent, 0, 0,
                                  WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    SetTimer(main_, kTimerIdle, 10000, nullptr);
    SaveJournal();

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
    LoadPileState(saved);
    std::vector<uint64_t> claimed;
    for (const SavedPile& sp : saved) {
        auto p = std::make_unique<Pile>();
        p->id = nextId_++;
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
            m.hidden = !IsWindowVisible(m.hwnd);
            if (m.hidden) m.snapshot = MakeIconCard(m.hwnd, Scale(240, GetDpiForSystem()), Scale(135, GetDpiForSystem()));
            claimed.push_back(sm.hwnd);
            p->members.push_back(std::move(m));
        }
        if (p->Empty()) continue;
        p->folded = sp.folded || p->members.empty();
        p->view = std::make_unique<PileWindow>(*p, *this);
        piles_.push_back(std::move(p));
        Pile& pile = *piles_.back();
        if (pile.folded) {
            // After an app restart/update the windows are visible again: put them back in.
            std::vector<HWND> visible;
            for (auto& m : pile.members)
                if (!m.hidden) visible.push_back(m.hwnd);
            HideMembers(pile, visible, false);
        } else {
            Refresh(pile);
        }
    }

    // Anything the last run hid that no pile claims (e.g. both processes were killed): show it.
    int recovered = 0;
    Journal j;
    std::string err;
    if (ReadJournal(j, err) && !j.clean) {
        Journal rest;
        for (auto& e : j.entries)
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

void App::PersistNow() {
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
            sp.members.push_back(sm);
        }
        out.push_back(std::move(sp));
    }
    SavePileState(out);
}

void App::AddMissing(Pile& pile, const Member& m) {
    pile.missing.push_back({m.exePath, FriendlyAppName(FileName(m.exePath)), m.title});
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
        p->view->SetVisible(true);
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
    if (!CanPile(hwnd)) return;
    bool onBar = zone == DropZone::NewPile;
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
    if (pile.members.empty()) return;  // only "reopen" entries: nothing to unfold
    if (pile.folded) Activate(pile);
    else Fold(pile);
}

void App::OnPileMoved(Pile&) { PersistSoon(); }

void App::OnMissingClick(Pile& pile, int index) {
    if (index < 0 || index >= (int)pile.missing.size()) return;
    const MissingWindow& w = pile.missing[index];
    if (w.exePath.empty() || Lower(FileName(w.exePath)) == L"applicationframehost.exe") return;
    // Opens the app; the entry disappears once one of its windows is dropped back into this pile.
    ShellExecuteW(nullptr, L"open", w.exePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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
    items.push_back({kMenuHelp, L"사용법"});
    items.push_back({});
    items.push_back({kMenuQuit, L"종료", L"창을 모두 꺼내요"});

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
    pile.view->Render();
    if (pile.active && frame_) frame_->Update();
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
    int pileId = pile.id;
    if (!visible.empty()) {
        // 1) previews while the windows are still visible (spec §4.3)
        std::vector<Image> shots = snapshots_.Capture(visible, Scale(240, dpi));
        std::vector<RECT> from;
        for (size_t i = 0; i < visible.size(); ++i) {
            Member* m = pile.Find(visible[i]);
            if (!shots[i].empty()) m->snapshot = std::move(shots[i]);
            else if (m->snapshot.empty()) m->snapshot = MakeIconCard(visible[i], Scale(240, dpi), Scale(135, dpi));
            m->title = WindowTitle(visible[i]);
            if (IsWindowVisible(visible[i]) && !IsIconic(visible[i])) {
                GetWindowRect(visible[i], &m->lastRect);
                from.push_back(m->lastRect);
            }
            m->hidden = true;
        }
        // 2) journal before hiding (spec §4.4 step 4 → 6)
        SaveJournal();
        // 3) hide the originals (async: a hung app can't block us) and gather them into the card
        for (HWND h : visible) ShowWindowAsync(h, SW_HIDE);
        if (pile.active) {
            pile.active = false;
            frame_.reset();
        }
        pile.folded = true;
        pile.view->Render();  // computes the card rect at the card's own position
        RECT card = pile.view->CardRect();
        AnimateRects(from, std::vector<RECT>(from.size(), card), pile.color, kAnimMs);
        SetTimer(main_, kTimerVerify, 500, nullptr);
    }
    Pile* p = FindPile(pileId);
    if (!p) return;
    if (p->active) frame_.reset();
    p->folded = true;
    p->active = false;
    p->view->SetVisible(true);
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
    std::vector<Member*> toShow;
    for (auto& m : pile.members)
        if (m.hidden) toShow.push_back(&m);

    if (makeActive) {
        // Spread out of the card, then the card steps aside for the frame + top tab.
        RECT card = pile.view->CardRect();
        std::vector<RECT> to;
        for (Member* m : toShow) {
            RECT r = RestoreRectOf(*m);
            if (HasArea(r)) to.push_back(r);
        }
        pile.view->SetVisible(false);
        AnimateRects(std::vector<RECT>(to.size(), card), to, pile.color, kAnimMs);
    }

    std::vector<HWND> shown;
    for (Member* m : toShow) {
        m->hidden = false;
        RestoreWindow(m->hwnd, m->placement, false);
        shown.push_back(m->hwnd);
    }
    pile.folded = false;
    pile.active = makeActive;
    if (makeActive) {
        for (HWND h : shown)
            SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
        // We just received the click, so the foreground lock allows this. Best effort only.
        if (!shown.empty() && !IsIconic(shown.back())) SetForegroundWindow(shown.back());
        if (!pile.customName) pile.name = AutoName(pile);
        frame_ = std::make_unique<WorkFrame>(pile, *this);
    } else {
        pile.view->SetVisible(true);
    }
    Refresh(pile);
    SaveJournal();
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
            p->view->SetVisible(true);
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
    Journal j;
    j.managerPid = GetCurrentProcessId();
    for (auto& p : piles_)
        for (auto& m : p->members)
            if (m.hidden) j.entries.push_back(EntryOf(m));
    j.clean = j.entries.empty();
    WriteJournal(j);
    PersistSoon();
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
        p->view->SetVisible(true);
    }
    Refresh(*p);
    SaveJournal();
}

void App::OnMemberShown(HWND hwnd) {
    Pile* p = PileOf(hwnd);
    if (!p) return;
    Member* m = p->Find(hwnd);
    if (!m->hidden || !IsWindowVisible(hwnd)) return;
    m->hidden = false;  // the app brought itself back (e.g. a messenger): respect that
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
    nid_.hIcon = icon_;
    CopyTrunc(nid_.szTip, ARRAYSIZE(nid_.szTip), L"창 더미 · 누르면 사용법");
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
        {kMenuAutostart, L"Windows 시작 시 자동 실행", autostart ? L"켜짐 ✓" : L"꺼짐"},
        {kMenuHelp, L"사용법"},
        {},
        {kMenuQuit, L"종료", L"창을 모두 꺼내요"},
    };
    UINT id = ShowMenu(items, pt);
    if (id == kMenuRestoreAll) RestoreAll();
    else if (id == kMenuAutostart) {
        SetAutostart(!autostart);
        SetFlag(L"autostart.off", autostart);  // remember the user's choice across launches
    } else if (id == kMenuHelp) ShowGuide();
    else if (id == kMenuQuit) Quit();
}

void App::Quit() {
    if (quitting_) return;
    quitting_ = true;
    // Remember the piles exactly as they are (folded stays folded) for the next start/update,
    // then bring every window back so nothing stays hidden while the app is not running.
    PersistNow();
    stateFrozen_ = true;
    RestoreAll();
    frame_.reset();
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
                case WM_LBUTTONUP: ShowGuide(); break;
            }
            return 0;
        case WM_APP_SHOW_GUIDE: ShowGuide(); return 0;
        case WM_APP_MEMBER_GONE: OnMemberGone((HWND)wp); return 0;
        case WM_APP_MEMBER_SHOWN: OnMemberShown((HWND)wp); return 0;
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
            } else if (wp == kTimerPersist) PersistNow();
            else if (wp == kTimerGoneRetry) {
                KillTimer(main_, kTimerGoneRetry);
                std::vector<HWND> pending;
                pending.swap(pendingGone_);
                for (HWND h : pending) OnMemberGone(h);  // re-queues itself if still alive
            }
            return 0;
        case WM_DISPLAYCHANGE:
            for (auto& p : piles_) p->view->Render();
            if (frame_) frame_->Update();
            return 0;
        case WM_QUERYENDSESSION:
            // Shutting down/logging off: save the piles as they are (they come back after the
            // restart), then bring every window back so its "save changes?" prompt is visible.
            PersistNow();
            stateFrozen_ = true;
            RestoreAll();
            return TRUE;
        case WM_ENDSESSION:
            if (wp) Quit();
            else stateFrozen_ = false;  // shutdown was cancelled
            return 0;
    }
    return DefWindowProcW(main_, msg, wp, lp);
}

}  // namespace wp
