"""Authentic 1998 `"Hit"` (`*gh`) knockback & recovery clips for Thief, Bomber, and Swimmer.

Split into a dedicated module to keep `ant_thief.py`, `ant_bomber.py`, and `ant_swimmer.py`
well under per-file line limits while staying pure-stdlib safe on import (CI without numpy).

Exact 1998 `*gh` cadences (from `docs/chd_table4_animations.json`):
- Thief   `atgh`: 0.95 s, 9 frames `[100, 100, 100, 100, 100, 100, 100, 100, 150]` ms
  (high airborne back-flip knockback -> thump onto back -> agile ninja forward roll)
- Bomber  `abgh`: 1.00 s, 9 frames `[100, 100, 100, 100, 120, 120, 120, 120, 120]` ms
  (airborne back-hop -> heavy 3-bomb backpack whiplash forward face-dive -> upright rebound)
- Swimmer `asgh`: 1.05 s, 9 frames `[100, 100, 100, 100, 150, 150, 150, 100, 100]` ms
  (flung back onto scuba tank -> rock heavily forward onto flippers -> snap upright)
"""
from __future__ import annotations

import math
from typing import Dict, List, Optional, Sequence, Tuple

try:
    import numpy as np
except ImportError:  # pragma: no cover - pure-stdlib import safety for CI
    np = None  # type: ignore

if np is not None:
    try:
        from .base_ant import sync_leg_flex_to_clip
        from .mesh_core import (
            AnimationChannel,
            AnimationClip,
            SceneBuilder,
            quat_from_euler_xyz,
            quat_mul,
        )
    except ImportError:
        from base_ant import sync_leg_flex_to_clip  # type: ignore
        from mesh_core import (  # type: ignore
            AnimationChannel,
            AnimationClip,
            SceneBuilder,
            quat_from_euler_xyz,
            quat_mul,
        )

Vec3 = Tuple[float, float, float]
Quat = Tuple[float, float, float, float]


def _ensure_hit_root(scene: SceneBuilder) -> str:
    """Wrap all current top-level nodes under an identity `"root"` transform group.

    Because `"root"` sits at rest identity `(0,0,0) / (0,0,0,1) / (1,1,1)` and is never keyed
    by `Idle`, `Blink`, or `Attack`, those clips remain bit-for-bit unaffected while `"Hit"`
    can tumble the entire ant (legs, upper_body, and Swimmer flippers) cleanly off the floor.
    """
    if "root" not in scene.nodes:
        top_nodes = [n for n in list(scene.nodes.keys()) if scene.get_node_parent(n) is None]
        scene.nodes["root"] = []
        scene.set_node_transform(
            "root",
            translation=(0.0, 0.0, 0.0),
            rotation=(0.0, 0.0, 0.0, 1.0),
            scale=(1.0, 1.0, 1.0),
        )
        for n in top_nodes:
            scene.set_node_parent(n, "root")
    return "root"


def _rest_t(scene: SceneBuilder, node: str) -> Vec3:
    xf = scene.get_node_transform(node)
    return xf.translation if xf is not None else (0.0, 0.0, 0.0)


def _rest_r(scene: SceneBuilder, node: str) -> Quat:
    xf = scene.get_node_transform(node)
    return xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)


def _quat_track(r0: Quat, deltas: Sequence[Vec3]) -> List[Quat]:
    """Build a hemisphere-continuous closed-loop quaternion track from Euler `(rx, ry, rz)` deltas."""
    out: List[Quat] = []
    for rx, ry, rz in deltas:
        q = quat_mul(r0, quat_from_euler_xyz(rx=rx, ry=ry, rz=rz))
        if out and sum(a * b for a, b in zip(out[-1], q)) < 0.0:
            q = (-q[0], -q[1], -q[2], -q[3])
        out.append(q)
    if len(out) >= 2:
        q_end = out[0]
        if sum(a * b for a, b in zip(out[-2], q_end)) < 0.0:
            q_end = (-q_end[0], -q_end[1], -q_end[2], -q_end[3])
        out[-1] = q_end
    return out


def _quat_to_mat3(q: Quat) -> np.ndarray:
    x, y, z, w = q
    return np.array(
        [
            [1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w), 2.0 * (x * z + y * w)],
            [2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w)],
            [2.0 * (x * z - y * w), 2.0 * (y * z + x * w), 1.0 - 2.0 * (x * x + y * y)],
        ],
        dtype=np.float64,
    )


def _slerp(q0: Quat, q1: Quat, u: float) -> Quat:
    a = np.asarray(q0, dtype=np.float64)
    b = np.asarray(q1, dtype=np.float64)
    dot = float(np.dot(a, b))
    if dot < 0.0:
        b = -b
        dot = -dot
    if dot > 0.9995:
        r = a + u * (b - a)
        r /= max(1e-12, float(np.linalg.norm(r)))
        return (float(r[0]), float(r[1]), float(r[2]), float(r[3]))
    th = math.acos(min(1.0, max(-1.0, dot)))
    r = (math.sin((1.0 - u) * th) * a + math.sin(u * th) * b) / math.sin(th)
    r /= max(1e-12, float(np.linalg.norm(r)))
    return (float(r[0]), float(r[1]), float(r[2]), float(r[3]))


def _sample_channel(ch: AnimationChannel, t: float):
    ts = ch.times
    if t <= ts[0]:
        return ch.values[0]
    if t >= ts[-1]:
        return ch.values[-1]
    lo = 0
    hi = len(ts) - 1
    while lo + 1 < hi:
        mid = (lo + hi) // 2
        if ts[mid] <= t:
            lo = mid
        else:
            hi = mid
    span = ts[lo + 1] - ts[lo]
    u = (t - ts[lo]) / span if span > 1e-9 else 0.0
    if ch.path == "rotation":
        return _slerp(ch.values[lo], ch.values[lo + 1], u)
    a = ch.values[lo]
    b = ch.values[lo + 1]
    return (a[0] + u * (b[0] - a[0]), a[1] + u * (b[1] - a[1]), a[2] + u * (b[2] - a[2]))


def _visible_mesh_samples(scene: SceneBuilder) -> Dict[str, np.ndarray]:
    """Precompute compact vertex arrays for every visible (non-hidden-FX) mesh node."""
    out: Dict[str, np.ndarray] = {}
    for node, parts in scene.nodes.items():
        if not parts:
            continue
        xf = scene.get_node_transform(node)
        if xf is not None and max(abs(c) for c in xf.scale) < 0.01:
            continue
        pts = np.vstack([p.positions for p in parts]).astype(np.float64)
        # Keep exact extreme vertices + dense deterministic stride for fast exact min-Y checks.
        if len(pts) > 320:
            step = max(1, len(pts) // 240)
            idx = set(range(0, len(pts), step))
            for ax in range(3):
                idx.add(int(np.argmin(pts[:, ax])))
                idx.add(int(np.argmax(pts[:, ax])))
            pts = pts[sorted(idx)]
        out[node] = pts
    return out


def _eval_min_y_at(
    scene: SceneBuilder,
    channels_by_node: Dict[str, Dict[str, AnimationChannel]],
    mesh_samples: Dict[str, np.ndarray],
    t: float,
    root_override: Optional[Tuple[Vec3, Quat]] = None,
) -> float:
    """Evaluate exact world-space `min_y` across all visible meshes at timestamp `t`."""
    wmats: Dict[str, np.ndarray] = {}

    def _wmat(n: str) -> np.ndarray:
        if n in wmats:
            return wmats[n]
        xf = scene.get_node_transform(n)
        tr = xf.translation if xf is not None else (0.0, 0.0, 0.0)
        rt = xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)
        sc = xf.scale if xf is not None else (1.0, 1.0, 1.0)
        if n == "root" and root_override is not None:
            tr, rt = root_override
        else:
            ch_map = channels_by_node.get(n)
            if ch_map is not None:
                if "translation" in ch_map:
                    tr = _sample_channel(ch_map["translation"], t)
                if "rotation" in ch_map:
                    rt = _sample_channel(ch_map["rotation"], t)
                if "scale" in ch_map:
                    sc = _sample_channel(ch_map["scale"], t)
        mat = np.eye(4, dtype=np.float64)
        mat[:3, :3] = _quat_to_mat3(rt) * np.asarray(sc, dtype=np.float64)[None, :]
        mat[:3, 3] = tr
        par = scene.get_node_parent(n)
        res = (_wmat(par) @ mat) if par is not None else mat
        wmats[n] = res
        return res

    min_y = 1e9
    for node, pts in mesh_samples.items():
        m = _wmat(node)
        y_vals = pts @ m[1, :3] + m[1, 3]
        local_min = float(y_vals.min())
        if local_min < min_y:
            min_y = local_min
    return min_y


def _solve_root_channels(
    scene: SceneBuilder,
    clip: AnimationClip,
    times: Sequence[float],
    root_eulers: Sequence[Vec3],
    root_z_com: Sequence[float],
    min_air_y: Sequence[float],
    *,
    com_y: float,
) -> None:
    """Attach exact floor-safe, COM-pivoted `"root"` translation & rotation channels to `clip`.

    1. Rotates cleanly around the torso center of mass `(0, com_y, 0)` so big back-flips stay
       centered in Z with controlled knockback (`root_z_com`).
    2. Solves exact `root_ty` at every keyframe so visible geometry hits `min_air_y[k]` (0.000
       when planted, `>= 0.003` when thumping on back, exact hop height when airborne).
    3. Subdivides any segment whose interpolated SLERP + linear translation sags below `Y = 0.0`
       so every dense millisecond sample stays strictly `min_y >= 0.000`.
    """
    ch_by_node: Dict[str, Dict[str, AnimationChannel]] = {}
    for ch in clip.channels:
        if ch.target_node != "root":
            ch_by_node.setdefault(ch.target_node, {})[ch.path] = ch

    mesh_samples = _visible_mesh_samples(scene)
    r_vals = _quat_track((0.0, 0.0, 0.0, 1.0), root_eulers)
    t_list = [float(x) for x in times]
    z_list = [float(z) for z in root_z_com]
    air_list = [float(y) for y in min_air_y]

    # Densify any fast root rotation segment (> 0.24 rad) so SLERP vs linear translation stays crisp
    # and back-thump frames sit cleanly on Y = 0 without extra chord-sag lift.
    dense_t: List[float] = [t_list[0]]
    dense_r: List[Quat] = [r_vals[0]]
    dense_z: List[float] = [z_list[0]]
    dense_air: List[float] = [air_list[0]]
    for k in range(len(t_list) - 1):
        dot = min(1.0, abs(sum(a * b for a, b in zip(r_vals[k], r_vals[k + 1]))))
        ang = 2.0 * math.acos(dot)
        n_sub = max(1, int(math.ceil(ang / 0.24)))
        for s_i in range(1, n_sub):
            u = s_i / n_sub
            dense_t.append(round(t_list[k] + u * (t_list[k + 1] - t_list[k]), 5))
            dense_r.append(_slerp(r_vals[k], r_vals[k + 1], u))
            dense_z.append(z_list[k] + u * (z_list[k + 1] - z_list[k]))
            dense_air.append(air_list[k] + u * (air_list[k + 1] - air_list[k]))
        dense_t.append(t_list[k + 1])
        dense_r.append(r_vals[k + 1])
        dense_z.append(z_list[k + 1])
        dense_air.append(air_list[k + 1])

    # Re-enforce hemisphere continuity & exact loop closure on dense root rotations.
    for i in range(1, len(dense_r)):
        if sum(a * b for a, b in zip(dense_r[i - 1], dense_r[i])) < 0.0:
            q = dense_r[i]
            dense_r[i] = (-q[0], -q[1], -q[2], -q[3])
    q_end = dense_r[0]
    if len(dense_r) >= 2 and sum(a * b for a, b in zip(dense_r[-2], q_end)) < 0.0:
        q_end = (-q_end[0], -q_end[1], -q_end[2], -q_end[3])
    dense_r[-1] = q_end

    def _solve_t(t_val: float, q_val: Quat, z_des: float, air_des: float) -> Vec3:
        is_identity_r = abs(q_val[0]) + abs(q_val[1]) + abs(q_val[2]) < 1e-6
        if is_identity_r and abs(z_des) < 1e-6 and air_des <= 1e-6:
            min_y_0 = _eval_min_y_at(scene, ch_by_node, mesh_samples, t_val, ((0.0, 0.0, 0.0), q_val))
            return (0.0, max(0.0, -min_y_0), 0.0)
        rot_m = _quat_to_mat3(q_val)
        com_rot = rot_m @ np.array([0.0, com_y, 0.0], dtype=np.float64)
        tz = z_des - float(com_rot[2])
        min_y_0 = _eval_min_y_at(scene, ch_by_node, mesh_samples, t_val, ((0.0, 0.0, tz), q_val))
        ty = air_des - min_y_0
        return (0.0, float(ty), float(tz))

    dense_pos: List[Vec3] = [
        _solve_t(dense_t[i], dense_r[i], dense_z[i], dense_air[i]) for i in range(len(dense_t))
    ]
    dense_pos[-1] = dense_pos[0]

    # Second pass: verify dense mid-interval samples never dip below Y = 0.0 due to SLERP chord sag.
    root_t_ch = AnimationChannel("root", "translation", list(dense_t), list(dense_pos))
    root_r_ch = AnimationChannel("root", "rotation", list(dense_t), list(dense_r))
    ch_by_node["root"] = {"translation": root_t_ch, "rotation": root_r_ch}
    for _ in range(3):
        lifted = False
        for i in range(len(dense_t) - 1):
            for sub in (0.25, 0.50, 0.75):
                tm = dense_t[i] + sub * (dense_t[i + 1] - dense_t[i])
                my = _eval_min_y_at(scene, ch_by_node, mesh_samples, tm)
                if my < -0.0005:
                    lift = -my + 0.0015
                    if i > 0:
                        px, py, pz = dense_pos[i]
                        dense_pos[i] = (px, py + lift, pz)
                    if i + 1 < len(dense_pos) - 1:
                        px, py, pz = dense_pos[i + 1]
                        dense_pos[i + 1] = (px, py + lift, pz)
                    lifted = True
        if not lifted:
            break
        dense_pos[-1] = dense_pos[0]
        root_t_ch.values = list(dense_pos)

    clip.channels = [ch for ch in clip.channels if ch.target_node != "root"]
    clip.channels.insert(0, AnimationChannel("root", "rotation", list(dense_t), list(dense_r)))
    clip.channels.insert(0, AnimationChannel("root", "translation", list(dense_t), list(dense_pos)))


def add_thief_hit_animation(scene: SceneBuilder) -> AnimationClip:
    """Attach Thief's authentic 1998 0.95s airborne back-flip & ninja roll (`atgh`) `"Hit"` clip."""
    _ensure_hit_root(scene)
    # Exact 1998 atgh 9-frame cadence [100, 100, 100, 100, 100, 100, 100, 100, 150] ms + wrap at 0.95 s,
    # with crisp impact onset (0.04 s) and smooth kip-up midpoint (0.35 s).
    times = [0.00, 0.04, 0.10, 0.20, 0.30, 0.35, 0.40, 0.50, 0.60, 0.70, 0.80, 0.95]

    # Root tumble: blasted high airborne (-1.52 rad, +0.18 Y) -> flat back thump (-1.66 rad, floor)
    # -> agile forward kip-up onto planted feet (0.40..0.95 s).
    root_eulers = [
        (0.00, 0.0, 0.0),    # 0.00 rest wrap
        (-0.68, 0.0, 0.0),   # 0.04 f0 impact launch
        (-1.52, 0.0, 0.0),   # 0.10 f1 high airborne back-flip peak
        (-1.66, 0.0, 0.0),   # 0.20 f2 flat thump onto back on ground
        (-1.38, 0.0, 0.0),   # 0.30 f3 on back, curling chin for kip-up
        (-0.56, 0.0, 0.0),   # 0.35 kip-up roll mid-arc
        (0.00, 0.0, 0.0),    # 0.40 f4 lands cleanly onto feet
        (0.00, 0.0, 0.0),    # 0.50 f5 agile forward roll crouch
        (0.00, 0.0, 0.0),    # 0.60 f6 deepest forward roll crouch
        (0.00, 0.0, 0.0),    # 0.70 f7 springing upright
        (0.00, 0.0, 0.0),    # 0.80 f8 poised settle
        (0.00, 0.0, 0.0),    # 0.95 exact rest wrap
    ]
    root_z_com = [0.00, -0.05, -0.10, -0.11, -0.09, -0.04, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00]
    min_air_y = [0.000, 0.090, 0.180, 0.003, 0.003, 0.032, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000]

    # Upper body stays locked to legs while airborne (0.00..0.35 s), then executes Thief's low
    # crouched ninja forward roll (+0.85 rad total forward lean) on planted feet (0.40..0.95 s).
    bx, by, bz = _rest_t(scene, "upper_body")
    ub_d = [
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, -0.010, +0.006),
        (0.0, -0.026, +0.014),
        (0.0, -0.044, +0.024),
        (0.0, -0.052, +0.028),
        (0.0, -0.018, +0.010),
        (0.0, -0.004, +0.002),
        (0.0, 0.000, 0.000),
    ]
    ub_rx = [0.00, 0.00, 0.00, 0.00, 0.00, +0.16, +0.44, +0.72, +0.84, +0.28, +0.06, 0.00]
    head_rx = [0.00, -0.14, -0.18, -0.10, +0.34, +0.26, +0.18, +0.14, +0.16, +0.04, -0.02, 0.00]
    gaster_rx = [0.00, -0.12, -0.22, -0.16, +0.24, +0.10, -0.22, -0.36, -0.42, -0.14, -0.03, 0.00]

    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), [(bx + dx, by + dy, bz + dz) for dx, dy, dz in ub_d]),
        AnimationChannel("upper_body", "rotation", list(times), _quat_track(_rest_r(scene, "upper_body"), [(rx, 0.0, 0.0) for rx in ub_rx])),
        AnimationChannel("head_group", "rotation", list(times), _quat_track(_rest_r(scene, "head_group"), [(rx, 0.0, 0.0) for rx in head_rx])),
    ]
    if "gaster_group" in scene.nodes:
        channels.append(
            AnimationChannel("gaster_group", "rotation", list(times), _quat_track(_rest_r(scene, "gaster_group"), [(rx, 0.0, 0.0) for rx in gaster_rx]))
        )

    # Arms fling wide in airborne shock (f0-f2), tuck forward to drive kip-up (f3-f4), then brace low-out on roll.
    for side, tag in ((+1.0, "left"), (-1.0, "right")):
        arm_n = f"arm_{tag}"
        fore_n = f"forearm_{tag}"
        if arm_n in scene.nodes:
            a_del = [
                (0.00, 0.00, 0.00),
                (-0.42, -side * 0.30, +side * 0.52),
                (-0.58, -side * 0.42, +side * 0.68),
                (-0.36, -side * 0.34, +side * 0.56),
                (+0.22, +side * 0.16, +side * 0.30),
                (+0.16, +side * 0.10, +side * 0.26),
                (-0.24, -side * 0.20, +side * 0.38),
                (-0.46, -side * 0.34, +side * 0.46),
                (-0.54, -side * 0.40, +side * 0.52),
                (-0.18, -side * 0.14, +side * 0.20),
                (-0.04, -side * 0.04, +side * 0.06),
                (0.00, 0.00, 0.00),
            ]
            channels.append(AnimationChannel(arm_n, "rotation", list(times), _quat_track(_rest_r(scene, arm_n), a_del)))
        if fore_n in scene.nodes:
            f_del = [
                (0.00, 0.00, 0.00),
                (-0.28, -side * 0.14, +side * 0.18),
                (-0.36, -side * 0.20, +side * 0.24),
                (-0.24, -side * 0.16, +side * 0.18),
                (+0.20, +side * 0.12, +side * 0.10),
                (+0.12, +side * 0.08, +side * 0.08),
                (-0.14, +side * 0.10, +side * 0.12),
                (-0.26, +side * 0.16, +side * 0.16),
                (-0.30, +side * 0.20, +side * 0.18),
                (-0.10, +side * 0.06, +side * 0.06),
                (-0.02, +side * 0.02, +side * 0.02),
                (0.00, 0.00, 0.00),
            ]
            channels.append(AnimationChannel(fore_n, "rotation", list(times), _quat_track(_rest_r(scene, fore_n), f_del)))

    # Dynamic bandana tail whip: flares up/back during airborne flip, then whips forward over brow on roll!
    for node_n, amp in (("bandana_tails", 1.0), ("bandana_tail_lower", 1.22)):
        if node_n in scene.nodes:
            b_del = [
                (0.00, 0.00, 0.00),
                (-0.28 * amp, -0.18 * amp, +0.34 * amp),
                (-0.44 * amp, -0.30 * amp, +0.52 * amp),
                (-0.32 * amp, -0.22 * amp, +0.38 * amp),
                (+0.16 * amp, +0.14 * amp, -0.12 * amp),
                (+0.28 * amp, +0.26 * amp, -0.22 * amp),
                (+0.36 * amp, +0.34 * amp, -0.26 * amp),
                (+0.42 * amp, +0.40 * amp, -0.30 * amp),
                (+0.38 * amp, +0.34 * amp, -0.24 * amp),
                (+0.14 * amp, +0.12 * amp, -0.10 * amp),
                (+0.04 * amp, +0.03 * amp, -0.03 * amp),
                (0.00, 0.00, 0.00),
            ]
            channels.append(AnimationChannel(node_n, "rotation", list(times), _quat_track(_rest_r(scene, node_n), b_del)))

    for node_n, side in (("antenna_left", +1.0), ("antenna_right", -1.0)):
        if node_n in scene.nodes and scene.get_node(node_n):
            ant_del = [
                (0.00, 0.0, 0.0),
                (+0.22, 0.0, +side * 0.12),
                (+0.34, 0.0, +side * 0.18),
                (+0.18, 0.0, +side * 0.10),
                (-0.24, 0.0, -side * 0.10),
                (-0.28, 0.0, -side * 0.12),
                (-0.22, 0.0, -side * 0.08),
                (-0.18, 0.0, -side * 0.06),
                (-0.16, 0.0, -side * 0.05),
                (+0.12, 0.0, +side * 0.04),
                (+0.04, 0.0, +side * 0.02),
                (0.00, 0.0, 0.0),
            ]
            channels.append(AnimationChannel(node_n, "rotation", list(times), _quat_track(_rest_r(scene, node_n), ant_del)))

    clip = scene.add_animation("Hit", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Hit", knee_bow=1.05, splay=0.25, swivel=0.0)
    _solve_root_channels(scene, clip, times, root_eulers, root_z_com, min_air_y, com_y=0.34)
    return clip


def add_bomber_hit_animation(scene: SceneBuilder) -> AnimationClip:
    """Attach Bomber's authentic 1998 1.00s hop-back & 3-bomb backpack whiplash bow (`abgh`) `"Hit"` clip."""
    _ensure_hit_root(scene)
    # Exact 1998 abgh 9-frame cadence [100, 100, 100, 100, 120, 120, 120, 120, 120] ms + wrap at 1.00 s.
    times = [0.00, 0.04, 0.12, 0.20, 0.30, 0.40, 0.52, 0.64, 0.76, 0.88, 1.00]

    # Root hops up & backward (f0-f2), then plants squarely on feet for heavy backpack forward whiplash (f3-f8).
    root_eulers = [
        (0.00, 0.0, 0.0),    # 0.00 rest wrap
        (-0.46, 0.0, 0.0),   # 0.04 f0 knocked back off heels
        (-0.82, 0.0, 0.0),   # 0.12 f1 peak airborne back-hop (+0.13 Y, goggles skyward)
        (-0.42, 0.0, 0.0),   # 0.20 f2 dropping back onto feet
        (0.00, 0.0, 0.0),    # 0.30 f3 feet planted, heavy backpack tips forward
        (0.00, 0.0, 0.0),    # 0.40 f4 deep backpack whiplash bow
        (0.00, 0.0, 0.0),    # 0.52 f5 deepest face-dive peak (3 pink bombs tower overhead!)
        (0.00, 0.0, 0.0),    # 0.64 f6 sustained heavy bow
        (0.00, 0.0, 0.0),    # 0.76 f7 rebounding upright
        (0.00, 0.0, 0.0),    # 0.88 f8 elastic settle
        (0.00, 0.0, 0.0),    # 1.00 exact rest wrap
    ]
    root_z_com = [0.00, -0.04, -0.08, -0.04, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00]
    min_air_y = [0.000, 0.065, 0.130, 0.032, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000]

    # Heavy 3-bomb backpack whips upper_body sharply forward (+1.28..+1.38 rad total pitch with head)
    # on planted feet while keeping brass goggles cleanly above Y = 0.0 and legs tracking hips!
    bx, by, bz = _rest_t(scene, "upper_body")
    ub_d = [
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, -0.010, +0.016),
        (0.0, -0.012, +0.036),
        (0.0, -0.010, +0.042),
        (0.0, -0.010, +0.034),
        (0.0, -0.006, +0.014),
        (0.0, -0.002, +0.003),
        (0.0, 0.000, 0.000),
    ]
    ub_rx = [0.00, 0.00, 0.00, 0.00, +0.42, +1.18, +1.28, +1.12, +0.44, +0.10, 0.00]
    head_rx = [0.00, -0.10, -0.14, -0.06, +0.08, +0.12, +0.10, +0.08, -0.04, -0.02, 0.00]
    gaster_rx = [0.00, -0.08, -0.14, -0.06, -0.20, -0.46, -0.52, -0.44, -0.18, -0.04, 0.00]

    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), [(bx + dx, by + dy, bz + dz) for dx, dy, dz in ub_d]),
        AnimationChannel("upper_body", "rotation", list(times), _quat_track(_rest_r(scene, "upper_body"), [(rx, 0.0, 0.0) for rx in ub_rx])),
        AnimationChannel("head_group", "rotation", list(times), _quat_track(_rest_r(scene, "head_group"), [(rx, 0.0, 0.0) for rx in head_rx])),
    ]
    if "gaster_group" in scene.nodes:
        channels.append(
            AnimationChannel("gaster_group", "rotation", list(times), _quat_track(_rest_r(scene, "gaster_group"), [(rx, 0.0, 0.0) for rx in gaster_rx]))
        )

    # Arms fling wide on airborne knockback (f0-f2), then sweep cleanly wide & rearward above legs during bow.
    for side, tag in ((+1.0, "left"), (-1.0, "right")):
        arm_n = f"arm_{tag}"
        fore_n = f"forearm_{tag}"
        if arm_n in scene.nodes:
            a_del = [
                (0.00, 0.00, 0.00),
                (-0.34, +side * 0.28, +side * 0.74),
                (-0.46, +side * 0.36, +side * 0.94),
                (-0.26, +side * 0.20, +side * 0.58),
                (-0.46, -side * 0.36, +side * 0.78),
                (-0.92, -side * 0.68, +side * 0.98),
                (-1.02, -side * 0.74, +side * 1.04),
                (-0.86, -side * 0.64, +side * 0.94),
                (-0.34, -side * 0.28, +side * 0.44),
                (-0.08, -side * 0.06, +side * 0.10),
                (0.00, 0.00, 0.00),
            ]
            channels.append(AnimationChannel(arm_n, "rotation", list(times), _quat_track(_rest_r(scene, arm_n), a_del)))
        if fore_n in scene.nodes:
            f_del = [
                (0.00, 0.00, 0.00),
                (-0.20, -side * 0.18, +side * 0.18),
                (-0.28, -side * 0.24, +side * 0.24),
                (-0.14, -side * 0.12, +side * 0.12),
                (-0.16, +side * 0.16, +side * 0.14),
                (-0.34, +side * 0.32, +side * 0.22),
                (-0.38, +side * 0.36, +side * 0.24),
                (-0.30, +side * 0.28, +side * 0.20),
                (-0.12, +side * 0.12, +side * 0.10),
                (-0.03, +side * 0.03, +side * 0.03),
                (0.00, 0.00, 0.00),
            ]
            channels.append(AnimationChannel(fore_n, "rotation", list(times), _quat_track(_rest_r(scene, fore_n), f_del)))

    for node_n, side in (("antenna_left", +1.0), ("antenna_right", -1.0)):
        if node_n in scene.nodes and scene.get_node(node_n):
            ant_del = [
                (0.00, 0.0, 0.0),
                (+0.22, 0.0, +side * 0.08),
                (+0.34, 0.0, +side * 0.14),
                (+0.16, 0.0, +side * 0.06),
                (-0.22, 0.0, -side * 0.08),
                (-0.44, 0.0, -side * 0.14),
                (-0.48, 0.0, -side * 0.16),
                (-0.36, 0.0, -side * 0.12),
                (+0.18, 0.0, +side * 0.06),
                (+0.06, 0.0, +side * 0.02),
                (0.00, 0.0, 0.0),
            ]
            channels.append(AnimationChannel(node_n, "rotation", list(times), _quat_track(_rest_r(scene, node_n), ant_del)))

    clip = scene.add_animation("Hit", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Hit", knee_bow=1.05, splay=0.25, swivel=0.0)
    _solve_root_channels(scene, clip, times, root_eulers, root_z_com, min_air_y, com_y=0.38)
    return clip


def add_swimmer_hit_animation(scene: SceneBuilder) -> AnimationClip:
    """Attach Swimmer's authentic 1998 1.05s scuba-tank knockdown & flipper recovery (`asgh`) `"Hit"` clip."""
    _ensure_hit_root(scene)
    # Exact 1998 asgh 9-frame cadence [100, 100, 100, 100, 150, 150, 150, 100, 100] ms + wrap at 1.05 s,
    # with crisp impact launch (0.04 s) and smooth rock-forward midpoint (0.35 s).
    times = [0.00, 0.04, 0.10, 0.20, 0.30, 0.35, 0.40, 0.55, 0.70, 0.85, 0.95, 1.05]

    # Flung backward onto scuba tank (-1.58 rad, mask staring skyward, flippers kicking up) ->
    # rocks heavily forward onto all 4 planted flippers (+0.95 rad) -> snaps upright!
    root_eulers = [
        (0.00, 0.0, 0.0),    # 0.00 rest wrap
        (-0.68, 0.0, 0.0),   # 0.04 f0 knocked back off flippers
        (-1.28, 0.0, 0.0),   # 0.10 f1 airborne backward launch
        (-1.58, 0.0, 0.0),   # 0.20 f2 flat back on scuba tank (mask straight skyward!)
        (-1.38, 0.0, 0.0),   # 0.30 f3 rocking forward on tank
        (-0.56, 0.0, 0.0),   # 0.35 rock-forward mid-arc onto flippers
        (0.00, 0.0, 0.0),    # 0.40 f4 planted on flippers, heavy forward lurch
        (0.00, 0.0, 0.0),    # 0.55 f5 deepest forward lurch peak
        (0.00, 0.0, 0.0),    # 0.70 f6 sustained forward lurch
        (0.00, 0.0, 0.0),    # 0.85 f7 snapping upright
        (0.00, 0.0, 0.0),    # 0.95 f8 popping upright settle
        (0.00, 0.0, 0.0),    # 1.05 exact rest wrap
    ]
    root_z_com = [0.00, -0.05, -0.09, -0.11, -0.09, -0.04, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00]
    min_air_y = [0.000, 0.050, 0.072, 0.004, 0.004, 0.024, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000]

    bx, by, bz = _rest_t(scene, "upper_body")
    ub_d = [
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, -0.008, +0.006),
        (0.0, -0.022, +0.018),
        (0.0, -0.028, +0.024),
        (0.0, -0.024, +0.020),
        (0.0, -0.008, +0.006),
        (0.0, -0.002, +0.001),
        (0.0, 0.000, 0.000),
    ]
    ub_rx = [0.00, 0.00, 0.00, 0.00, 0.00, +0.20, +0.72, +0.94, +0.82, +0.30, +0.06, 0.00]
    head_rx = [0.00, -0.10, -0.14, -0.08, +0.20, +0.22, +0.24, +0.28, +0.20, +0.06, -0.02, 0.00]
    gaster_rx = [0.00, -0.08, -0.14, -0.12, +0.16, +0.04, -0.34, -0.44, -0.38, -0.14, -0.03, 0.00]

    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), [(bx + dx, by + dy, bz + dz) for dx, dy, dz in ub_d]),
        AnimationChannel("upper_body", "rotation", list(times), _quat_track(_rest_r(scene, "upper_body"), [(rx, 0.0, 0.0) for rx in ub_rx])),
        AnimationChannel("head_group", "rotation", list(times), _quat_track(_rest_r(scene, "head_group"), [(rx, 0.0, 0.0) for rx in head_rx])),
    ]
    if "gaster_group" in scene.nodes:
        channels.append(
            AnimationChannel("gaster_group", "rotation", list(times), _quat_track(_rest_r(scene, "gaster_group"), [(rx, 0.0, 0.0) for rx in gaster_rx]))
        )

    # Right arm holds the yellow beach shovel: counter-rotates forward during back-fall (f0-f3) and
    # upward/back during forward rock (f4-f6) so the scoop stays clearly visible and >= 20 mm above floor!
    if "arm_right" in scene.nodes:
        ar_del = [
            (0.00, 0.00, 0.00),
            (+0.36, -0.10, -0.16),
            (+0.62, -0.14, -0.22),
            (+0.78, -0.16, -0.26),
            (+0.68, -0.14, -0.22),
            (+0.10, -0.08, -0.18),
            (-0.88, -0.06, -0.22),
            (-1.16, -0.08, -0.26),
            (-1.02, -0.06, -0.22),
            (-0.38, -0.02, -0.10),
            (-0.08, 0.00, -0.02),
            (0.00, 0.00, 0.00),
        ]
        channels.append(AnimationChannel("arm_right", "rotation", list(times), _quat_track(_rest_r(scene, "arm_right"), ar_del)))
    if "forearm_right" in scene.nodes:
        fr_del = [
            (0.00, 0.00, 0.00),
            (+0.18, 0.00, -0.08),
            (+0.32, 0.00, -0.12),
            (+0.42, 0.00, -0.14),
            (+0.34, 0.00, -0.10),
            (+0.06, 0.00, -0.06),
            (-0.20, 0.00, -0.10),
            (-0.28, 0.00, -0.12),
            (-0.24, 0.00, -0.10),
            (-0.08, 0.00, -0.04),
            (-0.02, 0.00, -0.01),
            (0.00, 0.00, 0.00),
        ]
        channels.append(AnimationChannel("forearm_right", "rotation", list(times), _quat_track(_rest_r(scene, "forearm_right"), fr_del)))

    # Left arm waves high in surprise on back-fall, then spreads outward for balance on forward rock.
    if "arm_left" in scene.nodes:
        al_del = [
            (0.00, 0.00, 0.00),
            (-0.46, -0.22, +0.58),
            (-0.62, -0.28, +0.72),
            (-0.42, -0.22, +0.58),
            (-0.18, -0.12, +0.42),
            (-0.24, -0.18, +0.46),
            (-0.56, -0.34, +0.52),
            (-0.72, -0.42, +0.62),
            (-0.62, -0.36, +0.54),
            (-0.22, -0.14, +0.24),
            (-0.05, -0.04, +0.06),
            (0.00, 0.00, 0.00),
        ]
        channels.append(AnimationChannel("arm_left", "rotation", list(times), _quat_track(_rest_r(scene, "arm_left"), al_del)))
    if "forearm_left" in scene.nodes:
        fl_del = [
            (0.00, 0.00, 0.00),
            (-0.24, -0.12, +0.18),
            (-0.34, -0.16, +0.24),
            (-0.22, -0.12, +0.18),
            (-0.10, -0.06, +0.10),
            (-0.12, -0.08, +0.12),
            (-0.24, +0.16, +0.16),
            (-0.32, +0.22, +0.20),
            (-0.26, +0.18, +0.16),
            (-0.10, +0.06, +0.06),
            (-0.02, +0.02, +0.02),
            (0.00, 0.00, 0.00),
        ]
        channels.append(AnimationChannel("forearm_left", "rotation", list(times), _quat_track(_rest_r(scene, "forearm_left"), fl_del)))

    for node_n, side in (("antenna_left", +1.0), ("antenna_right", -1.0)):
        if node_n in scene.nodes and scene.get_node(node_n):
            ant_del = [
                (0.00, 0.0, 0.0),
                (+0.20, 0.0, +side * 0.08),
                (+0.30, 0.0, +side * 0.12),
                (+0.22, 0.0, +side * 0.10),
                (-0.18, 0.0, -side * 0.06),
                (-0.24, 0.0, -side * 0.08),
                (-0.32, 0.0, -side * 0.10),
                (-0.38, 0.0, -side * 0.12),
                (-0.30, 0.0, -side * 0.10),
                (+0.14, 0.0, +side * 0.04),
                (+0.04, 0.0, +side * 0.02),
                (0.00, 0.0, 0.0),
            ]
            channels.append(AnimationChannel(node_n, "rotation", list(times), _quat_track(_rest_r(scene, node_n), ant_del)))

    clip = scene.add_animation("Hit", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Hit", knee_bow=1.05, splay=0.25, swivel=0.0)
    _solve_root_channels(scene, clip, times, root_eulers, root_z_com, min_air_y, com_y=0.45)
    return clip


__all__ = [
    "add_bomber_hit_animation",
    "add_swimmer_hit_animation",
    "add_thief_hit_animation",
]
