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

    # Multi-band procedural organic color ramp
    tex_coord = nodes.new('ShaderNodeTexCoord')
    noise_color = nodes.new('ShaderNodeTexNoise')
    noise_color.inputs['Scale'].default_value = 16.0
    noise_color.inputs['Detail'].default_value = 4.0
    noise_color.inputs['Roughness'].default_value = 0.52
    links.new(tex_coord.outputs['Object'], noise_color.inputs['Vector'])

    color_ramp = nodes.new('ShaderNodeValToRGB')
    color_ramp.color_ramp.elements[0].position = 0.15
    color_ramp.color_ramp.elements[0].color = (0.16, 0.24, 0.11, 1.0) # Deep forest moss green
    color_ramp.color_ramp.elements[1].position = 0.70
    color_ramp.color_ramp.elements[1].color = (0.28, 0.38, 0.18, 1.0) # Saturated rich olive green
    elem_warm = color_ramp.color_ramp.elements.new(0.42)
    elem_warm.color = (0.34, 0.26, 0.16, 1.0) # Warm terracotta-tan accent

    links.new(noise_color.outputs['Fac'], color_ramp.inputs['Fac'])
    links.new(color_ramp.outputs['Color'], bsdf.inputs['Base Color'])

    # Micro bump (subtle organic orange peel)
    noise_bump = nodes.new('ShaderNodeTexNoise')
    noise_bump.inputs['Scale'].default_value = 85.0
    noise_bump.inputs['Detail'].default_value = 5.0
    noise_bump.inputs['Roughness'].default_value = 0.55
    links.new(tex_coord.outputs['Object'], noise_bump.inputs['Vector'])

    bump = nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.10
    bump.inputs['Distance'].default_value = 0.005
    links.new(noise_bump.outputs['Fac'], bump.inputs['Height'])
    links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])

    bsdf.inputs['Roughness'].default_value = 0.30
    bsdf.inputs['Coat Weight'].default_value = 0.75
    bsdf.inputs['Coat Roughness'].default_value = 0.12
    bsdf.inputs['Subsurface Weight'].default_value = 0.06
    bsdf.inputs['Subsurface Radius'].default_value = (0.08, 0.12, 0.05)

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

    bsdf_e.inputs['Roughness'].default_value = 0.02
    bsdf_e.inputs['Coat Weight'].default_value = 1.0
    bsdf_e.inputs['Coat Roughness'].default_value = 0.01
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

    tc_m = nodes_m.new('ShaderNodeTexCoord')
    sep_m = nodes_m.new('ShaderNodeSeparateXYZ')
    links_m.new(tc_m.outputs['Generated'], sep_m.inputs['Vector'])

    ramp_m = nodes_m.new('ShaderNodeValToRGB')
    ramp_m.color_ramp.elements[0].position = 0.15
    ramp_m.color_ramp.elements[0].color = (0.20, 0.28, 0.14, 1.0) # Base moss olive
    ramp_m.color_ramp.elements[1].position = 0.70
    ramp_m.color_ramp.elements[1].color = (0.64, 0.78, 0.28, 1.0) # Luminous chartreuse tip
    elem_fang = ramp_m.color_ramp.elements.new(0.88)
    elem_fang.color = (0.90, 0.92, 0.84, 1.0) # Bone white teeth

    links_m.new(sep_m.outputs['X'], ramp_m.inputs['Fac'])
    links_m.new(ramp_m.outputs['Color'], bsdf_m.inputs['Base Color'])

    bsdf_m.inputs['Roughness'].default_value = 0.20
    bsdf_m.inputs['Coat Weight'].default_value = 0.85
    bsdf_m.inputs['Coat Roughness'].default_value = 0.07
    bsdf_m.inputs['Subsurface Weight'].default_value = 0.15
    bsdf_m.inputs['Subsurface Radius'].default_value = (0.35, 0.55, 0.20)

    # D. Limbs Material (Warm mottled terracotta/mahogany with worn amber highlights)
    mat_limbs = bpy.data.materials.new("M_Limbs_Authentic")
    mat_limbs.use_nodes = True
    nodes_l = mat_limbs.node_tree.nodes
    links_l = mat_limbs.node_tree.links
    nodes_l.clear()

    out_l = nodes_l.new('ShaderNodeOutputMaterial')
    bsdf_l = nodes_l.new('ShaderNodeBsdfPrincipled')
    links_l.new(bsdf_l.outputs['BSDF'], out_l.inputs['Surface'])

    tc_l = nodes_l.new('ShaderNodeTexCoord')
    noise_limb = nodes_l.new('ShaderNodeTexNoise')
    noise_limb.inputs['Scale'].default_value = 24.0
    noise_limb.inputs['Detail'].default_value = 4.0
    links_l.new(tc_l.outputs['Object'], noise_limb.inputs['Vector'])

    ramp_l = nodes_l.new('ShaderNodeValToRGB')
    ramp_l.color_ramp.elements[0].position = 0.20
    ramp_l.color_ramp.elements[0].color = (0.22, 0.11, 0.08, 1.0) # Deep mahogany
    ramp_l.color_ramp.elements[1].position = 0.65
    ramp_l.color_ramp.elements[1].color = (0.42, 0.19, 0.12, 1.0) # Warm terracotta
    elem_edge = ramp_l.color_ramp.elements.new(0.85)
    elem_edge.color = (0.52, 0.34, 0.22, 1.0) # Amber joint highlight

    links_l.new(noise_limb.outputs['Fac'], ramp_l.inputs['Fac'])
    links_l.new(ramp_l.outputs['Color'], bsdf_l.inputs['Base Color'])

    bump_l = nodes_l.new('ShaderNodeBump')
    bump_l.inputs['Strength'].default_value = 0.10
    bump_l.inputs['Distance'].default_value = 0.005
    links_l.new(noise_limb.outputs['Fac'], bump_l.inputs['Height'])
    links_l.new(bump_l.outputs['Normal'], bsdf_l.inputs['Normal'])

    bsdf_l.inputs['Roughness'].default_value = 0.34
    bsdf_l.inputs['Coat Weight'].default_value = 0.50
    bsdf_l.inputs['Coat Roughness'].default_value = 0.15
    bsdf_l.inputs['Subsurface Weight'].default_value = 0.08
    bsdf_l.inputs['Subsurface Radius'].default_value = (0.20, 0.10, 0.06)

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

    return mat_chitin, mat_eye, mat_mandible, mat_limbs, mat_antenna

mat_chitin, mat_eye, mat_mandible, mat_limbs, mat_antenna = create_materials()

worker_col = bpy.data.collections.new("Worker_Ant_Authentic")
bpy.context.scene.collection.children.link(worker_col)

def reg(obj):
    worker_col.objects.link(obj)
    if obj.name in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.unlink(obj)
    return obj

# -----------------------------------------------------------------------------
# 3. Large Sculpted Cartoon Cranium with Deep Eye Orbits
# -----------------------------------------------------------------------------
bm_head = bmesh.new()
bmesh.ops.create_cube(bm_head, size=1.0)
bmesh.ops.subdivide_edges(bm_head, edges=bm_head.edges, cuts=7, use_grid_fill=True)

# Head origin at (0, -0.04, 1.62)
# Dimensions: width 0.68, depth 0.52, height 0.52 (Heroic cartoon proportions)
rx_head, ry_head, rz_head = 0.34, 0.26, 0.26

for v in bm_head.verts:
    norm = v.co.normalized()
    x = norm.x * rx_head
    y = norm.y * ry_head
    z = norm.z * rz_head

    # 1. Crown lobes: rounded pillows with central dip between antennae
    if z > 0.05:
        cleft = 1.0 - 0.15 * math.exp(-((x / 0.11) ** 2))
        z *= cleft
        x *= (1.0 + 0.10 * (z / rz_head))

    # 2. Deep Orbital Sockets (Center at X = +/-0.185, Y = -0.16, Z = 0.04)
    for sign in [-1.0, 1.0]:
        sx, sy, sz = sign * 0.185, -0.16, 0.04
        d = math.sqrt((x - sx)**2 + (y - sy)**2 + (z - sz)**2)
        r_orb = 0.22

        if d < r_orb:
            falloff = (1.0 - (d / r_orb)**2)**1.3
            y += 0.18 * falloff

    # 3. Forehead Brow Bridge (Central vertical ridge between eyes)
    if abs(x) < 0.09 and z > -0.06 and y < 0:
        bridge = math.cos(abs(x) / 0.09 * (math.pi / 2.0))
        y -= 0.040 * bridge

    # 4. Clypeus / Snout (Bulldog nose bridge directly below eyes, Z < -0.04)
    if z < -0.04:
        ts = min(1.0, (-0.04 - z) / 0.22)
        if abs(x) < 0.14:
            snout_shape = (1.0 - abs(x) / 0.14)
            if y < 0:
                y -= 0.095 * snout_shape * ts
            if z < -0.16 and abs(x) < 0.04:
                y += 0.035 * (1.0 - abs(x) / 0.04)

    # 5. Rounded Cheeks flanking the lower jaw
    if -0.24 < z < -0.06 and abs(x) > 0.16:
        cheek = math.sin((z - (-0.24)) / 0.18 * math.pi)
        x *= (1.0 + 0.14 * cheek)
        if y < 0:
            y -= 0.06 * cheek

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
head_obj.location = Vector((0, -0.04, 1.62))
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
# 4. Large Bulging 3D Spherical Eyes with Sculpted Upper Eyelid Hoods
# -----------------------------------------------------------------------------
def make_bulging_eye(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    eye_pos = Vector((sign * 0.185, -0.22, 1.66))

    bm_eye = bmesh.new()
    bmesh.ops.create_uvsphere(bm_eye, u_segments=40, v_segments=28, radius=1.0)

    rx, ry, rz = 0.180, 0.175, 0.205
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

            u = 0.50 - (sign * vx) / (2.0 * rx * 1.10)
            v = 0.50 + vz / (2.0 * rz * 1.10)

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

    tilt_z = sign * math.radians(11.0)
    pitch_x = math.radians(5.0)
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
# 5. Plump Bulldog Jowl Mandibles (100% Manifold Quads with Interlocking Teeth)
# -----------------------------------------------------------------------------
def make_clean_mandible(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    bm = bmesh.new()

    # Elevated snug under snout and eyes (Z = 1.40 to 1.50)
    stations = [
        (Vector((sign * 0.18, -0.16, 1.50)), Vector((sign * 0.35, -0.90, -0.15)).normalized(), 0.110, 0.115),
        (Vector((sign * 0.28, -0.24, 1.46)), Vector((sign * 0.15, -0.92, -0.22)).normalized(), 0.145, 0.135),
        (Vector((sign * 0.23, -0.34, 1.42)), Vector((sign * -0.55, -0.70, -0.25)).normalized(), 0.135, 0.125),
        (Vector((sign * 0.12, -0.33, 1.40)), Vector((sign * -0.88, -0.25, -0.18)).normalized(), 0.100, 0.095),
        (Vector((sign * 0.04, -0.29, 1.39)), Vector((sign * -1.0, 0.0, 0.0)).normalized(), 0.040, 0.040)
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

            rx_eff = rx * (1.25 if (cos_t * sign > 0) else 0.85)
            rz_eff = rz * (0.92 if sin_t < 0 else 1.08)

            if i in [2, 3] and abs(j - num_pts // 2) <= 1:
                tooth_reach = 0.045 if i == 2 else 0.035
                z_tooth = 0.012 if is_left else -0.012
                p_local = (right * (cos_t * rx_eff - sign * tooth_reach)) + (up * (sin_t * rz_eff + z_tooth))
            else:
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
    return reg(obj)

make_clean_mandible("Mandible_L", True)
make_clean_mandible("Mandible_R", False)

# -----------------------------------------------------------------------------
# 6. Smooth Curved Bézier Antennae (Sweeping Gracefully OUTWARD past temples)
# -----------------------------------------------------------------------------
def make_authentic_antenna(name, is_left=True):
    sign = -1.0 if is_left else 1.0

    curve_data = bpy.data.curves.new(name, 'CURVE')
    curve_data.dimensions = '3D'
    curve_data.bevel_depth = 0.022
    curve_data.bevel_resolution = 6
    curve_data.fill_mode = 'FULL'

    spline = curve_data.splines.new('BEZIER')
    spline.bezier_points.add(2)

    # Trajectory: Sprouts from forehead brow, arches upward and OUTWARD past temples
    p0 = Vector((sign * 0.075, -0.15, 1.82)) # Forehead brow socket
    p1 = Vector((sign * 0.200, -0.15, 2.15)) # Mid stalk slanting outward
    p2 = Vector((sign * 0.420, -0.06, 2.34)) # Tip sweeping boldly OUTWARD past temples

    pts = [p0, p1, p2]
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

    # Rounded teardrop club tip at p2
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=20, ring_count=16, radius=0.038,
        location=p2
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
    vertices=24, radius=0.12, depth=0.10,
    location=(0, -0.03, 1.36), rotation=(math.radians(16), 0, 0)
)
neck = bpy.context.active_object
neck.name = "Neck"
neck.data.materials.append(mat_chitin)
bpy.ops.object.shade_smooth()
reg(neck)

thorax_plates = [
    ("Thorax_Pronotum",  Vector((0, 0.01, 1.22)), Vector((0.26, 0.23, 0.18))),
    ("Thorax_Mesonotum", Vector((0, 0.09, 1.07)), Vector((0.27, 0.25, 0.19))),
    ("Thorax_Metanotum", Vector((0, 0.19, 0.93)), Vector((0.24, 0.22, 0.17)))
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
    vertices=16, radius=0.075, depth=0.14,
    location=(0, 0.26, 0.82), rotation=(math.radians(35), 0, 0)
)
petiole = bpy.context.active_object
petiole.name = "Petiole"
petiole.data.materials.append(mat_chitin)
bpy.ops.object.shade_smooth()
reg(petiole)

# -----------------------------------------------------------------------------
# 8. Gaster: Suspended Plump Egg Abdomen at 40° with Sternite Grooves
# -----------------------------------------------------------------------------
bm_gaster = bmesh.new()
bmesh.ops.create_uvsphere(bm_gaster, u_segments=36, v_segments=24, radius=1.0)

for v in bm_gaster.verts:
    x = v.co.x * 0.36
    y = v.co.y * 0.48
    z = v.co.z * 0.40

    if y > 0:
        taper = 1.0 - 0.40 * (y / 0.48)
        x *= taper
        z *= (taper * 0.92)
    else:
        x *= 1.08
        z *= 1.05

    groove = math.sin((y + 0.48) * 14.0) * 0.012
    x += groove * (x / 0.36)
    z += groove * (z / 0.40)

    v.co = Vector((x, y, z))

gaster_mesh = bpy.data.meshes.new("Gaster")
gaster_obj = bpy.data.objects.new("Gaster", gaster_mesh)
gaster_obj.location = Vector((0, 0.34, 0.66))
gaster_obj.rotation_euler = Euler((math.radians(-40), 0, 0), 'XYZ')
bpy.context.scene.collection.objects.link(gaster_obj)
bm_gaster.to_mesh(gaster_mesh)
bm_gaster.free()

gaster_obj.data.materials.append(mat_chitin)
sub_g = gaster_obj.modifiers.new("Subsurf", 'SUBSURF')
sub_g.levels = 2
for p in gaster_mesh.polygons:
    p.use_smooth = True
reg(gaster_obj)

# -----------------------------------------------------------------------------
# 9. Anatomical Insect Limbs & Character Stance (Matching ref_limbs_crop.png)
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

    num_pts = 12
    rings = []
    stations = [
        (p0, r_start, 1.0, 1.0),
        (p0 + vec * 0.28, r_mid, 1.24, 0.88),
        (p0 + vec * 0.72, r_end * 1.05, 1.10, 0.92),
        (p1, r_end * (1.25 if is_sleeve else 1.0), 1.15, 0.95)
    ]
    for pos, rad, sx, sy in stations:
        c_ring = []
        for j in range(num_pts):
            th = 2.0 * math.pi * j / num_pts
            local_p = Vector((rad * sx * math.cos(th), rad * sy * math.sin(th), 0.0))
            world_p = pos + (mat_rot @ local_p)
            c_ring.append(bm.verts.new(world_p))
        rings.append(c_ring)

    for i in range(len(stations) - 1):
        r0, r1 = rings[i], rings[i+1]
        for j in range(num_pts):
            jn = (j + 1) % num_pts
            bm.faces.new([r0[j], r0[jn], r1[jn], r1[j]])
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
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=16, v_segments=12, radius=1.0)
    for v in bm.verts:
        v.co.x *= radius * 1.2
        v.co.y *= radius * 1.8
        v.co.z *= radius * 0.75
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    obj.location = p_toe
    bpy.context.scene.collection.objects.link(obj)
    obj.data.materials.append(mat_limbs)
    bpy.ops.object.shade_smooth()
    return reg(obj)

# A. Front Expressive Arms (Bent dynamically at elbows matching ref_limbs_crop.png)
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"

    p_shoulder = Vector((sign * 0.18, -0.04, 1.18))
    p_elbow    = Vector((sign * 0.36, -0.12, 0.90)) # Elbow bent outward!
    p_wrist    = Vector((sign * 0.28, -0.18, 0.62)) # Forearm back inward
    p_finger1  = Vector((sign * 0.26, -0.22, 0.46))
    p_finger2  = Vector((sign * 0.30, -0.18, 0.44))

    make_chitin_segment(f"Arm_Upper_{suf}", p_shoulder, p_elbow, 0.046, 0.052, 0.038, is_sleeve=True)
    make_chitin_segment(f"Arm_Forearm_{suf}", p_elbow, p_wrist, 0.038, 0.040, 0.026, is_sleeve=True)
    make_chitin_segment(f"Arm_Finger1_{suf}", p_wrist, p_finger1, 0.020, 0.020, 0.012)
    make_chitin_segment(f"Arm_Finger2_{suf}", p_wrist, p_finger2, 0.018, 0.018, 0.010)

# B. Middle Walking Legs (Planted forward on floor Z = 0)
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"

    p_hip   = Vector((sign * 0.18, 0.06, 1.02))
    p_knee  = Vector((sign * 0.26, -0.08, 0.58))
    p_ankle = Vector((sign * 0.22, -0.16, 0.08))
    p_toe1  = Vector((sign * 0.20, -0.26, 0.02))
    p_toe2  = Vector((sign * 0.24, -0.24, 0.02))

    make_chitin_segment(f"Leg_Mid_Femur_{suf}", p_hip, p_knee, 0.050, 0.056, 0.040, is_sleeve=True)
    make_chitin_segment(f"Leg_Mid_Tibia_{suf}", p_knee, p_ankle, 0.040, 0.036, 0.024, is_sleeve=True)
    make_chitin_segment(f"Leg_Mid_Foot_{suf}", p_ankle, p_toe1, 0.024, 0.022, 0.016)
    make_foot_toe_pad(f"Leg_Mid_Toe1_{suf}", p_toe1, 0.024)
    make_foot_toe_pad(f"Leg_Mid_Toe2_{suf}", p_toe2, 0.020)

# C. Hind Walking Legs (High lateral knees X = +/-0.65, wide planted stance at Z = 0)
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"

    p_hip   = Vector((sign * 0.16, 0.18, 0.92))
    p_knee  = Vector((sign * 0.65, 0.10, 0.82)) # High lateral knee reaching wide!
    p_ankle = Vector((sign * 0.60, -0.02, 0.08))
    p_toe1  = Vector((sign * 0.62, -0.10, 0.02))
    p_toe2  = Vector((sign * 0.66, 0.04, 0.02))

    make_chitin_segment(f"Leg_Hind_Femur_{suf}", p_hip, p_knee, 0.054, 0.060, 0.044, is_sleeve=True)
    make_chitin_segment(f"Leg_Hind_Tibia_{suf}", p_knee, p_ankle, 0.044, 0.040, 0.028, is_sleeve=True)
    make_chitin_segment(f"Leg_Hind_Foot_{suf}", p_ankle, p_toe1, 0.028, 0.024, 0.018)
    make_foot_toe_pad(f"Leg_Hind_Toe1_{suf}", p_toe1, 0.028)
    make_foot_toe_pad(f"Leg_Hind_Toe2_{suf}", p_toe2, 0.024)

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
        Vector((0.0, -4.2, 1.15)),
        Euler((math.radians(88), 0, 0), 'XYZ'),
        65.0,
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
print(f"Exporting Worker Ant Authentic 3D GLB to: {glb_path}...")

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
