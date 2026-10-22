#include "stats_window.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>

#include "common/canvas.h"
#include "common/win_util.h"
#include "ui/popup_base.h"
#include "ui/theme.h"

namespace wp {

namespace {

constexpr int kWidth = 600, kPad = 28, kRowH = 40, kMaxRows = 8, kBarH = 28, kWeekH = 112;
const wchar_t kTitle[] = L"작업 기록";
const wchar_t kNote[] = L"더미를 펼쳐 둔 시간 · 화면 잠금, 절전, 5분 넘게 자리를 비운 시간은 빼요";
const wchar_t kHint[] = L"막대에 마우스를 올리면 자세히 보여요";
const wchar_t* const kWeekdays[] = {L"일", L"월", L"화", L"수", L"목", L"금", L"토"};

std::wstring Duration(int sec) {
    if (sec <= 0) return L"0분";
    if (sec < 60) return L"1분 미만";
    int h = sec / 3600, m = sec % 3600 / 60;
    if (!h) return std::to_wstring(m) + L"분";
    if (!m) return std::to_wstring(h) + L"시간";
    return std::to_wstring(h) + L"시간 " + std::to_wstring(m) + L"분";
}

std::wstring ShortDuration(int sec) {
    if (sec < 60) return L"1분 미만";
    if (sec < 3600) return std::to_wstring(sec / 60) + L"분";
    wchar_t b[24];
    swprintf_s(b, L"%.1f시간", sec / 3600.0);
    return b;
}

std::wstring Clock(int sec) {
    wchar_t b[8];
    swprintf_s(b, L"%02d:%02d", sec / 3600, sec / 60 % 60);
    return b;
}

std::wstring DateText(const SYSTEMTIME& d) {
    return std::to_wstring(d.wMonth) + L"월 " + std::to_wstring(d.wDay) + L"일 (" + kWeekdays[d.wDayOfWeek % 7] + L")";
}

}  // namespace

StatsWindow::StatsWindow(Lookup lookup, std::function<void(bool)> setRecording)
    : lookup_(std::move(lookup)), setRecording_(std::move(setRecording)) {}

StatsWindow::~StatsWindow() { Close(); }

void StatsWindow::Close() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

StatsWindow::Day StatsWindow::Load(const SYSTEMTIME& date) {
    Day d;
    d.date = date;
    int end = 24 * 3600;
    if (activity::SameDay(date, today_)) {
        SYSTEMTIME now;
        GetLocalTime(&now);
        end = now.wHour * 3600 + now.wMinute * 60 + now.wSecond;  // a pile open right now counts until now
    }
    d.spans = activity::ComputeDay(activity::ReadDay(activity::AddDays(date, -1)), activity::ReadDay(date), end);
    for (auto& s : d.spans) {
        std::wstring name;
        COLORREF color;
        if (lookup_ && lookup_(s.pileId, name, color)) s.name = name, s.color = color;
    }
    d.totals = activity::Totals(d.spans);
    for (auto& t : d.totals) d.total += t.seconds;
    return d;
}

void StatsWindow::LoadAll() {
    today_ = activity::Today();
    week_.clear();
    for (int i = 6; i >= 0; --i) week_.push_back(Load(activity::AddDays(today_, -i)));
}

void StatsWindow::Select(const SYSTEMTIME& date) {
    hoverSpan_ = -1;
    auto it = std::find_if(week_.begin(), week_.end(), [&](const Day& d) { return activity::SameDay(d.date, date); });
    day_ = it != week_.end() ? *it : Load(date);
    if (!hwnd_) return;
    Layout();
    SetWindowPos(hwnd_, nullptr, 0, 0, size_.cx, size_.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void StatsWindow::Show() {
    LoadAll();
    if (hwnd_) {
        Select(today_);
        SetForegroundWindow(hwnd_);
        return;
    }
    day_ = week_.back();
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    POINT center{(work.left + work.right) / 2, (work.top + work.bottom) / 2};
    dpi_ = DpiForPoint(center);
    Layout();
    int x = center.x - size_.cx / 2, y = std::max<int>(work.top, center.y - size_.cy / 2);
    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, RegisterPopupClass(L"WindowPilesStats", &StatsWindow::Proc),
                            kTitle, WS_POPUP, x, y, size_.cx, size_.cy, nullptr, nullptr, GetModuleHandleW(nullptr),
                            nullptr);
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, (LONG_PTR)this);
    ApplyRoundedCorners(hwnd_);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void StatsWindow::Layout() {
    auto S = [this](int v) { return Scale(v, dpi_); };
    int w = S(kWidth), pad = S(kPad);
    closeRect_ = {w - S(16) - S(32), S(14), w - S(16), S(14) + S(32)};
    int y = pad + MeasureUiText(kTitle, S(20), true).cy + S(12);

    bool isToday = activity::SameDay(day_.date, today_);
    std::wstring date = DateText(day_.date) + (isToday ? L" · 오늘" : L"");
    prevRect_ = {pad - S(6), y, pad - S(6) + S(32), y + S(32)};
    int dw = MeasureUiText(date, S(14), true).cx;
    dateRect_ = {prevRect_.right + S(4), y, prevRect_.right + S(4) + dw, y + S(32)};
    nextRect_ = {dateRect_.right + S(4), y, dateRect_.right + S(4) + S(32), y + S(32)};
    y += S(32);
    headerH_ = y;
    y += S(16);

    int th = MeasureUiText(L"0시간", S(28), true).cy;
    totalRect_ = {pad, y, w - pad, y + th};
    y += th + S(2);
    int nh = MeasureUiText(kNote, S(12), false, w - 2 * pad).cy;
    totalNoteRect_ = {pad, y, w - pad, y + nh};
    y += nh + S(22);

    int lh = MeasureUiText(L"하루 흐름", S(13), true).cy;
    rows_.clear();
    moreRect_ = emptyRect_ = timeline_ = {};
    if (day_.spans.empty()) {
        emptyRect_ = {pad, y, w - pad, y + S(88)};
        y = emptyRect_.bottom + S(24);
    } else {
        timelineLabel_ = {pad, y, w - pad, y + lh};
        y += lh + S(10);
        timeline_ = {pad, y, w - pad, y + S(kBarH)};
        y += S(kBarH) + S(6);
        int tickH = MeasureUiText(L"09", S(11), false).cy;
        ticksRect_ = {pad, y, w - pad, y + tickH};
        y += tickH + S(6);
        int ih = MeasureUiText(kHint, S(12), false).cy;
        infoRect_ = {pad, y, w - pad, y + ih};
        y += ih + S(22);
        listLabel_ = {pad, y, w - pad, y + lh};
        y += lh + S(6);
        int rows = std::min<int>((int)day_.totals.size(), kMaxRows);
        for (int i = 0; i < rows; ++i, y += S(kRowH)) rows_.push_back({pad, y, w - pad, y + S(kRowH)});
        if ((int)day_.totals.size() > kMaxRows) {
            moreRect_ = {pad, y, w - pad, y + S(24)};
            y += S(24);
        }
        y += S(18);
    }

    weekLabel_ = {pad, y, w - pad, y + lh};
    y += lh + S(10);
    weekArea_ = {pad, y, w - pad, y + S(kWeekH)};
    weekCols_.clear();
    int colW = (w - 2 * pad) / 7;
    for (int i = 0; i < 7; ++i) weekCols_.push_back({pad + i * colW, y, pad + (i + 1) * colW, y + S(kWeekH)});
    y += S(kWeekH) + S(36);

    int bw = MeasureUiText(L"닫기", S(14), true).cx + S(40);
    buttonRect_ = {w - pad - bw, y, w - pad, y + S(38)};
    std::wstring toggle = activity::Enabled() ? L"기록 끄기" : L"기록 켜기";
    int tw = MeasureUiText(toggle, S(13), false).cx + S(20);
    toggleRect_ = {pad - S(10), y, pad - S(10) + tw, y + S(38)};
    int fw = MeasureUiText(L"기록 폴더 열기", S(13), false).cx + S(20);
    folderRect_ = {toggleRect_.right + S(2), y, toggleRect_.right + S(2) + fw, y + S(38)};
    y = buttonRect_.bottom + S(22);
    size_ = {w, y};
}

void StatsWindow::TimelineRange(int& from, int& to) const {
    from = 9 * 3600;
    to = 18 * 3600;
    if (day_.spans.empty()) return;
    from = day_.spans.front().start / 3600 * 3600;
    to = (day_.spans.back().end + 3599) / 3600 * 3600;
    if (to - from < 4 * 3600) to = std::min(24 * 3600, from + 4 * 3600);
    if (to - from < 4 * 3600) from = std::max(0, to - 4 * 3600);
}

int StatsWindow::SpanAt(POINT p) const {
    RECT hit = timeline_;
    InflateRect(&hit, 0, Scale(4, dpi_));
    if (day_.spans.empty() || !PtInRect(&hit, p)) return -1;
    int from, to;
    TimelineRange(from, to);
    int width = std::max<int>(1, timeline_.right - timeline_.left);
    int slack = (to - from) * Scale(2, dpi_) / width;  // thin stretches are still easy to point at
    int t = from + (int)((long long)(p.x - timeline_.left) * (to - from) / width);
    for (size_t i = 0; i < day_.spans.size(); ++i)
        if (t >= day_.spans[i].start - slack && t <= day_.spans[i].end + slack) return (int)i;
    return -1;
}

int StatsWindow::WeekDayAt(POINT p) const {
    for (size_t i = 0; i < weekCols_.size(); ++i)
        if (PtInRect(&weekCols_[i], p)) return (int)i;
    return -1;
}

void StatsWindow::Paint(HDC dc) {
    using namespace theme;
    auto S = [this](int v) { return Scale(v, dpi_); };
    Canvas c(size_.cx, size_.cy);
    c.FillRoundRect({0, 0, size_.cx, size_.cy}, 0, kBackground);
    if (!HasDwmRoundedCorners()) c.StrokeRoundRect({0, 0, size_.cx, size_.cy}, 0, 1, kBorder);
    int pad = S(kPad), w = size_.cx;
    const DWORD one = DT_SINGLELINE | DT_VCENTER;

    c.Text(kTitle, {pad, pad, w - pad, pad + S(30)}, kForeground, S(20), true, DT_LEFT | DT_TOP | DT_SINGLELINE);
    if (hot_ == Hot::Close) c.FillRoundRect(closeRect_, S(kRadiusSm), kMuted);
    c.Text(L"✕", closeRect_, hot_ == Hot::Close ? kForeground : kMutedFg, S(14), false, DT_CENTER | one);

    // date navigation
    bool isToday = activity::SameDay(day_.date, today_);
    if (hot_ == Hot::Prev) c.FillRoundRect(prevRect_, S(kRadiusSm), kMuted);
    c.Text(L"‹", prevRect_, kForeground, S(20), true, DT_CENTER | one);
    c.Text(DateText(day_.date) + (isToday ? L" · 오늘" : L""), dateRect_, kForeground, S(14), true, DT_LEFT | one);
    if (hot_ == Hot::Next && !isToday) c.FillRoundRect(nextRect_, S(kRadiusSm), kMuted);
    c.Text(L"›", nextRect_, isToday ? kBorder : kForeground, S(20), true, DT_CENTER | one);

    c.Text(Duration(day_.total), totalRect_, kForeground, S(28), true, DT_LEFT | DT_TOP | DT_SINGLELINE);
    c.Text(kNote, totalNoteRect_, kMutedFg, S(12), false, DT_LEFT | DT_TOP | DT_WORDBREAK);

    if (day_.spans.empty()) {
        c.FillRoundRect(emptyRect_, S(kRadius), kMuted);
        std::wstring msg = !activity::Enabled() ? L"기록이 꺼져 있어요. 아래 '기록 켜기'를 누르면 다시 쌓여요."
                           : isToday         ? L"아직 기록이 없어요. 더미를 펼쳐서 작업하면 그 시간이 여기에 쌓여요."
                                             : L"이 날은 기록이 없어요.";
        RECT tr = emptyRect_;
        InflateRect(&tr, -S(18), 0);
        c.Text(msg, tr, kSubtleFg, S(13), false, DT_CENTER | one | DT_END_ELLIPSIS);
    } else {
        // timeline: when each pile was open
        int from, to;
        TimelineRange(from, to);
        int tw = timeline_.right - timeline_.left;
        auto X = [&](int t) { return (int)(timeline_.left + (long long)(t - from) * tw / std::max(1, to - from)); };
        c.Text(L"하루 흐름", timelineLabel_, kForeground, S(13), true, DT_LEFT | DT_TOP | DT_SINGLELINE);
        c.FillRoundRect(timeline_, S(kRadiusSm), kMuted);
        for (size_t i = 0; i < day_.spans.size(); ++i) {
            const auto& sp = day_.spans[i];
            int x0 = X(sp.start), x1 = std::max(x0 + S(2), X(sp.end));
            RECT seg{x0, timeline_.top, std::min<int>(x1, timeline_.right), timeline_.bottom};
            c.FillRoundRect(seg, S(2), sp.color, hoverSpan_ < 0 || hoverSpan_ == (int)i ? 255 : 110);
        }
        int hours = (to - from) / 3600, step = hours <= 6 ? 1 : hours <= 12 ? 2 : 3;
        for (int h = from / 3600; h <= to / 3600; h += step) {
            int x = X(h * 3600);
            RECT tr{x - S(20), ticksRect_.top, x + S(20), ticksRect_.bottom};
            UINT align = DT_CENTER;
            if (h == from / 3600) tr = {x, ticksRect_.top, x + S(40), ticksRect_.bottom}, align = DT_LEFT;
            else if (h + step > to / 3600) tr = {x - S(40), ticksRect_.top, x, ticksRect_.bottom}, align = DT_RIGHT;
            wchar_t b[8];
            swprintf_s(b, L"%02d시", h);
            c.Text(b, tr, kMutedFg, S(11), false, align | DT_TOP | DT_SINGLELINE);
        }
        if (hoverSpan_ >= 0 && hoverSpan_ < (int)day_.spans.size()) {
            const auto& sp = day_.spans[hoverSpan_];
            c.Text(sp.name + L"  ·  " + Clock(sp.start) + L" ~ " + Clock(sp.end) + L"  ·  " + Duration(sp.end - sp.start),
                   infoRect_, kForeground, S(12), true, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            c.Text(kHint, infoRect_, kMutedFg, S(12), false, DT_LEFT | DT_TOP | DT_SINGLELINE);
        }

        // totals per pile
        c.Text(L"더미별", listLabel_, kForeground, S(13), true, DT_LEFT | DT_TOP | DT_SINGLELINE);
        int longest = std::max(1, day_.totals.front().seconds);
        for (size_t i = 0; i < rows_.size(); ++i) {
            const auto& t = day_.totals[i];
            const RECT& r = rows_[i];
            int cy = r.top + S(14);
            c.FillRoundRect({r.left, cy - S(5), r.left + S(10), cy + S(5)}, S(5), t.color);
            std::wstring dur = Duration(t.seconds);
            int dw = c.MeasureText(dur, S(13), true).cx;
            c.Text(t.name, {r.left + S(20), r.top, r.right - dw - S(16), cy + S(10)}, kForeground, S(13), false,
                   DT_LEFT | one | DT_END_ELLIPSIS);
            c.Text(dur, {r.right - dw - S(4), r.top, r.right, cy + S(10)}, kForeground, S(13), true, DT_RIGHT | one);
            RECT track{r.left + S(20), r.bottom - S(12), r.right, r.bottom - S(8)};
            c.FillRoundRect(track, S(2), kMuted);
            int fill = (int)((long long)(track.right - track.left) * t.seconds / longest);
            if (fill > 0) c.FillRoundRect({track.left, track.top, track.left + std::max(S(4), fill), track.bottom}, S(2), t.color);
        }
        if (!IsRectEmpty(&moreRect_)) {
            int rest = 0;
            for (size_t i = rows_.size(); i < day_.totals.size(); ++i) rest += day_.totals[i].seconds;
            c.Text(L"외 " + std::to_wstring(day_.totals.size() - rows_.size()) + L"개 더미 · " + Duration(rest), moreRect_,
                   kMutedFg, S(12), false, DT_LEFT | one);
        }
    }

    // the last 7 days: stacked by pile color, click a day to see it
    c.Text(L"최근 7일", weekLabel_, kForeground, S(13), true, DT_LEFT | DT_TOP | DT_SINGLELINE);
    int most = 3600;
    for (auto& d : week_) most = std::max(most, d.total);
    for (size_t i = 0; i < weekCols_.size() && i < week_.size(); ++i) {
        const Day& d = week_[i];
        RECT col = weekCols_[i];
        bool sel = activity::SameDay(d.date, day_.date);
        RECT bg = col;
        InflateRect(&bg, -S(3), 0);
        if (sel) c.FillRoundRect(bg, S(kRadius), kMuted);
        else if (hoverDay_ == (int)i) c.FillRoundRect(bg, S(kRadius), RGB(250, 250, 250));
        RECT label{col.left, col.bottom - S(26), col.right, col.bottom - S(4)};
        std::wstring dl = std::wstring(kWeekdays[d.date.wDayOfWeek % 7]) + L" " + std::to_wstring(d.date.wDay);
        c.Text(dl, label, sel ? kForeground : kMutedFg, S(12), sel, DT_CENTER | one);
        int top = col.top + S(22), bottom = col.bottom - S(30);
        int cx = (col.left + col.right) / 2, bw = S(12);
        if (d.total <= 0) {
            c.FillRoundRect({cx - bw, bottom - S(2), cx + bw, bottom}, S(1), kBorder);
            continue;
        }
        int h = std::max(S(3), (int)((long long)(bottom - top) * d.total / most));
        c.Text(ShortDuration(d.total), {col.left, bottom - h - S(20), col.right, bottom - h - S(4)}, kSubtleFg, S(11),
               false, DT_CENTER | one);
        int y = bottom;
        for (const auto& t : d.totals) {  // longest at the bottom
            int sh = (int)((long long)h * t.seconds / d.total);
            if (sh <= 0) continue;
            c.FillRoundRect({cx - bw, y - sh, cx + bw, y}, S(2), t.color);
            y -= sh;
        }
    }

    // footer
    c.FillRoundRect({pad, buttonRect_.top - S(16), w - pad, buttonRect_.top - S(15)}, 0, kBorder);
    if (hot_ == Hot::Toggle) c.FillRoundRect(toggleRect_, S(kRadiusSm), kMuted);
    c.Text(activity::Enabled() ? L"기록 끄기" : L"기록 켜기", toggleRect_, kSubtleFg, S(13), false, DT_CENTER | one);
    if (hot_ == Hot::Folder) c.FillRoundRect(folderRect_, S(kRadiusSm), kMuted);
    c.Text(L"기록 폴더 열기", folderRect_, kSubtleFg, S(13), false, DT_CENTER | one);
    c.FillRoundRect(buttonRect_, S(kRadius), hot_ == Hot::Button ? RGB(39, 39, 42) : kPrimary);
    c.Text(L"닫기", buttonRect_, kPrimaryFg, S(14), true, DT_CENTER | one);
    c.Blit(dc);
}

LRESULT CALLBACK StatsWindow::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = (StatsWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    return self->Handle(msg, wp, lp);
}

LRESULT StatsWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
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
            bool button = PtInRect(&closeRect_, p) || PtInRect(&prevRect_, p) || PtInRect(&nextRect_, p);
            if (p.y < headerH_ && !button) return HTCAPTION;  // drag by the header
            return HTCLIENT;
        }
        case WM_MOUSEMOVE: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            Hot hot = PtInRect(&closeRect_, p)    ? Hot::Close
                      : PtInRect(&prevRect_, p)   ? Hot::Prev
                      : PtInRect(&nextRect_, p)   ? Hot::Next
                      : PtInRect(&toggleRect_, p) ? Hot::Toggle
                      : PtInRect(&folderRect_, p) ? Hot::Folder
                      : PtInRect(&buttonRect_, p) ? Hot::Button
                                                  : Hot::None;
            int span = SpanAt(p), day = WeekDayAt(p);
            if (hot != hot_ || span != hoverSpan_ || day != hoverDay_) {
                hot_ = hot;
                hoverSpan_ = span;
                hoverDay_ = day;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            bool clickable = (hot != Hot::None && !(hot == Hot::Next && activity::SameDay(day_.date, today_))) || day >= 0;
            SetCursor(LoadCursorW(nullptr, clickable ? IDC_HAND : IDC_ARROW));
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE:
            hot_ = Hot::None;
            hoverSpan_ = hoverDay_ = -1;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (PtInRect(&closeRect_, p) || PtInRect(&buttonRect_, p)) {
                Close();
            } else if (PtInRect(&prevRect_, p)) {
                Select(activity::AddDays(day_.date, -1));
            } else if (PtInRect(&nextRect_, p)) {
                if (!activity::SameDay(day_.date, today_)) Select(activity::AddDays(day_.date, 1));
            } else if (PtInRect(&toggleRect_, p)) {
                if (setRecording_) setRecording_(!activity::Enabled());
                SYSTEMTIME keep = day_.date;
                LoadAll();
                Select(keep);
            } else if (PtInRect(&folderRect_, p)) {
                ShellExecuteW(nullptr, L"open", activity::Folder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else if (int d = WeekDayAt(p); d >= 0 && d < (int)week_.size()) {
                Select(week_[d].date);
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE || wp == VK_RETURN) Close();
            else if (wp == VK_LEFT) Select(activity::AddDays(day_.date, -1));
            else if (wp == VK_RIGHT && !activity::SameDay(day_.date, today_)) Select(activity::AddDays(day_.date, 1));
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace wp
