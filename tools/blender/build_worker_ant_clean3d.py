"""
Blender 4.3.2 Python Script: True 3D Polygonal Model of Worker Ant (Caste #1)
Assembles fully volumetric 3D polygonal geometry with proper anatomical proportions,
textured PBR materials (chitin, expressive eyes with pupils, pale mandibles, mahogany legs),
and exports an authentic, clean worker_ant.glb for the Three.js WebGL Inspector.
"""

import bpy
import bmesh
import math
from mathutils import Vector, Euler

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

# 2. Material Setup
def create_image_mat(name, img_path, roughness=0.3, clearcoat=0.8):
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

def create_color_mat(name, color_rgba, roughness=0.35, clearcoat=0.5):
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

mat_chitin = create_image_mat("M_Chitin", f"{web_dir}/chitin_pbr.png", roughness=0.28, clearcoat=0.85)
mat_eye = create_image_mat("M_Eye", f"{web_dir}/eye_pbr.png", roughness=0.04, clearcoat=1.0)
mat_mandible = create_image_mat("M_Mandible", f"{web_dir}/mandible_pbr.png", roughness=0.24, clearcoat=0.8)
mat_leg = create_color_mat("M_Leg", (0.16, 0.08, 0.05, 1.0), roughness=0.40, clearcoat=0.4)

# Collection for export
worker_col = bpy.data.collections.new("Worker_Ant_3D")
bpy.context.scene.collection.children.link(worker_col)

def reg(obj):
    worker_col.objects.link(obj)
    if obj.name in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.unlink(obj)
    return obj

# -----------------------------------------------------------------------------
# 3. 3D Head (Wide rounded cranium with snout)
# -----------------------------------------------------------------------------
# Ant faces towards -Y (standard front view)
bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=24, radius=0.62, location=(0, 0, 1.45))
head = bpy.context.active_object
head.name = "Head"
head.scale = Vector((1.22, 0.98, 1.08)) # Bean/heart width
head.data.materials.append(mat_chitin)

# Deform snout and eye sockets in bmesh
bm = bmesh.new()
bm.from_mesh(head.data)
for v in bm.verts:
    # Snout narrowing down towards mandibles (Z < 0, Y < 0)
    if v.co.z < -0.1:
        v.co.x *= 0.72 # Narrow
        v.co.y -= 0.12 # Push snout forward (-Y)
    elif v.co.z > 0.1:
        v.co.x *= 1.15 # Broad temples
        v.co.y += 0.08 # Round cranium back (+Y)
bm.to_mesh(head.data)
bm.free()

sub_head = head.modifiers.new("Subsurf", 'SUBSURF')
sub_head.levels = 2
bpy.ops.object.shade_smooth()
reg(head)

# -----------------------------------------------------------------------------
# 4. 3D Eye Globes (Seated in front orbits)
# -----------------------------------------------------------------------------
def make_eye(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    # Placed in front (-Y) of head
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=32, ring_count=24, radius=0.34,
        location=(sign * 0.32, -0.32, 1.55)
    )
    eye = bpy.context.active_object
    eye.name = name
    eye.scale = Vector((0.95, 0.92, 1.12)) # Oval compound eye
    # Rotate eye so pupil faces forward (-Y) and slightly inward
    eye.rotation_euler = Euler((math.radians(90), math.radians(sign * -12), math.radians(sign * 8)), 'XYZ')
    eye.data.materials.append(mat_eye)
    bpy.ops.object.shade_smooth()
    return reg(eye)

make_eye("Eye_L", True)
make_eye("Eye_R", False)

# -----------------------------------------------------------------------------
# 5. 3D Mandibles (Clasping pincers with pale lime tips)
# -----------------------------------------------------------------------------
def make_mandible(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=24, ring_count=18, radius=0.22,
        location=(sign * 0.20, -0.42, 1.08)
    )
    mand = bpy.context.active_object
    mand.name = name
    mand.scale = Vector((0.9, 1.1, 0.75))
    mand.data.materials.append(mat_mandible)

    bm = bmesh.new()
    bm.from_mesh(mand.data)
    for v in bm.verts:
        # Curve inward towards center
        v.co.x += (v.co.z - 1.08) * sign * 0.45
        # Tip points forward (-Y)
        if v.co.z < 1.05:
            v.co.y -= 0.10
            v.co.x -= sign * 0.08
    bm.to_mesh(mand.data)
    bm.free()

    sub = mand.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 2
    bpy.ops.object.shade_smooth()
    return reg(mand)

make_mandible("Mandible_L", True)
make_mandible("Mandible_R", False)

# -----------------------------------------------------------------------------
# 6. 3D Antennae (Elbowed stalks on forehead)
# -----------------------------------------------------------------------------
def make_antenna(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    curve_data = bpy.data.curves.new(name, 'CURVE')
    curve_data.dimensions = '3D'
    curve_data.bevel_depth = 0.028
    curve_data.bevel_resolution = 6

    spline = curve_data.splines.new('BEZIER')
    spline.bezier_points.add(2) # 3 points

    # Base at forehead
    p0 = spline.bezier_points[0]
    p0.co = Vector((sign * 0.16, -0.22, 1.98))
    p0.handle_right = Vector((sign * 0.24, -0.24, 2.15))

    # Mid arch
    p1 = spline.bezier_points[1]
    p1.co = Vector((sign * 0.38, -0.20, 2.36))
    p1.handle_left = Vector((sign * 0.30, -0.22, 2.26))
    p1.handle_right = Vector((sign * 0.44, -0.16, 2.46))

    # Tip curving forward
    p2 = spline.bezier_points[2]
    p2.co = Vector((sign * 0.48, -0.26, 2.55))
    p2.handle_left = Vector((sign * 0.45, -0.22, 2.50))

    obj = bpy.data.objects.new(name, curve_data)
    obj.data.materials.append(mat_leg)
    return reg(obj)

make_antenna("Antenna_L", True)
make_antenna("Antenna_R", False)

# -----------------------------------------------------------------------------
# 7. 3D Body (Thorax, Petiole, Gaster)
# -----------------------------------------------------------------------------
# Thorax (Mesosoma)
bpy.ops.mesh.primitive_uv_sphere_add(segments=28, ring_count=20, radius=0.44, location=(0, 0.20, 0.82))
thorax = bpy.context.active_object
thorax.name = "Thorax"
thorax.scale = Vector((0.75, 1.15, 0.85))
thorax.rotation_euler = Euler((math.radians(-25), 0, 0), 'XYZ')
thorax.data.materials.append(mat_chitin)
sub_th = thorax.modifiers.new("Subsurf", 'SUBSURF')
sub_th.levels = 2
bpy.ops.object.shade_smooth()
reg(thorax)

# Petiole (Waist Node)
bpy.ops.mesh.primitive_cylinder_add(radius=0.12, depth=0.30, location=(0, 0.65, 0.58))
petiole = bpy.context.active_object
petiole.name = "Petiole"
petiole.rotation_euler = Euler((math.radians(-50), 0, 0), 'XYZ')
petiole.data.materials.append(mat_chitin)
bpy.ops.object.shade_smooth()
reg(petiole)

# Gaster (Abdomen)
bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=24, radius=0.70, location=(0, 1.20, 0.42))
gaster = bpy.context.active_object
gaster.name = "Gaster"
gaster.scale = Vector((0.80, 1.35, 0.92))
gaster.rotation_euler = Euler((math.radians(20), 0, 0), 'XYZ')
gaster.data.materials.append(mat_chitin)
sub_g = gaster.modifiers.new("Subsurf", 'SUBSURF')
sub_g.levels = 2
bpy.ops.object.shade_smooth()
reg(gaster)

# -----------------------------------------------------------------------------
# 8. 6 Articulated 3D Legs (Coxa, Femur, Knee, Tibia, Tarsus)
# -----------------------------------------------------------------------------
def make_3d_leg(name, hip, knee, ankle, foot):
    # 1. Coxa
    bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=10, radius=0.075, location=hip)
    coxa = bpy.context.active_object
    coxa.name = f"{name}_Coxa"
    coxa.data.materials.append(mat_leg)
    bpy.ops.object.shade_smooth()
    reg(coxa)

    # 2. Femur
    f_mid = [(hip[i] + knee[i]) * 0.5 for i in range(3)]
    f_vec = Vector((knee[0] - hip[0], knee[1] - hip[1], knee[2] - hip[2]))
    bpy.ops.mesh.primitive_cylinder_add(radius=0.06, depth=f_vec.length, location=f_mid)
    femur = bpy.context.active_object
    femur.name = f"{name}_Femur"
    femur.rotation_mode = 'QUATERNION'
    femur.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(f_vec.normalized())
    femur.data.materials.append(mat_leg)
    bpy.ops.object.shade_smooth()
    reg(femur)

    # 3. Knee
    bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=10, radius=0.065, location=knee)
    knee_obj = bpy.context.active_object
    knee_obj.name = f"{name}_Knee"
    knee_obj.data.materials.append(mat_leg)
    bpy.ops.object.shade_smooth()
    reg(knee_obj)

    # 4. Tibia
    t_mid = [(knee[i] + ankle[i]) * 0.5 for i in range(3)]
    t_vec = Vector((ankle[0] - knee[0], ankle[1] - knee[1], ankle[2] - knee[2]))
    bpy.ops.mesh.primitive_cylinder_add(radius=0.045, depth=t_vec.length, location=t_mid)
    tibia = bpy.context.active_object
    tibia.name = f"{name}_Tibia"
    tibia.rotation_mode = 'QUATERNION'
    tibia.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(t_vec.normalized())
    tibia.data.materials.append(mat_leg)
    bpy.ops.object.shade_smooth()
    reg(tibia)

    # 5. Tarsus Foot
    foot_mid = [(ankle[i] + foot[i]) * 0.5 for i in range(3)]
    foot_vec = Vector((foot[0] - ankle[0], foot[1] - ankle[1], foot[2] - ankle[2]))
    bpy.ops.mesh.primitive_cylinder_add(radius=0.035, depth=foot_vec.length, location=foot_mid)
    tarsus = bpy.context.active_object
    tarsus.name = f"{name}_Tarsus"
    tarsus.rotation_mode = 'QUATERNION'
    tarsus.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(foot_vec.normalized())
    tarsus.data.materials.append(mat_leg)
    bpy.ops.object.shade_smooth()
    reg(tarsus)

# Left Front Leg (forward supporting stance)
make_3d_leg("Front_L",
            hip=(-0.24, -0.05, 0.78),
            knee=(-0.66, -0.28, 0.95),
            ankle=(-0.72, -0.48, 0.38),
            foot=(-0.58, -0.62, 0.0))

# Right Front Leg
make_3d_leg("Front_R",
            hip=(0.24, -0.05, 0.78),
            knee=(0.66, -0.28, 0.95),
            ankle=(0.72, -0.48, 0.38),
            foot=(0.58, -0.62, 0.0))

# Left Middle Leg
make_3d_leg("Mid_L",
            hip=(-0.28, 0.22, 0.75),
            knee=(-0.86, 0.15, 0.98),
            ankle=(-0.92, 0.05, 0.40),
            foot=(-0.78, -0.10, 0.0))

# Right Middle Leg
make_3d_leg("Mid_R",
            hip=(0.28, 0.22, 0.75),
            knee=(0.86, 0.15, 0.98),
            ankle=(0.92, 0.05, 0.40),
            foot=(0.78, -0.10, 0.0))

# Left Hind Leg
make_3d_leg("Hind_L",
            hip=(-0.25, 0.48, 0.70),
            knee=(-0.82, 0.75, 1.05),
            ankle=(-0.88, 0.92, 0.42),
            foot=(-0.75, 1.08, 0.0))

# Right Hind Leg
make_3d_leg("Hind_R",
            hip=(0.25, 0.48, 0.70),
            knee=(0.82, 0.75, 1.05),
            ankle=(0.90, 0.92, 0.42),
            foot=(0.75, 1.08, 0.0))

# -----------------------------------------------------------------------------
# 9. Studio Pedestal & Lighting
# -----------------------------------------------------------------------------
bpy.ops.mesh.primitive_cylinder_add(radius=2.0, depth=0.1, location=(0, 0, -0.05))
pedestal = bpy.context.active_object
pedestal.name = "Pedestal"
ped_mat = bpy.data.materials.new("M_Pedestal")
ped_mat.use_nodes = True
p_bsdf = ped_mat.node_tree.nodes.get("Principled BSDF")
p_bsdf.inputs['Base Color'].default_value = (0.06, 0.08, 0.10, 1.0)
p_bsdf.inputs['Roughness'].default_value = 0.75
pedestal.data.materials.append(ped_mat)

# Key Light (Front-Left, warm)
key_data = bpy.data.lights.new("Studio_Key", 'AREA')
key_data.energy = 180.0
key_data.size = 2.0
key_data.color = (1.0, 0.96, 0.90)
key_obj = bpy.data.objects.new("Studio_Key", key_data)
key_obj.location = (-2.5, -2.8, 2.5)
key_obj.rotation_euler = Euler((math.radians(45), 0, math.radians(-40)), 'XYZ')
bpy.context.scene.collection.objects.link(key_obj)

# Fill Light (Front-Right, cool)
fill_data = bpy.data.lights.new("Studio_Fill", 'AREA')
fill_data.energy = 75.0
fill_data.size = 2.5
fill_data.color = (0.85, 0.92, 1.0)
fill_obj = bpy.data.objects.new("Studio_Fill", fill_data)
fill_obj.location = (2.5, -2.5, 2.0)
fill_obj.rotation_euler = Euler((math.radians(40), 0, math.radians(40)), 'XYZ')
bpy.context.scene.collection.objects.link(fill_obj)

# Rim Light (Back-Top)
rim_data = bpy.data.lights.new("Studio_Rim", 'SPOT')
rim_data.energy = 240.0
rim_data.spot_size = math.radians(65)
rim_obj = bpy.data.objects.new("Studio_Rim", rim_data)
rim_obj.location = (0.0, 2.8, 3.2)
rim_obj.rotation_euler = Euler((math.radians(-45), 0, 0), 'XYZ')
bpy.context.scene.collection.objects.link(rim_obj)

# -----------------------------------------------------------------------------
# 10. Camera & Beauty Renders
# -----------------------------------------------------------------------------
cam_data = bpy.data.cameras.new("Worker_Cam")
cam_data.lens = 65.0
cam_obj = bpy.data.objects.new("Worker_Cam", cam_data)
bpy.context.scene.collection.objects.link(cam_obj)
scene.camera = cam_obj

views = [
    {
        "name": "worker_front.png",
        "loc": (0.0, -3.4, 1.25),
        "rot": (math.radians(85), 0, 0),
        "lens": 65.0
    },
    {
        "name": "worker_perspective.png",
        "loc": (-2.4, -2.6, 1.8),
        "rot": (math.radians(70), 0, math.radians(-42)),
        "lens": 55.0
    },
    {
        "name": "worker_face_closeup.png",
        "loc": (0.0, -1.6, 1.48),
        "rot": (math.radians(87), 0, 0),
        "lens": 85.0
    }
]

for v in views:
    cam_obj.location = Vector(v["loc"])
    cam_obj.rotation_euler = Euler(v["rot"], 'XYZ')
    cam_data.lens = v["lens"]
    scene.render.filepath = f"{web_dir}/{v['name']}"
    print(f"Rendering Cycles shot: {v['name']}...")
    bpy.ops.render.render(write_still=True)
    print(f"Saved: {web_dir}/{v['name']}")

# -----------------------------------------------------------------------------
# 11. Export True 3D Polygonal GLB
# -----------------------------------------------------------------------------
bpy.ops.object.select_all(action='DESELECT')
for obj in worker_col.objects:
    obj.select_set(True)

glb_path = f"{web_dir}/worker_ant.glb"
print(f"Exporting Worker Ant 3D GLB to: {glb_path}...")
bpy.ops.export_scene.gltf(
    filepath=glb_path,
    use_selection=True,
    export_format='GLB',
    export_apply=True
)
print("GLB export complete!")

blend_path = "/Users/dchadd/Desktop/Ants-Mac/tools/blender/worker_ant_clean3d.blend"
bpy.ops.wm.save_as_mainfile(filepath=blend_path)
print(f"Saved Blender file: {blend_path}")
