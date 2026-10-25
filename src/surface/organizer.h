// The edge organizer (Neverflow Tray; a paid organizer, open to everyone during the beta): a walnut
// bookmark in the middle of a screen's left or right edge. Hovering it slides out a tray with the piles
// kept in it, stacked top to bottom; their cards are off the desktop, so the screen stays clear.
// Cards go in by dragging them onto the bookmark (or "오거나이저에 꽂기") and out by dragging them
// off the tray (the card follows the pointer, as it does going in). The bookmark itself can be
// dragged along the edge, to the other side or monitor. Below the piles: memos, and "＋ 새 메모".
// Cards are dragged up and down to reorder them; a memo card dragged out becomes its window, and
// while one is carried the "＋ 새 메모" button turns into a trash can it is crumpled into.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "groups/pile.h"
#include "common/canvas.h"
#include "notes/memo_store.h"

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
    virtual void OnOrganizerReorder(const std::vector<int>& pileIds) = 0;  // cards dragged into a new order
    // Files or folders from Explorer dropped on a pile's card in the tray.
    virtual void OnOrganizerFilesDropped(Pile& pile, const std::vector<std::wstring>& paths) = 0;
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
    // Something is being carried at `pt`: near the bookmark the tray opens as a drop target. `add`: a window
    // or files, which go into the pile whose card they are dropped on (otherwise a desktop card, which docks).
    void DragOver(POINT pt, bool add = false);
    void DragEnd();
    bool IsDropTarget(POINT pt) const;  // dropping a card here keeps it in the tray
    // How many piles it holds: like a real tray it has a fixed number of slots, as many as fit in 60% of
    // the screen's height (at least 2). A card dropped on a full tray stays on the desktop.
    int Capacity() const;
    bool Full() const;
    void Raise();                // back on top of other always-on-top windows (an open pile's frame)
    void SetVisible(bool show);  // "숨기기" in the tray icon menu
    RECT HandleRect() const;            // screen px: where folding/opening animations meet the tray
    Pile* PileAtScreen(POINT pt);       // the pile whose card in the open tray is at `pt`
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
    int ItemAt(POINT client) const;  // a pile card's index, or one of the kItem* codes; -1 for none
    Pile* PileAt(int item);
    void Activate(int item, POINT screenPt);  // a click on an item
    void MemoMenu(int memo, POINT screenPt);
    void DrawPileCard(Canvas& c, const RECT& card, Pile& p, bool hot);
    void DrawMemoCard(Canvas& c, const RECT& card, const MemoInfo& m, bool hot);
    void DrawTrash(Canvas& c, const RECT& area, double now);
    void EndDrag(POINT client);

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
    bool dropAdd_ = false;    // ... or a window or files, to put into one of its piles
    // Explorer's file drags send no mouse messages: a held button resting on the bookmark opens the tray.
    bool filesDrag_ = false;
    double fileHoverSince_ = 0;
    bool seenInside_ = false;  // the pointer was over the bookmark or tray since it opened
    double outsideSince_ = 0;
    // pressing on the bookmark: a click opens the tray, a drag moves the bookmark
    bool handlePressed_ = false, handleDragging_ = false;
    POINT pressAt_{};
    // pressing on a card in the tray: a click opens it; dragging it moves it within the tray (drag_),
    // or out of it: a pile card back onto the desktop, a memo as its window
    int pressed_ = -1;
    POINT pressPt_{};  // tray client
    struct Drag {
        int item = -1;          // pile index or kItemMemo + memo index, while moving
        int grabDy = 0;         // pointer to card top
        int y = 0;              // pointer, tray client
        int slot = -1;          // where it would land (index in its list)
        bool overTrash = false;  // a memo over the trash can
        RECT card{};            // where it was drawn last, tray client
    } drag_;
    // a memo just dropped into the trash: crumpled and dropped in (animation only; the file is already gone)
    double crumpleStart_ = 0;
    RECT crumpleFrom_{};  // the card, relative to the trash area's top-left
    int hover_ = -1;
    std::vector<int> cardIds_;    // pile ids, in drawing order
    std::vector<RECT> cardRects_;  // tray client coords
    // Memos (notes/memo_store.h) below the piles, listed from their folder when the tray opens.
    std::vector<MemoInfo> memos_;
    std::vector<RECT> memoRects_;
    RECT plusRect_{}, memoMoreRect_{};
};

}  // namespace wp
