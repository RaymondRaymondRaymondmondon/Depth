# NOCLIP's people (the user's playtest note, 2026-10-05: "they should look like the yellow suit picture, with the design
# style of the first picture" - the yellow hazmat suits of the Backrooms films, in the rounded, chunky look of Red Tide's
# divers). Built on crew.py's skeleton, so the game poses them with fig::PoseFigure like every other figure:
#     hazmat   the Bureau's salvage suit: a baggy yellow coverall with the hood drawn over the head, a black full-face gas
#              mask (two round lenses, a filter canister at the chin, a speaking grille), black gloves and boots with
#              orange soles, black tape patches and a zip
#     orange   a unique costume (the user's first picture): an orange suit, a black rounded helmet with a smoked visor, a
#              harness crossing the chest and round the waist, a yellow neck seal, badges on the chest
# Flat named materials (suit, trim, glove, boot, sole, mask, lens, patch, badge...) so the game can recolour the suit.
#     blender -b --factory-startup -P tools/artgen/noclip_crew.py -- --out assets/noclip
import os
import sys
import math
import bmesh
from mathutils import Vector
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import common as C
import crew as K
os.environ["ARTGEN_IMPORT"] = "1"
import costumes as CO
bpy = C.bpy

HC = Vector((0.0, 0, 1.61))   # the head's centre (crew.py's skull, lowered by HEAD_DZ)

COLOURS = {
    "hazmat": {"suit": ((0.86, 0.66, 0.06), 0.55), "trim": ((0.7, 0.52, 0.04), 0.6), "glove": ((0.03, 0.03, 0.03), 0.35),
               "boot": ((0.04, 0.04, 0.045), 0.4), "sole": ((0.85, 0.32, 0.08), 0.6), "mask": ((0.025, 0.025, 0.03), 0.3),
               "lens": ((0.08, 0.1, 0.11), 0.05), "patch": ((0.02, 0.02, 0.02), 0.5), "badge": ((0.9, 0.9, 0.86), 0.5),
               "metal": ((0.55, 0.56, 0.58), 0.35), "skin": ((0.78, 0.55, 0.42), 0.6)},
    "orange": {"suit": ((0.9, 0.24, 0.05), 0.6), "trim": ((0.78, 0.2, 0.04), 0.6), "glove": ((0.03, 0.03, 0.03), 0.35),
               "boot": ((0.05, 0.05, 0.055), 0.4), "sole": ((0.12, 0.12, 0.12), 0.6), "mask": ((0.04, 0.04, 0.045), 0.25),
               "lens": ((0.18, 0.22, 0.3), 0.05), "patch": ((0.05, 0.05, 0.05), 0.55), "badge": ((0.9, 0.88, 0.82), 0.5),
               "metal": ((0.55, 0.56, 0.58), 0.35), "skin": ((0.78, 0.55, 0.42), 0.6), "seal": ((0.86, 0.8, 0.12), 0.6),
               "stripe": ((0.75, 0.08, 0.06), 0.5)},
}
METALLIC = {"metal"}


def paint_body(body, mats, order):
    """The body under the suit: gloves on the hands, boots on the feet, the suit everywhere else."""
    for n in order:
        body.data.materials.append(mats[n])
    J = K.joints()
    def nearest(p):
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
        b = nearest(f.center)
        if any(b.startswith(k) for k in ("hand", "thumb", "index", "middle", "ring", "pinky")):
            m = "glove"
        elif b.startswith(("foot", "toe")) or (b.startswith("shin") and f.center.z < 0.3):
            m = "boot"
        else:
            m = "suit"
        f.material_index = order.index(m)


def hazmat(k):
    """The yellow salvage suit."""
    # the baggy coverall over the body (a padded suit round the skeleton, soft): no head, the hands left to the gloves
    k.suit("suit", puff=1.42, extra=0.012)
    # the hood: drawn right over the head, a broad face opening the mask fills; it spreads onto the shoulders
    hood = k.ell("hood", tuple(HC + Vector((-0.012, 0, 0.012))), (0.17, 0.165, 0.185), "suit", "head", 40)
    CO.cut(hood, [((1, 0, -0.15), 52)], HC)
    sol = hood.modifiers.new("t", 'SOLIDIFY'); sol.thickness = 0.014; C.select_only([hood]); bpy.ops.object.modifier_apply(modifier="t")
    k.lathe("hood_skirt", [(0.235, -0.27), (0.2, -0.2), (0.155, -0.13)], tuple(HC), "suit", "chest", 32, sx=0.9, sy=1.05)
    k.add(K.tube_ring("hood_rim", tuple(HC + Vector((0.125, 0, -0.03))), 0.115, 0.016, "trim", k.mats, axis='X', squash=1.12), "trim", "head")
    # the full-face mask: a black rubber face, two round lenses, the filter canister at the chin, a speaking grille
    k.ell("mask", tuple(HC + Vector((0.075, 0, -0.025))), (0.085, 0.105, 0.125), "mask", "head", 32)
    for s in (1, -1):
        c = HC + Vector((0.15, s * 0.045, 0.02))
        k.add(K.tube_ring(f"lens_rim{s}", tuple(c), 0.034, 0.008, "mask", k.mats, axis='X'), "mask", "head")
        k.ell(f"lens{s}", tuple(c + Vector((0.002, 0, 0))), (0.012, 0.031, 0.031), "lens", "head", 20)
    fc = HC + Vector((0.17, 0, -0.1))
    k.cone("filter", fc - Vector((0.03, 0, 0.0)), fc + Vector((0.06, 0, -0.02)), 0.045, 0.048, "mask", "head", 24)
    k.cone("filter_cap", fc + Vector((0.06, 0, -0.02)), fc + Vector((0.075, 0, -0.023)), 0.049, 0.04, "metal", "head", 24)
    for s in (1, -1):
        k.ell(f"valve{s}", tuple(HC + Vector((0.14, s * 0.075, -0.075))), (0.02, 0.022, 0.022), "mask", "head", 16)
    # the zip down the front and the waist cord
    k.box("zip", (0.012, 0.02, 0.5), (0.215, 0, 1.18), "trim", "chest", bevel=0.004)
    k.add(K.tube_ring("cord", (0.0, 0, 0.98), 0.205, 0.012, "trim", k.mats, squash=0.86), "trim", "pelvis")
    # black tape patches (the reference's dabs): on the chest, the arms, the thighs and shins
    k.box("patch_chest", (0.02, 0.08, 0.05), (0.21, 0.09, 1.3), "patch", "chest", rot=(0.0, 0, 0.3), bevel=0.004)
    k.box("patch_belly", (0.02, 0.05, 0.08), (0.205, -0.1, 1.06), "patch", "spine", rot=(0.3, 0, 0), bevel=0.004)
    for sd, s in (("L", 1), ("R", -1)):
        p = k.along_arm(sd, 0.35) + Vector((0.07, 0, 0))
        k.box(f"patch_arm{sd}", (0.02, 0.05, 0.07), tuple(p), "patch", f"upperarm.{sd}", bevel=0.004)
        k.box(f"patch_thigh{sd}", (0.02, 0.06, 0.09), (0.15, s * 0.12, 0.72), "patch", f"thigh.{sd}", bevel=0.004)
        k.box(f"patch_shin{sd}", (0.02, 0.05, 0.06), (0.13, s * 0.12, 0.36), "patch", f"shin.{sd}", rot=(0.4 * s, 0, 0), bevel=0.004)
        # cuffs where the gloves meet the sleeves, boots over the legs, orange soles
        h = k.j(f"hand.{sd}"); fa = k.j(f"forearm.{sd}")
        k.add(K.tube_ring(f"cuff{sd}", tuple(h + (fa - h) * 0.18), 0.06, 0.018, "glove", k.mats, axis='ARM' if s > 0 else 'ARM_R'), "glove", f"forearm.{sd}")
        rubber_boot(k, sd, s)
        # a puffy rubber glove over each hand (the fingers read as one mitt at play distance)
        h = k.j(f"hand.{sd}"); fa = k.j(f"forearm.{sd}")
        d = (h - fa).normalized()
        k.ell(f"mitt{sd}", tuple(h + d * 0.045), (0.05, 0.042, 0.06), "glove", f"hand.{sd}", 24)
        k.ell(f"thumb{sd}", tuple(h + d * 0.02 + Vector((0.035, s * 0.02, 0))), (0.022, 0.02, 0.03), "glove", f"hand.{sd}", 16)
    # the air pack on the back: a rounded tank in a harness, a hose over the shoulder to the mask's filter
    k.cone("tank", Vector((-0.24, 0, 1.0)), Vector((-0.24, 0, 1.36)), 0.085, 0.085, "metal", "chest", 28)
    k.ell("tank_top", (-0.24, 0, 1.36), (0.085, 0.085, 0.05), "metal", "chest", 24)
    k.ell("tank_bot", (-0.24, 0, 1.0), (0.085, 0.085, 0.04), "metal", "chest", 24)
    k.cone("tank_valve", Vector((-0.24, 0, 1.4)), Vector((-0.24, 0, 1.44)), 0.02, 0.016, "mask", "chest", 12)
    for z in (1.08, 1.28):
        k.add(K.tube_ring(f"tank_band{z}", (-0.24, 0, z), 0.088, 0.01, "patch", k.mats), "patch", "chest")
    k.tube("hose", [(-0.24, 0, 1.44), (-0.2, -0.1, 1.5), (-0.02, -0.17, 1.5), (0.14, -0.1, 1.45), HC + Vector((0.16, -0.03, -0.11))], 0.016, "mask", "chest")


def rubber_boot(k, sd, s):
    """A rounded rubber boot: a domed toe cap, a heel, a shaft and a ribbed sole (no boxes)."""
    y = s * 0.115
    k.ell(f"toe{sd}", (0.1, y, 0.06), (0.09, 0.075, 0.06), "boot", f"foot.{sd}", 28)
    k.ell(f"heel{sd}", (-0.03, y, 0.07), (0.07, 0.072, 0.07), "boot", f"foot.{sd}", 24)
    k.lathe(f"bootleg{sd}", [(0.08, 0.0), (0.086, 0.12), (0.092, 0.17)], (0.0, y, 0.08), "boot", f"shin.{sd}", 24)
    k.add(K.tube_ring(f"bootlip{sd}", (0.0, y, 0.25), 0.092, 0.012, "boot", k.mats), "boot", f"shin.{sd}")
    sole = k.ell(f"sole{sd}", (0.04, y, 0.016), (0.16, 0.08, 0.018), "sole", f"foot.{sd}", 28)
    return sole


def orange(k):
    """The unique costume: an orange flight suit, a black helmet with a smoked visor, a harness."""
    k.suit("suit", puff=1.22, extra=0.012)
    helm = k.ell("helmet", tuple(HC + Vector((0.0, 0, 0.015))), (0.165, 0.15, 0.17), "mask", "head", 40)
    CO.cut(helm, [((1, 0, -0.05), 48)], HC)
    sol = helm.modifiers.new("t", 'SOLIDIFY'); sol.thickness = 0.016; C.select_only([helm]); bpy.ops.object.modifier_apply(modifier="t")
    k.ell("visor", tuple(HC + Vector((0.095, 0, 0.0))), (0.075, 0.125, 0.11), "lens", "head", 32)
    k.add(K.tube_ring("visor_rim", tuple(HC + Vector((0.115, 0, 0.0))), 0.11, 0.013, "mask", k.mats, axis='X', squash=1.12), "mask", "head")
    for s in (1, -1):
        k.cone(f"earpiece{s}", HC + Vector((-0.01, s * 0.14, -0.01)), HC + Vector((-0.01, s * 0.18, -0.01)), 0.045, 0.04, "mask", "head", 20)
    k.box("helm_ridge", (0.2, 0.03, 0.03), tuple(HC + Vector((-0.02, 0, 0.175))), "mask", "head", bevel=0.01)
    # the yellow neck seal, the harness (an X over the chest from the shoulders, a waist strap, leg loops)
    k.add(K.tube_ring("seal", (0.0, 0, 1.45), 0.11, 0.04, "seal", k.mats, squash=0.95), "seal", "chest")
    for s in (1, -1):
        k.tube(f"strap{s}", [(-0.12, s * 0.15, 1.42), (0.05, s * 0.14, 1.44), (0.2, s * 0.06, 1.3), (0.21, -s * 0.04, 1.16), (0.19, -s * 0.13, 1.03)], 0.018, "patch", "chest")
        k.tube(f"backstrap{s}", [(-0.12, s * 0.15, 1.42), (-0.2, s * 0.06, 1.25), (-0.19, -s * 0.1, 1.05)], 0.018, "patch", "chest")
    k.add(K.tube_ring("waist", (0.0, 0, 1.0), 0.2, 0.02, "patch", k.mats, squash=0.85), "patch", "pelvis")
    k.box("buckle", (0.02, 0.07, 0.05), (0.205, 0, 1.0), "metal", "pelvis", bevel=0.006)
    # chest badges: a white patch with red stripes on the right, a name tape on the left
    k.box("badge", (0.015, 0.075, 0.05), (0.205, -0.085, 1.33), "badge", "chest", bevel=0.004)
    for z in (1.322, 1.338):
        k.box(f"stripe{z}", (0.017, 0.07, 0.006), (0.206, -0.085, z), "stripe", "chest", bevel=0.001)
    k.box("nametape", (0.015, 0.08, 0.022), (0.205, 0.09, 1.34), "patch", "chest", bevel=0.003)
    for sd, s in (("L", 1), ("R", -1)):
        p = k.along_arm(sd, 0.3) + Vector((0.065, 0, 0))
        k.box(f"armbadge{sd}", (0.015, 0.05, 0.05), tuple(p), "badge", f"upperarm.{sd}", bevel=0.004)
        rubber_boot(k, sd, s)


SUITS = {"hazmat": hazmat, "orange": orange}


def build(who, out):
    C.reset()
    J = K.joints()
    cols = {n: (c, r, 0.9 if n in METALLIC else 0.0) for n, (c, r) in COLOURS[who].items()}
    k = CO.Kit(J, cols)
    rig = K.make_armature(J)
    body = K.body_mesh(J, 1.1)
    order = list(k.mats.keys())
    paint_body(body, k.mats, order)
    SUITS[who](k)
    meshes = [body]
    for o, bone in k.parts:
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
    path = os.path.join(out, f"crew_{who}.glb")
    C.select_only(meshes + [rig])
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=False,
                              export_yup=True, export_texcoords=False, export_normals=True, export_skins=True,
                              export_animations=False, export_materials='EXPORT', export_vertex_color='ACTIVE',
                              export_all_influences=False)
    print("artgen: wrote", path)


def build_fp(who, out):
    """The first-person arm (the right one; the game mirrors it for the left): the sleeve from the elbow, its cuff, and
    a puffy rubber glove closed as a loose fist. Local axes as the game wants them: x right, z up (glTF y), -y forward
    (glTF +z). The elbow is the origin; the knuckles sit about 0.42 m forward."""
    C.reset()
    cols = {n: (c, r, 0.9 if n in METALLIC else 0.0) for n, (c, r) in COLOURS[who].items()}
    mats = {n: C.mat_flat(n, c, rough=r, metal=m) for n, (c, r, m) in cols.items()}
    parts = []
    def ell(name, c, r, mat, segs=28):
        bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=segs, v_segments=max(8, segs * 2 // 3), radius=1.0)
        bmesh.ops.scale(bm, vec=Vector(r), verts=bm.verts); bmesh.ops.translate(bm, vec=Vector(c), verts=bm.verts)
        me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
        o = C.link(bpy.data.objects.new(name, me)); C.assign(o, mats[mat]); C.smooth(o, 180); parts.append(o); return o
    def sleeve(name, prof, mat, segs=28):
        """A surface of revolution along -y: prof [(radius, distance forward)], with a gentle fold wobble."""
        bm = bmesh.new(); rings = []
        for i, (r, d) in enumerate(prof):
            ring = []
            for k in range(segs):
                a = 2 * math.pi * k / segs
                rr = r * (1 + 0.04 * math.sin(3 * a + i * 1.7))   # (the baggy cloth's folds)
                ring.append(bm.verts.new((rr * math.cos(a), -d, rr * math.sin(a) * 0.92)))
            rings.append(ring)
        for a_, b_ in zip(rings, rings[1:]):
            for k in range(segs):
                bm.faces.new((a_[k], a_[(k + 1) % segs], b_[(k + 1) % segs], b_[k]))
        bmesh.ops.contextual_create(bm, geom=rings[0]); bmesh.ops.contextual_create(bm, geom=rings[-1])
        me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
        o = C.link(bpy.data.objects.new(name, me)); C.assign(o, mats[mat]); C.smooth(o, 180); parts.append(o); return o
    big = who == "hazmat"
    sleeve("sleeve", [(0.085 if big else 0.07, -0.06), (0.09 if big else 0.072, 0.06), (0.084 if big else 0.066, 0.18), (0.075 if big else 0.06, 0.27)], "suit")
    if big:
        ell("patch", (0.0, -0.13, 0.085), (0.045, 0.035, 0.012), "patch", 16)
    # the cuff: a thick rubber ring where the glove meets the sleeve
    ell("cuff", (0.0, -0.285, 0.0), (0.07, 0.035, 0.064), "glove", 28)
    # the fist: the back of the hand, the rolled fingers across the front, the thumb over them
    ell("palm", (0.0, -0.35, 0.005), (0.058, 0.07, 0.048), "glove")
    for i in range(4):
        x = -0.036 + i * 0.024
        ell(f"finger{i}", (x, -0.405 + abs(i - 1.5) * 0.004, -0.012), (0.014, 0.026, 0.03), "glove", 16)
    ell("thumb", (0.04, -0.39, 0.02), (0.018, 0.034, 0.017), "glove", 16)
    for o in parts:
        C.select_only([o]); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    C.export_glb(parts, os.path.join(out, f"fp_{who}.glb"))


a = C.args()
only = a[a.index("--suit") + 1] if "--suit" in a else None
out = C.out_dir()
for w in SUITS:
    if only and w != only:
        continue
    build(w, out)
    build_fp(w, out)
