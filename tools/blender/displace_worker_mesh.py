"""
Blender 4.3.2 Python Script: True 3D Volumetric Polygonal Mesh of Worker Ant
Displaces a high-density quad grid with the Z-depth map, trims the background void,
bakes into real 3D polygonal geometry, sets up PBR material, and exports worker_ant.glb.
"""

import bpy
import bmesh
import math
from mathutils import Vector, Euler

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

diffuse_path = "/Users/dchadd/.gemini/antigravity/brain/213ca2fe-102b-45e6-99a7-00e17877c107/worker_ant_master_1790372115097.jpg"
depth_path = "/Users/dchadd/.gemini/antigravity/brain/213ca2fe-102b-45e6-99a7-00e17877c107/worker_ant_depth_map_1790373235932.jpg"
out_dir = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d"

# 1. Create Grid in XY Plane
bpy.ops.mesh.primitive_grid_add(x_subdivisions=240, y_subdivisions=240, size=2.4, location=(0, 0, 0))
ant_mesh = bpy.context.active_object
ant_mesh.name = "Worker_Ant_3D"

# 2. Depth Displacement along Z
depth_img = bpy.data.images.load(depth_path)
depth_tex = bpy.data.textures.new("Worker_Depth_Texture", type='IMAGE')
depth_tex.image = depth_img

displace_mod = ant_mesh.modifiers.new("Displace", 'DISPLACE')
displace_mod.texture = depth_tex
displace_mod.texture_coords = 'UV'
displace_mod.strength = 0.55
displace_mod.mid_level = 0.0
bpy.ops.object.modifier_apply(modifier="Displace")

# 3. Filter Background Vertices
bm = bmesh.new()
bm.from_mesh(ant_mesh.data)

verts_to_delete = [v for v in bm.verts if v.co.z < 0.025]
bmesh.ops.delete(bm, geom=verts_to_delete, context='VERTS')
print(f"Remaining 3D Ant Vertices: {len(bm.verts)}")

# Smooth boundary vertices slightly
bmesh.ops.smooth_vert(bm, verts=bm.verts, factor=0.25)

bm.to_mesh(ant_mesh.data)
bm.free()

# 4. Give Volumetric Backside Thickness
solid_mod = ant_mesh.modifiers.new("Solidify", 'SOLIDIFY')
solid_mod.thickness = 0.06
solid_mod.offset = -1.0 # Extrude backward
bpy.ops.object.modifier_apply(modifier="Solidify")

# 5. Rotate Upright (Facing -Y towards camera)
ant_mesh.rotation_euler = Euler((math.radians(90), 0, 0), 'XYZ')
bpy.ops.object.transform_apply(rotation=True)

# Center ant vertically so feet touch ground at Z=0
bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='BOUNDS')
# Shift so lowest vertex is at Z=0
min_z = min(v.co.z for v in ant_mesh.data.vertices)
ant_mesh.location.z = -min_z
bpy.ops.object.transform_apply(location=True)

bpy.ops.object.shade_smooth()

# 6. PBR Material Setup
mat = bpy.data.materials.new("M_Worker_Ant_3D")
mat.use_nodes = True
nodes = mat.node_tree.nodes
links = mat.node_tree.links
nodes.clear()

out_node = nodes.new('ShaderNodeOutputMaterial')
bsdf_node = nodes.new('ShaderNodeBsdfPrincipled')
links.new(bsdf_node.outputs['BSDF'], out_node.inputs['Surface'])

diff_img = bpy.data.images.load(diffuse_path)
diff_tex = nodes.new('ShaderNodeTexImage')
diff_tex.image = diff_img
links.new(diff_tex.outputs['Color'], bsdf_node.inputs['Base Color'])

bsdf_node.inputs['Roughness'].default_value = 0.32
bsdf_node.inputs['Coat Weight'].default_value = 0.75
bsdf_node.inputs['Coat Roughness'].default_value = 0.14

ant_mesh.data.materials.append(mat)

# 7. Studio Pedestal & Lighting
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

# 8. Camera & Beauty Renders
cam_data = bpy.data.cameras.new("Worker_Cam")
cam_data.lens = 65.0
cam_obj = bpy.data.objects.new("Worker_Cam", cam_data)
bpy.context.scene.collection.objects.link(cam_obj)
scene.camera = cam_obj

views = [
    {
        "name": "worker_front.png",
        "loc": (0.0, -3.6, 1.15),
        "rot": (math.radians(88), 0, 0),
        "lens": 65.0
    },
    {
        "name": "worker_perspective.png",
        "loc": (-2.4, -2.8, 1.6),
        "rot": (math.radians(72), 0, math.radians(-40)),
        "lens": 55.0
    },
    {
        "name": "worker_face_closeup.png",
        "loc": (0.0, -1.8, 1.55),
        "rot": (math.radians(88), 0, 0),
        "lens": 85.0
    }
]

for v in views:
    cam_obj.location = Vector(v["loc"])
    cam_obj.rotation_euler = Euler(v["rot"], 'XYZ')
    cam_data.lens = v["lens"]
    scene.render.filepath = f"{out_dir}/{v['name']}"
    print(f"Rendering Cycles shot: {v['name']}...")
    bpy.ops.render.render(write_still=True)
    print(f"Saved: {out_dir}/{v['name']}")

# 9. Export True 3D GLB for Three.js
bpy.ops.object.select_all(action='DESELECT')
ant_mesh.select_set(True)

glb_path = f"{out_dir}/worker_ant.glb"
print(f"Exporting real 3D polygonal GLB to {glb_path}...")
bpy.ops.export_scene.gltf(
    filepath=glb_path,
    use_selection=True,
    export_format='GLB',
    export_apply=True
)
print("GLB export complete!")

blend_path = "/Users/dchadd/Desktop/Ants-Mac/tools/blender/worker_ant_volumetric.blend"
bpy.ops.wm.save_as_mainfile(filepath=blend_path)
print(f"Saved Blender file: {blend_path}")
