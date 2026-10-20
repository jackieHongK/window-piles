// The on-screen pile: a small topmost, non-activating, per-pixel-alpha layered window.
// Folded → card with up to 3 overlapped previews; open → small chip. Redrawn only on change.
#pragma once

#include <windows.h>

#include "groups/pile.h"

namespace wp {

class PileHost {
public:
    virtual ~PileHost() = default;
    virtual void OnPileClick(Pile& pile) = 0;
    virtual void OnPileMoved(Pile& pile) = 0;
    virtual void OnPileMenu(Pile& pile, POINT screenPt) = 0;
};

class PileWindow {
public:
    PileWindow(Pile& pile, PileHost& host);
    ~PileWindow();
    PileWindow(const PileWindow&) = delete;
    PileWindow& operator=(const PileWindow&) = delete;

    void Render();  // redraw from the model (size depends on folded/open)
    void SetHot(bool hot);
    bool hot() const { return hot_; }
    RECT ScreenRect() const;
    HWND hwnd() const { return hwnd_; }

    static SIZE CardSize(UINT dpi);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void SnapToWorkArea(bool magnet);

    Pile& pile_;
    PileHost& host_;
    HWND hwnd_ = nullptr;
    SIZE size_{};
    bool hot_ = false;

    // mouse interaction
    bool pressed_ = false;
    bool dragging_ = false;
    POINT pressCursor_{};
    POINT pressPos_{};
    DWORD lastClickTick_ = 0;
};

}  // namespace wp
