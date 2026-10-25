#include "organizer.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "common/canvas.h"
#include "common/win_util.h"
#include "notes/memo_format.h"
#include "notes/memo_list_window.h"
#include "ui/list_popup.h"
#include "notes/memo_store.h"
#include "notes/memo_window.h"
#include "ui/material.h"
#include "ui/menu.h"
#include "ui/theme.h"
#include "ui/wordmark.h"

namespace wp {

namespace {

// Layout in DIPs.
constexpr int kHandleW = 14, kHandleHoverW = 18, kHandleH = 128;
constexpr int kTrayPad = 14, kLogoH = 30, kSectionGap = 14;
constexpr int kCardW = 260, kCardHeader = 26, kCardStrip = 40, kCardGap = 12;
constexpr int kCardH = kCardHeader + kCardStrip;
constexpr int kMemoLabelH = 22, kFlagH = 26, kFlagGap = 6, kFlagMax = 5, kPlusH = 36, kMoreH = 22;
// A memo flag: its colored tip sticks out this far past the tray's edge (and out of the screen edge while
// the tray is put away); the tip is this wide in all.
constexpr int kFlagOut = 12, kFlagTip = 24;
constexpr int kOut = kHandleW;          // the tray window's room for what sticks out toward the desktop
constexpr int kSlots = 5, kMinSlots = 2;  // five pile slots, fewer only on a screen too short for them
// Hit codes for what isn't a pile card (pile cards are their index).
constexpr int kItemMemo = 1000, kItemPlus = 2000, kItemMemoMore = 2001, kItemHandle = 2002, kItemPin = 2003, kItemClipMore = 2004;
constexpr int kItemDock = 2999, kItemMessenger = 3000;  // the dock's row; + index: one of its apps
constexpr int kItemClip = 4000;                         // + index: an item on the clipboard shelf
constexpr int kTile = 44;                               // a messenger's (and a clip's) tile
constexpr UINT kNoticeMs = 1800;                        // a notice over a row of tiles
constexpr double kGrowMs = 160;  // hovered, the dispenser grows over the sections below this fast
constexpr double kNextMs = 300;  // one shot out, the next sheet comes up out of the slot this long
constexpr double kFlyMs = 460;   // ... while the one shot out flies to where its memo opens
bool IsMessenger(int item) { return item >= kItemMessenger && item < kItemClip; }
bool IsClip(int item) { return item >= kItemClip; }
constexpr int kPin = 28;  // the pin button at the end of the logo row
constexpr int kLift = 10;            // a hovered card slides this far out of the tray
constexpr double kHideAfterMs = 350;  // the pointer left the bookmark and tray this long ago: put it away
constexpr UINT_PTR kTimerWatch = 1, kTimerOpen = 2, kTimerAnim = 3, kTimerFiles = 4, kTimerPeek = 6, kTimerNotice = 7, kTimerSheet = 8, kTimerFly = 9, kTimerEdge = 10;
constexpr double kSlideMs = 240;    // the brand motion: 240 ms ease-out
constexpr double kSlideInMs = 200;  // folding back in, a little quicker
constexpr UINT kOpenDelayMs = 120;  // resting on the bookmark this long opens the drawer
constexpr double kPeekMs = 160;   // a memo unfolding from under the tray
constexpr int kPeekW = 300, kPeekMaxH = 380;
// Crumpling a memo into the trash: it is slowly crushed into a ball until kBallAt, falls in until kLandAt
// (the lid starts shutting at kShutAt), then the can settles.
constexpr double kCrumpleMs = 1050, kBallAt = 0.62, kShutAt = 0.79, kLandAt = 0.82;
constexpr wchar_t kHandleClass[] = L"WindowPilesOrganizerHandle";
constexpr wchar_t kTrayClass[] = L"WindowPilesOrganizerTray";
constexpr wchar_t kFlagsClass[] = L"WindowPilesOrganizerFlags";
constexpr wchar_t kPeekClass[] = L"WindowPilesOrganizerPeek";
constexpr wchar_t kFlyClass[] = L"WindowPilesOrganizerFlight";
const COLORREF kCream = RGB(246, 238, 228);
const COLORREF kFlagPaper = RGB(252, 250, 244);
constexpr const MemoColor* kFlagColors = kMemoColors;  // notes/memo_store.h
constexpr int kFlagColorCount = kMemoColorCount;

COLORREF FlagColorOf(const MemoInfo& m) { return MemoColorOf(m); }


HWND MakeWindow(const wchar_t* cls, WNDPROC proc, void* self, const wchar_t* title) {
    WNDCLASSW wc{};
    if (!GetClassInfoW(GetModuleHandleW(nullptr), cls, &wc)) {
        wc = {};
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
        wc.lpszClassName = cls;
        RegisterClassW(&wc);
    }
    HWND h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, title, WS_POPUP, 0,
                             0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    return h;
}

// The top part of a window snapshot in the strip's proportions.
Image TopPart(const Image& img, int w, int h) {
    int keep = std::max(1, img.w * h / std::max(1, w));
    if (img.h <= keep) return img;
    Image out;
    out.w = img.w;
    out.h = keep;
    out.px.assign(img.px.begin(), img.px.begin() + (size_t)img.w * keep);
    return out;
}

// A memo card pulled off the tray: its window comes up under the pointer and, while the button is still
// down, keeps following it (the system's own window move), so the sheet is simply carried away.
void TearOffMemo(const std::wstring& path, POINT pt, UINT dpi) {
    HWND w = OpenMemoWindow(path, pt);
    if (!w) return;
    RECT r;
    GetWindowRect(w, &r);
    SetWindowPos(w, nullptr, pt.x - (r.right - r.left) / 2, pt.y - Scale(18, dpi), 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    if (GetKeyState(VK_LBUTTON) < 0) PostMessageW(w, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
}

}  // namespace

Organizer::Organizer(OrganizerHost& host, const Place& place) : host_(host), place_(place) {
    handle_ = MakeWindow(kHandleClass, &Organizer::HandleProc, this, L"Neverflow 오거나이저");
    tray_ = MakeWindow(kTrayClass, &Organizer::TrayProc, this, L"Neverflow 오거나이저 트레이");
    flags_ = MakeWindow(kFlagsClass, &Organizer::FlagsProc, this, L"Neverflow 메모 플래그");
    peek_ = MakeWindow(kPeekClass, &Organizer::PeekProc, this, L"Neverflow 메모 미리보기");
    SetWindowLongPtrW(peek_, GWL_EXSTYLE, GetWindowLongPtrW(peek_, GWL_EXSTYLE) | WS_EX_TRANSPARENT);  // only to look at
    DragAcceptFiles(tray_, TRUE);  // files dropped on a card in the tray join that pile
    // ponytail: polls the button every 150 ms for Explorer drags; an OLE drop target needs an STA thread
    // (this one is MTA for WinRT capture), worth it only if the poll ever shows up.
    SetTimer(handle_, kTimerFiles, 150, nullptr);
    // ponytail: the screen edge along the tray's height is watched by polling the pointer (cheap) rather than a
    // strip window there, which would take clicks from whatever else is at the edge (a scroll bar).
    SetTimer(handle_, kTimerEdge, 40, nullptr);
    Layout();
    memos_ = ListMemos();
    messengers_ = LoadMessengers();
    OnMemoSaved([this] {
        if (drag_.item >= 0) return;  // the flags are being rearranged: they're read again when that ends
        memos_ = ListMemos();
        RenderFlags();
        if (trayOpen_) RenderTray();
    });
    AddClipboardFormatListener(handle_);  // the clipboard shelf (removed with the window)
    RenderHandle();
    ShowWindow(handle_, SW_SHOWNOACTIVATE);
    RenderFlags();
    if (place_.pinned) ShowTray();  // pinned last time: out from the start
}

Organizer::~Organizer() {
    *alive_ = false;
    OnMemoSaved(nullptr);
    for (Flight& f : flights_) DestroyWindow(f.hwnd);
    for (HWND h : {handle_, tray_, flags_, peek_})
        if (h) {
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            DestroyWindow(h);
        }
}

void Organizer::Layout() {
    HMONITOR mon = nullptr;
    struct Find {
        const std::wstring* name;
        HMONITOR found;
    } find{&place_.monitor, nullptr};
    if (!place_.monitor.empty())
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM lp) -> BOOL {
            auto* f = (Find*)lp;
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(m, &mi) && *f->name == mi.szDevice) {
                f->found = m;
                return FALSE;
            }
            return TRUE;
        }, (LPARAM)&find);
    mon = find.found ? find.found : MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    monitor_ = mi.rcMonitor;
    work_ = mi.rcWork;
    dpi_ = DpiForPoint({(monitor_.left + monitor_.right) / 2, (monitor_.top + monitor_.bottom) / 2});
    int w = Scale(handleHover_ ? kHandleHoverW : kHandleW, dpi_), h = Scale(kHandleH, dpi_);
    int cy = work_.top + MulDiv(work_.bottom - work_.top, std::clamp(place_.yPermille, 0, 1000), 1000);
    cy = std::clamp<int>(cy, work_.top + h / 2, work_.bottom - h / 2);
    // Kept where the tray, at its tallest, opens with its slots beside the bookmark and still fits the screen.
    Geometry g = Measure(0, true);
    int lo = work_.top + Scale(16, dpi_) + SlotsMid(g), hi = work_.bottom - Scale(16, dpi_) - g.h + SlotsMid(g);
    if (lo <= hi) cy = std::clamp(cy, lo, hi);
    int x = place_.right ? monitor_.right - w : monitor_.left;
    handleRect_ = {x, cy - h / 2, x + w, cy + h / 2};
}

RECT Organizer::HandleRect() const { return handleRect_; }

int Organizer::Mat() const { return MaterialFromId(place_.material.c_str()); }

COLORREF Organizer::Ink() const { return kMaterials[Mat()].dark ? kCream : RGB(58, 50, 42); }

void Organizer::SetMaterial(const std::string& id) {
    place_.material = id;
    Refresh();
}

int Organizer::Capacity() const {
    auto S = [this](int v) { return Scale(v, dpi_); };
    // Counted with the memo flags at their most, so the number of slots doesn't move as memos come and go.
    int maxH = work_.bottom - work_.top - S(32);
    int fixed = S(kTrayPad + kLogoH + kSectionGap + kSectionGap + kMemoLabelH + 6 + kFlagMax * (kFlagH + kFlagGap) +
                  kPlusH + kSectionGap + 2 * (kMemoLabelH + 6 + kTile) + kSectionGap + kTrayPad);
    return std::clamp((maxH - fixed + S(kCardGap)) / S(kCardH + kCardGap), kMinSlots, kSlots);
}

bool Organizer::Full() const { return (int)host_.OrganizerPiles().size() >= Capacity(); }

Organizer::Geometry Organizer::Measure(int pileCount, bool tallest) const {
    auto S = [this](int v) { return Scale(v, dpi_); };
    Geometry g;
    g.slots = Capacity();
    g.more = std::max(0, pileCount - g.slots);
    g.flagCount = tallest ? kFlagMax : std::min<int>((int)memos_.size(), kFlagMax);
    g.w = S(kOut) + S(kTrayPad * 2 + kCardW + kLift);
    g.bodyLeft = place_.right ? S(kOut) : 0;
    g.bodyRight = place_.right ? g.w : g.w - S(kOut);
    // Cards sit toward the screen edge; a hovered or carried one slides out toward the desktop.
    g.cardX = g.bodyLeft + S(kTrayPad) + (place_.right ? S(kLift) : 0);
    g.slide = place_.right ? -S(kLift) : S(kLift);
    int y = S(kTrayPad);
    g.logo = {g.cardX, y, g.cardX + S(kCardW), y + S(kLogoH)};
    y += S(kLogoH + kSectionGap);
    g.slotTop = y;
    y += g.slots * S(kCardH + kCardGap) - S(kCardGap);
    if (g.more) y += S(kMoreH);
    y += S(kSectionGap);
    // Memos: their flags, and the dispenser a new one is pulled from (the trash while something is carried).
    g.memoLabelY = y;
    y += S(kMemoLabelH + 6);
    g.flagTop = y;
    y += g.flagCount * S(kFlagH + kFlagGap);
    g.plus = {g.cardX, y, g.cardX + S(kCardW), y + S(kPlusH)};
    y += S(kPlusH + kSectionGap);
    // The clipboard shelf, then the messenger dock at the very bottom: a label and one row of tiles each
    // (the empty places too).
    g.clipLabelY = y;
    y += S(kMemoLabelH + 6);
    g.clipTop = y;
    y += S(kTile + kSectionGap);
    g.dockLabelY = y;
    y += S(kMemoLabelH + 6);
    g.dockTop = y;
    g.h = y + S(kTile + kTrayPad);
    // The bookmark sits beside the middle of the pile slots, so the memo flags' tips (on the same screen edge)
    // are always below it, never over it.
    int cy = (handleRect_.top + handleRect_.bottom) / 2;
    int top = std::clamp<int>(cy - SlotsMid(g), work_.top + S(16), std::max<int>(work_.top + S(16), work_.bottom - S(16) - g.h));
    int left = place_.right ? monitor_.right - g.w : monitor_.left;
    g.screen = {left, top, left + g.w, top + g.h};
    // The bookmark comes out with the tray, on its desktop side, at the height it was on the screen edge.
    int hh = S(kHandleH), hy = std::clamp<int>(cy - top, hh / 2, g.h - hh / 2);
    int hx = place_.right ? 0 : g.w - S(kHandleW);
    g.handle = {hx, hy - hh / 2, hx + S(kHandleW), hy + hh / 2};
    return g;
}

int Organizer::SlotsMid(const Geometry& g) const {
    return g.slotTop + (g.slots * Scale(kCardH + kCardGap, dpi_) - Scale(kCardGap, dpi_)) / 2;
}

RECT Organizer::SlotRect(const Geometry& g, int slot) const {
    int y = g.slotTop + slot * Scale(kCardH + kCardGap, dpi_);
    return {g.cardX, y, g.cardX + Scale(kCardW, dpi_), y + Scale(kCardH, dpi_)};
}

RECT Organizer::FlagRect(const Geometry& g, int i) const {
    auto S = [this](int v) { return Scale(v, dpi_); };
    int y = g.flagTop + i * S(kFlagH + kFlagGap);
    // From the tip, which sticks out past the tray's edge toward the desktop, to the card column's far side.
    return place_.right ? RECT{g.bodyLeft - S(kFlagOut), y, g.cardX + S(kCardW), y + S(kFlagH)}
                        : RECT{g.cardX, y, g.bodyRight + S(kFlagOut), y + S(kFlagH)};
}

std::vector<Pile*> Organizer::BySlot(int slots, std::vector<Pile*>* overflow) {
    std::vector<Pile*> piles = host_.OrganizerPiles(), out(slots, nullptr), rest;
    std::stable_sort(piles.begin(), piles.end(), [](Pile* a, Pile* b) { return a->dockOrder < b->dockOrder; });
    for (Pile* p : piles)
        if (p->dockOrder >= 0 && p->dockOrder < slots && !out[p->dockOrder]) out[p->dockOrder] = p;
        else rest.push_back(p);
    for (Pile* p : rest) {
        auto free = std::find(out.begin(), out.end(), nullptr);
        if (free != out.end()) *free = p;
        else if (overflow) overflow->push_back(p);
    }
    return out;
}

int Organizer::FreeSlot(const POINT* at) {
    Geometry g = Measure((int)host_.OrganizerPiles().size());
    std::vector<Pile*> by = BySlot(g.slots, nullptr);
    if (at && trayOpen_) {
        POINT c{at->x - trayRect_.left, at->y - trayRect_.top};
        for (int s = 0; s < g.slots; ++s) {
            RECT r = SlotRect(g, s);
            InflateRect(&r, 0, Scale(kCardGap / 2, dpi_));
            if (!by[s] && PtInRect(&r, c)) return s;
        }
    }
    auto free = std::find(by.begin(), by.end(), nullptr);
    return free == by.end() ? g.slots : (int)(free - by.begin());
}

void Organizer::Raise() {
    for (HWND h : {handle_, tray_, flags_})
        if (IsWindowVisible(h)) SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void Organizer::SetVisible(bool show) {
    visible_ = show;
    if (!show) HideTray(true);
    ShowWindow(handle_, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    RenderFlags();
    if (show) Raise();
}

// The walnut bookmark: rounded toward the desktop, flush on the other side, with grip ridges.
void Organizer::DrawBookmark(Canvas& c, const RECT& b, bool hot, bool body) {
    auto S = [this](int v) { return Scale(v, dpi_); };
    int w = b.right - b.left, h = b.bottom - b.top, r = S(7);
    if (body) {
        RECT shape = place_.right ? RECT{b.left, b.top, b.right + r, b.bottom} : RECT{b.left - r, b.top, b.right, b.bottom};
        FillMaterial(c, shape, r, Mat(), dpi_);
        if (hot) c.FillRoundRect(shape, r, RGB(255, 255, 255), 30);
    }
    int gx = place_.right ? b.left + S(5) : b.right - S(10);
    for (int i = -1; i <= 1; ++i)
        c.FillRoundRect({gx, b.top + h / 2 + i * S(6) - S(1), gx + S(5), b.top + h / 2 + i * S(6) + S(1)}, S(1), Ink(), 200);
    size_t n = host_.OrganizerPiles().size();
    if (n) c.Text(std::to_wstring(n), {b.left, b.top + S(8), b.left + w, b.top + S(22)}, Ink(), S(10), true,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void Organizer::RenderHandle() {
    if (handleDragging_) return;  // follows the pointer; drawn once it lands
    Layout();
    int w = handleRect_.right - handleRect_.left, h = handleRect_.bottom - handleRect_.top;
    Canvas c(w, h);
    DrawBookmark(c, {0, 0, w, h}, handleHover_ || dropMode_);
    c.Present(handle_, {handleRect_.left, handleRect_.top});
}

// While the tray is put away, the memo flags' colored tips still stick out of the screen edge, where
// they are on the open tray.
void Organizer::RenderFlags() {
    Geometry g = Measure((int)host_.OrganizerPiles().size());
    if (!visible_ || trayOpen_ || handleDragging_ || !g.flagCount) {
        ShowWindow(flags_, SW_HIDE);
        return;
    }
    auto S = [this](int v) { return Scale(v, dpi_); };
    int w = S(kFlagOut), unit = S(kFlagH + kFlagGap), h = g.flagCount * unit - S(kFlagGap);
    int x = place_.right ? monitor_.right - w : monitor_.left, y = g.screen.top + g.flagTop;
    flagsRect_ = {x, y, x + w, y + h};
    Canvas c(w, h);
    flagTipY_.clear();
    for (int i = 0; i < g.flagCount; ++i) {
        int ty = i * unit;
        flagTipY_.push_back(ty);
        RECT tip = place_.right ? RECT{0, ty, w + S(3), ty + S(kFlagH)} : RECT{-S(3), ty, w, ty + S(kFlagH)};
        c.FillRoundRect(tip, S(2), FlagColorOf(memos_[i]));
    }
    c.Present(flags_, {x, y});
    SetWindowPos(flags_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void Organizer::DrawPileCard(Canvas& c, const RECT& card, Pile& p, bool hot) {
    using namespace theme;
    auto S = [this](int v) { return Scale(v, dpi_); };
    int r = S(1);  // a sheet of paper: barely rounded
    c.Shadow(card, r, S(hot ? 10 : 6), S(2), hot ? 90 : 60, RGB(20, 10, 4));
    FillMaterial(c, card, r, p.material, dpi_);
    RECT head{card.left, card.top, card.right, card.top + S(kCardHeader)};
    c.FillRoundRect(head, r, p.color);
    c.FillRoundRect({head.left, head.bottom - r, head.right, head.bottom}, 0, p.color);
    COLORREF on = ContrastText(p.color);
    int n = (int)p.members.size(), hidden = p.HiddenCount();
    std::wstring count = n ? (hidden == n ? std::to_wstring(n) : std::to_wstring(n - hidden) + L"/" + std::to_wstring(n))
                           : (!p.files.empty() ? L"파일 " + std::to_wstring(p.files.size())
                                               : L"닫힌 창 " + std::to_wstring(p.missing.size()));
    SIZE cs = c.MeasureText(count, S(11), true);
    RECT pill{head.right - S(8) - cs.cx - S(12), head.top + S(5), head.right - S(8), head.bottom - S(5)};
    c.FillRoundRect(pill, S(8), on == kForeground ? RGB(0, 0, 0) : RGB(255, 255, 255), 40);
    c.Text(count, pill, on, S(11), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    c.Text(p.name, {head.left + S(10), head.top, pill.left - S(6), head.bottom}, on, S(12), true,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    // The front window's top strip, as on the card; otherwise what else the pile holds.
    RECT strip{card.left + S(8), head.bottom + S(6), card.right - S(8), card.bottom - S(6)};
    auto front = p.FrontToBack(1);
    if (!front.empty() && !front[0]->snapshot.empty()) {
        c.FillRoundRect(strip, r, kMuted);
        c.DrawImage(TopPart(front[0]->snapshot, strip.right - strip.left, strip.bottom - strip.top), strip, r);
        c.StrokeRoundRect(strip, r, 1, kBorder);
    } else {
        bool dark = kMaterials[ClampMaterial(p.material)].dark;
        std::wstring t = !p.files.empty() ? L"함께 열 파일 " + std::to_wstring(p.files.size()) + L"개"
                         : !p.missing.empty() ? L"다시 열어야 할 창 " + std::to_wstring(p.missing.size()) + L"개"
                                              : L"창 " + std::to_wstring(n) + L"개";
        if (kMaterials[ClampMaterial(p.material)].translucent) c.FillRoundRect(strip, r, RGB(250, 251, 253));
        c.Text(t, strip, dark ? RGB(228, 220, 209) : kSubtleFg, S(12), false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    if (hot) c.StrokeRoundRect(card, r, S(2), Ink());
}

// A Post-it index flag: a paper label with the memo's title in the middle of its height, and the colored
// tip at the end that sticks out toward the desktop.
void Organizer::DrawFlag(Canvas& c, const RECT& flag, const MemoInfo& m, bool hot) {
    using namespace theme;
    auto S = [this](int v) { return Scale(v, dpi_); };
    c.Shadow(flag, S(2), S(hot ? 8 : 4), S(1), hot ? 80 : 50, RGB(20, 10, 4));
    RECT tip = place_.right ? RECT{flag.left, flag.top, flag.left + S(kFlagTip), flag.bottom}
                            : RECT{flag.right - S(kFlagTip), flag.top, flag.right, flag.bottom};
    RECT label = place_.right ? RECT{tip.right - S(2), flag.top, flag.right, flag.bottom}
                              : RECT{flag.left, flag.top, tip.left + S(2), flag.bottom};
    c.FillRoundRect(label, S(2), kFlagPaper);
    c.FillRoundRect(tip, S(2), FlagColorOf(m));
    RECT text{label.left + S(10), label.top, label.right - S(10), label.bottom};
    c.Text(m.title.empty() ? L"빈 메모" : m.title, text, m.title.empty() ? kMutedFg : kForeground, S(12), !m.title.empty(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (hot) c.StrokeRoundRect(flag, S(2), S(2), Ink());
}

// The trash can the dispenser's row turns into while a memo, messenger or clip is carried. What is dropped in
// is slowly crumpled into a ball above the open can, falls in, and the lid closes with a small bounce.
void Organizer::DrawTrash(Canvas& c, const RECT& area, double now) {
    using namespace theme;
    using PtF = Canvas::PtF;
    auto S = [this](int v) { return (float)Scale(v, dpi_); };
    constexpr double kPi = 3.14159265358979;
    double t = crumpleStart_ ? std::min(1.0, (now - crumpleStart_) / kCrumpleMs) : -1;  // 0..1 while crumpling
    if (drag_.overTrash) c.FillRoundRect(area, (int)S(8), RGB(255, 255, 255), 40);
    c.DashedRoundRect(area, (int)S(8), (int)S(1) + drag_.overTrash, (int)S(5), (int)S(4), Ink(), drag_.overTrash ? 230 : 150);

    float cx = area.left + S(34), bottom = area.bottom - S(7), mouth = bottom - S(19);
    float bounce = t >= kLandAt ? S(2) * (float)std::sin((t - kLandAt) / (1 - kLandAt) * kPi) : 0;
    if (t >= 0 && t < kLandAt) {  // the ball (drawn before the can, whose front hides it as it falls in)
        RECT from = crumpleFrom_;
        OffsetRect(&from, area.left, area.top);
        float fx = (from.left + from.right) / 2.f, fy = (from.top + from.bottom) / 2.f;
        float hw = (from.right - from.left) / 2.f, hh = (from.bottom - from.top) / 2.f;
        float ax = cx, ay = mouth - S(14), px, py, k;
        if (t < kBallAt) {
            float u = (float)(t / kBallAt);
            k = u * u * (3 - 2 * u);  // ease in-out
            // squeezed in fits and starts, like a hand crushing paper
            k = std::min(1.0f, k + 0.04f * (float)std::sin(u * 3 * kPi) * (1 - k));
            px = fx + (ax - fx) * k;
            py = fy + (ay - fy) * k;
        } else {
            float u = (float)((t - kBallAt) / (kLandAt - kBallAt));
            k = 1;
            px = ax;
            py = ay + (mouth + S(10) - ay) * u * u;  // falling
        }
        std::vector<PtF> outer, inner;
        for (int i = 0; i < 18; ++i) {
            double th = i * 2 * kPi / 18 + k * 0.7, co = std::cos(th), si = std::sin(th);
            double edge = std::min(hw / std::max(1e-3, std::fabs(co)), hh / std::max(1e-3, std::fabs(si)));  // the flag
            double ball = S(8) * (1 + 0.24 * std::sin(i * 2.4 + 1) + 0.12 * std::cos(i * 5.3));          // crumpled
            double rad = edge * (1 - k) + ball * k;
            outer.push_back({px + (float)(rad * co), py + (float)(rad * si)});
            inner.push_back({px + (float)(rad * co * 0.76), py + (float)(rad * si * 0.76) - k});
        }
        c.FillPolygons({outer}, Mix(kFlagPaper, RGB(190, 176, 146), 0.6 * k));
        c.FillPolygons({inner}, Mix(kFlagPaper, RGB(228, 216, 188), 0.5 * k));
    }
    // The can: a tapered bucket with three ribs.
    c.FillPolygons({{{cx - S(9), mouth + bounce}, {cx + S(9), mouth + bounce}, {cx + S(7), bottom + bounce}, {cx - S(7), bottom + bounce}}},
                   Ink(), 235);
    for (int i = -1; i <= 1; ++i)
        c.FillRoundRect({(int)(cx + i * S(4)), (int)(mouth + bounce + S(4)), (int)(cx + i * S(4) + S(1)), (int)(bottom + bounce - S(3))},
                        0, RGB(150, 110, 80), 170);
    // The lid, hinged at its right end: open while a memo hovers or goes in, then it shuts.
    double open = drag_.overTrash ? 1 : t < 0 ? 0 : t < kShutAt ? 1 : std::max(0.0, 1 - (t - kShutAt) / (kLandAt - kShutAt + 0.12));
    double a = -0.6 * open, ca = std::cos(a), sa = std::sin(a);
    float hx = cx + S(11), hy = mouth + bounce - S(2);
    auto lid = [&](std::initializer_list<std::pair<float, float>> pts) {
        std::vector<PtF> out;
        for (auto [x, y] : pts) out.push_back({hx + (float)(x * ca - y * sa), hy + (float)(x * sa + y * ca)});
        return out;
    };
    c.FillPolygons({lid({{-S(22), -S(3)}, {0.f, -S(3)}, {0.f, 0.f}, {-S(22), 0.f}}), lid({{-S(15), -S(5)}, {-S(7), -S(5)}, {-S(7), -S(3)}, {-S(15), -S(3)}})},
                   Ink(), 235);

    std::wstring label = t >= 0 ? L"휴지통에 버렸어요" : drag_.overTrash ? L"놓으면 휴지통으로" : L"여기에 놓아 버리기";
    c.Text(label, {(int)(cx + S(22)), area.top, area.right - (int)S(10), area.bottom}, Ink(), (int)S(12), true,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void Organizer::RenderTray(bool present) {
    if (!trayOpen_ && present) return;
    using namespace theme;
    auto S = [this](int v) { return Scale(v, dpi_); };
    std::vector<Pile*> overflow;
    int pileCount = (int)host_.OrganizerPiles().size();
    Geometry g = Measure(pileCount);
    std::vector<Pile*> by = BySlot(g.slots, &overflow);
    trayRect_ = g.screen;

    trayCanvas_ = std::make_unique<Canvas>(g.w, g.h);
    Canvas& c = *trayCanvas_;
    // The tray and the bookmark it was pulled out by are one piece: filled and shaded together (the sheen of
    // metal and acrylic, the transparency of glass run across both), so nothing marks where they meet.
    int r = S(12), hr = S(7);
    RECT body = place_.right ? RECT{g.bodyLeft, 0, g.w + r, g.h} : RECT{-r, 0, g.bodyRight, g.h};
    RECT tab = place_.right ? RECT{g.handle.left, g.handle.top, g.handle.right + hr, g.handle.bottom}
                            : RECT{g.handle.left - hr, g.handle.top, g.handle.right, g.handle.bottom};
    FillMaterialShape(c, {{body, r}, {tab, hr}}, body, Mat(), dpi_);
    // A light rim along the tray's outline (the part off the screen edge isn't drawn), mended where the
    // bookmark joins it: those pixels are filled again exactly as the piece was.
    RECT rim = body;
    InflateRect(&rim, -1, -1);
    c.StrokeRoundRect(rim, r - 1, std::max(1, S(1)), RGB(255, 255, 255), 50);
    // The bookmark gets the same rim on its outer sides; what of it (and of the tray's) runs where the two meet
    // is mended away, so one outline goes round the whole piece.
    RECT tabRim = tab;
    InflateRect(&tabRim, -1, -1);
    c.StrokeRoundRect(tabRim, hr - 1, std::max(1, S(1)), RGB(255, 255, 255), 50);
    int jx = place_.right ? g.bodyLeft : g.bodyRight;
    RECT joint = place_.right ? RECT{jx, g.handle.top, jx + hr + S(3), g.handle.bottom}
                              : RECT{jx - hr - S(3), g.handle.top, jx, g.handle.bottom};
    FillMaterialShape(c, {{joint, 0}}, body, Mat(), dpi_);
    DrawBookmark(c, g.handle, false, false);

    // The wordmark carved into the wood; while something is carried over it, what a drop would do.
    bool full = pileCount >= g.slots;
    if (dropMode_) {
        std::wstring label = dropAdd_ ? L"더미 카드 위에 놓으면 그 더미에 들어가요"
                             : full   ? L"자리가 없어요 · " + std::to_wstring(g.slots) + L"칸이 다 찼어요"
                                      : L"빈 칸에 놓으면 그 자리에 꽂혀요";
        c.Text(label, {g.logo.left, g.logo.top, g.logo.right - S(kPin + 6), g.logo.bottom}, Ink(), S(12), true,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
        int lh = S(15), ly = g.logo.top + (g.logo.bottom - g.logo.top - lh) / 2;
        COLORREF base = kMaterials[Mat()].base;
        // On light materials a cut is darker than the surface, its lower edge lit. On dark ones the mark is a
        // warm off-white (the labels' cream with a fifth of the material in it, so it sits in rather than
        // glares), its lower edge in shadow.
        bool dark = kMaterials[Mat()].dark;
        DrawWordmark(c, g.logo.left, ly + 1, lh, dark ? Mix(base, RGB(0, 0, 0), 0.5) : Mix(base, RGB(255, 255, 255), 0.35));
        DrawWordmark(c, g.logo.left, ly, lh, dark ? Mix(Ink(), base, 0.2) : Mix(base, RGB(0, 0, 0), 0.5));
    }
    // The pin, at the end of the logo row: pinned, the drawer stays out; unpinned, it folds away by itself.
    {
        int cy = (g.logo.top + g.logo.bottom) / 2;
        RECT pin{g.logo.right - S(kPin), cy - S(kPin) / 2, g.logo.right, cy + S(kPin) / 2};
        bool hot = hover_ == kItemPin;
        if (place_.pinned) c.FillRoundRect(pin, S(kPin) / 2, Ink(), hot ? 70 : 50);
        else if (hot) c.FillRoundRect(pin, S(kPin) / 2, Ink(), 28);
        COLORREF ink = place_.pinned || hot ? Ink() : Mix(Ink(), kMaterials[Mat()].base, 0.35);
        if (place_.pinned) c.Glyph(0xE841, pin, ink, S(14));  // PinnedFill: the head filled ...
        c.Glyph(0xE718, pin, ink, S(14));                       // ... and the outline over it, so the needle shows
    }
    if (dropMode_ && !dropAdd_ && !full) {
        RECT o{g.bodyLeft + S(4), S(4), g.bodyRight - S(4), g.h - S(4)};
        c.DashedRoundRect(o, S(10), S(2), S(6), S(4), Ink(), 200);
    }

    // Slots: every one drawn, filled or not. A pile sits in the one it was put in.
    int unit = S(kCardH + kCardGap);
    int draggedPile = drag_.item >= 0 && drag_.item < kItemMemo ? drag_.item : -1;
    cardIds_.clear();
    cardSlots_.clear();
    cardRects_.clear();
    for (int s = 0; s < g.slots; ++s)
        if (by[s]) {
            RECT sr = SlotRect(g, s);
            cardIds_.push_back(by[s]->id);
            cardSlots_.push_back(s);
            cardRects_.push_back({g.bodyLeft + S(kTrayPad), sr.top, g.bodyRight - S(kTrayPad), sr.bottom});
        }
    if (draggedPile >= (int)cardIds_.size()) draggedPile = -1;
    if (draggedPile >= 0)
        drag_.slot = std::clamp((drag_.y - drag_.grabDy - g.slotTop + unit / 2) / unit, 0, g.slots - 1);
    for (int s = 0; s < g.slots; ++s) {
        RECT sr = SlotRect(g, s);
        bool target = (draggedPile >= 0 && s == drag_.slot) || (dropMode_ && !dropAdd_ && s == dropSlot_);
        if (!by[s] || (draggedPile >= 0 && cardSlots_[draggedPile] == s)) {
            // An empty slot is cut into the wood: a recess a card would sit right in. Shade under its top
            // and left lips, light on the far edges.
            c.FillRoundRect(sr, S(2), RGB(0, 0, 0), 55);
            c.FillRoundRect({sr.left, sr.top, sr.right, sr.top + S(3)}, 0, RGB(0, 0, 0), 60);
            c.FillRoundRect({sr.left, sr.top, sr.left + S(2), sr.bottom}, 0, RGB(0, 0, 0), 40);
            c.FillRoundRect({sr.left, sr.bottom - S(1), sr.right, sr.bottom}, 0, RGB(255, 226, 196), 60);
            c.FillRoundRect({sr.right - S(1), sr.top, sr.right, sr.bottom}, 0, RGB(255, 226, 196), 35);
            if (target) c.FillRoundRect(sr, S(2), RGB(255, 255, 255), 45);
            if (pileCount == 0 && s == 0)
                c.Text(L"바탕화면의 더미 카드를 이 막대로 끌어 와 빈 칸에 놓으면 꽂혀요.", {sr.left + S(12), sr.top, sr.right - S(12), sr.bottom},
                       Mix(Ink(), kMaterials[Mat()].base, 0.2), S(12), false, DT_LEFT | DT_VCENTER | DT_WORDBREAK);
        }
    }
    for (size_t i = 0; i < cardIds_.size(); ++i) {
        if ((int)i == draggedPile) continue;
        RECT sr = SlotRect(g, cardSlots_[i]);
        bool hot = (int)i == hover_ && drag_.item < 0;
        // The pile being worked on sits a little out of its slot toward the desktop (still in the tray), so it
        // reads as the one taken out; hovered, any card comes out the whole way.
        bool open = by[cardSlots_[i]]->active;
        if (hot) OffsetRect(&sr, g.slide, 0);
        else if (open) OffsetRect(&sr, g.slide * 4 / 5, 0);
        DrawPileCard(c, sr, *by[cardSlots_[i]], hot || open);
        if (open) {  // the pile being looked at: dimmed, an eye in the middle
            c.FillRoundRect(sr, S(1), RGB(24, 16, 10), 120);
            int cx = (sr.left + sr.right) / 2, cy = (sr.top + sr.bottom) / 2, d = S(34);
            RECT eye{cx - d / 2, cy - d / 2, cx + d / 2, cy + d / 2};
            c.FillRoundRect(eye, d / 2, RGB(255, 255, 255), 235);
            c.Glyph(0xE890, eye, RGB(40, 32, 26), S(16));  // View
        }
    }
    int y = g.slotTop + g.slots * unit - S(kCardGap);
    if (g.more)
        c.Text(L"이 화면에 다 못 놓은 더미 " + std::to_wstring(g.more) + L"개", {g.cardX, y, g.cardX + S(kCardW), y + S(kMoreH)},
               Ink(), S(12), false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Memos: Post-it flags, in the order they were arranged; the carried one leaves a gap where it would go.
    int draggedMemo = drag_.item >= kItemMemo && drag_.item - kItemMemo < g.flagCount ? drag_.item - kItemMemo : -1;
    int flagUnit = S(kFlagH + kFlagGap);
    if (draggedMemo >= 0) drag_.slot = std::clamp((drag_.y - drag_.grabDy - g.flagTop + flagUnit / 2) / flagUnit, 0, g.flagCount - 1);
    memoRects_.clear();
    c.Text(memos_.empty() ? L"메모" : L"메모 · " + std::to_wstring(memos_.size()) + L"개",
           {g.cardX, g.memoLabelY, g.cardX + S(kCardW), g.memoLabelY + S(kMemoLabelH)}, Ink(), S(12), true,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    for (int i = 0; i < g.flagCount; ++i) {
        int k = i;
        if (draggedMemo >= 0) {
            if (i == draggedMemo) k = drag_.slot;
            else {
                k = i - (i > draggedMemo);
                k += k >= drag_.slot;
            }
        }
        RECT f = FlagRect(g, k);
        memoRects_.push_back({g.bodyLeft + S(kTrayPad), f.top, g.bodyRight - S(kTrayPad), f.bottom});
        if (i == draggedMemo) continue;
        bool hot = hover_ == kItemMemo + i && drag_.item < 0;
        if (hot) OffsetRect(&f, g.slide / 2, 0);
        DrawFlag(c, f, memos_[i], hot);
    }
    memoMoreRect_ = memos_.empty() ? RECT{} : LabelLink(c, g.memoLabelY, L"전체 보기", hover_ == kItemMemoMore);
    // The dispenser, as wide as a card; the trash can while a memo, messenger or clip is carried or going in.
    plusRect_ = {g.bodyLeft + S(kTrayPad), g.plus.top, g.bodyRight - S(kTrayPad), g.plus.bottom};
    bool carried = draggedMemo >= 0 || IsMessenger(drag_.item) || IsClip(drag_.item);
    if (carried || crumpleStart_) DrawTrash(c, g.plus, NowMs());
    DrawClips(c, g);  // a carried clip or messenger is drawn on top of the trash
    DrawDock(c, g);
    if (!carried && !crumpleStart_) DrawDispenser(c, g, hover_ == kItemPlus);  // grown, it lies over the sections below

    // The carried card or flag, on top of everything, following the pointer.
    if (draggedPile >= 0) {
        int ty = std::clamp(drag_.y - drag_.grabDy, S(4), g.h - S(kCardH) - S(4));
        RECT card{g.cardX + g.slide, ty, g.cardX + g.slide + S(kCardW), ty + S(kCardH)};
        drag_.card = card;
        DrawPileCard(c, card, *by[cardSlots_[draggedPile]], true);
    } else if (draggedMemo >= 0) {
        RECT f = FlagRect(g, 0);
        int ty = std::clamp(drag_.y - drag_.grabDy, S(4), g.h - S(kFlagH) - S(4));
        OffsetRect(&f, g.slide / 2, ty - f.top);
        if (drag_.overTrash) InflateRect(&f, -S(12), -S(2));  // about to go in: it shrinks a little
        drag_.card = f;
        DrawFlag(c, f, memos_[draggedMemo], true);
    }
    if (present) PresentTray();
}

// A messenger's or clip's tile carried by the pointer, on top of everything (drawn by DrawDock / DrawClips).
static RECT CarriedTile(const RECT& tray, int x, int y, int size) {
    int tx = std::clamp(x, (int)tray.left + 2, (int)tray.right - size - 2), ty = std::clamp(y, (int)tray.top + 2, (int)tray.bottom - size - 2);
    return {tx, ty, tx + size, ty + size};
}

// A row of tile places carved into the tray, like the card slots.
static void DrawTileRecess(Canvas& c, const RECT& t, int r, int lip, BYTE depth) {
    c.FillRoundRect(t, r, RGB(0, 0, 0), depth);
    c.FillRoundRect({t.left + r / 2, t.top, t.right - r / 2, t.top + lip}, 0, RGB(0, 0, 0), 50);
    c.FillRoundRect({t.left + r / 2, t.bottom - 1, t.right - r / 2, t.bottom}, 0, RGB(255, 226, 196), 45);
}

// The memo dispenser: a box at the screen-edge end of its row, its mouth toward the desktop, with the next
// sheet's tongue sticking out. A click shoots that sheet out: it flies off, turning into a memo window as it
// goes, and the memo opens where it lands; meanwhile the next sheet comes up out of the slot.
double Organizer::NextSheet(double now) const {
    if (!launchAt_) return 1;
    double u = std::min(1.0, (now - launchAt_) / kNextMs) - 1;
    return 1 + 2.7 * u * u * u + 1.7 * u * u;  // ease out, overshooting a little
}

void Organizer::DrawDispenser(Canvas& c, const Geometry& g, bool hot) {
    using namespace theme;
    auto S = [this](int v) { return Scale(v, dpi_); };
    const RECT& r = g.plus;
    int dir = place_.right ? -1 : 1;  // toward the desktop
    double now = NowMs();
    // Hovered, the dispenser grows to about four times its size over the sections below (dimmed a little).
    // k: 0 resting .. 1 grown.
    double k = !hot ? 0 : std::min(1.0, (now - upAt_) / kGrowMs);
    k = 1 - (1 - k) * (1 - k) * (1 - k);  // ease out
    auto L = [&](int a, int b) { return a + (int)((b - a) * k); };
    if (k > 0) {
        RECT below{g.bodyLeft + S(2), r.top - S(8), g.bodyRight - S(2), g.h - S(2)};
        c.FillRoundRect(below, S(12), RGB(16, 10, 6), (BYTE)(80 * k));
    }
    int side = L(S(28), S(100));
    int top = L(r.top + (r.bottom - r.top - S(28)) / 2, r.top - S(6));
    RECT box = place_.right ? RECT{r.right - S(6) - side, top, r.right - S(6), top + side}
                            : RECT{r.left + S(6), top, r.left + S(6) + side, top + side};
    int mouth = place_.right ? box.left + side / 7 : box.right - side / 7;
    int mid = top + side / 2, sh = side * 13 / 20;  // shorter than the slot, which shows above and below it
    int out = (int)(L(S(10), S(38)) * NextSheet(now));
    // Grown, the whole box can be clicked (and keeps it grown) until the pointer leaves it.
    // At rest, only the box and its sheet's tongue are hovered to grow it.
    restRect_ = place_.right ? RECT{r.right - S(6) - S(28) - S(8), r.top, r.right - S(4), r.bottom}
                             : RECT{r.left + S(4), r.top, r.left + S(6) + S(28) + S(8), r.bottom};
    growRect_ = {};
    if (hot) {
        // Just the grown box and its sheet's tongue (the rest of the row is left alone).
        growRect_ = place_.right ? RECT{r.right - S(106) - S(26), r.top - S(6), r.right - S(6), r.top + S(94)}
                                 : RECT{r.left + S(6), r.top - S(6), r.left + S(106) + S(26), r.top + S(94)};
    }
    // The box: dark plastic, its mouth (a slot) toward the desktop, the stack of sheets showing through its face.
    COLORREF sheet = kFlagColors[2].color;  // a yellow Post-it
    int br = std::max(S(4), side / 8);
    c.Shadow(box, br, L(S(4), S(14)), L(S(1), S(5)), (BYTE)L(80, 130), RGB(20, 10, 4));
    c.FillRoundRectV(box, br, RGB(98, 94, 90), RGB(52, 49, 46));
    int slot = std::max(S(2), side / 22);
    c.FillRoundRect({mouth - slot / 2 - 1, box.top + side / 8, mouth + slot / 2 + 1, box.bottom - side / 8}, slot, RGB(18, 16, 14));
    RECT stack = place_.right ? RECT{mouth + side / 7, box.top + side / 5, box.right - side / 7, box.bottom - side / 5}
                              : RECT{box.left + side / 7, box.top + side / 5, mouth - side / 7, box.bottom - side / 5};
    c.FillRoundRect(stack, std::max(1, side / 30), Mix(sheet, RGB(0, 0, 0), 0.22));
    for (int i = 1; i <= 3 && side > S(48); ++i) {  // the sheets' edges, one behind another
        int x = place_.right ? stack.left + i * side / 20 : stack.right - i * side / 20;
        c.FillRoundRect({x, stack.top, x + 1, stack.bottom}, 0, Mix(sheet, RGB(0, 0, 0), 0.45), 160);
    }
    c.FillRoundRect({box.left + br / 2, box.top + 1, box.right - br / 2, box.top + 2}, 0, RGB(255, 255, 255), 60);
    if (k > 0.7) {  // grown: "PUSH" pressed into the window over the stack, as moulded on the product
        COLORREF face = Mix(sheet, RGB(0, 0, 0), 0.22);
        int size = std::max(S(8), side / 9);
        RECT at = stack;
        OffsetRect(&at, 0, 1);
        c.Text(L"P U S H", at, Mix(face, RGB(255, 255, 255), 0.3), size, false, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        c.Text(L"P U S H", stack, Mix(face, RGB(0, 0, 0), 0.5), size, false, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    // The sheet coming out of the slot, its free end lifting a little.
    float y0 = (float)mid - sh / 2.f, y1 = (float)mid + sh / 2.f, lift = std::min<float>(out * 0.06f, sh * 0.06f);
    float m = (float)mouth, e = (float)(mouth + dir * out);
    if (out > 0) {
        RECT shade{std::min<int>((int)m, (int)e), (int)y0 + L(S(2), S(4)), std::max<int>((int)m, (int)e), (int)y1 + L(S(2), S(4))};
        c.Shadow(shade, S(2), L(S(3), S(8)), L(S(1), S(3)), (BYTE)L(60, 100), RGB(20, 10, 4));
        c.FillPolygons({{{m, y0}, {e, y0 - lift}, {e, y1}, {m, y1}}}, sheet);
        float strip = (float)dir * std::min<float>((float)L(S(6), S(12)), (float)out);
        c.FillPolygons({{{m, y0}, {m + strip, y0}, {m + strip, y1}, {m, y1}}}, RGB(0, 0, 0), 40);
    }
    // The whole sheet as it would be out of the slot: where a shot one starts its flight.
    sheetRect_ = place_.right ? RECT{mouth - sh, (int)y0, mouth, (int)y1} : RECT{mouth, (int)y0, mouth + sh, (int)y1};
}

void Organizer::Launch() {
    std::wstring path = NewMemo();
    if (path.empty()) return;
    // Where it opens: 60% across the screen (40% for a tray on the left), 45% down, a little lower for each memo
    // already open or on its way.
    int flying = 0;
    for (const Flight& f : flights_) flying += f.start != 0;
    int ww = work_.right - work_.left, wh = work_.bottom - work_.top;
    POINT at{place_.right ? work_.left + ww * 60 / 100 : work_.right - ww * 60 / 100,
             work_.top + wh * 45 / 100 + (OpenMemoCount() + flying) % 8 * Scale(24, dpi_)};
    SIZE z = MemoWindowSize(DpiForPoint(at));
    Flight* f = nullptr;
    for (Flight& k : flights_)
        if (!k.start) f = &k;
    if (!f) {  // a window for it to fly in, kept for the next ones
        flights_.push_back({});
        f = &flights_.back();
        f->hwnd = MakeWindow(kFlyClass, &Organizer::PeekProc, this, L"Neverflow 새 메모");
        SetWindowLongPtrW(f->hwnd, GWL_EXSTYLE, GetWindowLongPtrW(f->hwnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
    }
    f->to = {at.x - z.cx / 2, at.y - z.cy / 2, at.x - z.cx / 2 + z.cx, at.y - z.cy / 2 + z.cy};
    f->from = sheetRect_;
    OffsetRect(&f->from, trayRect_.left, trayRect_.top);
    f->path = path;
    f->start = launchAt_ = NowMs();
    SetTimer(tray_, kTimerFly, 10, nullptr);
    SetTimer(tray_, kTimerSheet, 15, nullptr);
    RenderFlight(*f, 0);
    RenderTray();
}

// The shot sheet on its way: along an arc that rises a little, growing from the sheet to the memo window's size,
// turning straight as it goes, its yellow giving way to the memo's paper and title band just before it lands.
void Organizer::RenderFlight(Flight& f, double t) {
    using namespace theme;
    auto S = [this](int v) { return Scale(v, dpi_); };
    double e = 1 - (1 - t) * (1 - t) * (1 - t);  // ease out
    const RECT &from = f.from, &to = f.to;
    double ax = (from.left + from.right) / 2.0, ay = (from.top + from.bottom) / 2.0;
    double bx = (to.left + to.right) / 2.0, by = (to.top + to.bottom) / 2.0;
    double cx = (ax + bx) / 2, cy = std::min(ay, by) - std::hypot(bx - ax, by - ay) * 0.16;
    double x = (1 - e) * (1 - e) * ax + 2 * (1 - e) * e * cx + e * e * bx;
    double y = (1 - e) * (1 - e) * ay + 2 * (1 - e) * e * cy + e * e * by;
    double w = (from.right - from.left) + ((to.right - to.left) - (from.right - from.left)) * e;
    double h = (from.bottom - from.top) + ((to.bottom - to.top) - (from.bottom - from.top)) * e;
    double ang = (place_.right ? -1 : 1) * 0.26 * (1 - e) * (1 - e);  // tipped as it leaves, level as it lands
    double paper = std::clamp((e - 0.55) / 0.4, 0.0, 1.0);             // 0 yellow .. 1 the memo's own paper
    int pad = S(28);
    double co = std::cos(ang), si = std::sin(ang);
    int bw = (int)(std::fabs(w * co) + std::fabs(h * si)) + pad * 2, bh = (int)(std::fabs(w * si) + std::fabs(h * co)) + pad * 2;
    Canvas c(bw, bh);
    double ox = bw / 2.0, oy = bh / 2.0;
    // A rounded rectangle (w x h about the middle) turned by `ang`, as points: the part from `top` to `bottom`.
    auto shape = [&](double top, double bottom, double rad) {
        std::vector<Canvas::PtF> pts;
        double l = -w / 2, rr = w / 2;
        struct Corner {
            double cx, cy, from;
        } corners[] = {{rr - rad, bottom - rad, 0}, {l + rad, bottom - rad, 1.5708}, {l + rad, top + rad, 3.1416}, {rr - rad, top + rad, 4.7124}};
        for (const Corner& k : corners)
            for (int i = 0; i <= 6; ++i) {
                double a = k.from + 1.5708 * i / 6;
                double px = k.cx + rad * std::cos(a), py = k.cy + rad * std::sin(a);
                pts.push_back({(float)(ox + px * co - py * si), (float)(oy + px * si + py * co)});
            }
        return pts;
    };
    double rad = S(2) + (S(8) - S(2)) * e;
    // Its shadow, softening and spreading as it rises off the tray.
    RECT sr{(int)(ox - w / 2), (int)(oy - h / 2), (int)(ox + w / 2), (int)(oy + h / 2)};
    c.Shadow(sr, (int)rad, S(6) + (int)(S(10) * e), S(4) + (int)(S(4) * std::sin(t * 3.1416)), (BYTE)(70 + 40 * e), RGB(20, 10, 4));
    COLORREF yellow = kFlagColors[2].color, memoPaper = RGB(255, 252, 240);
    c.FillPolygons({shape(-h / 2, h / 2, rad)}, Mix(yellow, memoPaper, paper));
    if (paper > 0) {  // the memo window's title band appearing
        double band = h * 36 / 380;
        c.FillPolygons({shape(-h / 2, -h / 2 + band + rad, rad)}, kCream, (BYTE)(255 * paper));
        c.FillPolygons({shape(-h / 2 + band, -h / 2 + band + rad + 1, 0)}, Mix(yellow, memoPaper, paper));
    }
    c.Present(f.hwnd, {(int)(x - ox), (int)(y - oy)});
    SetWindowPos(f.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void Organizer::DrawNotice(Canvas& c, const Geometry& g, int row) {
    if (noticeRow_ != row || notice_.empty()) return;
    auto S = [this](int v) { return Scale(v, dpi_); };
    int top = row == 0 ? g.dockTop : g.clipTop;
    RECT o{g.cardX - S(4), top - S(4), g.cardX + S(kCardW) + S(4), top + S(kTile) + S(4)};
    c.FillRoundRect(o, S(12), RGB(30, 26, 22), 225);
    c.Text(notice_, o, kCream, S(12), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void Organizer::ShowNotice(int row, const std::wstring& text) {
    noticeRow_ = row;
    notice_ = text;
    SetTimer(tray_, kTimerNotice, kNoticeMs, nullptr);
    RenderTray();
}

// The clipboard shelf: the newest five things copied; text on a white card, images filling their tile
// (transparent ones without a background).
void Organizer::DrawClips(Canvas& c, const Geometry& g) {
    using namespace theme;
    auto S = [this](int v) { return Scale(v, dpi_); };
    int n = std::min<int>((int)clips_.size(), kClipMax);
    std::wstring label = L"클립보드";
    clipMoreRect_ = n ? LabelLink(c, g.clipLabelY, L"기록 보기", hover_ == kItemClipMore) : RECT{};
    c.Text(label, {g.cardX, g.clipLabelY, g.cardX + S(kCardW), g.clipLabelY + S(kMemoLabelH)}, Ink(), S(12), true,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    int size = S(kTile), gap = (S(kCardW) - kClipMax * size) / (kClipMax - 1);
    int carried = IsClip(drag_.item) && drag_.item - kItemClip < n ? drag_.item - kItemClip : -1;
    clipRects_.clear();
    for (int i = 0; i < kClipMax; ++i) {
        int x = g.cardX + i * (size + gap);
        RECT t{x, g.clipTop, x + size, g.clipTop + size};
        if (i < n) clipRects_.push_back(t);
        DrawTileRecess(c, t, S(8), S(2), 45);
    }
    auto draw = [&](int i, RECT t, bool hot) {
        const ClipItem& k = clips_[i];
        if (hot) OffsetRect(&t, 0, -S(2));
        if (i < (int)clipThumbs_.size() && !k.image.empty() && clipThumbs_[i].w != t.right - t.left)
            clipThumbs_[i] = ClipThumb(k, t.right - t.left, t.bottom - t.top);  // the dpi changed
        if (!k.text.empty()) {
            c.Shadow(t, S(8), S(hot ? 8 : 5), S(2), hot ? 80 : 55, RGB(20, 10, 4));
            c.FillRoundRect(t, S(8), RGB(255, 255, 255));
            std::wstring snip = k.text.substr(0, 60);
            for (wchar_t& ch : snip)
                if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
            c.Text(snip, {t.left + S(4), t.top + S(3), t.right - S(3), t.bottom - S(3)}, kForeground, S(9), false,
                   DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else if (i < (int)clipThumbs_.size()) {
            if (k.alpha) c.BlendImage(clipThumbs_[i], t.left, t.top);
            else {
                c.Shadow(t, S(8), S(hot ? 8 : 5), S(2), hot ? 80 : 55, RGB(20, 10, 4));
                c.DrawImage(clipThumbs_[i], t, S(8));
            }
        }
        if (hot) c.StrokeRoundRect(t, S(8), S(2), Ink());
    };
    for (int i = 0; i < n; ++i)
        if (i != carried) draw(i, clipRects_[i], hover_ == kItemClip + i && drag_.item < 0);
    DrawNotice(c, g, 1);
    if (carried >= 0) {
        RECT t = CarriedTile({0, 0, c.width(), c.height()}, drag_.x - drag_.grabDx, drag_.y - drag_.grabDy, size);
        if (drag_.overTrash) InflateRect(&t, -S(6), -S(6));
        drag_.card = t;
        draw(carried, t, false);
    }
}

// "전체 보기 ›" and the like, at the right end of a section's label row; returns where it is (for clicks).
RECT Organizer::LabelLink(Canvas& c, int labelY, const std::wstring& text, bool hot) {
    using namespace theme;
    auto S = [this](int v) { return Scale(v, dpi_); };
    Geometry g = Measure((int)host_.OrganizerPiles().size());
    std::wstring t = text + L" ›";
    int w = c.MeasureText(t, S(12), true).cx + S(10);
    RECT r{g.cardX + S(kCardW) - w, labelY, g.cardX + S(kCardW), labelY + S(kMemoLabelH)};
    if (hot) c.FillRoundRect(r, S(6), Ink(), 30);
    c.Text(t, r, hot ? Ink() : Mix(Ink(), kMaterials[Mat()].base, 0.3), S(12), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return r;
}

// Everything copied lately (kClipHistory), in the same list window as the memos; a row copies it again.
void Organizer::ShowClipHistory(POINT anchor) {
    std::weak_ptr<bool> alive = alive_;
    auto self = this;
    ListPopupSpec spec;
    spec.windowClass = L"WindowPilesClipList";
    spec.title = L"클립보드 기록";
    spec.button = L"모두 지우기";
    spec.empty = L"복사한 글과 이미지가 여기에 모여요. 앱을 끄면 함께 지워져요.";
    spec.rows = [alive, self] {
        std::vector<ListRow> rows;
        if (alive.expired() || !*alive.lock()) return rows;
        int t = Scale(46, self->dpi_);
        for (const ClipItem& k : self->clips_) {
            ListRow r;
            wchar_t time[32];
            swprintf_s(time, L"%s %d:%02d", k.at.wHour < 12 ? L"오전" : L"오후", (k.at.wHour + 11) % 12 + 1, k.at.wMinute);
            r.meta = time;
            if (!k.text.empty()) {
                size_t nl = k.text.find_first_of(L"\r\n");
                r.title = k.text.substr(0, nl);
                size_t rest = k.text.find_first_not_of(L"\r\n", nl == std::wstring::npos ? k.text.size() : nl);
                r.excerpt = rest == std::wstring::npos ? std::to_wstring(k.text.size()) + L"자" : k.text.substr(rest, 200);
                for (wchar_t& ch : r.excerpt)
                    if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
            } else {
                r.title = k.alpha ? L"투명 이미지" : L"이미지";
                r.excerpt = std::to_wstring(k.image.w) + L" × " + std::to_wstring(k.image.h);
                r.thumb = ClipThumb(k, t, t);
                r.thumbAlpha = k.alpha;
            }
            rows.push_back(std::move(r));
        }
        return rows;
    };
    spec.open = [alive, self](int row, HWND popup) {
        if (alive.expired() || !*alive.lock() || row >= (int)self->clips_.size()) return;
        CopyToClipboard(self->handle_, self->clips_[row]);  // comes back as the newest: moves to the front
        DestroyWindow(popup);
    };
    spec.press = [alive, self](HWND popup) {
        if (alive.expired() || !*alive.lock()) return;
        self->clips_.clear();
        self->clipThumbs_.clear();
        self->RenderTray();
        RefreshListPopup(popup);
    };
    ShowListPopup(spec, anchor);
}

void Organizer::AddClip(ClipItem item) {
    if (item.empty() || IsClip(drag_.item)) return;  // ponytail: a copy made while a clip is carried is missed
    for (size_t i = 0; i < clips_.size(); ++i)
        if (clips_[i].Same(item)) {
            clips_.erase(clips_.begin() + i);
            clipThumbs_.erase(clipThumbs_.begin() + i);
            break;
        }
    int size = Scale(kTile, dpi_);
    GetLocalTime(&item.at);
    clipThumbs_.insert(clipThumbs_.begin(), ClipThumb(item, size, size));
    clips_.insert(clips_.begin(), std::move(item));
    if ((int)clips_.size() > kClipHistory) {
        clips_.resize(kClipHistory);
        clipThumbs_.resize(kClipHistory);
    }
    // Off the shelf into the history only: a big image is kept smaller (at most 1024 px on a side).
    if ((int)clips_.size() > kClipMax) {
        ClipItem& old = clips_[kClipMax];
        int side = std::max(old.image.w, old.image.h);
        if (side > 1024) old.image = ClipThumb(old, old.image.w * 1024 / side, old.image.h * 1024 / side);
    }
    if (drag_.item < 0) RenderTray();
}

// The messenger dock: chat and mail apps kept at hand whatever the pile, each with what it says is unread.
void Organizer::DrawDock(Canvas& c, const Geometry& g) {
    using namespace theme;
    auto S = [this](int v) { return Scale(v, dpi_); };
    int n = (int)messengers_.size();
    bool dropping = dropMode_ && hover_ == kItemDock;  // an app window or shortcut carried over the dock
    std::wstring label = L"메신저";
    if (IsMessenger(hover_) && hover_ - kItemMessenger < n && drag_.item < 0) label += L" · " + messengers_[hover_ - kItemMessenger].name;
    else if (dropping) label += n >= kMessengerMax ? L" · 자리가 다 찼어요" : L" · 놓으면 여기에 둬요";
    else if (!n) label += L" · 앱 창이나 바로가기를 끌어다 놓기";
    c.Text(label, {g.cardX, g.dockLabelY, g.cardX + S(kCardW), g.dockLabelY + S(kMemoLabelH)}, Ink(), S(12), true,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    dockRect_ = {g.bodyLeft + S(kTrayPad), g.dockLabelY, g.bodyRight - S(kTrayPad), g.dockTop + S(kTile) + S(6)};
    int size = S(kTile), gap = (S(kCardW) - kMessengerMax * size) / (kMessengerMax - 1), pitch = size + gap;
    // The carried one leaves a gap where it would go; the others close up around it.
    int carried = IsMessenger(drag_.item) && drag_.item - kItemMessenger < n ? drag_.item - kItemMessenger : -1;
    if (carried >= 0) drag_.slot = std::clamp((drag_.x - drag_.grabDx - g.cardX + pitch / 2) / pitch, 0, n - 1);
    messengerRects_.clear();
    for (int i = 0; i < kMessengerMax; ++i) {
        int x = g.cardX + i * pitch;
        RECT t{x, g.dockTop, x + size, g.dockTop + size};
        if (i < n) messengerRects_.push_back(t);
        DrawTileRecess(c, t, S(10), S(2), i >= n && dropping ? 25 : 45);  // every place is carved in; a tile sits in it
    }
    auto tile = [&](int i, RECT t, bool hot) {
        const Messenger& m = messengers_[i];
        if (hot) OffsetRect(&t, 0, -S(2));
        c.Shadow(t, S(10), S(hot ? 8 : 5), S(2), hot ? 80 : 55, RGB(20, 10, 4));
        c.FillRoundRect(t, S(10), kFlagPaper);
        int is = S(30);
        if (HICON icon = MessengerIcon(m)) c.DrawIcon(icon, t.left + (S(kTile) - is) / 2, t.top + (S(kTile) - is) / 2, is);
        else c.Text(m.name.substr(0, 1), t, kForeground, S(16), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        // Unread: the count its window titles show, or a dot when they only say "something's new".
        UnreadInfo u = MessengerUnread(m);
        if (u.count > 0 || u.marker) {
            std::wstring num = u.count > 99 ? L"99+" : u.count ? std::to_wstring(u.count) : L"";
            int bh = num.empty() ? S(10) : S(18);
            int bw = num.empty() ? S(10) : std::max<int>(S(18), c.MeasureText(num, S(10), true).cx + S(10));
            RECT b{t.right - bw + S(5), t.top - S(5), t.right + S(5), t.top - S(5) + bh};
            c.FillRoundRect(b, bh / 2, RGB(232, 64, 58));
            if (!num.empty()) c.Text(num, b, RGB(255, 255, 255), S(10), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        if (hot) c.StrokeRoundRect(t, S(10), S(2), Ink());
    };
    for (int i = 0; i < n; ++i) {
        if (i == carried) continue;
        int k = i;
        if (carried >= 0) {
            k = i - (i > carried);
            k += k >= drag_.slot;
        }
        tile(i, {g.cardX + k * pitch, g.dockTop, g.cardX + k * pitch + size, g.dockTop + size}, hover_ == kItemMessenger + i && drag_.item < 0);
    }
    DrawNotice(c, g, 0);
    if (carried >= 0) {
        RECT t = CarriedTile({0, 0, c.width(), c.height()}, drag_.x - drag_.grabDx, drag_.y - drag_.grabDy, size);
        if (drag_.overTrash) InflateRect(&t, -S(6), -S(6));
        drag_.card = t;
        tile(carried, t, false);
    }
}

bool Organizer::AddMessenger(const Messenger& m) {
    for (const Messenger& k : messengers_)
        if (_wcsicmp(k.exe.c_str(), m.exe.c_str()) == 0) return false;
    if ((int)messengers_.size() >= kMessengerMax) {
        ShowNotice(0, L"메신저는 최대 " + std::to_wstring(kMessengerMax) + L"개까지 넣을 수 있어요");
        return false;
    }
    messengers_.push_back(m);
    SaveMessengers(messengers_);
    return true;
}

bool Organizer::DropWindowOnDock(HWND hwnd, POINT pt) {
    if (!trayOpen_) return false;
    ScreenToClient(tray_, &pt);
    int item = ItemAt(pt);
    if (item != kItemDock && !IsMessenger(item)) return false;
    Messenger m;
    if (MessengerFromWindow(hwnd, m)) AddMessenger(m);
    RenderTray();
    return true;
}

void Organizer::PresentTray() {
    if (trayCanvas_) trayCanvas_->Present(tray_, {trayRect_.left, trayRect_.top});
}

void Organizer::Prepare() {
    if (trayOpen_ || prepared_) return;
    memos_ = ListMemos();
    hover_ = -1;
    RenderTray(false);
    prepared_ = true;
}

// The drawer comes out from where its bookmark is: it starts with only the bookmark showing, exactly over
// the one on the screen edge, and eases out. The picture is sent once; each frame only moves the window,
// timed by the compositor (DwmFlush), so no frame is late or dropped.
void Organizer::SlideOut() {
    int w = trayRect_.right - trayRect_.left, start = w - Scale(kHandleW, dpi_);
    auto at = [&](int off) { return POINT{trayRect_.left + (place_.right ? off : -off), trayRect_.top}; };
    POINT p = at(start);
    trayCanvas_->Present(tray_, p);
    SetWindowPos(tray_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(handle_, SW_HIDE);
    double t0 = NowMs();
    for (double t = 0; t < 1;) {
        if (FAILED(DwmFlush())) Sleep(8);
        t = std::min(1.0, (NowMs() - t0) / kSlideMs);
        p = at((int)std::lround((1 - (1 - std::pow(1 - t, 3))) * start));  // ease-out
        SetWindowPos(tray_, nullptr, p.x, p.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    }
}

void Organizer::SlideIn() {
    HidePeek();
    int w = trayRect_.right - trayRect_.left, end = w - Scale(kHandleW, dpi_);
    double t0 = NowMs();
    for (double t = 0; t < 1;) {
        if (FAILED(DwmFlush())) Sleep(8);
        t = std::min(1.0, (NowMs() - t0) / kSlideInMs);
        int off = (int)std::lround(t * t * t * end);  // ease-in: it gathers speed as it goes back in
        SetWindowPos(tray_, nullptr, trayRect_.left + (place_.right ? off : -off), trayRect_.top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    }
}

void Organizer::Refresh() {
    prepared_ = false;  // what it holds changed: draw it again when it opens
    RenderHandle();
    RenderTray();
    RenderFlags();
}

void Organizer::ShowTray() {
    KillTimer(handle_, kTimerOpen);
    KillTimer(flags_, kTimerOpen);
    if (trayOpen_) return;
    trayOpen_ = true;
    seenInside_ = false;
    outsideSince_ = 0;
    if (!prepared_) {  // usually done already, while the pointer rested on the bookmark
        trayOpen_ = false;
        Prepare();
        trayOpen_ = true;
    }
    prepared_ = false;
    hover_ = -1;
    // The bookmark and the flag tips are on the tray now, pulled out with it.
    ShowWindow(flags_, SW_HIDE);
    if (place_.slide && trayCanvas_) {
        SlideOut();
    } else {
        PresentTray();
        SetWindowPos(tray_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ShowWindow(handle_, SW_HIDE);
    }
    SetTimer(tray_, kTimerWatch, 100, nullptr);
}

void Organizer::HideTray(bool force) {
    if (!trayOpen_) return;
    if (place_.pinned && !force) {  // pinned: it stays out; only what was going on in it ends
        hover_ = -1;
        pressed_ = -1;
        drag_ = {};
        HidePeek();
        if (GetCapture() == tray_) ReleaseCapture();
        memos_ = ListMemos();
        RenderTray();
        return;
    }
    trayOpen_ = false;
    KillTimer(tray_, kTimerWatch);
    // It folds back into its bookmark the way it came out; put away on purpose (숨기기, moving it), at once.
    if (place_.slide && !force && trayCanvas_ && IsWindowVisible(tray_)) SlideIn();
    ShowWindow(tray_, SW_HIDE);
    hover_ = -1;
    pressed_ = -1;
    drag_ = {};
    crumpleStart_ = 0;
    KillTimer(tray_, kTimerAnim);
    HidePeek();
    if (GetCapture() == tray_) ReleaseCapture();
    if (visible_) {
        handleHover_ = false;
        RenderHandle();
        ShowWindow(handle_, SW_SHOWNOACTIVATE);
    }
    memos_ = ListMemos();  // a memo may have been added, renamed or thrown away meanwhile
    RenderFlags();
}

Pile* Organizer::PileAtScreen(POINT pt) {
    if (!trayOpen_) return nullptr;
    ScreenToClient(tray_, &pt);
    return PileAt(ItemAt(pt));
}

void Organizer::Present() {
    if (visible_) ShowTray();
}

void Organizer::OpenForAdd() {
    if (!visible_) return;
    stickyDrop_ = true;
    dropMode_ = true;
    dropAdd_ = true;
    prepared_ = false;  // drawn for a drop, not as prepared on hover
    ShowTray();
    seenInside_ = true;
    RenderTray();
}

void Organizer::DragOver(POINT pt, bool add) {
    // Near the bookmark (within a card's width of the edge, around its height) the tray opens to catch it.
    int reach = Scale(kCardW, dpi_);
    bool nearEdge = place_.right ? pt.x >= monitor_.right - reach && pt.x <= monitor_.right
                                 : pt.x <= monitor_.left + reach && pt.x >= monitor_.left;
    bool nearHandle = nearEdge && pt.y >= handleRect_.top - reach && pt.y <= handleRect_.bottom + reach;
    if (stickyDrop_ || nearHandle || (trayOpen_ && IsDropTarget(pt))) {
        bool changed = !dropMode_ || dropAdd_ != add;
        dropAdd_ = add;
        if (!dropMode_) {
            dropMode_ = true;
            prepared_ = false;  // drawn for a drop, not as prepared on hover
            ShowTray();
            seenInside_ = true;
        }
        if (add) {  // the card it would go into lights up
            POINT c = pt;
            ScreenToClient(tray_, &c);
            int item = ItemAt(c);
            item = item >= 0 && item < kItemMemo ? item : item == kItemDock || IsMessenger(item) ? kItemDock : -1;
            changed |= item != hover_;
            hover_ = item;
        } else {  // the empty slot it would land in lights up
            int slot = FreeSlot(&pt);
            Geometry g = Measure((int)host_.OrganizerPiles().size());
            RECT sr = slot < g.slots ? SlotRect(g, slot) : RECT{};
            POINT c{pt.x - trayRect_.left, pt.y - trayRect_.top};
            InflateRect(&sr, 0, Scale(kCardGap / 2, dpi_));
            if (!PtInRect(&sr, c)) slot = -1;
            changed |= slot != dropSlot_;
            dropSlot_ = slot;
        }
        if (changed) RenderTray();
    } else if (dropMode_) {
        DragEnd();
    }
}

void Organizer::DragEnd() {
    if (!dropMode_) return;
    dropMode_ = false;
    dropAdd_ = false;
    stickyDrop_ = false;
    dropSlot_ = -1;
    HideTray();
}

bool Organizer::IsDropTarget(POINT pt) const {
    RECT h = handleRect_;
    InflateRect(&h, Scale(24, dpi_), Scale(24, dpi_));
    if (PtInRect(&h, pt)) return true;
    return trayOpen_ && PtInRect(&trayRect_, pt);
}

int Organizer::ItemAt(POINT client) const {
    Geometry g = Measure((int)host_.OrganizerPiles().size());
    int cy = (g.logo.top + g.logo.bottom) / 2;
    RECT pin{g.logo.right - Scale(kPin, dpi_), cy - Scale(kPin, dpi_) / 2, g.logo.right, cy + Scale(kPin, dpi_) / 2};
    if (PtInRect(&pin, client)) return kItemPin;
    if (PtInRect(&g.handle, client)) return kItemHandle;
    if (hover_ == kItemPlus && drag_.item < 0 && PtInRect(&growRect_, client)) return kItemPlus;  // the grown dispenser
    for (size_t i = 0; i < cardRects_.size(); ++i)
        if (PtInRect(&cardRects_[i], client)) return (int)i;
    for (size_t i = 0; i < memoRects_.size(); ++i)
        if (PtInRect(&memoRects_[i], client)) return kItemMemo + (int)i;
    for (size_t i = 0; i < messengerRects_.size(); ++i)
        if (PtInRect(&messengerRects_[i], client)) return kItemMessenger + (int)i;
    for (size_t i = 0; i < clipRects_.size(); ++i)
        if (PtInRect(&clipRects_[i], client)) return kItemClip + (int)i;
    if (PtInRect(&dockRect_, client)) return kItemDock;
    if (PtInRect(&restRect_, client)) return kItemPlus;  // the dispenser itself, not the rest of its row
    if (PtInRect(&memoMoreRect_, client)) return kItemMemoMore;
    if (PtInRect(&clipMoreRect_, client)) return kItemClipMore;
    return -1;
}

Pile* Organizer::PileAt(int item) {
    if (item < 0 || item >= (int)cardIds_.size()) return nullptr;
    for (Pile* p : host_.OrganizerPiles())
        if (p->id == cardIds_[item]) return p;
    return nullptr;
}

void Organizer::Activate(int item, POINT screenPt) {
    // A memo opens beside the tray, on the desktop side.
    POINT anchor{place_.right ? trayRect_.left : trayRect_.right, screenPt.y};
    if (item == kItemPin) {
        place_.pinned = !place_.pinned;
        host_.OnOrganizerMoved();  // remembered with its place
        RenderTray();
    } else if (item == kItemHandle) {
        if (place_.pinned) {  // pushed back in by its bookmark: that unpins it
            place_.pinned = false;
            host_.OnOrganizerMoved();
        }
        HideTray(true);
    } else if (IsMessenger(item) && item - kItemMessenger < (int)messengers_.size()) {
        Messenger m = messengers_[item - kItemMessenger];
        HideTray();
        ToggleMessenger(m);  // open, or closed if it's the window in front
    } else if (IsClip(item) && item - kItemClip < (int)clips_.size()) {
        CopyToClipboard(handle_, clips_[item - kItemClip]);  // comes back as the newest: moves to the front
        ShowNotice(1, L"복사했어요 · Ctrl+V로 붙여넣기");
    } else if (item == kItemPlus) {
        Launch();
    } else if (item >= kItemMemo && item - kItemMemo < (int)memos_.size()) {
        std::wstring path = memos_[item - kItemMemo].path;
        HideTray();
        OpenMemoWindow(path, anchor);
    } else if (item == kItemMemoMore) {
        HideTray();
        ShowMemoList(anchor);
    } else if (item == kItemClipMore) {
        HideTray();
        ShowClipHistory(anchor);
    } else if (Pile* pile = PileAt(item)) {
        HideTray();
        host_.OnOrganizerOpen(*pile);  // may change the tray: nothing may touch it afterwards
    }
}

void Organizer::MemoMenu(int memo, POINT screenPt) {
    if (memo < 0 || memo >= (int)memos_.size()) return;
    MemoInfo m = memos_[memo];
    POINT anchor{place_.right ? trayRect_.left : trayRect_.right, screenPt.y};
    enum { kOpen = 1, kReveal, kRecycle, kColor = 100 };
    std::vector<MenuItem> items = {{kOpen, L"열기"}, {kReveal, L"파일 위치 열기"}, {0, L""}};
    for (int i = 0; i < kFlagColorCount; ++i)
        items.push_back({(UINT)(kColor + i), std::wstring(L"플래그 · ") + kFlagColors[i].name, i == m.color ? L"✓" : L""});
    items.push_back({0, L""});
    items.push_back({kRecycle, L"휴지통으로 버리기", L"되살릴 수 있어요"});
    UINT id = ShowMenu(items, screenPt);
    if (id == kOpen) OpenMemoWindow(m.path, anchor);
    else if (id == kReveal) RevealInExplorer(m.path);
    else if (id == kRecycle) RecycleMemo(m.path);
    else if (id >= kColor && id < kColor + kFlagColorCount) {
        SetMemoColor(m.path, id - kColor);
        RefreshMemoTabs();
    }
    memos_ = ListMemos();
    RenderFlags();
}

LRESULT CALLBACK Organizer::HandleProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (Organizer*)GetWindowLongPtrW(h, GWLP_USERDATA);
    return self ? self->OnHandle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK Organizer::TrayProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (Organizer*)GetWindowLongPtrW(h, GWLP_USERDATA);
    return self ? self->OnTray(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK Organizer::PeekProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    return msg == WM_MOUSEACTIVATE ? MA_NOACTIVATE : DefWindowProcW(h, msg, wp, lp);
}

void Organizer::ShowPeek(int memo) {
    if (memo == peekMemo_ || memo < 0 || memo >= (int)memos_.size() || drag_.item >= 0 || dropMode_) return;
    using namespace theme;
    auto S = [this](int v) { return Scale(v, dpi_); };
    peekMemo_ = memo;
    // The memo as it reads in its window: headings bigger, list markers as bullets, then body text.
    const MemoInfo& m = memos_[memo];
    std::wstring text = ReadMemo(m.path);
    struct Line {
        std::wstring s;
        int size;
        bool bold;
    };
    std::vector<Line> lines;
    int h = S(10) + S(12), maxH = S(kPeekMaxH);
    bool clipped = false;
    for (size_t pos = 0; pos <= text.size();) {
        size_t end = std::min(text.find(L'\n', pos), text.size());
        std::wstring s = text.substr(pos, end - pos);
        if (!s.empty() && s.back() == L'\r') s.pop_back();
        pos = end + 1;
        if (s.empty() && lines.empty()) continue;
        MemoLineInfo info = ClassifyMemoLine(s);
        int size = info.kind == MemoLine::H1 ? 17 : info.kind == MemoLine::H2 ? 15 : info.kind == MemoLine::H3 ? 13 : 12;
        bool bold = info.kind == MemoLine::H1 || info.kind == MemoLine::H2 || info.kind == MemoLine::H3;
        if (info.kind == MemoLine::Bullet) s = L"•  " + s.substr(info.markerLen);
        else if (bold) s = s.substr(info.markerLen);
        int lh = S(size) + S(bold ? 10 : 7);
        if (h + lh > maxH - S(24)) {
            clipped = true;
            break;
        }
        lines.push_back({s, size, bold});
        h += lh;
        if (end >= text.size()) break;
    }
    if (lines.empty()) lines.push_back({L"빈 메모", 12, false});
    h = std::max(h, S(60)) + (clipped ? S(20) : 0) + S(12);
    int w = S(kPeekW);
    peekCanvas_ = std::make_unique<Canvas>(w, h);
    Canvas& c = *peekCanvas_;
    c.FillRoundRect({0, 0, w, h}, S(3), kFlagPaper);
    c.FillRoundRect({0, 0, w, S(6)}, 0, FlagColorOf(m));  // its flag's color along the top
    c.StrokeRoundRect({0, 0, w, h}, S(3), 1, RGB(214, 204, 186));
    int y = S(6) + S(12);
    for (const Line& l : lines) {
        int lh = S(l.size) + S(l.bold ? 10 : 7);
        c.Text(l.s, {S(16), y, w - S(16), y + lh}, l.bold || &l == &lines.front() ? kForeground : kSubtleFg, S(l.size),
               l.bold, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += lh;
    }
    if (clipped) c.Text(L"…  눌러서 열기", {S(16), y, w - S(16), y + S(18)}, kMutedFg, S(11), false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    // It comes out from under the tray's edge, level with its flag, toward the desktop.
    Geometry g = Measure((int)host_.OrganizerPiles().size());
    RECT f = FlagRect(g, memo);
    int edge = trayRect_.left + (place_.right ? g.bodyLeft : g.bodyRight);
    int top = std::clamp<int>(trayRect_.top + f.top - S(8), work_.top + S(8), std::max<int>(work_.top + S(8), work_.bottom - S(8) - h));
    peekRect_ = place_.right ? RECT{edge - w, top, edge, top + h} : RECT{edge, top, edge + w, top + h};
    peekStart_ = NowMs();
    SetTimer(tray_, kTimerPeek, 10, nullptr);
    RenderPeek();
}

void Organizer::RenderPeek() {
    if (peekMemo_ < 0 || !peekCanvas_) return;
    double t = std::min(1.0, (NowMs() - peekStart_) / kPeekMs), e = 1 - std::pow(1 - t, 3);
    int w = peekRect_.right - peekRect_.left, off = (int)std::lround((1 - e) * w);
    POINT at{peekRect_.left + (place_.right ? off : -off), peekRect_.top};
    peekCanvas_->Present(peek_, at);
    // Just under the tray, so its hidden part is still under the tray's edge while it unfolds.
    SetWindowPos(peek_, tray_, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void Organizer::HidePeek() {
    if (peekMemo_ < 0) return;
    peekMemo_ = -1;
    KillTimer(tray_, kTimerPeek);
    ShowWindow(peek_, SW_HIDE);
}

LRESULT CALLBACK Organizer::FlagsProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (Organizer*)GetWindowLongPtrW(h, GWLP_USERDATA);
    return self ? self->OnFlags(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

// The flag tips at the screen edge: a click opens that memo; resting on them pulls the tray out.
LRESULT Organizer::OnFlags(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_MOUSEMOVE: {
            if (flagsHover_) return 0;  // counted from when the pointer came onto them, not from its last move
            flagsHover_ = true;
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, flags_, 0};
            TrackMouseEvent(&tme);
            Prepare();
            SetTimer(flags_, kTimerOpen, kOpenDelayMs, nullptr);
            return 0;
        }
        case WM_MOUSELEAVE:
            flagsHover_ = false;
            KillTimer(flags_, kTimerOpen);
            return 0;
        case WM_TIMER:
            KillTimer(flags_, kTimerOpen);
            flagsHover_ = false;
            if (wp == kTimerOpen && GetAsyncKeyState(VK_LBUTTON) >= 0) ShowTray();
            return 0;
        case WM_LBUTTONUP: {
            KillTimer(flags_, kTimerOpen);
            int y = GET_Y_LPARAM(lp);
            for (size_t i = 0; i < flagTipY_.size() && i < memos_.size(); ++i)
                if (y >= flagTipY_[i] && y < flagTipY_[i] + Scale(kFlagH, dpi_)) {
                    POINT c;
                    GetCursorPos(&c);
                    OpenMemoWindow(memos_[i].path, {place_.right ? c.x - Scale(16, dpi_) : c.x + Scale(16, dpi_), c.y});
                    break;
                }
            return 0;
        }
    }
    return DefWindowProcW(flags_, msg, wp, lp);
}

LRESULT Organizer::OnHandle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_CLIPBOARDUPDATE: {  // something was copied: onto the clipboard shelf
            ClipItem item;
            if (ReadClipboard(handle_, item)) AddClip(std::move(item));
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!handleHover_) {
                handleHover_ = true;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, handle_, 0};
                TrackMouseEvent(&tme);
                RenderHandle();
                // A short rest opens the tray (drawn meanwhile, so it opens at once); grabbing the bookmark right
                // away moves it instead.
                Prepare();
                SetTimer(handle_, kTimerOpen, kOpenDelayMs, nullptr);
            }
            if (handlePressed_) {
                POINT c;
                GetCursorPos(&c);
                if (!handleDragging_ && (abs(c.y - pressAt_.y) > GetSystemMetrics(SM_CYDRAG) ||
                                         abs(c.x - pressAt_.x) > GetSystemMetrics(SM_CXDRAG))) {
                    handleDragging_ = true;
                    HideTray(true);
                    KillTimer(handle_, kTimerOpen);
                    ShowWindow(flags_, SW_HIDE);
                }
                if (handleDragging_) {
                    int hh = handleRect_.bottom - handleRect_.top;
                    SetWindowPos(handle_, nullptr, c.x - (handleRect_.right - handleRect_.left) / 2, c.y - hh / 2, 0, 0,
                                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            handleHover_ = false;
            KillTimer(handle_, kTimerOpen);
            RenderHandle();
            return 0;
        case WM_TIMER:
            if (wp == kTimerFiles) {
                bool down = GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON) & 0x8000;
                POINT c;
                GetCursorPos(&c);
                if (filesDrag_) {
                    if (down) DragOver(c, true);
                    else {  // let go: a drop on the tray came as WM_DROPFILES; anywhere else, nothing to do
                        filesDrag_ = false;
                        DragEnd();
                    }
                    return 0;
                }
                // Not our own drags (a card or the bookmark holds the capture), and only after a short rest.
                RECT h = handleRect_;
                InflateRect(&h, Scale(8, dpi_), Scale(8, dpi_));
                if (!down || !visible_ || GetCapture() || handlePressed_ || trayOpen_ || !PtInRect(&h, c)) {
                    fileHoverSince_ = 0;
                    return 0;
                }
                if (!fileHoverSince_) fileHoverSince_ = NowMs();
                else if (NowMs() - fileHoverSince_ >= 250) {
                    filesDrag_ = true;
                    DragOver(c, true);
                }
                return 0;
            }
            if (wp == kTimerEdge) {
                // Folded: the pointer resting against the screen edge anywhere along the tray's height opens it,
                // as resting on the bookmark does (not while a button is held, nor over a full-screen app).
                POINT c;
                GetCursorPos(&c);
                bool atEdge = visible_ && !trayOpen_ && !handleDragging_ && !GetCapture() &&
                              (place_.right ? c.x == monitor_.right - 1 : c.x == monitor_.left) && c.y >= monitor_.top &&
                              c.y < monitor_.bottom && GetAsyncKeyState(VK_LBUTTON) >= 0 && GetAsyncKeyState(VK_RBUTTON) >= 0;
                if (atEdge) {
                    Geometry g = Measure((int)host_.OrganizerPiles().size());
                    atEdge = c.y >= g.screen.top && c.y < g.screen.bottom;
                }
                if (atEdge) {
                    HWND fg = GetForegroundWindow();
                    RECT fr{};
                    wchar_t cls[32] = {};
                    if (fg) GetClassNameW(fg, cls, 32);
                    bool desktop = fg == GetShellWindow() || wcscmp(cls, L"WorkerW") == 0 || wcscmp(cls, L"Progman") == 0;
                    if (fg && !desktop && GetWindowRect(fg, &fr) && EqualRect(&fr, &monitor_)) atEdge = false;  // full screen
                }
                if (!atEdge) {
                    edgeSince_ = 0;
                } else if (!edgeSince_) {
                    edgeSince_ = NowMs();
                    Prepare();
                } else if (NowMs() - edgeSince_ >= kOpenDelayMs) {
                    edgeSince_ = 0;
                    ShowTray();
                }
                return 0;
            }
            if (wp == kTimerOpen) {
                KillTimer(handle_, kTimerOpen);
                if (!handlePressed_) ShowTray();
            }
            return 0;
        case WM_LBUTTONDOWN:
            handlePressed_ = true;
            handleDragging_ = false;
            GetCursorPos(&pressAt_);
            SetCapture(handle_);
            return 0;
        case WM_LBUTTONUP: {
            if (!handlePressed_) return 0;
            handlePressed_ = false;
            ReleaseCapture();
            if (!handleDragging_) {
                ShowTray();  // a click opens it too (touch, or a quick click before the hover delay)
                return 0;
            }
            handleDragging_ = false;
            // Dropped: the edge nearest to the pointer, on the monitor under it, at that height.
            POINT c;
            GetCursorPos(&c);
            HMONITOR mon = MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST);
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            GetMonitorInfoW(mon, &mi);
            place_.monitor = mi.szDevice;
            place_.right = c.x >= (mi.rcMonitor.left + mi.rcMonitor.right) / 2;
            int wh = std::max<int>(1, mi.rcWork.bottom - mi.rcWork.top);
            place_.yPermille = std::clamp<int>(MulDiv(c.y - mi.rcWork.top, 1000, wh), 0, 1000);
            RenderHandle();
            RenderFlags();
            host_.OnOrganizerMoved();
            return 0;
        }
        case WM_RBUTTONUP: {
            POINT c;
            GetCursorPos(&c);
            host_.OnOrganizerMenu(nullptr, c);
            return 0;
        }
    }
    return DefWindowProcW(handle_, msg, wp, lp);
}

void Organizer::EndDrag(POINT) {
    Drag d = drag_;
    drag_ = {};
    if (GetCapture() == tray_) ReleaseCapture();
    if (d.item < kItemMemo) {
        // Into the slot it was let go over: an empty one, or swapping with the pile that was there.
        std::vector<Pile*> overflow;
        std::vector<Pile*> by = BySlot(Capacity(), &overflow);
        if (d.item < (int)cardSlots_.size() && d.slot >= 0 && d.slot < (int)by.size() && d.slot != cardSlots_[d.item]) {
            int from = cardSlots_[d.item];
            std::swap(by[from], by[d.slot]);
            std::vector<int> ids;
            for (Pile* p : by) ids.push_back(p ? p->id : -1);
            for (Pile* p : overflow) ids.push_back(p->id);
            host_.OnOrganizerReorder(ids);  // redraws the tray
            return;
        }
    } else if (IsMessenger(d.item) || IsClip(d.item)) {
        bool clip = IsClip(d.item);
        int i = d.item - (clip ? kItemClip : kItemMessenger);
        bool there = i < (int)(clip ? clips_.size() : messengers_.size());
        if (there && d.overTrash) {  // out of the dock or off the shelf (the app itself stays as it is)
            if (clip) {
                clips_.erase(clips_.begin() + i);
                clipThumbs_.erase(clipThumbs_.begin() + i);
            } else {
                messengers_.erase(messengers_.begin() + i);
                SaveMessengers(messengers_);
            }
            Geometry g = Measure((int)host_.OrganizerPiles().size());
            crumpleFrom_ = d.card;
            OffsetRect(&crumpleFrom_, -g.plus.left, -g.plus.top);
            crumpleStart_ = NowMs();
            SetTimer(tray_, kTimerAnim, 15, nullptr);
        } else if (there && !clip && d.slot >= 0 && d.slot != i) {
            Messenger m = messengers_[i];
            messengers_.erase(messengers_.begin() + i);
            messengers_.insert(messengers_.begin() + std::min<int>(d.slot, (int)messengers_.size()), m);
            SaveMessengers(messengers_);
        }
    } else if (int i = d.item - kItemMemo; i < (int)memos_.size()) {
        if (d.overTrash) {
            RecycleMemo(memos_[i].path);  // in the Recycle Bin right away; what follows is the animation
            memos_.erase(memos_.begin() + i);
            Geometry g = Measure((int)host_.OrganizerPiles().size());
            crumpleFrom_ = d.card;
            OffsetRect(&crumpleFrom_, -g.plus.left, -g.plus.top);
            crumpleStart_ = NowMs();
            SetTimer(tray_, kTimerAnim, 15, nullptr);
        } else if (d.slot >= 0 && d.slot != i) {
            MemoInfo m = memos_[i];
            memos_.erase(memos_.begin() + i);
            memos_.insert(memos_.begin() + std::min<int>(d.slot, (int)memos_.size()), m);
            SaveMemoOrder(memos_);
        }
    }
    RenderTray();
}

LRESULT Organizer::OnTray(UINT msg, WPARAM wp, LPARAM lp) {
    auto S = [this](int v) { return Scale(v, dpi_); };
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_TIMER: {
            if (wp == kTimerAnim) {
                if (NowMs() - crumpleStart_ >= kCrumpleMs) {
                    crumpleStart_ = 0;
                    KillTimer(tray_, kTimerAnim);
                }
                RenderTray();
                return 0;
            }
            if (wp == kTimerPeek) {
                if (NowMs() - peekStart_ >= kPeekMs) KillTimer(tray_, kTimerPeek);
                RenderPeek();
                return 0;
            }
            if (wp == kTimerSheet) {
                if (NowMs() - launchAt_ >= kNextMs && NowMs() - upAt_ >= kGrowMs) {
                    launchAt_ = 0;
                    KillTimer(tray_, kTimerSheet);
                }
                RenderTray();
                return 0;
            }
            if (wp == kTimerFly) {
                bool flying = false;
                for (size_t i = 0; i < flights_.size(); ++i) {
                    Flight& f = flights_[i];
                    if (!f.start) continue;
                    double t = (NowMs() - f.start) / kFlyMs;
                    if (t < 1) {
                        RenderFlight(f, t);
                        flying = true;
                        continue;
                    }
                    // There: the memo's own window takes the flying sheet's place (the tray stays as it is, for
                    // the next one).
                    f.start = 0;
                    RECT at = f.to;
                    HWND sheet = f.hwnd;
                    OpenMemoWindow(f.path, {(at.left + at.right) / 2, (at.top + at.bottom) / 2}, &at);
                    ShowWindow(sheet, SW_HIDE);
                }
                if (!flying) KillTimer(tray_, kTimerFly);
                return 0;
            }
            if (wp == kTimerNotice) {
                KillTimer(tray_, kTimerNotice);
                noticeRow_ = -1;
                notice_.clear();
                RenderTray();
                return 0;
            }
            if (wp == kTimerWatch && !messengers_.empty() && ++watchTicks_ % 10 == 0 && drag_.item < 0) {
                // About once a second: the messengers' unread badges, redrawn only when one changed.
                std::wstring now;
                for (const Messenger& m : messengers_) {
                    UnreadInfo u = MessengerUnread(m);
                    now += std::to_wstring(u.count) + (u.marker ? L"*," : L",");
                }
                if (now != badges_) {
                    badges_ = now;
                    RenderTray();
                }
            }
            if (wp != kTimerWatch || place_.pinned || dropMode_ || menuOpen_ || drag_.item >= 0 || crumpleStart_) return 0;
            // Stays open while the pointer is over the bookmark or the tray; goes away shortly after it leaves.
            POINT c;
            GetCursorPos(&c);
            RECT t = trayRect_, h = handleRect_;
            InflateRect(&t, S(6), S(6));
            bool inside = PtInRect(&t, c) || PtInRect(&h, c);
            if (inside) {
                seenInside_ = true;
                outsideSince_ = 0;
            } else if (seenInside_) {
                if (!outsideSince_) outsideSince_ = NowMs();
                else if (NowMs() - outsideSince_ > kHideAfterMs) HideTray();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (drag_.item < 0 && hover_ != -1) {
                hover_ = -1;
                HidePeek();
                RenderTray();
            }
            return 0;
        case WM_MOUSEMOVE: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, tray_, 0};
            TrackMouseEvent(&tme);
            if (drag_.item >= 0) {
                drag_.x = p.x;
                drag_.y = p.y;
                bool memo = drag_.item >= kItemMemo && drag_.item < kItemPlus;
                bool tile = IsMessenger(drag_.item) || IsClip(drag_.item);
                RECT cr;
                GetClientRect(tray_, &cr);
                // A clip carried out of the tray: from here on it's an ordinary drag and drop into the app under
                // the pointer (the system's, on its own thread), and the tray is out of the way.
                RECT keep = cr;
                InflateRect(&keep, S(8), S(8));
                if (IsClip(drag_.item) && !PtInRect(&keep, p)) {
                    int i = drag_.item - kItemClip;
                    drag_ = {};
                    if (GetCapture() == tray_) ReleaseCapture();
                    if (i < (int)clips_.size()) {
                        ClipItem item = clips_[i];
                        HideTray();
                        DragClipOut(item);
                    }
                    RenderTray();  // pinned, it's still out
                    return 0;
                }
                // Off the tray toward the desktop: a pile card goes back there, a memo becomes its window.
                if (!tile && (place_.right ? p.x < -S(24) : p.x > cr.right + S(24))) {
                    POINT c = p;
                    ClientToScreen(tray_, &c);
                    int item = drag_.item;
                    Pile* pile = memo ? nullptr : PileAt(item);
                    std::wstring path = memo && item - kItemMemo < (int)memos_.size() ? memos_[item - kItemMemo].path : L"";
                    HideTray();
                    if (pile) host_.OnOrganizerCarryOut(*pile, c);  // may change the tray: nothing may touch it after
                    if (!path.empty()) TearOffMemo(path, c, dpi_);
                    return 0;
                }
                if (memo || tile) {
                    RECT t = plusRect_;
                    InflateRect(&t, 0, S(6));
                    drag_.overTrash = PtInRect(&t, p) != FALSE;
                }
                RenderTray();
                return 0;
            }
            bool movable = pressed_ >= 0 && (pressed_ < kItemPlus || IsMessenger(pressed_) || IsClip(pressed_));
            if (movable && (abs(p.x - pressPt_.x) > GetSystemMetrics(SM_CXDRAG) || abs(p.y - pressPt_.y) > GetSystemMetrics(SM_CYDRAG))) {
                auto at = [](const std::vector<RECT>& v, int i) { return i >= 0 && i < (int)v.size() ? &v[i] : nullptr; };
                const RECT* from = pressed_ < kItemMemo   ? at(cardRects_, pressed_)
                                   : pressed_ < kItemPlus ? at(memoRects_, pressed_ - kItemMemo)
                                   : IsClip(pressed_)     ? at(clipRects_, pressed_ - kItemClip)
                                                          : at(messengerRects_, pressed_ - kItemMessenger);
                if (from) {
                    HidePeek();
                    drag_ = {};
                    drag_.item = pressed_;
                    drag_.grabDx = pressPt_.x - from->left;
                    drag_.grabDy = pressPt_.y - from->top;
                    drag_.x = p.x;
                    drag_.y = p.y;
                    hover_ = -1;
                }
                pressed_ = -1;
                RenderTray();
                return 0;
            }
            int hc = ItemAt(p);
            if (hc != hover_) {
                hover_ = hc;
                if (hc == kItemPlus) {  // the dispenser grows, drawn frame by frame
                    upAt_ = NowMs();
                    SetTimer(tray_, kTimerSheet, 15, nullptr);
                }
                RenderTray();
                if (hc >= kItemMemo && hc < kItemPlus) ShowPeek(hc - kItemMemo);
                else HidePeek();
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (crumpleStart_) return 0;
            pressPt_ = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            pressed_ = ItemAt(pressPt_);
            SetCapture(tray_);
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (drag_.item >= 0) {
                EndDrag(p);
                return 0;
            }
            int item = pressed_;
            pressed_ = -1;
            if (GetCapture() == tray_) ReleaseCapture();
            if (item < 0 || item != ItemAt(p)) return 0;
            POINT c = p;
            ClientToScreen(tray_, &c);
            Activate(item, c);  // may change the tray: nothing may touch it afterwards
            return 0;
        }
        case WM_DROPFILES: {
            HDROP drop = (HDROP)wp;
            POINT p{};
            DragQueryPoint(drop, &p);
            std::vector<std::wstring> paths = DroppedPaths(drop);  // releases the drop
            int item = ItemAt(p);
            if (item == kItemDock || IsMessenger(item)) {  // a program or its shortcut, for the messenger dock
                for (const std::wstring& path : paths) {
                    Messenger m;
                    if (MessengerFromFile(path, m)) AddMessenger(m);
                }
                filesDrag_ = false;
                DragEnd();
                RenderTray();
                return 0;
            }
            Pile* pile = PileAt(item);
            filesDrag_ = false;
            DragEnd();
            if (pile && !paths.empty()) host_.OnOrganizerFilesDropped(*pile, paths);
            return 0;
        }
        case WM_RBUTTONUP: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int item = ItemAt(p);
            POINT c;
            GetCursorPos(&c);
            // The drawer stays out while its menu is up (and after, while the pointer is still on it).
            HidePeek();
            menuOpen_ = true;
            auto alive = alive_;
            if (IsMessenger(item) && item - kItemMessenger < (int)messengers_.size()) {
                int i = item - kItemMessenger;
                UINT id = ShowMenu({{1, L"열기"}, {0, L""}, {2, L"메신저에서 빼기", L"앱은 그대로"}}, c);
                if (id == 1) OpenMessenger(messengers_[i]);
                else if (id == 2) {
                    messengers_.erase(messengers_.begin() + i);
                    SaveMessengers(messengers_);
                }
            } else if (IsClip(item) && item - kItemClip < (int)clips_.size()) {
                int i = item - kItemClip;
                UINT id = ShowMenu({{1, L"다시 복사"}, {0, L""}, {2, L"클립보드 선반에서 빼기"}}, c);
                if (id == 1) CopyToClipboard(handle_, clips_[i]);
                else if (id == 2) {
                    clips_.erase(clips_.begin() + i);
                    clipThumbs_.erase(clipThumbs_.begin() + i);
                }
            } else if (item >= kItemMemo && item < kItemPlus) {
                MemoMenu(item - kItemMemo, c);
            } else {
                host_.OnOrganizerMenu(PileAt(item), c);  // may rebuild or remove the organizer
            }
            if (!*alive) return 0;
            menuOpen_ = false;
            outsideSince_ = 0;
            Refresh();
            return 0;
        }
    }
    return DefWindowProcW(tray_, msg, wp, lp);
}

}  // namespace wp
