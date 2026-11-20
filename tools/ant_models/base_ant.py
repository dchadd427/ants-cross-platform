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
        (0.092, 0.476, 0.014),
        (0.184, 0.436, 0.062),
        (0.166, 0.388, 0.154),
        (0.138, 0.382, 0.186),
    ],
    "hold_tool": [
        (0.092, 0.476, 0.014),
        (0.204, 0.444, 0.076),
        (0.210, 0.450, 0.184),
        (0.196, 0.470, 0.232),
    ],
    "hold_front": [
        (0.092, 0.476, 0.014),
        (0.204, 0.444, 0.076),
        (0.210, 0.450, 0.184),
        (0.196, 0.470, 0.232),
    ],
    "hold_high": [
        (0.092, 0.476, 0.014),
        (0.208, 0.486, 0.076),
        (0.224, 0.566, 0.172),
        (0.212, 0.622, 0.226),
    ],
    "akimbo": [
        (0.092, 0.476, 0.014),
        (0.210, 0.438, 0.044),
        (0.190, 0.376, 0.126),
        (0.164, 0.366, 0.154),
    ],
    "wide": [
        (0.092, 0.476, 0.014),
        (0.210, 0.438, 0.044),
        (0.190, 0.376, 0.126),
        (0.164, 0.366, 0.154),
    ],
}


def _head_sculpt_fn(pos: np.ndarray) -> np.ndarray:
    """Sculpt a tall, smooth rounded egg-shield insect cranium matching original Ants proportions.

    Preserves a full rounded upper crown (never squashed or square-cornered), adds a crisp
    subtle vertical V-groove center cranial seam, clean shallow orbital framing for big alert
    eyes, and a graceful organic egg/shield taper down to the clypeus and mandibles.
    """
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    ax = np.abs(x)
    front_mask = np.clip((z - 0.01) / 0.16, 0.0, 1.0)

    # 1. Crisp, subtle vertical V-groove center cranial seam running up the rounded forehead.
    seam = (
        np.exp(-((ax / 0.015) ** 1.6))
        * np.exp(-((y - 0.098) / 0.096) ** 2)
        * np.clip((z + 0.01) / 0.15, 0.0, 1.0)
    )
    p[:, 2] -= 0.010 * seam
    p[:, 1] -= 0.004 * seam

    # 2. Clean shallow orbital seat so big alert eyes sit proud and flush without dark hollows.
    socket = (
        np.exp(-(((ax - 0.086) / 0.064) ** 2 + ((y + 0.022) / 0.068) ** 2))
        * np.clip((z - 0.06) / 0.12, 0.0, 1.0)
    )
    p[:, 2] -= 0.009 * socket

    # 3. Gentle rounded egg-shield taper below the eyes into the sculpted clypeus chin.
    lower = np.clip((-y - 0.024) / 0.220, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.32 * (lower ** 1.38)
    p[:, 2] *= 1.0 - 0.12 * (lower ** 1.45)

    clypeus = np.exp(-((x / 0.052) ** 2)) * np.exp(-((y + 0.118) / 0.062) ** 2) * front_mask
    p[:, 2] += 0.013 * clypeus

    # 4. Soft occipital taper into the slender neck at the lower rear of the skull.
    rear_low = np.clip((-z - 0.03) / 0.15, 0.0, 1.0) * np.clip((-y + 0.04) / 0.25, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.15 * rear_low
    p[:, 2] += 0.014 * rear_low
    return p


def _build_mandibles(scene: SceneBuilder, head_center: Tuple[float, float, float], head_tilt_x: float) -> None:
    """Build curved sculpted toothed chitin pincer mandibles + dark mouth cleft."""
    _, hy, hz = head_center

    jaw_pts = [
        (0.072, hy - 0.162, hz + 0.094),
        (0.094, hy - 0.190, hz + 0.144),
        (0.074, hy - 0.204, hz + 0.194),
        (0.026, hy - 0.206, hz + 0.212),
    ]
    jaw_rads = [
        (0.021, 0.028),
        (0.022, 0.032),
        (0.016, 0.024),
        (0.007, 0.011),
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
            (0.052, hy - 0.202, hz + 0.190, 0.010, 0.024, 74.0),
            (0.032, hy - 0.205, hz + 0.204, 0.008, 0.020, 86.0),
        )
    ):
        tooth = (
            cone(rad, hgt, radial_segs=8, name=f"mandible_tooth_{t_idx}", material="chitin_team")
            .rotate_xyz(rx=12.0, ry=-18.0, rz=rz_deg, degrees=True)
            .translate(tx, ty, tz)
        )
        scene.add_mirrored("head", tooth)

    mouth_cleft = (
        ellipsoid(0.040, 0.022, 0.034, u_segs=14, v_segs=8, name="mouth_cleft", material="chitin_dark")
        .translate(0.0, hy - 0.186, hz + 0.126)
    )
    scene.add("head", mouth_cleft)


def _build_head(scene: SceneBuilder, head_center: Tuple[float, float, float], head_tilt_x: float) -> None:
    hx, hy, hz = head_center
    cranium = (
        superellipsoid(0.212, 0.244, 0.180, e1=0.84, e2=0.86, u_segs=36, v_segs=28, name="cranium", material="chitin_team")
        .deform(_head_sculpt_fn)
        .rotate_xyz(rx=head_tilt_x)
        .translate(hx, hy, hz)
    )
    scene.add("head", cranium)
    _build_mandibles(scene, head_center, head_tilt_x)


def _build_eyelid_shell(name: str, yaw_y: float) -> MeshPart:
    """Build a smooth curved chitin_team upper eyelid shell in eye-local coordinates.

    At neutral rotation (0, 0, 0, 1) the front lip sits tucked cleanly clear of the
    eye so the ant looks wide-open and alert. Rotating around local +X by
    +EYELID_CLOSED_ANGLE sweeps the convex shell smoothly down to seal 100% of
    sclera, iris, and pupil flush inside the orbital rim.
    """
    lid = hemisphere_shell(
        0.072,
        0.077,
        0.068,
        thickness=0.0055,
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
    eye_x = 0.086
    eye_y = hy - 0.022
    eye_z = hz + 0.152

    # Clean sculpted orbital socket rim framing each big alert eye.
    socket_rim = (
        torus_arc(0.069, 0.009, u_segs=24, v_segs=8, plane="xy", name="eye_socket_rim", material="chitin_dark")
        .scale(0.95, 1.04, 0.88)
        .rotate_xyz(rx=-0.14 + head_tilt_x, ry=0.22)
        .translate(eye_x, eye_y, eye_z + 0.004)
    )
    scene.add_mirrored("eyes", socket_rim)

    # Big warm cream-white sclera sized harmoniously to the tall rounded egg-shield head.
    sclera_left = (
        superellipsoid(0.068, 0.075, 0.050, e1=0.88, e2=0.90, u_segs=22, v_segs=16, name="sclera", material="eye_white")
        .rotate_xyz(rx=-0.14 + head_tilt_x, ry=0.22)
        .translate(eye_x, eye_y, eye_z)
    )
    scene.add_mirrored("eyes", sclera_left)

    # Vivid olive-amber iris dome seated cleanly on the front surface of the sclera.
    iris_left = (
        ellipsoid(0.041, 0.045, 0.020, u_segs=20, v_segs=12, name="iris", material="eye_iris")
        .rotate_xyz(rx=-0.12 + head_tilt_x, ry=0.16)
        .translate(eye_x - 0.005, eye_y + 0.003, eye_z + 0.036)
    )
    scene.add_mirrored("eyes", iris_left)

    # Crisp obsidian pupil centered cleanly inside the iris ring.
    pupil_left = (
        ellipsoid(0.023, 0.025, 0.014, u_segs=16, v_segs=10, name="pupil", material="eye_pupil")
        .rotate_xyz(rx=-0.12 + head_tilt_x, ry=0.14)
        .translate(eye_x - 0.006, eye_y + 0.004, eye_z + 0.045)
    )
    scene.add_mirrored("eyes", pupil_left)

    # Pivoted blinkable upper eyelid nodes (one per eye, pivoted right at each eye center).
    blink_clamped = max(0.0, min(1.0, float(blink)))
    rest_rot = quat_from_axis_angle((1.0, 0.0, 0.0), blink_clamped * EYELID_CLOSED_ANGLE)

    scene.add("eyelid_left", _build_eyelid_shell("eyelid_left_shell", yaw_y=+0.20))
    scene.set_node_transform("eyelid_left", translation=(eye_x, eye_y, eye_z), rotation=rest_rot)

    scene.add("eyelid_right", _build_eyelid_shell("eyelid_right_shell", yaw_y=-0.20))
    scene.set_node_transform("eyelid_right", translation=(-eye_x, eye_y, eye_z), rotation=rest_rot)

    scene.add_blink_animation(("eyelid_left", "eyelid_right"), closed_angle=EYELID_CLOSED_ANGLE, name="Blink")

    if include_brows:
        lid_pts = [
            (0.036, eye_y + 0.072, eye_z + 0.014),
            (0.086, eye_y + 0.086, eye_z + 0.016),
            (0.140, eye_y + 0.064, eye_z - 0.006),
        ]
        lid_left = curved_tube(
            lid_pts,
            [0.0075, 0.0095, 0.0065],
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
        "eye_center": (0.0, eye_y, eye_z + 0.014),
        "eyelid_closed_angle": EYELID_CLOSED_ANGLE,
    }


def _build_antennae(scene: SceneBuilder, head_center: Tuple[float, float, float]) -> Dict[str, Tuple[float, float, float]]:
    """Build smooth continuous flowing insect antennae with gently flared sensory tips."""
    _, hy, hz = head_center
    base_l = (0.088, hy + 0.194, hz + 0.022)
    root_l = (0.082, hy + 0.178, hz + 0.018)
    mid1_l = (0.112, hy + 0.270, hz + 0.032)
    bend_l = (0.132, hy + 0.332, hz + 0.052)
    mid2_l = (0.150, hy + 0.344, hz + 0.098)
    tip_l = (0.160, hy + 0.300, hz + 0.146)

    antenna = swept_tube(
        [root_l, base_l, mid1_l, bend_l, mid2_l, (0.158, hy + 0.318, hz + 0.130), tip_l],
        [
            (0.019, 0.019),
            (0.015, 0.016),
            (0.012, 0.013),
            (0.013, 0.014),
            (0.016, 0.018),
            (0.019, 0.021),
            (0.010, 0.012),
        ],
        radial_segs=10,
        smooth_subdiv=3,
        cap_rings=3,
        name="antenna_stalk",
        material="chitin_dark",
    )
    scene.add_mirrored("antennae", antenna)

    socket_left = (
        ellipsoid(0.022, 0.011, 0.022, u_segs=12, v_segs=8, name="antenna_socket", material="chitin_dark")
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
    top_mask = np.clip(y / 0.10, 0.0, 1.0)
    p[:, 1] += 0.008 * np.exp(-((x / 0.042) ** 2)) * top_mask
    ventral = np.clip((-y - 0.038) / 0.07, 0.0, 1.0)
    p[:, 1] += 0.010 * ventral
    return p


def _build_body(scene: SceneBuilder) -> Dict[str, Tuple[float, float, float]]:
    """Build slim organic ovoid thorax, short pinched petiole, and compact tucked gaster."""
    # Continuous smooth neck + slim tapered teardrop insect thorax (curved organic profile, never boxy).
    thorax_profile = [
        (0.000, 0.240),
        (0.040, 0.250),
        (0.064, 0.278),
        (0.078, 0.316),
        (0.086, 0.362),
        (0.094, 0.412),
        (0.092, 0.450),
        (0.078, 0.482),
        (0.056, 0.506),
        (0.048, 0.536),
        (0.052, 0.584),
        (0.000, 0.598),
    ]
    thorax_core = (
        lathe(thorax_profile, radial_segs=24, cap_bottom=False, cap_top=False, axis="y", name="thorax_core", material="chitin_dark")
        .scale(0.95, 1.0, 0.96)
        .rotate_xyz(rx=0.05)
        .translate(0.0, 0.0, -0.010)
    )
    scene.add("body", thorax_core)

    neck_collar = (
        torus_arc(0.056, 0.009, u_segs=18, v_segs=8, plane="xz", name="neck_collar", material="chitin_dark")
        .rotate_xyz(rx=0.10)
        .translate(0.0, 0.500, 0.008)
    )
    scene.add("body", neck_collar)

    # Smooth curved ovoid team-colored chest carapace sitting cleanly proud of the dark core.
    chest_profile = [
        (0.000, -0.092),
        (0.044, -0.080),
        (0.072, -0.046),
        (0.088, -0.004),
        (0.092, 0.036),
        (0.078, 0.070),
        (0.046, 0.090),
        (0.000, 0.098),
    ]
    chest_plate = (
        lathe(chest_profile, radial_segs=22, cap_bottom=False, cap_top=False, axis="y", name="chest_plate", material="chitin_team")
        .scale(0.94, 1.0, 0.68)
        .rotate_xyz(rx=0.10)
        .translate(0.0, 0.402, 0.052)
    )
    scene.add("body", chest_plate)

    # Smooth rounded dorsal pronotum carapace hugging the upper back cleanly proud of the core.
    dorsal_plate = (
        ellipsoid(0.086, 0.088, 0.062, u_segs=20, v_segs=14, name="dorsal_plate", material="chitin_team")
        .rotate_xyz(rx=-0.12)
        .translate(0.0, 0.422, -0.056)
    )
    scene.add("body", dorsal_plate)

    # Subtle curved organic segment seam dividing upper pronotum and lower thorax.
    waist_seam = (
        torus_arc(0.082, 0.006, u_segs=20, v_segs=8, plane="xz", name="thorax_seam", material="chitin_dark")
        .scale(0.96, 1.0, 1.06)
        .rotate_xyz(rx=0.06)
        .translate(0.0, 0.312, -0.006)
    )
    scene.add("body", waist_seam)

    # Short pinched insect petiole waist stalk + rounded dorsal petiole scale node.
    petiole_stem = swept_tube(
        [(0.0, 0.296, -0.068), (0.0, 0.278, -0.102), (0.0, 0.266, -0.132)],
        [(0.032, 0.034), (0.022, 0.024), (0.030, 0.032)],
        radial_segs=12,
        smooth_subdiv=2,
        cap_rings=2,
        name="petiole_stem",
        material="chitin_dark",
    )
    scene.add("body", petiole_stem)

    petiole_node = (
        ellipsoid(0.032, 0.038, 0.022, u_segs=14, v_segs=10, name="petiole_node", material="chitin_team")
        .rotate_xyz(rx=-0.20)
        .translate(0.0, 0.298, -0.102)
    )
    scene.add("body", petiole_node)

    # Compact tucked insect gaster (~35% shorter than Pass 3, z_min ~ -0.345 matching 901 E sprite).
    gaster_profile = [
        (0.000, 0.000),
        (0.018, 0.012),
        (0.044, 0.038),
        (0.066, 0.064),
        (0.058, 0.070),  # stepped tergite ledge 1
        (0.078, 0.084),
        (0.092, 0.118),
        (0.084, 0.126),  # stepped tergite ledge 2
        (0.096, 0.142),
        (0.092, 0.180),
        (0.072, 0.208),
        (0.038, 0.224),
        (0.000, 0.232),
    ]
    gaster = (
        lathe(gaster_profile, radial_segs=24, cap_bottom=False, cap_top=False, axis="z", name="gaster", material="chitin_team")
        .deform(_gaster_keel_fn)
        .scale(0.92, 0.88, 1.0)
        .rotate_xyz(rx=-0.24)
        .translate(0.0, 0.214, -0.346)
    )
    scene.add("body", gaster)

    for idx, (z_off, maj_r) in enumerate([(-0.212, 0.089), (-0.272, 0.064)]):
        dz = z_off - (-0.346)
        y_ring = 0.214 + dz * math.sin(0.24)
        band = (
            torus_arc(maj_r, 0.0055, u_segs=22, v_segs=8, plane="xy", name=f"gaster_seam_{idx}", material="chitin_dark")
            .scale(0.92, 0.88, 1.0)
            .rotate_xyz(rx=-0.24)
            .translate(0.0, y_ring, z_off)
        )
        scene.add("body", band)

    return {
        "neck_center": (0.0, 0.532, 0.012),
        "thorax_center": (0.0, 0.388, -0.008),
        "back_anchor": (0.0, 0.412, -0.118),
        "gaster_center": (0.0, 0.242, -0.236),
        "gaster_top": (0.0, 0.324, -0.220),
    }


def _resolve_arm_points(pose: ArmPoseLike, side: int) -> List[Tuple[float, float, float]]:
    """Resolve arm control points for `side` (+1 = ant left/+X, -1 = ant right/-X)."""
    shoulder = (side * 0.092, 0.476, 0.014)
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
    """Build one continuous, seamlessly flowing tapered insect forelimb + twin pincer claws."""
    pts = _resolve_arm_points(pose, side)
    n_pts = len(pts)
    sx, sy, sz = pts[0]
    root = np.array([side * 0.072, sy - 0.008, sz - 0.008], dtype=np.float64)

    if n_pts == 4:
        p0, p1, p2, p3 = [np.array(p, dtype=np.float64) for p in pts]
        upper_mid = 0.5 * (p0 + p1) + np.array([0.0, 0.006, 0.0])
        # Single uninterrupted curved_tube from deep thorax socket through elbow to tapered wrist/palm.
        arm_mesh = curved_tube(
            [root, p0, upper_mid, p1, p2, p3],
            [
                (0.030, 0.032),
                (0.026, 0.029),
                (0.025, 0.030),
                (0.021, 0.024),
                (0.018, 0.020),
                (0.019, 0.018),
            ],
            samples_per_seg=4,
            radial_segs=12,
            cap_rings=3,
            name=f"arm_{label}",
            material="chitin_dark",
        )
    else:
        ctrl = [root] + [np.array(p, dtype=np.float64) for p in pts]
        rads = [(float(r) * 0.90, float(r) * 1.08) for r in np.linspace(0.029, 0.018, len(ctrl))]
        arm_mesh = curved_tube(
            ctrl,
            rads,
            samples_per_seg=4,
            radial_segs=12,
            cap_rings=3,
            name=f"arm_{label}",
            material="chitin_dark",
        )
    scene.add("arms", arm_mesh)

    # Smoothly blended insect pincer fingers continuing out of the tapered wrist/hand tip.
    hand = np.array(pts[-1], dtype=np.float64)
    prev = np.array(pts[-2], dtype=np.float64)
    hdir = hand - prev
    hlen = float(np.linalg.norm(hdir))
    hdir = hdir / hlen if hlen > 1e-8 else np.array([0.0, 0.0, 1.0])

    up_ref = np.array([0.0, 1.0, 0.0]) if abs(hdir[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
    side_v = np.cross(hdir, up_ref)
    side_v /= max(1e-8, float(np.linalg.norm(side_v)))
    up_v = np.cross(side_v, hdir)

    for finger_sign, fname in ((-1.0, "inner"), (1.0, "outer")):
        f_root = hand - 0.006 * hdir + finger_sign * 0.008 * side_v
        f_base = hand + 0.010 * hdir + finger_sign * 0.014 * side_v + 0.002 * up_v
        f_mid = hand + 0.032 * hdir + finger_sign * 0.017 * side_v
        f_tip = hand + 0.052 * hdir + finger_sign * 0.004 * side_v - 0.008 * up_v
        claw = curved_tube(
            [f_root, f_base, f_mid, f_tip],
            [(0.012, 0.013), (0.010, 0.012), (0.008, 0.010), (0.004, 0.005)],
            samples_per_seg=3,
            radial_segs=8,
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
    """Create a sleek organically tapered insect tarsus claw-foot resting flat on Y = 0.0."""
    tarsus_prof = [
        (0.000, -0.020),
        (0.015, -0.010),
        (0.020, 0.008),
        (0.017, 0.034),
        (0.010, 0.054),
        (0.000, 0.066),
    ]
    foot = lathe(tarsus_prof, radial_segs=12, cap_bottom=False, cap_top=False, axis="z", name=name, material=material)
    foot = foot.scale(1.04, 0.72, 1.0)

    def ground_sole(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        p[:, 1] = np.maximum(p[:, 1], -0.011)
        return p

    foot = foot.deform(ground_sole)
    bmin, _ = foot.bounds()
    return foot.translate(0.0, -float(bmin[1]), 0.0)


def _add_flowing_insect_leg(
    scene: SceneBuilder,
    label: str,
    hip: Tuple[float, float, float],
    knee: Tuple[float, float, float],
    ankle: Tuple[float, float, float],
    foot_pos: Tuple[float, float, float],
    foot_yaw: float,
) -> None:
    """Build one continuous, seamlessly flowing tapered insect leg (zero stuck-on ball joints)."""
    p_hip = np.array(hip, dtype=np.float64)
    p_knee = np.array(knee, dtype=np.float64)
    p_ankle = np.array(ankle, dtype=np.float64)

    # Start nestled smoothly inside the thorax so the upper thigh flows seamlessly out of the body.
    p_root = np.array([0.056, p_hip[1] + 0.006, p_hip[2] * 0.72], dtype=np.float64)
    femur_mid = 0.5 * (p_hip + p_knee) + np.array([0.0, 0.016, 0.0])
    # Gentle pre/post knee anchors give an anatomical insect arch without bulbous ball hinges.
    pre_knee = 0.24 * femur_mid + 0.76 * p_knee + np.array([0.004, 0.006, 0.0])
    post_knee = 0.80 * p_knee + 0.20 * p_ankle + np.array([0.007, -0.004, 0.0])
    tibia_mid = 0.44 * p_knee + 0.56 * p_ankle + np.array([0.009, 0.0, 0.0])
    heel = np.array([foot_pos[0], 0.012, foot_pos[2]], dtype=np.float64)

    leg_tube = curved_tube(
        [p_root, p_hip, femur_mid, pre_knee, p_knee, post_knee, tibia_mid, p_ankle, heel],
        [
            (0.031, 0.034),
            (0.027, 0.031),
            (0.029, 0.036),
            (0.024, 0.029),
            (0.021, 0.025),
            (0.020, 0.024),
            (0.016, 0.019),
            (0.014, 0.016),
            (0.013, 0.014),
        ],
        samples_per_seg=3,
        radial_segs=12,
        cap_rings=3,
        name=f"leg_{label}",
        material="chitin_dark",
    )
    scene.add_mirrored("legs", leg_tube)

    foot = _tapered_tarsus_foot(f"tarsus_{label}").rotate_xyz(ry=foot_yaw).translate(*foot_pos)
    scene.add_mirrored("legs", foot)


def _build_legs(scene: SceneBuilder) -> Dict[str, Tuple[float, float, float]]:
    # Taller upright stance tucked tighter front-to-back matching original agst301/agst901 sprites.
    mid_foot = (0.156, 0.0, 0.098)
    rear_foot = (0.160, 0.0, -0.122)

    _add_flowing_insect_leg(
        scene,
        "mid",
        hip=(0.078, 0.342, 0.016),
        knee=(0.184, 0.354, 0.064),
        ankle=(0.156, 0.032, 0.094),
        foot_pos=mid_foot,
        foot_yaw=0.24,
    )

    _add_flowing_insect_leg(
        scene,
        "rear",
        hip=(0.076, 0.312, -0.056),
        knee=(0.188, 0.342, -0.106),
        ankle=(0.160, 0.032, -0.118),
        foot_pos=rear_foot,
        foot_yaw=math.pi - 0.26,
    )

    return {
        "foot_mid_left": mid_foot,
        "foot_mid_right": (-mid_foot[0], mid_foot[1], mid_foot[2]),
        "foot_rear_left": rear_foot,
        "foot_rear_right": (-rear_foot[0], rear_foot[1], rear_foot[2]),
    }


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
    head_center = (0.0, 0.778, 0.026)
    head_radii = (0.212, 0.244, 0.180)

    _build_head(scene, head_center, head_tilt_x)
    eye_anchors: Dict[str, Any] = {
        "eye_left": (0.086, head_center[1] - 0.022, head_center[2] + 0.152),
        "eye_right": (-0.086, head_center[1] - 0.022, head_center[2] + 0.152),
        "eye_center": (0.0, head_center[1] - 0.022, head_center[2] + 0.166),
        "eyelid_closed_angle": EYELID_CLOSED_ANGLE,
    }
    if include_eyes:
        eye_anchors = _build_eyes(scene, head_center, head_tilt_x, include_brows=include_brows, blink=blink)

    ant_anchors = {
        "antenna_base_left": (0.088, head_center[1] + 0.194, head_center[2] + 0.022),
        "antenna_base_right": (-0.088, head_center[1] + 0.194, head_center[2] + 0.022),
    }
    if include_antennae:
        ant_anchors = _build_antennae(scene, head_center)

    body_anchors = _build_body(scene)
    sh_l, hand_l, hdir_l = _build_arm(scene, left_arm_pose, side=+1, label="left")
    sh_r, hand_r, hdir_r = _build_arm(scene, right_arm_pose, side=-1, label="right")
    leg_anchors = _build_legs(scene)

    anchors: Dict[str, Any] = {
        "head_center": head_center,
        "head_top": (head_center[0], head_center[1] + head_radii[1], head_center[2]),
        "head_front": (head_center[0], head_center[1], head_center[2] + head_radii[2]),
        "head_back": (head_center[0], head_center[1], head_center[2] - head_radii[2]),
        "head_radius": 0.212,
        "head_radii": head_radii,
        "brow_y": head_center[1] + 0.070,
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
    anchors.update(leg_anchors)

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
