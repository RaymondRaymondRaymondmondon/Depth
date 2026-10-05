# Red Tide's decorations (the user: "decorations make these games feel more alive ... couches, chairs, beds, purely
# environmental flora ... come up with things specific to each level"): furniture and fittings for the Sunken Ship and
# the Void's station, things left lying in the Cave, the Reef and Atlantis, and plants and growths that are only there to
# make a place feel lived in (no part in the food web). Each stands on its base at the origin, front toward +x, in the
# game frame (x forward, y up, z to starboard), baked like the Trawl's props.
#     blender -b --factory-startup -P tools/artgen/rt_decor.py -- --out assets/redtide/decor
import os
import sys
import math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ["ARTGEN_IMPORT_TRAWLPROPS"] = "1"
import numpy as np
import common as C
import props as P
import boat as B
from boat import box, cyl, rod, disc, lathe, quads
from props import blob, boulder_mesh


def mats():
    for k, rgb, r, m in (("velvet", (0.32, 0.06, 0.07), 0.9, 0.0), ("velvet2", (0.08, 0.2, 0.16), 0.9, 0.0), ("mahog", (0.2, 0.08, 0.04), 0.45, 0.0),
                         ("gilt", (0.6, 0.45, 0.16), 0.35, 1.0), ("linen", (0.72, 0.68, 0.6), 0.9, 0.0), ("blanket", (0.25, 0.2, 0.32), 0.95, 0.0),
                         ("rug", (0.42, 0.12, 0.08), 0.95, 0.0), ("rug2", (0.7, 0.55, 0.25), 0.95, 0.0), ("canvasp", (0.25, 0.3, 0.26), 0.7, 0.0),
                         ("face", (0.6, 0.48, 0.38), 0.7, 0.0), ("bottle", (0.08, 0.22, 0.12), 0.1, 0.0), ("bottle2", (0.3, 0.16, 0.06), 0.1, 0.0),
                         ("book1", (0.4, 0.1, 0.08), 0.8, 0.0), ("book2", (0.12, 0.2, 0.35), 0.8, 0.0), ("book3", (0.36, 0.3, 0.12), 0.8, 0.0),
                         ("shade", (0.75, 0.62, 0.38), 0.8, 0.0), ("rust", (0.35, 0.16, 0.08), 0.85, 0.3), ("bone", (0.78, 0.74, 0.62), 0.6, 0.0),
                         ("terracotta", (0.55, 0.27, 0.14), 0.8, 0.0), ("mosaic1", (0.15, 0.35, 0.55), 0.4, 0.0), ("mosaic2", (0.7, 0.55, 0.2), 0.4, 0.0),
                         ("steel", (0.45, 0.47, 0.5), 0.35, 0.9), ("screen", (0.1, 0.35, 0.3), 0.2, 0.0), ("cable", (0.06, 0.06, 0.07), 0.6, 0.0),
                         ("paint_blue", (0.12, 0.2, 0.3), 0.6, 0.0), ("weedg", (0.12, 0.28, 0.1), 0.5, 0.0), ("weedr", (0.42, 0.12, 0.14), 0.5, 0.0),
                         ("barn", (0.62, 0.6, 0.55), 0.8, 0.0), ("tubew", (0.8, 0.75, 0.68), 0.6, 0.0), ("plume", (0.75, 0.2, 0.15), 0.5, 0.0),
                         ("shellp", (0.85, 0.7, 0.62), 0.5, 0.0), ("sand", (0.6, 0.55, 0.42), 0.95, 0.0)):
        B.MATS[k] = B.flat(k, rgb, r, m)


# ---------------------------------------------------------------- the Sunken Ship (and the Void's station): furniture
def sofa():
    # a buttoned chesterfield: rolled arms, a deep seat, a high back, turned feet
    box((0, 0.3, 0), (0.42, 0.14, 0.95), "velvet", 0.06)
    box((0.06, 0.48, 0), (0.34, 0.08, 0.86), "velvet", 0.07)
    box((-0.34, 0.62, 0), (0.1, 0.3, 0.95), "velvet", 0.07)
    for s in (-1, 1):
        cyl((-0.42, 0.62, s * 0.95), (0.42, 0.62, s * 0.95), 0.13, "velvet", 16)
        box((0, 0.42, s * 0.95), (0.42, 0.2, 0.1), "velvet", 0.05)
        for x in (-0.36, 0.36):
            lathe((x, 0, s * 0.85), [(0.0001, 0.0), (0.035, 0.0), (0.045, 0.08), (0.03, 0.16), (0.0001, 0.17)], "mahog", 10)
    for k in range(8):
        disc((-0.235, 0.7 + (k % 2) * 0.12, -0.75 + k * 0.21), 'x', 0.012, 0.01, "gilt", 8)


def chair():
    # a dining chair: turned legs, a padded seat, a carved back with a splat
    for x in (-0.18, 0.18):
        for z in (-0.18, 0.18):
            lathe((x, 0, z), [(0.0001, 0.0), (0.02, 0.0), (0.025, 0.2), (0.02, 0.42), (0.0001, 0.43)], "mahog", 8)
    box((0, 0.46, 0), (0.22, 0.04, 0.22), "velvet2", 0.02)
    for z in (-0.18, 0.18):
        cyl((-0.19, 0.46, z), (-0.21, 1.0, z), 0.02, "mahog", 8)
    box((-0.205, 0.95, 0), (0.025, 0.06, 0.2), "mahog", 0.01)
    box((-0.2, 0.72, 0), (0.015, 0.2, 0.06), "mahog", 0.01)


def bed():
    # a cabin bed: a mahogany frame, headboard and footboard, a mattress, the blanket half pulled back, a pillow
    box((0, 0.25, 0), (0.95, 0.06, 0.45), "mahog", 0.01)
    box((0.02, 0.37, 0), (0.92, 0.08, 0.42), "linen", 0.04)
    box((-0.95, 0.55, 0), (0.04, 0.4, 0.47), "mahog", 0.01)
    box((0.95, 0.42, 0), (0.04, 0.26, 0.47), "mahog", 0.01)
    for x in (-0.92, 0.92):
        for z in (-0.43, 0.43):
            cyl((x, 0, z), (x, 0.25, z), 0.035, "mahog", 8)
    blob((0.25, 0.46, 0), (0.65, 0.06, 0.44), "blanket", 2, 0.05, 20)
    blob((-0.36, 0.48, 0.05), (0.14, 0.04, 0.4), "blanket", 3, 0.15, 14)   # (the turned-back fold)
    blob((-0.72, 0.5, 0), (0.16, 0.07, 0.3), "linen", 4, 0.06, 16)


def rug():
    box((0, 0.008, 0), (1.2, 0.008, 0.8), "rug", 0.004)
    box((0, 0.01, 0), (1.0, 0.008, 0.6), "rug2", 0.004)
    box((0, 0.012, 0), (0.7, 0.008, 0.35), "rug", 0.004)
    for z in (-0.8, 0.8):
        for k in range(16):
            rod([(-1.15 + k * 0.153, 0.01, z), (-1.15 + k * 0.153, 0.005, z * 1.08)], 0.006, "rug2", 3, caps=False)


def portrait():
    # an oil portrait in a gilt frame (the frame's back against a wall at x 0, facing +x), hung at 1.6
    box((0.03, 1.6, 0), (0.025, 0.42, 0.33), "gilt", 0.02)
    box((0.055, 1.6, 0), (0.008, 0.36, 0.27), "canvasp", 0.0)
    blob((0.065, 1.66, 0), (0.008, 0.12, 0.09), "face", 1, 0.02, 12)
    blob((0.065, 1.42, 0), (0.008, 0.14, 0.2), "book2", 2, 0.05, 12)


def lamp():
    # a standard lamp: a weighted base, a brass stem, a fringed shade
    lathe((0, 0, 0), [(0.0001, 0.0), (0.18, 0.0), (0.16, 0.05), (0.04, 0.08), (0.0001, 0.08)], "gilt", 16)
    cyl((0, 0.08, 0), (0, 1.45, 0), 0.015, "gilt", 8)
    lathe((0, 0, 0), [(0.25, 1.38), (0.12, 1.65), (0.0001, 1.66)], "shade", 18)
    for k in range(20):
        a = k * 2 * math.pi / 20
        rod([(0.25 * math.cos(a), 1.38, 0.25 * math.sin(a)), (0.25 * math.cos(a), 1.32, 0.25 * math.sin(a))], 0.005, "gilt", 3, caps=False)


def bookshelf():
    # a shelved bookcase (its back against a wall at x 0) with rows of books, a few lying flat
    for y in (0.02, 0.5, 1.0, 1.5, 1.98):
        box((0.18, y, 0), (0.18, 0.02, 0.6), "mahog", 0.005)
    for s in (-1, 1):
        box((0.18, 1.0, s * 0.6), (0.18, 1.0, 0.02), "mahog", 0.005)
    box((0.01, 1.0, 0), (0.01, 1.0, 0.6), "mahog", 0.0)
    rng = np.random.default_rng(7)
    for y in (0.04, 0.52, 1.02, 1.52):
        z = -0.56
        while z < 0.52:
            t = 0.03 + 0.03 * rng.random(); hgt = 0.3 + 0.12 * rng.random()
            if rng.random() < 0.1:
                box((0.17, y + 0.04, z + 0.12), (0.12, 0.03, 0.12), "book3", 0.004); z += 0.26; continue
            box((0.17, y + hgt / 2, z + t), (0.12, hgt / 2, t), ("book1", "book2", "book3")[int(rng.random() * 3)], 0.003)
            z += 2 * t + 0.004


def bottles():
    rng = np.random.default_rng(5)
    for k in range(5):
        x, z = (rng.random() - 0.5) * 0.35, (rng.random() - 0.5) * 0.35
        m = "bottle" if k % 2 else "bottle2"
        if k == 4:   # one lying on its side
            o = cyl((x, 0.04, z), (x + 0.22, 0.04, z + 0.06), 0.04, m, 12)
            continue
        lathe((x, 0, z), [(0.0001, 0.0), (0.04, 0.0), (0.042, 0.18), (0.015, 0.25), (0.015, 0.3), (0.0001, 0.3)], m, 12)


def anchor():
    # an old stocked anchor lying on the bottom, its chain running off
    cyl((-0.9, 0.08, 0), (0.7, 0.08, 0), 0.06, "rust", 10)
    cyl((-0.85, 0.08, -0.6), (-0.85, 0.08, 0.6), 0.04, "rust", 8)
    pts = [(0.7, 0.08, -0.55), (0.85, 0.08, -0.3), (0.9, 0.08, 0.0), (0.85, 0.08, 0.3), (0.7, 0.08, 0.55)]
    rod(pts, 0.06, "rust", 8)
    for s in (-1, 1):
        blob((0.68, 0.08, s * 0.58), (0.08, 0.04, 0.12), "rust", 3 + s, 0.05, 10)
    disc((-1.0, 0.08, 0), 'z', 0.1, 0.03, "rust", 12, hole=0.06)
    for k in range(6):
        disc((-1.15 - k * 0.11, 0.05, 0.05 * math.sin(k)), 'z' if k % 2 else 'y', 0.06, 0.025, "rust", 10, hole=0.035)


def fishbones():
    # a fish skeleton on the sand: skull, spine, ribs, tail
    blob((0.35, 0.05, 0), (0.12, 0.06, 0.06), "bone", 1, 0.08, 12)
    rod([(0.25, 0.04, 0), (-0.35, 0.03, 0)], 0.012, "bone", 6)
    for k in range(9):
        x = 0.18 - k * 0.06
        for s in (-1, 1):
            rod([(x, 0.04, 0), (x - 0.02, 0.06, s * 0.1 * (1 - k / 12)), (x - 0.05, 0.02, s * 0.13 * (1 - k / 12))], 0.005, "bone", 3, caps=False)
    for s in (-1, 1):
        rod([(-0.35, 0.03, 0), (-0.48, 0.03, s * 0.09)], 0.01, "bone", 4)


def shells():
    rng = np.random.default_rng(11)
    for k in range(6):
        x, z = (rng.random() - 0.5) * 0.6, (rng.random() - 0.5) * 0.6
        if k % 2:
            lathe((x, 0, z), [(0.0001, 0.0), (0.06, 0.005), (0.05, 0.03), (0.0001, 0.035)], "shellp", 10)
        else:
            blob((x, 0.03, z), (0.06, 0.035, 0.05), "shellp", 20 + k, 0.1, 10)


def amphora():
    lathe((0, 0, 0), [(0.0001, 0.0), (0.06, 0.0), (0.04, 0.05), (0.18, 0.3), (0.2, 0.45), (0.14, 0.6), (0.07, 0.68), (0.07, 0.8), (0.09, 0.82), (0.0001, 0.82)], "terracotta", 18)
    for s in (-1, 1):
        rod([(0, 0.76, s * 0.07), (0, 0.78, s * 0.15), (0, 0.6, s * 0.16)], 0.018, "terracotta", 6, caps=False)


def amphora_fallen():
    amphora()
    for o in B.PARTS:
        o.rotation_euler = (math.radians(80), 0, math.radians(20))
        o.location = (0, 0, 0.18)
    for k in range(5):
        blob((0.5 + 0.1 * k, 0.02, 0.2 * math.sin(k * 2)), (0.07, 0.015, 0.05), "terracotta", 30 + k, 0.2, 8)


def mosaic():
    # a patch of floor mosaic: a ring of blue and gold tesserae round a sunburst, half buried in sand at one edge
    box((0, 0.01, 0), (0.9, 0.01, 0.9), "marble", 0.002)
    for k in range(40):
        a = k * 2 * math.pi / 40
        for r, m in ((0.75, "mosaic1"), (0.62, "mosaic2")):
            box((r * math.cos(a), 0.022, r * math.sin(a)), (0.04, 0.008, 0.04), m, 0.002, rot_y=a)
    for k in range(12):
        a = k * 2 * math.pi / 12
        quads([(0, 0.023, 0), (0.45 * math.cos(a - 0.12), 0.023, 0.45 * math.sin(a - 0.12)), (0.45 * math.cos(a + 0.12), 0.023, 0.45 * math.sin(a + 0.12))], [(0, 1, 2)], "mosaic2", "ray")
    blob((0.7, 0.02, 0.5), (0.45, 0.05, 0.4), "sand", 8, 0.2, 14)


def console():
    # the station's control console: a sloped desk of dials and lit screens on a steel plinth
    box((0.0, 0.45, 0), (0.35, 0.45, 0.7), "steel", 0.02)
    quads([(0.35, 0.9, -0.7), (0.35, 0.9, 0.7), (-0.05, 1.2, 0.7), (-0.05, 1.2, -0.7)], [(0, 1, 2, 3)], "steel", "desk")
    for k in range(3):
        quads([(0.3, 0.95, -0.55 + k * 0.4), (0.3, 0.95, -0.25 + k * 0.4), (0.04, 1.15, -0.25 + k * 0.4), (0.04, 1.15, -0.55 + k * 0.4)], [(0, 1, 2, 3)], "screen", "screen")
    for k in range(6):
        disc((0.36, 0.7, -0.5 + k * 0.2), 'x', 0.05, 0.02, "dial", 14)
    box((-0.1, 1.5, 0), (0.05, 0.3, 0.6), "steel", 0.01)


def lockers():
    for k in range(4):
        z = -0.75 + k * 0.5
        box((0.0, 0.95, z), (0.25, 0.95, 0.24), "paint_blue", 0.01)
        for y in (1.6, 1.68, 1.76):
            box((0.252, y, z), (0.004, 0.012, 0.15), "bulk", 0.0)
        box((0.255, 1.0, z + 0.17), (0.01, 0.06, 0.015), "steel", 0.003)


def cables():
    # a bundle of cables snaking along the floor
    for k in range(4):
        pts = [(-1.2 + i * 0.2, 0.03 + 0.012 * k, 0.12 * math.sin(i * 0.7 + k) + 0.05 * k) for i in range(13)]
        rod(pts, 0.025, "cable" if k % 2 else "rubber", 6)


# ---------------------------------------------------------------- growths: only there to make a place feel lived in
def weedtuft():
    rng = np.random.default_rng(3)
    for k in range(14):
        a = rng.random() * 6.283; r = rng.random() * 0.15
        x, z = r * math.cos(a), r * math.sin(a); h = 0.4 + rng.random() * 0.7
        lean = (rng.random() - 0.5) * 0.3
        vs, fs = [], []
        for i in range(6):
            u = i / 5; w = 0.035 * (1 - u * 0.8)
            px, py, pz = x + lean * u * u, h * u, z + 0.08 * math.sin(u * 4 + k)
            vs += [(px - w, py, pz), (px + w, py, pz)]
        for i in range(5):
            fs.append((2 * i, 2 * i + 1, 2 * i + 3, 2 * i + 2))
        quads(vs, fs, "weedg" if k % 4 else "weedr", "blade")


def barnacles():
    rng = np.random.default_rng(9)
    for k in range(22):
        x, z = (rng.random() - 0.5) * 0.7, (rng.random() - 0.5) * 0.7
        r = 0.025 + 0.03 * rng.random()
        lathe((x, 0, z), [(r * 1.2, 0.0), (r, r * 0.9), (r * 0.45, r * 1.2), (0.0001, r * 1.0)], "barn", 8)


def tubeworms():
    rng = np.random.default_rng(13)
    for k in range(9):
        x, z = (rng.random() - 0.5) * 0.4, (rng.random() - 0.5) * 0.4
        h = 0.25 + rng.random() * 0.35
        cyl((x, 0, z), (x + 0.03 * math.sin(k), h, z), 0.022, "tubew", 8)
        blob((x + 0.03 * math.sin(k), h + 0.03, z), (0.06, 0.04, 0.06), "plume", 40 + k, 0.4, 10)


def spongecluster():
    for k, (x, z, h, r) in enumerate(((0, 0, 0.5, 0.12), (0.18, 0.1, 0.35, 0.09), (-0.12, 0.15, 0.28, 0.08))):
        lathe((x, 0, z), [(r * 1.1, 0.0), (r, h * 0.5), (r * 1.15, h), (r * 0.8, h), (r * 0.7, h * 0.2), (0.0001, h * 0.2)], "terracotta" if k == 1 else "mosaic2", 12)


PIECES = {"sofa": sofa, "chair": chair, "bed": bed, "rug": rug, "portrait": portrait, "lamp": lamp, "bookshelf": bookshelf,
          "bottles": bottles, "anchor": anchor, "fishbones": fishbones, "shells": shells, "amphora": amphora,
          "amphora_fallen": amphora_fallen, "mosaic": mosaic, "console": console, "lockers": lockers, "cables": cables,
          "weedtuft": weedtuft, "barnacles": barnacles, "tubeworms": tubeworms, "spongecluster": spongecluster}

if __name__ == "__main__":
    out = C.out_dir()
    a = C.args()
    only = a[a.index("--only") + 1].split(",") if "--only" in a else None
    for name, fn in PIECES.items():
        if only and name not in only:
            continue
        P.reset(); P.land_mats(); P.clutter_mats(); mats()
        fn()
        B.finish(out, name + ".glb", lambda x, y, z, k: k)
