# Phase 1 test gun (the Trawl Visual Overhaul Spec): an 1890s-style lever-action saddle carbine, about 95 cm long,
# built part by part (receiver, barrel with a bore, magazine tube, barrel band, fore-end, stock with a wrist and
# comb, butt plate, lever loop, trigger, hammer, sights, loading gate, saddle ring, screws), bevelled, with blued and
# case-hardened steel, brass and oiled walnut baked into one PBR texture set.
#
# Frame: Blender +X is the muzzle, +Z up, +Y the gun's left. The origin sits at the trigger. Metres.
#     blender -b --factory-startup -P tools/artgen/test_carbine.py -- --out assets/shared/test

import os
import sys
import math
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import common as C

sc = C.reset()
steel = C.mat_blued_steel()
case = C.mat_case_hardened()
brass = C.mat_brass()
walnut = C.mat_walnut()
parts = []

def add(o, m):
    C.assign(o, m)
    parts.append(o)
    return o

# the receiver: a flat-sided steel box with rounded edges, case-hardened
rec = add(C.bevelled_box("receiver", (0.19, 0.03, 0.068), loc=(0.03, 0, 0.0), bevel=0.004, segments=3), case)
# its top tang running back into the wrist, and the lower tang under it
add(C.bevelled_box("upper_tang", (0.06, 0.016, 0.006), loc=(-0.085, 0, 0.026), bevel=0.002), case)
add(C.bevelled_box("lower_tang", (0.07, 0.014, 0.006), loc=(-0.08, 0, -0.03), bevel=0.002), case)
# the loading gate on the right side (-Y), a spring plate
add(C.bevelled_box("loading_gate", (0.045, 0.002, 0.018), loc=(0.07, -0.0158, -0.008), bevel=0.0008), steel)
# side-plate screws, slotted
for i, (x, z) in enumerate([(-0.02, 0.012), (0.05, 0.016), (0.09, -0.018)]):
    for side in (-1, 1):
        s = add(C.cylinder(f"screw{i}{side}", 0.0035, 0.002, loc=(x, side * 0.0155, z), rot=(math.pi / 2, 0, 0), verts=16, bevel=0.0004), steel)
        slot = C.bevelled_box(f"slot{i}{side}", (0.0062, 0.003, 0.0008), loc=(x, side * 0.0165, z), bevel=0)
        b = s.modifiers.new("slot", 'BOOLEAN'); b.object = slot; b.operation = 'DIFFERENCE'
        C.select_only([s]); C.bpy.ops.object.modifier_apply(modifier=b.name); C.bpy.data.objects.remove(slot)
# the saddle ring on the left (+Y), on its stud
add(C.cylinder("ring_stud", 0.004, 0.006, loc=(-0.035, 0.018, -0.005), rot=(math.pi / 2, 0, 0), verts=12), steel)
ring_pts = [(-0.035 + 0.016 * math.cos(a), 0.021, -0.005 - 0.016 * math.sin(a) - 0.016) for a in [k * math.pi / 8 for k in range(17)]]
add(C.tube_along("saddle_ring", ring_pts, 0.0022), steel)

# the barrel (round, a carbine's), bored, with a crowned muzzle, and the magazine tube under it
bar = add(C.cylinder("barrel", 0.0105, 0.5, loc=(0.375, 0, 0.012), rot=(0, math.pi / 2, 0), verts=32, bevel=0.0015, hole=0.0046), steel)
add(C.cylinder("magazine", 0.0088, 0.45, loc=(0.35, 0, -0.012), rot=(0, math.pi / 2, 0), verts=24, bevel=0.0015), steel)
add(C.cylinder("mag_cap", 0.0092, 0.012, loc=(0.578, 0, -0.012), rot=(0, math.pi / 2, 0), verts=24, bevel=0.002), steel)
# the barrel band near the muzzle, round both tubes, with its screw
band = add(C.bevelled_box("barrel_band", (0.014, 0.025, 0.052), loc=(0.555, 0, 0.0), bevel=0.006, segments=4), steel)
# the fore-end cap and the fore-end: walnut round the barrel and the tube
add(C.bevelled_box("forend_cap", (0.01, 0.03, 0.05), loc=(0.317, 0, -0.002), bevel=0.004, segments=3), steel)
add(C.lofted("forend", [(0.125, -0.002, 0.0155, 0.026), (0.2, -0.002, 0.0158, 0.026), (0.31, -0.002, 0.0152, 0.025)], segs=18), walnut)

# sights: a front blade with a brass bead on a ramp, and a ladder rear sight on its base
add(C.bevelled_box("front_ramp", (0.016, 0.006, 0.006), loc=(0.6, 0, 0.0245), bevel=0.0015), steel)
add(C.bevelled_box("front_blade", (0.006, 0.0018, 0.008), loc=(0.6, 0, 0.031), bevel=0.0005), steel)
add(C.cylinder("bead", 0.0016, 0.002, loc=(0.6, 0, 0.0355), rot=(0, math.pi / 2, 0), verts=12, bevel=0), brass)
add(C.bevelled_box("rear_base", (0.03, 0.012, 0.004), loc=(0.24, 0, 0.0235), bevel=0.0012), steel)
add(C.bevelled_box("rear_ladder", (0.003, 0.012, 0.016), loc=(0.236, 0, 0.032), bevel=0.0008, rot=(0, math.radians(-12), 0)), steel)

# the hammer (a spur at the back of the receiver, cocked back) and the trigger
ham = add(C.bevelled_box("hammer", (0.012, 0.007, 0.034), loc=(-0.07, 0, 0.038), bevel=0.0025, rot=(0, math.radians(-35), 0)), case)
add(C.bevelled_box("hammer_spur", (0.02, 0.009, 0.005), loc=(-0.087, 0, 0.054), bevel=0.0018, rot=(0, math.radians(-15), 0)), case)
add(C.tube_along("trigger", [(-0.004, 0, -0.033), (-0.008, 0, -0.045), (-0.014, 0, -0.053), (-0.022, 0, -0.056)], 0.0026), steel)
# the lever: a flat bar under the receiver into a finger loop round the trigger, and back up to the tang
lever = [(0.09, 0, -0.035), (0.04, 0, -0.04), (0.012, 0, -0.04), (0.0, 0, -0.045), (-0.004, 0, -0.065),
         (-0.02, 0, -0.084), (-0.045, 0, -0.092), (-0.075, 0, -0.088), (-0.098, 0, -0.07), (-0.1, 0, -0.052),
         (-0.088, 0, -0.04), (-0.062, 0, -0.037)]
add(C.tube_along("lever", lever, 0.0042), steel)

# the stock: a slim wrist, a comb, a dropping butt; the steel butt plate
stock = add(C.lofted("stock", [(-0.06, -0.004, 0.0145, 0.026), (-0.095, -0.007, 0.0135, 0.019), (-0.14, -0.012, 0.016, 0.026),
                               (-0.22, -0.022, 0.019, 0.04), (-0.31, -0.034, 0.021, 0.054), (-0.39, -0.045, 0.021, 0.064),
                               (-0.403, -0.046, 0.0205, 0.064)], segs=20), walnut)
add(C.bevelled_box("butt_plate", (0.006, 0.044, 0.13), loc=(-0.406, 0, -0.046), bevel=0.0025, segments=3), steel)
for z in (-0.005, -0.087):
    add(C.cylinder(f"butt_screw{z}", 0.003, 0.002, loc=(-0.41, 0, z), rot=(0, math.pi / 2, 0), verts=12, bevel=0.0004), steel)

for o in parts:
    C.apply_all(o)
    C.smooth(o, 35)

C.bake_pbr(parts, "carbine_test", size=2048)
out = C.out_dir()
C.export_glb(parts, os.path.join(out, "carbine_test.glb"))
