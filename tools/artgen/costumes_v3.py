# Costumes, third pass (the user, 2026-10-06: "they look better than before, but they are still geometric blocks
# instead of looking like what they are meant to be"). The outfits of costumes_v2.py keep their cut (armour, coats,
# capes, a weapon), and now every one is shaped like its creature: helmets lofted into real heads (a shark's snout and
# jaw, a lobster's rostrum and stalked eyes, a gull's beak, an orca's patches, a turtle's beak, a swordfish's bill),
# fins with a real outline (dorsal, pectoral, a tail's flukes, a sailfish's sail), tapered tentacles with rows of
# suckers, a lobster's segmented tail plates and fan, a hermit's spiral shell, branching coral, barnacle cones with
# mouths, kelp as wavy blades. And no blocks: every low-poly piece is subdivided and smoothed as it's made.
#     blender -b --factory-startup -P tools/artgen/costumes_v3.py -- --out assets/shared/costumes [--only lobster,shark]
# Frame: x forward, y to the left, z up (crew.py's joints); every piece rides one bone, the base layer is soft.
import os
import sys
import math
import bmesh
from mathutils import Vector, Matrix
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
os.environ["ARTGEN_IMPORT"] = "1"
os.environ["COSTUMES_V2_IMPORT"] = "1"
import common as C
import costumes as CO
import costumes_v2 as V2
from costumes_v2 import (V, HC, SIDES, base, gloves, boots, arm_armour, pauldrons, cuirass, belt, leg_armour, tassets, hood,
                         mask_face, cape, coat_skirt, coat_body, trousers, glow_seams, held, spear, blade, lantern, ring, two_sided, has_head, helm)
bpy = C.bpy
COL = CO.COL


# ---------------------------------------------------------------- smoothing: no blocks
_add0 = CO.Kit.add
def _add_smooth(self, o, mat, bone):
    """Every low-poly piece (a box, a cone, a tube) is subdivided and smoothed as it's made; the dense ones (spheres,
    lathes, the soft suit) only get smooth shading."""
    if o.type == 'MESH' and o.name != "suit":
        mods = [m.name for m in o.modifiers]
        nf = len(o.data.polygons)
        if not mods and nf < 160 and not getattr(self, "crisp", False):
            s = o.modifiers.new("smooth", 'SUBSURF'); s.levels = 1; mods.append("smooth")
        if mods:
            C.select_only([o])
            for m in mods: bpy.ops.object.modifier_apply(modifier=m)
        # (dense pieces are thinned: the public repo keeps its assets small)
        nf = len(o.data.polygons)
        if nf > 400:
            d = o.modifiers.new("thin", 'DECIMATE'); d.ratio = max(0.3, 400.0 / nf) if nf > 900 else 0.6
            C.select_only([o]); bpy.ops.object.modifier_apply(modifier="thin")
    return _add0(self, o, mat, bone)
CO.Kit.add = _add_smooth
_ell0 = CO.Kit.ell
def _ell_lean(self, name, c, r, mat, bone, segs=24):
    """Small spheres need few segments (they're smoothed anyway): eyes, bumps, studs, teeth."""
    m = max(r); segs = min(segs, 8 if m < 0.03 else 12 if m < 0.07 else 18 if m < 0.15 else segs)
    self.crisp = True; o = _ell0(self, name, c, r, mat, bone, segs); self.crisp = False   # (a sphere is round already)
    return o
CO.Kit.ell = _ell_lean
_lathe0 = CO.Kit.lathe
def _lathe_crisp(self, *a, **kw):
    self.crisp = True; o = _lathe0(self, *a, **kw); self.crisp = False; return o
CO.Kit.lathe = _lathe_crisp


def _obj(k, name, bm):
    me = bpy.data.meshes.new(name); bm.normal_update(); bm.to_mesh(me); bm.free()
    return C.link(bpy.data.objects.new(name, me))


# ---------------------------------------------------------------- the creature kit
def frame_of(d):
    d = Vector(d).normalized(); up = Vector((0, 0, 1)) if abs(d.z) < 0.9 else Vector((1, 0, 0))
    s1 = d.cross(up).normalized(); s2 = s1.cross(d)
    return s1, s2


def taper(k, name, pts, radii, mat, bone, segs=10, squash=1.0):
    """A tube through pts whose radius runs through radii (tentacles, horns, antennae, branches), capped at both ends."""
    pts = [Vector(p) for p in pts]; bm = bmesh.new(); rings = []
    for i, p in enumerate(pts):
        d = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)])
        s1, s2 = frame_of(d)
        r = radii[i] if i < len(radii) else radii[-1]
        rings.append([bm.verts.new(p + s1 * math.cos(2 * math.pi * j / segs) * r + s2 * math.sin(2 * math.pi * j / segs) * r * squash) for j in range(segs)])
    for a, b in zip(rings, rings[1:]):
        for j in range(segs):
            bm.faces.new((a[j], a[(j + 1) % segs], b[(j + 1) % segs], b[j]))
    bm.faces.new(list(reversed(rings[0]))); bm.faces.new(rings[-1])
    k.crisp = True; o = k.add(_obj(k, name, bm), mat, bone); k.crisp = False
    return o


def curve_pts(p0, p1, bend, n=8, twist=(0, 0, 0)):
    """n points from p0 to p1 bowed by `bend` (a vector added along a sine)."""
    p0, p1, bend, tw = Vector(p0), Vector(p1), Vector(bend), Vector(twist)
    return [p0.lerp(p1, t) + bend * math.sin(math.pi * t) + tw * (t * t) for t in [i / (n - 1) for i in range(n)]]


def loft(k, name, secs, mat, bone, segs=20, e=0.85, at=None, crisp=False):
    """A solid lofted through sections [(x, z, half_width, half_height), ...] along x (heads, snouts, beaks, bodies),
    capped; `at` offsets it (default the head's centre). Superelliptic rings (e < 1 squarer, 1 an ellipse)."""
    at = Vector(at) if at is not None else HC
    bm = bmesh.new(); rings = []
    for (x, z, hy, hz) in secs:
        ring = []
        for j in range(segs):
            a = 2 * math.pi * j / segs; c, s = math.cos(a), math.sin(a)
            y = hy * math.copysign(abs(c) ** e, c); zz = z + hz * math.copysign(abs(s) ** e, s)
            ring.append(bm.verts.new(at + Vector((x, y, zz))))
        rings.append(ring)
    for a, b in zip(rings, rings[1:]):
        for j in range(segs):
            bm.faces.new((a[j], a[(j + 1) % segs], b[(j + 1) % segs], b[j]))
    bm.faces.new(list(reversed(rings[0]))); bm.faces.new(rings[-1])
    o = _obj(k, name, bm)
    if not crisp: s = o.modifiers.new("smooth", 'SUBSURF'); s.levels = 1
    return k.add(o, mat, bone)


def fin(k, name, outline, origin, u, v, thick, mat, bone, bulge=0.6):
    """A fin: a flat outline [(a, b), ...] laid in the plane origin + a*u + b*v, thick in the middle, thin at the rim
    (a lens section), then smoothed."""
    origin, u, v = Vector(origin), Vector(u), Vector(v); n = u.cross(v).normalized()
    cx = sum(p[0] for p in outline) / len(outline); cy = sum(p[1] for p in outline) / len(outline)
    bm = bmesh.new()
    rim = [bm.verts.new(origin + u * a + v * b) for (a, b) in outline]
    inner = [(cx + (a - cx) * 0.55, cy + (b - cy) * 0.55) for (a, b) in outline]
    top = [bm.verts.new(origin + u * a + v * b + n * thick * 0.5) for (a, b) in inner]
    bot = [bm.verts.new(origin + u * a + v * b - n * thick * 0.5) for (a, b) in inner]
    N = len(outline)
    for i in range(N):
        j = (i + 1) % N
        bm.faces.new((rim[i], rim[j], top[j], top[i])); bm.faces.new((rim[j], rim[i], bot[i], bot[j]))
    bm.faces.new(top); bm.faces.new(list(reversed(bot)))
    o = _obj(k, name, bm); s = o.modifiers.new("smooth", 'SUBSURF'); s.levels = 1
    return k.add(o, mat, bone)


def suckers(k, name, pts, side_dir, r, mat, bone, every=1):
    """Little discs in a row along a tentacle's underside."""
    for i in range(1, len(pts) - 1, every):
        p = Vector(pts[i]); d = Vector(pts[i + 1]) - Vector(pts[i - 1]); s = Vector(side_dir)
        rr = r * (1 - i / len(pts) * 0.6)
        k.ell(f"{name}{i}", tuple(p + s * rr * 1.2), (rr, rr, rr * 0.5), mat, bone, 8)


def smooth_path(ctrl, per=4):
    """A Catmull-Rom curve through the control points (per samples between each pair): no sharp elbows."""
    P = [Vector(c) for c in ctrl]
    if len(P) < 3: return P
    P = [P[0] * 2 - P[1]] + P + [P[-1] * 2 - P[-2]]; out = []
    for i in range(1, len(P) - 2):
        p0, p1, p2, p3 = P[i - 1], P[i], P[i + 1], P[i + 2]
        for s in range(per):
            t = s / per; t2 = t * t; t3 = t2 * t
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
    out.append(P[-2]); return out


def tentacle(k, name, pts, r0, r1, mat, sucker_mat, bone, under=(0, 0, -1)):
    pts = smooth_path(pts, 4)
    radii = [r0 + (r1 - r0) * (i / (len(pts) - 1)) for i in range(len(pts))]
    taper(k, name, pts, radii, mat, bone, 10)
    suckers(k, name + "s", pts, under, r0 * 0.45, sucker_mat, bone, 2)


def eye_ball(k, name, c, r, mat="white", pupil="black", fwd=(1, 0, 0), bone="head"):
    c = Vector(c); k.ell(name, tuple(c), (r, r, r), mat, bone, 14)
    k.ell(name + "p", tuple(c + Vector(fwd).normalized() * r * 0.75), (r * 0.5, r * 0.5, r * 0.5), pupil, bone, 10)


def teeth(k, name, a, b, n, h, mat, bone, down=True):
    a, b = Vector(a), Vector(b)
    for i in range(n):
        p = a.lerp(b, (i + 0.5) / n); k.cone(f"{name}{i}", p, p + Vector((0.004, 0, -h if down else h)), h * 0.42, 0.0005, mat, bone, 6)


def arc_band(k, name, c, r, z0, z1, a0, a1, thick, mat, bone, segs=14, ry=None, flare=0.0):
    """A curved shell plate: part of a ring about the vertical through c (angle 0 faces forward, pi the back), from
    height z0 to z1, `thick` deep, flaring out `flare` at its lower edge (an overlapping plate)."""
    c = Vector(c); ry = ry or r; bm = bmesh.new(); rows = []
    for zz, fl in ((z1, 0.0), (z0, flare)):
        for rad in (r + fl, r + thick + fl):
            rows.append([bm.verts.new(c + Vector((math.cos(a0 + (a1 - a0) * j / segs) * rad, math.sin(a0 + (a1 - a0) * j / segs) * rad * ry / r, zz - c.z))) for j in range(segs + 1)])
    ti, to, bi, bo = rows
    for j in range(segs):
        bm.faces.new((to[j], to[j + 1], bo[j + 1], bo[j])); bm.faces.new((ti[j + 1], ti[j], bi[j], bi[j + 1]))
        bm.faces.new((ti[j], ti[j + 1], to[j + 1], to[j])); bm.faces.new((bo[j], bo[j + 1], bi[j + 1], bi[j]))
    for e in (0, segs): bm.faces.new((ti[e], to[e], bo[e], bi[e]) if e == 0 else (to[e], ti[e], bi[e], bo[e]))
    o = _obj(k, name, bm); s = o.modifiers.new("smooth", 'SUBSURF'); s.levels = 1
    return k.add(o, mat, bone)


def segmented_tail(k, name, mat, mat2, n=6, z0=1.02, length=0.55, w0=0.205, w1=0.17, back=0.0, fan=True):
    """A crustacean's tail down the back from the waist: overlapping curved plates hugging the body, each narrower
    than the one above and flared at its lower edge, ending in a fan of five flukes behind the knees."""
    for i in range(n):
        u = i / n; z1 = z0 - length * u; z0b = z1 - length / n * 1.3; r = w0 + (w1 - w0) * u; half = 1.15 - 0.35 * u
        arc_band(k, f"{name}{i}", (0.0, 0, 0), r, z0b, z1, math.pi - half, math.pi + half, 0.03, mat if i % 2 == 0 else mat2, "pelvis", 16, None, 0.025)
        for s in (1, -1): k.ell(f"{name}spine{i}{s}", (-(r + 0.03) * math.cos(half * 0.9), s * (r + 0.03) * math.sin(half * 0.9), z1 - 0.03), (0.018, 0.018, 0.022), mat2, "pelvis", 8)
    if fan:
        zf = z0 - length - 0.04; xb = -(w1 + 0.03)
        for j, a in enumerate((-0.75, -0.38, 0, 0.38, 0.75)):
            fin(k, f"{name}fan{j}", [(0, -0.045), (0.18, -0.07), (0.23, 0), (0.18, 0.07), (0, 0.045)], (xb, 0, zf), (-0.25, math.sin(a), -math.cos(a)), (0, math.cos(a), math.sin(a)), 0.02, mat if j % 2 == 0 else mat2, "pelvis")


def big_claw(k, sd, mat, mat2, size=1.0):
    """A crustacean's crusher claw over the hand: a swollen palm and two curved fingers with tooth bumps."""
    s = 1 if sd == "L" else -1
    h = Vector(k.j(f"palm.{sd}")); d = Vector((0, s * 0.62, -0.78)).normalized(); side = Vector((1, 0, 0))
    k.ell(f"cpalm{sd}", tuple(h + d * 0.06 * size), (0.085 * size, 0.07 * size, 0.13 * size), mat, f"hand.{sd}", 18)
    for f, off in ((0, 1), (1, -1)):
        p0 = h + d * 0.16 * size + side * off * 0.035 * size
        pts = curve_pts(p0, p0 + d * 0.2 * size, side * -off * 0.04 * size, 7)
        taper(k, f"cfinger{sd}{f}", pts, [0.045 * size, 0.04 * size, 0.035 * size, 0.028 * size, 0.02 * size, 0.012 * size, 0.003], mat, f"hand.{sd}", 10)
        for t in range(1, 5):
            q = Vector(pts[t]) - side * off * 0.035 * size
            k.ell(f"ctooth{sd}{f}{t}", tuple(q), (0.012 * size, 0.012 * size, 0.012 * size), mat2, f"hand.{sd}", 6)
    # the wrist's joint ring
    fa = Vector(k.j(f"forearm.{sd}")); k.ell(f"cwrist{sd}", tuple(h + (fa - h).normalized() * 0.04), (0.07, 0.07, 0.06), mat2, f"forearm.{sd}", 14)


def arm_bands(k, mat, mat2, n=4, r=0.085):
    """Segmented shell bands down each arm (a crustacean's limbs)."""
    for s, sd in SIDES:
        for bone, a, b in ((f"upperarm.{sd}", f"upperarm.{sd}", f"forearm.{sd}"), (f"forearm.{sd}", f"forearm.{sd}", f"hand.{sd}")):
            A, B = Vector(k.j(a)), Vector(k.j(b))
            for i in range(n):
                t0 = 0.08 + 0.84 * i / n; t1 = t0 + 0.84 / n * 1.15
                k.cone(f"band{sd}{bone}{i}", A.lerp(B, t0), A.lerp(B, t1), r * (1.06 - 0.12 * t0), r * (0.98 - 0.12 * t0), mat if i % 2 == 0 else mat2, bone, 16)


def scales(k, name, c, rows, cols, dy, dz, size, mat, bone, n=(1, 0, 0), up=(0, 0, 1)):
    """Overlapping fish scales on a surface facing n: staggered rows of flat rounded plates, each lifted at its
    lower edge so the row below tucks under it."""
    c, n, up = Vector(c), Vector(n).normalized(), Vector(up).normalized(); side = up.cross(n).normalized()
    for r in range(rows):
        for q in range(cols):
            y = (q - (cols - 1) / 2 + (0.5 if r % 2 else 0)) * dy; z = -r * dz
            p = c + side * y + up * z + n * (0.004 * r)
            fin(k, f"{name}{r}_{q}", [(-size, size * 0.3), (-size * 0.8, -size * 0.5), (0, -size), (size * 0.8, -size * 0.5), (size, size * 0.3), (0, size * 0.5)], p, side, (up * 0.95 - n * 0.3).normalized(), size * 0.25, mat, bone)


def conch(k, name, c, length, mat, mat2, lip, bone, axis=(-0.35, 0.1, 0.94)):
    """A conch shell: a cone of whorls rising to a spire, a ridge winding round it, knobs on the shoulders, and the
    flared, pink-lipped opening at its base."""
    c, ax = Vector(c), Vector(axis).normalized(); s1, s2 = frame_of(ax)
    whorls = 5
    for i in range(whorls):   # each whorl a swelling ring, smaller as it climbs
        t = i / whorls; r = length * 0.36 * (1 - t) ** 1.1 + 0.02; h = length * (0.18 + 0.72 * t)
        k.ell(f"{name}w{i}", tuple(c + ax * h), (r, r, length * 0.12 * (1 - t * 0.6)), mat if i % 2 == 0 else mat2, bone, 20)
    k.cone(f"{name}spire", c + ax * length * 0.85, c + ax * length * 1.12, length * 0.06, 0.002, mat2, bone, 10)
    k.ell(f"{name}body", tuple(c + ax * length * 0.2), (length * 0.4, length * 0.4, length * 0.26), mat, bone, 24)
    pts = []
    for j in range(40):
        t = j / 39; a = t * whorls * 2 * math.pi; r = length * 0.38 * (1 - t) ** 1.1 + 0.03; h = length * (0.1 + 0.8 * t)
        pts.append(c + ax * h + s1 * math.cos(a) * r + s2 * math.sin(a) * r)
    taper(k, f"{name}ridge", pts, [0.016 * (1 - j / 45) for j in range(40)], mat2, bone, 6)
    for j in range(0, 18, 3): k.cone(f"{name}knob{j}", pts[j], pts[j] + (pts[j] - c - ax * Vector(pts[j] - c).dot(ax)).normalized() * 0.05, 0.022, 0.004, mat2, bone, 8)
    k.ell(f"{name}lip", tuple(c - ax * length * 0.02 + s1 * length * 0.18), (length * 0.08, length * 0.22, length * 0.3), lip, bone, 18)


def spiral_shell(k, name, c, r0, turns, mat, mat2, bone, axis=(0.15, 0, 1)):
    """A conch's spiral: a tube whose radius grows as it winds, around an axis, and a flared mouth."""
    c = Vector(c); ax = Vector(axis).normalized(); s1, s2 = frame_of(ax)
    pts, radii = [], []
    N = 34
    for i in range(N):
        t = i / (N - 1); a = t * turns * 2 * math.pi; R = r0 * (0.15 + 0.85 * t); h = (1 - t) * r0 * 1.6
        pts.append(c + ax * h + s1 * math.cos(a) * R * 0.9 + s2 * math.sin(a) * R * 0.9); radii.append(r0 * (0.08 + 0.55 * t))
    taper(k, name, pts, radii, mat, bone, 14)
    for i in range(4, N, 4): k.ell(f"{name}knob{i}", tuple(pts[i] + (pts[i] - c).normalized() * radii[i] * 0.9), (radii[i] * 0.35,) * 3, mat2, bone, 8)


def coral_branch(k, name, p, d, length, r, depth, mat, bone, rng):
    p, d = Vector(p), Vector(d).normalized(); e = p + d * length
    taper(k, name, curve_pts(p, e, Vector((rng() - 0.5, rng() - 0.5, 0)) * length * 0.3, 5), [r, r * 0.9, r * 0.8, r * 0.7, r * 0.6], mat, bone, 8)
    k.ell(name + "tip", tuple(e), (r * 0.75,) * 3, mat, bone, 8)
    if depth > 0:
        for j in range(2):
            nd = (d + Vector(((rng() - 0.5) * 1.4, (rng() - 0.5) * 1.4, 0.5))).normalized()
            coral_branch(k, f"{name}{j}", e, nd, length * 0.7, r * 0.72, depth - 1, mat, bone, rng)


def barnacle(k, name, p, n, r, mat, mat2, bone):
    """A barnacle: a volcano cone with a dark mouth at its top."""
    p, n = Vector(p), Vector(n).normalized()
    k.cone(name, p, p + n * r * 1.1, r, r * 0.45, mat, bone, 10)
    k.ell(name + "m", tuple(p + n * r * 1.12), (r * 0.36, r * 0.36, r * 0.36), mat2, bone, 8)


def ribbon(k, name, pts, w, mat, bone, twist=0.0):
    """A flat wavy blade along pts (kelp, a sash, a torn hem): a thin strip that narrows to a point."""
    pts = [Vector(p) for p in pts]; bm = bmesh.new(); L, R = [], []
    for i, p in enumerate(pts):
        d = pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]; s1, s2 = frame_of(d)
        a = twist * i; side = s1 * math.cos(a) + s2 * math.sin(a); ww = w * (1 - i / len(pts) * 0.8) * (1 + 0.25 * math.sin(i * 1.7))
        L.append(bm.verts.new(p + side * ww)); R.append(bm.verts.new(p - side * ww))
    L2 = [bm.verts.new(v.co + Vector((0.004, 0, 0))) for v in L]; R2 = [bm.verts.new(v.co + Vector((0.004, 0, 0))) for v in R]
    for i in range(len(pts) - 1):
        bm.faces.new((L[i], L[i + 1], R[i + 1], R[i])); bm.faces.new((R2[i], R2[i + 1], L2[i + 1], L2[i]))
    o = _obj(k, name, bm); s = o.modifiers.new("smooth", 'SUBSURF'); s.levels = 1
    return k.add(o, mat, bone)


def head_shell(k, name, secs, mat, segs=22, e=0.9):
    """A creature-head helmet around the wearer's head (closed; the outfit gives it eyes and a mouth)."""
    has_head(k); return loft(k, name, secs, mat, "head", segs, e)


def visor_band(k, mat, z=0.0, x=0.16, w=0.12, h=0.035):
    k.ell("visorband", tuple(HC + V(x, 0, z)), (0.04, w, h), mat, "head", 18)


# ---------------------------------------------------------------- the outfits, shaped like what they are
def o_lobster(k):   # the Lobster Knight
    base(k, "under", 1.05)
    cuirass(k, "shell", "gold"); pauldrons(k, "shell", "gold", 1.05); leg_armour(k, "shell", "gold")
    arm_bands(k, "shell", "shell2"); belt(k, "leatherx", "gold"); boots(k, "shell"); k.mitts("shell")
    segmented_tail(k, "ltail", "shell", "shell2", 6, 1.0, 0.62)
    # the head: a lobster's carapace helm with its rostrum, stalked eyes, long antennae and the mouthparts at the visor
    head_shell(k, "lhead", [(-0.19, 0.02, 0.12, 0.15), (-0.1, 0.04, 0.16, 0.19), (0.02, 0.04, 0.165, 0.19), (0.14, 0.02, 0.15, 0.16), (0.24, 0.0, 0.1, 0.11), (0.31, -0.01, 0.05, 0.06)], "shell")
    for i in range(3): arc_band(k, f"lneck{i}", HC + V(-0.02, 0, 0), 0.16 - 0.005 * i, HC.z - 0.17 - i * 0.05, HC.z - 0.12 - i * 0.05, math.pi * 0.55, math.pi * 1.45, 0.02, "shell" if i % 2 else "shell2", "head", 14, None, 0.015)
    k.ell("lface", tuple(HC + V(0.2, 0, -0.08)), (0.06, 0.12, 0.07), "shell2", "head", 16)
    taper(k, "rostrum", [HC + V(0.22, 0, 0.1), HC + V(0.32, 0, 0.12), HC + V(0.42, 0, 0.11)], [0.035, 0.02, 0.003], "shell", "head")
    for s in (1, -1):
        taper(k, f"stalk{s}", curve_pts(HC + V(0.18, s * 0.06, 0.1), HC + V(0.22, s * 0.11, 0.2), V(0, s * 0.02, 0), 4), [0.022, 0.02, 0.018, 0.016], "shell", "head")
        eye_ball(k, f"leye{s}", HC + V(0.23, s * 0.115, 0.215), 0.03, "black", "gloss", (1, s * 0.4, 0.2))
        taper(k, f"antenna{s}", curve_pts(HC + V(0.2, s * 0.08, 0.04), HC + V(-0.3, s * 0.5, 0.75), V(0.3, s * 0.15, 0.3), 12), [0.016 - 0.0012 * i for i in range(12)], "shell2", "head", 6)
        taper(k, f"antennule{s}", curve_pts(HC + V(0.24, s * 0.03, 0.02), HC + V(0.45, s * 0.12, 0.25), V(0.05, 0, 0.05), 6), [0.008, 0.007, 0.006, 0.005, 0.004, 0.002], "shell2", "head", 6)
        k.ell(f"maxilla{s}", tuple(HC + V(0.2, s * 0.05, -0.12)), (0.04, 0.025, 0.06), "shell2", "head", 12)
    for i in range(5): k.ell(f"spot{i}", tuple(HC + V(-0.05 + i * 0.05, (i % 2 - 0.5) * 0.24, 0.17)), (0.02, 0.02, 0.012), "shell2", "head", 8)
    visor_band(k, "visor_d", -0.03, 0.2, 0.1, 0.03)
    big_claw(k, "R", "shell", "shell2", 1.25); big_claw(k, "L", "shell", "shell2", 0.85)


def o_crab(k):   # the Crab Samurai: a carapace across the shoulders with a toothed rim, stalked eyes on the kabuto
    base(k, "under", 1.05)
    cuirass(k, "shell", "lacing"); leg_armour(k, "lacing", "shell"); arm_bands(k, "shell", "lacing", 3)
    k.ell("carapace", (-0.17, 0, 1.3), (0.2, 0.38, 0.26), "shell", "chest", 30)
    for i in range(9):
        a = math.pi * (0.62 + 0.76 * i / 8)
        k.cone(f"rimtooth{i}", (-0.17 + 0.2 * math.cos(a) * 0.9, 0.38 * math.sin(a) * 0.98, 1.42), (-0.17 + 0.26 * math.cos(a), 0.47 * math.sin(a), 1.44), 0.035, 0.002, "shell2", "chest", 8)
    for i in range(6): k.ell(f"bump{i}", (-0.33, (i % 3 - 1) * 0.14, 1.2 + (i // 3) * 0.16), (0.03, 0.05, 0.05), "shell2", "chest", 10)
    for i in range(6):
        a = -1.2 + 2.4 * i / 5; sd = "L" if a > 0 else "R"
        fin(k, f"kusa{i}", [(0, -0.05), (0.22, -0.06), (0.24, 0.06), (0, 0.06)], (0.16 * math.cos(a) + 0.02, 0.2 * math.sin(a), 0.96), (0, 0, -1), (-math.sin(a), math.cos(a), 0), 0.03, "shell" if i % 2 else "lacing", f"thigh.{sd}")
    belt(k, "lacing", "gold"); boots(k, "lacing"); k.mitts("lacing")
    def crest(k):
        k.lathe("kabutorim", [(0.32, -0.02), (0.28, 0.03), (0.22, 0.04)], tuple(HC + V(0, 0, -0.02)), "shell", "head", 32)
        for s in (1, -1):
            taper(k, f"stalk{s}", curve_pts(HC + V(0.12, s * 0.05, 0.16), HC + V(0.16, s * 0.13, 0.36), V(0, s * 0.02, 0), 5), [0.022, 0.02, 0.018, 0.017, 0.016], "shell", "head")
            eye_ball(k, f"ceye{s}", HC + V(0.165, s * 0.135, 0.38), 0.03, "black", "gloss", (1, s * 0.3, 0.3))
        k.ell("maempo", tuple(HC + V(0.16, 0, -0.06)), (0.06, 0.11, 0.07), "lacing", "head", 18)
        for s in (1, -1): taper(k, f"maehorn{s}", curve_pts(HC + V(0.18, s * 0.03, 0.12), HC + V(0.22, s * 0.2, 0.36), V(0.05, s * 0.05, 0), 6), [0.02, 0.018, 0.014, 0.01, 0.006, 0.002], "gold", "head", 8)
    helm(k, "shell", "gold", "visor_d", 0.12, crest)
    big_claw(k, "L", "shell", "shell2", 1.4)
    blade(k, "steelg", "gold", "R", 0.8, 0.03, 0.08)


def o_pufferfish(k):   # the Spined Brawler: the helm is the puffer itself - huge eyes, a pout, spines and little fins
    base(k, "suit", 1.07)
    k.ell("vest", (0.02, 0, 1.2), (0.23, 0.26, 0.26), "skin", "chest", 30)
    k.ell("vestbelly", (0.08, 0, 1.12), (0.18, 0.21, 0.18), "belly", "chest", 24)
    CO.spikes(k, "vq", (0.02, 0, 1.2), (0.23, 0.26, 0.26), 26, 0.07, "spine_m", "chest", 0.018)
    belt(k, "leatherx", "gold"); trousers(k, "suit"); boots(k, "leatherx"); gloves(k, "skin")
    for s, sd in SIDES:
        h = k.j(f"palm.{sd}"); d = V(0, s * 0.62, -0.78).normalized()
        for i in range(3): k.cone(f"knuck{sd}{i}", h + d * 0.05 + V(0.05, 0, (i - 1) * 0.02), h + d * 0.05 + V(0.12, 0, (i - 1) * 0.02), 0.012, 0.001, "spine_m", f"hand.{sd}", 6)
    k.ell("phead", tuple(HC + V(0.02, 0, 0.0)), (0.25, 0.24, 0.24), "skin", "head", 32); has_head(k)
    k.ell("pbelly", tuple(HC + V(0.07, 0, -0.08)), (0.2, 0.2, 0.17), "belly", "head", 24)
    CO.spikes(k, "hq", tuple(HC), (0.25, 0.24, 0.24), 22, 0.08, "spine_m", "head", 0.016)
    for s in (1, -1):
        eye_ball(k, f"peye{s}", HC + V(0.17, s * 0.12, 0.07), 0.065, "white", "black", (1, s * 0.5, 0))
        fin(k, f"pfin{s}", [(0, -0.04), (0.12, -0.07), (0.14, 0.0), (0.12, 0.07), (0, 0.04)], HC + V(-0.02, s * 0.22, -0.02), (0, s * 0.6, -0.3), (1, 0, 0), 0.015, "fin", "head")
    k.ell("pmouth", tuple(HC + V(0.245, 0, -0.06)), (0.035, 0.045, 0.03), "lip", "head", 14)
    k.ell("pmouthin", tuple(HC + V(0.27, 0, -0.06)), (0.012, 0.025, 0.014), "black", "head", 10)
    k.ell("pvisor", tuple(HC + V(0.2, 0, 0.0)), (0.04, 0.12, 0.03), "visor_d", "head", 14)
    fin(k, "ptail", [(0, -0.05), (0.2, -0.13), (0.24, 0), (0.2, 0.13), (0, 0.05)], (-0.24, 0, 1.15), (-0.7, 0, 0.2), (0, 1, 0), 0.02, "fin", "chest")


def o_jelly(k):   # the Moon Drifter: a translucent bell with a glowing rim, frilly oral arms and long ribbons of light
    base(k, "suit", 1.04); glow_seams(k, "glow")
    k.lathe("glass_bell", [(0.001, 0.27), (0.14, 0.25), (0.23, 0.14), (0.27, 0.0), (0.26, -0.05), (0.21, -0.03)], tuple(HC), "glass_bell", "head", 36)
    k.lathe("bellinner", [(0.001, 0.2), (0.12, 0.18), (0.18, 0.08), (0.2, -0.02)], tuple(HC), "bellcore", "head", 28)
    for i in range(4): k.ell(f"gonad{i}", tuple(HC + V(0.08 * math.cos(i * math.pi / 2), 0.08 * math.sin(i * math.pi / 2), 0.15)), (0.05, 0.05, 0.02), "glow", "head", 12)
    for i in range(16):
        a = i * 2 * math.pi / 16; k.ell(f"rimlight{i}", tuple(HC + V(0.265 * math.cos(a), 0.265 * math.sin(a), -0.045)), (0.012, 0.012, 0.012), "glow", "head", 6)
    k.ell("jface", tuple(HC + V(0.01, 0, -0.03)), (0.12, 0.11, 0.13), "suit", "head", 24); has_head(k)
    for s in (1, -1): k.ell(f"jeye{s}", tuple(HC + V(0.115, s * 0.045, 0.0)), (0.01, 0.028, 0.01), "glow", "head", 10)
    for i in range(4):   # the oral arms: frilled ribbons down the front and back
        a = i * math.pi / 2 + math.pi / 4; p0 = HC + V(0.18 * math.cos(a), 0.18 * math.sin(a), -0.08)
        ribbon(k, f"oral{i}", curve_pts(p0, p0 + V(0.05 * math.cos(a), 0.05 * math.sin(a), -0.9), V(0.06, 0.06, 0), 10), 0.05, "tent", "chest", 0.9)
    for i in range(12):
        a = i * 2 * math.pi / 12 + 0.13; p0 = HC + V(0.25 * math.cos(a), 0.25 * math.sin(a), -0.06)
        taper(k, f"rib{i}", curve_pts(p0, p0 + V(-0.08, 0.03 * math.sin(i), -1.0 - 0.1 * (i % 3)), V(0.05 * math.cos(a), 0.05 * math.sin(a), 0), 10), [0.008] * 9 + [0.002], "tent", "chest", 6)
    gloves(k, "suit"); boots(k, "suit")
    k.tube("beltglow", [V(0.2 * math.cos(a), 0.2 * math.sin(a), 0.98) for a in [j * 2 * math.pi / 24 for j in range(25)]], 0.01, "glow", "pelvis")


def o_hermit(k):   # the Shell Wanderer: a real spiral conch on the back, eyestalks from the hood, claw gloves
    base(k, "canvas", 1.06)
    hood(k, "cloak", "shadow", "glint")
    for s in (1, -1):
        taper(k, f"hstalk{s}", curve_pts(HC + V(0.14, s * 0.04, -0.02), HC + V(0.24, s * 0.08, 0.06), V(0, 0, 0.03), 4), [0.016, 0.015, 0.014, 0.013], "claw", "head")
        eye_ball(k, f"heye{s}", HC + V(0.25, s * 0.085, 0.075), 0.022, "black", "gloss", (1, s * 0.3, 0.2))
    cape(k, "cloak", "canvas", 1.05, 0.25, 0.2)
    trousers(k, "canvas"); boots(k, "leatherx")
    big_claw(k, "R", "claw", "shell2", 0.8); big_claw(k, "L", "claw", "shell2", 0.65)
    k.tube("sash", [V(0.18, 0.18, 1.42), V(0.2, 0.0, 1.22), V(0.18, -0.18, 1.0)], 0.03, "wrap", "chest")
    conch(k, "conch", (-0.34, 0.0, 1.0), 0.6, "shell", "shell2", "lipp", "chest")
    k.tube("straps", [V(0.12, 0.15, 1.43), V(-0.2, 0.17, 1.3)], 0.015, "leatherx", "chest")


def star_shape(k, name, c, n, r_out, r_in, depth, mat, bone, u=(0, 1, 0), v=(0, 0, 1)):
    pts = []
    for i in range(n * 2):
        a = math.pi / 2 + i * math.pi / n; r = r_out if i % 2 == 0 else r_in
        pts.append((math.cos(a) * r, math.sin(a) * r))
    fin(k, name, pts, c, u, v, depth, mat, bone)


def o_starfish(k):   # Starfall: a thick five-armed star on the breast and back, bumpy, and a star crest
    base(k, "suitw", 1.05)
    cuirass(k, "suitw", "gold", belly=False)
    star_shape(k, "starfront", (0.24, 0, 1.25), 5, 0.24, 0.09, 0.06, "star", "chest")
    star_shape(k, "starback", (-0.2, 0, 1.25), 5, 0.22, 0.08, 0.05, "star", "chest")
    for i in range(5):
        a = math.pi / 2 + i * 2 * math.pi / 5
        for t in (0.25, 0.5, 0.75): k.ell(f"sbump{i}{t}", (0.27, math.cos(a) * 0.24 * t, 1.25 + math.sin(a) * 0.24 * t), (0.015, 0.018, 0.018), "bump", "chest", 8)
    k.ell("starcore", (0.272, 0, 1.25), (0.012, 0.04, 0.04), "glow", "chest", 12)
    pauldrons(k, "star", "gold", 0.9, 2); arm_armour(k, "suitw", "star", 0.95); leg_armour(k, "suitw", "star")
    belt(k, "star", "gold"); boots(k, "star"); k.mitts("star")
    def crest(k): star_shape(k, "starhelm", HC + V(-0.02, 0, 0.3), 5, 0.16, 0.06, 0.04, "star", "head", (0, 1, 0), (0, 0, 1))
    helm(k, "suitw", "star", "visor_g", 0.13, crest)
    cape(k, "star", "suitw", 0.7, 0.2, 0.1)


def o_kelp(k):   # the Kelp Ranger: a cloak of real kelp blades (wavy, flat), bladder floats, a ranger's hood
    base(k, "leathg", 1.05)
    hood(k, "kelp", "shadow", "glint", pointed=False)
    trousers(k, "leathg"); boots(k, "leatherx"); gloves(k, "leatherx"); belt(k, "leatherx", "brass")
    for i in range(18):
        a = math.pi * (0.5 + i / 17.0); p0 = V(0.19 * math.cos(a), 0.24 * math.sin(a), 1.44)
        ribbon(k, f"blade{i}", curve_pts(p0, p0 + V(-0.12, 0.03 * math.sin(i), -0.9 - 0.12 * (i % 3)), V(-0.05, 0.04 * math.cos(i), 0), 9), 0.045, "kelp" if i % 2 else "kelp2", "chest", 0.35)
        if i % 3 == 0: k.ell(f"float{i}", tuple(p0 + V(-0.06, 0, -0.3)), (0.03, 0.03, 0.05), "float", "chest", 10)
    for i in range(6):
        a = -0.9 + i * 0.36; p0 = HC + V(0.05 * math.cos(a) - 0.05, 0.2 * math.sin(a), 0.15)
        ribbon(k, f"hoodkelp{i}", curve_pts(p0, p0 + V(-0.25, 0.05 * math.sin(a), 0.2), V(0, 0, 0.08), 6), 0.03, "kelp2", "head", 0.5)
    p, d = held(k, "L")
    bow = [p + V(0.02, 0, -0.65 + 1.3 * t) + V(0.12 * math.sin(t * math.pi), 0, 0) for t in [j / 10 for j in range(11)]]
    taper(k, "bow", bow, [0.008, 0.012, 0.015, 0.016, 0.017, 0.018, 0.017, 0.016, 0.015, 0.012, 0.008], "wood", "hand.L", 8)
    k.tube("bowstring", [bow[0], bow[-1]], 0.003, "white", "hand.L")


def o_barnacle(k):   # the Hull Breaker: riveted plate crusted with real barnacles (cones with mouths) and mussels
    base(k, "under", 1.08)
    cuirass(k, "iron", "rust"); pauldrons(k, "iron", "rust", 1.3, 3); arm_armour(k, "iron", "rust", 1.2); leg_armour(k, "iron", "rust")
    import random; rng = random.Random(7)
    for i in range(24):
        a = rng.uniform(-1.4, 1.4); z = rng.uniform(1.08, 1.42); n = V(math.cos(a), math.sin(a) * 1.1, rng.uniform(-0.2, 0.4))
        barnacle(k, f"cb{i}", (0.03 + 0.19 * math.cos(a), 0.23 * math.sin(a), z), n, rng.uniform(0.018, 0.032), "barn", "black", "chest")
    for s, sd in SIDES:
        ua = Vector(k.j(f"upperarm.{sd}"))
        for i in range(6): barnacle(k, f"pb{sd}{i}", ua + V(rng.uniform(-0.06, 0.06), s * 0.08, 0.1 + rng.uniform(-0.03, 0.03)), (rng.uniform(-0.3, 0.3), s * 0.6, 0.8), rng.uniform(0.016, 0.026), "barn", "black", f"upperarm.{sd}")
        for i in range(3): k.ell(f"mussel{sd}{i}", tuple(ua + V(0.05 * i - 0.05, s * 0.12, -0.03)), (0.04, 0.015, 0.022), "mussel", f"upperarm.{sd}", 10)
    for s, sd in SIDES: k.ell(f"ggaunt{sd}", tuple(k.j(f"palm.{sd}")), (0.09, 0.09, 0.1), "iron", f"hand.{sd}", 18)
    boots(k, "iron"); belt(k, "rust", "iron")
    def crest(k):
        for i in range(12):
            a = rng.uniform(0, 2 * math.pi); el = rng.uniform(0.1, 1.2); n = V(math.cos(a) * math.cos(el), math.sin(a) * math.cos(el), math.sin(el))
            if n.x > 0.6 and el < 0.5: continue
            barnacle(k, f"hb{i}", HC + V(0, 0, 0.03) + n * 0.2, n, rng.uniform(0.018, 0.03), "barn", "black", "head")
    helm(k, "iron", "rust", "visor_d", 0.1, crest)
    p, d = held(k, "R"); up = V(0.3, 0, 1).normalized()
    taper(k, "hhaft", [p - up * 0.35, p + up * 0.75], [0.022, 0.02], "wood", "hand.R", 10)
    k.box("hhead", (0.16, 0.26, 0.14), tuple(p + up * 0.78), "iron", "hand.R", bevel=0.02)
    for i in range(4): barnacle(k, f"hamb{i}", p + up * 0.78 + V(0.08, (i - 1.5) * 0.06, 0.07), (0.3, 0, 1), 0.02, "barn", "black", "hand.R")


def o_shark(k):   # the Shark Hunter: the helm IS a shark's head - long snout, jaw full of teeth, gills, a dorsal fin
    base(k, "grey", 1.04)
    k.ell("belly", (0.07, 0, 1.18), (0.15, 0.17, 0.26), "pale", "chest", 24)
    glow_seams(k, "teal", chest=False)
    pauldrons(k, "grey", "steelg", 0.85, 2); arm_armour(k, "grey", "steelg", 0.95)
    belt(k, "black", "steelg"); boots(k, "grey"); k.mitts("black")
    fin(k, "dorsal", [(0, 0), (0.08, 0.36), (0.16, 0.38), (0.12, 0.2), (0.3, 0)], (-0.12, 0, 1.28), (-1, 0, 0), (0, 0, 1), 0.04, "grey", "chest")
    fin(k, "caudal", [(0, -0.05), (0.2, -0.26), (0.28, -0.24), (0.17, 0), (0.3, 0.3), (0.22, 0.32), (0, 0.05)], (-0.15, 0, 0.9), (-0.8, 0, -0.6), (0.0, 0.0, 1.0), 0.035, "grey", "pelvis")
    taper(k, "tailstock", [(-0.08, 0, 1.0), (-0.16, 0, 0.92), (-0.2, 0, 0.86)], [0.06, 0.05, 0.035], "grey", "pelvis", 12)
    for s, sd in SIDES:   # pectoral fins along the forearms
        fa, h = Vector(k.j(f"forearm.{sd}")), Vector(k.j(f"hand.{sd}"))
        fin(k, f"pect{sd}", [(0, -0.04), (0.18, -0.02), (0.32, 0.1), (0.2, 0.06), (0, 0.04)], fa + V(-0.05, 0, 0), (h - fa).normalized(), V(-1, 0, 0), 0.025, "grey", f"forearm.{sd}")
    # the head: snout forward over the face, the lower jaw below it (the face shows in the mouth), eyes on the sides
    head_shell(k, "shead", [(-0.22, 0.04, 0.13, 0.15), (-0.12, 0.06, 0.19, 0.2), (0.0, 0.07, 0.2, 0.19), (0.14, 0.07, 0.17, 0.15), (0.26, 0.06, 0.12, 0.1), (0.36, 0.04, 0.07, 0.06), (0.41, 0.03, 0.03, 0.03)], "grey")
    loft(k, "sjaw", [(-0.05, -0.15, 0.15, 0.06), (0.1, -0.16, 0.15, 0.06), (0.22, -0.13, 0.1, 0.05), (0.27, -0.11, 0.05, 0.03)], "pale", "head", 18)
    k.ell("smouth", tuple(HC + V(0.18, 0, -0.06)), (0.08, 0.13, 0.05), "black", "head", 18)
    teeth(k, "uteeth", HC + V(0.24, -0.1, -0.03), HC + V(0.24, 0.1, -0.03), 9, 0.04, "white", "head", True)
    teeth(k, "lteeth", HC + V(0.22, -0.09, -0.1), HC + V(0.22, 0.09, -0.1), 8, 0.035, "white", "head", False)
    k.ell("snout_under", tuple(HC + V(0.22, 0, 0.0)), (0.12, 0.12, 0.04), "pale", "head", 18)
    for s in (1, -1):
        eye_ball(k, f"seye{s}", HC + V(0.16, s * 0.16, 0.07), 0.025, "black", "gloss", (0.3, s, 0))
        for g in range(4): fin(k, f"gill{s}{g}", [(0, -0.05), (0.008, -0.05), (0.008, 0.05), (0, 0.05)], HC + V(-0.02 - g * 0.035, s * 0.2, -0.04), (1, 0, 0), (0, 0, 1), 0.006, "black", "head")
    fin(k, "hfin", [(0, 0), (0.06, 0.16), (0.12, 0.17), (0.1, 0.08), (0.2, 0)], HC + V(0.0, 0, 0.2), (-1, 0, 0), (0, 0, 1), 0.03, "grey", "head")
    spear(k, "steelg", "white", "R", 1.6)


def o_octopus(k):   # the Abyss Witch: a robe whose hem becomes eight curling tentacles with suckers; a wide hat
    base(k, "robe", 1.08)
    hood(k, "robe", "shadow", "glowv", pointed=False)
    k.lathe("hat", [(0.42, -0.0), (0.4, 0.02), (0.18, 0.03), (0.17, 0.12), (0.1, 0.3), (0.06, 0.4), (0.07, 0.45), (0.12, 0.47)], tuple(HC + V(0, 0, 0.12)), "skin", "head", 32)
    k.tube("hatband", [HC + V(0.17 * math.cos(a), 0.17 * math.sin(a), 0.16) for a in [j * 2 * math.pi / 24 for j in range(25)]], 0.012, "sucker", "head")
    coat_skirt(k, "robe", "sucker", 0.42, 0.15, 0.21, 0.29)
    for i in range(8):
        a = i * 2 * math.pi / 8 + 0.2; c, s = math.cos(a), math.sin(a)
        p0 = V(0.27 * c, 0.27 * s, 0.6)
        pts = [p0, p0 + V(0.06 * c, 0.06 * s, -0.2), p0 + V(0.15 * c, 0.15 * s, -0.38), p0 + V(0.27 * c, 0.27 * s, -0.5), p0 + V(0.38 * math.cos(a + 0.3), 0.38 * math.sin(a + 0.3), -0.52), p0 + V(0.42 * math.cos(a + 0.7), 0.42 * math.sin(a + 0.7), -0.45), p0 + V(0.36 * math.cos(a + 1.0), 0.36 * math.sin(a + 1.0), -0.4)]
        tentacle(k, f"oarm{i}", pts, 0.075, 0.012, "skin", "sucker", "thigh.L" if s > 0 else "thigh.R", (c * -0.3, s * -0.3, -1))
    gloves(k, "skin"); boots(k, "skin", tall=False)
    p, d = held(k, "R"); up = V(0.2, 0, 1).normalized()
    taper(k, "ostaff", curve_pts(p - up * 0.6, p + up * 1.0, V(0.03, 0, 0), 6), [0.018, 0.017, 0.016, 0.016, 0.015, 0.014], "wood", "hand.R", 8)
    tentacle(k, "staffcurl", [p + up * 1.0, p + up * 1.08 + V(0.06, 0, 0), p + up * 1.12 + V(0.12, 0, -0.04), p + up * 1.08 + V(0.14, 0, -0.1)], 0.018, 0.004, "skin", "sucker", "hand.R")
    k.ell("oorb", tuple(p + up * 1.0 + V(0.06, 0, 0.0)), (0.055, 0.055, 0.055), "glowv", "hand.R", 16)


def o_angler(k):   # the Lantern Wraith: a hood with a huge underbite of needle teeth, the lure arching over it
    base(k, "dark", 1.05)
    coat_body(k, "dark", "darkt", lapels=False, buttons="darkt"); coat_skirt(k, "dark", "darkt", 0.7, 0.4, 0.22, 0.34)
    hood(k, "dark", "shadow", "glow")
    loft(k, "ajaw", [(-0.08, -0.14, 0.17, 0.06), (0.1, -0.13, 0.19, 0.07), (0.22, -0.09, 0.16, 0.06), (0.27, -0.05, 0.1, 0.05)], "darkt", "head", 18)
    for i in range(9):
        a = -1.0 + i * 0.25; p = HC + V(0.24 * math.cos(a) * 0.9 + 0.02, 0.17 * math.sin(a), -0.07)
        k.cone(f"fang{i}", p, p + V(0.02, 0, 0.08 + 0.03 * (i % 2)), 0.012, 0.0006, "white", "head", 6)
    taper(k, "lure", curve_pts(HC + V(0.0, 0, 0.2), HC + V(0.42, 0, 0.36), V(0, 0, 0.3), 9), [0.014 - 0.001 * i for i in range(9)], "darkt", "head", 6)
    k.ell("lurelight", tuple(HC + V(0.45, 0, 0.3)), (0.05, 0.05, 0.06), "glow", "head", 16)
    trousers(k, "dark"); boots(k, "darkt"); gloves(k, "darkt")
    lantern(k, "glow", "darkt", "L")
    p, d = held(k, "R"); taper(k, "hook", [p, p + V(0.05, 0, -0.25), p + V(0.15, 0, -0.32), p + V(0.2, 0, -0.22)], [0.012, 0.012, 0.01, 0.004], "steelg", "hand.R", 8)


def o_turtle(k):   # the Shellback Guard: a domed carapace of raised hexagonal scutes, a beaked turtle helm, flipper bracers
    base(k, "skin", 1.05)
    cuirass(k, "belly", "scute", belly=True); pauldrons(k, "shell", "scute", 1.0, 2); leg_armour(k, "skin", "scute")
    k.ell("carapace", (-0.2, 0, 1.18), (0.17, 0.32, 0.37), "shell", "chest", 32)
    for i, (y, z) in enumerate([(0, 1.18), (0.16, 1.12), (-0.16, 1.12), (0, 1.4), (0, 0.96), (0.16, 1.32), (-0.16, 1.32), (0.17, 0.96), (-0.17, 0.96)]):
        n = Vector((-1, y * 1.6, (z - 1.18) * 1.6)).normalized(); p = Vector((-0.2, y, z)) + n * 0.16
        star_shape(k, f"scute{i}", tuple(p), 3, 0.115, 0.1, 0.045, "scute", "chest", tuple(Vector((0, 1, 0)).cross(n).normalized()) if abs(n.z) < 0.9 else (0, 1, 0), tuple(n.cross(Vector((0, 1, 0)).cross(n).normalized())))
    for s, sd in SIDES:
        fa, h = Vector(k.j(f"forearm.{sd}")), Vector(k.j(f"hand.{sd}"))
        fin(k, f"flipper{sd}", [(0, -0.05), (0.2, -0.07), (0.34, -0.02), (0.3, 0.05), (0, 0.05)], fa + V(0, s * 0.03, 0), (h - fa).normalized(), V(1, 0, 0), 0.03, "skin", f"forearm.{sd}")
    boots(k, "shell"); k.mitts("skin"); belt(k, "shell", "gold")
    head_shell(k, "thead", [(-0.2, 0.0, 0.14, 0.16), (-0.08, 0.02, 0.19, 0.2), (0.06, 0.02, 0.19, 0.19), (0.18, 0.0, 0.15, 0.15), (0.25, -0.03, 0.09, 0.09)], "skin")
    k.cone("beakup", HC + V(0.22, 0, 0.0), HC + V(0.32, 0, -0.07), 0.08, 0.004, "beak", "head", 12)
    k.ell("beaklow", tuple(HC + V(0.22, 0, -0.08)), (0.07, 0.08, 0.035), "beak", "head", 12)
    for s in (1, -1): eye_ball(k, f"teye{s}", HC + V(0.16, s * 0.13, 0.04), 0.03, "black", "gloss", (0.5, s, 0))
    for i in range(5): k.ell(f"hspot{i}", tuple(HC + V(-0.1 + i * 0.06, (i % 2 - 0.5) * 0.2, 0.18)), (0.03, 0.03, 0.015), "scute", "head", 8)
    visor_band(k, "visor_d", -0.02, 0.21, 0.1, 0.02)
    p, d = held(k, "L"); k.ell("shield", tuple(p + V(0.08, 0.05, 0.05)), (0.06, 0.3, 0.3), "shell", "hand.L", 26)
    for i, (dy, dz) in enumerate([(0, 0), (0.14, 0.07), (-0.14, 0.07), (0.14, -0.07), (-0.14, -0.07), (0, 0.15), (0, -0.15)]): star_shape(k, f"shex{i}", tuple(p + V(0.13, 0.05 + dy, 0.05 + dz)), 3, 0.08, 0.07, 0.03, "scute", "hand.L", (0, 1, 0), (0, 0, 1))
    spear(k, "wood", "gold", "R", 1.7)


def o_ghost(k):   # the Ghost Captain: a pale greatcoat in tatters, chains, a tricorn over a hood with burning eyes
    base(k, "sheet", 1.05)
    coat_body(k, "sheet", "sheetd", buttons="glowg"); coat_skirt(k, "sheet", "sheetd", 0.65, 0.45, 0.22, 0.36)
    for i in range(14):   # the torn hem: ragged ribbons hanging below the skirts
        a = 0.5 + i * (2 * math.pi - 1.0) / 13; p0 = V(0.35 * math.cos(a), 0.35 * math.sin(a), 0.38)
        ribbon(k, f"tatter{i}", curve_pts(p0, p0 + V(0.03 * math.cos(a), 0.03 * math.sin(a), -0.18 - 0.08 * (i % 3)), V(0, 0, 0), 4), 0.05, "sheet", "thigh.L" if math.sin(a) > 0 else "thigh.R", 0.4)
    hood(k, "sheet", "shadow", "glowg", pointed=False)
    k.lathe("tricorn", [(0.001, 0.0), (0.33, 0.0), (0.31, 0.03), (0.15, 0.05), (0.14, 0.13), (0.001, 0.15)], tuple(HC + V(0, 0, 0.14)), "black", "head", 3)
    k.tube("tritrim", [HC + V(0.33 * math.cos(a), 0.33 * math.sin(a), 0.15) for a in [j * 2 * math.pi / 3 for j in range(4)]], 0.01, "glowg", "head")
    trousers(k, "sheet"); boots(k, "black"); gloves(k, "sheetd")
    for i in range(16): k.lathe(f"link{i}", [(0.022, -0.006), (0.026, 0.0), (0.022, 0.006)], (0.21 * math.cos(i * 0.8), 0.23 * math.sin(i * 0.8), 1.04 + i * 0.022), "iron", "spine", 8, 1.0, 0.5)
    lantern(k, "glowg", "iron", "L"); blade(k, "sheetd", "glowg", "R", 0.8, 0.032, 0.1)


def o_coral(k):   # the Reef Monarch: a crown and pauldrons of branching coral with polyps, pearl armour, a trident
    import random; rng = random.Random(3).random
    base(k, "pearl", 1.05)
    cuirass(k, "pearl", "coral2"); arm_armour(k, "pearl", "coral2"); leg_armour(k, "pearl", "coral2")
    belt(k, "coral2", "gold"); boots(k, "pearl"); k.mitts("pearl")
    for s, sd in SIDES:
        ua = Vector(k.j(f"upperarm.{sd}"))
        k.ell(f"cpaul{sd}", tuple(ua + V(0, s * 0.03, 0.06)), (0.13, 0.12, 0.08), "coral1", f"upperarm.{sd}", 18)
        for j in range(3): coral_branch(k, f"cs{sd}{j}", ua + V(-0.05 + j * 0.05, s * 0.06, 0.1), (rng() - 0.5, s * 0.4, 1), 0.12, 0.02, 2, ["coral1", "coral2", "coral3"][j], f"upperarm.{sd}", rng)
    def crest(k):
        for i in range(7):
            a = i * 2 * math.pi / 7; p = HC + V(0.15 * math.cos(a), 0.15 * math.sin(a), 0.14)
            coral_branch(k, f"crown{i}", p, (math.cos(a) * 0.3, math.sin(a) * 0.3, 1), 0.13, 0.02, 2, "coral1" if i % 3 else "coral3", "head", rng)
        ring(k, "crownband", tuple(HC + V(0, 0, 0.12)), 0.18, 0.04, "gold", "head", 28)
        for i in range(8): k.ell(f"pearl{i}", tuple(HC + V(0.185 * math.cos(i * math.pi / 4), 0.185 * math.sin(i * math.pi / 4), 0.12)), (0.018,) * 3, "pearl", "head", 8)
    helm(k, "pearl", "gold", "visor_g", 0.12, crest)
    cape(k, "coral1", "coral3", 1.0, 0.26, 0.22)
    spear(k, "gold", "gold", "R", 1.8, tines=3)


def o_swordfish(k):   # the Swordfish Duellist: the helm is the fish - a long bill, a great sail along the back, eyes
    base(k, "blue", 1.04)
    coat_body(k, "blue", "silver", lapels=True, buttons="silver"); coat_skirt(k, "blue", "silver", 0.45, 0.65, 0.21, 0.28)
    trousers(k, "silverw"); boots(k, "black"); gloves(k, "black")
    k.ell("cravat", (0.2, 0, 1.38), (0.03, 0.06, 0.06), "silverw", "chest", 14)
    head_shell(k, "swhead", [(-0.2, 0.02, 0.14, 0.16), (-0.08, 0.04, 0.18, 0.19), (0.06, 0.03, 0.18, 0.17), (0.18, 0.01, 0.13, 0.12), (0.26, 0.0, 0.06, 0.06)], "blue")
    taper(k, "bill", [HC + V(0.24, 0, 0.01), HC + V(0.4, 0, 0.02), HC + V(0.58, 0, 0.03), HC + V(0.74, 0, 0.035)], [0.04, 0.025, 0.014, 0.002], "silver", "head", 10, 0.6)
    k.ell("swbelly", tuple(HC + V(0.06, 0, -0.1)), (0.16, 0.15, 0.08), "silverw", "head", 18)
    for s in (1, -1): eye_ball(k, f"sweye{s}", HC + V(0.17, s * 0.1, 0.06), 0.035, "white", "black", (0.4, s, 0))
    sail = [(0, 0), (0.05, 0.3), (0.15, 0.42), (0.3, 0.44), (0.45, 0.36), (0.55, 0.2), (0.6, 0)]
    fin(k, "sail", sail, HC + V(0.12, 0, 0.15), (-1, 0, 0), (0, 0, 1), 0.02, "sail", "head")
    for i in range(7): k.tube(f"spine{i}", [HC + V(0.12 - i * 0.08, 0, 0.17), HC + V(0.12 - i * 0.085 - 0.03, 0, 0.17 + sail[min(i, 6)][1] * 0.95)], 0.004, "blue", "head")
    visor_band(k, "visor_d", -0.04, 0.2, 0.1, 0.025)
    blade(k, "silver", "silver", "R", 0.95, 0.018)
    cape(k, "blue", "silver", 0.55, 0.15, 0.06)


def o_kraken(k):   # the Kraken Lord: black plate, a cloak of crimson tentacles with suckers, a beaked mask, a crown of horns
    base(k, "black", 1.05)
    cuirass(k, "blackp", "red"); pauldrons(k, "red", "gold", 1.15, 3); arm_armour(k, "blackp", "red"); leg_armour(k, "blackp", "red")
    belt(k, "red", "gold"); boots(k, "blackp"); k.mitts("blackp")
    for i in range(10):
        a = math.pi * (0.6 + 0.8 * i / 9); p0 = V(0.15 * math.cos(a), 0.24 * math.sin(a), 1.44)
        pts = [p0, p0 + V(-0.08, 0.03 * math.sin(i), -0.35), p0 + V(-0.13, 0.05 * math.sin(i * 1.3), -0.7), p0 + V(-0.08, 0.08 * math.sin(i), -0.95), p0 + V(0.02, 0.1 * math.sin(i), -1.05), p0 + V(0.06, 0.06 * math.sin(i), -1.0)]
        tentacle(k, f"ktent{i}", pts, 0.045 - 0.002 * i, 0.006, "red", "sucker", "chest", (1, 0, 0))
    def crest(k):
        for i in range(7):
            a = -1.2 + i * 0.4; p0 = HC + V(0.18 * math.cos(a) - 0.03, 0.18 * math.sin(a), 0.14)
            taper(k, f"khorn{i}", curve_pts(p0, p0 + V(-0.04, 0.08 * math.sin(a), 0.22 + 0.06 * (1 - abs(a))), V(-0.05, 0, 0), 5), [0.026, 0.02, 0.014, 0.008, 0.002], "gold", "head", 8)
        for s in (1, -1): k.ell(f"keye{s}", tuple(HC + V(0.19, s * 0.06, 0.02)), (0.012, 0.03, 0.016), "yellow", "head", 10)
        k.cone("kbeak", HC + V(0.2, 0, -0.06), HC + V(0.29, 0, -0.12), 0.05, 0.003, "beakk", "head", 10)
        for i in range(4):
            a = -0.6 + i * 0.4; p0 = HC + V(0.18, 0.08 * math.sin(a), -0.12)
            tentacle(k, f"kbeard{i}", curve_pts(p0, p0 + V(0.02, 0.05 * math.sin(a), -0.28), V(0.04, 0, 0), 6), 0.022, 0.004, "red", "sucker", "head", (1, 0, 0))
    helm(k, "blackp", "red", "visor_d", 0.12, crest)
    spear(k, "blackp", "gold", "R", 1.8, tines=3)


def o_goliath(k):   # the Goliath: a grouper's great head for a helm - heavy brow, fat lips, a gaping mouth - on scaled plate
    base(k, "olive", 1.15)
    cuirass(k, "plate", "lip"); pauldrons(k, "plate", "olive", 1.4, 3); arm_armour(k, "plate", "lip", 1.3); leg_armour(k, "plate", "lip"); tassets(k, "plate", "olive")
    for i in range(20): k.ell(f"scale{i}", (0.2 + 0.01 * (i % 2), -0.14 + (i % 5) * 0.07, 1.0 + (i // 5) * 0.07), (0.015, 0.04, 0.035), "pale", "spine", 10)
    boots(k, "plate"); belt(k, "lip", "iron")
    for s, sd in SIDES: k.ell(f"bgaunt{sd}", tuple(k.j(f"palm.{sd}")), (0.1, 0.1, 0.11), "plate", f"hand.{sd}", 18)
    head_shell(k, "ghead", [(-0.22, 0.02, 0.16, 0.17), (-0.08, 0.05, 0.22, 0.22), (0.08, 0.04, 0.23, 0.21), (0.2, 0.0, 0.2, 0.18), (0.28, -0.04, 0.14, 0.13)], "olive")
    k.ell("gtoplip", tuple(HC + V(0.27, 0, 0.0)), (0.06, 0.16, 0.05), "lip", "head", 18)
    k.ell("gbotlip", tuple(HC + V(0.25, 0, -0.14)), (0.07, 0.17, 0.06), "lip", "head", 18)
    k.ell("gmouth", tuple(HC + V(0.26, 0, -0.07)), (0.05, 0.14, 0.05), "black", "head", 18)
    for s in (1, -1):
        eye_ball(k, f"geye{s}", HC + V(0.14, s * 0.19, 0.1), 0.04, "glowy", "black", (0.4, s, 0))
        k.ell(f"gbrow{s}", tuple(HC + V(0.12, s * 0.15, 0.16)), (0.08, 0.06, 0.03), "plate", "head", 12)
    for i in range(7): k.ell(f"hspot{i}", tuple(HC + V(-0.05 + (i % 3) * 0.08, (i // 3 - 1) * 0.15, 0.2)), (0.03, 0.03, 0.012), "pale", "head", 8)
    fin(k, "gfin", [(0, 0), (0.1, 0.18), (0.3, 0.2), (0.45, 0)], HC + V(0.05, 0, 0.18), (-1, 0, 0), (0, 0, 1), 0.03, "olive", "head")
    p, d = held(k, "R"); fa = k.j("forearm.R")
    k.cone("hcannon", fa + V(0.04, 0, -0.02), p + V(0.08, 0, 0.02), 0.07, 0.06, "iron", "forearm.R", 16)
    k.tube("harpoon", [p + V(0.1, 0, 0.03), p + V(0.5, 0, 0.2)], 0.012, "steelg", "forearm.R")
    k.cone("harpoonhead", p + V(0.5, 0, 0.2), p + V(0.6, 0, 0.25), 0.03, 0.001, "steelg", "forearm.R", 6)


def o_nautilus(k):   # Captain Nemo: v2's captain, with a nautilus shell on the shoulder cape and a brass porthole gauntlet
    V2.o_nautilus(k)
    spiral_shell(k, "nshell", (-0.24, -0.12, 1.32), 0.14, 2.2, "shellw", "brass", "chest", (-1, 0.2, 0.1))


def o_fish(k):   # the Herring Rogue: a fish-head hood with gaping mouth and big round eyes, a scaled jerkin, a tail on the coat
    base(k, "silver", 1.05)
    k.ell("jerkin", (0.03, 0, 1.2), (0.2, 0.24, 0.27), "fin", "chest", 28)
    scales(k, "jscale", (0.215, 0, 1.36), 5, 5, 0.075, 0.065, 0.045, "silver", "chest")
    k.tube("scarf", [V(0.15 * math.cos(a), 0.15 * math.sin(a), 1.46) for a in [j * 2 * math.pi / 16 for j in range(17)]], 0.04, "lip", "chest")
    trousers(k, "fin"); boots(k, "leatherx"); gloves(k, "leatherx"); belt(k, "leatherx", "silver")
    head_shell(k, "fhead", [(-0.2, 0.02, 0.13, 0.16), (-0.06, 0.04, 0.17, 0.2), (0.08, 0.03, 0.16, 0.19), (0.2, 0.0, 0.12, 0.14), (0.27, -0.02, 0.07, 0.09)], "silver")
    k.ell("fmouth", tuple(HC + V(0.25, 0, -0.04)), (0.04, 0.07, 0.06), "black", "head", 14)
    k.ell("flip", tuple(HC + V(0.25, 0, -0.1)), (0.05, 0.08, 0.025), "lip", "head", 14)
    for s in (1, -1):
        eye_ball(k, f"feye{s}", HC + V(0.16, s * 0.14, 0.06), 0.05, "white", "black", (0.5, s, 0))
        for g in range(3): k.ell(f"fgill{s}{g}", tuple(HC + V(0.0 - g * 0.04, s * 0.17, -0.04)), (0.006, 0.03, 0.08), "fin", "head", 8)
    fin(k, "fdorsal", [(0, 0), (0.06, 0.16), (0.2, 0.18), (0.32, 0.06), (0.36, 0)], HC + V(0.12, 0, 0.18), (-1, 0, 0), (0, 0, 1), 0.02, "fin", "head")
    fin(k, "ftail", [(0, -0.05), (0.24, -0.26), (0.3, -0.22), (0.18, 0), (0.3, 0.22), (0.24, 0.26), (0, 0.05)], (-0.22, 0, 1.0), (-0.6, 0, -0.8), (0, 1, 0), 0.03, "fin", "pelvis")
    blade(k, "silver", "silver", "R", 0.45, 0.03, 0.05)


def o_gull(k):   # the Gull Rider: a real gull's head for a helmet (white, the yellow beak with its red spot), wing sleeves
    base(k, "grey", 1.04)
    k.ell("jacket", (0.015, 0, 1.25), (0.18, 0.22, 0.22), "leatherb", "chest", 28)
    k.ell("jacketw", (0.01, 0, 1.06), (0.17, 0.2, 0.12), "leatherb", "spine", 24)
    k.ell("fleece", (0.0, 0, 1.45), (0.17, 0.22, 0.08), "white", "chest", 22)
    for s, sd in SIDES:   # the wings: three rows of feathers along each arm, grey over white, black tips
        ua, fa, h = Vector(k.j(f"upperarm.{sd}")), Vector(k.j(f"forearm.{sd}")), Vector(k.j(f"hand.{sd}"))
        for row in range(3):
            for i in range(5):
                t = i / 4.0; p = ua.lerp(h, 0.1 + 0.85 * t) + V(-0.05 - row * 0.03, 0, -0.02)
                L = 0.16 + 0.1 * t + row * 0.05; col = "black" if (row == 2 and i >= 3) else ("grey" if row < 2 else "white")
                fin(k, f"feather{sd}{row}{i}", [(0, -0.03), (L, -0.022), (L + 0.025, 0), (L, 0.022), (0, 0.03)], p + V(0, 0, -0.03), (-0.25, s * 0.15, -1), (h - ua).normalized(), 0.012, col, f"forearm.{sd}" if t > 0.45 else f"upperarm.{sd}")
    for s, sd in SIDES:
        for row, (col, L, w, dz) in enumerate((("white", 0.78, 0.13, 0.0), ("grey", 0.7, 0.12, 0.02), ("black", 0.28, 0.07, -0.55))):
            o = V(-0.17 - row * 0.012, s * (0.13 - row * 0.015), 1.42 + dz)
            fin(k, f"wing{sd}{row}", [(0, -w * 0.4), (L * 0.5, -w), (L, -w * 0.5), (L + 0.06, 0), (L, w * 0.4), (0, w * 0.5)], o, (0.12, s * 0.12, -1), (0.15, s * 0.95, 0.05), 0.025, col, "chest")
        for j in range(3): k.ell(f"wingspot{sd}{j}", (-0.2, s * 0.12, 0.67 - j * 0.05), (0.012, 0.02, 0.018), "white", "chest", 8)
    for i in range(5):   # tail feathers
        a = -0.4 + i * 0.2
        fin(k, f"tail{i}", [(0, -0.03), (0.3, -0.03), (0.32, 0), (0.3, 0.03), (0, 0.03)], (-0.18, 0, 0.98), (-0.5, math.sin(a) * 0.5, -0.8), (0, math.cos(a), math.sin(a) * 0.3), 0.012, "white" if i % 2 else "grey", "pelvis")
    trousers(k, "grey"); boots(k, "yellow"); gloves(k, "leatherb"); belt(k, "leatherb", "brass")
    head_shell(k, "ghead", [(-0.17, 0.03, 0.12, 0.14), (-0.07, 0.05, 0.15, 0.17), (0.05, 0.05, 0.145, 0.16), (0.15, 0.02, 0.11, 0.12), (0.22, -0.01, 0.07, 0.08)], "white")
    k.ell("gcap", tuple(HC + V(-0.06, 0, 0.12)), (0.14, 0.13, 0.07), "grey", "head", 18)
    loft(k, "gbeak", [(0.19, -0.02, 0.055, 0.05), (0.29, -0.03, 0.04, 0.035), (0.38, -0.04, 0.025, 0.028), (0.43, -0.06, 0.014, 0.02), (0.445, -0.085, 0.008, 0.012)], "yellow", "head", 12)
    k.ell("beakspot", tuple(HC + V(0.36, 0, -0.07)), (0.012, 0.022, 0.014), "red", "head", 8)
    for s in (1, -1):
        eye_ball(k, f"geye{s}", HC + V(0.11, s * 0.13, 0.07), 0.026, "yellow", "black", (0.3, s, 0))
        k.ell(f"goggle{s}", tuple(HC + V(0.04, s * 0.09, 0.14)), (0.03, 0.035, 0.035), "visor_g", "head", 14)
    k.tube("gstrap", [HC + V(0.04 + 0.15 * math.cos(a) * 0.0 - 0.0, 0.16 * math.sin(a), 0.13) for a in [j * math.pi / 8 - math.pi / 2 for j in range(9)]], 0.012, "leatherb", "head")
    k.box("holster", (0.06, 0.035, 0.16), (0.03, -0.2, 0.78), "leatherb", "thigh.R", bevel=0.012)
    k.box("flaregun", (0.05, 0.03, 0.07), (0.035, -0.2, 0.88), "red", "thigh.R", bevel=0.01)


def o_mermaid(k):   # the Sea Siren: a scaled tail-gown that ends in a great fluke at the deck, a shell crown, a harp
    V2.o_mermaid(k)
    for s in (1, -1):
        fin(k, f"fluke{s}", [(0, -0.06), (0.32, -0.08), (0.42, 0.02), (0.36, 0.12), (0, 0.06)], (-0.15, s * 0.05, 0.12), (-0.3, s * 0.95, 0.0), (1, 0, 0.1), 0.03, "fin", "thigh.L" if s > 0 else "thigh.R")
    for s, sd in SIDES: scales(k, f"gscale{sd}", (0.205, s * 0.1, 0.86), 6, 3, 0.07, 0.085, 0.04, "fin", f"thigh.{sd}")


def o_orca(k):   # the Orca Commander: an orca's head for a helm (the white eye patch, the grey saddle), the great dorsal
    base(k, "black", 1.05)
    cuirass(k, "black", "white"); pauldrons(k, "black", "white", 1.1, 3); arm_armour(k, "black", "white"); leg_armour(k, "black", "white")
    k.ell("orcabelly", (0.2, 0, 1.15), (0.04, 0.15, 0.22), "white", "chest", 18)
    fin(k, "odorsal", [(0, 0), (0.12, 0.5), (0.18, 0.56), (0.16, 0.3), (0.3, 0)], (-0.12, 0, 1.3), (-1, 0, 0), (0, 0, 1), 0.05, "black", "chest")
    k.ell("saddle", (-0.2, 0, 1.25), (0.05, 0.16, 0.1), "greyp", "chest", 14)
    for s, sd in SIDES:
        fa, h = Vector(k.j(f"forearm.{sd}")), Vector(k.j(f"hand.{sd}"))
        fin(k, f"oflip{sd}", [(0, -0.06), (0.22, -0.09), (0.32, 0.0), (0.22, 0.08), (0, 0.06)], fa, (h - fa).normalized(), V(-1, 0, 0), 0.03, "black", f"forearm.{sd}")
    belt(k, "white", "steelg"); boots(k, "black"); k.mitts("black")
    head_shell(k, "ohead", [(-0.2, 0.02, 0.14, 0.16), (-0.06, 0.04, 0.19, 0.2), (0.08, 0.03, 0.19, 0.19), (0.2, 0.0, 0.15, 0.14), (0.28, -0.03, 0.08, 0.08)], "black")
    loft(k, "ochin", [(-0.04, -0.13, 0.14, 0.05), (0.12, -0.12, 0.14, 0.05), (0.24, -0.08, 0.08, 0.04)], "white", "head", 16)
    for s in (1, -1):
        k.ell(f"opatch{s}", tuple(HC + V(0.08, s * 0.17, 0.07)), (0.08, 0.03, 0.04), "white", "head", 14)
        eye_ball(k, f"oeye{s}", HC + V(0.16, s * 0.15, 0.02), 0.014, "black", "gloss", (0.3, s, 0))
    visor_band(k, "visor_t", -0.04, 0.25, 0.12, 0.025)
    cape(k, "black", "white", 0.9, 0.24, 0.18)
    spear(k, "steelg", "glow", "R", 1.8)


def o_seamine(k):   # Demolitions: v2's suit, the horns with lead caps, a chain from the mine helmet
    V2.o_seamine(k)
    for i in range(12): k.lathe(f"mchain{i}", [(0.02, -0.006), (0.025, 0.0), (0.02, 0.006)], (-0.26 - 0.01 * i, 0.15 - 0.025 * i, 1.48 - 0.04 * i), "iron", "chest", 8, 1.0, 0.5)


def o_divingbell(k): V2.o_divingbell(k)
def o_sack(k): V2.o_sack(k)
def o_sandwich(k): V2.o_sandwich(k)
def o_lifebuoy(k): V2.o_lifebuoy(k)
def o_scarecrow(k): V2.o_scarecrow(k)
def o_souwester(k): V2.o_souwester(k)
def o_pirate(k): V2.o_pirate(k)
def o_barrel(k): V2.o_barrel(k)
def o_gannet(k): V2.o_gannet(k)


EXTRA = {   # (colours the third pass adds to an outfit's palette)
    "lobster": {"shell2": COL(0.42, 0.04, 0.03, 0.4, 0.15), "gloss": COL(0.9, 0.9, 0.9, 0.1)},
    "crab": {"shell2": COL(0.55, 0.2, 0.05, 0.35, 0.1), "gloss": COL(0.9, 0.9, 0.9, 0.1)},
    "pufferfish": {"belly": COL(0.92, 0.88, 0.72, 0.5), "fin": COL(0.85, 0.7, 0.3, 0.4), "lip": COL(0.8, 0.45, 0.35, 0.5)},
    "jelly": {"glass_bell": COL(0.7, 0.5, 0.9, 0.05), "bellcore": COL(0.9, 0.5, 0.8, 0.2)},
    "hermit": {"claw": COL(0.75, 0.3, 0.12, 0.4), "gloss": COL(0.9, 0.9, 0.9, 0.1), "lipp": COL(0.95, 0.55, 0.55, 0.3)},
    "starfish": {"bump": COL(0.98, 0.8, 0.55, 0.6)},
    "barnacle": {"mussel": COL(0.06, 0.07, 0.12, 0.2, 0.3)},
    "shark": {"black": COL(0.02, 0.02, 0.03, 0.3), "gloss": COL(0.9, 0.9, 0.9, 0.1)},
    "octopus": {},
    "turtle": {"beak": COL(0.6, 0.55, 0.35, 0.4), "gloss": COL(0.9, 0.9, 0.9, 0.1)},
    "swordfish": {"sail": COL(0.15, 0.3, 0.75, 0.35)},
    "kraken": {"sucker": COL(0.9, 0.55, 0.55, 0.5), "beakk": COL(0.15, 0.12, 0.1, 0.3)},
    "nautilus": {"shellw": COL(0.92, 0.86, 0.75, 0.4)},
    "fish": {"black": COL(0.02, 0.02, 0.03, 0.3)},
    "gull": {"black": COL(0.03, 0.03, 0.04, 0.6), "white": COL(0.95, 0.95, 0.93, 0.6)},
    "orca": {"greyp": COL(0.55, 0.57, 0.6, 0.4), "gloss": COL(0.9, 0.9, 0.9, 0.1)},
    "mermaid": {},
}
OUTFITS = {}
for name, (fn, cols) in V2.OUTFITS.items():
    mine = globals().get("o_" + name)
    c = dict(cols); c.update(EXTRA.get(name, {}))
    OUTFITS[name] = (mine or fn, c)


def build(name, out):
    fn, cols = OUTFITS[name]
    V2.OUTFITS[name] = (fn, cols)
    V2.build(name, out)


if __name__ == "__main__" or True:
    a = C.args()
    only = a[a.index("--only") + 1].split(",") if "--only" in a else None
    out = C.out_dir()
    for n in OUTFITS:
        if only and n not in only:
            continue
        build(n, out)
