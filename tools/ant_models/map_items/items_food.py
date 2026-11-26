"""Realistic 3D sculptors for 1998 Ants food items & progressive eaten stages (Wave 1).

Covers:
- `fdburgr1`..`fdburgr3` (253–255): Sesame-seed cheeseburger (full -> half-eaten -> veggie remnants)
- `fdcornd1` (361): Golden cornmeal-crusted corn dog on wood stick with zigzag yellow mustard
- `fdjelo1`  (366): Translucent ruby-red fluted Jell-O dome with suspended banana slices + gentle wobble clip
- `fdchick1` (399): Crispy golden-brown fried chicken drumstick with exposed knuckle bone
- `fdsuckr1` (256): Glossy red-and-white swirled peppermint lollipop on curved paper stick
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
            AnimationChannel,
            MeshPart,
            SceneBuilder,
            curved_tube,
            cylinder,
            ellipsoid,
            ensure_outward_winding,
            lathe,
            quat_from_euler_xyz,
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
            AnimationChannel,
            MeshPart,
            SceneBuilder,
            curved_tube,
            cylinder,
            ellipsoid,
            ensure_outward_winding,
            lathe,
            quat_from_euler_xyz,
            recompute_smooth_normals,
            rounded_box,
            sphere,
            superellipsoid,
            torus_arc,
        )


def _project_bites(pos: np.ndarray, bites: List[Tuple[float, float, float]]) -> Tuple[np.ndarray, np.ndarray]:
    """Carve distinct concave crescent tooth-bites with vertical incisor fluting and return `(positions, is_bitten_mask)`."""
    p = pos.astype(np.float64).copy()
    bitten = np.zeros(len(p), dtype=bool)
    for bx, bz, br in bites:
        dx = p[:, 0] - bx
        dz = p[:, 2] - bz
        dist = np.hypot(dx, dz)
        inside = dist < br
        if not np.any(inside):
            continue
        # Push vertices cleanly along the bite direction (from bite center toward burger center) onto the concave tooth wall.
        dlen = max(1e-5, math.hypot(bx, bz))
        dir_x, dir_z = -bx / dlen, -bz / dlen
        ix, iz = dx[inside], dz[inside]
        s = ix * dir_x + iz * dir_z
        t = -ix * dir_z + iz * dir_x
        # Authentic vertical tooth-bite corrugations matching the fluted white-crumb marks in `fdburgr2`/`fdburgr3`!
        scallop = 0.0072 * np.sin(t * 54.0) + 0.0022 * np.cos(t * 108.0)
        s_wall = np.sqrt(np.maximum(0.0, br * br - t * t)) + scallop
        shift = np.maximum(0.0, s_wall - s)
        p[inside, 0] += dir_x * shift
        p[inside, 2] += dir_z * shift
        bitten[inside] = True
    return p.astype(np.float32), bitten


def _split_mesh_by_mask(part: MeshPart, mask: np.ndarray, alt_material: str, alt_suffix: str) -> List[MeshPart]:
    """Split a MeshPart cleanly into `(outer_part, bitten_interior_part)` so bitten white bread exposes true white crumb."""
    tris = part.indices.reshape(-1, 3)
    if len(tris) == 0 or not np.any(mask):
        return [part]
    hits = mask[tris[:, 0]].astype(np.int32) + mask[tris[:, 1]].astype(np.int32) + mask[tris[:, 2]].astype(np.int32)
    alt_sel = hits >= 2
    keep_sel = ~alt_sel

    out: List[MeshPart] = []
    for sel, mat_name, suffix, drop_tint in (
        (keep_sel, part.material, "", False),
        (alt_sel, alt_material, alt_suffix, True),
    ):
        sub_tris = tris[sel]
        if len(sub_tris) == 0:
            continue
        uniq, inv = np.unique(sub_tris.reshape(-1), return_inverse=True)
        sub_pos = part.positions[uniq].copy()
        sub_idx = inv.astype(np.uint32)
        sub_uv = part.uvs[uniq].copy() if part.uvs is not None else None
        if drop_tint and len(sub_pos) > 0:
            # Isotropic triplanar-like UVs on carved bite walls so spongy white-bread crumb pores NEVER stretch horizontally!
            sub_uv = np.column_stack([
                (sub_pos[:, 0] + sub_pos[:, 2]) * 3.8,
                sub_pos[:, 1] * 4.4,
            ]).astype(np.float32)
        sub_nrm = recompute_smooth_normals(sub_pos, sub_idx)
        sub_part = MeshPart(f"{part.name}{suffix}", mat_name, sub_pos, sub_nrm, sub_idx, uvs=sub_uv)
        if not drop_tint and part.colors is not None:
            sub_col = part.colors[uniq].copy()
            sub_part.colors = sub_col
            sub_part._base_tint = sub_col.copy()
        out.append(sub_part)
    return out


def _build_crisp_lettuce_sheet(
    name: str,
    *,
    width: float = 0.16,
    length: float = 0.22,
    bend_down: float = 0.032,
    seed: float = 0.0,
) -> MeshPart:
    """Build one realistic organic crinkled iceberg/green-leaf lettuce blade with pale crisp ribs & ruffled torn edges."""
    nu, nv = 18, 14
    us = np.linspace(-1.0, 1.0, nu)
    vs = np.linspace(0.0, 1.0, nv)

    pos_top = np.zeros((nv, nu, 3), dtype=np.float64)
    pos_bot = np.zeros((nv, nu, 3), dtype=np.float64)
    uv_grid = np.zeros((nv, nu, 2), dtype=np.float32)
    col_grid = np.zeros((nv, nu, 3), dtype=np.float32)

    for iv, v in enumerate(vs):
        fv = float(v)
        # Broad irregular leafy fan outline: narrow crisp stalk -> jagged asymmetric ruffled outer leaf blade!
        base_env = 0.34 + 0.74 * math.sin(min(1.0, (fv ** 0.76)) * math.pi * 0.92)
        for iu, u in enumerate(us):
            fu = float(u)
            au = abs(fu)
            # Non-uniform organic lobe silhouette around the outer perimeter (zero repeating radial sea-shell spikes!)
            ang_p = math.atan2(fv - 0.30, fu + 1e-6)
            lobe = (
                1.0
                + 0.16 * math.sin(ang_p * 3.0 + seed)
                + 0.12 * math.cos(ang_p * 5.0 - seed * 1.7)
                + 0.08 * math.sin(ang_p * 8.0 + seed * 2.3)
            )
            skew = 0.10 * math.sin(fv * 3.8 + seed)
            x = (fu * base_env * lobe + skew) * width
            z = fv * length * (0.94 + 0.12 * math.cos(fu * 2.6 + seed * 1.3))

            # Crisp central whitish midrib ridge + branching side ribs + organic curled crinkled margins!
            midrib = 0.011 * math.exp(-((fu / 0.24) ** 2)) * (1.0 - 0.65 * fv)
            edge_f = min(1.0, max(au, fv) ** 1.45)
            crinkle = (
                0.017 * math.sin(fu * 5.8 + fv * 6.2 + seed)
                + 0.011 * math.cos(fu * 10.5 - fv * 8.4 + seed * 1.9)
                + 0.007 * math.sin((fu + fv) * 14.0 + seed * 0.7)
            ) * edge_f
            cup = 0.014 * (au ** 1.8) * math.sin(fv * math.pi)
            drape = -bend_down * (fv ** 1.75) + 0.014 * math.sin(fv * 4.5 + seed) * (fv ** 1.2)
            y = midrib + crinkle + cup + drape

            thick = 0.0042 + 0.0045 * math.exp(-((fu / 0.30) ** 2)) * (1.0 - 0.65 * fv)
            pos_top[iv, iu] = (x, y + thick * 0.5, z)
            pos_bot[iv, iu] = (x, y - thick * 0.5, z)
            uv_grid[iv, iu] = (x * 4.2 + 0.5, z * 4.2 + 0.5)

            # Real iceberg/leaf lettuce coloring: pale creamy-ivory stalk & ribs -> fresh vibrant leafy green ruffles!
            rib_w = math.exp(-((fu / 0.36) ** 2)) * (1.0 - 0.58 * fv)
            vein_w = max(0.0, math.sin(fu * 11.0 - fv * 9.0 + seed)) * 0.25 * (1.0 - fv * 0.5)
            pale = min(1.0, max(0.0, rib_w * 0.92 + vein_w + (1.0 - fv) * 0.24))
            # Pale creamy-lime rib `(1.00, 1.00, 0.76)` -> rich fresh ruffled green edge `(0.48, 0.94, 0.32)`
            r_c = 0.46 * (1.0 - pale) + 1.00 * pale
            g_c = 0.92 * (1.0 - pale) + 1.00 * pale
            b_c = 0.30 * (1.0 - pale) + 0.72 * pale
            col_grid[iv, iu] = (r_c, g_c, b_c)

    n_surf = nu * nv
    flat_top = pos_top.reshape(n_surf, 3)
    flat_bot = pos_bot.reshape(n_surf, 3)
    flat_uv = uv_grid.reshape(n_surf, 2)
    flat_col = col_grid.reshape(n_surf, 3)

    def _vid(iv: int, iu: int) -> int:
        return iv * nu + iu

    top_tris: List[Tuple[int, int, int]] = []
    for iv in range(nv - 1):
        for iu in range(nu - 1):
            a, b, c, d = _vid(iv, iu), _vid(iv, iu + 1), _vid(iv + 1, iu + 1), _vid(iv + 1, iu)
            top_tris.append((a, c, b))
            top_tris.append((a, d, c))

    all_pos = np.vstack([flat_top, flat_bot]).astype(np.float32)
    all_uv = np.vstack([flat_uv, flat_uv]).astype(np.float32)
    all_col = np.vstack([flat_col, flat_col * 0.90]).astype(np.float32)
    all_tris: List[Tuple[int, int, int]] = list(top_tris)
    all_tris.extend((n_surf + a, n_surf + c, n_surf + b) for a, b, c in top_tris)

    # Seal outer perimeter hem watertight so leaves look crisp from every angle
    perim: List[int] = (
        [_vid(0, iu) for iu in range(nu - 1)]
        + [_vid(iv, nu - 1) for iv in range(nv - 1)]
        + [_vid(nv - 1, iu) for iu in range(nu - 1, 0, -1)]
        + [_vid(iv, 0) for iv in range(nv - 1, 0, -1)]
    )
    for k in range(len(perim)):
        e0 = perim[k]
        e1 = perim[(k + 1) % len(perim)]
        all_tris.append((e0, e1, n_surf + e1))
        all_tris.append((e0, n_surf + e1, n_surf + e0))

    idx_arr = np.asarray(all_tris, dtype=np.uint32).reshape(-1)
    nrm = recompute_smooth_normals(all_pos, idx_arr)
    leaf = MeshPart(name, "item_lettuce", all_pos, nrm, idx_arr, uvs=all_uv).ensure_outward()
    leaf.colors = all_col
    leaf._base_tint = all_col.copy()
    return leaf


def _build_tomato_wheel(name: str, radius: float = 0.145, thick: float = 0.026, *, open_pockets: bool = False) -> List[MeshPart]:
    """Build a realistic juicy sliced red tomato cross-section with organic thick fleshy wall, curved septa & seeds."""
    parts: List[MeshPart] = []

    def _organic_rim(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ang = np.arctan2(p[:, 2], p[:, 0])
        # Irregular natural heirloom-tomato oval waviness (never a rigid bicycle wheel!)
        wobble = 1.0 + 0.08 * np.sin(ang * 3.0 + 0.5) + 0.045 * np.cos(ang * 5.0 - 0.8)
        p[:, 0] *= wobble * 1.05
        p[:, 2] *= wobble * 0.94
        p[:, 1] += 0.004 * np.sin(ang * 2.0 + 0.3)
        return p

    rim = tint_part(
        torus_arc(radius * 0.84, thick * 0.68, u_segs=32, v_segs=12, plane="xz", name=f"{name}_rim", material="item_tomato")
        .deform(_organic_rim),
        lambda pos, nrm: np.clip(
            np.column_stack([
                0.92 + 0.08 * nrm[:, 1],
                0.78 + 0.18 * np.clip(1.0 - np.hypot(pos[:, 0], pos[:, 2]) / max(1e-4, radius), 0.0, 1.0),
                0.68 + 0.18 * np.clip(1.0 - np.hypot(pos[:, 0], pos[:, 2]) / max(1e-4, radius), 0.0, 1.0),
            ]),
            0.45,
            1.0,
        ),
    )
    parts.append(rim)

    # Plump fleshy irregular central tomato core/placenta
    hub = ellipsoid(radius * 0.31, thick * 0.52, radius * 0.27, u_segs=16, v_segs=10, name=f"{name}_hub", material="item_tomato_pulp").translate(radius * 0.04, 0.0, -radius * 0.03)
    parts.append(hub)

    # 3 broad curved organic fleshy septa flaring seamlessly from core to rind (not thin steering-wheel rods!)
    for k, (ang, curve_s) in enumerate(((0.55, +0.018), (2.68, -0.016), (4.72, +0.014))):
        spoke = (
            ellipsoid(radius * 0.48, thick * 0.46, radius * 0.17, u_segs=16, v_segs=10, name=f"{name}_spoke_{k}", material="item_tomato")
            .deform(lambda p, cs=curve_s: np.column_stack([p[:, 0], p[:, 1], p[:, 2] + cs * np.sin((p[:, 0] / max(1e-4, radius * 0.48)) * math.pi)]))
            .translate(radius * 0.40, 0.0, 0.0)
            .rotate_xyz(ry=ang)
        )
        parts.append(spoke)

    if open_pockets:
        # Glossy inner fleshy pericarp wall + tiny pale-ivory tomato seeds tucked along the chamber edges (`255:fdburgr3`)
        inner_lip = torus_arc(radius * 0.72, thick * 0.38, u_segs=28, v_segs=10, plane="xz", name=f"{name}_lip", material="item_tomato_pulp").deform(_organic_rim)
        parts.append(inner_lip)
        for k, ang in enumerate((1.35, 1.75, 3.45, 3.90, 5.45, 5.85)):
            sx = radius * 0.56 * math.cos(ang)
            sz = radius * 0.52 * math.sin(ang)
            parts.append(ellipsoid(0.011, 0.0055, 0.0065, u_segs=8, v_segs=6, name=f"{name}_seed_{k}", material="item_sesame").rotate_xyz(ry=ang).translate(sx, thick * 0.14, sz))
    else:
        pulp = cylinder(radius * 0.84, radius * 0.84, thick * 0.66, radial_segs=24, bevel=0.003, axis="y", name=f"{name}_pulp", material="item_tomato_pulp").deform(_organic_rim)
        tint_part(
            pulp,
            lambda pos, _n: np.column_stack([
                0.86 + 0.14 * np.cos(np.arctan2(pos[:, 2], pos[:, 0]) * 3.0),
                0.68 + 0.22 * np.clip(np.hypot(pos[:, 0], pos[:, 2]) / max(1e-4, radius), 0.0, 1.0),
                0.56 + 0.18 * np.clip(np.hypot(pos[:, 0], pos[:, 2]) / max(1e-4, radius), 0.0, 1.0),
            ]),
        )
        parts.append(pulp)
    return parts


def _build_pickle_chip(name: str, radius: float = 0.095, *, curl: float = 0.0, half_chip: bool = False) -> MeshPart:
    """Crinkle-cut dark-green dill pickle slice with deep cucumber rind rim, ribbed ridges & pale seed core."""
    def _crinkle(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        if half_chip:
            p[:, 2] = p[:, 2] * 0.60 - 0.22 * radius * (p[:, 0] / max(1e-4, radius)) ** 2
        # Crisp crinkle-cut pickle ridges clearly visible in Studio lighting!
        p[:, 1] += 0.0068 * np.sin(p[:, 0] * 96.0)
        if curl > 0.0:
            p[:, 1] += curl * ((p[:, 0] / max(1e-4, radius)) ** 2)
        return p

    chip = cylinder(radius, radius * 0.96, 0.020, radial_segs=30, bevel=0.003, axis="y", name=name, material="item_pickle").deform(_crinkle)
    return tint_part(
        chip,
        lambda pos, nrm: np.clip(
            np.column_stack([
                (0.46 + 0.44 * np.clip(1.0 - (np.hypot(pos[:, 0], pos[:, 2]) / max(1e-4, radius)) ** 1.8, 0.0, 1.0)) * (0.80 + 0.20 * np.sin(pos[:, 0] * 96.0)) + 0.10 * nrm[:, 1],
                (0.72 + 0.26 * np.clip(1.0 - (np.hypot(pos[:, 0], pos[:, 2]) / max(1e-4, radius)) ** 1.8, 0.0, 1.0)) * (0.84 + 0.16 * np.sin(pos[:, 0] * 96.0)) + 0.08 * nrm[:, 1],
                (0.32 + 0.42 * np.clip(1.0 - (np.hypot(pos[:, 0], pos[:, 2]) / max(1e-4, radius)) ** 1.8, 0.0, 1.0)) * (0.80 + 0.20 * np.sin(pos[:, 0] * 96.0)),
            ]),
            0.18,
            1.0,
        ),
    )


def _sculpt_bun_dome(pos: np.ndarray) -> np.ndarray:
    """Organic bakery sesame top bun silhouette with flat seated base & signature diagonal crown dimple crease."""
    p = pos.copy()
    p[:, 1] = np.where(p[:, 1] < 0.0, p[:, 1] * 0.18, p[:, 1])
    # Subtle irregular hand-baked bakery puffiness (breaks sterile sphere perfection!)
    ang = np.arctan2(p[:, 2], p[:, 0])
    r = np.hypot(p[:, 0], p[:, 2])
    puff = 1.0 + 0.022 * np.sin(ang * 3.0 + 0.4) + 0.014 * np.cos(ang * 5.0 - 0.7)
    p[:, 0] *= puff
    p[:, 2] *= puff
    # Iconic diagonal bakery dimple fold across upper-left-center crown matching `fdburgr1` & `fdburgr2`!
    u_diag = (p[:, 0] + p[:, 2]) * 0.707
    v_diag = (p[:, 0] - p[:, 2]) * 0.707
    crease = -0.019 * np.exp(-(((v_diag + 0.03) / 0.048) ** 2 + ((u_diag + 0.02) / 0.17) ** 2)) * np.clip(p[:, 1] / 0.11, 0.0, 1.0)
    # Soft pillowy crown dimples around the perimeter
    micro_puff = 0.004 * np.sin(p[:, 0] * 24.0 + 0.5) * np.cos(p[:, 2] * 22.0 - 0.3) * np.clip(r / 0.18, 0.0, 1.0)
    p[:, 1] += crease + micro_puff
    return p


def _tint_baked_bun(part: MeshPart) -> MeshPart:
    """Rich oven-toasted bun shading: pale warm cream lower skirt -> golden-russet toasted top with dimple AO."""
    y0 = float(part.positions[:, 1].min())
    y1 = float(part.positions[:, 1].max())
    span = max(1e-5, y1 - y0)

    def _bun_col(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
        t = np.clip((pos[:, 1] - y0) / span, 0.0, 1.0)
        # Warm pale-tan lower bakery rim -> toasted warm golden-russet crown matching `burgers_sheet.png`!
        u_diag = (pos[:, 0] + pos[:, 2]) * 0.707
        v_diag = (pos[:, 0] - pos[:, 2]) * 0.707
        fold_ao = 1.0 - 0.22 * np.exp(-(((v_diag + 0.03) / 0.055) ** 2 + ((u_diag + 0.02) / 0.18) ** 2)) * (t > 0.45)
        lo = np.array([[1.00, 0.94, 0.84]], dtype=np.float64)
        hi = np.array([[0.96, 0.82, 0.66]], dtype=np.float64)
        toast_var = 1.0 + 0.06 * np.sin(pos[:, 0] * 14.0 + pos[:, 2] * 12.0) * np.cos(pos[:, 2] * 16.0 - pos[:, 0] * 11.0)
        col = (lo * (1.0 - t[:, None]) + hi * t[:, None]) * fold_ao[:, None] * toast_var[:, None] * (0.92 + 0.08 * np.clip(nrm[:, 1:2], 0.0, 1.0))
        return np.clip(col, 0.45, 1.0)

    return tint_part(part, _bun_col)


def _build_burger(stage: int) -> SceneBuilder:
    """Build authentic 1998 toasted sesame hamburger (`fdburgr1` full, `fdburgr2` 3-bite, `fdburgr3` scraps)."""
    register_item_materials()
    sc = SceneBuilder(name=f"item_fdburgr{stage}")

    if stage == 3:
        # Final stage (`255:fdburgr3`): compact jagged upper-right bun wedge with fluted white crumb,
        # spilled organic hollow red tomato slice on left, ONE large oval white onion ring front-center,
        # crinkled bright-lime lettuce + ribbed pickle inside ring, charred beef remnant, & loose curled pickle far right!
        rem_bites = [(-0.22, -0.22, 0.175), (+0.14, +0.20, 0.225), (-0.12, +0.24, 0.185)]
        raw_bun = _tint_baked_bun(
            superellipsoid(0.255, 0.112, 0.220, e1=0.82, e2=0.96, u_segs=52, v_segs=24, name="bun_remnant", material="item_bun_golden")
            .deform(_sculpt_bun_dome)
            .translate(0.0, 0.062, -0.05)
        )
        new_pos, b_mask = _project_bites(raw_bun.positions, rem_bites)
        raw_bun = _tint_baked_bun(MeshPart(raw_bun.name, raw_bun.material, new_pos, recompute_smooth_normals(new_pos, raw_bun.indices), raw_bun.indices, uvs=raw_bun.uvs))
        bun_parts = [bp.rotate_xyz(rx=0.22, ry=-0.18).translate(+0.04, 0.012, -0.11) for bp in _split_mesh_by_mask(raw_bun, b_mask, "item_bun_crumb", "_white_crumb")]
        sc.add("burger", bun_parts)

        # Anchor warm sesame seeds directly onto actual surviving golden-crust surface vertices (ZERO floating seeds!)
        golden_part = next((bp for bp in bun_parts if bp.material == "item_bun_golden"), None)
        if golden_part is not None and len(golden_part.positions) > 0:
            cands = np.where((golden_part.normals[:, 1] > 0.62) & (golden_part.positions[:, 1] > 0.09))[0]
            placed: List[Tuple[float, float]] = []
            for idx in cands[::max(1, len(cands) // 28)]:
                px, py, pz = [float(v) for v in golden_part.positions[idx]]
                if any(math.hypot(px - qx, pz - qz) < 0.062 for qx, qz in placed):
                    continue
                nx, ny, nz = [float(v) for v in golden_part.normals[idx]]
                placed.append((px, pz))
                k = len(placed)
                sc.add(
                    "burger",
                    ellipsoid(0.0076, 0.0038, 0.0138, u_segs=8, v_segs=6, name=f"rem_sesame_{k}", material="item_sesame")
                    .rotate_xyz(rx=math.atan2(nz, max(0.2, ny)), ry=k * 1.1, rz=-math.atan2(nx, max(0.2, ny)))
                    .translate(px + nx * 0.001, py + ny * 0.001, pz + nz * 0.001),
                )
                if len(placed) >= 12:
                    break

        # Craggy flame-seared ground-beef patty remnants tucked underneath bottom-right edge (`255:fdburgr3`).
        for idx, (cx, cz, cr, sy_m) in enumerate(((+0.09, +0.18, 0.096, 0.30), (+0.14, +0.06, 0.072, 0.26), (-0.03, +0.10, 0.068, 0.26))):
            scrap = (
                superellipsoid(cr, cr * sy_m, cr * 0.88, e1=0.60, e2=0.88, u_segs=18, v_segs=12, name=f"patty_crumb_{idx}", material="item_beef_patty")
                .deform(lambda p, k=idx: p + 0.011 * np.sin(p * 42.0 + k * 1.7) * np.cos(np.roll(p, 1, axis=1) * 36.0))
                .translate(cx, cr * sy_m * 0.85 + 0.003, cz)
            )
            sc.add("burger", scrap)

        # Crumpled bright-lime crinkled lettuce peeking behind upper-left bun notch, under crumb & inside onion ring!
        for k, (lx, ly, lz, rot_y, sc_w, sc_l, sd) in enumerate((
            (-0.06, 0.056, -0.22, 3.05, 0.085, 0.13, 0.2),   # tiny green tuft peeking top-left of bun!
            (+0.06, 0.050, -0.04, 1.45, 0.125, 0.18, 1.3),   # ruffled lettuce spilling under bitten bun crumb
            (-0.04, 0.044, +0.02, 0.45, 0.125, 0.19, 2.6),   # bright ruffled lettuce bridging into onion ring
            (+0.03, 0.046, +0.07, -0.25, 0.110, 0.16, 3.9),  # bright lime heart filling upper half of onion ring
        )):
            sc.add("burger", _build_crisp_lettuce_sheet(f"lettuce_rem_{k}", width=sc_w, length=sc_l, bend_down=0.010, seed=sd).rotate_xyz(ry=rot_y).translate(lx, ly, lz))

        # Organic fleshy hollowed red tomato wheel slice spilled onto bottom-left (`255:fdburgr3`)!
        for tp in _build_tomato_wheel("tomato_rem", radius=0.140, thick=0.024, open_pockets=True):
            sc.add("burger", tp.rotate_xyz(rx=0.30, ry=-0.28, rz=-0.26).translate(-0.20, 0.060, +0.09))

        # Crinkle-cut dark-green dill pickle slice nestled prominently inside the large white onion ring!
        sc.add("burger", _build_pickle_chip("pickle_rem_in", radius=0.082).rotate_xyz(rx=0.16, ry=0.25, rz=-0.08).translate(+0.01, 0.056, +0.14))

        # ONE iconic slender crisp white oval onion ring draped cleanly ON TOP across front-center (`255:fdburgr3`)!
        onion_a = (
            torus_arc(0.132, 0.0082, u_segs=36, v_segs=10, plane="xz", name="onion_rem_a", material="item_onion")
            .scale(0.82, 1.0, 1.24)
            .rotate_xyz(rx=0.12, ry=-0.16, rz=-0.06)
            .translate(+0.02, 0.082, +0.16)
        )
        sc.add("burger", onion_a)

        # Detached curled dark-green ribbed pickle half-chip lying loose on the FAR RIGHT (`255:fdburgr3`)!
        sc.add("burger", _build_pickle_chip("pickle_rem_loose", radius=0.088, curl=0.048, half_chip=True).rotate_xyz(rx=0.26, ry=-0.58, rz=0.44).translate(+0.28, 0.042, +0.14))
        return _finalize_item(sc, target_span=0.92)

    # Stage 2 (`254:fdburgr2`): EXACTLY THREE SEPARATE SCALLOPED BITES — NEVER CUT IN HALF!
    # 1. Back-left (~10:30) nibble  2. Front-right (~4:30) deep bite exposing pickle  3. Right-rim (~2:30) separate bite!
    top_bites = [(-0.25, -0.24, 0.148), (+0.17, +0.26, 0.148), (+0.36, -0.02, 0.128)] if stage == 2 else []
    # Lower patty & bottom heel only bitten cleanly at front-bottom-right so left tomato/onion/patty stay full & intact!
    lower_bites = [(+0.14, +0.30, 0.146), (+0.35, +0.02, 0.112)] if stage == 2 else []

    def _carve_with(part: MeshPart, bite_list: List[Tuple[float, float, float]]) -> MeshPart:
        if not bite_list:
            return part
        p_new, _ = _project_bites(part.positions, bite_list)
        out = MeshPart(part.name, part.material, p_new, recompute_smooth_normals(p_new, part.indices), part.indices, uvs=part.uvs)
        if part.colors is not None:
            out.colors = part.colors.copy()
            out._base_tint = part.colors.copy()
        return out

    def _in_bites(x: float, z: float, bite_list: List[Tuple[float, float, float]], margin: float = 0.0) -> bool:
        return any(math.hypot(x - bx, z - bz) < (br + margin) for bx, bz, br in bite_list)

    # 1. Compact tucked bottom bun heel (`y ∈ [0.00, 0.068]`, realistic thin base letting the seared patty shine!).
    bot_bun_raw = gradient_y_tint(
        superellipsoid(0.300, 0.036, 0.300, e1=0.62, e2=0.96, u_segs=44, v_segs=16, name="bun_bottom", material="item_bun_golden")
        .translate(0.0, 0.036, 0.0),
        (0.92, 0.80, 0.64),
        (1.00, 0.96, 0.88),
    )
    if lower_bites:
        bp_pos, bp_mask = _project_bites(bot_bun_raw.positions, lower_bites)
        bot_bun_raw = MeshPart(bot_bun_raw.name, bot_bun_raw.material, bp_pos, recompute_smooth_normals(bp_pos, bot_bun_raw.indices), bot_bun_raw.indices, uvs=bot_bun_raw.uvs)
        gradient_y_tint(bot_bun_raw, (0.92, 0.80, 0.64), (1.00, 0.96, 0.88))
        sc.add("burger", _split_mesh_by_mask(bot_bun_raw, bp_mask, "item_bun_crumb", "_white_crumb"))
    else:
        sc.add("burger", bot_bun_raw)

    # 2. Thick craggy flame-broiled seared ground-beef patty (`y ∈ [0.064, 0.156]`, rich `beef_seared` PBR + bumpy hand-pressed edge!).
    def _patty_shape(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ang = np.arctan2(p[:, 2], p[:, 0])
        edge_bump = 1.0 + 0.035 * np.sin(ang * 7.0 + 0.6) + 0.024 * np.cos(ang * 13.0 - 0.4)
        p[:, 0] *= edge_bump
        p[:, 2] *= edge_bump
        p += 0.0085 * np.sin(p * 38.0) * np.cos(np.roll(p, 1, axis=1) * 34.0)
        return p

    patty_raw = tint_part(
        superellipsoid(0.336, 0.048, 0.336, e1=0.58, e2=0.98, u_segs=48, v_segs=18, name="beef_patty", material="item_beef_patty")
        .deform(_patty_shape)
        .translate(0.0, 0.110, 0.0),
        lambda pos, nrm: np.clip(
            (0.84 + 0.14 * np.sin(pos[:, 0] * 26.0 + pos[:, 2] * 24.0) + 0.12 * nrm[:, 1])[:, None] * np.array([[1.0, 0.96, 0.92]]),
            0.55,
            1.0,
        ),
    )
    sc.add("burger", _carve_with(patty_raw, lower_bites))

    # 3. Juicy bright red-orange sliced tomato wheels peeking prominently front-bottom-left (`253:fdburgr1` & `254:fdburgr2` — NO loud orange cheese!).
    tomato_spots = ((-0.13, +0.24, 0.158), (+0.15, +0.16, 0.135), (-0.06, -0.16, 0.135))
    for idx, (tx, tz, tr) in enumerate(tomato_spots):
        if stage == 2 and idx == 1:
            # On Stage 2, shift right tomato slightly back so it peeks cleanly under Bite 3 right edge!
            tx, tz = +0.20, +0.02
        for tp in _build_tomato_wheel(f"tomato_{idx}", radius=tr, thick=0.025, open_pockets=False):
            sc.add("burger", _carve_with(tp.translate(tx, 0.162, tz), lower_bites))

    # 4. Crisp slender white sliced onion rings arcing boldly right above the front-bottom-left tomato!
    onion_rad = 0.152 if stage == 2 else 0.144
    onion_pos = (-0.11, 0.180, +0.21) if stage == 2 else (-0.10, 0.178, +0.22)
    onion_a = _carve_with(torus_arc(onion_rad, 0.0080, u_segs=34, v_segs=10, plane="xz", name="burger_onion_a", material="item_onion").translate(*onion_pos), lower_bites)
    onion_b = _carve_with(torus_arc(onion_rad * 0.72, 0.0072, u_segs=28, v_segs=10, plane="xz", name="burger_onion_b", material="item_onion").translate(onion_pos[0] + 0.02, onion_pos[1], onion_pos[2] - 0.01), lower_bites)
    sc.add("burger", [onion_a, onion_b])

    # 5. Organic crinkled bright-lime lettuce billowing naturally around left & right rims (extra lush on `fdburgr2`!)
    if stage == 1:
        lettuce_specs = (
            (+1.26, 0.18, 0.186, 0.18, 0.23, 0.028, 0.5),   # lush right flank (`3:00-4:30`)
            (+0.78, 0.17, 0.184, 0.16, 0.21, 0.026, 1.8),   # lower-right peek
            (-1.28, 0.18, 0.186, 0.17, 0.22, 0.028, 3.1),   # crisp left flank (`8:30-9:30`)
            (-0.62, 0.17, 0.182, 0.15, 0.19, 0.024, 4.4),   # tucked behind front-left tomato/onion
            (+2.50, 0.16, 0.186, 0.16, 0.19, 0.026, 5.7),   # back-right
            (-2.45, 0.16, 0.186, 0.16, 0.19, 0.026, 6.9),   # back-left
        )
    else:
        lettuce_specs = (
            (-1.05, 0.20, 0.184, 0.19, 0.24, 0.028, 0.6),   # lush ruffled left-front lettuce (`fdburgr2`!)
            (-0.42, 0.19, 0.180, 0.17, 0.22, 0.024, 1.9),   # peeking under white onion ring front-left
            (+0.82, 0.18, 0.182, 0.18, 0.23, 0.026, 3.2),   # spilling out front-right under pickle bite!
            (+1.42, 0.18, 0.186, 0.17, 0.21, 0.026, 4.5),   # peeking right edge under Bite 3
            (-2.25, 0.16, 0.184, 0.15, 0.18, 0.022, 5.8),   # back-left
            (+2.40, 0.16, 0.184, 0.15, 0.18, 0.022, 6.9),   # back-right
        )
    for k, (ang, rad, ly, lw, ll, bd, sd) in enumerate(lettuce_specs):
        lx, lz = rad * math.sin(ang), rad * math.cos(ang)
        leaf = _build_crisp_lettuce_sheet(f"lettuce_{k}", width=lw, length=ll, bend_down=bd, seed=sd).rotate_xyz(ry=ang).translate(lx, ly, lz)
        sc.add("burger", _carve_with(leaf, lower_bites if stage == 2 else []))

    # 6. Crinkle-cut dark-green ribbed dill pickle slice:
    #    - Stage 1: peeking front-right (`4:30-5:30`) right above beef patty (`fdburgr1`)!
    #    - Stage 2: prominently exposed RIGHT INSIDE Bite 2 (`4:30`) underneath the bitten white bread crumb (`fdburgr2`)!
    if stage == 1:
        sc.add("burger", _build_pickle_chip("burger_pickle", radius=0.095).rotate_xyz(rx=0.10, ry=0.25, rz=-0.06).translate(+0.13, 0.176, +0.25))
    else:
        sc.add("burger", _build_pickle_chip("burger_pickle_bite", radius=0.088).rotate_xyz(rx=0.14, ry=-0.18, rz=-0.06).translate(+0.09, 0.194, +0.18))

    # 7. Plump wide oven-toasted golden-russet sesame top bun with authentic diagonal crown dimple & 3 separate bites on Stage 2!
    top_bun_raw = _tint_baked_bun(
        superellipsoid(0.348, 0.164, 0.342, e1=0.84, e2=0.96, u_segs=56, v_segs=28, name="bun_top", material="item_bun_golden")
        .deform(_sculpt_bun_dome)
        .translate(0.0, 0.214, -0.010)
    )
    if top_bites:
        tb_pos, tb_mask = _project_bites(top_bun_raw.positions, top_bites)
        top_bun_raw = _tint_baked_bun(MeshPart(top_bun_raw.name, top_bun_raw.material, tb_pos, recompute_smooth_normals(tb_pos, top_bun_raw.indices), top_bun_raw.indices, uvs=top_bun_raw.uvs))
        sc.add("burger", _split_mesh_by_mask(top_bun_raw, tb_mask, "item_bun_crumb", "_white_crumb"))
    else:
        sc.add("burger", top_bun_raw)

    # 8. Delicate scattered warm-ivory toasted sesame seeds nestled flush onto the bun crown (excluded cleanly from bites!).
    golden_angle = 2.39996323
    for i in range(40):
        f = (i + 0.5) / 40.0
        r_frac = 0.80 * math.sqrt(f)
        th = i * golden_angle
        sx = 0.336 * r_frac * math.cos(th)
        sz = 0.330 * r_frac * math.sin(th) - 0.010
        if top_bites and _in_bites(sx, sz, top_bites, margin=0.024):
            continue
        # Match the diagonal crown dimple depression so seeds never float over the crease!
        u_d = (sx + (sz + 0.010)) * 0.707
        v_d = (sx - (sz + 0.010)) * 0.707
        sy_base = 0.164 * ((max(0.01, 1.0 - (r_frac ** (2.0 / 0.84)))) ** (0.84 * 0.5))
        crease_y = -0.019 * math.exp(-(((v_d + 0.03) / 0.048) ** 2 + ((u_d + 0.02) / 0.17) ** 2)) * min(1.0, sy_base / 0.11)
        sy = 0.214 + sy_base + crease_y + 0.0012
        seed = (
            ellipsoid(0.0078, 0.0038, 0.0142, u_segs=8, v_segs=6, name=f"sesame_{i}", material="item_sesame")
            .rotate_xyz(rx=0.48 * r_frac * math.sin(th), ry=th + 0.8 * i, rz=-0.48 * r_frac * math.cos(th))
            .translate(sx, sy, sz)
        )
        sc.add("burger", seed)

    return _finalize_item(sc, target_span=0.92)


def build_fdburgr1() -> SceneBuilder:
    return _build_burger(1)


def build_fdburgr2() -> SceneBuilder:
    return _build_burger(2)


def build_fdburgr3() -> SceneBuilder:
    return _build_burger(3)


def _sculpt_corndog_wood_stick(name: str, z0: float, z1: float, *, y_base: float = 0.090, radius: float = 0.015, stain_end: float = +1.0) -> MeshPart:
    """Realistic pale raw-birch wooden skewer stick with longitudinal wood-fiber grain & warm fryer-oil stain (`361..365`)."""
    n_z = 22
    pts = [
        (
            0.003 * math.sin(i / float(n_z) * math.pi * 1.4),
            y_base + 0.003 * math.cos(i / float(n_z) * math.pi),
            z0 + (z1 - z0) * (i / float(n_z)),
        )
        for i in range(n_z + 1)
    ]
    stick = curved_tube(
        pts,
        [radius * (0.94 if i in (0, n_z) else 1.0) for i in range(n_z + 1)],
        samples_per_seg=2,
        radial_segs=18,
        cap_rings=3,
        name=name,
        material="item_wood_stick",
    )
    # Slightly subtle hexagonal/natural birch-dowel grain flattening + warm pale wood fiber & toasted fryer-oil stain near batter!
    z_lo, z_hi = min(z0, z1), max(z0, z1)
    z_span = max(1e-4, z_hi - z_lo)

    def _birch_wood_tint(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
        ang = np.arctan2(pos[:, 1] - y_base, pos[:, 0])
        # Fine longitudinal wood fibers + faint growth-ring streaks along the stick!
        fiber = 0.5 + 0.5 * np.sin(ang * 14.0 + pos[:, 2] * 7.5 + 0.8 * np.sin(pos[:, 2] * 28.0))
        ring = 0.5 + 0.5 * np.cos(pos[:, 2] * 16.0 + 2.2 * np.sin(ang * 3.0))
        # Subtle warm toasted fryer-oil darkening near the end that sat inside the corn dog!
        t_z = np.clip((pos[:, 2] - z_lo) / z_span, 0.0, 1.0)
        oil = np.clip((t_z - 0.35) / 0.65, 0.0, 1.0) if stain_end > 0 else np.clip((0.65 - t_z) / 0.65, 0.0, 1.0)
        top_hi = np.clip(0.55 + 0.45 * nrm[:, 1], 0.35, 1.0)
        # Pale raw birch cream-tan wood (`#D6B486`) -> warm golden-brown oiled wood (`#9A6A3A`)!
        r_c = (0.92 + 0.08 * fiber - 0.06 * ring - 0.16 * oil) * top_hi
        g_c = (0.82 + 0.08 * fiber - 0.07 * ring - 0.22 * oil) * top_hi
        b_c = (0.64 + 0.06 * fiber - 0.07 * ring - 0.26 * oil) * top_hi
        return np.clip(np.column_stack([r_c, g_c, b_c]), 0.26, 1.0)

    return tint_part(stick, _birch_wood_tint)


def build_fdcornd1() -> SceneBuilder:
    """Classic full-length golden fried corn dog on a stick with one clean top bite slicing cleanly through BOTH batter AND hot dog (`361:fdcornd1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdcornd1")

    yc = 0.096

    # After `ry = -135 deg`: local `+Z` is the top-left tip, local `+X` faces front-bottom-left toward the viewer, local `+Y` is UP!
    # So a clean curved crescent bite chomped out of the TOP-FRONT corner of the tip (`+X, +Y, +Z`) exposes BOTH the pale-yellow
    # cooked cornbread ring AND the plump rosy-brown bitten hot dog inside, framed by the intact rounded back-top golden crust (`361:fdcornd1`)!
    bite_center = np.array([+0.095, yc + 0.072, +0.295], dtype=np.float64)
    bite_radii = np.array([0.118, 0.112, 0.132], dtype=np.float64)

    def _carve_shared_bite(part: MeshPart, *, shrink: float = 0.0, crust_jitter: float = 0.0) -> Tuple[MeshPart, np.ndarray]:
        p = part.positions.astype(np.float64).copy()
        if crust_jitter > 0.0:
            r = np.maximum(1e-5, np.hypot(p[:, 0], p[:, 1] - yc))
            micro = (
                crust_jitter * np.sin(p[:, 2] * 34.0 + p[:, 0] * 38.0) * np.cos(p[:, 1] * 36.0)
                + 0.55 * crust_jitter * np.cos(p[:, 2] * 58.0 - p[:, 1] * 48.0)
            )
            p[:, 0] += micro * (p[:, 0] / r)
            p[:, 1] += micro * ((p[:, 1] - yc) / r)
        # Ellipsoidal tooth-bite scoop with subtle organic human bite scallop!
        rad = bite_radii - shrink
        rel = (p - bite_center[None, :]) / rad[None, :]
        dist = np.linalg.norm(rel, axis=1)
        scallop = 0.032 * np.cos(np.arctan2(rel[:, 1], rel[:, 2]) * 4.0)
        eff_dist = dist + scallop
        inside = eff_dist < 1.0
        if np.any(inside):
            safe_d = np.maximum(1e-5, dist[inside])
            target_d = 1.0 - scallop[inside]
            p[inside] = bite_center[None, :] + (rel[inside] * (target_d / safe_d)[:, None]) * rad[None, :]
        bitten_mask = eff_dist < 1.045
        out = MeshPart(part.name, part.material, p.astype(np.float32), recompute_smooth_normals(p.astype(np.float32), part.indices), part.indices, uvs=part.uvs)
        return out, bitten_mask

    # 1. Full-length classic fairground golden corn-dog batter capsule (`~4:1` proportion, smooth rounded tip cradling the top bite!).
    raw_batter = curved_tube(
        [(0.0, yc, -0.345), (0.0, yc, -0.250), (0.0, yc, 0.000), (0.0, yc, +0.255), (0.0, yc, +0.365)],
        [(0.056, 0.054), (0.090, 0.088), (0.092, 0.090), (0.090, 0.088), (0.068, 0.066)],
        samples_per_seg=10,
        radial_segs=42,
        cap_rings=10,
        name="corndog_batter",
        material="item_corndog_crust",
    )
    bitten_batter, batter_bite_mask = _carve_shared_bite(raw_batter, shrink=0.0, crust_jitter=0.0026)
    gradient_y_tint(bitten_batter, (0.78, 0.68, 0.54), (1.04, 0.98, 0.86), noise_amp=0.04)
    batter_parts = _split_mesh_by_mask(bitten_batter, batter_bite_mask, "item_corndog_crumb", "_crumb")
    for bp in batter_parts:
        if bp.material == "item_corndog_crumb":
            tint_part(bp, lambda pos, _n: np.column_stack([
                np.full(len(pos), 1.00),
                np.full(len(pos), 0.96),
                np.full(len(pos), 0.78),
            ]))
    sc.add("corndog", batter_parts)

    # 2. Encased frankfurter hot dog COMPLETELY INSIDE the batter, bitten cleanly by the EXACT SAME top bite cavity (`shrink=+0.005` proud so zero z-fighting)!
    raw_hotdog = curved_tube(
        [(0.0, yc, -0.220), (0.0, yc, +0.100), (0.0, yc, +0.330)],
        [(0.058, 0.057), (0.060, 0.059), (0.056, 0.055)],
        samples_per_seg=10,
        radial_segs=36,
        cap_rings=8,
        name="corndog_hotdog",
        material="item_hotdog_pink",
    )
    bitten_hotdog, dog_bite_mask = _carve_shared_bite(raw_hotdog, shrink=0.0055, crust_jitter=0.0)
    gradient_y_tint(bitten_hotdog, (0.84, 0.72, 0.66), (1.02, 0.94, 0.88), noise_amp=0.03)
    dog_parts = _split_mesh_by_mask(bitten_hotdog, dog_bite_mask, "item_hotdog_inside", "_meat")
    for dp in dog_parts:
        if dp.material == "item_hotdog_inside":
            tint_part(dp, lambda pos, _n: np.column_stack([
                0.96 + 0.04 * np.sin(pos[:, 0] * 60.0 + pos[:, 1] * 55.0),
                0.84 + 0.06 * np.cos(pos[:, 0] * 50.0 - pos[:, 1] * 48.0),
                0.78 + 0.05 * np.sin(pos[:, 1] * 65.0),
            ]))
    sc.add("corndog", dog_parts)

    # 3. Natural fibrous birch wooden skewer stick protruding cleanly from the tapered stick end (`-Z`).
    stick = _sculpt_corndog_wood_stick("corndog_stick", -0.665, -0.320, y_base=yc - 0.006, radius=0.0155, stain_end=+1.0)
    sc.add("corndog", stick)

    # 4. Glossy bright yellow 5-loop squeezed mustard zigzag running smoothly along the full top of the batter behind the bite!
    mustard_pts: List[Tuple[float, float, float]] = []
    n_m = 48
    for i in range(n_m + 1):
        u = i / float(n_m)
        mz = -0.290 + 0.450 * u
        mx = 0.050 * math.sin(u * math.pi * 8.6)
        r_surf = 0.090 * math.sqrt(max(0.22, 1.0 - ((mx / 0.094) ** 2)))
        end_taper = math.sin(u * math.pi) ** 0.14
        my = yc + r_surf * (0.90 + 0.10 * end_taper) + 0.006
        mustard_pts.append((mx, my, mz))
    mustard = curved_tube(
        mustard_pts,
        [0.0120] * len(mustard_pts),
        samples_per_seg=2,
        radial_segs=12,
        cap_rings=2,
        name="corndog_mustard",
        material="item_mustard",
    )
    sc.add("corndog", mustard)

    # Angle top-left (clean bitten tip facing viewer) -> bottom-right (wooden stick) matching `361:fdcornd1`!
    sc.nodes["corndog"] = [p.rotate_xyz(ry=-135.0, degrees=True) for p in sc.nodes["corndog"]]
    return _finalize_item(sc, target_span=0.96)


def _build_banana_coin(name: str, radius: float = 0.080, thickness: float = 0.032, spin_rad: float = 0.0) -> MeshPart:
    """Chunky 3-lobed organic sliced banana coin with centered planar face UVs for crisp seed-star texture."""
    def _trilobe(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ang = np.arctan2(p[:, 2], p[:, 0]) + spin_rad
        # Real banana cross-sections are softly 3-sided with a delicate vascular ridge notch.
        shape = 1.0 + 0.056 * np.cos(ang * 3.0) - 0.024 * np.cos(ang * 6.0) - 0.030 * np.exp(-(((ang % (2.0 * math.pi)) - 0.55) / 0.24) ** 2)
        p[:, 0] *= shape
        p[:, 2] *= shape
        return p

    coin = cylinder(radius, radius, thickness, radial_segs=28, bevel=thickness * 0.18, axis="y", name=name, material="item_banana_slice").deform(_trilobe)
    # Planar face UV projection centered at (0.5, 0.5) so the 3-armed banana seed star sits dead-center on both coin faces!
    inv_r = 0.475 / max(1e-5, radius)
    u = np.clip(0.5 + coin.positions[:, 0] * inv_r, 0.02, 0.98)
    v = np.clip(0.5 + coin.positions[:, 2] * inv_r, 0.02, 0.98)
    coin.uvs = np.column_stack([u, v]).astype(np.float32)
    return tint_part(
        coin,
        lambda pos, nrm: np.where(
            np.abs(nrm[:, 1:2]) > 0.45,
            np.array([[1.00, 0.99, 0.93]]),
            np.array([[0.92, 0.86, 0.72]]),
        ) * np.ones((len(pos), 3), dtype=np.float64),
    )


def build_fdjelo1() -> SceneBuilder:
    """Tall conical-bell 10-lobe scalloped ruby Jell-O mold with fully encased banana coins & 1.05s wobble (`366:fdjelo1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdjelo1")

    # Classic tall conical-bell Victorian 10-lobe scalloped gelatin mold (wide wavy base -> graceful taper -> tight pinched rosette crown!).
    mold_h = 0.610
    outer_prof = [
        (0.365, 0.000),
        (0.382, 0.036),
        (0.376, 0.105),
        (0.352, 0.195),
        (0.312, 0.295),
        (0.258, 0.392),
        (0.194, 0.478),
        (0.128, 0.548),
        (0.068, 0.596),
        (0.028, 0.610),
        (0.000, 0.586),
    ]

    def _lobe_arch(ang: np.ndarray) -> np.ndarray:
        """10 fat rounded continuous scallop flutes separated by rich crimson valleys (valley dead front center at +Z)."""
        s = np.abs(np.sin(5.0 * (ang - 0.5 * math.pi)))
        arch = np.power(s * s + 0.045, 0.34)
        return np.clip((arch - 0.348) / 0.668, 0.0, 1.0)

    def _flute_scale(ang: np.ndarray, h_frac: np.ndarray, strength: float = 1.0) -> np.ndarray:
        lobe = _lobe_arch(ang)
        # Continuous scalloped petal skirt all the way down to y=0, smoothly tapering into the tight pinched crown rosette!
        env = (0.88 + 0.14 * np.sin(np.clip(h_frac, 0.0, 1.0) * math.pi * 0.90)) * (1.0 - 0.36 * np.power(np.clip((h_frac - 0.78) / 0.22, 0.0, 1.0), 1.5))
        return 1.0 + strength * env * (-0.115 + 0.225 * lobe)

    def _outer_radius_at(y: float, ang: float) -> float:
        ys = [pt[1] for pt in outer_prof[:-1]]
        rs = [pt[0] for pt in outer_prof[:-1]]
        r_base = float(np.interp(y, ys, rs))
        sc_f = float(_flute_scale(np.array([ang]), np.array([y / mold_h]), 1.0)[0])
        return r_base * sc_f

    def _flute_mold(strength: float = 1.0):
        def _deform(pos: np.ndarray) -> np.ndarray:
            p = pos.copy()
            ang = np.arctan2(p[:, 2], p[:, 0])
            h_frac = np.clip(p[:, 1] / mold_h, 0.0, 1.0)
            flute = _flute_scale(ang, h_frac, strength)
            p[:, 0] *= flute
            p[:, 2] *= flute
            crown_w = np.clip((h_frac - 0.84) / 0.16, 0.0, 1.0) * np.clip(np.hypot(p[:, 0], p[:, 2]) / 0.05, 0.0, 1.0)
            p[:, 1] += 0.012 * crown_w * (_lobe_arch(ang) - 0.5)
            return p
        return _deform

    # 1. Dense rich ruby-garnet inner gelatin mass filling snugly inside the fluted mold (ZERO hollow jellyfish skirt gap!).
    core_prof = [(r * (0.85 - 0.04 * min(1.0, y / mold_h)), y * 0.94 + 0.006) for r, y in outer_prof]
    inner_core = tint_part(
        lathe(core_prof, radial_segs=80, cap_bottom=True, cap_top=False, axis="y", name="jello_inner_core", material="item_jello_core")
        .deform(_flute_mold(strength=0.96)),
        lambda pos, _n: np.column_stack([
            0.52 + 0.48 * _lobe_arch(np.arctan2(pos[:, 2], pos[:, 0])) * (0.76 + 0.24 * np.clip(pos[:, 1] / mold_h, 0.0, 1.0)),
            0.10 + 0.24 * _lobe_arch(np.arctan2(pos[:, 2], pos[:, 0])),
            0.16 + 0.26 * _lobe_arch(np.arctan2(pos[:, 2], pos[:, 0])),
        ]),
    )
    sc.add("jello_body", inner_core)

    # 2. Chunky, unmistakable 3-lobed creamy-yellow banana coins organically staggered & FULLY ENCASED INSIDE the glossy ruby gelatin!
    # Spec: (ang_from_front_deg, y, radius, tilt_out_deg, side_roll_deg, spin_deg)
    banana_specs = (
        # Organically staggered front lower pair matching `366:fdjelo1` (lower-left slightly lower, lower-right slightly higher & fuller!)
        (+22.0, 0.118, 0.076, 56.0, +14.0,  25.0),
        (-21.0, 0.160, 0.078, 60.0, -12.0, 140.0),
        # Organically staggered mid-slope pair (mid-left higher & angled, mid-right nestled slightly lower!)
        (+20.0, 0.332, 0.074, 48.0, +18.0, 215.0),
        (-19.0, 0.302, 0.074, 52.0, -16.0,  80.0),
        # Peeking flank slices (mid-left & lower-right angled partially edge-on matching `366:fdjelo1`!)
        (+58.0, 0.238, 0.068, 64.0, +24.0, 310.0),
        (-54.0, 0.210, 0.068, 70.0, -28.0,  45.0),
        # Upper shoulder / crown-hugging slices (top-left crown peek at ~11:30 & upper-right tilted slice at ~1:30!)
        (+18.0, 0.482, 0.052, 30.0, +16.0, 175.0),
        (-52.0, 0.412, 0.062, 42.0, -22.0, 260.0),
        # Full 360-degree organically staggered coverage around sides & back lobes!
        (+94.0, 0.150, 0.070, 58.0,  +8.0, 110.0),
        (-92.0, 0.190, 0.070, 60.0, -14.0, 290.0),
        (+126.0, 0.305, 0.070, 50.0, +12.0,  35.0),
        (-130.0, 0.275, 0.070, 52.0, -10.0, 195.0),
        (+160.0, 0.165, 0.074, 56.0,  +8.0, 125.0),
        (-164.0, 0.365, 0.064, 44.0, -10.0, 245.0),
    )
    ys_prof = np.array([pt[1] for pt in outer_prof[:-1]], dtype=np.float64)
    rs_prof = np.array([pt[0] for pt in outer_prof[:-1]], dtype=np.float64)

    for idx, (ang_deg, by, rad, tilt_deg, roll_deg, spin_deg) in enumerate(banana_specs):
        ang = 0.5 * math.pi + math.radians(ang_deg)
        r_wall = _outer_radius_at(by, ang)
        # Seat coin snugly right behind the glossy ruby outer skin so banana slices glow vividly right through the Jell-O!
        r_pos = max(0.04, r_wall - 0.046)
        bx, bz = r_pos * math.cos(ang), r_pos * math.sin(ang)
        yaw_out = math.atan2(bx, bz)
        sl = (
            _build_banana_coin(f"jello_banana_{idx}", radius=rad, thickness=0.024, spin_rad=math.radians(spin_deg))
            .rotate_xyz(rx=tilt_deg, rz=roll_deg, degrees=True)
            .rotate_xyz(ry=yaw_out)
            .translate(bx, by, bz)
        )
        vp = sl.positions.astype(np.float64)
        v_ang = np.arctan2(vp[:, 2], vp[:, 0])
        v_r = np.hypot(vp[:, 0], vp[:, 2])
        v_rb = np.interp(np.clip(vp[:, 1], 0.0, mold_h), ys_prof, rs_prof)
        v_lim = v_rb * _flute_scale(v_ang, np.clip(vp[:, 1] / mold_h, 0.0, 1.0), 1.0) - 0.012
        excess = float(np.max(v_r - v_lim))
        if excess > 0.0:
            sl = sl.translate(-excess * math.cos(ang), 0.0, -excess * math.sin(ang))
        sc.add("jello_body", sl)

    # 3. Crystal-glossy translucent outer 10-lobe ruby gelatin skin (high-res 96 radial segs for liquid-smooth wet flutes!).
    jelly_dome = lathe(outer_prof, radial_segs=96, cap_bottom=True, cap_top=False, axis="y", name="jello_dome", material="item_jello_ruby").deform(_flute_mold(strength=1.0))
    sc.add("jello_body", jelly_dome)

    # Authentic 1998 7-frame (1.05s) subtle grounded gelatinous Jell-O jiggle (gentle ±4.5% settle + delicate lateral wobble —
    # NO wild pulsing jellyfish umbrella bounce!).
    times = [0.00, 0.15, 0.30, 0.45, 0.60, 0.75, 0.90, 1.05]
    scales = [
        (1.000, 1.000, 1.000),  # F0: upright settled dome
        (1.018, 0.968, 1.012),  # F1: gentle jiggle dip
        (1.036, 0.946, 1.030),  # F2: soft settle
        (1.028, 0.958, 1.034),  # F3: slight counter-shift
        (0.980, 1.034, 0.984),  # F4: light gelatin rebound
        (0.986, 1.026, 0.988),  # F5: crest quiver
        (1.022, 0.966, 1.018),  # F6: damped secondary jiggle
        (1.000, 1.000, 1.000),  # seamless loop
    ]
    def _q_tilt(rx_deg: float, rz_deg: float) -> Tuple[float, float, float, float]:
        hx, hz = math.radians(rx_deg) * 0.5, math.radians(rz_deg) * 0.5
        cx, sx, cz, sz = math.cos(hx), math.sin(hx), math.cos(hz), math.sin(hz)
        return (sx * cz, -sx * sz, cx * sz, cx * cz)

    rots = [
        _q_tilt(0.0, 0.0),
        _q_tilt(+1.4, -2.2),
        _q_tilt(+2.0, -3.0),
        _q_tilt(-1.2, +2.8),
        _q_tilt(-1.8, +2.2),
        _q_tilt(+0.8, -1.6),
        _q_tilt(-0.6, +1.2),
        _q_tilt(0.0, 0.0),
    ]
    sc.add_animation(
        "Idle",
        [
            AnimationChannel("jello_body", "scale", times, scales),
            AnimationChannel("jello_body", "rotation", times, rots),
        ],
    )
    return _finalize_item(sc, target_span=0.88)


def _build_fried_chicken_drumstick(stage: int) -> SceneBuilder:
    """Unified authentic fried chicken drumstick progressively eaten from identical crispy geometry (`399..403:fdchick1..5`)."""
    register_item_materials()
    sc = SceneBuilder(name=f"item_fdchick{stage}")

    # Shared anatomical drumstick tibia bone (exact same silhouette across ALL 5 stages `fdchick1..5`!).
    def _sculpt_tibia_bone(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        t_ank = np.clip((-p[:, 2] - 0.244) / 0.121, 0.0, 1.0)
        dy = p[:, 1] - 0.078
        joint_flare = np.clip((t_ank - 0.58) / 0.42, 0.0, 1.0) ** 1.35
        condyle_w = 1.0 + 0.48 * joint_flare
        notch = -0.0085 * joint_flare * np.exp(-((p[:, 0] / 0.014) ** 2))
        p[:, 0] *= condyle_w
        p[:, 1] = 0.078 + dy * (0.88 + 0.22 * joint_flare) + notch * np.sign(dy + 1e-5)
        p[:, 2] += -notch * 0.85
        return p

    def _bone_tint(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
        end_w = np.clip((np.abs(pos[:, 2]) - 0.20) / 0.14, 0.0, 1.0)
        return np.clip(
            np.column_stack([
                0.80 + 0.18 * end_w + 0.05 * nrm[:, 1],
                0.73 + 0.22 * end_w + 0.06 * nrm[:, 1],
                0.70 + 0.22 * end_w + 0.06 * nrm[:, 1],
            ]),
            0.48,
            1.0,
        )

    bone_parts: List[MeshPart] = []
    full_top_bone = stage >= 4
    z_bone_hi = +0.340 if full_top_bone else +0.160
    bone_shaft = tint_part(
        curved_tube(
            [(0.0, 0.078, -0.365), (0.0, 0.078, -0.340), (0.0, 0.079, -0.285), (0.004, 0.082, 0.0), (0.0, 0.084, z_bone_hi)],
            [(0.024, 0.022), (0.022, 0.020), (0.019, 0.017), (0.021, 0.019), (0.036 if full_top_bone else 0.024, 0.030 if full_top_bone else 0.022)],
            samples_per_seg=6,
            radial_segs=20,
            cap_rings=5,
            name=f"chick{stage}_bone_shaft",
            material="item_bone_white",
        ).deform(_sculpt_tibia_bone),
        _bone_tint,
    )
    condyle_l = tint_part(ellipsoid(0.018, 0.022, 0.022, u_segs=14, v_segs=10, name=f"chick{stage}_cond_l", material="item_bone_white").rotate_xyz(ry=0.18).translate(+0.015, 0.078, -0.356), _bone_tint)
    condyle_r = tint_part(ellipsoid(0.017, 0.021, 0.021, u_segs=14, v_segs=10, name=f"chick{stage}_cond_r", material="item_bone_white").rotate_xyz(ry=-0.18).translate(-0.014, 0.077, -0.355), _bone_tint)
    bone_parts.extend([bone_shaft, condyle_l, condyle_r])
    if full_top_bone:
        bone_parts.append(tint_part(ellipsoid(0.034, 0.030, 0.036, u_segs=16, v_segs=10, name=f"chick{stage}_knee_a", material="item_bone_white").translate(-0.022, 0.090, +0.342), _bone_tint))
        bone_parts.append(tint_part(ellipsoid(0.032, 0.028, 0.034, u_segs=16, v_segs=10, name=f"chick{stage}_knee_b", material="item_bone_white").translate(+0.022, 0.085, +0.335), _bone_tint))

    if stage == 5:
        sc.add("chicken", [p.rotate_xyz(ry=136.0, degrees=True) for p in bone_parts])
        return _finalize_item(sc, target_span=0.88)

    # Identical drumstick spine & Maillard crispy crust geometry across stages 1..4 — progressively bitten from upper-right!
    spine_pts = [
        (0.000, 0.082, -0.255),
        (0.004, 0.098, -0.150),
        (0.014, 0.122, -0.020),
        (0.020, 0.144, +0.135),
        (0.008, 0.140, +0.285),
        (-0.006, 0.118, +0.382),
    ]
    if stage == 1:
        spine_rads = [(0.044, 0.042), (0.068, 0.062), (0.114, 0.100), (0.168, 0.144), (0.152, 0.132), (0.066, 0.060)]
        z_flake_hi, n_flakes = +0.370, 92
    elif stage == 2:
        # Stage 2 (`fdchick2`): first generous bite scooped cleanly off the upper-left meat lobe!
        spine_rads = [(0.044, 0.042), (0.066, 0.060), (0.106, 0.092), (0.138, 0.116), (0.128, 0.110), (0.060, 0.054)]
        z_flake_hi, n_flakes = +0.355, 76
    elif stage == 3:
        # Stage 3 (`fdchick3`): half-eaten — upper crest bitten deep showing pale cooked chicken meat patch!
        spine_rads = [(0.042, 0.040), (0.064, 0.058), (0.092, 0.078), (0.114, 0.094), (0.104, 0.088), (0.050, 0.044)]
        z_flake_hi, n_flakes = +0.335, 62
    else:
        # Stage 4 (`fdchick4`): almost gone — crispy crust remnant clinging around middle shaft with upper bone knuckle exposed!
        spine_pts = [(0.000, 0.082, -0.240), (0.006, 0.094, -0.105), (0.010, 0.102, +0.055), (0.006, 0.094, +0.195), (0.002, 0.086, +0.250)]
        spine_rads = [(0.040, 0.038), (0.066, 0.058), (0.084, 0.072), (0.062, 0.054), (0.032, 0.030)]
        z_flake_hi, n_flakes = +0.235, 42

    z_knots = np.array([pt[2] for pt in spine_pts], dtype=np.float64)
    x_knots = np.array([pt[0] for pt in spine_pts], dtype=np.float64)
    y_knots = np.array([pt[1] for pt in spine_pts], dtype=np.float64)
    rx_knots = np.array([r[0] for r in spine_rads], dtype=np.float64)
    ry_knots = np.array([r[1] for r in spine_rads], dtype=np.float64)

    def _crispy_drumstick_sculpt(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        cx = np.interp(p[:, 2], z_knots, x_knots)
        cy = np.interp(p[:, 2], z_knots, y_knots)
        dx, dy = p[:, 0] - cx, p[:, 1] - cy
        r = np.maximum(1e-5, np.hypot(dx, dy))
        ang = np.arctan2(dy, dx)
        meat_env = np.clip((p[:, 2] + 0.22) / 0.45, 0.0, 1.0)
        asymm = 1.0 + 0.10 * meat_env * np.cos(ang - 1.1) - 0.06 * meat_env * np.sin(ang * 2.0 + 0.4)
        fold = 0.017 * meat_env * np.sin(p[:, 2] * 24.0 + ang * 2.0 + 0.6 * np.cos(p[:, 2] * 14.0))
        crunch1 = 0.015 * np.abs(np.sin(p[:, 0] * 38.0 + p[:, 2] * 34.0 + 0.8 * np.cos(p[:, 1] * 32.0))) - 0.0075
        crunch2 = 0.010 * np.abs(np.cos(p[:, 2] * 62.0 - p[:, 0] * 48.0 + p[:, 1] * 54.0)) - 0.005
        disp = (asymm - 1.0) * r + fold + crunch1 + crunch2
        p[:, 0] += disp * (dx / r)
        p[:, 1] += disp * (dy / r)
        # Concave bite scoop on upper-front-left ridge for stages 2 & 3 matching `strip_chicken.png`!
        if stage in (2, 3):
            b_ctr = np.array([+0.095, 0.185 if stage == 2 else 0.160, +0.105], dtype=np.float64)
            b_rad = np.array([0.105, 0.090, 0.135 if stage == 2 else 0.155], dtype=np.float64)
            rel = (p - b_ctr[None, :]) / b_rad[None, :]
            d_b = np.linalg.norm(rel, axis=1)
            hit = d_b < 1.0
            if np.any(hit):
                p[hit] = b_ctr[None, :] + (rel[hit] / np.maximum(1e-5, d_b[hit])[:, None]) * b_rad[None, :]
        p[:, 1] = np.where(p[:, 1] < 0.018, 0.018 + 0.35 * (p[:, 1] - 0.018), p[:, 1])
        return p

    def _maillard_tint(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
        up = np.clip(0.55 + 0.45 * nrm[:, 1], 0.0, 1.0)
        crag = np.sin(pos[:, 2] * 36.0 + pos[:, 0] * 42.0) * np.cos(pos[:, 1] * 38.0 - pos[:, 2] * 24.0)
        dark_patch = np.clip(0.5 - 0.5 * crag, 0.0, 1.0)
        base = np.column_stack([
            0.76 + 0.28 * up - 0.14 * dark_patch,
            0.66 + 0.34 * up - 0.20 * dark_patch,
            0.52 + 0.36 * up - 0.22 * dark_patch,
        ])
        if stage == 3:
            # Pale-pink tender cooked chicken meat exposed right inside the bitten upper ridge (`401:fdchick3`)!
            bite_d = np.hypot((pos[:, 0] - 0.040) / 0.068, (pos[:, 2] - 0.085) / 0.088)
            meat_w = np.clip(1.0 - bite_d, 0.0, 1.0) * np.clip((pos[:, 1] - 0.105) / 0.035, 0.0, 1.0)
            pale = np.array([[0.96, 0.84, 0.80]], dtype=np.float64)
            base = base * (1.0 - 0.85 * meat_w[:, None]) + pale * (0.85 * meat_w[:, None])
        return np.clip(base, 0.34, 1.0)

    meat = tint_part(
        curved_tube(
            spine_pts,
            spine_rads,
            samples_per_seg=8,
            radial_segs=36,
            cap_rings=6,
            name=f"chick{stage}_meat",
            material="item_chicken_crust",
        ).deform(_crispy_drumstick_sculpt),
        _maillard_tint,
    )
    parts: List[MeshPart] = [meat]

    # Subtle authentic overlapping crispy fried-batter crags hugging the EXACT same drumstick surface (zero blocky cubes!).
    golden_ang = 2.39996323
    for k in range(n_flakes):
        u = (k + 0.35) / float(n_flakes)
        fz = z_knots[0] + (z_flake_hi - z_knots[0]) * (u ** 0.86)
        ang = k * golden_ang + 0.42 * math.sin(k * 2.1)
        # Skip placing crust flakes over the exposed pale-meat bite notch in stage 3!
        if stage == 3 and 0.01 < fz < 0.16 and math.sin(ang) > 0.35 and math.cos(ang) > 0.0:
            continue
        cx = float(np.interp(fz, z_knots, x_knots))
        cy = float(np.interp(fz, z_knots, y_knots))
        rx_s = float(np.interp(fz, z_knots, rx_knots))
        ry_s = float(np.interp(fz, z_knots, ry_knots))
        ca, sa = math.cos(ang), math.sin(ang)
        embed = 0.88 + 0.06 * math.sin(k * 3.7)
        fx = cx + rx_s * embed * ca
        fy = max(0.022, cy + ry_s * embed * sa)
        meat_scale = 0.56 + 0.52 * min(1.0, max(0.0, (fz + 0.24) / 0.45))
        fw = (0.030 + 0.018 * (0.5 + 0.5 * math.sin(k * 2.7))) * meat_scale
        fh = (0.011 + 0.008 * (0.5 + 0.5 * math.cos(k * 4.1))) * meat_scale
        fl = (0.036 + 0.022 * (0.5 + 0.5 * math.sin(k * 5.3))) * meat_scale
        flake = (
            superellipsoid(fw, fh, fl, e1=0.45, e2=0.55, u_segs=8, v_segs=6, name=f"chick{stage}_flake_{k}", material="item_chicken_crust")
            .deform(lambda p, s=k: p + 0.006 * np.sin(p * 62.0 + s * 1.7) * np.cos(np.roll(p, 1, axis=1) * 48.0 - s))
            .rotate_xyz(rx=0.40 * math.sin(k * 2.1), ry=0.50 * math.cos(k * 3.3), rz=ang - 0.5 * math.pi + 0.30 * math.sin(k))
            .translate(fx, fy, fz)
        )
        tint_part(
            flake,
            lambda pos, nrm, s=k: np.clip(
                np.column_stack([
                    0.76 + 0.28 * np.clip(0.4 + 0.6 * nrm[:, 1], 0.0, 1.0) + 0.07 * math.sin(s * 1.3),
                    0.64 + 0.34 * np.clip(0.4 + 0.6 * nrm[:, 1], 0.0, 1.0) + 0.06 * math.cos(s * 1.7),
                    0.48 + 0.38 * np.clip(0.4 + 0.6 * nrm[:, 1], 0.0, 1.0),
                ]),
                0.32,
                1.0,
            ),
        )
        parts.append(flake)

    tendon_cuff = tint_part(
        curved_tube(
            [(0.0, 0.082, -0.228), (0.0, 0.081, -0.252), (0.0, 0.080, -0.278), (0.0, 0.079, -0.296)],
            [(0.044, 0.040), (0.034, 0.031), (0.026, 0.024), (0.020, 0.019)],
            samples_per_seg=4,
            radial_segs=18,
            cap_rings=2,
            name=f"chick{stage}_tendon_cuff",
            material="item_chicken_crust",
        ).deform(lambda p: p + 0.005 * np.sin(p * 78.0) * np.cos(np.roll(p, 1, axis=1) * 64.0)),
        lambda pos, nrm: np.clip(
            np.column_stack([
                0.42 + 0.14 * np.clip(nrm[:, 1] + 0.4, 0.0, 1.0),
                0.24 + 0.10 * np.clip(nrm[:, 1] + 0.4, 0.0, 1.0),
                0.15 + 0.07 * np.clip(nrm[:, 1] + 0.4, 0.0, 1.0),
            ]),
            0.22,
            0.64,
        ),
    )
    parts.extend([tendon_cuff, *bone_parts])
    sc.add("chicken", [p.rotate_xyz(ry=136.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.95 if stage <= 2 else (0.92 if stage == 3 else 0.88))


def build_fdchick1() -> SceneBuilder:
    """Authentic crispy Southern fried chicken drumstick (`399:fdchick1`)."""
    return _build_fried_chicken_drumstick(1)


def build_fdsuckr1() -> SceneBuilder:
    """Glossy vivid crimson-red & snow-white swirled peppermint lollipop on crooked paper stick (`256:fdsuckr1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdsuckr1")

    # Plump glossy candy disc with razor-sharp 8-arm red-and-white peppermint pinwheel swirl!
    disc = superellipsoid(0.22, 0.068, 0.22, e1=0.68, e2=0.98, u_segs=64, v_segs=28, name="lollipop_candy", material="item_candy_white").translate(0.0, 0.068, +0.18)

    def _peppermint_swirl(pos: np.ndarray, _nrm: np.ndarray) -> np.ndarray:
        dx = pos[:, 0]
        dz = pos[:, 2] - 0.18
        r = np.hypot(dx, dz)
        ang = np.arctan2(dz, dx)
        swirl = np.sin(8.0 * ang + r * 22.0)
        red_mask = np.clip(swirl * 10.0 + 0.5, 0.0, 1.0)[:, None]
        white = np.array([[0.99, 0.98, 0.97]], dtype=np.float64)
        red = np.array([[0.96, 0.06, 0.08]], dtype=np.float64)
        return white * (1.0 - red_mask) + red * red_mask

    sc.add("lollipop", tint_part(disc, _peppermint_swirl))

    # Slightly bent white/pink-tinted paper lollipop stick pointing down-right matching `256:fdsuckr1`.
    stick = tint_part(
        curved_tube(
            [(0.0, 0.056, +0.02), (0.012, 0.044, -0.14), (-0.018, 0.032, -0.32), (0.014, 0.022, -0.50)],
            [0.016, 0.0155, 0.015, 0.0145],
            samples_per_seg=4,
            radial_segs=14,
            cap_rings=3,
            name="lollipop_stick",
            material="item_candy_white",
        ),
        lambda pos, _n: np.column_stack([
            np.full(len(pos), 0.98),
            0.78 + 0.16 * np.clip((-pos[:, 2]) / 0.50, 0.0, 1.0),
            0.80 + 0.14 * np.clip((-pos[:, 2]) / 0.50, 0.0, 1.0),
        ]),
    )
    sc.add("lollipop", stick)
    # Orient peppermint head upper-left -> crooked paper stick lower-right matching `256:fdsuckr1`!
    sc.nodes["lollipop"] = [p.rotate_xyz(ry=-132.0, degrees=True) for p in sc.nodes["lollipop"]]
    return _finalize_item(sc, target_span=0.95)
