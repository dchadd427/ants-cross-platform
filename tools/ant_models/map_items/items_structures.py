"""Realistic 3D sculptors for 1998 Ants team anthills, swimmer bridges, planted bombs, and fire wall (Wave 2).

Covers:
- `BLACKHILL` (245), `BLUEHILL` (246), `REDHILL` (247): Remaining 3 team anthills with waving flags & authentic decor
- `bridge1`..`bridge4`, `bridge4b` (34–38): Progressive 5-stage swimmer mud & river-stone water bridge
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
    """Reusable realistic 21-tooth crimped metallic soda bottle cap with optional crisp planar printed-logo UVs."""
    def _crimp(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ang = np.arctan2(p[:, 2], p[:, 0])
        bot = np.clip(-p[:, 1] / 0.024, 0.0, 1.0)
        flute = 1.0 + 0.12 * np.cos(ang * 21.0) * bot
        p[:, 0] *= flute
        p[:, 2] *= flute
        return p

    metal = cylinder(radius, radius * 1.13, 0.042, radial_segs=42, bevel=0.005, axis="y", name=f"{prefix}_metal", material="item_aluminum").deform(_crimp)
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
# Swimmer-built progressive mud/river-stone water bridges (`34..38`)
# ---------------------------------------------------------------------------

def _build_bridge_stage(name: str, *, blobs: List[Tuple[float, float, float, float, float]], has_water: bool, seed: int) -> SceneBuilder:
    """Build one stage of the swimmer's mud-and-cobble water bridge (`bridge1`..`bridge4b`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)

    if has_water:
        def _ripple(pos: np.ndarray) -> np.ndarray:
            p = pos.copy()
            ang = np.arctan2(p[:, 2], p[:, 0])
            edge = 1.0 + 0.14 * np.sin(ang * 4.0 + seed) + 0.09 * np.cos(ang * 7.0 - seed)
            p[:, 0] *= edge
            p[:, 2] *= edge
            return p

        water = cylinder(0.36, 0.36, 0.018, radial_segs=32, bevel=0.004, axis="y", name=f"{name}_water", material="item_water_ripple").deform(_ripple).translate(0.0, 0.010, 0.0)
        sc.add("bridge", water)

    for idx, (bx, bz, rx, ry, rz) in enumerate(blobs):
        def _mud_lump(pos: np.ndarray, k=idx) -> np.ndarray:
            p = pos.copy()
            r = np.maximum(1e-5, np.linalg.norm(p, axis=1, keepdims=True))
            n = p / r
            lumpy = 0.016 * np.sin(p[:, 0] * 18.0 + k * 1.7 + seed) * np.cos(p[:, 2] * 17.0 - k)
            p = p + lumpy[:, None] * n
            p[:, 1] = np.where(p[:, 1] < -ry * 0.45, -ry * 0.45 + 0.12 * (p[:, 1] + ry * 0.45), p[:, 1])
            return p

        lump = gradient_y_tint(
            superellipsoid(rx, ry, rz, e1=0.68, e2=0.82, u_segs=26, v_segs=16, name=f"{name}_mud_{idx}", material="item_soil_mud")
            .deform(_mud_lump)
            .translate(bx, ry * 0.55 + (0.012 if has_water else 0.0), bz),
            (0.62, 0.58, 0.54),
            (1.04, 0.98, 0.90),
            noise_amp=0.06,
        )
        sc.add("bridge", lump)

    return _finalize_item(sc, target_span=0.90)


def build_bridge1() -> SceneBuilder:
    """Bridge stage 1: tiny emerging mud shoal inside blue water ripple (`34:bridge1`)."""
    return _build_bridge_stage("item_bridge1", blobs=[(0.00, 0.00, 0.14, 0.055, 0.12)], has_water=True, seed=34)


def build_bridge2() -> SceneBuilder:
    """Bridge stage 2: wet clustered river stones emerging from water (`35:bridge2`)."""
    return _build_bridge_stage(
        "item_bridge2",
        blobs=[(-0.08, -0.04, 0.17, 0.075, 0.15), (+0.10, +0.06, 0.15, 0.068, 0.14), (+0.02, -0.10, 0.12, 0.058, 0.11)],
        has_water=True,
        seed=35,
    )


def build_bridge3() -> SceneBuilder:
    """Bridge stage 3: three broad overlapping dark cobble mud mounds (`36:bridge3`)."""
    return _build_bridge_stage(
        "item_bridge3",
        blobs=[(-0.14, -0.06, 0.22, 0.095, 0.18), (+0.14, -0.08, 0.21, 0.092, 0.19), (-0.02, +0.16, 0.20, 0.088, 0.17)],
        has_water=True,
        seed=36,
    )


def build_bridge4() -> SceneBuilder:
    """Bridge stage 4: solid packed walkable mud-bridge crossing pad (`37:bridge4`)."""
    return _build_bridge_stage(
        "item_bridge4",
        blobs=[
            (0.00, 0.00, 0.38, 0.105, 0.38),
            (-0.18, -0.18, 0.22, 0.098, 0.22),
            (+0.18, -0.16, 0.21, 0.095, 0.21),
            (-0.16, +0.18, 0.21, 0.096, 0.21),
            (+0.18, +0.18, 0.22, 0.098, 0.22),
        ],
        has_water=False,
        seed=37,
    )


def build_bridge4b() -> SceneBuilder:
    """Bridge stage 4 variant: sculpted scalloped mud-bridge pad (`38:bridge4b`)."""
    return _build_bridge_stage(
        "item_bridge4b",
        blobs=[
            (0.00, 0.00, 0.36, 0.108, 0.36),
            (-0.20, -0.10, 0.23, 0.100, 0.24),
            (+0.16, -0.20, 0.22, 0.096, 0.22),
            (+0.20, +0.14, 0.24, 0.102, 0.23),
            (-0.12, +0.20, 0.22, 0.096, 0.21),
        ],
        has_water=False,
        seed=38,
    )


# ---------------------------------------------------------------------------
# All 4 Team Planted Bombs (`129..132`) & Blazing Wall Fire (`134:wallup04`)
# ---------------------------------------------------------------------------

def _build_team_bomb(name: str, body_mat: str, accent_mat: str) -> SceneBuilder:
    """Team-colored cylindrical planted bomb with beveled purple collar, curving fuse & live animated spark."""
    register_item_materials()
    sc = SceneBuilder(name=name)

    # Tapered cartoon dynamite canister with bold team stripes & dark metal bottom cap
    base_cap = cylinder(0.118, 0.106, 0.055, radial_segs=26, bevel=0.008, axis="y", name=f"{name}_base", material=accent_mat).translate(0.0, 0.028, 0.0)
    canister = cylinder(0.132, 0.118, 0.270, radial_segs=28, bevel=0.010, axis="y", name=f"{name}_body", material=body_mat).translate(0.0, 0.185, 0.0)
    # Iconic wide purple top bevel flange (`129..132`)!
    collar = cylinder(0.154, 0.140, 0.088, radial_segs=28, bevel=0.014, axis="y", name=f"{name}_collar", material="item_bomb_purple").translate(0.0, 0.340, 0.0)
    plug = cylinder(0.042, 0.046, 0.034, radial_segs=16, bevel=0.005, axis="y", name=f"{name}_plug", material="item_steel_dark").translate(0.0, 0.396, 0.0)
    fuse = curved_tube(
        [(0.0, 0.405, 0.0), (0.015, 0.485, 0.01), (0.055, 0.545, 0.02), (0.105, 0.525, 0.01)],
        [0.014, 0.013, 0.012, 0.010],
        samples_per_seg=4,
        radial_segs=10,
        name=f"{name}_fuse",
        material="item_fuse_cord",
    )
    sc.set_node_transform("bomb_root", translation=(0.0, 0.0, 0.0))
    sc.add("bomb_root", [base_cap, canister, collar, plug, fuse])

    # Sizzling animated yellow-orange spark starburst mounted on its own child node for pulsing!
    spark_core = sphere(0.032, u_segs=14, v_segs=10, name=f"{name}_spark_core", material="item_flame_core")
    spark_rays: List[MeshPart] = [spark_core]
    for k in range(6):
        ang = 2.0 * math.pi * k / 6.0
        ray = cone(0.012, 0.055, radial_segs=8, name=f"{name}_ray_{k}", material="item_flame_outer").rotate_xyz(rx=math.pi * 0.5, rz=ang).translate(0.032 * math.sin(ang), 0.032 * math.cos(ang), 0.0)
        spark_rays.append(ray)
    sc.add("bomb_spark", spark_rays)
    sc.set_node_parent("bomb_spark", "bomb_root")
    sc.set_node_transform("bomb_spark", translation=(0.105, 0.525, 0.01))

    # Crisp 2-frame authentic sizzling spark flicker matching `129..132`!
    times = [0.00, 0.12, 0.24, 0.36]
    sc.add_animation(
        "Idle",
        [
            AnimationChannel("bomb_spark", "scale", times, [(0.88, 0.88, 0.88), (1.28, 1.28, 1.28), (0.94, 0.94, 0.94), (0.88, 0.88, 0.88)]),
            AnimationChannel("bomb_spark", "rotation", times, [quat_from_euler_xyz(rz=a) for a in (0.0, 0.55, 1.10, 0.0)]),
        ],
    )
    return _finalize_item(sc, target_span=0.92)


def build_redbomb() -> SceneBuilder:
    return _build_team_bomb("item_redbomb", "item_bomb_red", "item_steel_dark")


def build_greenbomb() -> SceneBuilder:
    return _build_team_bomb("item_greenbomb", "item_bomb_green", "item_bomb_red")


def build_blackbomb() -> SceneBuilder:
    return _build_team_bomb("item_blackbomb", "item_bomb_black", "item_bomb_red")


def build_bluebomb() -> SceneBuilder:
    return _build_team_bomb("item_bluebomb", "item_bomb_blue", "item_bomb_red")


def build_wallup04() -> SceneBuilder:
    """Blazing multi-layered 3D Fire Wall with scorched coal bed, 4 temperature shells, close rising sparks & 5-beat roar (`134:wallup04`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_wallup04")
    sc.set_node_transform("wallup04", translation=(0.0, 0.0, 0.0))

    # 1. Grounded scorched earth / charred kindling & glowing coal bed matching the dark base of `134:wallup04`!
    def _scorch_deform(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ang = np.arctan2(p[:, 2], p[:, 0])
        r = np.hypot(p[:, 0], p[:, 2])
        jagged = 1.0 + 0.14 * np.sin(5.0 * ang + 0.4) + 0.08 * np.cos(8.0 * ang - 0.7)
        p[:, 0] *= jagged
        p[:, 2] *= jagged
        top_mask = p[:, 1] > 0.0
        bump = 0.016 * np.sin(p[:, 0] * 26.0 + p[:, 2] * 19.0) * np.cos(p[:, 2] * 22.0 - p[:, 0] * 13.0)
        p[:, 1] = np.where(top_mask, p[:, 1] + bump * np.clip(1.0 - r * 1.4, 0.2, 1.0), p[:, 1] * 0.12)
        return p

    base_parts: List[MeshPart] = []
    scorch_bed = (
        ellipsoid(0.32, 0.056, 0.26, u_segs=26, v_segs=12, name="fire_scorch_bed", material="item_charcoal_ember")
        .deform(_scorch_deform)
        .translate(0.0, 0.032, 0.0)
    )
    tint_part(
        scorch_bed,
        lambda pos, _nrm: np.column_stack([
            0.58 + 0.42 * np.clip(1.0 - np.hypot(pos[:, 0] / 0.31, pos[:, 2] / 0.25), 0.0, 1.0),
            0.38 + 0.18 * np.clip(1.0 - np.hypot(pos[:, 0] / 0.31, pos[:, 2] / 0.25), 0.0, 1.0),
            0.28 + 0.06 * np.clip(1.0 - np.hypot(pos[:, 0] / 0.31, pos[:, 2] / 0.25), 0.0, 1.0),
        ]),
    )
    base_parts.append(scorch_bed)

    # Charred kindling sticks & protruding scorched wood stubs framing the bottom edge (`wallup04` dark brown base!).
    kindling_specs = (
        (-0.20, 0.034, +0.11, 0.18, 0.025, +0.22, +0.38, +0.12),
        (+0.20, 0.034, +0.10, 0.19, 0.025, -0.20, -0.42, -0.14),
        (-0.07, 0.046, +0.15, 0.22, 0.026, +0.16, +0.12, +0.16),
        (+0.09, 0.042, -0.13, 0.21, 0.025, -0.14, -0.26, -0.14),
        (-0.14, 0.040, -0.12, 0.20, 0.024, +0.18, +0.54, +0.10),
        (+0.02, 0.028, +0.18, 0.14, 0.022, +0.08, +1.35, -0.10),
        (-0.25, 0.028, -0.02, 0.14, 0.020, -0.10, +0.85, +0.16),
        (+0.25, 0.028, -0.04, 0.14, 0.020, +0.12, -0.78, -0.14),
    )
    for idx, (kx, ky, kz, klen, krad, rx, ry, rz) in enumerate(kindling_specs):
        log = (
            curved_tube(
                [(-0.5 * klen, 0.0, 0.0), (0.0, 0.012, 0.008), (+0.5 * klen, -0.005, -0.005)],
                [krad * 0.78, krad, krad * 0.65],
                samples_per_seg=2,
                radial_segs=7,
                name=f"fire_char_stick_{idx}",
                material="item_charcoal_ember",
            )
            .rotate_xyz(rx=rx, ry=ry, rz=rz)
            .translate(kx, ky, kz)
        )
        base_parts.append(log)

    # Smoldering glowing ember coals nestled right inside the charred wood bed!
    coal_specs = (
        (-0.18, 0.052, +0.13, 0.046, "item_flame_spark"),
        (-0.08, 0.060, +0.15, 0.052, "item_flame_core"),
        (+0.04, 0.062, +0.14, 0.054, "item_flame_core"),
        (+0.16, 0.056, +0.12, 0.046, "item_flame_spark"),
        (-0.22, 0.046, -0.02, 0.042, "item_flame_spark"),
        (+0.22, 0.046, -0.02, 0.042, "item_flame_spark"),
        (-0.10, 0.054, -0.14, 0.048, "item_flame_spark"),
        (+0.09, 0.056, -0.14, 0.050, "item_flame_core"),
        (0.00, 0.068, 0.00, 0.078, "item_flame_core"),
    )
    for idx, (cx, cy, cz, crad, cmat) in enumerate(coal_specs):
        base_parts.append(
            superellipsoid(crad, crad * 0.64, crad * 0.90, e1=0.68, e2=0.72, u_segs=8, v_segs=6, name=f"fire_coal_{idx}", material=cmat)
            .rotate_xyz(ry=idx * 0.7)
            .translate(cx, cy, cz)
        )
    sc.add("wallup04", base_parts)

    # 2. Sculpted billowing turbulent 3D flame plume: broad rounded burning root, spiral flutes & dramatic licking crest!
    def _turbulent_plume(
        name: str,
        *,
        rx: float,
        rz: float,
        height: float,
        bend_x: float,
        bend_z: float,
        curl_amp: float,
        flute_k: float,
        flute_amp: float,
        phase: float,
        mat: str,
        y0: float = 0.042,
        tint_low: Tuple[float, float, float] | None = None,
        tint_high: Tuple[float, float, float] | None = None,
    ) -> MeshPart:
        def _deform_plume(pos: np.ndarray) -> np.ndarray:
            p = pos.copy()
            u = np.clip((p[:, 1] + height * 0.5) / max(1e-5, height), 0.0, 1.0)
            theta = np.arctan2(p[:, 2], p[:, 0])
            # Full billowing fire volume through lower-mid belly, sweeping sharply into a licking tip at u=1!
            rad_env = (0.76 + 0.56 * np.sin(np.minimum(1.0, u * 1.32) * math.pi * 0.66)) * ((1.0 - u) ** 0.72)
            rad_env = np.maximum(0.015, rad_env)
            flutes = 1.0 + flute_amp * np.sin(flute_k * theta + 3.8 * u + phase) * np.sin(u * math.pi)
            ripple = 1.0 + 0.09 * np.cos((flute_k + 2.0) * theta - 5.0 * u + phase * 1.6) * u
            scale_xz = rad_env * flutes * ripple
            p[:, 0] *= scale_xz
            p[:, 2] *= scale_xz
            s_wave = curl_amp * np.sin(u * math.pi * 1.9 + phase) * (u ** 0.85)
            p[:, 0] += bend_x * (u ** 1.42) + s_wave
            p[:, 2] += bend_z * (u ** 1.42) + 0.50 * curl_amp * np.cos(u * math.pi * 1.7 + phase) * (u ** 0.9)
            p[:, 1] = y0 + u * height
            return p

        mesh = ellipsoid(rx, height * 0.5, rz, u_segs=14, v_segs=14, name=name, material=mat).deform(_deform_plume)
        if tint_low is not None and tint_high is not None:
            gradient_y_tint(mesh, tint_low, tint_high, noise_amp=0.04)
        return mesh

    # Layer A: BOLD INCANDESCENT WHITE-HOT PLASMA STREAKS blazing prominently in the heart (`316..320` bright white core!).
    white_specs = (
        ("c", 0.00, +0.07, 0.096, 0.074, 0.62, -0.02, +0.02, 0.032, 3.0, 0.14, 0.2),
        ("l1", -0.11, +0.08, 0.082, 0.064, 0.48, -0.10, +0.02, 0.026, 3.0, 0.14, 1.3),
        ("r1", +0.11, +0.08, 0.082, 0.064, 0.50, +0.10, +0.02, 0.026, 3.0, 0.14, 2.4),
        ("lb", -0.17, +0.10, 0.070, 0.052, 0.24, -0.12, +0.03, 0.018, 2.0, 0.12, 0.8),
        ("rb", +0.17, +0.10, 0.070, 0.052, 0.25, +0.12, +0.03, 0.018, 2.0, 0.12, 3.1),
        ("bc", 0.00, -0.08, 0.092, 0.072, 0.56, +0.03, -0.02, 0.030, 3.0, 0.14, 4.0),
        ("bl", -0.10, -0.07, 0.074, 0.060, 0.44, -0.09, -0.02, 0.024, 3.0, 0.14, 5.1),
        ("br", +0.10, -0.07, 0.074, 0.060, 0.45, +0.09, -0.02, 0.024, 3.0, 0.14, 1.9),
    )
    sc.set_node_parent("flame_white", "wallup04")
    sc.set_node_transform("flame_white", translation=(0.0, 0.0, 0.0))
    sc.add(
        "flame_white",
        [
            _turbulent_plume(f"fire_white_{tag}", rx=rx, rz=rz, height=h, bend_x=bx, bend_z=bz, curl_amp=ca, flute_k=fk, flute_amp=fa, phase=ph, mat="item_flame_white", y0=0.055).translate(x, 0.0, z)
            for tag, x, z, rx, rz, h, bx, bz, ca, fk, fa, ph in white_specs
        ],
    )

    # Layer B: FULL ROARING SOLAR LEMON-YELLOW CORE PLUMES filling ~60% of the fire body (`134:wallup04` iconic glow!).
    yellow_specs = (
        ("c", 0.00, +0.01, 0.175, 0.148, 0.78, -0.02, +0.01, 0.044, 4.0, 0.18, 0.5),
        ("l1", -0.14, +0.02, 0.148, 0.125, 0.66, -0.11, +0.02, 0.038, 3.0, 0.18, 1.6),
        ("r1", +0.14, +0.02, 0.148, 0.125, 0.68, +0.12, +0.02, 0.038, 3.0, 0.18, 2.7),
        ("l2", -0.22, +0.01, 0.118, 0.100, 0.48, -0.15, +0.01, 0.032, 3.0, 0.16, 0.9),
        ("r2", +0.22, +0.01, 0.118, 0.100, 0.50, +0.16, +0.01, 0.032, 3.0, 0.16, 3.5),
        ("fl", -0.08, +0.09, 0.110, 0.082, 0.46, -0.06, +0.04, 0.026, 3.0, 0.15, 2.1),
        ("fr", +0.08, +0.09, 0.110, 0.082, 0.48, +0.07, +0.04, 0.026, 3.0, 0.15, 4.2),
        ("bl", -0.10, -0.09, 0.132, 0.110, 0.62, -0.09, -0.04, 0.034, 3.0, 0.17, 1.1),
        ("br", +0.10, -0.09, 0.132, 0.110, 0.64, +0.10, -0.04, 0.034, 3.0, 0.17, 3.8),
    )
    sc.set_node_parent("flame_yellow", "wallup04")
    sc.set_node_transform("flame_yellow", translation=(0.0, 0.0, 0.0))
    sc.add(
        "flame_yellow",
        [
            _turbulent_plume(
                f"fire_yellow_{tag}",
                rx=rx,
                rz=rz,
                height=h,
                bend_x=bx,
                bend_z=bz,
                curl_amp=ca,
                flute_k=fk,
                flute_amp=fa,
                phase=ph,
                mat="item_flame_core",
                tint_low=(1.00, 1.00, 0.88),
                tint_high=(1.00, 0.84, 0.24),
            ).translate(x, 0.0, z)
            for tag, x, z, rx, rz, h, bx, bz, ca, fk, fa, ph in yellow_specs
        ],
    )

    # Layer C: Luminous translucent GOLDEN-AMBER volumetric fire aura wrapping cleanly behind & beside the yellow heart!
    amber_l_specs = (
        ("lc", -0.04, -0.02, 0.208, 0.174, 0.86, -0.04, +0.01, 0.050, 4.0, 0.22, 0.3),
        ("l1", -0.17, -0.01, 0.168, 0.142, 0.76, -0.14, +0.02, 0.046, 4.0, 0.22, 1.4),
        ("l2", -0.25, -0.02, 0.136, 0.118, 0.58, -0.18, +0.01, 0.040, 3.0, 0.20, 2.5),
        ("l3", -0.11, -0.12, 0.155, 0.132, 0.74, -0.10, -0.06, 0.040, 4.0, 0.20, 3.7),
        ("lw", -0.28, +0.03, 0.096, 0.082, 0.34, -0.19, +0.03, 0.028, 3.0, 0.18, 0.9),
    )
    amber_r_specs = (
        ("rc", +0.04, -0.02, 0.206, 0.174, 0.84, +0.05, -0.01, 0.050, 4.0, 0.22, 1.9),
        ("r1", +0.17, -0.01, 0.168, 0.142, 0.78, +0.15, +0.02, 0.046, 4.0, 0.22, 3.1),
        ("r2", +0.25, -0.02, 0.136, 0.118, 0.60, +0.19, +0.01, 0.040, 3.0, 0.20, 4.3),
        ("r3", +0.11, -0.12, 0.155, 0.132, 0.72, +0.11, -0.06, 0.040, 4.0, 0.20, 0.7),
        ("rw", +0.28, +0.03, 0.096, 0.082, 0.36, +0.20, +0.03, 0.028, 3.0, 0.18, 5.1),
    )
    for node_name, specs in (("flame_amber_l", amber_l_specs), ("flame_amber_r", amber_r_specs)):
        sc.set_node_parent(node_name, "wallup04")
        sc.set_node_transform(node_name, translation=(0.0, 0.0, 0.0))
        sc.add(
            node_name,
            [
                _turbulent_plume(
                    f"fire_amber_{tag}",
                    rx=rx,
                    rz=rz,
                    height=h,
                    bend_x=bx,
                    bend_z=bz,
                    curl_amp=ca,
                    flute_k=fk,
                    flute_amp=fa,
                    phase=ph,
                    mat="item_flame_amber",
                    tint_low=(1.00, 0.88, 0.30),
                    tint_high=(1.00, 0.36, 0.04),
                ).translate(x, 0.0, z)
                for tag, x, z, rx, rz, h, bx, bz, ca, fk, fa, ph in specs
            ],
        )

    # Layer D: Ragged VERMILION-CRIMSON perimeter crown licks & side wings framing the blazing white-yellow heart cleanly!
    crimson_specs = (
        ("t1", 0.00, -0.08, 0.122, 0.106, 0.94, +0.03, -0.03, 0.058, 5.0, 0.24, 0.4),
        ("t2", -0.10, -0.07, 0.110, 0.095, 0.88, -0.09, -0.02, 0.054, 4.0, 0.24, 1.5),
        ("t3", +0.10, -0.07, 0.110, 0.095, 0.90, +0.10, -0.02, 0.054, 4.0, 0.24, 2.8),
        ("t4", -0.20, -0.04, 0.102, 0.090, 0.78, -0.16, +0.01, 0.050, 4.0, 0.24, 3.9),
        ("t5", +0.20, -0.04, 0.102, 0.090, 0.80, +0.17, +0.01, 0.050, 4.0, 0.24, 5.0),
        ("l1", -0.28, +0.00, 0.092, 0.084, 0.66, -0.20, +0.01, 0.046, 3.0, 0.22, 0.9),
        ("r1", +0.28, +0.00, 0.092, 0.084, 0.68, +0.21, +0.01, 0.046, 3.0, 0.22, 2.2),
        ("l2", -0.31, +0.03, 0.076, 0.066, 0.38, -0.22, +0.02, 0.034, 3.0, 0.20, 4.1),
        ("r2", +0.31, +0.03, 0.076, 0.066, 0.40, +0.23, +0.02, 0.034, 3.0, 0.20, 1.2),
        ("b1", -0.06, -0.17, 0.125, 0.102, 0.82, -0.06, -0.08, 0.048, 4.0, 0.22, 3.4),
        ("b2", +0.07, -0.17, 0.122, 0.100, 0.84, +0.08, -0.08, 0.048, 4.0, 0.22, 5.6),
        ("s1", -0.24, +0.08, 0.080, 0.066, 0.42, -0.20, +0.06, 0.030, 3.0, 0.20, 1.8),
        ("s2", +0.24, +0.08, 0.080, 0.066, 0.44, +0.21, +0.06, 0.030, 3.0, 0.20, 4.5),
    )
    sc.set_node_parent("flame_crimson", "wallup04")
    sc.set_node_transform("flame_crimson", translation=(0.0, 0.0, 0.0))
    sc.add(
        "flame_crimson",
        [
            _turbulent_plume(
                f"fire_crim_{tag}",
                rx=rx,
                rz=rz,
                height=h,
                bend_x=bx,
                bend_z=bz,
                curl_amp=ca,
                flute_k=fk,
                flute_amp=fa,
                phase=ph,
                mat="item_flame_crimson",
                tint_low=(1.00, 0.60, 0.14),
                tint_high=(0.98, 0.14, 0.02),
            ).translate(x, 0.0, z)
            for tag, x, z, rx, rz, h, bx, bz, ca, fk, fa, ph in crimson_specs
        ],
    )

    # Layer E: Crisp tiny RISING EMBER SPARKS hugging tightly off the licking tips & flanks (`316..320` pixel sparks!).
    sparks_a_specs = (
        (-0.22, 0.84, +0.03, 0.011, 0.017, +0.25, "item_flame_spark"),
        (-0.07, 0.90, -0.02, 0.011, 0.018, -0.18, "item_flame_spark"),
        (+0.06, 0.92, +0.02, 0.012, 0.019, +0.15, "item_flame_spark"),
        (+0.21, 0.86, -0.03, 0.011, 0.017, -0.28, "item_flame_spark"),
        (-0.33, 0.68, +0.04, 0.011, 0.016, +0.38, "item_flame_spark"),
        (+0.34, 0.70, +0.02, 0.011, 0.016, -0.35, "item_flame_spark"),
        (-0.15, 0.77, +0.09, 0.010, 0.015, +0.22, "item_flame_core"),
        (+0.15, 0.79, +0.08, 0.010, 0.015, -0.20, "item_flame_core"),
        (-0.31, 0.48, -0.07, 0.010, 0.015, +0.32, "item_flame_spark"),
        (+0.32, 0.50, -0.06, 0.010, 0.015, -0.30, "item_flame_spark"),
        (0.00, 0.88, -0.14, 0.011, 0.017, +0.10, "item_flame_spark"),
    )
    sparks_b_specs = (
        (-0.16, 0.87, -0.04, 0.010, 0.016, +0.22, "item_flame_spark"),
        (-0.01, 0.94, +0.01, 0.012, 0.018, -0.12, "item_flame_spark"),
        (+0.14, 0.88, +0.03, 0.010, 0.016, -0.22, "item_flame_spark"),
        (+0.27, 0.80, -0.02, 0.011, 0.017, -0.34, "item_flame_spark"),
        (-0.28, 0.77, +0.02, 0.011, 0.017, +0.36, "item_flame_spark"),
        (-0.36, 0.57, +0.03, 0.010, 0.015, +0.42, "item_flame_spark"),
        (+0.37, 0.59, +0.03, 0.010, 0.015, -0.40, "item_flame_spark"),
        (-0.05, 0.81, +0.11, 0.009, 0.014, +0.18, "item_flame_core"),
        (+0.07, 0.77, -0.12, 0.010, 0.015, -0.18, "item_flame_core"),
        (+0.29, 0.40, +0.09, 0.010, 0.015, -0.28, "item_flame_spark"),
        (-0.28, 0.38, +0.09, 0.010, 0.015, +0.28, "item_flame_spark"),
    )
    for node_name, s_specs in (("flame_sparks_a", sparks_a_specs), ("flame_sparks_b", sparks_b_specs)):
        sc.set_node_parent(node_name, "wallup04")
        sc.set_node_transform(node_name, translation=(0.0, 0.0, 0.0))
        sc.add(
            node_name,
            [
                superellipsoid(sr, sh, sr * 0.90, e1=0.68, e2=0.72, u_segs=6, v_segs=5, name=f"{node_name}_{k}", material=smat)
                .rotate_xyz(rz=srz)
                .translate(sx, sy, sz)
                for k, (sx, sy, sz, sr, sh, srz, smat) in enumerate(s_specs)
            ],
        )

    # 3. Lively 5-beat counter-phased roaring fire animation (0.50 s loop matching 1998's 5 fire frames `316..320`!)
    times = [0.00, 0.10, 0.20, 0.30, 0.40, 0.50]
    sc.add_animation(
        "Idle",
        [
            AnimationChannel(
                "flame_white",
                "scale",
                times,
                [(1.00, 1.00, 1.00), (0.94, 1.11, 0.96), (1.06, 0.92, 1.04), (0.96, 1.08, 0.98), (1.04, 0.96, 1.02), (1.00, 1.00, 1.00)],
            ),
            AnimationChannel(
                "flame_yellow",
                "scale",
                times,
                [(1.00, 1.00, 1.00), (1.04, 1.08, 1.02), (0.96, 0.94, 0.98), (1.05, 1.06, 1.03), (0.97, 0.96, 0.98), (1.00, 1.00, 1.00)],
            ),
            AnimationChannel(
                "flame_yellow",
                "rotation",
                times,
                [quat_from_euler_xyz(rz=a, ry=b) for a, b in ((0.0, 0.0), (0.04, 0.05), (-0.04, -0.05), (0.03, 0.04), (-0.03, -0.03), (0.0, 0.0))],
            ),
            AnimationChannel(
                "flame_amber_l",
                "scale",
                times,
                [(1.00, 1.00, 1.00), (1.06, 1.10, 1.03), (0.95, 0.92, 0.97), (1.04, 1.07, 1.02), (0.96, 0.95, 0.98), (1.00, 1.00, 1.00)],
            ),
            AnimationChannel(
                "flame_amber_l",
                "rotation",
                times,
                [quat_from_euler_xyz(rz=a, ry=b) for a, b in ((0.0, 0.0), (0.06, 0.08), (-0.05, -0.07), (0.05, 0.06), (-0.04, -0.05), (0.0, 0.0))],
            ),
            AnimationChannel(
                "flame_amber_r",
                "scale",
                times,
                [(1.00, 1.00, 1.00), (0.95, 0.92, 0.97), (1.06, 1.11, 1.04), (0.96, 0.94, 0.98), (1.05, 1.08, 1.03), (1.00, 1.00, 1.00)],
            ),
            AnimationChannel(
                "flame_amber_r",
                "rotation",
                times,
                [quat_from_euler_xyz(rz=a, ry=b) for a, b in ((0.0, 0.0), (-0.06, -0.08), (0.06, 0.09), (-0.04, -0.06), (0.04, 0.06), (0.0, 0.0))],
            ),
            AnimationChannel(
                "flame_crimson",
                "scale",
                times,
                [(1.00, 1.00, 1.00), (1.05, 1.09, 1.03), (0.96, 0.93, 0.97), (1.07, 1.12, 1.04), (0.97, 0.95, 0.98), (1.00, 1.00, 1.00)],
            ),
            AnimationChannel(
                "flame_crimson",
                "rotation",
                times,
                [quat_from_euler_xyz(rz=a, ry=b) for a, b in ((0.0, 0.0), (-0.07, 0.10), (0.07, -0.09), (-0.05, 0.08), (0.05, -0.07), (0.0, 0.0))],
            ),
            AnimationChannel(
                "flame_sparks_a",
                "translation",
                times,
                [(0.0, 0.00, 0.0), (0.012, 0.036, 0.0), (-0.010, 0.060, 0.0), (0.010, 0.024, 0.0), (-0.010, 0.048, 0.0), (0.0, 0.00, 0.0)],
            ),
            AnimationChannel(
                "flame_sparks_a",
                "scale",
                times,
                [(0.96, 0.96, 0.96), (1.14, 1.18, 1.14), (0.86, 0.88, 0.86), (1.12, 1.15, 1.12), (0.90, 0.92, 0.90), (0.96, 0.96, 0.96)],
            ),
            AnimationChannel(
                "flame_sparks_b",
                "translation",
                times,
                [(0.0, 0.032, 0.0), (-0.012, 0.060, 0.0), (0.010, 0.008, 0.0), (-0.008, 0.044, 0.0), (0.012, 0.066, 0.0), (0.0, 0.032, 0.0)],
            ),
            AnimationChannel(
                "flame_sparks_b",
                "scale",
                times,
                [(1.08, 1.12, 1.08), (0.86, 0.88, 0.86), (1.15, 1.20, 1.15), (0.92, 0.94, 0.92), (1.10, 1.14, 1.10), (1.08, 1.12, 1.08)],
            ),
        ],
        replace=True,
    )
    return _finalize_item(sc, target_span=0.92)
