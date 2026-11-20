"""Procedural 3D stylized tabletop/vinyl ant model toolkit."""
from __future__ import annotations

from .gltf_writer import parse_glb, scene_to_glb_bytes, validate_glb, write_glb
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
    from .base_ant import EYELID_CLOSED_ANGLE, build_base_ant
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
    "EYELID_CLOSED_ANGLE",
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
