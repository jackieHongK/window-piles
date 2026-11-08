"""Bakes the scanned materials of the organizer (and the pile cards sharing their ids) into seamless tiles:
real scans, like the walnut (scripts/gen_walnut.py), instead of generated patterns.

Every source is public domain (CC0): Poly Haven (polyhaven.com) and ambientCG (ambientcg.com), and two
photo (rawpixel's public-domain collection) noted where it is used. The libraries'
textures already tile both ways, so a tile is the whole texture, only scaled (never cropped, which would
break the seam), turned when its grain runs the other way, and toned where the material is meant to be a
different shade than the scan (near-black ash, silver aluminum, grey felt, light quartz).

Tiles are stored at 2 px per DIP (the app scales them once per screen DPI) as JPEG, embedded in the exe as
RCDATA resources named MAT_<ID> (src/app/app.rc). The sources are downloaded to build/scan-src on first use.

Writes:
  assets/materials/<id>.jpg   one tile per material
and prints each tile's average color (the material's `base` in src/ui/material.h).

Usage (needs Pillow and numpy):
  python scripts/gen_scans.py
"""
import hashlib
import io
import json
import os
import urllib.request
import zipfile

import numpy as np
from PIL import Image

# id: (source, asset, tile px at 2 px/DIP, turn (degrees), tone (target average RGB or None), contrast)
MATERIALS = {
    'oak':        ('polyhaven', 'oak_veneer_01',       896,  90, None, 1.0),
    'black-ash':  ('polyhaven', 'flamed_black_veneer', 896,   0, (54, 50, 47), 1.0),
    'aluminum':   ('ambientcg', 'Metal009',           1024,   0, (192, 195, 199), 1.0),
    'acrylic':    ('ambientcg', 'Plastic013A',         512,   0, None, 1.0),
    'felt-gray':  ('ambientcg', 'Fabric034',           768,   0, (128, 128, 125), 1.0),
    'quartz':     ('ambientcg', 'Granite002A',         768,   0, (236, 233, 230), 0.55),
    'ebony':      ('ambientcg', 'Wood028',             896,   0, None, 1.0),
    'onyx':       ('ambientcg', 'Onyx007',            1024,   0, None, 1.0),
    'acrylic-cobalt': ('ambientcg', 'Plastic008',      768,   0, (36, 76, 168), 1.8),
    # The rest have no public-domain scan of their own; each is made from a real scan of the same structure
    # (see FINISH): nacre from a banded white onyx with the play of colors
    # its layers make, Damascus from an onyx's fine wavy layers over brushed steel, and clear glass is the
    # glass itself (see-through) with a scan of real smudges and fine scratches on it.
    'rosewood':   ('polyhaven', 'rosewood_veneer1',    896,  90, None, 1.0),
    'nacre':      ('ambientcg', 'Onyx015',            1024,   0, (232, 231, 238), 0.7),
    'damascus':   ('ambientcg', 'Onyx012',            1024,   0, None, 1.0),
    'clear-glass': ('ambientcg', 'SurfaceImperfections003', 1024, 0, None, 1.0),
    # Everyday and playful materials.
    'checkerboard':  ('ambientcg', 'Tiles012',          768,   0, None, 1.0),
    'turf':          ('ambientcg', 'Grass004',          768,   0, None, 1.0),
    'lava':          ('ambientcg', 'Lava001',          1024,   0, None, 1.0),
    'brick':         ('ambientcg', 'Bricks101',        1024,   0, None, 1.0),
    'rattan':        ('ambientcg', 'Wicker010A',        768,   0, None, 1.0),
    # The floral fabric is a CC0 photo (cropped to where the fabric fills the frame, its edges cross-faded so it
    # tiles); the towel is a woven cloth scan in a scrub towel's green.
    'monpe':         ('url', 'https://images.rawpixel.com/editor_1024/cHJpdmF0ZS9sci9pbWFnZXMvd2Vic2l0ZS8yMDIyLTEyL2FpYzU2MjUxLWltYWdlLmpwZw.jpg',
                      640, 0, None, 1.15, (360, 320, 760, 720)),  # rawpixel, "Vintage floral fabric pattern", CC0
    'italy-towel':   ('ambientcg', 'Fabric030',         640,   0, (46, 142, 84), 1.6),
}


def luma(rgb):
    return rgb @ np.array([0.299, 0.587, 0.114])


def nacre(rgb, size):
    # Mother-of-pearl shines in pastel colors that shift band by band (and a little across the shell): a hue
    # from each band's brightness and its place, laid lightly over the pearly white.
    l = luma(rgb) / 255
    yy, xx = np.mgrid[0:size, 0:size] / size
    h = (l * 2.6 + xx * 0.35 + yy * 0.2) % 1.0
    tint = 0.5 + 0.5 * np.cos(2 * np.pi * (h[..., None] + np.array([0.0, 0.33, 0.67])))
    out = rgb * 0.9 + tint * 255 * 0.1
    return out - out.reshape(-1, 3).mean(0) + np.array([232, 231, 238])  # still the pearly white on average


def damascus(rgb, size):
    # The onyx's layers as the folded steel's: its lines pulled into light and dark bands in steel grey, over
    # the brushed grain of a steel scan.
    l = luma(rgb)
    l = np.clip((l - l.mean()) / (l.std() + 1e-6), -2.2, 2.2)
    bands = 118 + l * 26
    brushed = luma(np.asarray(source('ambientcg', 'Metal009').resize((size, size), Image.LANCZOS), np.float64))
    bands = bands * (0.85 + 0.15 * brushed / brushed.mean())
    return np.stack([bands, bands + 4, bands + 10], -1)


def clear_glass(rgb, size):
    # Smudges and fine scratches only, as brightness: the app lays them on the pane as light (the glass itself
    # stays see-through).
    l = luma(rgb)
    return np.repeat(np.clip(l * 1.6, 0, 255)[..., None], 3, -1)


# Relief: how strongly the scan's own normal map (its surface, scanned with it) is lit into the tile, from the
# top left, for materials whose look is in their surface more than their color (a weave, a brick's mortar,
# the grout between tiles, a grass blade's shadow).
RELIEF = {'checkerboard': 0.6, 'brick': 0.9, 'rattan': 0.9, 'italy-towel': 1.1, 'turf': 0.6, 'aluminum': 0.4,
          'felt-gray': 0.5}


def normal_map(kind, asset):
    path = os.path.join(CACHE, asset + '_normal.jpg')
    if not os.path.exists(path):
        if kind == 'polyhaven':
            files = json.loads(fetch('https://api.polyhaven.com/files/' + asset))
            data = fetch(files['nor_gl']['2k']['jpg']['url'])
        else:
            z = zipfile.ZipFile(io.BytesIO(fetch('https://ambientcg.com/get?file=%s_2K-JPG.zip' % asset)))
            data = z.read(next(n for n in z.namelist() if n.endswith('_NormalGL.jpg')))
        open(path, 'wb').write(data)
    return Image.open(path).convert('RGB')


def lit(rgb, normal, strength):
    n = np.asarray(normal, np.float64) / 127.5 - 1  # OpenGL normal map: +y is up the image
    n /= np.linalg.norm(n, axis=-1, keepdims=True) + 1e-6
    light = np.array([-0.45, 0.5, 0.74])
    light /= np.linalg.norm(light)
    d = n @ light
    return rgb * np.clip(1 + strength * (d - light[2]) / light[2], 0.35, 1.8)[..., None]


FINISH = {'nacre': nacre, 'damascus': damascus, 'clear-glass': clear_glass}


def seamless(img, size, blend):
    # A photo cropped out of a scene doesn't tile: its edges are cross-faded into what lies just past the tile.
    img = img.resize((size + blend, size + blend), Image.LANCZOS)
    rgb = np.asarray(img).astype(np.float64)
    t = np.linspace(0, 1, blend)[None, :, None]
    rgb[:, :blend] = rgb[:, :blend] * t + rgb[:, size:size + blend] * (1 - t)
    rgb = rgb[:, :size]
    t = np.linspace(0, 1, blend)[:, None, None]
    rgb[:blend] = rgb[:blend] * t + rgb[size:size + blend] * (1 - t)
    return Image.fromarray(rgb[:size].round().astype(np.uint8))

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(ROOT, 'build', 'scan-src')
OUT = os.path.join(ROOT, 'assets', 'materials')
UA = {'User-Agent': 'stack-neverflow-gen-scans'}


def fetch(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA)) as r:
        return r.read()


def source(kind, asset):
    name = hashlib.sha1(asset.encode()).hexdigest()[:12] if kind == 'url' else asset
    path = os.path.join(CACHE, name + '.jpg')
    if not os.path.exists(path):
        os.makedirs(CACHE, exist_ok=True)
        if kind == 'url':  # a CC0 photo (Wikimedia Commons, rawpixel's public-domain collection)
            data = fetch(asset)
        elif kind == 'polyhaven':
            files = json.loads(fetch('https://api.polyhaven.com/files/' + asset))
            data = fetch(files['Diffuse']['2k']['jpg']['url'])
        else:
            z = zipfile.ZipFile(io.BytesIO(fetch('https://ambientcg.com/get?file=%s_2K-JPG.zip' % asset)))
            data = z.read(next(n for n in z.namelist() if n.endswith('_Color.jpg')))
        open(path, 'wb').write(data)
    return Image.open(path).convert('RGB')


def main():
    os.makedirs(OUT, exist_ok=True)
    for mid, (kind, asset, size, turn, tone, contrast, *crop) in MATERIALS.items():
        img = source(kind, asset)
        if turn:
            img = img.rotate(turn, expand=True)
        if crop:  # a photo: where the material fills the frame, made to tile
            img = seamless(img.crop(crop[0]), size, size // 8)
        else:
            img = img.resize((size, size), Image.LANCZOS)
        rgb = np.asarray(img).astype(np.float64)
        mean = rgb.reshape(-1, 3).mean(0)
        if tone is not None:  # moved to the material's shade, its variation kept (or softened)
            rgb = np.asarray(tone, np.float64) + (rgb - mean) * contrast
        if mid in FINISH:
            rgb = FINISH[mid](rgb, size)
        if mid in RELIEF:
            nm = normal_map(kind, asset)
            if turn:
                nm = nm.rotate(turn, expand=True)
            rgb = lit(rgb, nm.resize((size, size), Image.LANCZOS), RELIEF[mid])
        rgb = np.clip(rgb, 0, 255)
        Image.fromarray(rgb.round().astype(np.uint8)).save(os.path.join(OUT, mid + '.jpg'), quality=82, optimize=True)
        m = rgb.reshape(-1, 3).mean(0).round().astype(int)
        print('%-14s %-24s %4d px  base RGB(%d, %d, %d)%s' % (mid, asset[-24:], size, m[0], m[1], m[2],
              '  dark' if 0.299 * m[0] + 0.587 * m[1] + 0.114 * m[2] < 120 else ''))


if __name__ == '__main__':
    main()
