// Unit test for ParseUnread (window-title unread counts). Exit code = number of failures.
#include <cstdio>
#include <string>

#include "groups/unread.h"

namespace {
int g_fail = 0;
void Expect(const std::wstring& title, int count, bool marker) {
    wp::UnreadInfo u = wp::ParseUnread(title);
    bool ok = u.count == count && u.marker == marker;
    if (!ok) ++g_fail;
    wprintf(L"[%ls] \"%ls\" -> count=%d marker=%d (expected %d/%d)\n", ok ? L"PASS" : L"FAIL", title.c_str(), u.count,
            (int)u.marker, count, (int)marker);
}
}  // namespace

int wmain() {
    // web convention: leading (N) / [N]
    Expect(L"(3) Discord", 3, false);
    Expect(L"  [12] Chat | Microsoft Teams", 12, false);
    Expect(L"(99+) Facebook", 99, false);
    // mailboxes
    Expect(L"Inbox (5) - me@example.com - Gmail", 5, false);
    Expect(L"받은편지함 (7) - 네이버 메일", 7, false);
    Expect(L"받은 편지함 (1,234)", 1234, false);
    // "N new" / "N unread"
    Expect(L"Telegram - 4 new messages", 4, false);
    Expect(L"Mail — 12 unread", 12, false);
    // Slack-style markers
    Expect(L"! general | Acme - Slack", 0, true);
    Expect(L"* random | Acme - Slack", 0, true);
    // no false positives
    Expect(L"보고서 (1).docx - Word", 0, false);
    Expect(L"Q3 2026 매출 (최종) - Excel", 0, false);
    Expect(L"Release v1.2.0 notes", 0, false);
    Expect(L"Win 11 setup", 0, false);
    Expect(L"", 0, false);
    wprintf(L"\n%ls (%d failed)\n", g_fail ? L"UNREAD FAILED" : L"UNREAD PASSED", g_fail);
    return g_fail;
}
