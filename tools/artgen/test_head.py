# Phase 1 test head (the Trawl Visual Overhaul Spec, "Faces"): a stylised weathered sailor's head with real facial
# structure (brow ridge, eye sockets, a nose with a bridge and nostrils, cheekbones, lips, jaw, chin, ears), eyeballs
# as separate meshes with an iris and a wet highlight, a neck, and a short sculpted hair shell; skin with wind-reddened
# nose and cheeks; baked to one PBR texture set.
#
# Frame: Blender +Z up, the face looks along -Y (glTF: +Y up, the face toward +Z). The origin is at the base of the
# skull, so the head sits on a neck joint at 0. Metres; the head is about 23 cm tall.
#     blender -b --factory-startup -P tools/artgen/test_head.py -- --out assets/shared/test

import os
import sys
import math
import bmesh
from mathutils import Vector
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import common as C

sc = C.reset()
skin = C.mat_skin(tone=(0.6, 0.4, 0.3), ruddy=0.7)
parts = []


def gauss(p, c, s):
    d = (p - Vector(c)).length
    return math.exp(-(d * d) / (2 * s * s))


# ---------------------------------------------------------------- the skull and face: a sphere pushed into shape
C.bpy.ops.mesh.primitive_uv_sphere_add(segments=64, ring_count=48, radius=1.0, location=(0, 0, 0))
head = C.bpy.context.active_object
head.name = "head"
bm = bmesh.new()
bm.from_mesh(head.data)
for v in bm.verts:
    n = v.co.normalized()
    # proportions: narrower than deep, taller than wide; a long face, the cranium behind and above
    p = Vector((n.x * 0.074, n.y * 0.094, n.z * 0.112))
    front = max(0.0, -n.y)
    low = max(0.0, -n.z)
    # the jaw: narrower and squarer low down, the chin forward
    p.x *= 1.0 - 0.22 * low * low
    p.y *= 1.0 - 0.1 * low
    out = 0.0
    out += 0.009 * gauss(p, (0, -0.088, 0.03), 0.03) * (1 if p.z > 0.015 else 0.6)            # the brow ridge
    for s in (-1, 1):
        out -= 0.014 * gauss(p, (s * 0.032, -0.086, 0.012), 0.016)                              # the eye sockets
        out += 0.007 * gauss(p, (s * 0.052, -0.07, -0.012), 0.02)                               # the cheekbones
        out -= 0.006 * gauss(p, (s * 0.06, -0.05, -0.05), 0.022)                                # under the cheekbones
        out += 0.004 * gauss(p, (s * 0.058, -0.03, -0.075), 0.02)                               # the jaw's angle
    # the nose: a bridge growing to a rounded tip, the wings either side, nostrils under it
    nose_t = max(0.0, min(1.0, (0.02 - p.z) / 0.05))
    out += (0.006 + 0.022 * nose_t ** 1.5) * gauss(Vector((p.x * 2.2, p.y, 0)), (0, -0.094, 0), 0.016) * (1 if -0.035 < p.z < 0.025 else 0)
    for s in (-1, 1):
        out += 0.007 * gauss(p, (s * 0.013, -0.1, -0.026), 0.008)                               # the nose's wings
        out -= 0.005 * gauss(p, (s * 0.007, -0.108, -0.036), 0.004)                             # the nostrils
    # the mouth: a philtrum, the upper and lower lips, the line between them
    out += 0.005 * gauss(p, (0, -0.1, -0.05), 0.012)
    out += 0.004 * gauss(p, (0, -0.098, -0.064), 0.011)
    out -= 0.004 * gauss(Vector((p.x * 0.5, p.y, p.z)), (0, -0.1, -0.057), 0.004)
    out += 0.012 * gauss(p, (0, -0.085, -0.098), 0.02)                                          # the chin
    # the cranium a touch fuller behind
    out += 0.008 * max(0.0, n.y) * max(0.0, n.z)
    p += n * out * front + n * out * (1 - front) * 0.5
    v.co = p + Vector((0, 0, 0.13))
bm.to_mesh(head.data)
bm.free()
C.assign(head, skin)
parts.append(head)

# the ears: flattened shells with a rim, set back on the sides at the eyes' height
for s in (-1, 1):
    C.bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=14, radius=1.0, location=(s * 0.074, 0.008, 0.13))
    ear = C.bpy.context.active_object
    ear.name = f"ear{s}"
    ear.scale = (0.011, 0.02, 0.031)
    ear.rotation_euler = (0, s * math.radians(12), s * math.radians(-18))
    C.assign(ear, skin)
    parts.append(ear)

# the neck: a tapered column down into the collar
neck = C.lofted("neck", [(0.0, 0, 0.042, 0.048), (0.05, 0, 0.045, 0.05), (0.11, 0, 0.05, 0.056)], segs=20)
neck.rotation_euler = (0, math.radians(90), 0)
neck.location = (0, 0.012, 0.075)
C.assign(neck, skin)
parts.append(neck)

# the eyes: an eyeball with a sclera, an iris ring, a pupil and a wet clearcoat, in each socket; and eyelids as
# half shells over them
def eye_material(name, iris):
    m = C.bpy.data.materials.new(name)
    nt, b = C._nodes(m)
    tc = nt.nodes.new('ShaderNodeTexCoord')
    sep = nt.nodes.new('ShaderNodeSeparateXYZ')
    nt.links.new(tc.outputs['Object'], sep.inputs['Vector'])
    # the eyeball looks along -Y: radial distance from its axis picks sclera, iris or pupil
    xz = nt.nodes.new('ShaderNodeVectorMath'); xz.operation = 'LENGTH'
    comb = nt.nodes.new('ShaderNodeCombineXYZ')
    nt.links.new(sep.outputs['X'], comb.inputs['X']); nt.links.new(sep.outputs['Z'], comb.inputs['Y'])
    nt.links.new(comb.outputs['Vector'], xz.inputs[0])
    front = nt.nodes.new('ShaderNodeMath'); front.operation = 'LESS_THAN'; front.inputs[1].default_value = 0.0
    nt.links.new(sep.outputs['Y'], front.inputs[0])
    r = C._ramp(nt, xz.outputs['Value'], [(0.0, (0.01, 0.01, 0.01, 1)), (0.0036, (0.01, 0.01, 0.01, 1)),
                                          (0.0042, iris + (1,)), (0.0068, (iris[0] * 0.5, iris[1] * 0.5, iris[2] * 0.5, 1)),
                                          (0.0078, (0.86, 0.82, 0.76, 1)), (0.02, (0.8, 0.7, 0.66, 1))])
    white = nt.nodes.new('ShaderNodeMixRGB')
    white.inputs['Color1'].default_value = (0.85, 0.8, 0.74, 1)
    nt.links.new(r.outputs['Color'], white.inputs['Color2'])
    nt.links.new(front.outputs['Value'], white.inputs['Fac'])
    nt.links.new(white.outputs['Color'], b.inputs['Base Color'])
    b.inputs['Roughness'].default_value = 0.08
    return m

eyem = eye_material("eye", (0.24, 0.34, 0.42))
for s in (-1, 1):
    C.bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=16, radius=0.0115, location=(0, 0, 0))
    eye = C.bpy.context.active_object
    eye.name = f"eye{s}"
    C.assign(eye, eyem)
    C.bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    eye.location = (s * 0.032, -0.078, 0.142)
    parts.append(eye)
    # the upper lid: a shell over the top of the eye, a little proud of it
    C.bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, radius=0.0128, location=(s * 0.032, -0.077, 0.1425))
    lid = C.bpy.context.active_object
    lid.name = f"lid{s}"
    bm = bmesh.new(); bm.from_mesh(lid.data)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.z < 0.002 - 0.004 * max(0, -v.co.y) / 0.0128], context='VERTS')
    bm.to_mesh(lid.data); bm.free()
    sol = lid.modifiers.new("thick", 'SOLIDIFY'); sol.thickness = 0.0012
    C.assign(lid, skin)
    parts.append(lid)

# short hair under a cap line: a shell over the cranium, darker, with a strand bump
hair_m = C.bpy.data.materials.new("hair")
nt, b = C._nodes(hair_m)
b.inputs['Base Color'].default_value = (0.08, 0.06, 0.045, 1)
b.inputs['Roughness'].default_value = 0.7
wave = nt.nodes.new('ShaderNodeTexWave'); wave.inputs['Scale'].default_value = 140; wave.inputs['Distortion'].default_value = 4
C._bump(nt, b, wave.outputs['Fac'], 0.3, 0.001)
hair = C.bpy.data.objects.new("hair", head.data.copy())
C.link(hair)
bm = bmesh.new(); bm.from_mesh(hair.data)
cut = [v for v in bm.verts if v.co.z < 0.165 + 0.05 * max(0.0, -v.co.y) / 0.09 - 0.025 * max(0.0, v.co.y) / 0.09]
bmesh.ops.delete(bm, geom=cut, context='VERTS')
for v in bm.verts:
    v.co += v.normal * 0.003
bm.to_mesh(hair.data); bm.free()
sol = hair.modifiers.new("thick", 'SOLIDIFY'); sol.thickness = 0.004
C.assign(hair, hair_m)
parts.append(hair)

# a weathered stubble darkening on the jaw and upper lip is part of the skin bake: overlay a mask on the skin material
nt = skin.node_tree
bsdf = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
src = bsdf.inputs['Base Color'].links[0].from_socket
tc = nt.nodes.new('ShaderNodeTexCoord'); sep = nt.nodes.new('ShaderNodeSeparateXYZ')
nt.links.new(tc.outputs['Object'], sep.inputs['Vector'])
jaw = C._ramp(nt, sep.outputs['Z'], [(0.03, (1, 1, 1, 1)), (0.1, (0, 0, 0, 1))])
frontm = C._ramp(nt, sep.outputs['Y'], [(-0.07, (1, 1, 1, 1)), (0.0, (0, 0, 0, 1))])
mm = nt.nodes.new('ShaderNodeMath'); mm.operation = 'MULTIPLY'
nt.links.new(jaw.outputs['Color'], mm.inputs[0]); nt.links.new(frontm.outputs['Color'], mm.inputs[1])
speck = C._noise(nt, 2400, 1)
sm = nt.nodes.new('ShaderNodeMath'); sm.operation = 'MULTIPLY'
nt.links.new(mm.outputs['Value'], sm.inputs[0]); nt.links.new(speck.outputs['Fac'], sm.inputs[1])
st = nt.nodes.new('ShaderNodeMixRGB'); st.blend_type = 'MULTIPLY'
nt.links.new(sm.outputs['Value'], st.inputs['Fac'])
nt.links.new(src, st.inputs['Color1'])
st.inputs['Color2'].default_value = (0.35, 0.3, 0.28, 1)
nt.links.new(st.outputs['Color'], bsdf.inputs['Base Color'])

for o in parts:
    C.select_only([o])
    C.bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for m in list(o.modifiers):
        C.bpy.ops.object.modifier_apply(modifier=m.name)
    if o.name == "head":
        sub = o.modifiers.new("sub", 'SUBSURF'); sub.levels = 1
        C.bpy.ops.object.modifier_apply(modifier="sub")
    C.smooth(o, 80)

C.bake_pbr(parts, "head_test", size=1024, ao_distance=0.02)
C.export_glb(parts, os.path.join(C.out_dir(), "head_test.glb"))
