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
    from .mesh_core import (
        AnimationChannel,
        SceneBuilder,
        ellipsoid,
        quat_from_euler_xyz,
        quat_mul,
        superellipsoid,
        swept_tube,
    )
except ImportError:
    from base_ant import build_base_ant, sync_leg_flex_to_clip  # type: ignore
    from hit_clips import add_worker_hit  # type: ignore
    from mesh_core import (  # type: ignore
        AnimationChannel,
        SceneBuilder,
        ellipsoid,
        quat_from_euler_xyz,
        quat_mul,
        superellipsoid,
        swept_tube,
    )


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
    slim_names = {"thorax_core", "chest_plate", "dorsal_plate", "thorax_seam", "shoulder_socket", "shoulder_socket_right"}
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
    """Refine Worker's head (`agst701`/`agst301`/`agst901`): authentic wide rounded skull (~45% H),
    symmetric matched flattened eyes (`z_flat = 0.78`) with lower straight-ahead pupils (zero
    cross-eye), close-set `#1` crusher mandibles, and wide outer-corner thick clubbed antennae.
    """
    hc = np.asarray(anchors["head_center"], dtype=np.float64)
    nc = np.asarray(anchors["neck_center"], dtype=np.float64)
    x_scale = 0.94
    z_scale = 0.88
    face_dz = -0.018  # keeps front facial features flush on the 0.88x compressed cranium face
    eye_out = -0.009  # centers eyes harmoniously inside the slimmer rounded cranium

    def _deform_head(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        rx = p[:, 0] - hc[0]
        ry = p[:, 1] - hc[1]
        rz = p[:, 2] - hc[2]
        # 1. Wide rounded cranium (`1.10x` X -> ~45% of H matching `ORIG agst701`/`agst301`) & sleek depth (`0.86x` Z).
        p[:, 0] = hc[0] + x_scale * rx
        p[:, 2] = hc[2] + z_scale * rz
        # 2. Keep a full, cute rounded forehead high enough to wrap cleanly over the tall viewer-left eye
        #    (gentle upper-front brow fullness so zero white sclera or open eyelid pokes out of the crown),
        #    while softly rounding the upper-back occiput corner (`ORIG agst301`/`agst901`).
        crown_t = np.clip((ry - 0.045) / 0.143, 0.0, 1.0)
        corner_w = _smoothstep(0.030, 0.145, ry) * _smoothstep(-0.020, -0.135, rz)
        brow_w = _smoothstep(0.025, 0.125, ry) * _smoothstep(0.015, 0.125, rz)
        p[:, 1] += -0.008 * (crown_t ** 1.45) + 0.012 * brow_w - 0.005 * corner_w
        p[:, 2] += 0.014 * corner_w + 0.016 * brow_w
        return p

    head_parts = scene.get_node("head")
    new_head = []
    deformed_cranium = None
    for p in head_parts:
        if p.name == "cranium":
            deformed_cranium = p.deform(_deform_head)
            new_head.append(deformed_cranium)
        elif p.name == "mouth_cleft":
            new_head.append(p.scale(0.96, 1.0, 1.0).translate(0.0, 0.0, face_dz))
        else:
            new_head.append(p)
    scene.nodes["head"] = new_head

    # Asymmetric `ORIG agst301` eyes on the wide cranium: viewer-left (`-X`, `right`, 7x6 px) is
    # ~1.32x taller than viewer-right (`+X`, `left`, 5x7 px) at virtually the same width, lifted
    # `+0.010` so both eyes share a level lower baseline (`y=18`) with ZERO cross-eye (`gaze_ry=0.0`)!
    z_flat = 0.78
    eye_dy_right = 0.010
    eye_specs = {
        "left": (1.05, 0.94, +0.15, +0.13, 0.0, 1.24),
        "right": (1.03, 1.28, -0.15, +0.11, 0.0, 1.28),
    }
    if "eyes" in scene.nodes:
        new_eyes = []
        for p in scene.get_node("eyes"):
            tag = "left" if p.name.endswith("_left") else ("right" if p.name.endswith("_right") else "")
            sx = +eye_out if tag == "left" else (-eye_out if tag == "right" else 0.0)
            sy = eye_dy_right if tag == "right" else 0.0
            if tag:
                ec = anchors[f"eye_{tag}"]
                s_e, a_e, yaw_e, gaze_rx, gaze_ry, p_scale = eye_specs[tag]
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
                    # Transform into sclera-normalized unit sphere so enlargement & straight-ahead
                    # lower gaze stay 100% conformal on the flatter ellipsoidal sclera (zero bleed-through!).
                    part = (
                        p.translate(-ec[0], -ec[1], -ec[2])
                        .rotate_xyz(ry=-yaw_e)
                        .rotate_xyz(rx=+0.06)
                        .scale(1.0 / rx_s, 1.0 / ry_s, 1.0 / rz_s)
                    )
                    if not p.name.startswith("catchlight_"):
                        part = part.scale(p_scale, p_scale, 1.03)
                    else:
                        part = part.scale(1.0, 1.0, 1.02)
                    p = (
                        part.rotate_xyz(rx=gaze_rx, ry=gaze_ry)
                        .scale(rx_s, ry_s, rz_s * z_flat)
                        .rotate_xyz(rx=-0.06)
                        .rotate_xyz(ry=+yaw_e)
                        .translate(ec[0], ec[1], ec[2])
                    )
            new_eyes.append(p.translate(sx, sy, face_dz))
        scene.nodes["eyes"] = new_eyes
    for node_name, sx, sy in (("eyelid_left", +eye_out, 0.0), ("eyelid_right", -eye_out, eye_dy_right)):
        if node_name in scene.nodes:
            # Match eyelids cleanly to the flatter `0.78x` sclera while keeping full Blink=1.0 seal!
            scene.nodes[node_name] = [p.scale(1.0, 1.0, 0.92) for p in scene.get_node(node_name)]
        xf = scene.get_node_transform(node_name)
        if xf is not None:
            tx, ty, tz = xf.translation
            scene.set_node_transform(
                node_name,
                translation=(tx + sx, ty + sy, tz + face_dz),
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
                .scale(1.02, 1.08, 1.06)
                .translate(cx * 0.94, -0.004, 0.004)
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

    # Rebuild Worker's wide-set outer-corner antennae (`~78%` head width apart, crisp `\ /` V-flare
    # to `~112%` head width) with thicker overall shaft & plump clubbed tips (`ORIG agst701`/`agst301`).
    cr_pts = deformed_cranium.positions.astype(np.float64) if deformed_cranium is not None else None
    rads = [
        (0.024, 0.024),
        (0.022, 0.022),
        (0.018, 0.019),
        (0.022, 0.023),
        (0.029, 0.030),
        (0.034, 0.035),
        (0.028, 0.029),
    ]
    for ant_node, tag, side in (("antenna_left", "left", +1.0), ("antenna_right", "right", -1.0)):
        xf = scene.get_node_transform(ant_node)
        if xf is None:
            continue
        bx = side * 0.136
        bz = float(hc[2]) + 0.010
        by = float(hc[1]) + 0.106
        if cr_pts is not None:
            mask = (np.abs(cr_pts[:, 0] - bx) < 0.026) & (np.abs(cr_pts[:, 2] - bz) < 0.032) & (cr_pts[:, 1] > hc[1] + 0.04)
            if np.any(mask):
                by = float(np.percentile(cr_pts[mask, 1], 88)) - 0.006
        lift = 0.010 if side > 0 else -0.006
        curl_z = 0.006 if side > 0 else -0.008
        # Socket-local coordinates so Idle/Attack/Hit animations pivot right at the outer crown corner!
        pts = [
            (-side * 0.016, -0.022, -0.004),
            (0.0, 0.0, 0.0),
            (side * 0.024, 0.054 + 0.4 * lift, 0.010 + 0.3 * curl_z),
            (side * 0.042, 0.100 + 0.8 * lift, 0.024 + 0.6 * curl_z),
            (side * 0.052, 0.124 + lift, 0.044 + 0.8 * curl_z),
            (side * 0.056, 0.124 + lift, 0.062 + curl_z),
            (side * 0.058, 0.112 + 0.9 * lift, 0.074 + curl_z),
        ]
        stalk = swept_tube(
            pts,
            rads,
            radial_segs=12,
            smooth_subdiv=3,
            cap_rings=4,
            name=f"antenna_stalk_{tag}",
            material="chitin_dark",
        )
        sock = ellipsoid(0.026, 0.013, 0.026, u_segs=12, v_segs=8, name=f"antenna_socket_{tag}", material="chitin_dark").rotate_xyz(
            rx=0.18, rz=-side * 0.50
        )
        scene.nodes[ant_node] = [stalk, sock]
        scene.set_node_transform(
            ant_node,
            translation=(bx - float(nc[0]), by - float(nc[1]), bz - float(nc[2])),
            rotation=xf.rotation,
            scale=xf.scale,
        )


def _tuck_worker_legs(scene: SceneBuilder, sx_in: float = 0.85) -> None:
    """Pull Worker's 4 legs cleanly ~15% closer together in X around the pelvis seam (`ORIG agst701`/`301`)."""
    for node in ("leg_mid_left", "leg_mid_right", "leg_rear_left", "leg_rear_right"):
        xf = scene.get_node_transform(node)
        parts = scene.get_node(node)
        if xf is None or not parts:
            continue
        tx, ty, tz = xf.translation
        new_tx = tx * sx_in

        def _narrow_x(pos: np.ndarray, px: float = tx, n_px: float = new_tx) -> np.ndarray:
            p = pos.copy()
            wx = p[:, 0] + px
            w = np.clip((np.abs(wx) - 0.036) / 0.048, 0.0, 1.0)
            p[:, 0] = wx * (1.0 - w + w * sx_in) - n_px
            return p

        scene.nodes[node] = [p.deform(_narrow_x) for p in parts]
        scene.set_node_transform(node, translation=(new_tx, ty, tz), rotation=xf.rotation, scale=xf.scale)


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
        head_tilt_deg=2.0,
        head_yaw_deg=0.0,
        eye_look=(0.0, 0.0),
        eye_scale_left=1.05,
        eye_scale_right=1.03,
        eye_aspect_left=0.94,
        eye_aspect_right=1.28,
        antenna_asymmetry=0.18,
        stance_asymmetry=0.35,
        leg_style="worker",
        left_arm_pose="idle",
        right_arm_pose="idle",
        blink=blink,
    )
    _sculpt_worker_head_profile(scene, anchors)
    _slim_worker_torso_and_tail(scene, anchors)
    _add_carapace_details(scene, anchors)
    _tuck_worker_legs(scene, sx_in=0.85)
    scene.scale_all(0.97)
    scene.ground_to_y(0.0)
    sync_leg_flex_to_clip(scene, "Idle")
    _add_worker_attack_animation(scene)
    add_worker_hit(scene)
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical module entrypoint for `build_all.py`."""
    return build_worker(blink=blink)


__all__ = ["build_ant", "build_worker"]
