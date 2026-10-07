# The Deep's wrecks (stage 4; the design doc's salvage sources for phases 1-2):
#   galleon      the Sunken Pirate Galleon (the Shallows): a 30 m ship broken in two, lying heeled over in the sand,
#                ribs bare at the break, masts snapped, cannons spilled; Salvaged Galleon Wood and Brass
#   dreadnought  the tangled dreadnought (the Kelp Labyrinth): a 70 m steam warship, ram bow, two turrets, funnels, a
#                bridge and masts, torn armour; Military-Grade Titanium and Rusted Iron Scrap
# Each is written as wreck_<name>.glb (vertex colours: weathered wood, verdigris, rust, weed and barnacle crusts), +z
# toward the bow, +y up, metres, resting at y = 0. A wreck_<name>.json beside it lists where its salvage lies (the
# game places gatherable salvage there) and a few boxes for its colliders.
#     blender -b --factory-startup -P tools/artgen/deep_wrecks.py -- --out TheDeep/Assets/Deep/Resources/Wrecks
import bpy
import json
import math
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deep_creatures as K
from deep_creatures import MB, loft, tube, ellipsoid, cone, membrane, mix, noise3, clamp, H


def c4(c, a=0.0): return (c[0], c[1], c[2], a)


def weather(c, p, weed=0.35, crust=(0.62, 0.6, 0.52)):
    """Age a colour: weed on the up-facing, barnacle crusts in patches."""
    n = noise3((p[0] * 2.3, p[1] * 2.3, p[2] * 2.3), 5)
    m = noise3((p[0] * 6.1, p[1] * 6.1, p[2] * 6.1), 9)
    if m > 0.74:
        c = mix(c, crust, 0.45)
    if n > 1 - weed * 0.6:
        c = mix(c, (0.22, 0.3, 0.14), 0.4)
    return c


def rot(p, ax, a, about=(0.0, 0.0, 0.0)):
    """Rotate a point about an axis through `about` (ax 'z' rolls the ship over, 'x' pitches)."""
    x, y, z = p[0] - about[0], p[1] - about[1], p[2] - about[2]
    ca, sa = math.cos(a), math.sin(a)
    if ax == 'z':
        x, y = x * ca - y * sa, x * sa + y * ca
    elif ax == 'x':
        y, z = y * ca - z * sa, y * sa + z * ca
    else:
        x, z = x * ca + z * sa, -x * sa + z * ca
    return (x + about[0], y + about[1], z + about[2])


def transform(mb, start, fn):
    for i in range(start, len(mb.v)):
        mb.v[i] = fn(mb.v[i])


# ------------------------------------------------------------------------------------------------ the galleon
def galleon():
    mb = MB()
    salvage, boxes = [], []
    WOOD = (0.3, 0.21, 0.13)
    WOOD_D = (0.17, 0.12, 0.08)
    L, B2, Dp = 30.0, 4.2, 5.0          # length, half beam, depth of hold

    def hull_section(z0, z1, n, broken_end):
        """A run of the open wooden hull: a U of planking (a flattish floor rising to near-upright sides) from z0 to z1,
        drawn both sides (the hold shows where it's broken)."""
        M = 22
        start = len(mb.v)
        for i in range(n + 1):
            z = z0 + (z1 - z0) * i / n
            u = (z + L / 2) / L                          # 0 stern .. 1 bow
            beam = B2 * (0.35 + 0.65 * math.sin(math.pi * clamp(u * 1.02)) ** 0.45)
            depth = Dp * (0.85 + 0.15 * math.sin(math.pi * u)) + (1.0 if u > 0.92 else 0) * (u - 0.92) * 12
            for j in range(M + 1):
                w = -1 + 2 * j / M
                x = beam * math.sin(w * math.pi / 2)
                y = depth * abs(w) ** 2.6
                p = (x, y, z)
                c = mix(WOOD_D, WOOD, 0.5 + 0.5 * math.sin(y * 7.0))      # the strakes
                if abs(math.sin(y * 3.5)) < 0.06:
                    c = WOOD_D
                mb.vert(p, c4(weather(c, p)))
        for i in range(n):
            for j in range(M):
                a = start + i * (M + 1) + j
                b = a + M + 1
                mb.face(a, a + 1, b + 1, b)
        # the keel and the stem
        tube(mb, [(0, 0.05, z0), (0, 0.05, z1)], [0.25, 0.25], 6, c4(WOOD_D))

    def deck(z0, z1, y, holes):
        for k in range(int(abs(z1 - z0) / 0.5)):
            z = z0 + k * 0.5 * (1 if z1 > z0 else -1)
            u = (z + L / 2) / L
            beam = B2 * (0.55 + 0.45 * math.sin(math.pi * clamp(u * 1.05)) ** 0.5) * 0.95
            if any(h0 < z < h1 for h0, h1 in holes):
                continue
            tube(mb, [(-beam, y, z), (beam, y, z)], [0.22, 0.22], 4, c4(weather(WOOD, (0, y, z), 0.2)))

    # the bow section (lies heeled 28 degrees to port) and the stern section, torn apart and twisted further
    s0 = len(mb.v)
    hull_section(-4.0, L / 2, 18, (True, False))
    deck(-3.5, L / 2 - 1.5, Dp * 0.92, [(2.0, 5.0)])
    # the bowsprit and the foremast stump
    tube(mb, [(0, Dp * 1.0, L / 2 - 1), (0, Dp * 1.6, L / 2 + 5)], [0.25, 0.12], 6, c4(WOOD_D))
    tube(mb, [(0, Dp * 0.9, 8), (0, Dp * 0.9 + 7, 8.3)], [0.32, 0.22], 8, c4(WOOD))
    # ribs bare at the break
    for k in range(6):
        z = -4.0 + k * 0.3
        pts = [(math.cos(a) * B2 * 0.95, Dp * 0.5 + math.sin(a) * Dp * 0.55, z) for a in [math.pi * (1.05 + 0.9 * j / 8) for j in range(9)]]
        tube(mb, pts, [0.18] * 9, 5, c4(WOOD_D))
    transform(mb, s0, lambda p: rot(p, 'z', math.radians(-28), (0, 0, 0)))
    s1 = len(mb.v)
    hull_section(-L / 2, -6.5, 12, (False, True))
    deck(-L / 2 + 1, -7.0, Dp * 0.92, [])
    # the raised stern castle with its windows
    for k in range(3):
        y = Dp * (0.95 + 0.3 * k)
        tube(mb, [(-B2 * 0.8, y, -L / 2 + 0.5), (B2 * 0.8, y, -L / 2 + 0.5)], [0.22, 0.22], 4, c4(WOOD))
    K.ellipsoid(mb, (0, Dp * 1.3, -L / 2 + 0.3), (B2 * 0.75, 1.2, 0.3), None, 12, 4, 0.0, lambda p: c4(mix(WOOD_D, (0.6, 0.48, 0.2), 0.4 if int(p[0] * 2) % 2 == 0 and p[1] > Dp * 1.3 else 0)))
    # the mainmast, broken and fallen across the sand
    tube(mb, [(0, Dp * 0.9, -10), (2.0, Dp * 0.9 + 3.5, -11)], [0.35, 0.3], 8, c4(WOOD))
    transform(mb, s1, lambda p: rot(rot(p, 'z', math.radians(-40), (0, 0, -8)), 'y', math.radians(14), (0, 0, -8)))
    s2 = len(mb.v)
    tube(mb, [(4.5, 0.35, -2), (9.0, 0.3, 6), (12.5, 0.4, 13)], [0.32, 0.28, 0.22], 8, c4(weather(WOOD, (9, 0, 6))))
    # cannons spilled on the sand, and a spar
    for i in range(5):
        x, z = -5.5 + H("c" + str(i), 1) * 3, -12 + i * 4.5
        a = H("c" + str(i), 2) * 6.28
        tube(mb, [(x, 0.3, z), (x + math.cos(a) * 2.2, 0.3, z + math.sin(a) * 2.2)], [0.3, 0.22], 10, c4((0.12, 0.13, 0.12)))
    for c in [mb.v[i] for i in range(s2, len(mb.v))]:
        pass
    # where the salvage lies: timbers by the break and in the stern, brass in the stern castle and by the guns
    salvage += [{"kind": "galleon_wood", "item": "Salvaged Galleon Wood", "pos": p} for p in [[-2.5, 0.2, -5.0], [3.5, 0.2, -3.0], [1.5, 0.5, 9.0], [-3.0, 0.4, -14.0], [6.0, 0.2, 2.0]]]
    salvage += [{"kind": "galleon_brass", "item": "Salvaged Galleon Brass", "pos": p} for p in [[-1.5, 0.2, -16.0], [-5.0, 0.2, -9.0], [-4.0, 0.2, 1.0]]]
    boxes += [{"c": [-1.5, 2.5, 5.5], "h": [4.0, 2.6, 9.5]}, {"c": [-2.0, 2.5, -10.5], "h": [4.0, 2.6, 4.8]}]
    return mb, salvage, boxes


# ------------------------------------------------------------------------------------------------ the dreadnought
def dreadnought():
    mb = MB()
    salvage, boxes = [], []
    STEEL = (0.32, 0.34, 0.35)
    STEEL_D = (0.18, 0.19, 0.2)
    RUST = (0.42, 0.22, 0.1)
    L, B2, Dp = 70.0, 6.0, 7.0

    def scol(c, p, rust=0.45):
        n = noise3((p[0] * 1.3, p[1] * 1.3, p[2] * 1.3), 3)
        if abs(math.sin(p[2] * 1.6)) < 0.03 or abs(math.sin(p[1] * 2.1)) < 0.03:
            c = mix(c, (0.08, 0.08, 0.08), 0.5)                  # plate seams
        if n > 1 - rust:
            c = mix(c, RUST, 0.75 * (n - (1 - rust)) / rust + 0.25)
        return c4(weather(c, p, 0.25, (0.5, 0.48, 0.42)))

    # the hull: a long box-ish body, the ram bow below the waterline, a rounded stern
    rings = []
    for i in range(41):
        t = i / 40
        z = -L / 2 + L * t
        beam = B2 * (1 - (max(0, t - 0.8) / 0.2) ** 1.6 * 0.85 - (max(0, 0.08 - t) / 0.08) ** 1.5 * 0.4)
        rings.append(((0, Dp * 0.5, z), (0, 0, 1), max(0.3, beam), Dp * 0.5, lambda a: 1.0 if math.cos(a) > -0.3 else 0.92))
    K.loft(mb, rings, 16, lambda t, a, p: scol(STEEL if math.cos(a) > 0 else STEEL_D, p))
    tube(mb, [(0, 1.0, L / 2 - 1), (0, 0.6, L / 2 + 3)], [1.2, 0.3], 8, scol(STEEL_D, (0, 1, L / 2)))          # the ram
    # the deck and its plating, a tear amidships
    for k in range(70):
        z = -L / 2 + 2 + k * 0.95
        if 4 < z < 9:
            continue
        u = (z + L / 2) / L
        beam = B2 * (1 - (max(0, u - 0.8) / 0.2) ** 1.6 * 0.85) * 0.95
        tube(mb, [(-beam, Dp, z), (beam, Dp, z)], [0.48, 0.48], 4, c4(mix(STEEL, RUST, 0.25 * noise3((z * 0.3, 0, 0), 1))))
    # two turrets with their guns, fore and aft
    for zt, d in ((20, 1), (-22, -1)):
        tube(mb, [(0, Dp, zt), (0, Dp + 1.6, zt)], [2.6, 2.4], 16, scol(STEEL, (0, Dp, zt)))
        for s in (-0.6, 0.6):
            tube(mb, [(s, Dp + 0.9, zt + d * 1.8), (s, Dp + 1.1, zt + d * 9.5)], [0.35, 0.28], 10, scol(STEEL_D, (s, Dp, zt)))
    # the superstructure, the bridge, two funnels, the masts
    tube(mb, [(0, Dp, 2), (0, Dp + 4.5, 2)], [3.2, 2.6], 4, scol(STEEL, (0, Dp, 2)))
    tube(mb, [(0, Dp + 4.5, 6), (0, Dp + 7, 6)], [2.2, 2.0], 4, scol(STEEL, (0, Dp + 4, 6)))
    for zf in (-4, -10):
        tube(mb, [(0, Dp, zf), (0, Dp + 9, zf - 0.8)], [1.4, 1.3], 14, scol(STEEL_D, (0, Dp, zf), 0.6))
    tube(mb, [(0, Dp + 7, 7), (0, Dp + 18, 7.3)], [0.35, 0.2], 8, scol(STEEL, (0, Dp + 10, 7)))
    tube(mb, [(0, Dp + 4, -16), (0, Dp + 14, -16)], [0.3, 0.18], 8, scol(STEEL, (0, Dp + 10, -16)))
    tube(mb, [(-3.5, Dp + 15, 7.2), (3.5, Dp + 15, 7.2)], [0.15, 0.15], 6, scol(STEEL, (0, Dp + 15, 7)))
    # torn plates folded out of the hull at the tear, and debris
    for i in range(5):
        a = H("p" + str(i), 1) * 2
        K.ellipsoid(mb, (B2 + 0.5 + i * 0.3, Dp * 0.6 - i * 0.6, 5 + i * 0.8), (0.1, 1.2, 1.6), None, 6, 3, 0.0, lambda p: scol(STEEL, p, 0.8))
    salvage += [{"kind": "military_titanium", "item": "Military-Grade Titanium", "pos": p} for p in [[7.2, 1.2, 6.0], [-6.8, 1.0, 14.0], [0.0, Dp + 1.6, 22.0], [6.5, 0.3, -18.0]]]
    salvage += [{"kind": "iron_scrap", "item": "Rusted Iron Scrap", "pos": p} for p in [[0.0, Dp + 0.2, 6.5], [-7.5, 0.3, 2.0], [7.5, 0.3, -6.0], [0.0, Dp + 0.2, -25.0], [-7.0, 0.3, -28.0]]]
    boxes += [{"c": [0, Dp * 0.5, 0], "h": [B2, Dp * 0.5 + 0.3, L * 0.5]}, {"c": [0, Dp + 2.5, 3.5], "h": [3.2, 2.6, 4.0]}]
    # the whole ship settled a little bow-down and listing to starboard in the mud
    transform(mb, 0, lambda p: rot(rot(p, 'z', math.radians(9)), 'x', math.radians(2.5)))
    return mb, salvage, boxes


def main():
    a = K.args()
    out = a[a.index("--out") + 1] if "--out" in a else os.path.join(K.ROOT, "TheDeep", "Assets", "Deep", "Resources", "Wrecks")
    os.makedirs(out, exist_ok=True)
    for name, fn in (("galleon", galleon), ("dreadnought", dreadnought)):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        mb, salvage, boxes = fn()
        o = K.to_object("wreck_" + name, mb)
        o.select_set(True)
        path = os.path.join(out, f"wreck_{name}.glb")
        bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=True, export_yup=True,
                                  export_texcoords=True, export_normals=True, export_tangents=False, export_materials='NONE',
                                  export_vertex_color='ACTIVE', export_all_vertex_colors=False)
        with open(os.path.join(out, f"wreck_{name}.json"), "w") as f:
            json.dump({"salvage": salvage, "boxes": boxes}, f, indent=1)
        print(f"artgen: wrote {path} ({os.path.getsize(path) // 1024} KB, {sum(len(x) - 2 for x in mb.f)} triangles)")


if __name__ == "__main__":
    main()
