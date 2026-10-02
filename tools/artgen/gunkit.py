# The gun kit (Trawl Visual Overhaul, phase 5): parts, period materials and the conventions every weapon model in
# tools/artgen/weapons_*.py follows, so the game can hold, aim and animate any of them the same way.
#
# Frame: Blender +X toward the muzzle (or a blade's point), +Z up, +Y the weapon's left; metres, true to life.
# The origin is where the right hand's grip closes (the trigger, or a tool's handle).
#
# Moving parts are separate objects whose ORIGIN IS THEIR PIVOT; each carries custom properties the game reads from the
# glb's node extras:  group  (hammer, hammer2, trigger, cylinder, lever, bolt, break, latch, ejector, pump, load, string,
# drum, fins...),  axis  ([x, y, z], Blender axes) and  amount  (radians for kind "rot", metres for "slide";
# "show" parts are visible only when loaded).
# Markers are empties: grip_r (the right fist), grip_l (the left hand on the fore-end), muzzle, eject, sight (the eye
# when aiming down the sights) and the attachment mounts: mount_muzzle, mount_top, mount_under, mount_side.

import os
import math
import bmesh
from mathutils import Vector, Matrix
import common as C
bpy = C.bpy


# ---------------------------------------------------------------- materials (procedural, baked by common.bake_pbr)
def _mat(name, base, rough, metal=0.0):
    return C.mat_flat(name, base, rough=rough, metal=metal)


def mat_rosewood(name="rosewood"):
    m = C.mat_walnut(name)
    nt = m.node_tree
    r = next(n for n in nt.nodes if n.type == 'VALTORGB')
    r.color_ramp.elements[0].color = (0.022, 0.007, 0.004, 1)
    r.color_ramp.elements[-1].color = (0.075, 0.022, 0.012, 1)
    return m


def mat_iron(name="iron"):
    """Dark cast iron, rough, with rust in the crevices."""
    m = bpy.data.materials.new(name)
    nt, b = C._nodes(m)
    n = C._noise(nt, 30, 6)
    r = C._ramp(nt, n.outputs['Fac'], [(0.35, (0.07, 0.07, 0.075, 1)), (0.6, (0.12, 0.11, 0.1, 1)), (0.72, (0.3, 0.14, 0.06, 1))])
    nt.links.new(r.outputs['Color'], b.inputs['Base Color'])
    b.inputs['Metallic'].default_value = 0.85
    b.inputs['Roughness'].default_value = 0.7
    C._bump(nt, b, C._noise(nt, 300, 3).outputs['Fac'], 0.1)
    return m


def mat_silver(name="silver"):
    m = bpy.data.materials.new(name)
    nt, b = C._nodes(m)
    b.inputs['Base Color'].default_value = (0.8, 0.8, 0.78, 1)
    b.inputs['Metallic'].default_value = 1.0
    rr = C._ramp(nt, C._noise(nt, 40, 3).outputs['Fac'], [(0.3, (0.18, 0.18, 0.18, 1)), (0.7, (0.32, 0.32, 0.32, 1))])
    nt.links.new(rr.outputs['Color'], b.inputs['Roughness'])
    # engraved scrolls: a wave pattern in the bump
    w = nt.nodes.new('ShaderNodeTexWave'); w.inputs['Scale'].default_value = 60; w.inputs['Distortion'].default_value = 12
    C._bump(nt, b, w.outputs['Fac'], 0.25, 0.0005)
    return m


def mat_verdigris(name="verdigris brass"):
    """Old brass gone green in the crevices (the Captain's pistol)."""
    m = bpy.data.materials.new(name)
    nt, b = C._nodes(m)
    n = C._noise(nt, 25, 5)
    r = C._ramp(nt, n.outputs['Fac'], [(0.4, (0.72, 0.52, 0.22, 1)), (0.55, (0.5, 0.42, 0.22, 1)), (0.68, (0.2, 0.45, 0.36, 1))])
    nt.links.new(r.outputs['Color'], b.inputs['Base Color'])
    b.inputs['Metallic'].default_value = 0.8
    b.inputs['Roughness'].default_value = 0.45
    return m


def mat_copper(name="copper"):
    return _mat(name, (0.8, 0.38, 0.2), 0.35, 1.0)


def mat_glass(name="glass"):
    return _mat(name, (0.55, 0.62, 0.6), 0.05, 0.0)


def mat_rope(name="rope"):
    m = bpy.data.materials.new(name)
    nt, b = C._nodes(m)
    w = nt.nodes.new('ShaderNodeTexWave'); w.wave_type = 'BANDS'; w.bands_direction = 'DIAGONAL'
    w.inputs['Scale'].default_value = 90; w.inputs['Distortion'].default_value = 2
    r = C._ramp(nt, w.outputs['Fac'], [(0.2, (0.32, 0.26, 0.16, 1)), (0.8, (0.55, 0.46, 0.3, 1))])
    nt.links.new(r.outputs['Color'], b.inputs['Base Color'])
    b.inputs['Roughness'].default_value = 0.9
    C._bump(nt, b, w.outputs['Fac'], 0.5)
    return m


def mat_leather(name="leather"):
    m = bpy.data.materials.new(name)
    nt, b = C._nodes(m)
    r = C._ramp(nt, C._noise(nt, 20, 4).outputs['Fac'], [(0.35, (0.2, 0.11, 0.05, 1)), (0.7, (0.32, 0.18, 0.09, 1))])
    nt.links.new(r.outputs['Color'], b.inputs['Base Color'])
    b.inputs['Roughness'].default_value = 0.6
    C._bump(nt, b, C._noise(nt, 400, 2).outputs['Fac'], 0.15)
    return m


def mat_bone(name="bone"):
    m = bpy.data.materials.new(name)
    nt, b = C._nodes(m)
    r = C._ramp(nt, C._noise(nt, 12, 4).outputs['Fac'], [(0.3, (0.72, 0.66, 0.52, 1)), (0.7, (0.86, 0.82, 0.7, 1))])
    nt.links.new(r.outputs['Color'], b.inputs['Base Color'])
    b.inputs['Roughness'].default_value = 0.5
    return m


def mat_horn(name="horn"):
    m = bpy.data.materials.new(name)
    nt, b = C._nodes(m)
    r = C._ramp(nt, C._noise(nt, 9, 6).outputs['Fac'], [(0.3, (0.05, 0.04, 0.03, 1)), (0.6, (0.22, 0.16, 0.09, 1)), (0.8, (0.4, 0.3, 0.16, 1))])
    nt.links.new(r.outputs['Color'], b.inputs['Base Color'])
    b.inputs['Roughness'].default_value = 0.35
    return m


def mat_rubber(name="gutta-percha"):
    return _mat(name, (0.035, 0.03, 0.028), 0.75)


def mat_obsidian(name="obsidian"):
    return _mat(name, (0.02, 0.02, 0.025), 0.08)


def mat_coral(name="coral"):
    m = bpy.data.materials.new(name)
    nt, b = C._nodes(m)
    v = nt.nodes.new('ShaderNodeTexVoronoi'); v.inputs['Scale'].default_value = 120
    r = C._ramp(nt, v.outputs['Distance'], [(0.1, (0.55, 0.32, 0.3, 1)), (0.4, (0.86, 0.72, 0.64, 1))])
    nt.links.new(r.outputs['Color'], b.inputs['Base Color'])
    b.inputs['Roughness'].default_value = 0.85
    C._bump(nt, b, v.outputs['Distance'], 0.6)
    return m


def mat_laminate(name="laminated wood"):
    """The longbow's stave: light and dark woods glued in stripes along it."""
    m = bpy.data.materials.new(name)
    nt, b = C._nodes(m)
    tc = nt.nodes.new('ShaderNodeTexCoord'); sep = nt.nodes.new('ShaderNodeSeparateXYZ')
    nt.links.new(tc.outputs['Object'], sep.inputs['Vector'])
    w = nt.nodes.new('ShaderNodeTexWave'); w.wave_type = 'BANDS'; w.bands_direction = 'Y'; w.inputs['Scale'].default_value = 3
    nt.links.new(tc.outputs['Object'], w.inputs['Vector'])
    r = C._ramp(nt, w.outputs['Fac'], [(0.45, (0.3, 0.16, 0.07, 1)), (0.55, (0.62, 0.45, 0.25, 1))])
    nt.links.new(r.outputs['Color'], b.inputs['Base Color'])
    b.inputs['Roughness'].default_value = 0.45
    return m


def mat_canvas(name="oilskin"):
    return _mat(name, (0.4, 0.32, 0.12), 0.4)


def materials():
    """Every material a weapon may use, made once per weapon scene."""
    return {
        "steel": C.mat_blued_steel(), "case": C.mat_case_hardened(), "brass": C.mat_brass(), "walnut": C.mat_walnut(),
        "rosewood": mat_rosewood(), "iron": mat_iron(), "silver": mat_silver(), "verdigris": mat_verdigris(),
        "copper": mat_copper(), "glass": mat_glass(), "rope": mat_rope(), "leather": mat_leather(), "bone": mat_bone(),
        "horn": mat_horn(), "rubber": mat_rubber(), "obsidian": mat_obsidian(), "coral": mat_coral(),
        "laminate": mat_laminate(), "canvas": mat_canvas(),
        "bright": _mat("bright steel", (0.62, 0.62, 0.62), 0.3, 1.0), "red": _mat("red paint", (0.5, 0.06, 0.04), 0.5),
        "string": _mat("bowstring", (0.75, 0.7, 0.58), 0.8), "feather": _mat("feather", (0.8, 0.78, 0.7), 0.85),
    }


# ---------------------------------------------------------------- the weapon being built
class Weapon:
    def __init__(self, wid):
        C.reset()
        self.id = wid
        self.parts = []
        self.markers = []
        self.M = materials()

    # -- placing parts
    def add(self, o, mat, group="static", axis=None, amount=0.0, kind="rot", pivot=None, smooth=35):
        """A finished part: its material, modifiers applied, smooth-shaded; a moving part gets its group, axis and
        travel, with its origin moved to the pivot."""
        C.assign(o, self.M[mat] if isinstance(mat, str) else mat)
        C.apply_all(o)
        C.smooth(o, smooth)
        o["group"] = group
        if mat == "glass":
            o["glass"] = 1   # (the game draws glass parts last, blended: the bake can't say which they were)
        if group != "static":
            o["kind"] = kind
            o["axis"] = list(axis) if axis else [0.0, 1.0, 0.0]
            o["amount"] = float(amount)
            if pivot is not None:
                C.select_only([o])
                bpy.context.scene.cursor.location = pivot
                bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
        self.parts.append(o)
        return o

    def mark(self, name, p, d=(1, 0, 0)):
        e = bpy.data.objects.new(name, None)
        C.link(e)
        e.location = p
        e["marker"] = 1
        e["dir"] = list(d)
        self.markers.append(e)
        return e

    # -- shapes
    def box(self, name, size, loc, bevel=0.002, rot=(0, 0, 0), segments=2):
        return C.bevelled_box(name, size, loc=loc, bevel=bevel, rot=rot, segments=segments)

    def cyl(self, name, r, length, loc, axis='X', verts=24, bevel=0.001, hole=0.0):
        rot = (0, math.pi / 2, 0) if axis == 'X' else (math.pi / 2, 0, 0) if axis == 'Y' else (0, 0, 0)
        return C.cylinder(name, r, length, loc=loc, rot=rot, verts=verts, bevel=bevel, hole=hole)

    def barrel(self, name, x0, x1, z, r, bore=0.0, y=0.0, octagonal=False):
        """A barrel from x0 to x1 at height z: bored, its muzzle crowned (a bevel at the bore)."""
        o = self.cyl(name, r, x1 - x0, ((x0 + x1) / 2, y, z), 'X', verts=8 if octagonal else 32, bevel=0.0012, hole=bore)
        return o

    def tube(self, name, pts, r):
        return C.tube_along(name, pts, r)

    def loft(self, name, sections, segs=18):
        return C.lofted(name, sections, segs)

    def ring(self, name, centre, r_major, r_minor, axis='X'):
        bpy.ops.mesh.primitive_torus_add(major_radius=r_major, minor_radius=r_minor, location=centre, major_segments=28, minor_segments=8)
        o = bpy.context.active_object; o.name = name
        if axis == 'X':
            o.rotation_euler = (0, math.pi / 2, 0)
        elif axis == 'Y':
            o.rotation_euler = (math.pi / 2, 0, 0)
        return o

    def sphere(self, name, loc, scale, segs=20):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=segs, ring_count=max(8, segs // 2), radius=1.0, location=loc)
        o = bpy.context.active_object; o.name = name; o.scale = scale
        return o

    def cone(self, name, r0, r1, length, loc, axis='X', verts=24):
        rot = (0, math.pi / 2, 0) if axis == 'X' else (math.pi / 2, 0, 0) if axis == 'Y' else (0, 0, 0)
        bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r0, radius2=r1, depth=length, location=loc, rotation=rot)
        o = bpy.context.active_object; o.name = name
        return o

    def screw(self, name, loc, side=1, r=0.0025):
        """A slotted screw head on a flat side (side +1 the left face, -1 the right)."""
        s = self.cyl(name, r, 0.0016, loc, 'Y', verts=14, bevel=0.0003)
        slot = C.bevelled_box(name + "_slot", (r * 2.4, 0.003, r * 0.35), loc=(loc[0], loc[1] + side * 0.0008, loc[2]), bevel=0)
        b = s.modifiers.new("slot", 'BOOLEAN'); b.object = slot; b.operation = 'DIFFERENCE'
        C.select_only([s]); bpy.ops.object.modifier_apply(modifier=b.name); bpy.data.objects.remove(slot)
        return s

    def chequer(self, o, scale=900, strength=0.35):
        """Chequering on a grip: a diamond pattern in its material's bump (a copy of the material, for this part)."""
        m = o.data.materials[0].copy()
        nt = m.node_tree
        b = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
        tex = nt.nodes.new('ShaderNodeTexChecker'); tex.inputs['Scale'].default_value = scale
        mp = nt.nodes.new('ShaderNodeMapping'); mp.inputs['Rotation'].default_value = (0, 0, math.radians(45))
        tc = nt.nodes.new('ShaderNodeTexCoord')
        nt.links.new(tc.outputs['Object'], mp.inputs['Vector']); nt.links.new(mp.outputs['Vector'], tex.inputs['Vector'])
        C._bump(nt, b, tex.outputs['Fac'], strength, 0.0004)
        o.data.materials[0] = m

    # -- common assemblies
    def sights(self, x_front, x_rear, z, bead=True):
        self.add(self.box("front_sight", (0.006, 0.002, 0.007), (x_front, 0, z + 0.0035), bevel=0.0005), "steel")
        if bead:
            self.add(self.sphere("bead", (x_front, 0, z + 0.0075), (0.0016, 0.0016, 0.0016), 10), "brass")
        rear = self.box("rear_sight", (0.005, 0.012, 0.006), (x_rear, 0, z + 0.003), bevel=0.0008)
        notch = C.bevelled_box("notch", (0.01, 0.0028, 0.004), loc=(x_rear, 0, z + 0.0055), bevel=0)
        b = rear.modifiers.new("n", 'BOOLEAN'); b.object = notch; b.operation = 'DIFFERENCE'
        C.select_only([rear]); bpy.ops.object.modifier_apply(modifier=b.name); bpy.data.objects.remove(notch)
        self.add(rear, "steel")
        self.mark("sight", (x_rear - 0.25, 0, z + 0.006))

    def trigger_and_guard(self, x=0.0, z=-0.012, guard=True, mat="steel"):
        t = self.tube("trigger", [(x + 0.002, 0, z), (x - 0.002, 0, z - 0.012), (x - 0.008, 0, z - 0.02), (x - 0.015, 0, z - 0.022)], 0.0022)
        self.add(t, mat, "trigger", axis=(0, 1, 0), amount=0.22, pivot=(x + 0.002, 0, z))
        if guard:
            pts = [(x + 0.03, 0, z), (x + 0.025, 0, z - 0.022), (x + 0.01, 0, z - 0.03), (x - 0.012, 0, z - 0.031),
                   (x - 0.03, 0, z - 0.024), (x - 0.04, 0, z - 0.008)]
            self.add(self.tube("trigger_guard", pts, 0.0025), mat)

    def finish(self, out, size=1024, merge_static=False):
        """Bakes every part onto one texture set and writes <out>/<id>.glb with the markers and part extras.
        merge_static: the parts that never move are joined into one mesh after the bake (props placed many times: a
        tonic machine of sixty parts is one draw, not sixty); glass parts stay apart (they're drawn in their own pass)."""
        C.bake_pbr(self.parts, self.id, size=size, ao_distance=0.02)
        if merge_static:
            still = [o for o in self.parts if o.get("group", "static") == "static" and not o.get("glass")]
            if len(still) > 1:
                C.select_only(still); bpy.context.view_layer.objects.active = still[0]
                bpy.ops.object.join()
                keep = bpy.context.view_layer.objects.active
                self.parts = [keep] + [o for o in self.parts if o not in still]
        path = os.path.join(out, f"{self.id}.glb")
        C.select_only(self.parts + self.markers)
        bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=True,
                                  export_yup=True, export_texcoords=True, export_normals=True, export_extras=True,
                                  export_materials='EXPORT', export_image_format='JPEG', export_jpeg_quality=88)
        print("artgen: wrote", path)
