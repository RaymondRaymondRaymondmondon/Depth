# NOCLIP's loot as real things (the playtest, 2026-10-05: "the trash you pick up is just blocks"): one small model per
# archetype; the game maps each of the 117 loot names to an archetype by keywords (noclip_render.cpp, PropArchOf). Flat
# named materials (the level shader lights them), built facing the room (Blender -Y, the game's +Z), resting on the
# floor at the origin, real sizes in metres.
#     blender -b --factory-startup -P tools/artgen/noclip_props.py -- --out assets/noclip/props [--only wallet,tv]
import os
import sys
import math
from mathutils import Vector
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
os.environ["ARTGEN_IMPORT"] = "1"
import common as C
import fowl_props as F
bpy = C.bpy

PAL = {
    "leather": ((0.22, 0.12, 0.06), 0.6), "black": ((0.03, 0.03, 0.03), 0.4), "steel": ((0.6, 0.62, 0.64), 0.3, 0.9),
    "chrome": ((0.85, 0.85, 0.87), 0.12, 1.0), "brass": ((0.78, 0.56, 0.22), 0.28, 1.0), "copper": ((0.72, 0.36, 0.18), 0.3, 1.0),
    "red": ((0.6, 0.06, 0.05), 0.45), "yellow": ((0.85, 0.65, 0.06), 0.5), "green": ((0.12, 0.38, 0.12), 0.55),
    "blue": ((0.08, 0.2, 0.55), 0.45), "white": ((0.85, 0.84, 0.8), 0.5), "cream": ((0.84, 0.78, 0.6), 0.6),
    "wood": ((0.34, 0.2, 0.09), 0.55), "darkwood": ((0.16, 0.08, 0.04), 0.5), "grey": ((0.35, 0.36, 0.38), 0.55),
    "plastic": ((0.18, 0.18, 0.2), 0.45), "beige": ((0.72, 0.68, 0.58), 0.55), "glass": ((0.75, 0.88, 0.92), 0.05),
    "paper": ((0.9, 0.88, 0.8), 0.8), "canvas": ((0.36, 0.38, 0.24), 0.85), "orange": ((0.9, 0.4, 0.06), 0.5),
    "gold": ((0.95, 0.72, 0.2), 0.18, 1.0), "silver": ((0.85, 0.86, 0.88), 0.15, 1.0), "crystal": ((0.55, 0.85, 0.95), 0.05),
    "pink": ((0.9, 0.5, 0.6), 0.6), "purple": ((0.4, 0.18, 0.6), 0.5), "soil": ((0.18, 0.11, 0.06), 0.9), "flesh": ((0.55, 0.12, 0.14), 0.4),
    "fabric": ((0.55, 0.2, 0.18), 0.85), "burlap": ((0.58, 0.46, 0.28), 0.95), "rust": ((0.42, 0.2, 0.1), 0.8),
}


def setup():
    F.MATS.clear()
    for n, v in PAL.items():
        c, r = v[0], v[1]; m = v[2] if len(v) > 2 else 0.0
        F.mat(n, c, r, m)


B, S, Cy, L, T, Tu = F.box, F.sphere, F.cyl, F.lathe, F.torus, F.tube
R90 = (math.pi / 2, 0, 0)   # (a cylinder lying along y)
RY = (0, math.pi / 2, 0)    # (lying along x)


def wallet():
    B("w", (0.11, 0.09, 0.02), (0, 0, 0.01), "leather", 0.006); B("stitch", (0.1, 0.002, 0.012), (0, -0.045, 0.01), "cream", 0.001)
def tape():
    B("t", (0.19, 0.1, 0.025), (0, 0, 0.0125), "black", 0.004); B("label", (0.12, 0.002, 0.016), (0, -0.051, 0.013), "paper", 0.001)
    for x in (-0.04, 0.04): Cy(f"reel{x}", 0.022, 0.004, (x, 0, 0.026), "white")
def keys():
    T("ring", (0, 0, 0.01), 0.025, 0.004, "steel")
    for k in range(3): B(f"key{k}", (0.012, 0.06, 0.004), (0.02 + k * 0.012, 0.04, 0.008), "brass" if k else "steel", 0.002, rot=(0, 0, 0.3 * k))
    Tu("lanyard", [(0, -0.02, 0.01), (-0.1, -0.15, 0.012), (0.05, -0.25, 0.012), (0.12, -0.1, 0.01)], 0.006, "blue")
def flashlight():
    Cy("body", 0.022, 0.2, (0, 0, 0.022), "black", rot=RY); Cy("head", 0.032, 0.06, (0.12, 0, 0.032), "black", rot=RY); Cy("lens", 0.028, 0.005, (0.152, 0, 0.032), "glass", rot=RY)
def backpack():
    S("bag", (0, 0, 0.24), 0.2, "canvas", scale=(0.9, 0.55, 1.2)); B("pocket", (0.22, 0.08, 0.16), (0, -0.12, 0.17), "canvas", 0.04)
    for x in (-0.08, 0.08): Tu(f"strap{x}", [(x, 0.08, 0.42), (x, 0.16, 0.25), (x, 0.08, 0.08)], 0.015, "black")
def chair():
    Cy("base", 0.03, 0.4, (0, 0, 0.22), "chrome")
    for k in range(5):
        a = k * 2 * math.pi / 5; Tu(f"leg{k}", [(0, 0, 0.06), (0.28 * math.cos(a), 0.28 * math.sin(a), 0.04)], 0.018, "black"); S(f"wheel{k}", (0.28 * math.cos(a), 0.28 * math.sin(a), 0.03), 0.03, "black")
    B("seat", (0.48, 0.46, 0.09), (0, 0, 0.47), "black", 0.04); B("back", (0.44, 0.07, 0.52), (0, 0.22, 0.8), "black", 0.04)
def lamp():
    Cy("base", 0.09, 0.02, (0, 0, 0.01), "black"); Tu("arm", [(0, 0, 0.02), (0.05, 0, 0.22), (0.15, 0, 0.3)], 0.01, "black")
    o = L("shade", [(0.08, -0.07), (0.02, 0.0)], "yellow"); o.location = (0.17, 0, 0.32)
def pallet():
    for x in (-0.45, 0, 0.45): B(f"rail{x}", (0.1, 1.0, 0.1), (x, 0, 0.05), "wood", 0.01)
    B("deck", (1.0, 1.0, 0.03), (0, 0, 0.115), "wood", 0.01)
    for i in range(3):
        for j in range(3):
            Cy(f"b{i}{j}", 0.12, 0.6, (-0.3 + i * 0.3, -0.3 + j * 0.3, 0.43), "glass"); Cy(f"c{i}{j}", 0.04, 0.05, (-0.3 + i * 0.3, -0.3 + j * 0.3, 0.75), "blue")
def batteries():
    for k in range(4): Cy(f"b{k}", 0.008, 0.05, (-0.03 + k * 0.02, 0, 0.025), "orange" if k % 2 else "black")
def can():
    Cy("can", 0.038, 0.11, (0, 0, 0.055), "silver"); Cy("label", 0.0385, 0.07, (0, 0, 0.055), "red", bevel=0)
def badge():
    B("card", (0.054, 0.086, 0.004), (0, 0, 0.002), "white", 0.004); B("photo", (0.02, 0.025, 0.001), (-0.01, 0.015, 0.0045), "blue", 0.001); B("stripe", (0.054, 0.012, 0.001), (0, -0.03, 0.0045), "red", 0.001)
def crate():
    B("crate", (0.6, 0.45, 0.42), (0, 0, 0.21), "wood", 0.01)
    for z in (0.05, 0.37): B(f"band{z}", (0.62, 0.47, 0.03), (0, 0, z), "darkwood", 0.004)
    B("stencil", (0.3, 0.002, 0.12), (0, -0.226, 0.21), "black", 0.001)
def toolbox():
    B("box", (0.45, 0.2, 0.2), (0, 0, 0.1), "red", 0.01); B("lid", (0.46, 0.21, 0.04), (0, 0, 0.21), "red", 0.01)
    Tu("handle", [(-0.1, 0, 0.23), (-0.1, 0, 0.28), (0.1, 0, 0.28), (0.1, 0, 0.23)], 0.01, "black"); B("latch", (0.04, 0.01, 0.03), (0, -0.105, 0.19), "chrome", 0.002)
def jerrycan():
    B("can", (0.35, 0.16, 0.45), (0, 0, 0.225), "green", 0.03); B("x", (0.25, 0.002, 0.3), (0, -0.081, 0.2), "green", 0.02)
    Cy("spout", 0.02, 0.06, (0.12, 0, 0.47), "black"); B("handle", (0.2, 0.04, 0.05), (-0.04, 0, 0.47), "green", 0.01)
def machine():
    B("block", (0.3, 0.22, 0.2), (0, 0, 0.1), "grey", 0.015); Cy("drum", 0.08, 0.24, (0.05, 0, 0.18), "steel", rot=R90)
    for k in range(4): Cy(f"bolt{k}", 0.012, 0.02, (-0.12 + k * 0.08, -0.115, 0.15), "brass", rot=R90)
    Tu("hose", [(-0.15, 0, 0.12), (-0.25, -0.1, 0.06), (-0.2, -0.2, 0.02)], 0.015, "black")
def mask():
    S("face", (0, 0, 0.03), 0.1, "beige", scale=(0.8, 0.35, 1.0))
    for x in (-0.035, 0.035): S(f"eye{x}", (x, -0.03, 0.05), 0.016, "black")
def gauge():
    Cy("case", 0.06, 0.03, (0, 0, 0.015), "brass"); Cy("face", 0.052, 0.003, (0, 0, 0.031), "white"); B("needle", (0.004, 0.04, 0.002), (0.01, 0.01, 0.034), "red", 0.001, rot=(0, 0, 0.6)); Cy("stem", 0.012, 0.05, (0, -0.07, 0.015), "brass", rot=R90)
def valve():
    Cy("body", 0.04, 0.12, (0, 0, 0.04), "brass", rot=RY); Cy("stem", 0.01, 0.08, (0, 0, 0.1), "brass"); T("wheel", (0, 0, 0.14), 0.05, 0.008, "red")
    for k in range(3): B(f"spoke{k}", (0.1, 0.008, 0.008), (0, 0, 0.14), "red", 0.002, rot=(0, 0, k * math.pi / 3))
def jar():
    L("jar", [(0.05, 0.0), (0.06, 0.02), (0.06, 0.16), (0.045, 0.19), (0.045, 0.21)], "glass"); Cy("lid", 0.05, 0.02, (0, 0, 0.215), "brass")
    S("thing", (0, 0, 0.09), 0.04, "flesh", scale=(1, 1, 1.3))
def pipe():
    Cy("pipe", 0.05, 1.0, (0, 0, 0.05), "copper", rot=RY)
    for x in (-0.48, 0.48): Cy(f"flange{x}", 0.075, 0.03, (x, 0, 0.075), "copper", rot=RY)
def fuse():
    Cy("glass", 0.012, 0.06, (0, 0, 0.012), "glass", rot=RY)
    for x in (-0.03, 0.03): Cy(f"cap{x}", 0.013, 0.012, (x, 0, 0.013), "silver", rot=RY)
def radio():
    B("case", (0.32, 0.12, 0.2), (0, 0, 0.1), "wood", 0.02); Cy("speaker", 0.06, 0.005, (-0.07, -0.062, 0.1), "cream", rot=R90)
    for x in (0.06, 0.11): Cy(f"knob{x}", 0.015, 0.02, (x, -0.065, 0.08), "black", rot=R90)
    B("dial", (0.1, 0.004, 0.04), (0.08, -0.061, 0.14), "cream", 0.004); Tu("aerial", [(0.13, 0.03, 0.2), (0.2, 0.05, 0.45)], 0.003, "chrome")
def coil():
    Cy("core", 0.05, 0.25, (0, 0, 0.06), "grey", rot=RY)
    for k in range(9): T(f"turn{k}", (-0.1 + k * 0.025, 0, 0.06), 0.06, 0.012, "copper", rot=RY)
def stapler():
    B("base", (0.17, 0.045, 0.015), (0, 0, 0.0075), "black", 0.005); B("arm", (0.16, 0.04, 0.03), (0.005, 0, 0.035), "red", 0.008, rot=(0, -0.08, 0))
def photo():
    B("frame", (0.13, 0.015, 0.18), (0, 0, 0.09), "darkwood", 0.004, rot=(0.3, 0, 0)); B("pic", (0.1, 0.004, 0.14), (0, -0.01, 0.09), "pink", 0.002, rot=(0.3, 0, 0))
def tower():
    B("case", (0.2, 0.45, 0.42), (0, 0, 0.21), "beige", 0.01); B("drive", (0.15, 0.004, 0.04), (0, -0.226, 0.35), "grey", 0.003); Cy("power", 0.012, 0.006, (0.06, -0.226, 0.08), "green", rot=R90)
    for k in range(5): B(f"vent{k}", (0.14, 0.003, 0.008), (0, -0.226, 0.15 + k * 0.025), "grey", 0.001)
def coffee():
    B("body", (0.24, 0.26, 0.36), (0, 0, 0.18), "black", 0.02); B("cut", (0.2, 0.12, 0.14), (0, -0.09, 0.08), "steel", 0.01)
    o = L("pot", [(0.06, 0.02), (0.07, 0.08), (0.05, 0.13)], "glass"); o.location = (0, -0.1, 0.0)
def plant():
    L("pot", [(0.08, 0.0), (0.1, 0.18), (0.11, 0.2)], "orange"); Cy("soil", 0.098, 0.01, (0, 0, 0.19), "soil")
    for k in range(7):
        a = k * 2 * math.pi / 7; S(f"leaf{k}", (0.09 * math.cos(a), 0.09 * math.sin(a), 0.3 + 0.04 * (k % 3)), 0.07, "green", scale=(1.6, 0.6, 0.3))
def cabinet():
    B("body", (0.46, 0.6, 1.3), (0, 0, 0.65), "grey", 0.01)
    for k in range(4): B(f"drawer{k}", (0.42, 0.01, 0.28), (0, -0.305, 0.17 + k * 0.32), "steel", 0.005); B(f"pull{k}", (0.12, 0.02, 0.02), (0, -0.315, 0.25 + k * 0.32), "chrome", 0.004)
def jewelry():
    T("ring", (0, 0, 0.004), 0.012, 0.003, "gold"); S("gem", (0, -0.012, 0.012), 0.006, "crystal")
    Tu("chain", [(0.02, 0, 0.002), (0.06, 0.03, 0.002), (0.09, 0.0, 0.002), (0.07, -0.04, 0.002), (0.03, -0.03, 0.002)], 0.0025, "gold")
def gramophone():
    B("box", (0.32, 0.32, 0.14), (0, 0, 0.07), "wood", 0.01); Cy("plate", 0.13, 0.01, (0, 0, 0.145), "black")
    Tu("neck", [(0.12, 0.12, 0.15), (0.13, 0.13, 0.3), (0.05, 0.05, 0.4)], 0.015, "brass")
    o = L("horn", [(0.02, 0.0), (0.06, 0.12), (0.18, 0.24)], "brass"); o.location = (0.0, 0.0, 0.38); o.rotation_euler = (1.0, 0, 0.8)
def bottle():
    L("bottle", [(0.04, 0.0), (0.042, 0.2), (0.016, 0.26), (0.014, 0.32)], "green"); Cy("foil", 0.016, 0.04, (0, 0, 0.31), "gold"); Cy("label", 0.0425, 0.07, (0, 0, 0.1), "cream", bevel=0)
def tray():
    Cy("tray", 0.2, 0.012, (0, 0, 0.006), "silver"); T("rim", (0, 0, 0.012), 0.2, 0.008, "silver")
    for k in range(3): Cy(f"fork{k}", 0.004, 0.16, (-0.05 + k * 0.05, 0, 0.02), "silver", rot=RY)
def book():
    B("cover", (0.17, 0.24, 0.045), (0, 0, 0.0225), "darkwood", 0.004); B("pages", (0.16, 0.225, 0.038), (0.006, 0, 0.0225), "paper", 0.002)
def clock():
    B("case", (0.5, 0.3, 1.9), (0, 0, 0.95), "darkwood", 0.02); B("hood", (0.56, 0.34, 0.3), (0, 0, 1.75), "darkwood", 0.03)
    Cy("face", 0.17, 0.01, (0, -0.172, 1.72), "cream", rot=R90); B("hand", (0.012, 0.004, 0.12), (0.02, -0.18, 1.75), "black", 0.002, rot=(0, 0.5, 0))
    B("window", (0.26, 0.004, 0.7), (0, -0.152, 0.9), "glass", 0.004); Cy("pendulum", 0.06, 0.01, (0, -0.1, 0.75), "brass", rot=R90)
def camera():
    B("body", (0.13, 0.06, 0.08), (0, 0, 0.04), "black", 0.008); Cy("lens", 0.028, 0.05, (0, -0.05, 0.04), "black", rot=R90); Cy("glass", 0.022, 0.004, (0, -0.076, 0.04), "glass", rot=R90); B("flash", (0.03, 0.02, 0.02), (0.04, 0, 0.09), "silver", 0.004)
def batterybox():
    B("box", (0.3, 0.18, 0.2), (0, 0, 0.1), "black", 0.01)
    for x in (-0.1, 0.1): Cy(f"post{x}", 0.015, 0.03, (x, 0, 0.215), "red" if x > 0 else "black"); B("label", (0.18, 0.002, 0.08), (0, -0.091, 0.11), "yellow", 0.002)
def lantern():
    Cy("base", 0.08, 0.03, (0, 0, 0.015), "brass"); Cy("globe", 0.06, 0.18, (0, 0, 0.12), "glass"); Cy("cap", 0.07, 0.04, (0, 0, 0.23), "brass"); T("bail", (0, 0, 0.28), 0.05, 0.005, "brass", rot=R90)
    S("flame", (0, 0, 0.1), 0.025, "yellow", scale=(1, 1, 1.6))
def helmet():
    S("helm", (0, 0, 0.2), 0.18, "brass"); Cy("front", 0.08, 0.02, (0, -0.18, 0.21), "glass", rot=R90); T("rim", (0, -0.18, 0.21), 0.085, 0.012, "brass", rot=R90)
    Cy("collar", 0.17, 0.06, (0, 0, 0.03), "brass")
def bell():
    L("bell", [(0.16, 0.0), (0.15, 0.03), (0.1, 0.15), (0.09, 0.26), (0.05, 0.3)], "brass"); T("crown", (0, 0, 0.32), 0.03, 0.01, "brass", rot=R90)
def crystal():
    for k in range(5):
        a = k * 1.3; h = 0.12 + 0.05 * (k % 3); F.cone(f"c{k}", (0.03 * math.cos(a), 0.03 * math.sin(a), 0.0), (0.06 * math.cos(a), 0.06 * math.sin(a), h), 0.025, 0.0, "crystal") if hasattr(F, "cone") else S(f"c{k}", (0.03 * math.cos(a), 0.03 * math.sin(a), h / 2), 0.03, "crystal", scale=(0.6, 0.6, 2.0))
def toy():
    S("body", (0, 0, 0.07), 0.06, "fabric", scale=(1, 0.8, 1.1)); S("head", (0, 0, 0.16), 0.045, "fabric")
    for x in (-0.03, 0.03): S(f"ear{x}", (x, 0, 0.2), 0.018, "fabric"); S(f"eye{x}", (x * 0.6, -0.04, 0.17), 0.007, "black")
def remote():
    B("body", (0.05, 0.17, 0.02), (0, 0, 0.01), "black", 0.008)
    for k in range(6): Cy(f"btn{k}", 0.006, 0.004, (-0.012 + (k % 2) * 0.024, -0.04 + (k // 2) * 0.03, 0.021), "red" if k == 0 else "grey")
def tv():
    B("case", (0.6, 0.45, 0.48), (0, 0, 0.24), "wood", 0.04); B("screen", (0.42, 0.01, 0.34), (-0.05, -0.226, 0.26), "glass", 0.04)
    for z in (0.32, 0.22): Cy(f"knob{z}", 0.02, 0.02, (0.24, -0.23, z), "black", rot=R90)
    Tu("ears", [(-0.1, 0, 0.48), (-0.25, 0, 0.75)], 0.004, "chrome"); Tu("ears2", [(0.05, 0, 0.48), (0.18, 0, 0.75)], 0.004, "chrome")
def piano():
    B("body", (1.5, 0.6, 1.0), (0, 0, 0.6), "black", 0.03); B("keys", (1.3, 0.2, 0.04), (0, -0.38, 0.72), "white", 0.005)
    for k in range(18): B(f"bk{k}", (0.025, 0.12, 0.03), (-0.62 + k * 0.072, -0.34, 0.75), "black", 0.003)
    for x in (-0.65, 0.65): B(f"leg{x}", (0.08, 0.08, 0.3), (x, -0.3, 0.15), "black", 0.01)
def sack():
    S("sack", (0, 0, 0.22), 0.22, "burlap", scale=(1.1, 0.8, 1.0)); Cy("tie", 0.05, 0.05, (0, 0, 0.44), "burlap"); T("rope", (0, 0, 0.43), 0.05, 0.008, "darkwood")
def quilt():
    for i in range(3):
        for j in range(3): B(f"q{i}{j}", (0.18, 0.18, 0.06), (-0.18 + i * 0.18, -0.18 + j * 0.18, 0.03 + 0.03 * ((i + j) % 2)), ["red", "cream", "blue"][(i + 2 * j) % 3], 0.02)
def frame():
    B("frame", (0.9, 0.06, 0.7), (0, 0, 0.35), "gold", 0.01); B("canvas", (0.78, 0.002, 0.58), (0, -0.031, 0.35), "blue", 0.001)
    S("sun", (0.2, -0.033, 0.48), 0.06, "yellow", scale=(1, 0.05, 1)); B("hill", (0.6, 0.003, 0.15), (-0.05, -0.034, 0.18), "green", 0.04)
def cake():
    Cy("plate", 0.18, 0.01, (0, 0, 0.005), "white"); Cy("cake", 0.14, 0.12, (0, 0, 0.07), "pink"); Cy("icing", 0.145, 0.02, (0, 0, 0.13), "white")
    for k in range(5): a = k * 2 * math.pi / 5; Cy(f"candle{k}", 0.006, 0.06, (0.08 * math.cos(a), 0.08 * math.sin(a), 0.17), "blue"); S(f"fl{k}", (0.08 * math.cos(a), 0.08 * math.sin(a), 0.21), 0.007, "yellow")
def instruments():
    B("case", (0.32, 0.22, 0.08), (0, 0, 0.04), "grey", 0.01); B("panel", (0.28, 0.002, 0.06), (0, -0.111, 0.045), "black", 0.002)
    for x in (-0.08, 0.0, 0.08): Cy(f"dial{x}", 0.022, 0.004, (x, -0.113, 0.045), "white", rot=R90)
def die():
    B("die", (0.05, 0.05, 0.05), (0, 0, 0.025), "red", 0.008)
    for p in ((0, -0.026, 0.025), (0.026, 0, 0.012), (0.026, 0, 0.038), (0, 0, 0.051)): S(f"pip{p}", p, 0.005, "white")
def compass():
    Cy("case", 0.04, 0.015, (0, 0, 0.008), "brass"); Cy("face", 0.035, 0.002, (0, 0, 0.016), "cream"); B("needle", (0.004, 0.06, 0.002), (0, 0, 0.018), "red", 0.001)
def exitsign():
    B("box", (0.36, 0.06, 0.16), (0, 0, 0.08), "white", 0.006); B("face", (0.3, 0.002, 0.1), (0, -0.031, 0.08), "green", 0.002)
def key():
    T("bow", (0, 0, 0.004), 0.018, 0.005, "brass"); B("shaft", (0.08, 0.008, 0.008), (0.06, 0, 0.004), "brass", 0.002); B("bit", (0.012, 0.02, 0.008), (0.09, 0.012, 0.004), "brass", 0.002)
def luggage():
    B("case", (0.5, 0.22, 0.38), (0, 0, 0.19), "leather", 0.03); B("strap", (0.52, 0.23, 0.03), (0, 0, 0.19), "darkwood", 0.005); Tu("handle", [(-0.06, 0, 0.38), (-0.06, 0, 0.42), (0.06, 0, 0.42), (0.06, 0, 0.38)], 0.012, "darkwood")
def safe():
    B("body", (0.55, 0.55, 0.65), (0, 0, 0.325), "grey", 0.03); Cy("dial", 0.06, 0.03, (0.05, -0.29, 0.4), "silver", rot=R90); B("handle", (0.04, 0.03, 0.14), (-0.12, -0.29, 0.38), "silver", 0.008)
def skull_tooth():
    F.cone("tooth", (0, 0, 0), (0, 0, 0.06), 0.015, 0.002, "white") if hasattr(F, "cone") else S("tooth", (0, 0, 0.03), 0.015, "white", scale=(1, 1, 2.2))


BUILDS = {n: f for n, f in globals().items() if callable(f) and n in (
    "wallet tape keys flashlight backpack chair lamp pallet batteries can badge crate toolbox jerrycan machine mask gauge valve jar pipe "
    "fuse radio coil stapler photo tower coffee plant cabinet jewelry gramophone bottle tray book clock camera batterybox lantern helmet "
    "bell crystal toy remote tv piano sack quilt frame cake instruments die compass exitsign key luggage safe skull_tooth").split()}


def build(name, out):
    C.reset(); setup(); F.PARTS.clear()
    BUILDS[name]()
    C.export_glb(F.PARTS, os.path.join(out, f"{name}.glb"))


if not os.environ.get("ARTGEN_IMPORT_PROPS"):   # (noclip_world.py imports the palette)
    a = C.args()
    only = a[a.index("--only") + 1].split(",") if "--only" in a else None
    out = C.out_dir()
    for n in BUILDS:
        if only and n not in only:
            continue
        build(n, out)
