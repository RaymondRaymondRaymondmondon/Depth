# Shared helpers for Depth's art generators (the Trawl Visual Overhaul Spec, section 4).
#
# Every model and texture is made by code in this folder and baked by Blender run headless:
#     blender -b --factory-startup -P tools/artgen/<script>.py -- --out <dir>
# (tools/artgen/run.ps1 finds Blender and runs them all). Scripts build parts from bevelled primitives with
# procedural materials, then bake each asset's materials into one PBR texture set (base colour, roughness,
# metallic, normal, ambient occlusion) on a shared UV atlas and export glTF 2.0 (.glb) with the images embedded.
# No downloaded, purchased or third-party art, and no AI image generation.

import bpy
import bmesh
import math
import os
import sys
from mathutils import Vector, Matrix


# ---------------------------------------------------------------- the command line and the scene
def args():
    """The arguments after Blender's own '--'."""
    argv = sys.argv
    return argv[argv.index("--") + 1:] if "--" in argv else []


def out_dir():
    a = args()
    d = a[a.index("--out") + 1] if "--out" in a else os.path.join(os.getcwd(), "assets")
    os.makedirs(d, exist_ok=True)
    return d


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.cycles.samples = 16
    return sc


def link(obj):
    bpy.context.scene.collection.objects.link(obj)
    return obj


def select_only(objs):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]


def apply_all(obj):
    """Applies the object's modifiers and transform so the mesh is final."""
    select_only([obj])
    for m in list(obj.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)


def smooth(obj, angle_deg=40):
    """Smooth normals across curves, hard at real creases (angle-based)."""
    mesh = obj.data
    for p in mesh.polygons:
        p.use_smooth = True
    select_only([obj])
    try:
        bpy.ops.object.shade_smooth_by_angle(angle=math.radians(angle_deg))
    except Exception:
        try:
            bpy.ops.object.shade_auto_smooth(angle=math.radians(angle_deg))
        except Exception:
            pass


# ---------------------------------------------------------------- parts
def bevelled_box(name, size, loc=(0, 0, 0), bevel=0.003, segments=2, rot=(0, 0, 0)):
    """A box of size (x, y, z) metres with rounded edges (the spec's bevels on every hard edge)."""
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    o.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel > 0:
        m = o.modifiers.new("bevel", 'BEVEL')
        m.width = bevel
        m.segments = segments
        m.limit_method = 'ANGLE'
    return o


def cylinder(name, radius, depth, loc=(0, 0, 0), rot=(0, 0, 0), verts=24, bevel=0.0015, hole=0.0):
    """A cylinder along its local Z, optionally bored through (a barrel's bore)."""
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=radius, depth=depth, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    if hole > 0:
        bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=hole, depth=depth * 1.2, location=loc, rotation=rot)
        cutter = bpy.context.active_object
        b = o.modifiers.new("bore", 'BOOLEAN')
        b.object = cutter
        b.operation = 'DIFFERENCE'
        select_only([o])
        bpy.ops.object.modifier_apply(modifier=b.name)
        bpy.data.objects.remove(cutter)
    if bevel > 0:
        m = o.modifiers.new("bevel", 'BEVEL')
        m.width = bevel
        m.segments = 2
        m.limit_method = 'ANGLE'
    return o


def tube_along(name, points, radius, verts=12):
    """A round bar along a polyline (a lever loop, a trigger guard, a saddle ring)."""
    cu = bpy.data.curves.new(name, 'CURVE')
    cu.dimensions = '3D'
    cu.bevel_depth = radius
    cu.bevel_resolution = 3
    sp = cu.splines.new('POLY')
    sp.points.add(len(points) - 1)
    for i, p in enumerate(points):
        sp.points[i].co = (p[0], p[1], p[2], 1)
    o = link(bpy.data.objects.new(name, cu))
    select_only([o])
    bpy.ops.object.convert(target='MESH')
    return bpy.context.active_object


def lofted(name, sections, segs=16):
    """A solid lofted through elliptical sections [(x, centre_z, half_width_y, half_height_z), ...] along X: stocks,
    fore-ends, grips. The ends are capped."""
    bm = bmesh.new()
    rings = []
    for (x, cz, hy, hz) in sections:
        ring = []
        for k in range(segs):
            a = 2 * math.pi * k / segs
            # a superellipse: rounder than a box, flatter than an ellipse (wood is shaped, not turned)
            c, s = math.cos(a), math.sin(a)
            e = 0.7
            y = hy * math.copysign(abs(c) ** e, c)
            z = cz + hz * math.copysign(abs(s) ** e, s)
            ring.append(bm.verts.new((x, y, z)))
        rings.append(ring)
    for i in range(len(rings) - 1):
        for k in range(segs):
            k1 = (k + 1) % segs
            bm.faces.new((rings[i][k], rings[i][k1], rings[i + 1][k1], rings[i + 1][k]))
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])
    me = bpy.data.meshes.new(name)
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    o = link(bpy.data.objects.new(name, me))
    sub = o.modifiers.new("smooth", 'SUBSURF')
    sub.levels = 1
    sub.render_levels = 1
    return o


# ---------------------------------------------------------------- procedural materials
def _nodes(mat):
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled')
    nt.links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])
    return nt, bsdf


def _noise(nt, scale, detail=4, rough=0.55):
    n = nt.nodes.new('ShaderNodeTexNoise')
    n.inputs['Scale'].default_value = scale
    n.inputs['Detail'].default_value = detail
    n.inputs['Roughness'].default_value = rough
    return n


def _ramp(nt, src, stops):
    r = nt.nodes.new('ShaderNodeValToRGB')
    el = r.color_ramp.elements
    el[0].position, el[0].color = stops[0][0], stops[0][1]
    el[1].position, el[1].color = stops[-1][0], stops[-1][1]
    for pos, col in stops[1:-1]:
        e = el.new(pos)
        e.color = col
    nt.links.new(src, r.inputs['Fac'])
    return r


def _bump(nt, bsdf, height_src, strength, distance=0.001):
    b = nt.nodes.new('ShaderNodeBump')
    b.inputs['Strength'].default_value = strength
    b.inputs['Distance'].default_value = distance
    nt.links.new(height_src, b.inputs['Height'])
    nt.links.new(b.outputs['Normal'], bsdf.inputs['Normal'])
    return b


def _edge_wear(nt):
    """A mask that is 1 on worn edges: the curvature (pointiness) of the geometry, broken up by noise."""
    geo = nt.nodes.new('ShaderNodeNewGeometry')
    n = _noise(nt, 90, 3)
    mix = nt.nodes.new('ShaderNodeMath')
    mix.operation = 'MULTIPLY_ADD'
    nt.links.new(geo.outputs['Pointiness'], mix.inputs[0])
    mix.inputs[1].default_value = 1.0
    nt.links.new(n.outputs['Fac'], mix.inputs[2])
    r = _ramp(nt, mix.outputs['Value'], [(0.95, (0, 0, 0, 1)), (1.08, (1, 1, 1, 1))])
    return r.outputs['Color']


def mat_blued_steel(name="blued steel"):
    """Deep blue-black, worn to bright silver at the edges and high points (metallic 1, roughness 0.25-0.4)."""
    m = bpy.data.materials.new(name)
    nt, b = _nodes(m)
    wear = _edge_wear(nt)
    mixc = nt.nodes.new('ShaderNodeMixRGB')
    mixc.inputs['Color1'].default_value = (0.035, 0.045, 0.065, 1)
    mixc.inputs['Color2'].default_value = (0.62, 0.63, 0.64, 1)
    nt.links.new(wear, mixc.inputs['Fac'])
    nt.links.new(mixc.outputs['Color'], b.inputs['Base Color'])
    b.inputs['Metallic'].default_value = 1.0
    rr = _ramp(nt, _noise(nt, 40, 2).outputs['Fac'], [(0.3, (0.26, 0.26, 0.26, 1)), (0.7, (0.38, 0.38, 0.38, 1))])
    nt.links.new(rr.outputs['Color'], b.inputs['Roughness'])
    _bump(nt, b, _noise(nt, 600, 2).outputs['Fac'], 0.04)
    return m


def mat_case_hardened(name="case-hardened steel"):
    """Mottled blue, straw and purple (metallic 1, roughness 0.3)."""
    m = bpy.data.materials.new(name)
    nt, b = _nodes(m)
    n = _noise(nt, 18, 6, 0.6)
    r = _ramp(nt, n.outputs['Fac'], [(0.3, (0.10, 0.12, 0.22, 1)), (0.45, (0.30, 0.18, 0.30, 1)),
                                     (0.55, (0.55, 0.45, 0.22, 1)), (0.7, (0.42, 0.44, 0.48, 1))])
    wear = _edge_wear(nt)
    mixc = nt.nodes.new('ShaderNodeMixRGB')
    nt.links.new(r.outputs['Color'], mixc.inputs['Color1'])
    mixc.inputs['Color2'].default_value = (0.66, 0.66, 0.66, 1)
    nt.links.new(wear, mixc.inputs['Fac'])
    nt.links.new(mixc.outputs['Color'], b.inputs['Base Color'])
    b.inputs['Metallic'].default_value = 1.0
    b.inputs['Roughness'].default_value = 0.3
    return m


def mat_brass(name="brass"):
    m = bpy.data.materials.new(name)
    nt, b = _nodes(m)
    b.inputs['Base Color'].default_value = (0.78, 0.56, 0.24, 1)
    b.inputs['Metallic'].default_value = 1.0
    rr = _ramp(nt, _noise(nt, 30, 3).outputs['Fac'], [(0.3, (0.3, 0.3, 0.3, 1)), (0.7, (0.5, 0.5, 0.5, 1))])
    nt.links.new(rr.outputs['Color'], b.inputs['Roughness'])
    return m


def mat_walnut(name="oiled walnut", axis='X'):
    """Red-brown with visible grain running along the stock's length (metallic 0, roughness 0.35-0.5)."""
    m = bpy.data.materials.new(name)
    nt, b = _nodes(m)
    tc = nt.nodes.new('ShaderNodeTexCoord')
    mp = nt.nodes.new('ShaderNodeMapping')
    # grain bands run along the length: stretch the wave across the other two axes
    mp.inputs['Scale'].default_value = (4, 60, 60) if axis == 'X' else (60, 4, 60)
    nt.links.new(tc.outputs['Object'], mp.inputs['Vector'])
    dist = _noise(nt, 6, 4)
    wave = nt.nodes.new('ShaderNodeTexWave')
    wave.wave_type = 'BANDS'
    wave.bands_direction = 'Y' if axis == 'X' else 'X'
    wave.inputs['Scale'].default_value = 1.2
    wave.inputs['Distortion'].default_value = 6
    wave.inputs['Detail'].default_value = 3
    nt.links.new(mp.outputs['Vector'], wave.inputs['Vector'])
    r = _ramp(nt, wave.outputs['Fac'], [(0.0, (0.10, 0.045, 0.02, 1)), (0.5, (0.24, 0.11, 0.05, 1)), (1.0, (0.36, 0.18, 0.08, 1))])
    nt.links.new(r.outputs['Color'], b.inputs['Base Color'])
    rr = _ramp(nt, dist.outputs['Fac'], [(0.3, (0.36, 0.36, 0.36, 1)), (0.7, (0.5, 0.5, 0.5, 1))])
    nt.links.new(rr.outputs['Color'], b.inputs['Roughness'])
    _bump(nt, b, wave.outputs['Fac'], 0.05)
    return m


def mat_skin(name="skin", tone=(0.62, 0.42, 0.32), ruddy=0.6):
    """Weathered skin: a warm tone, wind-reddened at the nose and cheeks (a mask by height and how far forward a
    point sits), pores in the bump, roughness about 0.5."""
    m = bpy.data.materials.new(name)
    nt, b = _nodes(m)
    tc = nt.nodes.new('ShaderNodeTexCoord')
    sep = nt.nodes.new('ShaderNodeSeparateXYZ')
    nt.links.new(tc.outputs['Object'], sep.inputs['Vector'])
    # the face looks along -Y in the head's frame: redness rises toward the front
    fr = _ramp(nt, sep.outputs['Y'], [(-0.11, (1, 1, 1, 1)), (-0.05, (0, 0, 0, 1))])
    blot = _noise(nt, 14, 3)
    mixm = nt.nodes.new('ShaderNodeMath')
    mixm.operation = 'MULTIPLY'
    nt.links.new(fr.outputs['Color'], mixm.inputs[0])
    nt.links.new(blot.outputs['Fac'], mixm.inputs[1])
    mixc = nt.nodes.new('ShaderNodeMixRGB')
    mixc.inputs['Color1'].default_value = (tone[0], tone[1], tone[2], 1)
    mixc.inputs['Color2'].default_value = (tone[0] * 1.08, tone[1] * 0.62, tone[2] * 0.58, 1)
    mul = nt.nodes.new('ShaderNodeMath')
    mul.operation = 'MULTIPLY'
    mul.inputs[1].default_value = ruddy * 1.6
    nt.links.new(mixm.outputs['Value'], mul.inputs[0])
    nt.links.new(mul.outputs['Value'], mixc.inputs['Fac'])
    mott = _noise(nt, 60, 3)
    mixd = nt.nodes.new('ShaderNodeMixRGB')
    mixd.blend_type = 'MULTIPLY'
    mixd.inputs['Fac'].default_value = 0.18
    nt.links.new(mixc.outputs['Color'], mixd.inputs['Color1'])
    nt.links.new(mott.outputs['Color'], mixd.inputs['Color2'])
    nt.links.new(mixd.outputs['Color'], b.inputs['Base Color'])
    b.inputs['Roughness'].default_value = 0.5
    _bump(nt, b, _noise(nt, 900, 2).outputs['Fac'], 0.06, 0.0005)
    return m


def mat_flat(name, color, rough=0.5, metal=0.0):
    m = bpy.data.materials.new(name)
    nt, b = _nodes(m)
    b.inputs['Base Color'].default_value = (color[0], color[1], color[2], 1)
    b.inputs['Roughness'].default_value = rough
    b.inputs['Metallic'].default_value = metal
    return m


def assign(obj, mat):
    obj.data.materials.clear()
    obj.data.materials.append(mat)


# ---------------------------------------------------------------- baking to one PBR texture set
def _input_link_or_value(nt, bsdf, socket):
    s = bsdf.inputs[socket]
    return s.links[0].from_socket if s.is_linked else None, s.default_value


def bake_pbr(objs, name, size=1024, margin=8, ao_samples=48, ao_distance=0.05):
    """Unwraps the objects together onto one atlas, bakes their procedural materials into a base colour, a
    roughness, a metallic, a tangent-space normal and an ambient-occlusion map, and swaps every object onto one
    image-textured material. Returns that material."""
    sc = bpy.context.scene
    # one UV atlas over every part (moving parts stay separate objects, as the spec asks)
    select_only(objs)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.004, scale_to_bounds=True)
    bpy.ops.object.mode_set(mode='OBJECT')

    imgs = {}
    def _report(ch, r):
        px = imgs[ch].pixels[:]
        print(f'artgen: bake {name} {ch}: {r}, max {max(px[0::4]):.2f}')

    for ch in ('base', 'rough', 'metal', 'normal', 'ao'):
        im = bpy.data.images.new(f"{name}_{ch}", size, size, alpha=False, float_buffer=False)
        im.colorspace_settings.name = 'sRGB' if ch == 'base' else 'Non-Color'
        imgs[ch] = im

    # every material gets an image node (active) to bake into
    mats = {m for o in objs for m in o.data.materials if m}
    def target(ch):
        for m in mats:
            nt = m.node_tree
            n = nt.nodes.get("bake_target") or nt.nodes.new('ShaderNodeTexImage')
            n.name = "bake_target"
            n.image = imgs[ch]
            nt.nodes.active = n

    def emit_bake(ch, socket, as_value):
        # route the channel into an emission shader and bake EMIT (exact, no lighting)
        saved = []
        for m in mats:
            nt = m.node_tree
            out = next(n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL')
            bsdf = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
            em = nt.nodes.new('ShaderNodeEmission')
            src, val = _input_link_or_value(nt, bsdf, socket)
            if src is not None:
                nt.links.new(src, em.inputs['Color'])
            else:
                v = val if not as_value else (val, val, val, 1)
                em.inputs['Color'].default_value = v
            old = out.inputs['Surface'].links[0].from_socket
            nt.links.new(em.outputs['Emission'], out.inputs['Surface'])
            saved.append((nt, out, old, em))
        target(ch)
        select_only(objs)
        sc.cycles.samples = 1
        r = bpy.ops.object.bake(type='EMIT', margin=margin)
        _report(ch, r)
        for nt, out, old, em in saved:
            nt.links.new(old, out.inputs['Surface'])
            nt.nodes.remove(em)

    emit_bake('base', 'Base Color', False)
    emit_bake('rough', 'Roughness', True)
    emit_bake('metal', 'Metallic', True)
    target('normal')
    select_only(objs)
    sc.cycles.samples = 1
    _report('normal', bpy.ops.object.bake(type='NORMAL', normal_space='TANGENT', margin=margin))
    target('ao')
    sc.cycles.samples = ao_samples
    sc.world = sc.world or bpy.data.worlds.new("world")
    _report('ao', bpy.ops.object.bake(type='AO', margin=margin))

    for im in imgs.values():   # pack at once: the baked pixels live only in memory until then
        im.pack()

    # the final material: the images, set up as glTF's exporter reads them (roughness and metallic packed by it)
    fm = bpy.data.materials.new(name)
    nt, b = _nodes(fm)
    def img(ch, cs):
        n = nt.nodes.new('ShaderNodeTexImage')
        n.image = imgs[ch]   # (its colour space was set when it was made: changing it now would regenerate the image)
        return n
    nt.links.new(img('base', 'sRGB').outputs['Color'], b.inputs['Base Color'])
    rough_img = img('rough', 'Non-Color')
    metal_img = img('metal', 'Non-Color')
    # the exporter packs separate roughness and metallic images into one metallicRoughness texture when they
    # come through a Separate Color node's G and B
    comb = nt.nodes.new('ShaderNodeSeparateColor')
    nt.links.new(rough_img.outputs['Color'], b.inputs['Roughness'])
    nt.links.new(metal_img.outputs['Color'], b.inputs['Metallic'])
    nm = nt.nodes.new('ShaderNodeNormalMap')
    nt.links.new(img('normal', 'Non-Color').outputs['Color'], nm.inputs['Color'])
    nt.links.new(nm.outputs['Normal'], b.inputs['Normal'])
    nt.nodes.remove(comb)
    # occlusion goes through the glTF Material Output group the exporter looks for
    grp = bpy.data.node_groups.get("glTF Material Output")
    if grp is None:
        grp = bpy.data.node_groups.new("glTF Material Output", 'ShaderNodeTree')
        grp.interface.new_socket("Occlusion", in_out='INPUT', socket_type='NodeSocketFloat')
    gn = nt.nodes.new('ShaderNodeGroup')
    gn.node_tree = grp
    nt.links.new(img('ao', 'Non-Color').outputs['Color'], gn.inputs['Occlusion'])
    for o in objs:
        assign(o, fm)
    return fm


# ---------------------------------------------------------------- export
def export_glb(objs, path):
    """glTF 2.0 binary with the images embedded. Blender's Z-up becomes glTF's Y-up: an asset built along Blender +X
    still points along +X in the game; Blender +Z becomes +Y."""
    select_only(objs)
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=True,
                              export_yup=True, export_texcoords=True, export_normals=True, export_tangents=False,
                              export_materials='EXPORT', export_image_format='AUTO')
    print("artgen: wrote", path)
