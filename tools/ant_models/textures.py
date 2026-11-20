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


@functools.lru_cache(maxsize=16)
def get_texture_pack(family: str, size: Optional[int] = None) -> TexturePack:
    """Return cached seamless PBR PNG TexturePack for `family`."""
    resolved = int(size) if size is not None else FAMILY_TEXTURE_SIZES.get(family, DEFAULT_TEXTURE_SIZE)
    if family == "chitin":
        return _build_chitin_pack(resolved)
    if family == "cloth":
        return _build_cloth_pack(resolved)
    if family == "metal":
        return _build_metal_pack(resolved)
    if family == "rubber":
        return _build_rubber_pack(resolved)
    raise KeyError(f"Unknown texture family {family!r}; expected chitin, cloth, metal, or rubber")


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
            p.colors = np.clip(base_tint * ao[:, None], floor_ao, 1.0).astype(np.float32)
    return scene
