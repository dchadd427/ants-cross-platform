"""Sculpted high-detail 3D model for the iconic Rogue Bandit Thief Ant.

Built on `base_ant` (keeping its toothed mandibles and sturdier anatomy) with
Thief's signature full bandit disguise, rogue gear, and extreme shifty eyes:
- Full wide sculpted woven-gold bandit domino eye-mask + headband (`gear_gold`
  + `gear_gold_metal` stitched trim) ray-cast flush onto the base cranium,
  framing both big eyes cleanly with generous gaze and blink clearance
- Plump side knot on the left temple (`+X`) with two large, sweeping, pleated
  cloth tails flaring boldly back-left across every camera angle
- Extreme shifty lookout eyes: spherical sclera seated in dark orbital rims
  with pivoted `eye_ball_left` / `eye_ball_right` nodes rotating `+-0.62 rad`
  in the `"IdleLookout"` glTF 2.0 clip so pupils dart all the way left and
  right, plus half-lidded `eyelid_left` / `eyelid_right` `"Blink"` animation
- Rogue utility gear: dark leather crossbody strap (`gear_dark`) with brass
  chest buckle (`gear_gold_metal` / `gear_brass_trim`) carrying a compact
  cinched brown canvas loot pouch (`prop_brown` + `gear_gold` drawstring +
  brass clasp) slung neatly at the lower right-back hip

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet grounded cleanly at Y = 0.0)
  +Z = front (South / canonical camera 301)
  +X = ant's left (viewer's right from front), -X = ant's right
"""
from __future__ import annotations

import math
from typing import Dict, List, Sequence, Tuple

import numpy as np

try:
    from .base_ant import EYELID_CLOSED_ANGLE, build_base_ant
    from .mesh_core import (
        AnimationChannel,
        MeshPart,
        SceneBuilder,
        catmull_rom_spline,
        curved_tube,
        ellipsoid,
        hemisphere_shell,
        lathe,
        quat_from_axis_angle,
        recompute_smooth_normals,
        rounded_box,
        sphere,
        superellipsoid,
        torus_arc,
    )
except ImportError:
    from base_ant import EYELID_CLOSED_ANGLE, build_base_ant  # type: ignore
    from mesh_core import (  # type: ignore
        AnimationChannel,
        MeshPart,
        SceneBuilder,
        catmull_rom_spline,
        curved_tube,
        ellipsoid,
        hemisphere_shell,
        lathe,
        quat_from_axis_angle,
        recompute_smooth_normals,
        rounded_box,
        sphere,
        superellipsoid,
        torus_arc,
    )

# Extreme lookout gaze: eyeballs turn all the way into the left/right corners (+-35.5 deg)
# on a spherical sclera without sinking behind the sclera or clipping the dark orbital rim.
GAZE_ANGLE = 0.62

# Subtle mischievous half-lidded rest; lids still seal 100% cleanly on Blink.
_LID_DROOP: Dict[str, Tuple[float, float]] = {
    "eyelid_left": (0.18, 0.08),
    "eyelid_right": (0.12, -0.06),
}

# Nimble asymmetric rogue arm pose (left poised high-forward, right balanced low-forward).
_THIEF_ARM_LEFT = [
    (0.106, 0.440, 0.020),
    (0.212, 0.396, 0.104),
    (0.186, 0.410, 0.222),
    (0.148, 0.430, 0.262),
]
_THIEF_ARM_RIGHT = [
    (-0.106, 0.440, 0.020),
    (-0.226, 0.400, 0.072),
    (-0.208, 0.338, 0.180),
    (-0.176, 0.330, 0.226),
]

_U_SEGS = 72
_V_SEGS = 10


def _normalize(v: np.ndarray, fallback: Tuple[float, float, float] = (0.0, 1.0, 0.0)) -> np.ndarray:
    n = float(np.linalg.norm(v))
    return v / n if n > 1e-10 else np.asarray(fallback, dtype=np.float64)


def _direction(theta: float, elev: float) -> np.ndarray:
    """Unit direction from head center (`theta=0` is +Z front, `+pi/2` is +X ant's left)."""
    return np.array([math.sin(theta) * math.cos(elev), math.sin(elev), math.cos(theta) * math.cos(elev)])


class _CraniumCaster:
    """Ray-cast the base cranium mesh from head center so mask/headband always hugs flush."""

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
    """Elevation angles (bot, top) for the wide bandit headband + forehead brow arch.

    Wraps wide around the temples and back, rises cleanly over the big eyes in
    front, and dips slightly at the nose bridge (`theta = 0`) to join the lower
    domino eye-mask cheek contours.
    """
    ang = math.atan2(math.sin(theta), math.cos(theta))
    abs_a = abs(ang)

    # Top rim stays cleanly below the antenna sockets while sitting proud across the forehead.
    elev_top = 0.455 + 0.075 * math.exp(-((abs_a / 0.90) ** 2))

    # Wide bold wrap along temples and back of the head.
    elev_bot = -0.295

    # Broad, smooth arches over left/right eyes (centered around |theta| ~ 0.42 rad) so the
    # domino cutout stays sleek and wide across both inner and outer corners for gaze & blink.
    eye_arch = math.exp(-(((abs_a - 0.42) / 0.34) ** 2))
    elev_bot += 0.560 * eye_arch

    # Smooth gentle brow-bridge contour between the two eyes (not overly pinched).
    bridge = math.exp(-((abs_a / 0.18) ** 2))
    elev_bot += 0.310 * bridge
    return elev_bot, elev_top


def _build_headband_and_domino_mask(scene: SceneBuilder, caster: _CraniumCaster) -> None:
    """Build the wide woven-gold bandit headband, domino cheek-mask contours, and stitched trim."""
    thetas = np.linspace(0.0, 2.0 * math.pi, _U_SEGS, endpoint=False)
    vs = np.linspace(0.0, 1.0, _V_SEGS)
    dirs = np.zeros((_U_SEGS, _V_SEGS, 3))
    radii = np.zeros((_U_SEGS, _V_SEGS))
    for ui, th in enumerate(thetas):
        e_b, e_t = _band_limits(float(th))
        for vi, v in enumerate(vs):
            dirs[ui, vi] = _direction(float(th), e_b + v * (e_t - e_b))
            radii[ui, vi] = caster.radius(dirs[ui, vi])

    # Bridge cranial center furrow & smooth taut cloth tension across the forehead.
    bridged = np.maximum.reduce([np.roll(radii, k, axis=0) for k in (-2, -1, 0, 1, 2)])
    for _ in range(3):
        bridged = 0.25 * np.roll(bridged, 1, axis=0) + 0.50 * bridged + 0.25 * np.roll(bridged, -1, axis=0)
    bridged = np.maximum(bridged, radii + 0.002)

    ring_len = 2 * _V_SEGS + 2
    rings: List[np.ndarray] = []
    for ui, th in enumerate(thetas):
        outer, inner = [], []
        for vi, v in enumerate(vs):
            edge = math.sin(v * math.pi) ** 0.38
            fold = 0.0028 * math.sin(v * 3.0 * math.pi + 0.5 * math.cos(th)) * edge
            thick = 0.010 + 0.013 * edge + fold
            outer.append(caster.center + dirs[ui, vi] * (bridged[ui, vi] + thick))
            inner.append(caster.center + dirs[ui, vi] * (bridged[ui, vi] - 0.008))
        outer_a, inner_a = np.array(outer), np.array(inner)
        top_mid = 0.5 * (outer_a[-1] + inner_a[-1]) + 0.0055 * _normalize(outer_a[-1] - outer_a[-2])
        bot_mid = 0.5 * (outer_a[0] + inner_a[0]) + 0.0055 * _normalize(outer_a[0] - outer_a[1])
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
    band = MeshPart("bandit_headband", "gear_gold", positions, recompute_smooth_normals(positions, indices), indices).ensure_outward()
    scene.add("gear_bandana", band)

    # Lower domino eye-mask cheek rim curving generously beneath each eye from nose bridge to temple.
    cheek_angles = [
        (0.00, -0.14),
        (0.14, -0.35),
        (0.38, -0.49),
        (0.62, -0.43),
        (0.80, -0.26),
        (0.96, -0.10),
    ]
    cheek_pts = []
    for th, el in cheek_angles:
        d = _direction(th, el)
        cheek_pts.append(caster.center + d * (caster.radius(d) + 0.021))
    cheek_rim = curved_tube(
        cheek_pts,
        [0.020, 0.020, 0.021, 0.022, 0.024, 0.025],
        samples_per_seg=4,
        radial_segs=12,
        cap_rings=2,
        name="domino_cheek_rim",
        material="gear_gold",
    )
    scene.add_mirrored("gear_bandana", cheek_rim)

    # Sculpted vertical nose-bridge tab linking the brow band cleanly down to the cheek rims.
    bridge_pts = []
    for el in (0.30, 0.12, -0.05, -0.18):
        d = _direction(0.0, el)
        bridge_pts.append(caster.center + d * (caster.radius(d) + 0.022))
    scene.add(
        "gear_bandana",
        curved_tube(
            bridge_pts,
            [0.022, 0.018, 0.017, 0.019],
            samples_per_seg=3,
            radial_segs=10,
            cap_rings=2,
            name="domino_nose_bridge",
            material="gear_gold",
        ),
    )

    # Subtle stitched metallic-gold border trim piping around upper and lower headband borders.
    top_trim_pts, bot_trim_pts = [], []
    for ui in range(0, _U_SEGS, 2):
        top_trim_pts.append(caster.center + dirs[ui, -1] * (bridged[ui, -1] + 0.017))
        bot_trim_pts.append(caster.center + dirs[ui, 0] * (bridged[ui, 0] + 0.016))
    scene.add(
        "gear_bandana",
        curved_tube(top_trim_pts, 0.0058, samples_per_seg=2, radial_segs=8, closed=True, name="headband_trim_top", material="gear_gold_metal"),
    )
    scene.add(
        "gear_bandana",
        curved_tube(bot_trim_pts, 0.0054, samples_per_seg=2, radial_segs=8, closed=True, name="headband_trim_bot", material="gear_gold_metal"),
    )


def _pleated_cloth_tail(
    control_pts: Sequence[np.ndarray],
    half_widths: Sequence[float],
    half_thicknesses: Sequence[float],
    face_hint: Sequence[float],
    twist_deg: Tuple[float, float],
    *,
    name: str,
) -> MeshPart:
    """Build a thick, sweeping, gracefully pleated flowing cloth banner tail with closed caps."""
    pts = catmull_rom_spline(control_pts, samples_per_seg=7, closed=False)
    n = len(pts)
    u = np.linspace(0.0, 1.0, n)
    w_arr = np.interp(u, np.linspace(0.0, 1.0, len(half_widths)), half_widths)
    t_arr = np.interp(u, np.linspace(0.0, 1.0, len(half_thicknesses)), half_thicknesses)
    twists = np.radians(np.linspace(twist_deg[0], twist_deg[1], n))
    tangents = np.array([_normalize(pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]) for i in range(n)])

    segs = 16
    angs = np.linspace(0.0, 2.0 * math.pi, segs, endpoint=False)
    cu = np.sign(np.cos(angs)) * (np.abs(np.cos(angs)) ** 0.72)
    # Subtle longitudinal cloth pleat crease along the broad ribbon faces.
    su = np.sign(np.sin(angs)) * (np.abs(np.sin(angs)) ** 0.72) + 0.14 * np.cos(2.0 * angs) * np.sin(angs)
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
    start_r = []
    for k in range(caps - 1, 0, -1):
        ph = (k / caps) * (0.5 * math.pi)
        ctr = pts[0] - t_arr[0] * math.sin(ph) * tangents[0]
        start_r.append(ctr[None, :] + (w_arr[0] * math.cos(ph) * cu)[:, None] * frames_w[0][None, :] + (t_arr[0] * math.cos(ph) * su)[:, None] * frames_o[0][None, :])
    end_r = []
    for k in range(1, caps):
        ph = (k / caps) * (0.5 * math.pi)
        ctr = pts[-1] + 1.4 * t_arr[-1] * math.sin(ph) * tangents[-1]
        end_r.append(ctr[None, :] + (w_arr[-1] * math.cos(ph) * cu)[:, None] * frames_w[-1][None, :] + (t_arr[-1] * math.cos(ph) * su)[:, None] * frames_o[-1][None, :])

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
    """Build the plump folded left-temple knot and two large sweeping pleated cloth tails."""
    theta = math.radians(104.0)
    e_b, e_t = _band_limits(theta)
    d = _direction(theta, 0.45 * (e_b + e_t) + 0.04)
    knot = caster.center + d * (caster.radius(d) + 0.032)

    scene.add(
        "gear_bandana",
        superellipsoid(0.056, 0.050, 0.052, e1=0.72, e2=0.76, u_segs=18, v_segs=14, name="bandana_knot", material="gear_gold")
        .rotate_xyz(rx=0.14, ry=-0.34, rz=0.18)
        .translate(knot),
    )
    scene.add(
        "gear_bandana",
        torus_arc(0.040, 0.013, u_segs=18, v_segs=8, plane="xy", name="knot_cinch_fold", material="gear_gold_metal")
        .rotate_xyz(rx=0.20, ry=1.26, rz=0.18)
        .translate(knot + 0.012 * d),
    )

    up = np.array([0.0, 1.0, 0.0])
    out = _normalize(np.array([1.0, 0.0, -0.34]))
    back = _normalize(np.array([0.24, 0.0, -1.0]))

    # Dramatic sweeping upper cloth tail curving high, outward (+X), and back (-Z).
    upper = [
        knot - 0.010 * out + 0.008 * up,
        knot + 0.068 * out + 0.062 * up + 0.026 * back,
        knot + 0.148 * out + 0.142 * up + 0.072 * back,
        knot + 0.212 * out + 0.205 * up + 0.118 * back,
        knot + 0.248 * out + 0.238 * up + 0.150 * back,
    ]
    # Sweeping lower cloth tail fanning clearly below and back-outward for a bold twin-V silhouette.
    lower = [
        knot - 0.008 * out - 0.010 * up,
        knot + 0.074 * out - 0.036 * up + 0.040 * back,
        knot + 0.162 * out - 0.074 * up + 0.102 * back,
        knot + 0.234 * out - 0.070 * up + 0.164 * back,
        knot + 0.276 * out - 0.044 * up + 0.206 * back,
    ]
    scene.add(
        "gear_bandana",
        _pleated_cloth_tail(
            upper,
            half_widths=[0.024, 0.052, 0.072, 0.054, 0.011],
            half_thicknesses=[0.019, 0.020, 0.019, 0.015, 0.009],
            face_hint=(-0.38, 0.34, 0.86),
            twist_deg=(-8.0, 24.0),
            name="bandana_tail_upper",
        ),
    )
    scene.add(
        "gear_bandana",
        _pleated_cloth_tail(
            lower,
            half_widths=[0.024, 0.048, 0.066, 0.048, 0.011],
            half_thicknesses=[0.018, 0.019, 0.018, 0.014, 0.009],
            face_hint=(-0.22, 0.58, 0.78),
            twist_deg=(10.0, -20.0),
            name="bandana_tail_lower",
        ),
    )


def _build_spherical_eyelid(name: str, radius: float, yaw_y: float) -> MeshPart:
    """Curved chitin_team upper eyelid shell matching Thief's spherical sclera."""
    lid = hemisphere_shell(radius, radius * 1.04, radius, thickness=0.007, start_phi=-0.06, u_segs=24, v_segs=10, name=name, material="chitin_team")
    return lid.rotate_xyz(rx=-0.82, ry=yaw_y)


def _add_shifty_eyes(scene: SceneBuilder, anchors: dict, blink: float, gaze: float) -> None:
    """Build Thief's big spherical shifty eyes (`+-0.62 rad` full left/right dart) + blinking lids.

    A near-spherical sclera centered at each eye pivot lets the iris and pupil
    rotate cleanly all the way into the far-left and far-right corners of the
    orbital socket without sinking behind the sclera or clipping the rim.
    """
    sclera_r = 0.069
    for side, sign in (("left", 1.0), ("right", -1.0)):
        base_eye = np.asarray(anchors[f"eye_{side}"], dtype=np.float64)
        # Seat the spherical pivot slightly deeper so the front apex matches `base_ant` cleanly.
        eye = base_eye + np.array([0.0, 0.0, -0.012])
        yaw = 0.16 * sign

        # Wide dark orbital socket rim framing the eye cleanly inside the domino mask cutout.
        scene.add(
            "eyes",
            torus_arc(0.072, 0.011, u_segs=24, v_segs=8, plane="xy", name=f"eye_socket_rim_{side}", material="chitin_dark")
            .scale(1.04, 1.04, 0.92)
            .rotate_xyz(rx=-0.16, ry=yaw)
            .translate(eye + np.array([0.0, 0.0, 0.018])),
        )
        # True spherical-front warm-white sclera so extreme +-0.62 rad gaze stays flush.
        scene.add(
            "eyes",
            ellipsoid(sclera_r, sclera_r * 1.06, sclera_r, u_segs=24, v_segs=18, name=f"sclera_{side}", material="eye_white")
            .rotate_xyz(rx=-0.16, ry=yaw)
            .translate(eye),
        )

        # Pivoted eyeball node carrying curved iris + pupil right on the spherical sclera surface.
        # Keep iris/pupil apex <= sclera_r + 0.003 so the closed eyelid shell seals with zero poke-through.
        ball = f"eye_ball_{side}"
        iris = (
            ellipsoid(0.039, 0.042, 0.017, u_segs=18, v_segs=12, name=f"iris_{side}", material="eye_iris")
            .rotate_xyz(rx=-0.14)
            .translate(0.0, 0.004, sclera_r - 0.015)
        )
        pupil = (
            ellipsoid(0.022, 0.024, 0.011, u_segs=14, v_segs=10, name=f"pupil_{side}", material="eye_pupil")
            .rotate_xyz(rx=-0.14)
            .translate(0.0, 0.005, sclera_r - 0.008)
        )
        scene.add(ball, [iris, pupil])
        gaze_clamped = max(-1.0, min(1.0, float(gaze)))
        scene.set_node_transform(ball, translation=eye, rotation=quat_from_axis_angle((0.0, 1.0, 0.0), gaze_clamped * GAZE_ANGLE))

        # Half-lidded mischievous eyelid shell pivoted at the exact same spherical center.
        lid = f"eyelid_{side}"
        droop, roll = _LID_DROOP[lid]
        shell = _build_spherical_eyelid(f"{lid}_shell", sclera_r + 0.013, yaw_y=yaw).rotate_xyz(rz=roll).rotate_xyz(rx=droop)
        scene.add(lid, shell)
        remaining = EYELID_CLOSED_ANGLE - droop
        scene.set_node_transform(lid, translation=eye, rotation=quat_from_axis_angle((1.0, 0.0, 0.0), max(0.0, min(1.0, float(blink))) * remaining))

    # Shared "Blink" clip timing (each lid closes smoothly by its remaining angle).
    times = [0.0, 2.40, 2.48, 2.56, 2.68, 3.00]
    blink_channels = []
    for lid, (droop, _) in _LID_DROOP.items():
        q_open = (0.0, 0.0, 0.0, 1.0)
        q_closed = quat_from_axis_angle((1.0, 0.0, 0.0), EYELID_CLOSED_ANGLE - droop)
        blink_channels.append(AnimationChannel(lid, "rotation", list(times), [q_open, q_open, q_closed, q_closed, q_open, q_open]))
    scene.add_animation("Blink", blink_channels)

    # Authentic `atst301` lookout loop (2.52s): dart ALL THE WAY left (-0.62) and ALL THE WAY right (+0.62).
    q_left = quat_from_axis_angle((0.0, 1.0, 0.0), -GAZE_ANGLE)
    q_right = quat_from_axis_angle((0.0, 1.0, 0.0), +GAZE_ANGLE)
    look_times = [0.0, 1.10, 1.26, 2.36, 2.52]
    look_vals = [q_left, q_left, q_right, q_right, q_left]
    scene.add_animation("IdleLookout", [AnimationChannel(f"eye_ball_{s}", "rotation", list(look_times), list(look_vals)) for s in ("left", "right")])


def _sculpted_strap_loop(pts: np.ndarray, out_normals: np.ndarray, half_w: float, half_t: float, name: str) -> MeshPart:
    """Sweep a flat beveled closed leather crossbody strap flush to `out_normals`."""
    n = len(pts)
    segs = 10
    angs = np.linspace(0.0, 2.0 * math.pi, segs, endpoint=False)
    cu = np.sign(np.cos(angs)) * (np.abs(np.cos(angs)) ** 0.65)
    su = np.sign(np.sin(angs)) * (np.abs(np.sin(angs)) ** 0.65)
    rings = []
    for i in range(n):
        t = _normalize(pts[(i + 1) % n] - pts[(i - 1) % n])
        o = _normalize(out_normals[i] - np.dot(out_normals[i], t) * t, (0.0, 0.0, 1.0))
        w = _normalize(np.cross(o, t))
        o = np.cross(t, w)
        rings.append(pts[i][None, :] + (half_w * cu)[:, None] * w[None, :] + (half_t * su)[:, None] * o[None, :])
    positions = np.vstack(rings).astype(np.float32)
    tris: List[Tuple[int, int, int]] = []
    for r in range(n):
        b0, b1 = r * segs, ((r + 1) % n) * segs
        for k in range(segs):
            nk = (k + 1) % segs
            tris.append((b0 + k, b0 + nk, b1 + nk))
            tris.append((b0 + k, b1 + nk, b1 + k))
    indices = np.asarray(tris, dtype=np.uint32).reshape(-1)
    return MeshPart(name, "gear_dark", positions, recompute_smooth_normals(positions, indices), indices).ensure_outward()


def _add_rogue_strap_and_loot_pouch(scene: SceneBuilder) -> None:
    """Add dark leather crossbody utility strap, brass chest buckle, and cinched brown loot pouch."""
    # Conformed closed diagonal strap: over right shoulder (-X) -> across chest -> left waist (+X) -> across back.
    strap_ctrl = [
        (-0.088, 0.452, 0.012),   # over right shoulder beside neck
        (-0.052, 0.405, 0.114),   # upper-right chest
        (0.020, 0.340, 0.122),    # center-left chest
        (0.095, 0.278, 0.068),    # left waist/hip front
        (0.102, 0.275, -0.055),   # left rear hip
        (0.025, 0.345, -0.128),   # mid back
        (-0.072, 0.408, -0.116),  # upper-right back
    ]
    pivot = np.array([0.0, 0.345, -0.010])
    pts = catmull_rom_spline(strap_ctrl, samples_per_seg=6, closed=True)
    out_n = np.array([_normalize(p - pivot + (np.array([0.0, 0.16, 0.0]) if p[1] > 0.42 else 0.0)) for p in pts])

    # Push strap cleanly outside ONLY the thorax meshes (exclude gaster & petiole so back strap hugs snugly).
    thorax_parts = [
        p for p in scene.get_node("body")
        if not p.name.startswith(("gaster", "petiole"))
    ]
    body_verts = np.vstack([p.positions for p in thorax_parts]).astype(np.float64)
    v_rel = body_verts - pivot[None, :]
    shifts = np.zeros(len(pts))
    for i, (p, o) in enumerate(zip(pts, out_n)):
        sub = body_verts[(v_rel @ o) > 0.02]
        if len(sub) == 0:
            continue
        diff = sub - p[None, :]
        proj = diff @ o
        perp = diff - proj[:, None] * o[None, :]
        near = np.sum(perp * perp, axis=1) < (0.040 ** 2)
        if np.any(near):
            shifts[i] = float(np.max(proj[near])) + 0.007
    for _ in range(2):
        shifts = 0.25 * np.roll(shifts, 1) + 0.50 * shifts + 0.25 * np.roll(shifts, -1)
    pts = pts + shifts[:, None] * out_n
    scene.add("gear_rogue", _sculpted_strap_loop(pts, out_n, half_w=0.021, half_t=0.0065, name="rogue_crossbody_strap"))

    # Crisp brass chest buckle + keeper bar seated right on the front strap segment.
    front_idx = int(np.argmax(pts[:, 2]))
    b_pos = pts[front_idx] + 0.009 * out_n[front_idx]
    buckle_frame = rounded_box(0.052, 0.044, 0.015, bevel=0.005, segs=2, name="strap_buckle_frame", material="gear_gold_metal").rotate_xyz(rx=10.0, rz=-42.0, degrees=True).translate(b_pos)
    buckle_inner = rounded_box(0.030, 0.024, 0.018, bevel=0.003, segs=2, name="strap_buckle_cut", material="gear_dark").rotate_xyz(rx=10.0, rz=-42.0, degrees=True).translate(b_pos)
    buckle_pin = rounded_box(0.009, 0.040, 0.018, bevel=0.003, segs=2, name="strap_buckle_pin", material="gear_brass_trim").rotate_xyz(rx=10.0, rz=-42.0, degrees=True).translate(b_pos + np.array([0.0, 0.0, 0.003]))
    scene.add("gear_rogue", [buckle_frame, buckle_inner, buckle_pin])

    # Compact cinched canvas/brown loot pouch slung neatly on the lower right-back side (-X, -Z)
    # above the gaster, linked solidly to the crossbody strap via a dark leather hanger & brass clasp.
    pouch_ctr = np.array([-0.106, 0.312, -0.156])
    pouch_prof = [
        (0.000, -0.072),
        (0.038, -0.068),
        (0.064, -0.046),
        (0.074, -0.012),
        (0.066, +0.024),
        (0.042, +0.046),
        (0.026, +0.058),  # cinched drawstring neck
        (0.042, +0.078),  # gathered ruffled sack top
        (0.030, +0.088),
        (0.000, +0.082),
    ]
    def _sack_pleat(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ang = np.arctan2(p[:, 0], p[:, 2])
        r = np.hypot(p[:, 0], p[:, 2])
        pleat = 1.0 + 0.07 * np.sin(ang * 5.0) * np.clip((p[:, 1] + 0.04) / 0.12, 0.0, 1.0)
        scale = np.where(r > 1e-5, pleat, 1.0)
        p[:, 0] *= scale
        p[:, 2] *= scale
        return p

    sack = (
        lathe(pouch_prof, radial_segs=22, cap_bottom=False, cap_top=False, axis="y", name="loot_pouch_sack", material="prop_brown")
        .deform(_sack_pleat)
        .scale(0.95, 1.0, 1.05)
        .rotate_xyz(rx=-0.28, ry=0.32, rz=-0.36)
        .translate(pouch_ctr)
    )
    scene.add("gear_rogue", sack)

    # Golden woven drawstring cord cinched around the sack neck + two tiny dangling cord ends.
    neck_local = np.array([0.0, 0.058, 0.0])
    neck_ring = (
        torus_arc(0.030, 0.0075, u_segs=20, v_segs=8, plane="xz", name="pouch_drawstring", material="gear_gold")
        .translate(neck_local)
        .rotate_xyz(rx=-0.28, ry=0.32, rz=-0.36)
        .translate(pouch_ctr)
    )
    scene.add("gear_rogue", neck_ring)

    # Tiny dangling golden drawstring tassels + brass clasp anchoring pouch straight to the strap.
    for t_i, dx in enumerate((-0.012, 0.014)):
        tassel = curved_tube(
            [
                pouch_ctr + np.array([0.018 + dx, 0.052, -0.022]),
                pouch_ctr + np.array([0.008 + dx * 1.4, 0.022, -0.044]),
                pouch_ctr + np.array([0.004 + dx * 1.8, -0.004, -0.052]),
            ],
            [0.006, 0.0055, 0.008],
            samples_per_seg=3,
            radial_segs=8,
            cap_rings=2,
            name=f"pouch_tassel_{t_i}",
            material="gear_gold",
        )
        scene.add("gear_rogue", tassel)

    # Find nearest rear-right strap point and bridge solidly with a leather hanger + brass clasp.
    rear_right = np.argmin(np.linalg.norm(pts - (pouch_ctr + np.array([0.025, 0.080, 0.045]))[None, :], axis=1))
    strap_anchor = pts[int(rear_right)]
    pouch_neck_world = pouch_ctr + np.array([0.022, 0.052, 0.018])
    hanger = curved_tube(
        [strap_anchor, 0.5 * (strap_anchor + pouch_neck_world) + np.array([-0.006, 0.012, -0.006]), pouch_neck_world],
        [0.011, 0.010, 0.010],
        samples_per_seg=3,
        radial_segs=8,
        cap_rings=2,
        name="pouch_hanger",
        material="gear_dark",
    )
    clasp = sphere(0.014, u_segs=12, v_segs=8, name="pouch_clasp", material="gear_gold_metal").translate(0.5 * (strap_anchor + pouch_neck_world) + np.array([-0.004, 0.010, -0.004]))
    scene.add("gear_rogue", [hanger, clasp])


def build_thief(blink: float = 0.0, gaze: float = 0.0) -> SceneBuilder:
    """Build the iconic Rogue Bandit Thief Ant.

    `blink` in [0, 1] sets resting eyelid closure; `gaze` in [-1, 1] turns
    the shifty lookout eyes from full far-left (-1) to full far-right (+1).
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
    _build_headband_and_domino_mask(scene, caster)
    _add_knot_and_tails(scene, caster)
    _add_rogue_strap_and_loot_pouch(scene)
    scene.scale_all(0.97)
    scene.ground_to_y(0.0)
    return scene


def build_ant(blink: float = 0.0, gaze: float = 0.0) -> SceneBuilder:
    """Canonical module entrypoint for `build_all.py`."""
    return build_thief(blink=blink, gaze=gaze)


__all__ = ["build_ant", "build_thief"]
