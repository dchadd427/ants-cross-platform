"""Sculpted 3D Swimmer Ant (bold chunky scuba mask, snorkel, steel tank, flippers, shovel, 0.90s Idle).

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet and diving flippers grounded solidly at Y = 0.0)
  +Z = front (South / canonical camera 301)
  +X = ant's left (viewer's right from front), -X = ant's right
"""
from __future__ import annotations

import math
from typing import Any, Dict, List, Optional, Tuple

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
        lathe,
        quat_from_euler_xyz,
        quat_mul,
        recompute_smooth_normals,
        rounded_box,
        superellipsoid,
        swept_tube,
        torus_arc,
    )
    from .hit_clips_special import add_swimmer_hit_animation
except ImportError:
    from base_ant import build_base_ant, sync_arm_idle_to_clip, sync_leg_flex_to_clip  # type: ignore
    from hit_clips_special import add_swimmer_hit_animation  # type: ignore
    from mesh_core import (  # type: ignore
        AnimationChannel,
        AnimationClip,
        MeshPart,
        SceneBuilder,
        curved_tube,
        cylinder,
        ellipsoid,
        ensure_outward_winding,
        lathe,
        quat_from_euler_xyz,
        quat_mul,
        recompute_smooth_normals,
        rounded_box,
        superellipsoid,
        swept_tube,
        torus_arc,
    )

# Sporty right-arm grip starting at slim thorax shoulder (-0.078, 0.456, 0.014) holding coplanar beach shovel.
_SWIMMER_RIGHT_ARM: List[Tuple[float, float, float]] = [
    (-0.078, 0.456, 0.014),
    (-0.184, 0.382, 0.018),
    (-0.236, 0.314, 0.046),
    (-0.248, 0.296, 0.066),
]


def _spow(val: float, exp: float) -> float:
    return math.copysign(abs(val) ** exp, val)


def _spow_arr(arr: np.ndarray, exp: float) -> np.ndarray:
    return np.sign(arr) * (np.abs(arr) ** exp)


def _measure_eyelid_world_envelope(
    scene: SceneBuilder,
    eye_left: Tuple[float, float, float],
    closed_angle: float,
) -> Tuple[float, float, float]:
    """Measure max world (+X, +Y offset from eye, +Z) reached by eyelid_left across full blink=0..1."""
    parts = scene.nodes.get("eyelid_left", [])
    if not parts:
        return (eye_left[0] + 0.080, 0.082, eye_left[2] + 0.080)
    verts = np.vstack([p.positions.astype(np.float64) for p in parts])
    ex, ey, ez = eye_left
    max_x = ex
    max_dy = 0.0
    max_z = ez
    for frac in (0.0, 0.25, 0.50, 0.75, 1.0):
        ang = frac * closed_angle
        ca, sa = math.cos(ang), math.sin(ang)
        r_blink = np.array([[1.0, 0.0, 0.0], [0.0, ca, -sa], [0.0, sa, ca]], dtype=np.float64)
        p_w = verts @ r_blink.T + np.array([ex, ey, ez], dtype=np.float64)
        max_x = max(max_x, float(p_w[:, 0].max()))
        max_dy = max(max_dy, float(np.abs(p_w[:, 1] - ey).max()))
        max_z = max(max_z, float(p_w[:, 2].max()))
    return (max_x, max_dy, max_z)


def _hollow_oval_housing(
    profile_uz: List[Tuple[float, float]],
    *,
    rx_in: float,
    ry_in: float,
    wall_t: float,
    oval_exp: float,
    curve_depth: float,
    radial_segs: int = 56,
    name: str,
    material: str,
) -> MeshPart:
    """Build a watertight thick molded oval ring around +Z, curved smoothly to wrap the face.

    `profile_uz` entries are `(u, z)` where `u = 0.0` is the inner viewport wall and `u = 1.0`
    is the outer rim wall.
    """
    closed_prof = [(0.5 + 0.5 * u, z) for u, z in (list(profile_uz) + [profile_uz[0]])]
    ring = lathe(
        closed_prof,
        radial_segs=radial_segs,
        cap_bottom=False,
        cap_top=False,
        axis="z",
        name=name,
        material=material,
    )
    rx_out = rx_in + wall_t

    def _to_curved_oval(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        r = np.hypot(p[:, 0], p[:, 1])
        u = 2.0 * (r - 0.5)
        theta = np.arctan2(p[:, 1], p[:, 0])
        ox_dir = _spow_arr(np.cos(theta), oval_exp)
        oy_dir = _spow_arr(np.sin(theta), oval_exp)
        ox = (rx_in + u * wall_t) * ox_dir
        oy = (ry_in + u * wall_t) * oy_dir
        nose = 0.008 * np.exp(-((ox / 0.052) ** 2)) * np.clip(-np.sin(theta), 0.0, 1.0)
        nx = ox / max(1e-6, rx_out)
        p[:, 0] = ox
        p[:, 1] = oy - nose
        p[:, 2] = p[:, 2] - curve_depth * (nx * nx)
        return p

    return ring.deform(_to_curved_oval).ensure_outward()


def _build_scuba_mask(
    scene: SceneBuilder,
    head_center: Tuple[float, float, float],
    head_radii: Tuple[float, float, float],
    eye_left: Tuple[float, float, float],
    closed_angle: float,
) -> Tuple[float, float, float]:
    """Build a bold, thick, chunky yellow molded oval scuba mask, rubber skirt, clear visor, and rear strap."""
    hx, _, hz = head_center
    hrx, _, hrz = head_radii
    ey = eye_left[1]
    eye_max_x, eye_max_dy, eye_max_z = _measure_eyelid_world_envelope(scene, eye_left, closed_angle)

    mask_y = ey + 0.004
    oval_exp = 1.0  # Pure smooth horizontal ellipse (p=2.0, zero flat sides or boxy corners!)
    curve_depth = 0.052

    # Wide smooth true-oval open window framing both cute eyes & eyelids cleanly with generous corner margin.
    rx_in = max(hrx * 0.86, (eye_max_x - hx) + 0.025)
    ry_in = max(0.104, eye_max_dy + 0.020)
    wall_t = 0.040  # Bold chunky smooth yellow plastic scuba bezel matching asst301..701
    rx_out = rx_in + wall_t

    # Seat the translucent visor safely ahead of eyelid sweep even at the curved outer corner of the eye.
    eye_nx = (eye_max_x - hx) / max(1e-6, rx_out)
    visor_z_center = max(eye_left[2] + 0.102, eye_max_z + 0.012 + curve_depth * (eye_nx * eye_nx))

    # 1. Bold, chunky deep-rimmed smooth yellow plastic oval scuba housing (zero cloth weave).
    frame_prof = [
        (0.00, -0.046),
        (0.86, -0.048),
        (1.00, -0.036),
        (1.02, +0.010),
        (0.95, +0.025),
        (0.76, +0.032),
        (0.18, +0.032),
        (0.02, +0.022),
        (0.00, +0.006),
    ]
    frame = _hollow_oval_housing(
        frame_prof,
        rx_in=rx_in,
        ry_in=ry_in,
        wall_t=wall_t,
        oval_exp=oval_exp,
        curve_depth=curve_depth,
        radial_segs=56,
        name="scuba_mask_frame",
        material="gear_yellow_plastic",
    ).translate(hx, mask_y, visor_z_center)
    scene.add("scuba_mask", frame)

    # Crisp dark recessed inner gasket rim right around the glass viewport opening.
    gasket_prof = [
        (-0.04, -0.006),
        (0.10, -0.006),
        (0.12, +0.014),
        (-0.02, +0.016),
        (-0.06, +0.006),
    ]
    inner_bezel = _hollow_oval_housing(
        gasket_prof,
        rx_in=rx_in,
        ry_in=ry_in,
        wall_t=wall_t,
        oval_exp=oval_exp,
        curve_depth=curve_depth,
        radial_segs=52,
        name="scuba_mask_inner_bezel",
        material="gear_dark",
    ).translate(hx, mask_y, visor_z_center)
    scene.add("scuba_mask", inner_bezel)

    # 2. Hollow molded dark rubber sealing skirt bridging the chunky yellow frame cleanly onto the cranium.
    skirt_prof = [
        (0.06, -0.104),
        (0.58, -0.106),
        (0.96, -0.082),
        (1.00, -0.042),
        (0.12, -0.042),
    ]
    skirt_ring = _hollow_oval_housing(
        skirt_prof,
        rx_in=rx_in * 0.97,
        ry_in=ry_in * 0.97,
        wall_t=wall_t * 0.94,
        oval_exp=oval_exp,
        curve_depth=curve_depth,
        radial_segs=52,
        name="scuba_mask_skirt",
        material="gear_dark",
    ).translate(hx, mask_y, visor_z_center)
    scene.add("scuba_mask", skirt_ring)

    def _wrap_visor(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        nx = p[:, 0] / max(1e-6, rx_out)
        p[:, 2] -= curve_depth * (nx * nx)
        nose = 0.007 * np.exp(-((p[:, 0] / 0.052) ** 2)) * np.clip(-p[:, 1] / max(1e-6, ry_in), 0.0, 1.0)
        p[:, 1] -= nose
        return p

    # 3. Thin curved translucent smooth-oval glass visor cleanly ahead of both eyes & eyelids.
    visor = (
        superellipsoid(rx_in + 0.005, ry_in + 0.005, 0.008, e1=1.0, e2=1.0, u_segs=36, v_segs=18, name="scuba_visor", material="glass_tinted")
        .deform(_wrap_visor)
        .translate(hx, mask_y, visor_z_center + 0.006)
    )
    scene.add("scuba_mask", visor)

    # 4. Wide snug dark rubber rear head strap hugging the exact soft-cube cranium contour (e2=0.76).
    rx_strap = hrx + 0.010
    rz_strap = hrz + 0.010
    strap_pts: List[Tuple[float, float, float]] = []
    n_arc = 28
    start_ang = math.radians(52.0)
    end_ang = math.radians(308.0)
    for i in range(n_arc + 1):
        ang = start_ang + (end_ang - start_ang) * (i / n_arc)
        sx = hx + rx_strap * _spow(math.sin(ang), 0.76)
        sz = hz + rz_strap * _spow(math.cos(ang), 0.76)
        rear_lift = 0.014 * (0.5 * (1.0 - math.cos(ang)))
        strap_pts.append((sx, mask_y + 0.005 + rear_lift, sz))

    strap = swept_tube(
        strap_pts,
        (0.038, 0.015),
        radial_segs=12,
        cap_start=True,
        cap_end=True,
        closed=False,
        cap_rings=3,
        name="scuba_strap",
        material="gear_dark",
    )
    scene.add("scuba_mask", strap)

    # Bold smooth yellow plastic temple strap anchor hinges connecting outer mask wall to rear strap.
    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        buckle = (
            rounded_box(0.038, 0.068, 0.070, bevel=0.012, segs=3, name=f"scuba_buckle_{tag}", material="gear_yellow_plastic")
            .rotate_xyz(ry=side_sign * 0.30)
            .translate(hx + side_sign * max(hrx, rx_out - 0.022), mask_y + 0.004, hz + hrz * 0.68)
        )
        scene.add("scuba_mask", buckle)

    # Rear strap pad + brushed-steel adjuster buckle flush on back of cranium.
    rear_pad = (
        rounded_box(0.096, 0.080, 0.028, bevel=0.009, segs=3, name="scuba_rear_pad", material="gear_dark")
        .translate(hx, mask_y + 0.022, hz - rz_strap - 0.004)
    )
    scene.add("scuba_mask", rear_pad)

    rear_clip = (
        rounded_box(0.048, 0.058, 0.032, bevel=0.007, segs=2, name="scuba_rear_clip", material="gear_steel")
        .translate(hx, mask_y + 0.022, hz - rz_strap - 0.007)
    )
    scene.add("scuba_mask", rear_clip)

    return (hx + max(hrx + 0.014, rx_out - 0.004), mask_y + 0.006, visor_z_center - curve_depth - 0.032)


def _build_snorkel(
    scene: SceneBuilder,
    head_center: Tuple[float, float, float],
    head_top: Tuple[float, float, float],
    clip_anchor: Tuple[float, float, float],
) -> Tuple[float, float, float]:
    """Build two-tone tactical banded vertical snorkel on ant's LEFT (+X) side and return its barrel tip."""
    hx, hy, hz = head_center
    cx, cy, cz = clip_anchor
    tube_x = cx + 0.028
    tube_z = cz + 0.008

    # Dark rubber mouthpiece seated near the lower-left mandible/cheek of the bigger 1998-style head.
    mouthpiece = (
        rounded_box(0.074, 0.040, 0.044, bevel=0.012, segs=3, name="snorkel_mouthpiece", material="gear_dark")
        .rotate_xyz(ry=-0.35)
        .translate(hx + 0.076, hy - 0.160, hz + 0.184)
    )
    scene.add("snorkel", mouthpiece)

    # Lower dark curved flex-hose sweeping from mouthpiece around left cheek into vertical tactical shaft.
    flex_pts = [
        (hx + 0.098, hy - 0.160, hz + 0.190),
        (hx + 0.182, hy - 0.156, hz + 0.182),
        (tube_x - 0.006, hy - 0.118, tube_z + 0.014),
        (tube_x, hy - 0.050, tube_z + 0.006),
    ]
    flex_hose = curved_tube(
        flex_pts,
        [0.021, 0.025, 0.028, 0.029],
        samples_per_seg=4,
        radial_segs=14,
        cap_rings=3,
        name="snorkel_flex_elbow",
        material="gear_dark",
    )
    scene.add("snorkel", flex_hose)

    # Lower dark ribbed tactical barrel section with horizontal corrugated ribs.
    lower_mid_y = cy + 0.036
    lower_barrel = (
        cylinder(0.031, 0.032, 0.228, radial_segs=18, bevel=0.005, axis="y", name="snorkel_lower_dark", material="gear_dark")
        .translate(tube_x, lower_mid_y, tube_z)
    )
    scene.add("snorkel", lower_barrel)

    for r_idx in range(6):
        ry = lower_mid_y - 0.088 + r_idx * 0.035
        rib = (
            torus_arc(0.033, 0.006, u_segs=18, v_segs=8, plane="xz", name=f"snorkel_dark_rib_{r_idx}", material="gear_dark")
            .translate(tube_x, ry, tube_z)
        )
        scene.add("snorkel", rib)

    # Snorkel keeper clamp locking the vertical tactical tube firmly onto the left mask bezel.
    keeper = (
        rounded_box(0.072, 0.048, 0.058, bevel=0.011, segs=3, name="snorkel_keeper_clamp", material="gear_dark")
        .translate(cx + 0.004, cy + 0.004, tube_z - 0.004)
    )
    scene.add("snorkel", keeper)

    # Metallic transition collar joining dark lower tube to weathered yellow upper barrel.
    collar_y = lower_mid_y + 0.118
    mid_collar = (
        cylinder(0.035, 0.036, 0.024, radial_segs=20, bevel=0.004, axis="y", name="snorkel_mid_collar", material="gear_gold_metal")
        .translate(tube_x, collar_y, tube_z)
    )
    scene.add("snorkel", mid_collar)

    # Upper smooth molded yellow plastic snorkel barrel rising cleanly above head_top.
    upper_base_y = collar_y + 0.010
    upper_top_y = head_top[1] + 0.175
    upper_h = max(0.220, upper_top_y - upper_base_y)
    upper_top_y = upper_base_y + upper_h
    upper_barrel = (
        cylinder(0.034, 0.035, upper_h, radial_segs=22, bevel=0.006, axis="y", name="snorkel_upper_gold", material="gear_yellow_plastic")
        .translate(tube_x, upper_base_y + 0.5 * upper_h, tube_z)
    )
    scene.add("snorkel", upper_barrel)

    # Horizontal slotted vent grooves (dark grooves framed by brass rings).
    for v_idx, frac in enumerate((0.28, 0.50, 0.72)):
        vy = upper_base_y + frac * upper_h
        slot = (
            rounded_box(0.072, 0.013, 0.036, bevel=0.003, segs=2, name=f"snorkel_vent_slot_{v_idx}", material="gear_dark")
            .translate(tube_x, vy, tube_z + 0.017)
        )
        scene.add("snorkel", slot)
        ring = (
            torus_arc(0.0355, 0.0045, u_segs=20, v_segs=8, plane="xz", name=f"snorkel_vent_ring_{v_idx}", material="gear_gold_metal")
            .translate(tube_x, vy + 0.012, tube_z)
        )
        scene.add("snorkel", ring)

    top_rim = (
        torus_arc(0.033, 0.0085, u_segs=22, v_segs=10, plane="xz", name="snorkel_top_rim", material="gear_gold_metal")
        .translate(tube_x, upper_top_y - 0.002, tube_z)
    )
    scene.add("snorkel", top_rim)

    top_bore = (
        cylinder(0.024, 0.023, 0.016, radial_segs=18, bevel=0.003, axis="y", name="snorkel_intake_bore", material="gear_dark")
        .translate(tube_x, upper_top_y - 0.001, tube_z)
    )
    scene.add("snorkel", top_bore)
    return (tube_x, upper_top_y, tube_z)


def _build_scuba_tank(
    scene: SceneBuilder,
    back_anchor: Tuple[float, float, float],
    thorax_center: Tuple[float, float, float],
) -> None:
    """Build centered brushed-steel scuba backpack tank with brass gauge, valve, and symmetric harness."""
    bx, by, bz = back_anchor
    _, ty, tz = thorax_center
    # Centered squarely on Swimmer's dorsal back (`x = 0.0`), clear above gaster & behind pitched head/strap.
    tank_x = 0.0
    tank_y = by + 0.068
    tank_z = bz - 0.104

    parts: List[MeshPart] = []

    tank_prof = [
        (0.000, -0.124),
        (0.032, -0.120),
        (0.052, -0.104),
        (0.054, -0.082),
        (0.054, +0.082),
        (0.046, +0.108),
        (0.025, +0.124),
        (0.017, +0.136),
        (0.000, +0.138),
    ]
    tank_body = lathe(tank_prof, radial_segs=24, cap_bottom=False, cap_top=False, axis="y", name="scuba_tank_body", material="gear_steel")
    parts.append(tank_body)

    boot = (
        cylinder(0.057, 0.056, 0.038, radial_segs=22, bevel=0.006, axis="y", name="scuba_tank_boot", material="gear_dark")
        .translate(0.0, -0.098, 0.0)
    )
    parts.append(boot)

    for b_idx, by_off in enumerate((-0.028, +0.050)):
        band = (
            cylinder(0.058, 0.058, 0.020, radial_segs=22, bevel=0.004, axis="y", name=f"scuba_tank_band_{b_idx}", material="gear_dark")
            .translate(0.0, by_off, 0.0)
        )
        parts.append(band)

    brass_collar = (
        torus_arc(0.055, 0.0058, u_segs=22, v_segs=8, plane="xz", name="scuba_tank_brass_ring", material="gear_gold_metal")
        .translate(0.0, +0.072, 0.0)
    )
    parts.append(brass_collar)

    valve_neck = (
        cylinder(0.017, 0.015, 0.040, radial_segs=16, bevel=0.003, axis="y", name="scuba_valve_neck", material="gear_steel")
        .translate(0.0, +0.150, 0.0)
    )
    parts.append(valve_neck)

    valve_block = (
        rounded_box(0.044, 0.028, 0.032, bevel=0.006, segs=2, name="scuba_valve_block", material="gear_dark")
        .translate(0.0, +0.172, 0.0)
    )
    parts.append(valve_block)

    valve_wheel = (
        cylinder(0.021, 0.021, 0.015, radial_segs=16, bevel=0.004, axis="y", name="scuba_valve_wheel", material="gear_dark")
        .translate(0.0, +0.194, 0.0)
    )
    parts.append(valve_wheel)

    side_knob = (
        cylinder(0.013, 0.013, 0.026, radial_segs=14, bevel=0.003, axis="x", name="scuba_valve_knob", material="gear_steel")
        .translate(-0.026, +0.172, 0.0)
    )
    parts.append(side_knob)

    gauge = (
        cylinder(0.016, 0.016, 0.013, radial_segs=16, bevel=0.003, axis="z", name="scuba_pressure_gauge", material="gear_gold_metal")
        .translate(+0.025, +0.170, -0.020)
    )
    parts.append(gauge)

    for part in parts:
        placed = (
            part.rotate_xyz(rx=-22.0, degrees=True)
            .translate(tank_x, tank_y, tank_z)
        )
        scene.add("scuba_tank", placed)

    # Centered dark rubber backpack mounting plate bridging dorsal carapace cleanly into the tank.
    bracket = (
        rounded_box(0.118, 0.132, 0.056, bevel=0.012, segs=3, name="scuba_tank_bracket", material="gear_dark")
        .rotate_xyz(rx=-22.0, degrees=True)
        .translate(bx, tank_y - 0.004, bz - 0.040)
    )
    scene.add("scuba_tank", bracket)

    # Symmetric dark rubber backpack straps sweeping over both shoulders and down Swimmer's petite chest.
    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        h_pts = [
            (side_sign * 0.048, tank_y + 0.032, bz - 0.034),
            (side_sign * 0.066, ty + 0.106, tz - 0.024),
            (side_sign * 0.064, ty + 0.092, tz + 0.050),
            (side_sign * 0.058, ty + 0.022, tz + 0.094),
            (side_sign * 0.052, ty - 0.026, tz + 0.084),
        ]
        h_strap = curved_tube(
            h_pts,
            (0.009, 0.014),
            samples_per_seg=4,
            radial_segs=8,
            cap_rings=2,
            name=f"scuba_harness_{tag}",
            material="gear_dark",
        )
        scene.add("scuba_tank", h_strap)

    # Lower waist/chest cross-belt tying the centered backpack harness snugly around Swimmer's thorax.
    chest_belt = curved_tube(
        [
            (-0.070, ty - 0.020, tz + 0.048),
            (-0.044, ty - 0.020, tz + 0.094),
            (0.000, ty - 0.020, tz + 0.102),
            (0.044, ty - 0.020, tz + 0.094),
            (0.070, ty - 0.020, tz + 0.048),
        ],
        (0.011, 0.008),
        samples_per_seg=4,
        radial_segs=8,
        cap_rings=2,
        name="scuba_harness_belt",
        material="gear_dark",
    )
    scene.add("scuba_tank", chest_belt)


def _make_diving_flipper(name: str, length: float = 0.155, blade_w: float = 0.084) -> MeshPart:
    """Build one sculpted dark rubber diving flipper whose flat sole rests exactly at Y = 0.0."""
    def _sculpt_fin(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        u = np.clip((p[:, 2] + 0.5 * length) / length, 0.0, 1.0)
        flare = 0.42 + 0.58 * (u ** 0.85)
        p[:, 0] *= flare

        nx = np.abs(p[:, 0]) / np.maximum(1e-4, 0.5 * blade_w * flare)
        top_mask = (p[:, 1] > 0.0).astype(np.float64)
        rail = np.exp(-((nx - 0.88) / 0.16) ** 2) * np.clip((u - 0.18) / 0.30, 0.0, 1.0)
        center_rib = np.exp(-((nx / 0.18) ** 2)) * np.clip((u - 0.20) / 0.25, 0.0, 1.0)
        heel_pocket = np.exp(-((u / 0.34) ** 2))
        p[:, 1] += top_mask * (0.007 * rail + 0.005 * center_rib + 0.024 * heel_pocket)

        p[:, 1] = np.where(p[:, 1] > 0.0, p[:, 1] * (1.0 - 0.32 * u), -0.009)
        return p

    fin = rounded_box(blade_w, 0.020, length, bevel=0.006, segs=4, name=name, material="gear_dark").deform(_sculpt_fin)
    bmin, _ = fin.bounds()
    return fin.translate(0.0, -float(bmin[1]), 0.34 * length)


def _build_flippers(scene: SceneBuilder, anchors: Dict[str, Any]) -> None:
    """Attach 4 forward-facing frogman diving flippers individually onto all 4 feet at Y = 0.0."""
    mid_l = anchors.get("foot_mid_left", (0.152, 0.0, 0.094))
    mid_r = anchors.get("foot_mid_right", (-mid_l[0], 0.0, mid_l[2]))
    rear_l = anchors.get("foot_rear_left", (0.160, 0.0, -0.028))
    rear_r = anchors.get("foot_rear_right", (-rear_l[0], 0.0, rear_l[2]))

    # All 4 flippers face FORWARD (+Z), splayed naturally outward so mid & rear blades stay cleanly separated.
    mid_yaw = 0.24
    rear_yaw = 0.72

    for tag, foot_pos, length, blade_w, yaw_signed, boot_z_off in (
        ("rear_left", rear_l, 0.208, 0.110, +rear_yaw, +0.018),
        ("rear_right", rear_r, 0.208, 0.110, -rear_yaw, +0.018),
        ("mid_left", mid_l, 0.214, 0.116, +mid_yaw, +0.018),
        ("mid_right", mid_r, 0.214, 0.116, -mid_yaw, +0.018),
    ):
        fx, _, fz = foot_pos
        fin = _make_diving_flipper(f"flipper_{tag}", length=length, blade_w=blade_w)
        scene.add("flippers", fin.rotate_xyz(ry=yaw_signed).translate(fx, 0.0, fz))

        # Snug dark rubber boot pocket encasing the curved digitigrade tarsus cleanly above Y = 0.0.
        boot_vamp = (
            superellipsoid(0.028, 0.020, 0.052, e1=0.72, e2=0.76, u_segs=14, v_segs=10, name=f"flipper_boot_{tag}", material="gear_dark")
            .translate(0.0, 0.021, boot_z_off)
            .rotate_xyz(ry=yaw_signed)
            .translate(fx, 0.0, fz)
        )
        collar = (
            cylinder(0.030, 0.025, 0.040, radial_segs=14, bevel=0.005, axis="y", name=f"flipper_collar_{tag}", material="gear_dark")
            .translate(fx, 0.026, fz)
        )
        scene.add("flippers", boot_vamp)
        scene.add("flippers", collar)


def _subdivided_slab(
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
    """Build a watertight finely-subdivided rounded slab for smooth wide U-shaped sandbox scoop shaping."""
    nx, ny, nz = max(2, int(nx)), max(2, int(ny)), max(2, int(nz))
    hx, hy, hz = 0.5 * float(sx), 0.5 * float(sy), 0.5 * float(sz)
    r = min(max(1e-4, float(bevel)), 0.45 * min(float(sx), float(sy), float(sz)))
    core = np.array([hx - r, hy - r, hz - r], dtype=np.float64)

    vert_map: Dict[Tuple[int, int, int], int] = {}
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
    for iz, flip in ((nz, False), (0, True)):
        for j in range(ny):
            for i in range(nx):
                a, b, c, d = _vid(i, j, iz), _vid(i + 1, j, iz), _vid(i + 1, j + 1, iz), _vid(i, j + 1, iz)
                tris.extend([(a, c, b), (a, d, c)] if flip else [(a, b, c), (a, c, d)])
    for ix, flip in ((nx, False), (0, True)):
        for k in range(nz):
            for j in range(ny):
                a, b, c, d = _vid(ix, j, k), _vid(ix, j + 1, k), _vid(ix, j + 1, k + 1), _vid(ix, j, k + 1)
                tris.extend([(a, b, c), (a, c, d)] if flip else [(a, c, b), (a, d, c)])
    for iy, flip in ((ny, False), (0, True)):
        for k in range(nz):
            for i in range(nx):
                a, b, c, d = _vid(i, iy, k), _vid(i + 1, iy, k), _vid(i + 1, iy, k + 1), _vid(i, iy, k + 1)
                tris.extend([(a, b, c), (a, c, d)] if flip else [(a, c, b), (a, d, c)])

    g = np.asarray(grid_pts, dtype=np.float64)
    q = np.column_stack([g[:, 0] * hx, g[:, 1] * hy, g[:, 2] * hz])
    clamped = np.clip(q, -core, core)
    diff = q - clamped
    dnorm = np.linalg.norm(diff, axis=1, keepdims=True)
    pos = np.where(dnorm > 1e-10, clamped + r * diff / np.maximum(1e-10, dnorm), q).astype(np.float32)
    idx_arr = ensure_outward_winding(pos, np.asarray(tris, dtype=np.uint32).reshape(-1))
    return MeshPart(name, material, pos, recompute_smooth_normals(pos, idx_arr), idx_arr)


def _build_shovel(
    scene: SceneBuilder,
    right_hand: Tuple[float, float, float],
    right_hand_dir: Tuple[float, float, float],
) -> None:
    """Build Swimmer's short-handled wide flat-front square yellow plastic beach shovel.

    Classic one-piece molded `gear_yellow_plastic` sandbox shovel:
    - Short compact handle with straight-back D-handle arch at `-Z`
    - Wide flat-front square U-shaped scoop blade (`blade_w = 0.252`, zero pointed tip!) with rounded
      rear shoulders, flat tray floor, and crisp raised U-rim walls along back & parallel sides.
    - Carried at a low sporty bowl-up tilt (`rz = 26 deg`) so the wide flat-front scoop reads broadly
      in `301 S`, `201 SE`, and `901 E` while staying cleanly clear of legs and diving flippers.
    """
    hx, hy, hz = right_hand
    dx, dy, dz = right_hand_dir
    grip_x = hx + 0.020 * dx
    grip_y = hy + 0.020 * dy
    grip_z = hz + 0.020 * dz
    parts: List[MeshPart] = []

    # 1. Classic plastic D-handle at rear (-Z): straight flat grip bar at back + smooth rounded D-arch into shaft.
    d_front_z = -0.116
    d_back_z = -0.214
    d_half_y = 0.048
    d_loop_pts = [
        (0.0, +0.014, d_front_z),
        (0.0, +d_half_y * 0.72, d_front_z - 0.024),
        (0.0, +d_half_y, d_front_z - 0.062),
        (0.0, +d_half_y, d_back_z + 0.008),
        (0.0, +d_half_y * 0.65, d_back_z),
        (0.0, -d_half_y * 0.65, d_back_z),
        (0.0, -d_half_y, d_back_z + 0.008),
        (0.0, -d_half_y, d_front_z - 0.062),
        (0.0, -d_half_y * 0.72, d_front_z - 0.024),
        (0.0, -0.014, d_front_z),
    ]
    d_loop = curved_tube(
        d_loop_pts,
        (0.0145, 0.0145),
        samples_per_seg=3,
        radial_segs=12,
        closed=True,
        name="shovel_d_loop",
        material="gear_yellow_plastic",
    )
    parts.append(d_loop)

    d_boss = (
        cylinder(0.021, 0.019, 0.026, radial_segs=16, bevel=0.005, axis="z", name="shovel_d_boss", material="gear_yellow_plastic")
        .translate(0.0, 0.0, d_front_z + 0.010)
    )
    parts.append(d_boss)

    # 2. Short compact molded yellow plastic shaft gripped right through local (0, 0, 0).
    shaft_back_z = d_front_z + 0.016
    shaft_front_z = +0.088
    shaft = (
        cylinder(0.0180, 0.0190, shaft_front_z - shaft_back_z, radial_segs=18, bevel=0.004, axis="z", name="shovel_shaft", material="gear_yellow_plastic")
        .translate(0.0, 0.0, 0.5 * (shaft_back_z + shaft_front_z))
    )
    parts.append(shaft)

    neck_back_z = shaft_front_z - 0.010
    neck_front_z = shaft_front_z + 0.044
    neck = (
        cylinder(0.019, 0.030, neck_front_z - neck_back_z, radial_segs=18, bevel=0.005, axis="z", name="shovel_neck", material="gear_yellow_plastic")
        .scale(1.22, 0.78, 1.0)
        .translate(0.0, 0.004, 0.5 * (neck_back_z + neck_front_z))
        .rotate_xyz(rz=-90.0, degrees=True)
    )
    parts.append(neck)

    # 3. Wide flat-front square U-shaped sandbox scoop blade (`blade_w = 0.252`, ZERO pointed tip!),
    # rotated -90 deg around the shaft (+Z) so the flat blade width is 100% coplanar / parallel with
    # the back D-handle crossbar in YZ (scoop bowl facing +X inward toward Swimmer, like a real shovel!).
    blade_w = 0.252
    blade_l = 0.246
    blade_start_z = neck_front_z - 0.016
    blade_cz = blade_start_z + 0.5 * blade_l

    def _sculpt_square_scoop(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        # u = 0.0 at rounded rear heel shoulders -> u = 1.0 at wide, straight, flat-front digging edge.
        u = np.clip((p[:, 2] + 0.5 * blade_l) / blade_l, 0.0, 1.0)

        # Rounded rear heel shoulders flaring quickly to FULL wide straight parallel walls (subtle +4% front flare).
        t_rear = np.clip((0.30 - u) / 0.30, 0.0, 1.0)
        rear_curve = np.sqrt(np.clip(1.0 - 0.76 * (t_rear ** 2.2), 0.16, 1.0))
        front_flare = 1.0 + 0.04 * np.clip((u - 0.30) / 0.70, 0.0, 1.0)
        # Gentle tiny corner bevel only at the very front corners (keeps front edge wide, straight, and flat!).
        t_corner = np.clip((u - 0.90) / 0.10, 0.0, 1.0)
        corner_soft = 1.0 - 0.06 * (t_corner ** 2.0)
        p[:, 0] *= rear_curve * front_flare * corner_soft

        half_w_local = np.maximum(1e-4, 0.5 * blade_w * rear_curve * front_flare * corner_soft)
        nx = np.abs(p[:, 0]) / half_w_local

        # Flat tray floor across the wide center (|nx| < 0.74), curving sharply UP into crisp molded U-rim walls
        # along both parallel sides AND around the rounded rear heel, tapering smoothly open at the flat front edge!
        side_wall = np.clip((nx - 0.72) / 0.28, 0.0, 1.0) ** 1.65
        rear_wall = np.clip((0.24 - u) / 0.24, 0.0, 1.0) ** 1.75
        front_open = 1.0 - 0.85 * (np.clip((u - 0.84) / 0.16, 0.0, 1.0) ** 1.5)
        u_wall = np.maximum(side_wall * front_open, rear_wall)

        # Subtle flat tray floor camber + tall crisp 34 mm molded U-side walls.
        wall_lift = 0.036 * u_wall
        floor_camber = 0.004 * (nx * nx) * (1.0 - 0.5 * u)
        thick_scale = 1.0 - 0.30 * (u ** 1.2)
        p[:, 1] = p[:, 1] * thick_scale + wall_lift + floor_camber - 0.014
        return p

    blade_core = (
        _subdivided_slab(blade_w, 0.018, blade_l, bevel=0.005, nx=22, ny=4, nz=20, name="shovel_blade", material="gear_yellow_plastic")
        .deform(_sculpt_square_scoop)
        .translate(0.0, 0.0, blade_cz)
        .rotate_xyz(rz=-90.0, degrees=True)
    )
    parts.append(blade_core)

    # Molded central rear strengthening rib flaring cleanly from shaft neck into the rear heel (coplanar!).
    blade_spine = (
        lathe(
            [(0.023, 0.000), (0.017, 0.028), (0.010, 0.058), (0.000, 0.080)],
            radial_segs=14,
            cap_bottom=True,
            cap_top=False,
            axis="z",
            name="shovel_spine",
            material="gear_yellow_plastic",
        )
        .scale(1.15, 0.58, 1.0)
        .translate(0.0, 0.004, blade_start_z + 0.006)
        .rotate_xyz(rz=-90.0, degrees=True)
    )
    parts.append(blade_spine)

    # Coplanar beach shovel carry (`rz = +32 deg` tilts the YZ-coplanar handle + scoop blade cleanly
    # inward & upward toward Swimmer's body, matching asst301/201/901 while clearing legs, flippers & ground!).
    for part in parts:
        placed = part.rotate_xyz(rx=-2.0, ry=-8.0, rz=+32.0, degrees=True)
        scene.add("shovel", placed)
    scene.set_node_transform("shovel", translation=(grip_x, grip_y, grip_z))


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


def _trim_swimmer_crown(scene: SceneBuilder, anchors: Dict[str, Any]) -> Optional[MeshPart]:
    """Apply a smooth C^3 superelliptic crown trim to Swimmer's cranium matching `asst301`/`asst901`.

    Compresses the excess upper forehead dome cleanly down so the green skull top sits compact and
    sleek right behind/level with the top rim of the yellow oval scuba mask, while leaving the lower
    cranium, eye sockets, tucked blink=0 eyelids, rubber mask skirt contact, and rear strap untouched.
    """
    if "head" not in scene.nodes:
        return None
    ey_mid = 0.5 * (float(anchors["eye_left"][1]) + float(anchors["eye_right"][1]))
    crown_start_y = ey_mid + 0.028
    crown_cap_h = 0.106
    crown_p = 2.6

    def _flatten_crown(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        dy = p[:, 1] - crown_start_y
        above = dy > 0.0
        if np.any(above):
            s = dy[above] / crown_cap_h
            p[above, 1] = crown_start_y + crown_cap_h * s / ((1.0 + s ** crown_p) ** (1.0 / crown_p))
        return p

    cranium_part: Optional[MeshPart] = None
    rebuilt_head: List[MeshPart] = []
    for part in scene.nodes["head"]:
        if part.name == "cranium":
            trimmed = part.deform(_flatten_crown)
            cranium_part = trimmed
            rebuilt_head.append(trimmed)
        else:
            rebuilt_head.append(part)
    scene.nodes["head"] = rebuilt_head
    return cranium_part


def _build_clubbed_antennae(
    scene: SceneBuilder,
    anchors: Dict[str, Any],
    cranium: Optional[MeshPart] = None,
) -> None:
    """Replace base thin antennae with chunky bulbous-clubbed Swimmer antennae seated flush on the trimmed crown."""
    head_xf = scene.get_node_transform("head")
    neck_pivot = (
        (-head_xf.translation[0], -head_xf.translation[1], -head_xf.translation[2])
        if head_xf is not None
        else anchors["neck_center"]
    )
    # Sleek tapered stem swelling boldly into ~1.9x bigger bulbous rounded club tips (matching asst301).
    club_rads = [
        (0.018, 0.018),
        (0.015, 0.015),
        (0.013, 0.013),
        (0.015, 0.015),
        (0.024, 0.025),
        (0.032, 0.033),
        (0.022, 0.023),
    ]
    for side, tag, orig_key in ((+1.0, "left", "antenna_base_left"), (-1.0, "right", "antenna_base_right")):
        node_name = f"antenna_{tag}"
        orig_bx, orig_by, orig_bz = anchors[orig_key]
        bx = orig_bx
        bz = orig_bz - 0.008
        surf_y = (
            _cranium_top_y_at(cranium, bx, bz, orig_by - 0.048)
            if cranium is not None
            else orig_by
        )
        by = surf_y - 0.002
        base_p = (bx, by, bz)
        # Keep +X (left) antenna arching cleanly inboard/upward away from the tall +X yellow snorkel!
        out_x = 0.018 if side > 0 else 0.034
        h_off = 0.008 if side > 0 else -0.006
        pts = [
            (bx - side * 0.004, by - 0.018, bz - 0.004),
            base_p,
            (bx + side * (0.40 * out_x), by + 0.072 + 0.4 * h_off, bz + 0.016),
            (bx + side * (0.75 * out_x), by + 0.132 + 0.8 * h_off, bz + 0.042),
            (bx + side * (0.95 * out_x), by + 0.152 + h_off, bz + 0.078),
            (bx + side * (1.05 * out_x), by + 0.136 + h_off, bz + 0.110),
            (bx + side * (1.08 * out_x), by + 0.116 + h_off, bz + 0.124),
        ]
        stalk = swept_tube(
            pts,
            club_rads,
            radial_segs=14,
            smooth_subdiv=3,
            cap_rings=3,
            name=f"antenna_stalk_{tag}",
            material="chitin_dark",
        )
        sock = (
            ellipsoid(0.022, 0.011, 0.022, u_segs=12, v_segs=8, name=f"antenna_socket_{tag}", material="chitin_dark")
            .rotate_xyz(rx=0.06, rz=-side * 0.12)
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


def _add_swimmer_idle(scene: SceneBuilder) -> AnimationClip:
    """Attach Swimmer's calm, relaxed 1.80s aquatic breathing sway + gentle shovel rock Idle clip."""
    dur = 1.80
    n_steps = 16
    times = [round(dur * k / n_steps, 5) for k in range(n_steps + 1)]

    def _rest_t(node: str) -> Tuple[float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.translation if xf is not None else (0.0, 0.0, 0.0)

    def _rest_r(node: str) -> Tuple[float, float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)

    channels: List[AnimationChannel] = []

    bx, by, bz = _rest_t("upper_body")
    body_t = []
    for k in range(n_steps + 1):
        ph = 2.0 * math.pi * (k / n_steps)
        dip = 0.5 * (1.0 - math.cos(ph))
        sway = math.sin(ph)
        body_t.append((bx + 0.008 * sway, by - 0.011 * dip, bz + 0.002 * dip))
    body_t[-1] = body_t[0]
    channels.append(AnimationChannel("upper_body", "translation", list(times), body_t))

    r0_b = _rest_r("upper_body")
    body_r = [
        quat_mul(
            r0_b,
            quat_from_euler_xyz(
                rx=0.014 * math.sin(2.0 * math.pi * (k / n_steps)),
                rz=0.036 * math.sin(2.0 * math.pi * (k / n_steps)),
            ),
        )
        for k in range(n_steps + 1)
    ]
    body_r[-1] = body_r[0]
    channels.append(AnimationChannel("upper_body", "rotation", list(times), body_r))

    r0_h = _rest_r("head_group")
    head_r = [
        quat_mul(
            r0_h,
            quat_from_euler_xyz(
                rx=0.016 * math.sin(2.0 * math.pi * (k / n_steps) - 0.25),
                ry=0.032 * math.sin(2.0 * math.pi * (k / n_steps)),
                rz=0.058 * math.sin(2.0 * math.pi * (k / n_steps) - 0.20),
            ),
        )
        for k in range(n_steps + 1)
    ]
    head_r[-1] = head_r[0]
    channels.append(AnimationChannel("head_group", "rotation", list(times), head_r))

    if "shovel" in scene.nodes and scene.get_node("shovel"):
        r0_sh = _rest_r("shovel")
        sh_r = [
            quat_mul(
                r0_sh,
                quat_from_euler_xyz(
                    rx=0.035 * math.sin(2.0 * math.pi * (k / n_steps) - 0.35),
                    rz=0.022 * math.sin(2.0 * math.pi * (k / n_steps) - 0.20),
                ),
            )
            for k in range(n_steps + 1)
        ]
        sh_r[-1] = sh_r[0]
        channels.append(AnimationChannel("shovel", "rotation", list(times), sh_r))

    if "gaster_group" in scene.nodes:
        r0_g = _rest_r("gaster_group")
        g_r = [
            quat_mul(r0_g, quat_from_euler_xyz(rx=-0.024 * math.sin(2.0 * math.pi * (k / n_steps))))
            for k in range(n_steps + 1)
        ]
        g_r[-1] = g_r[0]
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), g_r))

    for node_name, side, poff in (("antenna_left", 1.0, 0.0), ("antenna_right", -1.0, 0.7)):
        if node_name in scene.nodes and scene.get_node(node_name):
            r0_a = _rest_r(node_name)
            a_r = [
                quat_mul(
                    r0_a,
                    quat_from_euler_xyz(
                        rx=0.044 * math.sin(2.0 * math.pi * (k / n_steps) + poff),
                        rz=side * 0.030 * math.sin(2.0 * math.pi * (k / n_steps) - 0.3),
                    ),
                )
                for k in range(n_steps + 1)
            ]
            a_r[-1] = a_r[0]
            channels.append(AnimationChannel(node_name, "rotation", list(times), a_r))

    if "mandibles" in scene.nodes and scene.get_node("mandibles"):
        r0_m = _rest_r("mandibles")
        m_r = [
            quat_mul(r0_m, quat_from_euler_xyz(rx=0.018 * 0.5 * (1.0 - math.cos(2.0 * math.pi * (k / n_steps)))))
            for k in range(n_steps + 1)
        ]
        m_r[-1] = m_r[0]
        channels.append(AnimationChannel("mandibles", "rotation", list(times), m_r))

    clip = scene.add_animation("Idle", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Idle")
    sync_arm_idle_to_clip(scene, "Idle", left_scale=1.0, right_scale=0.55)
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


def _build_snorkel_water_fx(
    scene: SceneBuilder,
    snorkel_tip: Tuple[float, float, float],
    head_translation: Tuple[float, float, float],
) -> Tuple[float, float, float]:
    """Attach 3 hidden rest-scale (`1e-4`) glossy aqua water-cannon FX nodes inside the snorkel tip."""
    tx, ty, tz = snorkel_tip
    hx, hy, hz = head_translation
    # Tuck rest pivot 18 mm inside the opaque yellow snorkel barrel so Idle/rest bounds & look stay 100% untouched.
    local_origin = (tx + hx, ty - 0.018 + hy, tz + hz)
    tiny = (1e-4, 1e-4, 1e-4)

    glob_parts = [
        ellipsoid(0.048, 0.056, 0.048, u_segs=14, v_segs=10, name="water_glob_outer", material="glass_tinted"),
        ellipsoid(0.028, 0.036, 0.028, u_segs=12, v_segs=8, name="water_glob_core", material="glass_clear"),
    ]
    spray_a_parts: List[MeshPart] = [
        ellipsoid(0.058, 0.048, 0.058, u_segs=14, v_segs=10, name="water_spray_a_hub", material="glass_tinted"),
    ]
    for k in range(5):
        ang = 2.0 * math.pi * k / 5.0 + 0.25
        ca, sa = math.cos(ang), math.sin(ang)
        lobe = (
            ellipsoid(0.038, 0.062, 0.034, u_segs=12, v_segs=8, name=f"water_spray_a_lobe_{k}", material="glass_tinted")
            .rotate_xyz(rx=0.58 * sa, rz=-0.58 * ca)
            .translate(0.064 * ca, 0.024, 0.064 * sa)
        )
        spray_a_parts.append(lobe)
    spray_a_parts.append(
        ellipsoid(0.030, 0.046, 0.030, u_segs=10, v_segs=8, name="water_spray_a_foam", material="glass_clear").translate(0.0, 0.048, 0.0)
    )

    spray_b_parts: List[MeshPart] = []
    drops = (
        (+0.115, +0.020, +0.042, 0.034, "glass_tinted"),
        (-0.118, +0.032, +0.038, 0.032, "glass_tinted"),
        (+0.052, +0.056, +0.112, 0.030, "glass_tinted"),
        (-0.048, +0.048, -0.108, 0.032, "glass_tinted"),
        (+0.132, +0.072, -0.056, 0.026, "glass_tinted"),
        (-0.124, +0.064, -0.062, 0.028, "glass_tinted"),
        (+0.068, +0.095, -0.028, 0.022, "glass_clear"),
        (-0.062, +0.088, +0.048, 0.022, "glass_clear"),
    )
    for idx, (dx, dy, dz, rad, mat) in enumerate(drops):
        spray_b_parts.append(
            ellipsoid(rad, rad * 1.25, rad, u_segs=10, v_segs=8, name=f"water_drop_{idx}", material=mat).translate(dx, dy, dz)
        )

    for node_name, parts in (
        ("snorkel_water_glob", glob_parts),
        ("snorkel_water_spray_a", spray_a_parts),
        ("snorkel_water_spray_b", spray_b_parts),
    ):
        for p in parts:
            scene.add(node_name, p, parent="head_group")
        scene.set_node_transform(node_name, translation=local_origin, scale=tiny)
    return local_origin


def _add_swimmer_attack(scene: SceneBuilder, water_origin: Tuple[float, float, float]) -> AnimationClip:
    """Attach Swimmer's authentic 1998 0.76s snorkel water-cannon blast (`asat`) Attack clip."""
    # Exact 1998 asat 10-frame cadence [60, 60, 60, 80, 80, 80, 100, 100, 80, 60] ms + loop wrap at 0.76 s.
    times = [0.00, 0.06, 0.12, 0.18, 0.26, 0.34, 0.42, 0.52, 0.62, 0.70, 0.76]

    def _rest_t(node: str) -> Tuple[float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.translation if xf is not None else (0.0, 0.0, 0.0)

    def _rest_r(node: str) -> Tuple[float, float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)

    channels: List[AnimationChannel] = []
    bx, by, bz = _rest_t("upper_body")
    body_t_deltas = [
        (0.0, 0.000, 0.000),   # f0: upright rest
        (0.0, -0.020, +0.018), # f1: fast duck forward
        (0.0, -0.036, +0.026), # f2: snorkel horizontal cannon aim
        (0.0, -0.040, +0.022), # f3: brace & charge
        (0.0, -0.042, +0.020), # f4: water glob swells
        (0.0, -0.044, +0.018), # f5: full water ball at nozzle
        (0.0, -0.032, +0.000), # f6: water cannon blast + recoil
        (0.0, -0.034, +0.008), # f7: wide splash burst
        (0.0, -0.018, +0.010), # f8: droplets scatter, rising
        (0.0, -0.006, +0.004), # f9: snapping upright
        (0.0, 0.000, 0.000),   # loop wrap
    ]
    channels.append(
        AnimationChannel("upper_body", "translation", list(times), [(bx + dx, by + dy, bz + dz) for dx, dy, dz in body_t_deltas])
    )
    body_rx = [0.00, 0.44, 0.72, 0.75, 0.75, 0.76, 0.66, 0.70, 0.42, 0.14, 0.00]
    channels.append(
        AnimationChannel("upper_body", "rotation", list(times), _quat_track(_rest_r("upper_body"), [(rx, 0.0, 0.0) for rx in body_rx]))
    )
    # Head bends +1.10 rad on top of body +0.72 rad (+1.82 rad total, cancelling -15.5 deg rest pitch -> horizontal +Z!).
    head_rx = [0.00, 0.58, 1.10, 1.12, 1.12, 1.14, 1.00, 1.06, 0.58, 0.18, 0.00]
    channels.append(
        AnimationChannel("head_group", "rotation", list(times), _quat_track(_rest_r("head_group"), [(rx, 0.0, 0.0) for rx in head_rx]))
    )
    # Counter-rotate right arm so the handheld yellow beach shovel stays cleanly horizontal & above flippers!
    if "arm_right" in scene.nodes:
        channels.append(
            AnimationChannel(
                "arm_right",
                "rotation",
                list(times),
                _quat_track(_rest_r("arm_right"), [(-1.08 * rx, 0.0, -0.14 * rx) for rx in body_rx]),
            )
        )
    if "arm_left" in scene.nodes:
        channels.append(
            AnimationChannel(
                "arm_left",
                "rotation",
                list(times),
                _quat_track(_rest_r("arm_left"), [(-0.55 * rx, -0.35 * rx, +0.42 * rx) for rx in body_rx]),
            )
        )
    if "gaster_group" in scene.nodes:
        channels.append(
            AnimationChannel(
                "gaster_group",
                "rotation",
                list(times),
                _quat_track(_rest_r("gaster_group"), [(-0.48 * rx, 0.0, 0.0) for rx in body_rx]),
            )
        )
    for node_name, side in (("antenna_left", +1.0), ("antenna_right", -1.0)):
        if node_name in scene.nodes and scene.get_node(node_name):
            a_deltas = [
                (0.0, 0.0, 0.0),
                (-0.18, 0.0, 0.0),
                (-0.30, 0.0, side * 0.06),
                (-0.24, 0.0, side * 0.05),
                (-0.20, 0.0, side * 0.04),
                (-0.22, 0.0, side * 0.05),
                (+0.22, 0.0, -side * 0.08),
                (+0.14, 0.0, -side * 0.04),
                (-0.10, 0.0, 0.0),
                (+0.08, 0.0, 0.0),
                (0.0, 0.0, 0.0),
            ]
            channels.append(AnimationChannel(node_name, "rotation", list(times), _quat_track(_rest_r(node_name), a_deltas)))

    # Animated water-cannon FX traveling straight out along head_group +Y (snorkel barrel axis -> world +Z!).
    wx, wy, wz = water_origin
    tiny = (1e-4, 1e-4, 1e-4)
    glob_dy = [0.0, 0.0, 0.0, 0.0, 0.058, 0.092, 0.170, 0.235, 0.0, 0.0, 0.0]
    glob_sc = [
        tiny, tiny, tiny, tiny,
        (0.75, 0.80, 0.75), (1.15, 1.18, 1.15), (0.95, 1.85, 0.95), (0.28, 0.36, 0.28),
        tiny, tiny, tiny,
    ]
    spray_a_dy = [0.0, 0.0, 0.0, 0.0, 0.0, 0.04, 0.230, 0.325, 0.385, 0.0, 0.0]
    spray_a_sc = [
        tiny, tiny, tiny, tiny, tiny, tiny,
        (1.18, 1.12, 1.18), (1.44, 1.18, 1.44), (0.30, 0.30, 0.30), tiny, tiny,
    ]
    spray_b_dy = [0.0, 0.0, 0.0, 0.0, 0.0, 0.04, 0.275, 0.415, 0.485, 0.0, 0.0]
    spray_b_sc = [
        tiny, tiny, tiny, tiny, tiny, tiny,
        (0.86, 0.86, 0.86), (1.52, 1.32, 1.52), (0.56, 0.56, 0.56), tiny, tiny,
    ]
    for node_name, dys, scs in (
        ("snorkel_water_glob", glob_dy, glob_sc),
        ("snorkel_water_spray_a", spray_a_dy, spray_a_sc),
        ("snorkel_water_spray_b", spray_b_dy, spray_b_sc),
    ):
        channels.append(AnimationChannel(node_name, "translation", list(times), [(wx, wy + dy, wz) for dy in dys]))
        channels.append(AnimationChannel(node_name, "scale", list(times), list(scs)))

    clip = scene.add_animation("Attack", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Attack", knee_bow=1.1, splay=0.30, swivel=0.25)
    return clip


def build_swimmer(blink: float = 0.0) -> SceneBuilder:
    """Build the sculpted 3D Swimmer Ant SceneBuilder with Blink, 1.80s Idle, and 0.76s Water-Cannon Attack."""
    scene, anchors = build_base_ant(
        scene_name="ant_swimmer",
        include_antennae=True,
        include_eyes=True,
        include_brows=False,
        brow_style="wonder",
        head_tilt_deg=4.0,
        head_yaw_deg=-2.5,
        head_pitch_deg=-15.5,
        eye_look=(0.04, 0.10),
        antenna_asymmetry=0.30,
        stance_asymmetry=0.25,
        leg_style="swimmer",
        include_idle=False,
        left_arm_pose="idle",
        right_arm_pose=_SWIMMER_RIGHT_ARM,
        blink=blink,
    )
    closed_angle = float(anchors.get("eyelid_closed_angle", 1.80))
    trimmed_cranium = _trim_swimmer_crown(scene, anchors)
    _build_clubbed_antennae(scene, anchors, cranium=trimmed_cranium)
    clip_anchor = _build_scuba_mask(scene, anchors["head_center"], anchors["head_radii"], anchors["eye_left"], closed_angle)
    snorkel_tip = _build_snorkel(scene, anchors["head_center"], anchors["head_top"], clip_anchor)
    _build_scuba_tank(scene, anchors["back_anchor"], anchors["thorax_center"])
    _build_flippers(scene, anchors)
    _build_shovel(scene, anchors["right_hand"], anchors["right_hand_dir"])

    head_xf = scene.get_node_transform("head")
    head_t = head_xf.translation if head_xf is not None else (0.0, 0.0, 0.0)
    for h_node in ("scuba_mask", "snorkel"):
        scene.attach_world_node(h_node, "head_group", preserve_world_pose=False)
        if head_xf is not None:
            scene.set_node_transform(h_node, translation=head_xf.translation)
    water_origin = _build_snorkel_water_fx(scene, snorkel_tip, head_t)
    scene.attach_world_node("scuba_tank", "upper_body", preserve_world_pose=False)
    # Parent shovel directly onto forearm_right (pivoted at elbow_right) so the grip rides lockstep
    # inside Swimmer's right pincer claws at rest and across every articulated Idle & Attack frame.
    scene.set_node_parent("shovel", "forearm_right")
    sh_xf = scene.get_node_transform("shovel")
    er = anchors["elbow_right"]
    if sh_xf is not None:
        gx, gy, gz = sh_xf.translation
        scene.set_node_transform("shovel", translation=(gx - er[0], gy - er[1], gz - er[2]))
    _add_swimmer_idle(scene)
    _add_swimmer_attack(scene, water_origin)
    add_swimmer_hit_animation(scene)
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical entry point discovered by build_all.py."""
    return build_swimmer(blink=blink)
