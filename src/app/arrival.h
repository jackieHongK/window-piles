// "Arrival": the unboxing page hands off to the app. The page opens windowpiles://organizer/arrive?y=0.5 when the walnut
// rack touches the monitor's edge; the running app (or a fresh one) plays a pulse of light from that edge and the real
// organizer slides out, open.
#pragma once

#include <windows.h>

#include <string>

namespace wp {

// Posted to the main window: lParam unused, wParam = where it touched, as a share of the work-area height (0..1000).
constexpr UINT WM_APP_ARRIVE = WM_APP + 12;

// Registers windowpiles:// for the current user (HKCU\Software\Classes), pointing at this exe. Cheap; run at every start.
void RegisterUrlProtocol();
// "windowpiles://organizer/arrive?y=0.42" -> true and y in permille (500 when absent). False for anything else.
bool ParseArriveUrl(const std::wstring& arg, int* yPermille);

// A fresh launch from the link: main() leaves it here, the app plays it once its organizer is up (-1: none).
void SetPendingArrive(int yPermille);
int TakePendingArrive();

// The touch: a soft white swell at (edgeX, y) on the monitor holding that point, then rings of colour running across it.
// Click-through, topmost, gone by itself after ~0.9 s.
void PlayArrivalPulse(POINT touch);

}  // namespace wp
