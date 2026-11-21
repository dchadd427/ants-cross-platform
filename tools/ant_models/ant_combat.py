"""Sculpted high-detail 3D model for the Combat Ant: an armored gorilla commando.

Built directly with `mesh_core.py` (independent of `base_ant`) to match the
original `acst` low, wide, rounded hunched gorilla silhouette (~2.5x Worker
width, ~0.98-1.02x Worker height, compact tucked depth) with smooth riveted
armor and real sculpted hardware detail:
- Wide armored heart-shield head tucked low and forward with a compact domed
  crown, crisp vertical & V-plate cranial seams with steel rivets, bold V-brow
  ridge, huge fierce 3-part eyes, plump lobster-claw mandibles with inner bite
  teeth, and short kinked dark antennae
- Pivoted `eyelid_left` / `eyelid_right` nodes with the shared glTF 2.0 "Blink"
  clip and strict radial clearance outside sclera/iris/pupil across all frames
- Smooth rounded hunched dome/egg carapace torso, overlapping curved scale-
  plated pauldrons studded with `gear_steel` rivets, wide-sweeping curved
  muscular ape arms with riveted gauntlets and dark clawed fists, short tucked
  smooth flowing dark muscular legs (zero knee glitches) grounded at Y = 0.0,
  and a compact tucked 3-step gaster
- Crossed ribbed woven-gold bullet bandoliers over chest AND back with brass
  cartridge casings, tips, and riveted front/back X-junction buckles
- Signature hot-magenta (`bomb_charge`) Rambo cloth strip wrapped snugly around
  the RIGHT (-X) upper arm with a tied outer knot and twin fluttering V-tails

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
    from .base_ant import EYELID_CLOSED_ANGLE, sync_leg_flex_to_clip
    from .mesh_core import (
        AnimationChannel,
        MeshPart,
        SceneBuilder,
        catmull_rom_spline,
        cone,
        curved_tube,
        cylinder,
        ellipsoid,
        hemisphere_shell,
        lathe,
        quat_from_axis_angle,
        quat_from_euler_xyz,
        quat_mul,
        recompute_smooth_normals,
        rounded_box,
        sphere,
        superellipsoid,
        swept_tube,
        torus_arc,
    )
except ImportError:
    from base_ant import EYELID_CLOSED_ANGLE, sync_leg_flex_to_clip  # type: ignore
    from mesh_core import (  # type: ignore
        AnimationChannel,
        MeshPart,
        SceneBuilder,
        catmull_rom_spline,
        cone,
        curved_tube,
        cylinder,
        ellipsoid,
        hemisphere_shell,
        lathe,
        quat_from_axis_angle,
        quat_from_euler_xyz,
        quat_mul,
        recompute_smooth_normals,
        rounded_box,
        sphere,
        superellipsoid,
        swept_tube,
        torus_arc,
    )

Vec3 = Tuple[float, float, float]

# Lower, forward-leaning linebacker commando head tucked prowling ahead of massive shoulders (`acst301`/`901`).
HEAD_CENTER: Vec3 = (0.0, 0.958, 0.176)
HEAD_SCALE = 1.00
EYE_OFFSET: Vec3 = (0.098, -0.002, 0.128)  # left eye relative to head center

# Wide-sweeping curved gorilla arm path (left +X; right -X mirrors cleanly; ~1.28 W total).
ARM_PTS_LEFT: List[Vec3] = [
    (0.320, 0.680, -0.015),   # broad rounded shoulder socket
    (0.482, 0.560, 0.032),    # bulging outer ape bicep
    (0.514, 0.372, 0.105),    # sweeping muscular outer elbow/forearm
    (0.445, 0.236, 0.215),    # heavy beveled gauntlet wrist
    (0.396, 0.190, 0.268),    # low crouched dark fist center
]


def _normalize(v: np.ndarray, fallback: Vec3 = (0.0, 1.0, 0.0)) -> np.ndarray:
    n = float(np.linalg.norm(v))
    if n > 1e-10:
        return v / n
    fb = np.asarray(fallback, dtype=np.float64)
    return fb / float(np.linalg.norm(fb))


def _frame(origin: Sequence[float], x_axis: np.ndarray, y_axis: np.ndarray, z_axis: np.ndarray) -> np.ndarray:
    mat = np.eye(4, dtype=np.float64)
    mat[:3, 0], mat[:3, 1], mat[:3, 2], mat[:3, 3] = x_axis, y_axis, z_axis, np.asarray(origin, dtype=np.float64)
    return mat


def _axis_frame(p0: Sequence[float], p1: Sequence[float]) -> Tuple[np.ndarray, float]:
    a, b = np.asarray(p0, dtype=np.float64), np.asarray(p1, dtype=np.float64)
    y = _normalize(b - a)
    ref = np.array([0.0, 0.0, 1.0]) if abs(y[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
    x = _normalize(np.cross(y, ref))
    z = np.cross(x, y)
    return _frame(a, x, y, z), float(np.linalg.norm(b - a))


def _ring_on_segment(
    p0: Sequence[float],
    p1: Sequence[float],
    frac: float,
    major_r: float,
    minor_r: float,
    *,
    name: str,
    material: str,
) -> MeshPart:
    mat, length = _axis_frame(p0, p1)
    ring = torus_arc(major_r, minor_r, u_segs=18, v_segs=6, plane="xz", name=name, material=material)
    return ring.translate(0.0, frac * length, 0.0).transform(mat)


def _rivet(pos: np.ndarray, normal: np.ndarray, name: str, radius: float = 0.012, material: str = "gear_steel") -> MeshPart:
    n = _normalize(normal)
    ref = np.array([1.0, 0.0, 0.0]) if abs(n[0]) < 0.9 else np.array([0.0, 0.0, 1.0])
    x = _normalize(np.cross(ref, n))
    z = np.cross(x, n)
    dome = ellipsoid(radius, 0.56 * radius, radius, u_segs=8, v_segs=5, name=name, material=material)
    return dome.transform(_frame(pos, x, n, z))


def _super_top(rx: float, ry: float, rz: float, e1: float, e2: float, lx: float, lz: float) -> float:
    h = (abs(lx / rx) ** (2.0 / e2) + abs(lz / rz) ** (2.0 / e2)) ** (e2 / e1)
    return ry * max(0.0, 1.0 - h) ** (e1 / 2.0)


def _sculpted_ribbon_mesh(
    pts: np.ndarray,
    out_normals: np.ndarray,
    half_widths: np.ndarray,
    half_thicknesses: np.ndarray,
    *,
    closed: bool = False,
    profile_segs: int = 8,
    name: str = "ribbon",
    material: str = "gear_dark",
) -> MeshPart:
    """Sweep a smooth beveled strap or cloth band oriented flush to `out_normals`."""
    n = len(pts)
    tangents = np.zeros((n, 3), dtype=np.float64)
    for i in range(n):
        d = pts[(i + 1) % n] - pts[(i - 1) % n] if closed else pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]
        tangents[i] = _normalize(d)

    angs = np.linspace(0.0, 2.0 * math.pi, profile_segs, endpoint=False)
    cu = np.sign(np.cos(angs)) * (np.abs(np.cos(angs)) ** 0.68)
    su = np.sign(np.sin(angs)) * (np.abs(np.sin(angs)) ** 0.68)

    rings: List[np.ndarray] = []
    for i in range(n):
        t = tangents[i]
        o = _normalize(out_normals[i] - np.dot(out_normals[i], t) * t, (0.0, 0.0, 1.0))
        w = _normalize(np.cross(o, t), (1.0, 0.0, 0.0))
        o = _normalize(np.cross(t, w), (0.0, 1.0, 0.0))
        rings.append(pts[i][None, :] + (half_widths[i] * cu)[:, None] * w[None, :] + (half_thicknesses[i] * su)[:, None] * o[None, :])

    blocks: List[np.ndarray] = []
    if not closed:
        blocks.append((pts[0] - half_thicknesses[0] * tangents[0])[None, :])
    ring_off = len(blocks)
    blocks.extend(rings)
    end_cap_idx = ring_off + n * profile_segs
    if not closed:
        blocks.append((pts[-1] + half_thicknesses[-1] * tangents[-1])[None, :])

    positions = np.vstack(blocks).astype(np.float32)
    tris: List[Tuple[int, int, int]] = []
    if not closed:
        tris.extend((0, ring_off + (k + 1) % profile_segs, ring_off + k) for k in range(profile_segs))
    for r in range(n if closed else n - 1):
        b0, b1 = ring_off + r * profile_segs, ring_off + ((r + 1) % n) * profile_segs
        for k in range(profile_segs):
            nk = (k + 1) % profile_segs
            tris.append((b0 + k, b0 + nk, b1 + nk))
            tris.append((b0 + k, b1 + nk, b1 + k))
    if not closed:
        b_last = ring_off + (n - 1) * profile_segs
        tris.extend((end_cap_idx, b_last + k, b_last + (k + 1) % profile_segs) for k in range(profile_segs))

    indices = np.asarray(tris, dtype=np.uint32).reshape(-1)
    return MeshPart(name, material, positions, recompute_smooth_normals(positions, indices), indices).ensure_outward()


# ---------------------------------------------------------------------------
# Rounded hunched dome torso, curved scale-plated pauldrons, compact gaster
# ---------------------------------------------------------------------------

def _upright_torso_deform(pos: np.ndarray) -> np.ndarray:
    """Sculpt a big, powerful armored commando upper back & lats tapering to hips (with room for proud neck)."""
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    upper = np.clip((y + 0.04) / 0.24, 0.0, 1.0)
    lower = np.clip(-y / 0.24, 0.0, 1.0)
    p[:, 0] = x * (1.0 + 0.11 * upper - 0.24 * (lower ** 1.45))
    p[:, 2] = z * (1.0 - 0.14 * (lower ** 1.35))
    # Fuller, deeper muscular upper back & lats curving smoothly down behind the elevated neck.
    back_swell = np.clip((-z) / 0.22, 0.0, 1.0) * np.exp(-((y - 0.04) / 0.18) ** 2)
    p[:, 2] -= 0.030 * back_swell
    spine = np.exp(-(x / 0.024) ** 2) * np.clip((-z) / 0.18, 0.0, 1.0)
    p[:, 2] += 0.009 * spine
    return p


def _build_torso(scene: SceneBuilder) -> np.ndarray:
    """Build powerful broad commando torso with big upper back, elevated dark neck, pauldrons, and gaster."""
    core = superellipsoid(0.248, 0.214, 0.206, e1=0.84, e2=0.88, u_segs=20, v_segs=14, name="thorax_core", material="chitin_dark").translate(0.0, 0.505, -0.026)
    # Massive, powerful armored upper back & lats (~0.835 shoulder crown, sitting cleanly below elevated neck!).
    dorsal = (
        superellipsoid(0.336, 0.232, 0.248, e1=0.82, e2=0.86, u_segs=26, v_segs=18, name="dorsal_carapace", material="chitin_team")
        .deform(_upright_torso_deform)
        .rotate_xyz(rx=0.08)
        .translate(0.0, 0.608, -0.056)
    )
    chest = superellipsoid(0.266, 0.196, 0.160, e1=0.82, e2=0.86, u_segs=20, v_segs=14, name="barrel_chest", material="chitin_team").rotate_xyz(rx=0.08).translate(0.0, 0.540, 0.072)
    pec_l = superellipsoid(0.132, 0.104, 0.068, e1=0.82, e2=0.84, u_segs=16, v_segs=12, name="pectoral_plate", material="chitin_team").rotate_xyz(rx=0.14, ry=0.28, rz=-0.06).translate(0.118, 0.576, 0.148)
    left_pec, right_pec = scene.add_mirrored("body", pec_l)
    scene.add("body", [core, dorsal, chest])

    # Rounded segmented lower abdominal plates tapering neatly into the crouched pelvis.
    abs_parts: List[MeshPart] = []
    for i, (y, rx, rz, z) in enumerate(((0.394, 0.172, 0.112, 0.068), (0.336, 0.140, 0.094, 0.046))):
        plate = superellipsoid(rx, 0.038, rz, e1=0.76, e2=0.82, u_segs=18, v_segs=10, name=f"ab_plate_{i}", material="chitin_team").rotate_xyz(rx=0.14).translate(0.0, y, z)
        seam = torus_arc(rx * 0.94, 0.0065, u_segs=18, v_segs=6, plane="xz", name=f"ab_seam_{i}", material="chitin_dark").scale(1.0, 1.0, rz / rx).rotate_xyz(rx=0.14).translate(0.0, y - 0.020, z)
        abs_parts.append(plate)
        scene.add("body", seam)
    scene.add("body", abs_parts)

    pelvis = superellipsoid(0.165, 0.096, 0.142, e1=0.86, e2=0.88, u_segs=18, v_segs=12, name="pelvis_core", material="chitin_dark").translate(0.0, 0.300, -0.055)
    # Thick muscular dark neck angling forward out of the broad armored shoulders (linebacker stance).
    neck = superellipsoid(0.142, 0.118, 0.156, e1=0.70, e2=0.82, u_segs=18, v_segs=12, name="neck_collar", material="chitin_dark").rotate_xyz(rx=0.40).translate(0.0, 0.794, 0.104)
    neck_ring = torus_arc(0.140, 0.013, u_segs=20, v_segs=8, plane="xz", name="neck_band", material="chitin_dark").scale(1.02, 1.0, 0.94).rotate_xyz(rx=0.40).translate(0.0, 0.816, 0.122)
    scene.add("body", [pelvis, neck, neck_ring])

    # Subtle recessed dark shoulder-socket joint rings separating torso cleanly from the sculpted arms.
    sh_socket = (
        ellipsoid(0.056, 0.084, 0.086, u_segs=14, v_segs=10, name="shoulder_socket", material="chitin_dark")
        .rotate_xyz(rz=-0.24)
        .translate(0.324, 0.672, -0.014)
    )
    scene.add_mirrored("body", sh_socket)

    # Compact pointed 3-step gaster tucked tightly down-back behind the hips (keeps 901 E depth tight!).
    gaster_profile = [
        (0.000, 0.000), (0.030, 0.018), (0.068, 0.050), (0.060, 0.056),
        (0.094, 0.084), (0.106, 0.118), (0.096, 0.125),
        (0.112, 0.148), (0.108, 0.192), (0.078, 0.228), (0.000, 0.248),
    ]
    tilt = -0.34
    gaster = (
        lathe(gaster_profile, radial_segs=22, cap_bottom=False, cap_top=False, axis="z", name="gaster", material="chitin_team")
        .scale(1.06, 0.94, 1.0)
        .rotate_xyz(rx=tilt)
        .translate(0.0, 0.195, -0.345)
    )
    scene.add("gaster", gaster)
    for idx, (z_ring, maj_r) in enumerate(((-0.224, 0.098), (-0.288, 0.062))):
        y_ring = 0.195 + (z_ring + 0.345) * math.sin(-tilt)
        ring = (
            torus_arc(maj_r, 0.0065, u_segs=20, v_segs=6, plane="xy", name=f"gaster_seam_{idx}", material="chitin_dark")
            .scale(1.06, 0.94, 1.0)
            .rotate_xyz(rx=tilt)
            .translate(0.0, y_ring, z_ring)
        )
        scene.add("gaster", ring)

    shell = [core, dorsal, chest, left_pec, right_pec, pelvis] + abs_parts
    return np.vstack([p.positions for p in shell]).astype(np.float64)


# ---------------------------------------------------------------------------
# Wide armored shield head, huge fierce eyes, zero-phase eyelids, lobster claws
# ---------------------------------------------------------------------------

def _commando_head_deform(pos: np.ndarray) -> np.ndarray:
    """Sculpt a compact, strong, softly-rounded boxy commando head (ZERO pointy alien chin!)."""
    p = pos.copy()
    x, y, z = pos[:, 0], pos[:, 1], pos[:, 2]
    ax = np.abs(x)
    front = np.clip((z + 0.02) / 0.18, 0.0, 1.0)

    # Strong squared-off softly-rounded crown & broad temples (non-egg, non-alien!).
    crown = np.clip((y - 0.10) / 0.09, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.04 * (crown ** 1.8)
    p[:, 2] *= 1.0 - 0.04 * (crown ** 1.8)

    # Crisp overhanging V-angled brow shelf sweeping across both fierce eyes.
    brow_y = 0.044 + 0.18 * np.minimum(ax, 0.21)
    brow_shelf = np.exp(-((y - brow_y) / 0.036) ** 2) * np.clip((z - 0.05) / 0.14, 0.0, 1.0)
    p[:, 2] += 0.024 * brow_shelf

    # Crisp narrow vertical center cranial seam + subtle V forehead armor-plate groove.
    v_seam = np.exp(-(x / 0.013) ** 2) * np.clip((y + 0.022) / 0.075, 0.0, 1.0) * front
    v_plate_y = 0.050 + 0.44 * ax
    plate_seam = np.exp(-((y - v_plate_y) / 0.014) ** 2) * np.clip((ax - 0.012) / 0.04, 0.0, 1.0) * np.clip((0.180 - ax) / 0.04, 0.0, 1.0) * front
    radial = p / np.maximum(1e-6, np.linalg.norm(p, axis=1, keepdims=True))
    p -= (0.012 * v_seam + 0.007 * plate_seam)[:, None] * radial

    # Deep recessed orbital hollows sized cleanly for the soulful olive-iris eyes.
    socket = np.exp(-(((ax - EYE_OFFSET[0]) / 0.074) ** 2 + ((y - EYE_OFFSET[1]) / 0.064) ** 2)) * np.clip((z - 0.04) / 0.14, 0.0, 1.0)
    p[:, 2] -= 0.040 * socket

    # Full, strong, gently-rounded commando jaw & cheeks (only subtle -14% taper, ZERO alien chin!).
    lower = np.clip((-y - 0.025) / 0.165, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.14 * (lower ** 1.45)
    p[:, 2] *= 1.0 - 0.08 * (lower ** 1.45)
    return p


# Fierce scowling upper eyelid droop tuned to reveal crisp bright white sclera around compact pupils.
COMBAT_LID_DROOP = 0.22


def _build_eyelid_shell(name: str, sign: float, r_outer: float, thickness: float) -> MeshPart:
    """Spherical upper eyelid shell with fierce inward V-scowl & menacing resting half-droop."""
    lid = hemisphere_shell(r_outer, r_outer, r_outer, thickness=thickness, start_phi=-0.12, u_segs=26, v_segs=12, name=name, material="chitin_team")
    # Tilt sharply inward toward nose bridge (`rz = +0.20 * sign`) and drop into fierce scowl rest.
    return lid.rotate_xyz(rx=-0.96, ry=0.26 * sign).rotate_xyz(rz=0.18 * sign).rotate_xyz(rx=COMBAT_LID_DROOP)


def _sculpt_head(scene: SceneBuilder, blink: float) -> None:
    hx, hy, hz = HEAD_CENTER
    # Compact, strong, softly-rounded boxy commando head (`e1=0.72, e2=0.78`, full rounded jaw!).
    cranium = (
        superellipsoid(0.226, 0.182, 0.186, e1=0.72, e2=0.78, u_segs=32, v_segs=24, name="cranium", material="chitin_team")
        .deform(_commando_head_deform)
        .translate(hx, hy, hz)
    )
    scene.add("head", cranium)

    # Thick, fiercely furrowed V-shaped armored brow ridge diving low toward the nose bridge!
    brow_pts = [
        (0.012, hy + 0.022, hz + 0.198),
        (0.068, hy + 0.048, hz + 0.200),
        (0.136, hy + 0.068, hz + 0.174),
        (0.194, hy + 0.078, hz + 0.120),
        (0.218, hy + 0.068, hz + 0.052),
    ]
    brow_rads = [(0.028, 0.024), (0.038, 0.029), (0.039, 0.030), (0.032, 0.024), (0.021, 0.017)]
    brow = curved_tube(brow_pts, brow_rads, samples_per_seg=4, radial_segs=12, cap_rings=3, name="brow_ledge", material="chitin_team")
    scene.add_mirrored("head", brow)
    # Recessed dark center scowl furrow right where the thick V-brows collide above the nose bridge.
    scene.add("head", ellipsoid(0.015, 0.024, 0.016, u_segs=12, v_segs=8, name="brow_furrow", material="chitin_dark").translate(0.0, hy + 0.024, hz + 0.196))

    # Living, sparkling 5-part fierce eyes (ivory sclera + dark limbal ring + olive iris + obsidian
    # pupil + dual studio-aligned wet-eye catchlight glints) glaring slightly up from under the brow!
    ex, ey, ez = EYE_OFFSET
    eye_l = np.array([ex, hy + ey, hz + ez])
    lid_thick = 0.0065
    r_eye_max = 0.077
    for node, tag, sign in (("eyelid_left", "left", 1.0), ("eyelid_right", "right", -1.0)):
        eye = np.array([sign * ex, hy + ey, hz + ez])
        yaw = 0.27 * sign
        scene.add(
            "eyes",
            torus_arc(0.080, 0.010, u_segs=26, v_segs=8, plane="xy", name=f"orbital_rim_{tag}", material="chitin_dark")
            .scale(1.02, 0.89, 0.86)
            .rotate_xyz(rx=-0.08, ry=yaw)
            .translate(eye + np.array([0.002 * sign, -0.002, 0.004])),
        )
        scene.add(
            "eyes",
            ellipsoid(0.082, 0.071, 0.046, u_segs=20, v_segs=12, name=f"orbital_socket_{tag}", material="chitin_dark")
            .rotate_xyz(rx=-0.08, ry=yaw)
            .translate(eye + np.array([0.0, 0.0, -0.016])),
        )
        sclera = (
            superellipsoid(0.077, 0.067, 0.064, e1=0.94, e2=0.96, u_segs=24, v_segs=16, name=f"sclera_{tag}", material="eye_white")
            .rotate_xyz(rx=-0.08, ry=yaw)
            .translate(eye)
        )
        # Focused compact 1998-style glossy black dot pupils (~40% of eyeball face) leaving plenty
        # of crisp bright white sclera clearly visible beneath the fierce V-scowl brow (`acst301`).
        limbal = (
            ellipsoid(0.032, 0.031, 0.018, u_segs=22, v_segs=12, name=f"limbal_{tag}", material="eye_limbal")
            .translate(-0.004 * sign, 0.002, 0.0520)
            .rotate_xyz(rx=-0.10, ry=0.18 * sign)
            .translate(eye)
        )
        iris = (
            ellipsoid(0.029, 0.028, 0.019, u_segs=20, v_segs=12, name=f"iris_{tag}", material="eye_iris")
            .translate(-0.004 * sign, 0.0025, 0.0525)
            .rotate_xyz(rx=-0.10, ry=0.18 * sign)
            .translate(eye)
        )
        pupil = (
            ellipsoid(0.026, 0.025, 0.016, u_segs=18, v_segs=10, name=f"pupil_{tag}", material="eye_pupil")
            .translate(-0.005 * sign, 0.003, 0.0565)
            .rotate_xyz(rx=-0.10, ry=0.16 * sign)
            .translate(eye)
        )
        # Single clean soft upper-left (-X, +Y) catchlight dot matching the iconic 1998 black-dot eye look.
        glint_main = (
            ellipsoid(0.0074, 0.0080, 0.0022, u_segs=14, v_segs=10, name=f"catchlight_main_{tag}", material="eye_catchlight")
            .rotate_xyz(rx=-0.16, ry=-0.20)
            .translate(-0.010, 0.009, 0.0678)
            .rotate_xyz(rx=-0.08, ry=0.17 * sign)
            .translate(eye)
        )
        scene.add("eyes", [sclera, limbal, iris, pupil, glint_main])
        eye_pts = np.vstack([p.positions for p in (sclera, limbal, iris, pupil, glint_main)]).astype(np.float64)
        r_eye_max = max(r_eye_max, float(np.max(np.linalg.norm(eye_pts - eye[None, :], axis=1))))

    # Spherical eyelids strictly outside max eye+catchlight radius + 0.0082 (stays >= 6.5 mm after 0.80x scale)!
    lid_outer_r = r_eye_max + 0.0082 + lid_thick
    rem = EYELID_CLOSED_ANGLE - COMBAT_LID_DROOP
    rest = quat_from_axis_angle((1.0, 0.0, 0.0), max(0.0, min(1.0, float(blink))) * rem)
    for node, sign in (("eyelid_left", 1.0), ("eyelid_right", -1.0)):
        scene.add(node, _build_eyelid_shell(f"{node}_shell", sign=sign, r_outer=lid_outer_r, thickness=lid_thick))
        scene.set_node_transform(node, translation=(sign * eye_l[0], eye_l[1], eye_l[2]), rotation=rest)
    scene.add_blink_animation(("eyelid_left", "eyelid_right"), closed_angle=rem, name="Blink")

    # Flush tapered V-chin clypeus + dark mouth recess + plump chunky lobster-claw mandibles.
    scene.add(
        "head",
        superellipsoid(0.044, 0.024, 0.032, e1=0.78, e2=0.84, u_segs=16, v_segs=10, name="clypeus", material="chitin_team")
        .rotate_xyz(rx=0.34)
        .translate(0.0, hy - 0.104, hz + 0.146),
    )
    scene.add("head", ellipsoid(0.048, 0.028, 0.034, u_segs=14, v_segs=10, name="mouth_cleft", material="chitin_dark").translate(0.0, hy - 0.136, hz + 0.144))
    scene.add_mirrored(
        "head",
        ellipsoid(0.040, 0.036, 0.034, u_segs=14, v_segs=10, name="mandible_socket", material="chitin_dark")
        .translate(0.094, hy - 0.114, hz + 0.116),
    )
    # Plump, chunky rounded lobster-claw pods (blunt rounded mitten tips, zero skinny tusks!).
    jaw = curved_tube(
        [
            (0.092, hy - 0.112, hz + 0.120),
            (0.106, hy - 0.138, hz + 0.172),
            (0.086, hy - 0.156, hz + 0.214),
            (0.052, hy - 0.160, hz + 0.228),
        ],
        [(0.036, 0.038), (0.048, 0.046), (0.044, 0.040), (0.032, 0.030)],
        samples_per_seg=4,
        radial_segs=14,
        cap_rings=4,
        name="mandible_claw",
        material="chitin_team",
    )
    scene.add_mirrored("head", jaw)
    # Small crisp rounded pale/off-white inner pincer bite teeth clearly visible along the inner-front claw edge.
    for t_idx, (tx, ty, tz, rx, ry, rz, yaw_deg) in enumerate((
        (0.060, hy - 0.132, hz + 0.194, 0.012, 0.010, 0.012, 22.0),
        (0.049, hy - 0.140, hz + 0.214, 0.013, 0.010, 0.012, 10.0),
        (0.034, hy - 0.147, hz + 0.229, 0.012, 0.010, 0.011, -8.0),
    )):
        tooth = (
            superellipsoid(rx, ry, rz, e1=0.68, e2=0.72, u_segs=12, v_segs=8, name=f"mandible_tooth_{t_idx}", material="fuse_white")
            .rotate_xyz(ry=yaw_deg, degrees=True)
            .translate(tx, ty, tz)
        )
        scene.add_mirrored("head", tooth)

    # Asymmetric battle-ready dark antennae: left (+X) cocked forward, right (-X) swept boldly back!
    ant_specs = (
        ("left",  (+0.114, hy + 0.142, hz + 0.060), (+0.132, hy + 0.208, hz + 0.072), (+0.156, hy + 0.262, hz + 0.088), (+0.184, hy + 0.280, hz + 0.146), (+0.196, hy + 0.242, hz + 0.192), -0.30),
        ("right", (-0.114, hy + 0.142, hz + 0.052), (-0.138, hy + 0.214, hz + 0.036), (-0.172, hy + 0.278, hz + 0.020), (-0.212, hy + 0.300, hz + 0.056), (-0.226, hy + 0.276, hz + 0.104), +0.34),
    )
    for tag, base, mid_a, elbow, club1, club2, rz_s in ant_specs:
        scene.add("antennae", swept_tube([base, mid_a, elbow], [0.026, 0.022, 0.025], radial_segs=12, smooth_subdiv=2, cap_rings=2, name=f"antenna_scape_{tag}", material="chitin_dark"))
        scene.add("antennae", curved_tube([elbow, club1, club2], [0.024, 0.043, 0.052], samples_per_seg=5, radial_segs=14, cap_rings=5, name=f"antenna_club_{tag}", material="chitin_dark"))
        scene.add("antennae", ellipsoid(0.039, 0.021, 0.039, u_segs=12, v_segs=8, name=f"antenna_socket_{tag}", material="chitin_dark").rotate_xyz(rx=0.20, rz=rz_s).translate(*base))


def _build_head(scene: SceneBuilder, blink: float) -> None:
    head = SceneBuilder(name="combat_head")
    _sculpt_head(head, blink)
    head.translate_all(-HEAD_CENTER[0], -HEAD_CENTER[1], -HEAD_CENTER[2])
    head.scale_all(HEAD_SCALE)
    head.translate_all(*HEAD_CENTER)
    scene.merge_from(head)


# ---------------------------------------------------------------------------
# Wide-sweeping curved gorilla arms, riveted gauntlets, short flowing legs
# ---------------------------------------------------------------------------

def _build_arms(scene: SceneBuilder) -> Tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Build clean, distinct sculpted muscular ape arms (zero stacked scales, rivets, or lumpy blobs!).

    Returns `(right_bicep_center, right_bicep_tangent, right_arm_verts, shoulder_cap_verts)`.
    """
    pts = [np.asarray(p, dtype=np.float64) for p in ARM_PTS_LEFT]
    sh, mid, el, wr, fi = pts
    pts_r = [np.array([-p[0], p[1], p[2]], dtype=np.float64) for p in pts]
    sh_r, mid_r, el_r, wr_r, fi_r = pts_r

    # Beefy sculpted ape upper arms (~1.24x fuller deltoid-to-bicep shoulder transition)
    # flowing smoothly into muscular forearms & cohesive sculpted green brawler fists.
    scene.add("left_upper_arm", [
        curved_tube([sh, 0.52 * sh + 0.48 * mid, mid, el], [0.100, 0.126, 0.128, 0.092], samples_per_seg=5, radial_segs=18, cap_rings=4, name="gorilla_upper_left", material="chitin_team"),
        sphere(0.090, u_segs=14, v_segs=10, name="elbow_joint_left", material="chitin_team").translate(el),
    ])
    scene.add("left_forearm", curved_tube([el, 0.45 * el + 0.55 * wr, wr, fi], [0.090, 0.098, 0.072, 0.064], samples_per_seg=5, radial_segs=16, cap_rings=4, name="gorilla_forearm_left", material="chitin_team"))
    scene.add("right_upper_arm", [
        curved_tube([sh_r, 0.52 * sh_r + 0.48 * mid_r, mid_r, el_r], [0.100, 0.126, 0.128, 0.092], samples_per_seg=5, radial_segs=18, cap_rings=4, name="gorilla_upper_right", material="chitin_team"),
        sphere(0.090, u_segs=14, v_segs=10, name="elbow_joint_right", material="chitin_team").translate(el_r),
    ])
    scene.add("right_forearm", curved_tube([el_r, 0.45 * el_r + 0.55 * wr_r, wr_r, fi_r], [0.090, 0.098, 0.072, 0.064], samples_per_seg=5, radial_segs=16, cap_rings=4, name="gorilla_forearm_right", material="chitin_team"))

    def _add_arm_pair(part: MeshPart, *, right_node: str) -> None:
        scene.add(right_node.replace("right_", "left_"), part)
        scene.add(right_node, part.mirror_x(f"{part.name}_right"))

    # Full, smooth, meaty deltoid shoulder caps blending seamlessly into the beefier upper biceps
    # (zero armor scales or rivets — lets the thick yellow X-sash drape right on top cleanly!).
    deltoid_l = (
        superellipsoid(0.122, 0.112, 0.122, e1=0.84, e2=0.86, u_segs=18, v_segs=12, name="deltoid_cap", material="chitin_team")
        .rotate_xyz(rz=-0.54)
        .translate(0.402, 0.644, -0.004)
    )
    deltoid_r = deltoid_l.mirror_x("deltoid_cap_right")
    scene.add("left_upper_arm", deltoid_l)
    scene.add("right_upper_arm", deltoid_r)

    # Smooth organic brachioradialis upper-forearm muscle swell (glossy green chitin, zero bands/cuffs/rivets!).
    forearm_muscle = (
        superellipsoid(0.076, 0.074, 0.104, e1=0.82, e2=0.86, u_segs=16, v_segs=12, name="forearm_muscle", material="chitin_team")
        .rotate_xyz(rx=0.62, ry=-0.42, rz=0.16)
        .translate(0.482, 0.312, 0.156)
    )
    _add_arm_pair(forearm_muscle, right_node="right_forearm")

    # Bold, cohesive organic green brawler fists/pincer-hands (`chitin_team` back/outer knuckle shell
    # & sculpted curled finger/thumb lobes with a subtle recessed `chitin_dark` inner palm crease,
    # matching `acst301`/`acst201` powerful inward-curving fists cleanly in rest & flex!).
    _add_arm_pair(
        superellipsoid(0.074, 0.070, 0.086, e1=0.74, e2=0.78, u_segs=18, v_segs=14, name="fist_back", material="chitin_team")
        .rotate_xyz(rx=0.28, ry=-0.48, rz=0.14)
        .translate(fi + np.array([0.004, 0.002, 0.006])),
        right_node="right_forearm",
    )
    _add_arm_pair(
        superellipsoid(0.056, 0.056, 0.062, e1=0.78, e2=0.82, u_segs=14, v_segs=10, name="fist_palm_inner", material="chitin_dark")
        .rotate_xyz(rx=0.25, ry=-0.48, rz=0.14)
        .translate(fi + np.array([-0.022, -0.010, 0.020])),
        right_node="right_forearm",
    )
    # Chunky sculpted inward-curving brawler finger-pincer lobes & opposable thumb in `chitin_team`.
    pincer_lobes = (
        ("fist_finger_upper", [(0.032, 0.030, 0.044), (0.012, 0.016, 0.104), (-0.036, -0.006, 0.114)], [0.034, 0.036, 0.022]),
        ("fist_finger_lower", [(0.028, -0.024, 0.040), (0.008, -0.040, 0.096), (-0.036, -0.056, 0.102)], [0.032, 0.034, 0.020]),
        ("fist_thumb",        [(-0.036, 0.034, 0.024), (-0.068, 0.018, 0.072), (-0.046, -0.014, 0.098)], [0.030, 0.030, 0.020]),
    )
    for lobe_name, ctrl, rads in pincer_lobes:
        c_pts = [fi + np.asarray(c) for c in ctrl]
        _add_arm_pair(curved_tube(c_pts, rads, samples_per_seg=4, radial_segs=14, cap_rings=4, name=lobe_name, material="chitin_team"), right_node="right_forearm")

    # Sample right (-X) mid-to-lower bicep (`idx = 13`) cleanly below the smooth deltoid cap
    # and just above the elbow bend so the hot-magenta cloth strip sits at authentic `acst301`/`901` height!
    sampled = catmull_rom_spline(ARM_PTS_LEFT, samples_per_seg=10)
    idx = 13
    p_l = sampled[idx]
    t_l = _normalize(sampled[idx + 1] - sampled[idx - 1])
    right_arm_verts = np.vstack([p.positions for p in scene.get_node("right_upper_arm") + scene.get_node("right_forearm")]).astype(np.float64)
    upper_arm_verts = np.vstack([p.positions for p in scene.get_node("left_upper_arm") + scene.get_node("right_upper_arm")]).astype(np.float64)
    shoulder_cap_verts = np.vstack([deltoid_l.positions, deltoid_r.positions, upper_arm_verts[upper_arm_verts[:, 1] > 0.56]])
    return np.array([-p_l[0], p_l[1], p_l[2]]), _normalize(np.array([-t_l[0], t_l[1], t_l[2]])), right_arm_verts, shoulder_cap_verts


def _sturdy_foot(name: str) -> MeshPart:
    """Broad sturdy armored dark insect claw-foot resting flat on Y = 0.0."""
    prof = [(0.000, -0.042), (0.032, -0.030), (0.046, 0.004), (0.040, 0.046), (0.022, 0.080), (0.000, 0.096)]
    foot = lathe(prof, radial_segs=14, cap_bottom=False, cap_top=False, axis="z", name=name, material="leg_chitin").scale(1.12, 0.72, 1.0)
    foot = foot.deform(lambda p: np.column_stack([p[:, 0], np.maximum(p[:, 1], -0.018), p[:, 2]]))
    return foot.translate(0.0, -float(foot.bounds()[0][1]), 0.0)


def _build_flowing_leg(
    scene: SceneBuilder,
    node_name: str,
    ctrl: Sequence[Vec3],
    foot_yaw: float,
    side: float,
) -> None:
    """One individual digitigrade backwards-knee (`< \\`) muscular dark insect leg pivoted at its planted foot."""
    pts = [np.array([side * abs(p[0]), p[1], p[2]], dtype=np.float64) for p in ctrl]
    leg = curved_tube(
        pts,
        [(0.056, 0.060), (0.052, 0.056), (0.048, 0.050), (0.044, 0.046), (0.036, 0.038), (0.032, 0.034)],
        samples_per_seg=5,
        radial_segs=16,
        cap_rings=3,
        name=f"{node_name}_tube",
        material="leg_chitin",
    )
    yaw_s = side * foot_yaw
    foot_x = float(pts[-1][0])
    foot_z = float(pts[-1][2] + 0.008)
    foot = _sturdy_foot(f"{node_name}_tarsus").rotate_xyz(ry=yaw_s).translate(foot_x, 0.0, foot_z)
    # Pivot cleanly on the ball of the planted foot at Y = 0.0 so knee-flex never lifts or slides the sole!
    pivot = (foot_x + 0.016 * math.sin(yaw_s), 0.0, foot_z + 0.016 * math.cos(yaw_s))
    leg_loc = leg.translate(-pivot[0], 0.0, -pivot[2])
    foot_loc = foot.translate(-pivot[0], 0.0, -pivot[2])
    scene.add(node_name, [leg_loc, foot_loc], parent="legs")
    scene.set_node_transform(node_name, translation=pivot)


def _build_legs(scene: SceneBuilder) -> None:
    """4 individual digitigrade backwards-knee dark insect legs with organic non-mirrored brawler stance.

    Hip roots start deeply inside `pelvis_core` (`|x| ~ 0.092`) and sit strictly higher than the
    backwards knees so zero daylight gap ever opens during chest-heave & flex knee-bending.
    """
    scene.set_node_transform("legs", translation=(0.0, 0.0, 0.0))
    # Subtle per-leg brawler stance individuality: (outward foot dx, foot stride dz, knee dy, knee dz, dyaw).
    stance = {
        "mid_left":   (+0.010, +0.020, -0.010, -0.010, +0.06),
        "mid_right":  (-0.006, -0.016, +0.006, +0.006, -0.05),
        "rear_left":  (-0.008, -0.014, +0.008, +0.006, -0.04),
        "rear_right": (+0.012, +0.018, -0.010, -0.012, +0.07),
    }
    for side, tag in ((+1.0, "left"), (-1.0, "right")):
        dx, dz, kdy, kdz, dyaw = stance[f"mid_{tag}"]
        _build_flowing_leg(
            scene,
            f"leg_mid_{tag}",
            [
                (0.092, 0.318, +0.018),
                (0.162, 0.308, +0.028 + 0.2 * dz),
                (0.228 + 0.5 * dx, 0.280 + 0.4 * kdy, -0.028 + 0.5 * kdz),
                (0.264 + 0.8 * dx, 0.226 + kdy, -0.078 + kdz + 0.4 * dz),
                (0.248 + 0.9 * dx, 0.118 + 0.4 * kdy, +0.006 + 0.8 * dz),
                (0.232 + dx, 0.032, +0.084 + dz),
            ],
            foot_yaw=0.20 + dyaw,
            side=side,
        )
        dx, dz, kdy, kdz, dyaw = stance[f"rear_{tag}"]
        _build_flowing_leg(
            scene,
            f"leg_rear_{tag}",
            [
                (0.092, 0.298, -0.086),
                (0.158, 0.288, -0.094 + 0.2 * dz),
                (0.226 + 0.5 * dx, 0.264 + 0.4 * kdy, -0.184 + 0.5 * kdz),
                (0.262 + 0.8 * dx, 0.212 + kdy, -0.268 + kdz + 0.4 * dz),
                (0.246 + 0.9 * dx, 0.110 + 0.4 * kdy, -0.184 + 0.8 * dz),
                (0.230 + dx, 0.032, -0.098 + dz),
            ],
            foot_yaw=0.16 + dyaw,
            side=side,
        )


# ---------------------------------------------------------------------------
# Crossed ribbed woven-gold bullet bandoliers with brass casings & X-buckles
# ---------------------------------------------------------------------------

def _conform_belt_to_torso(
    pts: np.ndarray,
    out_normals: np.ndarray,
    torso_verts: np.ndarray,
    torso_pivot: np.ndarray,
    clearance: float = 0.014,
) -> np.ndarray:
    """Conform closed belt spline points cleanly outside the rounded dome torso (zero clipping)."""
    v_rel = torso_verts - torso_pivot[None, :]

    def needed(points: np.ndarray, radius: float) -> np.ndarray:
        out = np.full(len(points), np.nan)
        for i, (p, o) in enumerate(zip(points, out_normals)):
            sub = torso_verts[(v_rel @ o) > 0.05]
            if len(sub) == 0:
                continue
            diff = sub - p[None, :]
            proj = diff @ o
            perp = diff - proj[:, None] * o[None, :]
            near = np.sum(perp * perp, axis=1) < radius * radius
            if np.any(near):
                out[i] = float(np.max(proj[near]))
        return out

    raw = needed(pts, 0.056)
    shifts = np.where(np.isnan(raw), 0.0, raw + clearance)
    for _ in range(4):
        shifts = 0.25 * np.roll(shifts, 1) + 0.50 * shifts + 0.25 * np.roll(shifts, -1)
    moved = pts + shifts[:, None] * out_normals
    extra = needed(moved, 0.052)
    shifts += np.where(np.isnan(extra), 0.0, np.maximum(0.0, extra + 0.85 * clearance))
    for _ in range(3):
        shifts = 0.25 * np.roll(shifts, 1) + 0.50 * shifts + 0.25 * np.roll(shifts, -1)
    return pts + shifts[:, None] * out_normals


def _make_tactical_pouch(center: np.ndarray, width_dir: np.ndarray, out_n: np.ndarray, tangent: np.ndarray, label: str) -> List[MeshPart]:
    """One bold, chunky (~1.38x thicker/wider) quilted bright-yellow tactical utility pouch (`acst301`)."""
    mat = _frame(center, width_dir, out_n, tangent)
    body = rounded_box(0.158, 0.058, 0.072, bevel=0.013, segs=1, name=f"tactical_pouch_{label}", material="gear_yellow_plastic").transform(mat)
    flap = rounded_box(0.162, 0.020, 0.054, bevel=0.006, segs=1, name=f"pouch_flap_{label}", material="gear_yellow_plastic").translate(0.0, 0.024, -0.005).transform(mat)
    crease = rounded_box(0.152, 0.009, 0.010, bevel=0.002, segs=1, name=f"pouch_seam_{label}", material="gear_gold").translate(0.0, 0.018, 0.020).transform(mat)
    return [body, flap, crease]


def _build_bandoliers(scene: SceneBuilder, torso_verts: np.ndarray) -> None:
    """Two full closed 3D bold (~1.38x thicker/wider) diagonal crossed yellow tactical sashes
    draping cleanly ON TOP OF the shoulders (covering shoulder tops, zero arm clipping!) and
    forming an unmistakable X across BOTH front chest/lower torso AND back (`acst301`/`701`).
    """
    # Strap 0: draped wide & proud OVER TOP of left shoulder/pauldron (+X) -> front X-center
    # -> right lower waist (-X) -> right flank -> lower-right back -> rear X-center -> left shoulder.
    strap0_ctrl = [
        (+0.305, 0.858, -0.024),  # wide & proud ON TOP OF left shoulder/pauldron (zero arm clip!)
        (+0.278, 0.720, +0.154),  # draping boldly down front-left shoulder cap
        (+0.148, 0.606, +0.252),  # diagonal across left pectoral toward front X-center
        ( 0.000, 0.514, +0.274),  # FRONT X-JUNCTION CENTER on mid-chest
        (-0.144, 0.420, +0.204),  # diagonal down across lower-right ribs
        (-0.214, 0.368, +0.068),  # wrapping right lower waist above mid-hip socket
        (-0.216, 0.366, -0.098),  # right lower flank (cleanly inside flexing arms & above hip)
        (-0.148, 0.418, -0.254),  # lower-right back rising diagonally
        ( 0.000, 0.526, -0.332),  # REAR X-JUNCTION CENTER on mid-back
        (+0.160, 0.668, -0.278),  # diagonal up across upper-left back
        (+0.268, 0.778, -0.178),  # sweeping proudly up onto rear-left shoulder top
    ]
    strap1_ctrl = [(-x, y, z) for (x, y, z) in strap0_ctrl]
    pivot = np.array([0.0, 0.535, -0.028])

    front_cross_pts: List[np.ndarray] = []
    back_cross_pts: List[np.ndarray] = []

    for s_idx, ctrl in enumerate((strap0_ctrl, strap1_ctrl)):
        pts = catmull_rom_spline(np.asarray(ctrl, dtype=np.float64), samples_per_seg=6, closed=True)
        out_n = np.array([
            _normalize(
                p - pivot
                + np.array([0.0, 0.46 * max(0.0, min(1.0, (p[1] - 0.62) / 0.22)), 0.0])
                + np.array([0.0, -0.14 * max(0.0, min(1.0, (0.42 - p[1]) / 0.12)), 0.0])
            )
            for p in pts
        ])
        pts = _conform_belt_to_torso(pts, out_n, torso_verts, pivot, clearance=0.016)
        # Subtle radial weave separation on Strap 1 across the front/back X crossings (zero z-fighting!).
        if s_idx == 1:
            cross_w = np.exp(-((pts[:, 0] / 0.10) ** 2)) * (np.abs(pts[:, 2]) > 0.15)
            pts = pts + (0.0075 * cross_w)[:, None] * out_n

        n_pts = len(pts)
        scene.add(
            "gear_bandolier",
            _sculpted_ribbon_mesh(
                pts,
                out_n,
                np.full(n_pts, 0.096),
                np.full(n_pts, 0.019),
                closed=True,
                profile_segs=8,
                name=f"bandolier_strap_{s_idx}",
                material="gear_gold",
            ),
        )

        front_sub = pts[pts[:, 2] > 0.16]
        back_sub = pts[pts[:, 2] < -0.20]
        front_cross_pts.append(front_sub[int(np.argmin(np.abs(front_sub[:, 0])))])
        back_cross_pts.append(back_sub[int(np.argmin(np.abs(back_sub[:, 0])))])

        tangents = np.array([_normalize(pts[(i + 1) % n_pts] - pts[(i - 1) % n_pts]) for i in range(n_pts)])
        surf_n = np.array([_normalize(o - np.dot(o, t) * t) for o, t in zip(out_n, tangents)])

        seg_lens = np.linalg.norm(np.roll(pts, -1, axis=0) - pts, axis=1)
        cum = np.concatenate([[0.0], np.cumsum(seg_lens)])
        total = float(cum[-1])
        count = int(total / 0.092)
        for k in range(count):
            s = (k + 0.5) * total / count
            i = int(np.searchsorted(cum, s, side="right") - 1) % n_pts
            j = (i + 1) % n_pts
            f = (s - cum[i]) / max(1e-8, float(seg_lens[i]))
            p = (1.0 - f) * pts[i] + f * pts[j]
            # Leave clean room for the bold front AND rear yellow X-junction clasps.
            if abs(p[0]) < 0.092 and (p[2] > 0.16 or p[2] < -0.20):
                continue
            t = _normalize((1.0 - f) * tangents[i] + f * tangents[j])
            o = _normalize((1.0 - f) * surf_n[i] + f * surf_n[j])
            w = _normalize(np.cross(o, t))
            o = np.cross(t, w)
            if np.dot(w, np.array([1.0 if p[0] >= 0.0 else -1.0, 0.20, 0.0])) < 0.0:
                w = -w
                t = -t
            scene.add("gear_bandolier", _make_tactical_pouch(p + 0.028 * o, w, o, t, f"x{s_idx}_{k}"))

    # Bold ~1.32x yellow tactical X-junction clasps anchoring the crossed sashes on BOTH front & back.
    front_pos = 0.5 * (front_cross_pts[0] + front_cross_pts[1]) + np.array([0.0, 0.0, +0.026])
    back_pos = 0.5 * (back_cross_pts[0] + back_cross_pts[1]) + np.array([0.0, 0.0, -0.026])
    for side_tag, pos, rx_deg, z_sign in (("front", front_pos, 14.0, +1.0), ("back", back_pos, -14.0, -1.0)):
        frame = rounded_box(0.176, 0.176, 0.048, bevel=0.015, segs=2, name=f"buckle_frame_{side_tag}", material="gear_yellow_plastic").rotate_xyz(rx=rx_deg, rz=45.0, degrees=True).translate(*pos)
        recess = rounded_box(0.106, 0.106, 0.052, bevel=0.010, segs=1, name=f"buckle_recess_{side_tag}", material="gear_gold").rotate_xyz(rx=rx_deg, rz=45.0, degrees=True).translate(*pos)
        boss = superellipsoid(0.044, 0.044, 0.026, e1=0.74, e2=0.74, u_segs=14, v_segs=8, name=f"buckle_boss_{side_tag}", material="gear_yellow_plastic").rotate_xyz(rx=rx_deg, degrees=True).translate(pos[0], pos[1], pos[2] + z_sign * 0.024)
        scene.add("gear_bandolier", [frame, recess, boss])


# ---------------------------------------------------------------------------
# Signature hot-magenta Rambo cloth strip tied around the RIGHT (-X) arm
# ---------------------------------------------------------------------------

def _build_right_bicep_cloth(
    scene: SceneBuilder,
    center: np.ndarray,
    tangent: np.ndarray,
    right_arm_verts: np.ndarray,
) -> None:
    """Hot-magenta (`bomb_charge`) cloth band conformed snugly 360 deg around the RIGHT (-X) arm
    with a real cinched outer knot and two crisp fluttering V-angled tails (matching `acst` sprites).
    """
    cloth_mat = "bomb_charge"  # vivid hot magenta/fuchsia matching original ants.chd palette #98
    # Slight authentic Rambo wrap slant (higher on outer-rear, dipping slightly across front inner bicep).
    t = _normalize(tangent + np.array([0.14, 0.0, 0.18]))
    out_x = _normalize(np.array([-1.0, 0.0, -0.18]) - np.dot(np.array([-1.0, 0.0, -0.18]), t) * t)
    fwd = _normalize(np.cross(t, out_x))
    out_x = _normalize(np.cross(fwd, t))

    # Measure exact radial profile of the right arm around `center` so the cloth band hugs
    # skin-tight OUTSIDE the arm surface all 360 deg around (zero clipping and zero daylight gap!).
    n_ring = 26
    angs = np.linspace(0.0, 2.0 * math.pi, n_ring, endpoint=False)
    band_out = np.array([math.cos(a) * out_x + math.sin(a) * fwd for a in angs])
    rel = right_arm_verts - center[None, :]
    axial = rel @ t
    near_band = np.abs(axial) < 0.056
    sub = rel[near_band] if np.any(near_band) else rel
    radii = np.full(n_ring, 0.096, dtype=np.float64)
    for i, d in enumerate(band_out):
        proj = sub @ d
        perp = sub - proj[:, None] * d[None, :] - (sub @ t)[:, None] * t[None, :]
        in_wedge = (proj > 0.02) & (np.abs(perp @ np.cross(t, d)) < 0.034)
        if np.any(in_wedge):
            radii[i] = max(0.094, float(np.max(proj[in_wedge])))
    for _ in range(3):
        radii = 0.25 * np.roll(radii, 1) + 0.50 * radii + 0.25 * np.roll(radii, -1)
    band_r = radii + 0.006  # snug proud wrap hugging the beefier bicep cleanly with zero clipping

    # Main snug flat cloth wrap + subtle taut overlapping wrap-lap cinching cleanly into the knot.
    band_pts = center[None, :] + band_r[:, None] * band_out
    band_w = 0.031 - 0.008 * np.exp(-(((angs + math.pi) % (2.0 * math.pi) - math.pi) / 0.48) ** 2)
    scene.add(
        "gear_bow",
        _sculpted_ribbon_mesh(band_pts, band_out, band_w, np.full(n_ring, 0.0095), closed=True, profile_segs=8, name="bicep_cloth_band", material=cloth_mat),
    )
    lap_shift = (0.011 * np.sin(angs))[:, None] * t[None, :]
    lap_pts = center[None, :] + (band_r + 0.003)[:, None] * band_out + lap_shift
    scene.add(
        "gear_bow",
        _sculpted_ribbon_mesh(lap_pts, band_out, np.full(n_ring, 0.015), np.full(n_ring, 0.006), closed=True, profile_segs=8, name="bicep_cloth_lap", material=cloth_mat),
    )

    # Real tight tied overhand cloth knot anchored rearward on the outer-back bicep (-X, strongly -Z)
    # so the fluttering V-ribbons trail cleanly back toward Combat's back in `901 E` while still flaring
    # outward on viewer-left in `301 S` (`acst301` / `acst901`).
    knot_dir = _normalize(0.56 * out_x - 0.83 * fwd)
    knot_idx = int(np.argmax(band_out @ knot_dir))
    knot = band_pts[knot_idx] + 0.011 * knot_dir
    up_v = _normalize(-t)
    back_dir = _normalize(-fwd)
    knot_core = (
        superellipsoid(0.030, 0.026, 0.024, e1=0.68, e2=0.72, u_segs=14, v_segs=10, name="cloth_knot_core", material=cloth_mat)
        .rotate_xyz(rx=0.22, ry=0.85, rz=0.18)
        .translate(knot)
    )
    cinch = (
        torus_arc(0.023, 0.009, u_segs=16, v_segs=8, plane="xy", name="cloth_knot_cinch", material=cloth_mat)
        .rotate_xyz(rx=0.35, ry=1.42, rz=-0.25)
        .translate(knot - 0.004 * knot_dir)
    )
    scene.add("gear_bow", [knot_core, cinch])

    # Two crisp, bold, fluttering V-angled flat cloth tails streaming back toward his back & slightly out (-X).
    upper_ctrl = [
        knot - 0.005 * knot_dir,
        knot + 0.028 * out_x + 0.052 * back_dir + 0.044 * up_v,
        knot + 0.058 * out_x + 0.102 * back_dir + 0.084 * up_v,
        knot + 0.082 * out_x + 0.138 * back_dir + 0.108 * up_v,
    ]
    lower_ctrl = [
        knot - 0.005 * knot_dir,
        knot + 0.034 * out_x + 0.058 * back_dir - 0.006 * up_v,
        knot + 0.070 * out_x + 0.118 * back_dir - 0.016 * up_v,
        knot + 0.096 * out_x + 0.158 * back_dir - 0.022 * up_v,
    ]
    for tag, ctrl, widths, face_vec in (
        ("upper_tail", upper_ctrl, [0.013, 0.028, 0.032, 0.009], _normalize(0.82 * out_x + 0.44 * fwd + 0.36 * up_v)),
        ("lower_tail", lower_ctrl, [0.013, 0.027, 0.031, 0.009], _normalize(0.76 * out_x + 0.36 * fwd + 0.54 * up_v)),
    ):
        pts = catmull_rom_spline(ctrl, samples_per_seg=4, closed=False)
        n_t = len(pts)
        w_arr = np.interp(np.linspace(0.0, 1.0, n_t), np.linspace(0.0, 1.0, len(widths)), widths)
        th_arr = np.interp(np.linspace(0.0, 1.0, n_t), [0.0, 0.75, 1.0], [0.010, 0.009, 0.005])
        scene.add(
            "gear_bow",
            _sculpted_ribbon_mesh(pts, np.repeat(face_vec[None, :], n_t, axis=0), w_arr, th_arr, closed=False, profile_segs=8, name=f"cloth_{tag}", material=cloth_mat),
        )


def _quat_shortest_arc(a: np.ndarray, b: np.ndarray) -> Tuple[float, float, float, float]:
    au, bu = _normalize(a), _normalize(b)
    ang = math.acos(max(-1.0, min(1.0, float(np.dot(au, bu)))))
    return quat_from_axis_angle(tuple(np.cross(au, bu)), ang)


def _quat_rot_mat(q: Tuple[float, float, float, float]) -> np.ndarray:
    x, y, z, w = q
    return np.array([
        [1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w), 2.0 * (x * z + y * w)],
        [2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w)],
        [2.0 * (x * z - y * w), 2.0 * (y * z + x * w), 1.0 - 2.0 * (x * x + y * y)],
    ], dtype=np.float64)


def _add_combat_idle_animation(scene: SceneBuilder) -> None:
    """Authentic `acst301` 7.82s energetic arm-bounce forward heave + right-arm flex with left hand on hip!"""
    q_id = (0.0, 0.0, 0.0, 1.0)
    sh_l = np.array(ARM_PTS_LEFT[0], dtype=np.float64)
    el_l = np.array(ARM_PTS_LEFT[2], dtype=np.float64)
    fi_l = np.array(ARM_PTS_LEFT[4], dtype=np.float64)
    sh_r = np.array([-ARM_PTS_LEFT[0][0], ARM_PTS_LEFT[0][1], ARM_PTS_LEFT[0][2]])
    el_r = np.array([-ARM_PTS_LEFT[2][0], ARM_PTS_LEFT[2][1], ARM_PTS_LEFT[2][2]])
    fi_r = np.array([-ARM_PTS_LEFT[4][0], ARM_PTS_LEFT[4][1], ARM_PTS_LEFT[4][2]])
    # Bold high-and-out right-arm-up victory flex (`+0.34 Y` shoulder lift!) + tight double-pump curl.
    q_sh_ext = _quat_shortest_arc(el_r - sh_r, np.array([-0.92, 0.34, 0.16]))
    r_sh = _quat_rot_mat(q_sh_ext)
    q_el_ext = _quat_shortest_arc(fi_r - el_r, r_sh.T @ np.array([-0.94, 0.24, 0.14]))
    z_loc = tuple(r_sh.T @ np.array([0.0, 0.0, 1.0]))
    q_el_mid = quat_mul(quat_from_axis_angle(z_loc, -1.04), q_el_ext)
    q_el_flex = quat_mul(quat_from_axis_angle(z_loc, -1.98), q_el_ext)
    q_el_hi = quat_mul(quat_from_axis_angle(z_loc, -2.18), q_el_ext)

    # Cocky left hand-on-hip bodybuilder pose (`#306`): elbow flares out akimbo, fist plants on left hip!
    q_lsh_hip = _quat_shortest_arc(el_l - sh_l, np.array([0.64, -0.76, -0.08]))
    r_lsh = _quat_rot_mat(q_lsh_hip)
    el_l_hip = sh_l + r_lsh @ (el_l - sh_l)
    q_lel_hip = _quat_shortest_arc(fi_l - el_l, r_lsh.T @ (np.array([0.285, 0.365, 0.035]) - el_l_hip))
    q_lsh_mid = _quat_shortest_arc(el_l - sh_l, np.array([0.58, -0.80, 0.10]))
    r_lmid = _quat_rot_mat(q_lsh_mid)
    q_lel_mid = _quat_shortest_arc(fi_l - el_l, r_lmid.T @ (np.array([0.34, 0.28, 0.15]) - (sh_l + r_lmid @ (el_l - sh_l))))

    rows = []
    # 5 energetic rhythmic boxer arm-bounce & forward-leaning chest heaves across [0.00, 5.90] (1.18s each)
    for b in range(5):
        t0 = b * 1.18
        rows.append((t0, 0.000, 0.000, q_id, q_id, q_id, q_id, q_id, 0.00))
        rows.append((
            t0 + 0.26, -0.020, +0.072,
            quat_from_euler_xyz(rx=-0.055),
            quat_from_euler_xyz(rx=-0.10, rz=0.14),
            quat_from_euler_xyz(rz=-0.12),
            quat_from_euler_xyz(rx=-0.10, rz=-0.14),
            quat_from_euler_xyz(rz=+0.12),
            +0.05,
        ))
        rows.append((
            t0 + 0.60, +0.056, +0.032,
            quat_from_euler_xyz(rx=-0.025),
            quat_from_euler_xyz(rx=-0.16, rz=0.23),
            quat_from_euler_xyz(rz=+0.10),
            quat_from_euler_xyz(rx=-0.16, rz=-0.23),
            quat_from_euler_xyz(rz=-0.10),
            -0.08,
        ))
        rows.append((
            t0 + 0.92, -0.010, +0.048,
            quat_from_euler_xyz(rx=-0.035),
            quat_from_euler_xyz(rx=-0.06, rz=0.08),
            quat_from_euler_xyz(rz=-0.06),
            quat_from_euler_xyz(rx=-0.06, rz=-0.08),
            quat_from_euler_xyz(rz=+0.06),
            +0.03,
        ))
    # Emote: plant left hand cockily on hip, whip right arm high up in double-pump flex, turn head to admire bicep!
    rows.extend([
        (5.90, 0.000, 0.000, q_id, q_id, q_id, q_id, q_id, 0.00),
        (6.06, +0.038, +0.056, quat_from_euler_xyz(rx=-0.04, ry=-0.26, rz=0.04), q_lsh_mid, q_lel_mid, q_sh_ext, q_el_ext, -0.05),
        (6.16, +0.054, +0.064, quat_from_euler_xyz(rx=-0.05, ry=-0.42, rz=0.08), q_lsh_hip, q_lel_hip, q_sh_ext, q_el_mid, -0.07),
        (6.28, +0.066, +0.070, quat_from_euler_xyz(rx=-0.06, ry=-0.52, rz=0.10), q_lsh_hip, q_lel_hip, q_sh_ext, q_el_flex, -0.09),
        (6.48, +0.074, +0.082, quat_from_euler_xyz(rx=-0.07, ry=-0.56, rz=0.11), q_lsh_hip, q_lel_hip, q_sh_ext, q_el_hi, -0.11),
        (6.70, +0.060, +0.064, quat_from_euler_xyz(rx=-0.05, ry=-0.50, rz=0.09), q_lsh_hip, q_lel_hip, q_sh_ext, q_el_flex, -0.08),
        (6.92, +0.074, +0.082, quat_from_euler_xyz(rx=-0.07, ry=-0.56, rz=0.11), q_lsh_hip, q_lel_hip, q_sh_ext, q_el_hi, -0.11),
        (7.16, +0.058, +0.058, quat_from_euler_xyz(rx=-0.05, ry=-0.46, rz=0.08), q_lsh_hip, q_lel_hip, q_sh_ext, q_el_flex, -0.07),
        (7.34, +0.028, +0.032, quat_from_euler_xyz(rx=-0.02, ry=-0.20, rz=0.03), q_lsh_mid, q_lel_mid, q_sh_ext, q_el_mid, -0.04),
        (7.54, +0.008, +0.012, q_id, q_id, q_id, quat_from_euler_xyz(rz=-0.22), q_id, -0.02),
        (7.82, 0.000, 0.000, q_id, q_id, q_id, q_id, q_id, 0.00),
    ])
    times = [round(r[0], 4) for r in rows]
    bx, by, bz = scene.get_node_transform("upper_body").translation
    ub_r0 = scene.get_node_transform("upper_body").rotation
    hd_r0 = scene.get_node_transform("head_group").rotation
    scene.add_animation("Idle", [
        AnimationChannel("upper_body", "translation", list(times), [(bx, by + r[1], bz) for r in rows]),
        AnimationChannel("upper_body", "rotation", list(times), [quat_mul(ub_r0, quat_from_euler_xyz(rx=r[2])) for r in rows]),
        AnimationChannel("head_group", "rotation", list(times), [quat_mul(hd_r0, r[3]) for r in rows]),
        AnimationChannel("left_arm_group", "rotation", list(times), [r[4] for r in rows]),
        AnimationChannel("left_forearm_group", "rotation", list(times), [r[5] for r in rows]),
        AnimationChannel("right_arm_group", "rotation", list(times), [r[6] for r in rows]),
        AnimationChannel("right_forearm_group", "rotation", list(times), [r[7] for r in rows]),
        AnimationChannel("gaster_group", "rotation", list(times), [quat_from_euler_xyz(rx=r[8]) for r in rows]),
    ], replace=True)


def _add_combat_attack_animation(scene: SceneBuilder) -> None:
    """Authentic `acat` 0.54s (6-frame [80,100,60,80,100,120]ms) cross-chest windup into wide haymaker spread."""
    q_id = (0.0, 0.0, 0.0, 1.0)
    sh_l = np.array(ARM_PTS_LEFT[0], dtype=np.float64)
    el_l = np.array(ARM_PTS_LEFT[2], dtype=np.float64)
    fi_l = np.array(ARM_PTS_LEFT[4], dtype=np.float64)
    sh_r = np.array([-ARM_PTS_LEFT[0][0], ARM_PTS_LEFT[0][1], ARM_PTS_LEFT[0][2]])
    el_r = np.array([-ARM_PTS_LEFT[2][0], ARM_PTS_LEFT[2][1], ARM_PTS_LEFT[2][2]])
    fi_r = np.array([-ARM_PTS_LEFT[4][0], ARM_PTS_LEFT[4][1], ARM_PTS_LEFT[4][2]])

    def _aim_arm(
        sh: np.ndarray,
        el: np.ndarray,
        fi: np.ndarray,
        u_dir: Tuple[float, float, float],
        f_dir: Tuple[float, float, float],
    ) -> Tuple[Tuple[float, float, float, float], Tuple[float, float, float, float]]:
        q_sh = _quat_shortest_arc(el - sh, np.asarray(u_dir, dtype=np.float64))
        r_sh = _quat_rot_mat(q_sh)
        q_el = _quat_shortest_arc(fi - el, r_sh.T @ np.asarray(f_dir, dtype=np.float64))
        return q_sh, q_el

    def _align_quats(qs: List[Tuple[float, float, float, float]]) -> List[Tuple[float, float, float, float]]:
        out = [qs[0]]
        for q in qs[1:]:
            if sum(a * b for a, b in zip(out[-1], q)) < 0.0:
                q = (-q[0], -q[1], -q[2], -q[3])
            out.append(q)
        return out

    # Cross-chest coiled windup poses (L slightly higher than R so massive gauntlets never collide!).
    lsh_f1, lel_f1 = _aim_arm(sh_l, el_l, fi_l, (0.20, -0.52, 0.83), (-0.82, 0.22, 0.53))
    rsh_f1, rel_f1 = _aim_arm(sh_r, el_r, fi_r, (-0.20, -0.64, 0.74), (0.82, -0.16, 0.55))
    lsh_f2, lel_f2 = _aim_arm(sh_l, el_l, fi_l, (0.12, -0.54, 0.83), (-0.88, 0.24, 0.41))
    rsh_f2, rel_f2 = _aim_arm(sh_r, el_r, fi_r, (-0.16, -0.62, 0.77), (0.86, -0.16, 0.48))
    # Explosive double-arm haymaker fling wide outward & high (`acat301`/`acat901` f3-f4) + follow-through (f5).
    lsh_f3, lel_f3 = _aim_arm(sh_l, el_l, fi_l, (0.88, 0.34, 0.33), (0.92, 0.28, 0.26))
    rsh_f3, rel_f3 = _aim_arm(sh_r, el_r, fi_r, (-0.88, 0.38, 0.28), (-0.92, 0.32, 0.22))
    lsh_f4, lel_f4 = _aim_arm(sh_l, el_l, fi_l, (0.86, 0.46, -0.22), (0.82, 0.44, -0.37))
    rsh_f4, rel_f4 = _aim_arm(sh_r, el_r, fi_r, (-0.86, 0.48, -0.22), (-0.82, 0.46, -0.37))
    lsh_f5, lel_f5 = _aim_arm(sh_l, el_l, fi_l, (0.92, 0.06, 0.39), (0.84, -0.16, 0.52))
    rsh_f5, rel_f5 = _aim_arm(sh_r, el_r, fi_r, (-0.92, 0.08, 0.39), (-0.84, -0.14, 0.52))

    # (t, dy, dz, ub_rx, head_q, lsh_q, lel_q, rsh_q, rel_q, gaster_rx)
    rows = [
        (0.00, 0.000, 0.000, 0.00, q_id, q_id, q_id, q_id, q_id, 0.00),
        (0.08, -0.028, 0.025, 0.30, quat_from_euler_xyz(rx=-0.16, ry=-0.06), lsh_f1, lel_f1, rsh_f1, rel_f1, -0.12),
        (0.18, -0.038, 0.032, 0.36, quat_from_euler_xyz(rx=-0.22, ry=0.06), lsh_f2, lel_f2, rsh_f2, rel_f2, -0.16),
        (0.24, 0.028, 0.090, 0.04, quat_from_euler_xyz(rx=-0.14, ry=-0.04), lsh_f3, lel_f3, rsh_f3, rel_f3, 0.20),
        (0.32, 0.038, 0.078, -0.06, quat_from_euler_xyz(rx=-0.20), lsh_f4, lel_f4, rsh_f4, rel_f4, 0.24),
        (0.42, 0.012, 0.036, 0.05, quat_from_euler_xyz(rx=-0.06), lsh_f5, lel_f5, rsh_f5, rel_f5, 0.10),
        (0.54, 0.000, 0.000, 0.00, q_id, q_id, q_id, q_id, q_id, 0.00),
    ]
    times = [round(r[0], 4) for r in rows]
    bx, by, bz = scene.get_node_transform("upper_body").translation
    ub_r0 = scene.get_node_transform("upper_body").rotation
    hd_r0 = scene.get_node_transform("head_group").rotation
    scene.add_animation("Attack", [
        AnimationChannel("upper_body", "translation", list(times), [(bx, by + r[1], bz + r[2]) for r in rows]),
        AnimationChannel("upper_body", "rotation", list(times), _align_quats([quat_mul(ub_r0, quat_from_euler_xyz(rx=r[3])) for r in rows])),
        AnimationChannel("head_group", "rotation", list(times), _align_quats([quat_mul(hd_r0, r[4]) for r in rows])),
        AnimationChannel("left_arm_group", "rotation", list(times), _align_quats([r[5] for r in rows])),
        AnimationChannel("left_forearm_group", "rotation", list(times), _align_quats([r[6] for r in rows])),
        AnimationChannel("right_arm_group", "rotation", list(times), _align_quats([r[7] for r in rows])),
        AnimationChannel("right_forearm_group", "rotation", list(times), _align_quats([r[8] for r in rows])),
        AnimationChannel("gaster_group", "rotation", list(times), _align_quats([quat_from_euler_xyz(rx=r[9]) for r in rows])),
    ], replace=True)


def build_combat(blink: float = 0.0) -> SceneBuilder:
    """Build the low, wide, rounded hunched armored Combat Ant (`blink` in [0, 1])."""
    scene = SceneBuilder(name="ant_combat")
    torso_verts = _build_torso(scene)
    _build_head(scene, blink)
    cloth_ctr, cloth_tan, right_arm_verts, shoulder_cap_verts = _build_arms(scene)
    _build_legs(scene)
    _build_bandoliers(scene, np.vstack([torso_verts, shoulder_cap_verts]))
    _build_right_bicep_cloth(scene, cloth_ctr, cloth_tan, right_arm_verts)

    # Assemble clean hierarchical rig keeping `legs` rooted solidly on Y = 0.0!
    sh_l = ARM_PTS_LEFT[0]
    el_l = ARM_PTS_LEFT[2]
    sh_r = (-ARM_PTS_LEFT[0][0], ARM_PTS_LEFT[0][1], ARM_PTS_LEFT[0][2])
    el_r = (-ARM_PTS_LEFT[2][0], ARM_PTS_LEFT[2][1], ARM_PTS_LEFT[2][2])

    scene.set_node_transform("upper_body", translation=(0.0, 0.460, -0.025))
    scene.attach_world_node("body", "upper_body")
    scene.attach_world_node("gear_bandolier", "upper_body")

    scene.set_node_transform("gaster_group", translation=(0.0, 0.255, -0.155))
    scene.attach_world_node("gaster_group", "upper_body")
    scene.attach_world_node("gaster", "gaster_group")

    scene.set_node_transform("head_group", translation=(0.0, 0.812, 0.116))
    scene.attach_world_node("head_group", "upper_body")
    for hc_name in ("head", "eyes", "eyelid_left", "eyelid_right", "antennae"):
        scene.attach_world_node(hc_name, "head_group")

    scene.set_node_transform("left_arm_group", translation=sh_l)
    scene.attach_world_node("left_arm_group", "upper_body")
    scene.attach_world_node("left_upper_arm", "left_arm_group")

    scene.set_node_transform("left_forearm_group", translation=el_l)
    scene.attach_world_node("left_forearm_group", "left_arm_group")
    scene.attach_world_node("left_forearm", "left_forearm_group")

    scene.set_node_transform("right_arm_group", translation=sh_r)
    scene.attach_world_node("right_arm_group", "upper_body")
    scene.attach_world_node("right_upper_arm", "right_arm_group")
    scene.attach_world_node("gear_bow", "right_arm_group")

    scene.set_node_transform("right_forearm_group", translation=el_r)
    scene.attach_world_node("right_forearm_group", "right_arm_group")
    scene.attach_world_node("right_forearm", "right_forearm_group")

    # Subtle tough-guy Rambo swagger attitude + 16 deg chin-up glare from lowered linebacker stance!
    ub_xf = scene.get_node_transform("upper_body")
    scene.set_node_transform("upper_body", translation=ub_xf.translation, rotation=quat_from_euler_xyz(ry=0.042, rz=-0.024))
    hd_xf = scene.get_node_transform("head_group")
    scene.set_node_transform("head_group", translation=hd_xf.translation, rotation=quat_from_euler_xyz(rx=-math.radians(16.0), ry=-0.058, rz=0.054))

    scene.ground_to_y(0.0)
    _add_combat_idle_animation(scene)
    _add_combat_attack_animation(scene)
    # Uniformly scale Combat Ant down to 0.80x (geometry, node pivots, and Idle/Attack heave translations)
    # for authentic proportions alongside Worker while preserving every sculpt detail and animation.
    scene.scale_all(0.80)
    scene.ground_to_y(0.0)
    # Sync all 4 individual grounded legs to Combat's final 0.80x Idle & Attack clips!
    sync_leg_flex_to_clip(scene, "Idle")
    sync_leg_flex_to_clip(scene, "Attack", swivel=0.0)
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical module entrypoint for `build_all.py`."""
    return build_combat(blink=blink)


__all__ = ["build_ant", "build_combat"]
