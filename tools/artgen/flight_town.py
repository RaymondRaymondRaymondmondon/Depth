# The Flight's towns and islands (the user: "the city/town needs to be developed to be more than geometric boxes and
# feel like a small city/town with roads, parking lots, houses, sometimes people walking around"; "environmental flora,
# developing the trees more"): houses, the church tower, the pier, boats' and townsfolk's things, cars, lamps, benches,
# a market stall, fences, trees, bushes, flowers and rocks. Low-poly and bright for the Flight's daylight. Each about
# its base at the origin in the game frame (x, y up, z), sized to the core's props where it stands in for one:
#     house_a / house_b   walls 8 x 5 x 10 (x by z) from y 0, a gabled roof along z to y 7.6
#     church              a tower 8 x 24 x 8 from y 0, a belfry and a spire to y 33
#     pier                a deck 4 x 32 along z at y 0.5, on posts
#     woodpile            logs, 6 x 2 x 4
#     blender -b --factory-startup -P tools/artgen/flight_town.py -- --out assets/flight/town
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
    for k, rgb, r, m in (("plaster", (0.86, 0.8, 0.68), 0.85, 0.0), ("plaster2", (0.62, 0.74, 0.8), 0.85, 0.0), ("plaster3", (0.9, 0.72, 0.6), 0.85, 0.0),
                         ("tile", (0.62, 0.2, 0.12), 0.7, 0.0), ("tile2", (0.24, 0.3, 0.38), 0.6, 0.0), ("trim", (0.95, 0.93, 0.88), 0.6, 0.0),
                         ("window", (0.18, 0.32, 0.42), 0.15, 0.0), ("door", (0.3, 0.18, 0.1), 0.6, 0.0), ("stone2", (0.62, 0.6, 0.55), 0.85, 0.0),
                         ("chimney", (0.5, 0.26, 0.18), 0.8, 0.0), ("plank", (0.48, 0.36, 0.22), 0.85, 0.0), ("post", (0.3, 0.22, 0.14), 0.9, 0.0),
                         ("carred", (0.62, 0.08, 0.06), 0.3, 0.3), ("carblue", (0.08, 0.24, 0.5), 0.3, 0.3), ("chrome", (0.75, 0.76, 0.78), 0.2, 1.0),
                         ("tyre", (0.05, 0.05, 0.05), 0.7, 0.0), ("lampglass", (0.95, 0.9, 0.7), 0.2, 0.0), ("ironb", (0.1, 0.1, 0.11), 0.5, 0.6),
                         ("awning", (0.75, 0.15, 0.12), 0.8, 0.0), ("awning2", (0.92, 0.9, 0.85), 0.8, 0.0), ("bark2", (0.32, 0.22, 0.14), 0.9, 0.0),
                         ("leafA", (0.22, 0.45, 0.16), 0.7, 0.0), ("leafB", (0.3, 0.55, 0.2), 0.7, 0.0), ("leafC", (0.16, 0.36, 0.14), 0.7, 0.0),
                         ("petal1", (0.9, 0.3, 0.35), 0.6, 0.0), ("petal2", (0.95, 0.85, 0.3), 0.6, 0.0), ("petal3", (0.6, 0.45, 0.85), 0.6, 0.0),
                         ("rockf", (0.52, 0.5, 0.46), 0.85, 0.0), ("fence", (0.88, 0.86, 0.8), 0.7, 0.0), ("fruit", (0.85, 0.5, 0.1), 0.6, 0.0)):
        B.MATS[k] = B.flat(k, rgb, r, m)


def gable_roof(w, d, y0, rise, mat, over=0.45):
    # a pitched roof along z: two slopes, the gable ends filled
    hw, hd = w / 2 + over, d / 2 + over
    quads([(-hw, y0, -hd), (0, y0 + rise, -hd), (0, y0 + rise, hd), (-hw, y0, hd)], [(0, 1, 2, 3)], mat, "roofL")
    quads([(hw, y0, -hd), (hw, y0, hd), (0, y0 + rise, hd), (0, y0 + rise, -hd)], [(0, 1, 2, 3)], mat, "roofR")
    for z in (-hd, hd):   # (the slopes have a thickness: an edge board)
        rod([(-hw, y0 - 0.05, z), (0, y0 + rise - 0.05, z), (hw, y0 - 0.05, z)], 0.08, "trim", 4, caps=False)
    for s in (-1, 1):   # the gable ends' plaster
        quads([(-w / 2, y0, s * d / 2), (w / 2, y0, s * d / 2), (0, y0 + rise - 0.1, s * d / 2)], [(0, 1, 2)], "plaster", "gable")
    rod([(0, y0 + rise + 0.05, -hd), (0, y0 + rise + 0.05, hd)], 0.12, mat, 6)   # the ridge


def windows_on(w, d, h, floors=1):
    for f in range(floors):
        y = 1.5 + f * 2.2
        for s in (-1, 1):
            for z in np.linspace(-d / 2 + 1.6, d / 2 - 1.6, 3):
                box((s * (w / 2 + 0.02), y, z), (0.04, 0.55, 0.42), "window", 0.0)
                box((s * (w / 2 + 0.05), y, z), (0.03, 0.62, 0.05), "trim", 0.0)
                box((s * (w / 2 + 0.05), y - 0.62, z), (0.08, 0.05, 0.5), "trim", 0.0)
                for zz in (z - 0.62, z + 0.62):   # (shutters)
                    box((s * (w / 2 + 0.06), y, zz), (0.03, 0.58, 0.18), "tile2", 0.0)


def house(style):
    W, D, H = 8.0, 10.0, 5.0
    wall = "plaster" if style == 0 else "plaster2"
    box((0, H / 2, 0), (W / 2, H / 2, D / 2), wall, 0.03)
    box((0, 0.25, 0), (W / 2 + 0.1, 0.25, D / 2 + 0.1), "stone2", 0.02)   # (a stone footing)
    gable_roof(W, D, H, 2.6, "tile" if style == 0 else "tile2")
    windows_on(W, D, H, 2)
    # the door in the front gable (+z), with a step and a lamp over it; a window above
    box((0, 1.1, D / 2 + 0.03), (0.6, 1.1, 0.05), "door", 0.01)
    box((0, 2.3, D / 2 + 0.06), (0.75, 0.1, 0.08), "trim", 0.01)
    box((0, 0.12, D / 2 + 0.5), (0.9, 0.12, 0.45), "stone2", 0.01)
    box((0, 3.7, D / 2 + 0.03), (0.5, 0.5, 0.04), "window", 0.0)
    # a chimney through the roof
    box((2.0, H + 2.2, -2.5), (0.45, 1.6, 0.45), "chimney", 0.02)
    box((2.0, H + 3.85, -2.5), (0.55, 0.08, 0.55), "stone2", 0.01)
    if style == 1:   # a little porch roof and a window box of flowers
        box((0, 2.7, D / 2 + 0.7), (1.2, 0.06, 0.75), "tile2", 0.01)
        for x in (-1.0, 1.0):
            cyl((x, 0, D / 2 + 1.35), (x, 2.65, D / 2 + 1.35), 0.07, "trim", 8)
        for s in (-1, 1):
            box((s * (W / 2 + 0.25), 1.0, 0), (0.18, 0.12, 0.5), "plank", 0.01)
            for k in range(4):
                blob((s * (W / 2 + 0.28), 1.18, -0.35 + k * 0.23), (0.09, 0.08, 0.09), "petal1" if k % 2 else "petal2", 10 + k, 0.2, 8)


def church():
    box((0, 12, 0), (4, 12, 4), "stone2", 0.05)
    for y in (6, 12, 18):
        box((0, y, 0), (4.08, 0.12, 4.08), "trim", 0.01)
    for s in (-1, 1):   # tall arched windows up its faces, the clock on the front
        for ax in ("x", "z"):
            c = (s * 4.03, 15, 0) if ax == "x" else (0, 15, s * 4.03)
            hs = (0.04, 1.6, 0.5) if ax == "x" else (0.5, 1.6, 0.04)
            box(c, hs, "window", 0.0)
    disc((0, 20.5, 4.05), 'z', 1.2, 0.08, "trim", 24)
    disc((0, 20.5, 4.12), 'z', 1.0, 0.04, "lampglass", 24)
    rod([(0, 20.5, 4.16), (0, 21.3, 4.16)], 0.05, "ironb", 4)
    rod([(0, 20.5, 4.16), (0.55, 20.5, 4.16)], 0.05, "ironb", 4)
    # the belfry: four open arches, a bell inside
    for sx in (-1, 1):
        for sz in (-1, 1):
            box((sx * 3.4, 26, sz * 3.4), (0.6, 2, 0.6), "stone2", 0.03)
    box((0, 28.2, 0), (4.1, 0.25, 4.1), "trim", 0.02)
    lathe((0, 0, 0), [(0.0001, 24.6), (0.9, 24.6), (0.8, 25.0), (0.55, 25.9), (0.0001, 26.0)], "brass", 16)
    # the spire
    lathe((0, 0, 0), [(3.8, 28.4), (0.08, 33.5), (0.0001, 33.6)], "tile2", 8)
    rod([(0, 33.5, 0), (0, 35, 0)], 0.06, "brass", 4)
    rod([(-0.5, 34.4, 0), (0.5, 34.4, 0)], 0.05, "brass", 4)


def pier():
    for k in range(32):   # planks across, with a gap now and then
        box((0, 0.5, -15.75 + k * 1.0), (2.0, 0.06, 0.45), "plank", 0.01)
    for s in (-1, 1):
        box((s * 1.95, 0.42, 0), (0.08, 0.06, 16), "post", 0.0)
        for k in range(9):
            cyl((s * 1.9, -5, -16 + k * 4.0), (s * 1.9, 1.2, -16 + k * 4.0), 0.16, "post", 8)
    for k in range(5):   # bollards and a coil of rope, a lamp at the end
        cyl((1.7, 0.56, -12 + k * 6.0), (1.7, 0.85, -12 + k * 6.0), 0.12, "ironb", 8)
    cyl((-1.7, 0.56, 15.2), (-1.7, 3.2, 15.2), 0.06, "ironb", 6)
    blob((-1.7, 3.3, 15.2), (0.18, 0.22, 0.18), "lampglass", 1, 0.0, 10)


def car(style):
    # a small rounded old-fashioned car: a curved body, a cabin with windows, mudguards, four wheels
    body = "carred" if style == 0 else "carblue"
    blob((0, 0.62, 0), (0.85, 0.32, 2.0), body, 1, 0.0, 24)
    blob((0, 1.05, -0.25), (0.75, 0.34, 0.9), body, 2, 0.0, 20)
    for s in (-1, 1):
        box((s * 0.74, 1.08, -0.25), (0.04, 0.2, 0.62), "window", 0.0)
    box((0, 1.08, 0.62), (0.62, 0.2, 0.04), "window", 0.0)
    for x in (-0.82, 0.82):
        for z in (-1.25, 1.3):
            disc((x, 0.38, z), 'x', 0.38, 0.22, "tyre", 18)
            disc((x * 1.02, 0.38, z), 'x', 0.2, 0.24, "chrome", 12)
            blob((x * 0.95, 0.72, z), (0.22, 0.12, 0.5), body, 5, 0.0, 12)
    for s in (-1, 1):
        blob((s * 0.55, 0.72, 1.95), (0.14, 0.14, 0.06), "lampglass", 6, 0.0, 10)
    box((0, 0.45, 2.02), (0.7, 0.08, 0.06), "chrome", 0.02)
    box((0, 0.45, -2.02), (0.7, 0.08, 0.06), "chrome", 0.02)


def lamppost():
    cyl((0, 0, 0), (0, 0.3, 0), 0.18, "ironb", 10)
    cyl((0, 0.3, 0), (0, 4.2, 0), 0.07, "ironb", 8)
    rod([(0, 4.1, 0), (0.25, 4.4, 0), (0.6, 4.35, 0)], 0.05, "ironb", 4, caps=False)
    lathe((0.6, 0, 0), [(0.0001, 3.9), (0.18, 3.95), (0.2, 4.2), (0.12, 4.32), (0.0001, 4.34)], "lampglass", 10)
    lathe((0.6, 0, 0), [(0.22, 4.28), (0.05, 4.45), (0.0001, 4.46)], "ironb", 10)


def bench():
    for k in range(3):
        box((0, 0.48, -0.15 + k * 0.15), (0.9, 0.03, 0.06), "plank", 0.005)
    for k in range(2):
        box((0, 0.75 + k * 0.15, -0.3), (0.9, 0.05, 0.03), "plank", 0.005)
    for x in (-0.75, 0.75):
        box((x, 0.24, 0), (0.04, 0.24, 0.22), "ironb", 0.01)
        box((x, 0.65, -0.3), (0.04, 0.35, 0.03), "ironb", 0.01)


def stall():
    # a market stall: a counter, poles, a striped awning, crates of fish and fruit
    box((0, 0.5, 0), (1.4, 0.5, 0.6), "plank", 0.01)
    for x in (-1.3, 1.3):
        for z in (-0.55, 0.55):
            cyl((x, 0, z), (x, 2.4, z), 0.05, "post", 6)
    for k in range(6):
        box((-1.25 + k * 0.5, 2.45 - 0.05 * (k % 2), 0.1), (0.25, 0.04, 0.85), "awning" if k % 2 else "awning2", 0.01)
    for k in range(3):
        box((-0.9 + k * 0.9, 1.08, 0.1), (0.35, 0.08, 0.35), "plank", 0.01)
        for j in range(4):
            blob((-0.9 + k * 0.9 + (j % 2 - 0.5) * 0.3, 1.2, 0.1 + (j // 2 - 0.5) * 0.3), (0.1, 0.07, 0.1), "fruit" if k != 1 else "chrome", 20 + j + k, 0.1, 8)


def fence():
    for k in range(7):
        box((0, 0.5, -1.5 + k * 0.5), (0.04, 0.5, 0.06), "fence", 0.005)
        quads([(0.04, 1.0, -1.56 + k * 0.5), (0.04, 1.0, -1.44 + k * 0.5), (0.04, 1.1, -1.5 + k * 0.5)], [(0, 1, 2)], "fence", "tip")
    for y in (0.3, 0.75):
        box((0.06, y, 0), (0.03, 0.05, 1.6), "fence", 0.005)


def tree(kind):
    # a broadleaf tree: a trunk forking into boughs, a crown of overlapping leafy lobes (kind 1: taller and darker)
    h = 3.2 if kind == 0 else 4.2
    rod([(0, 0, 0), (0.1, h * 0.55, 0.05), (0.05, h, -0.05)], 0.26 if kind == 0 else 0.3, "bark2", 10)
    for a, l in ((0.4, 1.6), (2.4, 1.4), (4.2, 1.5)):
        rod([(0.1, h * 0.6, 0), (math.cos(a) * l, h * 0.9, math.sin(a) * l)], 0.11, "bark2", 6)
    rng = np.random.default_rng(3 + kind)
    lobes = 9 if kind == 0 else 12
    for k in range(lobes):
        a = k * 2.399; r = 0.5 + rng.random() * 1.4
        y = h + 0.6 + rng.random() * (1.8 if kind == 0 else 2.8)
        s = 1.0 + rng.random() * 0.7
        blob((math.cos(a) * r, y, math.sin(a) * r), (s, s * 0.85, s), ("leafA", "leafB", "leafC")[k % 3] if kind == 0 else ("leafC", "leafA")[k % 2], 30 + k, 0.18, 12)


def pine():
    rod([(0, 0, 0), (0, 7.5, 0)], 0.25, "bark2", 8)
    for k in range(5):
        y = 1.8 + k * 1.25; r = 2.4 - k * 0.42
        lathe((0, 0, 0), [(r, y), (r * 0.55, y + 0.7), (0.0001, y + 1.5)], "leafC", 10)


def bush():
    rng = np.random.default_rng(8)
    for k in range(6):
        a = k * 1.05
        blob((math.cos(a) * 0.45 * rng.random(), 0.45 + 0.25 * rng.random(), math.sin(a) * 0.45 * rng.random()), (0.55, 0.45, 0.55), ("leafA", "leafB")[k % 2], 50 + k, 0.2, 12)


def flowers():
    rng = np.random.default_rng(12)
    for k in range(18):
        x, z = (rng.random() - 0.5) * 1.6, (rng.random() - 0.5) * 1.6
        h = 0.25 + 0.3 * rng.random()
        rod([(x, 0, z), (x, h, z)], 0.012, "leafB", 3)
        blob((x, h + 0.04, z), (0.07, 0.05, 0.07), ("petal1", "petal2", "petal3")[k % 3], 60 + k, 0.15, 8)
    for k in range(8):
        blob(((rng.random() - 0.5) * 1.5, 0.08, (rng.random() - 0.5) * 1.5), (0.18, 0.08, 0.18), "leafA", 80 + k, 0.2, 8)


def rocks():
    for k, (x, z, r) in enumerate(((0, 0, 0.9), (1.1, 0.5, 0.5), (-0.8, 0.7, 0.4))):
        o = boulder_mesh("r", r, "rockf", 90 + k, 0.7)
        o.location = B.G(x, 0.0, z)


def grass():
    rng = np.random.default_rng(15)
    for k in range(26):
        x, z = (rng.random() - 0.5) * 0.8, (rng.random() - 0.5) * 0.8
        h = 0.35 + 0.35 * rng.random(); lean = (rng.random() - 0.5) * 0.3
        quads([(x - 0.03, 0, z), (x + 0.03, 0, z), (x + lean, h, z + lean * 0.5)], [(0, 1, 2)], ("leafA", "leafB")[k % 2], "blade")


def woodpile():
    for row in range(3):
        for k in range(5 - row):
            cyl((-2.4 + row * 0.5 + k * 1.0, 0.35 + row * 0.62, -1.8), (-2.4 + row * 0.5 + k * 1.0, 0.35 + row * 0.62, 1.8), 0.34, "bark2", 10)
            disc((-2.4 + row * 0.5 + k * 1.0, 0.35 + row * 0.62, 1.82), 'z', 0.3, 0.02, "plank", 10)


PIECES = {"house_a": lambda: house(0), "house_b": lambda: house(1), "church": church, "pier": pier,
          "car_red": lambda: car(0), "car_blue": lambda: car(1), "lamppost": lamppost, "bench": bench, "stall": stall,
          "fence": fence, "tree": lambda: tree(0), "tree2": lambda: tree(1), "pine": pine, "bush": bush, "flowers": flowers,
          "rocks": rocks, "grass": grass, "woodpile": woodpile}

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
