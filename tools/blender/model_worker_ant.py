"""
Blender 4.3.2 Python Script: 1:1 Model of Worker Ant (Ant #1)
Models the Worker Ant individually with organic quad topology,
authentic caricature anatomy, procedural PBR chitin, multi-layered eyes,
mandibles, jointed insect legs, studio lighting, and exports GLB + renders multi-angle stills.
"""

import bpy
import bmesh
import math
from mathutils import Vector, Matrix, Euler

# -----------------------------------------------------------------------------
# 1. Clear existing scene
# -----------------------------------------------------------------------------
bpy.ops.wm.read_factory_settings(use_empty=True)

scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'GPU'

# Prefer Metal GPU if available on Apple Silicon
preferences = bpy.context.preferences
cycles_prefs = preferences.addons['cycles'].preferences
cycles_prefs.compute_device_type = 'METAL'
cycles_prefs.get_devices()
for d in cycles_prefs.devices:
    d.use = True
    print(f"Enabled compute device: {d.name} ({d.type})")

scene.cycles.samples = 128
scene.cycles.use_denoising = True
scene.render.film_transparent = True
scene.render.image_settings.file_format = 'PNG'
scene.render.image_settings.color_mode = 'RGBA'
scene.render.resolution_x = 1200
scene.render.resolution_y = 1200
scene.render.resolution_percentage = 100

# -----------------------------------------------------------------------------
# 2. Material Helpers
# -----------------------------------------------------------------------------
def create_chitin_material():
    mat = bpy.data.materials.new(name="Worker_Chitin")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    # Output node
    output = nodes.new(type='ShaderNodeOutputMaterial')
    output.location = (800, 0)

    # Principled BSDF
    bsdf = nodes.new(type='ShaderNodeBsdfPrincipled')
    bsdf.location = (500, 0)
    links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

    # Base Color Gradient & Noise
    tex_coord = nodes.new(type='ShaderNodeTexCoord')
    tex_coord.location = (-600, 0)

    noise = nodes.new(type='ShaderNodeTexNoise')
    noise.location = (-400, 100)
    noise.inputs['Scale'].default_value = 18.0
    noise.inputs['Detail'].default_value = 4.0
    noise.inputs['Roughness'].default_value = 0.55
    links.new(tex_coord.outputs['Object'], noise.inputs['Vector'])

    color_ramp = nodes.new(type='ShaderNodeValToRGB')
    color_ramp.location = (-150, 100)
    # Rich mottled emerald green to deep forest green
    color_ramp.color_ramp.elements[0].position = 0.25
    color_ramp.color_ramp.elements[0].color = (0.08, 0.22, 0.09, 1.0) # Deep forest
    color_ramp.color_ramp.elements[1].position = 0.75
    color_ramp.color_ramp.elements[1].color = (0.24, 0.44, 0.22, 1.0) # Vibrant emerald
    links.new(noise.outputs['Fac'], color_ramp.inputs['Fac'])
    links.new(color_ramp.outputs['Color'], bsdf.inputs['Base Color'])

    # Roughness & Clearcoat
    bsdf.inputs['Roughness'].default_value = 0.32
    bsdf.inputs['Coat Weight'].default_value = 0.75
    bsdf.inputs['Coat Roughness'].default_value = 0.15

    # Micro bump texture for organic insect cuticle
    bump_noise = nodes.new(type='ShaderNodeTexNoise')
    bump_noise.location = (-150, -150)
    bump_noise.inputs['Scale'].default_value = 45.0
    bump_noise.inputs['Detail'].default_value = 3.0
    links.new(tex_coord.outputs['Object'], bump_noise.inputs['Vector'])

    bump = nodes.new(type='ShaderNodeBump')
    bump.location = (150, -150)
    bump.inputs['Strength'].default_value = 0.03
    bump.inputs['Distance'].default_value = 0.05
    links.new(bump_noise.outputs['Fac'], bump.inputs['Height'])
    links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])

    return mat

def create_leg_material():
    mat = bpy.data.materials.new(name="Worker_Leg_Chitin")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    output = nodes.new(type='ShaderNodeOutputMaterial')
    output.location = (400, 0)
    bsdf = nodes.new(type='ShaderNodeBsdfPrincipled')
    bsdf.location = (150, 0)
    links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

    # Dark reddish-brown / mahogany insect leg chitin
    bsdf.inputs['Base Color'].default_value = (0.16, 0.07, 0.05, 1.0)
    bsdf.inputs['Roughness'].default_value = 0.40
    bsdf.inputs['Coat Weight'].default_value = 0.50
    bsdf.inputs['Coat Roughness'].default_value = 0.25

    return mat

def create_mandible_material():
    mat = bpy.data.materials.new(name="Worker_Mandibles")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    output = nodes.new(type='ShaderNodeOutputMaterial')
    output.location = (600, 0)
    bsdf = nodes.new(type='ShaderNodeBsdfPrincipled')
    bsdf.location = (300, 0)
    links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

    tex_coord = nodes.new(type='ShaderNodeTexCoord')
    tex_coord.location = (-400, 0)

    # Gradient from rich green at base to pale lime/white at the tips
    ramp = nodes.new(type='ShaderNodeValToRGB')
    ramp.location = (-150, 0)
    ramp.color_ramp.elements[0].position = 0.2
    ramp.color_ramp.elements[0].color = (0.15, 0.35, 0.16, 1.0) # Base green
    ramp.color_ramp.elements[1].position = 0.8
    ramp.color_ramp.elements[1].color = (0.65, 0.82, 0.55, 1.0) # Pale lime-white tips
    links.new(tex_coord.outputs['Object'], ramp.inputs['Fac'])
    links.new(ramp.outputs['Color'], bsdf.inputs['Base Color'])

    bsdf.inputs['Roughness'].default_value = 0.26
    bsdf.inputs['Coat Weight'].default_value = 0.85
    bsdf.inputs['Coat Roughness'].default_value = 0.12

    return mat

def create_eye_material(side="Left"):
    mat = bpy.data.materials.new(name=f"Worker_Eye_{side}")
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    output = nodes.new(type='ShaderNodeOutputMaterial')
    output.location = (800, 0)
    bsdf = nodes.new(type='ShaderNodeBsdfPrincipled')
    bsdf.location = (500, 0)
    links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])

    # Coordinates centered on pupil
    tex_coord = nodes.new(type='ShaderNodeTexCoord')
    tex_coord.location = (-600, 0)

    mapping = nodes.new(type='ShaderNodeMapping')
    mapping.location = (-400, 0)
    links.new(tex_coord.outputs['Object'], mapping.inputs['Vector'])

    # Radial gradient for pupil + iris + sclera
    gradient = nodes.new(type='ShaderNodeTexGradient')
    gradient.gradient_type = 'SPHERICAL'
    gradient.location = (-180, 0)
    links.new(mapping.outputs['Vector'], gradient.inputs['Vector'])

    ramp = nodes.new(type='ShaderNodeValToRGB')
    ramp.location = (100, 0)
    # 0.0 - 0.25: Deep Glossy Black Pupil
    # 0.25 - 0.50: Olive Green / Amber Iris
    # 0.50 - 0.60: Dark Limbal Ring
    # 0.60 - 1.0: Warm Cream Off-White Sclera
    ramp.color_ramp.interpolation = 'LINEAR'
    ramp.color_ramp.elements[0].position = 0.22
    ramp.color_ramp.elements[0].color = (0.015, 0.02, 0.015, 1.0) # Pupil

    elem1 = ramp.color_ramp.elements.new(0.35)
    elem1.color = (0.28, 0.38, 0.15, 1.0) # Olive Amber Iris

    elem2 = ramp.color_ramp.elements.new(0.50)
    elem2.color = (0.08, 0.12, 0.06, 1.0) # Limbal ring

    ramp.color_ramp.elements[len(ramp.color_ramp.elements)-1].position = 0.62
    ramp.color_ramp.elements[len(ramp.color_ramp.elements)-1].color = (0.92, 0.94, 0.88, 1.0) # Cream sclera

    links.new(gradient.outputs['Fac'], ramp.inputs['Fac'])
    links.new(ramp.outputs['Color'], bsdf.inputs['Base Color'])

    # Glassy glossy cornea reflections
    bsdf.inputs['Roughness'].default_value = 0.05
    bsdf.inputs['Coat Weight'].default_value = 1.0
    bsdf.inputs['Coat Roughness'].default_value = 0.02
    bsdf.inputs['IOR'].default_value = 1.45

    return mat

chitin_mat = create_chitin_material()
leg_mat = create_leg_material()
mandible_mat = create_mandible_material()
eye_left_mat = create_eye_material("Left")
eye_right_mat = create_eye_material("Right")

# -----------------------------------------------------------------------------
# 3. Model Worker Ant Geometry
# -----------------------------------------------------------------------------
# Base Ant Collection
ant_col = bpy.data.collections.new("Worker_Ant")
bpy.context.scene.collection.children.link(ant_col)

# --- A. Head (Expressive Bean / Heart Shape with Snout) ---
# Start from subdivided cube for quad topology
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, 1.5))
head_obj = bpy.context.active_object
head_obj.name = "Worker_Head"
head_obj.data.materials.append(chitin_mat)

# Deform cube vertices into organic ant head shape
bm = bmesh.new()
bm.from_mesh(head_obj.data)

for v in bm.verts:
    x, y, z = v.co.x, v.co.y, v.co.z
    # Scale width at top, narrow at bottom (snout)
    if z < 0:
        # Narrow snout towards mandibles
        width_factor = 0.75 + z * 0.25
        v.co.x *= width_factor
        # Project snout forward
        v.co.y += (z * -0.2)
    else:
        # Bulge cranium and temples
        v.co.x *= 1.25
        v.co.z *= 1.15
        # Round forehead
        v.co.y -= (z * 0.15)
    # Flatten depth slightly for bean profile
    v.co.y *= 0.88

bm.to_mesh(head_obj.data)
bm.free()

# Add Subdivision Surface
subsurf = head_obj.modifiers.new(name="Subsurf", type='SUBSURF')
subsurf.levels = 3
subsurf.render_levels = 3
bpy.ops.object.shade_smooth()

# Move head to collection
ant_col.objects.link(head_obj)
bpy.context.scene.collection.objects.unlink(head_obj)

# --- B. Innocent Big Eyes ---
# Left Eye
bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=24, radius=0.36, location=(-0.38, 0.30, 1.62))
eye_l = bpy.context.active_object
eye_l.name = "Worker_Eye_L"
eye_l.rotation_euler = Euler((math.radians(10), math.radians(-15), math.radians(12)), 'XYZ')
eye_l.scale = Vector((0.95, 0.95, 1.15))
eye_l.data.materials.append(eye_left_mat)
bpy.ops.object.shade_smooth()
ant_col.objects.link(eye_l)
bpy.context.scene.collection.objects.unlink(eye_l)

# Right Eye
bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=24, radius=0.36, location=(0.38, 0.30, 1.62))
eye_r = bpy.context.active_object
eye_r.name = "Worker_Eye_R"
eye_r.rotation_euler = Euler((math.radians(10), math.radians(15), math.radians(-12)), 'XYZ')
eye_r.scale = Vector((0.95, 0.95, 1.15))
eye_r.data.materials.append(eye_right_mat)
bpy.ops.object.shade_smooth()
ant_col.objects.link(eye_r)
bpy.context.scene.collection.objects.unlink(eye_r)

# --- C. Plump Clasping Mandibles ---
def create_mandible(is_left=True):
    sign = -1.0 if is_left else 1.0
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=18, radius=0.22, location=(sign * 0.22, 0.35, 1.08))
    mand = bpy.context.active_object
    mand.name = f"Worker_Mandible_{'L' if is_left else 'R'}"
    mand.data.materials.append(mandible_mat)

    # Deform into clasping curved pincer
    bm = bmesh.new()
    bm.from_mesh(mand.data)
    for v in bm.verts:
        # Curve inward towards midline
        v.co.x += (v.co.z - 1.08) * sign * 0.4
        # Flatten back
        if v.co.y < 0.35:
            v.co.y *= 0.7
        # Point tip inward
        if v.co.z < 1.05:
            v.co.x -= sign * 0.12
            v.co.y += 0.08
    bm.to_mesh(mand.data)
    bm.free()

    # Smooth & Subsurf
    sub = mand.modifiers.new(name="Subsurf", type='SUBSURF')
    sub.levels = 2
    bpy.ops.object.shade_smooth()

    ant_col.objects.link(mand)
    bpy.context.scene.collection.objects.unlink(mand)
    return mand

mand_l = create_mandible(True)
mand_r = create_mandible(False)

# --- D. Curved Antennae ---
def create_antenna(is_left=True):
    sign = -1.0 if is_left else 1.0
    curve_data = bpy.data.curves.new(name=f"AntennaCurve_{'L' if is_left else 'R'}", type='CURVE')
    curve_data.dimensions = '3D'
    curve_data.bevel_depth = 0.032
    curve_data.bevel_resolution = 6

    polyline = curve_data.splines.new('BEZIER')
    polyline.bezier_points.add(2) # Total 3 points

    # Base at forehead socket
    p0 = polyline.bezier_points[0]
    p0.co = Vector((sign * 0.16, 0.25, 2.05))
    p0.handle_right = Vector((sign * 0.22, 0.28, 2.25))

    # Mid curve arching upward & outward
    p1 = polyline.bezier_points[1]
    p1.co = Vector((sign * 0.38, 0.28, 2.45))
    p1.handle_left = Vector((sign * 0.30, 0.28, 2.35))
    p1.handle_right = Vector((sign * 0.48, 0.26, 2.55))

    # Tip sweeping forward
    p2 = polyline.bezier_points[2]
    p2.co = Vector((sign * 0.52, 0.35, 2.62))
    p2.handle_left = Vector((sign * 0.50, 0.32, 2.58))

    curve_obj = bpy.data.objects.new(f"Worker_Antenna_{'L' if is_left else 'R'}", curve_data)
    curve_obj.data.materials.append(leg_mat)
    ant_col.objects.link(curve_obj)
    return curve_obj

ant_l = create_antenna(True)
ant_r = create_antenna(False)

# --- E. Thorax (Mesosoma) ---
bpy.ops.mesh.primitive_uv_sphere_add(segments=28, ring_count=20, radius=0.48, location=(0, -0.22, 0.82))
thorax = bpy.context.active_object
thorax.name = "Worker_Thorax"
thorax.scale = Vector((0.78, 1.15, 0.85))
thorax.rotation_euler = Euler((math.radians(25), 0, 0), 'XYZ')
thorax.data.materials.append(chitin_mat)
sub_th = thorax.modifiers.new(name="Subsurf", type='SUBSURF')
sub_th.levels = 2
bpy.ops.object.shade_smooth()
ant_col.objects.link(thorax)
bpy.context.scene.collection.objects.unlink(thorax)

# --- F. Petiole (Waist Node) ---
bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=12, radius=0.18, location=(0, -0.68, 0.58))
petiole = bpy.context.active_object
petiole.name = "Worker_Petiole"
petiole.scale = Vector((0.75, 1.1, 0.85))
petiole.data.materials.append(chitin_mat)
bpy.ops.object.shade_smooth()
ant_col.objects.link(petiole)
bpy.context.scene.collection.objects.unlink(petiole)

# --- G. Gaster (Abdomen) ---
bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=24, radius=0.72, location=(0, -1.25, 0.42))
gaster = bpy.context.active_object
gaster.name = "Worker_Gaster"
gaster.scale = Vector((0.82, 1.35, 0.95))
gaster.rotation_euler = Euler((math.radians(-20), 0, 0), 'XYZ')
gaster.data.materials.append(chitin_mat)
sub_g = gaster.modifiers.new(name="Subsurf", type='SUBSURF')
sub_g.levels = 2
bpy.ops.object.shade_smooth()
ant_col.objects.link(gaster)
bpy.context.scene.collection.objects.unlink(gaster)

# --- H. Articulated Jointed Legs (3 Pairs) ---
def create_jointed_leg(name, hip_pos, knee_pos, ankle_pos, foot_pos):
    curve_data = bpy.data.curves.new(name=f"{name}_Curve", type='CURVE')
    curve_data.dimensions = '3D'
    curve_data.bevel_depth = 0.048
    curve_data.bevel_resolution = 6

    polyline = curve_data.splines.new('POLY')
    polyline.points.add(3) # Total 4 points: Hip -> Knee -> Ankle -> Foot

    polyline.points[0].co = (hip_pos[0], hip_pos[1], hip_pos[2], 1.0)
    polyline.points[1].co = (knee_pos[0], knee_pos[1], knee_pos[2], 1.0)
    polyline.points[2].co = (ankle_pos[0], ankle_pos[1], ankle_pos[2], 1.0)
    polyline.points[3].co = (foot_pos[0], foot_pos[1], foot_pos[2], 1.0)

    leg_obj = bpy.data.objects.new(name, curve_data)
    leg_obj.data.materials.append(leg_mat)
    ant_col.objects.link(leg_obj)
    return leg_obj

# Left Front Leg (raised/posed forward)
create_jointed_leg("Leg_Front_L",
                   hip_pos=(-0.25, 0.0, 0.75),
                   knee_pos=(-0.65, 0.25, 0.95),
                   ankle_pos=(-0.72, 0.45, 0.40),
                   foot_pos=(-0.58, 0.58, 0.0))

# Right Front Leg (supporting stance)
create_jointed_leg("Leg_Front_R",
                   hip_pos=(0.25, 0.0, 0.75),
                   knee_pos=(0.68, 0.22, 0.92),
                   ankle_pos=(0.74, 0.42, 0.38),
                   foot_pos=(0.60, 0.55, 0.0))

# Left Middle Leg
create_jointed_leg("Leg_Mid_L",
                   hip_pos=(-0.28, -0.25, 0.72),
                   knee_pos=(-0.85, -0.15, 0.98),
                   ankle_pos=(-0.92, -0.05, 0.42),
                   foot_pos=(-0.78, 0.10, 0.0))

# Right Middle Leg
create_jointed_leg("Leg_Mid_R",
                   hip_pos=(0.28, -0.25, 0.72),
                   knee_pos=(0.85, -0.15, 0.98),
                   ankle_pos=(0.92, -0.05, 0.42),
                   foot_pos=(0.78, 0.10, 0.0))

# Left Hind Leg
create_jointed_leg("Leg_Hind_L",
                   hip_pos=(-0.25, -0.50, 0.68),
                   knee_pos=(-0.82, -0.75, 1.05),
                   ankle_pos=(-0.88, -0.90, 0.45),
                   foot_pos=(-0.75, -1.05, 0.0))

# Right Hind Leg
create_jointed_leg("Leg_Hind_R",
                   hip_pos=(0.25, -0.50, 0.68),
                   knee_pos=(0.82, -0.75, 1.05),
                   ankle_pos=(0.88, -0.90, 0.45),
                   foot_pos=(0.75, -1.05, 0.0))

# -----------------------------------------------------------------------------
# 4. Multi-Point Studio Lighting Rig
# -----------------------------------------------------------------------------
# Warm Key Light (top-left front)
key_light_data = bpy.data.lights.new(name="Studio_Key", type='AREA')
key_light_data.energy = 160.0
key_light_data.size = 1.8
key_light_data.color = (1.0, 0.96, 0.90)
key_light = bpy.data.objects.new("Studio_Key", key_light_data)
key_light.location = (-2.2, 2.5, 3.2)
key_light.rotation_euler = Euler((math.radians(38), math.radians(-25), math.radians(-30)), 'XYZ')
bpy.context.scene.collection.objects.link(key_light)

# Cool Fill Light (front-right)
fill_light_data = bpy.data.lights.new(name="Studio_Fill", type='AREA')
fill_light_data.energy = 65.0
fill_light_data.size = 2.4
fill_light_data.color = (0.85, 0.92, 1.0)
fill_light = bpy.data.objects.new("Studio_Fill", fill_light_data)
fill_light.location = (2.4, 2.0, 2.2)
fill_light.rotation_euler = Euler((math.radians(35), math.radians(30), math.radians(25)), 'XYZ')
bpy.context.scene.collection.objects.link(fill_light)

# Sharp Rim Light (top-rear backlight for specular silhouette & catchlights)
rim_light_data = bpy.data.lights.new(name="Studio_Rim", type='SPOT')
rim_light_data.energy = 240.0
rim_light_data.spot_size = math.radians(65)
rim_light_data.spot_blend = 0.3
rim_light_data.color = (1.0, 1.0, 1.0)
rim_light = bpy.data.objects.new("Studio_Rim", rim_light_data)
rim_light.location = (0.0, -3.2, 3.6)
rim_light.rotation_euler = Euler((math.radians(-50), 0, 0), 'XYZ')
bpy.context.scene.collection.objects.link(rim_light)

# Soft Ground Bounce / Fill
bounce_data = bpy.data.lights.new(name="Studio_Bounce", type='POINT')
bounce_data.energy = 25.0
bounce_data.color = (0.9, 0.8, 0.7)
bounce = bpy.data.objects.new("Studio_Bounce", bounce_data)
bounce.location = (0.0, 1.2, -0.5)
bpy.context.scene.collection.objects.link(bounce)

# -----------------------------------------------------------------------------
# 5. Camera & Multi-Angle Renders
# -----------------------------------------------------------------------------
cam_data = bpy.data.cameras.new("Worker_Camera")
cam_data.lens = 65.0 # Flattering portrait focal length
cam_obj = bpy.data.objects.new("Worker_Camera", cam_data)
bpy.context.scene.collection.objects.link(cam_obj)
scene.camera = cam_obj

# Camera Views to render
render_views = [
    {
        "name": "worker_front.png",
        "loc": (0.0, 3.2, 1.55),
        "rot": (math.radians(88), 0, math.radians(180)),
        "lens": 65.0
    },
    {
        "name": "worker_perspective.png",
        "loc": (-2.1, 2.6, 2.1),
        "rot": (math.radians(68), 0, math.radians(-142)),
        "lens": 55.0
    },
    {
        "name": "worker_face_closeup.png",
        "loc": (0.0, 1.6, 1.55),
        "rot": (math.radians(88), 0, math.radians(180)),
        "lens": 85.0
    },
    {
        "name": "worker_side.png",
        "loc": (-3.4, 0.0, 1.3),
        "rot": (math.radians(88), 0, math.radians(-90)),
        "lens": 60.0
    }
]

out_dir = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d"

for v in render_views:
    cam_obj.location = Vector(v["loc"])
    cam_obj.rotation_euler = Euler(v["rot"], 'XYZ')
    cam_data.lens = v["lens"]
    scene.render.filepath = f"{out_dir}/{v['name']}"
    print(f"Rendering Cycles beauty shot: {v['name']}...")
    bpy.ops.render.render(write_still=True)
    print(f"Saved: {out_dir}/{v['name']}")

# -----------------------------------------------------------------------------
# 6. Export GLB Model for Interactive Web Inspector
# -----------------------------------------------------------------------------
# Select only the ant collection objects
bpy.ops.object.select_all(action='DESELECT')
for obj in ant_col.objects:
    obj.select_set(True)

glb_path = f"{out_dir}/worker_ant.glb"
print(f"Exporting worker ant GLB to {glb_path}...")
bpy.ops.export_scene.gltf(
    filepath=glb_path,
    use_selection=True,
    export_format='GLB',
    export_apply=True
)
print("GLB export complete!")

# Save Blender source file for iteration
blend_path = "/Users/dchadd/Desktop/Ants-Mac/tools/blender/worker_ant.blend"
bpy.ops.wm.save_as_mainfile(filepath=blend_path)
print(f"Saved Blender project to: {blend_path}")
