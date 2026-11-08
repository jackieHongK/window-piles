// Pile marks: a tiny tab in the pile's color hanging from the top edge of every window of a pile that is out on
// the screen, so it's clear which pile a window belongs to (and which windows folding a pile will put away).
// Click-through and never activated; it sits right above its window in the stacking order, so whatever covers
// the window covers the mark too. It follows its window through out-of-context events scoped to the processes
// of marked windows (moves, hides) plus the cloak events of virtual desktops: nothing is polled, and a mark is
// drawn only when it is created or its color or DPI changes (moving it is one SetWindowPos).
#pragma once

#include <windows.h>

#include <map>
#include <vector>

namespace wp {

class WindowMarks {
public:
    WindowMarks();
    ~WindowMarks();
    WindowMarks(const WindowMarks&) = delete;
    WindowMarks& operator=(const WindowMarks&) = delete;

    struct Want {
        HWND hwnd;
        COLORREF color;
    };
    void Set(const std::vector<Want>& windows);  // the windows to mark now (replaces the set; cheap when unchanged)
    void Place(HWND target);                     // that window moved, changed state or came forward: follow it
    void PlaceAllSoon();                          // after windows were restacked (a pile opened): once, shortly
    bool Has(HWND target) const { return marks_.count(target) != 0; }

private:
    struct Mark {
        HWND hwnd = nullptr;
        COLORREF color = 0;
        UINT dpi = 0;  // drawn for (0: not yet)
        SIZE size{};
        POINT pos{};
        bool shown = false;
    };
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    static void CALLBACK OnEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD);
    void Hide(Mark& m);
    void UpdateHooks();

    HWND host_ = nullptr;  // message-only: timers
    std::map<HWND, Mark> marks_;
    std::map<DWORD, std::vector<HWINEVENTHOOK>> hooks_;  // per process of a marked window
    HWINEVENTHOOK cloakHook_ = nullptr;
    static WindowMarks* instance_;
};

}  // namespace wp
