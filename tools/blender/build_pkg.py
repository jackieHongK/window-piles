# Packaging product shots: sleeve, rigid shoulder box, perforated seal, 3 mm charcoal velvet wrap, postcard.
# Runs on the studio scene (cyclorama + light rig + tray) saved by studio_shots.py.
# Run: blender -b studio/neverflow_tray_walnut_studio.blend -P build_pkg.py -- <out_dir> [--quick] [--spp N] [--only a,b]
import bpy, bmesh, math, os, sys
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
OUT = argv[0]; QUICK = '--quick' in argv
ONLY = argv[argv.index('--only') + 1].split(',') if '--only' in argv else None
ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'art')
os.makedirs(OUT, exist_ok=True)
scn = bpy.context.scene
MM = 0.001
def P(x, y, z): return Vector((x * MM, -z * MM, y * MM))

tray = bpy.data.objects['Tray']
cards = [o for o in bpy.data.objects if o.name.startswith('Card')]
PAD = 1.2

# ---------- materials ----------
def mat(name, color, rough, sheen=0.0, sheen_rough=0.5, coat=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True; b = m.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = color; b.inputs['Roughness'].default_value = rough
    b.inputs['Sheen Weight'].default_value = sheen; b.inputs['Sheen Roughness'].default_value = sheen_rough
    b.inputs['Coat Weight'].default_value = coat
    return m, b
def paper_bump(m, b, scale=1800.0, strength=0.06):
    N = m.node_tree.nodes; L = m.node_tree.links
    nz = N.new('ShaderNodeTexNoise'); nz.inputs['Scale'].default_value = scale; nz.inputs['Detail'].default_value = 6
    bp = N.new('ShaderNodeBump'); bp.inputs['Strength'].default_value = strength
    L.new(nz.outputs['Fac'], bp.inputs['Height']); L.new(bp.outputs['Normal'], b.inputs['Normal'])
GREIGE, gb = mat('Greige', (0.72, 0.68, 0.62, 1), 0.85, 0.3, 0.7); paper_bump(GREIGE, gb)
WHITE, wb = mat('BoxWhite', (0.86, 0.85, 0.82, 1), 0.88, 0.2, 0.75); paper_bump(WHITE, wb)
SLEEVE, sb = mat('SleevePaper', (0.88, 0.87, 0.84, 1), 0.92, 0.3, 0.75); paper_bump(SLEEVE, sb, 2400, 0.05)
VELVET, vb = mat('Velvet', (0.03, 0.03, 0.032, 1), 1.0, 0.7, 0.45)
vb.inputs['Sheen Tint'].default_value = (0.42, 0.41, 0.4, 1)
vb.inputs['Specular IOR Level'].default_value = 0.2
N = VELVET.node_tree.nodes; L = VELVET.node_tree.links
crush = N.new('ShaderNodeTexNoise'); crush.inputs['Scale'].default_value = 18; crush.inputs['Detail'].default_value = 3
ramp = N.new('ShaderNodeValToRGB'); ramp.color_ramp.elements[0].color = (0.012, 0.012, 0.013, 1); ramp.color_ramp.elements[1].color = (0.024, 0.024, 0.026, 1)
L.new(crush.outputs['Fac'], ramp.inputs['Fac']); L.new(ramp.outputs['Color'], vb.inputs['Base Color'])
pile = N.new('ShaderNodeTexNoise'); pile.inputs['Scale'].default_value = 6000; pile.inputs['Detail'].default_value = 2
pb = N.new('ShaderNodeBump'); pb.inputs['Strength'].default_value = 0.12
L.new(pile.outputs['Fac'], pb.inputs['Height']); L.new(pb.outputs['Normal'], vb.inputs['Normal'])

def printed_mat(name, key):
    """Page artwork: colour, debossed normal (if any) and ORM (G roughness, B metal = foil)."""
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree; b = nt.nodes['Principled BSDF']
    def tex(suffix, cs):
        path = os.path.join(ART, f'{key}_{suffix}.png')
        if not os.path.exists(path): return None
        t = nt.nodes.new('ShaderNodeTexImage'); t.image = bpy.data.images.load(path, check_existing=True); t.image.colorspace_settings.name = cs; return t
    c = tex('color', 'sRGB'); n = tex('normal', 'Non-Color'); o = tex('orm', 'Non-Color')
    if c: nt.links.new(c.outputs['Color'], b.inputs['Base Color'])
    if n:
        nm = nt.nodes.new('ShaderNodeNormalMap'); nm.inputs['Strength'].default_value = 0.8
        nt.links.new(n.outputs['Color'], nm.inputs['Color']); nt.links.new(nm.outputs['Normal'], b.inputs['Normal'])
    if o:
        sp = nt.nodes.new('ShaderNodeSeparateColor'); nt.links.new(o.outputs['Color'], sp.inputs['Color'])
        nt.links.new(sp.outputs['Green'], b.inputs['Roughness']); nt.links.new(sp.outputs['Blue'], b.inputs['Metallic'])
    else: b.inputs['Roughness'].default_value = 0.5
    b.inputs['Sheen Weight'].default_value = 0.25
    return m

# ---------- geometry helpers (page mm coords) ----------
def link(name, me, coll):
    ob = bpy.data.objects.new(name, me); coll.objects.link(ob); return ob
def box(name, w, h, d, cx, cy, cz, mats, coll, bevel=0.4):
    """Rounded paperboard panel; faces pointing away from the box centre get mats[0] (outside), the rest mats[1]."""
    bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts: v.co = P(v.co.x * w + cx, v.co.y * h + cy, v.co.z * d + cz)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    for m in mats: me.materials.append(m)
    ob = link(name, me, coll)
    c = P(0, 47, 0)
    for f in me.polygons: f.material_index = 0 if (f.normal.dot(f.center - c) > 0) else min(1, len(mats) - 1)
    md = ob.modifiers.new('Bevel', 'BEVEL'); md.width = bevel * MM; md.segments = 2; md.limit_method = 'ANGLE'
    return ob
def decal(name, corners, m, coll):
    """Printed face: a quad 0.05 mm proud of the surface, corners in page coords ordered bl, br, tr, tl of the artwork."""
    bm = bmesh.new(); vs = [bm.verts.new(P(*c)) for c in corners]; f = bm.faces.new(vs)
    uvl = bm.loops.layers.uv.new('UVMap')
    for loop, uv in zip(f.loops, ((0, 0), (1, 0), (1, 1), (0, 1))): loop[uvl].uv = uv
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free(); me.materials.append(m)
    return link(name, me, coll)

def new_coll(name):
    c = bpy.data.collections.new(name); scn.collection.children.link(c); return c
base_c, lid_c, sleeve_c, wrap_open_c, wrap_closed_c, card_c = (new_coll(n) for n in ('Base', 'Lid', 'Sleeve', 'WrapOpen', 'WrapClosed', 'Postcard'))

BW, BD, TK = 280, 156, 2
# base: floor, walls, white neck
box('Floor', BW, TK, BD, 0, TK / 2, 0, [GREIGE, WHITE], base_c)
box('WallF', BW, 60, TK, 0, 32, BD / 2 - TK / 2, [GREIGE, WHITE], base_c)
box('WallB', BW, 60, TK, 0, 32, -(BD / 2 - TK / 2), [GREIGE, WHITE], base_c)
box('WallR', TK, 60, BD - 2 * TK, BW / 2 - TK / 2, 32, 0, [GREIGE, WHITE], base_c)
box('WallL', TK, 60, BD - 2 * TK, -(BW / 2 - TK / 2), 32, 0, [GREIGE, WHITE], base_c)
nz, nx = BD / 2 - TK - 0.3 - 0.75, BW / 2 - TK - 0.3 - 0.75
for i, (w, d, x, z) in enumerate(((BW - 2 * TK - 0.6, 1.5, 0, nz), (BW - 2 * TK - 0.6, 1.5, 0, -nz), (1.5, BD - 2 * TK - 3.6, nx, 0), (1.5, BD - 2 * TK - 3.6, -nx, 0))):
    box(f'Neck{i}', w, 40, d, x, 60, z, [WHITE], base_c, 0.3)
# lid
box('LidTop', BW, TK, BD, 0, 93, 0, [GREIGE, WHITE], lid_c)
box('LidF', BW, 30, TK, 0, 77, BD / 2 - TK / 2, [GREIGE, WHITE], lid_c)
box('LidB', BW, 30, TK, 0, 77, -(BD / 2 - TK / 2), [GREIGE, WHITE], lid_c)
box('LidR', TK, 30, BD - 2 * TK, BW / 2 - TK / 2, 77, 0, [GREIGE, WHITE], lid_c)
box('LidL', TK, 30, BD - 2 * TK, -(BW / 2 - TK / 2), 77, 0, [GREIGE, WHITE], lid_c)
decal('LidArt', [(-BW / 2, 94.05, BD / 2), (BW / 2, 94.05, BD / 2), (BW / 2, 94.05, -BD / 2), (-BW / 2, 94.05, -BD / 2)], printed_mat('LidArt', 'lidTop'), lid_c)
# perforated seal across the front seam (y = 62): 250 mm run, tab standing 1.6 mm off at the right end
Z = BD / 2 + 0.06
decal('BandU', [(-125, 65, Z), (125, 65, Z), (125, 70, Z), (-125, 70, Z)], printed_mat('BandU', 'bandU'), lid_c)
decal('BandL', [(-125, 54, Z), (125, 54, Z), (125, 59, Z), (-125, 59, Z)], printed_mat('BandL', 'bandL'), base_c)
sm = printed_mat('Strip', 'strip')
decal('Strip', [(-125, 59, Z + 0.04), (125, 59, Z + 0.04), (125, 65, Z + 0.04), (-125, 65, Z + 0.04)], sm, base_c)
# the tab: same artwork, its last 14 mm, bent away from the box
tab = decal('Tab', [(125, 59, Z + 0.1), (139, 59, Z + 1.7), (139, 65, Z + 1.7), (125, 65, Z + 0.1)], sm, base_c)
for i, (u0) in enumerate((0,)):
    uvl = tab.data.uv_layers['UVMap'].data
    for li, uv in enumerate(((250 / 264, 0), (1, 0), (1, 1), (250 / 264, 1))): uvl[li].uv = uv
strip_ob = bpy.data.objects['Strip']
for li, uv in enumerate(((0, 0), (250 / 264, 0), (250 / 264, 1), (0, 1))): strip_ob.data.uv_layers['UVMap'].data[li].uv = uv
# sleeve (open at both ends)
SL = dict(w=281.2, d=158.2, h=96.4, t=0.6)
box('SlvTop', SL['w'], SL['t'], SL['d'], 0, SL['h'] - SL['t'] / 2, 0, [SLEEVE], sleeve_c, 0.3)
box('SlvBot', SL['w'], SL['t'], SL['d'], 0, SL['t'] / 2, 0, [SLEEVE], sleeve_c, 0.3)
box('SlvF', SL['w'], SL['h'], SL['t'], 0, SL['h'] / 2, SL['d'] / 2 - SL['t'] / 2, [SLEEVE], sleeve_c, 0.3)
box('SlvB', SL['w'], SL['h'], SL['t'], 0, SL['h'] / 2, -(SL['d'] / 2 - SL['t'] / 2), [SLEEVE], sleeve_c, 0.3)
W2, D2, H = SL['w'] / 2, SL['d'] / 2, SL['h']
decal('SlvTopArt', [(-W2, H + 0.05, D2), (W2, H + 0.05, D2), (W2, H + 0.05, -D2), (-W2, H + 0.05, -D2)], printed_mat('SlvTopArt', 'sleeveTop'), sleeve_c)
decal('SlvFrontArt', [(-W2, 0, D2 + 0.05), (W2, 0, D2 + 0.05), (W2, H, D2 + 0.05), (-W2, H, D2 + 0.05)], printed_mat('SlvFrontArt', 'sleeveFront'), sleeve_c)
decal('SlvBackArt', [(W2, 0, -D2 - 0.05), (-W2, 0, -D2 - 0.05), (-W2, H, -D2 - 0.05), (W2, H, -D2 - 0.05)], printed_mat('SlvBackArt', 'sleeveBack'), sleeve_c)

# ---------- velvet wrap: same bend model as the page (3 mm, stiff) ----------
TS = dict(hinge=121.6, y=81.0, L=134.0, Z=148.0, T=3.0)
def flap_line(k, n=48):
    out = [(0.0, 0.0, math.pi)]; ds = TS['L'] / n; x = y = 0.0
    for i in range(n):
        t = (i + 0.5) * ds; u = t / TS['L']
        closed = math.pi - 0.05 + 0.1 * u
        opn = 0.12 if t < 12 else (0.12 - (t - 12) / 48 * 1.32 if t < 60 else -1.2)
        kk = min(1, max(0, k * 1.35 - u * 0.35)); a = closed + (opn - closed) * (kk * kk * (3 - 2 * kk))
        x += math.cos(a) * ds; y += math.sin(a) * ds; out.append((x, y, a))
    return out
def flap(name, sx, lift, k, coll):
    line = flap_line(k); rows = 12
    bm = bmesh.new(); grid = []
    for (lx, ly, a) in line:
        col = []
        for r in range(rows + 1):
            z = (r / rows - 0.5) * TS['Z']
            col.append(bm.verts.new(P(sx * (TS['hinge'] + lx), max(TS['y'] + ly + lift, 1.6), z)))
        grid.append(col)
    for c in range(len(grid) - 1):
        for r in range(rows):
            vs = [grid[c][r], grid[c + 1][r], grid[c + 1][r + 1], grid[c][r + 1]]
            bm.faces.new(vs if sx > 0 else list(reversed(vs)))
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free(); me.materials.append(VELVET)
    for p in me.polygons: p.use_smooth = True
    ob = link(name, me, coll)
    so = ob.modifiers.new('Thick', 'SOLIDIFY'); so.thickness = TS['T'] * MM; so.offset = 0; so.use_even_offset = True
    bv = ob.modifiers.new('Soft', 'BEVEL'); bv.width = 1.0 * MM; bv.segments = 3; bv.limit_method = 'ANGLE'
    return ob, line
for coll, k in ((wrap_closed_c, 0.0), (wrap_open_c, 1.0)):
    fl, _ = flap(f'FlapL_{coll.name}', -1, 0.5, k, coll)
    fr, lineR = flap(f'FlapR_{coll.name}', 1, 1.1, k, coll)
    for sx in (1, -1): box(f'Drape{sx}_{coll.name}', TS['T'], TS['y'] - 12, TS['Z'], sx * TS['hinge'], 12 + (TS['y'] - 12) / 2, 0, [VELVET], coll, 1.2)
    box(f'Bed_{coll.name}', BW - 2 * TK - 3.6, 3, BD - 2 * TK - 3.6, 0, 10.5, 0, [VELVET], coll, 1.2)
    # seal on the right flap at the centre line
    n = len(lineR) - 1; i = round(TS['hinge'] / TS['L'] * n); lx, ly, a = lineR[i]
    o = TS['T'] / 2 + 0.3
    cx, cy = TS['hinge'] + lx + math.sin(a) * o, TS['y'] + ly + 1.1 - math.cos(a) * o
    bpy.ops.mesh.primitive_circle_add(vertices=64, radius=16 * MM, fill_type='NGON', location=P(cx, cy, 0))
    seal = bpy.context.active_object; seal.name = f'Seal_{coll.name}'
    for c in seal.users_collection: c.objects.unlink(seal)
    coll.objects.link(seal); seal.rotation_euler = (0, -(a - math.pi), 0)
    seal.data.materials.append(printed_mat('SealArt', 'seal'))
    uvl = seal.data.uv_layers.new(name='UVMap')
    for poly in seal.data.polygons:
        for li in poly.loop_indices:
            co = seal.data.vertices[seal.data.loops[li].vertex_index].co; uvl.data[li].uv = (co.x / (32 * MM) + 0.5, co.y / (32 * MM) + 0.5)
# postcard on the closed wrap
pc_m = printed_mat('PostcardArt', 'postcard')
box('Postcard', 148, 0.6, 100, -56, TS['y'] + TS['T'] / 2 + 2.6, 4, [WHITE], card_c, 0.2)
pc = decal('PostcardFace', [(-130, TS['y'] + TS['T'] / 2 + 2.95, 54), (18, TS['y'] + TS['T'] / 2 + 2.95, 54), (18, TS['y'] + TS['T'] / 2 + 2.95, -46), (-130, TS['y'] + TS['T'] / 2 + 2.95, -46)], pc_m, card_c)

# ---------- shots (reuse the studio rig from studio_shots.py) ----------
cyc_b = bpy.data.materials['CycMat'].node_tree.nodes['Principled BSDF']
world_bg = scn.world.node_tree.nodes['Background']
top, rimL, rimR, fill = (bpy.data.objects[n] for n in ('Top', 'RimL', 'RimR', 'Fill'))
product = bpy.data.collections['Product']
for c in (base_c, lid_c, sleeve_c, wrap_open_c, wrap_closed_c, card_c):
    for ob in c.objects: product.objects.link(ob)                    # rear strips light the packaging edges too
scn.cycles.samples = 64 if QUICK else int(argv[argv.index('--spp') + 1]) if '--spp' in argv else 256
scn.render.resolution_x, scn.render.resolution_y = (960, 600) if QUICK else (1920, 1200)
cam = scn.camera; cd = cam.data
def show(**vis):
    for c, v in zip((base_c, lid_c, sleeve_c, wrap_open_c, wrap_closed_c, card_c), (vis.get('base', 1), vis.get('lid', 0), vis.get('sleeve', 0), vis.get('wopen', 0), vis.get('wclosed', 0), vis.get('card', 0))):
        for ob in c.objects: ob.hide_render = not v               # per object: every piece is also in the light-linking collection
    tray.hide_render = not vis.get('tray', 0)
    for ob in tray.children_recursive: ob.hide_render = not vis.get('tray', 0)
    for ob in cards: ob.hide_render = True
def shot(name, *, loc, look, lens=85, fstop=9, rot=-24, dark=False, sleeve_x=0.0, exposure=0.0, **vis):
    if ONLY and name not in ONLY: return
    show(**vis)
    for c in (base_c, lid_c, wrap_open_c, wrap_closed_c, card_c):
        for ob in c.objects: ob.parent = None
    pivot = bpy.data.objects.get('PkgPivot') or bpy.data.objects.new('PkgPivot', None)
    if pivot.name not in scn.collection.objects: scn.collection.objects.link(pivot)
    pivot.rotation_euler = (0, 0, math.radians(rot))
    for c in (base_c, lid_c, sleeve_c, wrap_open_c, wrap_closed_c, card_c):
        for ob in c.objects:
            if ob.parent is None: ob.parent = pivot
    for ob in sleeve_c.objects: ob.location = (sleeve_x * MM, 0, 0) if ob.parent == pivot else ob.location
    tray.parent = pivot; tray.location = (0, 0, (12 + PAD) * MM); tray.rotation_euler = (0, 0, 0)
    if dark:
        cyc_b.inputs['Base Color'].default_value = (0.012, 0.012, 0.013, 1); cyc_b.inputs['Roughness'].default_value = 0.3; world_bg.inputs['Strength'].default_value = 0
        rimL.data.energy = rimR.data.energy = 38; top.data.energy = 22; fill.data.energy = 1
    else:
        cyc_b.inputs['Base Color'].default_value = (0.62, 0.615, 0.6, 1); cyc_b.inputs['Roughness'].default_value = 0.4; world_bg.inputs['Strength'].default_value = 0.2
        rimL.data.energy = rimR.data.energy = 22; top.data.energy = 34; fill.data.energy = 5
    scn.view_settings.exposure = exposure
    bpy.context.view_layer.update()
    cam.location = loc; cam.rotation_euler = (Vector(look) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    cd.lens = lens; cd.dof.use_dof = True; cd.dof.aperture_fstop = fstop; cd.dof.focus_distance = (Vector(look) - Vector(loc)).length
    scn.render.filepath = os.path.join(OUT, name + '.png'); bpy.ops.render.render(write_still=True); print('[pkg] rendered', name)

shot('pkg_hero', loc=(0.62, -0.78, 0.2), look=(0, 0, 0.045), lid=1, sleeve=1)
shot('pkg_sleeve', loc=(0.7, -0.62, 0.26), look=(0.06, 0, 0.05), lid=1, sleeve=1, sleeve_x=150, rot=-18)
shot('pkg_seal', loc=(0.5, -0.42, 0.15), look=(0.09, -0.09, 0.062), lens=85, fstop=10, lid=1, rot=-18)
shot('pkg_open', loc=(0.3, -0.52, 0.62), look=(0, 0, 0.06), lens=70, wclosed=1, card=1, rot=-12, exposure=-0.35)
shot('pkg_reveal', loc=(0.34, -0.5, 0.56), look=(0, 0, 0.06), lens=70, wopen=1, tray=1, rot=-12)
shot('pkg_dark', loc=(0.6, -0.72, 0.22), look=(0, 0, 0.045), lid=1, sleeve=1, dark=True, exposure=0.15)
print('[pkg] done')
