// Unit test for the activity log: line format and time-per-pile computation. Exit code = failures.
// File round trip uses its own folder (WINDOWPILES_DATA_DIR), never the user's records.
#include <cstdio>
#include <string>
#include <vector>

#include "activity/activity_log.h"

using namespace wp::activity;

namespace {

int g_fail = 0;

void Check(bool ok, const wchar_t* what) {
    if (!ok) ++g_fail;
    wprintf(L"[%ls] %ls\n", ok ? L"PASS" : L"FAIL", what);
}

int T(int h, int m, int s = 0) { return h * 3600 + m * 60 + s; }
Event Open(int t, int id, const wchar_t* name = L"더미") { return {t, Kind::Open, id, 0x123456, name}; }
Event Close(int t) { return {t, Kind::Close}; }
Event Pause(int t, const wchar_t* why) { return {t, Kind::Pause, 0, 0, why}; }
Event Resume(int t) { return {t, Kind::Resume}; }
Event Start(int t) { return {t, Kind::Start}; }

int Sum(const std::vector<Span>& spans, int id = -1) {
    int n = 0;
    for (auto& s : spans)
        if (id < 0 || s.pileId == id) n += s.end - s.start;
    return n;
}

}  // namespace

int wmain() {
    const int day = 24 * 3600;
    {
        auto s = ComputeDay({}, {Open(T(9, 0), 1), Open(T(10, 0), 2), Close(T(11, 30))}, day);
        Check(Sum(s, 1) == 3600 && Sum(s, 2) == 5400, L"더미 전환: A 1시간, B 1시간 30분");
    }
    {
        auto s = ComputeDay({}, {Open(T(9, 0), 1), Pause(T(10, 0), L"idle"), Resume(T(10, 20)), Close(T(11, 0))}, day);
        Check(Sum(s) == T(1, 40), L"자리 비움 20분은 빠짐");
    }
    {
        auto s = ComputeDay({}, {Open(T(9, 0), 1), Pause(T(12, 0), L"lock"), Resume(T(13, 0)), Close(T(14, 0))}, day);
        Check(Sum(s) == T(4, 0), L"화면 잠금 1시간은 빠짐");
    }
    {
        auto s = ComputeDay({}, {Open(T(9, 0), 1), Pause(T(9, 30), L"exit"), Start(T(9, 31))}, day);
        Check(Sum(s) == T(0, 30), L"크래시: 감시자가 남긴 종료 시각까지만");
    }
    {
        auto s = ComputeDay({}, {Open(T(9, 0), 1), Start(T(10, 0)), Open(T(10, 5), 2), Close(T(10, 35))}, day);
        Check(Sum(s, 1) == 0 && Sum(s, 2) == T(0, 30), L"전원 차단: 끝을 모르는 구간은 버림");
    }
    {
        std::vector<Event> prev = {Open(T(23, 0), 7)};
        Check(Sum(ComputeDay({}, prev, day)) == 3600, L"자정 전: 23시~24시");
        auto s = ComputeDay(prev, {Close(T(1, 0))}, day);
        Check(Sum(s, 7) == 3600 && !s.empty() && s[0].start == 0, L"자정을 넘긴 작업은 다음 날 00:00부터 이어짐");
    }
    {
        auto s = ComputeDay({}, {Open(T(9, 0), 1)}, T(10, 30));
        Check(Sum(s) == T(1, 30), L"오늘: 지금 펼쳐 둔 더미는 지금까지");
    }
    {
        auto s = ComputeDay({}, {Open(T(9, 0), 1), Pause(T(9, 30), L"idle"), Open(T(9, 50), 2), Close(T(10, 0))}, day);
        Check(Sum(s, 1) == T(0, 30) && Sum(s, 2) == T(0, 10), L"자리 비움 뒤 다른 더미를 펼치면 거기서 다시 셈");
    }
    {
        auto s = ComputeDay({}, {Open(T(9, 0), 1), Resume(T(9, 10)), Close(T(9, 20))}, day);
        Check(Sum(s) == T(0, 20), L"멈춘 적 없는 '다시 시작'은 무시");
    }
    {
        auto s = ComputeDay({}, {Open(T(9, 0), 1), Pause(T(9, 30), L"lock"), Close(T(9, 40)), Resume(T(10, 0))}, day);
        Check(Sum(s) == T(0, 30), L"잠긴 사이 접히면 다시 시작해도 셀 더미가 없음");
    }
    {
        auto s = ComputeDay({}, {Open(T(9, 0), 1, L"옛 이름"), Close(T(9, 10)), Open(T(9, 20), 1, L"새 이름"),
                                 Close(T(10, 0)), Open(T(10, 0), 2, L"짧은 것"), Close(T(10, 5))},
                            day);
        auto totals = Totals(s);
        Check(totals.size() == 2 && totals[0].pileId == 1 && totals[0].seconds == T(0, 50) && totals[0].name == L"새 이름",
              L"합계: 긴 순서, 최신 이름");
    }
    {
        Event e;
        bool ok = ParseLine("09:05:07\topen\t3\t337EA9\t보고서 작업", e);  // /utf-8: narrow literals are UTF-8
        Check(ok && e.sec == T(9, 5, 7) && e.kind == Kind::Open && e.pileId == 3 && e.color == 0x337EA9 &&
                  e.text == L"보고서 작업",
              L"줄 읽기");
        Check(!ParseLine("garbage", e) && !ParseLine("10:00:00\tjump\t1", e), L"잘못된 줄은 건너뜀");
    }
    {
        // File round trip in a throwaway folder next to the test.
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring dir = self;
        dir = dir.substr(0, dir.find_last_of(L'\\')) + L"\\unit-data";
        SetEnvironmentVariableW(L"WINDOWPILES_DATA_DIR", dir.c_str());
        SYSTEMTIME today = Today();
        wchar_t name[40];
        swprintf_s(name, L"\\%04d-%02d-%02d.tsv", today.wYear, today.wMonth, today.wDay);
        DeleteFileW((Folder() + name).c_str());
        SetEnabled(true);
        Append(Kind::Open, 5, 0xABCDEF, L"탭\t있는 이름");
        SetEnabled(false);
        Append(Kind::Close, 5);  // recording off: not written
        SetEnabled(true);
        auto events = ReadDay(today);
        Check(events.size() == 1 && events[0].kind == Kind::Open && events[0].pileId == 5 &&
                  events[0].text == L"탭 있는 이름",
              L"파일에 쓰고 읽기 (기록을 끄면 쓰지 않음)");
    }
    wprintf(L"\n%ls (%d failed)\n", g_fail ? L"ACTIVITY FAILED" : L"ACTIVITY PASSED", g_fail);
    return g_fail;
}
