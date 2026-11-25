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
    """One crisp separated heart-shaped clover leaflet with deep apical notch & pale V-chevron marking."""
    def _heart(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        # Map ellipsoid into a clean narrow-stemmed, wide-lobed heart silhouette with crisp gap at base!
        u = np.clip((p[:, 2] + 0.14) / 0.28, 0.0, 1.0)
        # Taper sharply to petiole point near u=0, puff into plump rounded twin heart lobes around u=0.72!
        flare = (0.14 + 1.18 * (u ** 0.68)) * (1.02 - 0.14 * (u ** 3.2))
        p[:, 0] *= flare * 1.06 * scale
        p[:, 2] = (0.014 + u * 0.275) * scale
        # Crisp heart-tip cleavage notch at u -> 1
        notch = 0.062 * scale * np.exp(-((p[:, 0] / (0.044 * scale)) ** 2)) * (u ** 2.0)
        p[:, 2] -= notch
        # Natural botanical V-fold along midrib + gentle downward curl at tips
        p[:, 1] += 0.022 * scale * np.abs(p[:, 0]) / max(1e-4, 0.11 * scale) - 0.020 * scale * (u ** 2.1)
        return p

    leaf = ellipsoid(0.118, 0.008, 0.14, u_segs=26, v_segs=10, name=name, material="item_leaf_rich").deform(_heart)

    def _clover_chevron(pos: np.ndarray, _nrm: np.ndarray) -> np.ndarray:
        r = np.hypot(pos[:, 0] * 1.05, pos[:, 2] - 0.03 * scale)
        # Signature pale-lime white-clover inner chevron band + deeper emerald veins!
        band = np.exp(-(((r - 0.155 * scale) / (0.028 * scale)) ** 2))[:, None]
        edge_dark = np.clip(1.0 - 0.28 * (r / (0.31 * scale)), 0.66, 1.0)[:, None]
        base = np.array([[0.74, 0.96, 0.54]], dtype=np.float64) * edge_dark
        chev = np.array([[0.98, 1.00, 0.82]], dtype=np.float64)
        return base * (1.0 - 0.55 * band) + chev * (0.55 * band)

    return tint_part(leaf, _clover_chevron)


def _build_single_clover_parts(
    prefix: str,
    *,
    stem_pts: List[Tuple[float, float, float]],
    n_leaves: int = 4,
    yaw_offset: float = 0.35,
    tilt_rx: float = 0.0,
    tilt_rz: float = 0.0,
    leaf_scale: float = 0.98,
) -> List[MeshPart]:
    """Build one clover stem + radiating heart leaflets anchored at the stem's last point."""
    parts: List[MeshPart] = []
    stem = curved_tube(
        stem_pts,
        [0.015, 0.013, 0.012, 0.015][: len(stem_pts)],
        samples_per_seg=4,
        radial_segs=10,
        cap_rings=2,
        name=f"{prefix}_stem",
        material="item_stem_green",
    )
    parts.append(stem)
    hx, hy, hz = stem_pts[-1]
    for k in range(n_leaves):
        ang = k * (2.0 * math.pi / n_leaves) + yaw_offset
        pitch = 0.14 + 0.05 * (k % 2)
        lf = (
            _heart_leaflet(f"{prefix}_leaf_{k}", scale=leaf_scale - 0.05 * (k % 2))
            .rotate_xyz(rx=pitch, ry=ang)
            .rotate_xyz(rx=tilt_rx, rz=tilt_rz)
            .translate(hx, hy + 0.005 * (k % 2), hz)
        )
        parts.append(lf)
    return parts


def build_clover1() -> SceneBuilder:
    """Realistic lucky 4-leaf clover with separated notched heart leaflets & sweeping curved stem (`404:clover1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_clover1")
    sc.add(
        "clover",
        _build_single_clover_parts(
            "clover1",
            stem_pts=[(+0.24, 0.00, +0.26), (+0.15, 0.06, +0.16), (+0.05, 0.15, +0.06), (0.00, 0.23, 0.00)],
            n_leaves=4,
            yaw_offset=0.35,
        ),
    )
    return _finalize_item(sc, target_span=0.90)


def build_clover2() -> SceneBuilder:
    """Symmetrical upright 4-leaf clover with straight vertical stem (`405:clover2`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_clover2")
    sc.add(
        "clover",
        _build_single_clover_parts(
            "clover2",
            stem_pts=[(0.00, 0.00, +0.28), (0.00, 0.08, +0.18), (0.00, 0.16, +0.08), (0.00, 0.24, 0.00)],
            n_leaves=4,
            yaw_offset=0.785,
            leaf_scale=1.02,
        ),
    )
    return _finalize_item(sc, target_span=0.90)


def build_clover3() -> SceneBuilder:
    """Wide tilted 4-leaf clover with long crooked stem sweeping bottom-left (`406:clover3`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_clover3")
    sc.add(
        "clover",
        _build_single_clover_parts(
            "clover3",
            stem_pts=[(-0.30, 0.00, +0.26), (-0.18, 0.05, +0.22), (-0.08, 0.14, +0.10), (0.00, 0.23, 0.00)],
            n_leaves=4,
            yaw_offset=0.20,
            tilt_rz=-0.12,
            leaf_scale=0.98,
        ),
    )
    return _finalize_item(sc, target_span=0.92)


def build_clover4() -> SceneBuilder:
    """Plump lush 4-leaf clover tilted softly with diagonal stem (`407:clover4`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_clover4")
    sc.add(
        "clover",
        _build_single_clover_parts(
            "clover4",
            stem_pts=[(-0.24, 0.00, +0.24), (-0.14, 0.07, +0.15), (-0.05, 0.15, +0.06), (0.00, 0.24, 0.00)],
            n_leaves=4,
            yaw_offset=0.52,
            tilt_rx=0.08,
            leaf_scale=1.04,
        ),
    )
    return _finalize_item(sc, target_span=0.90)


def build_clovers() -> SceneBuilder:
    """Wild patch bouquet of 4 intertwined clovers on sturdy arching stems (`408:clovers`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_clovers")
    bouquet = (
        ("c0", [(-0.02, 0.00, 0.02), (-0.11, 0.14, 0.03), (-0.23, 0.27, 0.05), (-0.31, 0.34, 0.05)], 3, 0.4, 0.22, +0.34, 0.80),
        ("c1", [(0.00, 0.00, 0.00), (-0.05, 0.18, -0.01), (-0.10, 0.34, -0.02), (-0.14, 0.44, -0.02)], 4, 0.6, 0.18, +0.16, 0.86),
        ("c2", [(0.02, 0.00, 0.00), (+0.05, 0.22, -0.02), (+0.07, 0.40, -0.03), (+0.08, 0.52, -0.03)], 4, 0.2, 0.16, -0.08, 0.88),
        ("c3", [(0.03, 0.00, 0.02), (+0.13, 0.15, 0.03), (+0.24, 0.28, 0.05), (+0.32, 0.36, 0.06)], 4, 0.9, 0.22, -0.32, 0.82),
    )
    for tag, pts, nl, yo, rx, rz, ls in bouquet:
        sc.add("clovers", _build_single_clover_parts(tag, stem_pts=pts, n_leaves=nl, yaw_offset=yo, tilt_rx=rx, tilt_rz=rz, leaf_scale=ls))
    return _finalize_item(sc, target_span=0.96)


def _build_daisy_base(sc: SceneBuilder, *, petal_count: int = 16, tilt_deg: float = 28.0) -> Tuple[float, float, float]:
    """Build shared realistic curved daisy stem, stem leaves, calyx cup, and ring of white petals."""
    # Graceful curved botanical stem grounded cleanly on Y = 0.0.
    stem = curved_tube(
        [(0.0, 0.00, -0.04), (0.015, 0.16, -0.03), (-0.01, 0.34, -0.01), (0.0, 0.48, +0.02)],
        [0.022, 0.018, 0.016, 0.018],
        samples_per_seg=5,
        radial_segs=12,
        cap_rings=2,
        name="daisy_stem",
        material="item_stem_green",
    )
    sc.add("flower", stem)

    # Two pointed dark-green botanical stem leaves near base.
    for side, tag, ly in ((+1.0, "l", 0.14), (-1.0, "r", 0.20)):
        lf = (
            ellipsoid(0.055, 0.008, 0.145, u_segs=16, v_segs=8, name=f"daisy_leaf_{tag}", material="item_leaf_rich")
            .deform(lambda p: np.column_stack([p[:, 0] * (1.0 - 0.55 * np.clip((p[:, 2] + 0.14) / 0.28, 0.0, 1.0) ** 1.6), p[:, 1] - 0.025 * ((p[:, 2] + 0.14) / 0.28) ** 2, p[:, 2] + 0.14]))
            .rotate_xyz(rx=0.35, ry=side * 1.15)
            .translate(0.0, ly, -0.02)
        )
        sc.add("flower", lf)

    head_ctr = (0.0, 0.50, +0.02)
    head_parts: List[MeshPart] = []

    # Green calyx bowl under the bloom.
    calyx = superellipsoid(0.088, 0.046, 0.088, e1=0.80, e2=0.95, u_segs=20, v_segs=10, name="daisy_calyx", material="item_stem_green").translate(0.0, -0.024, 0.0)
    head_parts.append(calyx)

    # Ring of gracefully arched, lightly fluted white Oxeye daisy petals!
    for k in range(petal_count):
        ang = 2.0 * math.pi * k / petal_count + (0.05 if k % 2 else -0.04)
        droop = 0.14 + 0.08 * math.sin(k * 2.7)
        plen = 0.22 + 0.018 * math.cos(k * 3.1)

        def _petal_shape(pos: np.ndarray, pl=plen) -> np.ndarray:
            p = pos.copy()
            u = np.clip((p[:, 2] + 0.5 * pl) / pl, 0.0, 1.0)
            p[:, 0] *= 0.45 + 0.65 * np.sin(u * math.pi * 0.85)
            p[:, 2] = (u * pl)
            p[:, 1] += 0.014 * np.sin(u * math.pi) - 0.032 * (u ** 2.2)
            # Subtle central petal groove.
            p[:, 1] -= 0.005 * np.exp(-((p[:, 0] / 0.015) ** 2))
            return p

        pet = tint_part(
            ellipsoid(0.044, 0.007, 0.5 * plen, u_segs=16, v_segs=8, name=f"daisy_petal_{k}", material="item_petal_white")
            .deform(_petal_shape)
            .rotate_xyz(rx=droop, ry=ang)
            .translate(0.075 * math.sin(ang), 0.004 * (k % 2), 0.075 * math.cos(ang)),
            lambda pos, _n: np.clip(0.90 + 0.10 * np.clip(np.hypot(pos[:, 0], pos[:, 2]) / 0.24, 0.0, 1.0)[:, None], 0.86, 1.0) * np.array([[1.0, 0.99, 0.95]]),
        )
        head_parts.append(pet)

    for p in head_parts:
        sc.add("flower", p.rotate_xyz(rx=tilt_deg, degrees=True).translate(*head_ctr))
    return head_ctr


def build_flower1a() -> SceneBuilder:
    """Realistic Oxeye Daisy with bumpy golden pollen disc, curved petals & leafy stem (`410:flower1a`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_flower1a")
    hx, hy, hz = _build_daisy_base(sc, petal_count=17, tilt_deg=32.0)

    # Plump granular golden-yellow pollen center cushion with rich ochre-to-gold dome shading.
    pollen = gradient_y_tint(
        superellipsoid(0.102, 0.052, 0.102, e1=0.78, e2=0.98, u_segs=26, v_segs=14, name="daisy_pollen", material="item_pollen_gold")
        .deform(lambda p: p + 0.005 * np.sin(p * 65.0) * np.cos(np.roll(p, 1, axis=1) * 60.0))
        .translate(0.0, 0.016, 0.0)
        .rotate_xyz(rx=32.0, degrees=True)
        .translate(hx, hy, hz),
        (0.84, 0.64, 0.42),
        (1.04, 1.00, 0.86),
    )
    sc.add("flower", pollen)
    return _finalize_item(sc, target_span=0.94)


def build_dflower1() -> SceneBuilder:
    """Animated 1998 Groucho-Marx Disguise Daisy looking suspiciously left & right (700 ms Idle clip, `420:dflower1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_dflower1")
    sc.set_node_transform("dflower_stem", translation=(0.0, 0.0, 0.0))

    # Taller graceful curvy dark-green botanical stem grounded on Y = 0.0 + twin vibrant leaves clearly visible below petals!
    stem = curved_tube(
        [(0.0, 0.00, -0.02), (-0.020, 0.16, -0.01), (+0.022, 0.36, 0.00), (0.0, 0.56, +0.01)],
        [0.022, 0.019, 0.017, 0.018],
        samples_per_seg=5,
        radial_segs=12,
        cap_rings=2,
        name="dflower_stem_mesh",
        material="item_stem_green",
    )
    sc.add("dflower_stem", stem)
    for side, tag, ly in ((+1.0, "l", 0.16), (-1.0, "r", 0.21)):
        lf = gradient_y_tint(
            ellipsoid(0.062, 0.009, 0.155, u_segs=18, v_segs=8, name=f"dflower_leaf_{tag}", material="item_leaf_bright")
            .deform(lambda p: np.column_stack([
                p[:, 0] * (1.0 - 0.58 * np.clip((p[:, 2] + 0.155) / 0.31, 0.0, 1.0) ** 1.5),
                p[:, 1] - 0.024 * ((p[:, 2] + 0.155) / 0.31) ** 2,
                p[:, 2] + 0.155,
            ]))
            .rotate_xyz(rx=0.34, ry=side * 1.18)
            .translate(0.0, ly, -0.01),
            (0.68, 0.88, 0.56),
            (1.02, 1.04, 0.86),
        )
        sc.add("dflower_stem", lf)

    # Pivoted flower bloom + Groucho disguise node (`dflower_head` parented cleanly to `dflower_stem`!)
    head_pivot = (0.0, 0.58, +0.02)
    sc.set_node_parent("dflower_head", "dflower_stem")
    sc.set_node_transform("dflower_head", translation=head_pivot, rotation=quat_from_euler_xyz(rx=56.0, degrees=True))

    head_parts: List[MeshPart] = []
    # Green sepal calyx cup under the bloom.
    calyx = superellipsoid(0.088, 0.046, 0.088, e1=0.80, e2=0.95, u_segs=20, v_segs=10, name="dflower_calyx", material="item_stem_green").translate(0.0, -0.024, 0.0)
    head_parts.append(calyx)

    # 9 iconic bold crisp white daisy petals radiating cleanly around the Groucho face (`dflower1_strip.png`).
    for k in range(9):
        ang = 2.0 * math.pi * k / 9.0 + 0.10
        droop = 0.08 + 0.05 * math.sin(k * 2.3)
        plen = 0.228 + 0.014 * math.cos(k * 3.1)

        def _petal_shape(pos: np.ndarray, pl=plen) -> np.ndarray:
            p = pos.copy()
            u = np.clip((p[:, 2] + 0.5 * pl) / pl, 0.0, 1.0)
            p[:, 0] *= 0.44 + 0.62 * np.sin(u * math.pi * 0.86)
            p[:, 2] = u * pl
            p[:, 1] += 0.012 * np.sin(u * math.pi) - 0.022 * (u ** 2.2)
            p[:, 1] -= 0.005 * np.exp(-((p[:, 0] / 0.018) ** 2))
            return p

        pet = tint_part(
            ellipsoid(0.048, 0.009, 0.5 * plen, u_segs=18, v_segs=8, name=f"dflower_petal_{k}", material="item_petal_white")
            .deform(_petal_shape)
            .rotate_xyz(rx=droop, ry=ang)
            .translate(0.068 * math.sin(ang), 0.003 * (k % 2), 0.068 * math.cos(ang)),
            lambda pos, _n: np.clip(0.88 + 0.12 * np.clip(np.hypot(pos[:, 0], pos[:, 2]) / 0.24, 0.0, 1.0)[:, None], 0.84, 1.0) * np.array([[1.0, 0.99, 0.96]]),
        )
        head_parts.append(pet)

    # Dark golden-brown flower cushion behind the Groucho disguise.
    disc = superellipsoid(0.098, 0.040, 0.098, e1=0.80, e2=0.96, u_segs=22, v_segs=12, name="dflower_center", material="item_pollen_gold").translate(0.0, 0.010, 0.0)
    head_parts.append(disc)

    # Oversized glossy black wayfarer-style Groucho sunglasses (sleek dark frames + curved glossy lenses + brow rim)!
    for side, tag in ((+1.0, "l"), (-1.0, "r")):
        frame = superellipsoid(0.068, 0.018, 0.056, e1=0.45, e2=0.55, u_segs=20, v_segs=10, name=f"groucho_frame_{tag}", material="gear_dark").rotate_xyz(ry=-side * 0.14).translate(side * 0.072, 0.054, -0.036)
        lens = superellipsoid(0.058, 0.016, 0.046, e1=0.50, e2=0.55, u_segs=18, v_segs=8, name=f"groucho_lens_{tag}", material="eye_pupil").rotate_xyz(ry=-side * 0.14).translate(side * 0.072, 0.062, -0.036)
        glint = ellipsoid(0.022, 0.005, 0.010, u_segs=10, v_segs=6, name=f"groucho_glint_{tag}", material="eye_catchlight").rotate_xyz(ry=0.48).translate(side * 0.056, 0.078, -0.046)
        temple = rounded_box(0.014, 0.016, 0.11, bevel=0.004, segs=2, name=f"groucho_arm_{tag}", material="gear_dark").rotate_xyz(rx=0.40, ry=side * 0.18).translate(side * 0.132, 0.030, -0.065)
        head_parts.extend([frame, lens, glint, temple])
    bridge = rounded_box(0.056, 0.020, 0.022, bevel=0.006, segs=2, name="groucho_bridge", material="gear_dark").translate(0.0, 0.064, -0.046)
    head_parts.append(bridge)

    # Big bulbous peach-pink Groucho nose with sculpted nostril flares (`420:dflower1`)!
    nose = (
        superellipsoid(0.046, 0.062, 0.072, e1=0.74, e2=0.80, u_segs=22, v_segs=14, name="groucho_nose", material="item_groucho_nose")
        .rotate_xyz(rx=0.22)
        .translate(0.0, 0.080, +0.018)
    )
    head_parts.append(nose)
    for side, tag in ((+1.0, "l"), (-1.0, "r")):
        nostril = ellipsoid(0.022, 0.018, 0.020, u_segs=12, v_segs=8, name=f"groucho_nostril_{tag}", material="item_groucho_nose").translate(side * 0.032, 0.062, +0.046)
        head_parts.append(nostril)
    sc.add("dflower_head", head_parts)

    # Drooping bristly broom-style black Groucho mustache mounted on a child node for a cheeky twitch!
    sc.set_node_parent("dflower_stache", "dflower_head")
    sc.set_node_transform("dflower_stache", translation=(0.0, 0.052, +0.066))
    stache_parts: List[MeshPart] = []
    for k in range(-4, 5):
        u = k / 4.0
        tx = u * 0.078
        tz = 0.010 + 0.024 * abs(u)
        ty = -0.016 * (u * u)
        tuft = (
            ellipsoid(0.016, 0.015, 0.056 - 0.010 * abs(u), u_segs=10, v_segs=8, name=f"groucho_stache_{k + 4}", material="item_dark_stitch")
            .rotate_xyz(rx=0.18, ry=u * 0.22)
            .translate(tx, ty, tz + 0.032)
        )
        stache_parts.append(tuft)
    sc.add("dflower_stache", stache_parts)

    # Authentic 700 ms 7-frame 1998 `420:dflower1` suspicious left-to-right look-around animation!
    times = [0.00, 0.10, 0.20, 0.30, 0.40, 0.50, 0.60, 0.70]
    head_poses = [
        (58.0, -28.0, +18.0),  # frame 0 (590): looking far LEFT
        (56.0, -18.0, +12.0),  # frame 1 (594): glancing left
        (54.0, -8.0, +5.0),    # frame 2 (597): turning toward center
        (52.0, 0.0, 0.0),      # frame 3 (600): dead CENTER suspicious stare
        (58.0, +28.0, -18.0),  # frame 4 (601): looking far RIGHT
        (52.0, +4.0, -3.0),    # frame 5 (600): swinging back center
        (55.0, -14.0, +9.0),   # frame 6 (597): swinging back left
        (58.0, -28.0, +18.0),  # seamless loop closure
    ]
    stem_sways = [+5.0, +3.0, +1.5, 0.0, -5.0, -1.0, +2.5, +5.0]
    stache_twitches = [+5.0, -3.0, +3.0, 0.0, -5.0, +3.0, -2.0, +5.0]
    sc.add_animation(
        "Idle",
        [
            AnimationChannel("dflower_head", "rotation", times, [quat_from_euler_xyz(rx=rx, ry=ry, rz=rz, degrees=True) for rx, ry, rz in head_poses]),
            AnimationChannel("dflower_stem", "rotation", times, [quat_from_euler_xyz(rz=a, degrees=True) for a in stem_sways]),
            AnimationChannel("dflower_stache", "rotation", times, [quat_from_euler_xyz(ry=a, rz=a * 0.6, degrees=True) for a in stache_twitches]),
        ],
        replace=True,
    )
    return _finalize_item(sc, target_span=0.92)


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
