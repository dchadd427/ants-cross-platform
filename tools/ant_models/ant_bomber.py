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
    # Compact sleek circular diameter sitting cleanly inside head width + noticeably longer metallic cans!
    bore_r = max(0.058, max_lid_r + 0.0048)
    lens_z = max(0.148, max_lid_z + 0.068)

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

    # Sleek all-yellow metallic nose bridge & gold center rivet joining the two long metallic cans cleanly.
    bridge_z = ez + lens_z * 0.62
    bridge = (
        rounded_box(0.032, 0.018, 0.024, bevel=0.005, segs=3, name="goggle_bridge", material="gear_gold_metal")
        .translate(hx, ey + 0.009, bridge_z)
    )
    scene.add("goggles", bridge)

    bridge_stud = (
        cylinder(0.0056, 0.0056, 0.008, radial_segs=14, bevel=0.0018, axis="z", name="goggle_bridge_stud", material="gear_gold_metal")
        .translate(hx, ey + 0.009, bridge_z + 0.012)
    )
    scene.add("goggles", bridge_stud)

    # Compact metallic gold temple clasps & thicker yellow strap tabs connecting the cans smoothly into the rear strap.
    rx_strap = hrx + 0.006
    rz_strap = hrz + 0.007
    strap_base_y = ey + 0.013
    for side_sign, tag, eye_pos in ((+1.0, "left", eye_left), (-1.0, "right", eye_right)):
        cup_side_x = eye_pos[0] + side_sign * (bore_r + 0.006)
        cup_side_z = eye_pos[2] + 0.024
        temple_x = hx + side_sign * (rx_strap * _spow(math.sin(math.radians(52.0)), 0.76))
        temple_z = hz + rz_strap * _spow(math.cos(math.radians(52.0)), 0.76)
        tab = curved_tube(
            [
                (cup_side_x, ey + 0.011, cup_side_z),
                (0.5 * (cup_side_x + temple_x) + side_sign * 0.012, ey + 0.012, 0.5 * (cup_side_z + temple_z) + 0.010),
                (temple_x, strap_base_y, temple_z),
            ],
            (0.022, 0.011),
            samples_per_seg=4,
            radial_segs=10,
            cap_rings=2,
            name=f"goggle_temple_tab_{tag}",
            material="gear_gold",
        )
        scene.add("goggles", tab)

        clasp = (
            rounded_box(0.018, 0.034, 0.034, bevel=0.006, segs=3, name=f"goggle_clasp_{tag}", material="gear_gold_metal")
            .rotate_xyz(ry=side_sign * 0.36)
            .translate(temple_x - side_sign * 0.006, strap_base_y - 0.001, temple_z + 0.004)
        )
        scene.add("goggles", clasp)

    # Bold thicker yellow head strap (~1.25x cross-section) hugging right below the flat-top cranium rim (e2=0.76).
    strap_pts: List[Tuple[float, float, float]] = []
    n_arc = 28
    start_ang = math.radians(52.0)
    end_ang = math.radians(308.0)
    for i in range(n_arc + 1):
        ang = start_ang + (end_ang - start_ang) * (i / n_arc)
        sx = hx + rx_strap * _spow(math.sin(ang), 0.76)
        sz = hz + rz_strap * _spow(math.cos(ang), 0.76)
        rear_lift = 0.010 * (0.5 * (1.0 - math.cos(ang)))
        strap_pts.append((sx, strap_base_y + rear_lift, sz))

    strap = swept_tube(
        strap_pts,
        (0.025, 0.012),
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
        rounded_box(0.056, 0.050, 0.020, bevel=0.006, segs=3, name="goggle_rear_buckle", material="gear_gold_metal")
        .translate(hx, strap_base_y + 0.010, hz - rz_strap - 0.002)
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
    head_scale_xz: float = 0.90,
    head_scale_y: float = 0.77,
    eye_inward_dx: float = -0.004,
) -> Tuple[
    Tuple[float, float, float],
    Tuple[float, float, float],
    Tuple[float, float, float],
    Tuple[float, float, float],
    float,
]:
    """Flatten Bomber's cranium into a sleek goggle-level flat top while keeping eyes spherical & antennae flush.

    Matches `abst301`/`abst901`: sleek C^3 flat-topped skull sitting nearly level with the top rim of the
    yellow goggles (zero tall green dome above the goggles), uniformly spherical eyes & tucked eyelids
    (zero blink=0 forehead protrusion), and bold slicked-back antennae seated flush on the flat top.
    """
    hc = anchors["head_center"]
    hrx, hry, hrz = anchors["head_radii"]
    head_xf = scene.get_node_transform("head")
    neck_pivot = (
        (-head_xf.translation[0], -head_xf.translation[1], -head_xf.translation[2])
        if head_xf is not None
        else anchors["neck_center"]
    )
    # Anchor scaling slightly below head center so the flatter skull stays seated flush on neck_joint.
    piv = (hc[0], hc[1] - 0.052, hc[2])
    p_hg = (piv[0] - neck_pivot[0], piv[1] - neck_pivot[1], piv[2] - neck_pivot[2])
    sx = float(head_scale_xz)
    sy = float(head_scale_y)
    sz = float(head_scale_xz)
    s_uni = float(head_scale_xz)

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

    # 1. Scale `head` around `piv`, then apply a smooth C^3 superelliptic flat-top ceiling deform to
    # `cranium` above eye/strap midline so the top of Bomber's skull sits nearly flush with the top edge
    # of the yellow goggles (matching abst901/abst301) while leaving eyes, eyelids, and jaw untouched.
    ey_mid = 0.5 * (new_eye_l[1] + new_eye_r[1])
    crown_start_y = ey_mid + 0.020
    crown_cap_h = 0.064
    crown_p = 3.5

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
        (0.023, 0.023),
        (0.019, 0.020),
        (0.016, 0.017),
        (0.018, 0.019),
        (0.023, 0.025),
        (0.026, 0.028),
        (0.014, 0.016),
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
        h_boost = 0.020 if side > 0 else -0.014
        z_extra = -0.016 if side > 0 else +0.012
        pts = [
            (bx - side * 0.004, by - 0.022, bz + 0.003),
            base_p,
            (bx + side * 0.014, by + 0.132 + 0.35 * h_boost, bz - 0.032 + 0.3 * z_extra),
            (bx + side * 0.028, by + 0.224 + 0.70 * h_boost, bz - 0.088 + 0.6 * z_extra),
            (bx + side * 0.038, by + 0.264 + 0.95 * h_boost, bz - 0.148 + 0.9 * z_extra),
            (bx + side * 0.044, by + 0.258 + h_boost, bz - 0.198 + z_extra),
            (bx + side * 0.048, by + 0.230 + 0.90 * h_boost, bz - 0.228 + 1.1 * z_extra),
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
            ellipsoid(0.025, 0.012, 0.025, u_segs=12, v_segs=8, name=f"antenna_socket_{tag}", material="chitin_dark")
            .rotate_xyz(rx=-0.08, rz=-side * 0.08)
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
    """Build the sculpted 3D Bomber Ant SceneBuilder with flatter crown, compact goggles, Blink, and 1.70s Idle."""
    scene, anchors = build_base_ant(
        scene_name="ant_bomber",
        include_antennae=True,
        include_eyes=True,
        include_brows=False,
        brow_style="eager",
        head_tilt_deg=-5.5,
        head_yaw_deg=3.5,
        head_pitch_deg=-16.0,
        eye_scale_left=0.78,
        eye_scale_right=0.78,
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
    head_c, head_r, eye_l, eye_r, head_back_z = _scale_bomber_head_and_tall_antennae(
        scene,
        anchors,
        head_scale_xz=0.90,
        head_scale_y=0.77,
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
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical entry point discovered by build_all.py."""
    return build_bomber(blink=blink)
