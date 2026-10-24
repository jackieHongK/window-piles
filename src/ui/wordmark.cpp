#include "wordmark.h"

#include <cmath>
#include <iterator>
#include <vector>

#include "common/canvas.h"

namespace wp {

namespace {
#include "wordmark_data.inc"
}  // namespace

SIZE WordmarkSize(int height) {
    double k = (double)height / (kWordmarkTop - kWordmarkBottom);
    return {(LONG)std::ceil(kWordmarkWidth * k), height};
}

void DrawWordmark(Canvas& c, int x, int y, int height, COLORREF color) {
    float k = (float)height / (kWordmarkTop - kWordmarkBottom);
    const int nContours = (int)std::size(kWordmarkContours), nPoints = (int)std::size(kWordmarkPoints);
    std::vector<std::vector<Canvas::PtF>> contours(nContours);
    for (int i = 0; i < nContours; ++i) {
        int end = i + 1 < nContours ? kWordmarkContours[i + 1] : nPoints;
        for (int p = kWordmarkContours[i]; p < end; ++p)  // font units are y up from the baseline
            contours[i].push_back({x + kWordmarkPoints[p][0] * k, y + (kWordmarkTop - kWordmarkPoints[p][1]) * k});
    }
    c.FillPolygons(contours, color);
}

}  // namespace wp
