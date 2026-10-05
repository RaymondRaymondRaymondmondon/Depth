# Red Tide's salvage parts (the workbench builds' fifteen pieces) as real things, not crates (the user: "the craftable
# pieces are just scattered on the floor instead of in corners"; they now lie tucked into corners, each looking like what
# it is). Each sits on its base at the origin in the game frame, about 0.3-0.6 m, baked like the Trawl's props.
#     blender -b --factory-startup -P tools/artgen/rt_salvage.py -- --out assets/redtide/salvage
import os
import sys
import math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ["ARTGEN_IMPORT_TRAWLPROPS"] = "1"
import numpy as np
import common as C
import props as P
import boat as B
from boat import box, cyl, rod, disc, lathe
from props import blob


def mats():
    for k, rgb, r, m in (("leather", (0.28, 0.16, 0.08), 0.6, 0.0), ("copper", (0.55, 0.28, 0.14), 0.35, 1.0),
                         ("shellc", (0.3, 0.24, 0.1), 0.5, 0.0), ("scute", (0.42, 0.33, 0.14), 0.45, 0.0),
                         ("hose", (0.05, 0.05, 0.05), 0.6, 0.0), ("tin", (0.6, 0.6, 0.58), 0.4, 0.8), ("label", (0.62, 0.12, 0.08), 0.6, 0.0),
                         ("lamp", (0.95, 0.8, 0.45), 0.3, 0.0), ("stake", (0.4, 0.3, 0.18), 0.85, 0.0), ("netc", (0.5, 0.42, 0.28), 0.9, 0.0)):
        B.MATS[k] = B.flat(k, rgb, r, m)


def shell():
    blob((0, 0.12, 0), (0.42, 0.14, 0.34), "shellc", 1, 0.03, 26)
    for k in range(5):
        blob((0.28 - k * 0.14, 0.24, 0), (0.07, 0.03, 0.06), "scute", 10 + k, 0.05, 12)
        for s in (-1, 1):
            blob((0.22 - k * 0.11, 0.18, s * 0.17), (0.06, 0.025, 0.05), "scute", 20 + k, 0.05, 10)
    lathe((0, 0, 0), [(0.0001, 0.0), (0.36, 0.0), (0.36, 0.02), (0.0001, 0.02)], "shellc", 20)


def strap():
    for k in range(3):
        rr = 0.17 - k * 0.035
        pts = [(math.cos(a) * rr, 0.012 + k * 0.012, math.sin(a) * rr) for a in np.linspace(0, 2 * math.pi, 24)]
        rod(pts, 0.012, "leather", 4, caps=False)
    box((0.2, 0.02, 0), (0.04, 0.012, 0.03), "brass", 0.004)
    rod([(0.17, 0.02, 0.0), (0.32, 0.015, 0.05), (0.42, 0.012, 0.03)], 0.012, "leather", 4)


def rim():
    disc((0, 0.03, 0), 'y', 0.28, 0.05, "brass", 32, hole=0.23)
    for k in range(8):
        a = k * math.pi / 4
        cyl((math.cos(a) * 0.255, 0.05, math.sin(a) * 0.255), (math.cos(a) * 0.255, 0.075, math.sin(a) * 0.255), 0.012, "iron", 6)


def fan():
    # a three-bladed propeller lying on the floor, its hub up
    cyl((0, 0.02, 0), (0, 0.16, 0), 0.06, "brass", 16)
    for k in range(3):
        a = k * 2 * math.pi / 3
        ca, sa = math.cos(a), math.sin(a)
        blob((ca * 0.2, 0.09 + 0.02 * k, sa * 0.2), (0.17, 0.015, 0.08), "brass", 30 + k, 0.02, 14)
    lathe((0, 0, 0), [(0.06, 0.16), (0.03, 0.2), (0.0001, 0.21)], "brass", 14)


def dynamo():
    # a dynamo on its side: the drum, copper windings, end plates, terminals
    cyl((-0.22, 0.17, 0), (0.22, 0.17, 0), 0.15, "iron", 20)
    for x in np.linspace(-0.15, 0.15, 5):
        cyl((x - 0.02, 0.17, 0), (x + 0.02, 0.17, 0), 0.158, "copper", 20)
    for x in (-0.23, 0.23):
        disc((x, 0.17, 0), 'x', 0.17, 0.03, "iron", 20)
    cyl((0.23, 0.17, 0), (0.33, 0.17, 0), 0.025, "iron", 10)
    for z in (-0.06, 0.06):
        cyl((0.0, 0.31, z), (0.0, 0.37, z), 0.018, "brass", 8)
    box((0, 0.02, 0), (0.24, 0.02, 0.13), "iron", 0.005)


def mount():
    # an iron mounting bracket: a base plate, two cheeks with bolt holes, a crossbar
    box((0, 0.015, 0), (0.25, 0.015, 0.16), "iron", 0.004)
    for z in (-0.12, 0.12):
        box((0, 0.17, z), (0.18, 0.15, 0.015), "iron", 0.004)
        disc((0, 0.24, z), 'z', 0.035, 0.035, "bulk", 12)
    cyl((0, 0.08, -0.13), (0, 0.08, 0.13), 0.02, "iron", 8)
    for x in (-0.2, 0.2):
        for z in (-0.12, 0.12):
            cyl((x, 0.03, z), (x, 0.05, z), 0.015, "brass", 6)


def net():
    rng = np.random.default_rng(3)
    for k in range(5):
        blob(((rng.random() - 0.5) * 0.5, 0.06 + 0.04 * rng.random(), (rng.random() - 0.5) * 0.35), (0.2, 0.08, 0.17), "netc", 40 + k, 0.2, 12)
    for k in range(6):
        a = k * 1.05
        blob((math.cos(a) * 0.25, 0.12, math.sin(a) * 0.18), (0.035, 0.025, 0.025), "cork", 50 + k, 0.05, 8)


def stakes():
    for s in (-1, 1):
        cyl((-0.4, 0.04, s * 0.05), (0.32, 0.04, s * 0.05), 0.035, "stake", 8)
        rod([(0.32, 0.04, s * 0.05), (0.44, 0.04, s * 0.05)], 0.02, "stake", 6)
    for x in (-0.2, 0.1):
        pts = [(x, 0.04 + 0.05 * math.sin(a), 0.1 * math.cos(a)) for a in np.linspace(0, 2 * math.pi, 14)]
        rod(pts, 0.008, "rope", 4, caps=False)


def bell():
    lathe((0, 0, 0), [(0.0001, 0.0), (0.16, 0.0), (0.15, 0.03), (0.11, 0.12), (0.09, 0.22), (0.06, 0.27), (0.0001, 0.28)], "brass", 24)
    disc((0, 0.3, 0), 'z', 0.035, 0.015, "brass", 12, hole=0.02)
    lathe((0, 0, 0), [(0.0001, 0.02), (0.03, 0.03), (0.0001, 0.06)], "iron", 10)


def buoy():
    blob((0, 0.17, 0), (0.17, 0.17, 0.17), "paint_red", 2, 0.0, 24)
    lathe((0, 0, 0), [(0.175, 0.14), (0.178, 0.2)], "paint_white", 24)
    disc((0, 0.35, 0), 'z', 0.04, 0.015, "iron", 12, hole=0.025)


def lantern():
    lathe((0, 0, 0), [(0.0001, 0.0), (0.11, 0.0), (0.11, 0.03), (0.0001, 0.03)], "iron", 16)
    lathe((0, 0, 0), [(0.075, 0.03), (0.09, 0.12), (0.075, 0.22)], "lamp", 16)
    for k in range(4):
        a = k * math.pi / 2 + math.pi / 4
        cyl((math.cos(a) * 0.1, 0.03, math.sin(a) * 0.1), (math.cos(a) * 0.1, 0.24, math.sin(a) * 0.1), 0.008, "iron", 6)
    lathe((0, 0, 0), [(0.12, 0.24), (0.06, 0.3), (0.02, 0.32), (0.0001, 0.33)], "iron", 16)
    rod([(-0.05, 0.33, 0), (0, 0.4, 0), (0.05, 0.33, 0)], 0.008, "iron", 4, caps=False)


def chumtin():
    lathe((0, 0, 0), [(0.0001, 0.0), (0.09, 0.0), (0.09, 0.2), (0.085, 0.21), (0.0001, 0.21)], "tin", 20)
    lathe((0, 0, 0), [(0.0915, 0.05), (0.0915, 0.15)], "label", 20)
    rod([(-0.09, 0.2, 0), (0, 0.27, 0), (0.09, 0.2, 0)], 0.006, "iron", 4, caps=False)


def compressor():
    # a small air compressor: a tank on its side, a pump head, a pressure gauge
    cyl((-0.25, 0.16, 0), (0.25, 0.16, 0), 0.14, "paint_green", 18)
    for x in (-0.25, 0.25):
        blob((x, 0.16, 0), (0.04, 0.14, 0.14), "paint_green", 60, 0.0, 16)
    box((0.05, 0.36, 0), (0.1, 0.08, 0.07), "iron", 0.01)
    disc((-0.12, 0.31, 0.0), 'y', 0.06, 0.03, "brass", 16)
    disc((-0.12, 0.33, 0.0), 'y', 0.045, 0.01, "dial", 16)
    for x in (-0.18, 0.18):
        box((x, 0.03, 0), (0.03, 0.03, 0.12), "iron", 0.004)


def hose():
    for k in range(4):
        rr = 0.22 - k * 0.012
        pts = [(math.cos(a) * rr, 0.03 + k * 0.045, math.sin(a) * rr) for a in np.linspace(0, 2 * math.pi, 26)]
        rod(pts, 0.025, "hose", 6, caps=False)
    cyl((0.22, 0.18, 0), (0.3, 0.18, 0.05), 0.03, "brass", 10)


def valve():
    cyl((0, 0.0, 0), (0, 0.16, 0), 0.05, "brass", 14)
    cyl((-0.12, 0.08, 0), (0.12, 0.08, 0), 0.04, "brass", 12)
    for x in (-0.12, 0.12):
        disc((x, 0.08, 0), 'x', 0.06, 0.02, "brass", 12)
    cyl((0, 0.16, 0), (0, 0.24, 0), 0.012, "iron", 6)
    disc((0, 0.25, 0), 'y', 0.11, 0.02, "paint_red", 20, hole=0.085)
    for k in range(3):
        a = k * math.pi / 3
        rod([(math.cos(a) * 0.1, 0.25, math.sin(a) * 0.1), (-math.cos(a) * 0.1, 0.25, -math.sin(a) * 0.1)], 0.008, "paint_red", 4)


PARTS = {"shell": shell, "strap": strap, "rim": rim, "fan": fan, "dynamo": dynamo, "mount": mount, "net": net,
         "stakes": stakes, "bell": bell, "buoy": buoy, "lantern": lantern, "chumtin": chumtin, "compressor": compressor,
         "hose": hose, "valve": valve}

if __name__ == "__main__":
    out = C.out_dir()
    a = C.args()
    only = a[a.index("--only") + 1].split(",") if "--only" in a else None
    for name, fn in PARTS.items():
        if only and name not in only:
            continue
        P.reset(); P.land_mats(); P.clutter_mats(); mats()
        fn()
        B.finish(out, name + ".glb", lambda x, y, z, k: k)
