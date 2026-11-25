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
            MeshPart,
            SceneBuilder,
            curved_tube,
            cylinder,
            ellipsoid,
            ensure_outward_winding,
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
            MeshPart,
            SceneBuilder,
            curved_tube,
            cylinder,
            ellipsoid,
            ensure_outward_winding,
            lathe,
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
    hue_shift: Tuple[float, float, float] = (1.0, 1.0, 1.0),
    segs: int = 10,
) -> MeshPart:
    """Build one realistic broad V-ribbed, gracefully arching 3D grass blade with rich root-to-tip chlorophyll gradient."""
    bx, bz = base_xz
    cy, sy = math.cos(yaw), math.sin(yaw)
    verts: List[Tuple[float, float, float]] = []
    uvs: List[Tuple[float, float]] = []
    cols: List[Tuple[float, float, float]] = []
    tris: List[Tuple[int, int, int]] = []

    # 3 vertices across blade width (left edge, recessed V-midrib center, right edge) -> genuine 3D cross-section!
    for i in range(segs + 1):
        u = i / segs
        horiz = lean * (u ** 1.28) + droop * (u ** 2.45)
        vy = base_y + height * (u - 0.22 * max(0.0, droop) * (u ** 2.8))
        # Realistic tapered grass blade: full fleshy base -> graceful lance taper -> sharp tip
        w = width * (1.0 - 0.88 * (u ** 1.65))
        if i == segs:
            w = 0.0022
        tw = twist * u
        ct, st = math.cos(tw), math.sin(tw)
        v_crease = 0.26 * w

        cx_w = bx + sy * horiz
        cz_w = bz + cy * horiz
        # Rich sunlit lawn grass gradient: deep emerald base -> vibrant meadow green -> bright lime-gold tips!
        r_c = (0.30 + 0.64 * (u ** 1.10)) * hue_shift[0]
        g_c = (0.62 + 0.38 * (u ** 0.70)) * hue_shift[1]
        b_c = (0.22 + 0.16 * (u ** 1.40)) * hue_shift[2]

        for j, (s_w, rib) in enumerate(((-0.5, 0.0), (0.0, -v_crease), (+0.5, 0.0))):
            lat = s_w * w
            px = cx_w + cy * (lat * ct) + sy * (rib - lat * st * 0.25)
            pz = cz_w - sy * (lat * ct) + cy * (rib - lat * st * 0.25)
            py = max(0.0, vy + abs(s_w) * v_crease * 0.55)
            verts.append((px, py, pz))
            uvs.append((j * 0.5, u * 2.2))
            rib_shade = 0.86 if j == 1 else 1.00
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
    col_arr = np.clip(np.asarray(cols, dtype=np.float32), 0.10, 1.0)
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
    seed: int = 11,
) -> SceneBuilder:
    """Build a dense, lush, soil-free realistic 3D grass tuft rooted directly on Y = 0.0."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    rng = np.random.default_rng(seed)

    for k in range(n_blades):
        rad_u = math.sqrt(float(rng.uniform(0.01, 1.0)))
        ang = float(rng.uniform(0.0, 2.0 * math.pi))
        bx = spread_x * rad_u * math.cos(ang)
        bz = spread_z * rad_u * math.sin(ang)
        # Inner blades stand taller & prouder; outer rim blades arch gracefully outward!
        height = float(rng.uniform(h_min, h_max)) * (1.12 - 0.30 * rad_u)
        width = float(rng.uniform(0.056, 0.088)) * width_scale * (0.85 + 0.25 * (height / h_max))
        if abs(lean_bias_x) > 0.02:
            # Wind-swept tuft (`grass1`): fan strongly toward lean_bias_x with natural organic spread
            base_yaw = math.atan2(lean_bias_x + 0.45 * bx, 0.35 * bz + 0.05)
            out_yaw = base_yaw + float(rng.uniform(-0.75, 0.75))
        else:
            out_yaw = math.atan2(bx, bz + 1e-4) + float(rng.uniform(-0.52, 0.52))
        lean = float(rng.uniform(0.07, 0.22)) * (0.45 + 0.85 * rad_u)
        droop = float(rng.uniform(0.06, 0.26)) * (0.50 + 0.70 * rad_u)
        twist = float(rng.uniform(-0.60, 0.60))
        hue = (float(rng.uniform(0.84, 1.18)), float(rng.uniform(0.92, 1.06)), float(rng.uniform(0.76, 1.08)))
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
                hue_shift=hue,
                segs=9,
            ),
        )
    return _finalize_item(sc, target_span=0.94)


def build_grass1() -> SceneBuilder:
    """Lush wind-swept small backyard grass tuft (`1320:grass1`)."""
    return _build_grass_cluster("item_grass1", n_blades=24, spread_x=0.16, spread_z=0.12, h_min=0.40, h_max=0.72, width_scale=1.08, lean_bias_x=-0.22, seed=1320)


def build_grass3() -> SceneBuilder:
    """Full fanning medium backyard meadow grass tuft (`1322:grass3`)."""
    return _build_grass_cluster("item_grass3", n_blades=42, spread_x=0.26, spread_z=0.19, h_min=0.34, h_max=0.68, width_scale=1.02, lean_bias_x=0.0, seed=1322)


def build_grassbig1() -> SceneBuilder:
    """Dense broad wild lawn grass clump with layered arching blades (`1325:grassbig1`)."""
    return _build_grass_cluster("item_grassbig1", n_blades=72, spread_x=0.38, spread_z=0.28, h_min=0.36, h_max=0.78, width_scale=0.98, lean_bias_x=0.0, seed=1325)


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


def build_clover1() -> SceneBuilder:
    """Realistic lucky 4-leaf clover with separated notched heart leaflets & sweeping curved stem (`404:clover1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_clover1")

    # Visible slender sweeping stem arching cleanly down-right just like `404:clover1`!
    stem = curved_tube(
        [(+0.24, 0.00, +0.26), (+0.15, 0.06, +0.16), (+0.05, 0.15, +0.06), (0.00, 0.23, 0.00)],
        [0.015, 0.013, 0.012, 0.015],
        samples_per_seg=5,
        radial_segs=12,
        cap_rings=2,
        name="clover_stem",
        material="item_stem_green",
    )
    sc.add("clover", stem)

    # 4 distinct heart leaflets with clear negative-space gaps between them!
    for k in range(4):
        ang = k * (0.5 * math.pi) + 0.35
        pitch = 0.14 + 0.05 * (k % 2)
        lf = (
            _heart_leaflet(f"clover_leaf_{k}", scale=0.98 - 0.05 * (k % 2))
            .rotate_xyz(rx=pitch, ry=ang)
            .translate(0.0, 0.232 + 0.005 * (k % 2), 0.0)
        )
        sc.add("clover", lf)
    return _finalize_item(sc, target_span=0.90)


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
    """Iconic 1998 Groucho-Marx Disguise Daisy (oversized shades, big nose, bushy mustache, `420:dflower1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_dflower1")
    hx, hy, hz = _build_daisy_base(sc, petal_count=9, tilt_deg=40.0)

    face_parts: List[MeshPart] = []
    # Dark brown inner flower cushion behind the big Groucho disguise.
    disc = superellipsoid(0.105, 0.040, 0.105, e1=0.80, e2=0.96, u_segs=22, v_segs=12, name="dflower_center", material="item_dark_stitch").translate(0.0, 0.010, 0.0)
    face_parts.append(disc)

    # Bold glossy black Groucho sunglasses (oversized dark frames + shiny lenses + catchlight + eyebrows)!
    for side, tag in ((+1.0, "l"), (-1.0, "r")):
        frame = torus_arc(0.060, 0.014, u_segs=22, v_segs=10, plane="xz", name=f"groucho_frame_{tag}", material="gear_dark").translate(side * 0.072, 0.058, -0.036)
        lens = ellipsoid(0.055, 0.012, 0.048, u_segs=18, v_segs=8, name=f"groucho_lens_{tag}", material="eye_pupil").translate(side * 0.072, 0.060, -0.036)
        glint = ellipsoid(0.022, 0.006, 0.012, u_segs=10, v_segs=6, name=f"groucho_glint_{tag}", material="eye_catchlight").rotate_xyz(ry=0.45).translate(side * 0.056, 0.073, -0.048)
        brow = rounded_box(0.088, 0.022, 0.026, bevel=0.007, segs=2, name=f"groucho_brow_{tag}", material="item_dark_stitch").rotate_xyz(ry=-side * 0.16).translate(side * 0.072, 0.064, -0.095)
        face_parts.extend([frame, lens, glint, brow])
    bridge = rounded_box(0.052, 0.020, 0.022, bevel=0.005, segs=2, name="groucho_bridge", material="gear_dark").translate(0.0, 0.064, -0.042)
    face_parts.append(bridge)

    # Big bulbous pinkish-peach plastic Groucho nose + thick bushy dark mustache (`420:dflower1`)!
    nose = (
        superellipsoid(0.052, 0.062, 0.074, e1=0.76, e2=0.82, u_segs=20, v_segs=14, name="groucho_nose", material="item_groucho_nose")
        .rotate_xyz(rx=0.25)
        .translate(0.0, 0.078, +0.020)
    )
    face_parts.append(nose)

    for side, tag in ((+1.0, "l"), (-1.0, "r")):
        stache = curved_tube(
            [(0.0, 0.052, +0.068), (side * 0.058, 0.048, +0.092), (side * 0.118, 0.036, +0.078)],
            [0.038, 0.042, 0.022],
            samples_per_seg=4,
            radial_segs=14,
            cap_rings=3,
            name=f"groucho_mustache_{tag}",
            material="item_dark_stitch",
        )
        face_parts.append(stache)

    for p in face_parts:
        sc.add("flower", p.rotate_xyz(rx=40.0, degrees=True).translate(hx, hy, hz))
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
