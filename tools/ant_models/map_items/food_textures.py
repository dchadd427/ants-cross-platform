"""Procedural 1998 parody food label & lettered candy PBR textures for Ants map items.

Kept separate from `textures.py` so `textures.py` stays well under size limits while giving
every 1998 Ants food package ('PIZZA! IN A TUBE', 'steak whiz', 'CHOCO-FISHY SNAX',
'Potted Meat', 'ULTIMATE CHOCOLATE BAR', and 'A-N-T-S' Skittles) razor-sharp graphics.
"""
from __future__ import annotations

from typing import Dict, Optional, Sequence, Tuple

import numpy as np

# Crisp 5x7 uppercase bitmap font (row 0 = top, bit 4 = left) for readable 1998 parody labels!
_FONT_5X7: Dict[str, Tuple[int, ...]] = {
    "A": (0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11),
    "B": (0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E),
    "C": (0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E),
    "D": (0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E),
    "E": (0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F),
    "F": (0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10),
    "G": (0x0E, 0x11, 0x10, 0x13, 0x11, 0x11, 0x0E),
    "H": (0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11),
    "I": (0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F),
    "K": (0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11),
    "L": (0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F),
    "M": (0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11),
    "N": (0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11),
    "O": (0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E),
    "P": (0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10),
    "R": (0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11),
    "S": (0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E),
    "T": (0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04),
    "U": (0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E),
    "W": (0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11),
    "X": (0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11),
    "Y": (0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04),
    "Z": (0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F),
    "!": (0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04),
    "-": (0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00),
    " ": (0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
}

FOOD_TEXTURE_SIZES: Dict[str, int] = {
    "pizza_tube": 128,
    "steak_whiz": 128,
    "choco_fishy": 128,
    "potted_meat": 128,
    "choc_bar": 128,
    "skittle_a": 64,
    "skittle_n": 64,
    "skittle_t": 64,
    "skittle_s": 64,
    "skittle_c": 64,
}


def _blit_text(
    mask: np.ndarray,
    text: str,
    *,
    cx: float,
    cy: float,
    char_w: float,
    char_h: float,
    spacing: float = 1.22,
    bold: int = 1,
) -> np.ndarray:
    """Rasterize crisp uppercase 5x7 bitmap text centered at normalized `(cx, cy)` onto `mask`."""
    h, w = mask.shape
    s = text.upper()
    if not s:
        return mask
    step = char_w * spacing
    total_w = step * (len(s) - 1) + char_w
    x0 = cx - 0.5 * total_w
    y0 = cy - 0.5 * char_h
    for ci, ch in enumerate(s):
        rows = _FONT_5X7.get(ch)
        if not rows:
            continue
        gx = x0 + ci * step
        for ry, bits in enumerate(rows):
            for rx in range(5):
                if not ((bits >> (4 - rx)) & 1):
                    continue
                px0 = int(round((gx + (rx / 5.0) * char_w) * w))
                px1 = max(px0 + 1, int(round((gx + ((rx + 1) / 5.0) * char_w) * w)) + bold)
                py0 = int(round((y0 + (ry / 7.0) * char_h) * h))
                py1 = max(py0 + 1, int(round((y0 + ((ry + 1) / 7.0) * char_h) * h)) + bold)
                mask[max(0, py0):min(h, py1), max(0, px0):min(w, px1)] = 1.0
    return mask


def _pack_from_rgb(family: str, size: int, rgb: np.ndarray, height: np.ndarray, rough: np.ndarray, metal: Optional[np.ndarray] = None):
    try:
        from .. import textures as tx
    except ImportError:
        import textures as tx  # type: ignore

    return tx.TexturePack(
        family=family,
        size=size,
        base_color_png=tx._rgb_to_png(np.clip(rgb, 0.0, 1.0)),
        normal_png=tx._encode_png_rgb(tx._height_to_normal_rgb(np.clip(height, 0.0, 1.0), strength=1.8)),
        metallic_roughness_png=tx._mr_to_rgb_png(np.clip(rough, 0.08, 0.96), metal),
    )


def _build_pizza_tube_pack(size: int):
    """Bright white & royal-blue toothpaste tube label with yellow 'PIZZA! IN A TUBE' banner (`389:pizzatube`)."""
    lin = np.linspace(0.0, 1.0, size, dtype=np.float32)
    u, v = np.meshgrid(lin, lin)
    rgb = np.full((size, size, 3), [0.95, 0.96, 0.98], dtype=np.float32)
    # Royal-blue bottom & rear crimp stripes
    blue = np.array([0.14, 0.24, 0.78], dtype=np.float32)
    yellow = np.array([0.99, 0.86, 0.14], dtype=np.float32)
    red = np.array([0.92, 0.12, 0.12], dtype=np.float32)
    dark = np.array([0.12, 0.14, 0.22], dtype=np.float32)
    blue_band = (v < 0.16) | (v > 0.86)
    rgb[blue_band] = blue
    # Tilted bright yellow central label badge across front face (u ~ 0.15..0.85, v ~ 0.22..0.78)
    badge = (u > 0.10) & (u < 0.90) & (v > 0.22) & (v < 0.78)
    rgb[badge] = yellow
    m_piz = np.zeros((size, size), dtype=np.float32)
    _blit_text(m_piz, "PIZZA!", cx=0.50, cy=0.39, char_w=0.105, char_h=0.22, bold=1)
    m_sub = np.zeros((size, size), dtype=np.float32)
    _blit_text(m_sub, "IN A TUBE", cx=0.50, cy=0.64, char_w=0.070, char_h=0.15, bold=0)
    rgb = rgb * (1.0 - m_piz[:, :, None]) + red[None, None, :] * m_piz[:, :, None]
    rgb = rgb * (1.0 - m_sub[:, :, None]) + dark[None, None, :] * m_sub[:, :, None]
    height = 0.50 + 0.15 * m_piz + 0.08 * badge.astype(np.float32)
    rough = np.full((size, size), 0.28, dtype=np.float32)
    return _pack_from_rgb("pizza_tube", size, rgb, height, rough)


def _build_steak_whiz_pack(size: int):
    """Bright crimson 'steak whiz' aerosol can label with yellow/white lettering & steak badge (`377:steakwhiz`)."""
    lin = np.linspace(0.0, 1.0, size, dtype=np.float32)
    u, v = np.meshgrid(lin, lin)
    crimson = np.array([0.88, 0.09, 0.11], dtype=np.float32)
    yellow = np.array([0.99, 0.88, 0.12], dtype=np.float32)
    white = np.array([0.98, 0.98, 0.98], dtype=np.float32)
    dark = np.array([0.14, 0.06, 0.06], dtype=np.float32)
    meat = np.array([0.74, 0.28, 0.24], dtype=np.float32)
    rgb = np.tile(crimson[None, None, :], (size, size, 1))
    # Dark framing banner behind text + silver/white edge bands
    rgb[(v < 0.08) | (v > 0.92)] = np.array([0.84, 0.86, 0.90], dtype=np.float32)
    box = (u > 0.10) & (u < 0.62) & (v > 0.22) & (v < 0.78)
    rgb[box] = dark
    m_st = np.zeros((size, size), dtype=np.float32)
    _blit_text(m_st, "STEAK", cx=0.36, cy=0.38, char_w=0.078, char_h=0.22, bold=1)
    m_wh = np.zeros((size, size), dtype=np.float32)
    _blit_text(m_wh, "WHIZ", cx=0.36, cy=0.63, char_w=0.092, char_h=0.22, bold=1)
    rgb = rgb * (1.0 - m_st[:, :, None]) + white[None, None, :] * m_st[:, :, None]
    rgb = rgb * (1.0 - m_wh[:, :, None]) + yellow[None, None, :] * m_wh[:, :, None]
    # Authentic triangular T-bone steak emblem on the right side (`u ~ 0.77, v ~ 0.50`)!
    du = (u - 0.76) / 0.155
    dv = (v - 0.50) / 0.21
    # Tapered steak silhouette: wide rounded top-right lobe, tapered bottom-left tail
    steak_shape = (du ** 2) + ((dv + 0.18 * du) ** 2) * (1.0 + 0.45 * np.clip(-du, 0.0, 1.0)) - 0.14 * np.sin(du * 3.0) * dv
    rgb[steak_shape < 1.05] = white
    rgb[steak_shape < 0.78] = meat
    # Authentic white T-bone inside the meat (vertical stem + upper crossbar)
    t_stem = (np.abs(u - 0.77) < 0.015) & (v > 0.42) & (v < 0.62) & (steak_shape < 0.80)
    t_bar = (np.abs(v - 0.44) < 0.018) & (u > 0.70) & (u < 0.85) & (steak_shape < 0.80)
    rgb[t_stem | t_bar] = white
    height = 0.45 + 0.18 * (m_st + m_wh)
    rough = np.full((size, size), 0.24, dtype=np.float32)
    metal = np.where((v < 0.08) | (v > 0.92), 0.88, 0.32).astype(np.float32)
    return _pack_from_rgb("steak_whiz", size, rgb, height, rough, metal)


def _build_choco_fishy_pack(size: int):
    """Crisp white-and-blue 'CHOCO-FISHY SNAX' fish wrapper band (`394..398:fdfish1..5`)."""
    lin = np.linspace(0.0, 1.0, size, dtype=np.float32)
    u, v = np.meshgrid(lin, lin)
    white = np.array([0.96, 0.97, 0.99], dtype=np.float32)
    navy = np.array([0.16, 0.24, 0.74], dtype=np.float32)
    red = np.array([0.84, 0.14, 0.12], dtype=np.float32)
    sky = np.array([0.16, 0.44, 0.92], dtype=np.float32)
    dark = np.array([0.22, 0.14, 0.10], dtype=np.float32)
    rgb = np.tile(white[None, None, :], (size, size, 1))
    border = (u < 0.09) | (u > 0.91) | (v < 0.09) | (v > 0.91)
    rgb[border] = navy
    m1 = _blit_text(np.zeros((size, size), np.float32), "CHOCO-", cx=0.50, cy=0.26, char_w=0.105, char_h=0.20, bold=1)
    m2 = _blit_text(np.zeros((size, size), np.float32), "FISHY", cx=0.50, cy=0.52, char_w=0.118, char_h=0.22, bold=1)
    m3 = _blit_text(np.zeros((size, size), np.float32), "SNAX", cx=0.50, cy=0.77, char_w=0.125, char_h=0.19, bold=1)
    rgb = rgb * (1.0 - m1[:, :, None]) + red[None, None, :] * m1[:, :, None]
    rgb = rgb * (1.0 - m2[:, :, None]) + sky[None, None, :] * m2[:, :, None]
    rgb = rgb * (1.0 - m3[:, :, None]) + dark[None, None, :] * m3[:, :, None]
    return _pack_from_rgb("choco_fishy", size, rgb, 0.5 + 0.12 * (m1 + m2 + m3), np.full((size, size), 0.54, np.float32))


def _build_potted_meat_pack(size: int):
    """Navy-blue tin can label stamped 'POTTED MEAT' in crimson & cream (`357..360:fdpmeat1..4`)."""
    lin = np.linspace(0.0, 1.0, size, dtype=np.float32)
    u, v = np.meshgrid(lin, lin)
    navy = np.array([0.12, 0.16, 0.56], dtype=np.float32)
    red = np.array([0.92, 0.22, 0.20], dtype=np.float32)
    gold = np.array([0.96, 0.80, 0.24], dtype=np.float32)
    white = np.array([0.96, 0.96, 0.98], dtype=np.float32)
    rgb = np.tile(navy[None, None, :], (size, size, 1))
    rgb[(v < 0.12) | (v > 0.88)] = gold
    m1 = _blit_text(np.zeros((size, size), np.float32), "POTTED", cx=0.50, cy=0.38, char_w=0.108, char_h=0.22, bold=1)
    m2 = _blit_text(np.zeros((size, size), np.float32), "MEAT", cx=0.50, cy=0.68, char_w=0.138, char_h=0.24, bold=1)
    rgb = rgb * (1.0 - m1[:, :, None]) + white[None, None, :] * m1[:, :, None]
    rgb = rgb * (1.0 - m2[:, :, None]) + red[None, None, :] * m2[:, :, None]
    return _pack_from_rgb("potted_meat", size, rgb, 0.5 + 0.12 * (m1 + m2), np.full((size, size), 0.34, np.float32), np.full((size, size), 0.28, np.float32))


def _build_choc_bar_pack(size: int):
    """Glossy cobalt-blue & white 'ULTIMATE CHOCOLATE BAR' foil wrapper (`260..263:fdchoc1..4`)."""
    lin = np.linspace(0.0, 1.0, size, dtype=np.float32)
    u, v = np.meshgrid(lin, lin)
    navy = np.array([0.15, 0.14, 0.68], dtype=np.float32)
    white = np.array([0.96, 0.96, 0.98], dtype=np.float32)
    orange = np.array([0.98, 0.58, 0.08], dtype=np.float32)
    yellow = np.array([0.99, 0.88, 0.16], dtype=np.float32)
    rgb = np.where((v < 0.44)[:, :, None], white[None, None, :], navy[None, None, :]).astype(np.float32)
    m1 = _blit_text(np.zeros((size, size), np.float32), "ULTIMATE", cx=0.50, cy=0.30, char_w=0.095, char_h=0.26, bold=1)
    m2 = _blit_text(np.zeros((size, size), np.float32), "CHOCOLATE", cx=0.50, cy=0.68, char_w=0.084, char_h=0.22, bold=1)
    rgb = rgb * (1.0 - m1[:, :, None]) + orange[None, None, :] * m1[:, :, None]
    rgb = rgb * (1.0 - m2[:, :, None]) + yellow[None, None, :] * m2[:, :, None]
    return _pack_from_rgb("choc_bar", size, rgb, 0.5 + 0.14 * (m1 + m2), np.full((size, size), 0.22, np.float32), np.full((size, size), 0.35, np.float32))


def _build_skittle_pack(family: str, letter: str, bg_rgb: Sequence[float], size: int):
    """Glossy sugar-shell Skittle candy lentil stamped with crisp white letter (`267..271:fdskit1..5`)."""
    bg = np.asarray(bg_rgb, dtype=np.float32)
    white = np.array([0.99, 0.99, 0.99], dtype=np.float32)
    rgb = np.tile(bg[None, None, :], (size, size, 1))
    m = _blit_text(np.zeros((size, size), np.float32), letter, cx=0.50, cy=0.50, char_w=0.38, char_h=0.48, bold=1)
    rgb = rgb * (1.0 - m[:, :, None]) + white[None, None, :] * m[:, :, None]
    return _pack_from_rgb(family, size, rgb, 0.50 + 0.08 * m, np.full((size, size), 0.12, np.float32))


def build_food_texture_pack(family: str, size: int):
    """Dispatch procedural 1998 food label & candy texture packs."""
    if family == "pizza_tube":
        return _build_pizza_tube_pack(size)
    if family == "steak_whiz":
        return _build_steak_whiz_pack(size)
    if family == "choco_fishy":
        return _build_choco_fishy_pack(size)
    if family == "potted_meat":
        return _build_potted_meat_pack(size)
    if family == "choc_bar":
        return _build_choc_bar_pack(size)
    skit_map = {
        "skittle_a": ("A", (0.94, 0.10, 0.10)),
        "skittle_n": ("N", (0.16, 0.24, 0.92)),
        "skittle_t": ("T", (0.16, 0.78, 0.14)),
        "skittle_s": ("S", (0.98, 0.44, 0.06)),
        "skittle_c": ("C", (0.64, 0.12, 0.44)),
    }
    if family in skit_map:
        ch, col = skit_map[family]
        return _build_skittle_pack(family, ch, col, size)
    return None
