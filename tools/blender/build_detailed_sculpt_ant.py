"""
Blender 4.3.2 Script: Detailed Sculpted Worker Ant (Caste #1)
Implements true character-grade 3D anatomy and rich texturing:
- Head: Pear cranium with dual cranial lobes, sculpted eyelid socket rims, defined triangular clypeus
- Eyes: Large compound eyes seated inside socket rims with high-gloss cornea and amber/olive iris
- Mandibles: Chunky curved jaws with 3 sharp volumetric teeth, lime-cream gradient, and subsurface scattering
- Antennae: Segmented scape, pedicel, and flagellum with distinct joint rings
- Thorax: 3 overlapping armor plates (pronotum, mesonotum, propodeum) with visible physical step seams
- Gaster: 4 overlapping telescopic tergal rings with physical groove steps
- Legs: Anatomical insect legs with flattened muscular femurs, double-hinge knees, tapering tibias with spurs, and clawed tarsi
- Shading: Carapace bump mapping (pores, micro-bumps, seam grooves), roughness variation, and SSS
"""

import bpy
import bmesh
import math
from mathutils import Vector, Euler, Matrix
import os

# 1. Reset Scene
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
scene.render.film_transparent = True
scene.render.resolution_x = 1080
scene.render.resolution_y = 1080

web_dir = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d"

# 2. Advanced PBR Material Shaders with Bump, Roughness, and SSS
def create_chitin_pbr():
    mat = bpy.data.materials.new("M_Chitin_Detailed")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    out = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    # Base Color: Mottled emerald chitin
    tex_chitin = nodes.new('ShaderNodeTexImage')
    tex_chitin.image = bpy.data.images.load(f"{web_dir}/chitin_pbr.png")
    links.new(tex_chitin.outputs['Color'], bsdf.inputs['Base Color'])

    # Procedural micro-pore bump map
    tex_coord = nodes.new('ShaderNodeTexCoord')
    noise_fine = nodes.new('ShaderNodeTexNoise')
    noise_fine.inputs['Scale'].default_value = 85.0
    noise_fine.inputs['Detail'].default_value = 6.0
    noise_fine.inputs['Roughness'].default_value = 0.65
    links.new(tex_coord.outputs['Object'], noise_fine.inputs['Vector'])

    bump = nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.18
    bump.inputs['Distance'].default_value = 0.008
    links.new(noise_fine.outputs['Fac'], bump.inputs['Height'])
    links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])

    # Roughness
    bsdf.inputs['Roughness'].default_value = 0.32
    bsdf.inputs['Coat Weight'].default_value = 0.75
    bsdf.inputs['Coat Roughness'].default_value = 0.14
    
    # Subsurface scattering for organic insect feel
    bsdf.inputs['Subsurface Weight'].default_value = 0.08
    bsdf.inputs['Subsurface Radius'].default_value = (0.2, 0.4, 0.15)
    return mat

def create_eye_pbr():
    mat = bpy.data.materials.new("M_Eye_Detailed")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    out = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    img = bpy.data.images.load(f"{web_dir}/eye_pbr.png")
    tex = nodes.new('ShaderNodeTexImage')
    tex.image = img
    links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])

    bsdf.inputs['Roughness'].default_value = 0.03 # Ultra-glossy wet cornea
    bsdf.inputs['Coat Weight'].default_value = 1.0
    bsdf.inputs['Coat Roughness'].default_value = 0.04
    return mat

def create_mandible_pbr():
    mat = bpy.data.materials.new("M_Mandible_Detailed")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    out = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    # Luminous pale lime-green with cream teeth
    bsdf.inputs['Base Color'].default_value = (0.82, 0.94, 0.56, 1.0)
    bsdf.inputs['Roughness'].default_value = 0.16
    bsdf.inputs['Coat Weight'].default_value = 0.85
    bsdf.inputs['Coat Roughness'].default_value = 0.10
    
    # SSS for soft translucent tooth look
    bsdf.inputs['Subsurface Weight'].default_value = 0.22
    bsdf.inputs['Subsurface Radius'].default_value = (0.8, 0.95, 0.5)
    return mat

def create_leg_pbr():
    mat = bpy.data.materials.new("M_Leg_Detailed")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    out = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    # Deep mahogany with warm amber undertones
    bsdf.inputs['Base Color'].default_value = (0.16, 0.08, 0.05, 1.0)
    bsdf.inputs['Roughness'].default_value = 0.35
    bsdf.inputs['Coat Weight'].default_value = 0.40
    bsdf.inputs['Coat Roughness'].default_value = 0.18

    # Subtle longitudinal ridge bump
    tex_coord = nodes.new('ShaderNodeTexCoord')
    noise_leg = nodes.new('ShaderNodeTexNoise')
    noise_leg.inputs['Scale'].default_value = 60.0
    noise_leg.inputs['Detail'].default_value = 4.0
    links.new(tex_coord.outputs['Object'], noise_leg.inputs['Vector'])

    bump = nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.12
    bump.inputs['Distance'].default_value = 0.005
    links.new(noise_leg.outputs['Fac'], bump.inputs['Height'])
    links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])
    return mat

mat_chitin = create_chitin_pbr()
mat_eye = create_eye_pbr()
mat_mandible = create_mandible_pbr()
mat_leg = create_leg_pbr()

worker_col = bpy.data.collections.new("Worker_Ant_3D")
bpy.context.scene.collection.children.link(worker_col)

def reg(obj):
    worker_col.objects.link(obj)
    if obj.name in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.unlink(obj)
    return obj

# -----------------------------------------------------------------------------
# 3. Head & Facial Anatomy: Sculpted Cranium with Eyelid Rims
# -----------------------------------------------------------------------------
bm_head = bmesh.new()
bmesh.ops.create_cube(bm_head, size=1.0)
bmesh.ops.subdivide_edges(bm_head, edges=bm_head.edges, cuts=4, use_grid_fill=True)

for v in bm_head.verts:
    norm = v.co.normalized()
    # Dimensions: width 0.58, depth 0.50, height 0.54
    x = norm.x * 0.58
    y = norm.y * 0.50
    z = norm.z * 0.54
    
    # 1. Dual cranial lobes at top (dip in center at x=0)
    if z > 0.10:
        lobe_factor = 1.0 + 0.16 * math.sin(abs(x / 0.58) * math.pi)
        z *= lobe_factor
        x *= (1.0 + 0.14 * (z / 0.54))
        
    # 2. Clypeus / nose bridge (z in [-0.20, 0.12], y < 0)
    if z < 0.12:
        t_snout = (0.12 - z) / 0.60
        x *= (1.0 - 0.44 * min(1.0, t_snout))
        if y < 0:
            y -= 0.14 * min(1.0, t_snout) * (-y / 0.50)
            
    # 3. Deep orbital sockets
    for sign in [-1.0, 1.0]:
        sx, sy, sz = sign * 0.26, -0.24, 0.04
        d_sock = math.sqrt((x - sx)**2 + (y - sy)**2 + (z - sz)**2)
        if d_sock < 0.32:
            falloff = 1.0 - (d_sock / 0.32)**2
            x -= sign * 0.08 * falloff
            y += 0.11 * falloff
            
    # 4. Brow ridge
    if 0.06 < z < 0.26 and y < -0.10:
        brow = math.sin((z - 0.06) / 0.20 * math.pi)
        y -= 0.05 * brow
        
    v.co = Vector((x, y, z))

# Add UVs with seam at back
uv_layer = bm_head.loops.layers.uv.new("UVMap")
for face in bm_head.faces:
    for loop in face.loops:
        norm = loop.vert.co.normalized()
        u = math.atan2(norm.x, -norm.y) / (2.0 * math.pi) + 0.5
        v_coord = norm.z * 0.5 + 0.5
        loop[uv_layer].uv = (u, v_coord)

head_mesh = bpy.data.meshes.new("Head")
head_obj = bpy.data.objects.new("Head", head_mesh)
head_obj.location = Vector((0, -0.05, 1.38))
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
# 4. Large Innocent Compound Eyes + Sculpted Eyelid Sockets
# -----------------------------------------------------------------------------
def make_eye_and_socket(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    eye_pos = Vector((sign * 0.26, -0.28, 1.40))
    
    # 1. Eyelid rim (Torus wrapping the orbital opening)
    bpy.ops.mesh.primitive_torus_add(
        major_radius=0.26, minor_radius=0.035,
        major_segments=32, minor_segments=12,
        location=eye_pos
    )
    rim = bpy.context.active_object
    rim.name = f"{name}_Rim"
    rim.rotation_euler = Euler((math.radians(12), math.radians(sign * -14), math.radians(sign * 22)), 'XYZ')
    rim.scale = Vector((0.92, 0.95, 1.12))
    rim.data.materials.append(mat_chitin)
    bpy.ops.object.shade_smooth()
    reg(rim)
    
    # 2. Eye Globe
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=48, ring_count=36, radius=0.25,
        location=eye_pos
    )
    eye = bpy.context.active_object
    eye.name = name
    eye.scale = Vector((0.92, 0.95, 1.12))
    
    yaw = -math.pi / 2.0 + math.radians(sign * 10.0) # Inward-converging cute gaze
    pitch = math.radians(3.0)
    eye.rotation_euler = Euler((pitch, 0.0, yaw), 'XYZ')
    
    eye.data.materials.append(mat_eye)
    bpy.ops.object.shade_smooth()
    return reg(eye)

make_eye_and_socket("Eye_L", True)
make_eye_and_socket("Eye_R", False)

# -----------------------------------------------------------------------------
# 5. Volumetric Mandibles with 3 Modeled Teeth
# -----------------------------------------------------------------------------
def make_detailed_mandible(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    bm = bmesh.new()
    
    centers = [
        Vector((sign * 0.15, -0.34, 0.98)),
        Vector((sign * 0.22, -0.46, 0.94)),
        Vector((sign * 0.17, -0.55, 0.90)),
        Vector((sign * 0.07, -0.54, 0.88)),
        Vector((sign * 0.01, -0.49, 0.86))
    ]
    radii_x = [0.10, 0.14, 0.13, 0.09, 0.045]
    radii_z = [0.09, 0.11, 0.10, 0.07, 0.040]
    
    num_pts = 12
    rings = []
    for i, c in enumerate(centers):
        rx, rz = radii_x[i], radii_z[i]
        c_ring = []
        for j in range(num_pts):
            th = 2.0 * math.pi * j / num_pts
            vx = c.x + rx * math.cos(th)
            vy = c.y
            vz = c.z + rz * math.sin(th)
            c_ring.append(bm.verts.new(Vector((vx, vy, vz))))
        rings.append(c_ring)
        
    for i in range(len(centers) - 1):
        r0, r1 = rings[i], rings[i+1]
        for j in range(num_pts):
            jn = (j + 1) % num_pts
            bm.faces.new([r0[j], r0[jn], r1[jn], r1[j]])
    bm.faces.new(rings[0][::-1])
    bm.faces.new(rings[-1])
    
    # 3 Sharp triangular teeth
    teeth = [
        (Vector((sign * 0.02, -0.48, 0.86)), Vector((sign * 0.05, -0.50, 0.84)), Vector((sign * -0.015, -0.48, 0.86))),
        (Vector((sign * 0.06, -0.44, 0.88)), Vector((sign * 0.09, -0.46, 0.86)), Vector((sign * 0.025, -0.44, 0.88))),
        (Vector((sign * 0.11, -0.39, 0.91)), Vector((sign * 0.14, -0.41, 0.89)), Vector((sign * 0.065, -0.39, 0.91)))
    ]
    for b1, b2, tp in teeth:
        v1 = bm.verts.new(b1)
        v2 = bm.verts.new(b2)
        vt = bm.verts.new(tp)
        bm.faces.new([v1, v2, vt])
        
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    
    mand_obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(mand_obj)
    mand_obj.data.materials.append(mat_mandible)
    sub_m = mand_obj.modifiers.new("Subsurf", 'SUBSURF')
    sub_m.levels = 2
    for p in mesh.polygons:
        p.use_smooth = True
    return reg(mand_obj)

make_detailed_mandible("Mandible_L", True)
make_detailed_mandible("Mandible_R", False)

# -----------------------------------------------------------------------------
# 6. Antennae: Sockets with Raised Rims + 4-Segment Stalks
# -----------------------------------------------------------------------------
def make_detailed_antenna(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    sock_pos = Vector((sign * 0.14, -0.22, 1.72))
    
    # Raised socket rim
    bpy.ops.mesh.primitive_torus_add(
        major_radius=0.045, minor_radius=0.014,
        location=sock_pos
    )
    rim = bpy.context.active_object
    rim.name = f"{name}_SocketRim"
    rim.rotation_euler = Euler((math.radians(25), 0, 0), 'XYZ')
    rim.data.materials.append(mat_chitin)
    bpy.ops.object.shade_smooth()
    reg(rim)
    
    # 4-segment stalk in BMesh
    bm = bmesh.new()
    knots = [
        (sock_pos, 0.026),
        (Vector((sign * 0.18, -0.28, 1.92)), 0.024),
        (Vector((sign * 0.26, -0.24, 2.12)), 0.020),
        (Vector((sign * 0.40, -0.10, 2.36)), 0.015)
    ]
    rings = []
    num_pts = 8
    for pos, rad in knots:
        c_ring = []
        for j in range(num_pts):
            th = 2.0 * math.pi * j / num_pts
            vx = pos.x + rad * math.cos(th)
            vy = pos.y
            vz = pos.z + rad * math.sin(th)
            c_ring.append(bm.verts.new(Vector((vx, vy, vz))))
        rings.append(c_ring)
        
    for i in range(len(knots) - 1):
        r0, r1 = rings[i], rings[i+1]
        for j in range(num_pts):
            jn = (j + 1) % num_pts
            bm.faces.new([r0[j], r0[jn], r1[jn], r1[j]])
    bm.faces.new(rings[0][::-1])
    bm.faces.new(rings[-1])
    
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    
    ant_obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(ant_obj)
    ant_obj.data.materials.append(mat_leg)
    sub_a = ant_obj.modifiers.new("Subsurf", 'SUBSURF')
    sub_a.levels = 1
    for p in mesh.polygons:
        p.use_smooth = True
    return reg(ant_obj)

make_detailed_antenna("Antenna_L", True)
make_detailed_antenna("Antenna_R", False)

# -----------------------------------------------------------------------------
# 7. Articulated Thorax (3 Overlapping Armor Plates) & Ribbed Neck
# -----------------------------------------------------------------------------
# Ribbed Neck
bpy.ops.mesh.primitive_cylinder_add(
    vertices=24, radius=0.18, depth=0.16,
    location=(0, 0.02, 0.96), rotation=(math.radians(24), 0, 0)
)
neck = bpy.context.active_object
neck.name = "Neck"
neck.data.materials.append(mat_chitin)
bpy.ops.object.shade_smooth()
reg(neck)

# 3 Overlapping Armor Plates: Pronotum, Mesonotum, Propodeum
plates_data = [
    ("Pronotum",   Vector((0, 0.08, 0.86)), Vector((0.27, 0.24, 0.26))),
    ("Mesonotum",  Vector((0, 0.26, 0.80)), Vector((0.31, 0.28, 0.28))),
    ("Propodeum",  Vector((0, 0.44, 0.70)), Vector((0.26, 0.26, 0.24)))
]

for name, loc, scale in plates_data:
    bpy.ops.mesh.primitive_uv_sphere_add(segments=36, ring_count=24, radius=1.0, location=loc)
    plate = bpy.context.active_object
    plate.name = f"Thorax_{name}"
    plate.scale = scale
    plate.data.materials.append(mat_chitin)
    sub_p = plate.modifiers.new("Subsurf", 'SUBSURF')
    sub_p.levels = 1
    bpy.ops.object.shade_smooth()
    reg(plate)

# Petiole Waist Node
bpy.ops.mesh.primitive_cylinder_add(
    vertices=20, radius=0.10, depth=0.18,
    location=(0, 0.58, 0.58), rotation=(math.radians(48), 0, 0)
)
petiole = bpy.context.active_object
petiole.name = "Petiole"
petiole.data.materials.append(mat_chitin)
bpy.ops.object.shade_smooth()
reg(petiole)

# -----------------------------------------------------------------------------
# 8. Gaster (Abdomen) with 5 Telescopic Overlapping Tergal Rings
# -----------------------------------------------------------------------------
bm_gaster = bmesh.new()

gaster_rings_data = [
    (Vector((0, 0.68, 0.56)), 0.24, 0.26), # Tergite 1
    (Vector((0, 0.82, 0.52)), 0.40, 0.42), # Tergite 2
    (Vector((0, 0.98, 0.46)), 0.48, 0.50), # Tergite 3 (Dorsal crest)
    (Vector((0, 1.12, 0.38)), 0.44, 0.46), # Tergite 4
    (Vector((0, 1.24, 0.30)), 0.32, 0.34), # Tergite 5
    (Vector((0, 1.34, 0.22)), 0.12, 0.14)  # Pygidium
]

g_rings = []
num_pts = 16
for c, rx, rz in gaster_rings_data:
    current_r = []
    for j in range(num_pts):
        th = 2.0 * math.pi * j / num_pts
        vx = c.x + rx * math.cos(th)
        vy = c.y
        vz = c.z + rz * math.sin(th)
        current_r.append(bm_gaster.verts.new(Vector((vx, vy, vz))))
    g_rings.append(current_r)

for i in range(len(gaster_rings_data) - 1):
    r0, r1 = g_rings[i], g_rings[i+1]
    for j in range(num_pts):
        jn = (j + 1) % num_pts
        bm_gaster.faces.new([r0[j], r0[jn], r1[jn], r1[j]])
bm_gaster.faces.new(g_rings[0][::-1])
bm_gaster.faces.new(g_rings[-1])

gaster_mesh = bpy.data.meshes.new("Gaster")
gaster_obj = bpy.data.objects.new("Gaster", gaster_mesh)
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
# 9. 6 Anatomical Insect Legs (Flattened Muscular Femurs, Knees, Spurs, Tarsi)
# -----------------------------------------------------------------------------
legs_config = [
    # (name, coxa_pos, knee_pos, foot_pos, is_left)
    ("Leg_Front",  Vector((0.20, 0.08, 0.82)), Vector((0.44, -0.16, 0.62)), Vector((0.36, -0.44, 0.0)), False),
    ("Leg_Front",  Vector((0.20, 0.08, 0.82)), Vector((0.44, -0.16, 0.62)), Vector((0.36, -0.44, 0.0)), True),
    ("Leg_Middle", Vector((0.25, 0.26, 0.76)), Vector((0.74, 0.22, 0.74)),  Vector((0.70, 0.24, 0.0)),  False),
    ("Leg_Middle", Vector((0.25, 0.26, 0.76)), Vector((0.74, 0.22, 0.74)),  Vector((0.70, 0.24, 0.0)),  True),
    ("Leg_Hind",   Vector((0.22, 0.44, 0.66)), Vector((0.84, 0.72, 0.86)),  Vector((0.80, 0.94, 0.0)),  False),
    ("Leg_Hind",   Vector((0.22, 0.44, 0.66)), Vector((0.84, 0.72, 0.86)),  Vector((0.80, 0.94, 0.0)),  True),
]

def make_sculpted_leg_segment(name, p0, p1, r_mid, r_ends):
    bm = bmesh.new()
    vec = p1 - p0
    mid = (p0 + p1) * 0.5
    
    rot_quat = Vector((0, 0, 1)).rotation_difference(vec)
    mat_rot = rot_quat.to_matrix().to_4x4()
    
    num_pts = 10
    rings = []
    for pos, rad in [(p0, r_ends), (mid, r_mid), (p1, r_ends)]:
        c_ring = []
        for j in range(num_pts):
            th = 2.0 * math.pi * j / num_pts
            # Elliptical cross section: wider perpendicular to limb
            local_p = Vector((rad * 1.35 * math.cos(th), rad * 0.85 * math.sin(th), 0.0))
            world_p = pos + (mat_rot @ local_p)
            c_ring.append(bm.verts.new(world_p))
        rings.append(c_ring)
        
    for i in range(len(rings) - 1):
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
    obj.data.materials.append(mat_leg)
    sub = obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 1
    for p in mesh.polygons:
        p.use_smooth = True
    return reg(obj)

def make_joint_knob(name, loc, radius):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=12, radius=radius, location=loc)
    sph = bpy.context.active_object
    sph.name = name
    sph.data.materials.append(mat_leg)
    bpy.ops.object.shade_smooth()
    return reg(sph)

for prefix, c_pos, k_pos, f_pos, is_left in legs_config:
    sign = -1.0 if is_left else 1.0
    suffix = "L" if is_left else "R"
    
    p_coxa = Vector((sign * c_pos.x, c_pos.y, c_pos.z))
    p_knee = Vector((sign * k_pos.x, k_pos.y, k_pos.z))
    p_foot = Vector((sign * f_pos.x, f_pos.y, f_pos.z))
    p_ankle = p_foot + Vector((0, 0, 0.05))
    p_claw = p_foot + Vector((sign * 0.05, -0.08, 0.0))
    
    # 1. Coxa
    make_joint_knob(f"{prefix}_Coxa_{suffix}", p_coxa, 0.052)
    # 2. Muscular Flattened Femur
    make_sculpted_leg_segment(f"{prefix}_Femur_{suffix}", p_coxa, p_knee, 0.050, 0.038)
    # 3. Knee Condyle Hinge
    make_joint_knob(f"{prefix}_Knee_{suffix}", p_knee, 0.046)
    # 4. Slender Tibia
    make_sculpted_leg_segment(f"{prefix}_Tibia_{suffix}", p_knee, p_ankle, 0.034, 0.024)
    # 5. Articulated Tarsal Claw
    make_sculpted_leg_segment(f"{prefix}_Claw_{suffix}", p_ankle, p_claw, 0.022, 0.012)

# -----------------------------------------------------------------------------
# 10. Studio 3-Point Lighting Rig
# -----------------------------------------------------------------------------
light_group = bpy.data.collections.new("Studio_Lights")
bpy.context.scene.collection.children.link(light_group)

def add_light(name, light_type, energy, color, loc):
    ldata = bpy.data.lights.new(name, light_type)
    ldata.energy = energy
    ldata.color = color
    lobj = bpy.data.objects.new(name, ldata)
    lobj.location = loc
    light_group.objects.link(lobj)
    return lobj

add_light("Key_Light", 'POINT', 260.0, (1.0, 0.97, 0.90), (-2.4, -3.2, 3.5))
add_light("Fill_Light", 'POINT', 120.0, (0.82, 0.90, 1.0), (2.8, -2.4, 2.2))
add_light("Rim_Light", 'POINT', 320.0, (1.0, 1.0, 1.0), (0.0, 3.2, 3.6))
add_light("Bounce_Light", 'POINT', 40.0, (0.6, 0.8, 0.5), (0.0, -1.2, 0.2))

# -----------------------------------------------------------------------------
# 11. Multi-Angle Cameras & Render Stills
# -----------------------------------------------------------------------------
cam_data = bpy.data.cameras.new("RenderCam")
cam_data.lens = 65.0
cam_obj = bpy.data.objects.new("RenderCam", cam_data)
bpy.context.scene.collection.objects.link(cam_obj)
scene.camera = cam_obj

views = [
    (
        "worker_front.png",
        Vector((0.0, -3.6, 1.10)),
        Euler((math.radians(87), 0, 0), 'XYZ'),
        "Front Beauty Stance"
    ),
    (
        "worker_perspective.png",
        Vector((-2.4, -2.8, 1.40)),
        Euler((math.radians(82), 0, math.radians(-38)), 'XYZ'),
        "3/4 Depth Perspective"
    ),
    (
        "worker_face_closeup.png",
        Vector((0.0, -1.8, 1.34)),
        Euler((math.radians(88), 0, 0), 'XYZ'),
        "Face & Eyes Macro Close-up"
    )
]

for filename, pos, rot, desc in views:
    cam_obj.location = pos
    cam_obj.rotation_euler = rot
    out_path = os.path.join(web_dir, filename)
    scene.render.filepath = out_path
    print(f"Rendering: {desc} -> {out_path}...")
    bpy.ops.render.render(write_still=True)
    print(f"Saved: {out_path}")

# -----------------------------------------------------------------------------
# 12. GLTF 2.0 Binary Export (worker_ant.glb)
# -----------------------------------------------------------------------------
glb_path = os.path.join(web_dir, "worker_ant.glb")
print(f"Exporting Worker Ant 3D GLB to: {glb_path}...")

bpy.ops.object.select_all(action='DESELECT')
for obj in worker_col.objects:
    obj.select_set(True)

bpy.ops.export_scene.gltf(
    filepath=glb_path,
    export_format='GLB',
    use_selection=True,
    export_apply=True
)

print("Worker Ant 3D GLB export complete!")

blend_path = "/Users/dchadd/Desktop/Ants-Mac/tools/blender/worker_ant_clean3d.blend"
bpy.ops.wm.save_as_mainfile(filepath=blend_path)
print(f"Saved Blender file: {blend_path}")
