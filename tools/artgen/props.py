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


def terrain_mats():
    # the ground's tiling sets, each on a token quad, for the game to put on the land it builds
    if "deck" not in B.MATS:
        B.MATS["deck"] = B.tile_mat(all_tiles()["deck"])
    for i, k in enumerate(("sand", "wetsand", "jungle", "rock", "caverock", "deck", "marble")):
        quads([(i, 0, 0), (i + 0.5, 0, 0), (i + 0.5, 0, 0.5), (i, 0, 0.5)], [(0, 1, 2, 3)], k, k)


def build(name, fn, out):
    reset()
    land_mats()
    fn()
    B.finish(out, name + ".glb", lambda x, y, z, k: k)


def main():
    out = C.out_dir()
    for name, fn in (("hatch_cover", hatch_cover), ("batten", batten), ("door", door), ("buoy_red", lambda: buoy("red")),
                     ("buoy_green", lambda: buoy("green")), ("life_ring", life_ring), ("skiff", skiff), ("oar", oar),
                     ("palm", palm), ("hut", hut), ("beached_sloop", beached_sloop), ("firering", firering), ("boulder", boulder),
                     ("stonehut", stonehut), ("stove", stove), ("cannery", cannery), ("boiler", boiler), ("tower", tower),
                     ("shrine", shrine), ("stair", stair), ("tent", tent), ("skullpost", skullpost), ("brazier", brazier),
                     ("stalactite", stalactite), ("mould", mould), ("terrain_mats", terrain_mats)):
        a = C.args()
        if "--only" in a and name not in a[a.index("--only") + 1].split(","):
            continue
        build(name, fn, out)


main()
