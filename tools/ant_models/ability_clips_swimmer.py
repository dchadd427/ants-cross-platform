"""Authentic 1998 class-specific ability animations for Swimmer Ant.

Split into a dedicated module to keep `ant_swimmer.py` well under per-file line
limits while staying pure-stdlib safe on import (CI without numpy).

Exact 1998 CHD Swimmer ability animations (`docs/chd_table4_animations.json`):
- `"Swim"`        (`assw`, 12f, 0.48 s, seamless loop):
    horizontal prone swimming posture, upright snorkel bob, fluttering flippers & freestyle arm strokes
- `"Dive"`        (`asdi`, 16f, 1.04 s):
    springy crouch -> explosive upward arch -> head-first swan plunge -> aqua splash ring entry
- `"Surface"`     (`asgo`,  7f, 0.42 s):
    emerge upward out of water -> energetic hop -> clean upright landing on all 4 forward flippers
- `"Shovel"`      (`asbbl`, 8f, 0.48 s):
    vigorous rhythmic beach-shovel digging & side/forward gravel toss on grounded flippers
- `"ShovelWater"` (`asbbw`, 8f, 0.50 s):
    deeper low water-scooping thrust & snappy outward aqua-spray toss flick
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
        from .ability_clips_special import ensure_ability_root, quat_track, rest_r, rest_t
        from .base_ant import sync_leg_flex_to_clip
        from .mesh_core import (
            AnimationChannel,
            AnimationClip,
            MeshPart,
            SceneBuilder,
            ellipsoid,
            quat_from_euler_xyz,
            quat_mul,
            sphere,
            torus_arc,
        )
    except ImportError:
        from ability_clips_special import ensure_ability_root, quat_track, rest_r, rest_t  # type: ignore
        from base_ant import sync_leg_flex_to_clip  # type: ignore
        from mesh_core import (  # type: ignore
            AnimationChannel,
            AnimationClip,
            MeshPart,
            SceneBuilder,
            ellipsoid,
            quat_from_euler_xyz,
            quat_mul,
            sphere,
            torus_arc,
        )

Vec3 = Tuple[float, float, float]
Quat = Tuple[float, float, float, float]
_TINY: Vec3 = (1e-4, 1e-4, 1e-4)
_ONE: Vec3 = (1.0, 1.0, 1.0)


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


def _sample_ch(ch: AnimationChannel, t: float):
    ts = ch.times
    if t <= ts[0]:
        return ch.values[0]
    if t >= ts[-1]:
        return ch.values[-1]
    lo, hi = 0, len(ts) - 1
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
    a, b = ch.values[lo], ch.values[lo + 1]
    return (a[0] + u * (b[0] - a[0]), a[1] + u * (b[1] - a[1]), a[2] + u * (b[2] - a[2]))


def _visible_body_samples(scene: SceneBuilder, skip_nodes: Sequence[str] = ()) -> Dict[str, np.ndarray]:
    """Collect compact vertex subsets for all rest-visible Swimmer body nodes."""
    skip = set(skip_nodes)
    out: Dict[str, np.ndarray] = {}
    for node, parts in scene.nodes.items():
        if not parts or node in skip:
            continue
        xf = scene.get_node_transform(node)
        if xf is not None and max(abs(c) for c in xf.scale) < 0.01:
            continue
        pts = np.vstack([p.positions for p in parts]).astype(np.float64)
        if len(pts) > 280:
            step = max(1, len(pts) // 220)
            idx = set(range(0, len(pts), step))
            for ax in range(3):
                idx.add(int(np.argmin(pts[:, ax])))
                idx.add(int(np.argmax(pts[:, ax])))
            pts = pts[sorted(idx)]
        out[node] = pts
    return out


def _eval_min_y(
    scene: SceneBuilder,
    ch_by_node: Dict[str, Dict[str, AnimationChannel]],
    samples: Dict[str, np.ndarray],
    t: float,
    root_ov: Optional[Tuple[Vec3, Quat, Vec3]] = None,
) -> float:
    wmats: Dict[str, np.ndarray] = {}

    def _wmat(n: str) -> np.ndarray:
        if n in wmats:
            return wmats[n]
        xf = scene.get_node_transform(n)
        tr = xf.translation if xf is not None else (0.0, 0.0, 0.0)
        rt = xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)
        sc = xf.scale if xf is not None else (1.0, 1.0, 1.0)
        if n == "root" and root_ov is not None:
            tr, rt, sc = root_ov
        elif n in ch_by_node:
            cm = ch_by_node[n]
            if "translation" in cm:
                tr = _sample_ch(cm["translation"], t)
            if "rotation" in cm:
                rt = _sample_ch(cm["rotation"], t)
            if "scale" in cm:
                sc = _sample_ch(cm["scale"], t)
        mat = np.eye(4, dtype=np.float64)
        mat[:3, :3] = _quat_to_mat3(rt) * np.asarray(sc, dtype=np.float64)[None, :]
        mat[:3, 3] = tr
        par = scene.get_node_parent(n)
        res = (_wmat(par) @ mat) if par is not None else mat
        wmats[n] = res
        return res

    min_y = 1e9
    for node, pts in samples.items():
        m = _wmat(node)
        my = float((pts @ m[1, :3] + m[1, 3]).min())
        if my < min_y:
            min_y = my
    return min_y


def _solve_root_pose(
    scene: SceneBuilder,
    clip: AnimationClip,
    times: Sequence[float],
    root_eulers: Sequence[Vec3],
    root_z_com: Sequence[float],
    min_air_y: Sequence[float],
    root_scales: Optional[Sequence[Vec3]] = None,
    *,
    com_y: float = 0.45,
    close_loop: bool = True,
) -> None:
    """Attach exact floor-safe, COM-pivoted `"root"` channels to `clip` (strictly `min_y >= 0.0`)."""
    ch_by_node: Dict[str, Dict[str, AnimationChannel]] = {}
    for ch in clip.channels:
        if ch.target_node != "root":
            ch_by_node.setdefault(ch.target_node, {})[ch.path] = ch
    samples = _visible_body_samples(scene)
    r_vals = quat_track((0.0, 0.0, 0.0, 1.0), root_eulers, close_loop=close_loop)
    t_list = [float(x) for x in times]
    z_list = [float(z) for z in root_z_com]
    air_list = [float(y) for y in min_air_y]
    sc_list = list(root_scales) if root_scales is not None else [_ONE] * len(t_list)

    pos_list: List[Vec3] = []
    for k in range(len(t_list)):
        q_k, s_k = r_vals[k], sc_list[k]
        is_id = abs(q_k[0]) + abs(q_k[1]) + abs(q_k[2]) < 1e-6 and all(abs(c - 1.0) < 1e-6 for c in s_k)
        if is_id and abs(z_list[k]) < 1e-6 and air_list[k] <= 1e-6:
            my0 = _eval_min_y(scene, ch_by_node, samples, t_list[k], ((0.0, 0.0, 0.0), q_k, s_k))
            pos_list.append((0.0, max(0.0, -my0), 0.0))
        else:
            rot_m = _quat_to_mat3(q_k)
            com_rot = rot_m @ np.array([0.0, com_y * s_k[1], 0.0], dtype=np.float64)
            tz = z_list[k] - float(com_rot[2])
            my0 = _eval_min_y(scene, ch_by_node, samples, t_list[k], ((0.0, 0.0, tz), q_k, s_k))
            pos_list.append((0.0, float(air_list[k] - my0), float(tz)))
    if close_loop:
        pos_list[-1] = pos_list[0]

    # Guard against SLERP chord sag between keyframes.
    rt_ch = AnimationChannel("root", "translation", list(t_list), list(pos_list))
    rr_ch = AnimationChannel("root", "rotation", list(t_list), list(r_vals))
    ch_by_node["root"] = {"translation": rt_ch, "rotation": rr_ch}
    if root_scales is not None:
        rs_ch = AnimationChannel("root", "scale", list(t_list), list(sc_list))
        ch_by_node["root"]["scale"] = rs_ch
    for _ in range(3):
        lifted = False
        for i in range(len(t_list) - 1):
            for sub in (0.25, 0.50, 0.75):
                tm = t_list[i] + sub * (t_list[i + 1] - t_list[i])
                my = _eval_min_y(scene, ch_by_node, samples, tm)
                if my < -0.0005:
                    lift = -my + 0.0015
                    if not (close_loop and i == 0):
                        px, py, pz = pos_list[i]
                        pos_list[i] = (px, py + lift, pz)
                    if not (close_loop and i + 1 == len(pos_list) - 1):
                        px, py, pz = pos_list[i + 1]
                        pos_list[i + 1] = (px, py + lift, pz)
                    lifted = True
        if not lifted:
            break
        if close_loop:
            pos_list[-1] = pos_list[0]
        rt_ch.values = list(pos_list)

    clip.channels = [ch for ch in clip.channels if ch.target_node != "root"]
    if root_scales is not None:
        clip.channels.insert(0, AnimationChannel("root", "scale", list(t_list), list(sc_list)))
    clip.channels.insert(0, AnimationChannel("root", "rotation", list(t_list), list(r_vals)))
    clip.channels.insert(0, AnimationChannel("root", "translation", list(t_list), list(pos_list)))


def _add_swimmer_ability_props(scene: SceneBuilder) -> Tuple[str, str, str, Vec3]:
    """Add hidden Swimmer splash-ring, gravel-toss, and water-toss FX props (`scale=1e-4` inside thorax)."""
    bx, by, bz = rest_t(scene, "upper_body")
    hide_origin: Vec3 = (bx, by, bz)

    # 1. Aqua splash crown ring for Dive (`asdi` f9-f13), resting flat with bottom at local Y = 0.
    splash_node = "swimmer_splash_ring"
    sp_parts: List[MeshPart] = [
        torus_arc(0.24, 0.032, u_segs=20, v_segs=8, plane="xz", name="dive_ring_outer", material="glass_tinted").translate(0.0, 0.034, 0.0),
        torus_arc(0.14, 0.024, u_segs=16, v_segs=8, plane="xz", name="dive_ring_inner", material="glass_clear").translate(0.0, 0.026, 0.0),
    ]
    for k in range(8):
        ang = (k / 8.0) * 2.0 * math.pi
        r_ring = 0.21 if k % 2 == 0 else 0.16
        h_drop = 0.14 if k % 2 == 0 else 0.10
        mat = "glass_clear" if k % 2 == 0 else "glass_tinted"
        sp_parts.append(
            ellipsoid(0.026, 0.044, 0.026, u_segs=10, v_segs=8, name=f"dive_splash_{k}", material=mat)
            .translate(r_ring * math.cos(ang), h_drop, r_ring * math.sin(ang))
        )
    for p in sp_parts:
        scene.add(splash_node, p)
    # Keep splash ring at top-level (not parented to root) so it stays flat on Y=0 while root dives & scales!
    scene.set_node_transform(splash_node, translation=hide_origin, scale=_TINY)

    # 2. Earth/gravel clods tossed off the beach shovel during Shovel (`asbbl`).
    dirt_node = "swimmer_dirt_toss"
    d_parts: List[MeshPart] = [
        ellipsoid(0.054, 0.036, 0.048, u_segs=12, v_segs=8, name="dirt_clod_main", material="prop_brown"),
        sphere(0.030, u_segs=10, v_segs=8, name="dirt_clod_a", material="prop_brown").translate(+0.052, +0.022, +0.036),
        sphere(0.024, u_segs=10, v_segs=8, name="dirt_clod_b", material="gear_dark").translate(-0.044, +0.034, +0.052),
        sphere(0.020, u_segs=8, v_segs=6, name="dirt_clod_c", material="prop_brown").translate(+0.018, -0.016, +0.068),
    ]
    for p in d_parts:
        scene.add(dirt_node, p)
    scene.set_node_parent(dirt_node, "root")
    scene.set_node_transform(dirt_node, translation=hide_origin, scale=_TINY)

    # 3. Aqua spray fan tossed off the beach shovel during ShovelWater (`asbbw`).
    water_node = "swimmer_water_toss"
    w_parts: List[MeshPart] = [
        ellipsoid(0.068, 0.046, 0.082, u_segs=14, v_segs=10, name="water_toss_core", material="glass_tinted"),
        ellipsoid(0.044, 0.034, 0.056, u_segs=12, v_segs=8, name="water_toss_foam", material="glass_clear").translate(0.0, +0.026, +0.042),
        sphere(0.026, u_segs=10, v_segs=8, name="water_toss_drop_a", material="glass_clear").translate(+0.064, +0.036, +0.072),
        sphere(0.024, u_segs=10, v_segs=8, name="water_toss_drop_b", material="glass_tinted").translate(-0.058, +0.028, +0.080),
        sphere(0.020, u_segs=8, v_segs=6, name="water_toss_drop_c", material="glass_clear").translate(+0.012, +0.058, +0.092),
    ]
    for p in w_parts:
        scene.add(water_node, p)
    scene.set_node_parent(water_node, "root")
    scene.set_node_transform(water_node, translation=hide_origin, scale=_TINY)
    return splash_node, dirt_node, water_node, hide_origin


def _add_swimmer_swim(scene: SceneBuilder) -> AnimationClip:
    """Attach Swimmer's authentic 1998 0.48s (`assw`, 12f) prone horizontal `"Swim"` loop."""
    dur = 0.48
    n_steps = 12
    times = [round(dur * k / n_steps, 5) for k in range(n_steps + 1)]

    # Horizontal prone swimming posture (~66 deg forward pitch) with gentle buoyancy bob & freestyle roll,
    # keeping snorkel upright and yellow shovel cleanly tucked alongside the body!
    root_eulers: List[Vec3] = []
    root_z_com: List[float] = []
    min_air_y: List[float] = []
    head_eulers: List[Vec3] = []
    gaster_eulers: List[Vec3] = []
    al_eulers: List[Vec3] = []
    fl_eulers: List[Vec3] = []
    ar_eulers: List[Vec3] = []
    fr_eulers: List[Vec3] = []
    legs_eulers: List[Vec3] = []
    ant_l_eulers: List[Vec3] = []
    ant_r_eulers: List[Vec3] = []

    for k in range(n_steps + 1):
        ph = 2.0 * math.pi * (k / n_steps)
        s1, c1 = math.sin(ph), math.cos(ph)
        s2 = math.sin(2.0 * ph)
        root_eulers.append((+1.16 + 0.04 * c1, 0.04 * s1, 0.09 * s1))
        root_z_com.append(0.015 * s2)
        min_air_y.append(0.115 + 0.024 * s1)
        # Head lifts cleanly forward so mask looks ahead & yellow snorkel stands vertically upright!
        head_eulers.append((-0.78 - 0.05 * c1, -0.05 * s1, -0.06 * s1))
        gaster_eulers.append((-0.34 + 0.14 * s2, 0.0, 0.0))
        # Left arm executes a smooth rhythmic freestyle/breaststroke paddle stroke!
        al_eulers.append((-0.65 + 0.52 * s1, -0.25 + 0.34 * c1, +0.45 + 0.28 * c1))
        fl_eulers.append((-0.35 + 0.30 * c1, +0.20 * s1, +0.16 * s1))
        # Right arm carries the yellow beach shovel cleanly tucked & paddling clear of body & legs!
        ar_eulers.append((-0.88 - 0.24 * s1, -0.10 + 0.10 * c1, -0.34 - 0.12 * c1))
        fr_eulers.append((-0.26 - 0.14 * c1, 0.0, -0.10))
        # Dolphin-kick flutter on `legs` (each flipper rides directly on its individual `leg_*` foot node).
        kick = 0.13 * s2
        legs_eulers.append((kick, 0.0, 0.05 * s1))
        ant_l_eulers.append((-0.24 + 0.10 * c1, 0.0, +0.08 * s1))
        ant_r_eulers.append((-0.24 - 0.10 * c1, 0.0, -0.08 * s1))

    channels: List[AnimationChannel] = [
        AnimationChannel("head_group", "rotation", list(times), quat_track(rest_r(scene, "head_group"), head_eulers)),
    ]
    if "gaster_group" in scene.nodes:
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), quat_track(rest_r(scene, "gaster_group"), gaster_eulers)))
    if "arm_left" in scene.nodes:
        channels.append(AnimationChannel("arm_left", "rotation", list(times), quat_track(rest_r(scene, "arm_left"), al_eulers)))
    if "forearm_left" in scene.nodes:
        channels.append(AnimationChannel("forearm_left", "rotation", list(times), quat_track(rest_r(scene, "forearm_left"), fl_eulers)))
    if "arm_right" in scene.nodes:
        channels.append(AnimationChannel("arm_right", "rotation", list(times), quat_track(rest_r(scene, "arm_right"), ar_eulers)))
    if "forearm_right" in scene.nodes:
        channels.append(AnimationChannel("forearm_right", "rotation", list(times), quat_track(rest_r(scene, "forearm_right"), fr_eulers)))
    if "legs" in scene.nodes:
        channels.append(AnimationChannel("legs", "rotation", list(times), quat_track(rest_r(scene, "legs"), legs_eulers)))
    if "antenna_left" in scene.nodes:
        channels.append(AnimationChannel("antenna_left", "rotation", list(times), quat_track(rest_r(scene, "antenna_left"), ant_l_eulers)))
    if "antenna_right" in scene.nodes:
        channels.append(AnimationChannel("antenna_right", "rotation", list(times), quat_track(rest_r(scene, "antenna_right"), ant_r_eulers)))

    clip = scene.add_animation("Swim", channels, replace=True)
    _solve_root_pose(scene, clip, times, root_eulers, root_z_com, min_air_y, com_y=0.46, close_loop=True)
    return clip


def _add_swimmer_dive(scene: SceneBuilder, splash_node: str, hide_origin: Vec3) -> AnimationClip:
    """Attach Swimmer's authentic 1998 1.04s (`asdi`, 16f) springing swan-dive & splash `"Dive"` clip."""
    # Exact 1998 asdi 16-frame cadence [60x6, 80, 100, 80, 60x7] ms = 1.04 s:
    # f0-f2 crouch windup -> f3-f6 explosive spring up -> f7-f8 head-first plunge -> f9-f13 splash ring -> f14-f15 wrap.
    times = [0.00, 0.06, 0.14, 0.24, 0.36, 0.44, 0.54, 0.62, 0.70, 0.78, 0.88, 0.98, 1.04]
    bx, by, bz = rest_t(scene, "upper_body")

    ub_d: List[Vec3] = [
        (0.0,  0.000,  0.000),  # 0.00 rest
        (0.0, -0.056, +0.020),  # 0.06 deep knee crouch
        (0.0, -0.072, +0.026),  # 0.14 coiled launch crouch
        (0.0,  0.000,  0.000),  # 0.24 explosive spring!
        (0.0,  0.000,  0.000),  # 0.36 soaring upward arch
        (0.0,  0.000,  0.000),  # 0.44 apex swan-dive!
        (0.0,  0.000,  0.000),  # 0.54 head-first plunge!
        (0.0,  0.000,  0.000),  # 0.62 entering water surface!
        (0.0,  0.000,  0.000),  # 0.70 splash entry (compact scale on Y=0)
        (0.0,  0.000,  0.000),  # 0.78 splash ring expands
        (0.0,  0.000,  0.000),  # 0.88 ripples settle
        (0.0,  0.000,  0.000),  # 0.98 underwater hold
        (0.0,  0.000,  0.000),  # 1.04 clean rest wrap
    ]
    ub_rx = [0.00, +0.34, +0.44, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00]
    root_eulers: List[Vec3] = [
        ( 0.00, 0.0, 0.0),
        ( 0.00, 0.0, 0.0),
        ( 0.00, 0.0, 0.0),
        (+0.32, 0.0, 0.0),  # spring up-forward
        (+0.92, 0.0, 0.0),  # soaring arch
        (+1.48, 0.0, 0.0),  # swan-dive apex
        (+1.96, 0.0, 0.0),  # head-first plunge!
        (+2.18, 0.0, 0.0),  # breaking surface!
        (+2.10, 0.0, 0.0),  # splash entry
        (+1.80, 0.0, 0.0),
        (+1.10, 0.0, 0.0),
        (+0.30, 0.0, 0.0),
        ( 0.00, 0.0, 0.0),
    ]
    root_z_com = [0.00, 0.00, 0.00, +0.05, +0.10, +0.14, +0.16, +0.14, +0.10, +0.06, +0.03, +0.01, 0.00]
    min_air_y = [0.000, 0.000, 0.000, 0.140, 0.300, 0.340, 0.180, 0.025, 0.002, 0.002, 0.002, 0.002, 0.000]
    root_scales: List[Vec3] = [
        _ONE, _ONE, _ONE, _ONE, _ONE, _ONE, _ONE,
        (0.86, 0.86, 0.86),
        (0.18, 0.18, 0.18),  # submerged beneath splash ring at Y=0!
        (0.08, 0.08, 0.08),
        (0.08, 0.08, 0.08),
        (0.45, 0.45, 0.45),
        _ONE,
    ]
    head_rx = [0.00, +0.16, +0.20, -0.22, -0.34, -0.24, -0.10, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00]
    gaster_rx = [0.00, -0.22, -0.28, +0.16, +0.22, +0.14, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00]

    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), [(bx + dx, by + dy, bz + dz) for dx, dy, dz in ub_d]),
        AnimationChannel("upper_body", "rotation", list(times), quat_track(rest_r(scene, "upper_body"), [(rx, 0.0, 0.0) for rx in ub_rx])),
        AnimationChannel("head_group", "rotation", list(times), quat_track(rest_r(scene, "head_group"), [(rx, 0.0, 0.0) for rx in head_rx])),
    ]
    if "gaster_group" in scene.nodes:
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), quat_track(rest_r(scene, "gaster_group"), [(rx, 0.0, 0.0) for rx in gaster_rx])))

    # Arms swing back on crouch, reach overhead streamlined for swan-dive, shovel tucked cleanly!
    if "arm_left" in scene.nodes:
        al_d = [
            ( 0.00,  0.00,  0.00),
            (+0.42, -0.20, +0.34),
            (+0.54, -0.24, +0.42),
            (-1.25, +0.10, +0.18),
            (-1.45, +0.12, +0.14),
            (-1.55, +0.14, +0.10),
            (-1.55, +0.14, +0.10),
            (-1.40, +0.10, +0.12),
            (-1.10,  0.00, +0.14),
            (-0.60,  0.00, +0.10),
            (-0.30,  0.00, +0.06),
            (-0.10,  0.00, +0.02),
            ( 0.00,  0.00,  0.00),
        ]
        channels.append(AnimationChannel("arm_left", "rotation", list(times), quat_track(rest_r(scene, "arm_left"), al_d)))
    if "arm_right" in scene.nodes:
        ar_d = [
            ( 0.00,  0.00,  0.00),
            (-0.46,  0.00, -0.14),
            (-0.56,  0.00, -0.18),
            (-0.85,  0.00, -0.22),
            (-1.05,  0.00, -0.24),
            (-1.15,  0.00, -0.24),
            (-1.10,  0.00, -0.22),
            (-0.95,  0.00, -0.20),
            (-0.75,  0.00, -0.16),
            (-0.45,  0.00, -0.10),
            (-0.22,  0.00, -0.06),
            (-0.08,  0.00, -0.02),
            ( 0.00,  0.00,  0.00),
        ]
        channels.append(AnimationChannel("arm_right", "rotation", list(times), quat_track(rest_r(scene, "arm_right"), ar_d)))

    # Aqua splash ring bursts cleanly at Y = 0 right as Swimmer breaks the water surface (0.62..0.98 s)!
    sp_ground: Vec3 = (0.0, 0.0, +0.12)
    sp_pos = [
        hide_origin, hide_origin, hide_origin, hide_origin, hide_origin, hide_origin, hide_origin,
        sp_ground, sp_ground, sp_ground, sp_ground, hide_origin, hide_origin,
    ]
    sp_sc: List[Vec3] = [
        _TINY, _TINY, _TINY, _TINY, _TINY, _TINY, _TINY,
        (0.55, 0.70, 0.55),
        (1.05, 1.25, 1.05),
        (1.38, 0.85, 1.38),
        (1.55, 0.32, 1.55),
        _TINY,
        _TINY,
    ]
    channels.append(AnimationChannel(splash_node, "translation", list(times), sp_pos))
    channels.append(AnimationChannel(splash_node, "scale", list(times), sp_sc))

    clip = scene.add_animation("Dive", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Dive", knee_bow=1.05, splay=0.25, swivel=0.0)
    _solve_root_pose(scene, clip, times, root_eulers, root_z_com, min_air_y, root_scales=root_scales, com_y=0.45, close_loop=True)
    return clip


def _add_swimmer_surface(scene: SceneBuilder) -> AnimationClip:
    """Attach Swimmer's authentic 1998 0.42s (`asgo`, 7f) water-emergence & hop `"Surface"` clip."""
    # Exact 1998 asgo 7-frame cadence [60x7] ms = 0.42 s:
    # f0-f1 pop out of water -> f2-f4 brace & hop up onto land -> f5 knee-cushion -> f6 rest pose!
    times = [0.00, 0.06, 0.12, 0.18, 0.24, 0.30, 0.36, 0.42]
    bx, by, bz = rest_t(scene, "upper_body")

    ub_d: List[Vec3] = [
        (0.0, -0.042, +0.018),  # 0.00 emerging tucked
        (0.0, -0.028, +0.012),  # 0.06 mask & snorkel breaking surface
        (0.0, -0.010, +0.006),  # 0.12 shoulders clear
        (0.0, +0.006,  0.000),  # 0.18 springing upward
        (0.0, +0.008,  0.000),  # 0.24 hop apex onto land!
        (0.0, -0.036, +0.012),  # 0.30 bouncy landing cushion on flippers
        (0.0, -0.010, +0.004),  # 0.36 rising smoothly
        (0.0,  0.000,  0.000),  # 0.42 exact rest pose
    ]
    ub_rx = [+0.36, +0.26, +0.14, -0.08, -0.10, +0.22, +0.06, 0.00]
    head_rx = [-0.24, -0.18, -0.10, +0.04, +0.06, -0.12, -0.03, 0.00]
    gaster_rx = [-0.18, -0.12, -0.06, +0.08, +0.10, -0.12, -0.04, 0.00]
    root_eulers: List[Vec3] = [(0.0, 0.0, 0.0)] * len(times)
    root_z_com = [0.0] * len(times)
    min_air_y = [0.002, 0.002, 0.004, 0.038, 0.068, 0.000, 0.000, 0.000]
    root_scales: List[Vec3] = [
        (0.42, 0.34, 0.42),
        (0.72, 0.66, 0.72),
        (0.94, 0.92, 0.94),
        (0.98, 1.04, 0.98),
        _ONE,
        (1.03, 0.96, 1.03),
        _ONE,
        _ONE,
    ]

    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), [(bx + dx, by + dy, bz + dz) for dx, dy, dz in ub_d]),
        AnimationChannel("upper_body", "rotation", list(times), quat_track(rest_r(scene, "upper_body"), [(rx, 0.0, 0.0) for rx in ub_rx], close_loop=False)),
        AnimationChannel("head_group", "rotation", list(times), quat_track(rest_r(scene, "head_group"), [(rx, 0.0, 0.0) for rx in head_rx], close_loop=False)),
    ]
    if "gaster_group" in scene.nodes:
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), quat_track(rest_r(scene, "gaster_group"), [(rx, 0.0, 0.0) for rx in gaster_rx], close_loop=False)))
    if "arm_left" in scene.nodes:
        al_d = [
            (-0.36, -0.18, +0.46),
            (-0.48, -0.22, +0.58),
            (-0.56, -0.26, +0.66),
            (-0.42, -0.18, +0.52),
            (-0.28, -0.12, +0.36),
            (-0.18, -0.06, +0.20),
            (-0.06,  0.00, +0.06),
            ( 0.00,  0.00,  0.00),
        ]
        channels.append(AnimationChannel("arm_left", "rotation", list(times), quat_track(rest_r(scene, "arm_left"), al_d, close_loop=False)))
    if "arm_right" in scene.nodes:
        ar_d = [
            (-0.44, 0.0, -0.18),
            (-0.38, 0.0, -0.16),
            (-0.30, 0.0, -0.14),
            (-0.22, 0.0, -0.12),
            (-0.16, 0.0, -0.08),
            (-0.24, 0.0, -0.10),
            (-0.08, 0.0, -0.04),
            ( 0.00, 0.0,  0.00),
        ]
        channels.append(AnimationChannel("arm_right", "rotation", list(times), quat_track(rest_r(scene, "arm_right"), ar_d, close_loop=False)))

    clip = scene.add_animation("Surface", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Surface", knee_bow=1.05, splay=0.25, swivel=0.0)
    _solve_root_pose(scene, clip, times, root_eulers, root_z_com, min_air_y, root_scales=root_scales, com_y=0.45, close_loop=False)
    return clip


def _add_swimmer_shovel_variant(
    scene: SceneBuilder,
    *,
    clip_name: str,
    times: Sequence[float],
    toss_node: str,
    hide_origin: Vec3,
    deep_scoop: bool,
) -> AnimationClip:
    """Build Swimmer's grounded beach-shovel digging (`Shovel`) or water-scooping (`ShovelWater`) loop."""
    bx, by, bz = rest_t(scene, "upper_body")
    d_mult = 1.18 if deep_scoop else 1.00

    ub_d: List[Vec3] = [
        (0.0,  0.000,           0.000),           # f0 start
        (0.0, -0.028 * d_mult, +0.016 * d_mult),  # f1 plunge down
        (0.0, -0.044 * d_mult, +0.026 * d_mult),  # f2 deep dig / scoop
        (0.0, -0.046 * d_mult, +0.028 * d_mult),  # f3 scoop load
        (0.0, -0.012,          +0.006),           # f4 heave upward
        (0.0, +0.008,          -0.008),           # f5 vigorous flick & toss!
        (0.0, +0.004,          -0.004),           # f6 follow-through
        (0.0, -0.008,          +0.004),           # f7 return down
        (0.0,  0.000,           0.000),           # loop wrap
    ]
    ub_eulers: List[Vec3] = [
        ( 0.00,           0.00,  0.00),
        (+0.38 * d_mult, -0.18, -0.08),
        (+0.54 * d_mult, -0.24, -0.12),
        (+0.56 * d_mult, -0.14, -0.08),
        (+0.18,          +0.12, +0.06),
        (-0.10,          +0.28, +0.12),
        (-0.04,          +0.20, +0.08),
        (+0.12,          +0.04,  0.00),
        ( 0.00,           0.00,  0.00),
    ]
    head_eulers: List[Vec3] = [
        ( 0.00,  0.00, 0.0),
        (+0.16, +0.10, 0.0),
        (+0.22, +0.14, 0.0),
        (+0.20, +0.08, 0.0),
        (-0.06, -0.08, 0.0),
        (-0.18, -0.16, 0.0),
        (-0.10, -0.12, 0.0),
        (+0.04, -0.02, 0.0),
        ( 0.00,  0.00, 0.0),
    ]
    gaster_rx = [0.00, -0.20, -0.28, -0.30, -0.08, +0.12, +0.06, -0.06, 0.00]

    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), [(bx + dx, by + dy, bz + dz) for dx, dy, dz in ub_d]),
        AnimationChannel("upper_body", "rotation", list(times), quat_track(rest_r(scene, "upper_body"), ub_eulers)),
        AnimationChannel("head_group", "rotation", list(times), quat_track(rest_r(scene, "head_group"), head_eulers)),
    ]
    if "gaster_group" in scene.nodes:
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), quat_track(rest_r(scene, "gaster_group"), [(rx, 0.0, 0.0) for rx in gaster_rx])))

    # Right arm drives the handheld yellow beach shovel cleanly: low dig -> scoop -> vigorous outward toss flick!
    # Flared cleanly outward (`rz < 0`) on heave so the blade never crosses in front of Swimmer's dive mask.
    if "arm_right" in scene.nodes:
        ar_d = [
            ( 0.00,  0.00,  0.00),
            (-0.08, -0.14, -0.14),  # plunge down toward ground
            (-0.22, -0.18, -0.18),  # low dig / scoop skim
            (-0.38, -0.14, -0.22),  # lift load from ground
            (-0.66, -0.10, -0.38),  # heave scoop up & outward!
            (-0.78, -0.06, -0.46),  # crisp outward toss apex (mask 100% clear!)
            (-0.60, -0.04, -0.36),  # follow-through
            (-0.20,  0.00, -0.12),
            ( 0.00,  0.00,  0.00),
        ]
        channels.append(AnimationChannel("arm_right", "rotation", list(times), quat_track(rest_r(scene, "arm_right"), ar_d)))
    if "forearm_right" in scene.nodes:
        fr_d = [
            ( 0.00,  0.00,  0.00),
            (-0.08, -0.08, -0.12),
            (-0.14, -0.14, -0.18),
            (-0.22, -0.08, -0.12),
            (-0.16, +0.10, +0.12),  # wrist scoop lift
            (-0.08, +0.16, +0.24),  # snappy outward toss flick!
            (-0.10, +0.10, +0.14),
            (-0.06, +0.02,  0.00),
            ( 0.00,  0.00,  0.00),
        ]
        channels.append(AnimationChannel("forearm_right", "rotation", list(times), quat_track(rest_r(scene, "forearm_right"), fr_d)))

    # Left arm braces & counter-swings naturally for balance.
    if "arm_left" in scene.nodes:
        al_d = [
            ( 0.00,  0.00,  0.00),
            (-0.22, +0.16, +0.26),
            (-0.34, +0.24, +0.36),
            (-0.30, +0.18, +0.32),
            (-0.18, -0.14, +0.44),
            (-0.12, -0.28, +0.56),
            (-0.14, -0.18, +0.42),
            (-0.08, -0.04, +0.16),
            ( 0.00,  0.00,  0.00),
        ]
        channels.append(AnimationChannel("arm_left", "rotation", list(times), quat_track(rest_r(scene, "arm_left"), al_d)))
    if "forearm_left" in scene.nodes:
        fl_d = [
            ( 0.00, 0.00, 0.0),
            (-0.14, -0.10, 0.0),
            (-0.22, -0.14, 0.0),
            (-0.20, -0.10, 0.0),
            (-0.12, +0.10, 0.0),
            (-0.08, +0.16, 0.0),
            (-0.10, +0.10, 0.0),
            (-0.04, +0.04, 0.0),
            ( 0.00, 0.00, 0.0),
        ]
        channels.append(AnimationChannel("forearm_left", "rotation", list(times), quat_track(rest_r(scene, "forearm_left"), fl_d)))

    for node_n, side in (("antenna_left", +1.0), ("antenna_right", -1.0)):
        if node_n in scene.nodes and scene.get_node(node_n):
            ant_del = [(-0.20 * urx, 0.0, side * 0.10 * ury) for urx, ury, _ in ub_eulers]
            channels.append(AnimationChannel(node_n, "rotation", list(times), quat_track(rest_r(scene, node_n), ant_del)))

    # Arc dirt clods (`Shovel`) or aqua water fan (`ShovelWater`) smoothly out of the shovel blade!
    toss_pos: List[Vec3] = [
        hide_origin,           # f0 hidden
        hide_origin,           # f1 hidden
        (-0.28, 0.14, +0.34),  # f2 loading on blade
        (-0.26, 0.18, +0.38),  # f3 lifting in scoop
        (-0.20, 0.36, +0.48),  # f4 launched upward!
        (-0.12, 0.48, +0.62),  # f5 peak toss arc!
        (-0.06, 0.34, +0.74),  # f6 arcing forward/down!
        hide_origin,           # f7 vanished
        hide_origin,           # loop wrap
    ]
    toss_sc: List[Vec3] = [
        _TINY, _TINY,
        (0.55, 0.55, 0.55),
        (0.95, 0.95, 0.95),
        (1.12, 1.08, 1.12),
        (1.22, 1.15, 1.28) if deep_scoop else (1.08, 1.05, 1.08),
        (0.68, 0.68, 0.78),
        _TINY,
        _TINY,
    ]
    channels.append(AnimationChannel(toss_node, "translation", list(times), toss_pos))
    channels.append(AnimationChannel(toss_node, "scale", list(times), toss_sc))

    clip = scene.add_animation(clip_name, channels, replace=True)
    sync_leg_flex_to_clip(scene, clip_name, knee_bow=1.05, splay=0.25, swivel=0.0)
    return clip


def add_swimmer_abilities(scene: SceneBuilder) -> Tuple[AnimationClip, ...]:
    """Attach all 5 authentic 1998 Swimmer ability clips (`Swim`, `Dive`, `Surface`, `Shovel`, `ShovelWater`)."""
    ensure_ability_root(scene)
    splash_node, dirt_node, water_node, hide_origin = _add_swimmer_ability_props(scene)
    c_swim = _add_swimmer_swim(scene)
    c_dive = _add_swimmer_dive(scene, splash_node, hide_origin)
    c_surface = _add_swimmer_surface(scene)
    c_shovel = _add_swimmer_shovel_variant(
        scene,
        clip_name="Shovel",
        times=[0.00, 0.06, 0.12, 0.18, 0.24, 0.30, 0.36, 0.42, 0.48],
        toss_node=dirt_node,
        hide_origin=hide_origin,
        deep_scoop=False,
    )
    c_shovel_w = _add_swimmer_shovel_variant(
        scene,
        clip_name="ShovelWater",
        times=[0.00, 0.06, 0.12, 0.18, 0.24, 0.30, 0.36, 0.42, 0.50],
        toss_node=water_node,
        hide_origin=hide_origin,
        deep_scoop=True,
    )
    return (c_swim, c_dive, c_surface, c_shovel, c_shovel_w)


__all__ = ["add_swimmer_abilities"]
