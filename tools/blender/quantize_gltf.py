# Rewrites a Blender .glb as a quantized, self-contained glTF JSON (KHR_mesh_quantization):
# positions int16 (node transform restores mm), normals int8, UVs uint16, indices uint16.
# usage: python quantize_gltf.py in.glb out.json
import base64, json, struct, sys
import numpy as np

src, dst = sys.argv[1], sys.argv[2]
b = open(src, 'rb').read()
jl = struct.unpack('<I', b[12:16])[0]
g = json.loads(b[20:20 + jl])
o = 20 + jl
binc = b[o + 8:o + 8 + struct.unpack('<I', b[o:o + 4])[0]]
CT = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}
NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}

def read(ai):
    a = g['accessors'][ai]; bv = g['bufferViews'][a['bufferView']]
    n, c = a['count'], NC[a['type']]
    dt = np.dtype(CT[a['componentType']])
    start = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
    per = bv.get('byteStride', dt.itemsize * c) // dt.itemsize      # Blender writes tightly packed views; stride kept for safety
    return np.frombuffer(binc, dt, offset=start, count=per * (n - 1) + c)[np.arange(n)[:, None] * per + np.arange(c)]

prims = [p for m in g['meshes'] for p in m['primitives']]
allpos = np.concatenate([read(p['attributes']['POSITION']).reshape(-1, 3) for p in prims])
lo, hi = allpos.min(0), allpos.max(0)
center = (lo + hi) / 2
scale = float((hi - lo).max() / 2)

out = bytearray(); views = []; accs = []
def push(arr, target=None, stride=None):
    while len(out) % 4: out.append(0)
    v = {'buffer': 0, 'byteOffset': len(out), 'byteLength': arr.nbytes}
    if target: v['target'] = target
    if stride: v['byteStride'] = stride
    out.extend(arr.tobytes()); views.append(v); return len(views) - 1
def acc(view, ctype, count, typ, normalized=False, mn=None, mx=None):
    a = {'bufferView': view, 'componentType': ctype, 'count': count, 'type': typ}
    if normalized: a['normalized'] = True
    if mn is not None: a['min'] = mn; a['max'] = mx
    accs.append(a); return len(accs) - 1

for p in prims:
    at = p['attributes']; new = {}
    pos = read(at['POSITION']).reshape(-1, 3)
    q = np.round((pos - center) / scale * 32767).astype(np.int16)
    q4 = np.zeros((len(q), 4), np.int16); q4[:, :3] = q
    new['POSITION'] = acc(push(q4, 34962, 8), 5122, len(q), 'VEC3', True, (q.min(0) / 32767).tolist(), (q.max(0) / 32767).tolist())
    nor = read(at['NORMAL']).reshape(-1, 3)
    n4 = np.zeros((len(nor), 4), np.int8); n4[:, :3] = np.round(np.clip(nor, -1, 1) * 127).astype(np.int8)
    new['NORMAL'] = acc(push(n4, 34962, 4), 5120, len(nor), 'VEC3', True)
    for k in ('TEXCOORD_0', 'TEXCOORD_1'):
        uv = read(at[k]).reshape(-1, 2)
        if uv.min() < 0 or uv.max() > 1:                    # keep exact floats if a UV leaves [0, 1]
            new[k] = acc(push(uv.astype(np.float32).copy(), 34962, 8), 5126, len(uv), 'VEC2')
        else:
            new[k] = acc(push(np.round(uv * 65535).astype(np.uint16), 34962, 4), 5123, len(uv), 'VEC2', True)
    idx = read(p['indices']).reshape(-1)
    p['indices'] = acc(push(idx.astype(np.uint16 if idx.max() < 65536 else np.uint32), 34963), 5123 if idx.max() < 65536 else 5125, len(idx), 'SCALAR')
    p['attributes'] = new

g['accessors'], g['bufferViews'] = accs, views
g['buffers'] = [{'byteLength': len(out), 'uri': 'data:application/octet-stream;base64,' + base64.b64encode(bytes(out)).decode()}]
for n in g['nodes']:
    if 'mesh' in n:
        n['translation'] = center.tolist(); n['scale'] = [scale] * 3; n.pop('rotation', None)
for k in ('extensionsUsed', 'extensionsRequired'):
    g[k] = sorted(set(g.get(k, [])) | {'KHR_mesh_quantization'})
s = json.dumps(g, separators=(',', ':'))
open(dst, 'w').write(s)
print(f'{src}: {len(b)} B -> {dst}: {len(s)} B (binary {len(out)} B), scale {scale:.5f} m')
