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
    """Carve clean concave crescent tooth-bites into any 3D food mesh and return `(positions, is_bitten_mask)`."""
    p = pos.copy()
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
        scallop = 0.0065 * np.sin(t * 40.0 + p[inside, 1] * 14.0)
        s_wall = np.sqrt(np.maximum(0.0, br * br - t * t)) + scallop
        shift = np.maximum(0.0, s_wall - s)
        p[inside, 0] += dir_x * shift
        p[inside, 2] += dir_z * shift
        bitten[inside] = True
    return p, bitten


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


def _build_tomato_wheel(name: str, radius: float = 0.138, thick: float = 0.026, *, open_pockets: bool = False) -> List[MeshPart]:
    """Build a realistic cross-sectioned red tomato wheel with fleshy outer wall, organic tapered septa & juicy pulp."""
    parts: List[MeshPart] = []
    rim = torus_arc(radius * 0.86, thick * 0.54, u_segs=28, v_segs=10, plane="xz", name=f"{name}_rim", material="item_tomato")
    parts.append(rim)
    hub = ellipsoid(radius * 0.25, thick * 0.46, radius * 0.22, u_segs=16, v_segs=10, name=f"{name}_hub", material="item_tomato_pulp")
    parts.append(hub)
    # 3 soft organic fleshy radial septa separating the tomato seed chambers (`255:fdburgr3` lower-left!)
    for k in range(3):
        ang = 2.0 * math.pi * k / 3.0 + 0.42
        spoke = (
            ellipsoid(radius * 0.44, thick * 0.36, radius * 0.10, u_segs=14, v_segs=8, name=f"{name}_spoke_{k}", material="item_tomato")
            .translate(radius * 0.42, 0.0, 0.0)
            .rotate_xyz(ry=ang)
        )
        parts.append(spoke)
    if open_pockets:
        # Thin glossy moist inner fleshy lip hugging inside the red tomato rind
        inner_lip = torus_arc(radius * 0.74, thick * 0.30, u_segs=24, v_segs=8, plane="xz", name=f"{name}_lip", material="item_tomato_pulp")
        parts.append(inner_lip)
    else:
        pulp = cylinder(radius * 0.84, radius * 0.84, thick * 0.62, radial_segs=24, bevel=0.002, axis="y", name=f"{name}_pulp", material="item_tomato_pulp")
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


def _build_pickle_chip(name: str, radius: float = 0.095) -> MeshPart:
    """Crinkle-cut dark-green dill pickle slice matching `254:fdburgr2` & `255:fdburgr3`."""
    def _crinkle(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        p[:, 1] += 0.0045 * np.sin(p[:, 0] * 78.0)
        return p

    chip = cylinder(radius, radius * 0.96, 0.018, radial_segs=22, bevel=0.003, axis="y", name=name, material="item_pickle").deform(_crinkle)
    return tint_part(
        chip,
        lambda pos, _n: np.column_stack([
            0.70 + 0.30 * np.clip(1.0 - np.hypot(pos[:, 0], pos[:, 2]) / max(1e-4, radius), 0.0, 1.0),
            0.84 + 0.16 * np.clip(1.0 - np.hypot(pos[:, 0], pos[:, 2]) / max(1e-4, radius), 0.0, 1.0),
            0.55 + 0.30 * np.clip(1.0 - np.hypot(pos[:, 0], pos[:, 2]) / max(1e-4, radius), 0.0, 1.0),
        ]),
    )


def _build_burger(stage: int) -> SceneBuilder:
    """Build realistic soft white-bun sesame cheeseburger (`fdburgr1` full, `fdburgr2` bitten, `fdburgr3` remnants)."""
    register_item_materials()
    sc = SceneBuilder(name=f"item_fdburgr{stage}")

    if stage == 3:
        # Final stage (`255:fdburgr3`): flat torn white-bread sesame bun wedge with fluffy white crumb rim,
        # spoked hollow red tomato slice, thin crisp white onion ring, organic crinkled lettuce, dill pickle & torn beef scraps!
        rem_bites = [(+0.15, +0.20, 0.23), (-0.23, +0.18, 0.17)]

        def _dome_rem(pos: np.ndarray) -> np.ndarray:
            p = pos.copy()
            p[:, 1] = np.where(p[:, 1] < 0.0, p[:, 1] * 0.18, p[:, 1])
            return p

        raw_bun = gradient_y_tint(
            superellipsoid(0.28, 0.095, 0.25, e1=0.82, e2=0.96, u_segs=44, v_segs=22, name="bun_remnant", material="item_bun_golden")
            .deform(_dome_rem)
            .translate(0.0, 0.056, -0.06),
            (1.00, 0.96, 0.88),
            (0.98, 0.86, 0.68),
        )
        new_pos, b_mask = _project_bites(raw_bun.positions, rem_bites)
        raw_bun = MeshPart(raw_bun.name, raw_bun.material, new_pos, recompute_smooth_normals(new_pos, raw_bun.indices), raw_bun.indices, uvs=raw_bun.uvs)
        raw_bun.colors = None
        gradient_y_tint(raw_bun, (1.00, 0.96, 0.88), (0.98, 0.86, 0.68))
        for bp in _split_mesh_by_mask(raw_bun, b_mask, "item_bun_crumb", "_white_crumb"):
            sc.add("burger", bp.rotate_xyz(rx=0.22, ry=-0.10).translate(+0.02, 0.012, -0.05))

        # Subtle golden sesame seeds resting flush on the tilted surviving bun crust!
        bun_top_y = float(max(p.positions[:, 1].max() for p in sc.all_parts()))
        for k, (sx, sz) in enumerate(((-0.12, -0.20), (-0.02, -0.24), (+0.09, -0.21), (+0.16, -0.15), (-0.07, -0.13), (+0.03, -0.14), (-0.17, -0.13), (+0.11, -0.10))):
            r2 = (sx - 0.02) ** 2 + (sz + 0.11) ** 2
            sy = bun_top_y - 0.004 - 0.52 * r2 - 0.18 * (sz + 0.16)
            sc.add("burger", ellipsoid(0.0085, 0.0045, 0.015, u_segs=8, v_segs=6, name=f"rem_sesame_{k}", material="item_sesame").rotate_xyz(rx=0.22, ry=k * 0.9).translate(sx, sy, sz))

        # Irregular craggy seared beef patty scraps tucked underneath the lettuce & onion (`255:fdburgr3`).
        for idx, (cx, cz, cr) in enumerate(((-0.02, +0.04, 0.088), (+0.11, +0.10, 0.068), (-0.11, +0.02, 0.062))):
            scrap = (
                superellipsoid(cr, cr * 0.30, cr * 0.86, e1=0.62, e2=0.88, u_segs=16, v_segs=10, name=f"patty_crumb_{idx}", material="item_beef_patty")
                .deform(lambda p, k=idx: p + 0.011 * np.sin(p * 42.0 + k * 1.7) * np.cos(np.roll(p, 1, axis=1) * 34.0))
                .translate(cx, cr * 0.28 + 0.004, cz)
            )
            sc.add("burger", scrap)

        # Organic crinkled pale-veined lettuce leaves lying naturally across the remnants!
        for k, (lx, ly, lz, rot_y, sc_w, sc_l, sd) in enumerate((
            (-0.04, 0.034, +0.02, 0.65, 0.14, 0.22, 0.4),
            (+0.04, 0.038, +0.05, 2.25, 0.13, 0.20, 1.8),
            (-0.08, 0.030, +0.08, -1.15, 0.13, 0.20, 3.1),
            (+0.10, 0.034, -0.02, 1.30, 0.12, 0.18, 4.4),
        )):
            sc.add("burger", _build_crisp_lettuce_sheet(f"lettuce_rem_{k}", width=sc_w, length=sc_l, bend_down=0.012, seed=sd).rotate_xyz(ry=rot_y).translate(lx, ly, lz))

        # Real organic spoked hollowed red tomato wheel slice bottom-left matching `255:fdburgr3`!
        for tp in _build_tomato_wheel("tomato_rem", radius=0.140, thick=0.022, open_pockets=True):
            sc.add("burger", tp.rotate_xyz(rx=0.14, rz=-0.16).translate(-0.17, 0.044, +0.10))

        # Slender crisp white sliced onion ring resting casually across front-center (`255:fdburgr3`!)
        onion_a = torus_arc(0.118, 0.0090, u_segs=32, v_segs=10, plane="xz", name="onion_rem_a", material="item_onion").scale(0.86, 1.0, 1.16).rotate_xyz(rx=0.14, rz=-0.10).translate(+0.02, 0.056, +0.13)
        onion_b = torus_arc(0.072, 0.0075, u_segs=26, v_segs=10, plane="xz", name="onion_rem_b", material="item_onion").rotate_xyz(rx=0.10, rz=0.14).translate(+0.04, 0.048, +0.11)
        sc.add("burger", [onion_a, onion_b])

        # Crinkle-cut dark-green dill pickle chip lying lower-right (`255:fdburgr3` lower-right green curl!).
        sc.add("burger", _build_pickle_chip("pickle_rem", radius=0.088).rotate_xyz(rx=0.10, rz=0.14).translate(+0.22, 0.026, +0.11))
        return _finalize_item(sc, target_span=0.92)

    # Authentic bite placement matching `254:fdburgr2`: crisp top-left nibble + big lower-right double crescent bites!
    bites = [(-0.27, -0.27, 0.16), (+0.23, +0.20, 0.25), (+0.30, -0.02, 0.18)] if stage == 2 else []

    def _carve_filling(part: MeshPart) -> MeshPart:
        if not bites:
            return part
        p_new, _ = _project_bites(part.positions, bites)
        out = MeshPart(part.name, part.material, p_new, recompute_smooth_normals(p_new, part.indices), part.indices, uvs=part.uvs)
        if part.colors is not None:
            out.colors = part.colors.copy()
            out._base_tint = part.colors.copy()
        return out

    def _is_inside_any_bite(x: float, z: float, margin: float = 0.0) -> bool:
        return any(math.hypot(x - bx, z - bz) < (br + margin) for bx, bz, br in bites)

    # 1. Soft white-bread bottom bun heel (clean vertical separation from patty so bitten face has zero z-fighting!).
    bot_bun_raw = gradient_y_tint(
        superellipsoid(0.312, 0.058, 0.312, e1=0.60, e2=0.96, u_segs=44, v_segs=18, name="bun_bottom", material="item_bun_golden")
        .translate(0.0, 0.056, 0.0),
        (0.95, 0.84, 0.66),
        (1.00, 0.98, 0.92),
    )
    if bites:
        bp_pos, bp_mask = _project_bites(bot_bun_raw.positions, bites)
        bot_bun_raw = MeshPart(bot_bun_raw.name, bot_bun_raw.material, bp_pos, recompute_smooth_normals(bp_pos, bot_bun_raw.indices), bot_bun_raw.indices, uvs=bot_bun_raw.uvs)
        gradient_y_tint(bot_bun_raw, (0.95, 0.84, 0.66), (1.00, 0.98, 0.92))
        sc.add("burger", _split_mesh_by_mask(bot_bun_raw, bp_mask, "item_bun_crumb", "_white_crumb"))
    else:
        sc.add("burger", bot_bun_raw)

    # 2. Thick char-seared dimpled beef patty sitting cleanly above bottom bun (`y ∈ [0.114, 0.214]`, zero overlap!).
    patty_bites = [(bx, bz, br + 0.006) for bx, bz, br in bites]
    patty_raw = tint_part(
        superellipsoid(0.318, 0.050, 0.318, e1=0.58, e2=0.98, u_segs=42, v_segs=16, name="beef_patty", material="item_beef_patty")
        .deform(lambda p: p + 0.007 * np.sin(p * 36.0) * np.cos(np.roll(p, 1, axis=1) * 32.0))
        .translate(0.0, 0.160, 0.0),
        lambda pos, _n: np.clip(0.84 + 0.16 * np.sin(pos[:, 0] * 28.0 + pos[:, 2] * 24.0)[:, None], 0.65, 1.0) * np.ones((1, 3)),
    )
    if patty_bites:
        pp_pos, _ = _project_bites(patty_raw.positions, patty_bites)
        patty = MeshPart(patty_raw.name, patty_raw.material, pp_pos, recompute_smooth_normals(pp_pos, patty_raw.indices), patty_raw.indices, uvs=patty_raw.uvs)
        patty.colors = patty_raw.colors.copy()
        patty._base_tint = patty_raw.colors.copy()
    else:
        patty = patty_raw
    sc.add("burger", patty)

    # 3. Softly melted cheddar cheese slice hugging warmly over the patty edge.
    def _drape_cheese(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        r = np.hypot(p[:, 0], p[:, 2])
        over = np.clip((r - 0.25) / 0.13, 0.0, 1.0)
        p[:, 1] -= 0.044 * (over ** 1.55)
        return p

    cheese = _carve_filling(
        rounded_box(0.54, 0.014, 0.54, bevel=0.005, segs=3, name="cheese_slice", material="item_cheese")
        .rotate_xyz(ry=28.0, degrees=True)
        .deform(_drape_cheese)
        .translate(0.0, 0.205, 0.0)
    )
    sc.add("burger", cheese)

    # 4. Juicy red sliced tomato wheel + crisp delicate white onion ring peeking warmly on lower-left (`253:fdburgr1` / `254:fdburgr2`)!
    for idx, (tx, tz) in enumerate(((-0.14, +0.20), (+0.11, +0.16), (-0.05, -0.16))):
        if stage == 2 and _is_inside_any_bite(tx, tz, margin=-0.04):
            continue
        for tp in _build_tomato_wheel(f"tomato_{idx}", radius=0.145, thick=0.025, open_pockets=False):
            sc.add("burger", _carve_filling(tp.translate(tx, 0.222, tz)))

    onion_a = _carve_filling(
        torus_arc(0.138, 0.0095, u_segs=32, v_segs=10, plane="xz", name="burger_onion_a", material="item_onion")
        .translate(-0.08, 0.236, +0.20)
    )
    onion_b = _carve_filling(
        torus_arc(0.102, 0.0085, u_segs=28, v_segs=10, plane="xz", name="burger_onion_b", material="item_onion")
        .translate(-0.08, 0.236, +0.20)
    )
    sc.add("burger", [onion_a, onion_b])

    # Crisp ridged dark-green dill pickle slice nestled cleanly inside the bitten edge on Stage 2 (`254:fdburgr2`!)
    if stage == 2:
        sc.add("burger", _carve_filling(_build_pickle_chip("burger_pickle_bite", radius=0.092).rotate_xyz(rx=0.05, rz=-0.06).translate(+0.02, 0.238, +0.10)))

    # 5. Organic crinkled iceberg/green-leaf lettuce leaves with pale crisp ribs & curly ruffled edges (ZERO scallop fans!).
    lettuce_placements = (
        (0.28, 0.16, 0.235, 0.17, 0.22, 0.036, 0.4),
        (1.22, 0.15, 0.240, 0.16, 0.21, 0.032, 1.7),
        (2.18, 0.16, 0.236, 0.17, 0.22, 0.038, 3.0),
        (3.25, 0.15, 0.240, 0.16, 0.20, 0.030, 4.3),
        (4.35, 0.16, 0.236, 0.17, 0.21, 0.034, 5.5),
        (5.38, 0.16, 0.238, 0.17, 0.22, 0.036, 6.8),
    )
    for k, (ang, rad, ly, lw, ll, bd, sd) in enumerate(lettuce_placements):
        lx, lz = rad * math.sin(ang), rad * math.cos(ang)
        if stage == 2 and _is_inside_any_bite(lx * 1.35, lz * 1.35, margin=-0.03):
            continue
        leaf = (
            _build_crisp_lettuce_sheet(f"lettuce_{k}", width=lw, length=ll, bend_down=bd, seed=sd)
            .rotate_xyz(ry=ang)
            .translate(lx, ly, lz)
        )
        sc.add("burger", _carve_filling(leaf))

    # 6. Soft pillowy smooth light-golden WHITE-BREAD sesame top bun (with pale creamy-white lower skirt,
    #    subtle natural diagonal crown crease like `253:fdburgr1`, and REAL fluffy white-bread bitten cross-section!).
    def _dome_white_bun(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        p[:, 1] = np.where(p[:, 1] < 0.0, p[:, 1] * 0.20, p[:, 1])
        # Subtle organic pillowy dimple crease across upper-left crown matching `fdburgr1` & `fdburgr2`!
        u_diag = (p[:, 0] + p[:, 2]) * 0.707
        v_diag = (p[:, 0] - p[:, 2]) * 0.707
        crease = -0.012 * np.exp(-((v_diag / 0.055) ** 2 + ((u_diag + 0.03) / 0.18) ** 2)) * np.clip(p[:, 1] / 0.12, 0.0, 1.0)
        p[:, 1] += crease
        return p

    top_bun_raw = gradient_y_tint(
        superellipsoid(0.328, 0.172, 0.328, e1=0.84, e2=0.96, u_segs=52, v_segs=26, name="bun_top", material="item_bun_golden")
        .deform(_dome_white_bun)
        .translate(0.0, 0.268, 0.0),
        (1.00, 0.97, 0.90),  # classic pale creamy-white lower white-bread bun rim!
        (0.95, 0.84, 0.68),  # soft warm light-golden baked top (zero dark wheat look!)
    )
    if bites:
        tb_pos, tb_mask = _project_bites(top_bun_raw.positions, bites)
        top_bun_raw = MeshPart(top_bun_raw.name, top_bun_raw.material, tb_pos, recompute_smooth_normals(tb_pos, top_bun_raw.indices), top_bun_raw.indices, uvs=top_bun_raw.uvs)
        gradient_y_tint(top_bun_raw, (1.00, 0.97, 0.90), (0.95, 0.84, 0.68))
        sc.add("burger", _split_mesh_by_mask(top_bun_raw, tb_mask, "item_bun_crumb", "_white_crumb"))
    else:
        sc.add("burger", top_bun_raw)

    # 7. Delicate pale golden-ivory sesame seeds nestled half-flush into the pillowy white-bread dome!
    golden_angle = 2.39996323
    for i in range(38):
        f = (i + 0.5) / 38.0
        r_frac = 0.82 * math.sqrt(f)
        th = i * golden_angle
        sx = 0.318 * r_frac * math.cos(th)
        sz = 0.318 * r_frac * math.sin(th)
        if stage == 2 and _is_inside_any_bite(sx, sz, margin=0.025):
            continue
        sy = 0.268 + 0.172 * ((max(0.01, 1.0 - (r_frac ** (2.0 / 0.84)))) ** (0.84 * 0.5)) + 0.0015
        seed = (
            ellipsoid(0.0085, 0.0042, 0.0155, u_segs=8, v_segs=6, name=f"sesame_{i}", material="item_sesame")
            .rotate_xyz(rx=0.52 * r_frac * math.sin(th), ry=th + 0.8 * i, rz=-0.52 * r_frac * math.cos(th))
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


def build_fdcornd1() -> SceneBuilder:
    """Golden fried cornmeal corn dog with slanted bitten crust & a visible chomped bite in the hot dog itself (`361:fdcornd1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdcornd1")

    # Plump deep-fried golden cornmeal batter cylinder with craggy crispy bubbles + slanted open bitten front hood!
    def _fry_and_bite_crust(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        bump = (
            0.010 * np.sin(p[:, 0] * 38.0 + p[:, 2] * 34.0) * np.cos(p[:, 1] * 36.0)
            + 0.006 * np.sin(p[:, 2] * 58.0 - p[:, 0] * 42.0)
            + 0.004 * np.cos(p[:, 1] * 64.0 + p[:, 2] * 52.0)
        )
        r = np.maximum(1e-5, np.hypot(p[:, 0], p[:, 1] - 0.128))
        p[:, 0] += bump * (p[:, 0] / r)
        p[:, 1] += bump * ((p[:, 1] - 0.128) / r)
        # Slanted bite opening across the front (+Z) end of the fried crust exposing the protruding hot dog!
        scallop = 0.012 * np.cos(p[:, 0] * 26.0)
        z_bite_limit = 0.255 - 0.32 * (p[:, 1] - 0.128) + scallop
        p[:, 2] = np.minimum(p[:, 2], z_bite_limit)
        return p

    batter = gradient_y_tint(
        curved_tube(
            [(0.0, 0.125, -0.24), (0.0, 0.128, -0.06), (0.0, 0.128, +0.16), (0.0, 0.126, +0.34)],
            [(0.066, 0.064), (0.118, 0.112), (0.118, 0.112), (0.106, 0.102)],
            samples_per_seg=7,
            radial_segs=30,
            cap_rings=5,
            name="corndog_batter",
            material="item_corndog_crust",
        ).deform(_fry_and_bite_crust),
        (0.74, 0.62, 0.46),
        (1.06, 0.96, 0.78),
        noise_amp=0.07,
    )

    # Slim HOLLOW pale-yellow spongy cornmeal cross-section lip ringing the batter mouth (keeps the hot dog 100% exposed!)
    crumb_ring = (
        torus_arc(0.084, 0.022, u_segs=32, v_segs=12, plane="xy", name="corndog_crumb_ring", material="item_bun_crumb")
        .deform(lambda p: p + 0.004 * np.sin(p * 52.0))
        .rotate_xyz(rx=0.30)
        .translate(0.0, 0.128, +0.246)
    )

    # Prominent protruding pink-brown hot dog frankfurter WITH A HUGE UNMISTAKABLE CURVED BITE CHOMPED OUT OF ITS TIP!
    def _bite_hotdog(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        # Carve a deep bold U-shaped semicircular tooth-bite directly out of the upper-front end of the frankfurter!
        # Center of bite arc sits at (x=0, z=+0.46), biting cleanly back to z=+0.35 in the center while leaving rounded
        # casing cheeks on left/right and an angled bottom lip just like a real human bite out of a hot dog!
        dx = p[:, 0]
        in_front = p[:, 2] > 0.28
        u_bite = 0.048 * np.exp(-((dx / 0.048) ** 2))
        z_cut = 0.422 - 0.52 * (p[:, 1] - 0.120) - u_bite
        p[:, 2] = np.where(in_front, np.minimum(p[:, 2], z_cut), p[:, 2])
        # Also scoop slightly down into the top crown of the frankfurter where the upper teeth bit down!
        top_scoop = 0.028 * np.exp(-((dx / 0.046) ** 2)) * np.clip((p[:, 2] - 0.30) / 0.10, 0.0, 1.0)
        p[:, 1] = np.where(p[:, 1] > 0.125, p[:, 1] - top_scoop, p[:, 1])
        return p

    hotdog = gradient_y_tint(
        curved_tube(
            [(0.0, 0.128, +0.16), (0.0, 0.130, +0.30), (0.0, 0.132, +0.445)],
            [(0.068, 0.066), (0.070, 0.068), (0.066, 0.064)],
            samples_per_seg=8,
            radial_segs=30,
            cap_rings=6,
            name="corndog_hotdog",
            material="item_hotdog_pink",
        ).deform(_bite_hotdog),
        (0.76, 0.64, 0.56),
        (1.04, 0.94, 0.86),
        noise_amp=0.05,
    )

    # Bright pale-pink concave bitten inner meat cross-section face seated right inside the chomped bite of the hot dog!
    def _cut_meat_scoop(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        p[:, 2] -= 0.026 * np.exp(-((p[:, 0] / 0.046) ** 2))
        return p

    hotdog_bite_face = (
        cylinder(0.060, 0.058, 0.016, radial_segs=24, bevel=0.004, axis="z", name="corndog_hotdog_bite", material="item_hotdog_inside")
        .deform(_cut_meat_scoop)
        .rotate_xyz(rx=0.66)
        .translate(0.0, 0.140, +0.382)
    )

    # Textured wooden skewer stick protruding cleanly from the rear end.
    stick = (
        cylinder(0.017, 0.015, 0.36, radial_segs=14, bevel=0.004, axis="z", name="corndog_stick", material="item_wood_stick")
        .translate(0.0, 0.115, -0.37)
    )

    # Thick plump bright yellow squeezed mustard zigzag ribbon draped across the fried batter behind the bite!
    mustard_pts: List[Tuple[float, float, float]] = []
    n_m = 32
    for i in range(n_m + 1):
        u = i / float(n_m)
        mz = -0.18 + 0.37 * u
        mx = 0.060 * math.sin(u * math.pi * 5.8)
        r_surf = 0.116 * math.sqrt(max(0.18, 1.0 - ((mx / 0.120) ** 2)))
        end_w = math.sin(u * math.pi) ** 0.22
        my = 0.128 + r_surf * (0.86 + 0.14 * end_w) + 0.010
        mustard_pts.append((mx, my, mz))
    mustard = curved_tube(
        mustard_pts,
        [0.0155] * len(mustard_pts),
        samples_per_seg=2,
        radial_segs=12,
        cap_rings=2,
        name="corndog_mustard",
        material="item_mustard",
    )
    sc.add("corndog", [batter, crumb_ring, hotdog, hotdog_bite_face, stick, mustard])
    # Angle top-left (bitten hot dog tip) -> bottom-right (wooden stick) matching `361:fdcornd1`!
    sc.nodes["corndog"] = [p.rotate_xyz(ry=-135.0, degrees=True) for p in sc.nodes["corndog"]]
    return _finalize_item(sc, target_span=0.96)


def build_fdjelo1() -> SceneBuilder:
    """Translucent ruby-red fluted molded Jell-O dome with visible banana coins & gentle wobble clip (`366:fdjelo1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdjelo1")

    # Classic deep 12-fluted scalloped gelatin Bundt mold dome!
    prof = [
        (0.000, 0.000),
        (0.325, 0.000),
        (0.334, 0.042),
        (0.308, 0.140),
        (0.262, 0.265),
        (0.200, 0.352),
        (0.136, 0.388),
        (0.060, 0.384),
        (0.000, 0.370),
    ]

    def _flute_mold(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ang = np.arctan2(p[:, 2], p[:, 0])
        h_frac = np.clip(p[:, 1] / 0.388, 0.0, 1.0)
        flute = 1.0 + 0.165 * np.cos(ang * 12.0) * np.sin(h_frac * math.pi * 0.94)
        p[:, 0] *= flute
        p[:, 2] *= flute
        return p

    # Chunky creamy banana coin slices pressed close to the translucent outer shell (with delicate seed center)!
    banana_specs = (
        (+0.18, 0.125, +0.16, +0.75, -0.45),
        (-0.18, 0.120, +0.15, +0.72, +0.48),
        (+0.02, 0.115, +0.23, +0.92, +0.08),
        (+0.16, 0.240, +0.08, +0.48, -0.58),
        (-0.15, 0.245, +0.09, +0.46, +0.55),
        (+0.00, 0.290, +0.11, +0.38, -0.05),
        (+0.19, 0.145, -0.14, -0.68, -0.50),
        (-0.19, 0.150, -0.13, -0.65, +0.52),
        (+0.02, 0.220, -0.18, -0.55, +0.10),
        (-0.04, 0.310, -0.06, -0.22, +0.18),
    )
    for idx, (bx, by, bz, rx, rz) in enumerate(banana_specs):
        sl = (
            cylinder(0.064, 0.064, 0.022, radial_segs=18, bevel=0.005, axis="y", name=f"jello_banana_{idx}", material="item_banana_slice")
            .rotate_xyz(rx=rx, rz=rz)
            .translate(bx, by, bz)
        )
        # Delicate tan banana pith dot in the center (subtle realistic detail, not a dark ring!)
        core = (
            cylinder(0.008, 0.008, 0.024, radial_segs=10, axis="y", name=f"jello_banana_core_{idx}", material="item_wood_stick")
            .rotate_xyz(rx=rx, rz=rz)
            .translate(bx, by, bz)
        )
        sc.add("jello_body", [sl, core])

    jelly_dome = lathe(prof, radial_segs=72, cap_bottom=False, cap_top=False, axis="y", name="jello_dome", material="item_jello_ruby").deform(_flute_mold)
    sc.add("jello_body", jelly_dome)

    # Subtle authentic 1998 Jell-O wobble idle animation (7-frame elastic squash/shear jiggle!).
    times = [0.00, 0.12, 0.24, 0.36, 0.48, 0.60, 0.72]
    scales = [
        (1.00, 1.00, 1.00),
        (1.045, 0.94, 1.035),
        (0.965, 1.055, 0.975),
        (1.030, 0.965, 1.025),
        (0.980, 1.030, 0.985),
        (1.012, 0.990, 1.010),
        (1.00, 1.00, 1.00),
    ]
    rots = [
        quat_from_euler_xyz(rz=a, rx=b)
        for a, b in ((0.0, 0.0), (0.045, 0.02), (-0.042, -0.018), (0.028, 0.014), (-0.020, -0.010), (0.010, 0.005), (0.0, 0.0))
    ]
    sc.add_animation(
        "Idle",
        [
            AnimationChannel("jello_body", "scale", times, scales),
            AnimationChannel("jello_body", "rotation", times, rots),
        ],
    )
    return _finalize_item(sc, target_span=0.88)


def build_fdchick1() -> SceneBuilder:
    """Crispy golden-amber fried chicken drumstick with craggy fried breading & exposed bone (`399:fdchick1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdchick1")

    def _crispy_breading(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        r = np.maximum(1e-5, np.hypot(p[:, 0], p[:, 1]))
        crag = (
            0.020 * np.sin(p[:, 0] * 32.0 + p[:, 2] * 28.0) * np.cos(p[:, 1] * 34.0)
            + 0.014 * np.sin(p[:, 2] * 54.0 - p[:, 1] * 46.0)
            + 0.009 * np.cos(p[:, 0] * 68.0 + p[:, 2] * 60.0)
        )
        p[:, 0] += crag * (p[:, 0] / r)
        p[:, 1] += crag * (p[:, 1] / r)
        return p

    meat = tint_part(
        curved_tube(
            [(0.0, 0.11, -0.18), (0.0, 0.13, -0.04), (0.0, 0.15, +0.14), (0.0, 0.14, +0.30), (0.0, 0.12, +0.38)],
            [(0.044, 0.042), (0.090, 0.084), (0.154, 0.138), (0.136, 0.122), (0.058, 0.054)],
            samples_per_seg=6,
            radial_segs=28,
            cap_rings=5,
            name="chicken_meat",
            material="item_chicken_crust",
        ).deform(_crispy_breading),
        lambda pos, nrm: np.clip(
            np.column_stack([
                0.86 + 0.14 * np.clip(nrm[:, 1], 0.0, 1.0) + 0.12 * np.sin(pos[:, 2] * 32.0 + pos[:, 0] * 36.0),
                0.68 + 0.24 * np.clip(nrm[:, 1], 0.0, 1.0) + 0.10 * np.sin(pos[:, 2] * 32.0 + pos[:, 0] * 36.0),
                0.44 + 0.22 * np.clip(nrm[:, 1], 0.0, 1.0),
            ]),
            0.38,
            1.0,
        ),
    )
    bone_shaft = cylinder(0.025, 0.031, 0.22, radial_segs=14, bevel=0.004, axis="z", name="chicken_bone", material="item_bone_white").translate(0.0, 0.105, -0.25)
    knuckle_l = ellipsoid(0.032, 0.030, 0.034, u_segs=14, v_segs=10, name="chicken_knuckle_l", material="item_bone_white").translate(+0.022, 0.105, -0.355)
    knuckle_r = ellipsoid(0.030, 0.028, 0.034, u_segs=14, v_segs=10, name="chicken_knuckle_r", material="item_bone_white").translate(-0.022, 0.105, -0.355)
    sc.add("chicken", [meat, bone_shaft, knuckle_l, knuckle_r])
    # Orient bone bottom-left -> plump golden drumstick top-right matching `399:fdchick1`!
    sc.nodes["chicken"] = [p.rotate_xyz(ry=136.0, degrees=True) for p in sc.nodes["chicken"]]
    return _finalize_item(sc, target_span=0.95)


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
