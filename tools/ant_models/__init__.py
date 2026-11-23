"""Procedural 3D stylized tabletop/vinyl ant model toolkit."""
from __future__ import annotations

from .gltf_writer import parse_glb, scene_to_glb_bytes, validate_glb, write_glb
from .orig_sprites import build_orig_sprites_bundle, can_generate_orig_sprites, write_orig_sprites
from .materials import (
    MATERIAL_ORDER,
    MATERIALS,
    TEAM_COLORS,
    MaterialSpec,
    get_material,
    material_color,
    material_linear_color,
    srgb_rgba_to_linear,
    srgb_to_linear,
)

try:
    import numpy as _np  # noqa: F401

    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False

if HAS_NUMPY:
    from .base_ant import BROW_STYLES, EYELID_CLOSED_ANGLE, add_idle_animation, apply_head_pose, build_base_ant
    from .leg_rig import (
        ARM_NODES,
        LEG_NODES,
        LEG_STYLES,
        add_leg_flex_channels,
        sync_arm_idle_to_clip,
        sync_leg_flex_to_clip,
    )
    from .mesh_core import (
        AnimationChannel,
        AnimationClip,
        MeshPart,
        NodeTransform,
        SceneBuilder,
        bake_scene_ao,
        capsule,
        catmull_rom_spline,
        cone,
        curved_tube,
        cylinder,
        ellipsoid,
        ensure_outward_winding,
        extrude_polygon,
        hemisphere_shell,
        lathe,
        merge_parts,
        quat_from_axis_angle,
        quat_from_euler_xyz,
        quat_mul,
        recompute_smooth_normals,
        rounded_box,
        signed_volume,
        sphere,
        superellipsoid,
        swept_tube,
        torus_arc,
    )
    from .textures import TexturePack, get_texture_pack

__all__ = [
    "HAS_NUMPY",
    "MATERIALS",
    "MATERIAL_ORDER",
    "TEAM_COLORS",
    "MaterialSpec",
    "get_material",
    "material_color",
    "material_linear_color",
    "srgb_rgba_to_linear",
    "srgb_to_linear",
    "parse_glb",
    "scene_to_glb_bytes",
    "validate_glb",
    "write_glb",
    "build_orig_sprites_bundle",
    "can_generate_orig_sprites",
    "write_orig_sprites",
    "BROW_STYLES",
    "EYELID_CLOSED_ANGLE",
    "add_idle_animation",
    "apply_head_pose",
    "ARM_NODES",
    "LEG_NODES",
    "LEG_STYLES",
    "add_leg_flex_channels",
    "sync_arm_idle_to_clip",
    "sync_leg_flex_to_clip",
    "AnimationChannel",
    "AnimationClip",
    "MeshPart",
    "NodeTransform",
    "SceneBuilder",
    "TexturePack",
    "bake_scene_ao",
    "build_base_ant",
    "get_texture_pack",
    "capsule",
    "catmull_rom_spline",
    "cone",
    "curved_tube",
    "cylinder",
    "ellipsoid",
    "ensure_outward_winding",
    "extrude_polygon",
    "hemisphere_shell",
    "lathe",
    "merge_parts",
    "quat_from_axis_angle",
    "quat_from_euler_xyz",
    "quat_mul",
    "recompute_smooth_normals",
    "rounded_box",
    "signed_volume",
    "sphere",
    "superellipsoid",
    "swept_tube",
    "torus_arc",
]
