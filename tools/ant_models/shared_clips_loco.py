"""Authentic 1998 shared locomotion & action glTF clips across all 6 ant classes.

Pure-stdlib safe on import (mirrors `hit_clips.py` / `thief_attack.py`) and hooked at the end
of the 6 hit builders so zero `ant_*.py` geometry modules need edits:
- `"Walk"`    (`*wg`, 12 frames, `0.60 s` on all 6 classes): alternating opposite-pair leg
  strides, hip bounce, counter-swinging arms (both standard & Combat rig), head stabilization,
  and seamless loop closure with stance soles planted on `Y = 0.0`.
- `"Harvest"` (`*gf`, exact CHD durations `0.32..0.44 s`): snappy forward stoop, reaching arms
  & mandible scoop-chomp, grounded soles, and upright recovery.
- `"CantGo"`  (`*cg`, exact CHD durations `0.36..1.38 s`): class-specific 1998 head-shake,
  shrug, side-lean, and exasperated gestures with grounded soles.
- `"Enter"`   (`*en`, exact CHD durations `0.50..1.24 s`): head-first dive into the anthill
  burrow (skyward gaster + kicking hind legs shrinking flush to `Y = 0.0`, then reversing cleanly
  back upright matching `agen301..afen301`).
- `"Hatch"`   (`*hatch`, exact CHD durations `0.30..0.64 s`): energetic emergence popping out
  of the ground burrow with squash-and-stretch and flailing arms into upright rest pose.
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
        from .mesh_core import AnimationChannel, AnimationClip, SceneBuilder, quat_from_axis_angle, quat_from_euler_xyz, quat_mul
    except ImportError:
        from base_ant import EYELID_CLOSED_ANGLE, sync_leg_flex_to_clip  # type: ignore
        from leg_rig import LEG_NODES, _quat_conj, _quat_rotate, _shortest_arc_quat, _solve_knee_scale, leg_rig_info  # type: ignore
        from mesh_core import AnimationChannel, AnimationClip, SceneBuilder, quat_from_axis_angle, quat_from_euler_xyz, quat_mul  # type: ignore

Vec3 = Tuple[float, float, float]
Quat = Tuple[float, float, float, float]

_HIDE_SCALE: Vec3 = (1e-4, 1e-4, 1e-4)
_SHOW_SCALE: Vec3 = (1.0, 1.0, 1.0)
_HIDDEN_PROP_NODES: Tuple[str, ...] = (
    "thief_whip_lasso",
    "thief_whip_crack",
    "thief_whip_recoil",
    "snorkel_water_glob",
    "snorkel_water_spray_a",
    "snorkel_water_spray_b",
)

# Exact 1998 CHD durations (seconds) from `docs/chd_table4_animations.json`.
_DURATIONS: Dict[str, Dict[str, float]] = {
    "Walk": {"worker": 0.60, "combat": 0.60, "thief": 0.60, "bomber": 0.60, "swimmer": 0.60, "fire": 0.60},
    "Harvest": {"worker": 0.42, "combat": 0.36, "thief": 0.34, "bomber": 0.44, "swimmer": 0.32, "fire": 0.40},
    "CantGo": {"worker": 0.36, "combat": 0.62, "thief": 0.89, "bomber": 1.38, "swimmer": 0.96, "fire": 0.66},
    "Enter": {"worker": 1.00, "combat": 1.20, "thief": 1.00, "bomber": 1.24, "swimmer": 0.90, "fire": 0.50},
    "Hatch": {"worker": 0.52, "combat": 0.64, "thief": 0.52, "bomber": 0.64, "swimmer": 0.48, "fire": 0.30},
}


def _rest_t(scene: SceneBuilder, node: str) -> Vec3:
    xf = scene.get_node_transform(node)
    return xf.translation if xf is not None else (0.0, 0.0, 0.0)


def _rest_r(scene: SceneBuilder, node: str) -> Quat:
    xf = scene.get_node_transform(node)
    return xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)


def _rest_s(scene: SceneBuilder, node: str) -> Vec3:
    xf = scene.get_node_transform(node)
    return xf.scale if xf is not None else (1.0, 1.0, 1.0)


def _align_quats(qs: Sequence[Quat], *, close_loop: bool = False) -> List[Quat]:
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


def _quat_rot_mat(q: Quat) -> "np.ndarray":
    x, y, z, w = q
    return np.array(
        [
            [1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w), 2.0 * (x * z + y * w)],
            [2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w)],
            [2.0 * (x * z - y * w), 2.0 * (y * z + x * w), 1.0 - 2.0 * (x * x + y * y)],
        ],
        dtype=np.float64,
    )


def _arm_pairs(scene: SceneBuilder) -> List[Tuple[str, str, float]]:
    """Return `(upper_arm_node, forearm_node, side_sign)` handling both standard & Combat rigs."""
    pairs: List[Tuple[str, str, float]] = []
    if "left_arm_group" in scene.nodes and "right_arm_group" in scene.nodes:
        pairs.append(("left_arm_group", "left_forearm_group", +1.0))
        pairs.append(("right_arm_group", "right_forearm_group", -1.0))
    else:
        for side_tag, s in (("left", +1.0), ("right", -1.0)):
            a_n, f_n = f"arm_{side_tag}", f"forearm_{side_tag}"
            if a_n in scene.nodes:
                pairs.append((a_n, f_n if f_n in scene.nodes else "", s))
    return pairs


def _arm_scale(ant_class: str, side: float) -> float:
    if ant_class == "swimmer" and side < 0.0:
        return 0.45  # keep Swimmer's beach shovel smoothly clear of legs & floor
    if ant_class == "combat":
        return 0.72
    if ant_class == "fire" and side < 0.0:
        return 0.78
    return 1.0


def _hidden_prop_channels(scene: SceneBuilder, dur: float) -> List[AnimationChannel]:
    """Keep attack-only prop nodes (`thief_whip_*`, `snorkel_water_*`) hidden & worn bandana shown."""
    out: List[AnimationChannel] = []
    ts = [0.0, round(float(dur), 5)]
    for n in _HIDDEN_PROP_NODES:
        if n in scene.nodes:
            out.append(AnimationChannel(n, "scale", list(ts), [_HIDE_SCALE, _HIDE_SCALE], interpolation="STEP"))
    if "bandana_worn_group" in scene.nodes:
        out.append(AnimationChannel("bandana_worn_group", "scale", list(ts), [_SHOW_SCALE, _SHOW_SCALE], interpolation="STEP"))
    return out


def _visible_node_verts(scene: SceneBuilder) -> Dict[str, "np.ndarray"]:
    """Collect vertices of all visible rest nodes (excluding hidden `1e-4` attack props)."""
    out: Dict[str, "np.ndarray"] = {}
    hidden = set(_HIDDEN_PROP_NODES)
    for n, ps in scene.nodes.items():
        if not ps or n in hidden:
            continue
        sx, sy, sz = _rest_s(scene, n)
        if max(abs(sx), abs(sy), abs(sz)) < 0.01:
            continue
        out[n] = np.vstack([np.asarray(p.positions, dtype=np.float64) for p in ps])
    return out


def _eval_pose_min_y(
    scene: SceneBuilder,
    overrides: Dict[str, Dict[str, Tuple[float, ...]]],
    node_verts: Dict[str, "np.ndarray"],
) -> float:
    """Exact world-space minimum Y across every visible mesh vertex for a single pose."""
    world: Dict[str, "np.ndarray"] = {}

    def _get(n: str) -> "np.ndarray":
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


def _leg_infos(scene: SceneBuilder, node_verts: Dict[str, "np.ndarray"]) -> list:
    t_ub0 = np.asarray(_rest_t(scene, "upper_body"), dtype=np.float64)
    r_ub0 = _rest_r(scene, "upper_body")
    inv_ub0 = _quat_conj(r_ub0)
    infos = []
    for node, pivot, hip in leg_rig_info(scene):
        lt0 = np.asarray(_rest_t(scene, node), dtype=np.float64)
        lr0 = _rest_r(scene, node)
        ls0 = np.asarray(_rest_s(scene, node), dtype=np.float64)
        verts = node_verts[node]
        hip_loc = _quat_rotate(_quat_conj(lr0), hip - lt0) / np.where(np.abs(ls0) > 1e-8, ls0, 1.0)
        hip_in_ub = _quat_rotate(inv_ub0, hip - t_ub0)
        v_rest = hip - lt0
        min_y_v = float(verts[:, 1].min())
        foot_low = verts[verts[:, 1] <= min_y_v + 0.014]
        if len(foot_low) == 0:
            foot_low = verts[[int(np.argmin(verts[:, 1]))]]
        foot_rest_min_y = float(_quat_rotate(lr0, foot_low * ls0)[:, 1].min())
        side = 1.0 if float(pivot[0]) >= 0.0 else -1.0
        is_rear = "rear" in node
        infos.append((node, lt0, lr0, ls0, hip_loc, hip_in_ub, v_rest, foot_low, foot_rest_min_y, side, is_rear))
    return infos


def _blink_closed_angle(scene: SceneBuilder) -> float:
    blink = next((c for c in scene.animations if c.name == "Blink"), None)
    if blink is not None:
        for ch in blink.channels:
            if ch.target_node == "eyelid_left" and ch.path == "rotation" and ch.values:
                inv0 = _quat_conj(_rest_r(scene, "eyelid_left"))
                best = 0.0
                for q in ch.values:
                    dq = quat_mul(inv0, q)
                    best = max(best, 2.0 * math.atan2(float(dq[0]), max(1e-9, abs(float(dq[3])))))
                if best > 0.1:
                    return best
    return float(EYELID_CLOSED_ANGLE)


def _eyelid_squint_channels(scene: SceneBuilder, times: Sequence[float], squints: Sequence[float], *, close_loop: bool = False) -> List[AnimationChannel]:
    if max((abs(s) for s in squints), default=0.0) < 1e-5:
        return []
    ang_max = _blink_closed_angle(scene)
    out: List[AnimationChannel] = []
    for lid_n in ("eyelid_left", "eyelid_right"):
        if lid_n in scene.nodes and scene.get_node(lid_n):
            r0 = _rest_r(scene, lid_n)
            qs = _align_quats([quat_mul(r0, quat_from_euler_xyz(rx=ang_max * float(sq))) for sq in squints], close_loop=close_loop)
            out.append(AnimationChannel(lid_n, "rotation", list(times), qs))
    return out


def _interp_rows(rows: Sequence[Tuple[float, ...]], u: float) -> Tuple[float, ...]:
    if u <= rows[0][0]:
        return rows[0]
    if u >= rows[-1][0]:
        return rows[-1]
    for i in range(len(rows) - 1):
        u0, u1 = rows[i][0], rows[i + 1][0]
        if u0 <= u <= u1:
            a = (u - u0) / max(1e-9, u1 - u0)
            return tuple((1.0 - a) * x + a * y for x, y in zip(rows[i], rows[i + 1]))
    return rows[-1]


# ---------------------------------------------------------------------------
# 1. Walk (`*wg`, 12 frames = 0.60 s seamless opposite-pair stride cycle)
# ---------------------------------------------------------------------------

def _add_walk_clip(scene: SceneBuilder, ant_class: str, node_verts: Dict[str, "np.ndarray"], legs: list) -> AnimationClip:
    dur = _DURATIONS["Walk"][ant_class]
    n_steps = 24
    times = [round(dur * (i / n_steps), 5) for i in range(n_steps + 1)]

    # Per-class gait flavor (stride_z, lift_y, bounce_y, sway_x, lean_rx, twist_ry, roll_rz, arm_swing).
    gait_cfg = {
        "worker": (0.064, 0.052, 0.017, 0.013, 0.11, 0.11, 0.055, 0.42),
        "combat": (0.068, 0.048, 0.015, 0.017, 0.08, 0.12, 0.060, 0.34),
        "thief": (0.058, 0.042, 0.013, 0.012, 0.13, 0.13, 0.050, 0.38),
        "bomber": (0.058, 0.046, 0.016, 0.014, 0.11, 0.10, 0.055, 0.36),
        "swimmer": (0.056, 0.050, 0.017, 0.017, 0.09, 0.11, 0.065, 0.34),
        "fire": (0.062, 0.050, 0.016, 0.013, 0.10, 0.11, 0.055, 0.38),
    }
    stride_z, lift_y, bounce_y, sway_x, lean_rx, twist_ry, roll_rz, arm_amp = gait_cfg.get(ant_class, gait_cfg["worker"])

    bx, by, bz = _rest_t(scene, "upper_body")
    r_ub0 = _rest_r(scene, "upper_body")
    r_hd0 = _rest_r(scene, "head_group")
    r_gs0 = _rest_r(scene, "gaster_group") if "gaster_group" in scene.nodes else None
    arms = _arm_pairs(scene)
    r_arms = {n: _rest_r(scene, n) for a, f, _ in arms for n in (a, f) if n}
    ant_nodes = [n for n in ("antenna_left", "antenna_right") if n in scene.nodes and scene.get_node(n)]
    r_ants = {n: _rest_r(scene, n) for n in ant_nodes}
    tail_nodes = [n for n in ("bandana_tails", "bandana_tail_lower") if n in scene.nodes and scene.get_node(n)]
    r_tails = {n: _rest_r(scene, n) for n in tail_nodes}

    ub_ts: List[Vec3] = []
    ub_rs: List[Quat] = []
    hd_rs: List[Quat] = []
    gs_rs: List[Quat] = []
    arm_rs: Dict[str, List[Quat]] = {n: [] for n in r_arms}
    ant_rs: Dict[str, List[Quat]] = {n: [] for n in ant_nodes}
    tail_rs: Dict[str, List[Quat]] = {n: [] for n in tail_nodes}
    leg_ts: Dict[str, List[Vec3]] = {info[0]: [] for info in legs}
    leg_rs: Dict[str, List[Quat]] = {info[0]: [] for info in legs}
    leg_ss: Dict[str, List[Vec3]] = {info[0]: [] for info in legs}

    for i in range(n_steps + 1):
        phi = 2.0 * math.pi * (i / n_steps)
        s1, c1 = math.sin(phi), math.cos(phi)
        s2, c2 = math.sin(2.0 * phi), math.cos(2.0 * phi)
        # Double-frequency cheerful walk bounce (peaks at mid-swing, dips slightly at foot plant).
        dy = -0.008 + bounce_y * (0.5 - 0.5 * c2)
        dx = sway_x * s1
        dz = 0.006 * s2
        t_ub = np.array([bx + dx, by + dy, bz + dz], dtype=np.float64)
        q_ub = quat_mul(r_ub0, quat_from_euler_xyz(rx=lean_rx + 0.045 * s2, ry=twist_ry * s1, rz=-roll_rz * s1))
        ub_ts.append((float(t_ub[0]), float(t_ub[1]), float(t_ub[2])))
        ub_rs.append(q_ub)

        # Head counter-stabilizes walk yaw/roll while bobbing briskly.
        hd_rs.append(quat_mul(r_hd0, quat_from_euler_xyz(rx=-0.06 - 0.04 * s2, ry=-0.55 * twist_ry * s1, rz=0.50 * roll_rz * s1)))
        if r_gs0 is not None:
            gs_rs.append(quat_mul(r_gs0, quat_from_euler_xyz(rx=-0.05 + 0.09 * c2, ry=-0.10 * s1)))

        # Natural opposite-arm counter-swing with elbow pump!
        for a_n, f_n, side in arms:
            k = _arm_scale(ant_class, side)
            # Left (+1) arm swings forward when right-mid leg swings forward (`-s1` phase).
            swing_a = -side * arm_amp * k * s1
            arm_rs[a_n].append(quat_mul(r_arms[a_n], quat_from_euler_xyz(rx=swing_a, ry=side * 0.08 * k * s1, rz=side * (0.06 + 0.05 * c2) * k)))
            if f_n:
                f_bend = (-0.16 - 0.20 * max(0.0, -swing_a)) * k
                arm_rs[f_n].append(quat_mul(r_arms[f_n], quat_from_euler_xyz(rx=f_bend)))

        for idx_a, an in enumerate(ant_nodes):
            sgn = +1.0 if idx_a == 0 else -1.0
            ant_rs[an].append(quat_mul(r_ants[an], quat_from_euler_xyz(rx=-0.10 * c2, rz=sgn * 0.08 * s1)))
        for idx_t, tn in enumerate(tail_nodes):
            tail_rs[tn].append(quat_mul(r_tails[tn], quat_from_euler_xyz(rx=0.10 * s2, ry=(0.14 if idx_t == 0 else -0.10) * s1)))

        # Alternating opposite-pair strides: (mid_left + rear_right) vs (mid_right + rear_left).
        for node, lt0, lr0, ls0, hip_loc, hip_in_ub, v_rest, foot_low, foot_rest_min_y, side, is_rear in legs:
            h_target = t_ub + _quat_rotate(q_ub, hip_in_ub)
            # Pair A (mid_left: side=+1,rear=False; rear_right: side=-1,rear=True) -> phase 0; Pair B -> pi.
            pair_b = (side < 0.0) ^ is_rear
            psi = phi + (math.pi if pair_b else 0.0)
            swing = max(0.0, math.sin(psi))
            z_off = -stride_z * math.cos(psi)
            y_off = lift_y * (swing ** 1.15)
            # Slight outward foot arc during swing so mid & rear legs never brush each other!
            x_off = side * 0.010 * swing
            t_cur = np.array([lt0[0] + x_off, lt0[1] + y_off, lt0[2] + z_off], dtype=np.float64)
            s_cur = ls0.copy()
            q_cur = lr0
            for step in range(4):
                d_vec = h_target - t_cur
                s_rel = _solve_knee_scale(hip_loc * ls0, float(v_rest[1]), d_vec, knee_bow=1.25, splay=0.35, flex_bias=0.06 * swing)
                s_cur = ls0 * s_rel
                v_scaled = _quat_rotate(lr0, hip_loc * s_cur)
                q_arc = _shortest_arc_quat(v_scaled, d_vec)
                # Subtle lively stride pitch tilt while swinging foot forward.
                q_cur = quat_mul(q_arc, lr0)
                if step < 3:
                    foot_now = _quat_rotate(q_cur, foot_low * s_cur)
                    min_world_y = float(t_cur[1] + foot_now[:, 1].min())
                    target_y = y_off
                    t_cur[1] += target_y - min_world_y
            # Final sole floor lock: stance foot sits flush at Y=0.0, swing foot strictly >= y_off!
            foot_final = _quat_rotate(q_cur, foot_low * s_cur)
            t_cur[1] += max(0.0, y_off) - float(t_cur[1] + foot_final[:, 1].min())
            # Re-aim once more after final Y clamp for sub-0.2 mm hip accuracy!
            d_vec = h_target - t_cur
            s_rel = _solve_knee_scale(hip_loc * ls0, float(v_rest[1]), d_vec, knee_bow=1.25, splay=0.35, flex_bias=0.06 * swing)
            s_cur = ls0 * s_rel
            q_cur = quat_mul(_shortest_arc_quat(_quat_rotate(lr0, hip_loc * s_cur), d_vec), lr0)
            foot_final2 = _quat_rotate(q_cur, foot_low * s_cur)
            floor_err = float(t_cur[1] + foot_final2[:, 1].min())
            if floor_err < 0.0:
                t_cur[1] -= floor_err

            leg_ts[node].append((float(t_cur[0]), float(t_cur[1]), float(t_cur[2])))
            leg_rs[node].append(q_cur)
            leg_ss[node].append((float(s_cur[0]), float(s_cur[1]), float(s_cur[2])))

    # Enforce exact periodic loop closure (frame -1 == frame 0).
    ub_ts[-1] = ub_ts[0]
    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), ub_ts),
        AnimationChannel("upper_body", "rotation", list(times), _align_quats(ub_rs, close_loop=True)),
        AnimationChannel("head_group", "rotation", list(times), _align_quats(hd_rs, close_loop=True)),
    ]
    if r_gs0 is not None:
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), _align_quats(gs_rs, close_loop=True)))
    for n, qs in arm_rs.items():
        channels.append(AnimationChannel(n, "rotation", list(times), _align_quats(qs, close_loop=True)))
    for n, qs in ant_rs.items():
        channels.append(AnimationChannel(n, "rotation", list(times), _align_quats(qs, close_loop=True)))
    for n, qs in tail_rs.items():
        channels.append(AnimationChannel(n, "rotation", list(times), _align_quats(qs, close_loop=True)))
    for info in legs:
        n = info[0]
        leg_ts[n][-1] = leg_ts[n][0]
        leg_ss[n][-1] = leg_ss[n][0]
        channels.append(AnimationChannel(n, "translation", list(times), leg_ts[n]))
        channels.append(AnimationChannel(n, "rotation", list(times), _align_quats(leg_rs[n], close_loop=True)))
        channels.append(AnimationChannel(n, "scale", list(times), leg_ss[n]))
    channels.extend(_hidden_prop_channels(scene, dur))
    return scene.add_animation("Walk", channels, replace=True)


# ---------------------------------------------------------------------------
# 2. Harvest (`*gf`, snappy forward stoop, reach & mandible chomp-scoop)
# ---------------------------------------------------------------------------

def _add_harvest_clip(scene: SceneBuilder, ant_class: str, node_verts: Dict[str, "np.ndarray"]) -> AnimationClip:
    dur = _DURATIONS["Harvest"][ant_class]
    # Limit forward stoop slightly on tall-helmet/goggle/shovel classes so face/gear stay clear of Y=0.
    stoop_scale = {"worker": 1.00, "combat": 0.78, "thief": 0.92, "bomber": 0.78, "swimmer": 0.74, "fire": 0.72}.get(ant_class, 0.85)
    # Rows: (u, dy, dz, ub_rx, hd_rx, gs_rx, arm_rx, arm_rz, fore_rx, mand_rx, mand_sx)
    key_rows = [
        (0.00, +0.000, +0.000, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, 1.00),
        (0.16, -0.014, +0.022, +0.34 * stoop_scale, +0.10, +0.10, +0.26, +0.16, -0.14, +0.18, 1.12),
        (0.36, -0.036, +0.056, +0.78 * stoop_scale, +0.16, +0.24, +0.58, +0.28, -0.26, +0.28, 1.18),
        (0.54, -0.044, +0.068, +0.94 * stoop_scale, +0.12, +0.30, +0.70, +0.18, -0.34, -0.20, 0.84),  # deep scoop & chomp shut!
        (0.70, -0.030, +0.048, +0.62 * stoop_scale, -0.04, +0.18, +0.44, +0.22, -0.22, -0.14, 0.90),  # lift food up
        (0.86, +0.008, +0.008, -0.08, -0.06, -0.06, -0.10, +0.10, -0.08, +0.04, 1.02),                # brisk upright rebound
        (1.00, +0.000, +0.000, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, 1.00),
    ]
    us = [round(i / 16.0, 5) for i in range(17)]
    times = [round(dur * u, 5) for u in us]
    samples = [_interp_rows(key_rows, u) for u in us]

    bx, by, bz = _rest_t(scene, "upper_body")
    r_ub0 = _rest_r(scene, "upper_body")
    r_hd0 = _rest_r(scene, "head_group")
    r_gs0 = _rest_r(scene, "gaster_group") if "gaster_group" in scene.nodes else None
    arms = _arm_pairs(scene)
    r_arms = {n: _rest_r(scene, n) for a, f, _ in arms for n in (a, f) if n}

    ub_ts: List[Vec3] = []
    ub_rs: List[Quat] = []
    hd_rs: List[Quat] = []
    gs_rs: List[Quat] = []
    arm_rs: Dict[str, List[Quat]] = {n: [] for n in r_arms}

    for row in samples:
        _, dy, dz, ub_rx, hd_rx, gs_rx, a_rx, a_rz, f_rx, _, _ = row
        q_ub = quat_mul(r_ub0, quat_from_euler_xyz(rx=ub_rx))
        q_hd = quat_mul(r_hd0, quat_from_euler_xyz(rx=hd_rx))
        ov: Dict[str, Dict[str, Tuple[float, ...]]] = {
            "upper_body": {"translation": (bx, by + dy, bz + dz), "rotation": q_ub},
            "head_group": {"rotation": q_hd},
        }
        if r_gs0 is not None:
            q_gs = quat_mul(r_gs0, quat_from_euler_xyz(rx=gs_rx))
            ov["gaster_group"] = {"rotation": q_gs}
            gs_rs.append(q_gs)
        for a_n, f_n, side in arms:
            k = _arm_scale(ant_class, side)
            qa = quat_mul(r_arms[a_n], quat_from_euler_xyz(rx=a_rx * k, rz=side * a_rz * k))
            ov[a_n] = {"rotation": qa}
            arm_rs[a_n].append(qa)
            if f_n:
                qf = quat_mul(r_arms[f_n], quat_from_euler_xyz(rx=f_rx * k))
                ov[f_n] = {"rotation": qf}
                arm_rs[f_n].append(qf)
        # Floor safety guard: if tall headgear, fists, or shovel dip near Y=0 during stoop, gently lift `dy`.
        min_y = _eval_pose_min_y(scene, ov, node_verts)
        if min_y < 0.006:
            dy += 0.008 - min_y
        ub_ts.append((bx, by + dy, bz + dz))
        ub_rs.append(q_ub)
        hd_rs.append(q_hd)

    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), ub_ts),
        AnimationChannel("upper_body", "rotation", list(times), _align_quats(ub_rs, close_loop=True)),
        AnimationChannel("head_group", "rotation", list(times), _align_quats(hd_rs, close_loop=True)),
    ]
    if r_gs0 is not None:
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), _align_quats(gs_rs, close_loop=True)))
    for n, qs in arm_rs.items():
        channels.append(AnimationChannel(n, "rotation", list(times), _align_quats(qs, close_loop=True)))
    if "mandibles" in scene.nodes and scene.get_node("mandibles"):
        r_m0 = _rest_r(scene, "mandibles")
        s_m0 = _rest_s(scene, "mandibles")
        channels.append(AnimationChannel("mandibles", "rotation", list(times), _align_quats([quat_mul(r_m0, quat_from_euler_xyz(rx=r[9])) for r in samples], close_loop=True)))
        channels.append(AnimationChannel("mandibles", "scale", list(times), [(s_m0[0] * r[10], s_m0[1], s_m0[2]) for r in samples]))
    for ant_n, s in (("antenna_left", +1.0), ("antenna_right", -1.0)):
        if ant_n in scene.nodes and scene.get_node(ant_n):
            r_a0 = _rest_r(scene, ant_n)
            channels.append(AnimationChannel(ant_n, "rotation", list(times), _align_quats([quat_mul(r_a0, quat_from_euler_xyz(rx=-0.26 * r[3], rz=s * 0.08 * r[3])) for r in samples], close_loop=True)))
    channels.extend(_hidden_prop_channels(scene, dur))
    clip = scene.add_animation("Harvest", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Harvest", swivel=0.0)
    return clip


# ---------------------------------------------------------------------------
# 3. CantGo (`*cg`, class-specific 1998 "nope / can't go!" gesture)
# ---------------------------------------------------------------------------

def _add_cantgo_clip(scene: SceneBuilder, ant_class: str) -> AnimationClip:
    dur = _DURATIONS["CantGo"][ant_class]
    # Columns: (u, dx, dy, ub_rx, ub_ry, ub_rz, hd_ry, hd_rz, arm_rx, arm_rz, fore_rx, squint)
    if ant_class == "worker":
        # Snappy quick left-right-left head shake NO + perked shrug (agcg301, 0.36s).
        rows = [
            (0.00, 0.000, 0.000, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00),
            (0.18, +0.006, +0.008, -0.04, +0.06, -0.04, +0.42, -0.10, -0.24, +0.34, -0.22, 0.12),
            (0.45, -0.006, +0.010, -0.06, -0.08, +0.05, -0.46, +0.12, -0.28, +0.40, -0.26, 0.16),
            (0.72, +0.004, +0.006, -0.04, +0.05, -0.03, +0.32, -0.08, -0.20, +0.30, -0.18, 0.10),
            (1.00, 0.000, 0.000, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00),
        ]
    elif ant_class == "combat":
        # Heavy brawler palms-out shrug + gruff head-shake NO & squint (accg301, 0.62s).
        rows = [
            (0.00, 0.000, 0.000, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00),
            (0.20, 0.000, +0.012, -0.08, +0.08, -0.04, +0.34, -0.08, -0.34, +0.48, -0.28, 0.34),
            (0.42, 0.000, +0.016, -0.10, -0.10, +0.05, -0.38, +0.10, -0.42, +0.56, -0.32, 0.42),
            (0.66, 0.000, +0.014, -0.08, +0.08, -0.04, +0.30, -0.08, -0.36, +0.50, -0.26, 0.36),
            (0.84, 0.000, +0.004, -0.02, -0.03, +0.02, -0.12, +0.03, -0.12, +0.18, -0.10, 0.12),
            (1.00, 0.000, 0.000, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00),
        ]
    elif ant_class == "thief":
        # Playful exasperated big side-to-side body tilt & arm-fling shrug (atcg301, 0.89s).
        rows = [
            (0.00, 0.000, 0.000, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00),
            (0.16, +0.014, +0.006, -0.06, +0.10, -0.18, +0.26, -0.14, -0.38, +0.54, -0.26, 0.10),
            (0.38, -0.016, +0.004, -0.08, -0.12, +0.24, -0.32, +0.18, -0.46, +0.64, -0.30, 0.16),
            (0.60, -0.020, +0.002, -0.06, -0.14, +0.28, -0.28, +0.22, -0.44, +0.62, -0.28, 0.18),
            (0.80, +0.010, +0.006, -0.04, +0.08, -0.12, +0.20, -0.10, -0.24, +0.36, -0.16, 0.08),
            (1.00, 0.000, 0.000, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00),
        ]
    elif ant_class == "bomber":
        # Long comic double head-shake NO + rocking goggle-bob shrug (abcg301, 1.38s).
        rows = [
            (0.00, 0.000, 0.000, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00),
            (0.14, +0.008, +0.008, -0.06, +0.08, -0.06, +0.38, -0.12, -0.28, +0.44, -0.24, 0.14),
            (0.32, -0.010, +0.010, -0.08, -0.10, +0.08, -0.42, +0.14, -0.34, +0.52, -0.28, 0.18),
            (0.50, +0.010, +0.008, -0.06, +0.10, -0.08, +0.40, -0.14, -0.32, +0.48, -0.26, 0.16),
            (0.68, -0.008, +0.010, -0.08, -0.08, +0.06, -0.38, +0.12, -0.30, +0.46, -0.24, 0.16),
            (0.84, +0.005, +0.004, -0.04, +0.04, -0.04, +0.20, -0.06, -0.16, +0.26, -0.14, 0.08),
            (1.00, 0.000, 0.000, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00),
        ]
    elif ant_class == "swimmer":
        # Expressive scuba-mask head roll side-to-side + nope shrug (ascg301, 0.96s).
        rows = [
            (0.00, 0.000, 0.000, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00),
            (0.18, +0.010, +0.006, -0.04, +0.08, -0.08, +0.24, -0.28, -0.30, +0.46, -0.22, 0.12),
            (0.42, -0.012, +0.008, -0.06, -0.10, +0.10, -0.28, +0.32, -0.36, +0.54, -0.26, 0.16),
            (0.66, +0.010, +0.006, -0.04, +0.08, -0.08, +0.22, -0.26, -0.28, +0.44, -0.20, 0.12),
            (0.84, -0.004, +0.002, -0.02, -0.04, +0.04, -0.10, +0.12, -0.12, +0.20, -0.10, 0.05),
            (1.00, 0.000, 0.000, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00),
        ]
    else:
        # Fire: emphatic fire-helmet shake NO + crisp hands-out shrug (afcg301, 0.66s).
        rows = [
            (0.00, 0.000, 0.000, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00),
            (0.18, +0.008, +0.008, -0.05, +0.08, -0.06, +0.36, -0.14, -0.28, +0.42, -0.24, 0.14),
            (0.40, -0.010, +0.010, -0.06, -0.10, +0.08, -0.40, +0.16, -0.34, +0.48, -0.28, 0.18),
            (0.64, +0.008, +0.008, -0.05, +0.08, -0.06, +0.34, -0.14, -0.28, +0.42, -0.22, 0.14),
            (0.84, -0.004, +0.003, -0.02, -0.04, +0.03, -0.16, +0.06, -0.12, +0.18, -0.10, 0.06),
            (1.00, 0.000, 0.000, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00),
        ]

    times = [round(dur * r[0], 5) for r in rows]
    bx, by, bz = _rest_t(scene, "upper_body")
    r_ub0 = _rest_r(scene, "upper_body")
    r_hd0 = _rest_r(scene, "head_group")
    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), [(bx + r[1], by + r[2], bz) for r in rows]),
        AnimationChannel("upper_body", "rotation", list(times), _align_quats([quat_mul(r_ub0, quat_from_euler_xyz(rx=r[3], ry=r[4], rz=r[5])) for r in rows], close_loop=True)),
        AnimationChannel("head_group", "rotation", list(times), _align_quats([quat_mul(r_hd0, quat_from_euler_xyz(ry=r[6], rz=r[7])) for r in rows], close_loop=True)),
    ]
    if "gaster_group" in scene.nodes:
        r_gs0 = _rest_r(scene, "gaster_group")
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), _align_quats([quat_mul(r_gs0, quat_from_euler_xyz(ry=-0.5 * r[4], rz=-0.6 * r[5])) for r in rows], close_loop=True)))
    for a_n, f_n, side in _arm_pairs(scene):
        k = _arm_scale(ant_class, side)
        r_a0 = _rest_r(scene, a_n)
        channels.append(AnimationChannel(a_n, "rotation", list(times), _align_quats([quat_mul(r_a0, quat_from_euler_xyz(rx=r[8] * k, rz=side * r[9] * k)) for r in rows], close_loop=True)))
        if f_n:
            r_f0 = _rest_r(scene, f_n)
            channels.append(AnimationChannel(f_n, "rotation", list(times), _align_quats([quat_mul(r_f0, quat_from_euler_xyz(rx=r[10] * k)) for r in rows], close_loop=True)))
    for ant_n, s in (("antenna_left", +1.0), ("antenna_right", -1.0)):
        if ant_n in scene.nodes and scene.get_node(ant_n):
            r_an0 = _rest_r(scene, ant_n)
            channels.append(AnimationChannel(ant_n, "rotation", list(times), _align_quats([quat_mul(r_an0, quat_from_euler_xyz(rx=0.12 * abs(r[6]), rz=s * 0.28 * r[6])) for r in rows], close_loop=True)))
    for tn, mult in (("bandana_tails", 1.0), ("bandana_tail_lower", 0.8)):
        if tn in scene.nodes and scene.get_node(tn):
            r_t0 = _rest_r(scene, tn)
            channels.append(AnimationChannel(tn, "rotation", list(times), _align_quats([quat_mul(r_t0, quat_from_euler_xyz(ry=-0.55 * mult * r[6], rz=0.45 * mult * r[7])) for r in rows], close_loop=True)))
    for side in ("left", "right"):
        ball = f"eye_ball_{side}"
        if ball in scene.nodes:
            r_b0 = _rest_r(scene, ball)
            channels.append(AnimationChannel(ball, "rotation", list(times), _align_quats([quat_mul(r_b0, quat_from_axis_angle((0.0, 1.0, 0.0), 0.35 * r[6])) for r in rows], close_loop=True)))
    channels.extend(_eyelid_squint_channels(scene, times, [r[11] for r in rows], close_loop=True))
    channels.extend(_hidden_prop_channels(scene, dur))
    clip = scene.add_animation("CantGo", channels, replace=True)
    sync_leg_flex_to_clip(scene, "CantGo", swivel=0.0)
    return clip


# ---------------------------------------------------------------------------
# 4 & 5. Enter (`*en`) and Hatch (`*hatch`) — floor-clamped burrow dive & pop
# ---------------------------------------------------------------------------

def _build_burrow_clip(
    scene: SceneBuilder,
    ant_class: str,
    clip_name: str,
    key_rows: Sequence[Tuple[float, ...]],
    node_verts: Dict[str, "np.ndarray"],
    legs: list,
    *,
    close_loop: bool,
) -> AnimationClip:
    """Shared exact floor-safe (`min_y >= 0.0`), zero-hip-gap builder for `"Enter"` & `"Hatch"`.

    Row columns: `(u, sx, sy, sz, ub_rx, hd_rx, gs_rx, arm_rx, arm_rz, leg_kick, air_w)`
    """
    dur = _DURATIONS[clip_name][ant_class]
    # Dense uniform sampling (36 segments) prevents chord-dipping & keeps hip error sub-mm during dives.
    n_sub = 36
    us = [i / n_sub for i in range(n_sub + 1)]
    times = [round(dur * u, 5) for u in us]
    samples = [_interp_rows(key_rows, u) for u in us]

    bx, by, bz = _rest_t(scene, "upper_body")
    r_ub0 = _rest_r(scene, "upper_body")
    s_ub0 = np.asarray(_rest_s(scene, "upper_body"), dtype=np.float64)
    r_hd0 = _rest_r(scene, "head_group")
    r_gs0 = _rest_r(scene, "gaster_group") if "gaster_group" in scene.nodes else None
    arms = _arm_pairs(scene)
    r_arms = {n: _rest_r(scene, n) for a, f, _ in arms for n in (a, f) if n}
    ant_nodes = [n for n in ("antenna_left", "antenna_right") if n in scene.nodes and scene.get_node(n)]
    r_ants = {n: _rest_r(scene, n) for n in ant_nodes}

    ub_ts: List[Vec3] = []
    ub_rs: List[Quat] = []
    ub_ss: List[Vec3] = []
    hd_rs: List[Quat] = []
    gs_rs: List[Quat] = []
    arm_rs: Dict[str, List[Quat]] = {n: [] for n in r_arms}
    ant_rs: Dict[str, List[Quat]] = {n: [] for n in ant_nodes}
    leg_ts: Dict[str, List[Vec3]] = {info[0]: [] for info in legs}
    leg_rs: Dict[str, List[Quat]] = {info[0]: [] for info in legs}
    leg_ss: Dict[str, List[Vec3]] = {info[0]: [] for info in legs}

    for row in samples:
        _, sx, sy, sz, ub_rx, hd_rx, gs_rx, a_rx, a_rz, kick, air_w = row
        s_vec = np.array([sx, sy, sz], dtype=np.float64)
        s_ub = s_ub0 * s_vec
        t_ub = np.array([bx * sx, by * sy, bz * sz], dtype=np.float64)
        q_ub = quat_mul(r_ub0, quat_from_euler_xyz(rx=ub_rx))
        q_delta_ub = quat_mul(q_ub, _quat_conj(r_ub0))
        q_hd = quat_mul(r_hd0, quat_from_euler_xyz(rx=hd_rx))

        ov: Dict[str, Dict[str, Tuple[float, ...]]] = {
            "upper_body": {"translation": tuple(float(c) for c in t_ub), "rotation": q_ub, "scale": tuple(float(c) for c in s_ub)},
            "head_group": {"rotation": q_hd},
        }
        if r_gs0 is not None:
            q_gs = quat_mul(r_gs0, quat_from_euler_xyz(rx=gs_rx))
            ov["gaster_group"] = {"rotation": q_gs}
            gs_rs.append(q_gs)
        for a_n, f_n, side in arms:
            k = _arm_scale(ant_class, side)
            qa = quat_mul(r_arms[a_n], quat_from_euler_xyz(rx=a_rx * k, rz=side * a_rz * k))
            ov[a_n] = {"rotation": qa}
            arm_rs[a_n].append(qa)
            if f_n:
                qf = quat_mul(r_arms[f_n], quat_from_euler_xyz(rx=-0.22 * abs(a_rx) * k))
                ov[f_n] = {"rotation": qf}
                arm_rs[f_n].append(qf)
        for idx_a, an in enumerate(ant_nodes):
            sgn = +1.0 if idx_a == 0 else -1.0
            q_an = quat_mul(r_ants[an], quat_from_euler_xyz(rx=-0.16 * ub_rx, rz=sgn * 0.10 * abs(ub_rx)))
            ov[an] = {"rotation": q_an}
            ant_rs[an].append(q_an)

        # Lock every leg's hip onto scaled/pitched `upper_body` (`0.000 mm` separation!).
        # When `air_w == 0` and upright, legs stay grounded; during burrow dive (`air_w > 0`) they
        # kick skyward beside the diving gaster just like `ORIG agen301..afen301` & `*hatch`!
        frame_legs: Dict[str, Tuple[np.ndarray, Quat, np.ndarray]] = {}
        for node, lt0, lr0, ls0, hip_loc, hip_in_ub, v_rest, foot_low, foot_rest_min_y, side, is_rear in legs:
            h_target = t_ub + _quat_rotate(q_ub, s_vec * hip_in_ub)
            s_leg = ls0 * s_vec
            if air_w <= 1e-4:
                t_cur = np.array([lt0[0] * sx, 0.0, lt0[2] * sz], dtype=np.float64)
                d_vec = h_target - t_cur
                s_rel = _solve_knee_scale(hip_loc * s_leg, max(0.02, float(v_rest[1]) * sy), d_vec, knee_bow=1.2, splay=0.30)
                s_use = s_leg * s_rel
                q_use = quat_mul(_shortest_arc_quat(_quat_rotate(lr0, hip_loc * s_use), d_vec), lr0)
                foot_now = _quat_rotate(q_use, foot_low * s_use)
                t_cur[1] = -float(foot_now[:, 1].min())
            else:
                # Legs follow body dive & kick upward beside the skyward gaster!
                kick_rx = kick * (-0.55 if is_rear else -0.85)
                kick_rz = side * kick * (0.30 if is_rear else 0.42)
                q_kick = quat_from_euler_xyz(rx=kick_rx, rz=kick_rz)
                q_use = quat_mul(q_delta_ub, quat_mul(q_kick, lr0))
                s_use = s_leg
                t_cur = h_target - _quat_rotate(q_use, hip_loc * s_use)
            frame_legs[node] = (t_cur, q_use, s_use)
            ov[node] = {"translation": tuple(float(c) for c in t_cur), "rotation": q_use, "scale": tuple(float(c) for c in s_use)}

        # Exact floor-clamp across EVERY visible vertex -> `min_y >= 0.000` (zero underground poke!).
        cur_min_y = _eval_pose_min_y(scene, ov, node_verts)
        if air_w > 1e-4:
            lift = 0.0015 - cur_min_y
        elif cur_min_y < 0.0:
            lift = -cur_min_y
        else:
            lift = 0.0
        t_ub[1] += lift
        ub_ts.append((float(t_ub[0]), float(t_ub[1]), float(t_ub[2])))
        ub_rs.append(q_ub)
        ub_ss.append((float(s_ub[0]), float(s_ub[1]), float(s_ub[2])))
        hd_rs.append(q_hd)
        for node, (t_l, q_l, s_l) in frame_legs.items():
            t_l[1] += lift
            leg_ts[node].append((float(t_l[0]), float(t_l[1]), float(t_l[2])))
            leg_rs[node].append(q_l)
            leg_ss[node].append((float(s_l[0]), float(s_l[1]), float(s_l[2])))

    if close_loop:
        ub_ts[-1], ub_ss[-1] = ub_ts[0], ub_ss[0]
        for info in legs:
            n = info[0]
            leg_ts[n][-1], leg_ss[n][-1] = leg_ts[n][0], leg_ss[n][0]

    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), ub_ts),
        AnimationChannel("upper_body", "rotation", list(times), _align_quats(ub_rs, close_loop=close_loop)),
        AnimationChannel("upper_body", "scale", list(times), ub_ss),
        AnimationChannel("head_group", "rotation", list(times), _align_quats(hd_rs, close_loop=close_loop)),
    ]
    if r_gs0 is not None:
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), _align_quats(gs_rs, close_loop=close_loop)))
    for n, qs in arm_rs.items():
        channels.append(AnimationChannel(n, "rotation", list(times), _align_quats(qs, close_loop=close_loop)))
    for n, qs in ant_rs.items():
        channels.append(AnimationChannel(n, "rotation", list(times), _align_quats(qs, close_loop=close_loop)))
    for info in legs:
        n = info[0]
        channels.append(AnimationChannel(n, "translation", list(times), leg_ts[n]))
        channels.append(AnimationChannel(n, "rotation", list(times), _align_quats(leg_rs[n], close_loop=close_loop)))
        channels.append(AnimationChannel(n, "scale", list(times), leg_ss[n]))
    channels.extend(_hidden_prop_channels(scene, dur))
    return scene.add_animation(clip_name, channels, replace=True)


def _add_enter_clip(scene: SceneBuilder, ant_class: str, node_verts: Dict[str, "np.ndarray"], legs: list) -> AnimationClip:
    # Authentic `agen301..afen301`: dive head-first into burrow -> butt & legs sticking skyward ->
    # shrink flush into hole at Y=0 -> emerge cleanly back upright so 1998 comparison loops in sync!
    min_sc = 0.42 if ant_class == "fire" else 0.18
    rows = [
        (0.00, 1.00, 1.00, 1.00, +0.00, +0.00, +0.00, +0.00, +0.00, 0.00, 0.00),
        (0.12, 0.96, 0.92, 0.96, +0.58, +0.12, +0.16, +0.44, +0.34, 0.20, 0.35),
        (0.28, 0.78, 0.80, 0.78, +1.36, +0.18, +0.34, +0.62, +0.56, 0.52, 1.00),  # head-first burrow dive!
        (0.44, min_sc, min_sc, min_sc, +1.50, +0.14, +0.40, +0.50, +0.48, 0.64, 1.00),  # tucked into hole at Y=0
        (0.56, min_sc, min_sc, min_sc, +1.50, +0.14, +0.40, +0.50, +0.48, 0.64, 1.00),  # brief burrow pause
        (0.72, 0.78, 0.80, 0.78, +1.36, +0.18, +0.34, +0.62, +0.56, 0.52, 1.00),  # reversing back up
        (0.88, 0.96, 0.92, 0.96, +0.58, +0.12, +0.16, +0.44, +0.34, 0.20, 0.35),
        (1.00, 1.00, 1.00, 1.00, +0.00, +0.00, +0.00, +0.00, +0.00, 0.00, 0.00),
    ]
    return _build_burrow_clip(scene, ant_class, "Enter", rows, node_verts, legs, close_loop=True)


def _add_hatch_clip(scene: SceneBuilder, ant_class: str, node_verts: Dict[str, "np.ndarray"], legs: list) -> AnimationClip:
    # Authentic `*hatch`: pop upward from tiny/tucked burrow at Y=0 with bouncy squash-and-stretch,
    # flailing arms & kicking legs tumbling cleanly upright into rest pose!
    start_sc = 0.28 if ant_class == "fire" else 0.08
    rows = [
        (0.00, start_sc, start_sc, start_sc, +1.46, +0.12, +0.38, +0.48, +0.68, 0.62, 1.00),
        (0.18, 0.44, 0.52, 0.44, +1.34, +0.16, +0.34, +0.52, +0.76, 0.56, 1.00),  # bursting up out of burrow!
        (0.38, 0.78, 0.86, 0.78, +0.96, +0.10, +0.24, +0.26, +0.86, 0.40, 1.00),  # tumbling upright, arms flailing!
        (0.58, 0.96, 1.07, 0.96, +0.26, -0.06, +0.06, -0.28, +0.64, 0.16, 0.45),  # energetic stretch overshoot!
        (0.76, 1.05, 0.94, 1.05, -0.10, -0.08, -0.08, -0.18, +0.34, 0.00, 0.00),  # bouncy landing squash!
        (0.90, 0.99, 1.02, 0.99, +0.02, +0.00, +0.00, -0.04, +0.08, 0.00, 0.00),
        (1.00, 1.00, 1.00, 1.00, +0.00, +0.00, +0.00, +0.00, +0.00, 0.00, 0.00),  # exact upright rest pose!
    ]
    return _build_burrow_clip(scene, ant_class, "Hatch", rows, node_verts, legs, close_loop=False)


# ---------------------------------------------------------------------------
# Public entrypoint hooked by `hit_clips.py` & `hit_clips_special.py`
# ---------------------------------------------------------------------------

def add_shared_loco_clips(scene: SceneBuilder, ant_class: str) -> List[AnimationClip]:
    """Attach authentic 1998 `"Walk"`, `"Harvest"`, `"CantGo"`, `"Enter"`, and `"Hatch"` clips."""
    cls = str(ant_class).strip().lower()
    if cls not in _DURATIONS["Walk"]:
        raise ValueError(f"Unknown ant_class {ant_class!r}")
    node_verts = _visible_node_verts(scene)
    legs = _leg_infos(scene, node_verts)
    return [
        _add_walk_clip(scene, cls, node_verts, legs),
        _add_harvest_clip(scene, cls, node_verts),
        _add_cantgo_clip(scene, cls),
        _add_enter_clip(scene, cls, node_verts, legs),
        _add_hatch_clip(scene, cls, node_verts, legs),
    ]


__all__ = ["add_shared_loco_clips"]
