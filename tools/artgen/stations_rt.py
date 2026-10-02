# Red Tide's interactables (the Red Tide Visual Overhaul Spec, "The maps": "Racks, Davy's Locker (with its lantern
# buoy), tonic machines, the Forge, workbenches ... traps, and power switches each get a distinct, well-lit, detailed
# model that never gets lost in the decor"), baked like the guns (the gun kit's baker). Placed by the game at each
# station (redtide_game.cpp, DrawStations), facing into its room; moving parts posed there (the power lever, the cache's
# lid). Frame: +X the front (toward the player), +Z up, +Y left; metres; standing on z = 0.
#     blender -b --factory-startup -P tools/artgen/stations_rt.py -- --out assets/redtide/stations [--only tonic]

import os
import sys
import math
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import common as C
import gunkit as K
bpy = C.bpy


def rivets(W, name, x, y0, y1, z, n, mat="brass", r=0.012):
    for k in range(n):
        y = y0 + (y1 - y0) * k / max(1, n - 1)
        W.add(W.sphere(f"{name}{k}", (x, y, z), (r, r, r), 10), mat)


# ---------------------------------------------------------------- a tonic machine: a brass dispensing cabinet, its
# tonic glowing in a bottle behind a round port (the game lights the bottle in the tonic's colour)
def build_tonic(W):
    W.add(W.box("cabinet", (0.55, 0.8, 1.55), (0, 0, 0.82), bevel=0.04, segments=3), "brass")
    W.add(W.box("plinth", (0.65, 0.9, 0.08), (0, 0, 0.04), bevel=0.01), "iron")
    dome = W.sphere("dome", (0, 0, 1.6), (0.3, 0.42, 0.2), 32)
    W.add(dome, "verdigris")
    W.add(W.box("sign", (0.04, 0.6, 0.16), (0.29, 0, 1.45), bevel=0.01), "rosewood")
    W.add(W.box("sign_plate", (0.012, 0.5, 0.1), (0.315, 0, 1.45), bevel=0.003), "bone")
    # the port: a thick brass ring, the glass over a dark well where the bottle stands
    W.add(W.cyl("well", 0.2, 0.06, (0.26, 0, 0.98), 'X', verts=32, bevel=0.01), "obsidian")
    W.add(W.ring("port_ring", (0.29, 0, 0.98), 0.21, 0.03, 'X'), "brass")
    W.add(W.cyl("port_glass", 0.19, 0.012, (0.3, 0, 0.98), 'X', verts=32, bevel=0.002), "glass")
    for k in range(8):
        a = 2 * math.pi * k / 8
        W.add(W.sphere(f"port_bolt{k}", (0.31, 0.21 * math.cos(a), 0.98 + 0.21 * math.sin(a)), (0.013, 0.013, 0.013), 8), "steel")
    # the valve wheel to pour, the coin slot, a gauge, the drip tray, the pipes down into the deck
    W.add(W.ring("valve", (0.3, -0.28, 0.62), 0.07, 0.01, 'X'), "red")
    for k in range(3):
        W.add(W.cyl(f"spoke{k}", 0.006, 0.14, (0.3, -0.28, 0.62), 'Y' if k == 0 else 'Z', verts=8, bevel=0), "red")
    W.add(W.box("slot", (0.02, 0.1, 0.03), (0.29, 0.25, 0.68), bevel=0.004), "obsidian")
    W.add(W.cyl("gauge", 0.06, 0.02, (0.29, 0.25, 0.5), 'X', verts=24, bevel=0.004), "brass")
    W.add(W.cyl("gauge_face", 0.05, 0.004, (0.301, 0.25, 0.5), 'X', verts=24, bevel=0), "bone")
    W.add(W.box("tray", (0.2, 0.36, 0.04), (0.33, 0, 0.36), bevel=0.01), "steel")
    for s in (-1, 1):
        W.add(W.tube(f"pipe{s}", [(-0.2, s * 0.32, 1.2), (-0.34, s * 0.32, 1.0), (-0.34, s * 0.32, 0.1), (-0.2, s * 0.32, 0.02)], 0.035), "copper")
    rivets(W, "rv_a", 0.28, -0.36, 0.36, 1.33, 9)
    rivets(W, "rv_b", 0.28, -0.36, 0.36, 0.2, 9)


# ---------------------------------------------------------------- Davy's Locker: a sea chest bound in iron, chained, and
# its lantern buoy (a separate model the game bobs on the swell above it)
def build_locker(W):
    W.add(W.box("chest", (0.7, 1.1, 0.5), (0, 0, 0.25), bevel=0.03, segments=3), "rosewood")
    lid = W.loft("lid", [(-0.55, 0.0, 0.35, 0.12), (0.0, 0.05, 0.35, 0.16), (0.55, 0.0, 0.35, 0.12)], 24)
    lid.rotation_euler = (0, 0, math.pi / 2); lid.location = (0, 0, 0.5)
    C.select_only([lid]); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    W.add(lid, "rosewood")
    for y in (-0.42, 0.0, 0.42):
        W.add(W.box(f"band{y}", (0.74, 0.06, 0.54), (0, y, 0.27), bevel=0.006), "iron")
    W.add(W.box("hasp", (0.03, 0.12, 0.16), (0.37, 0, 0.45), bevel=0.006), "iron")
    W.add(W.box("padlock", (0.05, 0.12, 0.13), (0.4, 0, 0.32), bevel=0.02), "brass")
    W.add(W.ring("shackle", (0.4, 0, 0.42), 0.045, 0.01, 'X'), "steel")
    for s in (-1, 1):
        W.add(W.ring(f"handle{s}", (0, s * 0.57, 0.35), 0.07, 0.012, 'Y'), "iron")
    # the chain up to the buoy
    for k in range(10):
        W.add(W.ring(f"chain{k}", (-0.2, 0.0, 0.62 + k * 0.07), 0.03, 0.008, 'Y' if k % 2 else 'X'), "iron")
    for x in (-0.3, 0.3):
        for y in (-0.5, 0.5):
            W.add(W.sphere(f"barnacle{x}{y}", (x, y, 0.04), (0.04, 0.04, 0.03), 10), "bone")


def build_buoy(W):
    """The lantern buoy: a red and white float with a caged lamp (the game lights it)."""
    W.add(W.sphere("float", (0, 0, 0.0), (0.3, 0.3, 0.26), 32), "red")
    W.add(W.ring("stripe", (0, 0, 0.0), 0.3, 0.05, 'Z'), "bone")
    W.add(W.cyl("post", 0.04, 0.35, (0, 0, 0.35), 'Z', verts=16, bevel=0.005), "iron")
    W.add(W.cyl("lamp_base", 0.1, 0.04, (0, 0, 0.53), 'Z', verts=20, bevel=0.005), "brass")
    W.add(W.cyl("lamp_glass", 0.08, 0.18, (0, 0, 0.65), 'Z', verts=20, bevel=0.004), "glass")
    for k in range(6):
        a = 2 * math.pi * k / 6
        W.add(W.cyl(f"cage{k}", 0.006, 0.2, (0.095 * math.cos(a), 0.095 * math.sin(a), 0.65), 'Z', verts=6, bevel=0), "brass")
    W.add(W.cone("lamp_cap", 0.12, 0.02, 0.09, (0, 0, 0.78), 'Z', verts=20), "brass")
    W.add(W.ring("eye", (0, 0, -0.27), 0.04, 0.01, 'Y'), "iron")


# ---------------------------------------------------------------- the Pressure Forge: a riveted copper pressure vessel
# on a stone footing, a sea-glass window in its door (the game glows behind it), gauges, valve wheels, a hand pump
def build_forge(W):
    W.add(W.box("footing", (1.4, 1.6, 0.35), (0, 0, 0.17), bevel=0.04, segments=2), "obsidian")
    W.add(W.cyl("vessel", 0.62, 1.3, (0, 0, 1.0), 'Z', verts=40, bevel=0.04), "copper")
    W.add(W.sphere("crown", (0, 0, 1.65), (0.62, 0.62, 0.32), 40), "copper")
    for z in (0.45, 0.95, 1.5):
        W.add(W.ring(f"seam{z}", (0, 0, z), 0.625, 0.02, 'Z'), "brass")
        for k in range(16):
            a = 2 * math.pi * k / 16
            W.add(W.sphere(f"rv{z}{k}", (0.63 * math.cos(a), 0.63 * math.sin(a), z + 0.04), (0.014, 0.014, 0.014), 8), "brass")
    # the door with its sea-glass window, hinged on the left
    W.add(W.cyl("door", 0.3, 0.06, (0.62, 0, 1.0), 'X', verts=36, bevel=0.01), "brass")
    W.add(W.cyl("seaglass", 0.22, 0.02, (0.66, 0, 1.0), 'X', verts=36, bevel=0.003), "glass")
    for k in range(10):
        a = 2 * math.pi * k / 10
        W.add(W.sphere(f"door_bolt{k}", (0.66, 0.26 * math.cos(a), 1.0 + 0.26 * math.sin(a)), (0.016, 0.016, 0.016), 8), "steel")
    W.add(W.box("latch", (0.05, 0.18, 0.05), (0.68, -0.28, 1.0), bevel=0.01), "iron")
    for s, z in ((1, 1.45), (-1, 1.4)):
        W.add(W.cyl(f"gauge{s}", 0.09, 0.03, (0.5, s * 0.36, z), 'X', verts=24, bevel=0.006), "brass")
        W.add(W.cyl(f"gauge_face{s}", 0.075, 0.004, (0.517, s * 0.36, z), 'X', verts=24, bevel=0), "bone")
        W.add(W.box(f"needle{s}", (0.002, 0.004, 0.06), (0.52, s * 0.36, z + 0.02), bevel=0), "red")
    W.add(W.ring("wheel", (0.0, 0.66, 1.1), 0.16, 0.018, 'Y'), "red")
    for k in range(4):
        W.add(W.cyl(f"wheel_spoke{k}", 0.008, 0.32, (0.0, 0.66, 1.1), 'X' if k % 2 else 'Z', verts=8, bevel=0), "red")
    # the pump and its pipes, the chimney valve on top
    W.add(W.cyl("pump", 0.07, 0.7, (-0.45, -0.75, 0.7), 'Z', verts=20, bevel=0.01), "steel")
    W.add(W.box("pump_handle", (0.5, 0.04, 0.04), (-0.3, -0.75, 1.1), bevel=0.01, rot=(0, math.radians(-15), 0)), "rosewood")
    W.add(W.tube("pump_pipe", [(-0.45, -0.75, 0.4), (-0.45, -0.6, 0.38), (-0.3, -0.5, 0.5)], 0.03), "copper")
    W.add(W.cyl("valve_stack", 0.08, 0.3, (0, 0, 2.05), 'Z', verts=20, bevel=0.01), "brass")
    W.add(W.cyl("valve_cap", 0.12, 0.04, (0, 0, 2.22), 'Z', verts=20, bevel=0.01), "brass")


# ---------------------------------------------------------------- a workbench: a heavy timber bench with a vice, tools
# hung on a board behind, a lamp, parts and scrap on the top
def build_workbench(W):
    W.add(W.box("top", (0.8, 1.8, 0.08), (0, 0, 0.88), bevel=0.01), "laminate")
    for x in (-0.33, 0.33):
        for y in (-0.82, 0.82):
            W.add(W.box(f"leg{x}{y}", (0.09, 0.09, 0.84), (x, y, 0.42), bevel=0.01), "laminate")
    W.add(W.box("shelf", (0.7, 1.7, 0.04), (0, 0, 0.2), bevel=0.006), "laminate")
    W.add(W.box("board", (0.04, 1.7, 0.8), (-0.42, 0, 1.35), bevel=0.01), "rosewood")
    W.add(W.box("vice_body", (0.16, 0.18, 0.12), (0.32, 0.6, 0.98), bevel=0.01), "iron")
    W.add(W.box("vice_jaw", (0.05, 0.2, 0.08), (0.43, 0.6, 1.0), bevel=0.006), "iron")
    W.add(W.cyl("vice_screw", 0.012, 0.22, (0.5, 0.6, 0.98), 'X', verts=10, bevel=0), "steel")
    W.add(W.cyl("vice_bar", 0.008, 0.22, (0.6, 0.6, 0.98), 'Y', verts=8, bevel=0), "steel")
    # tools on the board: a spanner, a hammer, a saw, coils of line
    W.add(W.box("spanner", (0.012, 0.04, 0.3), (-0.39, -0.5, 1.4), bevel=0.004, rot=(math.radians(20), 0, 0)), "steel")
    W.add(W.box("hammer_head", (0.03, 0.12, 0.04), (-0.39, -0.2, 1.55), bevel=0.006), "iron")
    W.add(W.cyl("hammer_haft", 0.015, 0.3, (-0.39, -0.2, 1.38), 'Z', verts=10, bevel=0.003), "rosewood")
    W.add(W.box("saw", (0.008, 0.35, 0.12), (-0.39, 0.25, 1.45), bevel=0.002), "bright")
    for k in range(3):
        W.add(W.ring(f"coil{k}", (-0.37, 0.6, 1.4 - k * 0.012), 0.1 - k * 0.012, 0.012, 'X'), "rope")
    # on the top: a gas bulb, a box of parts, a lamp, scrap plates
    W.add(W.cyl("bulb", 0.06, 0.25, (0.05, -0.4, 1.0), 'Y', verts=20, bevel=0.02), "brass")
    W.add(W.box("parts_box", (0.25, 0.3, 0.1), (0.05, 0.05, 0.97), bevel=0.01), "laminate")
    for k in range(6):
        W.add(W.cyl(f"part{k}", 0.012, 0.06, (0.0 + (k % 3) * 0.05, -0.04 + (k // 3) * 0.08, 1.03), 'Z', verts=8, bevel=0.002), "brass")
    W.add(W.cyl("lamp_base", 0.07, 0.03, (-0.2, -0.75, 0.93), 'Z', verts=16, bevel=0.005), "iron")
    W.add(W.cyl("lamp_glass", 0.05, 0.13, (-0.2, -0.75, 1.03), 'Z', verts=16, bevel=0.004), "glass")
    W.add(W.cone("lamp_cap", 0.07, 0.015, 0.06, (-0.2, -0.75, 1.12), 'Z', verts=16), "iron")
    W.add(W.box("plate", (0.3, 0.25, 0.01), (0.2, 0.55, 0.925), bevel=0.002, rot=(0, 0, 0.3)), "iron")


# ---------------------------------------------------------------- the power switch: a knife switch on an iron panel,
# porcelain insulators, cables down into the deck; the lever (group "lever") thrown up for on
def build_power(W):
    W.add(W.box("post", (0.12, 0.12, 1.0), (-0.1, 0, 0.5), bevel=0.01), "iron")
    W.add(W.box("panel", (0.08, 0.5, 0.7), (0, 0, 1.15), bevel=0.02, segments=2), "iron")
    W.add(W.box("slate", (0.02, 0.4, 0.55), (0.045, 0, 1.15), bevel=0.005), "obsidian")
    for z in (0.95, 1.35):
        for y in (-0.08, 0.08):
            W.add(W.cyl(f"ins{z}{y}", 0.025, 0.06, (0.08, y, z), 'X', verts=14, bevel=0.006), "bone")
            W.add(W.box(f"clip{z}{y}", (0.04, 0.012, 0.05), (0.12, y, z), bevel=0.002), "copper")
    lever = dict(group="lever", axis=(0, 1, 0), amount=-2.4, pivot=(0.12, 0, 0.95))
    for y in (-0.08, 0.08):
        W.add(W.box(f"blade{y}", (0.012, 0.012, 0.42), (0.13, y, 1.15), bevel=0.002), "copper", **lever)
    W.add(W.cyl("crossbar", 0.012, 0.22, (0.13, 0, 1.36), 'Y', verts=10, bevel=0.002), "bone", **lever)
    W.add(W.cyl("knob", 0.03, 0.08, (0.17, 0, 1.36), 'X', verts=14, bevel=0.01), "red", **lever)
    for y in (-0.15, 0.15):
        W.add(W.tube(f"cable{y}", [(0.0, y, 0.82), (-0.05, y, 0.5), (-0.1, y + 0.1, 0.05), (-0.3, y + 0.2, 0.02)], 0.018), "rubber")
    W.add(W.box("sign", (0.012, 0.3, 0.08), (0.05, 0, 1.55), bevel=0.003), "red")


# ---------------------------------------------------------------- a cache: an iron strongbox, its lid (group "lid")
# hinged at the back
def build_cache(W):
    W.add(W.box("box", (0.6, 0.9, 0.42), (0, 0, 0.21), bevel=0.02, segments=2), "iron")
    lid = dict(group="lid", axis=(0, 1, 0), amount=-1.9, pivot=(-0.3, 0, 0.44))
    W.add(W.box("lid", (0.62, 0.92, 0.06), (0, 0, 0.45), bevel=0.015), "iron", **lid)
    W.add(W.box("lid_band", (0.64, 0.08, 0.07), (0, 0, 0.45), bevel=0.006), "brass", **lid)
    for y in (-0.3, 0.3):
        W.add(W.box(f"band{y}", (0.62, 0.06, 0.44), (0, y, 0.22), bevel=0.005), "brass")
    W.add(W.box("lock", (0.03, 0.12, 0.12), (0.31, 0, 0.32), bevel=0.01), "brass")
    W.add(W.box("keyhole", (0.005, 0.015, 0.04), (0.325, 0, 0.31), bevel=0), "obsidian")
    rivets(W, "rv", 0.305, -0.42, 0.42, 0.06, 8, "steel", 0.01)


RECIPES = {n[6:]: f for n, f in globals().items() if n.startswith("build_")}

if __name__ == "__main__":
    a = C.args()
    only = a[a.index("--only") + 1].split(",") if "--only" in a else None
    out = C.out_dir()
    for pid, fn in RECIPES.items():
        if only and pid not in only:
            continue
        W = K.Weapon(pid)
        fn(W)
        W.finish(out, size=512)
