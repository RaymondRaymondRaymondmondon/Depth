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
from boat import box, cyl, rod, disc, lathe, quads


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


def build(name, fn, out):
    reset()
    fn()
    B.finish(out, name + ".glb", lambda x, y, z, k: k)


def main():
    out = C.out_dir()
    for name, fn in (("hatch_cover", hatch_cover), ("batten", batten), ("door", door), ("buoy_red", lambda: buoy("red")),
                     ("buoy_green", lambda: buoy("green")), ("life_ring", life_ring), ("skiff", skiff), ("oar", oar)):
        a = C.args()
        if "--only" in a and name not in a[a.index("--only") + 1].split(","):
            continue
        build(name, fn, out)


main()
