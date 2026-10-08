# The Thermal Vents' stonework (stage 10; the design doc: "volcanic ridges, black smoker chimneys, and basalt plateaus
# spewing superheated mineral plumes"): black smoker chimneys of three heights (knobbly mineral crust, sulphide streaks,
# a scorched orifice), a squat vent mound, a cluster of hexagonal basalt columns, and a geyser's cone. One glb,
# Vents/vents.glb, an object per piece; +y up, metres, standing on y = 0. Vertex colour carries the stone; alpha marks
# what glows (the orifice's heat), which the game draws hotter.
#     blender -b --factory-startup -P tools/artgen/deep_vents.py -- --out TheDeep/Assets/Deep/Resources/Vents
import bpy
import math
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deep_creatures as K
from deep_creatures import MB, loft, tube, ellipsoid, cone, mix, clamp, H, noise3

SOOT = (0.06, 0.055, 0.05)
SULPHIDE = (0.32, 0.24, 0.12)
RUST = (0.38, 0.17, 0.08)
SULPHUR = (0.7, 0.62, 0.2)
BASALT = (0.1, 0.1, 0.11)
HOT = (1.0, 0.45, 0.1)


def c4(c, a=0.0):
    return (c[0], c[1], c[2], a)


def crust(p, top):
    """The chimney's mineral crust: soot black, banded with sulphide brown and rust, sulphur yellow near the mouth."""
    n = noise3((p[0] * 3, p[1] * 1.5, p[2] * 3), 1)
    band = 0.5 + 0.5 * math.sin(p[1] * 2.3 + n * 4)
    c = mix(SOOT, SULPHIDE, band * 0.6)
    c = mix(c, RUST, clamp((n - 0.6) * 3))
    if p[1] > top - 1.2:
        c = mix(c, SULPHUR, clamp((p[1] - (top - 1.2)) / 1.2) * 0.6)
    return c4(c)


def chimney(height, radius, seed):
    mb = MB()
    rings = []
    N = max(8, int(height * 1.6))
    for i in range(N + 1):
        t = i / N
        y = height * t
        r = radius * (1.25 - 0.5 * t) * (1 + 0.25 * (noise3((t * 7 + seed, seed, 0), 2) - 0.5))
        x = 0.25 * radius * math.sin(t * 3.1 + seed)
        z = 0.25 * radius * math.cos(t * 2.3 + seed)
        rings.append(((x, y, z), (0, 1, 0), r, r, lambda a, t=t: 1.0 + 0.18 * math.sin(a * 5 + t * 13)))
    loft(mb, rings, 14, lambda t, a, p: crust(p, height), cap0=False, cap1=False)
    # the orifice: a scorched lip and the glowing throat
    top = rings[-1][0]
    ellipsoid(mb, (top[0], height - 0.05, top[2]), (radius * 0.55, 0.15, radius * 0.55), c4(HOT, 1.0), 12, 4)
    # knobs and side spires: other mouths, dead
    for k in range(int(height / 3) + 2):
        t = 0.15 + 0.7 * H(str(seed) + str(k), 1)
        a = H(str(seed) + str(k), 2) * 6.28
        base = (math.cos(a) * radius * (1.2 - 0.5 * t), height * t, math.sin(a) * radius * (1.2 - 0.5 * t))
        tip = (base[0] * 1.5, base[1] + 0.6 + 1.4 * H(str(seed) + str(k), 3), base[2] * 1.5)
        cone(mb, base, tip, radius * 0.35, crust(base, height), 7)
    # the mound of fallen crust round its foot
    for k in range(9):
        a = k / 9 * 6.28 + H(str(seed), 7)
        r = radius * (1.6 + 0.6 * H(str(seed) + "m" + str(k), 1))
        ellipsoid(mb, (math.cos(a) * r, 0.2, math.sin(a) * r), (radius * 0.7, radius * 0.4, radius * 0.7), c4(mix(SOOT, SULPHIDE, 0.3)), 8, 5)
    return mb


def mound():
    mb = MB()
    for k in range(14):
        a, r = H("mound" + str(k), 1) * 6.28, H("mound" + str(k), 2) * 3.0
        s = 1.2 + 1.4 * H("mound" + str(k), 3)
        ellipsoid(mb, (math.cos(a) * r, s * 0.35, math.sin(a) * r), (s, s * 0.6, s), crust((0, s * 0.3, 0), 3), 10, 6)
    ellipsoid(mb, (0, 1.4, 0), (0.5, 0.2, 0.5), c4(HOT, 1.0), 10, 4)
    return mb


def basalt_columns():
    """Hexagonal basalt columns, cooled lava cracked into prisms, at stepped heights."""
    mb = MB()
    for i in range(-3, 4):
        for j in range(-3, 4):
            x = i * 1.0 + (j % 2) * 0.5
            z = j * 0.87
            if x * x + z * z > 10:
                continue
            h = 1.0 + 4.0 * H("bc%d%d" % (i, j), 1) * (1 - (x * x + z * z) / 12)
            rings = [((x, 0, z), (0, 1, 0), 0.5, 0.5, None), ((x, h, z), (0, 1, 0), 0.5, 0.5, None)]
            loft(mb, rings, 6, lambda t, a, p: c4(mix(BASALT, (0.16, 0.16, 0.17), 0.5 + 0.5 * math.sin(p[1] * 5))), cap0=False, cap1=True)
    return mb


def geyser_cone():
    mb = MB()
    rings = []
    for i in range(7):
        t = i / 6
        r = 3.2 * (1 - t) + 0.7
        rings.append(((0, 1.6 * t, 0), (0, 1, 0), r, r, lambda a: 1.0 + 0.1 * math.sin(a * 7)))
    loft(mb, rings, 16, lambda t, a, p: c4(mix(mix(SULPHIDE, SULPHUR, 0.35 * t), (0.85, 0.82, 0.72), clamp(t * 1.5 - 0.6) * 0.6)), cap0=False, cap1=False)
    ellipsoid(mb, (0, 1.45, 0), (0.75, 0.2, 0.75), c4((0.6, 0.8, 0.85), 1.0), 12, 4)   # the boiling pool in its throat
    return mb


def glass_shard():
    """Volcanic glass: a black, sharp-edged shard (turbulence throws subs against it)."""
    mb = MB()
    for k in range(5):
        a = k / 5 * 6.28
        cone(mb, (math.cos(a) * 0.4, 0, math.sin(a) * 0.4), (math.cos(a) * 0.8, 2.0 + 1.5 * H("gs" + str(k), 1), math.sin(a) * 0.8), 0.45, c4((0.03, 0.03, 0.04)), 4)
    return mb


PIECES = [("chimney_tall", lambda: chimney(22, 1.6, 1.3)), ("chimney_mid", lambda: chimney(12, 1.3, 2.7)), ("chimney_short", lambda: chimney(6, 1.1, 4.1)),
          ("vent_mound", mound), ("basalt_columns", basalt_columns), ("geyser_cone", geyser_cone), ("glass_shard", glass_shard)]


def main():
    a = K.args()
    out = a[a.index("--out") + 1] if "--out" in a else os.path.join(K.ROOT, "TheDeep", "Assets", "Deep", "Resources", "Vents")
    os.makedirs(out, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    objs = [K.to_object(n, f()) for n, f in PIECES]
    for o in objs:
        o.select_set(True)
    path = os.path.join(out, "vents.glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=True, export_yup=True,
                              export_texcoords=True, export_normals=True, export_tangents=False, export_materials='NONE',
                              export_vertex_color='ACTIVE', export_all_vertex_colors=False)
    print(f"artgen: wrote {path} ({os.path.getsize(path) // 1024} KB, {len(objs)} pieces)")


if __name__ == "__main__":
    main()
