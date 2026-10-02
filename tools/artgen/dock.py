# The harbour quay (the Trawl Visual Overhaul Spec, phase 6): a timber wharf on piles beside the Gannet's port side,
# the sheds behind it (the Gunsmith, the Chandler, the Fish Market and the Owners' scales, the Owners' office, the
# slipway's hut), gas lamps, bollards, the gangplank, the chalkboard, a crane and the harbour's clutter.
#
# The moored frame (trawl_view3d.cpp's old BuildQuay): x along her, y up, z toward her starboard; the quay's top is
# QUAY_Y = 1.45 and its face is at z = -3.9; the walkable quay is x -13.5 .. 13.5, z -9.6 .. -3.9
# (QuayWalkable); the stations stand at z -8.2 in front of their sheds (DockStations in trawl_session.cpp); the lamp
# posts' glows are at (x, QUAY_Y + 3.2, -6.8) for x in -9, -1, 7, 13 and the sheds' window glows at WINDOWS below.
#     blender -b --factory-startup -P tools/artgen/dock.py -- --out assets/trawl

import bpy
import math
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import common as C
import boat as B
from boat import box, cyl, rod, disc, lathe, quads, G

Q = 1.45
FACE = -3.9
FRONT = -9.8                        # the sheds' fronts
SHEDS = [  # (centre x, half width, name on the board, kind)
    (-7.6, 1.5, "GUNSMITH", "gun"), (-3.5, 1.7, "CHANDLER", "shop"), (3.0, 2.2, "FISH MARKET", "market"),
    (8.0, 1.5, "OWNERS", "office"), (12.5, 1.4, "SLIPWAY", "hut")]
WINDOWS = [(x + 0.55 * w, Q + 1.55, FRONT + 0.02) for (x, w, _, k) in SHEDS if k != "market"]


def text(body, size, at, mat, depth=0.006):
    """Raised lettering on a board facing the quay (+z), centred on 'at'."""
    bpy.ops.object.text_add(location=(0, 0, 0))
    t = bpy.context.active_object
    t.data.body = body
    t.data.size = size
    t.data.extrude = depth
    t.data.align_x = 'CENTER'
    t.data.align_y = 'CENTER'
    C.select_only([t]); bpy.ops.object.convert(target='MESH')
    t = bpy.context.active_object
    t.location = G(*at)
    t.rotation_euler = (math.radians(90), 0, 0)
    return B.put(t, mat)


def wharf():
    # the deck: planks on stringers on round piles, a cap log along the face, fender piles, an iron ladder
    box((0, Q - 0.05, (FACE + FRONT) / 2), (15.0, 0.05, (FRONT - FACE) / -2), "deck", 0.0)
    for z in (-4.15, -5.6, -7.1, -8.6):
        box((0, Q - 0.17, z), (15.0, 0.07, 0.1), "house", 0.01)
    box((0, Q - 0.12, FACE - 0.08), (15.05, 0.13, 0.1), "house", 0.015)
    for x in np.arange(-14.0, 14.01, 2.0):
        for z in (-4.05, -6.3, -8.6):
            cyl((x, -3.0, z), (x, Q - 0.2, z), 0.16, "house", 12, bevel=0)
        if x < 14:   # cross bracing between the face piles
            rod([(x, -1.2, -4.05), (x + 2.0, Q - 0.3, -4.05)], 0.06, "house", 6)
    for x in (-11.0, -4.0, 4.0, 11.0):
        for dx in (-0.9, 0.9):
            cyl((x + dx, -1.8, FACE + 0.12), (x + dx, Q + 0.15, FACE + 0.12), 0.13, "house", 10)
    for k in range(9):
        y = Q - 0.25 - k * 0.32
        cyl((-6.25, y, FACE + 0.05), (-5.75, y, FACE + 0.05), 0.015, "iron", 6)
    for s in (-1, 1):
        rod([(-6.0 + s * 0.25, Q - 2.9, FACE + 0.05), (-6.0 + s * 0.25, Q + 0.6, FACE + 0.05), (-6.0 + s * 0.25, Q + 0.6, FACE - 0.25), (-6.0 + s * 0.25, Q, FACE - 0.25)], 0.022, "iron", 6)
    # the stone apron the sheds stand on, its sea wall at the ends, and a low wall at the back
    box((0, Q - 2.2, -12.7), (15.5, 2.2, 2.9), "stone", 0.0)
    box((0, Q + 0.5, -15.4), (15.5, 0.5, 0.25), "stone", 0.03)
    # iron bollards and the gangplank (cleated boards between rope rails)
    for x in (-11.0, -4.0, 4.0, 11.0):
        lathe((x, 0, -4.2), [(0.2, Q), (0.17, Q + 0.05), (0.13, Q + 0.12), (0.12, Q + 0.38), (0.18, Q + 0.44), (0.17, Q + 0.5), (0.0001, Q + 0.52)], "iron", 16)
    box((0, Q - 0.05, -3.15), (1.1, 0.04, 0.8), "house", 0.01)
    for k in range(5):
        box((0, Q - 0.0, -3.15 - 0.6 + k * 0.3), (1.0, 0.015, 0.02), "house", 0.004)
    for s in (-1, 1):
        for z in (-3.85, -2.45):
            cyl((s * 1.05, Q - 0.05, z), (s * 1.05, Q + 0.85, z), 0.025, "iron", 6)
        rod([(s * 1.05, Q + 0.85, -3.85), (s * 1.05, Q + 0.78, -3.15), (s * 1.05, Q + 0.85, -2.45)], 0.014, "rope", 4)


def shed(x, w, name, kind):
    d0, d1 = FRONT, FRONT - 2.8
    eave, ridge = Q + 2.8, Q + 3.75
    zc = (d0 + d1) / 2
    # the walls: weathered boards; the gable ends carry the roof's triangle
    box((x, Q + 1.4, d1), (w, 1.4, 0.05), "shed", 0.0)
    for s in (-1, 1):
        box((x + s * w, Q + 1.4, zc), (0.05, 1.4, 1.4), "shed", 0.0)
        quads([(x + s * w, eave, d0), (x + s * w, eave, d1), (x + s * w, ridge, zc)], [(0, 1, 2)], "shed", "gable")
    if kind == "market":   # open-fronted, posts and a lintel, the shutters propped up as an awning
        for px in (x - w, x - w / 3, x + w / 3, x + w):
            box((px, Q + 1.3, d0), (0.08, 1.3, 0.08), "house")
        box((x, eave - 0.15, d0), (w + 0.05, 0.15, 0.08), "house")
        quads([(x - w, eave - 0.3, d0 + 0.05), (x + w, eave - 0.3, d0 + 0.05), (x + w, eave - 0.6, d0 + 1.1), (x - w, eave - 0.6, d0 + 1.1)], [(0, 1, 2, 3)], "shed", "awning")
        for px in (x - w + 0.1, x + w - 0.1):
            rod([(px, eave - 0.32, d0 + 0.05), (px, eave - 0.62, d0 + 1.08)], 0.03, "house", 6)
        # the fish trestles inside and their boxes, the Owners' steelyard scales on a tripod by the quota station
        for tx in (x - 1.1, x + 0.0):
            box((tx, Q + 0.85, d0 - 0.7), (0.5, 0.03, 0.35), "zinc", 0.005)
            for sx in (-1, 1):
                for sz in (-1, 1):
                    box((tx + sx * 0.45, Q + 0.42, d0 - 0.7 + sz * 0.3), (0.03, 0.42, 0.03), "house")
            box((tx + 0.15, Q + 0.95, d0 - 0.65), (0.25, 0.08, 0.18), "house", 0.008)
        sx_, sz_ = 4.6, -9.25
        for k in range(3):
            a = k * 2 * math.pi / 3 + 0.4
            rod([(sx_ + math.cos(a) * 0.55, Q, sz_ + math.sin(a) * 0.45), (sx_, Q + 2.1, sz_)], 0.035, "house", 6)
        rod([(sx_ - 0.45, Q + 1.95, sz_), (sx_ + 0.35, Q + 2.0, sz_)], 0.025, "iron", 6)
        lathe((sx_ + 0.25, 0, sz_), [(0.0001, Q + 1.92), (0.08, Q + 1.92), (0.08, Q + 1.98), (0.0001, Q + 1.98)], "brass", 10)
        rod([(sx_ - 0.35, Q + 1.95, sz_), (sx_ - 0.35, Q + 1.25, sz_)], 0.008, "iron", 4)
        lathe((sx_ - 0.35, 0, sz_), [(0.0001, Q + 1.2), (0.25, Q + 1.24), (0.27, Q + 1.28), (0.0001, Q + 1.26)], "brass", 16)
    else:
        box((x, Q + 1.4, d0), (w, 1.4, 0.05), "shed", 0.0)
        # the door (boards, a frame, a latch), the window (frame, glass, sill) to its right
        box((x - 0.15, Q + 1.0, d0 + 0.04), (0.48, 1.0, 0.03), "house", 0.008)
        box((x - 0.15, Q + 2.05, d0 + 0.06), (0.56, 0.06, 0.05), "cream")
        for s in (-1, 1):
            box((x - 0.15 + s * 0.53, Q + 1.0, d0 + 0.06), (0.05, 1.02, 0.05), "cream")
        box((x + 0.2, Q + 1.0, d0 + 0.08), (0.03, 0.05, 0.02), "iron", 0.005)
        wx = x + 0.55 * w
        box((wx, Q + 1.55, d0 + 0.02), (0.32, 0.3, 0.02), "glass", 0.0)
        for s in (-1, 1):
            box((wx + s * 0.34, Q + 1.55, d0 + 0.06), (0.03, 0.33, 0.04), "cream")
            box((wx, Q + 1.55 + s * 0.31, d0 + 0.06), (0.36, 0.03, 0.04), "cream")
        box((wx, Q + 1.55, d0 + 0.05), (0.01, 0.28, 0.02), "cream", 0.0)
        box((wx, Q + 1.55, d0 + 0.05), (0.3, 0.01, 0.02), "cream", 0.0)
        box((wx, Q + 1.21, d0 + 0.12), (0.4, 0.025, 0.1), "cream")
        if kind == "gun":   # bars over the Gunsmith's window, and his sign of a long gun hung on a bracket
            for k in range(5):
                rod([(wx - 0.24 + k * 0.12, Q + 1.25, d0 + 0.1), (wx - 0.24 + k * 0.12, Q + 1.85, d0 + 0.1)], 0.012, "iron", 4)
            rod([(x - w + 0.1, Q + 2.5, d0), (x - w + 0.1, Q + 2.5, d0 + 0.9)], 0.025, "iron", 6)
            rod([(x - w + 0.1, Q + 2.5, d0 + 0.7), (x - w + 0.1, Q + 2.1, d0 + 0.7)], 0.01, "iron", 4)
            box((x - w + 0.1, Q + 1.9, d0 + 0.7), (0.03, 0.22, 0.42), "house")
            rod([(x - w + 0.14, Q + 1.85, d0 + 0.32), (x - w + 0.14, Q + 1.98, d0 + 1.05)], 0.018, "iron", 6)
            box((x - w + 0.14, Q + 1.8, d0 + 0.42), (0.02, 0.08, 0.12), "house", 0.01)
    # the signboard over the front, its lettering
    box((x, eave - 0.25, d0 + 0.08), (min(w - 0.1, 0.18 * len(name) + 0.25), 0.17, 0.025), "house", 0.008)
    text(name, 0.2, (x, eave - 0.25, d0 + 0.11), "paint_white")
    # the roof: tarred boards in two pitches with an overhang, a ridge board, a stovepipe on two of them
    for s in (-1, 1):
        e = d0 + 0.35 if s > 0 else d1 - 0.35
        quads([(x - w - 0.25, eave - 0.12, e), (x + w + 0.25, eave - 0.12, e), (x + w + 0.25, ridge + 0.05, zc), (x - w - 0.25, ridge + 0.05, zc)], [(0, 1, 2, 3)], "shingle", "roof")
    box((x, ridge + 0.07, zc), (w + 0.25, 0.05, 0.08), "house", 0.01)
    if kind in ("gun", "office", "shop"):
        cyl((x + w * 0.5, ridge - 0.4, zc - 0.6), (x + w * 0.5, ridge + 0.8, zc - 0.6), 0.08, "iron", 10)
        lathe((x + w * 0.5, 0, zc - 0.6), [(0.0001, ridge + 0.78), (0.18, ridge + 0.82), (0.0001, ridge + 0.95)], "iron", 10)


def furniture():
    # gas lamps: a fluted base, a column with a ladder bar, a glazed lantern round the glow at QUAY_Y + 3.2
    for x in (-9.0, -1.0, 7.0, 13.0):
        z = -6.8
        lathe((x, 0, z), [(0.16, Q), (0.15, Q + 0.1), (0.1, Q + 0.2), (0.09, Q + 0.6), (0.07, Q + 0.7), (0.055, Q + 2.9), (0.08, Q + 2.96)], "iron", 12)
        cyl((x - 0.3, Q + 2.6, z), (x + 0.3, Q + 2.6, z), 0.02, "iron", 6)
        for k in range(4):
            a = k * math.pi / 2 + math.pi / 4
            rod([(x + math.cos(a) * 0.15, Q + 2.98, z + math.sin(a) * 0.15), (x + math.cos(a) * 0.2, Q + 3.42, z + math.sin(a) * 0.2)], 0.012, "iron", 4)
        lathe((x, 0, z), [(0.0001, Q + 2.96), (0.16, Q + 2.97), (0.16, Q + 3.0), (0.0001, Q + 3.0)], "iron", 12)
        lathe((x, 0, z), [(0.24, Q + 3.42), (0.26, Q + 3.45), (0.12, Q + 3.6), (0.03, Q + 3.68), (0.05, Q + 3.72), (0.0001, Q + 3.76)], "iron", 12)
    # the chalkboard on its easel, chalked
    box((-8.5, Q + 1.25, -5.5), (0.82, 0.62, 0.03), "house", 0.01)
    box((-8.5, Q + 1.25, -5.47), (0.74, 0.54, 0.01), "coal", 0.0)
    for k in range(5):
        box((-8.75 + (k % 2) * 0.1, Q + 1.62 - k * 0.16, -5.455), (0.4 - k * 0.05, 0.012, 0.003), "paint_white", 0.0)
    for s in (-1, 1):
        rod([(-8.5 + s * 0.7, Q, -5.35), (-8.5 + s * 0.6, Q + 1.9, -5.52)], 0.025, "house", 6)
    rod([(-8.5, Q + 1.85, -5.55), (-8.5, Q, -6.1)], 0.022, "house", 6)
    box((-8.5, Q + 0.6, -5.47), (0.82, 0.025, 0.05), "house")
    # the slipway's cradle and its hand winch
    for s in (-1, 1):
        box((12.0 + s * 0.9, Q + 0.1, -5.2), (0.12, 0.1, 1.25), "house", 0.01)
    for k in range(4):
        box((12.0, Q + 0.24, -6.1 + k * 0.6), (1.1, 0.05, 0.08), "house", 0.008)
    box((12.0, Q + 0.4, -6.9), (0.35, 0.4, 0.25), "iron")
    cyl((11.6, Q + 0.65, -6.9), (12.4, Q + 0.65, -6.9), 0.12, "rope", 12)
    for s in (-1, 1):
        rod([(12.0 + s * 0.45, Q + 0.65, -6.9), (12.0 + s * 0.45, Q + 0.65, -6.6), (12.0 + s * 0.55, Q + 0.65, -6.6)], 0.02, "iron", 4)
    # a cargo crane at the west end: a post, a jib out over the water, guys, a fall and hook
    box((-12.8, Q + 0.15, -5.0), (0.5, 0.15, 0.5), "stone", 0.02)
    cyl((-12.8, Q + 0.3, -5.0), (-12.8, Q + 4.2, -5.0), 0.16, "house", 12, r2=0.13)
    rod([(-12.8, Q + 0.9, -5.0), (-12.8, Q + 4.6, -2.2)], 0.1, "house", 8)
    rod([(-12.8, Q + 4.2, -5.0), (-12.8, Q + 4.6, -2.2)], 0.02, "iron", 4)
    rod([(-12.8, Q + 4.1, -5.0), (-13.6, Q, -6.8)], 0.015, "iron", 4)
    rod([(-12.8, Q + 4.55, -2.25), (-12.8, Q + 2.2, -2.25)], 0.012, "rope", 4)
    rod([(-12.8, Q + 2.2, -2.25), (-12.8, Q + 2.0, -2.25), (-12.75, Q + 1.9, -2.2), (-12.7, Q + 1.95, -2.3)], 0.018, "iron", 4)
    lathe((-12.8, 0, -5.0), [(0.0001, Q + 0.6), (0.22, Q + 0.6), (0.22, Q + 0.9), (0.0001, Q + 0.9)], "iron", 14)


def clutter():
    # crates and barrels at the west end and between the sheds, fish boxes, lobster pots, coils by the bollards,
    # a net hung to dry on a rail
    for (x, z, y, r) in ((-13.0, -9.1, 0, 0.1), (-12.3, -9.2, 0, -0.05), (-12.65, -9.15, 0.6, 0.2), (5.7, -9.3, 0, 0.05)):
        box((x, Q + 0.3 + y, z), (0.3, 0.3, 0.3), "house", 0.01, rot_y=r)
        for k in (-1, 1):
            box((x, Q + 0.3 + y + k * 0.2, z), (0.31, 0.03, 0.31), "house", 0.004, rot_y=r)
    for (x, z) in ((-10.4, -9.35), (-9.9, -9.4), (10.2, -9.35)):
        lathe((x, 0, z), [(0.0001, Q), (0.24, Q), (0.28, Q + 0.4), (0.24, Q + 0.8), (0.0001, Q + 0.8)], "house", 16)
        for h in (0.15, 0.65):
            lathe((x, 0, z), [(0.265, Q + h - 0.02), (0.27, Q + h + 0.02)], "iron", 16)
    for (x, z, y) in ((10.6, -9.2, 0), (10.9, -8.9, 0), (10.75, -9.05, 0.42)):
        for k in range(5):
            a = k * math.pi / 4
            rod([(x - 0.3, Q + y + 0.2 + math.sin(a) * 0.2, z + math.cos(a) * 0.2), (x + 0.3, Q + y + 0.2 + math.sin(a) * 0.2, z + math.cos(a) * 0.2)], 0.008, "house", 4)
        box((x, Q + y + 0.02, z), (0.32, 0.02, 0.22), "house", 0.004)
    for x in (-11.0, -4.0, 4.0, 11.0):
        for k in range(3):
            pts = [(x + 0.55 + math.cos(a) * (0.28 - k * 0.04), Q + 0.03 + k * 0.045, -4.5 + math.sin(a) * (0.28 - k * 0.04)) for a in np.linspace(0, 2 * math.pi, 16)]
            rod(pts, 0.025, "rope", 6, caps=False)
    for s in (-1, 1):
        cyl((-1.2 + s * 1.3, Q, -9.45), (-1.2 + s * 1.3, Q + 1.6, -9.45), 0.05, "house", 8)
    cyl((-2.5, Q + 1.6, -9.45), (0.1, Q + 1.6, -9.45), 0.03, "house", 8)
    pts = []
    for k in range(13):
        u = k / 12
        pts.append((-2.5 + 2.6 * u, Q + 1.6 - 0.9 - 0.2 * math.sin(u * math.pi * 3), -9.45 + 0.05 * math.sin(u * 9)))
    quads([(p[0], Q + 1.6, -9.45) for p in pts] + pts, [(k, k + 1, 13 + k + 1, 13 + k) for k in range(12)], "canvas", "net")


def dock_grime(x, y, z, k):
    if abs(y - Q) < 0.02 and z > FACE - 0.6:     # salt and weed along the wharf's edge
        k *= 0.75 + 0.25 * min(1.0, (FACE - z) / 0.6) if z < FACE else 0.75
    if y < 0.3:                                   # the piles darken toward the water and below it
        k *= 0.5 + 0.5 * max(0.0, min(1.0, (y + 1.0) / 1.3))
    return k


def main():
    out = C.out_dir()
    C.reset()
    B.setup_mats(("deck", "house", "cream", "iron", "stone", "shingle", "shed"))
    wharf()
    for s in SHEDS:
        shed(*s)
    furniture(); clutter()
    B.finish(out, "dock.glb", dock_grime)


main()
