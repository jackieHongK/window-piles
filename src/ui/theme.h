// Design tokens — Stack Neverflow, "Deskterior": the screen is a desk, piles are paper stacks.
// Warm "ink" neutrals on white paper, walnut as the brand accent (mark and focus only).
// Other color appears only where the user chose it (pile title bars).
#pragma once

#include <windows.h>

namespace wp::theme {

constexpr COLORREF kBackground = RGB(255, 255, 255);   // paper
constexpr COLORREF kForeground = RGB(26, 25, 23);      // ink
constexpr COLORREF kBorder = RGB(225, 222, 214);
constexpr COLORREF kBorderStrong = RGB(168, 163, 153);
constexpr COLORREF kMuted = RGB(243, 241, 236);
constexpr COLORREF kMutedFg = RGB(120, 115, 106);
constexpr COLORREF kSubtleFg = RGB(87, 82, 74);
constexpr COLORREF kPrimary = RGB(33, 27, 22);
constexpr COLORREF kPrimaryFg = RGB(250, 249, 246);
constexpr COLORREF kPrimaryHover = RGB(56, 47, 39);
constexpr COLORREF kPrimarySubtleFg = RGB(214, 209, 200);  // secondary text on kPrimary
constexpr COLORREF kHover = RGB(250, 249, 246);            // faint hover on white
constexpr COLORREF kRing = RGB(123, 75, 42);           // walnut
constexpr COLORREF kBrand = RGB(123, 75, 42);          // walnut: logo/mark and focus, never on pile cards

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
