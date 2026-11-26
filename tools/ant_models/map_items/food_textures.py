"""Procedural 1998 parody food label & lettered candy PBR textures for Ants map items.

Kept separate from `textures.py` so `textures.py` stays well under size limits while giving
every 1998 Ants food package ('PIZZA! IN A TUBE', 'steak whiz', 'CHOCO-FISHY SNAX',
'Potted Meat', 'ULTIMATE CHOCOLATE BAR', and 'A-N-T-S' Skittles) razor-sharp graphics.
"""
from __future__ import annotations

import math
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
    "cola_label": 512,
    "pizza_tube": 128,
    "steak_whiz": 256,
    "choco_fishy": 128,
    "potted_meat": 512,
    "choc_bar": 128,
    "skittle_a": 128,
    "skittle_n": 128,
    "skittle_t": 128,
    "skittle_s": 128,
    "skittle_c": 128,
    "petal_daisy_white": 256,
    "petal_cosmos_pink": 256,
    "petal_cosmos_rose": 256,
    "petal_lily_lilac": 256,
    "petal_lily_violet": 256,
    "petal_lily_googly": 256,
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
    # Authentic triangular seared steak chop emblem on the right side (`u ~ 0.77, v ~ 0.50`, matching `377:steakwhiz`)!
    du = (u - 0.77) / 0.145
    dv = (v - 0.50) / 0.190
    # Rounded triangular T-bone cut: broad flat-ish upper shoulder (dv < 0), tapering toward lower-right tail!
    tri_w = 1.0 - 0.42 * np.clip((dv + 0.35) / 1.25, 0.0, 1.0)
    steak_shape = ((du / np.maximum(0.45, tri_w)) ** 2) + ((dv - 0.05 * du) ** 2)
    steak_outer = steak_shape < 1.02
    steak_inner = steak_shape < 0.74
    fat_cream = np.array([0.96, 0.90, 0.80], dtype=np.float32)
    sear_brown = np.array([0.48, 0.20, 0.12], dtype=np.float32)
    rgb[steak_outer] = fat_cream
    # Juicy seared reddish-brown steak with subtle marbled flecks & golden highlight
    marble = 0.5 + 0.5 * np.sin(u * 58.0 + v * 42.0) * np.cos(u * 38.0 - v * 52.0)
    grill = np.clip(np.sin((u * 34.0 + v * 28.0) * math.pi), 0.0, 1.0) ** 4
    meat_col = meat[None, None, :] * (0.80 + 0.24 * marble[:, :, None]) + sear_brown[None, None, :] * (0.24 + 0.20 * grill[:, :, None])
    rgb = np.where(steak_inner[:, :, None], np.clip(meat_col, 0.0, 1.0), rgb)
    # Small organic white rib bone spot tucked on the upper-left corner of the steak cut (like `377:steakwhiz`!)
    bone_spot = (((u - 0.725) / 0.022) ** 2 + ((v - 0.43) / 0.042) ** 2 < 1.0) & steak_inner
    rgb[bone_spot] = fat_cream
    # Crisp dark outline framing the steak badge against the crimson can
    steak_rim = (steak_shape >= 0.96) & (steak_shape < 1.12)
    rgb[steak_rim] = dark
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
    """512px vintage royal-blue 'Potted Meat' tin label with crisp white/red shadowed lettering & bottom red ribbon (`357..360`)."""
    try:
        from .. import textures as tx
    except ImportError:
        import textures as tx  # type: ignore

    lin = np.linspace(0.0, 1.0, size, dtype=np.float32)
    u, v = np.meshgrid(lin, lin)
    paper = tx._tile_noise(size, 28, 28, seed=851)
    sheen = tx._tile_noise(size, 6, 36, seed=852)

    # Authentic deep royal cobalt-blue vintage tin wrap (NO yellow bands; bold crimson ribbon along lower edge!)
    royal = np.array([0.16, 0.20, 0.72], dtype=np.float32)
    navy_deep = np.array([0.07, 0.08, 0.34], dtype=np.float32)
    crimson = np.array([0.84, 0.10, 0.12], dtype=np.float32)
    crimson_dark = np.array([0.46, 0.04, 0.06], dtype=np.float32)
    white = np.array([0.97, 0.97, 0.99], dtype=np.float32)
    pale_sky = np.array([0.74, 0.80, 0.98], dtype=np.float32)

    # Subtle glossy wrap gradient across the cobalt-blue paper label
    rgb = royal[None, None, :] * (0.86 + 0.16 * sheen[:, :, None] + 0.06 * paper[:, :, None])
    # Thin deep-navy vignette near top & bottom seams
    top_edge = 1.0 - tx._smoothstep(0.00, 0.06, v)
    rgb = rgb * (1.0 - 0.38 * top_edge[:, :, None]) + navy_deep[None, None, :] * (0.38 * top_edge[:, :, None])

    # Iconic bold crimson-red ribbon band along the lower edge (`v in 0.77..0.93`, matching `strip_pmeat.png`!)
    red_band = tx._smoothstep(0.75, 0.78, v) * (1.0 - tx._smoothstep(0.91, 0.94, v))
    red_trim = (
        tx._smoothstep(0.73, 0.75, v) * (1.0 - tx._smoothstep(0.75, 0.77, v))
        + tx._smoothstep(0.92, 0.94, v) * (1.0 - tx._smoothstep(0.95, 0.97, v))
    )
    rgb = rgb * (1.0 - red_band[:, :, None]) + crimson[None, None, :] * red_band[:, :, None]
    rgb = rgb * (1.0 - 0.80 * red_trim[:, :, None]) + navy_deep[None, None, :] * (0.80 * red_trim[:, :, None])

    # Subtle tiny pale-blue brand micro-header above 'Potted' (`v ~ 0.12..0.21`)
    m_sub = _blit_text(np.zeros((size, size), np.float32), "GOURMET", cx=0.50, cy=0.15, char_w=0.036, char_h=0.08, spacing=1.16, bold=2)
    rgb = rgb * (1.0 - 0.72 * m_sub[:, :, None]) + pale_sky[None, None, :] * (0.72 * m_sub[:, :, None])

    # Crisp compact 'POTTED' over 'MEAT' centered cleanly on the front face (zero wrap distortion at edges!)
    m1 = _blit_text(np.zeros((size, size), np.float32), "POTTED", cx=0.50, cy=0.38, char_w=0.068, char_h=0.22, spacing=1.18, bold=4)
    m2 = _blit_text(np.zeros((size, size), np.float32), "MEAT", cx=0.50, cy=0.63, char_w=0.082, char_h=0.22, spacing=1.22, bold=4)
    txt = np.clip(m1 + m2, 0.0, 1.0)
    # Crisp dark-navy + crimson offset drop shadow behind white lettering so it pops like the 1998 sprite!
    sh_px = max(2, size // 64)
    txt_sh = np.roll(np.roll(txt, sh_px, axis=0), sh_px, axis=1)
    txt_red = np.roll(np.roll(txt, max(1, sh_px // 2), axis=0), max(1, sh_px // 2), axis=1)
    rgb = rgb * (1.0 - 0.85 * txt_sh[:, :, None]) + navy_deep[None, None, :] * (0.85 * txt_sh[:, :, None])
    rgb = rgb * (1.0 - 0.65 * txt_red[:, :, None]) + crimson_dark[None, None, :] * (0.65 * txt_red[:, :, None])
    rgb = rgb * (1.0 - txt[:, :, None]) + white[None, None, :] * txt[:, :, None]

    # Subtle micro-text specks on the bottom crimson ribbon
    m_bot = _blit_text(np.zeros((size, size), np.float32), "NET WT 3 OZ", cx=0.50, cy=0.84, char_w=0.028, char_h=0.072, spacing=1.14, bold=1)
    rgb = rgb * (1.0 - 0.78 * m_bot[:, :, None]) + pale_sky[None, None, :] * (0.78 * m_bot[:, :, None])

    height = np.clip(0.46 + 0.08 * paper + 0.16 * txt + 0.08 * red_band, 0.0, 1.0)
    rough = np.clip(0.36 + 0.08 * paper - 0.06 * red_band, 0.24, 0.50)
    metal = np.full((size, size), 0.18, dtype=np.float32)
    return _pack_from_rgb("potted_meat", size, np.clip(rgb, 0.04, 1.0), height, rough, metal)


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
    """Glossy sugar-shell ANTS candy lentil stamped with authentic 1998 SDF white letter (`267..271:fdskit1..5`)."""
    try:
        from .. import textures as tx
    except ImportError:
        import textures as tx  # type: ignore

    lin = np.linspace(-1.0, 1.0, size, dtype=np.float32)
    px, py = np.meshgrid(lin, lin)
    bg = np.asarray(bg_rgb, dtype=np.float32)
    white = np.array([0.99, 0.99, 0.99], dtype=np.float32)
    rgb = np.tile(bg[None, None, :], (size, size, 1))
    d = np.full_like(px, 10.0)
    ch = letter.upper()
    if ch == "A":
        # Authentic 1998 Ants Candy 'A' (`#271:fdskit5`): tall sharp pointed-apex asymmetrical triangular 'A'
        # with long diagonal left leg sweeping down-left, crisp vertical right leg & clean mid-low crossbar!
        d = np.minimum(d, tx._sdf_segment(px, py, +0.06, -0.52, -0.44, +0.44))
        d = np.minimum(d, tx._sdf_segment(px, py, +0.06, -0.52, +0.16, +0.48))
        d = np.minimum(d, tx._sdf_segment(px, py, -0.34, +0.14, +0.20, +0.14))
    elif ch == "N":
        d = np.minimum(d, tx._sdf_segment(px, py, -0.32, +0.44, -0.32, -0.44))
        d = np.minimum(d, tx._sdf_segment(px, py, -0.32, -0.44, +0.32, +0.44))
        d = np.minimum(d, tx._sdf_segment(px, py, +0.32, +0.44, +0.32, -0.44))
    elif ch == "T":
        d = np.minimum(d, tx._sdf_segment(px, py, -0.40, -0.42, +0.40, -0.42))
        d = np.minimum(d, tx._sdf_segment(px, py, 0.00, -0.42, 0.00, +0.46))
    elif ch == "S":
        d = np.minimum(d, tx._sdf_ring_arc(px, py, 0.00, -0.20, 0.28, 0.22, 0.42 * math.pi, 1.92 * math.pi))
        d = np.minimum(d, tx._sdf_ring_arc(px, py, 0.00, +0.22, 0.28, 0.22, -0.58 * math.pi, 0.92 * math.pi))
    elif ch == "C":
        d = np.minimum(d, tx._sdf_ring_arc(px, py, +0.04, 0.00, 0.34, 0.44, 0.32 * math.pi, 1.68 * math.pi))
    m = 1.0 - tx._smoothstep(0.068, 0.112, d)
    rgb = rgb * (1.0 - m[:, :, None]) + white[None, None, :] * m[:, :, None]
    return _pack_from_rgb(family, size, rgb, 0.50 + 0.08 * m, np.full((size, size), 0.12, np.float32))


def _draw_polyline_sdf(d: np.ndarray, px: np.ndarray, py: np.ndarray, pts: Sequence[Tuple[float, float]], seg_fn) -> np.ndarray:
    """Fold minimum Euclidean distance to an open polyline into `d`."""
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        d = np.minimum(d, seg_fn(px, py, x0, y0, x1, y1))
    return d


def _build_cola_label_pack(size: int):
    """512px authentic 1998 Critter Cola aluminum can label (`238:can1`, `239:can2`).

    Matches `sprite_0372.png` & `sprite_0373.png` 1:1: brushed royal-indigo aluminum, jagged
    electric-blue & hot-magenta 90s splatters, top-only navy/silver/crimson shoulder stripe,
    diagonal stacked 'Critter Cola' with giant spiral 'a', cyan micro-tagline, 4 winged/crawling
    cartoon bugs, sparkling 4-point glints, white UPC barcode, peach seal & nutrition/ingredients strips.
    """
    try:
        from .. import textures as tx
    except ImportError:
        import textures as tx  # type: ignore

    lin = np.linspace(0.0, 1.0, size, endpoint=False, dtype=np.float32)
    u, v = np.meshgrid(lin, lin)

    def _wrap_du(u0: float) -> np.ndarray:
        return ((u - u0 + 0.5) % 1.0) - 0.5

    # 1. Brushed metallic royal-indigo base + jagged 90s electric-blue & hot-magenta splatters.
    brush = tx._tile_noise(size, 6, 48, seed=991)
    splat_a = tx._tile_noise(size, 6, 6, seed=992)
    splat_b = tx._tile_noise(size, 11, 9, seed=993)
    splat_c = tx._tile_noise(size, 22, 18, seed=994)
    warp_field = 0.52 * splat_a + 0.30 * splat_b + 0.18 * splat_c

    indigo = np.array([0.15, 0.13, 0.49], dtype=np.float32)
    royal = np.array([0.23, 0.21, 0.66], dtype=np.float32)
    elec_blue = np.array([0.24, 0.42, 0.98], dtype=np.float32)
    hot_pink = np.array([0.88, 0.11, 0.58], dtype=np.float32)
    navy_rim = np.array([0.10, 0.12, 0.42], dtype=np.float32)
    crimson = np.array([0.84, 0.07, 0.14], dtype=np.float32)
    silver = np.array([0.88, 0.90, 0.95], dtype=np.float32)
    cream = np.array([0.99, 0.98, 0.95], dtype=np.float32)
    orange_out = np.array([0.92, 0.29, 0.08], dtype=np.float32)
    shadow_ink = np.array([0.09, 0.07, 0.28], dtype=np.float32)

    rgb = (indigo[None, None, :] * (0.82 + 0.28 * brush[:, :, None]) + 0.16 * royal[None, None, :] * splat_a[:, :, None])
    # Organic jagged 90s electric-blue & hot-magenta paint clouds over deep royal-indigo metallic base (`sprite_0372.png`).
    front_splat = np.exp(-((_wrap_du(0.54) / 0.30) ** 2)) * tx._smoothstep(0.14, 0.28, v)
    back_splat = np.exp(-((_wrap_du(0.06) / 0.22) ** 2)) * tx._smoothstep(0.16, 0.30, v)
    blue_mask = tx._smoothstep(0.60, 0.67, warp_field + 0.14 * front_splat + 0.06 * back_splat) * (v > 0.12)
    pink_edge = tx._smoothstep(0.52, 0.59, warp_field + 0.16 * front_splat + 0.08 * back_splat) * (1.0 - 0.78 * blue_mask)
    pink_blobs = tx._smoothstep(0.58, 0.65, (1.0 - splat_a) * 0.56 + splat_b * 0.44 + 0.15 * front_splat) * (v > 0.14)
    pink_mask = np.clip(np.maximum(pink_edge * 0.94, pink_blobs), 0.0, 1.0) * (v > 0.12)
    rgb = rgb * (1.0 - pink_mask[:, :, None]) + hot_pink[None, None, :] * pink_mask[:, :, None]
    rgb = rgb * (1.0 - blue_mask[:, :, None]) + elec_blue[None, None, :] * blue_mask[:, :, None]

    # 2. Tight top-only navy / silver-pinstripe / crimson-swoosh shoulder band (NO bottom red stripe!).
    wave = 0.008 * np.sin(u * 2.0 * math.pi * 2.0 - 0.5)
    logo_swell = 0.044 * np.exp(-((_wrap_du(0.46) / 0.17) ** 2))
    v_navy = 0.046 + wave
    v_silver = 0.064 + wave
    v_red = 0.114 + wave + logo_swell
    m_navy = 1.0 - tx._smoothstep(-0.006, +0.006, v - v_navy)
    m_silver = tx._smoothstep(-0.004, +0.004, v - v_navy) * (1.0 - tx._smoothstep(-0.005, +0.005, v - v_silver))
    m_red = tx._smoothstep(-0.004, +0.004, v - v_silver) * (1.0 - tx._smoothstep(-0.007, +0.007, v - v_red))
    rgb = rgb * (1.0 - m_red[:, :, None]) + crimson[None, None, :] * m_red[:, :, None]
    rgb = rgb * (1.0 - m_silver[:, :, None]) + silver[None, None, :] * m_silver[:, :, None]
    rgb = rgb * (1.0 - m_navy[:, :, None]) + navy_rim[None, None, :] * m_navy[:, :, None]

    # 3. Long vertical white ingredients/legal micro-text strip (`u ~ 0.85..0.93`, visible on BOTH cans!).
    du_ing = _wrap_du(0.89)
    ing_box = (np.abs(du_ing) < 0.036) & (v > 0.19) & (v < 0.91)
    ing_frame = (np.abs(du_ing) < 0.040) & (v > 0.182) & (v < 0.918)
    ing_lines = ((np.sin(v * 2.0 * math.pi * 42.0) > 0.15) & (np.abs(du_ing) < 0.028)).astype(np.float32)
    ing_breaks = (tx._tile_noise(size, 18, 56, seed=995) > 0.34).astype(np.float32)
    ing_ink = ing_lines * ing_breaks * ing_box.astype(np.float32)
    rgb = np.where(ing_frame[:, :, None], np.array([0.82, 0.85, 0.94], dtype=np.float32)[None, None, :], rgb)
    rgb = np.where(ing_box[:, :, None], np.array([0.93, 0.94, 0.98], dtype=np.float32)[None, None, :], rgb)
    rgb = rgb * (1.0 - 0.65 * ing_ink[:, :, None]) + navy_rim[None, None, :] * (0.65 * ing_ink[:, :, None])

    # 4. Crisp white vertical UPC Barcode block (`u ~ 0.64..0.75, v ~ 0.83..0.96`, visible on `can1` left!).
    du_bc = _wrap_du(0.695)
    bc_box = (np.abs(du_bc) < 0.056) & (v > 0.825) & (v < 0.965)
    bc_inner = (np.abs(du_bc) < 0.046) & (v > 0.842) & (v < 0.952)
    # Deterministic varied-width UPC bars running along cylinder axis (parallel to `v`).
    bar_idx = np.clip(((du_bc + 0.046) / 0.092 * 28.0).astype(np.int32), 0, 27)
    upc_bits = np.array([1, 0, 1, 0, 0, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0], dtype=np.float32)
    bc_bars = upc_bits[bar_idx] * bc_inner.astype(np.float32)
    rgb = np.where(bc_box[:, :, None], np.array([0.96, 0.96, 0.97], dtype=np.float32)[None, None, :], rgb)
    rgb = rgb * (1.0 - 0.88 * bc_bars[:, :, None]) + np.array([0.08, 0.08, 0.14], dtype=np.float32)[None, None, :] * (0.88 * bc_bars[:, :, None])

    # 5. Nutrition Facts grey/white micro-table + warm coral-peach circular emblem (visible on BOTH cans!).
    peach_seal = np.array([0.94, 0.56, 0.44], dtype=np.float32)
    seal_red = np.array([0.82, 0.20, 0.24], dtype=np.float32)
    for (nu_c, nv_c, su_off, sv_off) in ((0.47, 0.845, +0.048, -0.020), (0.785, 0.815, -0.042, -0.015)):
        dnu, dnv = _wrap_du(nu_c), v - nv_c
        nbox = (np.abs(dnu) < 0.042) & (np.abs(dnv) < 0.056)
        nlines = ((np.sin((dnv + 0.056) * 2.0 * math.pi * 62.0) > 0.10) & (np.abs(dnu) < 0.036)).astype(np.float32)
        n_alpha = nbox.astype(np.float32) * 0.78
        rgb = rgb * (1.0 - n_alpha[:, :, None]) + np.array([0.78, 0.82, 0.92], dtype=np.float32)[None, None, :] * n_alpha[:, :, None]
        rgb = rgb * (1.0 - 0.58 * (nlines * nbox)[:, :, None]) + navy_rim[None, None, :] * (0.58 * (nlines * nbox)[:, :, None])
        # Circular coral-peach & red seal badge next to the table!
        sr = np.hypot(_wrap_du(nu_c + su_off) * 1.82, v - (nv_c + sv_off))
        sm_out = 1.0 - tx._smoothstep(0.032, 0.038, sr)
        sm_in = 1.0 - tx._smoothstep(0.016, 0.022, sr)
        rgb = rgb * (1.0 - sm_out[:, :, None]) + peach_seal[None, None, :] * sm_out[:, :, None]
        rgb = rgb * (1.0 - sm_in[:, :, None]) + seal_red[None, None, :] * sm_in[:, :, None]

    # 6. Four glossy cartoon bugs / flies with translucent silver-blue wings & spindly legs!
    bug_mask_all = np.zeros_like(u)
    bug_dark = np.array([0.11, 0.12, 0.22], dtype=np.float32)
    bug_wing = np.array([0.72, 0.78, 0.92], dtype=np.float32)
    bug_spec = np.array([0.92, 0.96, 1.00], dtype=np.float32)
    for (bu, bv, bang, bsc) in (
        (0.615, 0.655, -0.45, 1.12),   # Hero winged fly right near 'Cola' (visible on BOTH can1 & can2!)
        (0.605, 0.845, 0.85, 0.92),    # Crawling bug near bottom-right hot-pink splatter
        (0.325, 0.785, -1.10, 0.96),   # Upper-left crawling bug above 'C' on can1
        (0.085, 0.500, 0.40, 1.05),    # Back-side flying bug for 360° orbit detail
    ):
        du_b = _wrap_du(bu) * 1.82 / bsc
        dv_b = (v - bv) / bsc
        cb, sb = math.cos(bang), math.sin(bang)
        bx = du_b * cb + dv_b * sb
        by = -du_b * sb + dv_b * cb
        # 6 spindly dark legs
        d_leg = np.full_like(u, 10.0)
        for lx0, ly0, lx1, ly1 in (
            (-0.008, -0.008, -0.044, -0.028), (-0.008, +0.008, -0.044, +0.028),
            (0.002, -0.010, 0.006, -0.046), (0.002, +0.010, 0.006, +0.046),
            (0.016, -0.009, 0.048, -0.036), (0.016, +0.009, 0.048, +0.036),
        ):
            d_leg = np.minimum(d_leg, tx._sdf_segment(bx, by, lx0, ly0, lx1, ly1))
        m_legs = 1.0 - tx._smoothstep(0.0025, 0.0055, d_leg)
        # Translucent pale-silver splayed wings
        w1 = np.hypot((bx - 0.018) / 0.036, (by + 0.028) / 0.016)
        w2 = np.hypot((bx - 0.018) / 0.036, (by - 0.028) / 0.016)
        m_wing = np.clip((1.0 - tx._smoothstep(0.85, 1.08, np.minimum(w1, w2))) * 0.74, 0.0, 1.0)
        # Segmented glossy dark body (head + thorax + plump abdomen)
        d_head = np.hypot((bx + 0.028) / 0.013, by / 0.012)
        d_thor = np.hypot((bx + 0.006) / 0.016, by / 0.015)
        d_abdo = np.hypot((bx - 0.024) / 0.025, by / 0.018)
        m_body = 1.0 - tx._smoothstep(0.86, 1.06, np.minimum(d_head, np.minimum(d_thor, d_abdo)))
        # Shiny pale-blue specular glint dots on thorax & abdomen
        d_glint = np.minimum(np.hypot(bx - 0.018, by + 0.004), np.hypot(bx + 0.006, by + 0.003))
        m_glint = (1.0 - tx._smoothstep(0.004, 0.010, d_glint)) * m_body
        rgb = rgb * (1.0 - m_legs[:, :, None]) + bug_dark[None, None, :] * m_legs[:, :, None]
        rgb = rgb * (1.0 - m_wing[:, :, None]) + bug_wing[None, None, :] * m_wing[:, :, None]
        rgb = rgb * (1.0 - m_body[:, :, None]) + bug_dark[None, None, :] * m_body[:, :, None]
        rgb = rgb * (1.0 - 0.85 * m_glint[:, :, None]) + bug_spec[None, None, :] * (0.85 * m_glint[:, :, None])
        bug_mask_all = np.maximum(bug_mask_all, np.maximum(m_body, m_wing))

    # 7. Bright silvery-white 4-point sparkle bursts (exact hero sparkle visible on BOTH cans!).
    sparkle_all = np.zeros_like(u)
    for (su, sv, sr_sc) in (
        (0.735, 0.675, 1.25),   # Hero bright sparkle burst (mid-left can1 / upper-right can2!)
        (0.245, 0.420, 0.72),
        (0.760, 0.340, 0.68),
        (0.455, 0.725, 0.65),
        (0.145, 0.285, 0.75),
        (0.955, 0.460, 0.70),
    ):
        dx_s = _wrap_du(su) * 1.82 / sr_sc
        dy_s = (v - sv) / sr_sc
        core = np.exp(-((dx_s * dx_s + dy_s * dy_s) / (0.016 ** 2)))
        rays = np.exp(-(np.abs(dx_s) / 0.0045)) * np.exp(-(np.abs(dy_s) / 0.052))
        rays = np.maximum(rays, np.exp(-(np.abs(dy_s) / 0.0045)) * np.exp(-(np.abs(dx_s) / 0.052)))
        diag_a = 0.7071 * (dx_s + dy_s)
        diag_b = 0.7071 * (dx_s - dy_s)
        rays = np.maximum(rays, 0.65 * np.exp(-(np.abs(diag_a) / 0.004)) * np.exp(-(np.abs(diag_b) / 0.032)))
        rays = np.maximum(rays, 0.65 * np.exp(-(np.abs(diag_b) / 0.004)) * np.exp(-(np.abs(diag_a) / 0.032)))
        sp = np.clip(core + 0.85 * rays, 0.0, 1.0)
        sparkle_all = np.maximum(sparkle_all, sp)
    rgb = rgb * (1.0 - 0.92 * sparkle_all[:, :, None]) + np.array([0.95, 0.97, 1.00], dtype=np.float32)[None, None, :] * (0.92 * sparkle_all[:, :, None])

    # 8. Authentic diagonal stacked 'Critter Cola' retro script + giant spiral 'a' + cyan tagline!
    # Tilted ~18° UP-RIGHT in aspect-corrected cylinder space & sized boldly to match BOTH can1 & can2 1:1!
    du_p = _wrap_du(0.500) * 1.82
    dv_p = v - 0.395
    th = math.radians(18.0)
    ct, st = math.cos(th), math.sin(th)
    lx = (du_p * ct - dv_p * st) / 0.345
    ly = (du_p * st + dv_p * ct) / 0.345

    # Upper line: playful hand-lettered 'Critter' (`ly ~ -0.34`, tightly stacked above 'Cola'!).
    dc = np.full_like(u, 10.0)
    # 'C'
    dc = np.minimum(dc, tx._sdf_ring_arc(lx, ly, -0.64, -0.34, 0.105, 0.145, 0.42 * math.pi, 1.62 * math.pi))
    # 'r'
    dc = np.minimum(dc, tx._sdf_segment(lx, ly, -0.47, -0.42, -0.47, -0.20))
    dc = np.minimum(dc, tx._sdf_ring_arc(lx, ly, -0.40, -0.35, 0.070, 0.065, 1.05 * math.pi, 1.85 * math.pi))
    # 'i' + dot
    dc = np.minimum(dc, tx._sdf_segment(lx, ly, -0.26, -0.40, -0.26, -0.20))
    dc = np.minimum(dc, np.hypot(lx + 0.26, ly + 0.50))
    # 't' + 't' with shared sweeping retro crossbar!
    dc = np.minimum(dc, tx._sdf_segment(lx, ly, -0.12, -0.48, -0.12, -0.20))
    dc = np.minimum(dc, tx._sdf_segment(lx, ly, +0.06, -0.48, +0.06, -0.20))
    dc = np.minimum(dc, tx._sdf_segment(lx, ly, -0.20, -0.39, +0.16, -0.41))
    # 'e'
    dc = np.minimum(dc, tx._sdf_ring_arc(lx, ly, +0.26, -0.30, 0.095, 0.105, 0.18 * math.pi, 1.90 * math.pi))
    dc = np.minimum(dc, tx._sdf_segment(lx, ly, +0.17, -0.30, +0.35, -0.32))
    # 'r'
    dc = np.minimum(dc, tx._sdf_segment(lx, ly, +0.45, -0.41, +0.45, -0.20))
    dc = np.minimum(dc, tx._sdf_ring_arc(lx, ly, +0.52, -0.34, 0.072, 0.068, 1.05 * math.pi, 1.88 * math.pi))

    # Lower line: HUGE bold retro script 'Cola' (`ly ~ +0.10`) with iconic coiled spiral 'a'!
    dl = np.full_like(u, 10.0)
    # Big flared retro 'C'
    dl = np.minimum(dl, tx._sdf_ring_arc(lx, ly, -0.68, +0.08, 0.23, 0.27, 0.34 * math.pi, 1.72 * math.pi))
    dl = np.minimum(dl, tx._sdf_ring_arc(lx, ly, -0.56, -0.10, 0.065, 0.065, -0.2 * math.pi, 0.85 * math.pi))
    # Plump slightly tilted 'o'
    dl = np.minimum(dl, tx._sdf_ring_arc(lx, ly, -0.27, +0.13, 0.155, 0.195, 0.0, 2.0 * math.pi))
    # Sweeping tall curved 'l'
    l_pts = [(+0.02, -0.17), (+0.00, -0.02), (-0.01, +0.15), (+0.01, +0.29), (+0.09, +0.33)]
    dl = _draw_polyline_sdf(dl, lx, ly, l_pts, tx._sdf_segment)
    # Iconic HUGE cinnamon-roll coiled spiral 'a' (1.85 turns spiraling smoothly inward!).
    spiral_pts = []
    n_sp = 34
    for i in range(n_sp):
        t = i / float(n_sp - 1)
        ang = -0.38 * math.pi + t * 3.55 * math.pi
        rad = 0.245 * (1.0 - 0.84 * (t ** 0.92))
        spiral_pts.append((0.46 + rad * math.cos(ang) * 1.06, 0.12 + rad * math.sin(ang) * 1.02))
    dl = _draw_polyline_sdf(dl, lx, ly, spiral_pts, tx._sdf_segment)
    # Small sweeping tail kick below the 'a' bowl
    dl = np.minimum(dl, tx._sdf_segment(lx, ly, 0.65, 0.24, 0.74, 0.31))

    # Crisp offset navy drop-shadow -> bold fiery orange-red outline -> bright snowy cream fill!
    # Shift shadow slightly down-right in local logo space
    sh_c = tx._smoothstep(0.115, 0.075, np.minimum(dc, dl))
    # Approximate drop-shadow via dilated outer dark halo on bottom-right side
    d_min = np.minimum(dc * 1.38, dl)
    shadow_m = (1.0 - tx._smoothstep(0.095, 0.155, d_min)) * tx._smoothstep(-0.20, 0.20, ly + 0.35)
    critter_out = 1.0 - tx._smoothstep(0.058, 0.088, dc)
    critter_in = 1.0 - tx._smoothstep(0.024, 0.044, dc)
    cola_out = 1.0 - tx._smoothstep(0.088, 0.122, dl)
    cola_in = 1.0 - tx._smoothstep(0.042, 0.068, dl)
    logo_out = np.maximum(critter_out, cola_out)
    logo_in = np.maximum(critter_in, cola_in)

    # Tiny hand-scrawled bright cyan/magenta tagline right beneath 'Cola' (`ly ~ +0.52`).
    tag_zone = (lx > -0.45) & (lx < 0.72) & (np.abs(ly - 0.51) < 0.055)
    tag_strokes = (np.sin(lx * 44.0 + 0.6 * np.sin(ly * 32.0)) > 0.25) & (np.cos(lx * 21.0 + ly * 18.0) > -0.25)
    tag_m = (tag_zone & tag_strokes).astype(np.float32) * 0.90
    tag_col = np.array([0.24, 0.76, 1.00], dtype=np.float32)

    rgb = rgb * (1.0 - 0.82 * shadow_m[:, :, None]) + shadow_ink[None, None, :] * (0.82 * shadow_m[:, :, None])
    rgb = rgb * (1.0 - logo_out[:, :, None]) + orange_out[None, None, :] * logo_out[:, :, None]
    rgb = rgb * (1.0 - logo_in[:, :, None]) + cream[None, None, :] * logo_in[:, :, None]
    rgb = rgb * (1.0 - tag_m[:, :, None]) + tag_col[None, None, :] * tag_m[:, :, None]

    # Subtle metallic-ink height, glossy anodized aluminum roughness, and high metallic sheen!
    opaque_ink = np.clip(np.maximum(logo_out, np.maximum(bc_box.astype(np.float32), ing_box.astype(np.float32))), 0.0, 1.0)
    height = np.clip(0.42 + 0.12 * brush + 0.22 * logo_in + 0.14 * m_silver + 0.10 * bug_mask_all - 0.06 * sh_c, 0.0, 1.0)
    rough = np.clip(0.24 + 0.08 * brush + 0.14 * opaque_ink - 0.08 * m_silver, 0.14, 0.52)
    metal = np.clip(0.76 + 0.18 * m_silver - 0.56 * opaque_ink - 0.30 * bug_mask_all, 0.14, 0.95)
    return _pack_from_rgb("cola_label", size, rgb, height, rough, metal)


def _build_petal_stripe_pack(
    family: str,
    size: int,
    *,
    base_rgb: Tuple[float, float, float],
    stripe_rgb: Tuple[float, float, float],
    throat_rgb: Tuple[float, float, float],
    highlight_rgb: Tuple[float, float, float],
    n_stripes: float,
    stripe_strength: float,
    wave_amp: float = 0.0,
    wave_freq: float = 3.0,
    stripe_sharp: float = 2.0,
    seed: int = 811,
):
    """High-resolution 256px procedural 1998 floral petal texture with authentic longitudinal veins/tiger stripes (`410..416`)."""
    try:
        from .. import textures as tx
    except ImportError:
        import textures as tx  # type: ignore

    lin = np.linspace(0.0, 1.0, size, dtype=np.float32)
    u, v = np.meshgrid(lin, lin)  # u in [0,1] across petal width, v in [0,1] from throat (0) to tip (1)
    s = 2.0 * u - 1.0  # -1..+1 across width
    fine = tx._tile_noise(size, 28, 14, seed=seed)
    micro = tx._tile_noise(size, 52, 26, seed=seed + 1)

    # Organic longitudinal petal veins/stripes running cleanly from throat (`v=0`) all the way to the tip (`v=1`)!
    waver = wave_amp * (np.sin(v * math.pi * wave_freq + s * 2.2) + 0.45 * np.cos(v * math.pi * (wave_freq * 1.7) - s * 3.1))
    # Gently fan stripes out from the narrow throat while staying bold along the full blade!
    s_fan = s * (1.0 + 0.14 * (1.0 - v)) + waver + 0.045 * (fine - 0.5)
    raw_wave = 0.5 + 0.5 * np.cos(s_fan * math.pi * n_stripes)
    # Crisp fine midrib + bold parallel striation lines!
    stripes = (raw_wave ** stripe_sharp) * (0.76 + 0.24 * np.sin(v * math.pi))
    mid_vein = np.exp(-((s / 0.055) ** 2.0)) * (1.0 - 0.25 * v)
    stripe_mask = np.clip(stripes * stripe_strength + 0.28 * mid_vein * stripe_strength, 0.0, 0.96)

    # Highlight sheen ridges sitting cleanly between the dark longitudinal stripe grooves!
    ridge_hi = ((1.0 - raw_wave) ** 1.8) * (0.35 + 0.65 * np.sin(v * math.pi))
    # Rich darker floral throat near `v=0` fading naturally outward
    throat = np.exp(-((v / 0.24) ** 1.8))

    base_c = np.array(base_rgb, dtype=np.float32)[None, None, :]
    stripe_c = np.array(stripe_rgb, dtype=np.float32)[None, None, :]
    throat_c = np.array(throat_rgb, dtype=np.float32)[None, None, :]
    hi_c = np.array(highlight_rgb, dtype=np.float32)[None, None, :]

    rgb = base_c * (0.94 + 0.08 * fine[:, :, None])
    rgb = rgb * (1.0 - 0.36 * ridge_hi[:, :, None]) + hi_c * (0.36 * ridge_hi[:, :, None])
    rgb = rgb * (1.0 - stripe_mask[:, :, None]) + stripe_c * stripe_mask[:, :, None]
    rgb = rgb * (1.0 - 0.62 * throat[:, :, None]) + throat_c * (0.62 * throat[:, :, None])

    height = np.clip(0.56 + 0.28 * (1.0 - raw_wave) - 0.18 * mid_vein + 0.08 * micro, 0.0, 1.0)
    rough = np.clip(0.46 + 0.14 * stripe_mask + 0.08 * fine - 0.10 * ridge_hi, 0.28, 0.78)
    return _pack_from_rgb(family, size, np.clip(rgb, 0.04, 1.0), height, rough)


def build_food_texture_pack(family: str, size: int):
    """Dispatch procedural 1998 food label, candy & botanical floral petal texture packs."""
    if family == "cola_label":
        return _build_cola_label_pack(size)
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
    # Authentic 1998 floral petal striation & tiger-lily pattern packs (`strip_flowers.png`)!
    if family == "petal_daisy_white":
        return _build_petal_stripe_pack(
            family, size,
            base_rgb=(0.97, 0.97, 0.95), stripe_rgb=(0.82, 0.83, 0.86),
            throat_rgb=(0.94, 0.90, 0.72), highlight_rgb=(1.00, 1.00, 0.99),
            n_stripes=4.0, stripe_strength=0.45, wave_amp=0.02, stripe_sharp=2.6, seed=821,
        )
    if family == "petal_cosmos_pink":
        # Hot-pink cosmos (`412:flower2a`): bold fine longitudinal magenta-rose lines down every petal!
        return _build_petal_stripe_pack(
            family, size,
            base_rgb=(0.99, 0.44, 0.70), stripe_rgb=(0.56, 0.08, 0.34),
            throat_rgb=(0.68, 0.12, 0.38), highlight_rgb=(1.00, 0.74, 0.86),
            n_stripes=7.0, stripe_strength=0.78, wave_amp=0.03, wave_freq=2.0, stripe_sharp=2.2, seed=823,
        )
    if family == "petal_cosmos_rose":
        # Deep rose starflower (`413:flower2b`): vivid dark burgundy-magenta longitudinal striations!
        return _build_petal_stripe_pack(
            family, size,
            base_rgb=(0.90, 0.24, 0.50), stripe_rgb=(0.38, 0.04, 0.20),
            throat_rgb=(0.48, 0.06, 0.26), highlight_rgb=(1.00, 0.50, 0.70),
            n_stripes=7.5, stripe_strength=0.84, wave_amp=0.035, wave_freq=2.2, stripe_sharp=2.1, seed=825,
        )
    if family == "petal_lily_lilac":
        # Spotted lilac lily (`414:flower3a`): vibrant wavy radiating purple-violet zebra veins!
        return _build_petal_stripe_pack(
            family, size,
            base_rgb=(0.88, 0.60, 0.98), stripe_rgb=(0.40, 0.10, 0.68),
            throat_rgb=(0.32, 0.10, 0.64), highlight_rgb=(0.98, 0.84, 1.00),
            n_stripes=6.0, stripe_strength=0.84, wave_amp=0.14, wave_freq=3.6, stripe_sharp=1.5, seed=827,
        )
    if family == "petal_lily_violet":
        # Deep violet lily (`415:flower3b`): rich indigo-violet petals with bold dark-plum & lilac stripes!
        return _build_petal_stripe_pack(
            family, size,
            base_rgb=(0.54, 0.34, 0.92), stripe_rgb=(0.16, 0.06, 0.44),
            throat_rgb=(0.14, 0.05, 0.42), highlight_rgb=(0.82, 0.64, 1.00),
            n_stripes=6.0, stripe_strength=0.86, wave_amp=0.14, wave_freq=3.6, stripe_sharp=1.5, seed=829,
        )
    if family == "petal_lily_googly":
        # Googly-Eyed Flower (`416:flower3c`): iconic HIGH-CONTRAST white-lavender & vibrant magenta-purple tiger stripes!
        return _build_petal_stripe_pack(
            family, size,
            base_rgb=(0.98, 0.88, 1.00), stripe_rgb=(0.52, 0.12, 0.74),
            throat_rgb=(0.44, 0.10, 0.66), highlight_rgb=(1.00, 0.98, 1.00),
            n_stripes=6.0, stripe_strength=0.90, wave_amp=0.18, wave_freq=4.0, stripe_sharp=1.35, seed=831,
        )
    return None
