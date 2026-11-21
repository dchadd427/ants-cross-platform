"""Sculpted 3D Fire Ant (Option A: 1998 Wide Billboard yellow plastic helmet + empty idle hands).

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet grounded solidly at Y = 0.0)
  +Z = front (South / canonical camera 301)
  +X = ant's left (viewer's right from front), -X = ant's right
"""
from __future__ import annotations

import math
from typing import List, Tuple

import numpy as np

try:
    from .base_ant import build_base_ant, sync_arm_idle_to_clip, sync_leg_flex_to_clip
    from .mesh_core import (
        AnimationChannel,
        AnimationClip,
        MeshPart,
        SceneBuilder,
        curved_tube,
        cylinder,
        ellipsoid,
        ensure_outward_winding,
        hemisphere_shell,
        lathe,
        quat_from_euler_xyz,
        quat_mul,
        recompute_smooth_normals,
        sphere,
        swept_tube,
        torus_arc,
    )
except ImportError:
    from base_ant import build_base_ant, sync_arm_idle_to_clip, sync_leg_flex_to_clip  # type: ignore
    from mesh_core import (  # type: ignore
        AnimationChannel,
        AnimationClip,
        MeshPart,
        SceneBuilder,
        curved_tube,
        cylinder,
        ellipsoid,
        ensure_outward_winding,
        hemisphere_shell,
        lathe,
        quat_from_euler_xyz,
        quat_mul,
        recompute_smooth_normals,
        sphere,
        swept_tube,
        torus_arc,
    )

_HAT_MAT = "gear_yellow_plastic"

# Optional right-arm pose when `hold_magnifying_glass=True` (kept for Fire's fire-starting animation).
_FIRE_RIGHT_ARM: List[Tuple[float, float, float]] = [
    (-0.078, 0.456, 0.014),
    (-0.184, 0.422, 0.068),
    (-0.194, 0.464, 0.168),
    (-0.182, 0.494, 0.222),
]


def _spow_scalar(val: float, exp: float) -> float:
    return math.copysign(abs(val) ** exp, val)


def _subdivided_rounded_box(
    sx: float,
    sy: float,
    sz: float,
    bevel: float,
    nx: int,
    ny: int,
    nz: int,
    *,
    name: str,
    material: str,
) -> MeshPart:
    """Build a watertight subdivided rounded box with dense interior face vertices for smooth curving."""
    nx, ny, nz = max(2, int(nx)), max(2, int(ny)), max(2, int(nz))
    hx, hy, hz = 0.5 * float(sx), 0.5 * float(sy), 0.5 * float(sz)
    r = min(max(1e-4, float(bevel)), 0.45 * min(float(sx), float(sy), float(sz)))
    core = np.array([hx - r, hy - r, hz - r], dtype=np.float64)

    vert_map: dict = {}
    grid_pts: List[Tuple[float, float, float]] = []

    def _vid(ix: int, iy: int, iz: int) -> int:
        key = (ix, iy, iz)
        idx = vert_map.get(key)
        if idx is None:
            idx = len(grid_pts)
            vert_map[key] = idx
            grid_pts.append((2.0 * ix / nx - 1.0, 2.0 * iy / ny - 1.0, 2.0 * iz / nz - 1.0))
        return idx

    tris: List[Tuple[int, int, int]] = []
    # +Z and -Z faces (subdivided nx x ny)
    for iz, flip in ((nz, False), (0, True)):
        for j in range(ny):
            for i in range(nx):
                a, b, c, d = _vid(i, j, iz), _vid(i + 1, j, iz), _vid(i + 1, j + 1, iz), _vid(i, j + 1, iz)
                if flip:
                    tris.extend([(a, c, b), (a, d, c)])
                else:
                    tris.extend([(a, b, c), (a, c, d)])
    # +X and -X faces (subdivided ny x nz)
    for ix, flip in ((nx, False), (0, True)):
        for k in range(nz):
            for j in range(ny):
                a, b, c, d = _vid(ix, j, k), _vid(ix, j + 1, k), _vid(ix, j + 1, k + 1), _vid(ix, j, k + 1)
                if flip:
                    tris.extend([(a, b, c), (a, c, d)])
                else:
                    tris.extend([(a, c, b), (a, d, c)])
    # +Y and -Y faces (subdivided nx x nz)
    for iy, flip in ((ny, False), (0, True)):
        for k in range(nz):
            for i in range(nx):
                a, b, c, d = _vid(i, iy, k), _vid(i + 1, iy, k), _vid(i + 1, iy, k + 1), _vid(i, iy, k + 1)
                if flip:
                    tris.extend([(a, b, c), (a, c, d)])
                else:
                    tris.extend([(a, c, b), (a, d, c)])

    g = np.asarray(grid_pts, dtype=np.float64)
    q = np.column_stack([g[:, 0] * hx, g[:, 1] * hy, g[:, 2] * hz])
    clamped = np.clip(q, -core, core)
    diff = q - clamped
    dnorm = np.linalg.norm(diff, axis=1, keepdims=True)
    pos = np.where(dnorm > 1e-10, clamped + r * diff / np.maximum(1e-10, dnorm), q).astype(np.float32)
    idx_arr = ensure_outward_winding(pos, np.asarray(tris, dtype=np.uint32).reshape(-1))
    nrm = recompute_smooth_normals(pos, idx_arr)
    return MeshPart(name, material, pos, nrm, idx_arr)


def _brim_radial_and_y(theta: np.ndarray) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Compute sleek longitudinal firefighter brim: compact upturned sides & plump rounded-oval rear tail."""
    cos_t = np.cos(theta)
    sin_t = np.sin(theta)
    front_w = np.clip(cos_t, 0.0, 1.0) ** 1.35
    side_w = sin_t * sin_t
    # Broad flat-topped C^1 rear lobe `s^2 * (2 - s^2)` plus gentle rear-quarter fullness `side_w * rear_w`
    # so lateral sides stay narrow and parallel into a plump rounded U-oval rear tail matching `afst701`.
    s_rear = np.clip(-cos_t, 0.0, 1.0)
    rear_w = (s_rear * s_rear) * (2.0 - s_rear * s_rear)
    rear_quarter_w = side_w * rear_w

    x_flare = 0.16 * front_w + 0.19 * side_w + 0.22 * rear_w + 0.28 * rear_quarter_w
    z_flare = 0.28 * front_w + 0.15 * side_w + 0.60 * rear_w
    y_shift = -0.004 * front_w + 0.012 * side_w * (1.0 - 0.80 * rear_w) - 0.125 * rear_w
    return x_flare, z_flare, y_shift


def _helmet_shell_deform_bounds(pos: np.ndarray, y_min: float, y_max: float) -> np.ndarray:
    """Apply Option 3 deeply-fluted vintage ribbed dome + upturned side brims & plump oval rear neck-guard."""
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]

    h = np.clip((y - y_min) / max(1e-6, y_max - y_min), 0.0, 1.0)
    brim_t = np.clip(1.0 - h / 0.25, 0.0, 1.0)
    brim = brim_t ** 1.55

    theta = np.arctan2(x, z)
    cos_t = np.cos(theta)
    sin_t = np.sin(theta)
    x_flare, z_flare, y_shift = _brim_radial_and_y(theta)

    # Option 3 pronounced scalloped/fluted vintage firefighter dome ribs (8 bold convex panels separated
    # by crisp carved grooves), smoothly fading out at the brim (h < 0.22) and softening near apex.
    t_lo = np.clip((h - 0.20) / 0.18, 0.0, 1.0)
    fade_lo = t_lo * t_lo * (3.0 - 2.0 * t_lo)
    fade_hi = 1.0 - 0.58 * (np.clip((h - 0.78) / 0.22, 0.0, 1.0) ** 2.0)
    flute_mask = fade_lo * fade_hi
    lobe = np.abs(np.cos(4.0 * theta)) ** 0.72
    flute_scale = 1.0 + flute_mask * (0.098 * lobe - 0.056)

    p[:, 0] = x * flute_scale * (1.0 + brim * x_flare)
    p[:, 2] = z * flute_scale * (1.0 + brim * z_flare)

    s_rear = np.clip(-cos_t, 0.0, 1.0)
    rear_w = (s_rear * s_rear) * (2.0 - s_rear * s_rear)
    flatten_lift = 0.036 * (1.0 - 0.65 * rear_w) * (brim_t ** 1.35)
    edge_mask = np.clip(1.0 - h / 0.07, 0.0, 1.0) ** 2.0
    front_lip_curl = 0.004 * np.clip(cos_t, 0.0, 1.0) * edge_mask
    # Crisp, tasteful upward tilt along the outer left & right side brims matching afst301.
    side_edge_mask = np.clip(1.0 - h / 0.15, 0.0, 1.0) ** 1.75
    side_curl = 0.039 * (sin_t * sin_t) * (1.0 - 0.85 * rear_w) * side_edge_mask
    # Subtle vintage flute relief along dome height too so top ridges pop under overhead 45 deg lighting.
    flute_y = 0.010 * flute_mask * (lobe - 0.5)
    p[:, 1] = y + flatten_lift + brim * y_shift + front_lip_curl + side_curl + flute_y
    return p


def _shield_z_curve(
    x: np.ndarray,
    y: np.ndarray,
    shield_w: float,
    shield_h: float,
    curve_depth: float,
) -> np.ndarray:
    """Shared parametric recessed front-face Z curvature used identically by the shield & crimson 'A'."""
    u_y = np.clip((y + 0.5 * shield_h) / max(1e-6, shield_h), 0.0, 1.0)
    nx = x / max(1e-6, 0.5 * shield_w)
    return -curve_depth * (nx * nx) - 0.016 * (u_y ** 1.75)


def _build_option3_wide_shield(
    shield_w: float,
    shield_h: float,
    shield_depth: float,
    curve_depth: float,
) -> MeshPart:
    """Build Option 3's proud framed vintage crest shield at Option 2's extra-wide front outline width.

    Uses a finely-subdivided watertight solid panel (`30x18x6`) with a continuous crisp raised beveled
    molded border frame sculpted directly into the front face (zero detached border strips!).
    """
    half_w = 0.5 * shield_w
    half_h = 0.5 * shield_h
    half_d = 0.5 * shield_depth

    def _sculpt_shield(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ux = p[:, 0] / max(1e-6, half_w)  # [-1, +1]
        uy = p[:, 1] / max(1e-6, half_h)  # [-1, +1]
        uz = p[:, 2] / max(1e-6, half_d)  # [-1, +1]
        u_y = np.clip(0.5 * (uy + 1.0), 0.0, 1.0)

        # Option 2 wide flat base + Option 3 gentle inward taper & proud arched vintage crest top!
        taper = 1.0 - 0.14 * (u_y ** 1.35)
        p[:, 0] *= taper
        nx = p[:, 0] / max(1e-6, half_w)
        top_mask = np.clip((u_y - 0.45) / 0.55, 0.0, 1.0)
        # Proud arched crown crest rising gracefully at x=0 and arching cleanly toward outer corners.
        crest_arch = 0.016 * (1.0 - 1.55 * (nx * nx)) * top_mask
        p[:, 1] += crest_arch

        # Subtle crisp raised molded Option 3 perimeter border outline directly on the front face!
        # Smooth super-elliptical distance to perimeter (`edge_d in [0, 1]`): recessed field inside 0.78,
        # smooth raised +0.011 beveled border ridge around 0.78..1.0 on front face (`uz > 0.2`).
        edge_d = (np.abs(ux) ** 6.0 + np.abs(uy) ** 6.0) ** (1.0 / 6.0)
        t_rim = np.clip((edge_d - 0.76) / 0.14, 0.0, 1.0)
        rim_step = t_rim * t_rim * (3.0 - 2.0 * t_rim)
        front_mask = np.clip((uz - 0.20) / 0.55, 0.0, 1.0)
        border_relief = 0.011 * rim_step * front_mask

        # Slightly lift rear-bottom edge so deep embedded back never pokes below front brow.
        rear_bot = np.clip((-uz - 0.10) / 0.90, 0.0, 1.0) * np.clip((0.30 - u_y) / 0.30, 0.0, 1.0)
        p[:, 1] += 0.014 * rear_bot

        p[:, 2] += _shield_z_curve(p[:, 0], p[:, 1], shield_w, shield_h, curve_depth) + border_relief
        return p

    return _subdivided_rounded_box(
        shield_w,
        shield_h,
        shield_depth,
        bevel=0.015,
        nx=30,
        ny=18,
        nz=6,
        name="helmet_front_billboard",
        material=_HAT_MAT,
    ).deform(_sculpt_shield)


def _build_giant_letter_a(
    shield_w: float,
    shield_h: float,
    shield_depth: float,
    curve_depth: float,
    *,
    y_offset: float = -0.010,
) -> List[MeshPart]:
    """Build a bold wide crimson sans-serif 'A' embedded flush inside the framed yellow shield field.

    Back face embeds 0.006 inside the yellow shield front surface; front face stands +0.014 proud,
    conforming identically to `_shield_z_curve` with ZERO air gap and ZERO backing plaque.
    """
    parts: List[MeshPart] = []
    thickness = 0.020  # [-0.010, +0.010] -> shifted +0.004 => [-0.006 inside, +0.014 proud]
    front_z = 0.5 * shield_depth
    stroke_w = 0.036
    leg_len = 0.158
    tilt_deg = 22.8

    left_leg = (
        _subdivided_rounded_box(stroke_w, leg_len, thickness, bevel=0.004, nx=6, ny=12, nz=4, name="letter_a_left", material="gear_accent")
        .rotate_xyz(rz=-tilt_deg, degrees=True)
        .translate(-0.035, y_offset, 0.004)
    )
    right_leg = (
        _subdivided_rounded_box(stroke_w, leg_len, thickness, bevel=0.004, nx=6, ny=12, nz=4, name="letter_a_right", material="gear_accent")
        .rotate_xyz(rz=+tilt_deg, degrees=True)
        .translate(+0.035, y_offset, 0.004)
    )
    apex = (
        _subdivided_rounded_box(0.046, 0.030, thickness, bevel=0.004, nx=8, ny=4, nz=4, name="letter_a_apex", material="gear_accent")
        .translate(0.0, 0.061 + y_offset, 0.004)
    )
    crossbar = (
        _subdivided_rounded_box(0.108, 0.028, thickness, bevel=0.004, nx=12, ny=4, nz=4, name="letter_a_bar", material="gear_accent")
        .translate(0.0, -0.022 + y_offset, 0.004)
    )
    raw_parts = [left_leg, right_leg, apex, crossbar]

    def _conform_to_shield(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        p[:, 2] += front_z + _shield_z_curve(p[:, 0], p[:, 1], shield_w, shield_h, curve_depth)
        return p

    for part in raw_parts:
        parts.append(part.deform(_conform_to_shield))
    return parts


def _measure_eyelid_max_y(
    scene: SceneBuilder,
    eye_left: Tuple[float, float, float],
    closed_angle: float,
) -> float:
    """Measure the highest world Y reached by eyelid_left across full blink=0..1."""
    parts = scene.nodes.get("eyelid_left", [])
    if not parts:
        return eye_left[1] + 0.092
    verts = np.vstack([p.positions.astype(np.float64) for p in parts])
    ey = float(eye_left[1])
    max_y = ey
    for frac in (0.0, 0.25, 0.50, 0.75, 1.0):
        ang = frac * closed_angle
        ca, sa = math.cos(ang), math.sin(ang)
        r_blink = np.array([[1.0, 0.0, 0.0], [0.0, ca, -sa], [0.0, sa, ca]], dtype=np.float64)
        p_y = (verts @ r_blink.T)[:, 1] + ey
        max_y = max(max_y, float(p_y.max()))
    return max_y


def _build_fire_helmet(
    scene: SceneBuilder,
    head_center: Tuple[float, float, float],
    head_radii: Tuple[float, float, float],
    head_top: Tuple[float, float, float],
    eye_left: Tuple[float, float, float],
    closed_angle: float,
    *,
    orig_head_radii: Tuple[float, float, float],
) -> None:
    """Build Option 3+2 vintage deeply-fluted firefighter helmet with wide framed 'A' shield and flatter curled brim."""
    hx, _, hz = head_center
    hrx, _, hrz = head_radii
    orig_hrx, _, orig_hrz = orig_head_radii
    eyelid_top_y = _measure_eyelid_max_y(scene, eye_left, closed_angle)

    # Seat the yellow brim snugly above his cute eyes while preserving clean blink clearance (~9+ mm across blink=0..1).
    shell_y = max(eyelid_top_y - 0.031, head_top[1] - 0.136)
    shell_z = hz - 0.008
    # Sleek longitudinal firefighter dome (W <= D) proportioned cleanly above Fire's 0.95x head (matching afst301..701).
    dome_rx = max(hrx + 0.022, orig_hrx + 0.010)
    dome_ry = max(0.284, (head_top[1] - shell_y) + 0.144)
    dome_rz = max(hrz + 0.044, orig_hrz + 0.028)

    # 1. Option 3 deeply-fluted vintage ribbed dome + traditional swept firefighter brim (80x32 high-res!).
    u_segs = 80
    start_phi = -0.04
    e_dom = 0.92
    y_min_raw = dome_ry * _spow_scalar(math.sin(start_phi), e_dom)
    y_max_raw = dome_ry

    def _deform_dome(pos: np.ndarray) -> np.ndarray:
        return _helmet_shell_deform_bounds(pos, y_min_raw, y_max_raw)

    dome = (
        hemisphere_shell(
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
            name="helmet_dome",
            material=_HAT_MAT,
        )
        .translate(hx, shell_y, shell_z)
    )
    scene.add("fire_helmet", dome)

    # Smooth rolled yellow plastic outer brim edge bead hugging the exact beveled rim of dome
    # (`p_out[:u_segs]`, `p_in[:u_segs]`, `p_lip` -> max_outside <= 4.5 mm proud, 0.000 mm drift!).
    n_out = (len(dome.positions) - u_segs) // 2
    rim_mid = 0.5 * (dome.positions[:u_segs] + dome.positions[n_out : n_out + u_segs])
    rim_center = 0.88 * rim_mid + 0.12 * dome.positions[-u_segs:]
    lip_pts = [(float(pt[0]), float(pt[1]), float(pt[2])) for pt in rim_center]
    brim_lip = swept_tube(
        lip_pts,
        0.0085,
        radial_segs=10,
        closed=True,
        name="helmet_brim_lip",
        material=_HAT_MAT,
    )
    scene.add("fire_helmet", brim_lip)

    # Dark inner head-liner band tucked cleanly inside the lower shell around Fire's smaller 0.88x cranium.
    liner = (
        cylinder(hrx * 0.96, hrx * 1.00, 0.036, radial_segs=26, bevel=0.006, axis="y", name="helmet_liner", material="gear_dark")
        .scale(1.0, 1.0, hrz / max(1e-6, hrx))
        .translate(hx, shell_y + 0.064, hz)
    )
    scene.add("fire_helmet", liner)

    def _sample_dome_surface(phi: float, theta: float) -> Tuple[float, float, float]:
        cp = _spow_scalar(math.cos(phi), e_dom)
        sp = _spow_scalar(math.sin(phi), e_dom)
        st = _spow_scalar(math.sin(theta), e_dom)
        ct = _spow_scalar(math.cos(theta), e_dom)
        raw = np.array([[dome_rx * cp * st, dome_ry * sp, dome_rz * cp * ct]], dtype=np.float64)
        deformed = _deform_dome(raw)[0]
        return (hx + float(deformed[0]), shell_y + float(deformed[1]), shell_z + float(deformed[2]))

    # 2. Half-embedded crisp vintage accent spine beads right on the peaks of the 5 rear/side fluted ribs
    # (0.000 mm centerline drift ON the exact deformed fluted dome peaks -> crisp highlight lines, zero gap!).
    rib_specs = (
        (180.0, 0.24, 0.0135, "back"),
        (90.0, 0.26, 0.0125, "left"),
        (-90.0, 0.26, 0.0125, "right"),
        (135.0, 0.24, 0.0125, "bl"),
        (-135.0, 0.24, 0.0125, "br"),
    )
    for az_deg, phi_end, peak_rad, tag in rib_specs:
        az = math.radians(az_deg)
        phis = np.linspace(0.49 * math.pi, phi_end, 8)
        r_pts = [_sample_dome_surface(float(phi), az) for phi in phis]
        r_rads = [peak_rad * (0.95 - 0.30 * (k / (len(phis) - 1))) for k in range(len(phis))]
        rib = curved_tube(
            r_pts,
            r_rads,
            samples_per_seg=4,
            radial_segs=10,
            cap_rings=2,
            name=f"helmet_rib_{tag}",
            material=_HAT_MAT,
        )
        scene.add("fire_helmet", rib)

    # Top crown spine ridge running smoothly back from behind the proud shield crest along the exact dome surface.
    crest_samples = [
        (0.84, 0.0),
        (1.12, 0.0),
        (1.36, 0.0),
        (0.5 * math.pi, 0.0),
        (1.30, math.pi),
        (1.02, math.pi),
        (0.72, math.pi),
    ]
    crest_pts = [_sample_dome_surface(phi, az) for phi, az in crest_samples]
    crest = curved_tube(
        crest_pts,
        [0.0115, 0.0135, 0.0145, 0.0150, 0.0145, 0.0135, 0.0115],
        samples_per_seg=4,
        radial_segs=10,
        cap_rings=2,
        name="helmet_crown_crest",
        material=_HAT_MAT,
    )
    scene.add("fire_helmet", crest)

    # 3. Option 3's proud vintage framed front shield proportioned to the sleeker dome + flush crimson 'A'.
    shield_w = dome_rx * 1.62
    shield_h = dome_ry * 0.86
    shield_depth = 0.100
    curve_depth = 0.080
    shield_tilt_x = -8.5
    shield_pos = (hx, shell_y + 0.170, shell_z + dome_rz - 0.034)

    shield = (
        _build_option3_wide_shield(shield_w, shield_h, shield_depth, curve_depth)
        .rotate_xyz(rx=shield_tilt_x, degrees=True)
        .translate(*shield_pos)
    )
    scene.add("fire_helmet", shield)

    # Bold wide crimson 'A' embedded 0.006 inside the framed yellow shield face & raised +0.014 proud!
    for part in _build_giant_letter_a(shield_w, shield_h, shield_depth, curve_depth, y_offset=-0.010):
        placed = part.rotate_xyz(rx=shield_tilt_x, degrees=True).translate(*shield_pos)
        scene.add("fire_helmet", placed)


def build_magnifying_glass(
    scene: SceneBuilder,
    right_hand: Tuple[float, float, float],
    right_hand_dir: Tuple[float, float, float],
) -> None:
    """Build sleek realistic see-through brass magnifying glass gripped upright in right hand (-X)."""
    hx, hy, hz = right_hand
    dx, dy, dz = right_hand_dir
    grip_x = hx + 0.022 * dx
    grip_y = hy + 0.022 * dy
    grip_z = hz + 0.022 * dz
    parts: List[MeshPart] = []

    handle_profile = [
        (0.000, -0.142),
        (0.017, -0.136),
        (0.023, -0.115),
        (0.021, -0.072),
        (0.017, -0.014),
        (0.018, +0.034),
        (0.021, +0.060),
        (0.000, +0.064),
    ]
    handle = lathe(
        handle_profile,
        radial_segs=20,
        cap_bottom=False,
        cap_top=False,
        axis="y",
        name="mag_handle",
        material="gear_accent",
    )
    parts.append(handle)

    pommel_cap = (
        sphere(0.015, u_segs=12, v_segs=10, name="mag_pommel_cap", material="gear_gold_metal")
        .scale(1.0, 0.72, 1.0)
        .translate(0.0, -0.138, 0.0)
    )
    parts.append(pommel_cap)

    ferrule_profile = [
        (0.022, +0.056),
        (0.026, +0.064),
        (0.021, +0.078),
        (0.017, +0.094),
        (0.023, +0.110),
        (0.019, +0.116),
    ]
    ferrule = lathe(
        ferrule_profile,
        radial_segs=20,
        cap_bottom=True,
        cap_top=True,
        axis="y",
        name="mag_ferrule",
        material="gear_gold_metal",
    )
    parts.append(ferrule)

    lens_r = 0.090
    lens_cy = 0.112 + lens_r  # +0.202

    outer_rim = (
        torus_arc(lens_r + 0.004, 0.0125, u_segs=34, v_segs=12, plane="xy", name="mag_rim_outer", material="gear_gold_metal")
        .scale(1.0, 1.0, 0.88)
        .translate(0.0, lens_cy, 0.0)
    )
    parts.append(outer_rim)

    for z_sign, tag in ((+1.0, "front"), (-1.0, "back")):
        bezel = (
            torus_arc(lens_r - 0.004, 0.0058, u_segs=30, v_segs=8, plane="xy", name=f"mag_bezel_{tag}", material="gear_brass_trim")
            .translate(0.0, lens_cy, z_sign * 0.008)
        )
        parts.append(bezel)

    lens = (
        ellipsoid(lens_r - 0.004, lens_r - 0.004, 0.012, u_segs=28, v_segs=16, name="mag_lens", material="glass_clear")
        .translate(0.0, lens_cy, 0.0)
    )
    parts.append(lens)

    for part in parts:
        placed = part.rotate_xyz(rx=18.0, ry=-12.0, rz=10.0, degrees=True)
        scene.add("magnifying_glass", placed)
    scene.set_node_transform("magnifying_glass", translation=(grip_x, grip_y, grip_z))


_build_magnifying_glass = build_magnifying_glass


def _add_fire_idle(scene: SceneBuilder, *, hold_magnifying_glass: bool = False) -> AnimationClip:
    """Attach Fire's calm, composed 1.50s breathing bounce + gentle chin-up helmet nod Idle clip."""
    dur = 1.50
    n_steps = 16
    times = [round(dur * k / n_steps, 5) for k in range(n_steps + 1)]

    def _rest_t(node: str) -> Tuple[float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.translation if xf is not None else (0.0, 0.0, 0.0)

    def _rest_r(node: str) -> Tuple[float, float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)

    channels: List[AnimationChannel] = []
    pumps = [0.5 * (1.0 - math.cos(2.0 * math.pi * (k / n_steps))) for k in range(n_steps + 1)]
    pumps[-1] = pumps[0]

    bx, by, bz = _rest_t("upper_body")
    body_t = [(bx, by + 0.023 * p, bz - 0.003 * p) for p in pumps]
    body_t[-1] = body_t[0]
    channels.append(AnimationChannel("upper_body", "translation", list(times), body_t))

    r0_b = _rest_r("upper_body")
    body_r = [quat_mul(r0_b, quat_from_euler_xyz(rx=-0.024 * p)) for p in pumps]
    body_r[-1] = body_r[0]
    channels.append(AnimationChannel("upper_body", "rotation", list(times), body_r))

    r0_h = _rest_r("head_group")
    head_r = [
        quat_mul(
            r0_h,
            quat_from_euler_xyz(
                rx=-0.064 * pumps[k],
                rz=0.012 * math.sin(2.0 * math.pi * (k / n_steps)),
            ),
        )
        for k in range(n_steps + 1)
    ]
    head_r[-1] = head_r[0]
    channels.append(AnimationChannel("head_group", "rotation", list(times), head_r))

    if "magnifying_glass" in scene.nodes and scene.get_node("magnifying_glass"):
        r0_m = _rest_r("magnifying_glass")
        mag_r = [quat_mul(r0_m, quat_from_euler_xyz(rx=-0.028 * p, rz=-0.015 * p)) for p in pumps]
        mag_r[-1] = mag_r[0]
        channels.append(AnimationChannel("magnifying_glass", "rotation", list(times), mag_r))

    if "gaster_group" in scene.nodes:
        r0_g = _rest_r("gaster_group")
        g_r = [quat_mul(r0_g, quat_from_euler_xyz(rx=0.034 * p)) for p in pumps]
        g_r[-1] = g_r[0]
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), g_r))

    if "mandibles" in scene.nodes and scene.get_node("mandibles"):
        r0_j = _rest_r("mandibles")
        j_r = [quat_mul(r0_j, quat_from_euler_xyz(rx=0.018 * p)) for p in pumps]
        j_r[-1] = j_r[0]
        channels.append(AnimationChannel("mandibles", "rotation", list(times), j_r))

    clip = scene.add_animation("Idle", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Idle")
    sync_arm_idle_to_clip(scene, "Idle", left_scale=1.0, right_scale=0.6 if hold_magnifying_glass else 1.0)
    return clip


def _add_fire_attack(scene: SceneBuilder, *, hold_magnifying_glass: bool = False) -> AnimationClip:
    """Authentic `afat` 0.36s (6-frame [60x6]ms) rear-up windup into fluted-helmet battering-ram headbutt."""

    def _rest_t(node: str) -> Tuple[float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.translation if xf is not None else (0.0, 0.0, 0.0)

    def _rest_r(node: str) -> Tuple[float, float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)

    def _align_quats(qs: List[Tuple[float, float, float, float]]) -> List[Tuple[float, float, float, float]]:
        out = [qs[0]]
        for q in qs[1:]:
            if sum(a * b for a, b in zip(out[-1], q)) < 0.0:
                q = (-q[0], -q[1], -q[2], -q[3])
            out.append(q)
        return out

    # (t, dy, dz, ub_rx, head_rx, gaster_rx, arm_back_ry, arm_lift_rz, arm_pitch_rx, elbow_rx, jaw_rx)
    # f0-f3: rear tall & lean back lifting the 'A' shield; f4-f5: +1.16 rad battering-ram helmet slam!
    rows = [
        (0.00, 0.000, 0.000, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00),
        (0.06, 0.018, -0.010, -0.09, -0.07, 0.08, 0.14, 0.08, 0.06, -0.10, 0.03),
        (0.12, 0.032, -0.022, -0.17, -0.13, 0.15, 0.28, 0.16, 0.12, -0.20, 0.06),
        (0.18, 0.038, -0.028, -0.21, -0.16, 0.19, 0.36, 0.22, 0.16, -0.26, 0.08),
        (0.24, -0.036, 0.058, 1.14, 0.14, -0.24, 0.68, 0.88, -0.22, -0.55, -0.08),
        (0.30, -0.026, 0.044, 0.98, 0.09, -0.18, 0.56, 0.74, -0.16, -0.45, -0.05),
        (0.36, 0.000, 0.000, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00),
    ]
    times = [round(r[0], 4) for r in rows]
    bx, by, bz = _rest_t("upper_body")
    r0_b = _rest_r("upper_body")
    r0_h = _rest_r("head_group")
    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), [(bx, by + r[1], bz + r[2]) for r in rows]),
        AnimationChannel("upper_body", "rotation", list(times), _align_quats([quat_mul(r0_b, quat_from_euler_xyz(rx=r[3])) for r in rows])),
        AnimationChannel("head_group", "rotation", list(times), _align_quats([quat_mul(r0_h, quat_from_euler_xyz(rx=r[4])) for r in rows])),
    ]
    if "gaster_group" in scene.nodes:
        r0_g = _rest_r("gaster_group")
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), _align_quats([quat_mul(r0_g, quat_from_euler_xyz(rx=r[5])) for r in rows])))
    for side_tag, sign, scale in (("left", +1.0, 1.0), ("right", -1.0, 0.55 if hold_magnifying_glass else 1.0)):
        arm_n, fore_n = f"arm_{side_tag}", f"forearm_{side_tag}"
        if arm_n in scene.nodes and scene.get_node(arm_n):
            r0_a = _rest_r(arm_n)
            channels.append(
                AnimationChannel(
                    arm_n,
                    "rotation",
                    list(times),
                    _align_quats([
                        quat_mul(r0_a, quat_from_euler_xyz(rx=scale * r[8], ry=sign * scale * r[6], rz=sign * scale * r[7]))
                        for r in rows
                    ]),
                )
            )
        if fore_n in scene.nodes and scene.get_node(fore_n):
            r0_f = _rest_r(fore_n)
            channels.append(
                AnimationChannel(
                    fore_n,
                    "rotation",
                    list(times),
                    _align_quats([quat_mul(r0_f, quat_from_euler_xyz(rx=scale * r[9])) for r in rows]),
                )
            )
    if "mandibles" in scene.nodes and scene.get_node("mandibles"):
        r0_j = _rest_r("mandibles")
        channels.append(AnimationChannel("mandibles", "rotation", list(times), _align_quats([quat_mul(r0_j, quat_from_euler_xyz(rx=r[10])) for r in rows])))

    clip = scene.add_animation("Attack", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Attack", swivel=0.0)
    return clip


def _scale_fire_head(
    scene: SceneBuilder,
    anchors: dict,
    *,
    head_scale: float = 0.95,
) -> Tuple[
    Tuple[float, float, float],
    Tuple[float, float, float],
    Tuple[float, float, float],
    Tuple[float, float, float],
]:
    """Scale Fire's head/eyes/eyelids/mandibles to Goldilocks 0.95x beneath the Option 3+2 vintage helmet."""
    hc = anchors["head_center"]
    hrx, hry, hrz = anchors["head_radii"]
    head_xf = scene.get_node_transform("head")
    neck_pivot = (
        (-head_xf.translation[0], -head_xf.translation[1], -head_xf.translation[2])
        if head_xf is not None
        else anchors["neck_center"]
    )
    # Anchor scaling slightly below head center so the smaller cranium remains seated cleanly on neck_joint.
    piv = (hc[0], hc[1] - 0.044, hc[2])
    p_hg = (piv[0] - neck_pivot[0], piv[1] - neck_pivot[1], piv[2] - neck_pivot[2])
    s = float(head_scale)

    for node_name in ("head", "eyes"):
        if node_name in scene.nodes:
            scene.nodes[node_name] = [
                part.translate(-piv[0], -piv[1], -piv[2]).scale(s, s, s).translate(*piv)
                for part in scene.nodes[node_name]
            ]

    for node_name in ("mandibles", "eyelid_left", "eyelid_right"):
        if node_name in scene.nodes:
            scene.nodes[node_name] = [part.scale(s, s, s) for part in scene.nodes[node_name]]
            xf = scene.get_node_transform(node_name)
            if xf is not None:
                tx, ty, tz = xf.translation
                scene.set_node_transform(
                    node_name,
                    translation=(
                        p_hg[0] + s * (tx - p_hg[0]),
                        p_hg[1] + s * (ty - p_hg[1]),
                        p_hg[2] + s * (tz - p_hg[2]),
                    ),
                )

    def _scale_pt(pt: Tuple[float, float, float]) -> Tuple[float, float, float]:
        return (piv[0] + s * (pt[0] - piv[0]), piv[1] + s * (pt[1] - piv[1]), piv[2] + s * (pt[2] - piv[2]))

    new_hc = _scale_pt(hc)
    new_hr = (hrx * s, hry * s, hrz * s)
    new_top = _scale_pt(anchors["head_top"])
    new_eye_l = _scale_pt(anchors["eye_left"])
    return new_hc, new_hr, new_top, new_eye_l


def build_fire(blink: float = 0.0, *, hold_magnifying_glass: bool = False) -> SceneBuilder:
    """Build the sculpted 3D Fire Ant SceneBuilder (smaller cute head, oversized Option A hat, empty idle hands)."""
    scene, anchors = build_base_ant(
        scene_name="ant_fire",
        include_antennae=False,
        include_eyes=True,
        include_brows=False,
        brow_style="plucky",
        head_tilt_deg=3.5,
        head_yaw_deg=-3.0,
        head_pitch_deg=-15.5,
        eye_look=(0.05, 0.04),
        stance_asymmetry=0.18,
        leg_style="fire",
        include_idle=False,
        left_arm_pose="idle",
        right_arm_pose=_FIRE_RIGHT_ARM if hold_magnifying_glass else "idle",
        blink=blink,
    )
    orig_head_r = anchors["head_radii"]
    head_c, head_r, head_top, eye_l = _scale_fire_head(scene, anchors, head_scale=0.95)
    _build_fire_helmet(
        scene,
        head_c,
        head_r,
        head_top,
        eye_l,
        float(anchors.get("eyelid_closed_angle", 1.80)),
        orig_head_radii=orig_head_r,
    )
    scene.attach_world_node("fire_helmet", "head_group", preserve_world_pose=False)
    head_xf = scene.get_node_transform("head")
    if head_xf is not None:
        scene.set_node_transform("fire_helmet", translation=head_xf.translation)

    if hold_magnifying_glass:
        build_magnifying_glass(scene, anchors["right_hand"], anchors["right_hand_dir"])
        scene.set_node_parent("magnifying_glass", "forearm_right")
        mg_xf = scene.get_node_transform("magnifying_glass")
        er = anchors["elbow_right"]
        if mg_xf is not None:
            gx, gy, gz = mg_xf.translation
            scene.set_node_transform("magnifying_glass", translation=(gx - er[0], gy - er[1], gz - er[2]))

    _add_fire_idle(scene, hold_magnifying_glass=hold_magnifying_glass)
    _add_fire_attack(scene, hold_magnifying_glass=hold_magnifying_glass)
    return scene


def build_ant(blink: float = 0.0, *, hold_magnifying_glass: bool = False) -> SceneBuilder:
    """Canonical entry point discovered by build_all.py."""
    return build_fire(blink=blink, hold_magnifying_glass=hold_magnifying_glass)
