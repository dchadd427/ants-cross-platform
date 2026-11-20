"""Stylized 3D Bomber Ant figurine (twin demolition goggles, rear dynamite fan, handheld bomb).

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet grounded at Y = 0.0)
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


def _spow(val: float, exp: float) -> float:
    return math.copysign(abs(val) ** exp, val)


def _build_goggle_barrel(side_sign: float) -> List[MeshPart]:
    """Build one chunky binocular-style demolition goggle barrel for side (+1 left, -1 right)."""
    parts: List[MeshPart] = []
    tag = "left" if side_sign > 0 else "right"

    # Rear dark rubber eye-cup cushion hugging the cranium contour.
    cushion_profile = [
        (0.072, -0.088),
        (0.098, -0.074),
        (0.104, -0.042),
        (0.096, -0.020),
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

    # Chunky stepped vinyl-gold binocular barrel extending forward from the cushion.
    barrel_profile = [
        (0.092, -0.048),
        (0.102, -0.036),
        (0.104, 0.032),
        (0.096, 0.042),
        (0.106, 0.056),
        (0.108, 0.098),
        (0.100, 0.112),
        (0.084, 0.112),
        (0.078, 0.092),
    ]
    barrel = lathe(
        barrel_profile,
        radial_segs=28,
        cap_bottom=True,
        cap_top=True,
        axis="z",
        name=f"goggle_barrel_{tag}",
        material="gear_gold",
    )
    parts.append(barrel)

    # Metallic brass stepped front bezel ring framing the gleaming lens.
    bezel = torus_arc(
        0.094,
        0.015,
        u_segs=30,
        v_segs=12,
        plane="xy",
        name=f"goggle_bezel_{tag}",
        material="gear_gold_metal",
    ).scale(1.0, 1.0, 0.85).translate(0.0, 0.0, 0.106)
    parts.append(bezel)

    # Secondary mid-barrel brass reinforcement collar ring.
    mid_ring = torus_arc(
        0.103,
        0.0085,
        u_segs=28,
        v_segs=10,
        plane="xy",
        name=f"goggle_mid_ring_{tag}",
        material="gear_gold_metal",
    ).translate(0.0, 0.0, 0.046)
    parts.append(mid_ring)

    # Dark inner optical rim ring behind the lens edge for crisp contrast.
    inner_trim = torus_arc(
        0.082,
        0.007,
        u_segs=26,
        v_segs=8,
        plane="xy",
        name=f"goggle_inner_trim_{tag}",
        material="gear_dark",
    ).translate(0.0, 0.0, 0.096)
    parts.append(inner_trim)

    # Bright glossy slightly convex optical lens gleaming inside the front bezel.
    lens = (
        ellipsoid(0.082, 0.082, 0.024, u_segs=24, v_segs=14, name=f"goggle_lens_{tag}", material="glass_clear")
        .translate(0.0, 0.0, 0.094)
    )
    parts.append(lens)

    # Outer side brass rivet stud accent on the barrel temple.
    stud = (
        cylinder(0.016, 0.015, 0.022, radial_segs=14, bevel=0.004, axis="x", name=f"goggle_stud_{tag}", material="gear_accent")
        .translate(side_sign * 0.106, 0.0, 0.022)
    )
    parts.append(stud)

    return parts


def _build_goggles(scene: SceneBuilder, head_center: Tuple[float, float, float]) -> None:
    """Build twin protruding demolition goggles, nose bridge, side lugs, and snug head strap."""
    hx, hy, hz = head_center
    goggle_y = hy - 0.014
    goggle_z = hz + 0.258
    barrel_x = 0.114

    for side_sign in (+1.0, -1.0):
        yaw = side_sign * 0.11
        pitch = -0.08
        for part in _build_goggle_barrel(side_sign):
            placed = (
                part.rotate_xyz(rx=pitch, ry=yaw)
                .translate(hx + side_sign * barrel_x, goggle_y, goggle_z)
            )
            scene.add("goggles", placed)

    # Sculpted nose bridge connecting the twin barrels cleanly.
    bridge = (
        rounded_box(0.084, 0.048, 0.056, bevel=0.014, segs=3, name="goggle_bridge", material="gear_gold")
        .translate(hx, goggle_y + 0.010, goggle_z + 0.036)
    )
    scene.add("goggles", bridge)

    bridge_pin = (
        cylinder(0.016, 0.016, 0.018, radial_segs=14, bevel=0.004, axis="z", name="goggle_bridge_bolt", material="gear_gold_metal")
        .translate(hx, goggle_y + 0.012, goggle_z + 0.066)
    )
    scene.add("goggles", bridge_pin)

    # Side strap-mount temple brackets and brass pivot rivets.
    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        lug = (
            rounded_box(0.042, 0.066, 0.072, bevel=0.012, segs=3, name=f"goggle_lug_{tag}", material="gear_gold_metal")
            .rotate_xyz(ry=side_sign * 0.28)
            .translate(hx + side_sign * 0.236, goggle_y + 0.002, hz + 0.168)
        )
        scene.add("goggles", lug)

    # Snug dark rubber strap wrapping smoothly around the sides and back of the cranium.
    rx_strap = 0.264
    rz_strap = 0.254
    strap_pts: List[Tuple[float, float, float]] = []
    n_arc = 28
    # Sweep from right temple (~-55 deg) through back (-180 / +180 deg) to left temple (+55 deg).
    start_ang = math.radians(52.0)
    end_ang = math.radians(308.0)
    for i in range(n_arc + 1):
        ang = start_ang + (end_ang - start_ang) * (i / n_arc)
        # Match superellipsoid head e2=0.80 contour so strap hugs head uniformly.
        sx = hx + rx_strap * _spow(math.sin(ang), 0.80)
        sz = hz + rz_strap * _spow(math.cos(ang), 0.80)
        # Slight upward contour at the rear so strap sits snugly over the head equator.
        rear_lift = 0.012 * (0.5 * (1.0 - math.cos(ang)))
        strap_pts.append((sx, goggle_y + 0.004 + rear_lift, sz))

    strap = swept_tube(
        strap_pts,
        (0.036, 0.016),
        radial_segs=14,
        cap_start=True,
        cap_end=True,
        closed=False,
        cap_rings=3,
        name="goggle_strap",
        material="gear_dark",
    )
    scene.add("goggles", strap)

    # Brass rear strap adjuster buckle centered cleanly at the back of the head.
    rear_buckle = (
        rounded_box(0.088, 0.082, 0.032, bevel=0.010, segs=3, name="goggle_rear_buckle", material="gear_gold_metal")
        .translate(hx, goggle_y + 0.016, hz - rz_strap - 0.006)
    )
    scene.add("goggles", rear_buckle)


def _build_dynamite_stick(index: int, stick_len: float, stick_r: float, fuse_curve_x: float) -> List[MeshPart]:
    """Build a single sculpted crimson dynamite stick along local +Y (base at Y = 0, top at Y = stick_len)."""
    parts: List[MeshPart] = []

    # Main smooth beveled crimson dynamite cylinder anchored near its base for clean outward fanning.
    casing = (
        cylinder(
            stick_r,
            stick_r,
            stick_len,
            radial_segs=20,
            bevel=0.009,
            axis="y",
            name=f"dynamite_stick_{index}",
            material="gear_accent",
        )
        .translate(0.0, 0.5 * stick_len, 0.0)
    )
    parts.append(casing)

    # Twin dark rubber/tape binding bands around the stick.
    for b_idx, frac in enumerate((0.28, 0.74)):
        band = (
            cylinder(
                stick_r + 0.0045,
                stick_r + 0.0045,
                0.030,
                radial_segs=20,
                bevel=0.004,
                axis="y",
                name=f"dynamite_band_{index}_{b_idx}",
                material="gear_dark",
            )
            .translate(0.0, frac * stick_len, 0.0)
        )
        parts.append(band)

    # Top metallic crimp collar and dark fuse grommet plug.
    collar = (
        torus_arc(stick_r * 0.86, 0.008, u_segs=18, v_segs=10, plane="xz", name=f"dynamite_collar_{index}", material="gear_gold_metal")
        .translate(0.0, stick_len - 0.004, 0.0)
    )
    parts.append(collar)

    plug = (
        cylinder(stick_r * 0.62, stick_r * 0.54, 0.018, radial_segs=14, bevel=0.003, axis="y", name=f"dynamite_plug_{index}", material="gear_dark")
        .translate(0.0, stick_len + 0.004, 0.0)
    )
    parts.append(plug)

    # Expressive curved white fuse arching outward and upward from the top plug.
    fuse_pts = [
        (0.0, stick_len + 0.008, 0.0),
        (0.32 * fuse_curve_x, stick_len + 0.068, -0.010),
        (0.80 * fuse_curve_x, stick_len + 0.122, -0.020),
        (1.12 * fuse_curve_x, stick_len + 0.156, -0.012),
    ]
    fuse = curved_tube(
        fuse_pts,
        [0.012, 0.011, 0.010, 0.0085],
        samples_per_seg=5,
        radial_segs=10,
        cap_rings=3,
        name=f"dynamite_fuse_{index}",
        material="fuse_white",
    )
    parts.append(fuse)

    # Tiny warm gold igniter spark nub at the fuse tip for crisp silhouette punctuation.
    tip = sphere(0.015, u_segs=12, v_segs=10, name=f"dynamite_spark_{index}", material="gear_gold").translate(*fuse_pts[-1])
    parts.append(tip)

    return parts


def _build_dynamite_pack(scene: SceneBuilder, back_anchor: Tuple[float, float, float]) -> None:
    """Build buckled rear harness and spiky 4-stick crimson dynamite fan behind shoulders/head."""
    bx, by, bz = back_anchor
    pack_y = by + 0.115  # ~0.480, sitting snugly on upper dorsal thorax behind neck
    pack_z = bz - 0.052  # ~-0.207, flush against upper back and clear of gaster/head

    # Sculpted curved dark harness backplate resting cleanly on upper thorax.
    backplate = (
        rounded_box(0.245, 0.175, 0.068, bevel=0.022, segs=3, name="pack_backplate", material="gear_dark")
        .rotate_xyz(rx=-0.38)
        .translate(bx, pack_y - 0.010, pack_z + 0.024)
    )
    scene.add("dynamite_pack", backplate)

    # Twin dark shoulder harness straps looping cleanly from backplate over shoulders to chest.
    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        s_pts = [
            (side_sign * 0.095, pack_y + 0.028, pack_z + 0.032),
            (side_sign * 0.118, 0.482, -0.055),
            (side_sign * 0.112, 0.466, 0.068),
            (side_sign * 0.092, 0.370, 0.136),
            (side_sign * 0.088, 0.300, 0.125),
        ]
        s_strap = curved_tube(
            s_pts,
            (0.014, 0.022),
            samples_per_seg=5,
            radial_segs=10,
            cap_rings=2,
            name=f"harness_shoulder_{tag}",
            material="gear_dark",
        )
        scene.add("dynamite_pack", s_strap)

        # Metallic brass buckle badge on the front chest strap.
        chest_buckle = (
            rounded_box(0.038, 0.044, 0.024, bevel=0.006, segs=2, name=f"harness_buckle_{tag}", material="gear_gold_metal")
            .rotate_xyz(rx=-0.16, ry=side_sign * 0.34)
            .translate(side_sign * 0.094, 0.376, 0.140)
        )
        scene.add("dynamite_pack", chest_buckle)

    # Horizontal waist/chest cinch belt across the front chest connecting both shoulder straps.
    chest_belt = curved_tube(
        [
            (-0.118, 0.312, 0.082),
            (-0.072, 0.312, 0.144),
            (0.000, 0.312, 0.158),
            (0.072, 0.312, 0.144),
            (0.118, 0.312, 0.082),
        ],
        (0.018, 0.012),
        samples_per_seg=4,
        radial_segs=10,
        cap_rings=2,
        name="harness_chest_belt",
        material="gear_dark",
    )
    scene.add("dynamite_pack", chest_belt)

    # Curved harness retention belt & brass buckle clamping the bundled base of the dynamite fan.
    rack_pts = [
        (-0.185, pack_y + 0.078, pack_z - 0.030),
        (-0.105, pack_y + 0.096, pack_z - 0.088),
        (0.000, pack_y + 0.102, pack_z - 0.106),
        (0.105, pack_y + 0.096, pack_z - 0.088),
        (0.185, pack_y + 0.078, pack_z - 0.030),
    ]
    rack_strap = curved_tube(
        rack_pts,
        (0.026, 0.016),
        samples_per_seg=5,
        radial_segs=12,
        cap_rings=3,
        name="dynamite_rack_strap",
        material="gear_dark",
    )
    scene.add("dynamite_pack", rack_strap)

    rack_buckle = (
        rounded_box(0.076, 0.068, 0.034, bevel=0.009, segs=3, name="dynamite_rack_buckle", material="gear_gold_metal")
        .rotate_xyz(rx=-0.42)
        .translate(0.0, pack_y + 0.102, pack_z - 0.114)
    )
    scene.add("dynamite_pack", rack_buckle)

    # Spiky OUTWARD fan of 4 sculpted crimson dynamite sticks splayed boldly behind head & shoulders.
    # Note: positive rz tilts +Y toward -X (ant right); negative rz tilts +Y toward +X (ant left).
    # Each tuple: (splay_rz_deg, tilt_back_rx_deg, base_ox, base_oy, base_oz, length, fuse_curve_x).
    stick_specs = (
        (+40.0, -25.0, -0.096, 0.024, -0.018, 0.365, -0.048),
        (+15.0, -31.0, -0.038, 0.036, -0.038, 0.395, -0.022),
        (-15.0, -31.0, +0.038, 0.036, -0.038, 0.395, +0.022),
        (-40.0, -25.0, +0.096, 0.024, -0.018, 0.365, +0.048),
    )
    for idx, (rz_deg, rx_deg, ox, oy, oz, slen, fuse_dx) in enumerate(stick_specs):
        for part in _build_dynamite_stick(idx, slen, 0.048, fuse_dx):
            placed = (
                part.rotate_xyz(rx=rx_deg, rz=rz_deg, degrees=True)
                .translate(bx + ox, pack_y + oy, pack_z + oz)
            )
            scene.add("dynamite_pack", placed)


def _build_handheld_bomb(scene: SceneBuilder, right_hand: Tuple[float, float, float]) -> None:
    """Build a compact classic red TNT canister bomb gripped cleanly in the right hand."""
    hx, hy, hz = right_hand
    parts: List[MeshPart] = []

    # Compact crimson TNT bomb canister body (echoing the original powerup bomb sprite).
    body = cylinder(0.066, 0.070, 0.148, radial_segs=20, bevel=0.012, axis="y", name="bomb_body", material="gear_accent")
    parts.append(body)

    # Dark rubber bottom base & top lid collar.
    base_cap = (
        cylinder(0.070, 0.068, 0.026, radial_segs=20, bevel=0.005, axis="y", name="bomb_base_cap", material="gear_dark")
        .translate(0.0, -0.068, 0.0)
    )
    parts.append(base_cap)

    lid_band = (
        cylinder(0.072, 0.072, 0.030, radial_segs=20, bevel=0.005, axis="y", name="bomb_lid_band", material="gear_dark")
        .translate(0.0, 0.060, 0.0)
    )
    parts.append(lid_band)

    # Brass waist trim ring for gleaming close-up detail.
    brass_band = (
        torus_arc(0.069, 0.0075, u_segs=20, v_segs=10, plane="xz", name="bomb_brass_band", material="gear_gold_metal")
        .translate(0.0, -0.004, 0.0)
    )
    parts.append(brass_band)

    # Curved white wick arching playfully out of the canister top.
    wick_pts = [
        (0.0, 0.074, 0.0),
        (-0.012, 0.128, 0.014),
        (-0.044, 0.165, 0.030),
        (-0.066, 0.154, 0.046),
    ]
    wick = curved_tube(
        wick_pts,
        [0.011, 0.010, 0.009, 0.008],
        samples_per_seg=4,
        radial_segs=10,
        cap_rings=2,
        name="bomb_wick",
        material="fuse_white",
    )
    parts.append(wick)

    spark = sphere(0.014, u_segs=12, v_segs=10, name="bomb_spark", material="gear_gold").translate(*wick_pts[-1])
    parts.append(spark)

    # Seat cleanly in the ant's right hand, angled slightly forward-outward well above ground.
    for part in parts:
        placed = (
            part.rotate_xyz(rx=16.0, ry=14.0, rz=16.0, degrees=True)
            .translate(hx - 0.024, hy + 0.040, hz + 0.056)
        )
        scene.add("handheld_bomb", placed)


def build_bomber() -> SceneBuilder:
    """Build the complete stylized 3D Bomber Ant SceneBuilder."""
    scene, anchors = build_base_ant(
        scene_name="ant_bomber",
        include_antennae=True,
        include_eyes=False,
        include_brows=False,
        left_arm_pose="idle",
        right_arm_pose="hold_tool",
    )
    _build_goggles(scene, anchors["head_center"])
    _build_dynamite_pack(scene, anchors["back_anchor"])
    _build_handheld_bomb(scene, anchors["right_hand"])
    return scene


def build_ant() -> SceneBuilder:
    """Canonical entry point discovered by build_all.py."""
    return build_bomber()
