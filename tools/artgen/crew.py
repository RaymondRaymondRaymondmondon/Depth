# The Trawl's crew (Visual Overhaul phase 3, in the style of the user's reference: smooth, chunky, rounded stylised
# people with soft shading, a bald egg of a head with simple eyes, oversized hands with real fingers).
#
# One skinned body per role on one shared skeleton (Red Tide can use it too): the body is a single continuous mesh
# grown from a stick skeleton by Blender's Skin modifier and smoothed, skinned to the rig with automatic weights; the
# head (an egg with a nose, ears, white eyes with dark pupils), the role's clothes and its silhouette pieces (cap,
# sou'wester, apron, coat skirt, satchel, spectacles, lamp) are separate meshes on the same rig. Colours are flat
# named materials (skin, top, trousers, boots, hat, accent, leather, metal, eye_white, eye_dark) so the game recolours
# each sailor's skin tone and outfit at runtime; ambient occlusion is baked into the vertex colour.
#
# Frame: Blender +X forward, +Z up, +Y the figure's left; feet at z = 0, about 1.78 m to the crown.
#     blender -b --factory-startup -P tools/artgen/crew.py -- --out assets/shared/crew [--role bosun]

import os
import sys
import math
import bmesh
from mathutils import Vector
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import common as C
bpy = C.bpy

ROLES = ["bosun", "angler", "diver", "medic"]

# ---------------------------------------------------------------- the skeleton (A-pose: arms 45 degrees down)
def joints():
    J = {
        "pelvis": (0.0, 0, 0.95), "spine": (0.0, 0, 1.10), "chest": (0.0, 0, 1.28), "neck": (0.0, 0, 1.47),
        "head": (0.0, 0, 1.53), "crown": (0.0, 0, 1.80),
    }
    for s, sd in ((1, "L"), (-1, "R")):
        J[f"clavicle.{sd}"] = (0.0, s * 0.05, 1.43)
        J[f"upperarm.{sd}"] = (0.0, s * 0.19, 1.41)
        J[f"forearm.{sd}"] = (0.0, s * 0.355, 1.215)
        J[f"hand.{sd}"] = (0.01, s * 0.505, 1.04)
        J[f"palm.{sd}"] = (0.012, s * 0.56, 0.975)
        # the fingers carry on down the line of the arm, spread across it (forward and back)
        d = Vector((0, s * 0.62, -0.78)).normalized()
        for name, off, lens in (("index", 0.034, (0.038, 0.028, 0.023)), ("middle", 0.011, (0.042, 0.03, 0.024)),
                                ("ring", -0.012, (0.038, 0.028, 0.023)), ("pinky", -0.033, (0.031, 0.022, 0.019))):
            p = Vector(J[f"palm.{sd}"]) + Vector((off, 0, 0)) + d * 0.004
            for k in range(3):
                J[f"{name}{k + 1}.{sd}"] = tuple(p)
                p = p + d * lens[k] + Vector((off * 0.08, 0, 0))
            J[f"{name}_tip.{sd}"] = tuple(p)
        t = Vector(J[f"hand.{sd}"]) + Vector((0.03, s * 0.02, -0.03))
        td = Vector((0.6, s * 0.35, -0.72)).normalized()
        for k, ln in enumerate((0.03, 0.026, 0.022)):
            J[f"thumb{k + 1}.{sd}"] = tuple(t)
            t = t + td * ln
        J[f"thumb_tip.{sd}"] = tuple(t)
        J[f"thigh.{sd}"] = (0.0, s * 0.1, 0.92)
        J[f"shin.{sd}"] = (0.015, s * 0.11, 0.5)
        J[f"foot.{sd}"] = (0.0, s * 0.115, 0.1)
        J[f"toe.{sd}"] = (0.13, s * 0.115, 0.045)
        J[f"toe_tip.{sd}"] = (0.2, s * 0.115, 0.04)
        J[f"eye.{sd}"] = (0.09, s * 0.036, 1.67)
    return J

# bone: (head joint, tail joint, parent)
def bones():
    B = [("pelvis", "pelvis", "spine", None), ("spine", "spine", "chest", "pelvis"), ("chest", "chest", "neck", "spine"),
         ("neck", "neck", "head", "chest"), ("head", "head", "crown", "neck")]
    for sd in ("L", "R"):
        B += [(f"clavicle.{sd}", f"clavicle.{sd}", f"upperarm.{sd}", "chest"),
              (f"upperarm.{sd}", f"upperarm.{sd}", f"forearm.{sd}", f"clavicle.{sd}"),
              (f"forearm.{sd}", f"forearm.{sd}", f"hand.{sd}", f"upperarm.{sd}"),
              (f"hand.{sd}", f"hand.{sd}", f"palm.{sd}", f"forearm.{sd}")]
        for f in ("thumb", "index", "middle", "ring", "pinky"):
            for k in range(3):
                tail = f"{f}{k + 2}.{sd}" if k < 2 else f"{f}_tip.{sd}"
                B.append((f"{f}{k + 1}.{sd}", f"{f}{k + 1}.{sd}", tail, f"hand.{sd}" if k == 0 else f"{f}{k}.{sd}"))
        B += [(f"thigh.{sd}", f"thigh.{sd}", f"shin.{sd}", "pelvis"), (f"shin.{sd}", f"shin.{sd}", f"foot.{sd}", f"thigh.{sd}"),
              (f"foot.{sd}", f"foot.{sd}", f"toe.{sd}", f"shin.{sd}"), (f"toe.{sd}", f"toe.{sd}", f"toe_tip.{sd}", f"foot.{sd}"),
              (f"eye.{sd}", f"eye.{sd}", None, "head")]
    return B


def make_armature(J):
    arm = bpy.data.armatures.new("crew_rig")
    ob = C.link(bpy.data.objects.new("crew_rig", arm))
    C.select_only([ob])
    bpy.ops.object.mode_set(mode='EDIT')
    eb = arm.edit_bones
    for name, h, t, parent in bones():
        b = eb.new(name)
        b.head = J[h]
        b.tail = J[t] if t else tuple(Vector(J[h]) + Vector((0.02, 0, 0)))
        if (Vector(b.tail) - Vector(b.head)).length < 1e-4:
            b.tail = tuple(Vector(b.head) + Vector((0, 0, 0.05)))
        if parent:
            b.parent = eb[parent]
        b.use_deform = True
    bpy.ops.object.mode_set(mode='OBJECT')
    return ob


# ---------------------------------------------------------------- the body: a skin-modifier mesh round the skeleton
def body_mesh(J, build=1.0):
    pts, rad, edges = [], [], []
    idx = {}
    def P(name, r, pos=None):
        idx[name] = len(pts)
        pts.append(pos if pos else J[name])
        rad.append(r if isinstance(r, tuple) else (r, r))
        return idx[name]
    def E(a, b, n=1, r0=None, r1=None):
        # n - 1 extra points between joints a and b (smoother bends)
        ia, ib = idx[a], idx[b]
        prev = ia
        for k in range(1, n):
            t = k / n
            p = Vector(pts[ia]).lerp(Vector(pts[ib]), t)
            ra, rb = rad[ia], rad[ib]
            pts.append(tuple(p)); rad.append((ra[0] + (rb[0] - ra[0]) * t, ra[1] + (rb[1] - ra[1]) * t))
            edges.append((prev, len(pts) - 1)); prev = len(pts) - 1
        edges.append((prev, ib))
    P("pelvis", (0.15 * build, 0.15 * build)); P("spine", (0.135 * build, 0.135 * build)); P("chest", (0.165 * build, 0.165 * build))
    P("neck", 0.064); P("head", 0.062); P("headin", 0.05, (0.0, 0, 1.6))   # (the neck runs up inside the skull: no seam)
    E("pelvis", "spine", 2); E("spine", "chest", 2); E("chest", "neck", 2); E("neck", "head"); E("head", "headin")
    for sd in ("L", "R"):
        P(f"clavicle.{sd}", 0.085); P(f"upperarm.{sd}", 0.074); P(f"forearm.{sd}", 0.058); P(f"hand.{sd}", 0.043); P(f"palm.{sd}", (0.054, 0.028))
        E("chest", f"clavicle.{sd}"); E(f"clavicle.{sd}", f"upperarm.{sd}")
        E(f"upperarm.{sd}", f"forearm.{sd}", 3); E(f"forearm.{sd}", f"hand.{sd}", 3); E(f"hand.{sd}", f"palm.{sd}")
        for f, r in (("thumb", 0.019), ("index", 0.0165), ("middle", 0.017), ("ring", 0.016), ("pinky", 0.0142)):
            for k in range(3):
                P(f"{f}{k + 1}.{sd}", r * (1 - 0.1 * k))
            P(f"{f}_tip.{sd}", r * 0.78)
            E(f"hand.{sd}" if f == "thumb" else f"palm.{sd}", f"{f}1.{sd}")
            E(f"{f}1.{sd}", f"{f}2.{sd}"); E(f"{f}2.{sd}", f"{f}3.{sd}"); E(f"{f}3.{sd}", f"{f}_tip.{sd}")
        P(f"thigh.{sd}", 0.1 * build); P(f"shin.{sd}", 0.072); P(f"foot.{sd}", 0.062); P(f"toe.{sd}", (0.05, 0.045)); P(f"toe_tip.{sd}", 0.04)
        E("pelvis", f"thigh.{sd}"); E(f"thigh.{sd}", f"shin.{sd}", 4); E(f"shin.{sd}", f"foot.{sd}", 4); E(f"foot.{sd}", f"toe.{sd}"); E(f"toe.{sd}", f"toe_tip.{sd}")
    me = bpy.data.meshes.new("body")
    me.from_pydata(pts, edges, [])
    ob = C.link(bpy.data.objects.new("body", me))
    sk = ob.modifiers.new("skin", 'SKIN')
    sv = me.skin_vertices[0].data
    for i, r in enumerate(rad):
        sv[i].radius = r
    sv[idx["pelvis"]].use_root = True
    sub = ob.modifiers.new("sub", 'SUBSURF'); sub.levels = 2
    C.select_only([ob])
    bpy.ops.object.modifier_apply(modifier="skin")
    bpy.ops.object.modifier_apply(modifier="sub")
    # the torso flatter front to back, a chest and a seat; the boots squarer underfoot
    for v in me.vertices:
        x, y, z = v.co
        if 0.86 < z < 1.5 and abs(y) < 0.2:
            v.co.x = x * 0.78 + 0.012 * max(0.0, 1 - abs(z - 1.28) / 0.12)
        if z < 0.03:
            v.co.z = 0.0
    C.smooth(ob, 180)
    return ob


# ---------------------------------------------------------------- the head
def head_parts(J, mats):
    parts = []
    def sphere(name, loc, scale, mat, segs=32, rings=20):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=segs, ring_count=rings, radius=1.0, location=loc)
        o = bpy.context.active_object; o.name = name; o.scale = scale
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        C.assign(o, mats[mat]); C.smooth(o, 180)
        parts.append(o); return o
    hd = sphere("skull", (0.0, 0, 1.655), (0.108, 0.098, 0.13), "skin", 48, 32)
    # an egg: fuller at the crown and the back, the jaw tucked in toward the chin
    for v in hd.data.vertices:
        z = v.co.z / 0.13   # (the sphere's own coordinates: its centre is the object's location)
        if z < 0:
            v.co.y *= 1 - 0.18 * z * z
            v.co.x = v.co.x * (1 - 0.08 * z * z) + 0.008 * (-z)
        else:
            v.co.x -= 0.01 * z
    sphere("nose", (0.103, 0, 1.635), (0.019, 0.015, 0.021), "skin")
    for s in (1, -1):
        e = sphere(f"ear{s}", (-0.004, s * 0.097, 1.645), (0.014, 0.012, 0.03), "skin", 20, 12)
    for s, sd in ((1, "L"), (-1, "R")):
        ex, ey, ez = J[f"eye.{sd}"]
        w = sphere(f"eye_white.{sd}", (ex, ey, ez), (0.008, 0.019, 0.022), "eye_white", 24, 16)
        p = sphere(f"eye_dark.{sd}", (ex + 0.0075, ey * 0.97, ez - 0.002), (0.004, 0.0095, 0.0105), "eye_dark", 16, 12)
        w["bone"] = f"eye.{sd}"; p["bone"] = f"eye.{sd}"
    for o in parts:
        if "bone" not in o:
            o["bone"] = "head"
    return parts


# ---------------------------------------------------------------- clothes and the role's pieces
def tube_ring(name, centre, r_major, r_minor, mat, mats, axis='Z', squash=1.0):
    bpy.ops.mesh.primitive_torus_add(major_radius=r_major, minor_radius=r_minor, location=centre, major_segments=32, minor_segments=10)
    o = bpy.context.active_object; o.name = name
    if axis == 'Y':
        o.rotation_euler = (math.pi / 2, 0, 0)
    if axis == 'X':
        o.rotation_euler = (0, math.pi / 2, 0)
    if axis == 'ARM':   # square to the left upper arm in the A-pose
        o.rotation_euler = (math.radians(40), 0, 0)
    o.scale = (1, squash, 1)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    C.assign(o, mats[mat]); C.smooth(o, 180)
    return o


def lathe_z(name, profile, centre, mat, mats, segs=40, sx=1.0, sy=1.0, open_bottom=True):
    """A solid of revolution about Z: profile [(radius, z), ...] bottom to top."""
    bm = bmesh.new()
    rings = []
    for (r, z) in profile:
        ring = [bm.verts.new((centre[0] + r * sx * math.cos(2 * math.pi * k / segs), centre[1] + r * sy * math.sin(2 * math.pi * k / segs), centre[2] + z)) for k in range(segs)]
        rings.append(ring)
    for i in range(len(rings) - 1):
        for k in range(segs):
            k1 = (k + 1) % segs
            bm.faces.new((rings[i][k], rings[i][k1], rings[i + 1][k1], rings[i + 1][k]))
    if not open_bottom:
        bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = C.link(bpy.data.objects.new(name, me))
    sol = o.modifiers.new("thick", 'SOLIDIFY'); sol.thickness = 0.006; sol.offset = 1
    sub = o.modifiers.new("sub", 'SUBSURF'); sub.levels = 1
    C.select_only([o]); bpy.ops.object.modifier_apply(modifier="thick"); bpy.ops.object.modifier_apply(modifier="sub")
    C.assign(o, mats[mat]); C.smooth(o, 180)
    return o


def role_pieces(role, J, mats):
    """The pieces that make each role's silhouette (spec, 'Clothing by role'), with the bone each rides on (None:
    weighted automatically, for clothes that bend with the body)."""
    out = []
    def rigid(o, bone="head"):
        o["bone"] = bone; out.append(o); return o
    def soft(o):
        out.append(o); return o
    if role == "bosun":
        # a short peaked cap, a leather apron, a patch kit on the belt
        rigid(lathe_z("cap", [(0.102, 0.0), (0.104, 0.03), (0.098, 0.06), (0.0, 0.066)], (-0.004, 0, 1.735), "hat", mats, sx=1.05))
        visor = C.bevelled_box("visor", (0.07, 0.15, 0.008), loc=(0.11, 0, 1.738), bevel=0.003, rot=(0, math.radians(14), 0))
        C.assign(visor, mats["leather"]); C.apply_all(visor); C.smooth(visor, 40); rigid(visor)
        apron = C.lofted("apron", [(0.0, 0.0, 0.17, 0.004), (0.0, 0.0, 0.17, 0.004)], segs=8)
        C.bpy.data.objects.remove(apron)
        ap = C.bevelled_box("apron", (0.014, 0.3, 0.78), loc=(0.0, 0, 0.86), bevel=0.005, segments=2)
        ss = ap.modifiers.new("sub", 'SUBSURF'); ss.levels = 2
        C.assign(ap, mats["leather"]); C.apply_all(ap)
        for v in ap.data.vertices:   # a bib apron: wrapped round the body's front, narrowing to the bib at the chest
            z = v.co.z + 0.86   # (the object sits at its location: the mesh's own z is about its middle)
            half = 0.15 if z < 1.12 else 0.15 - (z - 1.12) * 0.55
            v.co.y = max(-half, min(half, v.co.y))
            bodyx = 0.135 if z > 1.0 else 0.125 + 0.05 * max(0.0, 0.95 - z)
            v.co.x += bodyx + 0.018 - 3.2 * (v.co.y ** 2) * (1.0 if z > 0.92 else 0.6)
        C.smooth(ap, 40); soft(ap)
        kit = C.bevelled_box("patch_kit", (0.06, 0.04, 0.05), loc=(0.02, -0.17, 0.9), bevel=0.008); C.assign(kit, mats["accent"]); C.apply_all(kit); rigid(kit, "pelvis")
    elif role == "angler":
        # the wide sou'wester, long at the back; a bait tin at the hip
        crown = rigid(lathe_z("souwester", [(0.105, 0.0), (0.104, 0.04), (0.09, 0.085), (0.0, 0.1)], (-0.01, 0, 1.715), "hat", mats, sx=1.05))
        brim = lathe_z("brim", [(0.105, 0.02), (0.2, -0.005), (0.215, -0.03)], (-0.03, 0, 1.715), "hat", mats, sx=1.15, sy=1.05)
        for v in brim.data.vertices:   # long at the back to shed the rain
            if v.co.x < -0.05:
                v.co.z -= 0.25 * (-0.05 - v.co.x)
        rigid(brim)
        tin = C.cylinder("bait_tin", 0.035, 0.05, loc=(0.0, 0.18, 0.88), verts=24, bevel=0.004); C.assign(tin, mats["metal"]); C.apply_all(tin); C.smooth(tin, 40); rigid(tin, "pelvis")
    elif role == "diver":
        # the diving dress rolled down to the waist, a lamp on a headband, a knife strapped to the calf
        soft(tube_ring("dress_roll", (0.0, 0, 0.98), 0.155, 0.045, "trousers", mats, squash=0.8))
        rigid(tube_ring("headband", (0.0, 0, 1.72), 0.1, 0.012, "leather", mats, squash=0.92))
        lamp = C.cylinder("lamp", 0.028, 0.045, loc=(0.115, 0, 1.73), rot=(0, math.pi / 2, 0), verts=24, bevel=0.004)
        C.assign(lamp, mats["metal"]); C.apply_all(lamp); C.smooth(lamp, 40); rigid(lamp)
        glass = C.cylinder("lamp_glass", 0.022, 0.006, loc=(0.14, 0, 1.73), rot=(0, math.pi / 2, 0), verts=24, bevel=0.001)
        C.assign(glass, mats["eye_white"]); C.apply_all(glass); rigid(glass)
        knife = C.bevelled_box("calf_knife", (0.03, 0.02, 0.16), loc=(0.0, 0.17, 0.33), bevel=0.006); C.assign(knife, mats["leather"]); C.apply_all(knife); rigid(knife, "shin.L")
    elif role == "medic":
        # a long waxed coat to the knee, round spectacles, a red-cross armband, a satchel across the body
        soft(lathe_z("coat_skirt", [(0.24, -0.42), (0.19, -0.1), (0.155, 0.0)], (0.0, 0, 0.98), "top", mats, sx=0.86, sy=1.0))
        for s in (1, -1):
            rigid(tube_ring(f"lens{s}", (0.108, s * 0.036, 1.668), 0.017, 0.0022, "metal", mats, axis='X'))
        bridge = C.cylinder("bridge", 0.002, 0.04, loc=(0.112, 0, 1.672), rot=(math.pi / 2, 0, 0), verts=8, bevel=0); C.assign(bridge, mats["metal"]); C.apply_all(bridge); rigid(bridge)
        band = tube_ring("armband", (0, 0, 0), 0.058, 0.014, "accent", mats, axis='ARM')
        mid = Vector(J["upperarm.L"]).lerp(Vector(J["forearm.L"]), 0.45)
        band.location = mid; C.select_only([band]); bpy.ops.object.transform_apply(location=True)
        rigid(band, "upperarm.L")
        bag = C.bevelled_box("satchel", (0.08, 0.06, 0.18), loc=(0.02, -0.2, 0.86), bevel=0.02, segments=3); C.assign(bag, mats["leather"]); C.apply_all(bag); C.smooth(bag, 40); rigid(bag, "pelvis")
        strap = C.tube_along("strap", [(0.13, -0.19, 0.95), (0.15, -0.05, 1.15), (0.13, 0.12, 1.38), (0.0, 0.15, 1.44), (-0.12, 0.08, 1.3), (-0.1, -0.1, 1.05), (-0.02, -0.2, 0.95)], 0.012)
        C.assign(strap, mats["leather"]); C.smooth(strap, 180); soft(strap)
    return out


# ---------------------------------------------------------------- body regions: which colour each face of the body wears
def paint_body(body, J, role, mats):
    order = ["skin", "top", "trousers", "boots", "hat", "accent", "leather", "metal", "eye_white", "eye_dark"]
    for n in order:
        body.data.materials.append(mats[n])
    segs = [(a, b) for (_, a, b, _) in bones() if b]
    def nearest_bone(p):
        best, bd = None, 1e9
        for name, h, t, _ in bones():
            if not t:
                continue
            a, b = Vector(J[h]), Vector(J[t])
            ab = b - a
            u = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.dot(ab), 1e-9)))
            d = (a + ab * u - p).length
            if d < bd:
                bd, best = d, name
        return best
    rolled = role in ("bosun",)
    for f in body.data.polygons:
        c = f.center
        b = nearest_bone(c)
        if b in ("neck", "head") or any(b.startswith(k) for k in ("hand", "thumb", "index", "middle", "ring", "pinky")):
            m = "skin"
        elif b.startswith("forearm") and rolled and c.z < 1.17:
            m = "skin"
        elif b.startswith(("foot", "toe")) or (b.startswith("shin") and c.z < 0.3):
            m = "boots"
        elif b.startswith(("thigh", "shin")) or (b == "pelvis" and c.z < 0.92):
            m = "trousers"
        else:
            m = "top"
        f.material_index = order.index(m)


# ---------------------------------------------------------------- build one role
STYLE = {   # flat colours (linear-ish sRGB) and roughness; the game recolours skin, top, trousers and hat per sailor
    "skin": ((0.78, 0.55, 0.42), 0.6), "boots": ((0.06, 0.055, 0.05), 0.45), "leather": ((0.33, 0.2, 0.11), 0.6),
    "metal": ((0.62, 0.48, 0.25), 0.35), "eye_white": ((0.92, 0.91, 0.88), 0.2), "eye_dark": ((0.03, 0.03, 0.035), 0.15),
}
ROLE_COLOURS = {
    "bosun": {"top": ((0.11, 0.15, 0.27), 0.9), "trousers": ((0.42, 0.37, 0.27), 0.85), "hat": ((0.09, 0.11, 0.18), 0.8), "accent": ((0.5, 0.12, 0.08), 0.7)},
    "angler": {"top": ((0.85, 0.62, 0.12), 0.38), "trousers": ((0.85, 0.62, 0.12), 0.38), "hat": ((0.85, 0.62, 0.12), 0.38), "accent": ((0.2, 0.2, 0.2), 0.5)},
    "diver": {"top": ((0.3, 0.33, 0.28), 0.95), "trousers": ((0.55, 0.5, 0.38), 0.85), "hat": ((0.3, 0.33, 0.28), 0.95), "accent": ((0.6, 0.5, 0.3), 0.6)},
    "medic": {"top": ((0.24, 0.22, 0.15), 0.55), "trousers": ((0.18, 0.17, 0.15), 0.8), "hat": ((0.24, 0.22, 0.15), 0.55), "accent": ((0.75, 0.08, 0.06), 0.6)},
}


def build(role, out):
    C.reset()
    J = joints()
    mats = {}
    cols = dict(STYLE); cols.update(ROLE_COLOURS[role])
    for name, (c, r) in cols.items():
        mats[name] = C.mat_flat(name, c, rough=r, metal=0.9 if name == "metal" else 0.0)
    rig = make_armature(J)
    body = body_mesh(J, 1.08 if role == "bosun" else 1.0)
    paint_body(body, J, role, mats)
    meshes = [body] + head_parts(J, mats) + role_pieces(role, J, mats)
    # skin everything to the rig: the body and soft clothes by automatic weights, the rest wholly to one bone
    for o in meshes:
        C.select_only([o])
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
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
    # ambient occlusion into the vertex colour (the creases, under the hat brim, between the fingers)
    sc = bpy.context.scene
    sc.cycles.samples = 24
    sc.world = sc.world or bpy.data.worlds.new("world")
    for o in meshes:
        o.data.color_attributes.new("Col", 'BYTE_COLOR', 'CORNER')
        o.data.color_attributes.active_color = o.data.color_attributes["Col"]
    C.select_only(meshes)
    sc.render.bake.target = 'VERTEX_COLORS'
    bpy.ops.object.bake(type='AO')
    # soften the occlusion: a stylised figure wants a hint of it, not soot
    for o in meshes:
        ca = o.data.color_attributes["Col"]
        for d in ca.data:
            a = 0.55 + 0.45 * d.color[0]
            d.color = (a, a, a, 1.0)
    path = os.path.join(out, f"crew_{role}.glb")
    C.select_only(meshes + [rig])
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=False,
                              export_yup=True, export_texcoords=False, export_normals=True, export_skins=True,
                              export_animations=False, export_materials='EXPORT', export_vertex_color='ACTIVE',
                              export_all_influences=False)
    print("artgen: wrote", path)


a = C.args()
only = a[a.index("--role") + 1] if "--role" in a else None
out = C.out_dir()
for r in ROLES:
    if only and r != only:
        continue
    build(r, out)
