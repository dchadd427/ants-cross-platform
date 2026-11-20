"""Sculpted organic insectoid base ant shared by Worker, Thief, Bomber, Swimmer, and Fire.

Coordinate system (glTF 2.0 right-handed):
  +Y = up (tarsus tips grounded solidly on Y = 0.0)
  +Z = front (South / canonical camera 301)
  +X = ant's left (viewer's right from front), -X = ant's right (viewer's left)

Matches original Ants (1998) slim upright Worker proportions (W/H ~ 0.48-0.54)
and exports pivoted `eyelid_left` / `eyelid_right` nodes with a glTF 2.0
"Blink" rotation animation clip.
"""
from __future__ import annotations

import math
from typing import Any, Dict, List, Optional, Sequence, Tuple, Union

import numpy as np

try:
    from .mesh_core import (
        MeshPart,
        SceneBuilder,
        cone,
        curved_tube,
        ellipsoid,
        hemisphere_shell,
        lathe,
        quat_from_axis_angle,
        sphere,
        superellipsoid,
        swept_tube,
        torus_arc,
    )
except ImportError:
    from mesh_core import (  # type: ignore
        MeshPart,
        SceneBuilder,
        cone,
        curved_tube,
        ellipsoid,
        hemisphere_shell,
        lathe,
        quat_from_axis_angle,
        sphere,
        superellipsoid,
        swept_tube,
        torus_arc,
    )

ArmPoseLike = Optional[Union[str, Sequence[Sequence[float]]]]

EYELID_CLOSED_ANGLE = 1.48  # radians around local +X (~85 deg) to seal 100% of sclera/iris/pupil cleanly

# Canonical left-side (+X) arm control curves: [shoulder, elbow, wrist, hand].
# Narrow upright posture matching original agst sprites (front W/H ~ 0.50).
_ARM_PRESETS_LEFT: Dict[str, List[Tuple[float, float, float]]] = {
    "idle": [
        (0.114, 0.438, 0.022),
        (0.206, 0.398, 0.078),
        (0.184, 0.354, 0.182),
        (0.156, 0.348, 0.218),
    ],
    "hold_tool": [
        (0.114, 0.438, 0.022),
        (0.228, 0.408, 0.092),
        (0.232, 0.416, 0.216),
        (0.220, 0.436, 0.268),
    ],
    "hold_front": [
        (0.114, 0.438, 0.022),
        (0.228, 0.408, 0.092),
        (0.232, 0.416, 0.216),
        (0.220, 0.436, 0.268),
    ],
    "hold_high": [
        (0.114, 0.438, 0.022),
        (0.238, 0.452, 0.106),
        (0.246, 0.550, 0.220),
        (0.234, 0.594, 0.274),
    ],
    "akimbo": [
        (0.114, 0.438, 0.022),
        (0.240, 0.402, 0.052),
        (0.214, 0.336, 0.142),
        (0.186, 0.324, 0.172),
    ],
    "wide": [
        (0.114, 0.438, 0.022),
        (0.240, 0.402, 0.052),
        (0.214, 0.336, 0.142),
        (0.186, 0.324, 0.172),
    ],
}


def _head_sculpt_fn(pos: np.ndarray) -> np.ndarray:
    """Sculpt an organic beveled-crown insect carapace with brow arches and center furrow.

    Creates a flatter angled forehead crown with a crisp specular bevel edge,
    arched chitin brow lobes over orbital sockets, a vertical center cranial
    furrow, and a sleek tapered insect wedge jaw below the eyes.
    """
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    ax = np.abs(x)
    front_mask = np.clip((z - 0.02) / 0.18, 0.0, 1.0)

    # 1. Crisp beveled crown plateau tilted slightly forward so overhead light catches the rim.
    crown_y = 0.184 - 0.052 * np.clip((z + 0.07) / 0.28, 0.0, 1.0)
    over = np.maximum(0.0, y - crown_y)
    p[:, 1] = y - 0.45 * over

    # 2. Arched chitin brow ridges directly above each orbital socket (replaces cartoon eyebrows).
    brow_arch = (
        np.exp(-((ax - 0.098) / 0.066) ** 2)
        * np.exp(-((y - 0.086) / 0.044) ** 2)
        * front_mask
    )
    p[:, 2] += 0.024 * brow_arch
    p[:, 1] += 0.012 * brow_arch

    # 3. Crisp center vertical cranial brow furrow between the two arched brow lobes.
    furrow = (
        np.exp(-((x / 0.024) ** 2))
        * np.exp(-((y - 0.066) / 0.080) ** 2)
        * front_mask
    )
    p[:, 2] -= 0.020 * furrow
    p[:, 1] -= 0.006 * furrow

    # 4. Shallow orbital depressions so eyes nestle organically inside the carapace.
    socket = (
        np.exp(-(((ax - 0.094) / 0.060) ** 2 + ((y + 0.010) / 0.062) ** 2))
        * np.clip((z - 0.07) / 0.15, 0.0, 1.0)
    )
    p[:, 2] -= 0.022 * socket

    # 5. Sleek insect wedge taper below the eyes into the narrow jaw/clypeus bridge.
    lower = np.clip((-y - 0.018) / 0.200, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.34 * (lower ** 1.35)
    p[:, 2] *= 1.0 - 0.14 * (lower ** 1.45)

    # Subtle forward clypeus/snout bridge between the lower eyes toward the mandibles.
    clypeus = np.exp(-((x / 0.068) ** 2)) * np.exp(-((y + 0.102) / 0.064) ** 2) * front_mask
    p[:, 2] += 0.018 * clypeus

    # Slight rear-lower chamfer so side and 3/4 silhouettes look chiseled into the neck.
    rear_low = np.clip((-z - 0.04) / 0.17, 0.0, 1.0) * np.clip((-y + 0.02) / 0.21, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.12 * rear_low
    return p


def _build_mandibles(scene: SceneBuilder, head_center: Tuple[float, float, float], head_tilt_x: float) -> None:
    """Build inward-curving toothed chitin pincer mandibles + recessed dark mouth cleft."""
    _, hy, hz = head_center

    # Curved pincer mandible jaw (left side +X, mirrored cleanly to -X).
    jaw_pts = [
        (0.084, hy - 0.132, hz + 0.118),
        (0.106, hy - 0.160, hz + 0.176),
        (0.084, hy - 0.178, hz + 0.236),
        (0.032, hy - 0.182, hz + 0.256),
    ]
    jaw_rads = [
        (0.032, 0.034),
        (0.036, 0.040),
        (0.028, 0.031),
        (0.013, 0.015),
    ]
    jaw_left = curved_tube(
        jaw_pts,
        jaw_rads,
        samples_per_seg=4,
        radial_segs=12,
        cap_rings=3,
        name="mandible_jaw",
        material="chitin_team",
    )
    if abs(head_tilt_x) > 1e-6:
        jaw_left = jaw_left.translate(0.0, -hy, -hz).rotate_xyz(rx=head_tilt_x).translate(0.0, hy, hz)
    scene.add_mirrored("head", jaw_left)

    # Two subtle sharp inner serration teeth along the biting edge of each mandible.
    for t_idx, (tx, ty, tz, rad, hgt, rz_deg) in enumerate(
        (
            (0.056, hy - 0.177, hz + 0.232, 0.011, 0.026, 72.0),
            (0.036, hy - 0.180, hz + 0.246, 0.009, 0.022, 84.0),
        )
    ):
        tooth = (
            cone(rad, hgt, radial_segs=10, name=f"mandible_tooth_{t_idx}", material="chitin_team")
            .rotate_xyz(rx=12.0, ry=-18.0, rz=rz_deg, degrees=True)
            .translate(tx, ty, tz)
        )
        scene.add_mirrored("head", tooth)

    # Recessed dark mouth/labrum cleft tucked between the mandible bases.
    mouth_cleft = (
        ellipsoid(0.050, 0.030, 0.044, u_segs=14, v_segs=10, name="mouth_cleft", material="chitin_dark")
        .translate(0.0, hy - 0.162, hz + 0.154)
    )
    scene.add("head", mouth_cleft)


def _build_head(scene: SceneBuilder, head_center: Tuple[float, float, float], head_tilt_x: float) -> None:
    hx, hy, hz = head_center
    cranium = (
        superellipsoid(0.228, 0.222, 0.226, e1=0.64, e2=0.72, u_segs=36, v_segs=26, name="cranium", material="chitin_team")
        .deform(_head_sculpt_fn)
        .rotate_xyz(rx=head_tilt_x)
        .translate(hx, hy, hz)
    )
    scene.add("head", cranium)
    _build_mandibles(scene, head_center, head_tilt_x)


def _build_eyelid_shell(name: str, yaw_y: float) -> MeshPart:
    """Build a curved chitin_team upper eyelid shell in eye-local coordinates around (0, 0, 0).

    At neutral rotation (0, 0, 0, 1) the front lip sits tucked into the upper eye
    socket (+44 deg) while the back half hides inside the cranium. Rotating around
    local +X by +EYELID_CLOSED_ANGLE sweeps the shell smoothly down to -41 deg,
    covering 100% of sclera, iris, and pupil with zero clipping.
    """
    lid = hemisphere_shell(
        0.071,
        0.076,
        0.071,
        thickness=0.007,
        start_phi=-0.06,
        u_segs=22,
        v_segs=10,
        name=name,
        material="chitin_team",
    )
    return lid.rotate_xyz(rx=-0.82, ry=yaw_y)


def _build_eyes(
    scene: SceneBuilder,
    head_center: Tuple[float, float, float],
    head_tilt_x: float,
    *,
    include_brows: bool = False,
    blink: float = 0.0,
) -> Dict[str, Any]:
    """Build 3-part eyes (sclera, olive-gold iris, obsidian pupil) + pivoted blinkable eyelids."""
    _, hy, hz = head_center
    eye_x = 0.095
    eye_y = hy - 0.012
    eye_z = hz + 0.194

    # Dark sculpted orbital socket rim grounding each eye deeply into the carapace.
    socket_rim = (
        torus_arc(0.066, 0.011, u_segs=22, v_segs=8, plane="xy", name="eye_socket_rim", material="chitin_dark")
        .scale(0.95, 1.04, 0.90)
        .rotate_xyz(rx=-0.18 + head_tilt_x, ry=0.20)
        .translate(eye_x, eye_y, eye_z + 0.006)
    )
    scene.add_mirrored("eyes", socket_rim)

    # Warm cream-white sclera seated snugly inside the orbital socket.
    sclera_left = (
        superellipsoid(0.063, 0.069, 0.052, e1=0.88, e2=0.90, u_segs=20, v_segs=16, name="sclera", material="eye_white")
        .rotate_xyz(rx=-0.18 + head_tilt_x, ry=0.20)
        .translate(eye_x, eye_y, eye_z)
    )
    scene.add_mirrored("eyes", sclera_left)

    # Warm olive-gold/amber iris dome seated cleanly on the front surface of the sclera.
    iris_left = (
        ellipsoid(0.036, 0.040, 0.020, u_segs=18, v_segs=12, name="iris", material="eye_iris")
        .rotate_xyz(rx=-0.16 + head_tilt_x, ry=0.15)
        .translate(eye_x - 0.004, eye_y + 0.003, eye_z + 0.038)
    )
    scene.add_mirrored("eyes", iris_left)

    # Crisp obsidian pupil centered cleanly inside the iris ring.
    pupil_left = (
        ellipsoid(0.020, 0.022, 0.014, u_segs=14, v_segs=10, name="pupil", material="eye_pupil")
        .rotate_xyz(rx=-0.16 + head_tilt_x, ry=0.13)
        .translate(eye_x - 0.006, eye_y + 0.004, eye_z + 0.047)
    )
    scene.add_mirrored("eyes", pupil_left)

    # Pivoted blinkable upper eyelid nodes (one per eye, pivoted right at each eye center).
    blink_clamped = max(0.0, min(1.0, float(blink)))
    rest_rot = quat_from_axis_angle((1.0, 0.0, 0.0), blink_clamped * EYELID_CLOSED_ANGLE)

    scene.add("eyelid_left", _build_eyelid_shell("eyelid_left_shell", yaw_y=+0.18))
    scene.set_node_transform("eyelid_left", translation=(eye_x, eye_y, eye_z), rotation=rest_rot)

    scene.add("eyelid_right", _build_eyelid_shell("eyelid_right_shell", yaw_y=-0.18))
    scene.set_node_transform("eyelid_right", translation=(-eye_x, eye_y, eye_z), rotation=rest_rot)

    scene.add_blink_animation(("eyelid_left", "eyelid_right"), closed_angle=EYELID_CLOSED_ANGLE, name="Blink")

    # Optional subtle chitin eyelid contour if explicitly requested (off by default).
    if include_brows:
        lid_pts = [
            (0.042, eye_y + 0.068, eye_z + 0.016),
            (0.095, eye_y + 0.082, eye_z + 0.018),
            (0.150, eye_y + 0.060, eye_z - 0.006),
        ]
        lid_left = curved_tube(
            lid_pts,
            [0.008, 0.010, 0.007],
            samples_per_seg=3,
            radial_segs=8,
            cap_rings=2,
            name="eyelid_ridge",
            material="chitin_dark",
        )
        scene.add_mirrored("eyes", lid_left)

    return {
        "eye_left": (eye_x, eye_y, eye_z),
        "eye_right": (-eye_x, eye_y, eye_z),
        "eye_center": (0.0, eye_y, eye_z + 0.016),
        "eyelid_closed_angle": EYELID_CLOSED_ANGLE,
    }


def _build_antennae(scene: SceneBuilder, head_center: Tuple[float, float, float]) -> Dict[str, Tuple[float, float, float]]:
    """Build slender 2-segment kinked dark insect antennae with subtle clubbed tips."""
    _, hy, hz = head_center
    base_l = (0.102, hy + 0.172, hz + 0.044)
    elbow_l = (0.142, hy + 0.328, hz + 0.060)
    mid2_l = (0.164, hy + 0.344, hz + 0.120)
    tip_l = (0.174, hy + 0.296, hz + 0.178)

    # Segment 1: slender scape rising cleanly from forehead socket to sharp elbow kink.
    scape = swept_tube(
        [base_l, (0.122, hy + 0.254, hz + 0.052), elbow_l],
        [0.015, 0.012, 0.014],
        radial_segs=10,
        smooth_subdiv=2,
        cap_rings=2,
        name="antenna_scape",
        material="chitin_dark",
    )
    scene.add_mirrored("antennae", scape)

    # Crisp elbow joint knuckle.
    scene.add_mirrored(
        "antennae",
        sphere(0.016, u_segs=10, v_segs=8, name="antenna_joint", material="chitin_dark").translate(*elbow_l),
    )

    # Segment 2: arched funiculus swelling gently into a subtle organic clubbed tip.
    funiculus = curved_tube(
        [elbow_l, mid2_l, tip_l],
        [0.012, 0.017, 0.023],
        samples_per_seg=4,
        radial_segs=10,
        cap_rings=3,
        name="antenna_club",
        material="chitin_dark",
    )
    scene.add_mirrored("antennae", funiculus)

    # Recessed dark socket collar at cranium base.
    socket_left = (
        ellipsoid(0.028, 0.016, 0.028, u_segs=12, v_segs=8, name="antenna_socket", material="chitin_dark")
        .rotate_xyz(rx=0.22, rz=-0.30)
        .translate(*base_l)
    )
    scene.add_mirrored("antennae", socket_left)

    return {
        "antenna_base_left": base_l,
        "antenna_base_right": (-base_l[0], base_l[1], base_l[2]),
    }


def _build_body(scene: SceneBuilder) -> Dict[str, Tuple[float, float, float]]:
    """Build sleek upright armored insect thorax and stepped-plate teardrop gaster."""
    # Compact neck joint linking cranium to thorax.
    neck = (
        lathe([(0.082, 0.0), (0.068, 0.055), (0.082, 0.118)], radial_segs=16, name="neck", material="chitin_dark")
        .rotate_xyz(rx=0.14)
        .translate(0.0, 0.440, 0.010)
    )
    scene.add("body", neck)

    # Sleek upright dark chitin thorax core.
    thorax_core = (
        superellipsoid(0.112, 0.152, 0.120, e1=0.76, e2=0.80, u_segs=20, v_segs=14, name="thorax_core", material="chitin_dark")
        .rotate_xyz(rx=0.12)
        .translate(0.0, 0.340, -0.010)
    )
    scene.add("body", thorax_core)

    # Crisper beveled team-colored thoracic chest plate and dorsal pronotum shield.
    chest_plate = (
        superellipsoid(0.100, 0.116, 0.078, e1=0.66, e2=0.72, u_segs=18, v_segs=14, name="chest_plate", material="chitin_team")
        .rotate_xyz(rx=0.16)
        .translate(0.0, 0.346, 0.052)
    )
    scene.add("body", chest_plate)

    dorsal_plate = (
        superellipsoid(0.098, 0.114, 0.082, e1=0.66, e2=0.72, u_segs=18, v_segs=14, name="dorsal_plate", material="chitin_team")
        .rotate_xyz(rx=-0.12)
        .translate(0.0, 0.358, -0.060)
    )
    scene.add("body", dorsal_plate)

    # Segmented petiole waist node connecting thorax to gaster.
    petiole = (
        superellipsoid(0.052, 0.052, 0.058, e1=0.78, e2=0.82, u_segs=14, v_segs=10, name="petiole", material="chitin_dark")
        .translate(0.0, 0.248, -0.130)
    )
    scene.add("body", petiole)

    # Stepped overlapping-plate teardrop insect gaster extending cleanly backward (-Z).
    gaster_profile = [
        (0.000, 0.000),  # crisp tapered insect tail tip (points toward -Z)
        (0.026, 0.016),
        (0.062, 0.054),
        (0.090, 0.098),
        (0.086, 0.106),  # intersegment groove 1
        (0.106, 0.122),
        (0.122, 0.176),
        (0.117, 0.186),  # intersegment groove 2
        (0.125, 0.202),
        (0.122, 0.256),
        (0.098, 0.312),
        (0.058, 0.352),
        (0.000, 0.370),  # front pole meeting petiole
    ]
    gaster = (
        lathe(gaster_profile, radial_segs=24, cap_bottom=False, cap_top=False, axis="z", name="gaster", material="chitin_team")
        .scale(0.96, 0.88, 1.0)
        .rotate_xyz(rx=-0.16)
        .translate(0.0, 0.214, -0.492)
    )
    scene.add("body", gaster)

    # Recessed dark chitin seam rings tucked cleanly into the stepped gaster grooves.
    for idx, (z_off, maj_r) in enumerate([(-0.310, 0.119), (-0.388, 0.090)]):
        dz = z_off - (-0.492)
        y_ring = 0.214 + dz * math.sin(0.16)
        band = (
            torus_arc(maj_r, 0.0064, u_segs=24, v_segs=8, plane="xy", name=f"gaster_seam_{idx}", material="chitin_dark")
            .scale(0.96, 0.88, 1.0)
            .rotate_xyz(rx=-0.16)
            .translate(0.0, y_ring, z_off)
        )
        scene.add("body", band)

    return {
        "neck_center": (0.0, 0.490, 0.014),
        "thorax_center": (0.0, 0.340, -0.010),
        "back_anchor": (0.0, 0.368, -0.142),
        "gaster_center": (0.0, 0.248, -0.296),
        "gaster_top": (0.0, 0.362, -0.272),
    }


def _resolve_arm_points(pose: ArmPoseLike, side: int) -> List[Tuple[float, float, float]]:
    """Resolve arm control points for `side` (+1 = ant left/+X, -1 = ant right/-X)."""
    shoulder = (side * 0.114, 0.438, 0.022)
    if pose is None:
        return [(side * x, y, z) for x, y, z in _ARM_PRESETS_LEFT["idle"]]
    if isinstance(pose, str):
        key = pose.strip().lower()
        if key not in _ARM_PRESETS_LEFT:
            known = ", ".join(sorted(_ARM_PRESETS_LEFT))
            raise ValueError(f"Unknown arm pose {pose!r}; expected one of: {known}")
        return [(side * x, y, z) for x, y, z in _ARM_PRESETS_LEFT[key]]
    pts = [tuple(float(c) for c in p) for p in pose]
    if len(pts) < 2:
        raise ValueError("Custom arm pose requires at least 2 points ([elbow, hand] or [shoulder, ...])")
    if len(pts) == 2:
        pts = [shoulder] + pts
    return pts


def _build_arm(
    scene: SceneBuilder,
    pose: ArmPoseLike,
    side: int,
    label: str,
) -> Tuple[Tuple[float, float, float], Tuple[float, float, float], Tuple[float, float, float]]:
    """Build one sleek segmented dark insect forelimb + sharp pincer claw hand."""
    pts = _resolve_arm_points(pose, side)
    n_pts = len(pts)

    if n_pts == 4:
        p0, p1, p2, p3 = [np.array(p, dtype=np.float64) for p in pts]
        upper = swept_tube(
            [p0, 0.5 * (p0 + p1) + np.array([0.0, 0.009, 0.0]), p1],
            [0.023, 0.026, 0.020],
            radial_segs=10,
            smooth_subdiv=2,
            cap_rings=2,
            name=f"arm_upper_{label}",
            material="chitin_dark",
        )
        forearm = swept_tube(
            [p1, p2, p3],
            [0.022, 0.017, 0.015],
            radial_segs=10,
            smooth_subdiv=2,
            cap_rings=2,
            name=f"arm_fore_{label}",
            material="chitin_dark",
        )
        scene.add("arms", upper)
        scene.add("arms", forearm)
    else:
        rads = list(np.linspace(0.024, 0.016, n_pts))
        arm_mesh = swept_tube(
            pts,
            rads,
            radial_segs=10,
            smooth_subdiv=2,
            cap_rings=2,
            name=f"arm_{label}",
            material="chitin_dark",
        )
        scene.add("arms", arm_mesh)

    # Crisp dark shoulder socket and angular elbow knuckle.
    sx, sy, sz = pts[0]
    scene.add("arms", ellipsoid(0.030, 0.028, 0.030, u_segs=10, v_segs=8, name=f"shoulder_{label}", material="chitin_dark").translate(sx, sy, sz))
    ex, ey, ez = pts[1]
    scene.add("arms", superellipsoid(0.024, 0.024, 0.024, e1=0.72, e2=0.72, u_segs=10, v_segs=8, name=f"elbow_{label}", material="chitin_dark").translate(ex, ey, ez))

    # Sleek tapered insect claw/pincer hand aligned to forearm direction.
    hand = np.array(pts[-1], dtype=np.float64)
    prev = np.array(pts[-2], dtype=np.float64)
    hdir = hand - prev
    hlen = float(np.linalg.norm(hdir))
    hdir = hdir / hlen if hlen > 1e-8 else np.array([0.0, 0.0, 1.0])

    palm = ellipsoid(0.024, 0.022, 0.026, u_segs=12, v_segs=8, name=f"hand_palm_{label}", material="chitin_dark").translate(hand)
    scene.add("arms", palm)

    up_ref = np.array([0.0, 1.0, 0.0]) if abs(hdir[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
    side_v = np.cross(hdir, up_ref)
    side_v /= max(1e-8, float(np.linalg.norm(side_v)))
    up_v = np.cross(side_v, hdir)

    for finger_sign, fname in ((-1.0, "inner"), (1.0, "outer")):
        f_base = hand + 0.008 * hdir + finger_sign * 0.013 * side_v + 0.004 * up_v
        f_mid = hand + 0.030 * hdir + finger_sign * 0.016 * side_v
        f_tip = hand + 0.050 * hdir + finger_sign * 0.005 * side_v - 0.007 * up_v
        claw = swept_tube(
            [f_base, f_mid, f_tip],
            [0.011, 0.009, 0.0045],
            radial_segs=8,
            smooth_subdiv=2,
            cap_rings=2,
            name=f"hand_claw_{fname}_{label}",
            material="chitin_dark",
        )
        scene.add("arms", claw)

    return (
        (float(sx), float(sy), float(sz)),
        (float(hand[0]), float(hand[1]), float(hand[2])),
        (float(hdir[0]), float(hdir[1]), float(hdir[2])),
    )


def _tapered_tarsus_foot(name: str, material: str = "chitin_dark") -> MeshPart:
    """Create a sleek tapered dark insect tarsus claw-foot resting flat on Y = 0.0."""
    tarsus_prof = [
        (0.000, -0.022),
        (0.014, -0.014),
        (0.018, 0.004),
        (0.014, 0.028),
        (0.008, 0.048),
        (0.000, 0.058),
    ]
    foot = lathe(tarsus_prof, radial_segs=12, cap_bottom=False, cap_top=False, axis="z", name=name, material=material)
    foot = foot.scale(1.02, 0.72, 1.0)

    def ground_sole(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        p[:, 1] = np.maximum(p[:, 1], -0.010)
        return p

    foot = foot.deform(ground_sole)
    bmin, _ = foot.bounds()
    return foot.translate(0.0, -float(bmin[1]), 0.0)


def _add_segmented_insect_leg(
    scene: SceneBuilder,
    label: str,
    hip: Tuple[float, float, float],
    knee: Tuple[float, float, float],
    ankle: Tuple[float, float, float],
    foot_pos: Tuple[float, float, float],
    foot_yaw: float,
) -> None:
    """Build one angular segmented insect leg (coxa, femur, sharp knee, tapered tibia, claw foot)."""
    p_hip = np.array(hip, dtype=np.float64)
    p_knee = np.array(knee, dtype=np.float64)
    p_ankle = np.array(ankle, dtype=np.float64)

    # Coxa/hip socket.
    scene.add_mirrored(
        "legs",
        ellipsoid(0.032, 0.028, 0.032, u_segs=10, v_segs=8, name=f"coxa_{label}", material="chitin_dark").translate(p_hip),
    )

    # Muscular angular upper femur rising sharply toward the tucked knee.
    femur_mid = 0.5 * (p_hip + p_knee) + np.array([0.0, 0.014, 0.0])
    femur = swept_tube(
        [p_hip, femur_mid, p_knee],
        [(0.020, 0.024), (0.026, 0.032), (0.019, 0.022)],
        radial_segs=10,
        smooth_subdiv=2,
        cap_rings=2,
        name=f"femur_{label}",
        material="chitin_dark",
    )
    scene.add_mirrored("legs", femur)

    # Crisp angular knee joint cap.
    scene.add_mirrored(
        "legs",
        superellipsoid(0.023, 0.026, 0.023, e1=0.62, e2=0.68, u_segs=10, v_segs=8, name=f"knee_{label}", material="chitin_dark").translate(p_knee),
    )

    # Slender sharply-tapered lower tibia plunging down to the narrow stance ankle.
    tibia_mid = 0.45 * p_knee + 0.55 * p_ankle + np.array([0.008, 0.0, 0.0])
    tibia = swept_tube(
        [p_knee, tibia_mid, p_ankle],
        [0.021, 0.016, 0.011],
        radial_segs=10,
        smooth_subdiv=2,
        cap_rings=2,
        name=f"tibia_{label}",
        material="chitin_dark",
    )
    scene.add_mirrored("legs", tibia)

    # Small ankle knuckle + sleek tapered tarsus claw foot resting flush on Y = 0.0.
    scene.add_mirrored(
        "legs",
        sphere(0.014, u_segs=8, v_segs=6, name=f"ankle_{label}", material="chitin_dark").translate(p_ankle),
    )
    foot = _tapered_tarsus_foot(f"tarsus_{label}").rotate_xyz(ry=foot_yaw).translate(*foot_pos)
    scene.add_mirrored("legs", foot)


def _build_legs(scene: SceneBuilder) -> None:
    # Tucked narrow upright stance matching original agst301 proportions (width ~0.48-0.50).
    _add_segmented_insect_leg(
        scene,
        "mid",
        hip=(0.094, 0.292, 0.024),
        knee=(0.204, 0.302, 0.084),
        ankle=(0.174, 0.024, 0.140),
        foot_pos=(0.174, 0.0, 0.142),
        foot_yaw=0.28,
    )

    _add_segmented_insect_leg(
        scene,
        "rear",
        hip=(0.092, 0.264, -0.068),
        knee=(0.210, 0.294, -0.140),
        ankle=(0.178, 0.024, -0.190),
        foot_pos=(0.178, 0.0, -0.192),
        foot_yaw=math.pi - 0.30,
    )


def build_base_ant(
    scene_name: str = "ant_base",
    *,
    include_antennae: bool = True,
    include_eyes: bool = True,
    include_brows: bool = False,
    left_arm_pose: ArmPoseLike = None,
    right_arm_pose: ArmPoseLike = None,
    head_tilt_x: float = 0.0,
    blink: float = 0.0,
    scale: float = 1.0,
) -> Tuple[SceneBuilder, Dict[str, Any]]:
    """Build the sculpted insectoid base ant and return `(SceneBuilder, anchors)`.

    Parameters
    ----------
    scene_name:
        Root node name in the exported glTF scene (e.g., "ant_worker").
    include_antennae:
        Set False for helmeted classes (e.g., Fire Ant) whose headwear covers them.
    include_eyes / include_brows:
        Toggle 3-part eyes + blinkable eyelids (default True) or optional subtle
        eyelid ridge (default False; sculpted chitin brow arches provide the brow).
    left_arm_pose / right_arm_pose:
        Preset name ("idle", "hold_tool", "hold_front", "hold_high", "akimbo",
        "wide") or an explicit list of 3D control points for that side.
        Remember: +X is ant's left (viewer's right), -X is ant's right.
    head_tilt_x:
        Additional pitch offset in radians around +X.
    blink:
        Resting eyelid closure in [0.0, 1.0] (0.0 = open, 1.0 = fully closed).
        The glTF 2.0 "Blink" animation clip is always exported when eyes exist.
    scale:
        Uniform world-space scale factor (grounded to Y = 0.0 after scaling).
    """
    scene = SceneBuilder(name=scene_name)
    head_center = (0.0, 0.724, 0.050)
    head_radii = (0.228, 0.222, 0.226)

    _build_head(scene, head_center, head_tilt_x)
    eye_anchors: Dict[str, Any] = {
        "eye_left": (0.095, head_center[1] - 0.012, head_center[2] + 0.194),
        "eye_right": (-0.095, head_center[1] - 0.012, head_center[2] + 0.194),
        "eye_center": (0.0, head_center[1] - 0.012, head_center[2] + 0.210),
        "eyelid_closed_angle": EYELID_CLOSED_ANGLE,
    }
    if include_eyes:
        eye_anchors = _build_eyes(scene, head_center, head_tilt_x, include_brows=include_brows, blink=blink)

    ant_anchors = {
        "antenna_base_left": (0.102, head_center[1] + 0.172, head_center[2] + 0.044),
        "antenna_base_right": (-0.102, head_center[1] + 0.172, head_center[2] + 0.044),
    }
    if include_antennae:
        ant_anchors = _build_antennae(scene, head_center)

    body_anchors = _build_body(scene)
    sh_l, hand_l, hdir_l = _build_arm(scene, left_arm_pose, side=+1, label="left")
    sh_r, hand_r, hdir_r = _build_arm(scene, right_arm_pose, side=-1, label="right")
    _build_legs(scene)

    anchors: Dict[str, Any] = {
        "head_center": head_center,
        "head_top": (head_center[0], head_center[1] + head_radii[1], head_center[2]),
        "head_front": (head_center[0], head_center[1], head_center[2] + head_radii[2]),
        "head_back": (head_center[0], head_center[1], head_center[2] - head_radii[2]),
        "head_radius": 0.226,
        "head_radii": head_radii,
        "brow_y": head_center[1] + 0.066,
        "ground_y": 0.0,
        "shoulder_left": sh_l,
        "shoulder_right": sh_r,
        "left_hand": hand_l,
        "right_hand": hand_r,
        "left_hand_dir": hdir_l,
        "right_hand_dir": hdir_r,
    }
    anchors.update(eye_anchors)
    anchors.update(ant_anchors)
    anchors.update(body_anchors)

    if abs(float(scale) - 1.0) > 1e-6:
        s = float(scale)
        scene.scale_all(s)
        for key, val in list(anchors.items()):
            if key.endswith("_dir") or key == "eyelid_closed_angle":
                continue
            if isinstance(val, tuple) and len(val) == 3:
                anchors[key] = (val[0] * s, val[1] * s, val[2] * s)
            elif isinstance(val, (int, float)) and key != "ground_y":
                anchors[key] = float(val) * s

    dy = scene.ground_to_y(0.0)
    if abs(dy) > 1e-6:
        for key, val in list(anchors.items()):
            if key.endswith("_dir") or key in ("head_radii", "eyelid_closed_angle"):
                continue
            if isinstance(val, tuple) and len(val) == 3:
                anchors[key] = (val[0], val[1] + dy, val[2])
            elif key == "brow_y":
                anchors[key] = float(val) + dy
    return scene, anchors
