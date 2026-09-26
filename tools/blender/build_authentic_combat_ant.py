"""
Blender 4.3.2 Script: Authentic Heroic Combat Ant (Caste #3)
Directly reverse-engineered from 1998 classic Ants (Original-Ants/ants.chd):
1. CROSSED GOLDEN AMMUNITION BANDOLIER:
   Heavy dark stitched leather harness crisscrossing chest and back in a bold 'X',
   bearing gleaming brass/gold ammunition cartridges across the shoulders and pectoral chest.
2. VIBRANT RED WARRIOR BANDANA:
   Vibrant crimson cloth bandana tied snugly around the right bicep with trailing knotted ends.
3. BRAWLER / GORILLA PHYSIQUE:
   Broad heavyweight boxer chest, muscular arms, clenched fists, and wide grounded stance.
4. HEAD & MANDIBLES:
   Large cartoon head (~45% of height), spherical continuous UV unwrapping, hooded eyelids,
   bulldog pincer jaws with sharp ivory teeth, and outward-sweeping antennae.
5. SOLID MUSCULAR PETIOLE & SNUG ABDOMEN:
   Thickened petiole waist (>2.2x thicker) seamlessly connecting thorax to gaster.
6. MATERIALS:
   Moss-green mottled chitin cuticle matching Worker and Fire Ant, gleaming brass cartridges,
   crimson cloth bandana, and dark glossy studio mirror floor.
7. EXPORTS:
   Multi-angle stills (front, 3/4 perspective, side, top, 1998 RTS gameplay angle),
   combat_ant.glb (glTF 2.0 PBR), combat_ant.usdz (macOS QuickLook), and combat_ant_authentic.blend.
"""

import bpy
import bmesh
import math
from mathutils import Vector, Euler, Matrix
import os

# -----------------------------------------------------------------------------
# 1. Reset Scene & Render Setup
# -----------------------------------------------------------------------------
bpy.ops.wm.read_factory_settings(use_empty=True)

scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'GPU'

preferences = bpy.context.preferences
cycles_prefs = preferences.addons['cycles'].preferences
cycles_prefs.compute_device_type = 'METAL'
cycles_prefs.get_devices()
for d in cycles_prefs.devices:
    d.use = True

scene.cycles.samples = 96
scene.cycles.use_denoising = True
scene.render.film_transparent = False
scene.render.resolution_x = 1080
scene.render.resolution_y = 1080

web_dir = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d"

# Dark studio world environment
world = bpy.data.worlds.new("StudioWorld")
scene.world = world
world.use_nodes = True
bg_node = world.node_tree.nodes['Background']
bg_node.inputs['Color'].default_value = (0.008, 0.008, 0.010, 1.0)
bg_node.inputs['Strength'].default_value = 0.20

# -----------------------------------------------------------------------------
# 2. PBR Materials Setup
# -----------------------------------------------------------------------------
def create_materials():
    # A. Chitin Material (Deep saturated moss-green cuticle matching Worker & Fire Ant)
    mat_chitin = bpy.data.materials.new("M_Combat_Chitin")
    mat_chitin.use_nodes = True
    nodes = mat_chitin.node_tree.nodes
    links = mat_chitin.node_tree.links
    nodes.clear()

    out = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    tex_chitin = nodes.new('ShaderNodeTexImage')
    tex_chitin.image = bpy.data.images.load(f"{web_dir}/chitin_pbr.png")
    links.new(tex_chitin.outputs['Color'], bsdf.inputs['Base Color'])
    bsdf.inputs['Base Color'].default_value = (0.24, 0.36, 0.16, 1.0)

    tex_coord = nodes.new('ShaderNodeTexCoord')
    noise_bump = nodes.new('ShaderNodeTexNoise')
    noise_bump.inputs['Scale'].default_value = 95.0
    noise_bump.inputs['Detail'].default_value = 5.0
    noise_bump.inputs['Roughness'].default_value = 0.55
    links.new(tex_coord.outputs['Object'], noise_bump.inputs['Vector'])

    bump = nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.22
    bump.inputs['Distance'].default_value = 0.003
    links.new(noise_bump.outputs['Fac'], bump.inputs['Height'])
    links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])

    bsdf.inputs['Roughness'].default_value = 0.50
    bsdf.inputs['Coat Weight'].default_value = 0.24
    bsdf.inputs['Coat Roughness'].default_value = 0.22
    bsdf.inputs['Subsurface Weight'].default_value = 0.09
    bsdf.inputs['Subsurface Radius'].default_value = (0.14, 0.18, 0.09)

    # B. Eye Material
    mat_eye = bpy.data.materials.new("M_Combat_Eye")
    mat_eye.use_nodes = True
    nodes_e = mat_eye.node_tree.nodes
    links_e = mat_eye.node_tree.links
    nodes_e.clear()

    out_e = nodes_e.new('ShaderNodeOutputMaterial')
    bsdf_e = nodes_e.new('ShaderNodeBsdfPrincipled')
    links_e.new(bsdf_e.outputs['BSDF'], out_e.inputs['Surface'])

    tex_eye = nodes_e.new('ShaderNodeTexImage')
    tex_eye.image = bpy.data.images.load(f"{web_dir}/eye_pbr.png")
    links_e.new(tex_eye.outputs['Color'], bsdf_e.inputs['Base Color'])
    bsdf_e.inputs['Base Color'].default_value = (1.0, 1.0, 1.0, 1.0)
    bsdf_e.inputs['Roughness'].default_value = 0.08
    bsdf_e.inputs['Coat Weight'].default_value = 0.95
    bsdf_e.inputs['Coat Roughness'].default_value = 0.02
    bsdf_e.inputs['IOR'].default_value = 1.48

    # C. Mandible Material
    mat_mandible = bpy.data.materials.new("M_Combat_Mandible")
    mat_mandible.use_nodes = True
    nodes_m = mat_mandible.node_tree.nodes
    links_m = mat_mandible.node_tree.links
    nodes_m.clear()

    out_m = nodes_m.new('ShaderNodeOutputMaterial')
    bsdf_m = nodes_m.new('ShaderNodeBsdfPrincipled')
    links_m.new(bsdf_m.outputs['BSDF'], out_m.inputs['Surface'])

    tex_mandible = nodes_m.new('ShaderNodeTexImage')
    tex_mandible.image = bpy.data.images.load(f"{web_dir}/mandible_pbr.png")
    links_m.new(tex_mandible.outputs['Color'], bsdf_m.inputs['Base Color'])
    bsdf_m.inputs['Base Color'].default_value = (0.38, 0.56, 0.20, 1.0)
    bsdf_m.inputs['Roughness'].default_value = 0.38
    bsdf_m.inputs['Coat Weight'].default_value = 0.30
    bsdf_m.inputs['Coat Roughness'].default_value = 0.15
    bsdf_m.inputs['Subsurface Weight'].default_value = 0.12
    bsdf_m.inputs['Subsurface Radius'].default_value = (0.25, 0.40, 0.15)

    # D. Limbs Material (Warm weathered terracotta mahogany)
    mat_limbs = bpy.data.materials.new("M_Combat_Limbs")
    mat_limbs.use_nodes = True
    nodes_l = mat_limbs.node_tree.nodes
    links_l = mat_limbs.node_tree.links
    nodes_l.clear()

    out_l = nodes_l.new('ShaderNodeOutputMaterial')
    bsdf_l = nodes_l.new('ShaderNodeBsdfPrincipled')
    links_l.new(bsdf_l.outputs['BSDF'], out_l.inputs['Surface'])

    tex_limbs = nodes_l.new('ShaderNodeTexImage')
    tex_limbs.image = bpy.data.images.load(f"{web_dir}/limbs_pbr.png")
    links_l.new(tex_limbs.outputs['Color'], bsdf_l.inputs['Base Color'])
    bsdf_l.inputs['Base Color'].default_value = (0.48, 0.22, 0.16, 1.0)

    tc_l = nodes_l.new('ShaderNodeTexCoord')
    noise_l = nodes_l.new('ShaderNodeTexNoise')
    noise_l.inputs['Scale'].default_value = 75.0
    noise_l.inputs['Detail'].default_value = 4.0
    noise_l.inputs['Roughness'].default_value = 0.52
    links_l.new(tc_l.outputs['Object'], noise_l.inputs['Vector'])

    bump_l = nodes_l.new('ShaderNodeBump')
    bump_l.inputs['Strength'].default_value = 0.14
    bump_l.inputs['Distance'].default_value = 0.002
    links_l.new(noise_l.outputs['Fac'], bump_l.inputs['Height'])
    links_l.new(bump_l.outputs['Normal'], bsdf_l.inputs['Normal'])

    bsdf_l.inputs['Roughness'].default_value = 0.44
    bsdf_l.inputs['Coat Weight'].default_value = 0.35
    bsdf_l.inputs['Coat Roughness'].default_value = 0.22
    bsdf_l.inputs['Subsurface Weight'].default_value = 0.06
    bsdf_l.inputs['Subsurface Radius'].default_value = (0.18, 0.08, 0.05)

    # E. Teeth Material (Sharp bone ivory)
    mat_teeth = bpy.data.materials.new("M_Combat_Teeth")
    mat_teeth.use_nodes = True
    nodes_t = mat_teeth.node_tree.nodes
    links_t = mat_teeth.node_tree.links
    nodes_t.clear()

    out_t = nodes_t.new('ShaderNodeOutputMaterial')
    bsdf_t = nodes_t.new('ShaderNodeBsdfPrincipled')
    links_t.new(bsdf_t.outputs['BSDF'], out_t.inputs['Surface'])
    bsdf_t.inputs['Base Color'].default_value = (0.94, 0.95, 0.88, 1.0)
    bsdf_t.inputs['Roughness'].default_value = 0.25
    bsdf_t.inputs['Coat Weight'].default_value = 0.40
    bsdf_t.inputs['Coat Roughness'].default_value = 0.15

    # F. Bandolier Leather Harness Material (Weathered dark chocolate leather)
    mat_bandolier = bpy.data.materials.new("M_Combat_Bandolier")
    mat_bandolier.use_nodes = True
    nodes_b = mat_bandolier.node_tree.nodes
    links_b = mat_bandolier.node_tree.links
    nodes_b.clear()

    out_b = nodes_b.new('ShaderNodeOutputMaterial')
    bsdf_b = nodes_b.new('ShaderNodeBsdfPrincipled')
    links_b.new(bsdf_b.outputs['BSDF'], out_b.inputs['Surface'])
    bsdf_b.inputs['Base Color'].default_value = (0.14, 0.09, 0.06, 1.0)
    bsdf_b.inputs['Roughness'].default_value = 0.58
    bsdf_b.inputs['Coat Weight'].default_value = 0.20
    bsdf_b.inputs['Coat Roughness'].default_value = 0.30

    tc_b = nodes_b.new('ShaderNodeTexCoord')
    noise_b = nodes_b.new('ShaderNodeTexNoise')
    noise_b.inputs['Scale'].default_value = 110.0
    noise_b.inputs['Detail'].default_value = 6.0
    links_b.new(tc_b.outputs['Object'], noise_b.inputs['Vector'])

    bump_b = nodes_b.new('ShaderNodeBump')
    bump_b.inputs['Strength'].default_value = 0.18
    bump_b.inputs['Distance'].default_value = 0.002
    links_b.new(noise_b.outputs['Fac'], bump_b.inputs['Height'])
    links_b.new(bump_b.outputs['Normal'], bsdf_b.inputs['Normal'])

    # G. Cartridge Bullets Material (Gleaming polished brass / gold ammunition)
    mat_bullets = bpy.data.materials.new("M_Combat_Bullets")
    mat_bullets.use_nodes = True
    nodes_bu = mat_bullets.node_tree.nodes
    links_bu = mat_bullets.node_tree.links
    nodes_bu.clear()

    out_bu = nodes_bu.new('ShaderNodeOutputMaterial')
    bsdf_bu = nodes_bu.new('ShaderNodeBsdfPrincipled')
    links_bu.new(bsdf_bu.outputs['BSDF'], out_bu.inputs['Surface'])
    # Rich warm golden brass
    bsdf_bu.inputs['Base Color'].default_value = (0.95, 0.78, 0.24, 1.0)
    bsdf_bu.inputs['Metallic'].default_value = 0.94
    bsdf_bu.inputs['Roughness'].default_value = 0.18
    bsdf_bu.inputs['Coat Weight'].default_value = 0.30
    bsdf_bu.inputs['Coat Roughness'].default_value = 0.10

    # H. Red Warrior Bandana Material (Vibrant crimson woven cloth)
    mat_bandana = bpy.data.materials.new("M_Combat_Bandana")
    mat_bandana.use_nodes = True
    nodes_bd = mat_bandana.node_tree.nodes
    links_bd = mat_bandana.node_tree.links
    nodes_bd.clear()

    out_bd = nodes_bd.new('ShaderNodeOutputMaterial')
    bsdf_bd = nodes_bd.new('ShaderNodeBsdfPrincipled')
    links_bd.new(bsdf_bd.outputs['BSDF'], out_bd.inputs['Surface'])
    # Intense martial crimson red
    bsdf_bd.inputs['Base Color'].default_value = (0.86, 0.10, 0.12, 1.0)
    bsdf_bd.inputs['Roughness'].default_value = 0.52
    bsdf_bd.inputs['Sheen Weight'].default_value = 0.65
    bsdf_bd.inputs['Sheen Roughness'].default_value = 0.40
    bsdf_bd.inputs['Subsurface Weight'].default_value = 0.15
    bsdf_bd.inputs['Subsurface Radius'].default_value = (0.35, 0.08, 0.08)

    tc_bd = nodes_bd.new('ShaderNodeTexCoord')
    noise_bd = nodes_bd.new('ShaderNodeTexNoise')
    noise_bd.inputs['Scale'].default_value = 140.0
    noise_bd.inputs['Detail'].default_value = 4.0
    links_bd.new(tc_bd.outputs['Object'], noise_bd.inputs['Vector'])

    bump_bd = nodes_bd.new('ShaderNodeBump')
    bump_bd.inputs['Strength'].default_value = 0.10
    bump_bd.inputs['Distance'].default_value = 0.001
    links_bd.new(noise_bd.outputs['Fac'], bump_bd.inputs['Height'])
    links_bd.new(bump_bd.outputs['Normal'], bsdf_bd.inputs['Normal'])

    return {
        'chitin': mat_chitin,
        'eye': mat_eye,
        'mandible': mat_mandible,
        'limbs': mat_limbs,
        'teeth': mat_teeth,
        'bandolier': mat_bandolier,
        'bullets': mat_bullets,
        'bandana': mat_bandana
    }

mats = create_materials()

# -----------------------------------------------------------------------------
# 3. Combat Ant Collection & Object Registry
# -----------------------------------------------------------------------------
combat_col = bpy.data.collections.new("Combat_Ant_Authentic")
bpy.context.scene.collection.children.link(combat_col)

def reg(obj):
    if obj.name not in combat_col.objects:
        combat_col.objects.link(obj)
    if obj.name in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.unlink(obj)
    return obj

# -----------------------------------------------------------------------------
# 4. Sculpted Cranium Head with Continuous Spherical UV Mapping
# -----------------------------------------------------------------------------
bm_head = bmesh.new()
bmesh.ops.create_cube(bm_head, size=1.0)
bmesh.ops.subdivide_edges(bm_head, edges=bm_head.edges, cuts=8, use_grid_fill=True)

rx_base = 0.280
ry_base = 0.250
rz_base = 0.315

for v in bm_head.verts:
    p = v.co.normalized()
    x = p.x * rx_base
    y = p.y * ry_base
    z = p.z * rz_base

    # Dome rounding
    if z > 0:
        x *= (1.0 - 0.08 * (z / rz_base))
        y *= (1.0 - 0.06 * (z / rz_base))

    # Bulldog snout and facial pinch
    if y < -0.05:
        t_front = min(1.0, (-y - 0.05) / 0.16)
        y -= 0.065 * t_front
        z -= 0.035 * t_front
        if z < 0.08 and abs(x) < 0.16:
            cavit_t = math.cos(x * 12.0) * math.exp(-((z - 0.02) / 0.12)**2)
            y += 0.085 * cavit_t
            x -= 0.028 * math.copysign(1.0, x) * cavit_t
            z -= 0.022 * cavit_t

    # Brawler heavy jowls / cheek flare
    if z < 0.02 and y < 0:
        cheeks = math.exp(-((z + 0.14) / 0.16)**2)
        if abs(x) > 0.06:
            x += math.copysign(0.055 * cheeks, x)
            y -= 0.060 * cheeks
        else:
            y -= 0.048 * math.exp(-((x / 0.06)**2)) * math.exp(-((z + 0.08) / 0.12)**2)

    # Carve lower chin so jaws form the true bottom
    if z < -0.05 and y < 0.05:
        t_cut = min(1.0, (-z - 0.05) / 0.20)
        y += 0.14 * t_cut
        z += 0.08 * t_cut
        x *= (1.0 - 0.25 * t_cut)
    elif z < -0.12:
        t_neck = min(1.0, (-z - 0.12) / 0.18)
        x *= (1.0 - 0.40 * t_neck)
        y *= (1.0 - 0.35 * t_neck)

    v.co = Vector((x, y, z))

# Continuous spherical UV unwrapping to completely eliminate rectangular seams
uv_layer_head = bm_head.loops.layers.uv.new("UVMap")
for face in bm_head.faces:
    for loop in face.loops:
        norm = loop.vert.co.normalized()
        u = math.atan2(norm.x, -norm.y) / (2.0 * math.pi) + 0.5
        v_coord = norm.z * 0.5 + 0.5
        loop[uv_layer_head].uv = (u, v_coord)

head_mesh = bpy.data.meshes.new("Combat_Head_Mesh")
bm_head.to_mesh(head_mesh)
bm_head.free()

for p in head_mesh.polygons:
    p.use_smooth = True

head_obj = bpy.data.objects.new("Combat_Head", head_mesh)
head_obj.location = (0, -0.04, 1.58)
head_obj.data.materials.append(mats['chitin'])
sub_h = head_obj.modifiers.new("Subsurf", 'SUBSURF')
sub_h.levels = 2
reg(head_obj)

# -----------------------------------------------------------------------------
# 5. Cartoon Compound Eyes & Hooded Eyelids
# -----------------------------------------------------------------------------
def make_bulging_eye(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    eye_pos = Vector((sign * 0.160, -0.21, 1.635))

    bm_eye = bmesh.new()
    bmesh.ops.create_uvsphere(bm_eye, u_segments=40, v_segments=28, radius=1.0)
    rx, ry, rz = 0.128, 0.118, 0.158
    for v in bm_eye.verts:
        v.co.x *= rx
        v.co.y *= ry
        v.co.z *= rz

    uv_l = bm_eye.loops.layers.uv.new("UVMap")
    for face in bm_eye.faces:
        for loop in face.loops:
            vx, vy, vz = loop.vert.co.x, loop.vert.co.y, loop.vert.co.z
            u_coord = (math.atan2(vx, -vy) / (2.0 * math.pi)) + 0.5
            v_coord = (vz / (2.0 * rz)) + 0.5
            loop[uv_l].uv = (u_coord, v_coord)

    mesh = bpy.data.meshes.new(f"{name}_Mesh")
    bm_eye.to_mesh(mesh)
    bm_eye.free()

    eye_obj = bpy.data.objects.new(name, mesh)
    eye_obj.location = eye_pos
    eye_obj.rotation_euler = Euler((math.radians(2.5), 0.0, sign * math.radians(3.5)), 'XYZ')
    eye_obj.data.materials.append(mats['eye'])
    sub = eye_obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 1
    for p in mesh.polygons:
        p.use_smooth = True
    return reg(eye_obj)

make_bulging_eye("Eye_L", True)
make_bulging_eye("Eye_R", False)

# Eyelid Hoods
for is_left in [True, False]:
    sign_x = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"
    bm_lid = bmesh.new()
    bmesh.ops.create_uvsphere(bm_lid, u_segments=32, v_segments=20, radius=0.134)
    for v in list(bm_lid.verts):
        if v.co.z < 0.015 or v.co.y > 0.02:
            bm_lid.verts.remove(v)
    for v in bm_lid.verts:
        v.co.x *= 1.02
        v.co.y *= 1.08
        v.co.z *= 0.94
    lid_mesh = bpy.data.meshes.new(f"Combat_Eyelid_{suf}_Mesh")
    bm_lid.to_mesh(lid_mesh)
    bm_lid.free()
    for p in lid_mesh.polygons:
        p.use_smooth = True

    lid_obj = bpy.data.objects.new(f"Combat_Eyelid_{suf}", lid_mesh)
    lid_obj.location = (sign_x * 0.160, -0.212, 1.640)
    lid_obj.rotation_euler = (math.radians(-4), math.radians(sign_x * 8), 0)
    lid_obj.data.materials.append(mats['chitin'])
    lid_sub = lid_obj.modifiers.new("Subsurf", 'SUBSURF')
    lid_sub.levels = 1
    reg(lid_obj)

# -----------------------------------------------------------------------------
# 6. Heavy Brawler Caliper Pincer Claws & Teeth
# -----------------------------------------------------------------------------
def make_combat_pincer_claw(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    bm = bmesh.new()

    stations = [
        # 0. Cheek hinge condyle
        (Vector((sign * 0.190, -0.160, 1.450)), Vector((sign * 0.25, -0.75, -0.60)).normalized(), 0.060, 0.055),
        # 1. Broad lateral caliper bow
        (Vector((sign * 0.245, -0.230, 1.400)), Vector((sign * 0.10, -0.90, -0.42)).normalized(), 0.076, 0.068),
        # 2. Heavy anterior brawler muscle lobe
        (Vector((sign * 0.200, -0.320, 1.350)), Vector((sign * -0.50, -0.80, -0.32)).normalized(), 0.082, 0.074),
        # 3. Anterior medial turn with inner bite notch
        (Vector((sign * 0.130, -0.325, 1.330)), Vector((sign * -0.88, -0.45, -0.15)).normalized(), 0.062, 0.056),
        # 4. Inward-hooking caliper tip
        (Vector((sign * 0.068, -0.290, 1.320)), Vector((sign * -0.96, -0.26, -0.05)).normalized(), 0.038, 0.034),
    ]

    num_pts = 16
    rings = []

    for i, (center, normal, rx, rz) in enumerate(stations):
        up_ref = Vector((0, 0, 1))
        right = normal.cross(up_ref).normalized()
        up = right.cross(normal).normalized()

        c_ring = []
        for j in range(num_pts):
            th = 2.0 * math.pi * j / num_pts
            cos_t = math.cos(th)
            sin_t = math.sin(th)

            is_outer = (cos_t * sign > 0)
            if i >= 2 and not is_outer:
                rx_eff = rx * 0.48
            else:
                rx_eff = rx * (1.28 if is_outer else 0.82)

            rz_eff = rz * (0.85 if sin_t < 0 else 1.10)
            p_local = (right * (cos_t * rx_eff)) + (up * (sin_t * rz_eff))
            world_p = center + p_local
            c_ring.append(bm.verts.new(world_p))
        rings.append(c_ring)

    uv_layer = bm.loops.layers.uv.new("UVMap")
    for i in range(len(stations) - 1):
        r0 = rings[i]
        r1 = rings[i + 1]
        v_coord0 = i / (len(stations) - 1)
        v_coord1 = (i + 1) / (len(stations) - 1)
        for j in range(num_pts):
            j_next = (j + 1) % num_pts
            f = bm.faces.new([r0[j], r1[j], r1[j_next], r0[j_next]])
            u0 = j / num_pts
            u1 = (j + 1) / num_pts
            for loop in f.loops:
                if loop.vert == r0[j]:
                    loop[uv_layer].uv = (u0, v_coord0)
                elif loop.vert == r1[j]:
                    loop[uv_layer].uv = (u0, v_coord1)
                elif loop.vert == r1[j_next]:
                    loop[uv_layer].uv = (u1, v_coord1)
                elif loop.vert == r0[j_next]:
                    loop[uv_layer].uv = (u1, v_coord0)

    # Caps
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new(f"{name}_Mesh")
    bm.to_mesh(mesh)
    bm.free()

    obj = bpy.data.objects.new(name, mesh)
    obj.data.materials.append(mats['mandible'])
    sub = obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 2
    for p in mesh.polygons:
        p.use_smooth = True
    return reg(obj)

make_combat_pincer_claw("Combat_Mandible_L", True)
make_combat_pincer_claw("Combat_Mandible_R", False)

# Sharp Ivory Teeth along inner jaw margin
teeth_data = [
    # Left mandible teeth
    (Vector((-0.076, -0.285, 1.320)), Vector((0.75, -0.60, -0.15)), 0.016, 0.040),
    (Vector((-0.118, -0.310, 1.332)), Vector((0.85, -0.45, -0.20)), 0.015, 0.036),
    # Right mandible teeth
    (Vector((0.076, -0.285, 1.320)), Vector((-0.75, -0.60, -0.15)), 0.016, 0.040),
    (Vector((0.118, -0.310, 1.332)), Vector((-0.85, -0.45, -0.20)), 0.015, 0.036),
]

for idx, (base_pos, dir_vec, r_base, length) in enumerate(teeth_data):
    dir_n = dir_vec.normalized()
    tip_pos = base_pos + dir_n * length
    bpy.ops.mesh.primitive_cone_add(
        vertices=12, radius1=r_base, radius2=0.002, depth=length,
        location=(base_pos + tip_pos) * 0.5
    )
    tooth_obj = bpy.context.active_object
    tooth_obj.name = f"Combat_Tooth_{idx}"
    z_axis = Vector((0, 0, 1))
    q_rot = z_axis.rotation_difference(dir_n)
    tooth_obj.rotation_euler = q_rot.to_euler()
    tooth_obj.data.materials.append(mats['teeth'])
    bpy.ops.object.shade_smooth()
    reg(tooth_obj)

# -----------------------------------------------------------------------------
# 7. Swept Outward Antennae
# -----------------------------------------------------------------------------
def make_combat_antenna(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    p0 = Vector((sign * 0.08, -0.22, 1.68))
    p1 = Vector((sign * 0.20, -0.20, 1.76))
    p2 = Vector((sign * 0.35, -0.12, 1.84))
    p3 = Vector((sign * 0.44, -0.04, 1.88))

    curve_data = bpy.data.curves.new(name=f"{name}_Curve", type='CURVE')
    curve_data.dimensions = '3D'
    curve_data.bevel_depth = 0.018
    curve_data.bevel_resolution = 4

    spline = curve_data.splines.new('BEZIER')
    spline.bezier_points.add(3)
    pts = [p0, p1, p2, p3]
    for i, p in enumerate(pts):
        bp = spline.bezier_points[i]
        bp.co = p
        bp.handle_left_type = 'AUTO'
        bp.handle_right_type = 'AUTO'

    obj = bpy.data.objects.new(name, curve_data)
    obj.data.materials.append(mats['chitin'])
    reg(obj)

    # Teardrop club tip
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=12, radius=0.030, location=p3)
    tip_obj = bpy.context.active_object
    tip_obj.name = f"{name}_Tip"
    tip_obj.scale = (1.0, 1.2, 0.9)
    bpy.ops.object.transform_apply(scale=True)
    tip_obj.data.materials.append(mats['chitin'])
    bpy.ops.object.shade_smooth()
    reg(tip_obj)

make_combat_antenna("Combat_Antenna_L", True)
make_combat_antenna("Combat_Antenna_R", False)

# -----------------------------------------------------------------------------
# 8. Heavy Gorilla / Boxer Thorax (Broad Muscular Shoulders & Chest)
# -----------------------------------------------------------------------------
# Neck socket
bpy.ops.mesh.primitive_cylinder_add(
    vertices=24, radius=0.10, depth=0.10,
    location=(0, -0.06, 1.40),
    rotation=(math.radians(18), 0, 0)
)
neck = bpy.context.active_object
neck.name = "Combat_Neck"
neck.data.materials.append(mats['chitin'])
bpy.ops.object.shade_smooth()
reg(neck)

# Thorax segments - significantly broader for heavyweight brawler build
thorax_segments = [
    # Pronotum (Massive rounded shoulder chest)
    ("Combat_Pronotum", (0, -0.08, 1.25), 0.23, (1.24, 0.96, 1.10), math.radians(12)),
    # Mesonotum (Muscular mid-back)
    ("Combat_Mesonotum", (0, -0.01, 1.12), 0.19, (1.14, 0.92, 1.04), math.radians(22)),
    # Metanotum (Rear arch leading into waist)
    ("Combat_Metanotum", (0, 0.05, 1.01), 0.17, (1.10, 0.92, 0.98), math.radians(30))
]

for name, loc, radius, scale, rot_x in thorax_segments:
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=32, ring_count=20, radius=radius,
        location=loc,
        rotation=(rot_x, 0, 0)
    )
    th_obj = bpy.context.active_object
    th_obj.name = name
    th_obj.scale = scale
    bpy.ops.object.transform_apply(scale=True)
    th_obj.data.materials.append(mats['chitin'])
    sub = th_obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 1
    bpy.ops.object.shade_smooth()
    reg(th_obj)

# -----------------------------------------------------------------------------
# 9. Crossed Golden Ammunition Bandolier Harness
# -----------------------------------------------------------------------------
# Strap 1: From right shoulder across chest to left hip, wrapping around back
strap1_pts = [
    Vector((0.18, -0.12, 1.36)),   # Right shoulder crest
    Vector((0.18, -0.26, 1.30)),   # Right shoulder front
    Vector((0.11, -0.32, 1.22)),   # Upper right chest
    Vector((0.00, -0.34, 1.15)),   # Center chest cross
    Vector((-0.12, -0.30, 1.08)),  # Lower left ribs
    Vector((-0.22, -0.10, 1.03)),  # Left flank
    Vector((-0.20, 0.12, 1.06)),   # Left rear flank
    Vector((-0.10, 0.18, 1.14)),   # Left rear back
    Vector((0.00, 0.20, 1.22)),    # Center back cross
    Vector((0.12, 0.16, 1.30)),    # Right rear shoulder
]

# Strap 2: From left shoulder across chest to right hip, wrapping around back
strap2_pts = [
    Vector((-0.18, -0.12, 1.36)),  # Left shoulder crest
    Vector((-0.18, -0.26, 1.30)),  # Left shoulder front
    Vector((-0.11, -0.32, 1.22)),  # Upper left chest
    Vector((0.00, -0.35, 1.15)),   # Center chest cross (slightly over strap 1)
    Vector((0.12, -0.30, 1.08)),   # Lower right ribs
    Vector((0.22, -0.10, 1.03)),   # Right flank
    Vector((0.20, 0.12, 1.06)),    # Right rear flank
    Vector((0.10, 0.18, 1.14)),    # Right rear back
    Vector((0.00, 0.21, 1.22)),    # Center back cross (slightly over strap 1)
    Vector((-0.12, 0.16, 1.30)),   # Left rear shoulder
]

def make_bandolier_strap(name, pts):
    curve_data = bpy.data.curves.new(name=f"{name}_Curve", type='CURVE')
    curve_data.dimensions = '3D'
    curve_data.bevel_depth = 0.028
    curve_data.bevel_resolution = 3
    curve_data.use_fill_caps = True

    spline = curve_data.splines.new('BEZIER')
    spline.use_cyclic_u = True
    spline.bezier_points.add(len(pts) - 1)

    for i, p in enumerate(pts):
        bp = spline.bezier_points[i]
        bp.co = p
        bp.handle_left_type = 'AUTO'
        bp.handle_right_type = 'AUTO'

    obj = bpy.data.objects.new(name, curve_data)
    obj.data.materials.append(mats['bandolier'])
    return reg(obj)

make_bandolier_strap("Bandolier_Strap_R_to_L", strap1_pts)
make_bandolier_strap("Bandolier_Strap_L_to_R", strap2_pts)

# Golden/Brass Ammunition Cartridges mounted along the front straps
cartridge_locations = [
    # Along Strap 1 (Right shoulder -> Left hip)
    (Vector((0.17, -0.26, 1.31)), Vector((0.55, -0.80, 0.25))),
    (Vector((0.13, -0.31, 1.25)), Vector((0.65, -0.75, 0.22))),
    (Vector((0.07, -0.34, 1.19)), Vector((0.72, -0.68, 0.18))),
    (Vector((-0.06, -0.33, 1.12)), Vector((0.78, -0.62, 0.14))),
    (Vector((-0.12, -0.29, 1.07)), Vector((0.82, -0.55, 0.10))),
    (Vector((-0.18, -0.20, 1.03)), Vector((0.86, -0.48, 0.05))),

    # Along Strap 2 (Left shoulder -> Right hip)
    (Vector((-0.17, -0.26, 1.31)), Vector((-0.55, -0.80, 0.25))),
    (Vector((-0.13, -0.31, 1.25)), Vector((-0.65, -0.75, 0.22))),
    (Vector((-0.07, -0.34, 1.19)), Vector((-0.72, -0.68, 0.18))),
    (Vector((0.06, -0.33, 1.12)), Vector((-0.78, -0.62, 0.14))),
    (Vector((0.12, -0.29, 1.07)), Vector((-0.82, -0.55, 0.10))),
    (Vector((0.18, -0.20, 1.03)), Vector((-0.86, -0.48, 0.05))),
]

for idx, (pos, orient_vec) in enumerate(cartridge_locations):
    orient_n = orient_vec.normalized()

    # Brass Shell Casing (Cylinder)
    casing_len = 0.054
    casing_r = 0.020
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=16, radius=casing_r, depth=casing_len,
        location=pos
    )
    casing = bpy.context.active_object
    casing.name = f"Cartridge_Casing_{idx}"
    z_axis = Vector((0, 0, 1))
    q_rot = z_axis.rotation_difference(orient_n)
    casing.rotation_euler = q_rot.to_euler()
    casing.data.materials.append(mats['bullets'])
    bpy.ops.object.shade_smooth()
    reg(casing)

    # Bullet Head (Pointed Cone / Oval Dome)
    bullet_tip_pos = pos + orient_n * (casing_len * 0.5 + 0.015)
    bpy.ops.mesh.primitive_cone_add(
        vertices=16, radius1=casing_r * 0.96, radius2=0.002, depth=0.030,
        location=bullet_tip_pos
    )
    bullet_head = bpy.context.active_object
    bullet_head.name = f"Cartridge_Bullet_{idx}"
    bullet_head.rotation_euler = q_rot.to_euler()
    bullet_head.data.materials.append(mats['bullets'])
    bpy.ops.object.shade_smooth()
    reg(bullet_head)

    # Leather Retaining Loop
    bpy.ops.mesh.primitive_torus_add(
        major_radius=casing_r + 0.006, minor_radius=0.005,
        major_segments=16, minor_segments=8,
        location=pos
    )
    loop_obj = bpy.context.active_object
    loop_obj.name = f"Cartridge_Loop_{idx}"
    loop_obj.rotation_euler = q_rot.to_euler()
    loop_obj.data.materials.append(mats['bandolier'])
    bpy.ops.object.shade_smooth()
    reg(loop_obj)

# -----------------------------------------------------------------------------
# 10. Substantially Thickened Petiole Waist & Snug Gaster Abdomen
# -----------------------------------------------------------------------------
def make_joint_socket(name, location, radius, material=mats['chitin']):
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=24, ring_count=16, radius=radius,
        location=location
    )
    socket = bpy.context.active_object
    socket.name = name
    socket.data.materials.append(material)
    bpy.ops.object.shade_smooth()
    return reg(socket)

def make_chitin_segment(name, p0, p1, r_start, r_mid, r_end, is_sleeve=True, material=mats['chitin']):
    bm = bmesh.new()
    dir_vec = p1 - p0
    length = dir_vec.length
    if length < 0.0001:
        return None
    dir_n = dir_vec.normalized()

    up_ref = Vector((0, 0, 1))
    if abs(dir_n.dot(up_ref)) > 0.95:
        up_ref = Vector((0, 1, 0))
    right = dir_n.cross(up_ref).normalized()
    up = right.cross(dir_n).normalized()

    num_rings = 10
    num_pts = 16
    rings = []

    for i in range(num_rings):
        t = i / (num_rings - 1)
        z_disp = length * t
        cen = p0 + dir_n * z_disp

        if t < 0.5:
            fac = t / 0.5
            rad = r_start * (1.0 - fac) + r_mid * fac
        else:
            fac = (t - 0.5) / 0.5
            rad = r_mid * (1.0 - fac) + r_end * fac

        c_ring = []
        for j in range(num_pts):
            th = 2.0 * math.pi * j / num_pts
            rx = rad * (1.08 if is_sleeve else 1.0)
            rz = rad * (0.94 if is_sleeve else 1.0)
            p_local = (right * (math.cos(th) * rx)) + (up * (math.sin(th) * rz))
            c_ring.append(bm.verts.new(cen + p_local))
        rings.append(c_ring)

    uv_layer = bm.loops.layers.uv.new("UVMap")
    for i in range(num_rings - 1):
        r0 = rings[i]
        r1 = rings[i + 1]
        v0 = i / (num_rings - 1)
        v1 = (i + 1) / (num_rings - 1)
        for j in range(num_pts):
            jn = (j + 1) % num_pts
            f = bm.faces.new([r0[j], r1[j], r1[jn], r0[jn]])
            u0 = j / num_pts
            u1 = (j + 1) / num_pts
            for loop in f.loops:
                if loop.vert == r0[j]:
                    loop[uv_layer].uv = (u0, v0)
                elif loop.vert == r1[j]:
                    loop[uv_layer].uv = (u0, v1)
                elif loop.vert == r1[jn]:
                    loop[uv_layer].uv = (u1, v1)
                elif loop.vert == r0[jn]:
                    loop[uv_layer].uv = (u1, v0)

    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)

    mesh = bpy.data.meshes.new(f"{name}_Mesh")
    bm.to_mesh(mesh)
    bm.free()

    obj = bpy.data.objects.new(name, mesh)
    obj.data.materials.append(material)
    sub = obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 1
    for p in mesh.polygons:
        p.use_smooth = True
    return reg(obj)

p_pet_start = Vector((0, 0.08, 0.94))
p_pet_end   = Vector((0, 0.20, 0.80))

make_joint_socket("Combat_Petiole_Thorax_Socket", p_pet_start, 0.104, material=mats['chitin'])
make_chitin_segment("Combat_Petiole", p_pet_start, p_pet_end, 0.102, 0.098, 0.110, is_sleeve=True, material=mats['chitin'])
make_joint_socket("Combat_Petiole_Gaster_Socket", p_pet_end, 0.112, material=mats['chitin'])

# Gaster: Suspended Plump Egg Abdomen seated snugly against rear thorax
bpy.ops.mesh.primitive_uv_sphere_add(
    segments=36, ring_count=24, radius=1.0,
    location=(0, 0.30, 0.72),
    rotation=(math.radians(20), 0, 0)
)
gaster_obj = bpy.context.active_object
gaster_obj.name = "Combat_Gaster"

for v in gaster_obj.data.vertices:
    x = v.co.x * 0.25
    y = v.co.y * 0.33
    z = v.co.z * 0.25

    if y > 0:
        taper = 1.0 - 0.36 * (y / 0.33)
        x *= taper
        z *= (taper * 0.92)
    else:
        # Gentle smooth anterior dome with zero tenting crease
        t_ant = min(1.0, (-y) / 0.33)
        x *= (1.0 - 0.10 * t_ant)
        z *= (1.0 - 0.10 * t_ant)

    groove = math.sin((y + 0.33) * 18.0) * 0.006
    x += groove * (x / 0.25)
    z += groove * (z / 0.25)
    v.co = Vector((x, y, z))

gaster_obj.data.update()
gaster_obj.data.materials.append(mats['chitin'])
sub_g = gaster_obj.modifiers.new("Subsurf", 'SUBSURF')
sub_g.levels = 2
bpy.ops.object.shade_smooth()
reg(gaster_obj)

# -----------------------------------------------------------------------------
# 11. Sculpted Muscular Limbs & Red Warrior Bandana on Bicep
# -----------------------------------------------------------------------------
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"

    # Broad heavyweight brawler shoulder guard stance (flared elbows, framing chest)
    p_shoulder = Vector((sign * 0.20, -0.06, 1.24))
    p_elbow    = Vector((sign * 0.34, -0.16, 1.02))
    p_wrist    = Vector((sign * 0.26, -0.30, 0.92))

    make_joint_socket(f"Combat_Shoulder_{suf}", p_shoulder, 0.074, material=mats['chitin'])
    # Muscular upper arm sleeve
    make_chitin_segment(f"Combat_Arm_Upper_{suf}", p_shoulder, p_elbow, 0.070, 0.065, 0.058, is_sleeve=True, material=mats['limbs'])

    make_joint_socket(f"Combat_Elbow_{suf}", p_elbow, 0.062, material=mats['limbs'])
    # Muscular forearm sleeve
    make_chitin_segment(f"Combat_Arm_Forearm_{suf}", p_elbow, p_wrist, 0.060, 0.066, 0.054, is_sleeve=True, material=mats['limbs'])

    # Clenched brawler fists / knuckles framing chest
    p_fist = p_wrist + Vector((sign * 0.02, -0.08, -0.02))
    make_joint_socket(f"Combat_Fist_{suf}", p_fist, 0.054, material=mats['chitin'])
    p_knuckle1 = p_fist + Vector((sign * -0.02, -0.04, 0.01))
    p_knuckle2 = p_fist + Vector((sign * 0.02, -0.04, -0.02))
    make_joint_socket(f"Combat_Knuckle1_{suf}", p_knuckle1, 0.026, material=mats['limbs'])
    make_joint_socket(f"Combat_Knuckle2_{suf}", p_knuckle2, 0.026, material=mats['limbs'])

    # Middle walking legs
    p_coxa_m  = Vector((sign * 0.14, 0.02, 1.06))
    p_femur_m = Vector((sign * 0.42, 0.00, 1.08))
    p_knee_m  = Vector((sign * 0.58, -0.10, 0.72))
    p_tars_m  = Vector((sign * 0.56, -0.22, 0.04))

    make_joint_socket(f"Combat_Leg_Mid_Coxa_{suf}", p_coxa_m, 0.058, material=mats['chitin'])
    make_chitin_segment(f"Combat_Leg_Mid_Femur_{suf}", p_coxa_m, p_knee_m, 0.054, 0.048, 0.044, is_sleeve=True, material=mats['limbs'])
    make_joint_socket(f"Combat_Leg_Mid_Knee_{suf}", p_knee_m, 0.046, material=mats['limbs'])
    make_chitin_segment(f"Combat_Leg_Mid_Tibia_{suf}", p_knee_m, p_tars_m, 0.044, 0.038, 0.034, is_sleeve=True, material=mats['limbs'])
    make_joint_socket(f"Combat_Leg_Mid_Foot_{suf}", p_tars_m, 0.038, material=mats['limbs'])

    # Hind walking legs
    p_coxa_h  = Vector((sign * 0.12, 0.12, 0.98))
    p_knee_h  = Vector((sign * 0.62, 0.28, 1.14))
    p_tars_h  = Vector((sign * 0.66, 0.42, 0.04))

    make_joint_socket(f"Combat_Leg_Hind_Coxa_{suf}", p_coxa_h, 0.060, material=mats['chitin'])
    make_chitin_segment(f"Combat_Leg_Hind_Femur_{suf}", p_coxa_h, p_knee_h, 0.056, 0.050, 0.046, is_sleeve=True, material=mats['limbs'])
    make_joint_socket(f"Combat_Leg_Hind_Knee_{suf}", p_knee_h, 0.048, material=mats['limbs'])
    make_chitin_segment(f"Combat_Leg_Hind_Tibia_{suf}", p_knee_h, p_tars_h, 0.046, 0.040, 0.036, is_sleeve=True, material=mats['limbs'])
    make_joint_socket(f"Combat_Leg_Hind_Foot_{suf}", p_tars_h, 0.040, material=mats['limbs'])

# Red Warrior Bandana tied around Right Bicep
p_bicep_r = Vector((0.27, -0.11, 1.13))
arm_dir = (Vector((0.34, -0.16, 1.02)) - Vector((0.20, -0.06, 1.24))).normalized()

# Bandana Ring Cuff
bpy.ops.mesh.primitive_cylinder_add(
    vertices=24, radius=0.088, depth=0.058,
    location=p_bicep_r
)
bandana_cuff = bpy.context.active_object
bandana_cuff.name = "Combat_Bandana_Cuff"
z_axis = Vector((0, 0, 1))
q_rot = z_axis.rotation_difference(arm_dir)
bandana_cuff.rotation_euler = q_rot.to_euler()
bandana_cuff.data.materials.append(mats['bandana'])
sub_bc = bandana_cuff.modifiers.new("Subsurf", 'SUBSURF')
sub_bc.levels = 1
bpy.ops.object.shade_smooth()
reg(bandana_cuff)

# Bandana Knot on outer lateral arm
p_knot = p_bicep_r + Vector((0.088, -0.02, 0.01))
bpy.ops.mesh.primitive_uv_sphere_add(
    segments=16, ring_count=12, radius=0.032,
    location=p_knot
)
knot_obj = bpy.context.active_object
knot_obj.name = "Combat_Bandana_Knot"
knot_obj.scale = (1.2, 0.8, 1.0)
knot_obj.data.materials.append(mats['bandana'])
bpy.ops.object.shade_smooth()
reg(knot_obj)

# Bandana Knot Ribbon Tails fluttering downward/backward
tails_pts = [
    # Tail 1
    [p_knot, p_knot + Vector((0.03, 0.05, -0.08)), p_knot + Vector((0.02, 0.10, -0.15))],
    # Tail 2
    [p_knot, p_knot + Vector((-0.02, 0.04, -0.09)), p_knot + Vector((-0.03, 0.09, -0.17))]
]

for t_idx, pts in enumerate(tails_pts):
    c_data = bpy.data.curves.new(name=f"Combat_Bandana_Tail_{t_idx}_Curve", type='CURVE')
    c_data.dimensions = '3D'
    c_data.bevel_depth = 0.020
    c_data.bevel_resolution = 2
    spline = c_data.splines.new('BEZIER')
    spline.bezier_points.add(len(pts) - 1)
    for p_i, p in enumerate(pts):
        bp = spline.bezier_points[p_i]
        bp.co = p
        bp.handle_left_type = 'AUTO'
        bp.handle_right_type = 'AUTO'
    t_obj = bpy.data.objects.new(f"Combat_Bandana_Tail_{t_idx}", c_data)
    t_obj.data.materials.append(mats['bandana'])
    reg(t_obj)

# -----------------------------------------------------------------------------
# 12. Studio Lighting & Ground Mirror Plane
# -----------------------------------------------------------------------------
# Mirror Ground Plane
bpy.ops.mesh.primitive_plane_add(size=12.0, location=(0, 0, 0))
ground = bpy.context.active_object
ground.name = "Studio_Ground"

mat_ground = bpy.data.materials.new("M_Ground_Mirror")
mat_ground.use_nodes = True
nodes_g = mat_ground.node_tree.nodes
nodes_g.clear()
out_g = nodes_g.new('ShaderNodeOutputMaterial')
bsdf_g = nodes_g.new('ShaderNodeBsdfPrincipled')
mat_ground.node_tree.links.new(bsdf_g.outputs['BSDF'], out_g.inputs['Surface'])
bsdf_g.inputs['Base Color'].default_value = (0.012, 0.015, 0.022, 1.0)
bsdf_g.inputs['Roughness'].default_value = 0.12
bsdf_g.inputs['Coat Weight'].default_value = 0.85
bsdf_g.inputs['Coat Roughness'].default_value = 0.06
ground.data.materials.append(mat_ground)

# 3-Point Studio Lights
lights = [
    ("Key_Light", 'AREA', (2.4, -3.2, 3.4), 180.0, (1.0, 0.98, 0.95), (1.4, 1.4)),
    ("Fill_Light", 'AREA', (-3.0, -2.4, 2.6), 85.0, (0.85, 0.92, 1.0), (1.8, 1.8)),
    ("Rim_Light_Top", 'AREA', (0.0, 3.2, 3.6), 220.0, (1.0, 0.92, 0.82), (1.6, 1.6)),
    ("Under_Bounce", 'AREA', (0.0, -1.2, 0.08), 24.0, (0.6, 0.7, 0.8), (2.0, 2.0))
]

for l_name, l_type, l_pos, l_power, l_color, l_size in lights:
    light_data = bpy.data.lights.new(name=l_name, type=l_type)
    light_data.energy = l_power
    light_data.color = l_color
    if l_type == 'AREA':
        light_data.size = l_size[0]
        light_data.size_y = l_size[1]
    light_obj = bpy.data.objects.new(l_name, light_data)
    light_obj.location = l_pos
    target = Vector((0, 0, 1.15))
    dir_l = (target - Vector(l_pos)).normalized()
    light_obj.rotation_euler = Vector((0, 0, -1)).rotation_difference(dir_l).to_euler()
    bpy.context.scene.collection.objects.link(light_obj)

# -----------------------------------------------------------------------------
# 13. Camera Setup & Multi-Angle Renders
# -----------------------------------------------------------------------------
cam_data = bpy.data.cameras.new("HeroCamera")
cam_obj = bpy.data.objects.new("HeroCamera", cam_data)
bpy.context.scene.collection.objects.link(cam_obj)
scene.camera = cam_obj

views = [
    (
        "combat_front.png",
        Vector((0.0, -3.8, 1.25)),
        Euler((math.radians(88), 0, 0), 'XYZ'),
        52.0,
        "Combat Ant Front Heroic Brawler Stance"
    ),
    (
        "combat_perspective.png",
        Vector((-2.8, -3.2, 1.40)),
        Euler((math.radians(82), 0, math.radians(-40)), 'XYZ'),
        65.0,
        "Combat Ant 3/4 Depth Perspective"
    ),
    (
        "combat_side.png",
        Vector((-3.8, 0.0, 1.25)),
        Euler((math.radians(90), 0, math.radians(-90)), 'XYZ'),
        52.0,
        "Combat Ant Profile View"
    ),
    (
        "combat_top.png",
        Vector((0.0, 0.0, 4.2)),
        Euler((0, 0, 0), 'XYZ'),
        55.0,
        "Combat Ant Top View"
    ),
    (
        "combat_gameplay_angle.png",
        Vector((0.0, -6.2, 5.6)),
        Euler((math.radians(56), 0, 0), 'XYZ'),
        85.0,
        "Authentic 1998 RTS Gameplay Angled Top-Down South Perspective"
    )
]

for filename, pos, rot, lens, desc in views:
    cam_obj.location = pos
    cam_obj.rotation_euler = rot
    cam_data.lens = lens
    out_path = os.path.join(web_dir, filename)
    scene.render.filepath = out_path
    print(f"Rendering: {desc} -> {out_path}...")
    bpy.ops.render.render(write_still=True)
    print(f"Saved: {out_path}")

# -----------------------------------------------------------------------------
# 14. GLTF 2.0 Binary Export (combat_ant.glb) & USDZ
# -----------------------------------------------------------------------------
glb_path = os.path.join(web_dir, "combat_ant.glb")
for obj in combat_col.objects:
    if obj.type == 'MESH' and len(obj.data.uv_layers) == 0:
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.uv.smart_project(angle_limit=66.0, island_margin=0.02)
        bpy.ops.object.mode_set(mode='OBJECT')
        obj.select_set(False)

bpy.ops.object.select_all(action='DESELECT')
for obj in combat_col.objects:
    obj.select_set(True)

bpy.ops.export_scene.gltf(
    filepath=glb_path,
    export_format='GLB',
    use_selection=True,
    export_apply=False,
    export_animations=True
)
print(f"Combat Ant 3D GLB export complete: {glb_path}")

usdz_path = os.path.join(web_dir, "combat_ant.usdz")
print(f"Exporting Combat Ant USDZ to: {usdz_path}...")
bpy.ops.wm.usd_export(
    filepath=usdz_path,
    selected_objects_only=True,
    export_textures=True
)
print(f"Combat Ant USDZ export complete: {usdz_path}")

blend_path = "/Users/dchadd/Desktop/Ants-Mac/tools/blender/combat_ant_authentic.blend"
bpy.ops.wm.save_as_mainfile(filepath=blend_path)
print(f"Saved Blender file: {blend_path}")
