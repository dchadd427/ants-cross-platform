"""
Blender 4.3.2 Script: Master Aesthetic Worker Ant (Caste #1)
Achieves authentic 1:1 fidelity with master reference artwork:
- Head: Single organic cranium with rounded cushion crown, central furrow, arched brow,
  slender snout bridge, and deep seamlessly integrated orbital sockets.
- Eyes: Expressive compound eyes with geometrically corrected UV mapping,
  warm cream sclera, large hazel-olive iris, dark pupil, and crisp catchlights.
- Mandibles: Smooth green cheek base curving into pale chartreuse pincer tips with sculpted teeth.
- Antennae: Crown-rooted stalks with hairpin backward loop and clubbed tips.
- Thorax: 3 overlapping articulated armor segments with physical step seams.
- Gaster: Teardrop abdomen with 4 tergal segments and organic chitin texture.
- Limbs: 2 front expressive arms with wrist collars and 2-fingered hands hovering in front of chest;
  4 rear muscular walking legs firmly planted on the ground plane (Z = 0).
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

# 2. PBR Materials
def create_materials():
    # A. Chitin Material
    mat_chitin = bpy.data.materials.new("M_Chitin_Master")
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

    tex_coord = nodes.new('ShaderNodeTexCoord')
    noise_bump = nodes.new('ShaderNodeTexNoise')
    noise_bump.inputs['Scale'].default_value = 75.0
    noise_bump.inputs['Detail'].default_value = 5.0
    noise_bump.inputs['Roughness'].default_value = 0.6
    links.new(tex_coord.outputs['Object'], noise_bump.inputs['Vector'])

    bump = nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.16
    bump.inputs['Distance'].default_value = 0.006
    links.new(noise_bump.outputs['Fac'], bump.inputs['Height'])
    links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])

    bsdf.inputs['Roughness'].default_value = 0.40
    bsdf.inputs['Coat Weight'].default_value = 0.65
    bsdf.inputs['Coat Roughness'].default_value = 0.18
    bsdf.inputs['Subsurface Weight'].default_value = 0.08
    bsdf.inputs['Subsurface Radius'].default_value = (0.2, 0.4, 0.15)

    # B. Eye Material
    mat_eye = bpy.data.materials.new("M_Eye_Master")
    mat_eye.use_nodes = True
    nodes = mat_eye.node_tree.nodes
    links = mat_eye.node_tree.links
    nodes.clear()

    out = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    tex_eye = nodes.new('ShaderNodeTexImage')
    tex_eye.image = bpy.data.images.load(f"{web_dir}/eye_pbr.png")
    links.new(tex_eye.outputs['Color'], bsdf.inputs['Base Color'])

    bsdf.inputs['Roughness'].default_value = 0.02 # Wet glossy cornea
    bsdf.inputs['Coat Weight'].default_value = 1.0
    bsdf.inputs['Coat Roughness'].default_value = 0.02
    bsdf.inputs['IOR'].default_value = 1.40

    # C. Mandible Material
    mat_mandible = bpy.data.materials.new("M_Mandible_Master")
    mat_mandible.use_nodes = True
    nodes = mat_mandible.node_tree.nodes
    links = mat_mandible.node_tree.links
    nodes.clear()

    out = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    tex_mand = nodes.new('ShaderNodeTexImage')
    tex_mand.image = bpy.data.images.load(f"{web_dir}/mandible_pbr.png")
    links.new(tex_mand.outputs['Color'], bsdf.inputs['Base Color'])

    bsdf.inputs['Roughness'].default_value = 0.22
    bsdf.inputs['Coat Weight'].default_value = 0.80
    bsdf.inputs['Coat Roughness'].default_value = 0.12
    bsdf.inputs['Subsurface Weight'].default_value = 0.25
    bsdf.inputs['Subsurface Radius'].default_value = (0.8, 0.95, 0.5)

    # D. Limbs Material
    mat_limbs = bpy.data.materials.new("M_Limbs_Master")
    mat_limbs.use_nodes = True
    nodes = mat_limbs.node_tree.nodes
    links = mat_limbs.node_tree.links
    nodes.clear()

    out = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    tex_limbs = nodes.new('ShaderNodeTexImage')
    tex_limbs.image = bpy.data.images.load(f"{web_dir}/limbs_pbr.png")
    links.new(tex_limbs.outputs['Color'], bsdf.inputs['Base Color'])

    bsdf.inputs['Roughness'].default_value = 0.48
    bsdf.inputs['Coat Weight'].default_value = 0.35
    bsdf.inputs['Coat Roughness'].default_value = 0.25

    # E. Antenna Material
    mat_antenna = bpy.data.materials.new("M_Antenna_Master")
    mat_antenna.use_nodes = True
    nodes = mat_antenna.node_tree.nodes
    links = mat_antenna.node_tree.links
    nodes.clear()

    out = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    bsdf.inputs['Base Color'].default_value = (0.16, 0.12, 0.10, 1.0)
    bsdf.inputs['Roughness'].default_value = 0.65
    bsdf.inputs['Coat Weight'].default_value = 0.20
    bsdf.inputs['Coat Roughness'].default_value = 0.30

    return mat_chitin, mat_eye, mat_mandible, mat_limbs, mat_antenna

mat_chitin, mat_eye, mat_mandible, mat_limbs, mat_antenna = create_materials()

worker_col = bpy.data.collections.new("Worker_Ant_Master")
bpy.context.scene.collection.children.link(worker_col)

def reg(obj):
    worker_col.objects.link(obj)
    if obj.name in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.unlink(obj)
    return obj

# -----------------------------------------------------------------------------
# 3. Head: Unified Sculpted Cranium with Seamless Orbital Sockets
# -----------------------------------------------------------------------------
bm_head = bmesh.new()
bmesh.ops.create_cube(bm_head, size=1.0)
bmesh.ops.subdivide_edges(bm_head, edges=bm_head.edges, cuts=5, use_grid_fill=True)

# Sculpt head vertices to match master reference silhouette
for v in bm_head.verts:
    norm = v.co.normalized()
    x = norm.x * 0.50
    y = norm.y * 0.44
    z = norm.z * 0.46
    
    # 1. Crown loaf: rounded cushion with central furrow
    if z > 0.08:
        furrow = 1.0 - 0.12 * math.exp(-((x / 0.14) ** 2))
        z *= furrow
        x *= (1.0 + 0.14 * (z / 0.46))
        
    # 2. Eyebrow ridge: arches over each eye
    if 0.05 < z < 0.28 and y < -0.12:
        bf = math.sin((z - 0.05) / 0.23 * math.pi)
        y -= 0.08 * bf * (1.0 - abs(x / 0.50))

    # 3. Seamless Concave Orbital Sockets (around eyes at sign * 0.19, local y = -0.20, local z = 0.03)
    for sign in [-1.0, 1.0]:
        sx, sy, sz = sign * 0.19, -0.20, 0.03
        d = math.sqrt((x - sx)**2 + (y - sy)**2 + (z - sz)**2)
        r_sock = 0.26
        if d < r_sock:
            falloff = (1.0 - (d / r_sock)**2)**1.5
            y += 0.18 * falloff
            x -= sign * 0.05 * falloff

    # 4. Slender Clypeus / Snout between eyes (x in [-0.08, 0.08], z < 0.05)
    if z < 0.05:
        ts = min(1.0, (0.05 - z) / 0.50)
        # Narrow the central snout bridge only, keeping cheeks broad
        if abs(x) < 0.10:
            x *= (1.0 - 0.35 * ts)
            if y < 0:
                y -= 0.10 * (1.0 - abs(x / 0.10)) * ts
                
    # 5. Broad full cheeks flanking jaw (z in [-0.35, -0.05], abs(x) > 0.14)
    if -0.35 < z < -0.02 and abs(x) > 0.14:
        cheek_bulge = math.sin((z - (-0.35)) / 0.33 * math.pi)
        x *= (1.0 + 0.15 * cheek_bulge)
        if y < 0:
            y -= 0.05 * cheek_bulge

    v.co = Vector((x, y, z))

# UV Mapping: cylindrical with seam strictly at the back of the head
uv_layer = bm_head.loops.layers.uv.new("UVMap")
for face in bm_head.faces:
    for loop in face.loops:
        norm = loop.vert.co.normalized()
        u = math.atan2(norm.x, -norm.y) / (2.0 * math.pi) + 0.5
        v_coord = norm.z * 0.5 + 0.5
        loop[uv_layer].uv = (u, v_coord)

head_mesh = bpy.data.meshes.new("Head")
head_obj = bpy.data.objects.new("Head", head_mesh)
head_obj.location = Vector((0, -0.10, 1.35))
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
# 4. Large Expressive Compound Eyes (Seated flush inside sockets)
# -----------------------------------------------------------------------------
def make_expressive_eye(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    eye_pos = Vector((sign * 0.19, -0.30, 1.38))
    
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=48, ring_count=36, radius=0.22,
        location=eye_pos
    )
    eye = bpy.context.active_object
    eye.name = name
    
    yaw = -math.pi / 2.0 + (sign * math.radians(7.0))
    pitch = math.radians(2.0)
    eye.rotation_euler = Euler((pitch, 0.0, yaw), 'XYZ')
    eye.scale = Vector((0.96, 1.0, 1.10)) # Expressive vertical oval
    
    eye.data.materials.append(mat_eye)
    bpy.ops.object.shade_smooth()
    return reg(eye)

make_expressive_eye("Eye_L", True)
make_expressive_eye("Eye_R", False)

# -----------------------------------------------------------------------------
# 5. Curved Mandibles with Sculpted Teeth & Subsurface Scattering
# -----------------------------------------------------------------------------
def make_mandible(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    bm = bmesh.new()
    
    pts = [
        Vector((sign * 0.16, -0.22, 1.12)), # Cheek anchor
        Vector((sign * 0.20, -0.32, 1.08)), # Forward flare
        Vector((sign * 0.16, -0.40, 1.04)), # Apex curve
        Vector((sign * 0.08, -0.41, 1.02)), # Turning inward
        Vector((sign * 0.02, -0.38, 1.00))  # Tip
    ]
    rads = [0.075, 0.085, 0.070, 0.050, 0.020]
    
    num_p = 10
    rings = []
    for i, c in enumerate(pts):
        r = rads[i]
        c_ring = []
        for j in range(num_p):
            th = 2.0 * math.pi * j / num_p
            sharp = 1.0 - 0.25 * math.cos(th) if (math.cos(th) * sign > 0) else 1.0
            vx = c.x + r * sharp * math.cos(th)
            vy = c.y
            vz = c.z + r * math.sin(th)
            c_ring.append(bm.verts.new(Vector((vx, vy, vz))))
        rings.append(c_ring)
        
    for i in range(len(pts) - 1):
        r0, r1 = rings[i], rings[i+1]
        for j in range(num_p):
            jn = (j + 1) % num_p
            bm.faces.new([r0[j], r0[jn], r1[jn], r1[j]])
    bm.faces.new(rings[0][::-1])
    bm.faces.new(rings[-1])
    
    # 2 Sharp triangular grasping teeth along the inner edge
    teeth = [
        (Vector((sign * 0.03, -0.38, 1.00)), Vector((sign * 0.05, -0.40, 0.99)), Vector((sign * -0.005, -0.38, 1.00))),
        (Vector((sign * 0.08, -0.35, 1.02)), Vector((sign * 0.11, -0.37, 1.01)), Vector((sign * 0.045, -0.35, 1.02)))
    ]
    for b1, b2, tp in teeth:
        v1 = bm.verts.new(b1)
        v2 = bm.verts.new(b2)
        vt = bm.verts.new(tp)
        bm.faces.new([v1, v2, vt])

    # UV mapping: Base is green chitin, tip and teeth are chartreuse/pale lime
    uv_l = bm.loops.layers.uv.new("UVMap")
    for face in bm.faces:
        for loop in face.loops:
            v_val = min(1.0, max(0.0, (loop.vert.co.y + 0.42) / 0.20))
            u_val = loop.vert.co.z * 0.5 + 0.5
            loop[uv_l].uv = (u_val, v_val)

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

make_mandible("Mandible_L", True)
make_mandible("Mandible_R", False)

# -----------------------------------------------------------------------------
# 6. Crown-Rooted Hairpin Antennae
# -----------------------------------------------------------------------------
def make_hairpin_antenna(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    root_pos = Vector((sign * 0.09, -0.10, 1.66))
    
    knots = [
        (root_pos, 0.026),
        (Vector((sign * 0.11, -0.11, 1.84)), 0.024),
        (Vector((sign * 0.14, -0.09, 2.02)), 0.022),
        (Vector((sign * 0.20, 0.02, 2.14)),  0.020), # Hairpin apex
        (Vector((sign * 0.25, 0.10, 2.06)),  0.024), # Downward sweep
        (Vector((sign * 0.28, 0.14, 2.02)),  0.030)  # Swollen club tip
    ]
    
    bm = bmesh.new()
    num_pts = 8
    rings = []
    for pos, rad in knots:
        c_ring = []
        for j in range(num_pts):
            th = 2.0 * math.pi * j / num_pts
            vx = pos.x + rad * math.cos(th)
            vy = pos.y + rad * math.sin(th) * 0.7
            vz = pos.z
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
    ant_obj.data.materials.append(mat_antenna)
    sub_a = ant_obj.modifiers.new("Subsurf", 'SUBSURF')
    sub_a.levels = 2
    for p in mesh.polygons:
        p.use_smooth = True
    return reg(ant_obj)

make_hairpin_antenna("Antenna_L", True)
make_hairpin_antenna("Antenna_R", False)

# -----------------------------------------------------------------------------
# 7. Thorax: 3 Articulated Armor Plates & Petiole Waist
# -----------------------------------------------------------------------------
# Ribbed Neck
bpy.ops.mesh.primitive_cylinder_add(
    vertices=24, radius=0.15, depth=0.14,
    location=(0, -0.04, 1.10), rotation=(math.radians(20), 0, 0)
)
neck = bpy.context.active_object
neck.name = "Neck"
neck.data.materials.append(mat_chitin)
bpy.ops.object.shade_smooth()
reg(neck)

# 3 Overlapping Armor Plates: Pronotum, Mesonotum, Propodeum
plates = [
    ("Thorax_Pronotum",  Vector((0, 0.04, 0.98)), Vector((0.25, 0.23, 0.23))),
    ("Thorax_Mesonotum", Vector((0, 0.22, 0.88)), Vector((0.29, 0.27, 0.27))),
    ("Thorax_Propodeum", Vector((0, 0.38, 0.78)), Vector((0.25, 0.25, 0.23)))
]
for name, loc, scale in plates:
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=20, radius=1.0, location=loc)
    p_obj = bpy.context.active_object
    p_obj.name = name
    p_obj.scale = scale
    p_obj.data.materials.append(mat_chitin)
    sub = p_obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 1
    bpy.ops.object.shade_smooth()
    reg(p_obj)

# Petiole Waist Node
bpy.ops.mesh.primitive_cylinder_add(
    vertices=18, radius=0.09, depth=0.16,
    location=(0, 0.50, 0.65), rotation=(math.radians(45), 0, 0)
)
petiole = bpy.context.active_object
petiole.name = "Petiole"
petiole.data.materials.append(mat_chitin)
bpy.ops.object.shade_smooth()
reg(petiole)

# -----------------------------------------------------------------------------
# 8. Gaster (Abdomen): Teardrop Abdomen with 4 Tergal Segments
# -----------------------------------------------------------------------------
bm_gaster = bmesh.new()
gaster_data = [
    (Vector((0, 0.60, 0.58)), 0.22, 0.24), # Tergite 1
    (Vector((0, 0.74, 0.54)), 0.38, 0.40), # Tergite 2
    (Vector((0, 0.90, 0.48)), 0.46, 0.48), # Tergite 3 (Dorsal crest)
    (Vector((0, 1.04, 0.40)), 0.40, 0.42), # Tergite 4
    (Vector((0, 1.16, 0.30)), 0.26, 0.28), # Pygidium
    (Vector((0, 1.24, 0.22)), 0.08, 0.10)  # Tip
]
g_rings = []
num_pts = 16
for c, rx, rz in gaster_data:
    current_r = []
    for j in range(num_pts):
        th = 2.0 * math.pi * j / num_pts
        vx = c.x + rx * math.cos(th)
        vy = c.y
        vz = c.z + rz * math.sin(th)
        current_r.append(bm_gaster.verts.new(Vector((vx, vy, vz))))
    g_rings.append(current_r)

for i in range(len(gaster_data) - 1):
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
# 9. 6 Limbs: 2 Front Expressive Arms with Hands + 4 Walking Legs
# -----------------------------------------------------------------------------
def make_limb_cylinder(name, p0, p1, r_mid, r_ends):
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
            local_p = Vector((rad * 1.25 * math.cos(th), rad * 0.85 * math.sin(th), 0.0))
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
    obj.data.materials.append(mat_limbs)
    sub = obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 1
    for p in mesh.polygons:
        p.use_smooth = True
    return reg(obj)

def make_joint_sphere(name, loc, radius):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=12, radius=radius, location=loc)
    sph = bpy.context.active_object
    sph.name = name
    sph.data.materials.append(mat_limbs)
    bpy.ops.object.shade_smooth()
    return reg(sph)

# A. Front Expressive Arms with Hands (L & R)
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"
    
    p_shoulder = Vector((sign * 0.15, 0.04, 0.96))
    p_elbow    = Vector((sign * 0.30, -0.02, 0.72))
    p_wrist    = Vector((sign * 0.26, -0.18, 0.46))
    p_finger1  = Vector((sign * 0.26, -0.20, 0.34))
    p_finger2  = Vector((sign * 0.28, -0.16, 0.32))
    
    make_joint_sphere(f"Arm_Shoulder_{suf}", p_shoulder, 0.046)
    make_limb_cylinder(f"Arm_Upper_{suf}", p_shoulder, p_elbow, 0.044, 0.034)
    make_joint_sphere(f"Arm_Elbow_{suf}", p_elbow, 0.040)
    make_limb_cylinder(f"Arm_Forearm_{suf}", p_elbow, p_wrist, 0.034, 0.026)
    
    # Wrist collar sleeve
    make_joint_sphere(f"Arm_Wrist_{suf}", p_wrist, 0.030)
    # Articulated grasping fingers
    make_limb_cylinder(f"Arm_Finger1_{suf}", p_wrist, p_finger1, 0.018, 0.010)
    make_limb_cylinder(f"Arm_Finger2_{suf}", p_wrist, p_finger2, 0.016, 0.009)

# B. Middle Walking Legs (Planted on ground Z = 0)
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"
    
    p_hip   = Vector((sign * 0.19, 0.20, 0.86))
    p_knee  = Vector((sign * 0.36, -0.08, 0.58))
    p_ankle = Vector((sign * 0.34, -0.20, 0.06))
    p_foot  = Vector((sign * 0.33, -0.26, 0.0))
    
    make_joint_sphere(f"Leg_Mid_Hip_{suf}", p_hip, 0.048)
    make_limb_cylinder(f"Leg_Mid_Femur_{suf}", p_hip, p_knee, 0.046, 0.036)
    make_joint_sphere(f"Leg_Mid_Knee_{suf}", p_knee, 0.042)
    make_limb_cylinder(f"Leg_Mid_Tibia_{suf}", p_knee, p_ankle, 0.032, 0.024)
    make_limb_cylinder(f"Leg_Mid_Foot_{suf}", p_ankle, p_foot, 0.020, 0.012)

# C. Hind Walking Legs (Planted on ground Z = 0)
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"
    
    p_hip   = Vector((sign * 0.17, 0.36, 0.76))
    p_knee  = Vector((sign * 0.54, 0.42, 0.80))
    p_ankle = Vector((sign * 0.60, 0.56, 0.06))
    p_foot  = Vector((sign * 0.66, 0.66, 0.0))
    
    make_joint_sphere(f"Leg_Hind_Hip_{suf}", p_hip, 0.048)
    make_limb_cylinder(f"Leg_Hind_Femur_{suf}", p_hip, p_knee, 0.048, 0.036)
    make_joint_sphere(f"Leg_Hind_Knee_{suf}", p_knee, 0.042)
    make_limb_cylinder(f"Leg_Hind_Tibia_{suf}", p_knee, p_ankle, 0.032, 0.024)
    make_limb_cylinder(f"Leg_Hind_Foot_{suf}", p_ankle, p_foot, 0.020, 0.012)

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

add_light("Key_Light", 'POINT', 280.0, (1.0, 0.97, 0.92), (-2.2, -3.0, 3.4))
add_light("Fill_Light", 'POINT', 130.0, (0.84, 0.92, 1.0), (2.6, -2.2, 2.2))
add_light("Rim_Light", 'POINT', 340.0, (1.0, 1.0, 1.0), (0.0, 3.0, 3.5))
add_light("Bounce_Light", 'POINT', 45.0, (0.6, 0.8, 0.5), (0.0, -1.0, 0.2))

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
        Vector((0.0, -3.8, 1.15)),
        Euler((math.radians(88), 0, 0), 'XYZ'),
        "Front Master Stance"
    ),
    (
        "worker_perspective.png",
        Vector((-2.6, -2.8, 1.40)),
        Euler((math.radians(82), 0, math.radians(-42)), 'XYZ'),
        "3/4 Depth Perspective"
    ),
    (
        "worker_face_closeup.png",
        Vector((0.0, -1.9, 1.36)),
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
print(f"Exporting Worker Ant Master 3D GLB to: {glb_path}...")

bpy.ops.object.select_all(action='DESELECT')
for obj in worker_col.objects:
    obj.select_set(True)

bpy.ops.export_scene.gltf(
    filepath=glb_path,
    export_format='GLB',
    use_selection=True,
    export_apply=True
)

print("Worker Ant Master 3D GLB export complete!")

blend_path = "/Users/dchadd/Desktop/Ants-Mac/tools/blender/worker_ant_master.blend"
bpy.ops.wm.save_as_mainfile(filepath=blend_path)
print(f"Saved Blender file: {blend_path}")
