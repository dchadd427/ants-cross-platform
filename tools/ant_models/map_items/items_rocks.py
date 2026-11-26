"""Realistic 3D sculptors for all 21 remaining 1998 Ants Rocks, Boulders & Pebbles (Wave 5 -> 216 / 216 = 100%).

Covers every authentic rock geology from `Original-Ants/ants.chd` (#289–#310, complementing `290:bigrock2`):
- Big Boulders: `bigrock1` (flat grey granite slab), `bigrock3` (tall fractured limestone crag), `bigrock4` (rose-pink speckled porphyry)
- Medium Stones: `medrock1`..`medrock9` (olive river cobbles, gleaming quartz, pink-veined cobble, twin blushed stones, slate flagstone, lichen fieldstone, kidney stone)
- Small Pebbles: `smlrock1`..`smlrock9` (angular slate chips, olive/pink river pebbles, pearlescent quartz beads)
"""
from __future__ import annotations

import math
from typing import Callable, Optional, Sequence, Tuple

try:
    import numpy as np
except ImportError:  # pragma: no cover - pure-stdlib import safety for CI
    np = None  # type: ignore

if np is not None:
    try:
        from ..mesh_core import (
            MeshPart,
            SceneBuilder,
            superellipsoid,
        )
        from .item_materials import register_item_materials, tint_part
        from .items_toys import _finalize_item
    except ImportError:
        from map_items.item_materials import register_item_materials, tint_part  # type: ignore
        from map_items.items_toys import _finalize_item  # type: ignore
        from mesh_core import (  # type: ignore
            MeshPart,
            SceneBuilder,
            superellipsoid,
        )


def _planar_stone_uvs(part: MeshPart, scale: float = 1.35) -> MeshPart:
    """Replace polar-pinched spherical UVs with seamless planar-blended geological stone UVs!"""
    pos = part.positions.astype(np.float64)
    nrm = part.normals.astype(np.float64)
    # Use smooth (x, z) projection on top/bottom & blend y smoothly on steep vertical rock flanks -> zero pole whorls!
    side_w = 1.0 - np.abs(nrm[:, 1])
    u = 0.5 + pos[:, 0] * scale + pos[:, 1] * (scale * 0.55) * side_w * np.where(nrm[:, 0] >= 0.0, 1.0, -1.0)
    v = 0.5 + pos[:, 2] * scale + pos[:, 1] * (scale * 0.55) * side_w * np.where(nrm[:, 2] >= 0.0, 1.0, -1.0)
    part.uvs = np.column_stack([u, v]).astype(np.float32)
    return part


def _build_stone_part(
    name: str,
    *,
    radii: Tuple[float, float, float],
    e1: float = 0.78,
    e2: float = 0.82,
    planes: Sequence[Tuple[Tuple[float, float, float], float, float]] = (),
    crag_amp: float = 0.020,
    crag_freq: float = 10.0,
    seed: float = 1.0,
    flat_bottom: float = 0.55,
    deform_extra: Optional[Callable[[np.ndarray], np.ndarray]] = None,
    material: str = "item_stone_granite",
    color_fn: Callable[[np.ndarray, np.ndarray], np.ndarray],
    rot_deg: Tuple[float, float, float] = (0.0, 0.0, 0.0),
    u_segs: int = 34,
    v_segs: int = 22,
    uv_scale: float = 1.35,
) -> MeshPart:
    """Sculpt one seamless-UV 3D geological stone mesh part with cleavage planes, crag relief & mineral tint."""
    rx, ry, rz = radii

    def _deform(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        if deform_extra is not None:
            p = deform_extra(p)
        for pn_t, d_lim, strength in planes:
            pn = np.asarray(pn_t, dtype=np.float64)
            pn = pn / max(1e-8, float(np.linalg.norm(pn)))
            proj = p @ pn
            over = np.maximum(0.0, proj - d_lim)
            p -= (over * strength)[:, None] * pn[None, :]
        if crag_amp > 0.0:
            r = np.maximum(1e-5, np.linalg.norm(p, axis=1, keepdims=True))
            n = p / r
            f = crag_freq
            s = seed
            crag = (
                crag_amp * np.sin(p[:, 0] * f + s * 1.7 + p[:, 2] * (f * 0.78)) * np.cos(p[:, 1] * (f * 1.1) - s)
                + (crag_amp * 0.55) * np.abs(np.sin(p[:, 0] * (f * 1.7) - p[:, 2] * (f * 1.5) + s * 2.3))
                - (crag_amp * 0.35) * np.cos(p[:, 1] * (f * 2.2) + p[:, 0] * (f * 1.9) - s * 1.3)
            )
            p = p + crag[:, None] * n
        y_floor = -ry * flat_bottom
        p[:, 1] = np.where(p[:, 1] < y_floor, y_floor + 0.14 * (p[:, 1] - y_floor), p[:, 1])
        return p

    mesh = superellipsoid(rx, ry, rz, e1=e1, e2=e2, u_segs=u_segs, v_segs=v_segs, name=f"{name}_body", material=material)
    mesh = mesh.deform(_deform)
    mesh = _planar_stone_uvs(mesh, scale=uv_scale)
    mesh = tint_part(mesh, color_fn)
    rx_d, ry_d, rz_d = rot_deg
    if rx_d or ry_d or rz_d:
        mesh = mesh.rotate_xyz(rx=rx_d, ry=ry_d, rz=rz_d, degrees=True)
    return mesh


def _sculpt_rock(
    name: str,
    *,
    radii: Tuple[float, float, float],
    e1: float = 0.78,
    e2: float = 0.82,
    planes: Sequence[Tuple[Tuple[float, float, float], float, float]] = (),
    crag_amp: float = 0.020,
    crag_freq: float = 10.0,
    seed: float = 1.0,
    flat_bottom: float = 0.55,
    deform_extra: Optional[Callable[[np.ndarray], np.ndarray]] = None,
    material: str = "item_stone_granite",
    color_fn: Callable[[np.ndarray, np.ndarray], np.ndarray],
    rot_deg: Tuple[float, float, float] = (0.0, 0.0, 0.0),
    target_span: float = 0.60,
    u_segs: int = 34,
    v_segs: int = 22,
    uv_scale: float = 1.35,
) -> SceneBuilder:
    """Wrap one sculpted geological stone into a grounded SceneBuilder."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    mesh = _build_stone_part(
        name,
        radii=radii,
        e1=e1,
        e2=e2,
        planes=planes,
        crag_amp=crag_amp,
        crag_freq=crag_freq,
        seed=seed,
        flat_bottom=flat_bottom,
        deform_extra=deform_extra,
        material=material,
        color_fn=color_fn,
        rot_deg=rot_deg,
        u_segs=u_segs,
        v_segs=v_segs,
        uv_scale=uv_scale,
    )
    sc.add("rock", mesh)
    return _finalize_item(sc, target_span=target_span)


def _mineral_tint(
    base_rgb: Tuple[float, float, float],
    hi_rgb: Tuple[float, float, float],
    crevice_rgb: Tuple[float, float, float],
    *,
    accent_rgb: Optional[Tuple[float, float, float]] = None,
    accent_thresh: float = 0.35,
    accent_freq: float = 14.0,
    vein_rgb: Optional[Tuple[float, float, float]] = None,
    vein_freq: float = 16.0,
    seed: float = 0.0,
) -> Callable[[np.ndarray, np.ndarray], np.ndarray]:
    """Build procedural mineral vertex coloration (granite speckle, satin cobble mottling, rose mineral veins)."""
    b_c = np.asarray(base_rgb, dtype=np.float64)[None, :]
    h_c = np.asarray(hi_rgb, dtype=np.float64)[None, :]
    c_c = np.asarray(crevice_rgb, dtype=np.float64)[None, :]

    def _fn(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
        up = np.clip(nrm[:, 1] * 0.65 + 0.35, 0.0, 1.0)[:, None]
        speck = 0.5 + 0.5 * np.sin(pos[:, 0] * 42.0 + seed * 3.1) * np.cos(pos[:, 2] * 38.0 - pos[:, 1] * 29.0)
        broad = 0.5 + 0.5 * np.sin(pos[:, 0] * 11.0 + pos[:, 2] * 9.5 + seed) * np.cos(pos[:, 1] * 13.0 - seed * 1.7)
        col = c_c * (1.0 - up) + (b_c * (0.78 + 0.22 * speck[:, None]) + (h_c - b_c) * (broad[:, None] * 0.60)) * up
        if accent_rgb is not None:
            a_c = np.asarray(accent_rgb, dtype=np.float64)[None, :]
            af = accent_freq
            pat = (
                np.sin(pos[:, 0] * af + seed * 1.9) * np.cos(pos[:, 2] * (af * 0.9) - seed)
                + 0.45 * np.sin((pos[:, 0] - pos[:, 2]) * (af * 1.6) + pos[:, 1] * af)
            )
            w = np.clip((pat - accent_thresh) * 2.8, 0.0, 1.0)[:, None]
            col = col * (1.0 - w * 0.80) + a_c * (w * 0.80)
        if vein_rgb is not None:
            v_c = np.asarray(vein_rgb, dtype=np.float64)[None, :]
            vf = vein_freq
            band1 = np.abs(np.sin(pos[:, 0] * vf + pos[:, 2] * (vf * 0.7) + 1.4 * np.sin(pos[:, 2] * 11.0 + seed)))
            band2 = np.abs(np.cos(pos[:, 0] * (vf * 0.6) - pos[:, 2] * vf + 1.2 * np.cos(pos[:, 0] * 10.0 - seed)))
            vw = np.clip(1.0 - np.minimum(band1, band2) * 4.2, 0.0, 1.0)[:, None]
            col = col * (1.0 - vw * 0.88) + v_c * (vw * 0.88)
        return np.clip(col, 0.06, 1.0)

    return _fn


# ---------------------------------------------------------------------------
# 1. BIG BOULDERS (`289:bigrock1`, `291:bigrock3`, `292:bigrock4`)
# ---------------------------------------------------------------------------

def build_bigrock1() -> SceneBuilder:
    """Long flat weathered pale-grey granite slab with rounded front-left lobe & narrow right tail (`289:bigrock1`)."""
    def _taper_slab(p: np.ndarray) -> np.ndarray:
        out = p.copy()
        t_r = np.clip((out[:, 0] + 0.45) / 0.90, 0.0, 1.0)
        # Prominent forward-left rounded belly lobe around x=-0.12, narrowing gracefully to right tail!
        belly = np.exp(-(((out[:, 0] + 0.14) / 0.24) ** 2))
        front = out[:, 2] > 0.0
        out[front, 2] *= 1.0 + 0.48 * belly[front]
        out[:, 2] *= 1.04 - 0.40 * (t_r ** 1.45)
        out[:, 1] *= 1.02 - 0.30 * (t_r ** 1.4)
        return out

    return _sculpt_rock(
        "item_bigrock1",
        radii=(0.48, 0.145, 0.20),
        e1=0.68,
        e2=0.72,
        planes=(
            ((+0.10, +0.97, +0.22), 0.112, 0.65),
            ((-0.32, +0.76, -0.56), 0.160, 0.60),
            ((+0.68, +0.58, +0.44), 0.28, 0.55),
        ),
        crag_amp=0.016,
        crag_freq=11.0,
        seed=1.1,
        flat_bottom=0.58,
        deform_extra=_taper_slab,
        material="item_stone_granite",
        color_fn=_mineral_tint((0.80, 0.82, 0.83), (0.98, 0.99, 1.00), (0.48, 0.50, 0.53), accent_rgb=(0.94, 0.95, 0.92), accent_thresh=0.42, seed=1.1),
        rot_deg=(0.0, 4.0, 0.0),
        target_span=0.96,
        u_segs=40,
        v_segs=26,
    )


def build_bigrock3() -> SceneBuilder:
    """Tall imposing fractured pale silver-grey limestone crag boulder (`291:bigrock3`)."""
    def _crag_shape(p: np.ndarray) -> np.ndarray:
        out = p.copy()
        diag = (out[:, 0] + out[:, 2]) * 0.707
        t_d = np.clip((diag + 0.42) / 0.84, 0.0, 1.0)
        out[:, 0] *= 1.06 - 0.26 * (t_d ** 1.3)
        out[:, 1] += 0.055 * (1.0 - t_d)
        cleft = np.exp(-(((out[:, 0] * 0.7 - out[:, 2] * 0.7 + 0.04) / 0.065) ** 2))
        out[:, 1] -= 0.036 * cleft * np.clip(out[:, 1] / 0.22, 0.0, 1.0)
        return out

    return _sculpt_rock(
        "item_bigrock3",
        radii=(0.35, 0.34, 0.46),
        e1=0.64,
        e2=0.72,
        planes=(
            ((+0.70, +0.62, +0.35), 0.29, 0.72),
            ((-0.68, +0.60, -0.42), 0.30, 0.70),
            ((+0.28, +0.80, -0.52), 0.28, 0.68),
            ((-0.44, +0.54, +0.72), 0.31, 0.68),
            ((+0.84, +0.30, -0.45), 0.31, 0.64),
            ((-0.82, +0.26, +0.52), 0.31, 0.64),
        ),
        crag_amp=0.032,
        crag_freq=9.5,
        seed=3.3,
        flat_bottom=0.52,
        deform_extra=_crag_shape,
        material="item_stone_granite",
        color_fn=_mineral_tint((0.82, 0.84, 0.86), (0.99, 1.00, 1.00), (0.46, 0.49, 0.54), accent_rgb=(0.66, 0.70, 0.74), accent_thresh=0.25, accent_freq=13.0, seed=3.3),
        rot_deg=(0.0, -24.0, 0.0),
        target_span=0.98,
        u_segs=42,
        v_segs=28,
    )


def build_bigrock4() -> SceneBuilder:
    """Iconic rose-pink & burgundy mottled porphyry granite boulder with pale quartz flecks (`292:bigrock4`)."""
    def _rose_shape(p: np.ndarray) -> np.ndarray:
        out = p.copy()
        out[:, 1] += 0.045 * np.clip(-out[:, 0] / 0.35, -0.5, 1.0)
        return out

    def _rose_porphyry_color(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
        up = np.clip(nrm[:, 1] * 0.55 + 0.45, 0.0, 1.0)[:, None]
        # Fine crisp porphyry granite mineral grain (dusty rose-mauve feldspar + warm beige quartz + dark plum biotite), zero meaty streaks!
        f1 = np.sin(pos[:, 0] * 38.0 + pos[:, 2] * 34.0) * np.cos(pos[:, 1] * 44.0 - pos[:, 0] * 28.0)
        f2 = np.sin(pos[:, 0] * 68.0 - pos[:, 2] * 62.0) * np.cos(pos[:, 2] * 58.0 + pos[:, 1] * 72.0)
        f3 = np.sin((pos[:, 0] + pos[:, 1] * 0.8) * 92.0) * np.cos((pos[:, 2] - pos[:, 0] * 0.7) * 86.0)
        dusty_rose = np.array([[0.82, 0.44, 0.50]], dtype=np.float64)
        pale_pink = np.array([[0.96, 0.66, 0.70]], dtype=np.float64)
        warm_quartz = np.array([[0.90, 0.86, 0.80]], dtype=np.float64)
        biotite = np.array([[0.46, 0.28, 0.34]], dtype=np.float64)
        w_pink = np.clip(f1 * 0.65 + 0.50, 0.0, 1.0)[:, None]
        w_quartz = np.clip((f2 - 0.12) * 2.6, 0.0, 1.0)[:, None]
        w_dark = np.clip((f3 - 0.48) * 3.0, 0.0, 1.0)[:, None]
        col = biotite * (1.0 - up) + (dusty_rose * (1.0 - w_pink) + pale_pink * w_pink) * up
        col = col * (1.0 - w_quartz * 0.68) + warm_quartz * (w_quartz * 0.68)
        col = col * (1.0 - w_dark * 0.38) + biotite * (w_dark * 0.38)
        return np.clip(col, 0.14, 1.0)

    return _sculpt_rock(
        "item_bigrock4",
        radii=(0.40, 0.29, 0.36),
        e1=0.72,
        e2=0.76,
        planes=(
            ((+0.72, +0.58, +0.38), 0.30, 0.68),
            ((-0.66, +0.64, -0.38), 0.31, 0.65),
            ((+0.24, +0.82, -0.52), 0.26, 0.62),
            ((-0.36, +0.54, +0.76), 0.29, 0.62),
        ),
        crag_amp=0.030,
        crag_freq=11.5,
        seed=4.4,
        flat_bottom=0.52,
        deform_extra=_rose_shape,
        material="item_stone_granite",
        color_fn=_rose_porphyry_color,
        rot_deg=(0.0, -14.0, 0.0),
        target_span=0.88,
        u_segs=40,
        v_segs=26,
    )


# ---------------------------------------------------------------------------
# 2. MEDIUM ROCKS & COBBLES (`293..301:medrock1..9`)
# ---------------------------------------------------------------------------

def build_medrock1() -> SceneBuilder:
    """Smooth oval mottled olive-tan river cobble (`293:medrock1`)."""
    return _sculpt_rock(
        "item_medrock1",
        radii=(0.36, 0.165, 0.23),
        e1=0.88,
        e2=0.88,
        crag_amp=0.009,
        crag_freq=12.0,
        seed=5.1,
        flat_bottom=0.55,
        material="item_stone_quartz",
        color_fn=_mineral_tint((0.58, 0.56, 0.42), (0.80, 0.76, 0.62), (0.30, 0.30, 0.22), accent_rgb=(0.46, 0.52, 0.34), accent_thresh=0.14, accent_freq=15.0, seed=5.1),
        rot_deg=(0.0, 12.0, 0.0),
        target_span=0.70,
    )


def build_medrock2() -> SceneBuilder:
    """Smooth plump gleaming pale-quartz & cool silver-grey river stone (`294:medrock2`)."""
    return _sculpt_rock(
        "item_medrock2",
        radii=(0.28, 0.20, 0.26),
        e1=0.90,
        e2=0.90,
        crag_amp=0.007,
        crag_freq=10.0,
        seed=6.2,
        flat_bottom=0.54,
        material="item_stone_quartz",
        color_fn=_mineral_tint((0.78, 0.80, 0.80), (0.98, 0.98, 0.96), (0.44, 0.46, 0.48), accent_rgb=(0.94, 0.94, 0.92), accent_thresh=0.26, accent_freq=9.0, seed=6.2),
        target_span=0.60,
    )


def build_medrock3() -> SceneBuilder:
    """Smooth warm-grey river cobble crisscrossed with vivid coral-pink mineral veins (`295:medrock3`)."""
    def _cobble_wedge(p: np.ndarray) -> np.ndarray:
        out = p.copy()
        t = np.clip((out[:, 0] + 0.34) / 0.68, 0.0, 1.0)
        out[:, 2] *= 1.06 - 0.24 * (t ** 1.3)
        return out

    return _sculpt_rock(
        "item_medrock3",
        radii=(0.34, 0.185, 0.24),
        e1=0.86,
        e2=0.86,
        crag_amp=0.010,
        crag_freq=12.0,
        seed=7.3,
        flat_bottom=0.55,
        deform_extra=_cobble_wedge,
        material="item_stone_quartz",
        color_fn=_mineral_tint((0.68, 0.66, 0.62), (0.88, 0.86, 0.82), (0.38, 0.36, 0.36), vein_rgb=(0.94, 0.34, 0.38), vein_freq=16.0, seed=7.3),
        rot_deg=(0.0, -14.0, 0.0),
        target_span=0.68,
    )


def build_medrock4() -> SceneBuilder:
    """Compact damp dark olive-bronze cobblestone (`296:medrock4`)."""
    return _sculpt_rock(
        "item_medrock4",
        radii=(0.25, 0.185, 0.24),
        e1=0.76,
        e2=0.78,
        planes=(((+0.45, +0.78, -0.42), 0.185, 0.55), ((-0.52, +0.72, +0.46), 0.195, 0.55)),
        crag_amp=0.014,
        crag_freq=14.0,
        seed=8.4,
        flat_bottom=0.55,
        material="item_stone_quartz",
        color_fn=_mineral_tint((0.34, 0.36, 0.24), (0.56, 0.58, 0.40), (0.16, 0.18, 0.12), accent_rgb=(0.44, 0.48, 0.28), accent_thresh=0.14, seed=8.4),
        rot_deg=(0.0, 18.0, 0.0),
        target_span=0.56,
    )


def build_medrock5() -> SceneBuilder:
    """Flat diagonal speckled grey slate/concrete flagstone slab (`297:medrock5`)."""
    def _trap_slab(p: np.ndarray) -> np.ndarray:
        out = p.copy()
        t = np.clip((out[:, 0] + 0.32) / 0.64, 0.0, 1.0)
        out[:, 2] *= 0.82 + 0.30 * t
        return out

    return _sculpt_rock(
        "item_medrock5",
        radii=(0.33, 0.105, 0.22),
        e1=0.38,
        e2=0.45,
        planes=(((0.0, +1.0, 0.0), 0.088, 0.85), ((+0.78, +0.42, -0.45), 0.26, 0.65)),
        crag_amp=0.009,
        crag_freq=18.0,
        seed=9.5,
        flat_bottom=0.68,
        deform_extra=_trap_slab,
        material="item_stone_granite",
        color_fn=_mineral_tint((0.78, 0.80, 0.82), (0.94, 0.96, 0.98), (0.50, 0.52, 0.55), accent_rgb=(0.66, 0.68, 0.70), accent_thresh=0.22, accent_freq=26.0, seed=9.5),
        rot_deg=(0.0, 38.0, 0.0),
        target_span=0.66,
    )


def build_medrock6() -> SceneBuilder:
    """Rough craggy warm earthy-brown & sulfur-lichen fieldstone (`298:medrock6`)."""
    return _sculpt_rock(
        "item_medrock6",
        radii=(0.30, 0.23, 0.28),
        e1=0.72,
        e2=0.78,
        planes=(
            ((+0.62, +0.66, +0.42), 0.22, 0.65),
            ((-0.65, +0.62, -0.44), 0.23, 0.65),
            ((+0.22, +0.84, -0.50), 0.21, 0.60),
        ),
        crag_amp=0.026,
        crag_freq=13.0,
        seed=10.6,
        flat_bottom=0.52,
        material="item_stone_granite",
        color_fn=_mineral_tint((0.68, 0.54, 0.46), (0.86, 0.78, 0.56), (0.34, 0.28, 0.24), accent_rgb=(0.74, 0.76, 0.34), accent_thresh=0.28, accent_freq=17.0, seed=10.6),
        rot_deg=(0.0, -18.0, 0.0),
        target_span=0.66,
    )


def build_medrock7() -> SceneBuilder:
    """Smooth teardrop-wedge pearlescent pale-grey quartz river stone (`299:medrock7`)."""
    def _teardrop_stone(p: np.ndarray) -> np.ndarray:
        out = p.copy()
        t = np.clip((out[:, 0] + 0.34) / 0.68, 0.0, 1.0)
        out[:, 2] *= 0.52 + 0.62 * (t ** 0.85)
        out[:, 2] -= 0.045 * (1.0 - t)
        out[:, 1] *= 0.78 + 0.30 * t
        return out

    return _sculpt_rock(
        "item_medrock7",
        radii=(0.34, 0.16, 0.23),
        e1=0.86,
        e2=0.88,
        crag_amp=0.008,
        crag_freq=11.0,
        seed=11.7,
        flat_bottom=0.56,
        deform_extra=_teardrop_stone,
        material="item_stone_quartz",
        color_fn=_mineral_tint((0.76, 0.78, 0.80), (0.98, 0.98, 0.97), (0.44, 0.46, 0.50), seed=11.7),
        rot_deg=(0.0, -14.0, 0.0),
        target_span=0.66,
    )


def build_medrock8() -> SceneBuilder:
    """Twin 1998 stone pair: upper smooth olive-tan cobble + front pink-blushed wedge pebble (`300:medrock8`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_medrock8")

    # Upper olive-tan river cobble (frame 1 of `300:medrock8`)
    top_cobble = _build_stone_part(
        "medrock8_top",
        radii=(0.30, 0.135, 0.165),
        e1=0.86,
        e2=0.86,
        crag_amp=0.008,
        crag_freq=13.0,
        seed=12.4,
        material="item_stone_quartz",
        color_fn=_mineral_tint((0.58, 0.56, 0.44), (0.82, 0.78, 0.66), (0.30, 0.30, 0.24), accent_rgb=(0.48, 0.52, 0.36), accent_thresh=0.18, seed=12.4),
        rot_deg=(0.0, 4.0, 0.0),
    ).translate(-0.04, 0.09, -0.14)

    # Front smaller pink-blushed wedge pebble (frame 0 of `300:medrock8`)
    def _wedge_blush(p: np.ndarray) -> np.ndarray:
        out = p.copy()
        t = np.clip((out[:, 0] + 0.18) / 0.36, 0.0, 1.0)
        out[:, 2] *= 1.08 - 0.34 * t
        return out

    def _blush_color(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
        base = _mineral_tint((0.68, 0.64, 0.56), (0.86, 0.82, 0.74), (0.38, 0.36, 0.32), seed=12.8)(pos, nrm)
        blush = np.exp(-(((pos[:, 0] + 0.03) / 0.085) ** 2 + ((pos[:, 2] + 0.01) / 0.065) ** 2))[:, None] * np.clip(nrm[:, 1:2], 0.0, 1.0)
        return np.clip(base * (1.0 - blush * 0.82) + np.array([[0.96, 0.44, 0.48]]) * (blush * 0.82), 0.10, 1.0)

    front_peb = _build_stone_part(
        "medrock8_front",
        radii=(0.18, 0.095, 0.115),
        e1=0.72,
        e2=0.76,
        planes=(((+0.25, +0.88, +0.40), 0.082, 0.60),),
        crag_amp=0.008,
        crag_freq=18.0,
        seed=12.8,
        deform_extra=_wedge_blush,
        material="item_stone_granite",
        color_fn=_blush_color,
        rot_deg=(0.0, 6.0, 0.0),
    ).translate(+0.07, 0.06, +0.16)

    sc.add("rock", [top_cobble, front_peb])
    return _finalize_item(sc, target_span=0.64)


def build_medrock9() -> SceneBuilder:
    """Distinctive crescent-kidney pale olive-tan river stone: concave back saddle, smooth convex belly (`301:medrock9`)."""
    def _kidney_curve(p: np.ndarray) -> np.ndarray:
        out = p.copy()
        # Scooped saddle indentation on BACK edge (-Z) around x=-0.06 while keeping FRONT belly (+Z) smoothly convex!
        saddle = np.exp(-(((out[:, 0] + 0.05) / 0.14) ** 2))
        back = out[:, 2] < 0.0
        out[back, 2] *= 1.0 - 0.52 * saddle[back]
        # Left upturned ear lobe (-X, -Z) + broader rounded right lobe (+X) matching `301:medrock9`!
        ear = np.exp(-(((out[:, 0] + 0.26) / 0.12) ** 2))
        out[:, 2] -= 0.065 * ear
        out[:, 1] *= 1.0 - 0.16 * saddle
        right_w = np.clip((out[:, 0] + 0.08) / 0.42, 0.0, 1.0)
        out[:, 2] *= 0.86 + 0.24 * right_w
        return out

    return _sculpt_rock(
        "item_medrock9",
        radii=(0.37, 0.175, 0.22),
        e1=0.86,
        e2=0.86,
        crag_amp=0.009,
        crag_freq=11.0,
        seed=13.9,
        flat_bottom=0.56,
        deform_extra=_kidney_curve,
        material="item_stone_quartz",
        color_fn=_mineral_tint((0.64, 0.62, 0.50), (0.84, 0.82, 0.72), (0.34, 0.34, 0.28), accent_rgb=(0.56, 0.50, 0.44), accent_thresh=0.22, seed=13.9),
        rot_deg=(0.0, -4.0, 0.0),
        target_span=0.72,
    )


# ---------------------------------------------------------------------------
# 3. SMALL PEBBLES & GRAVEL STONES (`302..310:smlrock1..9`)
# ---------------------------------------------------------------------------

def build_smlrock1() -> SceneBuilder:
    """Tiny diagonal angular cool-grey gravel pebble (`302:smlrock1`)."""
    return _sculpt_rock(
        "item_smlrock1",
        radii=(0.24, 0.14, 0.17),
        e1=0.74,
        e2=0.78,
        planes=(((+0.35, +0.85, -0.38), 0.12, 0.60),),
        crag_amp=0.011,
        crag_freq=18.0,
        seed=14.1,
        material="item_stone_granite",
        color_fn=_mineral_tint((0.72, 0.74, 0.78), (0.90, 0.92, 0.95), (0.42, 0.44, 0.48), seed=14.1),
        rot_deg=(0.0, 44.0, 0.0),
        target_span=0.44,
    )


def build_smlrock2() -> SceneBuilder:
    """Smooth triangular warm olive-tan river pebble (`303:smlrock2`)."""
    def _tri_pebble(p: np.ndarray) -> np.ndarray:
        out = p.copy()
        t = np.clip((out[:, 2] + 0.24) / 0.48, 0.0, 1.0)
        out[:, 0] *= 0.48 + 0.68 * t
        out[:, 0] += 0.05 * (1.0 - t)
        return out

    return _sculpt_rock(
        "item_smlrock2",
        radii=(0.24, 0.145, 0.24),
        e1=0.82,
        e2=0.84,
        crag_amp=0.009,
        crag_freq=15.0,
        seed=15.2,
        deform_extra=_tri_pebble,
        material="item_stone_quartz",
        color_fn=_mineral_tint((0.56, 0.54, 0.42), (0.78, 0.74, 0.62), (0.30, 0.28, 0.22), accent_rgb=(0.72, 0.48, 0.46), accent_thresh=0.42, seed=15.2),
        rot_deg=(0.0, -10.0, 0.0),
        target_span=0.48,
    )


def build_smlrock3() -> SceneBuilder:
    """Small flat angular tan pebble with coral-pink mineral smudge (`304:smlrock3`)."""
    def _pink_smudge(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
        base = _mineral_tint((0.72, 0.66, 0.58), (0.88, 0.82, 0.74), (0.40, 0.36, 0.32), seed=16.3)(pos, nrm)
        spot = np.exp(-(((pos[:, 0] + 0.04) / 0.11) ** 2 + ((pos[:, 2] + 0.03) / 0.09) ** 2))[:, None] * np.clip(nrm[:, 1:2], 0.0, 1.0)
        return np.clip(base * (1.0 - spot * 0.80) + np.array([[0.96, 0.44, 0.48]]) * (spot * 0.80), 0.10, 1.0)

    return _sculpt_rock(
        "item_smlrock3",
        radii=(0.26, 0.115, 0.16),
        e1=0.70,
        e2=0.76,
        planes=(((+0.20, +0.92, +0.32), 0.098, 0.65),),
        crag_amp=0.010,
        crag_freq=18.0,
        seed=16.3,
        material="item_stone_granite",
        color_fn=_pink_smudge,
        rot_deg=(0.0, 8.0, 0.0),
        target_span=0.46,
    )


def build_smlrock4() -> SceneBuilder:
    """Faceted diamond-shaped pale silver-grey granite stone chip (`305:smlrock4`)."""
    return _sculpt_rock(
        "item_smlrock4",
        radii=(0.22, 0.13, 0.21),
        e1=0.52,
        e2=0.56,
        planes=(((+0.42, +0.80, +0.42), 0.13, 0.72), ((-0.46, +0.78, -0.42), 0.14, 0.70)),
        crag_amp=0.011,
        crag_freq=18.0,
        seed=17.4,
        material="item_stone_granite",
        color_fn=_mineral_tint((0.80, 0.82, 0.85), (0.98, 0.99, 1.00), (0.48, 0.50, 0.54), seed=17.4),
        rot_deg=(0.0, 44.0, 0.0),
        target_span=0.46,
    )


def build_smlrock5() -> SceneBuilder:
    """Smooth round warm buff-tan pebble with faint sage-olive mottling (`306:smlrock5`)."""
    return _sculpt_rock(
        "item_smlrock5",
        radii=(0.23, 0.145, 0.21),
        e1=0.88,
        e2=0.88,
        crag_amp=0.008,
        crag_freq=14.0,
        seed=18.5,
        material="item_stone_quartz",
        color_fn=_mineral_tint((0.60, 0.56, 0.42), (0.78, 0.74, 0.58), (0.32, 0.30, 0.24), accent_rgb=(0.48, 0.54, 0.36), accent_thresh=0.18, seed=18.5),
        rot_deg=(0.0, 22.0, 0.0),
        target_span=0.46,
    )


def build_smlrock6() -> SceneBuilder:
    """Chunky five-sided pale cool-grey limestone chip (`307:smlrock6`)."""
    return _sculpt_rock(
        "item_smlrock6",
        radii=(0.24, 0.145, 0.22),
        e1=0.58,
        e2=0.62,
        planes=(
            ((+0.62, +0.68, -0.38), 0.15, 0.72),
            ((-0.55, +0.72, +0.42), 0.15, 0.70),
            ((+0.12, +0.86, +0.50), 0.14, 0.65),
        ),
        crag_amp=0.013,
        crag_freq=16.0,
        seed=19.6,
        material="item_stone_granite",
        color_fn=_mineral_tint((0.82, 0.84, 0.86), (1.00, 1.00, 1.00), (0.48, 0.50, 0.54), seed=19.6),
        rot_deg=(0.0, 28.0, 0.0),
        target_span=0.48,
    )


def build_smlrock7() -> SceneBuilder:
    """Tiny smooth rounded light-grey gravel bead (`308:smlrock7`)."""
    return _sculpt_rock(
        "item_smlrock7",
        radii=(0.20, 0.14, 0.18),
        e1=0.88,
        e2=0.88,
        crag_amp=0.007,
        crag_freq=16.0,
        seed=20.7,
        material="item_stone_granite",
        color_fn=_mineral_tint((0.78, 0.80, 0.82), (0.96, 0.98, 1.00), (0.46, 0.48, 0.52), seed=20.7),
        target_span=0.40,
    )


def build_smlrock8() -> SceneBuilder:
    """Plump glossy pearlescent white-silver quartz pebble (`309:smlrock8`)."""
    return _sculpt_rock(
        "item_smlrock8",
        radii=(0.24, 0.16, 0.20),
        e1=0.90,
        e2=0.90,
        crag_amp=0.006,
        crag_freq=12.0,
        seed=21.8,
        material="item_stone_quartz",
        color_fn=_mineral_tint((0.80, 0.82, 0.82), (0.99, 0.99, 0.98), (0.46, 0.48, 0.50), seed=21.8),
        rot_deg=(0.0, -12.0, 0.0),
        target_span=0.46,
    )


def build_smlrock9() -> SceneBuilder:
    """Elongated dark slate-grey river pebble with subtle emerald-teal moss flecks (`310:smlrock9`)."""
    return _sculpt_rock(
        "item_smlrock9",
        radii=(0.29, 0.135, 0.18),
        e1=0.84,
        e2=0.86,
        crag_amp=0.010,
        crag_freq=15.0,
        seed=22.9,
        material="item_stone_quartz",
        color_fn=_mineral_tint((0.40, 0.44, 0.48), (0.66, 0.70, 0.74), (0.20, 0.24, 0.28), accent_rgb=(0.20, 0.58, 0.40), accent_thresh=0.42, accent_freq=22.0, seed=22.9),
        rot_deg=(0.0, 4.0, 0.0),
        target_span=0.52,
    )
