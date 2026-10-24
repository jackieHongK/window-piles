# Blender pipeline for the unboxing PoC

Rebuilds the tray model, its baked AO, and every Cycles still used by `marketing/unboxing`.
Tested with Blender 5.2.3 (OptiX on an RTX 4060 Ti); CUDA is used if OptiX is missing, CPU otherwise.

## 1. Walnut scan (not committed)

Download the 4K JPGs of Poly Haven's "Black Walnut Veneer 02" (CC0): Diffuse, Normal (GL) and Roughness
from https://polyhaven.com/a/black_walnut_veneer_02 (`black_walnut_veneer_02_{diff,nor_gl,rough}_4k.jpg`, about 27 MB), then:

```
python tools/blender/grade_walnut.py <download dir> tools/blender/tex marketing/unboxing
```

This writes the oil-finish `walnut_*_4k.jpg` into `tex/` (ignored by git) and the page crops into `marketing/unboxing/`.

## 2. Tray, AO, glTF, base stills

```
blender -b -P tools/blender/build_tray.py -- tools/blender/out
python tools/blender/quantize_gltf.py tools/blender/out/tray_body.glb marketing/unboxing/tray_body.json
cp tools/blender/out/tray_ao.png marketing/unboxing/
```

`build_tray.py` mills the tray from one block (profile prism minus an R3-cornered pocket, ribs left standing,
one R0.5 bevel pass), cuts the engraving 0.2 mm deep, writes `tray_body.glb` (box-projected UVs that match the page,
plus a lightmap UV), bakes `tray_ao.png`, renders `cycles_*.png` and saves `neverflow_tray_walnut.blend`.
Add `--quick` for a 64-sample test, `--no-render` to skip the stills.

## 3. Studio product shots

```
blender -b tools/blender/out/neverflow_tray_walnut.blend -P tools/blender/studio_shots.py -- tools/blender/studio --spp 256
```

Seamless cyclorama, large top light, rear strip lights light-linked to the product; six shots (`studio_*.png`).
`--only studio_hero,studio_dark` renders a subset. About 2.5 minutes per shot at 256 samples on the RTX 4060 Ti.

## 4. Packaging shots

```
blender -b tools/blender/studio/neverflow_tray_walnut_studio.blend -P tools/blender/build_pkg.py -- tools/blender/pkg --spp 256
```

Sleeve, shoulder box, perforated seal, 3 mm charcoal velvet wrap and postcard, using the page's own artwork in `art/`.
To re-export that artwork after changing the page, serve `marketing/unboxing` on port 8765, start a headless Edge with
`--remote-debugging-port=9346`, and run `node tools/blender/exportart.mjs 9346 tools/blender/art`.

## 5. Stills for the page

Convert the PNGs to JPEG (longest side 1600 px, quality 86) into `marketing/unboxing/` with the same base names.

`tex/card_*.png` are the pile-card faces from the page (used by `build_tray.py`).
