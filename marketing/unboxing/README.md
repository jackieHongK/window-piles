# Neverflow Tray · Walnut — 3D unboxing PoC

A single-page, hand-driven unboxing of the first paid organizer (Tray · Walnut) at real dimensions, plus the
Cycles product stills it links to. Published copy: https://claude.ai/artifact/FhmNG5inbcjbAaWtaTAtfE (private).

Steps: parcel (slit the tape with a knife) → push the sleeve off → peel the perforated seal → lift the lid →
read the postcard → open the 3 mm velvet wrap (seal, right flap, left flap) → lift the tray out ("첫 만남") →
accessories (manual, name labels, holographic edition card) → detail view (macro, hotspots, path tracing).

## Open it

The page loads ES modules and its own files, so it needs an HTTP server (opening `index.html` from disk will not work):

```
cd marketing/unboxing
python -m http.server 8765
```

Then open http://127.0.0.1:8765/ in Edge or Chrome. three.js 0.186.1 and three-gpu-pathtracer 0.0.25 load from jsDelivr.

## Files

| File | What |
|---|---|
| `index.html` | the whole PoC (three.js, procedural packaging art, sound) |
| `tray_body.json` | the tray, CNC-style one-piece body with a 0.2 mm engraving; glTF with an embedded, quantized buffer (KHR_mesh_quantization). Built by `tools/blender` |
| `tray_ao.png` | ambient occlusion baked on the tray's second UV set |
| `walnut_{diff,nor,rough}.jpg` | 250 × 125 mm crop of the walnut scan, graded to an oil finish (also read by `scripts/gen_walnut.py`) |
| `studio_*.jpg`, `cycles_engraving.jpg`, `pkg_*.jpg` | Cycles stills shown in the page's render notes |

If `tray_body.json` or the walnut images are missing, the page falls back to a procedural tray and veneer.
`index.html?keep` keeps every generated canvas readable so `tools/blender/exportart.mjs` can export the packaging art.

## Credit

Walnut: "Black Walnut Veneer 02" by Jenelle van Heerden, Poly Haven, CC0 — https://polyhaven.com/a/black_walnut_veneer_02
