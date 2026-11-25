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
        from .items_toys import _finalize_item
    except ImportError:
        from map_items.item_materials import gradient_y_tint, register_item_materials, tint_part  # type: ignore
        from map_items.items_plants import _curved_grass_blade  # type: ignore
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
    """Realistic sculpted Green Team Anthill mound with tunnel hole, twigs, curling vines, green cone & bottle cap (`248:GREENHILL`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_greenhill")

    # Sculpted earthen dirt volcano mound with crisp sunken crater entrance hole in the crown!
    mound_prof = [
        (0.000, 0.110),  # dark deep tunnel floor inside hole
        (0.110, 0.115),  # tunnel shaft wall bottom
        (0.125, 0.265),  # tunnel shaft upper inner wall
        (0.165, 0.340),  # rounded earthen crater rim lip
        (0.255, 0.305),  # upper hill shoulder
        (0.380, 0.170),  # mid sloped dirt flank
        (0.495, 0.035),  # wide flaring base skirt
        (0.520, 0.000),  # grounded rim on Y = 0.0
        (0.000, 0.000),
    ]
    slope_stations = [(0.165, 0.340), (0.255, 0.305), (0.380, 0.170), (0.495, 0.035), (0.520, 0.000)]

    def _wobble_at(ang: float) -> float:
        return 1.0 + 0.075 * math.sin(ang * 3.0 + 0.4) + 0.045 * math.cos(ang * 5.0 - 0.8)

    def _mound_surface_y(x: float, z: float) -> float:
        ang = math.atan2(z, x)
        r_prof = math.hypot(x, z) / _wobble_at(ang)
        if r_prof <= slope_stations[0][0]:
            return slope_stations[0][1]
        for (r0, y0), (r1, y1) in zip(slope_stations[:-1], slope_stations[1:]):
            if r_prof <= r1:
                t = (r_prof - r0) / max(1e-5, r1 - r0)
                return y0 * (1.0 - t) + y1 * t
        return 0.0

    def _sculpt_dirt(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ang = np.arctan2(p[:, 2], p[:, 0])
        wobble = 1.0 + 0.075 * np.sin(ang * 3.0 + 0.4) + 0.045 * np.cos(ang * 5.0 - 0.8)
        p[:, 0] *= wobble
        p[:, 2] *= wobble
        clods = 0.014 * np.sin(p[:, 0] * 26.0 + p[:, 2] * 22.0) * np.cos(p[:, 1] * 28.0)
        skirt_w = np.clip(p[:, 1] / 0.06, 0.0, 1.0)
        p[:, 1] = np.maximum(0.0, p[:, 1] + clods * skirt_w)
        return p

    def _hill_dirt_tint(pos: np.ndarray, _nrm: np.ndarray) -> np.ndarray:
        r = np.hypot(pos[:, 0], pos[:, 2])
        hole_dark = np.clip((r - 0.105) / 0.080, 0.10, 1.0)[:, None]
        height_w = np.clip(pos[:, 1] / 0.34, 0.0, 1.0)[:, None]
        mottle = 0.07 * np.sin(pos[:, 0] * 22.0 + pos[:, 2] * 19.0)[:, None]
        loam = np.array([[0.74, 0.70, 0.66]], dtype=np.float64) * (1.0 - height_w) + np.array([[1.02, 0.96, 0.90]], dtype=np.float64) * height_w
        return np.clip((loam + mottle) * hole_dark, 0.06, 1.0)

    mound = tint_part(
        lathe(mound_prof, radial_segs=48, cap_bottom=False, cap_top=False, axis="y", name="hill_mound", material="item_soil_mound").deform(_sculpt_dirt),
        _hill_dirt_tint,
    )
    sc.add("anthill", mound)

    # Deep dark tunnel throat shadow plug inside the crater shaft.
    hole_plug = cylinder(0.118, 0.118, 0.030, radial_segs=24, axis="y", name="hill_hole_dark", material="item_soil_hole").translate(0.0, 0.145, 0.0)
    sc.add("anthill", hole_plug)

    # Scattered gritty dark-grey & tan pebbles seated cleanly ON TOP of the outer skirt (never poking underneath!).
    rng = np.random.default_rng(248)
    for k in range(20):
        ang = 2.0 * math.pi * k / 20.0 + float(rng.uniform(-0.14, 0.14))
        rad = float(rng.uniform(0.42, 0.56))
        px, pz = rad * math.cos(ang), rad * math.sin(ang)
        pr = float(rng.uniform(0.022, 0.044))
        surf_y = _mound_surface_y(px, pz)
        py = max(pr * 0.68, surf_y + pr * 0.28)
        mat = "item_stone_granite" if (k % 2 == 0) else "item_soil_mound"
        peb = (
            ellipsoid(pr * 1.18, pr * 0.66, pr * 0.94, u_segs=10, v_segs=6, name=f"hill_pebble_{k}", material=mat)
            .rotate_xyz(ry=ang)
            .translate(px, py, pz)
        )
        sc.add("anthill", peb)

    # Signature Green Team scavenged decor from `248:GREENHILL`, every piece seated cleanly ON the mound surface!
    # 1. Interwoven woody twigs hugging the front-lower dirt embankment (`+Z` rim) just like `248:GREENHILL`!
    twig_arcs = (
        ((-0.34, +0.26), (-0.12, +0.42), (+0.16, +0.40)),
        ((-0.24, +0.36), (+0.02, +0.45), (+0.26, +0.34)),
        ((-0.36, +0.14), (-0.22, +0.34), (-0.02, +0.44)),
        ((+0.06, +0.43), (+0.24, +0.36), (+0.34, +0.22)),
    )
    for idx, (a, b, c) in enumerate(twig_arcs):
        pts = [(x, max(0.025, _mound_surface_y(x, z) + 0.020), z) for x, z in (a, b, c)]
        twig = curved_tube(pts, [0.018, 0.021, 0.015], samples_per_seg=4, radial_segs=10, cap_rings=2, name=f"hill_twig_{idx}", material="item_twig_bark")
        sc.add("anthill", twig)

    # 2. Lush bright-green curling grass vines sprouting right OFF the front dirt slope & twigs (`248:GREENHILL`)!
    for k in range(20):
        ang = 0.25 + (k / 19.0) * 2.60  # bold front/left/right wrap on +Z half
        rad = 0.25 + 0.18 * ((k * 7) % 5) / 4.0
        bx, bz = rad * math.cos(ang), rad * math.sin(ang)
        by = max(0.01, _mound_surface_y(bx, bz) - 0.008)
        sc.add(
            "anthill",
            _curved_grass_blade(
                f"hill_vine_{k}",
                base_xz=(bx, bz),
                base_y=by,
                height=0.18 + 0.11 * (k % 3),
                width=0.044,
                yaw=ang + 0.42 * math.sin(k * 2.1),
                lean=0.14,
                droop=0.13,
                twist=0.45 * math.cos(k),
                segs=7,
            ),
        )

    # 3. Bold bright-green white-rimmed party cone pointing right from the crater lip (`248:GREENHILL`)!
    cone_x, cone_z = +0.24, -0.04
    cone_y = _mound_surface_y(cone_x, cone_z) + 0.048
    party_cone = (
        cone(0.088, 0.30, radial_segs=22, name="hill_green_cone", material="item_party_green")
        .rotate_xyz(rx=0.16, rz=-1.30)
        .translate(cone_x, cone_y, cone_z)
    )
    cone_rim = (
        torus_arc(0.088, 0.016, u_segs=20, v_segs=8, plane="xz", name="hill_cone_rim", material="item_cotton_fluff")
        .rotate_xyz(rx=0.16, rz=-1.30)
        .translate(cone_x - 0.125, cone_y - 0.036, cone_z + 0.015)
    )
    sc.add("anthill", [party_cone, cone_rim])

    # 4. Prominent scavenged crimped silver-and-teal soda bottle cap resting visibly on the right slope (`248:GREENHILL`)!
    cap_x, cap_z = +0.36, +0.16
    cap_y = _mound_surface_y(cap_x, cap_z) + 0.038

    def _crimp_skirt(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ang = np.arctan2(p[:, 2], p[:, 0])
        bot = np.clip(-p[:, 1] / 0.025, 0.0, 1.0)
        flute = 1.0 + 0.11 * np.cos(ang * 21.0) * bot
        p[:, 0] *= flute
        p[:, 2] *= flute
        return p

    cap_metal = (
        cylinder(0.105, 0.118, 0.042, radial_segs=42, bevel=0.006, axis="y", name="hill_cap_metal", material="item_aluminum")
        .deform(_crimp_skirt)
        .rotate_xyz(rx=0.38, rz=-0.46)
        .translate(cap_x, cap_y, cap_z)
    )
    cap_top = (
        cylinder(0.088, 0.088, 0.016, radial_segs=28, bevel=0.004, axis="y", name="hill_cap_green", material="item_bottle_teal")
        .rotate_xyz(rx=0.38, rz=-0.46)
        .translate(cap_x + 0.009, cap_y + 0.018, cap_z + 0.007)
    )
    sc.add("anthill", [cap_metal, cap_top])
    return _finalize_item(sc, target_span=1.00)
