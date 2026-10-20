// Watches other apps' window drags (EVENT_SYSTEM_MOVESIZESTART/END, out-of-context, no injection)
// and, only while a window is being moved, shows drop zones at the top of *every* monitor:
//   [＋ 새 더미]                      always
//   [－ '이름'에서 빼기]              only when the dragged window already belongs to a pile
// Validated in PoC-05: zero idle cost, Esc/resize/tiny-drag are told apart.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace wp {

enum class DropZone { None, NewPile, RemoveFromPile };

class DragHost {
public:
    virtual ~DragHost() = default;
    // Whether the window may go into a pile (not elevated, not a dialog, not the shell...).
    virtual bool CanPile(HWND hwnd) = 0;
    // Name of the pile the window belongs to (empty if none): offers the "빼기" zone.
    virtual std::wstring PileNameOf(HWND hwnd) = 0;
    // The drop zones just appeared: a real "put it somewhere" drag is under way.
    virtual void OnDragTargetsShown(HWND dragged) = 0;
    // While dragging: highlight the pile under the cursor.
    virtual void OnDragHover(HWND dragged, POINT cursor) = 0;
    // A move finished away from where it started. `before` = placement when the drag began
    // (a maximized/snapped window is un-maximized by the drag itself).
    virtual void OnDragDrop(HWND dragged, const WINDOWPLACEMENT& before, POINT cursor, DropZone zone) = 0;
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
    struct Bar {
        DropZone zone = DropZone::NewPile;
        HWND hwnd = nullptr;
        RECT card{};  // hit area (screen px), excludes the shadow margin
        POINT pos{};  // window top-left
        UINT dpi = 96;
        BarState state = BarState::Idle;
        bool shown = false;
    };

    static void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD);
    static LRESULT CALLBACK MsgProc(HWND, UINT, WPARAM, LPARAM);
    void Start(HWND hwnd);
    void End(HWND hwnd);
    void Tick();
    void ShowBars();
    void HideBars();
    void RenderBar(Bar& bar);
    DropZone ZoneAt(POINT pt) const;
    Bar& BarAt(size_t index);

    DragHost& host_;
    HWINEVENTHOOK hook_ = nullptr;
    HWND msgWnd_ = nullptr;
    std::vector<Bar> bars_;  // per monitor: new-pile bar (+ remove bar when relevant)

    bool active_ = false;
    HWND dragged_ = nullptr;
    bool eligible_ = false;
    std::wstring pileName_;  // pile of the dragged window, if any
    RECT startRect_{};
    WINDOWPLACEMENT startPlacement_{sizeof(WINDOWPLACEMENT)};
    POINT startCursor_{};
    double startMs_ = 0;
    bool barShown_ = false;
    bool resize_ = false;

    static DragWatcher* instance_;
};

}  // namespace wp
