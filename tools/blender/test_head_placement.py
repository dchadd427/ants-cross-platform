"""
Blender fast test script: Character Head, Eyes, and Mandibles
Directly aligns geometry in unified world coordinates.
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
scene.cycles.samples = 24
scene.render.resolution_x = 720
scene.render.resolution_y = 720

web_dir = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d"

# 1. Materials
mat_chitin = bpy.data.materials.new("M_Chitin")
mat_chitin.use_nodes = True
bsdf = mat_chitin.node_tree.nodes.get("Principled BSDF")
tex = mat_chitin.node_tree.nodes.new("ShaderNodeTexImage")
tex.image = bpy.data.images.load(f"{web_dir}/chitin_pbr.png")
mat_chitin.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
bsdf.inputs["Roughness"].default_value = 0.38

mat_eye = bpy.data.materials.new("M_Eye")
mat_eye.use_nodes = True
bsdf_e = mat_eye.node_tree.nodes.get("Principled BSDF")
tex_e = mat_eye.node_tree.nodes.new("ShaderNodeTexImage")
tex_e.image = bpy.data.images.load(f"{web_dir}/eye_pbr.png")
mat_eye.node_tree.links.new(tex_e.outputs["Color"], bsdf_e.inputs["Base Color"])
bsdf_e.inputs["Roughness"].default_value = 0.02
bsdf_e.inputs["Coat Weight"].default_value = 1.0

mat_mand = bpy.data.materials.new("M_Mand")
mat_mand.use_nodes = True
bsdf_m = mat_mand.node_tree.nodes.get("Principled BSDF")
tex_m = mat_mand.node_tree.nodes.new("ShaderNodeTexImage")
tex_m.image = bpy.data.images.load(f"{web_dir}/mandible_pbr.png")
mat_mand.node_tree.links.new(tex_m.outputs["Color"], bsdf_m.inputs["Base Color"])
bsdf_m.inputs["Roughness"].default_value = 0.20
bsdf_m.inputs["Subsurface Weight"].default_value = 0.25

# 2. Eyes: Placed at world positions
# Center of head is around (0, 0, 1.35)
# Eyes centered at y = -0.30, z = 1.38
eyes = []
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    name = "Eye_L" if is_left else "Eye_R"
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=48, ring_count=36, radius=0.22,
        location=(sign * 0.22, -0.30, 1.38)
    )
    eye = bpy.context.active_object
    eye.name = name
    eye.scale = (0.95, 1.0, 1.10) # Expressive vertical oval
    yaw = -math.pi / 2.0 + (sign * math.radians(8.0))
    pitch = math.radians(2.0)
    eye.rotation_euler = Euler((pitch, 0.0, yaw), 'XYZ')
    eye.data.materials.append(mat_eye)
    bpy.ops.object.shade_smooth()
    eyes.append(eye)

# 3. Head Cranium: Sculpted around the eyes
bm = bmesh.new()
bmesh.ops.create_cube(bm, size=1.0)
bmesh.ops.subdivide_edges(bm, edges=bm.edges, cuts=5, use_grid_fill=True)

for v in bm.verts:
    norm = v.co.normalized()
    # Local shape centered at (0, 0, 0)
    x = norm.x * 0.50
    y = norm.y * 0.44
    z = norm.z * 0.46
    
    # World position will be: x, y - 0.04, z + 1.35
    w_x = x
    w_y = y - 0.04
    w_z = z + 1.35
    
    # Crown loaf: rounded cushion with central furrow
    if z > 0.08:
        furrow = 1.0 - 0.12 * math.exp(-((x / 0.14) ** 2))
        z *= furrow
        x *= (1.0 + 0.14 * (z / 0.46))
        
    # Brow ridge over eyes:
    if 0.05 < z < 0.28 and y < -0.12:
        bf = math.sin((z - 0.05) / 0.23 * math.pi)
        y -= 0.08 * bf * (1.0 - abs(x / 0.50))
        
    # Orbital socket cavities (around eyes at sign * 0.22, local y = -0.20, local z = 0.03)
    for sign in [-1.0, 1.0]:
        sx, sy, sz = sign * 0.22, -0.20, 0.03
        d = math.sqrt((x - sx)**2 + (y - sy)**2 + (z - sz)**2)
        r_sock = 0.26
        if d < r_sock:
            falloff = (1.0 - (d / r_sock)**2)**1.5
            y += 0.18 * falloff
            x -= sign * 0.05 * falloff
            
    # Clypeus / Snout between eyes (x in [-0.10, 0.10], z < 0.05)
    if z < 0.05:
        ts = min(1.0, (0.05 - z) / 0.50)
        x *= (1.0 - 0.45 * ts)
        if y < 0 and abs(x) < 0.12:
            y -= 0.10 * (1.0 - abs(x / 0.12)) * ts
            
    v.co = Vector((x, y, z))

uv_layer = bm.loops.layers.uv.new("UVMap")
for face in bm.faces:
    for loop in face.loops:
        norm = loop.vert.co.normalized()
        u = math.atan2(norm.x, -norm.y) / (2.0 * math.pi) + 0.5
        v_coord = norm.z * 0.5 + 0.5
        loop[uv_layer].uv = (u, v_coord)

h_mesh = bpy.data.meshes.new("HeadMesh")
bm.to_mesh(h_mesh)
bm.free()

head = bpy.data.objects.new("Head", h_mesh)
head.location = (0, -0.10, 1.35)
bpy.context.scene.collection.objects.link(head)
head.data.materials.append(mat_chitin)
sub = head.modifiers.new("Subsurf", 'SUBSURF')
sub.levels = 2
for p in h_mesh.polygons:
    p.use_smooth = True

# 4. Mandibles (Pincers cupping underneath the snout)
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    name = "Mand_L" if is_left else "Mand_R"
    bm_m = bmesh.new()
    
    # Cheek anchor to pincer tip in world coords
    pts = [
        Vector((sign * 0.18, -0.22, 1.12)), # Cheek anchor
        Vector((sign * 0.22, -0.32, 1.08)), # Forward flare
        Vector((sign * 0.18, -0.40, 1.04)), # Apex curve
        Vector((sign * 0.10, -0.41, 1.02)), # Turning inward
        Vector((sign * 0.03, -0.38, 1.00))  # Tip
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
            c_ring.append(bm_m.verts.new(Vector((vx, vy, vz))))
        rings.append(c_ring)
        
    for i in range(len(pts) - 1):
        r0, r1 = rings[i], rings[i+1]
        for j in range(num_p):
            jn = (j + 1) % num_p
            bm_m.faces.new([r0[j], r0[jn], r1[jn], r1[j]])
    bm_m.faces.new(rings[0][::-1])
    bm_m.faces.new(rings[-1])
    
    # Teeth
    teeth = [
        (Vector((sign * 0.04, -0.38, 1.00)), Vector((sign * 0.06, -0.40, 0.99)), Vector((sign * 0.00, -0.38, 1.00))),
        (Vector((sign * 0.09, -0.35, 1.02)), Vector((sign * 0.12, -0.37, 1.01)), Vector((sign * 0.05, -0.35, 1.02)))
    ]
    for b1, b2, tp in teeth:
        v1 = bm_m.verts.new(b1)
        v2 = bm_m.verts.new(b2)
        vt = bm_m.verts.new(tp)
        bm_m.faces.new([v1, v2, vt])
        
    uv_l = bm_m.loops.layers.uv.new("UVMap")
    for face in bm_m.faces:
        for loop in face.loops:
            v_val = min(1.0, max(0.0, (-loop.vert.co.y - 0.20) / 0.22))
            u_val = loop.vert.co.z * 0.5 + 0.5
            loop[uv_l].uv = (u_val, v_val)
            
    m_mesh = bpy.data.meshes.new(name)
    bm_m.to_mesh(m_mesh)
    bm_m.free()
    m_obj = bpy.data.objects.new(name, m_mesh)
    bpy.context.scene.collection.objects.link(m_obj)
    m_obj.data.materials.append(mat_mand)
    sub_m = m_obj.modifiers.new("Subsurf", 'SUBSURF')
    sub_m.levels = 2
    for p in m_mesh.polygons:
        p.use_smooth = True

# 5. Camera & Lights
cam_d = bpy.data.cameras.new("Cam")
cam = bpy.data.objects.new("Cam", cam_d)
cam.location = (0, -2.4, 1.34)
cam.rotation_euler = (math.radians(90), 0, 0)
bpy.context.scene.collection.objects.link(cam)
scene.camera = cam

l1_d = bpy.data.lights.new("Key", 'POINT')
l1_d.energy = 220
l1 = bpy.data.objects.new("Key", l1_d)
l1.location = (-1.5, -2.0, 2.5)
bpy.context.scene.collection.objects.link(l1)

l2_d = bpy.data.lights.new("Fill", 'POINT')
l2_d.energy = 100
l2 = bpy.data.objects.new("Fill", l2_d)
l2.location = (1.8, -1.8, 1.8)
bpy.context.scene.collection.objects.link(l2)

l3_d = bpy.data.lights.new("Rim", 'POINT')
l3_d.energy = 260
l3 = bpy.data.objects.new("Rim", l3_d)
l3.location = (0.0, 1.8, 2.6)
bpy.context.scene.collection.objects.link(l3)

out_file = f"{web_dir}/test_head_face.png"
scene.render.filepath = out_file
bpy.ops.render.render(write_still=True)
print(f"Rendered test head to: {out_file}")
