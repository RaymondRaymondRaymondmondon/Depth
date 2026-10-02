# Red Tide's four divers (the Red Tide Visual Overhaul Spec, "Player characters: the four divers"), in the Trawl
# crew's stylised look (the user's call): the same skeleton and body as tools/artgen/crew.py, dressed in period diving
# gear whose silhouette no skin changes:
#     the Diver     round brass helmet with three ports and grilles, a bolted corselet, canvas dress, a weighted belt,
#                   lead-soled boots, an air hose looping from the back, a sheath knife
#     the Whaler    a hood drawn over a smaller face-plate helmet, a long slicker with straps and toggles, a harpoon-line
#                   coil, a rope belt, tarred gloves
#     the Stowaway  a suit patched in different canvases, a dented helmet with one mismatched port, a bottle on a cord at
#                   the chest, a sack, string-tied boots
#     the Mechanic  a riveted steel hard suit with articulated joints, a domed helmet with a grid of small ports, gauges
#                   and valves on the chest, a tool roll
# The face is the sailors' face, inside the helmet, seen through the ports. Helmets ride the chest bone (a standard
# dress helmet is bolted to its corselet: the head turns inside it). Colours are flat named materials as on the crew
# (skin, top, trousers, boots, hat, accent, leather, metal, eye_white, eye_dark, plus glass) so a token skin is a
# recolour of the same mesh.
#
#     blender -b --factory-startup -P tools/artgen/rt_divers.py -- --out assets/shared/divers [--diver diver]

import os
import sys
import math
import random
import bmesh
from mathutils import Vector
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import common as C
import crew as K
bpy = C.bpy

DIVERS = ["diver", "whaler", "stowaway", "mechanic"]
HEAD = Vector((0.0, 0, 1.61))   # the skull's centre (crew.py's 1.655 + HEAD_DZ)

STYLE = {
    "skin": ((0.78, 0.55, 0.42), 0.6), "leather": ((0.3, 0.19, 0.1), 0.65), "metal": ((0.7, 0.52, 0.24), 0.32),
    "eye_white": ((0.92, 0.91, 0.88), 0.2), "eye_dark": ((0.03, 0.03, 0.035), 0.15), "glass": ((0.55, 0.7, 0.66), 0.08),
}
COLOURS = {   # top/trousers the suit, boots, hat the helmet, accent the corselet/trim
    "diver":    {"top": ((0.62, 0.55, 0.4), 0.9), "trousers": ((0.62, 0.55, 0.4), 0.9), "boots": ((0.2, 0.2, 0.21), 0.5),
                 "hat": ((0.78, 0.5, 0.26), 0.3), "accent": ((0.82, 0.62, 0.28), 0.3)},
    "whaler":   {"top": ((0.2, 0.22, 0.2), 0.35), "trousers": ((0.32, 0.3, 0.26), 0.8), "boots": ((0.1, 0.09, 0.08), 0.5),
                 "hat": ((0.62, 0.62, 0.6), 0.35), "accent": ((0.42, 0.33, 0.2), 0.7)},
    "stowaway": {"top": ((0.55, 0.47, 0.36), 0.92), "trousers": ((0.4, 0.42, 0.36), 0.9), "boots": ((0.24, 0.2, 0.17), 0.7),
                 "hat": ((0.66, 0.42, 0.26), 0.4), "accent": ((0.42, 0.5, 0.56), 0.85)},
    "mechanic": {"top": ((0.46, 0.47, 0.48), 0.42), "trousers": ((0.4, 0.41, 0.42), 0.45), "boots": ((0.22, 0.22, 0.23), 0.45),
                 "hat": ((0.5, 0.51, 0.52), 0.4), "accent": ((0.76, 0.56, 0.26), 0.3)},
}
METALLIC = {"metal", "hat", "accent"}


def sphere(name, loc, radius, mat, segs=40, rings=24, scale=(1, 1, 1)):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segs, ring_count=rings, radius=radius, location=loc)
    o = bpy.context.active_object; o.name = name; o.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    C.assign(o, mat); C.smooth(o, 180)
    return o


def cut_ports(o, ports, centre):
    """Cuts round holes in a helmet shell: each port (direction from the centre, half-angle in degrees)."""
    bm = bmesh.new(); bm.from_mesh(o.data)
    dead = []
    for f in bm.faces:
        d = (o.matrix_world @ f.calc_center_median() - centre).normalized()
        for (axis, ang) in ports:
            if d.dot(Vector(axis).normalized()) > math.cos(math.radians(ang)):
                dead.append(f); break
    bmesh.ops.delete(bm, geom=dead, context='FACES')
    bm.to_mesh(o.data); bm.free()


def shell(o, thick=0.008):
    s = o.modifiers.new("thick", 'SOLIDIFY'); s.thickness = thick; s.offset = -1
    C.select_only([o]); bpy.ops.object.modifier_apply(modifier="thick"); C.smooth(o, 180)


def ring_at(name, centre, axis, r_major, r_minor, mat):
    """A torus round `axis` (a direction) at centre."""
    bpy.ops.mesh.primitive_torus_add(major_radius=r_major, minor_radius=r_minor, location=centre, major_segments=32, minor_segments=10)
    o = bpy.context.active_object; o.name = name
    o.rotation_euler = Vector((0, 0, 1)).rotation_difference(Vector(axis).normalized()).to_euler()
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    C.assign(o, mat); C.smooth(o, 180)
    return o


def rod(name, a, b, r, mat, verts=10):
    o = C.tube_along(name, [tuple(a), tuple(b)], r, verts=verts)
    C.assign(o, mat); C.smooth(o, 60)
    return o


def box(name, size, loc, mat, bevel=0.01, rot=(0, 0, 0), segs=2):
    o = C.bevelled_box(name, size, loc=loc, bevel=bevel, segments=segs, rot=rot)
    C.assign(o, mat); C.apply_all(o); C.smooth(o, 40)
    return o


def port(name, centre, dirn, r, mats, grille=True, glass=False, ring_mat="accent"):
    """A helmet port: a thick brass rim and a grille of three bars across it (glass, when it comes, is drawn by the game:
    an opaque pane would hide the face)."""
    out = [ring_at(name + "_rim", centre, dirn, r, r * 0.16, mats[ring_mat])]
    d = Vector(dirn).normalized()
    if glass:
        g = C.cylinder(name + "_glass", r * 0.98, 0.003, loc=tuple(Vector(centre) - d * 0.012), verts=32, bevel=0)
        g.rotation_euler = Vector((0, 0, 1)).rotation_difference(d).to_euler()
        C.select_only([g]); bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
        C.assign(g, mats["glass"]); out.append(g)
    if grille:
        up = Vector((0, 0, 1)); side = d.cross(up).normalized() if abs(d.dot(up)) < 0.9 else Vector((0, 1, 0))
        for k in (-0.5, 0.0, 0.5):
            c = Vector(centre) + d * 0.006 + side * (k * r)
            h = math.sqrt(max(0.0, 1 - k * k)) * r
            out.append(rod(f"{name}_bar{k}", c - up * h, c + up * h, r * 0.06, mats[ring_mat], 8))
    return out


# ---------------------------------------------------------------- each diver's gear: [(object, bone or None)]
def gear_diver(J, mats):
    G = []
    hc = Vector((0.0, 0, 1.635))
    helm = sphere("helmet", tuple(hc), 0.205, mats["hat"], 48, 32, (1.0, 0.97, 1.02))
    ports = [((1, 0, 0), 26), ((0.25, 1, 0), 18), ((0.25, -1, 0), 18)]
    cut_ports(helm, ports, hc); shell(helm, 0.01); G.append((helm, "chest"))
    G += [(o, "chest") for o in port("front", tuple(hc + Vector((0.2, 0, 0))), (1, 0, 0), 0.088, mats)]
    for s in (1, -1):
        d = Vector((0.25, s, 0)).normalized()
        G += [(o, "chest") for o in port(f"side{s}", tuple(hc + d * 0.197), tuple(d), 0.064, mats)]
    # the top valve and the exhaust, the neck ring
    G.append((C.cylinder("valve", 0.022, 0.05, loc=tuple(hc + Vector((-0.12, 0, 0.17))), rot=(0, -0.6, 0), verts=16, bevel=0.003), "chest"))
    C.assign(G[-1][0], mats["accent"]); C.apply_all(G[-1][0])
    G.append((ring_at("neck_ring", (0.0, 0, 1.44), (0, 0, 1), 0.17, 0.025, mats["accent"]), "chest"))
    # the corselet: a broad brass breastplate over the shoulders, bolted round its rim
    cors = K.lathe_z("corselet", [(0.27, -0.12), (0.25, -0.06), (0.21, 0.0), (0.165, 0.03)], (0.0, 0, 1.43), "accent", mats, sx=0.82, sy=1.0)
    G.append((cors, "chest"))
    for k in range(12):
        a = 2 * math.pi * k / 12
        G.append((sphere(f"bolt{k}", (0.27 * 0.82 * math.cos(a) * 0.95, 0.27 * math.sin(a) * 0.95, 1.315), 0.014, mats["metal"], 12, 8), "chest"))
    # the air hose from the back of the helmet, looping out and down
    hose = C.tube_along("hose", [(-0.2, 0, 1.66), (-0.33, 0.02, 1.62), (-0.42, 0.06, 1.45), (-0.4, 0.08, 1.22), (-0.3, 0.1, 1.05)], 0.028, verts=12)
    C.assign(hose, mats["leather"]); C.smooth(hose, 180); G.append((hose, "chest"))
    # the weighted belt with lead blocks, the sheath knife at the right hip
    G.append((K.tube_ring("belt", (0.0, 0, 0.97), 0.16, 0.03, "leather", mats, squash=0.82), "pelvis"))
    for a in (0.0, math.pi, 0.6, -0.6):
        G.append((box(f"lead{a}", (0.07, 0.05, 0.07), (0.17 * math.cos(a), 0.17 * math.sin(a) * 0.82, 0.97), mats["boots"], rot=(0, 0, a)), "pelvis"))
    G.append((box("sheath", (0.035, 0.03, 0.2), (0.03, -0.19, 0.86), mats["leather"]), "pelvis"))
    G.append((box("knife_grip", (0.025, 0.025, 0.07), (0.03, -0.19, 0.99), mats["boots"]), "pelvis"))
    # lead-soled boots: heavy, square, brass toe-caps
    for s, sd in ((1, "L"), (-1, "R")):
        G.append((box(f"boot{sd}", (0.27, 0.15, 0.13), (0.06, s * 0.115, 0.065), mats["boots"], 0.025, segs=3), f"foot.{sd}"))
        G.append((box(f"sole{sd}", (0.29, 0.16, 0.035), (0.06, s * 0.115, 0.0175), mats["metal"], 0.008), f"foot.{sd}"))
        G.append((box(f"toecap{sd}", (0.06, 0.15, 0.1), (0.17, s * 0.115, 0.06), mats["accent"], 0.02), f"foot.{sd}"))
    return G


def gear_whaler(J, mats):
    G = []
    hc = Vector((0.0, 0, 1.625))
    # the smaller face-plate helmet (one oval front plate), worn under a hood drawn up over it
    helm = sphere("helmet", tuple(hc), 0.165, mats["hat"], 44, 28, (1.0, 0.95, 1.05))
    cut_ports(helm, [((1, 0, -0.05), 34)], hc); shell(helm, 0.008); G.append((helm, "chest"))
    G += [(o, "chest") for o in port("plate", tuple(hc + Vector((0.155, 0, -0.008))), (1, 0, -0.05), 0.095, mats, grille=False, ring_mat="metal")]
    hood = sphere("hood", tuple(hc + Vector((-0.015, 0, 0.012))), 0.19, mats["top"], 96, 64, (1.0, 1.0, 1.08))
    cut_ports(hood, [((1, 0, -0.1), 58)], hc); shell(hood, 0.012)
    for v in hood.data.vertices:   # the hood's peak drawn forward over the brow, its cape spread down the back
        if v.co.z > hc.z + 0.12:
            v.co.x += 0.03
        if v.co.x < -0.05 and v.co.z < hc.z - 0.06:
            v.co.z -= 0.06
    G.append((hood, "chest"))
    G.append((ring_at("collar", (0.0, 0, 1.45), (0, 0, 1), 0.155, 0.03, mats["metal"]), "chest"))
    # the long slicker: skirt to the knee, a storm flap, straps and toggles down the front
    G.append((K.lathe_z("slicker", [(0.27, -0.55), (0.21, -0.2), (0.165, 0.0)], (0.0, 0, 0.98), "top", mats, sx=0.86, sy=1.02), None))
    for k, z in enumerate((1.36, 1.22, 1.08, 0.94, 0.8)):
        G.append((box(f"toggle{k}", (0.02, 0.07, 0.016), (0.155 if z > 0.95 else 0.2, 0.0, z), mats["accent"], 0.006), "chest" if z > 0.95 else "pelvis"))
    # the rope belt and the harpoon-line coil slung on the left hip
    G.append((K.tube_ring("rope_belt", (0.0, 0, 0.97), 0.17, 0.018, "accent", mats, squash=0.84), "pelvis"))
    for k in range(3):
        G.append((ring_at(f"coil{k}", (-0.02, 0.21 + 0.012 * k, 0.88), (0, 1, 0), 0.11 - 0.006 * k, 0.012, mats["accent"]), "pelvis"))
    # tarred gloves (the body's hands are painted tar), high sea boots
    for s, sd in ((1, "L"), (-1, "R")):
        G.append((box(f"boot{sd}", (0.24, 0.13, 0.12), (0.05, s * 0.115, 0.06), mats["boots"], 0.03, segs=3), f"foot.{sd}"))
    return G


def gear_stowaway(J, mats):
    G = []
    hc = Vector((0.0, 0, 1.635))
    # a dented helmet in another metal, a big front port and one odd little copper port on the left
    helm = sphere("helmet", tuple(hc), 0.2, mats["hat"], 44, 28, (1.0, 0.98, 0.98))
    cut_ports(helm, [((1, 0, 0), 25), ((0.2, 1, 0.1), 12)], hc)
    for v in helm.data.vertices:   # a dent above the right port, and the crown knocked a little flat
        p = helm.matrix_world @ v.co
        d = (p - (hc + Vector((0.09, -0.12, 0.12)))).length
        if d < 0.08:
            v.co -= (p - hc).normalized() * (0.035 * (1 - d / 0.08))
        if p.z > hc.z + 0.17:
            v.co.z -= 0.012
    shell(helm, 0.01); G.append((helm, "chest"))
    G += [(o, "chest") for o in port("front", tuple(hc + Vector((0.195, 0, 0))), (1, 0, 0), 0.085, mats)]
    d = Vector((0.2, 1, 0.1)).normalized()
    G += [(o, "chest") for o in port("odd", tuple(hc + d * 0.196), tuple(d), 0.042, mats, grille=False, ring_mat="metal")]
    G.append((ring_at("neck_ring", (0.0, 0, 1.44), (0, 0, 1), 0.165, 0.022, mats["metal"]), "chest"))
    # a bottle swinging on a cord at the chest
    bottle = C.cylinder("bottle", 0.035, 0.12, loc=(0.2, 0.04, 1.2), verts=20, bevel=0.01); C.assign(bottle, mats["glass"]); C.apply_all(bottle); C.smooth(bottle, 40)
    G.append((bottle, "chest"))
    neck = C.cylinder("bottle_neck", 0.012, 0.05, loc=(0.2, 0.04, 1.285), verts=12, bevel=0.002); C.assign(neck, mats["glass"]); C.apply_all(neck)
    G.append((neck, "chest"))
    G.append((C.cylinder("cork", 0.011, 0.02, loc=(0.2, 0.04, 1.315), verts=12, bevel=0.002), "chest")); C.assign(G[-1][0], mats["leather"]); C.apply_all(G[-1][0])
    cord = C.tube_along("cord", [(0.2, 0.04, 1.32), (0.17, 0.09, 1.4), (0.08, 0.12, 1.47), (-0.04, 0.0, 1.49), (0.08, -0.12, 1.47), (0.17, -0.02, 1.38), (0.2, 0.03, 1.32)], 0.005)
    C.assign(cord, mats["accent"]); G.append((cord, "chest"))
    # a sack slung on the back, string-tied boots that don't match
    sack = sphere("sack", (-0.22, 0.03, 1.12), 0.13, mats["leather"], 28, 18, (0.75, 1.0, 1.35)); G.append((sack, "chest"))
    G.append((rod("sack_tie", (-0.2, 0.0, 1.29), (-0.18, 0.06, 1.33), 0.012, mats["accent"]), "chest"))
    for s, sd in ((1, "L"), (-1, "R")):
        G.append((box(f"boot{sd}", (0.25 if s > 0 else 0.23, 0.14, 0.12 if s > 0 else 0.14), (0.05, s * 0.115, 0.065), mats["boots"], 0.03, segs=3), f"foot.{sd}"))
        for z in (0.07, 0.11):
            G.append((ring_at(f"tie{sd}{z}", (0.06, s * 0.115, z), (1, 0, 0), 0.075, 0.006, mats["accent"]), f"foot.{sd}"))
    return G


def gear_mechanic(J, mats):
    G = []
    hc = Vector((0.0, 0, 1.64))
    # the domed hard-suit helmet with a grid of small ports
    helm = sphere("helmet", tuple(hc), 0.2, mats["hat"], 44, 28, (1.0, 1.0, 1.05))
    grid = []
    for row, z in enumerate((0.22, -0.12)):
        for col, y in enumerate((-0.5, 0.0, 0.5)):
            grid.append((Vector((1, y, z)).normalized(), 9))
    cut_ports(helm, [(tuple(g), a) for g, a in grid], hc); shell(helm, 0.012); G.append((helm, "chest"))
    for i, (g, a) in enumerate(grid):
        G += [(o, "chest") for o in port(f"p{i}", tuple(hc + g * 0.205), tuple(g), 0.03, mats, grille=False, ring_mat="accent")]
    for k in range(16):   # rivets round the helmet's base ring
        a = 2 * math.pi * k / 16
        G.append((sphere(f"hr{k}", (0.205 * math.cos(a), 0.205 * math.sin(a), 1.47), 0.012, mats["metal"], 10, 6), "chest"))
    # the riveted torso: a boxy cuirass front and back, a gauge pair and a valve wheel on the chest
    G.append((box("cuirass", (0.36, 0.44, 0.5), (0.0, 0, 1.22), mats["top"], 0.05, segs=3), "chest"))
    G.append((box("hips", (0.32, 0.38, 0.2), (0.0, 0, 0.95), mats["trousers"], 0.05, segs=3), "pelvis"))
    for k in range(7):
        for zz in (1.44, 0.99):
            G.append((sphere(f"rv{k}{zz}", (0.181, -0.18 + 0.06 * k, zz), 0.011, mats["metal"], 10, 6), "chest"))
    for s in (1, -1):
        g = C.cylinder(f"gauge{s}", 0.04, 0.025, loc=(0.19, s * 0.08, 1.3), rot=(0, math.pi / 2, 0), verts=24, bevel=0.003); C.assign(g, mats["accent"]); C.apply_all(g); G.append((g, "chest"))
        f = C.cylinder(f"face{s}", 0.032, 0.004, loc=(0.204, s * 0.08, 1.3), rot=(0, math.pi / 2, 0), verts=24, bevel=0); C.assign(f, mats["eye_white"]); C.apply_all(f); G.append((f, "chest"))
        G.append((rod(f"needle{s}", (0.207, s * 0.08, 1.3), (0.207, s * 0.08 + 0.02, 1.318), 0.003, mats["eye_dark"], 6), "chest"))
    G.append((ring_at("valve", (0.2, 0, 1.17), (1, 0, 0), 0.035, 0.007, mats["accent"]), "chest"))
    # articulated limbs: plated segments with ring joints at the shoulder, elbow, wrist, hip and knee
    def seg(name, a, b, r, bone):
        G.append((rod(name, J[a], J[b], r, mats["top"], 16), bone))
    for s, sd in ((1, "L"), (-1, "R")):
        G.append((sphere(f"shoulder{sd}", J[f"upperarm.{sd}"], 0.1, mats["top"], 24, 16), f"upperarm.{sd}"))
        seg(f"upper{sd}", f"upperarm.{sd}", f"forearm.{sd}", 0.075, f"upperarm.{sd}")
        seg(f"lower{sd}", f"forearm.{sd}", f"hand.{sd}", 0.066, f"forearm.{sd}")
        for j, bone, r in ((f"forearm.{sd}", f"forearm.{sd}", 0.08), (f"hand.{sd}", f"forearm.{sd}", 0.07)):
            a = Vector(J[j]); nb = Vector(J[f"hand.{sd}"]) - Vector(J[f"forearm.{sd}"])
            G.append((ring_at(f"ring{j}", tuple(a), tuple(nb), r, 0.016, mats["accent"]), bone))
        seg(f"thigh{sd}", f"thigh.{sd}", f"shin.{sd}", 0.105, f"thigh.{sd}")
        seg(f"shin{sd}", f"shin.{sd}", f"foot.{sd}", 0.085, f"shin.{sd}")
        G.append((ring_at(f"knee{sd}", J[f"shin.{sd}"], (0, 0, 1), 0.095, 0.02, mats["accent"]), f"shin.{sd}"))
        G.append((box(f"boot{sd}", (0.28, 0.17, 0.14), (0.06, s * 0.115, 0.07), mats["boots"], 0.03, segs=3), f"foot.{sd}"))
    # the tool roll at the back of the belt
    roll = C.cylinder("tool_roll", 0.05, 0.3, loc=(-0.19, 0, 0.98), rot=(math.pi / 2, 0, 0), verts=20, bevel=0.01); C.assign(roll, mats["leather"]); C.apply_all(roll); C.smooth(roll, 40)
    G.append((roll, "pelvis"))
    return G


GEAR = {"diver": gear_diver, "whaler": gear_whaler, "stowaway": gear_stowaway, "mechanic": gear_mechanic}


def paint(body, J, who, mats):
    """Which colour each face of the body wears: the suit (top above the belt, trousers below), the gloves, the face."""
    order = ["skin", "top", "trousers", "boots", "hat", "accent", "leather", "metal", "eye_white", "eye_dark", "glass"]
    for n in order:
        body.data.materials.append(mats[n])
    rng = random.Random(7)
    patches = [(Vector((rng.uniform(-0.15, 0.15), rng.uniform(-0.3, 0.3), rng.uniform(0.3, 1.4))), rng.uniform(0.06, 0.1)) for _ in range(9)]
    def nearest_bone(p):
        best, bd = None, 1e9
        for name, h, t, _ in K.bones():
            if not t:
                continue
            a, b = Vector(J[h]), Vector(J[t]); ab = b - a
            u = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.dot(ab), 1e-9)))
            d = (a + ab * u - p).length
            if d < bd:
                bd, best = d, name
        return best
    for f in body.data.polygons:
        c = f.center
        b = nearest_bone(c)
        if b in ("neck", "head"):
            m = "skin" if c.z > 1.5 else "top"
        elif any(b.startswith(k) for k in ("hand", "thumb", "index", "middle", "ring", "pinky")):
            m = "leather" if who in ("diver", "whaler") else "accent" if who == "stowaway" else "top"   # rubber gauntlets, tarred gloves, mismatched mitts, steel
        elif b.startswith(("foot", "toe")) or (b.startswith("shin") and c.z < 0.3):
            m = "boots"
        elif b.startswith(("thigh", "shin")) or (b == "pelvis" and c.z < 0.92):
            m = "trousers"
        else:
            m = "top"
        if who == "stowaway" and m in ("top", "trousers") and any((c - p).length < r for p, r in patches):
            m = "accent"   # the patches: another canvas sewn on
        f.material_index = order.index(m)


def build(who, out):
    C.reset()
    J = K.joints()
    mats = {}
    cols = dict(STYLE); cols.update(COLOURS[who])
    for name, (c, r) in cols.items():
        mats[name] = C.mat_flat(name, c, rough=r, metal=0.9 if name in METALLIC else 0.0)
    rig = K.make_armature(J)
    body = K.body_mesh(J, 1.12 if who in ("diver", "mechanic") else 1.04)
    paint(body, J, who, mats)
    head = K.head_parts(J, mats)
    for o in head:   # (as in crew.py: the skull and face parts rise with the head; the eyes and mouth already sit on their joints)
        if o.get("bone") == "head":
            o.location.z += K.HEAD_DZ
    gear = GEAR[who](J, mats)
    meshes = [body] + head
    for o, bone in gear:
        if bone:
            o["bone"] = bone
        meshes.append(o)
    for o in meshes:
        C.select_only([o]); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    soft = [o for o in meshes if "bone" not in o]
    rigid = [o for o in meshes if "bone" in o]
    for o in soft:
        C.select_only([o, rig]); bpy.context.view_layer.objects.active = rig
        bpy.ops.object.parent_set(type='ARMATURE_AUTO')
    for o in rigid:
        g = o.vertex_groups.new(name=o["bone"])
        g.add(list(range(len(o.data.vertices))), 1.0, 'REPLACE')
        mod = o.modifiers.new("rig", 'ARMATURE'); mod.object = rig
        o.parent = rig
    sc = bpy.context.scene
    sc.cycles.samples = 24
    sc.world = sc.world or bpy.data.worlds.new("world")
    for o in meshes:
        o.data.color_attributes.new("Col", 'BYTE_COLOR', 'CORNER')
        o.data.color_attributes.active_color = o.data.color_attributes["Col"]
    C.select_only(meshes)
    sc.render.bake.target = 'VERTEX_COLORS'
    bpy.ops.object.bake(type='AO')
    for o in meshes:
        ca = o.data.color_attributes["Col"]
        for d in ca.data:
            a = 0.55 + 0.45 * d.color[0]
            d.color = (a, a, a, 1.0)
    path = os.path.join(out, f"diver_{who}.glb")
    C.select_only(meshes + [rig])
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=False,
                              export_yup=True, export_texcoords=False, export_normals=True, export_skins=True,
                              export_animations=False, export_materials='EXPORT', export_vertex_color='ACTIVE',
                              export_all_influences=False)
    print("artgen: wrote", path)


a = C.args()
only = a[a.index("--diver") + 1] if "--diver" in a else None
out = C.out_dir()
for w in DIVERS:
    if only and w != only:
        continue
    build(w, out)
