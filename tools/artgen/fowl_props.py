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
    for k, rgb, r in (("wood_lt", (0.5, 0.3, 0.15), 0.5), ("felt", (0.08, 0.25, 0.12), 0.95), ("pegboard", (0.62, 0.48, 0.3), 0.8), ("green", (0.12, 0.3, 0.16), 0.5), ("yellow", (0.85, 0.65, 0.15), 0.4), ("purple", (0.36, 0.1, 0.45), 0.4), ("purple_dk", (0.2, 0.05, 0.26), 0.5), ("velvet", (0.45, 0.04, 0.1), 0.9), ("bottle_g", (0.1, 0.35, 0.12), 0.1), ("bottle_b", (0.35, 0.18, 0.06), 0.1), ("bottle_c", (0.75, 0.75, 0.7), 0.1), ("fur", (0.35, 0.22, 0.12), 0.9), ("fur_dk", (0.18, 0.11, 0.06), 0.9), ("horn", (0.72, 0.62, 0.45), 0.6), ("rug", (0.2, 0.05, 0.04), 0.95), ("rug_edge", (0.42, 0.34, 0.22), 0.95), ("rug_band", (0.45, 0.3, 0.08), 0.9)):
        mat(k, rgb, r)
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


# ---------------------------------------------------------------- the rest of the clubhouse (2026-10-05 polish pass)
# Each is built about its station point in GAME coordinates (x, y up, z toward the players) through G(), so the game
# draws it with a plain translate. The coloured stock (attachment packs, trophies' glow, neon) stays in the game.
def G(x, y, z):
    return (x, -z, y)


def gbox(name, size, at, m, bevel=0.015, rot=(0, 0, 0)):
    return box(name, (size[0], size[2], size[1]), G(*at), m, bevel, rot)


def gcyl(name, r, depth, at, m, axis="y", verts=24):
    rot = (0, 0, 0) if axis == "y" else (math.pi / 2, 0, 0) if axis == "z" else (0, math.pi / 2, 0)
    return cyl(name, r, depth, G(*at), m, rot=rot, verts=verts)


def glathe(name, prof, at, m, segs=32):
    o = lathe(name, [(r, y) for (r, y) in prof], m, segs)
    o.location = G(*at); C.apply_all(o); return o


def guncase():
    # the gun counter: a panelled wooden counter under a glass case with brass corners; the guns lie inside on felt
    gbox("base", (5.6, 0.96, 1.0), (0, 0.48, 0), "wood", 0.03)
    gbox("kick", (5.5, 0.1, 0.94), (0, 0.05, 0.04), "black", 0.01)
    for k in range(6):
        gbox(f"panel{k}", (0.8, 0.6, 0.02), (-2.3 + k * 0.92, 0.5, 0.51), "wood_lt", 0.02)
    gbox("felt", (5.4, 0.03, 0.9), (0, 0.985, 0), "felt", 0.005)
    gbox("glass", (5.44, 0.32, 0.94), (0, 1.16, 0), "glass", 0.005)
    for sx in (-1, 1):
        for sz in (-1, 1):
            gbox(f"post{sx}{sz}", (0.04, 0.34, 0.04), (sx * 2.72, 1.16, sz * 0.47), "brass", 0.005)
    for sz in (-1, 1):
        gbox(f"rimtop{sz}", (5.48, 0.03, 0.04), (0, 1.33, sz * 0.47), "brass", 0.005)
    gbox("price", (0.5, 0.18, 0.02), (2.2, 1.45, 0.4), "cream", 0.01)
    gcyl("bell", 0.07, 0.04, (2.5, 1.35, 0.3), "brass")
    sphere("bellknob", G(2.5, 1.39, 0.3), 0.018, "brass", segs=10)


def pegboard():
    # behind the counter (game z -1.5 from the station): a framed pegboard with hooks in rows; the packs hang in the game
    gbox("board", (5.0, 1.6, 0.04), (0, 2.2, -1.5), "pegboard", 0.005)
    for s in (-1, 1):
        gbox(f"frame_v{s}", (0.08, 1.7, 0.07), (s * 2.52, 2.2, -1.49), "wood", 0.01)
    for s in (-1, 1):
        gbox(f"frame_h{s}", (5.12, 0.08, 0.07), (0, 2.2 + s * 0.82, -1.49), "wood", 0.01)
    for i in range(24):
        x = -2.2 + (i % 8) * 0.62; y = 1.7 + (i // 8) * 0.45 + 0.1
        tube(f"hook{i}", [G(x, y, -1.48), G(x, y, -1.36), G(x, y + 0.02, -1.34)], 0.008, "chrome")
    gbox("sign", (1.6, 0.26, 0.03), (0, 3.15, -1.48), "red", 0.01)
    gbox("sign_band", (1.5, 0.05, 0.035), (0, 3.15, -1.47), "gold", 0.005)


def scratchcounter():
    gbox("base", (2.4, 1.06, 0.9), (0, 0.53, 0), "green", 0.03)
    gbox("top", (2.5, 0.05, 1.0), (0, 1.085, 0), "wood", 0.01)
    gbox("trim", (2.42, 0.04, 0.02), (0, 0.9, 0.46), "gold", 0.004)
    # the ticket dispenser: a brass-trimmed cabinet with four rolls showing through a window and a pull slot
    gbox("disp", (0.8, 0.78, 0.5), (-0.6, 1.5, -0.2), "yellow", 0.03)
    gbox("disp_win", (0.66, 0.36, 0.02), (-0.6, 1.58, 0.06), "glass", 0.004)
    for k in range(4):
        gcyl(f"roll{k}", 0.06, 0.13, (-0.84 + k * 0.16, 1.58, -0.05), ["gum0", "gum1", "gum2", "gum3"][k], axis="x", verts=16)
    gbox("disp_slot", (0.6, 0.03, 0.04), (-0.6, 1.24, 0.06), "black", 0.004)
    gbox("disp_cap", (0.86, 0.06, 0.56), (-0.6, 1.92, -0.2), "gold", 0.01)
    # a coin dish and a spike of used tickets
    glathe("dish", [(0.0001, 0), (0.12, 0), (0.15, 0.03), (0.13, 0.035), (0.0001, 0.01)], (0.5, 1.11, 0.15), "brass")
    gcyl("spike_base", 0.05, 0.02, (0.9, 1.12, -0.25), "iron")
    gcyl("spike", 0.004, 0.22, (0.9, 1.23, -0.25), "chrome", verts=8)
    for k in range(5):
        gbox(f"stub{k}", (0.12, 0.004, 0.07), (0.9, 1.15 + k * 0.025, -0.25), "cream", 0.001, rot=(0, 0, k * 0.6))


def slopshop():
    # a garish booth (offsets from the station as the old boxes): counter, back wall, a striped awning, a curtain
    gbox("counter", (1.0, 1.2, 3.0), (0.9, 0.6, 0), "purple", 0.04)
    gbox("countertop", (1.12, 0.06, 3.12), (0.9, 1.23, 0), "gold", 0.01)
    for k in range(5):
        gbox(f"star{k}", (0.02, 0.18, 0.18), (0.39, 0.6, -1.2 + k * 0.6), "gold", 0.01, rot=(0, 0, 0))
    gbox("back", (0.3, 3.6, 3.4), (1.8, 1.8, 0), "purple_dk", 0.03)
    for k in range(8):   # the awning: alternating stripes, tilted out over the counter
        z = -1.5 + k * 0.43
        gbox(f"awn{k}", (1.2, 0.04, 0.42), (1.1, 2.55, z + 0.21), "red" if k % 2 == 0 else "cream", 0.004, rot=(0, 0.35, 0))
    for k in range(9):   # a scalloped hem
        sphere(f"hem{k}", G(0.55, 2.33, -1.5 + k * 0.375), 0.09, "red" if k % 2 == 0 else "cream", scale=(1, 0.5, 0.6), segs=12)
    for k in range(6):   # the curtain behind
        gbox(f"curt{k}", (0.06, 2.0, 0.5), (1.62, 1.3, -1.25 + k * 0.5), "velvet", 0.03, rot=(0, 0, 0.12 if k % 2 else -0.12))
    for s in (-1, 1):
        gcyl(f"pole{s}", 0.04, 2.6, (0.45, 1.3, s * 1.55), "gold", verts=12)


def trophywall():
    # on the west wall (the station's x is ignored: built at x = -15): a board, two shelves, cups, a mounted decoy
    gbox("board", (0.08, 2.4, 3.6), (-14.95, 1.8, 0), "wood", 0.02)
    for s in (-1, 1):
        gbox(f"shelf{s}", (0.3, 0.05, 3.3), (-14.8, 1.4 + (s + 1) * 0.4 - 0.02, 0), "wood_lt", 0.01)
    for k in range(6):
        z = -1.4 + k * 0.56; y = 1.4 + (k % 2) * 0.8
        glathe(f"cup{k}", [(0.0001, 0), (0.08, 0), (0.08, 0.03), (0.03, 0.05), (0.025, 0.14), (0.04, 0.16), (0.09, 0.24), (0.11, 0.34), (0.1, 0.35), (0.0001, 0.2)], (-14.8, y, z), "gold")
        for s in (-1, 1):
            torus("handle%d%d" % (k, s), G(-14.8, y + 0.26, z + s * 0.11), 0.04, 0.008, "gold", rot=(0, math.pi / 2, 0))
    # a mounted decoy head on a plaque at the top
    gbox("plaque", (0.05, 0.35, 0.28), (-14.9, 3.15, 0), "wood_lt", 0.02)
    sphere("duckhead", G(-14.75, 3.18, 0), 0.11, "green", scale=(1, 0.9, 1), segs=16)
    sphere("bill", G(-14.62, 3.15, 0), 0.06, "yellow", scale=(1.5, 0.5, 0.4), segs=12)


def clubbar():
    # the bar: a panelled counter with a brass top edge and foot rail, a back shelf of bottles, three stools
    gbox("counter", (4.0, 1.08, 0.8), (0, 0.54, 0), "wood", 0.03)
    gbox("top", (4.16, 0.06, 0.92), (0, 1.12, 0), "wood_lt", 0.015)
    gbox("toprim", (4.18, 0.03, 0.03), (0, 1.12, 0.47), "brass", 0.005)
    tube("footrail", [G(-1.95, 0.2, 0.55), G(1.95, 0.2, 0.55)], 0.025, "brass")
    for k in range(5):
        gbox(f"panel{k}", (0.64, 0.66, 0.02), (-1.6 + k * 0.8, 0.56, 0.41), "wood_lt", 0.02)
    gbox("backshelf", (3.8, 0.04, 0.3), (0, 1.55, -0.9), "wood_lt", 0.01)
    gbox("backshelf2", (3.8, 0.04, 0.3), (0, 2.05, -0.9), "wood_lt", 0.01)
    gbox("backboard", (3.9, 1.4, 0.04), (0, 1.9, -1.06), "wood", 0.01)
    rng = random.Random(3)
    for row, y in enumerate((1.57, 2.07)):
        for k in range(9):
            x = -1.7 + k * 0.42 + rng.uniform(-0.05, 0.05); h = rng.uniform(0.24, 0.34); m = ["bottle_g", "bottle_b", "bottle_c"][(k + row) % 3]
            glathe(f"bottle{row}{k}", [(0.0001, 0), (0.05, 0), (0.05, h * 0.62), (0.02, h * 0.82), (0.015, h), (0.0001, h)], (x, y, -0.88), m, segs=14)
    gbox("taps_base", (0.5, 0.08, 0.1), (0.9, 1.19, -0.3), "brass", 0.01)
    for k in range(3):
        gcyl(f"tap{k}", 0.015, 0.2, (0.75 + k * 0.15, 1.33, -0.3), "brass", verts=10)
        gcyl(f"taphandle{k}", 0.022, 0.12, (0.75 + k * 0.15, 1.49, -0.3), ["black", "red", "cream"][k], verts=10)
    for k in range(3):
        x = -1.2 + k * 1.2
        gcyl(f"seat{k}", 0.2, 0.08, (x, 0.78, 0.85), "red", verts=24)
        gcyl(f"stem{k}", 0.03, 0.7, (x, 0.4, 0.85), "chrome", verts=10)
        gcyl(f"foot{k}", 0.18, 0.03, (x, 0.02, 0.85), "chrome", verts=24)
        torus(f"ring{k}", G(x, 0.3, 0.85), 0.15, 0.012, "chrome")


def porchpost():
    # a turned porch post (3.8 m) and the waist-high stall partition (beadboard with a capped rail)
    glathe("post", [(0.09, 0), (0.09, 0.15), (0.07, 0.2), (0.065, 3.5), (0.08, 3.55), (0.08, 3.62), (0.06, 3.7), (0.06, 3.8)], (0, 0, 0), "wood", segs=12)


def partition():
    gbox("panel", (0.05, 1.0, 3.0), (0, 0.5, 0), "wood_lt", 0.01)
    for k in range(10):
        gbox(f"bead{k}", (0.06, 0.96, 0.015), (0, 0.5, -1.4 + k * 0.31), "wood", 0.003)
    gbox("cap", (0.12, 0.05, 3.06), (0, 1.03, 0), "wood", 0.01)


def clubdecor():
    # the room's dressing in absolute room coordinates (x -15..15, z -17.2..-1.8, ceiling 3.6): ceiling beams, a chair
    # rail, framed duck prints and two windows on the back wall (the game lights the panes), a moose head and a shelf of
    # decoys on the east wall, pendant lamp shades (the game hangs the glow under them), a rug, a wood stove, a coat rack
    for k in range(10):
        gbox(f"beam{k}", (30.4, 0.22, 0.2), (0, 3.47, -16.4 + k * 1.6), "wood", 0.02)
    gbox("rail_back", (30.2, 0.06, 0.06), (0, 1.02, -17.02), "wood_lt", 0.01)
    for s in (-1, 1):
        gbox(f"rail_side{s}", (0.06, 0.06, 15.2), (s * 15.02, 1.02, -9.5), "wood_lt", 0.01)
    rng = random.Random(11)
    for k, x in enumerate((-12.6, -10.9, -9.2)):   # framed prints: a gilt frame, a cream mat, a painted marsh with a duck
        gbox(f"frame{k}", (1.2, 0.9, 0.05), (x, 2.3, -17.02), "gold", 0.01)
        gbox(f"mat{k}", (1.06, 0.76, 0.02), (x, 2.3, -16.99), "cream", 0.004)
        gbox(f"sky{k}", (0.86, 0.34, 0.02), (x, 2.44, -16.97), ["yellow", "teal", "red_dark"][k], 0.003)
        gbox(f"marsh{k}", (0.86, 0.22, 0.02), (x, 2.16, -16.97), "green", 0.003)
        sphere(f"pduck{k}", G(x + rng.uniform(-0.2, 0.2), 2.45, -16.955), 0.06, "wood", scale=(1.6, 1, 0.6), segs=10)
    for k, x in enumerate((-1.5, 5.5)):   # two windows over the slot row: frame, sill, muntins (the pane is lit in game)
        for s in (-1, 1):
            gbox(f"win{k}v{s}", (0.08, 1.0, 0.12), (x + s * 0.9, 2.6, -17.0), "wood_lt", 0.01)
            gbox(f"win{k}h{s}", (1.88, 0.08, 0.12), (x, 2.6 + s * 0.5, -17.0), "wood_lt", 0.01)
        gbox(f"win{k}mv", (0.04, 1.0, 0.08), (x, 2.6, -16.99), "wood_lt", 0.005)
        gbox(f"win{k}mh", (1.8, 0.04, 0.08), (x, 2.6, -16.99), "wood_lt", 0.005)
        gbox(f"win{k}sill", (2.0, 0.06, 0.24), (x, 2.07, -16.92), "wood", 0.01)
    # the moose head on the east wall: a plaque, a long head, ears, and broad palmate antlers
    gbox("mplaque", (0.06, 0.7, 0.55), (14.97, 2.55, -11.0), "wood", 0.03)
    sphere("mneck", G(14.75, 2.5, -11.0), 0.26, "fur", scale=(1, 1.0, 1.1), segs=16)
    sphere("mhead", G(14.45, 2.42, -11.0), 0.2, "fur", scale=(1.7, 0.9, 0.9), segs=16)
    sphere("mnose", G(14.12, 2.36, -11.0), 0.13, "fur_dk", scale=(1, 0.9, 1), segs=12)
    for s in (-1, 1):
        sphere(f"meye{s}", G(14.42, 2.5, -11.0 + s * 0.15), 0.03, "black", segs=8)
        sphere(f"mear{s}", G(14.7, 2.7, -11.0 + s * 0.2), 0.08, "fur", scale=(0.5, 1.3, 0.6), segs=10)
        tube(f"mant{s}", [G(14.7, 2.68, -11.0 + s * 0.12), G(14.7, 2.85, -11.0 + s * 0.35), G(14.68, 2.95, -11.0 + s * 0.55)], 0.03, "horn")
        put(C.bevelled_box(f"mpalm{s}", (0.06, 0.4, 0.26), loc=G(14.68, 3.0, -11.0 + s * 0.6), bevel=0.03, segments=3, rot=(s * 0.4, 0, 0)), "horn")
        for t in range(4):
            tube(f"mtine{s}{t}", [G(14.68, 3.1, -11.0 + s * (0.45 + t * 0.07)), G(14.66, 3.26, -11.0 + s * (0.47 + t * 0.09))], 0.015, "horn")
    # a shelf of decoys on the east wall
    gbox("dshelf", (0.3, 0.05, 4.0), (14.85, 1.75, -5.0), "wood_lt", 0.01)
    for k in range(6):
        z = -6.6 + k * 0.64; col = ["green", "wood", "cream"][k % 3]
        sphere(f"dbody{k}", G(14.8, 1.88, z), 0.12, col, scale=(0.7, 0.75, 1.3), segs=14)
        sphere(f"dhead{k}", G(14.8, 2.03, z + 0.12), 0.065, "green" if k % 2 == 0 else "wood", segs=12)
        sphere(f"dbill{k}", G(14.8, 2.02, z + 0.2), 0.03, "yellow", scale=(0.6, 0.35, 1.3), segs=8)
    # pendant lamp shades over the room (the game's glow hangs under each), on cords to the beams
    for i in range(4):
        x = -10.5 + i * 7
        glathe(f"shade{i}", [(0.06, 0.25), (0.08, 0.22), (0.3, 0.02), (0.32, 0.0), (0.3, 0.0), (0.06, 0.2)], (x, 3.0, -9), "teal", segs=24)
        gcyl(f"cord{i}", 0.008, 0.36, (x, 3.43, -9), "black", verts=6)
    # a rug in the middle of the room: a dark red field, a cream border, a gold inner band
    gbox("rug", (7.0, 0.012, 4.4), (0.5, 0.006, -9.6), "rug", 0.004)
    gbox("rug_border", (7.2, 0.01, 4.6), (0.5, 0.004, -9.6), "rug_edge", 0.004)
    gbox("rug_band", (6.2, 0.014, 3.6), (0.5, 0.008, -9.6), "rug_band", 0.003)
    gbox("rug_in", (6.0, 0.016, 3.4), (0.5, 0.01, -9.6), "rug", 0.003)
    # the wood stove in the back-left corner: a pot-bellied iron stove on legs, a door with a grate, a flue to the ceiling
    glathe("stove", [(0.0001, 0.2), (0.3, 0.2), (0.36, 0.45), (0.34, 0.8), (0.24, 0.95), (0.12, 1.0), (0.0001, 1.0)], (-8.6, 0, -16.4), "iron", segs=24)
    for k in range(3):
        a = k * 2 * math.pi / 3
        gcyl(f"sleg{k}", 0.03, 0.22, (-8.6 + 0.24 * math.cos(a), 0.11, -16.4 + 0.24 * math.sin(a)), "iron", verts=8)
    gbox("sdoor", (0.3, 0.26, 0.04), (-8.6, 0.55, -16.05), "iron", 0.01)
    for k in range(4):
        gbox(f"sgrate{k}", (0.22, 0.02, 0.05), (-8.6, 0.47 + k * 0.05, -16.03), "black", 0.002)
    gcyl("flue", 0.08, 2.6, (-8.6, 2.3, -16.4), "iron", verts=16)
    glathe("kettle", [(0.0001, 0), (0.1, 0), (0.12, 0.08), (0.08, 0.16), (0.0001, 0.17)], (-8.55, 1.0, -16.35), "brass", segs=16)
    # a coat rack by the porch gates with a hat and a coat
    gcyl("rack", 0.03, 1.9, (13.9, 0.95, -2.6), "wood", verts=10)
    gcyl("rackfoot", 0.25, 0.04, (13.9, 0.02, -2.6), "wood", verts=16)
    for k in range(4):
        a = k * math.pi / 2
        tube(f"peg{k}", [G(13.9, 1.75, -2.6), G(13.9 + 0.18 * math.cos(a), 1.85, -2.6 + 0.18 * math.sin(a))], 0.012, "wood")
    glathe("hat", [(0.0001, 0), (0.2, 0), (0.2, 0.015), (0.11, 0.02), (0.1, 0.12), (0.0001, 0.13)], (14.08, 1.85, -2.6), "green", segs=20)
    gbox("coat", (0.1, 0.8, 0.4), (13.9, 1.35, -2.42), "red_dark", 0.05)


BUILDS = {"clubdecor": clubdecor, "gumball": gumball, "gumball_crank": gumball_crank, "slot": slot, "slot_handle": slot_handle,
          "guncase": guncase, "pegboard": pegboard, "scratchcounter": scratchcounter, "slopshop": slopshop,
          "trophywall": trophywall, "clubbar": clubbar, "porchpost": porchpost, "partition": partition}


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
