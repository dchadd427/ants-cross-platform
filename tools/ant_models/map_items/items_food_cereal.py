"""Realistic 3D sculptors for 1998 Ants Food Wave 4E: Cereal Bowls, Pez & Power-Up Nectar (13 items).

Covers:
- `cerbol` (#1330) + `fdcer1`..`fdcer3` (#1331–#1333): Shattered golden-yellow cereal bowl, shards, silver spoon, milk spill & 3D Fruit Loops
- `fdpezp1` (#340), `fdpezb1` (#341), `fdpez2p1` (#342), `fdpez2b1` (#343): Chamfered pastel pink & blue Pez candy bricks
- `FD_COMB` (#422), `FD_SWIM` (#423), `FD_THIEF` (#424), `FD_FIRE` (#425), `FD_BOMB` (#426): 9-frame animated golden nectar drops morphing into each 3D Power-Up gear!
"""
from __future__ import annotations

import math
from typing import Callable, List, Sequence, Tuple

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
            ellipsoid,
            lathe,
            recompute_smooth_normals,
            rounded_box,
            sphere,
            torus_arc,
        )
        from .item_materials import gradient_y_tint, register_item_materials, tint_part
        from .items_powerups import (
            build_pu_bomb,
            build_pu_comb,
            build_pu_mason,
            build_pu_swim,
            build_pu_thief,
        )
        from .items_toys import _finalize_item
    except ImportError:
        from map_items.item_materials import gradient_y_tint, register_item_materials, tint_part  # type: ignore
        from map_items.items_powerups import (  # type: ignore
            build_pu_bomb,
            build_pu_comb,
            build_pu_mason,
            build_pu_swim,
            build_pu_thief,
        )
        from map_items.items_toys import _finalize_item  # type: ignore
        from mesh_core import (  # type: ignore
            AnimationChannel,
            MeshPart,
            SceneBuilder,
            curved_tube,
            ellipsoid,
            lathe,
            recompute_smooth_normals,
            rounded_box,
            sphere,
            torus_arc,
        )


def _const_tint(rgb: Tuple[float, float, float]):
    def _fn(pos: np.ndarray, _n: np.ndarray) -> np.ndarray:
        return np.tile(np.asarray(rgb, dtype=np.float64)[None, :], (len(pos), 1))
    return _fn


# ---------------------------------------------------------------------------
# 1. SHATTERED YELLOW CEREAL BOWL, SPOON, MILK & FRUIT LOOPS (`1330..1333`)
# ---------------------------------------------------------------------------

def _shattered_yellow_bowl(name: str, *, overturned: bool = False) -> MeshPart:
    """Thick glazed golden-yellow ceramic bowl with jagged broken wall & white unglazed porcelain fracture edges (`1330..1333`)."""
    n_u = 64
    r_top = 0.31
    r_bot = 0.165
    h_bowl = 0.22
    wall = 0.024

    def _rim_notch(ang: float) -> Tuple[float, float]:
        # Returns (height_factor, fracture_amount in [0,1]) around bowl circumference
        da_front = math.atan2(math.sin(ang - math.pi * 0.50), math.cos(ang - math.pi * 0.50))
        da_right = math.atan2(math.sin(ang - 0.18), math.cos(ang - 0.18))
        da_left = math.atan2(math.sin(ang - 2.65), math.cos(ang - 2.65))
        if overturned:
            # `1330:cerbol`: overturned golden ceramic dome with crisp jagged right-shoulder notch!
            chip = 0.44 * math.exp(-((da_right / 0.34) ** 2)) + 0.16 * math.exp(-((da_front / 0.52) ** 2))
            jag = 0.05 * math.sin(ang * 13.0 + 0.4) * min(1.0, chip * 3.0)
            h_f = max(0.32, 1.0 - chip + jag)
            frac = min(1.0, max(0.0, (0.94 - h_f) * 6.0))
            return h_f, frac
        # `1331..1333:fdcer1..3`: upright bowl with entire FRONT half shattered jaggedly open down to the base floor!
        front_break = 0.84 * math.exp(-((da_front / 0.88) ** 2))
        side_chips = 0.26 * math.exp(-((da_right / 0.28) ** 2)) + 0.20 * math.exp(-((da_left / 0.26) ** 2))
        # Sharp ceramic fracture zigzag along broken edge
        jag = (0.065 * math.sin(ang * 11.0 + 0.3) + 0.035 * math.cos(ang * 19.0 - 0.7)) * min(1.0, (front_break + side_chips) * 2.2)
        h_f = max(0.14, 1.0 - front_break - side_chips + jag)
        frac = min(1.0, max(0.0, (0.95 - h_f) * 5.0))
        return h_f, frac

    # Double-walled ceramic profile: outer foot -> curved belly -> glazed/broken rim lip -> inner bowl wall -> flat floor
    # Tuple: (radius, height, rim_weight, is_rim_lip)
    prof_pts = [
        (0.000, 0.000, 0.00, False),
        (r_bot, 0.004, 0.00, False),
        (r_bot + 0.058, 0.048, 0.20, False),
        (r_top - 0.042, 0.128, 0.62, False),
        (r_top, h_bowl, 1.00, True),
        (r_top - wall * 0.5, h_bowl + 0.005, 1.00, True),
        (r_top - wall, h_bowl, 1.00, True),
        (r_top - 0.042 - wall, 0.132, 0.62, False),
        (r_bot + 0.042, 0.060, 0.20, False),
        (r_bot - 0.018, 0.024, 0.00, False),
        (0.000, 0.022, 0.00, False),
    ]
    verts, uvs, cols, tris = [], [], [], []
    for ir, (rad0, y0, rim_w, is_lip) in enumerate(prof_pts):
        vf = ir / float(len(prof_pts) - 1)
        for j in range(n_u + 1):
            uf = j / float(n_u)
            ang = uf * 2.0 * math.pi
            notch, frac = _rim_notch(ang)
            y = y0 * (1.0 - rim_w * (1.0 - notch))
            # Lowering the rim naturally follows the bowl's tapered radius!
            r_scale = 1.0 - 0.18 * rim_w * (1.0 - notch)
            px = rad0 * r_scale * math.cos(ang)
            pz = rad0 * r_scale * math.sin(ang)
            verts.append((px, y, pz))
            uvs.append((uf, vf))

            # Vibrant glazed sunshine-yellow ceramic body + CRISP WHITE UNGLAZED PORCELAIN FRACTURE EDGES on broken rim!
            if is_lip and frac > 0.22:
                w = min(1.0, frac * 1.25)
                # Unglazed cream-white porcelain bisque fracture edge contrasting sharply against sunshine-yellow glaze!
                r_c = 0.98 * (1.0 - w) + 0.98 * w
                g_c = 0.76 * (1.0 - w) + 0.96 * w
                b_c = 0.07 * (1.0 - w) + 0.91 * w
            else:
                y_norm = max(0.0, min(1.0, y / h_bowl))
                is_inner = ir >= 7
                shade = 0.90 if is_inner else 1.0
                r_c = (0.96 + 0.04 * y_norm) * shade
                g_c = (0.56 + 0.22 * y_norm) * shade
                b_c = (0.01 + 0.03 * y_norm) * shade
            cols.append((r_c, g_c, b_c))

    stride = n_u + 1
    for ir in range(len(prof_pts) - 1):
        for j in range(n_u):
            a, b, c, d = ir * stride + j, (ir + 1) * stride + j, (ir + 1) * stride + (j + 1), ir * stride + (j + 1)
            tris.extend(((a, c, b), (a, d, c)))
    pos = np.asarray(verts, dtype=np.float32)
    idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
    bowl = MeshPart(
        name,
        "item_bowl_yellow",
        pos,
        recompute_smooth_normals(pos, idx),
        idx,
        uvs=np.asarray(uvs, dtype=np.float32),
        colors=np.asarray(cols, dtype=np.float32),
    )
    if overturned:
        # Overturned yellow ceramic dome resting cleanly on its rim with right-side chip (`1330:cerbol`)!
        bowl = bowl.rotate_xyz(rx=172.0, ry=-14.0, rz=5.0, degrees=True).translate(0.0, 0.22, 0.0)
    else:
        # Authentic `#1331..1333`: bowl sits UPRIGHT with gentle forward tilt (~14 deg) & wide-open shattered front wall!
        bowl = bowl.rotate_xyz(rx=14.0, ry=-2.0, degrees=True).translate(0.01, 0.025, -0.14)
    return bowl


def _ceramic_shards(name: str) -> List[MeshPart]:
    """Three curved thick-walled glazed golden-yellow porcelain shards with crisp white unglazed broken edges (`1331..1333`)."""
    shards: List[MeshPart] = []
    # Exact `#1331..1333` scattered positions & irregular curved pottery wall contours!
    specs = (
        (-0.34, +0.08, 0.145, 0.115, 28.0, 14.0, 1.1),
        (-0.16, +0.28, 0.105, 0.112, -24.0, 18.0, 2.3),
        (+0.31, +0.12, 0.155, 0.125, -36.0, -16.0, 3.7),
    )
    for k, (cx, cz, sx, sz, ry, rx, seed) in enumerate(specs):
        sh = rounded_box(sx, 0.028, sz, bevel=0.004, segs=3, name=f"{name}_shard_{k}", material="item_bowl_yellow")
        p = sh.positions.astype(np.float64)
        # Authentic asymmetric broken pottery wall sherd (curved lip + jagged snapped diagonal break — not plain triangles!)
        ang = np.arctan2(p[:, 2] / max(1e-4, sz * 0.5), p[:, 0] / max(1e-4, sx * 0.5))
        tz = np.clip((p[:, 2] / max(1e-4, sz * 0.5) + 1.0) * 0.5, 0.0, 1.0)
        sherd = 0.42 + 0.62 * (tz ** 0.72) + 0.18 * np.sin(ang * 2.0 + seed) + 0.11 * np.cos(ang * 3.0 - seed * 1.4)
        p[:, 0] *= np.clip(sherd, 0.28, 1.18)
        p[:, 2] *= 1.0 + 0.12 * np.sin(ang * 2.0 - seed)
        # Genuine concave spherical ceramic bowl wall curvature!
        p[:, 1] += 0.028 * ((p[:, 0] / max(1e-4, sx * 0.5)) ** 2) + 0.018 * ((p[:, 2] / max(1e-4, sz * 0.5)) ** 2)
        sh.positions = p.astype(np.float32)
        sh.normals = recompute_smooth_normals(sh.positions, sh.indices)

        # Sunshine-yellow glazed porcelain top/bottom faces + CRISP WHITE UNGLAZED BROKEN PORCELAIN SIDE EDGES!
        def _shard_tint(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
            side_w = np.clip((0.78 - np.abs(nrm[:, 1])) / 0.35, 0.0, 1.0)[:, None]
            y_hi = np.clip((pos[:, 1] + 0.015) / 0.060, 0.0, 1.0)[:, None]
            glaze = np.hstack([0.96 + 0.04 * y_hi, 0.56 + 0.22 * y_hi, 0.01 + 0.03 * y_hi])
            bisque = np.array([[0.99, 0.97, 0.92]], dtype=np.float64)
            return np.clip(glaze * (1.0 - side_w) + bisque * side_w, 0.03, 1.0)

        sh = tint_part(sh, _shard_tint)
        shards.append(sh.rotate_xyz(rx=rx, ry=ry, degrees=True).translate(cx, 0.024, cz))
    return shards


def _cereal_spoon(name: str) -> List[MeshPart]:
    """Complete gleaming mirror-silver cereal spoon with full teardrop handle (`broken3`) attached cleanly to bowl scoop (`1331..1333`)."""
    try:
        from .items_props_hardware import build_silver_spoon_handle
    except ImportError:
        from map_items.items_props_hardware import build_silver_spoon_handle  # type: ignore

    # Full stamped mirror-chrome teardrop spoon handle extending seamlessly to the left (integrating `1334:broken3`!)
    handle = build_silver_spoon_handle(f"{name}_spoon_handle", length=0.46).scale(0.88, 0.88, 0.82).rotate_xyz(rz=-3.0, degrees=True).translate(-0.168, 0.102, +0.012)

    # Polished concave mirror-silver oval spoon bowl joining seamlessly with the slender neck!
    scoop = tint_part(
        ellipsoid(0.118, 0.036, 0.070, u_segs=32, v_segs=16, name=f"{name}_spoon_bowl", material="item_chrome_polished"),
        _const_tint((0.92, 0.94, 0.98)),
    )
    ps = scoop.positions.astype(np.float64)
    top = ps[:, 1] > 0.0
    ps[top, 1] = -0.022 * (1.0 - np.clip((ps[top, 0] / 0.118) ** 2 + (ps[top, 2] / 0.070) ** 2, 0.0, 1.0)) + 0.008
    scoop.positions = ps.astype(np.float32)
    scoop.normals = recompute_smooth_normals(scoop.positions, scoop.indices)
    scoop = scoop.rotate_xyz(rz=-4.0, degrees=True).translate(-0.072, 0.095, +0.012)
    return [handle, scoop]


def _milk_puddle(name: str, scale: float, cz: float) -> MeshPart:
    """Glossy creamy-white spilled milk pool flowing cleanly out of the shattered bowl mouth (`1331..1332`)."""
    n_r, n_a = 16, 64
    verts, uvs, tris = [], [], []
    lobes = (
        (-0.35, 0.65, 0.20),
        (+0.55, 0.60, 0.24),
        (+1.55, 0.62, 0.28),
        (+2.45, 0.54, 0.16),
        (-1.60, 0.72, 0.16),
    )
    ring_specs = [(ir / float(n_r), True) for ir in range(n_r + 1)] + [(1.0 - ir / float(n_r), False) for ir in range(n_r + 1)]
    for rf, is_top in ring_specs:
        for j in range(n_a + 1):
            ang = 2.0 * math.pi * j / float(n_a)
            r = 0.165
            for la, lw, amp in lobes:
                da = math.atan2(math.sin(ang - la), math.cos(ang - la))
                r += amp * math.exp(-((da / lw) ** 2))
            rad = r * rf * scale
            x = rad * math.cos(ang) + 0.035
            z = rad * math.sin(ang) * 0.90 + cz
            y = (0.006 + 0.028 * ((1.0 - rf ** 4.2) ** 0.42 if rf < 0.999 else 0.0)) if is_top else 0.002
            verts.append((x, y, z))
            uvs.append((0.5 + x, 0.5 + z))
    stride = n_a + 1
    for ir in range(len(ring_specs) - 1):
        for j in range(n_a):
            a, b, c, d = ir * stride + j, (ir + 1) * stride + j, (ir + 1) * stride + (j + 1), ir * stride + (j + 1)
            tris.extend(((a, c, b), (a, d, c)))
    pos = np.asarray(verts, dtype=np.float32)
    idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
    return MeshPart(name, "item_milk_spill", pos, recompute_smooth_normals(pos, idx), idx, uvs=np.asarray(uvs, dtype=np.float32))


def _mini_frootloops(name: str, count: int) -> List[MeshPart]:
    """Vibrant miniature 3D Fruit Loops cascading across the spilled milk & spoon (`1331..1332`)!"""
    palette = (
        (0.96, 0.22, 0.34),  # strawberry red-pink
        (0.34, 0.28, 0.94),  # blueberry purple-blue
        (0.18, 0.86, 0.18),  # lime green
        (0.99, 0.82, 0.14),  # lemon yellow
        (0.99, 0.44, 0.10),  # tangerine orange
        (0.94, 0.24, 0.76),  # raspberry magenta
    )
    coords = (
        (-0.085, 0.106, +0.006), (-0.032, 0.104, +0.018),  # nestled inside the gleaming silver spoon!
        (+0.02, 0.044, -0.10), (+0.10, 0.046, -0.06), (+0.18, 0.044, -0.02),
        (-0.04, 0.044, +0.02), (+0.04, 0.048, +0.05), (+0.14, 0.044, +0.08),
        (-0.10, 0.044, +0.09), (-0.01, 0.046, +0.14), (+0.09, 0.046, +0.16),
        (+0.22, 0.044, +0.05), (+0.26, 0.042, -0.08), (-0.15, 0.044, +0.02),
        (-0.05, 0.042, +0.24), (+0.06, 0.042, +0.26), (+0.17, 0.042, +0.22),
        (+0.28, 0.042, +0.16), (+0.32, 0.042, -0.02), (+0.03, 0.044, -0.16),
        (+0.13, 0.044, -0.14), (-0.11, 0.042, +0.17), (-0.02, 0.042, +0.32),
        (+0.12, 0.042, +0.30), (+0.22, 0.038, +0.34), (+0.25, 0.036, +0.41),
        (-0.06, 0.044, -0.14), (+0.16, 0.044, -0.09), (+0.08, 0.044, +0.01),
        (+0.01, 0.044, +0.20), (+0.21, 0.042, +0.12), (+0.14, 0.042, +0.25),
        (-0.08, 0.042, +0.28), (+0.30, 0.042, +0.06),
    )
    loops: List[MeshPart] = []
    for k, (lx, ly, lz) in enumerate(coords[:count]):
        col = palette[k % len(palette)]
        ring = torus_arc(0.032, 0.014, u_segs=18, v_segs=10, plane="xz", name=f"{name}_fl_{k}", material="item_cereal_matte")
        ring = tint_part(ring, _const_tint(col)).rotate_xyz(rx=(k * 13 % 20) - 10, rz=(k * 19 % 20) - 10, degrees=True).translate(lx, ly, lz)
        loops.append(ring)
    return loops


def build_cerbol() -> SceneBuilder:
    """Overturned chipped golden-yellow ceramic cereal bowl (`1330:cerbol`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_cerbol")
    sc.add("cerbol", [_shattered_yellow_bowl("cerbol_bowl", overturned=True)])
    return _finalize_item(sc, target_span=0.92)


def _build_cereal_spill(name: str, stage: int) -> SceneBuilder:
    """Shattered upright golden cereal bowl with white-edged ceramic shards, spoon, milk & Fruit Loops (`1331..1333`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = [_shattered_yellow_bowl(f"{name}_bowl", overturned=False)]
    parts.extend(_ceramic_shards(name))
    parts.extend(_cereal_spoon(name))
    if stage == 1:
        parts.append(_milk_puddle(f"{name}_milk", scale=1.10, cz=+0.09))
        parts.extend(_mini_frootloops(name, count=34))
    elif stage == 2:
        parts.append(_milk_puddle(f"{name}_milk", scale=0.66, cz=-0.03))
        parts.extend(_mini_frootloops(name, count=14))
    sc.add("cereal", parts)
    return _finalize_item(sc, target_span=0.98)


def build_fdcer1() -> SceneBuilder:
    return _build_cereal_spill("item_fdcer1", 1)


def build_fdcer2() -> SceneBuilder:
    return _build_cereal_spill("item_fdcer2", 2)


def build_fdcer3() -> SceneBuilder:
    return _build_cereal_spill("item_fdcer3", 3)


# ---------------------------------------------------------------------------
# 2. PASTEL PRESSED-SUGAR PEZ CANDY BRICKS (`340..343:fdpez*`)
# ---------------------------------------------------------------------------

def _build_pez_brick(
    name: str,
    *,
    mat: str,
    body_rgb: Tuple[float, float, float],
    dish_rgb: Tuple[float, float, float],
    yaw_deg: float,
    aspect_x: float = 0.46,
    aspect_z: float = 0.25,
) -> SceneBuilder:
    """Crisp pressed-sugar rectangular Pez candy brick with tight tablet bevel & recessed rectangular dish (`340..343`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)

    # Higher-resolution rectangular brick so outer tablet edges & rectangular top dish step down crisply (not soap-bar puffy!)
    nx, nz = 24, 16
    hx, hz = aspect_x * 0.5, aspect_z * 0.5
    thick = 0.140  # Guaranteed normalized size_y >= 0.14!
    verts, uvs, tris = [], [], []
    for is_top in (True, False):
        for iz in range(nz + 1):
            vz = iz / float(nz)
            z = -hz + 2.0 * hz * vz
            for ix in range(nx + 1):
                ux = ix / float(nx)
                x = -hx + 2.0 * hx * ux
                ex = abs(x) / hx
                ez = abs(z) / hz
                # Tight crisp pressed-tablet chamfer ONLY on outer 9% margin (rectangular brick, not soap bar!)
                edge = max(ex, ez)
                cham = 0.013 * max(0.0, (edge - 0.91) / 0.09) ** 1.6
                if is_top:
                    # Crisp rounded-rectangle indented top PEZ tablet dish (`340..343`)!
                    rx_d = abs(x) / (hx * 0.72)
                    rz_d = abs(z) / (hz * 0.64)
                    # Superellipse distance for crisp rectangular well with slightly rounded corners
                    rect_d = (rx_d ** 6 + rz_d ** 6) ** (1.0 / 6.0)
                    well = max(0.0, min(1.0, (1.04 - rect_d) / 0.24))
                    well = well * well * (3.0 - 2.0 * well)
                    recess = 0.022 * well
                    y = thick - cham - recess
                else:
                    y = 0.004 + cham * 0.6
                verts.append((x, y, z))
                uvs.append((ux, vz))
    stride = nx + 1
    off = (nz + 1) * stride
    for iz in range(nz):
        for ix in range(nx):
            a, b, c, d = iz * stride + ix, (iz + 1) * stride + ix, (iz + 1) * stride + (ix + 1), iz * stride + (ix + 1)
            tris.extend(((a, b, c), (a, c, d)))
            a2, b2, c2, d2 = off + a, off + b, off + c, off + d
            tris.extend(((a2, c2, b2), (a2, d2, c2)))
    # Stitch 4 flat rectangular tablet walls cleanly
    for i in range(nx):
        t0, t1, b0, b1 = i, i + 1, off + i, off + i + 1
        tris.extend(((t0, t1, b1), (t0, b1, b0)))
        r = nz * stride
        t0, t1, b0, b1 = r + i, r + i + 1, off + r + i, off + r + i + 1
        tris.extend(((t0, b1, t1), (t0, b0, b1)))
    for i in range(nz):
        t0, t1, b0, b1 = i * stride, (i + 1) * stride, off + i * stride, off + (i + 1) * stride
        tris.extend(((t0, b1, t1), (t0, b0, b1)))
        t0, t1, b0, b1 = i * stride + nx, (i + 1) * stride + nx, off + i * stride + nx, off + (i + 1) * stride + nx
        tris.extend(((t0, t1, b1), (t0, b1, b0)))

    pos = np.asarray(verts, dtype=np.float32)
    idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
    brick = MeshPart(f"{name}_brick", mat, pos, recompute_smooth_normals(pos, idx), idx, uvs=np.asarray(uvs, dtype=np.float32))

    # Crisp frosty-white recessed rectangular well contrasting with pastel pressed-sugar rim (`340..343`)!
    def _pez_color(p: np.ndarray, _n: np.ndarray) -> np.ndarray:
        rx_d = np.abs(p[:, 0]) / (hx * 0.74)
        rz_d = np.abs(p[:, 2]) / (hz * 0.66)
        rect_d = (rx_d ** 6 + rz_d ** 6) ** (1.0 / 6.0)
        dish = np.clip((1.04 - rect_d) / 0.18, 0.0, 1.0)
        top_w = np.clip((p[:, 1] - thick * 0.55) / (thick * 0.20), 0.0, 1.0)
        w = dish * top_w
        b_c = np.asarray(body_rgb, dtype=np.float64)[None, :]
        d_c = np.asarray(dish_rgb, dtype=np.float64)[None, :]
        return np.clip(b_c * (1.0 - w[:, None]) + d_c * w[:, None], 0.08, 1.0)

    brick = tint_part(brick, _pez_color).rotate_xyz(ry=yaw_deg, degrees=True)
    sc.add("pez", [brick])
    return _finalize_item(sc, target_span=0.52)


def build_fdpezp1() -> SceneBuilder:
    """Diagonal pastel pink Pez candy brick (`340:fdpezp1`)."""
    return _build_pez_brick("item_fdpezp1", mat="item_pez_pink", body_rgb=(0.98, 0.60, 0.80), dish_rgb=(1.00, 0.94, 0.98), yaw_deg=-52.0)


def build_fdpezb1() -> SceneBuilder:
    """Diagonal pastel blue Pez candy brick (`341:fdpezb1`)."""
    return _build_pez_brick("item_fdpezb1", mat="item_pez_blue", body_rgb=(0.62, 0.64, 0.98), dish_rgb=(0.90, 0.98, 1.00), yaw_deg=-34.0)


def build_fdpez2p1() -> SceneBuilder:
    """Vertical pastel pink Pez candy brick (`342:fdpez2p1`)."""
    return _build_pez_brick("item_fdpez2p1", mat="item_pez_pink", body_rgb=(0.98, 0.58, 0.78), dish_rgb=(1.00, 0.94, 0.98), yaw_deg=-84.0)


def build_fdpez2b1() -> SceneBuilder:
    """Horizontal pastel periwinkle Pez candy brick (`343:fdpez2b1`)."""
    return _build_pez_brick("item_fdpez2b1", mat="item_pez_blue", body_rgb=(0.64, 0.62, 0.98), dish_rgb=(0.86, 0.92, 1.00), yaw_deg=-4.0)


# ---------------------------------------------------------------------------
# 3. ANIMATED 9-FRAME POWER-UP NECTAR DROPS (`422..426:FD_COMB..FD_BOMB`)
# ---------------------------------------------------------------------------

def _collect_powerup_meshes(builder_fn: Callable[[], SceneBuilder], prefix: str, target_span: float = 0.50) -> List[MeshPart]:
    """Flatten & rescale an existing 3D Power-Up SceneBuilder into grounded local MeshParts for reveal inside `FD_*`!"""
    sub = builder_fn()
    bmin, bmax = sub.bounds()
    cx = 0.5 * (bmin[0] + bmax[0])
    cz = 0.5 * (bmin[2] + bmax[2])
    span = max(1e-5, float(max(bmax[0] - bmin[0], bmax[1] - bmin[1], bmax[2] - bmin[2])))
    scale = target_span / span
    out: List[MeshPart] = []
    for nname, plist in sub.nodes.items():
        for k, part in enumerate(plist):
            # Clone & normalize so gear sits squarely at (0, 0, 0) with min_y = 0!
            p = part.positions.astype(np.float64).copy()
            p[:, 0] = (p[:, 0] - cx) * scale
            p[:, 1] = (p[:, 1] - bmin[1]) * scale
            p[:, 2] = (p[:, 2] - cz) * scale
            out.append(
                MeshPart(
                    f"{prefix}_{nname}_{k}",
                    part.material,
                    p.astype(np.float32),
                    part.normals.copy(),
                    part.indices.copy(),
                    uvs=None if part.uvs is None else part.uvs.copy(),
                    colors=None if part.colors is None else part.colors.copy(),
                )
            )
    return out


def _golden_teardrop(name: str) -> MeshPart:
    """Luminous glossy golden honey/nectar teardrop (`422..426:FD_*` frame 0 silhouette)."""
    # Smooth teardrop lathe profile (plump round bottom, graceful tapering pointed apex at top!)
    n = 20
    prof: List[Tuple[float, float]] = []
    for i in range(n + 1):
        t = i / float(n)  # 0 at bottom pole -> 1 at pointed apex
        ang = t * math.pi
        y = 0.03 + 0.52 * t
        # Authentic plump teardrop curve: sin(ang) widened near bottom (t ~ 0.30) and pinched smoothly to apex
        rad = 0.215 * math.sin(ang) * ((1.0 - 0.52 * t) ** 0.72)
        prof.append((max(0.0, rad), y))
    drop = lathe(prof, radial_segs=32, cap_bottom=False, cap_top=False, axis="y", name=name, material="item_nectar_gold")
    return gradient_y_tint(drop, (0.98, 0.74, 0.04), (1.00, 0.96, 0.36))


def _build_power_nectar(name: str, pu_builder: Callable[[], SceneBuilder]) -> SceneBuilder:
    """Animated 9-frame Power-Up Nectar Drop: glowing golden teardrop -> green-sparkle splash -> 3D Power-Up gear!"""
    register_item_materials()
    sc = SceneBuilder(name=name)

    # 1. Golden honey/nectar teardrop (primary iconic frame-0 silhouette!)
    sc.add("nectar_drop", [_golden_teardrop(f"{name}_drop")])

    # 2. Impact golden splash disc + emerald-green magic sparkle burst ring (frames 6-7 of 1998 sprite!)
    splash_puddle = ellipsoid(0.24, 0.044, 0.24, u_segs=22, v_segs=10, name=f"{name}_splash", material="item_nectar_gold").translate(0.0, 0.022, 0.0)
    splash_parts: List[MeshPart] = [gradient_y_tint(splash_puddle, (0.98, 0.70, 0.04), (1.00, 0.94, 0.24))]
    for k in range(8):
        ang = k * (2.0 * math.pi / 8.0)
        sx, sz = 0.16 * math.cos(ang), 0.16 * math.sin(ang)
        sp = sphere(0.030, u_segs=10, v_segs=8, name=f"{name}_sp_{k}", material="item_frog_green").translate(sx, 0.072, sz)
        splash_parts.append(tint_part(sp, _const_tint((0.20, 1.00, 0.28))))
    sc.add("nectar_splash", splash_parts)

    # 3. Genuine 3D class Power-Up gear emerging cleanly from the golden nectar splash (frame 8 of 1998 sprite!)
    gear_parts = _collect_powerup_meshes(pu_builder, prefix=f"{name}_gear", target_span=0.52)
    sc.add("gear_reveal", gear_parts)

    # Authentic 9-frame-inspired 3.2s looping 'Idle' morph animation!
    # At t=0 (and loop end t=3.2): teardrop is full & glowing (`scale=1`), splash & gear are tucked tiny (`0.02`).
    # t=0.0->0.8: drop stretches tall; t=0.8->1.3: drop squishes down; t=1.2->1.7: splash+sparkles burst out;
    # t=1.6->2.7: Power-Up 3D gear pops to full scale & gleams; t=2.7->3.2: reforms into glowing teardrop!
    times = np.array([0.0, 0.45, 0.85, 1.15, 1.45, 1.75, 2.55, 2.85, 3.20], dtype=np.float32)

    drop_trans = np.array(
        [
            [0.0, 0.24, 0.0],  # 0.00: hovering high like frame 0
            [0.0, 0.16, 0.0],  # 0.45: falling & stretching (frames 1-2)
            [0.0, 0.04, 0.0],  # 0.85: nearing ground (frames 4-5)
            [0.0, 0.00, 0.0],  # 1.15: ground impact (frame 6)
            [0.0, 0.00, 0.0],
            [0.0, 0.00, 0.0],
            [0.0, 0.00, 0.0],
            [0.0, 0.14, 0.0],
            [0.0, 0.24, 0.0],
        ],
        dtype=np.float32,
    )
    drop_scale = np.array(
        [
            [0.88, 0.88, 0.88],  # 0.00: plump glowing teardrop (matches frame 0!)
            [0.64, 1.24, 0.64],  # 0.45: stretches tall (frames 1-2)
            [1.06, 0.84, 1.06],  # 0.85: gathers round (frames 4-5)
            [1.24, 0.32, 1.24],  # 1.15: impact squish (frame 6)
            [0.001, 0.001, 0.001],  # 1.45: vanished into splash
            [0.001, 0.001, 0.001],  # 1.75: hidden while gear gleams
            [0.001, 0.001, 0.001],  # 2.55: hidden
            [0.64, 0.64, 0.64],  # 2.85: reforming
            [0.88, 0.88, 0.88],  # 3.20: loop
        ],
        dtype=np.float32,
    )
    splash_scale = np.array(
        [
            [0.001, 0.001, 0.001],  # completely hidden while drop falls!
            [0.001, 0.001, 0.001],
            [0.001, 0.001, 0.001],
            [1.05, 0.85, 1.05],  # impact splash!
            [1.25, 1.15, 1.25],  # emerald sparkles burst (frame 7)!
            [0.25, 0.15, 0.25],
            [0.001, 0.001, 0.001],
            [0.001, 0.001, 0.001],
            [0.001, 0.001, 0.001],
        ],
        dtype=np.float32,
    )
    gear_scale = np.array(
        [
            [0.001, 0.001, 0.001],  # hidden while drop falls
            [0.001, 0.001, 0.001],
            [0.001, 0.001, 0.001],
            [0.001, 0.001, 0.001],
            [0.48, 0.48, 0.48],  # emerging!
            [1.04, 1.04, 1.04],  # full 3D Power-Up gear pop (frame 8)!
            [1.00, 1.00, 1.00],  # hold showcase
            [0.20, 0.20, 0.20],  # tuck back
            [0.001, 0.001, 0.001],  # loop
        ],
        dtype=np.float32,
    )

    sc.add_animation(
        "Idle",
        [
            AnimationChannel("nectar_drop", "translation", times, drop_trans),
            AnimationChannel("nectar_drop", "scale", times, drop_scale),
            AnimationChannel("nectar_splash", "scale", times, splash_scale),
            AnimationChannel("gear_reveal", "scale", times, gear_scale),
        ],
    )
    return _finalize_item(sc, target_span=0.68)


def build_fd_comb() -> SceneBuilder:
    return _build_power_nectar("item_fd_comb", build_pu_comb)


def build_fd_swim() -> SceneBuilder:
    return _build_power_nectar("item_fd_swim", build_pu_swim)


def build_fd_thief() -> SceneBuilder:
    return _build_power_nectar("item_fd_thief", build_pu_thief)


def build_fd_fire() -> SceneBuilder:
    return _build_power_nectar("item_fd_fire", build_pu_mason)


def build_fd_bomb() -> SceneBuilder:
    return _build_power_nectar("item_fd_bomb", build_pu_bomb)
