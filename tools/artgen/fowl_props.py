# Fowl Play's clubhouse machines (the user's playtest note, 2026-10-05: "the gumball machine and slot machines don't
# look like well crafted gumball machines or slot machines"):
#     gumball         the mystery-gun machine: a classic carnival gumball machine, oversized. A fluted cast-iron
#                     pedestal on a flared foot, a red body with a chrome coin plate and a chute with a flap, a glass
#                     globe full of gumballs, a red cap with a knob. (The crank turns in the game: gumball_crank.)
#     gumball_crank   the crank: a chrome spindle, a disc and a handle, turning about game +Z (out of the plate).
#     slot            a vintage one-armed bandit: a cast cabinet with a rounded crown and a marquee, chrome trim and
#                     pinstripes, three reel windows in a chrome bezel (the game draws the reels behind the glass), a
#                     coin slot and a payline plate, a payout tray and a jackpot window, on a short stand.
#     slot_handle     the arm: a chrome lever with a red ball, pivoting about game +X at its hub.
# Built facing the room (Blender -Y, which is the game's +Z), feet at the origin. Flat named materials; names with
# "glass" are drawn blended by the PBR path.
#     blender -b --factory-startup -P tools/artgen/fowl_props.py -- --out assets/fowl
import os
import sys
import math
import random
import bmesh
from mathutils import Vector
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import common as C
bpy = C.bpy

MATS = {}
def mat(name, rgb, rough=0.5, metal=0.0):
    if name not in MATS:
        MATS[name] = C.mat_flat(name, rgb, rough=rough, metal=metal)
    return MATS[name]


def setup():
    MATS.clear()
    mat("red", (0.62, 0.04, 0.03), 0.35)
    mat("red_dark", (0.36, 0.02, 0.02), 0.45)
    mat("chrome", (0.86, 0.86, 0.88), 0.12, 1.0)
    mat("brass", (0.8, 0.58, 0.22), 0.25, 1.0)
    mat("iron", (0.08, 0.08, 0.09), 0.5, 0.6)
    mat("glass", (0.85, 0.92, 0.95), 0.02)
    mat("cream", (0.86, 0.8, 0.62), 0.5)
    mat("black", (0.02, 0.02, 0.02), 0.4)
    mat("wood", (0.32, 0.17, 0.08), 0.55)
    mat("teal", (0.04, 0.32, 0.34), 0.4)
    mat("gold", (0.9, 0.66, 0.16), 0.2, 1.0)
    for k, rgb in enumerate([(0.9, 0.7, 0.05), (0.08, 0.4, 0.85), (0.9, 0.25, 0.5), (0.12, 0.65, 0.2), (0.95, 0.4, 0.05), (0.85, 0.85, 0.85), (0.5, 0.15, 0.7)]):
        mat(f"gum{k}", rgb, 0.25)


PARTS = []
def put(o, m, smooth=40):
    C.assign(o, MATS[m]); C.apply_all(o); C.smooth(o, smooth); PARTS.append(o); return o


def lathe(name, prof, m, segs=48, flutes=0, flute_depth=0.0):
    """A surface of revolution about Blender Z: prof [(radius, z), ...] bottom to top; flutes ripple the radius."""
    bm = bmesh.new(); rings = []
    for (r, z) in prof:
        ring = []
        for k in range(segs):
            a = 2 * math.pi * k / segs
            rr = r * (1 - flute_depth * (0.5 + 0.5 * math.cos(a * flutes))) if flutes else r
            ring.append(bm.verts.new((rr * math.cos(a), rr * math.sin(a), z)))
        rings.append(ring)
    for a, b in zip(rings, rings[1:]):
        for k in range(segs):
            bm.faces.new((a[k], a[(k + 1) % segs], b[(k + 1) % segs], b[k]))
    bm.faces.new(list(reversed(rings[0]))); bm.faces.new(rings[-1])
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = C.link(bpy.data.objects.new(name, me))
    return put(o, m, 60)


def sphere(name, loc, r, m, scale=(1, 1, 1), segs=32):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segs, ring_count=segs // 2, radius=r, location=loc)
    o = bpy.context.active_object; o.name = name; o.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return put(o, m, 180)


def box(name, size, loc, m, bevel=0.01, rot=(0, 0, 0)):
    return put(C.bevelled_box(name, size, loc=loc, bevel=bevel, segments=3, rot=rot), m)


def cyl(name, r, depth, loc, m, rot=(0, 0, 0), verts=32, bevel=0.004):
    return put(C.cylinder(name, r, depth, loc=loc, rot=rot, verts=verts, bevel=bevel), m)


def torus(name, loc, R, r, m, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_torus_add(major_radius=R, minor_radius=r, location=loc, rotation=rot, major_segments=40, minor_segments=10)
    o = bpy.context.active_object; o.name = name
    return put(o, m, 180)


def tube(name, pts, r, m):
    return put(C.tube_along(name, pts, r, verts=12), m, 180)


# ---------------------------------------------------------------- the gumball machine (2.7 m: an oversized carnival one)
def gumball():
    # the pedestal: a flared foot, a fluted column, a collar
    lathe("foot", [(0.62, 0.0), (0.6, 0.05), (0.5, 0.1), (0.34, 0.16), (0.22, 0.24)], "iron")
    lathe("column", [(0.17, 0.22), (0.15, 0.4), (0.14, 0.75), (0.16, 0.9), (0.2, 0.96)], "iron", flutes=12, flute_depth=0.12)
    lathe("collar", [(0.24, 0.94), (0.3, 0.98), (0.3, 1.02), (0.26, 1.05)], "chrome")
    # the body: a rounded red block, chrome bands
    lathe("body", [(0.36, 1.04), (0.42, 1.1), (0.44, 1.3), (0.44, 1.5), (0.4, 1.58), (0.34, 1.62)], "red", segs=8)
    torus("band_lo", (0, 0, 1.1), 0.43, 0.018, "chrome")
    torus("band_hi", (0, 0, 1.56), 0.41, 0.018, "chrome")
    # the coin plate (front, Blender -Y): a chrome escutcheon, a coin slot, a price plate
    box("plate", (0.36, 0.03, 0.3), (0, -0.44, 1.38), "chrome", 0.02)
    box("slot", (0.14, 0.02, 0.025), (0, -0.46, 1.47), "black", 0.004)
    box("price", (0.2, 0.02, 0.06), (0, -0.46, 1.28), "cream", 0.006)
    cyl("hub", 0.06, 0.04, (0, -0.47, 1.38), "chrome", rot=(math.pi / 2, 0, 0))
    # the chute: a scoop under the plate with a hinged flap
    box("chute", (0.24, 0.16, 0.12), (0, -0.48, 1.13), "red_dark", 0.03)
    box("flap", (0.22, 0.02, 0.1), (0, -0.56, 1.15), "chrome", 0.01, rot=(0.25, 0, 0))
    # the globe: a glass sphere seated in a chrome ring, full of gumballs
    torus("seat", (0, 0, 1.64), 0.3, 0.035, "chrome")
    sphere("globe", (0, 0, 2.03), 0.43, "glass", segs=48)
    rng = random.Random(7); placed = []
    for i in range(170):   # (packed toward the bottom of the globe)
        for tries in range(30):
            a = rng.uniform(0, 2 * math.pi); rr = math.sqrt(rng.uniform(0, 1)) * 0.36; z = 1.7 + rng.uniform(0, 0.52) ** 1.4
            p = Vector((rr * math.cos(a), rr * math.sin(a), z))
            if (p - Vector((0, 0, 2.03))).length > 0.37 or any((p - q).length < 0.085 for q in placed):
                continue
            placed.append(p); sphere(f"gum{i}", tuple(p), 0.045, f"gum{i % 7}", segs=12); break
    # the cap: a red dome with a chrome rim and a knob
    torus("cap_rim", (0, 0, 2.39), 0.18, 0.025, "chrome")
    lathe("cap", [(0.2, 2.38), (0.2, 2.44), (0.16, 2.52), (0.09, 2.57), (0.0, 2.58)], "red")
    sphere("knob", (0, 0, 2.63), 0.055, "chrome")


def gumball_crank():
    # about its own origin; turns about Blender -Y (the game's +Z, out of the plate)
    cyl("spindle", 0.025, 0.08, (0, -0.04, 0), "chrome", rot=(math.pi / 2, 0, 0))
    cyl("disc", 0.07, 0.02, (0, -0.08, 0), "chrome", rot=(math.pi / 2, 0, 0))
    box("arm", (0.04, 0.02, 0.2), (0, -0.095, 0.09), "chrome", 0.008)
    cyl("grip", 0.03, 0.09, (0, -0.14, 0.18), "red", rot=(math.pi / 2, 0, 0))


# ---------------------------------------------------------------- the slot machine (1.95 m with its stand)
def slot():
    W, D = 0.62, 0.5
    # the stand and the cabinet
    box("stand", (W * 0.9, D * 0.85, 0.5), (0, 0, 0.25), "wood", 0.02)
    box("plinth", (W * 1.02, D * 1.02, 0.05), (0, 0, 0.52), "chrome", 0.01)
    box("cabinet", (W, D, 0.95), (0, 0, 1.02), "red", 0.04)
    for s in (-1, 1):   # chrome side rails and gold pinstripes
        box(f"rail{s}", (0.03, D * 0.98, 0.95), (s * (W / 2 + 0.005), 0, 1.02), "chrome", 0.01)
        box(f"pin{s}", (0.006, D * 0.8, 0.8), (s * (W / 2 + 0.022), 0, 1.02), "gold", 0.002)
    # the crown: a half-drum over the top, a marquee panel and a ridge of lamps (the game lights them)
    bpy.ops.mesh.primitive_cylinder_add(vertices=40, radius=W / 2, depth=D, location=(0, 0, 1.5), rotation=(math.pi / 2, 0, 0))
    crown = bpy.context.active_object; crown.name = "crown"
    bm = bmesh.new(); bm.from_mesh(crown.data)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.y < -0.001 and abs(v.co.y) < 1], context='VERTS')   # (keep the top half: local Y is up after the turn)
    bm.to_mesh(crown.data); bm.free(); put(crown, "red", 60)
    box("marquee", (W * 0.82, 0.03, 0.22), (0, -D / 2 - 0.01, 1.62), "cream", 0.01)
    box("marquee_rim", (W * 0.88, 0.02, 0.27), (0, -D / 2 + 0.005, 1.62), "chrome", 0.01)
    for k in range(7):
        a = math.pi * (k + 0.5) / 7
        sphere(f"lamp{k}", (math.cos(a) * W * 0.47, -D / 2 + 0.02, 1.5 + math.sin(a) * W * 0.47), 0.022, "gold", segs=12)
    # the reel windows: a chrome bezel round three dark openings with glass over them
    box("bezel", (W * 0.86, 0.04, 0.3), (0, -D / 2 - 0.01, 1.2), "chrome", 0.02)
    for k in (-1, 0, 1):
        box(f"window{k}", (0.15, 0.03, 0.22), (k * 0.17, -D / 2 - 0.02, 1.2), "black", 0.008)
    box("window_glass", (W * 0.78, 0.01, 0.24), (0, -D / 2 - 0.035, 1.2), "glass", 0.004)
    box("payline", (W * 0.82, 0.012, 0.012), (0, -D / 2 - 0.042, 1.2), "red_dark", 0.002)
    # the jackpot window and the coin slot plate
    box("jackpot", (W * 0.5, 0.03, 0.12), (0, -D / 2 - 0.01, 0.98), "teal", 0.01)
    box("jackpot_rim", (W * 0.56, 0.02, 0.16), (0, -D / 2, 0.98), "chrome", 0.01)
    box("coinplate", (0.12, 0.03, 0.08), (0.18, -D / 2 - 0.01, 0.84), "chrome", 0.01)
    box("coinslot", (0.05, 0.02, 0.008), (0.18, -D / 2 - 0.025, 0.85), "black", 0.002)
    # the payout tray
    box("tray", (W * 0.7, 0.16, 0.08), (0, -D / 2 - 0.07, 0.62), "chrome", 0.02)
    box("tray_well", (W * 0.62, 0.12, 0.04), (0, -D / 2 - 0.07, 0.65), "black", 0.01)
    # the handle's hub on the right side
    cyl("handle_hub", 0.07, 0.06, (W / 2 + 0.05, 0, 1.15), "chrome", rot=(0, math.pi / 2, 0))


def slot_handle():
    # about the hub; swings about Blender X (the game's +X); the arm rises up and back from the hub
    cyl("axle", 0.03, 0.06, (0.03, 0, 0), "chrome", rot=(0, math.pi / 2, 0))
    tube("arm", [(0.06, 0, 0), (0.08, 0.0, 0.12), (0.09, 0.03, 0.32), (0.09, 0.05, 0.45)], 0.018, "chrome")
    sphere("ball", (0.09, 0.05, 0.5), 0.06, "red")


BUILDS = {"gumball": gumball, "gumball_crank": gumball_crank, "slot": slot, "slot_handle": slot_handle}


def build(name, out):
    C.reset(); setup(); PARTS.clear()
    BUILDS[name]()
    C.export_glb(PARTS, os.path.join(out, f"{name}.glb"))


if not os.environ.get("ARTGEN_IMPORT"):   # (noclip_props.py imports the helpers)
    a = C.args()
    only = a[a.index("--only") + 1].split(",") if "--only" in a else None
    out = C.out_dir()
    for n in BUILDS:
        if only and n not in only:
            continue
        build(n, out)
