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
    """Thick glazed golden-yellow ceramic bowl with authentic jagged broken rim (`1330..1333`)."""
    # Build open double-walled ceramic bowl shell (outer + rim + inner) around local Y axis
    n_u, n_v = 44, 16
    r_top = 0.30
    r_bot = 0.14
    h_bowl = 0.25
    wall = 0.024

    def _rim_notch(ang: float) -> float:
        # Authentic broken ceramic mouth: large jagged break facing front (+Z, ang ~ pi/2) + secondary right chip!
        da1 = math.atan2(math.sin(ang - math.pi * 0.50), math.cos(ang - math.pi * 0.50))
        da2 = math.atan2(math.sin(ang - 0.12), math.cos(ang - 0.12))
        if overturned:
            # `1330:cerbol`: overturned dome with crisp right-shoulder chip!
            return max(0.32, 1.0 - 0.42 * math.exp(-((da2 / 0.36) ** 2)) - 0.18 * math.exp(-((da1 / 0.55) ** 2)))
        # `1331..1333:fdcer1..3`: bowl mouth shattered wide open toward front (+Z) so milk & cereal spill out!
        return max(0.24, 1.0 - 0.68 * math.exp(-((da1 / 0.72) ** 2)) - 0.24 * math.exp(-((da2 / 0.38) ** 2)))

    # Profile parameter t in [0, 1]: bottom center -> outer wall -> rim -> inner wall -> inner floor!
    prof_pts = [
        (0.000, 0.000, 0.0),
        (r_bot, 0.004, 0.0),
        (r_bot + 0.055, 0.055, 0.25),
        (r_top - 0.035, 0.145, 0.65),
        (r_top, h_bowl, 1.0),
        (r_top - wall, h_bowl + 0.004, 1.0),
        (r_top - 0.035 - wall, 0.150, 0.65),
        (r_bot + 0.040, 0.068, 0.25),
        (r_bot - 0.020, 0.026, 0.0),
        (0.000, 0.024, 0.0),
    ]
    verts, uvs, tris = [], [], []
    for ir, (rad0, y0, rim_w) in enumerate(prof_pts):
        vf = ir / float(len(prof_pts) - 1)
        for j in range(n_u + 1):
            uf = j / float(n_u)
            ang = uf * 2.0 * math.pi
            notch = _rim_notch(ang)
            y = y0 * (1.0 - rim_w * (1.0 - notch))
            # Slightly widen radius smoothly as y changes
            r_scale = 1.0 - 0.14 * rim_w * (1.0 - notch)
            px = rad0 * r_scale * math.cos(ang)
            pz = rad0 * r_scale * math.sin(ang)
            verts.append((px, y, pz))
            uvs.append((uf, vf))
    stride = n_u + 1
    for ir in range(len(prof_pts) - 1):
        for j in range(n_u):
            a, b, c, d = ir * stride + j, (ir + 1) * stride + j, (ir + 1) * stride + (j + 1), ir * stride + (j + 1)
            tris.extend(((a, c, b), (a, d, c)))
    pos = np.asarray(verts, dtype=np.float32)
    idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
    bowl = MeshPart(name, "item_bowl_yellow", pos, recompute_smooth_normals(pos, idx), idx, uvs=np.asarray(uvs, dtype=np.float32))
    bowl = gradient_y_tint(bowl, (0.94, 0.66, 0.04), (1.00, 0.94, 0.24), noise_amp=0.02)
    if overturned:
        # Overturned yellow ceramic dome resting cleanly on its rim with right-side chip (`1330:cerbol`)!
        bowl = bowl.rotate_xyz(rx=168.0, ry=-18.0, rz=6.0, degrees=True).translate(0.0, 0.24, 0.0)
    else:
        # Shattered bowl tilted open squarely toward front (+Z) behind the spoon & milk spill (`1331..1333`)!
        bowl = bowl.rotate_xyz(rx=72.0, ry=-4.0, degrees=True).translate(0.00, 0.16, -0.26)
    return bowl


def _ceramic_shards(name: str) -> List[MeshPart]:
    """Three authentic jagged triangular broken golden-yellow ceramic shards scattered around the spilled bowl (`1331..1333`)."""
    shards: List[MeshPart] = []
    specs = (
        (-0.34, +0.06, 0.115, 0.088, 28.0, 14.0),
        (-0.18, +0.26, 0.086, 0.095, -24.0, 18.0),
        (+0.31, +0.10, 0.125, 0.105, -42.0, -16.0),
    )
    for k, (cx, cz, sx, sz, ry, rx) in enumerate(specs):
        sh = rounded_box(sx, 0.026, sz, bevel=0.005, segs=2, name=f"{name}_shard_{k}", material="item_bowl_yellow")
        p = sh.positions.astype(np.float64)
        # Taper one end into a sharp triangular ceramic shard + gentle ceramic bowl curvature!
        taper = np.clip((p[:, 2] / max(1e-4, sz * 0.5) + 1.0) * 0.5, 0.0, 1.0)
        p[:, 0] *= 0.28 + 0.85 * taper
        p[:, 1] += 0.016 * ((p[:, 0] / max(1e-4, sx * 0.5)) ** 2)
        sh.positions = p.astype(np.float32)
        sh.normals = recompute_smooth_normals(sh.positions, sh.indices)
        sh = gradient_y_tint(sh, (0.94, 0.64, 0.04), (1.00, 0.94, 0.28))
        shards.append(sh.rotate_xyz(rx=rx, ry=ry, degrees=True).translate(cx, 0.032, cz))
    return shards


def _cereal_spoon(name: str) -> List[MeshPart]:
    """Gleaming silver cereal spoon extending horizontally from LEFT resting cleanly atop milk & bowl rim (`1331..1333`)."""
    handle = curved_tube(
        [(-0.48, 0.118, +0.01), (-0.34, 0.106, +0.01), (-0.20, 0.096, +0.01)],
        [0.020, 0.016, 0.014],
        samples_per_seg=4,
        radial_segs=12,
        cap_rings=2,
        name=f"{name}_spoon_handle",
        material="item_aluminum",
    )
    # Flatten handle slightly into real stamped silverware flatware!
    ph = handle.positions.astype(np.float64)
    ph[:, 1] = 0.105 + (ph[:, 1] - 0.105) * 0.45
    handle.positions = ph.astype(np.float32)
    handle.normals = recompute_smooth_normals(handle.positions, handle.indices)

    # Concave gleaming silver oval spoon scoop bowl resting cleanly above bowl rim & milk!
    scoop = ellipsoid(0.114, 0.036, 0.070, u_segs=22, v_segs=14, name=f"{name}_spoon_bowl", material="item_aluminum")
    ps = scoop.positions.astype(np.float64)
    top = ps[:, 1] > 0.0
    ps[top, 1] = -0.018 * (1.0 - np.clip((ps[top, 0] / 0.114) ** 2 + (ps[top, 2] / 0.070) ** 2, 0.0, 1.0)) + 0.008
    scoop.positions = ps.astype(np.float32)
    scoop.normals = recompute_smooth_normals(scoop.positions, scoop.indices)
    scoop = scoop.rotate_xyz(rz=-4.0, degrees=True).translate(-0.085, 0.096, +0.012)
    return [handle, scoop]


def _milk_puddle(name: str, scale: float, cz: float) -> MeshPart:
    """Glossy creamy-white spilled milk pool flowing out of the shattered yellow bowl."""
    n_r, n_a = 12, 48
    verts, uvs, tris = [], [], []
    lobes = ((-0.4, 0.7, 0.18), (+0.6, 0.65, 0.22), (+1.65, 0.62, 0.26), (+2.55, 0.58, 0.14), (-1.6, 0.75, 0.16))
    ring_specs = [(ir / float(n_r), True) for ir in range(n_r + 1)] + [(1.0 - ir / float(n_r), False) for ir in range(n_r + 1)]
    for rf, is_top in ring_specs:
        for j in range(n_a + 1):
            ang = 2.0 * math.pi * j / float(n_a)
            r = 0.16
            for la, lw, amp in lobes:
                da = math.atan2(math.sin(ang - la), math.cos(ang - la))
                r += amp * math.exp(-((da / lw) ** 2))
            rad = r * rf * scale
            x = rad * math.cos(ang) + 0.03
            z = rad * math.sin(ang) * 0.88 + cz
            y = (0.006 + 0.030 * ((1.0 - rf ** 4.0) ** 0.45 if rf < 0.999 else 0.0)) if is_top else 0.002
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
    """Vibrant miniature 3D Fruit Loops floating in spilled milk & sitting inside the cereal spoon!"""
    palette = (
        (0.94, 0.24, 0.34),  # coral-red/pink
        (0.32, 0.28, 0.92),  # berry purple-blue
        (0.16, 0.84, 0.18),  # lime green
        (0.98, 0.78, 0.14),  # lemon yellow
        (0.98, 0.44, 0.10),  # tangerine orange
        (0.94, 0.26, 0.72),  # magenta pink
    )
    # Deterministic authentic cluster coordinates covering spoon, bowl mouth, spilled milk & stray bottom-right ring!
    coords = (
        (-0.09, 0.112, +0.008), (-0.04, 0.110, +0.018),  # right inside the gleaming silver spoon!
        (+0.02, 0.044, -0.10), (+0.10, 0.046, -0.06), (+0.18, 0.044, -0.02),
        (-0.04, 0.044, +0.02), (+0.04, 0.048, +0.05), (+0.14, 0.044, +0.08),
        (-0.10, 0.044, +0.09), (-0.01, 0.046, +0.14), (+0.09, 0.046, +0.16),
        (+0.22, 0.044, +0.05), (+0.26, 0.042, -0.08), (-0.15, 0.044, +0.02),
        (-0.05, 0.042, +0.24), (+0.06, 0.042, +0.26), (+0.17, 0.042, +0.22),
        (+0.28, 0.042, +0.16), (+0.32, 0.042, -0.02), (+0.03, 0.044, -0.16),
        (+0.13, 0.044, -0.14), (-0.11, 0.042, +0.17), (-0.02, 0.042, +0.32),
        (+0.12, 0.042, +0.30), (+0.22, 0.038, +0.34), (+0.24, 0.036, +0.40),
    )
    loops: List[MeshPart] = []
    for k, (lx, ly, lz) in enumerate(coords[:count]):
        col = palette[k % len(palette)]
        ring = torus_arc(0.034, 0.015, u_segs=14, v_segs=8, plane="xz", name=f"{name}_fl_{k}", material="item_cream_white")
        ring = tint_part(ring, _const_tint(col)).rotate_xyz(rx=(k * 13 % 18) - 9, rz=(k * 19 % 18) - 9, degrees=True).translate(lx, ly, lz)
        loops.append(ring)
    return loops


def build_cerbol() -> SceneBuilder:
    """Overturned chipped golden-yellow ceramic cereal bowl (`1330:cerbol`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_cerbol")
    sc.add("cerbol", [_shattered_yellow_bowl("cerbol_bowl", overturned=True)])
    return _finalize_item(sc, target_span=0.92)


def _build_cereal_spill(name: str, stage: int) -> SceneBuilder:
    """Shattered golden cereal bowl with shards, silver spoon, milk puddle & Fruit Loops (`1331..1333`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = [_shattered_yellow_bowl(f"{name}_bowl", overturned=False)]
    parts.extend(_ceramic_shards(name))
    parts.extend(_cereal_spoon(name))
    if stage == 1:
        parts.append(_milk_puddle(f"{name}_milk", scale=1.08, cz=+0.08))
        parts.extend(_mini_frootloops(name, count=26))
    elif stage == 2:
        parts.append(_milk_puddle(f"{name}_milk", scale=0.64, cz=-0.04))
        parts.extend(_mini_frootloops(name, count=11))
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
    aspect_x: float = 0.44,
    aspect_z: float = 0.24,
) -> SceneBuilder:
    """Chamfered rectangular Pez pressed-sugar candy brick with recessed frosted top oval dish (`340..343`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)

    # Subdivided rectangular brick so top oval dish recesses cleanly with crisp raised rim!
    nx, nz = 18, 12
    hx, hz = aspect_x * 0.5, aspect_z * 0.5
    thick = 0.135  # Guaranteed normalized size_y >= 0.14!
    verts, uvs, tris = [], [], []
    for is_top in (True, False):
        for iz in range(nz + 1):
            vz = iz / float(nz)
            z = -hz + 2.0 * hz * vz
            for ix in range(nx + 1):
                ux = ix / float(nx)
                x = -hx + 2.0 * hx * ux
                # Smooth chamfer on outer rectangular corners & edges
                ex = abs(x) / hx
                ez = abs(z) / hz
                edge = max(ex, ez)
                cham = 0.022 * max(0.0, (edge - 0.84) / 0.16) ** 1.5
                if is_top:
                    # Iconic recessed Pez oval dish on top face!
                    dish_r = ((x / (hx * 0.68)) ** 2) + ((z / (hz * 0.56)) ** 2)
                    recess = 0.022 * max(0.0, 1.0 - dish_r) ** 0.7 if dish_r < 1.0 else 0.0
                    y = thick - cham - recess
                else:
                    y = 0.004 + cham * 0.7
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
    # Stitch 4 side walls cleanly
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

    # Bright frosted-sugar sheen inside the recessed top dish matching `#340..343`!
    def _pez_color(p: np.ndarray, _n: np.ndarray) -> np.ndarray:
        dish = np.clip(1.0 - (((p[:, 0] / (hx * 0.72)) ** 2) + ((p[:, 2] / (hz * 0.60)) ** 2)), 0.0, 1.0)
        top_w = np.clip((p[:, 1] - thick * 0.65) / (thick * 0.25), 0.0, 1.0)
        w = (dish ** 0.5) * top_w
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
