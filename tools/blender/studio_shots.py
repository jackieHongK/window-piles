# Product-photography pass on the built tray: seamless cyclorama, large soft top light, thin rear strip lights that
# draw a line along every R0.5 edge, satin walnut, and a fixed shot list (hero, profile, top, float, edge, dark).
# Run: blender -b out/neverflow_tray_walnut.blend -P studio_shots.py -- <out_dir> [--quick] [--only name,name]
import bpy, bmesh, math, os, sys
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
OUT = argv[0]; QUICK = '--quick' in argv
ONLY = argv[argv.index('--only') + 1].split(',') if '--only' in argv else None
os.makedirs(OUT, exist_ok=True)
scn = bpy.context.scene
MM = 0.001
tray = bpy.data.objects['Tray']
cards = [o for o in bpy.data.objects if o.name.startswith('Card')]
REST_Z = tray.location.z

# ---------- strip the old set ----------
for ob in list(scn.objects):
    if ob.type == 'LIGHT' or ob.name.startswith(('Cylinder', 'Plane')): bpy.data.objects.remove(ob)

# ---------- satin walnut: deeper chocolate, calmer surface ----------
wal = bpy.data.materials['Walnut']; nt = wal.node_tree; N = nt.nodes; L = nt.links
b = N['Principled BSDF']
col = next(n for n in N if n.type == 'TEX_IMAGE' and n.image and n.image.name.startswith('walnut_diff'))
hsv = N.new('ShaderNodeHueSaturation'); hsv.inputs['Saturation'].default_value = 1.0; hsv.inputs['Value'].default_value = 1.0   # the scan is already graded
L.new(col.outputs['Color'], hsv.inputs['Color']); L.new(hsv.outputs['Color'], b.inputs['Base Color'])
for n in N:
    if n.type == 'NORMAL_MAP': n.inputs['Strength'].default_value = 0.55
    if n.type == 'BUMP': n.inputs['Strength'].default_value = 0.0
b.inputs['Coat Weight'].default_value = 0.45; b.inputs['Coat Roughness'].default_value = 0.22
b.inputs['Specular IOR Level'].default_value = 0.45

# ---------- cyclorama: floor that curves up into a back wall (no horizon line) ----------
def cyclorama():
    prof = [(-2.0, 0.0), (0.55, 0.0)]
    R = 0.6
    for i in range(1, 17):
        a = i / 16 * math.pi / 2
        prof.append((0.55 + math.sin(a) * R, R - math.cos(a) * R))
    prof.append((0.55 + R, 2.0))
    bm = bmesh.new()
    rows = []
    for x in (-3.0, 3.0): rows.append([bm.verts.new((x, y, z)) for (y, z) in prof])
    for i in range(len(prof) - 1): bm.faces.new([rows[0][i], rows[1][i], rows[1][i + 1], rows[0][i + 1]])
    me = bpy.data.meshes.new('Cyc'); bm.to_mesh(me); bm.free()
    for p in me.polygons: p.use_smooth = True
    ob = bpy.data.objects.new('Cyc', me); scn.collection.objects.link(ob)
    m = bpy.data.materials.new('CycMat'); m.use_nodes = True
    ob.data.materials.append(m)
    return ob, m.node_tree.nodes['Principled BSDF']
cyc, cyc_b = cyclorama()

def set_look(dark):
    if dark:
        cyc_b.inputs['Base Color'].default_value = (0.012, 0.012, 0.013, 1); cyc_b.inputs['Roughness'].default_value = 0.3; cyc_b.inputs['Specular IOR Level'].default_value = 0.12
        world_bg.inputs['Strength'].default_value = 0.0
    else:
        cyc_b.inputs['Base Color'].default_value = (0.62, 0.615, 0.6, 1); cyc_b.inputs['Roughness'].default_value = 0.4; cyc_b.inputs['Specular IOR Level'].default_value = 0.5
        world_bg.inputs['Strength'].default_value = 0.2

world = scn.world; world_bg = world.node_tree.nodes['Background']
world_bg.inputs['Color'].default_value = (0.8, 0.8, 0.8, 1)

# ---------- light rig ----------
def area(name, loc, size, power, target, color=(1, 1, 1)):
    ld = bpy.data.lights.new(name, 'AREA'); ld.shape = 'RECTANGLE'; ld.size, ld.size_y = size; ld.energy = power; ld.color = color
    ob = bpy.data.objects.new(name, ld); scn.collection.objects.link(ob); ob.location = loc
    ob.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    ob.visible_camera = False                       # the panel itself never shows; its reflection on edges does
    return ob
T = (0, 0, 0.035)
top = area('Top', (0, -0.05, 0.95), (1.5, 0.9), 34, T, (1.0, 0.97, 0.93))
rimL = area('RimL', (-0.5, 0.55, 0.42), (0.06, 0.9), 22, T, (0.95, 0.97, 1.0))
rimR = area('RimR', (0.5, 0.55, 0.42), (0.06, 0.9), 22, T, (0.95, 0.97, 1.0))
# light linking: the rear strips only reach the product, so the floor never shows their hot reflection
product = bpy.data.collections.new('Product'); scn.collection.children.link(product)
for ob in [tray] + list(tray.children_recursive): product.objects.link(ob)
for r in (rimL, rimR): r.light_linking.receiver_collection = product
fill = area('Fill', (0, -1.1, 0.35), (1.6, 0.6), 5, T)

# ---------- color + render ----------
scn.view_settings.view_transform = 'Khronos PBR Neutral'   # keeps the walnut's chroma; AgX washed it toward grey
scn.view_settings.exposure = 0.0
scn.cycles.samples = 64 if QUICK else int(argv[argv.index('--spp') + 1]) if '--spp' in argv else 640
scn.cycles.use_adaptive_sampling = True; scn.cycles.adaptive_threshold = 0.015
scn.cycles.max_bounces = 6
scn.render.resolution_x, scn.render.resolution_y = (960, 600) if QUICK else (1920, 1200)
cam = scn.camera; cd = cam.data

def P(x, y, z): return Vector((x * MM, -z * MM, y * MM))
def tp(x, y, z):                                   # page-space tray point -> world, after the shot's rotation/lift
    bpy.context.view_layer.update(); return tray.matrix_world @ P(x, y, z)

def shot(name, *, loc, look, lens, fstop, rot, cards_on=True, dark=False, lift=0.0, exposure=0.0, rims=22, topw=34, fillw=5):
    if ONLY and name not in ONLY: return
    set_look(dark)
    tray.rotation_euler.z = math.radians(rot); tray.location = (0, 0, REST_Z + lift)
    for c in cards: c.hide_render = not cards_on
    rimL.data.energy = rimR.data.energy = rims; top.data.energy = topw; fill.data.energy = fillw
    scn.view_settings.exposure = exposure
    bpy.context.view_layer.update()
    if callable(loc): loc = loc()
    if callable(look): look = look()
    cam.location = loc
    cam.rotation_euler = (Vector(look) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    cd.lens = lens; cd.clip_start = 0.002; cd.dof.use_dof = True; cd.dof.aperture_fstop = fstop
    cd.dof.focus_distance = (Vector(look) - Vector(loc)).length
    scn.render.filepath = os.path.join(OUT, name + '.png')
    bpy.ops.render.render(write_still=True); print('[studio] rendered', name)

# low 3/4 hero: the camera sits a little above the lip, long lens, everything sharp
shot('studio_hero', loc=(0.46, -0.58, 0.13), look=(0.0, 0.0, 0.045), lens=85, fstop=9, rot=-28)
# pure profile: shows the 28 -> 66 mm slope, the slot rhythm and the 1.2 mm felt gap
shot('studio_profile', loc=(0.95, 0.0, 0.034), look=(0.0, 0.0, 0.034), lens=135, fstop=14, rot=0, cards_on=False, rims=30)
# straight down, square to the frame
shot('studio_top', loc=(0.0, 0.0, 0.58), look=(0.0, 0.0001, 0.0), lens=70, fstop=11, rot=0)
# floating 30 mm: soft shadow below, felt pads visible
shot('studio_float', loc=(0.5, -0.62, 0.075), look=(0.0, 0.0, 0.05), lens=85, fstop=9, rot=-24, cards_on=False, lift=0.03)
# edge line: macro on the front-right corner where the rim light runs along the R0.5 edge
shot('studio_edge', loc=lambda: tp(205, 75, 150), look=lambda: tp(112, 27, 50), lens=105, fstop=8, rot=-28, cards_on=False, rims=40)
# dark hero: product alone, rims only
shot('studio_dark', loc=(0.48, -0.56, 0.16), look=(0.0, 0.0, 0.04), lens=85, fstop=11, rot=-28, cards_on=False, dark=True, rims=38, topw=22, fillw=1, exposure=0.1)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, 'neverflow_tray_walnut_studio.blend'))
print('[studio] done')
