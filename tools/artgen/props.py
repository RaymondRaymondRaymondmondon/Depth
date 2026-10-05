# Small props that the game moves or switches (the Trawl Visual Overhaul Spec, phase 7: nothing left as a raw
# primitive): a hatch cover and its battens, the watertight door, harbour buoys (red and green), the life ring.
# Each in the game's frame (x forward, y up, z to starboard) about its own origin, baked like the boat (tiling sets,
# occlusion in the vertex colours).
#     blender -b --factory-startup -P tools/artgen/props.py -- --out assets/trawl/props

import bpy
import math
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import common as C
import boat as B
import tiles
from boat import box, cyl, rod, disc, lathe, quads, G

_TILES = None
def all_tiles():
    global _TILES
    if _TILES is None:
        _TILES = tiles.build_all()
    return _TILES
tiles.build_all_cached = all_tiles


def reset():
    C.reset()
    B.PARTS.clear()
    B.MATS.clear()
    B.TILE_OF.clear()
    B.setup_mats(("deck", "house", "iron", "bulk"))
    for k, rgb in (("red", (0.45, 0.05, 0.04)), ("green", (0.04, 0.3, 0.1)), ("white", (0.75, 0.74, 0.7))):
        B.MATS["paint_" + k] = B.flat("paint " + k, rgb, 0.45)


def hatch_cover():
    # planked boards on two cross battens, a ring handle each side; sits on the coaming (its underside at y 0)
    for k in range(5):
        box((0, 0.035, -0.4 + k * 0.2), (0.54, 0.03, 0.095), "deck", 0.008)
    for x in (-0.35, 0.35):
        box((x, -0.02, 0), (0.05, 0.025, 0.5), "house", 0.006)
    for s in (-1, 1):
        box((s * 0.42, 0.075, 0), (0.05, 0.012, 0.04), "iron", 0.004)
        pts = [(s * 0.42 + s * 0.06 * math.sin(a), 0.08 + 0.05 * math.cos(a) * 0.3, 0.06 * math.cos(a)) for a in np.linspace(0, 2 * math.pi, 14)]
        rod(pts, 0.008, "iron", 4, caps=False)


def batten():
    # an iron batten bar across the cover, wedged at its ends
    box((0, 0, 0), (0.6, 0.02, 0.035), "iron", 0.006)
    for s in (-1, 1):
        box((s * 0.6, -0.04, 0), (0.03, 0.06, 0.05), "iron", 0.006)


def door():
    # the watertight door: a steel plate with a raised rim, six dogs and a wheel, hinged on its -z edge (origin at
    # its centre; the game draws it shut across the bulkhead's opening)
    box((0, 0, 0), (0.035, 0.93, 0.58), "bulk", 0.03)
    box((0.03, 0, 0), (0.012, 0.86, 0.52), "bulk", 0.01)
    for y in (-0.6, 0.0, 0.6):
        for s in (-1, 1):
            cyl((0.04, y, s * 0.54), (0.1, y, s * 0.54), 0.025, "iron", 8)
            rod([(0.1, y, s * 0.54), (0.1, y + 0.08, s * 0.5)], 0.012, "iron", 4)
    disc((0.08, 0.05, 0), 'x', 0.16, 0.025, "iron", 20, hole=0.13)
    for k in range(3):
        a = k * math.pi / 3
        rod([(0.08, 0.05 + math.sin(a) * 0.14, math.cos(a) * 0.14), (0.08, 0.05 - math.sin(a) * 0.14, -math.cos(a) * 0.14)], 0.012, "iron", 4)
    for y in (-0.7, 0.7):
        cyl((-0.02, y, -0.62), (0.05, y, -0.62), 0.04, "iron", 10)


def buoy(colour):
    # a can buoy riding at its waterline (y 0): the can, a band of white, a lattice tower, a lamp cage at 1.05
    lathe((0, 0, 0), [(0.0001, -0.35), (0.22, -0.35), (0.26, -0.2), (0.26, 0.55), (0.24, 0.62), (0.0001, 0.62)], "paint_" + colour, 20)
    lathe((0, 0, 0), [(0.262, 0.28), (0.262, 0.4)], "paint_white", 20)
    for k in range(3):
        a = k * 2 * math.pi / 3
        rod([(math.cos(a) * 0.2, 0.62, math.sin(a) * 0.2), (math.cos(a) * 0.07, 0.95, math.sin(a) * 0.07)], 0.015, "iron", 4)
    lathe((0, 0, 0), [(0.0001, 0.95), (0.1, 0.95), (0.1, 0.97), (0.0001, 0.97)], "iron", 12)
    for k in range(4):
        a = k * math.pi / 2
        rod([(math.cos(a) * 0.08, 0.97, math.sin(a) * 0.08), (math.cos(a) * 0.08, 1.15, math.sin(a) * 0.08)], 0.008, "iron", 4)
    lathe((0, 0, 0), [(0.11, 1.15), (0.06, 1.22), (0.0001, 1.24)], "iron", 12)
    pts = [(0.27 * math.cos(a), 0.1, 0.27 * math.sin(a)) for a in np.linspace(0, 2 * math.pi, 20)]
    rod(pts, 0.02, "iron", 6, caps=False)


def life_ring():
    # a cork ring in canvas, painted in white and red quarters, a grab line round it (lying flat: y up)
    bpy.ops.mesh.primitive_torus_add(major_radius=0.3, minor_radius=0.065, major_segments=32, minor_segments=10)
    t = bpy.context.active_object
    t.data.materials.append(B.MATS["paint_white"]); t.data.materials.append(B.MATS["paint_red"])
    for p in t.data.polygons:
        a = math.atan2(p.center.y, p.center.x)
        p.material_index = int(((a + math.pi) / (math.pi / 2))) % 2
    B.PARTS.append(t)
    for k in range(4):
        a0 = k * math.pi / 2 + 0.25
        pts = []
        for j in range(7):
            a = a0 + j * (math.pi / 2 - 0.5) / 6
            r = 0.37 - 0.03 * math.sin(j / 6 * math.pi)
            pts.append((math.cos(a) * r, 0.0, math.sin(a) * r))
        rod(pts, 0.008, "rope", 4, caps=False)


def SHB(x):
    """The skiff's half-breadth at x (trawl_view3d.cpp's SkiffHB): 4.5 m long, full aft, fine forward."""
    u = (x + 2.25) / 4.5
    return 0.8 * (0.88 + 0.12 * math.sin(u / 0.65 * math.pi * 0.5) if u < 0.65 else math.cos((u - 0.65) / 0.35 * math.pi * 0.5) * 0.97 + 0.03)


def skiff():
    # clinker built: five lapped strakes a side (white, the sheer strake red), varnished inside, a keel, the stem and
    # transom, three thwarts with knees, bottom boards, a gunwale, rowlocks at x 0.2, the lantern post forward
    BOT, TOP = -0.28, 0.34
    xs = list(np.linspace(-2.25, 2.25, 31))
    def zf(x, f):   # the hull's half-width at height fraction f (0 bottom .. 1 sheer)
        return SHB(x) * (0.55 + 0.45 * f ** 0.6)
    n = 5
    for s in (-1, 1):
        for k in range(n):
            f0, f1 = k / n, (k + 1) / n
            y0, y1 = BOT + (TOP - BOT) * f0, BOT + (TOP - BOT) * f1
            lap = 0.012
            vs, fs = [], []
            for x in xs:
                vs.append((x, y0 - 0.02, s * (zf(x, f0) + lap)))   # (each strake's lower edge laps outside the one below)
                vs.append((x, y1, s * zf(x, f1)))
            for i in range(len(xs) - 1):
                fs.append((2 * i, 2 * i + 2, 2 * i + 3, 2 * i + 1))
            quads(vs, fs, "paint_red" if k == n - 1 else "paint_white", "strake")
        # inside: varnished planking from the bottom to the sheer
        vs = [(x, y, s * zf(x, (y - BOT) / (TOP - BOT)) * 0.93) for x in xs for y in (BOT + 0.04, TOP)]   # (a fraction in: a fixed inset would cross over in the fine bow)
        quads(vs, [(2 * i, 2 * i + 2, 2 * i + 3, 2 * i + 1) for i in range(len(xs) - 1)], "house", "lining")
        rod([(x, TOP + 0.015, s * (SHB(x) - 0.01)) for x in xs], 0.025, "house", 6)   # the gunwale
        cyl((0.2, TOP, s * (SHB(0.2) - 0.01)), (0.2, TOP + 0.09, s * (SHB(0.2) - 0.01)), 0.012, "brass", 6)
        rod([(0.2, TOP + 0.09, s * (SHB(0.2) - 0.05)), (0.2, TOP + 0.13, s * (SHB(0.2) - 0.02)), (0.2, TOP + 0.09, s * (SHB(0.2) + 0.03))], 0.01, "brass", 4, caps=False)
    # the bottom, the keel, the stem, the transom
    quads([(x, BOT, -SHB(x) * 0.55) for x in xs] + [(x, BOT, SHB(x) * 0.55) for x in xs], [(i, i + 1, 31 + i + 1, 31 + i) for i in range(30)], "paint_white", "bottom")
    quads([(x, BOT + 0.03, -SHB(x) * 0.6) for x in xs] + [(x, BOT + 0.03, SHB(x) * 0.6) for x in xs], [(i, i + 1, 31 + i + 1, 31 + i) for i in range(30)], "house", "floor")
    box((0, BOT - 0.04, 0), (2.2, 0.04, 0.03), "house", 0.01)
    rod([(2.25, BOT - 0.04, 0), (2.3, 0.0, 0), (2.32, TOP + 0.08, 0)], 0.035, "house", 6)
    ht = SHB(-2.25)
    quads([(-2.25, BOT, -ht * 0.55), (-2.25, BOT, ht * 0.55), (-2.25, TOP, ht), (-2.25, TOP, -ht)], [(0, 1, 2, 3)], "paint_white", "transom")
    quads([(-2.22, BOT + 0.03, -ht * 0.6), (-2.22, BOT + 0.03, ht * 0.6), (-2.22, TOP, ht * 0.93), (-2.22, TOP, -ht * 0.93)], [(0, 1, 2, 3)], "house", "transom_in")
    box((-2.23, TOP - 0.02, 0), (0.03, 0.03, ht), "house", 0.008)
    # thwarts with their knees, bottom boards, the lantern post
    for x in (0.2, -1.3, 1.3):
        hw = SHB(x) - 0.06
        box((x, 0.06, 0), (0.13, 0.025, hw), "house", 0.008)
        for s in (-1, 1):
            rod([(x, 0.06, s * (hw - 0.05)), (x, 0.24, s * (hw + 0.02))], 0.02, "house", 4)
    for k in range(5):
        box((0, BOT + 0.07, -0.32 + k * 0.16), (1.7, 0.012, 0.06), "deck", 0.004)
    cyl((1.95, TOP, 0), (1.95, TOP + 0.5, 0), 0.02, "house", 6)
    lathe((1.95, 0, 0), [(0.06, TOP + 0.5), (0.07, TOP + 0.52), (0.05, TOP + 0.66), (0.02, TOP + 0.7)], "iron", 10)
    # a painter coiled in the bow
    pts = [(1.6 + math.cos(a) * 0.15, BOT + 0.1, math.sin(a) * 0.15) for a in np.linspace(0, 4 * math.pi, 26)]
    rod(pts, 0.015, "rope", 4, caps=False)


def oar():
    # one oar along +z: the handle at -1.3, a leather collar where it sits in the rowlock (-0.95 from the middle:
    # the game turns it about the lock), the loom, and the blade out to +1.3, edge-up (wide in y)
    cyl((0, 0, -1.3), (0, 0, -1.15), 0.02, "house", 8)
    cyl((0, 0, -1.15), (0, 0, 0.55), 0.028, "house", 10, r2=0.022)
    cyl((0, 0, -0.08), (0, 0, 0.12), 0.034, "rope", 10)
    quads([(0, -0.03, 0.5), (0, 0.03, 0.5), (0, 0.075, 0.75), (0, 0.075, 1.3), (0, -0.075, 1.3), (0, -0.075, 0.75)], [(0, 1, 2, 3, 4, 5)], "house", "blade")


# ---------------------------------------------------------------- the landings and the Grotto (each about its base)
def land_mats():
    T = all_tiles()
    for k in ("sand", "wetsand", "jungle", "rock", "caverock", "stone", "shed"):
        if k not in B.MATS:
            B.MATS[k] = B.tile_mat(T[k])
    for k, rgb, r in (("bark", (0.22, 0.16, 0.1), 0.9), ("frond", (0.1, 0.22, 0.07), 0.6), ("thatch", (0.42, 0.33, 0.17), 0.95),
                      ("marble", (0.72, 0.7, 0.64), 0.55), ("tent", (0.35, 0.05, 0.04), 0.85), ("bone", (0.75, 0.71, 0.6), 0.6),
                      ("mould", (0.1, 0.55, 0.45), 0.4), ("char", (0.04, 0.035, 0.03), 0.95), ("turf", (0.2, 0.28, 0.1), 0.95)):
        B.MATS[k] = B.flat(k, rgb, r)


def boulder_mesh(name, r, mat, seed, squash=0.7):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=r)
    o = bpy.context.active_object
    rng = np.random.default_rng(seed)
    for v in o.data.vertices:
        d = v.co.normalized()
        v.co *= 1 + 0.22 * math.sin(d.x * 5 + seed) * math.cos(d.y * 4) + 0.1 * rng.random()
        v.co.z = v.co.z * squash
        if v.co.z < -0.25 * r:
            v.co.z = -0.25 * r   # (sits flat in the sand)
    o.location.z = 0.25 * r
    return B.put(o, mat)


def palm():
    # a leaning, ringed trunk and a crown of drooping, split fronds; the base at the origin, leaning toward +x
    pts = [(0.9 * (t ** 1.6), 5.0 * t, 0.15 * math.sin(t * 3)) for t in np.linspace(0, 1, 9)]
    rod(pts, 0.17, "bark", 10)
    for t in np.linspace(0.08, 0.95, 14):
        p = (0.9 * (t ** 1.6), 5.0 * t, 0.15 * math.sin(t * 3))
        lathe(p, [(0.19 - 0.06 * t, -0.03), (0.2 - 0.06 * t, 0.0), (0.17 - 0.06 * t, 0.03)], "bark", 10)
    top = (0.9, 5.0, 0.15 * math.sin(3))
    lathe(top, [(0.12, -0.2), (0.25, 0.0), (0.12, 0.25)], "frond", 8)
    for k in range(9):
        a = k * 2 * math.pi / 9 + 0.3
        ca, sa = math.cos(a), math.sin(a)
        L = 2.6 + 0.4 * (k % 3)
        spine = [(top[0] + ca * L * s, top[1] + 0.5 * s - 1.6 * s * s, top[2] + sa * L * s) for s in np.linspace(0, 1, 8)]
        rod(spine, 0.02, "frond", 4)
        vs, fs = [], []
        for i, (x, y, z) in enumerate(spine):
            w = 0.42 * math.sin(math.pi * min(1, i / 7 * 1.1))
            vs += [(x - sa * w, y - 0.12 * w, z + ca * w), (x, y + 0.04, z), (x + sa * w, y - 0.12 * w, z - ca * w)]
        for i in range(len(spine) - 1):
            fs += [(3 * i, 3 * i + 1, 3 * i + 4, 3 * i + 3), (3 * i + 1, 3 * i + 2, 3 * i + 5, 3 * i + 4)]
        quads(vs, fs, "frond", "frond")
    for k in range(4):   # coconuts
        a = k * 1.7
        lathe((top[0] + math.cos(a) * 0.2, top[1] - 0.25, top[2] + math.sin(a) * 0.2), [(0.0001, -0.11), (0.1, -0.05), (0.11, 0.0), (0.09, 0.06), (0.0001, 0.11)], "bark", 10)


def hut():
    # the elder's hut: a low round wall of boards, a door gap, a deep thatched cone with a ragged eave
    for k in range(18):
        a = k * 2 * math.pi / 18
        if abs(a - math.pi / 2) < 0.35:
            continue   # (the door, facing +z)
        box((math.cos(a) * 1.05, 0.75, math.sin(a) * 1.05), (0.19, 0.75, 0.04), "shed", 0.01, rot_y=-a + math.pi / 2)
    lathe((0, 0, 0), [(1.75, 1.45), (1.7, 1.55), (1.2, 2.2), (0.5, 3.0), (0.08, 3.5), (0.0001, 3.55)], "thatch", 18)
    for k in range(24):   # the eave's straggle
        a = k * 2 * math.pi / 24
        rod([(math.cos(a) * 1.72, 1.5, math.sin(a) * 1.72), (math.cos(a) * 1.82, 1.32 - 0.05 * (k % 3), math.sin(a) * 1.82)], 0.025, "thatch", 4)
    cyl((0, 1.5, 0), (0, 3.6, 0), 0.05, "bark", 6)


def beached_sloop():
    # the skiff's lines enlarged into a small sloop lying on her side in the sand, her mast broken off
    skiff()
    for o in B.PARTS:
        o.scale = (1.15, 1.15, 1.15)
        o.rotation_euler = (math.radians(72), 0, 0)   # (rolled onto her side: her keel toward the sea)
        o.location = (0, 0, 0.55)
    rod([(0.6, 0.6, 0.0), (0.7, 0.9, -1.4)], 0.07, "house", 6)


def firering():
    for k in range(9):
        a = k * 2 * math.pi / 9
        o = boulder_mesh("stone", 0.13, "rock", 30 + k, 0.6)
        o.location = G(math.cos(a) * 0.5, 0.03, math.sin(a) * 0.5)
    for k in range(4):
        a = k * 0.8
        rod([(math.cos(a) * 0.35, 0.06, math.sin(a) * 0.35), (-math.cos(a) * 0.3, 0.12, -math.sin(a) * 0.3)], 0.05, "char", 6)
    lathe((0, 0, 0), [(0.0001, 0.0), (0.38, 0.0), (0.3, 0.03), (0.0001, 0.04)], "char", 14)


def boulder():
    boulder_mesh("boulder", 0.6, "rock", 7, 0.65)


def stonehut():
    # the sealers' hut: dry-stone walls, a turf roof, the door; the stove by it is its own prop
    for (c, h) in (((0, 1.0, -1.55), (2.6, 1.0, 0.12)), ((-2.5, 1.0, 0), (0.12, 1.0, 1.6)), ((2.5, 1.0, 0), (0.12, 1.0, 1.6))):
        box(c, h, "stone", 0.03)
    box((-1.6, 1.0, 1.55), (1.0, 1.0, 0.12), "stone", 0.03)
    box((1.6, 1.0, 1.55), (1.0, 1.0, 0.12), "stone", 0.03)
    box((0, 1.85, 1.55), (0.6, 0.15, 0.12), "stone", 0.02)
    box((0, 0.85, 1.62), (0.55, 0.85, 0.02), "shed", 0.01)
    quads([(-2.8, 2.0, -1.8), (2.8, 2.0, -1.8), (2.8, 2.45, 0), (-2.8, 2.45, 0)], [(0, 1, 2, 3)], "turf", "roof")
    quads([(-2.8, 2.0, 1.8), (2.8, 2.0, 1.8), (2.8, 2.45, 0), (-2.8, 2.45, 0)], [(0, 1, 2, 3)], "turf", "roof")


def stove():
    box((0, 0.35, 0), (0.4, 0.35, 0.3), "iron", 0.02)
    cyl((0.2, 0.7, 0), (0.2, 2.6, 0), 0.08, "iron", 10)
    lathe((0.2, 0, 0), [(0.0001, 2.6), (0.15, 2.62), (0.0001, 2.7)], "iron", 10)


def cannery():
    # the cannery's shed of corrugated iron and its door; the boiler drum and stack are their own prop
    box((0, 1.4, 0), (2.6, 1.4, 1.6), "iron", 0.02)
    for k in range(27):
        x = -2.6 + k * 0.2
        for s in (-1, 1):
            cyl((x, 0.05, s * 1.62), (x, 2.75, s * 1.62), 0.025, "iron", 6, bevel=0)
    quads([(-2.75, 2.8, -1.75), (2.75, 2.8, -1.75), (2.75, 3.15, 0), (-2.75, 3.15, 0)], [(0, 1, 2, 3)], "iron", "roof")
    quads([(-2.75, 2.8, 1.75), (2.75, 2.8, 1.75), (2.75, 3.15, 0), (-2.75, 3.15, 0)], [(0, 1, 2, 3)], "iron", "roof")
    box((0, 1.0, 1.65), (0.7, 1.0, 0.03), "shed", 0.01)


def boiler():
    cyl((-1.0, 0.8, 0), (1.0, 0.8, 0), 0.75, "iron", 20)
    for x in (-1.0, 1.0):
        disc((x, 0.8, 0), 'x', 0.78, 0.05, "iron", 20)
    cyl((0.4, 1.4, 0), (0.4, 4.6, 0), 0.2, "iron", 12, r2=0.18)
    for x in (-0.8, 0.8):
        box((x, 0.2, 0), (0.15, 0.2, 0.6), "stone", 0.02)


def tower():
    # the watchtower's stump: a ring of masonry, broken off unevenly, a fallen block
    for k in range(20):
        a = k * 2 * math.pi / 20
        top = 3.5 + 2.0 * abs(math.sin(k * 1.3)) * (1 if k % 5 else 0.3)
        box((math.cos(a) * 2.45, top / 2 - 1, math.sin(a) * 2.45), (0.42, top / 2, 0.3), "stone", 0.03, rot_y=-a + math.pi / 2)
    box((3.4, 0.25, 1.2), (0.5, 0.25, 0.32), "stone", 0.04, rot_y=0.5)


def shrine():
    for s in (-1, 1):
        lathe((s * 1.8, 0, 0), [(0.45, 0.0), (0.42, 0.2), (0.32, 0.3)] + [(0.3 - 0.02 * t, 0.3 + t * 3.1) for t in np.linspace(0, 1, 6)] + [(0.38, 3.45), (0.4, 3.6)], "marble", 16)
    box((0, 3.8, 0), (2.4, 0.25, 0.6), "marble", 0.03)


def stair():
    for s in range(6):
        box((-6.0 + s * 0.9, -0.8 + s * 0.15, 0), (0.45, 0.15 + s * 0.15, 6.5), "marble", 0.03)


def tent():
    lathe((0, 0, 0), [(2.6, 0.0), (2.0, 1.0), (1.0, 2.3), (0.08, 3.4), (0.0001, 3.45)], "tent", 14)
    cyl((0, 0, 0), (0, 3.7, 0), 0.05, "bark", 6)


def skullpost():
    cyl((0, 0, 0), (0, 2.4, 0), 0.12, "bark", 8, r2=0.1)
    for y in (0.6, 1.2, 1.8):
        lathe((0, 0, 0), [(0.125, y), (0.15, y + 0.04), (0.125, y + 0.08)], "bark", 8)
    lathe((0, 0, 0), [(0.0001, 2.4), (0.15, 2.45), (0.17, 2.6), (0.13, 2.75), (0.0001, 2.78)], "bone", 12)
    for s in (-1, 1):
        lathe((0.12, 0, s * 0.06), [(0.0001, 2.6), (0.035, 2.61), (0.0001, 2.66)], "char", 6)


def brazier():
    for k in range(3):
        a = k * 2 * math.pi / 3
        rod([(math.cos(a) * 0.5, 0, math.sin(a) * 0.5), (math.cos(a) * 0.3, 0.6, math.sin(a) * 0.3)], 0.03, "iron", 4)
    lathe((0, 0, 0), [(0.25, 0.55), (0.6, 0.9), (0.55, 0.92), (0.2, 0.6)], "iron", 16)


def stalactite():
    # a dripstone hanging from its root at the origin, 1 m long (the game scales it), wet and ridged
    prof = [(0.32 * (1 - t) ** 1.3 + 0.008, -t) for t in np.linspace(0, 1, 9)]
    lathe((0, 0, 0), prof, "caverock", 10)


def mould():
    # a mat of glowing mould on a ledge: lumpy blobs in a patch about 1 m across
    rng = np.random.default_rng(5)
    for k in range(9):
        o = boulder_mesh("blob", 0.12 + 0.1 * rng.random(), "mould", 50 + k, 0.45)
        o.location = G((rng.random() - 0.5) * 0.9, 0, (rng.random() - 0.5) * 0.9)


# ---------------------------------------------------------------- clutter (the user: "buckets on the ship and other purely
# environmental things"): small things a working boat and a landing leave lying about, each about its base
def clutter_mats():
    for k, rgb, r, m in (("galv", (0.5, 0.52, 0.52), 0.45, 0.8), ("wicker", (0.45, 0.32, 0.15), 0.95, 0.0),
                         ("sealhide", (0.24, 0.22, 0.2), 0.45, 0.0), ("sealbelly", (0.42, 0.38, 0.32), 0.55, 0.0),
                         ("crabshell", (0.62, 0.16, 0.06), 0.4, 0.0), ("crabdark", (0.25, 0.06, 0.03), 0.5, 0.0),
                         ("eye", (0.02, 0.02, 0.02), 0.1, 0.0), ("canvas", (0.48, 0.44, 0.34), 0.95, 0.0),
                         ("tarp", (0.16, 0.24, 0.2), 0.7, 0.0), ("net", (0.36, 0.3, 0.2), 0.95, 0.0),
                         ("cork", (0.6, 0.42, 0.22), 0.9, 0.0), ("drift", (0.55, 0.5, 0.42), 0.95, 0.0),
                         ("oilskin", (0.72, 0.56, 0.16), 0.35, 0.0), ("mophead", (0.66, 0.62, 0.52), 0.98, 0.0)):
        if k not in B.MATS:
            B.MATS[k] = B.flat(k, rgb, r, m)


def blob(c, r, mat, seed=0, wob=0.08, segs=20):
    """A smooth ellipsoid (game frame c, half-sizes r) with a little lumpiness."""
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segs, ring_count=max(8, segs * 2 // 3), radius=1.0)
    o = bpy.context.active_object
    rng = np.random.default_rng(seed)
    ph = rng.random(3) * 6
    for v in o.data.vertices:
        d = v.co.normalized()
        v.co *= 1 + wob * math.sin(d.x * 3 + ph[0]) * math.cos(d.y * 2.5 + ph[1]) * math.sin(d.z * 2 + ph[2])
    o.scale = (r[0], r[2], r[1])   # (Blender's y is the game's z)
    o.location = G(*c)
    B.put(o, mat)
    bpy.ops.object.shade_smooth()
    return o


def bucket():
    # a galvanised pail: tapered, rolled rim, two ears and a wire bail
    lathe((0, 0, 0), [(0.0001, 0.0), (0.12, 0.0), (0.125, 0.01), (0.155, 0.3), (0.165, 0.31), (0.16, 0.32), (0.145, 0.3), (0.112, 0.03), (0.0001, 0.03)], "galv", 22)
    for h in (0.1, 0.22):
        lathe((0, 0, 0), [(0.13 + h * 0.1, h), (0.135 + h * 0.1, h + 0.012), (0.13 + h * 0.1, h + 0.024)], "galv", 22)
    pts = [(0.16 * math.cos(a), 0.3 + 0.16 * math.sin(a), 0) for a in np.linspace(0, math.pi, 13)]
    rod(pts, 0.004, "iron", 4, caps=False)
    lathe((0, 0, 0), [(0.0001, 0.2), (0.115, 0.2), (0.0001, 0.205)], "bulk", 16)   # (water in it)


def fishbox():
    # a slatted fish box with hand-holes, a few herring in ice
    for s in (-1, 1):
        box((0, 0.12, s * 0.21), (0.32, 0.11, 0.015), "house", 0.006)
        box((s * 0.31, 0.12, 0), (0.015, 0.11, 0.2), "house", 0.006)
        box((s * 0.31, 0.17, 0), (0.02, 0.022, 0.06), "bulk", 0.004)   # (the hand-hole's shadow)
    box((0, 0.015, 0), (0.31, 0.012, 0.2), "house", 0.004)
    box((0, 0.13, 0), (0.29, 0.03, 0.18), "zinc" if "zinc" in B.MATS else "galv", 0.02)
    for k in range(5):
        blob((-0.2 + k * 0.1, 0.17, -0.05 + 0.07 * (k % 2)), (0.09, 0.025, 0.03), "galv", k, 0.05, 12)


def crate():
    # a nailed packing crate with corner battens
    box((0, 0.25, 0), (0.3, 0.25, 0.25), "house", 0.01)
    for sx in (-1, 1):
        for sz in (-1, 1):
            box((sx * 0.29, 0.25, sz * 0.24), (0.025, 0.26, 0.025), "deck", 0.005)
    for y in (0.08, 0.42):
        box((0, y, 0.255), (0.28, 0.03, 0.01), "deck", 0.004)
        box((0, y, -0.255), (0.28, 0.03, 0.01), "deck", 0.004)


def lobsterpot():
    # a creel: a flat base, three hooped arches of cane, netting over them, a funnel eye at the end
    box((0, 0.02, 0), (0.33, 0.02, 0.22), "house", 0.005)
    for x in (-0.28, 0.0, 0.28):
        pts = [(x, 0.03 + 0.24 * math.sin(a), 0.21 * math.cos(a)) for a in np.linspace(0, math.pi, 13)]
        rod(pts, 0.012, "wicker", 5, caps=False)
    for k in range(7):
        a = k / 6 * math.pi
        rod([(-0.3, 0.03 + 0.24 * math.sin(a), 0.21 * math.cos(a)), (0.3, 0.03 + 0.24 * math.sin(a), 0.21 * math.cos(a))], 0.004, "net", 3, caps=False)
    disc((0.3, 0.14, 0), 'x', 0.09, 0.01, "wicker", 14, hole=0.05)


def ropecoil():
    for k in range(5):
        rr = 0.3 - k * 0.035
        pts = [(math.cos(a) * rr, 0.025 + k * 0.042, math.sin(a) * rr) for a in np.linspace(0, 2 * math.pi, 22)]
        rod(pts, 0.024, "rope", 6, caps=False)
    rod([(0.27, 0.22, 0), (0.35, 0.1, 0.1), (0.42, 0.03, 0.25)], 0.024, "rope", 6)


def tacklebox():
    box((0, 0.1, 0), (0.22, 0.1, 0.12), "paint_green", 0.01)
    box((0, 0.205, 0), (0.225, 0.012, 0.125), "paint_green", 0.006)
    rod([(-0.08, 0.215, 0), (-0.06, 0.26, 0), (0.06, 0.26, 0), (0.08, 0.215, 0)], 0.008, "iron", 4, caps=False)
    box((0, 0.17, 0.122), (0.025, 0.02, 0.006), "brass", 0.002)


def oilcan():
    lathe((0, 0, 0), [(0.0001, 0.0), (0.09, 0.0), (0.09, 0.08), (0.05, 0.12), (0.012, 0.14), (0.0001, 0.14)], "iron", 16)
    rod([(0.01, 0.13, 0), (0.08, 0.2, 0), (0.15, 0.27, 0)], 0.006, "iron", 4)
    rod([(-0.07, 0.03, 0), (-0.12, 0.08, 0), (-0.07, 0.12, 0)], 0.008, "iron", 4, caps=False)


def mop():
    # a deck mop leaning on its handle (the head on the deck at the origin, the handle up toward +x)
    blob((0, 0.06, 0), (0.15, 0.07, 0.15), "mophead", 3, 0.25, 16)
    for k in range(10):
        a = k * 2 * math.pi / 10
        rod([(0.0, 0.08, 0), (math.cos(a) * 0.2, 0.01, math.sin(a) * 0.2)], 0.012, "mophead", 4)
    cyl((0, 0.08, 0), (0.6, 1.3, 0), 0.018, "drift", 8)


def netpile():
    # a heap of brown net with a run of cork floats through it
    rng = np.random.default_rng(11)
    for k in range(7):
        blob(((rng.random() - 0.5) * 0.9, 0.08 + 0.06 * rng.random(), (rng.random() - 0.5) * 0.6), (0.32, 0.12, 0.26), "net", 20 + k, 0.2, 14)
    for k in range(9):
        a = k * 0.7
        blob((math.cos(a) * 0.42, 0.18 + 0.05 * math.sin(k), math.sin(a) * 0.3), (0.05, 0.035, 0.035), "cork", k, 0.05, 10)


def tarp():
    # a lashed tarpaulin over something boxy, its edges sagging
    blob((0, 0.24, 0), (0.6, 0.26, 0.38), "tarp", 4, 0.06, 22)
    box((0, 0.02, 0), (0.62, 0.02, 0.4), "tarp", 0.02)
    for x in (-0.3, 0.3):
        pts = [(x, 0.02 + 0.5 * math.sin(a) * 0.98, 0.4 * math.cos(a)) for a in np.linspace(0, math.pi, 11)]
        rod(pts, 0.01, "rope", 4, caps=False)


def oilskin():
    # an oilskin coat and sou'wester hung on a peg (its peg at the origin, hanging down -y, lying against +x of a wall)
    cyl((0, 0, 0), (0.08, 0, 0), 0.012, "house", 6)
    blob((0.06, -0.38, 0), (0.07, 0.36, 0.2), "oilskin", 5, 0.1, 18)
    for s in (-1, 1):
        blob((0.07, -0.3, s * 0.2), (0.05, 0.25, 0.05), "oilskin", 6 + s, 0.08, 12)
    lathe((0.06, 0, 0), [(0.0001, 0.02), (0.1, 0.02), (0.13, -0.01), (0.15, -0.04)], "oilskin", 16)


def seal():
    # a harbour seal lying on the rocks, head up (facing +x): a plump spindle, a head, flippers, a tail fan
    blob((0, 0.24, 0), (0.62, 0.24, 0.28), "sealhide", 1, 0.04, 26)
    blob((0.05, 0.12, 0), (0.55, 0.12, 0.26), "sealbelly", 2, 0.03, 22)
    blob((0.62, 0.42, 0), (0.18, 0.15, 0.15), "sealhide", 3, 0.03, 20)
    blob((0.77, 0.4, 0), (0.08, 0.07, 0.08), "sealbelly", 4, 0.02, 14)
    for s in (-1, 1):
        blob((0.71, 0.48, s * 0.075), (0.025, 0.025, 0.025), "eye", 5, 0, 10)
        blob((0.25, 0.07, s * 0.3), (0.15, 0.03, 0.07), "sealhide", 6 + s, 0.05, 12)
        blob((-0.72, 0.08, s * 0.08), (0.14, 0.03, 0.08), "sealhide", 8 + s, 0.05, 12)


def crab():
    # a shore crab (facing +x): a carapace, eight jointed legs, two claws
    blob((0, 0.07, 0), (0.1, 0.04, 0.13), "crabshell", 1, 0.04, 18)
    for s in (-1, 1):
        for k in range(4):
            x = 0.05 - k * 0.035
            rod([(x, 0.07, s * 0.1), (x - 0.01, 0.1, s * 0.18), (x - 0.03, 0.0, s * 0.23)], 0.011, "crabdark", 4)
        rod([(0.07, 0.07, s * 0.08), (0.14, 0.08, s * 0.12)], 0.016, "crabshell", 5)
        blob((0.18, 0.08, s * 0.12), (0.05, 0.03, 0.035), "crabshell", 3 + s, 0.05, 12)
        blob((0.1, 0.11, s * 0.035), (0.012, 0.012, 0.012), "eye", 5, 0, 8)


def seachest():
    # a cache's sea chest: a domed lid, iron bands, a hasp
    box((0, 0.2, 0), (0.4, 0.2, 0.25), "house", 0.01)
    vs = []
    for x in (-0.41, 0.41):
        for k in range(9):
            a = k / 8 * math.pi
            vs.append((x, 0.4 + 0.12 * math.sin(a), 0.255 * math.cos(a)))
    quads(vs, [(i, i + 1, 9 + i + 1, 9 + i) for i in range(8)], "house", "lid")
    for x in (-0.41, 0.41):
        quads([(x, 0.4, 0.255)] + [(x, 0.4 + 0.12 * math.sin(k / 8 * math.pi), 0.255 * math.cos(k / 8 * math.pi)) for k in range(1, 9)], [tuple(range(9))], "house", "end")
    for x in (-0.28, 0.28):
        pts = [(x, 0.0, 0.26)] + [(x, 0.4 + 0.125 * math.sin(k / 8 * math.pi), 0.262 * math.cos(k / 8 * math.pi)) for k in range(9)] + [(x, 0.0, -0.26)]
        rod(pts, 0.012, "iron", 4, caps=False)
    box((0, 0.36, 0.262), (0.04, 0.06, 0.008), "brass", 0.003)


def driftwood():
    rod([(-0.9, 0.08, 0.1), (-0.3, 0.1, 0.0), (0.4, 0.09, -0.08), (0.95, 0.07, -0.02)], 0.08, "drift", 8)
    rod([(0.1, 0.1, 0.0), (0.35, 0.2, 0.3), (0.5, 0.22, 0.55)], 0.035, "drift", 6)
    rod([(-0.5, 0.1, 0.05), (-0.65, 0.3, -0.25)], 0.03, "drift", 6)


# ---------------------------------------------------------------- the grounds' scenery (the user: "improve the graphics,
# the locations as well"): what grows and stands on and in the water round the fishing grounds
def scenery_mats():
    for k, rgb, r, m in (("leaf", (0.07, 0.2, 0.06), 0.6, 0.0), ("leaf2", (0.12, 0.26, 0.05), 0.6, 0.0), ("flower", (0.6, 0.1, 0.12), 0.5, 0.0),
                         ("kelp", (0.3, 0.22, 0.06), 0.45, 0.0), ("bulb", (0.42, 0.32, 0.1), 0.35, 0.0), ("sarg", (0.45, 0.33, 0.08), 0.6, 0.0),
                         ("reed", (0.36, 0.34, 0.14), 0.8, 0.0), ("wetrock", (0.16, 0.16, 0.15), 0.35, 0.0), ("weed", (0.08, 0.16, 0.08), 0.5, 0.0),
                         ("rotwood", (0.13, 0.11, 0.08), 0.9, 0.0), ("barnacle", (0.55, 0.53, 0.48), 0.8, 0.0)):
        if k not in B.MATS:
            B.MATS[k] = B.flat(k, rgb, r, m)


def bush():
    # a jungle shrub: a clump of leafy lobes, a few broad leaves fanning out, a red flower or two
    rng = np.random.default_rng(3)
    for k in range(7):
        a = k * 0.9
        blob((math.cos(a) * 0.4 * rng.random(), 0.35 + 0.35 * rng.random(), math.sin(a) * 0.4 * rng.random()), (0.38, 0.3, 0.38), "leaf" if k % 2 else "leaf2", 30 + k, 0.18, 14)
    for k in range(8):
        a = k * 2 * math.pi / 8 + 0.2
        ca, sa = math.cos(a), math.sin(a)
        spine = [(ca * 0.9 * s, 0.25 + 0.5 * s - 0.55 * s * s, sa * 0.9 * s) for s in np.linspace(0, 1, 6)]
        vs, fs = [], []
        for i, (x, y, z) in enumerate(spine):
            w = 0.2 * math.sin(math.pi * min(1, i / 5 * 1.1))
            vs += [(x - sa * w, y, z + ca * w), (x, y + 0.03, z), (x + sa * w, y, z - ca * w)]
        for i in range(len(spine) - 1):
            fs += [(3 * i, 3 * i + 1, 3 * i + 4, 3 * i + 3), (3 * i + 1, 3 * i + 2, 3 * i + 5, 3 * i + 4)]
        quads(vs, fs, "leaf2", "leaf")
    for k in range(2):
        blob((0.25 - 0.5 * k, 0.78, 0.2 * k - 0.1), (0.07, 0.05, 0.07), "flower", 40 + k, 0.2, 10)


def shorerock():
    # a cluster of wet rocks at the waterline, weed and barnacles on their feet
    for k, (x, z, r) in enumerate(((0, 0, 0.9), (0.9, 0.4, 0.55), (-0.7, 0.6, 0.45), (0.3, -0.8, 0.5))):
        o = boulder_mesh("rock", r, "wetrock", 60 + k, 0.75)
        o.location = G(x, -0.25, z)
    for k in range(10):
        a = k * 0.63
        blob((math.cos(a) * 0.95, -0.05, math.sin(a) * 0.85), (0.22, 0.06, 0.16), "weed", 70 + k, 0.3, 10)
        blob((math.cos(a + 0.3) * 0.75, 0.15, math.sin(a + 0.3) * 0.7), (0.05, 0.03, 0.05), "barnacle", 80 + k, 0.1, 8)


def reeds():
    rng = np.random.default_rng(9)
    for k in range(26):
        x, z = (rng.random() - 0.5) * 1.4, (rng.random() - 0.5) * 1.4
        h = 1.0 + rng.random() * 0.9
        lean = (rng.random() - 0.5) * 0.4
        rod([(x, -0.3, z), (x + lean * 0.4, h * 0.6, z), (x + lean, h, z + lean * 0.3)], 0.012, "reed", 4)
        if k % 4 == 0:
            cyl((x + lean, h - 0.05, z + lean * 0.3), (x + lean, h + 0.18, z + lean * 0.3), 0.03, "rotwood", 6)   # (a bulrush head)


def kelpfloat():
    # the Weeds' canopy where it breaks the surface: a tangle of brown blades and gas bladders riding the swell (y 0 is
    # the water)
    rng = np.random.default_rng(13)
    for k in range(9):
        a = rng.random() * 2 * math.pi
        L = 1.2 + rng.random() * 1.4
        ca, sa = math.cos(a), math.sin(a)
        spine = [(ca * L * s + 0.15 * math.sin(s * 6 + k), 0.02 + 0.04 * math.sin(s * 9), sa * L * s + 0.15 * math.cos(s * 5 + k)) for s in np.linspace(0, 1, 8)]
        vs, fs = [], []
        for i, (x, y, z) in enumerate(spine):
            w = 0.16 * (0.4 + 0.6 * math.sin(math.pi * min(1, i / 7 * 1.05)))
            vs += [(x - sa * w, y, z + ca * w), (x + sa * w, y, z - ca * w)]
        for i in range(len(spine) - 1):
            fs.append((2 * i, 2 * i + 1, 2 * i + 3, 2 * i + 2))
        quads(vs, fs, "kelp", "blade")
        blob((ca * 0.3, 0.05, sa * 0.3), (0.07, 0.06, 0.07), "bulb", 90 + k, 0.05, 10)
    blob((0, 0.04, 0), (0.35, 0.08, 0.3), "kelp", 99, 0.3, 14)


def sargassum():
    rng = np.random.default_rng(17)
    for k in range(16):
        blob(((rng.random() - 0.5) * 3.0, 0.02, (rng.random() - 0.5) * 2.0), (0.4 + 0.3 * rng.random(), 0.05, 0.3 + 0.2 * rng.random()), "sarg", 100 + k, 0.35, 10)
    for k in range(20):
        blob(((rng.random() - 0.5) * 3.0, 0.07, (rng.random() - 0.5) * 2.0), (0.035, 0.035, 0.035), "bulb", 140 + k, 0.0, 8)


def wreckribs():
    # an old hull's ribs standing out of the water, the keel and a stub of mast, weed at the waterline
    for k in range(9):
        x = -3.2 + k * 0.8
        hgt = 1.6 + 0.9 * math.sin(k * 1.3) ** 2 - 0.15 * k
        for s in (-1, 1):
            pts = [(x, -1.5, 0.0), (x, -0.6, s * 1.5), (x, 0.4, s * 1.85), (x, hgt * 0.8, s * (1.9 - 0.2 * hgt))]
            if (k + (s > 0)) % 3 != 0:
                rod(pts, 0.09, "rotwood", 6)
    rod([(-3.4, -0.3, 0.0), (3.4, 0.1, 0.0)], 0.16, "rotwood", 8)
    rod([(0.5, -0.2, 0.0), (0.9, 2.6, 0.3)], 0.14, "rotwood", 8)
    rod([(-2.0, 0.45, -1.7), (1.5, 0.35, -1.75)], 0.07, "rotwood", 6)   # (a stringer still on)
    for k in range(12):
        blob((-3.0 + k * 0.55, 0.0, (1 if k % 2 else -1) * 1.8), (0.25, 0.06, 0.12), "weed", 160 + k, 0.3, 10)


def column():
    # a fluted marble column broken off, its drum fallen beside it, the base standing in the water (y 0 the surface)
    segs = 16
    lathe((0, 0, 0), [(0.62, -2.0), (0.62, -0.2), (0.55, 0.0), (0.48, 0.15)] + [(0.45, 0.15 + t * 3.6) for t in np.linspace(0, 1, 5)], "marble", 20)
    for k in range(12):   # the flutes
        a = k * 2 * math.pi / 12
        rod([(math.cos(a) * 0.45, 0.2, math.sin(a) * 0.45), (math.cos(a) * 0.45, 3.7, math.sin(a) * 0.45)], 0.035, "marble", 4)
    vs = []
    for k in range(segs):   # the broken top, jagged
        a = k * 2 * math.pi / segs
        vs.append((math.cos(a) * 0.46, 3.75 + 0.35 * abs(math.sin(k * 1.7)), math.sin(a) * 0.46))
    quads(vs + [(0, 3.95, 0)], [(i, (i + 1) % segs, segs) for i in range(segs)], "marble", "break")
    o = boulder_mesh("drum", 0.5, "marble", 3, 0.9)
    o.scale = (1.0, 1.0, 1.3); o.rotation_euler = (0, math.radians(84), 0.4); o.location = G(1.3, -0.1, 0.6)
    for k in range(6):
        blob((math.cos(k) * 0.66, 0.0, math.sin(k) * 0.66), (0.18, 0.08, 0.12), "weed", 180 + k, 0.3, 10)


def archruin():
    # two piers and the arch between them, half fallen: Atlantis showing above the water
    for s in (-1, 1):
        box((s * 2.2, 0.5, 0), (0.55, 2.5, 0.55), "marble", 0.05)
        box((s * 2.2, 3.05, 0), (0.7, 0.12, 0.7), "marble", 0.03)
    pts = []
    for k in range(9):
        a = math.pi - k / 8 * math.pi * 0.62   # (only part of the arch still stands)
        pts.append((math.cos(a) * 2.2, 3.1 + math.sin(a) * 1.8, 0))
    rod(pts, 0.42, "marble", 8)
    o = boulder_mesh("fallen", 0.6, "marble", 8, 0.7); o.location = G(1.4, -0.2, 1.1)
    for k in range(8):
        blob((math.cos(k * 0.8) * 2.5, 0.0, math.sin(k * 0.8) * 0.8), (0.2, 0.07, 0.14), "weed", 200 + k, 0.3, 10)


def stalagmite():
    prof = [(0.5 * (1 - t) ** 1.2 + 0.03, t * 3.2) for t in np.linspace(0, 1, 9)]
    lathe((0, 0, 0), [(0.62, -1.0)] + prof, "caverock", 12)


def terrain_mats():
    # the ground's tiling sets, each on a token quad, for the game to put on the land it builds
    if "deck" not in B.MATS:
        B.MATS["deck"] = B.tile_mat(all_tiles()["deck"])
    for i, k in enumerate(("sand", "wetsand", "jungle", "rock", "caverock", "deck", "marble")):
        quads([(i, 0, 0), (i + 0.5, 0, 0), (i + 0.5, 0, 0.5), (i, 0, 0.5)], [(0, 1, 2, 3)], k, k)


def build(name, fn, out):
    reset()
    land_mats()
    clutter_mats()
    scenery_mats()
    fn()
    B.finish(out, name + ".glb", lambda x, y, z, k: k)


def main():
    out = C.out_dir()
    for name, fn in (("hatch_cover", hatch_cover), ("batten", batten), ("door", door), ("buoy_red", lambda: buoy("red")),
                     ("buoy_green", lambda: buoy("green")), ("life_ring", life_ring), ("skiff", skiff), ("oar", oar),
                     ("palm", palm), ("hut", hut), ("beached_sloop", beached_sloop), ("firering", firering), ("boulder", boulder),
                     ("stonehut", stonehut), ("stove", stove), ("cannery", cannery), ("boiler", boiler), ("tower", tower),
                     ("shrine", shrine), ("stair", stair), ("tent", tent), ("skullpost", skullpost), ("brazier", brazier),
                     ("stalactite", stalactite), ("mould", mould), ("terrain_mats", terrain_mats),
                     ("bucket", bucket), ("fishbox", fishbox), ("crate", crate), ("lobsterpot", lobsterpot), ("ropecoil", ropecoil),
                     ("tacklebox", tacklebox), ("oilcan", oilcan), ("mop", mop), ("netpile", netpile), ("tarp", tarp),
                     ("oilskin", oilskin), ("seal", seal), ("crab", crab), ("seachest", seachest), ("driftwood", driftwood),
                     ("bush", bush), ("shorerock", shorerock), ("reeds", reeds), ("kelpfloat", kelpfloat), ("sargassum", sargassum),
                     ("wreckribs", wreckribs), ("column", column), ("archruin", archruin), ("stalagmite", stalagmite)):
        a = C.args()
        if "--only" in a and name not in a[a.index("--only") + 1].split(","):
            continue
        build(name, fn, out)


if __name__ == "__main__" and not os.environ.get("ARTGEN_IMPORT_TRAWLPROPS"):
    main()
