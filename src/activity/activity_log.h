// Time spent per pile ("작업 기록"): which pile was open, from when to when.
//
// Writing is append-only, one small line per event, one file per day:
//   %LOCALAPPDATA%\WindowPiles\activity\2026-10-21.tsv
//   HH:MM:SS <tab> open|close|pause|resume|start <tab> pile id <tab> color <tab> pile name / pause reason
// Nothing is polled and nothing is kept open; a day is a few KB. Only pile names and times are
// written — never window titles or contents. Reading happens only when the stats window opens.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wp::activity {

enum class Kind { Start, Open, Close, Pause, Resume };

struct Event {
    int sec = 0;  // seconds since local midnight
    Kind kind = Kind::Start;
    int pileId = 0;
    COLORREF color = 0;
    std::wstring text;  // pile name (open) or reason (pause: lock, sleep, idle, exit, off)
};

bool Enabled();  // the user can turn recording off in the stats window
void SetEnabled(bool on);
std::wstring Folder();

// Appends one event to the file of the day it happened on. `utc` is a FILETIME value (0 = now).
void Append(Kind kind, int pileId = 0, COLORREF color = 0, const std::wstring& text = {}, uint64_t utc = 0);
uint64_t NowUtc();

// Days are SYSTEMTIME values in local time (wYear/wMonth/wDay; wDayOfWeek is filled in).
SYSTEMTIME Today();
SYSTEMTIME AddDays(const SYSTEMTIME& day, int days);
bool SameDay(const SYSTEMTIME& a, const SYSTEMTIME& b);

std::vector<Event> ReadDay(const SYSTEMTIME& day);  // in time order
bool ParseLine(const std::string& utf8, Event& out);

// One stretch of time a pile was open (seconds since midnight of that day).
struct Span {
    int pileId = 0;
    COLORREF color = 0;
    std::wstring name;
    int start = 0, end = 0;
};
// A day's spans from its events, starting in the state the previous day ended in (a pile left
// open over midnight). The last open stretch runs until `endSec` (now for today, else midnight).
std::vector<Span> ComputeDay(const std::vector<Event>& previousDay, const std::vector<Event>& day, int endSec);

struct Total {
    int pileId = 0;
    COLORREF color = 0;
    std::wstring name;
    int seconds = 0;
};
std::vector<Total> Totals(const std::vector<Span>& spans);  // longest first

}  // namespace wp::activity
