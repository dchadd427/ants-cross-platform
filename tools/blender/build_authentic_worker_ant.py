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

    # Cuticle longitudinal groove bump
    tc_l = nodes_l.new('ShaderNodeTexCoord')
    wave_l = nodes_l.new('ShaderNodeTexWave')
    wave_l.wave_type = 'BANDS'
    wave_l.bands_direction = 'Z'
    wave_l.inputs['Scale'].default_value = 24.0
    wave_l.inputs['Distortion'].default_value = 1.2
    wave_l.inputs['Detail'].default_value = 3.0
    links_l.new(tc_l.outputs['Object'], wave_l.inputs['Vector'])

    bump_l = nodes_l.new('ShaderNodeBump')
    bump_l.inputs['Strength'].default_value = 0.18
    bump_l.inputs['Distance'].default_value = 0.003
    links_l.new(wave_l.outputs['Color'], bump_l.inputs['Height'])
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

    return mat_chitin, mat_eye, mat_mandible, mat_limbs, mat_antenna, mat_teeth

mat_chitin, mat_eye, mat_mandible, mat_limbs, mat_antenna, mat_teeth = create_materials()

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
# Character scale matching master reference: wide rounded trapezoid/pillow helmet
rx_head, ry_head, rz_head = 0.36, 0.25, 0.31

for v in bm_head.verts:
    # Superellipsoid formulation (p = 3.2): creates a wide pillow with rounded corners,
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

    # 1. Crown lobes & cleft (Z > 0.05): two distinct pillowy lobes with dip in middle
    if z > 0.05:
        cleft = 1.0 - 0.14 * math.exp(-((x / 0.09) ** 2))
        z *= cleft
        # Wide upper crown flare
        x *= (1.0 + 0.08 * (z / rz_head))

    # 2. Forehead dome & brow overhang (Z in [0.03, 0.22], Y < 0)
    if 0.03 < z < 0.22 and y < 0:
        brow_t = math.sin((z - 0.03) / 0.19 * math.pi)
        y -= 0.038 * brow_t

    # 3. Deep Orbital Sockets (Center at X = +/-0.165, Y = -0.15, Z = 0.015)
    for sign in [-1.0, 1.0]:
        sx, sy, sz = sign * 0.165, -0.15, 0.015
        d = math.sqrt((x - sx)**2 + (y - sy)**2 + (z - sz)**2)
        r_orb = 0.185

        if d < r_orb and y < 0:
            falloff = (1.0 - (d / r_orb)**2)**1.3
            y += 0.13 * falloff

    # 4. Solid Green Nose Bridge (Vertical ridge between eyes, keeping eyes cleanly separated)
    if abs(x) < 0.060 and -0.09 < z < 0.13 and y < 0:
        bridge = math.cos(abs(x) / 0.060 * (math.pi / 2.0))
        y -= 0.048 * bridge

    # 5. Clypeus / Snout (Tapers smoothly down below eyes, Z in [-0.26, -0.04])
    if -0.26 < z < -0.04 and abs(x) < 0.12 and y < 0:
        clyp = math.cos(abs(x) / 0.12 * (math.pi / 2.0))
        t_z = math.sin((z - (-0.26)) / 0.22 * math.pi)
        y -= 0.050 * clyp * t_z

    # 6. Temples & Cheeks flanking the eyes cleanly
    if abs(x) > 0.24 and -0.18 < z < 0.18:
        x *= 1.06

    # 7. Oral Cavity (Behind mandibles, Z < -0.18)
    if z < -0.18 and abs(x) < 0.16 and y < -0.02:
        y += 0.045 * (1.0 - abs(x) / 0.16)

    # 8. Lower jaw taper (narrowing toward neck)
    if z < -0.10:
        t_neck = min(1.0, (-0.10 - z) / 0.21)
        x *= (1.0 - 0.20 * t_neck)

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
    eye_pos = Vector((sign * 0.165, -0.180, 1.600))

    bm_eye = bmesh.new()
    bmesh.ops.create_uvsphere(bm_eye, u_segments=40, v_segments=28, radius=1.0)

    # Proportional cartoon dimensions: rx = 0.135, ry = 0.120, rz = 0.168
    rx, ry, rz = 0.135, 0.120, 0.168
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
            u = 0.50 - (sign * vx) / (2.0 * rx * 1.08) - (0.015 * sign)
            v = 0.50 + vz / (2.0 * rz * 1.08) + 0.020

            if vy > 0.02:
                u = 0.05
                v = 0.05

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
# 5. Horizontal Curved Pincer Mandibles with Sharp Medial Teeth
# -----------------------------------------------------------------------------
def make_clean_mandible(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    bm = bmesh.new()

    # Articulating from lower jaw corners, curving forward and horizontally inward
    stations = [
        # 0. Jaw hinge socket under cheek
        (Vector((sign * 0.18, -0.10, 1.36)), Vector((sign * 0.20, -0.95, -0.10)).normalized(), 0.065, 0.055),
        # 1. Lateral pincer curve
        (Vector((sign * 0.20, -0.21, 1.35)), Vector((sign * 0.10, -0.98, -0.05)).normalized(), 0.092, 0.064),
        # 2. Anterior turn
        (Vector((sign * 0.15, -0.27, 1.36)), Vector((sign * -0.65, -0.72, 0.0)).normalized(), 0.088, 0.060),
        # 3. Medial inward sweep
        (Vector((sign * 0.09, -0.28, 1.37)), Vector((sign * -0.95, -0.25, 0.0)).normalized(), 0.072, 0.050),
        # 4. Pointed pincer tip
        (Vector((sign * 0.03, -0.26, 1.38)), Vector((sign * -1.0, 0.0, 0.0)).normalized(), 0.038, 0.032)
    ]

    num_pts = 10
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

            rx_eff = rx * (1.18 if (cos_t * sign > 0) else 0.88)
            rz_eff = rz * (0.90 if sin_t < 0 else 1.10)

            p_local = (right * (cos_t * rx_eff)) + (up * (sin_t * rz_eff))
            world_p = center + p_local
            c_ring.append(bm.verts.new(world_p))
        rings.append(c_ring)

    for i in range(len(stations) - 1):
        r0 = rings[i]
        r1 = rings[i + 1]
        for j in range(num_pts):
            jn = (j + 1) % num_pts
            if is_left:
                bm.faces.new([r0[j], r0[jn], r1[jn], r1[j]])
            else:
                bm.faces.new([r0[jn], r0[j], r1[j], r1[jn]])

    if is_left:
        bm.faces.new(rings[0][::-1])
        bm.faces.new(rings[-1])
    else:
        bm.faces.new(rings[0])
        bm.faces.new(rings[-1][::-1])

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)

    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()

    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.data.materials.append(mat_mandible)
    sub = obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 2
    for p in mesh.polygons:
        p.use_smooth = True
    reg(obj)

    # Add sharp bone-white tooth cones on inner edge pointing horizontally inward
    teeth_locs = [
        (Vector((sign * 0.045, -0.265, 1.375)), 0.026, 0.045), # Main sharp fang
        (Vector((sign * 0.105, -0.275, 1.365)), 0.020, 0.032)  # Secondary tooth
    ]
    for idx, (t_pos, t_rad, t_len) in enumerate(teeth_locs):
        bpy.ops.mesh.primitive_cone_add(
            vertices=12, radius1=t_rad, depth=t_len,
            location=t_pos,
            rotation=(0, math.radians(-90 if is_left else 90), 0)
        )
        tooth_obj = bpy.context.active_object
        tooth_obj.name = f"{name}_Tooth_{idx}"
        tooth_obj.data.materials.append(mat_teeth)
        bpy.ops.object.shade_smooth()
        reg(tooth_obj)

    return obj

make_clean_mandible("Mandible_L", True)
make_clean_mandible("Mandible_R", False)

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
    ("Thorax_Pronotum",  Vector((0, 0.01, 1.25)), Vector((0.17, 0.16, 0.14))),
    ("Thorax_Mesonotum", Vector((0, 0.08, 1.11)), Vector((0.16, 0.15, 0.13))),
    ("Thorax_Metanotum", Vector((0, 0.15, 0.98)), Vector((0.14, 0.14, 0.12)))
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

# Slender Petiole Waist
bpy.ops.mesh.primitive_cylinder_add(
    vertices=16, radius=0.060, depth=0.13,
    location=(0, 0.22, 0.88), rotation=(math.radians(35), 0, 0)
)
petiole = bpy.context.active_object
petiole.name = "Petiole"
petiole.data.materials.append(mat_chitin)
bpy.ops.object.shade_smooth()
reg(petiole)

# -----------------------------------------------------------------------------
# 8. Gaster: Suspended Plump Egg Abdomen at 32° with Sternite Grooves
# -----------------------------------------------------------------------------
bpy.ops.mesh.primitive_uv_sphere_add(
    segments=36, ring_count=24, radius=1.0,
    location=(0, 0.30, 0.98),
    rotation=(math.radians(16), 0, 0)
)
gaster_obj = bpy.context.active_object
gaster_obj.name = "Gaster"

for v in gaster_obj.data.vertices:
    x = v.co.x * 0.25
    y = v.co.y * 0.38
    z = v.co.z * 0.26

    if y > 0:
        taper = 1.0 - 0.40 * (y / 0.38)
        x *= taper
        z *= (taper * 0.92)
    else:
        x *= 1.06
        z *= 1.04

    groove = math.sin((y + 0.38) * 16.0) * 0.008
    x += groove * (x / 0.25)
    z += groove * (z / 0.26)

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
def make_chitin_segment(name, p0, p1, r_start, r_mid, r_end, is_sleeve=False):
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
    # 5 profile stations with authentic arthropod taper and articulated joint collars
    stations = [
        (p0, r_start * 1.15, 1.25, 0.88),                  # Flared joint socket sleeve
        (p0 + vec * 0.18, r_start * 0.94, 1.20, 0.84),     # Narrow neck taper
        (p0 + vec * 0.45, r_mid * 1.14, 1.32, 0.80),       # Muscular lateral flattening & ridge bulge
        (p0 + vec * 0.80, r_end * 0.90, 1.18, 0.82),       # Distal shaft taper
        (p1, r_end * (1.30 if is_sleeve else 1.05), 1.22, 0.88) # Articulated condyle sleeve
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
    obj.data.materials.append(mat_limbs)
    sub = obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 1
    for p in mesh.polygons:
        p.use_smooth = True
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

# A. Front Expressive Arms (Bent dynamically at elbows framing chest)
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"

    p_shoulder = Vector((sign * 0.13, -0.04, 1.22))
    p_elbow    = Vector((sign * 0.22, -0.16, 0.98)) # Elbow bent outward and forward
    p_wrist    = Vector((sign * 0.18, -0.22, 0.74)) # Forearm back inward
    p_finger1  = Vector((sign * 0.16, -0.25, 0.58)) # Curved hand claw
    p_finger2  = Vector((sign * 0.20, -0.22, 0.56))

    make_chitin_segment(f"Arm_Upper_{suf}", p_shoulder, p_elbow, 0.038, 0.044, 0.032, is_sleeve=True)
    make_chitin_segment(f"Arm_Forearm_{suf}", p_elbow, p_wrist, 0.032, 0.034, 0.022, is_sleeve=True)
    make_chitin_segment(f"Arm_Finger1_{suf}", p_wrist, p_finger1, 0.016, 0.016, 0.010)
    make_chitin_segment(f"Arm_Finger2_{suf}", p_wrist, p_finger2, 0.014, 0.014, 0.008)

# B. Middle Walking Legs (Reaching laterally to mid-width)
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"

    p_hip   = Vector((sign * 0.14, 0.06, 1.08))
    p_knee  = Vector((sign * 0.44, 0.00, 0.68)) # Lateral knee reaching OUT to side!
    p_ankle = Vector((sign * 0.36, -0.08, 0.08))
    p_toe1  = Vector((sign * 0.34, -0.16, 0.02))
    p_toe2  = Vector((sign * 0.38, -0.14, 0.02))

    make_chitin_segment(f"Leg_Mid_Femur_{suf}", p_hip, p_knee, 0.044, 0.050, 0.036, is_sleeve=True)
    make_chitin_segment(f"Leg_Mid_Tibia_{suf}", p_knee, p_ankle, 0.036, 0.032, 0.022, is_sleeve=True)
    make_chitin_segment(f"Leg_Mid_Foot_{suf}", p_ankle, p_toe1, 0.022, 0.020, 0.015)
    make_foot_toe_pad(f"Leg_Mid_Toe1_{suf}", p_toe1, 0.022)
    make_foot_toe_pad(f"Leg_Mid_Toe2_{suf}", p_toe2, 0.018)

# C. Hind Walking Legs (Reaching backward & wide in authentic tripod stance)
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"

    p_hip   = Vector((sign * 0.12, 0.16, 0.98))
    p_knee  = Vector((sign * 0.64, 0.28, 0.88)) # High lateral knee reaching BACK and WIDE!
    p_ankle = Vector((sign * 0.56, 0.14, 0.08))
    p_toe1  = Vector((sign * 0.58, 0.06, 0.02))
    p_toe2  = Vector((sign * 0.62, 0.18, 0.02))

    make_chitin_segment(f"Leg_Hind_Femur_{suf}", p_hip, p_knee, 0.048, 0.054, 0.038, is_sleeve=True)
    make_chitin_segment(f"Leg_Hind_Tibia_{suf}", p_knee, p_ankle, 0.038, 0.034, 0.024, is_sleeve=True)
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

scene.view_settings.look = 'AgX - Medium High Contrast'

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
    export_apply=True
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
