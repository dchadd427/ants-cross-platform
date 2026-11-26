"""Realistic 3D sculptors for 1998 Ants Food Wave 4D: Candy, Gummy Worms, Fizzy Cola & Fruit Loops (20 items).

Covers:
- `fdskit1`..`fdskit5` (#267–#271): Glossy lettered 'A-N-T-S' Skittles candy lentil clusters
- `fdgumw1`..`fdgumw4` (#272–#275): Neon rainbow ribbed gummy worm stages
- `fdcola1`..`fdcola4` (#276–#279): Caramel-amber spilled cola puddles (`fdcola1..3` animated fizz!)
- `fdfrl1`..`fdfrl7`   (#280–#286): Individual frosted colorful Fruit Loops cereal rings & broken shards
"""
from __future__ import annotations

import math
from typing import List, Optional, Sequence, Tuple

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
            ellipsoid,
            recompute_smooth_normals,
            sphere,
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
            ellipsoid,
            recompute_smooth_normals,
            sphere,
            torus_arc,
        )


def _const_tint(rgb: Tuple[float, float, float]):
    def _fn(pos: np.ndarray, _n: np.ndarray) -> np.ndarray:
        return np.tile(np.asarray(rgb, dtype=np.float64)[None, :], (len(pos), 1))
    return _fn


# ---------------------------------------------------------------------------
# 1. 'A-N-T-S' LETTERED SKITTLES CANDY LENTILS (`267..271:fdskit1..5`)
# ---------------------------------------------------------------------------

def _skittle_lentil(
    name: str,
    cx: float,
    cz: float,
    *,
    mat: str,
    radius: float = 0.088,
    yaw_deg: float = 0.0,
    tilt_rx: float = 0.0,
    tilt_rz: float = 0.0,
    tint_rgb: Optional[Tuple[float, float, float]] = None,
) -> MeshPart:
    """Glossy oblate sugar-shell candy lentil with crisp planar top UV so white A/N/T/S/C letters read cleanly!"""
    h = radius * 0.50
    lentil = ellipsoid(radius, h, radius, u_segs=22, v_segs=14, name=name, material=mat)
    # Planar top-down UVs in local lentil coords BEFORE rotation/translation so letter stays centered & sharp!
    p_loc = lentil.positions.astype(np.float64)
    u = np.clip(0.5 + p_loc[:, 0] / (2.0 * radius * 0.96), 0.0, 1.0)
    v = np.clip(0.5 + p_loc[:, 2] / (2.0 * radius * 0.96), 0.0, 1.0)
    lentil.uvs = np.column_stack([u, v]).astype(np.float32)
    lentil = lentil.rotate_xyz(rx=tilt_rx, ry=yaw_deg, rz=tilt_rz, degrees=True).translate(cx, h * 0.96, cz)
    if tint_rgb is not None:
        lentil = tint_part(lentil, _const_tint(tint_rgb))
    return lentil


def _build_skittles(name: str, stage: int) -> SceneBuilder:
    """Glossy 'A-N-T-S' lettered Skittles candy lentils matching `#267..271`."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    if stage == 1:
        # 11-candy rich pile (`267:fdskit1`)
        items = (
            (-0.27, -0.05, "item_cheese", 0.072, 12.0, (0.98, 0.88, 0.08)),
            (-0.18, +0.11, "item_skit_a", 0.080, -10.0, None),
            (-0.07, +0.12, "item_skit_n", 0.080, 14.0, None),
            (-0.10, +0.26, "item_skit_c", 0.078, -8.0, None),
            (+0.05, +0.20, "item_skit_s", 0.080, 8.0, None),
            (+0.21, +0.18, "item_skit_n", 0.080, -16.0, None),
            (+0.07, +0.06, "item_skit_t", 0.080, 6.0, None),
            (+0.18, +0.01, "item_skit_s", 0.078, -6.0, None),
            (+0.10, -0.04, "item_skit_a", 0.078, 10.0, None),
            (-0.05, +0.01, "item_cheese", 0.074, -18.0, (0.98, 0.86, 0.10)),
            (-0.12, -0.10, "item_skit_t", 0.076, 22.0, None),
            (+0.00, -0.18, "item_skit_a", 0.076, -12.0, None),
            (+0.08, -0.13, "item_skit_n", 0.074, 15.0, None),
        )
    elif stage == 2:
        # 10-candy cluster with iconic A-N-T-S center (`268:fdskit2`)
        items = (
            (-0.24, +0.08, "item_skit_a", 0.088, -6.0, None),
            (-0.10, +0.08, "item_skit_n", 0.088, 12.0, None),
            (-0.13, +0.26, "item_skit_c", 0.086, -8.0, None),
            (+0.06, +0.18, "item_skit_s", 0.088, 10.0, None),
            (+0.27, +0.17, "item_skit_n", 0.088, -14.0, None),
            (+0.06, +0.01, "item_skit_t", 0.088, 6.0, None),
            (+0.20, -0.07, "item_skit_s", 0.086, -8.0, None),
            (+0.10, -0.12, "item_skit_a", 0.086, 8.0, None),
            (+0.05, -0.22, "item_skit_c", 0.082, 16.0, None),
            (-0.15, -0.21, "item_skit_t", 0.084, -22.0, None),
        )
    elif stage == 3:
        # 7-candy cluster (`269:fdskit3`)
        items = (
            (-0.24, +0.10, "item_skit_a", 0.094, -6.0, None),
            (-0.08, +0.10, "item_skit_n", 0.094, 12.0, None),
            (+0.08, +0.20, "item_skit_s", 0.094, 10.0, None),
            (+0.09, +0.01, "item_skit_t", 0.094, 4.0, None),
            (+0.24, -0.07, "item_skit_s", 0.092, -10.0, None),
            (+0.14, -0.12, "item_skit_a", 0.092, 6.0, None),
            (+0.08, -0.20, "item_skit_c", 0.088, 14.0, None),
        )
    elif stage == 4:
        # Iconic 4-candy diagonal spelling A - N - T - S (`270:fdskit4`)!!
        items = (
            (-0.26, +0.09, "item_skit_a", 0.098, -6.0, None),
            (-0.09, +0.07, "item_skit_n", 0.098, 12.0, None),
            (+0.09, -0.02, "item_skit_t", 0.098, 6.0, None),
            (+0.25, -0.10, "item_skit_s", 0.098, -8.0, None),
        )
    else:
        # Stage 5 (`271:fdskit5`): Single iconic cherry-red 'A' Skittle!
        items = ((0.0, 0.0, "item_skit_a", 0.22, -6.0, None),)

    for k, (cx, cz, mat, r, yaw, tint) in enumerate(items):
        parts.append(_skittle_lentil(f"{name}_c_{k}", cx, cz, mat=mat, radius=r, yaw_deg=yaw, tint_rgb=tint))

    sc.add("skittles", parts)
    return _finalize_item(sc, target_span=0.74 if stage <= 4 else 0.44)


def build_fdskit1() -> SceneBuilder:
    return _build_skittles("item_fdskit1", 1)


def build_fdskit2() -> SceneBuilder:
    return _build_skittles("item_fdskit2", 2)


def build_fdskit3() -> SceneBuilder:
    return _build_skittles("item_fdskit3", 3)


def build_fdskit4() -> SceneBuilder:
    return _build_skittles("item_fdskit4", 4)


def build_fdskit5() -> SceneBuilder:
    return _build_skittles("item_fdskit5", 5)


# ---------------------------------------------------------------------------
# 2. NEON RAINBOW GUMMY WORM (`272..275:fdgumw1..4`)
# ---------------------------------------------------------------------------

def _rainbow_worm_tint(pos: np.ndarray, _n: np.ndarray) -> np.ndarray:
    """Authentic 1998 vivid neon rainbow gummy worm gradient left->right: lime -> red -> yellow -> pink -> orange -> red!"""
    t = np.clip((pos[:, 0] + 0.48) / 0.96, 0.0, 1.0)
    stops = np.array([0.00, 0.14, 0.26, 0.42, 0.58, 0.74, 0.88, 1.00], dtype=np.float64)
    cols = np.array(
        [
            [0.08, 0.88, 0.04],  # vivid neon lime green head (left)
            [0.94, 0.04, 0.04],  # bright cherry red
            [0.98, 0.72, 0.02],  # vivid lemon-gold
            [0.98, 0.06, 0.40],  # electric hot pink
            [0.96, 0.08, 0.22],  # berry crimson-pink
            [0.99, 0.42, 0.02],  # vibrant tangerine orange
            [0.98, 0.74, 0.04],  # sunny neon yellow
            [0.92, 0.04, 0.04],  # crimson tail (right)
        ],
        dtype=np.float64,
    )
    r = np.interp(t, stops, cols[:, 0])
    g = np.interp(t, stops, cols[:, 1])
    b = np.interp(t, stops, cols[:, 2])
    shade = 0.72 + 0.28 * np.clip((pos[:, 1] - 0.01) / 0.07, 0.0, 1.0)
    return np.clip(np.column_stack([r * shade, g * shade, b * shade]), 0.02, 1.0)


def _ribbed_gummy_segment(name: str, pts: Sequence[Tuple[float, float, float]], rads: Sequence[float]) -> MeshPart:
    """Silky-smooth glossy rainbow gummy worm body matching `#272..275` (no jagged accordion ribs!)."""
    seg = curved_tube(list(pts), list(rads), samples_per_seg=10, radial_segs=26, cap_rings=5, name=name, material="item_candy_white")
    p = seg.positions.astype(np.float64)
    # Barely-perceptible organic gelatin pillow softness (smooth sleek tube, zero jagged ridges!)
    p[:, 1] = np.maximum(p[:, 1], 0.003)
    seg.positions = p.astype(np.float32)
    seg.normals = recompute_smooth_normals(seg.positions, seg.indices)
    return tint_part(seg, _rainbow_worm_tint)


def _build_gummy_worm(name: str, stage: int) -> SceneBuilder:
    """Smooth glossy neon rainbow gummy worm through 4 authentic stages (`272..275`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    if stage == 1:
        # Smooth, gently curved horizontal rainbow worm (`272:fdgumw1`)
        pts = [
            (-0.46, 0.048, +0.016),
            (-0.30, 0.049, -0.014),
            (-0.14, 0.049, -0.006),
            (+0.02, 0.049, +0.014),
            (+0.18, 0.049, +0.024),
            (+0.34, 0.049, -0.008),
            (+0.46, 0.047, +0.002),
        ]
        rads = [0.046, 0.047, 0.047, 0.047, 0.047, 0.046, 0.043]
        parts.append(_ribbed_gummy_segment(f"{name}_worm", pts, rads))
    elif stage == 2:
        # Smooth worm with 2 clean bitten notches along the middle (`273:fdgumw2`)
        pts = [
            (-0.46, 0.048, +0.016),
            (-0.30, 0.049, -0.014),
            (-0.16, 0.044, -0.006),
            (-0.02, 0.049, +0.012),
            (+0.14, 0.043, +0.022),
            (+0.30, 0.049, -0.008),
            (+0.45, 0.047, +0.002),
        ]
        rads = [0.046, 0.047, 0.026, 0.047, 0.024, 0.046, 0.042]
        parts.append(_ribbed_gummy_segment(f"{name}_worm", pts, rads))
    elif stage == 3:
        # 4 smooth separated bitten rainbow segments (`274:fdgumw3`)
        segs = (
            ([(-0.45, 0.047, +0.016), (-0.40, 0.048, +0.008), (-0.34, 0.046, -0.004)], [0.044, 0.046, 0.036]),
            ([(-0.24, 0.047, -0.012), (-0.16, 0.048, -0.006), (-0.08, 0.046, +0.006)], [0.038, 0.046, 0.036]),
            ([(+0.04, 0.047, +0.014), (+0.16, 0.048, +0.020), (+0.28, 0.046, +0.002)], [0.036, 0.046, 0.038]),
            ([(+0.37, 0.045, -0.006), (+0.41, 0.046, -0.004), (+0.45, 0.043, +0.000)], [0.034, 0.040, 0.032]),
        )
        for k, (pts, rads) in enumerate(segs):
            parts.append(_ribbed_gummy_segment(f"{name}_seg_{k}", pts, rads))
    else:
        # Stage 4 (`275:fdgumw4`): 3 smooth separated rainbow gummy scraps!
        for k, (cx, cz, rx) in enumerate(((-0.34, +0.014, 0.044), (-0.08, -0.008, 0.056), (+0.24, +0.010, 0.064))):
            bit = ellipsoid(rx, 0.046, 0.040, u_segs=22, v_segs=14, name=f"{name}_bit_{k}", material="item_candy_white").translate(cx, 0.044, cz)
            parts.append(tint_part(bit, _rainbow_worm_tint))

    sc.add("gummy_worm", parts)
    return _finalize_item(sc, target_span=0.96 if stage <= 3 else 0.72)


def build_fdgumw1() -> SceneBuilder:
    return _build_gummy_worm("item_fdgumw1", 1)


def build_fdgumw2() -> SceneBuilder:
    return _build_gummy_worm("item_fdgumw2", 2)


def build_fdgumw3() -> SceneBuilder:
    return _build_gummy_worm("item_fdgumw3", 3)


def build_fdgumw4() -> SceneBuilder:
    return _build_gummy_worm("item_fdgumw4", 4)


# ---------------------------------------------------------------------------
# 3. FIZZY SPILLED COLA PUDDLES (`276..279:fdcola1..4`, `1..3` ANIMATED FIZZ!)
# ---------------------------------------------------------------------------

# Exact 36-angle silhouettes measured from the 1998 sprites `#276..278` (angle 0 = +X, pi/2 = +Z/screen-down).
_COLA1_RADS36: Tuple[float, ...] = (
    0.950, 0.988, 0.925, 0.750, 0.712, 0.662, 0.638, 0.612, 0.575,
    0.550, 0.538, 0.562, 0.650, 0.862, 1.125, 1.188, 1.062, 0.900,
    0.750, 0.700, 0.762, 0.825, 0.812, 0.688, 0.638, 0.625, 0.675,
    0.725, 0.738, 0.775, 0.788, 0.788, 0.825, 0.988, 1.012, 0.950,
)
_COLA2_RADS36: Tuple[float, ...] = (
    0.688, 0.672, 0.672, 0.688, 0.688, 0.640, 0.608, 0.544, 0.464,
    0.400, 0.352, 0.368, 0.592, 0.944, 1.088, 1.088, 1.008, 0.848,
    0.704, 0.640, 0.608, 0.608, 0.656, 0.640, 0.576, 0.368, 0.288,
    0.304, 0.752, 0.784, 0.736, 0.752, 0.912, 1.168, 0.992, 0.736,
)
_COLA3_RADS36: Tuple[float, ...] = (
    0.791, 0.767, 0.744, 0.744, 0.674, 0.581, 0.419, 0.326, 0.279,
    0.256, 0.279, 0.372, 0.698, 0.907, 1.116, 1.163, 1.116, 0.930,
    0.767, 0.674, 0.674, 0.674, 0.628, 0.558, 0.279, 0.279, 0.302,
    0.372, 0.419, 0.488, 0.837, 0.977, 1.070, 1.116, 0.977, 0.860,
)


def _sample_periodic_catmull(samples: Sequence[float], ang: float) -> float:
    """Smooth periodic Catmull-Rom interpolation through N uniformly spaced radial samples."""
    n = len(samples)
    u = ((ang % (2.0 * math.pi)) / (2.0 * math.pi)) * n
    i1 = int(math.floor(u)) % n
    t = u - math.floor(u)
    p0 = samples[(i1 - 1) % n]
    p1 = samples[i1]
    p2 = samples[(i1 + 1) % n]
    p3 = samples[(i1 + 2) % n]
    t2 = t * t
    t3 = t2 * t
    val = 0.5 * (
        (2.0 * p1)
        + (-p0 + p2) * t
        + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2
        + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3
    )
    return max(0.18, val)


def _cola_puddle_mesh(
    name: str,
    rads36: Sequence[float],
    *,
    scale_r: float = 0.44,
    thick: float = 0.145,
    z_stretch: float = 1.18,
) -> MeshPart:
    """Plump 3D caramel-amber cola liquid dome matching exact 1998 sprite silhouette & glossy meniscus (`276..278`)."""
    # Lightly smooth the 36-ray measured profile so the liquid contour has silky surface-tension curvature!
    n0 = len(rads36)
    sm = [
        0.18 * rads36[(i - 1) % n0] + 0.64 * rads36[i] + 0.18 * rads36[(i + 1) % n0]
        for i in range(n0)
    ]
    n_r, n_a = 20, 96
    verts, uvs, cols, tris = [], [], [], []
    # Smooth curved top dome -> rounded meniscus roll -> flat underside seated flush on ground!
    ring_specs = [(ir / float(n_r), True) for ir in range(n_r + 1)] + [(1.0 - ir / float(n_r), False) for ir in range(n_r + 1)]
    for rf, is_top in ring_specs:
        for j in range(n_a + 1):
            ang = 2.0 * math.pi * j / float(n_a)
            rad = _sample_periodic_catmull(sm, ang) * scale_r * rf
            x = rad * math.cos(ang)
            z = rad * math.sin(ang) * z_stretch
            if is_top:
                # Plump pillowy 3D liquid dome with thick rounded meniscus shoulder (never flat!)
                dome = (1.0 - (rf ** 2.6)) ** 0.48 if rf < 0.999 else 0.0
                shoulder = 0.012 * math.exp(-(((rf - 0.80) / 0.12) ** 2))
                y = 0.008 + thick * dome + shoulder
                # Authentic 1998 caramel-cola shading: rich deep amber pool, bright creamy-gold left meniscus gleam,
                # and warm deep reddish-brown surface-tension base rim!
                left_gleam = max(0.0, -math.cos(ang - 0.35)) ** 1.4
                band = math.exp(-(((rf - 0.84) / 0.11) ** 2)) * (0.28 + 0.72 * left_gleam)
                outer_rim = max(0.0, (rf - 0.91) / 0.09)
                r_c = 0.90 + 0.10 * band - 0.16 * outer_rim
                g_c = 0.72 + 0.28 * band - 0.28 * outer_rim
                b_c = 0.24 + 0.56 * band - 0.14 * outer_rim
            else:
                y = 0.006 if rf > 0.95 else 0.001
                r_c, g_c, b_c = 0.62, 0.34, 0.10
            verts.append((x, y, z))
            uvs.append((0.5 + x, 0.5 + z))
            cols.append((max(0.05, min(1.0, r_c)), max(0.04, min(1.0, g_c)), max(0.03, min(1.0, b_c))))
    stride = n_a + 1
    for ir in range(len(ring_specs) - 1):
        for j in range(n_a):
            a, b, c, d = ir * stride + j, (ir + 1) * stride + j, (ir + 1) * stride + (j + 1), ir * stride + (j + 1)
            tris.extend(((a, c, b), (a, d, c)))
    pos = np.asarray(verts, dtype=np.float32)
    idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
    return MeshPart(
        name,
        "item_cola_spill",
        pos,
        recompute_smooth_normals(pos, idx),
        idx,
        uvs=np.asarray(uvs, dtype=np.float32),
        colors=np.asarray(cols, dtype=np.float32),
    )


def _build_cola_spill(name: str, stage: int) -> SceneBuilder:
    """Plump 3D fizzy caramel-amber cola puddles matching exact 1998 shapes & sizes (`276..279`; `1..3` animated fizz!)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    base_parts: List[MeshPart] = []

    if stage == 1:
        # Exact `#276:fdcola1` (161x118px): huge plump caramel puddle with long sweeping lower-left tongue!
        base_parts.append(_cola_puddle_mesh(f"{name}_pool", _COLA1_RADS36, scale_r=0.45, thick=0.155, z_stretch=1.18))
        bub_coords = (
            (-0.14, +0.08, 0.022, 0.142),
            (+0.11, -0.08, 0.026, 0.144),
            (+0.01, +0.02, 0.020, 0.158),
            (-0.22, +0.17, 0.018, 0.116),
            (+0.22, -0.02, 0.022, 0.132),
            (-0.04, -0.11, 0.018, 0.146),
            (+0.15, +0.10, 0.018, 0.136),
            (+0.06, -0.18, 0.016, 0.126),
        )
    elif stage == 2:
        # Exact `#277:fdcola2` (126x94px): sweeping diagonal butterfly/forked-crest cola splash!
        base_parts.append(_cola_puddle_mesh(f"{name}_pool", _COLA2_RADS36, scale_r=0.38, thick=0.135, z_stretch=1.18))
        bub_coords = (
            (-0.12, +0.10, 0.020, 0.118),
            (+0.10, -0.06, 0.022, 0.124),
            (-0.01, +0.02, 0.018, 0.136),
            (+0.17, -0.08, 0.016, 0.112),
            (-0.20, +0.19, 0.016, 0.096),
        )
    elif stage == 3:
        # Exact `#278:fdcola3` (87x76px): steep SW-NE peanut puddle + plump floating upper teardrop!
        base_parts.append(_cola_puddle_mesh(f"{name}_pool", _COLA3_RADS36, scale_r=0.31, thick=0.115, z_stretch=1.20))
        drop = ellipsoid(0.036, 0.056, 0.064, u_segs=22, v_segs=14, name=f"{name}_drop", material="item_cola_spill").translate(+0.066, 0.036, -0.320)
        p = drop.positions.astype(np.float64)
        p[:, 1] = np.maximum(p[:, 1], 0.002)
        drop.positions = p.astype(np.float32)
        drop.normals = recompute_smooth_normals(drop.positions, drop.indices)
        base_parts.append(gradient_y_tint(drop, (0.76, 0.44, 0.12), (1.00, 0.90, 0.56)))
        bub_coords = (
            (-0.10, +0.09, 0.018, 0.102),
            (+0.09, -0.07, 0.020, 0.106),
            (-0.01, +0.01, 0.016, 0.116),
        )
    else:
        # Exact `#279:fdcola4` (55x43px): 3 plump kissing diagonal droplets + tiny SW drip & SE teardrop!
        for k, (cx, cz, rx, ry, rz, yaw) in enumerate((
            (-0.17, +0.09, 0.098, 0.068, 0.088, 22.0),
            (-0.24, +0.16, 0.036, 0.038, 0.050, 35.0),
            (-0.01, +0.02, 0.072, 0.062, 0.078, -18.0),
            (+0.13, -0.09, 0.096, 0.070, 0.092, 24.0),
            (+0.15, +0.14, 0.044, 0.046, 0.058, -12.0),
        )):
            d = ellipsoid(rx, ry, rz, u_segs=22, v_segs=14, name=f"{name}_drop_{k}", material="item_cola_spill").rotate_xyz(ry=yaw, degrees=True).translate(cx, ry * 0.62, cz)
            p = d.positions.astype(np.float64)
            p[:, 1] = np.maximum(p[:, 1], 0.002)
            d.positions = p.astype(np.float32)
            d.normals = recompute_smooth_normals(d.positions, d.indices)
            base_parts.append(gradient_y_tint(d, (0.76, 0.44, 0.12), (1.00, 0.92, 0.60)))
        bub_coords = ()

    sc.add("cola", base_parts)

    # Delicate sparkling champagne-gold effervescent fizz bubbles & micro-foam rings (`276..278`)!
    if bub_coords:
        group_a: List[MeshPart] = []
        group_b: List[MeshPart] = []
        for k, (bx, bz, br, by) in enumerate(bub_coords):
            b = sphere(br * 0.34, u_segs=10, v_segs=8, name=f"{name}_fizz_{k}", material="item_candy_white").translate(bx, by + 0.002, bz)
            b = tint_part(b, _const_tint((1.00, 0.90, 0.62)))
            ring = torus_arc(br * 0.56, br * 0.11, u_segs=14, v_segs=6, plane="xz", name=f"{name}_ring_{k}", material="item_candy_white").translate(bx, by - 0.002, bz)
            ring = tint_part(ring, _const_tint((1.00, 0.82, 0.44)))
            target = group_a if k % 2 == 0 else group_b
            target.extend((b, ring))
        sc.add("fizz_a", group_a, parent="cola")
        sc.add("fizz_b", group_b, parent="cola")
        n_kf = 13
        times = np.linspace(0.0, 1.4, n_kf, dtype=np.float32)
        sa, sb = [], []
        ta, tb = [], []
        for t in times:
            ph = float(t) / 1.4 * 2.0 * math.pi
            fa = 0.55 + 0.65 * (0.5 + 0.5 * math.sin(ph))
            fb = 0.55 + 0.65 * (0.5 + 0.5 * math.sin(ph + math.pi))
            sa.append([fa, fa, fa])
            sb.append([fb, fb, fb])
            ta.append([0.0, 0.014 * (0.5 + 0.5 * math.sin(ph)), 0.0])
            tb.append([0.0, 0.014 * (0.5 + 0.5 * math.sin(ph + math.pi)), 0.0])
        sc.add_animation(
            "Idle",
            [
                AnimationChannel("fizz_a", "scale", times, np.asarray(sa, dtype=np.float32)),
                AnimationChannel("fizz_a", "translation", times, np.asarray(ta, dtype=np.float32)),
                AnimationChannel("fizz_b", "scale", times, np.asarray(sb, dtype=np.float32)),
                AnimationChannel("fizz_b", "translation", times, np.asarray(tb, dtype=np.float32)),
            ],
        )

    # Authentic relative sizing matching 1998 sprites (`161px`, `126px`, `87px`, `55px`)!
    return _finalize_item(sc, target_span=0.96 if stage == 1 else (0.78 if stage == 2 else (0.60 if stage == 3 else 0.44)))


def build_fdcola1() -> SceneBuilder:
    return _build_cola_spill("item_fdcola1", 1)


def build_fdcola2() -> SceneBuilder:
    return _build_cola_spill("item_fdcola2", 2)


def build_fdcola3() -> SceneBuilder:
    return _build_cola_spill("item_fdcola3", 3)


def build_fdcola4() -> SceneBuilder:
    return _build_cola_spill("item_fdcola4", 4)


# ---------------------------------------------------------------------------
# 4. INDIVIDUAL FRUIT LOOPS CEREAL RINGS (`280..286:fdfrl1..7`)
# ---------------------------------------------------------------------------

def _fruit_loop_ring(
    name: str,
    base_rgb: Tuple[float, float, float],
    glaze_rgb: Tuple[float, float, float],
    *,
    major_r: float = 0.21,
    minor_r: float = 0.092,
    oval_x: float = 1.0,
    oval_z: float = 1.0,
    start_ang: float = 0.0,
    end_ang: float = 2.0 * math.pi,
    speckle_rgb: Optional[Tuple[float, float, float]] = None,
) -> MeshPart:
    """Real matte puffed-corn Fruit Loops cereal ring (smooth circular torus, subtle sugar-frosted crunch!)."""
    is_closed = abs((end_ang - start_ang) - 2.0 * math.pi) < 1e-3
    if is_closed:
        ring = torus_arc(major_r, minor_r, u_segs=44, v_segs=22, plane="xz", name=name, material="item_cereal_matte")
    else:
        n_pts = 15
        pts = [
            (
                major_r * oval_x * math.cos(start_ang + (end_ang - start_ang) * i / (n_pts - 1)),
                minor_r,
                major_r * oval_z * math.sin(start_ang + (end_ang - start_ang) * i / (n_pts - 1)),
            )
            for i in range(n_pts)
        ]
        rads = [minor_r * (0.90 if i in (0, n_pts - 1) else 1.0) for i in range(n_pts)]
        ring = curved_tube(pts, rads, samples_per_seg=4, radial_segs=20, cap_rings=4, name=name, material="item_cereal_matte")

    p = ring.positions.astype(np.float64)
    if is_closed:
        p[:, 0] *= oval_x
        p[:, 2] *= oval_z
        p[:, 1] += minor_r
    # Very gentle natural puffed-cereal softness (never wavy/lumpy plastic!)
    bump = 0.0018 * np.sin(p[:, 0] * 26.0 + p[:, 2] * 22.0) * np.cos(p[:, 1] * 28.0)
    p += ring.normals.astype(np.float64) * bump[:, None]
    p[:, 1] = np.maximum(p[:, 1], 0.002)
    ring.positions = p.astype(np.float32)
    ring.normals = recompute_smooth_normals(ring.positions, ring.indices)

    def _cereal_color(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
        hi = np.clip((pos[:, 1] - 0.015) / (minor_r * 1.85), 0.0, 1.0)[:, None]
        # Subtle dry sugar-frosted dusting highlight on top crest while keeping vibrant Fruit Loops pigment!
        frost = (np.clip(nrm[:, 1], 0.0, 1.0) ** 3.2)[:, None] * 0.09
        lo_c = np.asarray(base_rgb, dtype=np.float64)[None, :]
        hi_c = np.asarray(glaze_rgb, dtype=np.float64)[None, :]
        c = lo_c * (1.0 - hi) + hi_c * hi
        c = c * (1.0 - frost) + np.array([[0.99, 0.97, 0.94]]) * frost
        if speckle_rgb is not None:
            sp = (np.sin(pos[:, 0] * 58.0 + pos[:, 2] * 44.0) * np.cos(pos[:, 2] * 62.0 - pos[:, 1] * 48.0) > 0.35).astype(np.float64)[:, None]
            c = c * (1.0 - 0.82 * sp) + np.asarray(speckle_rgb, dtype=np.float64)[None, :] * (0.82 * sp)
        return np.clip(c, 0.05, 1.0)

    return tint_part(ring, _cereal_color)


def build_fdfrl1() -> SceneBuilder:
    """Coral-pink frosted Fruit Loop cereal ring (`280:fdfrl1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdfrl1")
    sc.add("frootloop", [_fruit_loop_ring("fdfrl1_ring", (0.76, 0.22, 0.16), (0.96, 0.42, 0.34))])
    return _finalize_item(sc, target_span=0.52)


def build_fdfrl2() -> SceneBuilder:
    """Periwinkle purple-blue berry Fruit Loop with hot-pink speckles (`281:fdfrl2`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdfrl2")
    sc.add("frootloop", [_fruit_loop_ring("fdfrl2_ring", (0.26, 0.18, 0.84), (0.50, 0.40, 0.98), speckle_rgb=(0.98, 0.14, 0.56))])
    return _finalize_item(sc, target_span=0.52)


def build_fdfrl3() -> SceneBuilder:
    """Golden-yellow frosted Fruit Loop (`282:fdfrl3`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdfrl3")
    sc.add("frootloop", [_fruit_loop_ring("fdfrl3_ring", (0.76, 0.56, 0.22), (0.96, 0.80, 0.42), oval_x=0.90, oval_z=1.06)])
    return _finalize_item(sc, target_span=0.52)


def build_fdfrl4() -> SceneBuilder:
    """Half-eaten golden-yellow Fruit Loop C-arc (`283:fdfrl4`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdfrl4")
    sc.add("frootloop", [_fruit_loop_ring("fdfrl4_arc", (0.76, 0.56, 0.22), (0.96, 0.80, 0.42), start_ang=-1.85, end_ang=+1.85)])
    return _finalize_item(sc, target_span=0.48)


def build_fdfrl5() -> SceneBuilder:
    """Bright lime-green frosted Fruit Loop (`284:fdfrl5`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdfrl5")
    sc.add("frootloop", [_fruit_loop_ring("fdfrl5_ring", (0.08, 0.64, 0.08), (0.28, 0.90, 0.22), oval_x=1.10, oval_z=0.92)])
    return _finalize_item(sc, target_span=0.52)


def build_fdfrl6() -> SceneBuilder:
    """Broken purple-and-pink marbled Fruit Loop shards (`285:fdfrl6`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdfrl6")
    arc = _fruit_loop_ring("fdfrl6_arc", (0.24, 0.22, 0.86), (0.96, 0.24, 0.68), start_ang=-1.75, end_ang=+0.95, speckle_rgb=(0.98, 0.16, 0.62))
    chunk = ellipsoid(0.088, 0.072, 0.125, u_segs=20, v_segs=14, name="fdfrl6_chunk", material="item_cereal_matte").rotate_xyz(ry=28.0, degrees=True).translate(-0.15, 0.068, +0.16)
    chunk = tint_part(chunk, _const_tint((0.68, 0.24, 0.82)))
    sc.add("frootloop", [arc, chunk])
    return _finalize_item(sc, target_span=0.52)


def build_fdfrl7() -> SceneBuilder:
    """Oval lime-green frosted Fruit Loop (`286:fdfrl7`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdfrl7")
    sc.add("frootloop", [_fruit_loop_ring("fdfrl7_ring", (0.10, 0.70, 0.06), (0.34, 0.94, 0.20), oval_x=0.92, oval_z=1.12).rotate_xyz(ry=16.0, degrees=True)])
    return _finalize_item(sc, target_span=0.50)
