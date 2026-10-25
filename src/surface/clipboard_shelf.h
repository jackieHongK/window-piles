// The organizer's clipboard shelf: what was copied lately (text and images, the newest five), kept in
// memory only, never on disk. Content an app marks as private (password managers: "don't keep in
// clipboard history" / "don't monitor") is skipped. A click copies an item again; dragged out of the tray
// it is dropped into the app under the pointer like any drag and drop (text, or an image with a PNG file
// for apps that take files).
#pragma once

#include <windows.h>

#include <string>

#include "common/canvas.h"

namespace wp {

struct ClipItem {
    std::wstring text;  // a text item, or
    Image image;        // an image (premultiplied BGRA, at most 2048 px on a side)
    bool alpha = false;  // the image has transparent parts
    bool empty() const { return text.empty() && image.empty(); }
    bool Same(const ClipItem& o) const;
};

constexpr int kClipMax = 5;

// What the clipboard holds now, if it's text or an image that may be kept. False otherwise.
bool ReadClipboard(HWND owner, ClipItem& out);
void CopyToClipboard(HWND owner, const ClipItem& item);
// Starts dragging `item` out (call while the mouse button is still down): on its own thread, as OLE drag
// and drop needs one; the drop target gets it as if it came from any other app.
void DragClipOut(const ClipItem& item);
// The item as a w x h picture for its tile: images fill it (cropped to fit), transparent ones keep their
// transparency and fit inside.
Image ClipThumb(const ClipItem& item, int w, int h);

}  // namespace wp
