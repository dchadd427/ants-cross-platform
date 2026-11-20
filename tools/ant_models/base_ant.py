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
        AnimationChannel,
        AnimationClip,
        MeshPart,
        SceneBuilder,
        cone,
        curved_tube,
        ellipsoid,
        hemisphere_shell,
        lathe,
        quat_from_axis_angle,
        quat_from_euler_xyz,
        quat_mul,
        superellipsoid,
        swept_tube,
        torus_arc,
    )
except ImportError:
    from mesh_core import (  # type: ignore
        AnimationChannel,
        AnimationClip,
        MeshPart,
        SceneBuilder,
        cone,
        curved_tube,
        ellipsoid,
        hemisphere_shell,
        lathe,
        quat_from_axis_angle,
        quat_from_euler_xyz,
        quat_mul,
        superellipsoid,
        swept_tube,
        torus_arc,
    )

ArmPoseLike = Optional[Union[str, Sequence[Sequence[float]]]]

EYELID_CLOSED_ANGLE = 1.80  # radians around local +X (~103 deg) from wide-open +53 deg to sealed -50 deg
BROW_STYLES: Tuple[str, ...] = ("curious", "sneaky", "eager", "wonder", "plucky", "none")

# Per-class expressive brow curve profiles:
# maps style -> ((left inner/mid/outer dy, left z_push), (right inner/mid/outer dy, right z_push))
# All points stay cleanly <= eye_y + 0.077 * s so helmets, masks, and goggles never clip.
_BROW_PRESETS: Dict[str, Tuple[Tuple[float, float, float, float], Tuple[float, float, float, float]]] = {
    "curious": ((0.062, 0.070, 0.056, 0.038), (0.066, 0.076, 0.062, 0.040)),
    "sneaky": ((0.046, 0.056, 0.065, 0.045), (0.050, 0.061, 0.068, 0.045)),
    "eager": ((0.054, 0.065, 0.056, 0.041), (0.066, 0.076, 0.061, 0.041)),
    "wonder": ((0.067, 0.076, 0.062, 0.037), (0.067, 0.076, 0.062, 0.037)),
    "plucky": ((0.061, 0.071, 0.058, 0.039), (0.064, 0.074, 0.060, 0.039)),
}

# Canonical left-side (+X) arm control curves: [shoulder, elbow, wrist, hand].
_ARM_PRESETS_LEFT: Dict[str, List[Tuple[float, float, float]]] = {
    "idle": [
        (0.106, 0.478, 0.016),
        (0.194, 0.436, 0.064),
        (0.172, 0.388, 0.154),
        (0.142, 0.382, 0.186),
    ],
    "hold_tool": [
        (0.106, 0.478, 0.016),
        (0.210, 0.444, 0.078),
        (0.214, 0.450, 0.184),
        (0.198, 0.470, 0.232),
    ],
    "hold_front": [
        (0.106, 0.478, 0.016),
        (0.210, 0.444, 0.078),
        (0.214, 0.450, 0.184),
        (0.198, 0.470, 0.232),
    ],
    "hold_high": [
        (0.106, 0.478, 0.016),
        (0.214, 0.486, 0.078),
        (0.228, 0.566, 0.172),
        (0.214, 0.622, 0.226),
    ],
    "akimbo": [
        (0.106, 0.478, 0.016),
        (0.216, 0.438, 0.046),
        (0.196, 0.376, 0.126),
        (0.168, 0.366, 0.154),
    ],
    "wide": [
        (0.106, 0.478, 0.016),
        (0.216, 0.438, 0.046),
        (0.196, 0.376, 0.126),
        (0.168, 0.366, 0.154),
    ],
}


def _head_sculpt_fn(pos: np.ndarray) -> np.ndarray:
    """Sculpt a compact, expressive softly-rounded rectangular ant cranium (no pointy alien chin).

    Matches original 1998 Worker sprites (`23x40` / `23x42`) with full plump cheeks,
    a crisp insect forehead Y-suture ridge/plate seam, sculpted orbital eye cups + brow arches,
    and gentle lower-jaw rounding.
    """
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    ax = np.abs(x)
    front_mask = np.clip((z - 0.01) / 0.14, 0.0, 1.0)

    # 1. Crisp vertical center forehead seam branching softly into a Y-suture toward each antenna.
    seam_stem = (
        np.exp(-((ax / 0.014) ** 1.6))
        * np.exp(-((y - 0.058) / 0.056) ** 2)
        * np.clip((z + 0.01) / 0.14, 0.0, 1.0)
    )
    branch_x = 0.014 + np.clip(y - 0.066, 0.0, 0.12) * 0.92
    seam_branch = (
        np.exp(-(((ax - branch_x) / 0.014) ** 2))
        * np.clip((y - 0.060) / 0.025, 0.0, 1.0)
        * np.clip((0.165 - y) / 0.045, 0.0, 1.0)
        * np.clip((z + 0.04) / 0.14, 0.0, 1.0)
    )
    suture = np.maximum(seam_stem, 0.72 * seam_branch)
    p[:, 2] -= 0.010 * suture
    p[:, 1] -= 0.005 * suture

    # 2. Deeper sculpted orbital cups + soft fleshy brow arch right over each eye.
    socket = (
        np.exp(-(((ax - 0.080) / 0.060) ** 2 + ((y + 0.002) / 0.060) ** 2))
        * np.clip((z - 0.04) / 0.11, 0.0, 1.0)
    )
    p[:, 2] -= 0.013 * socket
    brow_crest = (
        np.exp(-(((ax - 0.078) / 0.052) ** 2 + ((y - 0.064) / 0.026) ** 2))
        * front_mask
    )
    p[:, 2] += 0.007 * brow_crest
    p[:, 1] += 0.003 * brow_crest

    # 3. Soft plump lower-cheek swell + gentle chin rounding (zero alien chin pinch!).
    cheek = (
        np.exp(-(((ax - 0.112) / 0.056) ** 2 + ((y + 0.066) / 0.046) ** 2))
        * np.clip((z + 0.01) / 0.14, 0.0, 1.0)
    )
    p[:, 0] += np.sign(x) * 0.007 * cheek
    p[:, 2] += 0.007 * cheek

    lower = np.clip((-y - 0.055) / 0.145, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.11 * (lower ** 1.75)
    p[:, 2] *= 1.0 - 0.08 * (lower ** 1.60)

    clypeus = np.exp(-((x / 0.044) ** 2)) * np.exp(-((y + 0.094) / 0.044) ** 2) * front_mask
    p[:, 2] += 0.011 * clypeus

    # 4. Subtle occipital rounding into the neck at the lower rear of the skull.
    rear_low = np.clip((-z - 0.03) / 0.14, 0.0, 1.0) * np.clip((-y + 0.02) / 0.20, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.10 * rear_low
    p[:, 2] += 0.010 * rear_low
    return p


def _paint_cranium_colors(cranium: MeshPart) -> MeshPart:
    """Paint subtle anatomical face shading into cranium vertex colors (works on all teams).

    Deepens orbital eye sockets, forehead Y-suture, and lower mouth recess while keeping
    cheek apples, brow arches, and crown bright so faces read warm and sculpted.
    """
    p = cranium.positions.astype(np.float64)
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    ax = np.abs(x)
    front_mask = np.clip((z - 0.01) / 0.14, 0.0, 1.0)

    socket_ring = (
        np.exp(-(((ax - 0.080) / 0.068) ** 2 + ((y + 0.002) / 0.068) ** 2))
        * np.clip((z - 0.03) / 0.12, 0.0, 1.0)
    )
    seam_stem = np.exp(-((ax / 0.017) ** 1.5)) * np.exp(-((y - 0.058) / 0.058) ** 2) * front_mask
    branch_x = 0.014 + np.clip(y - 0.066, 0.0, 0.12) * 0.92
    seam_branch = (
        np.exp(-(((ax - branch_x) / 0.017) ** 2))
        * np.clip((y - 0.060) / 0.025, 0.0, 1.0)
        * np.clip((0.165 - y) / 0.045, 0.0, 1.0)
        * np.clip((z + 0.03) / 0.14, 0.0, 1.0)
    )
    mouth_shadow = np.exp(-((ax / 0.068) ** 2 + ((y + 0.132) / 0.044) ** 2)) * front_mask
    cheek_hi = np.exp(-(((ax - 0.108) / 0.062) ** 2 + ((y + 0.064) / 0.050) ** 2)) * front_mask
    brow_hi = np.exp(-(((ax - 0.078) / 0.054) ** 2 + ((y - 0.074) / 0.030) ** 2)) * front_mask

    lum = (
        0.89
        + 0.09 * cheek_hi
        + 0.08 * brow_hi
        - 0.24 * socket_ring
        - 0.15 * np.maximum(seam_stem, 0.72 * seam_branch)
        - 0.14 * mouth_shadow
    )
    lum = np.clip(lum, 0.62, 1.0).astype(np.float32)
    cranium.colors = np.column_stack([lum, lum, lum]).astype(np.float32)
    return cranium


def _build_mandibles(scene: SceneBuilder, head_center: Tuple[float, float, float], head_tilt_x: float) -> None:
    """Build compact, plump rounded lobster-claw mandibles with crisp pale notched inner pincers."""
    _, hy, hz = head_center

    # Chunky rounded outer lobster-claw mitten proportional to the compact soft-cube head.
    claw_pts = [
        (0.076, hy - 0.096, hz + 0.098),
        (0.084, hy - 0.116, hz + 0.144),
        (0.060, hy - 0.126, hz + 0.172),
        (0.028, hy - 0.126, hz + 0.176),
    ]
    claw_rads = [
        (0.024, 0.026),
        (0.034, 0.032),
        (0.030, 0.029),
        (0.018, 0.019),
    ]
    claw_main = curved_tube(
        claw_pts,
        claw_rads,
        samples_per_seg=4,
        radial_segs=14,
        cap_rings=3,
        name="mandible_claw",
        material="chitin_team",
    )

    thumb_pts = [
        (0.062, hy - 0.110, hz + 0.148),
        (0.044, hy - 0.112, hz + 0.170),
        (0.023, hy - 0.116, hz + 0.177),
    ]
    claw_thumb = curved_tube(
        thumb_pts,
        [(0.019, 0.019), (0.017, 0.016), (0.010, 0.011)],
        samples_per_seg=3,
        radial_segs=10,
        cap_rings=2,
        name="mandible_thumb",
        material="chitin_team",
    )

    teeth: List[MeshPart] = []
    for t_idx, (tx, ty, tz, rx_t, ry_t, rz_t) in enumerate(
        (
            (0.019, hy - 0.115, hz + 0.182, 0.009, 0.007, 0.008),
            (0.021, hy - 0.127, hz + 0.181, 0.010, 0.0075, 0.0085),
            (0.031, hy - 0.135, hz + 0.173, 0.008, 0.007, 0.0075),
        )
    ):
        tooth = (
            ellipsoid(rx_t, ry_t, rz_t, u_segs=10, v_segs=8, name=f"mandible_tooth_{t_idx}", material="fuse_white")
            .rotate_xyz(ry=-0.25, rz=0.20)
            .translate(tx, ty, tz)
        )
        teeth.append(tooth)

    parts_left = [claw_main, claw_thumb] + teeth
    if abs(head_tilt_x) > 1e-6:
        parts_left = [p.translate(0.0, -hy, -hz).rotate_xyz(rx=head_tilt_x).translate(0.0, hy, hz) for p in parts_left]
    jaw_hinge = (0.0, hy - 0.112, hz + 0.106)
    for p in parts_left:
        scene.add_mirrored("mandibles", p.translate(-jaw_hinge[0], -jaw_hinge[1], -jaw_hinge[2]))
    scene.set_node_transform("mandibles", translation=jaw_hinge)

    mouth_cleft = (
        ellipsoid(0.036, 0.020, 0.032, u_segs=14, v_segs=8, name="mouth_cleft", material="chitin_dark")
        .translate(0.0, hy - 0.124, hz + 0.126)
    )
    scene.add("head", mouth_cleft)


def _build_head(scene: SceneBuilder, head_center: Tuple[float, float, float], head_tilt_x: float) -> None:
    hx, hy, hz = head_center
    cranium = (
        superellipsoid(0.192, 0.178, 0.170, e1=0.72, e2=0.76, u_segs=38, v_segs=30, name="cranium", material="chitin_team")
        .deform(_head_sculpt_fn)
    )
    cranium = _paint_cranium_colors(cranium).rotate_xyz(rx=head_tilt_x).translate(hx, hy, hz)
    scene.add("head", cranium)
    _build_mandibles(scene, head_center, head_tilt_x)


def _build_eyelid_shell(name: str, yaw_y: float, *, scale: float = 1.0) -> MeshPart:
    """Build a true spherical chitin_team upper eyelid shell in eye-local coordinates.

    At scale=1.0, outer radius is 0.0812 with thickness 0.0036 -> constant inner
    spherical radius 0.0776 everywhere around the eye pivot, strictly clearing the
    <=0.0696 max radial extent of sclera + limbal + iris + pupil + catchlights
    by >= +0.0072 across blink=0..1 and any eye_look gaze offset.
    """
    s = float(scale)
    lid = hemisphere_shell(
        0.0812 * s,
        0.0812 * s,
        0.0812 * s,
        thickness=0.0036 * s,
        start_phi=-0.16,
        u_segs=28,
        v_segs=12,
        name=name,
        material="chitin_team",
    )
    return lid.rotate_xyz(rx=-1.08, ry=yaw_y)


def _build_eyes(
    scene: SceneBuilder,
    head_center: Tuple[float, float, float],
    head_tilt_x: float,
    *,
    include_brows: bool = False,
    brow_style: str = "curious",
    blink: float = 0.0,
    eye_scale_left: float = 1.0,
    eye_scale_right: float = 1.0,
    eye_look: Tuple[float, float] = (0.0, 0.0),
) -> Dict[str, Any]:
    """Build sparkling 5-part eyes (warm ivory sclera, limbal ring, olive iris, pupil, glints) + expressive brows."""
    style = str(brow_style).strip().lower()
    if style not in BROW_STYLES:
        raise ValueError(f"Unknown brow_style {brow_style!r}; expected one of: {', '.join(BROW_STYLES)}")

    _, hy, hz = head_center
    eye_y = hy - 0.002
    eye_z = hz + 0.134
    eye_rx = -0.06 + head_tilt_x
    eye_ry = 0.15

    look_yaw = max(-0.26, min(0.26, float(eye_look[0])))
    look_pitch = max(-0.22, min(0.22, float(eye_look[1])))

    s_l = max(0.70, min(1.35, float(eye_scale_left)))
    s_r = max(0.70, min(1.35, float(eye_scale_right)))
    eye_x_l = 0.0800 + 0.072 * (s_l - 1.0)
    eye_x_r = -(0.0800 + 0.072 * (s_r - 1.0))

    blink_clamped = max(0.0, min(1.0, float(blink)))
    rest_rot = quat_from_axis_angle((1.0, 0.0, 0.0), blink_clamped * EYELID_CLOSED_ANGLE)

    for side, tag, ex, s, b_idx in ((1.0, "left", eye_x_l, s_l, 0), (-1.0, "right", eye_x_r, s_r, 1)):
        yaw = side * eye_ry
        # Deep sculpted dark orbital socket bezel nesting the eye into the face.
        socket_rim = (
            torus_arc(0.0685 * s, 0.0092 * s, u_segs=26, v_segs=10, plane="xy", name=f"eye_socket_rim_{tag}", material="chitin_dark")
            .scale(0.97, 1.03, 0.88)
            .translate(0.0, 0.0, 0.006 * s)
            .rotate_xyz(rx=eye_rx, ry=yaw)
            .translate(ex, eye_y, eye_z)
        )
        sclera = (
            ellipsoid(0.064 * s, 0.067 * s, 0.061 * s, u_segs=24, v_segs=16, name=f"sclera_{tag}", material="eye_white")
            .rotate_xyz(rx=eye_rx, ry=yaw)
            .translate(ex, eye_y, eye_z)
        )
        limbal = (
            ellipsoid(0.0495 * s, 0.0525 * s, 0.0275 * s, u_segs=24, v_segs=14, name=f"limbal_{tag}", material="eye_limbal")
            .translate(-side * 0.0020 * s, 0.0012 * s, 0.0365 * s)
            .rotate_xyz(rx=-look_pitch, ry=look_yaw)
            .rotate_xyz(rx=eye_rx, ry=yaw)
            .translate(ex, eye_y, eye_z)
        )
        iris = (
            ellipsoid(0.0435 * s, 0.0465 * s, 0.0285 * s, u_segs=22, v_segs=14, name=f"iris_{tag}", material="eye_iris")
            .translate(-side * 0.0024 * s, 0.0014 * s, 0.0385 * s)
            .rotate_xyz(rx=-look_pitch, ry=look_yaw)
            .rotate_xyz(rx=eye_rx, ry=yaw)
            .translate(ex, eye_y, eye_z)
        )
        pupil = (
            ellipsoid(0.0285 * s, 0.0305 * s, 0.0165 * s, u_segs=18, v_segs=12, name=f"pupil_{tag}", material="eye_pupil")
            .translate(-side * 0.0028 * s, 0.0015 * s, 0.0522 * s)
            .rotate_xyz(rx=-look_pitch, ry=look_yaw)
            .rotate_xyz(rx=eye_rx, ry=yaw)
            .translate(ex, eye_y, eye_z)
        )
        # Dual wet-eye specular catchlight glints (viewer's upper-left = -X, +Y; bounce = +X, -Y)
        # seated cleanly proud of the iris/pupil while strictly inside r <= 0.0695 * s.
        glint_main = (
            ellipsoid(0.0088 * s, 0.0102 * s, 0.0026 * s, u_segs=14, v_segs=10, name=f"catchlight_main_{tag}", material="eye_catchlight")
            .rotate_xyz(rx=-0.24, ry=-0.22)
            .translate(-0.0145 * s, 0.0155 * s, 0.0638 * s)
            .rotate_xyz(rx=-0.55 * look_pitch, ry=0.55 * look_yaw)
            .rotate_xyz(rx=eye_rx, ry=yaw)
            .translate(ex, eye_y, eye_z)
        )
        glint_bounce = (
            ellipsoid(0.0040 * s, 0.0040 * s, 0.0022 * s, u_segs=10, v_segs=8, name=f"catchlight_bounce_{tag}", material="eye_catchlight")
            .rotate_xyz(rx=0.22, ry=0.22)
            .translate(0.0150 * s, -0.0150 * s, 0.0636 * s)
            .rotate_xyz(rx=-0.55 * look_pitch, ry=0.55 * look_yaw)
            .rotate_xyz(rx=eye_rx, ry=yaw)
            .translate(ex, eye_y, eye_z)
        )
        scene.add("eyes", [socket_rim, sclera, limbal, iris, pupil, glint_main, glint_bounce])

        lid_node = f"eyelid_{tag}"
        scene.add(lid_node, _build_eyelid_shell(f"{lid_node}_shell", yaw_y=yaw, scale=s))
        scene.set_node_transform(lid_node, translation=(ex, eye_y, eye_z), rotation=rest_rot)

        if style != "none":
            dy_in, dy_mid, dy_out, z_push = _BROW_PRESETS[style][b_idx]
            # Anchor inner and outer ends cleanly inside the cranium so the brow blends
            # organically into the orbital ridge rather than floating above the socket.
            brow_pts = [
                (ex - side * 0.050 * s, eye_y + (dy_in - 0.002) * s, eye_z + (z_push - 0.022) * s),
                (ex - side * 0.015 * s, eye_y + (0.42 * dy_in + 0.58 * dy_mid) * s, eye_z + (z_push - 0.005) * s),
                (ex + side * 0.016 * s, eye_y + dy_mid * s, eye_z + (z_push - 0.008) * s),
                (ex + side * 0.050 * s, eye_y + (dy_out - 0.004) * s, eye_z + (z_push - 0.032) * s),
            ]
            r_scale = 1.15 if style == "sneaky" else (0.84 if style == "wonder" else 0.96)
            brow_cap = curved_tube(
                brow_pts,
                [
                    (0.0064 * s * r_scale, 0.0052 * s * r_scale),
                    (0.0098 * s * r_scale, 0.0076 * s * r_scale),
                    (0.0094 * s * r_scale, 0.0074 * s * r_scale),
                    (0.0058 * s * r_scale, 0.0048 * s * r_scale),
                ],
                samples_per_seg=3,
                radial_segs=10,
                cap_rings=2,
                name=f"brow_cap_{tag}",
                material="chitin_team",
            )
            scene.add("eyes", brow_cap)

        if include_brows:
            lid_pts = [
                (ex - side * 0.050 * s, eye_y + 0.068 * s, eye_z + 0.018 * s),
                (ex, eye_y + 0.077 * s, eye_z + 0.020 * s),
                (ex + side * 0.048 * s, eye_y + 0.060 * s, eye_z - 0.002 * s),
            ]
            scene.add(
                "eyes",
                curved_tube(
                    lid_pts,
                    [0.0060 * s, 0.0078 * s, 0.0052 * s],
                    samples_per_seg=3,
                    radial_segs=8,
                    cap_rings=2,
                    name=f"eyelid_ridge_{tag}",
                    material="chitin_dark",
                ),
            )

    scene.add_blink_animation(("eyelid_left", "eyelid_right"), closed_angle=EYELID_CLOSED_ANGLE, name="Blink")

    return {
        "eye_left": (eye_x_l, eye_y, eye_z),
        "eye_right": (eye_x_r, eye_y, eye_z),
        "eye_center": (0.0, eye_y, eye_z + 0.014),
        "eye_radius": 0.067 * max(s_l, s_r),
        "eyelid_outer_radius": 0.0812 * max(s_l, s_r),
        "eyelid_closed_angle": EYELID_CLOSED_ANGLE,
    }


def _build_antennae(
    scene: SceneBuilder,
    head_center: Tuple[float, float, float],
    *,
    antenna_asymmetry: float = 0.15,
) -> Dict[str, Tuple[float, float, float]]:
    """Build smooth continuous flowing insect antennae with organic left/right character asymmetry."""
    _, hy, hz = head_center
    a = max(-1.0, min(1.0, float(antenna_asymmetry)))
    base_l = (0.088, hy + 0.148, hz + 0.022)
    base_r = (-base_l[0], base_l[1], base_l[2])
    rads = [
        (0.019, 0.019),
        (0.015, 0.016),
        (0.012, 0.013),
        (0.013, 0.014),
        (0.016, 0.018),
        (0.020, 0.022),
        (0.010, 0.012),
    ]

    def _make_antenna(side: float, tag: str, base_p: Tuple[float, float, float]) -> List[MeshPart]:
        # Subtle expressive asymmetry: left (+1) perks gently higher & curls forward;
        # right (-1) arches slightly lower & sweeps playfully outward/back.
        lift = 0.024 * a if side > 0 else -0.018 * a
        spread = 0.018 * a if side > 0 else 0.014 * a
        curl_z = 0.016 * a if side > 0 else -0.024 * a
        pts = [
            (side * 0.082, hy + 0.132, hz + 0.018),
            base_p,
            (side * (0.114 + 0.45 * spread), hy + 0.224 + 0.55 * lift, hz + 0.034 + 0.35 * curl_z),
            (side * (0.136 + 0.85 * spread), hy + 0.280 + 0.90 * lift, hz + 0.056 + 0.65 * curl_z),
            (side * (0.154 + spread), hy + 0.288 + lift, hz + 0.098 + 0.90 * curl_z),
            (side * (0.156 + 1.05 * spread), hy + 0.272 + 0.95 * lift, hz + 0.128 + curl_z),
            (side * (0.162 + 1.15 * spread), hy + 0.246 + 0.85 * lift, hz + 0.142 + 1.15 * curl_z),
        ]
        stalk = swept_tube(
            pts,
            rads,
            radial_segs=10,
            smooth_subdiv=3,
            cap_rings=3,
            name=f"antenna_stalk_{tag}",
            material="chitin_dark",
        )
        sock = (
            ellipsoid(0.022, 0.011, 0.022, u_segs=12, v_segs=8, name=f"antenna_socket_{tag}", material="chitin_dark")
            .rotate_xyz(rx=0.22, rz=-side * 0.30)
            .translate(*base_p)
        )
        return [
            stalk.translate(-base_p[0], -base_p[1], -base_p[2]),
            sock.translate(-base_p[0], -base_p[1], -base_p[2]),
        ]

    scene.add("antenna_left", _make_antenna(+1.0, "left", base_l))
    scene.set_node_transform("antenna_left", translation=base_l)
    scene.add("antenna_right", _make_antenna(-1.0, "right", base_r))
    scene.set_node_transform("antenna_right", translation=base_r)

    return {
        "antenna_base_left": base_l,
        "antenna_base_right": base_r,
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


def _build_body(scene: SceneBuilder, *, y_off: float = 0.0) -> Dict[str, Tuple[float, float, float]]:
    """Build fuller grounded organic thorax, pinched petiole, and compact tucked gaster."""
    # Fuller rounded teardrop insect thorax balancing the compact head (matches Orig 23x40/23x42).
    thorax_profile = [
        (0.000, 0.244 + y_off),
        (0.048, 0.256 + y_off),
        (0.078, 0.286 + y_off),
        (0.096, 0.326 + y_off),
        (0.108, 0.372 + y_off),
        (0.114, 0.420 + y_off),
        (0.110, 0.460 + y_off),
        (0.090, 0.490 + y_off),
        (0.066, 0.514 + y_off),
        (0.062, 0.556 + y_off),
        (0.000, 0.568 + y_off),
    ]
    thorax_core = (
        lathe(thorax_profile, radial_segs=24, cap_bottom=False, cap_top=False, axis="y", name="thorax_core", material="chitin_dark")
        .scale(0.98, 1.0, 0.96)
        .rotate_xyz(rx=0.04)
        .translate(0.0, 0.0, -0.010)
    )
    scene.add("body", thorax_core)

    neck_collar = (
        torus_arc(0.066, 0.010, u_segs=18, v_segs=8, plane="xz", name="neck_collar", material="chitin_dark")
        .rotate_xyz(rx=0.08)
        .translate(0.0, 0.504 + y_off, 0.008)
    )
    scene.add("body", neck_collar)

    # Smooth dark neck ball joint centered at the throat pivot so pitching the head
    # chin-up (-10 to -15 deg) never reveals a gap under the jaw or occiput.
    neck_joint = (
        ellipsoid(0.064, 0.064, 0.066, u_segs=18, v_segs=12, name="neck_joint", material="chitin_dark")
        .translate(0.0, 0.526 + y_off, 0.016)
    )
    scene.add("body", neck_joint)

    # Fuller smooth ovoid team-colored chest carapace sitting cleanly proud of the dark core.
    chest_profile = [
        (0.000, -0.100),
        (0.052, -0.088),
        (0.084, -0.050),
        (0.104, -0.004),
        (0.108, 0.038),
        (0.092, 0.074),
        (0.054, 0.096),
        (0.000, 0.104),
    ]
    chest_plate = (
        lathe(chest_profile, radial_segs=22, cap_bottom=False, cap_top=False, axis="y", name="chest_plate", material="chitin_team")
        .scale(0.96, 1.0, 0.68)
        .rotate_xyz(rx=0.09)
        .translate(0.0, 0.408 + y_off, 0.056)
    )
    scene.add("body", chest_plate)

    # Fuller smooth rounded dorsal pronotum carapace hugging the upper back.
    dorsal_plate = (
        ellipsoid(0.102, 0.096, 0.068, u_segs=20, v_segs=14, name="dorsal_plate", material="chitin_team")
        .rotate_xyz(rx=-0.10)
        .translate(0.0, 0.428 + y_off, -0.060)
    )
    scene.add("body", dorsal_plate)

    waist_seam = (
        torus_arc(0.098, 0.0065, u_segs=20, v_segs=8, plane="xz", name="thorax_seam", material="chitin_dark")
        .scale(0.98, 1.0, 1.04)
        .rotate_xyz(rx=0.05)
        .translate(0.0, 0.314 + y_off, -0.006)
    )
    scene.add("body", waist_seam)

    c = max(0.0, -y_off)
    petiole_stem = swept_tube(
        [
            (0.0, 0.298 + y_off, -0.074),
            (0.0, 0.280 + y_off + 0.10 * c, -0.106),
            (0.0, 0.268 + y_off + 0.18 * c, -0.136),
        ],
        [(0.036, 0.038), (0.026, 0.028), (0.034, 0.036)],
        radial_segs=12,
        smooth_subdiv=2,
        cap_rings=2,
        name="petiole_stem",
        material="chitin_dark",
    )
    scene.add("body", petiole_stem)

    petiole_node = (
        ellipsoid(0.036, 0.042, 0.024, u_segs=14, v_segs=10, name="petiole_node", material="chitin_team")
        .rotate_xyz(rx=-0.20)
        .translate(0.0, 0.300 + y_off + 0.10 * c, -0.106)
    )
    scene.add("body", petiole_node)

    # Compact tucked insect gaster matching original 901 E / 701 N proportion.
    # When crouching (c > 0), gently perk the gaster upward around its petiole junction
    # so the abdomen never touches Y = 0.0 before the feet.
    g_pitch = -0.24 + 2.10 * c
    g_front_y = 0.218 + y_off + 0.232 * math.sin(0.24) + 0.18 * c
    g_front_z = -0.352 + 0.232 * math.cos(0.24)
    g_origin_y = g_front_y + 0.232 * math.sin(g_pitch)
    g_origin_z = g_front_z - 0.232 * math.cos(g_pitch)

    gaster_profile = [
        (0.000, 0.000),
        (0.019, 0.012),
        (0.046, 0.038),
        (0.070, 0.064),
        (0.062, 0.070),
        (0.082, 0.084),
        (0.098, 0.118),
        (0.089, 0.126),
        (0.102, 0.142),
        (0.098, 0.180),
        (0.076, 0.208),
        (0.040, 0.224),
        (0.000, 0.232),
    ]
    gaster = (
        lathe(gaster_profile, radial_segs=24, cap_bottom=False, cap_top=False, axis="z", name="gaster", material="chitin_team")
        .deform(_gaster_keel_fn)
        .scale(0.94, 0.90, 1.0)
        .rotate_xyz(rx=g_pitch)
        .translate(0.0, g_origin_y, g_origin_z)
    )
    scene.add("gaster", gaster)

    for idx, (s_local, maj_r) in enumerate([(0.138, 0.095), (0.076, 0.068)]):
        y_ring = g_origin_y - s_local * math.sin(g_pitch)
        z_ring = g_origin_z + s_local * math.cos(g_pitch)
        band = (
            torus_arc(maj_r, 0.0058, u_segs=22, v_segs=8, plane="xy", name=f"gaster_seam_{idx}", material="chitin_dark")
            .scale(0.94, 0.90, 1.0)
            .rotate_xyz(rx=g_pitch)
            .translate(0.0, y_ring, z_ring)
        )
        scene.add("gaster", band)

    g_mid_y = g_origin_y - 0.116 * math.sin(g_pitch)
    g_mid_z = g_origin_z + 0.116 * math.cos(g_pitch)
    return {
        "neck_center": (0.0, 0.520 + y_off, 0.012),
        "thorax_center": (0.0, 0.394 + y_off, -0.008),
        "back_anchor": (0.0, 0.418 + y_off, -0.124),
        "gaster_pivot": (0.0, 0.280 + y_off + 0.14 * c, -0.106),
        "gaster_center": (0.0, g_mid_y, g_mid_z),
        "gaster_top": (0.0, g_mid_y + 0.086, g_mid_z + 0.016),
    }


def _resolve_arm_points(
    pose: ArmPoseLike,
    side: int,
    *,
    y_off: float = 0.0,
    stance_asymmetry: float = 0.0,
) -> List[Tuple[float, float, float]]:
    """Resolve arm control points for `side` (+1 = ant left/+X, -1 = ant right/-X)."""
    sa = max(-1.0, min(1.0, float(stance_asymmetry)))
    shoulder = (side * 0.106, 0.478 + y_off, 0.016)
    if pose is None or isinstance(pose, str):
        key = "idle" if pose is None else pose.strip().lower()
        if key not in _ARM_PRESETS_LEFT:
            known = ", ".join(sorted(_ARM_PRESETS_LEFT))
            raise ValueError(f"Unknown arm pose {pose!r}; expected one of: {known}")
        base_pts = _ARM_PRESETS_LEFT[key]
        out: List[Tuple[float, float, float]] = []
        # Natural contrapposto hand stagger on preset poses when stance_asymmetry != 0.
        dy_hand = (0.020 if side > 0 else -0.014) * sa
        dz_hand = (0.016 if side > 0 else -0.012) * sa
        for idx, (x, y, z) in enumerate(base_pts):
            w = idx / max(1, len(base_pts) - 1)
            out.append((side * x, y + y_off + dy_hand * w, z + dz_hand * w))
        return out
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
    *,
    y_off: float = 0.0,
    stance_asymmetry: float = 0.0,
) -> Tuple[Tuple[float, float, float], Tuple[float, float, float], Tuple[float, float, float]]:
    """Build one continuous, seamlessly flowing tapered insect forelimb + twin pincer claws."""
    pts = _resolve_arm_points(pose, side, y_off=y_off, stance_asymmetry=stance_asymmetry)
    n_pts = len(pts)
    sx, sy, sz = pts[0]
    root = np.array([side * 0.082, sy - 0.008, sz - 0.008], dtype=np.float64)

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


def _add_flowing_insect_leg(
    scene: SceneBuilder,
    label: str,
    hip: Tuple[float, float, float],
    knee: Tuple[float, float, float],
    ankle: Tuple[float, float, float],
    foot_pos: Tuple[float, float, float],
    foot_yaw: float,
    *,
    side: int = 0,
) -> None:
    """Build one uninterrupted flowing insect leg from hip through a curved tapered tarsus claw tip.

    Pass `side=0` to mirror symmetrically across X, or `side=+1/-1` for one individual leg.
    """
    s = 1.0 if side >= 0 else -1.0
    p_hip = np.array([s * abs(hip[0]), hip[1], hip[2]], dtype=np.float64)
    p_knee = np.array([s * abs(knee[0]), knee[1], knee[2]], dtype=np.float64)
    p_ankle = np.array([s * abs(ankle[0]), ankle[1], ankle[2]], dtype=np.float64)
    p_foot = np.array([s * abs(foot_pos[0]), 0.0, foot_pos[2]], dtype=np.float64)
    yaw_s = s * foot_yaw
    fwd = np.array([math.sin(yaw_s), 0.0, math.cos(yaw_s)], dtype=np.float64)

    # Start nestled smoothly inside the thorax so the upper thigh flows seamlessly out of the body.
    p_root = np.array([s * 0.056, p_hip[1] + 0.006, p_hip[2] * 0.72], dtype=np.float64)
    femur_mid = 0.5 * (p_hip + p_knee) + np.array([0.0, 0.016, 0.0])
    pre_knee = 0.24 * femur_mid + 0.76 * p_knee + np.array([s * 0.004, 0.006, 0.0])
    post_knee = 0.80 * p_knee + 0.20 * p_ankle + np.array([s * 0.007, -0.004, 0.0])
    tibia_mid = 0.44 * p_knee + 0.56 * p_ankle + np.array([s * 0.009, 0.0, 0.0])
    # Graceful organic ankle curve flowing directly into a tapered rounded-pointed insect tarsus tip.
    ankle_bend = p_foot + np.array([0.0, 0.044, 0.0]) - 0.008 * fwd
    tarsus_arch = p_foot + np.array([0.0, 0.016, 0.0]) + 0.015 * fwd
    tarsus_mid = p_foot + np.array([0.0, 0.011, 0.0]) + 0.044 * fwd
    tarsus_tip = p_foot + np.array([0.0, 0.006, 0.0]) + 0.072 * fwd

    leg_tube = curved_tube(
        [p_root, p_hip, femur_mid, pre_knee, p_knee, post_knee, tibia_mid, ankle_bend, tarsus_arch, tarsus_mid, tarsus_tip],
        [
            (0.034, 0.038),
            (0.030, 0.034),
            (0.032, 0.040),
            (0.027, 0.032),
            (0.024, 0.028),
            (0.022, 0.026),
            (0.019, 0.023),
            (0.017, 0.019),
            (0.017, 0.016),
            (0.012, 0.011),
            (0.0060, 0.0048),
        ],
        samples_per_seg=3,
        radial_segs=12,
        cap_rings=3,
        name=f"leg_{label}",
        material="chitin_dark",
    )

    # Gently seat only the underside contact patch flush on Y = 0.0 (organic grounded claw, never a flat ski).
    def ground_tarsus(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        p[:, 1] = np.maximum(p[:, 1], 0.0)
        return p

    leg_tube = leg_tube.deform(ground_tarsus)
    if side == 0:
        scene.add_mirrored("legs", leg_tube)
    else:
        scene.add("legs", leg_tube)


def _build_legs(
    scene: SceneBuilder,
    *,
    crouch: float = 0.0,
    stance_asymmetry: float = 0.0,
) -> Dict[str, Tuple[float, float, float]]:
    # Sturdy grounded stance with organic crouch flex and optional contrapposto foot stagger (all feet on Y = 0.0).
    c = max(0.0, min(0.24, float(crouch)))
    sa = max(-1.0, min(1.0, float(stance_asymmetry)))
    dz_l = 0.014 * sa
    dz_r = -0.012 * sa
    mid_l = (0.162 + 0.08 * c, 0.0, 0.100 + dz_l)
    mid_r = (-(0.162 + 0.08 * c), 0.0, 0.100 + dz_r)
    rear_l = (0.166 + 0.08 * c, 0.0, -0.124 + 0.7 * dz_l)
    rear_r = (-(0.166 + 0.08 * c), 0.0, -0.124 + 0.7 * dz_r)

    if abs(sa) <= 1e-6:
        _add_flowing_insect_leg(
            scene,
            "mid",
            hip=(0.088, 0.344 - c, 0.018),
            knee=(0.194 + 0.18 * c, 0.356 - 0.52 * c, 0.066),
            ankle=(mid_l[0], 0.036, 0.094),
            foot_pos=mid_l,
            foot_yaw=0.24,
        )
        _add_flowing_insect_leg(
            scene,
            "rear",
            hip=(0.086, 0.314 - c, -0.058),
            knee=(0.198 + 0.18 * c, 0.344 - 0.52 * c, -0.108),
            ankle=(rear_l[0], 0.036, -0.118),
            foot_pos=rear_l,
            foot_yaw=math.pi - 0.26,
        )
    else:
        for side, tag, mf, rf, dz in ((+1, "left", mid_l, rear_l, dz_l), (-1, "right", mid_r, rear_r, dz_r)):
            _add_flowing_insect_leg(
                scene,
                f"mid_{tag}",
                hip=(0.088, 0.344 - c, 0.018),
                knee=(0.194 + 0.18 * c, 0.356 - 0.52 * c, 0.066 + 0.5 * dz),
                ankle=(abs(mf[0]), 0.036, mf[2] - 0.006),
                foot_pos=mf,
                foot_yaw=0.24,
                side=side,
            )
            _add_flowing_insect_leg(
                scene,
                f"rear_{tag}",
                hip=(0.086, 0.314 - c, -0.058),
                knee=(0.198 + 0.18 * c, 0.344 - 0.52 * c, -0.108 + 0.4 * dz),
                ankle=(abs(rf[0]), 0.036, rf[2] + 0.006),
                foot_pos=rf,
                foot_yaw=math.pi - 0.26,
                side=side,
            )

    return {
        "foot_mid_left": mid_l,
        "foot_mid_right": mid_r,
        "foot_rear_left": rear_l,
        "foot_rear_right": rear_r,
    }


def add_idle_animation(
    scene: SceneBuilder,
    *,
    cycle_s: float = 1.65,
    bob_y: float = 0.022,
    bob_x: float = 0.0,
    bob_z: float = 0.0,
    body_pitch: float = 0.022,
    body_roll: float = 0.0,
    head_pitch: float = 0.042,
    head_yaw: float = 0.026,
    head_roll: float = 0.020,
    antenna_wiggle: float = 0.085,
    gaster_pitch: float = 0.055,
    mandible_pitch: float = 0.036,
    breaths_per_cycle: int = 2,
    samples: int = 16,
    name: str = "Idle",
) -> AnimationClip:
    """Attach (or replace) a looping hierarchical glTF 2.0 Idle clip on `scene`.

    Default values match original 1998 Worker `agst301` (1,650 ms calm double-breath
    bob with subtle head nod/sway, antenna twitch, and counter-phase gaster breath,
    while keeping `legs` solidly planted on Y = 0.0).
    """
    dur = max(0.20, float(cycle_s))
    n_steps = max(8, int(samples))
    breaths = max(1, int(breaths_per_cycle))
    times = [round(dur * k / n_steps, 5) for k in range(n_steps + 1)]
    channels: List[AnimationChannel] = []

    def _rest_t(node: str) -> Tuple[float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.translation if xf is not None else (0.0, 0.0, 0.0)

    def _rest_r(node: str) -> Tuple[float, float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)

    if "upper_body" in scene.nodes:
        bx, by, bz = _rest_t("upper_body")
        if abs(bob_y) > 1e-6 or abs(bob_x) > 1e-6 or abs(bob_z) > 1e-6:
            t_vals = []
            for k in range(n_steps + 1):
                phase_b = 2.0 * math.pi * breaths * (k / n_steps)
                phase_s = 2.0 * math.pi * (k / n_steps)
                # Raised-cosine breath wave in [0, 1] peaking cleanly at mid-breath.
                breath = 0.5 * (1.0 - math.cos(phase_b))
                sway = math.sin(phase_s)
                t_vals.append((bx + bob_x * sway, by + bob_y * breath, bz + bob_z * breath))
            t_vals[-1] = t_vals[0]
            channels.append(AnimationChannel("upper_body", "translation", list(times), t_vals))
        if abs(body_pitch) > 1e-6 or abs(body_roll) > 1e-6:
            r0 = _rest_r("upper_body")
            r_vals = []
            for k in range(n_steps + 1):
                phase_b = 2.0 * math.pi * breaths * (k / n_steps)
                phase_s = 2.0 * math.pi * (k / n_steps)
                q = quat_from_euler_xyz(rx=body_pitch * math.sin(phase_b), rz=body_roll * math.sin(phase_s))
                r_vals.append(quat_mul(r0, q))
            r_vals[-1] = r_vals[0]
            channels.append(AnimationChannel("upper_body", "rotation", list(times), r_vals))

    if "head_group" in scene.nodes and (abs(head_pitch) > 1e-6 or abs(head_yaw) > 1e-6 or abs(head_roll) > 1e-6):
        r0 = _rest_r("head_group")
        r_vals = []
        for k in range(n_steps + 1):
            phase_b = 2.0 * math.pi * breaths * (k / n_steps)
            phase_s = 2.0 * math.pi * (k / n_steps)
            q = quat_from_euler_xyz(
                rx=head_pitch * math.sin(phase_b - 0.35),
                ry=head_yaw * math.sin(phase_s),
                rz=head_roll * math.sin(phase_s + 0.45),
            )
            r_vals.append(quat_mul(r0, q))
        r_vals[-1] = r_vals[0]
        channels.append(AnimationChannel("head_group", "rotation", list(times), r_vals))

    if "gaster_group" in scene.nodes and abs(gaster_pitch) > 1e-6:
        r0 = _rest_r("gaster_group")
        r_vals = []
        for k in range(n_steps + 1):
            phase_b = 2.0 * math.pi * breaths * (k / n_steps)
            r_vals.append(quat_mul(r0, quat_from_euler_xyz(rx=-gaster_pitch * math.sin(phase_b))))
        r_vals[-1] = r_vals[0]
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), r_vals))

    if abs(antenna_wiggle) > 1e-6:
        for node_name, side, phase_off in (("antenna_left", 1.0, 0.0), ("antenna_right", -1.0, 0.9)):
            if node_name in scene.nodes and scene.get_node(node_name):
                r0 = _rest_r(node_name)
                r_vals = []
                for k in range(n_steps + 1):
                    p1 = 2.0 * math.pi * breaths * (k / n_steps) + phase_off
                    p2 = 2.0 * math.pi * (k / n_steps) + 0.5 * phase_off
                    q = quat_from_euler_xyz(
                        rx=antenna_wiggle * math.sin(p1),
                        rz=side * 0.55 * antenna_wiggle * math.sin(p2),
                    )
                    r_vals.append(quat_mul(r0, q))
                r_vals[-1] = r_vals[0]
                channels.append(AnimationChannel(node_name, "rotation", list(times), r_vals))

    if "mandibles" in scene.nodes and scene.get_node("mandibles") and abs(mandible_pitch) > 1e-6:
        r0 = _rest_r("mandibles")
        r_vals = []
        for k in range(n_steps + 1):
            phase_b = 2.0 * math.pi * breaths * (k / n_steps)
            r_vals.append(quat_mul(r0, quat_from_euler_xyz(rx=mandible_pitch * 0.5 * (1.0 - math.cos(phase_b)))))
        r_vals[-1] = r_vals[0]
        channels.append(AnimationChannel("mandibles", "rotation", list(times), r_vals))

    return scene.add_animation(name, channels, replace=True)


def apply_head_pose(
    scene: SceneBuilder,
    *,
    tilt_deg: float = 0.0,
    yaw_deg: float = 0.0,
    pitch_deg: float = 0.0,
) -> Tuple[float, float, float, float]:
    """Bake a subtle character rest rotation (roll/yaw/pitch in degrees) onto `head_group`."""
    q = quat_from_euler_xyz(
        rx=math.radians(float(pitch_deg)),
        ry=math.radians(float(yaw_deg)),
        rz=math.radians(float(tilt_deg)),
    )
    if "head_group" in scene.nodes:
        scene.set_node_transform("head_group", rotation=q)
    return q


def build_base_ant(
    scene_name: str = "ant_base",
    *,
    include_antennae: bool = True,
    include_eyes: bool = True,
    include_brows: bool = False,
    brow_style: str = "curious",
    include_idle: bool = True,
    eye_scale_left: float = 1.0,
    eye_scale_right: float = 1.0,
    eye_look: Tuple[float, float] = (0.0, 0.0),
    head_pitch_deg: float = -11.5,
    head_tilt_deg: float = 0.0,
    head_yaw_deg: float = 0.0,
    antenna_asymmetry: float = 0.15,
    stance_asymmetry: float = 0.0,
    crouch: float = 0.0,
    left_arm_pose: ArmPoseLike = None,
    right_arm_pose: ArmPoseLike = None,
    head_tilt_x: float = 0.0,
    blink: float = 0.0,
    scale: float = 1.0,
) -> Tuple[SceneBuilder, Dict[str, Any]]:
    """Build the expressive sculpted base ant with rigged hierarchy and return `(SceneBuilder, anchors)`."""
    scene = SceneBuilder(name=scene_name)
    c = max(0.0, min(0.24, float(crouch)))
    sa = max(-1.0, min(1.0, float(stance_asymmetry)))
    head_center = (0.0, 0.706 - c, 0.024)
    head_radii = (0.192, 0.178, 0.170)

    _build_head(scene, head_center, head_tilt_x)
    s_l = max(0.70, min(1.35, float(eye_scale_left)))
    s_r = max(0.70, min(1.35, float(eye_scale_right)))
    eye_anchors: Dict[str, Any] = {
        "eye_left": (0.0800 + 0.072 * (s_l - 1.0), head_center[1] - 0.002, head_center[2] + 0.134),
        "eye_right": (-(0.0800 + 0.072 * (s_r - 1.0)), head_center[1] - 0.002, head_center[2] + 0.134),
        "eye_center": (0.0, head_center[1] - 0.002, head_center[2] + 0.148),
        "eye_radius": 0.067 * max(s_l, s_r),
        "eyelid_outer_radius": 0.0812 * max(s_l, s_r),
        "eyelid_closed_angle": EYELID_CLOSED_ANGLE,
    }
    if include_eyes:
        eye_anchors = _build_eyes(
            scene,
            head_center,
            head_tilt_x,
            include_brows=include_brows,
            brow_style=brow_style,
            blink=blink,
            eye_scale_left=s_l,
            eye_scale_right=s_r,
            eye_look=eye_look,
        )

    ant_anchors = {
        "antenna_base_left": (0.088, head_center[1] + 0.148, head_center[2] + 0.022),
        "antenna_base_right": (-0.088, head_center[1] + 0.148, head_center[2] + 0.022),
    }
    if include_antennae:
        ant_anchors = _build_antennae(scene, head_center, antenna_asymmetry=antenna_asymmetry)

    body_anchors = _build_body(scene, y_off=-c)
    sh_l, hand_l, hdir_l = _build_arm(scene, left_arm_pose, side=+1, label="left", y_off=-c, stance_asymmetry=sa)
    sh_r, hand_r, hdir_r = _build_arm(scene, right_arm_pose, side=-1, label="right", y_off=-c, stance_asymmetry=sa)
    leg_anchors = _build_legs(scene, crouch=c, stance_asymmetry=sa)

    # Assemble clean hierarchical animation rig while keeping `legs` rooted at Y = 0.0
    # and preserving exact world-space rest geometry via attach_world_node.
    neck_pivot = body_anchors["neck_center"]
    thorax_pivot = body_anchors["thorax_center"]
    gaster_pivot = body_anchors["gaster_pivot"]

    scene.set_node_transform("upper_body", translation=thorax_pivot)
    scene.attach_world_node("body", "upper_body")
    scene.attach_world_node("arms", "upper_body")

    scene.set_node_transform("gaster_group", translation=gaster_pivot)
    scene.attach_world_node("gaster_group", "upper_body")
    scene.attach_world_node("gaster", "gaster_group")

    scene.set_node_transform("head_group", translation=neck_pivot)
    scene.attach_world_node("head_group", "upper_body")
    scene.attach_world_node("head", "head_group")
    scene.attach_world_node("mandibles", "head_group")
    if include_eyes:
        scene.attach_world_node("eyes", "head_group")
        scene.attach_world_node("eyelid_left", "head_group")
        scene.attach_world_node("eyelid_right", "head_group")
    if include_antennae:
        scene.set_node_transform("antennae", translation=neck_pivot)
        scene.attach_world_node("antennae", "head_group")
        scene.attach_world_node("antenna_left", "antennae")
        scene.attach_world_node("antenna_right", "antennae")

    # Apply natural rest contrapposto & character head pitch/tilt/yaw AFTER parenting all children
    # so the entire head assembly rotates cleanly around `neck_pivot` and Idle loops compose on top.
    if abs(sa) > 1e-6:
        scene.set_node_transform("upper_body", rotation=quat_from_euler_xyz(ry=0.055 * sa, rz=-0.025 * sa))
    if abs(float(head_pitch_deg)) > 1e-6 or abs(float(head_tilt_deg)) > 1e-6 or abs(float(head_yaw_deg)) > 1e-6:
        apply_head_pose(scene, tilt_deg=head_tilt_deg, yaw_deg=head_yaw_deg, pitch_deg=head_pitch_deg)

    anchors: Dict[str, Any] = {
        "head_center": head_center,
        "head_top": (head_center[0], head_center[1] + head_radii[1], head_center[2]),
        "head_front": (head_center[0], head_center[1], head_center[2] + head_radii[2]),
        "head_back": (head_center[0], head_center[1], head_center[2] - head_radii[2]),
        "head_radius": 0.192,
        "head_radii": head_radii,
        "brow_y": head_center[1] + 0.072,
        "ground_y": 0.0,
        "crouch": c,
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

    if include_idle:
        add_idle_animation(scene)
    return scene, anchors
