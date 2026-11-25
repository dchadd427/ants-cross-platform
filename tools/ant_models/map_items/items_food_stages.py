"""Realistic 3D sculptors for remaining eaten stages of Wave-1 1998 Ants Foods (13 items).

Covers:
- Peppermint Lollipop stages: `fdsuckr2` (#257), `fdsuckr3` (#258), `fdsuckr4` (#259)
- Corn Dog stages: `fdcornd2` (#362), `fdcornd3` (#363), `fdcornd4` (#364), `fdcornd5` (#365)
- Ruby Jell-O & Banana stages (animated): `fdjelo2` (#367), `fdjelo3` (#368)
- Fried Chicken Drumstick stages: `fdchick2` (#400), `fdchick3` (#401), `fdchick4` (#402), `fdchick5` (#403)
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
            AnimationChannel,
            MeshPart,
            SceneBuilder,
            curved_tube,
            cylinder,
            ellipsoid,
            lathe,
            recompute_smooth_normals,
            superellipsoid,
        )
        from .item_materials import gradient_y_tint, register_item_materials, tint_part
        from .items_food import _build_banana_coin, _split_mesh_by_mask
        from .items_toys import _finalize_item
    except ImportError:
        from map_items.item_materials import gradient_y_tint, register_item_materials, tint_part  # type: ignore
        from map_items.items_food import _build_banana_coin, _split_mesh_by_mask  # type: ignore
        from map_items.items_toys import _finalize_item  # type: ignore
        from mesh_core import (  # type: ignore
            AnimationChannel,
            MeshPart,
            SceneBuilder,
            curved_tube,
            cylinder,
            ellipsoid,
            lathe,
            recompute_smooth_normals,
            superellipsoid,
        )


def _peppermint_swirl(pos: np.ndarray, _nrm: np.ndarray) -> np.ndarray:
    dx = pos[:, 0]
    dz = pos[:, 2] - 0.18
    r = np.hypot(dx, dz)
    ang = np.arctan2(dz, dx)
    swirl = np.sin(8.0 * ang + r * 22.0)
    red_mask = np.clip(swirl * 10.0 + 0.5, 0.0, 1.0)[:, None]
    white = np.array([[0.99, 0.98, 0.97]], dtype=np.float64)
    red = np.array([[0.96, 0.06, 0.08]], dtype=np.float64)
    return white * (1.0 - red_mask) + red * red_mask


def _lollipop_stick(name: str, *, radius_scale: float = 1.0, curl_lift: float = 0.0, tip_red: float = 0.0) -> MeshPart:
    return tint_part(
        curved_tube(
            [
                (0.0, 0.054 + curl_lift * 0.5, +0.06),
                (0.012, 0.044, -0.14),
                (-0.018, 0.032, -0.32),
                (0.024, 0.028 + curl_lift, -0.50),
            ],
            [0.017 * radius_scale, 0.016 * radius_scale, 0.0155 * radius_scale, 0.015 * radius_scale],
            samples_per_seg=4,
            radial_segs=14,
            cap_rings=3,
            name=name,
            material="item_candy_white",
        ),
        lambda pos, _n: np.clip(
            np.column_stack([
                np.full(len(pos), 0.98),
                0.76 - tip_red * 0.58 * np.clip((pos[:, 2] + 0.14) / 0.22, 0.0, 1.0) + 0.14 * np.clip((-pos[:, 2]) / 0.50, 0.0, 1.0),
                0.78 - tip_red * 0.56 * np.clip((pos[:, 2] + 0.14) / 0.22, 0.0, 1.0) + 0.12 * np.clip((-pos[:, 2]) / 0.50, 0.0, 1.0),
            ]),
            0.12,
            1.0,
        ),
    )


def _clip_lollipop_bite(part: MeshPart, z_base: float, curve_amp: float, x_shift: float = 0.0, tilt_x: float = 0.0) -> MeshPart:
    """Clean concave half-space crescent bite (NEVER balloons vertices outward!)."""
    p = part.positions.astype(np.float64).copy()
    dx = p[:, 0] - x_shift
    z_lim = z_base + tilt_x * dx + curve_amp * (dx * dx) + 0.008 * np.cos(dx * 42.0)
    over = p[:, 2] > z_lim
    if np.any(over):
        p[over, 2] = z_lim[over]
    pf = p.astype(np.float32)
    return MeshPart(part.name, part.material, pf, recompute_smooth_normals(pf, part.indices), part.indices, uvs=part.uvs)


def build_fdsuckr2() -> SceneBuilder:
    """Peppermint lollipop with a crisp scalloped crescent bite out of top-left (`257:fdsuckr2`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdsuckr2")
    disc = superellipsoid(0.215, 0.066, 0.215, e1=0.68, e2=0.98, u_segs=64, v_segs=26, name="lollipop2_candy", material="item_candy_white").translate(0.0, 0.068, +0.18)
    disc = _clip_lollipop_bite(disc, z_base=0.255, curve_amp=2.6, x_shift=-0.04, tilt_x=0.35)
    sc.add("lollipop", [tint_part(disc, _peppermint_swirl), _lollipop_stick("lollipop2_stick")])
    sc.nodes["lollipop"] = [p.rotate_xyz(ry=-128.0, degrees=True) for p in sc.nodes["lollipop"]]
    return _finalize_item(sc, target_span=0.92)


def build_fdsuckr3() -> SceneBuilder:
    """Peppermint lollipop eaten down to a small bitten candy sliver on the stick (`258:fdsuckr3`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdsuckr3")
    disc = superellipsoid(0.145, 0.052, 0.145, e1=0.68, e2=0.98, u_segs=56, v_segs=22, name="lollipop3_candy", material="item_candy_white").translate(0.0, 0.058, +0.12)
    disc = _clip_lollipop_bite(disc, z_base=0.115, curve_amp=3.4, x_shift=0.0, tilt_x=0.18)
    sc.add("lollipop", [tint_part(disc, _peppermint_swirl), _lollipop_stick("lollipop3_stick", curl_lift=0.012, tip_red=0.4)])
    sc.nodes["lollipop"] = [p.rotate_xyz(ry=-128.0, degrees=True) for p in sc.nodes["lollipop"]]
    return _finalize_item(sc, target_span=0.88)


def build_fdsuckr4() -> SceneBuilder:
    """Bare crooked pink-stained paper lollipop stick after the candy is gone (`259:fdsuckr4`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdsuckr4")
    stick = _lollipop_stick("lollipop4_stick", radius_scale=1.35, curl_lift=0.028, tip_red=0.95)
    sc.add("lollipop", stick.rotate_xyz(ry=-126.0, degrees=True))
    return _finalize_item(sc, target_span=0.86)


def _build_corndog_stage(stage: int) -> SceneBuilder:
    """Progressive eaten corn dog stages with clean concave cross-section bites (`362..365:fdcornd2..5`)."""
    register_item_materials()
    sc = SceneBuilder(name=f"item_fdcornd{stage}")
    yc = 0.096

    if stage == 5:
        stick = curved_tube(
            [(0.0, 0.036, -0.44), (0.008, 0.042, -0.14), (-0.006, 0.048, +0.16), (0.0, 0.056, +0.44)],
            [0.028, 0.027, 0.026, 0.024],
            samples_per_seg=4,
            radial_segs=14,
            cap_rings=3,
            name="corndog5_stick",
            material="item_wood_stick",
        )
        sc.add("corndog", stick.rotate_xyz(ry=-135.0, degrees=True))
        return _finalize_item(sc, target_span=0.90)

    # Clean concave bite planes along +Z (NEVER balloons outward!) exposing golden crumb & juicy frankfurter core!
    cfg = {
        2: (0.285, 0.210, 3.6, +0.145, 0.94),
        3: (0.140, 0.065, 2.2, +0.015, 0.86),
        4: (-0.045, -0.105, 1.0, -0.145, 0.76),
    }[stage]
    z_hi, z_cut, m_waves, m_end, span = cfg

    def _slice_bite(part: MeshPart, *, z_offset: float = 0.0, jitter: float = 0.0) -> Tuple[MeshPart, np.ndarray]:
        p = part.positions.astype(np.float64).copy()
        if jitter > 0.0:
            r = np.maximum(1e-5, np.hypot(p[:, 0], p[:, 1] - yc))
            m = jitter * np.sin(p[:, 2] * 34.0 + p[:, 0] * 38.0) * np.cos(p[:, 1] * 36.0)
            p[:, 0] += m * (p[:, 0] / r)
            p[:, 1] += m * ((p[:, 1] - yc) / r)
        # Concave angled bite plane slicing diagonally toward the viewer (+X, +Y) across the top-left end!
        ang = np.arctan2(p[:, 1] - yc, p[:, 0])
        z_lim = z_cut + z_offset + 0.36 * p[:, 0] - 0.22 * (p[:, 1] - yc) + 0.016 * (p[:, 0] / 0.09) ** 2 + 0.005 * np.cos(ang * 5.0)
        over = p[:, 2] > z_lim
        if np.any(over):
            p[over, 2] = z_lim[over]
        bitten = p[:, 2] >= (z_lim - 0.007)
        pf = p.astype(np.float32)
        return MeshPart(part.name, part.material, pf, recompute_smooth_normals(pf, part.indices), part.indices, uvs=part.uvs), bitten

    z_mid = 0.5 * (-0.340 + z_hi)
    raw_bat = curved_tube(
        [(0.0, yc, -0.340), (0.0, yc, -0.245), (0.0, yc, z_mid), (0.0, yc, z_hi)],
        [(0.058, 0.055), (0.090, 0.088), (0.092, 0.090), (0.084, 0.082)],
        samples_per_seg=8,
        radial_segs=38,
        cap_rings=8,
        name=f"corndog{stage}_batter",
        material="item_corndog_crust",
    )
    bat, b_mask = _slice_bite(raw_bat, z_offset=0.0, jitter=0.0024)
    gradient_y_tint(bat, (0.78, 0.68, 0.54), (1.04, 0.98, 0.86), noise_amp=0.04)
    sc.add("corndog", _split_mesh_by_mask(bat, b_mask, "item_corndog_crumb", "_crumb"))

    # Encased frankfurter hot dog protrudes cleanly out of the bitten top-left batter cavity (`362..364`)!
    raw_dog = curved_tube(
        [(0.0, yc, -0.220), (0.0, yc, 0.5 * (-0.220 + z_hi)), (0.0, yc, z_cut + 0.068)],
        [(0.056, 0.055), (0.060, 0.059), (0.058, 0.057)],
        samples_per_seg=8,
        radial_segs=32,
        cap_rings=6,
        name=f"corndog{stage}_hotdog",
        material="item_hotdog_pink",
    )
    dog, d_mask = _slice_bite(raw_dog, z_offset=+0.052, jitter=0.0)
    sc.add("corndog", _split_mesh_by_mask(dog, d_mask, "item_hotdog_inside", "_meat"))

    stick = cylinder(0.015, 0.014, 0.34, radial_segs=14, bevel=0.003, axis="z", name=f"corndog{stage}_stick", material="item_wood_stick").translate(0.0, yc - 0.006, -0.495)
    sc.add("corndog", stick)

    m_pts: List[Tuple[float, float, float]] = []
    n_m = 36
    for i in range(n_m + 1):
        u = i / float(n_m)
        mz = -0.285 + (m_end - (-0.285)) * u
        mx = 0.046 * math.sin(u * math.pi * (m_waves * 2.0))
        r_s = 0.090 * math.sqrt(max(0.24, 1.0 - ((mx / 0.094) ** 2)))
        m_pts.append((mx, yc + r_s + 0.005, mz))
    sc.add("corndog", curved_tube(m_pts, [0.012] * len(m_pts), samples_per_seg=2, radial_segs=10, cap_rings=2, name=f"corndog{stage}_mustard", material="item_mustard"))
    sc.nodes["corndog"] = [p.rotate_xyz(ry=-135.0, degrees=True) for p in sc.nodes["corndog"]]
    return _finalize_item(sc, target_span=span)


def build_fdcornd2() -> SceneBuilder:
    return _build_corndog_stage(2)


def build_fdcornd3() -> SceneBuilder:
    return _build_corndog_stage(3)


def build_fdcornd4() -> SceneBuilder:
    return _build_corndog_stage(4)


def build_fdcornd5() -> SceneBuilder:
    return _build_corndog_stage(5)


def _build_jello_eaten(stage: int) -> SceneBuilder:
    """Half-eaten & scrap translucent ruby Jell-O & banana coin mounds with animated wobble (`367..368:fdjelo2..3`)."""
    register_item_materials()
    sc = SceneBuilder(name=f"item_fdjelo{stage}")

    if stage == 2:
        mold_h = 0.34
        prof = [(0.34, 0.0), (0.36, 0.032), (0.34, 0.10), (0.29, 0.19), (0.22, 0.27), (0.12, 0.33), (0.0, 0.34)]
        bananas = (
            (-0.06, 0.265, -0.01, 0.080, 16.0, +10.0, 35.0),
            (+0.10, 0.255, -0.02, 0.076, 20.0, -14.0, 140.0),
            (+0.13, 0.165, +0.16, 0.074, 34.0, -10.0, 220.0),
            (-0.07, 0.155, +0.18, 0.074, 34.0, +14.0, 80.0),
            (+0.13, 0.220, -0.15, 0.068, 28.0, -16.0, 290.0),
            (-0.17, 0.190, -0.08, 0.066, 36.0, +20.0, 175.0),
        )
        span = 0.84
    else:
        mold_h = 0.23
        prof = [(0.24, 0.0), (0.25, 0.026), (0.23, 0.08), (0.19, 0.15), (0.12, 0.21), (0.0, 0.23)]
        bananas = (
            (-0.03, 0.188, -0.04, 0.070, 16.0, +12.0, 50.0),
            (+0.10, 0.165, -0.03, 0.068, 26.0, -16.0, 165.0),
            (-0.01, 0.132, +0.11, 0.070, 34.0, +8.0, 270.0),
            (+0.12, 0.120, +0.09, 0.064, 38.0, -20.0, 105.0),
        )
        span = 0.72

    def _ruffle(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ang = np.arctan2(p[:, 2], p[:, 0])
        h = np.clip(p[:, 1] / mold_h, 0.0, 1.0)
        wave = 1.0 + (0.16 * np.cos(ang * 7.0 + 0.5) + 0.10 * np.sin(ang * 3.0 - 0.8)) * (1.0 - 0.35 * h)
        p[:, 0] *= wave
        p[:, 2] *= wave
        bite = np.exp(-(((ang - 0.45) / 0.55) ** 2)) * (0.22 if stage == 2 else 0.16)
        p[:, 0] *= 1.0 - bite
        p[:, 2] *= 1.0 - bite
        return p

    # Recessed lower inner garnet core so ALL creamy banana slices shine vividly right through the translucent ruby dome!
    core_prof = [(r * 0.66, y * 0.58 + 0.008) for r, y in prof]
    core = lathe(core_prof, radial_segs=52, cap_bottom=True, cap_top=True, axis="y", name=f"jelo{stage}_core", material="item_jello_core").deform(_ruffle)
    sc.add("jello_body", core)
    for idx, (bx, by, bz, rad, rx, rz, spin) in enumerate(bananas):
        coin = _build_banana_coin(f"jelo{stage}_ban_{idx}", radius=rad, thickness=0.028, spin_rad=math.radians(spin)).rotate_xyz(rx=rx, rz=rz, degrees=True).translate(bx, by, bz)
        sc.add("jello_body", coin)
    shell = lathe(prof, radial_segs=64, cap_bottom=False, cap_top=False, axis="y", name=f"jelo{stage}_dome", material="item_jello_ruby").deform(_ruffle)
    sc.add("jello_body", shell)

    times = [0.00, 0.25, 0.50, 0.75, 1.05]
    scales = [(1.0, 1.0, 1.0), (1.05, 0.91, 1.05), (0.97, 1.06, 0.97), (1.03, 0.95, 1.03), (1.0, 1.0, 1.0)]
    sc.add_animation("Idle", [AnimationChannel("jello_body", "scale", times, scales)])
    return _finalize_item(sc, target_span=span)


def build_fdjelo2() -> SceneBuilder:
    return _build_jello_eaten(2)


def build_fdjelo3() -> SceneBuilder:
    return _build_jello_eaten(3)


def _ivory_bone_tint(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
    return np.clip(
        np.column_stack([
            0.88 + 0.12 * nrm[:, 1],
            0.83 + 0.14 * nrm[:, 1],
            0.78 + 0.15 * nrm[:, 1],
        ]),
        0.62,
        1.0,
    )


def _sculpt_full_drumstick_bone(prefix: str, *, full_top_joint: bool) -> List[MeshPart]:
    """Anatomically realistic glossy ivory chicken tibia bone (matching condyles & shaft material!)."""
    z_hi = +0.345 if full_top_joint else +0.18
    shaft = tint_part(
        curved_tube(
            [(0.0, 0.078, -0.365), (0.0, 0.079, -0.260), (+0.008, 0.082, 0.0), (0.0, 0.084, z_hi * 0.72), (0.0, 0.086, z_hi)],
            [(0.026, 0.024), (0.019, 0.018), (0.021, 0.020), (0.028, 0.024), (0.044 if full_top_joint else 0.026, 0.036 if full_top_joint else 0.024)],
            samples_per_seg=5,
            radial_segs=18,
            cap_rings=4,
            name=f"{prefix}_shaft",
            material="item_candy_white",
        ),
        _ivory_bone_tint,
    )
    parts: List[MeshPart] = [shaft]
    for sgn, tag in ((-1.0, "l"), (+1.0, "r")):
        parts.append(tint_part(ellipsoid(0.021, 0.024, 0.025, u_segs=14, v_segs=10, name=f"{prefix}_ank_{tag}", material="item_candy_white").translate(sgn * 0.016, 0.078, -0.362), _ivory_bone_tint))
    if full_top_joint:
        parts.append(tint_part(ellipsoid(0.037, 0.035, 0.039, u_segs=16, v_segs=10, name=f"{prefix}_knee_a", material="item_candy_white").translate(-0.025, 0.092, +0.348), _ivory_bone_tint))
        parts.append(tint_part(ellipsoid(0.035, 0.031, 0.037, u_segs=16, v_segs=10, name=f"{prefix}_knee_b", material="item_candy_white").translate(+0.026, 0.086, +0.340), _ivory_bone_tint))
    return parts


def _build_chicken_stage(stage: int) -> SceneBuilder:
    """Progressive eaten fried chicken drumstick stages (`400..403:fdchick2..5`)."""
    register_item_materials()
    sc = SceneBuilder(name=f"item_fdchick{stage}")

    if stage == 5:
        parts = _sculpt_full_drumstick_bone("chick5_bone", full_top_joint=True)
        sc.add("chicken", [p.rotate_xyz(ry=136.0, degrees=True) for p in parts])
        return _finalize_item(sc, target_span=0.90)

    parts = _sculpt_full_drumstick_bone(f"chick{stage}_bone", full_top_joint=(stage >= 4))

    # Authentic 1998 silhouettes: slimmer crispy crust in stage 2, small pale-meat bite notch on upper ridge in stage 3,
    # ragged solid crispy crust wrapping mid-shaft with BOTH gleaming white bone ends exposed in stage 4!
    if stage == 2:
        pts = [(0.0, 0.082, -0.255), (0.004, 0.095, -0.14), (0.010, 0.112, 0.0), (0.014, 0.126, +0.15), (0.004, 0.116, +0.29), (-0.004, 0.100, +0.360)]
        rads = [(0.042, 0.040), (0.062, 0.056), (0.094, 0.084), (0.128, 0.110), (0.114, 0.098), (0.054, 0.048)]
        n_flakes = 68
    elif stage == 3:
        pts = [(0.0, 0.082, -0.250), (0.004, 0.092, -0.12), (0.008, 0.098, +0.02), (0.012, 0.114, +0.16), (0.004, 0.108, +0.29), (-0.002, 0.096, +0.345)]
        rads = [(0.040, 0.038), (0.058, 0.052), (0.072, 0.064), (0.110, 0.092), (0.102, 0.086), (0.048, 0.044)]
        n_flakes = 58
    else:
        pts = [(0.0, 0.082, -0.215), (0.006, 0.090, -0.07), (0.008, 0.098, +0.08), (0.004, 0.092, +0.220)]
        rads = [(0.038, 0.036), (0.076, 0.066), (0.082, 0.070), (0.044, 0.040)]
        n_flakes = 50

    meat = curved_tube(pts, rads, samples_per_seg=6, radial_segs=30, cap_rings=5, name=f"chick{stage}_meat", material="item_chicken_crust").deform(
        lambda p: p + 0.014 * np.sin(p * 42.0) * np.cos(np.roll(p, 1, axis=1) * 38.0)
    )
    parts.append(meat)

    if stage == 3:
        # Modest exposed pale-pink bitten meat patch on the upper-middle ridge matching `401:fdchick3`!
        bite_patch = tint_part(
            ellipsoid(0.034, 0.026, 0.064, u_segs=14, v_segs=10, name="chick3_bite_meat", material="item_bone_white")
            .rotate_xyz(ry=-0.22, rz=+0.38)
            .translate(+0.042, 0.132, +0.045),
            lambda pos, _n: np.tile(np.array([[0.96, 0.84, 0.80]]), (len(pos), 1)),
        )
        parts.append(bite_patch)

    z_k = np.array([pt[2] for pt in pts], dtype=np.float64)
    rx_k = np.array([r[0] for r in rads], dtype=np.float64)
    ry_k = np.array([r[1] for r in rads], dtype=np.float64)
    y_k = np.array([pt[1] for pt in pts], dtype=np.float64)
    for k in range(n_flakes):
        u = (k + 0.5) / n_flakes
        fz = z_k[0] + (z_k[-1] - z_k[0]) * u
        ang = k * 2.39996323
        fx = float(np.interp(fz, z_k, rx_k)) * 0.94 * math.cos(ang)
        fy = max(0.025, float(np.interp(fz, z_k, y_k)) + float(np.interp(fz, z_k, ry_k)) * 0.94 * math.sin(ang))
        fl = superellipsoid(0.034, 0.014, 0.040, e1=0.40, e2=0.50, u_segs=8, v_segs=6, name=f"chick{stage}_fl_{k}", material="item_chicken_crust").rotate_xyz(rx=k * 0.5, ry=k * 0.8, rz=ang).translate(fx, fy, fz)
        parts.append(fl)

    sc.add("chicken", [pt.rotate_xyz(ry=136.0, degrees=True) for pt in parts])
    return _finalize_item(sc, target_span=0.92 if stage <= 3 else 0.90)


def build_fdchick2() -> SceneBuilder:
    return _build_chicken_stage(2)


def build_fdchick3() -> SceneBuilder:
    return _build_chicken_stage(3)


def build_fdchick4() -> SceneBuilder:
    return _build_chicken_stage(4)


def build_fdchick5() -> SceneBuilder:
    return _build_chicken_stage(5)
