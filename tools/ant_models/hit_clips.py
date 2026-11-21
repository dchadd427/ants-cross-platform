"""Authentic 1998 Get-Hit (`*gh`) 3D `"Hit"` clips for Worker, Combat, and Fire Ants.

Pure-stdlib safe on import (mirrors `thief_attack.py` / `leg_rig.py`) and keeps all 3 ant
builders well under per-file line limits while preserving `Idle`, `Blink`, and `Attack` intact:
- Worker `aggh` (0.80 s, 9 frames `[100, 100, 120, 80, 70, 60, 60, 60, 150]` ms):
  airborne knockback onto back (f0..f3) -> snappy kip-up forward roll (f4..f7) -> upright (f8).
- Combat `acgh` (0.88 s, 9 frames `[100, 100, 100, 100, 80, 100, 120, 120, 60]` ms):
  heavy rock back (f0..f1) -> doubled-over gut-punch reel (f2..f6) -> solid heave upright (f7..f8).
- Fire `afgh` (1.00 s, 10 frames `[100 x 10]` ms):
  jolt up-back (f0..f1) -> helmet-heavy forward whiplash stumble (f2..f6) -> proud recovery (f7..f9).
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
        from .base_ant import EYELID_CLOSED_ANGLE, sync_leg_flex_to_clip
        from .leg_rig import LEG_NODES, _quat_conj, _quat_rotate, _shortest_arc_quat, _solve_knee_scale, leg_rig_info
        from .mesh_core import AnimationChannel, AnimationClip, SceneBuilder, quat_from_euler_xyz, quat_mul
        from .shared_clips_hazards import add_hazard_clips
        from .shared_clips_loco import add_shared_loco_clips
    except ImportError:
        from base_ant import EYELID_CLOSED_ANGLE, sync_leg_flex_to_clip  # type: ignore
        from leg_rig import LEG_NODES, _quat_conj, _quat_rotate, _shortest_arc_quat, _solve_knee_scale, leg_rig_info  # type: ignore
        from mesh_core import AnimationChannel, AnimationClip, SceneBuilder, quat_from_euler_xyz, quat_mul  # type: ignore
        from shared_clips_hazards import add_hazard_clips  # type: ignore
        from shared_clips_loco import add_shared_loco_clips  # type: ignore

Vec3 = Tuple[float, float, float]
Quat = Tuple[float, float, float, float]


def _rest_t(scene: SceneBuilder, node: str) -> Vec3:
    xf = scene.get_node_transform(node)
    return xf.translation if xf is not None else (0.0, 0.0, 0.0)


def _rest_r(scene: SceneBuilder, node: str) -> Quat:
    xf = scene.get_node_transform(node)
    return xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)


def _align_quats(qs: Sequence[Quat], *, close_loop: bool = True) -> List[Quat]:
    out: List[Quat] = [qs[0]]
    for q in qs[1:]:
        if sum(a * b for a, b in zip(out[-1], q)) < 0.0:
            q = (-q[0], -q[1], -q[2], -q[3])
        out.append(q)
    if close_loop and len(out) >= 2:
        q_end = out[0]
        if sum(a * b for a, b in zip(out[-2], q_end)) < 0.0:
            q_end = (-q_end[0], -q_end[1], -q_end[2], -q_end[3])
        out[-1] = q_end
    return out


def _quat_rot_mat(q: Quat) -> np.ndarray:
    x, y, z, w = q
    return np.array(
        [
            [1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w), 2.0 * (x * z + y * w)],
            [2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w)],
            [2.0 * (x * z - y * w), 2.0 * (y * z + x * w), 1.0 - 2.0 * (x * x + y * y)],
        ],
        dtype=np.float64,
    )


def _eval_pose_min_y(
    scene: SceneBuilder,
    overrides: Dict[str, Dict[str, Tuple[float, ...]]],
    node_verts: Dict[str, np.ndarray],
) -> float:
    """Exact world-space minimum Y across every mesh vertex for a single pose."""
    world: Dict[str, np.ndarray] = {}

    def _get(n: str) -> np.ndarray:
        if n in world:
            return world[n]
        xf = scene.get_node_transform(n)
        ov = overrides.get(n, {})
        t = ov.get("translation", xf.translation if xf is not None else (0.0, 0.0, 0.0))
        r = ov.get("rotation", xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0))
        s = ov.get("scale", xf.scale if xf is not None else (1.0, 1.0, 1.0))
        m = np.eye(4, dtype=np.float64)
        m[:3, :3] = _quat_rot_mat(r) * np.asarray(s, dtype=np.float64)[None, :]
        m[:3, 3] = np.asarray(t, dtype=np.float64)
        parent = scene.get_node_parent(n)
        if parent:
            m = _get(parent) @ m
        world[n] = m
        return m

    min_y = 1e9
    for n, verts in node_verts.items():
        m = _get(n)
        ys = verts @ m[1, :3] + m[1, 3]
        min_y = min(min_y, float(ys.min()))
    return min_y


def _interp_rows(rows: Sequence[Tuple[float, ...]], t: float) -> Tuple[float, ...]:
    """Piecewise-linear interpolation of numeric keyframe rows keyed by row[0] = timestamp."""
    if t <= rows[0][0]:
        return rows[0]
    if t >= rows[-1][0]:
        return rows[-1]
    for i in range(len(rows) - 1):
        t0, t1 = rows[i][0], rows[i + 1][0]
        if t0 <= t <= t1:
            u = (t - t0) / max(1e-9, t1 - t0)
            return tuple((1.0 - u) * a + u * b for a, b in zip(rows[i], rows[i + 1]))
    return rows[-1]


def _dense_times(key_rows: Sequence[Tuple[float, ...]], max_dt: float = 0.020, max_drx: float = 0.08) -> List[float]:
    """Build strictly increasing dense timestamps containing every frame start & subdividing big pitch arcs."""
    out: List[float] = [round(float(key_rows[0][0]), 5)]
    for i in range(1, len(key_rows)):
        t_prev = out[-1]
        t_next = float(key_rows[i][0])
        dt = t_next - t_prev
        drx = abs(float(key_rows[i][4]) - float(key_rows[i - 1][4]))
        dair = abs(float(key_rows[i][1]) - float(key_rows[i - 1][1]))
        n_sub = max(1, int(math.ceil(dt / max_dt)), int(math.ceil(drx / max_drx)), int(math.ceil(dair / 0.10)))
        for k in range(1, n_sub):
            t_mid = round(t_prev + dt * (k / n_sub), 5)
            if t_prev < t_mid < t_next - 1e-6:
                out.append(t_mid)
        if t_next > out[-1] + 1e-6:
            out.append(round(t_next, 5))
    return out


def _blink_closed_angle(scene: SceneBuilder) -> float:
    """Read the scene's calibrated Blink closed X-rotation angle so hit squints never over-rotate."""
    blink = next((c for c in scene.animations if c.name == "Blink"), None)
    if blink is not None:
        for ch in blink.channels:
            if ch.target_node == "eyelid_left" and ch.path == "rotation" and ch.values:
                r0 = _rest_r(scene, "eyelid_left")
                inv0 = _quat_conj(r0)
                best = 0.0
                for q in ch.values:
                    dq = quat_mul(inv0, q)
                    ang = 2.0 * math.atan2(float(dq[0]), max(1e-9, abs(float(dq[3]))))
                    if ang > best:
                        best = ang
                if best > 0.1:
                    return best
    return float(EYELID_CLOSED_ANGLE)


def _eyelid_channels(
    scene: SceneBuilder,
    times: Sequence[float],
    squints: Sequence[float],
    *,
    closed_angle: Optional[float] = None,
) -> List[AnimationChannel]:
    """Animate `eyelid_left` and `eyelid_right` squinting in shock during impact recoil."""
    ang_max = float(closed_angle if closed_angle is not None else _blink_closed_angle(scene))
    out: List[AnimationChannel] = []
    for lid_n in ("eyelid_left", "eyelid_right"):
        if lid_n not in scene.nodes or not scene.get_node(lid_n):
            continue
        r0 = _rest_r(scene, lid_n)
        qs = _align_quats([quat_mul(r0, quat_from_euler_xyz(rx=ang_max * float(sq))) for sq in squints])
        out.append(AnimationChannel(lid_n, "rotation", list(times), qs))
    return out


def add_worker_hit(scene: SceneBuilder) -> AnimationClip:
    """Authentic `aggh` 0.80 s (9-frame [100,100,120,80,70,60,60,60,150]ms) knockback onto back + roll recovery."""
    # Exact aggh frame starts: f0=0.00, f1=0.10, f2=0.20, f3=0.32, f4=0.40, f5=0.47, f6=0.53, f7=0.59, f8=0.65 -> 0.80.
    # Columns: (t, air_w, dy, dz, ub_rx, head_rx, gaster_rx, arm_rx, arm_rz, fore_rx, ant_rx, squint, leg_kick)
    key_rows = [
        (0.00, 1.00, +0.165, -0.055, -0.68, -0.18, -0.18, -0.44, +0.86, -0.16, -0.28, 0.05, +0.26),  # f0: blasted up & back
        (0.10, 1.00, +0.070, -0.125, -1.28, -0.08, -0.22, -0.18, +1.05, -0.12, -0.40, 0.65, +0.54),  # f1: flat in mid-air
        (0.20, 1.00, -0.175, -0.155, -1.56, +0.08, -0.10, +0.18, +1.10, -0.08, -0.14, 0.92, +0.66),  # f2: thump on back
        (0.32, 1.00, -0.160, -0.135, -1.46, +0.20, +0.08, +0.34, +0.84, -0.14, +0.04, 0.55, +0.42),  # f3: coil to kip-up
        (0.36, 0.45, +0.055, -0.055, -0.50, +0.24, +0.22, +0.14, +0.54, -0.24, +0.14, 0.15, +0.12),  # kip-up mid-air snap
        (0.40, 0.00, -0.032, +0.015, +0.36, +0.26, +0.20, -0.36, +0.52, -0.24, +0.18, 0.00, +0.00),  # f4: land crouched
        (0.47, 0.00, -0.018, +0.044, +0.88, +0.26, +0.24, -0.52, +0.68, -0.30, +0.24, 0.00, +0.00),  # f5: forward roll
        (0.53, 0.00, -0.004, +0.054, +1.14, +0.22, +0.28, -0.56, +0.74, -0.32, +0.26, 0.00, +0.00),  # f6: deep forward bow
        (0.59, 0.00, -0.016, +0.026, +0.62, +0.10, +0.14, -0.26, +0.40, -0.16, +0.10, 0.00, +0.00),  # f7: rising upright
        (0.65, 0.00, +0.000, +0.000, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, 0.00, +0.00),  # f8: upright rest
        (0.72, 0.00, +0.000, +0.000, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, 0.00, +0.00),  # f8 hold
        (0.76, 0.55, +0.095, -0.028, -0.34, -0.10, -0.10, -0.22, +0.46, -0.10, -0.16, 0.02, +0.14),  # liftoff blend
        (0.80, 1.00, +0.165, -0.055, -0.68, -0.18, -0.18, -0.44, +0.86, -0.16, -0.28, 0.05, +0.26),  # wrap == f0
    ]
    times = _dense_times(key_rows, max_dt=0.020, max_drx=0.08)
    samples = [_interp_rows(key_rows, t) for t in times]

    bx, by, bz = _rest_t(scene, "upper_body")
    t_ub0 = np.array([bx, by, bz], dtype=np.float64)
    r_ub0 = _rest_r(scene, "upper_body")
    r_hd0 = _rest_r(scene, "head_group")
    r_gs0 = _rest_r(scene, "gaster_group")

    # Precollect per-node vertex arrays for fast exact floor-clearance clamping.
    node_verts: Dict[str, np.ndarray] = {
        n: np.vstack([np.asarray(p.positions, dtype=np.float64) for p in ps])
        for n, ps in scene.nodes.items()
        if ps
    }

    # Precompute per-leg rest geometry matching `leg_rig._flex_channels`.
    leg_infos = []
    for node, pivot, hip in leg_rig_info(scene):
        xf = scene.get_node_transform(node)
        lt0 = np.asarray(xf.translation if xf is not None else pivot, dtype=np.float64)
        lr0 = xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)
        ls0 = np.asarray(xf.scale if xf is not None else (1.0, 1.0, 1.0), dtype=np.float64)
        verts = node_verts[node]
        inv_r0 = _quat_conj(lr0)
        hip_loc = _quat_rotate(inv_r0, hip - lt0) / np.where(np.abs(ls0) > 1e-8, ls0, 1.0)
        hip_in_ub = _quat_rotate(_quat_conj(r_ub0), hip - t_ub0)
        v_rest = hip - lt0
        min_y_v = float(verts[:, 1].min())
        foot_low = verts[verts[:, 1] <= min_y_v + 0.012]
        sole = verts[verts[:, 1] <= min_y_v + 0.005]
        if len(sole) == 0:
            sole = verts[[int(np.argmin(verts[:, 1]))]]
        if len(foot_low) == 0:
            foot_low = sole
        foot_rest_min_y = float(_quat_rotate(lr0, foot_low * ls0)[:, 1].min())
        sole_rest_xz = _quat_rotate(lr0, sole * ls0)[:, [0, 2]].mean(axis=0)
        side = 1.0 if pivot[0] >= 0.0 else -1.0
        leg_infos.append((node, lt0, lr0, ls0, hip_loc, hip_in_ub, v_rest, foot_low, sole, foot_rest_min_y, sole_rest_xz, side))

    ub_ts: List[Vec3] = []
    ub_rs: List[Quat] = []
    hd_rs: List[Quat] = []
    gs_rs: List[Quat] = []
    arm_rs: Dict[str, List[Quat]] = {n: [] for n in ("arm_left", "arm_right", "forearm_left", "forearm_right")}
    ant_rs: Dict[str, List[Quat]] = {n: [] for n in ("antenna_left", "antenna_right")}
    squints: List[float] = []
    leg_ts: Dict[str, List[Vec3]] = {info[0]: [] for info in leg_infos}
    leg_rs: Dict[str, List[Quat]] = {info[0]: [] for info in leg_infos}
    leg_ss: Dict[str, List[Vec3]] = {info[0]: [] for info in leg_infos}

    r_arm0 = {n: _rest_r(scene, n) for n in arm_rs}
    r_ant0 = {n: _rest_r(scene, n) for n in ant_rs}
    r_lid0 = {n: _rest_r(scene, n) for n in ("eyelid_left", "eyelid_right")}

    for row in samples:
        _, air_w, dy, dz, ub_rx, hd_rx, gs_rx, a_rx, a_rz, f_rx, an_rx, sq, kick = row
        q_ub = quat_mul(r_ub0, quat_from_euler_xyz(rx=ub_rx))
        t_ub = np.array([bx, by + dy, bz + dz], dtype=np.float64)
        q_hd = quat_mul(r_hd0, quat_from_euler_xyz(rx=hd_rx))
        q_gs = quat_mul(r_gs0, quat_from_euler_xyz(rx=gs_rx))

        frame_arms: Dict[str, Quat] = {}
        for side_tag, s in (("left", +1.0), ("right", -1.0)):
            frame_arms[f"arm_{side_tag}"] = quat_mul(r_arm0[f"arm_{side_tag}"], quat_from_euler_xyz(rx=a_rx, rz=s * a_rz))
            frame_arms[f"forearm_{side_tag}"] = quat_mul(r_arm0[f"forearm_{side_tag}"], quat_from_euler_xyz(rx=f_rx, rz=s * 0.22 * a_rz))
        frame_ants = {
            "antenna_left": quat_mul(r_ant0["antenna_left"], quat_from_euler_xyz(rx=an_rx, rz=+0.08 * abs(ub_rx))),
            "antenna_right": quat_mul(r_ant0["antenna_right"], quat_from_euler_xyz(rx=an_rx, rz=-0.08 * abs(ub_rx))),
        }

        # Solve every leg coherently from `upper_body`'s hip world target:
        # air_w == 0 -> exact grounded sole-on-Y=0 flex; air_w > 0 -> rigid body follow + mid-air kick!
        frame_legs: Dict[str, Tuple[np.ndarray, Quat, np.ndarray]] = {}
        q_delta_ub = quat_mul(q_ub, _quat_conj(r_ub0))
        for node, lt0, lr0, ls0, hip_loc, hip_in_ub, v_rest, foot_low, sole, foot_rest_min_y, sole_rest_xz, side in leg_infos:
            h_target = t_ub + _quat_rotate(q_ub, hip_in_ub)
            if air_w <= 1e-6:
                t_cur = lt0.copy()
                s_cur = ls0.copy()
                q_cur = lr0
                for step in range(6):
                    d_vec = h_target - t_cur
                    s_rel = _solve_knee_scale(hip_loc * ls0, float(v_rest[1]), d_vec, knee_bow=1.6, splay=0.45, flex_bias=0.0)
                    s_cur = ls0 * s_rel
                    v_scaled = _quat_rotate(lr0, hip_loc * s_cur)
                    q_arc = _shortest_arc_quat(v_scaled, d_vec)
                    q_cur = quat_mul(q_arc, lr0)
                    if step < 5:
                        sole_now = _quat_rotate(q_cur, sole * s_cur)
                        foot_now = _quat_rotate(q_cur, foot_low * s_cur)
                        dy_sole = float(foot_now[:, 1].min()) - foot_rest_min_y
                        dxz_sole = sole_now[:, [0, 2]].mean(axis=0) - sole_rest_xz
                        t_next = np.array(
                            [lt0[0] - max(-0.0025, min(0.0025, float(dxz_sole[0]))), lt0[1] - dy_sole, lt0[2] - max(-0.0025, min(0.0025, float(dxz_sole[1])))],
                            dtype=np.float64,
                        )
                        t_cur = 0.55 * t_next + 0.45 * t_cur
                foot_final = _quat_rotate(q_cur, foot_low * s_cur)
                t_cur[1] = lt0[1] - (float(foot_final[:, 1].min()) - foot_rest_min_y)
                frame_legs[node] = (t_cur, q_cur, s_cur)
            else:
                # Mid-air kick around the true hip joint -> 0.000 mm hip separation at every airborne frame!
                is_rear = "rear" in node
                kick_rx = kick * (0.75 if is_rear else 1.15)
                kick_rz = side * kick * (0.34 if is_rear else 0.48)
                q_kick = quat_from_euler_xyz(rx=kick_rx, rz=kick_rz)
                q_air = quat_mul(q_delta_ub, quat_mul(q_kick, lr0))
                t_air = h_target - _quat_rotate(q_air, hip_loc * ls0)
                if air_w >= 0.999:
                    frame_legs[node] = (t_air, q_air, ls0.copy())
                else:
                    # Smooth blend between planted foot and airborne foot while keeping hip locked on `h_target`.
                    f_blend = (1.0 - air_w) * lt0 + air_w * t_air
                    f_blend[1] = max(0.0, float(f_blend[1]))
                    d_vec = h_target - f_blend
                    s_rel = _solve_knee_scale(hip_loc * ls0, float(v_rest[1]), d_vec, knee_bow=1.2, splay=0.35, flex_bias=0.0)
                    s_cur = ls0 * ((1.0 - air_w) * s_rel + air_w * np.ones(3))
                    v_scaled = _quat_rotate(lr0, hip_loc * s_cur)
                    q_cur = quat_mul(_shortest_arc_quat(v_scaled, d_vec), lr0)
                    t_cur = h_target - _quat_rotate(q_cur, hip_loc * s_cur)
                    frame_legs[node] = (t_cur, q_cur, s_cur)

        # Exact floor-clearance lift on airborne / supine frames so zero vertices ever sink below Y = 0.0.
        if air_w > 1e-6:
            ov: Dict[str, Dict[str, Tuple[float, ...]]] = {
                "upper_body": {"translation": tuple(float(c) for c in t_ub), "rotation": q_ub},
                "head_group": {"rotation": q_hd},
                "gaster_group": {"rotation": q_gs},
            }
            for an, aq in frame_arms.items():
                ov[an] = {"rotation": aq}
            for an, aq in frame_ants.items():
                ov[an] = {"rotation": aq}
            for ln, lq0 in r_lid0.items():
                ov[ln] = {"rotation": quat_mul(lq0, quat_from_euler_xyz(rx=EYELID_CLOSED_ANGLE * sq))}
            for ln, (lt, lq, ls) in frame_legs.items():
                ov[ln] = {"translation": tuple(float(c) for c in lt), "rotation": lq, "scale": tuple(float(c) for c in ls)}
            min_y = _eval_pose_min_y(scene, ov, node_verts)
            if min_y < 0.010:
                lift = 0.010 - min_y
                t_ub[1] += lift
                for ln in frame_legs:
                    lt, lq, ls = frame_legs[ln]
                    lt[1] += lift

        ub_ts.append((float(t_ub[0]), float(t_ub[1]), float(t_ub[2])))
        ub_rs.append(q_ub)
        hd_rs.append(q_hd)
        gs_rs.append(q_gs)
        squints.append(sq)
        for an, aq in frame_arms.items():
            arm_rs[an].append(aq)
        for an, aq in frame_ants.items():
            ant_rs[an].append(aq)
        for ln, (lt, lq, ls) in frame_legs.items():
            leg_ts[ln].append((float(lt[0]), float(lt[1]), float(lt[2])))
            leg_rs[ln].append(lq)
            leg_ss[ln].append((float(ls[0]), float(ls[1]), float(ls[2])))

    # Enforce exact periodic loop closure (vals[-1] == vals[0]).
    ub_ts[-1] = ub_ts[0]
    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), ub_ts),
        AnimationChannel("upper_body", "rotation", list(times), _align_quats(ub_rs)),
        AnimationChannel("head_group", "rotation", list(times), _align_quats(hd_rs)),
        AnimationChannel("gaster_group", "rotation", list(times), _align_quats(gs_rs)),
    ]
    for an, qs in arm_rs.items():
        if an in scene.nodes and scene.get_node(an):
            channels.append(AnimationChannel(an, "rotation", list(times), _align_quats(qs)))
    for an, qs in ant_rs.items():
        if an in scene.nodes and scene.get_node(an):
            channels.append(AnimationChannel(an, "rotation", list(times), _align_quats(qs)))
    channels.extend(_eyelid_channels(scene, times, squints))
    for ln in leg_ts:
        leg_ts[ln][-1] = leg_ts[ln][0]
        leg_ss[ln][-1] = leg_ss[ln][0]
        channels.append(AnimationChannel(ln, "scale", list(times), leg_ss[ln]))
        channels.append(AnimationChannel(ln, "rotation", list(times), _align_quats(leg_rs[ln])))
        channels.append(AnimationChannel(ln, "translation", list(times), leg_ts[ln]))

    clip = scene.add_animation("Hit", channels, replace=True)
    add_shared_loco_clips(scene, "worker")
    add_hazard_clips(scene, "worker")
    return clip


def add_combat_hit(scene: SceneBuilder) -> AnimationClip:
    """Authentic `acgh` 0.88 s (9-frame [100,100,100,100,80,100,120,120,60]ms) stagger back + gut-punch reel."""
    q_id: Quat = (0.0, 0.0, 0.0, 1.0)
    # Read final scaled arm pivots straight from the scene hierarchy (`0.80x` world space).
    el_rel_l = np.asarray(_rest_t(scene, "left_forearm_group"), dtype=np.float64)
    el_rel_r = np.asarray(_rest_t(scene, "right_forearm_group"), dtype=np.float64)
    # Compute rest forearm direction from forearm mesh centroid relative to elbow pivot.
    lf_v = np.vstack([np.asarray(p.positions, dtype=np.float64) for p in scene.get_node("left_forearm")])
    rf_v = np.vstack([np.asarray(p.positions, dtype=np.float64) for p in scene.get_node("right_forearm")])
    lf_t = np.asarray(_rest_t(scene, "left_forearm"), dtype=np.float64)
    rf_t = np.asarray(_rest_t(scene, "right_forearm"), dtype=np.float64)
    # Use the fist end (furthest vertex from elbow) as rest forearm vector.
    lf_pts = lf_v + lf_t[None, :]
    rf_pts = rf_v + rf_t[None, :]
    f_vec_l = lf_pts[int(np.argmax(np.linalg.norm(lf_pts, axis=1)))]
    f_vec_r = rf_pts[int(np.argmax(np.linalg.norm(rf_pts, axis=1)))]

    r_ub0 = _rest_r(scene, "upper_body")

    def _aim_pair_world(
        ub_rx: float,
        u_world_l: Vec3,
        f_world_l: Vec3,
    ) -> Tuple[Quat, Quat, Quat, Quat]:
        """Aim bent muscular gorilla arms directly in WORLD space (converted into pitched `upper_body` frame)."""
        r_ub_inv = _quat_rot_mat(quat_mul(r_ub0, quat_from_euler_xyz(rx=ub_rx))).T
        u_l = r_ub_inv @ np.asarray(u_world_l, dtype=np.float64)
        f_l = r_ub_inv @ np.asarray(f_world_l, dtype=np.float64)
        q_sh_l = _shortest_arc_quat(el_rel_l, u_l)
        q_el_l = _shortest_arc_quat(f_vec_l, _quat_rot_mat(q_sh_l).T @ f_l)
        u_r = r_ub_inv @ np.array([-u_world_l[0], u_world_l[1], u_world_l[2]], dtype=np.float64)
        f_r = r_ub_inv @ np.array([-f_world_l[0], f_world_l[1], f_world_l[2]], dtype=np.float64)
        q_sh_r = _shortest_arc_quat(el_rel_r, u_r)
        q_el_r = _shortest_arc_quat(f_vec_r, _quat_rot_mat(q_sh_r).T @ f_r)
        return q_sh_l, q_el_l, q_sh_r, q_el_r

    # Bent, natural gorilla brawler arms specified directly in WORLD space (+X out, -Y down, +Z forward)
    # matching `acgh301` & `acgh901` (curved down around knees/sides — zero stiff airplane T-pose or skyward arms!).
    arms_f0 = _aim_pair_world(-0.34, (+0.78, -0.52, +0.34), (+0.18, -0.78, +0.60))
    arms_f1 = _aim_pair_world(-0.38, (+0.82, -0.46, +0.34), (+0.24, -0.76, +0.60))
    arms_f2 = _aim_pair_world(+0.30, (+0.72, -0.60, +0.35), (+0.10, -0.76, +0.64))
    arms_f3 = _aim_pair_world(+0.80, (+0.74, -0.58, +0.34), (+0.14, -0.72, +0.68))
    arms_f4 = _aim_pair_world(+1.16, (+0.78, -0.54, +0.32), (+0.18, -0.64, +0.74))
    arms_f5 = _aim_pair_world(+1.30, (+0.80, -0.52, +0.30), (+0.20, -0.60, +0.77))
    arms_f6 = _aim_pair_world(+1.22, (+0.78, -0.54, +0.32), (+0.18, -0.64, +0.74))
    arms_f7 = _aim_pair_world(+0.62, (+0.70, -0.64, +0.32), (+0.04, -0.76, +0.65))
    arms_rest = (q_id, q_id, q_id, q_id)

    # Exact acgh starts: f0=0.00, f1=0.10, f2=0.20, f3=0.30, f4=0.40, f5=0.48, f6=0.58, f7=0.70, f8=0.82 -> 0.88.
    # (t, dy, dz, ub_rx, head_rx, gaster_rx, squint, arms_tuple)
    rows = [
        (0.00, +0.014, -0.022, -0.34, -0.16, -0.14, 0.72, arms_f0),    # f0: heavy rock back
        (0.10, +0.018, -0.028, -0.38, -0.20, -0.18, 0.80, arms_f1),    # f1: peak stagger back
        (0.20, -0.006, +0.012, +0.30, +0.08, +0.10, 0.40, arms_f2),    # f2: snapping forward
        (0.30, +0.014, +0.028, +0.80, +0.16, +0.20, 0.25, arms_f3),    # f3: doubling over
        (0.40, +0.036, +0.042, +1.16, +0.18, +0.26, 0.30, arms_f4),    # f4: deep gut-punch reel
        (0.48, +0.044, +0.048, +1.30, +0.18, +0.30, 0.35, arms_f5),    # f5: peak hunched over knees
        (0.58, +0.040, +0.044, +1.22, +0.16, +0.28, 0.25, arms_f6),    # f6: holding gut-punch hunch
        (0.70, +0.008, +0.022, +0.62, +0.06, +0.14, 0.10, arms_f7),    # f7: heaving back upright
        (0.82, +0.000, +0.000, +0.00, +0.00, +0.00, 0.00, arms_rest),  # f8: solidly upright
        (0.88, +0.014, -0.022, -0.34, -0.16, -0.14, 0.72, arms_f0),    # wrap == f0
    ]
    times = [round(r[0], 4) for r in rows]
    bx, by, bz = _rest_t(scene, "upper_body")
    r_hd0 = _rest_r(scene, "head_group")

    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), [(bx, by + r[1], bz + r[2]) for r in rows]),
        AnimationChannel("upper_body", "rotation", list(times), _align_quats([quat_mul(r_ub0, quat_from_euler_xyz(rx=r[3])) for r in rows])),
        AnimationChannel("head_group", "rotation", list(times), _align_quats([quat_mul(r_hd0, quat_from_euler_xyz(rx=r[4])) for r in rows])),
        AnimationChannel("gaster_group", "rotation", list(times), _align_quats([quat_from_euler_xyz(rx=r[5]) for r in rows])),
        AnimationChannel("left_arm_group", "rotation", list(times), _align_quats([r[7][0] for r in rows])),
        AnimationChannel("left_forearm_group", "rotation", list(times), _align_quats([r[7][1] for r in rows])),
        AnimationChannel("right_arm_group", "rotation", list(times), _align_quats([r[7][2] for r in rows])),
        AnimationChannel("right_forearm_group", "rotation", list(times), _align_quats([r[7][3] for r in rows])),
    ]
    channels.extend(_eyelid_channels(scene, times, [r[6] for r in rows]))

    clip = scene.add_animation("Hit", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Hit", swivel=0.0)
    add_shared_loco_clips(scene, "combat")
    add_hazard_clips(scene, "combat")
    return clip


def add_fire_hit(scene: SceneBuilder, *, hold_magnifying_glass: bool = False) -> AnimationClip:
    """Authentic `afgh` 1.00 s (10-frame [100x10]ms) jolt back + heavy-helmet forward whiplash stumble."""
    # Exact afgh starts: f0=0.00, f1=0.10, ..., f9=0.90 -> 1.00.
    # (t, dy, dz, ub_rx, ub_rz, head_rx, gaster_rx, arm_ry, arm_rz, arm_rx, fore_rx, squint)
    rows = [
        (0.00, +0.036, -0.020, -0.36, -0.08, -0.12, -0.14, +0.18, +0.70, -0.24, -0.20, 0.72),  # f0: jolt up-back
        (0.10, +0.020, -0.008, -0.16, -0.18, -0.04, -0.06, +0.24, +0.66, -0.20, -0.18, 0.55),  # f1: recoil & tilt
        (0.20, +0.002, +0.018, +0.40, -0.24, +0.08, +0.12, +0.42, +0.76, -0.38, -0.28, 0.30),  # f2: helmet tipping forward
        (0.30, -0.002, +0.032, +0.76, -0.28, +0.14, +0.20, +0.58, +0.84, -0.52, -0.36, 0.20),  # f3: stumbling forward
        (0.40, +0.008, +0.042, +1.08, -0.26, +0.18, +0.26, +0.68, +0.90, -0.62, -0.44, 0.25),  # f4: deep helmet stumble
        (0.50, +0.018, +0.048, +1.30, -0.22, +0.18, +0.28, +0.74, +0.92, -0.66, -0.48, 0.30),  # f5: helmet crown over face
        (0.60, +0.022, +0.050, +1.38, -0.14, +0.18, +0.30, +0.74, +0.94, -0.68, -0.50, 0.25),  # f6: peak helmet whiplash
        (0.70, +0.002, +0.028, +0.86, -0.08, +0.08, +0.16, +0.44, +0.56, -0.38, -0.28, 0.10),  # f7: heaving upright
        (0.80, -0.006, +0.010, +0.34, -0.03, +0.02, +0.06, +0.18, +0.24, -0.16, -0.12, 0.00),  # f8: nearly upright
        (0.90, +0.000, +0.000, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, 0.00),  # f9: proud recovery
        (0.94, +0.000, +0.000, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, 0.00),  # brief rest hold
        (1.00, +0.036, -0.020, -0.36, -0.08, -0.12, -0.14, +0.18, +0.70, -0.24, -0.20, 0.72),  # wrap == f0
    ]
    times = [round(r[0], 4) for r in rows]
    bx, by, bz = _rest_t(scene, "upper_body")
    r_ub0 = _rest_r(scene, "upper_body")
    r_hd0 = _rest_r(scene, "head_group")

    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), [(bx, by + r[1], bz + r[2]) for r in rows]),
        AnimationChannel("upper_body", "rotation", list(times), _align_quats([quat_mul(r_ub0, quat_from_euler_xyz(rx=r[3], rz=r[4])) for r in rows])),
        AnimationChannel("head_group", "rotation", list(times), _align_quats([quat_mul(r_hd0, quat_from_euler_xyz(rx=r[5])) for r in rows])),
    ]
    if "gaster_group" in scene.nodes:
        r_gs0 = _rest_r(scene, "gaster_group")
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), _align_quats([quat_mul(r_gs0, quat_from_euler_xyz(rx=r[6])) for r in rows])))
    for side_tag, sign, scale in (("left", +1.0, 1.0), ("right", -1.0, 0.55 if hold_magnifying_glass else 1.0)):
        arm_n, fore_n = f"arm_{side_tag}", f"forearm_{side_tag}"
        if arm_n in scene.nodes and scene.get_node(arm_n):
            r0_a = _rest_r(scene, arm_n)
            channels.append(
                AnimationChannel(
                    arm_n,
                    "rotation",
                    list(times),
                    _align_quats([
                        quat_mul(r0_a, quat_from_euler_xyz(rx=scale * r[9], ry=sign * scale * r[7], rz=sign * scale * r[8]))
                        for r in rows
                    ]),
                )
            )
        if fore_n in scene.nodes and scene.get_node(fore_n):
            r0_f = _rest_r(scene, fore_n)
            channels.append(
                AnimationChannel(
                    fore_n,
                    "rotation",
                    list(times),
                    _align_quats([quat_mul(r0_f, quat_from_euler_xyz(rx=scale * r[10])) for r in rows]),
                )
            )
    channels.extend(_eyelid_channels(scene, times, [r[11] for r in rows]))

    clip = scene.add_animation("Hit", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Hit", swivel=0.0)
    add_shared_loco_clips(scene, "fire")
    add_hazard_clips(scene, "fire")
    return clip


__all__ = ["add_combat_hit", "add_fire_hit", "add_worker_hit"]
