// The edge organizer (Neverflow Tray; a paid organizer, open to everyone during the beta): a walnut
// bookmark in the middle of a screen's left or right edge. Hovering it slides out a tray with the piles
// kept in it, stacked top to bottom; their cards are off the desktop, so the screen stays clear.
// Cards go in by dragging them onto the bookmark (or "오거나이저에 꽂기") and out by dragging them
// off the tray (the card follows the pointer, as it does going in). The bookmark itself can be
// dragged along the edge, to the other side or monitor.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "groups/pile.h"

namespace wp {

class OrganizerHost {
public:
    virtual ~OrganizerHost() = default;
    virtual std::vector<Pile*> OrganizerPiles() = 0;                  // piles kept in the tray, top to bottom
    virtual void OnOrganizerOpen(Pile& pile) = 0;                      // a card in the tray was clicked
    // A card in the tray started being dragged: take it out and let its desktop card follow the pointer.
    virtual void OnOrganizerCarryOut(Pile& pile, POINT screenPt) = 0;
    virtual void OnOrganizerMenu(Pile* pile, POINT screenPt) = 0;     // right click on a card, or the bookmark
    virtual void OnOrganizerMoved() = 0;                               // the bookmark moved: remember where
};

class Organizer {
public:
    struct Place {
        bool right = true;
        int yPermille = 500;  // bookmark center, as a share of the monitor's work-area height
        std::wstring monitor;  // device name (\\.\DISPLAY1); empty or unknown → primary monitor
    };

    Organizer(OrganizerHost& host, const Place& place);
    ~Organizer();
    Organizer(const Organizer&) = delete;
    Organizer& operator=(const Organizer&) = delete;

    void Refresh();  // the piles in it changed: redraw the bookmark and, if open, the tray
    // A desktop card is being carried at `pt`: near the bookmark the tray opens as a drop target.
    void DragOver(POINT pt);
    void DragEnd();
    bool IsDropTarget(POINT pt) const;  // dropping a card here keeps it in the tray
    RECT HandleRect() const;            // screen px: where folding/opening animations meet the tray
    const Place& place() const { return place_; }

private:
    static LRESULT CALLBACK HandleProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK TrayProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnHandle(UINT msg, WPARAM wp, LPARAM lp);
    LRESULT OnTray(UINT msg, WPARAM wp, LPARAM lp);
    void Layout();  // monitor, dpi, bookmark rect from place_
    void RenderHandle();
    void RenderTray();
    void ShowTray();
    void HideTray();
    int CardAt(POINT client) const;

    OrganizerHost& host_;
    Place place_;
    HWND handle_ = nullptr, tray_ = nullptr;
    RECT monitor_{}, work_{};
    UINT dpi_ = 96;
    RECT handleRect_{};  // screen
    RECT trayRect_{};    // screen, while open
    bool handleHover_ = false;
    bool trayOpen_ = false;
    bool dropMode_ = false;   // a card is being carried over it
    bool seenInside_ = false;  // the pointer was over the bookmark or tray since it opened
    double outsideSince_ = 0;
    // pressing on the bookmark: a click opens the tray, a drag moves the bookmark
    bool handlePressed_ = false, handleDragging_ = false;
    POINT pressAt_{};
    // pressing on a card in the tray: a click opens the pile, a drag takes the card out
    int pressedCard_ = -1;
    int hoverCard_ = -1;
    std::vector<int> cardIds_;    // pile ids, in drawing order
    std::vector<RECT> cardRects_;  // tray client coords
};

}  // namespace wp
