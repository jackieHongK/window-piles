// One-shot window snapshots with Windows Graphics Capture (PoC-03), plus the icon-card fallback.
// The D3D device is created on demand and released when idle, so an idle app holds no GPU memory.
#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "common/canvas.h"

namespace wp {

class SnapshotEngine {
public:
    SnapshotEngine();
    ~SnapshotEngine();

    // Captures all given windows in parallel. Minimized/hidden windows are skipped (no frame would
    // arrive). Result[i] is empty when capture failed; the caller then uses an icon card.
    std::vector<Image> Capture(const std::vector<HWND>& windows, int targetWidth);

    // Releases the D3D device if unused for `idleMs`. Call periodically.
    void ReleaseIfIdle(double idleMs);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Card with the app icon and window title, used when a snapshot is unavailable.
Image MakeIconCard(HWND hwnd, int w, int h);

}  // namespace wp
