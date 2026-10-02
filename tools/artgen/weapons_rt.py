# Red Tide's weapons (the Red Tide Visual Overhaul Spec, "Weapons"), built with the shared gun kit (gunkit.py): every
# gun a machine someone could have built in 1910 to fire under 30 m of water: gas bulbs and reservoirs with valves and
# gauges, rubber seals at every joint, drain ports, sealed breech covers, lanyard rings; glass cartridges of steel
# needles; sealed brass clips. Moving parts are separate pivoted objects (their group drives them in the game: hammer,
# trigger, cylinder, latch, bolt, pump, gauge, clip, load). Invented maker's marks only, never real trademarks.
#
# Frame: +X along the barrel (the muzzle), +Z up, +Y the gun's left; metres. Markers: grip_r (the right fist), grip_l
# (the other hand), muzzle, eject, sight, mount_muzzle, mount_top.
#     blender -b --factory-startup -P tools/artgen/weapons_rt.py -- --out assets/redtide/weapons [--only cormorant,gannet]

import os
import sys
import math
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import common as C
import gunkit as K
from weapons_side import common_marks, grip_down
bpy = C.bpy


def seal(W, name, x, z, r, y=0.0, axis='X'):
    """A black rubber O-ring at a joint (the underwater guns have one at every seam)."""
    W.add(W.ring(name, (x, y, z), r, max(0.0012, r * 0.14), axis), "rubber")


def gas_bulb(W, name, x, z, r, length, y=0.0, axis='X', valve=True):
    """A brass gas reservoir: a capsule with a knurled valve cap and a seal."""
    W.add(W.cyl(name, r, length, (x, y, z), axis, verts=32, bevel=r * 0.35), "brass")
    if valve:
        off = length / 2 + 0.006
        p = (x + off, y, z) if axis == 'X' else (x, y, z + off)
        W.add(W.cyl(name + "_valve", r * 0.55, 0.012, p, axis, verts=18, bevel=0.0008), "steel")
        seal(W, name + "_seal", p[0] - (0.006 if axis == 'X' else 0), p[2] - (0.006 if axis == 'Z' else 0), r * 0.62, y, axis)


def gauge(W, name, loc, r=0.009, axis='Y', group="gauge"):
    """A pressure gauge: a brass bezel, a white face, a needle that drops as the gas is used."""
    W.add(W.cyl(name, r, 0.006, loc, axis, verts=24, bevel=0.0008), "brass")
    face = (loc[0], loc[1] + (0.0032 if axis == 'Y' else 0), loc[2])
    W.add(W.cyl(name + "_face", r * 0.82, 0.0012, face, axis, verts=24, bevel=0), "bone")
    nd = W.box(name + "_needle", (0.0012, 0.0008, r * 0.7), (face[0], face[1] + 0.0008, face[2] + r * 0.3), bevel=0)
    W.add(nd, "red", group, axis=(0, 1, 0), amount=-1.6, pivot=(face[0], face[1] + 0.0008, face[2]))


def lanyard(W, x, z):
    W.add(W.ring("lanyard_ring", (x, 0, z), 0.007, 0.0014, 'Y'), "steel")


def stamp(W, name, loc, size=(0.02, 0.0004, 0.005)):
    """An engraved maker's panel (the texture bake carries no text: a recessed plate stands in for it)."""
    W.add(W.box(name, size, loc, bevel=0.0002), "case")


# ---------------------------------------------------------------- the Cormorant: a compact brass gas revolver
def build_cormorant(W):
    # the frame and a short barrel with a bubble vent near the muzzle
    W.add(W.box("frame", (0.07, 0.022, 0.034), (0.0, 0, 0.03), bevel=0.004, segments=3), "brass")
    W.add(W.barrel("barrel", 0.035, 0.15, 0.04, 0.0075, bore=0.003), "steel")
    W.add(W.box("vent_rib", (0.08, 0.006, 0.004), (0.1, 0, 0.049), bevel=0.001), "steel")
    for k in range(3):
        W.add(W.cyl(f"vent{k}", 0.0016, 0.004, (0.12 + k * 0.009, 0, 0.051), 'Z', verts=10, bevel=0), "rubber")
    seal(W, "barrel_seal", 0.036, 0.04, 0.009)
    # the six-dart cylinder (it turns a sixth per shot) on a crane that swings out to the left for the reload
    cyl = W.cyl("cylinder", 0.0165, 0.038, (0.012, 0, 0.04), 'X', verts=36, bevel=0.002)
    W.add(cyl, "steel", "cylinder", axis=(1, 0, 0), amount=math.pi / 3, pivot=(0.012, 0, 0.04))
    for k in range(6):
        a = k * math.pi / 3
        dart = W.cyl(f"dart{k}", 0.0035, 0.008, (-0.009, 0.0095 * math.cos(a), 0.04 + 0.0095 * math.sin(a)), 'X', verts=10, bevel=0.0004)
        W.add(dart, "copper", "cylinder", axis=(1, 0, 0), amount=math.pi / 3, pivot=(0.012, 0, 0.04))
    W.add(W.box("crane", (0.008, 0.004, 0.012), (0.03, 0.012, 0.03), bevel=0.001), "steel", "latch", axis=(1, 0, 0), amount=0.9, pivot=(0.03, 0.012, 0.024))
    # the grip with its gas bulb inside (a brass cap at the butt) and a pressure needle on the side
    W.add(grip_down(W, "grip", "walnut", -0.03, 0.02, 0.07, 72, 0.014, 0.024), "walnut")
    W.add(W.cyl("bulb_cap", 0.011, 0.012, (-0.052, 0, -0.045), 'Z', verts=24, bevel=0.002), "brass")
    seal(W, "bulb_seal", -0.052, -0.038, 0.012, 0, 'Z')
    gauge(W, "gauge", (-0.016, 0.0125, 0.034), 0.0075)
    W.add(W.box("hammer", (0.01, 0.007, 0.022), (-0.03, 0, 0.055), bevel=0.0025, rot=(0, math.radians(-18), 0)), "case",
          "hammer", axis=(0, 1, 0), amount=-0.5, pivot=(-0.026, 0, 0.044))
    W.trigger_and_guard(-0.008, 0.014)
    W.sights(0.145, -0.022, 0.0475)
    lanyard(W, -0.062, -0.05)
    stamp(W, "maker", (0.0, 0.0115, 0.024))
    common_marks(W, (-0.04, 0, -0.005), (0.15, 0, 0.04), eject=(0.012, 0.03, 0.04), left=(-0.045, 0, -0.03))


# ---------------------------------------------------------------- the Gannet: the Cormorant's bigger brother
def build_gannet(W):
    W.add(W.box("frame", (0.085, 0.024, 0.038), (0.0, 0, 0.03), bevel=0.004, segments=3), "brass")
    W.add(W.barrel("barrel", 0.04, 0.23, 0.042, 0.0085, bore=0.0032), "steel")
    # a bubble-vent shroud over the forward half of the barrel
    W.add(W.cyl("shroud", 0.0125, 0.1, (0.17, 0, 0.042), 'X', verts=32, bevel=0.0015, hole=0.009), "brass")
    for k in range(5):
        W.add(W.cyl(f"shroud_vent{k}", 0.0018, 0.004, (0.135 + k * 0.017, 0, 0.054), 'Z', verts=10, bevel=0), "rubber")
    seal(W, "shroud_seal", 0.12, 0.042, 0.0128)
    # the ten-dart rotary magazine (a drum turning a tenth per shot)
    W.add(W.cyl("drum", 0.022, 0.034, (0.016, 0, 0.042), 'X', verts=40, bevel=0.002), "steel", "drum", axis=(1, 0, 0), amount=math.pi / 5, pivot=(0.016, 0, 0.042))
    for k in range(10):
        a = k * math.pi / 5
        W.add(W.cyl(f"dart{k}", 0.003, 0.006, (-0.002, 0.014 * math.cos(a), 0.042 + 0.014 * math.sin(a)), 'X', verts=8, bevel=0.0003), "copper",
              "drum", axis=(1, 0, 0), amount=math.pi / 5, pivot=(0.016, 0, 0.042))
    # the screw-in gas cartridge under the barrel, with its knurled cap and seal
    gas_bulb(W, "cartridge", 0.12, 0.018, 0.0085, 0.11)
    W.add(W.box("cartridge_lug", (0.012, 0.01, 0.012), (0.06, 0, 0.026), bevel=0.002), "brass")
    W.add(grip_down(W, "grip", "walnut", -0.035, 0.018, 0.08, 72, 0.015, 0.026), "walnut")
    gauge(W, "gauge", (-0.018, 0.0135, 0.036), 0.008)
    W.add(W.box("hammer", (0.011, 0.008, 0.024), (-0.036, 0, 0.058), bevel=0.0025, rot=(0, math.radians(-18), 0)), "case",
          "hammer", axis=(0, 1, 0), amount=-0.5, pivot=(-0.032, 0, 0.046))
    W.trigger_and_guard(-0.01, 0.013)
    W.sights(0.225, -0.028, 0.0505)
    lanyard(W, -0.07, -0.058)
    stamp(W, "maker", (0.005, 0.0125, 0.022))
    common_marks(W, (-0.046, 0, -0.008), (0.23, 0, 0.042), eject=(0.016, 0.03, 0.042), left=(0.1, 0, 0.004))


# ---------------------------------------------------------------- the Needler Mk I: a needle SMG fed from a glass cartridge
def build_needler1(W):
    W.add(W.box("receiver", (0.16, 0.026, 0.04), (0.0, 0, 0.03), bevel=0.004, segments=3), "steel")
    W.add(W.barrel("barrel", 0.08, 0.3, 0.036, 0.006, bore=0.0018), "steel")
    # the cooling-and-bubble shroud: a perforated sleeve
    W.add(W.cyl("shroud", 0.0135, 0.15, (0.22, 0, 0.036), 'X', verts=32, bevel=0.0015, hole=0.01), "brass")
    for k in range(6):
        for s in (-1, 1):
            W.add(W.cyl(f"perf{k}{s}", 0.0022, 0.004, (0.16 + k * 0.022, s * 0.012, 0.036), 'Y', verts=10, bevel=0), "rubber")
    seal(W, "shroud_seal", 0.145, 0.036, 0.0138)
    # the glass cartridge of steel needles on top, held in a brass cradle; it slides back out for the reload
    clip = W.box("cartridge", (0.07, 0.016, 0.026), (0.0, 0, 0.066), bevel=0.003, segments=3)
    W.add(clip, "glass", "clip", kind="slide", axis=(0, 0, 1), amount=0.06)
    for k in range(7):
        W.add(W.box(f"needle{k}", (0.062, 0.0014, 0.0014), (0.0, -0.005 + (k % 4) * 0.0033, 0.06 + (k // 4) * 0.008), bevel=0), "bright",
              "clip", kind="slide", axis=(0, 0, 1), amount=0.06)
    W.add(W.box("cradle", (0.076, 0.02, 0.006), (0.0, 0, 0.051), bevel=0.0015), "brass")
    # the gas-bottle stock behind, with its gauge and valve
    gas_bulb(W, "stock_bottle", -0.16, 0.03, 0.017, 0.14)
    W.add(W.box("butt", (0.02, 0.03, 0.07), (-0.235, 0, 0.022), bevel=0.006, segments=3), "rubber")
    gauge(W, "gauge", (-0.12, 0.018, 0.045), 0.009)
    W.add(grip_down(W, "grip", "walnut", -0.01, 0.012, 0.075, 76, 0.014, 0.026), "walnut")
    W.add(W.box("fore_grip", (0.02, 0.018, 0.05), (0.12, 0, 0.0), bevel=0.004, segments=3), "walnut")
    W.trigger_and_guard(0.012, 0.01)
    W.sights(0.29, 0.04, 0.0445, bead=False)
    stamp(W, "maker", (0.02, 0.0135, 0.022))
    common_marks(W, (-0.02, 0, -0.01), (0.3, 0, 0.036), eject=(0.0, 0.03, 0.066), left=(0.12, 0, -0.02))


# ---------------------------------------------------------------- the Sea-Pattern Carbine: a service carbine sealed for water
def build_carbine(W):
    W.add(W.barrel("barrel", 0.05, 0.52, 0.03, 0.0085, bore=0.0035), "steel")
    W.add(W.box("receiver", (0.13, 0.026, 0.034), (0.0, 0, 0.03), bevel=0.003, segments=3), "steel")
    # the rubber-sealed breech cover over the bolt, and the bolt with a rubber-booted handle
    W.add(W.box("breech_cover", (0.07, 0.03, 0.012), (0.0, 0, 0.052), bevel=0.004, segments=3), "rubber")
    bolt = W.cyl("bolt", 0.006, 0.06, (0.0, -0.016, 0.04), 'X', verts=16, bevel=0.001)
    W.add(bolt, "bright", "bolt", kind="slide", axis=(-1, 0, 0), amount=0.05)
    W.add(W.sphere("bolt_knob", (-0.01, -0.03, 0.038), (0.008, 0.008, 0.008), 16), "rubber", "bolt", kind="slide", axis=(-1, 0, 0), amount=0.05)
    # the stock and fore-end in oiled walnut, a barrel band, a brass butt plate
    stock = W.loft("stock", [(-0.06, 0.0, 0.012, 0.022), (-0.18, -0.02, 0.016, 0.03), (-0.34, -0.045, 0.019, 0.045)], 20)
    W.add(stock, "walnut")
    W.add(W.box("butt_plate", (0.008, 0.04, 0.092), (-0.345, 0, -0.045), bevel=0.003), "brass")
    W.add(W.box("fore_end", (0.24, 0.026, 0.022), (0.17, 0, 0.018), bevel=0.006, segments=3), "walnut")
    W.add(W.ring("band", (0.28, 0, 0.026), 0.015, 0.0025, 'X'), "brass")
    seal(W, "breech_seal", 0.06, 0.03, 0.011)
    # the sealed brass clip below the receiver (it ejects and sinks on a reload) and a rubber muzzle cap on a lanyard
    W.add(W.box("clip", (0.05, 0.016, 0.03), (0.0, 0, 0.0), bevel=0.003), "brass", "clip", kind="slide", axis=(0, 0, -1), amount=0.08)
    W.add(W.cyl("muzzle_cap", 0.0105, 0.016, (0.53, 0, 0.03), 'X', verts=20, bevel=0.002), "rubber")
    W.add(W.tube("cap_cord", [(0.53, 0.01, 0.03), (0.5, 0.016, 0.02), (0.47, 0.012, 0.022)], 0.0012), "rope")
    W.trigger_and_guard(-0.03, 0.012)
    W.sights(0.505, 0.03, 0.039)
    stamp(W, "maker", (0.0, 0.0135, 0.022))
    common_marks(W, (-0.045, 0, -0.005), (0.53, 0, 0.03), eject=(0.0, 0.03, 0.04), left=(0.17, 0, 0.0))


# ---------------------------------------------------------------- Flechette 12: a pump scatter gun for flechette shells
def build_flechette12(W):
    W.add(W.barrel("barrel", 0.05, 0.46, 0.036, 0.011, bore=0.008), "steel")
    W.add(W.cyl("tube_mag", 0.0095, 0.32, (0.21, 0, 0.012), 'X', verts=24, bevel=0.0015), "steel")
    W.add(W.box("receiver", (0.14, 0.03, 0.05), (0.0, 0, 0.026), bevel=0.004, segments=3), "steel")
    # the pump (fore-end) slides back and forth through a reload
    W.add(W.cyl("pump", 0.017, 0.12, (0.2, 0, 0.012), 'X', verts=24, bevel=0.004, hole=0.01), "walnut", "pump", kind="slide", axis=(-1, 0, 0), amount=0.06)
    for k in range(5):
        W.add(W.ring(f"pump_rib{k}", (0.15 + k * 0.025, 0, 0.012), 0.0175, 0.0015, 'X'), "rubber", "pump", kind="slide", axis=(-1, 0, 0), amount=0.06)
    # the loading gate under the receiver and a shell going in, red-hulled with a brass head
    W.add(W.box("gate", (0.04, 0.016, 0.003), (0.01, 0, 0.0), bevel=0.0008), "steel", "latch", axis=(0, 1, 0), amount=0.5, pivot=(0.03, 0, 0.0))
    W.add(W.cyl("shell", 0.008, 0.05, (0.0, 0, -0.008), 'X', verts=18, bevel=0.001), "red", "load", kind="show")
    W.add(W.cyl("shell_head", 0.0085, 0.008, (-0.024, 0, -0.008), 'X', verts=18, bevel=0.0008), "brass", "load", kind="show")
    stock = W.loft("stock", [(-0.07, 0.0, 0.014, 0.026), (-0.2, -0.025, 0.017, 0.034), (-0.36, -0.05, 0.02, 0.05)], 20)
    W.add(stock, "walnut")
    W.add(W.box("pad", (0.012, 0.042, 0.1), (-0.366, 0, -0.05), bevel=0.004), "rubber")
    seal(W, "mag_seal", 0.37, 0.012, 0.0098)
    W.add(W.cyl("drain", 0.003, 0.006, (0.42, 0, 0.025), 'Z', verts=10, bevel=0), "brass")
    W.trigger_and_guard(-0.03, 0.006)
    W.sights(0.455, 0.05, 0.047)
    stamp(W, "maker", (0.0, 0.0155, 0.03))
    common_marks(W, (-0.05, 0, -0.008), (0.46, 0, 0.036), eject=(0.0, 0.03, 0.04), left=(0.2, 0, 0.0))


# ---------------------------------------------------------------- the Long Speargun: a pneumatic speargun with a barbed spear
def build_longspeargun(W):
    # the barrel: a brass air cylinder the spear sits in, a rail under it, the cocking strain shown in the bands
    W.add(W.cyl("air_tube", 0.012, 0.7, (0.3, 0, 0.03), 'X', verts=32, bevel=0.002), "brass")
    W.add(W.box("rail", (0.66, 0.012, 0.01), (0.31, 0, 0.012), bevel=0.002), "walnut")
    for k in range(4):
        seal(W, f"band{k}", 0.05 + k * 0.17, 0.03, 0.0125)
    W.add(W.cyl("muzzle_head", 0.016, 0.03, (0.655, 0, 0.03), 'X', verts=28, bevel=0.003), "brass")
    # the spear: a steel shaft out of the muzzle with a barbed head and a line to the reel (hidden once fired)
    W.add(W.cyl("shaft", 0.0035, 0.5, (0.74, 0, 0.03), 'X', verts=12, bevel=0.0005), "bright", "load", kind="show")
    W.add(W.cone("tip", 0.0055, 0.0, 0.05, (1.015, 0, 0.03), 'X', verts=12), "bright", "load", kind="show")
    for s in (-1, 1):
        barb = W.box(f"barb{s}", (0.022, 0.0016, 0.004), (0.98, s * 0.006, 0.03), bevel=0, rot=(0, 0, math.radians(s * 22)))
        W.add(barb, "bright", "load", kind="show")
    W.add(W.tube("line", [(0.55, 0.0, 0.034), (0.3, -0.015, 0.0), (0.0, -0.018, -0.01)], 0.0011), "rope")
    W.add(W.cyl("reel", 0.026, 0.02, (0.02, -0.025, -0.01), 'Y', verts=28, bevel=0.002), "brass")
    # the pistol grip, the pump lever to charge the tube (both arms strain on it)
    W.add(grip_down(W, "grip", "rubber", -0.04, 0.008, 0.09, 78, 0.016, 0.028), "rubber")
    W.add(W.box("charge_lever", (0.14, 0.01, 0.01), (0.12, 0, -0.004), bevel=0.003), "steel", "pump", axis=(0, 1, 0), amount=0.6, pivot=(0.19, 0, -0.004))
    gauge(W, "gauge", (-0.0, 0.014, 0.046), 0.009)
    W.trigger_and_guard(-0.015, 0.0)
    W.add(W.box("butt", (0.06, 0.026, 0.05), (-0.07, 0, 0.026), bevel=0.008, segments=3), "rubber")
    stamp(W, "maker", (0.2, 0.0125, 0.016))
    common_marks(W, (-0.05, 0, -0.012), (0.67, 0, 0.03), left=(0.2, 0, 0.004), sight=(-0.2, 0, 0.05))


# ---------------------------------------------------------------- melee: the diver's knife and the Boarding Axe
def build_knife(W):
    blade = W.loft("blade", [(0.0, 0.0, 0.004, 0.022), (0.08, 0.0, 0.003, 0.02), (0.15, 0.004, 0.0015, 0.008), (0.17, 0.006, 0.0004, 0.0012)], 14)
    W.add(blade, "bright")
    W.add(W.box("guard", (0.008, 0.012, 0.05), (-0.004, 0, 0.0), bevel=0.0015), "brass")
    W.add(W.cyl("grip", 0.013, 0.1, (-0.058, 0, 0.0), 'X', verts=20, bevel=0.003), "rubber")
    for k in range(4):
        W.add(W.ring(f"grip_ring{k}", (-0.03 - k * 0.02, 0, 0.0), 0.0135, 0.0015, 'X'), "rubber")
    W.add(W.sphere("pommel", (-0.112, 0, 0.0), (0.01, 0.014, 0.014), 16), "brass")
    common_marks(W, (-0.06, 0, 0.0), (0.17, 0, 0.004))


def build_boardingaxe(W):
    W.add(W.cyl("haft", 0.014, 0.6, (0.12, 0, 0.0), 'X', verts=20, bevel=0.003), "laminate")   # (a tarred haft)
    # the head: a rusted iron wedge, its bit flaring down to the edge, a spike behind for prising hatches
    W.add(W.box("eye", (0.05, 0.03, 0.04), (0.4, 0, 0.0), bevel=0.004, segments=2), "iron")
    bit = W.loft("bit", [(0.0, 0.0, 0.012, 0.022), (0.05, -0.01, 0.006, 0.045), (0.09, -0.02, 0.0015, 0.065)], 14)
    bit.rotation_euler = (0, math.radians(90), 0); bit.location = (0.4, 0, -0.01)
    C.select_only([bit]); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    W.add(bit, "iron")
    W.add(W.cone("spike", 0.012, 0.0, 0.07, (0.4, 0, 0.055), 'Z', verts=12), "iron")
    for k in range(5):
        W.add(W.ring(f"wrap{k}", (-0.12 + k * 0.02, 0, 0.0), 0.0145, 0.002, 'X'), "leather")
    common_marks(W, (-0.1, 0, 0.0), (0.4, 0, -0.08))

RECIPES = {n[6:]: f for n, f in globals().items() if n.startswith("build_")}

if __name__ == "__main__":
    a = C.args()
    only = a[a.index("--only") + 1].split(",") if "--only" in a else None
    out = C.out_dir()
    for wid, fn in RECIPES.items():
        if only and wid not in only:
            continue
        W = K.Weapon(wid)
        fn(W)
        W.finish(out, size=getattr(fn, "texture", 1024))
