// The Stack Neverflow mark, drawn at runtime so no resource file is needed: a walnut tile with a
// cream U-shaped tray and cards stacked inside it, all below the rim ("tasks stack up, but never
// overflow"). Small sizes (≤ 20 px) use two cards so it stays readable.
#pragma once

#include <windows.h>

namespace wp {

class Canvas;

// Color mark drawn into `c` at (x, y), size × size px.
void DrawMark(Canvas& c, int x, int y, int size);
// Color app icon (balloons, windows).
HICON MakeAppIcon(int size);
// Single-color mark for the notification area: white for a dark taskbar, near-black for a light one.
HICON MakeTrayIcon(int size, bool lightTaskbar);
// Whether the taskbar uses the light theme (HKCU ...\Themes\Personalize, SystemUsesLightTheme).
bool TaskbarIsLight();

}  // namespace wp
