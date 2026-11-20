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
    from .base_ant import EYELID_CLOSED_ANGLE
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
    from base_ant import EYELID_CLOSED_ANGLE  # type: ignore
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

# Proud upright boxy-rounded commando head perched high ABOVE the bigger muscular back on a real neck.
HEAD_CENTER: Vec3 = (0.0, 1.024, 0.068)
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
    # Elevated sturdy muscular dark neck rising clearly & proudly ABOVE the bigger back and bandoliers!
    neck = superellipsoid(0.146, 0.112, 0.136, e1=0.70, e2=0.82, u_segs=18, v_segs=12, name="neck_collar", material="chitin_dark").rotate_xyz(rx=0.08).translate(0.0, 0.838, 0.026)
    neck_ring = torus_arc(0.144, 0.013, u_segs=20, v_segs=8, plane="xz", name="neck_band", material="chitin_dark").scale(1.02, 1.0, 0.92).rotate_xyz(rx=0.08).translate(0.0, 0.850, 0.028)
    scene.add("body", [pelvis, neck, neck_ring])

    # Smooth curved scale-plated armored pauldrons: upper cap on torso, outer deltoid cap on arm roots!
    pauldron_specs = (
        ((0.308, 0.768, -0.022), (0.178, 0.072, 0.168), -0.44),
        ((0.402, 0.656, -0.004), (0.146, 0.068, 0.148), -0.82),
    )
    plate_parts: List[MeshPart] = []

    def _add_shoulder_pair(part: MeshPart, on_arm: bool) -> None:
        if on_arm:
            scene.add("arm_left", part)
            scene.add("right_upper_arm", part.mirror_x(f"{part.name}_right"))
        else:
            scene.add_mirrored("body", part)

    for idx, (ctr, (rx, ry, rz), roll) in enumerate(pauldron_specs):
        e1, e2 = 0.74, 0.80
        on_arm = idx == 1
        plate = superellipsoid(rx, ry, rz, e1=e1, e2=e2, u_segs=20, v_segs=12, name=f"pauldron_{idx}", material="chitin_team").rotate_xyz(rz=roll).translate(*ctr)
        plate_parts.append(plate)
        rot = np.array([[math.cos(roll), -math.sin(roll), 0.0], [math.sin(roll), math.cos(roll), 0.0], [0.0, 0.0, 1.0]])
        for r_i, (lx, lz) in enumerate(((0.56 * rx, 0.0), (0.34 * rx, 0.56 * rz), (0.34 * rx, -0.56 * rz))):
            local = np.array([lx, _super_top(rx, ry, rz, e1, e2, lx, lz) - 0.002, lz])
            normal = rot @ np.array([0.22 * lx / rx, 1.0, 0.22 * lz / rz])
            _add_shoulder_pair(_rivet(rot @ local + np.asarray(ctr), normal, f"pauldron_rivet_{idx}_{r_i}", radius=0.012), on_arm)
        trim = (
            torus_arc(0.92 * rz, 0.008, u_segs=20, v_segs=6, plane="xz", name=f"pauldron_seam_{idx}", material="chitin_dark")
            .scale(rx / rz, 1.0, 1.0)
            .translate(0.0, -0.36 * ry, 0.0)
            .rotate_xyz(rz=roll)
            .translate(*ctr)
        )
        _add_shoulder_pair(trim, on_arm)
        _add_shoulder_pair(plate, on_arm)

    # Row of 3 smooth overlapping curved armor scales riding organically on the outer deltoid arms.
    for s_idx, (z_off, yaw_z) in enumerate(((-0.072, -0.28), (0.0, 0.0), (0.072, 0.28))):
        sc_ctr = np.array([0.442, 0.622, 0.005 + z_off])
        scale_plate = (
            superellipsoid(0.056, 0.042, 0.054, e1=0.72, e2=0.78, u_segs=14, v_segs=10, name=f"pauldron_scale_{s_idx}", material="chitin_team")
            .rotate_xyz(ry=yaw_z, rz=-1.04)
            .translate(sc_ctr)
        )
        scale_rim = (
            torus_arc(0.052, 0.006, u_segs=14, v_segs=6, plane="xz", name=f"scale_rim_{s_idx}", material="chitin_dark")
            .translate(0.0, -0.016, 0.0)
            .rotate_xyz(ry=yaw_z, rz=-1.04)
            .translate(sc_ctr)
        )
        riv_n = _normalize(np.array([0.92, 0.34, z_off * 2.4]))
        _add_shoulder_pair(scale_plate, True)
        _add_shoulder_pair(scale_rim, True)
        _add_shoulder_pair(_rivet(sc_ctr + 0.035 * riv_n, riv_n, f"scale_rivet_{s_idx}", radius=0.010), True)

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

    shell = [core, dorsal, chest, left_pec, right_pec, pelvis] + abs_parts + plate_parts + [p.mirror_x() for p in plate_parts]
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


def _build_eyelid_shell(name: str, yaw_y: float, r_outer: float, thickness: float) -> MeshPart:
    """Spherical upper eyelid shell in eye-local space; inner radius clears eye at ALL blink frames."""
    lid = hemisphere_shell(r_outer, r_outer, r_outer, thickness=thickness, start_phi=-0.10, u_segs=26, v_segs=12, name=name, material="chitin_team")
    return lid.rotate_xyz(rx=-0.94, ry=yaw_y)


def _sculpt_head(scene: SceneBuilder, blink: float) -> None:
    hx, hy, hz = HEAD_CENTER
    # Compact, strong, softly-rounded boxy commando head (`e1=0.72, e2=0.78`, full rounded jaw!).
    cranium = (
        superellipsoid(0.226, 0.182, 0.186, e1=0.72, e2=0.78, u_segs=32, v_segs=24, name="cranium", material="chitin_team")
        .deform(_commando_head_deform)
        .translate(hx, hy, hz)
    )
    scene.add("head", cranium)

    # Bold wide V-angled armored brow ridge overhanging both fierce eyes.
    brow_pts = [
        (0.015, hy + 0.036, hz + 0.194),
        (0.074, hy + 0.056, hz + 0.192),
        (0.140, hy + 0.072, hz + 0.166),
        (0.194, hy + 0.078, hz + 0.116),
        (0.216, hy + 0.070, hz + 0.052),
    ]
    brow_rads = [(0.023, 0.020), (0.033, 0.025), (0.035, 0.026), (0.030, 0.022), (0.020, 0.016)]
    brow = curved_tube(brow_pts, brow_rads, samples_per_seg=4, radial_segs=12, cap_rings=3, name="brow_ledge", material="chitin_team")
    scene.add_mirrored("head", brow)

    # Crisp flush steel armor rivets along the arched forehead V-plate seam.
    for r_i, (rx, ry, rz) in enumerate(((0.044, 0.084, 0.172), (0.098, 0.112, 0.146), (0.150, 0.128, 0.104))):
        pos = np.array([hx + rx, hy + ry, hz + rz])
        nrm = _normalize(np.array([rx * 0.85, ry + 0.14, rz]))
        scene.add_mirrored("head", _rivet(pos, nrm, f"crown_rivet_{r_i}", radius=0.0068))

    # Expressive fierce 3-part eyes tucked right under the V-brow: trimmed sclera (~10% tighter
    # per #148) with huge rich olive-green irises (~72%) and glossy obsidian pupils (~47%).
    ex, ey, ez = EYE_OFFSET
    eye = np.array([ex, hy + ey, hz + ez])
    scene.add_mirrored(
        "eyes",
        torus_arc(0.080, 0.010, u_segs=26, v_segs=8, plane="xy", name="orbital_rim", material="chitin_dark")
        .scale(1.02, 0.89, 0.86)
        .rotate_xyz(rx=-0.08, ry=0.27)
        .translate(eye + np.array([0.002, -0.002, 0.004])),
    )
    scene.add_mirrored(
        "eyes",
        ellipsoid(0.082, 0.071, 0.046, u_segs=20, v_segs=12, name="orbital_socket", material="chitin_dark")
        .rotate_xyz(rx=-0.08, ry=0.27)
        .translate(eye + np.array([0.0, 0.0, -0.016])),
    )
    sclera = (
        superellipsoid(0.077, 0.067, 0.064, e1=0.94, e2=0.96, u_segs=24, v_segs=16, name="sclera", material="eye_white")
        .rotate_xyz(rx=-0.08, ry=0.27)
        .translate(eye)
    )
    # Curved ellipsoid iris & pupil seated smoothly into the sclera dome (r_max stays <= 0.078).
    iris = (
        ellipsoid(0.053, 0.051, 0.024, u_segs=22, v_segs=12, name="iris", material="eye_iris")
        .translate(-0.003, -0.002, 0.043)
        .rotate_xyz(rx=-0.06, ry=0.18)
        .translate(eye)
    )
    pupil = (
        ellipsoid(0.034, 0.033, 0.016, u_segs=18, v_segs=10, name="pupil", material="eye_pupil")
        .translate(-0.004, -0.002, 0.053)
        .rotate_xyz(rx=-0.06, ry=0.16)
        .translate(eye)
    )
    scene.add_mirrored("eyes", sclera)
    scene.add_mirrored("eyes", iris)
    scene.add_mirrored("eyes", pupil)

    # Exact radial eyelid clearance: spherical inner surface strictly outside max eye radius + 0.0075
    # so zero sclera/iris/pupil pixel ever phases through mid-blink at any blink angle in [0, 1].
    eye_pts = np.vstack([sclera.positions, iris.positions, pupil.positions]).astype(np.float64)
    r_eye_max = float(np.max(np.linalg.norm(eye_pts - eye[None, :], axis=1)))
    lid_thick = 0.0065
    lid_outer_r = r_eye_max + 0.0075 + lid_thick
    rest = quat_from_axis_angle((1.0, 0.0, 0.0), max(0.0, min(1.0, float(blink))) * EYELID_CLOSED_ANGLE)
    for node, sign in (("eyelid_left", 1.0), ("eyelid_right", -1.0)):
        scene.add(node, _build_eyelid_shell(f"{node}_shell", yaw_y=0.26 * sign, r_outer=lid_outer_r, thickness=lid_thick))
        scene.set_node_transform(node, translation=(sign * eye[0], eye[1], eye[2]), rotation=rest)
    scene.add_blink_animation(("eyelid_left", "eyelid_right"), closed_angle=EYELID_CLOSED_ANGLE, name="Blink")

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

    # Short sturdy kinked 2-segment dark antennae perched at the upper-outer forehead temples.
    base = (0.114, hy + 0.142, hz + 0.058)
    elbow = (0.154, hy + 0.264, hz + 0.062)
    scene.add_mirrored("antennae", swept_tube([base, (0.128, hy + 0.208, hz + 0.058), elbow], [0.022, 0.018, 0.020], radial_segs=10, smooth_subdiv=2, cap_rings=2, name="antenna_scape", material="chitin_dark"))
    scene.add_mirrored("antennae", curved_tube([elbow, (0.182, hy + 0.288, hz + 0.118), (0.198, hy + 0.256, hz + 0.168)], [0.018, 0.023, 0.029], samples_per_seg=4, radial_segs=10, cap_rings=3, name="antenna_club", material="chitin_dark"))
    scene.add_mirrored("antennae", ellipsoid(0.036, 0.019, 0.036, u_segs=12, v_segs=8, name="antenna_socket", material="chitin_dark").rotate_xyz(rx=0.20, rz=-0.30).translate(*base))


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

def _build_arms(scene: SceneBuilder) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Build articulated muscular ape arms (left whole; right split at elbow for bicep flex!).

    Returns `(right_bicep_center, right_bicep_tangent, right_arm_verts)` for the `-X` cloth wrap.
    """
    pts = [np.asarray(p, dtype=np.float64) for p in ARM_PTS_LEFT]
    sh, mid, el, wr, fi = pts
    pts_r = [np.array([-p[0], p[1], p[2]], dtype=np.float64) for p in pts]
    sh_r, mid_r, el_r, wr_r, fi_r = pts_r

    # Left (+X) continuous flowing ape arm; right (-X) split seamlessly at elbow for bicep flex!
    # Thick, meaty bodybuilder upper arms (0.130) clearly dominating the sleeker forearms (0.100).
    scene.add("arm_left", curved_tube(pts, [0.108, 0.132, 0.104, 0.100, 0.080], samples_per_seg=5, radial_segs=16, cap_rings=3, name="gorilla_arm_left", material="chitin_team"))
    scene.add("right_upper_arm", [
        curved_tube([sh_r, mid_r, el_r], [0.108, 0.132, 0.104], samples_per_seg=5, radial_segs=16, cap_rings=4, name="gorilla_upper_right", material="chitin_team"),
        sphere(0.102, u_segs=14, v_segs=10, name="elbow_joint_right", material="chitin_team").translate(el_r),
    ])
    scene.add("right_forearm", curved_tube([el_r, wr_r, fi_r], [0.104, 0.100, 0.080], samples_per_seg=5, radial_segs=16, cap_rings=4, name="gorilla_forearm_right", material="chitin_team"))

    def _add_arm_pair(part: MeshPart, *, right_node: str) -> None:
        scene.add("arm_left", part)
        scene.add(right_node, part.mirror_x(f"{part.name}_right"))

    # Sculpted bulging Rambo bicep peak (front-inner -> pops UP during flex) & horseshoe tricep belly.
    _add_arm_pair(superellipsoid(0.088, 0.124, 0.094, e1=0.78, e2=0.82, u_segs=14, v_segs=10, name="bicep_peak", material="chitin_team").rotate_xyz(rx=0.40, ry=-0.16, rz=0.38).translate(0.456, 0.514, 0.090), right_node="right_upper_arm")
    _add_arm_pair(superellipsoid(0.084, 0.116, 0.088, e1=0.80, e2=0.84, u_segs=12, v_segs=8, name="tricep_belly", material="chitin_team").rotate_xyz(rx=0.34, rz=0.40).translate(0.506, 0.520, -0.020), right_node="right_upper_arm")

    # Sculpted outer forearm gauntlet shell + recessed dark armor seam cuffs & elbow cop.
    gauntlet = (
        superellipsoid(0.102, 0.098, 0.118, e1=0.76, e2=0.80, u_segs=16, v_segs=12, name="forearm_gauntlet", material="chitin_team")
        .rotate_xyz(rx=0.58, ry=-0.38, rz=0.18)
        .translate(0.478, 0.304, 0.162)
    )
    _add_arm_pair(gauntlet, right_node="right_forearm")
    _add_arm_pair(_ring_on_segment(mid, el, 0.64, 0.116, 0.008, name="bracer_seam_0", material="chitin_dark"), right_node="right_upper_arm")
    _add_arm_pair(_ring_on_segment(el, wr, 0.36, 0.106, 0.0085, name="gauntlet_seam_0", material="chitin_dark"), right_node="right_forearm")
    _add_arm_pair(_ring_on_segment(el, wr, 0.72, 0.102, 0.0085, name="gauntlet_seam_1", material="chitin_dark"), right_node="right_forearm")
    _add_arm_pair(_ring_on_segment(el, wr, 0.96, 0.092, 0.014, name="gauntlet_cuff", material="chitin_dark"), right_node="right_forearm")

    elbow_cop = (
        superellipsoid(0.064, 0.058, 0.036, e1=0.76, e2=0.82, u_segs=14, v_segs=10, name="elbow_cop", material="chitin_team")
        .rotate_xyz(rx=0.42, ry=-0.32, rz=-0.26)
        .translate(el + np.array([0.028, 0.008, -0.042]))
    )
    _add_arm_pair(elbow_cop, right_node="right_forearm")

    mat, length = _axis_frame(el, wr)
    for k in range(6):
        ang = 2.0 * math.pi * k / 6.0 + 0.25
        local_n = np.array([math.cos(ang), 0.0, math.sin(ang)])
        local_p = local_n * 0.104 + np.array([0.0, 0.84 * length, 0.0])
        world_p = (mat @ np.append(local_p, 1.0))[:3]
        world_n = mat[:3, :3] @ local_n
        _add_arm_pair(_rivet(world_p, world_n, f"gauntlet_rivet_{k}", radius=0.011), right_node="right_forearm")

    # Big chunky dark clawed gorilla fists held wide and low (clear above Y = 0.0).
    _add_arm_pair(
        superellipsoid(0.084, 0.078, 0.088, e1=0.68, e2=0.74, u_segs=16, v_segs=12, name="fist_palm", material="chitin_dark")
        .rotate_xyz(rx=0.25, ry=-0.42, rz=0.12)
        .translate(fi),
        right_node="right_forearm",
    )
    claws = (
        ((0.036, 0.040, 0.052), (0.014, 0.024, 0.108), (-0.022, -0.008, 0.114)),
        ((0.040, -0.004, 0.058), (0.016, -0.020, 0.114), (-0.022, -0.046, 0.110)),
        ((0.034, -0.046, 0.048), (0.012, -0.062, 0.098), (-0.020, -0.074, 0.092)),
    )
    for c_idx, ctrl in enumerate(claws):
        c_pts = [fi + np.asarray(c) for c in ctrl]
        _add_arm_pair(curved_tube(c_pts, [0.026, 0.022, 0.010], samples_per_seg=3, radial_segs=10, cap_rings=2, name=f"fist_claw_{c_idx}", material="chitin_dark"), right_node="right_forearm")
    thumb = [fi + np.array(c) for c in ((-0.048, 0.040, 0.026), (-0.074, 0.026, 0.074), (-0.046, -0.004, 0.104))]
    _add_arm_pair(curved_tube(thumb, [0.026, 0.022, 0.010], samples_per_seg=3, radial_segs=10, cap_rings=2, name="fist_thumb", material="chitin_dark"), right_node="right_forearm")

    # Sample exact right (-X) lower-bicep center & vertices so hot-magenta cloth wraps cleanly
    # just below the bulging bicep mountain peak (letting the big bicep pop proudly above it!).
    sampled = catmull_rom_spline(ARM_PTS_LEFT, samples_per_seg=10)
    idx = 16
    p_l = sampled[idx]
    t_l = _normalize(sampled[idx + 1] - sampled[idx - 1])
    right_arm_verts = np.vstack([p.positions for p in scene.get_node("right_upper_arm") + scene.get_node("right_forearm")]).astype(np.float64)
    return np.array([-p_l[0], p_l[1], p_l[2]]), _normalize(np.array([-t_l[0], t_l[1], t_l[2]])), right_arm_verts


def _sturdy_foot(name: str) -> MeshPart:
    """Broad sturdy armored dark insect claw-foot resting flat on Y = 0.0."""
    prof = [(0.000, -0.042), (0.032, -0.030), (0.046, 0.004), (0.040, 0.046), (0.022, 0.080), (0.000, 0.096)]
    foot = lathe(prof, radial_segs=14, cap_bottom=False, cap_top=False, axis="z", name=name, material="chitin_dark").scale(1.12, 0.72, 1.0)
    foot = foot.deform(lambda p: np.column_stack([p[:, 0], np.maximum(p[:, 1], -0.018), p[:, 2]]))
    return foot.translate(0.0, -float(foot.bounds()[0][1]), 0.0)


def _build_flowing_leg(scene: SceneBuilder, label: str, ctrl: Sequence[Vec3], foot_yaw: float) -> None:
    """Short, tucked, 100% smooth twist-free dark muscular insect leg (zero knee clipping/glitches!)."""
    pts = [np.asarray(p, dtype=np.float64) for p in ctrl]
    leg = curved_tube(
        pts,
        [(0.052, 0.056), (0.054, 0.056), (0.044, 0.046), (0.036, 0.036)],
        samples_per_seg=6,
        radial_segs=16,
        cap_rings=3,
        name=f"leg_flow_{label}",
        material="chitin_dark",
    )
    foot = _sturdy_foot(f"tarsus_{label}").rotate_xyz(ry=foot_yaw).translate(pts[-1][0], 0.0, pts[-1][2] + 0.004)
    scene.add_mirrored("legs", leg)
    scene.add_mirrored("legs", foot)


def _build_legs(scene: SceneBuilder) -> None:
    """Short, compact crouched muscular dark legs tucked neatly underneath the hunched bulk."""
    _build_flowing_leg(
        scene,
        "mid",
        [(0.155, 0.304, 0.040), (0.254, 0.256, 0.096), (0.256, 0.142, 0.132), (0.238, 0.036, 0.146)],
        foot_yaw=0.22,
    )
    _build_flowing_leg(
        scene,
        "rear",
        [(0.150, 0.284, -0.095), (0.262, 0.248, -0.164), (0.264, 0.138, -0.202), (0.248, 0.036, -0.214)],
        foot_yaw=math.pi - 0.24,
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

    raw = needed(pts, 0.090)
    shifts = np.where(np.isnan(raw), 0.0, raw + clearance)
    for _ in range(3):
        shifts = 0.25 * np.roll(shifts, 1) + 0.50 * shifts + 0.25 * np.roll(shifts, -1)
    moved = pts + shifts[:, None] * out_normals
    extra = needed(moved, 0.080)
    shifts += np.where(np.isnan(extra), 0.0, np.maximum(0.0, extra + 0.85 * clearance))
    return pts + shifts[:, None] * out_normals


def _make_brass_bullet_pad(center: np.ndarray, width_dir: np.ndarray, out_n: np.ndarray, tangent: np.ndarray, label: str) -> List[MeshPart]:
    """One ribbed woven-gold ammo loop housing a crisp metallic-brass bullet cartridge."""
    mat = _frame(center, width_dir, out_n, tangent)
    loop_pad = rounded_box(0.084, 0.032, 0.042, bevel=0.010, segs=1, name=f"bandolier_pad_{label}", material="gear_gold").transform(mat)
    casing = cylinder(0.016, 0.016, 0.114, radial_segs=8, bevel=0.0, axis="x", name=f"bullet_case_{label}", material="gear_gold_metal").translate(-0.006, 0.006, 0.0).transform(mat)
    tip_prof = [(0.000, 0.048), (0.0155, 0.048), (0.000, 0.076)]
    tip = lathe(tip_prof, radial_segs=8, cap_bottom=False, cap_top=False, axis="x", name=f"bullet_tip_{label}", material="gear_brass_trim").translate(0.0, 0.006, 0.0).transform(mat)
    return [loop_pad, casing, tip]


def _build_bandoliers(scene: SceneBuilder, torso_verts: np.ndarray) -> None:
    """Crossed ribbed woven-gold bullet bandoliers over BOTH chest and back with brass X-buckles."""
    belt_ctrl = [
        (0.218, 0.835, -0.042),   # over fuller left shoulder beside the elevated dark muscular neck
        (0.228, 0.722, 0.155),    # upper-left chest
        (0.122, 0.578, 0.248),    # left pectoral
        (0.000, 0.474, 0.268),    # front chest X crossing
        (-0.144, 0.390, 0.224),   # lower-right abdomen
        (-0.244, 0.346, 0.100),   # right front hip
        (-0.262, 0.352, -0.080),  # right side hip
        (-0.194, 0.436, -0.268),  # lower-right back
        (0.000, 0.562, -0.332),   # back X crossing across bigger dorsal carapace
        (0.176, 0.726, -0.292),   # upper-left back
        (0.218, 0.808, -0.175),   # rear left shoulder slope
    ]
    pivot = np.array([0.0, 0.535, -0.032])
    crossings = {"front": np.array([0.0, 0.474, 0.294]), "back": np.array([0.0, 0.562, -0.360])}

    for belt_idx, sign in enumerate((1.0, -1.0)):
        raw = np.array([(sign * x, y, z) for x, y, z in belt_ctrl], dtype=np.float64)
        pts = catmull_rom_spline(raw, samples_per_seg=6, closed=True)
        out_n = np.array([_normalize(p - pivot + (np.array([0.0, 0.28, 0.0]) if p[1] > 0.74 else 0.0)) for p in pts])
        pts = _conform_belt_to_torso(pts, out_n, torso_verts, pivot, clearance=0.014)
        if belt_idx == 1:
            pts = pts + (0.030 * np.exp(-((pts[:, 0] / 0.11) ** 2)))[:, None] * out_n
            for tag, mask in (("front", pts[:, 2] > 0.15), ("back", pts[:, 2] < -0.15)):
                sub = pts[mask]
                hit = sub[int(np.argmin(np.abs(sub[:, 0])))]
                crossings[tag] = hit + np.array([0.0, 0.0, 0.020 if tag == "front" else -0.020])

        n_pts = len(pts)
        scene.add("gear_bandolier", _sculpted_ribbon_mesh(pts, out_n, np.full(n_pts, 0.072), np.full(n_pts, 0.012), closed=True, profile_segs=8, name=f"bandolier_strap_{belt_idx}", material="gear_dark"))

        tangents = np.array([_normalize(pts[(i + 1) % n_pts] - pts[(i - 1) % n_pts]) for i in range(n_pts)])
        surf_n = np.array([_normalize(o - np.dot(o, t) * t) for o, t in zip(out_n, tangents)])

        seg_lens = np.linalg.norm(np.roll(pts, -1, axis=0) - pts, axis=1)
        cum = np.concatenate([[0.0], np.cumsum(seg_lens)])
        total = float(cum[-1])
        count = int(total / 0.064)
        for k in range(count):
            s = (k + 0.5) * total / count
            i = int(np.searchsorted(cum, s, side="right") - 1) % n_pts
            j = (i + 1) % n_pts
            f = (s - cum[i]) / max(1e-8, float(seg_lens[i]))
            p = (1.0 - f) * pts[i] + f * pts[j]
            if abs(p[0]) < 0.085 and abs(p[2]) > 0.14:
                continue
            t = _normalize((1.0 - f) * tangents[i] + f * tangents[j])
            o = _normalize((1.0 - f) * surf_n[i] + f * surf_n[j])
            w = _normalize(np.cross(o, t))
            o = np.cross(t, w)
            if np.dot(w, np.array([sign, 0.25, 0.0])) < 0.0:
                w = -w
                t = -t
            scene.add("gear_bandolier", _make_brass_bullet_pad(p + 0.021 * o, w, o, t, f"{belt_idx}_{k}"))

    for tag, pos in crossings.items():
        tilt = 14.0 if tag == "front" else -16.0
        face = 1.0 if tag == "front" else -1.0
        frame = rounded_box(0.150, 0.150, 0.036, bevel=0.012, segs=2, name=f"buckle_frame_{tag}", material="gear_gold_metal").rotate_xyz(rx=tilt, rz=45.0, degrees=True).translate(*pos)
        recess = rounded_box(0.094, 0.094, 0.042, bevel=0.008, segs=1, name=f"buckle_recess_{tag}", material="gear_dark").rotate_xyz(rx=tilt, rz=45.0, degrees=True).translate(*pos)
        boss = superellipsoid(0.039, 0.039, 0.022, e1=0.74, e2=0.74, u_segs=14, v_segs=8, name=f"buckle_boss_{tag}", material="gear_brass_trim").rotate_xyz(rx=tilt, degrees=True).translate(pos[0], pos[1], pos[2] + 0.018 * face)
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
    radii = np.full(n_ring, 0.114, dtype=np.float64)
    for i, d in enumerate(band_out):
        proj = sub @ d
        perp = sub - proj[:, None] * d[None, :] - (sub @ t)[:, None] * t[None, :]
        in_wedge = (proj > 0.02) & (np.abs(perp @ np.cross(t, d)) < 0.038)
        if np.any(in_wedge):
            radii[i] = max(0.112, float(np.max(proj[in_wedge])))
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

    # Real tight tied overhand cloth knot anchored flush on the outer-rear side (-X, slightly -Z).
    knot_dir = _normalize(0.88 * out_x - 0.47 * fwd)
    knot_idx = int(np.argmax(band_out @ knot_dir))
    knot = band_pts[knot_idx] + 0.011 * knot_dir
    up_v = _normalize(-t)
    back_v = _normalize(0.72 * out_x - 0.69 * fwd)
    knot_core = (
        superellipsoid(0.030, 0.026, 0.024, e1=0.68, e2=0.72, u_segs=14, v_segs=10, name="cloth_knot_core", material=cloth_mat)
        .rotate_xyz(rx=0.22, ry=0.45, rz=0.18)
        .translate(knot)
    )
    cinch = (
        torus_arc(0.023, 0.009, u_segs=16, v_segs=8, plane="xy", name="cloth_knot_cinch", material=cloth_mat)
        .rotate_xyz(rx=0.35, ry=1.18, rz=-0.25)
        .translate(knot - 0.004 * knot_dir)
    )
    scene.add("gear_bow", [knot_core, cinch])

    # Two crisp, bold, fluttering V-angled flat cloth tails matching sprite_1800 / sprite_1813 silhouette.
    upper_ctrl = [
        knot - 0.005 * knot_dir,
        knot + 0.040 * out_x + 0.050 * up_v + 0.018 * back_v,
        knot + 0.078 * out_x + 0.098 * up_v + 0.032 * back_v,
        knot + 0.104 * out_x + 0.132 * up_v + 0.038 * back_v,
    ]
    lower_ctrl = [
        knot - 0.005 * knot_dir,
        knot + 0.048 * out_x - 0.004 * up_v + 0.036 * back_v,
        knot + 0.094 * out_x - 0.014 * up_v + 0.074 * back_v,
        knot + 0.122 * out_x - 0.018 * up_v + 0.100 * back_v,
    ]
    for tag, ctrl, widths, face_vec in (
        ("upper_tail", upper_ctrl, [0.013, 0.028, 0.032, 0.009], _normalize(0.28 * out_x + 0.90 * fwd + 0.34 * up_v)),
        ("lower_tail", lower_ctrl, [0.013, 0.027, 0.031, 0.009], _normalize(0.22 * out_x + 0.72 * fwd + 0.66 * up_v)),
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
    """Authentic `acst301` 7.82s heavy muscular heave + 900ms bodybuilder right-arm bicep flex!"""
    q_id = (0.0, 0.0, 0.0, 1.0)
    sh_r = np.array([-ARM_PTS_LEFT[0][0], ARM_PTS_LEFT[0][1], ARM_PTS_LEFT[0][2]])
    el_r = np.array([-ARM_PTS_LEFT[2][0], ARM_PTS_LEFT[2][1], ARM_PTS_LEFT[2][2]])
    fi_r = np.array([-ARM_PTS_LEFT[4][0], ARM_PTS_LEFT[4][1], ARM_PTS_LEFT[4][2]])
    q_sh_ext = _quat_shortest_arc(el_r - sh_r, np.array([-1.0, 0.06, 0.10]))
    r_sh = _quat_rot_mat(q_sh_ext)
    q_el_ext = _quat_shortest_arc(fi_r - el_r, r_sh.T @ np.array([-1.0, 0.02, 0.10]))
    z_loc = tuple(r_sh.T @ np.array([0.0, 0.0, 1.0]))
    q_el_mid = quat_mul(quat_from_axis_angle(z_loc, -0.92), q_el_ext)
    q_el_flex = quat_mul(quat_from_axis_angle(z_loc, -1.85), q_el_ext)
    q_el_hi = quat_mul(quat_from_axis_angle(z_loc, -1.96), q_el_ext)

    rows = []
    # 5 calm, weighty muscular breathing heaves across [0.00, 5.90] (1.18s each)
    for b in range(5):
        t0 = b * 1.18
        rows.append((t0, 0.000, 0.00, q_id, q_id, q_id, q_id, 0.00))
        rows.append((t0 + 0.56, 0.048, -0.036, quat_from_euler_xyz(rx=0.045), quat_from_euler_xyz(rx=-0.03, rz=0.11), quat_from_euler_xyz(rx=-0.03, rz=-0.11), quat_from_euler_xyz(rz=0.06), -0.06))
    # Emote: extend right (-X) arm sideways -> curl up -> hold 900ms bicep flex -> lower smoothly
    rows.extend([
        (5.90, 0.000, 0.000, q_id, q_id, q_id, q_id, 0.00),
        (6.08, 0.034, -0.030, quat_from_euler_xyz(rx=-0.02, ry=-0.14), quat_from_euler_xyz(rz=0.09), q_sh_ext, q_el_ext, -0.04),
        (6.17, 0.048, -0.042, quat_from_euler_xyz(rx=-0.04, ry=-0.20), quat_from_euler_xyz(rz=0.11), q_sh_ext, q_el_mid, -0.06),
        (6.26, 0.058, -0.052, quat_from_euler_xyz(rx=-0.05, ry=-0.24, rz=0.04), quat_from_euler_xyz(rz=0.14), q_sh_ext, q_el_flex, -0.08),
        (6.71, 0.064, -0.058, quat_from_euler_xyz(rx=-0.06, ry=-0.26, rz=0.05), quat_from_euler_xyz(rz=0.15), q_sh_ext, q_el_hi, -0.09),
        (7.16, 0.056, -0.050, quat_from_euler_xyz(rx=-0.05, ry=-0.22, rz=0.04), quat_from_euler_xyz(rz=0.13), q_sh_ext, q_el_flex, -0.07),
        (7.32, 0.030, -0.024, quat_from_euler_xyz(rx=-0.02, ry=-0.10), quat_from_euler_xyz(rz=0.07), q_sh_ext, q_el_mid, -0.04),
        (7.50, 0.010, -0.008, q_id, q_id, quat_from_euler_xyz(rz=-0.20), q_id, -0.02),
        (7.82, 0.000, 0.000, q_id, q_id, q_id, q_id, 0.00),
    ])
    times = [round(r[0], 4) for r in rows]
    bx, by, bz = scene.get_node_transform("upper_body").translation
    scene.add_animation("Idle", [
        AnimationChannel("upper_body", "translation", list(times), [(bx, by + r[1], bz) for r in rows]),
        AnimationChannel("upper_body", "rotation", list(times), [quat_from_euler_xyz(rx=r[2]) for r in rows]),
        AnimationChannel("head_group", "rotation", list(times), [r[3] for r in rows]),
        AnimationChannel("left_arm_group", "rotation", list(times), [r[4] for r in rows]),
        AnimationChannel("right_arm_group", "rotation", list(times), [r[5] for r in rows]),
        AnimationChannel("right_forearm_group", "rotation", list(times), [r[6] for r in rows]),
        AnimationChannel("gaster_group", "rotation", list(times), [quat_from_euler_xyz(rx=r[7]) for r in rows]),
    ], replace=True)


def build_combat(blink: float = 0.0) -> SceneBuilder:
    """Build the low, wide, rounded hunched armored Combat Ant (`blink` in [0, 1])."""
    scene = SceneBuilder(name="ant_combat")
    torso_verts = _build_torso(scene)
    _build_head(scene, blink)
    cloth_ctr, cloth_tan, right_arm_verts = _build_arms(scene)
    _build_legs(scene)
    _build_bandoliers(scene, torso_verts)
    _build_right_bicep_cloth(scene, cloth_ctr, cloth_tan, right_arm_verts)

    # Assemble clean hierarchical rig keeping `legs` rooted solidly on Y = 0.0!
    sh_l = ARM_PTS_LEFT[0]
    sh_r = (-ARM_PTS_LEFT[0][0], ARM_PTS_LEFT[0][1], ARM_PTS_LEFT[0][2])
    el_r = (-ARM_PTS_LEFT[2][0], ARM_PTS_LEFT[2][1], ARM_PTS_LEFT[2][2])

    scene.set_node_transform("upper_body", translation=(0.0, 0.460, -0.025))
    scene.attach_world_node("body", "upper_body")
    scene.attach_world_node("gear_bandolier", "upper_body")

    scene.set_node_transform("gaster_group", translation=(0.0, 0.255, -0.155))
    scene.attach_world_node("gaster_group", "upper_body")
    scene.attach_world_node("gaster", "gaster_group")

    scene.set_node_transform("head_group", translation=(0.0, 0.848, 0.030))
    scene.attach_world_node("head_group", "upper_body")
    for hc_name in ("head", "eyes", "eyelid_left", "eyelid_right", "antennae"):
        scene.attach_world_node(hc_name, "head_group")

    scene.set_node_transform("left_arm_group", translation=sh_l)
    scene.attach_world_node("left_arm_group", "upper_body")
    scene.attach_world_node("arm_left", "left_arm_group")

    scene.set_node_transform("right_arm_group", translation=sh_r)
    scene.attach_world_node("right_arm_group", "upper_body")
    scene.attach_world_node("right_upper_arm", "right_arm_group")
    scene.attach_world_node("gear_bow", "right_arm_group")

    scene.set_node_transform("right_forearm_group", translation=el_r)
    scene.attach_world_node("right_forearm_group", "right_arm_group")
    scene.attach_world_node("right_forearm", "right_forearm_group")

    scene.ground_to_y(0.0)
    _add_combat_idle_animation(scene)
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical module entrypoint for `build_all.py`."""
    return build_combat(blink=blink)


__all__ = ["build_ant", "build_combat"]
