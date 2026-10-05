# The Flight's island landmarks (the 3D polish pass, 2026-10-05), in GAME coordinates through fowl_props' G() helpers:
#     lighthouse   a 28 m tapering round tower (origin at its foot): white with red bands, a door, small windows, a
#                  railed gallery, a glazed lantern room (the game lights the lamp inside) and a red cap with a vane
#     wreckhull    a galleon's hull lying on the sand, 44 m along z (half 6 x 2.5 x 22 as the island's prop): planked
#                  sides, a broken gap in the port side showing the ribs, a raised stern, a bowsprit stump
#     deckhouse    the captain's cabin: a planked house with stern windows and a little roof (half 4 x 1.4 x 3.5)
#     blender -b --factory-startup -P tools/artgen/flight_isles_art.py -- --out assets/flight/isles
import os
import sys
import math
os.environ["ARTGEN_IMPORT"] = "1"
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import fowl_props as F
from fowl_props import G, gbox, gcyl, glathe, sphere, tube, torus, mat
C = F.C
import bmesh


def mats():
    F.setup()
    mat("whitewash", (0.88, 0.87, 0.82), 0.7)
    mat("bandred", (0.62, 0.1, 0.08), 0.6)
    mat("stone", (0.45, 0.43, 0.4), 0.9)
    mat("glassy", (0.85, 0.9, 0.7), 0.05)
    mat("hullwood", (0.3, 0.2, 0.12), 0.8)
    mat("hullwood_dk", (0.17, 0.11, 0.07), 0.85)
    mat("weed", (0.22, 0.32, 0.14), 0.9)
    mat("barnacle", (0.7, 0.68, 0.6), 0.9)


def lighthouse():
    prof = [(3.2, 0), (3.2, 1.2), (2.9, 1.4)]
    for k in range(1, 9):
        y = 1.4 + k * 3.0
        prof.append((2.9 - 0.12 * k, y))
    o = glathe("tower", prof, (0, 0, 0), "whitewash", segs=32)
    for k in (1, 3, 5, 7):   # red bands
        y0 = 1.4 + k * 3.0; r0 = 2.9 - 0.12 * k + 0.03; r1 = 2.9 - 0.12 * (k + 1) + 0.03
        glathe(f"band{k}", [(r0, y0), (r1, y0 + 3.0)], (0, 0, 0), "bandred", segs=32)
    glathe("plinth", [(3.6, 0), (3.6, 1.0), (3.25, 1.2)], (0, 0, 0), "stone", segs=32)
    gbox("door", (1.0, 2.0, 0.3), (0, 2.2, 3.15), "hullwood_dk", 0.03)
    for k, y in enumerate((8, 14, 20)):
        a = k * 2.1
        r = 2.9 - 0.12 * ((y - 1.4) / 3.0) + 0.02
        gbox(f"win{k}", (0.5, 0.8, 0.2), (r * math.sin(a), y, r * math.cos(a)), "hullwood_dk", 0.02)
    top = 25.4
    glathe("gallery", [(2.4, top), (3.3, top + 0.1), (3.3, top + 0.4), (2.2, top + 0.45)], (0, 0, 0), "stone", segs=32)
    for k in range(24):
        a = k * 2 * math.pi / 24
        gcyl(f"rail{k}", 0.04, 1.0, (3.15 * math.cos(a), top + 0.9, 3.15 * math.sin(a)), "iron", verts=6)
    torus("railtop", G(0, top + 1.4, 0), 3.15, 0.05, "iron")
    glathe("lantern", [(1.7, top + 0.4), (1.7, top + 2.8)], (0, 0, 0), "glassy", segs=16)
    for k in range(8):
        a = k * math.pi / 4
        gcyl(f"mullion{k}", 0.06, 2.4, (1.72 * math.cos(a), top + 1.6, 1.72 * math.sin(a)), "iron", verts=6)
    glathe("cap", [(2.0, top + 2.8), (1.9, top + 3.1), (0.6, top + 4.2), (0.15, top + 4.5), (0.0001, top + 4.6)], (0, 0, 0), "bandred", segs=24)
    gcyl("vane", 0.04, 1.4, (0, top + 5.2, 0), "iron", verts=6)
    gbox("vaneflag", (0.05, 0.25, 0.7), (0, top + 5.6, 0.3), "iron", 0.01)


def hull_ring(z, w, h, sheer):
    """A cross-section of the hull at z: a rounded U, half-width w, depth h, deck at sheer."""
    pts = []
    for k in range(9):
        a = math.pi * k / 8
        pts.append((-math.cos(a) * w, sheer - math.sin(a) * h))
    return pts


def wreckhull():
    L = 22.0
    stations = []
    for k in range(13):
        u = k / 12.0
        z = -L + 2 * L * u
        w = 6.0 * math.sin(math.pi * min(1.0, max(0.0, 0.08 + 0.92 * u)) ** 0.8) + 0.3
        h = 4.2 * (0.5 + 0.5 * math.sin(math.pi * u)) + 0.4
        sheer = 2.5 + 1.6 * (1 - u) ** 3 + 0.6 * u ** 4
        stations.append((z, w, h, sheer))
    bm = bmesh.new(); rings = []
    for (z, w, h, sh) in stations:
        rings.append([bm.verts.new(G(x, y, z)) for (x, y) in hull_ring(z, w, h, sh)])
    for i in range(len(rings) - 1):
        for j in range(8):
            if i in (5, 6, 7) and j in (1, 2):   # (a hole stove in her port side: the ribs show)
                continue
            bm.faces.new((rings[i][j], rings[i + 1][j], rings[i + 1][j + 1], rings[i][j + 1]))
    me = C.bpy.data.meshes.new("hull"); bm.to_mesh(me); bm.free()
    o = C.link(C.bpy.data.objects.new("hull", me))
    mod = o.modifiers.new("sol", "SOLIDIFY"); mod.thickness = 0.25
    F.put(o, "hullwood", 30)
    for i in (5, 6, 7, 8):   # the ribs across the hole
        z, w, h, sh = stations[i][0] + 1.8, stations[i][1], stations[i][2], stations[i][3]
        pts = [G(x * 0.97, y, z) for (x, y) in hull_ring(z, w, h, sh)[:4]]
        tube(f"rib{i}", pts, 0.18, "hullwood_dk")
    # wales (dark planks along her sides), a weed line low down, barnacles
    for s in (-1, 1):
        pts = [G(s * (st[1] + 0.12), st[3] - 0.6, st[0]) for st in stations[1:-1]]
        tube(f"wale{s}", pts, 0.15, "hullwood_dk")
        pts = [G(s * (st[1] * 0.92 + 0.1), st[3] - st[2] * 0.75, st[0]) for st in stations[1:-1]]
        tube(f"weed{s}", pts, 0.3, "weed")
    for k in range(18):
        st = stations[1 + k % 11]; s = 1 if k % 2 else -1
        sphere(f"barn{k}", G(s * st[1] * 0.85, st[3] - st[2] * 0.6 - (k % 3) * 0.3, st[0] + (k % 5) * 0.6), 0.25, "barnacle", segs=8)
    # the deck's beams across the open top, the bowsprit stump, the stern's rail
    for i in range(2, 11, 2):
        z, w, h, sh = stations[i]
        gbox(f"beam{i}", (w * 1.9, 0.25, 0.3), (0, sh - 0.2, z), "hullwood_dk", 0.03)
    tube("bowsprit", [G(0, stations[-1][3], L - 1), G(0, stations[-1][3] + 2.0, L + 4.5)], 0.3, "hullwood")
    for s in (-1, 1):
        tube(f"stern{s}", [G(s * 3.5, stations[0][3] + 1.0, -L + 0.5), G(s * 3.0, stations[0][3] + 1.0, -L + 6)], 0.12, "hullwood_dk")


def deckhouse():
    gbox("walls", (8, 2.6, 7), (0, 1.3, 0), "hullwood", 0.05)
    for k in range(9):
        gbox(f"plank{k}", (8.04, 0.04, 7.04), (0, 0.25 + k * 0.28, 0), "hullwood_dk", 0.0)
    gbox("roof", (8.6, 0.3, 7.6), (0, 2.75, 0), "hullwood_dk", 0.05)
    for k in range(3):
        gbox(f"win{k}", (1.0, 0.9, 0.1), (-2.5 + k * 2.5, 1.5, -3.52), "glassy", 0.02)
        gbox(f"winf{k}", (1.2, 1.1, 0.08), (-2.5 + k * 2.5, 1.5, -3.5), "gold", 0.02)
    gbox("door", (1.2, 2.0, 0.1), (0, 1.0, 3.52), "hullwood_dk", 0.02)


BUILDS = {"lighthouse": lighthouse, "wreckhull": wreckhull, "deckhouse": deckhouse}

a = C.args()
only = a[a.index("--only") + 1].split(",") if "--only" in a else None
out = C.out_dir()
for n, fn in BUILDS.items():
    if only and n not in only:
        continue
    C.reset(); mats(); F.PARTS.clear()
    fn()
    C.export_glb(F.PARTS, os.path.join(out, f"{n}.glb"))
