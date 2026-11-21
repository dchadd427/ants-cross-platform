"""Sculpted 3D Bomber Ant (compact riveted aviator goggles, empty idle hands, 4-bomb pack, 0.80s Idle).

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet grounded solidly at Y = 0.0)
  +Z = front (South / canonical camera 301)
  +X = ant's left (viewer's right from front), -X = ant's right
"""
from __future__ import annotations

import math
from typing import List, Optional, Tuple

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
        lathe,
        quat_from_euler_xyz,
        quat_mul,
        rounded_box,
        sphere,
        superellipsoid,
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
        lathe,
        quat_from_euler_xyz,
        quat_mul,
        rounded_box,
        sphere,
        superellipsoid,
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
    """Build one long, sleek, all-yellow metallic binocular 'can' goggle barrel (zero reddish copper).

    Keeps a compact circle diameter sitting cleanly inside the head width while projecting boldly
    forward like shiny metallic yellow tin cans (`abst201`/`abst901`) with rolled bead seams.
    """
    parts: List[MeshPart] = []
    tag = "left" if side_sign > 0 else "right"

    z_back = -0.022
    z_can_start = 0.006
    z_rim_start = lens_z - 0.016
    z_rim_front = lens_z + 0.016

    r_in = bore_r
    r_can_out = bore_r + 0.0095
    r_rim_out = bore_r + 0.0105

    # 1. Snug dark rubber face cushion tucking neatly onto the orbital rim.
    cushion_prof = [
        (r_in + 0.001, z_back),
        (r_can_out - 0.0025, z_back - 0.002),
        (r_can_out + 0.0008, z_back + 0.014),
        (r_can_out, z_can_start + 0.004),
        (r_in + 0.001, z_can_start + 0.004),
    ]
    parts.append(
        _hollow_ring_lathe(
            cushion_prof,
            radial_segs=30,
            name=f"goggle_cushion_{tag}",
            material="gear_dark",
        )
    )

    # 2. Long gleaming all-yellow metallic cylindrical 'can' barrel wall (`gear_gold_metal`, zero red/copper!).
    z_mid = z_can_start + 0.50 * (z_rim_start - z_can_start)
    can_prof = [
        (r_in, z_can_start),
        (r_can_out - 0.0006, z_can_start),
        (r_can_out + 0.0006, z_can_start + 0.018),
        (r_can_out + 0.0008, z_mid),
        (r_can_out + 0.0006, z_rim_start + 0.004),
        (r_in, z_rim_start + 0.004),
    ]
    parts.append(
        _hollow_ring_lathe(
            can_prof,
            radial_segs=32,
            name=f"goggle_copper_ring_{tag}",
            material="gear_gold_metal",
        )
    )

    # Three crisp rolled tin-can metallic bead rings along the elongated yellow metallic barrel.
    for b_idx, frac in enumerate((0.24, 0.52, 0.78)):
        bz = z_can_start + frac * (z_rim_start - z_can_start)
        bead_name = f"goggle_mid_band_{tag}" if b_idx == 1 else f"goggle_can_bead_{tag}_{b_idx}"
        bead = (
            torus_arc(r_can_out + 0.0008, 0.0024, u_segs=28, v_segs=8, plane="xy", name=bead_name, material="gear_gold_metal")
            .translate(0.0, 0.0, bz)
        )
        parts.append(bead)

    # 3. Crisp rolled bright yellow metallic front lens hood rim framing the recessed clear glass lens.
    rim_prof = [
        (r_in - 0.001, z_rim_start),
        (r_rim_out - 0.0012, z_rim_start),
        (r_rim_out + 0.0014, z_rim_start + 0.007),
        (r_rim_out + 0.0010, z_rim_front - 0.005),
        (r_rim_out - 0.0028, z_rim_front),
        (r_in + 0.0024, z_rim_front),
        (r_in - 0.001, z_rim_front - 0.004),
        (r_in - 0.001, z_rim_start + 0.004),
    ]
    parts.append(
        _hollow_ring_lathe(
            rim_prof,
            radial_segs=32,
            name=f"goggle_front_rim_{tag}",
            material="gear_gold_metal",
        )
    )

    # Crisp thin dark inner gasket ring right behind the glass edge.
    inner_gasket = (
        torus_arc(r_in, 0.0022, u_segs=26, v_segs=8, plane="xy", name=f"goggle_inner_gasket_{tag}", material="gear_dark")
        .translate(0.0, 0.0, lens_z + 0.003)
    )
    parts.append(inner_gasket)

    # 4. Thin see-through tinted optical glass lens seated inside the projecting front can rim.
    lens = (
        ellipsoid(r_in + 0.001, r_in + 0.001, 0.0055, u_segs=28, v_segs=14, name=f"goggle_lens_{tag}", material="glass_tinted")
        .translate(0.0, 0.0, lens_z)
    )
    parts.append(lens)

    # 5. Seven crisp metallic gold rivets studded around the forward half of the yellow metallic can.
    stud_r = r_can_out + 0.0018
    stud_z = z_can_start + 0.65 * (z_rim_start - z_can_start)
    for r_idx, ang_deg in enumerate((-26.0, 4.0, 34.0, 64.0, 94.0, 124.0, 154.0)):
        ang = math.radians(ang_deg)
        sx = side_sign * stud_r * math.sin(ang)
        sy = stud_r * math.cos(ang)
        rivet = (
            sphere(0.0044, u_segs=10, v_segs=8, name=f"goggle_stud_{tag}_{r_idx}", material="gear_gold_metal")
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
    """Build compact-diameter, long projecting all-yellow metallic 'can' goggles & snug rear strap."""
    hx, _, hz = head_center
    hrx, _, hrz = head_radii
    ey = 0.5 * (eye_left[1] + eye_right[1])
    ez = 0.5 * (eye_left[2] + eye_right[2])

    pitch = -0.10
    yaw_mag = 0.11
    mount_dx = 0.0
    mount_dz = 0.001

    max_lid_r, max_lid_z = _measure_eyelid_sweep_in_goggle_frame(
        scene,
        closed_angle=closed_angle,
        goggle_pitch=pitch,
        goggle_yaw=yaw_mag,
        mount_offset=(mount_dx, 0.0, mount_dz),
    )
    # Compact sleek circular diameter scaled to Bomber's narrow sleek head + long metallic cans!
    bore_r = max(0.045, max_lid_r + 0.0042)
    lens_z = max(0.124, max_lid_z + 0.056)

    for side_sign, eye_pos in ((+1.0, eye_left), (-1.0, eye_right)):
        mount = (eye_pos[0] + side_sign * mount_dx, eye_pos[1], eye_pos[2] + mount_dz)
        for part in _build_aviator_cup(side_sign, bore_r=bore_r, lens_z=lens_z):
            placed = part.rotate_xyz(rx=pitch, ry=side_sign * yaw_mag).translate(*mount)
            if part.name.startswith(("goggle_cushion_", "goggle_copper_ring_", "goggle_mid_band_", "goggle_can_bead_")):
                def _clamp_nose_midline(pos: np.ndarray, s: float = side_sign) -> np.ndarray:
                    p = pos.copy()
                    if s > 0.0:
                        p[:, 0] = np.maximum(p[:, 0], 0.0)
                    else:
                        p[:, 0] = np.minimum(p[:, 0], 0.0)
                    return p
                placed = placed.deform(_clamp_nose_midline)
            scene.add("goggles", placed)

    # Sleek all-yellow metallic nose bridge & gold center rivet joining the two closely-set cans cleanly.
    bridge_z = ez + lens_z * 0.62
    bridge = (
        rounded_box(0.022, 0.015, 0.020, bevel=0.004, segs=3, name="goggle_bridge", material="gear_gold_metal")
        .translate(hx, ey + 0.008, bridge_z)
    )
    scene.add("goggles", bridge)

    bridge_stud = (
        cylinder(0.0050, 0.0050, 0.007, radial_segs=14, bevel=0.0016, axis="z", name="goggle_bridge_stud", material="gear_gold_metal")
        .translate(hx, ey + 0.008, bridge_z + 0.011)
    )
    scene.add("goggles", bridge_stud)

    # Compact metallic gold temple clasps & yellow strap tabs connecting the cans smoothly into the rear strap.
    rx_strap = hrx + 0.005
    rz_strap = hrz + 0.006
    strap_base_y = ey + 0.007
    for side_sign, tag, eye_pos in ((+1.0, "left", eye_left), (-1.0, "right", eye_right)):
        cup_side_x = eye_pos[0] + side_sign * (bore_r + 0.005)
        cup_side_z = eye_pos[2] + 0.020
        temple_x = hx + side_sign * (rx_strap * _spow(math.sin(math.radians(52.0)), 0.76))
        temple_z = hz + rz_strap * _spow(math.cos(math.radians(52.0)), 0.76)
        tab = curved_tube(
            [
                (cup_side_x, ey + 0.006, cup_side_z),
                (0.5 * (cup_side_x + temple_x) + side_sign * 0.010, ey + 0.007, 0.5 * (cup_side_z + temple_z) + 0.008),
                (temple_x, strap_base_y, temple_z),
            ],
            (0.019, 0.0095),
            samples_per_seg=4,
            radial_segs=10,
            cap_rings=2,
            name=f"goggle_temple_tab_{tag}",
            material="gear_gold",
        )
        scene.add("goggles", tab)

        clasp = (
            rounded_box(0.016, 0.030, 0.030, bevel=0.005, segs=3, name=f"goggle_clasp_{tag}", material="gear_gold_metal")
            .rotate_xyz(ry=side_sign * 0.36)
            .translate(temple_x - side_sign * 0.005, strap_base_y - 0.001, temple_z + 0.004)
        )
        scene.add("goggles", clasp)

    # Bold yellow head strap hugging cleanly around Bomber's smaller head so the fuller crown rises naturally above it.
    strap_pts: List[Tuple[float, float, float]] = []
    n_arc = 28
    start_ang = math.radians(52.0)
    end_ang = math.radians(308.0)
    for i in range(n_arc + 1):
        ang = start_ang + (end_ang - start_ang) * (i / n_arc)
        sx = hx + rx_strap * _spow(math.sin(ang), 0.76)
        sz = hz + rz_strap * _spow(math.cos(ang), 0.76)
        rear_lift = 0.006 * (0.5 * (1.0 - math.cos(ang)))
        strap_pts.append((sx, strap_base_y + rear_lift, sz))

    strap = swept_tube(
        strap_pts,
        (0.022, 0.0105),
        radial_segs=12,
        cap_start=True,
        cap_end=True,
        closed=False,
        cap_rings=3,
        name="goggle_strap",
        material="gear_gold",
    )
    scene.add("goggles", strap)

    rear_buckle = (
        rounded_box(0.048, 0.044, 0.018, bevel=0.005, segs=3, name="goggle_rear_buckle", material="gear_gold_metal")
        .translate(hx, strap_base_y + 0.006, hz - rz_strap - 0.002)
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
    fspan = max(0.68 * h, abs(fuse_curve_x) * 2.1)
    # 2x longer braided white cord fuses with playful outward & rearward S-curve arcs (clear of antennae!).
    fuse_pts = [
        (0.0, 1.63 * h, 0.0),
        (0.10 * fdir * fspan, 2.08 * h, -0.08 * h),
        (0.42 * fdir * fspan, 2.52 * h, -0.18 * h),
        (0.78 * fdir * fspan, 2.84 * h, -0.22 * h),
        (1.06 * fdir * fspan, 2.94 * h, -0.18 * h),
    ]
    fuse = curved_tube(
        fuse_pts,
        [0.056 * h, 0.052 * h, 0.048 * h, 0.044 * h, 0.040 * h],
        samples_per_seg=4,
        radial_segs=8,
        cap_rings=2,
        name=f"{prefix}_fuse",
        material="fuse_white",
    )
    parts.append(fuse)

    tip_r = 0.095 * h if lit else 0.062 * h
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
    """Build Bomber's sculpted tan-leather tactical backpack physically holding 4 riveted cup-bombs."""
    bx, by, bz = back_anchor
    _, ty, tz = thorax_center
    pack_y = by + 0.112
    pack_z = min(bz - 0.068, head_back_z - 0.028)

    # 1. Plump sculpted tan/brown canvas-leather main rucksack body flush against the dorsal carapace
    # and extending solidly rearward to physically house & cradle all 4 cup-bombs (zero floating!).
    pack_body = (
        rounded_box(0.214, 0.194, 0.144, bevel=0.030, segs=4, name="pack_rucksack_body", material="prop_brown")
        .rotate_xyz(rx=-0.30)
        .translate(bx, pack_y + 0.012, pack_z - 0.052)
    )
    scene.add("dynamite_pack", pack_body)

    # Reinforced dark leather lower pack base pad (keeps tidy clearance above petiole/gaster).
    pack_base = (
        rounded_box(0.196, 0.052, 0.132, bevel=0.016, segs=3, name="pack_base_boot", material="gear_dark")
        .rotate_xyz(rx=-0.30)
        .translate(bx, pack_y - 0.072, pack_z - 0.026)
    )
    scene.add("dynamite_pack", pack_base)

    # Sculpted rear flap / ordnance pouch on the back face of the backpack with a gleaming gold buckle.
    rear_pouch = (
        rounded_box(0.156, 0.104, 0.044, bevel=0.016, segs=3, name="pack_rear_pouch", material="prop_brown")
        .rotate_xyz(rx=-0.30)
        .translate(bx, pack_y - 0.008, pack_z - 0.118)
    )
    scene.add("dynamite_pack", rear_pouch)

    # Side canvas holster pouches on left & right flanks of the rucksack physically cradling outer bombs 0 & 3.
    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        side_pouch = (
            rounded_box(0.068, 0.118, 0.088, bevel=0.018, segs=3, name=f"pack_side_pouch_{tag}", material="prop_brown")
            .rotate_xyz(rx=-0.30, rz=-side_sign * 0.24)
            .translate(bx + side_sign * 0.106, pack_y + 0.024, pack_z - 0.064)
        )
        scene.add("dynamite_pack", side_pouch)

    # Snug shoulder straps & chest belt routed cleanly from the backpack over Bomber's petite thorax.
    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        s_pts = [
            (side_sign * 0.058, pack_y + 0.040, pack_z + 0.014),
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
            rounded_box(0.024, 0.028, 0.016, bevel=0.0045, segs=2, name=f"harness_buckle_{tag}", material="gear_gold_metal")
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

    # Dark leather cinch straps wrapping tightly around the backpack & bombs with gleaming gold buckles!
    for strap_idx, y_off, z_off in ((0, 0.044, -0.128), (1, -0.020, -0.132)):
        c_pts = [
            (-0.144, pack_y + y_off - 0.012, pack_z - 0.044),
            (-0.118, pack_y + y_off + 0.008, pack_z + z_off + 0.024),
            (0.000, pack_y + y_off + 0.016, pack_z + z_off),
            (+0.118, pack_y + y_off + 0.008, pack_z + z_off + 0.024),
            (+0.144, pack_y + y_off - 0.012, pack_z - 0.044),
        ]
        c_strap = curved_tube(
            c_pts,
            (0.015, 0.011),
            samples_per_seg=4,
            radial_segs=10,
            cap_rings=2,
            name=f"pack_cinch_strap_{strap_idx}",
            material="gear_dark",
        )
        scene.add("dynamite_pack", c_strap)

    rack_buckle = (
        rounded_box(0.044, 0.038, 0.020, bevel=0.006, segs=3, name="bomb_rack_buckle", material="gear_gold_metal")
        .rotate_xyz(rx=-0.30)
        .translate(0.0, pack_y + 0.060, pack_z - 0.136)
    )
    scene.add("dynamite_pack", rack_buckle)

    # Seat all 4 hot-magenta cup-bombs SOLIDLY INTO the top & side pockets of the tan backpack (zero floating!)
    # with 2x longer braided fuses arching outward/upward with bright golden spark tips!
    pack_specs = (
        (+20.0, -18.0, -0.110, 0.046, -0.072, 0.080, -0.034),
        (+6.0, -21.0, -0.046, 0.088, -0.092, 0.084, -0.028),
        (-6.0, -21.0, +0.046, 0.088, -0.092, 0.084, +0.028),
        (-20.0, -18.0, +0.110, 0.046, -0.072, 0.080, +0.034),
    )
    for idx, (rz_deg, rx_deg, ox, oy, oz, cup_h, fuse_dx) in enumerate(pack_specs):
        holster = (
            cylinder(0.50 * cup_h, 0.46 * cup_h, 0.048, radial_segs=16, bevel=0.006, axis="y", name=f"pack_holster_{idx}", material="gear_dark")
            .rotate_xyz(rx=rx_deg, rz=rz_deg, degrees=True)
            .translate(bx + ox, pack_y + oy, pack_z + oz)
        )
        scene.add("dynamite_pack", holster)

        for part in _build_cup_bomb(f"pack_bomb_{idx}", cup_h, fuse_dx, lit=True, rivet_count=10):
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
                rx=0.088 * math.sin(4.0 * math.pi * (k / n_steps)),
                ry=-0.205 * math.sin(2.0 * math.pi * (k / n_steps)),
                rz=0.082 * math.sin(2.0 * math.pi * (k / n_steps) - 0.18),
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
    # Visibly floppy secondary-lag antennae bouncing ~0.65 rad behind Bomber's lively head nod & glance!
    for node_name, side, lag in (("antenna_left", 1.0, 0.58), ("antenna_right", -1.0, 0.78)):
        if node_name in scene.nodes and scene.get_node(node_name):
            r0_a = _rest_r(node_name)
            vals = [
                quat_mul(
                    r0_a,
                    quat_from_euler_xyz(
                        rx=-0.148 * math.sin(4.0 * math.pi * (k / n_steps) - lag)
                        + 0.042 * math.sin(2.0 * math.pi * (k / n_steps) - lag),
                        ry=side * 0.055 * math.sin(2.0 * math.pi * (k / n_steps) - lag),
                        rz=-0.105 * math.sin(2.0 * math.pi * (k / n_steps) - lag)
                        + side * 0.040 * math.sin(4.0 * math.pi * (k / n_steps) - lag),
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
    sync_arm_idle_to_clip(scene, "Idle")
    return clip


def _quat_track(r0: Tuple[float, float, float, float], deltas: List[Tuple[float, float, float]]) -> List[Tuple[float, float, float, float]]:
    """Build a hemisphere-continuous closed-loop quaternion list from `(rx, ry, rz)` euler deltas."""
    out: List[Tuple[float, float, float, float]] = []
    for rx, ry, rz in deltas:
        q = quat_mul(r0, quat_from_euler_xyz(rx=rx, ry=ry, rz=rz))
        if out and sum(a * b for a, b in zip(out[-1], q)) < 0.0:
            q = (-q[0], -q[1], -q[2], -q[3])
        out.append(q)
    if len(out) >= 2:
        q_end = out[0]
        if sum(a * b for a, b in zip(out[-2], q_end)) < 0.0:
            q_end = (-q_end[0], -q_end[1], -q_end[2], -q_end[3])
        out[-1] = q_end
    return out


def _add_bomber_attack(scene: SceneBuilder) -> AnimationClip:
    """Attach Bomber's authentic 1998 0.36s goggle-first headbutt slam (`abat`) Attack clip."""
    # Exact 1998 abat 6-frame cadence [60, 60, 60, 60, 60, 60] ms + exact loop closure at 0.36 s.
    times = [0.00, 0.06, 0.12, 0.18, 0.24, 0.30, 0.36]

    def _rest_t(node: str) -> Tuple[float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.translation if xf is not None else (0.0, 0.0, 0.0)

    def _rest_r(node: str) -> Tuple[float, float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)

    channels: List[AnimationChannel] = []
    bx, by, bz = _rest_t("upper_body")
    body_t_deltas = [
        (0.0, 0.000, 0.000),   # f0: ready
        (0.0, +0.018, -0.014), # f1: windup rise
        (0.0, +0.030, -0.026), # f2: peak rear windup
        (0.0, -0.024, +0.062), # f3: fierce forward headbutt slam
        (0.0, -0.032, +0.074), # f4: impact crunch
        (0.0, -0.010, +0.026), # f5: elastic recoil
        (0.0, 0.000, 0.000),   # loop wrap
    ]
    channels.append(
        AnimationChannel(
            "upper_body",
            "translation",
            list(times),
            [(bx + dx, by + dy, bz + dz) for dx, dy, dz in body_t_deltas],
        )
    )
    channels.append(
        AnimationChannel(
            "upper_body",
            "rotation",
            list(times),
            _quat_track(
                _rest_r("upper_body"),
                [
                    (0.00, 0.0, 0.0),
                    (-0.14, 0.0, 0.0),
                    (-0.22, 0.0, 0.0),
                    (+0.62, 0.0, 0.0),
                    (+0.72, 0.0, 0.0),
                    (+0.30, 0.0, 0.0),
                    (0.00, 0.0, 0.0),
                ],
            ),
        )
    )
    channels.append(
        AnimationChannel(
            "head_group",
            "rotation",
            list(times),
            _quat_track(
                _rest_r("head_group"),
                [
                    (0.00, 0.0, 0.0),
                    (-0.10, 0.0, 0.0),
                    (-0.18, 0.0, 0.0),
                    (+0.38, 0.0, 0.0),
                    (+0.44, 0.0, 0.0),
                    (+0.14, 0.0, 0.0),
                    (0.00, 0.0, 0.0),
                ],
            ),
        )
    )
    # Arms spread wide & high on windup (abat301 f1-f2), then sweep cleanly outward/back above legs on slam.
    for side, tag in ((+1.0, "left"), (-1.0, "right")):
        arm_node = f"arm_{tag}"
        fore_node = f"forearm_{tag}"
        if arm_node in scene.nodes:
            channels.append(
                AnimationChannel(
                    arm_node,
                    "rotation",
                    list(times),
                    _quat_track(
                        _rest_r(arm_node),
                        [
                            (0.00, 0.00, 0.00),
                            (-0.18, +side * 0.22, +side * 0.36),
                            (-0.28, +side * 0.34, +side * 0.56),
                            (-0.34, -side * 0.52, +side * 0.62),
                            (-0.38, -side * 0.62, +side * 0.68),
                            (-0.14, -side * 0.20, +side * 0.24),
                            (0.00, 0.00, 0.00),
                        ],
                    ),
                )
            )
        if fore_node in scene.nodes:
            channels.append(
                AnimationChannel(
                    fore_node,
                    "rotation",
                    list(times),
                    _quat_track(
                        _rest_r(fore_node),
                        [
                            (0.00, 0.00, 0.00),
                            (-0.12, -side * 0.14, +side * 0.14),
                            (-0.20, -side * 0.24, +side * 0.22),
                            (-0.18, +side * 0.24, +side * 0.16),
                            (-0.22, +side * 0.30, +side * 0.20),
                            (-0.08, +side * 0.10, +side * 0.08),
                            (0.00, 0.00, 0.00),
                        ],
                    ),
                )
            )
    if "gaster_group" in scene.nodes:
        channels.append(
            AnimationChannel(
                "gaster_group",
                "rotation",
                list(times),
                _quat_track(
                    _rest_r("gaster_group"),
                    [
                        (0.00, 0.0, 0.0),
                        (+0.08, 0.0, 0.0),
                        (+0.14, 0.0, 0.0),
                        (-0.30, 0.0, 0.0),
                        (-0.38, 0.0, 0.0),
                        (-0.12, 0.0, 0.0),
                        (0.00, 0.0, 0.0),
                    ],
                ),
            )
        )
    for node_name, side in (("antenna_left", +1.0), ("antenna_right", -1.0)):
        if node_name in scene.nodes and scene.get_node(node_name):
            channels.append(
                AnimationChannel(
                    node_name,
                    "rotation",
                    list(times),
                    _quat_track(
                        _rest_r(node_name),
                        [
                            (0.00, 0.0, 0.0),
                            (+0.12, 0.0, +side * 0.06),
                            (+0.22, 0.0, +side * 0.10),
                            (-0.38, 0.0, -side * 0.12),
                            (-0.46, 0.0, -side * 0.14),
                            (+0.24, 0.0, +side * 0.08),
                            (0.00, 0.0, 0.0),
                        ],
                    ),
                )
            )
    if "mandibles" in scene.nodes and scene.get_node("mandibles"):
        channels.append(
            AnimationChannel(
                "mandibles",
                "rotation",
                list(times),
                _quat_track(
                    _rest_r("mandibles"),
                    [
                        (0.00, 0.0, 0.0),
                        (-0.08, 0.0, 0.0),
                        (-0.14, 0.0, 0.0),
                        (+0.14, 0.0, 0.0),
                        (+0.18, 0.0, 0.0),
                        (+0.06, 0.0, 0.0),
                        (0.00, 0.0, 0.0),
                    ],
                ),
            )
        )
    clip = scene.add_animation("Attack", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Attack", knee_bow=1.1, splay=0.25, swivel=0.0)
    return clip


def _cranium_top_y_at(cranium: MeshPart, x: float, z: float, fallback_y: float) -> float:
    """Return the exact upper-surface Y of `cranium` at `(x, z)` via vertical ray-triangle intersection."""
    pos = cranium.positions.astype(np.float64)
    tris = pos[cranium.indices.reshape(-1, 3)]
    v0, v1, v2 = tris[:, 0], tris[:, 1], tris[:, 2]
    ax, az = v0[:, 0], v0[:, 2]
    bx, bz = v1[:, 0], v1[:, 2]
    cx, cz = v2[:, 0], v2[:, 2]
    det = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
    valid = np.abs(det) > 1e-12
    safe_det = np.where(valid, det, 1.0)
    w0 = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / safe_det
    w1 = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / safe_det
    w2 = 1.0 - w0 - w1
    hits = valid & (w0 >= -1e-6) & (w1 >= -1e-6) & (w2 >= -1e-6)
    if not np.any(hits):
        return fallback_y
    hit_y = w0[hits] * v0[hits, 1] + w1[hits] * v1[hits, 1] + w2[hits] * v2[hits, 1]
    return float(hit_y.max())


def _scale_bomber_head_and_tall_antennae(
    scene: SceneBuilder,
    anchors: dict,
    *,
    head_scale_x: float = 0.64,
    head_scale_y: float = 0.77,
    head_scale_z: float = 0.79,
    eye_inward_dx: float = 0.002,
) -> Tuple[
    Tuple[float, float, float],
    Tuple[float, float, float],
    Tuple[float, float, float],
    Tuple[float, float, float],
    float,
]:
    """Sculpt Bomber's sleek narrow vertical-oval cranium (`sx=0.64, sy=0.77, sz=0.79`) with flush antennae.

    Matches `abst301`/`abst901`/`abst701`: sleek narrow head fitting snugly above the two closely-set
    yellow goggles (NOT wide horizontally), softly-domed top crown, spherical eyes & tucked eyelids
    (zero blink=0 forehead/temple protrusion), and compact antennae seated flush on the crown.
    """
    hc = anchors["head_center"]
    hrx, hry, hrz = anchors["head_radii"]
    head_xf = scene.get_node_transform("head")
    neck_pivot = (
        (-head_xf.translation[0], -head_xf.translation[1], -head_xf.translation[2])
        if head_xf is not None
        else anchors["neck_center"]
    )
    # Anchor scaling near the neck base so Bomber's sleek narrow head stays seated flush on neck_joint.
    piv = (hc[0], hc[1] - 0.125, hc[2])
    p_hg = (piv[0] - neck_pivot[0], piv[1] - neck_pivot[1], piv[2] - neck_pivot[2])
    sx = float(head_scale_x)
    sy = float(head_scale_y)
    sz = float(head_scale_z)
    s_uni = float(head_scale_x)

    def _scale_pt_aniso(pt: Tuple[float, float, float]) -> Tuple[float, float, float]:
        return (
            piv[0] + sx * (pt[0] - piv[0]),
            piv[1] + sy * (pt[1] - piv[1]),
            piv[2] + sz * (pt[2] - piv[2]),
        )

    orig_eye_l = anchors["eye_left"]
    orig_eye_r = anchors["eye_right"]
    raw_el = _scale_pt_aniso(orig_eye_l)
    raw_er = _scale_pt_aniso(orig_eye_r)
    new_eye_l = (raw_el[0] - eye_inward_dx, raw_el[1], raw_el[2])
    new_eye_r = (raw_er[0] + eye_inward_dx, raw_er[1], raw_er[2])

    # 1. Scale `head` around `piv`, then apply a gentle superelliptic crown rounding to `cranium`
    # so Bomber's smaller head keeps a natural, fuller softly-rounded dome emerging cleanly above the
    # yellow goggle strap (matching ORIG abst301 & West view W / flipped abst901).
    ey_mid = 0.5 * (new_eye_l[1] + new_eye_r[1])
    crown_start_y = ey_mid + 0.018
    crown_cap_h = 0.094
    crown_p = 1.95

    def _flatten_crown(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        dy = p[:, 1] - crown_start_y
        above = dy > 0.0
        if np.any(above):
            s = dy[above] / crown_cap_h
            p[above, 1] = crown_start_y + crown_cap_h * s / ((1.0 + s ** crown_p) ** (1.0 / crown_p))
        return p

    cranium_part: Optional[MeshPart] = None
    if "head" in scene.nodes:
        rebuilt_head: List[MeshPart] = []
        for part in scene.nodes["head"]:
            scaled = part.translate(-piv[0], -piv[1], -piv[2]).scale(sx, sy, sz).translate(*piv)
            if part.name == "cranium":
                scaled = scaled.deform(_flatten_crown)
                cranium_part = scaled
            rebuilt_head.append(scaled)
        scene.nodes["head"] = rebuilt_head

    # 2. Keep `eyes`, `eyelid_left`, and `eyelid_right` UNIFORMLY scaled (100% spherical blink!)
    # while moving each eye cleanly onto its flattened orbital socket position.
    if "eyes" in scene.nodes:
        rebuilt_eyes: List[MeshPart] = []
        for part in scene.nodes["eyes"]:
            is_left = part.name.endswith("_left") or float(part.positions[:, 0].mean()) >= 0.0
            src_c = orig_eye_l if is_left else orig_eye_r
            dst_c = new_eye_l if is_left else new_eye_r
            rebuilt_eyes.append(
                part.translate(-src_c[0], -src_c[1], -src_c[2]).scale(s_uni, s_uni, s_uni).translate(*dst_c)
            )
        scene.nodes["eyes"] = rebuilt_eyes

    for node_name, dst_c in (("eyelid_left", new_eye_l), ("eyelid_right", new_eye_r)):
        if node_name in scene.nodes:
            scene.nodes[node_name] = [part.scale(s_uni, s_uni, s_uni) for part in scene.nodes[node_name]]
            scene.set_node_transform(
                node_name,
                translation=(dst_c[0] - neck_pivot[0], dst_c[1] - neck_pivot[1], dst_c[2] - neck_pivot[2]),
            )

    # Uniformly scale `mandibles` around its jaw hinge and seat its pivot cleanly on the flatter chin.
    if "mandibles" in scene.nodes:
        scene.nodes["mandibles"] = [part.scale(s_uni, s_uni, s_uni) for part in scene.nodes["mandibles"]]
        xf = scene.get_node_transform("mandibles")
        if xf is not None:
            tx, ty, tz = xf.translation
            scene.set_node_transform(
                "mandibles",
                translation=(
                    p_hg[0] + sx * (tx - p_hg[0]),
                    p_hg[1] + sy * (ty - p_hg[1]),
                    p_hg[2] + sz * (tz - p_hg[2]),
                ),
            )

    new_hc = _scale_pt_aniso(hc)
    new_hr = (hrx * sx, hry * sy, hrz * sz)
    new_head_back_z = new_hc[2] - new_hr[2]

    # 3. Seat sleek, tall, slicked-back (-Z) Bomber antennae flush ON the new flat cranium surface
    # via exact ray-triangle crown sampling (zero floating gap or buried base!).
    ant_rads = [
        (0.020, 0.020),
        (0.017, 0.018),
        (0.014, 0.015),
        (0.016, 0.017),
        (0.020, 0.022),
        (0.023, 0.025),
        (0.012, 0.014),
    ]
    for side, tag, orig_key in ((+1.0, "left", "antenna_base_left"), (-1.0, "right", "antenna_base_right")):
        node_name = f"antenna_{tag}"
        raw_base = _scale_pt_aniso(anchors[orig_key])
        bx = raw_base[0] * 0.96
        bz = raw_base[2] - 0.008
        surf_y = (
            _cranium_top_y_at(cranium_part, bx, bz, crown_start_y + crown_cap_h * 0.94)
            if cranium_part is not None
            else crown_start_y + crown_cap_h * 0.94
        )
        by = surf_y - 0.002
        base_p = (bx, by, bz)
        # Compact slicked-back Bomber antennae matching smaller head & abst301/abst201/abst901.
        h_boost = 0.012 if side > 0 else -0.009
        z_extra = -0.010 if side > 0 else +0.008
        pts = [
            (bx - side * 0.003, by - 0.018, bz + 0.003),
            base_p,
            (bx + side * 0.010, by + 0.088 + 0.35 * h_boost, bz - 0.020 + 0.3 * z_extra),
            (bx + side * 0.019, by + 0.146 + 0.70 * h_boost, bz - 0.056 + 0.6 * z_extra),
            (bx + side * 0.026, by + 0.172 + 0.95 * h_boost, bz - 0.094 + 0.9 * z_extra),
            (bx + side * 0.030, by + 0.166 + h_boost, bz - 0.124 + z_extra),
            (bx + side * 0.032, by + 0.148 + 0.90 * h_boost, bz - 0.142 + 1.1 * z_extra),
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
            ellipsoid(0.022, 0.011, 0.022, u_segs=12, v_segs=8, name=f"antenna_socket_{tag}", material="chitin_dark")
            .rotate_xyz(rx=-0.16, rz=-side * 0.14)
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


def _bomber_arm_pose(side: int, crouch: float) -> List[Tuple[float, float, float]]:
    """Build lifted, forward-reaching athletic Bomber arm curve clearing every leg cleanly (abst901)."""
    s = 1.0 if side >= 0 else -1.0
    dy_s = 0.010 if side >= 0 else -0.008
    dz_s = 0.010 if side >= 0 else -0.008
    return [
        (s * 0.074, 0.456 - crouch, 0.014),
        (s * 0.138, 0.422 - crouch + 0.5 * dy_s, 0.038 + 0.4 * dz_s),
        (s * 0.110, 0.404 - crouch + 0.8 * dy_s, 0.144 + 0.8 * dz_s),
        (s * 0.088, 0.400 - crouch + dy_s, 0.190 + dz_s),
    ]


def _oriented_joint_armor(
    center: np.ndarray,
    prev_pt: np.ndarray,
    next_pt: np.ndarray,
    radii: Tuple[float, float, float],
    push: float,
    *,
    name: str,
    material: str,
) -> MeshPart:
    """Build a flared exoskeleton joint condyle/collar shell oriented along the exterior bend bisector."""
    v1 = center - prev_pt
    v1 /= max(1e-8, float(np.linalg.norm(v1)))
    v2 = center - next_pt
    v2 /= max(1e-8, float(np.linalg.norm(v2)))
    bis = v1 + v2
    if float(np.linalg.norm(bis)) < 1e-4:
        bis = np.array([0.0, 0.0, 1.0], dtype=np.float64)
    bis /= max(1e-8, float(np.linalg.norm(bis)))
    hy = np.cross(v1, v2)
    if float(np.linalg.norm(hy)) < 1e-4:
        hy = np.array([1.0, 0.0, 0.0], dtype=np.float64)
    hy /= max(1e-8, float(np.linalg.norm(hy)))
    hx = np.cross(hy, bis)
    hx /= max(1e-8, float(np.linalg.norm(hx)))
    hy = np.cross(bis, hx)
    mat = np.eye(4, dtype=np.float64)
    mat[:3, 0], mat[:3, 1], mat[:3, 2] = hx, hy, bis
    mat[:3, 3] = center + push * bis
    return superellipsoid(
        radii[0], radii[1], radii[2], e1=0.62, e2=0.66, u_segs=14, v_segs=10, name=name, material=material
    ).transform(mat)


def _rebuild_bomber_legs(scene: SceneBuilder, crouch: float) -> None:
    """Rebuild Bomber's 4 legs as segmented glossy dark-brown (`leg_chitin`) exoskeleton insect legs.

    Sculpts pinched hip/knee/ankle joints, muscular spindle femurs, flared armored knee condyles and
    ankle collars, tapered angular tibias, and 2-segment pointed claw tarsi grounded on Y = 0.0 while
    preserving Bomber's narrow upright stance and zero leg-vs-leg / arm-vs-leg phasing.
    """
    c = float(crouch)
    leg_mat = "leg_chitin"
    radii = [
        (0.027, 0.031), (0.017, 0.019), (0.029, 0.035), (0.034, 0.042), (0.027, 0.033), (0.022, 0.026),
        (0.0155, 0.018), (0.022, 0.027), (0.024, 0.030), (0.019, 0.024),
        (0.0135, 0.0155), (0.0185, 0.021), (0.0125, 0.0135), (0.0175, 0.0165), (0.0145, 0.0135), (0.0068, 0.0062),
    ]
    lift = np.array([0.0, 1.0, 0.0], dtype=np.float64)
    specs = (
        ("mid_left", +1, 0.062, 0.326 - c, +0.044, (0.122, 0.214, +0.146), (0.118, 0.104, +0.076), (0.114, 0.0, +0.138), +0.15, +0.038),
        ("mid_right", -1, 0.062, 0.326 - c, +0.044, (0.118, 0.218, +0.140), (0.114, 0.106, +0.072), (0.110, 0.0, +0.132), -0.14, +0.038),
        ("rear_left", +1, 0.060, 0.302 - c, -0.062, (0.132, 0.194, -0.040), (0.128, 0.094, -0.132), (0.122, 0.0, -0.068), +0.20, -0.058),
        ("rear_right", -1, 0.060, 0.302 - c, -0.062, (0.128, 0.198, -0.046), (0.124, 0.096, -0.138), (0.118, 0.0, -0.074), -0.18, -0.058),
    )
    for label, side, hx, hy, hz, knee, ankle, foot, foot_yaw, root_z in specs:
        s = 1.0 if side >= 0 else -1.0
        p_root = np.array([s * 0.044, hy + 0.006, root_z], dtype=np.float64)
        p_hip = np.array([s * hx, hy, hz], dtype=np.float64)
        p_knee = np.array([s * knee[0], knee[1], knee[2]], dtype=np.float64)
        p_ankle = np.array([s * ankle[0], ankle[1], ankle[2]], dtype=np.float64)
        p_foot = np.array([s * foot[0], 0.0, foot[2]], dtype=np.float64)
        fwd = np.array([math.sin(foot_yaw), 0.0, math.cos(foot_yaw)], dtype=np.float64)

        femur_lo = 0.76 * p_hip + 0.24 * p_knee + np.array([s * 0.002, 0.007, 0.003])
        femur_mid = 0.46 * p_hip + 0.54 * p_knee + np.array([s * 0.004, 0.015, 0.006])
        femur_hi = 0.22 * p_hip + 0.78 * p_knee + np.array([s * 0.002, 0.007, 0.004])
        pre_knee = 0.08 * p_hip + 0.92 * p_knee + np.array([s * 0.001, 0.002, 0.002])
        post_knee = 0.90 * p_knee + 0.10 * p_ankle + np.array([s * 0.002, -0.002, 0.003])
        tibia_mid = 0.54 * p_knee + 0.46 * p_ankle + np.array([s * 0.004, 0.001, -0.004])
        pre_ankle = 0.16 * p_knee + 0.84 * p_ankle + np.array([s * 0.002, 0.003, -0.005])
        tarsus_1 = 0.70 * p_ankle + 0.30 * (p_foot + 0.020 * lift) + np.array([0.0, -0.004, -0.002])
        tarsus_joint = 0.36 * p_ankle + 0.64 * (p_foot + 0.017 * lift)
        tarsus_heel = p_foot + 0.0165 * lift - 0.005 * fwd
        tarsus_ball = p_foot + 0.0135 * lift + 0.018 * fwd
        tarsus_tip = p_foot + 0.0075 * lift + 0.044 * fwd

        leg_tube = curved_tube(
            [
                p_root, p_hip, femur_lo, femur_mid, femur_hi, pre_knee,
                p_knee, post_knee, tibia_mid, pre_ankle,
                p_ankle, tarsus_1, tarsus_joint, tarsus_heel, tarsus_ball, tarsus_tip,
            ],
            radii,
            samples_per_seg=3,
            radial_segs=10,
            cap_rings=3,
            name=f"leg_{label}",
            material=leg_mat,
        )
        coxa = ellipsoid(0.023, 0.023, 0.023, u_segs=12, v_segs=8, name=f"leg_coxa_{label}", material=leg_mat).translate(*p_hip)
        kneecap = _oriented_joint_armor(p_knee, femur_hi, post_knee, (0.021, 0.019, 0.026), 0.005, name=f"leg_kneecap_{label}", material=leg_mat)
        ankle_cap = _oriented_joint_armor(p_ankle, pre_ankle, tarsus_1, (0.017, 0.015, 0.021), 0.004, name=f"leg_anklecap_{label}", material=leg_mat)

        def _ground_sole(pos: np.ndarray) -> np.ndarray:
            p = pos.copy()
            p[:, 1] = np.maximum(p[:, 1], 0.0)
            return p

        ball = p_foot + 0.018 * fwd
        pivot = (float(ball[0]), 0.0, float(ball[2]))
        node = f"leg_{label}"
        scene.nodes[node] = [
            part.deform(_ground_sole).translate(-pivot[0], 0.0, -pivot[2])
            for part in (leg_tube, coxa, kneecap, ankle_cap)
        ]
        scene.set_node_parent(node, "legs")
        scene.set_node_transform(node, translation=pivot)


def build_bomber(blink: float = 0.0) -> SceneBuilder:
    """Build the sculpted 3D Bomber Ant SceneBuilder with compact upright stance, goggles, Blink, and 1.70s Idle."""
    bomber_crouch = 0.095
    scene, anchors = build_base_ant(
        scene_name="ant_bomber",
        include_antennae=True,
        include_eyes=True,
        include_brows=False,
        brow_style="eager",
        head_tilt_deg=-2.5,
        head_yaw_deg=3.0,
        head_pitch_deg=-14.0,
        eye_scale_left=0.88,
        eye_scale_right=0.88,
        eye_look=(-0.06, 0.04),
        antenna_asymmetry=0.45,
        stance_asymmetry=0.40,
        leg_style="bomber",
        include_idle=False,
        crouch=bomber_crouch,
        left_arm_pose=_bomber_arm_pose(+1, bomber_crouch),
        right_arm_pose=_bomber_arm_pose(-1, bomber_crouch),
        blink=blink,
    )
    _rebuild_bomber_legs(scene, bomber_crouch)
    head_c, head_r, eye_l, eye_r, head_back_z = _scale_bomber_head_and_tall_antennae(
        scene,
        anchors,
        head_scale_x=0.64,
        head_scale_y=0.77,
        head_scale_z=0.79,
    )
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
    _add_bomber_attack(scene)
    # Uniform similarity scale so Bomber lands cleanly at ~0.86-0.88 total height with smaller head & narrow stance.
    scene.scale_all(1.072)
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical entry point discovered by build_all.py."""
    return build_bomber(blink=blink)
