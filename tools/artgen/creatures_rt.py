# Red Tide's creature kit, the body plans that aren't fish (the Red Tide Visual Overhaul Spec, "Fish, beasts, and
# flora": one rig per body plan; the species record's three colours paint it in the game). Each is a unit-size
# creature in the game frame (x across, y up, z forward, the head toward +0.5), built of rigid parts on named bones,
# with flat materials the game recolours ('back', 'belly', 'fin'; 'eye' stays) and occlusion in the vertex colour:
#     crab        a carapace, two clawed arms (claw.L/R, pincer.L/R), eight two-part legs (leg<i>a, leg<i>b), eye stalks
#     shrimp      a body of six segments on a chain (t0..t5) curling into a tail fan, legs, long antennae
#     cephalopod  a mantle, two eyes, eight arms of four bones each (a<i>_<j>) trailing behind
#     jelly       a bell that pulses (bell), eight tentacles of four bones (k<i>_<j>) hanging, four frilled oral arms
#     turtle      a domed carapace and a plastron, a head on its neck (head), four flippers (flip.FL/FR/RL/RR)
#     cetacean    a dolphin-shaped body on a four-bone spine (s0..s3) that bends up and down, flukes, a dorsal fin
#     blender -b --factory-startup -P tools/artgen/creatures_rt.py -- --out assets/redtide/creatures [--only crab]

import os
import sys
import math
import bpy
from mathutils import Vector
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C
from boat import G   # (the game frame -> Blender's)

PLANS = {}


def plan(name):
    def deco(f):
        PLANS[name] = f
        return f
    return deco


def mats():
    return {
        "back": C.mat_flat("back", (0.5, 0.35, 0.25), 0.55), "belly": C.mat_flat("belly", (0.85, 0.8, 0.7), 0.6),
        "fin": C.mat_flat("fin", (0.4, 0.25, 0.18), 0.6), "eye": C.mat_flat("eye", (0.02, 0.02, 0.025), 0.1),
        "gill": C.mat_flat("gill", (0.7, 0.2, 0.15), 0.4),   # (gills, crystal, bronze fittings, lures: the game colours them per boss)
    }


class Kit:
    def __init__(self):
        self.parts = []      # (object, bone)
        self.bones = []      # (name, head, tail, parent): game frame
        self.M = mats()

    def bone(self, name, head, tail, parent=None):
        self.bones.append((name, head, tail, parent))

    def ell(self, name, c, r, mat, bone, segs=28):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=segs, ring_count=max(10, segs // 2), radius=1.0, location=G(*c))
        o = bpy.context.active_object; o.name = name
        o.scale = (r[0], r[2], r[1])   # (G: Blender x is the game's x, y the game's -z, z the game's y)
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        C.assign(o, self.M[mat]); C.smooth(o, 180)
        self.parts.append((o, bone)); return o

    def limb(self, name, a, b, r0, r1, mat, bone, verts=12):
        """A tapered limb from a to b (game frame)."""
        A, B = Vector(G(*a)), Vector(G(*b))
        d = B - A
        bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r0, radius2=r1, depth=d.length, location=(A + B) / 2)
        o = bpy.context.active_object; o.name = name
        o.rotation_euler = Vector((0, 0, 1)).rotation_difference(d.normalized()).to_euler()
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
        C.assign(o, self.M[mat]); C.smooth(o, 60)
        self.parts.append((o, bone)); return o

    def loft_body(self, name, sections, mat, spine_bones, spine_z, segs=28):
        """One smooth body lofted through sections [(z, half_width, half_height, y_centre), ...] head first (game frame),
        weighted along the spine: each vertex shared between the two nearest spine bones by its z (a hero sculpt bends
        as one surface, not as a stack of beads). spine_z: the bones' joint z values, head first, one more than bones."""
        import bmesh
        bm = bmesh.new(); rings = []
        for (z, hw, hh, yc) in sections:
            ring = []
            for k in range(segs):
                a = 2 * math.pi * k / segs
                c, s = math.cos(a), math.sin(a)
                # a superellipse: fuller than an ellipse along the flanks, the belly a little flatter
                x = hw * math.copysign(abs(c) ** 0.8, c)
                y = yc + hh * math.copysign(abs(s) ** 0.85, s) * (0.92 if s < 0 else 1.0)
                ring.append(bm.verts.new(G(x, y, z)))
            rings.append(ring)
        for a, b in zip(rings, rings[1:]):
            for k in range(segs):
                bm.faces.new((a[k], a[(k + 1) % segs], b[(k + 1) % segs], b[k]))
        for ring, sgn in ((rings[0], 1), (rings[-1], -1)):   # (the ends capped)
            bm.faces.new(ring if sgn < 0 else list(reversed(ring)))
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
        o = C.link(bpy.data.objects.new(name, me))
        C.assign(o, self.M[mat]); C.smooth(o, 180)
        sub = o.modifiers.new("sub", 'SUBSURF'); sub.levels = 1
        C.select_only([o]); bpy.ops.object.modifier_apply(modifier="sub")
        self.soft = getattr(self, "soft", [])
        self.soft.append((o, list(spine_bones), list(spine_z)))
        return o

    def finish(self, name, out):
        objs = [o for o, _ in self.parts] + [o for o, _, _ in getattr(self, "soft", [])]
        for o in objs:
            C.select_only([o]); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        for o, bones, zs in getattr(self, "soft", []):
            # (the lofted bodies' weights: by each vertex's game-frame z between the spine joints; Blender's -y is the game's z)
            groups = {b: o.vertex_groups.new(name=b) for b in bones}
            for v in o.data.vertices:
                z = -v.co.y
                w = [0.0] * len(bones)
                for i in range(len(bones)):
                    mid = (zs[i] + zs[i + 1]) / 2; half = abs(zs[i] - zs[i + 1]) / 2 + 1e-6
                    w[i] = max(0.0, 1 - abs(z - mid) / (half * 1.6))
                if sum(w) <= 0:
                    w[0 if z > zs[0] else len(bones) - 1] = 1.0
                tot = sum(w)
                for i, b in enumerate(bones):
                    if w[i] > 0:
                        groups[b].add([v.index], w[i] / tot, 'REPLACE')
        arm = bpy.data.armatures.new(name + "_rig")
        rig = C.link(bpy.data.objects.new(name + "_rig", arm))
        C.select_only([rig]); bpy.ops.object.mode_set(mode='EDIT')
        for bn, h, t, par in self.bones:
            b = arm.edit_bones.new(bn)
            b.head = G(*h); b.tail = G(*t)
            if (Vector(b.tail) - Vector(b.head)).length < 1e-4:
                b.tail = tuple(Vector(b.head) + Vector((0, 0, 0.02)))
            if par:
                b.parent = arm.edit_bones[par]
            b.use_deform = True
        bpy.ops.object.mode_set(mode='OBJECT')
        for o, bn in self.parts:
            g = o.vertex_groups.new(name=bn)
            g.add(list(range(len(o.data.vertices))), 1.0, 'REPLACE')
            mod = o.modifiers.new("rig", 'ARMATURE'); mod.object = rig
            o.parent = rig
        for o, _, _ in getattr(self, "soft", []):
            mod = o.modifiers.new("rig", 'ARMATURE'); mod.object = rig
            o.parent = rig
        sc = bpy.context.scene
        sc.cycles.samples = 16
        sc.world = sc.world or bpy.data.worlds.new("world")
        for o in objs:
            o.data.color_attributes.new("Col", 'BYTE_COLOR', 'CORNER')
            o.data.color_attributes.active_color = o.data.color_attributes["Col"]
        C.select_only(objs)
        sc.render.bake.target = 'VERTEX_COLORS'
        bpy.ops.object.bake(type='AO')
        for o in objs:
            for d in o.data.color_attributes["Col"].data:
                a = 0.55 + 0.45 * d.color[0]
                d.color = (a, a, a, 1.0)
        path = os.path.join(out, f"cr_{name}.glb")
        C.select_only(objs + [rig])
        bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=False,
                                  export_yup=True, export_texcoords=False, export_normals=True, export_skins=True,
                                  export_animations=False, export_materials='EXPORT', export_vertex_color='ACTIVE',
                                  export_all_influences=False)
        print("artgen: wrote", path)


@plan("crab")
def crab(K):
    K.bone("body", (0, 0, -0.1), (0, 0, 0.1))
    K.ell("carapace", (0, 0.04, 0), (0.42, 0.13, 0.32), "back", "body")
    K.ell("under", (0, -0.02, 0), (0.38, 0.08, 0.28), "belly", "body")
    for s, sd in ((1, "L"), (-1, "R")):
        # eye stalks
        K.limb(f"stalk{sd}", (s * 0.06, 0.12, 0.27), (s * 0.08, 0.2, 0.3), 0.012, 0.009, "fin", "body")
        K.ell(f"eye{sd}", (s * 0.08, 0.21, 0.3), (0.022, 0.022, 0.022), "eye", "body", 12)
        # the clawed arm: an arm then the claw (a fixed finger and the pincer that opens)
        K.bone(f"claw.{sd}", (s * 0.28, 0.0, 0.2), (s * 0.4, 0.04, 0.45), "body")
        K.limb(f"arm{sd}", (s * 0.28, 0.0, 0.2), (s * 0.4, 0.04, 0.42), 0.04, 0.035, "fin", f"claw.{sd}")
        K.ell(f"palm{sd}", (s * 0.41, 0.05, 0.5), (0.07, 0.06, 0.11), "back", f"claw.{sd}", 20)
        K.limb(f"finger{sd}", (s * 0.4, 0.04, 0.58), (s * 0.38, 0.03, 0.72), 0.03, 0.006, "back", f"claw.{sd}")
        K.bone(f"pincer.{sd}", (s * 0.43, 0.08, 0.57), (s * 0.42, 0.1, 0.72), f"claw.{sd}")
        K.limb(f"pincer{sd}", (s * 0.43, 0.08, 0.57), (s * 0.42, 0.09, 0.72), 0.028, 0.005, "back", f"pincer.{sd}")
        # four legs a side, each two segments: out and up, then down to the tip
        for i in range(4):
            z = 0.12 - i * 0.12
            n = f"leg{i}{sd}"
            K.bone(n + "a", (s * 0.32, 0.0, z), (s * 0.55, 0.12, z - 0.04), "body")
            K.limb(n + "a", (s * 0.32, 0.0, z), (s * 0.55, 0.12, z - 0.04), 0.03, 0.022, "fin", n + "a")
            K.bone(n + "b", (s * 0.55, 0.12, z - 0.04), (s * 0.68, -0.12, z - 0.07), n + "a")
            K.limb(n + "b", (s * 0.55, 0.12, z - 0.04), (s * 0.68, -0.12, z - 0.07), 0.022, 0.006, "fin", n + "b")


@plan("shrimp")
def shrimp(K):
    zs = [0.42, 0.24, 0.1, -0.03, -0.15, -0.26, -0.38]
    for k in range(6):
        K.bone(f"t{k}", (0, 0.0, zs[k]), (0, 0.0, zs[k + 1]), f"t{k - 1}" if k else None)
        r = 0.09 - 0.008 * k
        K.ell(f"seg{k}", (0, 0.02, (zs[k] + zs[k + 1]) / 2), (r, r * 1.1, (zs[k] - zs[k + 1]) * 0.62), "back", f"t{k}", 20)
        K.ell(f"segb{k}", (0, -0.03, (zs[k] + zs[k + 1]) / 2), (r * 0.8, r * 0.6, (zs[k] - zs[k + 1]) * 0.5), "belly", f"t{k}", 16)
    # the tail fan on the last segment, the rostrum and antennae on the head
    for s in (-1, 0, 1):
        K.limb(f"fan{s}", (0, 0.0, -0.38), (s * 0.08, 0.0, -0.52), 0.035, 0.012, "fin", "t5")
    K.limb("rostrum", (0, 0.07, 0.42), (0, 0.1, 0.56), 0.015, 0.002, "back", "t0")
    for s, sd in ((1, "L"), (-1, "R")):
        K.ell(f"eye{sd}", (s * 0.05, 0.08, 0.38), (0.02, 0.02, 0.02), "eye", "t0", 12)
        K.limb(f"antenna{sd}", (s * 0.03, 0.06, 0.44), (s * 0.18, 0.2, 1.1), 0.006, 0.001, "fin", "t0", 6)
        for i in range(5):
            z = 0.32 - i * 0.07
            K.limb(f"leg{i}{sd}", (s * 0.04, -0.06, z), (s * 0.09, -0.18, z + 0.03), 0.008, 0.003, "fin", "t0" if i < 2 else "t1", 6)


@plan("cephalopod")
def cephalopod(K):
    K.bone("mantle", (0, 0.0, 0.0), (0, 0.0, 0.45))
    K.ell("mantle", (0, 0.04, 0.22), (0.17, 0.17, 0.28), "back", "mantle", 32)
    K.ell("head", (0, 0.0, -0.02), (0.15, 0.13, 0.11), "back", "mantle", 24)
    for s, sd in ((1, "L"), (-1, "R")):
        K.ell(f"eye{sd}", (s * 0.12, 0.04, -0.02), (0.035, 0.04, 0.035), "eye", "mantle", 14)
    # eight arms from the head, trailing back (toward -z), each on a chain of four bones
    for i in range(8):
        a = i * 2 * math.pi / 8
        ox, oy = 0.09 * math.cos(a), 0.09 * math.sin(a)
        prev = "mantle"
        p = Vector((ox, oy - 0.03, -0.08))
        d = Vector((ox * 0.6, oy * 0.6 - 0.02, -0.14))
        for j in range(4):
            q = p + d
            bn = f"a{i}_{j}"
            K.bone(bn, tuple(p), tuple(q), prev)
            r0 = 0.035 * (1 - j * 0.22); r1 = 0.035 * (1 - (j + 1) * 0.22)
            K.limb(bn, tuple(p), tuple(q), r0, max(0.004, r1), "back" if j % 2 == 0 else "fin", bn, 10)
            prev = bn; p = q
            d = d * 1.0


@plan("jelly")
def jelly(K):
    K.bone("bell", (0, 0.0, 0.0), (0, 0.3, 0.0))
    bpy.ops.mesh.primitive_uv_sphere_add(segments=36, ring_count=18, radius=1.0, location=G(0, 0.0, 0))
    o = bpy.context.active_object; o.name = "bell"
    o.scale = (0.32, 0.32, 0.24)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    for v in o.data.vertices:   # a dome: the bottom half pushed up into the bell's hollow
        if v.co.z < 0:
            v.co.z = -v.co.z * 0.35
    C.assign(o, K.M["back"]); C.smooth(o, 180); K.parts.append((o, "bell"))
    K.ell("rim", (0, 0.0, 0), (0.33, 0.03, 0.33), "fin", "bell", 32)
    for i in range(8):
        a = i * 2 * math.pi / 8
        p = Vector((0.28 * math.cos(a), -0.01, 0.28 * math.sin(a)))
        prev = "bell"
        for j in range(4):
            q = p + Vector((0, -0.22, 0))
            bn = f"k{i}_{j}"
            K.bone(bn, tuple(p), tuple(q), prev)
            K.limb(bn, tuple(p), tuple(q), 0.008, 0.005, "fin", bn, 6)
            prev = bn; p = q
    for i in range(4):   # the frilled oral arms in the middle
        a = i * math.pi / 2 + math.pi / 4
        K.limb(f"oral{i}", (0.04 * math.cos(a), -0.02, 0.04 * math.sin(a)), (0.08 * math.cos(a), -0.5, 0.08 * math.sin(a)), 0.03, 0.01, "belly", "bell", 8)


@plan("turtle")
def turtle(K):
    K.bone("body", (0, 0, -0.2), (0, 0, 0.2))
    K.ell("carapace", (0, 0.06, -0.02), (0.3, 0.13, 0.38), "back", "body", 32)
    K.ell("plastron", (0, -0.02, -0.02), (0.27, 0.05, 0.34), "belly", "body", 24)
    for k in range(5):   # the scutes' ridges down the back
        K.ell(f"scute{k}", (0, 0.17, 0.22 - k * 0.12), (0.08, 0.03, 0.06), "fin", "body", 14)
    K.bone("head", (0, 0.02, 0.32), (0, 0.04, 0.5), "body")
    K.limb("neck", (0, 0.02, 0.3), (0, 0.04, 0.42), 0.06, 0.055, "belly", "head")
    K.ell("head", (0, 0.05, 0.47), (0.07, 0.06, 0.09), "fin", "head", 20)
    for s, sd in ((1, "L"), (-1, "R")):
        K.ell(f"eye{sd}", (s * 0.05, 0.08, 0.5), (0.014, 0.014, 0.014), "eye", "head", 10)
    for (nm, x, z, length, w) in (("FL", 0.26, 0.2, 0.42, 0.11), ("FR", -0.26, 0.2, 0.42, 0.11), ("RL", 0.22, -0.3, 0.2, 0.07), ("RR", -0.22, -0.3, 0.2, 0.07)):
        s = 1 if x > 0 else -1
        bn = f"flip.{nm}"
        tip = (x + s * length, -0.03, z - length * 0.3)
        K.bone(bn, (x, 0.0, z), tip, "body")
        f = K.ell(bn, ((x + tip[0]) / 2, -0.015, (z + tip[2]) / 2), (length * 0.5, 0.018, w), "fin", bn, 20)
    K.limb("tail", (0, 0.0, -0.38), (0, 0.0, -0.48), 0.03, 0.005, "fin", "body")


@plan("cetacean")
def cetacean(K):
    zs = [0.5, 0.22, -0.02, -0.25, -0.5]
    for k in range(4):
        K.bone(f"s{k}", (0, 0.0, zs[k]), (0, 0.0, zs[k + 1]), f"s{k - 1}" if k else None)
    prof = [(0.48, 0.035), (0.4, 0.08), (0.25, 0.12), (0.05, 0.13), (-0.15, 0.1), (-0.32, 0.055), (-0.44, 0.03)]
    for k in range(len(prof) - 1):
        (z0, r0), (z1, r1) = prof[k], prof[k + 1]
        bone = "s0" if z0 > 0.22 else "s1" if z0 > -0.02 else "s2" if z0 > -0.25 else "s3"
        K.ell(f"body{k}", (0, 0.0, (z0 + z1) / 2), ((r0 + r1) / 2, (r0 + r1) / 2 * 0.95, abs(z0 - z1) * 1.25), "back", bone, 24)
        K.ell(f"bellyp{k}", (0, -((r0 + r1) / 2) * 0.35, (z0 + z1) / 2), ((r0 + r1) / 2 * 0.85, (r0 + r1) / 2 * 0.6, abs(z0 - z1) * 1.2), "belly", bone, 20)
    K.limb("beak", (0, -0.01, 0.46), (0, -0.015, 0.56), 0.035, 0.018, "back", "s0")
    for s, sd in ((1, "L"), (-1, "R")):
        K.ell(f"eye{sd}", (s * 0.07, 0.02, 0.38), (0.012, 0.012, 0.012), "eye", "s0", 10)
        K.limb(f"flipper{sd}", (s * 0.1, -0.06, 0.2), (s * 0.26, -0.14, 0.08), 0.04, 0.012, "fin", "s1", 10)
        K.ell(f"fluke{sd}", (s * 0.11, 0.0, -0.5), (0.12, 0.012, 0.05), "fin", "s3", 16)
    K.limb("dorsal", (0, 0.11, 0.0), (0, 0.24, -0.1), 0.05, 0.006, "fin", "s1", 10)


# ---------------------------------------------------------------- the bosses (spec, "Bosses"), unit length like the rest
def spine(K, zs, prefix="s"):
    for k in range(len(zs) - 1):
        K.bone(f"{prefix}{k}", (0, 0.0, zs[k]), (0, 0.0, zs[k + 1]), f"{prefix}{k - 1}" if k else None)


@plan("goliath")
def goliath(K):
    # a colossal grouper, sculpted (the spec's hero sculpt): one smooth deep body lofted head to tail and bent along its
    # spine, a huge underslung jaw, the spined dorsal fin with its membrane, broad pectorals, a rounded tail fan, mottled
    # flanks; armour plates over the gills (they glow red in the game's windows) and flanks, barnacles, two old
    # harpoons and a chain embedded in it
    zs = [0.5, 0.22, -0.02, -0.25, -0.5]
    spine(K, zs)
    body = [(0.54, 0.05, 0.06, -0.02), (0.48, 0.13, 0.14, 0.0), (0.38, 0.19, 0.2, 0.02), (0.24, 0.215, 0.235, 0.03),
            (0.06, 0.205, 0.225, 0.02), (-0.12, 0.17, 0.185, 0.02), (-0.28, 0.115, 0.125, 0.01), (-0.4, 0.06, 0.075, 0.0),
            (-0.47, 0.035, 0.05, 0.0)]
    K.loft_body("body", body, "back", ["s0", "s1", "s2", "s3"], zs)
    K.loft_body("belly", [(z, hw * 0.86, hh * 0.55, yc - hh * 0.45) for (z, hw, hh, yc) in body[1:-1]], "belly", ["s0", "s1", "s2", "s3"], zs)
    # the mouth: a jutting lower jaw, thick lips, the dark gape between
    K.ell("jaw", (0, -0.075, 0.5), (0.125, 0.05, 0.075), "belly", "s0", 24)
    K.ell("lip_up", (0, 0.03, 0.53), (0.12, 0.035, 0.05), "back", "s0", 20)
    K.ell("gape", (0, -0.02, 0.55), (0.1, 0.025, 0.02), "eye", "s0", 16)
    for s, sd in ((1, "L"), (-1, "R")):
        K.ell(f"eye{sd}", (s * 0.12, 0.1, 0.4), (0.03, 0.03, 0.03), "eye", "s0", 14)
        K.ell(f"brow{sd}", (s * 0.11, 0.13, 0.39), (0.04, 0.02, 0.05), "back", "s0", 12)
        K.ell(f"gill{sd}", (s * 0.18, 0.0, 0.28), (0.02, 0.14, 0.055), "gill", "s0", 16)
        K.ell(f"plate_head{sd}", (s * 0.19, 0.06, 0.33), (0.016, 0.12, 0.08), "back", "s0", 16)   # the armour over the gills
        for k in range(3):
            K.ell(f"plate_flank{sd}{k}", (s * 0.205, 0.05 - k * 0.04, 0.12 - k * 0.14), (0.014, 0.08, 0.06), "back", "s1" if k < 2 else "s2", 16)
        K.ell(f"pectoral{sd}", (s * 0.24, -0.05, 0.16), (0.11, 0.015, 0.09), "fin", "s1", 16)
        K.ell(f"pelvic{sd}", (s * 0.08, -0.2, 0.16), (0.03, 0.06, 0.07), "fin", "s1", 12)
        for k in range(9):   # the mottling: darker blotches down the flanks
            z = 0.3 - k * 0.08; y = 0.08 * math.sin(k * 2.1)
            K.ell(f"spot{sd}{k}", (s * (0.2 - abs(z) * 0.18), y, z), (0.012, 0.035, 0.04), "fin", "s1" if z > -0.02 else "s2", 10)
    # the dorsal fin: eleven spines and the membrane between them, then the soft rear dorsal; the anal fin; the tail fan
    for k in range(11):
        z = 0.3 - k * 0.055; h = 0.08 + 0.05 * math.sin(k / 10 * math.pi)
        K.limb(f"spine{k}", (0, 0.2 + 0.02 * math.cos(k * 0.3), z), (0, 0.2 + h + 0.03, z - 0.02), 0.01, 0.002, "fin", "s1" if z > -0.02 else "s2", 6)
    K.ell("dorsal_web", (0, 0.27, 0.02), (0.008, 0.06, 0.29), "fin", "s1", 16)
    K.ell("dorsal_soft", (0, 0.2, -0.28), (0.01, 0.07, 0.1), "fin", "s3", 14)
    K.ell("anal", (0, -0.16, -0.27), (0.01, 0.06, 0.08), "fin", "s3", 12)
    K.ell("tail_fan", (0, 0.0, -0.54), (0.012, 0.17, 0.1), "fin", "s3", 18)
    for k in range(10):   # barnacles
        K.ell(f"barnacle{k}", (0.13 * math.sin(k * 2.3), 0.12 + 0.06 * math.cos(k * 1.7), 0.3 - k * 0.07), (0.018, 0.012, 0.018), "belly", "s0" if k < 3 else "s1" if k < 6 else "s2", 8)
    for k, (a, b) in enumerate([((0.1, 0.18, 0.1), (0.38, 0.38, 0.0)), ((-0.12, 0.14, -0.15), (-0.42, 0.32, -0.25))]):
        K.limb(f"harpoon{k}", a, b, 0.008, 0.006, "fin", "s1" if k == 0 else "s2", 6)
    K.limb("chain", (0.05, 0.2, -0.05), (0.2, -0.2, -0.3), 0.012, 0.012, "fin", "s2", 6)

@plan("lobster")
def lobster(K):
    # the giant cave lobster: pale and eyeless, crystal coral grown on its shell, a soft veined underside
    K.bone("body", (0, 0, 0.0), (0, 0, 0.25))
    zs = [0.0, -0.1, -0.2, -0.3, -0.4]
    for k in range(4):
        K.bone(f"t{k}", (0, 0, zs[k]), (0, 0, zs[k + 1]), "body" if k == 0 else f"t{k - 1}")
        K.ell(f"seg{k}", (0, 0.02, (zs[k] + zs[k + 1]) / 2), (0.12 - k * 0.015, 0.08, 0.06), "back", f"t{k}", 20)
        K.ell(f"segb{k}", (0, -0.03, (zs[k] + zs[k + 1]) / 2), (0.1 - k * 0.015, 0.04, 0.05), "belly", f"t{k}", 16)
    K.ell("carapace", (0, 0.04, 0.15), (0.15, 0.1, 0.2), "back", "body", 28)
    K.ell("underside", (0, -0.04, 0.15), (0.13, 0.06, 0.18), "belly", "body", 20)
    for s in (-1, 0, 1):
        K.limb(f"fan{s}", (0, 0.0, -0.4), (s * 0.08, 0.0, -0.5), 0.04, 0.015, "fin", "t3")
    for k in range(6):   # crystal coral on the shell
        K.limb(f"crystal{k}", (0.05 * math.sin(k * 2.1), 0.12, 0.25 - k * 0.06), (0.08 * math.sin(k * 2.1), 0.24 + 0.03 * (k % 2), 0.22 - k * 0.06), 0.02, 0.0, "gill", "body" if k < 3 else "t0", 6)
    for s, sd in ((1, "L"), (-1, "R")):
        K.bone(f"claw.{sd}", (s * 0.12, 0.0, 0.3), (s * 0.22, 0.04, 0.6), "body")
        K.limb(f"arm{sd}", (s * 0.12, 0.0, 0.3), (s * 0.2, 0.04, 0.55), 0.035, 0.03, "fin", f"claw.{sd}")
        K.ell(f"palm{sd}", (s * 0.22, 0.05, 0.66), (0.07, 0.05, 0.12), "back", f"claw.{sd}", 20)
        K.bone(f"pincer.{sd}", (s * 0.24, 0.09, 0.7), (s * 0.24, 0.1, 0.86), f"claw.{sd}")
        K.limb(f"pincer{sd}", (s * 0.24, 0.09, 0.7), (s * 0.24, 0.1, 0.86), 0.03, 0.005, "back", f"pincer.{sd}")
        K.limb(f"antenna{sd}", (s * 0.04, 0.06, 0.32), (s * 0.3, 0.2, 0.95), 0.01, 0.002, "fin", "body", 6)
        for i in range(4):
            z = 0.22 - i * 0.07
            n = f"leg{i}{sd}"
            K.bone(n + "a", (s * 0.12, -0.02, z), (s * 0.26, 0.04, z - 0.02), "body")
            K.limb(n + "a", (s * 0.12, -0.02, z), (s * 0.26, 0.04, z - 0.02), 0.016, 0.012, "fin", n + "a")
            K.bone(n + "b", (s * 0.26, 0.04, z - 0.02), (s * 0.34, -0.1, z - 0.04), n + "a")
            K.limb(n + "b", (s * 0.26, 0.04, z - 0.02), (s * 0.34, -0.1, z - 0.04), 0.012, 0.004, "fin", n + "b")


@plan("orca")
def orca(K):
    cetacean(K)
    for s in (-1, 1):   # the eye patch and the saddle (the game paints back black, belly white)
        K.ell(f"patch{s}", (s * 0.09, 0.04, 0.32), (0.015, 0.03, 0.06), "belly", "s0", 12)
    K.limb("scar", (0.12, 0.05, 0.1), (0.1, -0.05, -0.05), 0.006, 0.006, "gill", "s1", 6)
    K.limb("tall_dorsal", (0, 0.12, 0.02), (0, 0.38, -0.05), 0.06, 0.008, "fin", "s1", 10)


@plan("wyrm")
def wyrm(K):
    # the Cistern Wyrm: a long sea serpent on twelve bones, bronze collars from its taming, a crest of fins
    n = 12
    zs = [0.5 - k * (1.0 / n) for k in range(n + 1)]
    spine(K, zs, "w")
    for k in range(n):
        r = 0.05 * (1.0 - 0.6 * k / n) + 0.02
        K.ell(f"seg{k}", (0, 0.0, (zs[k] + zs[k + 1]) / 2), (r, r, (zs[k] - zs[k + 1]) * 1.2), "back", f"w{k}", 18)
        K.ell(f"segb{k}", (0, -r * 0.4, (zs[k] + zs[k + 1]) / 2), (r * 0.8, r * 0.5, (zs[k] - zs[k + 1]) * 0.6), "belly", f"w{k}", 14)
        if k % 3 == 1:
            K.ell(f"collar{k}", (0, 0.0, zs[k]), (r * 1.15, r * 1.15, 0.012), "gill", f"w{k}", 18)
        K.limb(f"crest{k}", (0, r * 0.9, (zs[k] + zs[k + 1]) / 2), (0, r * 0.9 + 0.035, zs[k + 1]), 0.012, 0.002, "fin", f"w{k}", 6)
    K.ell("head", (0, 0.01, 0.52), (0.08, 0.06, 0.09), "back", "w0", 22)
    K.ell("jaw", (0, -0.035, 0.55), (0.06, 0.025, 0.07), "belly", "w0", 16)
    for s, sd in ((1, "L"), (-1, "R")):
        K.ell(f"eye{sd}", (s * 0.06, 0.04, 0.56), (0.015, 0.015, 0.015), "eye", "w0", 10)
        K.limb(f"horn{sd}", (s * 0.04, 0.05, 0.48), (s * 0.08, 0.12, 0.38), 0.012, 0.002, "gill", "w0", 6)


@plan("angler")
def angler(K):
    # the Lantern Leviathan: a vast dark anglerfish, a mouth like a cave, needle teeth, the pale lure on its rod
    zs = [0.5, 0.2, -0.05, -0.28, -0.5]
    spine(K, zs)
    K.ell("body", (0, 0.0, 0.05), (0.3, 0.28, 0.38), "back", "s1", 32)
    K.ell("head", (0, 0.02, 0.32), (0.32, 0.27, 0.2), "back", "s0", 32)
    K.ell("tailbody", (0, 0.0, -0.3), (0.12, 0.13, 0.18), "back", "s2", 20)
    K.ell("mouth", (0, -0.05, 0.48), (0.26, 0.12, 0.04), "eye", "s0", 24)
    for k in range(14):   # needle teeth round the mouth
        a = math.pi * (k / 13.0)
        K.limb(f"tooth{k}", (0.24 * math.cos(a), -0.05 + 0.1 * math.sin(a), 0.5), (0.22 * math.cos(a), -0.05 + 0.04 * math.sin(a), 0.54), 0.008, 0.0, "belly", "s0", 5)
    for s, sd in ((1, "L"), (-1, "R")):
        K.ell(f"eye{sd}", (s * 0.18, 0.15, 0.38), (0.02, 0.02, 0.02), "eye", "s0", 10)
        K.limb(f"pectoral{sd}", (s * 0.26, -0.05, 0.08), (s * 0.42, -0.15, -0.05), 0.07, 0.01, "fin", "s1", 10)
    K.bone("lure", (0, 0.26, 0.3), (0, 0.46, 0.62), "s0")
    K.limb("rod", (0, 0.26, 0.3), (0, 0.46, 0.62), 0.012, 0.006, "fin", "lure", 8)
    K.ell("bulb", (0, 0.46, 0.64), (0.045, 0.045, 0.045), "gill", "lure", 14)
    for s in (-1, 1):
        K.limb(f"tail{s}", (0, 0.0, -0.45), (0, s * 0.14, -0.6), 0.05, 0.01, "fin", "s3", 10)


if __name__ == "__main__":
    a = C.args()
    only = a[a.index("--only") + 1].split(",") if "--only" in a else None
    out = C.out_dir()
    for n, f in PLANS.items():
        if only and n not in only:
            continue
        C.reset()
        K = Kit()
        f(K)
        K.finish(n, out)
