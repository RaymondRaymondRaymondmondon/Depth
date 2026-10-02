# The Gunsmith's attachments as their own models (the Visual Overhaul Spec: "every attachment is its own model mounted
# on the gun"). Each is built about the point where it mounts, +X along the barrel; the game sets it on the weapon's
# matching marker: mount_muzzle (compensator, choke, baffle, bayonet), mount_top (sight, night glass, lodestone sight,
# eyeglass scope), mount_under (extended and drum magazines), mount_side (steam feed), the breech (oilskin cover).
#     blender -b --factory-startup -P tools/artgen/attachments.py -- --out assets/shared/attachments

import os
import sys
import math
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import common as C
import gunkit as K
bpy = C.bpy


def build(aid, fn, out):
    W = K.Weapon("att_" + aid)
    fn(W)
    W.mark("grip_r", (0, 0, 0))
    W.finish(out, size=512)


def sight(W):       # a short brass scope on two rings
    W.add(W.cyl("tube", 0.009, 0.16, (0.0, 0, 0.022), 'X', verts=24, bevel=0.001), "brass")
    W.add(W.cone("eye", 0.009, 0.013, 0.03, (-0.093, 0, 0.022)), "brass")
    W.add(W.cone("bell", 0.009, 0.014, 0.03, (0.093, 0, 0.022)), "brass")
    for x in (-0.04, 0.04):
        W.add(W.ring(f"ring{x}", (x, 0, 0.022), 0.0105, 0.0028), "steel")
        W.add(W.box(f"foot{x}", (0.01, 0.01, 0.014), (x, 0, 0.008), bevel=0.0015), "steel")


def nightglass(W):  # a fat brass night glass with a big objective
    W.add(W.cyl("tube", 0.014, 0.2, (0.0, 0, 0.03), 'X', verts=28, bevel=0.0015), "brass")
    W.add(W.cone("bell", 0.014, 0.026, 0.06, (0.13, 0, 0.03)), "brass")
    W.add(W.cyl("glass", 0.024, 0.002, (0.161, 0, 0.03), 'X', verts=28, bevel=0), "glass")
    for x in (-0.05, 0.05):
        W.add(W.ring(f"ring{x}", (x, 0, 0.03), 0.016, 0.0035), "steel")
        W.add(W.box(f"foot{x}", (0.012, 0.012, 0.02), (x, 0, 0.011), bevel=0.002), "steel")


def eyeglass(W):    # the drowned eyeglass: a scope with a pale lens that seems to look back
    sight(W)
    W.add(W.cyl("lens", 0.012, 0.002, (0.109, 0, 0.022), 'X', verts=24, bevel=0), "bone")


def lodestone(W):   # a dark stone set in a brass ring on a post
    W.add(W.box("post", (0.01, 0.008, 0.016), (0.0, 0, 0.008), bevel=0.002), "brass")
    W.add(W.ring("setting", (0.0, 0, 0.024), 0.011, 0.003, 'X'), "brass")
    W.add(W.sphere("stone", (0.0, 0, 0.024), (0.009, 0.009, 0.009), 16), "obsidian")


def compensator(W): # a slotted muzzle brake
    o = W.cyl("brake", 0.012, 0.045, (0.022, 0, 0.0), 'X', verts=24, bevel=0.0015, hole=0.005)
    for k in range(3):
        for s in (-1, 1):
            cut = C.bevelled_box(f"slot{k}{s}", (0.006, 0.03, 0.008), loc=(0.01 + k * 0.012, s * 0.01, 0.0), bevel=0)
            b = o.modifiers.new(f"s{k}{s}", 'BOOLEAN'); b.object = cut; b.operation = 'DIFFERENCE'
            C.select_only([o]); bpy.ops.object.modifier_apply(modifier=b.name); bpy.data.objects.remove(cut)
    W.add(o, "steel")


def choke(W):       # a knurled choke ring
    W.add(W.cyl("choke", 0.0125, 0.03, (0.012, 0, 0.0), 'X', verts=28, bevel=0.0012, hole=0.008), "case")


def baffle(W):      # a period can-type silencer
    W.add(W.cyl("can", 0.019, 0.17, (0.085, 0, 0.0), 'X', verts=28, bevel=0.003), "steel")
    for x in (0.02, 0.15):
        W.add(W.ring(f"band{x}", (x, 0, 0.0), 0.0195, 0.0025), "brass")


def bayonet(W):     # a socket bayonet under the muzzle
    W.add(W.cyl("socket", 0.012, 0.05, (-0.02, 0, -0.012), 'X', verts=20, bevel=0.0015), "steel")
    b = W.box("blade", (0.3, 0.004, 0.02), (0.15, 0, -0.022), bevel=0.0008)
    C.apply_all(b)
    for v in b.data.vertices:
        u = (v.co.x + 0.15) / 0.3
        if u > 0.7:
            v.co.z *= 1 - (u - 0.7) / 0.3
    W.add(b, "bright")


def extmag(W):      # a longer box magazine
    W.add(W.box("mag", (0.06, 0.022, 0.09), (0.0, 0, -0.045), bevel=0.003), "steel")


def drum(W):        # a 75-round drum
    W.add(W.cyl("drum", 0.07, 0.04, (0.0, 0, -0.07), 'Y', verts=40, bevel=0.004), "steel")
    W.add(W.cyl("drum_hub", 0.02, 0.046, (0.0, 0, -0.07), 'Y', verts=20, bevel=0.002), "case")
    W.add(W.box("neck", (0.02, 0.02, 0.03), (0.0, 0, -0.008), bevel=0.003), "steel")


def steamfeed(W):   # a little boiler and its pipe along the side
    W.add(W.cyl("boiler", 0.025, 0.09, (-0.02, -0.03, 0.0), 'X', verts=24, bevel=0.004), "copper")
    W.add(W.tube("pipe", [(0.025, -0.03, 0.0), (0.06, -0.025, 0.01), (0.1, -0.012, 0.01)], 0.004), "brass")
    W.add(W.cyl("valve", 0.008, 0.012, (-0.02, -0.03, 0.03), 'Z', verts=16, bevel=0.002), "brass")


def oilskin(W):     # an oilskin cover tied round the breech
    W.add(W.cyl("cover", 0.034, 0.09, (0.0, 0, 0.0), 'X', verts=24, bevel=0.01), "canvas")
    for x in (-0.035, 0.035):
        W.add(W.ring(f"tie{x}", (x, 0, 0.0), 0.034, 0.003), "string")


def speedloader(W): # six rounds in a disc (seen during a revolver's reload)
    W.add(W.cyl("disc", 0.018, 0.01, (0.0, 0, 0.0), 'X', verts=24, bevel=0.002), "steel")
    for k in range(6):
        a = k * math.pi / 3
        W.add(W.cyl(f"round{k}", 0.0045, 0.03, (0.02, math.cos(a) * 0.011, math.sin(a) * 0.011), 'X', verts=12, bevel=0.0008), "brass")


ATTS = {"sight": sight, "nightglass": nightglass, "eyeglass": eyeglass, "lodestone": lodestone, "compensator": compensator,
        "choke": choke, "baffle": baffle, "bayonet": bayonet, "extmag": extmag, "drum": drum, "steamfeed": steamfeed,
        "oilskin": oilskin, "speedloader": speedloader}

a = C.args()
only = a[a.index("--only") + 1].split(",") if "--only" in a else None
out = C.out_dir()
for aid, fn in ATTS.items():
    if only and aid not in only:
        continue
    build(aid, fn, out)
