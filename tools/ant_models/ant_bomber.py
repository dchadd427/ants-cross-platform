"""Sculpted 3D Bomber Ant (compact riveted aviator goggles, empty idle hands, 3-bomb pack, 1.70s Idle).

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
    from .ability_clips_special import add_bomber_abilities
    from .hit_clips_special import add_bomber_hit_animation
except ImportError:
    from ability_clips_special import add_bomber_abilities  # type: ignore
    from base_ant import build_base_ant, sync_arm_idle_to_clip, sync_leg_flex_to_clip  # type: ignore
    from hit_clips_special import add_bomber_hit_animation  # type: ignore
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
    head_back_z: Optional[float] = None,
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

    # Bold yellow head strap hugging flush around Bomber's slimmed rear occiput (zero floating gap!).
    rz_back_strap = (hz - head_back_z + 0.004) if head_back_z is not None else rz_strap
    strap_pts: List[Tuple[float, float, float]] = []
    n_arc = 28
    start_ang = math.radians(52.0)
    end_ang = math.radians(308.0)
    for i in range(n_arc + 1):
        ang = start_ang + (end_ang - start_ang) * (i / n_arc)
        ca = math.cos(ang)
        rx_cur = rx_strap * (1.0 - 0.10 * (abs(ca) ** 1.4)) if ca < 0.0 else rx_strap
        rz_cur = rz_back_strap if ca < 0.0 else rz_strap
        sx = hx + rx_cur * _spow(math.sin(ang), 0.76)
        sz = hz + rz_cur * _spow(ca, 0.76)
        rear_lift = 0.006 * (0.5 * (1.0 - ca))
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
        .translate(hx, strap_base_y + 0.006, hz - rz_back_strap - 0.002)
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

    if abs(fuse_curve_x) < 1e-4:
        # Center bomb fuse arches gracefully up & rearward (-Z) with a subtle S-flick, clear of both antennae!
        fuse_pts = [
            (0.0, 1.63 * h, 0.0),
            (0.05 * h, 2.08 * h, -0.18 * h),
            (-0.04 * h, 2.52 * h, -0.42 * h),
            (0.03 * h, 2.84 * h, -0.64 * h),
            (0.01 * h, 2.94 * h, -0.78 * h),
        ]
    else:
        fdir = 1.0 if fuse_curve_x > 0.0 else -1.0
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


def _snap_pts_to_thorax(
    pts: List[Tuple[float, float, float]],
    thorax_core: Optional[MeshPart],
    center: Tuple[float, float, float],
    offset: float = 0.0055,
) -> List[Tuple[float, float, float]]:
    """Ray-cast harness points onto `thorax_core` so straps hug Bomber's torso with zero gap."""
    if thorax_core is None:
        return pts
    ctr = np.asarray(center, dtype=np.float64)
    tri = thorax_core.positions.astype(np.float64)[thorax_core.indices.reshape(-1, 3)]
    v0, e1, e2 = tri[:, 0] - ctr, tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0]
    snapped: List[Tuple[float, float, float]] = []
    for pt in pts:
        v = np.asarray(pt, dtype=np.float64) - ctr
        norm = float(np.linalg.norm(v))
        if norm < 1e-8:
            snapped.append(pt)
            continue
        d = v / norm
        h = np.cross(d[None, :], e2)
        a = np.einsum("ij,ij->i", e1, h)
        ok = np.abs(a) > 1e-12
        f = np.where(ok, 1.0 / np.where(ok, a, 1.0), 0.0)
        u, q = f * np.einsum("ij,ij->i", -v0, h), np.cross(-v0, e1)
        vv, t = f * (q @ d), f * np.einsum("ij,ij->i", e2, q)
        hit = ok & (u >= -1e-6) & (vv >= -1e-6) & (u + vv <= 1.0 + 1e-6) & (t > 0.0)
        snapped.append(tuple(float(c) for c in (ctr + d * (float(np.max(t[hit])) + offset))) if np.any(hit) else pt)
    return snapped


def _build_dynamite_pack(
    scene: SceneBuilder,
    back_anchor: Tuple[float, float, float],
    thorax_center: Tuple[float, float, float],
    head_back_z: float,
) -> None:
    """Build Bomber's tan-leather tactical backpack seated flush against his back (zero air gap)."""
    bx, _, bz = back_anchor
    tx, ty, tz = thorax_center
    # Front of pack embeds ~1.8 cm into rear dorsal thorax (`z ~ -0.088`), tilted cleanly behind cranium.
    pack_y = ty + 0.060
    pack_z = bz - 0.022
    pack_rx = -0.36

    pack_body = rounded_box(0.188, 0.184, 0.138, bevel=0.028, segs=4, name="pack_rucksack_body", material="prop_brown").rotate_xyz(rx=pack_rx).translate(bx, pack_y + 0.010, pack_z - 0.046)
    pack_base = rounded_box(0.174, 0.048, 0.126, bevel=0.015, segs=3, name="pack_base_boot", material="gear_dark").rotate_xyz(rx=pack_rx).translate(bx, pack_y - 0.072, pack_z - 0.018)
    rear_pouch = rounded_box(0.136, 0.100, 0.042, bevel=0.015, segs=3, name="pack_rear_pouch", material="prop_brown").rotate_xyz(rx=pack_rx).translate(bx, pack_y - 0.010, pack_z - 0.112)
    scene.add("dynamite_pack", [pack_body, pack_base, rear_pouch])

    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        side_pouch = rounded_box(0.062, 0.112, 0.084, bevel=0.016, segs=3, name=f"pack_side_pouch_{tag}", material="prop_brown").rotate_xyz(rx=pack_rx, rz=-side_sign * 0.24).translate(bx + side_sign * 0.092, pack_y + 0.022, pack_z - 0.056)
        scene.add("dynamite_pack", side_pouch)

    # Thorax-hugging shoulder straps & lower waist side straps anchored directly into the rucksack.
    thorax_core = next((p for p in scene.get_node("body") if p.name == "thorax_core"), None)
    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        upper_pack_anchor = (side_sign * 0.054, pack_y + 0.050, pack_z - 0.012)
        shoulder_mid = _snap_pts_to_thorax(
            [
                (side_sign * 0.054, ty + 0.088, tz - 0.046),
                (side_sign * 0.058, ty + 0.092, tz + 0.016),
                (side_sign * 0.058, ty + 0.064, tz + 0.068),
                (side_sign * 0.054, ty + 0.020, tz + 0.086),
                (side_sign * 0.052, ty - 0.026, tz + 0.078),
            ],
            thorax_core, (tx, ty, tz), offset=0.0052,
        )
        scene.add("dynamite_pack", curved_tube([upper_pack_anchor, *shoulder_mid], (0.0092, 0.0135), samples_per_seg=4, radial_segs=10, cap_rings=2, name=f"harness_shoulder_{tag}", material="gear_dark"))

        lower_pack_anchor = (side_sign * 0.064, pack_y - 0.050, pack_z - 0.012)
        side_mid = _snap_pts_to_thorax(
            [
                (side_sign * 0.056, ty - 0.024, tz + 0.072),
                (side_sign * 0.074, ty - 0.022, tz + 0.022),
                (side_sign * 0.072, ty - 0.014, tz - 0.038),
            ],
            thorax_core, (tx, ty, tz), offset=0.0048,
        )
        scene.add("dynamite_pack", curved_tube([*side_mid, lower_pack_anchor], (0.0085, 0.0115), samples_per_seg=4, radial_segs=8, cap_rings=2, name=f"harness_side_{tag}", material="gear_dark"))

        b_pt = shoulder_mid[3]
        scene.add("dynamite_pack", rounded_box(0.022, 0.026, 0.015, bevel=0.0042, segs=2, name=f"harness_buckle_{tag}", material="gear_gold_metal").rotate_xyz(rx=-0.16, ry=side_sign * 0.28).translate(b_pt[0], b_pt[1] + 0.004, b_pt[2] + 0.005))

    belt_pts = _snap_pts_to_thorax(
        [(-0.062, ty - 0.022, tz + 0.056), (-0.038, ty - 0.022, tz + 0.084), (0.0, ty - 0.022, tz + 0.092), (0.038, ty - 0.022, tz + 0.084), (0.062, ty - 0.022, tz + 0.056)],
        thorax_core, (tx, ty, tz), offset=0.0052,
    )
    scene.add("dynamite_pack", curved_tube(belt_pts, (0.011, 0.0082), samples_per_seg=4, radial_segs=8, cap_rings=2, name="harness_chest_belt", material="gear_dark"))

    for strap_idx, y_off, z_off in ((0, 0.040, -0.120), (1, -0.022, -0.124)):
        c_pts = [
            (-0.124, pack_y + y_off - 0.012, pack_z - 0.038), (-0.104, pack_y + y_off + 0.008, pack_z + z_off + 0.024),
            (0.0, pack_y + y_off + 0.016, pack_z + z_off), (+0.104, pack_y + y_off + 0.008, pack_z + z_off + 0.024),
            (+0.124, pack_y + y_off - 0.012, pack_z - 0.038),
        ]
        scene.add("dynamite_pack", curved_tube(c_pts, (0.014, 0.0105), samples_per_seg=4, radial_segs=10, cap_rings=2, name=f"pack_cinch_strap_{strap_idx}", material="gear_dark"))

    scene.add("dynamite_pack", rounded_box(0.042, 0.036, 0.018, bevel=0.0055, segs=3, name="bomb_rack_buckle", material="gear_gold_metal").rotate_xyz(rx=pack_rx).translate(0.0, pack_y + 0.058, pack_z - 0.128))

    # Seat 3 cup-bombs into rucksack pockets (`pack_bomb_center` node lets `PlantBomb` draw center bomb!).
    pack_specs = (
        (+22.0, -24.0, -0.092, 0.048, -0.070, 0.086, -0.036, "dynamite_pack"),
        (0.0, -27.0, 0.000, 0.080, -0.090, 0.089, 0.000, "pack_bomb_center"),
        (-22.0, -24.0, +0.092, 0.048, -0.070, 0.086, +0.036, "dynamite_pack"),
    )
    for idx, (rz_deg, rx_deg, ox, oy, oz, cup_h, fuse_dx, target_node) in enumerate(pack_specs):
        scene.add("dynamite_pack", cylinder(0.50 * cup_h, 0.46 * cup_h, 0.046, radial_segs=16, bevel=0.006, axis="y", name=f"pack_holster_{idx}", material="gear_dark").rotate_xyz(rx=rx_deg, rz=rz_deg, degrees=True).translate(bx + ox, pack_y + oy, pack_z + oz))
        for part in _build_cup_bomb(f"pack_bomb_{idx}", cup_h, fuse_dx, lit=True, rivet_count=10):
            scene.add(target_node, part.rotate_xyz(rx=rx_deg, rz=rz_deg, degrees=True).translate(bx + ox, pack_y + oy, pack_z + oz))


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

    # 1. Scale `head` around `piv`, apply soft crown rounding, and tuck ONLY the rear occiput (`-Z`)
    # smoothly inward (~0.77x rear depth with subtle rear lateral taper, front face 100% untouched!).
    new_hc = _scale_pt_aniso(hc)
    new_hr = (hrx * sx, hry * sy, hrz * sz)
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
        dz = p[:, 2] - new_hc[2]
        rear = dz < 0.0
        if np.any(rear):
            t_back = np.clip(-dz[rear] / max(1e-5, new_hr[2]), 0.0, 1.0)
            p[rear, 2] = new_hc[2] + dz[rear] * (1.0 - 0.23 * (t_back ** 0.85))
            p[rear, 0] = new_hc[0] + (p[rear, 0] - new_hc[0]) * (1.0 - 0.10 * (t_back ** 1.4))
            p[rear, 1] -= 0.010 * (t_back ** 1.5)
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

    new_head_back_z = float(cranium_part.positions[:, 2].min()) if cranium_part is not None else (new_hc[2] - new_hr[2])

    # 3. Seat bold, slightly shorter Bomber antennae flush ON the crown via exact ray-triangle sampling:
    # smooth monotonic narrow-to-thick taper (ZERO pinched neck or spherical ball knob at the tip!),
    # sweeping cleanly up-and-back (`+Y/-Z`) well clear of the 3 magenta bombs & curved fuses.
    ant_rads = [
        (0.015, 0.015),
        (0.015, 0.015),
        (0.019, 0.020),
        (0.023, 0.024),
        (0.028, 0.030),
        (0.031, 0.033),
        (0.033, 0.035),
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
        # Slightly shorter (~-16%) small-to-thick flared antennae matching ORIG abst301/201/901/701.
        h_boost = 0.012 if side > 0 else -0.008
        z_extra = -0.007 if side > 0 else +0.005
        pts = [
            (bx - side * 0.003, by - 0.018, bz + 0.003),
            base_p,
            (bx + side * 0.012, by + 0.078 + 0.35 * h_boost, bz - 0.014 + 0.3 * z_extra),
            (bx + side * 0.024, by + 0.144 + 0.70 * h_boost, bz - 0.040 + 0.6 * z_extra),
            (bx + side * 0.034, by + 0.190 + 0.90 * h_boost, bz - 0.075 + 0.85 * z_extra),
            (bx + side * 0.041, by + 0.210 + h_boost, bz - 0.107 + z_extra),
            (bx + side * 0.044, by + 0.207 + 0.95 * h_boost, bz - 0.130 + 1.05 * z_extra),
        ]
        stalk = swept_tube(
            pts,
            ant_rads,
            radial_segs=14,
            smooth_subdiv=3,
            cap_rings=4,
            name=f"antenna_stalk_{tag}",
            material="chitin_dark",
        )
        sock = (
            ellipsoid(0.020, 0.010, 0.020, u_segs=12, v_segs=8, name=f"antenna_socket_{tag}", material="chitin_dark")
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


def _rebuild_bomber_legs(scene: SceneBuilder, crouch: float) -> None:
    """Rebuild Bomber's 4 legs as continuous, streamlined, ball-free body-colored (`chitin_team`) insect legs.

    Sweeps a single smooth tapered exoskeleton curve per leg (zero protruding coxa/knee/ankle balls or
    pinched bead segments) while preserving Bomber's narrow upright stance and ground planting at Y = 0.0.
    """
    c = float(crouch)
    leg_mat = "chitin_team"
    radii = [
        (0.0180, 0.0200),
        (0.0205, 0.0235),
        (0.0225, 0.0265),
        (0.0180, 0.0205),
        (0.0170, 0.0190),
        (0.0156, 0.0174),
        (0.0142, 0.0156),
        (0.0124, 0.0136),
        (0.0120, 0.0130),
        (0.0112, 0.0120),
        (0.0124, 0.0110),
        (0.0110, 0.0096),
        (0.0048, 0.0044),
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
        p_root = np.array([s * 0.034, hy + 0.006, root_z], dtype=np.float64)
        p_hip = np.array([s * hx, hy, hz], dtype=np.float64)
        p_knee = np.array([s * knee[0], knee[1], knee[2]], dtype=np.float64)
        p_ankle = np.array([s * ankle[0], ankle[1], ankle[2]], dtype=np.float64)
        p_foot = np.array([s * foot[0], 0.0, foot[2]], dtype=np.float64)
        fwd = np.array([math.sin(foot_yaw), 0.0, math.cos(foot_yaw)], dtype=np.float64)

        femur_mid = 0.50 * p_hip + 0.50 * p_knee + np.array([s * 0.002, 0.006, 0.003])
        pre_knee = 0.16 * p_hip + 0.84 * p_knee + np.array([s * 0.001, 0.002, 0.001])
        post_knee = 0.86 * p_knee + 0.14 * p_ankle
        tibia_mid = 0.48 * p_knee + 0.52 * p_ankle + np.array([s * 0.001, 0.001, -0.002])
        pre_ankle = 0.14 * p_knee + 0.86 * p_ankle
        tarsus_heel = p_foot + 0.0102 * lift - 0.005 * fwd
        tarsus_mid = 0.50 * p_ankle + 0.50 * tarsus_heel
        tarsus_ball = p_foot + 0.0086 * lift + 0.018 * fwd
        tarsus_tip = p_foot + 0.0036 * lift + 0.044 * fwd

        leg_tube = curved_tube(
            [
                p_root, p_hip, femur_mid, pre_knee,
                p_knee,
                post_knee, tibia_mid, pre_ankle,
                p_ankle,
                tarsus_mid, tarsus_heel, tarsus_ball, tarsus_tip,
            ],
            radii,
            samples_per_seg=4,
            radial_segs=14,
            cap_rings=3,
            name=f"leg_{label}",
            material=leg_mat,
        )

        def _ground_sole(pos: np.ndarray) -> np.ndarray:
            p = pos.copy()
            p[:, 1] = np.maximum(p[:, 1], 0.0)
            return p

        ball = p_foot + 0.018 * fwd
        pivot = (float(ball[0]), 0.0, float(ball[2]))
        node = f"leg_{label}"
        scene.nodes[node] = [leg_tube.deform(_ground_sole).translate(-pivot[0], 0.0, -pivot[2])]
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
        head_back_z=head_back_z,
    )
    _build_dynamite_pack(scene, anchors["back_anchor"], anchors["thorax_center"], head_back_z)

    scene.attach_world_node("goggles", "head_group", preserve_world_pose=False)
    head_xf = scene.get_node_transform("head")
    if head_xf is not None:
        scene.set_node_transform("goggles", translation=head_xf.translation)
    scene.attach_world_node("dynamite_pack", "upper_body", preserve_world_pose=False)
    scene.attach_world_node("pack_bomb_center", "upper_body", preserve_world_pose=False)
    _add_bomber_idle(scene)
    _add_bomber_attack(scene)
    # Uniform similarity scale so Bomber lands cleanly at ~0.86-0.88 total height with smaller head & narrow stance.
    scene.scale_all(1.072)
    add_bomber_hit_animation(scene)
    add_bomber_abilities(scene)
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical entry point discovered by build_all.py."""
    return build_bomber(blink=blink)
