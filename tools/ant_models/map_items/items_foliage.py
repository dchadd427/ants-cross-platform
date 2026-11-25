"""Realistic 3D sculptors for 1998 Ants fallen leaves, twigs, ivy, herb shrubs, and exotic flowers (Wave 2).

Covers:
- `leaf1`..`leaf4` (323–326): Sculpted 3-lobed maple/currant leaves & deeply-lobed dark oak leaves
- `stick1`, `stick2a` (327–328): Sinuous woody twig & leafy branch twig
- `plant1` (409): Lush 5-leaf crinkled mint/herb rosette
- `ivyrt`, `ivylft`, `ivyfront` (417–419): Three-leaf bright backyard ivy clusters
- `flower1b` (411): Oxeye Daisy side-profile bloom
- `flower2a`, `flower2b` (412–413): Hot-pink / rose 8-petal Cosmos starflowers with golden stamens
- `flower3a`, `flower3b` (414–415): Spotted lilac & deep-violet 5-petal tiger lilies
- `flower3c` (416): Iconic googly-eyed tongue-sticking-out spotted lavender flower
- `flower1` (421): Wide resting Groucho Disguise Daisy
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
            curved_tube,
            ellipsoid,
            ensure_outward_winding,
            recompute_smooth_normals,
            rounded_box,
            sphere,
            superellipsoid,
            torus_arc,
        )
        from .item_materials import gradient_y_tint, register_item_materials, tint_part
        from .items_plants import (
            _build_daisy_base,
            _build_daisy_stem_leaf,
            _build_groucho_daisy,
            _build_swept_leaf_sheet,
        )
        from .items_toys import _finalize_item
    except ImportError:
        from map_items.item_materials import gradient_y_tint, register_item_materials, tint_part  # type: ignore
        from map_items.items_plants import (  # type: ignore
            _build_daisy_base,
            _build_daisy_stem_leaf,
            _build_groucho_daisy,
            _build_swept_leaf_sheet,
        )
        from map_items.items_toys import _finalize_item  # type: ignore
        from mesh_core import (  # type: ignore
            MeshPart,
            SceneBuilder,
            curved_tube,
            ellipsoid,
            ensure_outward_winding,
            recompute_smooth_normals,
            rounded_box,
            sphere,
            superellipsoid,
            torus_arc,
        )


def _sculpt_pointed_blade(
    name: str,
    *,
    length: float,
    width: float,
    material: str = "item_leaf_bright",
    serration: float = 0.0,
    arch: float = 0.030,
    vein_glow: float = 0.42,
    curve_x: float = 0.0,
    asym: float = 0.0,
    n_u: int = 26,
) -> MeshPart:
    """Build one smooth C-infinity closed 3D sculpted leaf blade extending from (0,0,0) along +Z with crisp venation."""
    hw = width * 0.5

    def _profile(u: float, mult: float) -> float:
        if u <= 0.0 or u >= 1.0:
            return 0.0008
        # Plump rounded ovate belly peaking at u=0.29 tapering to a crisp pointed botanical tip!
        env = (u ** 0.45) * ((1.0 - u) ** 1.08) * 2.55
        if serration > 0.0:
            k = u * math.pi * 11.0
            tooth = 0.62 * math.sin(k) - 0.25 * math.sin(2.0 * k)
            env *= 1.0 + serration * tooth * (math.sin(u * math.pi) ** 0.7)
        return hw * mult * max(0.015, env)

    def _color(_pos: np.ndarray, nrm: np.ndarray, u: np.ndarray, s: np.ndarray, top: np.ndarray) -> np.ndarray:
        mid = np.exp(-((s / 0.09) ** 2)) * (1.0 - 0.35 * u)
        lat = np.maximum(0.0, np.cos((u * 6.0 - np.abs(s) * 1.45) * math.pi)) ** 6 * (1.0 - 0.35 * u)
        vein = np.clip(mid + 0.55 * lat, 0.0, 1.0)[:, None]
        base_ao = (0.48 + 0.54 * np.clip(u / 0.28, 0.0, 1.0) + 0.10 * nrm[:, 1])[:, None]
        side_lit = np.where(top >= -0.05, 1.0, 0.66)[:, None]
        base = np.array([[0.38, 0.84, 0.16]], dtype=np.float64) * base_ao * side_lit
        glow = np.array([[0.92, 1.00, 0.40]], dtype=np.float64) * side_lit
        return base * (1.0 - vein_glow * vein) + glow * (vein_glow * vein)

    return _build_swept_leaf_sheet(
        name,
        length=length,
        w_left_fn=lambda u: _profile(u, 1.0 - asym),
        w_right_fn=lambda u: _profile(u, 1.0 + asym),
        spine_x_fn=(lambda u: curve_x * (u ** 1.55)) if abs(curve_x) > 1e-5 else None,
        spine_y_fn=lambda u: arch * math.sin(u * math.pi * 0.92) - 0.38 * arch * (u ** 2.2),
        thickness=max(0.0045, min(0.0082, width * 0.062)),
        v_cup=max(0.006, min(0.017, width * 0.10)),
        ripple_amp=0.0032 if serration > 0.0 else 0.0018,
        ripple_freq=6.0,
        n_u=n_u,
        n_half=7,
        material=material,
        color_fn=_color,
    )


def _palmate_contour(phi: float, *, spread: float = 1.0) -> float:
    """Continuous 3-lobed broadly-fused deeply-serrated maple/grapevine contour R(phi) around palm pivot."""
    side_ang = 1.04 * (0.94 + 0.06 * spread)
    sinus_ang = 0.63 * (0.95 + 0.05 * spread)

    # Broad lush fused central lobe + twin full rounded-shoulder flanking lobes (matches `leaf1`/`leaf2` fullness!)
    lobe_c = 0.405 * math.exp(-((abs(phi) / (0.48 * spread)) ** 2.55))
    lobe_l = 0.335 * math.exp(-((abs(phi + side_ang) / (0.44 * spread)) ** 2.35))
    lobe_r = 0.335 * math.exp(-((abs(phi - side_ang) / (0.44 * spread)) ** 2.35))
    # Full cordate lower hips wrapping roundly down to the stem junction
    basal = 0.220 * (1.0 - 0.62 * math.exp(-(((math.pi - abs(phi)) / 0.44) ** 2.0)))

    # Crisp V-sinuses cutting ~42% inward so all 3 lobes stay fused in one broad leaf sheet!
    sinus_l = 0.36 * math.exp(-(((phi + sinus_ang) / 0.105) ** 2.0))
    sinus_r = 0.35 * math.exp(-(((phi - sinus_ang) / 0.105) ** 2.0))
    base_r = (basal + lobe_c + lobe_l + lobe_r) * (1.0 - sinus_l - sinus_r)

    # Bold dramatic forward-leaning maple/grapevine sawtooth serrations along all 3 lobes (clean pointed apexes)!
    tooth_mask = (
        (1.0 - 0.90 * math.exp(-(((phi + sinus_ang) / 0.12) ** 2.0)))
        * (1.0 - 0.90 * math.exp(-(((phi - sinus_ang) / 0.12) ** 2.0)))
        * (1.0 - 0.85 * math.exp(-((phi / 0.095) ** 2.0)))
        * (1.0 - 0.85 * math.exp(-(((phi + side_ang) / 0.095) ** 2.0)))
        * (1.0 - 0.85 * math.exp(-(((phi - side_ang) / 0.095) ** 2.0)))
        * (1.0 - math.exp(-(((math.pi - abs(phi)) / 0.68) ** 2.6)))
    )
    apex_boost = (
        0.072 * math.exp(-((abs(phi) / 0.078) ** 1.6))
        + 0.062 * math.exp(-((abs(phi + side_ang) / 0.078) ** 1.6))
        + 0.064 * math.exp(-((abs(phi - side_ang) / 0.078) ** 1.6))
    )
    k_c = abs(phi) * 19.5 + 0.55
    tooth_wave = 0.58 * math.sin(k_c) - 0.28 * math.sin(2.0 * k_c) + 0.12 * math.sin(3.0 * k_c)
    return max(0.065, base_r * (1.0 + 0.140 * tooth_mask * tooth_wave) + apex_boost)


def _build_palmate_leaf_mesh(name: str, *, spread: float = 1.0, edge_curl: float = 0.036) -> MeshPart:
    """Build a single continuous closed 3D deeply-serrated 3-lobed maple/grapevine leaf sheet (`leaf1`, `leaf2`)."""
    n_ang = 156
    n_rad = 12
    phis = np.linspace(-math.pi, math.pi, n_ang, endpoint=False)
    r_max_arr = np.array([_palmate_contour(float(p), spread=spread) for p in phis], dtype=np.float64)
    side_ang = 1.04 * (0.94 + 0.06 * spread)

    def _surface_point(r_u: float, phi: float, r_max: float, is_top: bool) -> Tuple[float, float, float]:
        phi_w = phi - 0.075 * (r_u ** 1.7) * math.sin(phi * 1.4) + 0.04 * (r_u ** 1.9) * math.cos(phi)
        rad = r_u * r_max
        lx = rad * math.sin(phi_w)
        lz = -rad * math.cos(phi_w) - 0.085
        cup = 0.042 * (r_u ** 1.55) * (0.76 + 0.24 * math.cos(phi * 2.0))
        rim_t = max(0.0, (r_u - 0.72) / 0.28)
        curl_w = 0.65 + 0.55 * max(0.0, math.sin(phi - 0.2)) + 0.35 * max(0.0, math.sin(-phi - 0.3))
        d_vein = min(abs(phi), abs(phi + side_ang), abs(phi - side_ang))
        vein_trough = -0.0072 * math.exp(-((d_vein / 0.085) ** 2.0)) * math.sin(r_u * math.pi)
        herring = 0.0046 * math.sin((r_u * 6.0 - d_vein * 3.5) * math.pi) * math.sin(r_u * math.pi)
        y_mid = 0.022 + cup + edge_curl * (rim_t ** 2.1) * curl_w + vein_trough + herring
        half_t = 0.0080 * (1.0 - 0.68 * (r_u ** 1.8)) * (0.85 + 0.35 * math.exp(-((d_vein / 0.09) ** 2.0)))
        return (lx, y_mid + (half_t if is_top else -half_t * 1.15), lz)

    verts: List[Tuple[float, float, float]] = []
    uvs_list: List[Tuple[float, float]] = []
    meta_r: List[float] = []
    meta_phi: List[float] = []
    meta_top: List[float] = []
    r_steps = np.linspace(1.0 / n_rad, 1.0, n_rad)

    for is_top, t_sign in ((True, 1.0), (False, -1.0)):
        verts.append(_surface_point(0.0, 0.0, 0.1, is_top))
        uvs_list.append((0.5, 0.5))
        meta_r.append(0.0)
        meta_phi.append(0.0)
        meta_top.append(t_sign)
        for r_u_val in r_steps:
            r_u = float(r_u_val)
            for j in range(n_ang):
                phi = float(phis[j])
                pt = _surface_point(r_u, phi, float(r_max_arr[j]), is_top)
                verts.append(pt)
                uvs_list.append((0.5 + pt[0] * 1.15, 0.5 - pt[2] * 1.15))
                meta_r.append(r_u)
                meta_phi.append(phi)
                meta_top.append(t_sign)

    bot_c = 1 + n_rad * n_ang
    bot_r0 = bot_c + 1
    tris: List[Tuple[int, int, int]] = []
    for j in range(n_ang):
        j1 = (j + 1) % n_ang
        tris.append((0, 1 + j1, 1 + j))
        tris.append((bot_c, bot_r0 + j, bot_r0 + j1))
    for i in range(n_rad - 1):
        tr0, tr1 = 1 + i * n_ang, 1 + (i + 1) * n_ang
        br0, br1 = bot_r0 + i * n_ang, bot_r0 + (i + 1) * n_ang
        for j in range(n_ang):
            j1 = (j + 1) % n_ang
            tris.extend(((tr0 + j, tr0 + j1, tr1 + j1), (tr0 + j, tr1 + j1, tr1 + j)))
            tris.extend(((br0 + j, br1 + j1, br0 + j1), (br0 + j, br1 + j, br1 + j1)))
    t_out, b_out = 1 + (n_rad - 1) * n_ang, bot_r0 + (n_rad - 1) * n_ang
    for j in range(n_ang):
        j1 = (j + 1) % n_ang
        tris.extend(((t_out + j, t_out + j1, b_out + j1), (t_out + j, b_out + j1, b_out + j)))

    pos = np.asarray(verts, dtype=np.float32)
    idx = ensure_outward_winding(pos, np.asarray(tris, dtype=np.uint32).reshape(-1))
    nrm = recompute_smooth_normals(pos, idx)
    part = MeshPart(name, "item_leaf_bright", pos, nrm, idx, uvs=np.asarray(uvs_list, dtype=np.float32))

    r_arr = np.asarray(meta_r, dtype=np.float64)
    p_arr = np.asarray(meta_phi, dtype=np.float64)
    t_arr = np.asarray(meta_top, dtype=np.float64)
    d_min = np.minimum(np.abs(p_arr), np.minimum(np.abs(p_arr + side_ang), np.abs(p_arr - side_ang)))
    main_vein = np.exp(-((d_min / (0.055 + 0.08 * np.exp(-r_arr * 2.6))) ** 2.0)) * (1.0 - 0.30 * (r_arr ** 2.0)) * (np.abs(p_arr) < 1.65)
    lat_vein = (np.maximum(0.0, np.cos((r_arr * 6.0 - d_min * 3.5) * math.pi)) ** 6) * (1.0 - 0.30 * r_arr) * (r_arr > 0.14)
    vein = np.clip(main_vein + 0.48 * lat_vein, 0.0, 1.0)[:, None]
    # Dramatic 1998 dark emerald rolled outer rims & deep sinus shadows vs luminous yellow-lime inner blade!
    rim_dark = np.clip(1.0 - 0.56 * np.clip((r_arr - 0.68) / 0.32, 0.0, 1.0) ** 1.35, 0.36, 1.0)[:, None]
    side_lit = np.where(t_arr > 0.0, 1.0, 0.58)[:, None]
    sun = (0.64 + 0.28 * np.clip(nrm[:, 1], -0.4, 1.0) + 0.12 * (1.0 - r_arr))[:, None]
    lamina = np.array([[0.34, 0.80, 0.14]], dtype=np.float64) * sun * rim_dark * side_lit
    vein_col = np.array([[0.94, 1.00, 0.40]], dtype=np.float64) * side_lit
    cols = np.clip(lamina * (1.0 - 0.64 * vein) + vein_col * (0.64 * vein), 0.12, 1.0).astype(np.float32)
    part.colors = cols
    part._base_tint = cols.copy()
    return part


def _build_palmate_maple(
    name: str,
    *,
    yaw_deg: float,
    tilt_deg: float,
    spread: float = 1.0,
    stem_len: float = 0.27,
    stem_bend_x: float = 0.0,
    target_span: float = 0.94,
) -> SceneBuilder:
    """Build authentic single-sheet 3-lobed deeply-serrated maple/grapevine leaf (`323:leaf1` & `324:leaf2`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = [_build_palmate_leaf_mesh(f"{name}_blade", spread=spread, edge_curl=0.036 if spread < 1.02 else 0.042)]

    # Sturdy tapering curved petiole stem + plump bulbous abscission foot matching `leaf1`/`leaf2`!
    stem_pts = [
        (stem_bend_x, 0.015, stem_len),
        (stem_bend_x * 0.52, 0.018, stem_len * 0.64),
        (stem_bend_x * 0.14, 0.022, stem_len * 0.28),
        (0.0, 0.024, -0.025),
    ]
    stem = tint_part(
        curved_tube(stem_pts, [0.018, 0.015, 0.016, 0.018], samples_per_seg=4, radial_segs=12, cap_rings=2, name=f"{name}_stem", material="item_stem_green"),
        lambda _p, nrm: np.clip(np.column_stack([0.46 + 0.24 * nrm[:, 1], 0.84 + 0.14 * nrm[:, 1], 0.24 + 0.14 * nrm[:, 1]]), 0.22, 1.0),
    )
    foot = tint_part(
        ellipsoid(0.025, 0.018, 0.026, u_segs=14, v_segs=10, name=f"{name}_foot", material="item_leaf_bright").translate(stem_bend_x - 0.005, 0.016, stem_len + 0.008),
        lambda _p, nrm: np.clip(np.column_stack([0.76 + 0.18 * nrm[:, 1], 0.98 + 0.02 * nrm[:, 1], 0.40 + 0.16 * nrm[:, 1]]), 0.36, 1.0),
    )
    parts.extend([stem, foot])

    side_ang = 1.04 * (0.94 + 0.06 * spread)
    for ang, ln, tag in ((0.0, 0.36, "c"), (-side_ang, 0.26, "l"), (+side_ang, 0.26, "r")):
        rib = tint_part(
            curved_tube(
                [(0.0, 0.024, -0.025), (0.48 * ln * math.sin(ang), 0.036, -0.085 - 0.48 * ln * math.cos(ang)), (ln * math.sin(ang), 0.048, -0.085 - ln * math.cos(ang))],
                [0.0082, 0.0052, 0.0018],
                samples_per_seg=4,
                radial_segs=8,
                name=f"{name}_vein_{tag}",
                material="item_leaf_bright",
            ),
            lambda p, _n: np.tile(np.array([[0.86, 1.00, 0.42]], dtype=np.float64), (len(p), 1)),
        )
        parts.append(rib)

    for p in parts:
        sc.add("leaf", p.rotate_xyz(rx=tilt_deg, ry=yaw_deg, degrees=True))
    return _finalize_item(sc, target_span=target_span)


def build_leaf1() -> SceneBuilder:
    """Large lush 3-lobed serrated maple/grapevine leaf with downward petiole (`323:leaf1`)."""
    return _build_palmate_maple("item_leaf1", yaw_deg=-24.0, tilt_deg=4.0, spread=0.98, stem_len=0.28, stem_bend_x=+0.105, target_span=0.96)


def build_leaf2() -> SceneBuilder:
    """Tilted wide 3-lobed serrated maple leaf with down-left petiole (`324:leaf2`)."""
    return _build_palmate_maple("item_leaf2", yaw_deg=-54.0, tilt_deg=5.0, spread=1.05, stem_len=0.25, stem_bend_x=+0.020, target_span=0.92)


def _build_oak_leaf(
    name: str,
    *,
    yaw_deg: float,
    length: float,
    width_scale: float,
    spine_bend: float,
    left_lobes: Tuple[Tuple[float, float, float], ...],
    right_lobes: Tuple[Tuple[float, float, float], ...],
) -> SceneBuilder:
    """Build asymmetric deeply-incised dark-emerald oak/dandelion leaf with NO broomstick stem (`325:leaf3`, `326:leaf4`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)

    def _side_width(u: float, lobe_list: Tuple[Tuple[float, float, float], ...]) -> float:
        if u <= 0.0 or u >= 1.0:
            return 0.0008
        core = 0.028 * width_scale * (math.sin(u * math.pi) ** 0.55)
        ext = 0.0
        for u0, amp, sig in lobe_list:
            # Rounded-pointed authentic oak/dandelion lobe silhouette with crisp deep sinuses!
            ext = max(ext, amp * width_scale * math.exp(-(abs((u - u0) / sig) ** 1.62)))
        return max(0.002, core + ext)

    def _oak_color(_pos: np.ndarray, nrm: np.ndarray, u: np.ndarray, s: np.ndarray, top: np.ndarray) -> np.ndarray:
        mid_vein = np.exp(-((s / 0.085) ** 2.0)) * (1.0 - 0.20 * (u ** 2.0))
        lat_vein = (np.maximum(0.0, np.cos((u * 8.5 - np.abs(s) * 1.5) * math.pi)) ** 7) * (1.0 - 0.30 * np.abs(s))
        vein = np.clip(mid_vein + 0.36 * lat_vein, 0.0, 1.0)[:, None]
        # Bold 1998 split lighting: vivid sunlit chartreuse upper-left lobes vs deep pine-emerald lower-right shadow!
        sun_side = np.clip(0.68 - 0.44 * s + 0.18 * nrm[:, 1], 0.28, 1.18)[:, None]
        top_mul = np.where(top >= -0.05, 1.0, 0.56)[:, None]
        base = np.array([[0.28, 0.72, 0.16]], dtype=np.float64) * sun_side * top_mul
        glow = np.array([[0.86, 1.00, 0.42]], dtype=np.float64) * top_mul
        return base * (1.0 - 0.58 * vein) + glow * (0.58 * vein)

    blade = _build_swept_leaf_sheet(
        f"{name}_blade",
        length=length,
        w_left_fn=lambda u: _side_width(u, left_lobes),
        w_right_fn=lambda u: _side_width(u, right_lobes),
        spine_x_fn=lambda u: spine_bend * math.sin(u * math.pi * 1.05),
        spine_y_fn=lambda u: 0.032 * math.sin(u * math.pi),
        thickness=0.0085,
        v_cup=0.016,
        lobe_sweep=+0.54,
        ripple_amp=0.0040,
        ripple_freq=7.5,
        n_u=64,
        n_half=8,
        material="item_leaf_oak",
        color_fn=_oak_color,
    ).translate(0.0, 0.012, -0.5 * length)

    rib_pts = [(spine_bend * math.sin(t * math.pi * 1.05), 0.017 + 0.032 * math.sin(t * math.pi), (t - 0.5) * length) for t in (0.04, 0.30, 0.60, 0.90)]
    midrib = tint_part(
        curved_tube(rib_pts, [0.0065, 0.0058, 0.0045, 0.0022], samples_per_seg=4, radial_segs=8, name=f"{name}_midrib", material="item_leaf_bright"),
        lambda p, _n: np.tile(np.array([[0.76, 0.98, 0.40]], dtype=np.float64), (len(p), 1)),
    )
    sc.add("leaf", [p.rotate_xyz(ry=yaw_deg, degrees=True) for p in (blade, midrib)])
    return _finalize_item(sc, target_span=0.94)


def build_leaf3() -> SceneBuilder:
    """Diagonal deeply-lobed dark oak/dandelion leaf sweeping down-left (`325:leaf3`)."""
    return _build_oak_leaf(
        "item_leaf3",
        yaw_deg=-42.0,
        length=0.76,
        width_scale=1.02,
        spine_bend=-0.020,
        left_lobes=((0.22, 0.120, 0.062), (0.45, 0.165, 0.068), (0.68, 0.155, 0.066), (0.86, 0.105, 0.056)),
        right_lobes=((0.28, 0.130, 0.066), (0.54, 0.155, 0.068), (0.78, 0.125, 0.062)),
    )


def build_leaf4() -> SceneBuilder:
    """Slender upright curving deeply-lobed dark oak/dandelion leaf (`326:leaf4`)."""
    return _build_oak_leaf(
        "item_leaf4",
        yaw_deg=+10.0,
        length=0.78,
        width_scale=0.78,
        spine_bend=-0.042,
        left_lobes=((0.20, 0.105, 0.060), (0.44, 0.145, 0.064), (0.67, 0.135, 0.064), (0.85, 0.095, 0.055)),
        right_lobes=((0.28, 0.120, 0.064), (0.54, 0.145, 0.066), (0.78, 0.115, 0.060)),
    )


def _sculpt_bark_branch(
    pts: List[Tuple[float, float, float]],
    rads: List[float],
    *,
    name: str,
    material: str = "item_twig_bark",
    radial_segs: int = 22,
    samples_per_seg: int = 7,
    seed: float = 0.0,
    jagged_start: float = 0.0,
    jagged_end: float = 0.0,
    knuckles: Tuple[Tuple[float, float, float], ...] = (),
    base_rgb: Tuple[float, float, float] = (0.98, 0.94, 0.88),
    green_tip_start: float = 2.0,
    cap_start: bool = False,
    cap_end: bool = False,
) -> MeshPart:
    """Build a realistic gnarled fallen woodland branch with 3D fluted bark ridges, node knuckles & jagged snapped cuffs."""
    raw = curved_tube(
        pts,
        rads,
        samples_per_seg=samples_per_seg,
        radial_segs=radial_segs,
        cap_start=cap_start,
        cap_end=cap_end,
        cap_rings=2,
        name=name,
        material=material,
    )
    pos = raw.positions.astype(np.float64).copy()
    uvs = raw.uvs.astype(np.float32).copy()
    # Scale cylindrical UVs so the seamless `bark_wood` fissures, bark plates & lenticels wrap crisply around slender twigs.
    uvs[:, 0] *= 3.4
    uvs[:, 1] *= 1.35

    n_verts = len(pos)
    ridge_h = np.zeros(n_verts, dtype=np.float64)
    t_param = np.zeros(n_verts, dtype=np.float64)

    ring_start = 1 if cap_start else 0
    ring_end = n_verts - (1 if cap_end else 0)
    n_ring_verts = ring_end - ring_start
    if n_ring_verts > 0 and n_ring_verts % radial_segs == 0:
        n_rings = n_ring_verts // radial_segs
        rings = pos[ring_start:ring_end].reshape(n_rings, radial_segs, 3)
        centers = rings.mean(axis=1)
        tangents = np.zeros_like(centers)
        tangents[0] = centers[1] - centers[0]
        tangents[-1] = centers[-1] - centers[-2]
        if n_rings > 2:
            tangents[1:-1] = centers[2:] - centers[:-2]
        tangents /= np.maximum(1e-6, np.linalg.norm(tangents, axis=1, keepdims=True))

        thetas = np.linspace(0.0, 2.0 * math.pi, radial_segs, endpoint=False)[None, :]
        t_vals = np.linspace(0.0, 1.0, n_rings)[:, None]
        # Slowly twisting natural wood grain & branching bark flutes
        phi = thetas + 2.1 * t_vals + 0.52 * np.sin(t_vals * 10.0 + seed)
        flute_a = 0.14 * np.sin(phi * 5.0 + 0.85 * np.cos(t_vals * 13.0 + seed))
        flute_b = 0.085 * (np.abs(np.cos(phi * 9.0 - t_vals * 7.5 + seed * 1.7)) * 2.0 - 1.0)
        rough_c = 0.045 * np.sin(phi * 14.0 + t_vals * 34.0 + seed * 2.3)
        asymm = 0.10 * np.cos(2.0 * phi + seed * 0.8) + 0.055 * np.sin(3.0 * phi - t_vals * 6.0)
        # Organic longitudinal uneven thickness (real branches swell subtly at growth segments, never lathe-uniform!)
        uneven = 0.07 * np.sin(t_vals * 19.0 + seed * 1.3) + 0.04 * np.cos(t_vals * 33.0 - seed)
        swell = np.zeros_like(t_vals)
        for kt, ka, kw in knuckles:
            swell += ka * np.exp(-(((t_vals - kt) / max(0.015, kw)) ** 2))

        rad_scale = 1.0 + flute_a + flute_b + rough_c + asymm + uneven + swell
        radial_vecs = rings - centers[:, None, :]
        rings = centers[:, None, :] + radial_vecs * rad_scale[:, :, None]

        # Jagged broken bark collar teeth at open snapped ends (replaces smooth tube cuts!)
        if not cap_start and jagged_start > 0.0:
            teeth0 = (0.35 + 0.65 * np.sin(thetas * 3.0 + seed) * np.cos(thetas * 5.0 - seed * 0.7)) * jagged_start
            rings[0] -= tangents[0][None, :] * teeth0[0, :, None]
            if n_rings > 1:
                rings[1] -= tangents[1][None, :] * (0.42 * teeth0[0, :, None])
        if not cap_end and jagged_end > 0.0:
            teeth1 = (0.35 + 0.65 * np.cos(thetas * 3.0 - seed * 1.1) * np.sin(thetas * 5.0 + seed)) * jagged_end
            rings[-1] += tangents[-1][None, :] * teeth1[0, :, None]
            if n_rings > 1:
                rings[-2] += tangents[-2][None, :] * (0.42 * teeth1[0, :, None])

        pos[ring_start:ring_end] = rings.reshape(n_ring_verts, 3)
        ridge_h[ring_start:ring_end] = (flute_a + flute_b + 0.5 * rough_c).reshape(n_ring_verts)
        t_param[ring_start:ring_end] = np.broadcast_to(t_vals, (n_rings, radial_segs)).reshape(n_ring_verts)
        if cap_start:
            t_param[0] = 0.0
        if cap_end:
            t_param[-1] = 1.0
    else:
        # Fallback organic relief for non-ring geometry
        pos += 0.0028 * np.sin(pos * 64.0 + seed) * np.cos(np.roll(pos, 1, axis=1) * 48.0)

    pos32 = pos.astype(np.float32)
    nrms = recompute_smooth_normals(pos32, raw.indices)
    branch = MeshPart(name, material, pos32, nrms, raw.indices, uvs=uvs)

    def _bark_tint(p: np.ndarray, nrm: np.ndarray) -> np.ndarray:
        up = np.clip(0.54 + 0.46 * nrm[:, 1], 0.0, 1.0)
        # Deep umber shadow inside sculpted 3D bark fissures, warm sunlit wood crest on raised ridges
        crevice = np.clip(0.5 + ridge_h * 2.5, 0.0, 1.0)
        mottle = 0.07 * np.sin(p[:, 0] * 26.0 + p[:, 2] * 22.0 + seed) * np.cos(p[:, 2] * 38.0 - p[:, 0] * 18.0)
        shade = (0.58 + 0.28 * crevice + 0.22 * up + mottle)[:, None]
        col = np.asarray(base_rgb, dtype=np.float64)[None, :] * shade
        # Subtle warm rubbed inner-wood sheen on upper crests + cool dark umber in deepest fissures
        col[:, 0] *= 0.94 + 0.14 * crevice
        col[:, 1] *= 0.90 + 0.14 * crevice
        col[:, 2] *= 0.84 + 0.14 * crevice
        if green_tip_start < 1.0:
            g_blend = np.clip((t_param - green_tip_start) / max(0.05, 1.0 - green_tip_start), 0.0, 1.0)[:, None]
            green_bark = np.array([[0.38, 0.96, 0.32]], dtype=np.float64) * (0.78 + 0.26 * up[:, None])
            col = col * (1.0 - 0.92 * g_blend) + green_bark * (0.92 * g_blend)
        return np.clip(col, 0.18, 1.0)

    return tint_part(branch, _bark_tint)


def _add_snapped_wood_end(
    parts: List[MeshPart],
    prefix: str,
    end_pt: Tuple[float, float, float],
    out_dir: Tuple[float, float, float],
    radius: float,
    *,
    seed: float = 0.0,
) -> None:
    """Cap a broken twig end with an oblique snapped pale-heartwood fracture core and sharp wood splinters."""
    p0 = np.asarray(end_pt, dtype=np.float64)
    d = np.asarray(out_dir, dtype=np.float64)
    d /= max(1e-6, float(np.linalg.norm(d)))
    up = np.array([0.0, 1.0, 0.0], dtype=np.float64)
    side = np.cross(d, up)
    if float(np.linalg.norm(side)) < 1e-4:
        side = np.array([1.0, 0.0, 0.0], dtype=np.float64)
    else:
        side /= float(np.linalg.norm(side))
    ortho = np.cross(side, d)

    # Recessed diagonal sheared pale-tan heartwood core inside the jagged bark cuff
    c_in = p0 - d * (radius * 0.95)
    c_mid = p0 + d * (radius * 0.12)
    c_tip = p0 + d * (radius * 0.72) + side * (radius * 0.18 * math.sin(seed + 1.0)) + ortho * (radius * 0.14)
    core = tint_part(
        curved_tube(
            [tuple(c_in), tuple(c_mid), tuple(c_tip)],
            [radius * 0.84, radius * 0.76, radius * 0.34],
            samples_per_seg=3,
            radial_segs=12,
            cap_rings=2,
            name=f"{prefix}_heartwood",
            material="item_wood_heart",
        ).deform(lambda p: p + (radius * 0.10) * np.sin(p * 110.0 + seed)),
        lambda pos, nrm: np.clip(
            np.column_stack([
                0.96 + 0.04 * nrm[:, 1],
                0.88 + 0.06 * nrm[:, 1],
                0.68 + 0.08 * nrm[:, 1],
            ]),
            0.58,
            1.0,
        ),
    )
    parts.append(core)

    # Three sharp staggered wood splinters breaking off diagonally from the heartwood core!
    for k, (ang, ln_mul, r_mul) in enumerate(((0.4 + seed, 1.55, 0.32), (2.5 + seed, 1.25, 0.26), (4.6 + seed, 1.05, 0.24))):
        off = side * (math.cos(ang) * radius * 0.42) + ortho * (math.sin(ang) * radius * 0.36)
        s0 = p0 - d * (radius * 0.35) + off
        s1 = p0 + d * (radius * 0.55 * ln_mul) + off * 0.85
        s2 = p0 + d * (radius * ln_mul) + off * 0.45
        splint = tint_part(
            curved_tube(
                [tuple(s0), tuple(s1), tuple(s2)],
                [radius * r_mul, radius * r_mul * 0.62, radius * 0.08],
                samples_per_seg=2,
                radial_segs=6,
                cap_rings=2,
                name=f"{prefix}_splinter_{k}",
                material="item_wood_heart",
            ),
            lambda pos, _n: np.column_stack([
                np.full(len(pos), 0.98),
                np.full(len(pos), 0.90),
                np.full(len(pos), 0.72),
            ]),
        )
        parts.append(splint)


def _make_bark_knot(
    name: str,
    center: Tuple[float, float, float],
    out_dir: Tuple[float, float, float],
    radius: float,
    length: float,
    *,
    material: str = "item_twig_bark",
) -> MeshPart:
    """Tiny weathered dormant bud scar / broken twig knot flush along a branch bend."""
    p0 = np.asarray(center, dtype=np.float64)
    d = np.asarray(out_dir, dtype=np.float64)
    d /= max(1e-6, float(np.linalg.norm(d)))
    return tint_part(
        curved_tube(
            [tuple(p0 - d * (length * 0.30)), tuple(p0 + d * (length * 0.45)), tuple(p0 + d * length)],
            [radius * 1.15, radius * 0.82, radius * 0.48],
            samples_per_seg=2,
            radial_segs=9,
            cap_rings=2,
            name=name,
            material=material,
        ).deform(lambda p: p + 0.002 * np.sin(p * 120.0)),
        lambda pos, nrm: np.clip(
            np.column_stack([
                0.66 + 0.18 * np.clip(nrm[:, 1] + 0.4, 0.0, 1.0),
                0.56 + 0.16 * np.clip(nrm[:, 1] + 0.4, 0.0, 1.0),
                0.46 + 0.14 * np.clip(nrm[:, 1] + 0.4, 0.0, 1.0),
            ]),
            0.34,
            0.92,
        ),
    )


def build_stick1() -> SceneBuilder:
    """Weathered sinuous fallen woodland twig with gnarled bark ridges, knots & snapped splintered ends (`327:stick1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_stick1")
    # Exact 1:1 sinuous double-S curve traced from `stick1_4x.png` (vertical bottom-left shank -> graceful S-bend -> diagonal upper-right tip).
    pts = [
        (-0.228, 0.024, +0.440),
        (-0.240, 0.029, +0.285),
        (-0.232, 0.036, +0.145),
        (-0.155, 0.042, +0.048),
        (-0.048, 0.045, -0.022),
        (-0.005, 0.048, -0.145),
        (+0.072, 0.046, -0.258),
        (+0.178, 0.039, -0.340),
        (+0.258, 0.031, -0.435),
    ]
    rads = [0.022, 0.024, 0.027, 0.025, 0.026, 0.025, 0.028, 0.024, 0.021]
    twig = _sculpt_bark_branch(
        pts,
        rads,
        name="twig_main",
        material="item_twig_peeled",
        radial_segs=22,
        samples_per_seg=6,
        seed=1.4,
        jagged_start=0.024,
        jagged_end=0.022,
        knuckles=((0.26, 0.14, 0.045), (0.54, 0.16, 0.042), (0.75, 0.18, 0.045)),
        base_rgb=(1.08, 1.01, 0.90),
    )
    parts: List[MeshPart] = [twig]

    # Jagged snapped pale-heartwood fracture tips at BOTH bottom-left & top-right ends!
    _add_snapped_wood_end(parts, "stick1_bot", pts[0], (-0.08, -0.02, +1.00), rads[0], seed=0.6)
    _add_snapped_wood_end(parts, "stick1_top", pts[-1], (+0.64, +0.18, -0.74), rads[-1] * 1.12, seed=2.2)

    # Subtle weathered bark knots / dormant bud scars clinging organically at the natural wood bends (no big side spike!)
    parts.append(_make_bark_knot("twig_knot_a", (-0.162, 0.044, +0.052), (-0.70, 0.55, -0.45), 0.011, 0.022, material="item_twig_bark"))
    parts.append(_make_bark_knot("twig_knot_b", (+0.076, 0.050, -0.255), (-0.45, 0.60, -0.65), 0.012, 0.024, material="item_twig_bark"))
    parts.append(_make_bark_knot("twig_knot_c", (-0.012, 0.050, -0.140), (+0.65, 0.52, +0.55), 0.010, 0.018, material="item_twig_bark"))

    sc.add("stick", parts)
    return _finalize_item(sc, target_span=0.96)


def build_stick2a() -> SceneBuilder:
    """Long slender gnarled Y-forked woodland branch matching 1998 `198x56` (3.54:1) proportions (`328:stick2a`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_stick2a")

    # 1. Long whip-slender undulating branch traced 1:1 from `stick2a` (198x56px: stem thickness = 2.0-3.0% of total length!).
    main_pts = [
        (-0.500, 0.016, +0.020),
        (-0.395, 0.026, +0.018),
        (-0.275, 0.040, +0.002),
        (-0.155, 0.052, -0.024),
        (-0.045, 0.054, -0.030),
        (+0.035, 0.048, -0.016),
        (+0.105, 0.046, -0.020),
        (+0.205, 0.042, +0.000),
        (+0.315, 0.036, +0.018),
        (+0.420, 0.029, +0.036),
        (+0.492, 0.022, +0.050),
    ]
    main_rads = [0.0095, 0.0120, 0.0138, 0.0142, 0.0136, 0.0118, 0.0145, 0.0112, 0.0092, 0.0072, 0.0044]
    main_branch = _sculpt_bark_branch(
        main_pts,
        main_rads,
        name="branch_main",
        material="item_twig_bark",
        radial_segs=20,
        samples_per_seg=6,
        seed=3.1,
        jagged_start=0.014,
        jagged_end=0.0,
        knuckles=((0.34, 0.14, 0.040), (0.60, 0.20, 0.045), (0.82, 0.12, 0.035)),
        base_rgb=(1.02, 0.96, 0.88),
        green_tip_start=0.58,
        cap_start=False,
        cap_end=True,
    )
    parts: List[MeshPart] = [main_branch]

    # Fresh mossy-green living shoot overlay creeping cleanly along the long slender right-hand tip (`x = +0.18..+0.496`).
    green_shoot = tint_part(
        curved_tube(
            [(+0.185, 0.043, -0.004), (+0.295, 0.038, +0.015), (+0.405, 0.031, +0.034), (+0.496, 0.023, +0.051)],
            [0.0085, 0.0096, 0.0074, 0.0042],
            samples_per_seg=4,
            radial_segs=12,
            cap_rings=2,
            name="branch_green_shoot",
            material="item_stem_green",
        ).deform(lambda p: p + 0.0012 * np.sin(p * 110.0)),
        lambda pos, nrm: np.clip(
            np.column_stack([
                0.56 + 0.24 * np.clip(nrm[:, 1] + 0.4, 0.0, 1.0),
                0.86 + 0.14 * np.clip(nrm[:, 1] + 0.4, 0.0, 1.0),
                0.42 + 0.16 * np.clip(nrm[:, 1] + 0.4, 0.0, 1.0),
            ]),
            0.35,
            1.0,
        ),
    )
    parts.append(green_shoot)

    # 2. Iconic upper-right broken Y-fork side branch spur (`x = +0.10 -> +0.265`, exact 1998 length & angle!).
    fork_pts = [
        (+0.088, 0.046, -0.018),
        (+0.135, 0.050, -0.042),
        (+0.198, 0.048, -0.078),
        (+0.262, 0.042, -0.112),
    ]
    fork_rads = [0.0132, 0.0120, 0.0110, 0.0098]
    fork_branch = _sculpt_bark_branch(
        fork_pts,
        fork_rads,
        name="branch_fork",
        material="item_twig_bark",
        radial_segs=18,
        samples_per_seg=5,
        seed=5.2,
        jagged_start=0.0,
        jagged_end=0.013,
        knuckles=((0.22, 0.15, 0.09),),
        base_rgb=(0.98, 0.92, 0.84),
        cap_start=False,
        cap_end=False,
    )
    parts.append(fork_branch)

    # Jagged snapped pale-heartwood fracture tips on BOTH far-left main stem AND upper-right broken fork!
    _add_snapped_wood_end(parts, "stick2_left", main_pts[0], (-1.00, -0.06, +0.04), main_rads[0], seed=1.1)
    _add_snapped_wood_end(parts, "stick2_fork", fork_pts[-1], (+0.86, -0.04, -0.50), fork_rads[-1], seed=3.7)

    # Tiny dormant bark knots along the long slender weathered stem
    parts.append(_make_bark_knot("branch_knot_a", (-0.285, 0.041, +0.003), (-0.30, 0.58, -0.75), 0.0062, 0.013))
    parts.append(_make_bark_knot("branch_knot_b", (-0.060, 0.055, -0.029), (+0.25, 0.65, +0.72), 0.0058, 0.012))

    # 3. Delicate short green leaf petioles & twin compact slender drooping pointed leaves right at the far-right tip!
    pet_a = curved_tube(
        [(+0.358, 0.034, +0.026), (+0.368, 0.032, +0.040), (+0.376, 0.028, +0.052)],
        [0.0044, 0.0034, 0.0026],
        samples_per_seg=2,
        radial_segs=8,
        name="branch_petiole_a",
        material="item_stem_green",
    )
    pet_b = curved_tube(
        [(+0.398, 0.031, +0.034), (+0.412, 0.032, +0.046), (+0.424, 0.030, +0.058)],
        [0.0040, 0.0030, 0.0024],
        samples_per_seg=2,
        radial_segs=8,
        name="branch_petiole_b",
        material="item_stem_green",
    )
    leaf_a = (
        _sculpt_pointed_blade("branch_leaf_a", length=0.142, width=0.058, material="item_leaf_rich", arch=0.014, vein_glow=0.28)
        .rotate_xyz(rx=0.12, ry=0.34, rz=-0.06)
        .translate(+0.366, 0.028, +0.038)
    )
    leaf_b = (
        _sculpt_pointed_blade("branch_leaf_b", length=0.168, width=0.072, material="item_leaf_bright", arch=0.016, vein_glow=0.34)
        .rotate_xyz(rx=0.10, ry=0.68, rz=-0.08)
        .translate(+0.398, 0.033, +0.042)
    )
    parts.extend([pet_a, pet_b, leaf_a, leaf_b])

    sc.add("stick_leafy", parts)
    # True 1998 scale relative to `stick1` (198px vs 137px -> 1.38 world span, exact 3.54:1 visual aspect ratio!)
    return _finalize_item(sc, target_span=1.38)


def build_plant1() -> SceneBuilder:
    """Lush 5-leaf crinkled mint/basil herb rosette (`409:plant1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_plant1")
    sc.add("plant", ellipsoid(0.026, 0.020, 0.026, u_segs=10, v_segs=8, name="plant_crown", material="item_stem_green").translate(0.0, 0.032, 0.0))
    outer_specs = (
        (-136.0, 0.45, 0.27, 10.0, +0.020),
        (-62.0, 0.41, 0.25, 9.0, -0.018),
        (+22.0, 0.46, 0.27, 10.0, +0.018),
        (+96.0, 0.40, 0.24, 9.0, -0.018),
        (+166.0, 0.42, 0.25, 11.0, +0.016),
    )
    for k, (ang_deg, ln, wd, pitch_deg, crv) in enumerate(outer_specs):
        lf = (
            _sculpt_pointed_blade(f"plant_leaf_outer_{k}", length=ln, width=wd, material="item_leaf_rich", serration=0.095, arch=0.036, vein_glow=0.52, curve_x=crv)
            .rotate_xyz(rx=pitch_deg, ry=ang_deg, degrees=True)
            .translate(0.0, 0.044 + 0.007 * (k % 2), 0.0)
        )
        sc.add("plant", lf)
    for k in range(5):
        ang_deg = k * 72.0 + 36.0
        lf = (
            _sculpt_pointed_blade(f"plant_leaf_inner_{k}", length=0.23 + 0.02 * (k % 2), width=0.145, material="item_leaf_bright", serration=0.075, arch=0.026, vein_glow=0.50)
            .rotate_xyz(rx=-12.0 - 4.0 * (k % 2), ry=ang_deg, degrees=True)
            .translate(0.0, 0.062 + 0.005 * (k % 2), 0.0)
        )
        sc.add("plant", lf)
    return _finalize_item(sc, target_span=0.94)


def _sculpt_curved_ivy_blade(
    name: str,
    *,
    length: float,
    width: float,
    curve_x: float = 0.0,
    asym: float = 0.0,
) -> MeshPart:
    """Build one smooth C-infinity sickle-curved broad ovate-pointed backyard ivy leaflet with bold diagonal vein bands."""
    hw = width * 0.5

    def _w(u: float, side_mult: float) -> float:
        if u <= 0.0 or u >= 1.0:
            return 0.0008
        # Plump rounded heart-ovate belly peaking at u=0.29 tapering to a RAZOR-SHARP pointed ivy tip!
        env = (u ** 0.44) * ((1.0 - u) ** 1.10) * 2.58
        return hw * side_mult * max(0.015, env)

    def _ivy_color(_pos: np.ndarray, nrm: np.ndarray, u: np.ndarray, s: np.ndarray, top: np.ndarray) -> np.ndarray:
        # Crisp dark emerald midrib crease + bold luminous 1998 chartreuse diagonal vein stripes!
        mid_dark = np.exp(-((s / 0.075) ** 2.0)) * (1.0 - 0.25 * u)
        diag = (np.maximum(0.0, np.cos((u * 5.2 - np.abs(s) * 1.45) * math.pi)) ** 4) * (1.0 - 0.16 * u) * (u > 0.12) * (np.abs(s) > 0.06)
        base_ao = (0.34 + 0.70 * np.clip(u / 0.36, 0.0, 1.0) + 0.08 * nrm[:, 1])[:, None]
        side_lit = np.where(top >= -0.05, 1.0, 0.62)[:, None]
        lamina = np.array([[0.32, 0.84, 0.13]], dtype=np.float64) * base_ao * side_lit * (1.0 - 0.36 * mid_dark[:, None])
        glow = np.array([[0.94, 1.00, 0.40]], dtype=np.float64) * side_lit
        return lamina * (1.0 - 0.78 * diag[:, None]) + glow * (0.78 * diag[:, None])

    return _build_swept_leaf_sheet(
        name,
        length=length,
        w_left_fn=lambda u: _w(u, 1.0 - asym),
        w_right_fn=lambda u: _w(u, 1.0 + asym),
        spine_x_fn=(lambda u: curve_x * (u ** 1.55)) if abs(curve_x) > 1e-5 else None,
        spine_y_fn=lambda u: 0.032 * math.sin(u * math.pi * 0.90) - 0.015 * (u ** 2.2),
        thickness=0.0072,
        v_cup=0.014,
        ripple_amp=0.0024,
        ripple_freq=5.0,
        n_u=28,
        n_half=8,
        material="item_leaf_bright",
        color_fn=_ivy_color,
    )


def _build_ivy_cluster(name: str, leaf_specs: List[Tuple[float, float, float, float, float, float]]) -> SceneBuilder:
    """Build bright 3-leaf sickle-curved backyard ivy sprig with tucked nodal base (NO center sphere!)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    stem_tuck = curved_tube(
        [(0.0, 0.005, -0.040), (0.0, 0.016, -0.008), (0.0, 0.020, +0.010)],
        [0.007, 0.008, 0.006],
        samples_per_seg=2,
        radial_segs=8,
        name=f"{name}_tuck",
        material="item_stem_green",
    )
    sc.add("ivy", stem_tuck)
    for idx, (ang_deg, length, width, pitch_deg, curve_x, asym) in enumerate(leaf_specs):
        lf = (
            _sculpt_curved_ivy_blade(f"{name}_leaf_{idx}", length=length, width=width, curve_x=curve_x, asym=asym)
            .rotate_xyz(rx=pitch_deg, ry=ang_deg, degrees=True)
            .translate(0.0, 0.024 + 0.005 * idx, 0.0)
        )
        sc.add("ivy", lf)
    return _finalize_item(sc, target_span=0.94)


def build_ivyrt() -> SceneBuilder:
    """Three-leaf curved bright ivy sprig fanning right & downward (`417:ivyrt`)."""
    return _build_ivy_cluster(
        "item_ivyrt",
        [
            (+118.0, 0.44, 0.30, 8.0, +0.072, +0.16),
            (+44.0, 0.45, 0.27, 11.0, -0.028, -0.06),
            (-52.0, 0.43, 0.28, 10.0, +0.065, +0.14),
        ],
    )


def build_ivylft() -> SceneBuilder:
    """Three-leaf curved bright ivy sprig fanning left & downward (`418:ivylft`)."""
    return _build_ivy_cluster(
        "item_ivylft",
        [
            (-114.0, 0.43, 0.30, 8.0, -0.072, -0.16),
            (-44.0, 0.45, 0.28, 11.0, +0.036, +0.08),
            (+38.0, 0.44, 0.27, 11.0, -0.052, -0.10),
        ],
    )


def build_ivyfront() -> SceneBuilder:
    """Wide T-fan 3-leaf curved backyard ivy sprig (`419:ivyfront`)."""
    return _build_ivy_cluster(
        "item_ivyfront",
        [
            (-86.0, 0.45, 0.28, 9.0, -0.065, -0.16),
            (+86.0, 0.45, 0.28, 9.0, +0.065, +0.16),
            (0.0, 0.43, 0.28, 12.0, 0.0, 0.0),
        ],
    )


def build_flower1b() -> SceneBuilder:
    """Oxeye Daisy bloom seen in graceful 3/4 side profile (`411:flower1b`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_flower1b")
    hx, hy, hz = _build_daisy_base(sc, petal_count=12, tilt_deg=48.0, with_leaves=False)
    pollen = tint_part(
        superellipsoid(0.076, 0.040, 0.076, e1=0.78, e2=0.98, u_segs=26, v_segs=14, name="daisy1b_pollen", material="item_pollen_gold")
        .deform(lambda p: p + 0.0035 * np.sin(p * 78.0) * np.cos(np.roll(p, 1, axis=1) * 72.0))
        .translate(0.0, 0.015, 0.0)
        .rotate_xyz(rx=48.0, degrees=True)
        .translate(hx, hy, hz),
        lambda pos, _n: np.clip(
            np.column_stack([
                0.96 - 0.14 * np.exp(-((np.hypot(pos[:, 0] - hx, pos[:, 2] - hz) / 0.034) ** 2)),
                0.86 - 0.24 * np.exp(-((np.hypot(pos[:, 0] - hx, pos[:, 2] - hz) / 0.034) ** 2)),
                0.38 - 0.16 * np.exp(-((np.hypot(pos[:, 0] - hx, pos[:, 2] - hz) / 0.034) ** 2)),
            ]),
            0.20,
            1.0,
        ),
    )
    sc.add("flower", pollen)
    # Turn side-profile bloom facing forward-right matching `411:flower1b`, THEN add front-facing stem leaves!
    sc.nodes["flower"] = [p.rotate_xyz(ry=52.0, degrees=True) for p in sc.nodes["flower"]]
    sc.add("flower", _build_daisy_stem_leaf("daisy1b_leaf_l", length=0.195, width=0.132, side=-1.0, curve=0.014, pitch=0.16, yaw_mag=1.40, face_tilt=0.76).translate(-0.008, 0.135, -0.010))
    sc.add("flower", _build_daisy_stem_leaf("daisy1b_leaf_r", length=0.235, width=0.158, side=+1.0, curve=0.018, pitch=0.10, yaw_mag=1.34, face_tilt=0.74).translate(-0.004, 0.175, -0.008))
    return _finalize_item(sc, target_span=0.94)


def _sculpt_star_petal(name: str, *, length: float, width: float, material: str, curve_x: float = 0.0) -> MeshPart:
    """Slender gracefully arching 1998 Cosmos/Starflower petal (zero ellipsoid pinch!)."""
    hw = width * 0.5

    def _w(u: float) -> float:
        if u <= 0.0 or u >= 1.0:
            return 0.0008
        env = (math.sin(u * math.pi) ** 0.52) * (1.08 - 0.20 * u)
        return hw * max(0.02, env)

    def _petal_tint(_pos: np.ndarray, nrm: np.ndarray, u: np.ndarray, s: np.ndarray, _top: np.ndarray) -> np.ndarray:
        throat = np.exp(-((u / 0.24) ** 2.0))[:, None]
        stripe = (0.06 * np.cos(s * math.pi * 3.0) * (1.0 - 0.5 * u))[:, None]
        base = np.array([[0.98, 0.88, 0.94]], dtype=np.float64) * (0.86 + 0.14 * u[:, None] + 0.06 * nrm[:, 1:2]) + stripe
        deep = np.array([[0.64, 0.28, 0.46]], dtype=np.float64)
        return np.clip(base * (1.0 - 0.55 * throat) + deep * (0.55 * throat), 0.32, 1.0)

    return _build_swept_leaf_sheet(
        name,
        length=length,
        w_left_fn=_w,
        w_right_fn=_w,
        spine_x_fn=(lambda u: curve_x * (u ** 1.7)) if abs(curve_x) > 1e-5 else None,
        spine_y_fn=lambda u: 0.024 * math.sin(u * math.pi * 0.90) - 0.026 * (u ** 2.1),
        thickness=0.0068,
        v_cup=0.0075,
        ripple_amp=0.0014,
        ripple_freq=4.0,
        n_u=24,
        n_half=6,
        material=material,
        color_fn=_petal_tint,
    )


def _build_starflower(name: str, *, petal_mat: str, petal_count: int, tilt_deg: float, yaw_deg: float) -> SceneBuilder:
    """Hot-pink / rose 8-petal cosmos starflower with arching golden stamens & broad ovate stem leaves (`412`, `413`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    stem = curved_tube(
        [(0.0, 0.00, -0.04), (0.010, 0.22, -0.02), (-0.008, 0.44, 0.00), (0.0, 0.62, +0.02)],
        [0.018, 0.016, 0.015, 0.016],
        samples_per_seg=5,
        radial_segs=12,
        cap_rings=2,
        name=f"{name}_stem",
        material="item_stem_green",
    )
    sc.add("flower", stem)

    head_parts: List[MeshPart] = [
        superellipsoid(0.056, 0.030, 0.056, e1=0.80, e2=0.95, u_segs=16, v_segs=8, name=f"{name}_calyx", material="item_stem_green").translate(0.0, -0.028, 0.0),
        superellipsoid(0.058, 0.030, 0.058, e1=0.80, e2=0.95, u_segs=16, v_segs=8, name=f"{name}_throat", material="item_petal_rose").translate(0.0, 0.006, 0.0),
    ]
    for k in range(petal_count):
        ang = 2.0 * math.pi * k / petal_count + 0.18
        plen = 0.325 + 0.022 * math.sin(k * 2.1)
        pet = (
            _sculpt_star_petal(f"{name}_petal_{k}", length=plen, width=0.086, material=petal_mat, curve_x=0.026 * math.sin(k * 1.7 + 0.5))
            .rotate_xyz(rx=0.12 + 0.07 * (k % 2), ry=ang)
            .translate(0.032 * math.sin(ang), 0.006, 0.032 * math.cos(ang))
        )
        head_parts.append(pet)

    for k in range(7):
        ang = 2.0 * math.pi * k / 7.0 + 0.4
        r_tip = 0.098 if k % 2 else 0.072
        h_tip = 0.085 if k % 2 else 0.118
        fil = curved_tube(
            [(0.0, 0.020, 0.0), (0.45 * r_tip * math.sin(ang), h_tip * 0.65, 0.45 * r_tip * math.cos(ang)), (r_tip * math.sin(ang), h_tip, r_tip * math.cos(ang))],
            [0.007, 0.005, 0.005],
            samples_per_seg=3,
            radial_segs=8,
            name=f"{name}_fil_{k}",
            material="item_pollen_gold",
        )
        anther = ellipsoid(0.015, 0.012, 0.022, u_segs=10, v_segs=8, name=f"{name}_anther_{k}", material="item_pollen_gold").rotate_xyz(ry=ang).translate(r_tip * math.sin(ang), h_tip, r_tip * math.cos(ang))
        head_parts.extend([fil, anther])

    for p in head_parts:
        sc.add("flower", p.rotate_xyz(rx=tilt_deg, degrees=True).translate(0.0, 0.62, +0.02))
    if abs(yaw_deg) > 1e-3:
        sc.nodes["flower"] = [p.rotate_xyz(ry=yaw_deg, degrees=True) for p in sc.nodes["flower"]]
    # Attach broad front-facing triangular-ovate stem leaves AFTER yaw so both crisp faces shine like `flowers_sheet.png`!
    sc.add("flower", _build_daisy_stem_leaf(f"{name}_leaf_l", length=0.220, width=0.142, side=-1.0, curve=0.016, pitch=0.14, yaw_mag=1.40, face_tilt=0.80).translate(0.0, 0.145, -0.020))
    sc.add("flower", _build_daisy_stem_leaf(f"{name}_leaf_r", length=0.245, width=0.158, side=+1.0, curve=0.018, pitch=0.16, yaw_mag=1.34, face_tilt=0.78).translate(0.0, 0.165, -0.016))
    return _finalize_item(sc, target_span=0.94)


def build_flower2a() -> SceneBuilder:
    """Hot-pink 8-petal Cosmos starflower with arching golden stamens (`412:flower2a`)."""
    return _build_starflower("item_flower2a", petal_mat="item_petal_pink", petal_count=8, tilt_deg=42.0, yaw_deg=0.0)


def build_flower2b() -> SceneBuilder:
    """Deep rose-magenta 8-petal starflower variant (`413:flower2b`)."""
    return _build_starflower("item_flower2b", petal_mat="item_petal_rose", petal_count=8, tilt_deg=46.0, yaw_deg=-22.0)


def _sculpt_lily_petal(name: str, *, length: float, width: float, material: str) -> MeshPart:
    """Broad plump spotted diamond-ovate tiger lily petal tapering to a crisp pointed tip (`flower3a..3c`)."""
    hw = width * 0.5

    def _w(u: float) -> float:
        if u <= 0.0 or u >= 1.0:
            return 0.0008
        return hw * max(0.015, (u ** 0.52) * ((1.0 - u) ** 0.96) * 2.48)

    def _lily_tint(pos: np.ndarray, nrm: np.ndarray, u: np.ndarray, s: np.ndarray, _top: np.ndarray) -> np.ndarray:
        streaks = (np.maximum(0.0, np.cos((s * 5.2 + 0.6 * np.sin(u * 12.0)) * math.pi)) ** 4) * (1.0 - 0.25 * u)
        spots = ((np.sin(pos[:, 0] * 54.0) * np.cos(pos[:, 2] * 52.0)) > 0.42).astype(np.float64) * (u > 0.14) * (u < 0.86)
        throat = np.exp(-((u / 0.24) ** 2.0))
        base = np.array([[0.96, 0.90, 1.00]], dtype=np.float64) * (0.90 + 0.10 * nrm[:, 1:2])
        dark = np.array([[0.52, 0.30, 0.80]], dtype=np.float64)
        w = np.clip(0.32 * streaks + 0.38 * spots + 0.42 * throat, 0.0, 0.74)[:, None]
        return np.clip(base * (1.0 - w) + dark * w, 0.26, 1.0)

    return _build_swept_leaf_sheet(
        name,
        length=length,
        w_left_fn=_w,
        w_right_fn=_w,
        spine_y_fn=lambda u: 0.036 * math.sin(u * math.pi * 0.90) - 0.026 * (u ** 2.1),
        thickness=0.0080,
        v_cup=0.012,
        ripple_amp=0.0022,
        ripple_freq=5.0,
        n_u=24,
        n_half=7,
        material=material,
        color_fn=_lily_tint,
    )


def _build_purple_lily_base(
    sc: SceneBuilder,
    *,
    petal_mat: str,
    tilt_deg: float,
    with_stamens: bool = True,
) -> Tuple[float, float, float]:
    """Build shared 5-petal spotted lavender/purple tiger lily (`flower3a`, `flower3b`, `flower3c`)."""
    stem = curved_tube(
        [(0.0, 0.00, -0.04), (0.014, 0.18, -0.02), (-0.008, 0.38, 0.00), (0.0, 0.56, +0.02)],
        [0.022, 0.018, 0.016, 0.018],
        samples_per_seg=5,
        radial_segs=12,
        cap_rings=2,
        name="lily_stem",
        material="item_stem_green",
    )
    sc.add("flower", stem)

    hx, hy, hz = 0.0, 0.58, +0.02
    head_parts: List[MeshPart] = [
        # Tucked calyx behind bloom + rich violet throat cushion in front (zero green ball poking between petals!)
        superellipsoid(0.052, 0.032, 0.052, e1=0.80, e2=0.95, u_segs=16, v_segs=8, name="lily_calyx", material="item_stem_green").translate(0.0, -0.036, 0.0),
        superellipsoid(0.058, 0.026, 0.058, e1=0.80, e2=0.95, u_segs=16, v_segs=8, name="lily_throat", material="item_petal_violet").translate(0.0, 0.002, 0.0),
    ]
    for k in range(5):
        ang = 2.0 * math.pi * k / 5.0 + math.pi
        pet = (
            _sculpt_lily_petal(f"lily_petal_{k}", length=0.315, width=0.165, material=petal_mat)
            .rotate_xyz(rx=0.18, ry=ang)
            .translate(0.022 * math.sin(ang), 0.005 * (k % 2), 0.022 * math.cos(ang))
        )
        head_parts.append(pet)

    if with_stamens:
        for k in range(5):
            ang = 2.0 * math.pi * k / 5.0 + 0.35
            fil = curved_tube(
                [(0.0, 0.018, 0.0), (0.055 * math.sin(ang), 0.095, 0.055 * math.cos(ang)), (0.110 * math.sin(ang), 0.125, 0.110 * math.cos(ang))],
                [0.007, 0.005, 0.005],
                samples_per_seg=3,
                radial_segs=8,
                name=f"lily_stamen_{k}",
                material="item_petal_lilac",
            )
            tip = ellipsoid(0.016, 0.012, 0.026, u_segs=10, v_segs=8, name=f"lily_anther_{k}", material="item_pollen_gold").rotate_xyz(ry=ang).translate(0.112 * math.sin(ang), 0.126, 0.112 * math.cos(ang))
            head_parts.extend([fil, tip])

    for p in head_parts:
        sc.add("flower", p.rotate_xyz(rx=tilt_deg, degrees=True).translate(hx, hy, hz))
    return hx, hy, hz


def build_flower3a() -> SceneBuilder:
    """Front-facing spotted lilac 5-petal tiger lily with golden stamens (`414:flower3a`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_flower3a")
    _build_purple_lily_base(sc, petal_mat="item_petal_lilac", tilt_deg=44.0, with_stamens=True)
    # Prominent crisp pointed lower leaf dropping down-front (`flower3a`!) + small left tuck leaf
    sc.add("flower", _build_daisy_stem_leaf("lily3a_leaf_f", length=0.220, width=0.145, side=+1.0, curve=0.006, pitch=0.95, yaw_mag=0.20, face_tilt=0.10).translate(0.0, 0.195, +0.002))
    sc.add("flower", _build_daisy_stem_leaf("lily3a_leaf_l", length=0.125, width=0.082, side=-1.0, curve=0.010, pitch=0.18, yaw_mag=1.35, face_tilt=0.72).translate(0.0, 0.255, -0.014))
    return _finalize_item(sc, target_span=0.94)


def build_flower3b() -> SceneBuilder:
    """Deep violet spotted lily angled in side-3/4 profile (`415:flower3b`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_flower3b")
    _build_purple_lily_base(sc, petal_mat="item_petal_violet", tilt_deg=56.0, with_stamens=True)
    sc.nodes["flower"] = [p.rotate_xyz(ry=52.0, degrees=True) for p in sc.nodes["flower"]]
    # Crisp pointed leaf fanning left-down in world space matching `415:flower3b`!
    sc.add("flower", _build_daisy_stem_leaf("lily3b_leaf_l", length=0.215, width=0.142, side=-1.0, curve=0.012, pitch=0.22, yaw_mag=1.36, face_tilt=0.80).translate(-0.010, 0.165, -0.015))
    return _finalize_item(sc, target_span=0.94)


def build_flower3c() -> SceneBuilder:
    """Iconic 1998 goofy googly-eyed lavender flower sticking out its pink tongue (`416:flower3c`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_flower3c")
    hx, hy, hz = _build_purple_lily_base(sc, petal_mat="item_petal_lilac", tilt_deg=48.0, with_stamens=False)
    # Crisp bright-green broad pointed leaf angling UP-RIGHT + tiny left stipule (`416:flower3c`)!
    sc.add("flower", _build_daisy_stem_leaf("lily3c_leaf_r", length=0.235, width=0.148, side=+1.0, curve=-0.014, pitch=-0.42, yaw_mag=1.34, face_tilt=0.82).translate(0.0, 0.115, -0.015))
    sc.add("flower", _build_daisy_stem_leaf("lily3c_leaf_l", length=0.105, width=0.072, side=-1.0, curve=0.008, pitch=0.14, yaw_mag=1.36, face_tilt=0.76).translate(0.0, 0.125, -0.018))

    face_parts: List[MeshPart] = []
    # Two oversized bulging cartoony white googly eyes perched right on the bloom center (`416:flower3c`)!
    for side, tag, py_off in ((+1.0, "l", 0.008), (-1.0, "r", 0.000)):
        eye_w = ellipsoid(0.044, 0.046, 0.054, u_segs=20, v_segs=14, name=f"googly_eye_{tag}", material="eye_white").translate(side * 0.042, 0.056 + py_off, -0.022)
        pupil = sphere(0.022, u_segs=14, v_segs=10, name=f"googly_pupil_{tag}", material="eye_pupil").translate(side * 0.036, 0.092 + py_off, -0.012)
        glint = sphere(0.008, u_segs=10, v_segs=8, name=f"googly_glint_{tag}", material="eye_catchlight").translate(side * 0.044, 0.110 + py_off, -0.020)
        face_parts.extend([eye_w, pupil, glint])

    # Dark open laughing mouth pocket + big cheeky bright red-pink drooping tongue sticking out (`416:flower3c`)!
    mouth = ellipsoid(0.062, 0.022, 0.046, u_segs=16, v_segs=10, name="googly_mouth", material="item_dark_stitch").translate(0.0, 0.026, +0.046)

    def _sculpt_tongue(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        u = np.clip((p[:, 2] + 0.065) / 0.130, 0.0, 1.0)
        p[:, 1] -= 0.035 * (u ** 1.8)  # droop playfully downward
        p[:, 1] -= 0.009 * np.exp(-((p[:, 0] / 0.016) ** 2)) * np.sin(u * math.pi)  # central tongue groove!
        return p

    tongue = (
        superellipsoid(0.058, 0.024, 0.072, e1=0.72, e2=0.86, u_segs=24, v_segs=14, name="googly_tongue", material="item_tongue_pink")
        .deform(_sculpt_tongue)
        .rotate_xyz(rx=0.22)
        .translate(0.0, 0.046, +0.088)
    )
    face_parts.extend([mouth, tongue])

    for p in face_parts:
        sc.add("flower", p.rotate_xyz(rx=48.0, degrees=True).translate(hx, hy, hz))
    return _finalize_item(sc, target_span=0.94)


def build_flower1() -> SceneBuilder:
    """Still centered Groucho Disguise Daisy matching `421:flower1` (exact same 8-petal disguise model as `dflower1`)."""
    return _build_groucho_daisy("item_flower1", animated=False)
