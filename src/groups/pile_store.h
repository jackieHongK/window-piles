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
};

struct SavedPile {
    int id = 0;
    std::wstring name;
    bool customName = false;
    COLORREF color = 0;
    POINT pos{};
    bool folded = true;
    std::vector<SavedMember> members;
    std::vector<MissingWindow> missing;
};

std::wstring PileStatePath();
// Durable write (flushed): this file must survive a forced power-off.
bool SavePileState(const std::vector<SavedPile>& piles);
bool LoadPileState(std::vector<SavedPile>& piles);

}  // namespace wp
