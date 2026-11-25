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
    seg = curved_tube(list(pts), list(rads), samples_per_seg=5, radial_segs=14, cap_rings=3, name=name, material="item_candy_white")
    # Add tactile gummy worm transverse ring-ribs along X!
    p = seg.positions.astype(np.float64)
    nrm = seg.normals.astype(np.float64)
    rib = 0.0055 * np.sin(p[:, 0] * 115.0)
    p = p + nrm * rib[:, None]
    p[:, 1] = np.maximum(p[:, 1], 0.003)
    seg.positions = p.astype(np.float32)
    seg.normals = recompute_smooth_normals(seg.positions, seg.indices)
    return tint_part(seg, _rainbow_worm_tint)


def _build_gummy_worm(name: str, stage: int) -> SceneBuilder:
    """Neon rainbow glossy ribbed gummy worm through 4 authentic stages (`272..275`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    if stage == 1:
        pts = [
            (-0.46, 0.048, +0.025),
            (-0.32, 0.056, -0.020),
            (-0.16, 0.052, -0.010),
            (0.00, 0.056, +0.015),
            (+0.18, 0.052, +0.030),
            (+0.34, 0.058, -0.015),
            (+0.46, 0.048, -0.005),
        ]
        rads = [0.046, 0.048, 0.046, 0.048, 0.046, 0.048, 0.044]
        parts.append(_ribbed_gummy_segment(f"{name}_worm", pts, rads))
    elif stage == 2:
        # Pinched/chewed bite notches along middle of the gummy worm (`273:fdgumw2`)
        pts = [
            (-0.46, 0.048, +0.025),
            (-0.32, 0.056, -0.020),
            (-0.18, 0.042, -0.008),
            (-0.04, 0.054, +0.014),
            (+0.12, 0.040, +0.026),
            (+0.28, 0.056, -0.010),
            (+0.44, 0.048, -0.004),
        ]
        rads = [0.046, 0.048, 0.024, 0.046, 0.022, 0.046, 0.042]
        parts.append(_ribbed_gummy_segment(f"{name}_worm", pts, rads))
    elif stage == 3:
        # 4 separated chewed rainbow segments (`274:fdgumw3`)
        segs = (
            ([(-0.45, 0.046, +0.022), (-0.40, 0.050, +0.010), (-0.34, 0.046, -0.006)], [0.044, 0.046, 0.036]),
            ([(-0.24, 0.048, -0.018), (-0.16, 0.052, -0.010), (-0.08, 0.046, +0.006)], [0.038, 0.046, 0.036]),
            ([(+0.04, 0.048, +0.018), (+0.16, 0.052, +0.024), (+0.28, 0.046, +0.002)], [0.036, 0.046, 0.038]),
            ([(+0.37, 0.044, -0.010), (+0.41, 0.046, -0.006), (+0.45, 0.042, -0.002)], [0.034, 0.040, 0.032]),
        )
        for k, (pts, rads) in enumerate(segs):
            parts.append(_ribbed_gummy_segment(f"{name}_seg_{k}", pts, rads))
    else:
        # Stage 4 (`275:fdgumw4`): 3 tiny separated rainbow gummy scraps!
        for k, (cx, cz, rx) in enumerate(((-0.34, +0.018, 0.042), (-0.08, -0.010, 0.056), (+0.24, +0.014, 0.064))):
            bit = ellipsoid(rx, 0.046, 0.038, u_segs=14, v_segs=10, name=f"{name}_bit_{k}", material="item_candy_white").translate(cx, 0.044, cz)
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

def _cola_puddle_mesh(name: str, lobes: Sequence[Tuple[float, float, float, float]], *, thick: float = 0.052) -> MeshPart:
    """Glossy caramel-amber cola liquid puddle with surface-tension rounded meniscus & multi-lobe silhouette."""
    n_r, n_a = 14, 56
    verts, uvs, tris = [], [], []

    def _lobe_r(ang: float) -> float:
        # Smooth organic puddle outline formed by summing directional cosine lobes
        r = 0.16
        for la, lw, amp in lobes:
            da = math.atan2(math.sin(ang - la), math.cos(ang - la))
            r += amp * math.exp(-((da / lw) ** 2))
        return r

    ring_specs = [(ir / float(n_r), True) for ir in range(n_r + 1)] + [(1.0 - ir / float(n_r), False) for ir in range(n_r + 1)]
    for rf, is_top in ring_specs:
        for j in range(n_a + 1):
            ang = 2.0 * math.pi * j / float(n_a)
            rad = _lobe_r(ang) * rf
            x = rad * math.cos(ang)
            z = rad * math.sin(ang) * 0.76
            if is_top:
                # Realistic rounded liquid surface-tension dome (thick flat center, curving cleanly down at meniscus!)
                meniscus = (1.0 - (rf ** 4.0)) ** 0.42 if rf < 0.999 else 0.0
                y = 0.008 + thick * meniscus
            else:
                y = 0.004 if rf > 0.96 else 0.001
            verts.append((x, y, z))
            uvs.append((0.5 + x, 0.5 + z))
    stride = n_a + 1
    for ir in range(len(ring_specs) - 1):
        for j in range(n_a):
            a, b, c, d = ir * stride + j, (ir + 1) * stride + j, (ir + 1) * stride + (j + 1), ir * stride + (j + 1)
            tris.extend(((a, c, b), (a, d, c)))
    pos = np.asarray(verts, dtype=np.float32)
    idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
    mesh = MeshPart(name, "item_cola_spill", pos, recompute_smooth_normals(pos, idx), idx, uvs=np.asarray(uvs, dtype=np.float32))
    return gradient_y_tint(mesh, (0.66, 0.30, 0.04), (1.00, 0.68, 0.22), noise_amp=0.02)


def _build_cola_spill(name: str, stage: int) -> SceneBuilder:
    """Spilled fizzy caramel-amber cola puddles (`276..279`; stages 1..3 have animated rising fizz bubbles!)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    base_parts: List[MeshPart] = []

    if stage == 1:
        # Large multi-lobed caramel cola puddle sweeping upper-right -> lower-left (`276:fdcola1`)
        base_parts.append(
            _cola_puddle_mesh(
                f"{name}_pool",
                (
                    (-0.45, 0.85, 0.28),
                    (+0.35, 0.70, 0.16),
                    (+2.35, 0.65, 0.30),
                    (-1.75, 0.72, 0.14),
                ),
                thick=0.062,
            )
        )
        bub_coords = ((-0.12, +0.06, 0.028), (+0.10, -0.08, 0.032), (+0.02, +0.01, 0.024), (-0.22, +0.14, 0.022), (+0.20, -0.02, 0.026), (-0.04, -0.10, 0.020))
    elif stage == 2:
        # Medium 3-lobed splashing puddle (`277:fdcola2`)
        base_parts.append(
            _cola_puddle_mesh(
                f"{name}_pool",
                (
                    (-0.55, 0.62, 0.22),
                    (+2.25, 0.55, 0.26),
                    (-1.45, 0.45, 0.14),
                ),
                thick=0.056,
            )
        )
        bub_coords = ((-0.10, +0.08, 0.026), (+0.08, -0.06, 0.028), (-0.01, +0.01, 0.022), (+0.15, -0.02, 0.020))
    elif stage == 3:
        # Diagonal peanut puddle + detached upper-right splash drop (`278:fdcola3`)
        base_parts.append(
            _cola_puddle_mesh(
                f"{name}_pool",
                (
                    (-0.55, 0.62, 0.20),
                    (+2.40, 0.60, 0.22),
                ),
                thick=0.056,
            )
        )
        drop = ellipsoid(0.054, 0.044, 0.072, u_segs=14, v_segs=10, name=f"{name}_drop", material="item_cola_spill").translate(+0.10, 0.024, -0.26)
        base_parts.append(gradient_y_tint(drop, (0.68, 0.32, 0.04), (1.00, 0.70, 0.24)))
        bub_coords = ((-0.12, +0.08, 0.024), (+0.08, -0.06, 0.026), (-0.02, +0.01, 0.020))
    else:
        # Stage 4 (`279:fdcola4`): 4 glistening surface-tension cola droplets!
        for k, (cx, cz, rx, rz) in enumerate(((-0.18, +0.05, 0.105, 0.095), (-0.01, +0.01, 0.078, 0.076), (+0.14, -0.10, 0.100, 0.098), (+0.15, +0.16, 0.048, 0.058))):
            d = ellipsoid(rx, 0.048, rz, u_segs=16, v_segs=10, name=f"{name}_drop_{k}", material="item_cola_spill").translate(cx, 0.026, cz)
            p = d.positions.astype(np.float64)
            p[:, 1] = np.maximum(p[:, 1], 0.002)
            d.positions = p.astype(np.float32)
            d.normals = recompute_smooth_normals(d.positions, d.indices)
            base_parts.append(gradient_y_tint(d, (0.66, 0.30, 0.04), (1.00, 0.72, 0.24)))
        bub_coords = ()

    sc.add("cola", base_parts)

    # Delicate translucent golden-champagne effervescent fizz micro-bubbles matching `#276..278`!
    if bub_coords:
        group_a: List[MeshPart] = []
        group_b: List[MeshPart] = []
        for k, (bx, bz, br) in enumerate(bub_coords):
            b = sphere(br * 0.44, u_segs=10, v_segs=8, name=f"{name}_fizz_{k}", material="item_cola_spill").translate(bx, 0.066, bz)
            b = tint_part(b, _const_tint((1.00, 0.88, 0.54)))
            (group_a if k % 2 == 0 else group_b).append(b)
        sc.add("fizz_a", group_a, parent="cola")
        sc.add("fizz_b", group_b, parent="cola")
        n_kf = 13
        times = np.linspace(0.0, 1.4, n_kf, dtype=np.float32)
        sa, sb = [], []
        ta, tb = [], []
        for t in times:
            ph = float(t) / 1.4 * 2.0 * math.pi
            fa = 0.70 + 0.45 * (0.5 + 0.5 * math.sin(ph))
            fb = 0.70 + 0.45 * (0.5 + 0.5 * math.sin(ph + math.pi))
            sa.append([fa, fa, fa])
            sb.append([fb, fb, fb])
            ta.append([0.0, 0.012 * (0.5 + 0.5 * math.sin(ph)), 0.0])
            tb.append([0.0, 0.012 * (0.5 + 0.5 * math.sin(ph + math.pi)), 0.0])
        sc.add_animation(
            "Idle",
            [
                AnimationChannel("fizz_a", "scale", times, np.asarray(sa, dtype=np.float32)),
                AnimationChannel("fizz_a", "translation", times, np.asarray(ta, dtype=np.float32)),
                AnimationChannel("fizz_b", "scale", times, np.asarray(sb, dtype=np.float32)),
                AnimationChannel("fizz_b", "translation", times, np.asarray(tb, dtype=np.float32)),
            ],
        )

    return _finalize_item(sc, target_span=0.94 if stage == 1 else (0.82 if stage == 2 else (0.68 if stage == 3 else 0.56)))


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
    """Puffed frosted Fruit Loops cereal ring (full torus or bitten C-arc) with organic puffed cereal grain!"""
    is_closed = abs((end_ang - start_ang) - 2.0 * math.pi) < 1e-3
    if is_closed:
        ring = torus_arc(major_r, minor_r, u_segs=34, v_segs=16, plane="xz", name=name, material="item_cream_white")
    else:
        n_pts = 11
        pts = [
            (
                major_r * oval_x * math.cos(start_ang + (end_ang - start_ang) * i / (n_pts - 1)),
                minor_r,
                major_r * oval_z * math.sin(start_ang + (end_ang - start_ang) * i / (n_pts - 1)),
            )
            for i in range(n_pts)
        ]
        rads = [minor_r * (0.88 if i in (0, n_pts - 1) else 1.0) for i in range(n_pts)]
        ring = curved_tube(pts, rads, samples_per_seg=3, radial_segs=14, cap_rings=3, name=name, material="item_cream_white")

    p = ring.positions.astype(np.float64)
    if is_closed:
        p[:, 0] *= oval_x
        p[:, 2] *= oval_z
        p[:, 1] += minor_r
    # Subtle puffed extruded corn-cereal bumpiness!
    bump = 0.008 * np.sin(p[:, 0] * 36.0 + p[:, 2] * 32.0) * np.cos(p[:, 1] * 40.0)
    p += ring.normals.astype(np.float64) * bump[:, None]
    p[:, 1] = np.maximum(p[:, 1], 0.002)
    ring.positions = p.astype(np.float32)
    ring.normals = recompute_smooth_normals(ring.positions, ring.indices)

    def _cereal_color(pos: np.ndarray, _n: np.ndarray) -> np.ndarray:
        hi = np.clip((pos[:, 1] - 0.02) / (minor_r * 1.8), 0.0, 1.0)[:, None]
        lo_c = np.asarray(base_rgb, dtype=np.float64)[None, :]
        hi_c = np.asarray(glaze_rgb, dtype=np.float64)[None, :]
        c = lo_c * (1.0 - hi) + hi_c * hi
        if speckle_rgb is not None:
            sp = (np.sin(pos[:, 0] * 58.0 + pos[:, 2] * 44.0) * np.cos(pos[:, 2] * 62.0 - pos[:, 1] * 48.0) > 0.35).astype(np.float64)[:, None]
            c = c * (1.0 - 0.78 * sp) + np.asarray(speckle_rgb, dtype=np.float64)[None, :] * (0.78 * sp)
        return np.clip(c, 0.04, 1.0)

    return tint_part(ring, _cereal_color)


def build_fdfrl1() -> SceneBuilder:
    """Coral-pink frosted Fruit Loop cereal ring (`280:fdfrl1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdfrl1")
    sc.add("frootloop", [_fruit_loop_ring("fdfrl1_ring", (0.72, 0.24, 0.18), (0.94, 0.44, 0.36))])
    return _finalize_item(sc, target_span=0.52)


def build_fdfrl2() -> SceneBuilder:
    """Periwinkle purple-blue berry Fruit Loop with hot-pink speckles (`281:fdfrl2`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdfrl2")
    sc.add("frootloop", [_fruit_loop_ring("fdfrl2_ring", (0.24, 0.18, 0.80), (0.48, 0.42, 0.96), speckle_rgb=(0.98, 0.16, 0.56))])
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
    chunk = ellipsoid(0.088, 0.072, 0.125, u_segs=14, v_segs=10, name="fdfrl6_chunk", material="item_cream_white").rotate_xyz(ry=28.0, degrees=True).translate(-0.15, 0.068, +0.16)
    chunk = tint_part(chunk, _const_tint((0.68, 0.24, 0.82)))
    sc.add("frootloop", [arc, chunk])
    return _finalize_item(sc, target_span=0.52)


def build_fdfrl7() -> SceneBuilder:
    """Oval lime-green frosted Fruit Loop (`286:fdfrl7`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdfrl7")
    sc.add("frootloop", [_fruit_loop_ring("fdfrl7_ring", (0.10, 0.70, 0.06), (0.34, 0.94, 0.20), oval_x=0.92, oval_z=1.12).rotate_xyz(ry=16.0, degrees=True)])
    return _finalize_item(sc, target_span=0.50)
