"""Stylized tabletop/vinyl 3D model for the nimble Thief Ant.

Built on `base_ant` with a subtly nimbler, mischievous forward-ready stance and
the Thief Ant's signature yellow/gold bandit gear:
- Form-fitting wrap-around yellow/gold bandit headband & domino eye-mask
  contouring cleanly around the big expressive eyes (zero eye burial)
- Subtle metallic-gold trim borders and sculpted cloth folds
- Bold sculpted back-left (+X, -Z) knot with two long dynamic fluttering
  triangular vinyl ribbon tails readable from Front, 3/4, Side, and Back views
- Playfully arched mischievous eyebrows and nimble sneaky arm pose

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet grounded cleanly at Y = 0.0)
  +Z = front (South / canonical camera 301)
  +X = ant's left (viewer's right from front), -X = ant's right
"""
from __future__ import annotations

import math
from typing import List, Sequence, Tuple

import numpy as np

try:
    from .base_ant import build_base_ant
    from .mesh_core import (
        MeshPart,
        SceneBuilder,
        catmull_rom_spline,
        curved_tube,
        recompute_smooth_normals,
        superellipsoid,
        torus_arc,
    )
except ImportError:
    from base_ant import build_base_ant  # type: ignore
    from mesh_core import (  # type: ignore
        MeshPart,
        SceneBuilder,
        catmull_rom_spline,
        curved_tube,
        recompute_smooth_normals,
        superellipsoid,
        torus_arc,
    )

# Nimble, slightly asymmetric sneaky hand poses (left poised high-forward, right balanced low-forward).
_THIEF_ARM_LEFT = [
    (0.145, 0.432, 0.028),
    (0.252, 0.388, 0.122),
    (0.218, 0.402, 0.244),
    (0.172, 0.422, 0.284),
]

_THIEF_ARM_RIGHT = [
    (-0.145, 0.432, 0.028),
    (-0.278, 0.398, 0.088),
    (-0.258, 0.340, 0.198),
    (-0.214, 0.334, 0.252),
]


def _normalize(v: np.ndarray, fallback: Tuple[float, float, float] = (0.0, 1.0, 0.0)) -> np.ndarray:
    n = float(np.linalg.norm(v))
    if n > 1e-10:
        return v / n
    fb = np.asarray(fallback, dtype=np.float64)
    return fb / float(np.linalg.norm(fb))


def _spow(val: float, exp: float) -> float:
    return math.copysign(abs(val) ** exp, val)


def _cranium_surface_point(theta: float, phi: float) -> np.ndarray:
    """Evaluate a point directly on `base_ant`'s sculpted cranium in head-local coordinates."""
    cp = _spow(math.cos(phi), 0.74)
    sp = _spow(math.sin(phi), 0.74)
    x = 0.252 * cp * _spow(math.sin(theta), 0.80)
    y = 0.228 * sp
    z = 0.242 * cp * _spow(math.cos(theta), 0.80)

    # Mirror `base_ant._head_sculpt_fn` so the headband fits flush without floating or clipping.
    cheek = math.exp(-((y + 0.055) / 0.12) ** 2) * max(0.0, min(1.0, (z + 0.04) / 0.28))
    x = x * (1.0 + 0.075 * cheek)
    z = z + 0.026 * cheek

    crown = max(0.0, min(1.0, (y - 0.06) / 0.18))
    x *= 1.0 - 0.04 * (crown ** 2)
    z -= 0.014 * (crown ** 2) * max(0.0, min(1.0, z / 0.24))

    chin = max(0.0, min(1.0, (-y - 0.09) / 0.14))
    x *= 1.0 - 0.18 * (chin ** 1.6)
    z *= 1.0 - 0.12 * (chin ** 1.6)
    return np.array([x, y, z], dtype=np.float64)


def _headband_phi_limits(theta: float) -> Tuple[float, float]:
    """Return `(phi_bot, phi_top)` for the wrap-around bandit headband at azimuth `theta`.

    `theta = 0` is front (+Z). Across the front forehead it rises cleanly above
    the eyes with a gentle dip at the nose bridge, while wrapping wide and bold
    across the temples and back of the head.
    """
    # Normalize angle to [-pi, +pi].
    ang = math.atan2(math.sin(theta), math.cos(theta))
    abs_a = abs(ang)

    # Top edge sweeps slightly higher at the front forehead, safely below the antennae.
    phi_top = 0.505 + 0.055 * math.exp(-((abs_a / 0.85) ** 2))

    # Base lower edge along sides and back wraps deeply around the cranium.
    phi_bot = -0.340

    # Eye-clearance arches on left and right front (centered around |theta| ~ 0.44 rad).
    eye_arch = math.exp(-(((abs_a - 0.44) / 0.30) ** 2))
    phi_bot += 0.595 * eye_arch

    # Gentle brow-bridge V-dip at the nose bridge between the two eyes.
    bridge_dip = math.exp(-((abs_a / 0.16) ** 2))
    phi_bot += 0.280 * bridge_dip
    return phi_bot, phi_top


def _build_headband_shell(head_center: Tuple[float, float, float]) -> MeshPart:
    """Build a watertight, form-fitting sculpted vinyl headband shell around the cranium."""
    hx, hy, hz = head_center
    origin = np.array([hx, hy, hz], dtype=np.float64)

    u_segs = 64
    v_segs = 10
    thetas = np.linspace(0.0, 2.0 * math.pi, u_segs, endpoint=False)
    vs = np.linspace(0.0, 1.0, v_segs)

    # Cross-section loop around the thickness of the band at each azimuth:
    # outer surface (bot -> top), top bevel rim, inner surface (top -> bot), bottom bevel rim.
    outer_grid = np.zeros((u_segs, v_segs, 3), dtype=np.float64)
    inner_grid = np.zeros((u_segs, v_segs, 3), dtype=np.float64)

    for ui, theta in enumerate(thetas):
        phi_b, phi_t = _headband_phi_limits(float(theta))
        for vi, v in enumerate(vs):
            phi = phi_b + v * (phi_t - phi_b)
            surf = _cranium_surface_point(float(theta), float(phi))
            rad_dir = _normalize(surf)

            # Soft rounded edge taper near v=0 and v=1 + subtle cloth-fold ridge ripple.
            edge_soft = math.sin(v * math.pi) ** 0.35
            fold_wave = 0.0045 * math.sin(v * 2.0 * math.pi + 0.6 * math.cos(theta)) * (1.0 - 0.5 * math.exp(-((theta / 0.5) ** 2)))
            thick_out = 0.012 + 0.015 * edge_soft + fold_wave
            thick_in = -0.010

            outer_grid[ui, vi] = origin + surf + thick_out * rad_dir
            inner_grid[ui, vi] = origin + surf + thick_in * rad_dir

    # Build a closed cross-section loop of size M = 2 * v_segs + 2 at every theta slice.
    # Order: outer[0..v_segs-1], top_mid, inner[v_segs-1..0], bot_mid.
    ring_len = 2 * v_segs + 2
    positions_list: List[np.ndarray] = []
    for ui in range(u_segs):
        top_dir = _normalize(outer_grid[ui, -1] - outer_grid[ui, -2])
        bot_dir = _normalize(outer_grid[ui, 0] - outer_grid[ui, 1])
        top_mid = 0.5 * (outer_grid[ui, -1] + inner_grid[ui, -1]) + 0.0065 * top_dir
        bot_mid = 0.5 * (outer_grid[ui, 0] + inner_grid[ui, 0]) + 0.0065 * bot_dir

        slice_pts = np.vstack([
            outer_grid[ui],
            top_mid[None, :],
            inner_grid[ui, ::-1],
            bot_mid[None, :],
        ])
        positions_list.append(slice_pts)

    positions = np.vstack(positions_list).astype(np.float32)
    tris: List[Tuple[int, int, int]] = []
    for ui in range(u_segs):
        b0 = ui * ring_len
        b1 = ((ui + 1) % u_segs) * ring_len
        for k in range(ring_len):
            nk = (k + 1) % ring_len
            tris.append((b0 + k, b1 + k, b1 + nk))
            tris.append((b0 + k, b1 + nk, b0 + nk))

    indices = np.asarray(tris, dtype=np.uint32).reshape(-1)
    part = MeshPart("bandit_headband", "gear_gold", positions, recompute_smooth_normals(positions, indices), indices)
    return part.ensure_outward()


def _add_domino_mask_and_trim(scene: SceneBuilder, head_center: Tuple[float, float, float]) -> None:
    """Add lower domino eye-mask cheek contours, nose bridge, and metallic-gold trim lines."""
    hx, hy, hz = head_center
    origin = np.array([hx, hy, hz], dtype=np.float64)

    # Lower domino mask cheek rim curving snugly beneath each eye from nose bridge to temple.
    lower_angles_phis = [
        (0.00, -0.10),
        (0.14, -0.34),
        (0.36, -0.50),
        (0.58, -0.44),
        (0.76, -0.26),
        (0.92, -0.12),
    ]
    lower_pts_left = []
    for th, ph in lower_angles_phis:
        surf = _cranium_surface_point(th, ph)
        lower_pts_left.append(origin + surf + 0.020 * _normalize(surf))

    lower_mask_left = curved_tube(
        lower_pts_left,
        [0.022, 0.021, 0.022, 0.023, 0.025, 0.026],
        samples_per_seg=4,
        radial_segs=10,
        cap_rings=2,
        name="domino_lower_rim",
        material="gear_gold",
    )
    scene.add_mirrored("gear_bandana", lower_mask_left)

    # Sculpted vertical nose-bridge connector joining forehead band cleanly to lower eye rims.
    bridge_pts = []
    for ph in (0.34, 0.14, -0.04, -0.18):
        surf = _cranium_surface_point(0.0, ph)
        bridge_pts.append(origin + surf + 0.022 * _normalize(surf))
    nose_bridge = curved_tube(
        bridge_pts,
        [0.026, 0.022, 0.021, 0.022],
        samples_per_seg=3,
        radial_segs=10,
        cap_rings=2,
        name="domino_nose_bridge",
        material="gear_gold",
    )
    scene.add("gear_bandana", nose_bridge)

    # Metallic-gold sculpted upper and lower border trim piping for collector vinyl polish.
    u_trim = 48
    thetas = np.linspace(0.0, 2.0 * math.pi, u_trim, endpoint=False)
    top_trim_pts = []
    bot_trim_pts = []
    for theta in thetas:
        phi_b, phi_t = _headband_phi_limits(float(theta))
        s_top = _cranium_surface_point(float(theta), phi_t - 0.04)
        top_trim_pts.append(origin + s_top + 0.025 * _normalize(s_top))
        s_bot = _cranium_surface_point(float(theta), phi_b + 0.04)
        bot_trim_pts.append(origin + s_bot + 0.024 * _normalize(s_bot))

    scene.add(
        "gear_bandana",
        curved_tube(
            top_trim_pts,
            0.0068,
            samples_per_seg=2,
            radial_segs=8,
            closed=True,
            name="headband_trim_top",
            material="gear_gold_metal",
        ),
    )
    scene.add(
        "gear_bandana",
        curved_tube(
            bot_trim_pts,
            0.0062,
            samples_per_seg=2,
            radial_segs=8,
            closed=True,
            name="headband_trim_bottom",
            material="gear_gold_metal",
        ),
    )


def _add_mischievous_brows(scene: SceneBuilder, head_center: Tuple[float, float, float]) -> None:
    """Add sly, playfully arched mischievous brows right beneath the golden brow line."""
    _, hy, hz = head_center
    eye_y = hy - 0.018
    eye_z = hz + 0.224

    # Ant's left (+X) brow: playfully cocked higher with a confident sneaky arch.
    left_brow_pts = [
        (0.036, eye_y + 0.068, eye_z + 0.044),
        (0.108, eye_y + 0.102, eye_z + 0.042),
        (0.178, eye_y + 0.084, eye_z + 0.002),
    ]
    scene.add(
        "eyes",
        curved_tube(
            left_brow_pts,
            [0.012, 0.016, 0.011],
            samples_per_seg=4,
            radial_segs=8,
            cap_rings=2,
            name="sneaky_brow_left",
            material="chitin_dark",
        ),
    )

    # Ant's right (-X) brow: subtly lowered inward for a mischievous bandit squint.
    right_brow_pts = [
        (-0.034, eye_y + 0.058, eye_z + 0.044),
        (-0.108, eye_y + 0.088, eye_z + 0.042),
        (-0.180, eye_y + 0.078, eye_z + 0.000),
    ]
    scene.add(
        "eyes",
        curved_tube(
            right_brow_pts,
            [0.012, 0.016, 0.011],
            samples_per_seg=4,
            radial_segs=8,
            cap_rings=2,
            name="sneaky_brow_right",
            material="chitin_dark",
        ),
    )


def _sculpted_bandana_tail(
    control_pts: Sequence[Tuple[float, float, float]],
    half_widths: Sequence[float],
    half_thicknesses: Sequence[float],
    face_normal_hint: Tuple[float, float, float],
    twist_deg_range: Tuple[float, float],
    *,
    name: str,
    material: str = "gear_gold",
) -> MeshPart:
    """Build a thick, dynamic fluttering triangular vinyl ribbon tail with rounded caps."""
    pts = catmull_rom_spline(control_pts, samples_per_seg=7, closed=False)
    n = len(pts)
    u = np.linspace(0.0, 1.0, n)
    w_arr = np.interp(u, np.linspace(0.0, 1.0, len(half_widths)), half_widths)
    t_arr = np.interp(u, np.linspace(0.0, 1.0, len(half_thicknesses)), half_thicknesses)
    twists = np.radians(np.linspace(twist_deg_range[0], twist_deg_range[1], n))

    tangents = np.zeros((n, 3), dtype=np.float64)
    for i in range(n):
        d = pts[1] - pts[0] if i == 0 else (pts[-1] - pts[-2] if i == n - 1 else pts[i + 1] - pts[i - 1])
        tangents[i] = _normalize(d)

    profile_segs = 14
    angs = np.linspace(0.0, 2.0 * math.pi, profile_segs, endpoint=False)
    # Plump diamond-pill vinyl cross-section so ribbon reads cleanly at every silhouette angle.
    cu = np.sign(np.cos(angs)) * (np.abs(np.cos(angs)) ** 0.72)
    su = np.sign(np.sin(angs)) * (np.abs(np.sin(angs)) ** 0.72)

    hint = _normalize(np.asarray(face_normal_hint, dtype=np.float64))
    rings: List[np.ndarray] = []
    frame_o = np.zeros((n, 3), dtype=np.float64)
    frame_w = np.zeros((n, 3), dtype=np.float64)

    for i in range(n):
        tg = tangents[i]
        o0 = _normalize(hint - np.dot(hint, tg) * tg, (0.0, 0.0, 1.0))
        w0 = _normalize(np.cross(o0, tg), (0.0, 1.0, 0.0))
        o0 = _normalize(np.cross(tg, w0))
        ct, st = math.cos(twists[i]), math.sin(twists[i])
        o = ct * o0 + st * w0
        w = -st * o0 + ct * w0
        frame_o[i], frame_w[i] = o, w
        ring = pts[i][None, :] + (w_arr[i] * cu)[:, None] * w[None, :] + (t_arr[i] * su)[:, None] * o[None, :]
        rings.append(ring)

    # Rounded hemispherical start/end caps so the vinyl ribbon is completely closed and smooth.
    cap_rings = 3
    start_caps: List[np.ndarray] = []
    start_apex = pts[0] - t_arr[0] * tangents[0]
    for k in range(cap_rings - 1, 0, -1):
        ph = (k / cap_rings) * (0.5 * math.pi)
        cp, sp = math.cos(ph), math.sin(ph)
        ctr = pts[0] - (t_arr[0] * sp) * tangents[0]
        start_caps.append(ctr[None, :] + (w_arr[0] * cp * cu)[:, None] * frame_w[0][None, :] + (t_arr[0] * cp * su)[:, None] * frame_o[0][None, :])

    end_caps: List[np.ndarray] = []
    end_apex = pts[-1] + (1.4 * t_arr[-1]) * tangents[-1]
    for k in range(1, cap_rings):
        ph = (k / cap_rings) * (0.5 * math.pi)
        cp, sp = math.cos(ph), math.sin(ph)
        ctr = pts[-1] + (1.4 * t_arr[-1] * sp) * tangents[-1]
        end_caps.append(ctr[None, :] + (w_arr[-1] * cp * cu)[:, None] * frame_w[-1][None, :] + (t_arr[-1] * cp * su)[:, None] * frame_o[-1][None, :])

    all_rings = start_caps + rings + end_caps
    positions = np.vstack([start_apex[None, :]] + all_rings + [end_apex[None, :]]).astype(np.float32)

    n_rings = len(all_rings)
    end_idx = 1 + n_rings * profile_segs
    tris: List[Tuple[int, int, int]] = []
    for k in range(profile_segs):
        nk = (k + 1) % profile_segs
        tris.append((0, 1 + nk, 1 + k))
    for r in range(n_rings - 1):
        b0 = 1 + r * profile_segs
        b1 = 1 + (r + 1) * profile_segs
        for k in range(profile_segs):
            nk = (k + 1) % profile_segs
            tris.append((b0 + k, b0 + nk, b1 + nk))
            tris.append((b0 + k, b1 + nk, b1 + k))
    b_last = 1 + (n_rings - 1) * profile_segs
    for k in range(profile_segs):
        nk = (k + 1) % profile_segs
        tris.append((end_idx, b_last + k, b_last + nk))

    indices = np.asarray(tris, dtype=np.uint32).reshape(-1)
    part = MeshPart(name, material, positions, recompute_smooth_normals(positions, indices), indices)
    return part.ensure_outward()


def _add_knot_and_fluttering_tails(scene: SceneBuilder, head_center: Tuple[float, float, float]) -> None:
    """Build the bold back-left (+X, -Z) knot and twin dynamic fluttering triangular tails."""
    hx, hy, hz = head_center
    origin = np.array([hx, hy, hz], dtype=np.float64)

    # Anchor squarely on the back-left (+X, -Z) of the headband (~108 deg from +Z front)
    # so both the knot and twin fluttering tails pop boldly across Front, 3/4, Side, and Back views.
    knot_theta = math.radians(108.0)
    surf = _cranium_surface_point(knot_theta, 0.06)
    rad = _normalize(surf)
    knot_ctr = origin + surf + 0.036 * rad

    # Plump sculpted central vinyl knot nested flush against the headband.
    knot_core = (
        superellipsoid(0.064, 0.058, 0.058, e1=0.76, e2=0.78, u_segs=18, v_segs=14, name="bandana_knot", material="gear_gold")
        .rotate_xyz(rx=0.16, ry=-0.34, rz=0.18)
        .translate(knot_ctr)
    )
    scene.add("gear_bandana", knot_core)

    # Sculpted cinch fold wrap around the knot waist (metallic gold accent catches rim light).
    knot_wrap = (
        torus_arc(0.045, 0.014, u_segs=18, v_segs=8, plane="xy", name="knot_cinch_fold", material="gear_gold_metal")
        .rotate_xyz(rx=0.22, ry=1.22, rz=0.20)
        .translate(knot_ctr + 0.012 * rad)
    )
    scene.add("gear_bandana", knot_wrap)

    # Upper dynamic fluttering triangular tail sweeping UP, OUT (+X), and BACK (-Z).
    # Broad face angled cleanly toward Front-3/4 so it never appears edge-on from 201 SE or Hero.
    kx, ky, kz = float(knot_ctr[0]), float(knot_ctr[1]), float(knot_ctr[2])
    upper_ctrl = [
        (kx - 0.010, ky + 0.012, kz + 0.004),
        (kx + 0.066, ky + 0.074, kz - 0.048),
        (kx + 0.146, ky + 0.172, kz - 0.118),
        (kx + 0.208, ky + 0.258, kz - 0.184),
        (kx + 0.244, ky + 0.312, kz - 0.228),
    ]
    upper_tail = _sculpted_bandana_tail(
        upper_ctrl,
        half_widths=[0.025, 0.052, 0.078, 0.058, 0.013],
        half_thicknesses=[0.021, 0.023, 0.022, 0.017, 0.010],
        face_normal_hint=(-0.42, 0.28, 0.86),
        twist_deg_range=(-8.0, 26.0),
        name="bandana_tail_upper",
        material="gear_gold",
    )
    scene.add("gear_bandana", upper_tail)

    # Lower dynamic fluttering triangular tail fanning distinctly OUT (+X), LOWER, and BACK (-Z)
    # creating an unmistakable twin-pronged V-silhouette from Front, Hero, 3/4, Side, and Back.
    lower_ctrl = [
        (kx - 0.008, ky - 0.014, kz - 0.002),
        (kx + 0.082, ky - 0.048, kz - 0.056),
        (kx + 0.178, ky - 0.088, kz - 0.140),
        (kx + 0.256, ky - 0.082, kz - 0.214),
        (kx + 0.304, ky - 0.054, kz - 0.264),
    ]
    lower_tail = _sculpted_bandana_tail(
        lower_ctrl,
        half_widths=[0.025, 0.048, 0.072, 0.054, 0.012],
        half_thicknesses=[0.020, 0.022, 0.021, 0.016, 0.010],
        face_normal_hint=(-0.22, 0.55, 0.80),
        twist_deg_range=(12.0, -20.0),
        name="bandana_tail_lower",
        material="gear_gold",
    )
    scene.add("gear_bandana", lower_tail)


def build_thief() -> SceneBuilder:
    """Build the nimble stylized vinyl Thief Ant with yellow bandit mask and twin tails."""
    scene, anchors = build_base_ant(
        scene_name="ant_thief",
        include_antennae=True,
        include_eyes=True,
        include_brows=False,
        left_arm_pose=_THIEF_ARM_LEFT,
        right_arm_pose=_THIEF_ARM_RIGHT,
        head_tilt_x=0.0,
        scale=1.0,
    )
    head_center = anchors["head_center"]
    scene.add("gear_bandana", _build_headband_shell(head_center))
    _add_domino_mask_and_trim(scene, head_center)
    _add_mischievous_brows(scene, head_center)
    _add_knot_and_fluttering_tails(scene, head_center)

    # Subtle nimble scale & slight crouched-forward vibe while staying cleanly grounded at Y = 0.0.
    scene.scale_all(0.98)
    scene.ground_to_y(0.0)
    return scene


def build_ant() -> SceneBuilder:
    """Canonical module entrypoint for `build_all.py`."""
    return build_thief()


__all__ = ["build_ant", "build_thief"]
