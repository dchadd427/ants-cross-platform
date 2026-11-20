"""Sculpted 3D Fire Ant (rugged matte firefighter helmet with 'A' crest + sleek brass magnifying glass).

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet grounded solidly at Y = 0.0)
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
        hemisphere_shell,
        lathe,
        rounded_box,
        sphere,
        torus_arc,
    )

# Natural bent-elbow right-arm hold at chest height for gripping the upright magnifying glass.
_FIRE_RIGHT_ARM: List[Tuple[float, float, float]] = [
    (-0.114, 0.438, 0.022),
    (-0.214, 0.398, 0.096),
    (-0.224, 0.438, 0.214),
    (-0.214, 0.464, 0.272),
]


def _helmet_shell_deform(pos: np.ndarray) -> np.ndarray:
    """Sculpt classic sleek hard-hat dome with compact uniform safety rim and neat front sun bill."""
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]

    y_min = float(y.min())
    y_max = float(y.max())
    h = np.clip((y - y_min) / max(1e-6, y_max - y_min), 0.0, 1.0)

    # Compact, crisp hard-hat brim flare kicking in cleanly only at the bottom rim (no droopy rear skirt!).
    brim = np.clip(1.0 - h / 0.24, 0.0, 1.0) ** 1.75

    # Azimuth around +Y: theta = 0 at front (+Z), +-pi at back (-Z).
    theta = np.arctan2(x, z)
    cos_t = np.cos(theta)
    sin_t = np.sin(theta)

    # Front hard-hat sun bill peak centered cleanly over the brow; uniform compact gutter rim around sides & back.
    front_bill = np.clip(cos_t, 0.0, 1.0) ** 2.2
    side_back = 1.0 - front_bill

    # Slight clean flattening on the upper front emblem face so the bold crimson 'A' reads razor-sharp.
    front_panel = (np.clip(cos_t, 0.0, 1.0) ** 2.8) * np.clip((h - 0.26) / 0.54, 0.0, 1.0) * (1.0 - 0.45 * np.clip((h - 0.72) / 0.28, 0.0, 1.0))
    p[:, 2] -= 0.015 * front_panel

    # Classic regular hard-hat brim: uniform compact ~12% side/rear rim + crisp forward sun bill (+32% Z).
    x_scale = 1.0 + brim * (0.12 * side_back + 0.14 * front_bill)
    z_scale = 1.0 + brim * (0.12 * side_back + 0.34 * front_bill)
    p[:, 0] = x * x_scale
    p[:, 2] = z * z_scale

    # Level, clean hard-hat rim height: gentle upward curl on side/rear gutter, subtle sleek front bill pitch.
    p[:, 1] = y + brim * (0.010 * side_back + 0.012 * front_bill - 0.008 * (sin_t * sin_t))
    return p


def _build_letter_a() -> List[MeshPart]:
    """Build a bold, rugged 3D embossed dark-crimson capital 'A' with crisp serif feet."""
    parts: List[MeshPart] = []
    thickness = 0.024
    stroke_w = 0.032
    leg_len = 0.156
    tilt_deg = 22.0

    # Left and right diagonal legs with smooth matte bevels.
    left_leg = (
        rounded_box(stroke_w, leg_len, thickness, bevel=0.005, segs=3, name="letter_a_left", material="gear_accent")
        .rotate_xyz(rz=-tilt_deg, degrees=True)
        .translate(-0.029, 0.0, 0.0)
    )
    right_leg = (
        rounded_box(stroke_w, leg_len, thickness, bevel=0.005, segs=3, name="letter_a_right", material="gear_accent")
        .rotate_xyz(rz=+tilt_deg, degrees=True)
        .translate(+0.029, 0.0, 0.0)
    )
    parts.extend([left_leg, right_leg])

    # Clean apex peak join.
    apex = (
        rounded_box(0.034, 0.024, thickness, bevel=0.005, segs=2, name="letter_a_apex", material="gear_accent")
        .translate(0.0, 0.068, 0.0)
    )
    parts.append(apex)

    # Bold horizontal crossbar leaving a crisp open triangular window above.
    crossbar = (
        rounded_box(0.090, 0.025, thickness, bevel=0.0045, segs=2, name="letter_a_bar", material="gear_accent")
        .translate(0.0, -0.025, 0.0)
    )
    parts.append(crossbar)

    # Subtle classic serif foot pads matching the HD reference 'A'.
    for side_sign, tag in ((-1.0, "left"), (+1.0, "right")):
        serif = (
            rounded_box(0.050, 0.018, thickness, bevel=0.004, segs=2, name=f"letter_a_serif_{tag}", material="gear_accent")
            .translate(side_sign * 0.058, -0.070, 0.0)
        )
        parts.append(serif)

    return parts


def _build_fire_helmet(scene: SceneBuilder, head_center: Tuple[float, float, float]) -> None:
    """Build classic yellow safety hard-hat with clean center crown rib, compact front bill, and crimson 'A'."""
    hx, hy, hz = head_center
    # Seat cleanly right above Pass 2 brow arches & blinking eyelids (eye_y=0.712, eyelid top ~0.790).
    shell_y = hy + 0.114  # ~0.838, keeping deep-set eyes & blinking eyelids 100% unobstructed
    shell_z = hz + 0.004  # ~0.054, centered symmetrically on the cranium like a true hard hat

    # 1. Classic sleek rounded hard-hat shell with compact uniform gutter rim + crisp front bill.
    dome = (
        hemisphere_shell(
            0.256,
            0.264,
            0.262,
            thickness=0.026,
            start_phi=-0.06,
            u_segs=48,
            v_segs=20,
            e1=0.86,
            e2=0.86,
            deform_fn=_helmet_shell_deform,
            name="helmet_dome",
            material="gear_gold",
        )
        .translate(hx, shell_y, shell_z)
    )
    scene.add("fire_helmet", dome)

    # Dark inner suspension liner ring tucked snugly inside the hard hat above the brow line.
    liner = (
        cylinder(0.222, 0.228, 0.040, radial_segs=26, bevel=0.006, axis="y", name="helmet_liner", material="gear_dark")
        .translate(hx, shell_y + 0.012, hz)
    )
    scene.add("fire_helmet", liner)

    # 2. Iconic crisp center hard-hat crown rib arching cleanly front-to-back over the dome apex.
    rib_pts = [
        (hx, shell_y + 0.214, shell_z + 0.172),
        (hx, shell_y + 0.268, shell_z + 0.095),
        (hx, shell_y + 0.282, shell_z + 0.000),
        (hx, shell_y + 0.264, shell_z - 0.105),
        (hx, shell_y + 0.194, shell_z - 0.194),
        (hx, shell_y + 0.082, shell_z - 0.246),
    ]
    center_rib = curved_tube(
        rib_pts,
        [
            (0.016, 0.024),
            (0.026, 0.036),
            (0.030, 0.040),
            (0.028, 0.038),
            (0.022, 0.030),
            (0.012, 0.018),
        ],
        samples_per_seg=6,
        radial_segs=14,
        cap_rings=3,
        name="helmet_center_rib",
        material="gear_gold",
    )
    scene.add("fire_helmet", center_rib)

    # Subtle side step-bevel accent ribs flanking the center hard-hat ridge cleanly on top.
    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        flank_pts = [
            (hx + side_sign * 0.088, shell_y + 0.210, shell_z + 0.130),
            (hx + side_sign * 0.102, shell_y + 0.244, shell_z + 0.015),
            (hx + side_sign * 0.096, shell_y + 0.216, shell_z - 0.115),
            (hx + side_sign * 0.076, shell_y + 0.118, shell_z - 0.215),
        ]
        flank = curved_tube(
            flank_pts,
            [0.010, 0.014, 0.013, 0.009],
            samples_per_seg=5,
            radial_segs=10,
            cap_rings=2,
            name=f"helmet_side_ridge_{tag}",
            material="gear_gold",
        )
        scene.add("fire_helmet", flank)

    # 3. Three small weathered bronze/copper side vent studs (`gear_brass_trim`) flush on each side.
    vent_specs = (
        (0.244, 0.112, +0.068, 0.015),
        (0.246, 0.128, +0.002, 0.016),
        (0.240, 0.138, -0.064, 0.014),
    )
    for v_idx, (vx, vy_off, vz_off, vrad) in enumerate(vent_specs):
        stud_ring = (
            torus_arc(vrad, 0.0055, u_segs=14, v_segs=8, plane="yz", name=f"helmet_vent_ring_{v_idx}", material="gear_brass_trim")
            .translate(vx, shell_y + vy_off, shell_z + vz_off)
        )
        stud_core = (
            cylinder(vrad * 0.78, vrad * 0.72, 0.016, radial_segs=12, bevel=0.003, axis="x", name=f"helmet_vent_core_{v_idx}", material="gear_brass_trim")
            .translate(vx - 0.006, shell_y + vy_off, shell_z + vz_off)
        )
        scene.add_mirrored("fire_helmet", stud_ring)
        scene.add_mirrored("fire_helmet", stud_core)

    # 4. Bold dark-crimson 3D embossed capital letter 'A' emblazoned sharply on the front hard-hat crown!
    a_tilt_x = -24.0
    a_pos = (hx, shell_y + 0.138, shell_z + 0.236)
    for part in _build_letter_a():
        placed = (
            part.rotate_xyz(rx=a_tilt_x, degrees=True)
            .translate(*a_pos)
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
