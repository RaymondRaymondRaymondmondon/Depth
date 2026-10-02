# The Sunken Ship's kit (the Red Tide Visual Overhaul Spec, "The Sunken Ship": the Marguerite, a 60 m steam yacht):
# the furnishings and fittings the level dresses its rooms with, baked like the guns (the gun kit's baker, one PBR
# texture set each): the salon's chandelier, armchairs, a round table and an upright piano; the galley's range; the
# cabins' bunks; the engine room's generator; the bridge's wheel; crates, barrels; the funnel. Placed by the game in
# each zone (redtide_game.cpp, ShipDressing). Frame: +X forward, +Z up, +Y left; metres; the base at z = 0.
#     blender -b --factory-startup -P tools/artgen/ship_rt.py -- --out assets/redtide/ship [--only chandelier]

import os
import sys
import math
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import common as C
import gunkit as K
bpy = C.bpy


def build_chandelier(W):
    W.add(W.cyl("column", 0.03, 0.6, (0, 0, 0.3), 'Z', verts=20, bevel=0.005), "brass")
    for k in range(5):
        W.add(W.ring(f"chain{k}", (0, 0, 0.62 + k * 0.05), 0.02, 0.006, 'Y' if k % 2 else 'X'), "brass")
    for tier, (z, r, n) in enumerate([(0.12, 0.42, 8), (0.36, 0.26, 6)]):
        W.add(W.ring(f"hoop{tier}", (0, 0, z), r, 0.012, 'Z'), "brass")
        for k in range(n):
            a = 2 * math.pi * k / n
            x, y = r * math.cos(a), r * math.sin(a)
            W.add(W.tube(f"arm{tier}{k}", [(0, 0, z + 0.05), (x * 0.6, y * 0.6, z + 0.02), (x, y, z)], 0.008), "brass")
            W.add(W.cyl(f"cup{tier}{k}", 0.025, 0.02, (x, y, z + 0.015), 'Z', verts=14, bevel=0.002), "brass")
            W.add(W.cyl(f"candle{tier}{k}", 0.012, 0.08, (x, y, z + 0.065), 'Z', verts=10, bevel=0.001), "bone")
            W.add(W.sphere(f"drop{tier}{k}", (x * 1.02, y * 1.02, z - 0.07), (0.014, 0.014, 0.03), 10), "glass")
    W.add(W.sphere("finial", (0, 0, -0.04), (0.05, 0.05, 0.07), 16), "brass")


def build_armchair(W):
    W.add(W.box("seat", (0.62, 0.62, 0.14), (0, 0, 0.42), bevel=0.04, segments=3), "red")
    W.add(W.box("back", (0.14, 0.62, 0.62), (-0.26, 0, 0.78), bevel=0.05, segments=3), "red")
    for s in (-1, 1):
        W.add(W.box(f"arm{s}", (0.6, 0.1, 0.22), (0.0, s * 0.31, 0.6), bevel=0.04, segments=3), "red")
        W.add(W.box(f"armcap{s}", (0.62, 0.11, 0.035), (0.0, s * 0.31, 0.72), bevel=0.012), "rosewood")
    for x in (-0.26, 0.26):
        for y in (-0.26, 0.26):
            W.add(W.cyl(f"leg{x}{y}", 0.025, 0.36, (x, y, 0.18), 'Z', verts=12, bevel=0.004), "rosewood")
    for k in range(9):   # the buttons in the back
        W.add(W.sphere(f"button{k}", (-0.18, -0.2 + (k % 3) * 0.2, 0.62 + (k // 3) * 0.16), (0.012, 0.012, 0.012), 8), "brass")


def build_table(W):
    W.add(W.cyl("top", 0.55, 0.04, (0, 0, 0.74), 'Z', verts=48, bevel=0.01), "rosewood")
    W.add(W.ring("top_edge", (0, 0, 0.74), 0.55, 0.012, 'Z'), "brass")
    W.add(W.cyl("pillar", 0.06, 0.6, (0, 0, 0.42), 'Z', verts=20, bevel=0.01), "rosewood")
    for k in range(3):
        a = 2 * math.pi * k / 3
        W.add(W.tube(f"foot{k}", [(0, 0, 0.14), (0.25 * math.cos(a), 0.25 * math.sin(a), 0.05), (0.38 * math.cos(a), 0.38 * math.sin(a), 0.02)], 0.03), "rosewood")


def build_piano(W):
    W.add(W.box("case", (0.6, 1.5, 1.25), (0, 0, 0.66), bevel=0.02, segments=2), "rosewood")
    W.add(W.box("keybed", (0.32, 1.4, 0.06), (0.4, 0, 0.72), bevel=0.01), "rosewood")
    W.add(W.box("keys", (0.16, 1.32, 0.02), (0.47, 0, 0.765), bevel=0.003), "bone")
    for k in range(36):
        W.add(W.box(f"black{k}", (0.09, 0.014, 0.018), (0.43, -0.64 + k * 0.0365, 0.784), bevel=0.002), "obsidian")
    W.add(W.box("music_desk", (0.03, 0.9, 0.3), (0.32, 0, 1.0), bevel=0.01), "rosewood")
    for s in (-1, 1):
        W.add(W.cyl(f"candle_arm{s}", 0.012, 0.18, (0.32, s * 0.62, 1.05), 'X', verts=10, bevel=0.002), "brass")
    for x in (-0.15, 0.15):
        W.add(W.cyl(f"castor{x}", 0.03, 0.03, (x, 0.65, 0.02), 'Y', verts=12, bevel=0.003), "brass")


def build_range(W):
    W.add(W.box("body", (0.7, 1.4, 0.85), (0, 0, 0.43), bevel=0.01, segments=2), "iron")
    W.add(W.box("top", (0.75, 1.45, 0.05), (0, 0, 0.88), bevel=0.01), "steel")
    for k in range(2):
        W.add(W.box(f"oven_door{k}", (0.02, 0.5, 0.4), (0.36, -0.33 + k * 0.66, 0.42), bevel=0.01), "iron")
        W.add(W.cyl(f"handle{k}", 0.01, 0.3, (0.39, -0.33 + k * 0.66, 0.64), 'Y', verts=10, bevel=0.002), "brass")
    for k in range(3):
        W.add(W.cyl(f"pot{k}", 0.12 + 0.02 * k, 0.18, (0.05, -0.45 + k * 0.45, 1.0), 'Z', verts=24, bevel=0.005), "copper")
    W.add(W.cyl("flue", 0.09, 1.0, (-0.25, 0.5, 1.4), 'Z', verts=20, bevel=0.005), "iron")


def build_bunk(W):
    for x in (-0.95, 0.95):
        for y in (-0.42, 0.42):
            W.add(W.cyl(f"post{x}{y}", 0.025, 1.7, (x, y, 0.85), 'Z', verts=12, bevel=0.004), "iron")
    for z in (0.35, 1.25):
        W.add(W.box(f"frame{z}", (2.0, 0.9, 0.05), (0, 0, z), bevel=0.01), "iron")
        W.add(W.box(f"mattress{z}", (1.9, 0.8, 0.14), (0, 0, z + 0.1), bevel=0.05, segments=3), "canvas")
        W.add(W.box(f"blanket{z}", (1.1, 0.82, 0.04), (0.35, 0, z + 0.18), bevel=0.02, segments=2), "red")
    W.add(W.box("pillow", (0.3, 0.6, 0.1), (-0.75, 0, 1.43), bevel=0.04, segments=3), "bone")


def build_generator(W):
    W.add(W.cyl("drum", 0.45, 1.3, (0, 0, 0.6), 'X', verts=40, bevel=0.02), "steel")
    for x in (-0.6, -0.3, 0.0, 0.3, 0.6):
        W.add(W.ring(f"band{x}", (x, 0, 0.6), 0.46, 0.02, 'X'), "brass")
    W.add(W.cyl("shaft", 0.06, 0.5, (0.85, 0, 0.6), 'X', verts=20, bevel=0.005), "bright")
    W.add(W.cyl("flywheel", 0.38, 0.1, (1.1, 0, 0.6), 'X', verts=40, bevel=0.01, hole=0.3), "iron")
    for k in range(6):
        a = k * math.pi / 3
        W.add(W.box(f"spoke{k}", (0.06, 0.3, 0.05), (1.1, 0.15 * math.cos(a), 0.6 + 0.15 * math.sin(a)), bevel=0.01, rot=(a, 0, 0)), "iron")
    W.add(W.box("base", (1.8, 0.9, 0.12), (0.2, 0, 0.06), bevel=0.02), "iron")
    for k in range(2):
        W.add(W.cyl(f"gauge{k}", 0.08, 0.04, (-0.2 + k * 0.4, 0.46, 0.95), 'Y', verts=24, bevel=0.005), "brass")
    W.add(W.tube("pipe", [(-0.6, 0.0, 1.0), (-0.7, 0.0, 1.4), (-0.7, 0.4, 1.8)], 0.05), "copper")


def build_wheel(W):
    W.add(W.cyl("pedestal", 0.12, 1.0, (0, 0, 0.5), 'Z', verts=20, bevel=0.01), "rosewood")
    W.add(W.ring("rim", (0.15, 0, 1.15), 0.42, 0.03, 'X'), "rosewood")
    W.add(W.cyl("hub", 0.07, 0.1, (0.15, 0, 1.15), 'X', verts=20, bevel=0.01), "brass")
    for k in range(8):
        a = k * math.pi / 4
        W.add(W.cyl(f"spoke{k}", 0.018, 0.62, (0.15, 0.29 * math.cos(a), 1.15 + 0.29 * math.sin(a)), 'X', verts=10, bevel=0.003, ), "rosewood")
        W.add(W.cyl(f"handle{k}", 0.022, 0.14, (0.15, 0.52 * math.cos(a), 1.15 + 0.52 * math.sin(a)), 'Z', verts=10, bevel=0.004), "rosewood")
    W.add(W.cyl("binnacle", 0.18, 0.2, (0.6, 0, 1.05), 'Z', verts=24, bevel=0.02), "brass")


def build_crate(W):
    W.add(W.box("crate", (0.8, 0.6, 0.6), (0, 0, 0.3), bevel=0.01), "laminate")
    for z in (0.08, 0.52):
        W.add(W.box(f"band{z}", (0.82, 0.62, 0.04), (0, 0, z), bevel=0.004), "iron")


def build_barrel(W):
    W.add(W.cyl("staves", 0.3, 0.9, (0, 0, 0.45), 'Z', verts=28, bevel=0.03), "laminate")
    for z in (0.12, 0.45, 0.78):
        W.add(W.ring(f"hoop{z}", (0, 0, z), 0.31, 0.015, 'Z'), "iron")


def build_funnel(W):
    W.add(W.cyl("funnel", 0.9, 5.0, (0, 0, 2.5), 'Z', verts=40, bevel=0.03, hole=0.8), "steel")
    W.add(W.ring("band", (0, 0, 4.2), 0.92, 0.12, 'Z'), "red")
    W.add(W.ring("cap", (0, 0, 5.0), 0.92, 0.05, 'Z'), "iron")
    for k in range(4):
        a = k * math.pi / 2 + math.pi / 4
        W.add(W.tube(f"stay{k}", [(0.9 * math.cos(a), 0.9 * math.sin(a), 4.6), (2.8 * math.cos(a), 2.8 * math.sin(a), 0.1)], 0.015), "rope")


RECIPES = {n[6:]: f for n, f in globals().items() if n.startswith("build_")}

if __name__ == "__main__":
    a = C.args()
    only = a[a.index("--only") + 1].split(",") if "--only" in a else None
    out = C.out_dir()
    for pid, fn in RECIPES.items():
        if only and pid not in only:
            continue
        W = K.Weapon(pid)
        fn(W)
        W.finish(out, size=512)
