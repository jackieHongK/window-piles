// The edge organizer (Neverflow Tray; a paid organizer, open to everyone during the beta): a walnut
// bookmark in the middle of a screen's left or right edge. Hovering it pulls out a tray (the bookmark
// comes along on its edge) with the piles kept in it; their cards are off the desktop, so the screen
// stays clear. Like a real tray it has a fixed number of slots, and a pile sits in whichever one it was
// put in. Cards go in by dragging them onto the bookmark (or "오거나이저에 꽂기") and out by dragging them
// off the tray (the card follows the pointer, as it does going in); windows and files dragged onto a
// card in the tray join that pile. The bookmark itself can be dragged along the edge, to the other
// side or monitor. Below the piles, memos are Post-it flags whose colored tips stick out of the screen
// edge even while the tray is put away, pulled from a dispenser below them that turns into a trash can while
// a memo, messenger or clip is carried. At the bottom: the clipboard shelf, then the messenger dock.
#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "common/canvas.h"
#include "groups/pile.h"
#include "notes/memo_store.h"
#include "surface/clipboard_shelf.h"
#include "surface/messenger.h"

namespace wp {

// What the organizer (the drawer and its bookmark) can be made of: walnut, and paid materials, the rarer
// and costlier further down (all selectable during the beta). Ids are material ids (ui/material.h).
struct OrganizerMaterial {
    const wchar_t* name;
    const char* id;
};
inline constexpr OrganizerMaterial kOrganizerMaterials[] = {
    {L"월넛 원목", "walnut"},  {L"오크 원목", "oak"},     {L"블랙 애시", "black-ash"},
    {L"헤어라인 알루미늄", "aluminum"}, {L"무광 아크릴", "acrylic"}, {L"그레이 펠트", "felt-gray"},
    {L"투명 글래스", "clear-glass"}, {L"불투명 아크릴 · 코발트", "acrylic-cobalt"}, {L"글리터 석영", "quartz"},
    {L"자개", "nacre"}, {L"로즈우드", "rosewood"}, {L"흑단", "ebony"}, {L"오닉스", "onyx"}, {L"다마스커스 강철", "damascus"},
    // Everyday and playful.
    {L"블랙앤화이트 체커보드", "checkerboard"}, {L"잔디", "turf"}, {L"용암", "lava"}, {L"붉은 벽돌", "brick"},
    {L"라탄", "rattan"}, {L"타월", "italy-towel"}, {L"플로럴", "monpe"},
};

class OrganizerHost {
public:
    virtual ~OrganizerHost() = default;
    virtual std::vector<Pile*> OrganizerPiles() = 0;                  // piles kept in the tray (any order)
    virtual void OnOrganizerOpen(Pile& pile) = 0;                      // a card in the tray was clicked
    // A card in the tray was dragged out of it: take it out and let its desktop card follow the pointer, held
    // at the same spot (fx, fy: 0..1 across and down the card) it was grabbed by in the tray.
    virtual void OnOrganizerCarryOut(Pile& pile, double fx, double fy) = 0;
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
        bool slide = true;     // the tray slides out (240 ms) rather than just appearing
        std::string material = "walnut";  // one of kOrganizerMaterials
        bool pinned = false;   // the drawer stays out (the pin at the end of its logo row)
    };

    Organizer(OrganizerHost& host, const Place& place);
    ~Organizer();
    Organizer(const Organizer&) = delete;
    Organizer& operator=(const Organizer&) = delete;

    void Refresh();  // the piles in it changed: redraw the bookmark and, if open, the tray
    // Something is being carried at `pt`: near the bookmark the tray opens as a drop target. `add`: a window
    // or files, which go into the pile whose card they are dropped on (otherwise a desktop card, which docks).
    // `carried`: that desktop card. Over the open tray it is shown there as it would sit in a slot, settled
    // into the empty one under the pointer, which is where it goes if let go; true while it is (the desktop
    // card should not be seen meanwhile).
    bool DragOver(POINT pt, bool add = false, Pile* carried = nullptr);
    void DragEnd();
    // A window is being dragged while a pile is open: the tray comes out and stays out until the drag ends.
    void OpenForAdd();
    // Slide the tray out now, as if the bookmark had been hovered (the unboxing hand-off); it folds away once the
    // pointer has been in it and left, like any opening.
    void Present();
    void SetSlide(bool on) { place_.slide = on; }
    void SetMaterial(const std::string& id);
    bool IsDropTarget(POINT pt) const;  // dropping a card here keeps it in the tray
    // Like a real tray it has a fixed number of slots on a page, as many as fit on the screen (at most five,
    // at least 2), and pages of them side by side, flipped through below the slots. A new page opens when
    // every slot is taken and another card comes. Capacity: all the slots of all the pages it may have.
    int SlotsPerPage() const;
    int Capacity() const;
    bool Full() const;
    // The slot a pile put in now goes to (counted across the pages: page * SlotsPerPage() + slot): the empty
    // one under `at` (screen) on the page shown, if the tray is open there, else the first empty one.
    int FreeSlot(const POINT* at);
    void Raise();                // back on top of other always-on-top windows (an open pile's frame)
    void SetVisible(bool show);  // "숨기기" in the tray icon menu
    RECT HandleRect() const;            // screen px: where folding/opening animations meet the tray
    Pile* PileAtScreen(POINT pt);       // the pile whose card in the open tray is at `pt`
    // A window let go at `pt` over the messenger dock: its app is kept there. True if it was over the dock.
    bool DropWindowOnDock(HWND hwnd, POINT pt);
    const Place& place() const { return place_; }

private:
    // Where everything goes in the tray (tray client px); the folded flag tips use the same numbers.
    struct Geometry {
        int w = 0, h = 0;
        RECT screen{};          // the tray window
        int bodyLeft = 0, bodyRight = 0;  // the walnut, without what sticks out toward the desktop
        int cardX = 0, slide = 0;
        RECT logo{};
        int slotTop = 0, slots = 0;  // slots: on one page
        int pages = 1, pagerTop = 0; // the pages, and the row that flips through them (only with two or more)
        int memoLabelY = 0, flagTop = 0, flagCount = 0;  // flagCount: rows of flags (a page's worth at most)
        int memoPages = 1, memoPagerTop = 0;  // pages of flags, and the row that flips through them (two or more)
        int clipLabelY = 0, clipTop = 0;  // the clipboard shelf
        int dockLabelY = 0, dockTop = 0;  // the messenger dock
        RECT plus{};                      // the memo dispenser
        RECT handle{};          // the bookmark, pulled out with the tray
    };
    Geometry Measure(int pileCount, bool tallest = false) const;  // tallest: as if all five memo flags were there
    int SlotsMid(const Geometry& g) const;                        // the pile slots' middle, tray client y
    RECT SlotRect(const Geometry& g, int slot) const;
    RECT FlagRect(const Geometry& g, int i) const;
    // Piles by slot: each in its own slot if it's free and fits, the rest in the first free ones.
    std::vector<Pile*> BySlot(int slots, std::vector<Pile*>* overflow) const;
    int PageCount() const;     // pages that hold a pile (at least one), and the new one opened for a drop
    bool ShowPage(int page);   // flips to it (clamped); false if it was showing already
    // Something carried (a card, or a window or files for one) held over the pager's arrows flips the page
    // after a moment, and again while it stays there.
    void HoverPager(POINT client);
    Pile* PileById(int id) const;
    // Memos are flipped through like the pile slots: a page of flags (kFlagMax) at a time, the pages going round.
    int MemoPages() const;
    int MemoFirst() const;  // memos_ index of the first flag on the page shown
    int MemoShown() const;  // flags on the page shown
    bool ShowMemoPage(int page);

    static LRESULT CALLBACK HandleProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK TrayProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK FlagsProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnHandle(UINT msg, WPARAM wp, LPARAM lp);
    LRESULT OnTray(UINT msg, WPARAM wp, LPARAM lp);
    LRESULT OnFlags(UINT msg, WPARAM wp, LPARAM lp);
    void Layout();  // monitor, dpi, bookmark rect from place_
    // Moving it (grabbing the bookmark, or the open drawer by its bookmark or body): where it would go if
    // let go at `pt` — the nearest edge of the monitor under the pointer, at that height — shown meanwhile
    // as a dashed bookmark and a line along that edge.
    Place PlaceAt(POINT pt) const;
    void ShowLanding(POINT pt);
    void HideLanding();
    void ShowStandIn(Pile& p, POINT pt);  // the carried card at slot size (DragOver)
    void HideStandIn();
    void BeginMoveFromTray();  // the drawer was grabbed: fold it and carry its bookmark
    void RenderHandle();
    void RenderTray(bool present = true);  // present = false: only draw trayCanvas_ (ahead of opening)
    void Prepare();                         // memos read and tray drawn while the pointer rests on the bookmark
    void SlideOut();                        // the opening motion, in step with the screen
    void SlideIn();                         // ... and the folding one, back into its bookmark
    void RenderFlags();  // the flag tips at the screen edge while the tray is put away
    void ShowTray();
    // Pinned, it stays out unless forced (숨기기, moving it, pushing it in). animate = false: gone at once, without
    // sliding in (the slide holds up everything else meanwhile).
    void HideTray(bool force = false, bool animate = true);
    int ItemAt(POINT client) const;  // a pile card's index, or one of the kItem* codes; -1 for none
    Pile* PileAt(int item);
    void Activate(int item, POINT screenPt);  // a click on an item
    void MemoMenu(int memo, POINT screenPt);
    bool AddMessenger(const Messenger& m);  // kept (and saved) unless it's there already or the dock is full
    void AddClip(ClipItem item);            // the newest first; one already there moves to the front
    void ShowNotice(int row, const std::wstring& text);  // over a row of tiles for a moment: 0 dock, 1 clips
    void DrawDock(Canvas& c, const Geometry& g);
    void DrawPager(Canvas& c, const Geometry& g);  // the pages: arrows and dots below the slots
    // A pager row: arrows and dots (or "n / m" when there are too many for dots). codes: prev, next, dot + page.
    // armed: -1 / +1 lights that arrow (about to flip); fresh: a page drawn as an outline, -1 for none.
    void DrawPagerRow(Canvas& c, const RECT& row, int pages, int current, const int codes[3], int armed, int fresh, RECT& prev,
                      RECT& next, std::vector<RECT>& dots);
    void DrawClips(Canvas& c, const Geometry& g);
    void DrawDispenser(Canvas& c, const Geometry& g, bool hot);
    void DrawNotice(Canvas& c, const Geometry& g, int row);
    RECT LabelLink(Canvas& c, int labelY, const std::wstring& text, bool hot);  // at the right end of a section's label
    void ShowClipHistory(POINT anchor);
    // The dispenser: a click shoots its sheet out, which flies to where its memo then opens.
    void Launch();
    struct Flight;
    void RenderFlight(Flight& f, double t);  // a flying sheet, t 0..1 of the way
    double NextSheet(double now) const;  // the next sheet coming up out of the slot: 0 .. 1
    void DrawBookmark(Canvas& c, const RECT& r, bool hot, bool body = true);  // body = false: grips and count only
    void DrawPileCard(Canvas& c, const RECT& card, Pile& p, bool hot);
    int CarriedCardLeft(const Geometry& g) const;  // a pile card being dragged in the tray: its left, tray client
    void DrawFlag(Canvas& c, const RECT& flag, const MemoInfo& m, bool hot);
    void DrawTrash(Canvas& c, const RECT& area, double now);
    void EndDrag(POINT client);
    int Mat() const;       // the material, as an index into kMaterials
    COLORREF Ink() const;  // labels, grips and lines on it: cream on dark materials, ink on light ones
    void PresentTray();  // trayCanvas_ where the tray is, or on its way out while it slides
    // Hovering a memo flag: the memo unfolds to the side of the tray (from under it) to be read at a glance.
    void ShowPeek(int memo);
    void HidePeek();
    void RenderPeek();
    static LRESULT CALLBACK PeekProc(HWND, UINT, WPARAM, LPARAM);

    OrganizerHost& host_;
    Place place_;
    HWND handle_ = nullptr, tray_ = nullptr, flags_ = nullptr, peek_ = nullptr;
    HWND landing_ = nullptr;     // where a moved organizer will go (only while moving it)
    std::wstring landingKey_;    // what it shows now, so it's redrawn only when that changes
    HWND standIn_ = nullptr;     // a carried desktop card, at slot size over the open tray
    std::unique_ptr<Canvas> standInCanvas_;
    int standInPile_ = -1;       // the pile it was drawn for (-1: hidden)
    bool trayPressed_ = false;   // the button went down on the open drawer (a drag there moves it)
    // Set to false when this is destroyed: a menu shown from inside it may rebuild or remove the organizer.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    std::unique_ptr<Canvas> trayCanvas_;  // the tray as last drawn
    bool prepared_ = false;               // trayCanvas_ is ready for the next opening
    bool menuOpen_ = false;               // a right-click menu is up: the tray stays out meanwhile
    bool stickyDrop_ = false;             // opened for a window drag: stays out until it ends
    int peekMemo_ = -1;
    double peekStart_ = 0;
    std::unique_ptr<Canvas> peekCanvas_;
    RECT peekRect_{};  // screen, where it unfolds to
    RECT monitor_{}, work_{};
    UINT dpi_ = 96;
    RECT handleRect_{};  // screen
    RECT trayRect_{};    // screen, while open
    RECT flagsRect_{};   // screen, the strip of flag tips
    std::vector<int> flagTipY_;  // flags_ client y of each tip
    bool visible_ = true;     // not put away with "숨기기"
    bool handleHover_ = false;
    bool flagsHover_ = false;
    double edgeSince_ = 0;     // the pointer has rested against the screen edge, along the tray's height, since  // the pointer is on the folded flags (opening them shortly)
    bool trayOpen_ = false;
    bool dropMode_ = false;   // a card is being carried over it
    bool dropAdd_ = false;    // ... or a window or files, to put into one of its piles
    int dropSlot_ = -1;       // the empty slot a carried card is over (on the page shown)
    int page_ = 0;            // the page of pile slots shown
    bool extraPage_ = false;  // a new, empty page opened for a card being brought in (every slot was taken)
    int flipDir_ = 0;         // -1 / +1: something carried rests on the pager's arrow that way
    int wheelAcc_ = 0;        // wheel or touchpad swipe over the slots, toward the next flip
    double wheelAt_ = 0, flippedAt_ = 0;
    RECT pagePrev_{}, pageNext_{};  // the pager's arrows, tray client (empty with one page)
    std::vector<RECT> pageDots_;
    int memoPage_ = 0;  // the page of memo flags shown
    RECT memoPrev_{}, memoNext_{};
    std::vector<RECT> memoDots_;
    bool carriedOut_ = false; // a card just taken out: not a drop target for it until the pointer has left
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
        int item = -1;          // pile index, or kItemMemo / kItemMessenger / kItemClip + index, while moving
        int pileId = -1, from = -1;  // a pile card: which, and the slot (across pages) it was taken from
        int grabDx = 0, grabDy = 0;  // pointer to card top-left
        int x = 0, y = 0;            // pointer, tray client
        int slot = -1;          // where it would land (slot, or index among the flags or messengers)
        bool overTrash = false;  // a memo, messenger or clip over the trash can
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
    RECT plusRect_{}, memoMoreRect_{}, clipMoreRect_{};
    // The dispenser: its sheet as last drawn (tray client px), when one was shot out, and that one's flight.
    RECT sheetRect_{};
    double launchAt_ = 0;
    struct Flight {
        HWND hwnd = nullptr;  // the flying sheet's own window
        double start = 0;     // 0: landed, its window free for the next one
        RECT from{}, to{};    // screen
        std::wstring path;
    };
    std::vector<Flight> flights_;  // several may be in the air at once
    RECT restRect_{};              // the dispenser at rest: hovering it grows it
    RECT growRect_{};              // the grown dispenser: all of it a click target while hovered
    double upAt_ = 0;  // hovered since: the dispenser grows from then
    // The messenger dock (surface/messenger.h): its apps, their tiles, and what their badges said last.
    std::vector<Messenger> messengers_;
    std::vector<RECT> messengerRects_;
    RECT dockRect_{};
    std::wstring badges_;
    int watchTicks_ = 0;
    // The clipboard shelf (surface/clipboard_shelf.h): what was copied lately, newest first, and their tiles.
    std::vector<ClipItem> clips_;
    std::vector<Image> clipThumbs_;  // drawn at tile size
    std::vector<RECT> clipRects_;
    int noticeRow_ = -1;  // a notice over a row of tiles (0 dock, 1 clips), until kTimerNotice
    std::wstring notice_;
};

}  // namespace wp
