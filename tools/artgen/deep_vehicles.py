# The Deep's vehicles (stage 4): the Kite-Sub, the design doc's agile one-person mini-sub for tight places, built in
# the Nautilus's style - a riveted iron teardrop with a brass-ringed glass bubble for the pilot, twin ducted thrusters,
# a tail with fins and a lamp - written as kite_sub.glb with two objects: "kite_sub" (vertex-coloured, opaque) and
# "kite_sub_glass" (the canopy, drawn with Deep/Glass). +z is forward, +y up, metres (about 3.6 m long).
#     blender -b --factory-startup -P tools/artgen/deep_vehicles.py -- --out TheDeep/Assets/Deep/Resources/Vehicles
import bpy
import math
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deep_creatures as K
from deep_creatures import MB, loft, tube, ellipsoid, cone, membrane, mix, clamp, noise3

IRON = (0.17, 0.19, 0.19)
IRON_D = (0.09, 0.1, 0.1)
BRASS = (0.62, 0.45, 0.19)
RUST = (0.38, 0.2, 0.1)
LAMP = (1.0, 0.92, 0.7)


def c4(c, a=0.0):
    return (c[0], c[1], c[2], a)


def hull_col(t, a, p):
    up = math.cos(a)
    c = mix(IRON_D, IRON, clamp(0.6 + up * 0.4))
    # plate seams and rivets every 40 cm, a little rust weeping from them
    if abs(math.sin(p[2] * 7.85)) < 0.06 or abs(math.sin(a * 4)) < 0.04:
        c = mix(c, (0.05, 0.05, 0.05), 0.6)
    if noise3((p[0] * 6, p[1] * 6, p[2] * 6), 2) > 0.72 and up < 0.3:
        c = mix(c, RUST, 0.5)
    if 0.05 < t < 0.09:
        c = BRASS
    return c4(c)


def kite_sub():
    mb = MB()
    L = 3.6
    def prof(t):   # a teardrop: full at the front where the pilot sits, tapering to the tail
        return max(0.08, math.sin(math.pi * 0.5 * (0.06 + 0.94 * t)) ** 0.6)
    rings = K.straight_rings(-L / 2, L / 2 - 0.55, 30, prof, 0.62, 0.58)
    loft(mb, rings, 20, hull_col, cap1=False)
    # the canopy's brass collar
    z0 = L / 2 - 0.55
    pts = [(0.66 * math.cos(a), 0.62 * math.sin(a), z0) for a in [i * 2 * math.pi / 32 for i in range(33)]]
    tube(mb, pts, [0.05] * len(pts), 6, c4(BRASS))
    # inside: a seat, a brass panel and two levers (seen through the glass)
    ellipsoid(mb, (0, -0.25, z0 - 0.1), (0.3, 0.12, 0.3), c4((0.25, 0.12, 0.06)), 10, 5)
    ellipsoid(mb, (0, 0.05, z0 - 0.35), (0.32, 0.38, 0.1), c4((0.25, 0.12, 0.06)), 10, 5)
    ellipsoid(mb, (0, -0.1, z0 + 0.3), (0.35, 0.08, 0.12), c4(BRASS), 10, 4)
    for s in (-1, 1):
        tube(mb, [(s * 0.2, -0.2, z0 + 0.15), (s * 0.22, 0.05, z0 + 0.22)], [0.02, 0.015], 5, c4((0.05, 0.05, 0.05)))
    # twin ducted thrusters on short pylons, with props
    for s in (-1, 1):
        x = s * 0.92
        tube(mb, [(s * 0.5, -0.05, -0.4), (x, -0.1, -0.5)], [0.07, 0.06], 6, c4(IRON))
        duct = [((x, -0.1, -0.95 + 0.12 * k), (0, 0, 1), 0.26, 0.26, None) for k in range(5)]
        loft(mb, duct, 16, lambda t, a, p: c4(BRASS if t < 0.15 or t > 0.85 else IRON_D), cap0=False, cap1=False)
        ellipsoid(mb, (x, -0.1, -0.6), (0.07, 0.07, 0.18), c4(IRON), 8, 5)
        for b in range(4):
            a = b * math.pi / 2
            membrane(mb, [(x, -0.1, -0.68), (x, -0.1, -0.64)], [(x + math.cos(a) * 0.22, -0.1 + math.sin(a) * 0.22, -0.7), (x + math.cos(a + 0.4) * 0.22, -0.1 + math.sin(a + 0.4) * 0.22, -0.66)], 1, c4(BRASS), 0.0, False)
    # the tail: a vertical fin, two diving planes
    membrane(mb, [(0, 0.15, -1.45), (0, 0.18, -1.2)], [(0, 0.62, -1.75), (0, 0.6, -1.55)], 2, c4(IRON), 0.0, False)
    membrane(mb, [(0, -0.15, -1.45), (0, -0.18, -1.2)], [(0, -0.45, -1.7), (0, -0.42, -1.55)], 2, c4(IRON), 0.0, False)
    for s in (-1, 1):
        membrane(mb, [(s * 0.18, 0, -1.45), (s * 0.2, 0, -1.2)], [(s * 0.62, 0, -1.7), (s * 0.6, 0, -1.55)], 2, c4(IRON), 0.0, False)
    # the lamp under the bow, and skids
    tube(mb, [(0, -0.55, 0.7), (0, -0.6, 0.95)], [0.12, 0.13], 10, c4(BRASS))
    ellipsoid(mb, (0, -0.6, 0.98), (0.11, 0.11, 0.03), c4(LAMP, 0.0), 10, 4)
    for s in (-1, 1):
        tube(mb, [(s * 0.35, -0.55, -0.9), (s * 0.4, -0.72, -0.6), (s * 0.4, -0.72, 0.6), (s * 0.35, -0.6, 0.9)], [0.035] * 4, 6, c4(IRON_D))
    return mb


def canopy():
    mb = MB()
    z0 = 3.6 / 2 - 0.55
    rings = []
    for i in range(13):
        t = i / 12
        a = t * math.pi * 0.5
        rings.append(((0, 0.0, z0 + math.sin(a) * 0.55), (0, 0, 1), 0.64 * math.cos(a) + 0.001, 0.6 * math.cos(a) + 0.001, None))
    loft(mb, rings, 24, lambda t, a, p: (0.6, 0.7, 0.7, 1.0), cap0=False)
    return mb


def main():
    a = K.args()
    out = a[a.index("--out") + 1] if "--out" in a else os.path.join(K.ROOT, "TheDeep", "Assets", "Deep", "Resources", "Vehicles")
    os.makedirs(out, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    objs = [K.to_object("kite_sub", kite_sub()), K.to_object("kite_sub_glass", canopy())]
    for o in objs:
        o.select_set(True)
    path = os.path.join(out, "kite_sub.glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=True, export_yup=True,
                              export_texcoords=True, export_normals=True, export_tangents=False, export_materials='NONE',
                              export_vertex_color='ACTIVE', export_all_vertex_colors=False)
    print(f"artgen: wrote {path} ({os.path.getsize(path) // 1024} KB)")


if __name__ == "__main__":
    main()
