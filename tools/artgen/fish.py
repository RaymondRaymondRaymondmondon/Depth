# The fish (the Trawl Visual Overhaul Spec, phase 6): body archetypes the species are drawn with, each a unit-length
# fish along the game's +z (its head at +0.5, as the old unit fish), skinned to a four-bone spine (s0 at the head to
# s3 in the tail) so the game can swim and flop it. Materials by name, for the game to recolour per species: 'back'
# (above the lateral line), 'belly' (below it, silvered), 'fin'; 'eye' stays. Scales are a tiling normal map with
# a silver sheen (a metallic belly catches the sky, which is the cheap iridescence); fins carry rays.
#     blender -b --factory-startup -P tools/artgen/fish.py -- --out assets/trawl/fish

import bpy
import bmesh
import math
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from mathutils import Vector
import common as C
import tiles
import boat as B
from boat import G

ARCH = {}


def arch(name):
    def deco(f):
        ARCH[name] = f
        return f
    return deco


# ---------------------------------------------------------------- textures
def scale_tile(seed, rows=10):
    """Overlapping scales: rows of arcs, each scale raised toward its free (tail-ward) edge."""
    w = h = 256
    yy, xx = np.mgrid[:h, :w].astype(float)
    cell = w / rows
    hgt = np.zeros((h, w))
    for off in (0.0, 0.5):
        cy = (np.floor(yy / cell - off) + off + 0.5) * cell
        cx = (np.floor(xx / cell) + 0.5) * cell
        d = np.sqrt(((xx - cx) / cell) ** 2 + ((yy - cy) / cell) ** 2)
        hgt = np.maximum(hgt, np.clip(1 - d * 1.6, 0, 1) * (0.6 + 0.4 * ((yy - cy) / cell + 0.5)))
    n = tiles.fbm(h, w, 30, seed, 3)
    base = np.ones((h, w, 3)) * (0.82 + 0.18 * hgt[..., None]) * (0.9 + 0.1 * n[..., None])
    return tiles.Tile("scales", base, 0.3 + 0.2 * (1 - hgt), np.full((h, w), 1.0), hgt, 3.0, (0.25, 0.25))


def fin_tile(seed):
    w = h = 256
    xx = np.mgrid[:h, :w][1].astype(float)
    rays = 0.5 + 0.5 * np.cos(xx / w * 2 * np.pi * 14)
    n = tiles.fbm(h, w, 20, seed, 3)
    base = np.ones((h, w, 3)) * (0.7 + 0.3 * rays[..., None]) * (0.85 + 0.15 * n[..., None])
    return tiles.Tile("finrays", base, 0.45 + 0.1 * rays, np.zeros((h, w)), rays * 0.5, 2.0, (0.15, 0.15))


def setup():
    st, ft = scale_tile(5), fin_tile(6)
    mats = {}
    proto = B.tile_mat(st)
    for name, metal in (("back", 0.35), ("belly", 0.75)):
        m = proto.copy(); m.name = name   # (the copies share the scale images)
        nt = m.node_tree
        b = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
        mats[name] = m
        B.TILE_OF[name] = st.size_m
        # (the metallic map is all ones: the factor carries how silvery this part is)
        sep = next(n for n in nt.nodes if n.type == 'SEPARATE_COLOR')
        mul = nt.nodes.new('ShaderNodeMath'); mul.operation = 'MULTIPLY'; mul.inputs[1].default_value = metal
        nt.links.new(sep.outputs['Blue'], mul.inputs[0]); nt.links.new(mul.outputs['Value'], b.inputs['Metallic'])
    fm = B.tile_mat(ft); fm.name = "fin"; mats["fin"] = fm; B.TILE_OF["fin"] = ft.size_m
    mats["eye"] = C.mat_flat("eye", (0.01, 0.01, 0.012), 0.08, 0.0)
    mats["iris"] = C.mat_flat("iris", (0.55, 0.42, 0.12), 0.3, 0.6)
    mats["mouth"] = C.mat_flat("mouth", (0.08, 0.04, 0.04), 0.6)
    return mats


# ---------------------------------------------------------------- shapes (game frame: x left-right, y up, z forward)
def loft(prof, n=28, segs=18, e=0.75):
    """A body lofted along z from -0.5 (tail) to +0.5 (snout) through superelliptic sections: prof(u) for u in
    0..1 (tail to snout) gives (half width, half height, centre height). Returns (object, lateral-line height fn)."""
    bm = bmesh.new()
    rings = []
    for i in range(n + 1):
        u = i / n
        z = -0.5 + u
        hw, hh, cy = prof(u)
        hw, hh = max(hw, 0.002), max(hh, 0.002)
        ring = []
        for k in range(segs):
            a = 2 * math.pi * k / segs
            c, s = math.cos(a), math.sin(a)
            x = hw * math.copysign(abs(c) ** e, c)
            y = cy + hh * math.copysign(abs(s) ** e, s)
            ring.append(bm.verts.new(G(x, y, z)))
        rings.append(ring)
    for i in range(n):
        for k in range(segs):
            k1 = (k + 1) % segs
            bm.faces.new((rings[i][k], rings[i][k1], rings[i + 1][k1], rings[i + 1][k]))
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])
    me = bpy.data.meshes.new("body")
    bm.to_mesh(me); bm.free()
    o = C.link(bpy.data.objects.new("body", me))
    sub = o.modifiers.new("smooth", 'SUBSURF'); sub.levels = 1
    C.apply_all(o)
    return o


def split_body(o, prof, mats, line=0.1):
    """Back above the lateral line (a little above the centre), belly below it."""
    o.data.materials.append(mats["back"]); o.data.materials.append(mats["belly"])
    for p in o.data.polygons:
        c = p.center   # Blender: z up, -y forward
        u = min(1, max(0, -c.y + 0.5))
        hw, hh, cy = prof(u)
        p.material_index = 0 if c.z > cy + hh * line else 1
    return o


def fin(pts, mats, thick=0.004, name="fin"):
    """A fin: a flat polygon in game coordinates (it lies in whatever plane its points do), thickened a little."""
    me = bpy.data.meshes.new(name)
    me.from_pydata([G(*p) for p in pts], [], [list(range(len(pts)))])
    me.validate()
    o = C.link(bpy.data.objects.new(name, me))
    s = o.modifiers.new("solid", 'SOLIDIFY'); s.thickness = thick; s.offset = 0
    C.apply_all(o)
    C.assign(o, mats["fin"])
    return o


def eye(at, r, mats, side):
    """An eye on the head at game point 'at' (x the side): a black glossy ball in a gold ring."""
    bpy.ops.mesh.primitive_uv_sphere_add(segments=14, ring_count=8, radius=r, location=G(*at))
    e = bpy.context.active_object; C.assign(e, mats["eye"])
    bpy.ops.mesh.primitive_torus_add(major_radius=r * 1.05, minor_radius=r * 0.28, major_segments=16, minor_segments=6,
                                     location=G(at[0] - side * r * 0.25, at[1], at[2]), rotation=(0, math.pi / 2, 0))
    i = bpy.context.active_object; C.assign(i, mats["iris"])
    return [e, i]


def gill(z, prof, mats, depth=0.85):
    """The gill cover's back edge: a curved ridge round the head at z (each side)."""
    out = []
    u = z + 0.5
    hw, hh, cy = prof(u)
    for s in (-1, 1):
        pts = []
        for k in range(9):
            a = -1.2 + 2.4 * k / 8
            pts.append(G(s * hw * 1.0 * math.cos(a) ** 0.6, cy + hh * depth * math.sin(a), z - 0.015 * math.cos(a)))
        cu = bpy.data.curves.new("gill", 'CURVE'); cu.dimensions = '3D'; cu.bevel_depth = 0.006; cu.bevel_resolution = 1
        sp = cu.splines.new('POLY'); sp.points.add(len(pts) - 1)
        for i, p in enumerate(pts):
            sp.points[i].co = (p[0], p[1], p[2], 1)
        o = C.link(bpy.data.objects.new("gill", cu)); C.select_only([o]); bpy.ops.object.convert(target='MESH')
        g = bpy.context.active_object; C.assign(g, mats["back"]); out.append(g)
    return out


def tail(kind, prof, mats, size=1.0):
    """The caudal fin at the tail's end: forked, lunate, rounded, or a shark's heterocercal one."""
    u0 = 0.03
    hw, hh, cy = prof(u0)
    z0 = -0.5 + u0
    s = size
    def lobe(up, reach, back, spread):
        # one lobe as a convex quad from the wrist (a concave fin outline triangulates badly)
        sg = 1 if up > 0 else -1
        # (broad at the wrist, sweeping to a point, its trailing edge cut back toward the fork)
        return fin([(0, cy + 0.012 * sg, z0 + 0.03), (0, cy + up * s, z0 - back * s), (0, cy + (up - sg * spread) * s, z0 - (back + 0.05) * s),
                    (0, cy + up * 0.35 * s, z0 - 0.075 * s), (0, cy - 0.004 * sg, z0 - 0.07 * s)], mats)
    if kind == "lunate":
        return [lobe(0.2, 0, 0.12, 0.03), lobe(-0.2, 0, 0.12, 0.03)]
    if kind == "forked":
        return [lobe(0.15, 0, 0.13, 0.05), lobe(-0.15, 0, 0.13, 0.05)]
    if kind == "shark":
        return [lobe(0.2, 0, 0.15, 0.05), lobe(-0.1, 0, 0.07, 0.04)]
    else:   # rounded / truncate
        pts = [(0, cy, z0 + 0.03)] + [(0, cy + math.sin(a) * 0.11 * s, z0 - 0.04 * s - math.cos(a) * 0.08 * s) for a in np.linspace(math.pi * 0.45, -math.pi * 0.45, 9)]
    return [fin(pts, mats)]


def dorsal(z0, z1, prof, hgt, mats, sweep=0.3, spiny=False):
    u0, u1 = z0 + 0.5, z1 + 0.5
    pts = []
    n = 8
    for k in range(n + 1):
        u = u0 + (u1 - u0) * k / n
        hw, hh, cy = prof(u)
        pts.append((0, cy + hh * 0.92, u - 0.5))
    top = []
    for k in range(n + 1):
        f = k / n
        u = u0 + (u1 - u0) * f
        hw, hh, cy = prof(u)
        h = hgt * (math.sin(math.pi * min(1, f * 1.1)) ** 0.6 if not spiny else (0.75 + 0.25 * math.cos(f * 18)) * (1 - 0.4 * f))
        top.append((0, cy + hh * 0.92 + max(0.004, h), u - 0.5 - sweep * h))
    return fin(pts + list(reversed(top)), mats)


def anal(z0, z1, prof, hgt, mats, sweep=0.3):
    u0, u1 = z0 + 0.5, z1 + 0.5
    pts, bot = [], []
    for k in range(7):
        f = k / 6
        u = u0 + (u1 - u0) * f
        hw, hh, cy = prof(u)
        pts.append((0, cy - hh * 0.92, u - 0.5))
        h = hgt * math.sin(math.pi * min(1, f * 1.1)) ** 0.6
        bot.append((0, cy - hh * 0.92 - max(0.004, h), u - 0.5 - sweep * h))
    return fin(pts + list(reversed(bot)), mats)


def pectoral(z, prof, length, mats, droop=0.35, wide=0.4, low=-0.25):
    out = []
    u = z + 0.5
    hw, hh, cy = prof(u)
    for s in (-1, 1):
        root = (s * hw * 0.9, cy + hh * low, z)
        tip = (s * (hw + length * wide), cy + hh * low - length * droop, z - length * 0.9)
        mid = (s * (hw + length * wide * 0.6), cy + hh * low - length * droop * 0.4, z - length * 0.3)
        out.append(fin([root, (root[0], root[1] - 0.025, root[2] - 0.01), tip, mid], mats))
    return out


def mouth(prof, mats, gape=0.4, length=0.07):
    """The jaw line on each side of the head: from the snout's tip back along the surface, curving down."""
    out = []
    for s in (-1, 1):
        pts = []
        for k in range(6):
            f = k / 5
            u = 0.995 - f * length
            hw, hh, cy = prof(u)
            yy = cy - hh * gape - f * f * hh * 0.25
            # (sit on the section's superellipse at that height, a hair proud of it)
            q = max(0.0, 1 - abs((yy - cy) / max(hh, 1e-4)) ** (2 / 0.75))
            pts.append(G(s * (hw * q ** (0.75 / 2) + 0.003), yy, u - 0.5))
        cu = bpy.data.curves.new("mouth", 'CURVE'); cu.dimensions = '3D'; cu.bevel_depth = 0.0045; cu.bevel_resolution = 1
        sp = cu.splines.new('POLY'); sp.points.add(len(pts) - 1)
        for i, p in enumerate(pts):
            sp.points[i].co = (p[0], p[1], p[2], 1)
        o = C.link(bpy.data.objects.new("mouth", cu)); C.select_only([o]); bpy.ops.object.convert(target='MESH')
        m = bpy.context.active_object; C.assign(m, mats["mouth"]); out.append(m)
    return out


def head_eyes(prof, mats, z=0.38, r=0.022, up=0.3, fwd=1.0):
    hw, hh, cy = prof(z + 0.5)
    out = []
    for s in (-1, 1):
        out += eye((s * hw * 0.82, cy + hh * up, z), r, mats, s)
    return out


# ---------------------------------------------------------------- the archetypes
# ---------------------------------------------------------------- the archetypes
def body(u, umax=0.62, nose=0.6, rear=0.8, peduncle=0.14):
    """A fish's girth along it (0 at the tail's wrist .. 1 at the snout): rising from a thin peduncle to its widest at
    umax, then an elliptical head, blunt for nose 0.5, conical toward 1."""
    if u >= umax:
        f = min(1.0, (u - umax) / (1 - umax))
        return max(0.04, (1 - f * f) ** nose)
    return peduncle + (1 - peduncle) * (u / umax) ** rear


def fusiform(u, w=0.085, h=0.11):
    t = body(u, 0.6, 0.85, 0.9, 0.1)
    return (w * t, h * t, 0.0)


@arch("tuna")
def tuna(m):
    P = lambda u: fusiform(u, 0.085, 0.11)
    parts = [split_body(loft(P), P, m)]
    parts += tail("lunate", P, m, 1.1) + [dorsal(-0.02, 0.12, P, 0.09, m, 0.5), dorsal(-0.18, -0.1, P, 0.05, m, 0.4),
              anal(-0.18, -0.1, P, 0.05, m)] + pectoral(0.24, P, 0.14, m, 0.15, 0.25, 0.0)
    for k in range(6):   # finlets toward the tail
        z = -0.24 - k * 0.035
        hw, hh, cy = P(z + 0.5)
        parts.append(fin([(0, cy + hh, z), (0, cy + hh + 0.02, z - 0.015), (0, cy + hh, z - 0.02)], m))
        parts.append(fin([(0, cy - hh, z), (0, cy - hh - 0.02, z - 0.015), (0, cy - hh, z - 0.02)], m))
    parts += head_eyes(P, m, 0.4, 0.02, 0.25) + gill(0.3, P, m) + mouth(P, m, 0.2)
    return parts, P


@arch("herring")
def herring(m):
    P = lambda u: fusiform(u, 0.045, 0.085)
    parts = [split_body(loft(P), P, m, 0.25)]
    parts += tail("forked", P, m, 0.9) + [dorsal(-0.02, 0.1, P, 0.06, m), anal(-0.25, -0.15, P, 0.03, m)] + pectoral(0.28, P, 0.08, m)
    parts += head_eyes(P, m, 0.4, 0.026, 0.25) + gill(0.31, P, m) + mouth(P, m, 0.1)
    return parts, P


@arch("perch")
def perch(m):
    def P(u):
        t = body(u, 0.55, 0.5, 0.75, 0.22)
        return (0.07 * t, 0.15 * t, 0.012 * t)
    parts = [split_body(loft(P), P, m, 0.0)]
    parts += tail("rounded", P, m, 1.0) + [dorsal(-0.15, 0.22, P, 0.07, m, 0.2, spiny=True), anal(-0.24, -0.08, P, 0.06, m)]
    parts += pectoral(0.22, P, 0.11, m, 0.25, 0.3, -0.2) + pectoral(0.17, P, 0.07, m, 0.6, 0.15, -0.8)
    parts += head_eyes(P, m, 0.36, 0.026, 0.38) + gill(0.25, P, m, 0.95) + mouth(P, m, 0.1, 0.09)
    return parts, P


@arch("deep")
def deep(m):
    def P(u):
        t = body(u, 0.55, 0.5, 0.6, 0.15)
        return (0.045 * t, 0.26 * t, 0.0)
    parts = [split_body(loft(P), P, m, 0.0)]
    parts += tail("forked", P, m, 0.8) + [dorsal(-0.3, 0.12, P, 0.06, m, 0.4), anal(-0.3, 0.05, P, 0.06, m, 0.4)] + pectoral(0.2, P, 0.1, m, 0.2, 0.2, -0.1)
    parts += head_eyes(P, m, 0.32, 0.03, 0.3) + gill(0.22, P, m, 0.7) + mouth(P, m, 0.1, 0.05)
    return parts, P


@arch("eel")
def eel(m):
    def P(u):
        t = min(1, u / 0.15) * (1 if u < 0.9 else (1 - ((u - 0.9) / 0.1) ** 2) ** 0.5 * 0.8 + 0.2)
        return (0.03 * t + 0.004, 0.038 * t + 0.004, 0.0)
    parts = [split_body(loft(P, n=40), P, m, -0.1)]
    parts += [dorsal(-0.48, 0.22, P, 0.03, m, 0.0), anal(-0.48, -0.05, P, 0.025, m, 0.0)]
    parts += head_eyes(P, m, 0.44, 0.011, 0.35) + mouth(P, m, 0.0, 0.05)
    return parts, P


@arch("shark")
def shark(m):
    def P(u):
        t = body(u, 0.58, 0.75, 0.85, 0.12)
        return (0.075 * t, 0.085 * t, 0.005 * t)
    parts = [split_body(loft(P), P, m, -0.15)]
    parts += tail("shark", P, m, 1.05) + [dorsal(0.0, 0.13, P, 0.12, m, 0.55), dorsal(-0.3, -0.25, P, 0.03, m, 0.3), anal(-0.28, -0.23, P, 0.025, m)]
    parts += pectoral(0.2, P, 0.17, m, 0.3, 0.55, -0.6) + pectoral(-0.08, P, 0.06, m, 0.4, 0.25, -0.8)
    for s in (-1, 1):   # five gill slits
        for k in range(5):
            z = 0.3 - k * 0.022
            hw, hh, cy = P(z + 0.5)
            pts = [G(s * hw * 0.98, cy - hh * 0.3, z), G(s * hw * 0.98, cy + hh * 0.35, z + 0.005)]
            cu = bpy.data.curves.new("slit", 'CURVE'); cu.dimensions = '3D'; cu.bevel_depth = 0.004; cu.bevel_resolution = 0
            sp = cu.splines.new('POLY'); sp.points.add(1)
            for i, p in enumerate(pts):
                sp.points[i].co = (p[0], p[1], p[2], 1)
            o = C.link(bpy.data.objects.new("slit", cu)); C.select_only([o]); bpy.ops.object.convert(target='MESH')
            g = bpy.context.active_object; C.assign(g, m["mouth"]); parts.append(g)
    parts += head_eyes(P, m, 0.4, 0.014, 0.2) + mouth(P, m, 0.7, 0.08)
    return parts, P


@arch("ray")
def ray(m):
    # a flat body with wings to the sides: lofted narrow, then the wings as broad fins from snout to mid-body
    def P(u):
        t = min(1, max(0, (u - 0.45) / 0.15)) * (1 if u < 0.92 else 1 - (u - 0.92) / 0.08 * 0.7)
        return (0.02 + 0.09 * t, 0.012 + 0.03 * t, 0.0)
    parts = [split_body(loft(P), P, m, 0.3)]
    for s in (-1, 1):
        pts = [(s * 0.08, 0.0, 0.45), (s * 0.3, -0.005, 0.25), (s * 0.5, 0.02, 0.12), (s * 0.45, 0.0, 0.05), (s * 0.2, 0.0, 0.02), (s * 0.08, 0.0, 0.0)]
        parts.append(fin(pts, m, 0.012))
    parts += head_eyes(P, m, 0.38, 0.014, 0.8)
    return parts, P


@arch("flat")
def flat(m):
    # a flatfish lying on its blind side: wide in x, thin in y, both eyes on top, a fringe of fin all round
    def P(u):
        t = body(u, 0.5, 0.55, 0.6, 0.18)
        return (0.2 * t, 0.03 * t, 0.0)
    parts = [split_body(loft(P), P, m, -0.2)]
    for s in (-1, 1):
        for k in range(9):   # the fringe as a run of small quads (the whole outline is concave)
            u0, u1 = 0.12 + 0.75 * k / 9, 0.12 + 0.75 * (k + 1) / 9
            w0, w1 = P(u0)[0], P(u1)[0]
            e0, e1 = 0.035 * math.sin(math.pi * k / 9), 0.035 * math.sin(math.pi * (k + 1) / 9)
            parts.append(fin([(s * w0 * 0.95, 0.0, u0 - 0.5), (s * w1 * 0.95, 0.0, u1 - 0.5), (s * (w1 + e1), 0.0, u1 - 0.51), (s * (w0 + e0), 0.0, u0 - 0.51)], m, 0.003))
    tl = tail("rounded", lambda u: (0.03, 0.03, 0.0), m, 0.9)
    for o in tl:
        o.rotation_euler = (0, math.pi / 2, 0); C.apply_all(o)
    parts += tl
    for dx in (-0.03, 0.035):
        parts += eye((dx, 0.03, 0.35), 0.016, m, 1)[:1]
    return parts, P


@arch("billfish")
def billfish(m):
    def P(u):
        if u > 0.8:   # the bill, out of a sloping head
            f = (u - 0.8) / 0.2
            hw, hh, cy = fusiform(0.8 / 0.82, 0.065, 0.085)
            k = max(0.09, (1 - f) ** 2.2)
            return (max(0.006, hw * k), max(0.007, hh * k), 0.0)
        return fusiform(u / 0.82, 0.065, 0.085)
    parts = [split_body(loft(P, n=40), P, m)]
    parts += tail("lunate", P, m, 1.0) + [dorsal(-0.05, 0.25, P, 0.14, m, 0.15, spiny=True), anal(-0.2, -0.1, P, 0.05, m)] + pectoral(0.2, P, 0.12, m, 0.4, 0.3, -0.2)
    parts += head_eyes(P, m, 0.27, 0.016, 0.2) + gill(0.2, P, m)
    return parts, P


@arch("pike")
def pike(m):
    def P(u):
        t = body(u, 0.55, 0.9, 0.8, 0.22)
        return (0.042 * t, 0.055 * t, -0.004 * t)
    parts = [split_body(loft(P, n=34), P, m)]
    parts += tail("forked", P, m, 0.7) + [dorsal(0.05, 0.12, P, 0.05, m, 0.3, spiny=True), dorsal(-0.2, -0.12, P, 0.04, m), anal(-0.2, -0.12, P, 0.04, m)] + pectoral(0.28, P, 0.07, m)
    parts += head_eyes(P, m, 0.4, 0.016, 0.35) + gill(0.33, P, m) + mouth(P, m, 0.0, 0.12)
    return parts, P

# ---------------------------------------------------------------- rig, skin, export
SPINE = [0.32, 0.12, -0.1, -0.3, -0.5]   # joint z positions: s0 from the head, s3 into the tail


def build(name, out):
    C.reset()
    mats = setup()
    parts, P = ARCH[name](mats)
    for o in parts:
        C.select_only([o]); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    arm = bpy.data.armatures.new("fish_rig")
    rig = C.link(bpy.data.objects.new("fish_rig", arm))
    C.select_only([rig]); bpy.ops.object.mode_set(mode='EDIT')
    prev = None
    for k in range(4):
        b = arm.edit_bones.new(f"s{k}")
        b.head = G(0, P(SPINE[k] + 0.5)[2], SPINE[k]); b.tail = G(0, P(SPINE[k + 1] + 0.5)[2], SPINE[k + 1])
        if prev:
            b.parent = prev; b.use_connect = True
        b.use_deform = True
        prev = b
    if name == "ray":   # the wings beat on their own bones from the body's edge (the game flaps them)
        for sd, s in (("L", 1), ("R", -1)):
            b = arm.edit_bones.new(f"wing.{sd}")
            b.head = G(s * 0.09, 0, 0.22); b.tail = G(s * 0.45, 0, 0.12)
            b.parent = arm.edit_bones["s1"]; b.use_deform = True
    bpy.ops.object.mode_set(mode='OBJECT')
    # weights by hand: each vertex between its two nearest joints along the spine, smoothly (so a bend is a curve)
    for o in parts:
        groups = [o.vertex_groups.new(name=f"s{k}") for k in range(4)]
        for v in o.data.vertices:
            z = -v.co.y
            ws = []
            for k in range(4):
                c = (SPINE[k] + SPINE[k + 1]) / 2
                half = (SPINE[k] - SPINE[k + 1]) / 2
                d = abs(z - c) / (half * 1.6)
                ws.append(max(0.0, 1 - d) ** 1.5 if not (k == 0 and z > c) and not (k == 3 and z < c) else 1.0)
            tot = sum(ws) or 1.0
            wing = 0.0
            if name == "ray":   # (out along the wing, the wing's bone takes over from the spine)
                wing = max(0.0, min(1.0, (abs(v.co.x) - 0.08) / 0.22))
            for k in range(4):
                if ws[k] > 0:
                    groups[k].add([v.index], ws[k] / tot * (1 - wing), 'REPLACE')
            if wing > 0:
                g = o.vertex_groups.get("wing.L" if v.co.x > 0 else "wing.R") or o.vertex_groups.new(name="wing.L" if v.co.x > 0 else "wing.R")
                g.add([v.index], wing, 'REPLACE')
        mod = o.modifiers.new("rig", 'ARMATURE'); mod.object = rig
        o.parent = rig
    # UVs in metres for the scale and ray tiles; a soft occlusion and a countershade in the vertex colour
    for o in parts:
        me = o.data
        layer = me.uv_layers[0] if me.uv_layers else me.uv_layers.new(name="UVMap")
        while len(me.uv_layers) > 1:
            me.uv_layers.remove(me.uv_layers[-1])
        me.uv_layers.active = layer
        for p in me.polygons:
            n = p.normal
            ax = max(range(3), key=lambda i: abs(n[i]))
            for li in p.loop_indices:
                v = me.vertices[me.loops[li].vertex_index].co
                layer.data[li].uv = (v.y / 0.25, v.z / 0.25) if ax == 0 else ((v.x / 0.25, v.y / 0.25) if ax == 2 else (v.x / 0.25, v.z / 0.25))
        C.smooth(o, 50)
        ca = me.color_attributes.new("Col", 'BYTE_COLOR', 'POINT')
        me.color_attributes.active_color = ca
        for i, v in enumerate(me.vertices):
            z = -v.co.y
            hw, hh, cy = P(min(1, max(0, z + 0.5)))
            f = max(-1.0, min(1.0, (v.co.z - cy) / max(hh, 1e-3)))
            k = 0.55 + 0.45 * (1 - (f * 0.5 + 0.5)) ** 0.7 if f > 0 else 1.0   # darker toward the back's top
            ca.data[i].color = (k, k, k, 1.0)
    # one mesh per fish (a primitive per material): a school is many fish, and every draw call counts on this PC
    C.select_only(parts)
    bpy.ops.object.join()
    body = bpy.context.active_object
    path = os.path.join(out, f"fish_{name}.glb")
    C.select_only([body, rig])
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=False,
                              export_yup=True, export_texcoords=True, export_normals=True, export_skins=True,
                              export_animations=False, export_materials='EXPORT', export_vertex_color='ACTIVE',
                              export_all_influences=False, export_image_format='JPEG')
    print("artgen: wrote", path)


def main():
    out = C.out_dir()
    a = C.args()
    only = a[a.index("--only") + 1].split(",") if "--only" in a else list(ARCH)
    for n in only:
        build(n, out)


main()
