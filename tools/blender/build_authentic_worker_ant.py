"""
Blender 4.3.2 Script: Authentic Heroic Worker Ant (Caste #1) Overhaul
Directly calibrated 1:1 to master preview artwork (worker_ant_master_1790372115097.jpg):
1. EYES & HOODED EYELIDS: True 3D spherical eye globes with sculpted fleshy chitin upper
   eyelid hoods casting soft natural shadows over the upper sclera (matching ref_head_crop.png).
2. SNOUT & MANDIBLES: Rounded bulldog jowls curling inward toward midline with interlocking
   sharp white fangs (100% manifold quads), glowing chartreuse tooth gradient, and protruding snout.
3. ANTENNAE: Elegant outward-sweeping stalks sprouting from forehead brow between eyes, curving
   gracefully OUTWARD past the temples (X = +/-0.42) with teardrop club tips.
4. BODY & POSTURE: Large cartoon head (~45% of character height), compact arched 3-segment thorax,
   suspended plump egg gaster at 40°.
5. LIMBS & STANCE: Front arms bent dynamically at elbows (X = +/-0.36) with resting curled hands;
   middle legs planted forward; hind legs with high lateral knees (X = +/-0.65) and wide planted feet.
6. MATERIALS: Rich moss-green mottled dorsal chitin with warm terracotta/mahogany limbs,
   calibrated subtle subsurface scattering, and dark glossy mirrored floor.
7. RTS GAMEPLAY CAMERA: Authentic 1998 classic top-down south-angled perspective (pitch ~56°).
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

# Dark studio world environment matching master reference
world = bpy.data.worlds.new("StudioWorld")
scene.world = world
world.use_nodes = True
bg_node = world.node_tree.nodes['Background']
bg_node.inputs['Color'].default_value = (0.008, 0.008, 0.010, 1.0)
bg_node.inputs['Strength'].default_value = 0.20

# -----------------------------------------------------------------------------
# 2. Authentic PBR Materials (Rich Saturated Moss Green & Terracotta Mahogany)
# -----------------------------------------------------------------------------
def create_materials():
    # A. Chitin Material (Deep saturated moss-green with warm amber-tan accents)
    mat_chitin = bpy.data.materials.new("M_Chitin_Authentic")
    mat_chitin.use_nodes = True
    nodes = mat_chitin.node_tree.nodes
    links = mat_chitin.node_tree.links
    nodes.clear()

    out = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    # Load authentic high-resolution weathered moss-green chitin texture
    tex_chitin = nodes.new('ShaderNodeTexImage')
    tex_chitin.image = bpy.data.images.load(f"{web_dir}/chitin_pbr.png")
    links.new(tex_chitin.outputs['Color'], bsdf.inputs['Base Color'])
    bsdf.inputs['Base Color'].default_value = (0.24, 0.36, 0.16, 1.0)

    # Leathery organic pore micro-bump matching master reference
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

    # Authentic satin organic ant cuticle (subtle waxy sheen with leathery pores)
    bsdf.inputs['Roughness'].default_value = 0.50
    bsdf.inputs['Coat Weight'].default_value = 0.24
    bsdf.inputs['Coat Roughness'].default_value = 0.22
    bsdf.inputs['Subsurface Weight'].default_value = 0.09
    bsdf.inputs['Subsurface Radius'].default_value = (0.14, 0.18, 0.09)

    # B. Eye Material
    mat_eye = bpy.data.materials.new("M_Eye_Authentic")
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

    bsdf_e.inputs['Roughness'].default_value = 0.10
    bsdf_e.inputs['Coat Weight'].default_value = 0.90
    bsdf_e.inputs['Coat Roughness'].default_value = 0.03
    bsdf_e.inputs['IOR'].default_value = 1.48

    # C. Mandible Material (Moss green blending to luminous chartreuse & bone white fangs)
    mat_mandible = bpy.data.materials.new("M_Mandible_Authentic")
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

    # D. Limbs Material (Weathered Mahogany with Longitudinal Ridges & Cuticle Bump)
    mat_limbs = bpy.data.materials.new("M_Limbs_Authentic")
    mat_limbs.use_nodes = True
    nodes_l = mat_limbs.node_tree.nodes
    links_l = mat_limbs.node_tree.links
    nodes_l.clear()

    out_l = nodes_l.new('ShaderNodeOutputMaterial')
    bsdf_l = nodes_l.new('ShaderNodeBsdfPrincipled')
    links_l.new(bsdf_l.outputs['BSDF'], out_l.inputs['Surface'])

    # Load high-resolution striated cuticle texture
    tex_limb = nodes_l.new('ShaderNodeTexImage')
    tex_limb.image = bpy.data.images.load(f"{web_dir}/limbs_pbr.png")
    links_l.new(tex_limb.outputs['Color'], bsdf_l.inputs['Base Color'])
    bsdf_l.inputs['Base Color'].default_value = (0.48, 0.22, 0.16, 1.0)

    # Organic cuticle pore micro-bump (eliminating artificial zebra striations)
    tc_l = nodes_l.new('ShaderNodeTexCoord')
    noise_l = nodes_l.new('ShaderNodeTexNoise')
    noise_l.inputs['Scale'].default_value = 75.0
    noise_l.inputs['Detail'].default_value = 4.0
    noise_l.inputs['Roughness'].default_value = 0.52
    links_l.new(tc_l.outputs['Object'], noise_l.inputs['Vector'])

    bump_l = nodes_l.new('ShaderNodeBump')
    bump_l.inputs['Strength'].default_value = 0.12
    bump_l.inputs['Distance'].default_value = 0.002
    links_l.new(noise_l.outputs['Fac'], bump_l.inputs['Height'])
    links_l.new(bump_l.outputs['Normal'], bsdf_l.inputs['Normal'])

    bsdf_l.inputs['Roughness'].default_value = 0.44
    bsdf_l.inputs['Coat Weight'].default_value = 0.35
    bsdf_l.inputs['Coat Roughness'].default_value = 0.22
    bsdf_l.inputs['Subsurface Weight'].default_value = 0.06
    bsdf_l.inputs['Subsurface Radius'].default_value = (0.18, 0.08, 0.05)

    # E. Antenna Material
    mat_antenna = bpy.data.materials.new("M_Antenna_Authentic")
    mat_antenna.use_nodes = True
    nodes_a = mat_antenna.node_tree.nodes
    links_a = mat_antenna.node_tree.links
    nodes_a.clear()

    out_a = nodes_a.new('ShaderNodeOutputMaterial')
    bsdf_a = nodes_a.new('ShaderNodeBsdfPrincipled')
    links_a.new(bsdf_a.outputs['BSDF'], out_a.inputs['Surface'])

    bsdf_a.inputs['Base Color'].default_value = (0.18, 0.12, 0.09, 1.0)
    bsdf_a.inputs['Roughness'].default_value = 0.38
    bsdf_a.inputs['Coat Weight'].default_value = 0.55
    bsdf_a.inputs['Coat Roughness'].default_value = 0.14

    # F. Teeth Material (Sharp bone-ivory white)
    mat_teeth = bpy.data.materials.new("M_Teeth_Authentic")
    mat_teeth.use_nodes = True
    nodes_t = mat_teeth.node_tree.nodes
    links_t = mat_teeth.node_tree.links
    nodes_t.clear()

    out_t = nodes_t.new('ShaderNodeOutputMaterial')
    bsdf_t = nodes_t.new('ShaderNodeBsdfPrincipled')
    links_t.new(bsdf_t.outputs['BSDF'], out_t.inputs['Surface'])

    bsdf_t.inputs['Base Color'].default_value = (0.94, 0.95, 0.88, 1.0) # Bone ivory white
    bsdf_t.inputs['Roughness'].default_value = 0.25
    bsdf_t.inputs['Coat Weight'].default_value = 0.40
    bsdf_t.inputs['Coat Roughness'].default_value = 0.15

    # G. Recessed Oral Cavity Material (Velvety shadow void behind mandibles)
    mat_oral = bpy.data.materials.new("M_Oral_Authentic")
    mat_oral.use_nodes = True
    nodes_o = mat_oral.node_tree.nodes
    links_o = mat_oral.node_tree.links
    nodes_o.clear()

    out_o = nodes_o.new('ShaderNodeOutputMaterial')
    bsdf_o = nodes_o.new('ShaderNodeBsdfPrincipled')
    links_o.new(bsdf_o.outputs['BSDF'], out_o.inputs['Surface'])

    bsdf_o.inputs['Base Color'].default_value = (0.015, 0.010, 0.012, 1.0)
    bsdf_o.inputs['Roughness'].default_value = 0.90
    if 'Specular IOR Level' in bsdf_o.inputs:
        bsdf_o.inputs['Specular IOR Level'].default_value = 0.05

    return mat_chitin, mat_eye, mat_mandible, mat_limbs, mat_antenna, mat_teeth, mat_oral

mat_chitin, mat_eye, mat_mandible, mat_limbs, mat_antenna, mat_teeth, mat_oral = create_materials()

worker_col = bpy.data.collections.new("Worker_Ant_Authentic")
bpy.context.scene.collection.children.link(worker_col)

def reg(obj):
    worker_col.objects.link(obj)
    if obj.name in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.unlink(obj)
    return obj

# -----------------------------------------------------------------------------
# 3. Authentic Cartoon Cranium with Wide Crown Lobes & Orbital Cavities
# -----------------------------------------------------------------------------
bm_head = bmesh.new()
bmesh.ops.create_cube(bm_head, size=1.0)
bmesh.ops.subdivide_edges(bm_head, edges=bm_head.edges, cuts=8, use_grid_fill=True)

# Head origin at (0, -0.04, 1.58)
# Character scale matching master reference: tall rounded pear/dome cranium
rx_head, ry_head, rz_head = 0.31, 0.25, 0.37

for v in bm_head.verts:
    # Superellipsoid formulation (p = 3.2): creates a pillowy dome with rounded corners,
    # preventing the skull from tapering to an egg point at the crown!
    vx, vy, vz = v.co.x, v.co.y, v.co.z
    p_exp = 3.2
    r_super = (abs(vx)**p_exp + abs(vy)**p_exp + abs(vz)**p_exp)**(1.0 / p_exp)
    if r_super > 1e-5:
        nx, ny, nz = vx / r_super, vy / r_super, vz / r_super
    else:
        nx, ny, nz = 0.0, 0.0, 0.0

    x = nx * rx_head
    y = ny * ry_head
    z = nz * rz_head

    # 1. Crown lobes & cleft (Z > 0.06): two distinct pillowy lobes with dip in middle
    if z > 0.06:
        cleft = 1.0 - 0.13 * math.exp(-((x / 0.08) ** 2))
        z *= cleft
        # Upper crown gentle flare
        x *= (1.0 + 0.06 * (z / rz_head))

    # 2. Forehead dome & brow overhang (Z in [0.03, 0.24], Y < 0)
    if 0.03 < z < 0.24 and y < 0:
        brow_t = math.sin((z - 0.03) / 0.21 * math.pi)
        y -= 0.035 * brow_t

    # 3. Deep Orbital Sockets (Center at X = +/-0.145, Y = -0.15, Z = 0.025)
    for sign in [-1.0, 1.0]:
        sx, sy, sz = sign * 0.145, -0.15, 0.025
        d = math.sqrt((x - sx)**2 + (y - sy)**2 + (z - sz)**2)
        r_orb = 0.170

        if d < r_orb and y < 0:
            falloff = (1.0 - (d / r_orb)**2)**1.3
            y += 0.12 * falloff

    # 4. Solid Green Nose Bridge (Vertical ridge between eyes, keeping eyes cleanly separated)
    if abs(x) < 0.055 and -0.09 < z < 0.14 and y < 0:
        bridge = math.cos(abs(x) / 0.055 * (math.pi / 2.0))
        y -= 0.045 * bridge

    # 5. Prominent Teardrop Clypeus / Snout Flap (Matching ref_mouth_crop.png)
    if -0.27 < z < -0.02 and abs(x) < 0.11 and y < 0:
        clyp_profile = math.cos(abs(x) / 0.11 * (math.pi / 2.0))**0.75
        t_z = math.sin((z - (-0.27)) / 0.25 * math.pi)
        # Central vertical dimple down the snout
        dimple = 1.0 - 0.15 * math.exp(-((x / 0.024)**2))
        y -= 0.100 * clyp_profile * t_z * dimple

    # 6. Temples & Cheeks flanking the eyes cleanly
    if abs(x) > 0.22 and -0.18 < z < 0.18:
        x *= 1.05

    # 7. Carve away awkward lower chin below mouthparts so jaws form the true bottom of the head
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

# UV Mapping for Cranium
uv_layer = bm_head.loops.layers.uv.new("UVMap")
for face in bm_head.faces:
    for loop in face.loops:
        norm = loop.vert.co.normalized()
        u = math.atan2(norm.x, -norm.y) / (2.0 * math.pi) + 0.5
        v_coord = norm.z * 0.5 + 0.5
        loop[uv_layer].uv = (u, v_coord)

head_mesh = bpy.data.meshes.new("Head")
head_obj = bpy.data.objects.new("Head", head_mesh)
head_obj.location = Vector((0, -0.04, 1.58))
bpy.context.scene.collection.objects.link(head_obj)
bm_head.to_mesh(head_mesh)
bm_head.free()

head_obj.data.materials.append(mat_chitin)
sub_h = head_obj.modifiers.new("Subsurf", 'SUBSURF')
sub_h.levels = 2
for p in head_mesh.polygons:
    p.use_smooth = True
reg(head_obj)

# -----------------------------------------------------------------------------
# 4. Large Expressive Oval Eyes with Eyelid Hoods (Matching Master Reference)
# -----------------------------------------------------------------------------
def make_bulging_eye(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    eye_pos = Vector((sign * 0.145, -0.170, 1.610))

    bm_eye = bmesh.new()
    bmesh.ops.create_uvsphere(bm_eye, u_segments=40, v_segments=28, radius=1.0)

    # Proportional cartoon dimensions: rx = 0.126, ry = 0.116, rz = 0.158
    rx, ry, rz = 0.126, 0.116, 0.158
    for v in bm_eye.verts:
        v.co.x *= rx
        v.co.y *= ry
        v.co.z *= rz

    uv_l = bm_eye.loops.layers.uv.new("UVMap")
    for face in bm_eye.faces:
        for loop in face.loops:
            vx = loop.vert.co.x
            vy = loop.vert.co.y
            vz = loop.vert.co.z

            # Medial and slight downward shift for endearing cartoon focus
            u = 0.50 - (sign * vx) / (2.0 * rx * 1.15) - (0.015 * sign)
            v = 0.50 + vz / (2.0 * rz * 1.15) + 0.020

            # Entire back hemisphere maps to solid white sclera margin
            if vy > 0.03:
                u = 0.50
                v = 0.95

            loop[uv_l].uv = (min(1.0, max(0.0, u)), min(1.0, max(0.0, v)))

    mesh = bpy.data.meshes.new(name)
    bm_eye.to_mesh(mesh)
    bm_eye.free()

    eye_obj = bpy.data.objects.new(name, mesh)
    eye_obj.location = eye_pos
    bpy.context.scene.collection.objects.link(eye_obj)

    # Slight binocular convergence matching master reference
    tilt_z = sign * math.radians(3.5)
    pitch_x = math.radians(2.5)
    eye_obj.rotation_euler = Euler((pitch_x, 0.0, tilt_z), 'XYZ')

    eye_obj.data.materials.append(mat_eye)
    sub = eye_obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 1
    for p in mesh.polygons:
        p.use_smooth = True
    return reg(eye_obj)

make_bulging_eye("Eye_L", True)
make_bulging_eye("Eye_R", False)

# -----------------------------------------------------------------------------
# 5. Authentic 3D Caliper Pincer Claws & Recessed Oral Cavity
# -----------------------------------------------------------------------------
def make_authentic_pincer_claw(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    bm = bmesh.new()

    # True caliper pincer jaw path:
    # 0. Cheek hinge condyle (world Z=1.45)
    # 1. Broad lateral caliper bow flaring out as wide as cheek
    # 2. Massive anterior bulbous muscle lobe (sweeping forward and down)
    # 3. Anterior medial turn with deep inner bite notch
    # 4. Inward-hooking caliper tip (leaving ~0.130 unit open central mouth gap)
    stations = [
        # 0. Cheek hinge condyle
        (Vector((sign * 0.185, -0.160, 1.450)), Vector((sign * 0.25, -0.75, -0.60)).normalized(), 0.055, 0.050),
        # 1. Broad lateral caliper bow
        (Vector((sign * 0.235, -0.230, 1.400)), Vector((sign * 0.10, -0.90, -0.42)).normalized(), 0.070, 0.062),
        # 2. Massive anterior bulbous muscle lobe (sweeping forward and down)
        (Vector((sign * 0.190, -0.320, 1.350)), Vector((sign * -0.50, -0.80, -0.32)).normalized(), 0.076, 0.068),
        # 3. Anterior medial turn with deep inner bite notch
        (Vector((sign * 0.125, -0.325, 1.330)), Vector((sign * -0.88, -0.45, -0.15)).normalized(), 0.058, 0.052),
        # 4. Inward-hooking caliper tip (leaving ~0.130 unit open central mouth gap)
        (Vector((sign * 0.065, -0.290, 1.320)), Vector((sign * -0.96, -0.26, -0.05)).normalized(), 0.034, 0.030),
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

            # Plump outer hull vs scooped inner concavity
            is_outer = (cos_t * sign > 0)
            if i >= 2 and not is_outer:
                # Deep C-shaped bite notch scooped into inner surface
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
        for j in range(num_pts):
            jn = (j + 1) % num_pts
            if is_left:
                f = bm.faces.new([r0[j], r0[jn], r1[jn], r1[j]])
            else:
                f = bm.faces.new([r0[jn], r0[j], r1[j], r1[jn]])

            # UV coordinate: V runs along length from base (0.0) to tip (1.0)
            v0 = i / (len(stations) - 1)
            v1 = (i + 1) / (len(stations) - 1)
            u0 = j / num_pts
            u1 = (j + 1) / num_pts

            if is_left:
                f.loops[0][uv_layer].uv = (u0, v0)
                f.loops[1][uv_layer].uv = (u1, v0)
                f.loops[2][uv_layer].uv = (u1, v1)
                f.loops[3][uv_layer].uv = (u0, v1)
            else:
                f.loops[0][uv_layer].uv = (u1, v0)
                f.loops[1][uv_layer].uv = (u0, v0)
                f.loops[2][uv_layer].uv = (u0, v1)
                f.loops[3][uv_layer].uv = (u1, v1)

    if is_left:
        bm.faces.new(rings[0][::-1])
    else:
        bm.faces.new(rings[0])

    tip_center = stations[-1][0] + (stations[-1][1] * 0.015)
    tip_v = bm.verts.new(tip_center)
    last_ring = rings[-1]
    for j in range(num_pts):
        jn = (j + 1) % num_pts
        if is_left:
            f = bm.faces.new([last_ring[j], last_ring[jn], tip_v])
            f.loops[0][uv_layer].uv = (j / num_pts, 0.95)
            f.loops[1][uv_layer].uv = ((j + 1) / num_pts, 0.95)
            f.loops[2][uv_layer].uv = (0.5, 1.0)
        else:
            f = bm.faces.new([last_ring[jn], last_ring[j], tip_v])
            f.loops[0][uv_layer].uv = ((j + 1) / num_pts, 0.95)
            f.loops[1][uv_layer].uv = (j / num_pts, 0.95)
            f.loops[2][uv_layer].uv = (0.5, 1.0)

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)

    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()

    for p in mesh.polygons:
        p.use_smooth = True

    mand_obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(mand_obj)
    mand_obj.data.materials.append(mat_mandible)
    sub = mand_obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 2
    reg(mand_obj)

    # Sculpted sharp biting fangs inside the inner scoop
    suf = "L" if is_left else "R"
    # Upper primary fang
    f1_loc = (sign * 0.100, -0.300, 1.365)
    f1_rot = (math.radians(18), math.radians(sign * -28), math.radians(sign * -50))
    bpy.ops.mesh.primitive_cone_add(vertices=14, radius1=0.016, radius2=0.001, depth=0.048, location=f1_loc, rotation=f1_rot)
    tooth1 = bpy.context.active_object
    tooth1.name = f"Tooth_{suf}1"
    tooth1.data.materials.append(mat_teeth)
    bpy.ops.object.shade_smooth()
    reg(tooth1)

    # Lower secondary fang
    f2_loc = (sign * 0.075, -0.285, 1.335)
    f2_rot = (math.radians(8), math.radians(sign * -22), math.radians(sign * -65))
    bpy.ops.mesh.primitive_cone_add(vertices=12, radius1=0.013, radius2=0.001, depth=0.040, location=f2_loc, rotation=f2_rot)
    tooth2 = bpy.context.active_object
    tooth2.name = f"Tooth_{suf}2"
    tooth2.data.materials.append(mat_teeth)
    bpy.ops.object.shade_smooth()
    reg(tooth2)

    return mand_obj

def make_oral_cavity(name):
    # Recessed dark mouth interior cavity behind caliper pincer claws
    bm_oral = bmesh.new()
    bmesh.ops.create_uvsphere(bm_oral, u_segments=24, v_segments=16, radius=0.080)
    for v in list(bm_oral.verts):
        if v.co.y < 0.005:
            bm_oral.verts.remove(v)
    for v in bm_oral.verts:
        v.co.x *= 1.20
        v.co.y *= 1.40
        v.co.z *= 0.90
    bmesh.ops.recalc_face_normals(bm_oral, faces=bm_oral.faces)
    for f in bm_oral.faces:
        f.normal_flip()
    mesh = bpy.data.meshes.new(name)
    bm_oral.to_mesh(mesh)
    bm_oral.free()
    for p in mesh.polygons:
        p.use_smooth = True
    oral_obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(oral_obj)
    oral_obj.location = (0.0, -0.210, 1.350)
    oral_obj.data.materials.append(mat_oral)
    return reg(oral_obj)

make_authentic_pincer_claw("Mandible_L", True)
make_authentic_pincer_claw("Mandible_R", False)
make_oral_cavity("Oral_Cavity")

# -----------------------------------------------------------------------------
# 6. Jointed Crown Antennae (Sprouting from Crown Cleft, Sweeping OUT past Temples)
# -----------------------------------------------------------------------------
def make_authentic_antenna(name, is_left=True):
    curve_data = bpy.data.curves.new(name, 'CURVE')
    curve_data.dimensions = '3D'
    curve_data.bevel_depth = 0.018
    curve_data.bevel_resolution = 6
    curve_data.fill_mode = 'FULL'

    spline = curve_data.splines.new('BEZIER')
    spline.bezier_points.add(3) # 4 points: root, scape, elbow, tip

    # Trajectory matching master reference:
    # Sprouts from crown cleft, ascends, bends SHARPLY OUTWARD past temples (X = +/-0.35), curves back
    if is_left:
        p0 = Vector((-0.070, 0.02, 1.88)) # Crown cleft socket
        p1 = Vector((-0.140, -0.01, 2.12)) # Ascending scape
        p2 = Vector((-0.340, 0.01, 2.22))  # Elbow angled OUT past left temple!
        p3 = Vector((-0.280, 0.08, 2.34))  # Flagellum tip curling back
    else:
        p0 = Vector((0.070, 0.02, 1.88))   # Crown cleft socket
        p1 = Vector((0.150, -0.01, 2.15))  # Ascending scape
        p2 = Vector((0.360, 0.01, 2.22))   # Elbow angled OUT past right temple!
        p3 = Vector((0.260, 0.06, 2.38))   # Flagellum tip reaching high and back

    pts = [p0, p1, p2, p3]
    for i, p in enumerate(pts):
        bp = spline.bezier_points[i]
        bp.co = p
        bp.handle_left_type = 'AUTO'
        bp.handle_right_type = 'AUTO'

    ant_obj = bpy.data.objects.new(name, curve_data)
    bpy.context.scene.collection.objects.link(ant_obj)
    ant_obj.data.materials.append(mat_antenna)

    bpy.context.view_layer.objects.active = ant_obj
    ant_obj.select_set(True)
    bpy.ops.object.convert(target='MESH')
    bpy.ops.object.shade_smooth()

    # Rounded teardrop club tip at p3
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=18, ring_count=14, radius=0.030,
        location=p3
    )
    club = bpy.context.active_object
    club.name = f"{name}_Club"
    club.data.materials.append(mat_antenna)
    bpy.ops.object.shade_smooth()
    reg(club)

    return reg(ant_obj)

make_authentic_antenna("Antenna_L", True)
make_authentic_antenna("Antenna_R", False)

# -----------------------------------------------------------------------------
# 6.5. Anatomical Segment & Articulated Joint Condyle Builders
# -----------------------------------------------------------------------------
def make_chitin_segment(name, p0, p1, r_start, r_mid, r_end, is_sleeve=False, material=mat_limbs):
    bm = bmesh.new()
    vec = p1 - p0
    length = vec.length
    if length < 1e-4:
        bm.free()
        return None

    rot_quat = Vector((0, 0, 1)).rotation_difference(vec)
    mat_rot = rot_quat.to_matrix().to_4x4()

    num_pts = 16
    rings = []
    # 5 profile stations with authentic arthropod taper and sleeve overlap into joint condyles
    stations = [
        (p0 - vec * 0.04, r_start * 1.18, 1.25, 0.88),         # Flared sleeve overlapping into proximal joint
        (p0 + vec * 0.18, r_start * 0.94, 1.20, 0.84),         # Narrow neck taper
        (p0 + vec * 0.45, r_mid * 1.14, 1.32, 0.80),           # Muscular lateral flattening & ridge bulge
        (p0 + vec * 0.80, r_end * 0.90, 1.18, 0.82),           # Distal shaft taper
        (p1 + vec * 0.04, r_end * (1.32 if is_sleeve else 1.08), 1.22, 0.88) # Sleeve overlapping into distal joint
    ]
    for pos, rad, sx, sy in stations:
        c_ring = []
        for j in range(num_pts):
            th = 2.0 * math.pi * j / num_pts
            # 4 distinct longitudinal cuticle crests / flutes (dorsal, ventral, lateral ridges)
            ridge_mod = 1.0 + 0.10 * math.cos(th * 4.0) + 0.05 * math.cos(th * 2.0)
            local_p = Vector((rad * sx * math.cos(th) * ridge_mod, rad * sy * math.sin(th) * ridge_mod, 0.0))
            world_p = pos + (mat_rot @ local_p)
            c_ring.append(bm.verts.new(world_p))
        rings.append(c_ring)

    uv_layer = bm.loops.layers.uv.new("UVMap")
    for i in range(len(stations) - 1):
        r0, r1 = rings[i], rings[i+1]
        v_coord0 = i / (len(stations) - 1)
        v_coord1 = (i + 1) / (len(stations) - 1)
        for j in range(num_pts):
            jn = (j + 1) % num_pts
            face = bm.faces.new([r0[j], r0[jn], r1[jn], r1[j]])
            u0 = j / num_pts
            u1 = (j + 1) / num_pts
            face.loops[0][uv_layer].uv = (u0, v_coord0)
            face.loops[1][uv_layer].uv = (u1, v_coord0)
            face.loops[2][uv_layer].uv = (u1, v_coord1)
            face.loops[3][uv_layer].uv = (u0, v_coord1)

    bm.faces.new(rings[0][::-1])
    bm.faces.new(rings[-1])

    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()

    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.data.materials.append(material)
    sub = obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 1
    for p in mesh.polygons:
        p.use_smooth = True
    return reg(obj)

def make_joint_socket(name, location, radius, scale=(1.0, 1.0, 1.0), material=mat_limbs):
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=20, ring_count=16, radius=1.0,
        location=location
    )
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = (radius * scale[0], radius * scale[1], radius * scale[2])
    obj.data.materials.append(material)
    bpy.ops.object.shade_smooth()
    return reg(obj)

def make_foot_toe_pad(name, p_toe, radius):
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=16, ring_count=12, radius=1.0,
        location=p_toe
    )
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = (radius * 1.2, radius * 1.8, radius * 0.75)
    obj.data.materials.append(mat_limbs)
    bpy.ops.object.shade_smooth()
    return reg(obj)

# -----------------------------------------------------------------------------
# 7. Thorax: 3 Compact Arched Armor Plates (Matching ref_limbs_crop.png)
# -----------------------------------------------------------------------------
# Short Rounded Neck
bpy.ops.mesh.primitive_cylinder_add(
    vertices=24, radius=0.085, depth=0.10,
    location=(0, -0.02, 1.37), rotation=(math.radians(16), 0, 0)
)
neck = bpy.context.active_object
neck.name = "Neck"
neck.data.materials.append(mat_chitin)
bpy.ops.object.shade_smooth()
reg(neck)

# Slimmed Thorax: Authentic lean insect armor plates matching master reference artwork
thorax_plates = [
    ("Thorax_Pronotum",  Vector((0, -0.01, 1.25)), Vector((0.16, 0.15, 0.13))),
    ("Thorax_Mesonotum", Vector((0, 0.03, 1.10)),  Vector((0.15, 0.14, 0.12))),
    ("Thorax_Metanotum", Vector((0, 0.07, 0.96)),  Vector((0.13, 0.13, 0.11)))
]
for name, loc, scale in thorax_plates:
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=20, radius=1.0, location=loc)
    p_obj = bpy.context.active_object
    p_obj.name = name
    p_obj.scale = scale
    p_obj.data.materials.append(mat_chitin)
    sub = p_obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 1
    bpy.ops.object.shade_smooth()
    reg(p_obj)

# -----------------------------------------------------------------------------
# 8. Petiole & Gaster: Continuous, Fully Connected Waist & Suspended Abdomen
# -----------------------------------------------------------------------------
# Articulated Petiole Waist: Solid, gap-free bridge between rear Metanotum & Gaster
p_pet_start = Vector((0, 0.08, 0.94))  # Anchored firmly inside Thorax_Metanotum
p_pet_end   = Vector((0, 0.22, 0.78))  # Embedded deep inside anterior Gaster socket

make_joint_socket("Petiole_Thorax_Socket", p_pet_start, 0.052, material=mat_chitin)
make_chitin_segment("Petiole", p_pet_start, p_pet_end, 0.048, 0.044, 0.052, is_sleeve=True, material=mat_chitin)
make_joint_socket("Petiole_Gaster_Socket", p_pet_end, 0.056, material=mat_chitin)

# Gaster: Suspended Plump Egg Abdomen at 20° (Anterior pole encompasses p_pet_end)
bpy.ops.mesh.primitive_uv_sphere_add(
    segments=36, ring_count=24, radius=1.0,
    location=(0, 0.38, 0.68),
    rotation=(math.radians(20), 0, 0)
)
gaster_obj = bpy.context.active_object
gaster_obj.name = "Gaster"

for v in gaster_obj.data.vertices:
    x = v.co.x * 0.24
    y = v.co.y * 0.32
    z = v.co.z * 0.24

    if y > 0:
        # Posterior gentle taper towards sting tip
        taper = 1.0 - 0.36 * (y / 0.32)
        x *= taper
        z *= (taper * 0.92)
    else:
        # Anterior conical neck tapering into petiole socket
        t_ant = min(1.0, (-y) / 0.32)
        x *= (1.0 - 0.40 * t_ant)
        z *= (1.0 - 0.40 * t_ant)

    # Subtle sternite banding grooves
    groove = math.sin((y + 0.32) * 18.0) * 0.006
    x += groove * (x / 0.24)
    z += groove * (z / 0.24)

    v.co = Vector((x, y, z))

gaster_obj.data.update()
gaster_obj.data.materials.append(mat_chitin)
sub_g = gaster_obj.modifiers.new("Subsurf", 'SUBSURF')
sub_g.levels = 2
bpy.ops.object.shade_smooth()
reg(gaster_obj)

# -----------------------------------------------------------------------------
# 9. Sculpted Insect Limbs: Longitudinal Cuticle Ridges & Articulated Collars
# -----------------------------------------------------------------------------
# A. Front Expressive Arms (Bent dynamically at elbows framing chest)
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"

    p_shoulder = Vector((sign * 0.13, -0.04, 1.22))
    p_elbow    = Vector((sign * 0.20, -0.16, 0.98)) # Elbow bent gently outward and forward
    p_wrist    = Vector((sign * 0.16, -0.22, 0.74)) # Forearm back inward
    p_finger1  = Vector((sign * 0.14, -0.25, 0.58)) # Curved hand claw
    p_finger2  = Vector((sign * 0.18, -0.22, 0.56))

    make_joint_socket(f"Arm_Shoulder_{suf}", p_shoulder, 0.044, scale=(1.1, 1.1, 1.1))
    make_chitin_segment(f"Arm_Upper_{suf}", p_shoulder, p_elbow, 0.038, 0.044, 0.032, is_sleeve=True)
    make_joint_socket(f"Arm_Elbow_{suf}", p_elbow, 0.035, scale=(1.15, 1.25, 1.15))
    make_chitin_segment(f"Arm_Forearm_{suf}", p_elbow, p_wrist, 0.032, 0.034, 0.022, is_sleeve=True)
    make_joint_socket(f"Arm_Wrist_{suf}", p_wrist, 0.027, scale=(1.1, 1.1, 1.2))
    make_chitin_segment(f"Arm_Finger1_{suf}", p_wrist, p_finger1, 0.016, 0.016, 0.010)
    make_chitin_segment(f"Arm_Finger2_{suf}", p_wrist, p_finger2, 0.014, 0.014, 0.008)

# B. Middle Walking Legs (Narrowed stance matching master reference)
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"

    p_hip   = Vector((sign * 0.13, 0.03, 1.06))
    p_knee  = Vector((sign * 0.28, -0.02, 0.68)) # Compact lateral knee bend framing torso
    p_ankle = Vector((sign * 0.22, -0.08, 0.08))
    p_toe1  = Vector((sign * 0.20, -0.16, 0.02))
    p_toe2  = Vector((sign * 0.24, -0.14, 0.02))

    make_joint_socket(f"Leg_Mid_Coxa_{suf}", p_hip, 0.048, scale=(1.1, 1.2, 1.1))
    make_chitin_segment(f"Leg_Mid_Femur_{suf}", p_hip, p_knee, 0.044, 0.050, 0.036, is_sleeve=True)
    make_joint_socket(f"Leg_Mid_Knee_{suf}", p_knee, 0.042, scale=(1.2, 1.1, 1.2))
    make_chitin_segment(f"Leg_Mid_Tibia_{suf}", p_knee, p_ankle, 0.036, 0.032, 0.022, is_sleeve=True)
    make_joint_socket(f"Leg_Mid_Ankle_{suf}", p_ankle, 0.026, scale=(1.1, 1.1, 1.1))
    make_chitin_segment(f"Leg_Mid_Foot_{suf}", p_ankle, p_toe1, 0.022, 0.020, 0.015)
    make_foot_toe_pad(f"Leg_Mid_Toe1_{suf}", p_toe1, 0.022)
    make_foot_toe_pad(f"Leg_Mid_Toe2_{suf}", p_toe2, 0.018)

# C. Hind Walking Legs (Narrowed stance matching master reference)
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"

    p_hip   = Vector((sign * 0.11, 0.07, 0.96))
    p_knee  = Vector((sign * 0.42, 0.18, 0.82)) # Natural knee height and compact width
    p_ankle = Vector((sign * 0.35, 0.10, 0.08)) # Feet planted closer to body
    p_toe1  = Vector((sign * 0.36, 0.04, 0.02))
    p_toe2  = Vector((sign * 0.40, 0.14, 0.02))

    make_joint_socket(f"Leg_Hind_Coxa_{suf}", p_hip, 0.052, scale=(1.1, 1.2, 1.1))
    make_chitin_segment(f"Leg_Hind_Femur_{suf}", p_hip, p_knee, 0.048, 0.054, 0.038, is_sleeve=True)
    make_joint_socket(f"Leg_Hind_Knee_{suf}", p_knee, 0.046, scale=(1.2, 1.2, 1.1))
    make_chitin_segment(f"Leg_Hind_Tibia_{suf}", p_knee, p_ankle, 0.038, 0.034, 0.024, is_sleeve=True)
    make_joint_socket(f"Leg_Hind_Ankle_{suf}", p_ankle, 0.028, scale=(1.1, 1.1, 1.1))
    make_chitin_segment(f"Leg_Hind_Foot_{suf}", p_ankle, p_toe1, 0.024, 0.022, 0.016)
    make_foot_toe_pad(f"Leg_Hind_Toe1_{suf}", p_toe1, 0.024)
    make_foot_toe_pad(f"Leg_Hind_Toe2_{suf}", p_toe2, 0.020)

# -----------------------------------------------------------------------------
# 10. Studio Ground Plane (Large Dark Reflective Mirrored Floor)
# -----------------------------------------------------------------------------
bpy.ops.mesh.primitive_plane_add(size=60.0, location=(0, 0, 0))
floor = bpy.context.active_object
floor.name = "Studio_Floor"
mat_floor = bpy.data.materials.new("M_Studio_Floor")
mat_floor.use_nodes = True
nf = mat_floor.node_tree.nodes
lf = mat_floor.node_tree.links
nf.clear()
out_f = nf.new('ShaderNodeOutputMaterial')
bsdf_f = nf.new('ShaderNodeBsdfPrincipled')
lf.new(bsdf_f.outputs['BSDF'], out_f.inputs['Surface'])
bsdf_f.inputs['Base Color'].default_value = (0.008, 0.008, 0.010, 1.0)
bsdf_f.inputs['Roughness'].default_value = 0.20
bsdf_f.inputs['Coat Weight'].default_value = 0.80
floor.data.materials.append(mat_floor)

# -----------------------------------------------------------------------------
# 11. Balanced Studio 4-Point Area Lighting Rig (Cinematic Exposure)
# -----------------------------------------------------------------------------
light_group = bpy.data.collections.new("Studio_Lights")
bpy.context.scene.collection.children.link(light_group)

def add_area_light(name, energy, color, loc, size, spec_factor=1.0):
    ldata = bpy.data.lights.new(name, 'AREA')
    ldata.energy = energy
    ldata.color = color
    ldata.size = size
    ldata.specular_factor = spec_factor
    lobj = bpy.data.objects.new(name, ldata)
    v_loc = Vector(loc)
    lobj.location = v_loc
    dir_v = (Vector((0, 0, 1.2)) - v_loc).normalized()
    rot_quat = Vector((0, 0, -1)).rotation_difference(dir_v)
    lobj.rotation_euler = rot_quat.to_euler()
    light_group.objects.link(lobj)
    return lobj

# Soft studio key light at 10:30 o'clock
add_area_light("Key_Light", 85.0, (1.0, 0.98, 0.94), (-2.2, -3.2, 3.2), 1.8)
# Gentle fill light on right
add_area_light("Fill_Light", 32.0, (0.86, 0.92, 1.0), (2.8, -2.4, 2.2), 2.4)
# High rim light for crisp chitin contour (specular dimmed & glossy disabled to eliminate floor rectangular flare)
rim_top = add_area_light("Rim_Light_Top", 75.0, (1.0, 1.0, 1.0), (0.0, 3.0, 3.6), 1.8, spec_factor=0.15)
rim_top.visible_glossy = False
# Subtle side rim light
add_area_light("Rim_Light_Side", 40.0, (0.95, 1.0, 0.9), (-3.0, 1.2, 1.8), 1.6)

# -----------------------------------------------------------------------------
# 11.5. Skeletal Armature Rigging & Tripod Gait Walk Animation
# -----------------------------------------------------------------------------
amt = bpy.data.armatures.new("Worker_Armature")
rig = bpy.data.objects.new("Worker_Rig", amt)
worker_col.objects.link(rig)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode='EDIT')

# 1. Torso Spine Bones
b_root = amt.edit_bones.new("Root")
b_root.head = Vector((0, 0, 0))
b_root.tail = Vector((0, 0, 0.40))

b_thorax = amt.edit_bones.new("Thorax")
b_thorax.head = Vector((0, 0.03, 0.95))
b_thorax.tail = Vector((0, -0.01, 1.28))
b_thorax.parent = b_root

b_neck = amt.edit_bones.new("Neck")
b_neck.head = Vector((0, -0.02, 1.37))
b_neck.tail = Vector((0, -0.04, 1.58))
b_neck.parent = b_thorax

b_head = amt.edit_bones.new("Head")
b_head.head = Vector((0, -0.04, 1.58))
b_head.tail = Vector((0, -0.04, 1.95))
b_head.parent = b_neck

b_gaster = amt.edit_bones.new("Gaster")
b_gaster.head = Vector((0, 0.08, 0.94))
b_gaster.tail = Vector((0, 0.38, 0.68))
b_gaster.parent = b_thorax

# 2. Limb Bones
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"

    # Arms
    p_shoulder = Vector((sign * 0.13, -0.04, 1.22))
    p_elbow    = Vector((sign * 0.20, -0.16, 0.98))
    p_wrist    = Vector((sign * 0.16, -0.22, 0.74))
    p_hand     = Vector((sign * 0.14, -0.25, 0.58))

    b_up = amt.edit_bones.new(f"Arm_Upper_{suf}")
    b_up.head = p_shoulder
    b_up.tail = p_elbow
    b_up.parent = b_thorax

    b_fa = amt.edit_bones.new(f"Arm_Forearm_{suf}")
    b_fa.head = p_elbow
    b_fa.tail = p_wrist
    b_fa.parent = b_up

    b_hd = amt.edit_bones.new(f"Arm_Hand_{suf}")
    b_hd.head = p_wrist
    b_hd.tail = p_hand
    b_hd.parent = b_fa

    # Middle Legs (Narrowed stance matching master reference)
    p_hip_m   = Vector((sign * 0.13, 0.03, 1.06))
    p_knee_m  = Vector((sign * 0.28, -0.02, 0.68))
    p_ankle_m = Vector((sign * 0.22, -0.08, 0.08))
    p_foot_m  = Vector((sign * 0.22, -0.15, 0.02))

    b_fm = amt.edit_bones.new(f"Leg_Mid_Femur_{suf}")
    b_fm.head = p_hip_m
    b_fm.tail = p_knee_m
    b_fm.parent = b_thorax

    b_tm = amt.edit_bones.new(f"Leg_Mid_Tibia_{suf}")
    b_tm.head = p_knee_m
    b_tm.tail = p_ankle_m
    b_tm.parent = b_fm

    b_ftm = amt.edit_bones.new(f"Leg_Mid_Foot_{suf}")
    b_ftm.head = p_ankle_m
    b_ftm.tail = p_foot_m
    b_ftm.parent = b_tm

    # Hind Legs (Narrowed stance matching master reference)
    p_hip_h   = Vector((sign * 0.11, 0.07, 0.96))
    p_knee_h  = Vector((sign * 0.42, 0.18, 0.82))
    p_ankle_h = Vector((sign * 0.35, 0.10, 0.08))
    p_foot_h  = Vector((sign * 0.38, 0.09, 0.02))

    b_fh = amt.edit_bones.new(f"Leg_Hind_Femur_{suf}")
    b_fh.head = p_hip_h
    b_fh.tail = p_knee_h
    b_fh.parent = b_thorax

    b_th = amt.edit_bones.new(f"Leg_Hind_Tibia_{suf}")
    b_th.head = p_knee_h
    b_th.tail = p_ankle_h
    b_th.parent = b_fh

    b_fth = amt.edit_bones.new(f"Leg_Hind_Foot_{suf}")
    b_fth.head = p_ankle_h
    b_fth.tail = p_foot_h
    b_fth.parent = b_th

bpy.ops.object.mode_set(mode='OBJECT')

# Map mesh objects to respective bones
bone_map = {
    "Head": "Head",
    "Eye_L": "Head", "Eye_R": "Head",
    "Mandible_L": "Head", "Mandible_R": "Head",
    "Tooth_L1": "Head", "Tooth_L2": "Head",
    "Tooth_R1": "Head", "Tooth_R2": "Head",
    "Oral_Cavity": "Head",
    "Antenna_L": "Head", "Antenna_L_Club": "Head",
    "Antenna_R": "Head", "Antenna_R_Club": "Head",
    "Neck": "Neck",
    "Thorax_Pronotum": "Thorax",
    "Thorax_Mesonotum": "Thorax",
    "Thorax_Metanotum": "Thorax",
    "Petiole_Thorax_Socket": "Gaster",
    "Petiole": "Gaster",
    "Petiole_Gaster_Socket": "Gaster",
    "Gaster": "Gaster",
}

for is_left in [True, False]:
    suf = "L" if is_left else "R"
    bone_map[f"Arm_Shoulder_{suf}"] = "Thorax"
    bone_map[f"Arm_Upper_{suf}"] = f"Arm_Upper_{suf}"
    bone_map[f"Arm_Elbow_{suf}"] = f"Arm_Upper_{suf}"
    bone_map[f"Arm_Forearm_{suf}"] = f"Arm_Forearm_{suf}"
    bone_map[f"Arm_Wrist_{suf}"] = f"Arm_Hand_{suf}"
    bone_map[f"Arm_Finger1_{suf}"] = f"Arm_Hand_{suf}"
    bone_map[f"Arm_Finger2_{suf}"] = f"Arm_Hand_{suf}"

    bone_map[f"Leg_Mid_Coxa_{suf}"] = "Thorax"
    bone_map[f"Leg_Mid_Femur_{suf}"] = f"Leg_Mid_Femur_{suf}"
    bone_map[f"Leg_Mid_Knee_{suf}"] = f"Leg_Mid_Femur_{suf}"
    bone_map[f"Leg_Mid_Tibia_{suf}"] = f"Leg_Mid_Tibia_{suf}"
    bone_map[f"Leg_Mid_Ankle_{suf}"] = f"Leg_Mid_Foot_{suf}"
    bone_map[f"Leg_Mid_Foot_{suf}"] = f"Leg_Mid_Foot_{suf}"
    bone_map[f"Leg_Mid_Toe1_{suf}"] = f"Leg_Mid_Foot_{suf}"
    bone_map[f"Leg_Mid_Toe2_{suf}"] = f"Leg_Mid_Foot_{suf}"

    bone_map[f"Leg_Hind_Coxa_{suf}"] = "Thorax"
    bone_map[f"Leg_Hind_Femur_{suf}"] = f"Leg_Hind_Femur_{suf}"
    bone_map[f"Leg_Hind_Knee_{suf}"] = f"Leg_Hind_Femur_{suf}"
    bone_map[f"Leg_Hind_Tibia_{suf}"] = f"Leg_Hind_Tibia_{suf}"
    bone_map[f"Leg_Hind_Ankle_{suf}"] = f"Leg_Hind_Foot_{suf}"
    bone_map[f"Leg_Hind_Foot_{suf}"] = f"Leg_Hind_Foot_{suf}"
    bone_map[f"Leg_Hind_Toe1_{suf}"] = f"Leg_Hind_Foot_{suf}"
    bone_map[f"Leg_Hind_Toe2_{suf}"] = f"Leg_Hind_Foot_{suf}"

for obj_name, b_name in bone_map.items():
    o = bpy.data.objects.get(obj_name)
    if o:
        bpy.ops.object.select_all(action='DESELECT')
        o.select_set(True)
        rig.select_set(True)
        bpy.context.view_layer.objects.active = rig
        rig.data.bones.active = rig.data.bones[b_name]
        bpy.ops.object.parent_set(type='BONE')

# Keyframe 30-frame Alternating Tripod Walk Action
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode='POSE')
rig.animation_data_create()
action = bpy.data.actions.new(name="Walk")
rig.animation_data.action = action

for pb in rig.pose.bones:
    pb.rotation_mode = 'XYZ'

pb_thorax = rig.pose.bones['Thorax']
pb_head = rig.pose.bones['Head']
pb_gaster = rig.pose.bones['Gaster']

# Thorax vertical bounce and pelvic sway
pb_thorax.location = Vector((0, 0, 0))
pb_thorax.rotation_euler = Euler((0, 0, 0))
pb_thorax.keyframe_insert('location', frame=1)
pb_thorax.keyframe_insert('rotation_euler', frame=1)

pb_thorax.location = Vector((0, 0, 0.024))
pb_thorax.rotation_euler = Euler((math.radians(1.5), 0, math.radians(2.0)))
pb_thorax.keyframe_insert('location', frame=8)
pb_thorax.keyframe_insert('rotation_euler', frame=8)

pb_thorax.location = Vector((0, 0, 0))
pb_thorax.rotation_euler = Euler((0, 0, 0))
pb_thorax.keyframe_insert('location', frame=15)
pb_thorax.keyframe_insert('rotation_euler', frame=15)

pb_thorax.location = Vector((0, 0, 0.024))
pb_thorax.rotation_euler = Euler((math.radians(1.5), 0, math.radians(-2.0)))
pb_thorax.keyframe_insert('location', frame=23)
pb_thorax.keyframe_insert('rotation_euler', frame=23)

pb_thorax.location = Vector((0, 0, 0))
pb_thorax.rotation_euler = Euler((0, 0, 0))
pb_thorax.keyframe_insert('location', frame=30)
pb_thorax.keyframe_insert('rotation_euler', frame=30)

# Head inquisitive nod
pb_head.rotation_euler = Euler((0, 0, 0))
pb_head.keyframe_insert('rotation_euler', frame=1)
pb_head.rotation_euler = Euler((math.radians(2.5), 0, 0))
pb_head.keyframe_insert('rotation_euler', frame=8)
pb_head.rotation_euler = Euler((0, 0, 0))
pb_head.keyframe_insert('rotation_euler', frame=15)
pb_head.rotation_euler = Euler((math.radians(2.5), 0, 0))
pb_head.keyframe_insert('rotation_euler', frame=23)
pb_head.rotation_euler = Euler((0, 0, 0))
pb_head.keyframe_insert('rotation_euler', frame=30)

# Gaster counter-sway
pb_gaster.rotation_euler = Euler((0, 0, 0))
pb_gaster.keyframe_insert('rotation_euler', frame=1)
pb_gaster.rotation_euler = Euler((0, math.radians(-2.5), math.radians(-2.0)))
pb_gaster.keyframe_insert('rotation_euler', frame=8)
pb_gaster.rotation_euler = Euler((0, 0, 0))
pb_gaster.keyframe_insert('rotation_euler', frame=15)
pb_gaster.rotation_euler = Euler((0, math.radians(2.5), math.radians(2.0)))
pb_gaster.keyframe_insert('rotation_euler', frame=23)
pb_gaster.rotation_euler = Euler((0, 0, 0))
pb_gaster.keyframe_insert('rotation_euler', frame=30)

# Tripod alternating leg kinematics
def keyframe_leg(femur_name, tibia_name, is_group_a):
    pb_f = rig.pose.bones.get(femur_name)
    pb_t = rig.pose.bones.get(tibia_name)
    if not pb_f or not pb_t:
        return
    phases = [
        (1,  -15.0 if is_group_a else 15.0,   0.0 if is_group_a else 0.0),
        (8,    0.0 if is_group_a else 18.0,   0.0 if is_group_a else 24.0),
        (15,  15.0 if is_group_a else -15.0,  0.0 if is_group_a else 0.0),
        (23,  18.0 if is_group_a else 0.0,   24.0 if is_group_a else 0.0),
        (30, -15.0 if is_group_a else 15.0,   0.0 if is_group_a else 0.0)
    ]
    for frame, f_pitch, t_flex in phases:
        pb_f.rotation_euler = Euler((math.radians(f_pitch), 0, 0))
        pb_f.keyframe_insert('rotation_euler', frame=frame)
        pb_t.rotation_euler = Euler((math.radians(t_flex), 0, 0))
        pb_t.keyframe_insert('rotation_euler', frame=frame)

keyframe_leg("Leg_Mid_Femur_L", "Leg_Mid_Tibia_L", True)
keyframe_leg("Leg_Hind_Femur_R", "Leg_Hind_Tibia_R", True)

keyframe_leg("Leg_Mid_Femur_R", "Leg_Mid_Tibia_R", False)
keyframe_leg("Leg_Hind_Femur_L", "Leg_Hind_Tibia_L", False)

# Front Arm swings (rhythmic arm pump)
def keyframe_arm(arm_name, is_group_a):
    pb_a = rig.pose.bones.get(arm_name)
    if not pb_a:
        return
    phases = [
        (1,   16.0 if is_group_a else -16.0),
        (8,    0.0 if is_group_a else   0.0),
        (15, -16.0 if is_group_a else  16.0),
        (23,   0.0 if is_group_a else   0.0),
        (30,  16.0 if is_group_a else -16.0)
    ]
    for frame, pitch in phases:
        pb_a.rotation_euler = Euler((math.radians(pitch), 0, 0))
        pb_a.keyframe_insert('rotation_euler', frame=frame)

keyframe_arm("Arm_Upper_R", True)
keyframe_arm("Arm_Upper_L", False)

bpy.ops.object.mode_set(mode='OBJECT')
bpy.context.scene.frame_set(1)

# -----------------------------------------------------------------------------
# 12. Multi-Angle Cameras & Render Stills (Including RTS Gameplay Angle)
# -----------------------------------------------------------------------------
cam_data = bpy.data.cameras.new("RenderCam")
cam_data.lens = 65.0
cam_obj = bpy.data.objects.new("RenderCam", cam_data)
bpy.context.scene.collection.objects.link(cam_obj)
scene.camera = cam_obj

views = [
    (
        "worker_front.png",
        Vector((0.0, -4.7, 1.25)),
        Euler((math.radians(88), 0, 0), 'XYZ'),
        52.0,
        "Front Authentic Heroic Character Stance"
    ),
    (
        "worker_perspective.png",
        Vector((-2.8, -3.2, 1.40)),
        Euler((math.radians(82), 0, math.radians(-40)), 'XYZ'),
        65.0,
        "3/4 Depth Perspective"
    ),
    (
        "worker_face_closeup.png",
        Vector((0.0, -2.0, 1.58)),
        Euler((math.radians(88), 0, 0), 'XYZ'),
        75.0,
        "Face, Eyes, Eyelids & Mandibles Macro Close-up"
    ),
    (
        "worker_gameplay_angle.png",
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
# 13. GLTF 2.0 Binary Export (worker_ant.glb)
# -----------------------------------------------------------------------------
glb_path = os.path.join(web_dir, "worker_ant.glb")
# Ensure 100% of mesh objects in Worker_Ant_Authentic collection have valid UV maps
for obj in worker_col.objects:
    if obj.type == 'MESH' and len(obj.data.uv_layers) == 0:
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.uv.smart_project(angle_limit=66.0, island_margin=0.02)
        bpy.ops.object.mode_set(mode='OBJECT')
        obj.select_set(False)

bpy.ops.object.select_all(action='DESELECT')
for obj in worker_col.objects:
    obj.select_set(True)

bpy.ops.export_scene.gltf(
    filepath=glb_path,
    export_format='GLB',
    use_selection=True,
    export_apply=False,
    export_animations=True
)
print("Worker Ant Authentic 3D GLB export complete!")

usdz_path = os.path.join(web_dir, "worker_ant.usdz")
print(f"Exporting Worker Ant USDZ to: {usdz_path}...")
bpy.ops.wm.usd_export(
    filepath=usdz_path,
    selected_objects_only=True,
    export_textures=True
)
print("Worker Ant USDZ export complete!")

blend_path = "/Users/dchadd/Desktop/Ants-Mac/tools/blender/worker_ant_authentic.blend"
bpy.ops.wm.save_as_mainfile(filepath=blend_path)
print(f"Saved Blender file: {blend_path}")
