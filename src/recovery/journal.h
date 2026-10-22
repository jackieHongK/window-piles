// Recovery journal: every hidden window is recorded here *before* it is hidden, so the watchdog
// (or the next start) can bring it back if the manager dies. Validated in PoC-04.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wp {

struct JournalEntry {
    uint64_t hwnd = 0;
    DWORD pid = 0;
    uint64_t createTime = 0;
    std::wstring className;
    WINDOWPLACEMENT wp{sizeof(WINDOWPLACEMENT)};
};

struct Journal {
    DWORD managerPid = 0;
    bool clean = false;  // true: nothing is hidden / manager exited normally
    std::vector<JournalEntry> entries;
};

std::wstring JournalPath();
// Atomic replace (tmp file + MoveFileEx). No FlushFileBuffers: the OS cache survives a process
// crash, and an OS crash takes the hidden windows down with it (plan §3 ①, PoC-04).
bool WriteJournal(const Journal& j);
bool ReadJournal(Journal& j, std::string& err);

// Empty string when `e` still refers to the same live window of the same process instance.
std::string ValidateIdentity(const JournalEntry& e);

// Shows a window again with its saved placement, without stealing focus unless asked.
// Non-blocking: the cross-process call runs on a pool thread (a frozen app can't freeze us). A few
// restores run at a time; `urgent` (exit, logoff, crash recovery) runs them all at once instead.
void RestoreWindow(HWND hwnd, const WINDOWPLACEMENT& wp, bool activate, bool urgent = false);
// Waits (bounded) until queued RestoreWindow calls finished. Call before the process exits.
bool WaitForRestores(DWORD timeoutMs);
bool RestoresPending();

// Restores every validated entry; returns how many were restored.
int RecoverFromJournal(const Journal& j, int* skipped);

// Watchdog process body: waits on the manager handle (no CPU). After a crash it relaunches the app
// (piles come back as they were); otherwise, or if that fails, it shows the hidden windows.
int RunWatchdog(DWORD managerPid);

}  // namespace wp
