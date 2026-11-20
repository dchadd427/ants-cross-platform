"""Sculpted 3D Swimmer Ant (scuba mask, banded snorkel, brushed-steel air tank, flippers, spade shovel).

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet and diving flippers grounded solidly at Y = 0.0)
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

# Compact right-arm hold keeping the yellow spade tucked cleanly inside Swimmer's authentic ~0.64-0.70 width.
_SWIMMER_RIGHT_ARM: List[Tuple[float, float, float]] = [
    (-0.114, 0.438, 0.022),
    (-0.214, 0.408, 0.090),
    (-0.214, 0.412, 0.210),
    (-0.204, 0.428, 0.262),
]


def _spow(val: float, exp: float) -> float:
    return math.copysign(abs(val) ** exp, val)


def _mask_wrap_z(x: float, z_front: float, curve_depth: float = 0.064, half_w: float = 0.204) -> float:
    """Wrap scuba mask geometry smoothly around the front curve of the cranium."""
    nx = x / max(1e-6, half_w)
    return z_front - curve_depth * (nx * nx)


def _build_scuba_mask(scene: SceneBuilder, head_center: Tuple[float, float, float]) -> Tuple[float, float, float]:
    """Build weathered oval yellow scuba mask, rubber skirt, recessed dark visor, and rear strap.

    Returns the left-side temple strap anchor `(x, y, z)` for mounting the banded snorkel keeper.
    """
    hx, hy, hz = head_center
    mask_y = hy - 0.008  # ~0.716, centered over Pass 2 eyes (0.712)
    mask_z_front = hz + 0.298  # ~0.348, cleanly clear of blinking eyelids (Z <= 0.319)
    rx_frame = 0.202
    ry_frame = 0.124
    oval_exp = 0.72

    n_loop = 48
    frame_pts: List[Tuple[float, float, float]] = []
    skirt_pts: List[Tuple[float, float, float]] = []
    for i in range(n_loop):
        t = (i / n_loop) * 2.0 * math.pi
        ox = rx_frame * _spow(math.cos(t), oval_exp)
        oy = ry_frame * _spow(math.sin(t), oval_exp)
        nose_notch = 0.012 * math.exp(-((ox / 0.052) ** 2)) * max(0.0, -math.sin(t))
        fz = _mask_wrap_z(ox, mask_z_front, curve_depth=0.064, half_w=rx_frame)
        frame_pts.append((hx + ox, mask_y + oy - nose_notch, fz))
        skirt_pts.append((hx + ox * 0.96, mask_y + oy * 0.94 - nose_notch * 0.7, fz - 0.038))

    # 1. Thick weathered mustard-yellow rounded-oval outer scuba frame.
    frame = swept_tube(
        frame_pts,
        (0.025, 0.030),
        radial_segs=14,
        closed=True,
        name="scuba_mask_frame",
        material="gear_gold",
    )
    scene.add("scuba_mask", frame)

    # Subtle recessed dark inner bezel lip giving the visor deep physical set-in depth.
    inner_pts = [
        (hx + (px - hx) * 0.90, mask_y + (py - mask_y) * 0.88, pz + 0.008)
        for px, py, pz in frame_pts
    ]
    inner_bezel = swept_tube(
        inner_pts,
        0.009,
        radial_segs=10,
        closed=True,
        name="scuba_mask_inner_bezel",
        material="gear_dark",
    )
    scene.add("scuba_mask", inner_bezel)

    # 2. Dark rubber face sealing skirt bridging the yellow frame solidly onto the cranium.
    skirt_ring = swept_tube(
        skirt_pts,
        (0.026, 0.048),
        radial_segs=14,
        closed=True,
        name="scuba_mask_skirt",
        material="gear_dark",
    )
    scene.add("scuba_mask", skirt_ring)

    def _wrap_visor(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        nx = p[:, 0] / rx_frame
        p[:, 2] -= 0.064 * (nx * nx)
        nose = 0.009 * np.exp(-((p[:, 0] / 0.052) ** 2)) * np.clip(-p[:, 1] / ry_frame, 0.0, 1.0)
        p[:, 1] -= nose
        return p

    # Recessed glossy dark slate-teal visor seated cleanly ahead of blinking eyelids inside.
    visor = (
        superellipsoid(rx_frame * 0.91, ry_frame * 0.87, 0.022, e1=0.70, e2=0.74, u_segs=30, v_segs=18, name="scuba_visor", material="glass_tinted")
        .deform(_wrap_visor)
        .translate(hx, mask_y, mask_z_front - 0.004)
    )
    scene.add("scuba_mask", visor)

    # 3. Snug dark rubber rear head strap wrapping cleanly around the Pass 2 cranium.
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
        rear_lift = 0.015 * (0.5 * (1.0 - math.cos(ang)))
        strap_pts.append((sx, mask_y + 0.006 + rear_lift, sz))

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

    # Temple strap anchor blocks.
    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        buckle = (
            rounded_box(0.036, 0.068, 0.068, bevel=0.012, segs=3, name=f"scuba_buckle_{tag}", material="gear_gold")
            .rotate_xyz(ry=side_sign * 0.30)
            .translate(hx + side_sign * 0.214, mask_y + 0.004, hz + 0.174)
        )
        scene.add("scuba_mask", buckle)

    # Rear strap pad + brushed-steel adjuster buckle on the back of the cranium.
    rear_pad = (
        rounded_box(0.100, 0.084, 0.030, bevel=0.010, segs=3, name="scuba_rear_pad", material="gear_dark")
        .translate(hx, mask_y + 0.020, hz - rz_strap - 0.005)
    )
    scene.add("scuba_mask", rear_pad)

    rear_clip = (
        rounded_box(0.050, 0.060, 0.034, bevel=0.007, segs=2, name="scuba_rear_clip", material="gear_steel")
        .translate(hx, mask_y + 0.020, hz - rz_strap - 0.008)
    )
    scene.add("scuba_mask", rear_clip)

    return (hx + 0.244, mask_y + 0.010, hz + 0.150)


def _build_snorkel(scene: SceneBuilder, head_center: Tuple[float, float, float], clip_anchor: Tuple[float, float, float]) -> None:
    """Build two-tone tactical banded vertical snorkel on ant's LEFT (+X) side."""
    hx, hy, hz = head_center
    cx, cy, cz = clip_anchor
    tube_x = cx + 0.034  # ~+0.278, tucked cleanly right beside the left mask edge
    tube_z = cz + 0.012  # ~+0.212

    # Dark rubber mouthpiece seated near the lower-left mandible/cheek.
    mouthpiece = (
        rounded_box(0.082, 0.044, 0.050, bevel=0.014, segs=3, name="snorkel_mouthpiece", material="gear_dark")
        .rotate_xyz(ry=-0.35)
        .translate(hx + 0.084, hy - 0.156, hz + 0.232)
    )
    scene.add("snorkel", mouthpiece)

    # Lower dark curved flex-hose sweeping from mouthpiece around left cheek into vertical tactical shaft.
    flex_pts = [
        (hx + 0.106, hy - 0.156, hz + 0.238),
        (hx + 0.194, hy - 0.154, hz + 0.232),
        (tube_x - 0.006, hy - 0.118, tube_z + 0.014),
        (tube_x, hy - 0.048, tube_z + 0.006),
    ]
    flex_hose = curved_tube(
        flex_pts,
        [0.022, 0.026, 0.029, 0.030],
        samples_per_seg=4,
        radial_segs=14,
        cap_rings=3,
        name="snorkel_flex_elbow",
        material="gear_dark",
    )
    scene.add("snorkel", flex_hose)

    # Lower dark ribbed tactical barrel section (y ~ 0.65 to 0.85) with horizontal corrugated ribs.
    lower_mid_y = hy + 0.042  # ~0.766
    lower_barrel = (
        cylinder(0.032, 0.033, 0.220, radial_segs=18, bevel=0.005, axis="y", name="snorkel_lower_dark", material="gear_dark")
        .translate(tube_x, lower_mid_y, tube_z)
    )
    scene.add("snorkel", lower_barrel)

    for r_idx in range(6):
        ry = lower_mid_y - 0.085 + r_idx * 0.034
        rib = (
            torus_arc(0.034, 0.0065, u_segs=18, v_segs=8, plane="xz", name=f"snorkel_dark_rib_{r_idx}", material="gear_dark")
            .translate(tube_x, ry, tube_z)
        )
        scene.add("snorkel", rib)

    # Snorkel keeper clamp locking the vertical tactical tube firmly onto the left mask strap.
    keeper = (
        rounded_box(0.078, 0.050, 0.062, bevel=0.012, segs=3, name="snorkel_keeper_clamp", material="gear_dark")
        .translate(cx + 0.006, cy + 0.004, tube_z - 0.004)
    )
    scene.add("snorkel", keeper)

    # Metallic transition collar ring joining dark lower tube to weathered yellow upper barrel.
    mid_collar = (
        cylinder(0.036, 0.037, 0.026, radial_segs=20, bevel=0.004, axis="y", name="snorkel_mid_collar", material="gear_gold_metal")
        .translate(tube_x, hy + 0.156, tube_z)
    )
    scene.add("snorkel", mid_collar)

    # Upper weathered yellow-gold cylindrical tactical top barrel (rises cleanly to Y ~ 1.16).
    upper_base_y = hy + 0.168
    upper_h = 0.268
    upper_top_y = upper_base_y + upper_h  # ~1.160
    upper_barrel = (
        cylinder(0.035, 0.036, upper_h, radial_segs=22, bevel=0.006, axis="y", name="snorkel_upper_gold", material="gear_gold")
        .translate(tube_x, upper_base_y + 0.5 * upper_h, tube_z)
    )
    scene.add("snorkel", upper_barrel)

    # Horizontal slotted vent grooves (dark grooves framed by brass rings) matching the HD reference.
    for v_idx, frac in enumerate((0.28, 0.50, 0.72)):
        vy = upper_base_y + frac * upper_h
        slot = (
            rounded_box(0.075, 0.013, 0.038, bevel=0.003, segs=2, name=f"snorkel_vent_slot_{v_idx}", material="gear_dark")
            .translate(tube_x, vy, tube_z + 0.018)
        )
        scene.add("snorkel", slot)
        ring = (
            torus_arc(0.0365, 0.0048, u_segs=20, v_segs=8, plane="xz", name=f"snorkel_vent_ring_{v_idx}", material="gear_gold_metal")
            .translate(tube_x, vy + 0.012, tube_z)
        )
        scene.add("snorkel", ring)

    # Thickened brass-lipped top rim + recessed dark open intake bore visible from overhead views.
    top_rim = (
        torus_arc(0.034, 0.009, u_segs=22, v_segs=10, plane="xz", name="snorkel_top_rim", material="gear_gold_metal")
        .translate(tube_x, upper_top_y - 0.002, tube_z)
    )
    scene.add("snorkel", top_rim)

    top_bore = (
        cylinder(0.025, 0.024, 0.016, radial_segs=18, bevel=0.003, axis="y", name="snorkel_intake_bore", material="gear_dark")
        .translate(tube_x, upper_top_y - 0.001, tube_z)
    )
    scene.add("snorkel", top_bore)


def _build_scuba_tank(scene: SceneBuilder, back_anchor: Tuple[float, float, float]) -> None:
    """Build compact brushed-steel scuba air tank with brass gauge, valve, and harness on back-right (-X, -Z)."""
    bx, by, bz = back_anchor
    tank_x = bx - 0.210  # Peeks prominently behind the ant's right shoulder (-X), matching HD reference
    tank_y = by + 0.262  # ~0.630, valve peeks cleanly at shoulder/cheek level just like the HD reference
    tank_z = bz - 0.044  # ~-0.186, clear behind the cranium & dorsal thorax

    parts: List[MeshPart] = []

    # Smooth domed brushed-steel scuba cylinder (radius 0.056, height 0.27).
    tank_prof = [
        (0.000, -0.138),
        (0.034, -0.134),
        (0.054, -0.118),
        (0.056, -0.095),
        (0.056, +0.088),
        (0.048, +0.116),
        (0.026, +0.134),
        (0.018, +0.146),
        (0.000, +0.148),
    ]
    tank_body = lathe(tank_prof, radial_segs=24, cap_bottom=False, cap_top=False, axis="y", name="scuba_tank_body", material="gear_steel")
    parts.append(tank_body)

    # Dark protective rubber tank boot at the base and twin dark harness straps around the barrel.
    boot = (
        cylinder(0.059, 0.058, 0.042, radial_segs=22, bevel=0.006, axis="y", name="scuba_tank_boot", material="gear_dark")
        .translate(0.0, -0.112, 0.0)
    )
    parts.append(boot)

    for b_idx, by_off in enumerate((-0.032, +0.056)):
        band = (
            cylinder(0.060, 0.060, 0.022, radial_segs=22, bevel=0.004, axis="y", name=f"scuba_tank_band_{b_idx}", material="gear_dark")
            .translate(0.0, by_off, 0.0)
        )
        parts.append(band)

    # Brushed brass collar band & pressure gauge badge on the upper cylinder.
    brass_collar = (
        torus_arc(0.057, 0.006, u_segs=22, v_segs=8, plane="xz", name="scuba_tank_brass_ring", material="gear_gold_metal")
        .translate(0.0, +0.080, 0.0)
    )
    parts.append(brass_collar)

    # Valve manifold neck, side regulator outlet, top handwheel knob, and brass pressure dial.
    valve_neck = (
        cylinder(0.018, 0.016, 0.044, radial_segs=16, bevel=0.003, axis="y", name="scuba_valve_neck", material="gear_steel")
        .translate(0.0, +0.162, 0.0)
    )
    parts.append(valve_neck)

    valve_block = (
        rounded_box(0.046, 0.030, 0.034, bevel=0.006, segs=2, name="scuba_valve_block", material="gear_dark")
        .translate(0.0, +0.184, 0.0)
    )
    parts.append(valve_block)

    valve_wheel = (
        cylinder(0.022, 0.022, 0.016, radial_segs=16, bevel=0.004, axis="y", name="scuba_valve_wheel", material="gear_dark")
        .translate(0.0, +0.208, 0.0)
    )
    parts.append(valve_wheel)

    side_knob = (
        cylinder(0.014, 0.014, 0.028, radial_segs=14, bevel=0.003, axis="x", name="scuba_valve_knob", material="gear_steel")
        .translate(-0.028, +0.184, 0.0)
    )
    parts.append(side_knob)

    gauge = (
        cylinder(0.017, 0.017, 0.014, radial_segs=16, bevel=0.003, axis="z", name="scuba_pressure_gauge", material="gear_gold_metal")
        .translate(+0.026, +0.182, +0.022)
    )
    parts.append(gauge)

    for part in parts:
        placed = (
            part.rotate_xyz(rx=-10.0, rz=7.0, degrees=True)
            .translate(tank_x, tank_y, tank_z)
        )
        scene.add("scuba_tank", placed)

    # Dark harness cradle bracket & twin straps linking the steel tank solidly onto the dorsal thorax.
    bracket = (
        rounded_box(0.155, 0.092, 0.046, bevel=0.014, segs=3, name="scuba_tank_bracket", material="gear_dark")
        .rotate_xyz(ry=-0.22)
        .translate(bx - 0.095, by + 0.145, bz - 0.022)
    )
    scene.add("scuba_tank", bracket)

    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        h_pts = [
            (bx - 0.080 + side_sign * 0.055, by + 0.155, bz - 0.018),
            (side_sign * 0.096, 0.482, -0.046),
            (side_sign * 0.092, 0.462, 0.064),
            (side_sign * 0.076, 0.358, 0.120),
        ]
        h_strap = curved_tube(
            h_pts,
            (0.011, 0.018),
            samples_per_seg=4,
            radial_segs=8,
            cap_rings=2,
            name=f"scuba_harness_{tag}",
            material="gear_dark",
        )
        scene.add("scuba_tank", h_strap)


def _make_diving_flipper(name: str, length: float = 0.165, blade_w: float = 0.088) -> MeshPart:
    """Build one sculpted dark rubber diving flipper whose flat sole rests exactly at Y = 0.0."""
    def _sculpt_fin(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        # Local box spans x in [-w/2, +w/2], y in [-0.010, +0.010], z in [-length/2, +length/2].
        # u = 0 at heel (-Z), u = 1 at flared flipper blade tip (+Z).
        u = np.clip((p[:, 2] + 0.5 * length) / length, 0.0, 1.0)
        # Narrow snug heel pocket flaring out into a classic wide paddle blade.
        flare = 0.42 + 0.58 * (u ** 0.85)
        p[:, 0] *= flare

        # Subtle sculpted raised side stiffener rails + center fin ridge on upper face only.
        nx = np.abs(p[:, 0]) / np.maximum(1e-4, 0.5 * blade_w * flare)
        top_mask = (p[:, 1] > 0.0).astype(np.float64)
        rail = np.exp(-((nx - 0.88) / 0.16) ** 2) * np.clip((u - 0.18) / 0.30, 0.0, 1.0)
        center_rib = np.exp(-((nx / 0.18) ** 2)) * np.clip((u - 0.20) / 0.25, 0.0, 1.0)
        heel_pocket = np.exp(-((u / 0.24) ** 2))
        p[:, 1] += top_mask * (0.007 * rail + 0.005 * center_rib + 0.014 * heel_pocket)

        # Taper blade thickness cleanly toward the tip while keeping underside strictly flat.
        p[:, 1] = np.where(p[:, 1] > 0.0, p[:, 1] * (1.0 - 0.36 * u), -0.009)
        return p

    fin = rounded_box(blade_w, 0.018, length, bevel=0.006, segs=4, name=name, material="gear_dark").deform(_sculpt_fin)
    bmin, _ = fin.bounds()
    # Ground flat sole strictly to Y = 0.0 and shift heel pocket over local Z = 0.
    return fin.translate(0.0, -float(bmin[1]), 0.34 * length)


def _build_flippers(scene: SceneBuilder) -> None:
    """Attach sculpted dark rubber diving flippers solidly on the ant's feet at Y = 0.0."""
    # Rear feet: (0.178, 0.0, -0.192), yaw = pi - 0.30 (pointing backward-outward).
    rear_fin = _make_diving_flipper("flipper_rear", length=0.168, blade_w=0.090)
    rear_placed = rear_fin.rotate_xyz(ry=math.pi - 0.34).translate(0.178, 0.0, -0.192)
    scene.add_mirrored("flippers", rear_placed)

    # Front/mid feet: (0.174, 0.0, +0.142), yaw = 0.28 (pointing forward-outward).
    mid_fin = _make_diving_flipper("flipper_mid", length=0.150, blade_w=0.082)
    mid_placed = mid_fin.rotate_xyz(ry=0.32).translate(0.174, 0.0, 0.142)
    scene.add_mirrored("flippers", mid_placed)

    # Snug dark ankle boot collars seating each tarsus cleanly into its flipper pocket.
    for tag, fx, fz in (("rear", 0.178, -0.192), ("mid", 0.174, 0.142)):
        collar = (
            cylinder(0.022, 0.018, 0.028, radial_segs=14, bevel=0.004, axis="y", name=f"flipper_collar_{tag}", material="gear_dark")
            .translate(fx, 0.022, fz)
        )
        scene.add_mirrored("flippers", collar)


def _build_shovel(scene: SceneBuilder, right_hand: Tuple[float, float, float]) -> None:
    """Build compact cupped yellow spade shovel tucked tightly in the ant's right hand (-X side)."""
    hx, hy, hz = right_hand
    parts: List[MeshPart] = []

    # 1. Compact cylindrical yellow shaft + dark grip sleeve.
    shaft = (
        cylinder(0.018, 0.021, 0.258, radial_segs=16, bevel=0.004, axis="y", name="shovel_shaft", material="gear_gold")
        .translate(0.0, 0.014, 0.0)
    )
    parts.append(shaft)

    grip_wrap = (
        cylinder(0.023, 0.023, 0.076, radial_segs=16, bevel=0.004, axis="y", name="shovel_grip_wrap", material="gear_dark")
        .translate(0.0, 0.000, 0.0)
    )
    parts.append(grip_wrap)

    # 2. Compact D-grip handle at the rear/top end.
    d_collar = (
        cylinder(0.024, 0.022, 0.028, radial_segs=14, bevel=0.004, axis="y", name="shovel_d_collar", material="gear_gold_metal")
        .translate(0.0, -0.112, 0.0)
    )
    parts.append(d_collar)

    d_arch = curved_tube(
        [
            (-0.042, -0.198, 0.0),
            (-0.050, -0.156, 0.0),
            (-0.030, -0.122, 0.0),
            (0.000, -0.114, 0.0),
            (0.030, -0.122, 0.0),
            (0.050, -0.156, 0.0),
            (0.042, -0.198, 0.0),
        ],
        0.014,
        samples_per_seg=4,
        radial_segs=10,
        cap_rings=2,
        name="shovel_d_arch",
        material="gear_gold",
    )
    parts.append(d_arch)

    d_bar = (
        capsule(0.016, 0.082, radial_segs=12, cap_rings=3, axis="x", name="shovel_d_grip", material="gear_dark")
        .translate(0.0, -0.198, 0.0)
    )
    parts.append(d_bar)

    # 3. Brass ferrule socket connecting shaft into the spade blade.
    ferrule = (
        cylinder(0.024, 0.032, 0.058, radial_segs=16, bevel=0.005, axis="y", name="shovel_ferrule", material="gear_gold_metal")
        .translate(0.0, 0.148, 0.005)
    )
    parts.append(ferrule)

    # 4. Chunky cupped rounded-spade blade sculpted cleanly for tabletop readability.
    blade_w = 0.172
    blade_l = 0.194
    blade_cy = 0.252

    def _spade_half_width_factor(u_arr: np.ndarray) -> np.ndarray:
        t = np.clip((u_arr - 0.50) / 0.50, 0.0, 1.0)
        return np.sqrt(np.maximum(0.05, 1.0 - (t ** 2.1) * 0.88))

    def _sculpt_spade(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        u = np.clip((p[:, 1] + 0.5 * blade_l) / blade_l, 0.0, 1.0)
        p[:, 0] *= _spade_half_width_factor(u)
        nx = p[:, 0] / (0.5 * blade_w)
        cup_z = 0.022 * (nx * nx) + 0.018 * (u * u)
        p[:, 2] = p[:, 2] * (1.0 - 0.28 * u) + cup_z
        return p

    blade_core = (
        rounded_box(blade_w, blade_l, 0.024, bevel=0.009, segs=4, name="shovel_blade", material="gear_gold")
        .deform(_sculpt_spade)
        .translate(0.0, blade_cy, -0.005)
    )
    parts.append(blade_core)

    tread_bar = (
        capsule(0.013, blade_w * 0.82, radial_segs=12, cap_rings=3, axis="x", name="shovel_tread_bar", material="gear_gold_metal")
        .translate(0.0, blade_cy - 0.5 * blade_l + 0.011, 0.009)
    )
    parts.append(tread_bar)

    blade_rib = (
        capsule(0.014, 0.076, radial_segs=10, cap_rings=3, axis="y", name="shovel_blade_rib", material="gear_gold_metal")
        .translate(0.0, blade_cy - 0.042, 0.009)
    )
    parts.append(blade_rib)

    edge_pts: List[Tuple[float, float, float]] = []
    n_edge = 16
    for i in range(n_edge + 1):
        ang = -0.5 * math.pi + math.pi * (i / n_edge)
        t = math.cos(ang)
        u = 0.50 + 0.50 * t
        half_w = (0.5 * blade_w) * float(_spade_half_width_factor(np.array([u]))[0])
        ex = math.copysign(half_w, math.sin(ang)) * abs(math.sin(ang)) ** 0.75 if abs(ang) > 1e-4 else 0.0
        ey = (blade_cy - 0.5 * blade_l) + blade_l * u
        nx = ex / (0.5 * blade_w)
        ez = -0.005 + 0.022 * (nx * nx) + 0.018 * (u * u)
        edge_pts.append((ex, ey, ez))

    blade_edge = swept_tube(
        edge_pts,
        0.008,
        radial_segs=10,
        cap_start=True,
        cap_end=True,
        closed=False,
        cap_rings=2,
        name="shovel_blade_edge",
        material="gear_gold_metal",
    )
    parts.append(blade_edge)

    # Tucked neatly at the ant's right hand (-X) keeping total Swimmer width ~0.65-0.68 and Y >= 0.14.
    for part in parts:
        placed = (
            part.rotate_xyz(rx=114.0, ry=-8.0, rz=10.0, degrees=True)
            .translate(hx - 0.032, hy - 0.004, hz + 0.014)
        )
        scene.add("shovel", placed)


def build_swimmer(blink: float = 0.0) -> SceneBuilder:
    """Build the sculpted 3D Swimmer Ant SceneBuilder with intact Blink animation."""
    scene, anchors = build_base_ant(
        scene_name="ant_swimmer",
        include_antennae=True,
        include_eyes=True,
        include_brows=False,
        left_arm_pose="idle",
        right_arm_pose=_SWIMMER_RIGHT_ARM,
        blink=blink,
    )
    clip_anchor = _build_scuba_mask(scene, anchors["head_center"])
    _build_snorkel(scene, anchors["head_center"], clip_anchor)
    _build_scuba_tank(scene, anchors["back_anchor"])
    _build_flippers(scene)
    _build_shovel(scene, anchors["right_hand"])
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical entry point discovered by build_all.py."""
    return build_swimmer(blink=blink)
