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
    from .mesh_core import (
        AnimationChannel,
        MeshPart,
        SceneBuilder,
        curved_tube,
        quat_from_axis_angle,
        quat_from_euler_xyz,
        quat_mul,
        superellipsoid,
    )
except ImportError:
    from mesh_core import (  # type: ignore
        AnimationChannel,
        MeshPart,
        SceneBuilder,
        curved_tube,
        quat_from_axis_angle,
        quat_from_euler_xyz,
        quat_mul,
        superellipsoid,
    )

Vec3 = Tuple[float, float, float]
Quat = Tuple[float, float, float, float]

LEG_NODES: Tuple[str, ...] = ("leg_mid_left", "leg_mid_right", "leg_rear_left", "leg_rear_right")
ARM_NODES: Tuple[str, ...] = ("arm_left", "forearm_left", "arm_right", "forearm_right")
LEG_STYLES: Tuple[str, ...] = ("natural", "worker", "thief", "bomber", "swimmer", "fire", "symmetric")

# Per-leg organic Idle motion personality (non-lockstep variation):
# node -> (knee_bow_mult, splay_mult, phase_rad, swivel_amp_rad)
_LEG_VARIATION: Dict[str, Tuple[float, float, float, float]] = {
    "leg_mid_left": (1.25, 0.90, 0.00, 0.068),
    "leg_mid_right": (1.80, 1.15, 1.55, 0.084),
    "leg_rear_left": (1.50, 1.05, 2.95, 0.076),
    "leg_rear_right": (2.00, 1.25, 4.45, 0.088),
}

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


def _joint_collar(
    name: str,
    center: np.ndarray,
    axis_a: np.ndarray,
    axis_b: np.ndarray,
    radii: Vec3,
    out_bias: float,
) -> MeshPart:
    """Build a sculpted chitin exoskeleton joint condyle collar seated on a knee or ankle bend."""
    ua = axis_a / max(1e-8, float(np.linalg.norm(axis_a)))
    ub = axis_b / max(1e-8, float(np.linalg.norm(axis_b)))
    bis = -(ua + ub)
    bn = float(np.linalg.norm(bis))
    bis = bis / bn if bn > 1e-6 else np.array([0.0, 0.0, 1.0], dtype=np.float64)
    tan = ub - ua
    tn = float(np.linalg.norm(tan))
    tan = tan / tn if tn > 1e-6 else np.array([0.0, 1.0, 0.0], dtype=np.float64)
    lat = np.cross(tan, bis)
    ln = float(np.linalg.norm(lat))
    lat = lat / ln if ln > 1e-6 else np.array([1.0, 0.0, 0.0], dtype=np.float64)
    bis = np.cross(lat, tan)
    pos_c = center + out_bias * bis
    basis = np.column_stack([lat, tan, bis])

    def _place(p: np.ndarray) -> np.ndarray:
        return (p @ basis.T) + pos_c[None, :]

    return superellipsoid(
        radii[0],
        radii[1],
        radii[2],
        e1=0.68,
        e2=0.74,
        u_segs=14,
        v_segs=10,
        name=name,
        material="chitin_team",
    ).deform(_place)


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
    """Build one sculpted segmented exoskeleton digitigrade leg (pinched joints, spindle femur,
    armored knee/ankle condyle collars, angular tibia, 2-segment claw tarsus) pivoted at Y = 0.0.

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

    # 1. Coxa/trochanter + swollen spindle-shaped exoskeleton femur with pinched hip & knee necks.
    p_root = np.array([s * 0.024, p_hip[1] + 0.004, p_hip[2] * 0.62], dtype=np.float64)
    troch = 0.78 * p_hip + 0.22 * p_knee + np.array([0.0, -0.002, 0.0])
    fem_a = 0.56 * p_hip + 0.44 * p_knee + np.array([s * 0.003, 0.008, 0.006])
    fem_b = 0.34 * p_hip + 0.66 * p_knee + np.array([s * 0.003, 0.007, 0.006])
    fem_neck = 0.10 * p_hip + 0.90 * p_knee + np.array([0.0, 0.002, 0.002])
    femur = curved_tube(
        [p_root, p_hip, troch, fem_a, fem_b, fem_neck, p_knee],
        [(0.026, 0.030), (0.024, 0.028), (0.017, 0.020), (0.034, 0.043), (0.032, 0.040), (0.016, 0.019), (0.019, 0.022)],
        samples_per_seg=3,
        radial_segs=12,
        cap_rings=2,
        name=f"leg_{label}_femur",
        material="chitin_team",
    )

    # 2. Angular tapered exoskeleton tibia with pinched sub-knee neck, dorsal calf swell & distal ankle spur.
    tib_neck = 0.88 * p_knee + 0.12 * p_ankle + np.array([0.0, -0.003, 0.002])
    tib_hi = 0.66 * p_knee + 0.34 * p_ankle + np.array([s * 0.003, 0.002, -0.002])
    tib_lo = 0.28 * p_knee + 0.72 * p_ankle + np.array([s * 0.002, 0.002, -0.004])
    tib_spur = 0.12 * p_knee + 0.88 * p_ankle + np.array([s * 0.001, 0.004, -0.006])
    tibia = curved_tube(
        [p_knee, tib_neck, tib_hi, tib_lo, tib_spur, p_ankle],
        [(0.018, 0.020), (0.014, 0.016), (0.023, 0.029), (0.018, 0.022), (0.022, 0.026), (0.013, 0.015)],
        samples_per_seg=3,
        radial_segs=12,
        cap_rings=2,
        name=f"leg_{label}_tibia",
        material="chitin_team",
    )

    # 3. Sculpted armored exoskeleton knee-cap & ankle condyle joint collars wrapping the outer bends.
    knee_cap = _joint_collar(
        f"leg_{label}_knee_cap",
        p_knee,
        p_hip - p_knee,
        p_ankle - p_knee,
        (0.025, 0.028, 0.027),
        out_bias=0.007,
    )
    tars_ref = p_foot + 0.016 * lift - 0.006 * fwd
    ankle_cap = _joint_collar(
        f"leg_{label}_ankle_cap",
        p_ankle,
        p_knee - p_ankle,
        tars_ref - p_ankle,
        (0.019, 0.021, 0.021),
        out_bias=0.005,
    )

    # 4. 2-segment articulated tarsus (pinched basitarsus spindle -> mid-tarsal bead -> curved pointed claw).
    tars_neck = 0.86 * p_ankle + 0.14 * tars_ref
    tars_seg1 = 0.56 * p_ankle + 0.44 * tars_ref + np.array([0.0, -0.002, -0.002])
    tars_mid_pinch = 0.28 * p_ankle + 0.72 * tars_ref
    tars_heel = p_foot + 0.0155 * lift - 0.005 * fwd
    tars_ball = p_foot + 0.0130 * lift + 0.018 * fwd
    tars_tip = p_foot + 0.0055 * lift + 0.044 * fwd
    tarsus = curved_tube(
        [p_ankle, tars_neck, tars_seg1, tars_mid_pinch, tars_heel, tars_ball, tars_tip],
        [(0.013, 0.014), (0.011, 0.012), (0.018, 0.020), (0.0115, 0.0125), (0.0175, 0.0155), (0.014, 0.0125), (0.0055, 0.0050)],
        samples_per_seg=3,
        radial_segs=12,
        cap_rings=3,
        name=f"leg_{label}_tarsus",
        material="chitin_team",
    )

    # Seat only the underside contact patch flush on Y = 0.0 (organic grounded claw, never a flat ski).
    def ground_tarsus(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        p[:, 1] = np.maximum(p[:, 1], 0.0)
        return p

    parts = [femur, knee_cap, tibia, ankle_cap, tarsus.deform(ground_tarsus)]
    # Pivot at the ball of the foot (centroid of the planted contact patch) so flex scaling never slides it.
    ball = p_foot + 0.018 * fwd
    pivot = (float(ball[0]), 0.0, float(ball[2]))
    node = f"leg_{label}"
    scene.add(node, [part.translate(-pivot[0], 0.0, -pivot[2]) for part in parts], parent="legs")
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

    mid_ankle_y = max(0.072, 0.146 - 0.36 * c)
    rear_ankle_y = max(0.068, 0.134 - 0.36 * c)
    anchors: Dict[str, Vec3] = {}
    for side, tag, dz in ((+1, "left", 0.014 * sa), (-1, "right", -0.012 * sa)):
        for kind in ("mid", "rear"):
            if kind == "mid":
                hip = (0.058, 0.314 - c, 0.036)
                knee = (0.124 + 0.12 * c, 0.262 - 0.48 * c, 0.128 + 0.14 * c + 0.5 * dz)
                ankle = (0.120 + 0.08 * c, mid_ankle_y, 0.040 - 0.12 * c + 0.5 * dz)
                foot = (0.114 + 0.06 * c, 0.0, 0.102 + dz)
                yaw = 0.14
            else:
                hip = (0.056, 0.294 - c, -0.078)
                knee = (0.140 + 0.12 * c, 0.258 - 0.48 * c, -0.044 + 0.12 * c + 0.4 * dz)
                ankle = (0.136 + 0.08 * c, rear_ankle_y, -0.132 - 0.12 * c + 0.4 * dz)
                foot = (0.128 + 0.06 * c, 0.0, -0.052 + 0.7 * dz)
                yaw = 0.22
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


def _shortest_arc_quat(v_from: np.ndarray, v_to: np.ndarray) -> Quat:
    """Exact unit quaternion rotating vector `v_from` onto `v_to` along the shortest great-circle arc."""
    n0 = float(np.linalg.norm(v_from))
    n1 = float(np.linalg.norm(v_to))
    if n0 < 1e-12 or n1 < 1e-12:
        return (0.0, 0.0, 0.0, 1.0)
    u = v_from / n0
    w = v_to / n1
    dot = max(-1.0, min(1.0, float(np.dot(u, w))))
    if dot > 1.0 - 1e-12:
        return (0.0, 0.0, 0.0, 1.0)
    if dot < -1.0 + 1e-12:
        ref = np.array([1.0, 0.0, 0.0], dtype=np.float64) if abs(u[0]) < 0.9 else np.array([0.0, 1.0, 0.0], dtype=np.float64)
        axis = np.cross(u, ref)
        axis /= max(1e-12, float(np.linalg.norm(axis)))
        return (float(axis[0]), float(axis[1]), float(axis[2]), 0.0)
    c = np.cross(u, w)
    q = np.array([c[0], c[1], c[2], 1.0 + dot], dtype=np.float64)
    q /= max(1e-12, float(np.linalg.norm(q)))
    return (float(q[0]), float(q[1]), float(q[2]), float(q[3]))


def _solve_knee_scale(
    hip_loc: np.ndarray,
    v_rest_y: float,
    target_vec: np.ndarray,
    *,
    knee_bow: float,
    splay: float,
    flex_bias: float = 0.0,
) -> np.ndarray:
    """Compute smooth anisotropic knee-bend scale (sx, sy, sz) hitting exact `||target_vec||`."""
    d_len = max(1e-6, float(np.linalg.norm(target_vec)))
    c_y = max(0.72, min(1.32, float(target_vec[1]) / max(0.05, float(v_rest_y))))
    drive = (1.0 - c_y) + float(flex_bias)
    sx = max(0.72, min(1.45, 1.0 + splay * drive))
    sz = max(0.62, min(1.75, 1.0 + knee_bow * drive))
    # Smoothly normalize anisotropic knee-bow ratios to match ||target_vec|| identically (zero branch cliff).
    base = np.array([sx, c_y, sz], dtype=np.float64)
    cur_len = max(1e-6, float(np.linalg.norm(base * hip_loc)))
    return base * (d_len / cur_len)


def leg_rig_info(scene: SceneBuilder) -> List[Tuple[str, np.ndarray, np.ndarray]]:
    """Return `(node, foot_pivot_world, hip_world)` for every individual leg node present in `scene`.

    Identifies the true medial hip socket (innermost vertex cluster in the upper half of each leg),
    never the higher forward knee on crouched stances like Thief or Bomber.
    """
    out: List[Tuple[str, np.ndarray, np.ndarray]] = []
    for node in LEG_NODES:
        parts = scene.get_node(node)
        mat = scene.get_world_matrix(node)
        if not parts or mat is None:
            continue
        pos = np.vstack([p.positions for p in parts]).astype(np.float64)
        world = (mat[:3, :3] @ pos.T).T + mat[:3, 3]
        pivot = mat[:3, 3].astype(np.float64)
        y_min = float(world[:, 1].min())
        y_max = float(world[:, 1].max())
        upper = world[world[:, 1] >= y_min + 0.45 * (y_max - y_min)]
        if len(upper) == 0:
            upper = world
        side_sign = 1.0 if float(pivot[0]) >= 0.0 else -1.0
        signed_x = side_sign * upper[:, 0]
        medial = upper[signed_x <= float(signed_x.min()) + 0.030]
        hip = medial.mean(axis=0) if len(medial) > 0 else upper[int(np.argmin(signed_x))]
        out.append((node, pivot, hip))
    return out


def _flex_channels(
    scene: SceneBuilder,
    times: Sequence[float],
    per_leg: Dict[str, List[Vec3]],
    *,
    knee_bow: float,
    splay: float,
    swivel: float = 1.0,
) -> List[AnimationChannel]:
    channels: List[AnimationChannel] = []
    t_start = float(times[0]) if times else 0.0
    t_dur = max(1e-6, float(times[-1]) - t_start) if len(times) >= 2 else 1.0
    swivel_gain = max(0.0, float(swivel))

    for node, pivot, hip in leg_rig_info(scene):
        offsets = per_leg.get(node)
        if not offsets:
            continue
        bow_mult, splay_mult, phase_off, swivel_amp = _LEG_VARIATION.get(node, (1.0, 1.0, 0.0, 0.09))
        xf = scene.get_node_transform(node)
        r0 = xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)
        t0 = np.asarray(xf.translation if xf is not None else pivot, dtype=np.float64)
        s0 = np.asarray(xf.scale if xf is not None else (1.0, 1.0, 1.0), dtype=np.float64)
        local = np.vstack([p.positions for p in scene.get_node(node)]).astype(np.float64)
        y_loc_min = float(local[:, 1].min())
        sole = local[local[:, 1] <= y_loc_min + 0.002]
        foot_low = local[local[:, 1] <= y_loc_min + 0.024]
        sole_rest = _quat_rotate(r0, sole * s0)
        foot_rest_min_y = float(_quat_rotate(r0, foot_low * s0)[:, 1].min())
        sole_rest_xz = sole_rest[:, [0, 2]].mean(axis=0)

        # Rest hip vector in local unscaled space: `t0 + R(r0) @ (s0 * hip_loc) == hip`.
        v_rest = hip - t0
        hip_loc = _quat_rotate(_quat_conj(r0), v_rest) / np.where(np.abs(s0) > 1e-8, s0, 1.0)

        s_vals: List[Vec3] = []
        r_vals: List[Quat] = []
        t_vals: List[Vec3] = []
        prev_q: Optional[Quat] = None
        for idx, (dx, dy, dz) in enumerate(offsets):
            u = (float(times[idx]) - t_start) / t_dur
            phi = 2.0 * math.pi * u + phase_off
            phi2 = 4.0 * math.pi * u + 1.35 * phase_off
            bow_k = knee_bow * bow_mult * (1.0 + 0.16 * math.sin(phi))
            splay_k = splay * splay_mult * (1.0 + 0.14 * math.cos(phi))
            flex_bias = 0.024 * swivel_gain * math.sin(phi)
            twist_ang = swivel_gain * swivel_amp * (0.78 * math.sin(phi) + 0.22 * math.sin(phi2))

            h_target = hip + np.array([dx, dy, dz], dtype=np.float64)
            t_cur = t0.copy()
            s_cur = s0.copy()
            q_cur = r0
            # Damped fixed-point vector-tracking iterations: converge sole re-planting + knee-bend
            # scale + shortest-arc + hip-preserving swivel twist to sub-mm hip & sole closure even
            # on deeply crouched stances like Thief.
            for step in range(6):
                d_vec = h_target - t_cur
                s_rel = _solve_knee_scale(
                    hip_loc * s0,
                    float(v_rest[1]),
                    d_vec,
                    knee_bow=bow_k,
                    splay=splay_k,
                    flex_bias=flex_bias,
                )
                s_cur = s0 * s_rel
                v_scaled = _quat_rotate(r0, hip_loc * s_cur)
                q_arc = _shortest_arc_quat(v_scaled, d_vec)
                if abs(twist_ang) > 1e-7 and float(np.linalg.norm(d_vec)) > 1e-8:
                    q_tw = quat_from_axis_angle(tuple(float(c) for c in d_vec), twist_ang)
                    q_cur = quat_mul(q_tw, quat_mul(q_arc, r0))
                else:
                    q_cur = quat_mul(q_arc, r0)
                if step < 5:
                    sole_now = _quat_rotate(q_cur, sole * s_cur)
                    foot_now = _quat_rotate(q_cur, foot_low * s_cur)
                    dy_sole = float(foot_now[:, 1].min()) - foot_rest_min_y
                    dxz_sole = sole_now[:, [0, 2]].mean(axis=0) - sole_rest_xz
                    # Keep foot sole strictly grounded on Y = 0.0 with bounded tiny lateral recentering.
                    shift_x = max(-0.0025, min(0.0025, float(dxz_sole[0])))
                    shift_z = max(-0.0025, min(0.0025, float(dxz_sole[1])))
                    t_next = np.array([t0[0] - shift_x, t0[1] - dy_sole, t0[2] - shift_z], dtype=np.float64)
                    t_cur = 0.55 * t_next + 0.45 * t_cur
            # Final sole Y micro-clamp (sub-0.1 mm adjustment so min_y == 0.00000 across all foot vertices).
            foot_final = _quat_rotate(q_cur, foot_low * s_cur)
            t_cur[1] = t0[1] - (float(foot_final[:, 1].min()) - foot_rest_min_y)

            if prev_q is not None and sum(a * b for a, b in zip(prev_q, q_cur)) < 0.0:
                q_cur = (-q_cur[0], -q_cur[1], -q_cur[2], -q_cur[3])
            prev_q = q_cur
            s_vals.append((float(s_cur[0]), float(s_cur[1]), float(s_cur[2])))
            r_vals.append(q_cur)
            t_vals.append((float(t_cur[0]), float(t_cur[1]), float(t_cur[2])))
        # Exact periodic loop closure when first & last offsets match.
        if len(offsets) >= 2 and offsets[0] == offsets[-1]:
            s_vals[-1] = s_vals[0]
            q_end = r_vals[0]
            if len(r_vals) >= 2 and sum(a * b for a, b in zip(r_vals[-2], q_end)) < 0.0:
                q_end = (-q_end[0], -q_end[1], -q_end[2], -q_end[3])
            r_vals[-1] = q_end
            t_vals[-1] = t_vals[0]
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
    swivel: float = 1.0,
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
    return _flex_channels(scene, times, per_leg, knee_bow=knee_bow, splay=splay, swivel=swivel)


def sync_leg_flex_to_clip(
    scene: SceneBuilder,
    clip_name: str = "Idle",
    *,
    body_node: str = "upper_body",
    knee_bow: float = 1.6,
    splay: float = 0.45,
    weight_shift: float = 0.0,
    swivel: float = 1.0,
) -> List[AnimationChannel]:
    """Derive per-leg knee flex from `clip_name`'s body translation + rotation and append it to that clip.

    One line for any custom idle (Thief weight pivot, Bomber, Swimmer, Fire): each leg's hip follows
    the animated body exactly (including roll and pitch), while its foot stays planted at Y = 0.0
    and each knee breathes/swivels organically out of phase. Replaces existing leg channels.
    """
    clip = next((c for c in scene.animations if c.name == clip_name), None)
    if clip is None:
        return []
    t_ch = next((ch for ch in clip.channels if ch.target_node == body_node and ch.path == "translation"), None)
    r_ch = next((ch for ch in clip.channels if ch.target_node == body_node and ch.path == "rotation"), None)
    if t_ch is None and r_ch is None:
        return []
    base_times = sorted(set(t_ch.times if t_ch is not None else []) | set(r_ch.times if r_ch is not None else []))
    xf = scene.get_node_transform(body_node)
    t0 = np.asarray(xf.translation if xf is not None else (0.0, 0.0, 0.0), dtype=np.float64)
    q0 = xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)

    def _eval_body(t_val: float) -> Tuple[np.ndarray, Quat]:
        tt = np.asarray(_sample(t_ch.times, t_ch.values, t_val, False), dtype=np.float64) if t_ch is not None else t0
        qq = _sample(r_ch.times, r_ch.values, t_val, True) if r_ch is not None else q0
        return tt, qq

    # Ensure dense enough sampling (>= 24 segments/cycle & subdividing any swing > 0.025) so
    # staggered knee-swivel harmonics and big body arcs track smoothly with sub-mm hip error.
    dur = max(1e-6, float(base_times[-1]) - float(base_times[0])) if len(base_times) >= 2 else 1.0
    max_dt = dur / 24.0
    times: List[float] = []
    frames: List[Tuple[np.ndarray, Quat]] = []
    for idx, t_val in enumerate(base_times):
        cur_f = _eval_body(t_val)
        if idx > 0:
            t_prev = times[-1]
            dt = float(t_val) - t_prev
            jump = float(np.linalg.norm(cur_f[0] - frames[-1][0]))
            q_dot = min(1.0, abs(sum(a * b for a, b in zip(cur_f[1], frames[-1][1]))))
            ang_jump = 2.0 * math.acos(q_dot)
            n_sub = max(1, int(math.ceil(dt / max_dt)), 2 if jump > 0.025 else 1, int(math.ceil(ang_jump / 0.22)))
            for s_i in range(1, n_sub):
                t_mid = round(t_prev + dt * (s_i / n_sub), 5)
                if t_prev < t_mid < float(t_val):
                    times.append(t_mid)
                    frames.append(_eval_body(t_mid))
        times.append(t_val)
        frames.append(cur_f)

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
    new_channels = _flex_channels(scene, times, per_leg, knee_bow=knee_bow, splay=splay, swivel=swivel)
    clip.channels.extend(new_channels)
    return new_channels


def sync_arm_idle_to_clip(
    scene: SceneBuilder,
    clip_name: str = "Idle",
    *,
    left_scale: float = 1.0,
    right_scale: float = 1.0,
    samples: int = 16,
) -> List[AnimationChannel]:
    """Attach asymmetric, out-of-phase organic shoulder + elbow/hand Idle channels to `clip_name`.

    Targets `arm_left`, `forearm_left`, `arm_right`, `forearm_right` with distinct left vs right
    phases and lagging forearm follow-through so hands gesture naturally rather than mirroring.
    Pass `right_scale=0.0` (or `left_scale=0.0`) to keep a prop-holding arm fixed.
    """
    clip = next((c for c in scene.animations if c.name == clip_name), None)
    if clip is None:
        return []
    t_end = max((float(ch.times[-1]) for ch in clip.channels if ch.times), default=1.65)
    t_start = min((float(ch.times[0]) for ch in clip.channels if ch.times), default=0.0)
    dur = max(0.20, t_end - t_start)
    n_steps = max(12, int(samples))
    times = [round(t_start + dur * k / n_steps, 5) for k in range(n_steps + 1)]

    def _rest_r(node: str) -> Quat:
        xf = scene.get_node_transform(node)
        return xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)

    # Distinct organic phase offsets & amplitudes: left (+X) steady poise, right (-X) livelier gesture.
    arm_specs = (
        ("arm_left", "forearm_left", +1.0, float(left_scale), 0.35, 0.90, 0.65, 1.00),
        ("arm_right", "forearm_right", -1.0, float(right_scale), 2.25, 3.10, 0.78, 1.18),
    )
    new_channels: List[AnimationChannel] = []
    arm_set = set(ARM_NODES)
    for upper_node, fore_node, side, gain, ph1, ph2, lag, amp in arm_specs:
        if abs(gain) <= 1e-6:
            continue
        g = gain * amp
        if upper_node in scene.nodes and scene.get_node(upper_node):
            r0 = _rest_r(upper_node)
            u_vals: List[Quat] = []
            for k in range(n_steps + 1):
                u = k / n_steps
                p1 = 2.0 * math.pi * u + ph1
                p2 = 4.0 * math.pi * u + ph2
                q = quat_from_euler_xyz(
                    rx=g * (0.068 * math.sin(p1) + 0.020 * math.sin(p2)),
                    ry=side * g * 0.048 * math.cos(p1 + 0.40),
                    rz=side * g * 0.052 * math.sin(p1 - 0.30),
                )
                u_vals.append(quat_mul(r0, q))
            u_vals[-1] = u_vals[0]
            new_channels.append(AnimationChannel(upper_node, "rotation", list(times), u_vals))

        if fore_node in scene.nodes and scene.get_node(fore_node):
            r0 = _rest_r(fore_node)
            f_vals: List[Quat] = []
            for k in range(n_steps + 1):
                u = k / n_steps
                p1 = 2.0 * math.pi * u + ph1 - lag
                p2 = 4.0 * math.pi * u + ph2 - 0.65 * lag
                q = quat_from_euler_xyz(
                    rx=g * (0.125 * math.sin(p1) + 0.040 * math.sin(p2) - 0.045),
                    ry=side * g * 0.088 * math.cos(p1 + 0.25),
                    rz=side * g * 0.044 * math.sin(p2 + 0.30),
                )
                f_vals.append(quat_mul(r0, q))
            f_vals[-1] = f_vals[0]
            new_channels.append(AnimationChannel(fore_node, "rotation", list(times), f_vals))

    clip.channels = [ch for ch in clip.channels if ch.target_node not in arm_set]
    clip.channels.extend(new_channels)
    return new_channels
