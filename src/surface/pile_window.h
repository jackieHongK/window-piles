// The on-screen pile card: a small topmost, non-activating, per-pixel-alpha layered window.
// Title bar in the pile's color + up to 3 overlapped previews. Redrawn only on change.
// While its pile is the active (open) one, the card is hidden and WorkFrame shows instead.
#pragma once

#include <windows.h>

#include <string>
#include <utility>
#include <vector>

#include "groups/pile.h"

namespace wp {

class PileHost {
public:
    virtual ~PileHost() = default;
    virtual void OnPileClick(Pile& pile) = 0;
    virtual void OnPileMoved(Pile& pile) = 0;
    virtual void OnPileMenu(Pile& pile, POINT screenPt) = 0;
    // A "다시 열어야 할 창" row was clicked.
    virtual void OnMissingClick(Pile& pile, int index) = 0;
};

class PileWindow {
public:
    PileWindow(Pile& pile, PileHost& host);
    ~PileWindow();
    PileWindow(const PileWindow&) = delete;
    PileWindow& operator=(const PileWindow&) = delete;

    void Render();  // redraw from the model
    void SetHot(bool hot);
    void SetVisible(bool visible);
    bool visible() const { return visible_; }
    RECT ScreenRect() const;  // whole window (incl. shadow margin)
    RECT CardRect() const;    // the card itself, screen px
    HWND hwnd() const { return hwnd_; }

    static SIZE WindowSize(UINT dpi);  // size with previews and no missing-window list

private:
    HICON IconFor(const std::wstring& exePath);
    int MissingAt(POINT client) const;
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void SnapToWorkArea(bool magnet);

    Pile& pile_;
    PileHost& host_;
    HWND hwnd_ = nullptr;
    SIZE size_{};
    int margin_ = 0;
    bool hot_ = false;
    bool hover_ = false;
    bool visible_ = true;

    std::vector<RECT> missingRects_;  // client coords of the missing-window rows
    int hoverMissing_ = -1;
    std::vector<std::pair<std::wstring, HICON>> icons_;

    bool pressed_ = false;
    bool dragging_ = false;
    POINT pressCursor_{};
    POINT pressPos_{};
    DWORD lastClickTick_ = 0;
};

}  // namespace wp
