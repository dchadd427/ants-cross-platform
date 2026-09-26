"""
Blender 4.3.2 Script: Authentic Heroic Fire Ant (Caste #2: Mason / af)
Calibrated 1:1 to 2D master concept (fire_ant_master_reference.jpg) & 1998 sprites (afst301):
1. OVERSIZED FIRE CHIEF HELMET: Classic vintage Cairns-style golden-yellow helmet with
   a high domed crown, central comb ridge, wide flared duckbill brim dipping low over
   the brow and neck ("child wearing an adult's hat"), and front shield with bold RED 'A'.
2. PROMINENT SHIELD PLAQUE & 3D EMBOSSED BOLD RED 'A': Saturated scarlet red letter 'A' badge
   standing proud on the front gold shield plaque with zero specular washout.
3. SLATE-VIOLET CRANIUM: Smooth organic head in iconic slate-violet chitin (#4D4556)
   nestled under the golden helmet, matching 1998 sprite afst301.
4. NO ANTENNAE: Antennae are completely tucked inside/under the helmet with zero protrusion.
5. EMPTY HANDS: Front arms held alertly in front of the chest framing the body.
6. EYES & HEAD: Determined cartoon eyes with crisp white sclera and warm amber irises,
   with forward-facing binocular UV projection; chubby cheek lobes and interlocking sharp mandibles.
7. CHITIN & CARAPACE: Rich charcoal-plum and burnt-sienna cuticle with warm amber sheen.
8. CONTINUOUS PETIOLE WAIST: Gap-free articulated sleeve bridging Metanotum to Gaster.
9. COMPACT LEG STANCE: Inward knees and planted feet at Z = 0 matching master reference.
10. RIGGING & WALK ANIMATION: Full armature with Walk & Idle keyframe cycles.
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
scene.render.film_transparent = False
scene.render.resolution_x = 1080
scene.render.resolution_y = 1080

web_dir = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d"
tex_dir = os.path.join(web_dir, "textures_fire")

# Dark studio world environment matching master reference
world = bpy.data.worlds.new("StudioWorld")
scene.world = world
world.use_nodes = True
bg_node = world.node_tree.nodes['Background']
bg_node.inputs['Color'].default_value = (0.008, 0.008, 0.010, 1.0)
bg_node.inputs['Strength'].default_value = 0.20

# -----------------------------------------------------------------------------
# 2. Fire Ant PBR Materials
# -----------------------------------------------------------------------------
def create_materials():
    # A. Body Chitin Material (Canonical Moss-Green Chitin)
    mat_chitin = bpy.data.materials.new("M_Fire_Chitin")
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
    bsdf.inputs['Base Color'].default_value = (0.24, 0.36, 0.16, 1.0)

    # Micro-bump
    tex_coord = nodes.new('ShaderNodeTexCoord')
    noise_bump = nodes.new('ShaderNodeTexNoise')
    noise_bump.inputs['Scale'].default_value = 95.0
    noise_bump.inputs['Detail'].default_value = 5.0
    noise_bump.inputs['Roughness'].default_value = 0.55
    links.new(tex_coord.outputs['Object'], noise_bump.inputs['Vector'])

    bump = nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.22
    bump.inputs['Distance'].default_value = 0.003
    links.new(noise_bump.outputs['Fac'], bump.inputs['Height'])
    links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])

    bsdf.inputs['Roughness'].default_value = 0.50
    bsdf.inputs['Coat Weight'].default_value = 0.24
    bsdf.inputs['Coat Roughness'].default_value = 0.22
    bsdf.inputs['Subsurface Weight'].default_value = 0.09
    bsdf.inputs['Subsurface Radius'].default_value = (0.14, 0.18, 0.09)

    # B. Head Material (Canonical Moss-Green Chitin)
    mat_head = bpy.data.materials.new("M_Fire_Head")
    mat_head.use_nodes = True
    nodes_hd = mat_head.node_tree.nodes
    links_hd = mat_head.node_tree.links
    nodes_hd.clear()

    out_hd = nodes_hd.new('ShaderNodeOutputMaterial')
    bsdf_hd = nodes_hd.new('ShaderNodeBsdfPrincipled')
    links_hd.new(bsdf_hd.outputs['BSDF'], out_hd.inputs['Surface'])

    tex_head = nodes_hd.new('ShaderNodeTexImage')
    tex_head.image = bpy.data.images.load(f"{web_dir}/chitin_pbr.png")
    links_hd.new(tex_head.outputs['Color'], bsdf_hd.inputs['Base Color'])
    bsdf_hd.inputs['Base Color'].default_value = (0.24, 0.36, 0.16, 1.0)

    tc_hd = nodes_hd.new('ShaderNodeTexCoord')
    noise_hd = nodes_hd.new('ShaderNodeTexNoise')
    noise_hd.inputs['Scale'].default_value = 95.0
    noise_hd.inputs['Detail'].default_value = 5.0
    noise_hd.inputs['Roughness'].default_value = 0.55
    links_hd.new(tc_hd.outputs['Object'], noise_hd.inputs['Vector'])

    bump_hd = nodes_hd.new('ShaderNodeBump')
    bump_hd.inputs['Strength'].default_value = 0.20
    bump_hd.inputs['Distance'].default_value = 0.003
    links_hd.new(noise_hd.outputs['Fac'], bump_hd.inputs['Height'])
    links_hd.new(bump_hd.outputs['Normal'], bsdf_hd.inputs['Normal'])

    bsdf_hd.inputs['Roughness'].default_value = 0.50
    bsdf_hd.inputs['Coat Weight'].default_value = 0.24
    bsdf_hd.inputs['Coat Roughness'].default_value = 0.22
    bsdf_hd.inputs['Subsurface Weight'].default_value = 0.09
    bsdf_hd.inputs['Subsurface Radius'].default_value = (0.14, 0.18, 0.09)

    # C. Eye Material
    mat_eye = bpy.data.materials.new("M_Fire_Eye")
    mat_eye.use_nodes = True
    nodes_e = mat_eye.node_tree.nodes
    links_e = mat_eye.node_tree.links
    nodes_e.clear()

    out_e = nodes_e.new('ShaderNodeOutputMaterial')
    bsdf_e = nodes_e.new('ShaderNodeBsdfPrincipled')
    links_e.new(bsdf_e.outputs['BSDF'], out_e.inputs['Surface'])

    tex_eye = nodes_e.new('ShaderNodeTexImage')
    tex_eye.image = bpy.data.images.load(f"{tex_dir}/fire_eye_pbr.png")
    links_e.new(tex_eye.outputs['Color'], bsdf_e.inputs['Base Color'])
    bsdf_e.inputs['Base Color'].default_value = (1.0, 1.0, 1.0, 1.0)

    bsdf_e.inputs['Roughness'].default_value = 0.08
    bsdf_e.inputs['Coat Weight'].default_value = 0.95
    bsdf_e.inputs['Coat Roughness'].default_value = 0.02
    bsdf_e.inputs['IOR'].default_value = 1.48

    # D. Mandible Material (Moss green blending to chartreuse scoop)
    mat_mandible = bpy.data.materials.new("M_Fire_Mandible")
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
    bsdf_m.inputs['Coat Weight'].default_value = 0.30
    bsdf_m.inputs['Coat Roughness'].default_value = 0.15
    bsdf_m.inputs['Subsurface Weight'].default_value = 0.12
    bsdf_m.inputs['Subsurface Radius'].default_value = (0.25, 0.40, 0.15)

    # E. Limbs Material (Weathered mahogany / amber chitin matching Worker)
    mat_limbs = bpy.data.materials.new("M_Fire_Limbs")
    mat_limbs.use_nodes = True
    nodes_l = mat_limbs.node_tree.nodes
    links_l = mat_limbs.node_tree.links
    nodes_l.clear()

    out_l = nodes_l.new('ShaderNodeOutputMaterial')
    bsdf_l = nodes_l.new('ShaderNodeBsdfPrincipled')
    links_l.new(bsdf_l.outputs['BSDF'], out_l.inputs['Surface'])

    tex_limb = nodes_l.new('ShaderNodeTexImage')
    tex_limb.image = bpy.data.images.load(f"{web_dir}/limbs_pbr.png")
    links_l.new(tex_limb.outputs['Color'], bsdf_l.inputs['Base Color'])
    bsdf_l.inputs['Base Color'].default_value = (0.48, 0.22, 0.16, 1.0)

    tc_l = nodes_l.new('ShaderNodeTexCoord')
    noise_l = nodes_l.new('ShaderNodeTexNoise')
    noise_l.inputs['Scale'].default_value = 75.0
    noise_l.inputs['Detail'].default_value = 4.0
    noise_l.inputs['Roughness'].default_value = 0.52
    links_l.new(tc_l.outputs['Object'], noise_l.inputs['Vector'])

    bump_l = nodes_l.new('ShaderNodeBump')
    bump_l.inputs['Strength'].default_value = 0.12
    bump_l.inputs['Distance'].default_value = 0.002
    links_l.new(noise_l.outputs['Fac'], bump_l.inputs['Height'])
    links_l.new(bump_l.outputs['Normal'], bsdf_l.inputs['Normal'])

    bsdf_l.inputs['Roughness'].default_value = 0.44
    bsdf_l.inputs['Coat Weight'].default_value = 0.35
    bsdf_l.inputs['Coat Roughness'].default_value = 0.22
    bsdf_l.inputs['Subsurface Weight'].default_value = 0.06
    bsdf_l.inputs['Subsurface Radius'].default_value = (0.18, 0.08, 0.05)

    # F. Teeth Material (Sharp bone-ivory white)
    mat_teeth = bpy.data.materials.new("M_Fire_Teeth")
    mat_teeth.use_nodes = True
    nodes_t = mat_teeth.node_tree.nodes
    links_t = mat_teeth.node_tree.links
    nodes_t.clear()

    out_t = nodes_t.new('ShaderNodeOutputMaterial')
    bsdf_t = nodes_t.new('ShaderNodeBsdfPrincipled')
    links_t.new(bsdf_t.outputs['BSDF'], out_t.inputs['Surface'])

    bsdf_t.inputs['Base Color'].default_value = (0.94, 0.95, 0.88, 1.0)
    bsdf_t.inputs['Roughness'].default_value = 0.25
    bsdf_t.inputs['Coat Weight'].default_value = 0.40
    bsdf_t.inputs['Coat Roughness'].default_value = 0.15

    # G. Fire Chief Helmet Material (Rich warm golden yellow lacquer)
    mat_helmet = bpy.data.materials.new("M_Fire_Helmet")
    mat_helmet.use_nodes = True
    nodes_h = mat_helmet.node_tree.nodes
    links_h = mat_helmet.node_tree.links
    nodes_h.clear()

    out_h = nodes_h.new('ShaderNodeOutputMaterial')
    bsdf_h = nodes_h.new('ShaderNodeBsdfPrincipled')
    links_h.new(bsdf_h.outputs['BSDF'], out_h.inputs['Surface'])

    tex_helm = nodes_h.new('ShaderNodeTexImage')
    tex_helm.image = bpy.data.images.load(f"{tex_dir}/fire_helmet_pbr.png")
    links_h.new(tex_helm.outputs['Color'], bsdf_h.inputs['Base Color'])
    bsdf_h.inputs['Base Color'].default_value = (0.96, 0.72, 0.10, 1.0)

    bsdf_h.inputs['Roughness'].default_value = 0.20
    bsdf_h.inputs['Coat Weight'].default_value = 0.88
    bsdf_h.inputs['Coat Roughness'].default_value = 0.08
    bsdf_h.inputs['Metallic'].default_value = 0.04

    # H. Shield Plaque Material (Rich antique gold with darker bevel)
    mat_shield = bpy.data.materials.new("M_Fire_Shield")
    mat_shield.use_nodes = True
    nodes_s = mat_shield.node_tree.nodes
    links_s = mat_shield.node_tree.links
    nodes_s.clear()

    out_s = nodes_s.new('ShaderNodeOutputMaterial')
    bsdf_s = nodes_s.new('ShaderNodeBsdfPrincipled')
    links_s.new(bsdf_s.outputs['BSDF'], out_s.inputs['Surface'])

    # Burnished brass/gold shield plaque
    bsdf_s.inputs['Base Color'].default_value = (0.65, 0.42, 0.06, 1.0)
    bsdf_s.inputs['Metallic'].default_value = 0.65
    bsdf_s.inputs['Roughness'].default_value = 0.22
    bsdf_s.inputs['Coat Weight'].default_value = 0.50
    bsdf_s.inputs['Coat Roughness'].default_value = 0.12

    # I. Bold Embossed Saturated Scarlet Red 'A' Material
    mat_red_a = bpy.data.materials.new("M_Fire_Red_A")
    mat_red_a.use_nodes = True
    nodes_ra = mat_red_a.node_tree.nodes
    links_ra = mat_red_a.node_tree.links
    nodes_ra.clear()

    out_ra = nodes_ra.new('ShaderNodeOutputMaterial')
    bsdf_ra = nodes_ra.new('ShaderNodeBsdfPrincipled')
    links_ra.new(bsdf_ra.outputs['BSDF'], out_ra.inputs['Surface'])

    # Deep vibrant fire-engine scarlet red (controlled linear value to prevent AgX blowout)
    bsdf_ra.inputs['Base Color'].default_value = (0.50, 0.005, 0.005, 1.0)
    bsdf_ra.inputs['Roughness'].default_value = 0.42
    bsdf_ra.inputs['Coat Weight'].default_value = 0.0
    bsdf_ra.inputs['Coat Roughness'].default_value = 0.50
    if 'Specular IOR Level' in bsdf_ra.inputs:
        bsdf_ra.inputs['Specular IOR Level'].default_value = 0.12

    # J. Helmet Trim / Chin Strap Material (Black leather)
    mat_trim = bpy.data.materials.new("M_Fire_Trim")
    mat_trim.use_nodes = True
    nodes_tr = mat_trim.node_tree.nodes
    links_tr = mat_trim.node_tree.links
    nodes_tr.clear()

    out_tr = nodes_tr.new('ShaderNodeOutputMaterial')
    bsdf_tr = nodes_tr.new('ShaderNodeBsdfPrincipled')
    links_tr.new(bsdf_tr.outputs['BSDF'], out_tr.inputs['Surface'])

    bsdf_tr.inputs['Base Color'].default_value = (0.08, 0.07, 0.09, 1.0)
    bsdf_tr.inputs['Roughness'].default_value = 0.42
    bsdf_tr.inputs['Coat Weight'].default_value = 0.30

    # K. Recessed Oral Cavity Material (Velvety shadow void behind mandibles)
    mat_oral = bpy.data.materials.new("M_Fire_Oral")
    mat_oral.use_nodes = True
    nodes_o = mat_oral.node_tree.nodes
    links_o = mat_oral.node_tree.links
    nodes_o.clear()

    out_o = nodes_o.new('ShaderNodeOutputMaterial')
    bsdf_o = nodes_o.new('ShaderNodeBsdfPrincipled')
    links_o.new(bsdf_o.outputs['BSDF'], out_o.inputs['Surface'])

    bsdf_o.inputs['Base Color'].default_value = (0.012, 0.008, 0.015, 1.0)
    bsdf_o.inputs['Roughness'].default_value = 0.90
    if 'Specular IOR Level' in bsdf_o.inputs:
        bsdf_o.inputs['Specular IOR Level'].default_value = 0.05

    return (mat_chitin, mat_head, mat_eye, mat_mandible, mat_limbs, mat_teeth,
            mat_helmet, mat_shield, mat_red_a, mat_trim, mat_oral)

(mat_chitin, mat_head, mat_eye, mat_mandible, mat_limbs, mat_teeth,
 mat_helmet, mat_shield, mat_red_a, mat_trim, mat_oral) = create_materials()

fire_col = bpy.data.collections.new("Fire_Ant_Authentic")
bpy.context.scene.collection.children.link(fire_col)

def reg(obj):
    fire_col.objects.link(obj)
    if obj.name in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.unlink(obj)
    return obj

# -----------------------------------------------------------------------------
# 3. Studio Stage Lighting & Reflective Ground
# -----------------------------------------------------------------------------
bpy.ops.mesh.primitive_plane_add(size=24.0, location=(0, 0, 0))
floor_obj = bpy.context.active_object
floor_obj.name = "Studio_Floor"
mat_floor = bpy.data.materials.new("M_Studio_Floor")
mat_floor.use_nodes = True
f_bsdf = mat_floor.node_tree.nodes['Principled BSDF']
f_bsdf.inputs['Base Color'].default_value = (0.012, 0.012, 0.015, 1.0)
f_bsdf.inputs['Roughness'].default_value = 0.08
f_bsdf.inputs['Metallic'].default_value = 0.35
floor_obj.data.materials.append(mat_floor)

# Key Light: Warm golden sun key
light_key_data = bpy.data.lights.new(name="Key_Light", type='AREA')
light_key_data.energy = 420.0
light_key_data.color = (1.0, 0.94, 0.84)
light_key_data.size = 2.4
light_key = bpy.data.objects.new(name="Key_Light", object_data=light_key_data)
light_key.location = (-1.8, -3.0, 3.2)
light_key.rotation_euler = (math.radians(45), 0, math.radians(-30))
bpy.context.scene.collection.objects.link(light_key)

# Fill Light: Soft cool slate fill
light_fill_data = bpy.data.lights.new(name="Fill_Light", type='AREA')
light_fill_data.energy = 180.0
light_fill_data.color = (0.86, 0.90, 1.0)
light_fill_data.size = 3.2
light_fill = bpy.data.objects.new(name="Fill_Light", object_data=light_fill_data)
light_fill.location = (2.4, -2.4, 2.2)
light_fill.rotation_euler = (math.radians(50), 0, math.radians(40))
bpy.context.scene.collection.objects.link(light_fill)

# Rim Light: Fiery amber rim framing helmet crest & carapace
light_rim_data = bpy.data.lights.new(name="Rim_Light", type='AREA')
light_rim_data.energy = 450.0
light_rim_data.color = (1.0, 0.72, 0.35)
light_rim_data.size = 2.6
light_rim = bpy.data.objects.new(name="Rim_Light", object_data=light_rim_data)
light_rim.location = (0.0, 2.8, 2.8)
light_rim.rotation_euler = (math.radians(-40), 0, 0)
bpy.context.scene.collection.objects.link(light_rim)

# -----------------------------------------------------------------------------
# 4. Organic Cranium with Orbital Sockets (Nestled Under Helmet)
# -----------------------------------------------------------------------------
bm_head = bmesh.new()
bmesh.ops.create_cube(bm_head, size=1.0)
bmesh.ops.subdivide_edges(bm_head, edges=bm_head.edges, cuts=8, use_grid_fill=True)

rx_head, ry_head, rz_head = 0.31, 0.25, 0.36

for v in bm_head.verts:
    vx, vy, vz = v.co.x, v.co.y, v.co.z
    p_exp = 3.2
    r_super = (abs(vx)**p_exp + abs(vy)**p_exp + abs(vz)**p_exp)**(1.0 / p_exp)
    if r_super > 1e-5:
        nx, ny, nz = vx / r_super, vy / r_super, vz / r_super
    else:
        nx, ny, nz = 0.0, 0.0, 0.0

    x = nx * rx_head
    y = ny * ry_head
    z = nz * rz_head

    if z > 0.06:
        cleft = 1.0 - 0.12 * math.exp(-((x / 0.08) ** 2))
        z *= cleft
        x *= (1.0 + 0.06 * (z / rz_head))

    if 0.03 < z < 0.24 and y < 0:
        brow_t = math.sin((z - 0.03) / 0.21 * math.pi)
        y -= 0.035 * brow_t

    # Central vertical median nose bridge between eyes down to clypeus
    if abs(x) < 0.06 and -0.06 < z < 0.20:
        ridge = math.exp(-((x / 0.05)**2)) * math.sin(max(0, (z + 0.06) / 0.26 * math.pi))
        y -= 0.040 * ridge

    for sign_x in [-1.0, 1.0]:
        e_ox = sign_x * 0.160
        e_oy = -0.165
        e_oz = 0.060
        d_orbit = math.sqrt(((x - e_ox) / 0.145)**2 + ((y - e_oy) / 0.12)**2 + ((z - e_oz) / 0.155)**2)
        if d_orbit < 1.0:
            cavit_t = (1.0 - d_orbit) ** 2
            y += 0.085 * cavit_t
            x -= 0.028 * sign_x * cavit_t
            z -= 0.022 * cavit_t

    if z < 0.02 and y < 0:
        cheeks = math.exp(-((z + 0.14) / 0.16)**2)
        if abs(x) > 0.06:
            x += math.copysign(0.045 * cheeks, x)
            y -= 0.055 * cheeks
        else:
            y -= 0.048 * math.exp(-((x / 0.06)**2)) * math.exp(-((z + 0.08) / 0.12)**2)

    # Carve away awkward lower chin below mouthparts so jaws form the true bottom of the head
    if z < -0.05 and y < 0.05:
        t_cut = min(1.0, (-z - 0.05) / 0.20)
        y += 0.14 * t_cut
        z += 0.08 * t_cut
        x *= (1.0 - 0.25 * t_cut)
    elif z < -0.12:
        t_neck = min(1.0, (-z - 0.12) / 0.18)
        x *= (1.0 - 0.40 * t_neck)
        y *= (1.0 - 0.35 * t_neck)

    v.co = Vector((x, y, z))

head_mesh = bpy.data.meshes.new("HeadMesh")
bm_head.to_mesh(head_mesh)
bm_head.free()

for p in head_mesh.polygons:
    p.use_smooth = True

head_obj = bpy.data.objects.new("Head", head_mesh)
head_obj.location = (0, -0.04, 1.58)
head_obj.data.materials.append(mat_head)
sub_h = head_obj.modifiers.new("Subsurf", 'SUBSURF')
sub_h.levels = 2
reg(head_obj)

# -----------------------------------------------------------------------------
# 5. Forward-Facing Cartoon Eyes with Binocular UV Mapping
# -----------------------------------------------------------------------------
def make_bulging_eye(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    eye_pos = Vector((sign * 0.160, -0.21, 1.635))

    bm_eye = bmesh.new()
    bmesh.ops.create_uvsphere(bm_eye, u_segments=40, v_segments=28, radius=1.0)

    rx, ry, rz = 0.128, 0.118, 0.158
    for v in bm_eye.verts:
        v.co.x *= rx
        v.co.y *= ry
        v.co.z *= rz

    uv_l = bm_eye.loops.layers.uv.new("UVMap")
    for face in bm_eye.faces:
        for loop in face.loops:
            vx = loop.vert.co.x
            vy = loop.vert.co.y
            vz = loop.vert.co.z

            u = 0.50 - (sign * vx) / (2.0 * rx * 1.15) - (0.015 * sign)
            v = 0.50 + vz / (2.0 * rz * 1.15) + 0.020

            if vy > 0.03:
                u = 0.50
                v = 0.95

            loop[uv_l].uv = (min(1.0, max(0.0, u)), min(1.0, max(0.0, v)))

    mesh = bpy.data.meshes.new(name)
    bm_eye.to_mesh(mesh)
    bm_eye.free()

    for p in mesh.polygons:
        p.use_smooth = True

    eye_obj = bpy.data.objects.new(name, mesh)
    eye_obj.location = eye_pos
    bpy.context.scene.collection.objects.link(eye_obj)

    tilt_z = sign * math.radians(3.5)
    pitch_x = math.radians(2.5)
    eye_obj.rotation_euler = Euler((pitch_x, 0.0, tilt_z), 'XYZ')

    eye_obj.data.materials.append(mat_eye)
    sub = eye_obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 1
    return reg(eye_obj)

make_bulging_eye("Eye_L", True)
make_bulging_eye("Eye_R", False)

# Fleshy upper eyelid hoods
for is_left in [True, False]:
    suf = "L" if is_left else "R"
    sign_x = -1.0 if is_left else 1.0
    bm_lid = bmesh.new()
    bmesh.ops.create_uvsphere(bm_lid, u_segments=24, v_segments=16, radius=0.134)
    for v in list(bm_lid.verts):
        if v.co.z < 0.015 or v.co.y > 0.02:
            bm_lid.verts.remove(v)
    for v in bm_lid.verts:
        v.co.x *= 1.02
        v.co.y *= 1.08
        v.co.z *= 0.94
    lid_mesh = bpy.data.meshes.new(f"Eyelid_{suf}_Mesh")
    bm_lid.to_mesh(lid_mesh)
    bm_lid.free()

    for p in lid_mesh.polygons:
        p.use_smooth = True

    lid_obj = bpy.data.objects.new(f"Eyelid_{suf}", lid_mesh)
    lid_obj.location = (sign_x * 0.160, -0.212, 1.640)
    lid_obj.rotation_euler = (math.radians(-4), math.radians(sign_x * 8), 0)
    lid_obj.data.materials.append(mat_head)
    lid_sub = lid_obj.modifiers.new("Subsurf", 'SUBSURF')
    lid_sub.levels = 1
    reg(lid_obj)

# -----------------------------------------------------------------------------
# 6. Authentic 3D Caliper Pincer Claws & Recessed Oral Cavity
# -----------------------------------------------------------------------------
def make_authentic_pincer_claw(name, is_left=True):
    sign = -1.0 if is_left else 1.0
    bm = bmesh.new()

    # True caliper pincer jaw path:
    # 0. Cheek hinge condyle (world Z=1.45)
    # 1. Broad lateral caliper bow flaring out as wide as cheek
    # 2. Massive anterior bulbous muscle lobe (sweeping forward and down)
    # 3. Anterior medial turn with deep inner bite notch
    # 4. Inward-hooking caliper tip (leaving ~0.130 unit open central mouth gap)
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
    reg(mand_obj)

    # Sculpted sharp biting fangs inside the inner scoop
    suf = "L" if is_left else "R"
    # Upper primary fang
    f1_loc = (sign * 0.100, -0.300, 1.365)
    f1_rot = (math.radians(18), math.radians(sign * -28), math.radians(sign * -50))
    bpy.ops.mesh.primitive_cone_add(vertices=14, radius1=0.016, radius2=0.001, depth=0.048, location=f1_loc, rotation=f1_rot)
    tooth1 = bpy.context.active_object
    tooth1.name = f"Tooth_{suf}1"
    tooth1.data.materials.append(mat_teeth)
    bpy.ops.object.shade_smooth()
    reg(tooth1)

    # Lower secondary fang
    f2_loc = (sign * 0.075, -0.285, 1.335)
    f2_rot = (math.radians(8), math.radians(sign * -22), math.radians(sign * -65))
    bpy.ops.mesh.primitive_cone_add(vertices=12, radius1=0.013, radius2=0.001, depth=0.040, location=f2_loc, rotation=f2_rot)
    tooth2 = bpy.context.active_object
    tooth2.name = f"Tooth_{suf}2"
    tooth2.data.materials.append(mat_teeth)
    bpy.ops.object.shade_smooth()
    reg(tooth2)

    return mand_obj

def make_oral_cavity(name):
    # Recessed dark mouth interior cavity behind caliper pincer claws
    bm_oral = bmesh.new()
    bmesh.ops.create_uvsphere(bm_oral, u_segments=24, v_segments=16, radius=0.080)
    for v in list(bm_oral.verts):
        if v.co.y < 0.005:
            bm_oral.verts.remove(v)
    for v in bm_oral.verts:
        v.co.x *= 1.20
        v.co.y *= 1.40
        v.co.z *= 0.90
    bmesh.ops.recalc_face_normals(bm_oral, faces=bm_oral.faces)
    for f in bm_oral.faces:
        f.normal_flip()
    mesh = bpy.data.meshes.new(name)
    bm_oral.to_mesh(mesh)
    bm_oral.free()
    for p in mesh.polygons:
        p.use_smooth = True
    oral_obj = bpy.data.objects.new(name, mesh)
    oral_obj.location = (0.0, -0.210, 1.350)
    oral_obj.data.materials.append(mat_oral)
    return reg(oral_obj)

make_authentic_pincer_claw("Mandible_L", True)
make_authentic_pincer_claw("Mandible_R", False)
make_oral_cavity("Oral_Cavity")

# -----------------------------------------------------------------------------
# 7. OVERSIZED FIRE CHIEF HELMET ("Child Wearing an Adult's Hat")
# -----------------------------------------------------------------------------
# A. High Domed Crown Shell
bm_crown = bmesh.new()
bmesh.ops.create_uvsphere(bm_crown, u_segments=36, v_segments=24, radius=1.0)

for v in list(bm_crown.verts):
    if v.co.z < -0.05:
        bm_crown.verts.remove(v)

for v in bm_crown.verts:
    x = v.co.x * 0.40
    y = v.co.y * 0.45
    z = v.co.z * 0.36

    if y > 0:
        y *= (1.0 + 0.10 * (z / 0.36))
    else:
        y *= 0.96

    v.co = Vector((x, y, z))

crown_mesh = bpy.data.meshes.new("Helmet_Crown_Mesh")
bm_crown.to_mesh(crown_mesh)
bm_crown.free()

for p in crown_mesh.polygons:
    p.use_smooth = True

crown_obj = bpy.data.objects.new("Helmet_Crown", crown_mesh)
crown_obj.location = (0, -0.03, 1.69)
crown_obj.data.materials.append(mat_helmet)
sub_cr = crown_obj.modifiers.new("Subsurf", 'SUBSURF')
sub_cr.levels = 2
sol_cr = crown_obj.modifiers.new("Solidify", 'SOLIDIFY')
sol_cr.thickness = 0.016
reg(crown_obj)

# B. Central Spine / Comb Ridge
bm_comb = bmesh.new()
bmesh.ops.create_cube(bm_comb, size=1.0)
bmesh.ops.subdivide_edges(bm_comb, edges=bm_comb.edges, cuts=6, use_grid_fill=True)

for v in bm_comb.verts:
    vx, vy, vz = v.co.x, v.co.y, v.co.z
    y = vy * 0.54 + 0.05
    norm_y = max(-0.95, min(0.95, y / 0.45))
    arch_h = 0.36 * math.sqrt(max(0.01, 1.0 - norm_y**2))

    x = vx * 0.052 * (1.0 - 0.35 * vz)
    z = (vz + 0.5) * 0.070 + arch_h - 0.01

    v.co = Vector((x, y, z))

comb_mesh = bpy.data.meshes.new("Helmet_Comb_Mesh")
bm_comb.to_mesh(comb_mesh)
bm_comb.free()

for p in comb_mesh.polygons:
    p.use_smooth = True

comb_obj = bpy.data.objects.new("Helmet_Comb", comb_mesh)
comb_obj.location = (0, -0.03, 1.69)
comb_obj.data.materials.append(mat_helmet)
sub_cb = comb_obj.modifiers.new("Subsurf", 'SUBSURF')
sub_cb.levels = 2
reg(comb_obj)

# C. Flared Duckbill Brim ("Oversized Child-Wearing-Adult-Hat" Silhouette)
bm_brim = bmesh.new()
ring_segs = 36
inner_verts = []
outer_verts = []

for i in range(ring_segs):
    th = 2.0 * math.pi * i / ring_segs
    cos_t = math.cos(th)
    sin_t = math.sin(th)

    in_x = sin_t * 0.39
    in_y = cos_t * 0.43
    in_z = 0.02

    out_x = sin_t * 0.48
    if cos_t >= 0:
        out_y = cos_t * 0.54
        out_z = -0.14 * (cos_t ** 1.4)
    else:
        out_y = cos_t * 0.37
        out_z = -0.03 * (-cos_t)

    v_in = bm_brim.verts.new((in_x, in_y, in_z))
    v_out = bm_brim.verts.new((out_x, out_y, out_z))
    inner_verts.append(v_in)
    outer_verts.append(v_out)

bm_brim.verts.ensure_lookup_table()

for i in range(ring_segs):
    next_i = (i + 1) % ring_segs
    v1 = inner_verts[i]
    v2 = outer_verts[i]
    v3 = outer_verts[next_i]
    v4 = inner_verts[next_i]
    bm_brim.faces.new([v1, v2, v3, v4])

brim_mesh = bpy.data.meshes.new("Helmet_Brim_Mesh")
bm_brim.to_mesh(brim_mesh)
bm_brim.free()

for p in brim_mesh.polygons:
    p.use_smooth = True

brim_obj = bpy.data.objects.new("Helmet_Brim", brim_mesh)
brim_obj.location = (0, -0.03, 1.69)
brim_obj.data.materials.append(mat_helmet)
sol_br = brim_obj.modifiers.new("Solidify", 'SOLIDIFY')
sol_br.thickness = 0.016
sol_br.offset = 0.0
sub_br = brim_obj.modifiers.new("Subsurf", 'SUBSURF')
sub_br.levels = 2
reg(brim_obj)

# D. Front Shield Plaque Proudly Mounted on Front of Helmet
# Placed on outer front slope of helmet crown: Y = -0.38, Z = 1.85, tilt = -16 deg
shield_loc = Vector((0.0, -0.38, 1.85))
shield_rot = Euler((math.radians(-16), 0, 0), 'XYZ')

bm_shield = bmesh.new()
# Scaled shield silhouette (width 0.25, height 0.30)
shield_pts = [
    Vector(( 0.00, 0.0,  0.150)),  # Top peak
    Vector(( 0.125, 0.0,  0.130)), # Top right corner
    Vector(( 0.115, 0.0, -0.035)), # Mid right
    Vector(( 0.00, 0.0, -0.150)),  # Bottom point
    Vector((-0.115, 0.0, -0.035)), # Mid left
    Vector((-0.125, 0.0,  0.130)), # Top left corner
]

v_shield = [bm_shield.verts.new(p) for p in shield_pts]
bm_shield.faces.new(v_shield)
bmesh.ops.subdivide_edges(bm_shield, edges=bm_shield.edges, cuts=2, use_grid_fill=True)

uv_sh = bm_shield.loops.layers.uv.new("UVMap")
for f in bm_shield.faces:
    for loop in f.loops:
        u = 0.50 + loop.vert.co.x / 0.26
        v = 0.50 + loop.vert.co.z / 0.32
        loop[uv_sh].uv = (min(1.0, max(0.0, u)), min(1.0, max(0.0, v)))

shield_mesh = bpy.data.meshes.new("Helmet_Shield_Mesh")
bm_shield.to_mesh(shield_mesh)
bm_shield.free()

for p in shield_mesh.polygons:
    p.use_smooth = True

shield_obj = bpy.data.objects.new("Helmet_Shield", shield_mesh)
shield_obj.location = shield_loc
shield_obj.rotation_euler = shield_rot
shield_obj.data.materials.append(mat_shield)
sol_sh = shield_obj.modifiers.new("Solidify", 'SOLIDIFY')
sol_sh.thickness = 0.018
reg(shield_obj)

# E. Physical 3D Embossed Bold Red Letter 'A' Mounted on Shield Face
# Authentic bold typography with beveled edges and deep saturated scarlet red
font_curve = bpy.data.curves.new(name="Badge_Red_A_Curve", type='FONT')
font_curve.body = "A"
font_curve.offset = 0.010 # Chunky bold fire brigade lettering
font_curve.extrude = 0.018
font_curve.bevel_depth = 0.003
font_curve.bevel_resolution = 3
font_curve.align_x = 'CENTER'
font_curve.align_y = 'CENTER'
font_curve.size = 0.195

a_obj = bpy.data.objects.new("Badge_Red_A", font_curve)
a_obj.location = (0.0, -0.408, 1.882)
a_obj.rotation_euler = Euler((math.radians(74), 0, 0), 'XYZ')
bpy.context.scene.collection.objects.link(a_obj)
bpy.ops.object.select_all(action='DESELECT')
a_obj.select_set(True)
bpy.context.view_layer.objects.active = a_obj
bpy.ops.object.convert(target='MESH')
a_obj.data.materials.append(mat_red_a)
reg(a_obj)

# F. Leather Chin Strap / Helmet Trim
bpy.ops.mesh.primitive_torus_add(
    major_radius=0.40, minor_radius=0.014,
    major_segments=36, minor_segments=12,
    location=(0, -0.03, 1.72),
    rotation=(0, 0, 0)
)
trim_obj = bpy.context.active_object
trim_obj.name = "Helmet_Rim_Trim"
trim_obj.scale = (1.0, 1.08, 0.45)
bpy.ops.object.transform_apply(scale=True)
trim_obj.data.materials.append(mat_trim)
bpy.ops.object.shade_smooth()
reg(trim_obj)

# -----------------------------------------------------------------------------
# 8. Articulated Neck & 3-Segment Arched Thorax
# -----------------------------------------------------------------------------
bpy.ops.mesh.primitive_cylinder_add(
    vertices=24, radius=0.095, depth=0.12,
    location=(0, -0.02, 1.44),
    rotation=(math.radians(12), 0, 0)
)
neck_obj = bpy.context.active_object
neck_obj.name = "Neck"
neck_obj.scale = (1.0, 0.88, 1.0)
bpy.ops.object.transform_apply(scale=True)
neck_obj.data.materials.append(mat_chitin)
bpy.ops.object.shade_smooth()
reg(neck_obj)

# Segment A: Pronotum (Shield Collar)
bpy.ops.mesh.primitive_uv_sphere_add(
    segments=32, ring_count=20, radius=0.175,
    location=(0, -0.02, 1.32),
    rotation=(math.radians(-12), 0, 0)
)
pro_obj = bpy.context.active_object
pro_obj.name = "Thorax_Pronotum"
pro_obj.scale = (0.95, 0.90, 0.78)
bpy.ops.object.transform_apply(scale=True)
pro_obj.data.materials.append(mat_chitin)
sub_p = pro_obj.modifiers.new("Subsurf", 'SUBSURF')
sub_p.levels = 1
bpy.ops.object.shade_smooth()
reg(pro_obj)

# Segment B: Mesonotum (Central Muscular Arch)
bpy.ops.mesh.primitive_uv_sphere_add(
    segments=32, ring_count=20, radius=0.190,
    location=(0, 0.015, 1.18),
    rotation=(math.radians(10), 0, 0)
)
meso_obj = bpy.context.active_object
meso_obj.name = "Thorax_Mesonotum"
meso_obj.scale = (1.0, 0.96, 0.82)
bpy.ops.object.transform_apply(scale=True)
meso_obj.data.materials.append(mat_chitin)
sub_m = meso_obj.modifiers.new("Subsurf", 'SUBSURF')
sub_m.levels = 1
bpy.ops.object.shade_smooth()
reg(meso_obj)

# Segment C: Metanotum (Rear Tapering Segment)
bpy.ops.mesh.primitive_uv_sphere_add(
    segments=32, ring_count=20, radius=0.165,
    location=(0, 0.050, 1.04),
    rotation=(math.radians(22), 0, 0)
)
meta_obj = bpy.context.active_object
meta_obj.name = "Thorax_Metanotum"
meta_obj.scale = (0.90, 0.95, 0.85)
bpy.ops.object.transform_apply(scale=True)
meta_obj.data.materials.append(mat_chitin)
sub_mt = meta_obj.modifiers.new("Subsurf", 'SUBSURF')
sub_mt.levels = 1
bpy.ops.object.shade_smooth()
reg(meta_obj)

# -----------------------------------------------------------------------------
# 9. Segment Builders: Articulated Sockets, Collars & Sleeves
# -----------------------------------------------------------------------------
def make_joint_socket(name, location, radius, scale=(1.0, 1.0, 1.0), material=None):
    if material is None:
        material = mat_limbs
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=20, ring_count=14, radius=radius,
        location=location
    )
    sock = bpy.context.active_object
    sock.name = name
    sock.scale = scale
    bpy.ops.object.transform_apply(scale=True)
    sock.data.materials.append(material)
    bpy.ops.object.shade_smooth()
    return reg(sock)

def make_chitin_segment(name, p1, p2, r_start, r_mid, r_end, is_sleeve=False, material=None):
    if material is None:
        material = mat_limbs
    v_axis = p2 - p1
    length = v_axis.length
    if length < 1e-5:
        return None

    dir_axis = v_axis.normalized()
    z_up = Vector((0, 0, 1))
    q_rot = z_up.rotation_difference(dir_axis)
    p_center = (p1 + p2) * 0.5

    n_slices = 16
    n_rings = 18
    bm = bmesh.new()

    for ring in range(n_rings):
        t = ring / (n_rings - 1)
        z_loc = (t - 0.5) * length

        w_taper = math.sin(t * math.pi)
        radius = (r_start * (1.0 - t) + r_end * t) + (r_mid - (r_start + r_end) * 0.5) * w_taper

        for seg in range(n_slices):
            angle = 2.0 * math.pi * seg / n_slices
            rib = math.sin(angle * 6.0) * 0.08
            r_act = radius * (1.0 + rib)
            if is_sleeve and ring == 0:
                r_act *= 1.25

            x_loc = math.cos(angle) * r_act
            y_loc = math.sin(angle) * r_act * 0.90
            bm.verts.new(Vector((x_loc, y_loc, z_loc)))

    bm.verts.ensure_lookup_table()
    for ring in range(n_rings - 1):
        for seg in range(n_slices):
            next_seg = (seg + 1) % n_slices
            v1 = bm.verts[ring * n_slices + seg]
            v2 = bm.verts[ring * n_slices + next_seg]
            v3 = bm.verts[(ring + 1) * n_slices + next_seg]
            v4 = bm.verts[(ring + 1) * n_slices + seg]
            bm.faces.new([v1, v2, v3, v4])

    mesh = bpy.data.meshes.new(f"{name}_Mesh")
    bm.to_mesh(mesh)
    bm.free()

    for p in mesh.polygons:
        p.use_smooth = True

    obj = bpy.data.objects.new(name, mesh)
    obj.location = p_center
    obj.rotation_mode = 'QUATERNION'
    obj.rotation_quaternion = q_rot
    obj.data.materials.append(material)
    sub = obj.modifiers.new("Subsurf", 'SUBSURF')
    sub.levels = 1
    return reg(obj)

def make_foot_toe_pad(name, location, radius, material=None):
    if material is None:
        material = mat_limbs
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=16, ring_count=12, radius=radius,
        location=location
    )
    toe = bpy.context.active_object
    toe.name = name
    toe.scale = (0.75, 1.45, 0.45)
    bpy.ops.object.transform_apply(scale=True)
    toe.data.materials.append(material)
    bpy.ops.object.shade_smooth()
    return reg(toe)

# -----------------------------------------------------------------------------
# 10. Seamless Articulated Petiole Waist & Suspended Gaster Abdomen
# -----------------------------------------------------------------------------
p_pet_start = Vector((0, 0.08, 0.94))  # Anchored deep inside Thorax_Metanotum
p_pet_end   = Vector((0, 0.22, 0.78))  # Embedded deep inside anterior Gaster socket

make_joint_socket("Petiole_Thorax_Socket", p_pet_start, 0.052, material=mat_chitin)
make_chitin_segment("Petiole", p_pet_start, p_pet_end, 0.048, 0.044, 0.052, is_sleeve=True, material=mat_chitin)
make_joint_socket("Petiole_Gaster_Socket", p_pet_end, 0.056, material=mat_chitin)

# Gaster: Suspended Plump Egg Abdomen at 20°
bpy.ops.mesh.primitive_uv_sphere_add(
    segments=36, ring_count=24, radius=1.0,
    location=(0, 0.38, 0.68),
    rotation=(math.radians(20), 0, 0)
)
gaster_obj = bpy.context.active_object
gaster_obj.name = "Gaster"

for v in gaster_obj.data.vertices:
    x = v.co.x * 0.24
    y = v.co.y * 0.32
    z = v.co.z * 0.24

    if y > 0:
        taper = 1.0 - 0.36 * (y / 0.32)
        x *= taper
        z *= (taper * 0.92)
    else:
        t_ant = min(1.0, (-y) / 0.32)
        x *= (1.0 - 0.40 * t_ant)
        z *= (1.0 - 0.40 * t_ant)

    groove = math.sin((y + 0.32) * 18.0) * 0.006
    x += groove * (x / 0.24)
    z += groove * (z / 0.24)

    v.co = Vector((x, y, z))

gaster_obj.data.update()
gaster_obj.data.materials.append(mat_chitin)
sub_g = gaster_obj.modifiers.new("Subsurf", 'SUBSURF')
sub_g.levels = 2
bpy.ops.object.shade_smooth()
reg(gaster_obj)

# -----------------------------------------------------------------------------
# 11. Sculpted Insect Limbs: Expressive Empty Arms & Compact Walking Legs
# -----------------------------------------------------------------------------
for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"

    p_shoulder = Vector((sign * 0.13, -0.04, 1.22))
    p_elbow    = Vector((sign * 0.22, -0.12, 1.02))
    p_wrist    = Vector((sign * 0.17, -0.22, 0.82))
    p_finger1  = Vector((sign * 0.14, -0.26, 0.68))
    p_finger2  = Vector((sign * 0.18, -0.24, 0.66))

    make_joint_socket(f"Arm_Shoulder_{suf}", p_shoulder, 0.044, scale=(1.1, 1.1, 1.1))
    make_chitin_segment(f"Arm_Upper_{suf}", p_shoulder, p_elbow, 0.038, 0.044, 0.032, is_sleeve=True)
    make_joint_socket(f"Arm_Elbow_{suf}", p_elbow, 0.038)
    make_chitin_segment(f"Arm_Forearm_{suf}", p_elbow, p_wrist, 0.032, 0.036, 0.026, is_sleeve=True)
    make_joint_socket(f"Arm_Wrist_{suf}", p_wrist, 0.028)
    make_chitin_segment(f"Arm_Finger1_{suf}", p_wrist, p_finger1, 0.016, 0.016, 0.010)
    make_chitin_segment(f"Arm_Finger2_{suf}", p_wrist, p_finger2, 0.014, 0.014, 0.008)

for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"

    p_hip   = Vector((sign * 0.13, 0.03, 1.06))
    p_knee  = Vector((sign * 0.28, -0.02, 0.68))
    p_ankle = Vector((sign * 0.22, -0.08, 0.08))
    p_toe1  = Vector((sign * 0.20, -0.16, 0.02))
    p_toe2  = Vector((sign * 0.24, -0.14, 0.02))

    make_joint_socket(f"Leg_Mid_Coxa_{suf}", p_hip, 0.048, scale=(1.1, 1.2, 1.1))
    make_chitin_segment(f"Leg_Mid_Femur_{suf}", p_hip, p_knee, 0.044, 0.050, 0.036, is_sleeve=True)
    make_joint_socket(f"Leg_Mid_Knee_{suf}", p_knee, 0.042)
    make_chitin_segment(f"Leg_Mid_Tibia_{suf}", p_knee, p_ankle, 0.036, 0.032, 0.024)
    make_joint_socket(f"Leg_Mid_Ankle_{suf}", p_ankle, 0.028)
    make_chitin_segment(f"Leg_Mid_Foot_{suf}", p_ankle, (p_toe1 + p_toe2) * 0.5, 0.024, 0.022, 0.016)
    make_foot_toe_pad(f"Leg_Mid_Toe1_{suf}", p_toe1, 0.022)
    make_foot_toe_pad(f"Leg_Mid_Toe2_{suf}", p_toe2, 0.018)

for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"

    p_hip   = Vector((sign * 0.11, 0.07, 0.96))
    p_knee  = Vector((sign * 0.42, 0.18, 0.82))
    p_ankle = Vector((sign * 0.35, 0.10, 0.08))
    p_toe1  = Vector((sign * 0.36, 0.04, 0.02))
    p_toe2  = Vector((sign * 0.40, 0.14, 0.02))

    make_joint_socket(f"Leg_Hind_Coxa_{suf}", p_hip, 0.052, scale=(1.1, 1.2, 1.1))
    make_chitin_segment(f"Leg_Hind_Femur_{suf}", p_hip, p_knee, 0.048, 0.054, 0.038, is_sleeve=True)
    make_joint_socket(f"Leg_Hind_Knee_{suf}", p_knee, 0.044)
    make_chitin_segment(f"Leg_Hind_Tibia_{suf}", p_knee, p_ankle, 0.038, 0.034, 0.026)
    make_joint_socket(f"Leg_Hind_Ankle_{suf}", p_ankle, 0.030)
    make_chitin_segment(f"Leg_Hind_Foot_{suf}", p_ankle, (p_toe1 + p_toe2) * 0.5, 0.026, 0.024, 0.018)
    make_foot_toe_pad(f"Leg_Hind_Toe1_{suf}", p_toe1, 0.024)
    make_foot_toe_pad(f"Leg_Hind_Toe2_{suf}", p_toe2, 0.020)

# -----------------------------------------------------------------------------
# 12. Full Skeletal Armature Rig & Animation System
# -----------------------------------------------------------------------------
amt = bpy.data.armatures.new("Fire_Ant_Armature")
rig = bpy.data.objects.new("Fire_Ant_Rig", amt)
fire_col.objects.link(rig)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode='EDIT')

b_root = amt.edit_bones.new("Root")
b_root.head = Vector((0, 0, 0))
b_root.tail = Vector((0, 0, 0.40))

b_thorax = amt.edit_bones.new("Thorax")
b_thorax.head = Vector((0, 0.03, 0.95))
b_thorax.tail = Vector((0, -0.01, 1.28))
b_thorax.parent = b_root

b_neck = amt.edit_bones.new("Neck")
b_neck.head = Vector((0, -0.02, 1.37))
b_neck.tail = Vector((0, -0.04, 1.58))
b_neck.parent = b_thorax

b_head = amt.edit_bones.new("Head")
b_head.head = Vector((0, -0.04, 1.58))
b_head.tail = Vector((0, -0.04, 1.95))
b_head.parent = b_neck

b_gaster = amt.edit_bones.new("Gaster")
b_gaster.head = Vector((0, 0.08, 0.94))
b_gaster.tail = Vector((0, 0.38, 0.68))
b_gaster.parent = b_thorax

for is_left in [True, False]:
    sign = -1.0 if is_left else 1.0
    suf = "L" if is_left else "R"

    p_shoulder = Vector((sign * 0.13, -0.04, 1.22))
    p_elbow    = Vector((sign * 0.22, -0.12, 1.02))
    p_wrist    = Vector((sign * 0.17, -0.22, 0.82))
    p_hand     = Vector((sign * 0.16, -0.25, 0.67))

    b_up = amt.edit_bones.new(f"Arm_Upper_{suf}")
    b_up.head = p_shoulder
    b_up.tail = p_elbow
    b_up.parent = b_thorax

    b_fa = amt.edit_bones.new(f"Arm_Forearm_{suf}")
    b_fa.head = p_elbow
    b_fa.tail = p_wrist
    b_fa.parent = b_up

    b_hd = amt.edit_bones.new(f"Arm_Hand_{suf}")
    b_hd.head = p_wrist
    b_hd.tail = p_hand
    b_hd.parent = b_fa

    p_hip_m   = Vector((sign * 0.13, 0.03, 1.06))
    p_knee_m  = Vector((sign * 0.28, -0.02, 0.68))
    p_ankle_m = Vector((sign * 0.22, -0.08, 0.08))
    p_foot_m  = Vector((sign * 0.22, -0.15, 0.02))

    b_fm = amt.edit_bones.new(f"Leg_Mid_Femur_{suf}")
    b_fm.head = p_hip_m
    b_fm.tail = p_knee_m
    b_fm.parent = b_thorax

    b_tm = amt.edit_bones.new(f"Leg_Mid_Tibia_{suf}")
    b_tm.head = p_knee_m
    b_tm.tail = p_ankle_m
    b_tm.parent = b_fm

    b_ftm = amt.edit_bones.new(f"Leg_Mid_Foot_{suf}")
    b_ftm.head = p_ankle_m
    b_ftm.tail = p_foot_m
    b_ftm.parent = b_tm

    p_hip_h   = Vector((sign * 0.11, 0.07, 0.96))
    p_knee_h  = Vector((sign * 0.42, 0.18, 0.82))
    p_ankle_h = Vector((sign * 0.35, 0.10, 0.08))
    p_foot_h  = Vector((sign * 0.38, 0.09, 0.02))

    b_fh = amt.edit_bones.new(f"Leg_Hind_Femur_{suf}")
    b_fh.head = p_hip_h
    b_fh.tail = p_knee_h
    b_fh.parent = b_thorax

    b_th = amt.edit_bones.new(f"Leg_Hind_Tibia_{suf}")
    b_th.head = p_knee_h
    b_th.tail = p_ankle_h
    b_th.parent = b_fh

    b_fth = amt.edit_bones.new(f"Leg_Hind_Foot_{suf}")
    b_fth.head = p_ankle_h
    b_fth.tail = p_foot_h
    b_fth.parent = b_th

bpy.ops.object.mode_set(mode='OBJECT')

bone_map = {
    "Head": "Head",
    "Eye_L": "Head", "Eye_R": "Head",
    "Eyelid_L": "Head", "Eyelid_R": "Head",
    "Mandible_L": "Head", "Mandible_R": "Head",
    "Tooth_L1": "Head", "Tooth_L2": "Head",
    "Tooth_R1": "Head", "Tooth_R2": "Head",
    "Oral_Cavity": "Head",
    "Helmet_Crown": "Head",
    "Helmet_Comb": "Head",
    "Helmet_Brim": "Head",
    "Helmet_Shield": "Head",
    "Badge_Red_A": "Head",
    "Helmet_Rim_Trim": "Head",
    "Neck": "Neck",
    "Thorax_Pronotum": "Thorax",
    "Thorax_Mesonotum": "Thorax",
    "Thorax_Metanotum": "Thorax",
    "Petiole_Thorax_Socket": "Gaster",
    "Petiole": "Gaster",
    "Petiole_Gaster_Socket": "Gaster",
    "Gaster": "Gaster",
}

for is_left in [True, False]:
    suf = "L" if is_left else "R"
    bone_map[f"Arm_Shoulder_{suf}"] = "Thorax"
    bone_map[f"Arm_Upper_{suf}"] = f"Arm_Upper_{suf}"
    bone_map[f"Arm_Elbow_{suf}"] = f"Arm_Upper_{suf}"
    bone_map[f"Arm_Forearm_{suf}"] = f"Arm_Forearm_{suf}"
    bone_map[f"Arm_Wrist_{suf}"] = f"Arm_Hand_{suf}"
    bone_map[f"Arm_Finger1_{suf}"] = f"Arm_Hand_{suf}"
    bone_map[f"Arm_Finger2_{suf}"] = f"Arm_Hand_{suf}"

    bone_map[f"Leg_Mid_Coxa_{suf}"] = "Thorax"
    bone_map[f"Leg_Mid_Femur_{suf}"] = f"Leg_Mid_Femur_{suf}"
    bone_map[f"Leg_Mid_Knee_{suf}"] = f"Leg_Mid_Femur_{suf}"
    bone_map[f"Leg_Mid_Tibia_{suf}"] = f"Leg_Mid_Tibia_{suf}"
    bone_map[f"Leg_Mid_Ankle_{suf}"] = f"Leg_Mid_Foot_{suf}"
    bone_map[f"Leg_Mid_Foot_{suf}"] = f"Leg_Mid_Foot_{suf}"
    bone_map[f"Leg_Mid_Toe1_{suf}"] = f"Leg_Mid_Foot_{suf}"
    bone_map[f"Leg_Mid_Toe2_{suf}"] = f"Leg_Mid_Foot_{suf}"

    bone_map[f"Leg_Hind_Coxa_{suf}"] = "Thorax"
    bone_map[f"Leg_Hind_Femur_{suf}"] = f"Leg_Hind_Femur_{suf}"
    bone_map[f"Leg_Hind_Knee_{suf}"] = f"Leg_Hind_Femur_{suf}"
    bone_map[f"Leg_Hind_Tibia_{suf}"] = f"Leg_Hind_Tibia_{suf}"
    bone_map[f"Leg_Hind_Ankle_{suf}"] = f"Leg_Hind_Foot_{suf}"
    bone_map[f"Leg_Hind_Foot_{suf}"] = f"Leg_Hind_Foot_{suf}"
    bone_map[f"Leg_Hind_Toe1_{suf}"] = f"Leg_Hind_Foot_{suf}"
    bone_map[f"Leg_Hind_Toe2_{suf}"] = f"Leg_Hind_Foot_{suf}"

for obj_name, b_name in bone_map.items():
    o = bpy.data.objects.get(obj_name)
    if o:
        bpy.ops.object.select_all(action='DESELECT')
        o.select_set(True)
        rig.select_set(True)
        bpy.context.view_layer.objects.active = rig
        rig.data.bones.active = rig.data.bones[b_name]
        bpy.ops.object.parent_set(type='BONE')

# Keyframe 30-frame Alternating Tripod Walk Action
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode='POSE')
rig.animation_data_create()
action = bpy.data.actions.new(name="Walk")
rig.animation_data.action = action

for pb in rig.pose.bones:
    pb.rotation_mode = 'XYZ'

pb_thorax = rig.pose.bones['Thorax']
pb_head = rig.pose.bones['Head']
pb_gaster = rig.pose.bones['Gaster']

# Thorax vertical bounce and sway
pb_thorax.location = Vector((0, 0, 0))
pb_thorax.rotation_euler = Euler((0, 0, 0))
pb_thorax.keyframe_insert('location', frame=1)
pb_thorax.keyframe_insert('rotation_euler', frame=1)

pb_thorax.location = Vector((0, 0, 0.024))
pb_thorax.rotation_euler = Euler((math.radians(1.5), 0, math.radians(2.0)))
pb_thorax.keyframe_insert('location', frame=8)
pb_thorax.keyframe_insert('rotation_euler', frame=8)

pb_thorax.location = Vector((0, 0, 0))
pb_thorax.rotation_euler = Euler((0, 0, 0))
pb_thorax.keyframe_insert('location', frame=15)
pb_thorax.keyframe_insert('rotation_euler', frame=15)

pb_thorax.location = Vector((0, 0, 0.024))
pb_thorax.rotation_euler = Euler((math.radians(1.5), 0, math.radians(-2.0)))
pb_thorax.keyframe_insert('location', frame=23)
pb_thorax.keyframe_insert('rotation_euler', frame=23)

pb_thorax.location = Vector((0, 0, 0))
pb_thorax.rotation_euler = Euler((0, 0, 0))
pb_thorax.keyframe_insert('location', frame=30)
pb_thorax.keyframe_insert('rotation_euler', frame=30)

# Head & Helmet confident nod
pb_head.rotation_euler = Euler((0, 0, 0))
pb_head.keyframe_insert('rotation_euler', frame=1)
pb_head.rotation_euler = Euler((math.radians(2.5), 0, 0))
pb_head.keyframe_insert('rotation_euler', frame=8)
pb_head.rotation_euler = Euler((0, 0, 0))
pb_head.keyframe_insert('rotation_euler', frame=15)
pb_head.rotation_euler = Euler((math.radians(2.5), 0, 0))
pb_head.keyframe_insert('rotation_euler', frame=23)
pb_head.rotation_euler = Euler((0, 0, 0))
pb_head.keyframe_insert('rotation_euler', frame=30)

# Gaster counter-sway
pb_gaster.rotation_euler = Euler((0, 0, 0))
pb_gaster.keyframe_insert('rotation_euler', frame=1)
pb_gaster.rotation_euler = Euler((0, math.radians(-2.5), math.radians(-2.0)))
pb_gaster.keyframe_insert('rotation_euler', frame=8)
pb_gaster.rotation_euler = Euler((0, 0, 0))
pb_gaster.keyframe_insert('rotation_euler', frame=15)
pb_gaster.rotation_euler = Euler((0, math.radians(2.5), math.radians(2.0)))
pb_gaster.keyframe_insert('rotation_euler', frame=23)
pb_gaster.rotation_euler = Euler((0, 0, 0))
pb_gaster.keyframe_insert('rotation_euler', frame=30)

def keyframe_leg(femur_name, tibia_name, is_group_a):
    pb_f = rig.pose.bones.get(femur_name)
    pb_t = rig.pose.bones.get(tibia_name)
    if not pb_f or not pb_t:
        return
    phases = [
        (1,  -15.0 if is_group_a else 15.0,   0.0 if is_group_a else 0.0),
        (8,    0.0 if is_group_a else 18.0,   0.0 if is_group_a else 24.0),
        (15,  15.0 if is_group_a else -15.0,  0.0 if is_group_a else 0.0),
        (23,  18.0 if is_group_a else 0.0,   24.0 if is_group_a else 0.0),
        (30, -15.0 if is_group_a else 15.0,   0.0 if is_group_a else 0.0)
    ]
    for frame, f_pitch, t_flex in phases:
        pb_f.rotation_euler = Euler((math.radians(f_pitch), 0, 0))
        pb_f.keyframe_insert('rotation_euler', frame=frame)
        pb_t.rotation_euler = Euler((math.radians(t_flex), 0, 0))
        pb_t.keyframe_insert('rotation_euler', frame=frame)

keyframe_leg("Leg_Mid_Femur_L", "Leg_Mid_Tibia_L", True)
keyframe_leg("Leg_Hind_Femur_R", "Leg_Hind_Tibia_R", True)

keyframe_leg("Leg_Mid_Femur_R", "Leg_Mid_Tibia_R", False)
keyframe_leg("Leg_Hind_Femur_L", "Leg_Hind_Tibia_L", False)

def keyframe_arm(arm_name, is_group_a):
    pb_a = rig.pose.bones.get(arm_name)
    if not pb_a:
        return
    phases = [
        (1,   16.0 if is_group_a else -16.0),
        (8,    0.0 if is_group_a else   0.0),
        (15, -16.0 if is_group_a else  16.0),
        (23,   0.0 if is_group_a else   0.0),
        (30,  16.0 if is_group_a else -16.0)
    ]
    for frame, pitch in phases:
        pb_a.rotation_euler = Euler((math.radians(pitch), 0, 0))
        pb_a.keyframe_insert('rotation_euler', frame=frame)

keyframe_arm("Arm_Upper_R", True)
keyframe_arm("Arm_Upper_L", False)

# Push Walk action to NLA track so it is preserved alongside other actions for GLTF
track_walk = rig.animation_data.nla_tracks.new()
track_walk.name = "Walk_Track"
track_walk.strips.new("Walk", 1, action)

# Create 40-frame Organic Idle Breathing & Alert Stance Action
action_idle = bpy.data.actions.new(name="Idle")
rig.animation_data.action = action_idle

for pb in rig.pose.bones:
    pb.location = Vector((0, 0, 0))
    pb.rotation_euler = Euler((0, 0, 0))

idle_thorax_frames = [
    (1,  Vector((0, 0, 0)),     Euler((0, 0, 0))),
    (20, Vector((0, 0, 0.008)), Euler((math.radians(0.8), 0, 0))),
    (40, Vector((0, 0, 0)),     Euler((0, 0, 0)))
]
for f, loc, rot in idle_thorax_frames:
    pb_thorax.location = loc
    pb_thorax.rotation_euler = rot
    pb_thorax.keyframe_insert('location', frame=f)
    pb_thorax.keyframe_insert('rotation_euler', frame=f)

idle_head_frames = [
    (1,  Euler((0, 0, 0))),
    (18, Euler((math.radians(-1.5), 0, math.radians(1.2)))),
    (28, Euler((math.radians(1.0), 0, math.radians(-1.0)))),
    (40, Euler((0, 0, 0)))
]
for f, rot in idle_head_frames:
    pb_head.rotation_euler = rot
    pb_head.keyframe_insert('rotation_euler', frame=f)

idle_gaster_frames = [
    (1,  Euler((0, 0, 0))),
    (15, Euler((math.radians(-2.0), 0, 0))),
    (30, Euler((math.radians(1.2), 0, 0))),
    (40, Euler((0, 0, 0)))
]
for f, rot in idle_gaster_frames:
    pb_gaster.rotation_euler = rot
    pb_gaster.keyframe_insert('rotation_euler', frame=f)

for suf, sign_a in [("L", -1.0), ("R", 1.0)]:
    pb_up = rig.pose.bones.get(f"Arm_Upper_{suf}")
    if pb_up:
        pb_up.rotation_euler = Euler((0, 0, 0))
        pb_up.keyframe_insert('rotation_euler', frame=1)
        pb_up.rotation_euler = Euler((math.radians(1.5), 0, math.radians(sign_a * 1.0)))
        pb_up.keyframe_insert('rotation_euler', frame=20)
        pb_up.rotation_euler = Euler((0, 0, 0))
        pb_up.keyframe_insert('rotation_euler', frame=40)

track_idle = rig.animation_data.nla_tracks.new()
track_idle.name = "Idle_Track"
track_idle.strips.new("Idle", 1, action_idle)

# Set active action to Walk for default playback and renders
rig.animation_data.action = action
bpy.ops.object.mode_set(mode='OBJECT')
bpy.context.scene.frame_set(1)

# -----------------------------------------------------------------------------
# 13. Multi-Angle Cameras & Render Stills
# -----------------------------------------------------------------------------
cam_data = bpy.data.cameras.new("RenderCam")
cam_data.lens = 65.0
cam_obj = bpy.data.objects.new("RenderCam", cam_data)
bpy.context.scene.collection.objects.link(cam_obj)
scene.camera = cam_obj

views = [
    (
        "fire_front.png",
        Vector((0.0, -4.7, 1.25)),
        Euler((math.radians(88), 0, 0), 'XYZ'),
        52.0,
        "Fire Ant Front Heroic Character Stance"
    ),
    (
        "fire_perspective.png",
        Vector((-2.8, -3.2, 1.40)),
        Euler((math.radians(82), 0, math.radians(-40)), 'XYZ'),
        65.0,
        "Fire Ant 3/4 Depth Perspective"
    ),
    (
        "fire_face_closeup.png",
        Vector((0.0, -2.4, 1.74)),
        Euler((math.radians(86), 0, 0), 'XYZ'),
        68.0,
        "Fire Ant Helmet, Shield with Red 'A' & Eyes Macro Close-up"
    ),
    (
        "fire_gameplay_angle.png",
        Vector((0.0, -6.2, 5.6)),
        Euler((math.radians(56), 0, 0), 'XYZ'),
        85.0,
        "Authentic 1998 RTS Gameplay Angled Top-Down South Perspective"
    )
]

for filename, pos, rot, lens, desc in views:
    cam_obj.location = pos
    cam_obj.rotation_euler = rot
    cam_data.lens = lens
    out_path = os.path.join(web_dir, filename)
    scene.render.filepath = out_path
    print(f"Rendering: {desc} -> {out_path}...")
    bpy.ops.render.render(write_still=True)
    print(f"Saved: {out_path}")

# -----------------------------------------------------------------------------
# 14. GLTF 2.0 Binary Export (fire_ant.glb) & USDZ
# -----------------------------------------------------------------------------
glb_path = os.path.join(web_dir, "fire_ant.glb")
for obj in fire_col.objects:
    if obj.type == 'MESH' and len(obj.data.uv_layers) == 0:
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.uv.smart_project(angle_limit=66.0, island_margin=0.02)
        bpy.ops.object.mode_set(mode='OBJECT')
        obj.select_set(False)

bpy.ops.object.select_all(action='DESELECT')
for obj in fire_col.objects:
    obj.select_set(True)

bpy.ops.export_scene.gltf(
    filepath=glb_path,
    export_format='GLB',
    use_selection=True,
    export_apply=False,
    export_animations=True
)
print(f"Fire Ant 3D GLB export complete: {glb_path}")

usdz_path = os.path.join(web_dir, "fire_ant.usdz")
print(f"Exporting Fire Ant USDZ to: {usdz_path}...")
bpy.ops.wm.usd_export(
    filepath=usdz_path,
    selected_objects_only=True,
    export_textures=True
)
print(f"Fire Ant USDZ export complete: {usdz_path}")

blend_path = "/Users/dchadd/Desktop/Ants-Mac/tools/blender/fire_ant_authentic.blend"
bpy.ops.wm.save_as_mainfile(filepath=blend_path)
print(f"Saved Blender file: {blend_path}")
