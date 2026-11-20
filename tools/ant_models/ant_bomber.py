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

_BOMB_CHARGE_MAT = "bomb_charge"
_BOMB_CUP_TRIM_MAT = "bomb_cup_trim"

# Compact tucked right-arm hold so the handheld TNT canister stays neatly inside Bomber's tight width.
_BOMBER_RIGHT_ARM: List[Tuple[float, float, float]] = [
    (-0.114, 0.438, 0.022),
    (-0.204, 0.402, 0.088),
    (-0.194, 0.394, 0.198),
    (-0.178, 0.408, 0.246),
]


def _spow(val: float, exp: float) -> float:
    return math.copysign(abs(val) ** exp, val)


def _hollow_ring_lathe(
    profile_rz: List[Tuple[float, float]],
    *,
    radial_segs: int = 28,
    name: str,
    material: str,
) -> MeshPart:
    """Build a closed watertight hollow annular ring around +Z with positive outward winding."""
    closed_prof = list(profile_rz) + [profile_rz[0]]
    return (
        lathe(
            closed_prof,
            radial_segs=radial_segs,
            cap_bottom=False,
            cap_top=False,
            axis="z",
            name=name,
            material=material,
        )
        .ensure_outward()
    )


def _build_goggle_barrel(side_sign: float) -> List[MeshPart]:
    """Build one true hollow copper-and-brass demolition goggle barrel with thin see-through lens.

    Centered in eye-local coordinates around (0, 0, 0) so the ant's wide-alert 3-part eye and
    blinking eyelid (max radial sweep ~0.080, max Z ~0.077) clear the hollow bore (inner radius
    >= 0.083, lens back >= 0.084) cleanly across open, half, and closed blink poses.
    """
    parts: List[MeshPart] = []
    tag = "left" if side_sign > 0 else "right"

    # 1. Rear dark rubber hollow eye-cup gasket hugging the cranium around the eye socket.
    cushion_prof = [
        (0.086, -0.026),
        (0.095, -0.028),
        (0.102, -0.012),
        (0.102, +0.020),
        (0.094, +0.024),
        (0.086, +0.022),
    ]
    parts.append(
        _hollow_ring_lathe(
            cushion_prof,
            radial_segs=26,
            name=f"goggle_cushion_{tag}",
            material="gear_dark",
        )
    )

    # 2. Rugged hollow copper-bronze mid-barrel sleeve around the clear open eye bore.
    copper_prof = [
        (0.085, +0.018),
        (0.100, +0.018),
        (0.103, +0.026),
        (0.103, +0.064),
        (0.098, +0.070),
        (0.085, +0.070),
    ]
    parts.append(
        _hollow_ring_lathe(
            copper_prof,
            radial_segs=28,
            name=f"goggle_copper_barrel_{tag}",
            material="gear_brass_trim",
        )
    )

    # 3. Weathered yellow-gold hollow stepped front lens hood bezel framing the open viewport.
    hood_prof = [
        (0.084, +0.066),
        (0.100, +0.066),
        (0.104, +0.074),
        (0.104, +0.100),
        (0.097, +0.108),
        (0.084, +0.108),
        (0.083, +0.100),
        (0.083, +0.072),
    ]
    parts.append(
        _hollow_ring_lathe(
            hood_prof,
            radial_segs=28,
            name=f"goggle_front_hood_{tag}",
            material="gear_gold",
        )
    )

    # Crisp metallic brass front lip ring around the lens opening.
    front_ring = (
        torus_arc(0.093, 0.0085, u_segs=28, v_segs=10, plane="xy", name=f"goggle_brass_ring_{tag}", material="gear_gold_metal")
        .translate(0.0, 0.0, 0.105)
    )
    parts.append(front_ring)

    # Recessed dark inner lens gasket ring lining the front bore edge (open center >= 0.0815!).
    inner_seat = (
        torus_arc(0.085, 0.0035, u_segs=24, v_segs=8, plane="xy", name=f"goggle_inner_seat_{tag}", material="gear_dark")
        .translate(0.0, 0.0, 0.096)
    )
    parts.append(inner_seat)

    # Thin translucent optical glass lens seated cleanly ahead of the full eyelid blink sweep
    # (z_local = +0.093, back surface at +0.0845 vs closed eyelid max_z = +0.0766).
    lens = (
        ellipsoid(0.086, 0.086, 0.0085, u_segs=26, v_segs=14, name=f"goggle_lens_{tag}", material="glass_tinted")
        .translate(0.0, 0.0, 0.093)
    )
    parts.append(lens)

    # Three small raised golden rivets arrayed along the upper-outer copper barrel rim.
    for r_idx, ang_deg in enumerate((22.0, 56.0, 92.0)):
        ang = math.radians(ang_deg)
        rx = side_sign * 0.102 * math.sin(ang)
        ry = 0.102 * math.cos(ang)
        rivet = (
            sphere(0.010, u_segs=10, v_segs=8, name=f"goggle_rivet_{tag}_{r_idx}", material="gear_gold_metal")
            .translate(rx, ry, 0.046)
        )
        parts.append(rivet)

    return parts


def _build_goggles(
    scene: SceneBuilder,
    head_center: Tuple[float, float, float],
    eye_left: Tuple[float, float, float],
    eye_right: Tuple[float, float, float],
) -> None:
    """Build true hollow brass-and-copper goggles centered over both eyes with see-through lenses."""
    hx, hy, hz = head_center
    ey = 0.5 * (eye_left[1] + eye_right[1])
    ez = 0.5 * (eye_left[2] + eye_right[2])

    for side_sign, eye_pos in ((+1.0, eye_left), (-1.0, eye_right)):
        # Match Pass 3 eye gaze orientation (rx=-0.16, ry=+-0.20) with tiny +0.004 outward offset
        # so the wider 0.101 outer barrels kiss cleanly at the nose bridge without intersecting.
        yaw = side_sign * 0.19
        pitch = -0.16
        mount = (eye_pos[0] + side_sign * 0.004, eye_pos[1], eye_pos[2])
        for part in _build_goggle_barrel(side_sign):
            placed = part.rotate_xyz(rx=pitch, ry=yaw).translate(*mount)
            scene.add("goggles", placed)

    # Sculpted copper-bronze nose bridge joining the twin hollow barrels cleanly between the eyes.
    bridge = (
        rounded_box(0.046, 0.032, 0.042, bevel=0.009, segs=3, name="goggle_bridge", material="gear_brass_trim")
        .translate(hx, ey + 0.014, ez + 0.062)
    )
    scene.add("goggles", bridge)

    bridge_pin = (
        cylinder(0.011, 0.011, 0.014, radial_segs=14, bevel=0.003, axis="z", name="goggle_bridge_bolt", material="gear_gold_metal")
        .translate(hx, ey + 0.015, ez + 0.084)
    )
    scene.add("goggles", bridge_pin)

    # Temple strap clasps anchored cleanly on the sides of the cranium.
    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        clasp = (
            rounded_box(0.034, 0.056, 0.060, bevel=0.010, segs=3, name=f"goggle_clasp_{tag}", material="gear_brass_trim")
            .rotate_xyz(ry=side_sign * 0.30)
            .translate(hx + side_sign * 0.214, ey + 0.006, hz + 0.148)
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
        strap_pts.append((sx, ey + 0.008 + rear_lift, sz))

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
        .translate(hx, ey + 0.020, hz - rz_strap - 0.005)
    )
    scene.add("goggles", rear_buckle)


def _build_cup_bomb(
    prefix: str,
    cup_h: float,
    fuse_curve_x: float,
    *,
    lit: bool = False,
    rivet_count: int = 10,
) -> List[MeshPart]:
    """Build one tapered open-cup team bomb with riveted iron rim, flat magenta drum, and rope fuse.

    Normalized around cup height `cup_h` (base at local Y = 0.0, rolled rim at Y = cup_h,
    flat-topped cylindrical explosive drum to Y = 1.63 * cup_h, arching rope fuse to ~2.35 * cup_h).
    """
    h = float(cup_h)
    parts: List[MeshPart] = []

    def r_at(u: float) -> float:
        return (0.37 + 0.26 * u) * h

    # 1. Heavy dark iron foot collar ring at cup base.
    foot = (
        cylinder(0.43 * h, 0.41 * h, 0.11 * h, radial_segs=18, bevel=0.025 * h, axis="y", name=f"{prefix}_foot", material="gear_dark")
        .translate(0.0, 0.055 * h, 0.0)
    )
    parts.append(foot)

    # 2. Tapered cup wall: narrow dark-maroon bottom band, wide team-colored middle band, top maroon band.
    bot_band = (
        cylinder(r_at(0.00), r_at(0.16), 0.16 * h, radial_segs=20, bevel=0.008 * h, axis="y", name=f"{prefix}_cup_bot", material=_BOMB_CUP_TRIM_MAT)
        .translate(0.0, 0.08 * h, 0.0)
    )
    parts.append(bot_band)

    mid_wall = (
        cylinder(
            r_at(0.15) + 0.014 * h,
            r_at(0.83) + 0.014 * h,
            0.68 * h,
            radial_segs=22,
            bevel=0.012 * h,
            axis="y",
            name=f"{prefix}_cup_team",
            material="chitin_team",
        )
        .translate(0.0, 0.49 * h, 0.0)
    )
    parts.append(mid_wall)

    top_band = (
        cylinder(r_at(0.82), r_at(0.99), 0.17 * h, radial_segs=20, bevel=0.008 * h, axis="y", name=f"{prefix}_cup_top", material=_BOMB_CUP_TRIM_MAT)
        .translate(0.0, 0.905 * h, 0.0)
    )
    parts.append(top_band)

    # Twin thin dark iron hoops framing the team-colored middle band at u = 0.15 and 0.82.
    for h_idx, u_hoop in enumerate((0.15, 0.82)):
        hoop = (
            torus_arc(r_at(u_hoop) + 0.018 * h, 0.024 * h, u_segs=22, v_segs=8, plane="xz", name=f"{prefix}_hoop_{h_idx}", material="gear_dark")
            .translate(0.0, u_hoop * h, 0.0)
        )
        parts.append(hoop)

    # Dark recessed cup interior floor around the base of the charge drum.
    interior = (
        cylinder(0.56 * h, 0.56 * h, 0.10 * h, radial_segs=20, bevel=0.010 * h, axis="y", name=f"{prefix}_cup_well", material="gear_dark")
        .translate(0.0, 0.80 * h, 0.0)
    )
    parts.append(interior)

    # Chunky rolled dark iron open rim lip visibly wider than the charge drum (outer 0.68*h, inner 0.54*h).
    rim = (
        torus_arc(0.61 * h, 0.072 * h, u_segs=24, v_segs=10, plane="xz", name=f"{prefix}_rim", material="gear_dark")
        .translate(0.0, 1.00 * h, 0.0)
    )
    parts.append(rim)

    # Metallic iron/steel rivets ringing the outer perimeter of the rolled rim lip.
    for rv_idx in range(rivet_count):
        ang = (rv_idx / rivet_count) * 2.0 * math.pi
        rv = (
            sphere(0.030 * h, u_segs=8, v_segs=6, name=f"{prefix}_rivet_{rv_idx}", material="gear_steel")
            .translate(0.675 * h * math.cos(ang), 1.00 * h, 0.675 * h * math.sin(ang))
        )
        parts.append(rv)

    # 3. Flat-topped cylindrical explosive drum charge sitting visibly inside the wider iron rim.
    drum = (
        cylinder(
            0.52 * h,
            0.52 * h,
            0.88 * h,
            radial_segs=22,
            bevel=0.036 * h,
            axis="y",
            name=f"{prefix}_charge_drum",
            material=_BOMB_CHARGE_MAT,
        )
        .translate(0.0, 1.19 * h, 0.0)
    )
    parts.append(drum)

    # Recessed dark center fuse socket on the flat circular top of the drum.
    socket = (
        cylinder(0.098 * h, 0.090 * h, 0.044 * h, radial_segs=14, bevel=0.008 * h, axis="y", name=f"{prefix}_fuse_socket", material="gear_dark")
        .translate(0.0, 1.635 * h, 0.0)
    )
    parts.append(socket)

    # 4. Warm off-white rope fuse rising straight from center socket and arcing smoothly to one side.
    fdir = 1.0 if fuse_curve_x >= 0.0 else -1.0
    fspan = max(0.32 * h, abs(fuse_curve_x))
    fuse_pts = [
        (0.0, 1.63 * h, 0.0),
        (0.04 * fdir * fspan, 1.94 * h, 0.02 * h),
        (0.45 * fdir * fspan, 2.24 * h, 0.04 * h),
        (1.00 * fdir * fspan, 2.34 * h, 0.02 * h),
    ]
    fuse = curved_tube(
        fuse_pts,
        [0.056 * h, 0.052 * h, 0.048 * h, 0.044 * h],
        samples_per_seg=4,
        radial_segs=8,
        cap_rings=2,
        name=f"{prefix}_fuse",
        material="fuse_white",
    )
    parts.append(fuse)

    # Tip: bright warm gold spark nub when lit, crisp dark charred end when unlit.
    tip_r = 0.095 * h if lit else 0.056 * h
    tip_mat = "gear_gold" if lit else "gear_dark"
    tip = sphere(tip_r, u_segs=10, v_segs=8, name=f"{prefix}_fuse_tip", material=tip_mat).translate(*fuse_pts[-1])
    parts.append(tip)

    return parts


def _build_dynamite_pack(scene: SceneBuilder, back_anchor: Tuple[float, float, float]) -> None:
    """Build compact dorsal harness holding 4 upright riveted open-cup bombs behind shoulders/head."""
    bx, by, bz = back_anchor
    pack_y = by + 0.140  # ~0.508, seated high on upper dorsal thorax behind neck
    pack_z = bz - 0.062  # ~-0.204, snug behind thorax & head back (-0.176)

    # Compact dark harness backplate contoured onto the slender Pass 2 dorsal thorax.
    backplate = (
        rounded_box(0.220, 0.180, 0.058, bevel=0.018, segs=3, name="pack_backplate", material="gear_dark")
        .rotate_xyz(rx=-0.26)
        .translate(bx, pack_y + 0.018, pack_z + 0.020)
    )
    scene.add("dynamite_pack", backplate)

    # Twin dark shoulder harness straps wrapping over the narrow Pass 2 shoulders to chest.
    for side_sign, tag in ((+1.0, "left"), (-1.0, "right")):
        s_pts = [
            (side_sign * 0.076, pack_y + 0.032, pack_z + 0.032),
            (side_sign * 0.095, 0.484, -0.048),
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

    # Curved dark holster rack bar & brass center buckle cradling the 4 compact back-mounted bombs.
    rack_pts = [
        (-0.168, pack_y + 0.058, pack_z - 0.014),
        (-0.092, pack_y + 0.102, pack_z - 0.062),
        (0.000, pack_y + 0.118, pack_z - 0.078),
        (0.092, pack_y + 0.102, pack_z - 0.062),
        (0.168, pack_y + 0.058, pack_z - 0.014),
    ]
    rack_strap = curved_tube(
        rack_pts,
        (0.019, 0.014),
        samples_per_seg=5,
        radial_segs=10,
        cap_rings=2,
        name="bomb_rack_strap",
        material="gear_dark",
    )
    scene.add("dynamite_pack", rack_strap)

    rack_buckle = (
        rounded_box(0.050, 0.044, 0.022, bevel=0.007, segs=3, name="bomb_rack_buckle", material="gear_gold_metal")
        .rotate_xyz(rx=-0.28)
        .translate(0.0, pack_y + 0.118, pack_z - 0.086)
    )
    scene.add("dynamite_pack", rack_buckle)

    # Four compact (~0.76x) riveted open-cup bombs packed tightly & slightly lower behind shoulders/head
    # so Bomber fits neatly inside original abst301's compact silhouette (~0.52 W total).
    # Positive rz leans +Y toward -X (ant right); negative rz leans +Y toward +X (ant left).
    # Each tuple: (splay_rz_deg, tilt_back_rx_deg, base_ox, base_oy, base_oz, cup_h, fuse_dx).
    pack_specs = (
        (+18.0, -14.0, -0.116, 0.054, -0.018, 0.080, -0.028),
        (+6.0, -18.0, -0.048, 0.130, -0.054, 0.084, -0.024),
        (-6.0, -18.0, +0.048, 0.130, -0.054, 0.084, +0.024),
        (-18.0, -14.0, +0.116, 0.054, -0.018, 0.080, +0.028),
    )
    for idx, (rz_deg, rx_deg, ox, oy, oz, cup_h, fuse_dx) in enumerate(pack_specs):
        # Small dark socket holster pad anchoring each bomb cup base firmly to the rack.
        holster = (
            cylinder(0.45 * cup_h, 0.42 * cup_h, 0.034, radial_segs=14, bevel=0.005, axis="y", name=f"pack_holster_{idx}", material="gear_dark")
            .rotate_xyz(rx=rx_deg, rz=rz_deg, degrees=True)
            .translate(bx + ox, pack_y + oy, pack_z + oz)
        )
        scene.add("dynamite_pack", holster)

        for part in _build_cup_bomb(f"pack_bomb_{idx}", cup_h, fuse_dx, lit=False, rivet_count=10):
            placed = (
                part.rotate_xyz(rx=rx_deg, rz=rz_deg, degrees=True)
                .translate(bx + ox, pack_y + oy, pack_z + oz)
            )
            scene.add("dynamite_pack", placed)


def _build_handheld_bomb(scene: SceneBuilder, right_hand: Tuple[float, float, float]) -> None:
    """Build a compact handheld riveted open-cup team bomb tucked neatly in Bomber's right hand."""
    hx, hy, hz = right_hand
    cup_h = 0.080
    parts = _build_cup_bomb("hand_bomb", cup_h, fuse_curve_x=-0.034, lit=True, rivet_count=10)

    # Tucked closely against the gripped right hand well inside Bomber's compact width (~0.52).
    for part in parts:
        placed = (
            part.translate(0.0, -0.45 * cup_h, 0.0)
            .rotate_xyz(rx=12.0, ry=10.0, rz=6.0, degrees=True)
            .translate(hx - 0.010, hy + 0.008, hz + 0.032)
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
    _build_goggles(scene, anchors["head_center"], anchors["eye_left"], anchors["eye_right"])
    _build_dynamite_pack(scene, anchors["back_anchor"])
    _build_handheld_bomb(scene, anchors["right_hand"])
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical entry point discovered by build_all.py."""
    return build_bomber(blink=blink)
