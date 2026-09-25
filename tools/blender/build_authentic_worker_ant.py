"""
Blender 4.3.2 Script: Authentic Heroic Worker Ant (Caste #1)
Directly calibrated to master preview artwork (worker_ant_master_1790372115097.jpg):
1. Limbs: Anatomical insect segments with muscular flattened femurs, flared joint sleeves,
   articulated 2-fingered hands, and planted foot pads. Mottled terracotta/mahogany chitin.
2. Mouth/Mandibles: 3D sculpted pincer jaws projecting boldly forward with interlocking
   sharp medial teeth, glowing chartreuse tooth gradient, and protruding snout/clypeus.
3. Eyes: Expressive stylized cartoon eyes with warm ivory sclera, olive-hazel iris with
   golden-amber sunburst, black pupil, and studio softbox catchlights.
4. Antennae: Elegant outward-curving Bézier splines (V-shape '\ /') curving forward/outward
   with rounded club tips, zero 90° bends.
5. Height & Posture: Tall heroic upright character stance, dark studio lighting with crisp rim contour.
"""

import bpy
import bmesh
import math
from mathutils import Vector, Euler, Matrix
import os

# -----------------------------------------------------------------------------
# 1. Reset Scene & Render Setup
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
scene.render.film_transparent = False # Dark studio backdrop like master reference!
scene.render.resolution_x = 1080
scene.render.resolution_y = 1080

web_dir = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d"

# Dark studio world environment
world = bpy.data.worlds.new("StudioWorld")
scene.world = world
world.use_nodes = True
bg_node = world.node_tree.nodes['Background']
bg_node.inputs['Color'].default_value = (0.015, 0.015, 0.018, 1.0)
bg_node.inputs['Strength'].default_value = 0.40

# -----------------------------------------------------------------------------
# 2. Authentic PBR Materials
# -----------------------------------------------------------------------------
def create_materials():
    # A. Chitin Material (Mottled sage green with warm terracotta dusting)
    mat_chitin = bpy.data.materials.new("M_Chitin_Authentic")
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
    noise_bump.inputs['Scale'].default_value = 65.0
    noise_bump.inputs['Detail'].default_value = 6.0
    noise_bump.inputs['Roughness'].default_value = 0.55
    links.new(tex_coord.outputs['Object'], noise_bump.inputs['Vector'])

    bump = nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.22
    bump.inputs['Distance'].default_value = 0.008
    links.new(noise_bump.outputs['Fac'], bump.inputs['Height'])
    links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])

    bsdf.inputs['Roughness'].default_value = 0.38
    bsdf.inputs['Coat Weight'].default_value = 0.70
    bsdf.inputs['Coat Roughness'].default_value = 0.16
    bsdf.inputs['Subsurface Weight'].default_value = 0.10
    bsdf.inputs['Subsurface Radius'].default_value = (0.3, 0.5, 0.2)

    # B. Eye Material
    mat_eye = bpy.data.materials.new("M_Eye_Authentic")
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

    bsdf.inputs['Roughness'].default_value = 0.04
    bsdf.inputs['Coat Weight'].default_value = 1.0
    bsdf.inputs['Coat Roughness'].default_value = 0.02
    bsdf.inputs['IOR'].default_value = 1.45

    # C. Mandible Material (Sage to Chartreuse gradient)
    mat_mandible = bpy.data.materials.new("M_Mandible_Authentic")
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

    bsdf.inputs['Roughness'].default_value = 0.24
    bsdf.inputs['Coat Weight'].default_value = 0.85
    bsdf.inputs['Coat Roughness'].default_value = 0.10
    bsdf.inputs['Subsurface Weight'].default_value = 0.30
    bsdf.inputs['Subsurface Radius'].default_value = (0.7, 0.95, 0.4)

    # D. Limbs Material (Mottled terracotta/mahogany)
    mat_limbs = bpy.data.materials.new("M_Limbs_Authentic")
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

    # Micro bump for insect chitin
    noise_limb = nodes.new('ShaderNodeTexNoise')
    noise_limb.inputs['Scale'].default_value = 85.0
    noise_limb.inputs['Detail'].default_value = 4.0
    links.new(tex_coord.outputs['Object'], noise_limb.inputs['Vector'])

    bump_limb = nodes.new('ShaderNodeBump')
    bump_limb.inputs['Strength'].default_value = 0.18
    bump_limb.inputs['Distance'].default_value = 0.005
    links.new(noise_limb.outputs['Fac'], bump_limb.inputs['Height'])
    links.new(bump_limb.outputs['Normal'], bsdf.inputs['Normal'])

    bsdf.inputs['Roughness'].default_value = 0.44
    bsdf.inputs['Coat Weight'].default_value = 0.40
    bsdf.inputs['Coat Roughness'].default_value = 0.22

    # E. Antenna Material
    mat_antenna = bpy.data.materials.new("M_Antenna_Authentic")
    mat_antenna.use_nodes = True
    nodes = mat_antenna.node_tree.nodes
    links = mat_antenna.node_tree.links
    nodes.clear()

    out = nodes.new('ShaderNodeOutputMaterial')
    bsdf = nodes.new('ShaderNodeBsdfPrincipled')
    links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])

    bsdf.inputs['Base Color'].default_value = (0.24, 0.18, 0.14, 1.0)
    bsdf.inputs['Roughness'].default_value = 0.55
    bsdf.inputs['Coat Weight'].default_value = 0.35
    bsdf.inputs['Coat Roughness'].default_value = 0.25

    return mat_chitin, mat_eye, mat_mandible, mat_limbs, mat_antenna

mat_chitin, mat_eye, mat_mandible, mat_limbs, mat_antenna = create_materials()

worker_col = bpy.data.collections.new("Worker_Ant_Authentic")
bpy.context.scene.collection.children.link(worker_col)

def reg(obj):
    worker_col.objects.link(obj)
    if obj.name in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.unlink(obj)
    return obj

# -----------------------------------------------------------------------------
# 3. Head Cranium: Sculpted Heart-Shape with Snout & Orbital Brow Hoods
# -----------------------------------------------------------------------------
bm_head = bmesh.new()
bmesh.ops.create_cube(bm_head, size=1.0)
bmesh.ops.subdivide_edges(bm_head, edges=bm_head.edges, cuts=6, use_grid_fill=True)

# Head origin at (0, -0.08, 1.70)
for v in bm_head.verts:
    norm = v.co.normalized()
    # Dimensions: width 0.54, depth 0.44, height 0.48
    x = norm.x * 0.54
    y = norm.y * 0.44
    z = norm.z * 0.48

    # 1. Crown lobes: rounded pillows with soft central depression
    if z > 0.05:
        furrow = 1.0 - 0.14 * math.exp(-((x / 0.12) ** 2))
        z *= furrow
        x *= (1.0 + 0.16 * (z / 0.48))

    # 2. Prominent Eyebrow Hood: fleshy arch over each orbital socket
    if 0.02 < z < 0.30 and y < -0.10:
        bf = math.sin((z - 0.02) / 0.28 * math.pi)
        # Arch over eyes
        for sign in [-1.0, 1.0]:
            dist_brow = abs(x - sign * 0.19)
            if dist_brow < 0.16:
                arch_w = math.cos(dist_brow / 0.16 * (math.pi / 2.0))
                y -= 0.09 * bf * arch_w
                z += 0.03 * bf * arch_w

    # 3. Deep Concave Orbital Sockets (around eyes at sign * 0.19, y = -0.24, z = 0.02)
    for sign in [-1.0, 1.0]:
        sx, sy, sz = sign * 0.19, -0.24, 0.02
        d = math.sqrt((x - sx)**2 + (y - sy)**2 + (z - sz)**2)
        r_sock = 0.28
        if d < r_sock:
            falloff = (1.0 - (d / r_sock)**2)**1.5
            y += 0.19 * falloff
            x -= sign * 0.04 * falloff

    # 4. Protruding Clypeus / Snout (bulldog nose bridge between eyes)
    if z < 0.08:
        ts = min(1.0, (0.08 - z) / 0.48)
        if abs(x) < 0.13:
            # Narrow snout bridge
            x *= (1.0 - 0.28 * ts)
            if y < 0:
                # Protrude forward boldly!
                y -= 0.18 * (1.0 - abs(x / 0.13)) * ts
                # Soft central cleft on lower snout
                if z < -0.15 and abs(x) < 0.04:
                    y += 0.04 * (1.0 - abs(x / 0.04))

    # 5. Cheeks flanking lower jaw
    if -0.38 < z < -0.05 and abs(x) > 0.12:
        cheek = math.sin((z - (-0.38)) / 0.33 * math.pi)
        x *= (1.0 + 0.18 * cheek)
        if y < 0:
            y -= 0.08 * cheek

    v.co = Vector((x, y, z))

# UV Mapping for Cranium
uv_layer = bm_head.loops.layers.uv.new("UVMap")
for face in bm_head.faces:
    for loop in face.loops:
        norm = loop.vert.co.normalized()
        u = math.atan2(norm.x, -norm.y) / (2.0 * math.pi) + 0.5
        v_coord = norm.z * 0.5 + 0.5
        loop[uv_layer].uv = (u, v_coord)

head_mesh = bpy.data.meshes.new("Head")
head_obj = bpy.data.objects.new("Head", head_mesh)
head_obj.location = Vector((0, -0.08, 1.70))
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
# 4. Large Expressive Stylized Compound Eyes (Front-Projected UV Mapping)
# -----------------------------------------------------------------------------
def make_expressive_eye(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    eye_pos = Vector((sign * 0.19, -0.28, 1.71))
    
    # Base ellipsoid mesh
    bm_eye = bmesh.new()
    bmesh.ops.create_uvsphere(bm_eye, u_segments=36, v_segments=24, radius=1.0)
    
    # Scale: width 0.20, depth 0.18, height 0.245 (tall oval matching ref)
    rx, ry, rz = 0.20, 0.18, 0.245
    for v in bm_eye.verts:
        v.co.x *= rx
        v.co.y *= ry
        v.co.z *= rz

    # Front-projected UV mapping:
    # Visible front dome (-Y) receives the expressive iris and pupil!
    # Left eye: iris is shifted inward (medial is +X for Left eye)
    # Right eye: mirrored so both look slightly cross-eyed/inward
    uv_l = bm_eye.loops.layers.uv.new("UVMap")
    for face in bm_eye.faces:
        for loop in face.loops:
            vx = loop.vert.co.x
            vy = loop.vert.co.y
            vz = loop.vert.co.z
            
            # Map front hemisphere to UV center
            # In local space: front is -Y.
            u = 0.5 - (sign * vx) / (2.0 * rx * 1.15)
            v = 0.5 + vz / (2.0 * rz * 1.15)
            
            # If vertex is on back hemisphere inside the skull socket, push to dark margin
            if vy > 0.04:
                u = 0.05
                v = 0.05
                
            loop[uv_l].uv = (min(1.0, max(0.0, u)), min(1.0, max(0.0, v)))

    mesh = bpy.data.meshes.new(name)
    bm_eye.to_mesh(mesh)
    bm_eye.free()
    
    eye_obj = bpy.data.objects.new(name, mesh)
    eye_obj.location = eye_pos
    bpy.context.scene.collection.objects.link(eye_obj)
    
    # Rotation: Inward tilt at the top by 14 degrees, tilted slightly forward
    tilt_z = sign * math.radians(14.0)
    pitch_x = math.radians(4.0)
    eye_obj.rotation_euler = Euler((pitch_x, 0.0, tilt_z), 'XYZ')
    
    eye_obj.data.materials.append(mat_eye)
    sub = eye_obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 1
    for p in mesh.polygons:
        p.use_smooth = True
    return reg(eye_obj)

make_expressive_eye("Eye_L", True)
make_expressive_eye("Eye_R", False)

# -----------------------------------------------------------------------------
# 5. Bold Sculpted Pincer Mandibles with Interlocking Sharp Teeth
# -----------------------------------------------------------------------------
def make_sculpted_mandible(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    bm = bmesh.new()
    
    # 5 Anatomical Stations along the curved mandible claw (matching ref_head_crop.png)
    stations = [
        # Center, Tangent Normal, Width Rx, Height Rz
        (Vector((sign * 0.18, -0.22, 1.54)), Vector((sign * 0.35, -0.90, -0.15)).normalized(), 0.115, 0.120), # Cheek root directly below eye
        (Vector((sign * 0.29, -0.34, 1.50)), Vector((sign * 0.15, -0.92, -0.22)).normalized(), 0.148, 0.138), # Broad bulbous cheek jowl
        (Vector((sign * 0.24, -0.48, 1.46)), Vector((sign * -0.55, -0.70, -0.28)).normalized(), 0.138, 0.126), # Anterior apex (reaching forward Y = -0.48)
        (Vector((sign * 0.12, -0.48, 1.42)), Vector((sign * -0.90, -0.25, -0.18)).normalized(), 0.106, 0.098), # Inward hook
        (Vector((sign * 0.04, -0.44, 1.40)), Vector((sign * -1.0, 0.0, 0.0)).normalized(), 0.038, 0.038)        # Pincer tip meeting midline
    ]
    
    num_pts = 8
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
            
            # Puffed bulbous outer lateral side, flatter medial bite surface
            rx_eff = rx * (1.25 if (cos_t * sign > 0) else 0.80)
            rz_eff = rz * (0.92 if sin_t < 0 else 1.06)
            
            p_local = (right * (cos_t * rx_eff)) + (up * (sin_t * rz_eff))
            world_p = center + p_local
            c_ring.append(bm.verts.new(world_p))
        rings.append(c_ring)
        
    for i in range(len(stations) - 1):
        r0 = rings[i]
        r1 = rings[i + 1]
        for j in range(num_pts):
            jn = (j + 1) % num_pts
            bm.faces.new([r0[j], r0[jn], r1[jn], r1[j]])
            
    bm.faces.new(rings[0][::-1])
    bm.faces.new(rings[-1])
    
    # 2 Sharp Interlocking Fangs on the medial bite edge
    # Primary large fang (pointing inward towards midline)
    t1_base1 = rings[2][num_pts // 2]
    t1_base2 = rings[2][(num_pts // 2 + 1) % num_pts]
    t1_b3 = rings[3][num_pts // 2]
    t1_b4 = rings[3][(num_pts // 2 + 1) % num_pts]
    
    z_offset = 0.015 if is_left else -0.015
    v_t1 = bm.verts.new(Vector((sign * 0.022, -0.45, 1.44 + z_offset)))
    
    bm.faces.new([t1_base1, v_t1, t1_b3])
    bm.faces.new([t1_base2, t1_b4, v_t1])
    bm.faces.new([t1_base1, t1_base2, v_t1])
    bm.faces.new([t1_b3, v_t1, t1_b4])
    
    # Secondary sharp fang behind primary
    t2_base1 = rings[1][num_pts // 2]
    t2_base2 = rings[1][(num_pts // 2 + 1) % num_pts]
    v_t2 = bm.verts.new(Vector((sign * 0.10, -0.40, 1.41 + z_offset)))
    bm.faces.new([t2_base1, v_t2, t1_base1])
    bm.faces.new([t2_base2, t1_base2, v_t2])
    bm.faces.new([t2_base1, t2_base2, v_t2])
    
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    
    # UV Mapping: Cheek base is green (V=0), tip and teeth are chartreuse/pale lime (V=1.0)
    uv_l = bm.loops.layers.uv.new("UVMap")
    for face in bm.faces:
        for loop in face.loops:
            vy = loop.vert.co.y
            v_val = min(1.0, max(0.0, (-0.22 - vy) / 0.24))
            u_val = loop.vert.co.x * 2.0 + 0.5
            loop[uv_l].uv = (u_val, v_val)

    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.data.materials.append(mat_mandible)
    sub = obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 2
    for p in mesh.polygons:
        p.use_smooth = True
    return reg(obj)

make_sculpted_mandible("Mandible_L", True)
make_sculpted_mandible("Mandible_R", False)

# -----------------------------------------------------------------------------
# 6. Smooth Curved Bézier Antennae (Outward V-shape '\ /' with Club Tips)
# -----------------------------------------------------------------------------
def make_authentic_antenna(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    
    curve_data = bpy.data.curves.new(name, 'CURVE')
    curve_data.dimensions = '3D'
    curve_data.bevel_depth = 0.026 # Substantial insect stalk thickness
    curve_data.bevel_resolution = 6
    curve_data.fill_mode = 'FULL'
    
    spline = curve_data.splines.new('BEZIER')
    spline.bezier_points.add(2) # 3 control points
    
    # Trajectory matching ref_head_crop.png:
    # Sprouts from crown, rises UP and OUT, bends forward and outward at top
    p0 = Vector((sign * 0.08, -0.06, 1.90)) # Crown root
    p1 = Vector((sign * 0.14, -0.09, 2.24)) # Mid stalk slanting outward
    p2 = Vector((sign * 0.24, -0.16, 2.42)) # Tip sweeping forward & outward
    
    pts = [p0, p1, p2]
    for i, p in enumerate(pts):
        bp = spline.bezier_points[i]
        bp.co = p
        bp.handle_left_type = 'AUTO'
        bp.handle_right_type = 'AUTO'
        
    ant_obj = bpy.data.objects.new(name, curve_data)
    bpy.context.scene.collection.objects.link(ant_obj)
    ant_obj.data.materials.append(mat_antenna)
    
    # Convert to mesh to weld smooth club tip
    bpy.context.view_layer.objects.active = ant_obj
    ant_obj.select_set(True)
    bpy.ops.object.convert(target='MESH')
    bpy.ops.object.shade_smooth()
    
    # Rounded teardrop club tip at p2
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=20, ring_count=16, radius=0.036,
        location=p2
    )
    club = bpy.context.active_object
    club.name = f"{name}_Club"
    club.data.materials.append(mat_antenna)
    bpy.ops.object.shade_smooth()
    reg(club)
    
    return reg(ant_obj)

make_authentic_antenna("Antenna_L", True)
make_authentic_antenna("Antenna_R", False)

# -----------------------------------------------------------------------------
# 7. Thorax: 3 Articulated Segment Plates & Waist
# -----------------------------------------------------------------------------
# Segmented Neck
bpy.ops.mesh.primitive_cylinder_add(
    vertices=24, radius=0.14, depth=0.12,
    location=(0, -0.04, 1.45), rotation=(math.radians(18), 0, 0)
)
neck = bpy.context.active_object
neck.name = "Neck"
neck.data.materials.append(mat_chitin)
bpy.ops.object.shade_smooth()
reg(neck)

# 3 Overlapping Armor Plates: Pronotum, Mesonotum, Metanotum
thorax_plates = [
    ("Thorax_Pronotum",  Vector((0, 0.02, 1.28)), Vector((0.27, 0.25, 0.24))),
    ("Thorax_Mesonotum", Vector((0, 0.14, 1.14)), Vector((0.29, 0.27, 0.26))),
    ("Thorax_Metanotum", Vector((0, 0.28, 1.00)), Vector((0.26, 0.25, 0.23)))
]
for name, loc, scale in thorax_plates:
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=20, radius=1.0, location=loc)
    p_obj = bpy.context.active_object
    p_obj.name = name
    p_obj.scale = scale
    p_obj.data.materials.append(mat_chitin)
    sub = p_obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 1
    bpy.ops.object.shade_smooth()
    reg(p_obj)

# Petiole Waist
bpy.ops.mesh.primitive_cylinder_add(
    vertices=16, radius=0.08, depth=0.16,
    location=(0, 0.38, 0.88), rotation=(math.radians(35), 0, 0)
)
petiole = bpy.context.active_object
petiole.name = "Petiole"
petiole.data.materials.append(mat_chitin)
bpy.ops.object.shade_smooth()
reg(petiole)

# -----------------------------------------------------------------------------
# 8. Gaster: Heroic Suspended Abdomen with Segment Grooves
# -----------------------------------------------------------------------------
bm_gaster = bmesh.new()
bmesh.ops.create_uvsphere(bm_gaster, u_segments=36, v_segments=24, radius=1.0)

for v in bm_gaster.verts:
    # Base dimensions: width 0.38, depth 0.50, height 0.44
    x = v.co.x * 0.38
    y = v.co.y * 0.50
    z = v.co.z * 0.44
    
    # Teardrop taper towards posterior stinger tip
    if y > 0:
        taper = 1.0 - 0.45 * (y / 0.50)
        x *= taper
        z *= (taper * 0.90)
    else:
        # Full rounded anterior belly
        x *= 1.08
        z *= 1.06
        
    # Segmental sternite rings
    groove = math.sin((y + 0.50) * 14.0) * 0.012
    x += groove * (x / 0.38)
    z += groove * (z / 0.44)
    
    v.co = Vector((x, y, z))

gaster_mesh = bpy.data.meshes.new("Gaster")
gaster_obj = bpy.data.objects.new("Gaster", gaster_mesh)
gaster_obj.location = Vector((0, 0.48, 0.72))
gaster_obj.rotation_euler = Euler((math.radians(-24), 0, 0), 'XYZ')
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
# 9. Anatomical Insect Limbs: Flared Sleeves, Articulated Hands, Planted Feet
# -----------------------------------------------------------------------------
def make_chitin_segment(name, p0, p1, r_start, r_mid, r_end, is_sleeve=False):
    bm = bmesh.new()
    vec = p1 - p0
    length = vec.length
    if length < 1e-4:
        bm.free()
        return None
        
    rot_quat = Vector((0, 0, 1)).rotation_difference(vec)
    mat_rot = rot_quat.to_matrix().to_4x4()
    
    num_pts = 12
    rings = []
    # 4 Profile stations: base knuckle, muscular belly, tapering shaft, flared sleeve collar
    stations = [
        (p0, r_start, 1.0, 1.0),
        (p0 + vec * 0.28, r_mid, 1.25, 0.85), # Flattened muscular cross section
        (p0 + vec * 0.72, r_end * 1.05, 1.12, 0.90),
        (p1, r_end * (1.25 if is_sleeve else 1.0), 1.15, 0.95) # Flared joint collar
    ]
    for pos, rad, sx, sy in stations:
        c_ring = []
        for j in range(num_pts):
            th = 2.0 * math.pi * j / num_pts
            local_p = Vector((rad * sx * math.cos(th), rad * sy * math.sin(th), 0.0))
            world_p = pos + (mat_rot @ local_p)
            c_ring.append(bm.verts.new(world_p))
        rings.append(c_ring)
        
    for i in range(len(stations) - 1):
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

# A. Front Expressive Arms with Articulated Hands (Gesturing character arms)
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"
    
    p_shoulder = Vector((sign * 0.16, 0.0, 1.32))
    p_elbow    = Vector((sign * 0.30, -0.06, 1.06))
    p_wrist    = Vector((sign * 0.26, -0.16, 0.80))
    p_finger1  = Vector((sign * 0.23, -0.22, 0.68))
    p_finger2  = Vector((sign * 0.28, -0.18, 0.66))
    
    make_chitin_segment(f"Arm_Upper_{suf}", p_shoulder, p_elbow, 0.048, 0.054, 0.038, is_sleeve=True)
    make_chitin_segment(f"Arm_Forearm_{suf}", p_elbow, p_wrist, 0.038, 0.040, 0.028, is_sleeve=True)
    make_chitin_segment(f"Arm_Finger1_{suf}", p_wrist, p_finger1, 0.022, 0.022, 0.012)
    make_chitin_segment(f"Arm_Finger2_{suf}", p_wrist, p_finger2, 0.020, 0.020, 0.010)

# B. Middle Walking Legs (Planted firmly on floor Z = 0)
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"
    
    p_hip   = Vector((sign * 0.18, 0.10, 1.15))
    p_knee  = Vector((sign * 0.34, -0.08, 0.68))
    p_ankle = Vector((sign * 0.30, -0.16, 0.09))
    p_foot  = Vector((sign * 0.28, -0.25, 0.0))
    
    make_chitin_segment(f"Leg_Mid_Femur_{suf}", p_hip, p_knee, 0.052, 0.062, 0.042, is_sleeve=True)
    make_chitin_segment(f"Leg_Mid_Tibia_{suf}", p_knee, p_ankle, 0.040, 0.038, 0.026, is_sleeve=True)
    make_chitin_segment(f"Leg_Mid_Foot_{suf}", p_ankle, p_foot, 0.025, 0.022, 0.014)

# C. Hind Walking Legs (High knees, wide planted stance at Z = 0)
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"
    
    p_hip   = Vector((sign * 0.16, 0.26, 1.04))
    p_knee  = Vector((sign * 0.54, 0.28, 1.00))
    p_ankle = Vector((sign * 0.58, 0.44, 0.10))
    p_foot  = Vector((sign * 0.64, 0.56, 0.0))
    
    make_chitin_segment(f"Leg_Hind_Femur_{suf}", p_hip, p_knee, 0.054, 0.064, 0.044, is_sleeve=True)
    make_chitin_segment(f"Leg_Hind_Tibia_{suf}", p_knee, p_ankle, 0.042, 0.038, 0.026, is_sleeve=True)
    make_chitin_segment(f"Leg_Hind_Foot_{suf}", p_ankle, p_foot, 0.025, 0.022, 0.014)

# -----------------------------------------------------------------------------
# 10. Studio Ground Plane (Subtle Dark Floor Reflection)
# -----------------------------------------------------------------------------
bpy.ops.mesh.primitive_plane_add(size=12.0, location=(0, 0, 0))
floor = bpy.context.active_object
floor.name = "Studio_Floor"
mat_floor = bpy.data.materials.new("M_Studio_Floor")
mat_floor.use_nodes = True
nf = mat_floor.node_tree.nodes
lf = mat_floor.node_tree.links
nf.clear()
out_f = nf.new('ShaderNodeOutputMaterial')
bsdf_f = nf.new('ShaderNodeBsdfPrincipled')
lf.new(bsdf_f.outputs['BSDF'], out_f.inputs['Surface'])
bsdf_f.inputs['Base Color'].default_value = (0.01, 0.01, 0.012, 1.0)
bsdf_f.inputs['Roughness'].default_value = 0.32
bsdf_f.inputs['Coat Weight'].default_value = 0.60
floor.data.materials.append(mat_floor)

# -----------------------------------------------------------------------------
# 11. High-Impact Studio 4-Point Lighting Rig
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

# Key Light: Warm soft light at 10 o'clock matching catchlight
add_light("Key_Light", 'POINT', 380.0, (1.0, 0.98, 0.94), (-2.2, -3.2, 3.6))
# Fill Light: Soft cool fill on right
add_light("Fill_Light", 'POINT', 160.0, (0.86, 0.92, 1.0), (2.6, -2.2, 2.2))
# Rim Light: Powerful backlight for crisp insect silhouette edge
add_light("Rim_Light_Top", 'POINT', 460.0, (1.0, 1.0, 1.0), (0.0, 3.2, 3.8))
# Side Rim: Accents legs and cheeks
add_light("Rim_Light_Side", 'POINT', 220.0, (0.95, 1.0, 0.9), (-2.8, 1.5, 1.8))

# -----------------------------------------------------------------------------
# 12. Multi-Angle Cameras & Render Stills (Full Headroom & Authentic Stature)
# -----------------------------------------------------------------------------
cam_data = bpy.data.cameras.new("RenderCam")
cam_data.lens = 65.0 # Cinematic character portrait lens
cam_obj = bpy.data.objects.new("RenderCam", cam_data)
bpy.context.scene.collection.objects.link(cam_obj)
scene.camera = cam_obj

views = [
    (
        "worker_front.png",
        Vector((0.0, -4.6, 1.25)),
        Euler((math.radians(88), 0, 0), 'XYZ'),
        "Front Authentic Heroic Stance (Full Antennae Framing)"
    ),
    (
        "worker_perspective.png",
        Vector((-3.0, -3.5, 1.50)),
        Euler((math.radians(82), 0, math.radians(-40)), 'XYZ'),
        "3/4 Depth Perspective"
    ),
    (
        "worker_face_closeup.png",
        Vector((0.0, -2.3, 1.62)),
        Euler((math.radians(88), 0, 0), 'XYZ'),
        "Face, Eyes & Muzzle Macro Close-up"
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
# 13. GLTF 2.0 Binary Export (worker_ant.glb)
# -----------------------------------------------------------------------------
glb_path = os.path.join(web_dir, "worker_ant.glb")
print(f"Exporting Worker Ant Authentic 3D GLB to: {glb_path}...")

bpy.ops.object.select_all(action='DESELECT')
# Select ant character objects only (exclude lights and floor for clean game asset)
for obj in worker_col.objects:
    obj.select_set(True)

bpy.ops.export_scene.gltf(
    filepath=glb_path,
    export_format='GLB',
    use_selection=True,
    export_apply=True
)

print("Worker Ant Authentic 3D GLB export complete!")

usdz_path = os.path.join(web_dir, "worker_ant.usdz")
print(f"Exporting Worker Ant USDZ to: {usdz_path}...")
bpy.ops.wm.usd_export(
    filepath=usdz_path,
    selected_objects_only=True,
    export_textures=True
)
print("Worker Ant USDZ export complete!")

blend_path = "/Users/dchadd/Desktop/Ants-Mac/tools/blender/worker_ant_authentic.blend"
bpy.ops.wm.save_as_mainfile(filepath=blend_path)
print(f"Saved Blender file: {blend_path}")
