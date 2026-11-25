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
    """Glossy whipped white cream splat / squirt oozing from Twinkie filling holes (`382..384`)."""
    parts: List[MeshPart] = []
    # Main bulbous whipped dollop + 2 piped trailing ribbons
    main = ellipsoid(0.088 * scale, 0.044 * scale, 0.072 * scale, u_segs=18, v_segs=12, name=f"{name}_core", material="item_cream_white").translate(cx, 0.036 * scale, cz)
    p = main.positions.astype(np.float64)
    p[:, 1] = np.maximum(p[:, 1], 0.003)
    main.positions = p.astype(np.float32)
    main.normals = recompute_smooth_normals(main.positions, main.indices)
    parts.append(gradient_y_tint(main, (0.84, 0.84, 0.88), (1.00, 1.00, 0.99)))
    for k, (dx, dz, rx, rz) in enumerate((
        (-0.062 * scale, +0.042 * scale, 0.046 * scale, 0.032 * scale),
        (+0.056 * scale, +0.034 * scale, 0.042 * scale, 0.028 * scale),
        (-0.020 * scale, -0.048 * scale, 0.044 * scale, 0.036 * scale),
    )):
        lobe = ellipsoid(rx, 0.032 * scale, rz, u_segs=14, v_segs=10, name=f"{name}_lobe_{k}", material="item_cream_white").translate(cx + dx, 0.028 * scale, cz + dz)
        parts.append(gradient_y_tint(lobe, (0.84, 0.84, 0.88), (1.00, 1.00, 0.99)))
    return parts


def _build_twinkie(name: str, stage: int) -> SceneBuilder:
    """Golden sponge-cake Twinkie with 3 bottom cream injection dots & oozing cream (`382..384`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    # Plump rounded sponge cake loaf (flattened base, toasted golden-brown domed crown)
    if stage == 1:
        x_lo, x_hi = -0.37, +0.37
    elif stage == 2:
        x_lo, x_hi = -0.24, +0.37
    else:
        x_lo, x_hi = -0.02, +0.34

    half_l = 0.5 * (x_hi - x_lo)
    cx_cake = 0.5 * (x_hi + x_lo)
    cake = superellipsoid(half_l, 0.125, 0.148, e1=0.62, e2=0.55, u_segs=32, v_segs=20, name=f"{name}_cake", material="item_twinkie_cake")
    pc = cake.positions.astype(np.float64)
    # Shift so base rests cleanly at y=0, gently flatten bottom crust like baked sponge cake
    pc[:, 1] = np.where(pc[:, 1] < 0.0, pc[:, 1] * 0.42, pc[:, 1] * 1.05) + 0.056
    pc[:, 0] += cx_cake
    if stage >= 2:
        # Concave scalloped bite carved neatly on the LEFT (`x_lo`) end!
        left_mask = pc[:, 0] < (x_lo + 0.08)
        bite_curve = x_lo + 0.045 * np.exp(-((pc[left_mask, 2] / 0.11) ** 2)) + 0.012 * np.sin(pc[left_mask, 2] * 38.0 + pc[left_mask, 1] * 24.0)
        pc[left_mask, 0] = np.maximum(pc[left_mask, 0], bite_curve)
    cake.positions = pc.astype(np.float32)
    cake.normals = recompute_smooth_normals(cake.positions, cake.indices)
    # Warm baked golden top dome -> pale creamy-gold underside & exposed sponge crumb!
    cake = tint_part(
        cake,
        lambda pos, _n: np.clip(
            np.column_stack([
                0.92 + 0.08 * np.clip(pos[:, 1] / 0.18, 0.0, 1.0),
                0.76 + 0.16 * np.clip(pos[:, 1] / 0.18, 0.0, 1.0) - 0.12 * np.clip((pos[:, 1] - 0.13) / 0.06, 0.0, 1.0),
                0.28 + 0.24 * (1.0 - np.clip(pos[:, 1] / 0.16, 0.0, 1.0)),
            ]),
            0.16,
            1.0,
        ),
    )
    parts.append(cake)

    # Iconic 3 white cream injection dimples along the lower-front belly (+Z)!
    hole_xs = (-0.21, +0.02, +0.23) if stage == 1 else ((+0.02, +0.23) if stage == 2 else (+0.16,))
    for k, hx in enumerate(hole_xs):
        dot = ellipsoid(0.036, 0.022, 0.022, u_segs=14, v_segs=10, name=f"{name}_dot_{k}", material="item_cream_white").translate(hx, 0.046, +0.136)
        parts.append(dot)

    if stage == 1:
        # Oozing ribbon of white whipped cream squirting out bottom-left injection hole onto ground (`382:fdtwink1`)!
        ooze_neck = curved_tube(
            [(-0.21, 0.056, +0.125), (-0.21, 0.048, +0.185), (-0.22, 0.034, +0.255)],
            [0.026, 0.030, 0.036],
            samples_per_seg=4,
            radial_segs=12,
            cap_rings=2,
            name=f"{name}_ooze_neck",
            material="item_cream_white",
        )
        parts.append(gradient_y_tint(ooze_neck, (0.86, 0.86, 0.90), (1.00, 1.00, 0.99)))
        parts.extend(_cream_splat(f"{name}_splat", -0.22, +0.275, scale=0.88))
    elif stage == 2:
        # Fluffy whipped cream filling bursting out the bitten LEFT end (`383:fdtwink2`)!
        parts.extend(_cream_splat(f"{name}_bite_cream", x_lo - 0.015, +0.015, scale=1.08))
        plug = ellipsoid(0.055, 0.072, 0.092, u_segs=16, v_segs=12, name=f"{name}_cream_plug", material="item_cream_white").translate(x_lo + 0.022, 0.088, 0.0)
        parts.append(gradient_y_tint(plug, (0.86, 0.86, 0.90), (1.00, 1.00, 0.99)))
    else:
        # Half-eaten right stub with generous whipped cream puff spilling out the bitten left face (`384:fdtwink3`)!
        parts.extend(_cream_splat(f"{name}_bite_cream", x_lo - 0.055, +0.045, scale=1.22))
        plug = ellipsoid(0.062, 0.078, 0.098, u_segs=16, v_segs=12, name=f"{name}_cream_plug", material="item_cream_white").translate(x_lo + 0.012, 0.090, 0.0)
        parts.append(gradient_y_tint(plug, (0.86, 0.86, 0.90), (1.00, 1.00, 0.99)))

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

    # Authentic speckled Spam pink-beige chunky potted meat filling (`stage 1..3`; `stage 4` is completely empty!)
    def _spam_tint(pos: np.ndarray, _n: np.ndarray) -> np.ndarray:
        sp = 0.5 + 0.5 * np.sin(pos[:, 0] * 78.0 + pos[:, 2] * 64.0) * np.cos(pos[:, 1] * 85.0 - pos[:, 0] * 52.0)
        hi = np.clip((pos[:, 1] - 0.04) / 0.22, 0.0, 1.0)
        return np.clip(
            np.column_stack([
                0.76 + 0.16 * hi + 0.08 * sp,
                0.48 + 0.18 * hi + 0.10 * sp,
                0.42 + 0.16 * hi + 0.08 * sp,
            ]),
            0.22,
            1.0,
        )

    if stage <= 3:
        meat_parts: List[MeshPart] = []
        if stage == 1:
            loaf = cylinder(r_can - 0.022, r_can - 0.024, 0.215, radial_segs=32, bevel=0.018, axis="y", name=f"{name}_loaf", material="item_bun_crumb").translate(0.0, 0.132, 0.0)
            pm = loaf.positions.astype(np.float64)
            top = pm[:, 1] > 0.18
            pm[top, 1] += 0.024 * np.sin(pm[top, 0] * 34.0) * np.cos(pm[top, 2] * 36.0) + 0.018 * np.exp(-((pm[top, 0] ** 2 + pm[top, 2] ** 2) / 0.028))
            loaf.positions = pm.astype(np.float32)
            loaf.normals = recompute_smooth_normals(loaf.positions, loaf.indices)
            meat_parts.append(tint_part(loaf, _spam_tint))
            for k, (mx, mz, mr) in enumerate(((-0.08, -0.04, 0.052), (+0.07, +0.05, 0.048), (-0.02, +0.09, 0.046), (+0.09, -0.06, 0.044), (-0.11, +0.06, 0.042))):
                chunk = ellipsoid(mr, mr * 0.62, mr * 0.86, u_segs=12, v_segs=8, name=f"{name}_chunk_{k}", material="item_bun_crumb").translate(mx, 0.236, mz)
                meat_parts.append(tint_part(chunk, _spam_tint))
        elif stage == 2:
            loaf = ellipsoid(0.185, 0.105, 0.178, u_segs=24, v_segs=14, name=f"{name}_loaf", material="item_bun_crumb").translate(-0.032, 0.125, +0.012)
            pm = loaf.positions.astype(np.float64)
            pm[:, 1] = np.maximum(pm[:, 1], 0.026)
            pm[:, 1] += 0.016 * np.sin(pm[:, 0] * 38.0) * np.cos(pm[:, 2] * 38.0)
            loaf.positions = pm.astype(np.float32)
            loaf.normals = recompute_smooth_normals(loaf.positions, loaf.indices)
            meat_parts.append(tint_part(loaf, _spam_tint))
            for k, (mx, mz, mr) in enumerate(((-0.09, -0.02, 0.046), (-0.01, +0.07, 0.044), (+0.05, -0.04, 0.040))):
                meat_parts.append(tint_part(ellipsoid(mr, mr * 0.60, mr * 0.84, u_segs=12, v_segs=8, name=f"{name}_chunk_{k}", material="item_bun_crumb").translate(mx, 0.200, mz), _spam_tint))
        else:
            loaf = ellipsoid(0.135, 0.072, 0.128, u_segs=20, v_segs=12, name=f"{name}_loaf", material="item_bun_crumb").translate(-0.068, 0.088, +0.042)
            pm = loaf.positions.astype(np.float64)
            pm[:, 1] = np.maximum(pm[:, 1], 0.026)
            loaf.positions = pm.astype(np.float32)
            loaf.normals = recompute_smooth_normals(loaf.positions, loaf.indices)
            meat_parts.append(tint_part(loaf, _spam_tint))
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
    """Single chamfered molded milk-chocolate bar rectangle with embossed beveled top."""
    sq = rounded_box(0.095, 0.042, 0.088, bevel=0.009, segs=2, name=name, material="item_steak_paste")
    return gradient_y_tint(sq, (0.34, 0.16, 0.08), (0.64, 0.34, 0.18), noise_amp=0.02).translate(cx, cy, cz)


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
    """Curled-back shiny silver-white inner foil flaps peeled open around the bitten left mouth."""
    flaps: List[MeshPart] = []
    for sgn, tag in ((-1.0, "back"), (+1.0, "front")):
        flap = curved_tube(
            [
                (x_edge + 0.02, 0.072, sgn * 0.115),
                (x_edge - 0.025, 0.105, sgn * 0.155),
                (x_edge - 0.070, 0.088, sgn * 0.178),
            ],
            [0.036, 0.032, 0.016],
            samples_per_seg=4,
            radial_segs=10,
            cap_rings=2,
            name=f"{name}_{tag}",
            material="item_candy_white",
        )
        # Flatten ribbon thin into foil sheet
        p = flap.positions.astype(np.float64)
        p[:, 1] = 0.085 + (p[:, 1] - 0.085) * 0.45
        flap.positions = p.astype(np.float32)
        flap.normals = recompute_smooth_normals(flap.positions, flap.indices)
        flaps.append(gradient_y_tint(flap, (0.78, 0.80, 0.86), (0.99, 0.99, 1.00)))
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


def _build_pretzel(name: str, stage: int) -> SceneBuilder:
    """Classic glossy baked brown twisted hard pretzel & coarse salt crystals (`264..266`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    if stage == 1:
        # True 3D pretzel dough knot: heart-shaped outer ring + crossed over/under twisted arms tucked on shoulders!
        outer_pts: List[Tuple[float, float, float]] = []
        n = 36
        for i in range(n + 1):
            a = 2.0 * math.pi * i / n
            # Classic pretzel heart-oval: wider shoulders at top (-Z), gently tapered chin at bottom (+Z)
            w = 0.34 - 0.05 * math.sin(a)
            x = w * math.cos(a)
            z = 0.255 * math.sin(a) + 0.045 * math.cos(2.0 * a)
            y = 0.046 + 0.006 * math.sin(a * 2.0)
            outer_pts.append((x, y, z))
        outer = curved_tube(outer_pts, [0.042] * len(outer_pts), samples_per_seg=2, radial_segs=14, cap_start=False, cap_end=False, name=f"{name}_outer", material="item_pretzel_crust")
        # Arm 1 (tucked cleanly into lower rim -> crosses UNDER center -> upper-right shoulder)
        arm1 = curved_tube(
            [(-0.06, 0.046, +0.22), (-0.03, 0.044, +0.10), (0.00, 0.044, -0.02), (+0.11, 0.064, -0.14), (+0.19, 0.058, -0.19)],
            [0.036, 0.036, 0.036, 0.036, 0.034],
            samples_per_seg=4,
            radial_segs=14,
            cap_rings=2,
            name=f"{name}_arm1",
            material="item_pretzel_crust",
        )
        # Arm 2 (tucked cleanly into lower rim -> crosses OVER center -> upper-left shoulder)
        arm2 = curved_tube(
            [(+0.06, 0.046, +0.22), (+0.03, 0.064, +0.10), (0.00, 0.078, -0.02), (-0.11, 0.068, -0.14), (-0.19, 0.058, -0.19)],
            [0.036, 0.036, 0.038, 0.036, 0.034],
            samples_per_seg=4,
            radial_segs=14,
            cap_rings=2,
            name=f"{name}_arm2",
            material="item_pretzel_crust",
        )
        for p in (outer, arm1, arm2):
            parts.append(gradient_y_tint(p, (0.86, 0.72, 0.56), (1.00, 0.96, 0.84), noise_amp=0.04))
        parts.extend(
            _salt_crystals(
                name,
                (
                    (-0.27, 0.088, -0.08),
                    (-0.18, 0.094, -0.19),
                    (0.00, 0.114, -0.02),
                    (+0.18, 0.094, -0.18),
                    (+0.29, 0.086, +0.02),
                    (+0.14, 0.086, +0.21),
                    (-0.15, 0.086, +0.20),
                    (-0.29, 0.086, +0.06),
                ),
            )
        )
    elif stage == 2:
        # Three broken curved pretzel shards + crumbs matching `265:fdpretz2`!
        s1 = curved_tube([(-0.28, 0.044, +0.08), (-0.25, 0.046, -0.06), (-0.14, 0.046, -0.18)], [0.038, 0.040, 0.036], samples_per_seg=4, radial_segs=12, cap_rings=2, name=f"{name}_s1", material="item_pretzel_crust")
        s2 = curved_tube([(-0.04, 0.044, +0.16), (-0.01, 0.052, +0.02), (+0.08, 0.048, -0.12)], [0.038, 0.042, 0.036], samples_per_seg=4, radial_segs=12, cap_rings=2, name=f"{name}_s2", material="item_pretzel_crust")
        s2b = curved_tube([(-0.02, 0.050, +0.02), (-0.055, 0.048, -0.02), (-0.09, 0.046, -0.06)], [0.034, 0.032, 0.030], samples_per_seg=3, radial_segs=10, cap_rings=2, name=f"{name}_s2b", material="item_pretzel_crust")
        s3 = curved_tube([(+0.08, 0.044, +0.21), (+0.22, 0.046, +0.18), (+0.26, 0.046, +0.02), (+0.25, 0.044, -0.10)], [0.036, 0.040, 0.040, 0.034], samples_per_seg=4, radial_segs=12, cap_rings=2, name=f"{name}_s3", material="item_pretzel_crust")
        for p in (s1, s2, s2b, s3):
            parts.append(gradient_y_tint(p, (0.86, 0.72, 0.56), (1.00, 0.96, 0.84), noise_amp=0.04))
        parts.extend(_salt_crystals(name, ((-0.25, 0.086, -0.05), (-0.01, 0.092, +0.02), (+0.24, 0.086, +0.12))))
    else:
        # Stage 3 (`266:fdpretz3`): central Y-knot fragment + 4 tiny scattered crumbs!
        y1 = curved_tube([(-0.09, 0.046, +0.18), (-0.03, 0.052, +0.02), (+0.07, 0.048, -0.16)], [0.044, 0.048, 0.040], samples_per_seg=4, radial_segs=12, cap_rings=2, name=f"{name}_y1", material="item_pretzel_crust")
        y2 = curved_tube([(-0.03, 0.050, +0.02), (-0.06, 0.048, -0.035), (-0.09, 0.046, -0.09)], [0.040, 0.037, 0.034], samples_per_seg=3, radial_segs=10, cap_rings=2, name=f"{name}_y2", material="item_pretzel_crust")
        parts.extend([gradient_y_tint(p, (0.86, 0.72, 0.56), (1.00, 0.96, 0.84), noise_amp=0.04) for p in (y1, y2)])
        for k, (cx, cz, r) in enumerate(((-0.14, -0.20, 0.032), (+0.20, -0.08, 0.038), (+0.18, +0.12, 0.034), (+0.10, +0.19, 0.026))):
            c = ellipsoid(r, 0.028, r * 0.82, u_segs=10, v_segs=8, name=f"{name}_cr_{k}", material="item_pretzel_crust").translate(cx, 0.028, cz)
            parts.append(gradient_y_tint(c, (0.86, 0.72, 0.56), (1.00, 0.94, 0.80)))
        parts.extend(_salt_crystals(name, ((-0.03, 0.098, +0.02), (+0.05, 0.088, -0.12))))

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
    """High-res toasted golden square saltine with REAL recessed 3D docker dimples & crisp concave ant-bites!"""
    parts: List[MeshPart] = []
    half = 0.255
    n_g = 24
    holes = [
        (ix * 0.135, iz * 0.135) for ix in (-1, 0, 1) for iz in (-1, 0, 1)
    ] + [
        (ix * 0.068, iz * 0.068) for ix in (-1, 1) for iz in (-1, 1)
    ]

    # Build a dense subdivided closed grid so docker pinholes sink cleanly downward & ant bites carve crisp notches!
    verts, uvs, tris = [], [], []
    # Top grid (0..n_g x 0..n_g), then bottom grid
    for is_top in (True, False):
        for iz in range(n_g + 1):
            vz = iz / float(n_g)
            z0 = -half + 2.0 * half * vz
            for ix in range(n_g + 1):
                ux = ix / float(n_g)
                x0 = -half + 2.0 * half * ux
                x, z = x0, z0
                # Scalloped saltine perimeter serration
                if ix in (0, n_g):
                    x -= math.copysign(0.008 * (0.5 + 0.5 * math.cos(z0 * 50.0)), x0)
                if iz in (0, n_g):
                    z -= math.copysign(0.008 * (0.5 + 0.5 * math.cos(x0 * 50.0)), z0)
                # Carve crisp concave bite notches inward toward center!
                for bx, bz, br in bite_notches:
                    d = math.hypot(x - bx, z - bz)
                    if d < br:
                        nrm = max(1e-5, math.hypot(x, z))
                        push = (br - d) * 1.05
                        x -= (x / nrm) * push
                        z -= (z / nrm) * push
                edge = max(abs(x0), abs(z0)) / half
                if is_top:
                    # Subtle blister puff between docker holes + REAL recessed 3D docker dimples!
                    dimple = 0.0
                    for hx, hz in holes:
                        dh = math.hypot(x - hx, z - hz)
                        if dh < 0.032:
                            dimple = max(dimple, 0.026 * math.exp(-((dh / 0.015) ** 2)))
                    blister = 0.008 * (1.0 - edge * edge)
                    y = 0.056 + blister - dimple - (0.010 if edge > 0.96 else 0.0)
                else:
                    y = 0.012 if edge > 0.96 else 0.004
                verts.append((x, y, z))
                uvs.append((ux * 1.5, vz * 1.5))

    stride = n_g + 1
    off = (n_g + 1) * stride
    # Top face (+Y outward), bottom face (-Y outward)
    for iz in range(n_g):
        for ix in range(n_g):
            a, b, c, d = iz * stride + ix, (iz + 1) * stride + ix, (iz + 1) * stride + (ix + 1), iz * stride + (ix + 1)
            tris.extend(((a, b, c), (a, c, d)))
            a2, b2, c2, d2 = off + a, off + b, off + c, off + d
            tris.extend(((a2, c2, b2), (a2, d2, c2)))
    # Stitch 4 side walls around perimeter!
    for i in range(n_g):
        # z = -half edge (iz=0)
        t0, t1, b0, b1 = i, i + 1, off + i, off + i + 1
        tris.extend(((t0, t1, b1), (t0, b1, b0)))
        # z = +half edge (iz=n_g)
        r = n_g * stride
        t0, t1, b0, b1 = r + i, r + i + 1, off + r + i, off + r + i + 1
        tris.extend(((t0, b1, t1), (t0, b0, b1)))
        # x = -half edge (ix=0)
        t0, t1, b0, b1 = i * stride, (i + 1) * stride, off + i * stride, off + (i + 1) * stride
        tris.extend(((t0, b1, t1), (t0, b0, b1)))
        # x = +half edge (ix=n_g)
        t0, t1, b0, b1 = i * stride + n_g, (i + 1) * stride + n_g, off + i * stride + n_g, off + (i + 1) * stride + n_g
        tris.extend(((t0, t1, b1), (t0, b1, b0)))

    pos = np.asarray(verts, dtype=np.float32)
    idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
    slab = MeshPart(f"{name}_slab", "item_cracker_gold", pos, recompute_smooth_normals(pos, idx), idx, uvs=np.asarray(uvs, dtype=np.float32))
    # Darken inside the recessed docker pinholes + warm toasted golden saltine gradient!
    def _saltine_color(p: np.ndarray, _n: np.ndarray) -> np.ndarray:
        dimp = np.zeros(len(p), dtype=np.float64)
        for hx, hz in holes:
            dimp = np.maximum(dimp, np.exp(-(((p[:, 0] - hx) ** 2 + (p[:, 2] - hz) ** 2) / (0.017 ** 2))))
        top_w = np.clip((p[:, 1] - 0.025) / 0.025, 0.0, 1.0)
        dk = dimp * top_w
        r_f = np.clip(np.hypot(p[:, 0], p[:, 2]) / 0.34, 0.0, 1.0)
        return np.clip(
            np.column_stack([
                (0.98 - 0.14 * r_f) * (1.0 - 0.52 * dk),
                (0.84 - 0.22 * r_f) * (1.0 - 0.62 * dk),
                (0.56 - 0.20 * r_f) * (1.0 - 0.72 * dk),
            ]),
            0.12,
            1.0,
        )
    parts.append(tint_part(slab, _saltine_color))

    for k, (sx, sz) in enumerate(((-0.09, -0.04), (+0.11, -0.08), (+0.03, +0.10), (-0.12, +0.09))):
        if any(math.hypot(sx - bx, sz - bz) < br * 0.95 for bx, bz, br in bite_notches):
            continue
        parts.append(rounded_box(0.014, 0.008, 0.014, bevel=0.002, segs=1, name=f"{name}_salt_{k}", material="item_candy_white").translate(sx, 0.064, sz))
    return parts


def _build_saltine(name: str, stage: int) -> SceneBuilder:
    """Diamond-angled golden square saltine cracker through 4 eaten stages (`369..372`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    if stage == 1:
        parts.extend(_saltine_slab(name))
    elif stage == 2:
        parts.extend(_saltine_slab(name, bite_notches=((-0.24, +0.12, 0.11), (+0.23, -0.14, 0.10))))
        parts.append(rounded_box(0.048, 0.038, 0.042, bevel=0.008, segs=1, name=f"{name}_cr0", material="item_cracker_gold").translate(-0.29, 0.022, +0.18))
    elif stage == 3:
        parts.extend(_saltine_slab(name, bite_notches=((-0.22, +0.20, 0.19), (+0.22, +0.16, 0.18), (-0.18, -0.22, 0.14))))
        for k, (cx, cz, s) in enumerate(((-0.27, +0.21, 0.052), (+0.28, +0.14, 0.046), (+0.05, +0.29, 0.042))):
            parts.append(rounded_box(s, 0.036, s * 0.85, bevel=0.007, segs=1, name=f"{name}_cr_{k}", material="item_cracker_gold").rotate_xyz(ry=k * 42.0, degrees=True).translate(cx, 0.020, cz))
    else:
        # Stage 4 (`372:fdcrakr4`): 2 small triangular/jagged saltine corner fragments + 4 golden crumbs!
        for k, (cx, cz, sx, sz, ry) in enumerate((
            (+0.08, -0.10, 0.16, 0.12, 22.0),
            (+0.11, +0.11, 0.15, 0.12, -18.0),
            (-0.10, -0.02, 0.068, 0.056, 35.0),
            (-0.18, +0.06, 0.048, 0.040, 12.0),
            (-0.02, +0.14, 0.042, 0.038, -28.0),
        )):
            frag = rounded_box(sx, 0.044, sz, bevel=0.009, segs=2, name=f"{name}_frag_{k}", material="item_cracker_gold").rotate_xyz(ry=ry, degrees=True).translate(cx, 0.024, cz)
            parts.append(gradient_y_tint(frag, (0.76, 0.52, 0.24), (0.98, 0.78, 0.44)))

    # Rotate 45 deg into iconic diamond orientation matching `369..372`!
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
# 6. ROUND SCALLOPED RITZ CRACKER + BROWN SWIRL TOPPING (`373..376:fdcraks1..4`)
# ---------------------------------------------------------------------------

def _ritz_disc(name: str, stage: int) -> List[MeshPart]:
    """Round buttery golden Ritz-style scalloped cracker with 18 fluted rim scallops & bite notches."""
    parts: List[MeshPart] = []
    r_base = 0.285
    disc = cylinder(r_base, r_base * 0.98, 0.052, radial_segs=54, bevel=0.008, axis="y", name=f"{name}_disc", material="item_cracker_gold").translate(0.0, 0.028, 0.0)
    p = disc.positions.astype(np.float64)
    rad = np.hypot(p[:, 0], p[:, 2])
    ang = np.arctan2(p[:, 2], p[:, 0])
    # Classic 18-lobe scalloped buttery Ritz rim!
    rim_w = np.clip((rad - 0.16) / 0.125, 0.0, 1.0)
    scallop = 1.0 + 0.055 * np.cos(ang * 18.0) * rim_w
    # Stage bite notches along outer edge
    bite_scale = np.ones_like(ang)
    if stage == 2:
        for ba, bw, bd in ((+1.95, 0.35, 0.20), (+0.65, 0.32, 0.16)):
            da = np.arctan2(np.sin(ang - ba), np.cos(ang - ba))
            bite_scale -= bd * np.exp(-((da / bw) ** 2))
    elif stage == 3:
        for ba, bw, bd in ((+0.35, 0.55, 0.32), (+1.85, 0.45, 0.28), (-0.85, 0.40, 0.22)):
            da = np.arctan2(np.sin(ang - ba), np.cos(ang - ba))
            bite_scale -= bd * np.exp(-((da / bw) ** 2))
    elif stage == 4:
        # Half-eaten crescent wedge (`376:fdcraks4`)
        for ba, bw, bd in ((+0.15, 0.90, 0.58), (+1.40, 0.55, 0.38), (-1.10, 0.55, 0.42)):
            da = np.arctan2(np.sin(ang - ba), np.cos(ang - ba))
            bite_scale -= bd * np.exp(-((da / bw) ** 2))
    scale_r = scallop * np.clip(bite_scale, 0.28, 1.10)
    p[:, 0] *= scale_r
    p[:, 2] *= scale_r
    disc.positions = p.astype(np.float32)
    disc.normals = recompute_smooth_normals(disc.positions, disc.indices)
    # Recessed toasted docker dimples painted flush into the Ritz cracker rim (no raised chocolate dots!)
    dh_coords = [(0.215 * math.cos(k * 2.0 * math.pi / 7.0 + 0.3), 0.215 * math.sin(k * 2.0 * math.pi / 7.0 + 0.3)) for k in range(7)]
    def _ritz_tint(pos: np.ndarray, _n: np.ndarray) -> np.ndarray:
        hi = np.clip((pos[:, 1] - 0.005) / 0.050, 0.0, 1.0)
        dk = np.zeros(len(pos), dtype=np.float64)
        for hx, hz in dh_coords:
            dk = np.maximum(dk, np.exp(-(((pos[:, 0] - hx) ** 2 + (pos[:, 2] - hz) ** 2) / (0.022 ** 2))))
        dk *= np.clip((pos[:, 1] - 0.038) / 0.012, 0.0, 1.0)
        return np.clip(
            np.column_stack([
                (0.86 + 0.14 * hi) * (1.0 - 0.44 * dk),
                (0.64 + 0.28 * hi) * (1.0 - 0.56 * dk),
                (0.16 + 0.18 * hi) * (1.0 - 0.65 * dk),
            ]),
            0.12,
            1.0,
        )
    parts.append(tint_part(disc, _ritz_tint))
    return parts


def _build_swirl_cracker(name: str, stage: int) -> SceneBuilder:
    """Round scalloped Ritz cracker topped with coiled brown peanut-butter/steak-whiz swirl (`373..376`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []
    parts.extend(_ritz_disc(name, stage))

    # Coiled glossy piped brown swirl resting squarely on top of the golden Ritz cracker!
    if stage == 1:
        pts = [
            (-0.18, 0.088, +0.06),
            (-0.19, 0.092, -0.08),
            (-0.08, 0.098, -0.18),
            (+0.09, 0.100, -0.17),
            (+0.19, 0.094, -0.05),
            (+0.15, 0.096, +0.10),
            (+0.01, 0.106, +0.15),
            (-0.10, 0.116, +0.07),
            (-0.07, 0.126, -0.05),
            (+0.04, 0.138, -0.04),
            (+0.06, 0.152, +0.03),
        ]
        rads = [0.042, 0.048, 0.050, 0.050, 0.048, 0.048, 0.046, 0.044, 0.042, 0.036, 0.022]
    elif stage == 2:
        pts = [
            (-0.17, 0.088, +0.04),
            (-0.17, 0.092, -0.09),
            (-0.05, 0.098, -0.17),
            (+0.11, 0.098, -0.15),
            (+0.18, 0.092, -0.02),
            (+0.12, 0.098, +0.11),
            (-0.03, 0.110, +0.12),
            (-0.07, 0.124, -0.01),
            (+0.03, 0.136, -0.02),
        ]
        rads = [0.040, 0.046, 0.048, 0.048, 0.046, 0.046, 0.044, 0.040, 0.026]
    elif stage == 3:
        pts = [
            (-0.15, 0.088, +0.05),
            (-0.16, 0.092, -0.08),
            (-0.05, 0.098, -0.16),
            (+0.09, 0.098, -0.13),
            (+0.13, 0.094, 0.00),
            (+0.02, 0.112, +0.07),
            (-0.06, 0.122, -0.02),
        ]
        rads = [0.038, 0.045, 0.046, 0.046, 0.042, 0.040, 0.028]
    else:
        # Stage 4 (`376:fdcraks4`): truncated crescent swirl + scattered Ritz crumbs on right!
        pts = [
            (-0.15, 0.088, +0.05),
            (-0.15, 0.092, -0.07),
            (-0.04, 0.096, -0.14),
            (+0.06, 0.094, -0.10),
        ]
        rads = [0.038, 0.044, 0.044, 0.034]
        for k, (cx, cz, s) in enumerate(((+0.18, +0.02, 0.065), (+0.15, +0.14, 0.052), (+0.04, +0.20, 0.048))):
            cr = rounded_box(s, 0.038, s * 0.85, bevel=0.008, segs=1, name=f"{name}_cr_{k}", material="item_cracker_gold").rotate_xyz(ry=k * 33.0, degrees=True).translate(cx, 0.022, cz)
            parts.append(gradient_y_tint(cr, (0.86, 0.64, 0.16), (1.00, 0.92, 0.34)))

    swirl = curved_tube(pts, rads, samples_per_seg=4, radial_segs=14, cap_rings=3, name=f"{name}_swirl", material="item_pb_swirl")
    parts.append(gradient_y_tint(swirl, (0.56, 0.32, 0.18), (0.94, 0.64, 0.42), noise_amp=0.03))

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
