"""Realistic PBR materials and organic vertex-color tint helpers for 3D map items.

Registers extension `MaterialSpec` entries into `materials.EXTRA_MATERIALS` without
altering `materials.MATERIAL_ORDER` (keeping every ant class material contract intact).
Pure-stdlib safe on import (numpy is only needed by vertex-tinting mesh helpers).
"""
from __future__ import annotations

from typing import Callable, Optional, Tuple

try:
    import numpy as np
except ImportError:  # pragma: no cover - pure-stdlib import safety for CI
    np = None  # type: ignore

try:
    from ..materials import MaterialSpec, register_extra_material
except ImportError:
    from materials import MaterialSpec, register_extra_material  # type: ignore

_REGISTERED = False


def register_item_materials() -> None:
    """Idempotently register all realistic PBR materials for 3D map items."""
    global _REGISTERED
    if _REGISTERED:
        return
    specs = (
        # --- Plush / Fabric / Cotton / Waving Flags (Teddy bear, stuffing, cloth flags) ---
        MaterialSpec("item_bear_fur", (0.58, 0.36, 0.19, 1.0), 0.00, 0.92, texture_family="fur", normal_scale=0.74),
        MaterialSpec("item_bear_muzzle", (0.78, 0.62, 0.40, 1.0), 0.00, 0.90, texture_family="fur", normal_scale=0.64),
        MaterialSpec("item_bear_belly", (0.82, 0.66, 0.42, 1.0), 0.00, 0.90, texture_family="fur", normal_scale=0.64),
        MaterialSpec("item_cotton_fluff", (0.94, 0.93, 0.90, 1.0), 0.00, 0.95, texture_family="fur", normal_scale=0.68),
        MaterialSpec("item_dark_stitch", (0.09, 0.07, 0.06, 1.0), 0.00, 0.78, texture_family="cloth", normal_scale=0.42),
        MaterialSpec("item_flag_black", (0.19, 0.21, 0.26, 1.0), 0.00, 0.84, double_sided=True, texture_family="cloth", normal_scale=0.56),
        MaterialSpec("item_flag_blue", (0.20, 0.38, 0.94, 1.0), 0.00, 0.82, double_sided=True, texture_family="cloth", normal_scale=0.56),
        MaterialSpec("item_flag_red", (0.92, 0.14, 0.12, 1.0), 0.00, 0.82, double_sided=True, texture_family="cloth", normal_scale=0.56),
        MaterialSpec("item_flag_green", (0.24, 0.88, 0.20, 1.0), 0.00, 0.80, double_sided=True, texture_family="cloth", normal_scale=0.56),
        # --- Tactile Matte Rubber & Textured Dinosaur Hide (Zero cheap plastic glare!) ---
        MaterialSpec("item_duck_yellow", (0.97, 0.83, 0.14, 1.0), 0.00, 0.52, texture_family="vinyl_rubber", normal_scale=0.38),
        MaterialSpec("item_duck_bill", (0.94, 0.28, 0.16, 1.0), 0.00, 0.48, texture_family="vinyl_rubber", normal_scale=0.34),
        MaterialSpec("item_toy_green", (0.17, 0.58, 0.32, 1.0), 0.00, 0.56, texture_family="dino_skin", normal_scale=0.64),
        MaterialSpec("item_toy_green_dark", (0.11, 0.40, 0.22, 1.0), 0.00, 0.60, texture_family="dino_skin", normal_scale=0.68),
        MaterialSpec("item_dino_belly", (0.52, 0.76, 0.42, 1.0), 0.00, 0.60, texture_family="dino_skin", normal_scale=0.46),
        MaterialSpec("item_dino_claw", (0.88, 0.85, 0.74, 1.0), 0.00, 0.50, texture_family="stone", normal_scale=0.26),
        MaterialSpec("item_plastic_red", (0.86, 0.14, 0.14, 1.0), 0.02, 0.46, texture_family="vinyl_rubber", normal_scale=0.32),
        MaterialSpec("item_plastic_blue", (0.16, 0.38, 0.86, 1.0), 0.02, 0.46, texture_family="vinyl_rubber", normal_scale=0.32),
        MaterialSpec("item_plastic_pink", (0.92, 0.24, 0.54, 1.0), 0.02, 0.46, texture_family="vinyl_rubber", normal_scale=0.32),
        MaterialSpec("item_pin_green", (0.14, 0.62, 0.34, 1.0), 0.02, 0.46, texture_family="vinyl_rubber", normal_scale=0.32),
        MaterialSpec("item_sword_blue", (0.56, 0.78, 0.98, 1.0), 0.06, 0.42, texture_family="vinyl_rubber", normal_scale=0.26),
        # --- Metallic / Painted Aluminum / Printed Can & Cap Graphics ---
        MaterialSpec("item_cola_label", (1.00, 1.00, 1.00, 1.0), 0.56, 0.34, texture_family="cola_label", normal_scale=0.32),
        MaterialSpec("item_cap_h2o", (1.00, 1.00, 1.00, 1.0), 0.44, 0.40, texture_family="cap_h2o", normal_scale=0.26),
        MaterialSpec("item_cap_soda", (1.00, 1.00, 1.00, 1.0), 0.44, 0.42, texture_family="cap_soda", normal_scale=0.26),
        MaterialSpec("item_can_purple", (0.52, 0.38, 0.88, 1.0), 0.68, 0.28, texture_family="metal", normal_scale=0.24),
        MaterialSpec("item_can_red", (0.84, 0.14, 0.22, 1.0), 0.66, 0.28, texture_family="metal", normal_scale=0.24),
        MaterialSpec("item_can_white", (0.96, 0.96, 0.98, 1.0), 0.45, 0.30, texture_family="metal", normal_scale=0.18),
        MaterialSpec("item_aluminum", (0.84, 0.86, 0.90, 1.0), 0.88, 0.22, texture_family="metal", normal_scale=0.28),
        MaterialSpec("item_bottle_teal", (0.10, 0.56, 0.46, 1.0), 0.58, 0.24, texture_family="metal", normal_scale=0.22),
        # --- Realistic Food Surfaces (Baked crust, Seared meat, Fresh veggies, Candy, Translucent Jelly) ---
        MaterialSpec("item_bun_golden", (0.84, 0.58, 0.30, 1.0), 0.00, 0.76, texture_family="bread", normal_scale=0.52),
        MaterialSpec("item_bun_crumb", (0.95, 0.88, 0.58, 1.0), 0.00, 0.88, texture_family="bread", normal_scale=0.66),
        MaterialSpec("item_sesame", (0.96, 0.91, 0.76, 1.0), 0.00, 0.56, texture_family="organic_flesh", normal_scale=0.24),
        MaterialSpec("item_beef_patty", (0.34, 0.17, 0.10, 1.0), 0.00, 0.68, texture_family="soil", normal_scale=0.58),
        MaterialSpec("item_cheese", (0.96, 0.70, 0.12, 1.0), 0.00, 0.48, texture_family="organic_flesh", normal_scale=0.26),
        MaterialSpec("item_lettuce", (0.38, 0.78, 0.20, 1.0), 0.00, 0.46, double_sided=True, texture_family="foliage", normal_scale=0.46),
        MaterialSpec("item_tomato", (0.88, 0.14, 0.10, 1.0), 0.00, 0.32, texture_family="organic_flesh", normal_scale=0.30),
        MaterialSpec("item_onion", (0.95, 0.93, 0.86, 1.0), 0.00, 0.38, texture_family="organic_flesh", normal_scale=0.26),
        MaterialSpec("item_corndog_crust", (0.86, 0.62, 0.22, 1.0), 0.00, 0.78, texture_family="bread", normal_scale=0.68),
        MaterialSpec("item_hotdog_pink", (0.76, 0.42, 0.30, 1.0), 0.00, 0.48, texture_family="organic_flesh", normal_scale=0.36),
        MaterialSpec("item_hotdog_inside", (0.86, 0.58, 0.46, 1.0), 0.00, 0.62, texture_family="organic_flesh", normal_scale=0.42),
        MaterialSpec("item_mustard", (0.96, 0.78, 0.08, 1.0), 0.00, 0.36, texture_family="organic_flesh", normal_scale=0.22),
        MaterialSpec("item_wood_stick", (0.72, 0.52, 0.32, 1.0), 0.00, 0.82, texture_family="wood", normal_scale=0.50),
        MaterialSpec("item_birch_pole", (0.90, 0.88, 0.80, 1.0), 0.00, 0.78, texture_family="wood", normal_scale=0.46),
        MaterialSpec("item_chicken_crust", (0.86, 0.50, 0.18, 1.0), 0.00, 0.68, texture_family="bread", normal_scale=0.78),
        MaterialSpec("item_bone_white", (0.92, 0.90, 0.84, 1.0), 0.00, 0.48, texture_family="stone", normal_scale=0.24),
        MaterialSpec("item_jello_ruby", (0.86, 0.05, 0.13, 0.62), 0.02, 0.06, double_sided=True, alpha_mode="BLEND"),
        MaterialSpec("item_banana_slice", (0.98, 0.90, 0.56, 1.0), 0.00, 0.52, texture_family="organic_flesh", normal_scale=0.28),
        MaterialSpec("item_candy_red", (0.92, 0.08, 0.10, 1.0), 0.02, 0.12, texture_family=None),
        MaterialSpec("item_candy_white", (0.98, 0.97, 0.95, 1.0), 0.02, 0.14, texture_family=None),
        MaterialSpec("item_chocolate", (0.28, 0.14, 0.08, 1.0), 0.00, 0.46, texture_family="organic_flesh", normal_scale=0.26),
        # --- Realistic Botanical Grass, Plants, Clovers, Flowers, Mushroom ---
        MaterialSpec("item_grass_blade", (0.36, 0.80, 0.16, 1.0), 0.00, 0.58, double_sided=True, texture_family="foliage", normal_scale=0.48),
        MaterialSpec("item_leaf_rich", (0.30, 0.72, 0.18, 1.0), 0.00, 0.54, double_sided=True, texture_family="foliage", normal_scale=0.48),
        MaterialSpec("item_leaf_oak", (0.20, 0.56, 0.16, 1.0), 0.00, 0.56, double_sided=True, texture_family="foliage", normal_scale=0.52),
        MaterialSpec("item_leaf_bright", (0.42, 0.84, 0.22, 1.0), 0.00, 0.52, double_sided=True, texture_family="foliage", normal_scale=0.46),
        MaterialSpec("item_stem_green", (0.26, 0.58, 0.16, 1.0), 0.00, 0.62, texture_family="foliage", normal_scale=0.42),
        MaterialSpec("item_petal_white", (0.97, 0.97, 0.94, 1.0), 0.00, 0.56, double_sided=True, texture_family="organic_flesh", normal_scale=0.26),
        MaterialSpec("item_petal_pink", (0.94, 0.30, 0.58, 1.0), 0.00, 0.52, double_sided=True, texture_family="organic_flesh", normal_scale=0.28),
        MaterialSpec("item_petal_rose", (0.84, 0.18, 0.44, 1.0), 0.00, 0.52, double_sided=True, texture_family="organic_flesh", normal_scale=0.28),
        MaterialSpec("item_petal_lilac", (0.76, 0.38, 0.88, 1.0), 0.00, 0.50, double_sided=True, texture_family="organic_flesh", normal_scale=0.28),
        MaterialSpec("item_petal_violet", (0.48, 0.22, 0.78, 1.0), 0.00, 0.52, double_sided=True, texture_family="organic_flesh", normal_scale=0.28),
        MaterialSpec("item_tongue_pink", (0.92, 0.28, 0.32, 1.0), 0.00, 0.42, texture_family="organic_flesh", normal_scale=0.30),
        MaterialSpec("item_pollen_gold", (0.96, 0.72, 0.10, 1.0), 0.00, 0.76, texture_family="soil", normal_scale=0.52),
        MaterialSpec("item_groucho_nose", (0.86, 0.56, 0.48, 1.0), 0.00, 0.52, texture_family="vinyl_rubber", normal_scale=0.28),
        MaterialSpec("item_mush_cap", (0.64, 0.56, 0.48, 1.0), 0.00, 0.68, texture_family="soil", normal_scale=0.44),
        MaterialSpec("item_mush_orange", (0.86, 0.44, 0.18, 1.0), 0.00, 0.66, texture_family="soil", normal_scale=0.42),
        MaterialSpec("item_mush_spot", (0.30, 0.88, 0.54, 1.0), 0.00, 0.56, texture_family="organic_flesh", normal_scale=0.32),
        MaterialSpec("item_mush_stem", (0.66, 0.63, 0.58, 1.0), 0.00, 0.72, texture_family="wood", normal_scale=0.40),
        # --- Rocks, Moss, Mud Bridges, Anthills & Gameplay Gear ---
        MaterialSpec("item_stone_granite", (0.56, 0.56, 0.54, 1.0), 0.02, 0.84, texture_family="stone", normal_scale=0.76),
        MaterialSpec("item_stone_moss", (0.28, 0.76, 0.16, 1.0), 0.00, 0.88, texture_family="soil", normal_scale=0.64),
        MaterialSpec("item_soil_mound", (0.64, 0.54, 0.44, 1.0), 0.00, 0.92, texture_family="soil", normal_scale=0.78),
        MaterialSpec("item_soil_grey", (0.68, 0.65, 0.58, 1.0), 0.00, 0.92, texture_family="soil", normal_scale=0.78),
        MaterialSpec("item_soil_pink", (0.82, 0.64, 0.58, 1.0), 0.00, 0.90, texture_family="soil", normal_scale=0.76),
        MaterialSpec("item_soil_mud", (0.42, 0.37, 0.31, 1.0), 0.02, 0.72, texture_family="soil", normal_scale=0.72),
        MaterialSpec("item_water_ripple", (0.24, 0.26, 0.64, 0.84), 0.04, 0.14, double_sided=True, alpha_mode="BLEND"),
        MaterialSpec("item_soil_hole", (0.08, 0.06, 0.05, 1.0), 0.00, 0.96, texture_family="soil", normal_scale=0.44),
        MaterialSpec("item_twig_bark", (0.52, 0.34, 0.24, 1.0), 0.00, 0.86, texture_family="wood", normal_scale=0.62),
        MaterialSpec("item_party_green", (0.20, 0.82, 0.24, 1.0), 0.00, 0.62, texture_family="cloth", normal_scale=0.38),
        MaterialSpec("item_gear_gold", (0.94, 0.74, 0.14, 1.0), 0.56, 0.34, texture_family="metal", normal_scale=0.26),
        MaterialSpec("item_gear_brass", (0.86, 0.64, 0.18, 1.0), 0.78, 0.28, texture_family="metal", normal_scale=0.30),
        MaterialSpec("item_steel_dark", (0.24, 0.26, 0.30, 1.0), 0.82, 0.34, texture_family="metal", normal_scale=0.34),
        MaterialSpec("item_rubber_dark", (0.16, 0.18, 0.24, 1.0), 0.02, 0.62, texture_family="rubber", normal_scale=0.38),
        MaterialSpec("item_rubber_blue", (0.16, 0.18, 0.50, 1.0), 0.02, 0.68, texture_family="rubber", normal_scale=0.46),
        MaterialSpec("item_bomb_purple", (0.64, 0.14, 0.56, 1.0), 0.22, 0.44, texture_family="metal", normal_scale=0.28),
        MaterialSpec("item_bomb_red", (0.90, 0.10, 0.12, 1.0), 0.20, 0.42, texture_family="metal", normal_scale=0.28),
        MaterialSpec("item_bomb_green", (0.14, 0.56, 0.18, 1.0), 0.20, 0.44, texture_family="metal", normal_scale=0.28),
        MaterialSpec("item_bomb_blue", (0.20, 0.36, 0.88, 1.0), 0.20, 0.42, texture_family="metal", normal_scale=0.28),
        MaterialSpec("item_bomb_black", (0.18, 0.20, 0.24, 1.0), 0.28, 0.42, texture_family="metal", normal_scale=0.30),
        MaterialSpec("item_fuse_cord", (0.92, 0.88, 0.78, 1.0), 0.00, 0.88, texture_family="cloth", normal_scale=0.46),
        MaterialSpec("item_charcoal_ember", (0.26, 0.17, 0.11, 1.0), 0.00, 0.94, texture_family="soil", normal_scale=0.84),
        MaterialSpec("item_flame_white", (1.00, 1.00, 0.96, 1.0), 0.00, 0.98, emissive=(1.00, 1.00, 0.96)),
        MaterialSpec("item_flame_core", (1.00, 0.94, 0.22, 1.0), 0.00, 0.97, emissive=(1.00, 0.90, 0.14)),
        MaterialSpec("item_flame_amber", (1.00, 0.54, 0.03, 0.62), 0.00, 0.97, double_sided=True, alpha_mode="BLEND", emissive=(1.00, 0.44, 0.01)),
        MaterialSpec("item_flame_outer", (0.99, 0.48, 0.05, 0.80), 0.00, 0.96, double_sided=True, alpha_mode="BLEND", emissive=(0.94, 0.36, 0.02)),
        MaterialSpec("item_flame_crimson", (0.98, 0.20, 0.02, 0.74), 0.00, 0.97, double_sided=True, alpha_mode="BLEND", emissive=(0.98, 0.16, 0.01)),
        MaterialSpec("item_flame_spark", (0.98, 0.28, 0.02, 1.0), 0.00, 0.96, emissive=(0.98, 0.26, 0.02)),
        MaterialSpec("item_fire_volume", (1.00, 0.52, 0.04, 0.38), 0.00, 0.98, double_sided=True, alpha_mode="BLEND", emissive=(0.98, 0.42, 0.02)),
    )
    for spec in specs:
        register_extra_material(spec)
    _REGISTERED = True


def tint_part(part, color_fn: Callable[["np.ndarray", "np.ndarray"], "np.ndarray"]):
    """Apply sculptor-authored organic RGB vertex tinting preserved cleanly through `bake_scene_ao`."""
    if np is None or len(part.positions) == 0:
        return part
    cols = np.asarray(color_fn(part.positions.astype(np.float64), part.normals.astype(np.float64)), dtype=np.float32)
    cols = np.clip(cols, 0.04, 1.0)
    part.colors = cols
    part._base_tint = cols.copy()
    return part


def gradient_y_tint(
    part,
    low_rgb: Tuple[float, float, float],
    high_rgb: Tuple[float, float, float],
    *,
    y_min: Optional[float] = None,
    y_max: Optional[float] = None,
    noise_amp: float = 0.0,
):
    """Smooth vertical RGB tint gradient (e.g. earthy grass root -> sunlit blade tip, baked bun top)."""
    def _fn(pos: "np.ndarray", _nrm: "np.ndarray") -> "np.ndarray":
        y0 = float(pos[:, 1].min()) if y_min is None else float(y_min)
        y1 = float(pos[:, 1].max()) if y_max is None else float(y_max)
        t = np.clip((pos[:, 1] - y0) / max(1e-5, y1 - y0), 0.0, 1.0)[:, None]
        lo = np.asarray(low_rgb, dtype=np.float64)[None, :]
        hi = np.asarray(high_rgb, dtype=np.float64)[None, :]
        c = lo * (1.0 - t) + hi * t
        if noise_amp > 0.0:
            n = np.sin(pos[:, 0] * 31.0 + pos[:, 2] * 27.0) * np.cos(pos[:, 1] * 23.0 - pos[:, 0] * 17.0)
            c = c + noise_amp * n[:, None]
        return c

    return tint_part(part, _fn)
