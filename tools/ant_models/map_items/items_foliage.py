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
            rounded_box,
            sphere,
            superellipsoid,
            torus_arc,
        )
        from .item_materials import gradient_y_tint, register_item_materials, tint_part
        from .items_plants import _build_daisy_base
        from .items_toys import _finalize_item
    except ImportError:
        from map_items.item_materials import gradient_y_tint, register_item_materials, tint_part  # type: ignore
        from map_items.items_plants import _build_daisy_base  # type: ignore
        from map_items.items_toys import _finalize_item  # type: ignore
        from mesh_core import (  # type: ignore
            MeshPart,
            SceneBuilder,
            curved_tube,
            ellipsoid,
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


def build_stick1() -> SceneBuilder:
    """Crooked peeled brown woodland twig curving diagonally (`327:stick1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_stick1")
    pts = [
        (-0.22, 0.024, +0.36),
        (-0.26, 0.032, +0.16),
        (-0.14, 0.042, +0.02),
        (-0.02, 0.038, -0.10),
        (+0.10, 0.048, -0.22),
        (+0.26, 0.034, -0.36),
    ]
    rads = [0.024, 0.028, 0.025, 0.026, 0.022, 0.016]
    twig = gradient_y_tint(
        curved_tube(pts, rads, samples_per_seg=5, radial_segs=14, cap_rings=3, name="twig_main", material="item_wood_stick")
        .deform(lambda p: p + 0.004 * np.sin(p * 48.0)),
        (0.62, 0.48, 0.36),
        (1.04, 0.92, 0.78),
        noise_amp=0.06,
    )
    # Little broken bark nub along mid-bend
    nub = curved_tube([(-0.14, 0.042, +0.02), (-0.175, 0.052, 0.00), (-0.21, 0.062, -0.02)], [0.015, 0.012, 0.008], samples_per_seg=3, radial_segs=8, cap_rings=2, name="twig_nub", material="item_twig_bark")
    sc.add("stick", [twig, nub])
    return _finalize_item(sc, target_span=0.96)


def build_stick2a() -> SceneBuilder:
    """Horizontal woody brown branch twig with two hanging green pointed leaves (`328:stick2a`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_stick2a")
    branch = curved_tube(
        [(-0.36, 0.052, -0.16), (-0.10, 0.062, -0.14), (+0.16, 0.056, -0.12), (+0.36, 0.044, -0.10)],
        [0.028, 0.026, 0.023, 0.018],
        samples_per_seg=4,
        radial_segs=14,
        cap_rings=3,
        name="branch_wood",
        material="item_twig_bark",
    )
    leaf_a = (
        _sculpt_pointed_blade("branch_leaf_a", length=0.34, width=0.14, material="item_leaf_rich", arch=0.026, vein_glow=0.30)
        .rotate_xyz(rx=0.12, ry=0.38)
        .translate(-0.14, 0.046, -0.13)
    )
    leaf_b = (
        _sculpt_pointed_blade("branch_leaf_b", length=0.42, width=0.18, material="item_leaf_bright", arch=0.032, vein_glow=0.35)
        .rotate_xyz(rx=0.10, ry=0.72)
        .translate(+0.02, 0.048, -0.12)
    )
    sc.add("stick_leafy", [branch, leaf_a, leaf_b])
    return _finalize_item(sc, target_span=0.96)


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
    hx, hy, hz = _build_daisy_base(sc, petal_count=16, tilt_deg=44.0)
    pollen = gradient_y_tint(
        superellipsoid(0.104, 0.056, 0.104, e1=0.78, e2=0.98, u_segs=26, v_segs=14, name="daisy1b_pollen", material="item_pollen_gold")
        .deform(lambda p: p + 0.005 * np.sin(p * 65.0) * np.cos(np.roll(p, 1, axis=1) * 60.0))
        .translate(0.0, 0.018, 0.0)
        .rotate_xyz(rx=44.0, degrees=True)
        .translate(hx, hy, hz),
        (0.84, 0.64, 0.42),
        (1.04, 1.00, 0.86),
    )
    sc.add("flower", pollen)
    # Turn side-profile bloom facing forward-right matching `411:flower1b`!
    sc.nodes["flower"] = [p.rotate_xyz(ry=56.0, degrees=True) for p in sc.nodes["flower"]]
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
    """Resting open Groucho Disguise Daisy variant (`421:flower1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_flower1")
    hx, hy, hz = _build_daisy_base(sc, petal_count=8, tilt_deg=44.0)
    face_parts: List[MeshPart] = [
        superellipsoid(0.105, 0.038, 0.105, e1=0.80, e2=0.96, u_segs=22, v_segs=12, name="flower1_center", material="item_dark_stitch").translate(0.0, 0.010, 0.0),
    ]
    for side, tag in ((+1.0, "l"), (-1.0, "r")):
        face_parts.extend([
            torus_arc(0.064, 0.015, u_segs=22, v_segs=10, plane="xz", name=f"f1_frame_{tag}", material="gear_dark").translate(side * 0.074, 0.060, -0.036),
            ellipsoid(0.058, 0.012, 0.050, u_segs=18, v_segs=8, name=f"f1_lens_{tag}", material="eye_pupil").translate(side * 0.074, 0.062, -0.036),
            ellipsoid(0.022, 0.006, 0.012, u_segs=10, v_segs=6, name=f"f1_glint_{tag}", material="eye_catchlight").rotate_xyz(ry=0.45).translate(side * 0.058, 0.075, -0.048),
            rounded_box(0.090, 0.022, 0.026, bevel=0.007, segs=2, name=f"f1_brow_{tag}", material="item_dark_stitch").rotate_xyz(ry=-side * 0.14).translate(side * 0.074, 0.066, -0.096),
            curved_tube(
                [(0.0, 0.052, +0.068), (side * 0.060, 0.048, +0.094), (side * 0.122, 0.036, +0.078)],
                [0.038, 0.042, 0.022],
                samples_per_seg=4,
                radial_segs=14,
                cap_rings=3,
                name=f"f1_stache_{tag}",
                material="item_dark_stitch",
            ),
        ])
    face_parts.append(rounded_box(0.052, 0.020, 0.022, bevel=0.005, segs=2, name="f1_bridge", material="gear_dark").translate(0.0, 0.064, -0.042))
    face_parts.append(superellipsoid(0.054, 0.064, 0.076, e1=0.76, e2=0.82, u_segs=20, v_segs=14, name="f1_nose", material="item_groucho_nose").rotate_xyz(rx=0.25).translate(0.0, 0.080, +0.020))
    for p in face_parts:
        sc.add("flower", p.rotate_xyz(rx=44.0, degrees=True).translate(hx, hy, hz))
    return _finalize_item(sc, target_span=0.92)
