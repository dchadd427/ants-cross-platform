"""
Blender 4.3.2 Python Script: High-Fidelity 3D Polygonal Model of Worker Ant (Caste #1)
Authentic character modeling matching the master reference:
- Pear quad-sphere head with brow ridge, tapered snout, seamless front UVs
- Expressive inward-converging compound eyes with crisp PBR textures
- Pale lime-green clasping mandibles with distinct serrated fangs
- Forehead sockets with curved elbowed antennae
- Humped segmented thorax, petiole waist, and downward-angled teardrop gaster
- 6 articulated mahogany legs planted firmly on the floor at Z=0
- Cycles GPU raytracing & clean GLB export
"""

import bpy
import bmesh
import math
from mathutils import Vector, Euler
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

# 2. Material Definitions
def create_image_mat(name, img_path, roughness=0.28, clearcoat=0.80):
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    out = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    img = bpy.data.images.load(img_path)
    tex = nodes.new('ShaderNodeTexImage')
    tex.image = img
    links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])

    bsdf.inputs['Roughness'].default_value = roughness
    bsdf.inputs['Coat Weight'].default_value = clearcoat
    bsdf.inputs['Coat Roughness'].default_value = 0.12
    return mat

def create_color_mat(name, color_rgba, roughness=0.35, clearcoat=0.45):
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    out = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    bsdf.inputs['Base Color'].default_value = color_rgba
    bsdf.inputs['Roughness'].default_value = roughness
    bsdf.inputs['Coat Weight'].default_value = clearcoat
    bsdf.inputs['Coat Roughness'].default_value = 0.18
    return mat

def create_mandible_gradient_mat():
    # Dedicated material for pale lime-green biting pincers
    mat = bpy.data.materials.new(name="M_Mandible")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    out = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    uv_node = nodes.new('ShaderNodeUVMap')
    uv_node.uv_map = "UVMap"

    sep = nodes.new('ShaderNodeSeparateXYZ')
    links.new(uv_node.outputs['UV'], sep.inputs['Vector'])

    # Color ramp: V goes 0 at tip to 1 at base
    ramp = nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].position = 0.0 # Tip
    ramp.color_ramp.elements[0].color = (0.84, 0.95, 0.58, 1.0) # Pale lime-green
    ramp.color_ramp.elements[1].position = 0.85 # Base
    ramp.color_ramp.elements[1].color = (0.24, 0.44, 0.22, 1.0) # Emerald chitin

    links.new(sep.outputs['Y'], ramp.inputs['Fac'])
    links.new(ramp.outputs['Color'], bsdf.inputs['Base Color'])

    bsdf.inputs['Roughness'].default_value = 0.20
    bsdf.inputs['Coat Weight'].default_value = 0.85
    bsdf.inputs['Coat Roughness'].default_value = 0.10
    return mat

mat_chitin = create_image_mat("M_Chitin", f"{web_dir}/chitin_pbr.png", roughness=0.28, clearcoat=0.80)
mat_eye = create_image_mat("M_Eye", f"{web_dir}/eye_pbr.png", roughness=0.04, clearcoat=1.0)
mat_mandible = create_mandible_gradient_mat()
mat_leg = create_color_mat("M_Leg", (0.16, 0.08, 0.05, 1.0), roughness=0.36, clearcoat=0.45)
mat_antenna = create_color_mat("M_Antenna", (0.12, 0.07, 0.05, 1.0), roughness=0.40, clearcoat=0.35)

# Collection for clean GLTF export
worker_col = bpy.data.collections.new("Worker_Ant_3D")
bpy.context.scene.collection.children.link(worker_col)

def reg(obj):
    worker_col.objects.link(obj)
    if obj.name in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.unlink(obj)
    return obj

# -----------------------------------------------------------------------------
# 3. Head Mesh (Pear/Heart Quad Sphere with Eye Orbits and Tapered Snout)
# -----------------------------------------------------------------------------
bm = bmesh.new()
bmesh.ops.create_cube(bm, size=1.0)
bmesh.ops.subdivide_edges(bm, edges=bm.edges, cuts=4, use_grid_fill=True)

# Deform vertices into authentic Worker Ant cranium
for v in bm.verts:
    norm = v.co.normalized()
    # Dimensions: width 0.56, depth 0.48, height 0.52
    x = norm.x * 0.56
    y = norm.y * 0.48
    z = norm.z * 0.52
    
    # 1. Broad cranial dome at top
    if z > 0:
        dome = 1.0 + 0.14 * (z / 0.52)
        x *= dome
        y *= (1.0 + 0.06 * (z / 0.52))
        
    # 2. Tapered snout leading down to mouth (z < 0.05, y < 0)
    if z < 0.08:
        t_snout = (0.08 - z) / 0.58
        x *= (1.0 - 0.42 * min(1.0, t_snout))
        if y < 0:
            y -= 0.12 * min(1.0, t_snout) * (-y / 0.48)
            
    # 3. Smooth eye socket depressions (left at x = -0.25, right at x = 0.25)
    for sign in [-1.0, 1.0]:
        sx, sy, sz = sign * 0.25, -0.22, 0.02
        d_sock = math.sqrt((x - sx)**2 + (y - sy)**2 + (z - sz)**2)
        if d_sock < 0.28:
            falloff = 1.0 - (d_sock / 0.28)**2
            x -= sign * 0.06 * falloff
            y += 0.08 * falloff
            
    # 4. Brow ridge above eyes
    if 0.05 < z < 0.22 and y < -0.10:
        brow = math.sin((z - 0.05) / 0.17 * math.pi)
        y -= 0.045 * brow
        
    v.co = Vector((x, y, z))

# Add UVs for chitin texture — seam strictly at the BACK (+Y) of skull
uv_layer = bm.loops.layers.uv.new("UVMap")
for face in bm.faces:
    for loop in face.loops:
        norm = loop.vert.co.normalized()
        # atan2(x, -y): front (-y) gives angle near 0 -> u = 0.5 (continuous, no seam!)
        u = math.atan2(norm.x, -norm.y) / (2.0 * math.pi) + 0.5
        v_coord = norm.z * 0.5 + 0.5
        loop[uv_layer].uv = (u, v_coord)

mesh_head = bpy.data.meshes.new("Head")
bm.to_mesh(mesh_head)
bm.free()

head = bpy.data.objects.new("Head", mesh_head)
bpy.context.scene.collection.objects.link(head)
head.location = Vector((0, -0.04, 1.36))
head.data.materials.append(mat_chitin)

sub_head = head.modifiers.new("Subsurf", 'SUBSURF')
sub_head.levels = 2
for p in head.data.polygons:
    p.use_smooth = True
reg(head)

# -----------------------------------------------------------------------------
# 4. Expressive Compound Eyes (Inward-converging cute character angle)
# -----------------------------------------------------------------------------
def make_eye(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=48, ring_count=36, radius=0.25,
        location=(sign * 0.25, -0.27, 1.38)
    )
    eye = bpy.context.active_object
    eye.name = name
    eye.scale = Vector((0.92, 0.94, 1.10)) # Expressive ovoid shape
    
    # Rotate around Z by -90 deg so equator faces -Y (front).
    # Angle slightly inward (sign * +8 deg) so pupils slightly converge for innocent appeal!
    yaw = -math.pi / 2.0 + math.radians(sign * 8.0)
    pitch = math.radians(2.0)
    eye.rotation_euler = Euler((pitch, 0.0, yaw), 'XYZ')
    
    eye.data.materials.append(mat_eye)
    bpy.ops.object.shade_smooth()
    return reg(eye)

make_eye("Eye_L", True)
make_eye("Eye_R", False)

# -----------------------------------------------------------------------------
# 5. Clasping Mandibles with Pale Lime-Green Serrated Biting Teeth
# -----------------------------------------------------------------------------
def make_mandible(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    
    bm = bmesh.new()
    
    # 4 Cross-sectional rings along the inward curve
    centers = [
        Vector((sign * 0.14, -0.34, 0.96)),
        Vector((sign * 0.20, -0.46, 0.92)),
        Vector((sign * 0.14, -0.54, 0.89)),
        Vector((sign * 0.02, -0.51, 0.87))
    ]
    radii_x = [0.09, 0.12, 0.11, 0.05]
    radii_z = [0.08, 0.09, 0.08, 0.045]
    
    num_pts = 10
    ring_verts = []
    
    for i, c in enumerate(centers):
        rx = radii_x[i]
        rz = radii_z[i]
        current_ring = []
        for j in range(num_pts):
            theta = 2.0 * math.pi * j / num_pts
            vx = c.x + rx * math.cos(theta)
            vy = c.y
            vz = c.z + rz * math.sin(theta)
            v = bm.verts.new(Vector((vx, vy, vz)))
            current_ring.append(v)
        ring_verts.append(current_ring)
        
    # Bridge rings with quad faces
    for i in range(len(centers) - 1):
        r0 = ring_verts[i]
        r1 = ring_verts[i+1]
        for j in range(num_pts):
            j_next = (j + 1) % num_pts
            bm.faces.new([r0[j], r0[j_next], r1[j_next], r1[j]])
            
    # Cap the base (Ring 0) and tip (Ring 3)
    bm.faces.new(ring_verts[0][::-1])
    tip_face = bm.faces.new(ring_verts[-1])
    
    # Add 2 distinct triangular fangs/teeth on the inner edge (facing center X=0)
    # Tooth 1 near tip
    t1_b1 = bm.verts.new(Vector((sign * 0.05, -0.47, 0.89)))
    t1_b2 = bm.verts.new(Vector((sign * 0.08, -0.49, 0.87)))
    t1_tp = bm.verts.new(Vector((sign * -0.005, -0.48, 0.88)))
    bm.faces.new([t1_b1, t1_b2, t1_tp])
    
    # Tooth 2 mid-jaw
    t2_b1 = bm.verts.new(Vector((sign * 0.09, -0.42, 0.91)))
    t2_b2 = bm.verts.new(Vector((sign * 0.13, -0.44, 0.89)))
    t2_tp = bm.verts.new(Vector((sign * 0.03, -0.43, 0.90)))
    bm.faces.new([t2_b1, t2_b2, t2_tp])
    
    # Loop UV assignment: V maps 0 at tip (Y = -0.54) to 1 at base (Y = -0.34)
    uv_l = bm.loops.layers.uv.new("UVMap")
    for face in bm.faces:
        for loop in face.loops:
            v_coord = (loop.vert.co.y - (-0.54)) / 0.20
            u_coord = (loop.vert.co.x * sign) * 1.5 + 0.5
            loop[uv_l].uv = (u_coord, max(0.0, min(1.0, v_coord)))
            
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    
    mand_obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(mand_obj)
    mand_obj.data.materials.append(mat_mandible)
    sub_m = mand_obj.modifiers.new("Subsurf", 'SUBSURF')
    sub_m.levels = 2
    for p in mand_obj.data.polygons:
        p.use_smooth = True
    return reg(mand_obj)

make_mandible("Mandible_L", True)
make_mandible("Mandible_R", False)

# -----------------------------------------------------------------------------
# 6. Curved Elbowed Antennae with Forehead Sockets
# -----------------------------------------------------------------------------
def make_antenna(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    
    # Forehead socket mound
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=16, ring_count=12, radius=0.042,
        location=(sign * 0.14, -0.22, 1.70)
    )
    socket = bpy.context.active_object
    socket.name = f"{name}_Socket"
    socket.data.materials.append(mat_chitin)
    bpy.ops.object.shade_smooth()
    reg(socket)
    
    # Antenna stalk using BMesh chained cylinders
    bm = bmesh.new()
    knots = [
        (Vector((sign * 0.14, -0.22, 1.70)), 0.024),
        (Vector((sign * 0.18, -0.27, 1.88)), 0.022),
        (Vector((sign * 0.26, -0.23, 2.06)), 0.019),
        (Vector((sign * 0.38, -0.10, 2.28)), 0.014)
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
        r0 = rings[i]
        r1 = rings[i+1]
        for j in range(num_pts):
            j_next = (j + 1) % num_pts
            bm.faces.new([r0[j], r0[j_next], r1[j_next], r1[j]])
            
    bm.faces.new(rings[0][::-1])
    bm.faces.new(rings[-1])
    
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    
    antenna_obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(antenna_obj)
    antenna_obj.data.materials.append(mat_antenna)
    sub_a = antenna_obj.modifiers.new("Subsurf", 'SUBSURF')
    sub_a.levels = 1
    for p in antenna_obj.data.polygons:
        p.use_smooth = True
    return reg(antenna_obj)

make_antenna("Antenna_L", True)
make_antenna("Antenna_R", False)

# -----------------------------------------------------------------------------
# 7. Body: Neck Collar, 3-Segment Thorax, Petiole Waist, and Gaster Abdomen
# -----------------------------------------------------------------------------
# Neck Collar
bpy.ops.mesh.primitive_cylinder_add(
    vertices=24, radius=0.17, depth=0.16,
    location=(0, 0.02, 0.95), rotation=(math.radians(22), 0, 0)
)
neck = bpy.context.active_object
neck.name = "Neck"
neck.data.materials.append(mat_chitin)
bpy.ops.object.shade_smooth()
reg(neck)

# Thorax Segments (Prothorax, Mesothorax, Metathorax)
segments_data = [
    ("Thorax_Pro",  Vector((0, 0.08, 0.86)), Vector((0.26, 0.24, 0.25))),
    ("Thorax_Meso", Vector((0, 0.26, 0.80)), Vector((0.30, 0.28, 0.28))),
    ("Thorax_Meta", Vector((0, 0.44, 0.70)), Vector((0.25, 0.26, 0.24)))
]

for name, loc, scale in segments_data:
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=24, radius=1.0, location=loc)
    seg = bpy.context.active_object
    seg.name = name
    seg.scale = scale
    seg.data.materials.append(mat_chitin)
    sub_s = seg.modifiers.new("Subsurf", 'SUBSURF')
    sub_s.levels = 1
    bpy.ops.object.shade_smooth()
    reg(seg)

# Petiole (Narrow waist node)
bpy.ops.mesh.primitive_cylinder_add(
    vertices=20, radius=0.10, depth=0.18,
    location=(0, 0.58, 0.58), rotation=(math.radians(48), 0, 0)
)
petiole = bpy.context.active_object
petiole.name = "Petiole"
petiole.data.materials.append(mat_chitin)
bpy.ops.object.shade_smooth()
reg(petiole)

# Gaster (Plump teardrop abdomen pointing back and down)
bpy.ops.mesh.primitive_uv_sphere_add(
    segments=36, ring_count=28, radius=1.0,
    location=(0, 0.98, 0.42)
)
gaster = bpy.context.active_object
gaster.name = "Gaster"
gaster.scale = Vector((0.58, 0.92, 0.60))
gaster.rotation_euler = Euler((math.radians(-32), 0, 0), 'XYZ')
gaster.data.materials.append(mat_chitin)
sub_g = gaster.modifiers.new("Subsurf", 'SUBSURF')
sub_g.levels = 2
bpy.ops.object.shade_smooth()
reg(gaster)

# -----------------------------------------------------------------------------
# 8. 6 Articulated Legs (Coxa, Femur, Knee hinge, Tibia, and Tarsus Foot)
# -----------------------------------------------------------------------------
# Floor plane is at Z = 0.0 (all 6 feet firmly planted on the ground!)
legs_config = [
    # (name_prefix, coxa_pos, knee_pos, foot_pos, is_left)
    ("Leg_Front",  Vector((0.22, 0.08, 0.80)), Vector((0.48, -0.18, 0.62)), Vector((0.40, -0.48, 0.0)), False),
    ("Leg_Front",  Vector((0.22, 0.08, 0.80)), Vector((0.48, -0.18, 0.62)), Vector((0.40, -0.48, 0.0)), True),
    ("Leg_Middle", Vector((0.26, 0.26, 0.74)), Vector((0.76, 0.22, 0.72)),  Vector((0.72, 0.24, 0.0)),  False),
    ("Leg_Middle", Vector((0.26, 0.26, 0.74)), Vector((0.76, 0.22, 0.72)),  Vector((0.72, 0.24, 0.0)),  True),
    ("Leg_Hind",   Vector((0.24, 0.42, 0.64)), Vector((0.86, 0.70, 0.84)),  Vector((0.82, 0.94, 0.0)),  False),
    ("Leg_Hind",   Vector((0.24, 0.42, 0.64)), Vector((0.86, 0.70, 0.84)),  Vector((0.82, 0.94, 0.0)),  True),
]

def make_leg_segment(name, p0, p1, r0, r1):
    vec = p1 - p0
    dist = vec.length
    mid = (p0 + p1) * 0.5
    
    bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=1.0, depth=dist, location=mid)
    cyl = bpy.context.active_object
    cyl.name = name
    
    rot_quat = Vector((0, 0, 1)).rotation_difference(vec)
    cyl.rotation_euler = rot_quat.to_euler()
    cyl.scale = Vector((r0, r0, 1.0))
    cyl.data.materials.append(mat_leg)
    bpy.ops.object.shade_smooth()
    return reg(cyl)

def make_joint_sphere(name, loc, radius):
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
    
    # 1. Coxa ball at body
    make_joint_sphere(f"{prefix}_Coxa_{suffix}", p_coxa, 0.050)
    
    # 2. Femur (Coxa -> Knee)
    make_leg_segment(f"{prefix}_Femur_{suffix}", p_coxa, p_knee, 0.040, 0.036)
    
    # 3. Knee hinge sphere
    make_joint_sphere(f"{prefix}_Knee_{suffix}", p_knee, 0.044)
    
    # 4. Tibia (Knee -> Ankle)
    p_ankle = p_foot + Vector((0, 0, 0.04))
    make_leg_segment(f"{prefix}_Tibia_{suffix}", p_knee, p_ankle, 0.032, 0.024)
    
    # 5. Tarsus / Foot pad resting flat on ground (Z=0)
    p_toe = p_foot + Vector((sign * 0.04, -0.06, 0.0))
    make_leg_segment(f"{prefix}_Foot_{suffix}", p_ankle, p_toe, 0.022, 0.015)

# -----------------------------------------------------------------------------
# 9. Lighting Setup (Authentic 3-Point Studio + Rim)
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

# Key Light (Warm, top-left)
add_light("Key_Light", 'POINT', 260.0, (1.0, 0.96, 0.88), (-2.4, -3.2, 3.5))

# Fill Light (Cool, right-front)
add_light("Fill_Light", 'POINT', 110.0, (0.80, 0.90, 1.0), (2.8, -2.4, 2.2))

# Rim Light (Crisp rear highlight for chitin edge sheen)
add_light("Rim_Light", 'POINT', 300.0, (1.0, 1.0, 1.0), (0.0, 3.2, 3.6))

# Bounce Floor Light
add_light("Bounce_Light", 'POINT', 35.0, (0.6, 0.8, 0.5), (0.0, -1.2, 0.2))

# -----------------------------------------------------------------------------
# 10. Multi-Angle Cameras & Render Stills
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
        Vector((0.0, -1.8, 1.30)),
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
# 11. GLTF 2.0 Binary Export (worker_ant.glb)
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
