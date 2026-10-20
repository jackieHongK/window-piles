// The app/tray icon, drawn at runtime (three stacked cards) so no resource file is needed.
#pragma once

#include <windows.h>

namespace wp {

HICON MakeAppIcon(int size);

}  // namespace wp
