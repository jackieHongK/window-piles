#include "app/background.h"

#include <audiopolicy.h>
#include <dwmapi.h>
#include <mmdeviceapi.h>
#include <psapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>

#include "common/win_util.h"
#include "recovery/journal.h"

namespace wp {
namespace {

using Microsoft::WRL::ComPtr;

bool Contains(const std::vector<DWORD>& v, DWORD x) { return std::find(v.begin(), v.end(), x) != v.end(); }

// Processes with a window the user can get at: out on the screen, minimized to the taskbar, or on another
// virtual desktop (cloaked by the shell). Tool windows and empty ones don't count.
std::vector<DWORD> PidsInUse() {
    std::vector<DWORD> pids;
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            if (!IsWindowVisible(h) || (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) return TRUE;
            DWORD cloaked = 0;
            DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            if (cloaked & ~DWM_CLOAKED_SHELL) return TRUE;  // cloaked by its app (a suspended Store app)
            RECT r;
            if (!IsIconic(h) && (!GetWindowRect(h, &r) || r.right <= r.left || r.bottom <= r.top)) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            auto& v = *(std::vector<DWORD>*)lp;
            if (!Contains(v, pid)) v.push_back(pid);
            return TRUE;
        },
        (LPARAM)&pids);
    return pids;
}

// Processes playing sound on any output: music or a video in a put-away window keeps its full speed.
std::vector<DWORD> PidsPlayingSound() {
    std::vector<DWORD> pids;
    ComPtr<IMMDeviceEnumerator> en;
    ComPtr<IMMDeviceCollection> devices;
    UINT n = 0;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))) ||
        FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices)) || FAILED(devices->GetCount(&n)))
        return pids;
    for (UINT d = 0; d < n; ++d) {
        ComPtr<IMMDevice> dev;
        ComPtr<IAudioSessionManager2> mgr;
        ComPtr<IAudioSessionEnumerator> sessions;
        int count = 0;
        if (FAILED(devices->Item(d, &dev)) ||
            FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)mgr.GetAddressOf())) ||
            FAILED(mgr->GetSessionEnumerator(&sessions)) || FAILED(sessions->GetCount(&count)))
            continue;
        for (int i = 0; i < count; ++i) {
            ComPtr<IAudioSessionControl> s;
            ComPtr<IAudioSessionControl2> s2;
            AudioSessionState state;
            DWORD pid = 0;
            if (SUCCEEDED(sessions->GetSession(i, &s)) && SUCCEEDED(s->GetState(&state)) &&
                state == AudioSessionStateActive && SUCCEEDED(s.As(&s2)) && SUCCEEDED(s2->GetProcessId(&pid)) && pid)
                pids.push_back(pid);
        }
    }
    return pids;
}

// At work: more than 5% of a CPU core, 1 MB/s of disk or network, or 32 MB more memory since the last
// sample. Busy stays busy for a minute after the last sign of work, so a build pausing between steps
// isn't slowed down in the middle.
constexpr double kBusyCpu = 0.05, kBusyIoPerSec = 1 << 20;
constexpr int64_t kBusyGrowth = 32 << 20;
constexpr ULONGLONG kSampleMs = 2000, kQuietMs = 60000;

uint64_t Ticks(FILETIME f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; }

constexpr PROCESS_POWER_THROTTLING_STATE kPowerDefault{PROCESS_POWER_THROTTLING_CURRENT_VERSION, 0, 0};  // Windows decides

}  // namespace

bool BackgroundApps::Has(DWORD pid) const {
    return std::any_of(lowered_.begin(), lowered_.end(), [&](const Entry& e) { return e.pid == pid; });
}

bool BackgroundApps::Busy(DWORD pid, ULONGLONG now) {
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return false;  // can't tell (and it can't be lowered either)
    FILETIME c{}, e{}, k{}, u{};
    IO_COUNTERS io{};
    PROCESS_MEMORY_COUNTERS_EX mem{sizeof(mem)};
    GetProcessTimes(p, &c, &e, &k, &u);
    GetProcessIoCounters(p, &io);
    GetProcessMemoryInfo(p, (PROCESS_MEMORY_COUNTERS*)&mem, sizeof(mem));
    CloseHandle(p);
    Load now_{pid, Ticks(k) + Ticks(u), io.ReadTransferCount + io.WriteTransferCount + io.OtherTransferCount,
              mem.PrivateUsage, now};
    auto it = std::find_if(loads_.begin(), loads_.end(), [&](const Load& l) { return l.pid == pid; });
    if (it == loads_.end()) {  // just put away: busy until measured
        loads_.push_back(now_);
        return true;
    }
    if (now - it->at >= kSampleMs) {
        double ms = (double)(now - it->at);
        double cpu = (now_.cpu - it->cpu) / (ms * 10000), rate = (now_.io - it->io) * 1000 / ms;
        int64_t grew = (int64_t)now_.memory - (int64_t)it->memory;
        if (cpu > kBusyCpu || rate > kBusyIoPerSec || grew > kBusyGrowth) {
            if (now >= it->busyUntil)  // it just got to work (or was found at work)
                Log("background: pid %lu at work (cpu %.0f%%, io %.1f MB/s, memory %+lld MB): left as it is", pid,
                    cpu * 100, rate / (1 << 20), grew >> 20);
            now_.busyUntil = now + kQuietMs;
        } else {
            now_.busyUntil = it->busyUntil;
        }
        now_.measured = true;
        *it = now_;
    }
    return !it->measured || now < it->busyUntil;
}

void BackgroundApps::Update(const std::vector<DWORD>& pids) {
    ULONGLONG now = GetTickCount64();
    loads_.erase(std::remove_if(loads_.begin(), loads_.end(), [&](const Load& l) { return !Contains(pids, l.pid); }),
                 loads_.end());
    why_.erase(std::remove_if(why_.begin(), why_.end(), [&](const auto& w) { return !Contains(pids, w.first); }),
               why_.end());
    std::vector<DWORD> want, seen;
    if (!pids.empty()) {
        std::vector<DWORD> inUse = PidsInUse(), sound = PidsPlayingSound();
        for (DWORD pid : pids) {
            if (Contains(seen, pid)) continue;
            seen.push_back(pid);
            const char* why = pid == GetCurrentProcessId() ? "this app"
                              : Contains(inUse, pid)       ? "a window of it in use"
                              : Contains(sound, pid)       ? "playing sound"
                              : _wcsicmp(ProcessExeName(pid).c_str(), L"explorer.exe") == 0 ? "the shell"  // taskbar, desktop
                              : Busy(pid, now)             ? "at work or not measured yet"
                                                           : nullptr;
            if (!why) want.push_back(pid);
            // Why one is left as it is, once per change (for "why isn't it resting?").
            auto w = std::find_if(why_.begin(), why_.end(), [&](const auto& x) { return x.first == pid; });
            if (w == why_.end()) w = why_.insert(why_.end(), {pid, ""});
            if (why && std::strcmp(w->second, why) != 0) Log("background: pid %lu left as it is: %s", pid, why);
            w->second = why ? why : "";
        }
    }
    for (auto it = lowered_.begin(); it != lowered_.end();) {
        if (Contains(want, it->pid) && WaitForSingleObject(it->process, 0) == WAIT_TIMEOUT) {
            ++it;
            continue;
        }
        Restore(*it);
        Log("background: pid %lu back to its own settings", it->pid);
        CloseHandle(it->process);
        it = lowered_.erase(it);
    }
    for (DWORD pid : want) {
        if (Has(pid)) continue;
        Entry e;
        e.pid = pid;
        e.process = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
        if (!e.process) continue;  // running as administrator, or protected: left alone
        // What it had, to give back exactly (a browser may set its own).
        e.memory.MemoryPriority = MEMORY_PRIORITY_NORMAL;
        GetProcessInformation(e.process, ProcessMemoryPriority, &e.memory, sizeof(e.memory));
        e.power = kPowerDefault;
        if (!GetProcessInformation(e.process, ProcessPowerThrottling, &e.power, sizeof(e.power))) e.power = kPowerDefault;
        MEMORY_PRIORITY_INFORMATION low{MEMORY_PRIORITY_LOW};
        PROCESS_POWER_THROTTLING_STATE eco{PROCESS_POWER_THROTTLING_CURRENT_VERSION,
                                           PROCESS_POWER_THROTTLING_EXECUTION_SPEED,
                                           PROCESS_POWER_THROTTLING_EXECUTION_SPEED};
        bool memOk = SetProcessInformation(e.process, ProcessMemoryPriority, &low, sizeof(low));
        bool ecoOk = SetProcessInformation(e.process, ProcessPowerThrottling, &eco, sizeof(eco));
        Log("background: pid %lu (%ls) steps back: memory %d->%d, eco %d (had mask %lu/%lu)", pid,
            ProcessExeName(pid).c_str(), (int)e.memory.MemoryPriority, memOk ? (int)MEMORY_PRIORITY_LOW : -1, ecoOk,
            e.power.ControlMask, e.power.StateMask);
        if (!memOk && !ecoOk) {
            CloseHandle(e.process);
            continue;
        }
        lowered_.push_back(e);
    }
}

void BackgroundApps::ReleaseAll() {
    for (const Entry& e : lowered_) {
        Restore(e);
        CloseHandle(e.process);
    }
    lowered_.clear();
}

void BackgroundApps::Restore(const Entry& e) {
    if (WaitForSingleObject(e.process, 0) != WAIT_TIMEOUT) return;  // exited
    SetProcessInformation(e.process, ProcessMemoryPriority, (void*)&e.memory, sizeof(e.memory));
    SetProcessInformation(e.process, ProcessPowerThrottling, (void*)&e.power, sizeof(e.power));
}

void ResetBackgroundApps(const Journal& lastRun) {
    std::vector<DWORD> done;
    for (const JournalEntry& j : lastRun.entries) {
        if (Contains(done, j.pid) || ProcessCreateTime(j.pid) != j.createTime) continue;  // gone, or pid reused
        done.push_back(j.pid);
        HANDLE p = OpenProcess(PROCESS_SET_INFORMATION, FALSE, j.pid);
        if (!p) continue;
        MEMORY_PRIORITY_INFORMATION normal{MEMORY_PRIORITY_NORMAL};
        SetProcessInformation(p, ProcessMemoryPriority, &normal, sizeof(normal));
        SetProcessInformation(p, ProcessPowerThrottling, (void*)&kPowerDefault, sizeof(kPowerDefault));
        CloseHandle(p);
    }
    if (!done.empty()) Log("background: %zu apps reset after a crash", done.size());
}

}  // namespace wp
