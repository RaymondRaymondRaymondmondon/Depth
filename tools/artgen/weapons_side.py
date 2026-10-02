# Sidearms (the Visual Overhaul Spec's per-weapon table): service revolver, derringer, pepperbox, flare pistol,
# speargun, twin speargun, harpoon pistol, air pistol, captain's pistol. Frame and conventions: gunkit.py.

import math
import gunkit as K
from gunkit import bpy, C


def grip_down(W, name, mat, x0, z0, length, angle_deg, hy, hz, flare=1.25):
    """A pistol grip: a lofted wooden block running down and back from (x0, z0) at angle_deg below the horizontal."""
    o = W.loft(name, [(0.0, 0, hy, hz), (length * 0.5, 0, hy * 1.05, hz * 1.04), (length, 0, hy * flare * 0.95, hz * flare)], segs=18)
    o.rotation_euler = (0, math.radians(180 - angle_deg), 0)
    o.location = (x0, 0, z0)
    C.select_only([o]); bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    return o


# ---------------------------------------------------------------- service revolver (a British top-break, about 1900)
def build_revolver(W):
    # the frame: a flat-sided steel body over the trigger, its top strap reaching forward to the barrel's latch
    # (one body, its sides cut open in a window so the cylinder shows between the top strap, the recoil shield behind
    # and the standing breech in front)
    fr = W.box("frame", (0.085, 0.028, 0.06), (0.002, 0, 0.042), bevel=0.003, segments=3)
    C.apply_all(fr)
    win = C.bevelled_box("window", (0.058, 0.06, 0.039), loc=(0.009, 0, 0.0485), bevel=0)
    b = fr.modifiers.new("win", 'BOOLEAN'); b.object = win; b.operation = 'DIFFERENCE'
    C.select_only([fr]); bpy.ops.object.modifier_apply(modifier=b.name); bpy.data.objects.remove(win)
    W.add(fr, "steel")
    W.add(W.box("top_strap", (0.02, 0.016, 0.008), (-0.03, 0, 0.074), bevel=0.002), "steel")
    for i, (x, z) in enumerate([(-0.012, 0.03), (0.02, 0.026)]):
        W.add(W.screw(f"side_screw{i}", (x, 0.0145, z), 1), "bright")
    # the barrel, its top rib and lug, the cylinder and the ejector star: one assembly that breaks open about the hinge
    hinge = (0.035, 0, 0.022)
    br = dict(group="break", axis=(0, 1, 0), amount=0.85, pivot=hinge)
    W.add(W.barrel("barrel", 0.038, 0.165, 0.05, 0.0078, bore=0.0046), "steel", **br)
    W.add(W.box("rib", (0.125, 0.008, 0.006), (0.102, 0, 0.0585), bevel=0.0015), "steel", **br)
    W.add(W.box("barrel_lug", (0.024, 0.012, 0.03), (0.046, 0, 0.034), bevel=0.003), "steel", **br)
    W.add(W.box("front_sight", (0.007, 0.0025, 0.008), (0.158, 0, 0.065), bevel=0.0006), "steel", **br)
    W.add(W.box("latch_post", (0.012, 0.016, 0.012), (0.03, 0, 0.066), bevel=0.002), "steel", **br)
    # the cylinder: six chambers, six flutes; it turns a sixth at each shot (and rides the barrel when it breaks)
    cyl = W.cyl("cylinder", 0.019, 0.038, (0.016, 0, 0.048), 'X', verts=48, bevel=0.0015)
    for k in range(6):
        a = k * math.pi / 3
        ch = W.cyl(f"ch{k}", 0.0047, 0.06, (0.016, math.cos(a) * 0.011, 0.048 + math.sin(a) * 0.011), 'X', verts=16, bevel=0)
        b = cyl.modifiers.new(f"c{k}", 'BOOLEAN'); b.object = ch; b.operation = 'DIFFERENCE'
        C.select_only([cyl]); bpy.ops.object.modifier_apply(modifier=b.name); bpy.data.objects.remove(ch)
        fa = a + math.pi / 6
        fl = W.cyl(f"fl{k}", 0.0045, 0.022, (0.019, math.cos(fa) * 0.0205, 0.048 + math.sin(fa) * 0.0205), 'X', verts=12, bevel=0)
        b = cyl.modifiers.new(f"f{k}", 'BOOLEAN'); b.object = fl; b.operation = 'DIFFERENCE'
        C.select_only([cyl]); bpy.ops.object.modifier_apply(modifier=b.name); bpy.data.objects.remove(fl)
    o = W.add(cyl, "steel", "cylinder", axis=(1, 0, 0), amount=math.pi / 3, pivot=(0.016, 0, 0.048))
    o["parent"] = "break"
    # the cartridges' brass heads, seen at the back of the chambers ("show": only while loaded)
    for k in range(6):
        a = k * math.pi / 3
        rim = W.add(W.cyl(f"case{k}", 0.0052, 0.003, (-0.0045, math.cos(a) * 0.011, 0.048 + math.sin(a) * 0.011), 'X', verts=14, bevel=0.0004),
                    "brass", "load", axis=(1, 0, 0), amount=0, kind="show", pivot=(0.016, 0, 0.048))
        rim["parent"] = "cylinder"
    star = W.add(W.cyl("ejector_star", 0.012, 0.002, (-0.004, 0, 0.048), 'X', verts=6, bevel=0.0004), "bright", "ejector",
                 axis=(1, 0, 0), amount=-0.012, kind="slide", pivot=(-0.004, 0, 0.048))
    star["parent"] = "break"
    # the stirrup latch on top, pushed forward by the thumb to break the gun
    W.add(W.box("latch", (0.012, 0.02, 0.008), (-0.018, 0, 0.073), bevel=0.0025), "case", "latch", axis=(0, 1, 0), amount=-0.5, pivot=(-0.024, 0, 0.068))
    # the hammer (a spur, chequered), cocked back at rest and falling when fired
    ham = W.box("hammer", (0.012, 0.007, 0.03), (-0.04, 0, 0.068), bevel=0.0025, rot=(0, math.radians(-20), 0))
    W.add(ham, "case", "hammer", axis=(0, 1, 0), amount=-0.55, pivot=(-0.036, 0, 0.05))
    spur = W.box("hammer_spur", (0.018, 0.008, 0.005), (-0.052, 0, 0.083), bevel=0.0018, rot=(0, math.radians(-10), 0))
    W.add(spur, "case", "hammer", axis=(0, 1, 0), amount=-0.55, pivot=(-0.036, 0, 0.05))
    W.trigger_and_guard(0.0, 0.017)
    # the bird's-head grip with chequered walnut panels, its butt cap and the lanyard ring
    g = grip_down(W, "grip", "walnut", -0.03, 0.03, 0.095, 70, 0.0145, 0.019, flare=1.2)
    W.add(g, "walnut"); W.chequer(g)
    W.add(W.box("grip_strap", (0.008, 0.016, 0.09), (-0.058, 0, -0.012), bevel=0.003, rot=(0, math.radians(20), 0)), "steel")
    W.add(W.ring("lanyard_ring", (-0.072, 0, -0.058), 0.009, 0.0016, 'Y'), "steel")
    W.add(W.screw("grip_screw", (-0.047, 0.016, -0.015), 1, 0.003), "bright")
    W.mark("grip_r", (-0.045, 0, -0.012))
    W.mark("grip_l", (-0.05, 0, -0.03))          # (a sidearm: the left hand cups the right)
    W.mark("muzzle", (0.166, 0, 0.05))
    W.mark("eject", (0.0, 0, 0.05))
    W.mark("sight", (-0.2, 0, 0.068))
    W.mark("mount_muzzle", (0.166, 0, 0.05)); W.mark("mount_top", (0.09, 0, 0.062))


def bore_cut(o, name, r, x0, x1, y, z):
    """A bore through a solid along X (a barrel cut in a block, a chamber)."""
    c = C.cylinder(name, r, x1 - x0, loc=((x0 + x1) / 2, y, z), rot=(0, math.pi / 2, 0), verts=20, bevel=0)
    b = o.modifiers.new(name, 'BOOLEAN'); b.object = c; b.operation = 'DIFFERENCE'
    C.select_only([o]); bpy.ops.object.modifier_apply(modifier=b.name); bpy.data.objects.remove(c)


def common_marks(W, grip, muzzle, eject=None, sight=None, left=None, top=None):
    W.mark("grip_r", grip)
    W.mark("grip_l", left if left else (grip[0] - 0.005, 0, grip[2] - 0.02))
    W.mark("muzzle", muzzle)
    W.mark("eject", eject if eject else (0.0, 0, muzzle[2]))
    W.mark("sight", sight if sight else (-0.2, 0, muzzle[2] + 0.015))
    W.mark("mount_muzzle", muzzle)
    W.mark("mount_top", top if top else (muzzle[0] * 0.55, 0, muzzle[2] + 0.012))


# ---------------------------------------------------------------- derringer (an over-under two-shot pocket pistol)
def build_derringer(W):
    hinge = (0.004, 0, 0.047)
    br = dict(group="break", axis=(0, 1, 0), amount=-1.1, pivot=hinge)   # the barrels tip up about the hinge
    blk = W.box("barrels", (0.078, 0.02, 0.03), (0.043, 0, 0.034), bevel=0.0035, segments=3)
    C.apply_all(blk)
    for z in (0.027, 0.041):
        bore_cut(blk, f"bore{z}", 0.0045, 0.0, 0.09, 0, z)
    W.add(blk, "steel", **br)
    W.add(W.box("rib", (0.07, 0.006, 0.004), (0.046, 0, 0.051), bevel=0.0012), "steel", **br)
    W.add(W.box("front_sight", (0.005, 0.002, 0.004), (0.078, 0, 0.0545), bevel=0.0005), "steel", **br)
    for i, z in enumerate((0.027, 0.041)):
        r = W.add(W.cyl(f"case{i}", 0.0052, 0.002, (0.004, 0, z), 'X', verts=14, bevel=0.0003), "brass", "load", axis=(1, 0, 0), kind="show", pivot=hinge)
        r["parent"] = "break"
    W.add(W.box("frame", (0.03, 0.02, 0.04), (-0.012, 0, 0.033), bevel=0.004, segments=3), "steel")
    W.add(W.box("hammer", (0.008, 0.006, 0.022), (-0.028, 0, 0.056), bevel=0.002, rot=(0, math.radians(-25), 0)), "case",
          "hammer", axis=(0, 1, 0), amount=-0.5, pivot=(-0.024, 0, 0.044))
    # the spur trigger: no guard, a sheath under the frame
    W.add(W.box("sheath", (0.014, 0.012, 0.012), (-0.006, 0, 0.012), bevel=0.003), "steel")
    t = W.tube("trigger", [(-0.004, 0, 0.012), (-0.008, 0, 0.0), (-0.013, 0, -0.006)], 0.002)
    W.add(t, "steel", "trigger", axis=(0, 1, 0), amount=0.25, pivot=(-0.004, 0, 0.012))
    g = grip_down(W, "grip", "rosewood", -0.02, 0.028, 0.06, 62, 0.0115, 0.016, flare=1.25)
    W.add(g, "rosewood"); W.chequer(g, 1100)
    W.add(W.screw("grip_screw", (-0.04, 0.0125, 0.0), 1, 0.0024), "bright")
    common_marks(W, (-0.035, 0, 0.005), (0.082, 0, 0.034))


# ---------------------------------------------------------------- pepperbox (four barrels that turn with each pull)
def build_pepperbox(W):
    ax = (0.044, 0, 0.034)
    cl = W.cyl("cluster", 0.017, 0.088, ax, 'X', verts=40, bevel=0.002)
    for k in range(4):
        a = k * math.pi / 2 + math.pi / 4
        bore_cut(cl, f"b{k}", 0.0042, 0.0, 0.1, math.cos(a) * 0.0095, 0.034 + math.sin(a) * 0.0095)
        flute = C.cylinder(f"fl{k}", 0.004, 0.09, loc=(0.044, math.cos(a + math.pi / 4) * 0.019, 0.034 + math.sin(a + math.pi / 4) * 0.019), rot=(0, math.pi / 2, 0), verts=12, bevel=0)
        b = cl.modifiers.new(f"f{k}", 'BOOLEAN'); b.object = flute; b.operation = 'DIFFERENCE'
        C.select_only([cl]); bpy.ops.object.modifier_apply(modifier=b.name); bpy.data.objects.remove(flute)
    W.add(cl, "steel", "barrels", axis=(1, 0, 0), amount=math.pi / 2, pivot=ax)
    W.add(W.cyl("nipple_shield", 0.019, 0.01, (-0.002, 0, 0.034), 'X', verts=32, bevel=0.002), "silver")
    W.add(W.box("frame", (0.04, 0.022, 0.03), (-0.018, 0, 0.026), bevel=0.004, segments=3), "silver")
    # the bar hammer lying along the top, lifted by the pull and falling at the shot
    W.add(W.box("bar_hammer", (0.05, 0.007, 0.007), (-0.012, 0, 0.054), bevel=0.002), "case", "hammer", axis=(0, 1, 0), amount=-0.35, pivot=(-0.035, 0, 0.05))
    W.add(W.ring("guard", (-0.006, 0, 0.004), 0.013, 0.0022, 'Y'), "silver")
    t = W.tube("trigger", [(-0.004, 0, 0.012), (-0.008, 0, 0.0), (-0.013, 0, -0.004)], 0.0022)
    W.add(t, "steel", "trigger", axis=(0, 1, 0), amount=0.25, pivot=(-0.004, 0, 0.012))
    g = grip_down(W, "grip", "rosewood", -0.032, 0.024, 0.075, 66, 0.012, 0.017, flare=1.15)
    W.add(g, "rosewood"); W.chequer(g, 1000)
    W.add(W.cyl("butt_cap", 0.016, 0.006, (-0.063, 0, -0.045), 'Z', verts=24, bevel=0.002), "silver")
    common_marks(W, (-0.045, 0, 0.0), (0.09, 0, 0.034))


# ---------------------------------------------------------------- flare pistol (a large-bore brass break-open)
def build_flarepistol(W):
    hinge = (0.03, 0, 0.022)
    br = dict(group="break", axis=(0, 1, 0), amount=0.75, pivot=hinge)
    W.add(W.barrel("barrel", 0.03, 0.17, 0.045, 0.0185, bore=0.0145), "brass", **br)
    W.add(W.ring("muzzle_ring", (0.168, 0, 0.045), 0.019, 0.0028), "brass", **br)
    W.add(W.box("lug", (0.03, 0.016, 0.03), (0.045, 0, 0.026), bevel=0.004), "brass", **br)
    shell = W.cyl("flare_shell", 0.0138, 0.07, (0.065, 0, 0.045), 'X', verts=28, bevel=0.0015)
    s = W.add(shell, "red", "load", axis=(1, 0, 0), kind="show", pivot=hinge); s["parent"] = "break"
    rim = W.add(W.cyl("shell_rim", 0.0158, 0.004, (0.031, 0, 0.045), 'X', verts=28, bevel=0.0006), "brass", "load", axis=(1, 0, 0), kind="show", pivot=hinge)
    rim["parent"] = "break"
    W.add(W.box("frame", (0.06, 0.03, 0.05), (0.0, 0, 0.035), bevel=0.005, segments=3), "brass")
    W.add(W.box("hammer", (0.012, 0.009, 0.034), (-0.032, 0, 0.065), bevel=0.003, rot=(0, math.radians(-25), 0)), "steel",
          "hammer", axis=(0, 1, 0), amount=-0.5, pivot=(-0.028, 0, 0.05))
    W.add(W.box("catch", (0.014, 0.012, 0.01), (0.022, 0, 0.066), bevel=0.003), "steel", "latch", axis=(0, 1, 0), amount=-0.5, pivot=(0.016, 0, 0.062))
    W.trigger_and_guard(0.0, 0.012, mat="brass")
    g = grip_down(W, "grip", "walnut", -0.025, 0.026, 0.1, 72, 0.016, 0.022, flare=1.15)
    W.add(g, "walnut"); W.chequer(g, 800)
    W.add(W.ring("lanyard_ring", (-0.06, 0, -0.07), 0.009, 0.0016, 'Y'), "brass")
    common_marks(W, (-0.045, 0, -0.012), (0.172, 0, 0.045))


def spear(W, name, x0, x1, y, z, group="load"):
    """A steel spear on its rail: a shaft, a barbed head (a cone with two flipping barbs) and a line eye."""
    parts = []
    parts.append(W.add(W.cyl(name, 0.0035, x1 - x0, ((x0 + x1) / 2, y, z), 'X', verts=12, bevel=0), "bright", group, axis=(1, 0, 0), kind="show", pivot=(x0, y, z)))
    parts.append(W.add(W.cone(name + "_head", 0.0065, 0.0, 0.045, (x1 + 0.022, y, z), 'X', verts=12), "bright", group, axis=(1, 0, 0), kind="show", pivot=(x0, y, z)))
    for s in (-1, 1):
        bb = W.box(name + f"_barb{s}", (0.022, 0.0015, 0.004), (x1 + 0.004, y, z + s * 0.006), bevel=0, rot=(0, math.radians(s * -14), 0))
        parts.append(W.add(bb, "bright", group, axis=(1, 0, 0), kind="show", pivot=(x0, y, z)))
    return parts


# ---------------------------------------------------------------- speargun (a brass-and-wood spring gun)
def build_speargun(W, twin=False):
    # the stock: a walnut beam with a pistol grip and a shoulder pad; the spring barrel on top; the brass muzzle
    W.add(W.loft("stock", [(-0.25, -0.012, 0.016, 0.022), (-0.05, -0.006, 0.018, 0.02), (0.25, 0.0, 0.016, 0.017), (0.42, 0.004, 0.013, 0.014)], 20), "walnut")
    g = grip_down(W, "grip", "walnut", -0.02, -0.005, 0.095, 75, 0.015, 0.02, flare=1.1)
    W.add(g, "walnut"); W.chequer(g, 700)
    ys = (-0.014, 0.014) if twin else (0.0,)
    for y in ys:
        W.add(W.barrel(f"tube{y}", -0.12, 0.42, 0.027, 0.0095, bore=0.0055, y=y), "brass")
        W.add(W.ring(f"muzzle{y}", (0.42, y, 0.027), 0.0105, 0.0025), "brass")
        spear(W, f"spear{y}", -0.1, 0.62, y, 0.04)
    # the cocking lever along the left side, swung forward to load (the reload works it)
    lv = W.box("cocking_lever", (0.16, 0.006, 0.012), (0.06, 0.03 if not twin else 0.035, 0.008), bevel=0.003)
    W.add(lv, "steel", "lever", axis=(0, 0, 1), amount=0.0, pivot=(0.14, 0.03, 0.008))
    W.add(lv if False else W.box("lever_pivot", (0.014, 0.008, 0.014), (0.14, 0.03 if not twin else 0.035, 0.008), bevel=0.003), "brass")
    # the line spool on the right side, with the line wound on it
    W.add(W.cyl("spool", 0.022, 0.016, (0.02, -0.032, -0.005), 'Y', verts=28, bevel=0.002), "brass")
    W.add(W.cyl("line", 0.019, 0.012, (0.02, -0.032, -0.005), 'Y', verts=28, bevel=0.001), "rope")
    W.trigger_and_guard(0.0, -0.012)
    W.add(W.box("butt_pad", (0.01, 0.034, 0.05), (-0.255, 0, -0.012), bevel=0.006, segments=3), "rubber")
    common_marks(W, (-0.04, 0, -0.035), (0.66, 0, 0.04), left=(0.16, 0, -0.01), sight=(-0.25, 0, 0.055), top=(0.15, 0, 0.045))


def build_twinspear(W):
    build_speargun(W, twin=True)


# ---------------------------------------------------------------- harpoon pistol (a short barrel, a barbed head, a rope coil)
def build_harpistol(W):
    W.add(W.barrel("barrel", 0.03, 0.2, 0.04, 0.014, bore=0.011), "brass")
    W.add(W.ring("muzzle_band", (0.198, 0, 0.04), 0.0145, 0.003), "steel")
    W.add(W.ring("breech_band", (0.04, 0, 0.04), 0.0145, 0.003), "steel")
    spear(W, "harpoon", 0.06, 0.24, 0, 0.04)
    # the rope coil on the left, fed to the harpoon's eye
    W.add(W.ring("rope_coil", (0.09, 0.03, 0.03), 0.022, 0.009, 'Y'), "rope")
    W.add(W.box("frame", (0.06, 0.026, 0.04), (0.0, 0, 0.028), bevel=0.005, segments=3), "steel")
    W.add(W.box("hammer", (0.012, 0.009, 0.032), (-0.032, 0, 0.056), bevel=0.003, rot=(0, math.radians(-25), 0)), "case",
          "hammer", axis=(0, 1, 0), amount=-0.5, pivot=(-0.028, 0, 0.042))
    W.trigger_and_guard(0.0, 0.008)
    g = grip_down(W, "grip", "walnut", -0.025, 0.022, 0.1, 70, 0.016, 0.021, flare=1.15)
    W.add(g, "walnut"); W.chequer(g, 800)
    common_marks(W, (-0.045, 0, -0.016), (0.27, 0, 0.04))


# ---------------------------------------------------------------- air pistol (a reservoir in the grip, a pump lever)
def build_airpistol(W):
    W.add(W.barrel("barrel", 0.02, 0.2, 0.042, 0.0065, bore=0.0025), "steel")
    W.add(W.box("barrel_block", (0.05, 0.022, 0.026), (0.015, 0, 0.036), bevel=0.004, segments=3), "steel")
    W.add(W.cyl("reservoir", 0.02, 0.11, (-0.045, 0, -0.012), 'Z', verts=32, bevel=0.004), "brass")
    W.add(W.sphere("reservoir_cap", (-0.045, 0, -0.068), (0.02, 0.02, 0.012), 24), "brass")
    W.add(W.box("manometer", (0.004, 0.012, 0.012), (-0.034, 0.0, 0.028), bevel=0.002), "glass")
    # the pump lever under the barrel, hinged at the muzzle end, swung down and back through a reload
    W.add(W.box("pump_lever", (0.15, 0.008, 0.008), (0.11, 0, 0.026), bevel=0.0025), "steel", "pump", axis=(0, 1, 0), amount=0.5, pivot=(0.19, 0, 0.03))
    W.add(W.box("hammer", (0.01, 0.007, 0.024), (-0.016, 0, 0.058), bevel=0.0025, rot=(0, math.radians(-20), 0)), "case",
          "hammer", axis=(0, 1, 0), amount=-0.45, pivot=(-0.012, 0, 0.046))
    W.trigger_and_guard(0.0, 0.016)
    W.sights(0.192, -0.005, 0.0485, bead=False)
    common_marks(W, (-0.045, 0, -0.01), (0.2, 0, 0.042))


# ---------------------------------------------------------------- captain's pistol (a flintlock officer's pistol, a relic)
def build_captainpistol(W):
    W.add(W.barrel("barrel", 0.03, 0.26, 0.042, 0.0105, bore=0.0062, octagonal=True), "steel")
    # the full stock to the muzzle, its brass nose cap, the ramrod in its pipes beneath
    W.add(W.loft("fore_stock", [(0.0, 0.03, 0.013, 0.012), (0.24, 0.034, 0.011, 0.009)], 16), "walnut")
    W.add(W.cyl("nose_cap", 0.0125, 0.012, (0.245, 0, 0.036), 'X', verts=20, bevel=0.002), "verdigris")
    W.add(W.cyl("ramrod", 0.0028, 0.24, (0.12, 0, 0.022), 'X', verts=10, bevel=0.0005), "steel")
    for x in (0.1, 0.2):
        W.add(W.ring(f"pipe{x}", (x, 0, 0.022), 0.0045, 0.0015), "verdigris")
    # the lock on the right side: an engraved silver plate, the cock gripping its flint, the frizzen and its pan
    W.add(W.box("lock_plate", (0.07, 0.004, 0.022), (-0.004, -0.0135, 0.038), bevel=0.002), "silver")
    cock = W.box("cock", (0.012, 0.006, 0.03), (-0.026, -0.016, 0.058), bevel=0.0025, rot=(0, math.radians(-30), 0))
    W.add(cock, "silver", "hammer", axis=(0, 1, 0), amount=-0.6, pivot=(-0.022, -0.016, 0.044))
    W.add(W.box("flint", (0.008, 0.006, 0.005), (-0.02, -0.016, 0.073), bevel=0.001, rot=(0, math.radians(-30), 0)), "obsidian",
          "hammer", axis=(0, 1, 0), amount=-0.6, pivot=(-0.022, -0.016, 0.044))
    W.add(W.box("frizzen", (0.006, 0.009, 0.022), (0.01, -0.016, 0.058), bevel=0.0015), "steel", "frizzen", axis=(0, 1, 0), amount=0.7, pivot=(0.012, -0.016, 0.048))
    W.add(W.box("pan", (0.012, 0.01, 0.004), (0.01, -0.016, 0.047), bevel=0.001), "steel")
    W.trigger_and_guard(0.0, 0.026, mat="verdigris")
    # the bird's-head butt with its silver cap, silver wire inlay in the wrist
    g = grip_down(W, "grip", "walnut", -0.03, 0.035, 0.11, 58, 0.015, 0.02, flare=1.3)
    W.add(g, "walnut")
    W.add(W.sphere("butt_cap", (-0.088, 0, -0.06), (0.02, 0.019, 0.022), 24), "silver")
    W.add(W.box("inlay", (0.04, 0.0164, 0.003), (-0.04, 0, 0.034), bevel=0.0008), "silver")
    common_marks(W, (-0.05, 0, -0.005), (0.262, 0, 0.042))
