# The other four maps' kits (the Red Tide Visual Overhaul Spec, "Per-map treatment"), baked like the guns: the props the
# levels already place as boxes (Level::props) get real models. The Cave: a stalagmite cluster and crystal coral. The
# Reef: brain, table, staghorn and fan corals, a giant clam. Atlantis: a fluted column, a bronze statue, an amphora,
# a brazier. The Void: a specimen tank, a glass sponge. Frame: +X forward, +Z up, +Y left; metres; base at z = 0;
# roughly unit size (the game scales each to the prop's box).
#     blender -b --factory-startup -P tools/artgen/maps_rt.py -- --out assets/redtide/maps [--only brain]

import os
import sys
import math
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import common as C
import gunkit as K
bpy = C.bpy


def build_stalagmites(W):
    for k, (x, y, r, hgt) in enumerate([(0, 0, 0.18, 1.0), (0.22, 0.1, 0.1, 0.55), (-0.18, -0.12, 0.12, 0.7), (0.05, -0.25, 0.07, 0.35)]):
        W.add(W.cone(f"mite{k}", r, r * 0.12, hgt, (x, y, hgt / 2), 'Z', verts=14), "iron")
        W.add(W.ring(f"lip{k}", (x, y, hgt * 0.35), r * 0.75, r * 0.12, 'Z'), "iron")


def build_crystal(W):
    for k in range(9):
        a = k * 2.4; lean = 0.25 + 0.1 * (k % 3)
        hgt = 0.4 + 0.35 * ((k * 7) % 5) / 4.0
        x, y = 0.18 * math.cos(a), 0.18 * math.sin(a)
        o = W.cone(f"spar{k}", 0.06, 0.0, hgt, (x + math.cos(a) * lean * hgt * 0.5, y + math.sin(a) * lean * hgt * 0.5, hgt * 0.45), 'Z', verts=6)
        o.rotation_euler = (math.sin(a) * lean, -math.cos(a) * lean, 0)
        W.add(o, "glass")
    W.add(W.sphere("base", (0, 0, 0.05), (0.25, 0.25, 0.08), 12), "iron")


def build_brain(W):
    W.add(W.sphere("dome", (0, 0, 0.3), (0.5, 0.5, 0.36), 32), "coral")
    for k in range(10):
        a = k * 0.62
        W.add(W.tube(f"groove{k}", [(0.42 * math.cos(a), 0.42 * math.sin(a), 0.16), (0.2 * math.cos(a + 0.6), 0.2 * math.sin(a + 0.6), 0.55), (0.0, 0.0, 0.66)], 0.025), "bone")


def build_table(W):
    W.add(W.cyl("stem", 0.07, 0.45, (0, 0, 0.22), 'Z', verts=14, bevel=0.01), "coral")
    W.add(W.cyl("plate", 0.6, 0.05, (0, 0, 0.47), 'Z', verts=36, bevel=0.02), "coral")
    for k in range(18):
        a = k * math.pi / 9
        W.add(W.cone(f"branch{k}", 0.02, 0.005, 0.12, (0.5 * math.cos(a), 0.5 * math.sin(a), 0.53), 'Z', verts=6), "bone")


def build_staghorn(W):
    for k in range(9):
        a = k * 0.7; lean = 0.35 + 0.15 * (k % 2)
        tip = (0.45 * math.cos(a) * lean, 0.45 * math.sin(a) * lean, 0.5 + 0.25 * (k % 3) / 2)
        W.add(W.tube(f"branch{k}", [(0, 0, 0.0), (tip[0] * 0.4, tip[1] * 0.4, tip[2] * 0.45), tip], 0.025), "coral")
        W.add(W.tube(f"fork{k}", [(tip[0] * 0.6, tip[1] * 0.6, tip[2] * 0.65), (tip[0] * 0.9 + 0.08, tip[1] * 0.9, tip[2] * 0.95)], 0.017), "coral")


def build_fan(W):
    W.add(W.cyl("stalk", 0.025, 0.25, (0, 0, 0.12), 'Z', verts=10, bevel=0.003), "coral")
    for k in range(13):
        a = math.radians(-60 + k * 10)
        W.add(W.tube(f"rib{k}", [(0, 0, 0.22), (0, 0.35 * math.sin(a), 0.22 + 0.35 * math.cos(a)), (0, 0.6 * math.sin(a), 0.22 + 0.62 * math.cos(a))], 0.008), "red")
    for r in (0.25, 0.4, 0.55):
        W.add(W.tube(f"web{r}", [(0, r * math.sin(math.radians(a2)), 0.22 + r * math.cos(math.radians(a2))) for a2 in range(-60, 61, 10)], 0.004), "red")


def build_clam(W):
    for s in (1, -1):
        sh = W.sphere(f"valve{s}", (0, 0, 0.22 if s > 0 else 0.16), (0.55, 0.4, 0.14), 28)
        W.add(sh, "bone")
    W.add(W.box("mantle", (1.0, 0.7, 0.08), (0, 0, 0.2), bevel=0.03, segments=3), "verdigris")
    for k in range(7):
        W.add(W.ring(f"ridge{k}", (0, 0, 0.2), 0.15 + k * 0.06, 0.008, 'Z'), "bone")


def build_column(W):
    W.add(W.box("plinth", (0.7, 0.7, 0.15), (0, 0, 0.075), bevel=0.01), "bone")
    W.add(W.cyl("shaft", 0.24, 2.6, (0, 0, 1.45), 'Z', verts=24, bevel=0.01), "bone")
    for k in range(12):
        a = k * math.pi / 6
        W.add(W.cyl(f"flute{k}", 0.025, 2.5, (0.24 * math.cos(a), 0.24 * math.sin(a), 1.45), 'Z', verts=8, bevel=0), "bone")
    W.add(W.box("capital", (0.62, 0.62, 0.14), (0, 0, 2.82), bevel=0.02), "bone")
    W.add(W.box("abacus", (0.7, 0.7, 0.08), (0, 0, 2.93), bevel=0.01), "bone")


def build_statue(W):
    W.add(W.box("base", (0.7, 0.7, 0.5), (0, 0, 0.25), bevel=0.02), "bone")
    W.add(W.cyl("legs", 0.16, 0.9, (0, 0, 0.95), 'Z', verts=16, bevel=0.03), "verdigris")
    W.add(W.sphere("torso", (0, 0, 1.6), (0.22, 0.3, 0.38), 20), "verdigris")
    W.add(W.sphere("head", (0, 0, 2.12), (0.13, 0.13, 0.16), 18), "verdigris")
    W.add(W.tube("arm_spear", [(0, 0.3, 1.8), (0.1, 0.38, 2.1), (0.12, 0.38, 2.3)], 0.05), "verdigris")
    W.add(W.cyl("spear", 0.015, 2.4, (0.12, 0.38, 1.6), 'Z', verts=8, bevel=0), "verdigris")
    W.add(W.tube("arm_shield", [(0, -0.3, 1.8), (0.15, -0.36, 1.55)], 0.05), "verdigris")
    W.add(W.cyl("shield", 0.3, 0.04, (0.2, -0.38, 1.5), 'X', verts=28, bevel=0.01), "verdigris")


def build_amphora(W):
    W.add(W.sphere("belly", (0, 0, 0.4), (0.22, 0.22, 0.3), 24), "coral")
    W.add(W.cyl("neck", 0.07, 0.25, (0, 0, 0.78), 'Z', verts=18, bevel=0.01), "coral")
    W.add(W.ring("lip", (0, 0, 0.9), 0.08, 0.02, 'Z'), "coral")
    W.add(W.cone("foot", 0.05, 0.12, 0.12, (0, 0, 0.06), 'Z', verts=18), "coral")
    for s in (1, -1):
        W.add(W.tube(f"handle{s}", [(0, s * 0.07, 0.85), (0, s * 0.18, 0.82), (0, s * 0.16, 0.6)], 0.018), "coral")


def build_brazier(W):
    W.add(W.cyl("pedestal", 0.08, 1.0, (0, 0, 0.5), 'Z', verts=16, bevel=0.01), "verdigris")
    W.add(W.cone("bowl", 0.12, 0.35, 0.2, (0, 0, 1.1), 'Z', verts=28), "verdigris")
    for k in range(3):
        a = k * 2 * math.pi / 3
        W.add(W.tube(f"leg{k}", [(0, 0, 0.3), (0.25 * math.cos(a), 0.25 * math.sin(a), 0.02)], 0.025), "verdigris")
    W.add(W.sphere("ember", (0, 0, 1.18), (0.22, 0.22, 0.06), 18), "glass")


def build_tank(W):
    W.add(W.box("base", (1.0, 1.0, 0.2), (0, 0, 0.1), bevel=0.02), "steel")
    W.add(W.cyl("glass", 0.45, 1.6, (0, 0, 1.0), 'Z', verts=32, bevel=0.0, hole=0.43), "glass")
    W.add(W.box("cap", (1.0, 1.0, 0.18), (0, 0, 1.89), bevel=0.02), "steel")
    for k in range(4):
        a = k * math.pi / 2 + math.pi / 4
        W.add(W.cyl(f"post{k}", 0.035, 1.6, (0.47 * math.cos(a), 0.47 * math.sin(a), 1.0), 'Z', verts=10, bevel=0.003), "brass")
    W.add(W.tube("pipe", [(0, 0, 1.98), (0, 0.3, 2.2), (0, 0.8, 2.2)], 0.04), "copper")
    W.add(W.sphere("specimen", (0, 0, 0.9), (0.18, 0.12, 0.3), 16), "coral")


def build_sponge(W):
    W.add(W.cyl("vase", 0.22, 1.1, (0, 0, 0.55), 'Z', verts=20, bevel=0.0, hole=0.2), "glass")
    for k in range(10):
        W.add(W.ring(f"lattice{k}", (0, 0, 0.1 + k * 0.1), 0.215 + k * 0.006, 0.006, 'Z'), "bone")
    for k in range(8):
        a = k * math.pi / 4
        W.add(W.cyl(f"rib{k}", 0.006, 1.1, (0.22 * math.cos(a), 0.22 * math.sin(a), 0.55), 'Z', verts=6, bevel=0), "bone")


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
