"""Realistic 3D sculptors for 1998 Ants team anthills, planted bombs, and fire wall (Wave 2).

Covers:
- `BLACKHILL` (245), `BLUEHILL` (246), `REDHILL` (247): Remaining 3 team anthills with waving flags & authentic decor
- `redbomb` (129), `greenbomb` (130), `blackbomb` (131), `bluebomb` (132): All 4 team planted bombs + spark pulse
- `wallup04` (134): Blazing multi-layered 3D Fire Wall flame + animated flicker clip
"""
from __future__ import annotations

import math
from typing import Callable, List, Tuple

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
            cone,
            curved_tube,
            cylinder,
            ellipsoid,
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
            cone,
            curved_tube,
            cylinder,
            ellipsoid,
            lathe,
            quat_from_euler_xyz,
            recompute_smooth_normals,
            rounded_box,
            sphere,
            superellipsoid,
            torus_arc,
        )


# ---------------------------------------------------------------------------
# Shared sculpted earthen volcano anthill base + waving cloth flag & cap helpers
# ---------------------------------------------------------------------------

def _build_anthill_base(
    sc: SceneBuilder,
    *,
    soil_mat: str,
    seed: int,
    loam_lo: Tuple[float, float, float],
    loam_hi: Tuple[float, float, float],
    pebble_count: int = 30,
    crater_scale: float = 0.80,
) -> Callable[[float, float], float]:
    """Build hyper-detailed sculpted volcanic earthen anthill mound + tight burrow throat + pebbles, returning `_surface_y(x, z)`."""
    sc.set_node_transform("anthill", translation=(0.0, 0.0, 0.0))
    mound_prof = [
        (0.000, 0.110),
        (0.102 * crater_scale, 0.115),
        (0.116 * crater_scale, 0.310),
        (0.148 * crater_scale, 0.420),
        (0.188 * crater_scale, 0.436),  # sculpted volcanic ant-burrow peak lip
        (0.255, 0.370),
        (0.345, 0.245),
        (0.435, 0.125),
        (0.515, 0.036),
        (0.565, 0.000),
        (0.000, 0.000),
    ]
    slope_stations = [
        (0.188 * crater_scale, 0.436),
        (0.255, 0.370),
        (0.345, 0.245),
        (0.435, 0.125),
        (0.515, 0.036),
        (0.565, 0.000),
    ]
    ph_a = 0.4 + 0.7 * (seed % 7)
    ph_b = -0.8 + 0.5 * (seed % 5)

    def _wobble(ang: float) -> float:
        return 1.0 + 0.075 * math.sin(ang * 3.0 + ph_a) + 0.048 * math.cos(ang * 5.0 + ph_b)

    def _surface_y(x: float, z: float) -> float:
        ang = math.atan2(z, x)
        r_prof = math.hypot(x, z) / _wobble(ang)
        if r_prof <= slope_stations[0][0]:
            return slope_stations[0][1]
        for (r0, y0), (r1, y1) in zip(slope_stations[:-1], slope_stations[1:]):
            if r_prof <= r1:
                t = (r_prof - r0) / max(1e-5, r1 - r0)
                return y0 * (1.0 - t) + y1 * t
        return 0.0

    def _sculpt(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ang = np.arctan2(p[:, 2], p[:, 0])
        r = np.hypot(p[:, 0], p[:, 2])
        wobble = 1.0 + 0.075 * np.sin(ang * 3.0 + ph_a) + 0.048 * np.cos(ang * 5.0 + ph_b)
        p[:, 0] *= wobble
        p[:, 2] *= wobble
        # Pronounced radial rain-erosion gullies + crumbly soil ridges down the mound flanks!
        mid_slope = np.clip((r - 0.14) / 0.16, 0.0, 1.0) * np.clip((0.54 - r) / 0.14, 0.0, 1.0)
        gullies = (0.018 * np.sin(ang * 9.0 + ph_a) + 0.011 * np.cos(ang * 15.0 - ph_b)) * mid_slope
        clods = 0.015 * np.sin(p[:, 0] * 26.0 + p[:, 2] * 23.0 + seed) * np.cos(p[:, 1] * 29.0)
        p[:, 1] = np.maximum(0.0, p[:, 1] + (gullies + clods) * np.clip(p[:, 1] / 0.05, 0.0, 1.0))
        return p

    def _tint(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
        r = np.hypot(pos[:, 0], pos[:, 2])
        ang = np.arctan2(pos[:, 2], pos[:, 0])
        hole_dark = np.clip((r - 0.105) / 0.085, 0.08, 1.0)[:, None]
        hw = np.clip(pos[:, 1] / 0.36, 0.0, 1.0)[:, None]
        gully_shade = (0.94 + 0.08 * np.sin(ang * 9.0 + ph_a))[:, None]
        mottle = (0.07 * np.sin(pos[:, 0] * 24.0 + pos[:, 2] * 21.0 + seed) + 0.04 * np.cos(pos[:, 0] * 41.0 - pos[:, 2] * 37.0))[:, None]
        rim_sun = 1.0 + 0.06 * np.clip(nrm[:, 1], 0.0, 1.0)[:, None]
        lo = np.asarray(loam_lo, dtype=np.float64)[None, :]
        hi = np.asarray(loam_hi, dtype=np.float64)[None, :]
        return np.clip(((lo * (1.0 - hw) + hi * hw) * gully_shade + mottle) * hole_dark * rim_sun, 0.05, 1.0)

    mound = tint_part(
        lathe(mound_prof, radial_segs=56, cap_bottom=False, cap_top=False, axis="y", name="hill_mound", material=soil_mat).deform(_sculpt),
        _tint,
    )
    hole = cylinder(0.118 * crater_scale, 0.118 * crater_scale, 0.032, radial_segs=24, axis="y", name="hill_hole_dark", material="item_soil_hole").translate(0.0, 0.135, 0.0)
    sc.add("anthill", [mound, hole])

    # Sculpted dirt clod collar lumps & varied scattered gravel/pebbles seated firmly ON the mound skirt (zero floating stones)!
    rng = np.random.default_rng(seed)
    for k in range(pebble_count):
        ang = 2.0 * math.pi * k / float(pebble_count) + float(rng.uniform(-0.18, 0.18))
        rad = float(rng.uniform(0.34, 0.49)) * _wobble(ang)
        px, pz = rad * math.cos(ang), rad * math.sin(ang)
        pr = float(rng.uniform(0.018, 0.044))
        py = max(pr * 0.48, _surface_y(px, pz) + pr * 0.16)
        mat = "item_stone_granite" if (k % 3 != 0) else soil_mat
        peb = tint_part(
            ellipsoid(pr * 1.22, pr * 0.62, pr * 0.96, u_segs=10, v_segs=6, name=f"hill_pebble_{k}", material=mat)
            .rotate_xyz(rx=float(rng.uniform(-0.25, 0.25)), ry=ang, rz=float(rng.uniform(-0.25, 0.25)))
            .translate(px, py, pz),
            lambda pos, _n, shade=float(rng.uniform(0.62, 1.02)): np.full((len(pos), 3), shade, dtype=np.float64),
        )
        sc.add("anthill", peb)

    # Extra sculpted earthen clumps nestled right around the upper crater lip for natural ant-dug soil realism!
    for k in range(10):
        ang = 2.0 * math.pi * k / 10.0 + float(rng.uniform(-0.2, 0.2))
        rad = float(rng.uniform(0.19, 0.28))
        cx, cz = rad * math.cos(ang), rad * math.sin(ang)
        cr = float(rng.uniform(0.024, 0.044))
        clump = ellipsoid(cr * 1.3, cr * 0.65, cr * 1.1, u_segs=10, v_segs=6, name=f"hill_clod_{k}", material=soil_mat).rotate_xyz(ry=ang).translate(cx, _surface_y(cx, cz) + cr * 0.18, cz)
        sc.add("anthill", clump)
    return _surface_y


def _cloth_flag_segment(
    name: str,
    *,
    length: float,
    h0: float,
    h1: float,
    mat: str,
    thickness: float = 0.011,
    u0: float = 0.0,
    u1: float = 1.0,
    droop_y: float = 0.0,
) -> MeshPart:
    """Build one tapered 3D cloth pennant segment extending along local +X from x=-0.012 to x=length."""
    nx, ny = 8, 5
    verts: List[Tuple[float, float, float]] = []
    uvs: List[Tuple[float, float]] = []
    tris: List[Tuple[int, int, int]] = []
    cols: List[Tuple[float, float, float]] = []

    # Build top (+Z) and bottom (-Z) cloth faces with thin rounded fabric edge so light catches folds from every side.
    for side_sign in (+1.0, -1.0):
        base_v = len(verts)
        for ix in range(nx + 1):
            t = ix / float(nx)
            x = -0.010 + (length + 0.010) * t
            u_tot = u0 + (u1 - u0) * t
            half_h = 0.5 * (h0 * (1.0 - t) + h1 * t)
            y_mid = droop_y * t
            # Subtle fabric ripple built into rest geometry so it already reads like billowing cloth even paused!
            fold_z = 0.012 * math.sin(u_tot * math.pi * 2.5)
            for iy in range(ny + 1):
                v = iy / float(ny)
                y = y_mid + (v - 0.5) * 2.0 * half_h
                edge_t = math.sin(v * math.pi)
                z = fold_z + side_sign * (0.5 * thickness * (0.35 + 0.65 * edge_t))
                verts.append((x, y, z))
                uvs.append((u_tot * 1.4, v))
                # Gentle warm cloth fold shading (slightly richer near pole & lower hem).
                shade = 0.86 + 0.14 * (0.5 + 0.5 * math.cos(u_tot * math.pi * 2.5)) * (0.85 + 0.15 * v)
                cols.append((shade, shade, shade))
        stride = ny + 1
        for ix in range(nx):
            for iy in range(ny):
                a = base_v + ix * stride + iy
                b = base_v + (ix + 1) * stride + iy
                c = base_v + (ix + 1) * stride + (iy + 1)
                d = base_v + ix * stride + (iy + 1)
                if side_sign > 0:
                    tris.append((a, b, c))
                    tris.append((a, c, d))
                else:
                    tris.append((a, c, b))
                    tris.append((a, d, c))

    pos = np.asarray(verts, dtype=np.float32)
    idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
    nrm = recompute_smooth_normals(pos, idx)
    part = MeshPart(name, mat, pos, nrm, idx, uvs=np.asarray(uvs, dtype=np.float32))
    c_arr = np.clip(np.asarray(cols, dtype=np.float32), 0.12, 1.0)
    part.colors = c_arr
    part._base_tint = c_arr.copy()
    return part


def _add_waving_flag(
    sc: SceneBuilder,
    *,
    prefix: str,
    anchor: Tuple[float, float, float],
    yaw_deg: float,
    pitch_deg: float,
    roll_deg: float,
    length: float,
    base_height: float,
    mat: str,
    period_sec: float,
    wave_amp: float = 0.24,
    droop: float = -0.025,
) -> None:
    """Attach a 3-segment articulated waving cloth pennant parented to `anthill` with an authentic `Idle` wave clip."""
    root_node = f"{prefix}_flag_root"
    sc.set_node_parent(root_node, "anthill")
    sc.set_node_transform(
        root_node,
        translation=anchor,
        rotation=quat_from_euler_xyz(rx=pitch_deg, ry=yaw_deg, rz=roll_deg, degrees=True),
    )

    seg_len = length / 3.0
    heights = (base_height, base_height * 0.68, base_height * 0.34, 0.010)
    seg_nodes = [f"{prefix}_flag_s0", f"{prefix}_flag_s1", f"{prefix}_flag_s2"]
    for i, node_nm in enumerate(seg_nodes):
        parent_nm = root_node if i == 0 else seg_nodes[i - 1]
        sc.set_node_parent(node_nm, parent_nm)
        sc.set_node_transform(node_nm, translation=(0.0 if i == 0 else seg_len, 0.0 if i == 0 else droop, 0.0))
        seg = _cloth_flag_segment(
            f"{prefix}_cloth_{i}",
            length=seg_len,
            h0=heights[i],
            h1=heights[i + 1],
            mat=mat,
            u0=i / 3.0,
            u1=(i + 1) / 3.0,
            droop_y=droop,
        )
        sc.add(node_nm, seg)

    # Smooth looping traveling wind-wave across the 3 cloth joints (exact 1998 cycle period!).
    n_steps = 8
    times = [period_sec * (k / float(n_steps)) for k in range(n_steps + 1)]
    channels: List[AnimationChannel] = []
    for i, node_nm in enumerate(seg_nodes):
        phase = -1.15 * i
        amp_y = wave_amp * (0.72 + 0.34 * i)
        amp_z = wave_amp * (0.28 + 0.16 * i)
        rots = []
        for k in range(n_steps + 1):
            ang = 2.0 * math.pi * (k / float(n_steps))
            ry = amp_y * math.sin(ang + phase)
            rz = amp_z * math.cos(ang + phase - 0.5)
            rx = 0.08 * math.sin(ang * 2.0 + phase)
            rots.append(quat_from_euler_xyz(rx=rx, ry=ry, rz=rz))
        channels.append(AnimationChannel(node_nm, "rotation", times, rots))
    sc.add_animation("Idle", channels, replace=True)


def _crimped_bottle_cap(prefix: str, top_mat: str, *, radius: float = 0.106, planar_top_uv: bool = False) -> List[MeshPart]:
    """Reusable realistic 21-crimp rounded metallic soda bottle cap with optional crisp planar printed-logo UVs."""
    def _crimp(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ang = np.arctan2(p[:, 2], p[:, 0])
        bot = np.clip((0.014 - p[:, 1]) / 0.032, 0.0, 1.0)
        flute = 1.0 + 0.048 * np.cos(ang * 21.0) * bot
        p[:, 0] *= flute
        p[:, 2] *= flute
        return p

    metal = cylinder(radius, radius * 1.07, 0.040, radial_segs=126, bevel=0.004, axis="y", name=f"{prefix}_metal", material="item_tin_aluminum").deform(_crimp)
    top_r = radius * 0.88
    top = cylinder(top_r, top_r, 0.016, radial_segs=32, bevel=0.004, axis="y", name=f"{prefix}_top", material=top_mat).translate(0.0, 0.017, 0.0)
    if planar_top_uv:
        # Clean top-down planar UV mapping so 'H2O' / 'soda' graphics render razor-sharp & upright!
        u = np.clip(0.5 + top.positions[:, 0] / (2.0 * top_r * 1.05), 0.0, 1.0)
        v = np.clip(0.5 + top.positions[:, 2] / (2.0 * top_r * 1.05), 0.0, 1.0)
        top.uvs = np.column_stack([u, v]).astype(np.float32)
    return [metal, top]


def _paperclip(name: str, material: str = "item_aluminum", scale: float = 1.0) -> MeshPart:
    """Sculpted realistic looped wire paperclip."""
    s = scale
    pts = [
        (-0.075 * s, 0.0, +0.024 * s),
        (+0.085 * s, 0.0, +0.024 * s),
        (+0.112 * s, 0.0, 0.0),
        (+0.085 * s, 0.0, -0.024 * s),
        (-0.085 * s, 0.0, -0.024 * s),
        (-0.110 * s, 0.0, -0.004 * s),
        (-0.082 * s, 0.0, +0.014 * s),
        (+0.055 * s, 0.0, +0.014 * s),
    ]
    return curved_tube(pts, [0.0085 * s] * len(pts), samples_per_seg=3, radial_segs=8, cap_rings=2, name=name, material=material)


def build_blackhill() -> SceneBuilder:
    """Black Team Anthill with waving black nail-flag, Phillips screws, hex nut+bolt, washers & metal dome (`245:BLACKHILL`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_blackhill")
    surf_y = _build_anthill_base(sc, soil_mat="item_soil_grey", seed=245, loam_lo=(0.70, 0.68, 0.64), loam_hi=(1.02, 0.99, 0.92), pebble_count=34)

    # 1. Iconic giant slanted steel carpentry nail driven into the upper-left crater shoulder (`245:BLACKHILL`)!
    nx, nz = -0.22, -0.06
    ny = surf_y(nx, nz) + 0.010
    nail_shaft = curved_tube(
        [(nx + 0.03, ny - 0.04, nz + 0.02), (nx - 0.06, ny + 0.14, nz - 0.04), (nx - 0.15, ny + 0.30, nz - 0.09)],
        [0.020, 0.023, 0.025],
        samples_per_seg=4,
        radial_segs=14,
        name="bh_nail_shaft",
        material="item_steel_dark",
    )
    nail_head = cylinder(0.072, 0.066, 0.024, radial_segs=20, bevel=0.005, axis="y", name="bh_nail_head", material="item_aluminum").rotate_xyz(rx=-0.38, rz=0.52).translate(nx - 0.16, ny + 0.31, nz - 0.095)
    nail_slot = rounded_box(0.095, 0.010, 0.014, bevel=0.002, segs=1, name="bh_nail_slot", material="item_steel_dark").rotate_xyz(rx=-0.38, rz=0.52).translate(nx - 0.166, ny + 0.322, nz - 0.098)
    sc.add("anthill", [nail_shaft, nail_head, nail_slot])

    # Waving black cloth pennant tied just below the giant nail head, fluttering boldly right-down across the slope (700 ms)!
    _add_waving_flag(
        sc,
        prefix="bh",
        anchor=(nx - 0.105, ny + 0.225, nz - 0.055),
        yaw_deg=-24.0,
        pitch_deg=12.0,
        roll_deg=-28.0,
        length=0.40,
        base_height=0.165,
        mat="item_flag_black",
        period_sec=0.70,
        wave_amp=0.28,
        droop=-0.024,
    )

    # 2. Left slanted nail + pair of matte-black oval flat hardware buttons/washers along lower-left curve!
    ln_x, ln_z = -0.38, +0.16
    ln_y = max(0.04, surf_y(ln_x, ln_z) + 0.03)
    sc.add("anthill", [
        cylinder(0.014, 0.010, 0.16, radial_segs=10, axis="y", name="bh_left_nail", material="item_aluminum").rotate_xyz(rx=0.45, rz=-0.55).translate(ln_x, ln_y + 0.04, ln_z),
        cylinder(0.042, 0.038, 0.016, radial_segs=14, bevel=0.004, axis="y", name="bh_left_nail_h", material="item_aluminum").rotate_xyz(rx=0.45, rz=-0.55).translate(ln_x + 0.04, ln_y + 0.10, ln_z + 0.03),
    ])
    for idx, (wx, wz, rz) in enumerate(((-0.34, +0.34, 0.38), (-0.22, +0.44, 0.22))):
        wy = max(0.026, surf_y(wx, wz) + 0.020)
        disc = cylinder(0.064, 0.068, 0.028, radial_segs=20, bevel=0.006, axis="y", name=f"bh_washer_{idx}", material="item_rubber_dark").rotate_xyz(rx=0.38, rz=rz).translate(wx, wy, wz)
        hole = cylinder(0.024, 0.024, 0.032, radial_segs=14, axis="y", name=f"bh_washer_h_{idx}", material="item_soil_hole").rotate_xyz(rx=0.38, rz=rz).translate(wx, wy, wz)
        sc.add("anthill", [disc, hole])

    # 3. Three gleaming silver Phillips-head screws + chunky silver hex nut cradling the front center screw!
    screw_specs = (
        (-0.26, +0.30, 0.062, 0.42, +0.36, False),
        (-0.08, +0.44, 0.056, 0.56, +0.12, False),
        (+0.08, +0.36, 0.068, 0.40, -0.14, True),
    )
    for idx, (sx, sz, rad, rx, rz, with_nut) in enumerate(screw_specs):
        sy = max(0.040, surf_y(sx, sz) + 0.030)
        shaft = cylinder(rad * 0.40, rad * 0.36, 0.09, radial_segs=12, axis="y", name=f"bh_screw_s_{idx}", material="item_steel_dark").rotate_xyz(rx=rx, rz=rz).translate(sx, sy - 0.03, sz)
        head = cylinder(rad, rad * 0.92, 0.036, radial_segs=20, bevel=0.006, axis="y", name=f"bh_screw_h_{idx}", material="item_aluminum").rotate_xyz(rx=rx, rz=rz).translate(sx, sy + 0.01, sz)
        slot_a = rounded_box(rad * 1.18, 0.012, 0.016, bevel=0.002, segs=1, name=f"bh_slot_a_{idx}", material="item_steel_dark").rotate_xyz(rx=rx, rz=rz).translate(sx, sy + 0.028, sz)
        slot_b = rounded_box(0.016, 0.012, rad * 1.18, bevel=0.002, segs=1, name=f"bh_slot_b_{idx}", material="item_steel_dark").rotate_xyz(rx=rx, rz=rz).translate(sx, sy + 0.028, sz)
        sc.add("anthill", [shaft, head, slot_a, slot_b])
        if with_nut:
            nut = cylinder(0.102, 0.106, 0.044, radial_segs=6, bevel=0.008, axis="y", name="bh_center_nut", material="item_aluminum").rotate_xyz(rx=rx, rz=rz).translate(sx, sy - 0.012, sz)
            sc.add("anthill", nut)

    # 4. Long matte-black cylindrical rod/pen barrel angled cleanly along the bottom-right rim (`245:BLACKHILL`)!
    rod_a = (+0.02, max(0.032, surf_y(+0.02, +0.48) + 0.028), +0.48)
    rod_b = (+0.34, max(0.044, surf_y(+0.34, +0.26) + 0.036), +0.26)
    rod_mid = (0.5 * (rod_a[0] + rod_b[0]), 0.5 * (rod_a[1] + rod_b[1]) + 0.01, 0.5 * (rod_a[2] + rod_b[2]))
    rod = curved_tube([rod_a, rod_mid, rod_b], [0.024, 0.034, 0.044], samples_per_seg=4, radial_segs=14, cap_rings=3, name="bh_black_rod", material="item_rubber_dark")
    sc.add("anthill", rod)

    # 5. Domed gleaming silver fluted metal cap / thimble resting prominently on the far-right slope (`245:BLACKHILL`)!
    gx, gz = +0.38, +0.06
    gy = surf_y(gx, gz) + 0.046
    for p in _crimped_bottle_cap("bh_silver_cap", "item_aluminum", radius=0.112):
        sc.add("anthill", p.rotate_xyz(rx=0.28, rz=-0.42).translate(gx, gy, gz))
    dome = superellipsoid(0.095, 0.072, 0.095, e1=0.80, e2=0.92, u_segs=24, v_segs=14, name="bh_cap_dome", material="item_aluminum").rotate_xyz(rx=0.28, rz=-0.42).translate(gx + 0.024, gy + 0.034, gz + 0.010)
    sc.add("anthill", dome)
    return _finalize_item(sc, target_span=1.00)


def build_bluehill() -> SceneBuilder:
    """Blue Team Anthill with cocktail-sword waving blue flag, H2O cap, ribbed rubber skirt, paperclips & pushpins (`246:BLUEHILL`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_bluehill")
    surf_y = _build_anthill_base(sc, soil_mat="item_soil_mound", seed=246, loam_lo=(0.72, 0.68, 0.62), loam_hi=(1.02, 0.96, 0.88), pebble_count=28)

    # 1. Iconic sky-blue plastic cocktail sword plunged blade-down into front-center right crater lip (`246:BLUEHILL`)!
    sx, sz = +0.06, +0.16
    sy = surf_y(sx, sz) + 0.015
    sword_blade = rounded_box(0.032, 0.24, 0.010, bevel=0.003, segs=2, name="bl_sword_blade", material="item_sword_blue").rotate_xyz(rx=0.18, ry=0.20, rz=-0.10).translate(sx, sy + 0.07, sz)
    sword_hilt = cylinder(0.018, 0.018, 0.10, radial_segs=12, bevel=0.004, axis="y", name="bl_sword_hilt", material="item_sword_blue").rotate_xyz(rx=0.18, ry=0.20, rz=-0.10).translate(sx + 0.016, sy + 0.22, sz + 0.028)
    sword_guard = torus_arc(0.044, 0.010, u_segs=18, v_segs=8, plane="xy", name="bl_sword_guard", material="item_sword_blue").rotate_xyz(rx=0.18, ry=0.20, rz=-0.10).translate(sx - 0.020, sy + 0.195, sz + 0.022)
    sc.add("anthill", [sword_blade, sword_hilt, sword_guard])

    # Vivid royal-blue waving triangular pennant tied below the cocktail-sword hilt (exact 400 ms brisk flutter!)
    _add_waving_flag(
        sc,
        prefix="bl",
        anchor=(sx + 0.020, sy + 0.175, sz + 0.025),
        yaw_deg=-18.0,
        pitch_deg=6.0,
        roll_deg=-24.0,
        length=0.35,
        base_height=0.135,
        mat="item_flag_blue",
        period_sec=0.40,
        wave_amp=0.24,
        droop=-0.024,
    )

    # 2. Thick scalloped dark-indigo rubber band / tread collar hugging the entire front horseshoe skirt (`246:BLUEHILL`)!
    hose_pts = [
        (-0.44, max(0.032, surf_y(-0.44, +0.10) + 0.028), +0.10),
        (-0.36, max(0.036, surf_y(-0.36, +0.30) + 0.030), +0.30),
        (-0.14, max(0.036, surf_y(-0.14, +0.44) + 0.030), +0.44),
        (+0.14, max(0.036, surf_y(+0.14, +0.44) + 0.030), +0.44),
        (+0.36, max(0.034, surf_y(+0.36, +0.30) + 0.030), +0.30),
        (+0.44, max(0.032, surf_y(+0.44, +0.12) + 0.028), +0.12),
    ]
    hose = curved_tube(hose_pts, [0.032, 0.040, 0.044, 0.044, 0.038, 0.030], samples_per_seg=5, radial_segs=16, cap_rings=3, name="bl_rubber_skirt", material="item_rubber_blue")
    sc.add("anthill", hose)

    # 3. Scavenged crimped metallic silver & royal-blue "H2O" soda bottle cap on the far-right slope (`246:BLUEHILL`)!
    cx, cz = +0.40, +0.02
    cy = surf_y(cx, cz) + 0.048
    for p in _crimped_bottle_cap("bl_h2o_cap", "item_cap_h2o", radius=0.112, planar_top_uv=True):
        sc.add("anthill", p.rotate_xyz(rx=0.48, ry=-0.18, rz=-0.38).translate(cx, cy, cz))

    # 4. Six vivid colored office thumbtacks / pushpins jammed into the mound (`246:BLUEHILL`)!
    pin_specs = (
        (-0.36, +0.14, "item_plastic_pink", +1.15, +0.15),   # hot-pink pin sticking horizontally left
        (-0.16, +0.36, "item_plastic_blue", +0.32, +0.62),   # bright blue pin on front-left
        (+0.02, +0.38, "item_pin_green", -0.10, +0.58),      # emerald green pin front-center
        (+0.16, +0.16, "item_plastic_red", -0.45, +0.25),    # red pin behind flag
        (-0.32, -0.22, "item_bottle_teal", +0.85, -0.45),    # teal pushpin sticking upper-left
        (+0.22, -0.18, "item_plastic_blue", -0.82, -0.35),   # blue tack upper-right
    )
    for idx, (tx, tz, mat, rz, rx) in enumerate(pin_specs):
        ty = surf_y(tx, tz) + 0.028
        pin = cylinder(0.008, 0.004, 0.11, radial_segs=8, axis="y", name=f"bl_pin_s_{idx}", material="item_aluminum").rotate_xyz(rx=rx, rz=rz).translate(tx, ty + 0.02, tz)
        knob = cylinder(0.042, 0.032, 0.028, radial_segs=16, bevel=0.006, axis="y", name=f"bl_pin_h_{idx}", material=mat).rotate_xyz(rx=rx, rz=rz).translate(tx - 0.048 * math.sin(rz), ty + 0.052 * math.cos(rz), tz + 0.036 * math.sin(rx))
        sc.add("anthill", [pin, knob])

    # 5. Gleaming silver bent wire paperclips hooked cleanly across the front rubber collar (`246:BLUEHILL`)!
    for idx, (px, pz, ry, sc_clip) in enumerate(((+0.02, +0.46, 0.06, 1.45), (+0.26, +0.38, -0.42, 1.05), (-0.42, +0.28, 1.35, 0.90))):
        py = max(0.048, surf_y(px, pz) + 0.046)
        sc.add("anthill", _paperclip(f"bl_clip_{idx}", "item_aluminum", scale=sc_clip).rotate_xyz(rx=0.45, ry=ry).translate(px, py, pz))

    return _finalize_item(sc, target_span=1.00)


def build_redhill() -> SceneBuilder:
    """Red Team Anthill with warm clay crater, waving crimson pennant, 4 'soda' caps, giant paperclip & fork tines (`247:REDHILL`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_redhill")
    surf_y = _build_anthill_base(sc, soil_mat="item_soil_pink", seed=247, loam_lo=(0.82, 0.70, 0.66), loam_hi=(1.06, 0.94, 0.90), pebble_count=30)

    # 1. Bent silver spoon/pin flagpole sticking out upper-right crater rim + waving crimson streamer (`247:REDHILL`, 500 ms)!
    fx, fz = +0.24, -0.10
    fy = surf_y(fx, fz) + 0.015
    pole = curved_tube(
        [(fx - 0.02, fy - 0.03, fz + 0.04), (fx + 0.04, fy + 0.14, fz - 0.03), (fx + 0.09, fy + 0.28, fz - 0.09)],
        [0.018, 0.020, 0.024],
        samples_per_seg=4,
        radial_segs=12,
        name="rd_flag_pole",
        material="item_steel_dark",
    )
    pole_head = ellipsoid(0.046, 0.056, 0.020, u_segs=16, v_segs=10, name="rd_pole_head", material="item_aluminum").rotate_xyz(rx=0.25, ry=-0.45, rz=-0.35).translate(fx + 0.105, fy + 0.305, fz - 0.105)
    sc.add("anthill", [pole, pole_head])

    # Vivid crimson tapered pennant flying smoothly left-down across the pink clay slope (exact 500 ms wave!)
    _add_waving_flag(
        sc,
        prefix="rd",
        anchor=(fx + 0.052, fy + 0.195, fz - 0.042),
        yaw_deg=142.0,
        pitch_deg=10.0,
        roll_deg=-34.0,
        length=0.42,
        base_height=0.150,
        mat="item_flag_red",
        period_sec=0.50,
        wave_amp=0.26,
        droop=-0.028,
    )

    # 2. Four iconic crimped crimson "soda" soda bottle caps framing the base (`247:REDHILL`)!
    cap_specs = (
        (-0.34, +0.28, 0.112, 0.40, +0.22, +0.52),
        (-0.14, +0.40, 0.116, 0.36, +0.16, +0.20),
        (+0.40, +0.06, 0.114, 0.38, -0.22, -0.44),
        (-0.28, -0.32, 0.104, -0.32, +0.25, +0.36),
    )
    for idx, (cx, cz, rad, rx, ry, rz) in enumerate(cap_specs):
        cy = max(0.042, surf_y(cx, cz) + 0.038)
        for p in _crimped_bottle_cap(f"rd_cap_{idx}", "item_cap_soda", radius=rad, planar_top_uv=True):
            sc.add("anthill", p.rotate_xyz(rx=rx, ry=ry, rz=rz).translate(cx, cy, cz))

    # 3. Prominent giant shiny silver oval paperclip resting cleanly on the front-right clay slope (`247:REDHILL`)!
    px, pz = +0.16, +0.36
    py = max(0.036, surf_y(px, pz) + 0.032)
    clip = _paperclip("rd_paperclip", "item_aluminum", scale=1.65).rotate_xyz(rx=0.46, ry=0.68).translate(px, py, pz)
    sc.add("anthill", clip)

    # 4. Parallel silver wire fork tines / pins sticking out of the far-left embankment above the bottle caps!
    for k, dz in enumerate((-0.04, 0.0, +0.04)):
        tx, tz = -0.36 + k * 0.02, +0.08 + dz
        ty = surf_y(tx, tz) + 0.03
        tine = cylinder(0.009, 0.006, 0.24, radial_segs=8, axis="y", name=f"rd_tine_{k}", material="item_aluminum").rotate_xyz(rx=0.32, rz=-0.38).translate(tx, ty + 0.08, tz)
        sc.add("anthill", tine)

    return _finalize_item(sc, target_span=1.00)


# ---------------------------------------------------------------------------
# All 4 Team Planted Bombs (`129..132`) & Blazing Wall Fire (`134:wallup04`)
# ---------------------------------------------------------------------------

def _build_team_bomb(name: str, body_mat: str, accent_mat: str) -> SceneBuilder:
    """High-craft tapered ordnance canister matching `129..132`: riveted foot, team enamel casing, dark-crimson shoulder, bolted magenta blast cap, braided fuse & real-time GPU sparkler."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    sc.set_node_transform("bomb_root", translation=(0.0, 0.0, 0.0))

    # 1. Rounded riveted crimson-steel lower foot & gunmetal clamp ring (authentic tapered chalice base of `129..132`).
    base_prof = [
        (0.000, 0.000),
        (0.094, 0.000),
        (0.114, 0.016),
        (0.122, 0.048),
        (0.118, 0.066),
    ]
    base_cap = lathe(base_prof, radial_segs=32, cap_bottom=False, cap_top=True, axis="y", name=f"{name}_base", material="item_bomb_crimson")
    foot_ring = torus_arc(0.120, 0.009, u_segs=32, v_segs=10, plane="xz", name=f"{name}_foot_ring", material="item_steel_dark").translate(0.0, 0.062, 0.0)

    # 2. Tapered upward-flaring team-colored glossy enamel ordnance body with machined center channel & bolt studs!
    body_prof = [
        (0.116, 0.064),
        (0.128, 0.105),
        (0.136, 0.148),
        (0.132, 0.156),
        (0.138, 0.164),
        (0.146, 0.215),
        (0.150, 0.246),
    ]
    canister = gradient_y_tint(
        lathe(body_prof, radial_segs=36, cap_bottom=True, cap_top=True, axis="y", name=f"{name}_body", material=body_mat),
        (0.84, 0.84, 0.86),
        (1.00, 1.00, 1.00),
    )

    # 3. Iconic deep crimson warning shoulder band present under the purple lid on ALL 4 1998 bombs (`129..132`)!
    shoulder_prof = [
        (0.148, 0.244),
        (0.156, 0.260),
        (0.166, 0.308),
        (0.162, 0.328),
    ]
    crimson_band = lathe(shoulder_prof, radial_segs=36, cap_bottom=True, cap_top=True, axis="y", name=f"{name}_shoulder", material="item_bomb_crimson")
    mid_clamp = torus_arc(0.152, 0.0085, u_segs=36, v_segs=10, plane="xz", name=f"{name}_mid_clamp", material="item_steel_dark").translate(0.0, 0.246, 0.0)

    # 4. Signature overhanging domed magenta-purple metallic blast cap with dark rim seal, 8 perimeter hex bolts & brass fuse bushing!
    cap_prof = [
        (0.156, 0.324),
        (0.184, 0.336),
        (0.190, 0.362),
        (0.184, 0.392),
        (0.166, 0.412),
        (0.126, 0.422),
        (0.068, 0.418),
        (0.044, 0.410),
        (0.000, 0.408),
    ]
    purple_cap = gradient_y_tint(
        lathe(cap_prof, radial_segs=40, cap_bottom=True, cap_top=False, axis="y", name=f"{name}_collar", material="item_bomb_purple"),
        (0.76, 0.72, 0.82),
        (1.04, 1.00, 1.04),
    )
    cap_seal = torus_arc(0.172, 0.009, u_segs=36, v_segs=10, plane="xz", name=f"{name}_cap_seal", material="item_steel_dark").translate(0.0, 0.328, 0.0)

    parts: List[MeshPart] = [base_cap, foot_ring, canister, crimson_band, mid_clamp, purple_cap, cap_seal]

    # Machined armor hardware: 8 lower foot rivets, 4 vertical armored strap ribs & 8 top hex bolts around the blast cap!
    for k in range(8):
        ang = 2.0 * math.pi * k / 8.0 + math.pi / 8.0
        ca, sa = math.cos(ang), math.sin(ang)
        parts.append(
            sphere(0.0085, u_segs=10, v_segs=8, name=f"{name}_foot_rivet_{k}", material="item_steel_dark")
            .translate(0.124 * ca, 0.044, 0.124 * sa)
        )
        parts.append(
            cylinder(0.011, 0.011, 0.014, radial_segs=6, bevel=0.002, axis="y", name=f"{name}_cap_bolt_{k}", material="item_steel_dark")
            .rotate_xyz(ry=ang)
            .translate(0.136 * ca, 0.422, 0.136 * sa)
        )
    for k in range(4):
        ang = 2.0 * math.pi * k / 4.0 + math.pi / 4.0
        ca, sa = math.cos(ang), math.sin(ang)
        strap = (
            rounded_box(0.018, 0.178, 0.012, bevel=0.003, segs=2, name=f"{name}_strap_{k}", material="item_steel_dark")
            .rotate_xyz(rx=+0.184)
            .rotate_xyz(ry=math.atan2(ca, sa))
            .translate(0.135 * ca, 0.155, 0.135 * sa)
        )
        parts.append(strap)

    # Stepped gunmetal + knurled brass hex fuse bushing seated cleanly in the recessed blast cap dish!
    grommet = cylinder(0.048, 0.042, 0.022, radial_segs=20, bevel=0.004, axis="y", name=f"{name}_grommet", material="item_steel_dark").translate(0.0, 0.420, 0.0)
    brass_nut = cylinder(0.032, 0.030, 0.020, radial_segs=6, bevel=0.003, axis="y", name=f"{name}_brass_nut", material="item_gear_brass").translate(0.0, 0.436, 0.0)
    parts.extend([grommet, brass_nut])

    # 5. Authentic twisted braided cotton fuse cord sweeping gracefully up-left then arcing up-right (`129..132`)!
    fuse_pts = [
        (0.000, 0.440, 0.000),
        (-0.026, 0.492, -0.006),
        (-0.024, 0.548, 0.004),
        (0.024, 0.592, 0.010),
        (0.072, 0.596, 0.008),
    ]

    def _braid_fuse(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ang = np.arctan2(p[:, 2], p[:, 0])
        h = p[:, 1]
        twist = 0.0018 * np.sin(h * 115.0 + ang * 3.0)
        r = np.maximum(1e-4, np.hypot(p[:, 0], p[:, 2]))
        p[:, 0] += twist * (p[:, 0] / r)
        p[:, 2] += twist * (p[:, 2] / r)
        return p

    fuse = tint_part(
        curved_tube(fuse_pts, [0.0155, 0.0148, 0.0142, 0.0132, 0.0112], samples_per_seg=5, radial_segs=12, cap_rings=3, name=f"{name}_fuse", material="item_fuse_cord")
        .deform(_braid_fuse),
        lambda pos, _n: np.where(
            pos[:, 0:1] > 0.052,
            np.array([[0.32, 0.24, 0.20]]),
            np.array([[0.98, 0.96, 0.90]]) - 0.14 * np.clip((pos[:, 0:1] - 0.020) / 0.040, 0.0, 1.0),
        ) * np.ones((len(pos), 3), dtype=np.float64),
    )
    # Charred blackened ash collar + glowing incandescent molten ember bead right at the burning fuse end!
    tip_pos = (0.074, 0.596, 0.008)
    char_ring = sphere(0.0135, u_segs=12, v_segs=10, name=f"{name}_char_tip", material="item_charcoal_ember").translate(0.068, 0.596, 0.008)
    ember_bead = sphere(0.013, u_segs=12, v_segs=10, name=f"{name}_ember_bead", material="item_flame_white").translate(*tip_pos)
    parts.extend([fuse, char_ring, ember_bead])
    sc.add("bomb_root", parts)

    # 6. Real-time WebGL2 GPU Blazing Sparkler emitter (`item_spark_volume`) anchored right at the burning fuse tip!
    #    - In Ants WebGL2 (`viewer_fx.js`), spawns a sizzling incandescent molten core, 88 ballistic gravity-arcing
    #      golden-white spark streaks, 26 crackling starburst flashes, and 22 rising curls of gunpowder smoke!
    #    - In external glTF viewers, renders as a warm glowing emissive spark bead with smooth pulsing Idle clip.
    spark_emitter = sphere(0.058, u_segs=12, v_segs=10, name=f"{name}_spark_emitter", material="item_spark_volume")
    sc.add("bomb_spark", spark_emitter)
    sc.set_node_parent("bomb_spark", "bomb_root")
    sc.set_node_transform("bomb_spark", translation=tip_pos)

    times = [0.00, 0.25, 0.50, 0.75, 1.00]
    sc.add_animation(
        "Idle",
        [
            AnimationChannel("bomb_spark", "scale", times, [(1.00, 1.00, 1.00), (1.06, 1.06, 1.06), (0.96, 0.96, 0.96), (1.04, 1.04, 1.04), (1.00, 1.00, 1.00)]),
        ],
    )
    return _finalize_item(sc, target_span=0.92)


def build_redbomb() -> SceneBuilder:
    return _build_team_bomb("item_redbomb", "item_bomb_red", "item_bomb_crimson")


def build_greenbomb() -> SceneBuilder:
    return _build_team_bomb("item_greenbomb", "item_bomb_green", "item_bomb_crimson")


def build_blackbomb() -> SceneBuilder:
    return _build_team_bomb("item_blackbomb", "item_bomb_black", "item_bomb_crimson")


def build_bluebomb() -> SceneBuilder:
    return _build_team_bomb("item_bluebomb", "item_bomb_blue", "item_bomb_crimson")


def build_wallup04() -> SceneBuilder:
    """Blazing 3D Fire Wall: scorched charred wood & pulsing ember coal bed + GPU volumetric fire emitter (`134:wallup04`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_wallup04")
    sc.set_node_transform("wallup04", translation=(0.0, 0.0, 0.0))

    # 1. Grounded scorched earth crater, ash mound & charred kindling sticks matching the dark base of `134:wallup04`.
    def _scorch_deform(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ang = np.arctan2(p[:, 2], p[:, 0])
        r = np.hypot(p[:, 0], p[:, 2])
        jagged = 1.0 + 0.14 * np.sin(5.0 * ang + 0.4) + 0.08 * np.cos(8.0 * ang - 0.7)
        p[:, 0] *= jagged
        p[:, 2] *= jagged
        top_mask = p[:, 1] > 0.0
        bump = 0.018 * np.sin(p[:, 0] * 26.0 + p[:, 2] * 19.0) * np.cos(p[:, 2] * 22.0 - p[:, 0] * 13.0)
        p[:, 1] = np.where(top_mask, p[:, 1] + bump * np.clip(1.0 - r * 1.3, 0.2, 1.0), p[:, 1] * 0.10)
        return p

    base_parts: List[MeshPart] = []
    scorch_bed = (
        ellipsoid(0.36, 0.056, 0.27, u_segs=28, v_segs=12, name="fire_scorch_bed", material="item_charcoal_ember")
        .deform(_scorch_deform)
        .translate(0.0, 0.030, 0.0)
    )
    # Warm underlit radial ash-to-ember tint on the scorch mound so the fire bed glows hot in the center!
    tint_part(
        scorch_bed,
        lambda pos, _nrm: np.column_stack([
            0.38 + 0.62 * np.clip(1.0 - np.hypot(pos[:, 0] / 0.34, pos[:, 2] / 0.25), 0.0, 1.0),
            0.26 + 0.24 * np.clip(1.0 - np.hypot(pos[:, 0] / 0.34, pos[:, 2] / 0.25), 0.0, 1.0),
            0.20 + 0.05 * np.clip(1.0 - np.hypot(pos[:, 0] / 0.34, pos[:, 2] / 0.25), 0.0, 1.0),
        ]),
    )
    base_parts.append(scorch_bed)

    # Stacked charred firewood sticks & jagged burnt roots anchored around the scorch bed (`wallup04` dark brown base!).
    kindling_specs = (
        (-0.22, 0.036, +0.11, 0.24, 0.028, +0.20, +0.42, +0.14),
        (+0.21, 0.036, +0.10, 0.25, 0.028, -0.18, -0.46, -0.15),
        (-0.08, 0.048, +0.14, 0.28, 0.029, +0.15, +0.12, +0.18),
        (+0.10, 0.044, -0.13, 0.26, 0.028, -0.14, -0.28, -0.15),
        (-0.14, 0.042, -0.12, 0.24, 0.026, +0.18, +0.58, +0.12),
        (+0.02, 0.030, +0.18, 0.16, 0.023, +0.08, +1.35, -0.10),
        (-0.28, 0.028, -0.02, 0.17, 0.022, -0.10, +0.85, +0.16),
        (+0.28, 0.028, -0.04, 0.17, 0.022, +0.12, -0.78, -0.14),
        (0.00, 0.056, +0.02, 0.30, 0.027, +0.06, -0.22, +0.10),
        (-0.02, 0.054, -0.03, 0.28, 0.026, -0.08, +0.62, -0.12),
    )
    for idx, (kx, ky, kz, klen, krad, rx, ry, rz) in enumerate(kindling_specs):
        log = (
            curved_tube(
                [(-0.5 * klen, 0.0, 0.0), (0.0, 0.012, 0.008), (+0.5 * klen, -0.005, -0.005)],
                [krad * 0.78, krad, krad * 0.62],
                samples_per_seg=2,
                radial_segs=8,
                name=f"fire_char_stick_{idx}",
                material="item_charcoal_ember",
            )
            .rotate_xyz(rx=rx, ry=ry, rz=rz)
            .translate(kx, ky, kz)
        )
        base_parts.append(log)

    # Craggy blackened charcoal lumps framing the glowing coal pit.
    char_lumps = (
        (-0.25, 0.042, +0.07, 0.046),
        (+0.25, 0.042, +0.06, 0.044),
        (-0.18, 0.046, -0.13, 0.048),
        (+0.19, 0.046, -0.12, 0.046),
        (-0.04, 0.040, +0.19, 0.042),
        (+0.07, 0.040, -0.18, 0.042),
    )
    for idx, (lx, ly, lz, lrad) in enumerate(char_lumps):
        base_parts.append(
            superellipsoid(lrad, lrad * 0.62, lrad * 0.84, e1=0.62, e2=0.68, u_segs=8, v_segs=6, name=f"fire_char_lump_{idx}", material="item_charcoal_ember")
            .rotate_xyz(rx=0.2 * idx, ry=0.9 * idx)
            .translate(lx, ly, lz)
        )
    sc.add("wallup04", base_parts)

    # 2. Smoldering glowing ember coals tucked deeply inside the charred kindling pit (subtle organic heat breath!).
    coal_specs = (
        (-0.15, 0.044, +0.08, 0.036, "item_flame_spark"),
        (-0.07, 0.052, +0.09, 0.044, "item_flame_spark"),
        (+0.04, 0.054, +0.08, 0.046, "item_flame_spark"),
        (+0.14, 0.046, +0.07, 0.036, "item_flame_spark"),
        (+0.18, 0.040, +0.01, 0.032, "item_flame_spark"),
        (-0.18, 0.040, -0.02, 0.032, "item_flame_spark"),
        (-0.10, 0.048, -0.09, 0.040, "item_flame_spark"),
        (+0.08, 0.050, -0.09, 0.042, "item_flame_spark"),
        (-0.04, 0.060, +0.01, 0.056, "item_flame_core"),
        (+0.05, 0.058, -0.01, 0.054, "item_flame_core"),
        (0.00, 0.058, +0.03, 0.052, "item_flame_core"),
    )
    sc.set_node_parent("fire_coals", "wallup04")
    sc.set_node_transform("fire_coals", translation=(0.0, 0.0, 0.0))
    coal_parts: List[MeshPart] = []
    for idx, (cx, cy, cz, crad, cmat) in enumerate(coal_specs):
        c_mesh = (
            superellipsoid(crad, crad * 0.56, crad * 0.86, e1=0.64, e2=0.70, u_segs=8, v_segs=6, name=f"fire_coal_{idx}", material=cmat)
            .rotate_xyz(ry=idx * 0.7)
            .translate(cx, cy, cz)
        )
        # Glowing hot underside -> darker smoldering ash-crusted top on outer coals so they look like real burning embers!
        if cmat == "item_flame_spark":
            gradient_y_tint(c_mesh, (1.00, 0.52, 0.14), (0.48, 0.16, 0.06))
        coal_parts.append(c_mesh)
    sc.add("fire_coals", coal_parts)

    # 3. Data-driven 3D volumetric fire emitter envelope (`item_fire_volume`):
    #    - Sets exact camera-framing bounds (0.78 x 0.92 x 0.56) and serves as a clean translucent flame silhouette in external glTF viewers.
    #    - In Ants WebGL2 (`viewer_render.js`), automatically spawns 280+ real-time rising GPU fire plumes, curling wisps, smoke & rising sparks!
    def _emitter_envelope(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        u = np.clip((p[:, 1] + 0.44) / 0.88, 0.0, 1.0)
        taper = (0.82 + 0.30 * np.sin(np.minimum(1.0, u * 1.35) * math.pi * 0.65)) * ((1.0 - u) ** 0.62)
        p[:, 0] *= np.maximum(0.04, taper)
        p[:, 2] *= np.maximum(0.04, taper)
        p[:, 1] = 0.05 + u * 0.87
        return p

    sc.set_node_parent("fire_volume", "wallup04")
    sc.set_node_transform("fire_volume", translation=(0.0, 0.0, 0.0))
    sc.add(
        "fire_volume",
        [ellipsoid(0.36, 0.44, 0.25, u_segs=14, v_segs=12, name="wallup04_fire_emitter", material="item_fire_volume").deform(_emitter_envelope)],
    )

    # Gentle organic breathing pulse on the glowing coal hearth (actual rising fire & dissipating sparks run smoothly at 60 FPS on GPU!).
    times = [0.00, 0.35, 0.70, 1.05, 1.40]
    sc.add_animation(
        "Idle",
        [
            AnimationChannel(
                "fire_coals",
                "scale",
                times,
                [(1.00, 1.00, 1.00), (1.03, 1.07, 1.03), (0.98, 0.96, 0.98), (1.04, 1.08, 1.04), (1.00, 1.00, 1.00)],
            ),
        ],
        replace=True,
    )
    return _finalize_item(sc, target_span=0.92)
