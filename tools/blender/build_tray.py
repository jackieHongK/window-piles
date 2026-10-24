# Neverflow Tray · Walnut — Cycles stills + baked AO lightmap + glb for the realtime page.
# Run: blender -b -P build_tray.py -- <out_dir> [--quick]
# Geometry is authored in the realtime page's frame (mm, y up, +z = front) and mapped to Blender (m, z up)
# so the exported glb lands back in the page unchanged.
import bpy, bmesh, math, os, sys
from mathutils import Vector, Matrix

argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
OUT = argv[0] if argv else os.path.join(os.path.dirname(__file__), 'out')
QUICK = '--quick' in argv
TEX = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tex')
os.makedirs(OUT, exist_ok=True)

MM = 0.001
def P(x, y, z):                      # page (mm, y up) -> Blender (m, z up)
    return Vector((x * MM, -z * MM, y * MM))

PAD = 1.2
LEAN = math.radians(10.5)
SLOTS = [40.6, 13.75, -13.75, -40.6]

# ---------- clean scene ----------
bpy.ops.wm.read_factory_settings(use_empty=True)
scn = bpy.context.scene
scn.unit_settings.system = 'METRIC'

# ---------- materials ----------
def img(name, colorspace='sRGB'):
    im = bpy.data.images.load(os.path.join(TEX, name), check_existing=True)
    im.colorspace_settings.name = colorspace
    return im

def principled(name, color=(0.8, 0.8, 0.8, 1), rough=0.5, coat=0.0, coat_rough=0.3):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = m.node_tree.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = color
    b.inputs['Roughness'].default_value = rough
    b.inputs['Coat Weight'].default_value = coat
    b.inputs['Coat Roughness'].default_value = coat_rough
    return m, b

def walnut_material():
    m, b = principled('Walnut', coat=0.3, coat_rough=0.32)
    nt = m.node_tree; N = nt.nodes; L = nt.links
    uv = N.new('ShaderNodeUVMap'); uv.uv_map = 'UVMap'
    # Poly Haven "Black Walnut Veneer 02" (CC0), graded to an oil finish; the 4K scan covers 1000 mm, the UV tile is 240 x 120 mm
    col = N.new('ShaderNodeTexImage'); col.image = img('walnut_diff_4k.jpg')
    rgh = N.new('ShaderNodeTexImage'); rgh.image = img('walnut_rough_4k.jpg', 'Non-Color')
    nor = N.new('ShaderNodeTexImage'); nor.image = img('walnut_nor_4k.jpg', 'Non-Color')
    tile = N.new('ShaderNodeMapping'); tile.inputs['Scale'].default_value = (0.24, 0.12, 1.0)
    L.new(uv.outputs['UV'], tile.inputs['Vector'])
    for t in (col, rgh, nor): t.extension = 'REPEAT'; L.new(tile.outputs['Vector'], t.inputs['Vector'])
    sep = N.new('ShaderNodeSeparateColor')
    nm = N.new('ShaderNodeNormalMap'); nm.inputs['Strength'].default_value = 0.7
    L.new(col.outputs['Color'], b.inputs['Base Color'])
    L.new(rgh.outputs['Color'], sep.inputs['Color']); L.new(sep.outputs['Green'], b.inputs['Roughness'])
    L.new(nor.outputs['Color'], nm.inputs['Color']); L.new(nm.outputs['Normal'], b.inputs['Normal'])
    # micro pores along the grain, only visible in macro shots (matches the page's detail normal)
    wave = N.new('ShaderNodeTexNoise'); wave.inputs['Scale'].default_value = 900.0
    mp = N.new('ShaderNodeMapping'); mp.inputs['Scale'].default_value = (0.08, 1.0, 1.0)
    L.new(uv.outputs['UV'], mp.inputs['Vector']); L.new(mp.outputs['Vector'], wave.inputs['Vector'])
    ramp = N.new('ShaderNodeValToRGB'); ramp.color_ramp.elements[0].position = 0.62; ramp.color_ramp.elements[1].position = 0.72
    bump = N.new('ShaderNodeBump'); bump.inputs['Strength'].default_value = 0.0; bump.invert = True   # the scan carries the real pores
    L.new(wave.outputs['Fac'], ramp.inputs['Fac']); L.new(ramp.outputs['Color'], bump.inputs['Height'])
    L.new(nm.outputs['Normal'], bump.inputs['Normal']); L.new(bump.outputs['Normal'], b.inputs['Normal'])
    return m

WALNUT = walnut_material()
BURNT, _ = principled('Engraving', color=(0.045, 0.026, 0.014, 1), rough=0.92)
FELT, _ = principled('PadFelt', color=(0.035, 0.032, 0.03, 1), rough=1.0)
STONE, sb = principled('Stone', color=(0.46, 0.43, 0.39, 1), rough=0.55, coat=0.12, coat_rough=0.5)
DESK, db = principled('DeskFelt', color=(0.11, 0.1, 0.09, 1), rough=1.0)
PAPER, _ = principled('CardPaper', color=(0.9, 0.89, 0.86, 1), rough=0.82)
for mat, bsdf, scale, s in ((DESK, db, 2500.0, 0.25), (STONE, sb, 300.0, 0.05)):
    N = mat.node_tree.nodes; L = mat.node_tree.links
    nz = N.new('ShaderNodeTexNoise'); nz.inputs['Scale'].default_value = scale; nz.inputs['Detail'].default_value = 8
    bp = N.new('ShaderNodeBump'); bp.inputs['Strength'].default_value = s
    L.new(nz.outputs['Fac'], bp.inputs['Height']); L.new(bp.outputs['Normal'], bsdf.inputs['Normal'])

# ---------- geometry helpers ----------
def link(name, me):
    ob = bpy.data.objects.new(name, me); scn.collection.objects.link(ob); return ob

def box_obj(name, w, h, d, cx, cy, cz, bevel=0.5):
    """Axis-aligned box in page coords, edges rounded with a real bevel (R = bevel mm)."""
    bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        x, y, z = v.co.x * w + cx, v.co.y * h + cy, v.co.z * d + cz   # cube local xyz read as page xyz
        v.co = P(x, y, z)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    ob = link(name, me)
    if bevel:
        md = ob.modifiers.new('Bevel', 'BEVEL'); md.width = bevel * MM; md.segments = 3; md.limit_method = 'ANGLE'
    return ob

def prism_obj(name, pts, x0, x1, bevel=0.5):
    """Side plate: polygon in page (z, y), extruded along page x from x0 to x1."""
    bm = bmesh.new()
    a = [bm.verts.new(P(x0, y, z)) for (z, y) in pts]
    b = [bm.verts.new(P(x1, y, z)) for (z, y) in pts]
    bm.faces.new(a); bm.faces.new(list(reversed(b)))
    n = len(pts)
    for i in range(n):
        j = (i + 1) % n; bm.faces.new([a[i], a[j], b[j], b[i]])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    ob = link(name, me)
    if bevel:
        md = ob.modifiers.new('Bevel', 'BEVEL'); md.width = bevel * MM; md.segments = 3; md.limit_method = 'ANGLE'
    return ob

def pocket_obj(name, x0, x1, z0, z1, y0, y1, r_corner=3.0, r_floor=1.5):
    """The milled cavity: vertical inside corners at the tool radius, a small fillet where walls meet the floor."""
    bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = P((x0 + x1) / 2 + v.co.x * (x1 - x0), (y0 + y1) / 2 + v.co.y * (y1 - y0), (z0 + z1) / 2 + v.co.z * (z1 - z0))
    bm.edges.ensure_lookup_table()
    vert_edges = [e for e in bm.edges if abs((e.verts[0].co - e.verts[1].co).normalized().z) > 0.99]
    bmesh.ops.bevel(bm, geom=vert_edges, offset=r_corner * MM, segments=8, affect='EDGES', profile=0.5)
    zmin = min(v.co.z for v in bm.verts)
    floor_edges = [e for e in bm.edges if all(abs(v.co.z - zmin) < 1e-7 for v in e.verts)]
    bmesh.ops.bevel(bm, geom=floor_edges, offset=r_floor * MM, segments=4, affect='EDGES', profile=0.5)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    return link(name, me)

def evaluated_mesh(ob):
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg)); me.transform(ob.matrix_world)
    return me

# ---------- engraving cutter: real 0.2 mm recess in the right side plate ----------
def engraving_cutter():
    font = bpy.data.fonts.load('C:/Windows/Fonts/consola.ttf')
    cu = bpy.data.curves.new('EngraveText', 'FONT'); cu.font = font
    cu.body = 'SN-TR-WN \u00b7 No.0042'; cu.size = 4.4 * MM; cu.extrude = 0.2 * MM
    cu.align_x = 'CENTER'; cu.align_y = 'CENTER'
    t = link('EngraveText', cu)
    # text x -> page -z (reads left-to-right seen from +x), text y -> page +y, text normal -> page +x
    R = Matrix(((0, 0, 1, 0), (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
    t.matrix_world = Matrix.Translation(P(120.0, 12.0, -1.0)) @ R
    me = evaluated_mesh(t); bpy.data.objects.remove(t)
    # brand mark (U + three bars), same proportions as the logo, 6.6 mm tall, left of the text
    bm = bmesh.new(); bm.from_mesh(me)
    k = 6.6 / 23.0; ztop, ytop = 34.0, 12.0 + 3.3                       # logo units (x 3.3..36.7, y 12..35) -> mm
    def bar(z0, y0, z1, y1):
        zz0, zz1 = ztop - (z0 - 3.3) * k, ztop - (z1 - 3.3) * k
        yy0, yy1 = ytop - (y1 - 12) * k, ytop - (y0 - 12) * k
        for v in bmesh.ops.create_cube(bm, size=1.0)['verts']:
            v.co = P(120.0 + v.co.x * 0.4, (yy0 + yy1) / 2 + v.co.y * (yy1 - yy0), (zz0 + zz1) / 2 + v.co.z * (zz0 - zz1))
    bar(3.3, 12, 6.7, 33)        # left stroke of the U
    bar(33.3, 12, 36.7, 33)      # right stroke
    bar(3.3, 31.6, 36.7, 35)     # bottom of the U
    for y in (26, 19.5, 13): bar(10, y, 30, y + 5)
    bm.to_mesh(me); bm.free()
    ob = link('EngraveCutter', me); ob.data.materials.append(BURNT)
    return ob

# ---------- Tray ----------
def build_tray():
    # CNC-milled from one walnut block: the side-profile prism, minus a pocket with R3 inside corners (tool radius)
    # and R1.5 floor fillets, with the three divider ribs left standing; then a single R0.5 pass over every edge.
    block = prism_obj('Block', [(58, 0), (58, 28), (-58, 66), (-58, 0)], -120, 120, bevel=0)
    pocket = pocket_obj('Pocket', -114, 114, -52, 52, 6, 140)
    ribs = [box_obj(f'Rib{i}', 228.4, 5, 3.5, 0, 8.5, z, bevel=0) for i, z in enumerate((27.5, 0, -27.5))]
    cutter = engraving_cutter()
    for ob in [pocket, cutter] + ribs: ob.hide_render = True; ob.display_type = 'WIRE'
    for ob in [block, pocket] + ribs: ob.data.materials.append(WALNUT)
    def boolean(name, op, obj):
        md = block.modifiers.new(name, 'BOOLEAN'); md.operation = op; md.object = obj
        md.solver = 'EXACT'; md.use_self = True; md.use_hole_tolerant = True; md.material_mode = 'TRANSFER'
    boolean('Mill', 'DIFFERENCE', pocket)
    for i, r in enumerate(ribs): boolean(f'Rib{i}', 'UNION', r)
    bv = block.modifiers.new('Edges', 'BEVEL'); bv.width = 0.5 * MM; bv.segments = 3; bv.limit_method = 'ANGLE'
    bv.angle_limit = math.radians(30); bv.use_clamp_overlap = True
    boolean('Engrave', 'DIFFERENCE', cutter)                      # after the bevel so the letters stay crisp
    me = evaluated_mesh(block)
    for ob in [block, pocket, cutter] + ribs: bpy.data.objects.remove(ob)
    me.name = 'TrayBody'
    # boolean faces are split into triangles: shade smooth, sharp only across real edges, so flat faces read as one surface
    for f in me.polygons: f.use_smooth = True
    me.set_sharp_from_angle(angle=math.radians(35))
    while len(me.materials) > 0: me.materials.pop()
    me.materials.append(WALNUT); me.materials.append(BURNT)
    # engraving faces by position: inside the 0.2 mm recess of the right side, within the engraving's footprint
    for f in me.polygons:
        x, y, z = f.center.x / MM, f.center.z / MM, -f.center.y / MM
        f.material_index = 1 if (119.7 < x < 119.995 and 5 < y < 19 and -26 < z < 37) else 0
    # UVMap: the page's box projection (240 mm x 120 mm tile, grain along x) so the same textures line up
    while me.uv_layers: me.uv_layers.remove(me.uv_layers[0])     # the font mesh brings its own 'UVMap'
    uv0 = me.uv_layers.new(name='UVMap')
    for f in me.polygons:
        n = f.normal; ax, ay, az = abs(n.x), abs(n.z), abs(n.y)       # page axes: x, y(=blender z), z(=blender -y)
        for li in f.loop_indices:
            co = me.vertices[me.loops[li].vertex_index].co
            x, y, z = co.x / MM + 120, co.z / MM, -co.y / MM + 58
            if ay >= ax and ay >= az: u, v = x / 240, z / 120
            elif az >= ax: u, v = x / 240, y / 120
            else: u, v = z / 240, y / 120
            uv0.data[li].uv = (u, v)
    tray = link('Tray', me)
    tray.location = (0, 0, PAD * MM)
    # felt pads
    for (x, z) in ((108, 48), (-108, 48), (108, -48), (-108, -48)):
        bpy.ops.mesh.primitive_cylinder_add(radius=5 * MM, depth=PAD * MM, vertices=32, location=P(x, PAD / 2 - PAD, z))
        pad = bpy.context.active_object; pad.data.materials.append(FELT); pad.parent = tray
        pad.location = P(x, PAD / 2 - PAD, z)
    return tray

def card(i, parent):
    # 190 x 100 x 0.5 mm, leaning back 10.5 deg into slot i, image on the front face
    m = bpy.data.materials.new(f'Card{i}'); m.use_nodes = True
    b = m.node_tree.nodes['Principled BSDF']; b.inputs['Roughness'].default_value = 0.6; b.inputs['Coat Weight'].default_value = 0.1
    t = m.node_tree.nodes.new('ShaderNodeTexImage'); t.image = img(f'card_{i}.png')
    m.node_tree.links.new(t.outputs['Color'], b.inputs['Base Color'])
    bm = bmesh.new()
    up = Vector((0, math.cos(LEAN), -math.sin(LEAN)))
    base = Vector((0, 6, SLOTS[i]))
    corners = [(-95, 0), (95, 0), (95, 100), (-95, 100)]
    vs = []
    for (x, h) in corners:
        p = base + Vector((x, 0, 0)) + up * h
        vs.append(bm.verts.new(P(p.x, p.y, p.z)))
    f = bm.faces.new(vs)
    uvl = bm.loops.layers.uv.new('UVMap')
    for loop, uvv in zip(f.loops, ((0, 0), (1, 0), (1, 1), (0, 1))): loop[uvl].uv = uvv
    me = bpy.data.meshes.new(f'Card{i}'); bm.to_mesh(me); bm.free()
    ob = link(f'Card{i}', me); ob.data.materials.append(m); ob.data.materials.append(PAPER)
    so = ob.modifiers.new('Thick', 'SOLIDIFY'); so.thickness = 0.5 * MM; so.offset = -1; so.material_offset = 1; so.material_offset_rim = 1
    ob.parent = parent
    return ob

tray = build_tray()
cards = [card(i, tray) for i in range(4)]

# ---------- set: plinth on a felt desk ----------
bpy.ops.mesh.primitive_cylinder_add(radius=170 * MM, depth=24 * MM, vertices=160, location=(0, 0, -12 * MM))
plinth = bpy.context.active_object; plinth.data.materials.append(STONE)
bv = plinth.modifiers.new('Bevel', 'BEVEL'); bv.width = 1.5 * MM; bv.segments = 4; bv.limit_method = 'ANGLE'
bpy.ops.mesh.primitive_plane_add(size=6, location=(0, 0, -24 * MM))
desk = bpy.context.active_object; desk.data.materials.append(DESK)
for ob in (tray,): ob.location.z = PAD * MM        # pads touch the plinth top at z = 0
tray.rotation_euler.z = math.radians(-18)

# ---------- studio light (matches the page's softbox environment) ----------
world = bpy.data.worlds.new('Studio'); scn.world = world; world.use_nodes = True
world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.03, 0.028, 0.026, 1)
world.node_tree.nodes['Background'].inputs['Strength'].default_value = 1.0
def area(name, loc, size, power, color, target=(0, 0, 0.04)):
    ld = bpy.data.lights.new(name, 'AREA'); ld.shape = 'RECTANGLE'; ld.size, ld.size_y = size; ld.energy = power; ld.color = color
    ob = bpy.data.objects.new(name, ld); scn.collection.objects.link(ob); ob.location = loc
    d = Vector(target) - Vector(loc); ob.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    return ob
key = area('Key', (-0.42, 0.56 * -1, 0.9), (1.1, 0.55), 45, (1.0, 0.95, 0.88))
strip = area('Strip', (0.85, -0.15, 0.32), (0.16, 0.9), 16, (0.98, 0.98, 1.0))
rim = area('Rim', (-0.25, 0.85, 0.3), (0.65, 0.2), 12, (0.82, 0.9, 1.05))
graze = area('Graze', (0, 0, 0), (0.004, 0.08), 0, (1.0, 0.92, 0.8))

# ---------- render settings ----------
scn.render.engine = 'CYCLES'
prefs = bpy.context.preferences.addons['cycles'].preferences
for dev_type in ('OPTIX', 'CUDA'):
    try:
        prefs.compute_device_type = dev_type; prefs.get_devices()
        if any(d.type == dev_type for d in prefs.devices):
            for d in prefs.devices: d.use = d.type == dev_type
            scn.cycles.device = 'GPU'; break
    except Exception: pass
print('[build] device', scn.cycles.device, prefs.compute_device_type)
scn.cycles.samples = 64 if QUICK else 512
scn.cycles.use_denoising = True
scn.cycles.max_bounces = 8
scn.render.resolution_x, scn.render.resolution_y = (960, 600) if QUICK else (1920, 1200)
scn.render.image_settings.file_format = 'PNG'
try: scn.view_settings.view_transform = 'Khronos PBR Neutral'
except Exception: scn.view_settings.view_transform = 'AgX'
scn.view_settings.exposure = -0.35

cam_data = bpy.data.cameras.new('Cam'); cam = bpy.data.objects.new('Cam', cam_data); scn.collection.objects.link(cam); scn.camera = cam
def shot(name, loc, look, lens, fstop, focus, lights=None):
    cam.location = loc
    cam.rotation_euler = (Vector(look) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    cam_data.lens = lens; cam_data.clip_start = 0.002
    cam_data.dof.use_dof = True; cam_data.dof.aperture_fstop = fstop
    cam_data.dof.focus_distance = (Vector(focus) - Vector(loc)).length
    for ob, e in (lights or {}).items(): ob.data.energy = e
    scn.render.filepath = os.path.join(OUT, name + '.png')
    bpy.ops.render.render(write_still=True)
    print('[build] rendered', name)

def tray_point(x, y, z):            # page-space tray point -> world (follows the tray's rotation)
    return tray.matrix_world @ (P(x, y, z) - Vector((0, 0, 0)))
bpy.context.view_layer.update()
# grazing light: 6 mm off the engraved face, above it, skimming down so the recess walls cast shadows
graze.location = tray_point(126, 80, -1)
graze.rotation_euler = (tray_point(120, 12, -1) - graze.location).to_track_quat('-Z', 'Y').to_euler()

if '--no-render' not in argv:
    shot('cycles_hero', (0.36, -0.42, 0.22), tray_point(0, 40, 0), 50, 5.6, tray_point(60, 28, 58))
    shot('cycles_edge_macro', tray_point(230, 95, 215), tray_point(105, 26, 52), 100, 6.3, tray_point(120, 28, 58))
    for c in cards: c.hide_render = True
    shot('cycles_engraving', tray_point(300, 34, 7), tray_point(120, 12, 7), 100, 5.6, tray_point(120, 12, 7),
         lights={key: 20, graze: 0.4})
    graze.data.energy = 0; key.data.energy = 45
    shot('cycles_topdown', (0.0, -0.2, 0.55), tray_point(0, 30, 0), 50, 8, tray_point(0, 30, 0))
    for c in cards: c.hide_render = False

# ---------- bake: ambient occlusion into a second UV (lightmap) for the realtime page ----------
bpy.context.view_layer.objects.active = tray
for ob in scn.objects: ob.select_set(False)
tray.select_set(True)
me = tray.data
lm = me.uv_layers.new(name='Lightmap'); me.uv_layers.active = lm
bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.006)
bpy.ops.uv.pack_islands(rotate=True, margin=0.006)        # long thin panels: rotate to fill the square
bpy.ops.object.mode_set(mode='OBJECT')
me.uv_layers['UVMap'].active_render = True
size = 512 if QUICK else 1024
ao_img = bpy.data.images.new('TrayAO', size, size, alpha=False, float_buffer=False)
ao_img.colorspace_settings.name = 'Non-Color'
added = []
for mat in me.materials:
    nt = mat.node_tree
    uvn = nt.nodes.new('ShaderNodeUVMap'); uvn.uv_map = 'Lightmap'
    tn = nt.nodes.new('ShaderNodeTexImage'); tn.image = ao_img
    nt.links.new(uvn.outputs['UV'], tn.inputs['Vector']); added.append((nt, uvn)); added.append((nt, tn))
    for n in nt.nodes: n.select = False
    tn.select = True; nt.nodes.active = tn
# isolate the tray (self-occlusion only: it sits in a box insert, on a plinth and on desks)
hidden = []
for ob in scn.objects:
    if ob is not tray and ob.type == 'MESH' and ob.parent is not tray and not ob.hide_render:
        ob.hide_render = True; hidden.append(ob)
for c in cards: c.hide_render = True
scn.cycles.samples = 64 if QUICK else 512
scn.render.bake.margin = 8
scn.world.light_settings.distance = 0.04
bpy.ops.object.bake(type='AO')
ao_img.filepath_raw = os.path.join(OUT, 'tray_ao.png'); ao_img.file_format = 'PNG'; ao_img.save()
print('[build] baked AO')
for ob in hidden: ob.hide_render = False
for c in cards: c.hide_render = False

# ---------- export the tray body (geometry + both UVs) for the page ----------
for nt, n in added: nt.nodes.remove(n)             # drop the bake helper nodes before export
rot = tray.rotation_euler.z; loc = tray.location.copy()
tray.rotation_euler.z = 0; tray.location = (0, 0, 0)
for ob in scn.objects: ob.select_set(False)
tray.select_set(True)
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, 'tray_body.glb'), export_format='GLB', use_selection=True,
                          export_texcoords=True, export_normals=True, export_materials='PLACEHOLDER', export_apply=True)
tray.rotation_euler.z = rot; tray.location = loc
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, 'neverflow_tray_walnut.blend'))
print('[build] done', len(me.polygons), 'faces', sum(1 for f in me.polygons if f.material_index == 1), 'engraved')
