# NOCLIP's fittings and creatures (the playtest, 2026-10-05: "the enemies are still blockier than they should be"):
# the Threshold Lab's machines (portal ring, console, drop-off bin, vending machine, bunks, generator, monitor wall,
# archive shelves, blast door, breaker) and the level exits, and the creatures that aren't people (the Smiler's face,
# the Clump, the Deathmoth (a body and a wing the game flaps), the giant spider, the Sentry, the Seer's eye, the
# Leviathan). Flat named materials; facing the room (Blender -Y, the game's +Z); on the floor at the origin.
#     blender -b --factory-startup -P tools/artgen/noclip_world.py -- --out assets/noclip/world [--only ring,sentry]
import os
import sys
import math
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
os.environ["ARTGEN_IMPORT"] = "1"; os.environ["ARTGEN_IMPORT_PROPS"] = "1"
import common as C
import fowl_props as F
import noclip_props as P
bpy = C.bpy

EXTRA = {"hazard": ((0.9, 0.7, 0.05), 0.5), "labsteel": ((0.45, 0.47, 0.5), 0.35, 0.8), "screen": ((0.1, 0.35, 0.2), 0.1),
         "mattress": ((0.75, 0.73, 0.66), 0.9), "pillow": ((0.9, 0.9, 0.88), 0.9), "vend": ((0.62, 0.08, 0.06), 0.4),
         "eyewhite": ((0.95, 0.94, 0.9), 0.3), "iris": ((0.25, 0.08, 0.06), 0.3), "tooth": ((0.98, 0.97, 0.92), 0.3),
         "skin": ((0.62, 0.55, 0.5), 0.6), "moth": ((0.42, 0.36, 0.28), 0.9), "mothwing": ((0.66, 0.6, 0.48), 0.9),
         "chitin": ((0.12, 0.1, 0.09), 0.4), "visor": ((0.9, 0.08, 0.06), 0.2), "eel": ((0.06, 0.1, 0.12), 0.4), "glow": ((0.6, 0.85, 1.0), 0.2)}


def setup():
    P.setup()
    for n, v in EXTRA.items():
        F.mat(n, v[0], v[1], v[2] if len(v) > 2 else 0.0)


B, S, Cy, L, T, Tu = F.box, F.sphere, F.cyl, F.lathe, F.torus, F.tube
R90 = (math.pi / 2, 0, 0); RY = (0, math.pi / 2, 0)


# ---------------------------------------------------------------- the Lab
def ring():   # (the portal ring: its centre 1.5 m up; the game turns it about its axis while it charges)
    T("ring", (0, 0, 1.5), 1.45, 0.17, "labsteel", rot=R90)
    T("inner", (0, 0, 1.5), 1.27, 0.05, "brass", rot=R90)
    for k in range(8):
        a = k * math.pi / 4; c = (1.45 * math.cos(a), 0, 1.5 + 1.45 * math.sin(a))
        B(f"clamp{k}", (0.3, 0.46, 0.22), c, "brass" if k % 2 else "labsteel", 0.03, rot=(0, -a, 0))
    for s in (-1, 1):
        B(f"foot{s}", (0.5, 0.9, 0.18), (s * 0.9, 0, 0.09), "labsteel", 0.03); Tu(f"strut{s}", [(s * 0.9, 0, 0.18), (s * 1.15, 0, 0.6)], 0.08, "labsteel")
        Tu(f"cable{s}", [(s * 1.6, 0.3, 1.2), (s * 1.9, 0.6, 0.6), (s * 1.7, 0.9, 0.0)], 0.04, "black")
def desk():
    B("desk", (1.8, 0.7, 0.85), (0, 0, 0.425), "labsteel", 0.03); B("top", (1.86, 0.76, 0.05), (0, 0, 0.87), "black", 0.01)
    for k in range(3):
        x = -0.6 + k * 0.6; B(f"crt{k}", (0.42, 0.4, 0.36), (x, 0.1, 1.08), "beige", 0.04); B(f"scr{k}", (0.32, 0.01, 0.26), (x, -0.105, 1.09), "screen", 0.02)
    B("keys", (0.5, 0.18, 0.03), (0, -0.2, 0.91), "plastic", 0.005)
    for k in range(4): Cy(f"lever{k}", 0.012, 0.18, (0.55 + k * 0.08, -0.25, 0.97), "chrome"); S(f"knob{k}", (0.55 + k * 0.08, -0.25, 1.06), 0.022, "red")
def bin_():
    B("bin", (1.3, 1.0, 0.8), (0, 0, 0.4), "labsteel", 0.03)
    for x in (-0.45, 0, 0.45): B(f"rib{x}", (0.06, 1.02, 0.8), (x, 0, 0.4), "labsteel", 0.01)
    B("lid", (1.32, 0.06, 0.9), (0, 0.55, 1.15), "labsteel", 0.02, rot=(0.35, 0, 0))
    for k in range(6): B(f"stripe{k}", (0.12, 0.01, 0.12), (-0.55 + k * 0.22, -0.505, 0.65), "hazard" if k % 2 else "black", 0.002, rot=(0, 0.785, 0))
def vending():
    B("body", (0.9, 0.8, 1.9), (0, 0, 0.95), "vend", 0.03); B("window", (0.55, 0.02, 1.2), (-0.1, -0.4, 1.15), "glass", 0.01)
    for k in range(4):
        B(f"shelf{k}", (0.5, 0.3, 0.02), (-0.1, -0.22, 0.65 + k * 0.3), "steel", 0.003)
        for j in range(4): Cy(f"can{k}{j}", 0.03, 0.12, (-0.28 + j * 0.12, -0.25, 0.72 + k * 0.3), ["red", "blue", "green", "orange"][(k + j) % 4])
    B("panel", (0.2, 0.02, 0.5), (0.3, -0.405, 1.2), "black", 0.01); B("slot", (0.4, 0.02, 0.12), (-0.1, -0.405, 0.3), "black", 0.01)
def bunk():
    for x in (-0.45, 0.45):
        for y in (-0.95, 0.95): Cy(f"post{x}{y}", 0.03, 1.7, (x, y, 0.85), "labsteel")
    for z in (0.35, 1.25):
        B(f"frame{z}", (0.96, 2.0, 0.06), (0, 0, z), "labsteel", 0.01); B(f"mat{z}", (0.88, 1.92, 0.14), (0, 0, z + 0.1), "mattress", 0.05)
        B(f"pil{z}", (0.6, 0.3, 0.1), (0, 0.75, z + 0.22), "pillow", 0.04); B(f"blanket{z}", (0.9, 1.0, 0.06), (0, -0.4, z + 0.18), "blue", 0.03)
def generator():
    B("frame", (1.3, 1.0, 0.1), (0, 0, 0.05), "black", 0.01); B("engine", (1.0, 0.8, 0.8), (0, 0, 0.5), "hazard", 0.05)
    Cy("tank", 0.28, 1.1, (0, 0.2, 1.05), "hazard", rot=RY); Cy("cap", 0.06, 0.06, (0.3, 0.2, 1.35), "black")
    Tu("exhaust", [(-0.45, 0.3, 0.8), (-0.6, 0.3, 1.2), (-0.6, 0.3, 1.6)], 0.05, "rust"); B("panel", (0.4, 0.02, 0.3), (0.2, -0.41, 0.6), "labsteel", 0.01)
    for k in range(3): Cy(f"g{k}", 0.04, 0.01, (0.08 + k * 0.12, -0.42, 0.65), "white", rot=R90)
def monitors():
    B("rack", (0.4, 1.9, 1.6), (0.1, 0, 0.8), "black", 0.02)
    for k in range(6):
        y = -0.6 + (k % 3) * 0.6; z = 0.9 + (k // 3) * 0.55; B(f"crt{k}", (0.4, 0.5, 0.42), (-0.12, y, z), "beige", 0.04); B(f"scr{k}", (0.01, 0.38, 0.3), (-0.325, y, z), "screen", 0.02)
def archive():
    B("shelf", (0.6, 1.8, 1.9), (0, 0, 0.95), "labsteel", 0.02)
    for z in (0.3, 0.75, 1.2, 1.65):
        B(f"board{z}", (0.62, 1.82, 0.03), (0, 0, z - 0.2), "labsteel", 0.005)
        for k in range(6): B(f"box{z}{k}", (0.4, 0.26, 0.3), (-0.05, -0.75 + k * 0.3, z - 0.03), "cream" if k % 2 else "paper", 0.01)
def blastdoor():
    B("door", (2.4, 0.3, 2.6), (0, 0, 1.3), "labsteel", 0.05)
    for k in range(10): B(f"hz{k}", (0.24, 0.31, 0.2), (-1.08 + k * 0.24, 0, 2.45), "hazard" if k % 2 else "black", 0.002)
    T("wheel", (0, -0.17, 1.3), 0.3, 0.035, "brass", rot=R90)
    for k in range(3): B(f"sp{k}", (0.6, 0.04, 0.04), (0, -0.17, 1.3), "brass", 0.005, rot=(0, k * math.pi / 3, 0))
    for x in (-1.0, 1.0): B(f"bolt{x}", (0.12, 0.32, 2.2), (x, 0, 1.2), "steel", 0.02)
def exitdoor():
    B("frame", (1.2, 0.25, 2.3), (0, 0.04, 1.15), "wood", 0.02); B("door", (0.95, 0.06, 2.1), (0, -0.06, 1.05), "darkwood", 0.02)
    Cy("knob", 0.03, 0.06, (0.35, -0.1, 1.0), "brass", rot=R90)
    B("sign", (0.5, 0.08, 0.18), (0, -0.05, 2.45), "white", 0.01); B("signface", (0.42, 0.01, 0.12), (0, -0.095, 2.45), "green", 0.005)
def breaker():
    B("box", (0.4, 0.15, 0.55), (0, 0, 1.2), "grey", 0.02); B("lever", (0.06, 0.12, 0.2), (0, -0.12, 1.25), "red", 0.01); B("plate", (0.3, 0.01, 0.06), (0, -0.08, 1.42), "hazard", 0.005)


# ---------------------------------------------------------------- the creatures
def smiler():   # (just the face in the dark: eyes and teeth; drawn glowing)
    for x in (-0.18, 0.18): S(f"eye{x}", (x, 0, 1.72), 0.12, "eyewhite", scale=(1.1, 0.4, 0.7)); S(f"pupil{x}", (x, -0.05, 1.72), 0.035, "black")
    for k in range(13):
        x = -0.36 + k * 0.06; h = 0.07 - abs(k - 6) * 0.006
        B(f"tooth{k}", (0.05, 0.03, h), (x, 0, 1.45 + abs(k - 6) ** 1.6 * 0.006), "tooth", 0.008)
def clump():
    for k in range(9):
        a = k * 2.4; S(f"lump{k}", (0.35 * math.cos(a), 0.3 * math.sin(a), 0.5 + 0.25 * math.sin(k)), 0.32, "skin", scale=(1.0, 0.8, 1.2))
    for k in range(8):
        a = k * 0.8; base = (0.3 * math.cos(a), 0.3 * math.sin(a), 0.6)
        Tu(f"arm{k}", [base, (0.75 * math.cos(a), 0.75 * math.sin(a), 0.7 + 0.3 * math.sin(k)), (1.0 * math.cos(a), 1.0 * math.sin(a), 0.05)], 0.06, "skin")
        S(f"hand{k}", (1.0 * math.cos(a), 1.0 * math.sin(a), 0.06), 0.08, "skin", scale=(1.4, 1.0, 0.5))
def moth_body():
    S("thorax", (0, 0, 0), 0.22, "moth", scale=(1.0, 1.0, 1.0)); S("abdomen", (0, 0.42, -0.05), 0.2, "moth", scale=(0.9, 1.9, 0.9)); S("head", (0, -0.25, 0.04), 0.14, "moth")
    for x in (-0.08, 0.08): S(f"eye{x}", (x, -0.34, 0.08), 0.06, "black"); Tu(f"ant{x}", [(x * 0.6, -0.34, 0.12), (x * 3, -0.62, 0.35)], 0.012, "moth")
    for k in range(3):
        for x in (-1, 1): Tu(f"leg{k}{x}", [(x * 0.1, -0.1 + k * 0.12, -0.12), (x * 0.3, -0.15 + k * 0.12, -0.3)], 0.015, "chitin")
def moth_wing():   # (one wing reaching along +x from the body's side; the game mirrors and flaps it)
    S("wing", (0.55, 0, 0), 0.5, "mothwing", scale=(1.15, 0.9, 0.04)); S("spot", (0.6, -0.05, 0.02), 0.12, "black", scale=(1, 1, 0.12)); S("ring", (0.6, -0.05, 0.025), 0.06, "eyewhite", scale=(1, 1, 0.12))
def spider():
    S("abdomen", (0, 0.45, 0.65), 0.45, "chitin", scale=(1.0, 1.25, 0.85)); S("cephalo", (0, -0.25, 0.55), 0.3, "chitin", scale=(1.0, 1.1, 0.8))
    for k in range(6): S(f"eye{k}", (-0.12 + (k % 3) * 0.12, -0.5, 0.62 + (k // 3) * 0.08), 0.035, "visor")
    for side in (-1, 1):
        for k in range(4):
            y = -0.35 + k * 0.22; Tu(f"leg{side}{k}", [(side * 0.2, y, 0.55), (side * 0.75, y + (k - 1.5) * 0.15, 0.95), (side * 1.25, y + (k - 1.5) * 0.35, 0.0)], 0.045, "chitin")
def sentry():
    Cy("base", 0.45, 0.2, (0, 0, 0.1), "labsteel"); Cy("body", 0.32, 0.9, (0, 0, 0.65), "white"); B("chest", (0.4, 0.06, 0.3), (0, -0.33, 0.8), "labsteel", 0.02)
    S("head", (0, 0, 1.28), 0.33, "white", scale=(1, 1, 0.8)); B("visor", (0.42, 0.06, 0.09), (0, -0.3, 1.3), "visor", 0.03)
    for x in (-1, 1): Tu(f"arm{x}", [(x * 0.32, 0, 1.0), (x * 0.5, -0.1, 0.75), (x * 0.45, -0.3, 0.6)], 0.06, "labsteel")
def seer():
    S("eye", (0, 0, 0), 0.3, "eyewhite", scale=(1, 0.5, 0.75)); S("iris", (0, -0.13, 0), 0.12, "iris", scale=(1, 0.3, 1)); S("pupil", (0, -0.165, 0), 0.05, "black", scale=(1, 0.3, 1))
    S("lidtop", (0, -0.02, 0.16), 0.32, "skin", scale=(1.05, 0.55, 0.35)); S("lidbot", (0, -0.02, -0.17), 0.32, "skin", scale=(1.05, 0.55, 0.3))
def leviathan():   # (a vast eel-like shadow just under the surface)
    for k in range(12):
        x = -14 + k * 2.6; r = 2.4 * math.sin(math.pi * (k + 0.5) / 12) + 0.3
        S(f"seg{k}", (x, 0, -1.5 + math.sin(k * 0.7) * 0.6), r, "eel", scale=(1.3, 1.0, 0.8))
    for x in (-15, -14.5): S(f"eye{x}", (x, -1.2 if x < -14.8 else 1.2, -0.8), 0.35, "glow")


BUILDS = {"ring": ring, "desk": desk, "bin": bin_, "vending": vending, "bunk": bunk, "generator": generator, "monitors": monitors,
          "archive": archive, "blastdoor": blastdoor, "exitdoor": exitdoor, "breaker": breaker,
          "smiler": smiler, "clump": clump, "moth_body": moth_body, "moth_wing": moth_wing, "spider": spider, "sentry": sentry, "seer": seer, "leviathan": leviathan}


def build(name, out):
    C.reset(); setup(); F.PARTS.clear()
    BUILDS[name]()
    C.export_glb(F.PARTS, os.path.join(out, f"{name}.glb"))


if not os.environ.get("ARTGEN_IMPORT_WORLD"):
    a = C.args()
    only = a[a.index("--only") + 1].split(",") if "--only" in a else None
    out = C.out_dir()
    for n in BUILDS:
        if only and n not in only:
            continue
        build(n, out)
