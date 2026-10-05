# A Night Off's animals and hand things in parts the game moves (the user: "everything is still blocky"): the alley dog
# and the goat as a body, a head, a leg and a tail each (the game places and swings them like the old boxes), and the
# weapons and odds and ends people pick up: a pool cue, a frying pan, a kitchen knife, a police club, a shotgun, a
# motorbike and a coffin. Each about its own pivot in the game frame (x forward, y up, z), sized in metres.
#     blender -b --factory-startup -P tools/artgen/nightoff_animals.py -- --out assets/nightoff/props
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
    for k, rgb, r, m in (("fur", (0.42, 0.28, 0.16), 0.95, 0.0), ("furdark", (0.2, 0.13, 0.08), 0.95, 0.0), ("nose", (0.04, 0.03, 0.03), 0.3, 0.0),
                         ("wool", (0.82, 0.8, 0.74), 0.98, 0.0), ("horn", (0.42, 0.38, 0.3), 0.6, 0.0), ("hoof", (0.15, 0.12, 0.1), 0.6, 0.0),
                         ("eyeb", (0.02, 0.02, 0.02), 0.2, 0.0), ("tongue", (0.75, 0.3, 0.35), 0.5, 0.0), ("cue", (0.75, 0.55, 0.3), 0.4, 0.0),
                         ("cuetip", (0.2, 0.3, 0.6), 0.6, 0.0), ("pan", (0.1, 0.1, 0.11), 0.4, 0.8), ("steel2", (0.75, 0.76, 0.78), 0.25, 1.0),
                         ("wood2", (0.32, 0.2, 0.1), 0.6, 0.0), ("rubber2", (0.05, 0.05, 0.05), 0.7, 0.0), ("bikepaint", (0.08, 0.08, 0.1), 0.3, 0.4),
                         ("chrome3", (0.8, 0.8, 0.82), 0.15, 1.0), ("coffin", (0.22, 0.14, 0.09), 0.5, 0.0), ("brass3", (0.75, 0.58, 0.22), 0.3, 1.0)):
        B.MATS[k] = B.flat(k, rgb, r, m)


# ---- the dog (the old boxes: body 0.62 x 0.26 x 0.28 at y 0.42; head 0.24 at x 0.36, y 0.6; legs 0.32 tall; tail)
def dog_body():
    blob((0, 0, 0), (0.33, 0.15, 0.15), "fur", 1, 0.06, 20)
    blob((0.12, -0.02, 0), (0.18, 0.15, 0.16), "fur", 2, 0.06, 18)   # (the deeper chest)
    blob((0.05, -0.1, 0), (0.2, 0.06, 0.12), "furdark", 3, 0.1, 14)   # (a darker belly)


def dog_head():
    blob((0, 0, 0), (0.13, 0.12, 0.11), "fur", 4, 0.05, 18)
    blob((0.12, -0.03, 0), (0.09, 0.06, 0.065), "fur", 5, 0.03, 14)   # the muzzle
    blob((0.21, -0.02, 0), (0.025, 0.02, 0.03), "nose", 6, 0.0, 10)
    for s in (1, -1):
        blob((0.07, 0.04, s * 0.06), (0.018, 0.018, 0.018), "eyeb", 7, 0.0, 8)
        blob((-0.02, 0.06, s * 0.09), (0.04, 0.09, 0.02), "furdark", 8 + s, 0.05, 10)   # (floppy ears)
    blob((0.14, -0.08, 0), (0.04, 0.008, 0.025), "tongue", 9, 0.0, 8)


def dog_leg():
    # pivot at the hip/shoulder (the top), the paw at y -0.32
    cyl((0, 0, 0), (0, -0.28, 0), 0.04, "fur", 8, r2=0.032)
    blob((0.02, -0.3, 0), (0.05, 0.025, 0.04), "furdark", 10, 0.05, 10)


def dog_tail():
    rod([(0, 0, 0), (-0.1, 0.06, 0), (-0.2, 0.08, 0)], 0.025, "fur", 6)


# ---- the goat (the old boxes: body 0.75 x 0.32 x 0.32 at y 0.55; head 0.26 at x 0.45, y 0.78; horns; legs 0.4)
def goat_body():
    blob((0, 0, 0), (0.38, 0.17, 0.17), "wool", 11, 0.12, 20)
    blob((0.25, 0.04, 0), (0.12, 0.14, 0.13), "wool", 12, 0.12, 16)


def goat_head():
    blob((0, 0, 0), (0.12, 0.1, 0.09), "wool", 13, 0.05, 16)
    blob((0.12, -0.05, 0), (0.08, 0.06, 0.06), "wool", 14, 0.03, 12)
    blob((0.19, -0.06, 0), (0.02, 0.02, 0.025), "nose", 15, 0.0, 8)
    for s in (1, -1):
        blob((0.06, 0.03, s * 0.07), (0.016, 0.016, 0.016), "eyeb", 16, 0.0, 8)
        rod([(0.0, 0.08, s * 0.04), (-0.06, 0.18, s * 0.06), (-0.14, 0.18, s * 0.08)], 0.018, "horn", 6)
        blob((-0.02, 0.0, s * 0.1), (0.03, 0.02, 0.06), "wool", 17 + s, 0.05, 8)
    rod([(0.12, -0.11, 0), (0.1, -0.2, 0)], 0.015, "wool", 4)   # (the beard)


def goat_leg():
    cyl((0, 0, 0), (0, -0.36, 0), 0.035, "wool", 8, r2=0.028)
    cyl((0, -0.36, 0), (0, -0.4, 0), 0.03, "hoof", 8)


# ---- things in hands
def cue():
    cyl((0, -0.72, 0), (0, 0.72, 0), 0.016, "cue", 10, r2=0.007)
    cyl((0, -0.72, 0), (0, -0.35, 0), 0.0165, "wood2", 10, r2=0.014)
    cyl((0, 0.72, 0), (0, 0.735, 0), 0.007, "cuetip", 8)


def pan():
    lathe((0, 0, 0), [(0.0001, 0.0), (0.13, 0.0), (0.15, 0.05), (0.14, 0.05), (0.12, 0.008), (0.0001, 0.008)], "pan", 20)
    cyl((0.14, 0.04, 0), (0.42, 0.06, 0), 0.015, "pan", 8)
    cyl((0.28, 0.05, 0), (0.42, 0.065, 0), 0.02, "wood2", 8)


def knife():
    quads([(0.0, 0.0, -0.002), (0.18, 0.0, -0.002), (0.2, 0.008, -0.002), (0.0, 0.03, -0.002)], [(0, 1, 2, 3)], "steel2", "blade")
    quads([(0.0, 0.0, 0.002), (0.0, 0.03, 0.002), (0.2, 0.008, 0.002), (0.18, 0.0, 0.002)], [(0, 1, 2, 3)], "steel2", "blade2")
    box((-0.06, 0.015, 0), (0.06, 0.016, 0.01), "wood2", 0.004)


def club():
    lathe((0, 0, 0), [(0.0001, -0.47), (0.02, -0.47), (0.022, -0.3), (0.03, 0.4), (0.025, 0.47), (0.0001, 0.48)], "rubber2", 10)
    cyl((0.03, -0.25, 0), (0.12, -0.25, 0), 0.015, "rubber2", 8)


def shotgun():
    cyl((-0.1, 0.0, 0), (0.55, 0.0, 0), 0.018, "steel2", 10)
    cyl((-0.1, -0.03, 0), (0.45, -0.03, 0), 0.016, "steel2", 10)
    box((-0.2, -0.02, 0), (0.12, 0.04, 0.025), "wood2", 0.01)
    quads([(-0.3, 0.0, -0.02), (-0.5, -0.06, -0.02), (-0.5, -0.14, -0.02), (-0.3, -0.06, -0.02)], [(0, 1, 2, 3)], "wood2", "stock")
    box((-0.4, -0.05, 0), (0.1, 0.045, 0.02), "wood2", 0.01)


def bike():
    # a motorbike (the bikers' night): two spoked wheels, a tank, a seat, chrome forks and bars, an exhaust
    for x in (-0.75, 0.75):
        disc((x, 0.32, 0), 'z', 0.32, 0.1, "rubber2", 24, hole=0.24)
        disc((x, 0.32, 0), 'z', 0.24, 0.02, "chrome3", 24, hole=0.22)
        for k in range(8):
            a = k * math.pi / 4
            rod([(x, 0.32, 0), (x + 0.23 * math.cos(a), 0.32 + 0.23 * math.sin(a), 0)], 0.005, "chrome3", 3)
    blob((0.15, 0.78, 0), (0.28, 0.12, 0.13), "bikepaint", 20, 0.0, 16)   # the tank
    box((-0.25, 0.8, 0), (0.25, 0.04, 0.12), "rubber2", 0.03)
    box((0, 0.5, 0), (0.25, 0.14, 0.1), "chrome3", 0.03)                # the engine
    for s in (1, -1):
        cyl((0.75, 0.32, s * 0.07), (0.5, 1.0, s * 0.07), 0.02, "chrome3", 8)
    rod([(0.5, 1.05, -0.32), (0.45, 1.0, 0), (0.5, 1.05, 0.32)], 0.015, "chrome3", 6)
    cyl((-0.1, 0.35, 0.14), (-0.85, 0.42, 0.14), 0.035, "chrome3", 10)
    cyl((-0.75, 0.32, 0), (0.0, 0.45, 0), 0.03, "bikepaint", 8)
    blob((0.62, 0.95, 0), (0.06, 0.07, 0.07), "chrome3", 21, 0.0, 10)   # (the headlamp)


def coffin():
    vs = [(-1.0, 0, -0.22), (0.55, 0, -0.32), (1.0, 0, -0.18), (1.0, 0, 0.18), (0.55, 0, 0.32), (-1.0, 0, 0.22)]
    top = [(x, 0.36, z) for (x, y, z) in vs]
    quads(vs + top, [(0, 1, 7, 6), (1, 2, 8, 7), (2, 3, 9, 8), (3, 4, 10, 9), (4, 5, 11, 10), (5, 0, 6, 11), (11, 10, 9, 8, 7, 6), (0, 5, 4, 3, 2, 1)], "coffin", "box")
    lid = [(x * 1.02, 0.4, z * 1.04) for (x, y, z) in vs]
    quads(top + lid, [(0, 1, 7, 6), (1, 2, 8, 7), (2, 3, 9, 8), (3, 4, 10, 9), (4, 5, 11, 10), (5, 0, 6, 11), (11, 10, 9, 8, 7, 6)], "coffin", "lid")
    for x in (-0.6, 0.0, 0.6):
        for s in (1, -1):
            cyl((x - 0.08, 0.2, s * 0.3), (x + 0.08, 0.2, s * 0.3), 0.012, "brass3", 6)
    rod([(-0.25, 0.41, 0), (0.25, 0.41, 0)], 0.015, "brass3", 4)
    rod([(0.1, 0.41, -0.12), (0.1, 0.41, 0.12)], 0.015, "brass3", 4)


PIECES = {"dog_body": dog_body, "dog_head": dog_head, "dog_leg": dog_leg, "dog_tail": dog_tail, "goat_body": goat_body,
          "goat_head": goat_head, "goat_leg": goat_leg, "cue": cue, "pan": pan, "knife": knife, "club": club,
          "shotgun": shotgun, "bike": bike, "coffin": coffin}

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
