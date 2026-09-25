"""
Authentic 1998 Ants! 3D Title Scene Recreation in Blender 4.3.2
Master 1:1 High-Fidelity Reproduction of 1998 Cover Art & In-Game Sprite Models:
- Unified organic squircle ant head morphology based on sprite_1481 (Worker),
  sprite_1807 (Combat), sprite_0968 (Fire), sprite_2014 (Swimmer),
  sprite_1332 (Bomber), and sprite_2470 (Thief).
- Organic sage-green chitin exoskeleton with soft subsurface scattering.
- Expressive embedded eyes with sclera, glossy dark pupils, and specular glints.
- Combat Ant (top-left): muscular arms, fierce V-scowl brow, bandolier sash, gold canister, bazooka.
- Fire Ant (top-right): vintage firefighter chief helmet with crest ridge, curved brim, gold shield, maroon 'A'.
- Swimmer Ant (middle-left): oval scuba diving mask with glass lens, side snorkel tube, yellow shovel in hand.
- Bomber Ant (front-center): craned upward gazing at camera, yellow aviator goggles, bomb backpack, 2 front legs.
- Thief Ant (middle-right): yellow cloth bandit bandana with eye cutouts, knot with dual fluttering ribbon tails.
- Worker Ant (dead center): youthful rounded cranium, innocent forward eyes, central anchor.
- Elliptical crimson floor disc with authentic contact drop shadows.
- Foreground 3D stylized 'ants!' title logo with beveled geometry and purple/cyan iridescent shader.
- Rendered with Apple Silicon Metal Cycles ray tracing (128 samples, hardware denoised).
- Exports interactive 3D WebGL asset (GLB) and master still render (PNG).
"""

import math
import sys
import os

try:
    import bpy
    import bmesh
    import mathutils
except ImportError:
    print("Run via Blender CLI: /Applications/Blender.app/Contents/MacOS/Blender --background --python <script>")
    sys.exit(1)


def reset_scene():
    """Clear scene and configure Cycles render engine with Metal GPU."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene

    scene.render.engine = 'CYCLES'
    try:
        scene.cycles.device = 'GPU'
        prefs = bpy.context.preferences.addons['cycles'].preferences
        prefs.compute_device_type = 'METAL'
        for device in prefs.devices:
            device.use = True
        scene.cycles.samples = 36
        scene.cycles.use_denoising = True
    except Exception as e:
        print(f"Cycles GPU notice: {e}, using default device")

    scene.render.resolution_x = 1920
    scene.render.resolution_y = 1080
    scene.render.resolution_percentage = 100

    scene.display_settings.display_device = 'sRGB'
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'Medium High Contrast'


def create_pbr_material(name, base_color, roughness=0.4, metallic=0.0, coat=0.0, sss=0.0, transmission=0.0, ior=1.45, emissive=None):
    """Create Principled BSDF material compatible with Blender 4.x."""
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = base_color
        bsdf.inputs["Roughness"].default_value = roughness
        bsdf.inputs["Metallic"].default_value = metallic

        if "Coat Weight" in bsdf.inputs:
            bsdf.inputs["Coat Weight"].default_value = coat
            if "Coat Roughness" in bsdf.inputs:
                bsdf.inputs["Coat Roughness"].default_value = 0.08
        elif "Clearcoat" in bsdf.inputs:
            bsdf.inputs["Clearcoat"].default_value = coat

        if "Subsurface Weight" in bsdf.inputs and sss > 0:
            bsdf.inputs["Subsurface Weight"].default_value = sss
            if "Subsurface Radius" in bsdf.inputs:
                bsdf.inputs["Subsurface Radius"].default_value = (0.3, 0.4, 0.2)

        if "Transmission Weight" in bsdf.inputs and transmission > 0:
            bsdf.inputs["Transmission Weight"].default_value = transmission
        elif "Transmission" in bsdf.inputs and transmission > 0:
            bsdf.inputs["Transmission"].default_value = transmission

        if "IOR" in bsdf.inputs:
            bsdf.inputs["IOR"].default_value = ior

        if emissive and "Emission Color" in bsdf.inputs:
            bsdf.inputs["Emission Color"].default_value = emissive
            if "Emission Strength" in bsdf.inputs:
                bsdf.inputs["Emission Strength"].default_value = 1.0

    return mat


def setup_palette():
    """Authentic material palette matching 1998 CGI artwork and sprite catalog."""
    return {
        # Organic chitin: muted olive/sage green with subtle subsurface scattering
        'chitin': create_pbr_material("Chitin", (0.24, 0.36, 0.20, 1.0), roughness=0.50, coat=0.25, sss=0.12),
        'chitin_dark': create_pbr_material("DarkChitin", (0.17, 0.26, 0.15, 1.0), roughness=0.52, coat=0.20, sss=0.10),
        'chitin_light': create_pbr_material("LightChitin", (0.28, 0.40, 0.24, 1.0), roughness=0.48, coat=0.30, sss=0.14),

        # Expressive eyes
        'sclera': create_pbr_material("EyeSclera", (0.94, 0.95, 0.92, 1.0), roughness=0.10, coat=0.9),
        'pupil': create_pbr_material("EyePupil", (0.015, 0.015, 0.015, 1.0), roughness=0.04, coat=0.95),
        'glint': create_pbr_material("EyeGlint", (1.0, 1.0, 1.0, 1.0), roughness=0.0, emissive=(1.0, 1.0, 1.0, 1.0)),

        # Antennae & insect legs
        'black_horn': create_pbr_material("BlackHorn", (0.05, 0.05, 0.05, 1.0), roughness=0.40, coat=0.2),

        # Firefighter helmet
        'fire_helmet': create_pbr_material("FireHelmet", (0.96, 0.78, 0.04, 1.0), roughness=0.22, coat=0.8),
        'fire_shield': create_pbr_material("FireShield", (0.86, 0.68, 0.14, 1.0), roughness=0.30, metallic=0.3),
        'fire_letter_a': create_pbr_material("FireLetterA", (0.64, 0.08, 0.10, 1.0), roughness=0.32),
        'fire_collar': create_pbr_material("FireCollar", (0.90, 0.74, 0.05, 1.0), roughness=0.50),

        # Combat gear
        'combat_sash': create_pbr_material("CombatSash", (0.92, 0.75, 0.05, 1.0), roughness=0.55),
        'combat_canister': create_pbr_material("CombatCanister", (0.82, 0.68, 0.16, 1.0), roughness=0.25, metallic=0.7),
        'combat_bazooka': create_pbr_material("CombatBazooka", (0.18, 0.22, 0.16, 1.0), roughness=0.38, metallic=0.6),

        # Swimmer gear
        'swimmer_mask_rim': create_pbr_material("SwimmerMaskRim", (0.95, 0.80, 0.06, 1.0), roughness=0.35, coat=0.4),
        'swimmer_mask_glass': create_pbr_material("SwimmerMaskGlass", (0.60, 0.82, 0.90, 1.0), roughness=0.06, transmission=0.88, ior=1.45),
        'swimmer_mask_strap': create_pbr_material("SwimmerMaskStrap", (0.08, 0.08, 0.08, 1.0), roughness=0.50),
        'swimmer_snorkel': create_pbr_material("SwimmerSnorkel", (0.95, 0.65, 0.05, 1.0), roughness=0.35),
        'swimmer_shovel_shaft': create_pbr_material("ShovelShaft", (0.92, 0.76, 0.08, 1.0), roughness=0.40),
        'swimmer_shovel_blade': create_pbr_material("ShovelBlade", (0.42, 0.44, 0.46, 1.0), roughness=0.30, metallic=0.75),

        # Bomber gear
        'bomber_goggles_rim': create_pbr_material("BomberGogglesRim", (0.88, 0.72, 0.14, 1.0), roughness=0.28, metallic=0.5),
        'bomber_goggles_lens': create_pbr_material("BomberGogglesLens", (0.75, 0.85, 0.90, 1.0), roughness=0.08, transmission=0.82),
        'bomber_strap': create_pbr_material("BomberStrap", (0.12, 0.10, 0.08, 1.0), roughness=0.60),
        'bomber_pack': create_pbr_material("BomberPack", (0.34, 0.22, 0.12, 1.0), roughness=0.65),
        'bomber_bomb': create_pbr_material("BomberBomb", (0.07, 0.07, 0.07, 1.0), roughness=0.45, metallic=0.3),
        'bomber_fuse': create_pbr_material("BomberFuse", (0.75, 0.65, 0.45, 1.0), roughness=0.70),

        # Thief gear
        'thief_bandana': create_pbr_material("ThiefBandana", (0.92, 0.76, 0.05, 1.0), roughness=0.58),

        # Red floor disc
        'floor_crimson': create_pbr_material("FloorCrimson", (0.70, 0.16, 0.06, 1.0), roughness=0.42, coat=0.1),

        # Stylized 3D title logo
        'logo_purple': create_pbr_material("LogoPurple", (0.38, 0.22, 0.68, 1.0), roughness=0.25, coat=0.6),
        'logo_cyan': create_pbr_material("LogoCyan", (0.12, 0.65, 0.82, 1.0), roughness=0.25, coat=0.6),
    }


def add_subsurf(obj, levels=2):
    """Add subdivision surface modifier and smooth shading."""
    mod = obj.modifiers.new(name="Subsurf", type='SUBSURF')
    mod.levels = levels
    mod.render_levels = levels
    for poly in obj.data.polygons:
        poly.use_smooth = True


def smooth_all_faces(obj):
    """Ensure all faces on mesh use smooth shading."""
    for poly in obj.data.polygons:
        poly.use_smooth = True


def build_ant_head(name, root, mats, options=None):
    """
    Constructs an authentic 1998 CGI ant head based on sprite_1481:
    - Unified squircle cranium with rounded crown, subtle cheek flare, and eye socket recesses.
    - Sclera domes nestled flush into eye sockets.
    - Dark glossy pupils on the front surface of the eyes with specular catchlights.
    - Lower cheek lobes and small discrete downward mandibles.
    - Two curved insect antennae with elbow joints.
    """
    if options is None:
        options = {}

    head_scale = options.get('head_scale', (0.82, 0.70, 0.80))
    skin_mat = options.get('skin_mat', mats['chitin'])
    squint = options.get('squint', 1.0)
    has_antennae = options.get('has_antennae', True)
    has_mandibles = options.get('has_mandibles', True)

    scene = bpy.context.scene

    # 1. Base Cranium Mesh via BMesh
    mesh = bpy.data.meshes.new(f"{name}_CraniumMesh")
    head_obj = bpy.data.objects.new(f"{name}_Cranium", mesh)
    scene.collection.objects.link(head_obj)

    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.subdivide_edges(bm, edges=bm.edges, cuts=1, use_grid_fill=True)

    brow_scowl = options.get('brow_scowl', 0.0)

    # Deform vertices to match authentic 1998 ant head morphology:
    for v in bm.verts:
        v.co.x *= head_scale[0]
        v.co.y *= head_scale[1]
        v.co.z *= head_scale[2]

        # Flatten crown top slightly if z > 0.25
        if v.co.z > 0.20:
            v.co.z = 0.20 + (v.co.z - 0.20) * 0.65

        # Lower cheeks flare out
        if v.co.z < 0 and v.co.y < 0:
            flare = 1.0 + (-v.co.z) * 0.35
            v.co.x *= flare
            v.co.y *= (1.0 + (-v.co.z) * 0.2)

        # Eye socket indentation: mid height, front face
        if -0.1 < v.co.z < 0.22 and v.co.y < -0.1:
            if abs(v.co.x) > 0.12:
                v.co.y += 0.06

        # Integrated brow scowl (Combat Ant)
        if brow_scowl > 0 and 0.08 < v.co.z < 0.32 and v.co.y < -0.08:
            # Overhang forward
            v.co.y -= brow_scowl * 0.12
            # Slant center down into angry V
            center_weight = max(0.0, 1.0 - abs(v.co.x) / 0.45)
            v.co.z -= brow_scowl * 0.09 * center_weight

    bm.to_mesh(mesh)
    bm.free()

    head_obj.data.materials.append(skin_mat)
    add_subsurf(head_obj, 2)
    head_obj.parent = root

    # 2. Lower Cheeks / Jowls
    for sign in [-1, 1]:
        bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=16, radius=0.18)
        cheek = bpy.context.active_object
        cheek.name = f"{name}_Cheek_{sign}"
        cheek.location = (sign * 0.20, -0.26, -0.22)
        cheek.scale = (1.0, 0.75, 0.85)
        cheek.data.materials.append(skin_mat)
        add_subsurf(cheek, 1)
        cheek.parent = head_obj

    # 3. Small discrete mandibles
    if has_mandibles:
        for sign in [-1, 1]:
            bpy.ops.mesh.primitive_cone_add(vertices=12, radius1=0.06, radius2=0.015, depth=0.18)
            mand = bpy.context.active_object
            mand.name = f"{name}_Mandible_{sign}"
            mand.location = (sign * 0.10, -0.30, -0.38)
            mand.rotation_euler = (0.35, sign * -0.2, 0.0)
            mand.data.materials.append(skin_mat)
            smooth_all_faces(mand)
            mand.parent = head_obj

    # 4. Embedded Eyes
    eye_x = options.get('eye_x', 0.21)
    eye_y = options.get('eye_y', -0.25)
    eye_z = options.get('eye_z', 0.04)
    eye_radius = options.get('eye_radius', 0.20)
    pupil_offset_z = options.get('pupil_offset_z', 0.01)

    for sign in [-1, 1]:
        # Sclera (White eye dome)
        bpy.ops.mesh.primitive_uv_sphere_add(segments=28, ring_count=20, radius=eye_radius)
        eye = bpy.context.active_object
        eye.name = f"{name}_EyeSclera_{sign}"
        eye.location = (sign * eye_x, eye_y, eye_z)
        eye.scale = (0.95, 0.72, squint * 1.0)
        eye.data.materials.append(mats['sclera'])
        add_subsurf(eye, 1)
        eye.parent = head_obj

        # Pupil (Glossy dark disc on front surface)
        pupil_y = eye_y - (eye_radius * 0.72) - 0.008
        pupil_z = eye_z + pupil_offset_z
        bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=16, radius=eye_radius * 0.52)
        pup = bpy.context.active_object
        pup.name = f"{name}_Pupil_{sign}"
        pup.location = (sign * eye_x, pupil_y, pupil_z)
        pup.scale = (0.95, 0.18, squint * 0.95)
        pup.data.materials.append(mats['pupil'])
        smooth_all_faces(pup)
        pup.parent = head_obj

        # Specular glint catchlight
        bpy.ops.mesh.primitive_uv_sphere_add(segments=10, ring_count=8, radius=0.026)
        glint = bpy.context.active_object
        glint.name = f"{name}_Glint_{sign}"
        glint.location = (sign * eye_x + (sign * 0.025), pupil_y - 0.015, pupil_z + 0.032)
        glint.data.materials.append(mats['glint'])
        glint.parent = head_obj

    # 5. Antennae (Segmented elbow horns)
    if has_antennae:
        ant_rot_x = options.get('antenna_rot_x', -0.30)
        ant_spread = options.get('antenna_spread', 0.28)
        for sign in [-1, 1]:
            # Lower stalk
            bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.024, depth=0.34)
            stalk = bpy.context.active_object
            stalk.name = f"{name}_Antenna1_{sign}"
            stalk.location = (sign * 0.16, -0.10, 0.44)
            stalk.rotation_euler = (ant_rot_x, sign * ant_spread, 0.0)
            stalk.data.materials.append(mats['black_horn'])
            smooth_all_faces(stalk)
            stalk.parent = head_obj

            # Upper elbow stalk
            bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.020, depth=0.32)
            elbow = bpy.context.active_object
            elbow.name = f"{name}_Antenna2_{sign}"
            elbow.location = (sign * 0.23, -0.02, 0.65)
            elbow.rotation_euler = (0.22, sign * 0.52, 0.0)
            elbow.data.materials.append(mats['black_horn'])
            smooth_all_faces(elbow)
            elbow.parent = head_obj

            # Tip bulb
            bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=8, radius=0.035)
            bulb = bpy.context.active_object
            bulb.name = f"{name}_AntennaBulb_{sign}"
            bulb.location = (sign * 0.31, 0.05, 0.77)
            bulb.data.materials.append(mats['black_horn'])
            bulb.parent = head_obj

    return head_obj


def create_ant_torso(name, root, mats, options=None):
    """Create ant thorax and abdomen nestled under head."""
    if options is None:
        options = {}
    skin_mat = options.get('skin_mat', mats['chitin'])
    torso_scale = options.get('torso_scale', (0.50, 0.45, 0.48))
    torso_z = options.get('torso_z', -0.55)

    bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=16, radius=1.0)
    torso = bpy.context.active_object
    torso.name = f"{name}_Torso"
    torso.location = (0, 0.05, torso_z)
    torso.scale = torso_scale
    torso.data.materials.append(skin_mat)
    add_subsurf(torso, 1)
    torso.parent = root
    return torso


def create_ground_leg(name, root, mats, start_pos, knee_pos, foot_pos):
    """Create a clean two-segment insect leg with sharp knee joint contacting the floor."""
    leg_empty = bpy.data.objects.new(f"{name}_Leg", None)
    bpy.context.scene.collection.objects.link(leg_empty)
    leg_empty.parent = root

    p1 = mathutils.Vector(start_pos)
    p2 = mathutils.Vector(knee_pos)
    p3 = mathutils.Vector(foot_pos)

    # Femur (Upper leg)
    v1 = p2 - p1
    dist1 = v1.length
    bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.032, depth=dist1)
    femur = bpy.context.active_object
    femur.name = f"{name}_Femur"
    femur.location = (p1 + p2) * 0.5
    # Orient cylinder along v1
    v1_norm = v1.normalized()
    up = mathutils.Vector((0, 0, 1))
    rot_quat = up.rotation_difference(v1_norm)
    femur.rotation_euler = rot_quat.to_euler()
    femur.data.materials.append(mats['black_horn'])
    smooth_all_faces(femur)
    femur.parent = leg_empty

    # Knee joint
    bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=8, radius=0.042)
    knee = bpy.context.active_object
    knee.name = f"{name}_Knee"
    knee.location = p2
    knee.data.materials.append(mats['black_horn'])
    knee.parent = leg_empty

    # Tibia (Lower leg)
    v2 = p3 - p2
    dist2 = v2.length
    bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.024, depth=dist2)
    tibia = bpy.context.active_object
    tibia.name = f"{name}_Tibia"
    tibia.location = (p2 + p3) * 0.5
    v2_norm = v2.normalized()
    rot_quat2 = up.rotation_difference(v2_norm)
    tibia.rotation_euler = rot_quat2.to_euler()
    tibia.data.materials.append(mats['black_horn'])
    smooth_all_faces(tibia)
    tibia.parent = leg_empty

    return leg_empty


# =========================================================================
# Caste Builders
# =========================================================================

def build_worker_ant(scene_root, mats):
    """Worker Ant (Dead Center): innocent, friendly, youthful."""
    worker_root = bpy.data.objects.new("WorkerAnt", None)
    bpy.context.scene.collection.objects.link(worker_root)
    worker_root.parent = scene_root
    worker_root.location = (0.05, 0.05, 1.35)

    build_ant_head("Worker", worker_root, mats, {
        'head_scale': (0.80, 0.68, 0.78),
        'skin_mat': mats['chitin_light'],
        'eye_x': 0.20,
        'eye_y': -0.25,
        'eye_z': 0.05,
        'eye_radius': 0.21,
        'pupil_offset_z': 0.02,
        'antenna_rot_x': -0.28,
        'antenna_spread': 0.26,
    })
    create_ant_torso("Worker", worker_root, mats, {
        'skin_mat': mats['chitin_light'],
        'torso_scale': (0.48, 0.44, 0.46),
        'torso_z': -0.52,
    })

    # 2 slender legs reaching floor between Bomber and Thief
    create_ground_leg("Worker_L", worker_root, mats,
                      start_pos=(-0.25, 0.0, -0.40),
                      knee_pos=(-0.55, -0.30, -0.65),
                      foot_pos=(-0.45, -0.55, -1.35))
    create_ground_leg("Worker_R", worker_root, mats,
                      start_pos=(0.30, 0.0, -0.40),
                      knee_pos=(0.60, -0.25, -0.65),
                      foot_pos=(0.50, -0.50, -1.35))

    return worker_root


def build_combat_ant(scene_root, mats):
    """Combat Ant (Top Left): Muscular bicep arms, fierce V-scowl brow, bandolier sash, canister, bazooka."""
    combat_root = bpy.data.objects.new("CombatAnt", None)
    bpy.context.scene.collection.objects.link(combat_root)
    combat_root.parent = scene_root
    combat_root.location = (-1.15, 0.85, 2.05)

    # Head with fierce squint and integrated brow scowl
    head_obj = build_ant_head("Combat", combat_root, mats, {
        'head_scale': (0.88, 0.72, 0.82),
        'skin_mat': mats['chitin_dark'],
        'eye_x': 0.22,
        'eye_y': -0.26,
        'eye_z': 0.04,
        'eye_radius': 0.19,
        'squint': 0.65,  # Fierce narrow eyes
        'brow_scowl': 1.0,  # Angry V brow ridge integrated into cranium
        'pupil_offset_z': 0.0,
        'antenna_rot_x': -0.40,
        'antenna_spread': 0.35,
    })

    # Muscular broad chest/torso
    torso = create_ant_torso("Combat", combat_root, mats, {
        'skin_mat': mats['chitin_dark'],
        'torso_scale': (0.65, 0.52, 0.55),
        'torso_z': -0.55,
    })

    # Muscular arms (sprite_1807 signature bodybuilder pose)
    # Left arm (viewer's left): massive bicep and forearm
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=12, radius=0.22)
    bicep_l = bpy.context.active_object
    bicep_l.name = "Combat_Bicep_L"
    bicep_l.location = (-0.55, 0.0, -0.45)
    bicep_l.scale = (1.1, 0.85, 0.9)
    bicep_l.data.materials.append(mats['chitin_dark'])
    add_subsurf(bicep_l, 1)
    bicep_l.parent = combat_root

    bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.12, depth=0.45)
    forearm_l = bpy.context.active_object
    forearm_l.name = "Combat_Forearm_L"
    forearm_l.location = (-0.68, -0.15, -0.72)
    forearm_l.rotation_euler = (0.5, -0.4, 0.3)
    forearm_l.data.materials.append(mats['chitin_dark'])
    add_subsurf(forearm_l, 1)
    forearm_l.parent = combat_root

    # Right arm (viewer's right)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=12, radius=0.20)
    bicep_r = bpy.context.active_object
    bicep_r.name = "Combat_Bicep_R"
    bicep_r.location = (0.52, 0.02, -0.46)
    bicep_r.scale = (1.05, 0.85, 0.9)
    bicep_r.data.materials.append(mats['chitin_dark'])
    add_subsurf(bicep_r, 1)
    bicep_r.parent = combat_root

    # Yellow bandolier sash crossing chest
    bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=0.07, depth=1.0)
    sash = bpy.context.active_object
    sash.name = "Combat_Sash"
    sash.location = (-0.05, -0.22, -0.55)
    sash.rotation_euler = (0.3, 0.8, -0.2)
    sash.scale = (1.4, 0.4, 1.0)
    sash.data.materials.append(mats['combat_sash'])
    add_subsurf(sash, 1)
    sash.parent = combat_root

    # Gold ammo canister in center of sash
    bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=0.07, depth=0.25)
    can = bpy.context.active_object
    can.name = "Combat_Canister"
    can.location = (-0.05, -0.32, -0.55)
    can.rotation_euler = (0.3, 0.8, 0.0)
    can.data.materials.append(mats['combat_canister'])
    add_subsurf(can, 1)
    can.parent = combat_root

    # Bazooka rocket launcher tube over shoulder
    bpy.ops.mesh.primitive_cylinder_add(vertices=20, radius=0.10, depth=1.4)
    baz = bpy.context.active_object
    baz.name = "Combat_Bazooka"
    baz.location = (-0.52, 0.15, 0.15)
    baz.rotation_euler = (0.6, 0.25, -0.3)
    baz.data.materials.append(mats['combat_bazooka'])
    smooth_all_faces(baz)
    baz.parent = combat_root

    # Bazooka muzzle rim
    bpy.ops.mesh.primitive_torus_add(major_radius=0.11, minor_radius=0.025, major_segments=20, minor_segments=12)
    b_rim = bpy.context.active_object
    b_rim.name = "Combat_Bazooka_Rim"
    b_rim.location = (-0.68, -0.28, 0.65)
    b_rim.rotation_euler = (0.6, 0.25, -0.3)
    b_rim.data.materials.append(mats['combat_canister'])
    b_rim.parent = combat_root

    return combat_root


def build_fire_ant(scene_root, mats):
    """Fire Ant (Top Right): Firefighter chief helmet, crest, curved brim, gold shield, maroon 'A'."""
    fire_root = bpy.data.objects.new("FireAnt", None)
    bpy.context.scene.collection.objects.link(fire_root)
    fire_root.parent = scene_root
    fire_root.location = (1.10, 0.85, 2.10)

    # Head (no antennae because helmet covers forehead)
    head_obj = build_ant_head("Fire", fire_root, mats, {
        'head_scale': (0.80, 0.68, 0.76),
        'skin_mat': mats['chitin'],
        'eye_x': 0.19,
        'eye_y': -0.24,
        'eye_z': 0.02,
        'eye_radius': 0.19,
        'pupil_offset_z': 0.02,
        'has_antennae': False,
    })

    create_ant_torso("Fire", fire_root, mats, {
        'skin_mat': mats['chitin'],
        'torso_scale': (0.50, 0.45, 0.48),
        'torso_z': -0.52,
    })

    # Firefighter Chief Helmet:
    # 1. Crown dome
    bpy.ops.mesh.primitive_uv_sphere_add(segments=28, ring_count=20, radius=0.52)
    crown = bpy.context.active_object
    crown.name = "Fire_Helmet_Crown"
    crown.location = (0.0, 0.02, 0.32)
    crown.scale = (1.02, 1.08, 0.72)
    crown.data.materials.append(mats['fire_helmet'])
    add_subsurf(crown, 1)
    crown.parent = head_obj

    # 2. Center crest ridge (runs along top of crown from front to back)
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    crest = bpy.context.active_object
    crest.name = "Fire_Helmet_Crest"
    crest.location = (0.0, 0.02, 0.62)
    crest.scale = (0.08, 0.78, 0.16)
    crest.data.materials.append(mats['fire_helmet'])
    add_subsurf(crest, 1)
    crest.parent = crown

    # 3. Flared curved brim (wider in back for neck protection, dipping in front)
    bpy.ops.mesh.primitive_cone_add(vertices=32, radius1=0.78, radius2=0.52, depth=0.14)
    brim = bpy.context.active_object
    brim.name = "Fire_Helmet_Brim"
    brim.location = (0.0, 0.04, 0.14)
    brim.scale = (1.05, 1.25, 0.9)
    brim.rotation_euler = (0.12, 0.0, 0.0)  # Dip slightly in front
    brim.data.materials.append(mats['fire_helmet'])
    add_subsurf(brim, 1)
    brim.parent = crown

    # 4. Front upright shield badge
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    shield = bpy.context.active_object
    shield.name = "Fire_Helmet_Shield"
    shield.location = (0.0, -0.48, 0.36)
    shield.scale = (0.34, 0.04, 0.40)
    shield.rotation_euler = (-0.15, 0.0, 0.0)
    shield.data.materials.append(mats['fire_shield'])
    add_subsurf(shield, 1)
    shield.parent = crown

    # 5. Bold 3D Letter 'A' on shield
    try:
        font_curve = bpy.data.curves.new(type="FONT", name="FontCurve_A")
        font_curve.body = "A"
        font_curve.size = 0.34
        font_curve.extrude = 0.04
        font_curve.bevel_depth = 0.015
        text_a = bpy.data.objects.new("Fire_Letter_A", font_curve)
        bpy.context.scene.collection.objects.link(text_a)
        text_a.location = (-0.14, -0.52, 0.18)
        text_a.rotation_euler = (math.radians(98), 0.0, 0.0)
        text_a.data.materials.append(mats['fire_letter_a'])
        text_a.parent = crown
    except Exception as e:
        print("Letter A creation notice:", e)

    # Yellow collar
    bpy.ops.mesh.primitive_torus_add(major_radius=0.38, minor_radius=0.08, major_segments=24, minor_segments=12)
    collar = bpy.context.active_object
    collar.name = "Fire_Collar"
    collar.location = (0.0, 0.02, -0.34)
    collar.data.materials.append(mats['fire_collar'])
    add_subsurf(collar, 1)
    collar.parent = fire_root

    return fire_root


def build_swimmer_ant(scene_root, mats):
    """Swimmer Ant (Middle Left): Scuba mask with glass lens, snorkel tube, yellow shovel in hand."""
    swimmer_root = bpy.data.objects.new("SwimmerAnt", None)
    bpy.context.scene.collection.objects.link(swimmer_root)
    swimmer_root.parent = scene_root
    swimmer_root.location = (-1.35, -0.20, 1.15)

    head_obj = build_ant_head("Swimmer", swimmer_root, mats, {
        'head_scale': (0.80, 0.68, 0.76),
        'skin_mat': mats['chitin'],
        'eye_x': 0.19,
        'eye_y': -0.24,
        'eye_z': 0.04,
        'eye_radius': 0.19,
        'has_antennae': False,  # Mask & snorkel frame face
    })

    create_ant_torso("Swimmer", swimmer_root, mats, {
        'skin_mat': mats['chitin'],
        'torso_scale': (0.48, 0.44, 0.46),
        'torso_z': -0.52,
    })

    # Oval Scuba Mask Rim
    bpy.ops.mesh.primitive_torus_add(major_radius=0.40, minor_radius=0.075, major_segments=28, minor_segments=16)
    mask_rim = bpy.context.active_object
    mask_rim.name = "Swimmer_Mask_Rim"
    mask_rim.location = (0.0, -0.32, 0.04)
    mask_rim.scale = (1.15, 0.65, 0.85)
    mask_rim.rotation_euler = (math.radians(90), 0.0, 0.0)
    mask_rim.data.materials.append(mats['swimmer_mask_rim'])
    add_subsurf(mask_rim, 1)
    mask_rim.parent = head_obj

    # Glass lens inside mask
    bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=0.38, depth=0.02)
    glass = bpy.context.active_object
    glass.name = "Swimmer_Mask_Glass"
    glass.location = (0.0, 0.0, 0.0)
    glass.scale = (0.95, 0.72, 0.20)
    glass.rotation_euler = (0.0, 0.0, 0.0)
    glass.data.materials.append(mats['swimmer_mask_glass'])
    glass.parent = mask_rim

    # Mask rubber strap
    bpy.ops.mesh.primitive_torus_add(major_radius=0.52, minor_radius=0.035, major_segments=24, minor_segments=8)
    strap = bpy.context.active_object
    strap.name = "Swimmer_Mask_Strap"
    strap.location = (0.0, 0.0, 0.04)
    strap.scale = (0.95, 0.95, 0.6)
    strap.data.materials.append(mats['swimmer_mask_strap'])
    strap.parent = head_obj

    # Snorkel tube: attached on left side, curving up
    bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=0.040, depth=0.85)
    snorkel = bpy.context.active_object
    snorkel.name = "Swimmer_Snorkel"
    snorkel.location = (-0.46, -0.15, 0.42)
    snorkel.rotation_euler = (-0.15, -0.12, 0.0)
    snorkel.data.materials.append(mats['swimmer_snorkel'])
    add_subsurf(snorkel, 1)
    snorkel.parent = head_obj

    # Yellow Excavation Shovel in hand on left side
    # Shaft
    bpy.ops.mesh.primitive_cylinder_add(vertices=14, radius=0.032, depth=1.35)
    shaft = bpy.context.active_object
    shaft.name = "Swimmer_Shovel_Shaft"
    shaft.location = (-0.72, -0.22, -0.45)
    shaft.rotation_euler = (0.2, 0.15, -0.1)
    shaft.data.materials.append(mats['swimmer_shovel_shaft'])
    smooth_all_faces(shaft)
    shaft.parent = swimmer_root

    # Spade blade
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    blade = bpy.context.active_object
    blade.name = "Swimmer_Shovel_Blade"
    blade.location = (-0.78, -0.32, -1.05)
    blade.scale = (0.22, 0.04, 0.28)
    blade.rotation_euler = (0.2, 0.15, -0.1)
    blade.data.materials.append(mats['swimmer_shovel_blade'])
    add_subsurf(blade, 1)
    blade.parent = shaft

    # 2 left ground legs touching floor
    create_ground_leg("Swimmer_Leg1", swimmer_root, mats,
                      start_pos=(-0.25, 0.0, -0.42),
                      knee_pos=(-0.65, 0.15, -0.65),
                      foot_pos=(-0.85, 0.10, -1.15))
    create_ground_leg("Swimmer_Leg2", swimmer_root, mats,
                      start_pos=(-0.25, -0.15, -0.45),
                      knee_pos=(-0.58, -0.35, -0.70),
                      foot_pos=(-0.70, -0.45, -1.15))

    return swimmer_root


def build_bomber_ant(scene_root, mats):
    """Bomber Ant (Front Center / Lowest): Tilted up gazing at camera, aviator goggles, bomb backpack, 2 front legs."""
    bomber_root = bpy.data.objects.new("BomberAnt", None)
    bpy.context.scene.collection.objects.link(bomber_root)
    bomber_root.parent = scene_root
    bomber_root.location = (-0.15, -0.95, 0.65)
    # Head tilted upward towards the camera
    bomber_root.rotation_euler = (-0.52, 0.0, 0.0)

    head_obj = build_ant_head("Bomber", bomber_root, mats, {
        'head_scale': (0.80, 0.68, 0.76),
        'skin_mat': mats['chitin'],
        'eye_x': 0.20,
        'eye_y': -0.24,
        'eye_z': 0.04,
        'eye_radius': 0.20,
        'pupil_offset_z': 0.04,  # Looking up at camera!
        'antenna_rot_x': 0.60,  # Curve back over head away from Worker Ant
        'antenna_spread': 0.25,
    })

    create_ant_torso("Bomber", bomber_root, mats, {
        'skin_mat': mats['chitin'],
        'torso_scale': (0.46, 0.42, 0.44),
        'torso_z': -0.50,
    })

    # Aviator / Blast Goggles
    for sign in [-1, 1]:
        # Goggle brass rim
        bpy.ops.mesh.primitive_torus_add(major_radius=0.19, minor_radius=0.038, major_segments=24, minor_segments=12)
        rim = bpy.context.active_object
        rim.name = f"Bomber_Goggle_Rim_{sign}"
        rim.location = (sign * 0.20, -0.36, 0.05)
        rim.rotation_euler = (math.radians(90), 0.0, 0.0)
        rim.data.materials.append(mats['bomber_goggles_rim'])
        add_subsurf(rim, 1)
        rim.parent = head_obj

        # Goggle glass lens
        bpy.ops.mesh.primitive_cylinder_add(vertices=20, radius=0.18, depth=0.015)
        lens = bpy.context.active_object
        lens.name = f"Bomber_Goggle_Lens_{sign}"
        lens.location = (0.0, 0.0, 0.0)
        lens.rotation_euler = (0.0, 0.0, 0.0)
        lens.scale = (0.92, 0.92, 0.15)
        lens.data.materials.append(mats['bomber_goggles_lens'])
        lens.parent = rim

    # Goggle nose bridge
    bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.024, depth=0.16)
    bridge = bpy.context.active_object
    bridge.name = "Bomber_Goggle_Bridge"
    bridge.location = (0.0, -0.36, 0.05)
    bridge.rotation_euler = (0.0, math.radians(90), 0.0)
    bridge.data.materials.append(mats['bomber_goggles_rim'])
    bridge.parent = head_obj

    # Backpack with cartoon bombs
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    pack = bpy.context.active_object
    pack.name = "Bomber_Backpack"
    pack.location = (0.0, 0.35, -0.42)
    pack.scale = (0.42, 0.28, 0.38)
    pack.data.materials.append(mats['bomber_pack'])
    add_subsurf(pack, 1)
    pack.parent = bomber_root

    # Round cartoon bombs peeking out
    for b_idx, b_x in enumerate([-0.12, 0.12]):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=12, radius=0.12)
        bomb = bpy.context.active_object
        bomb.name = f"Bomber_Bomb_{b_idx}"
        bomb.location = (b_x, 0.36, -0.22)
        bomb.data.materials.append(mats['bomber_bomb'])
        add_subsurf(bomb, 1)
        bomb.parent = pack

        # Fuse
        bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.012, depth=0.10)
        fuse = bpy.context.active_object
        fuse.name = f"Bomber_Fuse_{b_idx}"
        fuse.location = (b_x, 0.36, -0.09)
        fuse.rotation_euler = (0.2, (b_idx - 0.5) * 0.4, 0.0)
        fuse.data.materials.append(mats['bomber_fuse'])
        fuse.parent = bomb

    # 2 front ground legs planted on floor in front (attached to scene_root with world coords)
    create_ground_leg("Bomber_Leg_L", scene_root, mats,
                      start_pos=(-0.35, -1.05, 0.35),
                      knee_pos=(-0.58, -1.35, 0.30),
                      foot_pos=(-0.48, -1.65, 0.0))
    create_ground_leg("Bomber_Leg_R", scene_root, mats,
                      start_pos=(0.05, -1.05, 0.35),
                      knee_pos=(0.28, -1.35, 0.30),
                      foot_pos=(0.18, -1.65, 0.0))

    return bomber_root


def build_thief_ant(scene_root, mats):
    """Thief Ant (Middle Right): Bandit bandana with eye cutouts, knot with dual fluttering ribbon tails."""
    thief_root = bpy.data.objects.new("ThiefAnt", None)
    bpy.context.scene.collection.objects.link(thief_root)
    thief_root.parent = scene_root
    thief_root.location = (1.30, -0.20, 1.05)

    head_obj = build_ant_head("Thief", thief_root, mats, {
        'head_scale': (0.80, 0.68, 0.76),
        'skin_mat': mats['chitin'],
        'eye_x': 0.19,
        'eye_y': -0.24,
        'eye_z': 0.04,
        'eye_radius': 0.19,
        'pupil_offset_z': 0.01,
        'antenna_rot_x': -0.35,
        'antenna_spread': 0.32,
    })

    create_ant_torso("Thief", thief_root, mats, {
        'skin_mat': mats['chitin'],
        'torso_scale': (0.48, 0.44, 0.46),
        'torso_z': -0.52,
    })

    # Bandit Bandana Mask (clean cloth band wrapped around forehead)
    bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=0.44, depth=0.22)
    bandana = bpy.context.active_object
    bandana.name = "Thief_Bandana"
    bandana.location = (0.0, -0.02, 0.16)
    bandana.scale = (0.92, 0.78, 1.0)
    bandana.data.materials.append(mats['thief_bandana'])
    smooth_all_faces(bandana)
    bandana.parent = head_obj

    # Bandana Knot on right temple (viewer's right)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=14, ring_count=10, radius=0.10)
    knot = bpy.context.active_object
    knot.name = "Thief_Bandana_Knot"
    knot.location = (0.42, -0.04, 0.16)
    knot.data.materials.append(mats['thief_bandana'])
    knot.parent = head_obj

    # 2 Fluttering Ribbon Tails pointing right
    for r_idx, (r_rot_z, r_len) in enumerate([(0.2, 0.42), (-0.25, 0.38)]):
        bpy.ops.mesh.primitive_cube_add(size=1.0)
        ribbon = bpy.context.active_object
        ribbon.name = f"Thief_Ribbon_{r_idx}"
        ribbon.location = (0.58 + (r_idx * 0.04), -0.05, 0.14 + (r_idx * -0.08))
        ribbon.scale = (r_len * 0.5, 0.03, 0.08)
        ribbon.rotation_euler = (0.1, 0.15, r_rot_z)
        ribbon.data.materials.append(mats['thief_bandana'])
        add_subsurf(ribbon, 1)
        ribbon.parent = knot

    # 2 right ground legs touching floor
    create_ground_leg("Thief_Leg1", thief_root, mats,
                      start_pos=(0.25, 0.0, -0.42),
                      knee_pos=(0.65, 0.10, -0.65),
                      foot_pos=(0.85, 0.08, -1.05))
    create_ground_leg("Thief_Leg2", thief_root, mats,
                      start_pos=(0.25, -0.15, -0.45),
                      knee_pos=(0.60, -0.32, -0.68),
                      foot_pos=(0.72, -0.40, -1.05))

    return thief_root


# =========================================================================
# Environment & Staging
# =========================================================================

def build_crimson_floor_disc(scene_root, mats):
    """Elliptical crimson floor disc with authentic bevel and contact shadow surface."""
    bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=3.2, depth=0.08)
    disc = bpy.context.active_object
    disc.name = "Floor_Crimson_Disc"
    disc.location = (0.0, 0.40, 0.0)
    disc.scale = (1.05, 0.78, 1.0)  # Perspective ellipse
    disc.data.materials.append(mats['floor_crimson'])
    smooth_all_faces(disc)
    disc.parent = scene_root
    return disc


def build_3d_title_logo(scene_root, mats):
    """
    3D stylized title 'ants!' with beveled geometry and purple/cyan iridescent shader
    matching sprite_0162.
    """
    logo_root = bpy.data.objects.new("TitleLogo", None)
    bpy.context.scene.collection.objects.link(logo_root)
    logo_root.parent = scene_root
    logo_root.location = (0.0, -2.60, 0.22)
    logo_root.rotation_euler = (math.radians(72), 0.0, 0.0)

    letters = [
        ('a', -1.20, mats['logo_purple'], 0.85),
        ('n', -0.58, mats['logo_cyan'], 0.85),
        ('t',  0.08, mats['logo_cyan'], 0.85),
        ('s',  0.64, mats['logo_purple'], 0.85),
        ('!',  1.18, mats['logo_purple'], 0.88),
    ]

    for char, x_pos, mat, size in letters:
        try:
            fc = bpy.data.curves.new(type="FONT", name=f"LogoChar_{char}")
            fc.body = char
            fc.size = size
            fc.extrude = 0.10
            fc.bevel_depth = 0.025
            fc.bevel_resolution = 4
            tobj = bpy.data.objects.new(f"Logo_{char}", fc)
            bpy.context.scene.collection.objects.link(tobj)
            tobj.location = (x_pos, 0.0, 0.0)
            tobj.data.materials.append(mat)
            tobj.parent = logo_root
        except Exception as e:
            print(f"Logo char {char} notice: {e}")

    return logo_root


def setup_lighting(scene_root):
    """Cinematic studio lighting matching 1998 cover art key-fill-rim setup."""
    scene = bpy.context.scene

    # 1. Warm Key Sun Light (Upper-left front)
    key_data = bpy.data.lights.new("KeyLight", type='SUN')
    key_data.energy = 5.2
    key_data.color = (1.0, 0.96, 0.90)
    key_data.angle = math.radians(2.0)  # Clean, realistic shadow softness
    key_obj = bpy.data.objects.new("KeyLight", key_data)
    scene.collection.objects.link(key_obj)
    key_obj.location = (-5.0, -6.5, 8.0)
    key_obj.rotation_euler = (math.radians(48), math.radians(-18), math.radians(-32))
    key_obj.parent = scene_root

    # 2. Cool Fill Area Light (Right side)
    fill_data = bpy.data.lights.new("FillLight", type='AREA')
    fill_data.energy = 160.0
    fill_data.size = 3.5
    fill_data.color = (0.75, 0.88, 1.0)
    fill_obj = bpy.data.objects.new("FillLight", fill_data)
    scene.collection.objects.link(fill_obj)
    fill_obj.location = (5.5, -4.0, 4.0)
    fill_obj.parent = scene_root

    # 3. Top-Back Rim Spot Light (Highlights character crowns against black void)
    rim_data = bpy.data.lights.new("RimLight", type='SPOT')
    rim_data.energy = 350.0
    rim_data.spot_size = math.radians(65)
    rim_data.spot_blend = 0.35
    rim_data.color = (0.90, 0.92, 1.0)
    rim_obj = bpy.data.objects.new("RimLight", rim_data)
    scene.collection.objects.link(rim_obj)
    rim_obj.location = (0.0, 4.5, 6.0)
    rim_obj.rotation_euler = (math.radians(-50), 0.0, 0.0)
    rim_obj.parent = scene_root

    # Pure black world background
    world = bpy.data.worlds.new("VoidWorld")
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    if bg:
        bg.inputs["Color"].default_value = (0.0, 0.0, 0.0, 1.0)
        bg.inputs["Strength"].default_value = 0.0
    scene.world = world


def setup_camera():
    """Portrait telephoto camera positioned along -Y looking towards +Y."""
    scene = bpy.context.scene
    cam_data = bpy.data.cameras.new("MasterCamera")
    cam_data.lens = 54  # 54mm frames full composition from helmet crown to title logo
    cam_data.clip_start = 0.1
    cam_data.clip_end = 100.0

    cam_obj = bpy.data.objects.new("MasterCamera", cam_data)
    scene.collection.objects.link(cam_obj)
    scene.camera = cam_obj

    # Position at -Y looking along +Y, elevated at Z=3.8
    cam_obj.location = (0.0, -10.2, 3.8)
    cam_obj.rotation_euler = (math.radians(73.5), 0.0, 0.0)
    return cam_obj


def export_scene(blend_path, glb_path, render_path):
    """Save .blend file, export GLB, and render high-resolution still."""
    os.makedirs(os.path.dirname(blend_path), exist_ok=True)
    os.makedirs(os.path.dirname(glb_path), exist_ok=True)
    os.makedirs(os.path.dirname(render_path), exist_ok=True)

    # 1. Save master .blend
    bpy.ops.wm.save_as_mainfile(filepath=blend_path)
    print(f"Master blend saved: {blend_path}")

    # 2. Export GLB for WebGL interactive viewer
    print(f"Exporting GLB to {glb_path}...")
    bpy.ops.export_scene.gltf(
        filepath=glb_path,
        export_format='GLB',
        use_selection=False,
        export_apply=True,
        export_yup=True,
    )
    print(f"GLB export complete: {glb_path}")

    # 3. Render master still
    scene = bpy.context.scene
    scene.render.filepath = render_path
    print(f"Rendering master still with Cycles ({scene.cycles.samples} samples)...")
    bpy.ops.render.render(write_still=True)
    print(f"Master render complete: {render_path}")


def main():
    print("=== Building 1:1 Authentic 1998 Ants! 3D Title Scene ===")
    reset_scene()

    scene_root = bpy.data.objects.new("SceneRoot", None)
    bpy.context.scene.collection.objects.link(scene_root)

    mats = setup_palette()
    print("Palette initialized.")

    # Build the 6 iconic ant castes
    print("Building Worker Ant (center)...")
    build_worker_ant(scene_root, mats)

    print("Building Combat Ant (top-left)...")
    build_combat_ant(scene_root, mats)

    print("Building Fire Ant (top-right)...")
    build_fire_ant(scene_root, mats)

    print("Building Swimmer Ant (middle-left)...")
    build_swimmer_ant(scene_root, mats)

    print("Building Bomber Ant (front-center)...")
    build_bomber_ant(scene_root, mats)

    print("Building Thief Ant (middle-right)...")
    build_thief_ant(scene_root, mats)

    print("Building Crimson Floor Disc...")
    build_crimson_floor_disc(scene_root, mats)

    print("Building 3D 'ants!' Title Logo...")
    build_3d_title_logo(scene_root, mats)

    print("Setting up cinematic lighting & camera...")
    setup_lighting(scene_root)
    setup_camera()

    blend_file = "/Users/dchadd/Desktop/Ants-Mac/tools/blender/authentic_ants_scene.blend"
    glb_file = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d/ants_scene.glb"
    render_file = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d/authentic_ants_render.png"

    export_scene(blend_file, glb_file, render_file)
    print("=== All generation tasks completed successfully! ===")


if __name__ == '__main__':
    main()
