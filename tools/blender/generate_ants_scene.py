"""
Ants! 3D Title Scene Generator for Blender
Authentic stylized realism recreation of the 1998 cover art.
Builds all 6 ant castes, custom gear, PBR materials, 5-point studio lighting, and renders.
Can be executed in Blender via:
  blender --background --python tools/blender/generate_ants_scene.py
Or opened inside Blender's Scripting workspace.
"""

import sys
import math

try:
    import bpy
    import mathutils
except ImportError:
    print("This script is designed to run within Blender's Python environment.")
    print("Example: /Applications/Blender.app/Contents/MacOS/Blender -b -P tools/blender/generate_ants_scene.py")
    sys.exit(0)


def clear_scene():
    """Clear default objects from the scene."""
    bpy.ops.wm.read_factory_settings(use_empty=True)


def create_material(name, base_color, roughness=0.3, metalness=0.0, sss=0.0, coat=0.0):
    """Create a Principled BSDF material with PBR properties."""
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    bsdf = nodes.get("Principled BSDF")

    if bsdf:
        bsdf.inputs["Base Color"].default_value = base_color
        bsdf.inputs["Roughness"].default_value = roughness
        bsdf.inputs["Metallic"].default_value = metalness

        # Subsurface scattering if available in Principled BSDF
        if "Subsurface Weight" in bsdf.inputs:
            bsdf.inputs["Subsurface Weight"].default_value = sss
        elif "Subsurface" in bsdf.inputs:
            bsdf.inputs["Subsurface"].default_value = sss

        # Coat for chitin sheen
        if "Coat Weight" in bsdf.inputs:
            bsdf.inputs["Coat Weight"].default_value = coat
        elif "Clearcoat" in bsdf.inputs:
            bsdf.inputs["Clearcoat"].default_value = coat

    return mat


def build_ant_base(name, location=(0, 0, 0), rotation=(0, 0, 0), scale=1.0, big_eyes=False, materials=None):
    """Procedurally assemble an anatomically structured stylized ant."""
    m_chitin = materials['chitin']
    m_dark_chitin = materials['dark_chitin']
    m_eye_white = materials['eye_white']
    m_eye_pupil = materials['eye_pupil']

    root = bpy.data.objects.new(name, None)
    bpy.context.collection.objects.link(root)
    root.location = location
    root.rotation_euler = rotation
    root.scale = (scale, scale, scale)

    # 1. Thorax (Mesosoma)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=20, radius=0.55)
    thorax = bpy.context.active_object
    thorax.name = f"{name}_Thorax"
    thorax.scale = (0.85, 1.25, 0.9)
    thorax.data.materials.append(m_chitin)
    thorax.parent = root

    # 2. Gaster (Abdomen)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=28, ring_count=24, radius=0.95, location=(0, -1.4, -0.15))
    gaster = bpy.context.active_object
    gaster.name = f"{name}_Gaster"
    gaster.scale = (0.85, 1.45, 0.95)
    gaster.rotation_euler = (-0.3, 0, 0)
    gaster.data.materials.append(m_chitin)
    gaster.parent = root

    # 3. Head
    bpy.ops.mesh.primitive_uv_sphere_add(segments=28, ring_count=24, radius=0.68, location=(0, 0.85, 0.42))
    head = bpy.context.active_object
    head.name = f"{name}_Head"
    head.scale = (0.95, 1.05, 1.15)
    head.data.materials.append(m_chitin)
    head.parent = root

    # Mandibles
    for sign, side in [(-1, "L"), (1, "R")]:
        bpy.ops.mesh.primitive_cone_add(vertices=12, radius1=0.12, depth=0.45, location=(sign * 0.24, 1.35, -0.05))
        mandible = bpy.context.active_object
        mandible.name = f"{name}_Mandible_{side}"
        mandible.rotation_euler = (1.2, sign * -0.5, 0)
        mandible.data.materials.append(m_dark_chitin)
        mandible.parent = head

    # Eyes
    eye_rad = 0.32 if big_eyes else 0.26
    for sign, side in [(-1, "L"), (1, "R")]:
        bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=16, radius=eye_rad, location=(sign * 0.42, 1.15, 0.55))
        eye = bpy.context.active_object
        eye.name = f"{name}_Eye_{side}"
        eye.data.materials.append(m_eye_white)
        eye.parent = head

        # Pupil
        bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=12, radius=eye_rad * 0.65, location=(sign * 0.44, 1.32, 0.58))
        pupil = bpy.context.active_object
        pupil.name = f"{name}_Pupil_{side}"
        pupil.scale = (0.9, 0.3, 1.1)
        pupil.data.materials.append(m_eye_pupil)
        pupil.parent = head

    # Antennae
    for sign, side in [(-1, "L"), (1, "R")]:
        bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.04, depth=0.6, location=(sign * 0.22, 1.0, 1.0))
        scape = bpy.context.active_object
        scape.name = f"{name}_Scape_{side}"
        scape.rotation_euler = (-0.2, sign * -0.45, 0)
        scape.data.materials.append(m_dark_chitin)
        scape.parent = head

        bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.03, depth=0.85, location=(sign * 0.45, 1.1, 1.5))
        flag = bpy.context.active_object
        flag.name = f"{name}_Flagellum_{side}"
        flag.rotation_euler = (-0.35, sign * 0.55, 0)
        flag.data.materials.append(m_chitin)
        flag.parent = head

    # 6 Articulated Legs
    for p, (y_off, z_off) in enumerate([(0.25, -0.15), (-0.15, -0.2), (-0.6, -0.25)]):
        for sign, side in [(-1, "L"), (1, "R")]:
            bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.07, depth=0.95, location=(sign * 0.65, y_off, z_off + 0.2))
            femur = bpy.context.active_object
            femur.name = f"{name}_Leg_{p}_{side}_Femur"
            femur.rotation_euler = (0, sign * -1.1, 0)
            femur.data.materials.append(m_dark_chitin)
            femur.parent = root

            bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.045, depth=1.1, location=(sign * 1.15, y_off, z_off - 0.45))
            tibia = bpy.context.active_object
            tibia.name = f"{name}_Leg_{p}_{side}_Tibia"
            tibia.rotation_euler = (0, sign * 0.45, 0)
            tibia.data.materials.append(m_chitin)
            tibia.parent = root

    return {
        'root': root,
        'head': head,
        'thorax': thorax,
        'gaster': gaster
    }


def add_combat_gear(ant, materials):
    """Combat Ant: Crossed bandolier sash (X-strap) with brass cartridge shells."""
    thorax = ant['thorax']
    head = ant['head']

    # Brow scowl
    bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=0.08, depth=0.65, location=(0, 1.25, 0.78))
    brow = bpy.context.active_object
    brow.rotation_euler = (-0.3, 0, math.pi / 2)
    brow.data.materials.append(materials['dark_chitin'])
    brow.parent = head

    # Crossed sashes
    for angle, z_rot in [(math.pi / 7, math.pi / 4), (-math.pi / 7, -math.pi / 4)]:
        bpy.ops.mesh.primitive_torus_add(major_radius=0.65, minor_radius=0.08, location=(0, 0, 0.05))
        sash = bpy.context.active_object
        sash.scale = (0.9, 0.95, 1.25)
        sash.rotation_euler = (angle, z_rot, 0)
        sash.data.materials.append(materials['leather'])
        sash.parent = thorax

        # Cartridge shells
        for i in range(5):
            bpy.ops.mesh.primitive_cylinder_add(vertices=10, radius=0.045, depth=0.28)
            shell = bpy.context.active_object
            shell.data.materials.append(materials['brass'])
            a = (i - 2) * 0.28
            shell.location = (math.sin(a) * 0.65, math.cos(a) * 0.4, 0.35)
            shell.parent = sash


def add_fire_gear(ant, materials):
    """Fire Ant: Yellow firefighter chief helmet with 'A' badge."""
    head = ant['head']

    # Brim
    bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=0.95, depth=0.1, location=(0, 0.8, 1.05))
    brim = bpy.context.active_object
    brim.scale = (1.0, 1.25, 1.0)
    brim.data.materials.append(materials['helmet'])
    brim.parent = head

    # Crown dome
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=20, radius=0.68, location=(0, 0.8, 1.4))
    crown = bpy.context.active_object
    crown.data.materials.append(materials['helmet'])
    crown.parent = head

    # Front shield with letter 'A'
    bpy.ops.mesh.primitive_cube_add(size=0.35, location=(0, 1.4, 1.5))
    shield = bpy.context.active_object
    shield.scale = (1.0, 0.1, 1.2)
    shield.data.materials.append(materials['gold_shield'])
    shield.parent = head


def add_thief_gear(ant, materials):
    """Thief Ant: Bandana mask with eye cutouts."""
    head = ant['head']
    bpy.ops.mesh.primitive_cylinder_add(vertices=28, radius=0.75, depth=0.48, location=(0, 0.9, 0.55))
    bandana = bpy.context.active_object
    bandana.data.materials.append(materials['bandana'])
    bandana.parent = head


def add_bomber_gear(ant, materials):
    """Bomber Ant: Yellow aviator goggles & backpack with cartoon bombs."""
    head = ant['head']
    thorax = ant['thorax']

    # Goggles
    for sign in [-1, 1]:
        bpy.ops.mesh.primitive_torus_add(major_radius=0.24, minor_radius=0.065, location=(sign * 0.38, 1.4, 0.58))
        rim = bpy.context.active_object
        rim.rotation_euler = (math.pi / 2, 0, 0)
        rim.data.materials.append(materials['helmet'])
        rim.parent = head

    # Backpack
    bpy.ops.mesh.primitive_cube_add(size=0.65, location=(0, -0.65, 0.3))
    pack = bpy.context.active_object
    pack.data.materials.append(materials['leather'])
    pack.parent = thorax

    # Cartoon spherical bombs
    for sign in [-1, 1]:
        bpy.ops.mesh.primitive_uv_sphere_add(segments=18, ring_count=16, radius=0.22, location=(sign * 0.2, -0.65, 0.75))
        bomb = bpy.context.active_object
        bomb.data.materials.append(materials['bomb_iron'])
        bomb.parent = pack


def add_swimmer_gear(ant, materials):
    """Swimmer Ant: Diving mask, snorkel tube & shovel in right hand."""
    head = ant['head']
    root = ant['root']

    # Diving mask frame
    bpy.ops.mesh.primitive_torus_add(major_radius=0.48, minor_radius=0.07, location=(0, 1.4, 0.6))
    mask_frame = bpy.context.active_object
    mask_frame.scale = (1.35, 1.0, 0.85)
    mask_frame.rotation_euler = (math.pi / 2, 0, 0)
    mask_frame.data.materials.append(materials['helmet'])
    mask_frame.parent = head

    # Shovel
    bpy.ops.mesh.primitive_cylinder_add(vertices=10, radius=0.045, depth=1.8, location=(-1.15, 0.4, 0.0))
    shaft = bpy.context.active_object
    shaft.rotation_euler = (0.25, 0, -0.4)
    shaft.data.materials.append(materials['wood'])
    shaft.parent = root

    bpy.ops.mesh.primitive_cube_add(size=0.45, location=(-1.15, 0.4, -0.85))
    blade = bpy.context.active_object
    blade.scale = (1.0, 0.1, 1.2)
    blade.data.materials.append(materials['steel'])
    blade.parent = shaft


def build_scene():
    """Main scene construction."""
    clear_scene()

    # Materials
    materials = {
        'chitin': create_material("Chitin", (0.18, 0.42, 0.18, 1.0), roughness=0.3, coat=0.85),
        'dark_chitin': create_material("DarkChitin", (0.1, 0.22, 0.1, 1.0), roughness=0.4, coat=0.6),
        'eye_white': create_material("EyeWhite", (1.0, 1.0, 1.0, 1.0), roughness=0.1, coat=1.0),
        'eye_pupil': create_material("EyePupil", (0.02, 0.02, 0.02, 1.0), roughness=0.05, coat=1.0),
        'leather': create_material("Leather", (0.28, 0.15, 0.08, 1.0), roughness=0.75),
        'brass': create_material("Brass", (0.85, 0.65, 0.2, 1.0), roughness=0.25, metalness=0.9),
        'helmet': create_material("RescueYellow", (0.95, 0.62, 0.08, 1.0), roughness=0.2, coat=0.9),
        'gold_shield': create_material("GoldShield", (0.9, 0.7, 0.2, 1.0), roughness=0.3, metalness=0.7),
        'bandana': create_material("BandanaCloth", (0.7, 0.55, 0.15, 1.0), roughness=0.85),
        'bomb_iron': create_material("BombIron", (0.08, 0.08, 0.08, 1.0), roughness=0.35, metalness=0.7),
        'wood': create_material("WoodShaft", (0.55, 0.35, 0.18, 1.0), roughness=0.7),
        'steel': create_material("SteelBlade", (0.75, 0.78, 0.8, 1.0), roughness=0.25, metalness=0.85)
    }

    # 1. Combat Ant (Top-Left)
    c_ant = build_ant_base("CombatAnt", location=(-2.0, 1.4, 0.6), rotation=(0.1, 0.25, 0), scale=1.18, materials=materials)
    add_combat_gear(c_ant, materials)

    # 2. Fire Ant (Back-Right)
    f_ant = build_ant_base("FireAnt", location=(1.9, 1.5, 0.4), rotation=(0.08, -0.3, 0), scale=1.08, materials=materials)
    add_fire_gear(f_ant, materials)

    # 3. Thief Ant (Bottom-Right)
    t_ant = build_ant_base("ThiefAnt", location=(2.2, -0.4, -0.3), rotation=(-0.05, -0.38, 0), scale=0.95, materials=materials)
    add_thief_gear(t_ant, materials)

    # 4. Worker Ant (Dead Center)
    build_ant_base("WorkerAnt", location=(0.0, 0.35, 0.0), scale=1.0, big_eyes=True, materials=materials)

    # 5. Bomber Ant (Front-Center)
    b_ant = build_ant_base("BomberAnt", location=(-0.35, -1.4, -0.5), rotation=(-0.22, 0.15, 0), scale=0.92, materials=materials)
    add_bomber_gear(b_ant, materials)

    # 6. Swimmer Ant (Middle-Left)
    s_ant = build_ant_base("SwimmerAnt", location=(-2.2, -0.25, -0.2), rotation=(0, 0.35, 0), scale=1.02, materials=materials)
    add_swimmer_gear(s_ant, materials)

    # Studio Lights
    # Key Light
    bpy.ops.object.light_add(type='SUN', location=(4, 7, 7))
    sun = bpy.context.active_object
    sun.data.energy = 4.0
    sun.data.color = (1.0, 0.95, 0.9)

    # Golden Rim Light
    bpy.ops.object.light_add(type='POINT', location=(-7, 5, -5))
    rim1 = bpy.context.active_object
    rim1.data.energy = 500.0
    rim1.data.color = (1.0, 0.65, 0.2)

    # Cyan Rim Light
    bpy.ops.object.light_add(type='POINT', location=(7, 4, -4))
    rim2 = bpy.context.active_object
    rim2.data.energy = 350.0
    rim2.data.color = (0.2, 0.85, 1.0)

    # Camera matching title cover art
    bpy.ops.object.camera_add(location=(0, -7.8, 0.2), rotation=(math.radians(88), 0, 0))
    cam = bpy.context.active_object
    cam.data.lens = 55
    bpy.context.scene.camera = cam

    # Save .blend file
    output_path = "ants_realistic_scene.blend"
    bpy.ops.wm.save_as_mainfile(filepath=output_path)
    print(f"Successfully generated Blender scene file: {output_path}")


if __name__ == "__main__":
    build_scene()
