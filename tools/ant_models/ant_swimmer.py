"""Sculpted 3D Swimmer Ant (bold chunky scuba mask, snorkel, steel tank, flippers, shovel, 0.90s Idle).

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet and diving flippers grounded solidly at Y = 0.0)
  +Z = front (South / canonical camera 301)
  +X = ant's left (viewer's right from front), -X = ant's right
"""
from __future__ import annotations

import math
from typing import Any, Dict, List, Tuple

import numpy as np

try:
    from .base_ant import build_base_ant
    from .mesh_core import (
        AnimationChannel,
        AnimationClip,
        MeshPart,
        SceneBuilder,
        curved_tube,
        cylinder,
        lathe,
        quat_from_euler_xyz,
        quat_mul,
        rounded_box,
        superellipsoid,
        swept_tube,
        torus_arc,
    )
except ImportError:
    from base_ant import build_base_ant  # type: ignore
    from mesh_core import (  # type: ignore
        AnimationChannel,
        AnimationClip,
        MeshPart,
        SceneBuilder,
        curved_tube,
        cylinder,
        lathe,
        quat_from_euler_xyz,
        quat_mul,
        rounded_box,
        superellipsoid,
        swept_tube,
        torus_arc,
    )

# Lowered right-arm grip starting at slim thorax shoulder (-0.078, 0.456, 0.014) holding horizontal beach shovel.
_SWIMMER_RIGHT_ARM: List[Tuple[float, float, float]] = [
    (-0.078, 0.456, 0.014),
    (-0.164, 0.368, 0.018),
    (-0.204, 0.282, 0.044),
    (-0.214, 0.256, 0.062),
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
) -> None:
    """Build two-tone tactical banded vertical snorkel on ant's LEFT (+X) side."""
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


def _build_scuba_tank(
    scene: SceneBuilder,
    back_anchor: Tuple[float, float, float],
    thorax_center: Tuple[float, float, float],
) -> None:
    """Build compact brushed-steel scuba air tank with brass gauge, valve, and harness on back-right (-X, -Z)."""
    bx, by, bz = back_anchor
    _, ty, tz = thorax_center
    tank_x = bx - 0.168
    tank_y = by + 0.252
    tank_z = bz - 0.072

    parts: List[MeshPart] = []

    tank_prof = [
        (0.000, -0.132),
        (0.032, -0.128),
        (0.052, -0.112),
        (0.054, -0.090),
        (0.054, +0.084),
        (0.046, +0.112),
        (0.025, +0.128),
        (0.017, +0.140),
        (0.000, +0.142),
    ]
    tank_body = lathe(tank_prof, radial_segs=24, cap_bottom=False, cap_top=False, axis="y", name="scuba_tank_body", material="gear_steel")
    parts.append(tank_body)

    boot = (
        cylinder(0.057, 0.056, 0.040, radial_segs=22, bevel=0.006, axis="y", name="scuba_tank_boot", material="gear_dark")
        .translate(0.0, -0.106, 0.0)
    )
    parts.append(boot)

    for b_idx, by_off in enumerate((-0.030, +0.054)):
        band = (
            cylinder(0.058, 0.058, 0.021, radial_segs=22, bevel=0.004, axis="y", name=f"scuba_tank_band_{b_idx}", material="gear_dark")
            .translate(0.0, by_off, 0.0)
        )
        parts.append(band)

    brass_collar = (
        torus_arc(0.055, 0.0058, u_segs=22, v_segs=8, plane="xz", name="scuba_tank_brass_ring", material="gear_gold_metal")
        .translate(0.0, +0.076, 0.0)
    )
    parts.append(brass_collar)

    valve_neck = (
        cylinder(0.017, 0.015, 0.042, radial_segs=16, bevel=0.003, axis="y", name="scuba_valve_neck", material="gear_steel")
        .translate(0.0, +0.156, 0.0)
    )
    parts.append(valve_neck)

    valve_block = (
        rounded_box(0.044, 0.028, 0.032, bevel=0.006, segs=2, name="scuba_valve_block", material="gear_dark")
        .translate(0.0, +0.178, 0.0)
    )
    parts.append(valve_block)

    valve_wheel = (
        cylinder(0.021, 0.021, 0.015, radial_segs=16, bevel=0.004, axis="y", name="scuba_valve_wheel", material="gear_dark")
        .translate(0.0, +0.200, 0.0)
    )
    parts.append(valve_wheel)

    side_knob = (
        cylinder(0.013, 0.013, 0.026, radial_segs=14, bevel=0.003, axis="x", name="scuba_valve_knob", material="gear_steel")
        .translate(-0.026, +0.178, 0.0)
    )
    parts.append(side_knob)

    gauge = (
        cylinder(0.016, 0.016, 0.013, radial_segs=16, bevel=0.003, axis="z", name="scuba_pressure_gauge", material="gear_gold_metal")
        .translate(+0.025, +0.176, +0.020)
    )
    parts.append(gauge)

    for part in parts:
        placed = (
            part.rotate_xyz(rx=-10.0, rz=7.0, degrees=True)
            .translate(tank_x, tank_y, tank_z)
        )
        scene.add("scuba_tank", placed)

    bracket = (
        rounded_box(0.128, 0.080, 0.040, bevel=0.012, segs=3, name="scuba_tank_bracket", material="gear_dark")
        .rotate_xyz(ry=-0.20)
        .translate(bx - 0.078, by + 0.124, bz - 0.032)
    )
    scene.add("scuba_tank", bracket)

    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        h_pts = [
            (bx - 0.062 + side_sign * 0.044, by + 0.130, bz - 0.024),
            (side_sign * 0.066, ty + 0.106, tz - 0.026),
            (side_sign * 0.064, ty + 0.094, tz + 0.050),
            (side_sign * 0.058, ty + 0.016, tz + 0.094),
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
    """Attach bold ~1.55x larger frogman diving flippers individually onto all 4 feet at Y = 0.0."""
    mid_l = anchors.get("foot_mid_left", (0.152, 0.0, 0.094))
    mid_r = anchors.get("foot_mid_right", (-mid_l[0], 0.0, mid_l[2]))
    rear_l = anchors.get("foot_rear_left", (0.160, 0.0, -0.028))
    rear_r = anchors.get("foot_rear_right", (-rear_l[0], 0.0, rear_l[2]))

    rear_yaw = math.pi - 0.34
    mid_yaw = 0.28

    for tag, foot_pos, length, blade_w, yaw_signed, boot_z_off in (
        ("rear_left", rear_l, 0.232, 0.132, +rear_yaw, -0.010),
        ("rear_right", rear_r, 0.232, 0.132, -rear_yaw, -0.010),
        ("mid_left", mid_l, 0.222, 0.126, +mid_yaw, +0.018),
        ("mid_right", mid_r, 0.222, 0.126, -mid_yaw, +0.018),
    ):
        fx, _, fz = foot_pos
        fin = _make_diving_flipper(f"flipper_{tag}", length=length, blade_w=blade_w)
        scene.add("flippers", fin.rotate_xyz(ry=yaw_signed).translate(fx, 0.0, fz))

        # Snug dark rubber boot pocket encasing the curved digitigrade tarsus cleanly above Y = 0.0.
        boot_vamp = (
            superellipsoid(0.027, 0.019, 0.052, e1=0.72, e2=0.76, u_segs=14, v_segs=10, name=f"flipper_boot_{tag}", material="gear_dark")
            .translate(0.0, 0.021, boot_z_off)
            .rotate_xyz(ry=yaw_signed)
            .translate(fx, 0.0, fz)
        )
        collar = (
            cylinder(0.028, 0.022, 0.038, radial_segs=14, bevel=0.005, axis="y", name=f"flipper_collar_{tag}", material="gear_dark")
            .translate(fx, 0.026, fz)
        )
        scene.add("flippers", boot_vamp)
        scene.add("flippers", collar)


def _build_shovel(
    scene: SceneBuilder,
    right_hand: Tuple[float, float, float],
    right_hand_dir: Tuple[float, float, float],
) -> None:
    """Build Swimmer's authentic horizontal one-piece yellow plastic sandbox hand shovel (`asst901`).

    Local coordinates: (0, 0, 0) sits directly inside Swimmer's lowered right fist at hip height;
    -Z extends backward into the hollow rounded-square D-handle loop (open along X so side view 901 E
    sees right through the loop!), and +Z extends forward into the wide rounded-leaf beach sand-scoop.
    All parts use smooth molded `gear_yellow_plastic` (zero wood shaft, zero woven canvas texture).
    """
    hx, hy, hz = right_hand
    dx, dy, dz = right_hand_dir
    grip_x = hx + 0.020 * dx
    grip_y = hy + 0.020 * dy
    grip_z = hz + 0.020 * dz
    parts: List[MeshPart] = []

    # 1. Rear hollow rounded-square D-ring handle loop lying in the YZ plane (visible open hole in 901 E!).
    d_front_z = -0.128
    d_back_z = -0.236
    d_half_y = 0.044
    d_loop_pts = [
        (0.0, +d_half_y * 0.55, d_front_z),
        (0.0, +d_half_y, d_front_z - 0.018),
        (0.0, +d_half_y, d_back_z + 0.016),
        (0.0, +d_half_y * 0.55, d_back_z),
        (0.0, -d_half_y * 0.55, d_back_z),
        (0.0, -d_half_y, d_back_z + 0.016),
        (0.0, -d_half_y, d_front_z - 0.018),
        (0.0, -d_half_y * 0.55, d_front_z),
    ]
    d_loop = curved_tube(
        d_loop_pts,
        (0.015, 0.015),
        samples_per_seg=3,
        radial_segs=12,
        closed=True,
        name="shovel_d_loop",
        material="gear_yellow_plastic",
    )
    parts.append(d_loop)

    # Molded junction boss joining the rear D-handle loop smoothly into the horizontal shaft.
    d_boss = (
        cylinder(0.022, 0.020, 0.028, radial_segs=16, bevel=0.005, axis="z", name="shovel_d_boss", material="gear_yellow_plastic")
        .translate(0.0, 0.0, d_front_z + 0.010)
    )
    parts.append(d_boss)

    # 2. Short chunky molded yellow plastic horizontal shaft gripped right through local (0, 0, 0).
    shaft_back_z = d_front_z + 0.016
    shaft_front_z = +0.092
    shaft = (
        cylinder(0.0185, 0.0195, shaft_front_z - shaft_back_z, radial_segs=18, bevel=0.004, axis="z", name="shovel_shaft", material="gear_yellow_plastic")
        .translate(0.0, 0.0, 0.5 * (shaft_back_z + shaft_front_z))
    )
    parts.append(shaft)

    # Smooth flared one-piece molded plastic neck transitioning from shaft into the wide sand scoop.
    neck_back_z = shaft_front_z - 0.012
    neck_front_z = shaft_front_z + 0.048
    neck = (
        cylinder(0.020, 0.032, neck_front_z - neck_back_z, radial_segs=18, bevel=0.005, axis="z", name="shovel_neck", material="gear_yellow_plastic")
        .scale(1.18, 0.82, 1.0)
        .translate(0.0, -0.002, 0.5 * (neck_back_z + neck_front_z))
    )
    parts.append(neck)

    # 3. Wide chunky rounded-leaf / beach sand-scoop blade pointing FORWARD (+Z) horizontally at hip height.
    blade_w = 0.220
    blade_l = 0.256
    blade_start_z = neck_front_z - 0.014
    blade_cz = blade_start_z + 0.5 * blade_l

    def _sculpt_beach_scoop(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        # u = 0.0 at rear rounded shoulder -> u = 1.0 at friendly wide rounded beach-scoop nose.
        u = np.clip((p[:, 2] + 0.5 * blade_l) / blade_l, 0.0, 1.0)

        # Classic plump rounded-leaf sandbox shovel planform: flares smoothly out from neck, wide mid belly, rounded nose.
        t_rear = np.clip((0.26 - u) / 0.26, 0.0, 1.0)
        t_tip = np.clip((u - 0.44) / 0.56, 0.0, 1.0)
        rear_taper = 1.0 - 0.56 * (t_rear ** 1.75)
        tip_taper = (1.0 - 0.86 * (t_tip ** 2.05)) ** 0.55
        p[:, 0] *= rear_taper * tip_taper

        nx = p[:, 0] / max(1e-6, 0.5 * blade_w)
        # Deep chunky molded plastic sand-scoop dish (concave upward +Y along side walls & front lip).
        scoop_y = 0.038 * (nx * nx) + 0.024 * (u ** 1.75)
        # Raised curved rear heel lip hugging behind the neck like a real sandbox toy scoop.
        heel_lip = 0.022 * (np.clip((0.18 - u) / 0.18, 0.0, 1.0) ** 1.6) * (0.35 + 0.65 * (nx * nx))
        thick_scale = 1.0 - 0.32 * (u ** 1.2)
        p[:, 1] = p[:, 1] * thick_scale + scoop_y + heel_lip - 0.016
        return p

    blade_core = (
        rounded_box(blade_w, 0.022, blade_l, bevel=0.008, segs=6, name="shovel_blade", material="gear_yellow_plastic")
        .deform(_sculpt_beach_scoop)
        .translate(0.0, 0.0, blade_cz)
    )
    parts.append(blade_core)

    # Molded central plastic strengthening spine tapering cleanly into the scoop dish.
    blade_spine = (
        lathe(
            [(0.024, 0.000), (0.018, 0.036), (0.011, 0.076), (0.000, 0.104)],
            radial_segs=14,
            cap_bottom=True,
            cap_top=False,
            axis="z",
            name="shovel_spine",
            material="gear_yellow_plastic",
        )
        .scale(1.0, 0.65, 1.0)
        .translate(0.0, -0.006, blade_start_z + 0.004)
    )
    parts.append(blade_spine)

    # Horizontal waist-level carry matching asst901 & asst201: bold upright scoop roll exposes the chunky
    # rounded-leaf yellow blade broadly in BOTH 901 E side and 301 S front views while clearing legs & fins.
    for part in parts:
        placed = part.rotate_xyz(rx=5.0, ry=-6.0, rz=62.0, degrees=True)
        scene.add("shovel", placed)
    scene.set_node_transform("shovel", translation=(grip_x, grip_y, grip_z))


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

    return scene.add_animation("Idle", channels, replace=True)


def build_swimmer(blink: float = 0.0) -> SceneBuilder:
    """Build the sculpted 3D Swimmer Ant SceneBuilder with earnest wonder expression, Blink, and 1.80s Idle."""
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
        include_idle=False,
        left_arm_pose="idle",
        right_arm_pose=_SWIMMER_RIGHT_ARM,
        blink=blink,
    )
    closed_angle = float(anchors.get("eyelid_closed_angle", 1.80))
    clip_anchor = _build_scuba_mask(scene, anchors["head_center"], anchors["head_radii"], anchors["eye_left"], closed_angle)
    _build_snorkel(scene, anchors["head_center"], anchors["head_top"], clip_anchor)
    _build_scuba_tank(scene, anchors["back_anchor"], anchors["thorax_center"])
    _build_flippers(scene, anchors)
    _build_shovel(scene, anchors["right_hand"], anchors["right_hand_dir"])

    head_xf = scene.get_node_transform("head")
    for h_node in ("scuba_mask", "snorkel"):
        scene.attach_world_node(h_node, "head_group", preserve_world_pose=False)
        if head_xf is not None:
            scene.set_node_transform(h_node, translation=head_xf.translation)
    scene.attach_world_node("scuba_tank", "upper_body", preserve_world_pose=False)
    scene.attach_world_node("shovel", "upper_body", preserve_world_pose=False)
    _add_swimmer_idle(scene)
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical entry point discovered by build_all.py."""
    return build_swimmer(blink=blink)
