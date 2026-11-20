"""Sculpted 3D model for the nimble Thief Ant.

Built on `base_ant` (keeping its toothed mandibles and anatomy) with the Thief's
own shifty eyes and signature gear:
- Static sockets and sclera with pivoted `eye_ball_left` / `eye_ball_right`
  nodes (iris + pupil) that dart left and right in the "IdleLookout" glTF 2.0
  clip, replaying the original lookout loop, plus half-lidded `eyelid_left` /
  `eyelid_right` nodes driven by the shared "Blink" timing
- A tightly wrapped woven mustard-gold cloth headband across the brow, ABOVE
  the eyes, leaving the domed crown exposed on top and the eyes unobstructed
- A plump knot on the ant's left (+X) temple with two leaf-shaped cloth tails
  flaring back-left, thick enough to read from every camera angle
- A nimble, asymmetric sneaky arm pose

The headband is fitted by ray-casting the base cranium mesh, so it follows any
change to the base head sculpt without floating or clipping.

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet grounded cleanly at Y = 0.0)
  +Z = front (South / canonical camera 301)
  +X = ant's left (viewer's right from front), -X = ant's right
"""
from __future__ import annotations

import math
from typing import List, Sequence, Tuple

import numpy as np

try:
    from .base_ant import EYELID_CLOSED_ANGLE, _build_eyelid_shell, build_base_ant
    from .mesh_core import (
        AnimationChannel, MeshPart, SceneBuilder, catmull_rom_spline, ellipsoid, quat_from_axis_angle,
        recompute_smooth_normals, superellipsoid, torus_arc,
    )
except ImportError:
    from base_ant import EYELID_CLOSED_ANGLE, _build_eyelid_shell, build_base_ant  # type: ignore
    from mesh_core import (  # type: ignore
        AnimationChannel, MeshPart, SceneBuilder, catmull_rom_spline, ellipsoid, quat_from_axis_angle,
        recompute_smooth_normals, superellipsoid, torus_arc,
    )

# Lookout gaze: eyeballs turn around local +Y; -GAZE_ANGLE looks left, +GAZE_ANGLE looks right.
GAZE_ANGLE = 0.30
# Mischievous half-lidded rest: lids sit pre-rotated by this much (left lid lower, slight roll),
# so the Blink clip only needs the remaining angle to reach the base's sealed pose.
_LID_DROOP = {"eyelid_left": (0.22, 0.10), "eyelid_right": (0.15, -0.08)}

# Nimble asymmetric arms: left hand raised forward (light-fingered), right low and ready.
_THIEF_ARM_LEFT = [(0.114, 0.438, 0.022), (0.214, 0.394, 0.104), (0.188, 0.408, 0.222), (0.150, 0.428, 0.262)]
_THIEF_ARM_RIGHT = [(-0.114, 0.438, 0.022), (-0.228, 0.398, 0.072), (-0.210, 0.338, 0.180), (-0.178, 0.330, 0.226)]

_U_SEGS = 72  # samples around the head
_V_SEGS = 9   # samples across the band height


def _normalize(v: np.ndarray, fallback: Tuple[float, float, float] = (0.0, 1.0, 0.0)) -> np.ndarray:
    n = float(np.linalg.norm(v))
    return v / n if n > 1e-10 else np.asarray(fallback, dtype=np.float64)


def _direction(theta: float, elev: float) -> np.ndarray:
    """Unit direction from the head center; theta = 0 is front (+Z), +pi/2 is ant's left (+X)."""
    return np.array([math.sin(theta) * math.cos(elev), math.sin(elev), math.cos(theta) * math.cos(elev)])


class _CraniumCaster:
    """Ray-cast the base cranium mesh from the head center to find its surface radius."""

    def __init__(self, cranium: MeshPart, center: Sequence[float]):
        tri = cranium.positions.astype(np.float64)[cranium.indices.reshape(-1, 3)]
        self.center = np.asarray(center, dtype=np.float64)
        self.v0 = tri[:, 0] - self.center
        self.e1 = tri[:, 1] - tri[:, 0]
        self.e2 = tri[:, 2] - tri[:, 0]

    def radius(self, d: np.ndarray) -> float:
        h = np.cross(d[None, :], self.e2)
        a = np.einsum("ij,ij->i", self.e1, h)
        ok = np.abs(a) > 1e-12
        f = np.where(ok, 1.0 / np.where(ok, a, 1.0), 0.0)
        s = -self.v0
        u = f * np.einsum("ij,ij->i", s, h)
        q = np.cross(s, self.e1)
        v = f * (q @ d)
        t = f * np.einsum("ij,ij->i", self.e2, q)
        hit = ok & (u >= -1e-6) & (v >= -1e-6) & (u + v <= 1.0 + 1e-6) & (t > 0.0)
        return float(np.max(t[hit])) if np.any(hit) else 0.23


def _band_limits(theta: float) -> Tuple[float, float]:
    """Elevation angles (bottom, top) of the band: above the eyes at the front, lower at the back."""
    c = math.cos(theta)
    return 0.165 + 0.185 * c, 0.470 + 0.170 * c


def _build_headband(caster: _CraniumCaster) -> MeshPart:
    """Watertight woven cloth band shell wrapped snugly around the cranium."""
    thetas = np.linspace(0.0, 2.0 * math.pi, _U_SEGS, endpoint=False)
    vs = np.linspace(0.0, 1.0, _V_SEGS)
    dirs = np.zeros((_U_SEGS, _V_SEGS, 3))
    radii = np.zeros((_U_SEGS, _V_SEGS))
    for ui, th in enumerate(thetas):
        e_b, e_t = _band_limits(float(th))
        for vi, v in enumerate(vs):
            dirs[ui, vi] = _direction(float(th), e_b + v * (e_t - e_b))
            radii[ui, vi] = caster.radius(dirs[ui, vi])
    # Bridge the center forehead furrow and socket dips the way stretched cloth would.
    bridged = np.maximum.reduce([np.roll(radii, k, axis=0) for k in (-2, -1, 0, 1, 2)])

    ring_len = 2 * _V_SEGS + 2
    rings: List[np.ndarray] = []
    for ui, th in enumerate(thetas):
        weave = 0.0018 * math.sin(th * 46.0)
        outer, inner = [], []
        for vi, v in enumerate(vs):
            edge = math.sin(v * math.pi) ** 0.4
            thick = 0.008 + 0.012 * edge + weave * edge + 0.0014 * math.sin(v * 3.0 * math.pi)
            outer.append(caster.center + dirs[ui, vi] * (bridged[ui, vi] + thick))
            inner.append(caster.center + dirs[ui, vi] * (bridged[ui, vi] - 0.006))
        outer_a, inner_a = np.array(outer), np.array(inner)
        top_mid = 0.5 * (outer_a[-1] + inner_a[-1]) + 0.005 * _normalize(outer_a[-1] - outer_a[-2])
        bot_mid = 0.5 * (outer_a[0] + inner_a[0]) + 0.005 * _normalize(outer_a[0] - outer_a[1])
        rings.append(np.vstack([outer_a, top_mid[None, :], inner_a[::-1], bot_mid[None, :]]))

    positions = np.vstack(rings).astype(np.float32)
    tris: List[Tuple[int, int, int]] = []
    for ui in range(_U_SEGS):
        b0, b1 = ui * ring_len, ((ui + 1) % _U_SEGS) * ring_len
        for k in range(ring_len):
            nk = (k + 1) % ring_len
            tris.append((b0 + k, b1 + k, b1 + nk))
            tris.append((b0 + k, b1 + nk, b0 + nk))
    indices = np.asarray(tris, dtype=np.uint32).reshape(-1)
    return MeshPart("bandit_headband", "gear_gold", positions, recompute_smooth_normals(positions, indices), indices).ensure_outward()


def _leaf_tail(
    control_pts: Sequence[np.ndarray],
    half_widths: Sequence[float],
    half_thicknesses: Sequence[float],
    face_hint: Sequence[float],
    twist_deg: Tuple[float, float],
    *,
    name: str,
) -> MeshPart:
    """Thick leaf-shaped cloth tail: wide belly, pointed tip, rounded closed caps."""
    pts = catmull_rom_spline(control_pts, samples_per_seg=7, closed=False)
    n = len(pts)
    u = np.linspace(0.0, 1.0, n)
    w_arr = np.interp(u, np.linspace(0.0, 1.0, len(half_widths)), half_widths)
    t_arr = np.interp(u, np.linspace(0.0, 1.0, len(half_thicknesses)), half_thicknesses)
    twists = np.radians(np.linspace(twist_deg[0], twist_deg[1], n))
    tangents = np.array([_normalize(pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]) for i in range(n)])

    segs = 14
    angs = np.linspace(0.0, 2.0 * math.pi, segs, endpoint=False)
    cu = np.sign(np.cos(angs)) * np.abs(np.cos(angs)) ** 0.72
    su = np.sign(np.sin(angs)) * np.abs(np.sin(angs)) ** 0.72
    hint = _normalize(np.asarray(face_hint, dtype=np.float64))
    frames_w, frames_o, rings = [], [], []
    for i in range(n):
        tg = tangents[i]
        o0 = _normalize(hint - np.dot(hint, tg) * tg, (0.0, 0.0, 1.0))
        w0 = _normalize(np.cross(o0, tg))
        o0 = np.cross(tg, w0)
        c, s = math.cos(twists[i]), math.sin(twists[i])
        o, w = c * o0 + s * w0, -s * o0 + c * w0
        frames_w.append(w)
        frames_o.append(o)
        rings.append(pts[i][None, :] + (w_arr[i] * cu)[:, None] * w[None, :] + (t_arr[i] * su)[:, None] * o[None, :])

    caps = 3
    start = [pts[0] - t_arr[0] * math.sin(k / caps * 0.5 * math.pi) * tangents[0] for k in range(caps - 1, 0, -1)]
    start_r = [p[None, :] + (w_arr[0] * math.cos(k / caps * 0.5 * math.pi) * cu)[:, None] * frames_w[0][None, :] + (t_arr[0] * math.cos(k / caps * 0.5 * math.pi) * su)[:, None] * frames_o[0][None, :] for p, k in zip(start, range(caps - 1, 0, -1))]
    end = [pts[-1] + 1.4 * t_arr[-1] * math.sin(k / caps * 0.5 * math.pi) * tangents[-1] for k in range(1, caps)]
    end_r = [p[None, :] + (w_arr[-1] * math.cos(k / caps * 0.5 * math.pi) * cu)[:, None] * frames_w[-1][None, :] + (t_arr[-1] * math.cos(k / caps * 0.5 * math.pi) * su)[:, None] * frames_o[-1][None, :] for p, k in zip(end, range(1, caps))]
    all_rings = start_r + rings + end_r
    apex0 = pts[0] - t_arr[0] * tangents[0]
    apex1 = pts[-1] + 1.4 * t_arr[-1] * tangents[-1]
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


def _add_knot_and_tails(scene: SceneBuilder, caster: _CraniumCaster) -> None:
    """Plump knot on the left (+X) temple and two leaf-shaped tails flaring back-left."""
    theta = math.radians(100.0)
    e_b, e_t = _band_limits(theta)
    d = _direction(theta, 0.5 * (e_b + e_t))
    knot = caster.center + d * (caster.radius(d) + 0.030)

    scene.add("gear_bandana", superellipsoid(0.044, 0.040, 0.040, e1=0.72, e2=0.76, u_segs=18, v_segs=14, name="bandana_knot", material="gear_gold").rotate_xyz(rx=0.12, ry=-0.30, rz=0.18).translate(knot))
    fold = torus_arc(0.032, 0.011, u_segs=18, v_segs=8, plane="xy", name="knot_fold", material="gear_gold").rotate_xyz(rx=0.20, ry=1.30, rz=0.18).translate(knot + 0.010 * d)
    scene.add("gear_bandana", fold)

    up = np.array([0.0, 1.0, 0.0])
    out = _normalize(np.array([1.0, 0.0, -0.35]))
    back = _normalize(np.array([0.25, 0.0, -1.0]))
    upper = [knot - 0.008 * out, knot + 0.056 * out + 0.040 * up + 0.020 * back, knot + 0.120 * out + 0.075 * up + 0.052 * back, knot + 0.172 * out + 0.084 * up + 0.084 * back, knot + 0.200 * out + 0.078 * up + 0.104 * back]
    lower = [knot - 0.008 * out, knot + 0.058 * out - 0.022 * up + 0.036 * back, knot + 0.126 * out - 0.040 * up + 0.084 * back, knot + 0.180 * out - 0.036 * up + 0.126 * back, knot + 0.206 * out - 0.024 * up + 0.150 * back]
    scene.add("gear_bandana", _leaf_tail(upper, [0.020, 0.044, 0.056, 0.040, 0.008], [0.016, 0.016, 0.015, 0.012, 0.008], (-0.25, 0.55, 0.80), (-6.0, 18.0), name="bandana_tail_upper"))
    scene.add("gear_bandana", _leaf_tail(lower, [0.020, 0.042, 0.052, 0.036, 0.008], [0.016, 0.016, 0.014, 0.011, 0.008], (-0.15, 0.70, 0.70), (8.0, -16.0), name="bandana_tail_lower"))


def _add_shifty_eyes(scene: SceneBuilder, anchors: dict, blink: float, gaze: float) -> None:
    """Static sockets + sclera, pivoted darting eyeballs (iris + pupil), droopy blinkable lids.

    Exports the shared "Blink" clip (eyelids) and the "IdleLookout" clip (eyeballs) that
    replays the original lookout loop: hold left, dart right, hold right, dart back.
    """
    for side, sign in (("left", 1.0), ("right", -1.0)):
        eye = np.asarray(anchors[f"eye_{side}"], dtype=np.float64)
        yaw = 0.20 * sign
        scene.add("eyes", torus_arc(0.066, 0.011, u_segs=22, v_segs=8, plane="xy", name=f"eye_socket_rim_{side}", material="chitin_dark").scale(0.95, 1.04, 0.90).rotate_xyz(rx=-0.18, ry=yaw).translate(eye + np.array([0.0, 0.0, 0.006])))
        scene.add("eyes", superellipsoid(0.063, 0.069, 0.052, e1=0.88, e2=0.90, u_segs=20, v_segs=16, name=f"sclera_{side}", material="eye_white").rotate_xyz(rx=-0.18, ry=yaw).translate(eye))

        # Eyeball node: iris + pupil in eye-local space on the sclera front, turned around +Y.
        ball = f"eye_ball_{side}"
        scene.add(ball, ellipsoid(0.034, 0.038, 0.020, u_segs=18, v_segs=12, name=f"iris_{side}", material="eye_iris").rotate_xyz(rx=-0.16, ry=0.15 * sign).translate(-0.004 * sign, 0.003, 0.038))
        scene.add(ball, ellipsoid(0.019, 0.021, 0.014, u_segs=14, v_segs=10, name=f"pupil_{side}", material="eye_pupil").rotate_xyz(rx=-0.16, ry=0.13 * sign).translate(-0.006 * sign, 0.004, 0.047))
        scene.set_node_transform(ball, translation=eye, rotation=quat_from_axis_angle((0.0, 1.0, 0.0), max(-1.0, min(1.0, float(gaze))) * GAZE_ANGLE))

        lid = f"eyelid_{side}"
        droop, roll = _LID_DROOP[lid]
        shell = _build_eyelid_shell(f"{lid}_shell", yaw_y=0.18 * sign).rotate_xyz(rz=roll).rotate_xyz(rx=droop)
        scene.add(lid, shell)
        remaining = EYELID_CLOSED_ANGLE - droop
        scene.set_node_transform(lid, translation=eye, rotation=quat_from_axis_angle((1.0, 0.0, 0.0), max(0.0, min(1.0, float(blink))) * remaining))

    # Blink: same timing as the shared clip, each lid closing by its remaining angle.
    times = [0.0, 2.40, 2.48, 2.56, 2.68, 3.00]
    blink_channels = []
    for lid, (droop, _) in _LID_DROOP.items():
        q_open, q_closed = (0.0, 0.0, 0.0, 1.0), quat_from_axis_angle((1.0, 0.0, 0.0), EYELID_CLOSED_ANGLE - droop)
        blink_channels.append(AnimationChannel(lid, "rotation", list(times), [q_open, q_open, q_closed, q_closed, q_open, q_open]))
    scene.add_animation("Blink", blink_channels)

    # IdleLookout: authentic lookout loop (2.52 s): hold left, snappy dart right, hold, dart back.
    q_left, q_right = quat_from_axis_angle((0.0, 1.0, 0.0), -GAZE_ANGLE), quat_from_axis_angle((0.0, 1.0, 0.0), GAZE_ANGLE)
    look_times = [0.0, 1.10, 1.26, 2.36, 2.52]
    look_vals = [q_left, q_left, q_right, q_right, q_left]
    scene.add_animation("IdleLookout", [AnimationChannel(f"eye_ball_{s}", "rotation", list(look_times), list(look_vals)) for s in ("left", "right")])


def build_thief(blink: float = 0.0, gaze: float = 0.0) -> SceneBuilder:
    """Build the nimble Thief Ant.

    `blink` in [0, 1] sets the resting eyelid closure; `gaze` in [-1, 1] turns the eyeballs
    from full left (-1) to full right (+1).
    """
    scene, anchors = build_base_ant(
        scene_name="ant_thief",
        include_eyes=False,
        include_brows=False,
        left_arm_pose=_THIEF_ARM_LEFT,
        right_arm_pose=_THIEF_ARM_RIGHT,
    )
    _add_shifty_eyes(scene, anchors, blink, gaze)
    cranium = next(p for p in scene.get_node("head") if p.name == "cranium")
    caster = _CraniumCaster(cranium, anchors["head_center"])
    scene.add("gear_bandana", _build_headband(caster))
    _add_knot_and_tails(scene, caster)
    scene.scale_all(0.96)
    scene.ground_to_y(0.0)
    return scene


def build_ant(blink: float = 0.0, gaze: float = 0.0) -> SceneBuilder:
    """Canonical module entrypoint for `build_all.py`."""
    return build_thief(blink=blink, gaze=gaze)


__all__ = ["build_ant", "build_thief"]
