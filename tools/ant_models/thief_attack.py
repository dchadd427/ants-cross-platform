"""Authentic 1998 Thief Ant `atat` (720 ms) wide folded bandana cloth slap `"Attack"` clip & meshes.

Split from `ant_thief.py` to stay well under per-file size limits and keep imports pure-stdlib safe.
Choreographs all 12 original `atat301`/`atat901` frames (`[60, 40, 80, 60, 40, 40, 80, 80, 60, 80, 60, 40]` ms):
- f0..f2 (0.00..0.10 s): right arm reaches up and peels the wide golden bandana off the brow
- f3..f5 (0.18..0.28 s): worn bandana hides (`1e-4`), exposing Thief's bare green head while
  `thief_whip_lasso` swings the wide folded golden bandana cloth off his head into a broad windup
- f6 (0.32 s): forward lunge & wide flat-ribbed golden bandana (`thief_whip_crack` + twin knot tails)
  slapping straight forward out of his right hand like a folded cloth towel slap
- f7 (0.40 s): wide folded bandana curls upward & back on impact recoil (`thief_whip_recoil`)
- f8 (0.48 s): wide folded bandana sweeps cleanly back around his brow (`thief_whip_lasso`)
- f9..f11 (0.54..0.72 s): worn bandana snaps right back onto Thief's face and settles to rest
"""
from __future__ import annotations

import math
from typing import List, Sequence, Tuple

try:
    import numpy as np
except ImportError:  # pragma: no cover - pure-stdlib import safety for CI
    np = None  # type: ignore

if np is not None:
    try:
        from .base_ant import sync_leg_flex_to_clip
        from .mesh_core import (
            AnimationChannel,
            MeshPart,
            SceneBuilder,
            catmull_rom_spline,
            quat_from_axis_angle,
            quat_from_euler_xyz,
            quat_mul,
            recompute_smooth_normals,
        )
    except ImportError:
        from base_ant import sync_leg_flex_to_clip  # type: ignore
        from mesh_core import (  # type: ignore
            AnimationChannel,
            MeshPart,
            SceneBuilder,
            catmull_rom_spline,
            quat_from_axis_angle,
            quat_from_euler_xyz,
            quat_mul,
            recompute_smooth_normals,
        )

_HIDE_SCALE = (1e-4, 1e-4, 1e-4)
_SHOW_SCALE = (1.0, 1.0, 1.0)


def _norm(v: "np.ndarray", fallback: Tuple[float, float, float] = (0.0, 1.0, 0.0)) -> "np.ndarray":
    n = float(np.linalg.norm(v))
    return v / n if n > 1e-10 else np.asarray(fallback, dtype=np.float64)


def _cloth_whip_ribbon(
    control_pts: Sequence[Sequence[float]],
    half_widths: Sequence[float],
    half_thicknesses: Sequence[float],
    face_hint: Sequence[float],
    twist_deg: Tuple[float, float],
    *,
    name: str,
    pleat_amp: float = 0.22,
) -> MeshPart:
    """Build a closed, outward-verified wide flat-folded pleated golden bandana cloth (`gear_gold`)."""
    pts = catmull_rom_spline([np.asarray(p, dtype=np.float64) for p in control_pts], samples_per_seg=6, closed=False)
    n = len(pts)
    u = np.linspace(0.0, 1.0, n)
    w_arr = np.interp(u, np.linspace(0.0, 1.0, len(half_widths)), half_widths)
    t_arr = np.interp(u, np.linspace(0.0, 1.0, len(half_thicknesses)), half_thicknesses)
    twists = np.radians(np.linspace(twist_deg[0], twist_deg[1], n))
    tangents = np.array([_norm(pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]) for i in range(n)])

    # Wide flat cloth cross-section with crisp longitudinal folded cloth pleats + hemmed borders.
    segs = 16
    angs = np.linspace(0.0, 2.0 * math.pi, segs, endpoint=False)
    cu = np.sign(np.cos(angs)) * (np.abs(np.cos(angs)) ** 0.56)
    pleat = pleat_amp * np.cos(3.0 * angs) * np.sin(angs)
    su = np.sign(np.sin(angs)) * (np.abs(np.sin(angs)) ** 0.62) + pleat
    ring_lum = np.clip(0.93 + 0.06 * np.cos(3.0 * angs) * np.abs(np.sin(angs)) - 0.08 * (np.abs(np.cos(angs)) ** 2.4), 0.78, 1.0)
    hint = _norm(np.asarray(face_hint, dtype=np.float64))

    frames_w, frames_o, rings = [], [], []
    for i in range(n):
        tg = tangents[i]
        o0 = _norm(hint - np.dot(hint, tg) * tg, (0.0, 0.0, 1.0))
        w0 = _norm(np.cross(o0, tg))
        o0 = np.cross(tg, w0)
        c, s = math.cos(twists[i]), math.sin(twists[i])
        o, w = c * o0 + s * w0, -s * o0 + c * w0
        frames_w.append(w)
        frames_o.append(o)
        rings.append(pts[i][None, :] + (w_arr[i] * cu)[:, None] * w[None, :] + (t_arr[i] * su)[:, None] * o[None, :])

    start_cap = [
        (pts[0] - t_arr[0] * math.sin(0.25 * math.pi) * tangents[0])[None, :]
        + (w_arr[0] * math.cos(0.25 * math.pi) * cu)[:, None] * frames_w[0][None, :]
        + (t_arr[0] * math.cos(0.25 * math.pi) * su)[:, None] * frames_o[0][None, :]
    ]
    end_cap = [
        (pts[-1] + 1.2 * t_arr[-1] * math.sin(0.25 * math.pi) * tangents[-1])[None, :]
        + (w_arr[-1] * math.cos(0.25 * math.pi) * cu)[:, None] * frames_w[-1][None, :]
        + (t_arr[-1] * math.cos(0.25 * math.pi) * su)[:, None] * frames_o[-1][None, :]
    ]
    all_rings = start_cap + rings + end_cap
    apex0 = pts[0] - t_arr[0] * tangents[0]
    apex1 = pts[-1] + 1.2 * t_arr[-1] * tangents[-1]
    positions = np.vstack([apex0[None, :]] + all_rings + [apex1[None, :]]).astype(np.float32)

    nr = len(all_rings)
    end_idx = 1 + nr * segs
    tris: List[Tuple[int, int, int]] = [(0, 1 + (k + 1) % segs, 1 + k) for k in range(segs)]
    for r in range(nr - 1):
        b0, b1 = 1 + r * segs, 1 + (r + 1) * segs
        for k in range(segs):
            nk = (k + 1) % segs
            tris.append((b0 + k, b0 + nk, b1 + nk))
            tris.append((b0 + k, b1 + nk, b1 + k))
    b_last = 1 + (nr - 1) * segs
    tris.extend((end_idx, b_last + k, b_last + (k + 1) % segs) for k in range(segs))
    indices = np.asarray(tris, dtype=np.uint32).reshape(-1)
    part = MeshPart(name, "gear_gold", positions, recompute_smooth_normals(positions, indices), indices).ensure_outward()
    lums = np.concatenate([[0.86], np.tile(ring_lum, nr), [0.86]]).astype(np.float32)
    part.colors = np.column_stack([lums, lums, lums]).astype(np.float32)
    return part


def _add_twin_knot_tails(tip: Sequence[float], fwd: Sequence[float], spread: Sequence[float], lift: Sequence[float], prefix: str) -> List[MeshPart]:
    """Build asymmetric pleated golden bandana knot-tails trailing off the swinging slap tip."""
    p0 = np.asarray(tip, dtype=np.float64)
    f = _norm(np.asarray(fwd, dtype=np.float64))
    s = _norm(np.asarray(spread, dtype=np.float64))
    u = _norm(np.asarray(lift, dtype=np.float64))
    tails: List[MeshPart] = []
    # Natural asymmetric cloth flaps (one longer sweeping flap + one shorter offset flap — never a symmetric fish tail).
    specs = (
        ("a", 1.00, +0.024, +0.022, 0.030, -14.0, +24.0),
        ("b", 0.72, -0.044, -0.014, 0.024, +18.0, -20.0),
    )
    for tag, l_mul, s_off, up_m, max_w, tw0, tw1 in specs:
        pts = [
            p0 - 0.012 * f,
            p0 + 0.034 * l_mul * f + 0.45 * s_off * s + 0.4 * up_m * u,
            p0 + 0.078 * l_mul * f + 0.85 * s_off * s + 0.9 * up_m * u,
            p0 + 0.112 * l_mul * f + 1.00 * s_off * s + 1.1 * up_m * u,
        ]
        tails.append(
            _cloth_whip_ribbon(
                pts,
                half_widths=[0.016, max_w * 0.92, max_w, 0.008],
                half_thicknesses=[0.006, 0.006, 0.005, 0.003],
                face_hint=tuple(float(c) for c in u),
                twist_deg=(tw0, tw1),
                name=f"{prefix}_tail_{tag}",
                pleat_amp=0.14,
            )
        )
    return tails


def add_thief_whip_meshes(scene: SceneBuilder, anchors: dict, *, head_scale: float = 1.0) -> None:
    """Attach hidden (`1e-4` inside opaque thorax) wide folded bandana cloth slap meshes."""
    tc = np.asarray(anchors["thorax_center"], dtype=np.float64)
    nc = np.asarray(anchors["neck_center"], dtype=np.float64)
    hc = np.asarray(anchors["head_center"], dtype=np.float64)
    hs = float(head_scale)
    hy_rel = float((nc[1] - tc[1]) + hs * (hc[1] - nc[1])) - 0.032
    hz_rel = float((nc[2] - tc[2]) + hs * (hc[2] - nc[2]))
    rs = 0.5 * (1.0 + hs)

    # 1. Wide folded golden bandana loop peeling off the head & winding into the slap (`f3..f5, f8`).
    lasso_pts = [
        (-0.09 * rs, hy_rel - 0.09 * rs, hz_rel + 0.12 * rs),
        (-0.26 * rs, hy_rel - 0.02 * rs, hz_rel + 0.10 * rs),
        (-0.30 * rs, hy_rel + 0.10 * rs, hz_rel - 0.04 * rs),
        (-0.14 * rs, hy_rel + 0.18 * rs, hz_rel - 0.21 * rs),
        (+0.15 * rs, hy_rel + 0.16 * rs, hz_rel - 0.20 * rs),
        (+0.29 * rs, hy_rel + 0.05 * rs, hz_rel - 0.02 * rs),
        (+0.21 * rs, hy_rel - 0.05 * rs, hz_rel + 0.14 * rs),
    ]
    lasso_band = _cloth_whip_ribbon(
        lasso_pts,
        half_widths=[0.026, 0.066, 0.084, 0.086, 0.080, 0.064, 0.028],
        half_thicknesses=[0.009, 0.011, 0.011, 0.010, 0.010, 0.009, 0.007],
        face_hint=(0.0, 0.72, 0.69),
        twist_deg=(-16.0, 28.0),
        name="thief_whip_lasso_ribbon",
    )
    lasso_tails = _add_twin_knot_tails(lasso_pts[-1], (-0.25, -0.35, 0.90), (0.85, 0.30, 0.42), (0.20, 0.92, 0.32), "thief_lasso")
    scene.add("thief_whip_lasso", [lasso_band] + lasso_tails, parent="upper_body")
    scene.set_node_transform("thief_whip_lasso", translation=(0.0, 0.0, 0.0), scale=_HIDE_SCALE)

    # 2. Long, sweeping flat-ribbed golden bandana cloth HORIZONTAL SLAP (`f6`: extended ~33% longer
    #    forward reach + crisp level horizontal left-to-right S-wave cloth paddle & billowing knot-tails!).
    crack_pts = [
        (-0.040, +0.096, +0.14),
        (-0.122, +0.140, +0.28),
        (-0.084, +0.184, +0.43),
        (+0.032, +0.228, +0.57),
        (+0.104, +0.262, +0.67),
        (+0.056, +0.278, +0.74),
    ]
    crack_band = _cloth_whip_ribbon(
        crack_pts,
        half_widths=[0.028, 0.068, 0.096, 0.108, 0.112, 0.084],
        half_thicknesses=[0.009, 0.011, 0.011, 0.010, 0.010, 0.008],
        face_hint=(0.12, 0.96, -0.24),
        twist_deg=(-14.0, 24.0),
        name="thief_whip_crack_ribbon",
    )
    crack_tails = _add_twin_knot_tails(
        crack_pts[-1],
        (-0.24, 0.08, 0.96),
        (0.92, 0.10, 0.36),
        (0.12, 0.96, -0.22),
        "thief_slap",
    )
    scene.add("thief_whip_crack", [crack_band] + crack_tails, parent="upper_body")
    scene.set_node_transform("thief_whip_crack", translation=(0.0, 0.0, 0.0), scale=_HIDE_SCALE)

    # 3. Extended wide folded bandana U-fold recoil (`f7`: long sweeping horizontal-to-upward S/U-curl
    #    rebound out in front after the hard horizontal slap, matching `atat301 f7` & `atat901 f7`!).
    recoil_pts = [
        (-0.026, +0.102, +0.15),
        (-0.052, +0.126, +0.30),
        (+0.024, +0.156, +0.46),
        (+0.114, +0.214, +0.58),
        (+0.078, +0.308, +0.54),
        (-0.028, +0.318, +0.42),
    ]
    recoil_band = _cloth_whip_ribbon(
        recoil_pts,
        half_widths=[0.026, 0.062, 0.092, 0.104, 0.096, 0.056],
        half_thicknesses=[0.009, 0.011, 0.011, 0.010, 0.009, 0.007],
        face_hint=(0.18, 0.92, -0.34),
        twist_deg=(-12.0, 22.0),
        name="thief_whip_recoil_ribbon",
    )
    recoil_tails = _add_twin_knot_tails(
        recoil_pts[-1],
        (-0.56, -0.22, -0.80),
        (0.80, 0.22, -0.56),
        (0.18, 0.94, -0.28),
        "thief_recoil",
    )
    scene.add("thief_whip_recoil", [recoil_band] + recoil_tails, parent="upper_body")
    scene.set_node_transform("thief_whip_recoil", translation=(0.0, 0.0, 0.0), scale=_HIDE_SCALE)


def add_thief_attack_animation(scene: SceneBuilder, gaze_angle: float) -> None:
    """Attach authentic 1998 `atat` 0.72 s 12-frame bandana-off horizontal slap `"Attack"` clip."""
    # Exact cumulative timestamps for `[60, 40, 80, 60, 40, 40, 80, 80, 60, 80, 60, 40]` ms = 720 ms,
    # with a 0.30 s mid-swing keyframe between f5 (0.28) and f6 (0.32) for sub-mm hip tracking.
    times = [0.00, 0.06, 0.10, 0.18, 0.24, 0.28, 0.30, 0.32, 0.40, 0.48, 0.54, 0.62, 0.68, 0.72]

    def _rest_t(node: str) -> Tuple[float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.translation if xf is not None else (0.0, 0.0, 0.0)

    def _rest_r(node: str) -> Tuple[float, float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)

    def _rot_track(node: str, eulers: Sequence[Tuple[float, float, float]]) -> AnimationChannel:
        r0 = _rest_r(node)
        qs: List[Tuple[float, float, float, float]] = []
        for rx, ry, rz in eulers:
            q = quat_mul(r0, quat_from_euler_xyz(rx=rx, ry=ry, rz=rz))
            if qs and sum(a * b for a, b in zip(qs[-1], q)) < 0.0:
                q = (-q[0], -q[1], -q[2], -q[3])
            qs.append(q)
        q_end = qs[0]
        if len(qs) >= 2 and sum(a * b for a, b in zip(qs[-2], q_end)) < 0.0:
            q_end = (-q_end[0], -q_end[1], -q_end[2], -q_end[3])
        qs[-1] = q_end
        return AnimationChannel(node, "rotation", list(times), qs)

    channels: List[AnimationChannel] = []
    bx, by, bz = _rest_t("upper_body")
    body_dxyz = [
        (0.000, +0.000, +0.000),  # f0 0.00: coil start
        (-0.012, -0.008, -0.006), # f1 0.06: reach up & grab bandana
        (-0.020, -0.014, -0.010), # f2 0.10: peel bandana off brow
        (-0.012, +0.010, -0.016), # f3 0.18: rip off -> lasso whirl
        (-0.014, +0.012, -0.018), # f4 0.24: overhead lasso spin
        (-0.018, +0.006, -0.014), # f5 0.28: wide back-left horizontal windup
        (-0.002, +0.000, +0.025), # mid 0.30: explosive horizontal sweep midpoint
        (+0.014, -0.006, +0.064), # f6 0.32: EXPLOSIVE LEFT-TO-RIGHT BANDANA SLAP!
        (+0.008, +0.002, +0.036), # f7 0.40: follow-through & extended U-fold recoil
        (+0.006, +0.010, +0.004), # f8 0.48: lasso sweeps back around head
        (-0.008, +0.004, -0.004), # f9 0.54: bandana snaps back on
        (-0.004, +0.002, +0.000), # f10 0.62: tug neat
        (0.000, +0.000, +0.000),  # f11 0.68: settle
        (0.000, +0.000, +0.000),  # wrap 0.72
    ]
    channels.append(
        AnimationChannel("upper_body", "translation", list(times), [(bx + dx, by + dy, bz + dz) for dx, dy, dz in body_dxyz])
    )
    channels.append(
        _rot_track(
            "upper_body",
            [
                (+0.02, -0.06, +0.02),
                (+0.08, -0.14, +0.08),
                (+0.14, -0.22, +0.12),
                (-0.08, -0.12, +0.04),
                (-0.10, -0.24, +0.08),
                (-0.08, -0.32, +0.10),  # f5: coil right shoulder back-left (-X)
                (+0.06, +0.00, +0.00),  # mid: snapping across center
                (+0.20, +0.32, -0.10),  # f6: hard horizontal torso torque across to +X!
                (+0.10, +0.22, -0.06),  # f7: follow-through hold
                (-0.06, +0.10, -0.04),
                (+0.04, -0.08, +0.04),
                (+0.02, -0.04, +0.02),
                (+0.00, +0.00, +0.00),
                (+0.02, -0.06, +0.02),
            ],
        )
    )
    channels.append(
        _rot_track(
            "head_group",
            [
                (+0.02, +0.04, -0.02),
                (+0.10, +0.12, -0.08),
                (+0.16, +0.18, -0.12),
                (-0.06, +0.10, -0.04),
                (-0.08, +0.18, -0.06),
                (-0.04, +0.24, -0.08),  # f5: eyes stay locked forward on target
                (-0.10, +0.00, +0.00),  # mid
                (-0.16, -0.24, +0.08),  # f6: head counter-rotates to stare down victim!
                (-0.10, -0.16, +0.04),  # f7
                (-0.04, -0.06, +0.04),
                (+0.06, +0.06, -0.06),
                (+0.02, +0.02, -0.02),
                (+0.00, +0.00, +0.00),
                (+0.02, +0.04, -0.02),
            ],
        )
    )
    # Eyes lock fierce straight ahead during the bare-headed bandana slap (f3..f8), then dart back left!
    gaze_factors = [-1.0, -0.8, -0.4, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -0.2, -0.7, -0.95, -1.0, -1.0]
    for side in ("left", "right"):
        ball = f"eye_ball_{side}"
        if ball in scene.nodes:
            qs = [quat_from_axis_angle((0.0, 1.0, 0.0), g * gaze_angle) for g in gaze_factors]
            channels.append(AnimationChannel(ball, "rotation", list(times), qs))

    # Right arm grabs bandana (f0-f2), coils wide back-left (f3-f5), whips hard across (f6-f7), re-ties (f8-f10).
    if "arm_right" in scene.nodes:
        channels.append(
            _rot_track(
                "arm_right",
                [
                    (-0.24, +0.10, -0.18),
                    (-0.88, +0.34, -0.48),
                    (-1.12, +0.44, -0.62),
                    (-1.24, +0.12, -0.96),
                    (-1.14, -0.34, -1.14),
                    (-0.74, -0.58, -1.16),  # f5: arm flung wide back-left for horizontal windup
                    (-0.44, +0.04, -0.48),  # mid: snapping horizontally across
                    (-0.14, +0.62, +0.18),  # f6: hard horizontal slap follow-through across chest!
                    (-0.42, +0.52, +0.04),  # f7: high rebound carry-through
                    (-1.04, +0.18, -0.78),
                    (-0.84, +0.32, -0.44),
                    (-0.32, +0.14, -0.20),
                    (-0.08, +0.04, -0.06),
                    (-0.24, +0.10, -0.18),
                ],
            )
        )
    if "forearm_right" in scene.nodes:
        channels.append(
            _rot_track(
                "forearm_right",
                [
                    (-0.14, +0.08, +0.00),
                    (-0.46, +0.22, +0.12),
                    (-0.52, +0.28, +0.16),
                    (-0.24, +0.10, -0.16),
                    (-0.16, -0.14, -0.22),
                    (-0.10, -0.26, -0.18),  # f5: forearm extended out on windup
                    (+0.08, +0.04, -0.02),  # mid
                    (+0.26, +0.34, +0.14),  # f6: wrist/forearm snaps forward-right into slap!
                    (-0.16, +0.26, +0.16),  # f7: recoil snap-up
                    (-0.38, +0.16, +0.06),
                    (-0.44, +0.20, +0.14),
                    (-0.18, +0.08, +0.04),
                    (-0.04, +0.02, +0.00),
                    (-0.14, +0.08, +0.00),
                ],
            )
        )
    # Left arm counterbalances high & cleanly clear above mid-left knee, then helps tug bandana neat at f9.
    if "arm_left" in scene.nodes:
        channels.append(
            _rot_track(
                "arm_left",
                [
                    (-0.08, -0.06, +0.14),
                    (-0.26, -0.14, +0.34),
                    (-0.44, -0.22, +0.48),
                    (-0.32, -0.26, +0.58),
                    (-0.28, -0.32, +0.62),
                    (-0.36, -0.22, +0.54),  # f5
                    (-0.42, -0.34, +0.62),  # mid
                    (-0.48, -0.46, +0.70),  # f6: strong left-arm counterbalance back-right!
                    (-0.36, -0.34, +0.56),  # f7
                    (-0.42, -0.20, +0.44),
                    (-0.56, -0.18, +0.36),
                    (-0.22, -0.10, +0.18),
                    (-0.04, -0.02, +0.04),
                    (-0.08, -0.06, +0.14),
                ],
            )
        )
    if "forearm_left" in scene.nodes:
        channels.append(
            _rot_track(
                "forearm_left",
                [
                    (-0.06, 0.0, +0.06),
                    (-0.18, 0.0, +0.14),
                    (-0.26, 0.0, +0.18),
                    (-0.18, 0.0, +0.20),
                    (-0.14, 0.0, +0.22),
                    (-0.20, 0.0, +0.20),
                    (-0.22, 0.0, +0.21),
                    (-0.24, 0.0, +0.22),
                    (-0.16, 0.0, +0.16),
                    (-0.22, 0.0, +0.16),
                    (-0.34, 0.0, +0.14),
                    (-0.14, 0.0, +0.08),
                    (-0.02, 0.0, +0.02),
                    (-0.06, 0.0, +0.06),
                ],
            )
        )

    # Razor-sharp STEP visibility swaps between worn bandana, overhead lasso, and long horizontal bandana slap!
    if "bandana_worn_group" in scene.nodes:
        worn_vis = [
            _SHOW_SCALE, _SHOW_SCALE, _SHOW_SCALE,
            _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE,
            _SHOW_SCALE, _SHOW_SCALE, _SHOW_SCALE, _SHOW_SCALE,
        ]
        channels.append(AnimationChannel("bandana_worn_group", "scale", list(times), worn_vis, interpolation="STEP"))
        channels.append(
            _rot_track(
                "bandana_worn_group",
                [
                    (0.0, 0.0, 0.0),
                    (-0.08, 0.0, -0.10),
                    (-0.22, -0.08, -0.26),
                    (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                    (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                    (-0.10, +0.08, +0.18),
                    (-0.02, +0.02, +0.04),
                    (0.0, 0.0, 0.0),
                    (0.0, 0.0, 0.0),
                ],
            )
        )

    if "thief_whip_lasso" in scene.nodes:
        lasso_vis = [
            _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE,
            _SHOW_SCALE, _SHOW_SCALE, _SHOW_SCALE, _SHOW_SCALE,
            _HIDE_SCALE, _HIDE_SCALE,
            _SHOW_SCALE,
            _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE,
        ]
        channels.append(AnimationChannel("thief_whip_lasso", "scale", list(times), lasso_vis, interpolation="STEP"))
        channels.append(
            _rot_track(
                "thief_whip_lasso",
                [
                    (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                    (+0.18, +0.20, -0.08),   # f3: wide bandana peels off both sides of bare head
                    (+0.38, -0.24, +0.36),   # f4: broad cloth loops wide around head
                    (+0.72, -0.34, +0.26),   # f5: bold wide back-left horizontal windup!
                    (+0.76, +0.28, -0.12),   # mid: whipping across center into f6 slap!
                    (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                    (+0.52, +3.04, -0.06),   # f8: wide bandana cups low across front cheeks/brow!
                    (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                ],
            )
        )

    if "thief_whip_crack" in scene.nodes:
        crack_vis = [
            _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE,
            (1.02, 1.02, 1.04),      # f6: long explosive horizontal left-to-right bandana SLAP!
            _HIDE_SCALE,             # f7 hands off cleanly to extended curled `thief_whip_recoil`!
            _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE,
        ]
        channels.append(AnimationChannel("thief_whip_crack", "scale", list(times), crack_vis, interpolation="STEP"))
        channels.append(
            _rot_track(
                "thief_whip_crack",
                [
                    (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                    (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                    (+0.00, -0.06, -0.10),   # f6: crisp level horizontal cloth slap impact!
                    (0.0, 0.0, 0.0),
                    (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                ],
            )
        )
        ct0 = _rest_t("thief_whip_crack")
        crack_dzs = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, +0.032, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
        channels.append(
            AnimationChannel(
                "thief_whip_crack",
                "translation",
                list(times),
                [(ct0[0], ct0[1], ct0[2] + dz) for dz in crack_dzs],
            )
        )

    if "thief_whip_recoil" in scene.nodes:
        recoil_vis = [
            _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE,
            _HIDE_SCALE,
            (1.02, 1.02, 1.02),      # f7: long wide folded bandana U-fold snap-back rebound!
            _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE,
        ]
        channels.append(AnimationChannel("thief_whip_recoil", "scale", list(times), recoil_vis, interpolation="STEP"))
        channels.append(
            _rot_track(
                "thief_whip_recoil",
                [
                    (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                    (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                    (0.0, 0.0, 0.0),
                    (-0.08, +0.06, -0.18),   # f7: sweeping extended cloth rebound!
                    (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                ],
            )
        )

    # Lively antenna & gaster inertia across the horizontal bandana slap!
    for ant_n, s in (("antenna_left", +1.0), ("antenna_right", -1.0)):
        if ant_n in scene.nodes and scene.get_node(ant_n):
            channels.append(
                _rot_track(
                    ant_n,
                    [
                        (0.0, 0.0, 0.0), (-0.10, 0.0, s * 0.08), (-0.18, 0.0, s * 0.14),
                        (+0.14, 0.0, -s * 0.16), (+0.18, 0.0, -s * 0.22), (+0.12, 0.0, -s * 0.20),
                        (-0.08, 0.0, 0.0), (-0.30, 0.0, s * 0.16), (-0.18, 0.0, s * 0.12),
                        (+0.12, 0.0, -s * 0.10), (-0.08, 0.0, s * 0.06), (-0.02, 0.0, s * 0.02),
                        (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                    ],
                )
            )
    if "gaster_group" in scene.nodes:
        channels.append(
            _rot_track(
                "gaster_group",
                [
                    (0.0, 0.0, 0.0), (+0.08, 0.0, 0.0), (+0.12, 0.0, 0.0),
                    (-0.08, 0.0, 0.0), (-0.12, 0.0, 0.0), (-0.06, 0.0, 0.0),
                    (+0.06, 0.0, 0.0), (+0.18, 0.0, 0.0), (+0.10, 0.0, 0.0),
                    (-0.04, 0.0, 0.0), (+0.04, 0.0, 0.0), (+0.02, 0.0, 0.0),
                    (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                ],
            )
        )

    scene.add_animation("Attack", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Attack")


def scale_thief_head_assembly(scene: SceneBuilder, scale: float) -> None:
    """Cohesively scale Thief's head subtree around `head_group` neck pivot (zero neck gap)."""
    head_nodes = (
        "head", "mandibles", "antennae", "antenna_left", "antenna_right",
        "eyes", "eye_ball_left", "eye_ball_right", "eyelid_left", "eyelid_right",
        "bandana_worn_group", "gear_bandana", "bandana_tails", "bandana_tail_lower",
    )
    tail_boost = 1.12
    for node in head_nodes:
        s_mesh = scale * tail_boost if node in ("bandana_tails", "bandana_tail_lower") else scale
        parts = scene.get_node(node)
        if parts:
            scene.nodes[node] = [p.scale(s_mesh) for p in parts]
        xf = scene.get_node_transform(node)
        if xf is not None:
            tx, ty, tz = xf.translation
            scene.set_node_transform(node, translation=(tx * scale, ty * scale, tz * scale), rotation=xf.rotation, scale=xf.scale)
    hg_xf = scene.get_node_transform("head_group")
    if hg_xf is not None:
        hx, hy, hz = hg_xf.translation
        scene.set_node_transform("head_group", translation=(hx, hy - 0.006, hz), rotation=hg_xf.rotation, scale=hg_xf.scale)


def compact_thief_legs(scene: SceneBuilder) -> None:
    """Tuck Thief's legs compactly underneath his body in Z & target-clamped X (`ORIG atst901`/`atst701`)."""
    leg_nodes = ("leg_mid_left", "leg_mid_right", "leg_rear_left", "leg_rear_right")
    cur_x = max(abs(scene.get_node_transform(n).translation[0]) for n in leg_nodes if scene.get_node_transform(n))
    sx_out = min(1.0, 0.144 / max(1e-4, cur_x))
    sz_out = 0.87
    z_ctr = -0.014
    for node in leg_nodes:
        xf = scene.get_node_transform(node)
        parts = scene.get_node(node)
        if xf is None or not parts:
            continue
        tx, ty, tz = xf.translation
        new_tx = tx * sx_out
        new_tz = z_ctr + (tz - z_ctr) * sz_out

        def _tuck(pos: "np.ndarray", px: float = tx, pz: float = tz, n_px: float = new_tx, n_pz: float = new_tz) -> "np.ndarray":
            p = pos.copy()
            wx = p[:, 0] + px
            wz = p[:, 2] + pz
            w = np.clip((np.abs(wx) - 0.042) / 0.055, 0.0, 1.0)
            p[:, 0] = wx * (1.0 - w + w * sx_out) - n_px
            p[:, 2] = wz * (1.0 - w) + (z_ctr + (wz - z_ctr) * sz_out) * w - n_pz
            return p

        scene.nodes[node] = [p.deform(_tuck) for p in parts]
        scene.set_node_transform(node, translation=(new_tx, ty, new_tz), rotation=xf.rotation, scale=xf.scale)


__all__ = ["add_thief_attack_animation", "add_thief_whip_meshes", "compact_thief_legs", "scale_thief_head_assembly"]
