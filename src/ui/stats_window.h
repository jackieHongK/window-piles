// "작업 기록": time spent per pile for a day — a timeline, totals per pile and the last 7 days.
// Reads the activity log only while it is open; nothing runs in the background for it.
#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "activity/activity_log.h"

namespace wp {

class StatsWindow {
public:
    // lookup: current name/color of a pile that still exists (renamed piles show their new name).
    using Lookup = std::function<bool(int id, std::wstring& name, COLORREF& color)>;
    StatsWindow(Lookup lookup, std::function<void(bool on)> setRecording);
    ~StatsWindow();
    void Show();
    void Close();

private:
    struct Day {
        SYSTEMTIME date{};
        std::vector<activity::Span> spans;
        std::vector<activity::Total> totals;
        int total = 0;
    };

    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    Day Load(const SYSTEMTIME& date);
    void LoadAll();
    void Select(const SYSTEMTIME& date);
    void Layout();
    void Paint(HDC dc);
    int SpanAt(POINT p) const;
    int WeekDayAt(POINT p) const;
    void TimelineRange(int& from, int& to) const;

    Lookup lookup_;
    std::function<void(bool)> setRecording_;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    SIZE size_{};
    Day day_;                // selected day
    std::vector<Day> week_;  // the last 7 days, oldest first
    SYSTEMTIME today_{};

    // layout (client px)
    int headerH_ = 0;
    RECT closeRect_{}, prevRect_{}, nextRect_{}, dateRect_{}, totalRect_{}, totalNoteRect_{};
    RECT timelineLabel_{}, timeline_{}, ticksRect_{}, infoRect_{};
    RECT listLabel_{};
    std::vector<RECT> rows_;
    RECT moreRect_{}, emptyRect_{};
    RECT weekLabel_{}, weekArea_{};
    std::vector<RECT> weekCols_;
    RECT toggleRect_{}, folderRect_{}, buttonRect_{};

    // hover
    int hoverSpan_ = -1, hoverDay_ = -1;
    enum class Hot { None, Close, Prev, Next, Toggle, Folder, Button } hot_ = Hot::None;
};

}  // namespace wp
