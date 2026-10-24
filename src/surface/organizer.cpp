#include "organizer.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "common/canvas.h"
#include "common/win_util.h"
#include "ui/material.h"
#include "ui/theme.h"

namespace wp {

namespace {

// Layout in DIPs.
constexpr int kHandleW = 14, kHandleHoverW = 18, kHandleH = 128;
constexpr int kTrayPad = 12, kTrayLabelH = 24, kCardW = 260, kCardHeader = 26, kCardStrip = 40, kCardGap = 8;
constexpr int kCardH = kCardHeader + kCardStrip;
constexpr int kLift = 10;            // a hovered card slides this far out of the tray
constexpr double kHideAfterMs = 350;  // the pointer left the bookmark and tray this long ago: put it away
constexpr UINT_PTR kTimerWatch = 1, kTimerOpen = 2;
constexpr wchar_t kHandleClass[] = L"WindowPilesOrganizerHandle";
constexpr wchar_t kTrayClass[] = L"WindowPilesOrganizerTray";
const COLORREF kCream = RGB(246, 238, 228);

int WalnutIndex() { return MaterialFromId("walnut"); }

HWND MakeWindow(const wchar_t* cls, WNDPROC proc, void* self) {
    static bool registered[2] = {};
    bool isHandle = cls == kHandleClass;
    if (!registered[isHandle]) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
        wc.lpszClassName = cls;
        RegisterClassW(&wc);
        registered[isHandle] = true;
    }
    HWND h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls,
                             isHandle ? L"Neverflow 오거나이저" : L"Neverflow 오거나이저 트레이", WS_POPUP, 0, 0, 1, 1,
                             nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
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

}  // namespace

Organizer::Organizer(OrganizerHost& host, const Place& place) : host_(host), place_(place) {
    handle_ = MakeWindow(kHandleClass, &Organizer::HandleProc, this);
    tray_ = MakeWindow(kTrayClass, &Organizer::TrayProc, this);
    Layout();
    RenderHandle();
    ShowWindow(handle_, SW_SHOWNOACTIVATE);
}

Organizer::~Organizer() {
    for (HWND h : {handle_, tray_})
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
    int x = place_.right ? monitor_.right - w : monitor_.left;
    handleRect_ = {x, cy - h / 2, x + w, cy + h / 2};
}

RECT Organizer::HandleRect() const { return handleRect_; }

void Organizer::RenderHandle() {
    if (handleDragging_) return;  // follows the pointer; drawn once it lands
    Layout();
    int w = handleRect_.right - handleRect_.left, h = handleRect_.bottom - handleRect_.top;
    auto S = [this](int v) { return Scale(v, dpi_); };
    Canvas c(w, h);
    // A bookmark sticking out of the screen edge: rounded on the inside, flush with the edge.
    int r = S(7);
    RECT body = place_.right ? RECT{0, 0, w + r, h} : RECT{-r, 0, w, h};
    FillMaterial(c, body, r, WalnutIndex(), dpi_);
    if (handleHover_ || dropMode_) c.FillRoundRect(body, r, RGB(255, 255, 255), 30);
    // Grip ridges, and how many piles it holds.
    int gx = place_.right ? S(5) : w - S(5) - S(5);
    for (int i = -1; i <= 1; ++i)
        c.FillRoundRect({gx, h / 2 + i * S(6) - S(1), gx + S(5), h / 2 + i * S(6) + S(1)}, S(1), kCream, 200);
    size_t n = host_.OrganizerPiles().size();
    if (n) {
        std::wstring t = std::to_wstring(n);
        c.Text(t, {0, S(8), w, S(8) + S(14)}, kCream, S(10), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    c.Present(handle_, {handleRect_.left, handleRect_.top});
}

void Organizer::RenderTray() {
    if (!trayOpen_) return;
    using namespace theme;
    auto S = [this](int v) { return Scale(v, dpi_); };
    std::vector<Pile*> piles = host_.OrganizerPiles();
    int w = S(kTrayPad * 2 + kCardW + kLift);
    int maxH = (work_.bottom - work_.top) - S(32);
    int fixed = S(kTrayPad * 2 + kTrayLabelH);
    int fit = std::max(0, (maxH - fixed + S(kCardGap)) / S(kCardH + kCardGap));
    int shown = std::min<int>((int)piles.size(), fit);
    bool more = (int)piles.size() > shown;
    int contentH = shown ? shown * S(kCardH + kCardGap) - S(kCardGap) : S(64);
    int h = fixed + contentH + (more ? S(22) : 0);
    int cy = (handleRect_.top + handleRect_.bottom) / 2;
    int top = std::clamp<int>(cy - h / 2, work_.top + S(16), std::max<int>(work_.top + S(16), work_.bottom - S(16) - h));
    int left = place_.right ? monitor_.right - w : monitor_.left;
    trayRect_ = {left, top, left + w, top + h};

    Canvas c(w, h);
    int r = S(12);
    RECT body = place_.right ? RECT{0, 0, w + r, h} : RECT{-r, 0, w, h};
    FillMaterial(c, body, r, WalnutIndex(), dpi_);
    // A light rim along the tray's outline (the part off the screen edge isn't drawn).
    RECT rim = body;
    InflateRect(&rim, -1, -1);
    c.StrokeRoundRect(rim, r - 1, std::max(1, S(1)), RGB(255, 255, 255), 50);
    std::wstring label = dropMode_ ? L"여기에 놓으면 꽂혀요" : L"오거나이저 · " + std::to_wstring(piles.size()) + L"개";
    c.Text(label, {S(kTrayPad), S(kTrayPad), w - S(kTrayPad), S(kTrayPad + kTrayLabelH)}, kCream, S(12), true,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (dropMode_) {
        RECT o{S(4), S(4), w - S(4), h - S(4)};
        c.DashedRoundRect(o, S(10), S(2), S(6), S(4), kCream, 200);
    }

    cardIds_.clear();
    cardRects_.clear();
    int y = S(kTrayPad + kTrayLabelH);
    if (piles.empty()) {
        c.Text(L"바탕화면의 더미 카드를 이 막대로 끌어 오면 여기에 꽂혀요.",
               {S(kTrayPad), y, w - S(kTrayPad), y + S(64)}, RGB(222, 206, 190), S(12), false, DT_LEFT | DT_WORDBREAK);
    }
    for (int i = 0; i < shown; ++i) {
        Pile& p = *piles[i];
        bool hot = i == hoverCard_;
        // Cards sit toward the screen edge; the hovered one slides out toward the desktop.
        int x = S(kTrayPad) + (place_.right ? S(kLift) : 0) + (hot ? (place_.right ? -S(kLift) : S(kLift)) : 0);
        RECT card{x, y, x + S(kCardW), y + S(kCardH)};
        cardIds_.push_back(p.id);
        cardRects_.push_back({S(kTrayPad), y, w - S(kTrayPad), y + S(kCardH)});
        c.Shadow(card, S(8), S(hot ? 10 : 6), S(2), hot ? 90 : 60, RGB(20, 10, 4));
        FillMaterial(c, card, S(8), p.material, dpi_);
        RECT head{card.left, card.top, card.right, card.top + S(kCardHeader)};
        c.FillRoundRect(head, S(8), p.color);
        c.FillRoundRect({head.left, head.bottom - S(8), head.right, head.bottom}, 0, p.color);
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
            c.FillRoundRect(strip, S(4), kMuted);
            c.DrawImage(TopPart(front[0]->snapshot, strip.right - strip.left, strip.bottom - strip.top), strip, S(4));
            c.StrokeRoundRect(strip, S(4), 1, kBorder);
        } else {
            bool dark = kMaterials[ClampMaterial(p.material)].dark;
            std::wstring t = !p.files.empty() ? L"함께 열 파일 " + std::to_wstring(p.files.size()) + L"개"
                             : !p.missing.empty() ? L"다시 열어야 할 창 " + std::to_wstring(p.missing.size()) + L"개"
                                                  : L"창 " + std::to_wstring(n) + L"개";
            if (kMaterials[ClampMaterial(p.material)].translucent) c.FillRoundRect(strip, S(4), RGB(250, 251, 253));
            c.Text(t, strip, dark ? RGB(228, 220, 209) : kSubtleFg, S(12), false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        if (hot) c.StrokeRoundRect(card, S(8), S(2), kCream);
        y += S(kCardH + kCardGap);
    }
    if (more)
        c.Text(L"외 " + std::to_wstring(piles.size() - shown) + L"개 더", {S(kTrayPad), y, w - S(kTrayPad), y + S(22)},
               kCream, S(12), false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    c.Present(tray_, {trayRect_.left, trayRect_.top});
}

void Organizer::Refresh() {
    RenderHandle();
    RenderTray();
}

void Organizer::ShowTray() {
    KillTimer(handle_, kTimerOpen);
    if (trayOpen_) return;
    trayOpen_ = true;
    seenInside_ = false;
    outsideSince_ = 0;
    hoverCard_ = -1;
    RenderTray();
    SetWindowPos(tray_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetTimer(tray_, kTimerWatch, 100, nullptr);
}

void Organizer::HideTray() {
    if (!trayOpen_) return;
    trayOpen_ = false;
    KillTimer(tray_, kTimerWatch);
    ShowWindow(tray_, SW_HIDE);
    hoverCard_ = -1;
}

void Organizer::DragOver(POINT pt) {
    // Near the bookmark (within a card's width of the edge, around its height) the tray opens to catch it.
    int reach = Scale(kCardW, dpi_);
    bool nearEdge = place_.right ? pt.x >= monitor_.right - reach && pt.x <= monitor_.right
                                 : pt.x <= monitor_.left + reach && pt.x >= monitor_.left;
    bool nearHandle = nearEdge && pt.y >= handleRect_.top - reach && pt.y <= handleRect_.bottom + reach;
    if (nearHandle || (trayOpen_ && IsDropTarget(pt))) {
        if (!dropMode_) {
            dropMode_ = true;
            ShowTray();
            seenInside_ = true;
            RenderHandle();
            RenderTray();
        }
    } else if (dropMode_) {
        DragEnd();
    }
}

void Organizer::DragEnd() {
    if (!dropMode_) return;
    dropMode_ = false;
    HideTray();
    RenderHandle();
}

bool Organizer::IsDropTarget(POINT pt) const {
    RECT h = handleRect_;
    InflateRect(&h, Scale(24, dpi_), Scale(24, dpi_));
    if (PtInRect(&h, pt)) return true;
    return trayOpen_ && PtInRect(&trayRect_, pt);
}

int Organizer::CardAt(POINT client) const {
    for (size_t i = 0; i < cardRects_.size(); ++i)
        if (PtInRect(&cardRects_[i], client)) return (int)i;
    return -1;
}

LRESULT CALLBACK Organizer::HandleProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (Organizer*)GetWindowLongPtrW(h, GWLP_USERDATA);
    return self ? self->OnHandle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK Organizer::TrayProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (Organizer*)GetWindowLongPtrW(h, GWLP_USERDATA);
    return self ? self->OnTray(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT Organizer::OnHandle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_MOUSEMOVE: {
            if (!handleHover_) {
                handleHover_ = true;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, handle_, 0};
                TrackMouseEvent(&tme);
                RenderHandle();
                // A short hover opens the tray; grabbing the bookmark right away moves it instead.
                SetTimer(handle_, kTimerOpen, 220, nullptr);
            }
            if (handlePressed_) {
                POINT c;
                GetCursorPos(&c);
                if (!handleDragging_ && (abs(c.y - pressAt_.y) > GetSystemMetrics(SM_CYDRAG) ||
                                         abs(c.x - pressAt_.x) > GetSystemMetrics(SM_CXDRAG))) {
                    handleDragging_ = true;
                    HideTray();
                    KillTimer(handle_, kTimerOpen);
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

LRESULT Organizer::OnTray(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_TIMER: {
            if (wp != kTimerWatch || dropMode_) return 0;
            // Stays open while the pointer is over the bookmark or the tray; goes away shortly after it leaves.
            POINT c;
            GetCursorPos(&c);
            RECT t = trayRect_, h = handleRect_;
            InflateRect(&t, Scale(6, dpi_), Scale(6, dpi_));
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
        case WM_MOUSEMOVE: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (pressedCard_ >= 0) {
                POINT c;
                GetCursorPos(&c);
                if (abs(c.x - pressAt_.x) > GetSystemMetrics(SM_CXDRAG) || abs(c.y - pressAt_.y) > GetSystemMetrics(SM_CYDRAG)) {
                    // Dragging a card out: from here its desktop card follows the pointer (as going in).
                    int idx = pressedCard_;
                    pressedCard_ = -1;
                    ReleaseCapture();
                    Pile* pile = nullptr;
                    if (idx < (int)cardIds_.size())
                        for (Pile* q : host_.OrganizerPiles())
                            if (q->id == cardIds_[idx]) pile = q;
                    HideTray();
                    if (pile) host_.OnOrganizerCarryOut(*pile, c);  // may change the tray: nothing may touch it after
                    return 0;
                }
            }
            int hc = CardAt(p);
            if (hc != hoverCard_) {
                hoverCard_ = hc;
                RenderTray();
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            pressedCard_ = CardAt(p);
            GetCursorPos(&pressAt_);
            SetCapture(tray_);
            return 0;
        }
        case WM_LBUTTONUP: {
            int idx = pressedCard_;
            pressedCard_ = -1;
            ReleaseCapture();
            if (idx < 0 || idx >= (int)cardIds_.size()) return 0;
            int id = cardIds_[idx];
            Pile* pile = nullptr;
            for (Pile* p : host_.OrganizerPiles())
                if (p->id == id) pile = p;
            if (!pile) return 0;
            HideTray();
            host_.OnOrganizerOpen(*pile);  // may change the tray: nothing may touch it afterwards
            return 0;
        }
        case WM_RBUTTONUP: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int idx = CardAt(p);
            Pile* pile = nullptr;
            if (idx >= 0)
                for (Pile* q : host_.OrganizerPiles())
                    if (q->id == cardIds_[idx]) pile = q;
            POINT c;
            GetCursorPos(&c);
            HideTray();
            host_.OnOrganizerMenu(pile, c);
            return 0;
        }
    }
    return DefWindowProcW(tray_, msg, wp, lp);
}

}  // namespace wp
