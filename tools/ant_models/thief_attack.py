"""Authentic 1998 Thief Ant `atat` (720 ms) bandana-off bullwhip crack `"Attack"` clip & meshes.

Split from `ant_thief.py` to stay well under per-file size limits and keep imports pure-stdlib safe.
Choreographs all 12 original `atat301`/`atat901` frames (`[60, 40, 80, 60, 40, 40, 80, 80, 60, 80, 60, 40]` ms):
- f0..f2 (0.00..0.10 s): right arm reaches up and peels the golden bandana off the brow
- f3..f5 (0.18..0.28 s): worn bandana hides (`1e-4`), exposing Thief's bare green head while
  `thief_whip_lasso` whips in a wide golden cloth crescent around his head into a loaded windup
- f6..f7 (0.32..0.40 s): forward lunge & explosive `thief_whip_crack` S-curved cloth whip snapping
  straight forward out of his right hand, then curling back on recoil
- f8 (0.48 s): `thief_whip_lasso` whips cleanly back around the brow
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
) -> MeshPart:
    """Build a closed, outward-verified glossy golden-satin cloth ribbon (`gear_gold`)."""
    pts = catmull_rom_spline([np.asarray(p, dtype=np.float64) for p in control_pts], samples_per_seg=6, closed=False)
    n = len(pts)
    u = np.linspace(0.0, 1.0, n)
    w_arr = np.interp(u, np.linspace(0.0, 1.0, len(half_widths)), half_widths)
    t_arr = np.interp(u, np.linspace(0.0, 1.0, len(half_thicknesses)), half_thicknesses)
    twists = np.radians(np.linspace(twist_deg[0], twist_deg[1], n))
    tangents = np.array([_norm(pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]) for i in range(n)])

    segs = 12
    angs = np.linspace(0.0, 2.0 * math.pi, segs, endpoint=False)
    cu = np.sign(np.cos(angs)) * (np.abs(np.cos(angs)) ** 0.72)
    su = np.sign(np.sin(angs)) * (np.abs(np.sin(angs)) ** 0.72) + 0.12 * np.cos(2.0 * angs) * np.sin(angs)
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
    return MeshPart(name, "gear_gold", positions, recompute_smooth_normals(positions, indices), indices).ensure_outward()


def add_thief_whip_meshes(scene: SceneBuilder, anchors: dict, *, head_scale: float = 1.0) -> None:
    """Attach hidden (`1e-4` inside opaque thorax) golden cloth lasso & bullwhip crack meshes."""
    tc = np.asarray(anchors["thorax_center"], dtype=np.float64)
    nc = np.asarray(anchors["neck_center"], dtype=np.float64)
    hc = np.asarray(anchors["head_center"], dtype=np.float64)
    hs = float(head_scale)
    hy_rel = float((nc[1] - tc[1]) + hs * (hc[1] - nc[1])) - 0.006
    hz_rel = float((nc[2] - tc[2]) + hs * (hc[2] - nc[2]))
    rs = 0.5 * (1.0 + hs)  # keep lasso bold & proportional around the smaller head

    # 1. Sweeping 240-deg crescent cloth lasso wrapping dramatically around Thief's bare head (`f3..f5, f8`).
    lasso_pts = [
        (-0.10 * rs, hy_rel - 0.10 * rs, hz_rel + 0.13 * rs),
        (-0.26 * rs, hy_rel - 0.03 * rs, hz_rel + 0.11 * rs),
        (-0.30 * rs, hy_rel + 0.10 * rs, hz_rel - 0.04 * rs),
        (-0.14 * rs, hy_rel + 0.20 * rs, hz_rel - 0.23 * rs),
        (+0.14 * rs, hy_rel + 0.18 * rs, hz_rel - 0.22 * rs),
        (+0.29 * rs, hy_rel + 0.06 * rs, hz_rel - 0.03 * rs),
        (+0.23 * rs, hy_rel - 0.06 * rs, hz_rel + 0.14 * rs),
    ]
    lasso = _cloth_whip_ribbon(
        lasso_pts,
        half_widths=[0.017, 0.033, 0.044, 0.040, 0.038, 0.035, 0.015],
        half_thicknesses=[0.008, 0.009, 0.009, 0.008, 0.008, 0.008, 0.005],
        face_hint=(0.0, 0.88, 0.48),
        twist_deg=(-20.0, 42.0),
        name="thief_whip_lasso_ribbon",
    )
    scene.add("thief_whip_lasso", lasso, parent="upper_body")
    scene.set_node_transform("thief_whip_lasso", translation=(0.0, 0.0, 0.0), scale=_HIDE_SCALE)

    # 2. Snapping S-curved golden cloth bullwhip extending +0.46 Z forward from right hand (`f6..f7`).
    #    Angled upward in upper_body local space to counterbalance Thief's +0.38 rad lunge lean so
    #    the bullwhip cracks dead horizontal at waist/hand height cleanly above Y = 0.0!
    crack_pts = [
        (-0.075, +0.08, +0.12),
        (-0.048, +0.12, +0.23),
        (+0.044, +0.16, +0.34),
        (-0.046, +0.20, +0.45),
        (+0.024, +0.23, +0.54),
        (+0.004, +0.26, +0.60),
    ]
    crack = _cloth_whip_ribbon(
        crack_pts,
        half_widths=[0.018, 0.034, 0.046, 0.042, 0.028, 0.010],
        half_thicknesses=[0.008, 0.009, 0.009, 0.008, 0.006, 0.004],
        face_hint=(0.24, 0.96, 0.12),
        twist_deg=(-15.0, 85.0),
        name="thief_whip_crack_ribbon",
    )
    scene.add("thief_whip_crack", crack, parent="upper_body")
    scene.set_node_transform("thief_whip_crack", translation=(0.0, 0.0, 0.0), scale=_HIDE_SCALE)


def add_thief_attack_animation(scene: SceneBuilder, gaze_angle: float) -> None:
    """Attach authentic 1998 `atat` 0.72 s 12-frame bandana-off bullwhip crack `"Attack"` clip."""
    # Exact cumulative timestamps for `[60, 40, 80, 60, 40, 40, 80, 80, 60, 80, 60, 40]` ms = 720 ms.
    times = [0.00, 0.06, 0.10, 0.18, 0.24, 0.28, 0.32, 0.40, 0.48, 0.54, 0.62, 0.68, 0.72]

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
        (-0.010, +0.010, -0.016), # f3 0.18: rip off -> lasso whirl
        (+0.014, +0.014, -0.020), # f4 0.24: overhead lasso spin
        (+0.022, +0.004, -0.010), # f5 0.28: wide side windup
        (-0.008, -0.006, +0.062), # f6 0.32: EXPLOSIVE FORWARD WHIP CRACK
        (-0.004, +0.002, +0.036), # f7 0.40: whip-tip snap recoil
        (+0.008, +0.010, +0.004), # f8 0.48: lasso sweeps back around head
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
                (-0.08, +0.06, -0.04),
                (-0.12, +0.18, -0.10),
                (-0.06, +0.26, -0.12),
                (+0.22, -0.18, +0.06),
                (+0.12, -0.08, +0.02),
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
                (-0.06, -0.04, +0.04),
                (-0.08, -0.12, +0.08),
                (-0.04, -0.18, +0.08),
                (-0.18, +0.14, -0.04),
                (-0.12, +0.06, -0.02),
                (-0.04, -0.06, +0.04),
                (+0.06, +0.06, -0.06),
                (+0.02, +0.02, -0.02),
                (+0.00, +0.00, +0.00),
                (+0.02, +0.04, -0.02),
            ],
        )
    )
    # Eyes lock fierce straight ahead during the bare-headed bullwhip crack (f3..f8), then dart back left!
    gaze_factors = [-1.0, -0.8, -0.4, 0.0, 0.0, 0.0, 0.0, 0.0, -0.2, -0.7, -0.95, -1.0, -1.0]
    for side in ("left", "right"):
        ball = f"eye_ball_{side}"
        if ball in scene.nodes:
            qs = [quat_from_axis_angle((0.0, 1.0, 0.0), g * gaze_angle) for g in gaze_factors]
            channels.append(AnimationChannel(ball, "rotation", list(times), qs))

    # Right arm grabs bandana (f0-f2), whips lasso overhead (f3-f5), snaps bullwhip forward (f6-f7), re-ties (f8-f10).
    if "arm_right" in scene.nodes:
        channels.append(
            _rot_track(
                "arm_right",
                [
                    (-0.24, +0.10, -0.18),
                    (-0.88, +0.34, -0.48),
                    (-1.12, +0.44, -0.62),
                    (-1.26, +0.16, -0.96),
                    (-1.18, -0.26, -1.10),
                    (-0.78, -0.46, -0.92),
                    (-0.18, +0.38, -0.22),
                    (-0.36, +0.26, -0.34),
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
                    (-0.16, -0.12, -0.20),
                    (-0.12, -0.20, -0.14),
                    (+0.22, +0.18, +0.06),
                    (-0.18, +0.14, +0.10),
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
                    (-0.36, -0.28, +0.56),
                    (-0.46, -0.38, +0.64),
                    (-0.34, -0.28, +0.50),
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

    # Razor-sharp STEP visibility swaps between worn bandana, overhead lasso, and forward bullwhip crack!
    if "bandana_worn_group" in scene.nodes:
        worn_vis = [
            _SHOW_SCALE, _SHOW_SCALE, _SHOW_SCALE,
            _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE,
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
                    (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
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
            _SHOW_SCALE, _SHOW_SCALE, _SHOW_SCALE,
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
                    (+0.18, +0.06, -0.12),   # f3: wide halo around both sides of bare head
                    (+0.28, -0.68, +0.42),   # f4: fast overhead whirl
                    (+0.46, -1.24, +0.62),   # f5: sweeping low-left windup arc
                    (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                    (+0.14, +0.42, -0.24),   # f8: whips cleanly back around brow
                    (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                ],
            )
        )

    if "thief_whip_crack" in scene.nodes:
        crack_vis = [
            _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE,
            (1.00, 1.00, 1.02),      # f6: full explosive forward whip crack!
            (1.06, 1.05, 0.84),      # f7: curled snapping tip rebound!
            _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE, _HIDE_SCALE,
        ]
        channels.append(AnimationChannel("thief_whip_crack", "scale", list(times), crack_vis, interpolation="STEP"))
        channels.append(
            _rot_track(
                "thief_whip_crack",
                [
                    (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                    (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                    (-0.06, +0.04, +0.06),   # f6: straight horizontal forward crack!
                    (-0.14, +0.26, -0.34),   # f7: upward snap-back curl rebound!
                    (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                ],
            )
        )
        ct0 = _rest_t("thief_whip_crack")
        crack_dzs = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, +0.020, -0.045, 0.0, 0.0, 0.0, 0.0, 0.0]
        channels.append(
            AnimationChannel(
                "thief_whip_crack",
                "translation",
                list(times),
                [(ct0[0], ct0[1], ct0[2] + dz) for dz in crack_dzs],
            )
        )

    # Lively antenna & gaster inertia across the whip crack!
    for ant_n, s in (("antenna_left", +1.0), ("antenna_right", -1.0)):
        if ant_n in scene.nodes and scene.get_node(ant_n):
            channels.append(
                _rot_track(
                    ant_n,
                    [
                        (0.0, 0.0, 0.0), (-0.10, 0.0, s * 0.08), (-0.18, 0.0, s * 0.14),
                        (+0.14, 0.0, -s * 0.16), (+0.18, 0.0, -s * 0.22), (+0.10, 0.0, -s * 0.18),
                        (-0.28, 0.0, s * 0.14), (-0.18, 0.0, s * 0.10), (+0.12, 0.0, -s * 0.10),
                        (-0.08, 0.0, s * 0.06), (-0.02, 0.0, s * 0.02), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
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
                    (+0.18, 0.0, 0.0), (+0.10, 0.0, 0.0), (-0.04, 0.0, 0.0),
                    (+0.04, 0.0, 0.0), (+0.02, 0.0, 0.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
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
