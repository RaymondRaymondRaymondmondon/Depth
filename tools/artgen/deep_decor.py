# The Deep's decorations (stage 7: the design doc's Artisan Bench cosmetics catalog), and the satchel a diver leaves
# where they die. One glb, Decor/decor.glb, with up to three objects per item: "<id>" (opaque, vertex-coloured),
# "<id>_glass" (drawn with Deep/Glass) and "<id>_glow" (drawn emissive: the lights). Each item's origin is where it
# mounts: floor items stand on y = 0, wall items hang on the wall at z = 0 and stand out along +z, ceiling items hang
# down from y = 0. +y up, metres, built round the item's centre in x.
#     blender -b --factory-startup -P tools/artgen/deep_decor.py -- --out TheDeep/Assets/Deep/Resources/Decor
import bpy
import math
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deep_creatures as K
from deep_creatures import MB, loft, tube, ellipsoid, cone, membrane, mix, clamp

WOOD = (0.45, 0.28, 0.15)
WOOD_D = (0.28, 0.16, 0.08)
WOOD_L = (0.6, 0.42, 0.24)
BRASS = (0.74, 0.55, 0.25)
BRASS_D = (0.46, 0.32, 0.13)
IRON = (0.27, 0.29, 0.29)
RUST = (0.42, 0.24, 0.12)
BONE = (0.88, 0.84, 0.72)
BONE_D = (0.66, 0.6, 0.48)
KELP = (0.32, 0.38, 0.14)
KELP_D = (0.2, 0.25, 0.08)
FAT = (0.82, 0.72, 0.6)
LEATHER = (0.36, 0.22, 0.12)
STONE = (0.09, 0.09, 0.1)
CLOTH = (0.55, 0.6, 0.45)
PAPER = (0.86, 0.8, 0.62)
RED = (0.62, 0.16, 0.12)
BLACK = (0.05, 0.05, 0.05)


def c4(c):
    return (c[0], c[1], c[2], 0.0)


def box(mb, c, h, col, top=None):
    """An axis box (centre c, half sizes h) with its own vertices per face, so it shades flat."""
    x, y, z = c
    a, b, d = h
    P = [(x - a, y - b, z - d), (x + a, y - b, z - d), (x + a, y + b, z - d), (x - a, y + b, z - d),
         (x - a, y - b, z + d), (x + a, y - b, z + d), (x + a, y + b, z + d), (x - a, y + b, z + d)]
    faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (3, 7, 6, 2), (0, 4, 7, 3), (1, 2, 6, 5)]
    for k, f in enumerate(faces):
        cc = c4(top) if top and k == 3 else c4(col)
        ix = [mb.vert(P[i], cc) for i in f]
        mb.face(*ix)


def cyl(mb, a, b, r, col, segs=12, r1=None):
    tube(mb, [a, b], [r, r if r1 is None else r1], segs, c4(col))


def ring(mb, centre, normal, r, thick, col, segs=20):
    t, side, up = K.frame(normal)
    pts = []
    for k in range(segs + 1):
        a = k / segs * math.pi * 2
        pts.append(K.add(centre, K.add(K.mul(side, math.cos(a) * r), K.mul(up, math.sin(a) * r))))
    tube(mb, pts, [thick] * len(pts), 6, c4(col), cap=False)


def disc(mb, c, normal, r, col, segs=20):
    t, side, up = K.frame(normal)
    ci = mb.vert(c, c4(col))
    base = len(mb.v)
    for k in range(segs):
        a = k / segs * math.pi * 2
        mb.vert(K.add(c, K.add(K.mul(side, math.cos(a) * r), K.mul(up, math.sin(a) * r))), c4(col))
    for k in range(segs):
        mb.face(ci, base + k, base + (k + 1) % segs)


def legs(mb, w, d, h, r, col):
    for sx in (-1, 1):
        for sz in (-1, 1):
            cyl(mb, (sx * w, 0, sz * d), (sx * w, h, sz * d), r, col, 8)


# ------------------------------------------------------------------------------------------------ the catalog
def leviathan_bone_display():
    mb = MB()
    box(mb, (0, 0.6, 0.03), (0.55, 0.42, 0.03), WOOD_D)
    pts = [(-0.45, 0.4, 0.12), (-0.2, 0.62, 0.16), (0.1, 0.72, 0.17), (0.4, 0.66, 0.15), (0.5, 0.5, 0.12)]
    tube(mb, pts, [0.07, 0.09, 0.1, 0.08, 0.05], 10, c4(BONE))
    for x in (-0.35, 0.35):
        cyl(mb, (x, 0.5, 0.04), (x, 0.55, 0.13), 0.02, BRASS, 6)
    return mb, None, None


def bioluminescent_wall_planter():
    mb, gl, gw = MB(), MB(), MB()
    box(mb, (0, 0.5, 0.05), (0.42, 0.06, 0.05), BRASS_D)
    box(mb, (0, 0.95, 0.05), (0.42, 0.04, 0.05), BRASS_D)
    box(gl, (0, 0.72, 0.08), (0.4, 0.2, 0.06), (0.6, 0.8, 0.8))
    for k in range(9):
        x = -0.32 + k * 0.08
        tube(gw, [(x, 0.56, 0.08), (x + 0.02, 0.68 + 0.1 * math.sin(k), 0.09)], [0.02, 0.008], 5, c4((0.3, 0.95, 0.85)))
    return mb, gl, gw


def woven_kelp_rug():
    mb = MB()
    for i in range(12):
        z = -0.9 + i * 0.163
        tube(mb, [(-1.3, 0.012, z), (1.3, 0.012, z)], [0.04, 0.04], 6, c4(mix(KELP, KELP_D, (i % 3) / 2)))
    for x in (-1.33, 1.33):
        tube(mb, [(x, 0.012, -0.95), (x, 0.012, 0.95)], [0.035, 0.035], 6, c4(KELP_D))
    return mb, None, None


def galleon_brass_porthole_frame():
    mb = MB()
    ring(mb, (0, 1.4, 0.04), (0, 0, 1), 0.62, 0.06, BRASS, 28)
    ring(mb, (0, 1.4, 0.07), (0, 0, 1), 0.5, 0.035, BRASS_D, 28)
    for k in range(12):
        a = k / 12 * math.pi * 2
        ellipsoid(mb, (math.cos(a) * 0.56, 1.4 + math.sin(a) * 0.56, 0.1), (0.025, 0.025, 0.025), c4(BRASS_D), 6, 4)
    for k in range(4):
        a = k / 4 * math.pi * 2 + math.pi / 4
        cone(mb, (math.cos(a) * 0.66, 1.4 + math.sin(a) * 0.66, 0.05), (math.cos(a) * 0.8, 1.4 + math.sin(a) * 0.8, 0.05), 0.05, c4(BRASS), 6)
    return mb, None, None


def captains_log_desk():
    mb, gl, gw = MB(), MB(), MB()
    box(mb, (0, 0.76, 0), (0.7, 0.03, 0.38), WOOD, top=WOOD_L)
    box(mb, (-0.48, 0.38, 0), (0.2, 0.36, 0.34), WOOD_D)
    legs(mb, 0.62, 0.3, 0.74, 0.03, WOOD_D)
    box(mb, (0.15, 0.8, 0.02), (0.18, 0.015, 0.13), PAPER)
    box(mb, (0.14, 0.82, 0.02), (0.01, 0.004, 0.13), LEATHER)
    cyl(mb, (-0.45, 0.79, -0.1), (-0.45, 0.93, -0.1), 0.03, BRASS, 8)
    ellipsoid(gw, (-0.45, 0.97, -0.1), (0.05, 0.05, 0.05), c4((1.0, 0.85, 0.5)), 8, 5)
    return mb, gl, gw


def galleon_captains_bed():
    mb = MB()
    box(mb, (0, 0.3, 0), (0.55, 0.12, 1.05), WOOD)
    box(mb, (0, 0.5, 0), (0.5, 0.09, 0.98), CLOTH)
    box(mb, (0, 0.62, -0.78), (0.4, 0.06, 0.15), PAPER)
    box(mb, (0, 0.85, -1.06), (0.58, 0.55, 0.04), WOOD_D)
    box(mb, (0, 0.6, 1.06), (0.58, 0.32, 0.04), WOOD_D)
    for sx in (-1, 1):
        for z in (-1.06, 1.06):
            cyl(mb, (sx * 0.56, 0, z), (sx * 0.56, 1.45 if z < 0 else 1.0, z), 0.04, WOOD_D, 8)
            ellipsoid(mb, (sx * 0.56, 1.47 if z < 0 else 1.02, z), (0.05, 0.05, 0.05), c4(BRASS), 6, 4)
    for k in range(5):
        z = -0.6 + k * 0.3
        box(mb, (0, 0.6, z), (0.51, 0.006, 0.02), KELP_D)
    return mb, None, None


def grazer_blubber_lounge_chair():
    mb = MB()
    ellipsoid(mb, (0, 0.28, 0.05), (0.5, 0.3, 0.48), c4(FAT), 16, 10)
    ellipsoid(mb, (0, 0.55, -0.28), (0.45, 0.32, 0.2), c4(mix(FAT, (0.7, 0.55, 0.45), 0.4)), 14, 8)
    return mb, None, None


def dreadnought_brass_locker():
    mb = MB()
    box(mb, (0, 0.9, 0), (0.4, 0.9, 0.28), IRON, top=RUST)
    box(mb, (0, 0.9, 0.285), (0.36, 0.84, 0.01), BRASS_D)
    for y in (0.35, 0.9, 1.45):
        box(mb, (0, y, 0.3), (0.38, 0.03, 0.012), BRASS)
    cyl(mb, (0.24, 0.9, 0.3), (0.24, 0.9, 0.36), 0.05, BRASS, 10)
    for x in (-0.32, 0.32):
        for y in (0.15, 1.65):
            ellipsoid(mb, (x, y, 0.3), (0.02, 0.02, 0.02), c4(BRASS), 5, 3)
    return mb, None, None


def abyssal_bone_table():
    mb, gl, gw = MB(), MB(), MB()
    for sx in (-1, 1):
        for sz in (-1, 1):
            tube(mb, [(sx * 0.5, 0, sz * 0.3), (sx * 0.42, 0.35, sz * 0.25), (sx * 0.48, 0.68, sz * 0.3)], [0.05, 0.04, 0.05], 8, c4(BONE))
    box(mb, (0, 0.7, 0), (0.6, 0.02, 0.38), BONE_D)
    box(gl, (0, 0.735, 0), (0.62, 0.015, 0.4), (0.7, 0.85, 0.85))
    return mb, gl, gw


def phosphor_mat_chandelier():
    mb, gl, gw = MB(), MB(), MB()
    cyl(mb, (0, 0, 0), (0, -0.45, 0), 0.015, IRON, 6)
    ring(mb, (0, -0.5, 0), (0, 1, 0), 0.38, 0.03, IRON, 24)
    for k in range(6):
        a = k / 6 * math.pi * 2
        p = (math.cos(a) * 0.38, -0.5, math.sin(a) * 0.38)
        tube(mb, [(0, -0.45, 0), p], [0.012, 0.012], 5, c4(IRON))
        tube(gw, [p, (p[0], -0.72, p[2])], [0.06, 0.03], 8, c4((0.3, 0.55, 1.0)))
    ellipsoid(gw, (0, -0.6, 0), (0.1, 0.12, 0.1), c4((0.35, 0.6, 1.0)), 10, 6)
    return mb, gl, gw


def lantern_vine_desk_lamp():
    mb, gl, gw = MB(), MB(), MB()
    disc(mb, (0, 0.01, 0), (0, 1, 0), 0.13, BRASS, 16)
    cyl(mb, (0, 0.01, 0), (0, 0.04, 0), 0.13, BRASS_D, 16)
    tube(mb, [(0, 0.04, 0), (0, 0.3, 0), (0.05, 0.42, 0.04)], [0.015, 0.015, 0.015], 6, c4(BRASS))
    tube(mb, [(0, 0.06, 0), (0.04, 0.2, 0.03), (-0.02, 0.33, -0.02), (0.05, 0.42, 0.04)], [0.008, 0.008, 0.007, 0.006], 5, c4(KELP))
    ellipsoid(gw, (0.06, 0.44, 0.05), (0.06, 0.07, 0.06), c4((1.0, 0.82, 0.35)), 10, 6)
    return mb, gl, gw


def shallows_algae_terrarium():
    mb, gl, gw = MB(), MB(), MB()
    cyl(mb, (0, 0, 0), (0, 0.06, 0), 0.22, BRASS_D, 18)
    cyl(mb, (0, 0.06, 0), (0, 0.12, 0), 0.2, (0.85, 0.75, 0.5), 18)
    for k in range(10):
        a = k * 2.4
        x, z = math.cos(a) * 0.11 * (k % 3) / 2, math.sin(a) * 0.11 * (k % 3) / 2
        tube(mb, [(x, 0.12, z), (x * 1.2, 0.22 + 0.05 * (k % 2), z * 1.2)], [0.02, 0.006], 5, c4((0.25, 0.65, 0.3)))
    ellipsoid(gl, (0, 0.2, 0), (0.2, 0.22, 0.2), c4((0.75, 0.9, 0.9)), 16, 10)
    return mb, gl, gw


def beetle_chitin_sconce():
    mb, gl, gw = MB(), MB(), MB()
    box(mb, (0, 1.5, 0.02), (0.1, 0.16, 0.02), BRASS_D)
    for s in (-1, 1):
        membrane(mb, [(0, 1.42, 0.04), (0, 1.62, 0.04)], [(s * 0.16, 1.4, 0.16), (s * 0.13, 1.66, 0.14)], 3, (0.18, 0.28, 0.16, 0.0), 0.0, True)
    ellipsoid(gw, (0, 1.52, 0.1), (0.05, 0.08, 0.05), c4((0.4, 1.0, 0.35)), 8, 6)
    return mb, gl, gw


def reef_snapper_jaw_mount():
    mb = MB()
    box(mb, (0, 1.4, 0.025), (0.3, 0.22, 0.025), WOOD)
    ring(mb, (0, 1.4, 0.1), (0, 0, 1), 0.15, 0.03, BONE, 18)
    for k in range(14):
        a = k / 14 * math.pi * 2
        base = (math.cos(a) * 0.15, 1.4 + math.sin(a) * 0.15, 0.1)
        cone(mb, base, (math.cos(a) * 0.08, 1.4 + math.sin(a) * 0.08, 0.12), 0.018, c4((0.95, 0.93, 0.85)), 5)
    return mb, None, None


def suspended_stalker_hound_core():
    mb, gl, gw = MB(), MB(), MB()
    cyl(mb, (0, 0, 0), (0, 0.08, 0), 0.16, BRASS_D, 16)
    cyl(mb, (0, 0.52, 0), (0, 0.58, 0), 0.16, BRASS_D, 16)
    for k in range(3):
        a = k / 3 * math.pi * 2
        cyl(mb, (math.cos(a) * 0.15, 0.08, math.sin(a) * 0.15), (math.cos(a) * 0.15, 0.52, math.sin(a) * 0.15), 0.012, BRASS, 6)
    cyl(gl, (0, 0.08, 0), (0, 0.52, 0), 0.14, (0.6, 0.8, 0.6), 16)
    ellipsoid(gw, (0, 0.3, 0), (0.07, 0.09, 0.07), c4((1.0, 0.35, 0.25)), 10, 6)
    return mb, gl, gw


def dreadnought_steam_gauge():
    mb, gl, gw = MB(), MB(), MB()
    cyl(mb, (0, 1.5, 0), (0, 1.5, 0.08), 0.2, BRASS, 24)
    disc(mb, (0, 1.5, 0.081), (0, 0, 1), 0.17, PAPER, 24)
    for k in range(9):
        a = math.pi * (1.25 - k * 0.1875)
        tube(mb, [(math.cos(a) * 0.13, 1.5 + math.sin(a) * 0.13, 0.083), (math.cos(a) * 0.16, 1.5 + math.sin(a) * 0.16, 0.083)], [0.006, 0.006], 4, c4(BLACK))
    cyl(mb, (0, 1.3, 0.04), (0, 1.15, 0.04), 0.025, BRASS_D, 8)
    ring(gl, (0, 1.5, 0.09), (0, 0, 1), 0.17, 0.01, (0.7, 0.8, 0.8), 24)
    disc(gl, (0, 1.5, 0.095), (0, 0, 1), 0.17, (0.7, 0.85, 0.85), 24)
    return mb, gl, gw


def dreadnought_steam_gauge_needle():
    mb = MB()
    tube(mb, [(0, -0.02, 0), (0, 0.14, 0)], [0.008, 0.003], 4, c4(RED))
    ellipsoid(mb, (0, 0, 0), (0.015, 0.015, 0.01), c4(BRASS_D), 6, 4)
    return mb


def echo_ray_acoustic_chimes():
    mb = MB()
    cyl(mb, (0, 0, 0), (0, -0.15, 0), 0.01, IRON, 5)
    ring(mb, (0, -0.16, 0), (0, 1, 0), 0.14, 0.015, KELP_D, 16)
    for k in range(7):
        a = k / 7 * math.pi * 2
        x, z = math.cos(a) * 0.14, math.sin(a) * 0.14
        tube(mb, [(x, -0.16, z), (x, -0.32 - 0.05 * (k % 3), z)], [0.004, 0.004], 4, c4(KELP_D))
        tube(mb, [(x, -0.32 - 0.05 * (k % 3), z), (x * 1.1, -0.5 - 0.06 * (k % 3), z * 1.1)], [0.03, 0.012], 6, c4((0.55, 0.6, 0.66)))
    return mb, None, None


def dreadnought_torpedo_tube_locker():
    mb = MB()
    tube(mb, [(-1.0, 0.45, 0), (1.0, 0.45, 0)], [0.38, 0.38], 20, c4(IRON))
    disc(mb, (1.0, 0.45, 0), (1, 0, 0), 0.38, RUST, 20)
    ring(mb, (1.02, 0.45, 0), (1, 0, 0), 0.38, 0.04, BRASS_D, 24)
    cyl(mb, (1.03, 0.45, 0), (1.12, 0.45, 0), 0.06, BRASS, 10)
    for x in (-0.6, 0.6):
        box(mb, (x, 0.04, 0), (0.08, 0.04, 0.3), IRON)
        ring(mb, (x, 0.45, 0), (1, 0, 0), 0.39, 0.025, RUST, 24)
    return mb, None, None


def ancient_masonry_pedestal():
    mb = MB()
    box(mb, (0, 0.08, 0), (0.3, 0.08, 0.3), STONE)
    box(mb, (0, 0.55, 0), (0.18, 0.4, 0.18), STONE)
    box(mb, (0, 1.0, 0), (0.26, 0.05, 0.26), STONE)
    for y in (0.3, 0.6, 0.85):
        box(mb, (0, y, 0.181), (0.15, 0.006, 0.002), (0.2, 0.4, 0.45))
    ellipsoid(mb, (0, 1.12, 0), (0.08, 0.08, 0.08), c4((0.6, 0.62, 0.66)), 8, 6)
    return mb, None, None


def galleon_map_board():
    mb = MB()
    box(mb, (0, 1.45, 0.03), (0.6, 0.42, 0.03), WOOD_D)
    box(mb, (0, 1.45, 0.065), (0.55, 0.37, 0.006), PAPER)
    for k in range(7):
        x, y = -0.4 + (k * 0.37) % 0.8, 1.2 + (k * 0.23) % 0.5
        cyl(mb, (x, y, 0.07), (x, y, 0.1), 0.012, RED if k % 3 == 0 else BRASS, 6)
    for k in range(5):
        tube(mb, [(-0.45 + k * 0.2, 1.15, 0.072), (-0.3 + k * 0.17, 1.4 + 0.1 * math.sin(k), 0.072), (-0.2 + k * 0.15, 1.7, 0.072)], [0.004, 0.004, 0.004], 4, c4((0.35, 0.3, 0.25)))
    return mb, None, None


def captains_armillary_sphere():
    mb, gl, gw = MB(), MB(), MB()
    cyl(mb, (0, 0, 0), (0, 0.05, 0), 0.22, WOOD_D, 16)
    tube(mb, [(0, 0.05, 0), (0, 0.4, 0)], [0.04, 0.025], 8, c4(BRASS))
    for k in range(3):
        n = [(1, 0, 0), (0, 0, 1), (0.7, 0.7, 0)][k]
        ring(mb, (0, 0.68, 0), n, 0.26, 0.012, BRASS, 28)
    ring(mb, (0, 0.68, 0), (0, 1, 0), 0.28, 0.014, BRASS_D, 28)
    ellipsoid(gw, (0, 0.68, 0), (0.07, 0.07, 0.07), c4((0.5, 0.85, 1.0)), 10, 6)
    return mb, gl, gw


def jelly_bioluminescence_tube():
    mb, gl, gw = MB(), MB(), MB()
    cyl(mb, (0, 0, 0), (0, 0.12, 0), 0.28, BRASS_D, 20)
    cyl(mb, (0, 1.95, 0), (0, 2.1, 0), 0.28, BRASS_D, 20)
    cyl(gl, (0, 0.12, 0), (0, 1.95, 0), 0.25, (0.6, 0.8, 0.85), 20)
    for k in range(5):
        y = 0.4 + k * 0.33
        x, z = math.cos(k * 2.1) * 0.1, math.sin(k * 2.1) * 0.1
        ellipsoid(gw, (x, y, z), (0.06, 0.04, 0.06), c4((0.55, 0.4, 1.0)), 10, 5)
        for t in range(4):
            a = t / 4 * math.pi * 2
            tube(gw, [(x + math.cos(a) * 0.04, y - 0.02, z + math.sin(a) * 0.04), (x + math.cos(a) * 0.05, y - 0.15, z + math.sin(a) * 0.05)], [0.006, 0.002], 4, c4((0.5, 0.45, 1.0)))
    return mb, gl, gw


def iron_kelp_bonsai():
    mb = MB()
    box(mb, (0, 0.08, 0), (0.18, 0.08, 0.12), WOOD)
    box(mb, (0, 0.165, 0), (0.16, 0.006, 0.1), (0.3, 0.25, 0.18))
    tube(mb, [(0, 0.16, 0), (0.03, 0.3, 0.01), (-0.04, 0.45, 0), (0.02, 0.58, 0.02)], [0.025, 0.02, 0.015, 0.01], 6, c4(KELP_D))
    for k in range(6):
        y = 0.3 + k * 0.05
        s = 1 if k % 2 else -1
        membrane(mb, [(0, y, 0), (0, y + 0.04, 0)], [(s * 0.15, y + 0.06, 0.02), (s * 0.13, y + 0.1, 0.02)], 2, (0.38, 0.45, 0.16, 0.0), 0.0, False)
    return mb, None, None


def thermal_coral_planter():
    mb, gl, gw = MB(), MB(), MB()
    box(mb, (0, 1.0, 0.12), (0.5, 0.1, 0.12), IRON, top=RUST)
    for k in range(7):
        x = -0.4 + k * 0.13
        tube(gw, [(x, 1.1, 0.12), (x + 0.03, 1.22 + 0.05 * (k % 2), 0.14), (x - 0.02, 1.32, 0.12)], [0.025, 0.018, 0.008], 6, c4((1.0, 0.3, 0.2)))
    return mb, gl, gw


def echo_ray_specimen_tank():
    mb, gl, gw = MB(), MB(), MB()
    box(mb, (0, 0.04, 0), (0.55, 0.04, 0.3), BRASS_D)
    box(mb, (0, 0.82, 0), (0.55, 0.03, 0.3), BRASS_D)
    for sx in (-1, 1):
        for sz in (-1, 1):
            cyl(mb, (sx * 0.53, 0.08, sz * 0.28), (sx * 0.53, 0.8, sz * 0.28), 0.015, BRASS, 6)
    membrane(mb, [(-0.15, 0.4, 0), (0, 0.4, 0.05), (0.15, 0.4, 0)], [(-0.18, 0.4, -0.15), (0, 0.42, -0.2), (0.18, 0.4, -0.15)], 2, (0.35, 0.38, 0.45, 0.0), 0.0, False)
    box(gl, (0, 0.43, 0), (0.53, 0.36, 0.28), (0.6, 0.85, 0.85))
    return mb, gl, gw


def restored_galleon_harpsichord():
    mb = MB()
    box(mb, (0, 0.8, 0), (0.45, 0.12, 0.9), WOOD, top=WOOD_L)
    legs(mb, 0.38, 0.8, 0.7, 0.035, WOOD_D)
    box(mb, (0, 0.82, 0.92), (0.44, 0.03, 0.08), WOOD_D)
    for k in range(20):
        x = -0.4 + k * 0.042
        box(mb, (x, 0.86, 0.95), (0.018, 0.012, 0.05), BONE if k % 3 else BLACK)
    box(mb, (0, 1.2, -0.85), (0.43, 0.3, 0.02), WOOD_D)
    return mb, None, None


def grazer_hide_hammock():
    mb = MB()
    for z in (-1.2, 1.2):
        cyl(mb, (0, 0, z), (0, 1.5, z), 0.05, WOOD_D, 8)
    rings = []
    for i in range(11):
        t = i / 10
        z = -1.0 + 2.0 * t
        sag = 0.35 * math.sin(t * math.pi)
        rings.append(((0, 1.05 - sag, z), (0, 0, 1), 0.38, 0.06, lambda a: 1.0 if math.cos(a) < 0 else 0.25))
    loft(mb, rings, 12, lambda t, a, p: c4(mix((0.62, 0.48, 0.34), (0.5, 0.36, 0.24), 0.5 + 0.5 * math.sin(p[0] * 30))))
    for z, zz in ((-1.2, -1.0), (1.2, 1.0)):
        for x in (-0.35, 0.0, 0.35):
            tube(mb, [(0, 1.4, z), (x, 1.05, zz)], [0.008, 0.008], 4, c4(KELP_D))
    return mb, None, None


def abyssal_brew_keg():
    mb = MB()
    rings = []
    for i in range(9):
        t = i / 8
        r = 0.3 + 0.06 * math.sin(t * math.pi)
        rings.append(((-0.42 + 0.84 * t, 0.5, 0), (1, 0, 0), r, r, None))
    loft(mb, rings, 18, lambda t, a, p: c4(mix(WOOD, WOOD_D, 0.5 + 0.5 * math.sin(a * 12))))
    for x in (-0.3, 0.3):
        ring(mb, (x, 0.5, 0), (1, 0, 0), 0.35, 0.02, IRON, 22)
    cyl(mb, (0.42, 0.42, 0), (0.55, 0.42, 0), 0.03, BRASS, 8)
    box(mb, (0, 0.1, 0.22), (0.4, 0.1, 0.05), WOOD_D)
    box(mb, (0, 0.1, -0.22), (0.4, 0.1, 0.05), WOOD_D)
    return mb, None, None


def tangle_serpent_shed_scale():
    mb, gl, gw = MB(), MB(), MB()
    box(mb, (0, 1.5, 0.03), (0.4, 0.4, 0.03), IRON)
    membrane(mb, [(-0.25, 1.25, 0.08), (0, 1.2, 0.09), (0.25, 1.25, 0.08)], [(-0.2, 1.75, 0.08), (0, 1.8, 0.1), (0.2, 1.75, 0.08)], 4, (0.45, 0.3, 0.55, 0.0), 0.0, True)
    box(gl, (0, 1.5, 0.12), (0.36, 0.36, 0.02), (0.7, 0.8, 0.8))
    return mb, gl, gw


def city_resonance_crystal():
    mb, gl, gw = MB(), MB(), MB()
    cyl(mb, (0, 0, 0), (0, 0.06, 0), 0.18, BRASS_D, 6)
    for k in range(3):
        a = k / 3 * math.pi * 2
        tube(mb, [(math.cos(a) * 0.15, 0.06, math.sin(a) * 0.15), (math.cos(a) * 0.08, 0.3, math.sin(a) * 0.08)], [0.015, 0.012], 5, c4(BRASS))
    cone(gw, (0, 0.18, 0), (0, 0.55, 0), 0.08, c4((0.55, 0.8, 1.0)), 6)
    cone(gw, (0, 0.18, 0), (0, 0.06, 0), 0.08, c4((0.55, 0.8, 1.0)), 6)
    return mb, gl, gw


def scavenger_hydraulic_pincer():
    mb = MB()
    for x in (-0.25, 0.25):
        tube(mb, [(x, 2.2, 0.05), (x * 0.5, 1.7, 0.25)], [0.006, 0.006], 4, c4(IRON))
    tube(mb, [(0, 1.7, 0.25), (0, 1.5, 0.3)], [0.08, 0.07], 10, c4(RUST))
    for s in (-1, 1):
        tube(mb, [(0, 1.5, 0.3), (s * 0.12, 1.3, 0.32), (s * 0.06, 1.1, 0.34)], [0.06, 0.045, 0.01], 8, c4(mix(RUST, RED, 0.4)))
    cyl(mb, (-0.06, 1.62, 0.3), (0.06, 1.62, 0.3), 0.05, BRASS, 8)
    return mb, None, None


def dreadnought_helm_wheel():
    mb = MB()
    cyl(mb, (0, 1.4, 0), (0, 1.4, 0.12), 0.05, IRON, 10)
    ring(mb, (0, 1.4, 0.12), (0, 0, 1), 0.38, 0.035, WOOD, 28)
    cyl(mb, (0, 1.4, 0.1), (0, 1.4, 0.15), 0.08, BRASS, 12)
    for k in range(8):
        a = k / 8 * math.pi * 2
        tube(mb, [(0, 1.4, 0.12), (math.cos(a) * 0.5, 1.4 + math.sin(a) * 0.5, 0.12)], [0.02, 0.022], 6, c4(WOOD_D))
        ellipsoid(mb, (math.cos(a) * 0.52, 1.4 + math.sin(a) * 0.52, 0.12), (0.03, 0.03, 0.03), c4(WOOD_D), 6, 4)
    return mb, None, None


def drop_satchel():
    mb, gl, gw = MB(), MB(), MB()
    rings = []
    for i in range(7):
        t = i / 6
        rings.append(((0, 0.04 + t * 0.32, 0), (0, 1, 0), 0.24 * (1 - 0.25 * t * t), 0.16 * (1 - 0.25 * t * t), None))
    loft(mb, rings, 14, lambda t, a, p: c4(mix((0.55, 0.5, 0.38), (0.45, 0.4, 0.3), 0.5 + 0.5 * math.sin(a * 6))))
    box(mb, (0, 0.33, 0.1), (0.18, 0.1, 0.04), LEATHER)
    cyl(mb, (0, 0.3, 0.15), (0, 0.3, 0.17), 0.03, BRASS, 8)
    tube(mb, [(-0.18, 0.35, 0), (0, 0.6, 0), (0.18, 0.35, 0)], [0.02, 0.02, 0.02], 5, c4(LEATHER))
    ellipsoid(gw, (0, 0.62, 0), (0.04, 0.04, 0.04), c4((1.0, 0.75, 0.2)), 8, 5)
    return mb, gl, gw


ITEMS = [
    ("leviathan_bone_display", leviathan_bone_display), ("bioluminescent_wall_planter", bioluminescent_wall_planter),
    ("woven_kelp_rug", woven_kelp_rug), ("galleon_brass_porthole_frame", galleon_brass_porthole_frame),
    ("captains_log_desk", captains_log_desk), ("galleon_captains_bed", galleon_captains_bed),
    ("grazer_blubber_lounge_chair", grazer_blubber_lounge_chair), ("dreadnought_brass_locker", dreadnought_brass_locker),
    ("abyssal_bone_table", abyssal_bone_table), ("phosphor_mat_chandelier", phosphor_mat_chandelier),
    ("lantern_vine_desk_lamp", lantern_vine_desk_lamp), ("shallows_algae_terrarium", shallows_algae_terrarium),
    ("beetle_chitin_sconce", beetle_chitin_sconce), ("reef_snapper_jaw_mount", reef_snapper_jaw_mount),
    ("suspended_stalker_hound_core", suspended_stalker_hound_core), ("dreadnought_steam_gauge", dreadnought_steam_gauge),
    ("echo_ray_acoustic_chimes", echo_ray_acoustic_chimes), ("dreadnought_torpedo_tube_locker", dreadnought_torpedo_tube_locker),
    ("ancient_masonry_pedestal", ancient_masonry_pedestal), ("galleon_map_board", galleon_map_board),
    ("captains_armillary_sphere", captains_armillary_sphere), ("jelly_bioluminescence_tube", jelly_bioluminescence_tube),
    ("iron_kelp_bonsai", iron_kelp_bonsai), ("thermal_coral_planter", thermal_coral_planter),
    ("echo_ray_specimen_tank", echo_ray_specimen_tank), ("restored_galleon_harpsichord", restored_galleon_harpsichord),
    ("grazer_hide_hammock", grazer_hide_hammock), ("abyssal_brew_keg", abyssal_brew_keg),
    ("tangle_serpent_shed_scale", tangle_serpent_shed_scale), ("city_resonance_crystal", city_resonance_crystal),
    ("scavenger_hydraulic_pincer", scavenger_hydraulic_pincer), ("dreadnought_helm_wheel", dreadnought_helm_wheel),
    ("drop_satchel", drop_satchel),
]


def main():
    a = K.args()
    out = a[a.index("--out") + 1] if "--out" in a else os.path.join(K.ROOT, "TheDeep", "Assets", "Deep", "Resources", "Decor")
    os.makedirs(out, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    objs = []
    for name, fn in ITEMS:
        mb, gl, gw = fn()
        objs.append(K.to_object(name, mb))
        if gl is not None and gl.v:
            objs.append(K.to_object(name + "_glass", gl))
        if gw is not None and gw.v:
            objs.append(K.to_object(name + "_glow", gw))
    objs.append(K.to_object("dreadnought_steam_gauge_needle", dreadnought_steam_gauge_needle()))
    for o in objs:
        o.select_set(True)
    path = os.path.join(out, "decor.glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=True, export_yup=True,
                              export_texcoords=True, export_normals=True, export_tangents=False, export_materials='NONE',
                              export_vertex_color='ACTIVE', export_all_vertex_colors=False)
    print(f"artgen: wrote {path} ({os.path.getsize(path) // 1024} KB, {len(objs)} objects)")


if __name__ == "__main__":
    main()
