#include "organizer.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "common/canvas.h"
#include "common/win_util.h"
#include "notes/memo_store.h"
#include "notes/memo_window.h"
#include "ui/material.h"
#include "ui/menu.h"
#include "ui/theme.h"

namespace wp {

namespace {

// Layout in DIPs.
constexpr int kHandleW = 14, kHandleHoverW = 18, kHandleH = 128;
constexpr int kTrayPad = 12, kTrayLabelH = 24, kCardW = 260, kCardHeader = 26, kCardStrip = 40, kCardGap = 8;
constexpr int kCardH = kCardHeader + kCardStrip;
constexpr int kMemoLabelH = 22, kMemoH = 44, kMemoMax = 5, kPlusH = 36, kMoreH = 22;
// Hit codes for what isn't a pile card (pile cards are their index).
constexpr int kItemMemo = 1000, kItemPlus = 2000, kItemMemoMore = 2001;
const COLORREF kNotePaper = RGB(255, 252, 240);  // as the memo window
constexpr int kLift = 10;            // a hovered card slides this far out of the tray
constexpr double kHideAfterMs = 350;  // the pointer left the bookmark and tray this long ago: put it away
constexpr UINT_PTR kTimerWatch = 1, kTimerOpen = 2, kTimerAnim = 3;
// Crumpling a memo into the trash: into a ball until kBallAt, falls in until kLandAt (the lid starts
// shutting at kShutAt), then the can settles.
constexpr double kCrumpleMs = 640, kBallAt = 0.45, kShutAt = 0.74, kLandAt = 0.78;
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

void Organizer::DrawPileCard(Canvas& c, const RECT& card, Pile& p, bool hot) {
    using namespace theme;
    auto S = [this](int v) { return Scale(v, dpi_); };
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
}

void Organizer::DrawMemoCard(Canvas& c, const RECT& card, const MemoInfo& m, bool hot) {
    using namespace theme;
    auto S = [this](int v) { return Scale(v, dpi_); };
    c.Shadow(card, S(8), S(hot ? 10 : 6), S(2), hot ? 90 : 60, RGB(20, 10, 4));
    c.FillRoundRect(card, S(8), kNotePaper);
    RECT t{card.left + S(12), card.top + S(5), card.right - S(12), card.top + S(23)};
    c.Text(m.title.empty() ? L"빈 메모" : m.title, t, m.title.empty() ? kMutedFg : kForeground, S(12), true,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (!m.excerpt.empty())
        c.Text(m.excerpt, {t.left, t.bottom, t.right, card.bottom - S(4)}, kMutedFg, S(11), false,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (hot) c.StrokeRoundRect(card, S(8), S(2), kCream);
}

// The trash can "＋ 새 메모" turns into while a memo is carried. A memo dropped in crumples into a ball
// above the open can, falls in, and the lid closes with a small bounce.
void Organizer::DrawTrash(Canvas& c, const RECT& area, double now) {
    using namespace theme;
    using PtF = Canvas::PtF;
    auto S = [this](int v) { return (float)Scale(v, dpi_); };
    constexpr double kPi = 3.14159265358979;
    double t = crumpleStart_ ? std::min(1.0, (now - crumpleStart_) / kCrumpleMs) : -1;  // 0..1 while crumpling
    if (drag_.overTrash) c.FillRoundRect(area, (int)S(8), RGB(255, 255, 255), 40);
    c.DashedRoundRect(area, (int)S(8), (int)S(1) + drag_.overTrash, (int)S(5), (int)S(4), kCream, drag_.overTrash ? 230 : 150);

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
            double edge = std::min(hw / std::max(1e-3, std::fabs(co)), hh / std::max(1e-3, std::fabs(si)));  // the card
            double ball = S(8) * (1 + 0.24 * std::sin(i * 2.4 + 1) + 0.12 * std::cos(i * 5.3));          // crumpled
            double rad = edge * (1 - k) + ball * k;
            outer.push_back({px + (float)(rad * co), py + (float)(rad * si)});
            inner.push_back({px + (float)(rad * co * 0.76), py + (float)(rad * si * 0.76) - k});
        }
        c.FillPolygons({outer}, Mix(kNotePaper, RGB(190, 176, 146), 0.6 * k));
        c.FillPolygons({inner}, Mix(kNotePaper, RGB(228, 216, 188), 0.5 * k));
    }
    // The can: a tapered bucket with three ribs.
    c.FillPolygons({{{cx - S(9), mouth + bounce}, {cx + S(9), mouth + bounce}, {cx + S(7), bottom + bounce}, {cx - S(7), bottom + bounce}}},
                   kCream, 235);
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
                   kCream, 235);

    std::wstring label = t >= 0 ? L"휴지통에 버렸어요" : drag_.overTrash ? L"놓으면 휴지통으로" : L"여기에 놓아 버리기";
    c.Text(label, {(int)(cx + S(22)), area.top, area.right - (int)S(10), area.bottom}, kCream, (int)S(12), true,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void Organizer::RenderTray() {
    if (!trayOpen_) return;
    using namespace theme;
    auto S = [this](int v) { return Scale(v, dpi_); };
    std::vector<Pile*> piles = host_.OrganizerPiles();
    int w = S(kTrayPad * 2 + kCardW + kLift);
    int maxH = (work_.bottom - work_.top) - S(32);
    int fixed = S(kTrayPad * 2 + kTrayLabelH);
    // The memo block: a label and the newest few, then the button for a new one.
    int memoShown = std::min<int>((int)memos_.size(), kMemoMax);
    bool memoMore = (int)memos_.size() > memoShown;
    int memoBlock = S(kCardGap * 2) + (memoShown ? S(kMemoLabelH) + memoShown * S(kMemoH + kCardGap) : 0) +
                    (memoMore ? S(kMoreH) : 0) + S(kPlusH);
    int fit = std::max(0, (maxH - fixed - memoBlock + S(kCardGap)) / S(kCardH + kCardGap));
    int shown = std::min<int>((int)piles.size(), fit);
    bool more = (int)piles.size() > shown;
    int contentH = shown ? shown * S(kCardH + kCardGap) - S(kCardGap) : S(64);
    int h = fixed + contentH + (more ? S(kMoreH) : 0) + memoBlock;
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

    // Cards sit toward the screen edge; a hovered or carried one slides out toward the desktop.
    int cardX = S(kTrayPad) + (place_.right ? S(kLift) : 0);
    int slide = place_.right ? -S(kLift) : S(kLift);
    int draggedPile = drag_.item >= 0 && drag_.item < shown ? drag_.item : -1;
    int draggedMemo = drag_.item >= kItemMemo && drag_.item - kItemMemo < memoShown ? drag_.item - kItemMemo : -1;
    // While a card is carried, the one under it makes room: the slot it would land in stays free.
    auto land = [&](int listTop, int unit, int count) {
        drag_.slot = std::clamp((drag_.y - drag_.grabDy - listTop + unit / 2) / unit, 0, count - 1);
    };
    auto slotY = [&](int i, int listTop, int unit, int dragged) {
        if (dragged < 0) return listTop + i * unit;
        if (i == dragged) return listTop + drag_.slot * unit;
        int k = i - (i > dragged);
        return listTop + (k + (k >= drag_.slot)) * unit;
    };
    auto gap = [&](int y0, int ch) { c.DashedRoundRect({cardX, y0, cardX + S(kCardW), y0 + ch}, S(8), S(1), S(5), S(4), kCream, 110); };

    cardIds_.clear();
    cardRects_.clear();
    int y = S(kTrayPad + kTrayLabelH), unit = S(kCardH + kCardGap);
    if (piles.empty())
        c.Text(L"바탕화면의 더미 카드를 이 막대로 끌어 오면 여기에 꽂혀요.",
               {S(kTrayPad), y, w - S(kTrayPad), y + S(64)}, RGB(222, 206, 190), S(12), false, DT_LEFT | DT_WORDBREAK);
    if (draggedPile >= 0) land(y, unit, shown);
    for (int i = 0; i < shown; ++i) {
        int yy = slotY(i, y, unit, draggedPile);
        cardIds_.push_back(piles[i]->id);
        cardRects_.push_back({S(kTrayPad), yy, w - S(kTrayPad), yy + S(kCardH)});
        if (i == draggedPile) {
            gap(yy, S(kCardH));
            continue;
        }
        bool hot = i == hover_ && drag_.item < 0;
        int x = cardX + (hot ? slide : 0);
        DrawPileCard(c, {x, yy, x + S(kCardW), yy + S(kCardH)}, *piles[i], hot);
    }
    y += contentH;
    if (more) {
        c.Text(L"외 " + std::to_wstring(piles.size() - shown) + L"개 더", {S(kTrayPad), y, w - S(kTrayPad), y + S(kMoreH)},
               kCream, S(12), false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        y += S(kMoreH);
    }

    // Memos: plain .txt files in the memo folder, in the order they were arranged.
    y += S(kCardGap * 2);
    memoRects_.clear();
    if (memoShown) {
        c.Text(L"메모 · " + std::to_wstring(memos_.size()) + L"개", {S(kTrayPad), y, w - S(kTrayPad), y + S(kMemoLabelH)},
               kCream, S(12), true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        y += S(kMemoLabelH);
    }
    unit = S(kMemoH + kCardGap);
    if (draggedMemo >= 0) land(y, unit, memoShown);
    for (int i = 0; i < memoShown; ++i) {
        int yy = slotY(i, y, unit, draggedMemo);
        memoRects_.push_back({S(kTrayPad), yy, w - S(kTrayPad), yy + S(kMemoH)});
        if (i == draggedMemo) {
            if (!drag_.overTrash) gap(yy, S(kMemoH));
            continue;
        }
        bool hot = hover_ == kItemMemo + i && drag_.item < 0;
        int x = cardX + (hot ? slide : 0);
        DrawMemoCard(c, {x, yy, x + S(kCardW), yy + S(kMemoH)}, memos_[i], hot);
    }
    y += memoShown * unit;
    memoMoreRect_ = {};
    if (memoMore) {
        memoMoreRect_ = {S(kTrayPad), y - S(kCardGap), w - S(kTrayPad), y - S(kCardGap) + S(kMoreH)};
        c.Text(L"메모 " + std::to_wstring(memos_.size() - memoShown) + L"개 더 · 폴더 열기", memoMoreRect_, kCream, S(12),
               hover_ == kItemMemoMore, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        y += S(kMoreH);
    }
    // "＋ 새 메모", as wide as a card; the trash can while a memo is carried or going in.
    RECT area{cardX, y, cardX + S(kCardW), y + S(kPlusH)};
    plusRect_ = {S(kTrayPad), y, w - S(kTrayPad), y + S(kPlusH)};
    if (draggedMemo >= 0 || crumpleStart_) {
        DrawTrash(c, area, NowMs());
    } else {
        bool hot = hover_ == kItemPlus;
        if (hot) c.FillRoundRect(area, S(8), RGB(255, 255, 255), 36);
        c.DashedRoundRect(area, S(8), S(1) + hot, S(5), S(4), kCream, hot ? 230 : 150);
        c.Text(L"＋  새 메모", area, kCream, S(12), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // The carried card, on top of everything, following the pointer.
    if (draggedPile >= 0 || draggedMemo >= 0) {
        int ch = draggedPile >= 0 ? S(kCardH) : S(kMemoH);
        int ty = std::clamp(drag_.y - drag_.grabDy, S(4), h - ch - S(4));
        RECT card{cardX + slide, ty, cardX + slide + S(kCardW), ty + ch};
        if (drag_.overTrash) InflateRect(&card, -S(12), -S(3));  // about to go in: it shrinks a little
        drag_.card = card;
        if (draggedPile >= 0) DrawPileCard(c, card, *piles[draggedPile], true);
        else DrawMemoCard(c, card, memos_[draggedMemo], true);
    }
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
    hover_ = -1;
    memos_ = ListMemos();
    RenderTray();
    SetWindowPos(tray_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetTimer(tray_, kTimerWatch, 100, nullptr);
}

void Organizer::HideTray() {
    if (!trayOpen_) return;
    trayOpen_ = false;
    KillTimer(tray_, kTimerWatch);
    ShowWindow(tray_, SW_HIDE);
    hover_ = -1;
    pressed_ = -1;
    drag_ = {};
    crumpleStart_ = 0;
    KillTimer(tray_, kTimerAnim);
    if (GetCapture() == tray_) ReleaseCapture();
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

int Organizer::ItemAt(POINT client) const {
    for (size_t i = 0; i < cardRects_.size(); ++i)
        if (PtInRect(&cardRects_[i], client)) return (int)i;
    for (size_t i = 0; i < memoRects_.size(); ++i)
        if (PtInRect(&memoRects_[i], client)) return kItemMemo + (int)i;
    if (PtInRect(&plusRect_, client)) return kItemPlus;
    if (PtInRect(&memoMoreRect_, client)) return kItemMemoMore;
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
    if (item == kItemPlus) {
        HideTray();
        std::wstring path = NewMemo();
        if (!path.empty()) OpenMemoWindow(path, anchor);
    } else if (item >= kItemMemo && item - kItemMemo < (int)memos_.size()) {
        std::wstring path = memos_[item - kItemMemo].path;
        HideTray();
        OpenMemoWindow(path, anchor);
    } else if (item == kItemMemoMore) {
        HideTray();
        ShellExecuteW(nullptr, L"open", MemoDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (Pile* pile = PileAt(item)) {
        HideTray();
        host_.OnOrganizerOpen(*pile);  // may change the tray: nothing may touch it afterwards
    }
}

void Organizer::MemoMenu(int memo, POINT screenPt) {
    if (memo < 0 || memo >= (int)memos_.size()) return;
    std::wstring path = memos_[memo].path;
    POINT anchor{place_.right ? trayRect_.left : trayRect_.right, screenPt.y};
    HideTray();
    enum { kOpen = 1, kReveal, kRecycle };
    UINT id = ShowMenu({{kOpen, L"열기"}, {kReveal, L"파일 위치 열기"}, {0, L""},
                        {kRecycle, L"휴지통으로 버리기", L"되살릴 수 있어요"}},
                       screenPt);
    if (id == kOpen) OpenMemoWindow(path, anchor);
    else if (id == kReveal) RevealInExplorer(path);
    else if (id == kRecycle) RecycleMemo(path);
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

void Organizer::EndDrag(POINT) {
    Drag d = drag_;
    drag_ = {};
    if (GetCapture() == tray_) ReleaseCapture();
    if (d.item < kItemMemo) {
        std::vector<int> ids;
        for (Pile* q : host_.OrganizerPiles()) ids.push_back(q->id);
        if (d.item < (int)ids.size() && d.slot >= 0 && d.slot != d.item) {
            int id = ids[d.item];
            ids.erase(ids.begin() + d.item);
            ids.insert(ids.begin() + std::min<int>(d.slot, (int)ids.size()), id);
            host_.OnOrganizerReorder(ids);  // redraws the tray
            return;
        }
    } else if (int i = d.item - kItemMemo; i < (int)memos_.size()) {
        if (d.overTrash) {
            RecycleMemo(memos_[i].path);  // in the Recycle Bin right away; what follows is the animation
            memos_.erase(memos_.begin() + i);
            crumpleFrom_ = d.card;
            OffsetRect(&crumpleFrom_, -(Scale(kTrayPad, dpi_) + (place_.right ? Scale(kLift, dpi_) : 0)), -plusRect_.top);
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
            if (wp != kTimerWatch || dropMode_ || drag_.item >= 0 || crumpleStart_) return 0;
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
        case WM_MOUSEMOVE: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (drag_.item >= 0) {
                drag_.y = p.y;
                bool memo = drag_.item >= kItemMemo;
                // Off the tray toward the desktop: a pile card goes back there, a memo becomes its window.
                RECT cr;
                GetClientRect(tray_, &cr);
                if (place_.right ? p.x < -S(24) : p.x > cr.right + S(24)) {
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
                if (memo) {
                    RECT t = plusRect_;
                    InflateRect(&t, 0, S(6));
                    drag_.overTrash = PtInRect(&t, p) != FALSE;
                }
                RenderTray();
                return 0;
            }
            bool movable = pressed_ >= 0 && pressed_ < kItemPlus;
            if (movable && (abs(p.x - pressPt_.x) > GetSystemMetrics(SM_CXDRAG) || abs(p.y - pressPt_.y) > GetSystemMetrics(SM_CYDRAG))) {
                const RECT* from = pressed_ < kItemMemo ? (pressed_ < (int)cardRects_.size() ? &cardRects_[pressed_] : nullptr)
                                                        : (pressed_ - kItemMemo < (int)memoRects_.size() ? &memoRects_[pressed_ - kItemMemo] : nullptr);
                if (from) {
                    drag_ = {};
                    drag_.item = pressed_;
                    drag_.grabDy = pressPt_.y - from->top;
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
                RenderTray();
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
        case WM_RBUTTONUP: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int item = ItemAt(p);
            POINT c;
            GetCursorPos(&c);
            if (item >= kItemMemo && item < kItemPlus) {
                MemoMenu(item - kItemMemo, c);
                return 0;
            }
            Pile* pile = PileAt(item);
            HideTray();
            host_.OnOrganizerMenu(pile, c);
            return 0;
        }
    }
    return DefWindowProcW(tray_, msg, wp, lp);
}

}  // namespace wp
