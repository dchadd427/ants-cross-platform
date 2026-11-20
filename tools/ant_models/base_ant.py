"""Stylized tabletop/vinyl base ant shared by Worker, Thief, Bomber, Swimmer, and Fire.

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet flat on Y = 0.0)
  +Z = front (South / canonical camera 301)
  +X = ant's left (viewer's right from front), -X = ant's right (viewer's left)

Returns `(SceneBuilder, anchors_dict)` so specialist class builders attach
helmets, goggles, scuba masks, bandanas, packs, and handheld props with zero
floating or awkward clipping.
"""
from __future__ import annotations

import math
from typing import Any, Dict, List, Optional, Sequence, Tuple, Union

import numpy as np

try:
    from .mesh_core import (
        MeshPart,
        SceneBuilder,
        curved_tube,
        ellipsoid,
        lathe,
        sphere,
        superellipsoid,
        torus_arc,
    )
except ImportError:
    from mesh_core import (  # type: ignore
        MeshPart,
        SceneBuilder,
        curved_tube,
        ellipsoid,
        lathe,
        sphere,
        superellipsoid,
        torus_arc,
    )

ArmPoseLike = Optional[Union[str, Sequence[Sequence[float]]]]

# Canonical left-side (+X) arm control curves: [shoulder, elbow, wrist, hand].
_ARM_PRESETS_LEFT: Dict[str, List[Tuple[float, float, float]]] = {
    "idle": [
        (0.145, 0.432, 0.028),
        (0.262, 0.388, 0.092),
        (0.228, 0.348, 0.205),
        (0.195, 0.340, 0.242),
    ],
    "hold_tool": [
        (0.145, 0.432, 0.028),
        (0.270, 0.400, 0.100),
        (0.266, 0.412, 0.226),
        (0.252, 0.432, 0.278),
    ],
    "hold_front": [
        (0.145, 0.432, 0.028),
        (0.270, 0.400, 0.100),
        (0.266, 0.412, 0.226),
        (0.252, 0.432, 0.278),
    ],
    "hold_high": [
        (0.145, 0.432, 0.028),
        (0.280, 0.445, 0.115),
        (0.288, 0.545, 0.232),
        (0.272, 0.592, 0.286),
    ],
    "akimbo": [
        (0.145, 0.432, 0.028),
        (0.292, 0.395, 0.056),
        (0.258, 0.326, 0.150),
        (0.225, 0.312, 0.180),
    ],
    "wide": [
        (0.145, 0.432, 0.028),
        (0.292, 0.395, 0.056),
        (0.258, 0.326, 0.150),
        (0.225, 0.312, 0.180),
    ],
}


def _head_sculpt_fn(pos: np.ndarray) -> np.ndarray:
    """Organic vinyl sculpting on the base head superellipsoid.

    Creates a full rounded forehead dome, gently fuller lower-front cheeks/muzzle
    so eyes read crisply from an overhead ~50-degree camera, and a tidy chin taper.
    """
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]

    # Gentle cheek fullness on the lower-front half.
    cheek = np.exp(-((y + 0.055) / 0.12) ** 2) * np.clip((z + 0.04) / 0.28, 0.0, 1.0)
    p[:, 0] = x * (1.0 + 0.075 * cheek)
    p[:, 2] = z + 0.026 * cheek

    # Subtle brow-crown roundness at top-front so helmets and bandanas seat cleanly.
    crown = np.clip((y - 0.06) / 0.18, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.04 * (crown ** 2)
    p[:, 2] -= 0.014 * (crown ** 2) * np.clip(z / 0.24, 0.0, 1.0)

    # Tidy chin taper underneath so the head joins the neck cleanly.
    chin = np.clip((-y - 0.09) / 0.14, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.18 * (chin ** 1.6)
    p[:, 2] *= 1.0 - 0.12 * (chin ** 1.6)
    return p


def _build_head(scene: SceneBuilder, head_center: Tuple[float, float, float], head_tilt_x: float) -> None:
    hx, hy, hz = head_center
    cranium = (
        superellipsoid(0.252, 0.228, 0.242, e1=0.74, e2=0.80, u_segs=30, v_segs=22, name="cranium", material="chitin_team")
        .deform(_head_sculpt_fn)
        .rotate_xyz(rx=head_tilt_x)
        .translate(hx, hy, hz)
    )
    scene.add("head", cranium)

    # Cute plump vinyl mandibles tucked beneath cheeks (matching original sprites).
    mandible_left = (
        superellipsoid(0.054, 0.058, 0.068, e1=0.82, e2=0.85, u_segs=16, v_segs=12, name="mandible", material="chitin_team")
        .rotate_xyz(rx=0.22 + head_tilt_x, ry=-0.26, rz=-0.10)
        .translate(0.078, hy - 0.172, hz + 0.198)
    )
    scene.add_mirrored("head", mandible_left)

    # Subtle dark mouth-cleft accent tucked right between mandibles.
    mouth_groove = (
        ellipsoid(0.040, 0.022, 0.032, u_segs=14, v_segs=10, name="mouth_cleft", material="chitin_dark")
        .translate(0.0, hy - 0.168, hz + 0.184)
    )
    scene.add("head", mouth_groove)


def _build_eyes(
    scene: SceneBuilder,
    head_center: Tuple[float, float, float],
    head_tilt_x: float,
    *,
    include_brows: bool = True,
) -> Dict[str, Tuple[float, float, float]]:
    _, hy, hz = head_center
    eye_x = 0.108
    eye_y = hy - 0.018
    eye_z = hz + 0.224

    # Glossy protruding rounded white sclera, tilted slightly upward so eyes stay bright at 50-deg isometric view.
    sclera_left = (
        superellipsoid(0.084, 0.094, 0.068, e1=0.88, e2=0.90, u_segs=20, v_segs=16, name="sclera", material="eye_white")
        .rotate_xyz(rx=-0.24 + head_tilt_x, ry=0.22)
        .translate(eye_x, eye_y, eye_z)
    )
    scene.add_mirrored("eyes", sclera_left)

    # Crisp large glossy dark pupil seated high-center on the sclera for lively overhead readability.
    pupil_left = (
        ellipsoid(0.044, 0.050, 0.030, u_segs=16, v_segs=12, name="pupil", material="eye_pupil")
        .rotate_xyz(rx=-0.22 + head_tilt_x, ry=0.14)
        .translate(eye_x - 0.008, eye_y + 0.006, eye_z + 0.050)
    )
    scene.add_mirrored("eyes", pupil_left)

    # Expressive dark brow arch set cleanly above the eye so it frames without hiding the sclera from above.
    if include_brows:
        brow_pts = [
            (0.042, eye_y + 0.088, eye_z + 0.022),
            (0.106, eye_y + 0.108, eye_z + 0.024),
            (0.174, eye_y + 0.080, eye_z - 0.012),
            (0.198, eye_y + 0.048, eye_z - 0.048),
        ]
        brow_left = curved_tube(
            brow_pts,
            [0.012, 0.017, 0.015, 0.010],
            samples_per_seg=4,
            radial_segs=8,
            cap_rings=2,
            name="brow",
            material="chitin_dark",
        )
        scene.add_mirrored("eyes", brow_left)

    return {
        "eye_left": (eye_x, eye_y, eye_z),
        "eye_right": (-eye_x, eye_y, eye_z),
        "eye_center": (0.0, eye_y, eye_z + 0.016),
    }


def _build_antennae(scene: SceneBuilder, head_center: Tuple[float, float, float]) -> Dict[str, Tuple[float, float, float]]:
    _, hy, hz = head_center
    base_l = (0.118, hy + 0.176, hz + 0.042)
    ant_pts = [
        base_l,
        (0.155, hy + 0.278, hz + 0.068),
        (0.188, hy + 0.362, hz + 0.125),
        (0.178, hy + 0.348, hz + 0.204),
    ]
    ant_rads = [0.022, 0.025, 0.032, 0.037]
    ant_left = curved_tube(
        ant_pts,
        ant_rads,
        samples_per_seg=4,
        radial_segs=12,
        cap_rings=3,
        name="antenna",
        material="chitin_dark",
    )
    scene.add_mirrored("antennae", ant_left)

    # Small sculpted base socket ring where each antenna meets the cranium.
    socket_left = (
        ellipsoid(0.036, 0.024, 0.036, u_segs=12, v_segs=10, name="antenna_socket", material="chitin_dark")
        .rotate_xyz(rx=0.22, rz=-0.32)
        .translate(*base_l)
    )
    scene.add_mirrored("antennae", socket_left)

    return {
        "antenna_base_left": base_l,
        "antenna_base_right": (-base_l[0], base_l[1], base_l[2]),
    }


def _build_body(scene: SceneBuilder) -> Dict[str, Tuple[float, float, float]]:
    # Neck collar linking cranium to thorax cleanly.
    neck = (
        lathe([(0.095, 0.0), (0.084, 0.055), (0.096, 0.115)], radial_segs=18, name="neck", material="chitin_dark")
        .rotate_xyz(rx=0.14)
        .translate(0.0, 0.435, 0.012)
    )
    scene.add("body", neck)

    # Upright sculpted vinyl thorax core.
    thorax_core = (
        superellipsoid(0.140, 0.158, 0.142, e1=0.86, e2=0.90, u_segs=22, v_segs=16, name="thorax_core", material="chitin_dark")
        .rotate_xyz(rx=0.12)
        .translate(0.0, 0.335, -0.012)
    )
    scene.add("body", thorax_core)

    # Glossy team-colored chest plate and dorsal saddle plate (strong readability from all angles).
    chest_plate = (
        superellipsoid(0.122, 0.124, 0.092, e1=0.82, e2=0.86, u_segs=20, v_segs=14, name="chest_plate", material="chitin_team")
        .rotate_xyz(rx=0.16)
        .translate(0.0, 0.340, 0.058)
    )
    scene.add("body", chest_plate)

    dorsal_plate = (
        superellipsoid(0.118, 0.118, 0.095, e1=0.82, e2=0.86, u_segs=20, v_segs=14, name="dorsal_plate", material="chitin_team")
        .rotate_xyz(rx=-0.12)
        .translate(0.0, 0.352, -0.068)
    )
    scene.add("body", dorsal_plate)

    # Cute waist petiole bead connecting thorax to gaster.
    petiole = (
        ellipsoid(0.066, 0.064, 0.068, u_segs=16, v_segs=12, name="petiole", material="chitin_dark")
        .translate(0.0, 0.242, -0.142)
    )
    scene.add("body", petiole)

    # Smooth rounded vinyl teardrop abdomen / gaster extending backward (-Z).
    gaster_profile = [
        (0.000, 0.000),  # softly rounded vinyl tail tip (points toward -Z)
        (0.040, 0.018),
        (0.080, 0.058),
        (0.118, 0.122),
        (0.138, 0.195),
        (0.135, 0.270),
        (0.110, 0.332),
        (0.068, 0.376),
        (0.000, 0.395),  # front pole meeting petiole
    ]
    gaster = (
        lathe(gaster_profile, radial_segs=24, cap_bottom=False, cap_top=False, axis="z", name="gaster", material="chitin_team")
        .scale(1.02, 0.94, 1.0)
        .rotate_xyz(rx=-0.16)
        .translate(0.0, 0.205, -0.530)
    )
    scene.add("body", gaster)

    # Two sleek flush vinyl chitin trim bands around the gaster.
    for idx, (z_off, maj_r) in enumerate([(-0.340, 0.132), (-0.412, 0.109)]):
        dz = z_off - (-0.530)
        y_ring = 0.205 + dz * math.sin(0.16)
        band = (
            torus_arc(maj_r, 0.0075, u_segs=24, v_segs=8, plane="xy", name=f"gaster_band_{idx}", material="chitin_dark")
            .scale(1.02, 0.94, 1.0)
            .rotate_xyz(rx=-0.16)
            .translate(0.0, y_ring, z_off)
        )
        scene.add("body", band)

    return {
        "neck_center": (0.0, 0.485, 0.015),
        "thorax_center": (0.0, 0.335, -0.012),
        "back_anchor": (0.0, 0.365, -0.155),
        "gaster_center": (0.0, 0.242, -0.320),
        "gaster_top": (0.0, 0.368, -0.295),
    }


def _resolve_arm_points(pose: ArmPoseLike, side: int) -> List[Tuple[float, float, float]]:
    """Resolve arm control points for `side` (+1 = ant left/+X, -1 = ant right/-X)."""
    shoulder = (side * 0.145, 0.425, 0.025)
    if pose is None:
        preset = _ARM_PRESETS_LEFT["idle"]
        return [(side * x, y, z) for x, y, z in preset]
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
    """Build one articulated curved vinyl arm + rounded mitten hand; return (shoulder, hand, hand_dir)."""
    pts = _resolve_arm_points(pose, side)
    n_pts = len(pts)
    if n_pts == 4:
        rads = [0.036, 0.031, 0.028, 0.034]
    else:
        rads = list(np.linspace(0.035, 0.031, n_pts))

    arm_mesh = curved_tube(
        pts,
        rads,
        samples_per_seg=4,
        radial_segs=12,
        cap_rings=3,
        name=f"arm_{label}",
        material="chitin_dark",
    )
    scene.add("arms", arm_mesh)

    # Rounded shoulder joint socket.
    sx, sy, sz = pts[0]
    scene.add("arms", sphere(0.044, u_segs=12, v_segs=10, name=f"shoulder_{label}", material="chitin_dark").translate(sx, sy, sz))

    # Subtle elbow bead.
    ex, ey, ez = pts[1]
    scene.add("arms", sphere(0.035, u_segs=12, v_segs=8, name=f"elbow_{label}", material="chitin_dark").translate(ex, ey, ez))

    # Rounded vinyl mitten/pincer hand aligned to forearm direction.
    hand = np.array(pts[-1], dtype=np.float64)
    prev = np.array(pts[-2], dtype=np.float64)
    hdir = hand - prev
    hlen = float(np.linalg.norm(hdir))
    hdir = hdir / hlen if hlen > 1e-8 else np.array([0.0, 0.0, 1.0])

    # Build palm + cute stubby two-lobed pincer tip in world space.
    palm = sphere(0.042, u_segs=14, v_segs=10, name=f"hand_palm_{label}", material="chitin_dark").translate(hand)
    scene.add("arms", palm)

    up_ref = np.array([0.0, 1.0, 0.0]) if abs(hdir[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
    side_v = np.cross(hdir, up_ref)
    side_v /= max(1e-8, float(np.linalg.norm(side_v)))
    up_v = np.cross(side_v, hdir)

    for finger_sign, fname in ((-1.0, "thumb"), (1.0, "finger")):
        f_base = hand + 0.015 * hdir + finger_sign * 0.020 * side_v + 0.008 * up_v
        f_tip = hand + 0.056 * hdir + finger_sign * 0.014 * side_v - 0.006 * up_v
        finger = curved_tube(
            [f_base, 0.5 * (f_base + f_tip) + finger_sign * 0.008 * side_v, f_tip],
            [0.018, 0.017, 0.013],
            samples_per_seg=3,
            radial_segs=8,
            cap_rings=2,
            name=f"hand_{fname}_{label}",
            material="chitin_dark",
        )
        scene.add("arms", finger)

    return (
        (float(sx), float(sy), float(sz)),
        (float(hand[0]), float(hand[1]), float(hand[2])),
        (float(hdir[0]), float(hdir[1]), float(hdir[2])),
    )


def _flat_foot_pad(name: str, material: str = "chitin_dark") -> MeshPart:
    """Create a chunky rounded tabletop foot pad whose flat sole sits exactly at Y = 0.0."""
    def flatten_sole(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        # Flatten bottom so figurine rests flush on Y = 0 with a soft rounded bevel.
        p[:, 1] = np.maximum(p[:, 1], -0.024)
        # Slight widening near the sole for solid vinyl-toy stance.
        sole_t = np.clip((-p[:, 1] + 0.006) / 0.030, 0.0, 1.0)
        p[:, 0] *= 1.0 + 0.14 * sole_t
        p[:, 2] *= 1.0 + 0.12 * sole_t
        return p

    pad = superellipsoid(0.050, 0.036, 0.064, e1=0.72, e2=0.82, u_segs=16, v_segs=12, name=name, material=material)
    pad = pad.deform(flatten_sole)
    bmin, _ = pad.bounds()
    return pad.translate(0.0, -float(bmin[1]), 0.0)


def _build_legs(scene: SceneBuilder) -> None:
    # Pair 1: front/mid walking legs (left side +X, mirrored cleanly to -X).
    mid_pts = [
        (0.118, 0.280, 0.032),
        (0.246, 0.252, 0.095),
        (0.250, 0.128, 0.132),
        (0.236, 0.044, 0.152),
    ]
    mid_leg = curved_tube(
        mid_pts,
        [0.036, 0.032, 0.028, 0.026],
        samples_per_seg=4,
        radial_segs=12,
        cap_rings=3,
        name="leg_mid",
        material="chitin_dark",
    )
    scene.add_mirrored("legs", mid_leg)
    scene.add_mirrored("legs", sphere(0.044, u_segs=12, v_segs=8, name="hip_mid", material="chitin_dark").translate(*mid_pts[0]))
    scene.add_mirrored("legs", sphere(0.036, u_segs=12, v_segs=8, name="knee_mid", material="chitin_dark").translate(*mid_pts[1]))
    mid_foot = _flat_foot_pad("foot_mid").rotate_xyz(ry=0.18).translate(0.236, 0.0, 0.168)
    scene.add_mirrored("legs", mid_foot)

    # Pair 2: rear walking legs (left side +X, mirrored cleanly to -X).
    rear_pts = [
        (0.114, 0.254, -0.072),
        (0.252, 0.242, -0.148),
        (0.248, 0.128, -0.182),
        (0.232, 0.044, -0.198),
    ]
    rear_leg = curved_tube(
        rear_pts,
        [0.037, 0.033, 0.028, 0.026],
        samples_per_seg=4,
        radial_segs=12,
        cap_rings=3,
        name="leg_rear",
        material="chitin_dark",
    )
    scene.add_mirrored("legs", rear_leg)
    scene.add_mirrored("legs", sphere(0.044, u_segs=12, v_segs=8, name="hip_rear", material="chitin_dark").translate(*rear_pts[0]))
    scene.add_mirrored("legs", sphere(0.037, u_segs=12, v_segs=8, name="knee_rear", material="chitin_dark").translate(*rear_pts[1]))
    rear_foot = _flat_foot_pad("foot_rear").rotate_xyz(ry=-0.22).translate(0.232, 0.0, -0.210)
    scene.add_mirrored("legs", rear_foot)


def build_base_ant(
    scene_name: str = "ant_base",
    *,
    include_antennae: bool = True,
    include_eyes: bool = True,
    include_brows: bool = True,
    left_arm_pose: ArmPoseLike = None,
    right_arm_pose: ArmPoseLike = None,
    head_tilt_x: float = 0.0,
    scale: float = 1.0,
) -> Tuple[SceneBuilder, Dict[str, Any]]:
    """Build the stylized vinyl base ant and return `(SceneBuilder, anchors)`.

    Parameters
    ----------
    scene_name:
        Root node name in the exported glTF scene (e.g., "ant_worker").
    include_antennae:
        Set False for helmeted classes (e.g., Fire Ant) whose headwear covers them.
    include_eyes / include_brows:
        Toggle glossy eyes or brow ridges if headwear encloses the eye area.
    left_arm_pose / right_arm_pose:
        Preset name ("idle", "hold_tool", "hold_front", "hold_high", "akimbo",
        "wide") or an explicit list of 3D control points for that side.
        Remember: +X is ant's left (viewer's right), -X is ant's right.
    head_tilt_x:
        Additional pitch offset in radians around +X.
    scale:
        Uniform world-space scale factor (grounded to Y = 0.0 after scaling).
    """
    scene = SceneBuilder(name=scene_name)
    head_center = (0.0, 0.722, 0.056)
    head_radii = (0.252, 0.228, 0.242)

    _build_head(scene, head_center, head_tilt_x)
    eye_anchors: Dict[str, Tuple[float, float, float]] = {
        "eye_left": (0.108, head_center[1] - 0.018, head_center[2] + 0.224),
        "eye_right": (-0.108, head_center[1] - 0.018, head_center[2] + 0.224),
        "eye_center": (0.0, head_center[1] - 0.018, head_center[2] + 0.240),
    }
    if include_eyes:
        eye_anchors = _build_eyes(scene, head_center, head_tilt_x, include_brows=include_brows)

    ant_anchors = {
        "antenna_base_left": (0.118, head_center[1] + 0.176, head_center[2] + 0.042),
        "antenna_base_right": (-0.118, head_center[1] + 0.176, head_center[2] + 0.042),
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
        "head_radius": 0.245,
        "head_radii": head_radii,
        "brow_y": head_center[1] + 0.068,
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
            if key.endswith("_dir"):
                continue
            if isinstance(val, tuple) and len(val) == 3:
                anchors[key] = (val[0] * s, val[1] * s, val[2] * s)
            elif isinstance(val, (int, float)) and key != "ground_y":
                anchors[key] = float(val) * s

    dy = scene.ground_to_y(0.0)
    if abs(dy) > 1e-6:
        for key, val in list(anchors.items()):
            if key.endswith("_dir") or key == "head_radii":
                continue
            if isinstance(val, tuple) and len(val) == 3:
                anchors[key] = (val[0], val[1] + dy, val[2])
            elif key == "brow_y":
                anchors[key] = float(val) + dy
    return scene, anchors
