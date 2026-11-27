"""Authentic 1998 shared hazard animations (`BlownBack`, `BlownUp`, `Stunned`, `Drown`) for all 6 ants.

Pure-stdlib safe on import (mirrors `hit_clips.py` / `thief_attack.py`) and works uniformly on
both standard-arm ants (`worker`, `thief`, `bomber`, `swimmer`, `fire`) and Combat's custom
`left_arm_group`/`right_arm_group` rig, whether or not a `"root"` node already exists.

Exact 1998 CHD durations (`docs/chd_table4_animations.json`):
- BlownBack (`*gb`): worker 0.90s, combat 0.98s, thief 0.91s, bomber 1.06s, swimmer 1.15s, fire 1.10s
- BlownUp   (`*bu`): worker 1.15s, combat 1.165s, thief 1.15s, bomber 1.61s, swimmer 1.05s, fire 1.33s
- Stunned   (`*sd`): worker 3.125s, combat 3.00s, thief 2.61s, bomber 3.835s, swimmer 2.88s, fire 3.00s
- Drown     (`*dr`): worker 2.37s, combat 2.37s, thief 2.37s, bomber 2.38s, swimmer 2.37s, fire 2.37s

Temporary effect nodes (`stun_stars`, `drown_splash`) rest tucked inside the opaque torso at
`_HIDE_SCALE = (1e-4, 1e-4, 1e-4)` so rest/Idle/Attack/Hit bounding boxes change by 0.000 mm.
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
        from .leg_rig import _quat_conj
        from .mesh_core import (
            AnimationChannel,
            AnimationClip,
            MeshPart,
            SceneBuilder,
            cone,
            ellipsoid,
            quat_from_euler_xyz,
            quat_mul,
            sphere,
            torus_arc,
        )
    except ImportError:
        from base_ant import EYELID_CLOSED_ANGLE, sync_leg_flex_to_clip  # type: ignore
        from leg_rig import _quat_conj  # type: ignore
        from mesh_core import (  # type: ignore
            AnimationChannel,
            AnimationClip,
            MeshPart,
            SceneBuilder,
            cone,
            ellipsoid,
            quat_from_euler_xyz,
            quat_mul,
            sphere,
            torus_arc,
        )

Vec3 = Tuple[float, float, float]
Quat = Tuple[float, float, float, float]

_HIDE_SCALE: Vec3 = (1e-4, 1e-4, 1e-4)
_HIDE_POS: Vec3 = (0.0, 0.35, 0.0)
_FX_NODES = frozenset(("stun_stars", "drown_splash"))

BLOWN_BACK_DURATIONS: Dict[str, float] = {
    "worker": 0.90,
    "combat": 0.98,
    "thief": 0.91,
    "bomber": 1.06,
    "swimmer": 1.15,
    "fire": 1.10,
}

BLOWN_UP_DURATIONS: Dict[str, float] = {
    "worker": 1.15,
    "combat": 1.165,
    "thief": 1.15,
    "bomber": 1.61,
    "swimmer": 1.05,
    "fire": 1.33,
}

STUNNED_DURATIONS: Dict[str, float] = {
    "worker": 3.125,
    "combat": 3.00,
    "thief": 2.61,
    "bomber": 3.835,
    "swimmer": 2.88,
    "fire": 3.00,
}

DROWN_DURATIONS: Dict[str, float] = {
    "worker": 2.37,
    "combat": 2.37,
    "thief": 2.37,
    "bomber": 2.38,
    "swimmer": 2.37,
    "fire": 2.37,
}


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


def _ensure_hazard_root(scene: SceneBuilder) -> str:
    """Ensure all primary ant top-level nodes sit under an identity `"root"` node.

    Existing `Idle`, `Attack`, `Hit`, and `Blink` clips never key `"root"`, so their poses stay
    bit-for-bit identical while `BlownBack`, `BlownUp`, and `Drown` tumble/scale the whole ant
    (including legs and Swimmer flippers) with 0.000 mm hip detachment.
    """
    if "root" not in scene.nodes:
        top_nodes = [
            n
            for n in list(scene.nodes.keys())
            if scene.get_node_parent(n) is None and n not in _FX_NODES
        ]
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


def _blink_closed_angle(scene: SceneBuilder) -> float:
    """Read the scene's calibrated Blink closed X-rotation angle so squints never over-rotate."""
    blink = next((c for c in scene.animations if c.name == "Blink"), None)
    if blink is not None:
        for ch in blink.channels:
            if ch.target_node == "eyelid_left" and ch.path == "rotation" and ch.values:
                inv0 = _quat_conj(_rest_r(scene, "eyelid_left"))
                best = 0.0
                for q in ch.values:
                    dq = quat_mul(inv0, q)
                    ang = 2.0 * math.atan2(float(dq[0]), max(1e-9, abs(float(dq[3]))))
                    if ang > best:
                        best = ang
                if best > 0.1:
                    return best
    return float(EYELID_CLOSED_ANGLE)


def _eyelid_channels(scene: SceneBuilder, times: Sequence[float], squints: Sequence[float]) -> List[AnimationChannel]:
    ang_max = _blink_closed_angle(scene)
    out: List[AnimationChannel] = []
    for lid_n in ("eyelid_left", "eyelid_right"):
        if lid_n not in scene.nodes or not scene.get_node(lid_n):
            continue
        r0 = _rest_r(scene, lid_n)
        qs = _align_quats([quat_mul(r0, quat_from_euler_xyz(rx=ang_max * float(sq))) for sq in squints])
        out.append(AnimationChannel(lid_n, "rotation", list(times), qs))
    return out


def _arm_node_pairs(scene: SceneBuilder) -> List[Tuple[str, str, float, bool]]:
    """Return `(upper_node, fore_node, side_sign, is_combat_rig)` for whichever arm rig the scene uses."""
    if "left_arm_group" in scene.nodes:
        return [
            ("left_arm_group", "left_forearm_group", +1.0, True),
            ("right_arm_group", "right_forearm_group", -1.0, True),
        ]
    pairs: List[Tuple[str, str, float, bool]] = []
    for side_tag, s in (("left", +1.0), ("right", -1.0)):
        if f"arm_{side_tag}" in scene.nodes:
            pairs.append((f"arm_{side_tag}", f"forearm_{side_tag}", s, False))
    return pairs


def _arm_channels(
    scene: SceneBuilder,
    times: Sequence[float],
    upper_eulers_l: Sequence[Vec3],
    fore_eulers_l: Sequence[Vec3],
    *,
    asym_phase: Optional[Sequence[float]] = None,
) -> List[AnimationChannel]:
    """Build mirrored or phase-offset shoulder & forearm channels for BOTH standard and Combat rigs."""
    out: List[AnimationChannel] = []
    for up_n, fo_n, s, is_combat in _arm_node_pairs(scene):
        # Combat's bulky arms have wider resting sweep; scale Euler swings slightly for clean torso clearance.
        k_rx = 0.72 if is_combat else 1.0
        k_rz = 0.65 if is_combat else 1.0
        r0_u = _rest_r(scene, up_n)
        u_qs: List[Quat] = []
        for idx, (rx, ry, rz) in enumerate(upper_eulers_l):
            alt = float(asym_phase[idx]) * s if asym_phase is not None else 0.0
            u_qs.append(quat_mul(r0_u, quat_from_euler_xyz(rx=k_rx * (rx + alt), ry=s * ry, rz=s * k_rz * rz)))
        out.append(AnimationChannel(up_n, "rotation", list(times), _align_quats(u_qs)))
        if fo_n in scene.nodes:
            r0_f = _rest_r(scene, fo_n)
            f_qs: List[Quat] = []
            for idx, (rx, ry, rz) in enumerate(fore_eulers_l):
                alt = 0.5 * float(asym_phase[idx]) * s if asym_phase is not None else 0.0
                f_qs.append(quat_mul(r0_f, quat_from_euler_xyz(rx=k_rx * (rx + alt), ry=s * ry, rz=s * k_rz * rz)))
            out.append(AnimationChannel(fo_n, "rotation", list(times), _align_quats(f_qs)))
    return out


def _visible_ant_node_verts(scene: SceneBuilder) -> Dict[str, np.ndarray]:
    """Collect non-hidden primary ant vertices (skipping `_HIDE_SCALE` prop/FX nodes) for floor clamping."""
    out: Dict[str, np.ndarray] = {}
    for n, ps in scene.nodes.items():
        if not ps or n in _FX_NODES:
            continue
        xf = scene.get_node_transform(n)
        if xf is not None and min(abs(c) for c in xf.scale) < 1e-2:
            continue
        out[n] = np.vstack([np.asarray(p.positions, dtype=np.float64) for p in ps])
    return out


def _eval_pose_min_y(
    scene: SceneBuilder,
    overrides: Dict[str, Dict[str, Tuple[float, ...]]],
    node_verts: Dict[str, np.ndarray],
) -> float:
    """Exact world-space minimum Y across all primary ant vertices for a single frame."""
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
        min_y = min(min_y, float((verts @ m[1, :3] + m[1, 3]).min()))
    return min_y


def _interp_rows(rows: Sequence[Tuple[float, ...]], u: float) -> Tuple[float, ...]:
    if u <= rows[0][0]:
        return rows[0]
    if u >= rows[-1][0]:
        return rows[-1]
    for i in range(len(rows) - 1):
        u0, u1 = rows[i][0], rows[i + 1][0]
        if u0 <= u <= u1:
            t = (u - u0) / max(1e-9, u1 - u0)
            return tuple((1.0 - t) * a + t * b for a, b in zip(rows[i], rows[i + 1]))
    return rows[-1]


def _dense_unit_grid(rows: Sequence[Tuple[float, ...]], dur: float, *, max_dt: float = 0.025, max_dang: float = 0.14) -> List[float]:
    """Dense normalized timeline u in [0, 1] subdividing both time and big root rotation arcs."""
    us: List[float] = [float(rows[0][0])]
    for i in range(1, len(rows)):
        u0 = us[-1]
        u1 = float(rows[i][0])
        du = u1 - u0
        dang = max(
            abs(float(rows[i][4]) - float(rows[i - 1][4])),
            abs(float(rows[i][5]) - float(rows[i - 1][5])),
            abs(float(rows[i][6]) - float(rows[i - 1][6])),
        )
        n_sub = max(1, int(math.ceil((du * dur) / max_dt)), int(math.ceil(dang / max_dang)))
        for k in range(1, n_sub):
            um = u0 + du * (k / n_sub)
            if u0 + 1e-6 < um < u1 - 1e-6:
                us.append(um)
        if u1 > us[-1] + 1e-6:
            us.append(u1)
    return us


def _ensure_stun_stars_node(scene: SceneBuilder) -> str:
    """Attach bold 1998-style cartoon dazed halo (5 bright stars + twinkle motes + spin ring) on `"stun_stars"`."""
    if "stun_stars" in scene.nodes:
        return "stun_stars"
    parts: List[MeshPart] = []
    orbit_r = 0.285
    # Crisp slender glowing dizzy motion-trail halo connecting the orbiting cartoon stars!
    halo = torus_arc(orbit_r, 0.0085, u_segs=32, v_segs=6, plane="xz", name="stun_orbit_halo", material="eye_catchlight").scale(1.0, 0.65, 1.0)
    parts.append(halo)
    # 5 bold orbiting cartoon stars (3 bright 5-point gold stars + 2 sparkling 4-point white-gold starbursts matching `agsd`/`acsd`!)
    for s_idx in range(5):
        ang = 2.0 * math.pi * s_idx / 5.0
        cx = orbit_r * math.cos(ang)
        cz = orbit_r * math.sin(ang)
        cy = (0.016, -0.028, +0.036, -0.018, +0.022)[s_idx]
        is_burst = (s_idx % 2 == 1)
        star_scale = (1.52, 1.32, 1.44, 1.26, 1.48)[s_idx]
        core_mat = "eye_catchlight" if is_burst else "gear_gold"
        tip_mat = "eye_catchlight" if is_burst else "gear_gold"
        core = sphere(0.025 * star_scale, u_segs=10, v_segs=8, name=f"stun_star_{s_idx}_core", material=core_mat).scale(1.0, 1.0, 0.60).rotate_xyz(ry=-ang + 0.5 * math.pi).translate(cx, cy, cz)
        parts.append(core)
        n_pts = 4 if is_burst else 5
        for p_idx in range(n_pts):
            pa = 2.0 * math.pi * p_idx / float(n_pts)
            spike = (
                cone(0.018 * star_scale, 0.072 * star_scale, radial_segs=6, name=f"stun_star_{s_idx}_pt{p_idx}", material=tip_mat)
                .rotate_xyz(rz=pa)
                .rotate_xyz(ry=-ang + 0.5 * math.pi)
                .translate(cx, cy, cz)
            )
            parts.append(spike)
        # Tiny trailing cartoon sparkle mote midway between stars
        m_ang = ang + math.pi / 5.0
        mote = sphere(0.017, u_segs=8, v_segs=6, name=f"stun_mote_{s_idx}", material="eye_catchlight").translate(
            (orbit_r * 0.90) * math.cos(m_ang), -cy * 0.65, (orbit_r * 0.90) * math.sin(m_ang)
        )
        parts.append(mote)
    scene.add("stun_stars", parts)
    scene.set_node_transform("stun_stars", translation=_HIDE_POS, scale=_HIDE_SCALE)
    return "stun_stars"


def _ensure_drown_splash_node(scene: SceneBuilder) -> str:
    """Attach translucent cyan water-splash crown & ripple rings on `"drown_splash"` hidden at `_HIDE_SCALE`."""
    if "drown_splash" in scene.nodes:
        return "drown_splash"
    parts: List[MeshPart] = []
    # Inner & outer concentric water surface ripple rings just above Y = 0.0.
    parts.append(
        torus_arc(0.24, 0.018, u_segs=28, v_segs=8, plane="xz", name="drown_ripple_inner", material="glass_tinted")
        .scale(1.0, 0.45, 1.0)
        .translate(0.0, 0.012, 0.0)
    )
    parts.append(
        torus_arc(0.38, 0.015, u_segs=32, v_segs=8, plane="xz", name="drown_ripple_outer", material="glass_clear")
        .scale(1.0, 0.42, 1.0)
        .translate(0.0, 0.010, 0.0)
    )
    # Radial scalloped 10-plume splash crown + bright floating water droplets matching `*dr301` (all vertices Y >= 0.005).
    for i in range(10):
        az = 2.0 * math.pi * i / 10.0
        ca, sa = math.cos(az), math.sin(az)
        h_var = 0.14 if i % 2 == 0 else 0.11
        r_base = 0.21 if i % 2 == 0 else 0.25
        plume = (
            ellipsoid(0.036, h_var, 0.026, u_segs=10, v_segs=8, name=f"drown_plume_{i}", material="glass_tinted")
            .rotate_xyz(rz=-0.52)
            .rotate_xyz(ry=-az)
            .translate(r_base * ca, 0.145 + 0.02 * (i % 2), r_base * sa)
        )
        plume = plume.deform(lambda p: np.column_stack([p[:, 0], np.maximum(p[:, 1], 0.005), p[:, 2]]))
        parts.append(plume)
        drop = sphere(
            0.022 if i % 2 == 0 else 0.016,
            u_segs=8,
            v_segs=6,
            name=f"drown_drop_{i}",
            material="glass_clear",
        ).translate((r_base + 0.12) * ca, 0.24 + 0.05 * (i % 2), (r_base + 0.12) * sa)
        parts.append(drop)
    scene.add("drown_splash", parts)
    scene.set_node_transform("drown_splash", translation=_HIDE_POS, scale=_HIDE_SCALE)
    return "drown_splash"


def _add_airborne_tumble_clip(
    scene: SceneBuilder,
    clip_name: str,
    dur: float,
    key_rows: Sequence[Tuple[float, ...]],
    *,
    com_y_frac: float = 0.46,
    min_floor_clear: float = 0.005,
) -> AnimationClip:
    """Build a COM-pivoted airborne knockback/launch clip (`BlownBack` or `BlownUp`) with exact floor clamp.

    Row columns (normalized u in [0, 1]):
      `(u, dx, dy, dz, root_rx, root_ry, root_rz, head_rx, gaster_rx, arm_rx, arm_rz, fore_rx, squint)`
    """
    _ensure_hazard_root(scene)
    node_verts = _visible_ant_node_verts(scene)
    mn, mx = scene.bounds()
    p_com = np.array([0.0, float(mn[1] + com_y_frac * (mx[1] - mn[1])), 0.0], dtype=np.float64)

    us = _dense_unit_grid(key_rows, dur, max_dt=0.024, max_dang=0.12)
    times = [round(u * dur, 5) for u in us]
    samples = [_interp_rows(key_rows, u) for u in us]

    r_hd0 = _rest_r(scene, "head_group")
    r_gs0 = _rest_r(scene, "gaster_group")
    arm_pairs = _arm_node_pairs(scene)
    r_arm0 = {n: _rest_r(scene, n) for pair in arm_pairs for n in (pair[0], pair[1]) if n in scene.nodes}

    root_ts: List[Vec3] = []
    root_rs: List[Quat] = []
    hd_rs: List[Quat] = []
    gs_rs: List[Quat] = []
    arm_u_l: List[Vec3] = []
    arm_f_l: List[Vec3] = []
    squints: List[float] = []

    for row in samples:
        _, dx, dy, dz, r_rx, r_ry, r_rz, hd_rx, gs_rx, a_rx, a_rz, f_rx, sq = row
        q_root = quat_from_euler_xyz(rx=r_rx, ry=r_ry, rz=r_rz)
        rot_m = _quat_rot_mat(q_root)
        # Rotate cleanly about center-of-mass pivot `p_com` then apply world translation `(dx, dy, dz)`.
        t_root = p_com + np.array([dx, dy, dz], dtype=np.float64) - rot_m @ p_com
        q_hd = quat_mul(r_hd0, quat_from_euler_xyz(rx=hd_rx))
        q_gs = quat_mul(r_gs0, quat_from_euler_xyz(rx=gs_rx))

        # Exact world min_y floor clamp so zero vertices ever dip below Y = 0.0 at any dense sample.
        ov: Dict[str, Dict[str, Tuple[float, ...]]] = {
            "root": {"translation": tuple(float(c) for c in t_root), "rotation": q_root},
            "head_group": {"rotation": q_hd},
            "gaster_group": {"rotation": q_gs},
        }
        for up_n, fo_n, s, is_combat in arm_pairs:
            k_rx = 0.72 if is_combat else 1.0
            k_rz = 0.65 if is_combat else 1.0
            ov[up_n] = {"rotation": quat_mul(r_arm0[up_n], quat_from_euler_xyz(rx=k_rx * a_rx, rz=s * k_rz * a_rz))}
            if fo_n in r_arm0:
                ov[fo_n] = {"rotation": quat_mul(r_arm0[fo_n], quat_from_euler_xyz(rx=k_rx * f_rx, rz=s * k_rz * 0.20 * a_rz))}

        min_y = _eval_pose_min_y(scene, ov, node_verts)
        if min_y < min_floor_clear and abs(r_rx) + abs(r_ry) + abs(r_rz) + abs(dy) + abs(dz) > 1e-4:
            t_root[1] += min_floor_clear - min_y
        elif min_y < 0.0:
            t_root[1] -= min_y

        root_ts.append((float(t_root[0]), float(t_root[1]), float(t_root[2])))
        root_rs.append(q_root)
        hd_rs.append(q_hd)
        gs_rs.append(q_gs)
        arm_u_l.append((a_rx, 0.0, a_rz))
        arm_f_l.append((f_rx, 0.0, 0.20 * a_rz))
        squints.append(sq)

    root_ts[-1] = root_ts[0]
    channels: List[AnimationChannel] = [
        AnimationChannel("root", "translation", list(times), root_ts),
        AnimationChannel("root", "rotation", list(times), _align_quats(root_rs)),
        AnimationChannel("head_group", "rotation", list(times), _align_quats(hd_rs)),
    ]
    if "gaster_group" in scene.nodes:
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), _align_quats(gs_rs)))
    channels.extend(_arm_channels(scene, times, arm_u_l, arm_f_l))
    channels.extend(_eyelid_channels(scene, times, squints))
    return scene.add_animation(clip_name, channels, replace=True)


def add_blown_back(scene: SceneBuilder, ant_class: str) -> AnimationClip:
    """Authentic `*gb` shockwave blow-back arc -> flat back-thump -> scramble roll recovery."""
    dur = BLOWN_BACK_DURATIONS.get(ant_class, 0.98)
    # Normalized u in [0, 1] matching `*gb301`/`*gb901` 12f blast arc -> back impact -> forward scramble -> rest.
    # (u, dx, dy, dz, root_rx, root_ry, root_rz, head_rx, gaster_rx, arm_rx, arm_rz, fore_rx, squint)
    rows = [
        (0.00, 0.000, +0.000, +0.000, +0.00, 0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, 0.00),
        (0.08, 0.000, +0.220, -0.090, -0.78, 0.00, +0.06, -0.22, -0.20, -0.45, +1.05, -0.22, 0.75),
        (0.18, 0.000, +0.340, -0.220, -1.42, 0.00, +0.10, -0.14, -0.24, -0.15, +1.18, -0.14, 0.90),
        (0.30, 0.000, +0.260, -0.320, -1.70, 0.00, +0.06, +0.02, -0.14, +0.16, +1.15, -0.10, 0.92),
        (0.42, 0.000, -0.100, -0.340, -1.58, 0.00, +0.00, +0.12, -0.04, +0.28, +0.98, -0.14, 0.85),  # thump on back
        (0.52, 0.000, -0.060, -0.260, -1.32, 0.00, -0.04, +0.22, +0.12, +0.32, +0.82, -0.20, 0.55),  # bounce & coil
        (0.62, 0.000, +0.070, -0.140, -0.28, 0.00, -0.04, +0.24, +0.24, -0.10, +0.62, -0.26, 0.25),  # kip-up snap
        (0.72, 0.000, -0.010, -0.050, +0.68, 0.00, +0.02, +0.20, +0.24, -0.48, +0.68, -0.30, 0.10),  # scramble roll
        (0.82, 0.000, +0.000, -0.020, +0.88, 0.00, +0.00, +0.16, +0.22, -0.52, +0.72, -0.32, 0.05),  # deep bow
        (0.91, 0.000, +0.000, +0.000, +0.28, 0.00, +0.00, +0.06, +0.08, -0.18, +0.26, -0.12, 0.00),  # heave up
        (1.00, 0.000, +0.000, +0.000, +0.00, 0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, 0.00),
    ]
    return _add_airborne_tumble_clip(scene, "BlownBack", dur, rows, com_y_frac=0.45, min_floor_clear=0.006)


def add_blown_up(scene: SceneBuilder, ant_class: str) -> AnimationClip:
    """Authentic `*bu` vertical explosion launch high into air -> hang-time -> bouncy splat recovery."""
    dur = BLOWN_UP_DURATIONS.get(ant_class, 1.15)
    # Normalized u in [0, 1] matching `agbu301`/`acbu301`: rocket straight up with splayed limbs -> fall -> splat -> pop up.
    # (u, dx, dy, dz, root_rx, root_ry, root_rz, head_rx, gaster_rx, arm_rx, arm_rz, fore_rx, squint)
    rows = [
        (0.00, 0.000, +0.000, +0.000, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, 0.00),
        (0.10, 0.000, +0.340, +0.015, -0.22, +0.32, +0.10, -0.22, -0.18, -0.55, +1.15, -0.18, 0.20),  # blast off!
        (0.22, 0.000, +0.660, +0.025, -0.34, +0.62, -0.12, -0.28, -0.24, -0.72, +1.28, -0.12, 0.05),  # skyrocketing
        (0.34, 0.000, +0.760, +0.020, -0.18, -0.42, +0.14, -0.20, -0.18, -0.68, +1.24, -0.14, 0.05),  # apex hang-time
        (0.46, 0.000, +0.540, +0.010, +0.18, -0.22, -0.10, +0.06, +0.10, -0.50, +1.10, -0.22, 0.45),  # plummeting
        (0.58, 0.000, +0.060, +0.005, +0.68, +0.00, +0.04, +0.20, +0.24, -0.45, +0.86, -0.32, 0.85),  # ground impact!
        (0.68, 0.000, -0.020, +0.010, +1.08, +0.00, +0.00, +0.24, +0.28, -0.56, +0.82, -0.36, 0.92),  # splat crouch
        (0.78, 0.000, +0.050, +0.005, +0.78, +0.00, -0.03, +0.14, +0.18, -0.38, +0.60, -0.24, 0.55),  # bouncy rebound
        (0.88, 0.000, +0.000, +0.000, +0.28, +0.00, +0.02, +0.05, +0.06, -0.14, +0.24, -0.10, 0.20),  # recovering
        (1.00, 0.000, +0.000, +0.000, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, 0.00),
    ]
    return _add_airborne_tumble_clip(scene, "BlownUp", dur, rows, com_y_frac=0.48, min_floor_clear=0.006)


def add_stunned(scene: SceneBuilder, ant_class: str) -> AnimationClip:
    """Authentic `*sd` dizzy woozy figure-8 sway + 3 orbiting golden stun stars above head (seamless loop)."""
    dur = STUNNED_DURATIONS.get(ant_class, 3.00)
    _ensure_stun_stars_node(scene)
    mn, mx = scene.bounds()
    crown_y = float(mx[1]) + 0.13

    n_steps = 36  # 10 deg/step for 2 full smooth star orbits (720 deg total) -> 20 deg/step, zero reverse!
    times = [round(dur * k / n_steps, 5) for k in range(n_steps + 1)]

    bx, by, bz = _rest_t(scene, "upper_body")
    r_ub0 = _rest_r(scene, "upper_body")
    r_hd0 = _rest_r(scene, "head_group")
    r_gs0 = _rest_r(scene, "gaster_group")

    ub_ts: List[Vec3] = []
    ub_rs: List[Quat] = []
    hd_rs: List[Quat] = []
    gs_rs: List[Quat] = []
    arm_u_l: List[Vec3] = []
    arm_f_l: List[Vec3] = []
    asym: List[float] = []
    squints: List[float] = []
    star_ts: List[Vec3] = []
    star_rs: List[Quat] = []
    star_ss: List[Vec3] = []

    is_combat = "left_arm_group" in scene.nodes
    has_shovel = "shovel" in scene.nodes
    keep_arms_up = is_combat or has_shovel
    for k in range(n_steps + 1):
        u = k / n_steps
        w1 = 2.0 * math.pi * u
        w2 = 4.0 * math.pi * u
        # Classic dizzy figure-8 wobble matching `agsd301`/`acsd301`, including deep woozy dip mid-cycle!
        dip = 0.5 * (1.0 - math.cos(w1))  # 0 at ends, 1 at mid-cycle
        sway_x = (0.034 if keep_arms_up else 0.042) * math.sin(w1) + 0.014 * math.sin(w2)
        sway_z = 0.024 * math.sin(w2) + 0.022 * dip
        sway_y = (-0.010 if keep_arms_up else -0.022) * dip - 0.006 * abs(math.sin(w1))

        ub_rx = (0.10 + 0.24 * dip + 0.09 * math.cos(w2)) if keep_arms_up else (0.12 + 0.36 * dip + 0.12 * math.cos(w2))
        ub_rz = (0.16 if keep_arms_up else 0.24) * math.sin(w1) + (0.05 if keep_arms_up else 0.08) * math.sin(w2)
        ub_ry = 0.16 * math.sin(w1 + 0.6)

        hd_rx = 0.16 * math.sin(w2 + 0.4) + 0.14 * dip
        hd_rz = 0.22 * math.cos(w1)
        hd_ry = 0.20 * math.sin(w1 - 0.5)

        ub_ts.append((bx + sway_x, by + sway_y, bz + sway_z))
        ub_rs.append(quat_mul(r_ub0, quat_from_euler_xyz(rx=ub_rx, ry=ub_ry, rz=ub_rz)))
        hd_rs.append(quat_mul(r_hd0, quat_from_euler_xyz(rx=hd_rx, ry=hd_ry, rz=hd_rz)))
        gs_rs.append(quat_mul(r_gs0, quat_from_euler_xyz(rx=-0.10 * dip, rz=-0.12 * math.sin(w1))))

        # Limp drooping arms (Combat's low fists & Swimmer's beach shovel stay lifted cleanly clear of Y = 0!).
        if has_shovel:
            arm_u_l.append((-0.44 + 0.10 * math.sin(w2), 0.0, +0.30 + 0.10 * math.sin(w1)))
            arm_f_l.append((-0.34 + 0.08 * math.cos(w1), 0.0, +0.08))
            asym.append(0.10 * math.cos(w1))
        elif is_combat:
            arm_u_l.append((-0.18 + 0.10 * math.sin(w2), 0.0, +0.34 + 0.10 * math.sin(w1)))
            arm_f_l.append((-0.24 + 0.10 * math.cos(w1), 0.0, +0.08))
            asym.append(0.10 * math.cos(w1))
        else:
            arm_u_l.append((+0.26 + 0.14 * dip, 0.0, -0.22 + 0.12 * math.sin(w1)))
            arm_f_l.append((+0.22 + 0.10 * math.cos(w1), 0.0, 0.0))
            asym.append(0.14 * math.cos(w1))
        # Half-closed woozy dazed eyelids drifting between 0.42 and 0.66.
        squints.append(0.54 + 0.12 * math.sin(w2))

        # Orbiting golden stars floating right over the swaying crown (2 full 360-deg orbits!).
        s_x = sway_x * 1.4 - 0.14 * ub_rz
        s_y = crown_y + sway_y - 0.08 * dip + 0.025 * math.sin(w2)
        s_z = sway_z + 0.04 + 0.16 * ub_rx
        star_ts.append((s_x, s_y, s_z))
        star_rs.append(quat_from_euler_xyz(rx=0.14 * math.sin(w1), ry=w2, rz=0.14 * math.cos(w1)))
        pulse = 1.0 + 0.06 * math.sin(w2)
        star_ss.append((pulse, pulse, pulse))

    ub_ts[-1] = ub_ts[0]
    star_ts[-1] = star_ts[0]
    star_ss[-1] = star_ss[0]
    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), ub_ts),
        AnimationChannel("upper_body", "rotation", list(times), _align_quats(ub_rs)),
        AnimationChannel("head_group", "rotation", list(times), _align_quats(hd_rs)),
        AnimationChannel("stun_stars", "translation", list(times), star_ts),
        AnimationChannel("stun_stars", "rotation", list(times), _align_quats(star_rs)),
        AnimationChannel("stun_stars", "scale", list(times), star_ss),
    ]
    if "gaster_group" in scene.nodes:
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), _align_quats(gs_rs)))
    channels.extend(_arm_channels(scene, times, arm_u_l, arm_f_l, asym_phase=asym))
    channels.extend(_eyelid_channels(scene, times, squints))

    clip = scene.add_animation("Stunned", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Stunned", swivel=0.0)
    return clip


def add_drown(scene: SceneBuilder, ant_class: str) -> AnimationClip:
    """Authentic `*dr` frantic drowning flail + cyan water splash ring + gradual sinking toward Y = 0."""
    dur = DROWN_DURATIONS.get(ant_class, 2.37)
    _ensure_hazard_root(scene)
    _ensure_drown_splash_node(scene)

    n_steps = 48
    times = [round(dur * k / n_steps, 5) for k in range(n_steps + 1)]

    r_ub0 = _rest_r(scene, "upper_body")
    r_hd0 = _rest_r(scene, "head_group")
    r_gs0 = _rest_r(scene, "gaster_group")

    root_ts: List[Vec3] = []
    root_rs: List[Quat] = []
    root_ss: List[Vec3] = []
    ub_rs: List[Quat] = []
    hd_rs: List[Quat] = []
    gs_rs: List[Quat] = []
    arm_u_l: List[Vec3] = []
    arm_f_l: List[Vec3] = []
    asym: List[float] = []
    squints: List[float] = []
    spl_ts: List[Vec3] = []
    spl_rs: List[Quat] = []
    spl_ss: List[Vec3] = []

    for k in range(n_steps + 1):
        u = k / n_steps
        # Phase envelopes:
        # 0.00..0.16: initial splash plunge & wide-eyed gasp
        # 0.16..0.72: frantic bobbing up/down with arms waving overhead while sinking deeper into water
        # 0.72..0.92: submerged low beneath expanding ripples (root scaled smoothly toward Y = 0 -> min_y >= 0!)
        # 0.92..1.00: clean wrap rise back to u=0 so loop closure is seamless
        if u < 0.78:
            # Smooth bobbing sink envelope (`sy` in [0.06, 0.98], always strictly positive -> min_y == 0.000!)
            sink_t = u / 0.78
            bob = 0.12 * math.sin(6.0 * math.pi * sink_t) * (1.0 - 0.65 * sink_t)
            sy = max(0.06, 0.96 - 0.88 * (sink_t ** 1.35) + bob)
            sxz = max(0.18, 1.00 - 0.78 * (sink_t ** 1.65))
        elif u < 0.92:
            sy = 0.06
            sxz = 0.18
        else:
            w = (u - 0.92) / 0.08
            sm = w * w * (3.0 - 2.0 * w)
            sy = 0.06 + (0.96 - 0.06) * sm
            sxz = 0.18 + (1.00 - 0.18) * sm

        flail = math.sin(14.0 * math.pi * u)
        flail2 = math.cos(10.0 * math.pi * u)
        # Keep root at Y >= 0 and small wobble so zero vertices ever sink below Y = 0.0!
        root_ts.append((0.022 * math.sin(6.0 * math.pi * u) * sxz, 0.004, 0.015 * math.cos(6.0 * math.pi * u) * sxz))
        root_rs.append(quat_from_euler_xyz(rx=0.0, ry=0.16 * math.sin(4.0 * math.pi * u), rz=0.0))
        root_ss.append((sxz, sy, sxz))

        # Torso & head tilt back gasping frantically for air!
        ub_rs.append(quat_mul(r_ub0, quat_from_euler_xyz(rx=-0.24 + 0.12 * flail2, rz=0.14 * flail)))
        hd_rs.append(quat_mul(r_hd0, quat_from_euler_xyz(rx=-0.34 + 0.10 * flail2, ry=0.20 * flail)))
        gs_rs.append(quat_mul(r_gs0, quat_from_euler_xyz(rx=-0.12)))

        # Frantic overhead arm waving!
        arm_u_l.append((-1.25 + 0.25 * flail2, 0.0, +1.05 + 0.20 * flail))
        arm_f_l.append((-0.38 + 0.18 * flail, 0.0, +0.18))
        asym.append(0.45 * flail)
        # Eyes wide open gasping in panic (brief splash blinks only on big plunges).
        squints.append(0.18 if (0.04 < u < 0.10 or 0.36 < u < 0.42) else 0.0)

        # Translucent cyan splash ring: huge bursting crown at splash-in (0.00..0.24), rippling outward as ant sinks!
        if u < 0.22:
            su = u / 0.22
            sp_xz = 0.55 + 0.85 * math.sin(0.5 * math.pi * su)
            sp_y = 0.45 + 0.95 * math.sin(math.pi * su)
        elif u < 0.92:
            ru = (u - 0.22) / 0.70
            pulse = 0.18 * math.sin(8.0 * math.pi * ru)
            sp_xz = 1.05 + 0.30 * math.sin(2.0 * math.pi * ru) + pulse
            sp_y = max(0.14, 0.52 * (1.0 - 0.68 * ru) + 0.14 * abs(math.sin(6.0 * math.pi * ru)))
        else:
            wu = (u - 0.92) / 0.08
            sp_xz = 1.05 * (1.0 - wu) + 0.55 * wu
            sp_y = 0.18 * (1.0 - wu) + 0.45 * wu

        spl_ts.append((0.0, 0.002, 0.0))
        spl_rs.append(quat_from_euler_xyz(ry=2.0 * math.pi * u))
        spl_ss.append((sp_xz, sp_y, sp_xz))

    root_ts[-1] = root_ts[0]
    root_ss[-1] = root_ss[0]
    spl_ts[-1] = spl_ts[0]
    spl_ss[-1] = spl_ss[0]
    channels: List[AnimationChannel] = [
        AnimationChannel("root", "translation", list(times), root_ts),
        AnimationChannel("root", "rotation", list(times), _align_quats(root_rs)),
        AnimationChannel("root", "scale", list(times), root_ss),
        AnimationChannel("upper_body", "rotation", list(times), _align_quats(ub_rs)),
        AnimationChannel("head_group", "rotation", list(times), _align_quats(hd_rs)),
        AnimationChannel("drown_splash", "translation", list(times), spl_ts),
        AnimationChannel("drown_splash", "rotation", list(times), _align_quats(spl_rs)),
        AnimationChannel("drown_splash", "scale", list(times), spl_ss),
    ]
    if "gaster_group" in scene.nodes:
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), _align_quats(gs_rs)))
    channels.extend(_arm_channels(scene, times, arm_u_l, arm_f_l, asym_phase=asym))
    channels.extend(_eyelid_channels(scene, times, squints))
    return scene.add_animation("Drown", channels, replace=True)


def add_hazard_clips(scene: SceneBuilder, ant_class: str) -> List[AnimationClip]:
    """Attach authentic 1998 shared hazard clips (`BlownBack`, `BlownUp`, `Stunned`, and `Drown` on land ants)."""
    cls = ant_class.strip().lower()
    clips = [
        add_blown_back(scene, cls),
        add_blown_up(scene, cls),
        add_stunned(scene, cls),
    ]
    if cls != "swimmer":
        clips.append(add_drown(scene, cls))
    return clips


__all__ = [
    "BLOWN_BACK_DURATIONS",
    "BLOWN_UP_DURATIONS",
    "DROWN_DURATIONS",
    "STUNNED_DURATIONS",
    "add_blown_back",
    "add_blown_up",
    "add_drown",
    "add_hazard_clips",
    "add_stunned",
]
