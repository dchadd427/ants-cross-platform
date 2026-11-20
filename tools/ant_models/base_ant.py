"""Sculpted high-detail organic insectoid base ant shared by Worker, Thief, Bomber, Swimmer, and Fire.

Coordinate system (glTF 2.0 right-handed):
  +Y = up (tarsus tips grounded solidly on Y = 0.0)
  +Z = front (South / canonical camera 301)
  +X = ant's left (viewer's right from front), -X = ant's right (viewer's left)

Features a clean sculpted shield-oval head carapace with crisp V-groove center
cranial seam, big wide-open alert 3-part eyes (sclera + olive-gold iris + pupil)
with pivoted glTF 2.0 "Blink" eyelids, curved toothed mandibles, sturdy muscular
tapered insect legs, pinched petiole waist, and overlapping stepped tergite
gaster plates matching original Ants (1998) upright Worker proportions.
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
        superellipsoid,
        swept_tube,
        torus_arc,
    )

ArmPoseLike = Optional[Union[str, Sequence[Sequence[float]]]]

EYELID_CLOSED_ANGLE = 1.66  # radians around local +X (~95 deg) from wide-open +45 deg to sealed -50 deg

# Canonical left-side (+X) arm control curves: [shoulder, elbow, wrist, hand].
_ARM_PRESETS_LEFT: Dict[str, List[Tuple[float, float, float]]] = {
    "idle": [
        (0.108, 0.440, 0.020),
        (0.208, 0.402, 0.076),
        (0.184, 0.354, 0.182),
        (0.156, 0.348, 0.218),
    ],
    "hold_tool": [
        (0.108, 0.440, 0.020),
        (0.228, 0.410, 0.092),
        (0.232, 0.416, 0.216),
        (0.220, 0.436, 0.268),
    ],
    "hold_front": [
        (0.108, 0.440, 0.020),
        (0.228, 0.410, 0.092),
        (0.232, 0.416, 0.216),
        (0.220, 0.436, 0.268),
    ],
    "hold_high": [
        (0.108, 0.440, 0.020),
        (0.238, 0.454, 0.106),
        (0.246, 0.550, 0.220),
        (0.234, 0.594, 0.274),
    ],
    "akimbo": [
        (0.108, 0.440, 0.020),
        (0.240, 0.404, 0.052),
        (0.214, 0.336, 0.142),
        (0.186, 0.324, 0.172),
    ],
    "wide": [
        (0.108, 0.440, 0.020),
        (0.240, 0.404, 0.052),
        (0.214, 0.336, 0.142),
        (0.186, 0.324, 0.172),
    ],
}


def _head_sculpt_fn(pos: np.ndarray) -> np.ndarray:
    """Sculpt a clean, handsome shield-oval insect head carapace (zero lumpy brow blobs).

    Features a sleek gently-angled forehead crown, a crisp tight vertical V-groove
    center cranial seam, clean shallow orbital framing for big alert eyes, and a
    graceful heart/shield taper down to the clypeus and mandibles.
    """
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    ax = np.abs(x)
    front_mask = np.clip((z - 0.02) / 0.18, 0.0, 1.0)

    # 1. Clean gently-sloped forehead crown plateau (smooth specular transition, no lumps).
    crown_y = 0.190 - 0.044 * np.clip((z + 0.08) / 0.28, 0.0, 1.0)
    over = np.maximum(0.0, y - crown_y)
    p[:, 1] = y - 0.38 * over

    # 2. Clean subtle temple planes so sides taper gracefully instead of puffing out.
    temple_limit = 0.220 - 0.036 * np.clip((-y + 0.02) / 0.24, 0.0, 1.0) - 0.020 * np.clip((-z - 0.05) / 0.18, 0.0, 1.0)
    side_over = np.maximum(0.0, ax - temple_limit)
    p[:, 0] = np.sign(x) * (ax - 0.36 * side_over)
    ax = np.abs(p[:, 0])

    # 3. Tight, crisp V-groove center cranial seam running up the forehead (clean & refined).
    seam = (
        np.exp(-((ax / 0.015) ** 1.6))
        * np.exp(-((y - 0.088) / 0.092) ** 2)
        * np.clip((z + 0.02) / 0.18, 0.0, 1.0)
    )
    p[:, 2] -= 0.014 * seam
    p[:, 1] -= 0.006 * seam

    # 4. Gentle smooth orbital seat (keeps eyes proud and alert, avoids sunken-skull sockets on blink).
    socket = (
        np.exp(-(((ax - 0.096) / 0.068) ** 2 + ((y + 0.010) / 0.070) ** 2))
        * np.clip((z - 0.08) / 0.14, 0.0, 1.0)
    )
    p[:, 2] -= 0.012 * socket

    # 5. Handsome shield-oval taper below the eyes into the sculpted clypeus bridge.
    lower = np.clip((-y - 0.018) / 0.198, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.36 * (lower ** 1.32)
    p[:, 2] *= 1.0 - 0.14 * (lower ** 1.40)

    clypeus = np.exp(-((x / 0.056) ** 2)) * np.exp(-((y + 0.100) / 0.062) ** 2) * front_mask
    p[:, 2] += 0.016 * clypeus

    # 6. Clean occipital taper into the slender neck behind the head.
    rear_low = np.clip((-z - 0.04) / 0.18, 0.0, 1.0) * np.clip((-y + 0.05) / 0.24, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.16 * rear_low
    p[:, 2] += 0.018 * rear_low
    return p


def _build_mandibles(scene: SceneBuilder, head_center: Tuple[float, float, float], head_tilt_x: float) -> None:
    """Build curved sculpted toothed chitin pincer mandibles + dark mouth cleft."""
    _, hy, hz = head_center

    jaw_pts = [
        (0.082, hy - 0.136, hz + 0.120),
        (0.106, hy - 0.164, hz + 0.180),
        (0.084, hy - 0.180, hz + 0.238),
        (0.030, hy - 0.184, hz + 0.260),
    ]
    jaw_rads = [
        (0.023, 0.032),
        (0.025, 0.036),
        (0.018, 0.028),
        (0.008, 0.013),
    ]
    jaw_left = swept_tube(
        jaw_pts,
        jaw_rads,
        radial_segs=10,
        smooth_subdiv=3,
        cap_rings=2,
        name="mandible_jaw",
        material="chitin_team",
    )
    if abs(head_tilt_x) > 1e-6:
        jaw_left = jaw_left.translate(0.0, -hy, -hz).rotate_xyz(rx=head_tilt_x).translate(0.0, hy, hz)
    scene.add_mirrored("head", jaw_left)

    for t_idx, (tx, ty, tz, rad, hgt, rz_deg) in enumerate(
        (
            (0.058, hy - 0.178, hz + 0.234, 0.011, 0.028, 74.0),
            (0.036, hy - 0.182, hz + 0.250, 0.009, 0.024, 86.0),
        )
    ):
        tooth = (
            cone(rad, hgt, radial_segs=8, name=f"mandible_tooth_{t_idx}", material="chitin_team")
            .rotate_xyz(rx=12.0, ry=-18.0, rz=rz_deg, degrees=True)
            .translate(tx, ty, tz)
        )
        scene.add_mirrored("head", tooth)

    mouth_cleft = (
        superellipsoid(0.044, 0.024, 0.040, e1=0.65, e2=0.70, u_segs=14, v_segs=8, name="mouth_cleft", material="chitin_dark")
        .translate(0.0, hy - 0.162, hz + 0.154)
    )
    scene.add("head", mouth_cleft)


def _build_head(scene: SceneBuilder, head_center: Tuple[float, float, float], head_tilt_x: float) -> None:
    hx, hy, hz = head_center
    cranium = (
        superellipsoid(0.228, 0.222, 0.226, e1=0.68, e2=0.74, u_segs=36, v_segs=28, name="cranium", material="chitin_team")
        .deform(_head_sculpt_fn)
        .rotate_xyz(rx=head_tilt_x)
        .translate(hx, hy, hz)
    )
    scene.add("head", cranium)
    _build_mandibles(scene, head_center, head_tilt_x)


def _build_eyelid_shell(name: str, yaw_y: float) -> MeshPart:
    """Build a smooth curved chitin_team upper eyelid shell in eye-local coordinates.

    At neutral rotation (0, 0, 0, 1) the front lip sits tucked cleanly at +47 deg
    clear of the eye so the ant looks wide-open and alert. Rotating around local
    +X by +EYELID_CLOSED_ANGLE sweeps the convex shell smoothly down to -42 deg,
    sealing 100% of sclera, iris, and pupil flush inside the orbital rim.
    """
    lid = hemisphere_shell(
        0.076,
        0.080,
        0.074,
        thickness=0.006,
        start_phi=-0.14,
        u_segs=24,
        v_segs=10,
        name=name,
        material="chitin_team",
    )
    return lid.rotate_xyz(rx=-0.92, ry=yaw_y)


def _build_eyes(
    scene: SceneBuilder,
    head_center: Tuple[float, float, float],
    head_tilt_x: float,
    *,
    include_brows: bool = False,
    blink: float = 0.0,
) -> Dict[str, Any]:
    """Build bold, wide-open alert 3-part eyes + pivoted blinkable eyelids."""
    _, hy, hz = head_center
    eye_x = 0.096
    eye_y = hy - 0.012
    eye_z = hz + 0.198

    # Clean sculpted orbital socket rim framing each big alert eye.
    socket_rim = (
        torus_arc(0.073, 0.0095, u_segs=24, v_segs=8, plane="xy", name="eye_socket_rim", material="chitin_dark")
        .scale(0.96, 1.03, 0.90)
        .rotate_xyz(rx=-0.16 + head_tilt_x, ry=0.20)
        .translate(eye_x, eye_y, eye_z + 0.005)
    )
    scene.add_mirrored("eyes", socket_rim)

    # Big warm cream-white sclera (~26% larger projected area than Pass 2 for wide-awake charm).
    sclera_left = (
        superellipsoid(0.072, 0.077, 0.056, e1=0.88, e2=0.90, u_segs=22, v_segs=16, name="sclera", material="eye_white")
        .rotate_xyz(rx=-0.16 + head_tilt_x, ry=0.20)
        .translate(eye_x, eye_y, eye_z)
    )
    scene.add_mirrored("eyes", sclera_left)

    # Vivid olive-amber iris dome seated cleanly on the front surface of the sclera.
    iris_left = (
        ellipsoid(0.043, 0.046, 0.022, u_segs=20, v_segs=12, name="iris", material="eye_iris")
        .rotate_xyz(rx=-0.14 + head_tilt_x, ry=0.15)
        .translate(eye_x - 0.005, eye_y + 0.003, eye_z + 0.041)
    )
    scene.add_mirrored("eyes", iris_left)

    # Crisp obsidian pupil centered cleanly inside the iris ring.
    pupil_left = (
        ellipsoid(0.024, 0.026, 0.015, u_segs=16, v_segs=10, name="pupil", material="eye_pupil")
        .rotate_xyz(rx=-0.14 + head_tilt_x, ry=0.13)
        .translate(eye_x - 0.006, eye_y + 0.004, eye_z + 0.051)
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

    if include_brows:
        lid_pts = [
            (0.042, eye_y + 0.074, eye_z + 0.016),
            (0.096, eye_y + 0.088, eye_z + 0.018),
            (0.154, eye_y + 0.066, eye_z - 0.006),
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
    """Build sturdy sleek 2-segment kinked insect antennae with tapered sensory clubs."""
    _, hy, hz = head_center
    base_l = (0.100, hy + 0.172, hz + 0.044)
    elbow_l = (0.142, hy + 0.328, hz + 0.060)
    mid2_l = (0.164, hy + 0.344, hz + 0.120)
    tip_l = (0.174, hy + 0.298, hz + 0.180)

    scape = swept_tube(
        [base_l, (0.120, hy + 0.252, hz + 0.052), elbow_l],
        [(0.016, 0.018), (0.013, 0.015), (0.015, 0.017)],
        radial_segs=10,
        smooth_subdiv=2,
        cap_rings=2,
        name="antenna_scape",
        material="chitin_dark",
    )
    scene.add_mirrored("antennae", scape)

    scene.add_mirrored(
        "antennae",
        superellipsoid(0.018, 0.019, 0.018, e1=0.68, e2=0.68, u_segs=10, v_segs=8, name="antenna_joint", material="chitin_dark").translate(*elbow_l),
    )

    funiculus = swept_tube(
        [elbow_l, mid2_l, (0.172, hy + 0.316, hz + 0.160), tip_l],
        [(0.014, 0.016), (0.017, 0.020), (0.021, 0.024), (0.011, 0.014)],
        radial_segs=10,
        smooth_subdiv=3,
        cap_rings=3,
        name="antenna_club",
        material="chitin_dark",
    )
    scene.add_mirrored("antennae", funiculus)

    socket_left = (
        superellipsoid(0.028, 0.016, 0.028, e1=0.64, e2=0.68, u_segs=12, v_segs=8, name="antenna_socket", material="chitin_dark")
        .rotate_xyz(rx=0.22, rz=-0.30)
        .translate(*base_l)
    )
    scene.add_mirrored("antennae", socket_left)

    return {
        "antenna_base_left": base_l,
        "antenna_base_right": (-base_l[0], base_l[1], base_l[2]),
    }


def _gaster_keel_fn(pos: np.ndarray) -> np.ndarray:
    """Subtle dorsal armor keel and gently flattened ventral plate on the stepped insect gaster."""
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    top_mask = np.clip(y / 0.12, 0.0, 1.0)
    p[:, 1] += 0.010 * np.exp(-((x / 0.048) ** 2)) * top_mask
    ventral = np.clip((-y - 0.045) / 0.08, 0.0, 1.0)
    p[:, 1] += 0.014 * ventral
    return p


def _build_body(scene: SceneBuilder) -> Dict[str, Tuple[float, float, float]]:
    """Build segmented armored thorax, crisp pinched petiole waist, and stepped-tergite gaster."""
    neck = (
        lathe([(0.062, 0.0), (0.054, 0.052), (0.064, 0.116)], radial_segs=14, name="neck", material="chitin_dark")
        .rotate_xyz(rx=0.14)
        .translate(0.0, 0.442, 0.010)
    )
    scene.add("body", neck)
    neck_collar = (
        torus_arc(0.072, 0.013, u_segs=16, v_segs=8, plane="xz", name="neck_collar", material="chitin_dark")
        .rotate_xyz(rx=0.14)
        .translate(0.0, 0.456, 0.012)
    )
    scene.add("body", neck_collar)

    # Armored 2-segment thorax: sculpted pronotum upper torso + tapered lower metathorax waist.
    pronotum_core = (
        superellipsoid(0.102, 0.088, 0.108, e1=0.62, e2=0.68, u_segs=20, v_segs=14, name="pronotum_core", material="chitin_dark")
        .rotate_xyz(rx=0.12)
        .translate(0.0, 0.386, -0.004)
    )
    scene.add("body", pronotum_core)

    metathorax = (
        superellipsoid(0.090, 0.086, 0.096, e1=0.64, e2=0.70, u_segs=18, v_segs=12, name="metathorax_core", material="chitin_dark")
        .rotate_xyz(rx=-0.10)
        .translate(0.0, 0.278, -0.022)
    )
    scene.add("body", metathorax)

    # Sculpted team-colored chest carapace and dorsal pronotum shield.
    chest_plate = (
        superellipsoid(0.090, 0.084, 0.058, e1=0.58, e2=0.66, u_segs=18, v_segs=14, name="chest_plate", material="chitin_team")
        .rotate_xyz(rx=0.18)
        .translate(0.0, 0.376, 0.060)
    )
    scene.add("body", chest_plate)

    lower_chest = (
        superellipsoid(0.074, 0.056, 0.048, e1=0.60, e2=0.66, u_segs=16, v_segs=10, name="sternum_lower", material="chitin_team")
        .rotate_xyz(rx=-0.12)
        .translate(0.0, 0.278, 0.042)
    )
    scene.add("body", lower_chest)

    dorsal_plate = (
        superellipsoid(0.092, 0.086, 0.060, e1=0.58, e2=0.66, u_segs=18, v_segs=14, name="dorsal_plate", material="chitin_team")
        .rotate_xyz(rx=-0.16)
        .translate(0.0, 0.388, -0.062)
    )
    scene.add("body", dorsal_plate)

    # Distinct pinched insect petiole waist stalk + sculpted dorsal petiole scale node.
    petiole_stem = swept_tube(
        [(0.0, 0.258, -0.088), (0.0, 0.244, -0.132), (0.0, 0.240, -0.172)],
        [(0.036, 0.038), (0.026, 0.028), (0.034, 0.036)],
        radial_segs=10,
        smooth_subdiv=2,
        cap_rings=2,
        name="petiole_stem",
        material="chitin_dark",
    )
    scene.add("body", petiole_stem)

    petiole_node = (
        superellipsoid(0.040, 0.048, 0.026, e1=0.58, e2=0.64, u_segs=14, v_segs=10, name="petiole_node", material="chitin_team")
        .rotate_xyz(rx=-0.18)
        .translate(0.0, 0.270, -0.132)
    )
    scene.add("body", petiole_node)

    # Pointed armored insect gaster with 4 overlapping stepped tergite carapace plates.
    gaster_profile = [
        (0.000, 0.000),
        (0.020, 0.014),
        (0.050, 0.048),
        (0.074, 0.082),
        (0.065, 0.088),  # stepped tergite ledge 1
        (0.088, 0.104),
        (0.108, 0.148),
        (0.098, 0.156),  # stepped tergite ledge 2
        (0.120, 0.172),
        (0.126, 0.222),
        (0.115, 0.230),  # stepped tergite ledge 3
        (0.124, 0.248),
        (0.116, 0.298),
        (0.088, 0.338),
        (0.046, 0.362),
        (0.000, 0.372),
    ]
    gaster = (
        lathe(gaster_profile, radial_segs=24, cap_bottom=False, cap_top=False, axis="z", name="gaster", material="chitin_team")
        .deform(_gaster_keel_fn)
        .scale(0.94, 0.88, 1.0)
        .rotate_xyz(rx=-0.16)
        .translate(0.0, 0.214, -0.506)
    )
    scene.add("body", gaster)

    for idx, (z_off, maj_r) in enumerate([(-0.278, 0.117), (-0.352, 0.100), (-0.420, 0.068)]):
        dz = z_off - (-0.506)
        y_ring = 0.214 + dz * math.sin(0.16)
        band = (
            torus_arc(maj_r, 0.0064, u_segs=24, v_segs=8, plane="xy", name=f"gaster_seam_{idx}", material="chitin_dark")
            .scale(0.94, 0.88, 1.0)
            .rotate_xyz(rx=-0.16)
            .translate(0.0, y_ring, z_off)
        )
        scene.add("body", band)

    return {
        "neck_center": (0.0, 0.490, 0.014),
        "thorax_center": (0.0, 0.340, -0.010),
        "back_anchor": (0.0, 0.368, -0.142),
        "gaster_center": (0.0, 0.248, -0.308),
        "gaster_top": (0.0, 0.358, -0.282),
    }


def _resolve_arm_points(pose: ArmPoseLike, side: int) -> List[Tuple[float, float, float]]:
    """Resolve arm control points for `side` (+1 = ant left/+X, -1 = ant right/-X)."""
    shoulder = (side * 0.108, 0.440, 0.020)
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
    """Build one sturdy sculpted tapered insect forelimb + articulated elbow + twin pincer claws."""
    pts = _resolve_arm_points(pose, side)
    n_pts = len(pts)

    if n_pts == 4:
        p0, p1, p2, p3 = [np.array(p, dtype=np.float64) for p in pts]
        upper = swept_tube(
            [p0, 0.5 * (p0 + p1) + np.array([0.0, 0.008, 0.0]), p1],
            [(0.022, 0.026), (0.027, 0.033), (0.021, 0.025)],
            radial_segs=10,
            smooth_subdiv=2,
            cap_rings=2,
            name=f"arm_upper_{label}",
            material="chitin_dark",
        )
        forearm = swept_tube(
            [p1, p2, p3],
            [(0.022, 0.026), (0.019, 0.022), (0.016, 0.018)],
            radial_segs=10,
            smooth_subdiv=2,
            cap_rings=2,
            name=f"arm_fore_{label}",
            material="chitin_dark",
        )
        scene.add("arms", upper)
        scene.add("arms", forearm)
    else:
        rads = [(float(r) * 0.88, float(r) * 1.12) for r in np.linspace(0.028, 0.019, n_pts)]
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

    # Sturdy sculpted shoulder cap and articulated elbow joint.
    sx, sy, sz = pts[0]
    scene.add("arms", superellipsoid(0.034, 0.032, 0.034, e1=0.66, e2=0.70, u_segs=12, v_segs=8, name=f"shoulder_{label}", material="chitin_dark").translate(sx, sy, sz))
    ex, ey, ez = pts[1]
    scene.add("arms", superellipsoid(0.027, 0.028, 0.027, e1=0.64, e2=0.68, u_segs=10, v_segs=8, name=f"elbow_{label}", material="chitin_dark").translate(ex, ey, ez))

    # Sculpted insect claw/pincer hand aligned to forearm direction.
    hand = np.array(pts[-1], dtype=np.float64)
    prev = np.array(pts[-2], dtype=np.float64)
    hdir = hand - prev
    hlen = float(np.linalg.norm(hdir))
    hdir = hdir / hlen if hlen > 1e-8 else np.array([0.0, 0.0, 1.0])

    palm = superellipsoid(0.025, 0.023, 0.028, e1=0.68, e2=0.72, u_segs=12, v_segs=8, name=f"hand_palm_{label}", material="chitin_dark").translate(hand)
    scene.add("arms", palm)

    up_ref = np.array([0.0, 1.0, 0.0]) if abs(hdir[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
    side_v = np.cross(hdir, up_ref)
    side_v /= max(1e-8, float(np.linalg.norm(side_v)))
    up_v = np.cross(side_v, hdir)

    for finger_sign, fname in ((-1.0, "inner"), (1.0, "outer")):
        f_base = hand + 0.008 * hdir + finger_sign * 0.015 * side_v + 0.003 * up_v
        f_mid = hand + 0.032 * hdir + finger_sign * 0.018 * side_v
        f_tip = hand + 0.054 * hdir + finger_sign * 0.005 * side_v - 0.008 * up_v
        claw = swept_tube(
            [f_base, f_mid, f_tip],
            [(0.010, 0.013), (0.008, 0.011), (0.004, 0.005)],
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
    """Create a sturdy tapered sculpted insect tarsus claw-foot resting flat on Y = 0.0."""
    tarsus_prof = [
        (0.000, -0.024),
        (0.018, -0.014),
        (0.023, 0.006),
        (0.019, 0.034),
        (0.011, 0.056),
        (0.000, 0.068),
    ]
    foot = lathe(tarsus_prof, radial_segs=12, cap_bottom=False, cap_top=False, axis="z", name=name, material=material)
    foot = foot.scale(1.04, 0.76, 1.0)

    def ground_sole(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        p[:, 1] = np.maximum(p[:, 1], -0.013)
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
    """Build one sturdy sculpted segmented insect leg (muscular femur, strong knee, tapered shin, claw foot)."""
    p_hip = np.array(hip, dtype=np.float64)
    p_knee = np.array(knee, dtype=np.float64)
    p_ankle = np.array(ankle, dtype=np.float64)

    # Sculpted coxa/hip joint socket.
    scene.add_mirrored(
        "legs",
        superellipsoid(0.036, 0.032, 0.036, e1=0.66, e2=0.70, u_segs=12, v_segs=8, name=f"coxa_{label}", material="chitin_dark").translate(p_hip),
    )

    # Sturdy muscular upper femur (~1.6x stronger than Pass 2 wire legs).
    femur_mid = 0.5 * (p_hip + p_knee) + np.array([0.0, 0.014, 0.0])
    femur = swept_tube(
        [p_hip, femur_mid, p_knee],
        [(0.023, 0.028), (0.031, 0.039), (0.023, 0.029)],
        radial_segs=10,
        smooth_subdiv=2,
        cap_rings=2,
        name=f"femur_{label}",
        material="chitin_dark",
    )
    scene.add_mirrored("legs", femur)

    # Defined articulated chitin knee joint cap with subtle armored knee crest.
    scene.add_mirrored(
        "legs",
        superellipsoid(0.028, 0.031, 0.028, e1=0.60, e2=0.66, u_segs=12, v_segs=8, name=f"knee_{label}", material="chitin_dark").translate(p_knee),
    )
    knee_crest = (
        cone(0.013, 0.028, radial_segs=8, name=f"knee_crest_{label}", material="chitin_dark")
        .rotate_xyz(rx=0.16 if p_knee[2] >= 0.0 else -0.16, rz=-0.58)
        .translate(float(p_knee[0] + 0.011), float(p_knee[1] + 0.014), float(p_knee[2]))
    )
    scene.add_mirrored("legs", knee_crest)

    # Substantial tapered lower tibia shin plunging firmly to the ankle (no fragile toothpicks).
    tibia_mid = 0.45 * p_knee + 0.55 * p_ankle + np.array([0.008, 0.0, 0.0])
    tibia = swept_tube(
        [p_knee, tibia_mid, p_ankle],
        [(0.023, 0.028), (0.018, 0.022), (0.014, 0.016)],
        radial_segs=10,
        smooth_subdiv=2,
        cap_rings=2,
        name=f"tibia_{label}",
        material="chitin_dark",
    )
    scene.add_mirrored("legs", tibia)

    # Articulated ankle cuff + grounded sculpted tarsus claw foot flush on Y = 0.0.
    scene.add_mirrored(
        "legs",
        superellipsoid(0.018, 0.018, 0.018, e1=0.68, e2=0.70, u_segs=10, v_segs=8, name=f"ankle_{label}", material="chitin_dark").translate(p_ankle),
    )
    foot = _tapered_tarsus_foot(f"tarsus_{label}").rotate_xyz(ry=foot_yaw).translate(*foot_pos)
    scene.add_mirrored("legs", foot)


def _build_legs(scene: SceneBuilder) -> None:
    # Upright narrow heroic stance (~0.48-0.51 W x 1.08 H, solidly grounded at Y=0.0).
    _add_segmented_insect_leg(
        scene,
        "mid",
        hip=(0.092, 0.292, 0.024),
        knee=(0.208, 0.304, 0.086),
        ankle=(0.178, 0.028, 0.142),
        foot_pos=(0.178, 0.0, 0.146),
        foot_yaw=0.28,
    )

    _add_segmented_insect_leg(
        scene,
        "rear",
        hip=(0.090, 0.264, -0.068),
        knee=(0.214, 0.296, -0.142),
        ankle=(0.182, 0.028, -0.192),
        foot_pos=(0.182, 0.0, -0.196),
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
    """Build the sculpted insectoid base ant and return `(SceneBuilder, anchors)`."""
    scene = SceneBuilder(name=scene_name)
    head_center = (0.0, 0.724, 0.050)
    head_radii = (0.228, 0.222, 0.226)

    _build_head(scene, head_center, head_tilt_x)
    eye_anchors: Dict[str, Any] = {
        "eye_left": (0.096, head_center[1] - 0.012, head_center[2] + 0.198),
        "eye_right": (-0.096, head_center[1] - 0.012, head_center[2] + 0.198),
        "eye_center": (0.0, head_center[1] - 0.012, head_center[2] + 0.214),
        "eyelid_closed_angle": EYELID_CLOSED_ANGLE,
    }
    if include_eyes:
        eye_anchors = _build_eyes(scene, head_center, head_tilt_x, include_brows=include_brows, blink=blink)

    ant_anchors = {
        "antenna_base_left": (0.100, head_center[1] + 0.172, head_center[2] + 0.044),
        "antenna_base_right": (-0.100, head_center[1] + 0.172, head_center[2] + 0.044),
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
