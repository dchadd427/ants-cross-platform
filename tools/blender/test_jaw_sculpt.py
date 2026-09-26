"""
Blender script to test the overhaul of the 3D caliper pincer claws and facial structure.
Renders a high-resolution closeup to web/viewer3d/test_jaw_render.png for visual verification.
"""

import bpy
import bmesh
import math
from mathutils import Vector, Euler
import os

blend_file = "/Users/dchadd/Desktop/Ants-Mac/tools/blender/worker_ant_authentic.blend"
bpy.ops.wm.open_mainfile(filepath=blend_file)

scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'GPU'
scene.cycles.samples = 64
scene.cycles.use_denoising = True
scene.render.resolution_x = 900
scene.render.resolution_y = 900

# Remove old mandibles and teeth
for obj_name in ["Mandible_L", "Mandible_R", "Tooth_L1", "Tooth_L2", "Tooth_R1", "Tooth_R2", "Oral_Cavity"]:
    if obj_name in bpy.data.objects:
        bpy.data.objects.remove(bpy.data.objects[obj_name], do_unlink=True)

worker_col = bpy.data.collections.get("Worker_Ant_Authentic")
web_dir = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d"

# Update mandible material to have proper chartreuse-ivory gradient mapping
mat_mandible = bpy.data.materials.get("M_Mandible_Authentic")
if not mat_mandible:
    mat_mandible = bpy.data.materials.new("M_Mandible_Authentic")
    mat_mandible.use_nodes = True

nodes_m = mat_mandible.node_tree.nodes
links_m = mat_mandible.node_tree.links
nodes_m.clear()

out_m = nodes_m.new('ShaderNodeOutputMaterial')
bsdf_m = nodes_m.new('ShaderNodeBsdfPrincipled')
links_m.new(bsdf_m.outputs['BSDF'], out_m.inputs['Surface'])

tex_mandible = nodes_m.new('ShaderNodeTexImage')
tex_mandible.image = bpy.data.images.load(f"{web_dir}/mandible_pbr.png")
links_m.new(tex_mandible.outputs['Color'], bsdf_m.inputs['Base Color'])
bsdf_m.inputs['Base Color'].default_value = (0.38, 0.56, 0.20, 1.0)
bsdf_m.inputs['Roughness'].default_value = 0.38
bsdf_m.inputs['Coat Weight'].default_value = 0.35
bsdf_m.inputs['Coat Roughness'].default_value = 0.15

# Teeth material
mat_teeth = bpy.data.materials.get("M_Teeth_Authentic")
if not mat_teeth:
    mat_teeth = bpy.data.materials.new("M_Teeth_Authentic")
    mat_teeth.use_nodes = True
nodes_t = mat_teeth.node_tree.nodes
links_t = mat_teeth.node_tree.links
nodes_t.clear()
out_t = nodes_t.new('ShaderNodeOutputMaterial')
bsdf_t = nodes_t.new('ShaderNodeBsdfPrincipled')
links_t.new(bsdf_t.outputs['BSDF'], out_t.inputs['Surface'])
bsdf_t.inputs['Base Color'].default_value = (0.95, 0.95, 0.88, 1.0)
bsdf_t.inputs['Roughness'].default_value = 0.25
bsdf_t.inputs['Coat Weight'].default_value = 0.40

# Oral Cavity material
mat_oral = bpy.data.materials.get("M_Oral_Authentic")
if not mat_oral:
    mat_oral = bpy.data.materials.new("M_Oral_Authentic")
    mat_oral.use_nodes = True
nodes_o = mat_oral.node_tree.nodes
links_o = mat_oral.node_tree.links
nodes_o.clear()
out_o = nodes_o.new('ShaderNodeOutputMaterial')
bsdf_o = nodes_o.new('ShaderNodeBsdfPrincipled')
links_o.new(bsdf_o.outputs['BSDF'], out_o.inputs['Surface'])
bsdf_o.inputs['Base Color'].default_value = (0.012, 0.008, 0.015, 1.0)
bsdf_o.inputs['Roughness'].default_value = 0.95

# Update Head Mesh so the lower face terminates at the mouthparts without double-chin
head_obj = bpy.data.objects.get("Head")
if head_obj:
    # Modify the head mesh vertices
    mesh_h = head_obj.data
    for v in mesh_h.vertices:
        x, y, z = v.co.x, v.co.y, v.co.z
        
        # Carve away the awkward lower chin below the mouthparts
        if z < -0.05 and y < 0.05:
            # Cut back sharply into the oral cavity/neck
            t_cut = min(1.0, (-z - 0.05) / 0.20)
            # Push posterior and upward
            y += 0.12 * t_cut
            z += 0.08 * t_cut
            x *= (1.0 - 0.25 * t_cut)
        elif z < -0.12:
            # Neck taper
            t_neck = min(1.0, (-z - 0.12) / 0.18)
            x *= (1.0 - 0.40 * t_neck)
            y *= (1.0 - 0.35 * t_neck)
            
        v.co = Vector((x, y, z))
    mesh_h.update()

# -----------------------------------------------------------------------------
# Precision Caliper Pincer Claws (Hanging down from cheeks as bottom mouthparts)
# -----------------------------------------------------------------------------
def make_caliper_claw(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    bm = bmesh.new()

    # True caliper pincer jaw path:
    # 0. Hinge under cheek margin (world Z=1.45)
    # 1. Lateral caliper flare out to X=sign*0.24, sweeping forward & down
    # 2. Anterior bulbous muscle swelling at front of jaw (world Z=1.35, Y=-0.32)
    # 3. Medial inward hook curving toward midline (world Z=1.33)
    # 4. Caliper fang tip pointing medially inward across central mouth gap (world Z=1.32)
    stations = [
        # 0. Cheek hinge condyle
        (Vector((sign * 0.185, -0.160, 1.450)), Vector((sign * 0.25, -0.75, -0.60)).normalized(), 0.055, 0.050),
        # 1. Broad lateral caliper bow
        (Vector((sign * 0.235, -0.230, 1.400)), Vector((sign * 0.10, -0.90, -0.42)).normalized(), 0.070, 0.062),
        # 2. Massive anterior bulbous muscle lobe (sweeping forward and down)
        (Vector((sign * 0.190, -0.320, 1.350)), Vector((sign * -0.50, -0.80, -0.32)).normalized(), 0.076, 0.068),
        # 3. Anterior medial turn with deep inner bite notch
        (Vector((sign * 0.125, -0.325, 1.330)), Vector((sign * -0.88, -0.45, -0.15)).normalized(), 0.058, 0.052),
        # 4. Inward-hooking caliper tip (leaving ~0.130 unit open central mouth gap)
        (Vector((sign * 0.065, -0.290, 1.320)), Vector((sign * -0.96, -0.26, -0.05)).normalized(), 0.034, 0.030),
    ]

    num_pts = 16
    rings = []

    for i, (center, normal, rx, rz) in enumerate(stations):
        up_ref = Vector((0, 0, 1))
        right = normal.cross(up_ref).normalized()
        up = right.cross(normal).normalized()

        c_ring = []
        for j in range(num_pts):
            th = 2.0 * math.pi * j / num_pts
            cos_t = math.cos(th)
            sin_t = math.sin(th)

            # Plump outer hull vs scooped inner concavity
            is_outer = (cos_t * sign > 0)
            if i >= 2 and not is_outer:
                # Deep C-shaped bite notch scooped into inner surface
                rx_eff = rx * 0.48
            else:
                rx_eff = rx * (1.28 if is_outer else 0.82)

            rz_eff = rz * (0.85 if sin_t < 0 else 1.10)

            p_local = (right * (cos_t * rx_eff)) + (up * (sin_t * rz_eff))
            world_p = center + p_local
            c_ring.append(bm.verts.new(world_p))
        rings.append(c_ring)

    uv_layer = bm.loops.layers.uv.new("UVMap")

    for i in range(len(stations) - 1):
        r0 = rings[i]
        r1 = rings[i + 1]
        for j in range(num_pts):
            jn = (j + 1) % num_pts
            if is_left:
                f = bm.faces.new([r0[j], r0[jn], r1[jn], r1[j]])
            else:
                f = bm.faces.new([r0[jn], r0[j], r1[j], r1[jn]])

            # UV coordinate: V runs along length from base (0.0) to tip (1.0)
            v0 = i / (len(stations) - 1)
            v1 = (i + 1) / (len(stations) - 1)
            u0 = j / num_pts
            u1 = (j + 1) / num_pts

            if is_left:
                f.loops[0][uv_layer].uv = (u0, v0)
                f.loops[1][uv_layer].uv = (u1, v0)
                f.loops[2][uv_layer].uv = (u1, v1)
                f.loops[3][uv_layer].uv = (u0, v1)
            else:
                f.loops[0][uv_layer].uv = (u1, v0)
                f.loops[1][uv_layer].uv = (u0, v0)
                f.loops[2][uv_layer].uv = (u0, v1)
                f.loops[3][uv_layer].uv = (u1, v1)

    if is_left:
        bm.faces.new(rings[0][::-1])
    else:
        bm.faces.new(rings[0])

    tip_center = stations[-1][0] + (stations[-1][1] * 0.015)
    tip_v = bm.verts.new(tip_center)
    last_ring = rings[-1]
    for j in range(num_pts):
        jn = (j + 1) % num_pts
        if is_left:
            f = bm.faces.new([last_ring[j], last_ring[jn], tip_v])
            f.loops[0][uv_layer].uv = (j / num_pts, 0.95)
            f.loops[1][uv_layer].uv = ((j + 1) / num_pts, 0.95)
            f.loops[2][uv_layer].uv = (0.5, 1.0)
        else:
            f = bm.faces.new([last_ring[jn], last_ring[j], tip_v])
            f.loops[0][uv_layer].uv = ((j + 1) / num_pts, 0.95)
            f.loops[1][uv_layer].uv = (j / num_pts, 0.95)
            f.loops[2][uv_layer].uv = (0.5, 1.0)

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)

    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()

    for p in mesh.polygons:
        p.use_smooth = True

    mand_obj = bpy.data.objects.new(name, mesh)
    mand_obj.data.materials.append(mat_mandible)
    sub = mand_obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 2
    worker_col.objects.link(mand_obj)

    # Sharp biting fangs inside inner scoop
    suf = "L" if is_left else "R"
    # Upper primary fang
    f1_loc = (sign * 0.100, -0.300, 1.365)
    f1_rot = (math.radians(18), math.radians(sign * -28), math.radians(sign * -50))
    bpy.ops.mesh.primitive_cone_add(vertices=14, radius1=0.016, radius2=0.001, depth=0.048, location=f1_loc, rotation=f1_rot)
    tooth1 = bpy.context.active_object
    tooth1.name = f"Tooth_{suf}1"
    tooth1.data.materials.append(mat_teeth)
    bpy.ops.object.shade_smooth()
    worker_col.objects.link(tooth1)
    bpy.context.scene.collection.objects.unlink(tooth1)

    # Lower secondary fang
    f2_loc = (sign * 0.075, -0.285, 1.335)
    f2_rot = (math.radians(8), math.radians(sign * -22), math.radians(sign * -65))
    bpy.ops.mesh.primitive_cone_add(vertices=12, radius1=0.013, radius2=0.001, depth=0.040, location=f2_loc, rotation=f2_rot)
    tooth2 = bpy.context.active_object
    tooth2.name = f"Tooth_{suf}2"
    tooth2.data.materials.append(mat_teeth)
    bpy.ops.object.shade_smooth()
    worker_col.objects.link(tooth2)
    bpy.context.scene.collection.objects.unlink(tooth2)

# Recessed dark oral cavity
def make_oral_cavity(name):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=16, radius=0.080)
    for v in list(bm.verts):
        if v.co.y < 0.005:
            bm.verts.remove(v)
    for v in bm.verts:
        v.co.x *= 1.20
        v.co.y *= 1.40
        v.co.z *= 0.90
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    for f in bm.faces:
        f.normal_flip()
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    for p in mesh.polygons:
        p.use_smooth = True
    oral_obj = bpy.data.objects.new(name, mesh)
    oral_obj.location = (0.0, -0.210, 1.350)
    oral_obj.data.materials.append(mat_oral)
    worker_col.objects.link(oral_obj)

make_caliper_claw("Mandible_L", True)
make_caliper_claw("Mandible_R", False)
make_oral_cavity("Oral_Cavity")

# Set up macro camera to render face and mouth
cam_data = bpy.data.cameras.new("MacroCam")
cam_obj = bpy.data.objects.new("MacroCam", cam_data)
bpy.context.scene.collection.objects.link(cam_obj)
scene.camera = cam_obj

cam_data.lens = 95.0
cam_obj.location = Vector((0.0, -1.55, 1.48))
cam_obj.rotation_euler = Euler((math.radians(82), 0, 0), 'XYZ')

out_path = f"{web_dir}/test_jaw_render.png"
scene.render.filepath = out_path
print(f"Rendering jaw test to {out_path}...")
bpy.ops.render.render(write_still=True)
print("Render complete!")
