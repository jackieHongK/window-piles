// The edge organizer (Neverflow Tray; a paid organizer, open to everyone during the beta): a walnut
// bookmark in the middle of a screen's left or right edge. Hovering it pulls out a tray (the bookmark
// comes along on its edge) with the piles kept in it; their cards are off the desktop, so the screen
// stays clear. Like a real tray it has a fixed number of slots, and a pile sits in whichever one it was
// put in. Cards go in by dragging them onto the bookmark (or "오거나이저에 꽂기") and out by dragging them
// off the tray (the card follows the pointer, as it does going in); windows and files dragged onto a
// card in the tray join that pile. The bookmark itself can be dragged along the edge, to the other
// side or monitor. Below the piles, memos are Post-it flags whose colored tips stick out of the screen
// edge even while the tray is put away, and "＋ 새 메모" turns into a trash can while one is carried.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "common/canvas.h"
#include "groups/pile.h"
#include "notes/memo_store.h"

namespace wp {

class OrganizerHost {
public:
    virtual ~OrganizerHost() = default;
    virtual std::vector<Pile*> OrganizerPiles() = 0;                  // piles kept in the tray (any order)
    virtual void OnOrganizerOpen(Pile& pile) = 0;                      // a card in the tray was clicked
    // A card in the tray started being dragged: take it out and let its desktop card follow the pointer.
    virtual void OnOrganizerCarryOut(Pile& pile, POINT screenPt) = 0;
    virtual void OnOrganizerMenu(Pile* pile, POINT screenPt) = 0;     // right click on a card, or the bookmark
    virtual void OnOrganizerMoved() = 0;                               // the bookmark moved: remember where
    // Piles moved between slots: pileIds[slot] (-1 = empty; ids past the slots don't fit on this screen).
    virtual void OnOrganizerReorder(const std::vector<int>& pileIds) = 0;
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
    // The slot a pile put in now goes to: the empty one under `at` (screen) if the tray is open there,
    // else the first empty one.
    int FreeSlot(const POINT* at);
    void Raise();                // back on top of other always-on-top windows (an open pile's frame)
    void SetVisible(bool show);  // "숨기기" in the tray icon menu
    RECT HandleRect() const;            // screen px: where folding/opening animations meet the tray
    Pile* PileAtScreen(POINT pt);       // the pile whose card in the open tray is at `pt`
    const Place& place() const { return place_; }

private:
    // Where everything goes in the tray (tray client px); the folded flag tips use the same numbers.
    struct Geometry {
        int w = 0, h = 0;
        RECT screen{};          // the tray window
        int bodyLeft = 0, bodyRight = 0;  // the walnut, without what sticks out toward the desktop
        int cardX = 0, slide = 0;
        RECT logo{};
        int slotTop = 0, slots = 0, more = 0;  // more: piles that don't fit on this screen
        int memoLabelY = 0, flagTop = 0, flagCount = 0, flagMore = 0;
        RECT plus{};
        RECT handle{};          // the bookmark, pulled out with the tray
    };
    Geometry Measure(int pileCount) const;
    RECT SlotRect(const Geometry& g, int slot) const;
    RECT FlagRect(const Geometry& g, int i) const;
    // Piles by slot: each in its own slot if it's free and fits, the rest in the first free ones.
    std::vector<Pile*> BySlot(int slots, std::vector<Pile*>* overflow);

    static LRESULT CALLBACK HandleProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK TrayProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK FlagsProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnHandle(UINT msg, WPARAM wp, LPARAM lp);
    LRESULT OnTray(UINT msg, WPARAM wp, LPARAM lp);
    LRESULT OnFlags(UINT msg, WPARAM wp, LPARAM lp);
    void Layout();  // monitor, dpi, bookmark rect from place_
    void RenderHandle();
    void RenderTray();
    void RenderFlags();  // the flag tips at the screen edge while the tray is put away
    void ShowTray();
    void HideTray();
    int ItemAt(POINT client) const;  // a pile card's index, or one of the kItem* codes; -1 for none
    Pile* PileAt(int item);
    void Activate(int item, POINT screenPt);  // a click on an item
    void MemoMenu(int memo, POINT screenPt);
    void DrawBookmark(Canvas& c, const RECT& r, bool hot);
    void DrawPileCard(Canvas& c, const RECT& card, Pile& p, bool hot);
    void DrawFlag(Canvas& c, const RECT& flag, const MemoInfo& m, bool hot);
    void DrawTrash(Canvas& c, const RECT& area, double now);
    void EndDrag(POINT client);

    OrganizerHost& host_;
    Place place_;
    HWND handle_ = nullptr, tray_ = nullptr, flags_ = nullptr;
    RECT monitor_{}, work_{};
    UINT dpi_ = 96;
    RECT handleRect_{};  // screen
    RECT trayRect_{};    // screen, while open
    RECT flagsRect_{};   // screen, the strip of flag tips
    std::vector<int> flagTipY_;  // flags_ client y of each tip
    bool visible_ = true;     // not put away with "숨기기"
    bool handleHover_ = false;
    bool trayOpen_ = false;
    bool dropMode_ = false;   // a card is being carried over it
    bool dropAdd_ = false;    // ... or a window or files, to put into one of its piles
    int dropSlot_ = -1;       // the empty slot a carried card is over
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
        int slot = -1;          // where it would land (slot, or index among the flags)
        bool overTrash = false;  // a memo over the trash can
        RECT card{};            // where it was drawn last, tray client
    } drag_;
    // a memo just dropped into the trash: crumpled and dropped in (animation only; the file is already gone)
    double crumpleStart_ = 0;
    RECT crumpleFrom_{};  // the flag, relative to the trash area's top-left
    int hover_ = -1;
    std::vector<int> cardIds_;    // pile ids, in drawing order
    std::vector<int> cardSlots_;  // their slots
    std::vector<RECT> cardRects_;  // tray client coords
    // Memos (notes/memo_store.h), listed from their folder when the tray opens or a memo changes.
    std::vector<MemoInfo> memos_;
    std::vector<RECT> memoRects_;
    RECT plusRect_{}, memoMoreRect_{};
};

}  // namespace wp
