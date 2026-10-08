# The Abandoned Underwater City (stage 9; the design doc: "a sprawling flooded city of unknown origin deep in the cave
# network, built of smooth, obsidian-like stone and archways"): a kit of its stonework for Caverns' city builder, and
# the salvage the doc names (Smooth Obsidian Stonework, Resonance Stones) and the Echo-Ray's guarded egg clusters as
# gathering nodes. One glb, Caverns/city.glb, an object per piece; +y up, metres, each standing on y = 0.
# The stone is glassy black with faint carved channels that glow a cold teal (the city's geometry is wrong somehow).
#     blender -b --factory-startup -P tools/artgen/deep_city.py -- --out TheDeep/Assets/Deep/Resources/Caverns
import bpy
import math
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deep_creatures as K
from deep_creatures import MB, loft, tube, ellipsoid, cone, mix, clamp, H

OBS = (0.05, 0.055, 0.07)
OBS_L = (0.12, 0.13, 0.16)
GLYPH = (0.25, 0.85, 0.8)
EGG = (0.85, 0.7, 0.85)


def c4(c, a=0.0):
    return (c[0], c[1], c[2], a)


def stone(p, glyph_band=0.0):
    """Obsidian with carved channels: a teal glow in thin bands (glyph_band sets how many)."""
    g = 0.0
    if glyph_band:
        g = 1.0 if abs(math.sin(p[1] * glyph_band * 6.0)) > 0.985 and abs(math.sin(p[0] * 9 + p[2] * 7)) > 0.3 else 0.0
    return c4(mix(mix(OBS, OBS_L, 0.5 + 0.5 * math.sin(p[0] * 3 + p[1] * 1.3 + p[2] * 2)), GLYPH, g * 0.8))


def box(mb, c, h, colfn):
    x, y, z = c
    a, b, d = h
    P = [(x - a, y - b, z - d), (x + a, y - b, z - d), (x + a, y + b, z - d), (x - a, y + b, z - d),
         (x - a, y - b, z + d), (x + a, y - b, z + d), (x + a, y + b, z + d), (x - a, y + b, z + d)]
    for f in [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (3, 7, 6, 2), (0, 4, 7, 3), (1, 2, 6, 5)]:
        ix = [mb.vert(P[i], colfn(P[i])) for i in f]
        mb.face(*ix)


def column(broken=False):
    mb = MB()
    box(mb, (0, 0.2, 0), (0.9, 0.2, 0.9), lambda p: stone(p))
    h = 4.2 if broken else 9.0
    segs = 14
    rings = []
    for i in range(13):
        t = i / 12
        y = 0.4 + h * t
        r = 0.6 * (1 - 0.12 * t) * (1 + 0.04 * math.sin(t * 20))
        rings.append(((0, y, 0), (0, 1, 0), r, r, lambda a: 1.0 + 0.06 * math.cos(a * 8)))   # fluted
    loft(mb, rings, segs, lambda t, a, p: stone(p, 1.2), cap0=False, cap1=not broken)
    if broken:   # a jagged break and a fallen drum beside it
        for k in range(6):
            a = k / 6 * math.pi * 2
            cone(mb, (math.cos(a) * 0.45, h + 0.35, math.sin(a) * 0.45), (math.cos(a) * 0.4, h + 0.7 + 0.4 * H("c" + str(k), 1), math.sin(a) * 0.4), 0.18, stone((0, h, 0)), 4)
        rings = [((-1.6 + 1.3 * t, 0.55, 1.4), (1, 0, 0), 0.55, 0.55, None) for t in (0, 1)]
        loft(mb, rings, segs, lambda t, a, p: stone(p, 1.2))
    else:
        box(mb, (0, h + 0.55, 0), (0.85, 0.18, 0.85), lambda p: stone(p))
    return mb


def arch():
    mb = MB()
    for s in (-1, 1):
        box(mb, (s * 3.0, 3.0, 0), (0.6, 3.0, 0.7), lambda p: stone(p, 1.5))
    pts, radii = [], []
    for i in range(17):
        a = math.pi * i / 16
        pts.append((-math.cos(a) * 3.0, 6.0 + math.sin(a) * 2.2, 0))
        radii.append(0.62)
    tube(mb, pts, radii, 8, None, colfn=lambda t, a, p: stone(p, 2.0))
    box(mb, (0, 8.5, 0), (0.5, 0.35, 0.7), lambda p: c4(GLYPH) if abs(p[0]) < 0.25 else stone(p))   # a keystone with a sigil
    return mb


def wall():
    mb = MB()
    box(mb, (0, 3.5, 0), (5.0, 3.5, 0.5), lambda p: stone(p, 1.0))
    # windows: tall slits (dark recesses built as inset boxes)
    for x in (-3, 0, 3):
        box(mb, (x, 4.2, 0.45), (0.45, 1.6, 0.08), lambda p: c4((0.01, 0.01, 0.015)))
    box(mb, (0, 7.2, 0), (5.3, 0.25, 0.65), lambda p: stone(p))
    return mb


def stairs():
    mb = MB()
    for i in range(8):
        box(mb, (0, 0.2 + i * 0.4, -i * 0.6), (3.0, 0.2 + i * 0.0, 0.3 + (8 - i) * 0.0), lambda p: stone(p, 0.6))
        box(mb, (0, 0.1 + i * 0.2, -i * 0.6 - 0.1), (3.0, 0.1 + i * 0.2, 0.32), lambda p: stone(p))
    return mb


def statue():
    """A figure of no known kind: a tall robed body, too many joints in its long arms, a smooth eyeless head."""
    mb = MB()
    box(mb, (0, 0.5, 0), (1.0, 0.5, 1.0), lambda p: stone(p, 1.0))
    rings = []
    for i in range(10):
        t = i / 9
        y = 1.0 + 4.8 * t
        r = 0.85 * (1 - 0.55 * t) + 0.12 * math.sin(t * 9)
        rings.append(((0, y, 0), (0, 1, 0), r, r * 0.8, None))
    loft(mb, rings, 12, lambda t, a, p: stone(p, 0.8))
    ellipsoid(mb, (0, 6.4, 0.1), (0.42, 0.75, 0.45), stone((0, 6, 0)), 12, 8)
    for s in (-1, 1):
        pts = [(s * 0.5, 5.0, 0), (s * 1.2, 4.6, 0.3), (s * 1.5, 3.6, 0.5), (s * 1.3, 2.6, 0.9), (s * 0.9, 2.0, 1.1)]
        tube(mb, pts, [0.2, 0.17, 0.15, 0.12, 0.08], 6, None, colfn=lambda t, a, p: stone(p))
    return mb


def plinth():
    mb = MB()
    box(mb, (0, 0.6, 0), (1.4, 0.6, 1.4), lambda p: stone(p, 1.5))
    box(mb, (0, 1.3, 0), (1.1, 0.1, 1.1), lambda p: stone(p))
    return mb


def block():
    mb = MB()
    box(mb, (0, 0.6, 0), (1.3, 0.6, 0.8), lambda p: stone(p, 1.4))
    box(mb, (0.5, 1.3, 0.2), (0.6, 0.12, 0.5), lambda p: stone(p))
    return mb


def obsidian_block():   # salvage: a dressed stone that can be cut free
    mb = MB()
    box(mb, (0, 0.35, 0), (0.55, 0.35, 0.4), lambda p: stone(p, 2.5))
    return mb


def resonance_stone():  # salvage: a faceted geometric stone that hums
    mb = MB()
    for k in range(6):
        a = k / 6 * math.pi * 2
        cone(mb, (0, 0.45, 0), (math.cos(a) * 0.35, 0.45, math.sin(a) * 0.35), 0.25, c4(mix(GLYPH, (0.1, 0.2, 0.25), 0.4)), 4)
    cone(mb, (0, 0.2, 0), (0, 0.95, 0), 0.22, c4(mix(GLYPH, (1, 1, 1), 0.3)), 6)
    cone(mb, (0, 0.5, 0), (0, 0.0, 0), 0.22, c4(mix(GLYPH, (0.1, 0.2, 0.25), 0.3)), 6)
    return mb


def echo_egg_cluster():   # the Echo-Ray's eggs, clustered on the rock (collected by hand)
    mb = MB()
    for i in range(14):
        a, r = H("egg" + str(i), 1) * 6.28, H("egg" + str(i), 2) * 0.3
        ellipsoid(mb, (math.cos(a) * r, 0.09 + 0.05 * H("egg" + str(i), 3), math.sin(a) * r), (0.08, 0.1, 0.12), c4(mix(EGG, (0.4, 0.2, 0.5), H("egg" + str(i), 4) * 0.4)), 8, 6)
    return mb


def shed_scale():   # a Tangle-Serpent's shed scale lying on the seabed: a curved, iridescent plate (collected by hand)
    mb = MB()
    rings = []
    for i in range(7):
        t = i / 6
        rings.append(((0, 0.04 + 0.08 * math.sin(math.pi * t), -0.35 + 0.7 * t), (0, 0, 1), 0.32 * math.sin(math.pi * min(1, t * 0.9 + 0.1)) + 0.02, 0.03, None))
    loft(mb, rings, 10, lambda t, a, p: c4(mix((0.42, 0.25, 0.55), (0.3, 0.55, 0.35), 0.5 + 0.5 * math.sin(p[0] * 12 + p[2] * 8))))
    return mb


PIECES = [("shed_scale", shed_scale), ("city_column", lambda: column(False)), ("city_column_broken", lambda: column(True)), ("city_arch", arch), ("city_wall", wall),
          ("city_stairs", stairs), ("city_statue", statue), ("city_plinth", plinth), ("city_block", block),
          ("obsidian_block", obsidian_block), ("resonance_stone", resonance_stone), ("echo_egg_cluster", echo_egg_cluster)]


def main():
    a = K.args()
    out = a[a.index("--out") + 1] if "--out" in a else os.path.join(K.ROOT, "TheDeep", "Assets", "Deep", "Resources", "Caverns")
    os.makedirs(out, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    objs = [K.to_object(n, f()) for n, f in PIECES]
    for o in objs:
        o.select_set(True)
    path = os.path.join(out, "city.glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=True, export_yup=True,
                              export_texcoords=True, export_normals=True, export_tangents=False, export_materials='NONE',
                              export_vertex_color='ACTIVE', export_all_vertex_colors=False)
    print(f"artgen: wrote {path} ({os.path.getsize(path) // 1024} KB, {len(objs)} pieces)")


if __name__ == "__main__":
    main()
