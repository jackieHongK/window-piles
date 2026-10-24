// Shared bits for our normal (non-layered, activatable) popups: menu, guide, pile editor.
// They need real keyboard focus and child controls, so they are regular WS_POPUP windows with the
// system drop shadow and (on Windows 11) DWM rounded corners, painted through a Canvas.
#pragma once

#include <windows.h>

namespace wp {

// Registers a popup window class once (CS_DROPSHADOW). Returns the class name.
const wchar_t* RegisterPopupClass(const wchar_t* name, WNDPROC proc);
// Rounded corners on Windows 11 (no-op on Windows 10).
void ApplyRoundedCorners(HWND hwnd, bool compact = false);  // ("small" is a macro in rpcndr.h)
bool HasDwmRoundedCorners();
// Keeps rect inside the work area of the monitor containing `anchor`.
RECT ClampToWorkArea(RECT r, POINT anchor);
// Message-loop filter for a pile-name EDIT: Ctrl+A selects all (the classic EDIT ignores it and
// beeps), and the first click into the field selects all unless it was a drag-selection, like an
// address bar. Returns true if the message was handled (skip Translate/Dispatch).
bool FilterNameEdit(const MSG& msg, HWND edit, bool& firstClick);

}  // namespace wp
