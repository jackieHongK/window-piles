// Application core: owns the piles, reacts to drags/clicks, hides and restores real windows,
// keeps the recovery journal current and runs the tray icon.
#pragma once

#include <windows.h>
#include <shellapi.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "groups/pile.h"
#include "interaction/drag_watcher.h"
#include "preview/snapshot.h"
#include "surface/organizer.h"
#include "surface/pile_window.h"
#include "ui/guide_window.h"
#include "ui/stats_window.h"
#include "ui/work_frame.h"

namespace wp {

// Posted to the running instance's main window by a second launch: show the guide.
constexpr UINT WM_APP_SHOW_GUIDE = WM_APP + 10;
// `WindowPiles.exe --stats`: the running instance opens "작업 기록".
constexpr UINT WM_APP_SHOW_STATS = WM_APP + 11;
constexpr wchar_t kMainWindowClass[] = L"WindowPilesMain";

class App : public PileHost, public DragHost, public OrganizerHost {
public:
    App();
    ~App() override;
    int Run();

    // PileHost (cards and the top tab)
    void OnPileClick(Pile& pile) override;
    void OnPileMoved(Pile& pile) override;
    void OnPileMenu(Pile& pile, POINT pt) override;
    void OnMissingClick(Pile& pile, int index) override;
    void OnPileDragging(Pile& pile) override;
    // OrganizerHost
    std::vector<Pile*> OrganizerPiles() override;
    void OnOrganizerOpen(Pile& pile) override;
    void OnOrganizerCarryOut(Pile& pile, POINT screenPt) override;
    void OnOrganizerMenu(Pile* pile, POINT screenPt) override;
    void OnOrganizerMoved() override;
    void OnOrganizerReorder(const std::vector<int>& pileIds) override;
    void OnFilesDropped(Pile& pile, const std::vector<std::wstring>& paths) override;
    void OnFileClick(Pile& pile, int index) override;
    void OnFileRemove(Pile& pile, int index) override;
    void OnPileRename(Pile& pile) override;

    // DragHost
    bool CanPile(HWND hwnd) override;
    std::wstring PileNameOf(HWND hwnd) override;
    void OnDragTargetsShown(HWND dragged) override;
    void OnDragHover(HWND dragged, POINT cursor) override;
    void OnDragDrop(HWND dragged, const WINDOWPLACEMENT& before, POINT cursor, DropZone zone) override;
    void OnDragEnd() override;

private:
    static LRESULT CALLBACK MainProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMain(UINT msg, WPARAM wp, LPARAM lp);
    static void CALLBACK OnObjectEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD,
                                       DWORD);

    // pile operations
    Pile* PileAt(POINT pt);
    Pile* PileOf(HWND hwnd);
    Pile* FindPile(int id);
    Pile* ActivePile();
    Pile& CreatePile(POINT nearPt);
    void RemovePile(Pile& pile);
    void HideMembers(Pile& pile, const std::vector<HWND>& hwnds, bool readPlacement, bool animate = true);
    void Fold(Pile& pile);
    void Activate(Pile& pile);                 // open and mark as "being worked on"
    void Expand(Pile& pile, bool makeActive);  // bring hidden windows back
    void Dissolve(Pile& pile);
    void TakeOut(HWND hwnd, const WINDOWPLACEMENT& before);  // remove one window from its pile
    void RestoreAll();
    void EditStyle(Pile& pile, POINT anchor);
    void ShowCard(Pile& pile);     // show its card again in its place in the stacking order
    void Refresh(Pile& pile);      // rename + redraw card / frame
    void RefreshSoon(Pile& pile);  // same, once per burst of changes
    void UpdatePreviews(Pile& pile);  // images only for the cards in front
    void VerifyHidden();
    // Recovery journal (hidden windows) + schedules a pile-state save. SaveJournal coalesces a burst
    // of changes into one write; SaveJournalNow writes at once (always before hiding windows).
    void SaveJournal();
    void SaveJournalNow();
    void PostFlush();
    void Flush();

    // persistence across restarts, updates and reboots
    int RestoreSavedPiles();  // returns how many leftover hidden windows were recovered
    void PersistSoon();
    void PersistNow(bool background = false);
    void AddMissing(Pile& pile, const Member& m);
    void ClaimMissing(Pile& pile, const Member& m);
    static bool AutostartEnabled();
    static void SetAutostart(bool on);

    void OnMemberGone(HWND hwnd);
    void OnMemberShown(HWND hwnd);
    void Revalidate();                 // after resume/unlock/display change: drop dead members, redraw
    void EndSessionNow();              // logoff/shutdown/update: state already saved; exit without blocking
    void ShowStillHidden();            // fallback when a restore is stuck on a frozen app
    // After a restart, windows that apps reopen with the same app + title go back into their pile.
    void UpdateReclaim();
    void TryReclaim(HWND hwnd, bool justShown);
    // Opening a pile reopens its closed apps (once per app); their next window joins the pile.
    void RelaunchMissing(Pile& pile);
    void LaunchIntoPile(Pile& pile, const std::wstring& exePath);
    // The next window of `exePath` (within a minute) joins the pile: it was opened from the pile.
    void JoinNextWindow(Pile& pile, const std::wstring& exePath);
    // Files kept with the pile: open those not already open in it; open one with the app it belongs to.
    void OpenPileFiles(Pile& pile);
    void OpenPileFile(Pile& pile, const std::wstring& path);
    void FoldReclaimed();  // windows matched in the last moment, put away together
    // Activity log ("작업 기록"): which pile is open, minus lock/sleep/idle/exit time.
    void TrackActivity();
    void PauseActivity(const wchar_t* reason);
    void ResumeActivity();
    void CheckIdle();  // runs once a minute, only while a pile is being worked on
    void SetActivityEnabled(bool on);
    void ShowStats();
    // Notification badges for hidden windows (their taskbar buttons are gone).
    void UpdateTitleHooks();          // watch title changes only in processes that own hidden windows
    void OnTitleChanged(HWND hwnd);   // unread count in the title
    void OnAttention(HWND hwnd);      // the app flashed its (hidden) taskbar button
    static void CALLBACK OnTitleEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD,
                                      DWORD);

    // shell
    void AddTrayIcon();
    void ShowTrayMenu();
    void ShowGuide();
    void Notify(const std::wstring& title, const std::wstring& text);
    POINT NextSlot(POINT nearPt);
    std::wstring AutoName(const Pile& pile) const;
    void Quit();

    HWND main_ = nullptr;
    HICON icon_ = nullptr;      // color mark (balloons)
    HICON trayIcon_ = nullptr;  // the color mark at the notification area's size
    NOTIFYICONDATAW nid_{};
    UINT taskbarCreatedMsg_ = 0;
    HWINEVENTHOOK objectHook_ = nullptr;

    std::vector<std::unique_ptr<Pile>> piles_;
    std::unique_ptr<WorkFrame> frame_;  // for the active pile
    std::unique_ptr<Organizer> organizer_;  // the edge organizer, when turned on
    void SetOrganizer(bool on);
    // Keep a pile in the organizer (its card leaves the desktop) or take it out, its card landing at
    // `at` (screen px) or next to the bookmark.
    void Dock(Pile& pile, bool on, const POINT* at = nullptr);
    std::unique_ptr<GuideWindow> guide_;
    int nextId_ = 1;
    uint64_t nextStamp_ = 1;  // for Member::stamp (stacking order when put away)
    int colorIndex_ = 0;
    SnapshotEngine snapshots_;
    std::unique_ptr<DragWatcher> drag_;
    bool quitting_ = false;
    bool stateFrozen_ = false;  // shutting down: keep the saved piles as they were
    std::vector<HWND> pendingGone_;  // destroy events seen before the window was fully gone
    std::vector<HWND> toFront_;      // windows to raise once their async restore has landed, back-most first
    int toFrontWaits_ = 0;
    double startedMs_ = 0;
    bool afterBoot_ = false;  // started soon after Windows did: windows reopened with their old title come back
    bool reclaimActive_ = false;
    HWINEVENTHOOK nameHook_ = nullptr;  // only while reclaiming (title changes are frequent)
    int reclaimed_ = 0;
    std::map<DWORD, std::wstring> reclaimExe_;  // pid → exe path, while reclaiming
    std::vector<HWND> reclaimPending_;          // matched, waiting to be folded together
    std::map<std::wstring, int> launchJoin_;    // lower-case exe → pile id: apps reopened from a card
    double launchJoinUntil_ = 0;                // their windows join until then
    std::vector<HWND> untitledShown_;           // appeared without a title yet: their first title still counts as new
    UINT shellHookMsg_ = 0;                        // HSHELL_* notifications from Explorer
    std::map<DWORD, HWINEVENTHOOK> titleHooks_;    // pid → title-change hook scoped to that process
    bool journalDirty_ = false;
    bool flushPosted_ = false;
    std::vector<int> refreshIds_;  // piles to redraw at the next flush

    std::unique_ptr<StatsWindow> stats_;
    int trackedPile_ = 0;          // pile last written as open in the activity log (0: none)
    bool activityPaused_ = false;  // lock / sleep / exit
    bool idlePaused_ = false;      // no input for a while
    bool sessionLocked_ = false;

    static App* instance_;
};

}  // namespace wp
