"""
Test 3D Solid Mesh Reconstruction from Master Render + AI Depth Map.
Builds a watertight, solid 3D polygonal volume with:
- Front surface displaced by depth
- Back surface smoothly capped
- Clean border quads sealing the perimeter
- UV mapping matching the photorealistic master render
"""

import bpy
import bmesh
import numpy as np
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

scene.cycles.samples = 64
scene.cycles.use_denoising = True
scene.render.film_transparent = True
scene.render.resolution_x = 1080
scene.render.resolution_y = 1080

# 1. Load depth map and master render
depth_path = '/Users/dchadd/.gemini/antigravity/brain/213ca2fe-102b-45e6-99a7-00e17877c107/worker_ant_depth_map_1790373235932.jpg'
master_path = '/Users/dchadd/.gemini/antigravity/brain/213ca2fe-102b-45e6-99a7-00e17877c107/worker_ant_master_1790372115097.jpg'

depth_img = bpy.data.images.load(depth_path)
w, h = depth_img.size
pixels = np.array(depth_img.pixels[:], dtype=np.float32).reshape((h, w, 4))
gray = pixels[:, :, 0] # (H, W)

# 2. Downsample with box blur smoothing
grid_size = 200
y_idx = np.linspace(0, h - 1, grid_size).astype(int)
x_idx = np.linspace(0, w - 1, grid_size).astype(int)
depth_raw = gray[np.ix_(y_idx, x_idx)]

# Smooth 3x3 filter
depth_smooth = np.zeros_like(depth_raw)
for dy in [-1, 0, 1]:
    for dx in [-1, 0, 1]:
        depth_smooth += np.roll(np.roll(depth_raw, dy, axis=0), dx, axis=1)
depth_smooth /= 9.0

# Mask threshold
mask = depth_smooth > 0.06

# Only keep cells where all 4 corners are inside ant silhouette
cell_mask = mask[:-1, :-1] & mask[1:, :-1] & mask[:-1, 1:] & mask[1:, 1:]

# 3. Build solid 3D mesh in BMesh
bm = bmesh.new()

# Pre-create vertices grid for front and back
front_verts = {}
back_verts = {}

# Ant scale dimensions
aspect = 1.0 # square image
scale_x = 2.4
scale_z = 2.4
depth_scale = 0.75 # 3D thickness

for j in range(grid_size):
    for i in range(grid_size):
        if mask[j, i]:
            # X from -scale_x/2 to +scale_x/2
            x = (i / (grid_size - 1.0) - 0.5) * scale_x
            # Z from 0 to scale_z
            z = (j / (grid_size - 1.0)) * scale_z
            
            d = depth_smooth[j, i]
            # Front surface projects forward (-Y)
            y_front = -d * depth_scale
            # Back surface projects back (+Y)
            y_back = +d * depth_scale * 0.45
            
            vf = bm.verts.new(Vector((x, y_front, z)))
            vb = bm.verts.new(Vector((x, y_back, z)))
            front_verts[(j, i)] = vf
            back_verts[(j, i)] = vb

# Add UV layer
uv_layer = bm.loops.layers.uv.new("UVMap")

# Create Front and Back quad faces
for j in range(grid_size - 1):
    for i in range(grid_size - 1):
        if cell_mask[j, i]:
            # Front face (counter-clockwise looking from front)
            vf00 = front_verts[(j, i)]
            vf10 = front_verts[(j, i+1)]
            vf11 = front_verts[(j+1, i+1)]
            vf01 = front_verts[(j+1, i)]
            
            f_front = bm.faces.new([vf00, vf10, vf11, vf01])
            for loop in f_front.loops:
                # Map to master image UVs
                u = (loop.vert.co.x / scale_x) + 0.5
                v = (loop.vert.co.z / scale_z)
                loop[uv_layer].uv = (u, v)
                
            # Back face (clockwise looking from front, counter-clockwise from back)
            vb00 = back_verts[(j, i)]
            vb10 = back_verts[(j, i+1)]
            vb11 = back_verts[(j+1, i+1)]
            vb01 = back_verts[(j+1, i)]
            
            f_back = bm.faces.new([vb00, vb01, vb11, vb10])
            for loop in f_back.loops:
                # Mirrored UV for back
                u = (loop.vert.co.x / scale_x) + 0.5
                v = (loop.vert.co.z / scale_z)
                loop[uv_layer].uv = (u, v)

# Bridge the boundary edges to create a watertight solid volume
# For each cell, check which of its 4 edges is on the border of cell_mask
for j in range(grid_size - 1):
    for i in range(grid_size - 1):
        if cell_mask[j, i]:
            # Bottom edge (j)
            if j == 0 or not cell_mask[j-1, i]:
                vf0, vf1 = front_verts[(j, i)], front_verts[(j, i+1)]
                vb0, vb1 = back_verts[(j, i)], back_verts[(j, i+1)]
                side_f = bm.faces.new([vf0, vb0, vb1, vf1])
                for loop in side_f.loops:
                    loop[uv_layer].uv = ((loop.vert.co.x / scale_x) + 0.5, (loop.vert.co.z / scale_z))
                    
            # Top edge (j+1)
            if j == grid_size - 2 or not cell_mask[j+1, i]:
                vf0, vf1 = front_verts[(j+1, i+1)], front_verts[(j+1, i)]
                vb0, vb1 = back_verts[(j+1, i+1)], back_verts[(j+1, i)]
                side_f = bm.faces.new([vf0, vb0, vb1, vf1])
                for loop in side_f.loops:
                    loop[uv_layer].uv = ((loop.vert.co.x / scale_x) + 0.5, (loop.vert.co.z / scale_z))
                    
            # Left edge (i)
            if i == 0 or not cell_mask[j, i-1]:
                vf0, vf1 = front_verts[(j+1, i)], front_verts[(j, i)]
                vb0, vb1 = back_verts[(j+1, i)], back_verts[(j, i)]
                side_f = bm.faces.new([vf0, vb0, vb1, vf1])
                for loop in side_f.loops:
                    loop[uv_layer].uv = ((loop.vert.co.x / scale_x) + 0.5, (loop.vert.co.z / scale_z))
                    
            # Right edge (i+1)
            if i == grid_size - 2 or not cell_mask[j, i+1]:
                vf0, vf1 = front_verts[(j, i+1)], front_verts[(j+1, i+1)]
                vb0, vb1 = back_verts[(j, i+1)], back_verts[(j+1, i+1)]
                side_f = bm.faces.new([vf0, vb0, vb1, vf1])
                for loop in side_f.loops:
                    loop[uv_layer].uv = ((loop.vert.co.x / scale_x) + 0.5, (loop.vert.co.z / scale_z))

mesh = bpy.data.meshes.new("WorkerAnt_Solid")
bm.to_mesh(mesh)
bm.free()

ant_obj = bpy.data.objects.new("WorkerAnt_Solid", mesh)
bpy.context.scene.collection.objects.link(ant_obj)

# Smooth shading
for p in mesh.polygons:
    p.use_smooth = True

# 4. Material Setup with Master Render
mat = bpy.data.materials.new("M_AntMaster")
mat.use_nodes = True
nodes = mat.node_tree.nodes
links = mat.node_tree.links
nodes.clear()

out = nodes.new('ShaderNodeOutputMaterial')
bsdf = nodes.new('ShaderNodeBsdfPrincipled')
links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

master_img = bpy.data.images.load(master_path)
tex = nodes.new('ShaderNodeTexImage')
tex.image = master_img
links.new(tex.outputs['Color'], bsdf.inputs['Base Color'])

bsdf.inputs['Roughness'].default_value = 0.28
bsdf.inputs['Coat Weight'].default_value = 0.85
bsdf.inputs['Coat Roughness'].default_value = 0.12

ant_obj.data.materials.append(mat)

# 5. Studio Lighting Rig
light_data = bpy.data.lights.new("KeyLight", 'POINT')
light_data.energy = 300.0
light_obj = bpy.data.objects.new("KeyLight", light_data)
bpy.context.scene.collection.objects.link(light_obj)
light_obj.location = (-2.5, -3.5, 3.5)

light_data2 = bpy.data.lights.new("FillLight", 'POINT')
light_data2.energy = 150.0
light_obj2 = bpy.data.objects.new("FillLight", light_data2)
bpy.context.scene.collection.objects.link(light_obj2)
light_obj2.location = (2.5, -2.5, 2.0)

light_data3 = bpy.data.lights.new("RimLight", 'POINT')
light_data3.energy = 350.0
light_obj3 = bpy.data.objects.new("RimLight", light_data3)
bpy.context.scene.collection.objects.link(light_obj3)
light_obj3.location = (0.0, 3.0, 3.0)

# 6. Render Stills
cam_data = bpy.data.cameras.new("TestCam")
cam_data.lens = 65.0
cam_obj = bpy.data.objects.new("TestCam", cam_data)
bpy.context.scene.collection.objects.link(cam_obj)
scene.camera = cam_obj

# Front view
cam_obj.location = Vector((0.0, -4.2, 1.2))
cam_obj.rotation_euler = Euler((math.radians(87), 0, 0), 'XYZ')
scene.render.filepath = '/tmp/test_recon_front.png'
bpy.ops.render.render(write_still=True)
print("Rendered /tmp/test_recon_front.png")

# 3/4 Perspective view
cam_obj.location = Vector((-2.8, -3.2, 1.5))
cam_obj.rotation_euler = Euler((math.radians(82), 0, math.radians(-38)), 'XYZ')
scene.render.filepath = '/tmp/test_recon_angle.png'
bpy.ops.render.render(write_still=True)
print("Rendered /tmp/test_recon_angle.png")
