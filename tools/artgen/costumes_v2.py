# Costumes, second design (the user, 2026-10-06: "the costumes for the trawl and red tide still look underdeveloped and
# wouldn't be something my friends would wear"). The first set were padded mascot onesies; these are outfits: armour
# with layered plates and trims, helmets with visors, long coats and capes, glowing seams, and a held weapon or tool,
# each a whole look on its theme (the Lobster Knight, the Shark Hunter, the Kraken Lord, Captain Nemo...). Same rig,
# same files (assets/shared/costumes/costume_<name>.glb), same ids, so saves and the catalogue keep working.
#     blender -b --factory-startup -P tools/artgen/costumes_v2.py -- --out assets/shared/costumes [--only lobster,shark]
# Frame: x forward, y to the left, z up (crew.py's joints); every piece rides one bone, the base layer is soft.

import os
import sys
import math
import bmesh
from mathutils import Vector
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
os.environ["ARTGEN_IMPORT"] = "1"
import common as C
import crew as K
import costumes as CO
bpy = C.bpy
COL = CO.COL
HC = CO.HC
SIDES = ((1, "L"), (-1, "R"))


def V(*a):
    return Vector(a)


# ---------------------------------------------------------------- the outfit kit (over costumes.Kit)
def two_sided(k, name, verts, faces, mat, bone):
    bm = bmesh.new()
    front = [bm.verts.new(v) for v in verts]
    back = [bm.verts.new(v) for v in verts]   # (its own vertices: bmesh refuses a second face on the same ones)
    for f in faces:
        bm.faces.new([front[i] for i in f])
        bm.faces.new([back[i] for i in reversed(f)])
    return k.add(k._obj(name, bm), mat, bone)


def ring(k, name, c, r, h, mat, bone, segs=28, flare=0.0):
    """A band round the body: radius r at its middle, h tall, a little flare at the bottom."""
    return k.lathe(name, [(r + flare, -h / 2), (r * 1.04, 0), (r, h / 2)], c, mat, bone, segs)


def base(k, mat, puff=1.08):
    k.suit(mat, puff)


def gloves(k, mat):
    k.mitts(mat)
    for s, sd in SIDES:   # (a cuff over the wrist)
        h = k.j(f"hand.{sd}"); f = k.j(f"forearm.{sd}"); d = (h - f).normalized()
        k.cone(f"cuff{sd}", h - d * 0.07, h + d * 0.005, 0.07, 0.062, mat, f"forearm.{sd}", 16)


def boots(k, mat, sole="boot", tall=True):
    for s, sd in SIDES:
        k.ell(f"bootf{sd}", (0.06, s * 0.115, 0.075), (0.15, 0.085, 0.085), mat, f"foot.{sd}", 20)
        k.box(f"sole{sd}", (0.3, 0.15, 0.03), (0.06, s * 0.115, 0.012), sole, f"foot.{sd}", bevel=0.012)
        if tall:
            k.cone(f"bootl{sd}", (0.01, s * 0.115, 0.12), (0.015, s * 0.112, 0.42), 0.085, 0.092, mat, f"shin.{sd}", 18)
            k.lathe(f"bootc{sd}", [(0.1, 0.0), (0.105, 0.03), (0.098, 0.05)], (0.015, s * 0.112, 0.4), mat, f"shin.{sd}", 18)


def limb_plate(k, name, a, b, r0, r1, mat, bone, segs=16):
    return k.cone(name, k.j(a) if isinstance(a, str) else Vector(a), k.j(b) if isinstance(b, str) else Vector(b), r0, r1, mat, bone, segs)


def arm_armour(k, mat, trim, bulk=1.0):
    for s, sd in SIDES:
        ua, fa, h = k.j(f"upperarm.{sd}"), k.j(f"forearm.{sd}"), k.j(f"hand.{sd}")
        k.cone(f"rere{sd}", ua + (fa - ua) * 0.25, fa - (fa - ua) * 0.05, 0.095 * bulk, 0.085 * bulk, mat, f"upperarm.{sd}", 16)
        k.ell(f"couter{sd}", tuple(fa + V(-0.02, 0, 0)), (0.075 * bulk, 0.075 * bulk, 0.075 * bulk), trim, f"forearm.{sd}", 16)
        k.cone(f"vam{sd}", fa + (h - fa) * 0.15, h - (h - fa) * 0.05, 0.083 * bulk, 0.072 * bulk, mat, f"forearm.{sd}", 16)
        k.cone(f"vamt{sd}", h - (h - fa) * 0.12, h - (h - fa) * 0.04, 0.078 * bulk, 0.078 * bulk, trim, f"forearm.{sd}", 16)


def pauldrons(k, mat, trim, size=1.0, layers=3):
    for s, sd in SIDES:
        ua = k.j(f"upperarm.{sd}")
        for i in range(layers):
            c = ua + V(0.0, s * (0.02 + 0.025 * i), 0.07 - 0.055 * i)
            r = (0.14 - 0.012 * i) * size
            k.ell(f"paul{sd}{i}", tuple(c), (r, r * 0.92, r * 0.62), mat if i % 2 == 0 else trim, f"upperarm.{sd}", 22)


def cuirass(k, mat, trim, belly=True):
    k.ell("breast", (0.045, 0, 1.27), (0.19, 0.235, 0.21), mat, "chest", 30)
    k.ell("backplate", (-0.04, 0, 1.28), (0.16, 0.22, 0.2), mat, "chest", 26)
    k.ell("keel", (0.2, 0, 1.28), (0.035, 0.03, 0.15), trim, "chest", 14)   # (the breastplate's ridge)
    ring(k, "gorget", (0.0, 0, 1.45), 0.105, 0.07, trim, "chest", 26, 0.02)
    if belly:
        for i, z in enumerate((1.13, 1.07, 1.01)):
            ring(k, f"fauld{i}", (0.015, 0, z), 0.195 + 0.008 * i, 0.05, mat if i % 2 == 0 else trim, "spine" if z > 1.05 else "pelvis", 28, 0.01)


def belt(k, mat, buckle, z=0.98, r=0.2):
    ring(k, "belt", (0.01, 0, z), r, 0.05, mat, "pelvis", 28)
    k.box("buckle", (0.02, 0.07, 0.055), (r + 0.015, 0, z), buckle, "pelvis", bevel=0.006)


def leg_armour(k, mat, trim):
    for s, sd in SIDES:
        k.cone(f"cuisse{sd}", (0.02, s * 0.1, 0.86), (0.035, s * 0.11, 0.58), 0.11, 0.092, mat, f"thigh.{sd}", 16)
        k.ell(f"poleyn{sd}", (0.085, s * 0.11, 0.5), (0.05, 0.075, 0.07), trim, f"shin.{sd}", 16)
        k.cone(f"greave{sd}", (0.025, s * 0.112, 0.45), (0.01, s * 0.115, 0.16), 0.088, 0.075, mat, f"shin.{sd}", 16)


def tassets(k, mat, trim, n=5):
    for i in range(n):   # plates hanging over the hips, on the thighs so they move with the legs
        a = -1.0 + 2.0 * i / (n - 1)
        for s, sd in SIDES:
            if (a > 0) != (s > 0) or abs(a) < 0.01:
                continue
            k.box(f"tasset{sd}{i}", (0.03, 0.12, 0.16), (0.13, s * 0.12, 0.84), mat if i % 2 else trim, f"thigh.{sd}", rot=(0, 0.25, s * 0.35), bevel=0.012)


def helm(k, mat, trim, visor="glass", visor_w=0.13, crest=None):
    """A closed helmet over the head: a dome, a brow band, cheek guards, a neck guard and a visor slit."""
    k.lathe("helm", [(0.17, -0.2), (0.2, -0.1), (0.205, 0.0), (0.19, 0.09), (0.15, 0.16), (0.08, 0.205), (0.001, 0.22)], tuple(HC), mat, "head", 32)
    k.lathe("helmbase", [(0.001, -0.2), (0.17, -0.2)], tuple(HC), mat, "head", 32)
    ring(k, "brow", tuple(HC + V(0.0, 0, 0.06)), 0.205, 0.035, trim, "head", 32)
    k.ell("visor", tuple(HC + V(0.175, 0, -0.01)), (0.045, visor_w, 0.04), visor, "head", 20)
    k.ell("chin", tuple(HC + V(0.13, 0, -0.13)), (0.08, 0.12, 0.07), mat, "head", 18)
    has_head(k)
    if crest:
        crest(k)


def has_head(k):
    k.has_head = True


def hood(k, mat, inner="shadow", eyes="glow", pointed=True):
    """A deep hood: the face in shadow, two eyes glowing in it."""
    k.ell("hoodshell", tuple(HC + V(-0.015, 0, 0.02)), (0.21, 0.2, 0.23), mat, "head", 30)
    if pointed:
        k.cone("hoodtip", HC + V(-0.08, 0, 0.15), HC + V(-0.24, 0, 0.32), 0.1, 0.01, mat, "head", 14)
    k.ell("hoodshade", tuple(HC + V(0.11, 0, -0.01)), (0.11, 0.15, 0.17), inner, "head", 22)
    for s in (1, -1):
        k.ell(f"heye{s}", tuple(HC + V(0.2, s * 0.05, 0.02)), (0.01, 0.025, 0.012), eyes, "head", 10)
    k.ell("mantle", (0.0, 0, 1.43), (0.2, 0.25, 0.1), mat, "chest", 26)
    has_head(k)


def mask_face(k, mat, eyes_mat="glow", mouth=None):
    k.ell("maskface", tuple(HC + V(0.01, 0, -0.01)), (0.125, 0.115, 0.14), mat, "head", 28)
    for s in (1, -1):
        k.ell(f"meye{s}", tuple(HC + V(0.115, s * 0.045, 0.025)), (0.012, 0.022, 0.012), eyes_mat, "head", 10)
    if mouth:
        k.ell("mmouth", tuple(HC + V(0.12, 0, -0.06)), (0.01, 0.04, 0.008), mouth, "head", 10)
    has_head(k)


def cape(k, mat, lining, length=0.95, width=0.26, flare=0.16, high=1.44):
    """A cape from the shoulders to the calves, curved round the back; lined (two sheets back to back)."""
    rows, cols = 7, 9
    for layer, (m, off) in enumerate(((mat, 0.0), (lining, 0.012))):
        verts, faces = [], []
        for i in range(rows):
            u = i / (rows - 1)
            z = high - length * u; w = width + flare * u
            for j in range(cols):
                v = j / (cols - 1) * 2 - 1
                a = v * 1.15
                x = -0.14 - 0.1 * u - 0.03 * math.cos(a) * (1 + u) + off * (1 - abs(v)) - 0.018 * math.sin(u * 7 + j) * u
                verts.append((x - 0.06 * (1 - math.cos(a)) * 0, math.sin(a) * w, z))
        for i in range(rows - 1):
            for j in range(cols - 1):
                a, b = i * cols + j, i * cols + j + 1
                faces.append((a, b, b + cols, a + cols))
        two_sided(k, f"cape{layer}", verts, faces, m, "chest")
    for s in (1, -1):   # clasps
        k.ell(f"clasp{s}", (0.08, s * 0.14, 1.44), (0.025, 0.025, 0.025), "gold", "chest", 12)


def coat_skirt(k, mat, trim, length=0.55, open_front=0.6, r0=0.215, r1=0.32, z0=1.02):
    """A coat's skirts from the waist toward the knees, open at the front; on the thighs (left and right halves) and
    the pelvis (the back), so they swing with the legs."""
    segs = 18
    for part, (a0, a1, bone) in enumerate(((open_front, math.pi * 0.62, "thigh.L"), (math.pi * 0.62, math.pi * 1.38, "pelvis"), (math.pi * 1.38, 2 * math.pi - open_front, "thigh.R"))):
        verts, faces = [], []
        rows = 5
        for i in range(rows):
            u = i / (rows - 1); z = z0 - length * u; r = r0 + (r1 - r0) * u
            for j in range(segs + 1):
                a = a0 + (a1 - a0) * j / segs
                verts.append((r * math.cos(a), r * math.sin(a), z))
        for i in range(rows - 1):
            for j in range(segs):
                p = i * (segs + 1) + j
                faces.append((p, p + 1, p + segs + 2, p + segs + 1))
        two_sided(k, f"skirt{part}", verts, faces, mat, bone)
        hem = [(r1 * 1.01 * math.cos(a0 + (a1 - a0) * j / 12), r1 * 1.01 * math.sin(a0 + (a1 - a0) * j / 12), z0 - length) for j in range(13)]
        k.tube(f"hem{part}", hem, 0.012, trim, bone)


def coat_body(k, mat, trim, lapels=True, buttons="gold"):
    k.ell("coatchest", (0.02, 0, 1.27), (0.19, 0.23, 0.22), mat, "chest", 28)
    k.ell("coatwaist", (0.01, 0, 1.08), (0.19, 0.215, 0.14), mat, "spine", 26)
    if lapels:
        for s in (1, -1):
            k.box(f"lapel{s}", (0.02, 0.07, 0.24), (0.19, s * 0.06, 1.3), trim, "chest", rot=(s * 0.25, -0.15, 0), bevel=0.01)
    for i in range(4):
        for s in (1, -1):
            k.ell(f"btn{i}{s}", (0.2, s * 0.06, 1.2 - i * 0.07), (0.012, 0.016, 0.016), buttons, "chest" if i < 2 else "spine", 10)
    ring(k, "collar", (0.0, 0, 1.46), 0.11, 0.09, trim, "chest", 24, 0.03)
    for s, sd in SIDES:   # sleeves with turned-back cuffs
        ua, fa, h = k.j(f"upperarm.{sd}"), k.j(f"forearm.{sd}"), k.j(f"hand.{sd}")
        k.cone(f"slv{sd}", ua - (fa - ua) * 0.05, fa, 0.098, 0.085, mat, f"upperarm.{sd}", 16)
        k.cone(f"slvl{sd}", fa, h - (h - fa) * 0.1, 0.085, 0.078, mat, f"forearm.{sd}", 16)
        k.cone(f"turn{sd}", h - (h - fa) * 0.28, h - (h - fa) * 0.05, 0.092, 0.092, trim, f"forearm.{sd}", 16)
        k.ell(f"shoulder{sd}", tuple(ua + V(0, s * 0.01, 0.03)), (0.1, 0.1, 0.08), mat, f"upperarm.{sd}", 18)


def trousers(k, mat):
    for s, sd in SIDES:
        k.cone(f"trs{sd}", (0.0, s * 0.1, 0.94), (0.015, s * 0.11, 0.5), 0.115, 0.09, mat, f"thigh.{sd}", 16)
        k.cone(f"trl{sd}", (0.015, s * 0.11, 0.5), (0.0, s * 0.115, 0.16), 0.09, 0.08, mat, f"shin.{sd}", 16)
    k.ell("seat", (-0.01, 0, 0.95), (0.16, 0.2, 0.12), mat, "pelvis", 20)


def glow_seams(k, mat, chest=True, arms=True, legs=True):
    if chest:
        for s in (1, -1):
            k.tube(f"gseam{s}", [V(0.17, s * 0.13, 1.42), V(0.205, s * 0.08, 1.3), V(0.19, s * 0.04, 1.15), V(0.17, s * 0.06, 1.0)], 0.008, mat, "chest")
    if arms:
        for s, sd in SIDES:
            ua, fa, h = k.j(f"upperarm.{sd}"), k.j(f"forearm.{sd}"), k.j(f"hand.{sd}")
            k.tube(f"gua{sd}", [ua + V(0.09, 0, 0), fa + V(0.08, 0, 0)], 0.007, mat, f"upperarm.{sd}")
            k.tube(f"gfa{sd}", [fa + V(0.075, 0, 0), h + V(0.065, 0, 0)], 0.007, mat, f"forearm.{sd}")
    if legs:
        for s, sd in SIDES:
            k.tube(f"gth{sd}", [V(0.1, s * 0.1, 0.88), V(0.1, s * 0.11, 0.55)], 0.007, mat, f"thigh.{sd}")
            k.tube(f"gsh{sd}", [V(0.085, s * 0.11, 0.46), V(0.075, s * 0.115, 0.18)], 0.007, mat, f"shin.{sd}")


def held(k, sd="R"):
    """The hand's grip point and the arm's line (for a weapon or tool held in it)."""
    s = 1 if sd == "L" else -1
    return k.j(f"palm.{sd}") + V(0.0, s * 0.02, -0.02), V(0, s * 0.62, -0.78).normalized()


def spear(k, mat, head_mat, sd="R", length=1.7, tines=1):
    p, d = held(k, sd)
    up = V(0.25, 0, 1).normalized()
    a, b = p - up * length * 0.35, p + up * length * 0.65
    k.tube("shaft", [a, b], 0.016, mat, f"hand.{sd}")
    if tines == 1:
        k.cone("spearhead", b, b + up * 0.2, 0.035, 0.002, head_mat, f"hand.{sd}", 8)
    else:
        for t in range(tines):
            o = (t - (tines - 1) / 2) * 0.05
            k.tube(f"tinebar{t}", [b + V(0, o, 0), b + V(0, o, 0) + up * 0.18], 0.008, head_mat, f"hand.{sd}")
            k.cone(f"tine{t}", b + V(0, o, 0) + up * 0.18, b + V(0, o, 0) + up * 0.25, 0.013, 0.001, head_mat, f"hand.{sd}", 6)
        k.tube("tinecross", [b + V(0, -0.06, 0.0), b + V(0, 0.06, 0.0)], 0.01, head_mat, f"hand.{sd}")


def blade(k, mat, guard, sd="R", length=0.75, width=0.035, curve=0.0):
    p, d = held(k, sd)
    fwd = V(0.75, 0, 0.66).normalized()
    pts = [p + fwd * (0.06 + length * t) + V(0, 0, -curve * t * t) for t in (0, 0.5, 1.0)]
    k.tube("grip", [p - fwd * 0.08, p + fwd * 0.05], 0.014, "leatherx", f"hand.{sd}")
    k.box("guard", (0.025, 0.14, 0.025), tuple(p + fwd * 0.055), guard, f"hand.{sd}", bevel=0.006)
    k.tube("blade", pts, width / 2, mat, f"hand.{sd}")
    k.cone("bladetip", pts[-1], pts[-1] + fwd * 0.08, width / 2, 0.001, mat, f"hand.{sd}", 8)


def lantern(k, glow, metal, sd="L"):
    p, d = held(k, sd)
    c = p + V(0.02, 0, -0.16)
    k.tube("lhandle", [p, c + V(0, 0, 0.09)], 0.006, metal, f"hand.{sd}")
    k.lathe("lcage", [(0.05, -0.08), (0.06, -0.06), (0.06, 0.06), (0.04, 0.08), (0.01, 0.1)], tuple(c), metal, f"hand.{sd}", 12)
    k.ell("lglow", tuple(c), (0.045, 0.045, 0.06), glow, f"hand.{sd}", 14)


# ---------------------------------------------------------------- the outfits
GOLD = COL(0.85, 0.62, 0.2, 0.3, 0.9)
STEEL = COL(0.55, 0.57, 0.6, 0.3, 0.9)
LEATHERX = COL(0.25, 0.14, 0.07, 0.6)


def o_lobster(k):   # the Lobster Knight: red lacquered plate, gold edging, a crested helm with feelers, one great claw
    base(k, "under", 1.05)
    cuirass(k, "shell", "gold"); pauldrons(k, "shell", "gold", 1.05); arm_armour(k, "shell", "gold"); leg_armour(k, "shell", "gold"); tassets(k, "shell", "gold")
    belt(k, "leatherx", "gold"); boots(k, "shell"); k.mitts("shell")
    def crest(k):
        for i in range(6):
            k.ell(f"crest{i}", tuple(HC + V(0.08 - i * 0.05, 0, 0.21 - 0.012 * i)), (0.035, 0.012, 0.05), "gold", "head", 10)
        for s in (1, -1):
            k.tube(f"feeler{s}", [HC + V(0.15, s * 0.06, 0.12), HC + V(0.3, s * 0.18, 0.4), HC + V(0.2, s * 0.35, 0.65), HC + V(0.0, s * 0.42, 0.75)], 0.006, "shell", "head")
    helm(k, "shell", "gold", "visor_d", 0.12, crest)
    k.claw("R", "shell", 1.3)
    cape(k, "cloth", "gold", 0.85, 0.22, 0.14)


def o_crab(k):   # the Crab Samurai: lacquered orange plates laced in black, a kabuto with eye-stalk crest, a claw arm
    base(k, "under", 1.05)
    cuirass(k, "shell", "lacing"); pauldrons(k, "shell", "lacing", 1.2, 4); arm_armour(k, "lacing", "shell"); leg_armour(k, "lacing", "shell")
    for i in range(6):   # the kusazuri: a skirt of lames
        a = -1.2 + 2.4 * i / 5
        sd = "L" if a > 0 else "R"
        k.box(f"kusa{i}", (0.03, 0.12, 0.2), (0.17 * math.cos(a) + 0.02, 0.2 * math.sin(a), 0.82), "shell" if i % 2 else "lacing", f"thigh.{sd}", rot=(0, 0.2, a), bevel=0.01)
    belt(k, "lacing", "gold"); boots(k, "lacing"); k.mitts("lacing")
    def crest(k):
        k.lathe("kabutorim", [(0.3, -0.02), (0.27, 0.02), (0.22, 0.03)], tuple(HC + V(0, 0, -0.02)), "shell", "head", 32)
        for s in (1, -1):
            k.tube(f"stalk{s}", [HC + V(0.12, s * 0.05, 0.18), HC + V(0.14, s * 0.12, 0.34)], 0.014, "shell", "head")
            k.ell(f"stalkeye{s}", tuple(HC + V(0.14, s * 0.12, 0.36)), (0.03, 0.03, 0.03), "black", "head", 12)
        k.ell("maempo", tuple(HC + V(0.16, 0, -0.06)), (0.06, 0.11, 0.07), "lacing", "head", 18)
    helm(k, "shell", "gold", "visor_d", 0.12, crest)
    k.claw("L", "shell", 1.5)
    blade(k, STEEL_M, "gold", "R", 0.8, 0.03, 0.08)


def o_pufferfish(k):   # the Spined Brawler: a padded spined vest, spiked knuckles, a round visored helm with quills
    base(k, "suit", 1.07)
    k.ell("vest", (0.02, 0, 1.2), (0.23, 0.26, 0.26), "skin", "chest", 30)
    CO.spikes(k, "vq", (0.02, 0, 1.2), (0.23, 0.26, 0.26), 26, 0.07, "spine_m", "chest", 0.018)
    belt(k, "leatherx", "gold"); trousers(k, "suit"); boots(k, "leatherx")
    gloves(k, "skin")
    for s, sd in SIDES:
        h = k.j(f"palm.{sd}"); d = V(0, s * 0.62, -0.78).normalized()
        for i in range(3):
            k.cone(f"knuck{sd}{i}", h + d * 0.05 + V(0.05, 0, 0) + V(0, 0, (i - 1) * 0.02), h + d * 0.05 + V(0.12, 0, 0) + V(0, 0, (i - 1) * 0.02), 0.012, 0.001, "spine_m", f"hand.{sd}", 6)
    k.lathe("phelm", [(0.001, -0.22), (0.18, -0.2), (0.24, -0.05), (0.24, 0.05), (0.18, 0.18), (0.001, 0.24)], tuple(HC), "skin", "head", 30)
    k.ell("pvisor", tuple(HC + V(0.21, 0, 0.0)), (0.05, 0.15, 0.06), "visor_d", "head", 20)
    CO.spikes(k, "hq", tuple(HC), (0.24, 0.24, 0.24), 18, 0.08, "spine_m", "head", 0.016)
    has_head(k)


def o_jelly(k):   # the Bioluminescent Drifter: a black stealth suit with glowing seams, a translucent bell hood, trailing light
    base(k, "suit", 1.04)
    glow_seams(k, "glow")
    k.lathe("bell", [(0.001, 0.26), (0.13, 0.23), (0.22, 0.12), (0.26, -0.02), (0.25, -0.06), (0.2, -0.04)], tuple(HC), "bell", "head", 32)
    k.ell("jface", tuple(HC + V(0.01, 0, -0.03)), (0.12, 0.11, 0.13), "suit", "head", 24)
    for s in (1, -1):
        k.ell(f"jeye{s}", tuple(HC + V(0.115, s * 0.045, 0.0)), (0.01, 0.028, 0.01), "glow", "head", 10)
    has_head(k)
    for i in range(10):   # ribbons of light trailing from the bell's rim
        a = i * 2 * math.pi / 10
        p0 = HC + V(0.24 * math.cos(a), 0.24 * math.sin(a), -0.06)
        k.tube(f"rib{i}", [p0, p0 + V(-0.05 * math.cos(a) - 0.04, 0.0, -0.35), p0 + V(-0.1 - 0.03 * math.cos(a), 0.03 * math.sin(i), -0.75)], 0.01, "tent", "chest")
    gloves(k, "suit"); boots(k, "suit")
    k.tube("beltglow", [V(0.2 * math.cos(a), 0.2 * math.sin(a), 0.98) for a in [j * 2 * math.pi / 24 for j in range(25)]], 0.01, "glow", "pelvis")


def o_hermit(k):   # the Shell Wanderer: a nomad's hooded cloak, wrappings, a spiral shell for a pack, a staff
    base(k, "canvas", 1.06)
    hood(k, "cloak", "shadow", "glint")
    cape(k, "cloak", "canvas", 1.05, 0.25, 0.2)
    trousers(k, "canvas"); boots(k, "leatherx"); gloves(k, "wrap")
    k.tube("sash", [V(0.18, 0.18, 1.42), V(0.2, 0.0, 1.22), V(0.18, -0.18, 1.0)], 0.03, "wrap", "chest")
    for i in range(5):   # the shell pack: whorls narrowing up the back
        u = i / 4
        r = 0.2 * (1 - u * 0.75)
        k.ell(f"pwhorl{i}", (-0.24 - 0.04 * u, 0.02 * math.sin(i * 2), 1.15 + u * 0.4), (r, r, r * 0.85), "shell" if i % 2 == 0 else "shell2", "chest", 22)
    k.tube("straps", [V(0.12, 0.15, 1.43), V(-0.2, 0.17, 1.3)], 0.015, "leatherx", "chest")
    spear(k, "wood", "shell", "R", 1.8)


def o_starfish(k):   # Starfall: a sleek white-and-coral suit, a glowing star on the chest, a star-crested helm
    base(k, "suitw", 1.05)
    cuirass(k, "star", "gold", belly=False)
    for i in range(5):   # the star on the breast
        a = math.pi / 2 + i * 2 * math.pi / 5
        k.cone(f"star{i}", V(0.235, 0, 1.28), V(0.235, 0.11 * math.cos(a), 1.28 + 0.11 * math.sin(a)), 0.03, 0.003, "glow", "chest", 8)
    k.ell("starcore", (0.235, 0, 1.28), (0.012, 0.04, 0.04), "glow", "chest", 12)
    pauldrons(k, "star", "gold", 0.9, 2); arm_armour(k, "suitw", "star", 0.95); leg_armour(k, "suitw", "star")
    belt(k, "star", "gold"); boots(k, "star"); k.mitts("star")
    def crest(k):
        for i in range(5):
            a = i * 2 * math.pi / 5
            k.cone(f"hstar{i}", HC + V(-0.02, 0, 0.21), HC + V(-0.02 + 0.06 * math.cos(a), 0.15 * math.sin(a) * 0.0 + 0.0, 0.21 + 0.14) + V(0, 0.14 * math.cos(a + 1.2), 0), 0.03, 0.002, "star", "head", 8)
    helm(k, "suitw", "star", "visor_g", 0.13, crest)
    cape(k, "star", "suitw", 0.7, 0.2, 0.1)


def o_kelp(k):   # the Kelp Ranger: a ghillie cloak of fronds over leathers, a ranger's hood, a longbow
    base(k, "leathg", 1.05)
    hood(k, "kelp", "shadow", "glint", pointed=False)
    trousers(k, "leathg"); boots(k, "leatherx"); gloves(k, "leatherx"); belt(k, "leatherx", "brass")
    for i in range(16):   # fronds down the back and shoulders
        a = math.pi * (0.55 + i / 15.0 * 0.9)
        p0 = V(0.2 * math.cos(a), 0.24 * math.sin(a), 1.44)
        k.tube(f"frond{i}", [p0, p0 + V(-0.05, 0.02 * math.sin(i), -0.35), p0 + V(-0.08 - 0.02 * (i % 3), 0.04 * math.sin(i * 1.7), -0.85)], 0.025, "kelp" if i % 2 else "kelp2", "chest")
    for s in (1, -1):
        k.ell(f"float{s}", (0.0, s * 0.2, 1.48), (0.035, 0.035, 0.045), "float", "chest", 12)
    for i in range(3):
        k.box(f"pouch{i}", (0.05, 0.06, 0.07), (0.17 * math.cos(-0.5 + i * 0.5), 0.2 * math.sin(-0.5 + i * 0.5), 0.94), "leatherx", "pelvis", rot=(0, 0, -0.5 + i * 0.5), bevel=0.01)
    p, d = held(k, "L")   # the bow
    bow = [p + V(0.02, 0, -0.65 + 1.3 * t) + V(0.12 * math.sin(t * math.pi), 0, 0) for t in [j / 8 for j in range(9)]]
    k.tube("bow", bow, 0.014, "wood", "hand.L"); k.tube("bowstring", [bow[0], bow[-1]], 0.003, "white", "hand.L")


def o_barnacle(k):   # the Hull Breaker: heavy riveted plate crusted with barnacles, huge gauntlets, a breaching hammer
    base(k, "under", 1.08)
    cuirass(k, "iron", "rust"); pauldrons(k, "iron", "rust", 1.3, 3); arm_armour(k, "iron", "rust", 1.2); leg_armour(k, "iron", "rust")
    CO.spikes(k, "barnc", (0.05, 0, 1.28), (0.2, 0.24, 0.21), 22, 0.025, "barn", "chest", 0.02)
    for s, sd in SIDES:   # great gauntlets
        h = k.j(f"palm.{sd}")
        k.ell(f"ggaunt{sd}", tuple(h), (0.09, 0.09, 0.1), "iron", f"hand.{sd}", 18)
    boots(k, "iron"); belt(k, "rust", "iron")
    def crest(k):
        CO.spikes(k, "hbarn", tuple(HC + V(0, 0, 0.05)), (0.2, 0.2, 0.18), 12, 0.03, "barn", "head", 0.022)
        for i in range(8):
            a = i * math.pi / 4
            k.ell(f"rivet{i}", tuple(HC + V(0.205 * math.cos(a), 0.205 * math.sin(a), 0.06)), (0.012, 0.012, 0.012), "rust", "head", 8)
    helm(k, "iron", "rust", "visor_d", 0.1, crest)
    p, d = held(k, "R")
    up = V(0.3, 0, 1).normalized()
    k.tube("hhaft", [p - up * 0.35, p + up * 0.75], 0.02, "wood", "hand.R")
    k.box("hhead", (0.16, 0.26, 0.14), tuple(p + up * 0.78), "iron", "hand.R", bevel=0.02)


def o_shark(k):   # the Shark Hunter: a sleek two-tone wetsuit, a finned helm with a toothed visor, a dorsal fin, a harpoon
    base(k, "grey", 1.04)
    k.ell("belly", (0.07, 0, 1.18), (0.15, 0.17, 0.26), "pale", "chest", 24)
    glow_seams(k, "teal", chest=False)
    pauldrons(k, "grey", "steelg", 0.85, 2); arm_armour(k, "grey", "steelg", 0.95)
    belt(k, "black", "steelg"); boots(k, "grey"); k.mitts("black")
    k.cone("dorsal", V(-0.17, 0, 1.25), V(-0.33, 0, 1.55), 0.1, 0.005, "grey", "chest", 3)
    def crest(k):
        k.cone("hfin", HC + V(-0.04, 0, 0.16), HC + V(-0.16, 0, 0.36), 0.07, 0.004, "grey", "head", 3)
        for i in range(7):
            k.cone(f"tooth{i}", HC + V(0.2, -0.1 + i * 0.033, -0.04), HC + V(0.215, -0.1 + i * 0.033, -0.08), 0.012, 0.001, "white", "head", 4)
        for s in (1, -1):
            for g in range(3):
                k.box(f"gill{s}{g}", (0.04, 0.003, 0.05), tuple(HC + V(0.05 - g * 0.035, s * 0.2, -0.08)), "black", "head", rot=(0, 0.2, 0), bevel=0.001)
    helm(k, "grey", "steelg", "visor_t", 0.14, crest)
    spear(k, "steelg", "white", "R", 1.6)


def o_octopus(k):   # the Abyss Witch: a violet robe whose skirt is eight curling arms, a wide hat, glowing eyes, a staff
    base(k, "robe", 1.08)
    hood(k, "robe", "shadow", "glowv", pointed=False)
    k.lathe("hat", [(0.42, -0.0), (0.4, 0.02), (0.18, 0.03), (0.17, 0.12), (0.1, 0.3), (0.02, 0.42)], tuple(HC + V(0, 0, 0.12)), "skin", "head", 32)
    k.tube("hatband", [HC + V(0.17 * math.cos(a), 0.17 * math.sin(a), 0.16) for a in [j * 2 * math.pi / 24 for j in range(25)]], 0.012, "sucker", "head")
    coat_skirt(k, "robe", "sucker", 0.6, 0.15, 0.21, 0.3)
    for i in range(8):   # arms from the waist, curling at the tips, suckers beneath
        a = i * 2 * math.pi / 8 + 0.2
        p0 = V(0.25 * math.cos(a), 0.25 * math.sin(a), 0.5)
        pts = [p0, p0 + V(0.08 * math.cos(a), 0.08 * math.sin(a), -0.25), p0 + V(0.2 * math.cos(a), 0.2 * math.sin(a), -0.42), p0 + V(0.3 * math.cos(a + 0.4), 0.3 * math.sin(a + 0.4), -0.38)]
        k.tube(f"oarm{i}", pts, 0.035, "skin", "thigh.L" if math.sin(a) > 0 else "thigh.R")
    gloves(k, "skin"); boots(k, "skin", tall=False)
    p, d = held(k, "R")
    up = V(0.2, 0, 1).normalized()
    k.tube("ostaff", [p - up * 0.6, p + up * 0.9, p + up * 1.0 + V(0.08, 0, 0.05)], 0.016, "wood", "hand.R")
    k.ell("oorb", tuple(p + up * 1.05 + V(0.1, 0, 0.06)), (0.06, 0.06, 0.06), "glowv", "hand.R", 16)


def o_angler(k):   # the Lantern Wraith: a long tattered dark coat, a deep hood, the lure's light arcing over it, a hook
    base(k, "dark", 1.05)
    coat_body(k, "dark", "darkt", lapels=False, buttons="darkt")
    coat_skirt(k, "dark", "darkt", 0.7, 0.4, 0.22, 0.34)
    hood(k, "dark", "shadow", "glow")
    k.tube("lure", [HC + V(-0.02, 0, 0.18), HC + V(0.1, 0, 0.42), HC + V(0.32, 0, 0.5), HC + V(0.44, 0, 0.38)], 0.01, "dark", "head")
    k.ell("lurelight", tuple(HC + V(0.45, 0, 0.32)), (0.05, 0.05, 0.06), "glow", "head", 16)
    for i in range(7):   # teeth round the hood's mouth
        a = -0.9 + i * 0.3
        k.cone(f"atooth{i}", HC + V(0.18 * math.cos(a), 0.18 * math.sin(a), -0.12), HC + V(0.2 * math.cos(a), 0.2 * math.sin(a), -0.05), 0.012, 0.001, "white", "head", 4)
    trousers(k, "dark"); boots(k, "darkt"); gloves(k, "darkt")
    lantern(k, "glow", "darkt", "L")
    p, d = held(k, "R")
    k.tube("hook", [p, p + V(0.05, 0, -0.25), p + V(0.15, 0, -0.32), p + V(0.2, 0, -0.22)], 0.012, "steelg", "hand.R")


def o_turtle(k):   # the Shellback Guard: a domed shell for a backplate and a round shield, green scale armour, a spear
    base(k, "skin", 1.05)
    cuirass(k, "belly", "scute", belly=True); pauldrons(k, "shell", "scute", 1.0, 2); arm_armour(k, "skin", "scute"); leg_armour(k, "skin", "scute")
    k.ell("carapace", (-0.2, 0, 1.18), (0.16, 0.3, 0.36), "shell", "chest", 30)
    for i in range(7):
        k.ell(f"bscute{i}", (-0.33, -0.15 + (i % 3) * 0.15, 0.95 + (i // 3) * 0.2), (0.03, 0.08, 0.08), "scute", "chest", 14)
    boots(k, "shell"); k.mitts("skin"); belt(k, "shell", "gold")
    helm(k, "shell", "scute", "visor_d", 0.12)
    p, d = held(k, "L")   # the shield
    k.ell("shield", tuple(p + V(0.08, 0.05, 0.05)), (0.06, 0.3, 0.3), "shell", "hand.L", 26)
    k.ell("shieldboss", tuple(p + V(0.13, 0.05, 0.05)), (0.04, 0.09, 0.09), "scute", "hand.L", 16)
    spear(k, "wood", "gold", "R", 1.7)


def o_ghost(k):   # the Ghost Captain: a pale translucent greatcoat, a tricorn, chains, eyes burning green
    base(k, "sheet", 1.05)
    coat_body(k, "sheet", "sheetd", buttons="glowg")
    coat_skirt(k, "sheet", "sheetd", 0.75, 0.45, 0.22, 0.36)
    hood(k, "sheet", "shadow", "glowg", pointed=False)
    k.lathe("tricorn", [(0.001, 0.0), (0.33, 0.0), (0.31, 0.03), (0.15, 0.05), (0.14, 0.13), (0.001, 0.15)], tuple(HC + V(0, 0, 0.12)), "black", "head", 3)
    k.tube("tritrim", [HC + V(0.33 * math.cos(a), 0.33 * math.sin(a), 0.13) for a in [j * 2 * math.pi / 3 for j in range(4)]], 0.01, "glowg", "head")
    trousers(k, "sheet"); boots(k, "black"); gloves(k, "sheetd")
    for i in range(12):   # chains wound round the body
        a = i * 0.9
        k.ell(f"chain{i}", (0.21 * math.cos(a), 0.23 * math.sin(a), 1.05 + i * 0.025), (0.02, 0.012, 0.02), "iron", "spine", 8)
    lantern(k, "glowg", "iron", "L")
    blade(k, "sheetd", "glowg", "R", 0.8, 0.032, 0.1)


def o_coral(k):   # the Reef Monarch: a regal coral crown, a coral-pink cape, mother-of-pearl armour, a trident
    base(k, "pearl", 1.05)
    cuirass(k, "pearl", "coral2"); pauldrons(k, "coral1", "pearl", 1.0, 3); arm_armour(k, "pearl", "coral2"); leg_armour(k, "pearl", "coral2")
    belt(k, "coral2", "gold"); boots(k, "pearl"); k.mitts("pearl")
    def crest(k):
        for i in range(9):   # a crown of branching coral
            a = i * 2 * math.pi / 9
            p0 = HC + V(0.17 * math.cos(a), 0.17 * math.sin(a), 0.12)
            k.tube(f"crown{i}", [p0, p0 + V(0.03 * math.cos(a), 0.03 * math.sin(a), 0.14), p0 + V(0.06 * math.cos(a + 0.4), 0.06 * math.sin(a + 0.4), 0.22)], 0.016, "coral1" if i % 3 else "coral3", "head")
        ring(k, "crownband", tuple(HC + V(0, 0, 0.12)), 0.18, 0.04, "gold", "head", 28)
    helm(k, "pearl", "gold", "visor_g", 0.12, crest)
    cape(k, "coral1", "coral3", 1.0, 0.26, 0.22)
    spear(k, "gold", "gold", "R", 1.8, tines=3)


def o_divingbell(k):   # the Mk.IV Hard-Hat: a canvas suit, a brass helmet with three ports and a corselet, weighted boots
    base(k, "canvas_g", 1.12)
    k.lathe("helmet", [(0.001, -0.23), (0.2, -0.22), (0.25, -0.12), (0.26, 0.0), (0.24, 0.12), (0.18, 0.22), (0.08, 0.27), (0.001, 0.28)], tuple(HC), "brass", "head", 36)
    for i, (a, z) in enumerate(((0.0, 0.0), (1.4, 0.0), (-1.4, 0.0), (0.0, 0.17))):   # ports: front, both sides, top
        c = HC + V(0.25 * math.cos(a) * (0.85 if z else 1), 0.25 * math.sin(a), z)
        n = V(math.cos(a), math.sin(a), 0.8 if z else 0.0).normalized()
        k.ell(f"port{i}", tuple(c + n * 0.01), (0.03 if abs(n.x) > 0.5 else 0.075, 0.03 if abs(n.y) > 0.5 else 0.075, 0.075 if not z else 0.03), "glass", "head", 18)
        k.cone(f"portrim{i}", c - n * 0.01, c + n * 0.03, 0.09, 0.085, "brass_d", "head", 20)
        for g in range(3):
            k.tube(f"grill{i}{g}", [c + n * 0.035 + V(0, 0, (g - 1) * 0.04) + V(-n.y, n.x, 0) * 0.08, c + n * 0.035 + V(0, 0, (g - 1) * 0.04) - V(-n.y, n.x, 0) * 0.08], 0.006, "brass_d", "head")
    has_head(k)
    k.lathe("corselet", [(0.32, 1.36 - 1.48), (0.3, 1.44 - 1.48), (0.2, 1.52 - 1.48), (0.16, 1.55 - 1.48)], (0.0, 0, 1.48), "brass", "chest", 32)
    for i in range(12):
        a = i * math.pi / 6
        k.ell(f"bolt{i}", (0.31 * math.cos(a), 0.31 * math.sin(a), 1.38), (0.015, 0.015, 0.015), "brass_d", "chest", 8)
    for s in (1, -1):
        k.box(f"weight{s}", (0.06, 0.18, 0.22), (0.24, s * 0.1, 1.2), "iron", "chest", bevel=0.01)
    k.tube("airhose", [V(-0.2, 0, 1.55), V(-0.32, 0.05, 1.3), V(-0.3, 0.1, 0.9), V(-0.45, 0.1, 0.4)], 0.03, "iron", "chest")
    gloves(k, "iron")
    for s, sd in SIDES:   # weighted boots: lead soles and brass toe caps
        k.ell(f"hboot{sd}", (0.06, s * 0.115, 0.08), (0.17, 0.095, 0.095), "iron", f"foot.{sd}", 20)
        k.box(f"hsole{sd}", (0.34, 0.17, 0.05), (0.06, s * 0.115, 0.02), "lead", f"foot.{sd}", bevel=0.01)
        k.ell(f"toecap{sd}", (0.18, s * 0.115, 0.07), (0.06, 0.08, 0.06), "brass", f"foot.{sd}", 14)
    belt(k, "leatherx", "brass_d", 1.0, 0.24)
    p, d = held(k, "R")
    k.tube("knifeh", [p, p + V(0.12, 0, 0.06)], 0.015, "leatherx", "hand.R"); k.tube("knifeb", [p + V(0.12, 0, 0.06), p + V(0.34, 0, 0.17)], 0.015, "steelg", "hand.R")


def o_seamine(k):   # Demolitions: a bomb-disposal suit, a horned helmet like a contact mine, a charge on the back
    base(k, "olive", 1.12)
    k.ell("blastplate", (0.08, 0, 1.25), (0.18, 0.25, 0.26), "olive", "chest", 28)
    k.ell("blastcollar", (0.0, 0, 1.47), (0.22, 0.26, 0.09), "olive", "chest", 26)
    pauldrons(k, "olive", "iron", 1.1, 2); arm_armour(k, "olive", "iron", 1.1); leg_armour(k, "olive", "iron")
    k.lathe("mhelm", [(0.001, -0.22), (0.2, -0.2), (0.26, -0.05), (0.26, 0.06), (0.2, 0.19), (0.001, 0.27)], tuple(HC), "iron", "head", 32)
    k.ell("mvisor", tuple(HC + V(0.22, 0, 0.0)), (0.05, 0.16, 0.08), "visor_g", "head", 20)
    for i in range(7):   # horns
        a = i * 2 * math.pi / 7; el = 0.7 if i % 2 else 0.2
        dvec = V(math.cos(a) * math.cos(el), math.sin(a) * math.cos(el), math.sin(el))
        if dvec.x > 0.75:
            continue
        c = HC + V(dvec.x * 0.26, dvec.y * 0.26, dvec.z * 0.26)
        k.cone(f"horn{i}", c, c + dvec * 0.09, 0.03, 0.018, "brass", "head", 10)
    has_head(k)
    k.lathe("charge", [(0.001, -0.15), (0.12, -0.12), (0.14, 0.0), (0.12, 0.12), (0.001, 0.15)], (-0.28, 0, 1.2), "iron", "chest", 20)
    k.ell("chargelight", (-0.33, 0.06, 1.3), (0.02, 0.02, 0.02), "glowr", "chest", 10)
    gloves(k, "olive"); boots(k, "iron"); belt(k, "leatherx", "brass")


def o_swordfish(k):   # the Swordfish Duellist: a fitted midnight-blue fencing coat, a sail crest, a rapier like a bill
    base(k, "blue", 1.04)
    coat_body(k, "blue", "silver", lapels=True, buttons="silver")
    coat_skirt(k, "blue", "silver", 0.45, 0.65, 0.21, 0.28)
    trousers(k, "silverw"); boots(k, "black"); gloves(k, "black")
    k.ell("cravat", (0.2, 0, 1.38), (0.03, 0.06, 0.06), "silverw", "chest", 14)
    def crest(k):
        sail = [V(0.05, 0, 0.18), V(-0.05, 0, 0.4), V(-0.2, 0, 0.36), V(-0.2, 0, 0.15)]
        two_sided(k, "sail", [tuple(HC + p) for p in sail], [(0, 1, 2, 3)], "silver", "head")
        k.cone("bill", HC + V(0.2, 0, 0.05), HC + V(0.55, 0, 0.07), 0.025, 0.002, "silver", "head", 10)
    helm(k, "blue", "silver", "visor_d", 0.12, crest)
    blade(k, "silver", "silver", "R", 0.95, 0.018)
    cape(k, "blue", "silver", 0.55, 0.15, 0.06)


def o_kraken(k):   # the Kraken Lord: a crimson cloak of tentacles, black plate, a crown of horns, eyes like the beast's
    base(k, "black", 1.05)
    cuirass(k, "blackp", "red"); pauldrons(k, "red", "gold", 1.15, 3); arm_armour(k, "blackp", "red"); leg_armour(k, "blackp", "red")
    belt(k, "red", "gold"); boots(k, "blackp"); k.mitts("blackp")
    for i in range(10):   # tentacles from the shoulders down the back, curling
        a = math.pi * (0.6 + 0.8 * i / 9)
        p0 = V(0.15 * math.cos(a), 0.24 * math.sin(a), 1.44)
        k.tube(f"ktent{i}", [p0, p0 + V(-0.08, 0.03 * math.sin(i), -0.4), p0 + V(-0.12, 0.05 * math.sin(i * 1.3), -0.8), p0 + V(-0.05, 0.08 * math.sin(i), -1.0)], 0.035 - 0.0015 * i, "red", "chest")
    def crest(k):
        for i in range(7):
            a = -1.2 + i * 0.4
            p0 = HC + V(0.18 * math.cos(a) - 0.03, 0.18 * math.sin(a), 0.14)
            k.cone(f"khorn{i}", p0, p0 + V(0.02, 0.06 * math.sin(a), 0.18 + 0.06 * (1 - abs(a))), 0.025, 0.003, "gold", "head", 8)
        for s in (1, -1):
            k.ell(f"keye{s}", tuple(HC + V(0.19, s * 0.06, 0.02)), (0.012, 0.03, 0.016), "yellow", "head", 10)
    helm(k, "blackp", "red", "visor_d", 0.12, crest)
    spear(k, "blackp", "gold", "R", 1.8, tines=3)


def o_goliath(k):   # the Goliath: a hulking grouper-scale juggernaut, a heavy helm with a fish's gape, a harpoon cannon
    base(k, "olive", 1.15)
    cuirass(k, "plate", "lip"); pauldrons(k, "plate", "olive", 1.4, 3); arm_armour(k, "plate", "lip", 1.3); leg_armour(k, "plate", "lip"); tassets(k, "plate", "olive")
    for i in range(14):   # scales over the belly
        k.ell(f"scale{i}", (0.2, -0.12 + (i % 4) * 0.08, 1.02 + (i // 4) * 0.07), (0.015, 0.045, 0.04), "pale", "spine", 10)
    boots(k, "plate"); belt(k, "lip", "iron")
    for s, sd in SIDES:
        k.ell(f"bgaunt{sd}", tuple(k.j(f"palm.{sd}")), (0.1, 0.1, 0.11), "plate", f"hand.{sd}", 18)
    def crest(k):
        k.ell("gape", tuple(HC + V(0.19, 0, -0.05)), (0.05, 0.15, 0.08), "lip", "head", 20)
        for s in (1, -1):
            k.ell(f"geye{s}", tuple(HC + V(0.12, s * 0.17, 0.07)), (0.035, 0.03, 0.035), "glowy", "head", 12)
        k.cone("gfin", HC + V(-0.02, 0, 0.18), HC + V(-0.2, 0, 0.32), 0.09, 0.005, "olive", "head", 3)
    helm(k, "plate", "lip", "visor_d", 0.08, crest)
    p, d = held(k, "R")   # the harpoon cannon on the forearm
    fa = k.j("forearm.R")
    k.cone("hcannon", fa + V(0.04, 0, -0.02), p + V(0.08, 0, 0.02), 0.07, 0.06, "iron", "forearm.R", 16)
    k.tube("harpoon", [p + V(0.1, 0, 0.03), p + V(0.5, 0, 0.2)], 0.012, "steelg", "forearm.R")
    k.cone("harpoonhead", p + V(0.5, 0, 0.2), p + V(0.6, 0, 0.25), 0.03, 0.001, "steelg", "forearm.R", 6)


def o_nautilus(k):   # Captain Nemo: a navy captain's coat with brass epaulettes, a peaked cap, a brass gauntlet, a sabre
    base(k, "navy", 1.04)
    coat_body(k, "navy", "brass", lapels=True, buttons="brass")
    coat_skirt(k, "navy", "brass", 0.6, 0.55, 0.21, 0.3)
    trousers(k, "navyl"); boots(k, "black")
    for s, sd in SIDES:   # epaulettes with fringes
        ua = k.j(f"upperarm.{sd}")
        k.ell(f"epaul{sd}", tuple(ua + V(0, s * 0.01, 0.075)), (0.09, 0.07, 0.025), "brass", f"upperarm.{sd}", 18)
        for f in range(6):
            a = -0.6 + f * 0.24
            k.tube(f"fringe{sd}{f}", [ua + V(0.07 * math.sin(a), s * 0.08, 0.07), ua + V(0.07 * math.sin(a), s * 0.09, 0.0)], 0.005, "brass", f"upperarm.{sd}")
    k.mitts("navyl")
    h = k.j("palm.L")   # the brass gauntlet, lit
    k.ell("bgauntlet", tuple(h), (0.085, 0.085, 0.09), "brass", "hand.L", 18)
    k.ell("bgglow", tuple(h + V(0.08, 0, 0.0)), (0.02, 0.035, 0.035), "glow", "hand.L", 12)
    mask_face(k, "mask", "dark", None)
    k.ell("beard", tuple(HC + V(0.08, 0, -0.09)), (0.08, 0.12, 0.08), "beard", "head", 18)
    k.lathe("capcrown", [(0.13, 0.0), (0.15, 0.05), (0.17, 0.09), (0.001, 0.1)], tuple(HC + V(0, 0, 0.1)), "navy", "head", 28)
    k.lathe("capband", [(0.135, -0.005), (0.14, 0.045)], tuple(HC + V(0, 0, 0.1)), "black", "head", 28)
    k.ell("peak", tuple(HC + V(0.14, 0, 0.1)), (0.08, 0.12, 0.012), "black", "head", 18)
    k.ell("capbadge", tuple(HC + V(0.14, 0, 0.16)), (0.008, 0.03, 0.02), "brass", "head", 10)
    blade(k, "steelg", "brass", "R", 0.8, 0.026, 0.12)


def o_fish(k):   # the Herring Rogue: a silver-scaled leather jerkin, a fish-skull mask, a short blade and a scarf
    base(k, "silver", 1.05)
    k.ell("jerkin", (0.03, 0, 1.2), (0.2, 0.24, 0.27), "fin", "chest", 28)
    for i in range(16):
        k.ell(f"jscale{i}", (0.22, -0.15 + (i % 4) * 0.1, 1.05 + (i // 4) * 0.08), (0.012, 0.05, 0.04), "silver", "chest" if i > 7 else "spine", 10)
    k.tube("scarf", [V(0.15 * math.cos(a), 0.15 * math.sin(a), 1.46) for a in [j * 2 * math.pi / 16 for j in range(17)]], 0.04, "lip", "chest")
    k.tube("scarftail", [V(-0.12, 0.05, 1.45), V(-0.2, 0.1, 1.3), V(-0.25, 0.12, 1.15)], 0.03, "lip", "chest")
    trousers(k, "fin"); boots(k, "leatherx"); gloves(k, "leatherx"); belt(k, "leatherx", "silver")
    mask_face(k, "bone", "glow", None)
    k.ell("snout", tuple(HC + V(0.13, 0, -0.03)), (0.07, 0.07, 0.06), "bone", "head", 16)
    for i in range(5):
        k.cone(f"fteeth{i}", HC + V(0.17, -0.04 + i * 0.02, -0.07), HC + V(0.18, -0.04 + i * 0.02, -0.1), 0.007, 0.001, "white", "head", 4)
    k.cone("fhfin", HC + V(-0.02, 0, 0.12), HC + V(-0.12, 0, 0.25), 0.06, 0.004, "silver", "head", 3)
    blade(k, "silver", "silver", "R", 0.45, 0.03, 0.05)


def o_gull(k):   # the Gull Rider: an aviator's jacket with a white fleece collar, goggles, a wing cape, a flare gun
    base(k, "grey", 1.04)
    k.ell("jacket", (0.015, 0, 1.25), (0.18, 0.22, 0.22), "leatherb", "chest", 28)
    k.ell("jacketw", (0.01, 0, 1.06), (0.17, 0.2, 0.12), "leatherb", "spine", 24)
    k.ell("fleece", (0.0, 0, 1.45), (0.17, 0.22, 0.08), "white", "chest", 22)
    for s, sd in SIDES:
        ua, fa, h = k.j(f"upperarm.{sd}"), k.j(f"forearm.{sd}"), k.j(f"hand.{sd}")
        k.cone(f"jslv{sd}", ua, fa, 0.1, 0.088, "leatherb", f"upperarm.{sd}", 16); k.cone(f"jslvl{sd}", fa, h, 0.088, 0.08, "leatherb", f"forearm.{sd}", 16)
        feathers = [(ua + (h - ua) * t + V(-0.08, s * 0.03, -0.1 - 0.05 * t)) for t in (0.0, 0.5, 1.0)]
        k.tube(f"wingedge{sd}", feathers, 0.012, "white", f"forearm.{sd}")
    cape(k, "white", "grey", 0.75, 0.32, 0.24, 1.42)
    trousers(k, "grey"); boots(k, "yellow"); gloves(k, "leatherb"); belt(k, "leatherb", "brass")
    mask_face(k, "white", "black", "yellow")
    k.ell("aviator", tuple(HC + V(-0.03, 0, 0.03)), (0.135, 0.135, 0.15), "leatherb", "head", 26)
    for s in (1, -1):
        k.ell(f"earflap{s}", tuple(HC + V(-0.01, s * 0.125, -0.06)), (0.05, 0.025, 0.08), "leatherb", "head", 14)
    k.cone("beak", HC + V(0.12, 0, -0.03), HC + V(0.24, 0, -0.06), 0.035, 0.004, "yellow", "head", 10)
    k.ell("beakspot", tuple(HC + V(0.2, 0, -0.06)), (0.012, 0.012, 0.012), "red", "head", 8)
    for s in (1, -1):
        k.ell(f"goggle{s}", tuple(HC + V(0.08, s * 0.06, 0.12)), (0.025, 0.035, 0.035), "visor_g", "head", 14)
        k.cone(f"gogrim{s}", HC + V(0.06, s * 0.06, 0.12), HC + V(0.1, s * 0.06, 0.12), 0.04, 0.04, "brass", "head", 14)
    k.box("holster", (0.06, 0.035, 0.16), (0.03, -0.2, 0.78), "leatherb", "thigh.R", bevel=0.012)   # (the flare gun, holstered)
    k.box("flaregun", (0.05, 0.03, 0.07), (0.035, -0.2, 0.88), "red", "thigh.R", bevel=0.01)


def o_sack(k):   # the Stowaway: a patched hooded cloak, a bandana mask, satchels and a bundle on a stick
    base(k, "sack", 1.05)
    hood(k, "patch", "shadow", "glint", pointed=False)
    k.ell("bandana", tuple(HC + V(0.13, 0, -0.06)), (0.06, 0.13, 0.06), "red", "head", 16)
    cape(k, "patch", "sack", 0.9, 0.24, 0.18)
    for i in range(6):
        k.box(f"patchc{i}", (0.008, 0.07, 0.07), (-0.18 - 0.03 * (i % 2), -0.12 + (i % 3) * 0.12, 1.2 - (i // 3) * 0.25), "sack", "chest", rot=(0.3 * i, 0, 0), bevel=0.003)
    trousers(k, "sack"); boots(k, "leatherx"); gloves(k, "rope")
    k.tube("satstrap", [V(0.14, 0.17, 1.43), V(0.18, 0.0, 1.2), V(0.12, -0.2, 0.98)], 0.015, "rope", "chest")
    k.box("satchel", (0.08, 0.16, 0.14), (0.05, -0.24, 0.95), "patch", "pelvis", bevel=0.02)
    p, d = held(k, "R")
    up = V(-0.3, 0, 1).normalized()
    k.tube("stick", [p - up * 0.2, p + up * 0.7], 0.012, "wood", "hand.R")
    k.ell("bundle", tuple(p + up * 0.72 + V(-0.05, 0, -0.05)), (0.1, 0.09, 0.1), "red", "hand.R", 16)


def o_sandwich(k):   # the Dock Boss: a pinstripe waistcoat and shirtsleeves, a bowler, a gold watch chain, a cane
    base(k, "shirt", 1.04)
    k.ell("waistcoat", (0.03, 0, 1.2), (0.2, 0.235, 0.26), "board", "chest", 28)
    for i in range(7):
        k.tube(f"pin{i}", [V(0.2, -0.15 + i * 0.05, 1.42), V(0.215, -0.15 + i * 0.05, 1.0)], 0.003, "paint2", "chest")
    for i in range(4):
        k.ell(f"wbtn{i}", (0.235, 0, 1.3 - i * 0.07), (0.01, 0.014, 0.014), "gold", "chest", 8)
    k.tube("watch", [V(0.23, 0.0, 1.16), V(0.235, 0.07, 1.12), V(0.225, 0.12, 1.15)], 0.006, "gold", "chest")
    k.ell("tie", (0.22, 0, 1.35), (0.02, 0.035, 0.08), "paint", "chest", 12)
    for s, sd in SIDES:   # sleeve garters
        fa = k.j(f"forearm.{sd}"); ua = k.j(f"upperarm.{sd}")
        k.cone(f"garter{sd}", ua + (fa - ua) * 0.55, ua + (fa - ua) * 0.62, 0.09, 0.09, "paint", f"upperarm.{sd}", 14)
    trousers(k, "paint2"); boots(k, "black", tall=False); k.mitts("mask")
    mask_face(k, "mask", "dark", "stitch")
    k.ell("moustache", tuple(HC + V(0.12, 0, -0.03)), (0.02, 0.08, 0.02), "beard", "head", 12)
    k.lathe("bowler", [(0.19, 0.0), (0.18, 0.015), (0.13, 0.02), (0.13, 0.1), (0.1, 0.15), (0.001, 0.16)], tuple(HC + V(0, 0, 0.1)), "black", "head", 28)
    p, d = held(k, "R")
    k.tube("cane", [p, p + V(0.02, 0, -0.9)], 0.012, "black", "hand.R"); k.ell("canetop", tuple(p + V(0, 0, 0.03)), (0.03, 0.03, 0.03), "gold", "hand.R", 12)


def o_lifebuoy(k):   # the Harbour Master: a white dress uniform with gold braid, a peaked cap, a life ring slung, a spyglass
    base(k, "whiteu", 1.04)
    coat_body(k, "whiteu", "gold", lapels=False, buttons="gold")
    for s in (1, -1):   # braid
        k.tube(f"braid{s}", [V(0.14, s * 0.18, 1.42), V(0.2, s * 0.12, 1.3), V(0.21, s * 0.05, 1.25)], 0.008, "gold", "chest")
    trousers(k, "navy"); boots(k, "black"); gloves(k, "whiteu")
    k.tube("ringsling", [V(0.17 * math.cos(a) + 0.02, 0.2 * math.sin(a), 1.12 + 0.18 * math.sin(a + 1)) for a in [j * 2 * math.pi / 20 for j in range(21)]], 0.05, "red", "chest")
    mask_face(k, "mask", "dark", None)
    k.ell("mbeard", tuple(HC + V(0.07, 0, -0.08)), (0.08, 0.12, 0.08), "white", "head", 18)
    k.lathe("hcap", [(0.13, 0.0), (0.15, 0.05), (0.17, 0.09), (0.001, 0.1)], tuple(HC + V(0, 0, 0.1)), "whiteu", "head", 28)
    k.lathe("hcapband", [(0.135, -0.005), (0.14, 0.045)], tuple(HC + V(0, 0, 0.1)), "navy", "head", 28)
    k.ell("hpeak", tuple(HC + V(0.14, 0, 0.1)), (0.08, 0.12, 0.012), "black", "head", 18)
    k.ell("hbadge", tuple(HC + V(0.14, 0, 0.16)), (0.008, 0.035, 0.02), "gold", "head", 10)
    p, d = held(k, "L")
    k.cone("spyglass", p + V(-0.05, 0, -0.02), p + V(0.3, 0, 0.1), 0.02, 0.03, "brass", "hand.L", 12)


def o_scarecrow(k):   # the Storm Scarecrow: a long tattered coat over a patched suit, a wide hat, straw everywhere, a crow
    base(k, "sack", 1.05)
    coat_body(k, "patch", "rope", lapels=True, buttons="button")
    coat_skirt(k, "patch", "rope", 0.6, 0.5, 0.21, 0.32)
    mask_face(k, "sack", "glowo", "stitch")
    k.lathe("whhat", [(0.4, 0.0), (0.38, 0.02), (0.17, 0.03), (0.16, 0.14), (0.13, 0.2), (0.001, 0.22)], tuple(HC + V(0, 0, 0.1)), "patch", "head", 28)
    for i in range(14):   # straw at the cuffs, the collar and the hat brim
        a = i * 2 * math.pi / 14
        k.cone(f"strawh{i}", HC + V(0.36 * math.cos(a), 0.36 * math.sin(a), 0.1), HC + V(0.44 * math.cos(a), 0.44 * math.sin(a), 0.05), 0.012, 0.002, "straw", "head", 4)
        k.cone(f"strawc{i}", V(0.13 * math.cos(a), 0.13 * math.sin(a), 1.47), V(0.18 * math.cos(a), 0.18 * math.sin(a), 1.4), 0.012, 0.002, "straw", "chest", 4)
    for s, sd in SIDES:
        h = k.j(f"hand.{sd}")
        for i in range(5):
            k.cone(f"strawhand{sd}{i}", h, h + V(0.04 * (i - 2), s * 0.05, -0.12), 0.01, 0.002, "straw", f"hand.{sd}", 4)
    trousers(k, "sack"); boots(k, "leatherx"); k.mitts("sack")
    k.ell("crow", (0.0, 0.27, 1.5), (0.06, 0.04, 0.05), "black", "upperarm.L", 12)
    k.cone("crowbeak", V(0.05, 0.27, 1.51), V(0.1, 0.27, 1.5), 0.012, 0.001, "yellow", "upperarm.L", 6)


def o_souwester(k):   # the Storm Rider: a glossy long oilskin coat, the great sou'wester, a knitted scarf, a storm lantern
    base(k, "oil", 1.06)
    coat_body(k, "oil", "oild", lapels=False, buttons="oild")
    coat_skirt(k, "oil", "oild", 0.75, 0.35, 0.22, 0.36)
    k.tube("kscarf", [V(0.14 * math.cos(a), 0.15 * math.sin(a), 1.46) for a in [j * 2 * math.pi / 16 for j in range(17)]], 0.045, "wool", "chest")
    k.tube("kscarft", [V(0.12, 0.06, 1.44), V(0.2, 0.1, 1.3), V(0.2, 0.12, 1.12)], 0.035, "wool", "chest")
    k.ell("shadowface", tuple(HC + V(0.01, 0, 0.0)), (0.12, 0.11, 0.135), "shadow", "head", 24)
    for s in (1, -1):
        k.ell(f"sglint{s}", tuple(HC + V(0.12, s * 0.045, 0.02)), (0.008, 0.012, 0.01), "glint", "head", 10)
    k.lathe("souw", [(0.45, -0.1), (0.4, -0.04), (0.17, 0.02), (0.15, 0.13), (0.001, 0.17)], tuple(HC + V(-0.04, 0, 0.08)), "oil", "head", 30)
    has_head(k)
    trousers(k, "oil"); boots(k, "black"); gloves(k, "oild")
    lantern(k, "glow", "iron", "L")


def o_mermaid(k):   # the Sea Siren: a scaled gown to the deck, a shell bodice, a pearl crown, a harp
    base(k, "top", 1.04)
    k.ell("bodice", (0.03, 0, 1.24), (0.165, 0.205, 0.13), "scale", "chest", 26)
    for s in (1, -1):
        k.ell(f"shellcup{s}", (0.17, s * 0.08, 1.28), (0.03, 0.065, 0.055), "shell", "chest", 14)
    for i in range(10):
        k.ell(f"pearl{i}", (0.2 - 0.003 * abs(i - 4.5), -0.12 + i * 0.027, 1.14), (0.012, 0.012, 0.012), "white", "chest", 8)
    segs = 20
    for part, bone in ((0, "thigh.L"), (1, "thigh.R")):   # the gown: scaled, flaring to fins at the deck
        verts, faces = [], []
        a0, a1 = (0.0, math.pi) if part == 0 else (math.pi, 2 * math.pi)
        for i in range(6):
            u = i / 5; z = 1.0 - 0.95 * u; r = 0.2 + 0.08 * u * u + 0.04 * (u > 0.8)
            for j in range(segs + 1):
                a = a0 + (a1 - a0) * j / segs
                verts.append((r * math.cos(a), r * math.sin(a), z))
        for i in range(5):
            for j in range(segs):
                p = i * (segs + 1) + j
                faces.append((p, p + 1, p + segs + 2, p + segs + 1))
        two_sided(k, f"gown{part}", verts, faces, "scale", bone)
    for i in range(8):
        a = i * 2 * math.pi / 8
        k.cone(f"gfinn{i}", V(0.27 * math.cos(a), 0.27 * math.sin(a), 0.1), V(0.4 * math.cos(a), 0.4 * math.sin(a), 0.02), 0.06, 0.004, "fin", "thigh.L" if math.sin(a) >= 0 else "thigh.R", 3)
    mask_face(k, "fin", "glowc", "shell")
    for i in range(11):
        a = math.pi * (0.5 + i / 10.0)
        p0 = HC + V(0.11 * math.cos(a) - 0.02, 0.12 * math.sin(a), 0.08)
        k.tube(f"lock{i}", [p0, p0 + V(-0.05, 0, -0.25), p0 + V(-0.08, 0.02 * math.sin(i), -0.55)], 0.025, "scale", "chest")
    for i in range(7):
        a = -0.9 + i * 0.3
        k.cone(f"pcrown{i}", HC + V(0.13 * math.cos(a), 0.13 * math.sin(a), 0.1), HC + V(0.14 * math.cos(a), 0.14 * math.sin(a), 0.2 - 0.04 * abs(a)), 0.015, 0.002, "gold", "head", 6)
        k.ell(f"pcp{i}", tuple(HC + V(0.14 * math.cos(a), 0.14 * math.sin(a), 0.2 - 0.04 * abs(a))), (0.012, 0.012, 0.012), "white", "head", 8)
    gloves(k, "top")
    p, d = held(k, "L")
    harp = [p + V(0.05, 0, 0.0), p + V(0.08, 0, 0.25), p + V(0.2, 0, 0.3), p + V(0.25, 0, 0.1), p + V(0.05, 0, 0.0)]
    k.tube("harp", harp, 0.012, "gold", "hand.L")


def o_pirate(k):   # the Pirate Captain: a crimson greatcoat with gold frogging, a plumed tricorn, a cutlass and a pistol
    base(k, "shirt", 1.04)
    coat_body(k, "coat", "gold", lapels=True, buttons="gold")
    coat_skirt(k, "coat", "gold", 0.65, 0.55, 0.21, 0.33)
    for i in range(4):
        for s in (1, -1):
            k.tube(f"frog{i}{s}", [V(0.2, s * 0.03, 1.32 - i * 0.07), V(0.2, s * 0.12, 1.32 - i * 0.07)], 0.006, "gold", "chest" if i < 2 else "spine")
    k.tube("sashp", [V(0.19 * math.cos(a), 0.21 * math.sin(a), 1.0) for a in [j * 2 * math.pi / 20 for j in range(21)]], 0.03, "sashc", "pelvis")
    k.tube("baldric", [V(0.14, 0.17, 1.43), V(0.2, 0.0, 1.2), V(0.12, -0.2, 0.98)], 0.022, "leather", "chest")
    trousers(k, "black"); boots(k, "leather"); gloves(k, "leather")
    for s, sd in SIDES:   # cuffed boot tops
        k.lathe(f"cuffb{sd}", [(0.11, 0.0), (0.13, 0.08)], (0.015, s * 0.112, 0.42), "leather", f"shin.{sd}", 18)
    mask_face(k, "mask", "dark", None)
    k.ell("beardp", tuple(HC + V(0.07, 0, -0.08)), (0.09, 0.12, 0.09), "beard", "head", 18)
    k.ell("patch", tuple(HC + V(0.115, -0.045, 0.025)), (0.01, 0.03, 0.025), "black", "head", 10)
    k.lathe("tricornp", [(0.001, 0.0), (0.33, 0.0), (0.31, 0.03), (0.15, 0.05), (0.14, 0.13), (0.001, 0.15)], tuple(HC + V(0, 0, 0.12)), "black", "head", 3)
    k.tube("tritrimp", [HC + V(0.33 * math.cos(a), 0.33 * math.sin(a), 0.13) for a in [j * 2 * math.pi / 3 for j in range(4)]], 0.012, "gold", "head")
    k.tube("plume", [HC + V(-0.05, 0.1, 0.2), HC + V(-0.2, 0.18, 0.32), HC + V(-0.38, 0.22, 0.28)], 0.03, "white", "head")
    blade(k, "steelg", "gold", "R", 0.75, 0.04, 0.12)
    p, d = held(k, "L")
    k.box("pistol", (0.22, 0.03, 0.04), tuple(p + V(0.12, 0, 0.03)), "leather", "hand.L", bevel=0.008)
    k.tube("pbarrel", [p + V(0.2, 0, 0.04), p + V(0.36, 0, 0.06)], 0.012, "steelg", "hand.L")


def o_barrel(k):   # the Cooper's Brute: braces, a leather apron, iron-hooped bracers, a barrel-lid shield and a mallet
    base(k, "shirt", 1.08)
    k.ell("apron", (0.12, 0, 1.0), (0.1, 0.2, 0.35), "leather", "spine", 22)
    for s in (1, -1):
        k.tube(f"brace{s}", [V(0.15, s * 0.1, 0.98), V(0.2, s * 0.1, 1.3), V(0.08, s * 0.12, 1.47), V(-0.15, s * 0.08, 1.3), V(-0.14, s * 0.06, 0.98)], 0.014, "leather", "chest")
    for s, sd in SIDES:
        fa, h = k.j(f"forearm.{sd}"), k.j(f"hand.{sd}")
        for i in range(3):
            c = fa + (h - fa) * (0.2 + 0.3 * i)
            k.cone(f"hoop{sd}{i}", c - (h - fa) * 0.04, c + (h - fa) * 0.04, 0.085, 0.085, "iron", f"forearm.{sd}", 16)
    trousers(k, "wood"); boots(k, "boot"); gloves(k, "leather")
    k.lathe("bhelm", [(0.001, -0.18), (0.2, -0.18), (0.21, 0.1), (0.19, 0.18), (0.001, 0.2)], tuple(HC), "wood", "head", 20)
    for z in (-0.12, 0.0, 0.12):
        ring(k, f"bhoop{z}", tuple(HC + V(0, 0, z)), 0.212, 0.02, "iron", "head", 24)
    k.ell("bslot", tuple(HC + V(0.2, 0, 0.04)), (0.02, 0.12, 0.025), "shadow", "head", 14)
    for s in (1, -1):
        k.ell(f"beye{s}", tuple(HC + V(0.21, s * 0.05, 0.04)), (0.008, 0.015, 0.01), "glowo", "head", 8)
    has_head(k)
    p, d = held(k, "L")
    k.lathe("lid", [(0.001, -0.02), (0.26, -0.02), (0.27, 0.02), (0.001, 0.02)], tuple(p + V(0.08, 0.06, 0)), "wood", "hand.L", 24)
    p2, d2 = held(k, "R")
    k.tube("mhaft", [p2 - V(0, 0, 0.2), p2 + V(0.15, 0, 0.5)], 0.02, "wood", "hand.R")
    k.lathe("mhead", [(0.001, -0.1), (0.08, -0.1), (0.08, 0.1), (0.001, 0.1)], tuple(p2 + V(0.17, 0, 0.55)), "iron", "hand.R", 14, 1.0, 1.0)


def o_gannet(k):   # the Ironclad Skipper: a riveted steel coat, a funnel-stack helm venting glow, a ship's wheel shield
    base(k, "hull", 1.06)
    coat_body(k, "hull", "trim", lapels=False, buttons="glow")
    coat_skirt(k, "hull", "trim", 0.55, 0.5, 0.21, 0.31)
    pauldrons(k, "hull", "trim", 1.15, 3)
    for i in range(10):
        a = -1.0 + i * 0.22
        k.ell(f"crivet{i}", (0.2 * math.cos(a), 0.23 * math.sin(a), 1.35), (0.012, 0.012, 0.012), "trim", "chest", 8)
    trousers(k, "hull"); boots(k, "trim"); gloves(k, "trim")
    k.lathe("shelm", [(0.001, -0.2), (0.19, -0.19), (0.21, 0.0), (0.18, 0.12), (0.001, 0.15)], tuple(HC), "hull", "head", 30)
    for s in (1, -1):
        k.ell(f"sport{s}", tuple(HC + V(0.19, s * 0.07, 0.03)), (0.02, 0.045, 0.045), "glow", "head", 16)
    k.lathe("funnel", [(0.07, 0.0), (0.075, 0.18), (0.085, 0.2)], tuple(HC + V(-0.04, 0, 0.13)), "red", "head", 18)
    k.lathe("funneltop", [(0.085, 0.0), (0.075, 0.03)], tuple(HC + V(-0.04, 0, 0.33)), "black", "head", 18)
    has_head(k)
    p, d = held(k, "L")   # the wheel
    c = p + V(0.1, 0.06, 0)
    k.lathe("wheelrim", [(0.22, -0.015), (0.24, 0.0), (0.22, 0.015)], tuple(c), "wood", "hand.L", 24)
    for i in range(8):
        a = i * math.pi / 4
        k.tube(f"spoke{i}", [c, c + V(0.3 * math.cos(a), 0.3 * math.sin(a), 0)], 0.012, "wood", "hand.L")


def o_orca(k):   # the Orca Commander: black-and-white armour like the whale, a dorsal blade, a glowing harpoon lance
    base(k, "black", 1.05)
    cuirass(k, "black", "white"); pauldrons(k, "black", "white", 1.1, 3); arm_armour(k, "black", "white"); leg_armour(k, "black", "white")
    k.ell("orcabelly", (0.2, 0, 1.15), (0.04, 0.15, 0.22), "white", "chest", 18)
    k.cone("odorsal", V(-0.17, 0, 1.3), V(-0.3, 0, 1.75), 0.12, 0.005, "black", "chest", 3)
    belt(k, "white", "steelg"); boots(k, "black"); k.mitts("black")
    def crest(k):
        for s in (1, -1):
            k.ell(f"opatch{s}", tuple(HC + V(0.13, s * 0.12, 0.06)), (0.04, 0.05, 0.03), "white", "head", 12)
    helm(k, "black", "white", "visor_t", 0.13, crest)
    cape(k, "black", "white", 0.9, 0.24, 0.18)
    spear(k, "steelg", "glow", "R", 1.8)


STEEL_M = "steelg"
OUTFITS = {
    "lobster": (o_lobster, {"shell": COL(0.62, 0.08, 0.04, 0.35, 0.2), "under": COL(0.12, 0.08, 0.07, 0.8), "cloth": COL(0.3, 0.04, 0.04, 0.85), "visor_d": COL(0.05, 0.05, 0.06, 0.15, 0.6)}),
    "crab": (o_crab, {"shell": COL(0.78, 0.33, 0.06, 0.3, 0.1), "lacing": COL(0.05, 0.05, 0.06, 0.7), "under": COL(0.12, 0.1, 0.09, 0.8), "visor_d": COL(0.05, 0.05, 0.06, 0.15, 0.6)}),
    "pufferfish": (o_pufferfish, {"skin": COL(0.75, 0.6, 0.18, 0.5), "suit": COL(0.1, 0.12, 0.16, 0.6), "spine_m": COL(0.95, 0.9, 0.75, 0.4), "visor_d": COL(0.1, 0.25, 0.3, 0.1, 0.5)}),
    "jelly": (o_jelly, {"suit": COL(0.03, 0.03, 0.05, 0.3), "bell": COL(0.75, 0.5, 0.85, 0.1), "tent": COL(0.95, 0.6, 0.95, 0.2), "glow": COL(0.6, 1.0, 1.0, 0.1)}),
    "hermit": (o_hermit, {"cloak": COL(0.45, 0.2, 0.12, 0.85), "canvas": COL(0.55, 0.48, 0.36, 0.9), "wrap": COL(0.75, 0.68, 0.5, 0.9), "shell": COL(0.9, 0.78, 0.6, 0.4), "shell2": COL(0.75, 0.52, 0.35, 0.4), "wood": COL(0.35, 0.22, 0.1, 0.7)}),
    "starfish": (o_starfish, {"suitw": COL(0.9, 0.88, 0.84, 0.35), "star": COL(0.92, 0.42, 0.18, 0.3, 0.1), "glow": COL(1.0, 0.85, 0.5, 0.1), "visor_g": COL(0.2, 0.6, 0.8, 0.05, 0.4)}),
    "kelp": (o_kelp, {"leathg": COL(0.18, 0.22, 0.12, 0.7), "kelp": COL(0.22, 0.34, 0.1, 0.6), "kelp2": COL(0.38, 0.36, 0.1, 0.6), "float": COL(0.55, 0.45, 0.12, 0.4), "wood": COL(0.35, 0.22, 0.1, 0.7), "brass": COL(0.75, 0.55, 0.22, 0.3, 0.9)}),
    "barnacle": (o_barnacle, {"iron": COL(0.3, 0.32, 0.34, 0.45, 0.85), "rust": COL(0.45, 0.22, 0.1, 0.8, 0.3), "barn": COL(0.85, 0.82, 0.74, 0.8), "under": COL(0.12, 0.1, 0.09, 0.8), "visor_d": COL(0.04, 0.04, 0.05, 0.2, 0.6), "wood": COL(0.35, 0.22, 0.1, 0.7)}),
    "shark": (o_shark, {"grey": COL(0.3, 0.35, 0.4, 0.35), "pale": COL(0.85, 0.86, 0.85, 0.4), "teal": COL(0.3, 0.95, 0.9, 0.1), "visor_t": COL(0.05, 0.3, 0.35, 0.05, 0.5)}),
    "octopus": (o_octopus, {"robe": COL(0.28, 0.08, 0.35, 0.7), "skin": COL(0.5, 0.15, 0.5, 0.4), "sucker": COL(0.9, 0.65, 0.8, 0.4), "glowv": COL(0.8, 0.5, 1.0, 0.1), "wood": COL(0.2, 0.12, 0.08, 0.6)}),
    "angler": (o_angler, {"dark": COL(0.05, 0.05, 0.07, 0.6), "darkt": COL(0.18, 0.16, 0.2, 0.5), "glow": COL(0.75, 1.0, 0.8, 0.1)}),
    "turtle": (o_turtle, {"skin": COL(0.32, 0.45, 0.24, 0.6), "shell": COL(0.35, 0.24, 0.1, 0.35, 0.1), "scute": COL(0.62, 0.48, 0.18, 0.35, 0.2), "belly": COL(0.82, 0.76, 0.52, 0.5), "visor_d": COL(0.05, 0.08, 0.05, 0.15, 0.5), "wood": COL(0.35, 0.22, 0.1, 0.7)}),
    "ghost": (o_ghost, {"sheet": COL(0.72, 0.84, 0.82, 0.4), "sheetd": COL(0.45, 0.58, 0.58, 0.45), "glowg": COL(0.4, 1.0, 0.6, 0.1), "iron": COL(0.25, 0.27, 0.28, 0.5, 0.8)}),
    "coral": (o_coral, {"pearl": COL(0.9, 0.86, 0.85, 0.2, 0.3), "coral1": COL(0.95, 0.32, 0.45, 0.4), "coral2": COL(0.98, 0.6, 0.2, 0.4), "coral3": COL(0.6, 0.32, 0.8, 0.4), "visor_g": COL(0.3, 0.7, 0.8, 0.05, 0.4)}),
    "divingbell": (o_divingbell, {"brass": COL(0.78, 0.56, 0.22, 0.25, 0.95), "brass_d": COL(0.52, 0.36, 0.14, 0.3, 0.9), "glass": COL(0.35, 0.55, 0.6, 0.03), "iron": COL(0.18, 0.18, 0.19, 0.5, 0.8), "lead": COL(0.3, 0.3, 0.32, 0.6, 0.7)}),
    "seamine": (o_seamine, {"olive": COL(0.28, 0.3, 0.18, 0.7), "iron": COL(0.12, 0.12, 0.13, 0.45, 0.85), "brass": COL(0.72, 0.52, 0.2, 0.3, 0.9), "visor_g": COL(0.2, 0.5, 0.4, 0.05, 0.4), "glowr": COL(1.0, 0.2, 0.15, 0.1)}),
    "swordfish": (o_swordfish, {"blue": COL(0.06, 0.12, 0.3, 0.45), "silver": COL(0.78, 0.8, 0.85, 0.2, 0.9), "silverw": COL(0.9, 0.9, 0.92, 0.4), "visor_d": COL(0.03, 0.05, 0.1, 0.1, 0.6)}),
    "kraken": (o_kraken, {"red": COL(0.5, 0.05, 0.06, 0.4), "blackp": COL(0.08, 0.07, 0.08, 0.3, 0.6), "yellow": COL(1.0, 0.8, 0.2, 0.2), "visor_d": COL(0.15, 0.02, 0.02, 0.1, 0.6)}),
    "goliath": (o_goliath, {"olive": COL(0.3, 0.32, 0.16, 0.6), "plate": COL(0.4, 0.4, 0.36, 0.4, 0.7), "lip": COL(0.5, 0.36, 0.2, 0.6), "pale": COL(0.78, 0.74, 0.55, 0.5), "iron": COL(0.22, 0.2, 0.18, 0.5, 0.8), "glowy": COL(1.0, 0.85, 0.3, 0.1), "visor_d": COL(0.04, 0.04, 0.03, 0.2, 0.6)}),
    "nautilus": (o_nautilus, {"navy": COL(0.05, 0.08, 0.2, 0.6), "navyl": COL(0.1, 0.12, 0.22, 0.7), "brass": COL(0.8, 0.58, 0.22, 0.25, 0.95), "glow": COL(1.0, 0.85, 0.5, 0.1)}),
    "fish": (o_fish, {"silver": COL(0.6, 0.66, 0.72, 0.3, 0.6), "fin": COL(0.35, 0.22, 0.12, 0.6), "lip": COL(0.7, 0.2, 0.15, 0.8), "bone": COL(0.88, 0.85, 0.76, 0.5), "glow": COL(0.6, 0.95, 1.0, 0.1)}),
    "gull": (o_gull, {"grey": COL(0.55, 0.57, 0.6, 0.6), "leatherb": COL(0.35, 0.2, 0.1, 0.55), "yellow": COL(0.95, 0.75, 0.15, 0.4), "red": COL(0.8, 0.1, 0.05, 0.5), "brass": COL(0.75, 0.55, 0.22, 0.3, 0.9), "visor_g": COL(0.25, 0.55, 0.6, 0.05, 0.3)}),
    "sack": (o_sack, {"sack": COL(0.5, 0.42, 0.28, 0.95), "patch": COL(0.3, 0.32, 0.36, 0.9), "rope": COL(0.45, 0.35, 0.2, 0.9), "red": COL(0.6, 0.1, 0.08, 0.85), "wood": COL(0.35, 0.22, 0.1, 0.7)}),
    "sandwich": (o_sandwich, {"shirt": COL(0.9, 0.88, 0.84, 0.7), "board": COL(0.18, 0.18, 0.22, 0.6), "paint": COL(0.55, 0.08, 0.08, 0.6), "paint2": COL(0.12, 0.13, 0.2, 0.6), "gold": COL(0.85, 0.62, 0.2, 0.3, 0.9)}),
    "lifebuoy": (o_lifebuoy, {"whiteu": COL(0.92, 0.92, 0.9, 0.5), "red": COL(0.82, 0.12, 0.08, 0.5), "brass": COL(0.78, 0.56, 0.22, 0.25, 0.95)}),
    "scarecrow": (o_scarecrow, {"sack": COL(0.5, 0.4, 0.25, 0.95), "straw": COL(0.85, 0.7, 0.3, 0.9), "patch": COL(0.22, 0.25, 0.3, 0.9), "rope": COL(0.45, 0.35, 0.2, 0.9), "glowo": COL(1.0, 0.6, 0.15, 0.1), "yellow": COL(0.95, 0.75, 0.15, 0.4)}),
    "souwester": (o_souwester, {"oil": COL(0.95, 0.72, 0.05, 0.2), "oild": COL(0.3, 0.25, 0.08, 0.4), "wool": COL(0.55, 0.12, 0.1, 0.95), "glow": COL(1.0, 0.8, 0.45, 0.1), "iron": COL(0.18, 0.18, 0.19, 0.5, 0.8)}),
    "mermaid": (o_mermaid, {"top": COL(0.15, 0.45, 0.42, 0.5), "scale": COL(0.08, 0.55, 0.5, 0.25, 0.3), "fin": COL(0.3, 0.75, 0.65, 0.3), "shell": COL(0.92, 0.72, 0.78, 0.3), "glowc": COL(0.5, 1.0, 0.95, 0.1)}),
    "pirate": (o_pirate, {"shirt": COL(0.9, 0.86, 0.78, 0.7), "coat": COL(0.42, 0.05, 0.06, 0.5), "leather": COL(0.22, 0.13, 0.06, 0.55), "sashc": COL(0.12, 0.2, 0.42, 0.8)}),
    "barrel": (o_barrel, {"shirt": COL(0.82, 0.78, 0.68, 0.8), "wood": COL(0.42, 0.26, 0.12, 0.75), "iron": COL(0.2, 0.2, 0.21, 0.45, 0.85), "leather": COL(0.25, 0.15, 0.08, 0.6), "glowo": COL(1.0, 0.6, 0.15, 0.1)}),
    "gannet": (o_gannet, {"hull": COL(0.12, 0.18, 0.3, 0.4, 0.6), "trim": COL(0.6, 0.12, 0.08, 0.5), "wood": COL(0.4, 0.25, 0.12, 0.7), "glow": COL(1.0, 0.85, 0.5, 0.1), "red": COL(0.7, 0.1, 0.06, 0.5)}),
    "orca": (o_orca, {"black": COL(0.03, 0.03, 0.04, 0.3, 0.3), "white": COL(0.92, 0.92, 0.9, 0.35), "glow": COL(0.5, 0.9, 1.0, 0.1), "visor_t": COL(0.05, 0.25, 0.35, 0.05, 0.5)}),
}
COMMON_COLS = {   # (every outfit can use these; its own table overrides them)
    "gold": GOLD, "steelg": STEEL, "leatherx": LEATHERX, "glint": COL(1.0, 0.9, 0.5, 0.2), "shadow": COL(0.02, 0.02, 0.03, 0.95),
    "under": COL(0.12, 0.1, 0.09, 0.8), "visor_d": COL(0.05, 0.05, 0.06, 0.15, 0.6), "visor_g": COL(0.25, 0.6, 0.75, 0.05, 0.4),
    "visor_t": COL(0.05, 0.3, 0.35, 0.05, 0.5), "wood": COL(0.35, 0.22, 0.1, 0.7), "brass": COL(0.78, 0.56, 0.22, 0.25, 0.95),
    "brass_d": COL(0.52, 0.36, 0.14, 0.3, 0.9), "iron": COL(0.2, 0.2, 0.21, 0.45, 0.85), "leather": COL(0.25, 0.15, 0.08, 0.6),
    "glow": COL(1.0, 0.85, 0.5, 0.1), "red": COL(0.7, 0.1, 0.06, 0.5), "silver": COL(0.75, 0.78, 0.82, 0.25, 0.85),
    "rope": COL(0.45, 0.35, 0.2, 0.9), "glass": COL(0.35, 0.55, 0.6, 0.03), "yellow": COL(0.95, 0.75, 0.15, 0.4),
}


def build(name, out):
    fn, cols = OUTFITS[name]
    CO.COSTUMES[name] = (fn, cols)
    allc = dict(COMMON_COLS); allc.update(cols)
    # (costumes.build adds the base and extra colours, the rig and the export; complete() does nothing for a known
    # outfit since each builds its own head, hands and feet)
    CO.complete = lambda n, k: None
    saved = dict(CO.EXTRA_COLS)
    CO.EXTRA_COLS.update(COMMON_COLS)
    CO.COSTUMES[name] = (fn, allc)
    CO.build(name, out)
    CO.EXTRA_COLS.clear(); CO.EXTRA_COLS.update(saved)


if __name__ == "__main__" or True:
    a = C.args()
    only = a[a.index("--only") + 1].split(",") if "--only" in a else None
    out = C.out_dir()
    for n in OUTFITS:
        if only and n not in only:
            continue
        build(n, out)
