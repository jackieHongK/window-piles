// The Stack Neverflow mark, drawn in code (also baked into the exe's .ico by make_icon): a walnut tile
// with a cream U-shaped tray and cards stacked inside it, all below the rim ("tasks stack up, but never
// overflow"). Flat colors with volume (gradients, highlights, soft shadows). Icon sizes (≤ 32 px,
// incl. the tray) use two thicker cards so the colors read as surfaces, not lines.
#pragma once

#include <windows.h>

namespace wp {

class Canvas;

// Color mark drawn into `c` at (x, y), size × size px.
void DrawMark(Canvas& c, int x, int y, int size);
// Color app icon (tray, balloons, windows).
HICON MakeAppIcon(int size);

}  // namespace wp
