// Apps whose every window is put away in piles step back: Windows reclaims their memory first when RAM
// runs short (low memory priority) and runs them on efficient cores at low clocks (EcoQoS, the same as
// Task Manager's "efficiency mode"). Nothing is freed or suspended: an app comes back as it was, and gets
// its own settings back as soon as one of its windows is out again (or this app exits). An app at work in
// the background (a build, a render, a download: CPU, disk or network, memory growing) is left as it is
// until it has been quiet for a minute.
#pragma once

#include <windows.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace wp {

struct Journal;

class BackgroundApps {
public:
    ~BackgroundApps() { ReleaseAll(); }
    // `pids`: processes owning a window hidden in a pile. Those with no other window in use (out on the
    // screen, minimized to the taskbar, on another desktop), not playing sound and not busy step back; any
    // other that had stepped back gets its settings back. Cheap: one EnumWindows plus the changes. Call it
    // every few seconds too: how busy an app is, is measured between calls (a new one is measured first).
    void Update(const std::vector<DWORD>& pids);
    void ReleaseAll();
    bool Has(DWORD pid) const;

private:
    struct Entry {
        DWORD pid = 0;
        HANDLE process = nullptr;  // held while lowered: the settings go back to this very process
        MEMORY_PRIORITY_INFORMATION memory{};
        PROCESS_POWER_THROTTLING_STATE power{};
    };
    struct Load {  // the last sample of a process's work
        DWORD pid = 0;
        uint64_t cpu = 0, io = 0, memory = 0;  // CPU time (100 ns), bytes read/written, private bytes
        ULONGLONG at = 0, busyUntil = 0;
        bool measured = false;
    };
    static void Restore(const Entry& e);
    bool Busy(DWORD pid, ULONGLONG now);
    std::vector<Entry> lowered_;
    std::vector<Load> loads_;
    std::vector<std::pair<DWORD, const char*>> why_;  // why a hidden one was last left as it is ("" if not)
};

// After a crash (ours): the apps the last run had hidden windows of get the default settings back, since
// what they had before is gone with it. Before anything steps back again.
void ResetBackgroundApps(const Journal& lastRun);

}  // namespace wp
