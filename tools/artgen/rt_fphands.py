# Red Tide's first-person hands (the viewmodel): a dedicated pair of gloved hands and sleeves for each diver, in
# place of the whole body with its head cut off (whose big stylised mitts, reached onto the gun by IK from the
# shoulders, read badly at the bottom of the screen). Like any shooter's viewmodel: a right hand closed round a pistol
# grip with the index on the trigger, a left hand cupping a fore-end from below, a mirrored left grip hand for a pistol
# held in two hands or a second pistol, and for each arm a gauntlet cuff and a sleeve. Each part is rigid on its own
# bone and built at the origin, so the game places it with one matrix (skinned with DrawPbrSkinned for the recolours):
#     hand.R   the grip hand: the grip's axis is local Z through the origin (an ellipse, half-depth GA along X, half-width
#              GB along Y), the fingers wrap from the right (-Y) round the front, the thumb over the left side
#     grip.L   hand.R mirrored (Y -> -Y)
#     hand.L   the support hand: the fore-end is local X through the origin (radius FR), cupped from below-left
#     cuff.R/L the gauntlet: from the wrist (origin) toward the elbow (+X), CUFF_LEN long
#     arm.R/L  the sleeve: from the wrist (origin) to +X 1.0 (the game stretches it to the elbow)
# Wrists (where the cuff starts), in this frame: WRIST_R, WRIST_GL, WRIST_L below; src/redtide_vis.cpp mirrors them.
# Materials by the diver figure's names (rt_divers.py), so the Locker's skins and the Wardrobe recolour them alike:
# the sleeve is "top", the cuff's ring "accent", the glove the diver's own (rubber gauntlets: leather; tarred gloves:
# leather; mismatched mitts: accent; steel: top).
#     blender -b --factory-startup -P tools/artgen/rt_fphands.py -- --out assets/shared/divers

import os
import sys
import math
import bmesh
from mathutils import Vector
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import common as C
bpy = C.bpy

# (the divers' colours, as rt_divers.py; importing it would build the divers)
DIVERS = ["diver", "whaler", "stowaway", "mechanic", "sailor"]   # (sailor: the Trawl crew's bare hands, rolled sleeves)
STYLE = {"skin": ((0.78, 0.55, 0.42), 0.6), "leather": ((0.3, 0.19, 0.1), 0.65), "metal": ((0.7, 0.52, 0.24), 0.32)}
COLOURS = {
    "diver":    {"top": ((0.62, 0.55, 0.4), 0.9), "accent": ((0.82, 0.62, 0.28), 0.3), "leather": ((0.055, 0.05, 0.045), 0.55)},
    "whaler":   {"top": ((0.2, 0.22, 0.2), 0.35), "accent": ((0.42, 0.33, 0.2), 0.7), "leather": ((0.025, 0.022, 0.02), 0.4)},
    "stowaway": {"top": ((0.55, 0.47, 0.36), 0.92), "accent": ((0.42, 0.5, 0.56), 0.85)},
    "mechanic": {"top": ((0.46, 0.47, 0.48), 0.42), "accent": ((0.76, 0.56, 0.26), 0.3)},
    "sailor":   {"top": ((0.2, 0.22, 0.3), 0.85), "accent": ((0.2, 0.22, 0.3), 0.85)},
}
METALLIC = {"metal", "accent"}


def ring_at(name, centre, axis, r_major, r_minor, mat):
    bpy.ops.mesh.primitive_torus_add(major_radius=r_major, minor_radius=r_minor, location=centre, major_segments=24, minor_segments=8)
    o = bpy.context.active_object; o.name = name
    o.rotation_euler = Vector((0, 0, 1)).rotation_difference(Vector(axis).normalized()).to_euler()
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    C.assign(o, mat); C.smooth(o, 180)
    return o

GA, GB = 0.025, 0.015          # the pistol grip's half-depth and half-width the right hand closes on
FR = 0.018                     # the fore-end's radius the left hand cups
WRIST_R = (-0.072, -0.024, -0.006)
WRIST_L = (-0.03, 0.036, -0.066)
CUFF_LEN = 0.1
GLOVE = {"diver": "leather", "whaler": "leather", "stowaway": "accent", "mechanic": "top", "sailor": "skin"}


def capsule_chain(name, pts, radii, verts=10):
    """A finger: spheres at the joints and tapered cylinders between them, one mesh."""
    bm = bmesh.new()
    for i, p in enumerate(pts):
        bmesh.ops.create_uvsphere(bm, u_segments=verts, v_segments=max(6, verts * 2 // 3), radius=radii[i],
                                  matrix=__import__("mathutils").Matrix.Translation(Vector(p)))
    for i in range(len(pts) - 1):
        a, b = Vector(pts[i]), Vector(pts[i + 1])
        d = b - a
        q = Vector((0, 0, 1)).rotation_difference(d.normalized())
        m = __import__("mathutils").Matrix.Translation((a + b) / 2) @ q.to_matrix().to_4x4()
        bmesh.ops.create_cone(bm, cap_ends=False, segments=verts, radius1=radii[i], radius2=radii[i + 1], depth=d.length, matrix=m)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = C.link(bpy.data.objects.new(name, me))
    return o


def hull(name, pts):
    """A convex solid through points (a palm, the back of a hand): low-poly planes, like a viewmodel's."""
    bm = bmesh.new()
    vs = [bm.verts.new(Vector(p)) for p in pts]
    bmesh.ops.convex_hull(bm, input=vs)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = C.link(bpy.data.objects.new(name, me))
    return o


def ring_pts(c, axis, r, n=8):
    c, axis = Vector(c), Vector(axis).normalized()
    u = axis.cross(Vector((0, 0, 1)) if abs(axis.z) < 0.9 else Vector((1, 0, 0))).normalized()
    v = axis.cross(u)
    return [tuple(c + (u * math.cos(2 * math.pi * k / n) + v * math.sin(2 * math.pi * k / n)) * r) for k in range(n)]


def wrap_pt(theta_deg, out, z):
    """A point round the grip's ellipse (theta 0 at the front, -90 the right side), `out` beyond its surface."""
    t = math.radians(theta_deg)
    return ((GA + out) * math.cos(t), (GB + out) * math.sin(t), z)


def grip_hand(mat):
    """The right hand closed round a pistol grip (local Z), the index finger curled on the trigger ahead and above."""
    parts = []
    fr = 0.0092   # a gloved finger's radius
    # the three lower fingers wrap the grip: knuckle on the right, round the front, the tips on the left side
    for k, (z, s) in enumerate(((0.006, 1.0), (-0.015, 0.97), (-0.034, 0.86))):
        pts = [(0.006, -0.031, z), wrap_pt(-42, fr * s, z), wrap_pt(4, fr * s, z - 0.001), wrap_pt(52, fr * s * 0.9, z - 0.002)]
        parts.append(capsule_chain(f"finger{k}", pts, [fr * s * 1.05, fr * s, fr * s * 0.95, fr * s * 0.88]))
    # the index on the trigger (forward of the grip, at the frame's height)
    parts.append(capsule_chain("index", [(0.008, -0.03, 0.027), (0.032, -0.024, 0.031), (0.05, -0.013, 0.027), (0.054, 0.0, 0.019)],
                               [fr * 1.05, fr, fr * 0.95, fr * 0.88]))
    # the thumb from the web of the hand over the left side of the frame
    parts.append(capsule_chain("thumb", [(-0.032, 0.004, 0.02), (-0.014, 0.024, 0.027), (0.008, 0.029, 0.026), (0.026, 0.024, 0.022)],
                               [0.0125, 0.0115, 0.0105, 0.0095]))
    # the palm and the back of the hand: from the knuckle row on the right round the back strap to the wrist
    pts = []
    for z in (0.036, 0.016, -0.004, -0.024, -0.044):
        pts += [(0.004, -0.024, z), (0.01, -0.042, z * 0.95)]
    pts += [(-0.03, -0.006, 0.026), (-0.036, 0.006, 0.012), (-0.037, 0.0, -0.03), (-0.03, -0.014, -0.048),
            (-0.046, -0.036, 0.022), (-0.05, -0.036, -0.03)]
    pts += ring_pts(WRIST_R, (1, 0.15, 0), 0.024, 6)
    parts.append(hull("palm", pts))
    for o in parts:
        C.assign(o, mat); C.smooth(o, 50)
    return parts


def support_hand(mat):
    """The left hand cupping a fore-end (local X) from below and the left, the fingers up its right side."""
    parts = []
    fr = 0.0092
    def around(phi_deg, out, x):   # phi from the left (+Y) toward the top (+Z); -90 below, -180 the right side
        p = math.radians(phi_deg)
        return (x, (FR + out) * math.cos(p), (FR + out) * math.sin(p))
    for k, (x, s) in enumerate(((0.032, 1.0), (0.011, 1.02), (-0.01, 0.97), (-0.029, 0.86))):
        pts = [around(-92, 0.02, x - 0.006), around(-140, fr * s, x), around(-182, fr * s, x + 0.002), around(-222, fr * s * 0.9, x + 0.003)]
        parts.append(capsule_chain(f"finger{k}", pts, [fr * s * 1.05, fr * s, fr * s * 0.95, fr * s * 0.88]))
    parts.append(capsule_chain("thumb", [(-0.026, 0.034, -0.03), (-0.004, 0.034, -0.008), (0.02, 0.029, 0.004), (0.04, 0.022, 0.01)],
                               [0.0125, 0.0115, 0.0105, 0.0095]))
    pts = []
    for x in (0.046, 0.024, 0.0, -0.022, -0.042):
        pts += [around(-95, 0.012, x), around(-90, 0.03, x)]
    pts += [(-0.03, 0.03, -0.02), (-0.006, 0.03, -0.012), (0.03, 0.03, -0.012), (0.03, 0.012, -0.05), (-0.03, 0.012, -0.055)]
    pts += ring_pts(WRIST_L, (-0.4, 0.5, -0.75), 0.024, 6)
    parts.append(hull("palm", pts))
    for o in parts:
        C.assign(o, mat); C.smooth(o, 50)
    return parts


def cuff(mats, glove, sailor=False):
    """The gauntlet from the wrist toward the elbow, flaring, with a ring at its mouth."""
    o = C.cylinder("cuff", 1.0, 1.0, verts=16, bevel=0)
    bm = bmesh.new(); bm.from_mesh(o.data)
    for v in bm.verts:   # (the unit cylinder along Z: z -0.5..0.5 -> x 0..CUFF_LEN, radius 0.029 at the wrist to 0.042)
        t = v.co.z + 0.5
        r = 0.029 + 0.013 * t ** 1.4
        v.co = Vector((t * CUFF_LEN, v.co.x * r, v.co.y * r))
    bm.to_mesh(o.data); bm.free()
    C.assign(o, mats["top" if sailor else glove]); C.smooth(o, 60)
    if sailor:   # (a rolled sleeve: a fat roll of the cloth at the mouth, no ring)
        return [o, ring_at("cuff_roll", (CUFF_LEN * 0.9, 0, 0), (1, 0, 0), 0.04, 0.012, mats["top"])]
    ring = ring_at("cuff_ring", (CUFF_LEN * 0.92, 0, 0), (1, 0, 0), 0.043, 0.0055, mats["accent"])
    return [o, ring]


def sleeve(mats):
    o = C.cylinder("sleeve", 1.0, 1.0, verts=16, bevel=0)
    bm = bmesh.new(); bm.from_mesh(o.data)
    for v in bm.verts:
        t = v.co.z + 0.5
        r = 0.041 + 0.012 * t
        v.co = Vector((t, v.co.x * r, v.co.y * r))
    bm.to_mesh(o.data); bm.free()
    C.assign(o, mats["top"]); C.smooth(o, 60)
    return [o]


def mirror_y(objs):
    for o in objs:
        for v in o.data.vertices:
            v.co.y = -v.co.y
        o.data.flip_normals()
    return objs


def build(who, out):
    C.reset()
    mats = {}
    cols = dict(STYLE); cols.update(COLOURS[who])
    for name, (c, r) in cols.items():
        mats[name] = C.mat_flat(name, c, rough=r, metal=0.9 if name in METALLIC else 0.0)
    glove = GLOVE[who]
    groups = {"hand.R": grip_hand(mats[glove]), "grip.L": mirror_y(grip_hand(mats[glove])), "hand.L": support_hand(mats[glove]),
              "cuff.R": cuff(mats, glove, who == "sailor"), "cuff.L": cuff(mats, glove, who == "sailor"), "arm.R": sleeve(mats), "arm.L": sleeve(mats)}
    arm = bpy.data.armatures.new("vm"); rig = C.link(bpy.data.objects.new("vm", arm))
    C.select_only([rig]); bpy.ops.object.mode_set(mode='EDIT')
    for b in groups:
        eb = arm.edit_bones.new(b); eb.head = (0, 0, 0); eb.tail = (0, 0, 0.05)
    bpy.ops.object.mode_set(mode='OBJECT')
    meshes = []
    for b, objs in groups.items():
        C.select_only(objs); bpy.ops.object.join()
        o = bpy.context.active_object; o.name = b.replace(".", "_")
        C.select_only([o]); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        g = o.vertex_groups.new(name=b); g.add(list(range(len(o.data.vertices))), 1.0, 'REPLACE')
        mod = o.modifiers.new("rig", 'ARMATURE'); mod.object = rig; o.parent = rig
        o.data.color_attributes.new("Col", 'BYTE_COLOR', 'CORNER')
        o.data.color_attributes.active_color = o.data.color_attributes["Col"]
        for d in o.data.color_attributes["Col"].data:
            d.color = (1, 1, 1, 1)
        meshes.append(o)
    path = os.path.join(out, f"fp_{who}.glb")
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
