// Persists piles (names, colors, positions, folded state, member identities, missing windows) to
// %LOCALAPPDATA%\WindowPiles\piles.json, so piles survive an app restart, an update and a reboot.
// Window snapshots are never written (privacy, spec §5.3).
#pragma once

#include <string>
#include <vector>

#include "groups/pile.h"

namespace wp {

struct SavedMember {
    uint64_t hwnd = 0;
    DWORD pid = 0;
    uint64_t createTime = 0;
    std::wstring className, exePath, title;
    WINDOWPLACEMENT wp{sizeof(WINDOWPLACEMENT)};
    bool hidden = false;
    uint64_t stamp = 0;  // stacking order when put away (Member::stamp)
};

struct SavedPile {
    int id = 0;
    std::wstring name;
    bool customName = false;
    COLORREF color = 0;
    int material = 0;
    bool docked = false;
    POINT pos{};
    bool folded = true;
    std::vector<SavedMember> members;
    std::vector<MissingWindow> missing;
    std::vector<std::wstring> files;
};

std::wstring PileStatePath();
// Durable write (flushed): this file must survive a forced power-off. `background`: queue the write
// on a pool thread (newest state wins); otherwise write now, replacing anything still queued.
bool SavePileState(const std::vector<SavedPile>& piles, bool background = false);
bool LoadPileState(std::vector<SavedPile>& piles);

}  // namespace wp
