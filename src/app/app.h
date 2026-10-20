// Application core: owns the piles, reacts to drags/clicks, hides and restores real windows,
// keeps the recovery journal current and runs the tray icon.
#pragma once

#include <windows.h>
#include <shellapi.h>

#include <memory>
#include <string>
#include <vector>

#include "groups/pile.h"
#include "interaction/drag_watcher.h"
#include "preview/snapshot.h"
#include "surface/pile_window.h"
#include "ui/guide_window.h"
#include "ui/work_frame.h"

namespace wp {

// Posted to the running instance's main window by a second launch: show the guide.
constexpr UINT WM_APP_SHOW_GUIDE = WM_APP + 10;
constexpr wchar_t kMainWindowClass[] = L"WindowPilesMain";

class App : public PileHost, public DragHost {
public:
    App();
    ~App() override;
    int Run();

    // PileHost (cards and the top tab)
    void OnPileClick(Pile& pile) override;
    void OnPileMoved(Pile& pile) override;
    void OnPileMenu(Pile& pile, POINT pt) override;
    void OnMissingClick(Pile& pile, int index) override;
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
    void HideMembers(Pile& pile, const std::vector<HWND>& hwnds, bool readPlacement);
    void Fold(Pile& pile);
    void Activate(Pile& pile);                 // open and mark as "being worked on"
    void Expand(Pile& pile, bool makeActive);  // bring hidden windows back
    void Dissolve(Pile& pile);
    void TakeOut(HWND hwnd, const WINDOWPLACEMENT& before);  // remove one window from its pile
    void RestoreAll();
    void EditStyle(Pile& pile, POINT anchor);
    void Refresh(Pile& pile);  // rename + redraw card / frame
    void VerifyHidden();
    void SaveJournal();  // recovery journal (hidden windows) + schedules a pile-state save

    // persistence across restarts, updates and reboots
    int RestoreSavedPiles();  // returns how many leftover hidden windows were recovered
    void PersistSoon();
    void PersistNow();
    void AddMissing(Pile& pile, const Member& m);
    void ClaimMissing(Pile& pile, const Member& m);
    static bool AutostartEnabled();
    static void SetAutostart(bool on);

    void OnMemberGone(HWND hwnd);
    void OnMemberShown(HWND hwnd);

    // shell
    void AddTrayIcon();
    void ShowTrayMenu();
    void ShowGuide();
    void Notify(const std::wstring& title, const std::wstring& text);
    POINT NextSlot(POINT nearPt);
    std::wstring AutoName(const Pile& pile) const;
    void Quit();

    HWND main_ = nullptr;
    HICON icon_ = nullptr;
    NOTIFYICONDATAW nid_{};
    UINT taskbarCreatedMsg_ = 0;
    HWINEVENTHOOK objectHook_ = nullptr;

    std::vector<std::unique_ptr<Pile>> piles_;
    std::unique_ptr<WorkFrame> frame_;  // for the active pile
    std::unique_ptr<GuideWindow> guide_;
    int nextId_ = 1;
    int colorIndex_ = 0;
    SnapshotEngine snapshots_;
    std::unique_ptr<DragWatcher> drag_;
    bool quitting_ = false;
    bool stateFrozen_ = false;  // shutting down: keep the saved piles as they were
    std::vector<HWND> pendingGone_;  // destroy events seen before the window was fully gone

    static App* instance_;
};

}  // namespace wp
