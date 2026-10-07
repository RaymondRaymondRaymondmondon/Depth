# The Nautilus for The Deep (Unity): Verne's 70 m iron spindle with a bow spur, a pilot house and a lantern on its
# back, the great salon windows, a four-bladed screw, rudder and planes; inside, seven rooms bow to stern (the
# Bridge, the Fabrication Bay in the old salon, Hydroponics and Larder, the Airlock and Dive Room, the Moonpool, the
# Engine and Boiler Room, the Pump and Bilge Room), derelict until the crew restores her.
#
# The game's frame is x toward the bow, y up, z to starboard (boat.py's G() converts to Blender's). Big surfaces carry
# tiling texture sets (deep_tiles.py / tiles.py) with UVs in metres and ambient occlusion in the vertex colours.
# The layout the model is built from is also written as JSON for the game (rooms, doors, stations, hatches, lamps).
#     blender -b --factory-startup -P tools/artgen/deep_nautilus.py -- --out TheDeep/Assets/Deep/Resources/Models
import bpy
import bmesh
import json
import math
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mathutils import Vector
import common as C
import tiles
import deep_tiles
import boat as B

G = B.G
R = 4.0            # the hull's radius amidships
HALF = 35.0        # half the length before the crew section was let in (the spur reaches past the bow)
MID = 14.0         # the parallel middle body ran |x| < MID
S = 8.0            # the Crew Quarters' section, let in at x = 6: everything forward of it sits S further toward the bow
CREW_X0, CREW_X1 = 6.0, 6.0 + S
FLOOR, CEIL = -1.6, 2.2
# the pilot house on the back over the Bridge: its floor, size, the eye height of its ports, the shaft and ladder
PH_X, PH_HX, PH_HZ, PH_H = 21.8 + S, 1.3, 0.95, 2.25
PH_FLOOR = 3.6
PH_EYE = PH_FLOOR + 1.62
PH_SHAFT = 20.95 + S
PH_PORTS = [(PH_X + PH_HX, 0.0, 'x', 0.34), (PH_X + PH_HX, 0.55, 'x', 0.2), (PH_X + PH_HX, -0.55, 'x', 0.2),
            (PH_X + 0.4, PH_HZ, 'z', 0.24), (PH_X + 0.4, -PH_HZ, 'z', 0.24), (PH_X - 0.6, PH_HZ, 'z', 0.24), (PH_X - 0.6, -PH_HZ, 'z', 0.24),
            (PH_X - PH_HX, 0.0, 'x', 0.24)]


def r_at(x):
    """The hull's radius at x: a cylinder amidships (lengthened by the crew section), tapering to a point at the bow
    and to the screw shaft astern."""
    if x > S:
        x -= S
    elif x > 0:
        x = 0.0
    if abs(x) <= MID:
        return R
    t = min(1.0, (abs(x) - MID) / (HALF - MID))
    return max(0.25 if x < 0 else 0.0, R * (1 - t ** 2) ** 0.6)


ROOMS = [   # (id, name, x0 aft, x1 forward, ceiling)
    ("bridge", "The Bridge", 18.5 + S, 26.0 + S, CEIL),
    ("fab", "The Fabrication Bay", 6.0 + S, 18.5 + S, CEIL),
    ("crew", "Crew Quarters", CREW_X0, CREW_X1, CEIL),
    ("hydro", "Hydroponics & Larder", -1.0, 6.0, CEIL),
    ("dive", "Airlock & Dive Room", -8.0, -1.0, CEIL),
    ("moonpool", "The Moonpool", -16.0, -8.0, CEIL),
    ("engine", "Engine & Boiler Room", -25.0, -16.0, CEIL),
    ("pump", "Pump & Bilge Room", -30.0, -25.0, 1.4),
]


def room_half(x0, x1, ceil):
    """The room's half width: the narrowest chord of the hull at its floor and ceiling, less the wall's thickness."""
    w = 99.0
    for k in range(9):
        x = x0 + (x1 - x0) * k / 8
        r = r_at(x)
        for y in (FLOOR, ceil):
            w = min(w, math.sqrt(max(0.0, r * r - y * y)))
    return round(w - 0.25, 2)


LAYOUT = {"frame": "x bow, y up, z starboard (metres, about the hull's axis)", "rooms": [], "doors": [], "stations": [],
          "lamps": [], "hatches": [], "ladders": [], "windows": [], "moonpool": {}, "lights": [], "cabins": [], "walls": []}


# ---------------------------------------------------------------- the hull
def hull_mesh():
    """The skin: rings of 40 around the axis every half metre, cut later for the windows, the moonpool and the doors."""
    xs = [-HALF + 0.5 * i for i in range(int((2 * HALF + S) / 0.5) + 1)]
    segs = 40
    vs, faces = [], []
    for x in xs:
        r = r_at(x)
        for k in range(segs):
            a = 2 * math.pi * k / segs
            vs.append((x, r * math.sin(a), r * math.cos(a)))   # (k = 0 is the top)
    for i in range(len(xs) - 1):
        for k in range(segs):
            a, b = i * segs + k, i * segs + (k + 1) % segs
            faces.append((a, b, b + segs, a + segs))
    # caps: the stern around the shaft, the bow is already a point
    faces.append(tuple(range(segs - 1, -1, -1)))
    o = B.quads(vs, faces, "hull", "hull")
    outward(o)
    # a real plate thickness: the closed cigar would otherwise be a solid, and every window cut a pocket with a floor
    return hollow(o, 0.12)


def hollow(o, t):
    """Give a closed mesh a plate thickness t inward, so cuts through it make holes, not pockets."""
    sm = o.modifiers.new("plate", 'SOLIDIFY')
    sm.thickness = t; sm.offset = -1.0; sm.use_even_offset = True
    C.select_only([o])
    bpy.ops.object.modifier_apply(modifier=sm.name)
    return o


def outward(o):
    """Recalculate the mesh's normals to face out."""
    C.select_only([o])
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode='OBJECT')


def fin(pts, thick, mat, name="fin"):
    """A flat plate (a rudder, a plane) of some thickness: the outline pts (4 game points in a plane), thickened
    along the plane's normal into a closed prism."""
    p = [Vector(q) for q in pts]
    n = (p[1] - p[0]).cross(p[3] - p[0]).normalized() * (thick / 2)
    vs = [tuple(q + n) for q in p] + [tuple(q - n) for q in p]
    faces = [(0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]
    o = B.quads(vs, faces, mat, name)
    outward(o)
    return o


def tube_ring(c, axis, ra, rb, length, wall, mat, segs=40):
    """A short elliptic sleeve (a window's well through the hull), built directly: an outer and an inner wall and the
    two end rings (a boolean of two coaxial cylinders drops faces)."""
    vs, faces = [], []
    for e in (-1, 1):                       # the two ends along the axis
        for (ka, kb) in ((ra + wall, rb + wall), (ra, rb)):     # outer, then inner
            for k in range(segs):
                a = 2 * math.pi * k / segs
                u, v = ka * math.cos(a), kb * math.sin(a)
                if axis == 'z':
                    vs.append((c[0] + u, c[1] + v, c[2] + e * length / 2))
                else:
                    vs.append((c[0] + e * length / 2, c[1] + v, c[2] + u))
    O0, I0, O1, I1 = 0, segs, 2 * segs, 3 * segs
    for k in range(segs):
        k2 = (k + 1) % segs
        faces.append((O0 + k, O0 + k2, O1 + k2, O1 + k))      # outer wall
        faces.append((I0 + k, I1 + k, I1 + k2, I0 + k2))      # inner wall
        faces.append((O0 + k, I0 + k, I0 + k2, O0 + k2))      # the near end
        faces.append((O1 + k, O1 + k2, I1 + k2, I1 + k))      # the far end
    o = B.quads(vs, faces, mat, "sleeve")
    outward(o)
    return o


def boolean_cut(target, cutter, keep_cutter=False):
    m = target.modifiers.new("cut", 'BOOLEAN')
    m.object = cutter
    m.operation = 'DIFFERENCE'
    m.solver = 'EXACT'
    C.select_only([target])
    bpy.ops.object.modifier_apply(modifier=m.name)
    if not keep_cutter:
        bpy.data.objects.remove(cutter)


def cutter_box(c, half):
    o = C.bevelled_box("cut", (half[0] * 2, half[2] * 2, half[1] * 2), G(*c), 0.0)
    return o


def cutter_ellipse(c, axis, ra, rb, depth):
    """An elliptic cylinder through the hull along a game axis ('z' through the side, 'x' through the bow)."""
    bpy.ops.mesh.primitive_cylinder_add(vertices=28, radius=1.0, depth=depth, location=G(*c))
    o = bpy.context.active_object
    if axis == 'z':      # Blender -y is game z: lie the cylinder along Blender y; ra along x (length), rb along z (height)
        o.rotation_euler = (math.radians(90), 0, 0)
        o.scale = (ra, rb, 1)
    else:                # along game x (Blender x)
        o.rotation_euler = (0, math.radians(90), 0)
        o.scale = (rb, ra, 1)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    return o


def exterior():
    h = hull_mesh()
    # the salon's great windows, both sides; the Bridge's three bow ports; the moonpool through the bottom
    wins = [(10.0 + S, 0.5), (15.0 + S, 0.5)]
    for (x, y) in wins:
        for s in (-1, 1):
            boolean_cut(h, cutter_ellipse((x, y, s * R), 'z', 1.25, 0.75, 2.4))
            LAYOUT["windows"].append({"room": "fab", "x": x, "y": y, "z": s * (R - 0.02), "ra": 1.25, "rb": 0.75})
    boolean_cut(h, cutter_box((-12.0, -R, 0.0), (2.0, 1.0, 1.5)))
    boolean_cut(h, cutter_box((PH_SHAFT, R - 0.4, 0.0), (0.45, 0.8, 0.45)))   # the pilot house's shaft through the back
    # the airlock's outer door (starboard, in the Dive Room) and the deck hatch (over the Dive Room)
    boolean_cut(h, cutter_ellipse((-4.0, 0.0, R), 'z', 0.55, 0.95, 2.0))
    # a porthole for each cabin in the Crew Quarters
    for cx in CABIN_XS:
        for sd in (-1, 1):
            boolean_cut(h, cutter_ellipse((cx, 0.55, sd * R), 'z', 0.32, 0.32, 2.0))
    B.put(h, "hull")
    for cx in CABIN_XS:
        for sd in (-1, 1):
            pts = [(cx + 0.32 * math.cos(a), 0.55 + 0.32 * math.sin(a), sd * (R + 0.02)) for a in [i * 2 * math.pi / 20 for i in range(21)]]
            B.rod(pts, 0.045, "brass", 8)
            B.put(cutter_ellipse((cx, 0.55, sd * (R - 0.1)), 'z', 0.31, 0.31, 0.02), "glass")

    # the deck: a teak walkway along the back, on frames
    for x0, x1 in ((-21.0, -6.0), (-3.0, 19.0 + S)):
        B.box(((x0 + x1) / 2, R + 0.08, 0), ((x1 - x0) / 2, 0.08, 1.3), "teak", 0.02)
    for x in range(-21, 20 + int(S), 2):
        B.box((x, R - 0.05, 0), (0.06, 0.12, 1.35), "iron_in", 0.01)
    # the deck hatch: a brass coaming and a lid hinged open, over the ladder in the Dive Room
    B.lathe((-4.5, R - 0.1, 0), [(0.62, 0.0), (0.62, 0.32), (0.56, 0.34), (0.56, 0.05)], "brass", 24)
    B.disc((-4.5, R + 0.62, -0.6), 'z', 0.6, 0.06, "hull_clean", 24)
    LAYOUT["hatches"].append({"kind": "deck", "x": -4.5, "y": R + 0.3, "z": 0.0, "inside": [-4.5, CEIL - 0.3, 0.0], "outside": [-4.5, R + 0.4, 0.8]})
    LAYOUT["hatches"].append({"kind": "airlock", "x": -4.0, "y": 0.0, "z": R, "inside": [-4.0, FLOOR + 0.1, 2.4], "outside": [-4.0, -0.4, R + 1.6]})
    # the airlock door's brass ring and its dogged lid
    pts = [(-4.0 + 0.6 * math.cos(a), 0.0 + 1.0 * math.sin(a), R + 0.05) for a in [i * 2 * math.pi / 24 for i in range(25)]]
    B.rod(pts, 0.06, "brass", 8)

    # the pilot house: Verne's iron cabin on the back over the Bridge, lenticular ports all round at eye height; the
    # helmsman steers from inside it, looking out over the bow (its inside is part of the interior model)
    # four iron wall panels (separate solids cut cleanly; a hollowed box left plugs in some ports)
    yc, hy, t = PH_FLOOR + PH_H / 2, PH_H / 2, 0.05
    walls = [B.box((PH_X + s * (PH_HX - t), yc, 0), (t, hy, PH_HZ), "hull_clean", 0.0) for s in (-1, 1)]
    walls += [B.box((PH_X, yc, s * (PH_HZ - t)), (PH_HX - 2 * t, hy, t), "hull_clean", 0.0) for s in (-1, 1)]
    for (px_, pz_, ax, rad) in PH_PORTS:
        for wl in walls:
            boolean_cut(wl, cutter_ellipse((px_, PH_EYE, pz_), ax, rad, rad, 0.8))
        B.disc((px_, PH_EYE, pz_), ax, rad + 0.07, 0.1, "brass", 24, rad)
        B.put(cutter_ellipse((px_, PH_EYE, pz_), ax, rad, rad, 0.02), "glass")
    B.box((PH_X, PH_FLOOR + PH_H + 0.05, 0), (PH_HX + 0.1, 0.06, PH_HZ + 0.1), "hull_clean", 0.04)
    LAYOUT["lights"].append({"kind": "pilothouse", "pos": [PH_X, PH_EYE, 0]})
    # the lantern: a dome on the back with a great lens looking forward (the sub's searchlight)
    lx = 12.5 + S
    B.lathe((lx, R - 0.05, 0), [(0.75, 0.0), (0.75, 0.45), (0.6, 0.85), (0.3, 1.05), (0.05, 1.1)], "hull_clean", 28)
    B.disc((lx + 0.62, R + 0.45, 0), 'x', 0.42, 0.12, "brass", 24, 0.34)
    B.disc((lx + 0.66, R + 0.45, 0), 'x', 0.36, 0.06, "lens", 24)
    LAYOUT["lights"].append({"kind": "lantern", "pos": [lx + 0.8, R + 0.45, 0], "dir": [1, -0.12, 0]})
    # the floodlights under the bow
    for s in (-1, 1):
        B.cyl((24.0 + S, -2.2, s * 2.0), (24.6 + S, -2.3, s * 2.0), 0.22, "brass", 16, 0.18)
        B.disc((24.62 + S, -2.3, s * 2.0), 'x', 0.17, 0.03, "lens", 16)
        LAYOUT["lights"].append({"kind": "flood", "pos": [24.8 + S, -2.3, s * 2.0], "dir": [1, -0.25, s * 0.15]})
    # the spur: a long iron ram from the bow, ridged
    B.cyl((33.5 + S, 0, 0), (39.5 + S, 0, 0), 0.55, "hull_clean", 12, 0.02)
    B.box((36.0 + S, 0.35, 0), (3.0, 0.08, 0.05), "hull_clean", 0.01)
    # the stern: the shaft, the rudder above and below, the diving planes
    B.cyl((-34.5, 0, 0), (-37.2, 0, 0), 0.22, "iron_in", 16)
    fin([(-33.0, 0.4, 0.0), (-36.4, 0.4, 0.0), (-36.8, 3.4, 0.0), (-34.2, 3.2, 0.0)], 0.12, "hull_clean", "rudder_top")
    fin([(-33.0, -0.4, 0.0), (-36.4, -0.4, 0.0), (-36.8, -3.0, 0.0), (-34.2, -2.8, 0.0)], 0.12, "hull_clean", "rudder_bot")
    for s in (-1, 1):
        fin([(-31.0, 0.0, s * 1.6), (-34.6, 0.0, s * 0.9), (-35.4, 0.0, s * 3.4), (-32.6, 0.0, s * 3.6)], 0.1, "hull_clean", "plane")
        fin([(22.0 + S, -0.6, s * 3.6), (19.0 + S, -0.6, s * 3.9), (19.4 + S, -0.6, s * 5.2), (21.6 + S, -0.6, s * 5.0)], 0.1, "hull_clean", "bowplane")
    # brass frames round the salon windows, and their glass
    for w in LAYOUT["windows"]:
        pts = [(w["x"] + 1.25 * math.cos(a), w["y"] + 0.75 * math.sin(a), w["z"]) for a in [i * 2 * math.pi / 32 for i in range(33)]]
        B.rod(pts, 0.07, "brass", 8)
        o = cutter_ellipse((w["x"], w["y"], w["z"] - math.copysign(0.25, w["z"])), 'z', 1.22, 0.72, 0.03)
        B.put(o, "glass")


# ---------------------------------------------------------------- the interior
def room_shell(rid, name, x0, x1, ceil, wall="walnut", floor="teak", hole=None):
    """A room's floor (with an optional opening (x0, x1, z0, z1)), ceiling, side walls, skirting, ribs, pipes and lamps.
    Returns (half width, [port wall, starboard wall])."""
    hw = room_half(x0, x1, ceil)
    LAYOUT["rooms"].append({"id": rid, "name": name, "x0": x0, "x1": x1, "half": hw, "floor": FLOOR, "ceil": ceil})
    L = x1 - x0; cx = (x0 + x1) / 2
    if hole is None:
        B.box((cx, FLOOR - 0.06, 0), (L / 2, 0.06, hw + 0.1), floor, 0.0)
    else:
        hx0, hx1, hz0, hz1 = hole
        for (a, b2, c0, c1) in ((x0, hx0, -hw - 0.1, hw + 0.1), (hx1, x1, -hw - 0.1, hw + 0.1), (hx0, hx1, -hw - 0.1, hz0), (hx0, hx1, hz1, hw + 0.1)):
            B.box(((a + b2) / 2, FLOOR - 0.06, (c0 + c1) / 2), ((b2 - a) / 2, 0.06, (c1 - c0) / 2), floor, 0.0)
    ch = (PH_SHAFT - 0.47, PH_SHAFT + 0.47, -0.47, 0.47) if x0 < PH_SHAFT < x1 else None   # (the pilot house's shaft)
    if ch is None:
        B.box((cx, ceil + 0.06, 0), (L / 2, 0.06, hw + 0.1), "iron_in", 0.0)
    else:
        hx0, hx1, hz0, hz1 = ch
        for (a, b2, c0, c1) in ((x0, hx0, -hw - 0.1, hw + 0.1), (hx1, x1, -hw - 0.1, hw + 0.1), (hx0, hx1, -hw - 0.1, hz0), (hx0, hx1, hz1, hw + 0.1)):
            B.box(((a + b2) / 2, ceil + 0.06, (c0 + c1) / 2), ((b2 - a) / 2, 0.06, (c1 - c0) / 2), "iron_in", 0.0)
    walls = []
    for s in (-1, 1):
        walls.append(B.box((cx, (FLOOR + ceil) / 2, s * (hw + 0.06)), (L / 2, (ceil - FLOOR) / 2 + 0.06, 0.06), wall, 0.0))
        B.box((cx, FLOOR + 0.08, s * (hw - 0.02)), (L / 2, 0.08, 0.03), "walnut" if wall != "walnut" else "brass", 0.005)   # (a skirting board)
    # ceiling frames (the hull's ribs) and a pipe along each side
    k = 0
    x = x0 + 0.75
    while x < x1 - 0.3:
        if ch is None or not (ch[0] - 0.1 < x < ch[1] + 0.1):
            B.box((x, ceil - 0.08, 0), (0.07, 0.08, hw), "iron_in", 0.01)
        k += 1; x += 1.5
    for s in (-1, 1):
        B.rod([(x0 + 0.1, ceil - 0.25, s * (hw - 0.25)), (x1 - 0.1, ceil - 0.25, s * (hw - 0.25))], 0.06, "copper", 12)
    # the lamps: a brass fitting with a glass globe every few metres down the middle (the game lights them)
    n = max(1, int(L / 4))
    for i in range(n):
        lx = x0 + L * (i + 0.5) / n
        B.cyl((lx, ceil, 0), (lx, ceil - 0.18, 0), 0.03, "brass", 8)
        B.lathe((lx, ceil - 0.42, 0), [(0.02, 0.0), (0.14, 0.06), (0.16, 0.14), (0.12, 0.22), (0.04, 0.24)], "lamp", 16)
        LAYOUT["lamps"].append({"room": rid, "pos": [lx, ceil - 0.32, 0.0]})
    return hw, walls


def bulkhead(x, hw_a, hw_b, ceil_a, ceil_b, door=True, ports=None):
    """A wall across the hull at x with a round-topped door in the middle (or none)."""
    hw = max(hw_a, hw_b); ceil = max(ceil_a, ceil_b)
    if not door:
        B.box((x, (FLOOR + ceil) / 2, 0), (0.08, (ceil - FLOOR) / 2 + 0.06, hw + 0.12), "iron_in", 0.0)
        return
    dw, dh = 0.62, 2.05
    for s in (-1, 1):
        B.box((x, (FLOOR + ceil) / 2, s * (dw + (hw + 0.12 - dw) / 2)), (0.08, (ceil - FLOOR) / 2 + 0.06, (hw + 0.12 - dw) / 2), "iron_in", 0.0)
    B.box((x, (FLOOR + dh + ceil) / 2, 0), (0.08, (ceil - FLOOR - dh) / 2 + 0.06, dw), "iron_in", 0.0)
    # the door's brass frame and a heavy open hatch door swung back against the wall
    pts = [(x, FLOOR, -dw), (x, FLOOR + dh - 0.25, -dw)] + [(x, FLOOR + dh - 0.25 + 0.25 * math.sin(a), -dw * math.cos(a)) for a in [i * math.pi / 10 for i in range(11)]] + [(x, FLOOR, dw)]
    B.rod(pts, 0.05, "brass", 8, caps=True)
    B.box((x - 0.15, FLOOR + 1.0, dw + 0.55), (0.06, 0.95, 0.5), "iron_in", 0.03)
    LAYOUT["doors"].append({"x": x, "z": 0.0, "w": dw * 2, "h": dh})


def pilot_house():
    """The pilot house from inside: a floor over the shaft, walls with the same ports as the shell, the helm wheel's
    pedestal facing the bow; the shaft down to the Bridge with its ladder. (The Bridge's ceiling has the shaft's hole.)"""
    LAYOUT["rooms"].append({"id": "pilot", "name": "The Pilot House", "x0": PH_X - PH_HX, "x1": PH_X + PH_HX, "half": PH_HZ - 0.1,
                            "floor": PH_FLOOR, "ceil": PH_FLOOR + PH_H})
    ix, iz = PH_HX - 0.08, PH_HZ - 0.08
    # the floor, leaving the shaft open
    s0, s1 = PH_SHAFT - 0.45, PH_SHAFT + 0.45
    B.box(((PH_X - ix + s0) / 2, PH_FLOOR - 0.05, 0), ((s0 - (PH_X - ix)) / 2, 0.05, iz), "teak", 0.0)
    B.box(((s1 + PH_X + ix) / 2, PH_FLOOR - 0.05, 0), ((PH_X + ix - s1) / 2, 0.05, iz), "teak", 0.0)
    for s in (-1, 1):
        B.box((PH_SHAFT, PH_FLOOR - 0.05, s * (0.45 + (iz - 0.45) / 2)), (0.45, 0.05, (iz - 0.45) / 2), "teak", 0.0)
    B.box((PH_X, PH_FLOOR + PH_H - 0.05, 0), (ix, 0.05, iz), "walnut", 0.0)
    # the walls, the ports cut through them
    walls = [B.box((PH_X + s * ix, PH_FLOOR + PH_H / 2, 0), (0.04, PH_H / 2, iz), "walnut", 0.0) for s in (-1, 1)]
    walls += [B.box((PH_X, PH_FLOOR + PH_H / 2, s * iz), (ix, PH_H / 2, 0.04), "walnut", 0.0) for s in (-1, 1)]
    for (px_, pz_, ax, rad) in PH_PORTS:
        for wl in walls:
            boolean_cut(wl, cutter_ellipse((px_, PH_EYE, pz_), ax, rad, rad, 0.6))
        B.disc((px_ - (0.06 if ax == 'x' and px_ > PH_X else -0.06 if ax == 'x' else 0), PH_EYE, pz_ - (0.06 if ax == 'z' and pz_ > 0 else -0.06 if ax == 'z' else 0)), ax, rad + 0.06, 0.04, "brass", 24, rad)
    # the shaft's walls from the Bridge's ceiling up to the pilot house's floor, and the ladder up it
    for s in (-1, 1):
        B.box((PH_SHAFT + s * 0.47, (CEIL + PH_FLOOR) / 2, 0), (0.03, (PH_FLOOR - CEIL) / 2, 0.47), "iron_in", 0.0)
        B.box((PH_SHAFT, (CEIL + PH_FLOOR) / 2, s * 0.47), (0.47, (PH_FLOOR - CEIL) / 2, 0.03), "iron_in", 0.0)
    for s in (-1, 1):
        B.cyl((PH_SHAFT - 0.32, FLOOR, s * 0.26), (PH_SHAFT - 0.32, PH_FLOOR + 0.9, s * 0.26), 0.03, "brass", 8)
    y = FLOOR + 0.3
    while y < PH_FLOOR + 0.1:
        B.cyl((PH_SHAFT - 0.32, y, -0.26), (PH_SHAFT - 0.32, y, 0.26), 0.022, "brass", 8)
        y += 0.32
    LAYOUT["ladders"].append({"x": PH_SHAFT - 0.32, "z": 0.0, "y0": FLOOR, "y1": PH_FLOOR + 0.05, "hatch": ""})
    # the helm: a pedestal and the wheel (the wheel itself is its own model so it can turn)
    B.cyl((PH_X + 0.85, PH_FLOOR, 0), (PH_X + 0.85, PH_FLOOR + 1.05, 0), 0.08, "brass", 12)
    B.box((PH_X + 0.95, PH_FLOOR + 1.05, 0), (0.12, 0.08, 0.1), "brass", 0.02)
    LAYOUT["helmWheel"] = [PH_X + 0.88, PH_FLOOR + 1.12, 0.0]
    station("helm", "helm", (PH_X + 0.2, PH_FLOOR, 0.0), (1, 0, 0), "pilot")
    # a compass binnacle and the depth gauge beside the wheel
    B.lathe((PH_X + 0.75, PH_FLOOR, 0.6), [(0.12, 0.0), (0.12, 0.9), (0.16, 0.95), (0.16, 1.05), (0.06, 1.12)], "brass", 16)
    B.disc((PH_X + 1.18, PH_FLOOR + 1.25, -0.6), 'x', 0.18, 0.04, "brass", 20)
    B.disc((PH_X + 1.16, PH_FLOOR + 1.25, -0.6), 'x', 0.15, 0.01, "dial", 20)
    LAYOUT["gauges"] = {"depth": [PH_X + 1.15, PH_FLOOR + 1.25, -0.6]}


CABIN_XS = (CREW_X0 + S * 0.25, CREW_X0 + S * 0.75)    # the cabins' middles along her (two each side)


def wall(x0, x1, z0, z1, mat="walnut"):
    """A partition from (x0, z0) to (x1, z1), floor to ceiling, written to the layout for the game's colliders."""
    if x1 - x0 < 0.01 or z1 - z0 < 0.01:
        return
    B.box(((x0 + x1) / 2, (FLOOR + CEIL) / 2, (z0 + z1) / 2), ((x1 - x0) / 2, (CEIL - FLOOR) / 2, (z1 - z0) / 2), mat, 0.0)
    LAYOUT["walls"].append({"x0": x0, "x1": x1, "z0": z0, "z1": z1})


def crew_quarters(hw):
    """Four cabins, two each side of a central passage, for the crew to make their own: a bunk with a blanket, a small
    desk and chair, a locker, a shelf, a porthole in the hull and a lamp. (The decorating comes later; the layout
    lists each cabin's floor so things can be placed in it.)"""
    cw = 0.65                       # half the passage's width
    dw = 0.45                       # half a cabin door's width
    t = 0.04
    mid = (CREW_X0 + CREW_X1) / 2
    for sd in (-1, 1):
        # the passage wall on this side, with a door into each cabin
        z = sd * cw
        xs = [CREW_X0]
        for cx in CABIN_XS:
            xs += [cx - dw, cx + dw]
        xs.append(CREW_X1)
        for k in range(0, len(xs), 2):
            wall(xs[k], xs[k + 1], z - t if sd > 0 else z - t, z + t)
        for cx in CABIN_XS:   # the lintel over each door
            B.box((cx, (FLOOR + 2.05 + CEIL) / 2, z), (dw, (CEIL - FLOOR - 2.05) / 2, t), "walnut", 0.0)
            pts = [(cx - dw, FLOOR, z), (cx - dw, FLOOR + 2.05, z), (cx + dw, FLOOR + 2.05, z), (cx + dw, FLOOR, z)]
            B.rod(pts, 0.03, "brass", 6)
        # the wall between the two cabins on this side
        z0, z1 = (cw, hw) if sd > 0 else (-hw, -cw)
        wall(mid - t, mid + t, z0, z1)
    n = 0
    for sd in (-1, 1):
        for cx in CABIN_XS:
            n += 1
            zi, zo = sd * cw, sd * hw              # the passage side and the hull side
            x0, x1 = (CREW_X0, mid) if cx < mid else (mid, CREW_X1)
            zlo, zhi = min(zi, zo), max(zi, zo)
            LAYOUT["cabins"].append({"id": f"cabin{n}", "name": f"Cabin {n}", "x0": x0 + t, "x1": x1 - t, "z0": zlo + t, "z1": zhi - t,
                                     "door": [cx, zi], "porthole": [cx, 0.55, sd * (hw + 0.05)]})
            # the bunk along the inner wall (the bulkhead or the partition), the blanket and pillow
            bx = x0 + 0.5 if cx < mid else x1 - 0.5
            bz = (zi + zo) / 2 + sd * 0.25
            B.box((bx, FLOOR + 0.25, bz), (0.45, 0.25, 0.95), "walnut", 0.02)
            B.box((bx, FLOOR + 0.53, bz), (0.42, 0.06, 0.92), "rug", 0.04)
            B.box((bx, FLOOR + 0.62, bz + sd * 0.7), (0.3, 0.06, 0.18), "chart", 0.04)
            # the desk and chair under the porthole, a locker by the door, a shelf over the bunk
            dx = x1 - 0.6 if cx < mid else x0 + 0.6
            B.box((dx, FLOOR + 0.75, zo - sd * 0.32), (0.45, 0.03, 0.3), "walnut", 0.01)
            B.box((dx, FLOOR + 0.37, zo - sd * 0.32), (0.4, 0.37, 0.02), "walnut", 0.0)
            B.box((dx, FLOOR + 0.24, zo - sd * 0.9), (0.2, 0.24, 0.2), "leather", 0.05)
            lx = x1 - 0.35 if cx < mid else x0 + 0.35
            B.box((lx, FLOOR + 0.95, zi + sd * 0.3), (0.25, 0.95, 0.24), "iron_in", 0.02)
            B.box((bx, FLOOR + 1.6, zo - sd * 0.12), (0.4, 0.02, 0.1), "walnut", 0.0)
            B.box((bx, FLOOR + 1.72, zo - sd * 0.12), (0.3, 0.1, 0.07), "books", 0.01)
            # a lamp of its own
            B.cyl((cx, CEIL, (zi + zo) / 2), (cx, CEIL - 0.15, (zi + zo) / 2), 0.03, "brass", 8)
            B.lathe((cx, CEIL - 0.36, (zi + zo) / 2), [(0.02, 0.0), (0.12, 0.05), (0.13, 0.12), (0.1, 0.18), (0.03, 0.2)], "lamp", 14)
            LAYOUT["lamps"].append({"room": "crew", "pos": [cx, CEIL - 0.3, (zi + zo) / 2]})
            station(f"cabin{n}", "cabin", (cx, FLOOR, zi + sd * 0.2), (0, 0, sd), "crew")


def station(sid, kind, pos, facing, room):
    LAYOUT["stations"].append({"id": sid, "kind": kind, "pos": list(pos), "facing": list(facing), "room": room})


def interior():
    hws, walls = {}, {}
    for (rid, name, x0, x1, ceil) in ROOMS:
        wall = "walnut" if rid in ("bridge", "fab", "hydro", "crew") else "iron_in"
        floor = "teak" if rid in ("bridge", "fab", "hydro", "dive") else "chequer"
        hole = (-14.0, -10.0, -1.5, 1.5) if rid == "moonpool" else None
        hws[rid], walls[rid] = room_shell(rid, name, x0, x1, ceil, wall, floor, hole)
    for i in range(len(ROOMS) - 1):
        a, b = ROOMS[i], ROOMS[i + 1]
        bulkhead(a[2], hws[a[0]], hws[b[0]], a[4], b[4])
    bulkhead(ROOMS[-1][2], hws["pump"], hws["pump"], 1.4, 1.4, door=False)
    # the Bridge's front wall
    hwb = hws["bridge"]
    B.box((ROOMS[0][3], (FLOOR + CEIL) / 2, 0), (0.08, (CEIL - FLOOR) / 2 + 0.06, hwb + 0.12), "walnut", 0.0)
    pilot_house()
    # the salon's great windows through its walls, with sleeves out to the hull
    for w in LAYOUT["windows"]:
        side = 0 if w["z"] < 0 else 1
        hw = hws["fab"]
        boolean_cut(walls["fab"][side], cutter_ellipse((w["x"], w["y"], math.copysign(hw + 0.06, w["z"])), 'z', w["ra"], w["rb"], 1.0))
        zmid = math.copysign((hw + R) / 2, w["z"])
        tube_ring((w["x"], w["y"], zmid), 'z', w["ra"], w["rb"], R - hw + 0.2, 0.08, "brass")

    # ---- the Bridge: the engine telegraph, the sonar console, the chart table (the helm is up in the pilot house)
    B.cyl((24.0 + S, FLOOR, 1.5), (24.0 + S, FLOOR + 1.05, 1.5), 0.13, "brass", 16)   # the telegraph's pedestal
    B.disc((24.0 + S, FLOOR + 1.22, 1.5), 'x', 0.26, 0.12, "brass", 24)
    B.disc((24.07 + S, FLOOR + 1.22, 1.5), 'x', 0.21, 0.02, "dial", 24)
    station("telegraph", "telegraph", (23.4 + S, FLOOR, 1.5), (1, 0, 0), "bridge")
    B.box((23.6 + S, FLOOR + 0.55, -1.6), (0.4, 0.55, 0.45), "iron_in", 0.03)     # the sonar console
    B.box((23.95 + S, FLOOR + 1.25, -1.6), (0.08, 0.32, 0.36), "brass", 0.02)
    B.disc((24.04 + S, FLOOR + 1.25, -1.6), 'x', 0.27, 0.02, "screen", 32)
    station("sonar", "sonar", (23.0 + S, FLOOR, -1.6), (1, 0, 0), "bridge")
    B.box((19.7 + S, FLOOR + 0.45, 0), (0.6, 0.45, 0.6), "walnut", 0.03)      # the chart table
    B.box((19.7 + S, FLOOR + 0.92, 0), (0.55, 0.02, 0.55), "chart", 0.005)
    for (gx, gz) in ((19.0 + S, 1.9), (19.0 + S, -1.9)):                          # the depth gauge and the clock
        B.disc((gx, 0.8, gz + (0.02 if gz < 0 else -0.02)), 'z', 0.32, 0.05, "brass", 24)
        B.disc((gx, 0.8, gz + (0.05 if gz < 0 else -0.05)), 'z', 0.27, 0.01, "dial", 24)
    LAYOUT["bridgeGauges"] = {"depth": [19.0 + S, 0.8, -1.85], "clock": [19.0 + S, 0.8, 1.85]}

    # ---- the Fabrication Bay (the old salon): the fabricator, a workbench, bookcases, the organ, a rug, chairs
    hw = hws["fab"]
    B.box((12.5 + S, FLOOR + 0.9, -(hw - 0.55)), (0.7, 0.9, 0.5), "brass", 0.05)            # the fabricator
    B.lathe((12.5 + S, FLOOR + 1.8, -(hw - 0.55)), [(0.45, 0.0), (0.42, 0.3), (0.25, 0.55), (0.05, 0.62)], "glass", 20)
    B.box((12.5 + S, FLOOR + 1.2, -(hw - 1.06)), (0.45, 0.25, 0.02), "screen", 0.005)
    station("fabricator", "fabricator", (12.5 + S, FLOOR, -(hw - 1.7)), (0, 0, -1), "fab")
    B.box((16.6 + S, FLOOR + 0.45, hw - 0.5), (1.2, 0.45, 0.45), "walnut", 0.03)            # the workbench (the Abyssal Forge's place)
    B.box((16.6 + S, FLOOR + 0.92, hw - 0.5), (1.25, 0.03, 0.5), "iron_in", 0.01)
    station("forge", "forge", (16.6 + S, FLOOR, hw - 1.4), (0, 0, 1), "fab")
    for (bx, s) in ((7.4 + S, 1), (7.4 + S, -1), (17.6 + S, -1)):                                  # bookcases
        B.box((bx, FLOOR + 1.1, s * (hw - 0.25)), (0.75, 1.1, 0.22), "walnut", 0.02)
        for k in range(4):
            B.box((bx, FLOOR + 0.35 + k * 0.5, s * (hw - 0.3)), (0.7, 0.18, 0.16), "books", 0.01)
    B.box((12.5 + S, FLOOR + 0.006, 0), (3.5, 0.006, 1.6), "rug", 0.0)                       # the rug
    for (cx, cz) in ((11.0 + S, 0.9), (14.0 + S, 0.9)):
        B.box((cx, FLOOR + 0.25, cz), (0.4, 0.25, 0.4), "leather", 0.08)
        B.box((cx - 0.3, FLOOR + 0.6, cz), (0.08, 0.35, 0.4), "leather", 0.06)

    crew_quarters(hws["crew"])

    # ---- Hydroponics & Larder: planter troughs under grow lamps, larder shelves, the Pressure Grill and the desalinator
    hw = hws["hydro"]
    for k in range(2):
        tx = 1.0 + k * 2.6
        B.box((tx, FLOOR + 0.4, hw - 0.55), (1.0, 0.4, 0.4), "copper", 0.03)
        B.box((tx, FLOOR + 0.79, hw - 0.55), (0.95, 0.02, 0.35), "soil", 0.0)
        LAYOUT["lamps"].append({"room": "hydro", "pos": [tx, FLOOR + 1.8, hw - 0.55], "grow": True})
    station("planter", "planter", (2.3, FLOOR, hw - 1.3), (0, 0, 1), "hydro")
    for k in range(3):
        B.box((5.0, FLOOR + 0.3 + k * 0.6, -(hw - 0.3)), (0.8, 0.03, 0.28), "walnut", 0.01)
    station("larder", "larder", (5.0, FLOOR, -(hw - 1.1)), (0, 0, -1), "hydro")
    B.box((0.0, FLOOR + 0.45, -(hw - 0.45)), (0.45, 0.45, 0.4), "iron_in", 0.04)        # the Pressure Grill (an iron range)
    B.cyl((0.0, FLOOR + 0.9, -(hw - 0.45)), (0.0, CEIL, -(hw - 0.45)), 0.09, "iron_in", 12)
    station("grill", "grill", (0.0, FLOOR, -(hw - 1.2)), (0, 0, -1), "hydro")
    B.lathe((2.7, FLOOR, -(hw - 0.45)), [(0.32, 0.0), (0.34, 0.5), (0.28, 0.85), (0.1, 1.0), (0.05, 1.3)], "copper", 18)   # the desalinator's still
    station("desalinator", "desalinator", (2.7, FLOOR, -(hw - 1.2)), (0, 0, -1), "hydro")

    # ---- the Airlock & Dive Room: suit lockers, the oxygen rack, the airlock door, the ladder to the deck hatch
    hw = hws["dive"]
    for k in range(4):
        lx = -7.3 + k * 0.75
        B.box((lx, FLOOR + 1.0, -(hw - 0.3)), (0.33, 1.0, 0.28), "iron_in", 0.02)
        B.lathe((lx, FLOOR + 1.45, -(hw - 0.3)), [(0.02, 0.0), (0.2, 0.1), (0.22, 0.28), (0.15, 0.42), (0.02, 0.45)], "brass", 16)   # a hard-hat on its shelf
    station("lockers", "lockers", (-6.2, FLOOR, -(hw - 1.1)), (0, 0, -1), "dive")
    for k in range(5):
        B.cyl((-2.2 - k * 0.32, FLOOR + 0.05, -(hw - 0.25)), (-2.2 - k * 0.32, FLOOR + 1.05, -(hw - 0.25)), 0.13, "o2", 14)
    station("oxygen", "oxygen", (-2.8, FLOOR, -(hw - 1.0)), (0, 0, -1), "dive")
    pts = [(-4.0 + 0.62 * math.cos(a), FLOOR + 1.0 + 0.95 * math.sin(a), hw - 0.02) for a in [i * 2 * math.pi / 24 for i in range(25)]]
    B.rod(pts, 0.07, "brass", 8)
    B.disc((-4.0, FLOOR + 1.0, hw - 0.1), 'z', 0.18, 0.05, "brass", 16, 0.12)   # the dogging wheel
    station("airlock", "airlock", (-4.0, FLOOR, hw - 0.9), (0, 0, 1), "dive")
    # the ladder up to the deck hatch (through the ceiling and the hull)
    for s in (-1, 1):
        B.cyl((-4.5, FLOOR, s * 0.28), (-4.5, CEIL, s * 0.28), 0.03, "brass", 8)
    y = FLOOR + 0.3
    while y < CEIL - 0.1:
        B.cyl((-4.5, y, -0.28), (-4.5, y, 0.28), 0.022, "brass", 8)
        y += 0.32
    B.disc((-4.5, CEIL - 0.02, 0.0), 'y', 0.62, 0.08, "brass", 24, 0.5)        # the hatch's ring in the ceiling
    B.disc((-4.5, CEIL - 0.05, 0.0), 'y', 0.52, 0.06, "iron_in", 24)          # and its lid, dogged shut
    LAYOUT["ladders"].append({"x": -4.5, "z": 0.0, "y0": FLOOR, "y1": CEIL - 0.2, "hatch": "deck"})

    # ---- the Moonpool: the opening in the floor down through the hull, a railing, a winch overhead
    LAYOUT["moonpool"] = {"x0": -14.0, "x1": -10.0, "z0": -1.5, "z1": 1.5, "y": FLOOR - 0.35}
    for s in (-1, 1):
        B.box((-12.0, (FLOOR + -R) / 2, s * 1.55), (2.0, (R + FLOOR) / 2, 0.06), "iron_in", 0.0)
        B.box((-12.0 + s * 2.05, (FLOOR + -R) / 2, 0), (0.06, (R + FLOOR) / 2, 1.5), "iron_in", 0.0)
    for (rx, rz) in ((-14.1, -1.6), (-14.1, 1.6), (-9.9, -1.6), (-9.9, 1.6), (-12.0, -1.6), (-12.0, 1.6)):
        B.cyl((rx, FLOOR, rz), (rx, FLOOR + 1.0, rz), 0.035, "brass", 8)
    B.rod([(-14.1, FLOOR + 1.0, -1.6), (-9.9, FLOOR + 1.0, -1.6)], 0.035, "brass", 8)
    B.rod([(-14.1, FLOOR + 1.0, 1.6), (-9.9, FLOOR + 1.0, 1.6)], 0.035, "brass", 8)
    B.box((-12.0, CEIL - 0.3, 0), (0.25, 0.25, 1.2), "iron_in", 0.03)
    B.disc((-12.0, CEIL - 0.3, 1.25), 'z', 0.35, 0.12, "iron_in", 20)
    station("moonpool", "moonpool", (-12.0, FLOOR, -2.2), (0, 0, 1), "moonpool")

    # ---- the Engine & Boiler Room: the boiler, the engine's cylinders and flywheel, the switchboard (power states)
    hw = hws["engine"]
    B.cyl((-23.8, 0.0, -1.3), (-19.0, 0.0, -1.3), 1.15, "iron_in", 24)                # the boiler, lying along her
    B.disc((-18.95, -0.3, -1.3), 'x', 0.35, 0.08, "brass", 16)                       # the firebox door
    station("boiler", "boiler", (-18.2, FLOOR, -1.3), (1, 0, 0), "engine")
    for k in range(2):
        B.cyl((-21.5 + k * 1.2, FLOOR, 1.5), (-21.5 + k * 1.2, FLOOR + 1.7, 1.5), 0.36, "iron_in", 16)
        B.cyl((-21.5 + k * 1.2, FLOOR + 1.7, 1.5), (-21.5 + k * 1.2, FLOOR + 1.85, 1.5), 0.42, "brass", 16)
    B.disc((-23.4, FLOOR + 0.9, 1.5), 'z', 0.85, 0.16, "iron_in", 32, 0.6)          # the flywheel
    for k in range(4):
        B.rod([(-24.6, CEIL - 0.45 - k * 0.15, -hw + 0.3), (-16.4, CEIL - 0.45 - k * 0.15, -hw + 0.3)], 0.05, "copper", 10)
    B.box((-17.0, FLOOR + 1.1, hw - 0.12), (0.7, 0.75, 0.08), "walnut", 0.02)        # the switchboard
    for k in range(3):
        B.disc((-17.4 + k * 0.4, FLOOR + 1.4, hw - 0.2), 'z', 0.12, 0.04, "dial", 16)
    station("switchboard", "power", (-17.0, FLOOR, hw - 0.9), (0, 0, 1), "engine")

    # ---- the Pump & Bilge Room: two pumps with their wheels, the valves, a grating over the bilge
    for s in (-1, 1):
        B.cyl((-27.5, FLOOR, s * 0.9), (-27.5, FLOOR + 1.2, s * 0.9), 0.32, "iron_in", 16)
        B.disc((-27.5, FLOOR + 1.35, s * 0.9), 'y', 0.3, 0.05, "brass", 16, 0.24)
    station("pumps", "pumps", (-26.4, FLOOR, 0.0), (-1, 0, 0), "pump")


def main():
    out = C.out_dir()
    C.reset()
    T = dict(deep_tiles.build_all())

    def make_mats():
        for k, t in T.items():
            B.MATS[k] = B.tile_mat(t)
        B.MATS["brass"] = B.flat("brass", (0.55, 0.40, 0.17), 0.35, 1.0)
        B.MATS["copper"] = B.flat("copper", (0.55, 0.27, 0.16), 0.4, 1.0)
        B.MATS["glass"] = B.flat("glass", (0.03, 0.05, 0.06), 0.05, 0.0)
        B.MATS["lens"] = B.flat("lens", (0.75, 0.70, 0.55), 0.1, 0.0)
        B.MATS["lamp"] = B.flat("lamp", (0.85, 0.80, 0.62), 0.2, 0.0)
        B.MATS["dial"] = B.flat("dial", (0.78, 0.74, 0.62), 0.45)
        B.MATS["screen"] = B.flat("screen", (0.02, 0.07, 0.05), 0.15)
        B.MATS["chart"] = B.flat("chart", (0.72, 0.66, 0.50), 0.8)
        B.MATS["books"] = B.flat("books", (0.30, 0.12, 0.08), 0.7)
        B.MATS["rug"] = B.flat("rug", (0.35, 0.08, 0.06), 0.95)
        B.MATS["leather"] = B.flat("leather", (0.22, 0.10, 0.05), 0.55)
        B.MATS["soil"] = B.flat("soil", (0.12, 0.08, 0.05), 0.95)
        B.MATS["o2"] = B.flat("oxygen bottle", (0.18, 0.30, 0.22), 0.4, 0.6)

    make_mats()

    part = C.args()[C.args().index("--part") + 1] if "--part" in C.args() else "all"
    def grime(x, y, z, k):
        return k
    if part in ("all", "hull"):
        B.PARTS.clear()
        exterior()
        B.finish(out, "nautilus_hull.glb", grime)
    if part in ("all", "interior"):
        C.reset()
        make_mats()
        B.PARTS.clear()
        LAYOUT["stations"].clear(); LAYOUT["lamps"].clear(); LAYOUT["rooms"].clear(); LAYOUT["doors"].clear(); LAYOUT["ladders"].clear()
        interior()
        B.finish(out, "nautilus_interior.glb", grime)
    if part == "all":
        with open(os.path.join(out, "nautilus_layout.json"), "w") as f:
            json.dump(LAYOUT, f, indent=1)
        print("artgen: wrote", os.path.join(out, "nautilus_layout.json"))


if __name__ == "__main__":
    main()
