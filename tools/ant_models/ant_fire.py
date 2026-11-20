"""Sculpted 3D Fire Ant (rugged matte firefighter helmet with 'A' crest + sleek brass magnifying glass).

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

# Natural bent-elbow right-arm hold at chest height for gripping the upright magnifying glass.
_FIRE_RIGHT_ARM: List[Tuple[float, float, float]] = [
    (-0.114, 0.438, 0.022),
    (-0.214, 0.398, 0.096),
    (-0.224, 0.438, 0.214),
    (-0.214, 0.464, 0.272),
]


_HELMET_RX = 0.242
_HELMET_RY = 0.240
_HELMET_RZ = 0.246


def _brim_radial_and_y(theta: np.ndarray) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Compute horizontal-sweeping traditional firefighter helmet brim flare & height vs azimuth."""
    cos_t = np.cos(theta)
    sin_t = np.sin(theta)
    front_w = np.clip(cos_t, 0.0, 1.0) ** 1.4
    side_w = sin_t * sin_t
    rear_w = (0.5 * (1.0 - cos_t)) ** 1.35

    # Classic traditional firefighter oval brim: sleek horizontal side flare (~0.68 W total)
    # with clean front bill and longer gently sloped horizontal rear brim (NOT a wide circular hat).
    x_flare = 0.24 * front_w + 0.26 * side_w + 0.30 * rear_w
    z_flare = 0.34 * front_w + 0.26 * side_w + 0.66 * rear_w
    # Front/sides stay cleanly level-horizontal with gentle curl; rear slopes smoothly down ~14 deg.
    y_shift = 0.018 * front_w - 0.006 * side_w - 0.056 * rear_w
    return x_flare, z_flare, y_shift


def _helmet_shell_deform(pos: np.ndarray) -> np.ndarray:
    """Sculpt classic American traditional firefighter helmet: rounded bell dome + horizontal sweeping brim."""
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]

    y_min = float(y.min())
    y_max = float(y.max())
    h = np.clip((y - y_min) / max(1e-6, y_max - y_min), 0.0, 1.0)

    # Crisp horizontal bell-to-brim transition across the lower 30% of the dome shell.
    brim = np.clip(1.0 - h / 0.30, 0.0, 1.0) ** 1.85

    theta = np.arctan2(x, z)
    x_flare, z_flare, y_shift = _brim_radial_and_y(theta)

    p[:, 0] = x * (1.0 + brim * x_flare)
    p[:, 2] = z * (1.0 + brim * z_flare)

    # Slight crisp upturned lip curl right at the very outer rim edge (h < 0.06).
    lip_curl = 0.012 * (np.clip(1.0 - h / 0.06, 0.0, 1.0) ** 2) * (0.5 + 0.5 * np.cos(theta))
    p[:, 1] = y + brim * y_shift + lip_curl
    return p


def _build_letter_a() -> List[MeshPart]:
    """Build a bold, sharp 3D embossed dark-crimson capital 'A' centered cleanly on the front shield."""
    parts: List[MeshPart] = []
    thickness = 0.022
    stroke_w = 0.028
    leg_len = 0.132
    tilt_deg = 21.5

    left_leg = (
        rounded_box(stroke_w, leg_len, thickness, bevel=0.0045, segs=3, name="letter_a_left", material="gear_accent")
        .rotate_xyz(rz=-tilt_deg, degrees=True)
        .translate(-0.025, 0.0, 0.0)
    )
    right_leg = (
        rounded_box(stroke_w, leg_len, thickness, bevel=0.0045, segs=3, name="letter_a_right", material="gear_accent")
        .rotate_xyz(rz=+tilt_deg, degrees=True)
        .translate(+0.025, 0.0, 0.0)
    )
    parts.extend([left_leg, right_leg])

    apex = (
        rounded_box(0.030, 0.022, thickness, bevel=0.0045, segs=2, name="letter_a_apex", material="gear_accent")
        .translate(0.0, 0.056, 0.0)
    )
    parts.append(apex)

    crossbar = (
        rounded_box(0.078, 0.022, thickness, bevel=0.004, segs=2, name="letter_a_bar", material="gear_accent")
        .translate(0.0, -0.021, 0.0)
    )
    parts.append(crossbar)

    for side_sign, tag in ((-1.0, "left"), (+1.0, "right")):
        serif = (
            rounded_box(0.044, 0.016, thickness, bevel=0.0035, segs=2, name=f"letter_a_serif_{tag}", material="gear_accent")
            .translate(side_sign * 0.050, -0.059, 0.0)
        )
        parts.append(serif)

    return parts


def _build_fire_helmet(scene: SceneBuilder, head_center: Tuple[float, float, float]) -> None:
    """Build classic yellow ribbed American firefighter helmet with rolled brim and scalloped 'A' shield."""
    hx, hy, hz = head_center
    # Seated cleanly above Pass 3 wide-alert eyes & blinking eyelids (eyes y <= 0.796).
    shell_y = hy + 0.112  # ~0.836
    shell_z = hz - 0.006  # ~0.044, balanced over the head with classic horizontal rear brim sweep

    # 1. Smooth rounded bell dome + wide horizontal-sweeping brim.
    dome = (
        hemisphere_shell(
            _HELMET_RX,
            _HELMET_RY,
            _HELMET_RZ,
            thickness=0.024,
            start_phi=-0.04,
            u_segs=52,
            v_segs=22,
            e1=0.94,
            e2=0.94,
            deform_fn=_helmet_shell_deform,
            name="helmet_dome",
            material="gear_gold",
        )
        .translate(hx, shell_y, shell_z)
    )
    scene.add("fire_helmet", dome)

    # Clean rolled outer edge beading loop wrapping smoothly around the entire horizontal brim perimeter.
    n_lip = 56
    lip_pts: List[Tuple[float, float, float]] = []
    start_phi = -0.04
    c_phi = math.cos(start_phi)
    s_phi = math.sin(start_phi)
    y_base = _HELMET_RY * s_phi
    for i in range(n_lip):
        ang = (i / n_lip) * 2.0 * math.pi
        bx = _HELMET_RX * c_phi * math.sin(ang)
        bz = _HELMET_RZ * c_phi * math.cos(ang)
        xf, zf, ys = _brim_radial_and_y(np.array([ang]))
        curl = 0.012 * (0.5 + 0.5 * math.cos(ang))
        lx = hx + bx * (1.0 + float(xf[0]))
        ly = shell_y + y_base + float(ys[0]) + curl - 0.004
        lz = shell_z + bz * (1.0 + float(zf[0]))
        lip_pts.append((lx, ly, lz))

    brim_lip = swept_tube(
        lip_pts,
        0.0155,
        radial_segs=10,
        closed=True,
        name="helmet_brim_lip",
        material="gear_gold",
    )
    scene.add("fire_helmet", brim_lip)

    # Dark inner head-liner band tucked cleanly inside the dome above the brow line.
    liner = (
        cylinder(0.220, 0.226, 0.040, radial_segs=26, bevel=0.006, axis="y", name="helmet_liner", material="gear_dark")
        .translate(hx, shell_y + 0.016, hz)
    )
    scene.add("fire_helmet", liner)

    # 2. Classic 8 radiating molded firefighter crown ribs meeting cleanly at the domed apex star!
    # Azimuths: 0 (front to shield), 180 (back), +-90 (sides), +-48 (front-diagonals), +-132 (rear-diagonals).
    rib_specs = (
        (0.0, 0.42, 0.021, "front"),
        (180.0, 0.08, 0.023, "back"),
        (90.0, 0.12, 0.019, "left"),
        (-90.0, 0.12, 0.019, "right"),
        (46.0, 0.14, 0.018, "fl"),
        (-46.0, 0.14, 0.018, "fr"),
        (134.0, 0.10, 0.018, "bl"),
        (-134.0, 0.10, 0.018, "br"),
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
            # Lift slightly proud of dome shell along the rib arc, blending smoothly at base.
            t_arc = k / (len(phis) - 1)
            lift = 0.012 * (1.0 - 0.55 * (t_arc * t_arc))
            px = hx + (_HELMET_RX + lift) * cp * sa
            py = shell_y + (_HELMET_RY + lift) * sp
            pz = shell_z + (_HELMET_RZ + lift) * cp * ca
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

    # Smooth sculpted central crown boss joining all 8 radiating ribs cleanly at the peak.
    crown_hub = (
        ellipsoid(0.044, 0.026, 0.052, u_segs=16, v_segs=10, name="helmet_crown_hub", material="gear_gold")
        .translate(hx, shell_y + _HELMET_RY + 0.010, shell_z)
    )
    scene.add("fire_helmet", crown_hub)

    # 3. Classic scalloped firefighter front shield badge holding the bold 3D crimson 'A'!
    # Peaked top crown, graceful inward side scallops, wide lower cheeks, flat-curved base resting on front brim.
    shield_poly = [
        (0.000, +0.128),
        (+0.034, +0.104),
        (+0.076, +0.094),
        (+0.096, +0.074),
        (+0.086, +0.022),
        (+0.096, -0.024),
        (+0.092, -0.078),
        (+0.054, -0.098),
        (0.000, -0.104),
        (-0.054, -0.098),
        (-0.092, -0.078),
        (-0.096, -0.024),
        (-0.086, +0.022),
        (-0.096, +0.074),
        (-0.076, +0.094),
        (-0.034, +0.104),
    ]
    shield_tilt_x = -20.0
    shield_pos = (hx, shell_y + 0.118, shell_z + 0.252)

    shield_plate = (
        extrude_polygon(shield_poly, depth=0.020, name="helmet_shield_plate", material="gear_gold_metal")
        .rotate_xyz(rx=shield_tilt_x, degrees=True)
        .translate(*shield_pos)
    )
    scene.add("fire_helmet", shield_plate)

    # Crisp dark-trimmed metallic scalloped border hugging the shield edge cleanly.
    border_pts = [(px, py, 0.008) for px, py in shield_poly]
    shield_border = (
        curved_tube(
            border_pts,
            0.009,
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

    # Three crisp metallic shield mounting rivets (top crown peak + lower-left + lower-right).
    for rv_idx, (rvx, rvy) in enumerate(((0.0, 0.098), (-0.072, -0.068), (+0.072, -0.068))):
        rivet = (
            sphere(0.0085, u_segs=10, v_segs=8, name=f"helmet_shield_rivet_{rv_idx}", material="gear_steel")
            .translate(rvx, rvy, 0.015)
            .rotate_xyz(rx=shield_tilt_x, degrees=True)
            .translate(*shield_pos)
        )
        scene.add("fire_helmet", rivet)

    # Bold dark-crimson 3D embossed capital letter 'A' centered proudly inside the scalloped shield.
    for part in _build_letter_a():
        placed = (
            part.translate(0.0, -0.006, 0.016)
            .rotate_xyz(rx=shield_tilt_x, degrees=True)
            .translate(*shield_pos)
        )
        scene.add("fire_helmet", placed)


def _build_magnifying_glass(scene: SceneBuilder, right_hand: Tuple[float, float, float]) -> None:
    """Build sleek realistic brass magnifying glass tucked cleanly forward at the ant's right hand (-X)."""
    hx, hy, hz = right_hand
    parts: List[MeshPart] = []

    # 1. Slender sculpted dark-crimson handle (`gear_accent`) with refined brass end finial.
    handle_profile = [
        (0.000, -0.148),
        (0.018, -0.142),
        (0.024, -0.120),
        (0.022, -0.075),
        (0.018, -0.015),
        (0.019, +0.036),
        (0.022, +0.064),
        (0.000, +0.068),
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
        sphere(0.016, u_segs=12, v_segs=10, name="mag_pommel_cap", material="gear_gold_metal")
        .scale(1.0, 0.72, 1.0)
        .translate(0.0, -0.144, 0.0)
    )
    parts.append(pommel_cap)

    # 2. Sleek turned brass ferrule collar linking handle cleanly to the circular lens rim.
    ferrule_profile = [
        (0.023, +0.060),
        (0.027, +0.068),
        (0.022, +0.082),
        (0.018, +0.098),
        (0.024, +0.114),
        (0.020, +0.120),
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

    # 3. Slim hollow circular brass rim (`gear_gold_metal`) + copper inner bezels (`gear_brass_trim`).
    # Zero opaque backing inside the ring so the translucent `glass_clear` lens is genuinely see-through.
    lens_r = 0.094
    lens_cy = 0.114 + lens_r  # +0.208

    outer_rim = (
        torus_arc(lens_r + 0.004, 0.013, u_segs=34, v_segs=12, plane="xy", name="mag_rim_outer", material="gear_gold_metal")
        .scale(1.0, 1.0, 0.88)
        .translate(0.0, lens_cy, 0.0)
    )
    parts.append(outer_rim)

    for z_sign, tag in ((+1.0, "front"), (-1.0, "back")):
        bezel = (
            torus_arc(lens_r - 0.004, 0.006, u_segs=30, v_segs=8, plane="xy", name=f"mag_bezel_{tag}", material="gear_brass_trim")
            .translate(0.0, lens_cy, z_sign * 0.0085)
        )
        parts.append(bezel)

    # 4. Thin translucent biconvex optical glass lens (`glass_clear`, alphaMode BLEND).
    lens = (
        ellipsoid(lens_r - 0.004, lens_r - 0.004, 0.013, u_segs=28, v_segs=16, name="mag_lens", material="glass_clear")
        .translate(0.0, lens_cy, 0.0)
    )
    parts.append(lens)

    # Seat handle straight through the gripped right hand at mid/chest height, angled naturally
    # upright and slightly forward-outward so the see-through circular lens faces cleanly forward
    # while staying completely clear of the hard-hat brim, face, and ground.
    for part in parts:
        placed = (
            part.rotate_xyz(rx=24.0, ry=-22.0, rz=14.0, degrees=True)
            .translate(hx, hy, hz + 0.010)
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
    _build_fire_helmet(scene, anchors["head_center"])
    _build_magnifying_glass(scene, anchors["right_hand"])
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical entry point discovered by build_all.py."""
    return build_fire(blink=blink)
