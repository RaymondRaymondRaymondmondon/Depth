# Warp Dodgeball's gym props (the 3D polish pass, 2026-10-05: "add aesthetic background items so the games feel alive").
# Built in GAME coordinates through fowl_props' G() helpers (x, y up, z), each about its own origin on the floor:
#     bench        a 4 m slatted gym bench along x: three slats on steel A-frame legs with a foot rail
#     kitbag       a duffel bag with end caps, a zip and two handles (the game tints the cloth by team)
#     bottle       a squeeze bottle with a sports cap (the game tints the cap)
#     towel        a folded towel (tinted)
#     ballcart     a wire ball cart on castors (the game puts balls in it)
#     cooler       a water cooler on a stand with a stack of paper cups
#     scoretable   the scorer's table: a skirted table with a flip scoreboard, a whistle and a clipboard
#     ball         a playground ball of unit radius, white rubber with raised seams (the game tints it)
#     boardfoot    the steel foot of a freestanding portal board (one per end)
#     pennant      a string of five triangular flags hanging between two points 4 m apart (tinted per team)
#     blender -b --factory-startup -P tools/artgen/warp_props.py -- --out assets/warp
import os
import sys
import math
os.environ["ARTGEN_IMPORT"] = "1"
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import fowl_props as F
from fowl_props import G, gbox, gcyl, glathe, sphere, tube, torus, mat
C = F.C


def mats():
    F.setup()
    mat("steel", (0.55, 0.56, 0.6), 0.35, 1.0)
    mat("maple", (0.72, 0.5, 0.28), 0.45)
    mat("cloth", (0.85, 0.85, 0.85), 0.85)
    mat("rubber", (0.05, 0.05, 0.05), 0.7)
    mat("white", (0.92, 0.92, 0.9), 0.55)
    mat("seam", (0.55, 0.55, 0.52), 0.6)
    mat("blue_plastic", (0.25, 0.55, 0.85), 0.2)
    mat("water", (0.55, 0.8, 0.95), 0.05)
    mat("navy", (0.06, 0.1, 0.22), 0.6)
    mat("paper", (0.95, 0.94, 0.9), 0.8)


def bench():
    for k in range(3):
        gbox(f"slat{k}", (4.0, 0.04, 0.11), (0, 0.44, -0.13 + k * 0.13), "maple", 0.01)
    for x in (-1.7, 0, 1.7):
        for s in (-1, 1):
            tube(f"leg{x}{s}", [G(x, 0.42, s * 0.05), G(x, 0.02, s * 0.17)], 0.018, "steel")
        gbox(f"cross{x}", (0.04, 0.04, 0.34), (x, 0.4, 0), "steel", 0.008)
        for s in (-1, 1):
            gbox(f"foot{x}{s}", (0.06, 0.02, 0.06), (x, 0.01, s * 0.17), "rubber", 0.005)
    tube("rail", [G(-1.8, 0.12, 0), G(1.8, 0.12, 0)], 0.015, "steel")


def kitbag():
    o = gcyl("body", 0.17, 0.62, (0, 0.17, 0), "cloth", axis="x", verts=20)
    for s in (-1, 1):
        gcyl(f"cap{s}", 0.175, 0.03, (s * 0.31, 0.17, 0), "navy", axis="x", verts=20)
    gbox("zip", (0.5, 0.012, 0.02), (0, 0.345, 0), "rubber", 0.004)
    for s in (-1, 1):
        tube(f"handle{s}", [G(-0.12, 0.3, s * 0.06), G(0, 0.42, s * 0.06), G(0.12, 0.3, s * 0.06)], 0.012, "navy")


def bottle():
    glathe("body", [(0.0001, 0), (0.035, 0), (0.037, 0.02), (0.036, 0.15), (0.03, 0.17), (0.02, 0.18), (0.0001, 0.18)], (0, 0, 0), "blue_plastic", segs=16)
    glathe("cap", [(0.0001, 0.18), (0.022, 0.18), (0.022, 0.2), (0.008, 0.215), (0.006, 0.23), (0.0001, 0.23)], (0, 0, 0), "white", segs=12)


def towel():
    gbox("fold", (0.42, 0.05, 0.3), (0, 0.025, 0), "cloth", 0.02)
    gbox("band", (0.42, 0.052, 0.04), (0, 0.025, 0.1), "white", 0.01)


def ballcart():
    for sx in (-1, 1):
        for sz in (-1, 1):
            gcyl(f"post{sx}{sz}", 0.012, 0.75, (sx * 0.4, 0.5, sz * 0.3), "steel", verts=8)
            gcyl(f"wheel{sx}{sz}", 0.05, 0.03, (sx * 0.4, 0.06, sz * 0.3), "rubber", axis="z", verts=12)
    for y in (0.15, 0.5, 0.86):
        for s in (-1, 1):
            tube(f"rx{y}{s}", [G(-0.4, y, s * 0.3), G(0.4, y, s * 0.3)], 0.008, "steel")
            tube(f"rz{y}{s}", [G(s * 0.4, y, -0.3), G(s * 0.4, y, 0.3)], 0.008, "steel")
    for k in range(5):
        tube(f"floor{k}", [G(-0.4 + k * 0.2, 0.15, -0.3), G(-0.4 + k * 0.2, 0.15, 0.3)], 0.006, "steel")
    tube("handle", [G(-0.4, 0.86, -0.3), G(-0.55, 1.0, -0.3), G(-0.55, 1.0, 0.3), G(-0.4, 0.86, 0.3)], 0.012, "rubber")


def cooler():
    gbox("stand", (0.34, 0.8, 0.34), (0, 0.4, 0), "white", 0.02)
    gbox("tray", (0.3, 0.03, 0.12), (0, 0.62, 0.18), "rubber", 0.005)
    for s in (-1, 1):
        gbox(f"tap{s}", (0.04, 0.05, 0.05), (s * 0.07, 0.72, 0.18), "blue_plastic" if s < 0 else "red", 0.008)
    glathe("jug", [(0.06, 0.8), (0.15, 0.84), (0.16, 1.0), (0.15, 1.15), (0.1, 1.24), (0.05, 1.27), (0.0001, 1.27)], (0, 0, 0), "water", segs=24)
    for k in range(6):
        glathe(f"cup{k}", [(0.0001, 0), (0.025, 0), (0.035, 0.08), (0.0001, 0.08)], (0.26, 0.8 + k * 0.02, 0), "paper", segs=10)
    gcyl("cuptube", 0.04, 0.3, (0.26, 0.95, 0), "white", verts=12)


def scoretable():
    gbox("top", (1.6, 0.04, 0.6), (0, 0.76, 0), "maple", 0.01)
    gbox("skirt", (1.6, 0.6, 0.02), (0, 0.45, 0.29), "navy", 0.01)
    for sx in (-1, 1):
        for sz in (-1, 1):
            gcyl(f"leg{sx}{sz}", 0.02, 0.74, (sx * 0.75, 0.37, sz * 0.26), "steel", verts=8)
    # the flip board: a stand with two number flaps per team
    gbox("flipback", (0.5, 0.3, 0.05), (0.3, 0.93, -0.1), "rubber", 0.01, rot=(0.2, 0, 0))
    for k in range(4):
        gbox(f"flap{k}", (0.1, 0.16, 0.01), (0.14 + k * 0.11, 0.94, -0.07), "white", 0.004, rot=(0.2, 0, 0))
    gbox("clip", (0.24, 0.01, 0.32), (-0.4, 0.785, 0.05), "maple", 0.004)
    gbox("sheet", (0.21, 0.012, 0.28), (-0.4, 0.79, 0.06), "paper", 0.002)
    sphere("whistle", G(-0.05, 0.8, 0.1), 0.025, "steel", scale=(1.5, 1, 1), segs=10)
    tube("lanyard", [G(-0.02, 0.79, 0.1), G(0.1, 0.785, 0.2), G(0.0, 0.783, 0.24), G(-0.08, 0.79, 0.14)], 0.004, "red")


def ball():
    sphere("rubber", (0, 0, 0), 1.0, "white", segs=40)
    for k, rot in enumerate(((0, 0, 0), (math.pi / 2, 0, 0), (0, math.pi / 2, 0))):
        torus(f"seam{k}", (0, 0, 0), 1.0, 0.035, "seam", rot=rot)


def boardfoot():
    gbox("base", (0.7, 0.06, 0.16), (0, 0.03, 0), "steel", 0.015)
    for s in (-1, 1):
        gcyl(f"castor{s}", 0.04, 0.04, (s * 0.3, 0.04, 0.1), "rubber", axis="x", verts=10)
    gbox("bracket", (0.24, 0.18, 0.04), (0, 0.15, 0), "steel", 0.01)


def pennant():
    pts = [G(-2 + k * 0.4, -0.12 * math.sin(math.pi * k / 10), 0) for k in range(11)]
    tube("string", pts, 0.006, "rubber")
    for k in range(5):
        x = -1.6 + k * 0.8; y = -0.12 * math.sin(math.pi * (x + 2) / 4)
        import bmesh
        bm = bmesh.new()
        v = [bm.verts.new(G(x - 0.17, y, 0)), bm.verts.new(G(x + 0.17, y, 0)), bm.verts.new(G(x, y - 0.4, 0))]
        bm.faces.new(v); bm.faces.new(list(reversed([bm.verts.new(G(x - 0.17, y, 0.002)), bm.verts.new(G(x + 0.17, y, 0.002)), bm.verts.new(G(x, y - 0.4, 0.002))])))
        me = C.bpy.data.meshes.new(f"flag{k}"); bm.to_mesh(me); bm.free()
        F.put(C.link(C.bpy.data.objects.new(f"flag{k}", me)), "cloth", 30)


BUILDS = {"bench": bench, "kitbag": kitbag, "bottle": bottle, "towel": towel, "ballcart": ballcart, "cooler": cooler,
          "scoretable": scoretable, "ball": ball, "boardfoot": boardfoot, "pennant": pennant}

if __name__ == "__main__" or True:
    a = C.args()
    only = a[a.index("--only") + 1].split(",") if "--only" in a else None
    out = C.out_dir()
    for n, fn in BUILDS.items():
        if only and n not in only:
            continue
        C.reset(); mats(); F.PARTS.clear()
        fn()
        C.export_glb(F.PARTS, os.path.join(out, f"{n}.glb"))
