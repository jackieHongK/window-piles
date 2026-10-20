// Design tokens: shadcn/ui "zinc" neutrals with a Notion-like, monochrome feel.
// Color is used only where the user chose it (pile title bars).
#pragma once

#include <windows.h>

namespace wp::theme {

constexpr COLORREF kBackground = RGB(255, 255, 255);
constexpr COLORREF kForeground = RGB(9, 9, 11);        // zinc-950
constexpr COLORREF kBorder = RGB(228, 228, 231);       // zinc-200
constexpr COLORREF kBorderStrong = RGB(161, 161, 170); // zinc-400
constexpr COLORREF kMuted = RGB(244, 244, 245);        // zinc-100
constexpr COLORREF kMutedFg = RGB(113, 113, 122);      // zinc-500
constexpr COLORREF kSubtleFg = RGB(82, 82, 91);        // zinc-600
constexpr COLORREF kPrimary = RGB(24, 24, 27);         // zinc-900
constexpr COLORREF kPrimaryFg = RGB(250, 250, 250);    // zinc-50
constexpr COLORREF kRing = RGB(161, 161, 170);

constexpr wchar_t kFont[] = L"Malgun Gothic";

// Corner radii (DIP)
constexpr int kRadiusSm = 6;
constexpr int kRadius = 8;
constexpr int kRadiusLg = 12;

struct Preset {
    const wchar_t* name;
    COLORREF color;
};

// Notion's text colors: muted, but easy to tell apart.
constexpr Preset kPresets[] = {
    {L"차콜", RGB(55, 53, 47)},    {L"회색", RGB(120, 119, 116)}, {L"갈색", RGB(159, 107, 83)},
    {L"주황", RGB(217, 115, 13)},  {L"노랑", RGB(203, 145, 47)},  {L"초록", RGB(68, 131, 97)},
    {L"파랑", RGB(51, 126, 169)},  {L"보라", RGB(144, 101, 176)}, {L"분홍", RGB(193, 76, 138)},
    {L"빨강", RGB(212, 76, 71)},
};
constexpr int kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

// Order used for new piles, so neighbouring piles are easy to tell apart.
constexpr int kNewPileOrder[] = {6, 5, 3, 7, 9, 8, 2, 4, 0};

// Readable text color (near-black or white) on top of `bg`.
inline COLORREF ContrastText(COLORREF bg) {
    double l = 0.2126 * GetRValue(bg) + 0.7152 * GetGValue(bg) + 0.0722 * GetBValue(bg);
    return l > 160 ? kForeground : RGB(255, 255, 255);
}

inline COLORREF Mix(COLORREF a, COLORREF b, double t) {  // t=0 → a, t=1 → b
    return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t),
               (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
               (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

}  // namespace wp::theme
