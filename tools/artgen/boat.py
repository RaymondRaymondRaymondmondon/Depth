# The Gannet, rebuilt (the Trawl Visual Overhaul Spec, phase 6): a 22 m steam drifter-trawler in her own frame,
# exactly on the layout the game uses (trawl_view3d.cpp's old box model, trawl_data.cpp's stations, trawl_below.cpp's
# hatches and lamps): the hull from HalfBeam3/KeelY, the deck at DECK_Y = 1.2, the engine room floor at -1.3, the
# wheelhouse from x 0.9 to 5.1, the helm wheel at (4.36, DECK_Y + 1.2) radius 0.42, the funnel at (0.6, 1.0).
#
# The game's frame is x forward, y up, z to starboard; Blender's is x forward, y to port, z up. G() converts.
# Big surfaces carry tiling texture sets from tiles.py with UVs in metres; their ambient occlusion (and grime) is
# baked into the vertex colours, which the game reads as occlusion for assets flagged depth_vcao.
#     blender -b --factory-startup -P tools/artgen/boat.py -- --out assets/trawl

import bpy
import bmesh
import math
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from mathutils import Vector, Quaternion
import common as C
import tiles

DECK, ENG = 1.2, -1.3
TOP = DECK + 0.85
X0, X1 = -11.0, 10.8


def HB(x):
    return max(0.5, 3.0 - (x - 5) * 0.42) if x > 5 else (3.0 - (-10 - x) * 0.8 if x < -10 else 3.0)


def KEEL(x):
    # (the old model's keel rose from x 5, which left the fo'c'sle's floor at -1.3 hanging out of her: a near-plumb
    # stem like a real steam drifter's keeps it inside, and meets the old stem head at the bow)
    return -1.9 + (x - 9.0) / 1.8 * 2.2 if x > 9.0 else -1.9


def FOC(x):
    """The fo'c'sle's half-width at x: inside the hull where she narrows."""
    return min(1.95, 0.72 * HB(x))


def G(x, y, z):
    """Game (x forward, y up, z starboard) to Blender (x forward, y port, z up)."""
    return (x, -z, y)


# ---------------------------------------------------------------- materials
MATS = {}
TILE_OF = {}


def _img(name, arr, noncolor):
    h, w = arr.shape[:2]
    im = bpy.data.images.new(name, w, h, alpha=False)
    if noncolor:
        im.colorspace_settings.name = 'Non-Color'   # (before the pixels go in: changing it later regenerates the image)
    rgba = np.ones((h, w, 4), dtype=np.float32)
    rgba[..., :3] = arr[..., :3]
    im.pixels.foreach_set(rgba.ravel())
    im.pack()
    return im


def tile_mat(t):
    m = bpy.data.materials.new(t.name)
    nt, b = C._nodes(m)
    tb = nt.nodes.new('ShaderNodeTexImage'); tb.image = _img(t.name + "_base", t.base, False)
    nt.links.new(tb.outputs['Color'], b.inputs['Base Color'])
    mr = np.zeros(t.base.shape); mr[..., 0] = 1; mr[..., 1] = t.rough; mr[..., 2] = t.metal
    tm = nt.nodes.new('ShaderNodeTexImage'); tm.image = _img(t.name + "_mr", mr, True)
    sep = nt.nodes.new('ShaderNodeSeparateColor')
    nt.links.new(tm.outputs['Color'], sep.inputs['Color'])
    nt.links.new(sep.outputs['Green'], b.inputs['Roughness'])
    nt.links.new(sep.outputs['Blue'], b.inputs['Metallic'])
    tn = nt.nodes.new('ShaderNodeTexImage'); tn.image = _img(t.name + "_nrm", t.normal, True)
    nm = nt.nodes.new('ShaderNodeNormalMap')
    nt.links.new(tn.outputs['Color'], nm.inputs['Color'])
    nt.links.new(nm.outputs['Normal'], b.inputs['Normal'])
    TILE_OF[t.name] = t.size_m
    return m


def flat(name, rgb, rough=0.5, metal=0.0):
    m = C.mat_flat(name, rgb, rough, metal)
    TILE_OF[name] = None
    return m


def setup_mats():
    T = tiles.build_all()
    for k in ("deck", "hull_red", "boot", "bottom", "house", "cream", "iron", "bulk", "chequer"):
        MATS[k] = tile_mat(T[k])
    MATS["brass"] = flat("brass", (0.42, 0.31, 0.15), 0.42, 1.0)
    MATS["glass"] = flat("glass", (0.012, 0.016, 0.02), 0.06, 0.0)
    MATS["rope"] = flat("rope", (0.34, 0.26, 0.15), 0.9)
    MATS["coal"] = flat("coal", (0.018, 0.018, 0.02), 0.55)
    MATS["red"] = flat("funnel band", (0.42, 0.05, 0.035), 0.5)
    MATS["canvas"] = flat("canvas", (0.36, 0.33, 0.25), 0.85)
    MATS["blanket"] = flat("blanket", (0.2, 0.06, 0.05), 0.95)
    MATS["mattress"] = flat("ticking", (0.55, 0.52, 0.44), 0.9)
    MATS["ice"] = flat("ice", (0.55, 0.68, 0.75), 0.15)
    MATS["zinc"] = flat("zinc", (0.2, 0.21, 0.22), 0.55, 0.85)
    MATS["paint_white"] = flat("white paint", (0.62, 0.6, 0.54), 0.5)
    MATS["red_glass"] = flat("port light", (0.35, 0.02, 0.02), 0.1)
    MATS["green_glass"] = flat("starboard light", (0.02, 0.3, 0.06), 0.1)
    MATS["dial"] = flat("dial", (0.75, 0.72, 0.62), 0.4)
    MATS["rubber"] = flat("hose", (0.04, 0.035, 0.03), 0.7)


# ---------------------------------------------------------------- parts (all in the game's frame)
PARTS = []


def put(o, mat):
    C.assign(o, MATS[mat])
    PARTS.append(o)
    return o


def box(c, half, mat, bevel=0.012, rot_y=0.0):
    """A box centred on game point c with half-sizes (along x, up, along z); rot_y turns it about the vertical."""
    o = C.bevelled_box("b", (half[0] * 2, half[2] * 2, half[1] * 2), G(*c), bevel, 2, (0, 0, -rot_y))
    return put(o, mat)


def cyl(a, b, r, mat, verts=16, r2=None, bevel=0.004):
    """A cylinder (or a truncated cone with r2) from game point a to b."""
    a, b = Vector(G(*a)), Vector(G(*b))
    d = b - a
    if r2 is None or abs(r2 - r) < 1e-6:
        bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=d.length, location=(a + b) / 2)
    else:
        bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r, radius2=r2, depth=d.length, location=(a + b) / 2)
    o = bpy.context.active_object
    o.rotation_mode = 'QUATERNION'
    o.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(d.normalized())
    if bevel > 0:
        m = o.modifiers.new("bevel", 'BEVEL'); m.width = bevel; m.segments = 2; m.limit_method = 'ANGLE'
    return put(o, mat)


def rod(pts, r, mat, verts=8, caps=True):
    """A round bar along a polyline of game points (rails, rigging, pipes)."""
    cu = bpy.data.curves.new("rod", 'CURVE')
    cu.dimensions = '3D'
    cu.bevel_depth = r
    cu.bevel_resolution = max(1, verts // 4 - 1)
    cu.use_fill_caps = caps
    sp = cu.splines.new('POLY')
    sp.points.add(len(pts) - 1)
    for i, p in enumerate(pts):
        g = G(*p)
        sp.points[i].co = (g[0], g[1], g[2], 1)
    o = C.link(bpy.data.objects.new("rod", cu))
    C.select_only([o])
    bpy.ops.object.convert(target='MESH')
    return put(bpy.context.active_object, mat)


def disc(c, axis, r, thick, mat, verts=24, hole=0.0):
    """A disc (a wheel, a flange, a dial) centred on c, its face toward the game axis 'x', 'y' or 'z'."""
    a = Vector(G(*c))
    n = {'x': (1, 0, 0), 'y': (0, 1, 0), 'z': (0, 0, 1)}[axis]
    n = Vector(G(*n))
    o = C.cylinder("disc", r, thick, tuple(a), verts=verts, hole=hole)
    o.rotation_mode = 'QUATERNION'
    o.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(n)
    return put(o, mat)


def lathe(c, prof, mat, segs=20):
    """A turned shape about the game's vertical through c: prof is [(radius, height), ...] bottom to top."""
    bm = bmesh.new()
    rings = []
    for (r, h) in prof:
        ring = []
        for k in range(segs):
            a = 2 * math.pi * k / segs
            ring.append(bm.verts.new((c[0] + r * math.cos(a), -c[2] + r * math.sin(a), c[1] + h)))
        rings.append(ring)
    for i in range(len(rings) - 1):
        for k in range(segs):
            k1 = (k + 1) % segs
            bm.faces.new((rings[i][k], rings[i][k1], rings[i + 1][k1], rings[i + 1][k]))
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])
    me = bpy.data.meshes.new("lathe")
    bm.to_mesh(me); bm.free()
    return put(C.link(bpy.data.objects.new("lathe", me)), mat)


def quads(vs, faces, mat, name="mesh"):
    """A mesh from game-frame vertices and quad/tri faces."""
    me = bpy.data.meshes.new(name)
    me.from_pydata([G(*v) for v in vs], [], faces)
    me.validate()
    return put(C.link(bpy.data.objects.new(name, me)), mat)


# ---------------------------------------------------------------- the hull
def hull():
    xs = []
    x = X0
    while x < X1 - 1e-6:
        xs.append(round(x, 4)); x += 0.25
    xs.append(X1)
    # levels from the keel up: keel, chine, a mid bilge, the waterline, the boot-top's upper edge, the deck, the rail
    def levels(x):
        hb, k = HB(x), KEEL(x)
        ch = max(-1.0, k + 0.4)
        return [(k, 0.0), (ch, hb * 0.78), ((ch + 0.0) / 2, hb * 0.9), (0.0, hb * 0.97), (0.16, hb * (0.97 + 0.03 * 0.16 / DECK)),
                (DECK, hb), (TOP, hb)]
    NL = 7
    vs, faces = [], []
    idx = {}
    for i, x in enumerate(xs):
        for s in (-1, 1):
            for k, (y, w) in enumerate(levels(x)):
                idx[(i, s, k)] = len(vs); vs.append((x, y, s * w))
    # the stem: one more section at the cutwater, collapsed onto the centreline
    st = len(xs)
    for k, (y, w) in enumerate(levels(X1)):
        idx[(st, 0, k)] = len(vs); vs.append((X1 + 0.45 * (0.55 + 0.45 * k / (NL - 1)), y + (0.1 if k < NL - 1 else 0.25), 0.0))
    byband = {"bottom": [], "boot": [], "hull_red": []}
    def band(k):
        return "bottom" if k < 3 else ("boot" if k == 3 else "hull_red")
    for i in range(len(xs) - 1):
        for s in (-1, 1):
            for k in range(NL - 1):
                f = (idx[(i, s, k)], idx[(i + 1, s, k)], idx[(i + 1, s, k + 1)], idx[(i, s, k + 1)])
                byband[band(k)].append(f if s > 0 else tuple(reversed(f)))
    for s in (-1, 1):
        i = len(xs) - 1
        for k in range(NL - 1):
            f = (idx[(i, s, k)], idx[(st, 0, k)], idx[(st, 0, k + 1)], idx[(i, s, k + 1)])
            byband[band(k)].append(f if s > 0 else tuple(reversed(f)))
    # the transom: painted above the waterline, antifouled below
    for k in range(NL - 1):
        f = (idx[(0, -1, k)], idx[(0, 1, k)], idx[(0, 1, k + 1)], idx[(0, -1, k + 1)])
        byband[band(k)].append(f)
    for mat, fs in byband.items():
        used = sorted({v for f in fs for v in f})
        remap = {v: n for n, v in enumerate(used)}
        quads([vs[v] for v in used], [tuple(remap[v] for v in f) for f in fs], mat, "hull_" + mat)
    # the bulwark's inner face (cream), the cap rail (oiled wood), stanchions and freeing ports
    inner, cap = [], []
    for s in (-1, 1):
        pts_in = [(x, DECK, s * (HB(x) - 0.07)) for x in xs] + [(X1 + 0.3, DECK, 0.0)]
        n = len(pts_in)
        vs2 = pts_in + [(p[0], TOP, p[2]) for p in pts_in]
        fs2 = []
        for i in range(n - 1):
            f = (i, i + 1, n + i + 1, n + i)
            fs2.append(tuple(reversed(f)) if s > 0 else f)
        quads(vs2, fs2, "cream", "bulwark")
        # the cap: a moulded rail riding the sheer
        rail = [(x, TOP + 0.03, s * (HB(x) - 0.02)) for x in xs] + [(X1 + 0.4, TOP + 0.03, 0.0)]
        rod(rail, 0.065, "house", verts=8)
        for x in [v for v in np.arange(-10.5, 10.0, 1.0)]:
            box((x, (DECK + TOP) / 2, s * (HB(x) - 0.13)), (0.05, (TOP - DECK) / 2, 0.05), "house", 0.008)
        for x in (-8.75, -5.75, -1.75, 2.25, 6.25):
            box((x, DECK + 0.09, s * (HB(x) - 0.06)), (0.32, 0.08, 0.03), "iron", 0.006)
            box((x, DECK + 0.09, s * (HB(x) - 0.045)), (0.27, 0.055, 0.02), "coal", 0.0)
    # the transom's inner face and rail
    quads([(X0 + 0.02, DECK, -HB(X0) + 0.07), (X0 + 0.02, DECK, HB(X0) - 0.07), (X0 + 0.02, TOP, HB(X0) - 0.07), (X0 + 0.02, TOP, -HB(X0) + 0.07)], [(0, 1, 2, 3)], "cream", "transom_in")
    rod([(X0 + 0.02, TOP + 0.03, -HB(X0)), (X0 + 0.02, TOP + 0.03, HB(X0))], 0.065, "house")
    # a rubbing strake along the deck line, and the name on both bows
    for s in (-1, 1):
        rod([(x, DECK - 0.05, s * (HB(x) + 0.03)) for x in xs if x < X1 - 0.3], 0.045, "house")
        bpy.ops.object.text_add(location=(0, 0, 0))
        t = bpy.context.active_object
        t.data.body = "GANNET"
        t.data.size = 0.32
        t.data.extrude = 0.006
        t.data.align_x = 'CENTER'
        C.select_only([t]); bpy.ops.object.convert(target='MESH')
        t = bpy.context.active_object
        x = 8.2
        z = s * (HB(x) + 0.012)
        t.location = G(x, DECK + 0.32, z)
        # facing out and reading from aft to forward on both sides, along the bow's 0.42-per-metre narrowing
        ang = math.atan(0.42)
        t.rotation_euler = (math.radians(90), 0, ang if s > 0 else math.pi - ang)
        put(t, "paint_white")


# ---------------------------------------------------------------- the deck
HATCHES = [((-3.4, -1.8), (0.5, 0.4)), ((-1.0, -1.0), (0.5, 0.5)), ((7.6, 0.0), (0.5, 0.5))]


def in_hatch(x, z, pad=0.0):
    return any(abs(x - c[0]) < h[0] - pad and abs(z - c[1]) < h[1] - pad for c, h in HATCHES)


def deck():
    vs, faces = [], []
    xs = list(np.arange(X0 + 0.07, X1 - 0.05, 0.25)) + [X1 + 0.25]
    N = 24
    grid = {}
    for i, x in enumerate(xs):
        w = HB(min(x, X1)) - 0.07 if x <= X1 else 0.05
        for j in range(N + 1):
            grid[(i, j)] = len(vs); vs.append((x, DECK, -w + 2 * w * j / N))
    for i in range(len(xs) - 1):
        for j in range(N):
            cx = (xs[i] + xs[i + 1]) / 2
            cz = (vs[grid[(i, j)]][2] + vs[grid[(i, j + 1)]][2]) / 2
            if in_hatch(cx, cz, 0.06):
                continue
            faces.append((grid[(i, j)], grid[(i, j + 1)], grid[(i + 1, j + 1)], grid[(i + 1, j)]))
    quads(vs, faces, "deck", "deck")
    # the hatch coamings (the covers are drawn by the game as they stand)
    for (c, h) in HATCHES:
        hx, hz = h[0] + 0.06, h[1] + 0.06
        box((c[0], DECK + 0.08, c[1] - hz), (hx + 0.05, 0.08, 0.05), "house")
        box((c[0], DECK + 0.08, c[1] + hz), (hx + 0.05, 0.08, 0.05), "house")
        box((c[0] - hx, DECK + 0.08, c[1]), (0.05, 0.08, hz), "house")
        box((c[0] + hx, DECK + 0.08, c[1]), (0.05, 0.08, hz), "house")
        # the well below the deck, so the opening has depth
        for sx in (-1, 1):
            box((c[0] + sx * (hx - 0.02), DECK - 0.18, c[1]), (0.02, 0.18, hz), "house", 0.0)
            box((c[0], DECK - 0.18, c[1] + sx * (hz - 0.02)), (hx, 0.18, 0.02), "house", 0.0)


# ---------------------------------------------------------------- the wheelhouse
def wheelhouse():
    y0, y1, y2, y3 = DECK, DECK + 1.05, DECK + 1.75, DECK + 2.2
    for s in (-1, 1):
        # the side: a lower panel, the window band with its posts and glass, the upper band
        box((3.0, (y0 + y1) / 2, s * 2.1), (2.12, (y1 - y0) / 2, 0.05), "house")
        box((3.0, (y2 + y3) / 2, s * 2.1), (2.12, (y3 - y2) / 2, 0.05), "house")
        box((3.0, y1 + 0.02, s * 2.14), (2.16, 0.03, 0.08), "house", 0.01)   # the sill
        for x in np.arange(0.9, 5.11, 1.05):
            box((x, (y1 + y2) / 2, s * 2.1), (0.06, (y2 - y1) / 2, 0.065), "house")
        for x0 in np.arange(0.9, 5.0, 1.05):
            box((x0 + 0.525, (y1 + y2) / 2, s * 2.1), (0.47, (y2 - y1) / 2 - 0.02, 0.012), "glass", 0.0)
        # the side lights: port red, starboard green, on the wheelhouse's forward corners
        box((4.7, y2 + 0.2, s * 2.22), (0.12, 0.12, 0.08), "iron")
        box((4.7, y2 + 0.2, s * 2.3), (0.08, 0.08, 0.01), "green_glass" if s > 0 else "red_glass", 0.0)
    # the front: three windows
    box((5.1, (y0 + y1) / 2, 0), (0.05, (y1 - y0) / 2, 2.15), "house")
    box((5.1, (y2 + y3) / 2, 0), (0.05, (y3 - y2) / 2, 2.15), "house")
    box((5.14, y1 + 0.02, 0), (0.08, 0.03, 2.2), "house", 0.01)
    for z in np.arange(-2.1, 2.11, 1.05):
        box((5.1, (y1 + y2) / 2, z), (0.065, (y2 - y1) / 2, 0.06), "house")
    for z0 in np.arange(-2.1, 2.0, 1.05):
        box((5.1, (y1 + y2) / 2, z0 + 0.525), (0.012, (y2 - y1) / 2 - 0.02, 0.47), "glass", 0.0)
    # the aft wall and its door opening (z -0.7 .. 0.7, 1.9 high)
    box((0.9, (y0 + y3) / 2, -1.4), (0.05, (y3 - y0) / 2, 0.7), "house")
    box((0.9, (y0 + y3) / 2, 1.4), (0.05, (y3 - y0) / 2, 0.7), "house")
    box((0.9, (DECK + 1.9 + y3) / 2, 0), (0.05, (y3 - DECK - 1.9) / 2, 0.7), "house")
    for z in (-0.72, 0.72):
        box((0.86, DECK + 0.95, z), (0.07, 0.95, 0.04), "cream")
    box((0.86, DECK + 1.92, 0), (0.07, 0.04, 0.76), "cream")
    box((0.92, DECK + 0.06, 0), (0.1, 0.06, 0.7), "house")   # the step
    # the roof: planked with an overhang, a lip and a handrail
    box((3.0, y3 + 0.06, 0), (2.32, 0.06, 2.32), "house", 0.02)
    for s in (-1, 1):
        box((3.0, y3 + 0.14, s * 2.25), (2.2, 0.03, 0.03), "house")
        box((5.25, y3 + 0.14, s * 1.1), (0.03, 0.03, 1.1), "house")
        rod([(1.2, y3 + 0.12, s * 1.9), (1.2, y3 + 0.32, s * 1.9), (4.8, y3 + 0.32, s * 1.9), (4.8, y3 + 0.12, s * 1.9)], 0.018, "brass", caps=True)
    # inside: the deckhead lamp's fitting, the floorboards, a bench along the aft wall
    cyl((3.0, y3, 0), (3.0, DECK + 2.16, 0), 0.012, "brass", 6)   # (the game lights the bulb at DECK + 2.05, under the shade)
    lathe((3.0, 0, 0), [(0.12, DECK + 2.07), (0.13, DECK + 2.09), (0.06, DECK + 2.15), (0.015, DECK + 2.17)], "brass", 14)
    box((1.25, DECK + 0.45, -1.5), (0.25, 0.04, 0.5), "house")
    box((1.25, DECK + 0.22, -1.5), (0.2, 0.22, 0.03), "house")


def helm():
    # the binnacle: a turned pedestal with a brass hood (the compass), the wheel's shaft out of its back
    lathe((4.62, 0, 0), [(0.2, DECK), (0.17, DECK + 0.05), (0.13, DECK + 0.12), (0.12, DECK + 0.85), (0.16, DECK + 0.9), (0.16, DECK + 0.95)], "house", 16)
    lathe((4.62, 0, 0), [(0.16, DECK + 0.95), (0.17, DECK + 1.0), (0.15, DECK + 1.12), (0.08, DECK + 1.2), (0.02, DECK + 1.23)], "brass", 16)
    for s in (-1, 1):
        lathe((4.62, 0, s * 0.25), [(0.0001, DECK + 1.0), (0.06, DECK + 1.0), (0.07, DECK + 1.06), (0.06, DECK + 1.12), (0.0001, DECK + 1.12)], "iron", 12)   # the quadrant spheres
    cyl((4.62, DECK + 1.2, 0), (4.36, DECK + 1.2, 0), 0.03, "brass", 12)
    # the wheel: a turned hub, eight spokes through a laminated rim, each with a handle outside it
    c = (4.36, DECK + 1.2, 0.0)
    disc(c, 'x', 0.075, 0.07, "brass", 20)
    disc((4.36, DECK + 1.2, 0), 'x', 0.05, 0.1, "house", 16)
    rim = [(4.36, c[1] + math.sin(2 * math.pi * k / 40) * 0.42, math.cos(2 * math.pi * k / 40) * 0.42) for k in range(41)]
    rod(rim, 0.026, "house", verts=8, caps=False)
    rim2 = [(4.33, c[1] + math.sin(2 * math.pi * k / 40) * 0.42, math.cos(2 * math.pi * k / 40) * 0.42) for k in range(41)]
    rod(rim2, 0.008, "brass", verts=6, caps=False)
    for k in range(8):
        a = k * math.pi / 4
        ca, sa = math.cos(a), math.sin(a)
        rod([(4.36, c[1] + sa * 0.07, ca * 0.07), (4.36, c[1] + sa * 0.42, ca * 0.42)], 0.017, "house", 6)
        p0 = (4.36, c[1] + sa * 0.44, ca * 0.44)
        p1 = (4.36, c[1] + sa * 0.58, ca * 0.58)
        cyl(p0, p1, 0.022, "house", 8, r2=0.016)


def stations():
    # the sonar and radio cabinet (its screen glows on the face at z -1.28), the printer, the clock
    box((2.4, DECK + 0.45, -1.6), (0.45, 0.45, 0.3), "iron")
    box((2.4, DECK + 0.75, -1.29), (0.3, 0.17, 0.01), "brass", 0.004)
    box((2.4, DECK + 0.75, -1.285), (0.26, 0.14, 0.008), "glass", 0.0)
    for k in range(3):
        disc((2.18 + k * 0.22, DECK + 0.38, -1.295), 'z', 0.045, 0.03, "brass", 14)
    box((2.4, DECK + 0.95, -1.6), (0.47, 0.05, 0.32), "house")
    rod([(2.1, DECK + 1.0, -1.7), (2.1, DECK + 1.5, -1.9), (2.2, DECK + 2.2, -2.0)], 0.01, "rubber", 6)   # the aerial lead
    box((2.4, DECK + 0.45, 1.6), (0.4, 0.45, 0.28), "brass", 0.02)
    cyl((2.4, DECK + 0.95, 1.38), (2.4, DECK + 0.95, 1.82), 0.08, "paint_white", 16)   # the tape roll
    box((2.4, DECK + 0.72, 1.31), (0.22, 0.12, 0.01), "dial", 0.0)
    disc((4.0, DECK + 1.55, -2.04), 'z', 0.2, 0.05, "brass", 28)
    disc((4.0, DECK + 1.55, -2.015), 'z', 0.17, 0.02, "dial", 28)
    rod([(4.0, DECK + 1.55, -2.0), (4.0, DECK + 1.67, -2.0)], 0.006, "coal", 4)
    rod([(4.0, DECK + 1.55, -2.0), (4.08, DECK + 1.5, -2.0)], 0.006, "coal", 4)
    # a chart table forward on the starboard side
    box((4.4, DECK + 0.85, 1.75), (0.45, 0.04, 0.3), "house")
    box((4.4, DECK + 0.42, 1.75), (0.43, 0.4, 0.28), "house")
    box((4.4, DECK + 0.9, 1.75), (0.32, 0.005, 0.22), "dial", 0.0)
    # the engine telegraph beside the wheel
    cyl((4.5, DECK, 0.75), (4.5, DECK + 0.9, 0.75), 0.06, "brass", 12)
    disc((4.5, DECK + 1.05, 0.75), 'z', 0.18, 0.12, "brass", 24)
    disc((4.5, DECK + 1.05, 0.69), 'z', 0.15, 0.01, "dial", 24)


def funnel_mast_gantry():
    # the funnel: black with a red band, a flange at the deck, a lip, the steam pipe and whistle
    cyl((0.6, DECK, 1.0), (0.6, DECK + 3.4, 1.0), 0.33, "iron", 24, bevel=0)
    cyl((0.6, DECK + 2.8, 1.0), (0.6, DECK + 3.05, 1.0), 0.338, "red", 24, bevel=0)
    cyl((0.6, DECK, 1.0), (0.6, DECK + 0.08, 1.0), 0.42, "iron", 24)
    cyl((0.6, DECK + 3.33, 1.0), (0.6, DECK + 3.42, 1.0), 0.36, "iron", 24)
    cyl((0.6, DECK + 3.3, 1.0), (0.6, DECK + 3.43, 1.0), 0.3, "coal", 24, bevel=0)
    rod([(0.95, DECK + 0.1, 1.0), (0.95, DECK + 3.0, 1.0), (0.95, DECK + 3.25, 1.0)], 0.03, "iron", 6)
    cyl((0.95, DECK + 3.25, 1.0), (0.95, DECK + 3.5, 1.0), 0.04, "brass", 10, r2=0.05)
    for s in (-1, 1):   # funnel stays
        rod([(0.6 + 0.3 * s, DECK + 3.1, 1.0), (0.6 + 1.6 * s, DECK + 0.1 if s < 0 else DECK + 2.3, 1.0 + 1.2)], 0.008, "iron", 4)
    # the lantern mast: tapered, a yard, a cage round the lamp at 5.45, a truck and a pennant halyard
    cyl((0.2, DECK, 0), (0.2, DECK + 5.9, 0), 0.09, "house", 12, r2=0.06)
    cyl((0.2, DECK, 0), (0.2, DECK + 0.25, 0), 0.13, "iron", 12)
    cyl((0.2, DECK + 5.0, -1.3), (0.2, DECK + 5.0, 1.3), 0.04, "house", 8, r2=0.03)
    box((0.2, DECK + 5.0, 0), (0.07, 0.07, 0.07), "iron")
    lathe((0.2, 0, 0), [(0.11, DECK + 5.62), (0.14, DECK + 5.66), (0.07, DECK + 5.76), (0.03, DECK + 5.8)], "brass", 12)
    for k in range(4):
        a = k * math.pi / 2 + math.pi / 4
        rod([(0.2 + math.cos(a) * 0.14, DECK + 5.3, math.sin(a) * 0.14), (0.2 + math.cos(a) * 0.14, DECK + 5.63, math.sin(a) * 0.14)], 0.008, "brass", 4)
    lathe((0.2, 0, 0), [(0.0001, DECK + 5.27), (0.15, DECK + 5.28), (0.15, DECK + 5.31), (0.0001, DECK + 5.32)], "brass", 12)
    # the rigging: shrouds to the rails, a forestay to the stem head, a backstay to the gantry
    for s in (-1, 1):
        for dx in (-0.5, 0.4):
            rod([(0.2, DECK + 5.0, s * 0.05), (0.2 + dx, TOP + 0.05, s * (HB(0.2 + dx) - 0.05))], 0.009, "rope", 4)
        for k in range(1, 6):   # ratlines
            y = DECK + 0.9 + k * 0.55
            f = (y - TOP) / (DECK + 5.0 - TOP)
            z = s * ((HB(0) - 0.05) * (1 - f) + 0.05 * f)
            rod([(0.2 + -0.5 * (1 - f), y, z), (0.2 + 0.4 * (1 - f), y, z)], 0.006, "rope", 4)
    rod([(0.2, DECK + 5.6, 0), (X1 + 0.35, TOP + 0.2, 0)], 0.009, "rope", 4)
    rod([(0.2, DECK + 5.6, 0), (-10.6, DECK + 3.35, 0)], 0.009, "rope", 4)
    # the stern gantry (an A-frame of iron tubes), its blocks, the stern lamp's bracket
    for s in (-1, 1):
        cyl((-10.6, DECK, s * 2.1), (-10.6, DECK + 3.3, s * 1.5), 0.1, "iron", 12, r2=0.08)
        box((-10.6, DECK + 0.06, s * 2.1), (0.2, 0.06, 0.2), "iron")
        lathe((-10.6, 0, s * 1.4), [(0.0001, DECK + 3.0), (0.12, DECK + 3.05), (0.12, DECK + 3.2), (0.0001, DECK + 3.25)], "iron", 12)   # the towing block
    cyl((-10.6, DECK + 3.3, -1.6), (-10.6, DECK + 3.3, 1.6), 0.09, "iron", 12)
    rod([(-10.6, DECK + 3.3, 0), (-10.45, DECK + 3.25, 0), (-10.4, DECK + 3.25, 0)], 0.02, "iron", 6)
    lathe((-10.4, 0, 0), [(0.05, DECK + 3.0), (0.08, DECK + 3.04), (0.08, DECK + 3.24), (0.04, DECK + 3.3)], "brass", 10)


def net_winch():
    # the trawl drum between two plates, net wound on it; the steam winch forward of it with its brake wheel
    cyl((-9.4, DECK + 0.75, -1.2), (-9.4, DECK + 0.75, 1.2), 0.3, "iron", 20)
    cyl((-9.4, DECK + 0.75, -1.12), (-9.4, DECK + 0.75, 1.12), 0.5, "rope", 20)
    for s in (-1, 1):
        disc((-9.4, DECK + 0.75, s * 1.22), 'z', 0.62, 0.05, "iron", 28)
        box((-9.4, DECK + 0.38, s * 1.3), (0.55, 0.38, 0.04), "iron")
        box((-9.4, DECK + 0.05, s * 1.3), (0.7, 0.05, 0.12), "iron")
    cyl((-9.4, DECK + 0.75, -1.45), (-9.4, DECK + 0.75, 1.45), 0.06, "iron", 12)
    # the winch: a bed, two small cylinders and their rods, a gear, the brake wheel and lever
    box((-8.75, DECK + 0.18, -1.0), (0.35, 0.18, 0.35), "iron")
    for dz in (-0.18, 0.18):
        cyl((-8.75, DECK + 0.36, -1.0 + dz), (-8.75, DECK + 0.75, -1.0 + dz), 0.11, "iron", 14)
        disc((-8.75, DECK + 0.77, -1.0 + dz), 'y', 0.12, 0.04, "brass", 14)
    disc((-9.4, DECK + 0.75, -1.5), 'z', 0.3, 0.05, "iron", 28, hole=0.22)
    rod([(-8.75, DECK + 0.2, -1.4), (-8.6, DECK + 1.1, -1.4)], 0.025, "iron", 6)
    lathe((-8.6, 0, -1.4), [(0.0001, DECK + 1.08), (0.045, DECK + 1.1), (0.045, DECK + 1.2), (0.0001, DECK + 1.22)], "house", 10)


def deck_fittings():
    # the harpoon cannon on its pedestal (fires along +x from 9.3 to 10.6 at DECK + 1.0)
    cyl((9.6, DECK, 0), (9.6, DECK + 0.75, 0), 0.18, "iron", 18, r2=0.13)
    disc((9.6, DECK + 0.03, 0), 'y', 0.28, 0.06, "iron", 20)
    box((9.6, DECK + 0.85, 0), (0.1, 0.1, 0.16), "iron")
    for s in (-1, 1):
        box((9.6, DECK + 0.95, s * 0.14), (0.08, 0.12, 0.025), "iron")
    cyl((9.3, DECK + 1.0, 0), (10.6, DECK + 1.06, 0), 0.09, "iron", 16, r2=0.065)
    cyl((9.15, DECK + 1.0, 0), (9.35, DECK + 1.0, 0), 0.11, "iron", 16)
    cyl((10.45, DECK + 1.055, 0), (10.62, DECK + 1.06, 0), 0.08, "iron", 16)
    rod([(9.1, DECK + 1.0, 0), (8.85, DECK + 1.05, -0.18), (8.75, DECK + 1.05, -0.18)], 0.02, "iron", 6)
    rod([(9.1, DECK + 1.0, 0), (8.85, DECK + 1.05, 0.18), (8.75, DECK + 1.05, 0.18)], 0.02, "iron", 6)
    for s in (-1, 1):
        cyl((8.75, DECK + 1.05, s * 0.18), (8.6, DECK + 1.05, s * 0.18), 0.03, "house", 8)
    # the bell in its little belfry
    cyl((6.0, DECK, 2.05), (6.0, DECK + 1.62, 2.05), 0.045, "house", 10)
    box((6.0, DECK + 1.62, 2.05), (0.05, 0.04, 0.24), "house")
    lathe((6.0, 0, 2.05 - 0.2), [(0.0001, DECK + 1.12), (0.2, DECK + 1.12), (0.18, DECK + 1.18), (0.14, DECK + 1.32), (0.12, DECK + 1.46), (0.08, DECK + 1.52), (0.0001, DECK + 1.54)], "brass", 20)
    rod([(6.0, DECK + 1.6, 2.05 - 0.2), (6.0, DECK + 1.54, 2.05 - 0.2)], 0.015, "iron", 4)
    rod([(6.0, DECK + 1.16, 1.85), (6.0, DECK + 0.95, 1.8), (6.1, DECK + 0.85, 1.75)], 0.01, "rope", 4)
    # the gutting table: zinc top, legs, a lip, a knife on it and a fish box by it
    box((-2.2, DECK + 0.88, 2.2), (0.7, 0.025, 0.35), "zinc", 0.006)
    box((-2.2, DECK + 0.93, 2.53), (0.7, 0.05, 0.02), "house")
    box((-2.2, DECK + 0.84, 2.2), (0.68, 0.03, 0.33), "house")
    for sx in (-1, 1):
        for sz in (-1, 1):
            box((-2.2 + sx * 0.6, DECK + 0.42, 2.2 + sz * 0.28), (0.035, 0.42, 0.035), "house")
    box((-2.2, DECK + 0.22, 2.2), (0.62, 0.02, 0.26), "house")
    box((-2.0, DECK + 0.915, 2.1), (0.12, 0.006, 0.012), "iron", 0.002)
    box((-2.17, DECK + 0.915, 2.1), (0.06, 0.012, 0.016), "house", 0.004)
    # the fish pound beside the main hatch (where the old 'hold hatch' box stood)
    for s in (-1, 1):
        box((-2.2, DECK + 0.15, -0.9 + s * 0.58), (0.8, 0.15, 0.03), "house")
        box((-2.2 + s * 0.78, DECK + 0.15, -0.9), (0.03, 0.15, 0.58), "house")
    box((-2.2, DECK + 0.03, -0.9), (0.75, 0.02, 0.55), "zinc", 0.0)
    # the deck locker (a sea chest with brass hinges), the air pump (a cased double pump with flywheels)
    box((-6.4, DECK + 0.38, -2.35), (0.5, 0.38, 0.25), "house")
    box((-6.4, DECK + 0.79, -2.35), (0.53, 0.04, 0.27), "house")
    for dx in (-0.3, 0.3):
        box((-6.4 + dx, DECK + 0.8, -2.08), (0.06, 0.02, 0.01), "brass", 0.003)
    box((-6.4, DECK + 0.6, -2.09), (0.05, 0.06, 0.01), "brass", 0.003)
    box((-6.2, DECK + 0.4, 2.5), (0.3, 0.4, 0.2), "house")
    box((-6.2, DECK + 0.82, 2.5), (0.32, 0.03, 0.22), "brass")
    for s in (-1, 1):
        disc((-6.2 + s * 0.34, DECK + 0.55, 2.5), 'x', 0.3, 0.04, "iron", 28, hole=0.24)
        for k in range(4):
            a = k * math.pi / 4
            rod([(-6.2 + s * 0.34, DECK + 0.55 + math.sin(a) * 0.27, 2.5 + math.cos(a) * 0.27), (-6.2 + s * 0.34, DECK + 0.55 - math.sin(a) * 0.27, 2.5 - math.cos(a) * 0.27)], 0.012, "iron", 4)
        cyl((-6.2 + s * 0.34, DECK + 0.55 + 0.25, 2.5), (-6.2 + s * 0.48, DECK + 0.55 + 0.25, 2.5), 0.02, "house", 6)
    disc((-6.2, DECK + 0.6, 2.29), 'z', 0.07, 0.02, "dial", 18)
    rod([(-6.2, DECK + 0.3, 2.3), (-6.0, DECK + 0.05, 2.0), (-5.6, DECK + 0.03, 1.4), (-5.0, DECK + 0.03, 1.6)], 0.022, "rubber", 6, caps=True)
    # rod holders at the four rod stations (the rods are drawn from DECK + 0.75)
    for at in ((-0.8, -2.55), (-0.8, 2.55), (-10.2, -1.7), (-10.2, 1.7)):
        cyl((at[0], DECK, at[1]), (at[0], DECK + 0.75, at[1]), 0.045, "iron", 10)
        cyl((at[0], DECK + 0.62, at[1]), (at[0], DECK + 0.76, at[1]), 0.06, "brass", 12)
        box((at[0], DECK + 0.03, at[1]), (0.1, 0.03, 0.1), "iron")
    # bollards and fairleads, fore and aft and amidships
    for (x, z) in ((9.0, -1.2), (9.0, 1.2), (-9.9, -2.55), (-9.9, 2.55), (-4.6, -2.65), (-4.6, 2.65), (5.6, -2.55), (5.6, 2.55)):
        for dx in (-0.14, 0.14):
            cyl((x + dx, DECK, z), (x + dx, DECK + 0.32, z), 0.07, "iron", 12)
            disc((x + dx, DECK + 0.33, z), 'y', 0.09, 0.03, "iron", 12)
        box((x, DECK + 0.04, z), (0.26, 0.04, 0.1), "iron")
    # the engine room's hatch ladder, the hold's and the fo'c'sle's (brass rails and rungs)
    for (lx, lz, z0) in ((-3.4, -2.05, ENG), (-1.0, -1.0, ENG), (7.6, 0.0, ENG)):
        for s in (-1, 1):
            cyl((lx + s * 0.25, z0, lz), (lx + s * 0.25, DECK + 0.6, lz), 0.022, "brass", 8)
        y = z0 + 0.3
        while y < DECK:
            cyl((lx - 0.25, y, lz), (lx + 0.25, y, lz), 0.016, "brass", 8)
            y += 0.3


def clutter():
    # fish boxes stacked against the starboard bulwark, a coil of rope, a barrel, a bucket, a tarpaulin
    for k, (x, z, y, r) in enumerate(((-4.4, 2.45, 0, 0.0), (-4.42, 2.45, 0.24, 0.06), (-3.8, 2.5, 0, -0.08), (-4.1, 2.47, 0.48, 0.15))):
        box((x, DECK + 0.12 + y, z), (0.3, 0.11, 0.22), "house", 0.008, rot_y=r)
    for (x, z, rr) in ((-4.7, -2.4, 0.32), (7.0, -1.4, 0.26), (-8.3, 2.0, 0.28)):
        for k in range(4):
            pts = [(x + math.cos(a) * (rr - k * 0.03), DECK + 0.03 + k * 0.045, z + math.sin(a) * (rr - k * 0.03)) for a in np.linspace(0, 2 * math.pi, 18)]
            rod(pts, 0.024, "rope", 6, caps=False)
    lathe((-7.6, 0, -2.4), [(0.0001, DECK), (0.22, DECK), (0.26, DECK + 0.3), (0.22, DECK + 0.6), (0.0001, DECK + 0.6)], "house", 16)
    for h in (0.12, 0.48):
        lathe((-7.6, 0, -2.4), [(0.235 + 0.025 * (1 - abs(h - 0.3) / 0.3), DECK + h - 0.02), (0.24 + 0.03 * (1 - abs(h - 0.3) / 0.3), DECK + h + 0.02)], "iron", 16)
    lathe((7.0, 0, 1.3), [(0.0001, DECK), (0.12, DECK), (0.15, DECK + 0.28), (0.13, DECK + 0.28), (0.1, DECK + 0.02), (0.0001, DECK + 0.02)], "zinc", 14)
    box((-5.4, DECK + 0.1, -2.5), (0.45, 0.1, 0.32), "canvas", 0.06)
    # fenders over the side
    for (x, s) in ((-6.0, -1), (-1.5, -1), (3.0, -1), (-3.5, 1), (2.0, 1)):
        z = s * (HB(x) + 0.12)
        cyl((x, 0.35, z), (x, 0.85, z), 0.11, "rope", 12, r2=0.11)
        rod([(x, 0.86, z), (x, TOP + 0.05, s * (HB(x) + 0.02)), (x, TOP + 0.05, s * (HB(x) - 0.12))], 0.012, "rope", 4)
    # the stowed anchor on the foredeck
    rod([(8.2, DECK + 0.06, -0.9), (9.1, DECK + 0.06, -0.9)], 0.04, "iron", 8)
    rod([(8.2, DECK + 0.06, -1.25), (8.15, DECK + 0.06, -0.9), (8.2, DECK + 0.06, -0.55)], 0.035, "iron", 8)
    rod([(9.1, DECK + 0.06, -0.9), (9.4, DECK + 0.04, -0.85), (9.6, DECK + 0.03, -0.75)], 0.02, "iron", 6)


def below():
    bulk = "bulk"
    # the floors: chequer plate in the engine room, boards in the hold and the fo'c'sle
    box((-5.65, ENG - 0.05, 0), (2.9, 0.05, 2.35), "chequer", 0.0)
    box((-1.0, ENG - 0.05, 0), (1.8, 0.05, 2.35), "deck", 0.0)
    fx = list(np.linspace(5.05, 9.1, 9))
    quads([(x, ENG - 0.0, s * FOC(x)) for s in (-1, 1) for x in fx], [(k, k + 1, 9 + k + 1, 9 + k) for k in range(8)], "deck", "foc_floor")
    def wall_x(x, z0, z1, y0, y1):
        box((x, (y0 + y1) / 2, (z0 + z1) / 2), (0.05, (y1 - y0) / 2, (z1 - z0) / 2), bulk, 0.0)
    def wall_z(z, x0, x1, y0, y1):
        box(((x0 + x1) / 2, (y0 + y1) / 2, z), ((x1 - x0) / 2, (y1 - y0) / 2, 0.05), bulk, 0.0)
    wall_x(-2.75, -2.35, -0.6, ENG, DECK); wall_x(-2.75, 0.6, 2.35, ENG, DECK); wall_x(-2.75, -0.6, 0.6, ENG + 1.9, DECK)
    for s in (-1, 1):   # the watertight door's frame and dogs
        box((-2.72, ENG + 0.95, s * 0.64), (0.06, 0.95, 0.05), "iron")
    box((-2.72, ENG + 1.93, 0), (0.06, 0.05, 0.68), "iron")
    wall_x(-8.55, -2.35, 2.35, ENG, DECK)
    wall_x(0.95, -2.35, 2.35, ENG, DECK)
    for s in (-1, 1):
        wall_z(s * 2.35, -8.55, 0.95, ENG, DECK)
    wall_x(5.05, -1.95, 1.95, ENG, DECK); wall_x(9.1, -FOC(9.1), FOC(9.1), ENG, DECK)
    for s in (-1, 1):   # the side linings follow her flare in
        vs = [(x, y, s * FOC(x)) for y in (ENG, DECK) for x in fx]
        quads(vs, [(k, k + 1, 9 + k + 1, 9 + k) for k in range(8)], bulk, "foc_side")
    # deck beams overhead and frames on the walls
    for x in np.arange(-8.3, 0.9, 0.7):
        box((x, DECK - 0.09, 0), (0.06, 0.08, 2.3), "house", 0.006)
        for s in (-1, 1):
            box((x, (ENG + DECK) / 2, s * 2.28), (0.05, (DECK - ENG) / 2, 0.04), "iron", 0.004)
    for x in np.arange(5.3, 9.0, 0.7):
        box((x, DECK - 0.09, 0), (0.06, 0.08, FOC(x) - 0.03), "house", 0.006)
    # the boiler: a Scotch boiler along the port side, its furnace door on the inboard face (the glow at -4.8)
    cyl((-5.8, ENG + 0.75, -1.55), (-3.8, ENG + 0.75, -1.55), 0.68, "iron", 28, bevel=0)
    for x in (-5.8, -3.8):
        disc((x, ENG + 0.75, -1.55), 'x', 0.7, 0.06, "iron", 28)
        for k in range(14):   # rivet rings on the end plates
            a = 2 * math.pi * k / 14
            box((x + (0.035 if x > -5 else -0.035), ENG + 0.75 + math.sin(a) * 0.62, -1.55 + math.cos(a) * 0.62), (0.012, 0.02, 0.02), "iron", 0.006)
    box((-4.8, ENG + 0.55, -0.88), (0.34, 0.25, 0.03), "iron")
    box((-4.8, ENG + 0.55, -0.86), (0.27, 0.19, 0.012), "coal", 0.0)
    box((-4.8, ENG + 0.88, -0.87), (0.36, 0.03, 0.04), "brass")
    rod([(-4.4, ENG + 0.62, -0.84), (-4.25, ENG + 0.62, -0.78)], 0.012, "iron", 4)
    lathe((-4.8, 0, -1.55), [(0.0001, ENG + 1.4), (0.25, ENG + 1.4), (0.18, ENG + 1.75), (0.14, DECK)], "iron", 16)   # the uptake
    # its gauge glass and pressure gauge (the game lights the dial at (-4.0, ENG + 1.2, -0.85))
    cyl((-4.0, ENG + 1.0, -0.95), (-4.0, ENG + 1.2, -0.95), 0.02, "brass", 8)
    disc((-4.0, ENG + 1.2, -0.88), 'z', 0.12, 0.05, "brass", 24)
    disc((-4.0, ENG + 1.2, -0.85), 'z', 0.1, 0.01, "dial", 24)
    cyl((-4.3, ENG + 0.9, -0.88), (-4.3, ENG + 1.4, -0.88), 0.025, "glass", 8)
    # steam pipes from the dome to the engine along the deckhead
    rod([(-4.6, ENG + 1.43, -1.55), (-4.6, DECK - 0.25, -1.55), (-6.3, DECK - 0.25, -1.55), (-6.3, DECK - 0.25, 0.8), (-6.3, ENG + 1.1, 0.8)], 0.05, "iron", 8)
    # the coal bunker with its heap, a shovel against it
    box((-7.4, ENG + 0.6, -1.3), (0.7, 0.6, 0.8), "iron")
    box((-6.68, ENG + 0.3, -1.3), (0.03, 0.3, 0.5), "coal", 0.0)
    for k in range(9):
        a = k * 0.7
        lathe((-6.5 + 0.12 * math.cos(a), 0, -1.3 + 0.35 * math.sin(a * 1.3)), [(0.0001, ENG), (0.16, ENG), (0.0001, ENG + 0.12 + 0.05 * (k % 3))], "coal", 8)
    rod([(-6.62, ENG + 0.05, -0.6), (-6.66, ENG + 1.1, -0.65)], 0.018, "house", 6)
    box((-6.6, ENG + 0.06, -0.55), (0.12, 0.015, 0.14), "iron", 0.004, rot_y=0.2)
    # the engine: a bedplate, two vertical cylinders on columns, the crank and a flywheel
    box((-6.3, ENG + 0.12, 0.8), (1.0, 0.12, 0.45), "iron")
    for dx in (-0.35, 0.35):
        for dz in (-0.3, 0.3):
            cyl((-6.3 + dx, ENG + 0.24, 0.8 + dz), (-6.3 + dx, ENG + 0.85, 0.8 + dz), 0.04, "iron", 8)
        cyl((-6.3 + dx, ENG + 0.85, 0.8), (-6.3 + dx, ENG + 1.3, 0.8), 0.22, "iron", 20)
        disc((-6.3 + dx, ENG + 1.31, 0.8), 'y', 0.24, 0.04, "brass", 20)
        cyl((-6.3 + dx, ENG + 0.4, 0.8), (-6.3 + dx, ENG + 0.85, 0.8), 0.035, "brass", 8)
    cyl((-7.15, ENG + 0.4, 0.8), (-5.45, ENG + 0.4, 0.8), 0.07, "iron", 12)
    disc((-7.25, ENG + 0.6, 0.8), 'x', 0.55, 0.1, "iron", 32, hole=0.45)
    for k in range(6):
        a = k * math.pi / 6
        rod([(-7.25, ENG + 0.6 + math.sin(a) * 0.46, 0.8 + math.cos(a) * 0.46), (-7.25, ENG + 0.6 - math.sin(a) * 0.46, 0.8 - math.cos(a) * 0.46)], 0.025, "iron", 6)
    # the bilge pump with its lever, a hose into the bilge
    box((-7.0, ENG + 0.35, 1.9), (0.22, 0.35, 0.22), "iron")
    cyl((-7.0, ENG + 0.7, 1.9), (-7.0, ENG + 0.95, 1.9), 0.06, "brass", 12)
    rod([(-7.0, ENG + 0.95, 1.9), (-6.6, ENG + 1.25, 1.7)], 0.025, "iron", 6)
    lathe((-6.6, 0, 1.7), [(0.0001, ENG + 1.22), (0.035, ENG + 1.24), (0.035, ENG + 1.36), (0.0001, ENG + 1.38)], "house", 8)
    rod([(-7.0, ENG + 0.1, 1.68), (-7.4, ENG + 0.02, 1.3), (-8.0, ENG + 0.02, 1.0)], 0.03, "rubber", 6)
    # the hold: ice pounds along the starboard side, shelving boards, a shovel
    for k in range(3):
        box((-2.2 + k * 1.2, ENG + 0.35, 1.6), (0.5, 0.35, 0.6), "ice", 0.06)
        box((-2.2 + k * 1.2 - 0.55, ENG + 0.4, 1.6), (0.03, 0.4, 0.65), "house")
    box((-1.0, ENG + 0.8, -2.0), (1.6, 0.03, 0.3), "house")
    # the fo'c'sle: bunks either side with mattresses and blankets, the magazine locker, the Medic's cot
    for s in (-1, 1):
        for k in range(2):
            bx = 5.9 + k * 1.1
            bz = min(1.5, FOC(bx + 0.55) - 0.32)   # (the forward pair sits in where she narrows)
            for lv in (0.5, 1.5):
                box((bx, ENG + lv, s * bz), (0.52, 0.06, 0.3), "house")
                box((bx, ENG + lv + 0.1, s * bz), (0.48, 0.05, 0.26), "mattress", 0.03)
                box((bx + 0.15, ENG + lv + 0.16, s * bz), (0.31, 0.025, 0.27), "blanket", 0.02)
                box((bx, ENG + lv + 0.14, s * (bz - 0.3)), (0.52, 0.1, 0.02), "house")
    box((8.3, ENG + 0.6, 0.8), (0.35, 0.6, 0.25), "iron")
    box((8.3, ENG + 0.9, 0.54), (0.22, 0.04, 0.02), "brass", 0.004)
    disc((8.15, ENG + 0.6, 0.54), 'z', 0.05, 0.03, "brass", 12)
    box((6.0, ENG + 0.4, -1.2), (0.7, 0.06, 0.3), "paint_white")
    for sx in (-1, 1):
        for sz in (-1, 1):
            box((6.0 + sx * 0.62, ENG + 0.2, -1.2 + sz * 0.24), (0.025, 0.2, 0.025), "iron")
    box((6.0, ENG + 0.5, -1.2), (0.6, 0.04, 0.24), "mattress", 0.03)
    # the oil lamps' brackets (the game lights them at DECK - 0.3)
    for (x, z) in ((-6.6, 1.6), (-1.0, 1.5), (6.4, 0.9)):
        rod([(x, DECK - 0.05, z), (x, DECK - 0.2, z)], 0.008, "brass", 4)
        lathe((x, 0, z), [(0.04, DECK - 0.42), (0.06, DECK - 0.38), (0.05, DECK - 0.22), (0.02, DECK - 0.2)], "brass", 10)


# ---------------------------------------------------------------- finishing: join, UVs, occlusion, export
def finish(out):
    for o in PARTS:
        C.apply_all(o)
    groups = {}
    for o in PARTS:
        groups.setdefault(o.data.materials[0].name, []).append(o)
    joined = []
    for name, objs in groups.items():
        C.select_only(objs)
        bpy.ops.object.join()
        j = bpy.context.active_object
        j.name = "boat_" + name.replace(" ", "_")
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        joined.append(j)
    # UVs in metres: each face projected along its dominant axis, scaled by the material's tile size
    for j in joined:
        sz = TILE_OF.get(j.data.materials[0].name)
        su, sv = sz if sz else (1.0, 1.0)
        me = j.data
        while len(me.uv_layers) > 1:
            me.uv_layers.remove(me.uv_layers[-1])
        layer = me.uv_layers[0] if me.uv_layers else me.uv_layers.new(name="UVMap")
        me.uv_layers.active = layer
        uv = layer.data
        for p in me.polygons:
            n = p.normal
            ax = max(range(3), key=lambda i: abs(n[i]))
            for li in p.loop_indices:
                v = me.vertices[me.loops[li].vertex_index].co
                if ax == 2:
                    uv[li].uv = (v.x / su, v.y / sv)
                elif ax == 1:
                    uv[li].uv = (v.x / su, v.z / sv)
                else:
                    uv[li].uv = (v.y / su, v.z / sv)
        C.smooth(j, 35)
    # ambient occlusion into a vertex colour (linear), then grime: soot on the funnel, dirt along the waterways
    sc = bpy.context.scene
    sc.world = sc.world or bpy.data.worlds.new("world")
    sc.world.light_settings.distance = 0.7
    sc.cycles.samples = 48
    for j in joined:
        me = j.data
        a = me.color_attributes.new("AO", 'FLOAT_COLOR', 'POINT')
        me.color_attributes.active_color = a
        try:
            me.color_attributes.render_color_index = me.color_attributes.active_color_index
        except Exception:
            pass
    C.select_only(joined)
    r = bpy.ops.object.bake(type='AO', target='VERTEX_COLORS')
    print("artgen: bake boat AO", r)
    for j in joined:
        me = j.data
        a = me.color_attributes["AO"]
        for i, v in enumerate(me.vertices):
            x, y, z = v.co.x, v.co.z, -v.co.y   # back to the game's frame
            ao = a.data[i].color[0]
            k = 0.2 + 0.8 * ao
            if math.hypot(x - 0.6, z - 1.0) < 0.5 and y > DECK + 2.2:      # soot on the funnel's top
                k *= 1.0 - 0.6 * min(1.0, (y - DECK - 2.2) / 1.2)
            if abs(y - DECK) < 0.02 and abs(z) > HB(x) - 0.6:              # grime along the waterways
                k *= 0.7 + 0.3 * min(1.0, (HB(x) - abs(z)) / 0.6)
            k = max(0.08, min(1.0, k))
            a.data[i].color = (k, k, k, 1.0)
    sc["depth_vcao"] = 1
    path = os.path.join(out, "boat.glb")
    C.select_only(joined)
    kw = dict(filepath=path, export_format='GLB', use_selection=True, export_apply=True, export_yup=True,
              export_texcoords=True, export_normals=True, export_tangents=False, export_materials='EXPORT',
              export_image_format='JPEG', export_extras=True)
    try:
        bpy.ops.export_scene.gltf(**kw, export_vertex_color='ACTIVE', export_jpeg_quality=88)
    except TypeError:
        bpy.ops.export_scene.gltf(**kw, export_colors=True)
    tris = sum(len(p.vertices) - 2 for j in joined for p in j.data.polygons)
    print(f"artgen: wrote {path} ({len(joined)} materials, {tris} triangles)")


def main():
    out = C.out_dir()
    C.reset()
    setup_mats()
    hull(); deck(); wheelhouse(); helm(); stations(); funnel_mast_gantry(); net_winch(); deck_fittings(); clutter(); below()
    finish(out)


main()
