"""Realistic 3D sculptors for 1998 Ants rocks and team anthills (Wave 1).

Covers:
- `bigrock2`  (290): Weathered faceted granite boulder with organic green moss & lichen patches
- `GREENHILL` (248): Realistic sculpted earthen Green Team Anthill mound with tunnel entrance,
                     scattered soil pebbles, tangled vines, green party cone & bottle cap
"""
from __future__ import annotations

import math
from typing import List, Tuple

try:
    import numpy as np
except ImportError:  # pragma: no cover - pure-stdlib import safety for CI
    np = None  # type: ignore

if np is not None:
    try:
        from ..mesh_core import (
            MeshPart,
            SceneBuilder,
            cone,
            curved_tube,
            cylinder,
            ellipsoid,
            lathe,
            sphere,
            superellipsoid,
            torus_arc,
        )
        from .item_materials import gradient_y_tint, register_item_materials, tint_part
        from .items_plants import _curved_grass_blade
        from .items_structures import _add_hatch_bottle_cap, _add_waving_flag, _build_anthill_base, _crimped_bottle_cap
        from .items_toys import _finalize_item
    except ImportError:
        from map_items.item_materials import gradient_y_tint, register_item_materials, tint_part  # type: ignore
        from map_items.items_plants import _curved_grass_blade  # type: ignore
        from map_items.items_structures import _add_hatch_bottle_cap, _add_waving_flag, _build_anthill_base, _crimped_bottle_cap  # type: ignore
        from map_items.items_toys import _finalize_item  # type: ignore
        from mesh_core import (  # type: ignore
            MeshPart,
            SceneBuilder,
            cone,
            curved_tube,
            cylinder,
            ellipsoid,
            lathe,
            sphere,
            superellipsoid,
            torus_arc,
        )


def build_bigrock2() -> SceneBuilder:
    """Weathered fractured granite boulder with vivid green velvet moss & rust-orange lichen (`290:bigrock2`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_bigrock2")

    # Crisp angular granite fracture planes + natural rough stone crags (authentic boulder geometry!).
    planes = (
        (np.array([+0.68, +0.62, +0.38]), 0.34),
        (np.array([-0.72, +0.58, -0.36]), 0.35),
        (np.array([+0.25, +0.76, -0.60]), 0.32),
        (np.array([-0.38, +0.52, +0.76]), 0.34),
        (np.array([+0.82, +0.22, -0.52]), 0.36),
        (np.array([-0.80, +0.32, +0.50]), 0.36),
    )

    def _fracture_boulder(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        # Shear flat rock cleavage planes cleanly like real weathered granite
        for pn, d_lim in planes:
            pn_u = pn / np.linalg.norm(pn)
            proj = p @ pn_u
            over = np.maximum(0.0, proj - d_lim)
            p -= (over * 0.72)[:, None] * pn_u[None, :]
        r = np.maximum(1e-5, np.linalg.norm(p, axis=1, keepdims=True))
        n = p / r
        crag = (
            0.038 * np.sin(p[:, 0] * 9.5 + p[:, 2] * 7.2) * np.cos(p[:, 1] * 10.0)
            + 0.024 * np.abs(np.sin(p[:, 0] * 16.0 - p[:, 2] * 14.0))
            - 0.016 * np.cos(p[:, 1] * 22.0 + p[:, 0] * 18.0)
        )
        p = p + crag[:, None] * n
        p[:, 1] = np.where(p[:, 1] < -0.17, -0.17 + 0.08 * (p[:, 1] + 0.17), p[:, 1])
        return p

    def _mossy_granite_tint(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
        up = np.clip(nrm[:, 1], 0.0, 1.0)
        # Bold vivid emerald moss wrapping lower-right & left crevices + warm rust-orange center lichen (`290:bigrock2`)!
        moss_field = (
            np.sin(pos[:, 0] * 8.5 - 0.5) * np.cos(pos[:, 2] * 8.0 - 0.6)
            + 0.50 * np.sin(pos[:, 0] * 19.0 + pos[:, 2] * 15.0)
            + 0.45 * np.clip(pos[:, 0] + pos[:, 2] * 0.7, -0.3, 0.5)
        )
        moss_w = np.clip((moss_field * 0.85 + up * 0.50 - 0.42) * 3.8, 0.0, 1.0)[:, None]
        lichen = np.exp(-(((pos[:, 0] - 0.04) / 0.14) ** 2 + ((pos[:, 2] - 0.02) / 0.13) ** 2))[:, None] * (up[:, None] > 0.35)
        granite = np.array([[0.76, 0.78, 0.76]], dtype=np.float64) * (0.80 + 0.20 * np.sin(pos[:, 0] * 32.0 + pos[:, 2] * 28.0)[:, None])
        moss = np.array([[0.18, 0.96, 0.12]], dtype=np.float64)
        ochre = np.array([[0.98, 0.48, 0.16]], dtype=np.float64)
        col = granite * (1.0 - moss_w) + moss * moss_w
        return np.clip(col * (1.0 - 0.78 * lichen) + ochre * (0.78 * lichen), 0.12, 1.0)

    boulder = tint_part(
        superellipsoid(0.45, 0.31, 0.41, e1=0.72, e2=0.78, u_segs=42, v_segs=28, name="boulder_core", material="item_stone_granite")
        .deform(_fracture_boulder)
        .translate(0.0, 0.21, 0.0),
        _mossy_granite_tint,
    )
    sc.add("rock", boulder)

    # Raised lush velvet moss tufts clinging to the fractured lower-right & crown crevices!
    for idx, (mx, my, mz, rx, ry, rz) in enumerate((
        (+0.18, 0.26, +0.20, 0.13, 0.055, 0.11),
        (+0.22, 0.31, -0.08, 0.11, 0.048, 0.10),
        (-0.22, 0.19, +0.22, 0.10, 0.044, 0.09),
        (+0.02, 0.42, -0.10, 0.09, 0.036, 0.08),
    )):
        moss_pad = (
            ellipsoid(rx, ry, rz, u_segs=16, v_segs=10, name=f"rock_moss_{idx}", material="item_stone_moss")
            .deform(lambda p, k=idx: p + 0.010 * np.sin(p * 42.0 + k))
            .rotate_xyz(rx=0.25 if mz > 0 else -0.20, rz=-0.32 if mx > 0 else 0.30)
            .translate(mx, my, mz)
        )
        sc.add("rock", moss_pad)
    return _finalize_item(sc, target_span=0.95)


def build_greenhill() -> SceneBuilder:
    """Green Team Anthill with eroded earthen crater, braided twig/vine palisade, waving green flag & teal cap (`248:GREENHILL`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_greenhill")
    surf_y = _build_anthill_base(
        sc,
        soil_mat="item_soil_mound",
        seed=248,
        loam_lo=(0.72, 0.66, 0.62),
        loam_hi=(1.04, 0.96, 0.90),
        pebble_count=34,
    )

    # 1. Crooked pale cream-white birch twig flagpole curving along the right crater lip (`248:GREENHILL`)!
    px0, pz0 = +0.16, +0.06
    py0 = surf_y(px0, pz0)
    birch_pole = curved_tube(
        [
            (px0 - 0.02, py0 - 0.04, pz0 + 0.04),
            (px0 + 0.01, py0 + 0.09, pz0 - 0.01),
            (px0 + 0.03, py0 + 0.21, pz0 - 0.06),
            (px0 + 0.01, py0 + 0.30, pz0 - 0.11),
        ],
        [0.024, 0.022, 0.020, 0.014],
        samples_per_seg=4,
        radial_segs=12,
        cap_rings=2,
        name="gh_birch_pole",
        material="item_birch_pole",
    )
    sc.add("anthill", birch_pole)

    # Iconic bright-green triangular cloth pennant snapping & waving to the right (exact 700 ms wave of `248:GREENHILL`!)
    _add_waving_flag(
        sc,
        prefix="gh",
        anchor=(px0 + 0.024, py0 + 0.220, pz0 - 0.070),
        yaw_deg=-8.0,
        pitch_deg=14.0,
        roll_deg=-12.0,
        length=0.42,
        base_height=0.195,
        mat="item_flag_green",
        period_sec=0.70,
        wave_amp=0.30,
        droop=-0.018,
    )

    # 2. Rich braided wooden twig & vine fortress palisade cradling the entire front lower dirt skirt (`248:GREENHILL`)!
    twig_arcs = (
        ((-0.40, +0.20), (-0.18, +0.42), (+0.12, +0.44), (+0.34, +0.28)),
        ((-0.34, +0.30), (-0.06, +0.47), (+0.22, +0.38), (+0.38, +0.18)),
        ((-0.42, +0.10), (-0.26, +0.34), (-0.02, +0.45), (+0.18, +0.40)),
        ((-0.26, +0.36), (+0.04, +0.42), (+0.28, +0.32), (+0.36, +0.22)),
        ((-0.38, +0.24), (-0.14, +0.38), (+0.08, +0.38), (+0.26, +0.30)),
        ((-0.22, +0.42), (-0.04, +0.48), (+0.16, +0.44), (+0.30, +0.34)),
    )
    for idx, arc in enumerate(twig_arcs):
        y_off = 0.020 + 0.014 * (idx % 3)
        pts = [(x, max(0.024, surf_y(x, z) + y_off), z) for x, z in arc]
        rad = 0.018 + 0.004 * (idx % 2)
        twig = curved_tube(
            pts,
            [rad * 0.9, rad * 1.15, rad * 1.05, rad * 0.78],
            samples_per_seg=4,
            radial_segs=10,
            cap_rings=2,
            name=f"hill_twig_{idx}",
            material="item_twig_bark",
        )
        sc.add("anthill", twig)

    # 3. Dense vibrant curling green vine tendrils & meadow grass blades hugging the twig weave (keeping the burrow crater open!)
    for k in range(28):
        ang = 0.16 + (k / 27.0) * 2.82
        rad = 0.30 + 0.18 * ((k * 7) % 6) / 5.0
        bx, bz = rad * math.cos(ang), rad * math.sin(ang)
        if math.hypot(bx - 0.40, bz - 0.14) < 0.18:
            continue
        by = max(0.012, surf_y(bx, bz) - 0.006)
        sc.add(
            "anthill",
            _curved_grass_blade(
                f"hill_vine_{k}",
                base_xz=(bx, bz),
                base_y=by,
                height=0.14 + 0.10 * (k % 4) / 3.0,
                width=0.044 + 0.010 * (k % 2),
                yaw=ang + 0.45 * math.sin(k * 2.1),
                lean=0.10 + 0.04 * (k % 3),
                droop=0.11 + 0.04 * (k % 2),
                twist=0.52 * math.cos(k),
                segs=7,
            ),
        )
    # Iconic lone forked bright-green grass sprig sprouting in the back-right soil (`248:GREENHILL`)!
    for idx, (yo, lean) in enumerate(((0.5, 0.08), (1.1, 0.12), (-0.4, 0.10))):
        bx, bz = +0.34, -0.32
        sc.add(
            "anthill",
            _curved_grass_blade(
                f"hill_back_sprig_{idx}",
                base_xz=(bx, bz),
                base_y=max(0.0, surf_y(bx, bz) - 0.005),
                height=0.22 + 0.04 * idx,
                width=0.040,
                yaw=yo,
                lean=lean,
                droop=0.08,
                twist=0.35,
                segs=6,
            ),
        )

    # 4. Hinged metallic silver & deep emerald-teal soda bottle cap secret hatch on the far-right slope (`248:GREENHILL`)!
    cap_x, cap_z = +0.40, +0.14
    cap_y = surf_y(cap_x, cap_z) + 0.044
    cap_parts = _crimped_bottle_cap("gh_teal_cap", "item_bottle_teal", radius=0.114)
    cap_ring = torus_arc(0.072, 0.012, u_segs=26, v_segs=8, plane="xz", name="gh_cap_inner_rim", material="item_aluminum").translate(0.0, 0.024, 0.0)
    _add_hatch_bottle_cap(
        sc,
        prefix="gh",
        cap_parts=cap_parts + [cap_ring],
        pos=(cap_x, cap_y, cap_z),
        rot_rad=(0.44, -0.12, -0.46),
        radius=0.114,
        soil_mat="item_soil_mound",
    )
    return _finalize_item(sc, target_span=1.00)
