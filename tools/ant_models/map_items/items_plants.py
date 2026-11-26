"""Realistic 3D sculptors for 1998 Ants botanical grass, clovers, flowers, and mushroom (Wave 1).

Covers:
- `grass1`    (1320): Realistic multi-blade arching grass tuft (natural root-to-tip gradient)
- `grass3`    (1322): Lush fanning mid-size meadow grass cluster
- `grassbig1` (1325): Dense tall wild grass clump with overlapping arching blades
- `clover1`   (404):  Realistic 4-leaf clover with veined heart-shaped leaflets
- `flower1a`  (410):  Realistic Oxeye Daisy with golden pollen disc & curved petals
- `dflower1`  (420):  Iconic 1998 Groucho-Marx sunglasses & mustache daisy
- `mush2a`    (329):  Organic woodland spotted mushroom
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
            ensure_outward_winding,
            lathe,
            quat_from_euler_xyz,
            recompute_smooth_normals,
            rounded_box,
            sphere,
            superellipsoid,
            torus_arc,
        )
        from .item_materials import gradient_y_tint, register_item_materials, tint_part
        from .items_toys import _finalize_item
    except ImportError:
        from map_items.item_materials import gradient_y_tint, register_item_materials, tint_part  # type: ignore
        from map_items.items_toys import _finalize_item  # type: ignore
        from mesh_core import (  # type: ignore
            AnimationChannel,
            MeshPart,
            SceneBuilder,
            curved_tube,
            cylinder,
            ellipsoid,
            ensure_outward_winding,
            lathe,
            quat_from_euler_xyz,
            recompute_smooth_normals,
            rounded_box,
            sphere,
            superellipsoid,
            torus_arc,
        )


def _curved_grass_blade(
    name: str,
    *,
    base_xz: Tuple[float, float],
    base_y: float = 0.0,
    height: float,
    width: float,
    yaw: float,
    lean: float,
    droop: float,
    twist: float,
    sway: float = 0.0,
    shade_mult: float = 1.0,
    hue_shift: Tuple[float, float, float] = (1.0, 1.0, 1.0),
    segs: int = 8,
) -> MeshPart:
    """Build one realistic broad V-ribbed, gracefully arching & flexing 3D grass blade with rich root-to-tip chlorophyll gradient."""
    bx, bz = base_xz
    cy, sy = math.cos(yaw), math.sin(yaw)
    verts: List[Tuple[float, float, float]] = []
    uvs: List[Tuple[float, float]] = []
    cols: List[Tuple[float, float, float]] = []
    tris: List[Tuple[int, int, int]] = []

    # 3 vertices across blade width (left edge, recessed V-midrib center, right edge) -> genuine sculpted 3D blade!
    for i in range(segs + 1):
        u = i / float(segs)
        horiz = lean * (u ** 1.24) + droop * (u ** 2.30)
        lat_bend = sway * math.sin(u * math.pi * 1.15) * (u ** 1.1)
        vy = base_y + height * (u - 0.28 * max(0.0, droop) * (u ** 2.55))
        # Natural botanical blade profile: sturdy broad lower sheath -> graceful lance swell -> crisp pointed tip!
        w = width * (0.94 + 0.14 * math.sin(u * math.pi * 0.65)) * (1.0 - 0.90 * (u ** 1.65))
        if i == segs:
            w = 0.0020
        tw = twist * u
        ct, st = math.cos(tw), math.sin(tw)
        v_crease = 0.34 * w

        cx_w = bx + sy * horiz + cy * lat_bend
        cz_w = bz + cy * horiz - sy * lat_bend
        # Deep shaded forest-emerald base -> rich lush lawn green -> sunlit chartreuse-lime tips!
        r_c = (0.24 + 0.66 * (u ** 1.08)) * hue_shift[0] * shade_mult
        g_c = (0.52 + 0.48 * (u ** 0.68)) * hue_shift[1] * shade_mult
        b_c = (0.18 + 0.18 * (u ** 1.35)) * hue_shift[2] * shade_mult

        for j, (s_w, rib) in enumerate(((-0.5, 0.0), (0.0, -v_crease), (+0.5, 0.0))):
            lat = s_w * w
            px = cx_w + cy * (lat * ct) + sy * (rib - lat * st * 0.28)
            pz = cz_w - sy * (lat * ct) + cy * (rib - lat * st * 0.28)
            py = max(0.0, vy + abs(s_w) * v_crease * 0.58)
            verts.append((px, py, pz))
            uvs.append((j * 0.5, u * 2.4))
            rib_shade = 0.82 if j == 1 else 1.02
            cols.append((min(1.0, r_c * rib_shade), min(1.0, g_c * rib_shade), min(1.0, b_c * rib_shade)))

    for i in range(segs):
        r0 = i * 3
        r1 = (i + 1) * 3
        tris.append((r0 + 0, r0 + 1, r1 + 1))
        tris.append((r0 + 0, r1 + 1, r1 + 0))
        tris.append((r0 + 1, r0 + 2, r1 + 2))
        tris.append((r0 + 1, r1 + 2, r1 + 1))

    pos = np.asarray(verts, dtype=np.float32)
    idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
    nrm = recompute_smooth_normals(pos, idx)
    part = MeshPart(name, "item_grass_blade", pos, nrm, idx, uvs=np.asarray(uvs, dtype=np.float32))
    col_arr = np.clip(np.asarray(cols, dtype=np.float32), 0.08, 1.0)
    part.colors = col_arr
    part._base_tint = col_arr.copy()
    return part


def _build_grass_cluster(
    name: str,
    *,
    n_blades: int,
    spread_x: float,
    spread_z: float,
    h_min: float,
    h_max: float,
    width_scale: float = 1.0,
    lean_bias_x: float = 0.0,
    n_culms: int = 4,
    seed: int = 11,
) -> SceneBuilder:
    """Build a lush, dense, 3-tiered multi-culm 3D grass tuft rooted directly on Y = 0.0 (zero bare gaps)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    rng = np.random.default_rng(seed)

    # Group blades into natural botanical root culms/rosettes so tufts read rich, clustered & organic!
    culms: List[Tuple[float, float, float]] = [(0.0, 0.0, 1.0)]
    for c in range(1, max(1, n_culms)):
        ca = (2.0 * math.pi * c / float(max(1, n_culms - 1))) + float(rng.uniform(-0.35, 0.35))
        cr = float(rng.uniform(0.32, 0.72))
        culms.append((spread_x * cr * math.cos(ca), spread_z * cr * math.sin(ca), float(rng.uniform(0.84, 1.04))))

    for k in range(n_blades):
        cx0, cz0, ch_scale = culms[k % len(culms)]
        # 3 upright botanical canopy tiers matching 1998 lush meadow grass:
        # 0 = dense thick upright basal sheath (40%), 1 = tall lush full mid-canopy (42%), 2 = tall arching outer spires (18%)
        tier_u = (k / float(max(1, n_blades)))
        if tier_u < 0.40:
            tier = 0
            rad_local = math.sqrt(float(rng.uniform(0.01, 0.85))) * 0.44
            h_mult = float(rng.uniform(0.54, 0.82))
            w_mult = float(rng.uniform(1.18, 1.46))
            lean_m, droop_m = 0.58, 0.55
            shade_m = float(rng.uniform(0.74, 0.92))
        elif tier_u < 0.82:
            tier = 1
            rad_local = math.sqrt(float(rng.uniform(0.01, 0.95))) * 0.48
            h_mult = float(rng.uniform(0.78, 1.06))
            w_mult = float(rng.uniform(1.04, 1.30))
            lean_m, droop_m = 0.72, 0.65
            shade_m = float(rng.uniform(0.88, 1.05))
        else:
            tier = 2
            rad_local = math.sqrt(float(rng.uniform(0.02, 1.0))) * 0.52
            h_mult = float(rng.uniform(1.02, 1.28))
            w_mult = float(rng.uniform(0.86, 1.08))
            lean_m, droop_m = 1.05, 0.95
            shade_m = float(rng.uniform(0.96, 1.12))

        ang = float(rng.uniform(0.0, 2.0 * math.pi))
        bx = cx0 + spread_x * rad_local * math.cos(ang)
        bz = cz0 + spread_z * rad_local * math.sin(ang)
        rad_global = min(1.0, math.hypot(bx / max(1e-4, spread_x), bz / max(1e-4, spread_z)))
        height = float(rng.uniform(h_min, h_max)) * h_mult * ch_scale * (1.08 - 0.14 * rad_global)
        width = float(rng.uniform(0.080, 0.118)) * width_scale * w_mult
        if abs(lean_bias_x) > 0.02:
            # Upright wind-swept tuft (`grass1`/`grass2`): rises vertically then bends gracefully left!
            base_yaw = math.atan2(lean_bias_x + 0.35 * bx, 0.32 * bz + 0.05)
            out_yaw = base_yaw + float(rng.uniform(-0.72, 0.72))
        else:
            out_yaw = math.atan2(bx - cx0 * 0.25, (bz - cz0 * 0.25) + 1e-4) + float(rng.uniform(-0.68, 0.68))
        # Upright core blades stand tall & dense; outer rim blades sweep & arch gracefully outward like 1998 meadow tufts!
        rim_boost = 1.95 if rad_global > 0.56 else 1.0
        outer_flare = (0.18 + 1.05 * (rad_global ** 1.25)) * rim_boost
        lean = float(rng.uniform(0.05, 0.17)) * lean_m * outer_flare
        droop = float(rng.uniform(0.05, 0.20)) * droop_m * outer_flare
        twist = float(rng.uniform(-0.62, 0.62))
        sway = float(rng.uniform(-0.055, 0.055))
        hue = (float(rng.uniform(0.82, 1.16)), float(rng.uniform(0.92, 1.08)), float(rng.uniform(0.74, 1.08)))
        sc.add(
            "grass",
            _curved_grass_blade(
                f"blade_{k}",
                base_xz=(bx, bz),
                height=height,
                width=width,
                yaw=out_yaw,
                lean=lean,
                droop=droop,
                twist=twist,
                sway=sway,
                shade_mult=shade_m,
                hue_shift=hue,
                segs=7 if tier == 0 else 8,
            ),
        )
    return _finalize_item(sc, target_span=0.96)


def build_grass1() -> SceneBuilder:
    """Lush upright wind-swept small backyard grass tuft (`1320:grass1`)."""
    return _build_grass_cluster("item_grass1", n_blades=76, spread_x=0.16, spread_z=0.13, h_min=0.58, h_max=0.92, width_scale=1.12, lean_bias_x=-0.16, n_culms=3, seed=1320)


def build_grass2() -> SceneBuilder:
    """Upright wind-swept small backyard grass tuft variant (`1321:grass2`)."""
    return _build_grass_cluster("item_grass2", n_blades=80, spread_x=0.17, spread_z=0.14, h_min=0.56, h_max=0.94, width_scale=1.10, lean_bias_x=-0.18, n_culms=3, seed=1321)


def build_grass3() -> SceneBuilder:
    """Full tall lush backyard meadow grass tuft (`1322:grass3`)."""
    return _build_grass_cluster("item_grass3", n_blades=116, spread_x=0.25, spread_z=0.20, h_min=0.56, h_max=0.90, width_scale=1.08, lean_bias_x=0.0, n_culms=5, seed=1322)


def build_grass4() -> SceneBuilder:
    """Wide-splayed lush tall grass blades fanning outward (`1323:grass4`)."""
    return _build_grass_cluster("item_grass4", n_blades=92, spread_x=0.24, spread_z=0.18, h_min=0.60, h_max=0.96, width_scale=1.12, lean_bias_x=0.0, n_culms=4, seed=1323)


def build_grassbig1() -> SceneBuilder:
    """Dense tall broad wild lawn grass clump with layered multi-culm blades (`1325:grassbig1`)."""
    return _build_grass_cluster("item_grassbig1", n_blades=188, spread_x=0.34, spread_z=0.28, h_min=0.58, h_max=0.96, width_scale=1.06, lean_bias_x=0.0, n_culms=7, seed=1325)


def _build_grass_strip(
    name: str,
    *,
    n_blades: int,
    length: float,
    thickness: float,
    strip_angle_deg: float,
    h_min: float,
    h_max: float,
    curve_amp: float = 0.0,
    seed: int = 1324,
) -> SceneBuilder:
    """Build a lush, thick, upright multi-tiered curving hedge ribbon of overlapping 3D grass culms (`grassmed1..4`, `grassbig2`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    rng = np.random.default_rng(seed)
    rad_ang = math.radians(strip_angle_deg)
    ca, sa = math.cos(rad_ang), math.sin(rad_ang)

    clump_us = [(-0.44 + 0.88 * (c / 6.0)) for c in range(7)]
    for k in range(n_blades):
        cu = clump_us[k % len(clump_us)]
        u = float(np.clip(cu + rng.uniform(-0.13, 0.13), -0.5, 0.5))
        v = float(rng.uniform(-0.5, 0.5))
        along = u * length
        across = v * thickness + curve_amp * math.sin(u * math.pi * 1.6)
        bx = along * ca - across * sa
        bz = along * sa + across * ca
        edge_frac = min(1.0, abs(v) * 2.0)

        tier_u = k / float(max(1, n_blades))
        if tier_u < 0.38:
            h_m, w_m, sh_m = float(rng.uniform(0.54, 0.80)), 1.28, float(rng.uniform(0.74, 0.92))
        elif tier_u < 0.82:
            h_m, w_m, sh_m = float(rng.uniform(0.78, 1.04)), 1.12, float(rng.uniform(0.88, 1.05))
        else:
            h_m, w_m, sh_m = float(rng.uniform(1.02, 1.24)), 0.94, float(rng.uniform(0.96, 1.12))

        height = float(rng.uniform(h_min, h_max)) * h_m * (1.06 - 0.14 * edge_frac)
        width = float(rng.uniform(0.072, 0.106)) * w_m
        out_yaw = math.atan2(-sa * (1.0 if v >= 0 else -1.0), ca * (1.0 if v >= 0 else -1.0)) + float(rng.uniform(-1.0, 1.0))
        outer_flare = 0.22 + 0.85 * (edge_frac ** 1.2)
        lean = float(rng.uniform(0.04, 0.14)) * outer_flare
        droop = float(rng.uniform(0.04, 0.15)) * outer_flare
        twist = float(rng.uniform(-0.58, 0.58))
        sway = float(rng.uniform(-0.048, 0.048))
        hue = (float(rng.uniform(0.82, 1.16)), float(rng.uniform(0.92, 1.08)), float(rng.uniform(0.74, 1.08)))
        sc.add(
            "grass",
            _curved_grass_blade(
                f"blade_{k}",
                base_xz=(bx, bz),
                height=height,
                width=width,
                yaw=out_yaw,
                lean=lean,
                droop=droop,
                twist=twist,
                sway=sway,
                shade_mult=sh_m,
                hue_shift=hue,
                segs=7,
            ),
        )
    return _finalize_item(sc, target_span=0.96)


def build_grassmed1() -> SceneBuilder:
    """Horizontal lush meadow grass hedge strip (`1324:grassmed1`)."""
    return _build_grass_strip("item_grassmed1", n_blades=196, length=0.92, thickness=0.22, strip_angle_deg=0.0, h_min=0.42, h_max=0.68, seed=1324)


def build_grassmed2() -> SceneBuilder:
    """Vertical narrow lush meadow grass row (`1326:grassmed2`)."""
    return _build_grass_strip("item_grassmed2", n_blades=200, length=1.08, thickness=0.20, strip_angle_deg=90.0, h_min=0.44, h_max=0.72, seed=1326)


def build_grassmed3() -> SceneBuilder:
    """Diagonal SW-to-NE lush grass hedge ribbon (`1327:grassmed3`)."""
    return _build_grass_strip("item_grassmed3", n_blades=204, length=1.00, thickness=0.24, strip_angle_deg=-42.0, h_min=0.44, h_max=0.72, curve_amp=0.07, seed=1327)


def build_grassbig2() -> SceneBuilder:
    """Curving wide NW-to-SE lush wild grass hedge (`1328:grassbig2`)."""
    return _build_grass_strip("item_grassbig2", n_blades=230, length=1.04, thickness=0.28, strip_angle_deg=+36.0, h_min=0.46, h_max=0.76, curve_amp=0.09, seed=1328)


def build_grassmed4() -> SceneBuilder:
    """Diagonal NW-to-SE lush meadow grass hedge ribbon (`1329:grassmed4`)."""
    return _build_grass_strip("item_grassmed4", n_blades=204, length=1.00, thickness=0.24, strip_angle_deg=+48.0, h_min=0.44, h_max=0.72, curve_amp=-0.06, seed=1329)


def _heart_leaflet(name: str, *, scale: float = 1.0) -> MeshPart:
    """One plump rounded heart-shaped clover leaflet with smooth twin cheeks, apical notch & sunlit V-chevron (`404..408`)."""
    def _heart(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        s = np.clip(p[:, 2] / 0.14, -1.0, 1.0)
        u = 0.5 * (s + 1.0)  # 0 at narrow stem petiole -> 1 at outer heart tip
        raw_env = np.sqrt(np.maximum(0.06, 1.0 - s * s))
        # Plump rounded clover heart silhouette: slender stem junction -> full round twin cheeks -> crisp heart notch!
        base_taper = np.clip(u / 0.50, 0.0, 1.0) ** 0.68
        lobe_round = np.sqrt(np.maximum(0.0, 1.0 - np.clip((u - 0.66) / 0.34, 0.0, 1.0) ** 2.0))
        desired_w = np.where(u >= 0.998, 0.0, np.maximum(0.04, (0.12 + 0.98 * base_taper) * lobe_round))
        p[:, 0] *= np.clip(desired_w / raw_env, 0.0, 2.8) * 1.00 * scale
        p[:, 2] = (0.014 + u * 0.272) * scale
        # Crisp botanical heart cleavage notch separating the twin rounded outer cheeks (`strip_clovers.png`)!
        notch = 0.072 * scale * np.exp(-((p[:, 0] / (0.040 * scale)) ** 2)) * (u ** 2.1)
        p[:, 2] -= notch
        # Natural botanical V-fold along midrib + gentle downward curl at tips
        p[:, 1] += 0.020 * scale * np.abs(p[:, 0]) / max(1e-4, 0.12 * scale) - 0.018 * scale * (u ** 2.0)
        return p

    leaf = ellipsoid(0.124, 0.008, 0.14, u_segs=30, v_segs=14, name=name, material="item_leaf_rich").deform(_heart)

    def _clover_chevron(pos: np.ndarray, _nrm: np.ndarray) -> np.ndarray:
        r = np.hypot(pos[:, 0] * 1.05, pos[:, 2] - 0.03 * scale)
        # Signature bright sunlit lime-green inner chevron band + deep shaded emerald base & rims (`strip_clovers.png`)!
        band = np.exp(-(((r - 0.148 * scale) / (0.036 * scale)) ** 2))[:, None]
        edge_dark = np.clip(1.0 - 0.34 * (r / (0.30 * scale)), 0.56, 1.0)[:, None]
        base = np.array([[0.64, 0.92, 0.44]], dtype=np.float64) * edge_dark
        chev = np.array([[0.94, 1.00, 0.66]], dtype=np.float64)
        return base * (1.0 - 0.62 * band) + chev * (0.62 * band)

    return tint_part(leaf, _clover_chevron)


def _build_single_clover_parts(
    prefix: str,
    *,
    stem_pts: List[Tuple[float, float, float]],
    leaf_angles_deg: Tuple[float, ...] = (0.0, 90.0, 180.0, 270.0),
    tilt_rx: float = 0.0,
    tilt_rz: float = 0.0,
    leaf_scale: float = 1.00,
) -> List[MeshPart]:
    """Build one clover stem + radiating rounded heart leaflets at exact 1998 angles."""
    parts: List[MeshPart] = []
    stem = curved_tube(
        stem_pts,
        [0.015, 0.013, 0.012, 0.014][: len(stem_pts)],
        samples_per_seg=5,
        radial_segs=10,
        cap_rings=2,
        name=f"{prefix}_stem",
        material="item_stem_green",
    )
    parts.append(stem)
    hx, hy, hz = stem_pts[-1]
    for k, deg in enumerate(leaf_angles_deg):
        ang = math.radians(deg)
        pitch = 0.13 + 0.04 * (k % 2)
        lf = (
            _heart_leaflet(f"{prefix}_leaf_{k}", scale=leaf_scale - 0.03 * (k % 2))
            .rotate_xyz(rx=pitch, ry=ang)
            .rotate_xyz(rx=tilt_rx, rz=tilt_rz)
            .translate(hx, hy + 0.005 * (k % 2), hz)
        )
        parts.append(lf)
    return parts


def build_clover1() -> SceneBuilder:
    """Authentic 3-leaf clover with crooked stem curving down-right (`404:clover1`, `strip_clovers.png` #1)."""
    register_item_materials()
    sc = SceneBuilder(name="item_clover1")
    sc.add(
        "clover",
        _build_single_clover_parts(
            "clover1",
            stem_pts=[(+0.22, 0.00, +0.29), (+0.15, 0.06, +0.20), (+0.08, 0.14, +0.10), (0.00, 0.22, 0.00)],
            leaf_angles_deg=(-148.0, +132.0, -26.0),
            leaf_scale=1.04,
        ),
    )
    return _finalize_item(sc, target_span=0.90)


def build_clover2() -> SceneBuilder:
    """Symmetrical upright 4-leaf clover in classic `+` cross with straight vertical stem (`405:clover2`, `strip_clovers.png` #2)."""
    register_item_materials()
    sc = SceneBuilder(name="item_clover2")
    sc.add(
        "clover",
        _build_single_clover_parts(
            "clover2",
            stem_pts=[(0.00, 0.00, +0.34), (0.00, 0.08, +0.22), (0.00, 0.16, +0.11), (0.00, 0.24, 0.00)],
            leaf_angles_deg=(0.0, 90.0, 180.0, 270.0),
            leaf_scale=1.02,
        ),
    )
    return _finalize_item(sc, target_span=0.90)


def build_clover3() -> SceneBuilder:
    """Authentic 3-leaf clover with long crooked stem sweeping down-left and hooking left (`406:clover3`, `strip_clovers.png` #3)."""
    register_item_materials()
    sc = SceneBuilder(name="item_clover3")
    sc.add(
        "clover",
        _build_single_clover_parts(
            "clover3",
            stem_pts=[(-0.34, 0.00, +0.29), (-0.19, 0.02, +0.27), (-0.09, 0.12, +0.13), (0.00, 0.22, 0.00)],
            leaf_angles_deg=(+164.0, +64.0, -82.0),
            tilt_rz=-0.10,
            leaf_scale=1.04,
        ),
    )
    return _finalize_item(sc, target_span=0.92)


def build_clover4() -> SceneBuilder:
    """Lush broad 3-leaf clover with straight diagonal stem sweeping bottom-left (`407:clover4`, `strip_clovers.png` #4)."""
    register_item_materials()
    sc = SceneBuilder(name="item_clover4")
    sc.add(
        "clover",
        _build_single_clover_parts(
            "clover4",
            stem_pts=[(-0.27, 0.00, +0.26), (-0.17, 0.07, +0.17), (-0.08, 0.15, +0.08), (0.00, 0.23, 0.00)],
            leaf_angles_deg=(-132.0, +124.0, +12.0),
            tilt_rx=0.06,
            leaf_scale=1.10,
        ),
    )
    return _finalize_item(sc, target_span=0.90)


def build_clovers() -> SceneBuilder:
    """Wild patch bouquet of 4 intertwined 3-leaf clovers on arching stems (`408:clovers`, `strip_clovers.png` #5)."""
    register_item_materials()
    sc = SceneBuilder(name="item_clovers")
    bouquet = (
        ("c0", [(-0.02, 0.00, 0.02), (-0.13, 0.12, 0.04), (-0.25, 0.22, 0.06), (-0.33, 0.29, 0.06)], (-145.0, +118.0, -24.0), 0.24, +0.34, 0.78),
        ("c1", [(0.00, 0.00, 0.00), (-0.06, 0.18, -0.01), (-0.12, 0.34, -0.02), (-0.16, 0.43, -0.02)], (-132.0, +124.0, +12.0), 0.20, +0.18, 0.94),
        ("c2", [(0.02, 0.00, 0.00), (+0.03, 0.22, -0.02), (+0.04, 0.42, -0.03), (+0.04, 0.56, -0.03)], (+176.0, +62.0, -64.0), 0.16, -0.04, 0.96),
        ("c3", [(0.03, 0.00, 0.02), (+0.13, 0.15, 0.03), (+0.23, 0.28, 0.05), (+0.30, 0.37, 0.06)], (+152.0, +48.0, -76.0), 0.22, -0.30, 0.92),
    )
    for tag, pts, angs, rx, rz, ls in bouquet:
        sc.add("clovers", _build_single_clover_parts(tag, stem_pts=pts, leaf_angles_deg=angs, tilt_rx=rx, tilt_rz=rz, leaf_scale=ls))
    return _finalize_item(sc, target_span=0.96)


def _sculpt_daisy_petal(
    name: str,
    *,
    length: float,
    half_width: float,
    thickness: float = 0.008,
    arch: float = 0.011,
    droop_tip: float = 0.016,
) -> MeshPart:
    """Build one authentic 1998 Oxeye daisy ray petal: slender strap widening slightly to a broad blunt rounded U-tip."""
    def _strap_shape(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        s = np.clip(p[:, 2] / max(1e-5, 0.5 * length), -1.0, 1.0)
        u = 0.5 * (s + 1.0)  # 0 at receptacle base, 1 at outer petal tip
        # Remove raw ellipsoid narrowing so the sculpted width profile controls the true paddle/strap outline!
        raw_env = np.sqrt(np.maximum(0.05, 1.0 - s * s))
        # Narrow base (0.44) -> gently widening straight strap (0.94..1.08 at u=0.78) -> broad blunt rounded U-tip!
        base_w = 0.44 + 0.52 * np.clip(u / 0.28, 0.0, 1.0) ** 0.72
        paddle_flare = 1.0 + 0.14 * np.clip((u - 0.25) / 0.58, 0.0, 1.0) * (1.0 - 0.45 * np.clip((u - 0.80) / 0.20, 0.0, 1.0))
        tip_t = np.clip((u - 0.82) / 0.18, 0.0, 1.0)
        tip_round = np.sqrt(np.maximum(0.0, 1.0 - tip_t * tip_t))
        desired_env = np.where(u >= 0.998, 0.0, np.maximum(0.02, base_w * paddle_flare * tip_round))
        p[:, 0] *= np.clip(desired_env / raw_env, 0.0, 3.2)
        # Keep uniform crisp thin petal thickness across the full paddle, rounding smoothly at the tip
        p[:, 1] *= np.clip(np.maximum(0.35, tip_round) / np.maximum(0.28, raw_env), 0.45, 1.55)
        p[:, 2] = u * length
        # Subtle botanical arch + gentle tip droop + soft concave midline crease catching realistic studio shading
        p[:, 1] += arch * np.sin(u * math.pi) - droop_tip * (u ** 2.3)
        p[:, 1] -= 0.0042 * np.exp(-((p[:, 0] / max(0.009, half_width * 0.48)) ** 2)) * np.sin(u * math.pi)
        return p

    petal = ellipsoid(half_width, thickness, 0.5 * length, u_segs=26, v_segs=14, name=name, material="item_petal_white").deform(_strap_shape)
    # Assign clean longitudinal petal UVs (`u` across width, `v` from base=0 to tip=1) so daisy veins align down every ray!
    p_loc = petal.positions.astype(np.float64)
    u_tex = np.clip(0.5 + p_loc[:, 0] / max(1e-5, 2.0 * half_width * 1.15), 0.0, 1.0)
    v_tex = np.clip(p_loc[:, 2] / max(1e-5, length), 0.0, 1.0)
    petal.uvs = np.column_stack([u_tex, v_tex]).astype(np.float32)
    return tint_part(
        petal,
        lambda pos, nrm: np.clip(
            (0.84 + 0.16 * np.clip(pos[:, 2] / max(1e-5, length), 0.0, 1.0) + 0.06 * nrm[:, 1])[:, None]
            * np.array([[1.00, 0.99, 0.97]]),
            0.74,
            1.0,
        ),
    )


def _build_swept_leaf_sheet(
    name: str,
    *,
    length: float,
    w_left_fn,
    w_right_fn,
    spine_x_fn=None,
    spine_y_fn=None,
    thickness: float = 0.0085,
    v_cup: float = 0.012,
    lobe_sweep: float = 0.0,
    ripple_amp: float = 0.003,
    ripple_freq: float = 6.0,
    n_u: int = 34,
    n_half: int = 8,
    material: str = "item_leaf_bright",
    color_fn=None,
) -> MeshPart:
    """Build a watertight, outward-wound closed 3D botanical leaf sheet swept along a curved spine."""
    ring_n = 2 * n_half
    alphas = np.linspace(0.0, 2.0 * math.pi, ring_n, endpoint=False)
    cos_a = np.cos(alphas)  # +1 right margin, 0 center, -1 left margin
    sin_a = np.sin(alphas)  # >0 top surface, <0 bottom surface
    abs_s = np.abs(cos_a)
    # Subtle thicker midrib keel on underside + razor-tapered natural margin
    midrib_keel = np.where(sin_a < 0.0, 0.45 * np.exp(-((cos_a / 0.22) ** 2)), 0.0)

    verts: List[Tuple[float, float, float]] = []
    uvs_list: List[Tuple[float, float]] = []
    u_meta: List[float] = []
    s_meta: List[float] = []
    top_meta: List[float] = []

    def _eval_pt(u: float, s: float, sa: float, keel: float) -> Tuple[float, float, float]:
        wl = max(0.0008, float(w_left_fn(u)))
        wr = max(0.0008, float(w_right_fn(u)))
        w_side = wr if s >= 0.0 else wl
        sx = float(spine_x_fn(u)) if spine_x_fn is not None else 0.0
        sy = float(spine_y_fn(u)) if spine_y_fn is not None else 0.0
        lx = sx + s * w_side
        lz = u * length + lobe_sweep * abs(s) * w_side * math.sin(u * math.pi)
        # Organic V-trough cupping along midrib + subtle quilted lateral vein pillowing
        pillowing = ripple_amp * math.sin((u * ripple_freq - abs(s) * 1.15) * math.pi) * math.sin(u * math.pi) * math.sin(abs(s) * math.pi)
        half_t = thickness * (0.45 + 0.55 * math.sin(u * math.pi) ** 0.45) * (0.35 + 0.65 * (1.0 - s * s) ** 0.45)
        ly = sy + v_cup * (abs(s) ** 1.25) * math.sin(u * math.pi) + pillowing + half_t * (sa - keel)
        return (lx, ly, lz)

    # Base pole (u = 0)
    bx0 = float(spine_x_fn(0.0)) if spine_x_fn is not None else 0.0
    by0 = float(spine_y_fn(0.0)) if spine_y_fn is not None else 0.0
    verts.append((bx0, by0, 0.0))
    uvs_list.append((0.5, 0.0))
    u_meta.append(0.0)
    s_meta.append(0.0)
    top_meta.append(0.0)

    # Interior rings (u in (0, 1))
    u_vals = np.linspace(0.0, 1.0, n_u + 1)[1:-1]
    for u_val in u_vals:
        u = float(u_val)
        for j in range(ring_n):
            s = float(cos_a[j])
            sa = float(sin_a[j])
            keel = float(midrib_keel[j])
            verts.append(_eval_pt(u, s, sa, keel))
            uvs_list.append((0.5 + 0.5 * s, u))
            u_meta.append(u)
            s_meta.append(s)
            top_meta.append(sa)

    # Tip pole (u = 1)
    bx1 = float(spine_x_fn(1.0)) if spine_x_fn is not None else 0.0
    by1 = float(spine_y_fn(1.0)) if spine_y_fn is not None else 0.0
    verts.append((bx1, by1, length))
    uvs_list.append((0.5, 1.0))
    u_meta.append(1.0)
    s_meta.append(0.0)
    top_meta.append(0.0)

    tris: List[Tuple[int, int, int]] = []
    n_rings = len(u_vals)
    # Base fan
    for j in range(ring_n):
        j1 = (j + 1) % ring_n
        tris.append((0, 1 + j1, 1 + j))
    # Ring strips
    for i in range(n_rings - 1):
        r0 = 1 + i * ring_n
        r1 = 1 + (i + 1) * ring_n
        for j in range(ring_n):
            j1 = (j + 1) % ring_n
            tris.append((r0 + j, r0 + j1, r1 + j1))
            tris.append((r0 + j, r1 + j1, r1 + j))
    # Tip fan
    tip_idx = len(verts) - 1
    last_r = 1 + (n_rings - 1) * ring_n
    for j in range(ring_n):
        j1 = (j + 1) % ring_n
        tris.append((last_r + j, last_r + j1, tip_idx))

    pos = np.asarray(verts, dtype=np.float32)
    idx = ensure_outward_winding(pos, np.asarray(tris, dtype=np.uint32).reshape(-1))
    nrm = recompute_smooth_normals(pos, idx)
    part = MeshPart(name, material, pos, nrm, idx, uvs=np.asarray(uvs_list, dtype=np.float32))

    if color_fn is not None:
        u_arr = np.asarray(u_meta, dtype=np.float64)
        s_arr = np.asarray(s_meta, dtype=np.float64)
        top_arr = np.asarray(top_meta, dtype=np.float64)
        cols = np.clip(np.asarray(color_fn(pos.astype(np.float64), nrm.astype(np.float64), u_arr, s_arr, top_arr), dtype=np.float32), 0.14, 1.0)
        part.colors = cols
        part._base_tint = cols.copy()
    return part


def _build_daisy_stem_leaf(
    name: str,
    *,
    length: float = 0.215,
    width: float = 0.145,
    side: float = 1.0,
    curve: float = 0.014,
    pitch: float = 0.14,
    yaw_mag: float = 1.38,
    face_tilt: float = 0.72,
    material: str = "item_leaf_bright",
) -> MeshPart:
    """Crisp broad triangular-ovate pointed flower stem leaf tilted to present its full sunlit face (`flower1a..3c`)."""
    hw = width * 0.5

    def _w(u: float) -> float:
        if u <= 0.0 or u >= 1.0:
            return 0.0008
        # Smooth plump rounded ovate belly peaking at u=0.29 tapering to a RAZOR-SHARP botanical point (zero blunt tip!)
        env = (u ** 0.45) * ((1.0 - u) ** 1.08) * 2.55
        serr = 1.0 + 0.036 * math.sin(u * math.pi * 6.0) * math.sin(u * math.pi)
        return hw * max(0.015, env * serr)

    def _color(_pos: np.ndarray, nrm: np.ndarray, u: np.ndarray, s: np.ndarray, top: np.ndarray) -> np.ndarray:
        mid_vein = np.exp(-((s / 0.085) ** 2)) * (1.0 - 0.30 * u)
        lat_vein = np.maximum(0.0, np.cos((u * 5.2 - np.abs(s) * 1.4) * math.pi)) ** 5 * (1.0 - 0.30 * u) * (1.0 - np.abs(s) * 0.35)
        vein = np.clip(mid_vein + 0.58 * lat_vein, 0.0, 1.0)[:, None]
        # Crisp dark-emerald shaded stem base -> bright vivid backlit apple-lime leaf blade!
        base_ao = (0.50 + 0.54 * np.clip(u / 0.28, 0.0, 1.0) + 0.10 * nrm[:, 1])[:, None]
        side_lit = np.where(top >= -0.05, 1.0, 0.66)[:, None]
        base = np.array([[0.36, 0.88, 0.16]], dtype=np.float64) * base_ao * side_lit
        glow = np.array([[0.95, 1.00, 0.40]], dtype=np.float64) * side_lit
        return base * (1.0 - 0.56 * vein) + glow * (0.56 * vein)

    leaf = _build_swept_leaf_sheet(
        name,
        length=length,
        w_left_fn=_w,
        w_right_fn=_w,
        spine_x_fn=lambda u: side * curve * (u ** 1.5),
        spine_y_fn=lambda u: 0.010 * math.sin(u * math.pi) - 0.005 * (u ** 2.0),
        thickness=0.0070,
        v_cup=0.011,
        ripple_amp=0.0020,
        ripple_freq=5.0,
        n_u=24,
        n_half=7,
        material=material,
        color_fn=_color,
    )
    # Positive `side * face_tilt` rolls the +Y upper face toward -side*X, which `ry=side*yaw_mag` turns straight to +Z (camera)!
    return leaf.rotate_xyz(rz=+side * face_tilt).rotate_xyz(rx=pitch, ry=side * yaw_mag)


def _build_daisy_base(
    sc: SceneBuilder,
    *,
    petal_count: int = 12,
    tilt_deg: float = 30.0,
    with_leaves: bool = True,
) -> Tuple[float, float, float]:
    """Build shared 1998 Oxeye daisy stem, compact ovate leaves, calyx cup, and ring of long slender strap petals."""
    stem = curved_tube(
        [(0.0, 0.00, -0.02), (0.008, 0.18, -0.016), (-0.006, 0.36, -0.006), (0.0, 0.54, +0.01)],
        [0.015, 0.013, 0.012, 0.013],
        samples_per_seg=5,
        radial_segs=10,
        cap_rings=2,
        name="daisy_stem",
        material="item_stem_green",
    )
    sc.add("flower", stem)

    if with_leaves:
        # Twin crisp broad triangular-ovate bright-green stem leaves clearly visible below the petals (`flower1a`)!
        sc.add("flower", _build_daisy_stem_leaf("daisy_leaf_l", length=0.195, width=0.132, side=-1.0, curve=0.014, pitch=0.16, yaw_mag=1.40, face_tilt=0.76).translate(0.0, 0.135, -0.014))
        sc.add("flower", _build_daisy_stem_leaf("daisy_leaf_r", length=0.235, width=0.158, side=+1.0, curve=0.018, pitch=0.10, yaw_mag=1.34, face_tilt=0.74).translate(0.0, 0.175, -0.010))

    head_ctr = (0.0, 0.56, +0.015)
    head_parts: List[MeshPart] = []

    calyx = superellipsoid(0.068, 0.038, 0.068, e1=0.80, e2=0.95, u_segs=20, v_segs=10, name="daisy_calyx", material="item_stem_green").translate(0.0, -0.020, 0.0)
    head_parts.append(calyx)

    # 12 long slender separated strap petals with broad rounded blunt tips & crisp airy gaps (`flower1a_f0`, `flower1b_f0`)!
    for k in range(petal_count):
        ang = 2.0 * math.pi * k / petal_count + 0.04 * math.sin(k * 1.9)
        droop = 0.09 + 0.05 * math.sin(k * 2.5)
        plen = 0.275 + 0.016 * math.cos(k * 2.7)
        pet = (
            _sculpt_daisy_petal(f"daisy_petal_{k}", length=plen, half_width=0.031, thickness=0.007, arch=0.011, droop_tip=0.022)
            .rotate_xyz(rx=droop, ry=ang)
            .translate(0.054 * math.sin(ang), 0.003 * (k % 2), 0.054 * math.cos(ang))
        )
        head_parts.append(pet)

    for p in head_parts:
        sc.add("flower", p.rotate_xyz(rx=tilt_deg, degrees=True).translate(*head_ctr))
    return head_ctr


def build_flower1a() -> SceneBuilder:
    """Authentic 1998 Oxeye Daisy with 12 slender strap petals, compact golden pollen disc & ovate stem leaves (`410:flower1a`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_flower1a")
    hx, hy, hz = _build_daisy_base(sc, petal_count=12, tilt_deg=30.0)

    # Compact domed golden-yellow pollen disc with warm amber inner ring (`flower1a_f0`!).
    pollen = tint_part(
        superellipsoid(0.076, 0.038, 0.076, e1=0.78, e2=0.98, u_segs=26, v_segs=14, name="daisy_pollen", material="item_pollen_gold")
        .deform(lambda p: p + 0.0035 * np.sin(p * 78.0) * np.cos(np.roll(p, 1, axis=1) * 72.0))
        .translate(0.0, 0.014, 0.0)
        .rotate_xyz(rx=30.0, degrees=True)
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
    return _finalize_item(sc, target_span=0.94)


def _build_groucho_daisy(name: str, *, animated: bool) -> SceneBuilder:
    """Authentic 1998 Groucho Disguise Daisy (`420:dflower1` flexing-stem head-bobbing anim & `421:flower1` still)."""
    register_item_materials()
    sc = SceneBuilder(name=name)

    # 3-segment hierarchical articulated stem (`bot` -> `mid` -> `top` -> `head` -> `stache`) so the stem flexes organically in S-curves!
    sc.set_node_transform("dflower_stem_bot", translation=(0.0, 0.0, 0.0))
    stem_bot = curved_tube(
        [(0.0, 0.00, -0.020), (-0.014, 0.10, -0.017), (-0.012, 0.22, -0.014)],
        [0.016, 0.015, 0.014],
        samples_per_seg=4,
        radial_segs=12,
        cap_rings=2,
        name="dflower_stem_bot_mesh",
        material="item_stem_green",
    )
    sc.add("dflower_stem_bot", stem_bot)
    sc.add("dflower_stem_bot", _build_daisy_stem_leaf("dflower_leaf_l", length=0.195, width=0.132, side=-1.0, curve=0.014, pitch=0.22, yaw_mag=1.38, face_tilt=0.80).translate(-0.010, 0.135, -0.015))

    sc.set_node_parent("dflower_stem_mid", "dflower_stem_bot")
    sc.set_node_transform("dflower_stem_mid", translation=(-0.012, 0.21, -0.014))
    stem_mid = curved_tube(
        [(0.0, -0.012, 0.0), (+0.022, 0.10, +0.004), (+0.016, 0.22, +0.010)],
        [0.0145, 0.0135, 0.0135],
        samples_per_seg=4,
        radial_segs=12,
        cap_rings=2,
        name="dflower_stem_mid_mesh",
        material="item_stem_green",
    )
    sc.add("dflower_stem_mid", stem_mid)
    sc.add("dflower_stem_mid", _build_daisy_stem_leaf("dflower_leaf_r", length=0.225, width=0.148, side=+1.0, curve=-0.016, pitch=-0.08, yaw_mag=1.34, face_tilt=0.76).translate(+0.008, 0.012, +0.002))

    sc.set_node_parent("dflower_stem_top", "dflower_stem_mid")
    sc.set_node_transform("dflower_stem_top", translation=(+0.016, 0.21, +0.010))
    stem_top = curved_tube(
        [(0.0, -0.012, 0.0), (-0.006, 0.11, +0.008), (-0.004, 0.22, +0.020)],
        [0.0135, 0.013, 0.014],
        samples_per_seg=4,
        radial_segs=12,
        cap_rings=2,
        name="dflower_stem_top_mesh",
        material="item_stem_green",
    )
    sc.add("dflower_stem_top", stem_top)

    # Pivoted flower bloom + Groucho disguise node (`dflower_head` parented cleanly to `dflower_stem_top`!)
    sc.set_node_parent("dflower_head", "dflower_stem_top")
    sc.set_node_transform("dflower_head", translation=(-0.004, 0.23, +0.022), rotation=quat_from_euler_xyz(rx=68.0, degrees=True))

    head_parts: List[MeshPart] = []
    calyx = superellipsoid(0.068, 0.036, 0.068, e1=0.80, e2=0.95, u_segs=20, v_segs=10, name="dflower_calyx", material="item_stem_green").translate(0.0, -0.020, 0.0)
    head_parts.append(calyx)

    # EXACTLY 8 iconic broad-tipped slender straight-sided white daisy petals spaced at 45 deg (`12:00..10:30`) with wide open gaps!
    for k in range(8):
        ang = 2.0 * math.pi * k / 8.0
        droop = 0.04 + 0.03 * math.sin(k * 2.1)
        plen = 0.275 + 0.010 * math.cos(k * 2.0)
        pet = (
            _sculpt_daisy_petal(f"dflower_petal_{k}", length=plen, half_width=0.036, thickness=0.0085, arch=0.009, droop_tip=0.012)
            .rotate_xyz(rx=droop, ry=ang)
            .translate(0.050 * math.sin(ang), 0.002 * (k % 2), 0.050 * math.cos(ang))
        )
        head_parts.append(pet)

    # Compact warm golden-ochre flower center tucked tightly behind the Groucho disguise.
    disc = superellipsoid(0.076, 0.034, 0.076, e1=0.80, e2=0.96, u_segs=22, v_segs=12, name="dflower_center", material="item_pollen_gold").translate(0.0, 0.008, 0.0)
    head_parts.append(disc)

    # Sleek glossy black Wayfarer Groucho sunglasses with temple arms routed CLEANLY BETWEEN the upper-side petals (`1:30`/`3:00` & `10:30`/`9:00`)!
    for side, tag in ((+1.0, "r"), (-1.0, "l")):
        frame = superellipsoid(0.062, 0.016, 0.048, e1=0.45, e2=0.55, u_segs=20, v_segs=10, name=f"groucho_frame_{tag}", material="gear_dark").rotate_xyz(ry=-side * 0.14).translate(side * 0.066, 0.048, -0.034)
        lens = superellipsoid(0.053, 0.015, 0.040, e1=0.50, e2=0.55, u_segs=18, v_segs=8, name=f"groucho_lens_{tag}", material="eye_pupil").rotate_xyz(ry=-side * 0.14).translate(side * 0.066, 0.056, -0.034)
        glint = ellipsoid(0.020, 0.005, 0.009, u_segs=10, v_segs=6, name=f"groucho_glint_{tag}", material="eye_catchlight").rotate_xyz(ry=0.48).translate(side * 0.052, 0.070, -0.044)
        # Sleek temple arm runs from outer frame hinge BACKWARD (`-Y`) cleanly through the open gap at `+-112.5 deg` between the petals & hooks behind calyx!
        temple = curved_tube(
            [
                (side * 0.118, +0.046, -0.050),
                (side * 0.140, +0.020, -0.058),
                (side * 0.142, -0.016, -0.059),
                (side * 0.120, -0.048, -0.050),
                (side * 0.088, -0.060, -0.038),
            ],
            [0.0085, 0.0080, 0.0075, 0.0070, 0.0055],
            samples_per_seg=4,
            radial_segs=10,
            cap_rings=2,
            name=f"groucho_arm_{tag}",
            material="gear_dark",
        )
        head_parts.extend([frame, lens, glint, temple])
    bridge = rounded_box(0.050, 0.018, 0.020, bevel=0.005, segs=2, name="groucho_bridge", material="gear_dark").translate(0.0, 0.056, -0.042)
    head_parts.append(bridge)

    # Sculpted bulbous peach-pink Groucho nose with flared nostrils (`strip_groucho.png`)!
    nose = (
        superellipsoid(0.040, 0.054, 0.062, e1=0.74, e2=0.80, u_segs=22, v_segs=14, name="groucho_nose", material="item_groucho_nose")
        .rotate_xyz(rx=0.22)
        .translate(0.0, 0.072, +0.016)
    )
    head_parts.append(nose)
    for side, tag in ((+1.0, "r"), (-1.0, "l")):
        nostril = ellipsoid(0.020, 0.016, 0.018, u_segs=12, v_segs=8, name=f"groucho_nostril_{tag}", material="item_groucho_nose").translate(side * 0.028, 0.054, +0.040)
        head_parts.append(nostril)
    sc.add("dflower_head", head_parts)

    # Drooping bristly broom-style black Groucho mustache mounted on a child node!
    sc.set_node_parent("dflower_stache", "dflower_head")
    sc.set_node_transform("dflower_stache", translation=(0.0, 0.046, +0.058))
    stache_parts: List[MeshPart] = []
    for k in range(-4, 5):
        u = k / 4.0
        tx = u * 0.072
        tz = 0.008 + 0.024 * abs(u)
        ty = -0.014 * (u * u)
        tuft = (
            ellipsoid(0.014, 0.013, 0.052 - 0.009 * abs(u), u_segs=10, v_segs=8, name=f"groucho_stache_{k + 4}", material="item_dark_stitch")
            .rotate_xyz(rx=0.18, ry=u * 0.24)
            .translate(tx, ty, tz + 0.028)
        )
        stache_parts.append(tuft)
    sc.add("dflower_stache", stache_parts)

    if animated:
        # Exact 0.70s 1998 `420:dflower1` smooth rhythmic head-bobbing + graceful flexing stem (7 frames x 100ms!).
        # Matched directly to `strip_dflower1.png`: steady grounded lower leaves, smooth organic stem sway,
        # gentle left tilt (f0-f1) -> soft center glide (f2) -> slight lift (f3) -> signature cheerful right head-bob (f4) -> rebound (f5-f6).
        times = [0.00, 0.10, 0.20, 0.30, 0.40, 0.50, 0.60, 0.70]
        bot_rot = [
            (+0.5, 0.0, +2.5),    # f0: grounded steady base, slight left lean
            (+1.0, 0.0, +1.5),    # f1: smooth glide toward center
            (+1.5, 0.0, +0.0),    # f2: center
            (+0.5, 0.0, -1.5),    # f3: lifting into right lean
            (+2.5, 0.0, -4.5),    # f4: smooth right bow on bob beat
            (+1.0, 0.0, -1.5),    # f5: smooth elastic rebound
            (+0.5, 0.0, +1.2),    # f6: returning left
            (+0.5, 0.0, +2.5),    # seamless 700ms loop
        ]
        mid_rot = [
            (+1.0, 0.0, +4.0),    # f0: smooth continuous arc (same direction as base, zero robotic kink!)
            (+1.5, 0.0, +2.2),    # f1
            (+2.5, 0.0, -0.5),    # f2
            (+0.5, 0.0, -2.8),    # f3
            (+4.5, 0.0, -7.5),    # f4: flexible stem bows smoothly right
            (+1.5, 0.0, -2.5),    # f5
            (+1.0, 0.0, +2.0),    # f6
            (+1.0, 0.0, +4.0),
        ]
        top_rot = [
            (-1.0, 0.0, +2.5),    # f0: gentle neck follow-through
            (+0.5, 0.0, +1.2),    # f1
            (+1.5, 0.0, -0.5),    # f2
            (-1.5, 0.0, -1.8),    # f3: slight buoyant lift
            (+4.0, 0.0, -5.5),    # f4: neck nods smoothly down-right
            (-0.5, 0.0, -1.5),    # f5
            (-0.5, 0.0, +1.2),    # f6
            (-1.0, 0.0, +2.5),
        ]
        head_poses = [
            (66.0, -5.0, +6.5),   # f0: gentle cheeky left tilt
            (67.5, -2.5, +3.8),   # f1: gliding smoothly right
            (69.5, +0.0, +0.5),   # f2: soft center bob
            (64.5, +3.5, -2.5),   # f3: upright anticipation lift
            (74.5, +8.5, -9.5),   # f4: iconic playful right-down head bob (`strip_dflower1.png` f4!)
            (65.5, +3.0, -2.5),   # f5: buoyant rebound up-center
            (67.0, -2.5, +3.5),   # f6: gliding back left
            (66.0, -5.0, +6.5),   # seamless loop
        ]
        # Subtle rhythmic vertical head-bob translation matching the 10px vertical dip on f4!
        head_trans = [
            (-0.004, 0.230, +0.022),
            (-0.004, 0.227, +0.022),
            (-0.004, 0.224, +0.023),
            (-0.004, 0.234, +0.021),
            (-0.004, 0.208, +0.028),
            (-0.004, 0.232, +0.022),
            (-0.004, 0.228, +0.022),
            (-0.004, 0.230, +0.022),
        ]
        stache_twitches = [
            (-1.0, +1.0, +1.0),
            (+0.5, +0.0, +0.0),
            (+1.5, +0.0, +0.5),
            (-1.5, -0.5, -0.5),
            (+3.0, -2.0, -2.0),
            (-1.0, +0.5, +0.5),
            (+0.0, +0.5, +0.5),
            (-1.0, +1.0, +1.0),
        ]
        sc.add_animation(
            "Idle",
            [
                AnimationChannel("dflower_stem_bot", "rotation", times, [quat_from_euler_xyz(rx=rx, ry=ry, rz=rz, degrees=True) for rx, ry, rz in bot_rot]),
                AnimationChannel("dflower_stem_mid", "rotation", times, [quat_from_euler_xyz(rx=rx, ry=ry, rz=rz, degrees=True) for rx, ry, rz in mid_rot]),
                AnimationChannel("dflower_stem_top", "rotation", times, [quat_from_euler_xyz(rx=rx, ry=ry, rz=rz, degrees=True) for rx, ry, rz in top_rot]),
                AnimationChannel("dflower_head", "rotation", times, [quat_from_euler_xyz(rx=rx, ry=ry, rz=rz, degrees=True) for rx, ry, rz in head_poses]),
                AnimationChannel("dflower_head", "translation", times, head_trans),
                AnimationChannel("dflower_stache", "rotation", times, [quat_from_euler_xyz(rx=rx, ry=ry, rz=rz, degrees=True) for rx, ry, rz in stache_twitches]),
            ],
            replace=True,
        )
    return _finalize_item(sc, target_span=0.94)


def build_dflower1() -> SceneBuilder:
    """Animated 1998 Groucho-Marx Disguise Daisy looking suspiciously left & right (700 ms Idle clip, `420:dflower1`)."""
    return _build_groucho_daisy("item_dflower1", animated=True)


def build_mush2a() -> SceneBuilder:
    """Organic spotted woodland mushroom with mottled orange/grey cap, glowing mint-green spots & gills (`329:mush2a`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_mush2a")

    # Plump curved fibrous grey-tan mushroom stalk with spotted base matching `329:mush2a`!
    stalk = gradient_y_tint(
        curved_tube(
            [(+0.11, 0.02, +0.04), (+0.13, 0.14, +0.02), (+0.05, 0.27, +0.00), (-0.02, 0.39, -0.02)],
            [(0.102, 0.096), (0.078, 0.076), (0.070, 0.068), (0.082, 0.080)],
            samples_per_seg=5,
            radial_segs=22,
            cap_rings=4,
            name="mush_stalk",
            material="item_mush_stem",
        ).deform(lambda p: np.column_stack([p[:, 0], np.maximum(p[:, 1], 0.0), p[:, 2]])),
        (0.76, 0.78, 0.72),
        (1.00, 0.98, 0.94),
    )
    sc.add("mushroom", stalk)
    # Iconic bright mint-green spots right on the curving stalk surface (`329:mush2a`)!
    for idx, (sx, sy, sz, sr) in enumerate(((+0.13, 0.13, +0.105, 0.030), (+0.18, 0.08, +0.065, 0.024), (+0.05, 0.22, +0.070, 0.022))):
        sc.add("mushroom", ellipsoid(sr, sr * 0.90, sr * 0.52, u_segs=14, v_segs=10, name=f"mush_stalk_spot_{idx}", material="item_mush_spot").translate(sx, sy, sz))

    # Build the cap, underside gills, AND raised orange + mint-jade spots in LOCAL cap space BEFORE tilting!
    cap_parts: List[MeshPart] = []

    def _mush_cap(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        r = np.hypot(p[:, 0], p[:, 2])
        ang = np.arctan2(p[:, 2], p[:, 0])
        rim_wave = 0.024 * np.sin(ang * 4.0 + 0.6) * np.clip(r / 0.28, 0.0, 1.0) ** 1.5
        p[:, 1] = np.where(p[:, 1] < 0.0, p[:, 1] * 0.22, p[:, 1]) + rim_wave
        p[:, 0] *= 1.0 + 0.07 * np.cos(ang * 3.0)
        return p

    def _mush_mottle(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
        # Weathered slate-grey mushroom skin mottled organically with warm burnt-orange patches (`329:mush2a`)!
        patch = (
            np.sin(pos[:, 0] * 11.0 + 0.4) * np.cos(pos[:, 2] * 10.0 - 0.5)
            + 0.45 * np.sin(pos[:, 1] * 16.0 + pos[:, 0] * 14.0)
        )
        org_w = np.clip((patch + 0.10) * 2.6, 0.0, 1.0)[:, None] * (nrm[:, 1:2] > -0.1)
        grey_tan = np.array([[0.74, 0.74, 0.72]], dtype=np.float64)
        rust_org = np.array([[0.98, 0.52, 0.20]], dtype=np.float64)
        return np.clip(grey_tan * (1.0 - org_w) + rust_org * org_w, 0.2, 1.0)

    cap = tint_part(
        superellipsoid(0.32, 0.22, 0.30, e1=0.84, e2=0.92, u_segs=36, v_segs=22, name="mush_cap", material="item_mush_cap").deform(_mush_cap),
        _mush_mottle,
    )
    cap_parts.append(cap)

    # Radial pale gills underneath the cap skirt.
    gills = cylinder(0.28, 0.26, 0.025, radial_segs=32, axis="y", name="mush_gills", material="item_mush_stem").translate(0.0, -0.028, 0.0)
    cap_parts.append(gills)

    # Proud raised bright mint-green spots seated flush on the LOCAL cap ellipsoid dome (impossible to bury!)
    # Each tuple: (theta_rad, phi_from_top_rad, spot_radius, orange_halo)
    local_spots = (
        (0.45, 0.36, 0.068, True),
        (1.45, 0.72, 0.062, True),
        (2.45, 0.62, 0.064, True),
        (3.55, 0.68, 0.058, True),
        (4.75, 0.78, 0.056, True),
        (5.65, 0.66, 0.060, True),
        (0.95, 1.04, 0.050, False),
        (2.05, 1.08, 0.048, False),
        (3.95, 1.06, 0.048, False),
    )
    rx_cap, ry_cap, rz_cap = 0.32, 0.22, 0.30
    for idx, (th, phi, sr, has_halo) in enumerate(local_spots):
        nx = math.sin(phi) * math.cos(th)
        ny = math.cos(phi)
        nz = math.sin(phi) * math.sin(th)
        sx = rx_cap * nx * 0.99
        sy = ry_cap * ny * 0.99
        sz = rz_cap * nz * 0.99
        # Tilt spot flat against surface normal
        tilt_x = math.atan2(nz, max(0.15, ny))
        tilt_z = -math.atan2(nx, max(0.15, ny))
        if has_halo:
            halo = (
                ellipsoid(sr * 1.28, sr * 0.32, sr * 1.24, u_segs=16, v_segs=8, name=f"mush_halo_{idx}", material="item_mush_orange")
                .rotate_xyz(rx=tilt_x, rz=tilt_z)
                .translate(sx * 0.985, sy * 0.985, sz * 0.985)
            )
            cap_parts.append(halo)
        spot = (
            ellipsoid(sr, sr * 0.38, sr * 0.94, u_segs=16, v_segs=8, name=f"mush_spot_{idx}", material="item_mush_spot")
            .rotate_xyz(rx=tilt_x, rz=tilt_z)
            .translate(sx * 1.015, sy * 1.015, sz * 1.015)
        )
        cap_parts.append(spot)

    # Tilt the whole cap assembly boldly forward-left just like `329:mush2a`!
    for p in cap_parts:
        sc.add("mushroom", p.rotate_xyz(rx=22.0, ry=-12.0, rz=26.0, degrees=True).translate(-0.04, 0.39, +0.02))
    return _finalize_item(sc, target_span=0.90)
