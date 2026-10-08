# The Deep's flora (stage 3): a model for every plant in the design doc's two flora tables (20 for the Sunlit
# Shallows, 20 for the Kelp Labyrinth), in real metres with the base at the origin and +y up, written as one glTF per
# biome with an object per plant (named by its id) for Flora.cs to plant instanced. (Iron-Kelp itself stays the
# procedural stalk in FloraMeshes.cs, which already sways and has a far LOD.)
# Vertex colour: rgb the plant's colour; alpha how far up it is (0 at the root, 1 at the tips), which Flora.shader
# uses to sway it in the current and to shade the roots. The geometry kit is deep_creatures.py's.
#     blender -b --factory-startup -P tools/artgen/deep_flora.py -- --out TheDeep/Assets/Deep/Resources/Flora
import bpy
import json
import math
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deep_creatures as K
from deep_creatures import MB, add, sub, mul, norm, cross, lerp, lerp3, clamp, mix, H, noise3, tube, ellipsoid, cone, membrane, loft


def col(rgb, h):
    return (rgb[0], rgb[1], rgb[2], clamp(h))


def hsv(h, s, v):
    import colorsys
    return colorsys.hsv_to_rgb(h % 1, clamp(s), clamp(v))


def blade(mb, base, dirn, length, width, c, bend=0.3, segs=6, twist=0.0):
    """A flat, tapering blade from base along dirn, curving over (sea-grass, kelp blades, fronds)."""
    side = norm(cross(dirn, (0.0, 1.0, 0.0))) if abs(dirn[1]) < 0.99 else (1.0, 0.0, 0.0)
    pts = []
    for i in range(segs + 1):
        t = i / segs
        p = add(base, mul(dirn, length * t))
        p = add(p, mul(side, math.sin(t * 2.0 + twist) * bend * length * t * t * 0.4))
        p = (p[0], p[1] - bend * length * t * t * 0.5, p[2])
        pts.append(p)
    b0 = len(mb.v)
    for i, p in enumerate(pts):
        t = i / segs
        w = width * (1 - t * 0.85) * (0.6 + 0.4 * math.sin(math.pi * min(1, t * 1.3 + 0.2)))
        h = p[1] / max(0.01, length)
        mb.vert(add(p, mul(side, -w)), col(mix(c, mul(c, 0.7), t * 0.4), h))
        mb.vert(add(p, mul(side, w)), col(mix(c, mul(c, 0.7), t * 0.4), h))
    for i in range(segs):
        a = b0 + i * 2
        mb.face(a, a + 1, a + 3, a + 2)


def strand(mb, base, end, r, c, segs=5, wob=0.1, k=0):
    pts = [add(lerp3(base, end, i / segs), (math.sin(i * 1.3 + k) * wob, 0, math.cos(i * 1.1 + k) * wob)) for i in range(segs + 1)]
    hmax = max(0.01, end[1])
    tube(mb, pts, [r * (1 - 0.8 * i / segs) for i in range(segs + 1)], 4, None, 0.0,
         colfn=lambda t, a, p: col(c, p[1] / hmax))


def lumps(mb, centre, radius, n, c, seed, flat=0.6, height_frac=1.0):
    """A cushion or a mass of rounded lumps (moss, sponge masses, algae crust)."""
    for i in range(n):
        a = H(seed + str(i), 1) * math.pi * 2
        r = H(seed + str(i), 2) ** 0.5 * radius
        s = radius * (0.25 + 0.3 * H(seed + str(i), 3))
        p = (centre[0] + math.cos(a) * r, centre[1] + s * flat * 0.6, centre[2] + math.sin(a) * r)
        ellipsoid(mb, p, (s, s * flat, s), None, 8, 5, 0.0,
                  lambda q, c=c: col(mix(c, mul(c, 0.6), noise3((q[0] * 20, q[1] * 20, q[2] * 20), 1) * 0.6), q[1] / max(0.05, radius * height_frac)))


def build(f, biome):
    """One plant from its table entry: the name and type pick the build."""
    n = f["name"].lower()
    t = f.get("type", "").lower()
    mb = MB()
    seed = f["id"]
    if "carpet" in n or ("algae" in t and "coral" not in n and "verdant" not in n):     # Solar-Carpet Algae: a fizzing mat
        lumps(mb, (0, 0, 0), 0.7, 28, hsv(0.28, 0.5, 0.42), seed, 0.25)
        for i in range(30):   # oxygen bubbles caught on it
            a, r = H(seed + str(i), 7) * 6.28, H(seed + str(i), 8) * 0.6
            ellipsoid(mb, (math.cos(a) * r, 0.12, math.sin(a) * r), (0.015, 0.015, 0.015), col((0.85, 0.95, 0.95), 0.2), 5, 3)
    elif "coral algae" in n:       # a pink calcified crust over rubble
        lumps(mb, (0, 0, 0), 0.8, 22, hsv(0.95, 0.45, 0.75), seed, 0.35)
    elif "verdant" in n:           # lime filaments on rock
        lumps(mb, (0, 0, 0), 0.4, 8, hsv(0.22, 0.7, 0.45), seed, 0.3)
        for i in range(60):
            a, r = H(seed + str(i), 1) * 6.28, H(seed + str(i), 2) * 0.4
            b = (math.cos(a) * r, 0.05, math.sin(a) * r)
            strand(mb, b, add(b, (math.cos(a) * 0.1, 0.35 + 0.25 * H(seed + str(i), 3), math.sin(a) * 0.1)), 0.006, hsv(0.2, 0.75, 0.7), 4, 0.03, i)
    elif "sea-grass" in n:         # a clump of blades: long ribbons or short soft tufts
        short = "glade" in n
        for i in range(22 if not short else 30):
            a, r = H(seed + str(i), 1) * 6.28, H(seed + str(i), 2) * (0.25 if not short else 0.35)
            d = norm((math.cos(a) * 0.15, 1, math.sin(a) * 0.15))
            blade(mb, (math.cos(a) * r, 0, math.sin(a) * r), d, (0.9 + 0.6 * H(seed + str(i), 3)) if not short else 0.25 + 0.15 * H(seed + str(i), 3),
                  0.025 if not short else 0.03, hsv(0.27 + 0.05 * H(seed + str(i), 4), 0.65, 0.5), 0.25, 6, a)
    elif "moss" in n and biome == "shallows":     # cushions: metallic gold, or velvet with pink spore-flowers
        gold = "golden" in n
        lumps(mb, (0, 0, 0), 0.5, 14, hsv(0.12, 0.8, 0.75) if gold else hsv(0.3, 0.45, 0.4), seed, 0.6)
        if not gold:
            for i in range(25):
                a, r = H(seed + str(i), 5) * 6.28, H(seed + str(i), 6) * 0.45
                ellipsoid(mb, (math.cos(a) * r, 0.18, math.sin(a) * r), (0.02, 0.02, 0.02), col(hsv(0.93, 0.6, 0.9), 0.5), 5, 3)
    elif "fern" in n:              # a rosette of fronds: petal-shaped, or a translucent lattice
        lace = "lace" in n
        for i in range(9):
            a = i / 9 * math.pi * 2 + H(seed, 1)
            d = norm((math.cos(a), 0.8, math.sin(a)))
            c = hsv(0.9, 0.35, 0.8) if not lace else (0.92, 0.95, 0.9)
            k = 6
            root = [add((0, 0.02, 0), mul(d, 0.08 * j / k)) for j in range(k)]
            tip = []
            for j in range(k):
                s = math.sin(math.pi * (j + 0.5) / k)
                perp = norm(cross(d, (0, 1, 0)))
                tip.append(add(add(root[j], mul(d, 0.25 + 0.35 * j / k)), mul(perp, s * 0.12)))
            membrane(mb, root, tip, 3, col(c, 0.6), 0.0, lace, 0.01)
    elif "kelplet" in n:           # a short stalk with blades (and the solar one's gas floats)
        feather = "feather" in n
        strand(mb, (0, 0, 0), (0.05, 0.6, 0.0), 0.02, hsv(0.08, 0.6, 0.45), 6, 0.02)
        for i in range(8):
            h = 0.15 + 0.06 * i
            a = i * 2.4
            d = norm((math.cos(a), 0.6 if not feather else 1.2, math.sin(a)))
            blade(mb, (0.0, h, 0.0), d, 0.35 if not feather else 0.25, 0.05 if not feather else 0.02, hsv(0.1 if not feather else 0.3, 0.7, 0.55), 0.3, 5, a)
            if not feather:
                ellipsoid(mb, add((0, h, 0), mul(d, 0.08)), (0.03, 0.03, 0.03), col(hsv(0.11, 0.8, 0.8), h / 0.6), 6, 4)
    elif "reed" in n:              # dense furry stalks, or tall stiff serrated ones on the sandbar edges
        crest = "crest" in n
        for i in range(14):
            a, r = H(seed + str(i), 1) * 6.28, H(seed + str(i), 2) * 0.3
            hgt = (1.2 + 0.8 * H(seed + str(i), 3)) * (1.4 if crest else 1.0)
            b = (math.cos(a) * r, 0, math.sin(a) * r)
            strand(mb, b, add(b, (0.05, hgt, 0.02)), 0.018 if not crest else 0.012, hsv(0.09, 0.5, 0.4) if not crest else hsv(0.15, 0.4, 0.6), 7, 0.02, i)
            if crest:
                for k in range(4):
                    y = hgt * (0.55 + 0.1 * k)
                    cone(mb, add(b, (0.05, y, 0.02)), add(b, (0.12, y + 0.05, 0.02)), 0.01, col(hsv(0.12, 0.45, 0.7), y / hgt), 4)
    elif "anemone" in n:           # a broad fan of stinging tentacles on a stalk
        strand(mb, (0, 0, 0), (0, 0.25, 0), 0.05, hsv(0.02, 0.5, 0.5), 3, 0.0)
        for i in range(36):
            a = (i / 36 - 0.5) * math.pi * 0.9
            d = (math.sin(a) * 0.9, math.cos(a), 0.15 * math.sin(i))
            strand(mb, (0, 0.25, 0), add((0, 0.25, 0), mul(d, 0.55)), 0.012, hsv(0.02 + 0.03 * (i % 3), 0.65, 0.85), 5, 0.04, i)
    elif "bloom" in n:             # flowering plants: iridescent petals, or white bulbs holding pearls
        pearl = "pearl" in n
        for i in range(5):
            a = i * 1.26
            stem_top = (math.cos(a) * 0.15, 0.35 + 0.15 * H(seed + str(i), 1), math.sin(a) * 0.15)
            strand(mb, (0, 0, 0), stem_top, 0.012, hsv(0.3, 0.5, 0.45), 4, 0.03, i)
            if pearl:
                ellipsoid(mb, stem_top, (0.06, 0.08, 0.06), col((0.95, 0.93, 0.88), 1.0), 8, 6)
            else:
                for k in range(6):
                    b = k / 6 * math.pi * 2
                    d = norm((math.cos(b), 0.5, math.sin(b)))
                    membrane(mb, [stem_top, add(stem_top, mul(d, 0.02))], [add(stem_top, mul(d, 0.12)), add(stem_top, mul(d, 0.14))], 2,
                             col(hsv(0.55 + 0.12 * k, 0.55, 0.95), 1.0), 0.0, False)
    elif "sponge" in n and biome == "shallows":   # porous tubes, or a barrel ringed with spikes
        crown = "crown" in n
        if crown:
            K.loft(mb, [((0, 0.0, 0), (0, 1, 0), 0.35, 0.35, None), ((0, 0.5, 0), (0, 1, 0), 0.45, 0.45, None), ((0, 0.95, 0), (0, 1, 0), 0.4, 0.4, None)], 14,
                   lambda t, a, p: col(mix(hsv(0.06, 0.55, 0.8), (0.35, 0.2, 0.15), 0.5 if (a * 3) % 1 < 0.15 else 0), p[1]), cap1=False)
            for i in range(16):
                a = i / 16 * math.pi * 2
                cone(mb, (math.cos(a) * 0.4, 0.95, math.sin(a) * 0.4), (math.cos(a) * 0.5, 1.15, math.sin(a) * 0.5), 0.03, col(hsv(0.05, 0.5, 0.6), 1), 5)
        else:
            for i in range(7):
                a, r = H(seed + str(i), 1) * 6.28, H(seed + str(i), 2) * 0.2
                h = 0.25 + 0.3 * H(seed + str(i), 3)
                b = (math.cos(a) * r, 0, math.sin(a) * r)
                K.loft(mb, [(b, (0, 1, 0), 0.07, 0.07, None), (add(b, (0, h, 0)), (0, 1, 0), 0.08, 0.08, None)], 10,
                       lambda t, a_, p: col(hsv(0.14, 0.75, 0.8), p[1] / 0.55), cap1=False)
    elif "table coral" in n:       # tiers of flat plates on a stout stalk
        strand(mb, (0, 0, 0), (0, 0.5, 0), 0.09, hsv(0.05, 0.35, 0.65), 3, 0.0)
        for k in range(3):
            y = 0.35 + k * 0.22
            r = 0.75 - k * 0.18
            ellipsoid(mb, (0.05 * k, y, -0.04 * k), (r, 0.05, r * 0.9), None, 16, 4, 0.0,
                      lambda q: col(mix(hsv(0.06, 0.35, 0.85), hsv(0.95, 0.5, 0.75), noise3((q[0] * 8, 0, q[2] * 8), 2)), q[1]))
    elif "spire coral" in n:       # branching spires
        def branch(b, d, length, r, depth):
            e = add(b, mul(d, length))
            strand(mb, b, e, r, hsv(0.88, 0.55, 0.6 + 0.1 * depth), 3, 0.0)
            if depth < 3:
                for j in range(2):
                    a = H(seed + str(depth) + str(j) + str(b[1]), 1) * 6.28
                    nd = norm(add(d, (math.cos(a) * 0.6, 0.2, math.sin(a) * 0.6)))
                    branch(e, nd, length * 0.7, r * 0.7, depth + 1)
        for i in range(3):
            a = i * 2.1
            branch((math.cos(a) * 0.1, 0, math.sin(a) * 0.1), norm((math.cos(a) * 0.2, 1, math.sin(a) * 0.2)), 0.7, 0.06, 0)
    # ---- the Kelp Labyrinth's growths
    elif "glow bulb" in n:         # small glowing fruits at a stalk's foot
        for i in range(7):
            a, r = H(seed + str(i), 1) * 6.28, 0.05 + H(seed + str(i), 2) * 0.12
            ellipsoid(mb, (math.cos(a) * r, 0.05 + 0.06 * H(seed + str(i), 3), math.sin(a) * r), (0.04, 0.05, 0.04), col((0.45, 1.0, 0.6), 0.2), 8, 5)
    elif "tangle vines" in n or "vine lace" in n or "strand lace" in n:   # curtains strung or streaming between stalks
        lace = "lace" in n
        for i in range(10 if not lace else 7):
            x = (i - 4.5) * 0.3
            pts = [(x + math.sin(j + i) * 0.08, 2.4 - j * 0.3 + 0.2 * math.sin(x * 3), math.cos(j * 0.7 + i) * 0.08) for j in range(9)]
            tube(mb, pts, [0.012 if not lace else 0.008] * 9, 4, None, 0.0, colfn=lambda t, a, p: col(hsv(0.18, 0.45, 0.35) if not lace else (0.85, 0.9, 0.82), p[1] / 2.6))
        if lace:   # the cross threads
            for j in range(6):
                y = 2.2 - j * 0.35
                tube(mb, [(-1.4, y, 0), (0, y - 0.1, 0.05), (1.4, y, 0)], [0.006] * 3, 3, None, 0.0, colfn=lambda t, a, p: col((0.85, 0.9, 0.82), p[1] / 2.6))
    elif "bracket" in n:           # springy shelves (smooth, or ribbed) that sit on a stalk's side
        ribbed = "rib" in n
        for k in range(3 if not ribbed else 5):
            y = k * (0.18 if not ribbed else 0.12)
            ellipsoid(mb, (0.18, y, 0), (0.22, 0.035, 0.28), None, 12, 4, 0.0,
                      lambda q: col(mix(hsv(0.1, 0.55, 0.55), (0.25, 0.15, 0.08), 0.6 if ribbed and int(q[0] * 40) % 2 else 0), 0.3))
    elif "tubeworm" in n:          # chalky spiral tubes, or a venting colony
        gasp = "gasp" in n
        for i in range(6 if gasp else 4):
            a, r = H(seed + str(i), 1) * 6.28, H(seed + str(i), 2) * 0.2
            pts, radii = [], []
            for j in range(12):
                h = j / 11
                aa = a + (h * 6 if not gasp else 0)
                pts.append((math.cos(a) * r + (math.cos(aa) * 0.05 if not gasp else 0), h * (0.6 + 0.3 * H(seed + str(i), 3)), math.sin(a) * r + (math.sin(aa) * 0.05 if not gasp else 0)))
                radii.append(0.03 if not gasp else 0.05)
            tube(mb, pts, radii, 6, None, 0.0, colfn=lambda t, a_, p: col((0.88, 0.86, 0.8) if not gasp else hsv(0.08, 0.3, 0.5), p[1] / 0.9))
    elif "spongeweed" in n:        # a smothering porous mass, or squat silty floor sponges
        silt = "silt" in n
        lumps(mb, (0, 0, 0), 0.45 if silt else 0.35, 10, hsv(0.1, 0.3, 0.35) if silt else hsv(0.25, 0.4, 0.4), seed, 0.7 if silt else 1.2, 1.5)
    elif "moss" in n:              # the Kelp's carpets: veined, or hooked
        latch = "latch" in n
        lumps(mb, (0, 0, 0), 0.7, 18, hsv(0.2, 0.55, 0.3) if latch else hsv(0.33, 0.5, 0.3), seed, 0.2)
        if not latch:
            for i in range(8):
                a = i * 0.8
                tube(mb, [(0, 0.06, 0), (math.cos(a) * 0.35, 0.07, math.sin(a) * 0.35), (math.cos(a + 0.4) * 0.65, 0.05, math.sin(a + 0.4) * 0.65)], [0.012] * 3, 4, None, 0.0,
                     colfn=lambda t, a_, p: col((0.6, 0.15, 0.12), 0.1))
    elif "tendril" in n:           # black light-swallowing filaments, or rigid vibrating spikes
        spindle = "spindle" in n
        for i in range(24):
            a, r = H(seed + str(i), 1) * 6.28, H(seed + str(i), 2) * 0.3
            b = (math.cos(a) * r, 0, math.sin(a) * r)
            hgt = 0.8 + 0.8 * H(seed + str(i), 3)
            if spindle:
                cone(mb, b, add(b, (0.02, hgt, 0)), 0.012, col((0.75, 0.78, 0.7), 1.0), 4)
            else:
                strand(mb, b, add(b, (0.1, hgt, 0.05)), 0.01, (0.02, 0.02, 0.025), 6, 0.08, i)
    elif "pod" in n:               # green gas pods on short stems, or armoured woody pods
        bark = "bark" in n
        for i in range(5):
            a, r = i * 1.25, 0.12
            b = (math.cos(a) * r, 0, math.sin(a) * r)
            top = add(b, (0, 0.25 if not bark else 0.08, 0))
            if not bark:
                strand(mb, b, top, 0.01, hsv(0.3, 0.5, 0.4), 3, 0.02, i)
            ellipsoid(mb, top, (0.07, 0.11, 0.07) if not bark else (0.09, 0.07, 0.12), col(hsv(0.32, 0.75, 0.6) if not bark else hsv(0.07, 0.6, 0.3), 0.8), 8, 6)
    elif "root kelp" in n:         # a huge gnarled holdfast
        for i in range(9):
            a = i / 9 * 6.28
            pts = [(0, 0.9, 0), (math.cos(a) * 0.4, 0.5, math.sin(a) * 0.4), (math.cos(a) * 0.9, 0.12, math.sin(a) * 0.9), (math.cos(a) * 1.2, 0.0, math.sin(a) * 1.2)]
            tube(mb, pts, [0.16, 0.13, 0.09, 0.05], 6, None, 0.0, colfn=lambda t, a_, p: col(mix(hsv(0.08, 0.6, 0.3), (0.1, 0.06, 0.04), noise3((p[0] * 6, p[1] * 6, p[2] * 6), 4)), p[1] / 1.2))
        strand(mb, (0, 0.8, 0), (0, 1.6, 0), 0.18, hsv(0.08, 0.6, 0.3), 3, 0.0)
    elif "bulb cluster" in n:      # grape-like bladder bunches
        for i in range(18):
            ellipsoid(mb, (math.sin(i * 1.7) * 0.1, 0.1 + i * 0.035, math.cos(i * 2.3) * 0.1), (0.045, 0.045, 0.045), col(hsv(0.14, 0.6, 0.65), 0.5 + i / 36), 7, 5)
    elif "canopy" in n:            # thick woven vines (the canopy's ceiling)
        for i in range(8):
            pts = [(math.sin(j * 0.8 + i) * 1.2, 0.3 * math.sin(j + i * 0.5) + 0.4, (j - 4) * 0.4) for j in range(9)]
            tube(mb, pts, [0.05] * 9, 5, None, 0.0, colfn=lambda t, a, p: col(hsv(0.2, 0.5, 0.3), 0.8))
    else:                          # anything else: a generic frond clump
        for i in range(8):
            a = i * 0.8
            blade(mb, (0, 0, 0), norm((math.cos(a) * 0.4, 1, math.sin(a) * 0.4)), 0.6, 0.06, hsv(H(seed, 1), 0.5, 0.5), 0.3, 5, a)
    return mb

# ------------------------------------------------------------------------------------------------ the caverns' growth
NEON = {"blue": (0.25, 0.55, 1.0), "cyan": (0.2, 0.95, 1.0), "magenta": (1.0, 0.25, 0.85), "amber": (1.0, 0.62, 0.18),
        "green": (0.45, 1.0, 0.35), "white": (0.92, 0.95, 1.0), "red": (1.0, 0.25, 0.2), "violet": (0.65, 0.35, 1.0)}
PALE = (0.62, 0.6, 0.55)


def build_cave(f):
    """The Bioluminescent Caverns' twenty: everything grows along +y from the rock it clings to (Flora.cs turns +y to
    the wall's normal, so a ceiling's vines hang down). The glowing parts are bright (Flora.shader's _Glow lights them)."""
    n = f["name"].lower()
    seed = f["id"]
    g = NEON.get(f.get("color", "blue"), NEON["blue"])
    dim = mix(PALE, g, 0.25)
    mb = MB()
    if "lantern vine" in n:          # vines hanging from the ceiling with neon-blue seed bulbs
        for i in range(5):
            a = H(seed + str(i), 1) * 6.28
            b = (math.cos(a) * 0.2, 0, math.sin(a) * 0.2)
            L = 1.4 + 1.6 * H(seed + str(i), 2)
            strand(mb, b, add(b, (0.1, L, 0.05)), 0.02, mix(dim, (0.2, 0.3, 0.2), 0.5), 7, 0.08, i)
            for j in range(4):
                t = 0.35 + 0.2 * j
                ellipsoid(mb, add(b, (0.1 * t + 0.03, L * t, 0.05 * t)), (0.05, 0.07, 0.05), col(g, t), 7, 5)
    elif "phosphor mat" in n or "radiant lichen" in n or "pulse lichen" in n:   # a crust on the rock
        lumps(mb, (0, 0, 0), 0.7 if "mat" in n else 0.5, 26, g, seed, 0.18)
    elif "lantern shroom" in n or "beacon" in n or "strobe shroom" in n:      # squat caps on stalks
        big = "beacon" in n
        for i in range(6 if not big else 3):
            a, r = H(seed + str(i), 1) * 6.28, H(seed + str(i), 2) * 0.25
            h = (0.12 + 0.15 * H(seed + str(i), 3)) * (2.5 if big else 1)
            b = (math.cos(a) * r, 0, math.sin(a) * r)
            tube(mb, [b, add(b, (0, h, 0))], [0.03 * (2 if big else 1), 0.025 * (2 if big else 1)], 6, None, 0.0, colfn=lambda t, a_, p: col(PALE, p[1]))
            cr = (0.09 + 0.06 * H(seed + str(i), 4)) * (2.4 if big else 1)
            ellipsoid(mb, add(b, (0, h, 0)), (cr, cr * 0.45, cr), col(g, 1.0), 10, 5)
    elif "acid vine" in n:            # a pale green creeper over the rock
        for i in range(10):
            a = H(seed + str(i), 1) * 6.28
            pts = [(math.cos(a) * 0.08 * j + math.sin(j + i) * 0.05, 0.03 + 0.02 * math.sin(j * 1.7 + i), math.sin(a) * 0.08 * j) for j in range(8)]
            tube(mb, pts, [0.018] * 8, 4, None, 0.0, colfn=lambda t, a_, p: col(g, 0.1))
    elif "neon fungi" in n:           # thin shelves stacked up the wall
        for i in range(6):
            y = 0.08 + i * 0.12
            x = math.sin(i * 1.7) * 0.15
            membrane(mb, [(x - 0.12, y, 0.0), (x, y, 0.0), (x + 0.12, y, 0.0)], [(x - 0.1, y + 0.02, 0.12), (x, y + 0.03, 0.16), (x + 0.1, y + 0.02, 0.12)], 2, col(g, 0.2), 0.0, False)
            membrane(mb, [(x + 0.12, y - 0.004, 0.0), (x, y - 0.004, 0.0), (x - 0.12, y - 0.004, 0.0)], [(x + 0.1, y + 0.016, 0.12), (x, y + 0.026, 0.16), (x - 0.1, y + 0.016, 0.12)], 2, col(mul(g, 0.6), 0.2), 0.0, False)
    elif "crystal-moss" in n or "flicker mold" in n:   # fuzzy mould with calcite needles
        lumps(mb, (0, 0, 0), 0.35, 14, mix(PALE, g, 0.35), seed, 0.5)
        for i in range(18):
            a, r = H(seed + str(i), 5) * 6.28, H(seed + str(i), 6) * 0.3
            b = (math.cos(a) * r, 0.08, math.sin(a) * r)
            cone(mb, b, add(b, (math.cos(a) * 0.05, 0.12 + 0.1 * H(seed + str(i), 7), math.sin(a) * 0.05)), 0.012, col((0.9, 0.95, 1.0), 0.6), 4)
    elif "tubule" in n:               # clustered tubes with glowing mouths
        for i in range(9):
            a, r = H(seed + str(i), 1) * 6.28, H(seed + str(i), 2) * 0.2
            h = 0.15 + 0.25 * H(seed + str(i), 3)
            b = (math.cos(a) * r, 0, math.sin(a) * r)
            tube(mb, [b, add(b, (0.02, h, 0))], [0.035, 0.03], 7, None, 0.0, colfn=lambda t, a_, p: col(mix(PALE, g, 0.2), p[1]))
            ellipsoid(mb, add(b, (0.02, h, 0)), (0.032, 0.012, 0.032), col(g, 1.0), 7, 3)
    elif "spire" in n:                # a 3 m glowing spire from the cavern floor
        tube(mb, [(0, 0, 0), (0.05, 1.2, 0.02), (-0.04, 2.3, 0.0), (0.0, 3.0, 0.03)], [0.22, 0.16, 0.1, 0.04], 10, None, 0.0,
             colfn=lambda t, a_, p: col(mix(PALE, g, clamp(p[1] / 3.0) * 0.8), p[1] / 3.0))
        for i in range(7):
            y = 0.6 + i * 0.35
            ellipsoid(mb, (0.0, y, 0.0), (0.28 - 0.03 * i, 0.04, 0.28 - 0.03 * i), col(g, y / 3.0), 10, 3)
    elif "pouch" in n:                # round sap bladders
        for i in range(4):
            a, r = H(seed + str(i), 1) * 6.28, H(seed + str(i), 2) * 0.15
            rr = 0.08 + 0.08 * H(seed + str(i), 3)
            ellipsoid(mb, (math.cos(a) * r, rr * 0.9, math.sin(a) * r), (rr, rr, rr), col(g, 0.5), 10, 7)
    elif "filament" in n:             # hair-thin strands, a curtain in the current
        for i in range(30):
            x = (i - 15) * 0.03
            L = 0.8 + 1.2 * H(seed + str(i), 1)
            strand(mb, (x, 0, 0), (x + 0.05, L, 0.05), 0.004, g, 6, 0.06, i)
    elif "cluster" in n:              # grape-like nodules tucked into a crevice
        for i in range(16):
            a, r = H(seed + str(i), 1) * 6.28, H(seed + str(i), 2) * 0.15
            ellipsoid(mb, (math.cos(a) * r, 0.04 + H(seed + str(i), 3) * 0.12, math.sin(a) * r), (0.035, 0.035, 0.035), col(g, 0.5), 7, 5)
    else:
        lumps(mb, (0, 0, 0), 0.4, 12, g, seed, 0.4)
    return mb


# ------------------------------------------------------------------------------------------------ the vents' growth
VCOL = {"orange": (1.0, 0.45, 0.1), "red": (1.0, 0.2, 0.08), "amber": (1.0, 0.62, 0.18), "white": (1.0, 0.95, 0.85),
        "yellow": (0.95, 0.85, 0.2)}


def build_vent(f):
    """The Thermal Vents' twenty: soot-black, rust, sulphur and pale mineral growth on chimneys, basalt and sediment,
    a few glowing hot (their colour from the table). +y away from the rock."""
    n = f["name"].lower()
    seed = f["id"]
    glow = VCOL.get(f.get("color", "none"))
    soot, rust, sul, pale = (0.08, 0.07, 0.06), (0.45, 0.2, 0.08), (0.78, 0.68, 0.22), (0.82, 0.8, 0.74)
    mb = MB()
    if "tubeworm" in n:                 # white tubes with blood-red plumes (the plume tips glow)
        for i in range(12):
            a, r = H(seed + str(i), 1) * 6.28, H(seed + str(i), 2) * 0.35
            h = 0.6 + 1.2 * H(seed + str(i), 3)
            b = (math.cos(a) * r, 0, math.sin(a) * r)
            tube(mb, [b, add(b, (0.03, h, 0))], [0.05, 0.045], 7, None, 0.0, colfn=lambda t, a_, p: col(pale, p[1] / 1.8))
            ellipsoid(mb, add(b, (0.03, h + 0.08, 0)), (0.09, 0.14, 0.09), col(glow or (0.85, 0.12, 0.1), 1.0), 8, 5)
    elif "spores" in n:                 # puffs of spore cloud over a crusted base
        lumps(mb, (0, 0, 0), 0.3, 8, mix(soot, rust, 0.4), seed, 0.4)
        for i in range(14):
            a, r = H(seed + str(i), 1) * 6.28, H(seed + str(i), 2) * 0.4
            ellipsoid(mb, (math.cos(a) * r, 0.2 + 0.4 * H(seed + str(i), 3), math.sin(a) * r), (0.05, 0.05, 0.05), col(glow or mix(sul, pale, 0.5), 0.8), 6, 4)
    elif "moss" in n or "algae" in n:   # mats and cushions
        c = soot if "soot" in n else rust if ("pyre" in n or "eruptor" in n) else mix(soot, sul, 0.3)
        lumps(mb, (0, 0, 0), 0.6, 20, c, seed, 0.3)
    elif "grass" in n:                  # stiff blades that sway in the vents' draught
        for i in range(24):
            a, r = H(seed + str(i), 1) * 6.28, H(seed + str(i), 2) * 0.35
            d = norm((math.cos(a) * 0.15, 1, math.sin(a) * 0.15))
            blade(mb, (math.cos(a) * r, 0, math.sin(a) * r), d, 0.5 + 0.4 * H(seed + str(i), 3), 0.02, mix(sul, (0.4, 0.5, 0.2), 0.4) if "sulfur" in n else mix(soot, (0.3, 0.32, 0.25), 0.5), 0.2, 5, a)
    elif "filament" in n:               # hair-thin strands (the blaze filament burning white-hot)
        for i in range(26):
            x = (i - 13) * 0.025
            L = 0.4 + 0.8 * H(seed + str(i), 1)
            strand(mb, (x, 0, 0), (x + 0.04, L, 0.03), 0.005, glow or mix(soot, pale, 0.5), 5, 0.04, i)
    elif "crust" in n:                  # a mineral crust (copper green-gold in the forge's)
        c = mix(rust, (0.55, 0.42, 0.15), 0.6) if "forge" in n else mix(pale, sul, 0.4)
        lumps(mb, (0, 0, 0), 0.7, 24, c, seed, 0.18)
        if "forge" in n:
            for i in range(10):
                a, r = H(seed + str(i), 5) * 6.28, H(seed + str(i), 6) * 0.6
                ellipsoid(mb, (math.cos(a) * r, 0.05, math.sin(a) * r), (0.05, 0.03, 0.05), col((0.85, 0.5, 0.25), 0.3), 6, 3)
    elif "stalk" in n or "spire" in n:  # tall mineral stalks and spires
        tall = 1.6 if "spire" in n else 1.1
        for i in range(3 if "spire" in n else 6):
            a, r = H(seed + str(i), 1) * 6.28, H(seed + str(i), 2) * 0.3
            b = (math.cos(a) * r, 0, math.sin(a) * r)
            h = tall * (0.6 + 0.6 * H(seed + str(i), 3))
            tube(mb, [b, add(b, (0.02, h * 0.5, 0.03)), add(b, (0, h, 0))], [0.09, 0.06, 0.02], 7, None, 0.0,
                 colfn=lambda t, a_, p: col(mix(soot, pale if "ash" in n else sul, clamp(p[1] / 1.6)), p[1] / 1.6))
    elif "cluster" in n:                # knobbly nodules
        for i in range(16):
            a, r = H(seed + str(i), 1) * 6.28, H(seed + str(i), 2) * 0.25
            ellipsoid(mb, (math.cos(a) * r, 0.05 + H(seed + str(i), 3) * 0.2, math.sin(a) * r), (0.06, 0.06, 0.06), col(glow or mix(rust, sul, 0.3), 0.5), 7, 5)
    elif "bloom" in n:                  # a cinder-orange flower of fleshy petals
        for i in range(8):
            a = i / 8 * math.pi * 2
            d = norm((math.cos(a), 0.6, math.sin(a)))
            blade(mb, (0, 0.15, 0), d, 0.35, 0.09, glow or (1.0, 0.5, 0.15), 0.4, 4, a)
        strand(mb, (0, 0, 0), (0, 0.18, 0), 0.03, soot, 3, 0.0)
    else:
        lumps(mb, (0, 0, 0), 0.4, 12, mix(soot, rust, 0.5), seed, 0.4)
    return mb


def main():
    a = K.args()
    out = a[a.index("--out") + 1] if "--out" in a else os.path.join(K.ROOT, "TheDeep", "Assets", "Deep", "Resources", "Flora")
    os.makedirs(out, exist_ok=True)
    biomes = ("shallows", "kelp", "caverns", "vents")
    if "--biome" in a:
        biomes = (a[a.index("--biome") + 1],)
    for biome in biomes:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        with open(os.path.join(K.DATA, f"species_{biome}.json"), encoding="utf-8") as fh:
            tab = json.load(fh)
        objs, tris = [], 0
        for f in tab["flora"]:
            if "iron-kelp" in f["name"].lower():
                continue
            mb = build_cave(f) if biome == "caverns" else build_vent(f) if biome == "vents" else build(f, biome)
            if not mb.f:
                continue
            objs.append(K.to_object(f["id"], mb))
            tris += sum(len(x) - 2 for x in mb.f)
        for o in objs:
            o.select_set(True)
        path = os.path.join(out, f"flora_{biome}.glb")
        bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=True, export_yup=True,
                                  export_texcoords=True, export_normals=True, export_tangents=False, export_materials='NONE',
                                  export_vertex_color='ACTIVE', export_all_vertex_colors=False)
        print(f"artgen: wrote {path}: {len(objs)} plants, {tris} triangles, {os.path.getsize(path) // 1024} KB")


if __name__ == "__main__":
    main()
