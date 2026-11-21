"""Sculpted 3D Bomber Ant (compact riveted aviator goggles, empty idle hands, 4-bomb pack, 0.80s Idle).

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
    from .base_ant import build_base_ant, sync_leg_flex_to_clip
    from .mesh_core import (
        AnimationChannel,
        AnimationClip,
        MeshPart,
        SceneBuilder,
        curved_tube,
        cylinder,
        ellipsoid,
        lathe,
        quat_from_euler_xyz,
        quat_mul,
        rounded_box,
        sphere,
        swept_tube,
        torus_arc,
    )
except ImportError:
    from base_ant import build_base_ant, sync_leg_flex_to_clip  # type: ignore
    from mesh_core import (  # type: ignore
        AnimationChannel,
        AnimationClip,
        MeshPart,
        SceneBuilder,
        curved_tube,
        cylinder,
        ellipsoid,
        lathe,
        quat_from_euler_xyz,
        quat_mul,
        rounded_box,
        sphere,
        swept_tube,
        torus_arc,
    )

_BOMB_CHARGE_MAT = "bomb_charge"
_BOMB_CUP_TRIM_MAT = "bomb_cup_trim"


def _spow(val: float, exp: float) -> float:
    return math.copysign(abs(val) ** exp, val)


def _hollow_ring_lathe(
    profile_rz: List[Tuple[float, float]],
    *,
    radial_segs: int = 30,
    name: str,
    material: str,
) -> MeshPart:
    """Build a closed watertight hollow annular ring around +Z with positive outward winding."""
    closed_prof = list(profile_rz) + [profile_rz[0]]
    return (
        lathe(
            closed_prof,
            radial_segs=radial_segs,
            cap_bottom=False,
            cap_top=False,
            axis="z",
            name=name,
            material=material,
        )
        .ensure_outward()
    )


def _measure_eyelid_sweep_in_goggle_frame(
    scene: SceneBuilder,
    closed_angle: float,
    goggle_pitch: float,
    goggle_yaw: float,
    mount_offset: Tuple[float, float, float],
) -> Tuple[float, float]:
    """Measure max (r_xy, z) reached by eyelid_left across full blink=0..1 in goggle-local space."""
    parts = scene.nodes.get("eyelid_left", [])
    if not parts:
        return (0.092, 0.092)
    verts = np.vstack([p.positions.astype(np.float64) for p in parts])
    cp, sp = math.cos(goggle_pitch), math.sin(goggle_pitch)
    cy, sy = math.cos(goggle_yaw), math.sin(goggle_yaw)
    rx_m = np.array([[1.0, 0.0, 0.0], [0.0, cp, -sp], [0.0, sp, cp]], dtype=np.float64)
    ry_m = np.array([[cy, 0.0, sy], [0.0, 1.0, 0.0], [-sy, 0.0, cy]], dtype=np.float64)
    inv_rot = (ry_m @ rx_m).T
    off = np.array(mount_offset, dtype=np.float64)

    max_r_xy = 0.0
    max_z = 0.0
    for frac in (0.0, 0.25, 0.50, 0.75, 1.0):
        ang = frac * closed_angle
        ca, sa = math.cos(ang), math.sin(ang)
        r_blink = np.array([[1.0, 0.0, 0.0], [0.0, ca, -sa], [0.0, sa, ca]], dtype=np.float64)
        p_eye = verts @ r_blink.T
        p_loc = (p_eye - off) @ inv_rot.T
        r_xy = float(np.hypot(p_loc[:, 0], p_loc[:, 1]).max())
        z_hi = float(p_loc[:, 2].max())
        if r_xy > max_r_xy:
            max_r_xy = r_xy
        if z_hi > max_z:
            max_z = z_hi
    return (max_r_xy, max_z)


def _build_aviator_cup(
    side_sign: float,
    bore_r: float,
    lens_z: float,
) -> List[MeshPart]:
    """Build one narrow, forward-projecting copper-and-gold aviator goggle barrel (7 gold studs).

    Keeps a sleek narrow outer radius hugging the compact eyes while extending ~1.45x deeper along +Z
    so the twin projecting barrels read crisply in 201 SE and 901 E with clear see-through lenses.
    """
    parts: List[MeshPart] = []
    tag = "left" if side_sign > 0 else "right"

    z_back = -0.020
    z_copper_start = 0.008
    z_rim_start = lens_z - 0.018
    z_rim_front = lens_z + 0.019

    r_in = bore_r
    r_copper_out = bore_r + 0.0135
    r_rim_out = bore_r + 0.0120

    # 1. Snug dark rubber face cushion tucking neatly onto the orbital rim.
    cushion_prof = [
        (r_in + 0.001, z_back),
        (r_copper_out - 0.003, z_back - 0.002),
        (r_copper_out + 0.001, z_back + 0.014),
        (r_copper_out, z_copper_start + 0.004),
        (r_in + 0.001, z_copper_start + 0.004),
    ]
    parts.append(
        _hollow_ring_lathe(
            cushion_prof,
            radial_segs=30,
            name=f"goggle_cushion_{tag}",
            material="gear_dark",
        )
    )

    # 2. Elongated warm copper / orange-brass binocular barrel wall projecting cleanly forward.
    z_mid = z_copper_start + 0.55 * (z_rim_start - z_copper_start)
    copper_prof = [
        (r_in, z_copper_start),
        (r_copper_out - 0.001, z_copper_start),
        (r_copper_out + 0.0018, z_copper_start + 0.024),
        (r_copper_out + 0.0022, z_mid),
        (r_copper_out + 0.001, z_rim_start + 0.004),
        (r_in, z_rim_start + 0.004),
    ]
    parts.append(
        _hollow_ring_lathe(
            copper_prof,
            radial_segs=32,
            name=f"goggle_copper_ring_{tag}",
            material="gear_brass_trim",
        )
    )

    # Subtle mid-barrel metallic brass accent ring adding sculptural definition along the longer barrel.
    mid_band = (
        torus_arc(r_copper_out + 0.001, 0.0032, u_segs=28, v_segs=8, plane="xy", name=f"goggle_mid_band_{tag}", material="gear_gold_metal")
        .translate(0.0, 0.0, z_copper_start + 0.30 * (z_rim_start - z_copper_start))
    )
    parts.append(mid_band)

    # 3. Bold beveled bright yellow-gold front lens hood rim framing the recessed clear glass lens.
    rim_prof = [
        (r_in - 0.001, z_rim_start),
        (r_rim_out - 0.002, z_rim_start),
        (r_rim_out + 0.0015, z_rim_start + 0.009),
        (r_rim_out + 0.001, z_rim_front - 0.006),
        (r_rim_out - 0.004, z_rim_front),
        (r_in + 0.003, z_rim_front),
        (r_in - 0.001, z_rim_front - 0.005),
        (r_in - 0.001, z_rim_start + 0.004),
    ]
    parts.append(
        _hollow_ring_lathe(
            rim_prof,
            radial_segs=32,
            name=f"goggle_front_rim_{tag}",
            material="gear_gold",
        )
    )

    # Crisp thin dark inner gasket ring right behind the glass edge.
    inner_gasket = (
        torus_arc(r_in, 0.0028, u_segs=26, v_segs=8, plane="xy", name=f"goggle_inner_gasket_{tag}", material="gear_dark")
        .translate(0.0, 0.0, lens_z + 0.004)
    )
    parts.append(inner_gasket)

    # 4. Thin see-through tinted optical glass lens seated inside the projecting front hood.
    lens = (
        ellipsoid(r_in + 0.001, r_in + 0.001, 0.006, u_segs=28, v_segs=14, name=f"goggle_lens_{tag}", material="glass_tinted")
        .translate(0.0, 0.0, lens_z)
    )
    parts.append(lens)

    # 5. Seven crisp bright yellow-gold rivets studded around the projecting copper wall.
    stud_r = r_copper_out + 0.0026
    stud_z = z_mid + 0.012
    for r_idx, ang_deg in enumerate((-26.0, 4.0, 34.0, 64.0, 94.0, 124.0, 154.0)):
        ang = math.radians(ang_deg)
        sx = side_sign * stud_r * math.sin(ang)
        sy = stud_r * math.cos(ang)
        rivet = (
            sphere(0.0062, u_segs=10, v_segs=8, name=f"goggle_stud_{tag}_{r_idx}", material="gear_gold")
            .translate(sx, sy, stud_z)
        )
        parts.append(rivet)

    return parts


def _build_goggles(
    scene: SceneBuilder,
    head_center: Tuple[float, float, float],
    head_radii: Tuple[float, float, float],
    eye_left: Tuple[float, float, float],
    eye_right: Tuple[float, float, float],
    closed_angle: float,
) -> None:
    """Build narrow-width, forward-projecting copper-and-gold aviator goggles & rear strap."""
    hx, _, hz = head_center
    hrx, _, hrz = head_radii
    ey = 0.5 * (eye_left[1] + eye_right[1])
    ez = 0.5 * (eye_left[2] + eye_right[2])

    pitch = -0.13
    yaw_mag = 0.21
    mount_dx = 0.0
    mount_dz = 0.001

    max_lid_r, max_lid_z = _measure_eyelid_sweep_in_goggle_frame(
        scene,
        closed_angle=closed_angle,
        goggle_pitch=pitch,
        goggle_yaw=yaw_mag,
        mount_offset=(mount_dx, 0.0, mount_dz),
    )
    # Narrow radial fit around Bomber's compact head + bold projecting binocular barrel length.
    bore_r = max(0.0785, max_lid_r + 0.0036)
    lens_z = max(0.114, max_lid_z + 0.038)

    for side_sign, eye_pos in ((+1.0, eye_left), (-1.0, eye_right)):
        mount = (eye_pos[0] + side_sign * mount_dx, eye_pos[1], eye_pos[2] + mount_dz)
        for part in _build_aviator_cup(side_sign, bore_r=bore_r, lens_z=lens_z):
            placed = part.rotate_xyz(rx=pitch, ry=side_sign * yaw_mag).translate(*mount)
            if part.name.startswith(("goggle_cushion_", "goggle_copper_ring_", "goggle_mid_band_")):
                def _clamp_nose_midline(pos: np.ndarray, s: float = side_sign) -> np.ndarray:
                    p = pos.copy()
                    if s > 0.0:
                        p[:, 0] = np.maximum(p[:, 0], 0.0)
                    else:
                        p[:, 0] = np.minimum(p[:, 0], 0.0)
                    return p
                placed = placed.deform(_clamp_nose_midline)
            scene.add("goggles", placed)

    # Sleek copper nose bridge & gold center rivet joining the two projecting barrels cleanly.
    bridge_z = ez + lens_z * 0.68
    bridge = (
        rounded_box(0.036, 0.022, 0.026, bevel=0.006, segs=3, name="goggle_bridge", material="gear_brass_trim")
        .translate(hx, ey + 0.011, bridge_z)
    )
    scene.add("goggles", bridge)

    bridge_stud = (
        cylinder(0.0072, 0.0072, 0.010, radial_segs=14, bevel=0.0022, axis="z", name="goggle_bridge_stud", material="gear_gold")
        .translate(hx, ey + 0.011, bridge_z + 0.013)
    )
    scene.add("goggles", bridge_stud)

    # Compact temple hinges connecting outer goggle cups smoothly into the dark rear head strap.
    rx_strap = hrx + 0.008
    rz_strap = hrz + 0.009
    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        clasp = (
            rounded_box(0.022, 0.038, 0.040, bevel=0.007, segs=3, name=f"goggle_clasp_{tag}", material="gear_brass_trim")
            .rotate_xyz(ry=side_sign * 0.34)
            .translate(hx + side_sign * (hrx - 0.010), ey + 0.004, hz + hrz * 0.56)
        )
        scene.add("goggles", clasp)

    # Snug dark rubber head strap hugging the compact soft-cube cranium contour (e2=0.76).
    strap_pts: List[Tuple[float, float, float]] = []
    n_arc = 28
    start_ang = math.radians(52.0)
    end_ang = math.radians(308.0)
    for i in range(n_arc + 1):
        ang = start_ang + (end_ang - start_ang) * (i / n_arc)
        sx = hx + rx_strap * _spow(math.sin(ang), 0.76)
        sz = hz + rz_strap * _spow(math.cos(ang), 0.76)
        rear_lift = 0.012 * (0.5 * (1.0 - math.cos(ang)))
        strap_pts.append((sx, ey + 0.005 + rear_lift, sz))

    strap = swept_tube(
        strap_pts,
        (0.024, 0.011),
        radial_segs=12,
        cap_start=True,
        cap_end=True,
        closed=False,
        cap_rings=3,
        name="goggle_strap",
        material="gear_dark",
    )
    scene.add("goggles", strap)

    rear_buckle = (
        rounded_box(0.062, 0.056, 0.022, bevel=0.007, segs=3, name="goggle_rear_buckle", material="gear_gold_metal")
        .translate(hx, ey + 0.018, hz - rz_strap - 0.003)
    )
    scene.add("goggles", rear_buckle)


def _build_cup_bomb(
    prefix: str,
    cup_h: float,
    fuse_curve_x: float,
    *,
    lit: bool = False,
    rivet_count: int = 10,
) -> List[MeshPart]:
    """Build one tapered open-cup team bomb with riveted iron rim, flat magenta drum, and rope fuse."""
    h = float(cup_h)
    parts: List[MeshPart] = []

    def r_at(u: float) -> float:
        return (0.37 + 0.26 * u) * h

    foot = (
        cylinder(0.43 * h, 0.41 * h, 0.11 * h, radial_segs=18, bevel=0.025 * h, axis="y", name=f"{prefix}_foot", material="gear_dark")
        .translate(0.0, 0.055 * h, 0.0)
    )
    parts.append(foot)

    bot_band = (
        cylinder(r_at(0.00), r_at(0.16), 0.16 * h, radial_segs=20, bevel=0.008 * h, axis="y", name=f"{prefix}_cup_bot", material=_BOMB_CUP_TRIM_MAT)
        .translate(0.0, 0.08 * h, 0.0)
    )
    parts.append(bot_band)

    mid_wall = (
        cylinder(
            r_at(0.15) + 0.014 * h,
            r_at(0.83) + 0.014 * h,
            0.68 * h,
            radial_segs=22,
            bevel=0.012 * h,
            axis="y",
            name=f"{prefix}_cup_team",
            material="chitin_team",
        )
        .translate(0.0, 0.49 * h, 0.0)
    )
    parts.append(mid_wall)

    top_band = (
        cylinder(r_at(0.82), r_at(0.99), 0.17 * h, radial_segs=20, bevel=0.008 * h, axis="y", name=f"{prefix}_cup_top", material=_BOMB_CUP_TRIM_MAT)
        .translate(0.0, 0.905 * h, 0.0)
    )
    parts.append(top_band)

    for h_idx, u_hoop in enumerate((0.15, 0.82)):
        hoop = (
            torus_arc(r_at(u_hoop) + 0.018 * h, 0.024 * h, u_segs=22, v_segs=8, plane="xz", name=f"{prefix}_hoop_{h_idx}", material="gear_dark")
            .translate(0.0, u_hoop * h, 0.0)
        )
        parts.append(hoop)

    interior = (
        cylinder(0.56 * h, 0.56 * h, 0.10 * h, radial_segs=20, bevel=0.010 * h, axis="y", name=f"{prefix}_cup_well", material="gear_dark")
        .translate(0.0, 0.80 * h, 0.0)
    )
    parts.append(interior)

    rim = (
        torus_arc(0.61 * h, 0.072 * h, u_segs=24, v_segs=10, plane="xz", name=f"{prefix}_rim", material="gear_dark")
        .translate(0.0, 1.00 * h, 0.0)
    )
    parts.append(rim)

    for rv_idx in range(rivet_count):
        ang = (rv_idx / rivet_count) * 2.0 * math.pi
        rv = (
            sphere(0.030 * h, u_segs=8, v_segs=6, name=f"{prefix}_rivet_{rv_idx}", material="gear_steel")
            .translate(0.675 * h * math.cos(ang), 1.00 * h, 0.675 * h * math.sin(ang))
        )
        parts.append(rv)

    drum = (
        cylinder(
            0.52 * h,
            0.52 * h,
            0.88 * h,
            radial_segs=22,
            bevel=0.036 * h,
            axis="y",
            name=f"{prefix}_charge_drum",
            material=_BOMB_CHARGE_MAT,
        )
        .translate(0.0, 1.19 * h, 0.0)
    )
    parts.append(drum)

    socket = (
        cylinder(0.098 * h, 0.090 * h, 0.044 * h, radial_segs=14, bevel=0.008 * h, axis="y", name=f"{prefix}_fuse_socket", material="gear_dark")
        .translate(0.0, 1.635 * h, 0.0)
    )
    parts.append(socket)

    fdir = 1.0 if fuse_curve_x >= 0.0 else -1.0
    fspan = max(0.32 * h, abs(fuse_curve_x))
    fuse_pts = [
        (0.0, 1.63 * h, 0.0),
        (0.04 * fdir * fspan, 1.94 * h, 0.02 * h),
        (0.45 * fdir * fspan, 2.24 * h, 0.04 * h),
        (1.00 * fdir * fspan, 2.34 * h, 0.02 * h),
    ]
    fuse = curved_tube(
        fuse_pts,
        [0.056 * h, 0.052 * h, 0.048 * h, 0.044 * h],
        samples_per_seg=4,
        radial_segs=8,
        cap_rings=2,
        name=f"{prefix}_fuse",
        material="fuse_white",
    )
    parts.append(fuse)

    tip_r = 0.095 * h if lit else 0.056 * h
    tip_mat = "gear_gold" if lit else "gear_dark"
    tip = sphere(tip_r, u_segs=10, v_segs=8, name=f"{prefix}_fuse_tip", material=tip_mat).translate(*fuse_pts[-1])
    parts.append(tip)

    return parts


def _build_dynamite_pack(
    scene: SceneBuilder,
    back_anchor: Tuple[float, float, float],
    thorax_center: Tuple[float, float, float],
    head_back_z: float,
) -> None:
    """Build sleek dorsal harness holding 4 compact upright riveted open-cup bombs."""
    bx, by, bz = back_anchor
    _, ty, tz = thorax_center
    pack_y = by + 0.132
    pack_z = min(bz - 0.052, head_back_z - 0.016)

    backplate = (
        rounded_box(0.184, 0.168, 0.052, bevel=0.016, segs=3, name="pack_backplate", material="gear_dark")
        .rotate_xyz(rx=-0.24)
        .translate(bx, pack_y + 0.008, pack_z + 0.022)
    )
    scene.add("dynamite_pack", backplate)

    # Snug shoulder straps & chest belt conformed tightly to core's slim petite thorax (`max r=0.082`).
    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        s_pts = [
            (side_sign * 0.056, pack_y + 0.022, pack_z + 0.030),
            (side_sign * 0.066, ty + 0.106, tz - 0.026),
            (side_sign * 0.064, ty + 0.094, tz + 0.048),
            (side_sign * 0.058, ty + 0.026, tz + 0.094),
            (side_sign * 0.054, ty - 0.026, tz + 0.086),
        ]
        s_strap = curved_tube(
            s_pts,
            (0.0095, 0.0145),
            samples_per_seg=4,
            radial_segs=10,
            cap_rings=2,
            name=f"harness_shoulder_{tag}",
            material="gear_dark",
        )
        scene.add("dynamite_pack", s_strap)

        chest_buckle = (
            rounded_box(0.024, 0.028, 0.016, bevel=0.0045, segs=2, name=f"harness_buckle_{tag}", material="gear_brass_trim")
            .rotate_xyz(rx=-0.16, ry=side_sign * 0.28)
            .translate(side_sign * 0.058, ty + 0.030, tz + 0.098)
        )
        scene.add("dynamite_pack", chest_buckle)

    chest_belt = curved_tube(
        [
            (-0.072, ty - 0.020, tz + 0.052),
            (-0.046, ty - 0.020, tz + 0.096),
            (0.000, ty - 0.020, tz + 0.104),
            (0.046, ty - 0.020, tz + 0.096),
            (0.072, ty - 0.020, tz + 0.052),
        ],
        (0.012, 0.0085),
        samples_per_seg=4,
        radial_segs=8,
        cap_rings=2,
        name="harness_chest_belt",
        material="gear_dark",
    )
    scene.add("dynamite_pack", chest_belt)

    rack_pts = [
        (-0.158, pack_y + 0.054, pack_z - 0.068),
        (-0.088, pack_y + 0.098, pack_z - 0.110),
        (0.000, pack_y + 0.114, pack_z - 0.124),
        (0.088, pack_y + 0.098, pack_z - 0.110),
        (0.158, pack_y + 0.054, pack_z - 0.068),
    ]
    rack_strap = curved_tube(
        rack_pts,
        (0.018, 0.013),
        samples_per_seg=5,
        radial_segs=10,
        cap_rings=2,
        name="bomb_rack_strap",
        material="gear_dark",
    )
    scene.add("dynamite_pack", rack_strap)

    rack_buckle = (
        rounded_box(0.048, 0.042, 0.022, bevel=0.007, segs=3, name="bomb_rack_buckle", material="gear_gold_metal")
        .rotate_xyz(rx=-0.26)
        .translate(0.0, pack_y + 0.114, pack_z - 0.132)
    )
    scene.add("dynamite_pack", rack_buckle)

    # Set back cleanly behind the -16 deg chin-up pitched cranium & rear goggle strap/buckle.
    pack_specs = (
        (+18.0, -18.0, -0.112, 0.062, -0.076, 0.080, -0.026),
        (+6.0, -23.0, -0.046, 0.140, -0.106, 0.084, -0.022),
        (-6.0, -23.0, +0.046, 0.140, -0.106, 0.084, +0.022),
        (-18.0, -18.0, +0.112, 0.062, -0.076, 0.080, +0.026),
    )
    for idx, (rz_deg, rx_deg, ox, oy, oz, cup_h, fuse_dx) in enumerate(pack_specs):
        holster = (
            cylinder(0.45 * cup_h, 0.42 * cup_h, 0.032, radial_segs=14, bevel=0.005, axis="y", name=f"pack_holster_{idx}", material="gear_dark")
            .rotate_xyz(rx=rx_deg, rz=rz_deg, degrees=True)
            .translate(bx + ox, pack_y + oy, pack_z + oz)
        )
        scene.add("dynamite_pack", holster)

        for part in _build_cup_bomb(f"pack_bomb_{idx}", cup_h, fuse_dx, lit=False, rivet_count=10):
            placed = (
                part.rotate_xyz(rx=rx_deg, rz=rz_deg, degrees=True)
                .translate(bx + ox, pack_y + oy, pack_z + oz)
            )
            scene.add("dynamite_pack", placed)


def _add_bomber_idle(scene: SceneBuilder) -> AnimationClip:
    """Attach Bomber's calm, composed 1.70s breathing + gentle alert glance Idle clip."""
    dur = 1.70
    n_steps = 16
    times = [round(dur * k / n_steps, 5) for k in range(n_steps + 1)]

    def _rest_t(node: str) -> Tuple[float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.translation if xf is not None else (0.0, 0.0, 0.0)

    def _rest_r(node: str) -> Tuple[float, float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)

    bx, by, bz = _rest_t("upper_body")
    body_t = []
    for k in range(n_steps + 1):
        ph = 2.0 * math.pi * (k / n_steps)
        dip = 0.5 * (1.0 - math.cos(2.0 * ph))
        sway = math.sin(ph)
        body_t.append((bx - 0.008 * sway, by - 0.018 * dip, bz + 0.003 * dip))
    body_t[-1] = body_t[0]

    r0_body = _rest_r("upper_body")
    body_r = [
        quat_mul(
            r0_body,
            quat_from_euler_xyz(
                rx=0.032 * 0.5 * (1.0 - math.cos(2.0 * math.pi * (k / n_steps) * 2.0)),
                ry=-0.025 * math.sin(2.0 * math.pi * (k / n_steps)),
                rz=0.028 * math.sin(2.0 * math.pi * (k / n_steps)),
            ),
        )
        for k in range(n_steps + 1)
    ]
    body_r[-1] = body_r[0]

    r0_head = _rest_r("head_group")
    head_r = [
        quat_mul(
            r0_head,
            quat_from_euler_xyz(
                rx=0.038 * math.sin(4.0 * math.pi * (k / n_steps)),
                ry=-0.085 * math.sin(2.0 * math.pi * (k / n_steps)),
                rz=0.034 * math.sin(2.0 * math.pi * (k / n_steps) - 0.2),
            ),
        )
        for k in range(n_steps + 1)
    ]
    head_r[-1] = head_r[0]

    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), body_t),
        AnimationChannel("upper_body", "rotation", list(times), body_r),
        AnimationChannel("head_group", "rotation", list(times), head_r),
    ]
    if "gaster_group" in scene.nodes:
        r0_g = _rest_r("gaster_group")
        g_r = [
            quat_mul(r0_g, quat_from_euler_xyz(rx=-0.042 * 0.5 * (1.0 - math.cos(4.0 * math.pi * (k / n_steps)))))
            for k in range(n_steps + 1)
        ]
        g_r[-1] = g_r[0]
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), g_r))
    for node_name, side, poff in (("antenna_left", 1.0, 0.0), ("antenna_right", -1.0, 0.8)):
        if node_name in scene.nodes and scene.get_node(node_name):
            r0_a = _rest_r(node_name)
            vals = [
                quat_mul(
                    r0_a,
                    quat_from_euler_xyz(
                        rx=0.052 * math.sin(4.0 * math.pi * (k / n_steps) + poff),
                        rz=side * 0.032 * math.sin(2.0 * math.pi * (k / n_steps)),
                    ),
                )
                for k in range(n_steps + 1)
            ]
            vals[-1] = vals[0]
            channels.append(AnimationChannel(node_name, "rotation", list(times), vals))
    if "mandibles" in scene.nodes and scene.get_node("mandibles"):
        r0_m = _rest_r("mandibles")
        m_r = [
            quat_mul(r0_m, quat_from_euler_xyz(rx=0.024 * 0.5 * (1.0 - math.cos(4.0 * math.pi * (k / n_steps)))))
            for k in range(n_steps + 1)
        ]
        m_r[-1] = m_r[0]
        channels.append(AnimationChannel("mandibles", "rotation", list(times), m_r))
    clip = scene.add_animation("Idle", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Idle")
    return clip


def _scale_bomber_head_and_tall_antennae(
    scene: SceneBuilder,
    anchors: dict,
    *,
    head_scale: float = 0.90,
) -> Tuple[
    Tuple[float, float, float],
    Tuple[float, float, float],
    Tuple[float, float, float],
    Tuple[float, float, float],
    float,
]:
    """Scale Bomber's head/eyes/eyelids/mandibles ~10% slimmer and sculpt tall, bold upright antennae.

    Matches `abst301` compact slim head silhouette (`~21px` wide) topped by tall bold dark antennae
    towering prominently above the goggles and pink back-pack bombs.
    """
    hc = anchors["head_center"]
    hrx, hry, hrz = anchors["head_radii"]
    head_xf = scene.get_node_transform("head")
    neck_pivot = (
        (-head_xf.translation[0], -head_xf.translation[1], -head_xf.translation[2])
        if head_xf is not None
        else anchors["neck_center"]
    )
    # Anchor scaling slightly below head center so the slimmer skull stays seated flush on neck_joint.
    piv = (hc[0], hc[1] - 0.052, hc[2])
    p_hg = (piv[0] - neck_pivot[0], piv[1] - neck_pivot[1], piv[2] - neck_pivot[2])
    s = float(head_scale)

    # 1. Uniformly scale `head` & `eyes` parts around `piv` in neutral world space (t stays -neck_pivot).
    for node_name in ("head", "eyes"):
        if node_name in scene.nodes:
            scene.nodes[node_name] = [
                part.translate(-piv[0], -piv[1], -piv[2]).scale(s, s, s).translate(*piv)
                for part in scene.nodes[node_name]
            ]

    # 2. Scale locally-pivoted `mandibles`, `eyelid_left`, `eyelid_right` around their own (0,0,0) hinges
    # and shift their `head_group`-local pivot positions toward `p_hg` so blink & jaw pivots stay exact.
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
    new_eye_l = _scale_pt(anchors["eye_left"])
    new_eye_r = _scale_pt(anchors["eye_right"])
    new_head_back_z = new_hc[2] - new_hr[2]

    # 3. Replace antennae with tall, thick, bold upright Bomber antennae (~1.52x taller, ~1.38x thicker)
    # anchored onto the scaled skull crown so they tower cleanly above goggles & bombs like `abst301`.
    ant_rads = [
        (0.024, 0.024),
        (0.020, 0.021),
        (0.017, 0.018),
        (0.019, 0.020),
        (0.024, 0.026),
        (0.028, 0.030),
        (0.015, 0.017),
    ]
    for side, tag, orig_key in ((+1.0, "left", "antenna_base_left"), (-1.0, "right", "antenna_base_right")):
        node_name = f"antenna_{tag}"
        base_p = _scale_pt(anchors[orig_key])
        # Subtle playful jaunty asymmetry while keeping BOTH antennae boldly tall & upright.
        h_boost = 0.022 if side > 0 else -0.014
        z_lean = 0.016 if side > 0 else -0.014
        bx, by, bz = base_p
        pts = [
            (bx - side * 0.006, by - 0.018, bz - 0.004),
            base_p,
            (bx + side * 0.018, by + 0.115 + 0.4 * h_boost, bz + 0.008 + 0.3 * z_lean),
            (bx + side * 0.034, by + 0.215 + 0.8 * h_boost, bz + 0.024 + 0.6 * z_lean),
            (bx + side * 0.044, by + 0.258 + h_boost, bz + 0.068 + 0.9 * z_lean),
            (bx + side * 0.048, by + 0.244 + h_boost, bz + 0.104 + z_lean),
            (bx + side * 0.050, by + 0.212 + 0.9 * h_boost, bz + 0.122 + 1.1 * z_lean),
        ]
        stalk = swept_tube(
            pts,
            ant_rads,
            radial_segs=12,
            smooth_subdiv=3,
            cap_rings=3,
            name=f"antenna_stalk_{tag}",
            material="chitin_dark",
        )
        sock = (
            ellipsoid(0.026, 0.013, 0.026, u_segs=12, v_segs=8, name=f"antenna_socket_{tag}", material="chitin_dark")
            .rotate_xyz(rx=0.18, rz=-side * 0.24)
            .translate(*base_p)
        )
        scene.nodes[node_name] = [
            stalk.translate(-bx, -by, -bz),
            sock.translate(-bx, -by, -bz),
        ]
        scene.set_node_transform(
            node_name,
            translation=(bx - neck_pivot[0], by - neck_pivot[1], bz - neck_pivot[2]),
        )

    return new_hc, new_hr, new_eye_l, new_eye_r, new_head_back_z


def build_bomber(blink: float = 0.0) -> SceneBuilder:
    """Build the sculpted 3D Bomber Ant SceneBuilder with slim head, tall antennae, Blink, and 1.70s Idle."""
    scene, anchors = build_base_ant(
        scene_name="ant_bomber",
        include_antennae=True,
        include_eyes=True,
        include_brows=False,
        brow_style="eager",
        head_tilt_deg=-5.5,
        head_yaw_deg=3.5,
        head_pitch_deg=-16.0,
        eye_look=(-0.06, 0.04),
        antenna_asymmetry=0.45,
        stance_asymmetry=0.40,
        leg_style="bomber",
        include_idle=False,
        crouch=0.12,
        left_arm_pose="idle",
        right_arm_pose="idle",
        blink=blink,
    )
    head_c, head_r, eye_l, eye_r, head_back_z = _scale_bomber_head_and_tall_antennae(scene, anchors, head_scale=0.90)
    _build_goggles(
        scene,
        head_c,
        head_r,
        eye_l,
        eye_r,
        float(anchors.get("eyelid_closed_angle", 1.80)),
    )
    _build_dynamite_pack(scene, anchors["back_anchor"], anchors["thorax_center"], head_back_z)

    scene.attach_world_node("goggles", "head_group", preserve_world_pose=False)
    head_xf = scene.get_node_transform("head")
    if head_xf is not None:
        scene.set_node_transform("goggles", translation=head_xf.translation)
    scene.attach_world_node("dynamite_pack", "upper_body", preserve_world_pose=False)
    _add_bomber_idle(scene)
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical entry point discovered by build_all.py."""
    return build_bomber(blink=blink)
