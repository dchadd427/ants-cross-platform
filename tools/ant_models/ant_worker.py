"""Sculpted 3D model for the classic Worker Ant.

Built directly on `base_ant` so it keeps the iconic unadorned slim upright
Worker silhouette (no headwear or tools), with subtle smooth sculpted chitin
finishing touches fitted flush to the base anatomy: a curved chest carapace
crest, a dorsal pronotum ridge, sleek shoulder fairings, and slim wrist cuffs.
The base supplies the broad-crowned heart-shield head, huge 3-part alert eyes,
plump lobster-claw mandibles, flowing ball-free insect legs, clubbed antennae,
and the pivoted `eyelid_left` / `eyelid_right` nodes with the glTF 2.0 "Blink"
clip.

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet grounded cleanly at Y = 0.0)
  +Z = front (South / canonical camera 301)
  +X = ant's left (viewer's right from front)
"""
from __future__ import annotations

from typing import List, Tuple

import numpy as np

try:
    from .base_ant import build_base_ant, sync_leg_flex_to_clip
    from .hit_clips import add_worker_hit
    from .mesh_core import AnimationChannel, SceneBuilder, quat_from_euler_xyz, quat_mul, superellipsoid
except ImportError:
    from base_ant import build_base_ant, sync_leg_flex_to_clip  # type: ignore
    from hit_clips import add_worker_hit  # type: ignore
    from mesh_core import AnimationChannel, SceneBuilder, quat_from_euler_xyz, quat_mul, superellipsoid  # type: ignore


def _add_carapace_details(scene: SceneBuilder, anchors: dict) -> None:
    """Subtle smooth organic chitin accents fitted flush onto Worker's fuller 0.93x thorax."""
    # Petite curved breastplate crest hugging the fuller chest carapace (`z_max ~ 0.088`).
    crest = (
        superellipsoid(0.036, 0.048, 0.015, e1=0.80, e2=0.84, u_segs=16, v_segs=12, name="chest_crest", material="chitin_team")
        .rotate_xyz(rx=0.10)
        .translate(0.0, 0.402, 0.072)
    )
    # Sleek dorsal pronotum ridge sitting flush atop the fuller rear dorsal carapace (`z_min ~ -0.090`).
    ridge = (
        superellipsoid(0.031, 0.024, 0.029, e1=0.80, e2=0.84, u_segs=16, v_segs=10, name="dorsal_ridge", material="chitin_team")
        .rotate_xyz(rx=-0.16)
        .translate(0.0, 0.432, -0.068)
    )
    scene.add("worker_carapace", [crest, ridge])

    # Subtle curved team-colored shoulder fairings anchored cleanly onto the fuller shoulders.
    sh_l = anchors["shoulder_left"]
    cap = (
        superellipsoid(0.021, 0.017, 0.020, e1=0.80, e2=0.84, u_segs=14, v_segs=10, name="shoulder_cap", material="chitin_team")
        .rotate_xyz(rz=-0.34)
        .translate(sh_l[0] * 0.92, sh_l[1] + 0.006, sh_l[2] * 0.90)
    )
    scene.add_mirrored("worker_carapace", cap)

    # Parent canonical world-authored accents under upper_body in lockstep with stance contrapposto & Idle.
    # (Wrist cuffs now ride natively on `base_ant`'s articulated `forearm_left`/`forearm_right` nodes!)
    scene.attach_world_node("worker_carapace", "upper_body", preserve_world_pose=False)


def _slim_worker_torso_and_tail(scene: SceneBuilder, anchors: dict) -> None:
    """Shape Worker's balanced thorax (~0.93x X/Z, 1.04x Y — fuller between head & gaster for
    `ORIG agst701`) and sleek pointed gaster (~0.76x radial thickness) with seamless joints.
    """
    tc = np.asarray(anchors["thorax_center"], dtype=np.float64)
    # 1. Shape thorax shell parts cleanly around `thorax_center` (keeping neck_joint & petiole seamless).
    slim_names = {"thorax_core", "chest_plate", "dorsal_plate", "thorax_seam"}
    new_body = []
    for p in scene.get_node("body"):
        if p.name in slim_names:
            new_body.append(
                p.translate(-tc[0], -tc[1], -tc[2])
                .scale(0.93, 1.04, 0.93)
                .translate(tc[0], tc[1], tc[2])
            )
        elif p.name == "neck_collar":
            new_body.append(
                p.translate(-tc[0], -tc[1], -tc[2])
                .scale(0.95, 1.0, 0.95)
                .translate(tc[0], tc[1], tc[2])
            )
        else:
            new_body.append(p)
    scene.nodes["body"] = new_body

    # Seat shoulder arm pivots flush & slightly higher on the fuller 0.93x thorax (clean arm<->leg clearance).
    for arm_node, sign in (("arm_left", +1.0), ("arm_right", -1.0)):
        xf = scene.get_node_transform(arm_node)
        if xf is not None:
            tx, ty, tz = xf.translation
            scene.set_node_transform(
                arm_node,
                translation=(tx - sign * 0.007, ty + 0.010, tz),
                rotation=xf.rotation,
                scale=xf.scale,
            )

    # 2. Slim gaster radially (~0.76x cross-section) around its tilted axis from the petiole hinge,
    #    keeping full pointed length & seamless petiole junction (`agst901`).
    gp = (0.0, 0.278, -0.112)
    g_tilt = -0.24
    if "gaster" in scene.nodes:
        scene.nodes["gaster"] = [
            p.translate(-gp[0], -gp[1], -gp[2])
            .rotate_xyz(rx=-g_tilt)
            .scale(0.76, 0.76, 1.00)
            .rotate_xyz(rx=g_tilt)
            .translate(gp[0], gp[1], gp[2])
            for p in scene.get_node("gaster")
        ]


def _smoothstep(edge0: float, edge1: float, val: np.ndarray) -> np.ndarray:
    t = np.clip((val - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _sculpt_worker_head_profile(scene: SceneBuilder, anchors: dict) -> None:
    """Refine Worker's head (`agst301`/`agst901`): slim width-wise (`0.82x` X so eyes span right
    to the outer edges of the head with zero wide cheek walls), slim front-to-back (`0.86x` Z),
    compact smoothly rounded rear occiput, and flush-seated eyes, concentric eyelids, & mandibles.
    """
    hc = np.asarray(anchors["head_center"], dtype=np.float64)
    nc = np.asarray(anchors["neck_center"], dtype=np.float64)
    x_scale = 0.84
    z_scale = 0.86
    face_dz = -0.022  # keeps front facial features flush on the 0.86x compressed cranium face
    eye_in_left = 0.012
    eye_in_right = 0.016  # slightly extra tuck on bigger viewer-left eye for <= 0 mm eyelid protrusion

    def _deform_head(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        rx = p[:, 0] - hc[0]
        ry = p[:, 1] - hc[1]
        rz = p[:, 2] - hc[2]
        # 1. Slim width-wise (`0.84x` X -> eyes span full head width like `ORIG agst301`) & depth-wise (`0.86x` Z).
        p[:, 0] = hc[0] + x_scale * rx
        p[:, 2] = hc[2] + z_scale * rz
        # 2. Smoothly lower the tall forehead (~38 mm less crown height above the eyes, keeping a
        #    cute rounded top arch — never flat) and gently round the upper-back occiput corner.
        crown_t = np.clip((ry - 0.045) / 0.143, 0.0, 1.0)
        corner_w = _smoothstep(0.030, 0.145, ry) * _smoothstep(-0.020, -0.135, rz)
        p[:, 1] -= 0.038 * (crown_t ** 1.45) + 0.005 * corner_w
        p[:, 2] += 0.014 * corner_w
        return p

    head_parts = scene.get_node("head")
    new_head = []
    for p in head_parts:
        if p.name == "cranium":
            new_head.append(p.deform(_deform_head))
        elif p.name == "mouth_cleft":
            new_head.append(p.scale(0.86, 1.0, 1.0).translate(0.0, 0.0, face_dz))
        else:
            new_head.append(p)
    scene.nodes["head"] = new_head

    # Shift eyes & concentric Blink eyelids cleanly backward (`face_dz`) and subtly inward;
    # flatten sclera Z depth (`0.78x`, less bulbous/round) and seat enlarged `1.24x` pupils
    # cleanly in the lower half (`L +0.12, R +0.18`, inward `+-0.11`) with `ORIG agst301`'s
    # bright white upper sclera crescent while still looking straight out at `301 S (45°)`.
    z_flat = 0.78
    eye_specs = {
        "left": (0.94, 0.92, +0.15, +0.12, -0.11),
        "right": (1.05, 1.18, -0.15, +0.18, +0.11),
    }
    if "eyes" in scene.nodes:
        new_eyes = []
        for p in scene.get_node("eyes"):
            tag = "left" if p.name.endswith("_left") else ("right" if p.name.endswith("_right") else "")
            sx = -eye_in_left if tag == "left" else (+eye_in_right if tag == "right" else 0.0)
            if tag:
                ec = anchors[f"eye_{tag}"]
                s_e, a_e, yaw_e, gaze_rx, gaze_ry = eye_specs[tag]
                rx_s, ry_s, rz_s = 0.0678 * s_e, 0.0710 * s_e * a_e, 0.0646 * s_e
                if p.name.startswith(("sclera_", "eye_socket_rim_")):
                    p = (
                        p.translate(-ec[0], -ec[1], -ec[2])
                        .rotate_xyz(ry=-yaw_e)
                        .rotate_xyz(rx=+0.06)
                        .scale(1.0, 1.0, z_flat)
                        .rotate_xyz(rx=-0.06)
                        .rotate_xyz(ry=+yaw_e)
                        .translate(ec[0], ec[1], ec[2])
                    )
                elif p.name.startswith(("limbal_", "iris_", "pupil_", "catchlight_")):
                    # Transform into sclera-normalized unit sphere so enlargement & camera-aimed
                    # gaze stay 100% conformal on the flatter ellipsoidal sclera (zero bleed-through!).
                    part = (
                        p.translate(-ec[0], -ec[1], -ec[2])
                        .rotate_xyz(ry=-yaw_e)
                        .rotate_xyz(rx=+0.06)
                        .scale(1.0 / rx_s, 1.0 / ry_s, 1.0 / rz_s)
                    )
                    if not p.name.startswith("catchlight_"):
                        part = part.scale(1.24, 1.24, 1.03)
                    else:
                        part = part.scale(1.0, 1.0, 1.02)
                    p = (
                        part.rotate_xyz(rx=gaze_rx, ry=gaze_ry)
                        .scale(rx_s, ry_s, rz_s * z_flat)
                        .rotate_xyz(rx=-0.06)
                        .rotate_xyz(ry=+yaw_e)
                        .translate(ec[0], ec[1], ec[2])
                    )
            new_eyes.append(p.translate(sx, 0.0, face_dz))
        scene.nodes["eyes"] = new_eyes
    for node_name, sx, x_trim in (("eyelid_left", -eye_in_left, 1.0), ("eyelid_right", +eye_in_right, 0.96)):
        if node_name in scene.nodes:
            # Match eyelid contour gently to the flatter `0.78x` sclera while keeping full Blink=1.0 seal!
            scene.nodes[node_name] = [p.scale(x_trim, 1.0, 0.92) for p in scene.get_node(node_name)]
        xf = scene.get_node_transform(node_name)
        if xf is not None:
            tx, ty, tz = xf.translation
            scene.set_node_transform(
                node_name,
                translation=(tx + sx, ty, tz + face_dz),
                rotation=xf.rotation,
                scale=xf.scale,
            )

    # Keep both crusher mandibles close together around center (~1.00x X offset) while preserving
    # chunky fullness (`1.12x X, 1.12x Y, 1.10x Z`) so Worker inherits `@core`'s close-set `#1` claws cleanly!
    if "mandibles" in scene.nodes:
        new_mands = []
        for p in scene.get_node("mandibles"):
            cx = float(p.positions[:, 0].mean())
            part = (
                p.translate(-cx, 0.0, 0.0)
                .scale(1.12, 1.12, 1.10)
                .translate(cx, -0.004, 0.004)
            )
            new_mands.append(part)
        scene.nodes["mandibles"] = new_mands
    m_xf = scene.get_node_transform("mandibles")
    if m_xf is not None:
        tx, ty, tz = m_xf.translation
        scene.set_node_transform(
            "mandibles",
            translation=(tx, ty, tz + face_dz),
            rotation=m_xf.rotation,
            scale=m_xf.scale,
        )

    # Bring antenna bases inward onto the narrower 0.82x crown and nudge by exact local crown delta.
    for ant_node in ("antenna_left", "antenna_right"):
        xf = scene.get_node_transform(ant_node)
        if xf is None:
            continue
        base_canon = np.asarray(xf.translation, dtype=np.float64) + nc
        sample = np.array([[base_canon[0], hc[1] + 0.185, base_canon[2]]], dtype=np.float64)
        delta = _deform_head(sample)[0] - sample[0]
        tx, ty, tz = xf.translation
        scene.set_node_transform(
            ant_node,
            translation=(tx * 0.86, ty + float(delta[1]), tz + float(delta[2])),
            rotation=xf.rotation,
            scale=xf.scale,
        )


def _add_worker_attack_animation(scene: SceneBuilder) -> None:
    """Attach authentic 1998 Worker `agat` 0.42s diving headbutt-lunge & chomp `"Attack"` clip.

    6-frame cadence `[60, 60, 60, 90, 60, 90]` ms (`0.42 s`):
    - f0 (0.00s): diving forward windup
    - f1 (0.06s): deep horizontal headbutt dive (`+1.18 rad`), gaster kicking high behind
    - f2 (0.12s): low power lunge forward, arms splayed wide, mandibles flung wide open
    - f3 (0.18s..0.24s, 90ms): peak forward lunge & fierce mandible chomp snap shut
    - f4 (0.27s): snaps tall & proudly upright with arms flaring excitedly
    - f5 (0.33s..0.38s -> 0.42s): upright settle looping cleanly back into f0
    """
    times = [0.00, 0.06, 0.12, 0.18, 0.23, 0.25, 0.27, 0.33, 0.38, 0.42]

    def _rest_t(node: str) -> Tuple[float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.translation if xf is not None else (0.0, 0.0, 0.0)

    def _rest_r(node: str) -> Tuple[float, float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)

    def _rot_track(node: str, eulers: List[Tuple[float, float, float]]) -> AnimationChannel:
        r0 = _rest_r(node)
        qs: List[Tuple[float, float, float, float]] = []
        for rx, ry, rz in eulers:
            q = quat_mul(r0, quat_from_euler_xyz(rx=rx, ry=ry, rz=rz))
            if qs and sum(a * b for a, b in zip(qs[-1], q)) < 0.0:
                q = (-q[0], -q[1], -q[2], -q[3])
            qs.append(q)
        qs[-1] = qs[0] if (len(qs) < 2 or sum(a * b for a, b in zip(qs[-2], qs[0])) >= 0.0) else tuple(-c for c in qs[0])
        return AnimationChannel(node, "rotation", list(times), qs)

    channels: List[AnimationChannel] = []
    bx, by, bz = _rest_t("upper_body")
    body_dxyz = [
        (0.000, -0.018, +0.024),  # f0 0.00s: dive start
        (0.000, -0.044, +0.056),  # f1 0.06s: deep horizontal headbutt dive
        (0.000, -0.036, +0.088),  # f2 0.12s: low spring lunge
        (0.000, -0.028, +0.104),  # f3 0.18s: peak forward lunge & chomp
        (0.000, -0.024, +0.096),  # f3 hold 0.23s
        (0.000, +0.004, +0.058),  # rebound mid 0.25s (keeps hip-tracking sub-mm!)
        (0.000, +0.032, +0.024),  # f4 0.27s: snap tall & upright
        (0.000, +0.010, +0.004),  # f5 0.33s: upright settle
        (0.000, -0.004, +0.012),  # f5->f0 0.38s
        (0.000, -0.018, +0.024),  # wrap 0.42s == f0
    ]
    channels.append(
        AnimationChannel(
            "upper_body",
            "translation",
            list(times),
            [(bx + dx, by + dy, bz + dz) for dx, dy, dz in body_dxyz],
        )
    )
    channels.append(
        _rot_track(
            "upper_body",
            [
                (+0.58, 0.0, 0.0),
                (+1.14, 0.0, 0.0),
                (+1.00, 0.0, 0.0),
                (+0.78, 0.0, 0.0),
                (+0.68, 0.0, 0.0),
                (+0.26, 0.0, 0.0),
                (-0.16, 0.0, 0.0),
                (-0.04, 0.0, 0.0),
                (+0.24, 0.0, 0.0),
                (+0.58, 0.0, 0.0),
            ],
        )
    )
    channels.append(
        _rot_track(
            "head_group",
            [
                (+0.10, 0.0, 0.0),
                (-0.14, 0.0, 0.0),
                (-0.26, 0.0, 0.0),
                (-0.34, 0.0, 0.0),
                (-0.30, 0.0, 0.0),
                (-0.18, 0.0, 0.0),
                (-0.06, 0.0, 0.0),
                (+0.00, 0.0, 0.0),
                (+0.06, 0.0, 0.0),
                (+0.10, 0.0, 0.0),
            ],
        )
    )
    channels.append(
        _rot_track(
            "gaster_group",
            [
                (+0.16, 0.0, 0.0),
                (+0.34, 0.0, 0.0),
                (+0.28, 0.0, 0.0),
                (+0.20, 0.0, 0.0),
                (+0.16, 0.0, 0.0),
                (+0.02, 0.0, 0.0),
                (-0.14, 0.0, 0.0),
                (-0.04, 0.0, 0.0),
                (+0.06, 0.0, 0.0),
                (+0.16, 0.0, 0.0),
            ],
        )
    )
    # Fierce mandible chomp: flare wide open on dive/lunge (f1-f2), snap sharply shut at f3 impact!
    if "mandibles" in scene.nodes and scene.get_node("mandibles"):
        channels.append(
            _rot_track(
                "mandibles",
                [
                    (+0.12, 0.0, 0.0),
                    (+0.24, 0.0, 0.0),
                    (+0.30, 0.0, 0.0),
                    (-0.22, 0.0, 0.0),
                    (-0.24, 0.0, 0.0),
                    (-0.06, 0.0, 0.0),
                    (+0.10, 0.0, 0.0),
                    (+0.02, 0.0, 0.0),
                    (+0.06, 0.0, 0.0),
                    (+0.12, 0.0, 0.0),
                ],
            )
        )
        channels.append(
            AnimationChannel(
                "mandibles",
                "scale",
                list(times),
                [
                    (1.06, 1.0, 0.98),
                    (1.14, 1.0, 0.96),
                    (1.18, 1.0, 0.95),
                    (0.82, 1.0, 1.08),
                    (0.80, 1.0, 1.08),
                    (0.94, 1.0, 1.04),
                    (1.06, 1.0, 1.00),
                    (1.00, 1.0, 1.00),
                    (1.03, 1.0, 0.99),
                    (1.06, 1.0, 0.98),
                ],
            )
        )
    # Expressive arms: tuck & brace out-back during deep dive (zero leg clipping!), flare wide on upright snap!
    for arm_n, fore_n, s in (("arm_left", "forearm_left", +1.0), ("arm_right", "forearm_right", -1.0)):
        if arm_n in scene.nodes:
            channels.append(
                _rot_track(
                    arm_n,
                    [
                        (-0.22, 0.0, s * 0.34),
                        (-0.62, 0.0, s * 0.62),
                        (-0.52, 0.0, s * 0.78),
                        (-0.60, 0.0, s * 0.62),
                        (-0.54, 0.0, s * 0.56),
                        (-0.36, 0.0, s * 0.55),
                        (-0.20, 0.0, s * 0.54),
                        (-0.04, 0.0, s * 0.14),
                        (-0.12, 0.0, s * 0.24),
                        (-0.22, 0.0, s * 0.34),
                    ],
                )
            )
        if fore_n in scene.nodes:
            channels.append(
                _rot_track(
                    fore_n,
                    [
                        (-0.18, 0.0, s * 0.12),
                        (-0.34, 0.0, s * 0.24),
                        (-0.22, 0.0, s * 0.32),
                        (-0.28, 0.0, s * 0.24),
                        (-0.24, 0.0, s * 0.20),
                        (-0.17, 0.0, s * 0.20),
                        (-0.10, 0.0, s * 0.20),
                        (0.00, 0.0, 0.00),
                        (-0.08, 0.0, s * 0.06),
                        (-0.18, 0.0, s * 0.12),
                    ],
                )
            )
    # Antennae whip back into the lunge and perk excitedly on the upright rebound!
    for ant_n, s in (("antenna_left", +1.0), ("antenna_right", -1.0)):
        if ant_n in scene.nodes and scene.get_node(ant_n):
            channels.append(
                _rot_track(
                    ant_n,
                    [
                        (-0.18, 0.0, s * 0.06),
                        (-0.34, 0.0, s * 0.12),
                        (-0.40, 0.0, s * 0.14),
                        (-0.32, 0.0, s * 0.10),
                        (-0.26, 0.0, s * 0.08),
                        (-0.02, 0.0, s * 0.11),
                        (+0.22, 0.0, s * 0.14),
                        (+0.06, 0.0, s * 0.04),
                        (-0.06, 0.0, s * 0.05),
                        (-0.18, 0.0, s * 0.06),
                    ],
                )
            )

    scene.add_animation("Attack", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Attack")


def build_worker(blink: float = 0.0) -> SceneBuilder:
    """Build the curious, charming classic Worker Ant (`blink` in [0, 1] sets resting eyelid closure)."""
    scene, anchors = build_base_ant(
        scene_name="ant_worker",
        include_brows=False,
        brow_style="none",
        head_pitch_deg=-13.5,
        head_tilt_deg=4.5,
        head_yaw_deg=-3.5,
        eye_look=(0.0, 0.0),
        eye_scale_left=0.94,
        eye_scale_right=1.05,
        eye_aspect_left=0.92,
        eye_aspect_right=1.18,
        antenna_asymmetry=0.35,
        stance_asymmetry=0.35,
        leg_style="worker",
        left_arm_pose="idle",
        right_arm_pose="idle",
        blink=blink,
    )
    _sculpt_worker_head_profile(scene, anchors)
    _slim_worker_torso_and_tail(scene, anchors)
    _add_carapace_details(scene, anchors)
    scene.scale_all(0.97)
    scene.ground_to_y(0.0)
    _add_worker_attack_animation(scene)
    add_worker_hit(scene)
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical module entrypoint for `build_all.py`."""
    return build_worker(blink=blink)


__all__ = ["build_ant", "build_worker"]
