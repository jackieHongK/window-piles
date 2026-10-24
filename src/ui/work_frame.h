// "You are working on this pile" indicator, shown while a pile is open (active):
// a thin frame in the pile's color around every monitor that holds its windows (like a screen
// recorder's capture border) plus a small title tab at the very top. Clicking the tab folds the pile.
// The frame is click-through; only the tab takes input.
#pragma once

#include <windows.h>

#include <vector>

#include "groups/pile.h"
#include "surface/pile_window.h"

namespace wp {

class WorkFrame {
public:
    WorkFrame(Pile& pile, PileHost& host);
    ~WorkFrame();
    WorkFrame(const WorkFrame&) = delete;
    WorkFrame& operator=(const WorkFrame&) = delete;

    void Update();  // re-read monitors, name, color, count
    bool HitTab(POINT pt) const;
    void SetHot(bool hot);
    // While a window is being dragged the tab grows into an obvious "drop here to add" target.
    void SetDropMode(bool on);
    Pile& pile() { return pile_; }

private:
    struct Part {
        HMONITOR monitor = nullptr;
        RECT monitorRect{};
        HWND edges[4]{};
        HWND tab = nullptr;
        RECT tabRect{};  // screen px, the visible tab (no shadow margin)
        POINT tabPos{};
        UINT dpi = 96;
    };

    static LRESULT CALLBACK TabProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EdgeProc(HWND, UINT, WPARAM, LPARAM);
    void Build();
    void Destroy();
    void RenderTab(Part& part);

    Pile& pile_;
    PileHost& host_;
    std::vector<Part> parts_;
    bool hot_ = false;
    bool hover_ = false;
    bool dropMode_ = false;
    DWORD lastClickTick_ = 0;
};

// Outlines that fly between window rectangles and the pile card ("gathering" / "spreading").
// Blocks for `durationMs` while pumping messages. No-op when Windows animations are turned off.
// `settle`: on arrival they sink 2 px and come back, like paper landing on a stack.
void AnimateRects(const std::vector<RECT>& from, const std::vector<RECT>& to, COLORREF color, int durationMs,
                  bool settle = false);

}  // namespace wp
