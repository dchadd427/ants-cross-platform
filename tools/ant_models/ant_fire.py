"""Stylized 3D Fire Ant figurine (ribbed firefighter helmet with red 'A' shield + giant magnifying glass).

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet grounded at Y = 0.0)
  +Z = front (South / canonical camera 301)
  +X = ant's left (viewer's right from front), -X = ant's right
"""
from __future__ import annotations

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
        torus_arc,
    )


def _helmet_shell_deform(pos: np.ndarray) -> np.ndarray:
    """Sculpt classic firefighter helmet silhouette: dome + tidy front visor + long sweeping rear neck brim."""
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]

    y_min = float(y.min())
    y_max = float(y.max())
    h = np.clip((y - y_min) / max(1e-6, y_max - y_min), 0.0, 1.0)

    # Smooth brim flare kicking in across the lower third of the dome.
    brim = np.clip(1.0 - h / 0.36, 0.0, 1.0) ** 1.65

    # Azimuth around +Y: theta = 0 at front (+Z), +-pi at back (-Z).
    theta = np.arctan2(x, z)
    cos_t = np.cos(theta)
    sin_t = np.sin(theta)

    front_w = np.clip(cos_t, 0.0, 1.0) ** 1.5
    side_w = sin_t * sin_t
    rear_w = (0.5 * (1.0 - cos_t)) ** 1.45

    # Classic firefighter brim: compact front & sides hugging the head, long flared rear beaver-tail.
    x_scale = 1.0 + brim * (0.14 * front_w + 0.16 * side_w + 0.30 * rear_w)
    z_scale = 1.0 + brim * (0.16 * front_w + 0.14 * side_w + 0.66 * rear_w)
    p[:, 0] = x * x_scale
    p[:, 2] = z * z_scale

    # Vertical brim sweep: lift front cleanly over eyes, swoop rear dramatically downward behind neck.
    p[:, 1] = y + brim * (0.048 * front_w - 0.032 * side_w - 0.255 * rear_w)

    # Subtle proud dome height along the center crown.
    crown_w = np.clip((h - 0.25) / 0.75, 0.0, 1.0)
    center_w = np.exp(-((x / 0.13) ** 2))
    p[:, 1] += 0.024 * crown_w * center_w
    return p


def _build_letter_a() -> List[MeshPart]:
    """Build a crisp, bold 3D embossed crimson capital letter 'A' centered in local XY."""
    parts: List[MeshPart] = []
    thickness = 0.028
    stroke_w = 0.032
    leg_len = 0.154
    tilt_deg = 21.0

    # Left and right diagonal legs of the capital 'A' with smooth vinyl bevels.
    left_leg = (
        rounded_box(stroke_w, leg_len, thickness, bevel=0.0055, segs=3, name="letter_a_left", material="gear_accent")
        .rotate_xyz(rz=-tilt_deg, degrees=True)
        .translate(-0.028, 0.0, 0.0)
    )
    right_leg = (
        rounded_box(stroke_w, leg_len, thickness, bevel=0.0055, segs=3, name="letter_a_right", material="gear_accent")
        .rotate_xyz(rz=+tilt_deg, degrees=True)
        .translate(+0.028, 0.0, 0.0)
    )
    parts.extend([left_leg, right_leg])

    # Crisp joined apex cap so the peak reads sharp and clean.
    apex = (
        rounded_box(0.036, 0.024, thickness, bevel=0.0055, segs=2, name="letter_a_apex", material="gear_accent")
        .translate(0.0, 0.066, 0.0)
    )
    parts.append(apex)

    # Bold horizontal crossbar leaving a crisp triangular inner window above it.
    crossbar = (
        rounded_box(0.088, 0.026, thickness, bevel=0.005, segs=2, name="letter_a_bar", material="gear_accent")
        .translate(0.0, -0.026, 0.0)
    )
    parts.append(crossbar)

    return parts


def _build_fire_helmet(scene: SceneBuilder, head_center: Tuple[float, float, float]) -> None:
    """Build oversized yellow firefighter helmet with ribbed crest, sweeping rear brim, and red 'A' shield."""
    hx, hy, hz = head_center
    # Seat helmet cleanly above brow line so eyes shine brightly underneath (head_top=0.950, eyes=0.704).
    shell_y = hy + 0.094  # ~0.816
    shell_z = hz - 0.016  # ~0.040, biased rearward for iconic neck-guard silhouette

    # 1. Main sculpted dome + sweeping flared rear beaver-tail shell.
    dome = (
        hemisphere_shell(
            0.272,
            0.282,
            0.290,
            thickness=0.032,
            start_phi=-0.12,
            u_segs=48,
            v_segs=20,
            e1=0.88,
            e2=0.92,
            deform_fn=_helmet_shell_deform,
            name="helmet_dome",
            material="gear_gold",
        )
        .translate(hx, shell_y, shell_z)
    )
    scene.add("fire_helmet", dome)

    # Dark inner head-liner shadow band tucked cleanly under the dome rim.
    liner = (
        cylinder(0.244, 0.252, 0.048, radial_segs=28, bevel=0.008, axis="y", name="helmet_liner", material="gear_dark")
        .translate(hx, shell_y + 0.006, hz)
    )
    scene.add("fire_helmet", liner)

    # 2. Iconic fluted/ribbed center crest ridge arching boldly front-to-back across the crown.
    crest_pts = [
        (hx, shell_y + 0.185, shell_z + 0.228),
        (hx, shell_y + 0.296, shell_z + 0.135),
        (hx, shell_y + 0.342, shell_z + 0.010),
        (hx, shell_y + 0.304, shell_z - 0.138),
        (hx, shell_y + 0.188, shell_z - 0.258),
        (hx, shell_y + 0.045, shell_z - 0.335),
    ]
    center_crest = curved_tube(
        crest_pts,
        [
            (0.024, 0.022),
            (0.050, 0.032),
            (0.062, 0.034),
            (0.054, 0.030),
            (0.038, 0.024),
            (0.020, 0.016),
        ],
        samples_per_seg=6,
        radial_segs=14,
        cap_rings=3,
        name="helmet_center_crest",
        material="gear_gold",
    )
    scene.add("fire_helmet", center_crest)

    # Gleaming metallic brass spine trim running along the apex of the center crest.
    trim_pts = [(px, py + 0.032, pz) for px, py, pz in crest_pts[1:-1]]
    crest_trim = curved_tube(
        trim_pts,
        [0.013, 0.015, 0.014, 0.010],
        samples_per_seg=5,
        radial_segs=10,
        cap_rings=2,
        name="helmet_crest_trim",
        material="gear_gold_metal",
    )
    scene.add("fire_helmet", crest_trim)

    # Two pairs of mirrored flanking fluted ribs along the left/right dome slopes (authentic to afst901/afst701).
    for rib_idx, (x_span, y_drop, rad) in enumerate(((0.118, 0.026, 0.024), (0.202, 0.088, 0.020))):
        rib_pts = [
            (x_span * 0.78, shell_y + 0.152 - y_drop * 0.6, shell_z + 0.195),
            (x_span * 0.96, shell_y + 0.244 - y_drop, shell_z + 0.088),
            (x_span * 1.00, shell_y + 0.258 - y_drop, shell_z - 0.038),
            (x_span * 0.94, shell_y + 0.186 - y_drop * 0.85, shell_z - 0.180),
            (x_span * 0.80, shell_y + 0.048 - y_drop * 0.4, shell_z - 0.295),
        ]
        rib_left = curved_tube(
            rib_pts,
            [rad * 0.72, rad, rad * 1.12, rad * 0.98, rad * 0.68],
            samples_per_seg=5,
            radial_segs=12,
            cap_rings=2,
            name=f"helmet_rib_{rib_idx}",
            material="gear_gold",
        )
        scene.add_mirrored("fire_helmet", rib_left)

    # Side metallic brass decorative stud rivets on the helmet temple rim.
    stud_left = (
        sphere(0.018, u_segs=12, v_segs=10, name="helmet_side_stud", material="gear_gold_metal")
        .translate(0.292, shell_y + 0.018, shell_z + 0.020)
    )
    scene.add_mirrored("fire_helmet", stud_left)

    # 3. Raised sculpted classic firefighter front shield badge (`gear_gold_metal`) + bold 3D crimson 'A'.
    shield_poly = [
        (-0.098, +0.098),
        (-0.038, +0.114),
        (0.000, +0.128),
        (+0.038, +0.114),
        (+0.098, +0.098),
        (+0.110, +0.018),
        (+0.094, -0.072),
        (+0.048, -0.106),
        (0.000, -0.118),
        (-0.048, -0.106),
        (-0.094, -0.072),
        (-0.110, +0.018),
    ]
    shield_tilt_x = -26.0  # Tilts shield slightly upward so 'A' reads loud & crisp at 50-deg isometric!
    shield_pos = (hx, shell_y + 0.172, shell_z + 0.268)

    shield_plate = (
        extrude_polygon(shield_poly, depth=0.026, name="helmet_shield_plate", material="gear_gold_metal")
        .rotate_xyz(rx=shield_tilt_x, degrees=True)
        .translate(*shield_pos)
    )
    scene.add("fire_helmet", shield_plate)

    # Sculpted yellow vinyl border frame around the metallic badge perimeter.
    border_pts = [(px, py, 0.012) for px, py in shield_poly]
    shield_border = (
        curved_tube(
            border_pts,
            0.0115,
            samples_per_seg=3,
            radial_segs=10,
            closed=True,
            name="helmet_shield_border",
            material="gear_gold",
        )
        .rotate_xyz(rx=shield_tilt_x, degrees=True)
        .translate(*shield_pos)
    )
    scene.add("fire_helmet", shield_border)

    # Bold 3D embossed crimson capital letter 'A' centered proudly on the front shield face.
    for part in _build_letter_a():
        placed = (
            part.translate(0.0, -0.002, 0.022)
            .rotate_xyz(rx=shield_tilt_x, degrees=True)
            .translate(*shield_pos)
        )
        scene.add("fire_helmet", placed)


def _build_magnifying_glass(scene: SceneBuilder, right_hand: Tuple[float, float, float]) -> None:
    """Build iconic giant magnifying glass gripped cleanly in the ant's right hand (-X side)."""
    hx, hy, hz = right_hand
    parts: List[MeshPart] = []

    # Build in local coordinates along +Y (grip at y=0, crimson handle down to -0.16, lens centered at +0.255).
    # 1. Plump sculpted crimson vinyl handle with ergonomic taper & rounded pommel.
    handle_profile = [
        (0.000, -0.164),
        (0.028, -0.158),
        (0.040, -0.134),
        (0.037, -0.088),
        (0.029, -0.020),
        (0.030, +0.036),
        (0.034, +0.068),
        (0.000, +0.074),
    ]
    handle = lathe(
        handle_profile,
        radial_segs=22,
        cap_bottom=False,
        cap_top=False,
        axis="y",
        name="mag_handle",
        material="gear_accent",
    )
    parts.append(handle)

    # Decorative brass end-cap finial button at bottom of the handle.
    pommel_cap = (
        sphere(0.024, u_segs=14, v_segs=10, name="mag_pommel_cap", material="gear_gold_metal")
        .scale(1.0, 0.72, 1.0)
        .translate(0.0, -0.160, 0.0)
    )
    parts.append(pommel_cap)

    # 2. Stepped metallic brass ferrule collar linking handle into the lens rim.
    ferrule_profile = [
        (0.035, +0.064),
        (0.041, +0.072),
        (0.036, +0.088),
        (0.030, +0.106),
        (0.038, +0.124),
        (0.034, +0.132),
    ]
    ferrule = lathe(
        ferrule_profile,
        radial_segs=22,
        cap_bottom=True,
        cap_top=True,
        axis="y",
        name="mag_ferrule",
        material="gear_gold_metal",
    )
    parts.append(ferrule)

    # 3. Giant circular rim (`gear_gold` body + `gear_gold_metal` twin front/back bezel rings).
    lens_r = 0.134
    lens_cy = 0.126 + lens_r  # +0.260

    outer_rim = (
        torus_arc(lens_r + 0.006, 0.024, u_segs=36, v_segs=14, plane="xy", name="mag_rim_outer", material="gear_gold")
        .scale(1.0, 1.0, 0.86)
        .translate(0.0, lens_cy, 0.0)
    )
    parts.append(outer_rim)

    for z_sign, tag in ((+1.0, "front"), (-1.0, "back")):
        bezel = (
            torus_arc(lens_r - 0.006, 0.011, u_segs=34, v_segs=10, plane="xy", name=f"mag_bezel_{tag}", material="gear_gold_metal")
            .translate(0.0, lens_cy, z_sign * 0.015)
        )
        parts.append(bezel)

    # 4. Glossy pale-cyan biconvex optical glass lens gleaming from every angle.
    lens = (
        ellipsoid(lens_r - 0.006, lens_r - 0.006, 0.028, u_segs=30, v_segs=18, name="mag_lens", material="glass_clear")
        .translate(0.0, lens_cy, 0.0)
    )
    parts.append(lens)

    # Place handle squarely in the ant's right hand (-X), angled forward-outward like focusing
    # sunlight so the gleaming circular lens reads wide and clear across Hero, Front, 3/4, Side,
    # and Rear angles while staying well clear of helmet brim, legs, and ground (Y > 0.0).
    for part in parts:
        placed = (
            part.rotate_xyz(ry=-16.0, degrees=True)
            .rotate_xyz(rx=96.0, ry=-26.0, rz=30.0, degrees=True)
            .translate(hx - 0.022, hy + 0.010, hz + 0.012)
        )
        scene.add("magnifying_glass", placed)


def build_fire() -> SceneBuilder:
    """Build the complete stylized 3D Fire Ant SceneBuilder."""
    scene, anchors = build_base_ant(
        scene_name="ant_fire",
        include_antennae=False,
        include_eyes=True,
        include_brows=True,
        left_arm_pose="idle",
        right_arm_pose="hold_tool",
    )
    _build_fire_helmet(scene, anchors["head_center"])
    _build_magnifying_glass(scene, anchors["right_hand"])
    return scene


def build_ant() -> SceneBuilder:
    """Canonical entry point discovered by build_all.py."""
    return build_fire()
