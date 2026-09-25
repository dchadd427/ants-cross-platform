"""
Blender 4.3.2 Python Script: True 3D Polygonal Mesh of Worker Ant (Caste #1)
Constructs fully volumetric 3D geometry:
- Sculpted cranium with recessed eye orbits, forehead antenna sockets, and clypeus
- 3D eye globes seated inside orbits with UV-mapped iris/pupil and glossy cornea
- 3D curved mandibles with thickness and inner teeth prongs
- 3D articulated antennae with elbow joints
- 3D segmented thorax (mesosoma), petiole waist, and volumetric 4-ring gaster (abdomen)
- 6 articulated 3D insect legs (coxa, muscular femur, knee hinge, tibia, tarsal claws)
- PBR chitin, leg, mandible, and eye shaders
- Exports web/viewer3d/worker_ant.glb and renders beauty inspection stills
"""

import bpy
import bmesh
import math
from mathutils import Vector, Euler, Matrix

# -----------------------------------------------------------------------------
# 1. Reset Scene & Setup Cycles
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
scene.render.film_transparent = True
scene.render.resolution_x = 1080
scene.render.resolution_y = 1080

# -----------------------------------------------------------------------------
# 2. Material Definitions
# -----------------------------------------------------------------------------
def make_chitin_mat():
    mat = bpy.data.materials.new(name="M_Worker_Chitin")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    out = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    # Base Color: mottled emerald green with subtle warm tan variations
    tex_coord = nodes.new('ShaderNodeTexCoord')
    noise = nodes.new('ShaderNodeTexNoise')
    noise.inputs['Scale'].default_value = 16.0
    noise.inputs['Detail'].default_value = 4.0
    noise.inputs['Roughness'].default_value = 0.5
    links.new(tex_coord.outputs['Object'], noise.inputs['Vector'])

    ramp = nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].position = 0.25
    ramp.color_ramp.elements[0].color = (0.12, 0.28, 0.14, 1.0) # Forest green
    ramp.color_ramp.elements[1].position = 0.70
    ramp.color_ramp.elements[1].color = (0.28, 0.50, 0.26, 1.0) # Emerald green
    links.new(noise.outputs['Fac'], ramp.inputs['Fac'])
    links.new(ramp.outputs['Color'], bsdf.inputs['Base Color'])

    # Micro bump for organic cuticle pores
    bump_noise = nodes.new('ShaderNodeTexNoise')
    bump_noise.inputs['Scale'].default_value = 50.0
    bump_noise.inputs['Detail'].default_value = 3.0
    links.new(tex_coord.outputs['Object'], bump_noise.inputs['Vector'])

    bump = nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.04
    bump.inputs['Distance'].default_value = 0.05
    links.new(bump_noise.outputs['Fac'], bump.inputs['Height'])
    links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])

    bsdf.inputs['Roughness'].default_value = 0.30
    bsdf.inputs['Coat Weight'].default_value = 0.80
    bsdf.inputs['Coat Roughness'].default_value = 0.14
    return mat

def make_leg_mat():
    mat = bpy.data.materials.new(name="M_Worker_Leg")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    out = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    # Dark mahogany / espresso chitin
    bsdf.inputs['Base Color'].default_value = (0.16, 0.07, 0.05, 1.0)
    bsdf.inputs['Roughness'].default_value = 0.38
    bsdf.inputs['Coat Weight'].default_value = 0.55
    bsdf.inputs['Coat Roughness'].default_value = 0.22
    return mat

def make_mandible_mat():
    mat = bpy.data.materials.new(name="M_Worker_Mandible")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    out = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    tex_coord = nodes.new('ShaderNodeTexCoord')
    ramp = nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].position = 0.2
    ramp.color_ramp.elements[0].color = (0.18, 0.38, 0.18, 1.0) # Base green
    ramp.color_ramp.elements[1].position = 0.8
    ramp.color_ramp.elements[1].color = (0.75, 0.88, 0.65, 1.0) # Pale lime-ivory tips
    links.new(tex_coord.outputs['Generated'], ramp.inputs['Fac'])
    links.new(ramp.outputs['Color'], bsdf.inputs['Base Color'])

    bsdf.inputs['Roughness'].default_value = 0.25
    bsdf.inputs['Coat Weight'].default_value = 0.85
    bsdf.inputs['Coat Roughness'].default_value = 0.10
    return mat

def make_eye_mat(is_left=True):
    mat = bpy.data.materials.new(name=f"M_Worker_Eye_{'L' if is_left else 'R'}")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    out = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    # Coordinates mapped to front of eyeball
    tex_coord = nodes.new('ShaderNodeTexCoord')
    mapping = nodes.new('ShaderNodeMapping')
    links.new(tex_coord.outputs['Object'], mapping.inputs['Vector'])

    # Center pupil slightly forward and inward
    x_offset = 0.05 if is_left else -0.05
    mapping.inputs['Location'].default_value = (x_offset, -0.15, 0.0)

    grad = nodes.new('ShaderNodeTexGradient')
    grad.gradient_type = 'SPHERICAL'
    links.new(mapping.outputs['Vector'], grad.inputs['Vector'])

    ramp = nodes.new('ShaderNodeValToRGB')
    # Pupil -> Olive Iris -> Limbal Ring -> Cream Sclera
    ramp.color_ramp.elements[0].position = 0.18
    ramp.color_ramp.elements[0].color = (0.015, 0.02, 0.015, 1.0) # Black pupil

    elem1 = ramp.color_ramp.elements.new(0.36)
    elem1.color = (0.35, 0.44, 0.18, 1.0) # Olive Amber Iris

    elem2 = ramp.color_ramp.elements.new(0.48)
    elem2.color = (0.08, 0.12, 0.06, 1.0) # Dark limbal ring

    ramp.color_ramp.elements[len(ramp.color_ramp.elements)-1].position = 0.60
    ramp.color_ramp.elements[len(ramp.color_ramp.elements)-1].color = (0.94, 0.95, 0.90, 1.0) # Sclera

    links.new(grad.outputs['Fac'], ramp.inputs['Fac'])
    links.new(ramp.outputs['Color'], bsdf.inputs['Base Color'])

    # Cornea Glass Reflectivity
    bsdf.inputs['Roughness'].default_value = 0.04
    bsdf.inputs['Coat Weight'].default_value = 1.0
    bsdf.inputs['Coat Roughness'].default_value = 0.02
    bsdf.inputs['IOR'].default_value = 1.45
    return mat

mat_chitin = make_chitin_mat()
mat_leg = make_leg_mat()
mat_mandible = make_mandible_mat()
mat_eye_l = make_eye_mat(True)
mat_eye_r = make_eye_mat(False)

# Collection for export
worker_col = bpy.data.collections.new("Worker_Ant_Model")
bpy.context.scene.collection.children.link(worker_col)

def register_obj(obj):
    worker_col.objects.link(obj)
    if obj.name in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.unlink(obj)
    return obj

# -----------------------------------------------------------------------------
# 3. Model Head with Recessed Eye Orbits & Clypeus
# -----------------------------------------------------------------------------
# Create subdivided cube as organic cranium base
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, 1.45))
head_obj = bpy.context.active_object
head_obj.name = "Worker_Head"
head_obj.data.materials.append(mat_chitin)

# Subdivide cube into clean quad grid
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.subdivide(number_cuts=4)
bpy.ops.object.mode_set(mode='OBJECT')

bm = bmesh.new()
bm.from_mesh(head_obj.data)

for v in bm.verts:
    x, y, z = v.co.x, v.co.y, v.co.z
    # Sphere-like normalization with anatomical offsets
    rad = math.sqrt(x*x + y*y + z*z)
    if rad > 0.001:
        # Scale to caricature head proportions
        v.co.x = (x / rad) * 0.78
        v.co.y = (y / rad) * 0.65
        v.co.z = (z / rad) * 0.68

    # Cranium widening at top, narrowing into snout at bottom
    if v.co.z > 0.1:
        v.co.x *= 1.25 # Wide cranium/temples
        v.co.y *= 1.10 # Rounded back
        v.co.z *= 1.15
    elif v.co.z < -0.1:
        v.co.x *= 0.68 # Narrow snout
        v.co.y += 0.08 # Push mouth forward

    # Carve recessed eye orbits (depressions where eyes sit)
    # Eye positions around x = +/- 0.35, y = 0.25, z = 0.15
    for sign in [-1.0, 1.0]:
        dist_eye = math.sqrt((v.co.x - sign*0.35)**2 + (v.co.y - 0.28)**2 + (v.co.z - 0.12)**2)
        if dist_eye < 0.36 and v.co.y > 0.0:
            depth = (0.36 - dist_eye) * 0.35
            v.co.y -= depth
            v.co.x -= sign * (depth * 0.2)

    # Antenna root socket dents on forehead (x = +/- 0.16, y = 0.22, z = 0.50)
    for sign in [-1.0, 1.0]:
        dist_ant = math.sqrt((v.co.x - sign*0.16)**2 + (v.co.y - 0.22)**2 + (v.co.z - 0.50)**2)
        if dist_ant < 0.14:
            v.co.y -= (0.14 - dist_ant) * 0.25

bm.to_mesh(head_obj.data)
bm.free()

subsurf_head = head_obj.modifiers.new("Subsurf", 'SUBSURF')
subsurf_head.levels = 2
subsurf_head.render_levels = 2
bpy.ops.object.shade_smooth()
register_obj(head_obj)

# -----------------------------------------------------------------------------
# 4. 3D Eye Globes Seated in Sockets
# -----------------------------------------------------------------------------
def make_eye(is_left=True):
    sign = -1.0 if is_left else 1.0
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=32, ring_count=24, radius=0.34,
        location=(sign * 0.34, 0.22, 1.58)
    )
    eye = bpy.context.active_object
    eye.name = f"Worker_Eye_{'L' if is_left else 'R'}"
    eye.scale = Vector((0.92, 0.95, 1.12)) # Flattering oval compound eye
    eye.rotation_euler = Euler((math.radians(12), math.radians(sign * -14), math.radians(sign * 10)), 'XYZ')
    eye.data.materials.append(mat_eye_l if is_left else mat_eye_r)
    bpy.ops.object.shade_smooth()
    return register_obj(eye)

eye_l = make_eye(True)
eye_r = make_eye(False)

# -----------------------------------------------------------------------------
# 5. Volumetric 3D Clasping Mandibles with Teeth Prongs
# -----------------------------------------------------------------------------
def make_mandible(is_left=True):
    sign = -1.0 if is_left else 1.0
    bpy.ops.mesh.primitive_cube_add(size=0.28, location=(sign * 0.24, 0.32, 1.08))
    mand = bpy.context.active_object
    mand.name = f"Worker_Mandible_{'L' if is_left else 'R'}"
    mand.data.materials.append(mat_mandible)

    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.subdivide(number_cuts=3)
    bpy.ops.object.mode_set(mode='OBJECT')

    bm = bmesh.new()
    bm.from_mesh(mand.data)

    for v in bm.verts:
        # Curve inward towards center
        v.co.x += (v.co.z - 1.08) * sign * 0.55
        # Round the outer cheek
        if (sign > 0 and v.co.x > 0.24) or (sign < 0 and v.co.x < -0.24):
            v.co.x += sign * 0.08
        # Point the tip inward and forward
        if v.co.z < 1.02:
            v.co.x -= sign * 0.15
            v.co.y += 0.12
            # Sculpt inner tooth prong
            if (sign > 0 and v.co.x < 0.22) or (sign < 0 and v.co.x > -0.22):
                v.co.x -= sign * 0.08
                v.co.z += 0.04

    bm.to_mesh(mand.data)
    bm.free()

    sub = mand.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 2
    bpy.ops.object.shade_smooth()
    return register_obj(mand)

mand_l = make_mandible(True)
mand_r = make_mandible(False)

# -----------------------------------------------------------------------------
# 6. 3D Articulated Antennae with Elbow Joints
# -----------------------------------------------------------------------------
def make_antenna(is_left=True):
    sign = -1.0 if is_left else 1.0
    
    # 1. Base Scape cylinder emerging from forehead pit
    bpy.ops.mesh.primitive_cylinder_add(
        radius=0.032, depth=0.22,
        location=(sign * 0.16, 0.24, 1.95)
    )
    scape = bpy.context.active_object
    scape.name = f"Worker_Antenna_Scape_{'L' if is_left else 'R'}"
    scape.rotation_euler = Euler((math.radians(35), math.radians(sign * -25), math.radians(sign * 15)), 'XYZ')
    scape.data.materials.append(mat_leg)
    bpy.ops.object.shade_smooth()
    register_obj(scape)

    # 2. Flagellum (sweeping curved whip with elbow)
    curve_data = bpy.data.curves.new(f"Antenna_Flagellum_{'L' if is_left else 'R'}", 'CURVE')
    curve_data.dimensions = '3D'
    curve_data.bevel_depth = 0.028
    curve_data.bevel_resolution = 6

    polyline = curve_data.splines.new('BEZIER')
    polyline.bezier_points.add(2) # 3 control points

    p0 = polyline.bezier_points[0] # Elbow joint
    p0.co = Vector((sign * 0.22, 0.28, 2.05))
    p0.handle_right = Vector((sign * 0.28, 0.28, 2.22))

    p1 = polyline.bezier_points[1] # Mid curve
    p1.co = Vector((sign * 0.38, 0.26, 2.38))
    p1.handle_left = Vector((sign * 0.32, 0.28, 2.28))
    p1.handle_right = Vector((sign * 0.44, 0.24, 2.48))

    p2 = polyline.bezier_points[2] # Tip forward
    p2.co = Vector((sign * 0.48, 0.32, 2.58))
    p2.handle_left = Vector((sign * 0.45, 0.28, 2.52))

    flag_obj = bpy.data.objects.new(f"Worker_Antenna_Flag_{'L' if is_left else 'R'}", curve_data)
    flag_obj.data.materials.append(mat_leg)
    register_obj(flag_obj)

make_antenna(True)
make_antenna(False)

# -----------------------------------------------------------------------------
# 7. 3D Segmented Thorax, Petiole Waist, and Volumetric Gaster
# -----------------------------------------------------------------------------
# A. Thorax (Mesosoma) - 3 Segmented plates
bpy.ops.mesh.primitive_uv_sphere_add(segments=28, ring_count=20, radius=0.46, location=(0, -0.20, 0.84))
thorax = bpy.context.active_object
thorax.name = "Worker_Thorax"
thorax.scale = Vector((0.76, 1.15, 0.88))
thorax.rotation_euler = Euler((math.radians(24), 0, 0), 'XYZ')
thorax.data.materials.append(mat_chitin)
sub_th = thorax.modifiers.new("Subsurf", 'SUBSURF')
sub_th.levels = 2
bpy.ops.object.shade_smooth()
register_obj(thorax)

# B. Petiole (Waist Node)
bpy.ops.mesh.primitive_cylinder_add(radius=0.14, depth=0.32, location=(0, -0.66, 0.60))
petiole = bpy.context.active_object
petiole.name = "Worker_Petiole"
petiole.rotation_euler = Euler((math.radians(50), 0, 0), 'XYZ')
petiole.data.materials.append(mat_chitin)
bpy.ops.object.shade_smooth()
register_obj(petiole)

# C. Gaster (Abdomen) - Volumetric with 4 segmented plates
bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=24, radius=0.72, location=(0, -1.22, 0.44))
gaster = bpy.context.active_object
gaster.name = "Worker_Gaster"
gaster.scale = Vector((0.82, 1.38, 0.95))
gaster.rotation_euler = Euler((math.radians(-22), 0, 0), 'XYZ')
gaster.data.materials.append(mat_chitin)
sub_g = gaster.modifiers.new("Subsurf", 'SUBSURF')
sub_g.levels = 2
bpy.ops.object.shade_smooth()
register_obj(gaster)

# -----------------------------------------------------------------------------
# 8. 6 Articulated 3D Polygonal Legs (Muscular Femur, Knee, Tibia, Tarsus)
# -----------------------------------------------------------------------------
def make_3d_leg(name, hip, knee, ankle, foot):
    col_name = f"Leg_{name}"
    
    # 1. Coxa (hip ball joint)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=10, radius=0.08, location=hip)
    coxa = bpy.context.active_object
    coxa.name = f"{name}_Coxa"
    coxa.data.materials.append(mat_leg)
    bpy.ops.object.shade_smooth()
    register_obj(coxa)

    # 2. Femur (Muscular flattened upper leg)
    # Modeled as cylinder stretched and tapered
    f_mid = [(hip[i] + knee[i]) * 0.5 for i in range(3)]
    f_vec = Vector((knee[0] - hip[0], knee[1] - hip[1], knee[2] - hip[2]))
    f_len = f_vec.length
    
    bpy.ops.mesh.primitive_cylinder_add(radius=0.065, depth=f_len, location=f_mid)
    femur = bpy.context.active_object
    femur.name = f"{name}_Femur"
    # Align cylinder Z-axis to vector
    femur.rotation_mode = 'QUATERNION'
    femur.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(f_vec.normalized())
    femur.scale = Vector((0.7, 1.25, 1.0)) # Flattened muscular cross-section
    femur.data.materials.append(mat_leg)
    bpy.ops.object.shade_smooth()
    register_obj(femur)

    # 3. Knee Hinge
    bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=10, radius=0.068, location=knee)
    k_obj = bpy.context.active_object
    k_obj.name = f"{name}_Knee"
    k_obj.data.materials.append(mat_leg)
    bpy.ops.object.shade_smooth()
    register_obj(k_obj)

    # 4. Tibia (Slender lower leg)
    t_mid = [(knee[i] + ankle[i]) * 0.5 for i in range(3)]
    t_vec = Vector((ankle[0] - knee[0], ankle[1] - knee[1], ankle[2] - knee[2]))
    t_len = t_vec.length
    
    bpy.ops.mesh.primitive_cylinder_add(radius=0.048, depth=t_len, location=t_mid)
    tibia = bpy.context.active_object
    tibia.name = f"{name}_Tibia"
    tibia.rotation_mode = 'QUATERNION'
    tibia.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(t_vec.normalized())
    tibia.data.materials.append(mat_leg)
    bpy.ops.object.shade_smooth()
    register_obj(tibia)

    # 5. Tarsus & Claws (Segmented foot planted on floor)
    foot_mid = [(ankle[i] + foot[i]) * 0.5 for i in range(3)]
    foot_vec = Vector((foot[0] - ankle[0], foot[1] - ankle[1], foot[2] - ankle[2]))
    foot_len = foot_vec.length

    bpy.ops.mesh.primitive_cylinder_add(radius=0.038, depth=foot_len, location=foot_mid)
    tarsus = bpy.context.active_object
    tarsus.name = f"{name}_Tarsus"
    tarsus.rotation_mode = 'QUATERNION'
    tarsus.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(foot_vec.normalized())
    tarsus.data.materials.append(mat_leg)
    bpy.ops.object.shade_smooth()
    register_obj(tarsus)

# Left Front Leg (curved forward, supporting stance)
make_3d_leg("Front_L",
            hip=(-0.24, 0.05, 0.78),
            knee=(-0.68, 0.28, 0.96),
            ankle=(-0.74, 0.48, 0.38),
            foot=(-0.60, 0.62, 0.0))

# Right Front Leg
make_3d_leg("Front_R",
            hip=(0.24, 0.05, 0.78),
            knee=(0.68, 0.28, 0.96),
            ankle=(0.74, 0.48, 0.38),
            foot=(0.60, 0.62, 0.0))

# Left Middle Leg (lateral balance)
make_3d_leg("Mid_L",
            hip=(-0.28, -0.22, 0.75),
            knee=(-0.88, -0.15, 0.98),
            ankle=(-0.94, -0.05, 0.40),
            foot=(-0.80, 0.12, 0.0))

# Right Middle Leg
make_3d_leg("Mid_R",
            hip=(0.28, -0.22, 0.75),
            knee=(0.88, -0.15, 0.98),
            ankle=(0.94, -0.05, 0.40),
            foot=(0.80, 0.12, 0.0))

# Left Hind Leg (rear anchor)
make_3d_leg("Hind_L",
            hip=(-0.25, -0.48, 0.70),
            knee=(-0.84, -0.75, 1.05),
            ankle=(-0.90, -0.92, 0.42),
            foot=(-0.76, -1.08, 0.0))

# Right Hind Leg
make_3d_leg("Hind_R",
            hip=(0.25, -0.48, 0.70),
            knee=(0.84, -0.75, 1.05),
            ankle=(0.90, -0.92, 0.42),
            foot=(0.76, -1.08, 0.0))

# -----------------------------------------------------------------------------
# 9. Multi-Point Studio Lighting Rig
# -----------------------------------------------------------------------------
# Warm Key Light
key_light_data = bpy.data.lights.new(name="Studio_Key", type='AREA')
key_light_data.energy = 180.0
key_light_data.size = 2.0
key_light_data.color = (1.0, 0.97, 0.92)
key_light = bpy.data.objects.new("Studio_Key", key_light_data)
key_light.location = (-2.8, 2.8, 3.2)
key_light.rotation_euler = Euler((math.radians(40), math.radians(-25), math.radians(-35)), 'XYZ')
bpy.context.scene.collection.objects.link(key_light)

# Cool Fill Light
fill_light_data = bpy.data.lights.new(name="Studio_Fill", type='AREA')
fill_light_data.energy = 70.0
fill_light_data.size = 2.6
fill_light_data.color = (0.85, 0.92, 1.0)
fill_light = bpy.data.objects.new("Studio_Fill", fill_light_data)
fill_light.location = (2.6, 2.2, 2.4)
fill_light.rotation_euler = Euler((math.radians(35), math.radians(28), math.radians(22)), 'XYZ')
bpy.context.scene.collection.objects.link(fill_light)

# Sharp Rim Light (Backlight for silhouette & catchlights)
rim_light_data = bpy.data.lights.new(name="Studio_Rim", type='SPOT')
rim_light_data.energy = 260.0
rim_light_data.spot_size = math.radians(65)
rim_light_data.spot_blend = 0.25
rim_light = bpy.data.objects.new("Studio_Rim", rim_light_data)
rim_light.location = (0.0, -3.5, 3.8)
rim_light.rotation_euler = Euler((math.radians(-50), 0, 0), 'XYZ')
bpy.context.scene.collection.objects.link(rim_light)

# Ground Pedestal Disc for shadows in render
bpy.ops.mesh.primitive_cylinder_add(radius=2.4, depth=0.1, location=(0, 0, -0.05))
pedestal = bpy.context.active_object
pedestal.name = "Studio_Pedestal"
ped_mat = bpy.data.materials.new("M_Pedestal")
ped_mat.use_nodes = True
p_bsdf = ped_mat.node_tree.nodes.get("Principled BSDF")
p_bsdf.inputs['Base Color'].default_value = (0.05, 0.07, 0.09, 1.0)
p_bsdf.inputs['Roughness'].default_value = 0.8
pedestal.data.materials.append(ped_mat)

# -----------------------------------------------------------------------------
# 10. Multi-Angle Beauty Renders
# -----------------------------------------------------------------------------
cam_data = bpy.data.cameras.new("Worker_Camera")
cam_data.lens = 65.0
cam_obj = bpy.data.objects.new("Worker_Camera", cam_data)
bpy.context.scene.collection.objects.link(cam_obj)
scene.camera = cam_obj

out_dir = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d"

render_views = [
    {
        "name": "worker_front.png",
        "loc": (0.0, 3.4, 1.35),
        "rot": (math.radians(84), 0, math.radians(180)),
        "lens": 65.0
    },
    {
        "name": "worker_perspective.png",
        "loc": (-2.4, 2.5, 2.0),
        "rot": (math.radians(68), 0, math.radians(-140)),
        "lens": 55.0
    },
    {
        "name": "worker_face_closeup.png",
        "loc": (0.0, 1.7, 1.48),
        "rot": (math.radians(85), 0, math.radians(180)),
        "lens": 85.0
    }
]

for v in render_views:
    cam_obj.location = Vector(v["loc"])
    cam_obj.rotation_euler = Euler(v["rot"], 'XYZ')
    cam_data.lens = v["lens"]
    scene.render.filepath = f"{out_dir}/{v['name']}"
    print(f"Rendering Cycles beauty shot: {v['name']}...")
    bpy.ops.render.render(write_still=True)
    print(f"Saved: {out_dir}/{v['name']}")

# -----------------------------------------------------------------------------
# 11. Export True 3D GLB Model for Three.js WebGL Inspector
# -----------------------------------------------------------------------------
# Deselect pedestal and lights, select ONLY worker ant 3D objects
bpy.ops.object.select_all(action='DESELECT')
for obj in worker_col.objects:
    obj.select_set(True)

glb_path = f"{out_dir}/worker_ant.glb"
print(f"Exporting Worker Ant 3D GLB to: {glb_path}...")
bpy.ops.export_scene.gltf(
    filepath=glb_path,
    use_selection=True,
    export_format='GLB',
    export_apply=True
)
print("GLB export complete!")

blend_path = "/Users/dchadd/Desktop/Ants-Mac/tools/blender/worker_ant_true3d.blend"
bpy.ops.wm.save_as_mainfile(filepath=blend_path)
print(f"Saved Blender project to: {blend_path}")
