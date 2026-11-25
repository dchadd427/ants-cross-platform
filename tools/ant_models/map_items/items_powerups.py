"""Authentic 3D sculptors for all 5 1998 Ants class power-up pickups (`62`–`66`).

Each pickup reuses or directly mirrors the real 3D class gear worn by its corresponding ant
class, posed on the ground to match the 1998 power-up sprites 1:1:
- `pu_comb`  (62): Combat Ant's crossed golden-yellow tactical bullet bandolier X-harness
- `pu_thief` (63): Thief Ant's hollow woven-gold domino bandana mask loop with smooth hemmed eye holes & broad ribbon tails
- `pu_bomb`  (64): Bomber Ant's twin riveted yellow tin-can aviator goggles, upright open leather strap & sizzling dynamite
- `pu_swim`  (65): Swimmer Ant's chunky diagonal yellow oval scuba mask, tall open rubber strap, corrugated hose & horizontal snorkel
- `pu_mason` (66): Fire Ant's exact deeply-fluted yellow vintage firefighter helmet with crimson 'A' shield
"""
from __future__ import annotations

import math
from typing import Dict, List, Sequence, Tuple

try:
    import numpy as np
except ImportError:  # pragma: no cover - pure-stdlib import safety for CI
    np = None  # type: ignore

if np is not None:
    try:
        from ..ant_bomber import _build_aviator_cup
        from ..ant_fire import (
            _HAT_MAT as _FIRE_HAT_MAT,
            _build_giant_letter_a,
            _build_option3_wide_shield,
            _helmet_shell_deform_bounds,
            _spow_scalar,
        )
        from ..ant_swimmer import _hollow_oval_housing
        from ..mesh_core import (
            AnimationChannel,
            MeshPart,
            SceneBuilder,
            catmull_rom_spline,
            cone,
            curved_tube,
            cylinder,
            ellipsoid,
            hemisphere_shell,
            quat_from_euler_xyz,
            recompute_smooth_normals,
            rounded_box,
            sphere,
            superellipsoid,
            swept_tube,
            torus_arc,
        )
        from .item_materials import register_item_materials
        from .items_toys import _finalize_item
    except ImportError:
        from ant_bomber import _build_aviator_cup  # type: ignore
        from ant_fire import (  # type: ignore
            _HAT_MAT as _FIRE_HAT_MAT,
            _build_giant_letter_a,
            _build_option3_wide_shield,
            _helmet_shell_deform_bounds,
            _spow_scalar,
        )
        from ant_swimmer import _hollow_oval_housing  # type: ignore
        from map_items.item_materials import register_item_materials  # type: ignore
        from map_items.items_toys import _finalize_item  # type: ignore
        from mesh_core import (  # type: ignore
            AnimationChannel,
            MeshPart,
            SceneBuilder,
            catmull_rom_spline,
            cone,
            curved_tube,
            cylinder,
            ellipsoid,
            hemisphere_shell,
            quat_from_euler_xyz,
            recompute_smooth_normals,
            rounded_box,
            sphere,
            superellipsoid,
            swept_tube,
            torus_arc,
        )


# ---------------------------------------------------------------------------
# 62:pu_comb — Combat Ant's Crossed Golden Tactical Bullet Bandoliers
# ---------------------------------------------------------------------------

def _bandolier_pouch(name: str) -> List[MeshPart]:
    """Chunky bright golden tactical ammo pouch + brass buckle flap matching Combat Ant (`ant_combat`)."""
    body = rounded_box(0.092, 0.056, 0.168, bevel=0.014, segs=2, name=f"{name}_box", material="gear_gold")
    flap = rounded_box(0.098, 0.022, 0.150, bevel=0.008, segs=2, name=f"{name}_flap", material="gear_yellow_plastic").translate(0.0, 0.027, 0.0)
    strap = rounded_box(0.026, 0.060, 0.174, bevel=0.006, segs=1, name=f"{name}_strap", material="gear_gold_metal")
    clasp = sphere(0.015, u_segs=10, v_segs=8, name=f"{name}_snap", material="gear_gold_metal").translate(0.0, 0.036, 0.044)
    return [body, flap, strap, clasp]


def build_pu_comb() -> SceneBuilder:
    """Combat Ant Power-Up: draped crossed golden-yellow tactical bullet bandolier X-harness (`62:pu_comb`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_pu_comb")

    # Two broad, gently arched golden-yellow cloth-and-leather bandolier straps crossed in a bold X (`pu_comb_4x`).
    for ang_deg, y_lift, tag in ((+39.0, 0.000, "a"), (-39.0, 0.046, "b")):
        parts: List[MeshPart] = []

        def _arch_belt(pos: np.ndarray, lift: float = y_lift) -> np.ndarray:
            p = pos.copy()
            u = np.clip(p[:, 0] / 0.44, -1.0, 1.0)
            arch_y = 0.090 * (1.0 - u * u)
            # Subtle flared swallow-tail cut at belt ends matching `pu_comb_4x` silhouette!
            end_w = np.clip((np.abs(u) - 0.72) / 0.28, 0.0, 1.0)
            p[:, 2] *= 1.0 + 0.16 * end_w
            p[:, 1] += arch_y + lift
            return p

        # Dark mahogany leather under-backing peeking slightly at the X-notches (`pu_comb_4x`!).
        backing = (
            rounded_box(0.84, 0.028, 0.242, bevel=0.010, segs=3, name=f"comb_leather_{tag}", material="item_twig_bark")
            .deform(_arch_belt)
            .translate(0.0, 0.026, 0.0)
        )
        # Main broad woven golden-yellow bandolier strap.
        strap = (
            rounded_box(0.89, 0.048, 0.222, bevel=0.015, segs=3, name=f"comb_strap_{tag}", material="gear_gold")
            .deform(_arch_belt)
            .translate(0.0, 0.054, 0.0)
        )
        # Soft stitched golden hem piping tucked flush along both outer strap edges.
        for sz, etag in ((-0.104, "n"), (+0.104, "p")):
            edge_pts = [
                (-0.42 + 0.84 * (k / 10.0), 0.068 + y_lift + 0.090 * (1.0 - ((-0.42 + 0.84 * (k / 10.0)) / 0.44) ** 2), sz)
                for k in range(11)
            ]
            parts.append(curved_tube(edge_pts, [0.011] * len(edge_pts), samples_per_seg=2, radial_segs=8, name=f"comb_piping_{tag}_{etag}", material="gear_gold"))
        parts.extend([backing, strap])

        # Row of chunky bright-golden ammo pouches strapped along each arm of the X!
        for k, bx in enumerate((-0.34, -0.22, -0.10, +0.10, +0.22, +0.34)):
            u = bx / 0.44
            by = 0.098 + y_lift + 0.090 * (1.0 - u * u)
            slope_rz = -math.atan2(0.180 * u / 0.44, 1.0)
            for p_part in _bandolier_pouch(f"comb_pouch_{tag}_{k}"):
                parts.append(p_part.rotate_xyz(rz=slope_rz).translate(bx, by, 0.0))

        for part in parts:
            sc.add("pu_comb", part.rotate_xyz(ry=ang_deg, degrees=True).rotate_xyz(rx=10.0, degrees=True))

    # Combat Ant's iconic diamond golden X-junction clasp anchoring the center crossing!
    center_y = 0.236
    clasp_parts = [
        rounded_box(0.200, 0.056, 0.200, bevel=0.016, segs=2, name="comb_x_clasp", material="gear_gold").rotate_xyz(ry=45.0, degrees=True).translate(0.0, center_y, 0.0),
        rounded_box(0.130, 0.062, 0.130, bevel=0.010, segs=2, name="comb_x_recess", material="gear_gold_metal").rotate_xyz(ry=45.0, degrees=True).translate(0.0, center_y + 0.004, 0.0),
        superellipsoid(0.050, 0.026, 0.050, e1=0.72, e2=0.72, u_segs=16, v_segs=10, name="comb_x_boss", material="gear_yellow_plastic").translate(0.0, center_y + 0.028, 0.0),
    ]
    for cp in clasp_parts:
        sc.add("pu_comb", cp.rotate_xyz(rx=10.0, degrees=True))

    return _finalize_item(sc, target_span=0.94)


# ---------------------------------------------------------------------------
# 63:pu_thief — Thief Ant's Hollow Woven-Gold Cloth Domino Mask & Ribbon Tails
# ---------------------------------------------------------------------------

def _build_hollow_domino_band(name: str) -> List[MeshPart]:
    """Watertight hollow golden-yellow cloth headband loop with TWO smooth-contoured hemmed oval eye holes."""
    n_u = 96
    n_v = 22
    r_x, r_z = 0.258, 0.230
    thick = 0.026
    eye_centers = (-0.42, +0.42)
    eye_y0 = -0.008
    eye_au = 0.210
    eye_by = 0.048

    def _band_v_bounds(u: float) -> Tuple[float, float]:
        a = abs(math.atan2(math.sin(u), math.cos(u)))
        top = 0.098 + 0.040 * math.exp(-((a / 1.15) ** 2))
        cheek_dip = -0.062 * math.exp(-(((a - 0.44) / 0.30) ** 2))
        nose_dip = -0.026 * math.exp(-((a / 0.22) ** 2))
        bot = -0.072 + cheek_dip + nose_dip
        return bot, top

    def _band_surface_pts(u_val: float, y_val: float, fv_val: float) -> Tuple[Tuple[float, float, float], Tuple[float, float, float]]:
        du_knot = math.atan2(math.sin(u_val - 1.92), math.cos(u_val - 1.92))
        cinch = 1.0 - 0.14 * math.exp(-((du_knot / 0.44) ** 2))
        cx, cz = r_x * math.sin(u_val), r_z * math.cos(u_val)
        nx, nz = math.sin(u_val) / r_x, math.cos(u_val) / r_z
        nlen = math.hypot(nx, nz)
        nx, nz = nx / nlen, nz / nlen
        fold = 0.004 * math.sin(fv_val * 3.0 * math.pi + u_val * 2.0)
        r_flare = 0.008 * math.sin(fv_val * math.pi)
        y_c = y_val * cinch
        p_out = (cx + nx * (0.5 * thick + fold + r_flare), y_c, cz + nz * (0.5 * thick + fold + r_flare))
        p_in = (cx - nx * (0.5 * thick - fold * 0.5), y_c, cz - nz * (0.5 * thick - fold * 0.5))
        return p_out, p_in

    def _eye_dist(u: float, y: float) -> Tuple[float, float]:
        uw = math.atan2(math.sin(u), math.cos(u))
        best_d = 999.0
        best_eu = eye_centers[0]
        for eu in eye_centers:
            d = math.hypot((uw - eu) / eye_au, (y - eye_y0) / eye_by)
            if d < best_d:
                best_d, best_eu = d, eu
        return best_d, best_eu

    us = np.linspace(0.0, 2.0 * math.pi, n_u, endpoint=False)
    u_grid = np.zeros((n_u, n_v + 1), dtype=np.float64)
    y_grid = np.zeros((n_u, n_v + 1), dtype=np.float64)
    fv_grid = np.zeros((n_u, n_v + 1), dtype=np.float64)

    for iu, u in enumerate(us):
        vb, vt = _band_v_bounds(float(u))
        for iv in range(n_v + 1):
            fv = iv / float(n_v)
            u_grid[iu, iv] = float(u)
            y_grid[iu, iv] = vb + fv * (vt - vb)
            fv_grid[iu, iv] = fv

    def _vid(iu: int, iv: int) -> int:
        return (iu % n_u) * (n_v + 1) + iv

    surf_tris: List[Tuple[int, int, int]] = []
    for iu in range(n_u):
        iu1 = (iu + 1) % n_u
        u_mid = float(us[iu]) + (math.pi / n_u)
        for iv in range(n_v):
            a, b, c, d = _vid(iu, iv), _vid(iu1, iv), _vid(iu1, iv + 1), _vid(iu, iv + 1)
            y_mid = 0.25 * (y_grid[iu, iv] + y_grid[iu1, iv] + y_grid[iu1, iv + 1] + y_grid[iu, iv + 1])
            dist, _ = _eye_dist(u_mid, float(y_mid))
            if dist < 1.0:
                continue
            surf_tris.append((a, b, c))
            surf_tris.append((a, c, d))

    # Find all open boundary edges; snap interior eye-hole boundary vertices onto the exact smooth ellipse d=1.0!
    edge_counts: Dict[Tuple[int, int], int] = {}
    dir_edges: List[Tuple[int, int]] = []
    for a, b, c in surf_tris:
        for e0, e1 in ((a, b), (b, c), (c, a)):
            key = (e0, e1) if e0 < e1 else (e1, e0)
            edge_counts[key] = edge_counts.get(key, 0) + 1
            dir_edges.append((e0, e1))
    boundary = [(e0, e1) for e0, e1 in dir_edges if edge_counts[(e0, e1) if e0 < e1 else (e1, e0)] == 1]

    hole_verts = set()
    for e0, e1 in boundary:
        for v in (e0, e1):
            iv = v % (n_v + 1)
            if 0 < iv < n_v:
                hole_verts.add(v)

    for v in hole_verts:
        iu, iv = divmod(v, n_v + 1)
        uw = math.atan2(math.sin(u_grid[iu, iv]), math.cos(u_grid[iu, iv]))
        yv = y_grid[iu, iv]
        dist, eu = _eye_dist(uw, yv)
        if 0.2 < dist < 1.8:
            ang = math.atan2((yv - eye_y0) / eye_by, (uw - eu) / eye_au)
            u_snap = eu + eye_au * math.cos(ang)
            y_snap = eye_y0 + eye_by * math.sin(ang)
            u_grid[iu, iv] = u_snap % (2.0 * math.pi)
            y_grid[iu, iv] = y_snap

    n_surf = n_u * (n_v + 1)
    flat_out = np.zeros((n_surf, 3), dtype=np.float64)
    flat_in = np.zeros((n_surf, 3), dtype=np.float64)
    for iu in range(n_u):
        for iv in range(n_v + 1):
            po, pi = _band_surface_pts(float(u_grid[iu, iv]), float(y_grid[iu, iv]), float(fv_grid[iu, iv]))
            idx_v = _vid(iu, iv)
            flat_out[idx_v] = po
            flat_in[idx_v] = pi

    all_pos = np.vstack([flat_out, flat_in]).astype(np.float32)
    all_tris: List[Tuple[int, int, int]] = list(surf_tris)
    all_tris.extend((n_surf + a, n_surf + c, n_surf + b) for a, b, c in surf_tris)
    for e0, e1 in boundary:
        all_tris.append((e0, n_surf + e1, e1))
        all_tris.append((e0, n_surf + e0, n_surf + e1))

    idx_arr = np.asarray(all_tris, dtype=np.uint32).reshape(-1)
    nrm = recompute_smooth_normals(all_pos, idx_arr)
    band_mesh = MeshPart(name, "gear_gold", all_pos, nrm, idx_arr).ensure_outward()

    # Smooth rolled stitched cloth bezel ring hugging each oval eye cutout cleanly (zero jagged edges!).
    out_parts: List[MeshPart] = [band_mesh]
    for e_idx, eu in enumerate(eye_centers):
        rim_pts: List[Tuple[float, float, float]] = []
        for k in range(32):
            ang = 2.0 * math.pi * k / 32.0
            uu = eu + (eye_au + 0.004) * math.cos(ang)
            yy = eye_y0 + (eye_by + 0.002) * math.sin(ang)
            vb, vt = _band_v_bounds(uu)
            fv = max(0.0, min(1.0, (yy - vb) / max(1e-5, vt - vb)))
            po, pi = _band_surface_pts(uu, yy, fv)
            rim_pts.append((0.5 * (po[0] + pi[0]), 0.5 * (po[1] + pi[1]), 0.5 * (po[2] + pi[2])))
        out_parts.append(
            swept_tube(
                rim_pts,
                0.0155,
                radial_segs=10,
                closed=True,
                name=f"{name}_eye_hem_{e_idx}",
                material="gear_gold",
            )
        )
    return out_parts


def _broad_ribbon_tail(
    spine_ctrl: Sequence[Tuple[float, float, float]],
    half_widths: Sequence[float],
    half_thicks: Sequence[float],
    width_axis: Tuple[float, float, float],
    name: str,
) -> MeshPart:
    """Build a crisp, wide, flattened pleated bandana ribbon flap with clean kite flare & beveled hems."""
    spine = catmull_rom_spline(np.asarray(spine_ctrl, dtype=np.float64), samples_per_seg=6, closed=False)
    n_len = len(spine)
    s_u = np.linspace(0.0, 1.0, n_len)
    w_arr = np.interp(s_u, np.linspace(0.0, 1.0, len(half_widths)), half_widths)
    t_arr = np.interp(s_u, np.linspace(0.0, 1.0, len(half_thicks)), half_thicks)

    w_hint = np.asarray(width_axis, dtype=np.float64)
    w_hint /= max(1e-9, float(np.linalg.norm(w_hint)))

    n_prof = 12
    angles = np.linspace(0.0, 2.0 * math.pi, n_prof, endpoint=False)
    ca = np.cos(angles)
    sa = np.sin(angles)

    rings: List[np.ndarray] = []
    for i in range(n_len):
        t_vec = spine[min(n_len - 1, i + 1)] - spine[max(0, i - 1)]
        t_len = float(np.linalg.norm(t_vec))
        t_dir = t_vec / max(1e-9, t_len)
        # Orthonormal width (broad ribbon face) and thickness (thin cloth edge) axes!
        w_dir = w_hint - float(np.dot(w_hint, t_dir)) * t_dir
        w_dir /= max(1e-9, float(np.linalg.norm(w_dir)))
        n_dir = np.cross(t_dir, w_dir)
        n_dir /= max(1e-9, float(np.linalg.norm(n_dir)))

        # Subtle cloth wave + sharp flattened oval ribbon cross-section (superellipse exp 0.65)
        wave = 0.012 * math.sin(s_u[i] * math.pi * 1.4) * (1.0 - 0.5 * s_u[i])
        cw = np.sign(ca) * (np.abs(ca) ** 0.72) * w_arr[i]
        ct = np.sign(sa) * (np.abs(sa) ** 0.72) * t_arr[i]
        ring = spine[i][None, :] + wave * n_dir[None, :] + cw[:, None] * w_dir[None, :] + ct[:, None] * n_dir[None, :]
        rings.append(ring)

    start_apex = spine[0] - 0.008 * (spine[1] - spine[0]) / max(1e-9, float(np.linalg.norm(spine[1] - spine[0])))
    end_apex = spine[-1] + 0.012 * (spine[-1] - spine[-2]) / max(1e-9, float(np.linalg.norm(spine[-1] - spine[-2])))
    positions = np.vstack([start_apex[None, :]] + rings + [end_apex[None, :]]).astype(np.float32)

    tris: List[Tuple[int, int, int]] = []
    for j in range(n_prof):
        tris.append((0, 1 + (j + 1) % n_prof, 1 + j))
    for i in range(n_len - 1):
        b0 = 1 + i * n_prof
        b1 = 1 + (i + 1) * n_prof
        for j in range(n_prof):
            jn = (j + 1) % n_prof
            tris.append((b0 + j, b1 + j, b1 + jn))
            tris.append((b0 + j, b1 + jn, b0 + jn))
    end_i = len(positions) - 1
    b_last = 1 + (n_len - 1) * n_prof
    for j in range(n_prof):
        tris.append((end_i, b_last + j, b_last + (j + 1) % n_prof))

    idx_arr = np.asarray(tris, dtype=np.uint32).reshape(-1)
    nrm = recompute_smooth_normals(positions, idx_arr)
    return MeshPart(name, "gear_gold", positions, nrm, idx_arr).ensure_outward()


def build_pu_thief() -> SceneBuilder:
    """Thief Ant Power-Up: hollow woven-gold cloth domino mask with smooth eye cutouts & broad pleated bandana tails (`63:pu_thief`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_pu_thief")

    # 1. Continuous hollow woven-gold cloth domino headband with smooth hemmed oval eye cutouts (`pu_thief_4x`)!
    all_parts: List[MeshPart] = _build_hollow_domino_band("thief_domino_band")

    # 2. All-gold cinched cloth knot nestled cleanly into the right-rear rim (no dark red-brown ring!).
    knot_pos = (0.244, 0.014, -0.092)
    knot_core = superellipsoid(0.062, 0.054, 0.056, e1=0.80, e2=0.82, u_segs=18, v_segs=12, name="thief_knot_core", material="gear_gold").rotate_xyz(ry=-0.45).translate(*knot_pos)
    knot_fold = superellipsoid(0.044, 0.046, 0.032, e1=0.78, e2=0.78, u_segs=14, v_segs=10, name="thief_knot_fold", material="gear_gold").rotate_xyz(rx=0.35, ry=-0.45).translate(0.228, 0.024, -0.082)
    all_parts.extend([knot_core, knot_fold])

    # 3. Broad, crisp flattened pleated bandana ribbon flaps matching `63:pu_thief`'s iconic twin kite tails!
    tail_upper = _broad_ribbon_tail(
        [(0.244, 0.022, -0.092), (0.312, 0.095, -0.118), (0.388, 0.185, -0.145), (0.458, 0.278, -0.165)],
        half_widths=[0.038, 0.092, 0.082, 0.012],
        half_thicks=[0.016, 0.015, 0.012, 0.005],
        width_axis=(-0.62, 0.62, -0.48),
        name="thief_tail_upper",
    )
    tail_lower = _broad_ribbon_tail(
        [(0.248, 0.002, -0.090), (0.345, -0.004, -0.085), (0.445, 0.006, -0.072), (0.525, 0.014, -0.058)],
        half_widths=[0.036, 0.076, 0.064, 0.010],
        half_thicks=[0.015, 0.014, 0.011, 0.005],
        width_axis=(0.0, 0.81, -0.58),
        name="thief_tail_lower",
    )
    all_parts.extend([tail_upper, tail_lower])

    # Tilt slightly so the open head-ring, smooth oval eye holes & broad upper-right ribbon tails match `63:pu_thief`!
    for p in all_parts:
        sc.add("pu_thief", p.rotate_xyz(rx=20.0, ry=-10.0, rz=-10.0, degrees=True))
    return _finalize_item(sc, target_span=0.92)


# ---------------------------------------------------------------------------
# 64:pu_bomb — Bomber Ant's Exact Riveted Yellow Tin-Can Goggles + Dynamite
# ---------------------------------------------------------------------------

def build_pu_bomb() -> SceneBuilder:
    """Bomber Ant Power-Up: twin riveted yellow tin-can aviator goggles, upright leather strap & sizzling dynamite (`64:pu_bomb`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_pu_bomb")
    sc.set_node_transform("pu_bomb", translation=(0.0, 0.0, 0.0))

    # 1. Bomber Ant's EXACT twin long riveted yellow metallic binocular tin-can goggle barrels (`ant_bomber._build_aviator_cup`)!
    bore_r = 0.092
    lens_z = 0.168
    goggle_parts: List[MeshPart] = []
    for side_sign, gx, yaw in ((-1.0, -0.110, -0.16), (+1.0, +0.110, +0.16)):
        for part in _build_aviator_cup(side_sign, bore_r=bore_r, lens_z=lens_z):
            # Brighten goggle lens to crisp frosty clear aviator glass & rich golden-yellow barrels matching `64:pu_bomb`!
            if part.material == "glass_tinted":
                part = MeshPart(part.name, "glass_clear", part.positions, part.normals, part.indices, uvs=part.uvs)
            elif "copper_ring" in part.name or "rim" in part.name:
                part = MeshPart(part.name, "gear_gold", part.positions, part.normals, part.indices, uvs=part.uvs)
            goggle_parts.append(part.rotate_xyz(rx=-0.05, ry=yaw).translate(gx, 0.130, +0.118))

    # Metallic nose bridge & stud connecting the twin goggle cans.
    goggle_parts.append(rounded_box(0.054, 0.032, 0.036, bevel=0.008, segs=2, name="pubomb_bridge", material="gear_gold").translate(0.0, 0.135, +0.205))
    goggle_parts.append(cylinder(0.012, 0.012, 0.014, radial_segs=14, axis="z", name="pubomb_bridge_rivet", material="gear_gold_metal").translate(0.0, 0.135, +0.226))
    # Temple strap clasps joining goggle barrels cleanly into the upright brown leather head-strap!
    for side_sign, tag in ((-1.0, "r"), (+1.0, "l")):
        goggle_parts.append(rounded_box(0.034, 0.072, 0.064, bevel=0.008, segs=2, name=f"pubomb_clasp_{tag}", material="gear_gold").translate(side_sign * 0.218, 0.130, +0.112))

    # 2. UPRIGHT vertical-band open brown leather head-strap arching cleanly behind the goggles (`r0=height, r1=thickness`)!
    strap_pts = [
        (-0.216, 0.130, +0.098),
        (-0.268, 0.130, -0.025),
        (-0.212, 0.134, -0.172),
        (0.000, 0.138, -0.242),
        (+0.212, 0.134, -0.172),
        (+0.268, 0.130, -0.025),
        (+0.216, 0.130, +0.098),
    ]
    strap = swept_tube(
        strap_pts,
        (0.056, 0.018),
        radial_segs=12,
        smooth_subdiv=4,
        cap_start=True,
        cap_end=True,
        closed=False,
        name="pubomb_leather_strap",
        material="item_twig_bark",
    )
    goggle_parts.append(strap)

    # Rotate so twin gleaming yellow cans face cleanly lower-right & upright brown leather strap loops left (`64:pu_bomb`)!
    for p in goggle_parts:
        sc.add("pu_bomb", p.rotate_xyz(ry=48.0, degrees=True))

    # 3. Bold upright crimson-red & purple-collared dynamite stick tucked cleanly inside the goggle strap (`64:pu_bomb`)!
    bx, bz = -0.035, -0.035
    bomb_lower = cylinder(0.094, 0.090, 0.215, radial_segs=26, bevel=0.008, axis="y", name="pubomb_stick_base", material="gear_gold").translate(bx, 0.125, bz)
    bomb_band_red = cylinder(0.098, 0.098, 0.048, radial_segs=26, bevel=0.005, axis="y", name="pubomb_red_ring", material="item_bomb_red").translate(bx, 0.085, bz)
    bomb_collar_purp = cylinder(0.112, 0.106, 0.078, radial_segs=26, bevel=0.010, axis="y", name="pubomb_purple_collar", material="item_bomb_purple").translate(bx, 0.258, bz)
    bomb_cap_red = cylinder(0.114, 0.112, 0.105, radial_segs=26, bevel=0.015, axis="y", name="pubomb_red_top", material="item_bomb_red").translate(bx, 0.338, bz)
    fuse = curved_tube(
        [(bx, 0.382, bz), (bx - 0.022, 0.455, bz - 0.008), (bx + 0.010, 0.515, bz + 0.005), (bx + 0.046, 0.502, bz + 0.010)],
        [0.015, 0.014, 0.013, 0.011],
        samples_per_seg=4,
        radial_segs=10,
        name="pubomb_fuse",
        material="item_fuse_cord",
    )
    sc.add("pu_bomb", [bomb_lower, bomb_band_red, bomb_collar_purp, bomb_cap_red, fuse])

    # Sizzling animated spark starburst perched right at the fuse tip!
    spark_core = sphere(0.028, u_segs=12, v_segs=10, name="pubomb_spark_core", material="item_flame_core")
    spark_parts = [spark_core]
    for k in range(6):
        ang = 2.0 * math.pi * k / 6.0
        spark_parts.append(
            cone(0.011, 0.052, radial_segs=8, name=f"pubomb_ray_{k}", material="item_flame_outer")
            .rotate_xyz(rx=math.pi * 0.5, rz=ang)
            .translate(0.028 * math.sin(ang), 0.028 * math.cos(ang), 0.0)
        )
    sc.set_node_parent("pubomb_spark", "pu_bomb")
    sc.set_node_transform("pubomb_spark", translation=(bx + 0.046, 0.502, bz + 0.010))
    sc.add("pubomb_spark", spark_parts)
    times = [0.00, 0.12, 0.24, 0.36]
    sc.add_animation(
        "Idle",
        [
            AnimationChannel("pubomb_spark", "scale", times, [(0.86, 0.86, 0.86), (1.28, 1.28, 1.28), (0.94, 0.94, 0.94), (0.86, 0.86, 0.86)]),
            AnimationChannel("pubomb_spark", "rotation", times, [quat_from_euler_xyz(rz=a) for a in (0.0, 0.6, 1.2, 0.0)]),
        ],
        replace=True,
    )
    return _finalize_item(sc, target_span=0.92)


# ---------------------------------------------------------------------------
# 65:pu_swim — Swimmer Ant's Diagonal Yellow Oval Scuba Mask, Tall Strap & Snorkel
# ---------------------------------------------------------------------------

def build_pu_swim() -> SceneBuilder:
    """Swimmer Ant Power-Up: chunky diagonal yellow scuba mask, tall open rubber strap, corrugated hose & ground snorkel (`65:pu_swim`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_pu_swim")

    rx_in, ry_in = 0.176, 0.118
    wall_t = 0.062
    curve_depth = 0.046
    rx_out = rx_in + wall_t

    # 1. Swimmer Ant's EXACT chunky bright-yellow molded oval scuba mask housing (`ant_swimmer._hollow_oval_housing`)!
    frame_prof = [
        (0.00, -0.056),
        (0.86, -0.058),
        (1.00, -0.044),
        (1.04, +0.016),
        (0.96, +0.038),
        (0.76, +0.046),
        (0.18, +0.046),
        (0.02, +0.032),
        (0.00, +0.008),
    ]
    frame = _hollow_oval_housing(
        frame_prof,
        rx_in=rx_in,
        ry_in=ry_in,
        wall_t=wall_t,
        oval_exp=1.0,
        curve_depth=curve_depth,
        radial_segs=56,
        name="puswim_mask_frame",
        material="gear_yellow_plastic",
    )

    # Dark rubber inner viewport gasket + hollow face-sealing rubber skirt (NO solid egg blob!).
    gasket_prof = [(-0.04, -0.010), (0.10, -0.010), (0.12, +0.022), (-0.02, +0.024), (-0.06, +0.010)]
    gasket = _hollow_oval_housing(
        gasket_prof, rx_in=rx_in, ry_in=ry_in, wall_t=wall_t, oval_exp=1.0, curve_depth=curve_depth, radial_segs=48, name="puswim_gasket", material="gear_dark"
    )
    skirt_prof = [(0.08, -0.125), (0.58, -0.128), (0.96, -0.098), (1.00, -0.050), (0.12, -0.050)]
    skirt = _hollow_oval_housing(
        skirt_prof, rx_in=rx_in * 0.96, ry_in=ry_in * 0.96, wall_t=wall_t * 0.94, oval_exp=1.0, curve_depth=curve_depth, radial_segs=48, name="puswim_skirt", material="gear_dark"
    )

    # Curved translucent slate-blue glass faceplate lens (`glass_tinted`) matching `ant_swimmer` + `65:pu_swim`!
    visor = (
        superellipsoid(rx_in + 0.006, ry_in + 0.006, 0.009, e1=1.0, e2=1.0, u_segs=36, v_segs=18, name="puswim_visor", material="glass_tinted")
        .deform(lambda p: np.column_stack([p[:, 0], p[:, 1], p[:, 2] - curve_depth * ((p[:, 0] / rx_out) ** 2)]))
        .translate(0.0, 0.0, +0.008)
    )

    mask_parts: List[MeshPart] = [frame, gasket, skirt, visor]
    # Chunky yellow temple strap hinges on left & right sides of the mask.
    for s, tag in ((+1.0, "l"), (-1.0, "r")):
        mask_parts.append(
            rounded_box(0.048, 0.078, 0.078, bevel=0.012, segs=2, name=f"puswim_buckle_{tag}", material="gear_yellow_plastic")
            .rotate_xyz(ry=s * 0.28)
            .translate(s * (rx_out - 0.016), 0.0, -0.070)
        )

    # Pose mask cleanly in the iconic `/` +44° diagonal facing front-right matching `65:pu_swim` (`pu_swim_4x.png`)!
    mask_center = (0.040, 0.220, 0.050)
    for mp in mask_parts:
        sc.add("pu_swim", mp.rotate_xyz(rx=-8.0, ry=24.0, rz=44.0, degrees=True).translate(*mask_center))

    # 2. Tall wide OPEN dark-rubber rear head-strap arching high behind the upper-left of the mask (`pu_swim_4x.png`)!
    #    Endpoints attach directly into the posed left/right temple hinges while the wide band rises cleanly above-left.
    strap_pts = [
        (-0.140, 0.085, +0.040),
        (-0.215, 0.210, -0.070),
        (-0.195, 0.405, -0.140),
        (-0.085, 0.512, -0.168),
        (+0.065, 0.492, -0.188),
        (+0.175, 0.395, -0.135),
        (+0.195, 0.335, -0.055),
    ]
    head_strap = swept_tube(
        strap_pts,
        (0.026, 0.088),
        radial_segs=16,
        smooth_subdiv=4,
        cap_start=True,
        cap_end=True,
        closed=False,
        name="puswim_head_strap",
        material="gear_dark",
    )
    rear_clip = (
        rounded_box(0.086, 0.072, 0.034, bevel=0.008, segs=2, name="puswim_rear_clip", material="gear_steel")
        .rotate_xyz(rx=-24.0, ry=18.0, degrees=True)
        .translate(-0.040, 0.502, -0.186)
    )
    sc.add("pu_swim", [head_strap, rear_clip])

    # 3. Corrugated dark breathing hose hugging the lower-right rim of the yellow mask (with slate-grey rib rings!)
    #    flowing smoothly into the HORIZONTAL bright-yellow snorkel tip resting flat on the bottom-left (`pu_swim_4x.png`)!
    hose_ctrl = [
        (+0.220, 0.265, -0.015),
        (+0.280, 0.160, +0.025),
        (+0.215, 0.068, +0.062),
        (+0.070, 0.048, +0.062),
        (-0.115, 0.052, +0.042),
        (-0.210, 0.052, +0.022),
    ]
    hose_dense = catmull_rom_spline(np.asarray(hose_ctrl, dtype=np.float64), samples_per_seg=6, closed=False)
    hose = curved_tube(hose_ctrl, [0.030, 0.034, 0.036, 0.036, 0.034, 0.034], samples_per_seg=5, radial_segs=14, cap_rings=3, name="puswim_snorkel_hose", material="gear_dark")
    hose_parts: List[MeshPart] = [hose]

    # Crisp corrugated scuba hose rib rings wrapping the lower-right bend (matching the 3 grey rib highlights in `pu_swim_4x`!).
    for k in range(9):
        frac = 0.08 + 0.72 * (k / 8.0)
        idx_i = min(len(hose_dense) - 2, max(1, int(frac * (len(hose_dense) - 1))))
        pt = hose_dense[idx_i]
        t_dir = hose_dense[idx_i + 1] - hose_dense[idx_i - 1]
        t_dir /= max(1e-9, float(np.linalg.norm(t_dir)))
        ry_rib = math.atan2(float(t_dir[0]), float(t_dir[2]))
        rx_rib = -math.asin(max(-1.0, min(1.0, float(t_dir[1]))))
        rib_mat = "gear_steel" if (k in (2, 4, 6)) else "gear_dark"
        rib = (
            torus_arc(0.033, 0.011, u_segs=18, v_segs=8, plane="xy", name=f"puswim_hose_rib_{k}", material=rib_mat)
            .rotate_xyz(rx=rx_rib, ry=ry_rib)
            .translate(float(pt[0]), float(pt[1]), float(pt[2]))
        )
        hose_parts.append(rib)

    # Compact brushed-steel scuba regulator valve block tucked neatly on the upper-right hose junction.
    reg_body = cylinder(0.042, 0.042, 0.068, radial_segs=18, bevel=0.008, axis="y", name="puswim_reg_valve", material="gear_steel").rotate_xyz(rz=-0.38).translate(+0.225, 0.275, -0.045)
    reg_knob = cylinder(0.022, 0.022, 0.024, radial_segs=14, bevel=0.004, axis="y", name="puswim_reg_knob", material="gear_dark").rotate_xyz(rz=-0.38).translate(+0.242, 0.312, -0.045)

    # Bright-yellow horizontal snorkel tip & recessed black intake bore resting flat on the bottom-left (`pu_swim_4x`!).
    snk_y, snk_z = 0.052, 0.022
    snk_collar = cylinder(0.043, 0.043, 0.028, radial_segs=20, bevel=0.004, axis="x", name="puswim_snk_collar", material="gear_gold_metal").translate(-0.215, snk_y, snk_z)
    snk_tip = cylinder(0.041, 0.042, 0.175, radial_segs=22, bevel=0.007, axis="x", name="puswim_snk_yellow", material="gear_yellow_plastic").translate(-0.312, snk_y, snk_z)
    snk_vent = rounded_box(0.026, 0.056, 0.032, bevel=0.005, segs=1, name="puswim_snk_vent", material="gear_dark").translate(-0.365, snk_y + 0.004, snk_z + 0.030)
    snk_bore = cylinder(0.028, 0.028, 0.022, radial_segs=16, axis="x", name="puswim_snk_bore", material="gear_dark").translate(-0.394, snk_y, snk_z)

    hose_parts.extend([reg_body, reg_knob, snk_collar, snk_tip, snk_vent, snk_bore])
    sc.add("pu_swim", hose_parts)
    return _finalize_item(sc, target_span=0.92)


# ---------------------------------------------------------------------------
# 66:pu_mason — Fire Ant's EXACT Deeply-Fluted Yellow Vintage Fire Helmet
# ---------------------------------------------------------------------------

def build_pu_mason() -> SceneBuilder:
    """Fire Ant Power-Up: Fire Ant's exact deeply-fluted vintage yellow firefighter helmet with crimson 'A' shield (`66:pu_mason`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_pu_mason")

    dome_rx, dome_ry, dome_rz = 0.242, 0.262, 0.256
    u_segs = 80
    start_phi = -0.04
    e_dom = 0.92
    y_min_raw = dome_ry * _spow_scalar(math.sin(start_phi), e_dom)
    y_max_raw = dome_ry

    def _deform_dome(pos: np.ndarray) -> np.ndarray:
        return _helmet_shell_deform_bounds(pos, y_min_raw, y_max_raw)

    parts: List[MeshPart] = []

    # 1. Exact 80x32 deeply-fluted 8-rib yellow plastic firefighter dome + upturned sides & sweeping rear neck-guard!
    dome = hemisphere_shell(
        dome_rx,
        dome_ry,
        dome_rz,
        thickness=0.022,
        start_phi=start_phi,
        u_segs=u_segs,
        v_segs=32,
        e1=e_dom,
        e2=e_dom,
        deform_fn=_deform_dome,
        name="pumason_dome",
        material=_FIRE_HAT_MAT,
    )
    parts.append(dome)

    # Rolled outer brim edge lip hugging the exact firefighter brim perimeter.
    n_out = (len(dome.positions) - u_segs) // 2
    rim_mid = 0.5 * (dome.positions[:u_segs] + dome.positions[n_out : n_out + u_segs])
    rim_center = 0.88 * rim_mid + 0.12 * dome.positions[-u_segs:]
    parts.append(
        swept_tube(
            [(float(pt[0]), float(pt[1]), float(pt[2])) for pt in rim_center],
            0.0085,
            radial_segs=10,
            closed=True,
            name="pumason_brim_lip",
            material=_FIRE_HAT_MAT,
        )
    )

    # Dark recessed inner helmet headband liner inside the hollow shell.
    parts.append(cylinder(dome_rx * 0.78, dome_rx * 0.82, 0.048, radial_segs=26, bevel=0.006, axis="y", name="pumason_liner", material="gear_dark").translate(0.0, 0.048, 0.0))

    # 2. Exact vintage fluted dome spine ribs + top crown crest!
    def _sample_dome(phi: float, theta: float) -> Tuple[float, float, float]:
        cp = _spow_scalar(math.cos(phi), e_dom)
        sp = _spow_scalar(math.sin(phi), e_dom)
        st = _spow_scalar(math.sin(theta), e_dom)
        ct = _spow_scalar(math.cos(theta), e_dom)
        raw = np.array([[dome_rx * cp * st, dome_ry * sp, dome_rz * cp * ct]], dtype=np.float64)
        d = _deform_dome(raw)[0]
        return (float(d[0]), float(d[1]), float(d[2]))

    for az_deg, phi_end, peak_rad, tag in ((180.0, 0.24, 0.0135, "back"), (90.0, 0.26, 0.0125, "left"), (-90.0, 0.26, 0.0125, "right"), (135.0, 0.24, 0.0125, "bl"), (-135.0, 0.24, 0.0125, "br")):
        az = math.radians(az_deg)
        phis = np.linspace(0.49 * math.pi, phi_end, 8)
        r_pts = [_sample_dome(float(phi), az) for phi in phis]
        r_rads = [peak_rad * (0.95 - 0.30 * (k / (len(phis) - 1))) for k in range(len(phis))]
        parts.append(curved_tube(r_pts, r_rads, samples_per_seg=4, radial_segs=10, cap_rings=2, name=f"pumason_rib_{tag}", material=_FIRE_HAT_MAT))

    crest_pts = [_sample_dome(phi, az) for phi, az in ((0.84, 0.0), (1.12, 0.0), (1.36, 0.0), (0.5 * math.pi, 0.0), (1.30, math.pi), (1.02, math.pi), (0.72, math.pi))]
    parts.append(curved_tube(crest_pts, [0.0115, 0.0135, 0.0145, 0.0150, 0.0145, 0.0135, 0.0115], samples_per_seg=4, radial_segs=10, cap_rings=2, name="pumason_crest", material=_FIRE_HAT_MAT))

    # 3. Fire Ant's EXACT proud framed yellow front crest shield + snug seamless rear gusset + bold crimson 'A' emblem!
    shield_w = dome_rx * 1.94
    shield_h = dome_ry * 0.96
    shield_depth = 0.092
    curve_depth = 0.132
    shield_tilt_x = -14.5
    shield_pos = (0.0, 0.158, dome_rz - 0.032)

    parts.append(_build_option3_wide_shield(shield_w, shield_h, shield_depth, curve_depth).rotate_xyz(rx=shield_tilt_x, degrees=True).translate(*shield_pos))
    # Recessed rear collar tucked strictly BEHIND the shield's back wall so there is zero gap & zero front poke-through!
    gusset = (
        superellipsoid(dome_rx * 0.58, dome_ry * 0.34, 0.042, e1=0.70, e2=0.75, u_segs=22, v_segs=12, name="pumason_shield_gusset", material=_FIRE_HAT_MAT)
        .rotate_xyz(rx=shield_tilt_x, degrees=True)
        .translate(0.0, 0.144, dome_rz - 0.112)
    )
    parts.append(gusset)
    for a_part in _build_giant_letter_a(shield_w, shield_h, shield_depth, curve_depth, y_offset=-0.004):
        parts.append(a_part.rotate_xyz(rx=shield_tilt_x, degrees=True).translate(*shield_pos))

    # Pose cleanly angled so sweeping yellow rear neck-guard sits left/top & crimson 'A' shield faces front-right (`66:pu_mason`)!
    for p in parts:
        sc.add("pu_mason", p.rotate_xyz(rx=10.0, ry=40.0, rz=-5.0, degrees=True))
    return _finalize_item(sc, target_span=0.94)
