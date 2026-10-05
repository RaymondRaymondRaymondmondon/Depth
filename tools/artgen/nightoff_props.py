# A Night Off's furniture and things (the user: "In night off there is a lot of problems with the way it looks.
# Everything is still blocky"): the Sodden Gull's bar stools, tavern chairs, round and square tables, the bottles on the
# back shelf, beer mugs, a ship's lantern, the jukebox, the dartboard, the fireplace, the upright piano, the pool table,
# the bar counter (a 1 m section the game stretches), barrels, an armchair, a chaise, a wine rack and a bookcase. Each on
# its base at the origin, its front toward +x, in the game frame (x, y up, z).
#     blender -b --factory-startup -P tools/artgen/nightoff_props.py -- --out assets/nightoff/props
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
from props import blob


def mats():
    for k, rgb, r, m in (("oak", (0.36, 0.22, 0.12), 0.55, 0.0), ("darkwood", (0.18, 0.1, 0.06), 0.5, 0.0), ("redleather", (0.45, 0.08, 0.07), 0.45, 0.0),
                         ("green_felt", (0.06, 0.32, 0.16), 0.95, 0.0), ("chrome2", (0.75, 0.75, 0.78), 0.2, 1.0), ("brass2", (0.7, 0.52, 0.2), 0.3, 1.0),
                         ("glassg", (0.12, 0.38, 0.16), 0.08, 0.0), ("glassb", (0.4, 0.2, 0.06), 0.08, 0.0), ("glassc", (0.75, 0.78, 0.8), 0.05, 0.0),
                         ("glassr", (0.45, 0.06, 0.1), 0.08, 0.0), ("label2", (0.9, 0.85, 0.7), 0.7, 0.0), ("beer", (0.85, 0.55, 0.12), 0.2, 0.0),
                         ("foam", (0.95, 0.93, 0.86), 0.8, 0.0), ("stone3", (0.42, 0.38, 0.34), 0.9, 0.0), ("soot", (0.05, 0.04, 0.04), 0.95, 0.0),
                         ("ivory", (0.92, 0.9, 0.84), 0.4, 0.0), ("ebony", (0.04, 0.04, 0.05), 0.3, 0.0), ("cork2", (0.7, 0.55, 0.35), 0.9, 0.0),
                         ("jukeglow", (1.0, 0.55, 0.85), 0.3, 0.0), ("jukeglow2", (0.4, 0.8, 1.0), 0.3, 0.0), ("velvetp", (0.6, 0.42, 0.5), 0.9, 0.0),
                         ("dartred", (0.6, 0.08, 0.06), 0.8, 0.0), ("dartblack", (0.06, 0.06, 0.06), 0.8, 0.0), ("dartcream", (0.88, 0.8, 0.6), 0.8, 0.0),
                         ("lampg", (1.0, 0.82, 0.5), 0.3, 0.0)):
        B.MATS[k] = B.flat(k, rgb, r, m)


def stool():
    # a bar stool: a padded round seat, a chrome column, a foot ring, a weighted base
    lathe((0, 0, 0), [(0.0001, 0.0), (0.22, 0.0), (0.2, 0.03), (0.06, 0.05), (0.0001, 0.05)], "chrome2", 18)
    cyl((0, 0.04, 0), (0, 0.68, 0), 0.035, "chrome2", 10)
    disc((0, 0.3, 0), 'y', 0.18, 0.02, "chrome2", 18, hole=0.16)
    for k in range(3):
        a = k * 2 * math.pi / 3
        rod([(0, 0.3, 0), (0.17 * math.cos(a), 0.3, 0.17 * math.sin(a))], 0.01, "chrome2", 4)
    lathe((0, 0, 0), [(0.0001, 0.68), (0.2, 0.68), (0.21, 0.72), (0.19, 0.78), (0.0001, 0.79)], "redleather", 20)


def chair():
    # a tavern chair: turned legs with stretchers, a saddle seat, a spindle back with a curved top rail
    for x in (-0.18, 0.18):
        for z in (-0.18, 0.18):
            lathe((x, 0, z), [(0.0001, 0.0), (0.022, 0.0), (0.028, 0.15), (0.02, 0.3), (0.025, 0.44), (0.0001, 0.45)], "oak", 8)
    for z in (-0.18, 0.18):
        cyl((-0.18, 0.15, z), (0.18, 0.15, z), 0.012, "oak", 6)
    blob((0, 0.46, 0), (0.23, 0.03, 0.23), "oak", 1, 0.0, 18)
    for k in range(5):
        z = -0.16 + k * 0.08
        cyl((-0.19, 0.48, z), (-0.21, 0.92, z), 0.012, "oak", 6)
    rod([(-0.21, 0.94, -0.22), (-0.24, 0.96, -0.1), (-0.25, 0.97, 0), (-0.24, 0.96, 0.1), (-0.21, 0.94, 0.22)], 0.035, "oak", 6)


def table_round():
    lathe((0, 0, 0), [(0.0001, 0.76), (0.45, 0.76), (0.46, 0.78), (0.45, 0.8), (0.0001, 0.8)], "oak", 28)
    lathe((0, 0, 0), [(0.06, 0.08), (0.05, 0.4), (0.07, 0.76)], "darkwood", 12)
    for k in range(3):
        a = k * 2 * math.pi / 3
        rod([(0, 0.1, 0), (0.3 * math.cos(a), 0.02, 0.3 * math.sin(a))], 0.035, "darkwood", 6)


def table_sq():
    box((0, 0.775, 0), (0.5, 0.025, 0.5), "oak", 0.01)
    for x in (-0.42, 0.42):
        for z in (-0.42, 0.42):
            lathe((x, 0, z), [(0.0001, 0.0), (0.03, 0.0), (0.04, 0.2), (0.03, 0.5), (0.035, 0.75), (0.0001, 0.76)], "darkwood", 8)
    for s in (-1, 1):
        box((s * 0.44, 0.7, 0), (0.015, 0.05, 0.42), "darkwood", 0.005)
        box((0, 0.7, s * 0.44), (0.42, 0.05, 0.015), "darkwood", 0.005)


def bottle():
    lathe((0, 0, 0), [(0.0001, 0.0), (0.04, 0.0), (0.042, 0.18), (0.016, 0.25), (0.015, 0.3), (0.017, 0.31), (0.0001, 0.31)], "glassg", 14)
    lathe((0, 0, 0), [(0.0425, 0.06), (0.0425, 0.13)], "label2", 14)


def bottles_row():
    # a row of bottles for the back shelf, 1 m long along z: wine, rum, gin, whisky, of different shapes
    rng = np.random.default_rng(4)
    z = -0.47
    k = 0
    while z < 0.47:
        kind = k % 4
        m = ("glassg", "glassb", "glassc", "glassr")[kind]
        if kind == 0:
            prof = [(0.0001, 0.0), (0.035, 0.0), (0.036, 0.2), (0.014, 0.27), (0.013, 0.32), (0.0001, 0.32)]
        elif kind == 1:
            prof = [(0.0001, 0.0), (0.045, 0.0), (0.045, 0.16), (0.02, 0.2), (0.016, 0.26), (0.0001, 0.26)]
        elif kind == 2:
            prof = [(0.0001, 0.0), (0.038, 0.0), (0.04, 0.22), (0.015, 0.25), (0.014, 0.3), (0.0001, 0.3)]
        else:
            prof = [(0.0001, 0.0), (0.05, 0.0), (0.05, 0.12), (0.04, 0.17), (0.015, 0.22), (0.0001, 0.22)]
        lathe((0, 0, z), prof, m, 12)
        lathe((0, 0, z), [(prof[1][0] + 0.002, 0.05), (prof[1][0] + 0.002, 0.11)], "label2", 12)
        z += 0.1 + 0.02 * rng.random(); k += 1 + int(rng.random() * 2)


def mug():
    lathe((0, 0, 0), [(0.0001, 0.0), (0.045, 0.0), (0.047, 0.14), (0.042, 0.14), (0.04, 0.012), (0.0001, 0.012)], "glassc", 14)
    lathe((0, 0, 0), [(0.0001, 0.01), (0.039, 0.01), (0.039, 0.12), (0.0001, 0.12)], "beer", 14)
    lathe((0, 0, 0), [(0.04, 0.115), (0.047, 0.14), (0.03, 0.16), (0.0001, 0.162)], "foam", 14)
    rod([(0.045, 0.11, 0), (0.08, 0.1, 0), (0.08, 0.04, 0), (0.045, 0.035, 0)], 0.009, "glassc", 4, caps=False)


def lantern():
    # a ship's lantern on its chain: a brass cap and base, a glowing glass, wire guards
    lathe((0, 0, 0), [(0.0001, -0.2), (0.09, -0.2), (0.1, -0.17), (0.0001, -0.17)], "brass2", 14)
    lathe((0, 0, 0), [(0.07, -0.17), (0.085, -0.05), (0.07, 0.07)], "lampg", 14)
    for k in range(4):
        a = k * math.pi / 2 + math.pi / 4
        cyl((0.09 * math.cos(a), -0.17, 0.09 * math.sin(a)), (0.09 * math.cos(a), 0.07, 0.09 * math.sin(a)), 0.006, "brass2", 4)
    lathe((0, 0, 0), [(0.1, 0.07), (0.06, 0.14), (0.02, 0.17), (0.0001, 0.18)], "brass2", 14)
    rod([(-0.03, 0.18, 0), (0, 0.24, 0), (0.03, 0.18, 0)], 0.006, "brass2", 4, caps=False)


def jukebox():
    # an arched jukebox: a wooden cabinet, a lit glass arch, chrome grille, the selector
    box((0, 0.55, 0), (0.3, 0.55, 0.45), "oak", 0.03)
    vs =[(0.3, 1.1, -0.45)] + [(0.3, 1.1 + 0.42 * math.sin(k / 12 * math.pi), -0.45 * math.cos(k / 12 * math.pi)) for k in range(13)]
    quads(vs, [tuple(range(14))], "oak", "front")
    rod([(0.31, 1.1, -0.42), *[(0.31, 1.1 + 0.39 * math.sin(k / 12 * math.pi), -0.42 * math.cos(k / 12 * math.pi)) for k in range(13)]], 0.04, "jukeglow", 6, caps=False)
    rod([(0.32, 1.1, -0.34), *[(0.32, 1.1 + 0.3 * math.sin(k / 12 * math.pi), -0.34 * math.cos(k / 12 * math.pi)) for k in range(13)]], 0.03, "jukeglow2", 6, caps=False)
    box((0.31, 0.95, 0), (0.01, 0.12, 0.3), "glassc", 0.0)
    for k in range(9):
        box((0.31, 0.25 + k * 0.05, 0), (0.012, 0.012, 0.3), "chrome2", 0.002)
    box((0.31, 0.72, 0), (0.02, 0.06, 0.32), "chrome2", 0.005)


def dartboard():
    # on a wall (its back at x 0, facing +x), centred at y 1.73
    disc((0.02, 1.73, 0), 'x', 0.26, 0.04, "darkwood", 32)
    for k in range(20):
        a0 = k * 2 * math.pi / 20
        for (r0, r1, m) in ((0.02, 0.1, "dartblack" if k % 2 else "dartcream"), (0.1, 0.11, "dartred"), (0.11, 0.17, "dartblack" if k % 2 else "dartcream"), (0.17, 0.18, "dartred")):
            a1 = a0 + 2 * math.pi / 20
            quads([(0.045, 1.73 + r0 * math.sin(a0), r0 * math.cos(a0)), (0.045, 1.73 + r1 * math.sin(a0), r1 * math.cos(a0)), (0.045, 1.73 + r1 * math.sin(a1), r1 * math.cos(a1)), (0.045, 1.73 + r0 * math.sin(a1), r0 * math.cos(a1))], [(0, 1, 2, 3)], m, "seg")
    disc((0.05, 1.73, 0), 'x', 0.02, 0.01, "dartred", 12)


def hearth():
    # a stone fireplace against a wall (back at x 0): the surround, a mantel with candlesticks, the dark firebox, logs
    for z in (-0.85, 0.85):
        box((0.25, 0.6, z), (0.25, 0.6, 0.18), "stone3", 0.03)
    box((0.25, 1.32, 0), (0.28, 0.12, 1.1), "stone3", 0.03)
    box((0.32, 1.48, 0), (0.32, 0.05, 1.2), "darkwood", 0.01)
    box((0.12, 0.6, 0), (0.12, 0.6, 0.68), "soot", 0.0)
    box((0.3, 0.03, 0), (0.42, 0.03, 1.1), "stone3", 0.01)
    for k in range(3):
        cyl((0.25, 0.1 + (k == 2) * 0.12, -0.4 + k * 0.35), (0.25, 0.1 + (k == 2) * 0.12, 0.0 + k * 0.25), 0.07, "darkwood", 8)
    for z in (-0.8, 0.8):
        lathe((0.35, 0, z), [(0.0001, 1.53), (0.04, 1.53), (0.02, 1.6), (0.015, 1.75), (0.03, 1.77), (0.0001, 1.78)], "brass2", 10)
        cyl((0.35, 1.78, z), (0.35, 1.9, z), 0.013, "ivory", 6)
    box((0.3, 2.0, 0), (0.03, 0.3, 0.45), "brass2", 0.01)   # (a mirror's frame over it)
    box((0.33, 2.0, 0), (0.01, 0.25, 0.4), "glassc", 0.0)


def piano():
    # an upright piano (its back at x 0): the case, the keyboard, the music stand, the pedals, a stool
    box((0.3, 0.7, 0), (0.3, 0.6, 0.75), "ebony", 0.02)
    box((0.3, 1.31, 0), (0.32, 0.02, 0.77), "ebony", 0.01)
    box((0.62, 0.74, 0), (0.12, 0.04, 0.7), "ebony", 0.01)
    for k in range(36):
        box((0.68, 0.79, -0.68 + k * 0.039), (0.07, 0.012, 0.017), "ivory", 0.002)
        if k % 7 not in (2, 6):
            box((0.66, 0.81, -0.66 + k * 0.039), (0.045, 0.012, 0.008), "ebony", 0.001)
    box((0.52, 1.0, 0), (0.02, 0.12, 0.35), "ebony", 0.005)
    for z in (-0.1, 0.1):
        box((0.6, 0.05, z), (0.04, 0.012, 0.025), "brass2", 0.003)
    box((1.2, 0.45, 0), (0.18, 0.04, 0.35), "ebony", 0.01)
    for x in (1.05, 1.35):
        for z in (-0.3, 0.3):
            cyl((x, 0, z), (x, 0.43, z), 0.025, "ebony", 6)


def pooltable():
    # a pool table 1.4 x 2.6 m: a carved frame on turned legs, the green bed, cushions, six pockets
    box((0, 0.73, 0), (0.7, 0.1, 1.3), "darkwood", 0.03)
    box((0, 0.82, 0), (0.6, 0.02, 1.2), "green_felt", 0.0)
    for s in (-1, 1):
        box((s * 0.63, 0.86, 0), (0.05, 0.04, 1.2), "green_felt", 0.01)
        box((0, 0.86, s * 1.23), (0.6, 0.04, 0.05), "green_felt", 0.01)
        box((s * 0.69, 0.86, 0), (0.03, 0.05, 1.3), "darkwood", 0.01)
        box((0, 0.86, s * 1.29), (0.7, 0.05, 0.03), "darkwood", 0.01)
    for x in (-0.6, 0.6):
        for z in (-1.2, 0.0, 1.2):
            disc((x, 0.83, z), 'y', 0.06, 0.03, "dartblack", 12)
    for x in (-0.6, 0.6):
        for z in (-1.15, 1.15):
            lathe((x, 0, z), [(0.0001, 0.0), (0.08, 0.0), (0.06, 0.1), (0.09, 0.3), (0.05, 0.5), (0.07, 0.63), (0.0001, 0.64)], "darkwood", 10)
    for k, (x, z) in enumerate(((0.1, 0.3), (-0.15, 0.4), (0.0, -0.6), (0.2, -0.5))):
        blob((x, 0.86, z), (0.03, 0.03, 0.03), ("dartred", "beer", "ivory", "glassb")[k], 70 + k, 0.0, 10)


def counter():
    # a 1 m length of bar counter along z (the game stretches it): the panelled front (+x), the brass foot rail, the top
    box((0, 0.52, 0), (0.3, 0.52, 0.5), "darkwood", 0.0)
    box((0.31, 0.52, 0), (0.01, 0.42, 0.42), "oak", 0.01)
    box((0.0, 1.08, 0), (0.38, 0.035, 0.5), "oak", 0.0)
    cyl((0.42, 0.18, -0.5), (0.42, 0.18, 0.5), 0.025, "brass2", 10)
    box((0.36, 0.12, 0), (0.04, 0.008, 0.02), "brass2", 0.002)


def barrel():
    lathe((0, 0, 0), [(0.0001, 0.0), (0.24, 0.0), (0.28, 0.3), (0.24, 0.6), (0.0001, 0.6)], "oak", 18)
    for h in (0.1, 0.5):
        lathe((0, 0, 0), [(0.255 + 0.02 * (1 - abs(h - 0.3) / 0.3), h - 0.02), (0.26 + 0.02 * (1 - abs(h - 0.3) / 0.3), h + 0.02)], "iron", 18)
    disc((0.27, 0.3, 0), 'x', 0.03, 0.04, "brass2", 8)


def armchair():
    box((0, 0.25, 0), (0.42, 0.13, 0.42), "redleather", 0.06)
    box((0.05, 0.42, 0), (0.35, 0.07, 0.36), "redleather", 0.06)
    box((-0.34, 0.7, 0), (0.1, 0.4, 0.44), "redleather", 0.07)
    for s in (-1, 1):
        cyl((-0.42, 0.55, s * 0.42), (0.4, 0.55, s * 0.42), 0.11, "redleather", 14)
        box((0, 0.35, s * 0.42), (0.42, 0.2, 0.1), "redleather", 0.05)
        for x in (-0.35, 0.35):
            lathe((x, 0, s * 0.35), [(0.0001, 0.0), (0.03, 0.0), (0.04, 0.08), (0.0001, 0.12)], "darkwood", 8)


def chaise():
    box((0, 0.28, 0), (0.42, 0.12, 0.95), "velvetp", 0.06)
    blob((-0.05, 0.62, -0.8), (0.4, 0.35, 0.18), "velvetp", 3, 0.0, 16)
    for x in (-0.35, 0.35):
        for z in (-0.85, 0.85):
            lathe((x, 0, z), [(0.0001, 0.0), (0.03, 0.0), (0.04, 0.12), (0.0001, 0.16)], "brass2", 8)


def winerack():
    box((0.15, 0.95, 0), (0.15, 0.95, 0.9), "darkwood", 0.01)
    for r in range(5):
        for i in range(10):
            z = -0.81 + i * 0.18
            cyl((0.3, 0.3 + r * 0.36, z), (0.42, 0.3 + r * 0.36, z), 0.04, "glassr" if (i + r) % 3 else "glassg", 10)


PIECES = {"stool": stool, "chair": chair, "table_round": table_round, "table_sq": table_sq, "bottle": bottle,
          "bottles_row": bottles_row, "mug": mug, "lantern": lantern, "jukebox": jukebox, "dartboard": dartboard,
          "hearth": hearth, "piano": piano, "pooltable": pooltable, "counter": counter, "barrel": barrel,
          "armchair": armchair, "chaise": chaise, "winerack": winerack}

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
