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
        # --- Plush / Fabric / Cotton (Teddy bear, stuffing, cloth) ---
        MaterialSpec("item_bear_fur", (0.58, 0.36, 0.19, 1.0), 0.00, 0.90, texture_family="fur", normal_scale=0.68),
        MaterialSpec("item_bear_muzzle", (0.78, 0.62, 0.40, 1.0), 0.00, 0.88, texture_family="fur", normal_scale=0.58),
        MaterialSpec("item_bear_belly", (0.82, 0.66, 0.42, 1.0), 0.00, 0.88, texture_family="fur", normal_scale=0.58),
        MaterialSpec("item_cotton_fluff", (0.94, 0.93, 0.90, 1.0), 0.00, 0.94, texture_family="fur", normal_scale=0.64),
        MaterialSpec("item_dark_stitch", (0.09, 0.07, 0.06, 1.0), 0.02, 0.72, texture_family="cloth", normal_scale=0.35),
        # --- Glossy Vinyl / Molded Plastic Toys (Rubber ducky, Dino, Crayons) ---
        MaterialSpec("item_duck_yellow", (0.98, 0.84, 0.14, 1.0), 0.02, 0.24, texture_family=None),
        MaterialSpec("item_duck_bill", (0.94, 0.30, 0.10, 1.0), 0.02, 0.22, texture_family=None),
        MaterialSpec("item_toy_green", (0.16, 0.62, 0.30, 1.0), 0.03, 0.26, texture_family="chitin", normal_scale=0.14),
        MaterialSpec("item_toy_green_dark", (0.09, 0.38, 0.18, 1.0), 0.03, 0.28, texture_family="chitin", normal_scale=0.16),
        MaterialSpec("item_plastic_red", (0.86, 0.14, 0.14, 1.0), 0.03, 0.24, texture_family=None),
        MaterialSpec("item_plastic_blue", (0.16, 0.38, 0.86, 1.0), 0.03, 0.24, texture_family=None),
        MaterialSpec("item_plastic_pink", (0.92, 0.26, 0.62, 1.0), 0.03, 0.24, texture_family=None),
        # --- Metallic / Painted Aluminum (Soda cans, Bottle caps, Hardware) ---
        MaterialSpec("item_can_purple", (0.52, 0.38, 0.88, 1.0), 0.68, 0.26, texture_family="metal", normal_scale=0.20),
        MaterialSpec("item_can_red", (0.84, 0.14, 0.22, 1.0), 0.66, 0.26, texture_family="metal", normal_scale=0.20),
        MaterialSpec("item_can_white", (0.96, 0.96, 0.98, 1.0), 0.45, 0.28, texture_family="metal", normal_scale=0.14),
        MaterialSpec("item_aluminum", (0.84, 0.86, 0.90, 1.0), 0.88, 0.20, texture_family="metal", normal_scale=0.24),
        MaterialSpec("item_bottle_teal", (0.12, 0.56, 0.46, 1.0), 0.58, 0.24, texture_family="metal", normal_scale=0.18),
        # --- Realistic Food Surfaces (Baked crust, Seared meat, Fresh veggies, Candy, Translucent Jelly) ---
        MaterialSpec("item_bun_golden", (0.84, 0.58, 0.30, 1.0), 0.00, 0.72, texture_family="bread", normal_scale=0.46),
        MaterialSpec("item_bun_crumb", (0.92, 0.82, 0.62, 1.0), 0.00, 0.84, texture_family="bread", normal_scale=0.58),
        MaterialSpec("item_sesame", (0.96, 0.91, 0.76, 1.0), 0.00, 0.46, texture_family=None),
        MaterialSpec("item_beef_patty", (0.34, 0.17, 0.10, 1.0), 0.02, 0.62, texture_family="soil", normal_scale=0.52),
        MaterialSpec("item_cheese", (0.96, 0.70, 0.12, 1.0), 0.00, 0.34, texture_family=None),
        MaterialSpec("item_lettuce", (0.38, 0.78, 0.20, 1.0), 0.00, 0.36, double_sided=True, texture_family="foliage", normal_scale=0.38),
        MaterialSpec("item_tomato", (0.88, 0.14, 0.10, 1.0), 0.00, 0.16, texture_family=None),
        MaterialSpec("item_onion", (0.95, 0.93, 0.86, 1.0), 0.00, 0.24, texture_family=None),
        MaterialSpec("item_corndog_crust", (0.88, 0.62, 0.22, 1.0), 0.00, 0.74, texture_family="bread", normal_scale=0.58),
        MaterialSpec("item_hotdog_pink", (0.78, 0.38, 0.30, 1.0), 0.00, 0.38, texture_family=None),
        MaterialSpec("item_mustard", (0.96, 0.78, 0.08, 1.0), 0.00, 0.26, texture_family=None),
        MaterialSpec("item_wood_stick", (0.76, 0.58, 0.38, 1.0), 0.00, 0.78, texture_family="wood", normal_scale=0.44),
        MaterialSpec("item_chicken_crust", (0.86, 0.50, 0.18, 1.0), 0.00, 0.64, texture_family="bread", normal_scale=0.72),
        MaterialSpec("item_bone_white", (0.92, 0.90, 0.84, 1.0), 0.00, 0.42, texture_family="stone", normal_scale=0.18),
        MaterialSpec("item_jello_ruby", (0.86, 0.05, 0.13, 0.62), 0.02, 0.06, double_sided=True, alpha_mode="BLEND"),
        MaterialSpec("item_banana_slice", (0.98, 0.90, 0.56, 1.0), 0.00, 0.44, texture_family="bread", normal_scale=0.18),
        MaterialSpec("item_candy_red", (0.92, 0.08, 0.10, 1.0), 0.02, 0.12, texture_family=None),
        MaterialSpec("item_candy_white", (0.98, 0.97, 0.95, 1.0), 0.02, 0.14, texture_family=None),
        MaterialSpec("item_chocolate", (0.28, 0.14, 0.08, 1.0), 0.02, 0.34, texture_family=None),
        # --- Realistic Botanical Grass, Plants, Clovers, Flowers, Mushroom ---
        MaterialSpec("item_grass_blade", (0.38, 0.82, 0.18, 1.0), 0.00, 0.44, double_sided=True, texture_family="foliage", normal_scale=0.36),
        MaterialSpec("item_leaf_rich", (0.30, 0.72, 0.18, 1.0), 0.00, 0.42, double_sided=True, texture_family="foliage", normal_scale=0.40),
        MaterialSpec("item_stem_green", (0.26, 0.58, 0.16, 1.0), 0.00, 0.52, texture_family="foliage", normal_scale=0.34),
        MaterialSpec("item_petal_white", (0.97, 0.97, 0.94, 1.0), 0.00, 0.44, double_sided=True, texture_family="foliage", normal_scale=0.20),
        MaterialSpec("item_pollen_gold", (0.96, 0.72, 0.10, 1.0), 0.00, 0.70, texture_family="soil", normal_scale=0.44),
        MaterialSpec("item_groucho_nose", (0.82, 0.56, 0.44, 1.0), 0.00, 0.48, texture_family="chitin", normal_scale=0.14),
        MaterialSpec("item_mush_cap", (0.64, 0.56, 0.48, 1.0), 0.00, 0.62, texture_family="soil", normal_scale=0.38),
        MaterialSpec("item_mush_orange", (0.86, 0.44, 0.18, 1.0), 0.00, 0.60, texture_family="soil", normal_scale=0.36),
        MaterialSpec("item_mush_spot", (0.30, 0.88, 0.54, 1.0), 0.00, 0.48, texture_family="foliage", normal_scale=0.22),
        MaterialSpec("item_mush_stem", (0.66, 0.63, 0.58, 1.0), 0.00, 0.68, texture_family="wood", normal_scale=0.34),
        # --- Rocks, Moss & Anthill Soil ---
        MaterialSpec("item_stone_granite", (0.56, 0.56, 0.54, 1.0), 0.02, 0.80, texture_family="stone", normal_scale=0.72),
        MaterialSpec("item_stone_moss", (0.28, 0.76, 0.16, 1.0), 0.00, 0.84, texture_family="soil", normal_scale=0.58),
        MaterialSpec("item_soil_mound", (0.64, 0.54, 0.44, 1.0), 0.00, 0.90, texture_family="soil", normal_scale=0.72),
        MaterialSpec("item_soil_hole", (0.10, 0.07, 0.06, 1.0), 0.00, 0.96, texture_family="soil", normal_scale=0.40),
        MaterialSpec("item_twig_bark", (0.48, 0.30, 0.22, 1.0), 0.00, 0.82, texture_family="wood", normal_scale=0.56),
        MaterialSpec("item_party_green", (0.20, 0.82, 0.24, 1.0), 0.02, 0.42, texture_family="cloth", normal_scale=0.22),
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
