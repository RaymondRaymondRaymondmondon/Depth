# Long guns and specials (the Visual Overhaul Spec's per-weapon table): coach shotgun, lever carbine, chatter gun,
# long rifle (scoped), nitro express, tribal longbow, punt gun, steam riveter, blunderbuss, air rifle, galvanic prod,
# rocket harpoon. Frame and conventions: gunkit.py. The origin is the trigger; the right hand closes on the wrist
# behind it (grip_r), the left on the fore-end (grip_l).

import math
import gunkit as K
from gunkit import bpy, C
from weapons_side import bore_cut, spear


def marks(W, grip, left, muzzle, sight, eject=(0.0, 0, 0.02), top=None, under=None):
    W.mark("grip_r", grip); W.mark("grip_l", left); W.mark("muzzle", muzzle); W.mark("eject", eject)
    W.mark("sight", sight); W.mark("mount_muzzle", muzzle)
    W.mark("mount_top", top if top else (muzzle[0] * 0.4, 0, muzzle[2] + 0.02))
    W.mark("mount_under", under if under else (left[0] + 0.05, 0, left[2] - 0.02))


def stock(W, name, mat, x0, length, drop=0.04, wrist=0.016, butt=0.062, width=0.021, z0=-0.005):
    """A rifle or shotgun butt-stock: a slim wrist behind the action, the comb, dropping to a tall butt; its butt plate."""
    s = W.loft(name, [(x0, z0, width * 0.68, wrist * 1.5), (x0 - 0.04, z0 - 0.004, width * 0.64, wrist), (x0 - 0.1, z0 - 0.012, width * 0.82, butt * 0.45),
                      (x0 - length * 0.6, z0 - drop * 0.7, width, butt * 0.85), (x0 - length, z0 - drop, width, butt)], 20)
    W.add(s, mat); W.chequer(s, 700, 0.2)
    W.add(W.box(name + "_plate", (0.006, width * 2.05, butt * 2.02), (x0 - length - 0.003, 0, z0 - drop), bevel=0.003, segments=3), "steel")


def hammer_lock(W, name, x, y, z, group="hammer"):
    W.add(W.box(name, (0.011, 0.006, 0.028), (x - 0.004, y, z + 0.016), bevel=0.0025, rot=(0, math.radians(-25), 0)), "case",
          group, axis=(0, 1, 0), amount=-0.55, pivot=(x, y, z + 0.004))
    W.add(W.box(name + "_spur", (0.016, 0.007, 0.005), (x - 0.016, y, z + 0.032), bevel=0.0018, rot=(0, math.radians(-15), 0)), "case",
          group, axis=(0, 1, 0), amount=-0.55, pivot=(x, y, z + 0.004))


# ---------------------------------------------------------------- coach shotgun (a side-by-side with external hammers)
def build_shotgun(W):
    hinge = (0.065, 0, -0.004)
    br = dict(group="break", axis=(0, 1, 0), amount=0.6, pivot=hinge)
    for s in (-1, 1):
        W.add(W.barrel(f"barrel{s}", 0.06, 0.52, 0.016, 0.0105, bore=0.0092, y=s * 0.0104), "steel", **br)
        sh = W.add(W.cyl(f"shell{s}", 0.0104, 0.004, (0.0615, s * 0.0104, 0.016), 'X', verts=20, bevel=0.0006), "brass", "load", axis=(1, 0, 0), kind="show", pivot=hinge)
        sh["parent"] = "break"
    W.add(W.box("rib", (0.46, 0.009, 0.006), (0.29, 0, 0.027), bevel=0.0018), "steel", **br)
    W.add(W.box("lump", (0.05, 0.02, 0.016), (0.085, 0, 0.0), bevel=0.003), "steel", **br)
    W.add(W.loft("fore_end", [(0.09, 0.0, 0.017, 0.012), (0.28, 0.002, 0.016, 0.011)], 16), "walnut", **br)
    W.add(W.sphere("bead", (0.515, 0, 0.0315), (0.0018, 0.0018, 0.0018), 10), "brass", **br)
    W.add(W.box("action", (0.09, 0.044, 0.04), (0.02, 0, 0.008), bevel=0.005, segments=3), "case")
    W.add(W.box("top_lever", (0.04, 0.012, 0.006), (-0.03, 0.004, 0.031), bevel=0.0025, rot=(0, 0, math.radians(8))), "case",
          "latch", axis=(0, 0, 1), amount=0.7, pivot=(-0.01, 0, 0.031))
    for s in (-1, 1):
        W.add(W.box(f"lock{s}", (0.07, 0.004, 0.03), (-0.02, s * 0.0225, 0.004), bevel=0.0015), "silver")
        hammer_lock(W, f"hammer{s}", -0.03, s * 0.019, 0.016, "hammer" if s > 0 else "hammer2")
    W.trigger_and_guard(0.0, -0.012)
    W.add(W.tube("trigger2", [(0.014, 0, -0.012), (0.01, 0, -0.024), (0.004, 0, -0.032)], 0.0022), "steel")
    stock(W, "stock", "walnut", -0.045, 0.33, drop=0.05)
    marks(W, (-0.075, 0, -0.022), (0.2, 0, -0.008), (0.52, 0, 0.016), (-0.2, 0, 0.042), eject=(0.065, 0, 0.016))


# ---------------------------------------------------------------- lever carbine (an 1890s-style saddle carbine)
def build_carbine(W):
    W.add(W.box("receiver", (0.19, 0.03, 0.068), (0.03, 0, 0.0), bevel=0.004, segments=3), "case")
    W.add(W.box("upper_tang", (0.06, 0.016, 0.006), (-0.085, 0, 0.026), bevel=0.002), "case")
    W.add(W.box("lower_tang", (0.07, 0.014, 0.006), (-0.08, 0, -0.03), bevel=0.002), "case")
    W.add(W.box("loading_gate", (0.045, 0.002, 0.018), (0.07, -0.0158, -0.008), bevel=0.0008), "steel", "latch", axis=(0, 0, 1), amount=0.0, kind="rot", pivot=(0.09, -0.0158, -0.008))
    for i, (x, z) in enumerate([(-0.02, 0.012), (0.05, 0.016), (0.09, -0.018)]):
        for side in (-1, 1):
            W.add(W.screw(f"screw{i}{side}", (x, side * 0.0155, z), side), "bright")
    W.add(W.cyl("ring_stud", 0.004, 0.006, (-0.035, 0.018, -0.005), 'Y', verts=12), "steel")
    W.add(W.tube("saddle_ring", [(-0.035 + 0.016 * math.cos(k * math.pi / 8), 0.021, -0.021 - 0.016 * math.sin(k * math.pi / 8)) for k in range(17)], 0.0022), "steel")
    W.add(W.barrel("barrel", 0.125, 0.625, 0.012, 0.0105, bore=0.0046), "steel")
    W.add(W.cyl("magazine", 0.0088, 0.45, (0.35, 0, -0.012), 'X', verts=24, bevel=0.0015), "steel")
    W.add(W.cyl("mag_cap", 0.0092, 0.012, (0.578, 0, -0.012), 'X', verts=24, bevel=0.002), "steel")
    W.add(W.box("barrel_band", (0.014, 0.025, 0.052), (0.555, 0, 0.0), bevel=0.006, segments=4), "steel")
    W.add(W.box("forend_cap", (0.01, 0.03, 0.05), (0.317, 0, -0.002), bevel=0.004, segments=3), "steel")
    W.add(W.loft("forend", [(0.125, -0.002, 0.0155, 0.026), (0.2, -0.002, 0.0158, 0.026), (0.31, -0.002, 0.0152, 0.025)], 18), "walnut")
    W.add(W.box("front_ramp", (0.016, 0.006, 0.006), (0.6, 0, 0.0245), bevel=0.0015), "steel")
    W.add(W.box("front_blade", (0.006, 0.0018, 0.008), (0.6, 0, 0.031), bevel=0.0005), "steel")
    W.add(W.sphere("bead", (0.6, 0, 0.0355), (0.0016, 0.0016, 0.0016), 10), "brass")
    W.add(W.box("rear_base", (0.03, 0.012, 0.004), (0.24, 0, 0.0235), bevel=0.0012), "steel")
    W.add(W.box("rear_ladder", (0.003, 0.012, 0.016), (0.236, 0, 0.032), bevel=0.0008, rot=(0, math.radians(-12), 0)), "steel")
    # the bolt on top of the receiver runs back with the lever; the hammer is cocked by it
    W.add(W.box("bolt", (0.05, 0.012, 0.01), (0.0, 0, 0.034), bevel=0.002), "bright", "bolt", axis=(1, 0, 0), amount=-0.045, kind="slide", pivot=(0.0, 0, 0.034))
    hammer_lock(W, "hammer", -0.066, 0, 0.026)
    W.add(W.tube("trigger", [(-0.004, 0, -0.033), (-0.008, 0, -0.045), (-0.014, 0, -0.053), (-0.022, 0, -0.056)], 0.0026), "steel",
          "trigger", axis=(0, 1, 0), amount=0.2, pivot=(-0.004, 0, -0.033))
    lever = [(0.09, 0, -0.035), (0.04, 0, -0.04), (0.012, 0, -0.04), (0.0, 0, -0.045), (-0.004, 0, -0.065), (-0.02, 0, -0.084),
             (-0.045, 0, -0.092), (-0.075, 0, -0.088), (-0.098, 0, -0.07), (-0.1, 0, -0.052), (-0.088, 0, -0.04), (-0.062, 0, -0.037)]
    W.add(W.tube("lever", lever, 0.0042), "steel", "lever", axis=(0, 1, 0), amount=-0.85, pivot=(0.09, 0, -0.035))
    W.add(W.loft("stock", [(-0.06, -0.004, 0.0145, 0.026), (-0.095, -0.007, 0.0135, 0.019), (-0.14, -0.012, 0.016, 0.026),
                           (-0.22, -0.022, 0.019, 0.04), (-0.31, -0.034, 0.021, 0.054), (-0.39, -0.045, 0.021, 0.064), (-0.403, -0.046, 0.0205, 0.064)], 20), "walnut")
    W.add(W.box("butt_plate", (0.006, 0.044, 0.13), (-0.406, 0, -0.046), bevel=0.0025, segments=3), "steel")
    marks(W, (-0.085, 0, -0.015), (0.22, 0, -0.012), (0.627, 0, 0.012), (-0.15, 0, 0.042), eject=(0.03, 0, 0.034), top=(0.06, 0, 0.04))


# ---------------------------------------------------------------- chatter gun (early automatic: finned shroud, pan magazine)
def build_chatter(W):
    W.add(W.box("receiver", (0.2, 0.04, 0.07), (0.03, 0, 0.012), bevel=0.005, segments=3), "steel")
    W.add(W.cyl("shroud", 0.03, 0.36, (0.31, 0, 0.022), 'X', verts=36, bevel=0.002), "steel")
    for k in range(16):
        W.add(W.cyl(f"fin{k}", 0.037, 0.004, (0.16 + k * 0.019, 0, 0.022), 'X', verts=36, bevel=0.0006), "iron")
    W.add(W.barrel("barrel", 0.49, 0.56, 0.022, 0.009, bore=0.0045), "steel")
    W.add(W.cone("flash_hider", 0.012, 0.018, 0.04, (0.575, 0, 0.022)), "steel")
    # the pan magazine lying flat on top, turning a round's worth at every shot; the feed throat under it
    pan = W.cyl("pan", 0.085, 0.022, (0.04, 0, 0.068), 'Z', verts=48, bevel=0.003)
    W.add(pan, "steel", "drum", axis=(0, 0, 1), amount=2 * math.pi / 47, pivot=(0.04, 0, 0.068))
    W.add(W.cyl("pan_hub", 0.022, 0.03, (0.04, 0, 0.07), 'Z', verts=24, bevel=0.002), "case")
    for k in range(12):
        a = k * math.pi / 6
        W.add(W.box(f"pan_rib{k}", (0.07, 0.006, 0.004), (0.04 + math.cos(a) * 0.045, math.sin(a) * 0.045, 0.08), bevel=0.0015, rot=(0, 0, a)),
              "steel", "drum", axis=(0, 0, 1), amount=2 * math.pi / 47, pivot=(0.04, 0, 0.068))
    W.add(W.box("feed", (0.03, 0.02, 0.02), (0.04, 0, 0.05), bevel=0.003), "steel")
    W.add(W.box("cocking_handle", (0.03, 0.03, 0.008), (0.08, -0.024, 0.02), bevel=0.003), "bright", "bolt", axis=(1, 0, 0), amount=-0.05, kind="slide", pivot=(0.08, -0.024, 0.02))
    W.trigger_and_guard(0.0, -0.023)
    g = W.loft("pistol_grip", [(0.0, 0, 0.016, 0.02), (0.05, 0, 0.017, 0.021), (0.1, 0, 0.018, 0.022)], 16)
    g.rotation_euler = (0, math.radians(180 - 72), 0); g.location = (-0.03, 0, -0.02)
    C.select_only([g]); bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    W.add(g, "walnut"); W.chequer(g, 700)
    stock(W, "stock", "walnut", -0.07, 0.24, drop=0.02, butt=0.055)
    # a forward grip under the shroud and the bipod folded beneath it
    W.add(W.box("fore_grip", (0.03, 0.03, 0.08), (0.22, 0, -0.03), bevel=0.01, segments=3), "walnut")
    for s in (-1, 1):
        W.add(W.cyl(f"bipod{s}", 0.005, 0.22, (0.35, s * 0.02, -0.012), 'X', verts=10, bevel=0.0008), "steel")
    marks(W, (-0.05, 0, -0.04), (0.22, 0, -0.04), (0.595, 0, 0.022), (-0.12, 0, 0.11), eject=(0.04, -0.02, 0.012), top=(0.2, 0, 0.06))


# ---------------------------------------------------------------- long rifle (a bolt-action service rifle, a long brass scope)
def build_rifle(W):
    W.add(W.barrel("barrel", 0.06, 0.76, 0.012, 0.0095, bore=0.0042), "steel")
    W.add(W.cyl("receiver", 0.016, 0.14, (0.0, 0, 0.012), 'X', verts=32, bevel=0.002), "steel")
    W.add(W.box("magazine", (0.08, 0.026, 0.05), (0.01, 0, -0.03), bevel=0.004, segments=3), "steel")
    W.add(W.box("stripper_guide", (0.014, 0.022, 0.008), (0.045, 0, 0.03), bevel=0.0015), "steel")
    # the bolt: its body and handle slide back and home with each round
    blt = dict(group="bolt", axis=(1, 0, 0), amount=-0.075, kind="slide", pivot=(-0.03, 0, 0.012))
    W.add(W.cyl("bolt_body", 0.0095, 0.09, (-0.035, 0, 0.012), 'X', verts=20, bevel=0.0015), "bright", **blt)
    W.add(W.tube("bolt_handle", [(-0.06, -0.008, 0.016), (-0.06, -0.035, 0.0), (-0.06, -0.045, -0.008)], 0.0035), "bright", **blt)
    W.add(W.sphere("bolt_knob", (-0.06, -0.047, -0.011), (0.007, 0.007, 0.007), 16), "bright", **blt)
    # the full-length stock to near the muzzle, the upper hand guard, two bands, the sling swivels
    W.add(W.loft("fore_stock", [(0.07, -0.006, 0.017, 0.022), (0.4, -0.004, 0.0155, 0.018), (0.68, -0.002, 0.013, 0.014)], 18), "walnut")
    W.add(W.loft("hand_guard", [(0.09, 0.022, 0.012, 0.006), (0.55, 0.022, 0.011, 0.005)], 14), "walnut")
    for x in (0.38, 0.66):
        W.add(W.box(f"band{x}", (0.012, 0.034, 0.042), (x, 0, 0.004), bevel=0.005, segments=3), "steel")
    W.add(W.ring("swivel_f", (0.5, 0, -0.022), 0.008, 0.0016, 'Y'), "steel")
    W.add(W.ring("swivel_r", (-0.32, 0, -0.072), 0.008, 0.0016, 'Y'), "steel")
    W.trigger_and_guard(0.0, -0.014)
    stock(W, "stock", "walnut", -0.06, 0.36, drop=0.045)
    # the brass telescope in its rings, eyepiece and objective bells
    W.add(W.cyl("scope_tube", 0.0115, 0.3, (0.1, 0, 0.058), 'X', verts=32, bevel=0.0015), "brass")
    W.add(W.cone("scope_eye", 0.0115, 0.017, 0.05, (-0.073, 0, 0.058)), "brass")
    W.add(W.cone("scope_bell", 0.0115, 0.019, 0.06, (0.276, 0, 0.058)), "brass")
    W.add(W.cyl("scope_glass", 0.016, 0.002, (0.305, 0, 0.058), 'X', verts=32, bevel=0), "glass")
    for x in (0.0, 0.18):
        W.add(W.ring(f"scope_ring{x}", (x, 0, 0.058), 0.013, 0.0035), "steel")
        W.add(W.box(f"scope_mount{x}", (0.012, 0.012, 0.03), (x, 0, 0.037), bevel=0.002), "steel")
    marks(W, (-0.085, 0, -0.02), (0.3, 0, -0.01), (0.76, 0, 0.012), (-0.13, 0, 0.058), eject=(0.03, -0.012, 0.02), top=(0.1, 0, 0.075))


# ---------------------------------------------------------------- nitro express (a big-game double rifle)
def build_nitro(W):
    hinge = (0.065, 0, -0.006)
    br = dict(group="break", axis=(0, 1, 0), amount=0.55, pivot=hinge)
    for s in (-1, 1):
        W.add(W.barrel(f"barrel{s}", 0.06, 0.66, 0.017, 0.0128, bore=0.0062, y=s * 0.0126), "steel", **br)
        sh = W.add(W.cyl(f"case{s}", 0.009, 0.004, (0.0615, s * 0.0126, 0.017), 'X', verts=20, bevel=0.0006), "brass", "load", axis=(1, 0, 0), kind="show", pivot=hinge)
        sh["parent"] = "break"
    W.add(W.box("rib", (0.6, 0.012, 0.008), (0.36, 0, 0.031), bevel=0.002), "steel", **br)
    # the express sights: a standing leaf and two folded ones on the rib
    for i, (x, ang) in enumerate([(0.26, 0), (0.275, 75), (0.29, 75)]):
        W.add(W.box(f"leaf{i}", (0.002, 0.014, 0.01), (x, 0, 0.039), bevel=0.0004, rot=(0, math.radians(ang), 0)), "steel", **br)
    W.add(W.sphere("bead", (0.655, 0, 0.038), (0.002, 0.002, 0.002), 10), "silver", **br)
    W.add(W.loft("fore_end", [(0.09, -0.002, 0.02, 0.014), (0.32, 0.0, 0.019, 0.013)], 16), "walnut", **br)
    W.add(W.box("action", (0.1, 0.05, 0.046), (0.02, 0, 0.006), bevel=0.006, segments=3), "case")
    for s in (-1, 1):
        W.add(W.box(f"sidelock{s}", (0.1, 0.004, 0.036), (-0.03, s * 0.0255, 0.004), bevel=0.0018), "silver")
    W.add(W.box("top_lever", (0.045, 0.013, 0.007), (-0.035, 0.004, 0.032), bevel=0.0028, rot=(0, 0, math.radians(8))), "case",
          "latch", axis=(0, 0, 1), amount=0.7, pivot=(-0.012, 0, 0.032))
    W.add(W.box("safety", (0.014, 0.008, 0.004), (-0.06, 0, 0.03), bevel=0.0012), "silver")
    W.trigger_and_guard(0.0, -0.014)
    W.add(W.tube("trigger2", [(0.014, 0, -0.014), (0.01, 0, -0.026), (0.004, 0, -0.034)], 0.0024), "steel")
    stock(W, "stock", "walnut", -0.05, 0.36, drop=0.045, butt=0.068, width=0.023)
    W.add(W.ring("swivel", (-0.3, 0, -0.08), 0.009, 0.0018, 'Y'), "steel")
    marks(W, (-0.08, 0, -0.024), (0.22, 0, -0.012), (0.66, 0, 0.017), (-0.2, 0, 0.046), eject=(0.065, 0, 0.017))


# ---------------------------------------------------------------- tribal longbow (laminated, a fletched arrow on the string)
def build_longbow(W):
    # the stave stands upright through the grip at the origin, its limbs curving back toward the archer
    pts = [(-0.11 * (z / 0.74) ** 2, 0, z) for z in [k * 0.74 / 12 for k in range(-12, 13)]]
    W.add(W.tube("stave", pts, 0.012), "laminate")
    W.add(W.cyl("grip_wrap", 0.016, 0.11, (0.0, 0, 0.0), 'Z', verts=20, bevel=0.002), "leather")
    for z in (-0.74, 0.74):
        W.add(W.sphere(f"tip{z}", (-0.11, 0, z), (0.009, 0.009, 0.016), 12), "horn")
    # the string: two halves pulled back to the nock while an arrow is on it
    nock = (-0.11, 0, 0.0)
    up = W.tube("string_up", [(-0.11, 0, 0.74), nock], 0.0016)
    W.add(up, "string", "string", axis=(0, 1, 0), amount=0.32, pivot=(-0.11, 0, 0.74))
    dn = W.tube("string_dn", [(-0.11, 0, -0.74), nock], 0.0016)
    W.add(dn, "string", "string2", axis=(0, 1, 0), amount=-0.32, pivot=(-0.11, 0, -0.74))
    # the arrow: shaft, head, three feathers (on the string only while loaded)
    ar = dict(group="load", axis=(1, 0, 0), kind="show", pivot=(0, 0, 0))
    W.add(W.cyl("arrow", 0.004, 0.72, (0.16, 0.012, 0.004), 'X', verts=10, bevel=0), "walnut", **ar)
    W.add(W.cone("arrow_head", 0.008, 0.0, 0.04, (0.54, 0.012, 0.004)), "obsidian", **ar)
    for k in range(3):
        a = k * 2 * math.pi / 3
        W.add(W.box(f"fletch{k}", (0.07, 0.001, 0.012), (-0.15, 0.012 + math.cos(a) * 0.006, 0.004 + math.sin(a) * 0.006), bevel=0, rot=(a, 0, 0)), "feather", **ar)
    marks(W, (0.0, 0, 0.0), (-0.2, 0, 0.0), (0.56, 0.012, 0.004), (-0.6, 0, 0.03), eject=(0.0, 0, 0.0))


# ---------------------------------------------------------------- punt gun (a wildfowl gun taller than a man)
def build_puntgun(W):
    W.add(W.barrel("barrel", -0.05, 2.3, 0.02, 0.036, bore=0.023), "iron")
    for x in (0.4, 1.2, 2.0):
        W.add(W.ring(f"reinforce{x}", (x, 0, 0.02), 0.038, 0.006), "iron")
    W.add(W.cyl("breech", 0.046, 0.18, (0.0, 0, 0.02), 'X', verts=32, bevel=0.006), "iron")
    W.add(W.ring("breeching", (-0.02, 0, 0.02), 0.06, 0.014), "rope")
    W.add(W.tube("breech_rope", [(-0.02, 0.06, 0.02), (-0.15, 0.12, -0.05), (-0.3, 0.1, -0.12)], 0.013), "rope")
    hammer_lock(W, "hammer", -0.06, 0, 0.06)
    W.add(W.box("lock", (0.09, 0.006, 0.04), (-0.05, -0.048, 0.04), bevel=0.003), "iron")
    W.trigger_and_guard(-0.04, -0.03)
    W.add(W.loft("stock", [(-0.1, 0.0, 0.03, 0.04), (-0.3, -0.03, 0.03, 0.05), (-0.5, -0.06, 0.03, 0.06)], 16), "walnut")
    # the swivel yoke that sits it on the rail
    W.add(W.cyl("swivel_pin", 0.015, 0.14, (0.25, 0, -0.07), 'Z', verts=16, bevel=0.002), "iron")
    for s in (-1, 1):
        W.add(W.box(f"yoke{s}", (0.03, 0.012, 0.09), (0.25, s * 0.05, -0.02), bevel=0.004), "iron")
    marks(W, (-0.12, 0, -0.03), (0.5, 0, -0.02), (2.3, 0, 0.02), (-0.3, 0, 0.07), eject=(0.0, 0, 0.02))


# ---------------------------------------------------------------- steam riveter (an industrial tool turned weapon)
def build_riveter(W):
    W.add(W.cyl("body", 0.042, 0.36, (0.1, 0, 0.03), 'X', verts=36, bevel=0.004), "brass")
    for x in (-0.06, 0.26):
        W.add(W.ring(f"band{x}", (x, 0, 0.03), 0.044, 0.005), "iron")
    W.add(W.cone("head", 0.03, 0.016, 0.07, (0.315, 0, 0.03)), "iron")
    W.add(W.cyl("plunger", 0.011, 0.07, (0.36, 0, 0.03), 'X', verts=20, bevel=0.002), "bright", "bolt", axis=(1, 0, 0), amount=0.035, kind="slide", pivot=(0.36, 0, 0.03))
    # the pressure gauge on top, its glass and needle; the steam hose curling away from the back
    W.add(W.cyl("gauge", 0.026, 0.014, (0.02, 0, 0.087), 'X', verts=32, bevel=0.003), "brass")
    W.add(W.cyl("gauge_face", 0.022, 0.002, (0.0275, 0, 0.087), 'X', verts=32, bevel=0), "bone")
    W.add(W.box("needle", (0.001, 0.003, 0.018), (0.029, 0.004, 0.09), bevel=0, rot=(math.radians(35), 0, 0)), "red")
    W.add(W.cyl("gauge_stem", 0.006, 0.02, (0.02, 0, 0.07), 'Z', verts=12, bevel=0.001), "brass")
    W.add(W.tube("hose", [(-0.08, 0, 0.03), (-0.16, 0, 0.0), (-0.2, 0.05, -0.1), (-0.15, 0.12, -0.25)], 0.013), "rubber")
    # the rivet hopper with rivets in it; the spade handles
    W.add(W.box("hopper", (0.06, 0.04, 0.05), (0.16, 0, 0.09), bevel=0.004), "iron")
    for k in range(5):
        W.add(W.sphere(f"rivet{k}", (0.14 + k * 0.01, (k % 2) * 0.01 - 0.005, 0.117), (0.006, 0.006, 0.004), 10), "copper", "load", axis=(1, 0, 0), kind="show", pivot=(0.16, 0, 0.09))
    for s in (-1, 1):
        W.add(W.tube(f"handle{s}", [(-0.07, s * 0.03, 0.03), (-0.12, s * 0.06, 0.03), (-0.12, s * 0.06, -0.05)], 0.008), "steel")
        W.add(W.cyl(f"grip{s}", 0.013, 0.07, (-0.12, s * 0.06, -0.03), 'Z', verts=16, bevel=0.003), "walnut")
    W.trigger_and_guard(-0.06, -0.02)
    marks(W, (-0.12, 0.06, -0.03), (-0.12, -0.06, -0.03), (0.4, 0, 0.03), (-0.3, 0, 0.1), eject=(0.16, 0, 0.11))


def flintlock(W, x, y, z):
    """A flintlock on the right side: plate, cock with flint, frizzen and pan."""
    W.add(W.box("lock_plate", (0.085, 0.004, 0.026), (x, y, z), bevel=0.002), "brass")
    W.add(W.box("cock", (0.013, 0.007, 0.034), (x - 0.024, y - 0.003, z + 0.024), bevel=0.0028, rot=(0, math.radians(-30), 0)), "steel",
          "hammer", axis=(0, 1, 0), amount=-0.6, pivot=(x - 0.02, y - 0.003, z + 0.008))
    W.add(W.box("flint", (0.009, 0.007, 0.006), (x - 0.017, y - 0.003, z + 0.042), bevel=0.001, rot=(0, math.radians(-30), 0)), "obsidian",
          "hammer", axis=(0, 1, 0), amount=-0.6, pivot=(x - 0.02, y - 0.003, z + 0.008))
    W.add(W.box("frizzen", (0.006, 0.01, 0.024), (x + 0.014, y - 0.003, z + 0.024), bevel=0.0015), "steel", "frizzen", axis=(0, 1, 0), amount=0.7, pivot=(x + 0.016, y - 0.003, z + 0.013))
    W.add(W.box("pan", (0.014, 0.012, 0.005), (x + 0.014, y - 0.003, z + 0.012), bevel=0.001), "steel")


# ---------------------------------------------------------------- blunderbuss (a flared-muzzle flintlock)
def build_blunderbuss(W):
    W.add(W.barrel("barrel", 0.03, 0.38, 0.03, 0.016, bore=0.012), "brass")
    W.add(W.cone("bell", 0.016, 0.034, 0.09, (0.42, 0, 0.03)), "brass")
    W.add(W.loft("fore_stock", [(0.0, 0.012, 0.018, 0.02), (0.33, 0.016, 0.015, 0.014)], 16), "walnut")
    W.add(W.cyl("ramrod", 0.003, 0.3, (0.17, 0, 0.004), 'X', verts=10, bevel=0.0005), "walnut")
    flintlock(W, -0.005, -0.0185, 0.03)
    W.trigger_and_guard(0.0, -0.002, mat="brass")
    stock(W, "stock", "walnut", -0.05, 0.34, drop=0.06, wrist=0.017, butt=0.06)
    W.add(W.box("butt_brass", (0.007, 0.044, 0.124), (-0.393, 0, -0.065), bevel=0.003), "brass")
    marks(W, (-0.08, 0, -0.02), (0.2, 0, 0.0), (0.465, 0, 0.03), (-0.2, 0, 0.06), eject=(0.0, 0, 0.03))


# ---------------------------------------------------------------- air rifle (a reservoir butt and a pump lever)
def build_airrifle(W):
    W.add(W.barrel("barrel", 0.04, 0.62, 0.014, 0.0085, bore=0.003), "steel")
    W.add(W.box("action", (0.12, 0.028, 0.04), (0.0, 0, 0.008), bevel=0.004, segments=3), "case")
    W.add(W.loft("fore_end", [(0.06, -0.002, 0.016, 0.018), (0.34, 0.0, 0.014, 0.015)], 16), "walnut")
    # the reservoir is the butt: a brass flask screwed on behind the action, a leather cheek wrap
    W.add(W.cyl("reservoir", 0.034, 0.3, (-0.21, 0, -0.03), 'X', verts=36, bevel=0.006), "brass")
    W.add(W.sphere("reservoir_end", (-0.36, 0, -0.03), (0.02, 0.034, 0.034), 24), "brass")
    W.add(W.cyl("cheek", 0.036, 0.1, (-0.2, 0, -0.03), 'X', verts=36, bevel=0.004), "leather")
    W.add(W.box("pump_lever", (0.26, 0.01, 0.01), (0.2, 0, -0.012), bevel=0.003), "steel", "pump", axis=(0, 1, 0), amount=0.45, pivot=(0.33, 0, -0.008))
    hammer_lock(W, "hammer", -0.04, 0, 0.022)
    W.trigger_and_guard(0.0, -0.014)
    W.sights(0.6, 0.1, 0.0225, bead=True)
    marks(W, (-0.07, 0, -0.03), (0.22, 0, -0.012), (0.62, 0, 0.014), (-0.15, 0, 0.03), eject=(0.0, 0, 0.02))


# ---------------------------------------------------------------- galvanic prod (jar cells, copper coils, sparks)
def build_prod(W):
    W.add(W.cyl("rod", 0.014, 0.75, (0.3, 0, 0.0), 'X', verts=24, bevel=0.002), "brass")
    W.add(W.cyl("grip", 0.022, 0.2, (-0.05, 0, 0.0), 'X', verts=24, bevel=0.004), "rubber")
    for k in range(6):
        W.add(W.ring(f"grip_ring{k}", (-0.13 + k * 0.032, 0, 0.0), 0.022, 0.003), "rubber")
    for i, x in enumerate((0.1, 0.17)):
        W.add(W.cyl(f"jar{i}", 0.026, 0.07, (x, 0, 0.05), 'Z', verts=28, bevel=0.003), "glass")
        W.add(W.cyl(f"jar_cap{i}", 0.027, 0.012, (x, 0, 0.09), 'Z', verts=28, bevel=0.002), "copper")
        W.add(W.cyl(f"jar_core{i}", 0.01, 0.06, (x, 0, 0.05), 'Z', verts=12, bevel=0.001), "iron")
    W.add(W.box("cradle", (0.13, 0.03, 0.01), (0.135, 0, 0.016), bevel=0.003), "iron")
    for k in range(20):
        W.add(W.ring(f"coil{k}", (0.42 + k * 0.006, 0, 0.0), 0.02, 0.0028), "copper")
    W.add(W.tube("wire", [(0.135, 0.0, 0.1), (0.25, 0.0, 0.06), (0.42, 0.0, 0.025)], 0.002), "copper")
    for s in (-1, 1):
        W.add(W.tube(f"prong{s}", [(0.67, 0, 0.0), (0.72, s * 0.02, 0.0), (0.76, s * 0.02, 0.0)], 0.004), "copper")
    W.add(W.box("switch", (0.02, 0.012, 0.008), (0.03, 0, 0.022), bevel=0.003), "brass", "trigger", axis=(0, 1, 0), amount=0.3, pivot=(0.04, 0, 0.02))
    marks(W, (-0.05, 0, 0.0), (0.3, 0, 0.0), (0.77, 0, 0.0), (-0.3, 0, 0.1), eject=(0.1, 0, 0.1))


# ---------------------------------------------------------------- rocket harpoon (a finned rocket on a launcher tube)
def build_rocketharpoon(W):
    W.add(W.barrel("tube", -0.25, 0.55, 0.04, 0.036, bore=0.031), "steel")
    for x in (-0.24, 0.05, 0.54):
        W.add(W.ring(f"band{x}", (x, 0, 0.04), 0.038, 0.005), "brass")
    # the rocket harpoon: head and barbs out of the muzzle, the rocket body inside, its fins at the back; the fuse
    rk = dict(group="load", axis=(1, 0, 0), kind="show", pivot=(0.0, 0, 0.04))
    W.add(W.cyl("rocket", 0.026, 0.62, (0.25, 0, 0.04), 'X', verts=24, bevel=0.003), "red", **rk)
    W.add(W.cyl("harpoon_shaft", 0.008, 0.2, (0.64, 0, 0.04), 'X', verts=12, bevel=0.001), "bright", **rk)
    W.add(W.cone("harpoon_head", 0.022, 0.0, 0.07, (0.775, 0, 0.04)), "bright", **rk)
    for s in (-1, 1):
        W.add(W.box(f"barb{s}", (0.04, 0.003, 0.008), (0.735, 0, 0.04 + s * 0.016), bevel=0, rot=(0, math.radians(s * -18), 0)), "bright", **rk)
    for k in range(4):
        a = k * math.pi / 2 + math.pi / 4
        W.add(W.box(f"fin{k}", (0.06, 0.002, 0.028), (-0.29, math.cos(a) * 0.024, 0.04 + math.sin(a) * 0.024), bevel=0, rot=(a, 0, 0)), "steel", **rk)
    W.add(W.tube("fuse", [(-0.31, 0, 0.04), (-0.34, 0.02, 0.0), (-0.32, 0.04, -0.03)], 0.002), "rope", **rk)
    # the tether's coil on the left, a pistol grip, a shoulder pad, a ring sight
    W.add(W.cyl("tether_drum", 0.04, 0.03, (0.15, 0.06, 0.0), 'Y', verts=32, bevel=0.003), "steel")
    W.add(W.cyl("tether", 0.036, 0.026, (0.15, 0.06, 0.0), 'Y', verts=32, bevel=0.002), "rope")
    W.trigger_and_guard(0.0, -0.002)
    g = W.loft("pistol_grip", [(0.0, 0, 0.016, 0.021), (0.05, 0, 0.017, 0.022), (0.1, 0, 0.018, 0.023)], 16)
    g.rotation_euler = (0, math.radians(180 - 72), 0); g.location = (-0.03, 0, 0.0)
    C.select_only([g]); bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    W.add(g, "walnut")
    W.add(W.box("shoulder_pad", (0.03, 0.06, 0.1), (-0.28, 0, 0.02), bevel=0.012, segments=3), "leather")
    W.add(W.ring("ring_sight", (0.0, 0, 0.1), 0.015, 0.002), "brass")
    W.add(W.box("sight_post", (0.006, 0.006, 0.02), (0.0, 0, 0.08), bevel=0.001), "steel")
    marks(W, (-0.05, 0, -0.04), (0.3, 0, 0.0), (0.81, 0, 0.04), (-0.25, 0, 0.1), eject=(-0.3, 0, 0.04))
