#include "guide_window.h"

#include <windowsx.h>

#include <algorithm>

#include "app/app_icon.h"
#include "common/canvas.h"
#include "common/win_util.h"
#include "ui/popup_base.h"
#include "ui/theme.h"

namespace wp {

namespace {

constexpr int kWidth = 520, kPad = 28, kBadge = 26, kGapStep = 18, kMark = 40;
const wchar_t kCaption[] = L"Stack Neverflow 사용법";  // taskbar / Alt+Tab
const wchar_t kWordmark[] = L"Stack Neverflow";
const wchar_t kSlogan[] = L"Tasks stack up, but never overflow.";
const wchar_t kTitle[] = L"사용법";
const wchar_t kSubtitle[] = L"이 다섯 가지만 알면 바로 쓸 수 있어요.";
const wchar_t kTipTitle[] = L"알아두면 좋아요";
const wchar_t kTip[] =
    L"컴퓨터를 다시 켜거나 프로그램을 업데이트해도 더미는 그대로 남아요. 그사이 닫힌 앱은 더미 안 '다시 열어야 할 "
    L"창' 목록에 보이고, 누르면 그 앱이 열려요. 그 앱 창을 다시 더미에 넣으면 목록에서 사라져요.\n"
    L"'모든 창 꺼내기'와 'Neverflow 종료'는 더미 오른쪽 버튼 메뉴나 화면 오른쪽 아래 트레이 아이콘에 있어요. 프로그램이 "
    L"갑자기 꺼져도 숨겨 둔 창은 자동으로 다시 나타나요.";
const wchar_t kButton[] = L"알겠어요";

}  // namespace

GuideWindow::~GuideWindow() { Close(); }

void GuideWindow::Close() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void GuideWindow::Show() {
    if (hwnd_) {
        SetForegroundWindow(hwnd_);
        return;
    }
    steps_ = {
        {L"창 넣기",
         L"창의 제목줄을 잡고 끌면 화면 위쪽에 '＋ 새 더미' 칸이 나타나요. 그 위에 놓으면 창이 쏙 들어가고 화면 "
         L"오른쪽 위에 더미가 생겨요."},
        {L"더 넣기", L"다른 창을 끌어서 더미 위에 놓으면 같은 더미에 함께 들어가요."},
        {L"펼치기",
         L"더미를 한 번 누르면 창들이 원래 자리로 돌아와요. 화면 가장자리에 색 테두리와 맨 위 이름표가 생겨서 "
         L"지금 어떤 작업 중인지 알 수 있어요."},
        {L"접기",
         L"맨 위 이름표를 누르면 창들이 다시 더미 자리로 모여요. 다른 더미를 펼치면 지금 펼친 더미는 자동으로 "
         L"접혀요."},
        {L"이름·색 바꾸기", L"더미를 오른쪽 버튼으로 누르고 '이름·색 바꾸기'를 고르세요."},
    };
    POINT center;
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    center = {(work.left + work.right) / 2, (work.top + work.bottom) / 2};
    dpi_ = DpiForPoint(center);
    Layout();
    int x = center.x - size_.cx / 2, y = center.y - size_.cy / 2;
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, RegisterPopupClass(L"WindowPilesGuide", &GuideWindow::Proc),
                            kCaption, WS_POPUP, x, y, size_.cx, size_.cy, nullptr, nullptr, GetModuleHandleW(nullptr),
                            nullptr);
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, (LONG_PTR)this);
    ApplyRoundedCorners(hwnd_);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void GuideWindow::Layout() {
    auto S = [this](int v) { return Scale(v, dpi_); };
    int w = S(kWidth), pad = S(kPad);
    int y = pad;
    closeRect_ = {w - S(16) - S(32), S(14), w - S(16), S(14) + S(32)};
    y += S(kMark) + S(18);  // brand row: mark + wordmark + slogan
    y += MeasureUiText(kTitle, S(20), true).cy + S(4);
    y += MeasureUiText(kSubtitle, S(13), false).cy;
    headerH_ = y;
    y += S(22);

    int textX = pad + S(kBadge) + S(14);
    int textW = w - textX - pad;
    for (auto& s : steps_) {
        int th = MeasureUiText(s.title, S(15), true).cy;
        int dh = MeasureUiText(s.text, S(13), false, textW).cy;
        s.badge = {pad, y, pad + S(kBadge), y + S(kBadge)};
        s.titleRect = {textX, y + (S(kBadge) - th) / 2, w - pad, y + (S(kBadge) - th) / 2 + th};
        s.textRect = {textX, s.titleRect.bottom + S(4), textX + textW, s.titleRect.bottom + S(4) + dh};
        y = s.textRect.bottom + S(kGapStep);
    }
    y += S(2);
    int tipPad = S(14);
    int tipTitleH = MeasureUiText(kTipTitle, S(13), true).cy;
    int tipTextH = MeasureUiText(kTip, S(13), false, w - 2 * pad - 2 * tipPad).cy;
    tipRect_ = {pad, y, w - pad, y + tipPad + tipTitleH + S(4) + tipTextH + tipPad};
    tipTitleRect_ = {pad + tipPad, y + tipPad, w - pad - tipPad, y + tipPad + tipTitleH};
    tipTextRect_ = {pad + tipPad, tipTitleRect_.bottom + S(4), w - pad - tipPad, tipTitleRect_.bottom + S(4) + tipTextH};
    y = tipRect_.bottom + S(22);
    int bw = MeasureUiText(kButton, S(14), true).cx + S(40);
    buttonRect_ = {w - pad - bw, y, w - pad, y + S(40)};
    y = buttonRect_.bottom + pad;
    size_ = {w, y};
}

void GuideWindow::Paint(HDC dc) {
    using namespace theme;
    auto S = [this](int v) { return Scale(v, dpi_); };
    Canvas c(size_.cx, size_.cy);
    c.FillRoundRect({0, 0, size_.cx, size_.cy}, 0, kBackground);
    if (!HasDwmRoundedCorners()) c.StrokeRoundRect({0, 0, size_.cx, size_.cy}, 0, 1, kBorder);
    int pad = S(kPad);

    // Brand row: the mark, the wordmark and the slogan.
    DrawMark(c, pad, pad, S(kMark));
    int bx = pad + S(kMark) + S(12);
    SIZE ws = c.MeasureText(kWordmark, S(17), true), sl = c.MeasureText(kSlogan, S(12), false);
    int by = pad + (S(kMark) - ws.cy - S(2) - sl.cy) / 2;
    c.Text(kWordmark, {bx, by, size_.cx - pad, by + ws.cy}, kForeground, S(17), true, DT_LEFT | DT_TOP | DT_SINGLELINE);
    c.Text(kSlogan, {bx, by + ws.cy + S(2), size_.cx - pad, by + ws.cy + S(2) + sl.cy}, kMutedFg, S(12), false,
           DT_LEFT | DT_TOP | DT_SINGLELINE);

    int y = pad + S(kMark) + S(18);
    SIZE ts = c.MeasureText(kTitle, S(20), true);
    c.Text(kTitle, {pad, y, size_.cx - pad, y + ts.cy}, kForeground, S(20), true, DT_LEFT | DT_TOP | DT_SINGLELINE);
    y += ts.cy + S(4);
    SIZE ss = c.MeasureText(kSubtitle, S(13), false);
    c.Text(kSubtitle, {pad, y, size_.cx - pad, y + ss.cy}, kMutedFg, S(13), false, DT_LEFT | DT_TOP | DT_SINGLELINE);
    c.FillRoundRect({pad, headerH_ + S(11), size_.cx - pad, headerH_ + S(12)}, 0, kBorder);

    if (hoverClose_) c.FillRoundRect(closeRect_, S(kRadiusSm), kMuted);
    c.Text(L"✕", closeRect_, hoverClose_ ? kForeground : kMutedFg, S(14), false, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    for (size_t i = 0; i < steps_.size(); ++i) {
        const Step& s = steps_[i];
        c.FillRoundRect(s.badge, S(kBadge / 2), kMuted);
        c.StrokeRoundRect(s.badge, S(kBadge / 2), 1, kBorder);
        c.Text(std::to_wstring(i + 1), s.badge, kForeground, S(13), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        c.Text(s.title, s.titleRect, kForeground, S(15), true, DT_LEFT | DT_TOP | DT_SINGLELINE);
        c.Text(s.text, s.textRect, kSubtleFg, S(13), false, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }

    c.FillRoundRect(tipRect_, S(kRadius), kMuted);
    c.Text(kTipTitle, tipTitleRect_, kForeground, S(13), true, DT_LEFT | DT_TOP | DT_SINGLELINE);
    c.Text(kTip, tipTextRect_, kSubtleFg, S(13), false, DT_LEFT | DT_TOP | DT_WORDBREAK);

    c.FillRoundRect(buttonRect_, S(kRadius), hoverButton_ ? kPrimaryHover : kPrimary);
    c.Text(kButton, buttonRect_, kPrimaryFg, S(14), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    c.Blit(dc);
}

LRESULT CALLBACK GuideWindow::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (GuideWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    return self->Handle(msg, wp, lp);
}

LRESULT GuideWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            Paint(dc);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_NCHITTEST: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd_, &p);
            if (p.y < headerH_ && !PtInRect(&closeRect_, p)) return HTCAPTION;  // drag by the header
            return HTCLIENT;
        }
        case WM_MOUSEMOVE: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            bool hc = PtInRect(&closeRect_, p) != FALSE, hb = PtInRect(&buttonRect_, p) != FALSE;
            if (hc != hoverClose_ || hb != hoverButton_) {
                hoverClose_ = hc;
                hoverButton_ = hb;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            SetCursor(LoadCursorW(nullptr, (hc || hb) ? IDC_HAND : IDC_ARROW));
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE:
            hoverClose_ = hoverButton_ = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (PtInRect(&closeRect_, p) || PtInRect(&buttonRect_, p)) Close();
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE || wp == VK_RETURN) Close();
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace wp
