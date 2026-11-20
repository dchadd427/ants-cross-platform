"""Organic per-leg insect stance and grounded knee-flex animation shared by every base ant.

Each of the 4 digitigrade legs is its own glTF node (`leg_mid_left`, `leg_mid_right`,
`leg_rear_left`, `leg_rear_right`) parented to `legs` and pivoted exactly on its own
foot contact point at Y = 0.0. Scale channels compress each leg about its planted foot
(sharpening the forward knee `>` and rearward ankle `<`), and small rotation channels
tilt it to follow sideways or forward body sway, so every foot stays glued to the ground.

Coordinate system (glTF 2.0 right-handed): +Y up, +Z front, +X = ant's left.
"""
from __future__ import annotations

import math
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np

try:
    from .mesh_core import AnimationChannel, SceneBuilder, curved_tube, quat_from_euler_xyz, quat_mul
except ImportError:
    from mesh_core import AnimationChannel, SceneBuilder, curved_tube, quat_from_euler_xyz, quat_mul  # type: ignore

Vec3 = Tuple[float, float, float]
Quat = Tuple[float, float, float, float]

LEG_NODES: Tuple[str, ...] = ("leg_mid_left", "leg_mid_right", "leg_rear_left", "leg_rear_right")
LEG_STYLES: Tuple[str, ...] = ("natural", "worker", "thief", "bomber", "swimmer", "fire", "symmetric")

# Per-leg organic offsets for the canonical "natural" stance (left = +X):
# (outward foot dx, foot stride dz, knee dy, knee dz, ankle dz, foot yaw).
_BASE_OFFSETS: Dict[str, Tuple[float, float, float, float, float, float]] = {
    "mid_left": (0.006, 0.016, -0.012, 0.012, 0.002, 0.07),
    "mid_right": (-0.003, -0.010, 0.004, -0.002, -0.006, -0.05),
    "rear_left": (-0.005, -0.008, 0.006, -0.004, 0.006, -0.04),
    "rear_right": (0.008, 0.012, -0.010, 0.010, 0.008, 0.08),
}

# style -> (amount, mirror left/right, swap mid/rear) so each class stands with its own individuality.
_STYLE_RECIPES: Dict[str, Tuple[float, bool, bool]] = {
    "natural": (1.0, False, False),
    "worker": (1.0, False, False),
    "thief": (1.15, True, False),
    "bomber": (1.05, True, True),
    "swimmer": (0.95, False, True),
    "fire": (0.85, True, True),
    "symmetric": (0.0, False, False),
}

REST_KNEE_BEND = 0.010  # extra relaxed resting knee/ankle flex for every non-symmetric style


def _resolve_offsets(leg_style: str) -> Tuple[float, Dict[str, Tuple[float, ...]]]:
    style = str(leg_style).strip().lower()
    if style not in _STYLE_RECIPES:
        raise ValueError(f"Unknown leg_style {leg_style!r}; expected one of: {', '.join(LEG_STYLES)}")
    amount, mirror_lr, swap_mr = _STYLE_RECIPES[style]
    out: Dict[str, Tuple[float, ...]] = {}
    for label, vals in _BASE_OFFSETS.items():
        kind, tag = label.split("_")
        if mirror_lr:
            tag = "right" if tag == "left" else "left"
        if swap_mr:
            kind = "rear" if kind == "mid" else "mid"
        out[f"{kind}_{tag}"] = tuple(amount * v for v in vals)
    return amount, out


def _add_leg(
    scene: SceneBuilder,
    label: str,
    hip: Vec3,
    knee: Vec3,
    ankle: Vec3,
    foot_pos: Vec3,
    foot_yaw: float,
    side: int,
) -> Vec3:
    """Build one 3-segment digitigrade leg (hip -> forward KNEE > -> rearward ANKLE < -> foot) as its own node.

    Returns the world-space foot pivot (x, 0.0, z) where the node is anchored.
    """
    s = 1.0 if side >= 0 else -1.0
    p_hip = np.array([s * abs(hip[0]), hip[1], hip[2]], dtype=np.float64)
    p_knee = np.array([s * abs(knee[0]), knee[1], knee[2]], dtype=np.float64)
    p_ankle = np.array([s * abs(ankle[0]), ankle[1], ankle[2]], dtype=np.float64)
    p_foot = np.array([s * abs(foot_pos[0]), 0.0, foot_pos[2]], dtype=np.float64)
    yaw_s = s * foot_yaw
    fwd = np.array([math.sin(yaw_s), 0.0, math.cos(yaw_s)], dtype=np.float64)
    lift = np.array([0.0, 1.0, 0.0], dtype=np.float64)

    # Segment 1 (femur): hip socket -> muscular thigh -> forward-bent KNEE (>).
    p_root = np.array([s * 0.044, p_hip[1] + 0.005, p_hip[2] * 0.70], dtype=np.float64)
    femur_mid = 0.48 * p_hip + 0.52 * p_knee + np.array([s * 0.004, 0.016, 0.006])
    pre_knee = 0.18 * p_hip + 0.82 * p_knee + np.array([s * 0.002, 0.006, 0.004])
    # Segment 2 (tibia): KNEE (>) -> sweeping backward-down -> rearward-bent ANKLE (<).
    post_knee = 0.82 * p_knee + 0.18 * p_ankle + np.array([s * 0.003, -0.004, 0.004])
    tibia_mid = 0.48 * p_knee + 0.52 * p_ankle + np.array([s * 0.004, 0.0, -0.004])
    pre_ankle = 0.16 * p_knee + 0.84 * p_ankle + np.array([s * 0.002, 0.004, -0.005])
    # Segment 3 (tarsus): ANKLE (<) -> angling forward-down -> planted tapered claw tip on Y = 0.
    post_ankle = 0.80 * p_ankle + 0.20 * (p_foot + 0.018 * lift) + np.array([0.0, -0.005, -0.003])
    tarsus_mid = 0.42 * p_ankle + 0.58 * (p_foot + 0.014 * lift)
    tarsus_heel = p_foot + 0.014 * lift - 0.006 * fwd
    tarsus_ball = p_foot + 0.010 * lift + 0.018 * fwd
    tarsus_tip = p_foot + 0.0055 * lift + 0.044 * fwd

    leg_tube = curved_tube(
        [
            p_root, p_hip, femur_mid, pre_knee, p_knee,
            post_knee, tibia_mid, pre_ankle, p_ankle,
            post_ankle, tarsus_mid, tarsus_heel, tarsus_ball, tarsus_tip,
        ],
        [
            (0.028, 0.032), (0.026, 0.030), (0.029, 0.035), (0.024, 0.028), (0.022, 0.025),
            (0.020, 0.024), (0.019, 0.022), (0.017, 0.020), (0.016, 0.019),
            (0.015, 0.017), (0.014, 0.016), (0.014, 0.013), (0.011, 0.010), (0.0055, 0.0045),
        ],
        samples_per_seg=3,
        radial_segs=12,
        cap_rings=3,
        name=f"leg_{label}",
        material="chitin_dark",
    )

    # Seat only the underside contact patch flush on Y = 0.0 (organic grounded claw, never a flat ski).
    def ground_tarsus(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        p[:, 1] = np.maximum(p[:, 1], 0.0)
        return p

    leg_tube = leg_tube.deform(ground_tarsus)
    # Pivot at the ball of the foot (centroid of the planted contact patch) so flex scaling never slides it.
    ball = p_foot + 0.018 * fwd
    pivot = (float(ball[0]), 0.0, float(ball[2]))
    node = f"leg_{label}"
    scene.add(node, leg_tube.translate(-pivot[0], 0.0, -pivot[2]), parent="legs")
    scene.set_node_transform(node, translation=pivot)
    return (float(p_foot[0]), 0.0, float(p_foot[2]))


def build_legs(
    scene: SceneBuilder,
    *,
    crouch: float = 0.0,
    stance_asymmetry: float = 0.0,
    leg_style: str = "natural",
) -> Dict[str, Vec3]:
    """Build 4 individual digitigrade legs with natural per-leg stride, splay, and knee bend.

    Crouch compresses the Z-spring (knees forward/out, ankles back/out) while all 4 tarsus
    feet stay planted on Y = 0.0. Returns exact world foot anchors for gear such as flippers.
    """
    c = max(0.0, min(0.24, float(crouch)))
    sa = max(-1.0, min(1.0, float(stance_asymmetry)))
    amount, offs = _resolve_offsets(leg_style)
    bend = REST_KNEE_BEND * min(1.0, amount)
    scene.set_node_transform("legs", translation=(0.0, 0.0, 0.0))

    mid_ankle_y = max(0.072, 0.148 - 0.36 * c)
    rear_ankle_y = max(0.068, 0.136 - 0.36 * c)
    anchors: Dict[str, Vec3] = {}
    for side, tag, dz in ((+1, "left", 0.014 * sa), (-1, "right", -0.012 * sa)):
        for kind in ("mid", "rear"):
            if kind == "mid":
                hip = (0.068, 0.326 - c, 0.014)
                knee = (0.158 + 0.15 * c, 0.296 - 0.54 * c, 0.118 + 0.14 * c + 0.5 * dz)
                ankle = (0.156 + 0.10 * c, mid_ankle_y, 0.008 - 0.12 * c + 0.5 * dz)
                foot = (0.152 + 0.08 * c, 0.0, 0.094 + dz)
                yaw = 0.18
            else:
                hip = (0.066, 0.302 - c, -0.048)
                knee = (0.168 + 0.15 * c, 0.278 - 0.54 * c, 0.022 + 0.12 * c + 0.4 * dz)
                ankle = (0.168 + 0.10 * c, rear_ankle_y, -0.098 - 0.12 * c + 0.4 * dz)
                foot = (0.160 + 0.08 * c, 0.0, -0.028 + 0.7 * dz)
                yaw = 0.26
            label = f"{kind}_{tag}"
            fdx, fdz, kdy, kdz, adz, dyaw = offs[label]
            foot = (foot[0] + fdx, 0.0, foot[2] + fdz)
            knee = (knee[0] + 0.6 * fdx, knee[1] + kdy, knee[2] + kdz + bend + 0.5 * fdz)
            ankle = (ankle[0] + 0.8 * fdx, ankle[1] - 0.4 * bend, ankle[2] + adz - 0.6 * bend + 0.7 * fdz)
            anchors[f"foot_{label}"] = _add_leg(scene, label, hip, knee, ankle, foot, yaw + dyaw, side)
    return anchors


# ---------------------------------------------------------------------------
# Grounded knee-flex animation
# ---------------------------------------------------------------------------

def _quat_rotate(q: Sequence[float], v: np.ndarray) -> np.ndarray:
    x, y, z, w = (float(c) for c in q)
    u = np.array([x, y, z], dtype=np.float64)
    t = 2.0 * np.cross(u, v)
    return v + w * t + np.cross(u, t)


def _quat_conj(q: Sequence[float]) -> Quat:
    return (-float(q[0]), -float(q[1]), -float(q[2]), float(q[3]))


def _sample(times: Sequence[float], values: Sequence[Sequence[float]], t: float, is_quat: bool) -> Tuple[float, ...]:
    if t <= times[0]:
        return tuple(values[0])
    if t >= times[-1]:
        return tuple(values[-1])
    k = int(np.searchsorted(np.asarray(times, dtype=np.float64), t, side="right")) - 1
    t0, t1 = float(times[k]), float(times[k + 1])
    a = (t - t0) / max(1e-9, t1 - t0)
    v0 = np.asarray(values[k], dtype=np.float64)
    v1 = np.asarray(values[k + 1], dtype=np.float64)
    if is_quat:
        if float(np.dot(v0, v1)) < 0.0:
            v1 = -v1
        out = (1.0 - a) * v0 + a * v1
        out /= max(1e-12, float(np.linalg.norm(out)))
        return tuple(float(c) for c in out)
    return tuple(float(c) for c in (1.0 - a) * v0 + a * v1)


def leg_rig_info(scene: SceneBuilder) -> List[Tuple[str, np.ndarray, np.ndarray]]:
    """Return `(node, foot_pivot_world, hip_world)` for every individual leg node present in `scene`."""
    out: List[Tuple[str, np.ndarray, np.ndarray]] = []
    for node in LEG_NODES:
        parts = scene.get_node(node)
        mat = scene.get_world_matrix(node)
        if not parts or mat is None:
            continue
        pos = np.vstack([p.positions for p in parts]).astype(np.float64)
        top = pos[int(np.argmax(pos[:, 1]))]
        hip = (mat @ np.append(top, 1.0))[:3]
        out.append((node, mat[:3, 3].astype(np.float64), hip))
    return out


def _flex_channels(
    scene: SceneBuilder,
    times: Sequence[float],
    per_leg: Dict[str, List[Vec3]],
    *,
    knee_bow: float,
    splay: float,
) -> List[AnimationChannel]:
    channels: List[AnimationChannel] = []
    for node, pivot, hip in leg_rig_info(scene):
        offsets = per_leg.get(node)
        if not offsets:
            continue
        h = max(0.05, float(hip[1] - pivot[1]))
        hx, hz = float(hip[0] - pivot[0]), float(hip[2] - pivot[2])
        xf = scene.get_node_transform(node)
        r0 = xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)
        t0 = np.asarray(xf.translation if xf is not None else pivot, dtype=np.float64)
        local = np.vstack([p.positions for p in scene.get_node(node)]).astype(np.float64)
        sole = local[local[:, 1] <= float(local[:, 1].min()) + 0.002]
        sole_rest = _quat_rotate(r0, sole)
        s_vals: List[Vec3] = []
        r_vals: List[Quat] = []
        t_vals: List[Vec3] = []
        for dx, dy, dz in offsets:
            sy = max(0.80, min(1.20, 1.0 + dy / h))
            sx = 1.0 + splay * (1.0 - sy)
            sz = 1.0 + knee_bow * (1.0 - sy)
            s_vals.append((sx, sy, sz))
            # Tilt about the planted foot so the hip tracks the body exactly (cancelling the drift that
            # the knee-bow scale adds to the hip), leaving only the knee/ankle bend visible.
            rx = max(-0.12, min(0.12, (dz - (sz - 1.0) * hz) / h))
            rz = max(-0.12, min(0.12, ((sx - 1.0) * hx - dx) / h))
            q = quat_mul(r0, quat_from_euler_xyz(rx=rx, rz=rz))
            r_vals.append(q)
            # Re-plant the sole: lowest point exactly on Y = 0.0 and zero mean contact slide.
            sole_now = _quat_rotate(q, sole * np.array([sx, sy, sz]))
            slide = (sole_now - sole_rest).mean(axis=0)
            t_vals.append((float(t0[0] - slide[0]), float(t0[1] - (sole_now[:, 1].min() - sole_rest[:, 1].min())), float(t0[2] - slide[2])))
        channels.append(AnimationChannel(node, "scale", list(times), s_vals))
        channels.append(AnimationChannel(node, "rotation", list(times), r_vals))
        channels.append(AnimationChannel(node, "translation", list(times), t_vals))
    return channels


def add_leg_flex_channels(
    scene: SceneBuilder,
    times: Sequence[float],
    upper_dys: Sequence[float],
    upper_dxs: Optional[Sequence[float]] = None,
    upper_dzs: Optional[Sequence[float]] = None,
    *,
    knee_bow: float = 1.6,
    splay: float = 0.45,
    weight_shift: float = 0.35,
) -> List[AnimationChannel]:
    """Return grounded knee-flex channels following a body offset track; extend your clip's channels with them.

    `upper_dys/dxs/dzs` are per-keyframe body displacements from rest (same length as `times`).
    A dip (dy < 0) compresses each leg about its planted foot and sharpens the knee/ankle bend;
    sideways weight shifts (dx) load the near-side legs a little more and tilt all legs to follow.
    """
    n = len(times)
    if len(upper_dys) != n:
        raise ValueError(f"upper_dys has {len(upper_dys)} values, expected {n}")
    dxs = list(upper_dxs) if upper_dxs is not None else [0.0] * n
    dzs = list(upper_dzs) if upper_dzs is not None else [0.0] * n
    if len(dxs) != n or len(dzs) != n:
        raise ValueError("upper_dxs and upper_dzs must match the length of times")
    per_leg: Dict[str, List[Vec3]] = {}
    for node, pivot, _ in leg_rig_info(scene):
        side = 1.0 if pivot[0] >= 0.0 else -1.0
        per_leg[node] = [
            (float(dxs[k]), float(upper_dys[k]) - weight_shift * side * float(dxs[k]), float(dzs[k])) for k in range(n)
        ]
    return _flex_channels(scene, times, per_leg, knee_bow=knee_bow, splay=splay)


def sync_leg_flex_to_clip(
    scene: SceneBuilder,
    clip_name: str = "Idle",
    *,
    body_node: str = "upper_body",
    knee_bow: float = 1.6,
    splay: float = 0.45,
    weight_shift: float = 0.20,
) -> List[AnimationChannel]:
    """Derive per-leg knee flex from `clip_name`'s body translation + rotation and append it to that clip.

    One line for any custom idle (Thief weight pivot, Bomber, Swimmer, Fire): each leg's hip follows
    the animated body exactly (including roll and pitch), while its foot stays planted at Y = 0.0.
    Existing leg channels in the clip are replaced. Returns the appended channels.
    """
    clip = next((c for c in scene.animations if c.name == clip_name), None)
    if clip is None:
        return []
    t_ch = next((ch for ch in clip.channels if ch.target_node == body_node and ch.path == "translation"), None)
    r_ch = next((ch for ch in clip.channels if ch.target_node == body_node and ch.path == "rotation"), None)
    if t_ch is None and r_ch is None:
        return []
    times = sorted(set(t_ch.times if t_ch is not None else []) | set(r_ch.times if r_ch is not None else []))
    xf = scene.get_node_transform(body_node)
    t0 = np.asarray(xf.translation if xf is not None else (0.0, 0.0, 0.0), dtype=np.float64)
    q0 = xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)
    frames = []
    for t in times:
        tt = np.asarray(_sample(t_ch.times, t_ch.values, t, False), dtype=np.float64) if t_ch is not None else t0
        qq = _sample(r_ch.times, r_ch.values, t, True) if r_ch is not None else q0
        frames.append((tt, qq))

    per_leg: Dict[str, List[Vec3]] = {}
    for node, pivot, hip in leg_rig_info(scene):
        side = 1.0 if pivot[0] >= 0.0 else -1.0
        local = _quat_rotate(_quat_conj(q0), hip - t0)
        vals: List[Vec3] = []
        for tt, qq in frames:
            d = tt + _quat_rotate(qq, local) - hip
            body_dx = float(tt[0] - t0[0])
            vals.append((float(d[0]), float(d[1]) - weight_shift * side * body_dx, float(d[2])))
        per_leg[node] = vals

    leg_set = set(LEG_NODES)
    clip.channels = [ch for ch in clip.channels if ch.target_node not in leg_set]
    new_channels = _flex_channels(scene, times, per_leg, knee_bow=knee_bow, splay=splay)
    clip.channels.extend(new_channels)
    return new_channels
