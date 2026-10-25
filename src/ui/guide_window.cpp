#include "guide_window.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>

#include "app/app_icon.h"
#include "common/canvas.h"
#include "common/win_util.h"
#include "surface/organizer.h"
#include "ui/material.h"
#include "ui/popup_base.h"
#include "ui/theme.h"
#include "ui/wordmark.h"

namespace wp {

namespace {

constexpr int kWidth = 520, kPad = 28, kBadge = 26, kGapStep = 18, kMark = 40;
constexpr int kTabsH = 36, kRowH = 52, kToggleW = 40, kToggleH = 22, kSideW = 64, kSwatchH = 34, kSwatchGap = 8;
constexpr int kSwatchesPerRow = 7;
const wchar_t kCaption[] = L"Stack Neverflow 설정 · 사용법";  // taskbar / Alt+Tab
const wchar_t kSlogan[] = L"Tasks stack up, but never overflow.";
const wchar_t* const kTabs[] = {L"설정", L"사용법"};
const wchar_t* const kSections[] = {L"기본", L"가장자리 오거나이저", L"메모"};
const wchar_t kTipTitle[] = L"알아두면 좋아요";
const wchar_t kTip[] =
    L"컴퓨터를 다시 켜거나 프로그램을 업데이트해도 더미는 그대로 남아요. 그사이 닫힌 앱은 더미 안 '다시 열어야 할 "
    L"창' 목록에 보이고, 누르면 그 앱이 열려요. 그 앱 창을 다시 더미에 넣으면 목록에서 사라져요.\n"
    L"'모든 창 꺼내기'와 'Neverflow 종료'는 더미 오른쪽 버튼 메뉴나 화면 오른쪽 아래 트레이 아이콘에 있어요. 프로그램이 "
    L"갑자기 꺼져도 숨겨 둔 창은 자동으로 다시 나타나요.";

// What can be clicked.
enum Hit {
    kHitNone = 0,
    kHitClose,
    kHitButton,
    kHitTab = 10,  // + tab
    kHitAutostart = 20,
    kHitShown,
    kHitOrganizer,
    kHitSlide,
    kHitSide = 30,  // + 0 left, 1 right
    kHitFolder = 40,
    kHitMaterial = 100,  // + index into kOrganizerMaterials
};

// A see-through swatch (glass) drawn on the white card: as it would look over white.
void FlattenOnWhite(Canvas& c, const RECT& r) {
    uint32_t* px = c.pixels();
    for (int y = std::max<int>(0, r.top); y < std::min<int>(c.height(), r.bottom); ++y)
        for (int x = std::max<int>(0, r.left); x < std::min<int>(c.width(), r.right); ++x) {
            uint32_t& p = px[(size_t)y * c.width() + x];
            uint32_t a = p >> 24, add = 255 - a;
            auto ch = [&](int s) { return std::min<uint32_t>(255, ((p >> s) & 255) + add); };
            p = 0xFF000000u | (ch(16) << 16) | (ch(8) << 8) | ch(0);
        }
}

}  // namespace

GuideWindow::~GuideWindow() { Close(); }

void GuideWindow::Close() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void GuideWindow::Show(Tab tab) {
    tab_ = tab;
    s_ = get_();
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
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
        {L"오거나이저와 메모",
         L"더미 카드를 화면 끝 손잡이로 끌어다 빈 칸에 놓으면 오거나이저에 꽂혀요. 손잡이나 그 높이의 화면 끝에 "
         L"마우스를 대면 서랍이 열리고, 포스트잇 디스펜서의 PUSH를 누르면 새 메모가 날아와 열려요."},
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
    closeRect_ = {w - S(16) - S(32), S(14), w - S(16), S(14) + S(32)};
    int y = pad + S(kMark) + S(18);  // brand row: mark + wordmark + slogan
    headerH_ = y;
    tabsRect_ = {pad, y, w - pad, y + S(kTabsH)};
    int half = (tabsRect_.right - tabsRect_.left) / 2;
    tabRects_[0] = {tabsRect_.left + S(3), tabsRect_.top + S(3), tabsRect_.left + half, tabsRect_.bottom - S(3)};
    tabRects_[1] = {tabsRect_.left + half, tabsRect_.top + S(3), tabsRect_.right - S(3), tabsRect_.bottom - S(3)};
    contentTop_ = tabsRect_.bottom + S(20);

    // 설정
    rows_.clear();
    sections_.clear();
    y = contentTop_;
    auto section = [&](int i) {
        sections_.push_back({i, {pad, y, w - pad, y + S(18)}});
        y += S(18 + 4);
    };
    auto row = [&](int hit, std::wstring label, std::wstring note, int controlW, bool orgOnly) {
        Row r{hit, std::move(label), std::move(note)};
        r.rect = {pad, y, w - pad, y + S(kRowH)};
        int ch = hit == kHitFolder ? S(30) : S(kToggleH);
        r.control = {w - pad - controlW, y + (S(kRowH) - ch) / 2, w - pad, y + (S(kRowH) - ch) / 2 + ch};
        r.organizerOnly = orgOnly;
        rows_.push_back(r);
        y += S(kRowH);
    };
    section(0);
    row(kHitAutostart, L"Windows 시작 시 자동 실행", L"켜 두면 로그인할 때 더미가 그대로 돌아와요", S(kToggleW), false);
    row(kHitShown, L"카드와 오거나이저 보이기", L"끄면 '숨기기': 집중할 때 잠시 치워 둬요", S(kToggleW), false);
    y += S(14);
    section(1);
    row(kHitOrganizer, L"오거나이저 사용", L"화면 끝 막대에 더미를 꽂아 두는 서랍", S(kToggleW), false);
    row(kHitSide, L"놓을 위치", L"막대는 끌어서 위아래·다른 모니터로도 옮겨요", 2 * S(kSideW) + S(6), true);
    sideRects_[0] = {rows_.back().control.left, rows_.back().control.top - S(4), rows_.back().control.left + S(kSideW),
                     rows_.back().control.bottom + S(4)};
    sideRects_[1] = {sideRects_[0].right + S(6), sideRects_[0].top, sideRects_[0].right + S(6) + S(kSideW), sideRects_[0].bottom};
    row(kHitSlide, L"서랍이 스르륵 열리기", L"끄면 바로 나타나요", S(kToggleW), true);
    Row mat{kHitMaterial, L"재질", L""};
    mat.rect = {pad, y, w - pad, y + S(30)};
    mat.organizerOnly = true;
    rows_.push_back(mat);
    y += S(30);
    swatches_.clear();
    int count = (int)std::size(kOrganizerMaterials);
    int sw = (w - 2 * pad - (kSwatchesPerRow - 1) * S(kSwatchGap)) / kSwatchesPerRow;
    for (int i = 0; i < count; ++i) {
        int sx = pad + (i % kSwatchesPerRow) * (sw + S(kSwatchGap)), sy = y + (i / kSwatchesPerRow) * (S(kSwatchH) + S(kSwatchGap));
        swatches_.push_back({sx, sy, sx + sw, sy + S(kSwatchH)});
    }
    y = swatches_.back().bottom + S(8);
    materialNameRect_ = {pad, y, w - pad, y + S(20)};
    y += S(20 + 14);
    section(2);
    int fw = MeasureUiText(L"폴더 열기", S(13), true).cx + S(24);
    row(kHitFolder, L"메모 폴더", L"", fw, false);
    folderButton_ = rows_.back().control;
    folderRect_ = {pad, rows_.back().rect.top + S(26), folderButton_.left - S(12), rows_.back().rect.top + S(44)};
    int settingsBottom = y;

    // 사용법
    y = contentTop_;
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
    int usageBottom = tipRect_.bottom;

    // One size for both tabs, so switching doesn't move the window.
    y = std::max(settingsBottom, usageBottom) + S(22);
    int bw = std::max(MeasureUiText(L"닫기", S(14), true).cx, MeasureUiText(L"알겠어요", S(14), true).cx) + S(40);
    buttonRect_ = {w - pad - bw, y, w - pad, y + S(40)};
    size_ = {w, buttonRect_.bottom + pad};
}

void GuideWindow::Paint(HDC dc) {
    using namespace theme;
    auto S = [this](int v) { return Scale(v, dpi_); };
    Canvas c(size_.cx, size_.cy);
    c.FillRoundRect({0, 0, size_.cx, size_.cy}, 0, kBackground);
    if (!HasDwmRoundedCorners()) c.StrokeRoundRect({0, 0, size_.cx, size_.cy}, 0, 1, kBorder);
    int pad = S(kPad), w = size_.cx;

    // Brand row: the mark, the wordmark and the slogan.
    DrawMark(c, pad, pad, S(kMark));
    int bx = pad + S(kMark) + S(12);
    SIZE ws = WordmarkSize(S(17)), sl = c.MeasureText(kSlogan, S(12), false);
    int by = pad + (S(kMark) - ws.cy - S(4) - sl.cy) / 2;
    DrawWordmark(c, bx, by, ws.cy, kForeground);
    c.Text(kSlogan, {bx, by + ws.cy + S(4), w - pad, by + ws.cy + S(4) + sl.cy}, kMutedFg, S(12), false,
           DT_LEFT | DT_TOP | DT_SINGLELINE);
    if (hover_ == kHitClose) c.FillRoundRect(closeRect_, S(kRadiusSm), kMuted);
    c.Text(L"✕", closeRect_, hover_ == kHitClose ? kForeground : kMutedFg, S(14), false, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Tabs: 설정 | 사용법
    c.FillRoundRect(tabsRect_, S(kRadius), kMuted);
    for (int i = 0; i < 2; ++i) {
        bool on = (int)tab_ == i;
        if (on) {
            c.Shadow(tabRects_[i], S(kRadiusSm), S(4), S(1), 40, kForeground);
            c.FillRoundRect(tabRects_[i], S(kRadiusSm), kBackground);
        }
        c.Text(kTabs[i], tabRects_[i], on || hover_ == kHitTab + i ? kForeground : kMutedFg, S(13), true,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (tab_ == Tab::Settings) {
        for (auto& [i, r] : sections_)
            c.Text(kSections[i], r, kMutedFg, S(12), true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        for (const Row& r : rows_) {
            bool off = r.organizerOnly && !s_.organizer;
            RECT lr{r.rect.left, r.rect.top + S(8), r.control.right > r.control.left ? r.control.left - S(12) : r.rect.right,
                    r.rect.top + S(28)};
            c.Text(r.label, lr, kForeground, S(14), true, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            if (!r.note.empty())
                c.Text(r.note, {lr.left, lr.bottom, lr.right, lr.bottom + S(18)}, kMutedFg, S(12), false,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            if (r.hit == kHitAutostart || r.hit == kHitShown || r.hit == kHitOrganizer || r.hit == kHitSlide) {
                bool on = r.hit == kHitAutostart ? s_.autostart : r.hit == kHitShown ? s_.shown
                          : r.hit == kHitOrganizer ? s_.organizer : s_.slide;
                const RECT& t = r.control;
                c.FillRoundRect(t, S(kToggleH / 2), on ? kPrimary : kBorder);
                int k = S(kToggleH - 4);
                int kx = on ? t.right - S(2) - k : t.left + S(2);
                c.Shadow({kx, t.top + S(2), kx + k, t.top + S(2) + k}, k / 2, S(2), S(1), 40, kForeground);
                c.FillRoundRect({kx, t.top + S(2), kx + k, t.top + S(2) + k}, k / 2, kBackground);
            } else if (r.hit == kHitSide) {
                for (int i = 0; i < 2; ++i) {
                    bool on = s_.right == (i == 1);
                    c.FillRoundRect(sideRects_[i], S(kRadiusSm), on ? kPrimary : hover_ == kHitSide + i ? kMuted : kBackground);
                    if (!on) c.StrokeRoundRect(sideRects_[i], S(kRadiusSm), 1, kBorder);
                    c.Text(i ? L"오른쪽" : L"왼쪽", sideRects_[i], on ? kPrimaryFg : kForeground, S(13), true,
                           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            } else if (r.hit == kHitFolder) {
                c.Text(s_.memoDir, folderRect_, kMutedFg, S(12), false, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
                c.FillRoundRect(folderButton_, S(kRadiusSm), hover_ == kHitFolder ? kMuted : kBackground);
                c.StrokeRoundRect(folderButton_, S(kRadiusSm), 1, kBorder);
                c.Text(L"폴더 열기", folderButton_, kForeground, S(13), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            if (off) c.FillRoundRect(r.rect, 0, kBackground, 160);  // the organizer is off: these wait
        }
        // Materials: real swatches, the chosen one ringed.
        int chosen = 0, shownName = -1;
        for (int i = 0; i < (int)swatches_.size(); ++i) {
            const RECT& r = swatches_[i];
            int m = MaterialFromId(kOrganizerMaterials[i].id);
            if (s_.material == kOrganizerMaterials[i].id) chosen = i;
            RECT o{r.left - S(3), r.top - S(3), r.right + S(3), r.bottom + S(3)};
            if (s_.material == kOrganizerMaterials[i].id) c.StrokeRoundRect(o, S(kRadiusSm) + S(3), S(2), kForeground);
            else if (hover_ == kHitMaterial + i) c.StrokeRoundRect(o, S(kRadiusSm) + S(3), S(2), kBorder);
            FillMaterial(c, r, S(kRadiusSm), m, dpi_);
            if (kMaterials[m].translucent) FlattenOnWhite(c, r);
            c.StrokeRoundRect(r, S(kRadiusSm), 1, kBorder);
            if (hover_ == kHitMaterial + i) shownName = i;
        }
        int ni = shownName >= 0 ? shownName : chosen;
        std::wstring name = std::wstring(kOrganizerMaterials[ni].name) + (ni ? L" · 유료 소재, 베타 동안 무료" : L" · 기본");
        c.Text(name, materialNameRect_, kMutedFg, S(12), false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (!s_.organizer) {
            RECT block{swatches_.front().left - S(4), swatches_.front().top - S(4), w - pad + S(4), materialNameRect_.bottom};
            c.FillRoundRect(block, 0, kBackground, 160);
        }
    } else {
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
    }

    c.FillRoundRect(buttonRect_, S(kRadius), hover_ == kHitButton ? kPrimaryHover : kPrimary);
    c.Text(tab_ == Tab::Usage ? L"알겠어요" : L"닫기", buttonRect_, kPrimaryFg, S(14), true, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    c.Blit(dc);
}

int GuideWindow::HitAt(POINT p) const {
    if (PtInRect(&closeRect_, p)) return kHitClose;
    if (PtInRect(&buttonRect_, p)) return kHitButton;
    for (int i = 0; i < 2; ++i)
        if (PtInRect(&tabRects_[i], p)) return kHitTab + i;
    if (tab_ != Tab::Settings) return kHitNone;
    for (const Row& r : rows_) {
        if (r.organizerOnly && !s_.organizer) continue;
        if (r.hit == kHitSide) {
            for (int i = 0; i < 2; ++i)
                if (PtInRect(&sideRects_[i], p)) return kHitSide + i;
        } else if (r.hit == kHitFolder) {
            if (PtInRect(&folderButton_, p)) return kHitFolder;
        } else if (r.hit != kHitMaterial && PtInRect(&r.rect, p)) {
            return r.hit;  // a toggle row: anywhere on it
        }
    }
    if (s_.organizer)
        for (int i = 0; i < (int)swatches_.size(); ++i)
            if (PtInRect(&swatches_[i], p)) return kHitMaterial + i;
    return kHitNone;
}

void GuideWindow::Click(int hit) {
    if (hit == kHitClose || hit == kHitButton) {
        Close();
        return;
    }
    if (hit >= kHitTab && hit < kHitTab + 2) {
        tab_ = (Tab)(hit - kHitTab);
    } else if (hit == kHitFolder) {
        ShellExecuteW(nullptr, L"open", s_.memoDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    } else if (hit) {
        AppSettings next = s_;
        if (hit == kHitAutostart) next.autostart = !next.autostart;
        else if (hit == kHitShown) next.shown = !next.shown;
        else if (hit == kHitOrganizer) next.organizer = !next.organizer;
        else if (hit == kHitSlide) next.slide = !next.slide;
        else if (hit == kHitSide || hit == kHitSide + 1) next.right = hit == kHitSide + 1;
        else if (hit >= kHitMaterial) next.material = kOrganizerMaterials[hit - kHitMaterial].id;
        set_(next);
        s_ = get_();  // as it really is now
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
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
            int h = HitAt(p);
            if (h != hover_) {
                hover_ = h;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            SetCursor(LoadCursorW(nullptr, h ? IDC_HAND : IDC_ARROW));
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE:
            hover_ = 0;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            Click(HitAt({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}));
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE || wp == VK_RETURN) Close();
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace wp
