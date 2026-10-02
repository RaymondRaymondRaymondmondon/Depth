# Melee and thrown weapons (the Visual Overhaul Spec: the realistic layer at a little less detail). The origin is where
# the right hand closes on the handle; +X runs to the business end. Two-handed ones mark the left hand's place too.

import math
import gunkit as K
from gunkit import bpy, C


def marks(W, tip, left=None):
    W.mark("grip_r", (0.0, 0, 0.0))
    W.mark("grip_l", left if left else (-0.02, 0, -0.01))
    W.mark("muzzle", tip)
    W.mark("eject", (0.0, 0, 0.0)); W.mark("sight", (-0.3, 0, 0.1))
    W.mark("mount_muzzle", tip); W.mark("mount_top", (tip[0] * 0.5, 0, 0.02))


def ash(W):
    if "ash" not in W.M:
        m = C.mat_walnut("ash")
        r = next(n for n in m.node_tree.nodes if n.type == 'VALTORGB')
        r.color_ramp.elements[0].color = (0.18, 0.13, 0.08, 1)
        r.color_ramp.elements[-1].color = (0.42, 0.32, 0.2, 1)
        W.M["ash"] = m
    return "ash"


def handle(W, x0, x1, r=0.015, mat="walnut", wrap=False):
    W.add(W.loft("handle", [(x0, 0, r * 0.95, r * 1.1), ((x0 + x1) / 2, 0, r, r * 1.15), (x1, 0, r * 0.9, r * 1.05)], 16), mat)
    if wrap:
        for k in range(int((x1 - x0) / 0.012)):
            W.add(W.ring(f"wrap{k}", (x0 + 0.006 + k * 0.012, 0, 0), r * 1.05, 0.0028), "leather")


def blade(W, name, x0, length, width, thick, point=0.35, curve=0.0, mat="bright", back_up=True):
    """A flat blade along +X from x0: full width for most of its length, then a point; an optional curve (a cutlass)."""
    bm_pts = []
    o = W.box(name, (length, thick, width), (x0 + length / 2, 0, width / 2 if back_up else 0), bevel=0.0008)
    C.apply_all(o)
    for v in o.data.vertices:
        u = (v.co.x + length / 2) / length                    # 0 at the hilt, 1 at the tip
        if u > 1 - point:
            k = (u - (1 - point)) / point
            v.co.z = v.co.z * (1 - k) + (width * 0.85 if back_up else 0) * k * (1 if v.co.z > 0 else 0.3)
        if v.co.y != 0 and v.co.z < (width * 0.2 if back_up else -width * 0.3):
            v.co.y *= 0.25                                    # the honed edge, thinner than the spine
        v.co.z += curve * (u ** 2)
    sub = o.modifiers.new("sub", 'SUBSURF'); sub.levels = 1
    return W.add(o, mat)


# ---------------------------------------------------------------- the starting kit and the Gunsmith's blades
def build_priest(W):
    W.add(W.loft("priest", [(-0.12, 0, 0.012, 0.012), (-0.02, 0, 0.014, 0.014), (0.08, 0, 0.022, 0.022), (0.17, 0, 0.03, 0.03), (0.2, 0, 0.026, 0.026)], 20), "walnut")
    W.add(W.ring("lead_band", (0.15, 0, 0), 0.03, 0.006), "iron")
    W.add(W.ring("lanyard", (-0.13, 0, 0), 0.01, 0.002, 'Y'), "rope")
    marks(W, (0.2, 0, 0))


def build_knife(W):
    handle(W, -0.1, 0.0, 0.012)
    for x in (-0.07, -0.03):
        W.add(W.cyl(f"rivet{x}", 0.003, 0.028, (x, 0, 0.0), 'Y', verts=12, bevel=0.0004), "brass")
    W.add(W.box("bolster", (0.008, 0.026, 0.028), (0.004, 0, 0.0), bevel=0.002), "brass")
    blade(W, "blade", 0.008, 0.14, 0.024, 0.0035, back_up=False)
    marks(W, (0.15, 0, 0))


def build_gaff(W):
    a = ash(W)
    W.add(W.cyl("shaft", 0.014, 1.1, (0.35, 0, 0), 'X', verts=16, bevel=0.002), a)
    W.add(W.cyl("ferrule", 0.016, 0.05, (0.9, 0, 0), 'X', verts=16, bevel=0.002), "iron")
    hook = [(0.92, 0, 0), (1.0, 0, 0.01), (1.06, 0, -0.02), (1.06, 0, -0.08), (1.02, 0, -0.11), (0.98, 0, -0.1)]
    W.add(W.tube("hook", hook, 0.006), "iron")
    W.add(W.cone("hook_point", 0.006, 0.0, 0.025, (0.97, 0, -0.095)), "bright")
    W.add(W.ring("grip_tape", (-0.15, 0, 0), 0.015, 0.004), "rope")
    marks(W, (1.06, 0, -0.05), left=(0.35, 0, 0))


def build_knuckles(W):
    for k in range(4):
        W.add(W.ring(f"ring{k}", (0.0, (k - 1.5) * 0.024, 0.02), 0.013, 0.004, 'X'), "brass")
    W.add(W.box("bar", (0.012, 0.1, 0.012), (0.0, 0, 0.034), bevel=0.003), "brass")
    W.add(W.loft("palm_bar", [(-0.03, 0, 0.012, 0.01), (0.0, 0, 0.012, 0.01)], 12), "brass")
    marks(W, (0.03, 0, 0.03))


def build_pin(W):
    W.add(W.loft("pin", [(-0.12, 0, 0.016, 0.016), (-0.02, 0, 0.018, 0.018), (0.0, 0, 0.026, 0.026), (0.02, 0, 0.02, 0.02), (0.25, 0, 0.022, 0.022), (0.28, 0, 0.018, 0.018)], 20), "walnut")
    marks(W, (0.28, 0, 0))


def build_boathook(W):
    a = ash(W)
    W.add(W.cyl("pole", 0.016, 1.6, (0.6, 0, 0), 'X', verts=16, bevel=0.002), a)
    W.add(W.cyl("socket", 0.019, 0.08, (1.42, 0, 0), 'X', verts=16, bevel=0.002), "brass")
    W.add(W.cone("spike", 0.01, 0.0, 0.12, (1.52, 0, 0)), "brass")
    W.add(W.tube("hook", [(1.44, 0, 0.0), (1.47, 0, 0.05), (1.43, 0, 0.09), (1.39, 0, 0.08)], 0.008), "brass")
    marks(W, (1.58, 0, 0), left=(0.5, 0, 0))


def build_spike(W):
    W.add(W.cone("spike", 0.016, 0.002, 0.3, (0.13, 0, 0)), "bright")
    W.add(W.loft("head", [(-0.06, 0, 0.018, 0.018), (-0.02, 0, 0.017, 0.017)], 16), "bright")
    W.add(W.ring("eye", (-0.075, 0, 0), 0.014, 0.004, 'Y'), "bright")
    W.add(W.ring("lanyard", (-0.075, 0, 0), 0.018, 0.003, 'Y'), "rope")
    marks(W, (0.28, 0, 0))


def build_cleaver(W):
    handle(W, -0.11, 0.0, 0.013)
    for x in (-0.08, -0.04):
        W.add(W.cyl(f"rivet{x}", 0.003, 0.03, (x, 0, 0.0), 'Y', verts=12, bevel=0.0004), "brass")
    b = W.box("blade", (0.18, 0.004, 0.08), (0.09, 0, -0.025), bevel=0.0015)
    C.apply_all(b)
    for v in b.data.vertices:
        if v.co.z < -0.02:
            v.co.y *= 0.3
    W.add(b, "bright")
    W.add(W.cyl("hole", 0.008, 0.006, (0.16, 0, 0.004), 'Y', verts=16, bevel=0.0008), "iron")
    marks(W, (0.18, 0, -0.02))


def build_cutlass(W):
    handle(W, -0.11, 0.0, 0.014, mat="leather", wrap=True)
    # the basket guard: a pierced iron shell round the knuckles
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, radius=0.055, location=(-0.04, 0, -0.01))
    g = bpy.context.active_object; g.name = "basket"; g.scale = (1.15, 0.85, 1.0)
    import bmesh
    bm = bmesh.new(); bm.from_mesh(g.data)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.z > 0.0 or v.co.x < -0.04], context='VERTS')
    bm.to_mesh(g.data); bm.free()
    sol = g.modifiers.new("t", 'SOLIDIFY'); sol.thickness = 0.003
    W.add(g, "iron")
    W.add(W.sphere("pommel", (-0.115, 0, 0.0), (0.016, 0.016, 0.016), 16), "iron")
    blade(W, "blade", 0.005, 0.62, 0.035, 0.005, point=0.15, curve=0.06)
    marks(W, (0.63, 0, 0.05))


def build_coralclub(W):
    handle(W, -0.15, 0.1, 0.016, mat="walnut", wrap=True)
    W.add(W.loft("shaft", [(0.08, 0, 0.018, 0.018), (0.3, 0, 0.026, 0.026)], 16), "walnut")
    for k in range(9):
        a = k * 2.39996
        W.add(W.sphere(f"coral{k}", (0.3 + (k % 3) * 0.02, math.cos(a) * 0.022, math.sin(a) * 0.022), (0.025, 0.02, 0.02), 14), "coral")
    marks(W, (0.36, 0, 0))


def build_sharkblade(W):
    handle(W, -0.12, 0.0, 0.014, mat="rope")
    W.add(W.box("blade", (0.36, 0.012, 0.05), (0.18, 0, 0.0), bevel=0.004, segments=3), "walnut")
    for k in range(9):
        for s in (-1, 1):
            W.add(W.cone(f"tooth{k}{s}", 0.007, 0.0, 0.02, (0.04 + k * 0.035, 0, s * 0.033), 'Z' if s > 0 else 'Z', verts=8), "bone")
    marks(W, (0.37, 0, 0))


def build_flenser(W):
    a = ash(W)
    W.add(W.cyl("pole", 0.018, 1.5, (0.55, 0, 0), 'X', verts=16, bevel=0.002), a)
    W.add(W.cyl("socket", 0.02, 0.12, (1.33, 0, 0), 'X', verts=16, bevel=0.002), "iron")
    W.add(W.box("spade", (0.16, 0.006, 0.12), (1.47, 0, 0), bevel=0.003), "bright")
    marks(W, (1.55, 0, 0), left=(0.5, 0, 0))


def build_lance(W):
    a = ash(W)
    W.add(W.cyl("pole", 0.018, 1.4, (0.5, 0, 0), 'X', verts=16, bevel=0.002), a)
    W.add(W.cyl("shank", 0.008, 0.5, (1.45, 0, 0), 'X', verts=12, bevel=0.001), "iron")
    W.add(W.loft("leaf", [(1.7, 0, 0.004, 0.006), (1.76, 0, 0.004, 0.03), (1.86, 0, 0.002, 0.002)], 12), "bright")
    W.add(W.ring("line_eye", (1.22, 0, 0.0), 0.02, 0.004), "iron")
    marks(W, (1.86, 0, 0), left=(0.45, 0, 0))


def build_maul(W):
    a = ash(W)
    W.add(W.cyl("handle", 0.016, 0.75, (0.25, 0, 0), 'X', verts=16, bevel=0.002), a)
    W.add(W.cyl("cylinder", 0.05, 0.16, (0.62, 0, 0.0), 'Z', verts=28, bevel=0.004), "brass")
    W.add(W.cyl("head_face", 0.055, 0.03, (0.62, 0, -0.095), 'Z', verts=28, bevel=0.004), "iron",
          "bolt", axis=(0, 0, 1), amount=-0.03, kind="slide", pivot=(0.62, 0, -0.095))
    W.add(W.tube("pipe", [(0.56, 0.04, 0.06), (0.45, 0.03, 0.03), (0.35, 0.02, 0.02)], 0.007), "copper")
    W.add(W.cyl("gauge", 0.016, 0.01, (0.62, 0.05, 0.04), 'Y', verts=20, bevel=0.002), "brass")
    marks(W, (0.62, 0, -0.11), left=(0.3, 0, 0))


def build_obsidian(W):
    handle(W, -0.1, 0.0, 0.013, mat="bone")
    W.add(W.ring("binding", (0.004, 0, 0), 0.016, 0.005), "rope")
    blade(W, "blade", 0.008, 0.17, 0.034, 0.006, point=0.5, mat="obsidian", back_up=False)
    marks(W, (0.18, 0, 0))


# ---------------------------------------------------------------- thrown
def build_crackerjack(W):
    for k in range(7):
        a = k * 2 * math.pi / 6
        r = 0 if k == 6 else 0.014
        W.add(W.cyl(f"cracker{k}", 0.007, 0.08, (0.0, math.cos(a) * r, math.sin(a) * r), 'X', verts=12, bevel=0.001), "red")
    W.add(W.ring("tie", (0.0, 0, 0), 0.023, 0.003), "string")
    W.add(W.tube("fuse", [(0.04, 0, 0), (0.07, 0, 0.02), (0.09, 0.01, 0.01)], 0.0018), "rope")
    marks(W, (0.09, 0, 0.01))


def build_lampoil(W):
    W.add(W.loft("bottle", [(-0.09, 0, 0.032, 0.032), (0.03, 0, 0.034, 0.034), (0.06, 0, 0.014, 0.014), (0.1, 0, 0.012, 0.012)], 20), "glass")
    W.add(W.cyl("oil", 0.03, 0.09, (-0.04, 0, 0), 'X', verts=20, bevel=0.002), "walnut")
    W.add(W.tube("rag", [(0.1, 0, 0), (0.13, 0.01, 0.02), (0.15, -0.01, 0.04)], 0.009), "canvas")
    marks(W, (0.15, 0, 0.04))


def build_dynamite(W):
    for k, (y, z) in enumerate([(-0.014, 0), (0.014, 0), (0.0, 0.022)]):
        W.add(W.cyl(f"stick{k}", 0.014, 0.2, (0.0, y, z), 'X', verts=16, bevel=0.002), "red")
    for x in (-0.06, 0.06):
        W.add(W.ring(f"tape{x}", (x, 0, 0.008), 0.032, 0.004), "canvas")
    W.add(W.tube("fuse", [(0.1, 0, 0.022), (0.14, 0, 0.04), (0.17, 0.02, 0.03)], 0.002), "rope")
    marks(W, (0.17, 0, 0.03))


def build_depthcharge(W):
    W.add(W.cyl("drum", 0.11, 0.3, (0.0, 0, 0.0), 'X', verts=36, bevel=0.008), "iron")
    for x in (-0.12, 0.12):
        W.add(W.ring(f"rim{x}", (x, 0, 0.0), 0.11, 0.008), "steel")
    W.add(W.cyl("pistol", 0.03, 0.04, (0.16, 0, 0.0), 'X', verts=24, bevel=0.003), "brass")
    W.add(W.cyl("dial", 0.022, 0.006, (0.183, 0, 0.0), 'X', verts=24, bevel=0.001), "bone")
    W.add(W.tube("handle", [(-0.08, 0, 0.11), (-0.08, 0, 0.15), (0.08, 0, 0.15), (0.08, 0, 0.11)], 0.007), "steel")
    marks(W, (0.19, 0, 0.0))
