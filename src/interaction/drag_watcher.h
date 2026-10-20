// Watches other apps' window drags (EVENT_SYSTEM_MOVESIZESTART/END, out-of-context, no injection)
// and shows the "＋ 새 더미" drop bar at the top of the monitor only while a window is being moved.
// Validated in PoC-05: zero idle cost, Esc/resize/tiny-drag are told apart.
#pragma once

#include <windows.h>

namespace wp {

class DragHost {
public:
    virtual ~DragHost() = default;
    // Whether the window may go into a pile (not elevated, not a dialog, not the shell...).
    virtual bool CanPile(HWND hwnd) = 0;
    // While dragging: highlight the pile under the cursor (nullptr-equivalent handled by host).
    virtual void OnDragHover(HWND dragged, POINT cursor) = 0;
    // A move finished away from where it started. `before` = placement when the drag began
    // (a maximized/snapped window is un-maximized by the drag itself). `onBar` = released on the bar.
    virtual void OnDragDrop(HWND dragged, const WINDOWPLACEMENT& before, POINT cursor, bool onBar) = 0;
    virtual void OnDragEnd() = 0;  // always called last (clear highlights)
};

class DragWatcher {
public:
    explicit DragWatcher(DragHost& host);
    ~DragWatcher();
    DragWatcher(const DragWatcher&) = delete;
    DragWatcher& operator=(const DragWatcher&) = delete;

    bool dragging() const { return active_; }

private:
    enum class BarState { Idle, Hot, Blocked };

    static void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD);
    static LRESULT CALLBACK MsgProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK BarProc(HWND, UINT, WPARAM, LPARAM);
    void Start(HWND hwnd);
    void End(HWND hwnd);
    void Tick();
    void ShowBar(POINT cursor);
    void HideBar();
    void SetBarState(BarState s);

    DragHost& host_;
    HWINEVENTHOOK hook_ = nullptr;
    HWND msgWnd_ = nullptr;
    HWND bar_ = nullptr;
    RECT barRect_{};
    BarState barState_ = BarState::Idle;

    bool active_ = false;
    HWND dragged_ = nullptr;
    bool eligible_ = false;
    RECT startRect_{};
    WINDOWPLACEMENT startPlacement_{sizeof(WINDOWPLACEMENT)};
    POINT startCursor_{};
    double startMs_ = 0;
    bool barShown_ = false;
    bool resize_ = false;

    static DragWatcher* instance_;
};

}  // namespace wp
