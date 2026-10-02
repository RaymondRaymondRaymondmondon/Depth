# Red Tide's flora with bones (the Red Tide Visual Overhaul Spec, "Fish, beasts, and flora": flora that bends in the
# current), on the creature kit's rig builder (creatures_rt.Kit): unit-height plants standing on y = 0 in the game frame
# (x across, y up, z forward), rigid parts on named bones the game sways, flat materials it recolours per flora ('back'
# the plant, 'fin' its leaves or tips, 'belly' its holdfast):
#     kelp      a stipe on six bones (k0..k5) with blades and gas floats along it
#     grass     a tuft of nine blades of three bones each (b<i>_0..2)
#     fan       a sea fan's lattice on two bones (f0, f1), swaying as one sheet
#     anemone   a column (col) crowned with sixteen tentacles of two bones (t<i>_0..1)
#     branch    a branching coral (fire coral, hydroids, staghorn) on a base and six branch bones (br<i>)
#     sponge    a barrel sponge with its mouth (base)
#     roots     arching mangrove roots (base)
#     sargassum a floating raft of fronds and bladders (s<i>)
#     blender -b --factory-startup -P tools/artgen/flora_rt.py -- --out assets/redtide/flora [--only kelp]

import os
import sys
import math
import random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C
import creatures_rt as CR

FLORA = {}


def finish_joined(K, name, out):
    """As creatures_rt.Kit.finish, but each part's vertices are weighted to its bone and then the parts are joined into
    one mesh per material: a plant of thirty parts becomes three meshes (it's drawn dozens of times a frame)."""
    import bpy
    from mathutils import Vector
    from boat import G
    objs = [o for o, _ in K.parts]
    for o, bn in K.parts:
        C.select_only([o]); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        g = o.vertex_groups.new(name=bn)
        g.add(list(range(len(o.data.vertices))), 1.0, 'REPLACE')
    by_mat = {}
    for o in objs:
        by_mat.setdefault(o.data.materials[0].name if o.data.materials else "", []).append(o)
    joined = []
    for mat, group in by_mat.items():
        C.select_only(group); bpy.context.view_layer.objects.active = group[0]
        if len(group) > 1:
            bpy.ops.object.join()
        joined.append(bpy.context.view_layer.objects.active)
    arm = bpy.data.armatures.new(name + "_rig")
    rig = C.link(bpy.data.objects.new(name + "_rig", arm))
    C.select_only([rig]); bpy.ops.object.mode_set(mode='EDIT')
    for bn, h, t, par in K.bones:
        b = arm.edit_bones.new(bn)
        b.head = G(*h); b.tail = G(*t)
        if (Vector(b.tail) - Vector(b.head)).length < 1e-4:
            b.tail = tuple(Vector(b.head) + Vector((0, 0, 0.02)))
        if par:
            b.parent = arm.edit_bones[par]
        b.use_deform = True
    bpy.ops.object.mode_set(mode='OBJECT')
    for o in joined:
        mod = o.modifiers.new("rig", 'ARMATURE'); mod.object = rig
        o.parent = rig
        o.data.color_attributes.new("Col", 'BYTE_COLOR', 'CORNER')
        o.data.color_attributes.active_color = o.data.color_attributes["Col"]
        for d in o.data.color_attributes["Col"].data:
            d.color = (1, 1, 1, 1)
    path = os.path.join(out, f"cr_{name}.glb")
    C.select_only(joined + [rig])
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=False,
                              export_yup=True, export_texcoords=False, export_normals=True, export_skins=True,
                              export_animations=False, export_materials='EXPORT', export_vertex_color='ACTIVE',
                              export_all_influences=False)
    print("artgen: wrote", path)


def flora(name):
    def deco(f):
        FLORA[name] = f
        return f
    return deco


@flora("kelp")
def kelp(K):
    ys = [0.0, 0.17, 0.34, 0.5, 0.66, 0.83, 1.0]
    for k in range(6):
        K.bone(f"k{k}", (0, ys[k], 0), (0, ys[k + 1], 0), f"k{k - 1}" if k else None)
        K.limb(f"stipe{k}", (0, ys[k], 0), (0, ys[k + 1], 0), 0.012, 0.01, "back", f"k{k}", 8)
        for s in (1, -1):   # a blade each side, long and ribbon-like, tipped out and up
            y = (ys[k] + ys[k + 1]) / 2
            K.ell(f"blade{k}{s}", (s * 0.07, y + 0.03, 0.01 * s), (0.06, 0.09, 0.006), "fin", f"k{k}", 12)
        if k % 2 == 1:
            K.ell(f"float{k}", (0.025, ys[k + 1] - 0.02, 0), (0.018, 0.022, 0.018), "fin", f"k{k}", 10)
    K.ell("holdfast", (0, 0.0, 0), (0.05, 0.02, 0.05), "belly", "k0", 12)


@flora("grass")
def grass(K):
    rng = random.Random(3)
    for i in range(9):
        a = i * 2.4; r = 0.04 + 0.06 * rng.random()
        x, z = r * math.cos(a), r * math.sin(a)
        h = 0.7 + 0.3 * rng.random()
        lean = (0.08 * math.cos(a), 0.08 * math.sin(a))
        prev = None
        for j in range(3):
            y0, y1 = h * j / 3, h * (j + 1) / 3
            p0 = (x + lean[0] * j / 3, y0, z + lean[1] * j / 3); p1 = (x + lean[0] * (j + 1) / 3, y1, z + lean[1] * (j + 1) / 3)
            K.bone(f"b{i}_{j}", p0, p1, prev)
            K.limb(f"blade{i}{j}", p0, p1, 0.012 - j * 0.003, 0.009 - j * 0.003, "back" if j < 2 else "fin", f"b{i}_{j}", 6)
            prev = f"b{i}_{j}"
    K.ell("base", (0, 0.0, 0), (0.1, 0.015, 0.1), "belly", "b0_0", 10)


@flora("fan")
def fan(K):
    K.bone("f0", (0, 0, 0), (0, 0.5, 0)); K.bone("f1", (0, 0.5, 0), (0, 1.0, 0), "f0")
    K.limb("trunk", (0, 0, 0), (0, 0.25, 0), 0.025, 0.018, "belly", "f0", 10)
    rng = random.Random(7)
    for i in range(9):   # the branches fanning out in one plane (x-y), then a lattice across them
        a = math.radians(-70 + i * 17.5)
        tip = (0.55 * math.sin(a), 0.25 + 0.75 * math.cos(a) * (0.85 + 0.15 * rng.random()), 0)
        mid = (tip[0] * 0.5, 0.25 + (tip[1] - 0.25) * 0.5, 0)
        K.limb(f"br{i}a", (0, 0.25, 0), mid, 0.012, 0.009, "back", "f0", 6)
        K.limb(f"br{i}b", mid, tip, 0.009, 0.005, "fin", "f1", 6)
    for r in (0.45, 0.65, 0.85):
        for i in range(8):
            a0, a1 = math.radians(-70 + i * 17.5), math.radians(-70 + (i + 1) * 17.5)
            p0 = (r * 0.62 * math.sin(a0), 0.25 + r * 0.8 * math.cos(a0), 0); p1 = (r * 0.62 * math.sin(a1), 0.25 + r * 0.8 * math.cos(a1), 0)
            K.limb(f"lat{r}{i}", p0, p1, 0.004, 0.004, "back", "f1" if r > 0.5 else "f0", 4)


@flora("anemone")
def anemone(K):
    K.bone("col", (0, 0, 0), (0, 0.4, 0))
    K.limb("column", (0, 0, 0), (0, 0.38, 0), 0.12, 0.14, "belly", "col", 16)
    K.ell("disc", (0, 0.39, 0), (0.15, 0.03, 0.15), "back", "col", 18)
    for i in range(16):
        a = 2 * math.pi * i / 16; r = 0.13 if i % 2 else 0.09
        p0 = (r * math.cos(a), 0.4, r * math.sin(a))
        p1 = (r * 1.4 * math.cos(a), 0.62, r * 1.4 * math.sin(a))
        p2 = (r * 1.8 * math.cos(a), 0.82, r * 1.8 * math.sin(a))
        K.bone(f"t{i}_0", p0, p1, "col"); K.bone(f"t{i}_1", p1, p2, f"t{i}_0")
        K.limb(f"ten{i}a", p0, p1, 0.022, 0.017, "back", f"t{i}_0", 8)
        K.limb(f"ten{i}b", p1, p2, 0.017, 0.006, "fin", f"t{i}_1", 8)


@flora("branch")
def branch(K):
    K.bone("base", (0, 0, 0), (0, 0.3, 0))
    K.ell("foot", (0, 0.03, 0), (0.12, 0.05, 0.12), "belly", "base", 12)
    rng = random.Random(11)
    for i in range(6):
        a = i * 1.05 + rng.random() * 0.4
        p0 = (0.04 * math.cos(a), 0.05, 0.04 * math.sin(a))
        p1 = (0.18 * math.cos(a), 0.45 + 0.2 * rng.random(), 0.18 * math.sin(a))
        p2 = (0.26 * math.cos(a + 0.3), 0.8 + 0.2 * rng.random(), 0.26 * math.sin(a + 0.3))
        K.bone(f"br{i}", p0, p2, "base")
        K.limb(f"lim{i}a", p0, p1, 0.035, 0.025, "back", f"br{i}", 8)
        K.limb(f"lim{i}b", p1, p2, 0.025, 0.012, "back", f"br{i}", 8)
        K.ell(f"tip{i}", p2, (0.02, 0.02, 0.02), "fin", f"br{i}", 8)
        sub = ((p1[0] + p2[0]) / 2 + 0.08 * math.cos(a + 1.5), (p1[1] + p2[1]) / 2 + 0.1, (p1[2] + p2[2]) / 2 + 0.08 * math.sin(a + 1.5))
        K.limb(f"lim{i}c", ((p1[0] + p2[0]) / 2, (p1[1] + p2[1]) / 2, (p1[2] + p2[2]) / 2), sub, 0.016, 0.008, "back", f"br{i}", 6)
        K.ell(f"tip{i}c", sub, (0.014, 0.014, 0.014), "fin", f"br{i}", 6)


@flora("sponge")
def sponge(K):
    K.bone("base", (0, 0, 0), (0, 1.0, 0))
    for k in range(6):   # a barrel built of stacked flaring rings, ridged
        y = k * 0.16; r = 0.28 + 0.08 * math.sin(k / 5 * math.pi * 0.8)
        K.ell(f"ring{k}", (0, y + 0.08, 0), (r, 0.1, r), "back", "base", 20)
    K.ell("lip", (0, 0.96, 0), (0.33, 0.05, 0.33), "fin", "base", 20)
    K.ell("mouth", (0, 0.97, 0), (0.24, 0.04, 0.24), "belly", "base", 18)


@flora("roots")
def roots(K):
    K.bone("base", (0, 0, 0), (0, 1.0, 0))
    K.limb("trunk", (0, 0.55, 0), (0, 1.0, 0), 0.06, 0.05, "back", "base", 10)
    rng = random.Random(8)
    for i in range(11):   # prop roots: out of the trunk, curving down and a little out, into the sand
        a = i * 0.57 + rng.random() * 0.3; top = 0.5 + 0.12 * rng.random()
        out = 0.12 + 0.12 * rng.random()
        pts = [(0.03 * math.cos(a), top, 0.03 * math.sin(a)),
               (out * 0.8 * math.cos(a), top - 0.08, out * 0.8 * math.sin(a)),
               (out * math.cos(a), top * 0.45, out * math.sin(a)),
               (out * 1.15 * math.cos(a), 0.0, out * 1.15 * math.sin(a))]
        for j in range(3):
            K.limb(f"root{i}{j}", pts[j], pts[j + 1], 0.022 - j * 0.004, 0.018 - j * 0.004, "back" if j < 2 else "belly", "base", 8)

@flora("sargassum")
def sargassum(K):
    rng = random.Random(5)
    K.bone("base", (0, 0, 0), (0, 0.2, 0))
    for i in range(10):
        a = rng.random() * 2 * math.pi; r = rng.random() * 0.45
        c = (r * math.cos(a), 0.05, r * math.sin(a))
        K.bone(f"s{i}", c, (c[0], 0.25, c[2]), "base")
        for j in range(4):   # each frond a little bush of leaves and air bladders
            d = (c[0] + 0.12 * math.cos(a + j * 1.6), 0.05 + 0.04 * j, c[2] + 0.12 * math.sin(a + j * 1.6))
            K.ell(f"leaf{i}{j}", d, (0.05, 0.012, 0.02), "back", f"s{i}", 8)
            K.ell(f"blad{i}{j}", (d[0], d[1] + 0.02, d[2] + 0.03), (0.014, 0.014, 0.014), "fin", f"s{i}", 6)


if __name__ == "__main__":
    a = C.args()
    only = a[a.index("--only") + 1].split(",") if "--only" in a else None
    out = C.out_dir()
    for n, f in FLORA.items():
        if only and n not in only:
            continue
        C.reset()
        K = CR.Kit()
        f(K)
        finish_joined(K, "fl_" + n, out)   # (writes cr_fl_<name>.glb)
