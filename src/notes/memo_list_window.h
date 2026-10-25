// Every memo at once ("메모 N개 더 보기" in the organizer): a calm list (flag color, title, first line,
// date) to open any of them from, and the folder they live in, opened in Explorer.
#pragma once

#include <windows.h>

namespace wp {

void ShowMemoList(POINT anchor);  // near `anchor` (screen); brought forward if already open

}  // namespace wp
