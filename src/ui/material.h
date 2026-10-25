// Materials for the pile card's body (everything below the title bar). Papers are free; the others are
// paid materials (all selectable during the beta; prices come later), several of them experimental.
// Textures are generated (walnut: a small tile baked from a veneer scan) and fixed per screen position,
// so redraws don't shimmer.
#pragma once

#include <windows.h>

#include <initializer_list>

namespace wp {

class Canvas;

struct Material {
    const wchar_t* name;  // shown in the editor
    const char* id;       // saved in piles.json
    COLORREF base;
    bool dark;         // text on it must be light
    bool paid;         // a paid material (free during the beta)
    bool translucent;  // the desktop shows through: text needs an opaque plate (Canvas::Text)
};

constexpr Material kMaterials[] = {
    {L"종이 · 화이트", "white", RGB(255, 255, 255), false, false, false},
    {L"종이 · 아이보리", "ivory", RGB(246, 240, 224), false, false, false},
    {L"종이 · 크래프트", "kraft", RGB(201, 173, 134), false, false, false},
    {L"가죽", "leather", RGB(122, 78, 50), true, true, false},
    {L"알루미늄", "aluminum", RGB(204, 207, 211), false, true, false},
    {L"티타늄", "titanium", RGB(96, 98, 102), true, true, false},
    {L"세라믹", "ceramic", RGB(244, 242, 237), false, true, false},
    {L"리퀴드 글래스 (실험)", "liquid-glass", RGB(250, 251, 253), false, true, true},
    {L"홀로그램 (실험)", "hologram", RGB(214, 217, 224), false, true, false},
    {L"대리석 (실험)", "marble", RGB(240, 238, 234), false, true, false},
    {L"카본 (실험)", "carbon", RGB(40, 41, 45), true, true, false},
    {L"리넨 (실험)", "linen", RGB(222, 214, 198), false, true, false},
    {L"펠트 (실험)", "felt", RGB(70, 76, 72), true, true, false},
    {L"코르크 (실험)", "cork", RGB(190, 148, 102), false, true, false},
    {L"황동 (실험)", "brass", RGB(196, 162, 96), false, true, false},
    {L"콘크리트 (실험)", "concrete", RGB(178, 176, 171), false, true, false},
    {L"오크 원목 (실험)", "oak", RGB(198, 160, 116), false, true, false},
    {L"월넛 원목 (실험)", "walnut", RGB(114, 80, 57), true, true, false},  // the scan's middle tone
    {L"블랙 애시 (실험)", "black-ash", RGB(50, 46, 43), true, true, false},     // ash stained near black, grain showing
    {L"무광 아크릴 (실험)", "acrylic", RGB(232, 234, 236), false, true, false},  // frosted, matte
    {L"그레이 펠트 (실험)", "felt-gray", RGB(128, 128, 125), true, true, false},
    // Rare and costly: what a desk rarely gets to be made of.
    {L"투명 글래스 (실험)", "clear-glass", RGB(242, 247, 250), false, true, true},
    {L"불투명 아크릴 · 코발트 (실험)", "acrylic-cobalt", RGB(36, 76, 168), true, true, false},
    {L"글리터 석영 (실험)", "quartz", RGB(236, 233, 230), false, true, false},
    {L"자개 (실험)", "nacre", RGB(232, 231, 238), false, true, false},
    {L"버얼 월넛 (실험)", "burl", RGB(124, 86, 56), true, true, false},
    {L"흑단 (실험)", "ebony", RGB(36, 30, 28), true, true, false},
    {L"오닉스 (실험)", "onyx", RGB(212, 188, 146), false, true, false},
    {L"다마스커스 강철 (실험)", "damascus", RGB(118, 122, 128), true, true, false},
};
constexpr int kMaterialCount = sizeof(kMaterials) / sizeof(kMaterials[0]);

// Fills the rounded rect `r` with material `m` (an index into kMaterials; out of range → white paper).
// `dpi` scales pattern sizes and details like leather stitching.
void FillMaterial(Canvas& c, const RECT& r, int radius, int m, UINT dpi);
// One piece made of several rounded rects (the organizer's tray and the bookmark it was pulled out by):
// filled together, shaded as if they were all `shading` (sheen, glass), so there is no seam where they
// meet. Also used to mend a seam drawn over afterwards: the same pixels come out again. No rims drawn.
struct MaterialPart {
    RECT r;
    int radius;
};
void FillMaterialShape(Canvas& c, std::initializer_list<MaterialPart> parts, const RECT& shading, int m, UINT dpi);
// Material id ↔ index for piles.json; unknown ids are white paper.
int MaterialFromId(const char* id);
inline int ClampMaterial(int m) { return m >= 0 && m < kMaterialCount ? m : 0; }

}  // namespace wp
