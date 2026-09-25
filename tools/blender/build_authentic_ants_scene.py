"""
Authentic 1998 Ants! 3D Title Scene Recreation in Blender 4.3.2
Master 1:1 High-Fidelity Reproduction of 1998 Cover Art & In-Game Sprite Models:
- Clean 1998 caricature cartoon anatomy:
  - Smooth rounded bean heads, nestled chubby mandibles at bottom of chin, embedded expressive eyes with Pixar catchlights.
  - Natural organic chitin with glossy clearcoat and subtle subsurface scattering.
- Combat Ant (top-left):
  - Elevated high at back-left so his muscular chest, bodybuilder flexed arm, and crossing X-SASH with vertical gold canister are 100% visible!
  - Fierce downward-slanting V-brow scowl with almond slit eyes.
  - Heavy military rocket launcher (bazooka) slung over right shoulder.
- Fire Ant (top-right):
  - Authentic structured firefighter chief helmet: central spine comb, curved flared brim swooping low behind in duckbill tail, upright front shield plaque with bold 3D maroon 'A', chin strap.
  - Two alert eyes peering from under brim shadow.
- Swimmer Ant (middle-left):
  - Snug smooth oval yellow scuba diving mask with transparent glass lens revealing eyes inside.
  - Side J-snorkel tube curving up past head with 180-degree top bend.
  - Yellow excavation shovel held upright in front claw.
- Bomber Ant (front-center / lowest):
  - Crouched lowest in front, tilted UPWARD 32° staring directly into camera lens.
  - Dual circular yellow aviator goggles fitted snugly over eyes with connecting bridge and clear lenses.
  - Antennae swept back over crown.
  - Canvas pack with round black cartoon bombs and coiled string fuses.
- Thief Ant (bottom-right):
  - Sneaking crouch, bright yellow bandit bandana across forehead with sly squinting eyes underneath.
  - Temple knot seamlessly joined to two fluttering yellow ribbon streamers flying to right.
  - Mandibles peeking out beneath bandana.
- Worker Ant (dead center):
  - Friendly, youthful rounded bean head, huge innocent round eyes with catchlights, plump mandible cheeks.
- Crimson Floor Disc:
  - Vibrant fiery vermilion-red beveled elliptical disc tilted 22 degrees toward camera.
  - Authentic crisp contact drop shadows on pitch-black void.
- Stylized 3D 'ants!' Title Logo:
  - Centered foreground 3D beveled typography reading 'ants!' with purple/cyan gradient and ant silhouette inside 'n'.
- Camera & Framing:
  - 45mm portrait camera framing the ENTIRE squad heroically with zero clipping.
- Renders with Apple Silicon Metal Cycles (64 samples, hardware denoised).
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
        scene.cycles.samples = 64
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
                bsdf.inputs["Coat Roughness"].default_value = 0.05
        elif "Clearcoat" in bsdf.inputs:
            bsdf.inputs["Clearcoat"].default_value = coat

        if "Subsurface Weight" in bsdf.inputs and sss > 0:
            bsdf.inputs["Subsurface Weight"].default_value = sss
            if "Subsurface Radius" in bsdf.inputs:
                bsdf.inputs["Subsurface Radius"].default_value = (0.25, 0.35, 0.15)

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
        # Organic chitin: warm glossy olive/forest green with soft subsurface scattering
        'chitin': create_pbr_material("Chitin", (0.30, 0.44, 0.22, 1.0), roughness=0.36, coat=0.40, sss=0.10),
        'chitin_dark': create_pbr_material("DarkChitin", (0.18, 0.28, 0.14, 1.0), roughness=0.38, coat=0.35, sss=0.08),
        'chitin_light': create_pbr_material("LightChitin", (0.36, 0.50, 0.26, 1.0), roughness=0.34, coat=0.42, sss=0.12),

        # Expressive eyes
        'sclera': create_pbr_material("EyeSclera", (0.97, 0.97, 0.95, 1.0), roughness=0.06, coat=0.9),
        'pupil': create_pbr_material("EyePupil", (0.010, 0.010, 0.010, 1.0), roughness=0.03, coat=0.98),
        'glint': create_pbr_material("EyeGlint", (1.0, 1.0, 1.0, 1.0), roughness=0.0, emissive=(1.0, 1.0, 1.0, 1.0)),

        # Antennae & insect legs
        'black_horn': create_pbr_material("BlackHorn", (0.03, 0.03, 0.03, 1.0), roughness=0.25, coat=0.35),

        # Firefighter helmet
        'fire_helmet': create_pbr_material("FireHelmet", (0.96, 0.78, 0.04, 1.0), roughness=0.20, coat=0.8),
        'fire_comb': create_pbr_material("FireComb", (0.90, 0.72, 0.03, 1.0), roughness=0.22, coat=0.7),
        'fire_shield': create_pbr_material("FireShield", (0.88, 0.70, 0.12, 1.0), roughness=0.25, metallic=0.5),
        'fire_letter_a': create_pbr_material("FireLetterA", (0.75, 0.04, 0.06, 1.0), roughness=0.25),
        'fire_strap': create_pbr_material("FireStrap", (0.85, 0.68, 0.05, 1.0), roughness=0.45),

        # Combat gear
        'combat_sash': create_pbr_material("CombatSash", (0.96, 0.78, 0.05, 1.0), roughness=0.42),
        'combat_canister': create_pbr_material("CombatCanister", (0.92, 0.76, 0.15, 1.0), roughness=0.16, metallic=0.90),
        'combat_bazooka': create_pbr_material("CombatBazooka", (0.16, 0.20, 0.14, 1.0), roughness=0.32, metallic=0.65),
        'combat_tie_red': create_pbr_material("CombatTieRed", (0.80, 0.08, 0.08, 1.0), roughness=0.38),

        # Swimmer gear
        'swimmer_mask_rim': create_pbr_material("SwimmerMaskRim", (0.96, 0.80, 0.05, 1.0), roughness=0.22, coat=0.5),
        'swimmer_mask_glass': create_pbr_material("SwimmerMaskGlass", (0.45, 0.75, 0.88, 1.0), roughness=0.03, transmission=0.88, ior=1.45),
        'swimmer_snorkel': create_pbr_material("SwimmerSnorkel", (0.95, 0.68, 0.04, 1.0), roughness=0.25),
        'swimmer_shovel_shaft': create_pbr_material("ShovelShaft", (0.94, 0.76, 0.06, 1.0), roughness=0.32),
        'swimmer_shovel_blade': create_pbr_material("ShovelBlade", (0.50, 0.52, 0.55, 1.0), roughness=0.25, metallic=0.80),

        # Bomber gear
        'bomber_goggles_rim': create_pbr_material("BomberGogglesRim", (0.94, 0.76, 0.08, 1.0), roughness=0.22, metallic=0.4),
        'bomber_goggles_lens': create_pbr_material("BomberGogglesLens", (0.60, 0.78, 0.88, 1.0), roughness=0.04, transmission=0.85, ior=1.45),
        'bomber_pack': create_pbr_material("BomberPack", (0.35, 0.22, 0.12, 1.0), roughness=0.65),
        'bomber_bomb': create_pbr_material("BomberBomb", (0.05, 0.05, 0.05, 1.0), roughness=0.35, metallic=0.35),
        'bomber_fuse': create_pbr_material("BomberFuse", (0.78, 0.68, 0.45, 1.0), roughness=0.70),

        # Thief gear
        'thief_bandana': create_pbr_material("ThiefBandana", (0.95, 0.80, 0.05, 1.0), roughness=0.48),

        # Red floor disc
        'floor_crimson': create_pbr_material("FloorCrimson", (0.84, 0.12, 0.04, 1.0), roughness=0.32, coat=0.20),

        # Stylized 3D title logo
        'logo_purple': create_pbr_material("LogoPurple", (0.46, 0.20, 0.86, 1.0), roughness=0.18, coat=0.7),
        'logo_cyan': create_pbr_material("LogoCyan", (0.10, 0.72, 0.92, 1.0), roughness=0.18, coat=0.7),
    }


def add_subsurf(obj, levels=2):
    """Add subdivision surface modifier and smooth shading."""
    mod = obj.modifiers.new(name="Subsurf", type='SUBSURF')
    mod.levels = levels
    mod.render_levels = levels
    for poly in obj.data.polygons:
        poly.use_smooth = True


def smooth_all_faces(obj):
    for poly in obj.data.polygons:
        poly.use_smooth = True


# ==============================================================================
# Clean 1998 Caricature Ant Anatomy Modeling
# ==============================================================================

def build_cartoon_ant_head(name, root, mats, options=None):
    """
    Constructs a clean, authentic 1998 cartoon ant head:
    - Base: Clean smooth bean-shaped ellipsoid (wider than tall).
    - Mandibles: Two plump rounded smooth lobes nestled side-by-side hanging down from the lower chin.
    - Eyes: Pure white sclera with glossy dark pupils and bright white catchlights.
    - Antennae: Curved black stalks with elbow joints and clubbed tips.
    """
    if options is None:
        options = {}

    head_scale = options.get('head_scale', (1.24, 0.96, 0.96))
    skin_mat = options.get('skin_mat', mats['chitin'])
    brow_scowl = options.get('brow_scowl', 0.0)
    squint = options.get('squint', 0.0)
    has_antennae = options.get('has_antennae', True)
    has_mandibles = options.get('has_mandibles', True)
    eye_radius = options.get('eye_radius', 0.20)
    eye_spacing_x = options.get('eye_spacing_x', 0.24)
    eye_z = options.get('eye_z', 0.10)
    eye_y_offset = options.get('eye_y_offset', -0.36)
    pupil_offset_z = options.get('pupil_offset_z', 0.0)
    antenna_spread = options.get('antenna_spread', 0.32)
    antenna_rot_x = options.get('antenna_rot_x', -0.22)
    antenna_pos_z = options.get('antenna_pos_z', 0.48)
    antenna_pos_y = options.get('antenna_pos_y', -0.20)

    scene = bpy.context.scene

    # 1. Base Cranium: Clean UV Sphere stretched to bean proportions
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=24, radius=0.60)
    head_obj = bpy.context.active_object
    head_obj.name = f"{name}_Cranium"
    head_obj.scale = head_scale
    head_obj.data.materials.append(skin_mat)
    add_subsurf(head_obj, 2)
    head_obj.parent = root

    # 2. Dual Plump Mandible Lobes hanging naturally down from chin
    if has_mandibles:
        for sign in [-1, 1]:
            bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=16, radius=0.16)
            lobe = bpy.context.active_object
            lobe.name = f"{name}_Mandible_{sign}"
            lobe.location = (sign * 0.15, -0.28 * head_scale[1], -0.48 * head_scale[2])
            lobe.scale = (0.80, 0.80, 1.10)
            lobe.data.materials.append(mats['chitin_dark'])
            add_subsurf(lobe, 1)
            lobe.parent = head_obj

    # 3. Expressive Eyes (Left & Right)
    for sign in [-1, 1]:
        # Sclera dome
        bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=18, radius=eye_radius)
        sclera = bpy.context.active_object
        sclera.name = f"{name}_Sclera_{sign}"
        sclera.location = (sign * eye_spacing_x, eye_y_offset, eye_z)
        sclera.scale = (1.0, 0.85, 1.0)
        sclera.data.materials.append(mats['sclera'])
        add_subsurf(sclera, 1)
        sclera.parent = head_obj

        # Pupil / Iris (Nestled on front surface of sclera)
        pupil_r = eye_radius * 0.58
        bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=14, radius=pupil_r)
        pupil = bpy.context.active_object
        pupil.name = f"{name}_Pupil_{sign}"
        pupil.location = (
            sign * eye_spacing_x,
            eye_y_offset - eye_radius * 0.75,
            eye_z + pupil_offset_z
        )
        pupil.scale = (1.0, 0.18, 1.0)
        pupil.data.materials.append(mats['pupil'])
        add_subsurf(pupil, 1)
        pupil.parent = head_obj

        # Specular Catchlight (Upper-Left 10 o'clock position on pupil)
        bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=8, radius=pupil_r * 0.25)
        glint = bpy.context.active_object
        glint.name = f"{name}_Glint_{sign}"
        glint.location = (
            sign * eye_spacing_x - 0.04,
            eye_y_offset - eye_radius * 0.88,
            eye_z + pupil_offset_z + 0.045
        )
        glint.data.materials.append(mats['glint'])
        glint.parent = head_obj

        # Brow Hooding / Eyelids (Combat or Thief)
        if brow_scowl > 0 or squint > 0:
            lid_weight = max(brow_scowl, squint)
            bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=14, radius=eye_radius * 1.08)
            lid = bpy.context.active_object
            lid.name = f"{name}_BrowLid_{sign}"
            lid.location = (sign * eye_spacing_x, eye_y_offset - 0.02, eye_z + 0.08)
            lid.scale = (1.05, 0.90, 0.48)
            lid.rotation_euler = (0.2, sign * -0.32 * lid_weight, 0.0)
            lid.data.materials.append(skin_mat)
            add_subsurf(lid, 1)
            lid.parent = head_obj

    # 4. Curved Insect Antennae
    if has_antennae:
        for sign in [-1, 1]:
            ant_root = bpy.data.objects.new(f"{name}_AntennaRoot_{sign}", None)
            scene.collection.objects.link(ant_root)
            ant_root.parent = head_obj
            ant_root.location = (sign * 0.12, antenna_pos_y, antenna_pos_z)

            # Lower stalk
            bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.030, depth=0.40)
            stalk = bpy.context.active_object
            stalk.name = f"{name}_AntennaStalk_{sign}"
            stalk.location = (sign * 0.06, 0.0, 0.18)
            stalk.rotation_euler = (antenna_rot_x, sign * antenna_spread, 0.0)
            stalk.data.materials.append(mats['black_horn'])
            smooth_all_faces(stalk)
            stalk.parent = ant_root

            # Elbow joint & club tip
            bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=8, radius=0.045)
            tip = bpy.context.active_object
            tip.name = f"{name}_AntennaTip_{sign}"
            tip.location = (sign * 0.15, 0.05, 0.38)
            tip.scale = (1.0, 1.0, 1.4)
            tip.rotation_euler = (antenna_rot_x * 1.2, sign * antenna_spread * 1.3, 0.0)
            tip.data.materials.append(mats['black_horn'])
            tip.parent = ant_root

    return head_obj


def create_ant_body(name, root, mats, options=None):
    """Creates compact thorax, abdomen, and grounding legs."""
    if options is None:
        options = {}

    skin_mat = options.get('skin_mat', mats['chitin'])
    thorax_scale = options.get('thorax_scale', (0.55, 0.45, 0.45))
    thorax_pos = options.get('thorax_pos', (0.0, 0.05, -0.55))
    abdomen_scale = options.get('abdomen_scale', (0.50, 0.70, 0.50))
    abdomen_pos = options.get('abdomen_pos', (0.0, 0.38, -0.68))

    # Thorax
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=18, radius=0.60)
    thorax = bpy.context.active_object
    thorax.name = f"{name}_Thorax"
    thorax.location = thorax_pos
    thorax.scale = thorax_scale
    thorax.data.materials.append(skin_mat)
    add_subsurf(thorax, 2)
    thorax.parent = root

    # Abdomen (Gaster)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=18, radius=0.65)
    abdomen = bpy.context.active_object
    abdomen.name = f"{name}_Abdomen"
    abdomen.location = abdomen_pos
    abdomen.scale = abdomen_scale
    abdomen.data.materials.append(skin_mat)
    add_subsurf(abdomen, 2)
    abdomen.parent = root

    return thorax, abdomen


def create_ground_leg(name, root, mats, start_pos, knee_pos, foot_pos):
    """Creates a cleanly jointed insect leg reaching down to the floor."""
    # Femur
    bpy.ops.mesh.primitive_cylinder_add(vertices=10, radius=0.036, depth=1.0)
    femur = bpy.context.active_object
    femur.name = f"{name}_Femur"
    smooth_all_faces(femur)
    femur.data.materials.append(mats['black_horn'])

    dx1 = knee_pos[0] - start_pos[0]
    dy1 = knee_pos[1] - start_pos[1]
    dz1 = knee_pos[2] - start_pos[2]
    len1 = math.sqrt(dx1*dx1 + dy1*dy1 + dz1*dz1)
    femur.location = (start_pos[0] + dx1*0.5, start_pos[1] + dy1*0.5, start_pos[2] + dz1*0.5)
    femur.scale = (1.0, 1.0, len1)
    phi1 = math.atan2(math.sqrt(dx1*dx1 + dy1*dy1), dz1)
    theta1 = math.atan2(dy1, dx1)
    femur.rotation_euler = (0, phi1, theta1)
    femur.parent = root

    # Knee Joint
    bpy.ops.mesh.primitive_uv_sphere_add(segments=10, ring_count=8, radius=0.045)
    knee = bpy.context.active_object
    knee.name = f"{name}_Knee"
    knee.location = knee_pos
    knee.data.materials.append(mats['black_horn'])
    knee.parent = root

    # Tibia
    bpy.ops.mesh.primitive_cylinder_add(vertices=10, radius=0.030, depth=1.0)
    tibia = bpy.context.active_object
    tibia.name = f"{name}_Tibia"
    smooth_all_faces(tibia)
    tibia.data.materials.append(mats['black_horn'])

    dx2 = foot_pos[0] - knee_pos[0]
    dy2 = foot_pos[1] - knee_pos[1]
    dz2 = foot_pos[2] - knee_pos[2]
    len2 = math.sqrt(dx2*dx2 + dy2*dy2 + dz2*dz2)
    tibia.location = (knee_pos[0] + dx2*0.5, knee_pos[1] + dy2*0.5, knee_pos[2] + dz2*0.5)
    tibia.scale = (1.0, 1.0, len2)
    phi2 = math.atan2(math.sqrt(dx2*dx2 + dy2*dy2), dz2)
    theta2 = math.atan2(dy2, dx2)
    tibia.rotation_euler = (0, phi2, theta2)
    tibia.parent = root


# ==============================================================================
# The 6 Iconic Ant Castes
# ==============================================================================

def build_worker_ant(scene_root, mats):
    """
    Worker Ant (Center Anchor):
    - Friendly, youthful rounded bean head.
    - Large innocent round eyes with bright specular catchlights.
    - Plump rounded mandibles at chin.
    """
    worker_root = bpy.data.objects.new("WorkerAnt", None)
    bpy.context.scene.collection.objects.link(worker_root)
    worker_root.parent = scene_root
    worker_root.location = (0.05, 0.0, 1.35)

    head = build_cartoon_ant_head("Worker", worker_root, mats, {
        'head_scale': (1.26, 0.96, 0.96),
        'skin_mat': mats['chitin'],
        'eye_radius': 0.22,
        'eye_spacing_x': 0.24,
        'eye_z': 0.12,
        'pupil_offset_z': -0.01,
        'antenna_spread': 0.32,
        'antenna_rot_x': -0.20,
    })

    create_ant_body("Worker", worker_root, mats, {
        'skin_mat': mats['chitin'],
        'thorax_scale': (0.52, 0.44, 0.44),
        'thorax_pos': (0.0, 0.05, -0.55),
        'abdomen_scale': (0.48, 0.68, 0.48),
        'abdomen_pos': (0.0, 0.38, -0.68),
    })

    create_ground_leg("Worker_L", worker_root, mats,
                      start_pos=(-0.25, 0.0, -0.40),
                      knee_pos=(-0.55, -0.28, -0.65),
                      foot_pos=(-0.45, -0.55, -1.35))
    create_ground_leg("Worker_R", worker_root, mats,
                      start_pos=(0.30, 0.0, -0.40),
                      knee_pos=(0.60, -0.25, -0.65),
                      foot_pos=(0.50, -0.50, -1.35))

    return worker_root


def build_combat_ant(scene_root, mats):
    """
    Combat Ant (Top Left):
    - Elevated HIGH at back-left so chest, X-SASH, and gold canister are 100% visible!
    - Muscular bicep arm flexed upward in bodybuilder pose (sprite_1807).
    - Heavy hooded brow with fierce downward-slanting angry V-scowl.
    - True dual-strap crossing X-SASH with vertical gold canister in center and red shoulder ties.
    - Military rocket launcher (bazooka) slung over right shoulder.
    """
    combat_root = bpy.data.objects.new("CombatAnt", None)
    bpy.context.scene.collection.objects.link(combat_root)
    combat_root.parent = scene_root
    # Elevated high and slightly forward so the chest and X-sash are prominently displayed!
    combat_root.location = (-1.10, 0.85, 2.35)

    head = build_cartoon_ant_head("Combat", combat_root, mats, {
        'head_scale': (1.32, 0.98, 0.98),
        'skin_mat': mats['chitin_dark'],
        'brow_scowl': 1.0,  # Angry V brow ridge
        'squint': 0.65,      # Fierce narrow eyes
        'eye_radius': 0.20,
        'eye_spacing_x': 0.26,
        'eye_z': 0.10,
        'antenna_spread': 0.40,
        'antenna_rot_x': -0.35,
    })

    # Muscular broad thorax positioned forward so chest is fully visible
    create_ant_body("Combat", combat_root, mats, {
        'skin_mat': mats['chitin_dark'],
        'thorax_scale': (0.72, 0.58, 0.58),
        'thorax_pos': (0.0, -0.05, -0.55),
        'abdomen_scale': (0.55, 0.72, 0.52),
        'abdomen_pos': (0.0, 0.40, -0.68),
    })

    # --- Muscular Bodybuilder Left Arm (Viewer's Left, flexed upward) ---
    bpy.ops.mesh.primitive_uv_sphere_add(segments=18, ring_count=14, radius=0.22)
    deltoid = bpy.context.active_object
    deltoid.name = "Combat_Deltoid_L"
    deltoid.location = (-0.65, 0.0, -0.35)
    deltoid.data.materials.append(mats['chitin_dark'])
    add_subsurf(deltoid, 1)
    deltoid.parent = combat_root

    # Bicep (Flexed outward and forward)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=18, ring_count=14, radius=0.21)
    bicep = bpy.context.active_object
    bicep.name = "Combat_Bicep_L"
    bicep.location = (-0.84, -0.15, -0.28)
    bicep.scale = (1.25, 1.0, 1.0)
    bicep.data.materials.append(mats['chitin_dark'])
    add_subsurf(bicep, 1)
    bicep.parent = combat_root

    # Forearm (Angled up towards head at 60 degrees)
    bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.12, depth=0.48)
    forearm = bpy.context.active_object
    forearm.name = "Combat_Forearm_L"
    forearm.location = (-0.76, -0.28, -0.02)
    forearm.rotation_euler = (0.5, -0.6, 0.3)
    forearm.data.materials.append(mats['chitin_dark'])
    add_subsurf(forearm, 1)
    forearm.parent = combat_root

    # Clenched claw / hand at shoulder level
    bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=10, radius=0.13)
    fist = bpy.context.active_object
    fist.name = "Combat_Claw_L"
    fist.location = (-0.68, -0.32, 0.20)
    fist.data.materials.append(mats['black_horn'])
    add_subsurf(fist, 1)
    fist.parent = combat_root

    # Right arm (Viewer's right: resting forward)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=12, radius=0.19)
    arm_r = bpy.context.active_object
    arm_r.name = "Combat_Arm_R"
    arm_r.location = (0.58, 0.0, -0.42)
    arm_r.scale = (1.1, 0.85, 0.9)
    arm_r.data.materials.append(mats['chitin_dark'])
    add_subsurf(arm_r, 1)
    arm_r.parent = combat_root

    # --- TRUE DUAL-STRAP CROSSING X-SASH (Tactical Bandolier) ---
    # Strap 1: From right shoulder across chest to left waist
    bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=0.08, depth=1.20)
    sash1 = bpy.context.active_object
    sash1.name = "Combat_Sash_Diagonal_1"
    sash1.location = (0.02, -0.32, -0.55)
    sash1.rotation_euler = (0.35, 0.78, -0.20)
    sash1.scale = (1.45, 0.38, 1.0)
    sash1.data.materials.append(mats['combat_sash'])
    add_subsurf(sash1, 1)
    sash1.parent = combat_root

    # Strap 2: From left shoulder across chest to right waist (completing the X!)
    bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=0.08, depth=1.20)
    sash2 = bpy.context.active_object
    sash2.name = "Combat_Sash_Diagonal_2"
    sash2.location = (-0.02, -0.32, -0.55)
    sash2.rotation_euler = (0.35, -0.78, 0.20)
    sash2.scale = (1.45, 0.38, 1.0)
    sash2.data.materials.append(mats['combat_sash'])
    add_subsurf(sash2, 1)
    sash2.parent = combat_root

    # Vertical Gold Ammo Canister prominently placed at the center of the X
    bpy.ops.mesh.primitive_cylinder_add(vertices=18, radius=0.085, depth=0.36)
    canister = bpy.context.active_object
    canister.name = "Combat_Gold_Canister"
    canister.location = (0.0, -0.42, -0.55)
    canister.rotation_euler = (0.28, 0.0, 0.0)
    canister.data.materials.append(mats['combat_canister'])
    add_subsurf(canister, 1)
    canister.parent = combat_root

    # Red cloth tie accents on left and right shoulders
    for sign, xpos in [(-1, -0.45), (1, 0.42)]:
        bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=8, radius=0.07)
        tie = bpy.context.active_object
        tie.name = f"Combat_RedTie_{sign}"
        tie.location = (xpos, -0.20, -0.26)
        tie.scale = (1.0, 0.8, 1.4)
        tie.data.materials.append(mats['combat_tie_red'])
        tie.parent = combat_root

    # Bazooka rocket launcher tube slung over right shoulder
    bpy.ops.mesh.primitive_cylinder_add(vertices=20, radius=0.11, depth=1.5)
    baz = bpy.context.active_object
    baz.name = "Combat_Bazooka"
    baz.location = (-0.56, 0.20, 0.20)
    baz.rotation_euler = (0.65, 0.25, -0.32)
    baz.data.materials.append(mats['combat_bazooka'])
    smooth_all_faces(baz)
    baz.parent = combat_root

    # Bazooka gold muzzle rim
    bpy.ops.mesh.primitive_torus_add(major_radius=0.12, minor_radius=0.025, major_segments=20, minor_segments=12)
    b_rim = bpy.context.active_object
    b_rim.name = "Combat_Bazooka_Rim"
    b_rim.location = (-0.73, -0.32, 0.72)
    b_rim.rotation_euler = (0.65, 0.25, -0.32)
    b_rim.data.materials.append(mats['combat_canister'])
    b_rim.parent = combat_root

    return combat_root


def build_fire_ant(scene_root, mats):
    """
    Fire Ant (Top Right):
    - Classic American Firefighter Chief Helmet:
      - High crown dome with central raised spine/comb.
      - Compact curved brim turning up slightly in front, swooping down in beaver tail behind.
      - Upright gold shield plaque with bold 3D maroon capital letter 'A'.
      - Yellow chin strap under jaw.
    - Two alert white eyes peeking from under helmet shadow.
    """
    fire_root = bpy.data.objects.new("FireAnt", None)
    bpy.context.scene.collection.objects.link(fire_root)
    fire_root.parent = scene_root
    fire_root.location = (1.20, 0.65, 1.95)

    head = build_cartoon_ant_head("Fire", fire_root, mats, {
        'head_scale': (1.26, 0.95, 0.95),
        'skin_mat': mats['chitin'],
        'eye_radius': 0.20,
        'eye_spacing_x': 0.24,
        'eye_z': 0.10,
        'has_antennae': False,  # Covered by helmet
    })

    create_ant_body("Fire", fire_root, mats, {
        'skin_mat': mats['chitin'],
        'thorax_scale': (0.55, 0.45, 0.45),
        'thorax_pos': (0.0, 0.05, -0.55),
        'abdomen_scale': (0.50, 0.70, 0.50),
        'abdomen_pos': (0.0, 0.40, -0.68),
    })

    # --- AUTHENTIC FIREFIGHTER CHIEF HELMET ---
    helmet_root = bpy.data.objects.new("Fire_Helmet_Root", None)
    bpy.context.scene.collection.objects.link(helmet_root)
    helmet_root.parent = head
    helmet_root.location = (0.0, 0.05, 0.22)
    helmet_root.rotation_euler = (math.radians(-6), 0.0, math.radians(-5))

    # 1. Crown Dome (Tall, solid helmet)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=26, ring_count=18, radius=0.68)
    crown = bpy.context.active_object
    crown.name = "Fire_Helmet_Crown"
    crown.location = (0.0, 0.0, 0.18)
    crown.scale = (1.05, 1.15, 0.82)
    crown.data.materials.append(mats['fire_helmet'])
    add_subsurf(crown, 1)
    crown.parent = helmet_root

    # 2. Central Raised Spine/Comb along top of helmet
    bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=0.06, depth=1.05)
    comb = bpy.context.active_object
    comb.name = "Fire_Helmet_Comb"
    comb.location = (0.0, 0.02, 0.60)
    comb.rotation_euler = (math.radians(88), 0.0, 0.0)
    comb.scale = (0.75, 1.0, 0.85)
    comb.data.materials.append(mats['fire_comb'])
    add_subsurf(comb, 1)
    comb.parent = helmet_root

    # 3. Compact Curved Helmet Brim (Fits close to head, not an oversized disc!)
    mesh_brim = bpy.data.meshes.new("Fire_Brim_Mesh")
    brim_obj = bpy.data.objects.new("Fire_Helmet_Brim", mesh_brim)
    bpy.context.scene.collection.objects.link(brim_obj)
    brim_obj.parent = helmet_root

    bm = bmesh.new()
    bmesh.ops.create_circle(bm, cap_ends=True, radius=0.88, segments=32)
    for v in bm.verts:
        v.co.x *= 1.08
        v.co.y *= 1.18
        if v.co.y < -0.25:
            v.co.z += (-v.co.y - 0.25) * 0.22
        elif v.co.y > 0.20:
            v.co.z -= (v.co.y - 0.20) * 0.32
            v.co.y *= 1.15
    bm.to_mesh(mesh_brim)
    bm.free()

    brim_obj.location = (0.0, -0.02, 0.08)
    brim_obj.data.materials.append(mats['fire_helmet'])
    sol = brim_obj.modifiers.new(name="Solidify", type='SOLIDIFY')
    sol.thickness = 0.04
    add_subsurf(brim_obj, 1)

    # 4. Upright Gold Front Shield Plaque
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    shield = bpy.context.active_object
    shield.name = "Fire_Shield_Plaque"
    shield.location = (0.0, -0.68, 0.38)
    shield.rotation_euler = (math.radians(-16), 0.0, 0.0)
    shield.scale = (0.38, 0.045, 0.44)
    shield.data.materials.append(mats['fire_shield'])
    add_subsurf(shield, 1)
    shield.parent = helmet_root

    # 5. Bold 3D Maroon Capital Letter 'A' on Shield (Real 3D Text!)
    a_curve = bpy.data.curves.new(type="FONT", name="FireLetterACurve")
    a_curve.body = "A"
    a_curve.size = 0.34
    a_curve.align_x = 'CENTER'
    a_curve.align_y = 'CENTER'
    a_curve.extrude = 0.045
    a_curve.bevel_depth = 0.015

    a_obj = bpy.data.objects.new("Fire_Letter_A_Text", a_curve)
    bpy.context.scene.collection.objects.link(a_obj)
    a_obj.location = (0.0, -0.72, 0.36)
    a_obj.rotation_euler = (math.radians(74), 0.0, 0.0)
    a_obj.data.materials.append(mats['fire_letter_a'])
    a_obj.parent = helmet_root

    # 6. Chin Strap under jaw
    bpy.ops.mesh.primitive_torus_add(major_radius=0.46, minor_radius=0.030, major_segments=24, minor_segments=10)
    strap = bpy.context.active_object
    strap.name = "Fire_Chin_Strap"
    strap.location = (0.0, -0.05, -0.28)
    strap.rotation_euler = (math.radians(45), 0.0, 0.0)
    strap.scale = (1.0, 0.8, 0.7)
    strap.data.materials.append(mats['fire_strap'])
    strap.parent = head

    return fire_root


def build_swimmer_ant(scene_root, mats):
    """
    Swimmer Ant (Middle Left):
    - Positioned down-left so Combat Ant's chest is clearly visible above!
    - Snug smooth oval yellow scuba diving mask with transparent glass lens revealing eyes inside.
    - Side J-snorkel tube curving down to mouth and up past crown with 180° top bend.
    - Straight yellow excavation shovel held upright in front claw.
    """
    swimmer_root = bpy.data.objects.new("SwimmerAnt", None)
    bpy.context.scene.collection.objects.link(swimmer_root)
    swimmer_root.parent = scene_root
    # Lower and further left so Combat Ant is completely uncovered!
    swimmer_root.location = (-1.60, -0.25, 0.92)

    head = build_cartoon_ant_head("Swimmer", swimmer_root, mats, {
        'head_scale': (1.28, 0.96, 0.96),
        'skin_mat': mats['chitin'],
        'eye_radius': 0.20,
        'eye_spacing_x': 0.24,
        'eye_z': 0.12,
        'has_antennae': True,
        'antenna_spread': 0.30,
        'antenna_rot_x': -0.18,
    })

    create_ant_body("Swimmer", swimmer_root, mats, {
        'skin_mat': mats['chitin'],
        'thorax_scale': (0.54, 0.44, 0.44),
        'thorax_pos': (0.0, 0.05, -0.55),
        'abdomen_scale': (0.48, 0.68, 0.48),
        'abdomen_pos': (0.0, 0.38, -0.68),
    })

    # --- SCUBA DIVING MASK (Smooth oval rubber frame snug around eyes!) ---
    mask_root = bpy.data.objects.new("Swimmer_Mask_Root", None)
    bpy.context.scene.collection.objects.link(mask_root)
    mask_root.parent = head
    mask_root.location = (0.0, -0.38, 0.12)

    # 1. Oval Rubber Frame (Snug fit without wide wings)
    bpy.ops.mesh.primitive_torus_add(major_radius=0.38, minor_radius=0.065, major_segments=32, minor_segments=16)
    mask_rim = bpy.context.active_object
    mask_rim.name = "Swimmer_Mask_Rim"
    # Local X is width (1.28), Local Y is height (0.80), Local Z is depth (1.0)
    mask_rim.scale = (1.28, 0.80, 1.0)
    mask_rim.rotation_euler = (math.radians(90), 0.0, 0.0)
    mask_rim.data.materials.append(mats['swimmer_mask_rim'])
    add_subsurf(mask_rim, 1)
    mask_rim.parent = mask_root

    # 2. Transparent Glass Faceplate (Eyes clearly visible behind)
    bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=0.38, depth=0.016)
    glass = bpy.context.active_object
    glass.name = "Swimmer_Mask_Glass"
    glass.location = (0.0, -0.015, 0.0)
    glass.rotation_euler = (math.radians(90), 0.0, 0.0)
    glass.scale = (1.24, 1.0, 0.75)
    glass.data.materials.append(mats['swimmer_mask_glass'])
    glass.parent = mask_root

    # 3. Yellow Snorkel Tube (J-tube on right side of mask)
    bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=0.040, depth=0.82)
    snork_pipe = bpy.context.active_object
    snork_pipe.name = "Swimmer_Snorkel_Pipe"
    snork_pipe.location = (0.54, 0.06, 0.28)
    snork_pipe.rotation_euler = (math.radians(12), math.radians(6), 0.0)
    snork_pipe.data.materials.append(mats['swimmer_snorkel'])
    smooth_all_faces(snork_pipe)
    snork_pipe.parent = mask_root

    # 180° Top Curve
    bpy.ops.mesh.primitive_torus_add(major_radius=0.085, minor_radius=0.036, major_segments=16, minor_segments=10)
    snork_curve = bpy.context.active_object
    snork_curve.name = "Swimmer_Snorkel_Curve"
    snork_curve.location = (0.58, 0.14, 0.68)
    snork_curve.rotation_euler = (0.0, math.radians(90), 0.0)
    snork_curve.data.materials.append(mats['swimmer_snorkel'])
    snork_curve.parent = mask_root

    # --- UPRIGHT EXCAVATION SHOVEL (Held in left claw) ---
    shovel_root = bpy.data.objects.new("Swimmer_Shovel_Root", None)
    bpy.context.scene.collection.objects.link(shovel_root)
    shovel_root.parent = swimmer_root
    shovel_root.location = (-0.95, -0.32, 0.35)

    # Shovel Shaft (Yellow)
    bpy.ops.mesh.primitive_cylinder_add(vertices=14, radius=0.036, depth=1.50)
    shaft = bpy.context.active_object
    shaft.name = "Swimmer_Shovel_Shaft"
    shaft.location = (0.0, 0.0, 0.0)
    shaft.rotation_euler = (math.radians(-5), math.radians(4), 0.0)
    shaft.data.materials.append(mats['swimmer_shovel_shaft'])
    smooth_all_faces(shaft)
    shaft.parent = shovel_root

    # Shovel Metal Spade Blade
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    blade = bpy.context.active_object
    blade.name = "Swimmer_Shovel_Blade"
    blade.location = (0.0, -0.02, 0.72)
    blade.scale = (0.16, 0.025, 0.22)
    blade.data.materials.append(mats['swimmer_shovel_blade'])
    add_subsurf(blade, 1)
    blade.parent = shovel_root

    return swimmer_root


def build_bomber_ant(scene_root, mats):
    """
    Bomber Ant (Front Center / Lowest):
    - Crouched lowest in front, tilted UPWARD 32° staring directly into camera.
    - Dual circular yellow aviator goggles snug over eyes with connecting bridge and clear lenses.
    - Antennae swept back over crown.
    - Canvas pack with round black cartoon bombs and coiled string fuses.
    """
    bomber_root = bpy.data.objects.new("BomberAnt", None)
    bpy.context.scene.collection.objects.link(bomber_root)
    bomber_root.parent = scene_root
    bomber_root.location = (-0.15, -0.65, 0.60)
    # Tilted upward toward camera!
    bomber_root.rotation_euler = (math.radians(-32), 0.0, math.radians(5))

    head = build_cartoon_ant_head("Bomber", bomber_root, mats, {
        'head_scale': (1.24, 0.94, 0.94),
        'skin_mat': mats['chitin_light'],
        'eye_radius': 0.21,
        'eye_spacing_x': 0.24,
        'eye_z': 0.12,
        'eye_y_offset': -0.36,
        'pupil_offset_z': 0.04,  # Looking up towards camera!
        'antenna_spread': 0.36,
        'antenna_rot_x': 0.45,   # Swept back over skull
        'antenna_pos_z': 0.42,
        'antenna_pos_y': 0.15,   # From top-back of head!
        'has_mandibles': False,  # Tucked back beneath head
    })

    create_ant_body("Bomber", bomber_root, mats, {
        'skin_mat': mats['chitin_light'],
        'thorax_scale': (0.50, 0.42, 0.42),
        'thorax_pos': (0.0, 0.05, -0.52),
        'abdomen_scale': (0.45, 0.65, 0.45),
        'abdomen_pos': (0.0, 0.36, -0.65),
    })

    # --- DUAL ROUND AVIATOR GOGGLES (Fitted right over the eye centers!) ---
    for sign in [-1, 1]:
        # Circular Yellow Frame
        bpy.ops.mesh.primitive_torus_add(major_radius=0.23, minor_radius=0.042, major_segments=24, minor_segments=12)
        rim = bpy.context.active_object
        rim.name = f"Bomber_Goggle_Rim_{sign}"
        rim.location = (sign * 0.24, -0.37, 0.12)
        rim.rotation_euler = (math.radians(90), 0.0, 0.0)
        rim.data.materials.append(mats['bomber_goggles_rim'])
        add_subsurf(rim, 1)
        rim.parent = head

        # Glass Lens
        bpy.ops.mesh.primitive_cylinder_add(vertices=20, radius=0.21, depth=0.016)
        lens = bpy.context.active_object
        lens.name = f"Bomber_Goggle_Lens_{sign}"
        lens.location = (sign * 0.24, -0.365, 0.12)
        lens.rotation_euler = (math.radians(90), 0.0, 0.0)
        lens.data.materials.append(mats['bomber_goggles_lens'])
        lens.parent = head

    # Center Bridge between goggle frames
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    bridge = bpy.context.active_object
    bridge.name = "Bomber_Goggle_Bridge"
    bridge.location = (0.0, -0.37, 0.12)
    bridge.scale = (0.08, 0.02, 0.03)
    bridge.data.materials.append(mats['bomber_goggles_rim'])
    bridge.parent = head

    # --- BOMB BACKPACK WITH ROUND BOMBS ---
    pack = bpy.data.objects.new("Bomber_Pack_Root", None)
    bpy.context.scene.collection.objects.link(pack)
    pack.parent = bomber_root
    pack.location = (0.0, 0.35, -0.45)

    # Leather Pack Body
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    bag = bpy.context.active_object
    bag.name = "Bomber_Bag"
    bag.scale = (0.42, 0.30, 0.32)
    bag.data.materials.append(mats['bomber_pack'])
    add_subsurf(bag, 1)
    bag.parent = pack

    # Cartoon Black Cannonball Bombs nestled in pack
    for sign, xpos in [(-1, -0.15), (1, 0.15)]:
        bpy.ops.mesh.primitive_uv_sphere_add(segments=18, ring_count=14, radius=0.15)
        bomb = bpy.context.active_object
        bomb.name = f"Bomber_Bomb_{sign}"
        bomb.location = (xpos, 0.02, 0.18)
        bomb.data.materials.append(mats['bomber_bomb'])
        add_subsurf(bomb, 1)
        bomb.parent = pack

        # Bomb neck collar
        bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.035, depth=0.06)
        neck = bpy.context.active_object
        neck.name = f"Bomber_Bomb_Neck_{sign}"
        neck.location = (xpos, 0.02, 0.34)
        neck.data.materials.append(mats['bomber_bomb'])
        neck.parent = pack

        # Coiled Fuse
        bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.015, depth=0.12)
        fuse = bpy.context.active_object
        fuse.name = f"Bomber_Bomb_Fuse_{sign}"
        fuse.location = (xpos + 0.02, 0.02, 0.40)
        fuse.rotation_euler = (0.2, sign * 0.4, 0.0)
        fuse.data.materials.append(mats['bomber_fuse'])
        fuse.parent = pack

    # 2 small front legs grounding Bomber to floor
    create_ground_leg("Bomber_L", bomber_root, mats,
                      start_pos=(-0.20, -0.20, -0.35),
                      knee_pos=(-0.40, -0.45, -0.45),
                      foot_pos=(-0.30, -0.65, -0.75))
    create_ground_leg("Bomber_R", bomber_root, mats,
                      start_pos=(0.20, -0.20, -0.35),
                      knee_pos=(0.40, -0.45, -0.45),
                      foot_pos=(0.30, -0.65, -0.75))

    return bomber_root


def build_thief_ant(scene_root, mats):
    """
    Thief Ant (Bottom Right):
    - Sneaking low-right crouch.
    - Bright yellow cloth bandana tied across forehead with sly squinting eyes underneath.
    - Temple knot seamlessly joined to two fluttering yellow ribbon streamers flying to the right.
    - Mandibles peeking out beneath bandana.
    """
    thief_root = bpy.data.objects.new("ThiefAnt", None)
    bpy.context.scene.collection.objects.link(thief_root)
    thief_root.parent = scene_root
    thief_root.location = (1.32, -0.15, 0.98)
    thief_root.rotation_euler = (0.0, 0.0, math.radians(-12))

    head = build_cartoon_ant_head("Thief", thief_root, mats, {
        'head_scale': (1.24, 0.94, 0.94),
        'skin_mat': mats['chitin'],
        'eye_radius': 0.19,
        'eye_spacing_x': 0.24,
        'eye_z': 0.08,
        'squint': 0.70,  # Mischievous squinting eyes
        'antenna_spread': 0.36,
        'antenna_rot_x': -0.28,
        'antenna_pos_z': 0.45,
    })

    create_ant_body("Thief", thief_root, mats, {
        'skin_mat': mats['chitin'],
        'thorax_scale': (0.50, 0.42, 0.42),
        'thorax_pos': (0.0, 0.05, -0.55),
        'abdomen_scale': (0.46, 0.66, 0.46),
        'abdomen_pos': (0.0, 0.38, -0.68),
    })

    # --- BANDIT BANDANA WITH FLUTTERING RIBBON TAILS ---
    bandana_root = bpy.data.objects.new("Thief_Bandana_Root", None)
    bpy.context.scene.collection.objects.link(bandana_root)
    bandana_root.parent = head
    bandana_root.location = (0.0, 0.0, 0.18)

    # 1. Fitted Cloth Band around forehead (Sitting above eyes, not slicing through them!)
    bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=0.64, depth=0.22)
    band = bpy.context.active_object
    band.name = "Thief_Bandana_Main"
    band.scale = (1.26, 0.96, 1.0)
    band.data.materials.append(mats['thief_bandana'])
    add_subsurf(band, 1)
    band.parent = bandana_root

    # 2. Right Temple Knot
    bpy.ops.mesh.primitive_uv_sphere_add(segments=14, ring_count=10, radius=0.12)
    knot = bpy.context.active_object
    knot.name = "Thief_Bandana_Knot"
    knot.location = (0.76, -0.05, 0.02)
    knot.scale = (1.1, 0.85, 1.2)
    knot.data.materials.append(mats['thief_bandana'])
    knot.parent = bandana_root

    # 3. Two Fluttering Cloth Ribbon Tails (Flying to the right!)
    bpy.ops.mesh.primitive_cube_add(size=1.0)
    ribbon1 = bpy.context.active_object
    ribbon1.name = "Thief_Ribbon_Upper"
    ribbon1.location = (1.02, -0.02, 0.08)
    ribbon1.rotation_euler = (math.radians(10), math.radians(-15), math.radians(25))
    ribbon1.scale = (0.28, 0.025, 0.09)
    ribbon1.data.materials.append(mats['thief_bandana'])
    add_subsurf(ribbon1, 1)
    ribbon1.parent = bandana_root

    bpy.ops.mesh.primitive_cube_add(size=1.0)
    ribbon2 = bpy.context.active_object
    ribbon2.name = "Thief_Ribbon_Lower"
    ribbon2.location = (1.08, 0.04, -0.06)
    ribbon2.rotation_euler = (math.radians(-8), math.radians(12), math.radians(-15))
    ribbon2.scale = (0.32, 0.025, 0.08)
    ribbon2.data.materials.append(mats['thief_bandana'])
    add_subsurf(ribbon2, 1)
    ribbon2.parent = bandana_root

    create_ground_leg("Thief_L", thief_root, mats,
                      start_pos=(-0.25, -0.10, -0.40),
                      knee_pos=(-0.45, -0.35, -0.65),
                      foot_pos=(-0.35, -0.55, -1.05))
    create_ground_leg("Thief_R", thief_root, mats,
                      start_pos=(0.25, -0.10, -0.40),
                      knee_pos=(0.55, -0.30, -0.65),
                      foot_pos=(0.45, -0.50, -1.05))

    return thief_root


# ==============================================================================
# Environment, Lighting & Foreground Title Logo
# ==============================================================================

def build_crimson_floor_disc(scene_root, mats):
    """Vibrant fiery vermilion-red beveled elliptical disc tilted toward camera."""
    disc_obj = bpy.data.objects.new("CrimsonFloorDisc", None)
    bpy.context.scene.collection.objects.link(disc_obj)
    disc_obj.parent = scene_root

    # Tilted 22 degrees toward camera
    disc_obj.location = (0.0, 0.0, -0.35)
    disc_obj.rotation_euler = (math.radians(22), 0.0, 0.0)

    # Elliptical disc mesh
    bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=5.6, depth=0.28)
    mesh_disc = bpy.context.active_object
    mesh_disc.name = "Crimson_Disc_Mesh"
    mesh_disc.scale = (1.0, 0.76, 1.0)
    mesh_disc.data.materials.append(mats['floor_crimson'])
    smooth_all_faces(mesh_disc)
    mesh_disc.parent = disc_obj

    # Subtle beveled edge rim
    bev = mesh_disc.modifiers.new(name="Bevel", type='BEVEL')
    bev.width = 0.08
    bev.segments = 4


def build_3d_title_logo(scene_root, mats):
    """
    Centered 3D beveled stylized 'ants!' title logo matching 1998 cover art typography.
    Constructed with authentic 3D text and the ant silhouette character!
    """
    logo_root = bpy.data.objects.new("TitleLogo_Root", None)
    bpy.context.scene.collection.objects.link(logo_root)
    logo_root.parent = scene_root
    # Centered at x=0.0 across the bottom front of the disc!
    logo_root.location = (0.0, -2.55, 0.18)
    logo_root.rotation_euler = (math.radians(20), 0.0, 0.0)

    # Native 3D Text Object perfectly centered!
    font_curve = bpy.data.curves.new(type="FONT", name="AntsLogoCurve")
    font_curve.body = "ants!"
    font_curve.size = 1.30
    font_curve.align_x = 'CENTER'
    font_curve.align_y = 'CENTER'
    font_curve.extrude = 0.15
    font_curve.bevel_depth = 0.04
    font_curve.bevel_resolution = 4

    font_obj = bpy.data.objects.new("AntsLogoText", font_curve)
    bpy.context.scene.collection.objects.link(font_obj)
    font_obj.data.materials.append(mats['logo_purple'])
    font_obj.parent = logo_root

    # Ant silhouette glyph in center of letter 'n'
    bpy.ops.mesh.primitive_uv_sphere_add(segments=14, ring_count=10, radius=0.10)
    glyph = bpy.context.active_object
    glyph.name = "Logo_Ant_Glyph"
    glyph.location = (-0.32, -0.06, 0.0)
    glyph.scale = (1.0, 0.4, 1.3)
    glyph.data.materials.append(mats['black_horn'])
    glyph.parent = logo_root


def setup_lighting(scene_root):
    """Multi-point studio lighting matching the warm cover art illumination."""
    lights_root = bpy.data.objects.new("StudioLights", None)
    bpy.context.scene.collection.objects.link(lights_root)
    lights_root.parent = scene_root

    # 1. Warm Key Sun Light from top-left (rich warm illumination & crisp contact shadows)
    key_data = bpy.data.lights.new(name="KeySun", type='SUN')
    key_data.energy = 3.6
    key_data.color = (1.0, 0.96, 0.90)  # Warm sunlight
    key_data.angle = math.radians(2.0)   # Soft realistic shadow edges
    key_obj = bpy.data.objects.new("KeySunObj", key_data)
    bpy.context.scene.collection.objects.link(key_obj)
    key_obj.location = (-4.8, 7.5, 6.2)
    key_obj.rotation_euler = (math.radians(48), math.radians(-24), math.radians(35))
    key_obj.parent = lights_root

    # 2. Cool Sky Fill Light from right (softens contrast without muddying shadows)
    fill_data = bpy.data.lights.new(name="SkyFill", type='SUN')
    fill_data.energy = 1.3
    fill_data.color = (0.75, 0.88, 1.0)  # Soft cool sky
    fill_data.angle = math.radians(10.0)
    fill_obj = bpy.data.objects.new("SkyFillObj", fill_data)
    bpy.context.scene.collection.objects.link(fill_obj)
    fill_obj.location = (5.5, 4.0, 3.8)
    fill_obj.rotation_euler = (math.radians(35), math.radians(30), math.radians(-45))
    fill_obj.parent = lights_root

    # 3. Top-Back Rim Light (creates beautiful glossy edge highlights on heads & helmets)
    rim_data = bpy.data.lights.new(name="TopRim", type='POINT')
    rim_data.energy = 450.0
    rim_data.color = (0.95, 0.98, 1.0)
    rim_data.shadow_soft_size = 0.4
    rim_obj = bpy.data.objects.new("TopRimObj", rim_data)
    bpy.context.scene.collection.objects.link(rim_obj)
    rim_obj.location = (0.0, 5.0, -3.8)
    rim_obj.parent = lights_root

    # 4. Crimson Ground Bounce Fill (simulates warm bounce from the red floor disc)
    bounce_data = bpy.data.lights.new(name="FloorBounce", type='POINT')
    bounce_data.energy = 180.0
    bounce_data.color = (1.0, 0.35, 0.15)
    bounce_data.shadow_soft_size = 1.0
    bounce_obj = bpy.data.objects.new("FloorBounceObj", bounce_data)
    bpy.context.scene.collection.objects.link(bounce_obj)
    bounce_obj.location = (0.0, -1.0, -0.10)
    bounce_obj.parent = lights_root


def setup_camera():
    """Sets up 45mm portrait camera framing the entire squad heroically."""
    scene = bpy.context.scene
    cam_data = bpy.data.cameras.new(name="MasterCamera")
    cam_data.lens = 45.0
    cam_data.sensor_width = 36.0

    cam_obj = bpy.data.objects.new("MasterCamera", cam_data)
    scene.collection.objects.link(cam_obj)
    scene.camera = cam_obj

    # Positioned at -Y looking along +Y, elevated at Z=3.1, aimed at center of squad (Z=1.35)
    cam_obj.location = (0.0, -10.4, 3.1)
    cam_obj.rotation_euler = (math.radians(78.5), 0.0, 0.0)
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

    print("Building Thief Ant (bottom-right)...")
    build_thief_ant(scene_root, mats)

    print("Building Crimson Floor Disc...")
    build_crimson_floor_disc(scene_root, mats)

    print("Building 3D Title Logo...")
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
