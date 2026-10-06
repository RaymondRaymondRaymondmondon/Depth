# Ball Pit Brawl's props (the spec's milestone 8, "the full map and art pass"): toy foam blasters in bright moulded
# plastic, the foam knife, the hand vacuum, the giant foam finger, the ball cannon (base and barrel), the reward toys
# (the ride-on dart tank, the RC car, the dart drone, the first-aid box), the foam bomb and the team flag.
# Built in GAME coordinates through fowl_props' G() helpers (x forward, y up, z right), each about its own origin.
# The blasters' grip is at the origin (the hand closes there) and their lengths match GunInfo in ballpit_game.cpp.
#     blender -b --factory-startup -P tools/artgen/ballpit_props.py -- --out assets/ballpit [--only pistol,cannon]
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
    mat("orange", (0.95, 0.38, 0.06), 0.35)
    mat("yellow", (0.98, 0.78, 0.08), 0.35)
    mat("blue", (0.08, 0.32, 0.85), 0.35)
    mat("green", (0.18, 0.7, 0.2), 0.35)
    mat("purple", (0.5, 0.22, 0.75), 0.35)
    mat("white", (0.92, 0.92, 0.9), 0.4)
    mat("grey", (0.35, 0.37, 0.42), 0.5)
    mat("grip", (0.06, 0.06, 0.07), 0.8)
    mat("foam", (0.98, 0.45, 0.1), 0.9)
    mat("foamblue", (0.1, 0.35, 0.9), 0.9)
    mat("tealpad", (0.1, 0.6, 0.65), 0.7)
    mat("red", (0.85, 0.08, 0.06), 0.4)
    mat("rubber", (0.04, 0.04, 0.04), 0.8)
    mat("chrome", (0.86, 0.86, 0.88), 0.15, 1.0)


# ---------------------------------------------------------------- shared blaster pieces
def handle(col="grip", at=(0, 0, 0), tilt=0.25, h=0.14):
    """A pistol grip below the origin, raked back, with finger bumps and a trigger and guard in front."""
    x, y, z = at
    gbox("handle", (0.05, h, 0.042), (x - 0.015, y - h / 2 + 0.01, z), col, 0.014, rot=(0, -tilt, 0))
    for k in range(3):
        gbox(f"bump{k}", (0.012, 0.022, 0.044), (x + 0.012 - k * 0.004, y - 0.03 - k * 0.032, z), col, 0.006, rot=(0, -tilt, 0))
    gbox("trigger", (0.012, 0.03, 0.012), (x + 0.045, y - 0.03, z), "yellow", 0.004, rot=(0, 0.3, 0))
    tube("guard", [G(x + 0.025, y - 0.005, z), G(x + 0.08, y - 0.02, z), G(x + 0.075, y - 0.065, z), G(x + 0.03, y - 0.07, z)], 0.006, col)


def muzzle(at, r, col="orange", length=0.03):
    x, y, z = at
    gcyl("muzzle", r, length, (x, y, z), col, axis="x", verts=20)
    gcyl("bore", r * 0.55, length + 0.002, (x + 0.001, y, z), "grip", axis="x", verts=16)


def dart_tips(x0, y, z, n, col="foamblue"):
    for k in range(n):
        sphere(f"tip{k}", G(x0 + k * 0.02, y, z), 0.008, col, segs=10)


# ---------------------------------------------------------------- the blasters
def starter():   # a single-shot plunger pistol: orange shell, a yellow barrel, a blue pull-back plunger handle
    handle("grey")
    gbox("body", (0.22, 0.07, 0.05), (0.075, 0.04, 0), "orange", 0.022)
    gbox("rib", (0.2, 0.012, 0.03), (0.075, 0.082, 0), "yellow", 0.005)
    gcyl("barrel", 0.019, 0.1, (0.21, 0.058, 0), "yellow", axis="x")
    muzzle((0.262, 0.058, 0), 0.022)
    gcyl("plungerrod", 0.009, 0.08, (-0.07, 0.05, 0), "grey", axis="x", verts=12)
    sphere("plungerknob", G(-0.115, 0.05, 0), 0.025, "blue", scale=(0.8, 1, 1), segs=16)
    for s in (-1, 1):
        gbox(f"panel{s}", (0.1, 0.035, 0.004), (0.08, 0.035, s * 0.026), "yellow", 0.003)


def pistol():   # a semi-auto: blue frame, an orange slide with grip serrations, a yellow magazine in the handle
    handle("grip")
    gbox("frame", (0.2, 0.045, 0.044), (0.07, 0.025, 0), "blue", 0.016)
    gbox("slide", (0.22, 0.04, 0.04), (0.075, 0.07, 0), "orange", 0.012)
    for k in range(5):
        gbox(f"serr{k}", (0.005, 0.03, 0.042), (-0.015 + k * 0.012, 0.07, 0), "grip", 0.001)
    gbox("mag", (0.034, 0.03, 0.03), (-0.025, -0.13, 0), "yellow", 0.006, rot=(0, -0.25, 0))
    muzzle((0.19, 0.07, 0), 0.016)
    gbox("sight", (0.012, 0.012, 0.006), (0.17, 0.095, 0), "yellow", 0.002)
    dart_tips(-0.035, -0.1, 0, 2)


def revolver():   # a six-shot: a grey frame, a turning cylinder of six chambers, a long orange barrel, a wood-look grip
    handle("orange")
    gbox("frame", (0.13, 0.065, 0.045), (0.04, 0.04, 0), "grey", 0.015)
    gcyl("drum", 0.04, 0.065, (0.07, 0.06, 0), "yellow", axis="x", verts=24)
    for k in range(6):
        a = k * math.pi / 3
        gcyl(f"chamber{k}", 0.011, 0.068, (0.07, 0.06 + math.cos(a) * 0.026, math.sin(a) * 0.026), "foamblue", axis="x", verts=10)
    gcyl("barrel", 0.017, 0.17, (0.2, 0.072, 0), "orange", axis="x")
    gbox("underlug", (0.15, 0.016, 0.02), (0.19, 0.05, 0), "orange", 0.006)
    muzzle((0.29, 0.072, 0), 0.02, "yellow")
    gbox("hammer", (0.02, 0.03, 0.014), (-0.03, 0.085, 0), "grip", 0.004, rot=(0, 0.4, 0))
    gbox("sight", (0.01, 0.014, 0.006), (0.27, 0.095, 0), "yellow", 0.002)


def dual():   # one of a pair of mini blasters: chunky yellow with purple accents (the game draws two)
    handle("grip", h=0.12)
    gbox("body", (0.15, 0.065, 0.046), (0.05, 0.035, 0), "yellow", 0.02)
    gbox("top", (0.1, 0.02, 0.036), (0.04, 0.078, 0), "purple", 0.008)
    gcyl("barrel", 0.016, 0.07, (0.15, 0.05, 0), "purple", axis="x")
    muzzle((0.19, 0.05, 0), 0.019, "orange")
    for s in (-1, 1):
        gcyl(f"bolt{s}", 0.008, 0.004, (0.0, 0.035, s * 0.025), "grey", axis="z", verts=10)


def pump():   # a pump-action scatter blaster: green body, a fat orange barrel over a yellow pump grip, a stock
    handle("grip")
    gbox("body", (0.36, 0.08, 0.06), (0.11, 0.045, 0), "green", 0.024)
    gcyl("barrel", 0.032, 0.26, (0.36, 0.07, 0), "orange", axis="x", verts=24)
    muzzle((0.5, 0.07, 0), 0.036, "yellow")
    gbox("pumpgrip", (0.13, 0.05, 0.07), (0.27, 0.012, 0), "yellow", 0.018)
    for k in range(4):
        gbox(f"rib{k}", (0.006, 0.054, 0.074), (0.22 + k * 0.03, 0.012, 0), "orange", 0.002)
    gbox("stock", (0.16, 0.07, 0.044), (-0.13, 0.035, 0), "green", 0.02, rot=(0, 0.12, 0))
    gbox("pad", (0.02, 0.08, 0.05), (-0.21, 0.025, 0), "grip", 0.008, rot=(0, 0.12, 0))
    for k in range(4):
        gcyl(f"shell{k}", 0.012, 0.03, (0.05 + k * 0.03, 0.045, 0.034), "red", axis="z", verts=10)


def burst():   # a three-round burst: white with orange panels, a top rail and sight, a straight magazine, a skeleton stock
    handle("grip")
    gbox("body", (0.36, 0.075, 0.052), (0.12, 0.045, 0), "white", 0.02)
    for s in (-1, 1):
        gbox(f"panel{s}", (0.18, 0.04, 0.004), (0.13, 0.045, s * 0.028), "orange", 0.003)
    gbox("rail", (0.24, 0.012, 0.03), (0.13, 0.09, 0), "grip", 0.003)
    for k in range(8):
        gbox(f"tooth{k}", (0.012, 0.006, 0.032), (0.03 + k * 0.028, 0.098, 0), "grip", 0.001)
    gbox("sightbody", (0.06, 0.04, 0.03), (0.15, 0.12, 0), "orange", 0.01)
    gcyl("lens", 0.012, 0.062, (0.15, 0.124, 0), "glass", axis="x", verts=12)
    gbox("mag", (0.045, 0.12, 0.034), (0.06, -0.05, 0), "orange", 0.01)
    dart_tips(0.045, 0.012, 0, 2)
    gcyl("barrel", 0.016, 0.14, (0.37, 0.06, 0), "blue", axis="x")
    muzzle((0.45, 0.06, 0), 0.02, "orange")
    tube("stock", [G(-0.04, 0.07, 0), G(-0.18, 0.07, 0), G(-0.19, -0.02, 0), G(-0.05, 0.0, 0)], 0.012, "white")


def flywheel():   # a flywheel auto: a blue body with a big front cage, a clear drum magazine, a battery tray
    handle("grip")
    gbox("body", (0.3, 0.09, 0.064), (0.1, 0.05, 0), "blue", 0.026)
    gcyl("cage", 0.042, 0.14, (0.37, 0.06, 0), "yellow", axis="x", verts=24)
    for k in range(8):
        a = k * math.pi / 4
        gbox(f"slot{k}", (0.11, 0.006, 0.01), (0.37, 0.06 + math.cos(a) * 0.043, math.sin(a) * 0.043), "grip", 0.002, rot=(a, 0, 0))
    muzzle((0.45, 0.06, 0), 0.026, "orange")
    gcyl("drum", 0.065, 0.06, (0.1, -0.06, 0), "glass", axis="z", verts=28)
    gcyl("hub", 0.02, 0.064, (0.1, -0.06, 0), "orange", axis="z", verts=12)
    for k in range(10):
        a = k * math.pi / 5
        gcyl(f"drumdart{k}", 0.008, 0.05, (0.1 + math.cos(a) * 0.045, -0.06 + math.sin(a) * 0.045, 0), "foam", axis="z", verts=8)
    gbox("battery", (0.09, 0.03, 0.05), (-0.09, 0.03, 0), "yellow", 0.01)
    gbox("rev", (0.02, 0.02, 0.01), (0.09, 0.012, 0.03), "red", 0.004)


def crossbow():   # the mega dart crossbow: an orange stock and rail, blue limbs, a string, a huge foam dart loaded
    handle("grip")
    gbox("stock", (0.5, 0.06, 0.05), (0.18, 0.045, 0), "orange", 0.02)
    gbox("butt", (0.14, 0.09, 0.05), (-0.13, 0.03, 0), "orange", 0.022, rot=(0, 0.1, 0))
    for s in (-1, 1):
        tube(f"limb{s}", [G(0.38, 0.06, 0), G(0.36, 0.07, s * 0.12), G(0.3, 0.075, s * 0.24)], 0.016, "blue")
    tube("string", [G(0.3, 0.075, 0.24), G(0.1, 0.07, 0), G(0.3, 0.075, -0.24)], 0.003, "white")
    gcyl("megadart", 0.04, 0.34, (0.27, 0.11, 0), "foam", axis="x", verts=20)
    sphere("megatip", G(0.44, 0.11, 0), 0.044, "foamblue", scale=(0.8, 1, 1), segs=16)
    gbox("sight", (0.02, 0.04, 0.008), (0.05, 0.1, 0), "yellow", 0.003)


def belt():   # the belt-fed heavy: a grey box receiver, six red barrels round a spindle, a yellow ammo box, a belt of darts
    handle("grip")
    gbox("receiver", (0.32, 0.13, 0.1), (0.15, 0.06, 0), "grey", 0.03)
    gbox("cover", (0.3, 0.03, 0.09), (0.15, 0.14, 0), "orange", 0.012)
    for k in range(6):
        a = k * math.pi / 3
        gcyl(f"barrel{k}", 0.012, 0.2, (0.42, 0.06 + math.cos(a) * 0.035, math.sin(a) * 0.035), "red", axis="x", verts=12)
    gcyl("spindle", 0.016, 0.21, (0.42, 0.06, 0), "grip", axis="x", verts=12)
    torus("band", G(0.5, 0.06, 0), 0.05, 0.008, "yellow", rot=(0, math.pi / 2, 0))
    gbox("ammobox", (0.16, 0.12, 0.09), (0.1, -0.06, 0.1), "yellow", 0.02)
    for k in range(7):
        gcyl(f"beltdart{k}", 0.008, 0.06, (0.08 + k * 0.015, 0.01 + k * 0.007, 0.06), "foam", axis="z", verts=8)
    tube("carry", [G(0.08, 0.15, 0), G(0.15, 0.2, 0), G(0.24, 0.15, 0)], 0.012, "grip")


def longgun():   # the long-range blaster: a purple body, a long yellow barrel, a scope, a bipod folded under
    handle("grip")
    gbox("body", (0.38, 0.07, 0.048), (0.12, 0.045, 0), "purple", 0.018)
    gcyl("barrel", 0.017, 0.48, (0.56, 0.06, 0), "yellow", axis="x", verts=16)
    muzzle((0.83, 0.06, 0), 0.022, "orange")
    gcyl("scope", 0.024, 0.24, (0.15, 0.13, 0), "grip", axis="x", verts=20)
    for s in (-1, 1):
        gcyl(f"bell{s}", 0.03, 0.04, (0.15 + s * 0.11, 0.13, 0), "grip", axis="x", verts=20)
    gcyl("lens", 0.026, 0.005, (0.272, 0.13, 0), "glass", axis="x", verts=20)
    for s in (-1, 1):
        gbox(f"ring{s}", (0.02, 0.05, 0.03), (0.15 + s * 0.06, 0.1, 0), "grip", 0.005)
    gbox("stock", (0.16, 0.08, 0.044), (-0.15, 0.03, 0), "purple", 0.02, rot=(0, 0.08, 0))
    gbox("cheek", (0.1, 0.02, 0.046), (-0.12, 0.08, 0), "grip", 0.008)
    for s in (-1, 1):
        tube(f"bipod{s}", [G(0.5, 0.045, 0), G(0.38, 0.03, s * 0.025)], 0.006, "grip")


def rocket():   # the foam rocket launcher: a fat green tube, a yellow flared mouth, a red rocket's nose peeping out, a sight
    handle("grip", at=(0, 0.0, 0))
    gcyl("tube", 0.07, 0.68, (0.16, 0.12, 0), "green", axis="x", verts=28)
    glathe_x("mouth", [(0.07, 0.0), (0.09, 0.04), (0.092, 0.06)], (0.5, 0.12, 0), "yellow")
    glathe_x("rear", [(0.072, 0.0), (0.085, -0.03), (0.06, -0.05)], (-0.18, 0.12, 0), "yellow")
    sphere("nose", G(0.5, 0.12, 0), 0.05, "red", scale=(1.2, 1, 1), segs=18)
    gbox("sight", (0.06, 0.05, 0.016), (0.15, 0.21, 0.0), "red", 0.006)
    gbox("foregrip", (0.04, 0.09, 0.04), (0.25, 0.03, 0), "grip", 0.012)
    for s in (-1, 1):
        torus(f"band{s}", G(0.16 + s * 0.2, 0.12, 0), 0.072, 0.008, "yellow", rot=(0, math.pi / 2, 0))


def glathe_x(name, prof, at, m, segs=24):
    """A lathe about the x axis (a ring's radius r at x offset dx)."""
    import bmesh
    bm = bmesh.new(); rings = []
    for (r, dx) in prof:
        rings.append([bm.verts.new(G(at[0] + dx, at[1] + r * math.cos(2 * math.pi * k / segs), at[2] + r * math.sin(2 * math.pi * k / segs))) for k in range(segs)])
    for a, b in zip(rings, rings[1:]):
        for k in range(segs):
            bm.faces.new((a[k], a[(k + 1) % segs], b[(k + 1) % segs], b[k]))
    me = C.bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    return F.put(C.link(C.bpy.data.objects.new(name, me)), m, 60)


def knife():   # the foam knife: a blue handle with a guard, a soft orange blade
    gcyl("handle", 0.022, 0.11, (-0.0, 0, 0), "blue", axis="x", verts=16)
    gbox("guard", (0.02, 0.07, 0.03), (0.06, 0.0, 0), "yellow", 0.008)
    gbox("blade", (0.22, 0.05, 0.018), (0.18, 0.01, 0), "foam", 0.015)
    gbox("edge", (0.05, 0.035, 0.016), (0.3, 0.0, 0), "foam", 0.015, rot=(0, 0, -0.4))


def vacuum():   # the handheld dart vacuum: a green body, a clear dust cup, a long nozzle, a handle on top
    gbox("body", (0.22, 0.1, 0.09), (0.08, 0.0, 0), "green", 0.03)
    gcyl("cup", 0.045, 0.1, (0.2, 0.0, 0), "glass", axis="x", verts=20)
    gcyl("nozzle", 0.03, 0.18, (0.33, 0.01, 0), "grey", axis="x", verts=16)
    gbox("mouth", (0.03, 0.03, 0.09), (0.42, 0.01, 0), "grey", 0.008)
    tube("handle", [G(-0.03, 0.05, 0), G(0.0, 0.12, 0), G(0.12, 0.12, 0), G(0.16, 0.05, 0)], 0.016, "grip")
    for k in range(4):
        gbox(f"vent{k}", (0.03, 0.01, 0.092), (-0.0 + k * 0.035, -0.035, 0), "grip", 0.003)


def finger():   # the giant foam finger: a mitt with "#1"-style ridges, held upright
    gbox("mitt", (0.1, 0.32, 0.18), (0, 0.18, 0), "blue", 0.05)
    gbox("finger", (0.08, 0.32, 0.07), (0, 0.5, 0.03), "blue", 0.035)
    gbox("thumb", (0.07, 0.12, 0.06), (0, 0.24, -0.11), "blue", 0.03)
    gbox("cuff", (0.11, 0.06, 0.19), (0, 0.02, 0), "yellow", 0.02)


# ---------------------------------------------------------------- the ball cannon (base: yaw only; barrel: yaw and pitch, about the pivot)
def cannon_base():
    glathe("post", [(0.16, -1.0), (0.16, -0.38)], (0, 0, 0), "grey", segs=24)
    glathe("drum", [(0.42, -0.38), (0.44, -0.3), (0.44, -0.18), (0.4, -0.15)], (0, 0, 0), "yellow", segs=32)
    torus("ring", G(0, -0.26, 0), 0.44, 0.02, "orange", rot=(math.pi / 2, 0, 0))
    for s in (-1, 1):
        gbox(f"yoke{s}", (0.36, 0.42, 0.08), (0, 0.04, s * 0.36), "yellow", 0.03)
        gcyl(f"axle{s}", 0.07, 0.05, (0, 0.0, s * 0.42), "orange", axis="z", verts=20)
    glathe("seatpost", [(0.04, -1.0), (0.04, -0.55)], (-1.0, 0, 0), "grey", segs=12)
    gbox("seat", (0.32, 0.08, 0.36), (-1.0, -0.52, 0), "red", 0.03)


def cannon_barrel():
    glathe_x("barrel", [(0.22, -0.55), (0.24, -0.45), (0.2, -0.3), (0.19, 1.0), (0.22, 1.15), (0.25, 1.32), (0.2, 1.36)], (0, 0, 0), "red", segs=32)
    for x in (0.2, 0.6, 0.95):
        torus(f"hoop{x}", G(x, 0, 0), 0.2, 0.02, "yellow", rot=(0, math.pi / 2, 0))
    gcyl("bore", 0.13, 0.05, (1.35, 0, 0), "grip", axis="x", verts=24)
    for s in (-1, 1):
        tube(f"handle{s}", [G(-0.45, 0, s * 0.15), G(-0.75, -0.05, s * 0.22), G(-0.95, -0.05, s * 0.22)], 0.025, "grip")
        sphere(f"knob{s}", G(-0.97, -0.05, s * 0.22), 0.045, "orange", segs=14)
    # the hopper beside the breech (clear, the game fills it with balls) and its feed chute
    glathe("hopper", [(0.1, 0.0), (0.17, 0.2), (0.2, 0.36), (0.19, 0.38)], (0.1, 0, 0.55), "glass", segs=24)
    torus("lip", G(0.1, 0.37, 0.55), 0.195, 0.016, "yellow", rot=(math.pi / 2, 0, 0))
    tube("chute", [G(0.1, 0.0, 0.47), G(0.1, 0.0, 0.2)], 0.05, "glass")
    gbox("gauge", (0.1, 0.08, 0.02), (-0.35, 0.22, 0), "white", 0.01)


# ---------------------------------------------------------------- the reward toys
def tank():   # the ride-on dart tank: a padded body in two tones, tracks with road wheels, a domed turret with twin blasters
    gbox("hull", (1.7, 0.45, 1.05), (0, 0.42, 0), "tealpad", 0.12)
    gbox("deck", (1.4, 0.12, 0.9), (0.0, 0.7, 0), "yellow", 0.06)
    gbox("bumper", (0.15, 0.3, 1.0), (0.88, 0.4, 0), "orange", 0.07)
    for s in (-1, 1):
        gbox(f"track{s}", (1.85, 0.38, 0.26), (0, 0.22, s * 0.62), "rubber", 0.12)
        for k in range(5):
            gcyl(f"wheel{s}{k}", 0.13, 0.28, (-0.72 + k * 0.36, 0.2, s * 0.62), "grey", axis="z", verts=18)
            gcyl(f"hub{s}{k}", 0.05, 0.3, (-0.72 + k * 0.36, 0.2, s * 0.62), "yellow", axis="z", verts=12)
    sphere("turret", G(-0.05, 0.85, 0), 0.42, "tealpad", scale=(1.1, 0.6, 1.0), segs=28)
    gbox("hatch", (0.3, 0.06, 0.3), (-0.12, 1.1, 0), "yellow", 0.03)
    for s in (-1, 1):
        gcyl(f"gun{s}", 0.05, 0.6, (0.42, 0.9, s * 0.24), "blue", axis="x", verts=18)
        gcyl(f"cage{s}", 0.065, 0.16, (0.7, 0.9, s * 0.24), "yellow", axis="x", verts=18)
    gbox("flagpole", (0.02, 0.6, 0.02), (-0.5, 1.2, 0.35), "grey", 0.005)
    gbox("pennant", (0.22, 0.14, 0.01), (-0.38, 1.42, 0.35), "orange", 0.004)


def rccar():   # the RC car: a red buggy shell, chunky tyres, a roll bar, an aerial and a little dart turret
    gbox("chassis", (0.46, 0.08, 0.28), (0, 0.1, 0), "grip", 0.02)
    gbox("shell", (0.38, 0.1, 0.26), (0.02, 0.18, 0), "red", 0.05)
    gbox("nose", (0.12, 0.06, 0.24), (0.22, 0.13, 0), "yellow", 0.03)
    for sx in (-1, 1):
        for sz in (-1, 1):
            gcyl(f"tyre{sx}{sz}", 0.075, 0.07, (sx * 0.17, 0.075, sz * 0.16), "rubber", axis="z", verts=18)
            gcyl(f"rim{sx}{sz}", 0.04, 0.072, (sx * 0.17, 0.075, sz * 0.16), "yellow", axis="z", verts=12)
    tube("rollbar", [G(-0.1, 0.22, -0.11), G(-0.08, 0.32, -0.08), G(-0.08, 0.32, 0.08), G(-0.1, 0.22, 0.11)], 0.012, "chrome")
    tube("aerial", [G(-0.18, 0.2, 0.1), G(-0.2, 0.55, 0.12)], 0.004, "grip")
    sphere("aerialtip", G(-0.2, 0.55, 0.12), 0.012, "red", segs=10)
    gcyl("turretbase", 0.06, 0.04, (0.0, 0.25, 0), "grey", verts=18)
    gcyl("turretgun", 0.018, 0.16, (0.08, 0.28, 0), "orange", axis="x", verts=12)


def drone():   # the dart drone: a white body, four arms with prop guards, an eye, a little blaster underneath
    gbox("body", (0.22, 0.07, 0.22), (0, 0, 0), "white", 0.04)
    gbox("canopy", (0.12, 0.04, 0.12), (0, 0.05, 0), "glass", 0.03)
    for k in range(4):
        a = k * math.pi / 2 + math.pi / 4
        tube(f"arm{k}", [G(0, 0, 0), G(math.cos(a) * 0.24, 0.02, math.sin(a) * 0.24)], 0.014, "grey")
        torus(f"guard{k}", G(math.cos(a) * 0.24, 0.03, math.sin(a) * 0.24), 0.09, 0.008, "orange", rot=(math.pi / 2, 0, 0))
        gcyl(f"motor{k}", 0.02, 0.04, (math.cos(a) * 0.24, 0.03, math.sin(a) * 0.24), "grip", verts=12)
    sphere("eye", G(0.11, 0.0, 0), 0.025, "red", segs=12)
    gcyl("gun", 0.014, 0.12, (0.06, -0.06, 0), "blue", axis="x", verts=12)


def healthbox():   # a first-aid box: white with red crosses, a handle, latches
    gbox("box", (0.5, 0.3, 0.34), (0, 0.15, 0), "white", 0.04)
    for s in (-1, 1):
        gbox(f"crossh{s}", (0.2, 0.06, 0.006), (0, 0.15, s * 0.172), "red", 0.003)
        gbox(f"crossv{s}", (0.06, 0.2, 0.006), (0, 0.15, s * 0.172), "red", 0.003)
    gbox("crosstop1", (0.2, 0.006, 0.06), (0, 0.302, 0), "red", 0.003)
    gbox("crosstop2", (0.06, 0.006, 0.2), (0, 0.302, 0), "red", 0.003)
    tube("handle", [G(-0.1, 0.3, 0), G(-0.08, 0.36, 0), G(0.08, 0.36, 0), G(0.1, 0.3, 0)], 0.012, "grey")
    for s in (-1, 1):
        gbox(f"latch{s}", (0.04, 0.05, 0.02), (s * 0.15, 0.26, 0.172), "grey", 0.005)


def bomb():   # the foam bomb: a big black foam ball with a yellow band, a fuse cap, a countdown face
    sphere("ball", G(0, 0.32, 0), 0.32, "grip", segs=32)
    torus("band", G(0, 0.32, 0), 0.32, 0.03, "yellow", rot=(math.pi / 2, 0, 0))
    gcyl("cap", 0.08, 0.08, (0, 0.66, 0), "grey", verts=20)
    tube("fuse", [G(0, 0.7, 0), G(0.05, 0.78, 0.02), G(0.02, 0.85, 0.05)], 0.012, "white")
    gbox("face", (0.16, 0.08, 0.02), (0.0, 0.35, 0.31), "red", 0.01)


def flag():   # the team flag: a pole with a ball top, a cloth (the game tints it), a weighted base
    glathe("base", [(0.0001, 0), (0.28, 0), (0.28, 0.06), (0.2, 0.1), (0.0001, 0.1)], (0, 0, 0), "grey", segs=24)
    gcyl("pole", 0.025, 2.2, (0, 1.15, 0), "chrome", verts=12)
    sphere("top", G(0, 2.27, 0), 0.05, "yellow", segs=14)
    import bmesh
    bm = bmesh.new(); W, H, N = 0.85, 0.5, 8; grid = []
    for i in range(N + 1):
        row = []
        for j in range(3):
            u = i / N; x = 0.02 + u * W; y = 2.15 - j * H / 2; z = 0.06 * math.sin(u * math.pi * 1.5) * u
            row.append((bm.verts.new(G(x, y, z)), bm.verts.new(G(x, y, z - 0.004))))
        grid.append(row)
    for i in range(N):
        for j in range(2):
            bm.faces.new((grid[i][j][0], grid[i + 1][j][0], grid[i + 1][j + 1][0], grid[i][j + 1][0]))
            bm.faces.new((grid[i][j + 1][1], grid[i + 1][j + 1][1], grid[i + 1][j][1], grid[i][j][1]))
    me = C.bpy.data.meshes.new("cloth"); bm.to_mesh(me); bm.free()
    F.put(C.link(C.bpy.data.objects.new("cloth", me)), "white", 30)


BUILDS = {"starter": starter, "pistol": pistol, "revolver": revolver, "dual": dual, "pump": pump, "burst": burst, "flywheel": flywheel,
          "crossbow": crossbow, "belt": belt, "long": longgun, "rocket": rocket, "knife": knife, "vacuum": vacuum, "finger": finger,
          "cannon_base": cannon_base, "cannon_barrel": cannon_barrel, "tank": tank, "rccar": rccar, "drone": drone,
          "healthbox": healthbox, "bomb": bomb, "flag": flag}

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
