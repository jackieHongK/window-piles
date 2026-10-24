#include "pile_window.h"

#include <shellapi.h>

#include <algorithm>
#include <string>

#include "common/win_util.h"
#include "ui/theme.h"

namespace wp {

namespace {

constexpr wchar_t kClass[] = L"WindowPilesPile";

// Layout in DIPs. The previews are strips of each window's top part (tabs, address bar, title):
// enough to recognise it, while a card stays much shorter than with whole-window previews.
constexpr int kMargin = 12;  // transparent room for the shadow
constexpr int kHeaderH = 32;
constexpr int kPad = 10, kPadY = 9;
constexpr int kThumbW = Pile::kPreviewW, kThumbH = Pile::kPreviewH;
constexpr int kStep = 10, kStepY = 6;  // overlap offset between stacked previews
constexpr int kCardW = kPad * 2 + kThumbW + kStep * 2;
constexpr int kThumbsH = kPadY * 2 + kThumbH + kStepY * 2;
constexpr int kMissingTitleH = 26, kMissingRowH = 28, kMissingMoreH = 22, kMissingMaxRows = 4;
constexpr COLORREF kBadge = RGB(239, 68, 68);  // notification red (shadcn red-500)

void RegisterOnce() {
    static bool done = false;
    if (done) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;  // replaced per instance (GWLP_WNDPROC) after creation
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);
    done = true;
}

std::wstring CountText(const Pile& p) {
    int n = (int)p.members.size(), hidden = p.HiddenCount();
    if (hidden == n) return std::to_wstring(n);
    return std::to_wstring(n - hidden) + L"/" + std::to_wstring(n) + L" 열림";
}

// The top part of a window snapshot in the strip's proportions (a whole window is taller).
Image TopStrip(const Image& img) {
    int h = std::max(1, img.w * kThumbH / kThumbW);
    if (img.h <= h) return img;
    Image out;
    out.w = img.w;
    out.h = h;
    out.px.assign(img.px.begin(), img.px.begin() + (size_t)img.w * h);
    return out;
}

int MissingSectionH(const Pile& p) {
    if (p.missing.empty()) return 0;
    int rows = std::min<int>((int)p.missing.size(), kMissingMaxRows);
    int h = kMissingTitleH + rows * kMissingRowH + kPad;
    if ((int)p.missing.size() > kMissingMaxRows) h += kMissingMoreH;
    if (p.members.empty()) h += kPad;
    return h;
}

}  // namespace

SIZE PileWindow::WindowSize(UINT dpi) {
    return {Scale(kCardW + kMargin * 2, dpi), Scale(kHeaderH + kThumbsH + kMargin * 2, dpi)};
}

PileWindow::PileWindow(Pile& pile, PileHost& host) : pile_(pile), host_(host) {
    RegisterOnce();
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass, L"Stack Neverflow 더미",
                            WS_POPUP, pile.pos.x, pile.pos.y, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, (LONG_PTR)this);
    SetWindowLongPtrW(hwnd_, GWLP_WNDPROC, (LONG_PTR)&PileWindow::WndProc);
    Render();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

PileWindow::~PileWindow() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
    for (auto& [path, icon] : icons_)
        if (icon) DestroyIcon(icon);
}

RECT PileWindow::ScreenRect() const { return {pile_.pos.x, pile_.pos.y, pile_.pos.x + size_.cx, pile_.pos.y + size_.cy}; }

RECT PileWindow::RenameRect() const {
    return {pile_.pos.x + renameRect_.left, pile_.pos.y + renameRect_.top, pile_.pos.x + renameRect_.right,
            pile_.pos.y + renameRect_.bottom};
}

RECT PileWindow::CardRect() const {
    return {pile_.pos.x + margin_, pile_.pos.y + margin_, pile_.pos.x + size_.cx - margin_,
            pile_.pos.y + size_.cy - margin_};
}

void PileWindow::SetHot(bool hot) {
    if (hot == hot_) return;
    hot_ = hot;
    if (visible_) Render();
}

void PileWindow::SetVisible(bool visible, HWND insertAfter) {
    if (visible == visible_) return;
    visible_ = visible;
    if (visible) {
        Render();
        SetWindowPos(hwnd_, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

HICON PileWindow::IconFor(const std::wstring& exePath) {
    for (auto& [path, icon] : icons_)
        if (path == exePath) return icon;
    HICON icon = nullptr;
    SHFILEINFOW sfi{};
    if (!exePath.empty() && SHGetFileInfoW(exePath.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON))
        icon = sfi.hIcon;
    icons_.push_back({exePath, icon});
    return icon;
}

int PileWindow::MissingAt(POINT client) const {
    for (size_t i = 0; i < missingRects_.size(); ++i)
        if (PtInRect(&missingRects_[i], client)) return (int)i;
    return -1;
}

void PileWindow::SnapToWorkArea(bool magnet) {
    RECT r = ScreenRect();
    RECT work = WorkAreaForRect(r);
    UINT dpi = DpiForPoint(pile_.pos);
    int pull = Scale(20, dpi);
    int w = size_.cx, h = size_.cy;
    int x = pile_.pos.x, y = pile_.pos.y;
    // The shadow margin may hang off the work area; the card itself stays inside.
    int m = margin_;
    if (magnet) {
        if (x + m - work.left < pull) x = work.left - m + Scale(6, dpi);
        if (work.right - (x + w - m) < pull) x = work.right - w + m - Scale(6, dpi);
        if (y + m - work.top < pull) y = work.top - m + Scale(6, dpi);
        if (work.bottom - (y + h - m) < pull) y = work.bottom - h + m - Scale(6, dpi);
    }
    x = std::clamp<int>(x, work.left - m, std::max<int>(work.left - m, work.right - w + m));
    y = std::clamp<int>(y, work.top - m, std::max<int>(work.top - m, work.bottom - h + m));
    pile_.pos = {x, y};
}

void PileWindow::Render() {
    using namespace theme;
    UINT dpi = DpiForPoint(pile_.pos);
    auto S = [dpi](int v) { return Scale(v, dpi); };
    bool hasMembers = !pile_.members.empty();
    int cardH = kHeaderH + (hasMembers ? kThumbsH : 0) + MissingSectionH(pile_);
    margin_ = S(kMargin);
    size_ = {S(kCardW) + 2 * margin_, S(cardH) + 2 * margin_};
    SnapToWorkArea(false);
    if (!visible_) return;

    Canvas c(size_.cx, size_.cy);
    RECT card{margin_, margin_, size_.cx - margin_, size_.cy - margin_};
    int radius = S(kRadiusLg);
    c.Shadow(card, radius, S(10), S(3), hot_ ? 70 : 45, kForeground);  // warm ink, not gray
    c.FillRoundRect(card, radius, kBackground);  // one clean sheet: no stacked layers behind it

    // Title bar in the pile's color (rounded top, straight bottom).
    RECT header{card.left, card.top, card.right, card.top + S(kHeaderH)};
    c.FillRoundRect({header.left, header.top, header.right, header.bottom + radius}, radius, pile_.color);
    c.FillRoundRect({header.left, header.bottom, header.right, header.bottom + radius}, 0, kBackground);
    COLORREF onColor = ContrastText(pile_.color);
    std::wstring count = hasMembers ? CountText(pile_) : L"닫힌 창 " + std::to_wstring(pile_.missing.size());
    SIZE cs = c.MeasureText(count, S(12), true);
    RECT pill{header.right - S(12) - cs.cx - S(16), header.top + S(7), header.right - S(12), header.bottom - S(7)};
    c.FillRoundRect(pill, S(10), onColor == kForeground ? RGB(0, 0, 0) : RGB(255, 255, 255), 40);
    c.Text(count, pill, onColor, S(12), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    // Title text; hovering it shows a pencil and a soft highlight: click to rename in place.
    int pencil = S(22);
    int nameLeft = header.left + S(14), nameMax = pill.left - S(10) - pencil - S(4) - nameLeft;
    int nameW = std::min<int>(c.MeasureText(pile_.name, S(14), true).cx, std::max(0, nameMax));
    RECT nameR{nameLeft, header.top, nameLeft + nameW, header.bottom};
    pencilRect_ = {nameR.right + S(4), header.top + (S(kHeaderH) - pencil) / 2, nameR.right + S(4) + pencil,
                   header.top + (S(kHeaderH) - pencil) / 2 + pencil};
    nameZone_ = {nameR.left - S(6), header.top + S(5), pencilRect_.right + S(2), header.bottom - S(5)};
    renameRect_ = {header.left + S(6), header.top + S(4), pill.left - S(8), header.bottom - S(4)};
    if (hoverName_) {
        COLORREF tint = onColor == kForeground ? RGB(0, 0, 0) : RGB(255, 255, 255);
        c.FillRoundRect(nameZone_, S(kRadiusSm), tint, 36);
        c.Glyph(0xE70F, pencilRect_, onColor, S(13));  // Edit
    }
    c.Text(pile_.name, nameR, onColor, S(14), true, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    int y = header.bottom;
    if (hasMembers) {
        // Up to 3 windows that were in front when put away, the front-most in front (bottom-left).
        int n = (int)pile_.members.size();
        auto front = pile_.FrontToBack(Pile::kPreviews);
        int shown = (int)front.size();
        int bodyTop = y + S(kPadY);
        for (int k = shown - 1; k >= 0; --k) {
            const Member& m = *front[k];
            int x = card.left + S(kPad + kStep * k), ty = bodyTop + S(kStepY * (2 - k));
            RECT box{x, ty, x + S(kThumbW), ty + S(kThumbH)};
            c.Shadow(box, S(kRadiusSm), S(4), S(1), 30, kForeground);
            c.FillRoundRect(box, S(kRadiusSm), kMuted);
            if (!m.snapshot.empty()) {
                Image strip = TopStrip(m.snapshot);
                RECT dst = strip.h * kThumbW >= strip.w * kThumbH ? box : FitRect(strip.w, strip.h, box);
                c.DrawImage(strip, dst, S(kRadiusSm));
            }
            c.StrokeRoundRect(box, S(kRadiusSm), 1, kBorder);
            if (!m.hidden && pile_.folded) c.FillRoundRect(box, S(kRadiusSm), kBackground, 150);  // this one is out
        }
        if (n > Pile::kPreviews) {
            std::wstring more = L"+" + std::to_wstring(n - Pile::kPreviews);
            SIZE ms = c.MeasureText(more, S(12), true);
            RECT badge{card.right - S(kPad) - ms.cx - S(14), bodyTop + S(4), card.right - S(kPad) - S(4), bodyTop + S(26)};
            c.FillRoundRect(badge, S(11), kPrimary, 235);
            c.Text(more, badge, kPrimaryFg, S(12), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        // Open but not the active pile (e.g. after "모든 창 꺼내기"): say so, click folds it.
        if (!pile_.folded) {
            RECT body{card.left, y, card.right, y + S(kThumbsH)};
            if (pile_.missing.empty()) body.bottom = card.bottom;
            c.FillRoundRect(body, pile_.missing.empty() ? radius : 0, kBackground, 170);
            std::wstring t = L"펼쳐져 있어요 · 누르면 접혀요";
            SIZE ts = c.MeasureText(t, S(12), true);
            int cx = (body.left + body.right) / 2, cy = (body.top + body.bottom) / 2;
            RECT chip{cx - ts.cx / 2 - S(12), cy - S(14), cx + ts.cx / 2 + S(12), cy + S(14)};
            c.FillRoundRect(chip, S(14), kBackground);
            c.StrokeRoundRect(chip, S(14), 1, kBorder);
            c.Text(t, chip, kSubtleFg, S(12), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        y += S(kThumbsH);
    }

    // Windows that were in this pile but are gone (app closed / PC restarted): reopen list.
    missingRects_.clear();
    if (!pile_.missing.empty()) {
        if (hasMembers) c.FillRoundRect({card.left + S(kPad), y, card.right - S(kPad), y + 1}, 0, kBorder);
        else y += S(kPad);
        RECT title{card.left + S(14), y, card.right - S(14), y + S(kMissingTitleH)};
        c.Text(L"다시 열어야 할 창 " + std::to_wstring(pile_.missing.size()) + L"개", title, kMutedFg, S(12), true,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        y += S(kMissingTitleH);
        int rows = std::min<int>((int)pile_.missing.size(), kMissingMaxRows);
        for (int i = 0; i < rows; ++i) {
            const MissingWindow& mw = pile_.missing[i];
            RECT row{card.left + S(6), y, card.right - S(6), y + S(kMissingRowH)};
            missingRects_.push_back(row);  // canvas coords == client coords
            bool canOpen = !mw.exePath.empty();
            if (i == hoverMissing_ && canOpen) c.FillRoundRect(row, S(kRadiusSm), kMuted);
            int ix = row.left + S(8), iy = row.top + (S(kMissingRowH) - S(16)) / 2;
            c.FillRoundRect({ix, iy, ix + S(16), iy + S(16)}, S(3), i == hoverMissing_ && canOpen ? kMuted : kBackground);
            c.DrawIcon(IconFor(mw.exePath), ix, iy, S(16));
            std::wstring text = mw.appName + (mw.title.empty() ? L"" : L" · " + mw.title);
            int right = row.right - S(8) - (i == hoverMissing_ && canOpen ? S(44) : 0);
            c.Text(text, {ix + S(24), row.top, right, row.bottom}, kSubtleFg, S(12), false,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            if (i == hoverMissing_ && canOpen)
                c.Text(L"열기 ↗", {right, row.top, row.right - S(8), row.bottom}, kForeground, S(12), true,
                       DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            y += S(kMissingRowH);
        }
        if ((int)pile_.missing.size() > kMissingMaxRows) {
            RECT more{card.left + S(14), y, card.right - S(14), y + S(kMissingMoreH)};
            c.Text(L"외 " + std::to_wstring(pile_.missing.size() - kMissingMaxRows) + L"개 더", more, kMutedFg, S(12),
                   false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    if (hot_) c.StrokeRoundRect(card, radius, S(2), kPrimary);
    else if (hover_) c.StrokeRoundRect(card, radius, 1, kBorderStrong);
    else c.StrokeRoundRect(card, radius, 1, kBorder);

    // Notification badge on the top-right corner (like an app icon badge): the unread total of the
    // hidden windows, or a dot when an app only flashed for attention.
    int badge = pile_.BadgeCount();
    if (badge > 0 || pile_.BadgeAttention()) {
        std::wstring txt = badge > 99 ? L"99+" : badge > 0 ? std::to_wstring(badge) : L"";
        int h = txt.empty() ? S(18) : S(26);
        int w = txt.empty() ? h : std::max(h, (int)c.MeasureText(txt, S(14), true).cx + S(16));
        int cx = card.right - std::max(S(6), w / 2 - S(10)), cy = card.top + S(6);
        RECT b{cx - w / 2, cy - h / 2, cx - w / 2 + w, cy - h / 2 + h};
        RECT ring{b.left - S(2), b.top - S(2), b.right + S(2), b.bottom + S(2)};
        c.Shadow(ring, h / 2 + S(2), S(4), S(1), 70);
        c.FillRoundRect(ring, h / 2 + S(2), RGB(255, 255, 255));
        c.FillRoundRect(b, h / 2, kBadge);
        if (!txt.empty()) c.Text(txt, b, RGB(255, 255, 255), S(14), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    c.Present(hwnd_, pile_.pos);
}

LRESULT CALLBACK PileWindow::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (PileWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    return self->Handle(msg, wp, lp);
}

LRESULT PileWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;  // clicking a pile never steals focus from the user's work
        case WM_LBUTTONDOWN: {
            pressed_ = true;
            dragging_ = false;
            GetCursorPos(&pressCursor_);
            pressPos_ = pile_.pos;
            SetCapture(hwnd_);
            return 0;
        }
        case WM_MOUSEMOVE: {
            bool needRender = false;
            if (!hover_) {
                hover_ = true;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                needRender = true;
            }
            POINT client{(short)LOWORD(lp), (short)HIWORD(lp)};
            int hm = pressed_ ? -1 : MissingAt(client);
            if (hm != hoverMissing_) {
                hoverMissing_ = hm;
                needRender = true;
            }
            bool hn = !pressed_ && PtInRect(&nameZone_, client);
            if (hn != hoverName_) {
                hoverName_ = hn;
                needRender = true;
            }
            if (needRender && !dragging_) Render();
            if (!pressed_) return 0;
            POINT c;
            GetCursorPos(&c);
            int dx = c.x - pressCursor_.x, dy = c.y - pressCursor_.y;
            if (!dragging_ && (abs(dx) > GetSystemMetrics(SM_CXDRAG) || abs(dy) > GetSystemMetrics(SM_CYDRAG))) {
                dragging_ = true;
                // A card being carried is on top of the others (and stays there: OnPileMoved).
                SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            if (dragging_) {
                pile_.pos = {pressPos_.x + dx, pressPos_.y + dy};
                SetWindowPos(hwnd_, nullptr, pile_.pos.x, pile_.pos.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            hover_ = false;
            hoverName_ = false;
            hoverMissing_ = -1;
            Render();
            return 0;
        case WM_SETCURSOR:
            // I-beam over the title says "you can type here"; the rest of the card is a button.
            SetCursor(LoadCursorW(nullptr, hoverName_ ? IDC_IBEAM : IDC_HAND));
            return TRUE;
        case WM_LBUTTONUP: {
            if (!pressed_) return 0;
            pressed_ = false;
            ReleaseCapture();
            if (dragging_) {
                dragging_ = false;
                SnapToWorkArea(true);
                Render();  // DPI may differ on the new monitor
                host_.OnPileMoved(pile_);
                return 0;
            }
            POINT client{(short)LOWORD(lp), (short)HIWORD(lp)};
            int mi = MissingAt(client);
            if (mi >= 0) {
                host_.OnMissingClick(pile_, mi);
                return 0;
            }
            if (PtInRect(&nameZone_, client)) {
                hoverName_ = false;
                Render();
                host_.OnPileRename(pile_);
                return 0;
            }
            // A double click is two clicks: treat it as one toggle.
            DWORD now = GetTickCount();
            if (now - lastClickTick_ > GetDoubleClickTime()) {
                lastClickTick_ = now;
                host_.OnPileClick(pile_);
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (pressed_ && (HWND)lp != hwnd_) {
                pressed_ = false;
                if (dragging_) {
                    dragging_ = false;
                    SnapToWorkArea(true);
                    Render();
                    host_.OnPileMoved(pile_);
                }
            }
            return 0;
        case WM_RBUTTONUP: {
            POINT c;
            GetCursorPos(&c);
            host_.OnPileMenu(pile_, c);
            return 0;
        }
        case WM_DPICHANGED:
            Render();
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace wp
