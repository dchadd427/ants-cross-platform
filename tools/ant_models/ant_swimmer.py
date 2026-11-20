"""Stylized 3D Swimmer Ant figurine (oval scuba mask, tall curved snorkel, handheld spade shovel).

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet grounded at Y = 0.0)
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
        capsule,
        curved_tube,
        cylinder,
        lathe,
        rounded_box,
        superellipsoid,
        swept_tube,
        torus_arc,
    )
except ImportError:
    from base_ant import build_base_ant  # type: ignore
    from mesh_core import (  # type: ignore
        MeshPart,
        SceneBuilder,
        capsule,
        curved_tube,
        cylinder,
        lathe,
        rounded_box,
        superellipsoid,
        swept_tube,
        torus_arc,
    )


def _spow(val: float, exp: float) -> float:
    return math.copysign(abs(val) ** exp, val)


def _mask_wrap_z(x: float, z_front: float, curve_depth: float = 0.068, half_w: float = 0.212) -> float:
    """Wrap mask geometry smoothly around the front curve of the cranium."""
    nx = x / max(1e-6, half_w)
    return z_front - curve_depth * (nx * nx)


def _build_scuba_mask(scene: SceneBuilder, head_center: Tuple[float, float, float]) -> Tuple[float, float, float]:
    """Build oversized oval yellow scuba mask, rubber skirt, tinted visor, and rear head strap.

    Returns the left-side temple strap anchor `(x, y, z)` for mounting the snorkel keeper clip.
    """
    hx, hy, hz = head_center
    mask_y = hy - 0.010
    mask_z_front = hz + 0.304  # ~0.360, proud of head_front (~0.298)
    rx_frame = 0.216
    ry_frame = 0.132
    oval_exp = 0.72

    # 1. Thick beveled oval vinyl-gold scuba mask outer frame wrapping gently around the face.
    n_loop = 48
    frame_pts: List[Tuple[float, float, float]] = []
    skirt_pts: List[Tuple[float, float, float]] = []
    for i in range(n_loop):
        t = (i / n_loop) * 2.0 * math.pi
        ox = rx_frame * _spow(math.cos(t), oval_exp)
        oy = ry_frame * _spow(math.sin(t), oval_exp)
        # Subtle classic scuba-mask nose-pocket dip at bottom center.
        nose_notch = 0.014 * math.exp(-((ox / 0.055) ** 2)) * max(0.0, -math.sin(t))
        fz = _mask_wrap_z(ox, mask_z_front, curve_depth=0.066, half_w=rx_frame)
        frame_pts.append((hx + ox, mask_y + oy - nose_notch, fz))
        # Dark rubber face skirt sits snug right behind the frame, extending back into the cranium.
        skirt_pts.append((hx + ox * 0.96, mask_y + oy * 0.94 - nose_notch * 0.7, fz - 0.036))

    frame = swept_tube(
        frame_pts,
        (0.025, 0.030),
        radial_segs=14,
        closed=True,
        name="scuba_mask_frame",
        material="gear_gold",
    )
    scene.add("scuba_mask", frame)

    # Subtle metallic inner accent rim beading inside the yellow frame.
    inner_pts = [
        (hx + (px - hx) * 0.91, mask_y + (py - mask_y) * 0.89, pz + 0.012)
        for px, py, pz in frame_pts
    ]
    inner_rim = swept_tube(
        inner_pts,
        0.0085,
        radial_segs=10,
        closed=True,
        name="scuba_mask_trim",
        material="gear_gold_metal",
    )
    scene.add("scuba_mask", inner_rim)

    # 2. Chunky dark rubber sealing skirt bridging the yellow frame cleanly into the head.
    skirt_ring = swept_tube(
        skirt_pts,
        (0.028, 0.044),
        radial_segs=14,
        closed=True,
        name="scuba_mask_skirt",
        material="gear_dark",
    )
    scene.add("scuba_mask", skirt_ring)

    # Sealed dark backing cushion inside the mask cavity so head chitin never clips through the lens.
    def _wrap_visor(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        nx = p[:, 0] / rx_frame
        p[:, 2] -= 0.066 * (nx * nx)
        # Match subtle bottom nose contour.
        nose = 0.010 * np.exp(-((p[:, 0] / 0.055) ** 2)) * np.clip(-p[:, 1] / ry_frame, 0.0, 1.0)
        p[:, 1] -= nose
        return p

    mask_backing = (
        superellipsoid(rx_frame * 0.92, ry_frame * 0.88, 0.052, e1=0.72, e2=0.74, u_segs=28, v_segs=16, name="scuba_mask_backing", material="gear_dark")
        .deform(_wrap_visor)
        .translate(hx, mask_y, mask_z_front - 0.046)
    )
    scene.add("scuba_mask", mask_backing)

    # 3. Large glossy curved dark-teal tinted visor seated cleanly inside the yellow frame.
    visor = (
        superellipsoid(rx_frame * 0.92, ry_frame * 0.88, 0.028, e1=0.70, e2=0.74, u_segs=32, v_segs=18, name="scuba_visor", material="glass_tinted")
        .deform(_wrap_visor)
        .translate(hx, mask_y, mask_z_front + 0.004)
    )
    scene.add("scuba_mask", visor)

    # 4. Wide dark rubber head strap + side temple strap buckles wrapping snugly around cranium.
    rx_strap = 0.264
    rz_strap = 0.254
    strap_pts: List[Tuple[float, float, float]] = []
    n_arc = 28
    start_ang = math.radians(52.0)
    end_ang = math.radians(308.0)
    for i in range(n_arc + 1):
        ang = start_ang + (end_ang - start_ang) * (i / n_arc)
        sx = hx + rx_strap * _spow(math.sin(ang), 0.80)
        sz = hz + rz_strap * _spow(math.cos(ang), 0.80)
        rear_lift = 0.016 * (0.5 * (1.0 - math.cos(ang)))
        strap_pts.append((sx, mask_y + 0.006 + rear_lift, sz))

    strap = swept_tube(
        strap_pts,
        (0.042, 0.017),
        radial_segs=14,
        cap_start=True,
        cap_end=True,
        closed=False,
        cap_rings=3,
        name="scuba_strap",
        material="gear_dark",
    )
    scene.add("scuba_mask", strap)

    # Sculpted side temple buckle clips anchoring the strap to the mask frame.
    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        buckle = (
            rounded_box(0.044, 0.076, 0.078, bevel=0.014, segs=3, name=f"scuba_buckle_{tag}", material="gear_gold")
            .rotate_xyz(ry=side_sign * 0.30)
            .translate(hx + side_sign * 0.228, mask_y + 0.004, hz + 0.194)
        )
        scene.add("scuba_mask", buckle)

    # Rear strap twin pad/buckle on back of head for crisp rear-view craft.
    rear_pad = (
        rounded_box(0.112, 0.094, 0.034, bevel=0.012, segs=3, name="scuba_rear_pad", material="gear_dark")
        .translate(hx, mask_y + 0.022, hz - rz_strap - 0.006)
    )
    scene.add("scuba_mask", rear_pad)

    rear_clip = (
        rounded_box(0.056, 0.068, 0.038, bevel=0.008, segs=2, name="scuba_rear_clip", material="gear_gold_metal")
        .translate(hx, mask_y + 0.022, hz - rz_strap - 0.009)
    )
    scene.add("scuba_mask", rear_clip)

    # Return left temple clip anchor (+X side) where the snorkel keeper attaches.
    return (hx + 0.258, mask_y + 0.012, hz + 0.160)


def _build_snorkel(scene: SceneBuilder, head_center: Tuple[float, float, float], clip_anchor: Tuple[float, float, float]) -> None:
    """Build tall curved yellow snorkel on ant's LEFT (+X) side with mouthpiece, keeper clamp, and splash guard."""
    hx, hy, hz = head_center
    cx, cy, cz = clip_anchor

    # Mouthpiece block seated cleanly near the ant's mouth / lower-left cheek.
    mouthpiece = (
        rounded_box(0.092, 0.050, 0.058, bevel=0.016, segs=3, name="snorkel_mouthpiece", material="gear_dark")
        .rotate_xyz(ry=-0.35)
        .translate(hx + 0.088, hy - 0.158, hz + 0.246)
    )
    scene.add("snorkel", mouthpiece)

    # Lower flexible dark rubber elbow curving from mouthpiece around the left cheek to the tube base.
    flex_pts = [
        (hx + 0.112, hy - 0.158, hz + 0.252),
        (hx + 0.205, hy - 0.158, hz + 0.242),
        (hx + 0.278, hy - 0.125, hz + 0.202),
        (cx + 0.034, cy - 0.078, cz + 0.022),
    ]
    flex_hose = curved_tube(
        flex_pts,
        [0.025, 0.029, 0.031, 0.030],
        samples_per_seg=5,
        radial_segs=14,
        cap_rings=3,
        name="snorkel_flex_elbow",
        material="gear_dark",
    )
    scene.add("snorkel", flex_hose)

    # Brass collar ring joining the dark lower flex hose into the main yellow vertical snorkel barrel.
    joint_collar = (
        cylinder(0.035, 0.036, 0.028, radial_segs=18, bevel=0.005, axis="y", name="snorkel_joint_collar", material="gear_gold_metal")
        .rotate_xyz(rx=-0.10, rz=-0.06)
        .translate(cx + 0.034, cy - 0.068, cz + 0.020)
    )
    scene.add("snorkel", joint_collar)

    # Main tall smooth curved yellow snorkel barrel rising proudly above the head & left antenna.
    top_x = cx + 0.036
    top_y = hy + 0.450  # ~1.172, cleanly above head top (0.95) and antenna (~1.12)
    top_z = cz - 0.042
    tube_pts = [
        (cx + 0.034, cy - 0.068, cz + 0.020),
        (cx + 0.038, cy + 0.015, cz + 0.008),
        (cx + 0.042, hy + 0.175, cz - 0.012),
        (cx + 0.040, hy + 0.335, cz - 0.030),
        (top_x, top_y, top_z),
    ]
    main_tube = curved_tube(
        tube_pts,
        [0.032, 0.033, 0.034, 0.035, 0.036],
        samples_per_seg=5,
        radial_segs=18,
        cap_start=True,
        cap_end=False,
        cap_rings=3,
        name="snorkel_tube",
        material="gear_gold",
    )
    scene.add("snorkel", main_tube)

    # Snorkel keeper clamp securing the vertical tube directly onto the left scuba mask strap.
    keeper = (
        rounded_box(0.088, 0.056, 0.068, bevel=0.014, segs=3, name="snorkel_keeper_clamp", material="gear_dark")
        .translate(cx + 0.012, cy + 0.006, cz + 0.008)
    )
    scene.add("snorkel", keeper)

    keeper_ring = (
        torus_arc(0.038, 0.008, u_segs=20, v_segs=10, plane="xz", name="snorkel_keeper_ring", material="gear_gold_metal")
        .translate(cx + 0.038, cy + 0.036, cz + 0.006)
    )
    scene.add("snorkel", keeper_ring)

    # Sculpted top splash-guard collar + dark open bore cap so it reads crisply from 50-deg isometric view.
    cap_profile = [
        (0.036, 0.000),
        (0.044, 0.014),
        (0.046, 0.048),
        (0.039, 0.058),
        (0.026, 0.058),
        (0.024, 0.036),
    ]
    top_cap = (
        lathe(cap_profile, radial_segs=22, cap_bottom=True, cap_top=True, axis="y", name="snorkel_splash_guard", material="gear_gold")
        .rotate_xyz(rx=-0.08)
        .translate(top_x, top_y - 0.006, top_z)
    )
    scene.add("snorkel", top_cap)

    top_band = (
        cylinder(0.047, 0.047, 0.018, radial_segs=22, bevel=0.004, axis="y", name="snorkel_top_band", material="gear_dark")
        .rotate_xyz(rx=-0.08)
        .translate(top_x, top_y + 0.020, top_z - 0.002)
    )
    scene.add("snorkel", top_band)

    # Recessed dark inner air-intake bore plug visible from overhead views.
    top_bore = (
        cylinder(0.027, 0.026, 0.018, radial_segs=18, bevel=0.003, axis="y", name="snorkel_intake_bore", material="gear_dark")
        .rotate_xyz(rx=-0.08)
        .translate(top_x, top_y + 0.044, top_z - 0.004)
    )
    scene.add("snorkel", top_bore)


def _build_shovel(scene: SceneBuilder, right_hand: Tuple[float, float, float]) -> None:
    """Build handheld yellow spade/shovel anchored cleanly in the ant's right hand (-X side)."""
    hx, hy, hz = right_hand
    parts: List[MeshPart] = []

    # Build shovel in local coordinates along +Y (grip at y=0, D-handle at -Y, broad spade blade at +Y).
    # 1. Main cylindrical yellow shaft.
    shaft = (
        cylinder(0.021, 0.024, 0.295, radial_segs=16, bevel=0.004, axis="y", name="shovel_shaft", material="gear_gold")
        .translate(0.0, 0.018, 0.0)
    )
    parts.append(shaft)

    # Dark textured grip sleeve right where the ant's right hand clasps the shaft.
    grip_wrap = (
        cylinder(0.026, 0.026, 0.086, radial_segs=16, bevel=0.005, axis="y", name="shovel_grip_wrap", material="gear_dark")
        .translate(0.0, 0.000, 0.0)
    )
    parts.append(grip_wrap)

    # 2. Sculpted D-grip handle at the rear/top end (y = -0.13 to -0.24).
    d_collar = (
        cylinder(0.027, 0.025, 0.032, radial_segs=16, bevel=0.005, axis="y", name="shovel_d_collar", material="gear_gold_metal")
        .translate(0.0, -0.126, 0.0)
    )
    parts.append(d_collar)

    d_arch = curved_tube(
        [
            (-0.048, -0.224, 0.0),
            (-0.058, -0.175, 0.0),
            (-0.034, -0.138, 0.0),
            (0.000, -0.128, 0.0),
            (0.034, -0.138, 0.0),
            (0.058, -0.175, 0.0),
            (0.048, -0.224, 0.0),
        ],
        0.016,
        samples_per_seg=4,
        radial_segs=12,
        cap_rings=2,
        name="shovel_d_arch",
        material="gear_gold",
    )
    parts.append(d_arch)

    d_bar = (
        capsule(0.019, 0.094, radial_segs=14, cap_rings=4, axis="x", name="shovel_d_grip", material="gear_dark")
        .translate(0.0, -0.224, 0.0)
    )
    parts.append(d_bar)

    # 3. Brass ferrule socket connecting shaft to spade blade.
    ferrule = (
        cylinder(0.027, 0.036, 0.066, radial_segs=18, bevel=0.006, axis="y", name="shovel_ferrule", material="gear_gold_metal")
        .translate(0.0, 0.172, 0.006)
    )
    parts.append(ferrule)

    # 4. Broad, chunky, friendly cupped rounded-spade blade sculpted via smooth vertex deformation.
    blade_w = 0.204
    blade_l = 0.222
    blade_cy = 0.290

    def _spade_half_width_factor(u_arr: np.ndarray) -> np.ndarray:
        # Full broad spade shoulders for u in [0, 0.52], smoothly rounding to a chunky vinyl tip at u=1.
        t = np.clip((u_arr - 0.52) / 0.48, 0.0, 1.0)
        return np.sqrt(np.maximum(0.04, 1.0 - (t ** 2.15) * 0.90))

    def _sculpt_spade(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        u = np.clip((p[:, 1] + 0.5 * blade_l) / blade_l, 0.0, 1.0)
        p[:, 0] *= _spade_half_width_factor(u)

        # Gentle cupped scoop dish (edges curl slightly toward +Z = upward when held).
        nx = p[:, 0] / (0.5 * blade_w)
        cup_z = 0.026 * (nx * nx) + 0.020 * (u * u)
        # Keep plump beveled thickness at shoulders, gently tapering to a clean beveled digging edge.
        p[:, 2] = p[:, 2] * (1.0 - 0.28 * u) + cup_z
        return p

    blade_core = (
        rounded_box(blade_w, blade_l, 0.026, bevel=0.010, segs=4, name="shovel_blade", material="gear_gold")
        .deform(_sculpt_spade)
        .translate(0.0, blade_cy, -0.006)
    )
    parts.append(blade_core)

    # Stepped top foot-tread shoulders & raised center spine rib on the broad spade.
    tread_bar = (
        capsule(0.015, blade_w * 0.84, radial_segs=12, cap_rings=3, axis="x", name="shovel_tread_bar", material="gear_gold_metal")
        .translate(0.0, blade_cy - 0.5 * blade_l + 0.012, 0.010)
    )
    parts.append(tread_bar)

    blade_rib = (
        capsule(0.016, 0.088, radial_segs=12, cap_rings=3, axis="y", name="shovel_blade_rib", material="gear_gold_metal")
        .translate(0.0, blade_cy - 0.048, 0.010)
    )
    parts.append(blade_rib)

    # Flush metallic cutting bevel rim hugging strictly along the curved rounded digging edge (u >= 0.52).
    edge_pts: List[Tuple[float, float, float]] = []
    n_edge = 18
    for i in range(n_edge + 1):
        ang = -0.5 * math.pi + math.pi * (i / n_edge)  # -pi/2 (right) -> 0 (tip) -> +pi/2 (left)
        t = math.cos(ang)  # 0 at u=0.52 -> 1 at tip u=1.0
        u = 0.52 + 0.48 * t
        half_w = (0.5 * blade_w) * float(_spade_half_width_factor(np.array([u]))[0])
        ex = math.copysign(half_w, math.sin(ang)) * abs(math.sin(ang)) ** 0.75 if abs(ang) > 1e-4 else 0.0
        ey = (blade_cy - 0.5 * blade_l) + blade_l * u
        nx = ex / (0.5 * blade_w)
        ez = -0.006 + 0.026 * (nx * nx) + 0.020 * (u * u)
        edge_pts.append((ex, ey, ez))

    blade_edge = swept_tube(
        edge_pts,
        0.009,
        radial_segs=10,
        cap_start=True,
        cap_end=True,
        closed=False,
        cap_rings=2,
        name="shovel_blade_edge",
        material="gear_gold_metal",
    )
    parts.append(blade_edge)

    # Orient shovel cleanly forward & gently downward from right hand (-X) with the broad cupped
    # yellow spade face angled upward so it reads unmistakably from Front, 3/4, Side, and Back,
    # while staying comfortably above Y = 0.0.
    for part in parts:
        placed = (
            part.rotate_xyz(rx=112.0, ry=-10.0, rz=8.0, degrees=True)
            .translate(hx - 0.022, hy + 0.006, hz + 0.012)
        )
        scene.add("shovel", placed)


def build_swimmer() -> SceneBuilder:
    """Build the complete stylized 3D Swimmer Ant SceneBuilder."""
    scene, anchors = build_base_ant(
        scene_name="ant_swimmer",
        include_antennae=True,
        include_eyes=False,
        include_brows=False,
        left_arm_pose="idle",
        right_arm_pose="hold_tool",
    )
    clip_anchor = _build_scuba_mask(scene, anchors["head_center"])
    _build_snorkel(scene, anchors["head_center"], clip_anchor)
    _build_shovel(scene, anchors["right_hand"])
    return scene


def build_ant() -> SceneBuilder:
    """Canonical entry point discovered by build_all.py."""
    return build_swimmer()
