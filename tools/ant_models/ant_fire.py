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

# Tucked forward right-arm pose keeping the sleek magnifying glass neatly inside Fire's ~0.62-0.68 width.
_FIRE_RIGHT_ARM: List[Tuple[float, float, float]] = [
    (-0.114, 0.438, 0.022),
    (-0.208, 0.410, 0.092),
    (-0.206, 0.416, 0.214),
    (-0.194, 0.434, 0.266),
]


def _helmet_shell_deform(pos: np.ndarray) -> np.ndarray:
    """Sculpt rugged blocky-crowned matte firefighter/ranger hat with sweeping rear neck brim."""
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]

    y_min = float(y.min())
    y_max = float(y.max())
    h = np.clip((y - y_min) / max(1e-6, y_max - y_min), 0.0, 1.0)

    # Smooth brim flare across the lower third of the hat shell.
    brim = np.clip(1.0 - h / 0.35, 0.0, 1.0) ** 1.60

    # Azimuth around +Y: theta = 0 at front (+Z), +-pi at back (-Z).
    theta = np.arctan2(x, z)
    cos_t = np.cos(theta)
    sin_t = np.sin(theta)

    front_w = np.clip(cos_t, 0.0, 1.0) ** 1.5
    side_w = sin_t * sin_t
    rear_w = (0.5 * (1.0 - cos_t)) ** 1.40

    # Blockier, slightly flat-topped sculpted crown (less smooth bubble dome, more rugged hat crown).
    crown_h = np.clip((h - 0.32) / 0.68, 0.0, 1.0)
    # Soft center pinch crease along the crown top (authentic to the rugged HD reference hat).
    crease = np.exp(-((x / 0.085) ** 2)) * np.clip((h - 0.72) / 0.28, 0.0, 1.0)
    p[:, 1] -= 0.024 * crease
    # Flatten the very front crown face slightly so the bold dark-crimson 'A' sits cleanly proud.
    front_face = np.clip(cos_t, 0.0, 1.0) ** 2.6 * np.clip((h - 0.30) / 0.55, 0.0, 1.0) * (1.0 - 0.4 * crown_h)
    p[:, 2] -= 0.016 * front_face

    # Compact front/side brim (total hat width ~0.62) sweeping smoothly into a dramatic flared rear neck brim.
    x_scale = 1.0 + brim * (0.14 * front_w + 0.22 * side_w + 0.32 * rear_w)
    z_scale = 1.0 + brim * (0.18 * front_w + 0.14 * side_w + 0.62 * rear_w)
    p[:, 0] = x * x_scale
    p[:, 2] = z * z_scale

    # Vertical brim sweep: clean raised front visor clearing the blinking eyes, curved sides, low sweeping rear guard.
    p[:, 1] = y + brim * (0.058 * front_w - 0.036 * side_w - 0.232 * rear_w)
    return p


def _build_letter_a() -> List[MeshPart]:
    """Build a bold, rugged 3D embossed dark-crimson capital 'A' with crisp serif feet."""
    parts: List[MeshPart] = []
    thickness = 0.024
    stroke_w = 0.032
    leg_len = 0.162
    tilt_deg = 22.0

    # Left and right diagonal legs with subtle vinyl-matte bevels.
    left_leg = (
        rounded_box(stroke_w, leg_len, thickness, bevel=0.005, segs=3, name="letter_a_left", material="gear_accent")
        .rotate_xyz(rz=-tilt_deg, degrees=True)
        .translate(-0.030, 0.0, 0.0)
    )
    right_leg = (
        rounded_box(stroke_w, leg_len, thickness, bevel=0.005, segs=3, name="letter_a_right", material="gear_accent")
        .rotate_xyz(rz=+tilt_deg, degrees=True)
        .translate(+0.030, 0.0, 0.0)
    )
    parts.extend([left_leg, right_leg])

    # Clean apex peak join.
    apex = (
        rounded_box(0.036, 0.024, thickness, bevel=0.005, segs=2, name="letter_a_apex", material="gear_accent")
        .translate(0.0, 0.070, 0.0)
    )
    parts.append(apex)

    # Bold horizontal crossbar leaving a crisp open triangular window above.
    crossbar = (
        rounded_box(0.094, 0.025, thickness, bevel=0.0045, segs=2, name="letter_a_bar", material="gear_accent")
        .translate(0.0, -0.026, 0.0)
    )
    parts.append(crossbar)

    # Subtle classic serif foot pads matching the HD reference 'A'.
    for side_sign, tag in ((-1.0, "left"), (+1.0, "right")):
        serif = (
            rounded_box(0.052, 0.018, thickness, bevel=0.004, segs=2, name=f"letter_a_serif_{tag}", material="gear_accent")
            .translate(side_sign * 0.060, -0.072, 0.0)
        )
        parts.append(serif)

    return parts


def _build_fire_helmet(scene: SceneBuilder, head_center: Tuple[float, float, float]) -> None:
    """Build rugged matte mustard-yellow firefighter hat with 3 side vent studs, rear brim, and crimson 'A'."""
    hx, hy, hz = head_center
    # Seat cleanly above Pass 2 brow arches & eyelids (eye_y=0.712, eyelid top ~0.790).
    shell_y = hy + 0.118  # ~0.842, keeping deep-set eyes & blinking eyelids 100% unobstructed even at 50-deg pitch
    shell_z = hz - 0.016  # ~0.034, biased slightly back for authentic neck-guard sweep

    # 1. Blockier rugged matte mustard-yellow hat/helmet shell (`e1=0.66, e2=0.72` for structured crown).
    dome = (
        hemisphere_shell(
            0.246,
            0.282,
            0.264,
            thickness=0.028,
            start_phi=-0.10,
            u_segs=48,
            v_segs=20,
            e1=0.66,
            e2=0.72,
            deform_fn=_helmet_shell_deform,
            name="helmet_dome",
            material="gear_gold",
        )
        .translate(hx, shell_y, shell_z)
    )
    scene.add("fire_helmet", dome)

    # Dark inner head-liner rim tucked snugly under the hat crown above the brow line.
    liner = (
        cylinder(0.220, 0.228, 0.044, radial_segs=26, bevel=0.007, axis="y", name="helmet_liner", material="gear_dark")
        .translate(hx, shell_y + 0.012, hz)
    )
    scene.add("fire_helmet", liner)

    # 2. Subtle sculpted twin-ridged firefighter top crown contours + rear spine blended flush into crown.
    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        rail_pts = [
            (hx + side_sign * 0.086, shell_y + 0.252, shell_z + 0.156),
            (hx + side_sign * 0.094, shell_y + 0.280, shell_z + 0.052),
            (hx + side_sign * 0.092, shell_y + 0.274, shell_z - 0.072),
            (hx + side_sign * 0.078, shell_y + 0.208, shell_z - 0.188),
            (hx + side_sign * 0.064, shell_y + 0.094, shell_z - 0.260),
        ]
        rail = curved_tube(
            rail_pts,
            [0.016, 0.023, 0.023, 0.018, 0.012],
            samples_per_seg=5,
            radial_segs=12,
            cap_rings=2,
            name=f"helmet_crown_rail_{tag}",
            material="gear_gold",
        )
        scene.add("fire_helmet", rail)

    # Subtle lower center firefighter comb rib along the back crown for classic rear/side silhouette.
    rear_comb_pts = [
        (hx, shell_y + 0.264, shell_z + 0.040),
        (hx, shell_y + 0.268, shell_z - 0.064),
        (hx, shell_y + 0.216, shell_z - 0.180),
        (hx, shell_y + 0.102, shell_z - 0.272),
        (hx, shell_y + 0.010, shell_z - 0.314),
    ]
    rear_comb = curved_tube(
        rear_comb_pts,
        [0.015, 0.023, 0.022, 0.016, 0.012],
        samples_per_seg=5,
        radial_segs=10,
        cap_rings=2,
        name="helmet_rear_comb",
        material="gear_gold",
    )
    scene.add("fire_helmet", rear_comb)

    # 3. Three small weathered bronze/copper side vent studs (`gear_brass_trim`) flush on EACH crown side!
    # Mirrors the 3 rising round side grommets prominently shown on the HD reference hat.
    vent_specs = (
        (0.236, 0.136, +0.076, 0.017),
        (0.236, 0.156, +0.002, 0.019),
        (0.230, 0.168, -0.072, 0.016),
    )
    for v_idx, (vx, vy_off, vz_off, vrad) in enumerate(vent_specs):
        stud_ring = (
            torus_arc(vrad, 0.006, u_segs=14, v_segs=8, plane="yz", name=f"helmet_vent_ring_{v_idx}", material="gear_brass_trim")
            .translate(vx, shell_y + vy_off, shell_z + vz_off)
        )
        stud_core = (
            cylinder(vrad * 0.78, vrad * 0.72, 0.016, radial_segs=12, bevel=0.003, axis="x", name=f"helmet_vent_core_{v_idx}", material="gear_brass_trim")
            .translate(vx - 0.006, shell_y + vy_off, shell_z + vz_off)
        )
        scene.add_mirrored("fire_helmet", stud_ring)
        scene.add_mirrored("fire_helmet", stud_core)

    # 4. Bold dark-crimson 3D embossed capital letter 'A' emblazoned directly on the front hat crown!
    a_tilt_x = -18.0
    a_pos = (hx, shell_y + 0.162, shell_z + 0.252)
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

    # 3. Slim realistic circular brass rim (`gear_gold_metal`) + subtle copper inner bezel (`gear_brass_trim`).
    lens_r = 0.096
    lens_cy = 0.116 + lens_r  # +0.212

    outer_rim = (
        torus_arc(lens_r + 0.004, 0.0135, u_segs=34, v_segs=12, plane="xy", name="mag_rim_outer", material="gear_gold_metal")
        .scale(1.0, 1.0, 0.90)
        .translate(0.0, lens_cy, 0.0)
    )
    parts.append(outer_rim)

    for z_sign, tag in ((+1.0, "front"), (-1.0, "back")):
        bezel = (
            torus_arc(lens_r - 0.004, 0.0065, u_segs=30, v_segs=8, plane="xy", name=f"mag_bezel_{tag}", material="gear_brass_trim")
            .translate(0.0, lens_cy, z_sign * 0.009)
        )
        parts.append(bezel)

    # 4. Sleek glossy pale-cyan biconvex optical lens.
    lens = (
        ellipsoid(lens_r - 0.004, lens_r - 0.004, 0.019, u_segs=28, v_segs=16, name="mag_lens", material="glass_clear")
        .translate(0.0, lens_cy, 0.0)
    )
    parts.append(lens)

    # Tucked neatly forward from right hand so total width stays ~0.64 (never sticking out sideways),
    # angled smoothly so the bright glass lens reads cleanly across Hero, Front, 3/4, Side, and Back.
    for part in parts:
        placed = (
            part.rotate_xyz(ry=-14.0, degrees=True)
            .rotate_xyz(rx=96.0, ry=-8.0, rz=12.0, degrees=True)
            .translate(hx - 0.008, hy + 0.008, hz + 0.012)
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
