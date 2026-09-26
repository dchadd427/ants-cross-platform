"""
Blender 4.3.2 Script: 360° Ray-Traced Cycles Turntable Generator for Combat Ant (Caste #3)
Renders 36 photorealistic path-traced frames (every 10 degrees) with Metal GPU.
Saves to web/viewer3d/turntable_combat/frame_XX.png for smooth interactive browser scrubbing.
"""

import bpy
import math
from mathutils import Vector, Euler
import os

blend_file = "/Users/dchadd/Desktop/Ants-Mac/tools/blender/combat_ant_authentic.blend"
out_dir = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d/turntable_combat"
os.makedirs(out_dir, exist_ok=True)

# 1. Open the authentic blend file
bpy.ops.wm.open_mainfile(filepath=blend_file)

scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'GPU'

preferences = bpy.context.preferences
cycles_prefs = preferences.addons['cycles'].preferences
cycles_prefs.compute_device_type = 'METAL'
cycles_prefs.get_devices()
for d in cycles_prefs.devices:
    d.use = True

scene.cycles.samples = 40
scene.cycles.use_denoising = True
scene.render.film_transparent = False
scene.render.resolution_x = 800
scene.render.resolution_y = 800

# 2. Camera Setup (Full heroic brawler framing)
cam_obj = scene.camera
if not cam_obj:
    cam_data = bpy.data.cameras.new("TurntableCam")
    cam_obj = bpy.data.objects.new("TurntableCam", cam_data)
    bpy.context.scene.collection.objects.link(cam_obj)
    scene.camera = cam_obj

cam_obj.data.lens = 52.0
cam_distance = 4.7
cam_height = 1.25

# 3. Create Turntable Empty to rotate ant collection
ant_col = bpy.data.collections.get("Combat_Ant_Authentic")
if not ant_col:
    print("Error: Combat_Ant_Authentic collection not found!")
    exit(1)

turntable_empty = bpy.data.objects.new("Turntable_Center", None)
turntable_empty.location = Vector((0, 0, 0))
bpy.context.scene.collection.objects.link(turntable_empty)

for obj in ant_col.objects:
    if obj.parent is None:
        obj.parent = turntable_empty

# Camera stays stationary at front, ant spins on center turntable
cam_obj.location = Vector((0.0, -cam_distance, cam_height))
cam_obj.rotation_euler = Euler((math.radians(88), 0, 0), 'XYZ')

num_frames = 36
print(f"Starting 360° Cycles Metal GPU Turntable Render for Combat Ant ({num_frames} frames)...")

for frame_idx in range(num_frames):
    angle_deg = frame_idx * (360.0 / num_frames)
    turntable_empty.rotation_euler.z = math.radians(angle_deg)
    bpy.context.view_layer.update()
    
    frame_filename = f"frame_{frame_idx:02d}.png"
    frame_path = os.path.join(out_dir, frame_filename)
    scene.render.filepath = frame_path
    
    print(f"[{frame_idx+1:02d}/{num_frames}] Rendering {angle_deg:.0f}° -> {frame_filename}...")
    bpy.ops.render.render(write_still=True)

print("360° Cycles Turntable rendering for Combat Ant complete!")
