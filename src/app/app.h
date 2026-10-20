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

namespace wp {

class App : public PileHost, public DragHost {
public:
    App();
    ~App() override;
    int Run(int recoveredAtStart);

    // PileHost
    void OnPileClick(Pile& pile) override;
    void OnPileMoved(Pile& pile) override;
    void OnPileMenu(Pile& pile, POINT pt) override;

    // DragHost
    bool CanPile(HWND hwnd) override;
    void OnDragHover(HWND dragged, POINT cursor) override;
    void OnDragDrop(HWND dragged, const WINDOWPLACEMENT& before, POINT cursor, bool onBar) override;
    void OnDragEnd() override;

private:
    static LRESULT CALLBACK MainProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMain(UINT msg, WPARAM wp, LPARAM lp);
    static void CALLBACK OnObjectEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD,
                                       DWORD);

    // pile operations
    Pile* PileAt(POINT pt);
    Pile* PileOf(HWND hwnd);
    Pile& CreatePile(POINT nearPt);
    void RemovePile(Pile& pile);
    void HideMembers(Pile& pile, const std::vector<HWND>& hwnds, bool readPlacement);
    void Fold(Pile& pile);
    void Expand(Pile& pile, bool bringToFront);
    void Dissolve(Pile& pile);
    void RestoreAll();
    void Refresh(Pile& pile);  // rename + redraw
    void VerifyHidden();
    void SaveJournal();

    void OnMemberGone(HWND hwnd);
    void OnMemberShown(HWND hwnd);

    // shell
    void AddTrayIcon();
    void ShowTrayMenu();
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
    int nextId_ = 1;
    int colorIndex_ = 0;
    SnapshotEngine snapshots_;
    std::unique_ptr<DragWatcher> drag_;
    bool quitting_ = false;

    static App* instance_;
};

}  // namespace wp
