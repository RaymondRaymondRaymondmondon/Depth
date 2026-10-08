# The Deep's sea life (stage 3): a model for every species in the design doc's rosters (TheDeep/Assets/Deep/Resources/
# Data/species_<biome>.json), built from its body plan and its name, one metre long along +z (head forward), written
# as one glTF per biome with an object per species (named by its id) for Life.cs to draw instanced.
#   - Bodies are lofted cross-sections with real profiles (snouts, gill covers, peduncles), smooth-shaded; fins are
#     ruffled membranes with rays; eyes are spheres with an iris and a pupil; legs, arms, tentacles and spines are
#     tapered tubes; shells are spirals, valves and cone clusters.
#   - Colour is in the vertex colours (no textures: the repo is public and the PC is an Intel UHD): each species' own
#     palette from its name and role (reef colours, countershaded open-water silver, kelp browns and olives,
#     ghostly translucency) with its pattern - bars, a lateral stripe, spots, mottling, scutes, an orca's markings.
#     Vertex alpha is how much light passes through (fins and jellies).
#   - UV0 carries the swim for Creature.shader: u = flap weight (wings, flippers, legs, a pulsing mantle), v = how far
#     back along the body (0 at the nose, 1 at the tail tip).
# The frame here is Unity's (x right, y up, z forward); Gc() converts for Blender so the glTF arrives that way.
#     blender -b --factory-startup -P tools/artgen/deep_creatures.py -- --out TheDeep/Assets/Deep/Resources/Creatures
import bpy
import bmesh
import json
import math
import os
import sys
import colorsys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DATA = os.path.join(ROOT, "TheDeep", "Assets", "Deep", "Resources", "Data")


def args():
    a = sys.argv
    return a[a.index("--") + 1:] if "--" in a else []


# ------------------------------------------------------------------------------------------------ small vector maths
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, k): return (a[0] * k, a[1] * k, a[2] * k)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def length(a): return math.sqrt(dot(a, a))
def norm(a):
    l = length(a)
    return (a[0] / l, a[1] / l, a[2] / l) if l > 1e-9 else (0.0, 1.0, 0.0)
def lerp(a, b, t): return a + (b - a) * t
def lerp3(a, b, t): return (lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t))
def clamp(x, a=0.0, b=1.0): return max(a, min(b, x))
def smooth(a, b, x):
    t = clamp((x - a) / (b - a)) if b != a else 0.0
    return t * t * (3 - 2 * t)
def mix(c1, c2, t): return tuple(lerp(c1[i], c2[i], t) for i in range(len(c1)))


def H(s, k=0):
    h = 7 + k * 131
    for ch in s:
        h = (h * 37 + ord(ch)) & 0xffffffff
    h ^= h >> 13; h = (h * 0x5bd1e995) & 0xffffffff; h ^= h >> 15
    return (h & 0xffff) / 65535.0


def noise3(p, k=0):
    """Cheap value noise for mottling."""
    x, y, z = p
    s = math.sin(x * 12.9898 + y * 78.233 + z * 37.719 + k * 11.3) * 43758.5453
    s2 = math.sin(x * 4.1 + y * 7.3 - z * 3.7 + k) * 0.5 + math.sin(x * 9.7 - y * 2.9 + z * 6.1 + k * 2) * 0.5
    return clamp(0.5 + 0.35 * s2 + 0.15 * (s - math.floor(s) - 0.5))


# ------------------------------------------------------------------------------------------------ the mesh builder
class MB:
    def __init__(self):
        self.v, self.c, self.uv, self.f = [], [], [], []

    def vert(self, p, col, flap=0.0, tail=None):
        if tail is None:
            tail = clamp(0.5 - p[2])
        self.v.append(p)
        self.c.append(col if len(col) == 4 else (col[0], col[1], col[2], 0.0))
        self.uv.append((flap, tail))
        return len(self.v) - 1

    def face(self, *ix):
        self.f.append(tuple(ix))


def frame(tangent, up_hint=(0.0, 1.0, 0.0)):
    t = norm(tangent)
    side = cross(up_hint, t)
    if length(side) < 1e-4:
        side = cross((1.0, 0.0, 0.0), t)
    side = norm(side)
    up = norm(cross(t, side))
    return t, side, up


def loft(mb, rings, segs, colfn, flapfn=None, cap0=True, cap1=True, tailfn=None):
    """rings: list of (centre, tangent, rw, rh, shape) along a spine (first = the tail end); a cross-section ellipse
    rw wide and rh high (shape(a) can bulge it). colfn(i/n, angle, point) -> colour."""
    n = len(rings) - 1
    base = len(mb.v)
    for i, (c, tg, rw, rh, shape) in enumerate(rings):
        t, side, up = frame(tg)
        for k in range(segs + 1):
            a = k / segs * math.pi * 2
            s = shape(a) if shape else 1.0
            p = add(c, add(mul(side, math.sin(a) * rw * s), mul(up, math.cos(a) * rh * s)))
            fl = flapfn(i / n, a, p) if flapfn else 0.0
            tl = tailfn(i / n, p) if tailfn else None
            mb.vert(p, colfn(i / n, a, p), fl, tl)
    for i in range(n):
        for k in range(segs):
            a = base + i * (segs + 1) + k
            b = a + segs + 1
            mb.face(a, a + 1, b + 1, b)
    for end, cap in ((0, cap0), (n, cap1)):
        if not cap:
            continue
        c, tg, rw, rh, _ = rings[end]
        tip = add(c, mul(norm(tg), (-1 if end == 0 else 1) * min(rw, rh) * 0.3))
        ci = mb.vert(tip, colfn(end / max(1, n), 0.0, tip), flapfn(end / max(1, n), 0.0, tip) if flapfn else 0.0, tailfn(end / max(1, n), tip) if tailfn else None)
        ring0 = base + end * (segs + 1)
        for k in range(segs):
            if end == 0:
                mb.face(ci, ring0 + k + 1, ring0 + k)
            else:
                mb.face(ci, ring0 + k, ring0 + k + 1)


def straight_rings(z0, z1, n, radius, rw=1.0, rh=1.0, shape=None, y=0.0, x=0.0):
    """Rings along +z from z0 (tail) to z1 (nose): radius(t) for t in 0..1 from the tail."""
    out = []
    for i in range(n + 1):
        t = i / n
        r = radius(t)
        out.append(((x, y if not callable(y) else y(t), lerp(z0, z1, t)), (0, 0, 1), max(1e-4, r * rw), max(1e-4, r * rh), shape))
    return out


def tube(mb, pts, radii, segs, col, flap=0.0, cap=True, colfn=None):
    """A tapered tube along a polyline (legs, arms, tentacles, barbels, spines)."""
    rings = []
    for i, p in enumerate(pts):
        tg = sub(pts[min(i + 1, len(pts) - 1)], pts[max(i - 1, 0)])
        rings.append((p, tg, radii[i], radii[i], None))
    fl = flap if callable(flap) else (lambda t, a, p: flap)
    loft(mb, rings, segs, colfn or (lambda t, a, p: col), fl, cap0=cap, cap1=cap)


def ellipsoid(mb, c, r, col, segs=10, rings=6, flap=0.0, colfn=None):
    base = len(mb.v)
    for i in range(rings + 1):
        th = math.pi * i / rings
        for k in range(segs + 1):
            ph = 2 * math.pi * k / segs
            p = (c[0] + r[0] * math.sin(th) * math.cos(ph), c[1] + r[1] * math.cos(th), c[2] + r[2] * math.sin(th) * math.sin(ph))
            mb.vert(p, colfn(p) if colfn else col, flap if not callable(flap) else flap(p))
    for i in range(rings):
        for k in range(segs):
            a = base + i * (segs + 1) + k
            b = a + segs + 1
            mb.face(a, b, b + 1, a + 1)


def cone(mb, base, tip, radius, col, segs=6, flap=0.0):
    t, side, up = frame(sub(tip, base))
    b0 = len(mb.v)
    for k in range(segs):
        a = 2 * math.pi * k / segs
        mb.vert(add(base, add(mul(side, math.cos(a) * radius), mul(up, math.sin(a) * radius))), col, flap)
    ti = mb.vert(tip, col, flap)
    for k in range(segs):
        mb.face(b0 + k, b0 + (k + 1) % segs, ti)


def membrane(mb, root, tip, rows, col, flap=0.0, rays=True, ruffle=0.0, edge_dark=0.35):
    """A fin: a grid from the root line (points on the body) out to the tip outline (same count), with rays and a
    ruffled edge; translucent (alpha)."""
    n = len(root)
    base = len(mb.v)
    for r in range(rows + 1):
        t = r / rows
        for i in range(n):
            p = lerp3(root[i], tip[i], t)
            if ruffle and 0 < i < n - 1:
                nrm = norm(cross(sub(tip[i], root[i]), sub(root[min(i + 1, n - 1)], root[max(i - 1, 0)])))
                p = add(p, mul(nrm, math.sin(i * 1.7) * ruffle * t))
            ray = 0.82 if rays and i % 2 == 0 else 1.0
            c = mix(col, mul(col[:3], 0.55) + (col[3],), clamp(t * t * edge_dark * 2)) if len(col) == 4 else col
            c = (c[0] * ray, c[1] * ray, c[2] * ray, (c[3] if len(c) == 4 else 0.8))
            mb.vert(p, c, flap(t, i / max(1, n - 1)) if callable(flap) else flap * t)
    for r in range(rows):
        for i in range(n - 1):
            a = base + r * n + i
            b = a + n
            mb.face(a, a + 1, b + 1, b)


def eye(mb, c, r, side, iris=(0.75, 0.62, 0.3), glow=False):
    """An eye on the side `side` (+1 right, -1 left): a dark glossy ball with a ring of iris."""
    def col(p):
        d = (p[0] - c[0]) * side
        if d > r * 0.75:
            return (0.01, 0.01, 0.015, 0.0)
        if d > r * 0.35:
            return iris + (0.0,) if not glow else (0.5, 0.9, 0.8, 1.0)
        return (0.08, 0.08, 0.08, 0.0)
    ellipsoid(mb, c, (r, r, r), None, 8, 6, 0.0, col)


# ------------------------------------------------------------------------------------------------ palettes
KEYHUES = [  # (keyword, hue, saturation boost) - read from the names
    ("sun", 0.12, 0.25), ("golden", 0.11, 0.3), ("gilded", 0.12, 0.3), ("turquoise", 0.48, 0.3), ("lagoon", 0.5, 0.1),
    ("coral", 0.02, 0.25), ("crimson", 0.0, 0.35), ("rose", 0.95, 0.2), ("pearl", 0.1, -0.35), ("silt", 0.08, -0.3),
    ("mud", 0.07, -0.3), ("kelp", 0.17, -0.1), ("frond", 0.22, -0.05), ("thicket", 0.15, -0.15), ("holdfast", 0.09, -0.15),
    ("shroud", 0.6, -0.35), ("wraith", 0.55, -0.4), ("murk", 0.15, -0.35), ("glimmer", 0.55, -0.45), ("shimmer", 0.55, -0.3),
    ("petal", 0.92, 0.25), ("bloom", 0.9, 0.15), ("lace", 0.8, 0.0), ("violet", 0.78, 0.2), ("azure", 0.58, 0.3),
    ("spire", 0.95, 0.1), ("tang", 0.6, 0.35), ("parrot", 0.42, 0.3), ("bleach", 0.1, -0.5), ("iron", 0.06, -0.3),
    ("stripe", 0.08, 0.15), ("tiger", 0.08, 0.3), ("jade", 0.38, 0.2), ("ember", 0.04, 0.35), ("ink", 0.7, -0.2),
]


class Look:
    def __init__(self, sp, biome):
        n = sp["name"].lower()
        self.n = n
        hue, sat = H(n, 1), 0.45 + 0.35 * H(n, 2)
        for kw, h, s in KEYHUES:
            if kw in n:
                hue, sat = h + (H(n, 3) - 0.5) * 0.06, clamp(sat + s, 0.08, 0.95)
                break
        if biome == "kelp":
            sat *= 0.75
        val = 0.55 + 0.3 * H(n, 4)
        # the caverns: blind, pale animals (cream, pink, ghost-white), the glowing ones with neon organs
        self.neon = None
        if biome == "caverns":
            sat *= 0.28
            val = 0.68 + 0.22 * H(n, 4)
            if sp.get("light") in ("glow", "strobe") or any(k in n for k in ("lumen", "glow", "neon", "lantern", "lamp", "strobe", "flash", "magenta", "ember", "spark", "halo")):
                nh = 0.85 if "magenta" in n else 0.08 if ("ember" in n or "amber" in n) else 0.5 if ("glass" in n or "halo" in n) else 0.6
                self.neon = colorsys.hsv_to_rgb(nh, 0.85, 1.0)
        # the vents: armour-plated animals in soot, basalt grey and rust, the heat-proof ones with ember-orange markings
        if biome == "vents":
            hue = 0.03 + 0.06 * H(n, 1)
            sat = 0.35 + 0.3 * H(n, 2)
            val = 0.35 + 0.25 * H(n, 4)
            if sp.get("heatProof"):
                self.neon = colorsys.hsv_to_rgb(0.06, 0.9, 1.0)
        open_water = sp.get("habitat") == "open" or any(k in n for k in ("shark", "sardine", "shoal", "jack", "basker", "halfbeak", "lancer", "pike"))
        if open_water:
            sat *= 0.35
        self.open = open_water
        self.top = colorsys.hsv_to_rgb(hue % 1, sat, val * 0.62)
        self.belly = mix(colorsys.hsv_to_rgb(hue % 1, sat * 0.25, 0.9), (0.92, 0.9, 0.84), 0.55)
        self.accent = colorsys.hsv_to_rgb((hue + 0.08 + 0.4 * (H(n, 5) > 0.6)) % 1, clamp(sat + 0.25), clamp(val + 0.25))
        if self.neon is not None:
            self.accent = self.neon
        self.fin = mix(self.top, self.accent, 0.45)
        self.ghost = any(k in n for k in ("wraith", "shroud", "glass", "ghost", "halo", "veil"))
        # the pattern, from the name first, then by chance
        p = None
        for kws, pat in ((("stripe", "banded", "krait", "tiger", "zebra"), "bars"), (("jack", "lancer", "pike", "halfbeak", "sardine", "shoal", "glimmer"), "lateral"),
                         (("pearl", "spotted", "spot", "dot", "star"), "spots"), (("scorpion", "sculpin", "lurker", "grouper", "mimic", "angler", "wolffish", "stonefish"), "mottle"),
                         (("orca",), "orca"), (("turtle",), "scutes")):
            if any(k in n for k in kws):
                p = pat
                break
        if p is None and self.neon is not None:
            p = "spots" if H(n, 8) < 0.5 else "lateral"
        if p is None:
            r = H(n, 6)
            p = "bars" if r < 0.2 else "spots" if r < 0.35 else "mottle" if r < 0.5 else "plain"
        self.pattern = p
        self.bars = 4 + int(H(n, 7) * 5)

    def body(self, t, a, p):
        """t along the body (0 tail..1 nose), a the angle round it (0 on the back)."""
        up = math.cos(a)
        c = mix(self.belly, self.top, smooth(-0.55, 0.55, up))
        if self.open:   # silvered flanks
            c = mix(c, (0.78, 0.82, 0.86), 0.35 * clamp(1 - abs(up) * 1.3))
        if self.pattern == "bars" and (t * self.bars) % 1 < 0.33 and up > -0.45:
            c = mix(c, mul(self.accent, 0.7), 0.7)
        elif self.pattern == "lateral" and abs(up) < 0.18:
            c = mix(c, self.accent, 0.75)
        elif self.pattern == "spots":
            q = (math.sin(p[2] * 61 + p[1] * 23) * math.sin(p[0] * 47 + p[2] * 31 + a * 3))
            if q > 0.62 and up > -0.3:
                c = mix(c, self.accent, 0.8)
        elif self.pattern == "mottle":
            c = mix(c, mul(c, 0.55), noise3((p[0] * 9, p[1] * 9, p[2] * 9), 3))
        elif self.pattern == "orca":
            white = up < -0.25 or (0.62 < t < 0.75 and 0.0 < up < 0.7 and abs(math.sin(a)) > 0.5)
            c = (0.92, 0.92, 0.9) if white else (0.03, 0.03, 0.035)
        elif self.pattern == "scutes":
            q = abs(math.sin(p[2] * 18)) * abs(math.sin(a * 3))
            c = mix(c, mul(c, 0.5), 1.0 if q < 0.12 else 0.0)
        alpha = 0.55 if self.ghost else 0.0
        return (c[0], c[1], c[2], alpha)

    def finc(self, alpha=0.75):
        f = self.fin
        return (f[0], f[1], f[2], alpha if not self.ghost else 0.95)


# ------------------------------------------------------------------------------------------------ body plans
def fish(mb, sp, L):
    n = L.n
    deep = any(k in n for k in ("butterfly", "angel", "tang", "surgeon", "trigger", "bream", "discus", "pomfret"))
    long_ = any(k in n for k in ("lancer", "pike", "halfbeak", "barracuda", "needle", "blenny", "goby", "pipe"))
    heavy = any(k in n for k in ("grouper", "wolffish", "maw", "titan", "sculpin", "scorpion", "angler", "puffer"))
    hr = 0.17 + 0.1 * H(n, 8) + (0.17 if deep else 0) - (0.08 if long_ else 0) + (0.05 if heavy else 0)
    wr = (0.09 + 0.04 * H(n, 9)) * (0.6 if deep else 1.0) * (1.3 if heavy else 1.0)
    if "puffer" in n:
        hr, wr = 0.32, 0.3
    # the profile: a peduncle at the tail, the fullest third of the way back from the head, a snout
    def prof(t):
        if t < 0.1:
            return lerp(0.13, 0.22, t / 0.1)
        x = (t - 0.1) / 0.9
        return max(0.03, math.sin(math.pi * lerp(0.12, 1.0, x)) ** 0.62 * (1.0 - 0.15 * (x > 0.85) * (x - 0.85) / 0.15))
    segs, nr = (10, 12) if sp["sizeM"] < 0.15 else (14, 20)
    mouth_t = 0.985
    def col(t, a, p):
        c = L.body(t, a, p)
        if t > 0.955 and abs(math.cos(a) + 0.05) < 0.22:
            c = (0.12, 0.08, 0.07, 0.0)           # the mouth line
        if 0.78 < t < 0.8 and abs(math.cos(a)) < 0.75:
            c = mix(c, mul(c[:3], 0.6) + (0.0,), 0.6)   # the gill cover's edge
        return c
    rings = straight_rings(-0.43, 0.5, nr, prof, wr, hr)
    loft(mb, rings, segs, col)
    # the eyes
    er = 0.022 + 0.012 * H(n, 10)
    ez = 0.36
    for s in (-1, 1):
        eye(mb, (s * wr * prof(0.86) * 0.92, hr * prof(0.86) * 0.35, ez), er, s)
    # the tail: forked, lunate, rounded or truncate
    tt = H(n, 11)
    fc = L.finc()
    h = hr * (0.95 + 0.35 * H(n, 12))
    if tt < 0.4:     # forked
        tip = [(0, h, -0.62), (0, h * 0.55, -0.6), (0, h * 0.15, -0.53), (0, -h * 0.15, -0.53), (0, -h * 0.55, -0.6), (0, -h, -0.62)]
    elif tt < 0.6:   # lunate
        tip = [(0, h * 1.1, -0.66), (0, h * 0.5, -0.57), (0, 0.0, -0.54), (0, 0.0, -0.54), (0, -h * 0.5, -0.57), (0, -h * 1.1, -0.66)]
    elif tt < 0.85:  # rounded
        tip = [(0, h * 0.55, -0.55), (0, h * 0.7, -0.62), (0, h * 0.35, -0.67), (0, -h * 0.35, -0.67), (0, -h * 0.7, -0.62), (0, -h * 0.55, -0.55)]
    else:            # truncate
        tip = [(0, h * 0.7, -0.6), (0, h * 0.5, -0.62), (0, h * 0.2, -0.62), (0, -h * 0.2, -0.62), (0, -h * 0.5, -0.62), (0, -h * 0.7, -0.6)]
    root = [(0, hr * 0.13 * (1 - 2 * i / 5), -0.43) for i in range(6)]
    membrane(mb, root, tip, 3, fc, 0.0, True, 0.004)
    # the dorsal: one long fin, or a spiny front and a soft back
    spiny = heavy or "scorpion" in n or "trigger" in n or H(n, 13) > 0.65
    k = 7
    d0, d1 = (0.25, -0.3) if not long_ else (0.05, -0.35)
    root = [(0, hr * prof(lerp(0.1, 1, (0.5 - lerp(d0, d1, i / (k - 1)) - 0.07) / 0.93)) * 0.95, lerp(d0, d1, i / (k - 1))) for i in range(k)]
    tip = []
    for i, r in enumerate(root):
        hh = hr * (0.2 + 0.32 * math.sin(math.pi * (i + 0.5) / k) * (1.25 if spiny and i < 4 else 1.0) + 0.1 * H(n, 14))
        tip.append((0, r[1] + hh, r[2] - 0.05))
    membrane(mb, root, tip, 3, fc, 0.0, True, 0.006)
    if spiny:
        for i in range(0, 4):
            cone(mb, root[i], add(tip[i], (0, hr * 0.12, 0)), 0.004, mul(fc[:3], 0.7) + (0.0,), 4)
    # the anal fin
    root = [(0, -hr * 0.7, lerp(-0.1, -0.33, i / 4)) for i in range(5)]
    tip = [(0, r[1] - hr * (0.3 + 0.15 * math.sin(math.pi * i / 4)), r[2] - 0.04) for i, r in enumerate(root)]
    membrane(mb, root, tip, 2, fc, 0.0, True, 0.004)
    # the pectorals and pelvics (they beat a little: flap)
    for s in (-1, 1):
        x = s * wr * 0.8
        membrane(mb, [(x, -hr * 0.15, 0.2), (x, -hr * 0.3, 0.17), (x, -hr * 0.42, 0.15)],
                 [(x + s * 0.13, -hr * 0.25, 0.08), (x + s * 0.16, -hr * 0.5, 0.03), (x + s * 0.1, -hr * 0.65, 0.04)], 3, fc, 0.6, True, 0.003)
        membrane(mb, [(s * wr * 0.4, -hr * 0.8, 0.08), (s * wr * 0.3, -hr * 0.85, 0.03)],
                 [(s * wr * 0.7, -hr * 1.15, -0.04), (s * wr * 0.5, -hr * 1.05, -0.06)], 2, fc, 0.3, True)
    if "angler" in n:   # the lure
        tube(mb, [(0, hr * 0.8, 0.38), (0, hr * 1.6, 0.42), (0, hr * 1.8, 0.55)], [0.006, 0.005, 0.004], 4, fc)
        ellipsoid(mb, (0, hr * 1.8, 0.57), (0.025, 0.025, 0.025), (0.6, 1.0, 0.85, 1.0), 6, 4)
    if "puffer" in n:
        for i in range(60):
            a, b = H(n + str(i), 1) * math.pi * 2, H(n + str(i), 2) * math.pi - math.pi / 2
            d = (math.cos(b) * math.cos(a), math.sin(b), math.cos(b) * math.sin(a))
            base = (d[0] * wr * 0.95, d[1] * hr * 0.95, d[2] * 0.3)
            cone(mb, base, add(base, mul(d, 0.06)), 0.006, mul(L.top, 0.6) + (0.0,), 4)
    if "halfbeak" in n:
        tube(mb, [(0, -hr * 0.2, 0.48), (0, -hr * 0.25, 0.65)], [0.012, 0.004], 5, L.body(1, 3.14, (0, 0, 0.5)))
    if "parrot" in n:   # the beak
        ellipsoid(mb, (0, 0, 0.49), (wr * 0.6, hr * 0.35, 0.04), (0.85, 0.85, 0.75, 0.0), 8, 5)


def shark(mb, sp, L):
    n = L.n
    basker = "basker" in n
    def prof(t):
        if t < 0.12:
            return lerp(0.07, 0.15, t / 0.12)
        x = (t - 0.12) / 0.88
        return max(0.03, math.sin(math.pi * lerp(0.15, 0.97, x)) ** 0.55)
    hr, wr = 0.085, 0.095
    def col(t, a, p):
        c = L.body(t, a, p)
        if 0.72 < t < 0.8 and abs(math.sin(a)) > 0.8 and int(t * 120) % 3 == 0:
            c = (0.1, 0.1, 0.12, 0.0)       # gill slits
        if basker and t > 0.93 and abs(math.cos(a) + 0.2) < 0.45:
            c = (0.05, 0.03, 0.03, 0.0)     # the great gape
        return c
    loft(mb, straight_rings(-0.45, 0.5, 26, prof, wr, hr, y=lambda t: -0.01 * t), 16, col)
    for s in (-1, 1):
        eye(mb, (s * wr * 0.62, hr * 0.35, 0.39), 0.012, s, (0.2, 0.2, 0.15))
    fc = L.finc(0.0)
    membrane(mb, [(0, hr * 0.92, 0.12), (0, hr * 0.95, 0.04), (0, hr * 0.9, -0.04)], [(0, hr * 1.0, 0.11), (0, hr * 3.6, -0.06), (0, hr * 1.0, -0.06)], 3, fc, 0.0, False)
    membrane(mb, [(0, hr * 0.5, -0.25), (0, hr * 0.45, -0.3)], [(0, hr * 1.4, -0.3), (0, hr * 0.5, -0.32)], 2, fc, 0.0, False)
    membrane(mb, [(0, 0.01, -0.43), (0, 0.0, -0.45), (0, -0.01, -0.45)], [(0, 0.3, -0.62), (0, 0.08, -0.58), (0, -0.16, -0.56)], 3, fc, 0.0, False)
    for s in (-1, 1):
        membrane(mb, [(s * wr * 0.8, -hr * 0.5, 0.22), (s * wr * 0.8, -hr * 0.5, 0.13)], [(s * 0.3, -hr * 1.6, 0.05), (s * wr, -hr * 0.6, 0.1)], 3, fc, 0.5, False)
        membrane(mb, [(s * wr * 0.4, -hr * 0.8, -0.18), (s * wr * 0.3, -hr * 0.8, -0.23)], [(s * 0.1, -hr * 1.4, -0.26), (s * wr * 0.4, -hr * 0.9, -0.25)], 2, fc, 0.3, False)


def ray(mb, sp, L):
    n = L.n
    manta = "manta" in n or "veil" in n
    span = 0.62 if manta else 0.5
    R, C = 12, 20
    base = len(mb.v)
    for i in range(R + 1):
        z = lerp(-0.3, 0.42, i / R)
        half = span * max(0.03, 1 - abs((z - 0.08) / 0.36) ** (1.4 if manta else 1.1))
        for k in range(C + 1):
            u = lerp(-1, 1, k / C)
            x = u * half
            th = 0.07 * (1 - u * u) * (1 - abs(z - 0.08) * 1.4)
            wing = abs(u)
            y_top = th + (0.08 * wing * wing if manta else 0)
            c = L.body(z + 0.5, 0.0, (x, y_top, z))
            mb.vert((x, y_top, z), c, wing)
    for i in range(R):
        for k in range(C):
            a = base + i * (C + 1) + k
            b = a + C + 1
            mb.face(a, a + 1, b + 1, b)
    base2 = len(mb.v)
    for i in range(R + 1):     # the pale underside
        z = lerp(-0.3, 0.42, i / R)
        half = span * max(0.03, 1 - abs((z - 0.08) / 0.36) ** (1.4 if manta else 1.1))
        for k in range(C + 1):
            u = lerp(-1, 1, k / C)
            x = u * half
            th = -0.03 * (1 - u * u) * (1 - abs(z - 0.08) * 1.4)
            mb.vert((x, th + (0.08 * u * u if manta else 0), z), L.belly + (0.0,), abs(u))
    for i in range(R):
        for k in range(C):
            a = base2 + i * (C + 1) + k
            b = a + C + 1
            mb.face(a, b, b + 1, a + 1)
    for s in (-1, 1):
        eye(mb, (s * 0.07, 0.06, 0.32), 0.02, s)
        if manta:   # the cephalic lobes
            tube(mb, [(s * 0.08, 0.02, 0.4), (s * 0.1, 0.0, 0.5), (s * 0.08, -0.03, 0.56)], [0.025, 0.02, 0.012], 6, L.top + (0.0,))
    tube(mb, [(0, 0.02, -0.28), (0, 0.02, -0.5), (0, 0.03, -0.7), (0, 0.04, -0.9)], [0.025, 0.014, 0.008, 0.003], 6, L.top + (0.0,))
    if not manta:
        cone(mb, (0, 0.03, -0.45), (0, 0.05, -0.58), 0.008, (0.85, 0.8, 0.7, 0.0), 4)


def eel(mb, sp, L, krait=False, lamprey=False):
    n = L.n
    rad = 0.035 if not krait else 0.03
    def prof(t):
        return max(0.15, math.sin(math.pi * lerp(0.03, 0.97, t)) ** 0.3)
    loft(mb, straight_rings(-0.5, 0.5, 48, prof, rad, rad * 1.15), 10, L.body)
    for s in (-1, 1):
        eye(mb, (s * rad * 0.7, rad * 0.4, 0.46), rad * 0.3, s)
    fc = L.finc()
    if krait:
        membrane(mb, [(0, rad, -0.42), (0, 0.0, -0.5), (0, -rad, -0.42)], [(0, rad * 3, -0.48), (0, 0.0, -0.56), (0, -rad * 3, -0.48)], 2, fc, 0.0)
    else:
        k = 14
        root = [(0, rad * 0.9, lerp(0.25, -0.5, i / (k - 1))) for i in range(k)]
        membrane(mb, root, [(0, r[1] + rad * 1.1, r[2]) for r in root], 2, fc, 0.0, True)
        root = [(0, -rad * 0.9, lerp(-0.05, -0.5, i / (k - 1))) for i in range(k)]
        membrane(mb, root, [(0, r[1] - rad * 0.9, r[2]) for r in root], 2, fc, 0.0, True)
    if lamprey:
        ellipsoid(mb, (0, 0, 0.5), (rad * 1.3, rad * 1.3, rad * 0.4), (0.35, 0.15, 0.15, 0.0), 10, 5)
    if "moray" in n or "creeper" in n or "maw" in n:
        tube(mb, [(0, -rad * 0.3, 0.44), (0, -rad * 0.5, 0.5)], [rad * 0.6, rad * 0.4], 6, (0.15, 0.06, 0.05, 0.0))


def worm(mb, sp, L):
    n = L.n
    if any(k in n for k in ("feather duster", "christmas", "fan worm", "tubeworm")):
        # a tube in the seabed and a crown of feathery radioles
        tube(mb, [(0, 0, 0), (0, 0.25, 0), (0, 0.5, 0)], [0.06, 0.055, 0.05], 8, mix(L.top, (0.5, 0.45, 0.4), 0.6) + (0.0,))
        spiral = "christmas" in n
        for i in range(16):
            a = i / 16 * math.pi * 2
            d = (math.cos(a), 0, math.sin(a))
            pts = [(0, 0.5, 0)]
            for k in range(1, 5):
                h = k / 4
                r = 0.06 + h * 0.22
                aa = a + (h * 2.5 if spiral else 0)
                pts.append((math.cos(aa) * r, 0.5 + h * (0.35 if not spiral else 0.45), math.sin(aa) * r))
            tube(mb, pts, [0.012, 0.01, 0.008, 0.006, 0.003], 4, L.accent + (0.6,), lambda t, a_, p: t)
        return
    bristle = "bristle" in n
    def prof(t):
        return max(0.2, math.sin(math.pi * lerp(0.03, 0.97, t)) ** 0.4) * (1 + (0.12 * abs(math.sin(t * 60)) if bristle or "cucumber" in n else 0))
    rad = 0.06 if "cucumber" not in n else 0.16
    loft(mb, straight_rings(-0.5, 0.5, 40, prof, rad, rad * 0.8, y=rad * 0.8), 10, L.body)
    if bristle:
        for i in range(30):
            z = lerp(-0.45, 0.45, i / 29)
            for s in (-1, 1):
                cone(mb, (s * rad * 0.9, rad * 0.8, z), (s * rad * 2.2, rad * 0.9, z - 0.01), 0.006, L.accent + (0.5,), 3, 1.0)
    if "cucumber" in n:
        for i in range(10):
            a = i / 10 * math.pi * 2
            tube(mb, [(0, rad * 0.8, 0.5), (math.cos(a) * 0.05, rad * 0.8 + math.sin(a) * 0.05, 0.56)], [0.012, 0.004], 4, L.accent + (0.3,))


def leviathan(mb, sp, L):
    """The giants: a long armoured body that undulates, a heavy head with a lower jaw and teeth, scute plates down the
    back, paired fins and a tail fluke. Each of the four has its own build."""
    n = L.n
    crusher = "reef-crusher" in n
    coral = "coral-crusher" in n
    tangle = "tangle" in n
    shroud = "shroud" in n
    rad = 0.115 if crusher else 0.12 if coral else 0.055 if tangle else 0.065
    hz = 0.5
    def prof(t):
        body = math.sin(math.pi * lerp(0.04, 0.88, t)) ** (0.45 if not tangle else 0.25)
        head = smooth(0.82, 0.9, t) * (0.25 if crusher or coral else 0.12)
        return max(0.06, body + head - smooth(0.96, 1.0, t) * 0.4)
    def col(t, a, p):
        c = L.body(t, a, p)
        up = math.cos(a)
        if crusher or coral:   # barnacle and coral crusts on the back
            q = noise3((p[0] * 40, p[1] * 40, p[2] * 40), 7)
            if up > 0.2 and q > 0.62:
                c = mix(c, (0.75, 0.72, 0.62, 0.0) if crusher else (0.85, 0.4, 0.4, 0.0), 0.8)
        if tangle and up < 0.4 and (int(p[2] * 70) % 5 == 0) and abs(math.sin(a)) > 0.6:
            c = (0.35, 0.9, 0.7, 1.0)      # a row of glowing spots
        if t > 0.93 and abs(up + 0.15) < 0.12:
            c = (0.06, 0.02, 0.02, 0.0)    # the jaw line
        return c
    loft(mb, straight_rings(-0.5, hz, 90, prof, rad * 1.1, rad), 20, col)
    # the lower jaw hanging a little open, and teeth along both jaws
    jaw = [(0, -rad * 0.55, lerp(0.38, 0.5, i / 6)) for i in range(7)]
    tube(mb, jaw, [rad * lerp(0.6, 0.35, i / 6) for i in range(7)], 10, mul(L.belly, 0.85) + (0.0,))
    tw = (0.92, 0.9, 0.82, 0.0)
    for i in range(10):
        z = lerp(0.4, 0.49, i / 9)
        for s in (-1, 1):
            w = rad * lerp(0.75, 0.3, i / 9) * s
            cone(mb, (w, -rad * 0.2, z), (w, -rad * 0.55, z + 0.002), rad * 0.07, tw, 4)
            cone(mb, (w * 0.8, -rad * 0.45, z), (w * 0.8, -rad * 0.15, z + 0.002), rad * 0.06, tw, 4)
    # eyes: two pairs on the crusher, one great pair on the serpents
    pairs = 2 if crusher or coral else 1
    for k in range(pairs):
        for s in (-1, 1):
            eye(mb, (s * rad * 0.8, rad * 0.45 - k * rad * 0.25, 0.43 - k * 0.025), rad * 0.16, s, (0.9, 0.55, 0.15), glow=tangle)
    # the crusher's battering crest / the coral-crusher's grinding plates
    if crusher:
        for i in range(5):
            z = 0.42 + i * 0.016
            ellipsoid(mb, (0, rad * (1.05 + 0.05 * i), z), (rad * 0.6, rad * 0.25, 0.014), mix(L.top, (0.6, 0.58, 0.5), 0.5) + (0.0,), 10, 5)
    if coral:
        for i in range(6):
            for s in (-1, 1):
                ellipsoid(mb, (s * rad * 0.7, rad * 0.2, 0.44 + i * 0.01), (rad * 0.25, rad * 0.4, 0.008), (0.7, 0.66, 0.6, 0.0), 6, 4)
    # scute plates down the back
    plates = 26 if not tangle else 40
    for i in range(plates):
        z = lerp(-0.4, 0.38, i / (plates - 1))
        t = (z + 0.5) / 1.0
        r = rad * prof(t)
        root = [(-r * 0.35, r * 0.92, z + 0.012), (0, r * 1.0, z + 0.014), (r * 0.35, r * 0.92, z + 0.012)]
        tip = [(-r * 0.2, r * (1.25 if not tangle else 1.1), z - 0.012), (0, r * (1.45 if not tangle else 1.2), z - 0.016), (r * 0.2, r * (1.25 if not tangle else 1.1), z - 0.012)]
        membrane(mb, root, tip, 1, mix(L.top, L.accent, 0.3) + (0.0,), 0.0, False)
    # fins
    fc = L.finc(0.85 if shroud else 0.5)
    for k, z in enumerate((0.3, 0.05, -0.2)):
        for s in (-1, 1):
            r = rad * prof((z + 0.5))
            size = rad * (2.6 if not shroud else 4.5) * (1 - k * 0.2)
            membrane(mb, [(s * r * 0.9, -r * 0.3, z + 0.03), (s * r * 0.9, -r * 0.3, z - 0.02)],
                     [(s * (r + size), -r * 1.1, z - 0.06), (s * (r + size * 0.6), -r * 0.8, z - 0.09)], 4, fc, 0.6, True, rad * 0.05)
    if shroud:     # the veil along the whole body
        k = 30
        root = [(0, -rad * 0.6, lerp(0.35, -0.48, i / (k - 1))) for i in range(k)]
        membrane(mb, root, [(0, r[1] - rad * 3.2, r[2] - 0.01) for r in root], 3, fc, 0.0, True, rad * 0.2)
    # the fluke
    membrane(mb, [(0, rad * 0.3, -0.48), (0, 0, -0.5), (0, -rad * 0.3, -0.48)], [(rad * 3, 0, -0.58), (0, 0, -0.53), (-rad * 3, 0, -0.58)], 3, fc, 0.0)
    if tangle:     # the barbels: long tendrils from the jaw that trail and tangle
        for i in range(6):
            s = -1 if i % 2 else 1
            pts = []
            for k in range(8):
                h = k / 7
                pts.append((s * (rad * 0.5 + h * rad * 1.2) + math.sin(h * 5 + i) * rad * 0.4, -rad * 0.6 - h * rad * 2.5, 0.47 - i * 0.008 - h * 0.12))
            tube(mb, pts, [rad * 0.12 * (1 - h * 0.85) for h in [k / 7 for k in range(8)]], 4, L.accent + (0.4,), lambda t, a_, p: t)


def turtle(mb, sp, L):
    def col(t, a, p):
        c = L.body(t, a, p)
        q = abs(math.sin(p[2] * 16 + 0.5)) * abs(math.sin(p[0] * 16))
        return mix(c, mul(c[:3], 0.45) + (0.0,), 1.0 if q < 0.08 else 0.0)
    ellipsoid(mb, (0, 0.04, -0.02), (0.36, 0.13, 0.42), None, 20, 10, 0.0, lambda p: col(0.5, 0, p) if p[1] > 0.02 else L.belly + (0.0,))
    skin = mix(L.top, L.belly, 0.4) + (0.0,)
    tube(mb, [(0, 0.03, 0.36), (0, 0.04, 0.44), (0, 0.05, 0.5)], [0.065, 0.06, 0.04], 8, skin)
    for s in (-1, 1):
        eye(mb, (s * 0.04, 0.07, 0.47), 0.012, s)
        membrane(mb, [(s * 0.3, 0.02, 0.24), (s * 0.3, 0.02, 0.14)], [(s * 0.85, -0.02, 0.0), (s * 0.55, 0.0, -0.06)], 4, skin[:3] + (0.0,), 1.0, False)
        membrane(mb, [(s * 0.25, 0.01, -0.3), (s * 0.22, 0.01, -0.36)], [(s * 0.42, 0.0, -0.46), (s * 0.3, 0.0, -0.48)], 2, skin[:3] + (0.0,), 0.5, False)


def mammal(mb, sp, L):
    n = L.n
    orca = "orca" in n
    seal = any(k in n for k in ("seal", "lion", "otter"))
    if orca:
        L.pattern = "orca"
    fat = not (orca or seal)
    def prof(t):
        if t < 0.12:
            return lerp(0.12, 0.25, t / 0.12)
        x = (t - 0.12) / 0.88
        return max(0.05, math.sin(math.pi * lerp(0.15, 0.96, x)) ** (0.45 if fat else 0.6))
    r = 0.2 if fat else 0.12
    loft(mb, straight_rings(-0.42, 0.5, 24, prof, r, r * 0.92), 16, L.body)
    for s in (-1, 1):
        eye(mb, (s * r * 0.55, r * 0.3, 0.42), 0.015, s, (0.2, 0.15, 0.1))
    fc = L.body(0.5, 0, (0, 0, 0))[:3] + (0.0,)
    membrane(mb, [(0.02, 0, -0.42), (0, 0, -0.43), (-0.02, 0, -0.42)], [(0.24 if fat else 0.2, -0.01, -0.58), (0, 0, -0.52), (-0.24 if fat else -0.2, -0.01, -0.58)], 3, fc)
    for s in (-1, 1):
        membrane(mb, [(s * r * 0.85, -r * 0.4, 0.25), (s * r * 0.85, -r * 0.4, 0.17)], [(s * (r + 0.22), -r * 0.9, 0.1), (s * (r + 0.1), -r * 0.6, 0.08)], 3, fc, 0.6, False)
    if orca:
        membrane(mb, [(0, r * 0.9, 0.08), (0, r * 0.92, 0.0), (0, r * 0.88, -0.06)], [(0, r * 0.95, 0.06), (0, r * 2.6, -0.02), (0, r * 0.95, -0.05)], 3, (0.03, 0.03, 0.035, 0.0))
    if seal:
        for k in range(6):
            tube(mb, [(-0.02 * (k - 2.5), -0.01, 0.49), (-0.06 * (k - 2.5), -0.02, 0.56)], [0.002, 0.001], 3, (0.85, 0.85, 0.8, 0.0))


def squid(mb, sp, L):
    n = L.n
    cuttle = "cuttle" in n
    rad = 0.12 if not cuttle else 0.15
    def prof(t):
        return max(0.08, math.sin(math.pi * lerp(0.3 if not cuttle else 0.15, 1.0, t)) ** 0.5)
    loft(mb, straight_rings(-0.1, 0.5, 20, prof, rad, rad * (0.75 if cuttle else 1.0)), 14, L.body, lambda t, a, p: 1.0 - t * 0.6)
    fc = L.finc(0.7)
    if cuttle:   # the undulating fin skirt
        k = 12
        for s in (-1, 1):
            root = [(s * rad * 0.95, -0.01, lerp(0.45, -0.05, i / (k - 1))) for i in range(k)]
            membrane(mb, root, [(r[0] + s * 0.07, r[1] - 0.01, r[2]) for r in root], 2, fc, 0.6, True, 0.012)
    else:
        for s in (-1, 1):
            membrane(mb, [(s * 0.03, 0, 0.45), (s * 0.06, 0, 0.32)], [(s * 0.2, 0, 0.36), (s * 0.12, 0, 0.24)], 2, fc, 0.6)
    for s in (-1, 1):
        eye(mb, (s * rad * 0.85, 0.02, -0.08), rad * 0.3, s, (0.8, 0.75, 0.4))
    for i in range(8):
        a = i / 8 * math.pi * 2
        d = (math.cos(a) * rad * 0.5, math.sin(a) * rad * 0.5, 0)
        pts = [add(d, (0, 0, -0.1 - k * 0.07)) for k in range(6)]
        pts = [add(p, mul(norm((d[0], d[1], 0.0001)), k * 0.006)) for k, p in enumerate(pts)]
        tube(mb, pts, [0.025, 0.02, 0.016, 0.011, 0.007, 0.003], 5, L.body(0.2, a, pts[0]), lambda t, a_, p: t * 0.6)
    for s in (-1, 1):
        tube(mb, [(s * 0.02, 0, -0.1), (s * 0.03, -0.01, -0.35), (s * 0.03, -0.02, -0.6), (s * 0.04, -0.02, -0.68)], [0.012, 0.008, 0.008, 0.016], 4, L.accent + (0.0,), lambda t, a_, p: t * 0.6)


def octopus(mb, sp, L):
    ellipsoid(mb, (0, 0.24, -0.06), (0.17, 0.17, 0.22), None, 14, 8, lambda p: 1.0 if p[1] > 0.3 else 0.4, lambda p: L.body(0.5, 0.0 if p[1] > 0.24 else 3.14, p))
    for s in (-1, 1):
        eye(mb, (s * 0.12, 0.2, 0.08), 0.03, s, (0.85, 0.7, 0.3))
    for i in range(8):
        a = i / 8 * math.pi * 2 + 0.2
        d = (math.cos(a), 0, math.sin(a))
        pts = []
        for k in range(8):
            h = k / 7
            curl = math.sin(h * 3 + i) * 0.06
            pts.append((d[0] * (0.08 + h * 0.45) - d[2] * curl, 0.1 - h * 0.09 + (0.04 * h * h if i % 3 == 0 else 0), d[2] * (0.08 + h * 0.45) + d[0] * curl))
        tube(mb, pts, [0.045 * (1 - h * 0.88) for h in [k / 7 for k in range(8)]], 6, L.body(0.4, a, pts[0]), lambda t, a_, p: t)


def star(mb, sp, L):
    n = L.n
    brittle = "brittle" in n
    basket = "basket" in n
    arms = 5
    ellipsoid(mb, (0, 0.03, 0), (0.12 if not brittle else 0.08, 0.04, 0.12 if not brittle else 0.08), None, 12, 6, 0.0, lambda p: L.body(0.5, 0.0, p))
    for i in range(arms):
        a = i / arms * math.pi * 2
        d = (math.cos(a), 0, math.sin(a))
        reach = 0.45 if not brittle else 0.5
        pts = [(d[0] * reach * h + (math.sin(h * 6 + i) * 0.04 * d[2] if brittle else 0), 0.03 - h * 0.02 + (0.06 * h if basket else 0), d[2] * reach * h - (math.sin(h * 6 + i) * 0.04 * d[0] if brittle else 0)) for h in [k / 6 for k in range(7)]]
        w = 0.07 if not brittle else 0.018
        tube(mb, pts, [w * (1 - h * 0.85) for h in [k / 6 for k in range(7)]], 6, L.body(0.5, 0.0, pts[0]), 0.0,
             colfn=lambda t, a_, p: L.body(t, 0.0, p) if (t * 9) % 1 > 0.25 else mix(L.body(t, 0, p), L.accent + (0.0,), 0.6))
        if basket:
            for k in (2, 4):
                for s in (-1, 1):
                    q = pts[k]
                    tube(mb, [q, add(q, (d[2] * s * 0.12, 0.08, -d[0] * s * 0.12))], [0.008, 0.003], 4, L.accent + (0.0,))


def crab(mb, sp, L):
    n = L.n
    isopod = "isopod" in n
    hermit = "hermit" in n
    if isopod:   # a segmented oval with many short legs
        def prof(t): return max(0.1, math.sin(math.pi * t) ** 0.5)
        loft(mb, straight_rings(-0.45, 0.45, 20, prof, 0.28, 0.12, y=0.08, shape=lambda a: 1.0 if math.cos(a) > -0.2 else 0.3),
             12, lambda t, a, p: mix(L.body(t, a, p), mul(L.top, 0.5) + (0.0,), 1.0 if (t * 9) % 1 < 0.12 else 0.0))
        for k in range(7):
            z = lerp(-0.3, 0.3, k / 6)
            for s in (-1, 1):
                tube(mb, [(s * 0.2, 0.05, z), (s * 0.34, 0.06, z), (s * 0.4, 0.0, z - 0.03)], [0.018, 0.012, 0.006], 4, L.top + (0.0,), 1.0)
        for s in (-1, 1):
            tube(mb, [(s * 0.05, 0.09, 0.42), (s * 0.15, 0.12, 0.6)], [0.01, 0.004], 4, L.top + (0.0,))
        return
    if hermit:   # a borrowed spiral shell on its back
        shell(mb, sp, L, (0, 0.18, -0.12), 0.32)
    else:
        ellipsoid(mb, (0, 0.14, 0), (0.33, 0.11, 0.26), None, 16, 8, 0.0, lambda p: L.body(0.5, 0.0 if p[1] > 0.12 else 3.1, p))
    leg = L.top
    for k in range(4):
        z = 0.12 - k * 0.1
        for s in (-1, 1):
            pts = [(s * 0.28, 0.12, z), (s * 0.48, 0.2, z - 0.02), (s * 0.62, 0.02, z - 0.05), (s * 0.64, -0.03, z - 0.06)]
            tube(mb, pts, [0.03, 0.024, 0.014, 0.004], 5, leg + (0.0,), 1.0)
    for s in (-1, 1):   # the claws
        tube(mb, [(s * 0.2, 0.13, 0.2), (s * 0.32, 0.17, 0.32), (s * 0.28, 0.15, 0.45)], [0.035, 0.04, 0.04], 6, L.accent + (0.0,), 0.5)
        ellipsoid(mb, (s * 0.27, 0.15, 0.5), (0.06, 0.04, 0.09), L.accent + (0.0,), 8, 5, 0.5)
        cone(mb, (s * 0.25, 0.13, 0.55), (s * 0.24, 0.12, 0.66), 0.015, mul(L.accent, 0.6) + (0.0,), 5, 0.5)
        tube(mb, [(s * 0.06, 0.2, 0.22), (s * 0.08, 0.27, 0.26)], [0.012, 0.008], 4, leg + (0.0,))
        ellipsoid(mb, (s * 0.08, 0.28, 0.27), (0.018, 0.018, 0.018), (0.02, 0.02, 0.02, 0.0), 6, 4)


def shrimp(mb, sp, L):
    n = L.n
    mantis = "mantis" in n
    def prof(t):
        return max(0.08, math.sin(math.pi * lerp(0.1, 0.95, t)) ** 0.5)
    rings = []
    for i in range(25):
        t = i / 24
        # the abdomen curls down at the back
        ang = (1 - t) * (0.9 if not mantis else 0.25)
        z = lerp(-0.45, 0.45, t)
        y = 0.12 - math.sin(ang) * 0.12
        r = 0.09 * prof(t) * (1.2 if mantis else 1)
        rings.append(((0, y, z), (0, -math.cos(ang) * 0 - ang * 0.3, 1), r * 0.85, r, None))
    loft(mb, rings, 10, lambda t, a, p: mix(L.body(t, a, p), mul(L.top, 0.55) + (0.0,), 1.0 if (t * 8) % 1 < 0.1 else 0.0))
    fc = L.finc(0.7)
    membrane(mb, [(-0.04, 0.0, -0.45), (0, 0.0, -0.46), (0.04, 0.0, -0.45)], [(-0.12, -0.06, -0.58), (0, -0.07, -0.6), (0.12, -0.06, -0.58)], 2, fc)
    for s in (-1, 1):
        tube(mb, [(s * 0.02, 0.16, 0.45), (s * 0.12, 0.25, 0.75), (s * 0.2, 0.2, 1.05)], [0.006, 0.003, 0.001], 3, L.accent + (0.3,))
        ellipsoid(mb, (s * 0.04, 0.2, 0.42), (0.02, 0.02, 0.02), (0.03, 0.03, 0.03, 0.0), 6, 4)
        for k in range(5):
            z = 0.25 - k * 0.07
            tube(mb, [(s * 0.05, 0.08, z), (s * 0.12, 0.0, z), (s * 0.14, -0.05, z - 0.02)], [0.008, 0.005, 0.002], 3, L.top + (0.3,), 1.0)
        if mantis:   # the raptorial clubs folded under the head
            tube(mb, [(s * 0.05, 0.08, 0.38), (s * 0.08, 0.02, 0.48), (s * 0.06, 0.05, 0.56)], [0.025, 0.03, 0.035], 6, L.accent + (0.0,), 0.5)


def jelly(mb, sp, L):
    col = L.accent + (0.85,)
    rings = []
    for i in range(12):
        t = i / 11
        r = 0.42 * math.sin(math.pi * 0.5 * (1 - t)) ** 0.6 + 0.001
        rings.append(((0, 0.0, lerp(0.0, 0.32, t)), (0, 0, 1), r, r, None))
    loft(mb, rings, 18, lambda t, a, p: mix(col, (0.95, 0.95, 1.0, 0.9), 0.3 * (1 - t)), lambda t, a, p: 1.0 - t, cap0=False)
    for i in range(14):
        a = i / 14 * math.pi * 2
        d = (math.cos(a) * 0.38, math.sin(a) * 0.38, 0.0)
        pts = [add(d, (0, 0, -k * 0.12)) for k in range(8)]
        pts = [add(p, (math.sin(k * 0.9 + i) * 0.03, math.cos(k * 0.7 + i) * 0.03, 0)) for k, p in enumerate(pts)]
        tube(mb, pts, [0.008 * (1 - k / 9) + 0.001 for k in range(8)], 3, col, lambda t, a_, p: 0.3)
    for i in range(4):   # the frilled oral arms
        a = i / 4 * math.pi * 2
        root = [(math.cos(a) * 0.04, math.sin(a) * 0.04, -k * 0.08) for k in range(6)]
        tip = [add(r, (math.cos(a) * 0.08, math.sin(a) * 0.08, 0)) for r in root]
        membrane(mb, root, tip, 2, mix(col, (1, 1, 1, 0.9), 0.4), 0.2, False, 0.03)


def urchin(mb, sp, L):
    n = L.n
    if "dollar" in n:   # a sand dollar: a flat disc with a five-petal pattern
        ellipsoid(mb, (0, 0.02, 0), (0.45, 0.03, 0.45), None, 24, 6, 0.0,
                  lambda p: mix(L.body(0.5, 0, p), (0.95, 0.92, 0.85, 0.0), 0.8 if abs(math.sin(math.atan2(p[2], p[0]) * 5 / 2)) < 0.15 and 0.08 < math.hypot(p[0], p[2]) < 0.3 and p[1] > 0.02 else 0.0))
        return
    ellipsoid(mb, (0, 0.18, 0), (0.22, 0.16, 0.22), None, 14, 8, 0.0, lambda p: L.body(0.5, 0.0, p))
    stalk = "stalk" in n or "pencil" in n
    for i in range(70):
        a, b = H(n + str(i), 1) * math.pi * 2, H(n + str(i), 2) * math.pi * 0.55
        d = (math.cos(b) * math.cos(a), math.sin(b), math.cos(b) * math.sin(a))
        base = (d[0] * 0.2, 0.18 + d[1] * 0.15, d[2] * 0.2)
        cone(mb, base, add(base, mul(d, 0.22 if not stalk else 0.12)), 0.01 if not stalk else 0.025, mul(L.top, 0.55 + 0.3 * H(n, i)) + (0.0,), 4)


def shell(mb, sp, L, at=(0, 0, 0), size=1.0):
    """A spiral shell (a conch, a whelk, a cone): a tapering tube wound round its axis."""
    n = L.n
    pts, radii = [], []
    turns = 3.2 if "cone" not in n else 1.6
    for k in range(40):
        h = k / 39
        a = h * turns * math.pi * 2
        r = (0.28 * (1 - h) + 0.02) * size
        pts.append((at[0] + math.cos(a) * r * 0.6, at[1] + (0.05 + h * 0.5) * size * (0.8 if "cone" not in n else 0.4), at[2] + math.sin(a) * r * 0.6 - h * 0.15 * size))
        radii.append(r * 0.7)
    tube(mb, pts, radii, 10, L.top + (0.0,), 0.0,
         colfn=lambda t, a_, p: mix(L.body(t, 0, p), L.accent + (0.0,), 0.7 if (t * 14 + a_ * 0.4) % 1 < 0.3 else 0.0))


def snail(mb, sp, L):
    n = L.n
    if any(k in n for k in ("barnacle",)):
        for i in range(14):
            a, r = H(n + str(i), 1) * math.pi * 2, H(n + str(i), 2) * 0.35
            c = (math.cos(a) * r, 0, math.sin(a) * r)
            h = 0.12 + 0.12 * H(n + str(i), 3)
            rings = [((c[0], 0.0, c[2]), (0, 1, 0), 0.09, 0.09, None), ((c[0], h * 0.7, c[2]), (0, 1, 0), 0.07, 0.07, None), ((c[0], h, c[2]), (0, 1, 0), 0.04, 0.04, None)]
            loft(mb, rings, 8, lambda t, a_, p: mix(L.belly + (0.0,), (0.2, 0.15, 0.12, 0.0), 1.0 if t > 0.9 else 0.0), cap1=True)
        return
    if any(k in n for k in ("clam", "mussel", "scallop")):
        scallop = "scallop" in n
        for s in (-1, 1):   # two valves, a little agape
            ellipsoid(mb, (0, 0.08 + s * 0.04, 0), (0.42 if not scallop else 0.4, 0.06, 0.32 if not scallop else 0.38), None, 18, 6, 0.0,
                      lambda p: mix(L.body(0.5, 0, p), mul(L.top, 0.6) + (0.0,), 1.0 if scallop and abs(math.sin(math.atan2(p[2], p[0]) * 9)) < 0.2 else 0.0))
        ellipsoid(mb, (0, 0.08, 0.05), (0.35, 0.02, 0.25), L.accent + (0.3,), 12, 4)
        return
    if "tunicate" in n:
        tube(mb, [(0, 0, 0), (0, 0.3, 0), (0, 0.55, 0)], [0.16, 0.18, 0.12], 12, L.body(0.5, 0, (0, 0, 0))[:3] + (0.6,))
        for s in (-1, 1):
            tube(mb, [(s * 0.05, 0.55, 0), (s * 0.12, 0.7, 0)], [0.05, 0.04], 8, L.accent + (0.6,))
        return
    slug = any(k in n for k in ("nudibranch", "sea-hare", "sea hare", "cow", "slug"))
    chiton = "chiton" in n
    # the foot
    def prof(t): return max(0.15, math.sin(math.pi * t) ** 0.6)
    loft(mb, straight_rings(-0.45, 0.45, 18, prof, 0.18 if not slug else 0.2, 0.08 if not slug else 0.14, y=0.06), 12, L.body)
    if slug:
        for s in (-1, 1):
            tube(mb, [(s * 0.06, 0.16, 0.38), (s * 0.08, 0.28, 0.42)], [0.02, 0.008], 5, L.accent + (0.0,))
        for i in range(20 if "nudibranch" in n else 6):   # cerata or the sea hare's parapodia
            a, z = H(n + str(i), 4) * math.pi, lerp(-0.3, 0.25, H(n + str(i), 5))
            base = (math.cos(a) * 0.12, 0.14 + math.sin(a) * 0.08, z)
            cone(mb, base, add(base, (math.cos(a) * 0.08, 0.12, -0.02)), 0.025, L.accent + (0.2,), 5)
        return
    if chiton:
        for i in range(8):
            z = lerp(-0.35, 0.35, i / 7)
            ellipsoid(mb, (0, 0.11, z), (0.16, 0.05, 0.05), mix(L.top, L.accent, 0.3 * (i % 2)) + (0.0,), 10, 4)
        return
    shell(mb, sp, L, (0, 0.06, -0.05), 1.0)
    for s in (-1, 1):
        tube(mb, [(s * 0.05, 0.1, 0.4), (s * 0.06, 0.2, 0.46)], [0.012, 0.005], 4, L.belly + (0.0,))


def bat(mb, sp, L):
    """A cave bat at rest in flight pose: furred body, a blunt head with big ears, two leathery wings spread on their
    finger bones, the little feet tucked back. +z forward, wingspan about 1 unit."""
    fur = L.top + (0.0,)
    ellipsoid(mb, (0, 0, -0.02), (0.09, 0.08, 0.16), fur, 10, 6)
    ellipsoid(mb, (0, 0.03, 0.15), (0.07, 0.065, 0.07), fur, 10, 6)
    for s in (-1, 1):
        cone(mb, (s * 0.04, 0.08, 0.15), (s * 0.07, 0.19, 0.12), 0.03, L.belly + (0.0,), 5)
        ellipsoid(mb, (s * 0.03, 0.05, 0.21), (0.012, 0.012, 0.012), (0.02, 0.02, 0.02, 0.0), 5, 3)
        root = [(s * 0.06, 0.02, z) for z in (0.08, 0.0, -0.08, -0.14)]
        tip = [(s * 0.5, 0.06, 0.06), (s * 0.48, 0.02, -0.06), (s * 0.36, 0.0, -0.16), (s * 0.18, 0.0, -0.2)]
        membrane(mb, root, tip, 4, (L.fin[0] * 0.7, L.fin[1] * 0.7, L.fin[2] * 0.7, 0.35), lambda t, i: t, True, 0.02)
        for e in tip[:3]:
            tube(mb, [(s * 0.06, 0.03, 0.06), e], [0.008, 0.004], 4, (0.15, 0.12, 0.1, 0.0))
        tube(mb, [(s * 0.03, -0.04, -0.14), (s * 0.04, -0.06, -0.22)], [0.01, 0.006], 4, (0.15, 0.12, 0.1, 0.0))


def build(sp, biome):
    L = Look(sp, biome)
    mb = MB()
    kind = sp.get("kind", "fish")
    n = L.n
    shellname = any(k in n for k in ("barnacle", "mussel", "clam", "scallop", "tunicate"))
    if shellname:
        kind = "snail"
    elif ("star" in n or "brittle" in n) and kind in ("other", "octopus", "fish"):
        kind = "star"
    elif "cucumber" in n:
        kind = "worm"
    elif kind == "other":
        kind = "fish"
    if "krait" in n:
        eel(mb, sp, L, krait=True)
    elif "lamprey" in n:
        eel(mb, sp, L, lamprey=True)
    elif kind == "fish":
        fish(mb, sp, L)
    elif kind == "shark":
        shark(mb, sp, L)
    elif kind == "ray":
        ray(mb, sp, L)
    elif kind == "eel":
        eel(mb, sp, L)
    elif kind == "worm":
        worm(mb, sp, L)
    elif kind == "leviathan":
        leviathan(mb, sp, L)
    elif kind == "turtle":
        turtle(mb, sp, L)
    elif kind == "mammal":
        mammal(mb, sp, L)
    elif kind == "squid":
        squid(mb, sp, L)
    elif kind == "octopus":
        octopus(mb, sp, L)
    elif kind == "star":
        star(mb, sp, L)
    elif kind == "crab":
        crab(mb, sp, L)
    elif kind == "shrimp":
        shrimp(mb, sp, L)
    elif kind == "jelly":
        jelly(mb, sp, L)
    elif kind == "urchin":
        urchin(mb, sp, L)
    elif kind == "snail":
        snail(mb, sp, L)
    elif kind == "bat":
        bat(mb, sp, L)
    else:
        fish(mb, sp, L)
    return mb


# ------------------------------------------------------------------------------------------------ to Blender and out
def Gc(p):
    """Unity frame (x right, y up, z forward) -> Blender, so the glTF reaches Unity (through glTFast's mirror) as built."""
    return (-p[0], -p[2], p[1])


def to_object(name, mb):
    me = bpy.data.meshes.new(name)
    me.from_pydata([Gc(p) for p in mb.v], [], [list(f) for f in mb.f])
    me.validate()
    bm = bmesh.new(); bm.from_mesh(me)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me); bm.free()
    for poly in me.polygons:
        poly.use_smooth = True
    ca = me.color_attributes.new("Col", 'FLOAT_COLOR', 'POINT')
    for i, c in enumerate(mb.c):   # (the palettes are written as seen; vertex colours are linear)
        ca.data[i].color = (c[0] ** 2.2, c[1] ** 2.2, c[2] ** 2.2, c[3])
    me.color_attributes.active_color = ca
    uv = me.uv_layers.new(name="swim")
    for loop in me.loops:
        u, v = mb.uv[loop.vertex_index]
        uv.data[loop.index].uv = (u, v)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def main():
    a = args()
    out = a[a.index("--out") + 1] if "--out" in a else os.path.join(ROOT, "TheDeep", "Assets", "Deep", "Resources", "Creatures")
    only = a[a.index("--only") + 1].lower() if "--only" in a else None
    os.makedirs(out, exist_ok=True)
    biomes = ("shallows", "kelp", "caverns", "vents")
    if "--biome" in a:
        biomes = (a[a.index("--biome") + 1],)
    for biome in biomes:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        with open(os.path.join(DATA, f"species_{biome}.json"), encoding="utf-8") as f:
            tab = json.load(f)
        objs, tris = [], 0
        for sp in tab["fauna"]:
            if sp.get("role") == "Plankton":
                continue
            if only and only not in sp["name"].lower():
                continue
            mb = build(sp, biome)
            ob = to_object(sp["id"], mb)
            objs.append(ob)
            t = sum(len(f) - 2 for f in mb.f)
            tris += t
        for o in objs:
            o.select_set(True)
        path = os.path.join(out, f"creatures_{biome}.glb")
        bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=True, export_yup=True,
                                  export_texcoords=True, export_normals=True, export_tangents=False, export_materials='NONE',
                                  export_vertex_color='ACTIVE', export_all_vertex_colors=False)
        print(f"artgen: wrote {path}: {len(objs)} species, {tris} triangles, {os.path.getsize(path) // 1024} KB")


if __name__ == "__main__":
    main()
