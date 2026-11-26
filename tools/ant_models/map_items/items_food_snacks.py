"""Realistic 3D sculptors for 1998 Ants Food Wave 4C: Snacks, Bakery & Crackers (22 items).

Covers:
- `fdtwink1`..`fdtwink3` (#382–#384): Golden Twinkie sponge cake with oozing white cream filling
- `fdpmeat1`..`fdpmeat4` (#357–#360): 'POTTED MEAT' pull-tab silver tin (`fdpmeat1` animated squirming meat!)
- `fdchoc1`..`fdchoc4`   (#260–#263): Diagonal 'ULTIMATE CHOCOLATE BAR' in cobalt & white serrated foil
- `fdpretz1`..`fdpretz3` (#264–#266): Salted twisted hard brown pretzel & broken shards
- `fdcrakr1`..`fdcrakr4` (#369–#372): Diamond-angled golden square saltine cracker with docker holes
- `fdcraks1`..`fdcraks4` (#373–#376): Round scalloped Ritz cracker with piped brown swirl topping
"""
from __future__ import annotations

import math
from typing import List, Sequence, Tuple

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
            rounded_box,
            sphere,
            superellipsoid,
            torus_arc,
        )
        from .item_materials import gradient_y_tint, register_item_materials, tint_part
        from .items_food_savory import extrude_star_piped_ribbon
        from .items_toys import _finalize_item
    except ImportError:
        from map_items.item_materials import gradient_y_tint, register_item_materials, tint_part  # type: ignore
        from map_items.items_food_savory import extrude_star_piped_ribbon  # type: ignore
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
            rounded_box,
            sphere,
            superellipsoid,
            torus_arc,
        )


def _const_tint(rgb: Tuple[float, float, float]):
    def _fn(pos: np.ndarray, _n: np.ndarray) -> np.ndarray:
        return np.tile(np.asarray(rgb, dtype=np.float64)[None, :], (len(pos), 1))
    return _fn


# ---------------------------------------------------------------------------
# 1. GOLDEN TWINKIE SPONGE CAKE (`382..384:fdtwink1..3`)
# ---------------------------------------------------------------------------

def _cream_splat(name: str, cx: float, cz: float, scale: float = 1.0) -> List[MeshPart]:
    """Soft billowing whipped-cream squirt & puffs oozing from Twinkie filling holes (`382..384`)."""
    parts: List[MeshPart] = []
    main = ellipsoid(0.086 * scale, 0.044 * scale, 0.072 * scale, u_segs=22, v_segs=14, name=f"{name}_core", material="item_cream_white").translate(cx, 0.036 * scale, cz)
    p = main.positions.astype(np.float64)
    ang = np.arctan2(p[:, 2] - cz, p[:, 0] - cx)
    fold = 1.0 + 0.055 * np.sin(ang * 3.0 + 0.4) + 0.035 * np.cos(ang * 5.0 - 0.6)
    p[:, 0] = cx + (p[:, 0] - cx) * fold
    p[:, 2] = cz + (p[:, 2] - cz) * fold
    p[:, 1] = np.maximum(p[:, 1] + 0.003 * scale * np.cos(ang * 4.0), 0.003)
    main.positions = p.astype(np.float32)
    main.normals = recompute_smooth_normals(main.positions, main.indices)
    parts.append(gradient_y_tint(main, (0.85, 0.86, 0.90), (1.00, 1.00, 0.99)))
    for k, (dx, dz, rx, rz, ph) in enumerate((
        (-0.060 * scale, +0.040 * scale, 0.048 * scale, 0.034 * scale, 0.5),
        (+0.056 * scale, +0.032 * scale, 0.046 * scale, 0.032 * scale, 2.1),
        (-0.016 * scale, -0.046 * scale, 0.046 * scale, 0.036 * scale, 3.8),
    )):
        lobe = ellipsoid(rx, 0.032 * scale, rz, u_segs=16, v_segs=10, name=f"{name}_lobe_{k}", material="item_cream_white").translate(cx + dx, 0.027 * scale, cz + dz)
        parts.append(gradient_y_tint(lobe, (0.85, 0.86, 0.90), (1.00, 1.00, 0.99)))
    return parts


def _build_twinkie(name: str, stage: int) -> SceneBuilder:
    """Golden sponge-cake Twinkie with 3 bottom cream injection dots, porous bitten crumb & oozing cream (`382..384`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    if stage == 1:
        x_lo, x_hi = -0.37, +0.37
    elif stage == 2:
        x_lo, x_hi = -0.24, +0.37
    else:
        x_lo, x_hi = -0.02, +0.34

    half_l = 0.5 * (x_hi - x_lo)
    cx_cake = 0.5 * (x_hi + x_lo)
    cake = superellipsoid(half_l, 0.125, 0.148, e1=0.62, e2=0.55, u_segs=36, v_segs=22, name=f"{name}_cake", material="item_twinkie_cake")
    pc = cake.positions.astype(np.float64)
    pc[:, 1] = np.where(pc[:, 1] < 0.0, pc[:, 1] * 0.42, pc[:, 1] * 1.05) + 0.056
    pc[:, 0] += cx_cake
    if stage >= 2:
        # Ragged bitten sponge-cake cross-section on LEFT (`x_lo`) end!
        left_mask = pc[:, 0] < (x_lo + 0.085)
        bite_curve = (
            x_lo
            + 0.046 * np.exp(-((pc[left_mask, 2] / 0.115) ** 2))
            + 0.015 * np.sin(pc[left_mask, 2] * 42.0 + pc[left_mask, 1] * 28.0)
            + 0.008 * np.cos(pc[left_mask, 1] * 54.0 - pc[left_mask, 2] * 36.0)
        )
        pc[left_mask, 0] = np.maximum(pc[left_mask, 0], bite_curve)
    cake.positions = pc.astype(np.float32)
    cake.normals = recompute_smooth_normals(cake.positions, cake.indices)
    # Warm baked golden top dome -> pale creamy-gold underside & bright airy yellow sponge crumb on bitten face!
    def _twinkie_tint(pos: np.ndarray, _n: np.ndarray) -> np.ndarray:
        bitten_w = np.clip(((x_lo + 0.085) - pos[:, 0]) / 0.055, 0.0, 1.0) if stage >= 2 else np.zeros(len(pos))
        r_c = (0.92 + 0.08 * np.clip(pos[:, 1] / 0.18, 0.0, 1.0)) * (1.0 - 0.02 * bitten_w) + 1.00 * (0.02 * bitten_w)
        g_c = (0.76 + 0.16 * np.clip(pos[:, 1] / 0.18, 0.0, 1.0) - 0.12 * np.clip((pos[:, 1] - 0.13) / 0.06, 0.0, 1.0)) * (1.0 - 0.35 * bitten_w) + 0.94 * (0.35 * bitten_w)
        b_c = (0.28 + 0.24 * (1.0 - np.clip(pos[:, 1] / 0.16, 0.0, 1.0))) * (1.0 - 0.45 * bitten_w) + 0.56 * (0.45 * bitten_w)
        return np.clip(np.column_stack([r_c, g_c, b_c]), 0.16, 1.0)

    parts.append(tint_part(cake, _twinkie_tint))

    # Iconic 3 white cream injection dimples along the lower-front belly (+Z)!
    hole_xs = (-0.21, +0.02, +0.23) if stage == 1 else ((+0.02, +0.23) if stage == 2 else (+0.16,))
    for k, hx in enumerate(hole_xs):
        dot = ellipsoid(0.036, 0.022, 0.022, u_segs=14, v_segs=10, name=f"{name}_dot_{k}", material="item_cream_white").translate(hx, 0.046, +0.136)
        parts.append(dot)

    if stage == 1:
        # Fluffy ridged whipped cream squirting out bottom-left injection hole onto ground (`382:fdtwink1`)!
        ooze_neck = extrude_star_piped_ribbon(
            [(-0.21, 0.054, +0.125), (-0.21, 0.046, +0.190), (-0.22, 0.034, +0.262)],
            [0.024, 0.028, 0.032],
            n_flutes=8,
            flute_depth=0.28,
            width_ratio=1.18,
            height_ratio=0.82,
            samples_per_seg=6,
            radial_segs=32,
            valley_rgb=(0.82, 0.84, 0.89),
            mid_rgb=(0.94, 0.95, 0.96),
            crest_rgb=(1.00, 1.00, 0.99),
            name=f"{name}_ooze_neck",
            material="item_cream_white",
        )
        parts.append(ooze_neck)
        parts.extend(_cream_splat(f"{name}_splat", -0.22, +0.278, scale=0.88))
    elif stage == 2:
        # Exposed porous yellow sponge-crumb collar + fluffy whipped cream bursting out bitten LEFT end (`383:fdtwink2`)!
        crumb = ellipsoid(0.042, 0.098, 0.126, u_segs=18, v_segs=12, name=f"{name}_crumb_face", material="item_corndog_crumb").translate(x_lo + 0.032, 0.094, 0.0)
        parts.append(gradient_y_tint(crumb, (0.94, 0.84, 0.44), (1.00, 0.95, 0.62), noise_amp=0.04))
        parts.extend(_cream_splat(f"{name}_bite_cream", x_lo - 0.020, +0.016, scale=1.08))
        plug = ellipsoid(0.056, 0.072, 0.088, u_segs=18, v_segs=12, name=f"{name}_cream_plug", material="item_cream_white").translate(x_lo + 0.016, 0.088, 0.0)
        parts.append(gradient_y_tint(plug, (0.85, 0.86, 0.91), (1.00, 1.00, 0.99)))
    else:
        # Half-eaten right stub with porous sponge-crumb cut face & generous whipped cream puff (`384:fdtwink3`)!
        crumb = ellipsoid(0.042, 0.098, 0.126, u_segs=18, v_segs=12, name=f"{name}_crumb_face", material="item_corndog_crumb").translate(x_lo + 0.022, 0.094, 0.0)
        parts.append(gradient_y_tint(crumb, (0.94, 0.84, 0.44), (1.00, 0.95, 0.62), noise_amp=0.04))
        parts.extend(_cream_splat(f"{name}_bite_cream", x_lo - 0.060, +0.044, scale=1.20))
        plug = ellipsoid(0.060, 0.076, 0.094, u_segs=18, v_segs=12, name=f"{name}_cream_plug", material="item_cream_white").translate(x_lo + 0.008, 0.090, 0.0)
        parts.append(gradient_y_tint(plug, (0.85, 0.86, 0.91), (1.00, 1.00, 0.99)))

    sc.add("twinkie", parts)
    return _finalize_item(sc, target_span=0.94 if stage <= 2 else 0.72)


def build_fdtwink1() -> SceneBuilder:
    return _build_twinkie("item_fdtwink1", 1)


def build_fdtwink2() -> SceneBuilder:
    return _build_twinkie("item_fdtwink2", 2)


def build_fdtwink3() -> SceneBuilder:
    return _build_twinkie("item_fdtwink3", 3)


# ---------------------------------------------------------------------------
# 2. 'POTTED MEAT' PULL-TAB TIN CAN (`357..360:fdpmeat1..4`, `fdpmeat1` ANIMATED!)
# ---------------------------------------------------------------------------

def _build_potted_meat(name: str, stage: int) -> SceneBuilder:
    """Short squat upright silver tin with navy 'POTTED MEAT' label & vertical peeled pull-tab lid (`357..360`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    can_parts: List[MeshPart] = []

    r_can = 0.235
    h_can = 0.255

    # Outer cylindrical label sleeve with outward normals (`u` wraps around, `u=0.5` centered straight FRONT +Z!)
    n_rad, n_y = 44, 12
    verts, uvs, tris = [], [], []
    for iy in range(n_y + 1):
        vf = iy / float(n_y)  # 0 at top -> 1 at bottom
        y = h_can - 0.018 - (h_can - 0.036) * vf
        for j in range(n_rad + 1):
            uf = j / float(n_rad)
            # Map front facing arc (+Z, ang = pi/2) cleanly to u=0.50 so 'POTTED / MEAT' reads upright left-to-right!
            ang = math.pi * 0.5 + (uf - 0.5) * 2.0 * math.pi
            px = -r_can * math.cos(ang)  # wait: ang=pi/2 -> x=0, z=+r; uf increases -> x goes -r to +r!
            # Let's check exact cross product: at ang = pi/2, cos=0, sin=1. As uf increases: x = +r*sin(theta), z = +r*cos(theta)!
            theta = (uf - 0.5) * 2.0 * math.pi  # -pi -> +pi; 0 at front (+Z), -pi/2 at left (-X), +pi/2 at right (+X)
            px = r_can * math.sin(theta)
            pz = r_can * math.cos(theta)
            verts.append((px, y, pz))
            # Repeat label 2x around circumference so 'POTTED MEAT' fits generously across front 180-deg face!
            tex_u = (uf * 2.0 - 0.5) % 1.0
            uvs.append((tex_u, vf))
    stride = n_rad + 1
    for iy in range(n_y):
        for j in range(n_rad):
            a, b, c, d = iy * stride + j, (iy + 1) * stride + j, (iy + 1) * stride + (j + 1), iy * stride + (j + 1)
            # a=(y_hi, x_lo), b=(y_lo, x_lo), c=(y_lo, x_hi), d=(y_hi, x_hi) -> (a,b,c) faces OUTWARD (+Z)!
            tris.extend(((a, b, c), (a, c, d)))
    pos_l = np.asarray(verts, dtype=np.float32)
    idx_l = np.asarray(tris, dtype=np.uint32).reshape(-1)
    label_wall = MeshPart(f"{name}_label", "item_potted_meat", pos_l, recompute_smooth_normals(pos_l, idx_l), idx_l, uvs=np.asarray(uvs, dtype=np.float32))
    can_parts.append(label_wall)

    # Hollow gleaming silver tin interior + beaded rolled top & bottom metal rims!
    tin_prof = [
        (0.000, 0.004),
        (r_can - 0.008, 0.004),
        (r_can + 0.008, 0.012),
        (r_can + 0.008, 0.026),
        (r_can - 0.002, 0.028),
        (r_can - 0.002, h_can - 0.024),
        (r_can + 0.010, h_can - 0.016),
        (r_can + 0.010, h_can + 0.004),
        (r_can - 0.006, h_can + 0.008),
        (r_can - 0.018, h_can - 0.004),
        (r_can - 0.020, 0.026),
        (0.000, 0.024),
    ]
    tin_shell = lathe(tin_prof, radial_segs=42, cap_bottom=False, cap_top=False, axis="y", name=f"{name}_tin", material="item_aluminum")
    can_parts.append(tin_shell)

    # Peeled-back round silver tin lid standing curled STRAIGHT UP vertically on the back-center rim (`357..360`)!
    lid_disc = cylinder(0.185, 0.185, 0.014, radial_segs=32, bevel=0.004, axis="y", name=f"{name}_lid", material="item_aluminum")
    # Gently curl the peeled metal lid like a real pull-tab lid bent upward!
    pl = lid_disc.positions.astype(np.float64)
    pl[:, 1] += 0.048 * ((pl[:, 2] / 0.185) ** 2)
    lid_disc.positions = pl.astype(np.float32)
    lid_disc.normals = recompute_smooth_normals(lid_disc.positions, lid_disc.indices)
    # Stand lid nearly upright angled edge-on like `#357..360` showing classic half-moon curl + oval pull ring!
    lid_disc = lid_disc.rotate_xyz(rx=78.0, ry=-54.0, degrees=True).translate(+0.055, h_can + 0.140, -0.045)
    can_parts.append(lid_disc)

    # Classic metal pull-tab ring riveted near the top of the peeled-up lid!
    rivet = sphere(0.018, u_segs=12, v_segs=8, name=f"{name}_rivet", material="item_steel_dark").translate(+0.012, h_can + 0.258, -0.025)
    pull_ring = torus_arc(0.054, 0.013, u_segs=22, v_segs=8, plane="xy", name=f"{name}_ring", material="item_chrome_spring").rotate_xyz(rx=-16.0, ry=-48.0, degrees=True).translate(-0.012, h_can + 0.305, -0.018)
    can_parts.extend([rivet, pull_ring])
    sc.add("can", can_parts)

    # Authentic speckled rosy-pink minced potted-meat hash filling (`stage 1..3`; `stage 4` is completely empty!)
    def _spam_tint(pos: np.ndarray, _n: np.ndarray) -> np.ndarray:
        sp = 0.5 + 0.5 * np.sin(pos[:, 0] * 112.0 + pos[:, 2] * 94.0) * np.cos(pos[:, 1] * 105.0 - pos[:, 0] * 78.0)
        fat = (np.sin(pos[:, 0] * 155.0 - pos[:, 2] * 140.0) * np.cos(pos[:, 2] * 130.0 + pos[:, 1] * 110.0) > 0.62).astype(np.float64)
        hi = np.clip((pos[:, 1] - 0.04) / 0.22, 0.0, 1.0)
        return np.clip(
            np.column_stack([
                0.86 + 0.12 * hi + 0.06 * sp + 0.08 * fat,
                0.42 + 0.16 * hi + 0.09 * sp + 0.30 * fat,
                0.34 + 0.14 * hi + 0.08 * sp + 0.28 * fat,
            ]),
            0.20,
            1.0,
        )

    def _meat_morsel(tag: str, mx: float, my: float, mz: float, mr: float, seed: float) -> MeshPart:
        m = ellipsoid(mr, mr * 0.68, mr * 0.82, u_segs=10, v_segs=7, name=tag, material="item_bun_crumb")
        pm = m.positions.astype(np.float64)
        ang = np.arctan2(pm[:, 2], pm[:, 0])
        jag = 1.0 + 0.20 * np.sin(ang * 3.0 + seed) + 0.12 * np.cos(ang * 5.0 - seed * 1.7)
        pm[:, 0] *= jag
        pm[:, 2] *= jag
        m.positions = pm.astype(np.float32)
        m.normals = recompute_smooth_normals(m.positions, m.indices)
        return tint_part(m.rotate_xyz(rx=seed * 19.0, ry=seed * 37.0, degrees=True).translate(mx, my, mz), _spam_tint)

    if stage <= 3:
        meat_parts: List[MeshPart] = []
        if stage == 1:
            loaf = cylinder(r_can - 0.022, r_can - 0.024, 0.215, radial_segs=36, bevel=0.016, axis="y", name=f"{name}_loaf", material="item_bun_crumb").translate(0.0, 0.130, 0.0)
            pm = loaf.positions.astype(np.float64)
            top = pm[:, 1] > 0.18
            pm[top, 1] += (
                0.022 * np.sin(pm[top, 0] * 48.0) * np.cos(pm[top, 2] * 52.0)
                + 0.014 * np.cos(pm[top, 0] * 86.0 - pm[top, 2] * 78.0)
                + 0.016 * np.exp(-((pm[top, 0] ** 2 + pm[top, 2] ** 2) / 0.032))
            )
            loaf.positions = pm.astype(np.float32)
            loaf.normals = recompute_smooth_normals(loaf.positions, loaf.indices)
            meat_parts.append(tint_part(loaf, _spam_tint))
            for k in range(16):
                a = k * 2.39996
                rr = 0.028 + 0.140 * math.sqrt((k + 0.5) / 16.0)
                mx, mz = rr * math.cos(a), rr * math.sin(a)
                mr = 0.034 + 0.014 * ((k * 7) % 3) * 0.5
                meat_parts.append(_meat_morsel(f"{name}_morsel_{k}", mx, 0.236 + 0.010 * (1.0 - rr / 0.18), mz, mr, 1.3 + k * 1.1))
        elif stage == 2:
            loaf = ellipsoid(0.185, 0.105, 0.178, u_segs=26, v_segs=14, name=f"{name}_loaf", material="item_bun_crumb").translate(-0.032, 0.122, +0.012)
            pm = loaf.positions.astype(np.float64)
            pm[:, 1] = np.maximum(pm[:, 1], 0.026)
            pm[:, 1] += 0.018 * np.sin(pm[:, 0] * 54.0) * np.cos(pm[:, 2] * 50.0)
            loaf.positions = pm.astype(np.float32)
            loaf.normals = recompute_smooth_normals(loaf.positions, loaf.indices)
            meat_parts.append(tint_part(loaf, _spam_tint))
            for k in range(10):
                a = k * 2.39996 + 0.4
                rr = 0.022 + 0.115 * math.sqrt((k + 0.5) / 10.0)
                mx, mz = -0.032 + rr * math.cos(a), +0.012 + rr * math.sin(a)
                meat_parts.append(_meat_morsel(f"{name}_morsel_{k}", mx, 0.198 + 0.012 * (1.0 - rr / 0.15), mz, 0.034, 2.2 + k * 1.3))
        else:
            loaf = ellipsoid(0.135, 0.072, 0.128, u_segs=22, v_segs=12, name=f"{name}_loaf", material="item_bun_crumb").translate(-0.068, 0.086, +0.042)
            pm = loaf.positions.astype(np.float64)
            pm[:, 1] = np.maximum(pm[:, 1], 0.026)
            pm[:, 1] += 0.014 * np.sin(pm[:, 0] * 56.0) * np.cos(pm[:, 2] * 54.0)
            loaf.positions = pm.astype(np.float32)
            loaf.normals = recompute_smooth_normals(loaf.positions, loaf.indices)
            meat_parts.append(tint_part(loaf, _spam_tint))
            for k in range(5):
                a = k * 2.4 + 0.8
                rr = 0.018 + 0.065 * math.sqrt((k + 0.5) / 5.0)
                meat_parts.append(_meat_morsel(f"{name}_morsel_{k}", -0.068 + rr * math.cos(a), 0.142, +0.042 + rr * math.sin(a), 0.030, 3.1 + k * 1.5))
        sc.add("meat_fill", meat_parts, parent="can")

    # Stage 1 (`357:fdpmeat1`) has 42-frame authentic creeping/throbbing potted-meat idle animation!
    if stage == 1:
        n_kf = 17
        times = np.linspace(0.0, 2.0, n_kf, dtype=np.float32)
        scales = []
        trans = []
        for t in times:
            ph = float(t) * math.pi * 2.0
            sx = 1.0 + 0.045 * math.sin(ph)
            sy = 1.0 + 0.065 * math.cos(ph * 2.0)
            sz = 1.0 - 0.040 * math.sin(ph)
            scales.append([sx, sy, sz])
            trans.append([0.006 * math.sin(ph), 0.008 * max(0.0, math.sin(ph * 2.0)), 0.005 * math.cos(ph)])
        sc.add_animation(
            "Idle",
            [
                AnimationChannel("meat_fill", "scale", times, np.asarray(scales, dtype=np.float32)),
                AnimationChannel("meat_fill", "translation", times, np.asarray(trans, dtype=np.float32)),
            ],
        )

    return _finalize_item(sc, target_span=0.72)


def build_fdpmeat1() -> SceneBuilder:
    return _build_potted_meat("item_fdpmeat1", 1)


def build_fdpmeat2() -> SceneBuilder:
    return _build_potted_meat("item_fdpmeat2", 2)


def build_fdpmeat3() -> SceneBuilder:
    return _build_potted_meat("item_fdpmeat3", 3)


def build_fdpmeat4() -> SceneBuilder:
    return _build_potted_meat("item_fdpmeat4", 4)


# ---------------------------------------------------------------------------
# 3. 'ULTIMATE CHOCOLATE BAR' (`260..263:fdchoc1..4`)
# ---------------------------------------------------------------------------

def _choc_square(name: str, cx: float, cy: float, cz: float) -> MeshPart:
    """Single chamfered molded milk-chocolate bar pip with crisp beveled rectangular crown."""
    sq = rounded_box(0.094, 0.044, 0.086, bevel=0.011, segs=3, name=name, material="item_chocolate")
    return gradient_y_tint(sq, (0.72, 0.56, 0.44), (1.00, 0.90, 0.78), noise_amp=0.02).translate(cx, cy, cz)


def _foil_wrapper_body(name: str, x_lo: float, x_hi: float, *, crumpled: bool = False) -> MeshPart:
    """Cobalt & white 'ULTIMATE CHOCOLATE' foil wrapper shell with serrated right tail & crisp top UVs."""
    n_x, n_th = 24, 28
    verts, uvs, tris = [], [], []
    for ix in range(n_x + 1):
        uf = ix / float(n_x)
        x = x_lo + (x_hi - x_lo) * uf
        # Pinch right tail flat into classic zigzag crimped foil seam (`uf > 0.84`)
        tail = max(0.0, (uf - 0.82) / 0.18)
        rz = 0.135 * (1.0 - 0.06 * tail)
        ry = (0.046 if not crumpled else 0.036) * (1.0 - 0.72 * tail)
        if crumpled:
            # Crinkled twisted empty wrapper pinch in middle!
            pinch = 1.0 - 0.36 * math.exp(-(((uf - 0.45) / 0.22) ** 2))
            rz *= pinch
            ry = 0.034 + 0.018 * abs(math.sin(uf * 11.0))
        for j in range(n_th + 1):
            ang = 2.0 * math.pi * j / float(n_th)
            # ang=0 -> -Z (back edge), pi/2 -> +Y (top face), pi -> +Z (front edge), 3pi/2 -> -Y (bottom)
            pz = -rz * math.cos(ang)
            crinkle = (0.010 * math.sin(uf * 19.0 + ang * 3.0)) if crumpled else (0.003 * math.sin(uf * 14.0 + ang * 2.0))
            py = 0.048 + ry * math.sin(ang) + crinkle
            # Serrated zigzag foil teeth along the crimped right end (`uf > 0.94`)!
            px = x + (0.016 * math.sin(pz * 95.0) if uf > 0.94 else 0.0)
            verts.append((px, max(0.004, py), pz))
            tex_v = 0.5 + 0.5 * (pz / max(1e-4, rz))
            uvs.append((uf, tex_v))
    stride = n_th + 1
    for ix in range(n_x):
        for j in range(n_th):
            a, b, c, d = ix * stride + j, (ix + 1) * stride + j, (ix + 1) * stride + (j + 1), ix * stride + (j + 1)
            # Outward-facing winding matching verified `fdfish` / `pizzatube`!
            tris.extend(((a, c, b), (a, d, c)))
    pos = np.asarray(verts, dtype=np.float32)
    idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
    return MeshPart(name, "item_choc_bar", pos, recompute_smooth_normals(pos, idx), idx, uvs=np.asarray(uvs, dtype=np.float32))


def _peeled_foil_flaps(name: str, x_edge: float) -> List[MeshPart]:
    """Thin crinkled serrated silver-white torn inner foil flaps wrapping snugly around the open mouth (zero thick horns!)."""
    flaps: List[MeshPart] = []
    # Build two thin double-sided crinkled torn foil panels peeled gently back on back (-Z) & front (+Z) edges!
    for sgn, tag in ((-1.0, "back"), (+1.0, "front")):
        n_u, n_v = 14, 10
        verts, uvs, tris = [], [], []
        for top_s in (True, False):
            for iv in range(n_v + 1):
                vf = iv / float(n_v)  # 0 at wrapper seam -> 1 at torn outer foil edge
                for iu in range(n_u + 1):
                    uf = iu / float(n_u)  # 0..1 along length of torn flap
                    # Serrated jagged tear profile along the outer torn foil margin!
                    tear_jag = 0.024 * math.sin(uf * math.pi * 5.0 + sgn) + 0.012 * math.cos(uf * math.pi * 9.0)
                    span_x = (x_edge - 0.068 + 0.155 * uf) - vf * 0.022 * (1.0 - uf)
                    # Foil peels gently outward & curls softly back with subtle metallic crinkle facets!
                    curl_z = sgn * (0.118 + vf * (0.062 + tear_jag) * math.sin(uf * math.pi * 0.85 + 0.15))
                    crinkle = 0.008 * math.sin(uf * 22.0 + vf * 12.0 + sgn)
                    curl_y = 0.068 + 0.036 * math.sin(vf * math.pi * 0.72) - 0.012 * (vf ** 2) + crinkle
                    thick = 0.0028 if top_s else -0.0028
                    verts.append((span_x, max(0.008, curl_y + thick), curl_z))
                    uvs.append((uf, vf))
        stride = n_u + 1
        off = (n_v + 1) * stride
        for iv in range(n_v):
            for iu in range(n_u):
                a, b, c, d = iv * stride + iu, (iv + 1) * stride + iu, (iv + 1) * stride + (iu + 1), iv * stride + (iu + 1)
                if sgn > 0.0:
                    tris.extend(((a, b, c), (a, c, d)))
                    tris.extend(((off + a, off + c, off + b), (off + a, off + d, off + c)))
                else:
                    tris.extend(((a, c, b), (a, d, c)))
                    tris.extend(((off + a, off + b, off + c), (off + a, off + c, off + d)))
        pos = np.asarray(verts, dtype=np.float32)
        idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
        panel = MeshPart(f"{name}_{tag}", "item_aluminum", pos, recompute_smooth_normals(pos, idx), idx, uvs=np.asarray(uvs, dtype=np.float32))
        flaps.append(gradient_y_tint(panel, (0.82, 0.85, 0.90), (1.00, 1.00, 1.00), noise_amp=0.04))
    return flaps


def _build_choc_bar(name: str, stage: int) -> SceneBuilder:
    """Diagonal 'ULTIMATE CHOCOLATE BAR' in cobalt & white serrated foil (`260..263`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    if stage == 1:
        parts.append(_foil_wrapper_body(f"{name}_wrap", -0.22, +0.44, crumpled=False))
        parts.extend(_peeled_foil_flaps(f"{name}_flaps", -0.21))
        # Exposed glossy molded 2x2 chocolate squares protruding out the upper-left wrapper mouth!
        for ix, cx in enumerate((-0.34, -0.24)):
            for iz, cz in enumerate((-0.050, +0.050)):
                parts.append(_choc_square(f"{name}_sq_{ix}_{iz}", cx, 0.044, cz))
    elif stage == 2:
        parts.append(_foil_wrapper_body(f"{name}_wrap", -0.14, +0.42, crumpled=False))
        parts.extend(_peeled_foil_flaps(f"{name}_flaps", -0.14))
        # Bitten jagged chocolate squares protruding left + one nibbled corner
        for k, (cx, cz) in enumerate(((-0.27, -0.052), (-0.18, -0.050), (-0.19, +0.048))):
            parts.append(_choc_square(f"{name}_sq_{k}", cx, 0.044, cz))
    elif stage == 3:
        # Mostly eaten crumpled wrapper holding last bitten chocolate squares (`262:fdchoc3`)
        parts.append(_foil_wrapper_body(f"{name}_wrap", -0.06, +0.36, crumpled=True))
        parts.extend(_peeled_foil_flaps(f"{name}_flaps", -0.06))
        parts.append(_choc_square(f"{name}_sq_0", -0.12, 0.044, -0.032))
        parts.append(_choc_square(f"{name}_sq_1", -0.08, 0.044, +0.042))
    else:
        # Stage 4 (`263:fdchoc4`): completely empty crumpled & twisted cobalt/white foil wrapper!
        parts.append(_foil_wrapper_body(f"{name}_wrap", -0.18, +0.34, crumpled=True))
        parts.extend(_peeled_foil_flaps(f"{name}_flaps", -0.17))

    # Diagonal orientation from upper-left (-X, -Z) to lower-right (+X, +Z) matching `260..263`!
    sc.add("choc_bar", [p.rotate_xyz(ry=-30.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.96 if stage <= 2 else (0.84 if stage == 3 else 0.74))


def build_fdchoc1() -> SceneBuilder:
    return _build_choc_bar("item_fdchoc1", 1)


def build_fdchoc2() -> SceneBuilder:
    return _build_choc_bar("item_fdchoc2", 2)


def build_fdchoc3() -> SceneBuilder:
    return _build_choc_bar("item_fdchoc3", 3)


def build_fdchoc4() -> SceneBuilder:
    return _build_choc_bar("item_fdchoc4", 4)


# ---------------------------------------------------------------------------
# 4. SALTED TWISTED HARD PRETZEL (`264..266:fdpretz1..3`)
# ---------------------------------------------------------------------------

def _salt_crystals(name: str, coords: Sequence[Tuple[float, float, float]]) -> List[MeshPart]:
    return [
        rounded_box(0.016, 0.012, 0.016, bevel=0.003, segs=1, name=f"{name}_salt_{k}", material="item_candy_white")
        .rotate_xyz(ry=k * 37.0, degrees=True)
        .translate(x, y, z)
        for k, (x, y, z) in enumerate(coords)
    ]


def _pretzel_snap_cap(name: str, pt: Tuple[float, float, float], prev_pt: Tuple[float, float, float], rad: float) -> MeshPart:
    """Exposed pale-cream baked pretzel interior crumb flush on a snapped fracture end (`265..266`)."""
    dx, dy, dz = pt[0] - prev_pt[0], pt[1] - prev_pt[1], pt[2] - prev_pt[2]
    ln = max(1e-5, math.hypot(dx, dz))
    ux, uz = dx / ln, dz / ln
    yaw = math.degrees(math.atan2(ux, uz))
    cap = cylinder(rad * 0.96, rad * 0.90, 0.016, radial_segs=14, bevel=0.003, axis="z", name=name, material="item_bun_crumb")
    cap = gradient_y_tint(cap, (0.90, 0.82, 0.62), (1.00, 0.96, 0.82), noise_amp=0.04)
    return cap.rotate_xyz(ry=yaw, degrees=True).translate(pt[0] + ux * 0.004, pt[1], pt[2] + uz * 0.004)


def _cracker_flake(
    name: str,
    cx: float,
    cz: float,
    rx: float,
    rz: float,
    thickness: float,
    *,
    seed: float,
    rgb_lo: Tuple[float, float, float],
    rgb_hi: Tuple[float, float, float],
    rot_y: float = 0.0,
) -> MeshPart:
    """Thin flat jagged broken cracker shard/flake with baked blistering (replaces thick dice cubes!)."""
    disc = cylinder(rx, rx * 0.96, thickness, radial_segs=18, bevel=0.003, axis="y", name=name, material="item_cracker_gold")
    p = disc.positions.astype(np.float64)
    ang = np.arctan2(p[:, 2], p[:, 0])
    # Angular broken-cracker polygon contour (flat & crisp, asymmetrical wedge)
    jag = 1.0 + 0.22 * np.sin(ang * 3.0 + seed * 1.9) + 0.14 * np.cos(ang * 4.0 - seed * 2.5)
    p[:, 0] *= jag
    p[:, 2] *= jag * (rz / max(1e-4, rx))
    p[:, 1] += thickness * 0.5 + 0.002
    disc.positions = p.astype(np.float32)
    disc.normals = recompute_smooth_normals(disc.positions, disc.indices)
    disc = gradient_y_tint(disc, rgb_lo, rgb_hi, noise_amp=0.05)
    return disc.rotate_xyz(ry=rot_y, degrees=True).translate(cx, 0.0, cz)


def _build_pretzel(name: str, stage: int) -> SceneBuilder:
    """Glossy golden-amber lye-glazed twisted hard pretzel with coarse salt & pale snapped interiors (`264..266`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    if stage == 1:
        outer_pts: List[Tuple[float, float, float]] = []
        n = 40
        for i in range(n + 1):
            a = 2.0 * math.pi * i / n
            w = 0.34 - 0.045 * math.sin(a)
            x = w * math.cos(a)
            z = 0.255 * math.sin(a) + 0.042 * math.cos(2.0 * a)
            y = 0.046 + 0.006 * math.sin(a * 2.0)
            outer_pts.append((x, y, z))
        outer = curved_tube(outer_pts, [0.044] * len(outer_pts), samples_per_seg=2, radial_segs=16, cap_start=False, cap_end=False, name=f"{name}_outer", material="item_pretzel_crust")
        arm1 = curved_tube(
            [(-0.06, 0.046, +0.22), (-0.03, 0.044, +0.10), (0.00, 0.044, -0.02), (+0.11, 0.066, -0.14), (+0.19, 0.058, -0.19)],
            [0.038, 0.038, 0.038, 0.038, 0.036],
            samples_per_seg=4,
            radial_segs=16,
            cap_rings=2,
            name=f"{name}_arm1",
            material="item_pretzel_crust",
        )
        arm2 = curved_tube(
            [(+0.06, 0.046, +0.22), (+0.03, 0.066, +0.10), (0.00, 0.080, -0.02), (-0.11, 0.070, -0.14), (-0.19, 0.058, -0.19)],
            [0.038, 0.038, 0.040, 0.038, 0.036],
            samples_per_seg=4,
            radial_segs=16,
            cap_rings=2,
            name=f"{name}_arm2",
            material="item_pretzel_crust",
        )
        for p in (outer, arm1, arm2):
            parts.append(gradient_y_tint(p, (0.78, 0.62, 0.44), (1.00, 0.94, 0.80), noise_amp=0.04))
        parts.extend(
            _salt_crystals(
                name,
                (
                    (-0.27, 0.090, -0.08),
                    (-0.18, 0.096, -0.19),
                    (0.00, 0.118, -0.02),
                    (+0.18, 0.096, -0.18),
                    (+0.29, 0.088, +0.02),
                    (+0.14, 0.088, +0.21),
                    (-0.15, 0.088, +0.20),
                    (-0.29, 0.088, +0.06),
                ),
            )
        )
    elif stage == 2:
        # Three broken curved pretzel shards with exposed pale-dough snapped ends (`265:fdpretz2`)!
        segs = [
            ([(-0.28, 0.044, +0.08), (-0.25, 0.046, -0.06), (-0.14, 0.046, -0.18)], [0.040, 0.042, 0.038], "s1"),
            ([(-0.04, 0.044, +0.16), (-0.01, 0.052, +0.02), (+0.08, 0.048, -0.12)], [0.040, 0.044, 0.038], "s2"),
            ([(-0.02, 0.050, +0.02), (-0.055, 0.048, -0.02), (-0.09, 0.046, -0.06)], [0.036, 0.034, 0.032], "s2b"),
            ([(+0.08, 0.044, +0.21), (+0.22, 0.046, +0.18), (+0.26, 0.046, +0.02), (+0.25, 0.044, -0.10)], [0.038, 0.042, 0.042, 0.036], "s3"),
        ]
        for pts, rads, tag in segs:
            sh = curved_tube(pts, rads, samples_per_seg=4, radial_segs=14, cap_start=False, cap_end=False, name=f"{name}_{tag}", material="item_pretzel_crust")
            parts.append(gradient_y_tint(sh, (0.78, 0.62, 0.44), (1.00, 0.94, 0.80), noise_amp=0.04))
            parts.append(_pretzel_snap_cap(f"{name}_{tag}_c0", pts[0], pts[1], rads[0]))
            parts.append(_pretzel_snap_cap(f"{name}_{tag}_c1", pts[-1], pts[-2], rads[-1]))
        parts.extend(_salt_crystals(name, ((-0.25, 0.088, -0.05), (-0.01, 0.094, +0.02), (+0.24, 0.088, +0.12))))
    else:
        # Stage 3 (`266:fdpretz3`): central Y-knot fragment with exposed pale dough snaps + jagged broken pretzel chips!
        y1_pts = [(-0.09, 0.046, +0.18), (-0.03, 0.052, +0.02), (+0.07, 0.048, -0.16)]
        y2_pts = [(-0.03, 0.050, +0.02), (-0.06, 0.048, -0.035), (-0.09, 0.046, -0.09)]
        y1 = curved_tube(y1_pts, [0.046, 0.050, 0.042], samples_per_seg=4, radial_segs=14, cap_start=False, cap_end=False, name=f"{name}_y1", material="item_pretzel_crust")
        y2 = curved_tube(y2_pts, [0.042, 0.039, 0.036], samples_per_seg=3, radial_segs=12, cap_start=True, cap_end=False, name=f"{name}_y2", material="item_pretzel_crust")
        parts.extend([gradient_y_tint(p, (0.78, 0.62, 0.44), (1.00, 0.94, 0.80), noise_amp=0.04) for p in (y1, y2)])
        parts.append(_pretzel_snap_cap(f"{name}_y1_c0", y1_pts[0], y1_pts[1], 0.046))
        parts.append(_pretzel_snap_cap(f"{name}_y1_c1", y1_pts[-1], y1_pts[-2], 0.042))
        parts.append(_pretzel_snap_cap(f"{name}_y2_c1", y2_pts[-1], y2_pts[-2], 0.036))
        for k, (cx, cz, rx, rz, ry) in enumerate((
            (-0.14, -0.20, 0.036, 0.024, 24.0),
            (+0.20, -0.08, 0.042, 0.028, -32.0),
            (+0.18, +0.12, 0.038, 0.026, 48.0),
            (+0.10, +0.19, 0.030, 0.022, -16.0),
        )):
            parts.append(_cracker_flake(f"{name}_chip_{k}", cx, cz, rx, rz, 0.024, seed=4.0 + k * 1.7, rgb_lo=(0.56, 0.34, 0.16), rgb_hi=(0.86, 0.60, 0.34), rot_y=ry))
        parts.extend(_salt_crystals(name, ((-0.03, 0.100, +0.02), (+0.05, 0.090, -0.12))))

    sc.add("pretzel", parts)
    return _finalize_item(sc, target_span=0.76 if stage <= 2 else 0.58)


def build_fdpretz1() -> SceneBuilder:
    return _build_pretzel("item_fdpretz1", 1)


def build_fdpretz2() -> SceneBuilder:
    return _build_pretzel("item_fdpretz2", 2)


def build_fdpretz3() -> SceneBuilder:
    return _build_pretzel("item_fdpretz3", 3)


# ---------------------------------------------------------------------------
# 5. SQUARE DIAMOND SALTINE CRACKER (`369..372:fdcrakr1..4`)
# ---------------------------------------------------------------------------

def _saltine_slab(name: str, *, bite_notches: Sequence[Tuple[float, float, float]] = ()) -> List[MeshPart]:
    """High-res toasted pale-ivory square saltine with 36x36 crisp recessed docker dimples, oven blisters & jagged bites!"""
    parts: List[MeshPart] = []
    half = 0.255
    n_g = 36
    holes = [
        (ix * 0.135, iz * 0.135) for ix in (-1, 0, 1) for iz in (-1, 0, 1)
    ] + [
        (ix * 0.068, iz * 0.068) for ix in (-1, 1) for iz in (-1, 1)
    ]

    verts, uvs, tris = [], [], []
    for is_top in (True, False):
        for iz in range(n_g + 1):
            vz = iz / float(n_g)
            z0 = -half + 2.0 * half * vz
            for ix in range(n_g + 1):
                ux = ix / float(n_g)
                x0 = -half + 2.0 * half * ux
                x, z = x0, z0
                # Classic perforated snap-scallop saltine perimeter edges
                if ix in (0, n_g):
                    x -= math.copysign(0.008 * (0.5 + 0.5 * math.cos(z0 * 56.0)), x0)
                if iz in (0, n_g):
                    z -= math.copysign(0.008 * (0.5 + 0.5 * math.cos(x0 * 56.0)), z0)
                # Carve clean concave bite notches inward toward center (smooth curve, zero pinched spikes!)
                for bx, bz, br in bite_notches:
                    d = math.hypot(x - bx, z - bz)
                    if d < br:
                        nrm = max(1e-5, math.hypot(x, z))
                        push = (br - d) * 0.94
                        x -= (x / nrm) * push
                        z -= (z / nrm) * push
                edge = max(abs(x0), abs(z0)) / half
                if is_top:
                    # Puffy oven-baked blisters between docker pinholes + REAL sharp recessed 3D docker dimples!
                    dimple = 0.0
                    min_dh = 1.0
                    for hx, hz in holes:
                        dh = math.hypot(x - hx, z - hz)
                        min_dh = min(min_dh, dh)
                        if dh < 0.032:
                            dimple = max(dimple, 0.030 * math.exp(-((dh / 0.013) ** 2)))
                    blister = 0.011 * (1.0 - edge * edge) * min(1.0, min_dh / 0.055) + 0.003 * math.sin(x * 38.0) * math.cos(z * 38.0)
                    y = 0.052 + blister - dimple - (0.011 if edge > 0.95 else 0.0)
                else:
                    y = 0.011 if edge > 0.95 else 0.004
                verts.append((x, y, z))
                uvs.append((ux * 1.5, vz * 1.5))

    stride = n_g + 1
    off = (n_g + 1) * stride
    for iz in range(n_g):
        for ix in range(n_g):
            a, b, c, d = iz * stride + ix, (iz + 1) * stride + ix, (iz + 1) * stride + (ix + 1), iz * stride + (ix + 1)
            tris.extend(((a, b, c), (a, c, d)))
            a2, b2, c2, d2 = off + a, off + b, off + c, off + d
            tris.extend(((a2, c2, b2), (a2, d2, c2)))
    for i in range(n_g):
        t0, t1, b0, b1 = i, i + 1, off + i, off + i + 1
        tris.extend(((t0, t1, b1), (t0, b1, b0)))
        r = n_g * stride
        t0, t1, b0, b1 = r + i, r + i + 1, off + r + i, off + r + i + 1
        tris.extend(((t0, b1, t1), (t0, b0, b1)))
        t0, t1, b0, b1 = i * stride, (i + 1) * stride, off + i * stride, off + (i + 1) * stride
        tris.extend(((t0, b1, t1), (t0, b0, b1)))
        t0, t1, b0, b1 = i * stride + n_g, (i + 1) * stride + n_g, off + i * stride + n_g, off + (i + 1) * stride + n_g
        tris.extend(((t0, t1, b1), (t0, b1, b0)))

    pos = np.asarray(verts, dtype=np.float32)
    idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
    slab = MeshPart(f"{name}_slab", "item_cracker_gold", pos, recompute_smooth_normals(pos, idx), idx, uvs=np.asarray(uvs, dtype=np.float32))

    # Authentic crisp toasted ivory-tan saltine with brown-toasted puffy blister crests & deep dark docker holes!
    def _saltine_color(p: np.ndarray, _n: np.ndarray) -> np.ndarray:
        dimp = np.zeros(len(p), dtype=np.float64)
        for hx, hz in holes:
            dimp = np.maximum(dimp, np.exp(-(((p[:, 0] - hx) ** 2 + (p[:, 2] - hz) ** 2) / (0.016 ** 2))))
        top_w = np.clip((p[:, 1] - 0.022) / 0.024, 0.0, 1.0)
        dk = dimp * top_w
        r_f = np.clip(np.hypot(p[:, 0], p[:, 2]) / 0.34, 0.0, 1.0)
        toast_spot = np.clip(np.sin(p[:, 0] * 26.0 + 0.5) * np.cos(p[:, 2] * 28.0 - 0.3), 0.0, 1.0)
        return np.clip(
            np.column_stack([
                (1.00 - 0.14 * r_f - 0.08 * toast_spot) * (1.0 - 0.58 * dk),
                (0.92 - 0.22 * r_f - 0.12 * toast_spot) * (1.0 - 0.68 * dk),
                (0.70 - 0.22 * r_f - 0.14 * toast_spot) * (1.0 - 0.78 * dk),
            ]),
            0.10,
            1.0,
        )

    parts.append(tint_part(slab, _saltine_color))
    for k, (sx, sz) in enumerate(((-0.09, -0.04), (+0.11, -0.08), (+0.03, +0.10), (-0.12, +0.09), (+0.00, -0.11), (+0.14, +0.06))):
        if any(math.hypot(sx - bx, sz - bz) < br * 0.96 for bx, bz, br in bite_notches):
            continue
        parts.append(rounded_box(0.013, 0.008, 0.013, bevel=0.002, segs=1, name=f"{name}_salt_{k}", material="item_candy_white").rotate_xyz(ry=k * 29.0, degrees=True).translate(sx, 0.062, sz))
    return parts


def _build_saltine(name: str, stage: int) -> SceneBuilder:
    """Diamond-angled golden-ivory square saltine cracker through 4 eaten stages (`369..372`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    if stage == 1:
        parts.extend(_saltine_slab(name))
    elif stage == 2:
        parts.extend(_saltine_slab(name, bite_notches=((-0.24, +0.12, 0.11), (+0.23, -0.14, 0.10))))
        parts.append(_cracker_flake(f"{name}_cr0", -0.29, +0.18, 0.044, 0.034, 0.024, seed=1.2, rgb_lo=(0.76, 0.58, 0.34), rgb_hi=(0.98, 0.88, 0.64), rot_y=24.0))
    elif stage == 3:
        parts.extend(_saltine_slab(name, bite_notches=((-0.22, +0.20, 0.19), (+0.22, +0.16, 0.18), (-0.18, -0.22, 0.14))))
        for k, (cx, cz, rx, rz, ry) in enumerate((
            (-0.27, +0.21, 0.048, 0.036, 18.0),
            (+0.28, +0.14, 0.044, 0.032, -34.0),
            (+0.05, +0.29, 0.038, 0.028, 52.0),
        )):
            parts.append(_cracker_flake(f"{name}_cr_{k}", cx, cz, rx, rz, 0.022, seed=2.4 + k * 1.3, rgb_lo=(0.76, 0.58, 0.34), rgb_hi=(0.98, 0.88, 0.64), rot_y=ry))
    else:
        # Stage 4 (`372:fdcrakr4`): 2 crisp broken saltine corner shards (with docker pinholes & salt!) + 4 flaky crumbs!
        for k, (cx, cz, rx, rz, th, ry) in enumerate((
            (+0.08, -0.10, 0.125, 0.088, 0.048, 22.0),
            (+0.11, +0.11, 0.118, 0.086, 0.048, -18.0),
            (-0.10, -0.02, 0.056, 0.042, 0.032, 35.0),
            (-0.18, +0.06, 0.040, 0.030, 0.026, 12.0),
            (-0.02, +0.14, 0.036, 0.028, 0.026, -28.0),
        )):
            sh = _cracker_flake(f"{name}_frag_{k}", cx, cz, rx, rz, th, seed=3.1 + k * 1.9, rgb_lo=(0.74, 0.56, 0.32), rgb_hi=(0.98, 0.88, 0.64), rot_y=ry)
            if k < 2:
                # Flush-shaded dark-toasted docker pinhole + tiny white salt speck on each big saltine shard (no raised dots!)
                sh = tint_part(
                    sh,
                    lambda p, _n, cx0=cx, cz0=cz: np.clip(
                        np.column_stack([
                            0.94 * (1.0 - 0.52 * np.exp(-(((p[:, 0] - cx0) ** 2 + (p[:, 2] - cz0) ** 2) / (0.024 ** 2)))),
                            0.82 * (1.0 - 0.62 * np.exp(-(((p[:, 0] - cx0) ** 2 + (p[:, 2] - cz0) ** 2) / (0.024 ** 2)))),
                            0.58 * (1.0 - 0.72 * np.exp(-(((p[:, 0] - cx0) ** 2 + (p[:, 2] - cz0) ** 2) / (0.024 ** 2)))),
                        ]),
                        0.12,
                        1.0,
                    ),
                )
                parts.append(rounded_box(0.013, 0.008, 0.013, bevel=0.002, segs=1, name=f"{name}_s_{k}", material="item_candy_white").translate(cx - 0.028, th + 0.005, cz + 0.018))
            parts.append(sh)

    sc.add("saltine", [p.rotate_xyz(ry=42.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.72 if stage <= 3 else 0.58)


def build_fdcrakr1() -> SceneBuilder:
    return _build_saltine("item_fdcrakr1", 1)


def build_fdcrakr2() -> SceneBuilder:
    return _build_saltine("item_fdcrakr2", 2)


def build_fdcrakr3() -> SceneBuilder:
    return _build_saltine("item_fdcrakr3", 3)


def build_fdcrakr4() -> SceneBuilder:
    return _build_saltine("item_fdcrakr4", 4)


# ---------------------------------------------------------------------------
# 6. ROUND SCALLOPED RITZ CRACKER + STAR-TIP CHEEZ/STEAK WHIZ SWIRL (`373..376:fdcraks1..4`)
# ---------------------------------------------------------------------------

def _ritz_disc(name: str, stage: int) -> List[MeshPart]:
    """High-res 22-tooth scalloped golden-butter Ritz cracker with REAL 3D recessed toasted docker holes & jagged bites!"""
    parts: List[MeshPart] = []
    r_base = 0.288
    n_rings, n_ang = 16, 88
    dh_coords = [(0.205 * math.cos(k * 2.0 * math.pi / 7.0 + 0.35), 0.205 * math.sin(k * 2.0 * math.pi / 7.0 + 0.35)) for k in range(7)] + [(0.0, 0.0)]

    def _bound_r(ang: float) -> Tuple[float, float]:
        # Classic crisp 22-tooth buttery scalloped Ritz perimeter — fades cleanly out on bitten curves (zero spiky sun-rays!)
        bite_s = 1.0
        bitten = 0.0
        if stage == 2:
            for ba, bw, bd in ((+1.95, 0.32, 0.20), (+0.65, 0.28, 0.15)):
                da = math.atan2(math.sin(ang - ba), math.cos(ang - ba))
                w = math.exp(-((da / bw) ** 2))
                bite_s -= bd * w
                bitten = max(bitten, w)
        elif stage == 3:
            for ba, bw, bd in ((+0.35, 0.48, 0.32), (+1.85, 0.42, 0.28), (-0.85, 0.36, 0.20)):
                da = math.atan2(math.sin(ang - ba), math.cos(ang - ba))
                w = math.exp(-((da / bw) ** 2))
                bite_s -= bd * w
                bitten = max(bitten, w)
        elif stage == 4:
            # Left-half broken crescent Ritz cracker (`376:fdcraks4`)
            for ba, bw, bd in ((+0.15, 0.88, 0.60), (+1.38, 0.52, 0.38), (-1.08, 0.52, 0.42)):
                da = math.atan2(math.sin(ang - ba), math.cos(ang - ba))
                w = math.exp(-((da / bw) ** 2))
                bite_s -= bd * w
                bitten = max(bitten, w)
        scallop = 1.0 + 0.044 * math.cos(ang * 22.0) * (1.0 - 0.85 * bitten)
        return r_base * scallop * max(0.24, bite_s), bitten

    bound_r = []
    bound_bit = []
    for j in range(n_ang + 1):
        ang = 2.0 * math.pi * j / float(n_ang)
        rb, bb = _bound_r(ang)
        bound_r.append(rb)
        bound_bit.append(bb)

    verts, uvs, cols, tris = [], [], [], []
    ring_specs = [(ir / float(n_rings), True) for ir in range(n_rings + 1)] + [(1.0 - ir / float(n_rings), False) for ir in range(n_rings + 1)]
    for rf, is_top in ring_specs:
        for j in range(n_ang + 1):
            ang = 2.0 * math.pi * j / float(n_ang)
            x = rf * bound_r[j] * math.cos(ang)
            z = rf * bound_r[j] * math.sin(ang)
            dimp = 0.0
            for hx, hz in dh_coords:
                dh = math.hypot(x - hx, z - hz)
                if dh < 0.036:
                    dimp = max(dimp, math.exp(-((dh / 0.016) ** 2)))
            if is_top:
                # Subtle golden puff dome + bevelled scalloped edge + REAL sunken 3D docker pinholes!
                puff = 0.008 * (1.0 - rf * rf) + 0.003 * math.sin(x * 36.0) * math.cos(z * 36.0)
                edge_bev = 0.014 * max(0.0, (rf - 0.90) / 0.10)
                y = max(0.012, 0.048 + puff - edge_bev - 0.022 * dimp)
                # Bright buttery golden Ritz surface + warm toasted amber scalloped rim + deep dark toasted docker holes!
                rim_t = max(0.0, (rf - 0.72) / 0.28) * (1.0 - 0.65 * bound_bit[j])
                r_c = (1.00 - 0.12 * rim_t) * (1.0 - 0.48 * dimp)
                g_c = (0.94 - 0.24 * rim_t) * (1.0 - 0.60 * dimp)
                b_c = (0.42 - 0.20 * rim_t) * (1.0 - 0.72 * dimp)
            else:
                y = 0.012 if rf > 0.94 else 0.002
                r_c, g_c, b_c = 0.80, 0.56, 0.18
            verts.append((x, y, z))
            uvs.append((0.5 + x * 1.8, 0.5 + z * 1.8))
            cols.append((max(0.10, min(1.0, r_c)), max(0.08, min(1.0, g_c)), max(0.05, min(1.0, b_c))))

    stride = n_ang + 1
    for ir in range(len(ring_specs) - 1):
        for j in range(n_ang):
            a, b, c, d = ir * stride + j, (ir + 1) * stride + j, (ir + 1) * stride + (j + 1), ir * stride + (j + 1)
            tris.extend(((a, c, b), (a, d, c)))
    pos = np.asarray(verts, dtype=np.float32)
    idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
    disc = MeshPart(f"{name}_disc", "item_cracker_gold", pos, recompute_smooth_normals(pos, idx), idx, uvs=np.asarray(uvs, dtype=np.float32), colors=np.asarray(cols, dtype=np.float32))
    parts.append(disc)

    # Crisp fine salt specks glinting on the exposed buttery Ritz rim!
    for k, (sx, sz) in enumerate(((-0.22, +0.08), (-0.10, +0.22), (+0.10, +0.21), (+0.22, +0.06), (+0.18, -0.15))):
        ang_s = math.atan2(sz, sx)
        rb_s, _ = _bound_r(ang_s)
        if math.hypot(sx, sz) < rb_s - 0.025:
            parts.append(rounded_box(0.011, 0.007, 0.011, bevel=0.002, segs=1, name=f"{name}_salt_{k}", material="item_candy_white").rotate_xyz(ry=k * 31.0, degrees=True).translate(sx, 0.053, sz))
    return parts


def _build_swirl_cracker(name: str, stage: int) -> SceneBuilder:
    """Round 22-tooth scalloped Ritz cracker topped with sharp star-fluted Cheez/Steak Whiz ribbon spiral (`373..376`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []
    parts.extend(_ritz_disc(name, stage))

    # Sharp 5-ridge wide flat star-nozzle ribbon spiral (ZERO smooth turd coil!) sitting snugly on top of the Ritz cracker!
    swirl_kw = dict(
        n_flutes=10,
        flute_depth=0.48,
        width_ratio=1.52,
        height_ratio=0.60,
        samples_per_seg=7,
        radial_segs=52,
        valley_rgb=(0.36, 0.22, 0.14),
        mid_rgb=(0.86, 0.72, 0.58),
        crest_rgb=(1.00, 0.95, 0.86),
        y_floor=0.048,
        material="item_pb_swirl",
    )

    if stage == 1:
        # Full 1.5-turn star-fluted ribbon swirl centered on the buttery Ritz cracker (`373:fdcraks1`)!
        pts = [
            (-0.16, 0.072, +0.08),
            (-0.18, 0.074, -0.06),
            (-0.08, 0.078, -0.17),
            (+0.08, 0.078, -0.16),
            (+0.18, 0.074, -0.04),
            (+0.13, 0.078, +0.10),
            (-0.01, 0.088, +0.13),
            (-0.11, 0.098, +0.04),
            (-0.06, 0.108, -0.06),
            (+0.05, 0.114, -0.04),
            (+0.04, 0.122, +0.03),
        ]
        rads = [0.042, 0.048, 0.050, 0.050, 0.048, 0.048, 0.046, 0.044, 0.042, 0.038, 0.032]
        parts.append(extrude_star_piped_ribbon(pts, rads, start_pinch=True, end_pinch=True, name=f"{name}_swirl", **swirl_kw))
    elif stage == 2:
        pts = [
            (-0.16, 0.072, +0.06),
            (-0.17, 0.074, -0.08),
            (-0.05, 0.078, -0.16),
            (+0.10, 0.078, -0.14),
            (+0.17, 0.074, -0.02),
            (+0.11, 0.080, +0.10),
            (-0.03, 0.092, +0.10),
            (-0.07, 0.104, -0.02),
            (+0.03, 0.112, -0.02),
        ]
        rads = [0.042, 0.048, 0.050, 0.048, 0.046, 0.046, 0.044, 0.040, 0.034]
        parts.append(extrude_star_piped_ribbon(pts, rads, start_pinch=True, end_pinch=True, name=f"{name}_swirl", **swirl_kw))
    elif stage == 3:
        pts = [
            (-0.15, 0.072, +0.06),
            (-0.16, 0.074, -0.07),
            (-0.05, 0.078, -0.15),
            (+0.09, 0.078, -0.12),
            (+0.13, 0.078, +0.00),
            (+0.01, 0.094, +0.06),
            (-0.06, 0.104, -0.02),
        ]
        rads = [0.040, 0.046, 0.048, 0.046, 0.044, 0.042, 0.036]
        parts.append(extrude_star_piped_ribbon(pts, rads, start_pinch=False, end_pinch=True, jagged_start=True, name=f"{name}_swirl", **swirl_kw))
    else:
        # Stage 4 (`376:fdcraks4`): bitten crescent star-ribbon swirl + crisp flaky golden Ritz cracker shards on right!
        pts = [
            (-0.15, 0.072, +0.06),
            (-0.15, 0.074, -0.07),
            (-0.04, 0.078, -0.13),
            (+0.07, 0.076, -0.09),
        ]
        rads = [0.042, 0.046, 0.046, 0.040]
        parts.append(extrude_star_piped_ribbon(pts, rads, start_pinch=False, end_pinch=False, jagged_start=True, jagged_end=True, name=f"{name}_swirl", **swirl_kw))
        for k, (cx, cz, rx, rz, ry) in enumerate((
            (+0.18, +0.01, 0.064, 0.044, 24.0),
            (+0.15, +0.14, 0.052, 0.036, -32.0),
            (+0.04, +0.20, 0.046, 0.032, 48.0),
        )):
            parts.append(_cracker_flake(f"{name}_cr_{k}", cx, cz, rx, rz, 0.026, seed=5.1 + k * 1.7, rgb_lo=(0.82, 0.62, 0.18), rgb_hi=(1.00, 0.94, 0.44), rot_y=ry))

    sc.add("ritz_swirl", parts)
    return _finalize_item(sc, target_span=0.68 if stage <= 3 else 0.58)


def build_fdcraks1() -> SceneBuilder:
    return _build_swirl_cracker("item_fdcraks1", 1)


def build_fdcraks2() -> SceneBuilder:
    return _build_swirl_cracker("item_fdcraks2", 2)


def build_fdcraks3() -> SceneBuilder:
    return _build_swirl_cracker("item_fdcraks3", 3)


def build_fdcraks4() -> SceneBuilder:
    return _build_swirl_cracker("item_fdcraks4", 4)
