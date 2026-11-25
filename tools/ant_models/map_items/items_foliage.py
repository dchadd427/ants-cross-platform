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
            recompute_smooth_normals,
            rounded_box,
            sphere,
            superellipsoid,
            torus_arc,
        )
        from .item_materials import gradient_y_tint, register_item_materials, tint_part
        from .items_plants import _build_daisy_base, _build_groucho_daisy
        from .items_toys import _finalize_item
    except ImportError:
        from map_items.item_materials import gradient_y_tint, register_item_materials, tint_part  # type: ignore
        from map_items.items_plants import _build_daisy_base, _build_groucho_daisy  # type: ignore
        from map_items.items_toys import _finalize_item  # type: ignore
        from mesh_core import (  # type: ignore
            MeshPart,
            SceneBuilder,
            curved_tube,
            ellipsoid,
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
    arch: float = 0.035,
    vein_glow: float = 0.35,
) -> MeshPart:
    """Build one sculpted organic leaf lobe extending from (0,0,0) along +Z with raised midrib & subtle venation."""
    def _shape(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        u = np.clip((p[:, 2] + 0.5 * length) / max(1e-5, length), 0.0, 1.0)
        # Teardrop/ovate leaf silhouette: rounded lower belly tapering cleanly to pointed tip!
        profile = np.sin(u * math.pi) ** 0.72 * (1.14 - 0.38 * u)
        if serration > 0.0:
            profile = profile * (1.0 + serration * np.sin(u * math.pi * 10.0) * np.sin(u * math.pi))
        p[:, 0] *= np.maximum(0.03, profile)
        p[:, 2] = u * length
        # Gentle organic arch + subtle V-trough cupping along central midrib
        p[:, 1] += arch * np.sin(u * math.pi * 0.92) + 0.016 * np.abs(p[:, 0]) / max(0.02, width * 0.5)
        # Fine lateral rib waves
        p[:, 1] += 0.004 * np.sin((u * 8.0 - np.abs(p[:, 0]) * 18.0) * math.pi) * np.sin(u * math.pi)
        return p

    blade = ellipsoid(width * 0.5, 0.008, length * 0.5, u_segs=26, v_segs=10, name=name, material=material).deform(_shape)

    def _leaf_veins(pos: np.ndarray, _nrm: np.ndarray) -> np.ndarray:
        u = np.clip(pos[:, 2] / max(1e-5, length), 0.0, 1.0)
        mid = np.exp(-((pos[:, 0] / max(0.012, width * 0.08)) ** 2))[:, None]
        lat = np.maximum(0.0, np.cos((u * 7.0 - np.abs(pos[:, 0]) * 14.0) * math.pi)) ** 8
        vein = np.clip(mid + 0.38 * lat[:, None] * (1.0 - u[:, None] * 0.5), 0.0, 1.0)
        base = np.array([[0.74, 0.96, 0.44]], dtype=np.float64) * (0.82 + 0.18 * u[:, None])
        glow = np.array([[0.98, 1.00, 0.72]], dtype=np.float64)
        return np.clip(base * (1.0 - vein_glow * vein) + glow * (vein_glow * vein), 0.25, 1.0)

    return tint_part(blade, _leaf_veins)


def _build_palmate_maple(name: str, *, yaw_deg: float, tilt_deg: float, spread: float = 1.0) -> SceneBuilder:
    """Build realistic 3-lobed palmate bright-green maple/currant leaf (`323:leaf1` & `324:leaf2`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    # Slender petiole stem
    stem = curved_tube(
        [(0.0, 0.02, -0.22), (0.0, 0.028, -0.10), (0.0, 0.032, 0.00)],
        [0.012, 0.011, 0.014],
        samples_per_seg=3,
        radial_segs=10,
        cap_rings=2,
        name=f"{name}_stem",
        material="item_stem_green",
    )
    parts.append(stem)

    # Central big serrated lobe + two flanking palmate side lobes
    parts.append(_sculpt_pointed_blade(f"{name}_lobe_c", length=0.46, width=0.24 * spread, material="item_leaf_bright", serration=0.14, arch=0.042, vein_glow=0.45).translate(0.0, 0.030, 0.0))
    for side, tag, ang in ((+1.0, "l", 0.72 * spread), (-1.0, "r", -0.72 * spread)):
        lobe = (
            _sculpt_pointed_blade(f"{name}_lobe_{tag}", length=0.34, width=0.19 * spread, material="item_leaf_bright", serration=0.13, arch=0.030, vein_glow=0.42)
            .rotate_xyz(ry=ang, rz=-side * 0.08)
            .translate(side * 0.025, 0.028, +0.02)
        )
        parts.append(lobe)
    # Raised pale-green central vein ribs converging at stem base
    for ang, ln, tag in ((0.0, 0.38, "c"), (+0.68 * spread, 0.26, "l"), (-0.68 * spread, 0.26, "r")):
        rib = curved_tube(
            [(0.0, 0.036, 0.0), (0.5 * ln * math.sin(ang), 0.056, 0.5 * ln * math.cos(ang)), (ln * math.sin(ang), 0.042, ln * math.cos(ang))],
            [0.010, 0.007, 0.003],
            samples_per_seg=3,
            radial_segs=8,
            name=f"{name}_vein_{tag}",
            material="item_leaf_bright",
        )
        parts.append(rib)

    for p in parts:
        sc.add("leaf", p.rotate_xyz(rx=tilt_deg, ry=yaw_deg, degrees=True))
    return _finalize_item(sc, target_span=0.92)


def build_leaf1() -> SceneBuilder:
    """Upright bright 3-lobed maple/currant leaf (`323:leaf1`)."""
    return _build_palmate_maple("item_leaf1", yaw_deg=165.0, tilt_deg=-8.0, spread=0.95)


def build_leaf2() -> SceneBuilder:
    """Wide diagonal 3-lobed maple leaf with luminous veins (`324:leaf2`)."""
    return _build_palmate_maple("item_leaf2", yaw_deg=134.0, tilt_deg=-6.0, spread=1.12)


def _build_oak_leaf(name: str, *, yaw_deg: float, width_scale: float = 1.0) -> SceneBuilder:
    """Build deeply-lobed dark-emerald oak leaf (`325:leaf3` & `326:leaf4`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)

    def _oak_contour(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        u = np.clip((p[:, 2] + 0.36) / 0.72, 0.0, 1.0)
        # Signature deeply-incised pinnate oak leaf lobes (4 bold rounded fingered pairs along a slender blade!)
        base_env = (np.sin(u * math.pi) ** 0.58) * (0.72 + 0.42 * np.sin(u * math.pi * 0.90))
        lobes = 0.42 + 0.68 * (0.5 + 0.5 * np.sin(u * math.pi * 8.5 - 1.1)) ** 1.35
        p[:, 0] *= np.maximum(0.05, base_env * lobes) * width_scale
        p[:, 1] += 0.040 * np.sin(u * math.pi) + 0.018 * np.abs(p[:, 0]) / max(0.03, 0.14 * width_scale)
        return p

    blade = tint_part(
        ellipsoid(0.155, 0.010, 0.36, u_segs=42, v_segs=18, name=f"{name}_blade", material="item_leaf_oak").deform(_oak_contour),
        lambda pos, _n: np.clip(
            np.array([[0.58, 0.96, 0.44]]) * (0.76 + 0.24 * np.clip((pos[:, 2] + 0.36) / 0.72, 0.0, 1.0)[:, None])
            + 0.38 * np.exp(-((pos[:, 0] / 0.020) ** 2))[:, None] * np.array([[0.95, 1.00, 0.68]]),
            0.20,
            1.0,
        ),
    )
    stem = curved_tube(
        [(0.0, 0.008, -0.44), (0.0, 0.014, -0.34), (0.0, 0.044, 0.00), (0.0, 0.022, +0.30)],
        [0.012, 0.011, 0.009, 0.004],
        samples_per_seg=4,
        radial_segs=10,
        name=f"{name}_midrib",
        material="item_stem_green",
    )
    sc.add("leaf", [p.rotate_xyz(ry=yaw_deg, degrees=True) for p in (blade, stem)])
    return _finalize_item(sc, target_span=0.94)


def build_leaf3() -> SceneBuilder:
    """Diagonal deeply-lobed dark oak leaf (`325:leaf3`)."""
    return _build_oak_leaf("item_leaf3", yaw_deg=138.0, width_scale=0.86)


def build_leaf4() -> SceneBuilder:
    """Slender upright deeply-lobed dark oak leaf (`326:leaf4`)."""
    return _build_oak_leaf("item_leaf4", yaw_deg=176.0, width_scale=0.68)


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
    # Small central crown bud
    sc.add("plant", sphere(0.042, u_segs=14, v_segs=10, name="plant_crown", material="item_stem_green").translate(0.0, 0.075, 0.0))
    # Lower tier of 5 big serrated quilted leaves + upper inner whorl of 4 fresh sprout leaves!
    for k in range(5):
        ang = 2.0 * math.pi * k / 5.0 + 0.15
        lf = (
            _sculpt_pointed_blade(f"plant_leaf_outer_{k}", length=0.40 + 0.03 * (k % 2), width=0.22, material="item_leaf_rich", serration=0.10, arch=0.048, vein_glow=0.38)
            .rotate_xyz(rx=0.18, ry=ang)
            .translate(0.0, 0.065 + 0.008 * (k % 2), 0.0)
        )
        sc.add("plant", lf)
    for k in range(4):
        ang = 2.0 * math.pi * k / 4.0 + 0.65
        lf = (
            _sculpt_pointed_blade(f"plant_leaf_inner_{k}", length=0.24, width=0.15, material="item_leaf_bright", serration=0.08, arch=0.038, vein_glow=0.42)
            .rotate_xyz(rx=-0.26, ry=ang)
            .translate(0.0, 0.095, 0.0)
        )
        sc.add("plant", lf)
    return _finalize_item(sc, target_span=0.94)


def _build_ivy_cluster(name: str, leaf_specs: List[Tuple[float, float, float, float]]) -> SceneBuilder:
    """Build bright 3-leaf backyard ivy cluster (`417:ivyrt`, `418:ivylft`, `419:ivyfront`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    sc.add("ivy", sphere(0.032, u_segs=12, v_segs=8, name=f"{name}_node", material="item_stem_green").translate(0.0, 0.050, 0.0))
    for idx, (ang_deg, length, width, pitch_deg) in enumerate(leaf_specs):
        lf = (
            _sculpt_pointed_blade(f"{name}_leaf_{idx}", length=length, width=width, material="item_leaf_bright", arch=0.036, vein_glow=0.44)
            .rotate_xyz(rx=pitch_deg, ry=ang_deg, degrees=True)
            .translate(0.0, 0.048 + 0.006 * idx, 0.0)
        )
        sc.add("ivy", lf)
    return _finalize_item(sc, target_span=0.94)


def build_ivyrt() -> SceneBuilder:
    """Three-leaf bright ivy cluster fanning right & downward (`417:ivyrt`)."""
    return _build_ivy_cluster("item_ivyrt", [(118.0, 0.44, 0.25, 10.0), (42.0, 0.46, 0.24, 12.0), (-42.0, 0.40, 0.22, 14.0)])


def build_ivylft() -> SceneBuilder:
    """Three-leaf bright ivy cluster fanning left & downward (`418:ivylft`)."""
    return _build_ivy_cluster("item_ivylft", [(-125.0, 0.42, 0.24, 10.0), (-36.0, 0.46, 0.24, 12.0), (+24.0, 0.44, 0.24, 12.0)])


def build_ivyfront() -> SceneBuilder:
    """Wide T-spread 3-leaf backyard ivy cluster (`419:ivyfront`)."""
    return _build_ivy_cluster("item_ivyfront", [(-98.0, 0.44, 0.23, 10.0), (+98.0, 0.44, 0.23, 10.0), (0.0, 0.42, 0.22, 14.0)])


def build_flower1b() -> SceneBuilder:
    """Oxeye Daisy bloom seen in graceful 3/4 side profile (`411:flower1b`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_flower1b")
    hx, hy, hz = _build_daisy_base(sc, petal_count=12, tilt_deg=48.0)
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
    # Turn side-profile bloom facing forward-right matching `411:flower1b`!
    sc.nodes["flower"] = [p.rotate_xyz(ry=52.0, degrees=True) for p in sc.nodes["flower"]]
    return _finalize_item(sc, target_span=0.94)


def _build_starflower(name: str, *, petal_mat: str, petal_count: int, tilt_deg: float, yaw_deg: float) -> SceneBuilder:
    """Hot-pink / rose cosmos starflower with slender petals & arching golden stamens (`412:flower2a` & `413:flower2b`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    stem = curved_tube(
        [(0.0, 0.00, -0.04), (0.012, 0.18, -0.02), (-0.010, 0.36, 0.00), (0.0, 0.50, +0.02)],
        [0.018, 0.016, 0.015, 0.016],
        samples_per_seg=5,
        radial_segs=12,
        cap_rings=2,
        name=f"{name}_stem",
        material="item_stem_green",
    )
    sc.add("flower", stem)
    for side, tag, ly in ((+1.0, "l", 0.14), (-1.0, "r", 0.19)):
        sc.add(
            "flower",
            _sculpt_pointed_blade(f"{name}_leaf_{tag}", length=0.22, width=0.075, material="item_leaf_rich", arch=0.018)
            .rotate_xyz(rx=0.38, ry=side * 1.18)
            .translate(0.0, ly, -0.01),
        )

    head_parts: List[MeshPart] = []
    head_parts.append(superellipsoid(0.075, 0.040, 0.075, e1=0.80, e2=0.95, u_segs=18, v_segs=10, name=f"{name}_calyx", material="item_stem_green").translate(0.0, -0.020, 0.0))
    head_parts.append(superellipsoid(0.068, 0.036, 0.068, e1=0.80, e2=0.95, u_segs=18, v_segs=10, name=f"{name}_throat", material="item_petal_rose").translate(0.0, 0.010, 0.0))

    for k in range(petal_count):
        ang = 2.0 * math.pi * k / petal_count + 0.18
        plen = 0.25 + 0.02 * math.sin(k * 2.3)
        pet = (
            _sculpt_pointed_blade(f"{name}_petal_{k}", length=plen, width=0.072, material=petal_mat, arch=0.022, vein_glow=0.0)
            .rotate_xyz(rx=0.14 + 0.08 * (k % 2), ry=ang)
            .translate(0.042 * math.sin(ang), 0.008, 0.042 * math.cos(ang))
        )
        # Re-tint petals softly: deep magenta base -> bright vivid hot-pink tips!
        pet = tint_part(
            pet,
            lambda pos, _n: np.clip(
                np.array([[0.76, 0.42, 0.56]]) + 0.24 * np.clip(np.hypot(pos[:, 0], pos[:, 2]) / 0.26, 0.0, 1.0)[:, None] * np.array([[1.0, 1.0, 1.0]]),
                0.35,
                1.0,
            ),
        )
        head_parts.append(pet)

    # Radiating arching golden-yellow pollen stamens bursting from the bloom center!
    for k in range(7):
        ang = 2.0 * math.pi * k / 7.0 + 0.4
        r_tip = 0.095 if k % 2 else 0.070
        h_tip = 0.085 if k % 2 else 0.115
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
        sc.add("flower", p.rotate_xyz(rx=tilt_deg, degrees=True).translate(0.0, 0.50, +0.02))
    if abs(yaw_deg) > 1e-3:
        sc.nodes["flower"] = [p.rotate_xyz(ry=yaw_deg, degrees=True) for p in sc.nodes["flower"]]
    return _finalize_item(sc, target_span=0.94)


def build_flower2a() -> SceneBuilder:
    """Hot-pink 8-petal Cosmos starflower with arching golden stamens (`412:flower2a`)."""
    return _build_starflower("item_flower2a", petal_mat="item_petal_pink", petal_count=8, tilt_deg=42.0, yaw_deg=0.0)


def build_flower2b() -> SceneBuilder:
    """Deep rose-magenta starflower variant (`413:flower2b`)."""
    return _build_starflower("item_flower2b", petal_mat="item_petal_rose", petal_count=9, tilt_deg=46.0, yaw_deg=-22.0)


def _build_purple_lily_base(
    sc: SceneBuilder,
    *,
    petal_mat: str,
    tilt_deg: float,
    with_stamens: bool = True,
) -> Tuple[float, float, float]:
    """Build shared 5-petal spotted lavender/purple tiger lily (`flower3a`, `flower3b`, `flower3c`)."""
    stem = curved_tube(
        [(0.0, 0.00, -0.04), (0.018, 0.16, -0.02), (-0.012, 0.34, 0.00), (0.0, 0.48, +0.02)],
        [0.022, 0.018, 0.016, 0.018],
        samples_per_seg=5,
        radial_segs=12,
        cap_rings=2,
        name="lily_stem",
        material="item_stem_green",
    )
    sc.add("flower", stem)
    for side, tag, ly in ((+1.0, "l", 0.14), (-1.0, "r", 0.22)):
        sc.add(
            "flower",
            _sculpt_pointed_blade(f"lily_leaf_{tag}", length=0.22, width=0.085, material="item_leaf_rich", arch=0.020)
            .rotate_xyz(rx=0.35, ry=side * 1.12)
            .translate(0.0, ly, -0.01),
        )

    hx, hy, hz = 0.0, 0.50, +0.02
    head_parts: List[MeshPart] = []
    head_parts.append(superellipsoid(0.078, 0.044, 0.078, e1=0.80, e2=0.95, u_segs=18, v_segs=10, name="lily_calyx", material="item_stem_green").translate(0.0, -0.022, 0.0))

    # 5 wide dramatic pointed spotted lily petals!
    for k in range(5):
        ang = 2.0 * math.pi * k / 5.0 + math.pi  # 1 petal pointing straight UP like `flower3a`/`flower3c`!
        pet = (
            _sculpt_pointed_blade(f"lily_petal_{k}", length=0.29, width=0.145, material=petal_mat, arch=0.040, vein_glow=0.0)
            .rotate_xyz(rx=0.22, ry=ang)
            .translate(0.036 * math.sin(ang), 0.006 * (k % 2), 0.036 * math.cos(ang))
        )
        # Signature purple lily leopard spots + rich violet center throat!
        pet = tint_part(
            pet,
            lambda pos, _n: np.clip(
                np.array([[0.94, 0.88, 1.00]])
                - 0.42 * (np.sin(pos[:, 0] * 52.0) * np.cos(pos[:, 2] * 56.0) > 0.45)[:, None] * np.array([[0.42, 0.66, 0.18]])
                - 0.22 * np.exp(-((np.hypot(pos[:, 0], pos[:, 2]) / 0.10) ** 2))[:, None] * np.array([[0.4, 0.55, 0.1]]),
                0.28,
                1.0,
            ),
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
    return _finalize_item(sc, target_span=0.94)


def build_flower3b() -> SceneBuilder:
    """Deep violet spotted lily angled in side-3/4 profile (`415:flower3b`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_flower3b")
    _build_purple_lily_base(sc, petal_mat="item_petal_violet", tilt_deg=56.0, with_stamens=True)
    sc.nodes["flower"] = [p.rotate_xyz(ry=52.0, degrees=True) for p in sc.nodes["flower"]]
    return _finalize_item(sc, target_span=0.94)


def build_flower3c() -> SceneBuilder:
    """Iconic 1998 goofy googly-eyed lavender flower sticking out its pink tongue (`416:flower3c`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_flower3c")
    hx, hy, hz = _build_purple_lily_base(sc, petal_mat="item_petal_lilac", tilt_deg=48.0, with_stamens=False)

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
