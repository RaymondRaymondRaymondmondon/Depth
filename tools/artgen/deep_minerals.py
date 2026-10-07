# The Deep's mineral deposits and salvage (stage 4): the nodes the Starter Drill (and later drills) work, and the
# wreckage a diver strips, one object per kind in minerals.glb, base at the origin, +y up, metres:
#   titanium_ore     a dark rock with blue-grey metallic veins          (tier 2: the Starter Drill)
#   copper_ore       a lump streaked with copper and green patina       (tier 2)
#   quartz_node      a cluster of clear crystal points on a rock         (tier 2)
#   calcite_crust    a pale pink-white bio-calcite crust                 (tier 1)
#   silver_ore       grey rock with bright silver flecks                 (tier 2; found with sonar)
#   lithium_ore      pale lilac crystalline rock                         (tier 2; found with sonar)
#   galleon_wood     broken, weed-grown ship's timbers                   (salvage: the knife and hands)
#   galleon_brass    a heap of brass fittings: a ship's bell, rings      (salvage)
#   iron_scrap       rusted iron plate and girders                       (salvage: the Heated Blade)
#   military_titanium  a torn grey armour plate with rivets              (salvage: the Heated Blade)
#     blender -b --factory-startup -P tools/artgen/deep_minerals.py -- --out TheDeep/Assets/Deep/Resources/Minerals
import bpy
import math
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deep_creatures as K
from deep_creatures import MB, ellipsoid, cone, tube, loft, mix, noise3, H, clamp


def rock(mb, c, r, base, vein, seed, vein_amt=0.35, flat=0.7, lumps=7):
    """A lumpy outcrop of several rounded stones, veined."""
    for i in range(lumps):
        a = H(seed + str(i), 1) * math.pi * 2
        d = H(seed + str(i), 2) * r * 0.6
        s = r * (0.45 + 0.4 * H(seed + str(i), 3))
        p = (c[0] + math.cos(a) * d, c[1] + s * flat * 0.7, c[2] + math.sin(a) * d)
        def col(q, seed=seed, i=i):
            n = noise3((q[0] * 7, q[1] * 7, q[2] * 7), i)
            v = abs(math.sin(q[0] * 9 + q[1] * 13 + q[2] * 5 + n * 3))
            cc = mix(base, (base[0] * 0.6, base[1] * 0.6, base[2] * 0.6), n * 0.6)
            if v < vein_amt * 0.3:
                cc = vein
            return (cc[0], cc[1], cc[2], 0.0)
        ellipsoid(mb, p, (s, s * flat, s * (0.8 + 0.4 * H(seed + str(i), 4))), None, 10, 7, 0.0, col)


def crystals(mb, c, n, col, seed, size=0.35):
    for i in range(n):
        a, b = H(seed + str(i), 1) * math.pi * 2, 0.3 + H(seed + str(i), 2) * 0.9
        d = (math.cos(a) * math.sin(b), math.cos(b), math.sin(a) * math.sin(b))
        L = size * (0.5 + H(seed + str(i), 3))
        base = (c[0] + d[0] * 0.1, c[1], c[2] + d[2] * 0.1)
        cone(mb, base, (base[0] + d[0] * L, base[1] + d[1] * L, base[2] + d[2] * L), L * 0.16, col, 6)


def plank(mb, a, b, w, col):
    tube(mb, [a, ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2 + 0.02, (a[2] + b[2]) / 2), b], [w, w, w * 0.8], 4, col)


def build(kind):
    mb = MB()
    if kind == "titanium_ore":
        rock(mb, (0, 0, 0), 0.7, (0.22, 0.23, 0.25), (0.62, 0.68, 0.78), kind, 0.45)
    elif kind == "copper_ore":
        rock(mb, (0, 0, 0), 0.6, (0.3, 0.26, 0.22), (0.78, 0.45, 0.22), kind, 0.4)
        rock(mb, (0.15, 0.2, 0.1), 0.25, (0.25, 0.5, 0.42), (0.78, 0.45, 0.22), kind + "p", 0.3, 0.8, 4)
    elif kind == "quartz_node":
        rock(mb, (0, 0, 0), 0.45, (0.3, 0.29, 0.27), (0.4, 0.4, 0.4), kind, 0.1)
        crystals(mb, (0, 0.25, 0), 9, (0.85, 0.9, 0.92, 0.8), kind, 0.5)
    elif kind == "calcite_crust":
        rock(mb, (0, 0, 0), 0.6, (0.88, 0.8, 0.78), (0.95, 0.6, 0.65), kind, 0.3, 0.35, 9)
    elif kind == "silver_ore":
        rock(mb, (0, 0, 0), 0.55, (0.28, 0.28, 0.3), (0.92, 0.93, 0.95), kind, 0.25)
    elif kind == "lithium_ore":
        rock(mb, (0, 0, 0), 0.5, (0.55, 0.5, 0.62), (0.8, 0.72, 0.95), kind, 0.35)
        crystals(mb, (0, 0.2, 0), 6, (0.82, 0.72, 0.95, 0.5), kind, 0.3)
    elif kind == "galleon_wood":
        wood = (0.32, 0.22, 0.14, 0.0)
        for i in range(6):
            a = H(kind + str(i), 1) * math.pi
            L = 1.0 + H(kind + str(i), 2) * 1.2
            y = 0.06 + i * 0.07
            plank(mb, (-math.cos(a) * L / 2, y, -math.sin(a) * L / 2), (math.cos(a) * L / 2, y + 0.05, math.sin(a) * L / 2), 0.07, wood)
        for i in range(4):   # weed on it
            cone(mb, (H(kind + str(i), 5) - 0.5, 0.3, H(kind + str(i), 6) - 0.5), (H(kind + str(i), 5) - 0.5, 0.75, H(kind + str(i), 6) - 0.45), 0.03, (0.3, 0.42, 0.15, 0.0), 4)
    elif kind == "galleon_brass":
        brass = (0.55, 0.42, 0.18, 0.0)
        green = (0.3, 0.48, 0.38, 0.0)
        # a ship's bell on its side, rings and a porthole frame in the sand
        rings = [((0.0, 0.3 * math.sin(t * 1.4), 0.0 + t * 0.4), (0, 0, 1), 0.12 + 0.18 * t, 0.12 + 0.18 * t, None) for t in [k / 6 for k in range(7)]]
        loft(mb, rings, 14, lambda t, a, p: brass if K.noise3((p[0] * 9, p[1] * 9, p[2] * 9), 2) < 0.6 else green, cap1=False)
        for i in range(3):
            pts = [(0.5 + 0.18 * math.cos(a), 0.05, -0.3 + i * 0.35 + 0.18 * math.sin(a)) for a in [k * 2 * math.pi / 16 for k in range(17)]]
            tube(mb, pts, [0.03] * 17, 5, brass)
    elif kind == "iron_scrap":
        rust = (0.38, 0.2, 0.1, 0.0)
        for i in range(4):
            a = H(kind + str(i), 1) * math.pi
            ellipsoid(mb, (math.cos(a) * 0.3, 0.08 + i * 0.05, math.sin(a) * 0.3), (0.6, 0.03, 0.4), None, 8, 3, 0.0,
                      lambda q: (rust[0] * (0.7 + 0.5 * noise3((q[0] * 8, 0, q[2] * 8), 4)), rust[1] * 0.9, rust[2], 0.0))
        tube(mb, [(-0.8, 0.1, -0.2), (0.0, 0.4, 0.1), (0.9, 0.15, 0.3)], [0.06] * 3, 4, rust)
    elif kind == "military_titanium":
        grey = (0.42, 0.45, 0.47, 0.0)
        ellipsoid(mb, (0, 0.25, 0), (0.8, 0.05, 0.55), None, 10, 3, 0.0, lambda q: grey if (int(q[0] * 8) + int(q[2] * 8)) % 7 else (0.2, 0.2, 0.22, 0.0))
        for i in range(10):
            ellipsoid(mb, (-0.7 + i * 0.15, 0.31, 0.45), (0.025, 0.02, 0.025), (0.3, 0.3, 0.3, 0.0), 6, 3)
        tube(mb, [(-0.8, 0.0, -0.5), (-0.6, 0.25, -0.45)], [0.05, 0.04], 4, grey)
    return mb


KINDS = ["titanium_ore", "copper_ore", "quartz_node", "calcite_crust", "silver_ore", "lithium_ore",
         "galleon_wood", "galleon_brass", "iron_scrap", "military_titanium"]


def main():
    a = K.args()
    out = a[a.index("--out") + 1] if "--out" in a else os.path.join(K.ROOT, "TheDeep", "Assets", "Deep", "Resources", "Minerals")
    os.makedirs(out, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    objs = [K.to_object(k, build(k)) for k in KINDS]
    for o in objs:
        o.select_set(True)
    path = os.path.join(out, "minerals.glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=True, export_yup=True,
                              export_texcoords=True, export_normals=True, export_tangents=False, export_materials='NONE',
                              export_vertex_color='ACTIVE', export_all_vertex_colors=False)
    print(f"artgen: wrote {path} ({os.path.getsize(path) // 1024} KB)")


if __name__ == "__main__":
    main()
