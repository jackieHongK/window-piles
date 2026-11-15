#include "wordmark.h"

#include <cmath>
#include <iterator>
#include <vector>

#include "common/canvas.h"

namespace wp {

namespace {
#include "sideck_data.inc"
#include "wordmark_data.inc"

struct Data {
    int width, top, bottom;
    const int* contours;
    int nContours;
    const short (*points)[2];
    int nPoints;
};

Data Get(Mark m) {
    if (m == Mark::Sideck)
        return {kSideckWidth, kSideckTop, kSideckBottom, kSideckContours, (int)std::size(kSideckContours), kSideckPoints,
                (int)std::size(kSideckPoints)};
    return {kWordmarkWidth, kWordmarkTop, kWordmarkBottom, kWordmarkContours, (int)std::size(kWordmarkContours),
            kWordmarkPoints, (int)std::size(kWordmarkPoints)};
}
}  // namespace

SIZE WordmarkSize(int height, Mark m) {
    Data d = Get(m);
    double k = (double)height / (d.top - d.bottom);
    return {(LONG)std::ceil(d.width * k), height};
}

void DrawWordmark(Canvas& c, int x, int y, int height, COLORREF color, Mark m) {
    Data d = Get(m);
    float k = (float)height / (d.top - d.bottom);
    std::vector<std::vector<Canvas::PtF>> contours(d.nContours);
    for (int i = 0; i < d.nContours; ++i) {
        int end = i + 1 < d.nContours ? d.contours[i + 1] : d.nPoints;
        for (int p = d.contours[i]; p < end; ++p)  // font units are y up from the baseline
            contours[i].push_back({x + d.points[p][0] * k, y + (d.top - d.points[p][1]) * k});
    }
    c.FillPolygons(contours, color);
}

}  // namespace wp
