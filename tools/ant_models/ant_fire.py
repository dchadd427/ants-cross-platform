"""Sculpted 3D Fire Ant (classic 8-rib American firefighter helmet with 'A' shield + see-through magnifier).

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
    from .base_ant import build_base_ant
    from .mesh_core import (
        MeshPart,
        SceneBuilder,
        curved_tube,
        cylinder,
        ellipsoid,
        extrude_polygon,
        hemisphere_shell,
        lathe,
        rounded_box,
        sphere,
        swept_tube,
        torus_arc,
    )
except ImportError:
    from base_ant import build_base_ant  # type: ignore
    from mesh_core import (  # type: ignore
        MeshPart,
        SceneBuilder,
        curved_tube,
        cylinder,
        ellipsoid,
        extrude_polygon,
        hemisphere_shell,
        lathe,
        rounded_box,
        sphere,
        swept_tube,
        torus_arc,
    )

# Natural flowing bent-elbow right-arm hold starting at Pass 4 shoulder (-0.092, 0.476, 0.014).
_FIRE_RIGHT_ARM: List[Tuple[float, float, float]] = [
    (-0.092, 0.476, 0.014),
    (-0.194, 0.434, 0.072),
    (-0.204, 0.468, 0.174),
    (-0.194, 0.494, 0.228),
]


def _brim_radial_and_y(theta: np.ndarray) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Compute horizontal-sweeping traditional firefighter helmet brim flare & height vs azimuth."""
    cos_t = np.cos(theta)
    sin_t = np.sin(theta)
    front_w = np.clip(cos_t, 0.0, 1.0) ** 1.4
    side_w = sin_t * sin_t
    rear_w = (0.5 * (1.0 - cos_t)) ** 1.25

    # Classic traditional firefighter oval brim: crisp front bill, sleek flared sides, and a bold
    # wide horizontal-sloped rear ducktail extending generously behind the neck/shoulders.
    x_flare = 0.24 * front_w + 0.28 * side_w + 0.46 * rear_w
    z_flare = 0.34 * front_w + 0.28 * side_w + 1.15 * rear_w
    y_shift = 0.018 * front_w - 0.010 * side_w - 0.092 * rear_w
    return x_flare, z_flare, y_shift


def _helmet_shell_deform(pos: np.ndarray) -> np.ndarray:
    """Sculpt classic American traditional firefighter helmet: rounded bell dome + bold ducktail brim."""
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]

    y_min = float(y.min())
    y_max = float(y.max())
    h = np.clip((y - y_min) / max(1e-6, y_max - y_min), 0.0, 1.0)

    brim = np.clip(1.0 - h / 0.32, 0.0, 1.0) ** 1.80

    theta = np.arctan2(x, z)
    x_flare, z_flare, y_shift = _brim_radial_and_y(theta)

    p[:, 0] = x * (1.0 + brim * x_flare)
    p[:, 2] = z * (1.0 + brim * z_flare)

    # Crisp rolled outer edge curl along both the front bill and the sweeping rear ducktail lip.
    edge_mask = np.clip(1.0 - h / 0.06, 0.0, 1.0) ** 2
    lip_curl = (0.011 * (0.5 + 0.5 * np.cos(theta)) + 0.010 * (0.5 * (1.0 - np.cos(theta)) ** 1.4)) * edge_mask
    p[:, 1] = y + brim * y_shift + lip_curl
    return p


def _build_letter_a() -> List[MeshPart]:
    """Build a bold, sharp 3D embossed dark-crimson capital 'A' centered cleanly on the front shield."""
    parts: List[MeshPart] = []
    thickness = 0.020
    stroke_w = 0.026
    leg_len = 0.124
    tilt_deg = 21.5

    left_leg = (
        rounded_box(stroke_w, leg_len, thickness, bevel=0.0045, segs=3, name="letter_a_left", material="gear_accent")
        .rotate_xyz(rz=-tilt_deg, degrees=True)
        .translate(-0.024, 0.0, 0.0)
    )
    right_leg = (
        rounded_box(stroke_w, leg_len, thickness, bevel=0.0045, segs=3, name="letter_a_right", material="gear_accent")
        .rotate_xyz(rz=+tilt_deg, degrees=True)
        .translate(+0.024, 0.0, 0.0)
    )
    parts.extend([left_leg, right_leg])

    apex = (
        rounded_box(0.028, 0.020, thickness, bevel=0.0045, segs=2, name="letter_a_apex", material="gear_accent")
        .translate(0.0, 0.052, 0.0)
    )
    parts.append(apex)

    crossbar = (
        rounded_box(0.074, 0.021, thickness, bevel=0.004, segs=2, name="letter_a_bar", material="gear_accent")
        .translate(0.0, -0.020, 0.0)
    )
    parts.append(crossbar)

    for side_sign, tag in ((-1.0, "left"), (+1.0, "right")):
        serif = (
            rounded_box(0.042, 0.015, thickness, bevel=0.0035, segs=2, name=f"letter_a_serif_{tag}", material="gear_accent")
            .translate(side_sign * 0.047, -0.056, 0.0)
        )
        parts.append(serif)

    return parts


def _measure_eyelid_max_y(
    scene: SceneBuilder,
    eye_left: Tuple[float, float, float],
    closed_angle: float,
) -> float:
    """Measure the highest world Y reached by eyelid_left across full blink=0..1."""
    parts = scene.nodes.get("eyelid_left", [])
    if not parts:
        return eye_left[1] + 0.082
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
) -> None:
    """Build classic yellow 8-rib American firefighter helmet with bold rear ducktail brim."""
    hx, _, hz = head_center
    hrx, _, hrz = head_radii
    eyelid_top_y = _measure_eyelid_max_y(scene, eye_left, closed_angle)

    # Seat helmet brim cleanly above the big eyes & blinking eyelids while hugging the crown tightly.
    shell_y = max(eyelid_top_y + 0.022, head_top[1] - 0.148)
    shell_z = hz - 0.008
    dome_rx = hrx + 0.018
    dome_ry = max(0.212, (head_top[1] - shell_y) + 0.088)
    dome_rz = hrz + 0.028

    # 1. Smooth rounded bell dome + wide horizontal-sweeping rear ducktail brim.
    dome = (
        hemisphere_shell(
            dome_rx,
            dome_ry,
            dome_rz,
            thickness=0.022,
            start_phi=-0.04,
            u_segs=56,
            v_segs=24,
            e1=0.94,
            e2=0.94,
            deform_fn=_helmet_shell_deform,
            name="helmet_dome",
            material="gear_gold",
        )
        .translate(hx, shell_y, shell_z)
    )
    scene.add("fire_helmet", dome)

    # Clean rolled outer edge beading loop wrapping smoothly around the entire oval & ducktail brim.
    n_lip = 64
    lip_pts: List[Tuple[float, float, float]] = []
    start_phi = -0.04
    c_phi = math.cos(start_phi)
    s_phi = math.sin(start_phi)
    y_base = dome_ry * s_phi
    for i in range(n_lip):
        ang = (i / n_lip) * 2.0 * math.pi
        bx = dome_rx * c_phi * math.sin(ang)
        bz = dome_rz * c_phi * math.cos(ang)
        xf, zf, ys = _brim_radial_and_y(np.array([ang]))
        cos_a = math.cos(ang)
        curl = 0.011 * (0.5 + 0.5 * cos_a) + 0.010 * (0.5 * ((1.0 - cos_a) ** 1.4))
        lx = hx + bx * (1.0 + float(xf[0]))
        ly = shell_y + y_base + float(ys[0]) + curl - 0.004
        lz = shell_z + bz * (1.0 + float(zf[0]))
        lip_pts.append((lx, ly, lz))

    brim_lip = swept_tube(
        lip_pts,
        0.0145,
        radial_segs=10,
        closed=True,
        name="helmet_brim_lip",
        material="gear_gold",
    )
    scene.add("fire_helmet", brim_lip)

    # Dark inner head-liner band contoured elliptically to the narrower-Z Pass 4 cranium.
    liner = (
        cylinder(hrx * 0.94, hrx * 0.97, 0.036, radial_segs=26, bevel=0.006, axis="y", name="helmet_liner", material="gear_dark")
        .scale(1.0, 1.0, hrz / max(1e-6, hrx))
        .translate(hx, shell_y + 0.018, hz)
    )
    scene.add("fire_helmet", liner)

    # 2. Classic 8 radiating molded firefighter crown ribs meeting cleanly at the domed apex star.
    rib_specs = (
        (0.0, 0.42, 0.020, "front"),
        (180.0, 0.08, 0.022, "back"),
        (90.0, 0.12, 0.018, "left"),
        (-90.0, 0.12, 0.018, "right"),
        (46.0, 0.14, 0.017, "fl"),
        (-46.0, 0.14, 0.017, "fr"),
        (134.0, 0.10, 0.017, "bl"),
        (-134.0, 0.10, 0.017, "br"),
    )
    for az_deg, phi_end, peak_rad, tag in rib_specs:
        az = math.radians(az_deg)
        sa, ca = math.sin(az), math.cos(az)
        phis = np.linspace(0.5 * math.pi, phi_end, 6)
        r_pts: List[Tuple[float, float, float]] = []
        r_rads: List[float] = []
        for k, phi in enumerate(phis):
            cp = float(abs(math.cos(phi)) ** 0.94)
            sp = float(abs(math.sin(phi)) ** 0.94)
            t_arc = k / (len(phis) - 1)
            lift = 0.011 * (1.0 - 0.55 * (t_arc * t_arc))
            px = hx + (dome_rx + lift) * cp * sa
            py = shell_y + (dome_ry + lift) * sp
            pz = shell_z + (dome_rz + lift) * cp * ca
            r_pts.append((px, py, pz))
            r_rads.append(peak_rad * (0.95 - 0.35 * t_arc))
        rib = curved_tube(
            r_pts,
            r_rads,
            samples_per_seg=4,
            radial_segs=10,
            cap_rings=2,
            name=f"helmet_rib_{tag}",
            material="gear_gold",
        )
        scene.add("fire_helmet", rib)

    crown_hub = (
        ellipsoid(0.042, 0.024, 0.048, u_segs=16, v_segs=10, name="helmet_crown_hub", material="gear_gold")
        .translate(hx, shell_y + dome_ry + 0.009, shell_z)
    )
    scene.add("fire_helmet", crown_hub)

    # 3. Classic scalloped firefighter front shield badge holding the bold 3D crimson 'A'.
    shield_poly = [
        (0.000, +0.120),
        (+0.032, +0.098),
        (+0.072, +0.088),
        (+0.090, +0.070),
        (+0.080, +0.020),
        (+0.090, -0.022),
        (+0.086, -0.074),
        (+0.050, -0.092),
        (0.000, -0.098),
        (-0.050, -0.092),
        (-0.086, -0.074),
        (-0.090, -0.022),
        (-0.080, +0.020),
        (-0.090, +0.070),
        (-0.072, +0.088),
        (-0.032, +0.098),
    ]
    shield_tilt_x = -20.0
    shield_pos = (hx, shell_y + 0.106, shell_z + dome_rz + 0.004)

    shield_plate = (
        extrude_polygon(shield_poly, depth=0.018, name="helmet_shield_plate", material="gear_gold_metal")
        .rotate_xyz(rx=shield_tilt_x, degrees=True)
        .translate(*shield_pos)
    )
    scene.add("fire_helmet", shield_plate)

    border_pts = [(px, py, 0.007) for px, py in shield_poly]
    shield_border = (
        curved_tube(
            border_pts,
            0.0085,
            samples_per_seg=3,
            radial_segs=10,
            closed=True,
            name="helmet_shield_border",
            material="gear_dark",
        )
        .rotate_xyz(rx=shield_tilt_x, degrees=True)
        .translate(*shield_pos)
    )
    scene.add("fire_helmet", shield_border)

    for rv_idx, (rvx, rvy) in enumerate(((0.0, 0.092), (-0.068, -0.064), (+0.068, -0.064))):
        rivet = (
            sphere(0.008, u_segs=10, v_segs=8, name=f"helmet_shield_rivet_{rv_idx}", material="gear_steel")
            .translate(rvx, rvy, 0.014)
            .rotate_xyz(rx=shield_tilt_x, degrees=True)
            .translate(*shield_pos)
        )
        scene.add("fire_helmet", rivet)

    for part in _build_letter_a():
        placed = (
            part.translate(0.0, -0.005, 0.015)
            .rotate_xyz(rx=shield_tilt_x, degrees=True)
            .translate(*shield_pos)
        )
        scene.add("fire_helmet", placed)


def _build_magnifying_glass(
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

    # Handle seated cleanly directly inside the right pincer-grip pocket at chest height, angled
    # upright-forward so the hollow see-through lens clears the helmet brim, arm, and ground.
    for part in parts:
        placed = (
            part.rotate_xyz(rx=24.0, ry=-22.0, rz=14.0, degrees=True)
            .translate(grip_x, grip_y, grip_z)
        )
        scene.add("magnifying_glass", placed)


def build_fire(blink: float = 0.0) -> SceneBuilder:
    """Build the sculpted 3D Fire Ant SceneBuilder with intact Blink animation."""
    scene, anchors = build_base_ant(
        scene_name="ant_fire",
        include_antennae=False,
        include_eyes=True,
        include_brows=False,
        left_arm_pose="idle",
        right_arm_pose=_FIRE_RIGHT_ARM,
        blink=blink,
    )
    _build_fire_helmet(
        scene,
        anchors["head_center"],
        anchors["head_radii"],
        anchors["head_top"],
        anchors["eye_left"],
        float(anchors.get("eyelid_closed_angle", 1.66)),
    )
    _build_magnifying_glass(scene, anchors["right_hand"], anchors["right_hand_dir"])
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical entry point discovered by build_all.py."""
    return build_fire(blink=blink)
