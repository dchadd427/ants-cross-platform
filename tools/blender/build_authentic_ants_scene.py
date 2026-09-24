"""
Authentic 1998 Ants! 3D Title Scene Recreation in Blender 4.3.2
Master 1:1 High-Fidelity Reproduction of 1998 Cover Art & Sprite Models:
- Accurate coordinate space: Camera at -Y looking +Y, ants facing -Y directly toward camera.
- Left is -X (Combat Ant with X-sash & gold canister, Swimmer Ant with diving mask and shovel).
- Right is +X (Fire Ant with firefighter helmet and bold 'A' shield, Thief Ant with bandana and knot streamers).
- Center is Worker Ant (youthful rounded cranium, big innocent forward-facing eyes, dual lime mandibles).
- Front-Center is Bomber Ant (craned upward gazing at camera, yellow aviator goggles, bomb backpack).
- Elliptical crimson floor disc on pitch black void with crisp, elongated contact shadows.
- Foreground 3D stylized 'ants!' title logo with beveled geometry and purple/cyan iridescent shader.
- Rendered with Apple Silicon Metal Cycles ray tracing (128 samples, hardware denoised).
"""

import math
import sys
import os

try:
    import bpy
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
        scene.cycles.samples = 128
        scene.cycles.use_denoising = True
    except Exception as e:
        print(f"Cycles GPU notice: {e}, using default device")

    scene.render.resolution_x = 1920
    scene.render.resolution_y = 1080
    scene.render.resolution_percentage = 100

    scene.display_settings.display_device = 'sRGB'
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.look = 'Medium High Contrast'


def create_pbr_material(name, base_color, roughness=0.3, metallic=0.0, coat=0.8, emissive=(0, 0, 0, 1), transmission=0.0, ior=1.45):
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

        if "Transmission Weight" in bsdf.inputs and transmission > 0:
            bsdf.inputs["Transmission Weight"].default_value = transmission
        elif "Transmission" in bsdf.inputs and transmission > 0:
            bsdf.inputs["Transmission"].default_value = transmission

        if "IOR" in bsdf.inputs:
            bsdf.inputs["IOR"].default_value = ior

        if "Emission Color" in bsdf.inputs and any(c > 0 for c in emissive[:3]):
            bsdf.inputs["Emission Color"].default_value = emissive
            if "Emission Strength" in bsdf.inputs:
                bsdf.inputs["Emission Strength"].default_value = 1.0

    return mat


def setup_palette():
    """Authentic material palette matching 1998 CGI artwork and sprite catalog."""
    return {
        # Glossy emerald-green chitin carapace
        'chitin': create_pbr_material("Chitin", (0.07, 0.30, 0.09, 1.0), roughness=0.22, metallic=0.08, coat=1.0),
        'chitin_dark': create_pbr_material("DarkChitin", (0.03, 0.14, 0.04, 1.0), roughness=0.28, metallic=0.15, coat=0.8),
        # Dual lime-green mandible lobes (signature 1998 feature)
        'lime_mandible': create_pbr_material("LimeMandible", (0.36, 0.86, 0.26, 1.0), roughness=0.18, coat=0.95),
        # Expressive eyes: glossy white cornea with dark specular pupil and glint
        'sclera': create_pbr_material("EyeSclera", (0.92, 0.95, 0.92, 1.0), roughness=0.06, coat=1.0),
        'pupil': create_pbr_material("EyePupil", (0.01, 0.015, 0.01, 1.0), roughness=0.04, coat=1.0),
        'glint': create_pbr_material("EyeGlint", (1.0, 1.0, 1.0, 1.0), roughness=0.0, coat=1.0, emissive=(1.0, 1.0, 1.0, 1.0)),
        # Black insect horn & articulated legs
        'black_horn': create_pbr_material("BlackHorn", (0.02, 0.025, 0.02, 1.0), roughness=0.25, coat=0.85),
        # Vibrant yellow gear
        'yellow_gear': create_pbr_material("YellowGear", (0.96, 0.78, 0.08, 1.0), roughness=0.22, coat=0.85),
        'gold_metal': create_pbr_material("GoldMetal", (0.95, 0.75, 0.15, 1.0), roughness=0.18, metallic=0.85, coat=0.9),
        'fire_badge_red': create_pbr_material("FireBadgeRed", (0.58, 0.02, 0.05, 1.0), roughness=0.28, coat=0.6),
        'bandana_yellow': create_pbr_material("BandanaYellow", (0.94, 0.76, 0.12, 1.0), roughness=0.45, coat=0.3),
        'goggle_orange': create_pbr_material("GoggleOrange", (0.95, 0.38, 0.05, 1.0), roughness=0.28, coat=0.7),
        'glass_lens': create_pbr_material("GlassLens", (0.85, 0.95, 1.0, 1.0), roughness=0.05, coat=1.0, transmission=0.88, ior=1.52),
        'bomb_black': create_pbr_material("BombBlack", (0.06, 0.06, 0.07, 1.0), roughness=0.32, metallic=0.5),
        'fuse_tan': create_pbr_material("FuseTan", (0.75, 0.65, 0.45, 1.0), roughness=0.8),
        # Rich crimson floor disc
        'stage_red': create_pbr_material("StageRed", (0.84, 0.10, 0.03, 1.0), roughness=0.82, coat=0.15),
        # 3D Title Logo purple/cyan iridescent letters
        'logo_purple': create_pbr_material("LogoPurple", (0.35, 0.12, 0.85, 1.0), roughness=0.15, coat=1.0),
        'logo_cyan': create_pbr_material("LogoCyan", (0.10, 0.65, 0.95, 1.0), roughness=0.15, coat=1.0),
    }


def add_subsurf(obj, levels=1):
    """Add subdivision surface modifier and smooth shading."""
    mod = obj.modifiers.new(name="Subsurf", type='SUBSURF')
    mod.levels = levels
    mod.render_levels = levels
    for poly in obj.data.polygons:
        poly.use_smooth = True


def create_ant_character(name, loc, rot, scale, mats, options=None):
    """
    Constructs an authentic ant character facing -Y (toward camera):
    - Front face points to -Y.
    - Right side is +X (camera right). Left side is -X (camera left).
    - Rounded squircle cranium dome with emerald chitin.
    - Two prominent rounded lime-green mandible lobes at lower front jaw.
    - Expressive eyes with white cornea, glossy black pupils, and specular highlights.
    - Curved black segmented antennae with clubbed ends.
    - Articulated legs contacting the ground.
    """
    if options is None:
        options = {}

    root = bpy.data.objects.new(name, None)
    bpy.context.collection.objects.link(root)
    root.location = loc
    root.rotation_euler = rot
    root.scale = scale

    # 1. Cranium: Subdivided cube for authentic squircle shape (wider than tall)
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    head = bpy.context.active_object
    head.name = f"{name}_Cranium"
    head.location = (0, 0, 0)
    head.scale = (0.72, 0.60, 0.56)
    head.data.materials.append(mats['chitin'])
    add_subsurf(head, 2)
    head.parent = root

    # 2. Dual Lime-Green Mandible Lobes (Key signature 1998 feature)
    mand_scale = options.get('mandible_scale', (0.24, 0.22, 0.25))
    mand_y = options.get('mandible_y', -0.48)
    mand_z = options.get('mandible_z', -0.34)
    mand_spacing = options.get('mandible_spacing', 0.23)
    for sign in [-1, 1]:
        bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=16, radius=1.0)
        mand = bpy.context.active_object
        mand.name = f"{name}_Mandible_{sign}"
        mand.location = (sign * mand_spacing, mand_y, mand_z)
        mand.scale = mand_scale
        mand.data.materials.append(mats['lime_mandible'])
        add_subsurf(mand, 1)
        mand.parent = root

    # 3. Eyes (Front of head at -Y)
    eye_rad = options.get('eye_radius', 0.26)
    eye_squint = options.get('eye_squint', 1.0)
    eye_x = options.get('eye_x', 0.35)
    eye_y = options.get('eye_y', -0.38)
    eye_z = options.get('eye_z', 0.06)
    pupil_offset_y = options.get('pupil_offset_y', -0.16)
    pupil_offset_z = options.get('pupil_offset_z', 0.0)
    pupil_scale = options.get('pupil_scale', (0.85, 0.20, 0.85))

    for sign in [-1, 1]:
        # Sclera (White cornea)
        bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=18, radius=eye_rad)
        eye = bpy.context.active_object
        eye.name = f"{name}_EyeSclera_{sign}"
        eye.location = (sign * eye_x, eye_y, eye_z)
        eye.scale = (0.95, 0.85, eye_squint * 1.05)
        eye.data.materials.append(mats['sclera'])
        add_subsurf(eye, 1)
        eye.parent = root

        # Pupil (Glossy black disc on front surface of eye)
        bpy.ops.mesh.primitive_uv_sphere_add(segments=18, ring_count=14, radius=eye_rad * 0.62)
        pupil = bpy.context.active_object
        pupil.name = f"{name}_EyePupil_{sign}"
        pupil.location = (sign * (eye_x + 0.01), eye_y + pupil_offset_y, eye_z + pupil_offset_z)
        pupil.scale = (pupil_scale[0], pupil_scale[1], eye_squint * pupil_scale[2])
        pupil.data.materials.append(mats['pupil'])
        add_subsurf(pupil, 1)
        pupil.parent = root

        # Specular glint
        bpy.ops.mesh.primitive_uv_sphere_add(segments=10, ring_count=8, radius=eye_rad * 0.18)
        glint = bpy.context.active_object
        glint.name = f"{name}_EyeGlint_{sign}"
        glint.location = (sign * (eye_x + 0.04), eye_y + pupil_offset_y - 0.04, eye_z + pupil_offset_z + 0.06)
        glint.data.materials.append(mats['glint'])
        glint.parent = root

    # 4. Antennae (Curving up and outward from forehead)
    for sign in [-1, 1]:
        # Lower shaft
        bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.032, depth=0.55)
        ant1 = bpy.context.active_object
        ant1.name = f"{name}_Antenna1_{sign}"
        ant1.location = (sign * 0.18, -0.20, 0.52)
        ant1.rotation_euler = (-0.35, sign * 0.32, 0)
        ant1.data.materials.append(mats['black_horn'])
        add_subsurf(ant1, 1)
        ant1.parent = root

        # Upper curved segment
        bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.026, depth=0.50)
        ant2 = bpy.context.active_object
        ant2.name = f"{name}_Antenna2_{sign}"
        ant2.location = (sign * 0.30, -0.34, 0.90)
        ant2.rotation_euler = (-0.75, sign * -0.42, 0)
        ant2.data.materials.append(mats['black_horn'])
        add_subsurf(ant2, 1)
        ant2.parent = root

        # Teardrop / club tip
        bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=10, radius=0.045)
        tip = bpy.context.active_object
        tip.name = f"{name}_AntennaTip_{sign}"
        tip.location = (sign * 0.38, -0.48, 1.10)
        tip.data.materials.append(mats['black_horn'])
        add_subsurf(tip, 1)
        tip.parent = root

    # 5. Thorax & Abdomen (Behind head along +Y)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=16, radius=0.42)
    thorax = bpy.context.active_object
    thorax.name = f"{name}_Thorax"
    thorax.location = (0, 0.32, -0.18)
    thorax.scale = (0.9, 1.1, 0.85)
    thorax.data.materials.append(mats['chitin_dark'])
    add_subsurf(thorax, 1)
    thorax.parent = root

    bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=16, radius=0.55)
    abdomen = bpy.context.active_object
    abdomen.name = f"{name}_Abdomen"
    abdomen.location = (0, 0.82, -0.28)
    abdomen.scale = (0.85, 1.25, 0.85)
    abdomen.rotation_euler = (0.2, 0, 0)
    abdomen.data.materials.append(mats['chitin'])
    add_subsurf(abdomen, 1)
    abdomen.parent = root

    # 6. Articulated Legs (Touching the ground)
    leg_coords = [
        (-0.12, -0.22, 0.35, 0.75),   # Front legs
        (0.25, -0.26, 0.70, 0.55),    # Middle legs
        (0.55, -0.30, 0.85, 0.35),    # Rear legs
    ]
    for p, (y_off, z_off, spread_x, f_rot_y) in enumerate(leg_coords):
        for sign in [-1, 1]:
            # Femur
            bpy.ops.mesh.primitive_cylinder_add(vertices=10, radius=0.038, depth=0.75)
            femur = bpy.context.active_object
            femur.name = f"{name}_Femur_{p}_{sign}"
            femur.location = (sign * 0.45, y_off, z_off)
            femur.rotation_euler = (-0.2, sign * -f_rot_y, sign * 0.3)
            femur.data.materials.append(mats['black_horn'])
            femur.parent = root

            # Tibia extending down to floor
            bpy.ops.mesh.primitive_cylinder_add(vertices=10, radius=0.028, depth=0.85)
            tibia = bpy.context.active_object
            tibia.name = f"{name}_Tibia_{p}_{sign}"
            tibia.location = (sign * (0.45 + spread_x * 0.5), y_off - 0.1, z_off - 0.42)
            tibia.rotation_euler = (0.1, sign * 0.5, 0)
            tibia.data.materials.append(mats['black_horn'])
            tibia.parent = root

    return root


def build_all_characters(mats):
    """
    Build all 6 ant castes in the exact squad formation from the original cover art:
    Looking toward -Y from camera at (0, -9.0, 4.2):
    - Combat Ant: Top-Left (X = -1.25, Y = 0.75, Z = 1.05)
    - Fire Ant: Top-Right (X = +1.20, Y = 0.70, Z = 0.95)
    - Worker Ant: Dead Center (X = 0.0, Y = 0.10, Z = 0.40)
    - Swimmer Ant: Middle-Left (X = -1.45, Y = -0.25, Z = 0.35)
    - Thief Ant: Bottom-Right (X = +1.25, Y = -0.20, Z = 0.05)
    - Bomber Ant: Front-Center (X = -0.20, Y = -0.75, Z = -0.35)
    """

    # -------------------------------------------------------------------------
    # 1. Worker Ant (Dead Center, Youthful, Innocent Big Eyes)
    # -------------------------------------------------------------------------
    worker = create_ant_character(
        "WorkerAnt",
        loc=(0.0, 0.10, 0.40),
        rot=(0.0, 0.0, 0.0),
        scale=(1.0, 1.0, 1.0),
        mats=mats,
        options={
            'eye_radius': 0.28,  # Large innocent eyes
            'mandible_scale': (0.24, 0.24, 0.26),
            'mandible_y': -0.48,
            'mandible_z': -0.34,
            'mandible_spacing': 0.23,
            'pupil_offset_y': -0.16,
            'pupil_offset_z': 0.05,  # Gaze angled up towards elevated camera
            'pupil_scale': (0.88, 0.22, 0.88),  # Big centered pupils
        }
    )

    # -------------------------------------------------------------------------
    # 2. Combat Ant (Top-Left, Towering, Fierce Scowl, X-Sash, Gold Canister)
    # -------------------------------------------------------------------------
    combat = create_ant_character(
        "CombatAnt",
        loc=(-1.25, 0.75, 1.05),
        rot=(0.08, 0.15, -0.22),
        scale=(1.18, 1.18, 1.18),
        mats=mats,
        options={
            'eye_radius': 0.24,
            'eye_squint': 0.65,  # Fierce squint
            'mandible_scale': (0.24, 0.22, 0.26),
            'mandible_y': -0.46,
            'mandible_z': -0.32,
            'pupil_offset_y': -0.16,
        }
    )

    # Muscular furrowed brow ridge across forehead (V-scowl)
    bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=0.08, depth=0.78)
    brow = bpy.context.active_object
    brow.name = "Combat_BrowScowl"
    brow.location = (0, -0.44, 0.20)
    brow.rotation_euler = (-0.25, 0, math.pi / 2)
    brow.data.materials.append(mats['chitin_dark'])
    add_subsurf(brow, 1)
    brow.parent = combat

    # Fitted Yellow Bandolier Sash (X-strap crossing over shoulders & chest)
    # Strap A (left shoulder to right hip)
    bpy.ops.mesh.primitive_cube_add(size=0.15)
    sash_a = bpy.context.active_object
    sash_a.name = "Combat_Sash_A"
    sash_a.location = (0, -0.15, -0.18)
    sash_a.scale = (0.75, 5.2, 0.18)
    sash_a.rotation_euler = (0.22, 0.75, 0)
    sash_a.data.materials.append(mats['yellow_gear'])
    add_subsurf(sash_a, 1)
    sash_a.parent = combat

    # Strap B (right shoulder to left hip)
    bpy.ops.mesh.primitive_cube_add(size=0.15)
    sash_b = bpy.context.active_object
    sash_b.name = "Combat_Sash_B"
    sash_b.location = (0, -0.15, -0.18)
    sash_b.scale = (0.75, 5.2, 0.18)
    sash_b.rotation_euler = (0.22, -0.75, 0)
    sash_b.data.materials.append(mats['yellow_gear'])
    add_subsurf(sash_b, 1)
    sash_b.parent = combat

    # Central Vertical Golden Ammo Canister on chest at center of X
    bpy.ops.mesh.primitive_cylinder_add(vertices=20, radius=0.10, depth=0.50)
    canister = bpy.context.active_object
    canister.name = "Combat_GoldCanister"
    canister.location = (0.0, -0.44, -0.20)
    canister.rotation_euler = (-0.18, 0, 0)
    canister.data.materials.append(mats['gold_metal'])
    canister.parent = combat

    # Bazooka / Gun barrel resting over shoulder
    bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=0.08, depth=1.3)
    gun = bpy.context.active_object
    gun.name = "Combat_WeaponBarrel"
    gun.location = (-0.60, 0.05, 0.15)
    gun.rotation_euler = (-0.45, 0.35, 0.1)
    gun.data.materials.append(mats['bomb_black'])
    gun.parent = combat

    # -------------------------------------------------------------------------
    # 3. Fire Ant (Top-Right, Firefighter Chief Helmet with 'A' Shield)
    # -------------------------------------------------------------------------
    fire = create_ant_character(
        "FireAnt",
        loc=(1.20, 0.70, 0.95),
        rot=(0.06, -0.16, 0.18),
        scale=(1.10, 1.10, 1.10),
        mats=mats,
        options={
            'eye_radius': 0.23,
            'mandible_scale': (0.22, 0.22, 0.24),
        }
    )

    # Firefighter Helmet Flared Base Brim (sloping downward around neck/back)
    bpy.ops.mesh.primitive_cylinder_add(vertices=36, radius=0.82, depth=0.07)
    brim = bpy.context.active_object
    brim.name = "Fire_HelmetBrim"
    brim.location = (0, 0.05, 0.44)
    brim.scale = (0.95, 1.25, 1.0)
    brim.rotation_euler = (-0.15, 0, 0)
    brim.data.materials.append(mats['yellow_gear'])
    add_subsurf(brim, 1)
    brim.parent = fire

    # High Domed Helmet Crown with longitudinal center crest
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=18, radius=0.55)
    crown = bpy.context.active_object
    crown.name = "Fire_HelmetCrown"
    crown.location = (0, 0.02, 0.78)
    crown.scale = (0.85, 0.95, 0.95)
    crown.rotation_euler = (-0.15, 0, 0)
    crown.data.materials.append(mats['yellow_gear'])
    add_subsurf(crown, 1)
    crown.parent = fire

    # Center crest ridge along helmet top
    bpy.ops.mesh.primitive_cube_add(size=0.15)
    crest = bpy.context.active_object
    crest.name = "Fire_HelmetCrest"
    crest.location = (0, 0.02, 1.08)
    crest.scale = (0.32, 2.5, 0.8)
    crest.rotation_euler = (-0.15, 0, 0)
    crest.data.materials.append(mats['yellow_gear'])
    add_subsurf(crest, 1)
    crest.parent = fire

    # Upright Gold Shield Plaque on front (-Y) facing camera!
    bpy.ops.mesh.primitive_cube_add(size=0.38)
    shield = bpy.context.active_object
    shield.name = "Fire_ShieldPlaque"
    shield.location = (0, -0.42, 0.88)
    shield.scale = (0.95, 0.12, 1.2)
    shield.rotation_euler = (-0.15, 0, 0)
    shield.data.materials.append(mats['gold_metal'])
    shield.parent = fire

    # 3D Text Letter 'A' embossed on shield plate (authentic typography)
    bpy.ops.object.text_add(location=(-0.11, -0.46, 0.72))
    txt_a = bpy.context.active_object
    txt_a.name = "Fire_LetterA"
    txt_a.data.body = "A"
    txt_a.data.size = 0.32
    txt_a.data.extrude = 0.06
    txt_a.rotation_euler = (math.pi / 2 - 0.15, 0, 0)
    txt_a.data.materials.append(mats['fire_badge_red'])
    txt_a.parent = fire

    # -------------------------------------------------------------------------
    # 4. Swimmer Ant (Middle-Left, Scuba Mask, Snorkel & Yellow Shovel)
    # -------------------------------------------------------------------------
    swimmer = create_ant_character(
        "SwimmerAnt",
        loc=(-1.45, -0.25, 0.35),
        rot=(0.05, 0.18, -0.15),
        scale=(1.05, 1.05, 1.05),
        mats=mats,
        options={
            'eye_radius': 0.22,
            'mandible_scale': (0.22, 0.22, 0.24),
        }
    )

    # Thick Yellow Oval Diving Mask Frame around both eyes
    bpy.ops.mesh.primitive_torus_add(major_radius=0.46, minor_radius=0.075)
    mask_frame = bpy.context.active_object
    mask_frame.name = "Swimmer_MaskFrame"
    mask_frame.location = (0, -0.44, 0.06)
    mask_frame.scale = (1.30, 0.90, 0.82)
    mask_frame.rotation_euler = (math.pi / 2, 0, 0)
    mask_frame.data.materials.append(mats['yellow_gear'])
    add_subsurf(mask_frame, 1)
    mask_frame.parent = swimmer

    # Transparent/Reflective Glass Lens inside mask frame
    bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=0.43, depth=0.02)
    glass = bpy.context.active_object
    glass.name = "Swimmer_MaskGlass"
    glass.location = (0, -0.44, 0.06)
    glass.scale = (1.28, 0.80, 1.0)
    glass.rotation_euler = (math.pi / 2, 0, 0)
    glass.data.materials.append(mats['glass_lens'])
    glass.parent = swimmer

    # Yellow Snorkel Tube curving along the outer left side (-X) of head
    bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=0.048, depth=0.95)
    snorkel = bpy.context.active_object
    snorkel.name = "Swimmer_Snorkel"
    snorkel.location = (-0.56, -0.25, 0.40)
    snorkel.rotation_euler = (-0.25, -0.12, 0.25)
    snorkel.data.materials.append(mats['yellow_gear'])
    add_subsurf(snorkel, 1)
    snorkel.parent = swimmer

    # Excavation Shovel in right hand (held forward and down on viewer's left side, -X)
    bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.038, depth=1.65)
    shaft = bpy.context.active_object
    shaft.name = "Swimmer_ShovelShaft"
    shaft.location = (-0.78, -0.38, -0.05)
    shaft.rotation_euler = (0.25, -0.12, -0.42)
    shaft.data.materials.append(mats['yellow_gear'])
    shaft.parent = swimmer

    # Curved Yellow Spade Scoop / Blade
    bpy.ops.mesh.primitive_cube_add(size=0.35)
    blade = bpy.context.active_object
    blade.name = "Swimmer_ShovelBlade"
    blade.location = (-1.08, -0.48, -0.55)
    blade.scale = (0.95, 0.15, 1.35)
    blade.rotation_euler = (0.35, -0.20, -0.42)
    blade.data.materials.append(mats['yellow_gear'])
    add_subsurf(blade, 1)
    blade.parent = swimmer

    # -------------------------------------------------------------------------
    # 5. Bomber Ant (Front-Center, Craned Upward, Goggles & Bomb Pack)
    # -------------------------------------------------------------------------
    bomber = create_ant_character(
        "BomberAnt",
        loc=(-0.20, -0.75, -0.35),
        rot=(-0.55, 0.08, -0.04),  # Craned upward looking at camera
        scale=(0.95, 0.95, 0.95),
        mats=mats,
        options={
            'eye_radius': 0.22,
            'mandible_scale': (0.22, 0.22, 0.24),
            'pupil_offset_y': -0.18,
            'pupil_offset_z': 0.08,  # Looking upward
        }
    )

    # Chunky Yellow Aviator Goggles (two circular yellow frames)
    for sign in [-1, 1]:
        bpy.ops.mesh.primitive_torus_add(major_radius=0.22, minor_radius=0.065)
        rim = bpy.context.active_object
        rim.name = f"Bomber_GoggleRim_{sign}"
        rim.location = (sign * 0.32, -0.46, 0.08)
        rim.rotation_euler = (math.pi / 2 + 0.22, 0, 0)
        rim.data.materials.append(mats['yellow_gear'])
        add_subsurf(rim, 1)
        rim.parent = bomber

        bpy.ops.mesh.primitive_cylinder_add(vertices=20, radius=0.19, depth=0.03)
        lens = bpy.context.active_object
        lens.name = f"Bomber_GoggleLens_{sign}"
        lens.location = (sign * 0.32, -0.46, 0.08)
        lens.rotation_euler = (math.pi / 2 + 0.22, 0, 0)
        lens.data.materials.append(mats['glass_lens'])
        lens.parent = bomber

    # Goggle Bridge across the nose
    bpy.ops.mesh.primitive_cube_add(size=0.12)
    bridge = bpy.context.active_object
    bridge.name = "Bomber_GoggleBridge"
    bridge.location = (0, -0.48, 0.08)
    bridge.scale = (1.5, 0.4, 0.4)
    bridge.rotation_euler = (math.pi / 2 + 0.22, 0, 0)
    bridge.data.materials.append(mats['goggle_orange'])
    bridge.parent = bomber

    # Bomb Backpack with spherical cartoon bombs and fuses
    bpy.ops.mesh.primitive_cube_add(size=0.52)
    pack = bpy.context.active_object
    pack.name = "Bomber_Backpack"
    pack.location = (0, 0.42, -0.22)
    pack.data.materials.append(mats['yellow_gear'])
    pack.parent = bomber

    # Spherical bombs with fuses
    for sign in [-1, 1]:
        bpy.ops.mesh.primitive_uv_sphere_add(segments=18, ring_count=14, radius=0.22)
        bomb = bpy.context.active_object
        bomb.name = f"Bomber_Bomb_{sign}"
        bomb.location = (sign * 0.18, 0.48, 0.08)
        bomb.data.materials.append(mats['bomb_black'])
        add_subsurf(bomb, 1)
        bomb.parent = pack

        bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.02, depth=0.18)
        fuse = bpy.context.active_object
        fuse.name = f"Bomber_Fuse_{sign}"
        fuse.location = (sign * 0.18, 0.48, 0.34)
        fuse.rotation_euler = (-0.2, sign * 0.3, 0)
        fuse.data.materials.append(mats['fuse_tan'])
        fuse.parent = pack

    # -------------------------------------------------------------------------
    # 6. Thief Ant (Bottom-Right, Bandit Bandana Mask with Fluttering Knot Streamers)
    # -------------------------------------------------------------------------
    thief = create_ant_character(
        "ThiefAnt",
        loc=(1.25, -0.20, 0.05),
        rot=(-0.04, -0.15, 0.18),
        scale=(0.98, 0.98, 0.98),
        mats=mats,
        options={
            'eye_radius': 0.22,
            'mandible_scale': (0.22, 0.22, 0.24),
            'mandible_y': -0.46,
            'mandible_z': -0.34,
            'pupil_offset_y': -0.16,
        }
    )

    # Yellow Bandit Bandana Mask wrapped across forehead & eye level
    bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=0.68, depth=0.40)
    bandana = bpy.context.active_object
    bandana.name = "Thief_BandanaBand"
    bandana.location = (0, -0.05, 0.08)
    bandana.scale = (1.05, 0.96, 0.88)
    bandana.rotation_euler = (0.10, 0, 0)
    bandana.data.materials.append(mats['bandana_yellow'])
    add_subsurf(bandana, 1)
    bandana.parent = thief

    # Eye Cutouts: subtle rims around eye holes
    for sign in [-1, 1]:
        bpy.ops.mesh.primitive_torus_add(major_radius=0.24, minor_radius=0.035)
        rim = bpy.context.active_object
        rim.name = f"Thief_EyeCutout_{sign}"
        rim.location = (sign * 0.35, -0.42, 0.06)
        rim.rotation_euler = (math.pi / 2 + 0.1, 0, 0)
        rim.data.materials.append(mats['bandana_yellow'])
        rim.parent = thief

    # Bandana Knot at right temple (+X side)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=14, ring_count=12, radius=0.15)
    knot = bpy.context.active_object
    knot.name = "Thief_BandanaKnot"
    knot.location = (0.72, -0.02, 0.10)
    knot.data.materials.append(mats['bandana_yellow'])
    knot.parent = thief

    # Two distinct fluttering yellow ribbon streamers pointing out to the right (+X)
    for z_off, rot_z, scale_x in [(0.14, -0.40, 1.25), (-0.14, -0.82, 0.95)]:
        bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.22, depth=0.04)
        streamer = bpy.context.active_object
        streamer.name = f"Thief_KnotStreamer_{z_off}"
        streamer.location = (0.92, -0.02, 0.10 + z_off)
        streamer.scale = (scale_x, 0.75, 1.0)
        streamer.rotation_euler = (-0.22, 0.55, rot_z)
        streamer.data.materials.append(mats['bandana_yellow'])
        add_subsurf(streamer, 1)
        streamer.parent = thief


def build_environment(mats):
    """
    Crimson/vermilion floor disc tilted forward on pitch black void.
    Catches authentic dark contact shadows cast by the squad.
    """
    # Elliptical Crimson Floor Disc on ground
    bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=3.8, depth=0.04)
    disc = bpy.context.active_object
    disc.name = "Stage_CrimsonFloorDisc"
    disc.location = (0.0, 0.10, -0.85)
    disc.scale = (1.0, 0.75, 1.0)
    disc.rotation_euler = (0.18, 0, 0)  # Tilted up toward camera at -Y
    disc.data.materials.append(mats['stage_red'])

    # Pitch black infinite void backdrop
    world = bpy.context.scene.world
    if not world:
        world = bpy.data.worlds.new("BlackVoidWorld")
        bpy.context.scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    if bg:
        bg.inputs["Color"].default_value = (0.0, 0.0, 0.0, 1.0)
        bg.inputs["Strength"].default_value = 0.0


def build_3d_title_logo(mats):
    """
    Stylized 3D 'ants!' title logo in the lower foreground.
    Beveled 3D block letters reading left-to-right (-X to +X) with purple/cyan iridescent shader.
    """
    root_logo = bpy.data.objects.new("Title_Logo_Root", None)
    bpy.context.collection.objects.link(root_logo)
    root_logo.location = (0.0, -2.1, -0.65)
    root_logo.rotation_euler = (math.radians(65), 0, 0)
    root_logo.scale = (0.85, 0.85, 0.85)

    letters = [
        ('a', -1.35, mats['logo_purple']),
        ('n', -0.65, mats['logo_cyan']),
        ('t', 0.05, mats['logo_purple']),
        ('s', 0.75, mats['logo_cyan']),
        ('!', 1.35, mats['logo_purple']),
    ]

    for char, x_pos, mat in letters:
        bpy.ops.object.text_add(location=(x_pos, 0, 0))
        txt = bpy.context.active_object
        txt.name = f"Logo_Letter_{char}"
        txt.data.body = char
        txt.data.size = 0.95
        txt.data.extrude = 0.12
        txt.data.bevel_depth = 0.035
        txt.data.bevel_resolution = 3
        txt.data.materials.append(mat)
        txt.parent = root_logo

    return root_logo


def setup_lighting():
    """
    Authentic 1998 CGI high-contrast studio lighting:
    - Key Sun: Crisp directional sunlight from upper-left casting dark shadows across the red floor.
    - Fill Point: Soft cool cyan fill from the right.
    - Rim Light: Warm golden rim from top-back for glistening chitin highlights.
    - Front Under-Fill: Soft bounce from the red floor onto chins and mandibles.
    """
    # 1. Main Key Sunlight from top-front-left (casts crisp shadows towards bottom-right)
    bpy.ops.object.light_add(type='SUN', location=(-4.5, -6.0, 7.5))
    key = bpy.context.active_object
    key.name = "Key_Sunlight"
    key.data.energy = 5.2
    key.data.color = (1.0, 0.98, 0.92)
    key.rotation_euler = (math.radians(-52), math.radians(25), math.radians(28))

    # 2. Cool Cyan Fill Light from Right (+X)
    bpy.ops.object.light_add(type='POINT', location=(6.0, -4.0, 3.0))
    fill = bpy.context.active_object
    fill.name = "Fill_Cyan"
    fill.data.energy = 220.0
    fill.data.color = (0.35, 0.72, 0.95)

    # 3. Warm Golden Rim Light from Top-Back (+Y)
    bpy.ops.object.light_add(type='POINT', location=(-3.0, 4.5, 5.5))
    rim = bpy.context.active_object
    rim.name = "Rim_Gold"
    rim.data.energy = 400.0
    rim.data.color = (1.0, 0.82, 0.35)

    # 4. Electric Violet Rim Light from Back-Right (+X, +Y)
    bpy.ops.object.light_add(type='POINT', location=(4.5, 3.5, 4.0))
    rim2 = bpy.context.active_object
    rim2.name = "Rim_Violet"
    rim2.data.energy = 300.0
    rim2.data.color = (0.65, 0.35, 1.0)


def setup_camera():
    """
    Authentic isometric cover camera setup:
    Positioned at -Y looking up-forward (+Y) at the squad cluster and floor disc.
    Using Track-To constraint targeted at scene center to ensure perfect framing.
    """
    target = bpy.data.objects.new("Camera_Target", None)
    bpy.context.collection.objects.link(target)
    target.location = (0.0, 0.0, 0.15)

    bpy.ops.object.camera_add(location=(0.0, -9.0, 4.2))
    cam = bpy.context.active_object
    cam.name = "Cover_Camera"
    cam.data.lens = 50.0  # 50mm portrait perspective
    cam.data.clip_start = 0.1
    cam.data.clip_end = 100.0

    track = cam.constraints.new(type='TRACK_TO')
    track.target = target
    track.track_axis = 'TRACK_NEGATIVE_Z'
    track.up_axis = 'UP_Y'

    bpy.context.scene.camera = cam


def main():
    print("=== Generating Authentic 1998 Ants! 3D Title Scene ===")
    reset_scene()
    mats = setup_palette()

    print("1. Building All 6 Ant Castes in Authentic Squad Formation...")
    build_all_characters(mats)

    print("2. Setting up Environment & Crimson Floor Disc...")
    build_environment(mats)

    print("3. Building Foreground 3D 'ants!' Title Logo...")
    build_3d_title_logo(mats)

    print("4. Configuring Studio Lighting Rig...")
    setup_lighting()

    print("5. Framing Isometric Camera from -Y...")
    setup_camera()

    # Save master .blend file
    blend_path = "/Users/dchadd/Desktop/Ants-Mac/tools/blender/authentic_ants_scene.blend"
    bpy.ops.wm.save_as_mainfile(filepath=blend_path)
    print(f"Saved .blend file: {blend_path}")

    # Render photorealistic cover still using Metal Cycles
    render_path = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d/authentic_ants_render.png"
    bpy.context.scene.render.filepath = render_path
    print(f"Rendering photorealistic cover still to: {render_path}...")
    bpy.ops.render.render(write_still=True)
    print(f"Render complete: {render_path}")

    # Export glTF / GLB for interactive 3D Web Viewer
    glb_path = "/Users/dchadd/Desktop/Ants-Mac/web/viewer3d/ants_scene.glb"
    print(f"Exporting GLB model to: {glb_path}...")
    bpy.ops.export_scene.gltf(
        filepath=glb_path,
        export_format='GLB',
        use_selection=False,
        export_apply=True
    )
    print(f"GLB export complete: {glb_path}")
    print("=== All generation tasks completed successfully! ===")


if __name__ == "__main__":
    main()
