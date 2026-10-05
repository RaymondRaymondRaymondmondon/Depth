# Costumes (the user, 2026-10-02: "the character is wearing a costume that makes them look different", after the
# examples in Reference_For_Future_MP_Games/"A Night Off - Arcade Game 6 Design Document.pdf"): whole-figure outfits
# worn over a Trawl sailor or a Red Tide diver, both built on crew.py's skeleton, so one costume fits either. Each is
# its own skinned .glb on the same rig: a padded suit grown round the skeleton (soft, weighted to it) and rigid pieces
# (hoods, shells, fins, claws, hats, props) each on one bone. The game draws it with the figure's own skinning
# matrices, matched by bone name. Flat named materials per costume (never recoloured).
#     blender -b --factory-startup -P tools/artgen/costumes.py -- --out assets/shared/costumes [--only lobster,shark]

import os
import sys
import math
import bmesh
from mathutils import Vector, Matrix
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import common as C
import crew as K
bpy = C.bpy

HC = Vector((0.0, 0, 1.62))   # the head (and the divers' helmet) centre the hoods are built round


class Kit:
    def __init__(self, J, colours):
        self.J = J
        self.parts = []           # (object, bone or None for soft)
        self.mats = {n: C.mat_flat(n, c, rough=r, metal=m) for n, (c, r, m) in colours.items()}

    def add(self, o, mat, bone):
        C.assign(o, self.mats[mat]); C.smooth(o, 45)
        self.parts.append((o, bone)); return o

    def j(self, n):
        return Vector(self.J[n])

    # ---- primitives
    def ell(self, name, c, r, mat, bone, segs=24):
        bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=segs, v_segments=max(8, segs * 2 // 3), radius=1.0)
        bmesh.ops.scale(bm, vec=Vector(r), verts=bm.verts)
        bmesh.ops.translate(bm, vec=Vector(c), verts=bm.verts)
        return self.add(self._obj(name, bm), mat, bone)

    def cone(self, name, a, b, r0, r1, mat, bone, segs=12):
        a, b = Vector(a), Vector(b); d = b - a
        bm = bmesh.new()
        m = Matrix.Translation((a + b) / 2) @ Vector((0, 0, 1)).rotation_difference(d.normalized()).to_matrix().to_4x4()
        bmesh.ops.create_cone(bm, cap_ends=True, segments=segs, radius1=r0, radius2=max(r1, 0.0005), depth=d.length, matrix=m)
        return self.add(self._obj(name, bm), mat, bone)

    def box(self, name, size, c, mat, bone, rot=(0, 0, 0), bevel=0.01):
        o = C.bevelled_box(name, size, loc=c, bevel=bevel, rot=rot); C.apply_all(o)
        return self.add(o, mat, bone)

    def tube(self, name, pts, r, mat, bone):
        o = C.tube_along(name, [tuple(p) for p in pts], r, verts=8)
        return self.add(o, mat, bone)

    def lathe(self, name, prof, c, mat, bone, segs=28, sx=1.0, sy=1.0):
        """A surface of revolution about the vertical through c: prof [(radius, dz), ...] bottom to top."""
        bm = bmesh.new(); rings = []
        for (r, dz) in prof:
            rings.append([bm.verts.new((c[0] + r * sx * math.cos(2 * math.pi * k / segs), c[1] + r * sy * math.sin(2 * math.pi * k / segs), c[2] + dz)) for k in range(segs)])
        for a, b in zip(rings, rings[1:]):
            for k in range(segs):
                bm.faces.new((a[k], a[(k + 1) % segs], b[(k + 1) % segs], b[k]))
        return self.add(self._obj(name, bm), mat, bone)

    def hood(self, name, mat, r=(0.27, 0.255, 0.27), c=None, face=42, bone="head"):
        """A hood over the head (and a diver's helmet) with the face left open."""
        o = self.ell(name, tuple(c or HC + Vector((0.0, 0, 0.01))), r, mat, bone, 32)
        cut(o, [((1, 0, -0.1), face)], Vector(c or HC))
        sol = o.modifiers.new("t", 'SOLIDIFY'); sol.thickness = 0.012; C.select_only([o]); bpy.ops.object.modifier_apply(modifier="t")
        return o

    def suit(self, mat, puff=1.3, extra=0.02, legs=True, arms=True):
        """A padded suit round the skeleton (no hands or head): soft, weighted to the rig."""
        J = self.J; pts, rad, edges, idx = [], [], [], {}
        def P(n, r, pos=None):
            idx[n] = len(pts); pts.append(pos or J[n]); rad.append((r * puff + extra, r * puff + extra))
        def E(a, b):
            edges.append((idx[a], idx[b]))
        P("pelvis", 0.15); P("spine", 0.14); P("chest", 0.17); P("neck", 0.075)
        E("pelvis", "spine"); E("spine", "chest"); E("chest", "neck")
        for sd in ("L", "R"):
            if arms:
                P(f"clavicle.{sd}", 0.085); P(f"upperarm.{sd}", 0.075); P(f"forearm.{sd}", 0.06); P(f"hand.{sd}", 0.045)
                E("chest", f"clavicle.{sd}"); E(f"clavicle.{sd}", f"upperarm.{sd}"); E(f"upperarm.{sd}", f"forearm.{sd}"); E(f"forearm.{sd}", f"hand.{sd}")
            if legs:
                P(f"thigh.{sd}", 0.1); P(f"shin.{sd}", 0.075); P(f"foot.{sd}", 0.065)
                E("pelvis", f"thigh.{sd}"); E(f"thigh.{sd}", f"shin.{sd}"); E(f"shin.{sd}", f"foot.{sd}")
        me = bpy.data.meshes.new("suit"); me.from_pydata(pts, edges, [])
        ob = C.link(bpy.data.objects.new("suit", me))
        ob.modifiers.new("skin", 'SKIN')
        for i, r in enumerate(rad):
            me.skin_vertices[0].data[i].radius = r
        me.skin_vertices[0].data[idx["pelvis"]].use_root = True
        s = ob.modifiers.new("sub", 'SUBSURF'); s.levels = 1
        C.select_only([ob]); bpy.ops.object.modifier_apply(modifier="skin"); bpy.ops.object.modifier_apply(modifier="sub")
        return self.add(ob, mat, None)

    def _obj(self, name, bm):
        me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
        return C.link(bpy.data.objects.new(name, me))

    # ---- common pieces
    def eyes(self, c, sep, r, bone="head", fwd=(1, 0, 0), white="white", dark="black"):
        for s in (1, -1):
            p = Vector(c) + Vector((0, s * sep, 0))
            self.ell(f"eye{s}", tuple(p), (r, r, r), white, bone, 16)
            self.ell(f"pupil{s}", tuple(p + Vector(fwd) * r * 0.8), (r * 0.45, r * 0.45, r * 0.45), dark, bone, 12)

    def along_arm(self, sd, k):
        """A point k of the way down the arm from the upperarm joint past the hand (k 1 = the hand)."""
        a, h = self.j(f"upperarm.{sd}"), self.j(f"hand.{sd}")
        return a + (h - a) * k

    def claw(self, sd, mat, size=1.0):
        s = 1 if sd == "L" else -1
        h = self.j(f"palm.{sd}"); d = Vector((0, s * 0.62, -0.78)).normalized()
        base = h + d * 0.03
        self.ell(f"claw{sd}", tuple(base + d * 0.09 * size), (0.07 * size, 0.06 * size, 0.11 * size), mat, f"hand.{sd}")
        for k, off in ((0, 0.035), (1, -0.035)):
            self.cone(f"pincer{sd}{k}", base + d * 0.15 * size + Vector((off * size, 0, 0)), base + d * 0.3 * size + Vector((off * 0.4 * size, 0, 0)), 0.035 * size, 0.004, mat, f"hand.{sd}")


def cut(o, ports, centre):
    bm = bmesh.new(); bm.from_mesh(o.data)
    dead = [f for f in bm.faces if any((f.calc_center_median() - centre).normalized().dot(Vector(a).normalized()) > math.cos(math.radians(g)) for a, g in ports)]
    bmesh.ops.delete(bm, geom=dead, context='FACES'); bm.to_mesh(o.data); bm.free()


def spikes(k, name, c, r, n, length, mat, bone, rad=0.02):
    """Cones standing out of a sphere's surface (a pufferfish, a mine, a barnacled man)."""
    ga = math.pi * (3 - math.sqrt(5))
    for i in range(n):
        z = 1 - 2 * (i + 0.5) / n; rr = math.sqrt(1 - z * z); a = i * ga
        d = Vector((rr * math.cos(a), rr * math.sin(a), z))
        p = Vector(c) + Vector((d.x * r[0], d.y * r[1], d.z * r[2]))
        k.cone(f"{name}{i}", p, p + d * length, rad, 0.002, mat, bone, 8)


# ---------------------------------------------------------------- the costumes: colours, then the build
COL = lambda r, g, b, rough=0.6, metal=0.0: ((r, g, b), rough, metal)
BASE = {"white": COL(0.9, 0.9, 0.88, 0.3), "black": COL(0.02, 0.02, 0.02, 0.2)}


def c_lobster(k):
    k.suit("shell", 1.35)
    k.claw("L", "shell", 1.3); k.claw("R", "shell", 1.3)
    k.hood("hood", "shell")
    for s in (1, -1):
        k.tube(f"feeler{s}", [HC + Vector((0.18, s * 0.08, 0.15)), HC + Vector((0.35, s * 0.25, 0.45)), HC + Vector((0.2, s * 0.45, 0.7))], 0.01, "shell", "head")
        k.cone(f"stalk{s}", HC + Vector((0.12, s * 0.09, 0.2)), HC + Vector((0.18, s * 0.11, 0.32)), 0.025, 0.02, "shell", "head")
        k.ell(f"seye{s}", tuple(HC + Vector((0.18, s * 0.11, 0.33))), (0.03, 0.03, 0.03), "black", "head")
    for i in range(4):   # the tail: plates down the back, the fan at the seat
        k.ell(f"seg{i}", (-0.2 - 0.02 * i, 0, 1.15 - 0.12 * i), (0.07, 0.16 - 0.02 * i, 0.07), "shell", "pelvis" if i > 1 else "spine")
    for s in (-1, 0, 1):
        k.ell(f"fan{s}", (-0.3, s * 0.08, 0.68), (0.03, 0.06, 0.1), "shell", "pelvis")


def c_crab(k):
    k.suit("shell", 1.3)
    k.ell("carapace", (-0.02, 0, 1.28), (0.24, 0.36, 0.2), "shell", "chest")
    spikes(k, "rim", (-0.02, 0, 1.28), (0.24, 0.36, 0.05), 14, 0.05, "shell", "chest", 0.018)
    k.claw("L", "shell", 1.5); k.claw("R", "shell", 1.1)
    for s in (1, -1):
        k.cone(f"stalk{s}", HC + Vector((0.05, s * 0.07, 0.15)), HC + Vector((0.08, s * 0.1, 0.32)), 0.02, 0.016, "shell", "head")
        k.ell(f"seye{s}", tuple(HC + Vector((0.08, s * 0.1, 0.34))), (0.035, 0.035, 0.035), "black", "head")
        for i in range(3):   # the walking legs out of the sides
            z = 1.18 - 0.08 * i
            k.tube(f"leg{s}{i}", [(0.0, s * 0.3, z), (0.0, s * 0.48, z + 0.1), (0.0, s * 0.56, z - 0.15)], 0.022, "shell", "chest")


def c_pufferfish(k):
    k.ell("body", (0.0, 0, 1.22), (0.36, 0.36, 0.38), "skin", "chest", 32)
    k.ell("belly", (0.12, 0, 1.12), (0.28, 0.3, 0.28), "belly", "chest", 24)
    spikes(k, "sp", (0.0, 0, 1.22), (0.36, 0.36, 0.38), 60, 0.08, "spine_m", "chest", 0.016)
    k.eyes((0.28, 0, 1.4), 0.16, 0.06, "chest")
    k.ell("lips", (0.37, 0, 1.2), (0.05, 0.08, 0.05), "spine_m", "chest")
    for s in (1, -1):
        k.ell(f"fin{s}", (0.0, s * 0.37, 1.2), (0.06, 0.02, 0.1), "spine_m", "chest")
    k.cone("tail", (-0.33, 0, 1.2), (-0.5, 0, 1.2), 0.04, 0.02, "spine_m", "chest")
    k.ell("tailfin", (-0.55, 0, 1.2), (0.05, 0.02, 0.12), "spine_m", "chest")


def c_jelly(k):
    k.lathe("bell", [(0.34, -0.12), (0.33, 0.0), (0.29, 0.12), (0.2, 0.22), (0.08, 0.28), (0.001, 0.29)], tuple(HC + Vector((0, 0, 0.0))), "bell", "head", 32)
    for i in range(10):
        a = 2 * math.pi * i / 10
        p = HC + Vector((0.3 * math.cos(a), 0.3 * math.sin(a), -0.12))
        k.tube(f"tent{i}", [p, p + Vector((0.04 * math.cos(a), 0.04 * math.sin(a), -0.35)), p + Vector((-0.03 * math.cos(a + 1), 0.03, -0.7))], 0.012, "tent", "head")
    for i in range(4):
        a = 2 * math.pi * i / 4 + 0.4
        p = HC + Vector((0.1 * math.cos(a), 0.1 * math.sin(a), -0.1))
        k.tube(f"oral{i}", [p, p + Vector((0.05, 0, -0.3)), p + Vector((0.0, 0.05, -0.55))], 0.03, "bell", "head")


def c_hermit(k):
    k.suit("canvas", 1.2)
    # a big spiral shell on the back: stacked shrinking rings rising and turning
    for i in range(9):
        t = i / 8.0
        r = 0.3 * (1 - t) + 0.04
        a = t * 4.5
        k.ell(f"whorl{i}", (-0.28 - 0.06 * math.cos(a), 0.06 * math.sin(a), 1.0 + 0.55 * t), (r, r * 0.95, r * 0.7), "shell" if i % 2 == 0 else "shell2", "chest")
    k.claw("R", "claw", 1.4)
    for s in (1, -1):
        k.cone(f"stalk{s}", HC + Vector((0.12, s * 0.07, 0.17)), HC + Vector((0.16, s * 0.1, 0.3)), 0.02, 0.016, "claw", "head")
        k.ell(f"seye{s}", tuple(HC + Vector((0.16, s * 0.1, 0.32))), (0.03, 0.03, 0.03), "black", "head")


def c_starfish(k):
    # a five-pointed star round the body: the arms out of a disc at the chest (one up past the head, two out, two down)
    for i in range(5):
        a = math.pi / 2 + 2 * math.pi * i / 5
        d = Vector((0, math.cos(a), math.sin(a)))
        c0 = Vector((0.16, 0, 1.2))
        k.cone(f"arm{i}", c0, c0 + d * 0.62, 0.17, 0.03, "star", "chest", 10)
        for j in range(4):
            k.ell(f"bump{i}{j}", tuple(c0 + d * (0.12 + 0.12 * j) + Vector((0.13 - 0.025 * j, 0, 0))), (0.03, 0.03, 0.03), "bump", "chest", 10)
    k.ell("disc", (0.16, 0, 1.2), (0.14, 0.2, 0.2), "star", "chest")


def c_kelp(k):
    k.suit("kelp", 1.15)
    import random
    rng = random.Random(4)
    for i in range(26):
        a = 2 * math.pi * i / 26
        top = Vector((0.2 * math.cos(a), 0.22 * math.sin(a), 1.45 + 0.25 * rng.random()))
        ln = 0.5 + 0.5 * rng.random()
        p1 = top + Vector((0.12 * math.cos(a), 0.12 * math.sin(a), -ln * 0.5))
        p2 = top + Vector((0.16 * math.cos(a) + 0.05, 0.16 * math.sin(a), -ln))
        k.tube(f"frond{i}", [top, p1, p2], 0.022 + 0.01 * rng.random(), "kelp" if i % 3 else "kelp2", "chest")
    for i in range(5):
        k.ell(f"float{i}", (0.15 * math.cos(i * 1.3), 0.2 * math.sin(i * 1.3), 1.85 + 0.03 * i), (0.04, 0.04, 0.05), "float", "head")


def c_barnacle(k):
    k.suit("skin", 1.2)
    import random
    rng = random.Random(9)
    J = k.J
    for bone, a, b, n, r in (("chest", "spine", "neck", 14, 0.2), ("pelvis", "pelvis", "spine", 8, 0.18), ("upperarm.L", "upperarm.L", "forearm.L", 5, 0.1),
                             ("upperarm.R", "upperarm.R", "forearm.R", 5, 0.1), ("thigh.L", "thigh.L", "shin.L", 5, 0.13), ("thigh.R", "thigh.R", "shin.R", 5, 0.13),
                             ("head", "head", "crown", 6, 0.25)):
        A, B = Vector(J[a]), Vector(J[b])
        for i in range(n):
            t = rng.random(); ang = rng.random() * 2 * math.pi
            d = Vector((math.cos(ang), math.sin(ang), 0))
            p = A + (B - A) * t + d * r
            k.cone(f"bar{bone}{i}", p, p + d * 0.04, 0.03, 0.012, "barn", bone, 8)


def c_shark(k):
    k.suit("grey", 1.3)
    k.ell("belly", (0.1, 0, 1.15), (0.14, 0.2, 0.3), "white", "chest")
    h = k.hood("head", "grey", (0.42, 0.27, 0.27), HC + Vector((0.08, 0, 0.06)), 38)
    k.cone("snout", HC + Vector((0.3, 0, 0.12)), HC + Vector((0.52, 0, 0.08)), 0.15, 0.06, "grey", "head")
    for i in range(7):   # teeth round the open mouth
        a = -0.9 + 1.8 * i / 6
        p = HC + Vector((0.27, 0.17 * math.sin(a), 0.14 - 0.02 * abs(a)))
        k.cone(f"tooth{i}", p, p + Vector((0, 0, -0.06)), 0.016, 0.002, "white", "head", 6)
    k.eyes(HC + Vector((0.2, 0, 0.22)), 0.2, 0.03, "head")
    k.cone("dorsal", (-0.2, 0, 1.3), (-0.4, 0, 1.75), 0.14, 0.02, "grey", "chest", 6)
    k.cone("tail", (-0.25, 0, 0.9), (-0.55, 0, 0.75), 0.1, 0.05, "grey", "pelvis")
    k.cone("caudal_up", (-0.55, 0, 0.75), (-0.75, 0, 1.05), 0.06, 0.01, "grey", "pelvis", 6)
    k.cone("caudal_dn", (-0.55, 0, 0.75), (-0.68, 0, 0.55), 0.05, 0.01, "grey", "pelvis", 6)
    for s in (1, -1):
        k.cone(f"pec{s}", k.along_arm("L" if s > 0 else "R", 0.4), k.along_arm("L" if s > 0 else "R", 0.4) + Vector((-0.12, s * 0.22, -0.12)), 0.08, 0.01, "grey", f"upperarm.{'L' if s > 0 else 'R'}", 6)


def c_octopus(k):
    k.suit("skin", 1.25, legs=False)
    k.ell("mantle", tuple(HC + Vector((-0.14, 0, 0.18))), (0.32, 0.3, 0.4), "skin", "head", 32)
    k.hood("hood", "skin", (0.27, 0.26, 0.26))
    k.eyes(HC + Vector((0.2, 0, 0.12)), 0.17, 0.05, "head")
    for i in range(8):
        a = 2 * math.pi * (i + 0.5) / 8
        d = Vector((math.cos(a), math.sin(a), 0))
        p0 = Vector((0, 0, 0.98)) + d * 0.18
        pts = [p0 + d * (0.08 * j) + Vector((0, 0, -0.17 * j)) + Vector((-d.y, d.x, 0)) * 0.06 * math.sin(j * 1.2 + i) for j in range(6)]
        pts[-1] = pts[-1] + d * 0.1
        k.tube(f"arm{i}", pts, 0.05 - 0.0, "skin", "pelvis")
        for j in range(1, 5):
            k.ell(f"sucker{i}{j}", tuple(pts[j] - d * 0.035), (0.02, 0.02, 0.02), "sucker", "pelvis", 8)


def c_angler(k):
    k.suit("dark", 1.25)
    k.hood("hood", "dark", (0.3, 0.28, 0.3), None, 40)
    for i in range(9):   # the needle teeth round the face
        a = -1.2 + 2.4 * i / 8
        p = HC + Vector((0.24, 0.2 * math.sin(a), -0.06 - 0.03 * math.cos(a)))
        k.cone(f"tooth{i}", p, p + Vector((0.02, 0, 0.08)), 0.014, 0.002, "white", "head", 6)
    k.tube("rod", [HC + Vector((0.02, 0, 0.28)), HC + Vector((0.18, 0, 0.55)), HC + Vector((0.42, 0, 0.6)), HC + Vector((0.55, 0, 0.48))], 0.012, "dark", "head")
    k.ell("lure", tuple(HC + Vector((0.56, 0, 0.42))), (0.06, 0.06, 0.07), "glow", "head")
    for s in (1, -1):
        k.ell(f"fin{s}", (-0.05, s * 0.28, 1.2), (0.12, 0.02, 0.08), "dark", "chest")


def c_turtle(k):
    k.suit("skin", 1.2)
    k.ell("shell", (-0.2, 0, 1.15), (0.24, 0.36, 0.42), "shell", "chest", 32)
    for i in range(7):   # the scutes
        a = (i - 3) * 0.35
        k.ell(f"scute{i}", (-0.4, 0.2 * math.sin(a), 1.15 + 0.22 * math.cos(a) * (1 if i % 2 else -0.6)), (0.04, 0.1, 0.1), "scute", "chest")
    k.ell("plastron", (0.14, 0, 1.1), (0.1, 0.24, 0.32), "belly", "chest")
    for sd in ("L", "R"):
        s = 1 if sd == "L" else -1
        p = k.along_arm(sd, 0.85)
        k.ell(f"flipper{sd}", tuple(p + Vector((0, s * 0.08, -0.08))), (0.08, 0.2, 0.05), "skin", f"forearm.{sd}")
    k.hood("hood", "skin")


def c_ghost(k):
    k.lathe("sheet", [(0.42, -1.48), (0.38, -1.2), (0.3, -0.8), (0.3, -0.35), (0.3, -0.2), (0.27, 0.0), (0.22, 0.14), (0.12, 0.24), (0.001, 0.27)],
            tuple(HC), "sheet", None, 32)
    for s in (1, -1):
        k.ell(f"hole{s}", tuple(HC + Vector((0.27, s * 0.09, 0.03))), (0.02, 0.045, 0.06), "black", "head")
    for i in range(12):   # a ragged hem
        a = 2 * math.pi * i / 12
        p = HC + Vector((0.42 * math.cos(a), 0.42 * math.sin(a), -1.48))
        k.cone(f"rag{i}", p, p + Vector((0, 0, -0.1 - 0.06 * (i % 3))), 0.07, 0.01, "sheet", "pelvis", 6)


def c_coral(k):
    k.suit("rock", 1.15)
    import random
    rng = random.Random(3)
    for i, (bone, base) in enumerate((("head", HC + Vector((0, 0, 0.22))), ("chest", Vector((0, 0.22, 1.42))), ("chest", Vector((0, -0.22, 1.42))),
                                      ("head", HC + Vector((-0.1, 0.14, 0.15))), ("chest", Vector((-0.15, 0.1, 1.3))))):
        mat = ("coral1", "coral2", "coral3")[i % 3]
        for j in range(6):
            d = Vector((rng.uniform(-0.4, 0.4), rng.uniform(-0.6, 0.6), 1)).normalized()
            p1 = base + d * rng.uniform(0.12, 0.22)
            k.cone(f"br{i}{j}", base, p1, 0.035, 0.018, mat, bone, 8)
            k.ell(f"tip{i}{j}", tuple(p1), (0.022, 0.022, 0.022), mat, bone, 8)
    k.ell("brain", (0.05, 0.12, 1.0), (0.12, 0.1, 0.1), "coral3", "pelvis")


def c_divingbell(k):
    k.lathe("bell", [(0.46, -0.7), (0.45, -0.5), (0.4, -0.1), (0.33, 0.15), (0.2, 0.28), (0.001, 0.31)], tuple(HC + Vector((0, 0, 0.04))), "brass", "chest", 36)
    k.lathe("rim", [(0.49, -0.74), (0.49, -0.66), (0.44, -0.66)], tuple(HC + Vector((0, 0, 0.04))), "brass_d", "chest", 36)
    port = HC + Vector((0.39, 0, -0.08))
    k.ell("port", tuple(port), (0.03, 0.12, 0.12), "glass", "chest")
    k.lathe("portrim", [(0.13, -0.02), (0.13, 0.02)], (0, 0, 0), "brass_d", "chest", 24)
    o = k.parts[-1][0]; o.rotation_euler = (0, math.pi / 2, 0); o.location = port; C.apply_all(o)
    C.select_only([o]); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    k.ell("lug", tuple(HC + Vector((0, 0, 0.36))), (0.05, 0.02, 0.05), "brass_d", "chest")
    k.tube("chain", [HC + Vector((0, 0, 0.4)), HC + Vector((0, 0, 0.8))], 0.012, "iron", "chest")


def c_seamine(k):
    k.ell("mine", (0.0, 0, 1.2), (0.38, 0.38, 0.38), "iron", "chest", 32)
    spikes(k, "horn", (0.0, 0, 1.2), (0.38, 0.38, 0.38), 18, 0.12, "brass", "chest", 0.03)
    k.tube("mchain", [(0.0, 0, 0.82), (0.0, 0.03, 0.4), (0.02, 0, 0.05)], 0.014, "iron", "pelvis")


def c_swordfish(k):
    k.suit("blue", 1.25)
    k.ell("belly", (0.1, 0, 1.15), (0.14, 0.2, 0.3), "silver", "chest")
    k.hood("head", "blue", (0.34, 0.26, 0.27), HC + Vector((0.04, 0, 0.04)), 38)
    k.cone("bill", HC + Vector((0.3, 0, 0.12)), HC + Vector((1.0, 0, 0.12)), 0.04, 0.004, "blue", "head")
    k.eyes(HC + Vector((0.2, 0, 0.2)), 0.2, 0.035, "head")
    k.cone("sail", (-0.1, 0, 1.4), (-0.45, 0, 1.95), 0.2, 0.02, "blue", "chest", 6)
    k.cone("tail", (-0.25, 0, 0.9), (-0.45, 0, 0.75), 0.08, 0.04, "blue", "pelvis")
    for s in (1, -1):
        k.cone(f"lobe{s}", (-0.45, 0, 0.75), (-0.62, 0, 0.75 + s * 0.28), 0.05, 0.008, "blue", "pelvis", 6)


def c_kraken(k):
    k.suit("red", 1.25)
    k.ell("mantle", tuple(HC + Vector((-0.12, 0, 0.25))), (0.4, 0.36, 0.5), "red", "head", 32)
    k.hood("hood", "red", (0.28, 0.27, 0.27))
    k.eyes(HC + Vector((0.18, 0, 0.18)), 0.2, 0.06, "head", white="yellow")
    for i in range(6):   # the arms draping down over the shoulders and the back
        a = math.pi * 0.25 + math.pi * 1.5 * i / 5
        d = Vector((math.cos(a), math.sin(a), 0))
        pts = [HC + d * 0.25 + Vector((0, 0, -0.05)), HC + d * 0.36 + Vector((0, 0, -0.3)), HC + d * 0.34 + Vector((0, 0, -0.6)), HC + d * 0.4 + Vector((0, 0, -0.85))]
        k.tube(f"arm{i}", pts, 0.05, "red", "chest")


def c_goliath(k):
    k.suit("olive", 1.4)
    k.ell("belly", (0.12, 0, 1.1), (0.15, 0.24, 0.32), "pale", "chest")
    k.hood("head", "olive", (0.36, 0.32, 0.32), HC + Vector((0.04, 0, 0.0)), 34)
    k.ell("lip_up", tuple(HC + Vector((0.3, 0, 0.1))), (0.08, 0.2, 0.06), "lip", "head")
    k.ell("lip_dn", tuple(HC + Vector((0.3, 0, -0.17))), (0.1, 0.22, 0.07), "lip", "head")
    k.eyes(HC + Vector((0.16, 0, 0.26)), 0.24, 0.05, "head")
    for i in range(3):   # the armour plates and an old harpoon in it
        k.ell(f"plate{i}", (0.0, 0.3, 1.32 - 0.14 * i), (0.12, 0.03, 0.06), "plate", "chest")
    k.tube("harpoon", [(-0.3, 0.1, 1.35), (0.05, 0.25, 1.5)], 0.01, "iron", "chest")
    k.cone("dorsal", (-0.15, 0, 1.35), (-0.35, 0, 1.75), 0.15, 0.03, "olive", "chest", 6)
    k.ell("tail", (-0.4, 0, 0.8), (0.06, 0.03, 0.2), "olive", "pelvis")


def c_nautilus(k):
    # the submarine round the waist: a long riveted hull, a conning tower on the chest, a propeller astern; the
    # periscope as a hat
    k.ell("hull", (0.0, 0, 1.0), (0.75, 0.3, 0.22), "steel", "pelvis", 32)
    k.ell("ram", (0.75, 0, 1.0), (0.12, 0.05, 0.05), "steel_d", "pelvis")
    k.box("tower", (0.3, 0.2, 0.22), (0.05, 0, 1.25), "steel", "spine")
    for s in (1, -1):
        for i in range(4):
            k.ell(f"win{s}{i}", (0.4 - 0.22 * i, s * 0.27, 1.02), (0.035, 0.02, 0.035), "glow", "pelvis", 12)
    for i in range(4):
        a = i * math.pi / 2 + 0.4
        k.ell(f"blade{i}", (-0.8, 0.08 * math.cos(a), 1.0 + 0.08 * math.sin(a)), (0.012, 0.04 * abs(math.cos(a)) + 0.012, 0.04 * abs(math.sin(a)) + 0.012), "brass", "pelvis")
    k.tube("scope", [HC + Vector((0, 0, 0.2)), HC + Vector((0, 0, 0.6)), HC + Vector((0.1, 0, 0.65))], 0.025, "steel_d", "head")
    k.box("scopehead", (0.08, 0.06, 0.06), tuple(HC + Vector((0.12, 0, 0.65))), "steel_d", "head")


def c_fish(k):
    k.suit("silver", 1.3)
    k.hood("head", "silver", (0.34, 0.27, 0.3), HC + Vector((0.04, 0, 0.03)), 38)
    k.ell("mouth", tuple(HC + Vector((0.33, 0, -0.02))), (0.04, 0.12, 0.05), "lip", "head")
    k.eyes(HC + Vector((0.18, 0, 0.2)), 0.21, 0.06, "head")
    k.cone("dorsal", (-0.15, 0, 1.4), (-0.3, 0, 1.68), 0.13, 0.02, "fin", "chest", 6)
    for s in (1, -1):
        k.cone(f"tailfin{s}", (-0.3, 0, 0.85), (-0.55, 0, 0.85 + s * 0.25), 0.07, 0.01, "fin", "pelvis", 6)


def c_gull(k):
    k.suit("white", 1.3)
    k.ell("cap", tuple(HC + Vector((0.0, 0, 0.12))), (0.25, 0.24, 0.2), "white", "head")
    k.cone("beak", HC + Vector((0.2, 0, 0.16)), HC + Vector((0.42, 0, 0.12)), 0.06, 0.01, "yellow", "head")
    k.ell("redspot", tuple(HC + Vector((0.36, 0, 0.11))), (0.015, 0.015, 0.015), "red", "head")
    k.eyes(HC + Vector((0.13, 0, 0.24)), 0.15, 0.03, "head")
    for sd in ("L", "R"):
        s = 1 if sd == "L" else -1
        a, h = k.j(f"upperarm.{sd}"), k.j(f"hand.{sd}")
        mid = (a + h) / 2
        o = k.box(f"wing{sd}", (0.06, 0.03, 0.5), tuple(mid + Vector((-0.12, s * 0.03, 0))), "grey", f"forearm.{sd}", rot=(s * 0.6, 0, 0))
        k.box(f"tips{sd}", (0.05, 0.03, 0.2), tuple(h + Vector((-0.15, s * 0.05, -0.1))), "black", f"hand.{sd}", rot=(s * 0.6, 0, 0))
    for s in (-1, 0, 1):
        k.box(f"tail{s}", (0.18, 0.06, 0.03), (-0.3, s * 0.06, 0.9), "white", "pelvis", rot=(0, -0.4, 0))


def c_sack(k):
    k.lathe("sack", [(0.36, -1.15), (0.4, -0.9), (0.4, -0.5), (0.36, -0.15), (0.3, 0.05), (0.22, 0.2), (0.14, 0.28), (0.001, 0.3)], tuple(HC), "sack", None, 28)
    for s in (1, -1):
        k.ell(f"hole{s}", tuple(HC + Vector((0.29, s * 0.09, 0.02))), (0.02, 0.04, 0.035), "black", "head")
    k.ell("mouthhole", tuple(HC + Vector((0.3, 0, -0.1))), (0.02, 0.06, 0.02), "black", "head")
    k.lathe("tie", [(0.37, -1.1), (0.37, -1.06)], tuple(HC), "rope", "pelvis", 28)
    k.ell("patch", tuple(HC + Vector((0.3, 0.12, -0.55))), (0.03, 0.08, 0.08), "patch", "chest")


def c_sandwich(k):
    for s, x in ((1, 0.2), (-1, -0.2)):
        k.box(f"board{s}", (0.03, 0.5, 0.75), (x, 0, 1.08), "board", "chest", bevel=0.005)
        for i in range(3):
            k.box(f"stripe{s}{i}", (0.035, 0.36, 0.06), (x, 0, 1.3 - 0.18 * i), "paint" if i != 1 else "paint2", "chest", bevel=0.002)
        k.box(f"fishsign{s}", (0.036, 0.2, 0.08), (x, 0, 0.82), "paint2", "chest", bevel=0.002)
    for s in (1, -1):
        k.tube(f"strap{s}", [(0.2, s * 0.18, 1.45), (0.0, s * 0.18, 1.52), (-0.2, s * 0.18, 1.45)], 0.012, "rope", "chest")
    k.lathe("bowler", [(0.15, 0.0), (0.15, 0.01), (0.11, 0.02), (0.11, 0.1), (0.08, 0.14), (0.001, 0.15)], tuple(HC + Vector((0, 0, 0.17))), "black", "head", 24)


def c_lifebuoy(k):
    k.parts.append((K.tube_ring("buoy", (0.0, 0, 1.0), 0.3, 0.075, "red", k.mats), "pelvis"))
    for i in range(4):
        a = i * math.pi / 2 + math.pi / 4
        k.ell(f"band{i}", (0.3 * math.cos(a), 0.3 * math.sin(a), 1.0), (0.06, 0.06, 0.082), "white", "pelvis")
    k.lathe("cap", [(0.16, 0.0), (0.17, 0.03), (0.15, 0.08), (0.001, 0.09)], tuple(HC + Vector((0, 0, 0.17))), "white", "head", 24)
    k.box("visor", (0.08, 0.2, 0.012), tuple(HC + Vector((0.16, 0, 0.17))), "black", "head")


def c_scarecrow(k):
    k.suit("sack", 1.25)
    k.lathe("hat", [(0.34, 0.0), (0.34, 0.015), (0.15, 0.02), (0.14, 0.18), (0.001, 0.2)], tuple(HC + Vector((0, 0, 0.14))), "straw", "head", 28)
    import random
    rng = random.Random(2)
    for sd in ("L", "R"):
        h = k.j(f"hand.{sd}")
        for i in range(6):
            d = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), -1.5)).normalized()
            k.cone(f"straw{sd}{i}", h + Vector((0, 0, 0.03)), h + d * 0.12, 0.01, 0.002, "straw", f"forearm.{sd}", 5)
    for i in range(8):
        d = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), 0.3)).normalized()
        k.cone(f"neck{i}", Vector((0, 0, 1.45)), Vector((0, 0, 1.45)) + d * 0.14, 0.012, 0.002, "straw", "chest", 5)
    k.box("patch1", (0.02, 0.1, 0.1), (0.2, 0.08, 1.25), "patch", "chest")


def c_souwester(k):
    k.lathe("hat", [(0.55, -0.12), (0.5, -0.06), (0.3, 0.0), (0.24, 0.12), (0.18, 0.24), (0.001, 0.28)], tuple(HC + Vector((-0.04, 0, 0.12))), "oil", "head", 32)
    k.lathe("cape", [(0.5, -0.75), (0.4, -0.45), (0.28, -0.22), (0.2, -0.16)], tuple(HC), "oil", "chest", 32)
    k.tube("tie", [HC + Vector((0.12, 0.12, 0.0)), HC + Vector((0.16, 0, -0.16)), HC + Vector((0.12, -0.12, 0.0))], 0.008, "rope", "head")


def c_mermaid(k):
    k.suit("top", 1.15, legs=False)
    k.lathe("tail", [(0.06, -0.92), (0.12, -0.8), (0.18, -0.5), (0.2, -0.2), (0.21, 0.0), (0.19, 0.12)], (0.0, 0, 0.92), "scale", "pelvis", 28, sx=0.8)
    for s in (1, -1):
        k.cone(f"fluke{s}", (0.02, 0, 0.04), (0.12, s * 0.32, 0.0), 0.06, 0.01, "fin", "pelvis", 6)
    k.ell("shell1", (0.15, 0.08, 1.3), (0.03, 0.07, 0.06), "shell", "chest"); k.ell("shell2", (0.15, -0.08, 1.3), (0.03, 0.07, 0.06), "shell", "chest")
    k.lathe("crown", [(0.15, 0.0), (0.15, 0.06)], tuple(HC + Vector((0, 0, 0.2))), "gold", "head", 18)
    for i in range(6):
        a = 2 * math.pi * i / 6
        k.cone(f"pt{i}", HC + Vector((0.15 * math.cos(a), 0.15 * math.sin(a), 0.26)), HC + Vector((0.15 * math.cos(a), 0.15 * math.sin(a), 0.33)), 0.02, 0.002, "gold", "head", 5)


def c_pirate(k):
    k.lathe("coat", [(0.4, -0.95), (0.33, -0.6), (0.27, -0.35), (0.25, 0.0)], (0.0, 0, 1.15), "coat", None, 32)
    k.suit("coat", 1.15, legs=False)
    k.ell("tricorn", tuple(HC + Vector((0, 0, 0.18))), (0.3, 0.3, 0.08), "black", "head", 3)
    k.ell("crowntop", tuple(HC + Vector((0, 0, 0.23))), (0.16, 0.16, 0.08), "black", "head")
    k.ell("patch", tuple(HC + Vector((0.2, 0.08, 0.06))), (0.02, 0.05, 0.04), "black", "head")
    k.tube("patchcord", [HC + Vector((0.18, 0.2, 0.1)), HC + Vector((0.2, 0.0, 0.08)), HC + Vector((0.16, -0.2, 0.14))], 0.006, "black", "head")
    k.lathe("belt", [(0.24, -0.03), (0.24, 0.03)], (0.0, 0, 0.98), "leather", "pelvis", 28)
    k.box("buckle", (0.02, 0.07, 0.07), (0.24, 0, 0.98), "gold", "pelvis")
    for i in range(4):
        k.ell(f"button{i}", (0.24, 0.1, 1.38 - 0.1 * i), (0.015, 0.015, 0.015), "gold", "chest", 8)


def c_barrel(k):
    k.lathe("barrel", [(0.3, -0.55), (0.36, -0.3), (0.38, 0.0), (0.36, 0.3), (0.3, 0.55)], (0.0, 0, 1.05), "wood", "spine", 28)
    for z in (0.58, 0.82, 1.28, 1.52):
        r = 0.38 - 0.08 * (abs(z - 1.05) / 0.5) ** 2 + 0.006
        k.lathe(f"hoop{z}", [(r, -0.02), (r, 0.02)], (0.0, 0, z), "iron", "spine", 28)
    for s in (1, -1):
        k.tube(f"strap{s}", [(0.2, s * 0.18, 1.58), (0.0, s * 0.18, 1.5), (-0.2, s * 0.18, 1.58)], 0.015, "leather", "chest")


def c_gannet(k):
    # the Gannet herself round the waist: a hull with her wheelhouse on the chest, the funnel as a hat
    k.lathe("hull", [(0.2, -0.12), (0.4, 0.0), (0.46, 0.14)], (0.0, 0, 0.92), "hull", "pelvis", 28, sx=1.6, sy=0.9)
    k.lathe("bulwark", [(0.46, 0.14), (0.47, 0.2)], (0.0, 0, 0.92), "trim", "pelvis", 28, sx=1.6, sy=0.9)
    k.box("wheelhouse", (0.22, 0.3, 0.2), (0.12, 0, 1.25), "white", "chest")
    k.box("roof", (0.26, 0.34, 0.03), (0.12, 0, 1.36), "trim", "chest")
    for s in (1, -1):
        k.box(f"window{s}", (0.01, 0.08, 0.06), (0.235, s * 0.07, 1.28), "glow", "chest")
    k.lathe("funnel", [(0.09, 0.0), (0.09, 0.22)], tuple(HC + Vector((-0.02, 0, 0.18))), "red", "head", 18)
    k.lathe("band", [(0.092, 0.16), (0.092, 0.2)], tuple(HC + Vector((-0.02, 0, 0.18))), "black", "head", 18)
    k.tube("mast", [(0.55, 0, 1.0), (0.6, 0, 1.6)], 0.012, "wood", "pelvis")


def c_orca(k):
    k.suit("black", 1.3)
    k.ell("belly", (0.12, 0, 1.12), (0.14, 0.22, 0.32), "white", "chest")
    k.hood("head", "black", (0.32, 0.28, 0.3), HC + Vector((0.03, 0, 0.03)), 38)
    for s in (1, -1):
        k.ell(f"patch{s}", tuple(HC + Vector((0.12, s * 0.22, 0.14))), (0.08, 0.03, 0.04), "white", "head")
    k.cone("dorsal", (-0.15, 0, 1.4), (-0.2, 0, 2.0), 0.13, 0.02, "black", "chest", 6)
    for s in (1, -1):
        k.cone(f"fluke{s}", (-0.3, 0, 0.85), (-0.5, s * 0.3, 0.85), 0.07, 0.01, "black", "pelvis", 6)
    # three balloon orcas on strings from the right hand
    h = k.j("hand.R")
    for i, (dx, dy, dz) in enumerate(((0.1, -0.1, 1.0), (-0.05, -0.2, 1.15), (0.0, 0.05, 1.25))):
        top = h + Vector((dx, dy, dz))
        k.tube(f"string{i}", [h, top], 0.003, "white", "hand.R")
        k.ell(f"balloon{i}", tuple(top + Vector((0, 0, 0.08))), (0.14, 0.06, 0.07), "black", "hand.R", 16)
        k.ell(f"bbelly{i}", tuple(top + Vector((0.02, 0, 0.05))), (0.1, 0.045, 0.035), "white", "hand.R", 12)
        k.cone(f"bfin{i}", top + Vector((-0.02, 0, 0.13)), top + Vector((-0.04, 0, 0.2)), 0.025, 0.004, "black", "hand.R", 6)


COSTUMES = {
    "lobster": (c_lobster, {"shell": COL(0.62, 0.08, 0.04, 0.45)}),
    "crab": (c_crab, {"shell": COL(0.75, 0.32, 0.08, 0.5)}),
    "pufferfish": (c_pufferfish, {"skin": COL(0.62, 0.5, 0.2, 0.6), "belly": COL(0.85, 0.82, 0.7), "spine_m": COL(0.4, 0.3, 0.12)}),
    "jelly": (c_jelly, {"bell": COL(0.8, 0.45, 0.7, 0.2), "tent": COL(0.9, 0.7, 0.85, 0.3)}),
    "hermit": (c_hermit, {"canvas": COL(0.5, 0.2, 0.15), "shell": COL(0.85, 0.72, 0.55, 0.4), "shell2": COL(0.7, 0.5, 0.35, 0.4), "claw": COL(0.7, 0.25, 0.1)}),
    "starfish": (c_starfish, {"star": COL(0.85, 0.4, 0.12), "bump": COL(0.95, 0.75, 0.5)}),
    "kelp": (c_kelp, {"kelp": COL(0.2, 0.3, 0.08), "kelp2": COL(0.35, 0.33, 0.1), "float": COL(0.5, 0.42, 0.12)}),
    "barnacle": (c_barnacle, {"skin": COL(0.35, 0.33, 0.3), "barn": COL(0.8, 0.78, 0.7, 0.8)}),
    "shark": (c_shark, {"grey": COL(0.32, 0.36, 0.4, 0.45)}),
    "octopus": (c_octopus, {"skin": COL(0.45, 0.15, 0.45, 0.4), "sucker": COL(0.85, 0.6, 0.75)}),
    "angler": (c_angler, {"dark": COL(0.08, 0.07, 0.08, 0.5), "glow": COL(1.0, 0.95, 0.6, 0.2)}),
    "turtle": (c_turtle, {"skin": COL(0.35, 0.45, 0.25), "shell": COL(0.35, 0.25, 0.1, 0.4), "scute": COL(0.5, 0.38, 0.15, 0.4), "belly": COL(0.8, 0.75, 0.5)}),
    "ghost": (c_ghost, {"sheet": COL(0.82, 0.86, 0.88, 0.7)}),
    "coral": (c_coral, {"rock": COL(0.4, 0.35, 0.3), "coral1": COL(0.9, 0.3, 0.45), "coral2": COL(0.95, 0.55, 0.15), "coral3": COL(0.55, 0.3, 0.75)}),
    "divingbell": (c_divingbell, {"brass": COL(0.75, 0.55, 0.22, 0.3, 0.9), "brass_d": COL(0.5, 0.35, 0.15, 0.35, 0.9), "glass": COL(0.4, 0.6, 0.6, 0.05), "iron": COL(0.2, 0.2, 0.2, 0.5, 0.8)}),
    "seamine": (c_seamine, {"iron": COL(0.12, 0.12, 0.13, 0.5, 0.8), "brass": COL(0.7, 0.5, 0.2, 0.3, 0.9)}),
    "swordfish": (c_swordfish, {"blue": COL(0.08, 0.15, 0.35, 0.35), "silver": COL(0.75, 0.78, 0.82, 0.3)}),
    "kraken": (c_kraken, {"red": COL(0.45, 0.05, 0.05, 0.4), "yellow": COL(0.95, 0.8, 0.2, 0.3)}),
    "goliath": (c_goliath, {"olive": COL(0.3, 0.3, 0.15), "pale": COL(0.75, 0.72, 0.55), "lip": COL(0.45, 0.35, 0.2), "plate": COL(0.4, 0.38, 0.35, 0.5, 0.6), "iron": COL(0.25, 0.22, 0.2, 0.5, 0.8)}),
    "nautilus": (c_nautilus, {"steel": COL(0.3, 0.32, 0.3, 0.4, 0.8), "steel_d": COL(0.18, 0.18, 0.18, 0.4, 0.8), "glow": COL(1.0, 0.85, 0.5, 0.2), "brass": COL(0.75, 0.55, 0.22, 0.3, 0.9)}),
    "fish": (c_fish, {"silver": COL(0.55, 0.62, 0.68, 0.35), "fin": COL(0.75, 0.45, 0.2), "lip": COL(0.8, 0.5, 0.45)}),
    "gull": (c_gull, {"grey": COL(0.6, 0.62, 0.66), "yellow": COL(0.95, 0.75, 0.15), "red": COL(0.8, 0.1, 0.05)}),
    "sack": (c_sack, {"sack": COL(0.55, 0.42, 0.25, 0.95), "rope": COL(0.45, 0.35, 0.2), "patch": COL(0.4, 0.3, 0.2, 0.95)}),
    "sandwich": (c_sandwich, {"board": COL(0.8, 0.75, 0.6), "paint": COL(0.6, 0.1, 0.08), "paint2": COL(0.1, 0.2, 0.45), "rope": COL(0.45, 0.35, 0.2)}),
    "lifebuoy": (c_lifebuoy, {"red": COL(0.8, 0.12, 0.08, 0.5)}),
    "scarecrow": (c_scarecrow, {"sack": COL(0.5, 0.4, 0.25, 0.95), "straw": COL(0.85, 0.7, 0.3, 0.9), "patch": COL(0.3, 0.35, 0.5, 0.95)}),
    "souwester": (c_souwester, {"oil": COL(0.9, 0.7, 0.05, 0.3), "rope": COL(0.45, 0.35, 0.2)}),
    "mermaid": (c_mermaid, {"top": COL(0.2, 0.5, 0.45), "scale": COL(0.1, 0.55, 0.5, 0.3), "fin": COL(0.3, 0.75, 0.65, 0.3), "shell": COL(0.9, 0.7, 0.75), "gold": COL(0.9, 0.7, 0.2, 0.3, 0.9)}),
    "pirate": (c_pirate, {"coat": COL(0.35, 0.05, 0.06), "leather": COL(0.25, 0.15, 0.08), "gold": COL(0.9, 0.7, 0.2, 0.3, 0.9)}),
    "barrel": (c_barrel, {"wood": COL(0.4, 0.25, 0.12, 0.8), "iron": COL(0.2, 0.2, 0.2, 0.5, 0.8), "leather": COL(0.25, 0.15, 0.08)}),
    "gannet": (c_gannet, {"hull": COL(0.12, 0.18, 0.3), "trim": COL(0.6, 0.12, 0.08), "wood": COL(0.4, 0.25, 0.12), "glow": COL(1.0, 0.85, 0.5, 0.2), "red": COL(0.7, 0.1, 0.06)}),
    "orca": (c_orca, {}),
}


def build(name, out):
    C.reset()
    J = K.joints()
    fn, cols = COSTUMES[name]
    allc = dict(BASE); allc.update(cols)
    k = Kit(J, allc)
    rig = K.make_armature(J)
    fn(k)
    meshes = []
    for o, bone in k.parts:
        C.select_only([o]); bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        if bone:
            g = o.vertex_groups.new(name=bone); g.add(list(range(len(o.data.vertices))), 1.0, 'REPLACE')
            mod = o.modifiers.new("rig", 'ARMATURE'); mod.object = rig; o.parent = rig
        else:
            C.select_only([o, rig]); bpy.context.view_layer.objects.active = rig
            bpy.ops.object.parent_set(type='ARMATURE_AUTO')
        o.data.color_attributes.new("Col", 'BYTE_COLOR', 'CORNER')
        o.data.color_attributes.active_color = o.data.color_attributes["Col"]
        for d in o.data.color_attributes["Col"].data:
            d.color = (1, 1, 1, 1)
        meshes.append(o)
    path = os.path.join(out, f"costume_{name}.glb")
    C.select_only(meshes + [rig])
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=False,
                              export_yup=True, export_texcoords=False, export_normals=True, export_skins=True,
                              export_animations=False, export_materials='EXPORT', export_vertex_color='ACTIVE',
                              export_all_influences=False)
    print("artgen: wrote", path)


if not os.environ.get("ARTGEN_IMPORT"):   # (noclip_crew.py imports the kit without building every costume)
    a = C.args()
    only = a[a.index("--only") + 1].split(",") if "--only" in a else None
    out = C.out_dir()
    for n in COSTUMES:
        if only and n not in only:
            continue
        build(n, out)
