#include "memo_peek.h"

#include <algorithm>
#include <vector>

#include "common/win_util.h"
#include "notes/memo_format.h"
#include "notes/memo_store.h"
#include "ui/theme.h"

namespace wp {

std::unique_ptr<Canvas> RenderMemoPeek(const std::wstring& path, COLORREF color, UINT dpi) {
    using namespace theme;
    auto S = [dpi](int v) { return Scale(v, dpi); };
    std::wstring text = ReadMemo(path);
    struct Line {
        std::wstring s;
        int size;
        bool bold;
    };
    std::vector<Line> lines;
    int h = S(10) + S(12), maxH = S(kMemoPeekMaxH);
    bool clipped = false;
    for (size_t pos = 0; pos <= text.size();) {
        size_t end = std::min(text.find(L'\n', pos), text.size());
        std::wstring s = text.substr(pos, end - pos);
        if (!s.empty() && s.back() == L'\r') s.pop_back();
        pos = end + 1;
        if (s.empty() && lines.empty()) continue;
        MemoLineInfo info = ClassifyMemoLine(s);
        int size = info.kind == MemoLine::H1 ? 17 : info.kind == MemoLine::H2 ? 15 : info.kind == MemoLine::H3 ? 13 : 12;
        bool bold = info.kind == MemoLine::H1 || info.kind == MemoLine::H2 || info.kind == MemoLine::H3;
        std::wstring in((size_t)info.indent * 3, L' ');  // a nested item, a little in
        if (info.kind == MemoLine::Bullet) s = in + L"•  " + s.substr(info.markerLen);
        else if (bold) s = s.substr(info.markerLen);
        else if (info.indent) s = in + s.substr(info.indent);
        int lh = S(size) + S(bold ? 10 : 7);
        if (h + lh > maxH - S(24)) {
            clipped = true;
            break;
        }
        lines.push_back({s, size, bold});
        h += lh;
        if (end >= text.size()) break;
    }
    if (lines.empty()) lines.push_back({L"빈 메모", 12, false});
    h = std::max(h, S(60)) + (clipped ? S(20) : 0) + S(12);
    int w = S(kMemoPeekW);
    auto canvas = std::make_unique<Canvas>(w, h);
    Canvas& c = *canvas;
    c.FillRoundRect({0, 0, w, h}, S(3), RGB(252, 250, 244));  // the flags' paper
    c.FillRoundRect({0, 0, w, S(6)}, 0, color);                // its flag's color along the top
    c.StrokeRoundRect({0, 0, w, h}, S(3), 1, RGB(214, 204, 186));
    int y = S(6) + S(12);
    for (const Line& l : lines) {
        int lh = S(l.size) + S(l.bold ? 10 : 7);
        c.Text(l.s, {S(16), y, w - S(16), y + lh}, l.bold || &l == &lines.front() ? kForeground : kSubtleFg, S(l.size), l.bold,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        y += lh;
    }
    if (clipped) c.Text(L"…  눌러서 열기", {S(16), y, w - S(16), y + S(18)}, kMutedFg, S(11), false, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    return canvas;
}

}  // namespace wp
