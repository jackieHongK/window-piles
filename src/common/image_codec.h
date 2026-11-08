// Images in the formats Windows decodes itself (WIC: PNG, JPEG, ...): the clipboard's pictures and the
// material scans embedded in the exe.
#pragma once

#include <windows.h>
#include <wincodec.h>

#include "common/canvas.h"

namespace wp {

IWICImagingFactory* Wic();  // the process's factory, made on first use (nullptr if it can't be)
// Decodes `data` into premultiplied BGRA. False if it isn't an image WIC can read.
bool DecodeImage(const void* data, size_t size, Image& out);

}  // namespace wp
