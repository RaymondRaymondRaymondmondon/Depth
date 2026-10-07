# The Deep's crew as others see them (stage 6, multiplayer): a Verne-era diver in a canvas suit, a brass helmet with a
# round front port and two side ports, a brass corselet, a weight belt, an air tank on the back, gloves, heavy boots
# and fins. Written as diver.glb with one object per body part, each with its pivot at its joint so the game can pose
# it (Runtime/Net/Mate.cs): diver_torso (pivot at the hips), diver_helmet (the neck), diver_uarm (the shoulder),
# diver_farm (the elbow), diver_thigh (the hip), diver_shin (the knee), diver_fin (the ankle). +z forward, +y up,
# metres (about 1.8 m tall). The suit's canvas carries vertex alpha 1: the game tints it per crew member (Deep/Lit
# _Tint); brass, rubber and glass carry alpha 0 and keep their colours.
#     blender -b --factory-startup -P tools/artgen/deep_diver.py -- --out TheDeep/Assets/Deep/Resources/Vehicles
import bpy
import math
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deep_creatures as K
from deep_creatures import MB, loft, tube, ellipsoid, cone, membrane, mix, clamp

CANVAS = (0.88, 0.86, 0.8)
BRASS = (0.72, 0.53, 0.24)
BRASS_D = (0.45, 0.31, 0.12)
RUBBER = (0.1, 0.1, 0.1)
LEATHER = (0.28, 0.18, 0.1)
GLASS = (0.06, 0.16, 0.17)
LENS = (1.0, 0.95, 0.75)
IRON = (0.25, 0.27, 0.27)


def c4(c, a=0.0):
    return (c[0], c[1], c[2], a)


def suit(c, k=1.0):
    # canvas with seams: the tint mask (alpha 1)
    return (c[0] * k, c[1] * k, c[2] * k, 1.0)


def ring(mb, centre, normal, r, thick, col, segs=16):
    # a torus-like ring as a closed tube around `normal`
    t, side, up = K.frame(normal)
    pts = []
    for k in range(segs + 1):
        a = k / segs * math.pi * 2
        pts.append(K.add(centre, K.add(K.mul(side, math.cos(a) * r), K.mul(up, math.sin(a) * r))))
    tube(mb, pts, [thick] * len(pts), 6, col, cap=False)


def torso():
    mb = MB()
    # the canvas body: hips to shoulders, a little barrel-chested
    def prof(t):
        return 0.15 + 0.06 * math.sin(clamp(t * 1.15) * math.pi * 0.85)
    rings = []
    for i in range(11):
        t = i / 10
        y = -0.04 + 0.6 * t
        w = prof(t)
        rings.append(((0, y, 0.0), (0, 1, 0), w * 1.15, w * 0.85, None))
    loft(mb, rings, 14, lambda t, a, p: suit(CANVAS, 0.8 if abs(math.sin(a * 2)) < 0.05 else 1.0))
    # the weight belt and its lead weights
    ring(mb, (0, 0.06, 0), (0, 1, 0), 0.17, 0.035, c4(LEATHER), 20)
    for k in range(6):
        a = k / 6 * math.pi * 2 + 0.3
        ellipsoid(mb, (math.sin(a) * 0.19, 0.06, math.cos(a) * 0.15), (0.045, 0.05, 0.03), c4(IRON), 6, 4)
    # the brass corselet over the shoulders, its rim studded
    cors = []
    for i in range(6):
        t = i / 5
        y = 0.42 + 0.14 * t
        w = 0.27 - 0.11 * t * t
        cors.append(((0, y, 0.0), (0, 1, 0), w, w * 0.78, None))
    loft(mb, cors, 18, lambda t, a, p: c4(mix(BRASS_D, BRASS, 0.4 + 0.6 * t)), cap0=False, cap1=True)
    for k in range(12):
        a = k / 12 * math.pi * 2
        ellipsoid(mb, (math.sin(a) * 0.27, 0.43, math.cos(a) * 0.21), (0.018, 0.018, 0.018), c4(BRASS_D), 5, 3)
    # the air tank on the back, with its valve and a hose round to the helmet
    tube(mb, [(0, 0.1, -0.24), (0, 0.5, -0.24)], [0.085, 0.085], 10, c4(IRON))
    ellipsoid(mb, (0, 0.5, -0.24), (0.085, 0.05, 0.085), c4(IRON), 10, 4)
    ellipsoid(mb, (0, 0.1, -0.24), (0.085, 0.04, 0.085), c4(IRON), 10, 4)
    tube(mb, [(0, 0.55, -0.24), (0, 0.6, -0.24)], [0.02, 0.02], 6, c4(BRASS))
    tube(mb, [(0, 0.6, -0.24), (0.12, 0.66, -0.16), (0.18, 0.66, 0.0)], [0.022, 0.022, 0.022], 6, c4(RUBBER))
    return mb


def helmet():
    mb = MB()
    cy = 0.19
    ellipsoid(mb, (0, cy, 0.0), (0.2, 0.21, 0.2), c4(BRASS), 18, 12,
              colfn=lambda p: c4(mix(BRASS_D, BRASS, clamp(0.5 + (p[1] - cy) * 3))))
    # the neck ring that bolts to the corselet
    ring(mb, (0, 0.0, 0), (0, 1, 0), 0.16, 0.03, c4(BRASS_D), 18)
    # the front port: dark glass in a heavy ring, with a grille
    ellipsoid(mb, (0, cy, 0.185), (0.11, 0.11, 0.035), c4(GLASS), 14, 6)
    ring(mb, (0, cy, 0.2), (0, 0, 1), 0.115, 0.025, c4(BRASS_D), 18)
    for x in (-0.05, 0.0, 0.05):
        tube(mb, [(x, cy - 0.1, 0.225), (x, cy + 0.1, 0.225)], [0.007, 0.007], 4, c4(BRASS_D))
    # the side ports
    for s in (-1, 1):
        ellipsoid(mb, (s * 0.185, cy + 0.01, 0.02), (0.03, 0.07, 0.07), c4(GLASS), 10, 5)
        ring(mb, (s * 0.2, cy + 0.01, 0.02), (s, 0, 0), 0.075, 0.018, c4(BRASS_D), 14)
    # the lamp on its crown, facing forward
    tube(mb, [(0, cy + 0.2, 0.02), (0, cy + 0.23, 0.11)], [0.045, 0.05], 10, c4(IRON))
    ellipsoid(mb, (0, cy + 0.23, 0.115), (0.04, 0.04, 0.015), c4(LENS), 10, 4)
    return mb


def limb(length, r0, r1, end=None):
    mb = MB()
    pts = [(0, -length * t, 0) for t in (0.0, 0.33, 0.66, 1.0)]
    radii = [r0, K.lerp(r0, r1, 0.4) * 1.05, K.lerp(r0, r1, 0.75), r1]
    tube(mb, pts, radii, 10, None, colfn=lambda t, a, p: suit(CANVAS, 0.85 if abs(t - 0.5) < 0.04 else 1.0))
    ellipsoid(mb, (0, 0, 0), (r0 * 1.05, r0 * 1.05, r0 * 1.05), suit(CANVAS), 10, 6)
    if end:
        end(mb, length)
    return mb


def glove(mb, length):
    ring(mb, (0, -length + 0.01, 0), (0, 1, 0), 0.05, 0.015, c4(BRASS), 14)
    ellipsoid(mb, (0, -length - 0.07, 0.01), (0.045, 0.08, 0.05), c4(RUBBER), 10, 6)
    tube(mb, [(0.035, -length - 0.04, 0.03), (0.05, -length - 0.1, 0.06)], [0.016, 0.013], 6, c4(RUBBER))


def boot(mb, length):
    ellipsoid(mb, (0, -length - 0.02, 0.04), (0.07, 0.07, 0.13), c4(LEATHER), 10, 6)
    tube(mb, [(0, -length - 0.08, -0.08), (0, -length - 0.08, 0.16)], [0.06, 0.06], 8, c4(IRON))


def fin():
    mb = MB()
    # the strap round the boot, then the blade forward of the toes
    ring(mb, (0, -0.06, 0.05), (0, 0, 1), 0.075, 0.015, c4(RUBBER), 12)
    root = [(-0.075, -0.08, 0.12), (0.0, -0.085, 0.14), (0.075, -0.08, 0.12)]
    tip = [(-0.13, -0.08, 0.58), (0.0, -0.07, 0.62), (0.13, -0.08, 0.58)]
    membrane(mb, root, tip, 5, (0.85, 0.65, 0.12, 0.0), 0.0, True)
    membrane(mb, [(p[0], p[1] - 0.004, p[2]) for p in root[::-1]], [(p[0], p[1] - 0.004, p[2]) for p in tip[::-1]], 5, (0.85, 0.65, 0.12, 0.0), 0.0, True)
    return mb


def main():
    a = K.args()
    out = a[a.index("--out") + 1] if "--out" in a else os.path.join(K.ROOT, "TheDeep", "Assets", "Deep", "Resources", "Vehicles")
    os.makedirs(out, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    parts = [("diver_torso", torso()), ("diver_helmet", helmet()),
             ("diver_uarm", limb(0.3, 0.068, 0.056)), ("diver_farm", limb(0.27, 0.055, 0.047, glove)),
             ("diver_thigh", limb(0.42, 0.09, 0.068)), ("diver_shin", limb(0.42, 0.066, 0.055, boot)),
             ("diver_fin", fin())]
    objs = [K.to_object(n, mb) for n, mb in parts]
    for o in objs:
        o.select_set(True)
    path = os.path.join(out, "diver.glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=True, export_yup=True,
                              export_texcoords=True, export_normals=True, export_tangents=False, export_materials='NONE',
                              export_vertex_color='ACTIVE', export_all_vertex_colors=False)
    print(f"artgen: wrote {path} ({os.path.getsize(path) // 1024} KB)")


if __name__ == "__main__":
    main()
