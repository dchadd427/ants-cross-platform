"""Sculpted 3D Bomber Ant (rugged brass-and-copper goggles, tight upright dynamite pack, tucked TNT bomb).

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet grounded solidly at Y = 0.0)
  +Z = front (South / canonical camera 301)
  +X = ant's left (viewer's right from front), -X = ant's right
"""
from __future__ import annotations

import math
from typing import List, Tuple

try:
    from .base_ant import build_base_ant
    from .mesh_core import (
        MeshPart,
        SceneBuilder,
        curved_tube,
        cylinder,
        ellipsoid,
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
        lathe,
        rounded_box,
        sphere,
        swept_tube,
        torus_arc,
    )

# Compact tucked right-arm hold so the handheld TNT canister stays neatly inside Bomber's tight width.
_BOMBER_RIGHT_ARM: List[Tuple[float, float, float]] = [
    (-0.114, 0.438, 0.022),
    (-0.204, 0.402, 0.088),
    (-0.194, 0.394, 0.198),
    (-0.178, 0.408, 0.246),
]


def _spow(val: float, exp: float) -> float:
    return math.copysign(abs(val) ** exp, val)


def _build_goggle_barrel(side_sign: float) -> List[MeshPart]:
    """Build one rugged copper-and-brass demolition goggle barrel with dark slate-teal lens."""
    parts: List[MeshPart] = []
    tag = "left" if side_sign > 0 else "right"

    # Rear dark rubber eye-cup housing enclosing the eye & eyelid sweep cleanly with zero clipping.
    cushion_profile = [
        (0.068, -0.044),
        (0.088, -0.032),
        (0.094, -0.004),
        (0.086, +0.016),
    ]
    cushion = lathe(
        cushion_profile,
        radial_segs=26,
        cap_bottom=True,
        cap_top=True,
        axis="z",
        name=f"goggle_cushion_{tag}",
        material="gear_dark",
    )
    parts.append(cushion)

    # Rugged copper-bronze mid-barrel sleeve matching the weathered aviator-goggle silhouette.
    copper_profile = [
        (0.084, +0.004),
        (0.093, +0.012),
        (0.095, +0.058),
        (0.088, +0.068),
    ]
    copper_body = lathe(
        copper_profile,
        radial_segs=28,
        cap_bottom=True,
        cap_top=True,
        axis="z",
        name=f"goggle_copper_barrel_{tag}",
        material="gear_brass_trim",
    )
    parts.append(copper_body)

    # Weathered yellow-gold stepped front lens hood bezel framing the dark reflective lens.
    bezel_profile = [
        (0.084, +0.056),
        (0.095, +0.066),
        (0.096, +0.096),
        (0.088, +0.106),
        (0.072, +0.106),
        (0.068, +0.084),
    ]
    front_hood = lathe(
        bezel_profile,
        radial_segs=28,
        cap_bottom=True,
        cap_top=True,
        axis="z",
        name=f"goggle_front_hood_{tag}",
        material="gear_gold",
    )
    parts.append(front_hood)

    # Crisp metallic brass front trim ring around the lip of the hood.
    front_ring = (
        torus_arc(0.084, 0.010, u_segs=28, v_segs=10, plane="xy", name=f"goggle_brass_ring_{tag}", material="gear_gold_metal")
        .translate(0.0, 0.0, 0.101)
    )
    parts.append(front_ring)

    # Recessed dark inner lens seat collar behind the glass edge.
    inner_seat = (
        torus_arc(0.071, 0.0065, u_segs=24, v_segs=8, plane="xy", name=f"goggle_inner_seat_{tag}", material="gear_dark")
        .translate(0.0, 0.0, 0.090)
    )
    parts.append(inner_seat)

    # Deep dark reflective slate-teal optical lens seated cleanly ahead of eyelids (Z > 0.32).
    lens = (
        ellipsoid(0.072, 0.072, 0.022, u_segs=24, v_segs=14, name=f"goggle_lens_{tag}", material="glass_tinted")
        .translate(0.0, 0.0, 0.086)
    )
    parts.append(lens)

    # Three small raised golden rivets arrayed along the upper-outer copper barrel rim.
    for r_idx, ang_deg in enumerate((22.0, 56.0, 92.0)):
        ang = math.radians(ang_deg)
        rx = side_sign * 0.094 * math.sin(ang)
        ry = 0.094 * math.cos(ang)
        rivet = (
            sphere(0.0105, u_segs=10, v_segs=8, name=f"goggle_rivet_{tag}_{r_idx}", material="gear_gold_metal")
            .translate(rx, ry, 0.036)
        )
        parts.append(rivet)

    return parts


def _build_goggles(scene: SceneBuilder, head_center: Tuple[float, float, float]) -> None:
    """Build rugged twin brass-and-copper demolition goggles, nose bridge, and snug dark head strap."""
    hx, hy, hz = head_center
    goggle_y = hy - 0.010  # ~0.714, aligned cleanly with Pass 2 eye_y (0.712)
    goggle_z = hz + 0.214  # ~0.264; lens sits at Z ~ 0.350, strictly clear of closed eyelids (Z <= 0.319)
    barrel_x = 0.098

    for side_sign in (+1.0, -1.0):
        yaw = side_sign * 0.17
        pitch = -0.09
        for part in _build_goggle_barrel(side_sign):
            placed = (
                part.rotate_xyz(rx=pitch, ry=yaw)
                .translate(hx + side_sign * barrel_x, goggle_y, goggle_z)
            )
            scene.add("goggles", placed)

    # Sculpted copper-bronze nose bridge joining the twin goggle housings.
    bridge = (
        rounded_box(0.068, 0.036, 0.046, bevel=0.011, segs=3, name="goggle_bridge", material="gear_brass_trim")
        .translate(hx, goggle_y + 0.010, goggle_z + 0.044)
    )
    scene.add("goggles", bridge)

    bridge_pin = (
        cylinder(0.012, 0.012, 0.014, radial_segs=14, bevel=0.003, axis="z", name="goggle_bridge_bolt", material="gear_gold_metal")
        .translate(hx, goggle_y + 0.012, goggle_z + 0.068)
    )
    scene.add("goggles", bridge_pin)

    # Temple strap clasps anchored cleanly on the sides of the narrower Pass 2 cranium.
    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        clasp = (
            rounded_box(0.034, 0.056, 0.060, bevel=0.010, segs=3, name=f"goggle_clasp_{tag}", material="gear_brass_trim")
            .rotate_xyz(ry=side_sign * 0.30)
            .translate(hx + side_sign * 0.216, goggle_y + 0.004, hz + 0.148)
        )
        scene.add("goggles", clasp)

    # Snug dark rubber strap hugging the Pass 2 superellipsoid head (rx=0.228, rz=0.226, e2=0.72).
    rx_strap = 0.240
    rz_strap = 0.238
    strap_pts: List[Tuple[float, float, float]] = []
    n_arc = 28
    start_ang = math.radians(52.0)
    end_ang = math.radians(308.0)
    for i in range(n_arc + 1):
        ang = start_ang + (end_ang - start_ang) * (i / n_arc)
        sx = hx + rx_strap * _spow(math.sin(ang), 0.72)
        sz = hz + rz_strap * _spow(math.cos(ang), 0.72)
        rear_lift = 0.012 * (0.5 * (1.0 - math.cos(ang)))
        strap_pts.append((sx, goggle_y + 0.006 + rear_lift, sz))

    strap = swept_tube(
        strap_pts,
        (0.032, 0.014),
        radial_segs=12,
        cap_start=True,
        cap_end=True,
        closed=False,
        cap_rings=3,
        name="goggle_strap",
        material="gear_dark",
    )
    scene.add("goggles", strap)

    # Metallic rear adjuster buckle centered cleanly at the back of the head.
    rear_buckle = (
        rounded_box(0.076, 0.072, 0.028, bevel=0.008, segs=3, name="goggle_rear_buckle", material="gear_gold_metal")
        .translate(hx, goggle_y + 0.018, hz - rz_strap - 0.005)
    )
    scene.add("goggles", rear_buckle)


def _build_dynamite_stick(index: int, stick_len: float, stick_r: float, fuse_curve_x: float) -> List[MeshPart]:
    """Build one sculpted crimson dynamite stick along local +Y (base at Y = 0, top at Y = stick_len)."""
    parts: List[MeshPart] = []

    casing = (
        cylinder(
            stick_r,
            stick_r,
            stick_len,
            radial_segs=18,
            bevel=0.0075,
            axis="y",
            name=f"dynamite_stick_{index}",
            material="gear_accent",
        )
        .translate(0.0, 0.5 * stick_len, 0.0)
    )
    parts.append(casing)

    # Twin dark tape binding straps around each stick.
    for b_idx, frac in enumerate((0.28, 0.74)):
        band = (
            cylinder(
                stick_r + 0.0038,
                stick_r + 0.0038,
                0.024,
                radial_segs=18,
                bevel=0.003,
                axis="y",
                name=f"dynamite_band_{index}_{b_idx}",
                material="gear_dark",
            )
            .translate(0.0, frac * stick_len, 0.0)
        )
        parts.append(band)

    # Copper-brass crimp ring and recessed dark fuse plug at the stick crown.
    collar = (
        torus_arc(stick_r * 0.85, 0.0068, u_segs=16, v_segs=8, plane="xz", name=f"dynamite_collar_{index}", material="gear_brass_trim")
        .translate(0.0, stick_len - 0.003, 0.0)
    )
    parts.append(collar)

    plug = (
        cylinder(stick_r * 0.60, stick_r * 0.52, 0.015, radial_segs=14, bevel=0.0025, axis="y", name=f"dynamite_plug_{index}", material="gear_dark")
        .translate(0.0, stick_len + 0.003, 0.0)
    )
    parts.append(plug)

    # Short crisp curved white fuse arching cleanly upward from the top plug.
    fuse_pts = [
        (0.0, stick_len + 0.006, 0.0),
        (0.35 * fuse_curve_x, stick_len + 0.048, -0.008),
        (0.82 * fuse_curve_x, stick_len + 0.088, -0.016),
        (1.08 * fuse_curve_x, stick_len + 0.114, -0.010),
    ]
    fuse = curved_tube(
        fuse_pts,
        [0.010, 0.009, 0.0082, 0.0072],
        samples_per_seg=4,
        radial_segs=8,
        cap_rings=2,
        name=f"dynamite_fuse_{index}",
        material="fuse_white",
    )
    parts.append(fuse)

    tip = sphere(0.012, u_segs=10, v_segs=8, name=f"dynamite_spark_{index}", material="gear_gold").translate(*fuse_pts[-1])
    parts.append(tip)

    return parts


def _build_dynamite_pack(scene: SceneBuilder, back_anchor: Tuple[float, float, float]) -> None:
    """Build compact dorsal harness and tightly bundled upright 4-stick crimson dynamite pack."""
    bx, by, bz = back_anchor
    pack_y = by + 0.118  # ~0.486, seated high on upper dorsal thorax behind neck
    pack_z = bz - 0.056  # ~-0.198, snug behind thorax & head back (-0.176)

    # Compact dark harness backplate contoured onto the slender Pass 2 dorsal thorax.
    backplate = (
        rounded_box(0.196, 0.162, 0.058, bevel=0.018, segs=3, name="pack_backplate", material="gear_dark")
        .rotate_xyz(rx=-0.30)
        .translate(bx, pack_y - 0.008, pack_z + 0.022)
    )
    scene.add("dynamite_pack", backplate)

    # Twin dark shoulder harness straps wrapping over the narrow Pass 2 shoulders to chest.
    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        s_pts = [
            (side_sign * 0.076, pack_y + 0.024, pack_z + 0.030),
            (side_sign * 0.095, 0.482, -0.048),
            (side_sign * 0.092, 0.466, 0.062),
            (side_sign * 0.078, 0.376, 0.122),
            (side_sign * 0.074, 0.308, 0.112),
        ]
        s_strap = curved_tube(
            s_pts,
            (0.012, 0.019),
            samples_per_seg=4,
            radial_segs=10,
            cap_rings=2,
            name=f"harness_shoulder_{tag}",
            material="gear_dark",
        )
        scene.add("dynamite_pack", s_strap)

        chest_buckle = (
            rounded_box(0.032, 0.038, 0.020, bevel=0.005, segs=2, name=f"harness_buckle_{tag}", material="gear_brass_trim")
            .rotate_xyz(rx=-0.16, ry=side_sign * 0.30)
            .translate(side_sign * 0.078, 0.382, 0.126)
        )
        scene.add("dynamite_pack", chest_buckle)

    # Horizontal front chest cinch strap linking both shoulder straps cleanly.
    chest_belt = curved_tube(
        [
            (-0.098, 0.318, 0.074),
            (-0.060, 0.318, 0.128),
            (0.000, 0.318, 0.140),
            (0.060, 0.318, 0.128),
            (0.098, 0.318, 0.074),
        ],
        (0.015, 0.010),
        samples_per_seg=4,
        radial_segs=8,
        cap_rings=2,
        name="harness_chest_belt",
        material="gear_dark",
    )
    scene.add("dynamite_pack", chest_belt)

    # Tight curved retention strap + brass buckle clamping the upright 4-stick dynamite bundle.
    rack_pts = [
        (-0.155, pack_y + 0.098, pack_z - 0.032),
        (-0.092, pack_y + 0.112, pack_z - 0.084),
        (0.000, pack_y + 0.118, pack_z - 0.100),
        (0.092, pack_y + 0.112, pack_z - 0.084),
        (0.155, pack_y + 0.098, pack_z - 0.032),
    ]
    rack_strap = curved_tube(
        rack_pts,
        (0.022, 0.014),
        samples_per_seg=5,
        radial_segs=10,
        cap_rings=2,
        name="dynamite_rack_strap",
        material="gear_dark",
    )
    scene.add("dynamite_pack", rack_strap)

    rack_buckle = (
        rounded_box(0.064, 0.056, 0.028, bevel=0.008, segs=3, name="dynamite_rack_buckle", material="gear_gold_metal")
        .rotate_xyz(rx=-0.34)
        .translate(0.0, pack_y + 0.118, pack_z - 0.108)
    )
    scene.add("dynamite_pack", rack_buckle)

    # Tight steep upright 4-stick crimson dynamite fan behind the shoulders/head (keeps Bomber width ~0.57).
    # Note: positive rz leans +Y toward -X (ant right); negative rz leans +Y toward +X (ant left).
    # Each tuple: (splay_rz_deg, tilt_back_rx_deg, base_ox, base_oy, base_oz, length, fuse_curve_x).
    stick_specs = (
        (+24.0, -20.0, -0.080, 0.032, -0.024, 0.350, -0.032),
        (+8.0, -24.0, -0.030, 0.044, -0.042, 0.380, -0.015),
        (-8.0, -24.0, +0.030, 0.044, -0.042, 0.380, +0.015),
        (-24.0, -20.0, +0.080, 0.032, -0.024, 0.350, +0.032),
    )
    for idx, (rz_deg, rx_deg, ox, oy, oz, slen, fuse_dx) in enumerate(stick_specs):
        for part in _build_dynamite_stick(idx, slen, 0.040, fuse_dx):
            placed = (
                part.rotate_xyz(rx=rx_deg, rz=rz_deg, degrees=True)
                .translate(bx + ox, pack_y + oy, pack_z + oz)
            )
            scene.add("dynamite_pack", placed)


def _build_handheld_bomb(scene: SceneBuilder, right_hand: Tuple[float, float, float]) -> None:
    """Build a compact classic red TNT canister bomb tucked neatly in the right hand."""
    hx, hy, hz = right_hand
    parts: List[MeshPart] = []

    body = cylinder(0.052, 0.055, 0.122, radial_segs=20, bevel=0.010, axis="y", name="bomb_body", material="gear_accent")
    parts.append(body)

    base_cap = (
        cylinder(0.055, 0.054, 0.022, radial_segs=20, bevel=0.004, axis="y", name="bomb_base_cap", material="gear_dark")
        .translate(0.0, -0.056, 0.0)
    )
    parts.append(base_cap)

    lid_band = (
        cylinder(0.057, 0.057, 0.024, radial_segs=20, bevel=0.004, axis="y", name="bomb_lid_band", material="gear_dark")
        .translate(0.0, 0.050, 0.0)
    )
    parts.append(lid_band)

    brass_band = (
        torus_arc(0.054, 0.0065, u_segs=20, v_segs=8, plane="xz", name="bomb_brass_band", material="gear_brass_trim")
        .translate(0.0, -0.004, 0.0)
    )
    parts.append(brass_band)

    wick_pts = [
        (0.0, 0.062, 0.0),
        (-0.008, 0.104, 0.010),
        (-0.028, 0.132, 0.022),
        (-0.042, 0.124, 0.032),
    ]
    wick = curved_tube(
        wick_pts,
        [0.009, 0.0085, 0.0075, 0.0068],
        samples_per_seg=4,
        radial_segs=8,
        cap_rings=2,
        name="bomb_wick",
        material="fuse_white",
    )
    parts.append(wick)

    spark = sphere(0.0115, u_segs=10, v_segs=8, name="bomb_spark", material="gear_gold").translate(*wick_pts[-1])
    parts.append(spark)

    # Tucked neatly at the right hand well inside Bomber's compact silhouette width (~0.56).
    for part in parts:
        placed = (
            part.rotate_xyz(rx=14.0, ry=10.0, rz=8.0, degrees=True)
            .translate(hx - 0.016, hy + 0.032, hz + 0.044)
        )
        scene.add("handheld_bomb", placed)


def build_bomber(blink: float = 0.0) -> SceneBuilder:
    """Build the sculpted 3D Bomber Ant SceneBuilder with intact Blink animation."""
    scene, anchors = build_base_ant(
        scene_name="ant_bomber",
        include_antennae=True,
        include_eyes=True,
        include_brows=False,
        left_arm_pose="idle",
        right_arm_pose=_BOMBER_RIGHT_ARM,
        blink=blink,
    )
    _build_goggles(scene, anchors["head_center"])
    _build_dynamite_pack(scene, anchors["back_anchor"])
    _build_handheld_bomb(scene, anchors["right_hand"])
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical entry point discovered by build_all.py."""
    return build_bomber(blink=blink)
