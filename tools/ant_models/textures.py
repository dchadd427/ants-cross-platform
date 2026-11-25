"""Seamless procedural PBR PNG textures (albedo, normal, metallic-roughness) for glTF 2.0.

Generates tileable PNG maps cached in memory using numpy + stdlib zlib with
PNG Paeth scanline filtering so builds are deterministic, fast (<0.25s total),
and keep embedded .glb files compact.
"""
from __future__ import annotations

import functools
import math
import struct
import zlib
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

import numpy as np

try:
    from .materials import get_material
except ImportError:
    from materials import get_material  # type: ignore

DEFAULT_TEXTURE_SIZE = 256
FAMILY_TEXTURE_SIZES: Dict[str, int] = {
    "chitin": 256,
    "cloth": 128,
    "metal": 128,
    "rubber": 128,
    "fur": 128,
    "bread": 128,
    "foliage": 128,
    "leaf_vein": 128,
    "stone": 128,
    "soil": 128,
    "wood": 128,
    "bark_wood": 256,
    "vinyl_rubber": 128,
    "dino_skin": 128,
    "organic_flesh": 128,
    "banana_slice": 128,
    "fried_chicken": 256,
    "cornmeal_batter": 128,
    "cola_label": 256,
    "cap_h2o": 128,
    "cap_soda": 128,
}


@dataclass(frozen=True)
class TexturePack:
    """Embedded PNG texture triplet for one PBR material family."""

    family: str
    size: int
    base_color_png: Optional[bytes]
    normal_png: bytes
    metallic_roughness_png: bytes


def _encode_png_rgb(rgb: np.ndarray) -> bytes:
    """Encode an (H, W, 3) uint8 array to a compact deterministic baseline RGB PNG."""
    arr = np.ascontiguousarray(rgb, dtype=np.uint8)
    h, w, ch = arr.shape
    if ch != 3:
        raise ValueError(f"Expected 3-channel RGB array, got shape {arr.shape}")

    def chunk(tag: bytes, payload: bytes) -> bytes:
        crc = zlib.crc32(tag + payload) & 0xFFFFFFFF
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", crc)

    # PNG Paeth filter (type 4) gives strong deterministic lossless compression on smooth maps.
    raw = arr.reshape(h, w * 3).astype(np.int16)
    left = np.zeros_like(raw)
    left[:, 3:] = raw[:, :-3]
    up = np.zeros_like(raw)
    up[1:, :] = raw[:-1, :]
    up_left = np.zeros_like(raw)
    up_left[1:, 3:] = raw[:-1, :-3]

    p = left + up - up_left
    pa = np.abs(p - left)
    pb = np.abs(p - up)
    pc = np.abs(p - up_left)
    pred = np.where((pa <= pb) & (pa <= pc), left, np.where(pb <= pc, up, up_left))
    filtered = ((raw - pred) & 0xFF).astype(np.uint8)

    rows = np.empty((h, w * 3 + 1), dtype=np.uint8)
    rows[:, 0] = 4
    rows[:, 1:] = filtered
    compressed = zlib.compress(rows.tobytes(), level=9)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", compressed) + chunk(b"IEND", b"")


def _tile_noise(size: int, cells_x: int, cells_y: int, seed: int) -> np.ndarray:
    """Seamless periodic 2D value noise in [0, 1] with quintic interpolation."""
    rng = np.random.default_rng(seed)
    grid = rng.random((cells_y, cells_x), dtype=np.float32)

    u = np.linspace(0.0, float(cells_x), size, endpoint=False, dtype=np.float32)
    v = np.linspace(0.0, float(cells_y), size, endpoint=False, dtype=np.float32)
    ix = np.floor(u).astype(np.int32)
    iy = np.floor(v).astype(np.int32)
    fx = u - ix
    fy = v - iy
    sx = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0)
    sy = fy * fy * fy * (fy * (fy * 6.0 - 15.0) + 10.0)

    x0 = ix % cells_x
    x1 = (ix + 1) % cells_x
    y0 = iy % cells_y
    y1 = (iy + 1) % cells_y

    v00 = grid[np.ix_(y0, x0)]
    v10 = grid[np.ix_(y0, x1)]
    v01 = grid[np.ix_(y1, x0)]
    v11 = grid[np.ix_(y1, x1)]

    top = v00 * (1.0 - sx[None, :]) + v10 * sx[None, :]
    bot = v01 * (1.0 - sx[None, :]) + v11 * sx[None, :]
    return top * (1.0 - sy[:, None]) + bot * sy[:, None]


def _tile_voronoi_f1_f2(size: int, cells: int, seed: int) -> Tuple[np.ndarray, np.ndarray]:
    """Seamless periodic Voronoi distances (F1 nearest, F2 - F1 edge ridge) in [0, 1]."""
    rng = np.random.default_rng(seed)
    pts_x = rng.uniform(0.14, 0.86, (cells, cells)).astype(np.float32)
    pts_y = rng.uniform(0.14, 0.86, (cells, cells)).astype(np.float32)

    u = np.linspace(0.0, float(cells), size, endpoint=False, dtype=np.float32)
    v = np.linspace(0.0, float(cells), size, endpoint=False, dtype=np.float32)
    gx, gy = np.meshgrid(u, v)
    ix = np.floor(gx).astype(np.int32)
    iy = np.floor(gy).astype(np.int32)
    fx = gx - ix
    fy = gy - iy

    d1 = np.full((size, size), 10.0, dtype=np.float32)
    d2 = np.full((size, size), 10.0, dtype=np.float32)
    for dy in (-1, 0, 1):
        ny = (iy + dy) % cells
        for dx in (-1, 0, 1):
            nx = (ix + dx) % cells
            px = dx + pts_x[ny, nx]
            py = dy + pts_y[ny, nx]
            dist = np.sqrt((fx - px) ** 2 + (fy - py) ** 2)
            closer = dist < d1
            d2 = np.where(closer, d1, np.minimum(d2, dist))
            d1 = np.where(closer, dist, d1)
    f1 = np.clip(d1 * 1.35, 0.0, 1.0)
    edge = np.clip((d2 - d1) * 1.85, 0.0, 1.0)
    return f1, edge


def _tile_pores(size: int, cells: int, seed: int) -> np.ndarray:
    """Seamless periodic Voronoi F1 micro-pore/pebble field in [0, 1] (0 = cell center)."""
    f1, _ = _tile_voronoi_f1_f2(size, cells, seed)
    return f1


def _smoothstep(edge0: float, edge1: float, x: np.ndarray) -> np.ndarray:
    t = np.clip((x - edge0) / max(1e-6, edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _sample_periodic_bilinear(field: np.ndarray, u_idx: np.ndarray, v_idx: np.ndarray) -> np.ndarray:
    """Bilinearly sample a periodic (H, W) array at floating-point pixel coordinates."""
    h, w = field.shape
    x0 = np.floor(u_idx).astype(np.int32) % w
    y0 = np.floor(v_idx).astype(np.int32) % h
    x1 = (x0 + 1) % w
    y1 = (y0 + 1) % h
    fx = (u_idx - np.floor(u_idx)).astype(np.float32)
    fy = (v_idx - np.floor(v_idx)).astype(np.float32)
    top = field[y0, x0] * (1.0 - fx) + field[y0, x1] * fx
    bot = field[y1, x0] * (1.0 - fx) + field[y1, x1] * fx
    return top * (1.0 - fy) + bot * fy


def _height_to_normal_rgb(height: np.ndarray, strength: float) -> np.ndarray:
    """Convert periodic 2D heightfield [0, 1] into OpenGL (+Y up) tangent-space RGB normal map."""
    h = np.asarray(height, dtype=np.float32)
    hx = (
        (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 2.0
        + (np.roll(np.roll(h, -1, axis=0), -1, axis=1) - np.roll(np.roll(h, -1, axis=0), 1, axis=1))
        + (np.roll(np.roll(h, 1, axis=0), -1, axis=1) - np.roll(np.roll(h, 1, axis=0), 1, axis=1))
    ) * (0.125 * strength)
    hy = (
        (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 2.0
        + (np.roll(np.roll(h, -1, axis=0), -1, axis=1) - np.roll(np.roll(h, 1, axis=0), -1, axis=1))
        + (np.roll(np.roll(h, -1, axis=0), 1, axis=1) - np.roll(np.roll(h, 1, axis=0), 1, axis=1))
    ) * (0.125 * strength)

    nx = -hx
    ny = -hy
    nz = np.ones_like(h, dtype=np.float32)
    inv_len = 1.0 / np.sqrt(nx * nx + ny * ny + nz * nz)
    rgb = np.stack(
        [
            np.clip((nx * inv_len * 0.5 + 0.5) * 255.0 + 0.5, 0.0, 255.0),
            np.clip((ny * inv_len * 0.5 + 0.5) * 255.0 + 0.5, 0.0, 255.0),
            np.clip((nz * inv_len * 0.5 + 0.5) * 255.0 + 0.5, 0.0, 255.0),
        ],
        axis=-1,
    )
    return rgb.astype(np.uint8)


def _gray_to_rgb_png(vals_01: np.ndarray) -> bytes:
    u8 = np.clip(vals_01 * 255.0 + 0.5, 0.0, 255.0).astype(np.uint8)
    return _encode_png_rgb(np.repeat(u8[:, :, None], 3, axis=2))


def _rgb_to_png(rgb_01: np.ndarray) -> bytes:
    """Encode an (H, W, 3) float array in [0, 1] directly to RGB PNG bytes."""
    u8 = np.clip(rgb_01 * 255.0 + 0.5, 0.0, 255.0).astype(np.uint8)
    return _encode_png_rgb(u8)


def _sdf_segment(px: np.ndarray, py: np.ndarray, x0: float, y0: float, x1: float, y1: float) -> np.ndarray:
    """Euclidean distance from 2D grid (px, py) to line segment (x0, y0)-(x1, y1)."""
    vx, vy = x1 - x0, y1 - y0
    l2 = max(1e-8, vx * vx + vy * vy)
    t = np.clip(((px - x0) * vx + (py - y0) * vy) / l2, 0.0, 1.0)
    return np.hypot(px - (x0 + t * vx), py - (y0 + t * vy))


def _sdf_ring_arc(
    px: np.ndarray, py: np.ndarray, cx: float, cy: float, rx: float, ry: float, a0: float, a1: float
) -> np.ndarray:
    """Approximate Euclidean distance to an elliptical arc spanning [a0, a1] radians."""
    dx = px - cx
    dy = py - cy
    ang = np.arctan2(dy / max(1e-5, ry), dx / max(1e-5, rx))
    # Unwrap angular difference relative to arc midpoint so wrap-around arcs work cleanly.
    mid = 0.5 * (a0 + a1)
    half = 0.5 * abs(a1 - a0)
    diff = np.arctan2(np.sin(ang - mid), np.cos(ang - mid))
    clamped = mid + np.clip(diff, -half, half)
    ax = cx + rx * np.cos(clamped)
    ay = cy + ry * np.sin(clamped)
    return np.hypot(px - ax, py - ay)


def _mr_to_rgb_png(rough_mult: np.ndarray, metal_mult: Optional[np.ndarray] = None) -> bytes:
    """Encode glTF 2.0 metallicRoughnessTexture (G=roughness mult, B=metallic mult)."""
    g = np.clip(rough_mult * 255.0 + 0.5, 0.0, 255.0).astype(np.uint8)
    if metal_mult is None:
        b = np.full_like(g, 255)
    else:
        b = np.clip(metal_mult * 255.0 + 0.5, 0.0, 255.0).astype(np.uint8)
    r = np.full_like(g, 255)
    return _encode_png_rgb(np.stack([r, g, b], axis=-1))


def _build_chitin_pack(size: int) -> TexturePack:
    """Rich organic insectoid beetle/ant carapace: dimpled pores, curved striations & wear."""
    f1_coarse, edge_coarse = _tile_voronoi_f1_f2(size, 26, seed=101)
    f1_fine = _tile_pores(size, 48, seed=102)

    # Crisp dimpled beetle micro-pits: 0 at pit bottom, 1 on smooth raised carapace ridges.
    pit_a = _smoothstep(0.14, 0.44, f1_coarse)
    pit_b = _smoothstep(0.12, 0.36, f1_fine)
    # Subtle polygonal carapace micro-cell bevel between neighboring pores.
    cell_ridge = 1.0 - _smoothstep(0.0, 0.22, edge_coarse)

    # Domain-warped curved chitin growth striations (organic flow instead of straight lines).
    warp = (_tile_noise(size, 6, 6, seed=103) - 0.5) * (size * 0.045)
    base_stria = _tile_noise(size, 8, 52, seed=104)
    gx, gy = np.meshgrid(np.arange(size, dtype=np.float32), np.arange(size, dtype=np.float32))
    curved_stria = _sample_periodic_bilinear(base_stria, gx + warp * 0.5, gy + warp)

    # Faint battle-wear micro-scratches along two diagonal periodic directions.
    diag_u = (gx + gy) % float(size)
    diag_v = (gx - gy) % float(size)
    streak_src_a = _tile_noise(size, 4, 68, seed=105)
    streak_src_b = _tile_noise(size, 5, 84, seed=106)
    scuff_a = _smoothstep(0.84, 0.96, _sample_periodic_bilinear(streak_src_a, diag_u, diag_v))
    scuff_b = _smoothstep(0.87, 0.98, _sample_periodic_bilinear(streak_src_b, diag_v, gx))
    scratches = np.clip(scuff_a * 0.75 + scuff_b * 0.55, 0.0, 1.0)

    # Multi-scale domain-warped organic mottled hide variation (weathered Pixar insect skin,
    # never flat plastic and never harsh static) + faint dusty micro-speckle.
    mottle_warp_x = (_tile_noise(size, 5, 5, seed=110) - 0.5) * (size * 0.065)
    mottle_warp_y = (_tile_noise(size, 5, 5, seed=111) - 0.5) * (size * 0.065)
    patina_macro = _sample_periodic_bilinear(
        _tile_noise(size, 4, 4, seed=112), gx + mottle_warp_x, gy + mottle_warp_y
    )
    patina_broad = _sample_periodic_bilinear(
        _tile_noise(size, 9, 9, seed=107), gx + 0.6 * mottle_warp_x, gy + 0.6 * mottle_warp_y
    )
    patina_med = _tile_noise(size, 18, 18, seed=108)
    speckle = _tile_noise(size, 56, 56, seed=109)
    dust_speck = _smoothstep(0.68, 0.92, speckle)

    height = (
        0.38 * pit_a
        + 0.20 * pit_b
        + 0.08 * cell_ridge
        + 0.16 * curved_stria
        + 0.10 * patina_broad
        + 0.10 * speckle
        - 0.12 * scratches
    )
    height = np.clip(height, 0.0, 1.0)

    # Tactile organic mottled hide (~0.83..1.00, mean ~0.935) with gentle crevice pore darkening.
    albedo = (
        0.842
        + 0.052 * patina_macro
        + 0.036 * patina_broad
        + 0.022 * patina_med
        + 0.030 * pit_a
        + 0.014 * pit_b
        + 0.010 * curved_stria
        - 0.020 * dust_speck
        + 0.014 * scratches
    )

    # Organic satin-to-matte roughness breakup (~0.60..1.00): smooth carapace crowns catch
    # rim highlights while mottled hide patches, micro-pores, and scuffs stay softly matte.
    rough_mult = (
        0.60
        + 0.16 * (1.0 - pit_a)
        + 0.08 * (1.0 - pit_b)
        + 0.08 * (1.0 - patina_macro)
        + 0.06 * (1.0 - patina_broad)
        + 0.06 * dust_speck
        + 0.08 * scratches
    )
    return TexturePack(
        family="chitin",
        size=size,
        base_color_png=_gray_to_rgb_png(np.clip(albedo, 0.82, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=3.1)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(rough_mult, 0.58, 1.0)),
    )


def _build_cloth_pack(size: int) -> TexturePack:
    """Crisp cross-hatch woven canvas warp/weft with organic yarn slub and tactile fiber grain."""
    t = np.linspace(0.0, 2.0 * math.pi, size, endpoint=False, dtype=np.float32)
    u, v = np.meshgrid(t, t)
    threads = 24.0 if size <= 128 else 32.0
    warp_phase = np.sin(u * threads)
    weft_phase = np.sin(v * threads)
    checker = np.sign(np.sin(u * threads * 0.5) * np.sin(v * threads * 0.5))
    warp = 0.5 + 0.5 * np.cos(u * threads)
    weft = 0.5 + 0.5 * np.cos(v * threads)
    weave = np.where(checker >= 0.0, 0.70 * warp + 0.30 * (1.0 - weft), 0.70 * weft + 0.30 * (1.0 - warp))
    cross_ridge = 0.5 + 0.25 * (warp_phase * weft_phase)
    fiber = _tile_noise(size, 44, 44, seed=201)
    slub = _tile_noise(size, 10, 10, seed=202)
    yarn_var = _tile_noise(size, 5, 5, seed=203)

    height = 0.64 * weave + 0.16 * cross_ridge + 0.14 * fiber + 0.06 * slub
    albedo = 0.77 + 0.16 * weave + 0.04 * slub + 0.03 * yarn_var
    rough_mult = 0.74 + 0.26 * (1.0 - weave)
    return TexturePack(
        family="cloth",
        size=size,
        base_color_png=_gray_to_rgb_png(np.clip(albedo, 0.74, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=5.0)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(rough_mult, 0.74, 1.0)),
    )


def _build_metal_pack(size: int) -> TexturePack:
    """Crisp directional brushed metallic sheen with fine anisotropic grain and edge patina."""
    brush_a = _tile_noise(size, 3, 60, seed=301)
    brush_b = _tile_noise(size, 6, 72, seed=302)
    cross_scratch = _tile_noise(size, 40, 5, seed=303)
    patina = _tile_noise(size, 10, 10, seed=304)
    pitting = _tile_pores(size, 22, seed=305)
    pit_mask = _smoothstep(0.12, 0.32, pitting)

    height = 0.48 * brush_a + 0.28 * brush_b + 0.13 * cross_scratch + 0.11 * pit_mask
    albedo = 0.78 + 0.13 * brush_a + 0.04 * brush_b + 0.05 * patina
    rough_mult = 0.50 + 0.26 * (1.0 - brush_a) + 0.16 * (1.0 - patina) + 0.10 * (1.0 - pit_mask)
    metal_mult = 0.88 + 0.12 * pit_mask
    return TexturePack(
        family="metal",
        size=size,
        base_color_png=_gray_to_rgb_png(np.clip(albedo, 0.75, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=3.9)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(rough_mult, 0.50, 1.0), np.clip(metal_mult, 0.86, 1.0)),
    )


def _build_rubber_pack(size: int) -> TexturePack:
    """Fine tactical molded rubber/cast-iron micro-pebble stippling."""
    peb_a = _tile_pores(size, 26, seed=401)
    peb_b = _tile_pores(size, 42, seed=402)
    stipple = 0.62 * (1.0 - peb_a) + 0.38 * (1.0 - peb_b)
    fine = _tile_noise(size, 36, 36, seed=403)

    height = 0.80 * stipple + 0.20 * fine
    albedo = 0.82 + 0.13 * stipple + 0.05 * fine
    rough_mult = 0.72 + 0.28 * (1.0 - stipple)
    return TexturePack(
        family="rubber",
        size=size,
        base_color_png=_gray_to_rgb_png(np.clip(albedo, 0.78, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=4.4)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(rough_mult, 0.72, 1.0)),
    )


def _build_fur_pack(size: int) -> TexturePack:
    """Soft combed plush teddy-bear fur: directional micro-strands, fleece clumps, velvety matte sheen."""
    gx, gy = np.meshgrid(np.arange(size, dtype=np.float32), np.arange(size, dtype=np.float32))
    warp = (_tile_noise(size, 6, 6, seed=501) - 0.5) * (size * 0.08)
    strands_a = _sample_periodic_bilinear(_tile_noise(size, 64, 10, seed=502), gx + warp, gy + 0.4 * warp)
    strands_b = _sample_periodic_bilinear(_tile_noise(size, 42, 16, seed=503), gx - 0.6 * warp, gy + warp)
    clumps = _tile_noise(size, 14, 14, seed=504)
    fuzz = _tile_noise(size, 48, 48, seed=505)
    height = np.clip(0.42 * strands_a + 0.28 * strands_b + 0.18 * clumps + 0.12 * fuzz, 0.0, 1.0)
    albedo = 0.74 + 0.14 * strands_a + 0.07 * clumps + 0.05 * fuzz
    rough_mult = 0.86 + 0.14 * (1.0 - strands_a)
    return TexturePack(
        family="fur",
        size=size,
        base_color_png=_gray_to_rgb_png(np.clip(albedo, 0.72, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=5.4)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(rough_mult, 0.84, 1.0)),
    )


def _build_bread_pack(size: int) -> TexturePack:
    """Realistic baked bakery crust & crispy fried batter: porous crumb bubbles + Maillard mottling."""
    pores_a = _tile_pores(size, 18, seed=601)
    pores_b = _tile_pores(size, 34, seed=602)
    crumb = _tile_noise(size, 28, 28, seed=603)
    toast = _tile_noise(size, 7, 7, seed=604)
    blister = _smoothstep(0.18, 0.58, pores_a) * 0.62 + _smoothstep(0.16, 0.50, pores_b) * 0.38
    height = np.clip(0.56 * blister + 0.26 * crumb + 0.18 * toast, 0.0, 1.0)
    albedo = 0.76 + 0.12 * toast + 0.08 * blister + 0.04 * crumb
    rough_mult = 0.72 + 0.24 * (1.0 - blister) + 0.04 * (1.0 - toast)
    return TexturePack(
        family="bread",
        size=size,
        base_color_png=_gray_to_rgb_png(np.clip(albedo, 0.72, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=4.8)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(rough_mult, 0.70, 1.0)),
    )


def _build_bun_white_pack(size: int) -> TexturePack:
    """Silky-smooth soft white-bread hamburger bun crust: gentle oven blush, ultra-fine matte dough grain."""
    blush = _tile_noise(size, 5, 5, seed=611)
    soft_dimple = _tile_noise(size, 14, 14, seed=612)
    micro_flour = _tile_noise(size, 40, 40, seed=613)
    height = np.clip(0.46 * soft_dimple + 0.34 * blush + 0.20 * micro_flour, 0.0, 1.0)
    albedo = 0.91 + 0.05 * blush + 0.025 * soft_dimple + 0.015 * micro_flour
    rough_mult = 0.76 + 0.16 * (1.0 - blush) + 0.08 * micro_flour
    return TexturePack(
        family="bun_white",
        size=size,
        base_color_png=_gray_to_rgb_png(np.clip(albedo, 0.89, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=1.15)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(rough_mult, 0.72, 1.0)),
    )


def _build_white_crumb_pack(size: int) -> TexturePack:
    """Airy spongy torn white-bread interior crumb: soft yeast pockets, bright clean matte ivory pores."""
    cells_a = _tile_pores(size, 22, seed=621)
    cells_b = _tile_pores(size, 44, seed=622)
    sponge = _tile_noise(size, 18, 18, seed=623)
    pocket = _smoothstep(0.14, 0.56, cells_a) * 0.58 + _smoothstep(0.12, 0.48, cells_b) * 0.42
    height = np.clip(0.60 * pocket + 0.40 * sponge, 0.0, 1.0)
    albedo = 0.84 + 0.10 * pocket + 0.06 * sponge
    rough_mult = 0.88 + 0.12 * (1.0 - pocket)
    return TexturePack(
        family="white_crumb",
        size=size,
        base_color_png=_gray_to_rgb_png(np.clip(albedo, 0.82, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=3.2)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(rough_mult, 0.86, 1.0)),
    )


def _build_lettuce_leaf_pack(size: int) -> TexturePack:
    """Crisp crinkled iceberg/leaf lettuce: organic branching cellular dimples & fresh dewy sheen (zero grass stripes)."""
    gx, gy = np.meshgrid(np.arange(size, dtype=np.float32), np.arange(size, dtype=np.float32))
    warp_x = (_tile_noise(size, 6, 6, seed=711) - 0.5) * (size * 0.12)
    warp_y = (_tile_noise(size, 6, 6, seed=712) - 0.5) * (size * 0.12)
    crinkle = _sample_periodic_bilinear(_tile_noise(size, 10, 10, seed=713), gx + warp_x, gy + warp_y)
    f1, edge = _tile_voronoi_f1_f2(size, 16, seed=714)
    veins = 1.0 - _smoothstep(0.02, 0.18, edge)
    blisters = _smoothstep(0.08, 0.52, 1.0 - f1)
    micro = _tile_noise(size, 36, 36, seed=715)
    height = np.clip(0.42 * crinkle + 0.28 * blisters + 0.18 * veins + 0.12 * micro, 0.0, 1.0)
    albedo = 0.82 + 0.10 * veins + 0.05 * crinkle + 0.03 * blisters
    rough_mult = 0.44 + 0.26 * (1.0 - blisters) + 0.12 * veins
    return TexturePack(
        family="lettuce_leaf",
        size=size,
        base_color_png=_gray_to_rgb_png(np.clip(albedo, 0.80, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=2.6)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(rough_mult, 0.40, 0.85)),
    )


def _build_foliage_pack(size: int) -> TexturePack:
    """Botanical grass/leaf venation: fine parallel veins, cellular ribbing, subtle waxy sheen."""
    t = np.linspace(0.0, 2.0 * math.pi, size, endpoint=False, dtype=np.float32)
    u, v = np.meshgrid(t, t)
    ribs = 0.5 + 0.5 * np.cos(u * 18.0 + 0.35 * np.sin(v * 3.0))
    micro_rib = 0.5 + 0.5 * np.cos(u * 44.0)
    varieg = _tile_noise(size, 6, 12, seed=701)
    cells = _tile_noise(size, 36, 20, seed=702)
    height = np.clip(0.50 * ribs + 0.22 * micro_rib + 0.18 * cells + 0.10 * varieg, 0.0, 1.0)
    albedo = 0.80 + 0.10 * ribs + 0.06 * varieg + 0.04 * cells
    rough_mult = 0.56 + 0.24 * (1.0 - ribs) + 0.12 * (1.0 - varieg)
    return TexturePack(
        family="foliage",
        size=size,
        base_color_png=_gray_to_rgb_png(np.clip(albedo, 0.76, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=3.4)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(rough_mult, 0.52, 1.0)),
    )


def _build_leaf_vein_pack(size: int) -> TexturePack:
    """Velvety broadleaf epidermis: organic reticulated micro-veinlets & soft chlorophyll mottle (zero grass stripes!)."""
    f1_a, edge_a = _tile_voronoi_f1_f2(size, 14, seed=731)
    f1_b, edge_b = _tile_voronoi_f1_f2(size, 32, seed=732)
    vein_a = 1.0 - _smoothstep(0.02, 0.16, edge_a)
    vein_b = 1.0 - _smoothstep(0.02, 0.22, edge_b)
    mottle = _tile_noise(size, 8, 8, seed=733)
    cells = _tile_noise(size, 36, 36, seed=734)
    height = np.clip(0.36 * vein_a + 0.20 * vein_b + 0.24 * (1.0 - f1_a) + 0.20 * cells, 0.0, 1.0)
    albedo = 0.84 + 0.08 * vein_a + 0.05 * mottle + 0.03 * cells
    rough_mult = 0.68 + 0.18 * (1.0 - vein_a) + 0.08 * (1.0 - mottle)
    return TexturePack(
        family="leaf_vein",
        size=size,
        base_color_png=_gray_to_rgb_png(np.clip(albedo, 0.80, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=2.0)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(rough_mult, 0.60, 0.90)),
    )


def _build_stone_pack(size: int) -> TexturePack:
    """Weathered fractured granite/rock: crystalline mineral facets, fissures, and gritty pores."""
    f1, edge = _tile_voronoi_f1_f2(size, 14, seed=801)
    f2_fine = _tile_pores(size, 32, seed=802)
    grit = _tile_noise(size, 44, 44, seed=803)
    mottle = _tile_noise(size, 8, 8, seed=804)
    crack = _smoothstep(0.02, 0.16, edge)
    height = np.clip(0.36 * crack + 0.26 * (1.0 - f1) + 0.20 * grit + 0.18 * mottle, 0.0, 1.0)
    albedo = 0.70 + 0.12 * crack + 0.10 * mottle + 0.08 * (1.0 - f2_fine)
    rough_mult = 0.68 + 0.22 * (1.0 - crack) + 0.10 * grit
    return TexturePack(
        family="stone",
        size=size,
        base_color_png=_gray_to_rgb_png(np.clip(albedo, 0.66, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=5.6)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(rough_mult, 0.65, 1.0)),
    )


def _build_soil_pack(size: int) -> TexturePack:
    """Granular packed earth & anthill loam: pebble grains, crumbly clods, matte humus breakup."""
    peb_a = _tile_pores(size, 22, seed=901)
    peb_b = _tile_pores(size, 46, seed=902)
    clod = _tile_noise(size, 10, 10, seed=903)
    sand = _tile_noise(size, 52, 52, seed=904)
    grains = 0.55 * (1.0 - peb_a) + 0.45 * (1.0 - peb_b)
    height = np.clip(0.54 * grains + 0.26 * clod + 0.20 * sand, 0.0, 1.0)
    albedo = 0.72 + 0.14 * clod + 0.08 * grains + 0.06 * sand
    rough_mult = 0.82 + 0.18 * (1.0 - grains)
    return TexturePack(
        family="soil",
        size=size,
        base_color_png=_gray_to_rgb_png(np.clip(albedo, 0.68, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=5.8)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(rough_mult, 0.80, 1.0)),
    )


def _build_wood_pack(size: int) -> TexturePack:
    """Natural longitudinal wood grain with subtle growth rings and fibrous bark micro-relief."""
    gx, gy = np.meshgrid(np.arange(size, dtype=np.float32), np.arange(size, dtype=np.float32))
    warp = (_tile_noise(size, 5, 5, seed=951) - 0.5) * (size * 0.06)
    rings = np.sin(((gx + warp) / float(size)) * 2.0 * math.pi * 12.0) * 0.5 + 0.5
    fibers = _tile_noise(size, 48, 6, seed=952)
    tone = _tile_noise(size, 8, 4, seed=953)
    height = np.clip(0.45 * rings + 0.35 * fibers + 0.20 * tone, 0.0, 1.0)
    albedo = 0.76 + 0.12 * rings + 0.07 * fibers + 0.05 * tone
    rough_mult = 0.70 + 0.24 * (1.0 - rings) + 0.06 * fibers
    return TexturePack(
        family="wood",
        size=size,
        base_color_png=_gray_to_rgb_png(np.clip(albedo, 0.72, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=4.2)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(rough_mult, 0.68, 1.0)),
    )


def _build_bark_wood_pack(size: int) -> TexturePack:
    """Authentic weathered woodland twig bark: domain-warped fissures, bark plates, pale lenticels, knots & lichen flecks (`327:stick1`, `328:stick2a`)."""
    gx, gy = np.meshgrid(np.arange(size, dtype=np.float32), np.arange(size, dtype=np.float32))
    # Heavy longitudinal domain warp so bark furrows branch, pinch, and twist organically (zero uniform stripes!)
    wx = (_tile_noise(size, 6, 4, seed=955) - 0.5) * (size * 0.22) + (_tile_noise(size, 14, 7, seed=956) - 0.5) * (size * 0.08)
    wy = (_tile_noise(size, 5, 8, seed=957) - 0.5) * (size * 0.14)
    # Stretched Voronoi cells create real elongated rough bark plates separated by deep dark branching fissures!
    # Integer X multipliers guarantee 100% seamless periodic tiling in BOTH U and V with 2x-3x vertical plate stretch.
    f1_a, edge_a = _tile_voronoi_f1_f2(size, 10, seed=958)
    f1_b, edge_b = _tile_voronoi_f1_f2(size, 22, seed=959)
    plate_a = _sample_periodic_bilinear(edge_a, gx * 2.0 + wx, gy + wy)
    plate_b = _sample_periodic_bilinear(edge_b, gx * 3.0 + wx * 1.15, gy + wy * 1.10)
    fibers = _tile_noise(size, 42, 10, seed=960)
    mottle = _tile_noise(size, 8, 6, seed=962)
    # Horizontal bark lenticel dashes + weathered dusty grey lichen patches
    lenticel = _smoothstep(0.76, 0.90, _tile_noise(size, 10, 46, seed=963)) * _smoothstep(0.38, 0.72, mottle)
    lichen = _smoothstep(0.74, 0.88, _tile_noise(size, 9, 9, seed=964)) * _smoothstep(0.35, 0.65, plate_a)

    furrow = _smoothstep(0.03, 0.24, plate_a) * 0.64 + _smoothstep(0.02, 0.22, plate_b) * 0.36
    height = np.clip(0.56 * furrow + 0.26 * fibers + 0.18 * mottle - 0.10 * lenticel, 0.0, 1.0)

    # Natural fallen twig wood tones: deep dark umber bark fissures -> weathered walnut/chestnut bark -> sun-rubbed dry tan ridges
    crack_col = np.array([0.10, 0.06, 0.035], dtype=np.float32)
    bark_mid = np.array([0.44, 0.29, 0.17], dtype=np.float32)
    ridge_tan = np.array([0.80, 0.60, 0.40], dtype=np.float32)
    lichen_grey = np.array([0.56, 0.55, 0.47], dtype=np.float32)
    lent_pale = np.array([0.86, 0.72, 0.54], dtype=np.float32)

    t1 = _smoothstep(0.16, 0.50, height)
    t2 = _smoothstep(0.50, 0.84, height)
    rgb = crack_col[None, None, :] * (1.0 - t1[:, :, None]) + bark_mid[None, None, :] * t1[:, :, None]
    rgb = rgb * (1.0 - t2[:, :, None]) + ridge_tan[None, None, :] * t2[:, :, None]
    rgb = rgb * (1.0 - 0.36 * lichen[:, :, None]) + lichen_grey[None, None, :] * (0.36 * lichen[:, :, None])
    rgb = rgb * (1.0 - 0.44 * lenticel[:, :, None]) + lent_pale[None, None, :] * (0.44 * lenticel[:, :, None])

    rough_mult = np.clip(0.96 - 0.12 * t2 + 0.04 * (1.0 - fibers), 0.78, 0.98)
    return TexturePack(
        family="bark_wood",
        size=size,
        base_color_png=_rgb_to_png(np.clip(rgb, 0.0, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=6.2)),
        metallic_roughness_png=_mr_to_rgb_png(rough_mult),
    )


def _build_vinyl_rubber_pack(size: int) -> TexturePack:
    """Soft-touch matte bath rubber & tactile satin vinyl: micro-pebble grain, zero cheap plastic glare."""
    pebble_a = _tile_pores(size, 34, seed=961)
    pebble_b = _tile_noise(size, 52, 52, seed=962)
    mottle = _tile_noise(size, 8, 8, seed=963)
    stipple = 1.0 - _smoothstep(0.10, 0.48, pebble_a)
    height = np.clip(0.42 * stipple + 0.34 * pebble_b + 0.24 * mottle, 0.0, 1.0)
    albedo = 0.90 + 0.05 * mottle + 0.03 * pebble_b + 0.02 * (1.0 - stipple)
    rough_mult = 0.80 + 0.14 * stipple + 0.06 * (1.0 - mottle)
    return TexturePack(
        family="vinyl_rubber",
        size=size,
        base_color_png=_gray_to_rgb_png(np.clip(albedo, 0.86, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=2.6)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(rough_mult, 0.78, 1.0)),
    )


def _build_dino_skin_pack(size: int) -> TexturePack:
    """Pebbled reptilian dermal scales & matte sculpted toy-dinosaur hide with tactile micro-relief."""
    f1_sc, edge_sc = _tile_voronoi_f1_f2(size, 18, seed=971)
    f1_fine = _tile_pores(size, 42, seed=972)
    mottle = _tile_noise(size, 7, 7, seed=973)
    grit = _tile_noise(size, 44, 44, seed=974)
    # Domed polygonal dermal scales separated by crisp dark grooves.
    scale_dome = _smoothstep(0.04, 0.30, edge_sc) * (1.0 - 0.35 * f1_sc)
    micro_peb = 1.0 - _smoothstep(0.12, 0.45, f1_fine)
    height = np.clip(0.56 * scale_dome + 0.22 * micro_peb + 0.12 * grit + 0.10 * mottle, 0.0, 1.0)
    albedo = 0.76 + 0.14 * scale_dome + 0.07 * mottle + 0.03 * grit
    rough_mult = 0.74 + 0.22 * (1.0 - scale_dome) + 0.04 * grit
    return TexturePack(
        family="dino_skin",
        size=size,
        base_color_png=_gray_to_rgb_png(np.clip(albedo, 0.72, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=4.8)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(rough_mult, 0.72, 1.0)),
    )


def _build_banana_slice_pack(size: int) -> TexturePack:
    """Authentic sliced banana coin face: creamy pale-yellow flesh, outer cortex rim & 3-armed amber seed star (`366:fdjelo1`)."""
    lin = np.linspace(-1.0, 1.0, size, dtype=np.float32)
    px, py = np.meshgrid(lin, lin)
    r = np.hypot(px, py)
    ang = np.arctan2(py, px)
    pulp_noise = _tile_noise(size, 22, 22, seed=985)
    fine_grain = _tile_noise(size, 44, 44, seed=986)

    # Creamy butter-yellow flesh with slightly brighter ivory outer cortex and faint warm skin-cut perimeter.
    cream = np.array([1.00, 0.94, 0.66], dtype=np.float32)
    outer_ivory = np.array([1.00, 0.97, 0.80], dtype=np.float32)
    rim_tan = np.array([0.88, 0.74, 0.44], dtype=np.float32)
    core_honey = np.array([0.96, 0.80, 0.44], dtype=np.float32)
    star_amber = np.array([0.52, 0.30, 0.10], dtype=np.float32)
    seed_dark = np.array([0.24, 0.12, 0.04], dtype=np.float32)

    radial_fibers = 0.5 + 0.5 * np.cos(ang * 15.0 + pulp_noise * 2.2)
    rgb = cream[None, None, :] * (0.93 + 0.07 * pulp_noise[:, :, None] + 0.03 * radial_fibers[:, :, None])
    cortex = _smoothstep(0.62, 0.84, r) * (1.0 - _smoothstep(0.88, 0.97, r))
    rgb = rgb * (1.0 - 0.55 * cortex[:, :, None]) + outer_ivory[None, None, :] * (0.55 * cortex[:, :, None])
    rim = _smoothstep(0.89, 0.98, r)
    rgb = rgb * (1.0 - 0.65 * rim[:, :, None]) + rim_tan[None, None, :] * (0.65 * rim[:, :, None])

    # Slightly translucent golden-honey seed core zone in the center.
    inner_core = 1.0 - _smoothstep(0.18, 0.48, r)
    rgb = rgb * (1.0 - 0.36 * inner_core[:, :, None]) + core_honey[None, None, :] * (0.36 * inner_core[:, :, None])

    # Iconic 3-armed Y-shaped banana carpel clefts (120 deg apart) + delicate dark banana seed specks!
    d_star = np.full_like(px, 10.0)
    d_seeds = np.full_like(px, 10.0)
    for k in range(3):
        a = k * (2.0 * math.pi / 3.0) + 0.35
        ca, sa = math.cos(a), math.sin(a)
        tx, ty = -sa, ca
        d_star = np.minimum(d_star, _sdf_segment(px, py, 0.0, 0.0, 0.36 * ca, 0.36 * sa))
        for sr, off in ((0.14, +0.048), (0.14, -0.048), (0.26, +0.052), (0.26, -0.052)):
            sx = sr * ca + off * tx
            sy = sr * sa + off * ty
            d_seeds = np.minimum(d_seeds, np.hypot(px - sx, py - sy))
    # Subtle fibrous radiating spokes extending gently from the Y-star into the creamy flesh.
    star_mask = (1.0 - _smoothstep(0.018, 0.068, d_star)) * (0.78 + 0.22 * pulp_noise)
    seed_mask = 1.0 - _smoothstep(0.018, 0.044, d_seeds)
    rgb = rgb * (1.0 - 0.72 * star_mask[:, :, None]) + star_amber[None, None, :] * (0.72 * star_mask[:, :, None])
    rgb = rgb * (1.0 - 0.85 * seed_mask[:, :, None]) + seed_dark[None, None, :] * (0.85 * seed_mask[:, :, None])

    height = np.clip(0.55 + 0.20 * pulp_noise + 0.10 * fine_grain - 0.28 * star_mask - 0.16 * seed_mask, 0.0, 1.0)
    rough_mult = np.clip(0.46 + 0.18 * (1.0 - inner_core) + 0.12 * fine_grain - 0.14 * star_mask, 0.30, 0.82)
    return TexturePack(
        family="banana_slice",
        size=size,
        base_color_png=_rgb_to_png(np.clip(rgb, 0.0, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=2.2)),
        metallic_roughness_png=_mr_to_rgb_png(rough_mult),
    )


def _build_fried_chicken_pack(size: int) -> TexturePack:
    """Photorealistic crispy Southern fried chicken crust: strongly domain-warped irregular batter flakes, oil bubbles & savory golden-brown Maillard tones (`399:fdchick1`)."""
    gx, gy = np.meshgrid(np.arange(size, dtype=np.float32), np.arange(size, dtype=np.float32))
    # Heavy two-octave organic domain warp eliminates any regular tile/cobblestone pattern!
    wx1 = (_tile_noise(size, 5, 5, seed=741) - 0.5) * (size * 0.28)
    wy1 = (_tile_noise(size, 5, 5, seed=742) - 0.5) * (size * 0.28)
    wx2 = (_tile_noise(size, 13, 13, seed=748) - 0.5) * (size * 0.12)
    wy2 = (_tile_noise(size, 13, 13, seed=749) - 0.5) * (size * 0.12)
    sx, sy = gx + wx1 + wx2, gy + wy1 + wy2

    raw_f1a, raw_ea = _tile_voronoi_f1_f2(size, 9, seed=743)
    raw_f1b, raw_eb = _tile_voronoi_f1_f2(size, 22, seed=744)
    ea = _sample_periodic_bilinear(raw_ea, sx, sy)
    eb = _sample_periodic_bilinear(raw_eb, sx * 1.0, sy * 1.0)
    f1b = _sample_periodic_bilinear(raw_f1b, sx, sy)
    crumbs = _sample_periodic_bilinear(_tile_noise(size, 18, 18, seed=745), sx, sy)
    micro = _tile_noise(size, 52, 52, seed=746)
    pores = _tile_pores(size, 36, seed=747)
    pepper = (micro > 0.88).astype(np.float32) * (crumbs < 0.52).astype(np.float32)

    flake_ridge = _smoothstep(0.03, 0.30, ea) * 0.54 + _smoothstep(0.02, 0.28, eb) * 0.46
    height = np.clip(0.38 * flake_ridge + 0.24 * (1.0 - f1b) + 0.24 * crumbs + 0.14 * micro - 0.18 * pores, 0.0, 1.0)

    # Authentic fried chicken Maillard palette: deep toasted chestnut crevices -> savory golden-brown -> crisp pale honey-gold flakes!
    deep_roast = np.array([0.26, 0.11, 0.04], dtype=np.float32)
    golden_brown = np.array([0.68, 0.38, 0.14], dtype=np.float32)
    honey_crisp = np.array([0.92, 0.68, 0.30], dtype=np.float32)
    bright_flake = np.array([0.99, 0.84, 0.46], dtype=np.float32)

    t1 = _smoothstep(0.14, 0.50, height)
    t2 = _smoothstep(0.48, 0.78, height)
    t3 = _smoothstep(0.76, 0.94, height)
    rgb = deep_roast[None, None, :] * (1.0 - t1[:, :, None]) + golden_brown[None, None, :] * t1[:, :, None]
    rgb = rgb * (1.0 - t2[:, :, None]) + honey_crisp[None, None, :] * t2[:, :, None]
    rgb = rgb * (1.0 - t3[:, :, None]) + bright_flake[None, None, :] * t3[:, :, None]
    rgb = rgb * (1.0 - 0.45 * pepper[:, :, None])

    # Authentic dry crunchy flour crevices + warm oil-kissed blister peaks
    rough_mult = np.clip(0.88 - 0.34 * t2 - 0.14 * t3 + 0.12 * pores, 0.40, 0.95)
    return TexturePack(
        family="fried_chicken",
        size=size,
        base_color_png=_rgb_to_png(np.clip(rgb, 0.0, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=4.6)),
        metallic_roughness_png=_mr_to_rgb_png(rough_mult),
    )


def _build_cornmeal_batter_pack(size: int) -> TexturePack:
    """Classic fairground fried corn-dog batter: fine sandy golden cornmeal pebble grain & micro fry-oil pinholes (`361:fdcornd1`)."""
    coarse = _tile_noise(size, 14, 14, seed=761)
    meal = _tile_noise(size, 36, 36, seed=762)
    fine = _tile_noise(size, 64, 64, seed=763)
    pinholes = _tile_pores(size, 34, seed=764)
    height = np.clip(0.32 * coarse + 0.40 * meal + 0.28 * fine - 0.24 * pinholes, 0.0, 1.0)
    # Warm golden fried cornmeal speckle tones
    albedo = np.clip(0.82 + 0.10 * coarse + 0.08 * meal + 0.05 * fine - 0.16 * pinholes, 0.64, 1.0)
    rough_mult = np.clip(0.68 + 0.18 * (1.0 - meal) + 0.14 * pinholes, 0.50, 0.94)
    return TexturePack(
        family="cornmeal_batter",
        size=size,
        base_color_png=_gray_to_rgb_png(albedo),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=3.0)),
        metallic_roughness_png=_mr_to_rgb_png(rough_mult),
    )


def _build_organic_flesh_pack(size: int) -> TexturePack:
    """Fine cellular matte-moist organic texture for meats, produce, cheese, petals, and food surfaces."""
    cells = _tile_pores(size, 28, seed=981)
    fibers = _tile_noise(size, 36, 18, seed=982)
    mottle = _tile_noise(size, 8, 8, seed=983)
    cell_bump = _smoothstep(0.14, 0.56, cells)
    height = np.clip(0.44 * cell_bump + 0.32 * fibers + 0.24 * mottle, 0.0, 1.0)
    albedo = 0.84 + 0.08 * mottle + 0.05 * cell_bump + 0.03 * fibers
    rough_mult = 0.74 + 0.18 * (1.0 - cell_bump) + 0.08 * fibers
    return TexturePack(
        family="organic_flesh",
        size=size,
        base_color_png=_gray_to_rgb_png(np.clip(albedo, 0.80, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=2.8)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(rough_mult, 0.70, 1.0)),
    )


def _build_cola_label_pack(size: int) -> TexturePack:
    """Crisp printed metallic Critter Cola can label (purple/crimson waves, white-outlined 'Cola', barcode)."""
    u_1d = np.linspace(0.0, 1.0, size, endpoint=False, dtype=np.float32)
    v_1d = np.linspace(0.0, 1.0, size, endpoint=False, dtype=np.float32)
    u, v = np.meshgrid(u_1d, v_1d)

    # Brushed aluminum grain + subtle crumpled metallic facets for realistic crushed soda can sheen.
    brush = _tile_noise(size, 64, 8, seed=991)
    crinkle = _tile_noise(size, 9, 9, seed=992)
    base_indigo = np.array([0.24, 0.18, 0.72], dtype=np.float32)
    deep_navy = np.array([0.11, 0.08, 0.42], dtype=np.float32)
    crimson = np.array([0.90, 0.10, 0.20], dtype=np.float32)
    magenta = np.array([0.82, 0.14, 0.56], dtype=np.float32)
    white = np.array([0.98, 0.98, 1.00], dtype=np.float32)
    gold = np.array([0.98, 0.74, 0.16], dtype=np.float32)
    ink_dark = np.array([0.08, 0.08, 0.12], dtype=np.float32)

    # Royal purple brushed gradient with dramatic diagonal crimson/magenta retro soda waves.
    grad = 0.85 + 0.18 * brush + 0.07 * np.sin(u * 2.0 * math.pi * 2.0)
    rgb = np.clip(base_indigo[None, None, :] * grad[:, :, None], 0.0, 1.0)

    # Diagonal dynamic swoosh bands wrapping around the barrel.
    wave_a = np.sin((u * 2.0 + v * 1.35) * math.pi * 2.0)
    wave_b = np.cos((u * 3.0 - v * 1.8) * math.pi * 2.0)
    mag_mask = _smoothstep(0.28, 0.48, wave_a * 0.65 + wave_b * 0.35)
    rgb = rgb * (1.0 - 0.58 * mag_mask[:, :, None]) + magenta[None, None, :] * (0.58 * mag_mask[:, :, None])

    # Deep navy shadow pockets framing the logo region for high-contrast pop.
    logo_bg = np.exp(-(((u - 0.50) / 0.26) ** 2 + ((v - 0.60) / 0.22) ** 2))
    rgb = rgb * (1.0 - 0.45 * logo_bg[:, :, None]) + deep_navy[None, None, :] * (0.45 * logo_bg[:, :, None])

    # Top & bottom vivid crimson rim stripes with crisp thin white trim lines.
    rim_red = np.maximum(1.0 - _smoothstep(0.09, 0.11, v), _smoothstep(0.89, 0.91, v))
    rgb = rgb * (1.0 - rim_red[:, :, None]) + crimson[None, None, :] * rim_red[:, :, None]
    rim_line = np.maximum(
        1.0 - _smoothstep(0.004, 0.014, np.abs(v - 0.115)),
        1.0 - _smoothstep(0.004, 0.014, np.abs(v - 0.885)),
    )
    rgb = rgb * (1.0 - rim_line[:, :, None]) + white[None, None, :] * rim_line[:, :, None]

    # Crisp white barcode label box on rear-side panel (authentic `238:can1` detail!).
    bc_box = (u >= 0.08) & (u <= 0.25) & (v >= 0.16) & (v <= 0.42)
    bc_inner = (u >= 0.095) & (u <= 0.235) & (v >= 0.18) & (v <= 0.40)
    bar_idx = np.floor((u - 0.095) / 0.0075).astype(np.int32)
    bar_on = ((bar_idx * 7 + (bar_idx // 2) * 3) % 5) < 2
    rgb = np.where(bc_box[:, :, None], white[None, None, :], rgb)
    rgb = np.where((bc_inner & bar_on)[:, :, None], ink_dark[None, None, :], rgb)

    # Retro white-outlined bold "Cola" lettering running cleanly across the hero face (u in [0.28, 0.82], v in [0.40, 0.72]).
    # Evaluate in local logo coordinates (lx left->right, ly top->bottom so PNG y=0 is top!).
    lx = (u - 0.55) / 0.26
    ly = (0.56 - v) / 0.15
    d_cola = np.full_like(u, 10.0)
    # 'C' (open bold retro arc on left)
    d_cola = np.minimum(d_cola, _sdf_ring_arc(lx, ly, -0.68, 0.02, 0.23, 0.52, 0.68, 2.0 * math.pi - 0.68))
    # 'o' (plump oval)
    d_cola = np.minimum(d_cola, _sdf_ring_arc(lx, ly, -0.22, -0.06, 0.17, 0.36, 0.0, 2.0 * math.pi))
    # 'l' (tall tilted stem with retro curved bottom hook)
    d_cola = np.minimum(d_cola, _sdf_segment(lx, ly, +0.10, -0.40, +0.16, +0.56))
    d_cola = np.minimum(d_cola, _sdf_ring_arc(lx, ly, +0.19, -0.32, 0.10, 0.14, math.pi * 0.65, math.pi * 1.45))
    # 'a' (round bowl + vertical right stem + playful spiral tail)
    d_cola = np.minimum(d_cola, _sdf_ring_arc(lx, ly, +0.46, -0.06, 0.16, 0.34, 0.0, 2.0 * math.pi))
    d_cola = np.minimum(d_cola, _sdf_segment(lx, ly, +0.62, -0.40, +0.62, +0.32))
    d_cola = np.minimum(d_cola, _sdf_ring_arc(lx, ly, +0.72, -0.34, 0.10, 0.14, math.pi * 0.65, math.pi * 1.45))
    # Iconic retro swoosh underline beneath "Cola"!
    d_cola = np.minimum(d_cola, _sdf_ring_arc(lx, ly, 0.00, -1.02, 0.88, 0.42, 0.36 * math.pi, 0.66 * math.pi))

    # Crisp smaller upper script "Critter" above "Cola" (v ~ 0.28, top of PNG = smaller v!).
    cx = (u - 0.55) / 0.23
    cy = (0.31 - v) / 0.08
    d_crit = np.full_like(u, 10.0)
    # C-r-i-t-t-e-r stylized SDF glyphs
    d_crit = np.minimum(d_crit, _sdf_ring_arc(cx, cy, -0.78, 0.0, 0.13, 0.40, 0.72, 2.0 * math.pi - 0.72))
    d_crit = np.minimum(d_crit, _sdf_segment(cx, cy, -0.52, -0.36, -0.52, +0.26))
    d_crit = np.minimum(d_crit, _sdf_ring_arc(cx, cy, -0.42, +0.10, 0.10, 0.16, 0.0, math.pi))
    d_crit = np.minimum(d_crit, _sdf_segment(cx, cy, -0.24, -0.36, -0.24, +0.20))
    d_crit = np.minimum(d_crit, np.hypot(cx + 0.24, cy - 0.42))
    d_crit = np.minimum(d_crit, _sdf_segment(cx, cy, -0.04, -0.36, -0.04, +0.46))
    d_crit = np.minimum(d_crit, _sdf_segment(cx, cy, -0.14, +0.22, +0.06, +0.22))
    d_crit = np.minimum(d_crit, _sdf_segment(cx, cy, +0.18, -0.36, +0.18, +0.46))
    d_crit = np.minimum(d_crit, _sdf_segment(cx, cy, +0.08, +0.22, +0.28, +0.22))
    d_crit = np.minimum(d_crit, _sdf_ring_arc(cx, cy, +0.44, -0.04, 0.12, 0.30, 0.35, 2.0 * math.pi - 0.35))
    d_crit = np.minimum(d_crit, _sdf_segment(cx, cy, +0.34, +0.02, +0.55, +0.02))
    d_crit = np.minimum(d_crit, _sdf_segment(cx, cy, +0.68, -0.36, +0.68, +0.26))
    d_crit = np.minimum(d_crit, _sdf_ring_arc(cx, cy, +0.78, +0.10, 0.10, 0.16, 0.0, math.pi))

    # Crisp layered white border + vivid crimson/gold letter fill for "Cola" + warm white/gold "Critter"!
    outline_cola = 1.0 - _smoothstep(0.095, 0.135, d_cola)
    fill_cola = 1.0 - _smoothstep(0.045, 0.075, d_cola)
    inner_gold = 1.0 - _smoothstep(0.015, 0.036, d_cola)
    outline_crit = 1.0 - _smoothstep(0.085, 0.135, d_crit)
    fill_crit = 1.0 - _smoothstep(0.035, 0.070, d_crit)

    # Dark drop shadow behind white lettering for razor readability from any angle.
    d_shadow = np.roll(np.roll(d_cola, 4, axis=0), 3, axis=1)
    sh_mask = (1.0 - _smoothstep(0.09, 0.15, d_shadow)) * (1.0 - outline_cola)
    rgb = rgb * (1.0 - 0.62 * sh_mask[:, :, None]) + ink_dark[None, None, :] * (0.62 * sh_mask[:, :, None])

    rgb = rgb * (1.0 - outline_cola[:, :, None]) + white[None, None, :] * outline_cola[:, :, None]
    rgb = rgb * (1.0 - fill_cola[:, :, None]) + crimson[None, None, :] * fill_cola[:, :, None]
    rgb = rgb * (1.0 - 0.65 * inner_gold[:, :, None]) + gold[None, None, :] * (0.65 * inner_gold[:, :, None])

    rgb = rgb * (1.0 - outline_crit[:, :, None]) + white[None, None, :] * outline_crit[:, :, None]
    rgb = rgb * (1.0 - 0.72 * fill_crit[:, :, None]) + gold[None, None, :] * (0.72 * fill_crit[:, :, None])

    # Crisp metallic fizz stars/bubbles scattered on the purple field (`238:can1`).
    for sx, sy, sr in ((0.32, 0.24, 0.020), (0.86, 0.34, 0.018), (0.84, 0.72, 0.022), (0.30, 0.76, 0.018), (0.92, 0.54, 0.016)):
        star_d = np.minimum(
            _sdf_segment(u, v, sx - sr, sy, sx + sr, sy),
            _sdf_segment(u, v, sx, sy - sr, sx, sy + sr),
        )
        star_m = 1.0 - _smoothstep(0.002, 0.008, star_d)
        rgb = rgb * (1.0 - star_m[:, :, None]) + white[None, None, :] * star_m[:, :, None]

    # Normal map: brushed aluminum horizontal striations + slight ink emboss + subtle metallic crush ripples.
    ink_emboss = np.clip(outline_cola * 0.45 + outline_crit * 0.25, 0.0, 1.0)
    height = np.clip(0.30 * brush + 0.40 * crinkle + 0.30 * ink_emboss, 0.0, 1.0)
    rough_mult = np.clip(0.55 + 0.20 * (1.0 - brush) + 0.18 * (bc_box.astype(np.float32)) - 0.12 * outline_cola, 0.38, 0.92)
    metal_mult = np.clip(0.95 - 0.75 * (bc_box.astype(np.float32)) - 0.28 * outline_cola, 0.15, 1.0)
    return TexturePack(
        family="cola_label",
        size=size,
        base_color_png=_rgb_to_png(np.clip(rgb, 0.0, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=2.6)),
        metallic_roughness_png=_mr_to_rgb_png(rough_mult, metal_mult),
    )


def _build_cap_h2o_pack(size: int) -> TexturePack:
    """Crisp royal-blue metallic 'H2O' soda bottle cap face with cyan water waves (`246:BLUEHILL`)."""
    lin = np.linspace(-1.0, 1.0, size, dtype=np.float32)
    px, py = np.meshgrid(lin, lin)
    r = np.hypot(px, py)
    brush = _tile_noise(size, 28, 28, seed=994)

    cobalt = np.array([0.14, 0.28, 0.84], dtype=np.float32)
    cyan = np.array([0.62, 0.90, 1.00], dtype=np.float32)
    silver = np.array([0.82, 0.86, 0.92], dtype=np.float32)
    rgb = np.clip(cobalt[None, None, :] * (0.86 + 0.18 * brush[:, :, None]), 0.0, 1.0)

    # Outer metallic rim ring + cyan wavy water accents at top and bottom.
    rim = _smoothstep(0.76, 0.86, r)
    rgb = rgb * (1.0 - rim[:, :, None]) + silver[None, None, :] * rim[:, :, None]
    w_top = (1.0 - _smoothstep(0.025, 0.075, np.abs(py + 0.48 - 0.07 * np.sin(px * 9.0)))) * (r < 0.72)
    w_bot = (1.0 - _smoothstep(0.025, 0.075, np.abs(py - 0.48 - 0.07 * np.cos(px * 9.0)))) * (r < 0.72)

    # Crisp SDF 'H', '2', 'O' centered on the cap.
    d = np.full_like(px, 10.0)
    # 'H'
    d = np.minimum(d, _sdf_segment(px, py, -0.58, -0.26, -0.58, +0.26))
    d = np.minimum(d, _sdf_segment(px, py, -0.28, -0.26, -0.28, +0.26))
    d = np.minimum(d, _sdf_segment(px, py, -0.58, 0.00, -0.28, 0.00))
    # '2'
    d = np.minimum(d, _sdf_ring_arc(px, py, 0.00, -0.10, 0.15, 0.15, math.pi * 0.95, math.pi * 2.15))
    d = np.minimum(d, _sdf_segment(px, py, +0.13, -0.02, -0.15, +0.25))
    d = np.minimum(d, _sdf_segment(px, py, -0.15, +0.25, +0.16, +0.25))
    # 'O'
    d = np.minimum(d, _sdf_ring_arc(px, py, +0.46, 0.00, 0.17, 0.25, 0.0, 2.0 * math.pi))

    glyph = np.maximum(1.0 - _smoothstep(0.042, 0.078, d), np.maximum(w_top, w_bot))
    rgb = rgb * (1.0 - glyph[:, :, None]) + cyan[None, None, :] * glyph[:, :, None]
    height = np.clip(0.35 * brush + 0.40 * glyph + 0.25 * rim, 0.0, 1.0)
    return TexturePack(
        family="cap_h2o",
        size=size,
        base_color_png=_rgb_to_png(np.clip(rgb, 0.0, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=2.4)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(0.55 - 0.12 * rim + 0.12 * brush, 0.35, 0.85)),
    )


def _build_cap_soda_pack(size: int) -> TexturePack:
    """Crisp cherry-crimson metallic 'soda' bottle cap face (`247:REDHILL`)."""
    lin = np.linspace(-1.0, 1.0, size, dtype=np.float32)
    px, py = np.meshgrid(lin, lin)
    r = np.hypot(px, py)
    brush = _tile_noise(size, 28, 28, seed=996)

    crimson = np.array([0.86, 0.12, 0.16], dtype=np.float32)
    deep_red = np.array([0.48, 0.04, 0.08], dtype=np.float32)
    white = np.array([0.98, 0.98, 0.98], dtype=np.float32)
    silver = np.array([0.84, 0.86, 0.90], dtype=np.float32)
    rgb = np.clip(crimson[None, None, :] * (0.88 + 0.16 * brush[:, :, None]), 0.0, 1.0)

    # Diagonal sash behind 'soda' + outer silver metallic ring.
    rx = px * 0.88 - py * 0.47
    ry = px * 0.47 + py * 0.88
    sash = (1.0 - _smoothstep(0.28, 0.36, np.abs(ry))) * (r < 0.78)
    rgb = rgb * (1.0 - 0.45 * sash[:, :, None]) + deep_red[None, None, :] * (0.45 * sash[:, :, None])
    rim = _smoothstep(0.76, 0.86, r)
    rgb = rgb * (1.0 - rim[:, :, None]) + silver[None, None, :] * rim[:, :, None]

    # Crisp SDF 's', 'o', 'd', 'a' along rotated sash coordinates (rx, ry).
    d = np.full_like(px, 10.0)
    # 's'
    d = np.minimum(d, _sdf_ring_arc(rx, ry, -0.52, -0.09, 0.11, 0.09, math.pi * 0.45, math.pi * 1.90))
    d = np.minimum(d, _sdf_ring_arc(rx, ry, -0.52, +0.09, 0.11, 0.09, -math.pi * 0.55, math.pi * 0.90))
    # 'o'
    d = np.minimum(d, _sdf_ring_arc(rx, ry, -0.18, 0.00, 0.13, 0.17, 0.0, 2.0 * math.pi))
    # 'd'
    d = np.minimum(d, _sdf_ring_arc(rx, ry, +0.15, +0.02, 0.12, 0.15, 0.0, 2.0 * math.pi))
    d = np.minimum(d, _sdf_segment(rx, ry, +0.27, -0.26, +0.27, +0.18))
    # 'a'
    d = np.minimum(d, _sdf_ring_arc(rx, ry, +0.49, +0.02, 0.11, 0.15, 0.0, 2.0 * math.pi))
    d = np.minimum(d, _sdf_segment(rx, ry, +0.60, -0.14, +0.60, +0.18))

    glyph = (1.0 - _smoothstep(0.036, 0.068, d)) * (r < 0.76)
    rgb = rgb * (1.0 - glyph[:, :, None]) + white[None, None, :] * glyph[:, :, None]
    height = np.clip(0.35 * brush + 0.40 * glyph + 0.25 * rim, 0.0, 1.0)
    return TexturePack(
        family="cap_soda",
        size=size,
        base_color_png=_rgb_to_png(np.clip(rgb, 0.0, 1.0)),
        normal_png=_encode_png_rgb(_height_to_normal_rgb(height, strength=2.4)),
        metallic_roughness_png=_mr_to_rgb_png(np.clip(0.54 - 0.12 * rim + 0.12 * brush, 0.35, 0.85)),
    )


@functools.lru_cache(maxsize=32)
def get_texture_pack(family: str, size: Optional[int] = None) -> TexturePack:
    """Return cached seamless PBR PNG TexturePack for `family`."""
    resolved = int(size) if size is not None else FAMILY_TEXTURE_SIZES.get(family, DEFAULT_TEXTURE_SIZE)
    builders = {
        "chitin": _build_chitin_pack,
        "cloth": _build_cloth_pack,
        "metal": _build_metal_pack,
        "rubber": _build_rubber_pack,
        "fur": _build_fur_pack,
        "bread": _build_bread_pack,
        "bun_white": _build_bun_white_pack,
        "white_crumb": _build_white_crumb_pack,
        "lettuce_leaf": _build_lettuce_leaf_pack,
        "foliage": _build_foliage_pack,
        "leaf_vein": _build_leaf_vein_pack,
        "stone": _build_stone_pack,
        "soil": _build_soil_pack,
        "wood": _build_wood_pack,
        "bark_wood": _build_bark_wood_pack,
        "vinyl_rubber": _build_vinyl_rubber_pack,
        "dino_skin": _build_dino_skin_pack,
        "organic_flesh": _build_organic_flesh_pack,
        "banana_slice": _build_banana_slice_pack,
        "fried_chicken": _build_fried_chicken_pack,
        "cornmeal_batter": _build_cornmeal_batter_pack,
        "cola_label": _build_cola_label_pack,
        "cap_h2o": _build_cap_h2o_pack,
        "cap_soda": _build_cap_soda_pack,
    }
    fn = builders.get(family)
    if fn is not None:
        return fn(resolved)
    raise KeyError(f"Unknown texture family {family!r}; expected one of {', '.join(builders)}")


# ---------------------------------------------------------------------------
# Fast vectorized baked crevice and contact Ambient Occlusion (COLOR_0)
# ---------------------------------------------------------------------------

def _norm_rows(vecs: np.ndarray) -> np.ndarray:
    norms = np.linalg.norm(vecs, axis=1, keepdims=True)
    out = np.empty_like(vecs, dtype=np.float64)
    good = norms[:, 0] > 1e-10
    out[good] = vecs[good] / norms[good]
    if not np.all(good):
        out[~good] = np.array([0.0, 1.0, 0.0], dtype=np.float64)
    return out


def _box_blur_3d(grid: np.ndarray, passes: int) -> np.ndarray:
    g = grid.astype(np.float32, copy=True)
    for _ in range(passes):
        for axis in (0, 1, 2):
            p = np.pad(g, [(1, 1) if a == axis else (0, 0) for a in range(3)], mode="edge")
            sl0 = [slice(0, -2) if a == axis else slice(None) for a in range(3)]
            sl1 = [slice(1, -1) if a == axis else slice(None) for a in range(3)]
            sl2 = [slice(2, None) if a == axis else slice(None) for a in range(3)]
            g = (p[tuple(sl0)] + 2.0 * p[tuple(sl1)] + p[tuple(sl2)]) * 0.25
    return g


def _sample_grid_trilinear(grid: np.ndarray, coords: np.ndarray) -> np.ndarray:
    res = grid.shape[0]
    c = np.clip(coords, 0.0, float(res - 1.001))
    i0 = np.floor(c).astype(np.int32)
    f = (c - i0).astype(np.float32)
    i1 = np.minimum(i0 + 1, res - 1)
    x0, y0, z0 = i0[:, 0], i0[:, 1], i0[:, 2]
    x1, y1, z1 = i1[:, 0], i1[:, 1], i1[:, 2]
    fx, fy, fz = f[:, 0], f[:, 1], f[:, 2]
    c00 = grid[x0, y0, z0] * (1.0 - fx) + grid[x1, y0, z0] * fx
    c10 = grid[x0, y1, z0] * (1.0 - fx) + grid[x1, y1, z0] * fx
    c01 = grid[x0, y0, z1] * (1.0 - fx) + grid[x1, y0, z1] * fx
    c11 = grid[x0, y1, z1] * (1.0 - fx) + grid[x1, y1, z1] * fx
    return (c00 * (1.0 - fy) + c10 * fy) * (1.0 - fz) + (c01 * (1.0 - fy) + c11 * fy) * fz


def _local_concavity(positions: np.ndarray, normals: np.ndarray, indices: np.ndarray) -> np.ndarray:
    """Measure local mesh concavity in [0, 1] for crisp groove/seam shading."""
    n = len(positions)
    if n == 0 or len(indices) == 0:
        return np.zeros(n, dtype=np.float32)
    pos = positions.astype(np.float64)
    idx = indices.reshape(-1, 3)
    acc = np.zeros_like(pos)
    cnt = np.zeros(n, dtype=np.float64)
    for a, b in ((0, 1), (1, 2), (2, 0)):
        ia, ib = idx[:, a], idx[:, b]
        np.add.at(acc, ia, pos[ib])
        np.add.at(acc, ib, pos[ia])
        np.add.at(cnt, ia, 1.0)
        np.add.at(cnt, ib, 1.0)
    avg = acc / np.maximum(1.0, cnt[:, None])
    conc = np.sum((avg - pos) * normals.astype(np.float64), axis=1)
    return np.clip(conc * 38.0, 0.0, 1.0).astype(np.float32)


def bake_scene_ao(scene: Any, *, grid_res: int = 56) -> Any:
    """Bake inter-part and crevice Ambient Occlusion into every MeshPart.colors (COLOR_0).

    Composes parent->child world transforms and excludes translucent BLEND glass
    materials as occluders so eyes behind scuba masks and goggles stay crisp and bright.
    """
    bmin, bmax = scene.bounds()
    pad = 0.14
    origin = bmin.astype(np.float64) - pad
    extent = np.maximum(0.25, (bmax - bmin).astype(np.float64) + 2.0 * pad)
    step = float(extent.max()) / float(grid_res - 1)
    inv_step = 1.0 / step

    occ_pts: List[np.ndarray] = []
    for k, parts in scene.nodes.items():
        if not parts:
            continue
        mat = scene.get_world_matrix(k)
        for p in parts:
            spec = get_material(p.material)
            if spec.resolved_alpha_mode() == "BLEND" or p.material == "eye_catchlight":
                continue
            wp = p.transform(mat) if mat is not None else p
            occ_pts.append(wp.positions.astype(np.float64))
            idx = wp.indices.reshape(-1, 3)
            if len(idx) > 0:
                tri_c = (wp.positions[idx[:, 0]] + wp.positions[idx[:, 1]] + wp.positions[idx[:, 2]]) / 3.0
                occ_pts.append(tri_c.astype(np.float64))

    if not occ_pts:
        return scene

    all_occ = np.vstack(occ_pts)
    grid = np.zeros((grid_res, grid_res, grid_res), dtype=np.float32)
    vox = np.clip(np.round((all_occ - origin[None, :]) * inv_step).astype(np.int32), 0, grid_res - 1)
    np.add.at(grid, (vox[:, 0], vox[:, 1], vox[:, 2]), 0.45)
    grid = np.clip(grid, 0.0, 1.0)
    occ_fine = _box_blur_3d(grid, 1)
    occ_broad = _box_blur_3d(grid, 3)

    for k, parts in scene.nodes.items():
        if not parts:
            continue
        mat = scene.get_world_matrix(k)
        for p in parts:
            spec = get_material(p.material)
            if spec.resolved_alpha_mode() == "BLEND" or p.material == "eye_catchlight":
                p.colors = np.ones((len(p.positions), 3), dtype=np.float32)
                continue
            # Idempotently preserve any sculptor-authored vertex color shading/highlights.
            base_tint = getattr(p, "_base_tint", None)
            if base_tint is None or len(base_tint) != len(p.positions):
                base_tint = (
                    p.colors.astype(np.float32, copy=True)
                    if p.colors is not None and len(p.colors) == len(p.positions)
                    else np.ones((len(p.positions), 3), dtype=np.float32)
                )
                p._base_tint = base_tint
            wp = p.transform(mat) if mat is not None else p
            pos = wp.positions.astype(np.float64)
            nrm = wp.normals.astype(np.float64)

            ref = np.where(np.abs(nrm[:, 1:2]) < 0.85, np.array([[0.0, 1.0, 0.0]]), np.array([[1.0, 0.0, 0.0]]))
            t1 = _norm_rows(np.cross(nrm, ref))
            t2 = np.cross(nrm, t1)

            occ = np.zeros(len(pos), dtype=np.float32)
            for r_off, w, g_src in ((0.032, 0.24, occ_fine), (0.068, 0.24, occ_broad), (0.120, 0.16, occ_broad)):
                occ += w * _sample_grid_trilinear(g_src, (pos + r_off * nrm - origin[None, :]) * inv_step)
            for d_vec in (nrm + 0.72 * t1, nrm - 0.72 * t1, nrm + 0.72 * t2, nrm - 0.72 * t2):
                d_u = _norm_rows(d_vec)
                occ += 0.09 * _sample_grid_trilinear(occ_broad, (pos + 0.064 * d_u - origin[None, :]) * inv_step)

            conc = _local_concavity(wp.positions, wp.normals, wp.indices)
            gnd = np.clip(pos[:, 1] / 0.10, 0.0, 1.0).astype(np.float32)
            gnd_ao = 0.82 + 0.18 * gnd

            ao = (1.0 - np.clip(occ * 0.74 + conc * 0.26, 0.0, 0.58)) * gnd_ao
            if p.material in ("eye_white", "eye_iris", "eye_pupil", "eye_limbal"):
                floor_ao = 0.78
            elif p.name.startswith("mandible_"):
                floor_ao = 0.76
            else:
                floor_ao = 0.44
            ao = np.clip(ao, floor_ao, 1.0).astype(np.float32)
            lo_clip = 0.0 if p.material.startswith("item_") else floor_ao
            p.colors = np.clip(base_tint * ao[:, None], lo_clip, 1.0).astype(np.float32)
    return scene
