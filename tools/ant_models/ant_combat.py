"""Sculpted high-detail 3D model for the Combat Ant: an armored insect commando.

Built directly with `mesh_core.py` (independent of `base_ant`) to capture the
broad brawler silhouette (~2.2-2.4x Worker width, ~1.24x Worker height) with
smooth high-resolution armored surfaces and real sculpted hardware detail:
- Smooth beveled armored head with crisp vertical cranial groove, brooding V
  chitin brow ridge, forehead plate rivets, deep dark orbital sockets, larger
  fierce 3-part eyes, serrated curved mandibles, and kinked dark antennae
- Pivoted `eyelid_left` / `eyelid_right` nodes with the shared glTF 2.0 "Blink"
  clip (matching `base_ant`'s exact convention)
- Overlapping curved scale-plated armored pauldrons with `gear_steel` rivets
  and dark seam trim, muscular contoured arm armor, beveled gauntlets with
  cuff studs, dark clawed fists, and sturdy armored dark legs grounded at Y=0.0
- Crossed ribbed woven-gold bullet bandoliers over chest AND back with brass
  cartridge casings, tips, and riveted front/back X-junction buckles
- Signature crimson cloth ribbon bow tied around the RIGHT bicep (-X side)

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
        recompute_smooth_normals,
        rounded_box,
        sphere,
        superellipsoid,
        swept_tube,
        torus_arc,
    )

Vec3 = Tuple[float, float, float]

HEAD_CENTER: Vec3 = (0.0, 0.990, 0.195)
HEAD_SCALE = 1.16
EYE_OFFSET: Vec3 = (0.088, -0.010, 0.168)  # left eye relative to head center before HEAD_SCALE

# Left (+X) arm joints; right arm (-X) mirrors them cleanly.
SHOULDER: Vec3 = (0.330, 0.870, -0.010)
BICEP_MID: Vec3 = (0.422, 0.735, -0.022)
ELBOW: Vec3 = (0.460, 0.585, -0.010)
WRIST: Vec3 = (0.426, 0.420, 0.162)
FIST: Vec3 = (0.406, 0.376, 0.232)


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
    """Return 4x4 frame mapping local +Y onto segment p0 -> p1 (origin at p0) and segment length."""
    a, b = np.asarray(p0, dtype=np.float64), np.asarray(p1, dtype=np.float64)
    y = _normalize(b - a)
    ref = np.array([0.0, 0.0, 1.0]) if abs(y[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
    x = _normalize(np.cross(y, ref))
    z = np.cross(x, y)
    return _frame(a, x, y, z), float(np.linalg.norm(b - a))


def _segment_lathe(
    p0: Sequence[float],
    p1: Sequence[float],
    radii_at: Sequence[Tuple[float, float]],
    *,
    name: str,
    material: str,
    segs: int = 20,
) -> MeshPart:
    mat, length = _axis_frame(p0, p1)
    prof = [(r, f * length) for r, f in radii_at]
    return lathe(prof, radial_segs=segs, cap_bottom=True, cap_top=True, axis="y", name=name, material=material).transform(mat)


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


def _rivet(pos: np.ndarray, normal: np.ndarray, name: str, radius: float = 0.013, material: str = "gear_steel") -> MeshPart:
    n = _normalize(normal)
    ref = np.array([1.0, 0.0, 0.0]) if abs(n[0]) < 0.9 else np.array([0.0, 0.0, 1.0])
    x = _normalize(np.cross(ref, n))
    z = np.cross(x, n)
    dome = ellipsoid(radius, 0.58 * radius, radius, u_segs=8, v_segs=5, name=name, material=material)
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
    profile_segs: int = 12,
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
    cu = np.sign(np.cos(angs)) * (np.abs(np.cos(angs)) ** 0.65)
    su = np.sign(np.sin(angs)) * (np.abs(np.sin(angs)) ** 0.65)

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
# Torso, overlapping scale-plated pauldrons, and stepped gaster
# ---------------------------------------------------------------------------

def _hunch_deform(pos: np.ndarray) -> np.ndarray:
    """Sculpt broad contoured trapezius shelves tapering cleanly toward the waist."""
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    upper = np.clip((y + 0.05) / 0.26, 0.0, 1.0)
    lower = np.clip(-y / 0.24, 0.0, 1.0)
    p[:, 0] = x * (1.0 + 0.09 * upper - 0.18 * (lower ** 1.4))
    crest = np.exp(-(x / 0.26) ** 2) * np.clip((-z + 0.05) / 0.30, 0.0, 1.0) * upper
    p[:, 1] = y + 0.034 * crest
    # Subtle dorsal spine groove down the upper back carapace.
    spine_groove = np.exp(-(x / 0.022) ** 2) * np.clip((-z) / 0.18, 0.0, 1.0)
    p[:, 2] += 0.010 * spine_groove
    return p


def _build_torso(scene: SceneBuilder) -> np.ndarray:
    """Build smooth contoured thorax armor, scale-plated riveted pauldrons, and gaster."""
    core = superellipsoid(0.228, 0.240, 0.194, e1=0.68, e2=0.72, u_segs=20, v_segs=14, name="thorax_core", material="chitin_dark").translate(0.0, 0.715, -0.030)
    dorsal = (
        superellipsoid(0.266, 0.216, 0.204, e1=0.58, e2=0.64, u_segs=24, v_segs=16, name="dorsal_carapace", material="chitin_team")
        .deform(_hunch_deform)
        .rotate_xyz(rx=0.12)
        .translate(0.0, 0.802, -0.068)
    )
    chest = superellipsoid(0.230, 0.188, 0.126, e1=0.54, e2=0.62, u_segs=20, v_segs=14, name="breastplate", material="chitin_team").rotate_xyz(rx=0.10).translate(0.0, 0.736, 0.094)
    pec_l = (
        superellipsoid(0.116, 0.102, 0.064, e1=0.56, e2=0.64, u_segs=16, v_segs=12, name="pectoral_plate", material="chitin_team")
        .rotate_xyz(rx=0.14, ry=0.28, rz=-0.05)
        .translate(0.112, 0.778, 0.172)
    )
    left_pec, right_pec = scene.add_mirrored("body", pec_l)
    scene.add("body", [core, dorsal, chest])

    # Interlocking smooth beveled abdominal armor plates pinching cleanly into the waist.
    abs_parts: List[MeshPart] = []
    for i, (y, rx, rz, z) in enumerate(((0.574, 0.162, 0.106, 0.090), (0.506, 0.134, 0.092, 0.068))):
        plate = superellipsoid(rx, 0.038, rz, e1=0.54, e2=0.62, u_segs=18, v_segs=10, name=f"ab_plate_{i}", material="chitin_team").rotate_xyz(rx=0.12).translate(0.0, y, z)
        seam = torus_arc(rx * 0.94, 0.007, u_segs=18, v_segs=6, plane="xz", name=f"ab_seam_{i}", material="chitin_dark").scale(1.0, 1.0, rz / rx).rotate_xyz(rx=0.12).translate(0.0, y - 0.024, z)
        abs_parts.append(plate)
        scene.add("body", seam)
    scene.add("body", abs_parts)

    pelvis = superellipsoid(0.156, 0.096, 0.146, e1=0.68, e2=0.72, u_segs=18, v_segs=10, name="pelvis_core", material="chitin_dark").translate(0.0, 0.466, -0.062)
    petiole = superellipsoid(0.072, 0.064, 0.072, e1=0.68, e2=0.74, u_segs=14, v_segs=10, name="petiole", material="chitin_dark").translate(0.0, 0.452, -0.205)
    neck = superellipsoid(0.126, 0.084, 0.116, e1=0.72, e2=0.76, u_segs=16, v_segs=10, name="neck_collar", material="chitin_dark").rotate_xyz(rx=0.30).translate(0.0, 0.875, 0.105)
    scene.add("body", [pelvis, petiole, neck])

    # Smooth curved scale-plated armored pauldrons: 2 primary domed shoulder lames + 3 arched
    # overlapping armor scales cascading over the deltoid, lined with raised steel rivets.
    pauldron_specs = (
        ((0.282, 0.962, -0.015), (0.164, 0.074, 0.162), -0.38),
        ((0.368, 0.888, -0.015), (0.132, 0.062, 0.142), -0.78),
    )
    plate_parts: List[MeshPart] = []
    for idx, (ctr, (rx, ry, rz), roll) in enumerate(pauldron_specs):
        e1, e2 = 0.56, 0.68
        plate = superellipsoid(rx, ry, rz, e1=e1, e2=e2, u_segs=20, v_segs=12, name=f"pauldron_{idx}", material="chitin_team").rotate_xyz(rz=roll).translate(*ctr)
        plate_parts.append(plate)
        rot = np.array([[math.cos(roll), -math.sin(roll), 0.0], [math.sin(roll), math.cos(roll), 0.0], [0.0, 0.0, 1.0]])
        for r_i, (lx, lz) in enumerate(((0.56 * rx, 0.0), (0.34 * rx, 0.56 * rz), (0.34 * rx, -0.56 * rz))):
            local = np.array([lx, _super_top(rx, ry, rz, e1, e2, lx, lz) - 0.002, lz])
            normal = rot @ np.array([0.22 * lx / rx, 1.0, 0.22 * lz / rz])
            scene.add_mirrored("body", _rivet(rot @ local + np.asarray(ctr), normal, f"pauldron_rivet_{idx}_{r_i}", radius=0.013))
        trim = (
            torus_arc(0.92 * rz, 0.0085, u_segs=20, v_segs=6, plane="xz", name=f"pauldron_seam_{idx}", material="chitin_dark")
            .scale(rx / rz, 1.0, 1.0)
            .translate(0.0, -0.38 * ry, 0.0)
            .rotate_xyz(rz=roll)
            .translate(*ctr)
        )
        scene.add_mirrored("body", trim)
    for plate in plate_parts:
        scene.add_mirrored("body", plate)

    # Row of 3 curved overlapping armor scales rimming the outer deltoid crest (matches HD reference).
    for s_idx, (z_off, yaw_z) in enumerate(((-0.072, -0.28), (0.0, 0.0), (0.072, 0.28))):
        sc_ctr = np.array([0.418, 0.818, -0.015 + z_off])
        scale_plate = (
            superellipsoid(0.056, 0.042, 0.054, e1=0.58, e2=0.66, u_segs=14, v_segs=10, name=f"pauldron_scale_{s_idx}", material="chitin_team")
            .rotate_xyz(ry=yaw_z, rz=-1.06)
            .translate(sc_ctr)
        )
        scale_rim = (
            torus_arc(0.052, 0.0065, u_segs=14, v_segs=6, plane="xz", name=f"scale_rim_{s_idx}", material="chitin_dark")
            .translate(0.0, -0.018, 0.0)
            .rotate_xyz(ry=yaw_z, rz=-1.06)
            .translate(sc_ctr)
        )
        riv_n = _normalize(np.array([0.92, 0.34, z_off * 2.5]))
        scene.add_mirrored("body", scale_plate)
        scene.add_mirrored("body", scale_rim)
        scene.add_mirrored("body", _rivet(sc_ctr + 0.036 * riv_n, riv_n, f"scale_rivet_{s_idx}", radius=0.011))

    # Sleek overlapping 4-step smooth tergite gaster tucked low behind the hips.
    gaster_profile = [
        (0.000, 0.000), (0.036, 0.026), (0.078, 0.070), (0.068, 0.078),
        (0.108, 0.112), (0.124, 0.152), (0.112, 0.160),
        (0.142, 0.194), (0.152, 0.234), (0.140, 0.244),
        (0.150, 0.266), (0.146, 0.304), (0.110, 0.346), (0.000, 0.372),
    ]
    tilt = -0.26
    gaster = (
        lathe(gaster_profile, radial_segs=26, cap_bottom=False, cap_top=False, axis="z", name="gaster", material="chitin_team")
        .scale(1.04, 0.92, 1.0)
        .rotate_xyz(rx=tilt)
        .translate(0.0, 0.452, -0.555)
    )
    scene.add("body", gaster)
    for idx, (z_ring, maj_r) in enumerate(((-0.312, 0.142), (-0.394, 0.114), (-0.476, 0.070))):
        y_ring = 0.452 + (z_ring + 0.555) * math.sin(-tilt)
        ring = (
            torus_arc(maj_r, 0.0075, u_segs=24, v_segs=8, plane="xy", name=f"gaster_seam_{idx}", material="chitin_dark")
            .scale(1.04, 0.92, 1.0)
            .rotate_xyz(rx=tilt)
            .translate(0.0, y_ring, z_ring)
        )
        scene.add("body", ring)

    shell = [core, dorsal, chest, left_pec, right_pec, pelvis] + abs_parts + plate_parts + [p.mirror_x() for p in plate_parts]
    return np.vstack([p.positions for p in shell]).astype(np.float64)


# ---------------------------------------------------------------------------
# Smooth beveled armored head, larger fierce blinking eyes, serrated mandibles
# ---------------------------------------------------------------------------

def _armored_head_deform(pos: np.ndarray) -> np.ndarray:
    """Sculpt a sleek beveled armored cranium with sharp center groove and deep eye sockets."""
    p = pos.copy()
    x, y, z = pos[:, 0], pos[:, 1], pos[:, 2]
    ax = np.abs(x)
    # Beveled angled crown plateau catching studio top-key highlights cleanly.
    crown_y = 0.148 - 0.038 * np.clip((z + 0.06) / 0.26, 0.0, 1.0)
    p[:, 1] = y - 0.48 * np.maximum(0.0, y - crown_y)
    # Crisp narrow vertical center skull seam (V-groove between left & right forehead plates).
    seam = np.exp(-(x / 0.015) ** 2) * np.clip((y + 0.012) / 0.075, 0.0, 1.0) * np.clip((z + 0.05) / 0.20, 0.0, 1.0)
    radial = p / np.maximum(1e-6, np.linalg.norm(p, axis=1, keepdims=True))
    p -= (0.014 * seam)[:, None] * radial
    # Recessed orbital hollows so the larger eyes nestle convincingly beneath the brow.
    socket = np.exp(-(((ax - EYE_OFFSET[0]) / 0.062) ** 2 + ((y - EYE_OFFSET[1]) / 0.056) ** 2)) * np.clip((z - 0.07) / 0.13, 0.0, 1.0)
    p[:, 2] -= 0.032 * socket
    # Taper lower cheeks neatly into the strong insect jaw.
    lower = np.clip((-y - 0.032) / 0.160, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.22 * (lower ** 1.4)
    p[:, 2] *= 1.0 - 0.06 * (lower ** 1.4)
    return p


def _build_eyelid_shell(name: str, yaw_y: float) -> MeshPart:
    """Curved upper eyelid shell in eye-local space; +X rotation seals 100% of sclera/iris/pupil."""
    lid = hemisphere_shell(0.072, 0.070, 0.068, thickness=0.007, start_phi=-0.06, u_segs=24, v_segs=10, name=name, material="chitin_team")
    return lid.rotate_xyz(rx=-0.82, ry=yaw_y)


def _sculpt_head(scene: SceneBuilder, blink: float) -> None:
    hx, hy, hz = HEAD_CENTER
    cranium = (
        superellipsoid(0.212, 0.186, 0.198, e1=0.64, e2=0.70, u_segs=36, v_segs=26, name="cranium", material="chitin_team")
        .deform(_armored_head_deform)
        .translate(hx, hy, hz)
    )
    scene.add("head", cranium)

    # Smooth brooding overhanging chitin brow ridges angling sharply into a fierce center scowl.
    brow = (
        superellipsoid(0.106, 0.028, 0.064, e1=0.56, e2=0.64, u_segs=24, v_segs=14, name="brow_ledge", material="chitin_team")
        .rotate_xyz(rx=0.20, ry=0.40, rz=0.35)
        .translate(0.090, hy + 0.064, hz + 0.172)
    )
    scene.add_mirrored("head", brow)

    # Subtle steel armor rivets along the forehead V-plate seams (authentic HD reference detail!).
    for r_i, (rx, ry, rz) in enumerate(((0.034, 0.106, 0.158), (0.076, 0.130, 0.132), (0.120, 0.138, 0.096))):
        pos = np.array([hx + rx, hy + ry, hz + rz])
        nrm = _normalize(np.array([rx * 0.8, ry + 0.12, rz]))
        scene.add_mirrored("head", _rivet(pos, nrm, f"crown_rivet_{r_i}", radius=0.0075))

    # Deep dark orbital sockets + larger fiercer 3-part eyes (cream sclera, olive-gold iris, obsidian pupil).
    ex, ey, ez = EYE_OFFSET
    eye = np.array([ex, hy + ey, hz + ez])
    scene.add_mirrored(
        "eyes",
        torus_arc(0.064, 0.012, u_segs=24, v_segs=8, plane="xy", name="orbital_rim", material="chitin_dark")
        .scale(1.02, 0.94, 0.90)
        .rotate_xyz(rx=-0.16, ry=0.20)
        .translate(eye + np.array([0.0, 0.0, 0.004])),
    )
    scene.add_mirrored(
        "eyes",
        ellipsoid(0.072, 0.064, 0.044, u_segs=20, v_segs=14, name="orbital_socket", material="chitin_dark")
        .rotate_xyz(rx=-0.16, ry=0.20)
        .translate(eye + np.array([0.0, 0.0, -0.014])),
    )
    scene.add_mirrored(
        "eyes",
        superellipsoid(0.062, 0.057, 0.047, e1=0.88, e2=0.90, u_segs=22, v_segs=16, name="sclera", material="eye_white")
        .rotate_xyz(rx=-0.16, ry=0.20)
        .translate(eye),
    )
    scene.add_mirrored(
        "eyes",
        ellipsoid(0.036, 0.037, 0.018, u_segs=18, v_segs=12, name="iris", material="eye_iris")
        .rotate_xyz(rx=-0.14, ry=0.15)
        .translate(eye + np.array([-0.006, -0.003, 0.035])),
    )
    scene.add_mirrored(
        "eyes",
        ellipsoid(0.019, 0.020, 0.012, u_segs=14, v_segs=10, name="pupil", material="eye_pupil")
        .rotate_xyz(rx=-0.14, ry=0.13)
        .translate(eye + np.array([-0.008, -0.003, 0.043])),
    )

    # Pivoted blinkable upper eyelids matching `base_ant` convention.
    rest = quat_from_axis_angle((1.0, 0.0, 0.0), max(0.0, min(1.0, float(blink))) * EYELID_CLOSED_ANGLE)
    for node, sign in (("eyelid_left", 1.0), ("eyelid_right", -1.0)):
        scene.add(node, _build_eyelid_shell(f"{node}_shell", yaw_y=0.20 * sign))
        scene.set_node_transform(node, translation=(sign * eye[0], eye[1], eye[2]), rotation=rest)
    scene.add_blink_animation(("eyelid_left", "eyelid_right"), closed_angle=EYELID_CLOSED_ANGLE, name="Blink")

    # Clypeus armor nose bridge + bold curved serrated pincer mandibles.
    scene.add(
        "head",
        superellipsoid(0.062, 0.032, 0.042, e1=0.60, e2=0.68, u_segs=18, v_segs=12, name="clypeus", material="chitin_team")
        .rotate_xyz(rx=0.35)
        .translate(0.0, hy - 0.100, hz + 0.180),
    )
    scene.add("head", ellipsoid(0.058, 0.032, 0.046, u_segs=16, v_segs=10, name="mouth_cleft", material="chitin_dark").translate(0.0, hy - 0.144, hz + 0.172))
    jaw = curved_tube(
        [(0.078, hy - 0.118, hz + 0.118), (0.106, hy - 0.152, hz + 0.182), (0.080, hy - 0.170, hz + 0.244), (0.024, hy - 0.176, hz + 0.266)],
        [(0.035, 0.039), (0.041, 0.045), (0.032, 0.035), (0.013, 0.015)],
        samples_per_seg=4, radial_segs=14, cap_rings=3, name="mandible_jaw", material="chitin_team",
    )
    scene.add_mirrored("head", jaw)
    for t_idx, (tx, ty, tz, rad, hgt, rz_deg) in enumerate((
        (0.080, hy - 0.166, hz + 0.226, 0.013, 0.032, 66.0),
        (0.059, hy - 0.174, hz + 0.248, 0.011, 0.028, 76.0),
        (0.038, hy - 0.177, hz + 0.260, 0.009, 0.022, 86.0),
    )):
        tooth = cone(rad, hgt, radial_segs=12, name=f"mandible_tooth_{t_idx}", material="chitin_team").rotate_xyz(rx=12.0, ry=-18.0, rz=rz_deg, degrees=True).translate(tx, ty, tz)
        scene.add_mirrored("head", tooth)

    # Kinked 2-segment dark antennae with sculpted sockets and subtle clubbed tips.
    base = (0.084, hy + 0.144, hz + 0.042)
    elbow = (0.118, hy + 0.266, hz + 0.046)
    scene.add_mirrored("antennae", swept_tube([base, (0.098, hy + 0.206, hz + 0.040), elbow], [0.019, 0.015, 0.017], radial_segs=12, smooth_subdiv=2, cap_rings=2, name="antenna_scape", material="chitin_dark"))
    scene.add_mirrored("antennae", sphere(0.019, u_segs=12, v_segs=8, name="antenna_joint", material="chitin_dark").translate(*elbow))
    scene.add_mirrored("antennae", curved_tube([elbow, (0.150, hy + 0.292, hz + 0.106), (0.166, hy + 0.262, hz + 0.166)], [0.015, 0.020, 0.026], samples_per_seg=4, radial_segs=12, cap_rings=3, name="antenna_club", material="chitin_dark"))
    scene.add_mirrored("antennae", ellipsoid(0.032, 0.018, 0.032, u_segs=14, v_segs=8, name="antenna_socket", material="chitin_dark").rotate_xyz(rx=0.20, rz=-0.30).translate(*base))


def _build_head(scene: SceneBuilder, blink: float) -> None:
    head = SceneBuilder(name="combat_head")
    _sculpt_head(head, blink)
    head.translate_all(-HEAD_CENTER[0], -HEAD_CENTER[1], -HEAD_CENTER[2])
    head.scale_all(HEAD_SCALE)
    head.translate_all(*HEAD_CENTER)
    scene.merge_from(head)


# ---------------------------------------------------------------------------
# Smooth contoured armored arms, gauntlets, fists, and sturdy armored legs
# ---------------------------------------------------------------------------

def _build_arms(scene: SceneBuilder) -> None:
    """Build smooth muscular contoured arm armor, beveled gauntlets, and dark clawed fists."""
    sh, mid, el, wr, fi = (np.asarray(v, dtype=np.float64) for v in (SHOULDER, BICEP_MID, ELBOW, WRIST, FIST))
    parts: List[MeshPart] = [
        sphere(0.090, u_segs=14, v_segs=10, name="shoulder_joint", material="chitin_dark").translate(sh),
        curved_tube([sh, mid, el], [0.080, 0.092, 0.076], samples_per_seg=4, radial_segs=14, cap_rings=2, name="upper_arm", material="chitin_team"),
        _segment_lathe(sh + 0.22 * (el - sh), sh + 0.78 * (el - sh), [(0.088, 0.0), (0.098, 0.14), (0.099, 0.76), (0.090, 1.0)], name="bicep_bracer", material="chitin_team", segs=18),
        _ring_on_segment(sh, el, 0.34, 0.097, 0.008, name="bracer_seam_0", material="chitin_dark"),
        _ring_on_segment(sh, el, 0.78, 0.091, 0.008, name="bracer_seam_1", material="chitin_dark"),
        sphere(0.075, u_segs=14, v_segs=10, name="elbow_joint", material="chitin_dark").translate(el),
        superellipsoid(0.068, 0.056, 0.044, e1=0.58, e2=0.66, u_segs=14, v_segs=10, name="elbow_cop", material="chitin_team").rotate_xyz(rx=0.45, rz=-0.30).translate(el + np.array([0.020, 0.006, -0.060])),
        # Contoured beveled forearm gauntlet with recessed dark seam bands and heavy cuff.
        _segment_lathe(el, wr, [(0.070, 0.0), (0.092, 0.10), (0.106, 0.42), (0.102, 0.80), (0.088, 1.0)], name="gauntlet", material="chitin_team", segs=18),
        _ring_on_segment(el, wr, 0.35, 0.1055, 0.0085, name="gauntlet_seam_0", material="chitin_dark"),
        _ring_on_segment(el, wr, 0.66, 0.1035, 0.0085, name="gauntlet_seam_1", material="chitin_dark"),
        _ring_on_segment(el, wr, 0.96, 0.090, 0.014, name="gauntlet_cuff", material="chitin_dark"),
    ]
    for part in parts:
        scene.add_mirrored("arms", part)

    # Metallic steel stud ring around each armored gauntlet cuff.
    mat, length = _axis_frame(el, wr)
    for k in range(6):
        ang = 2.0 * math.pi * k / 6.0 + 0.25
        local_n = np.array([math.cos(ang), 0.0, math.sin(ang)])
        local_p = local_n * 0.101 + np.array([0.0, 0.83 * length, 0.0])
        world_p = (mat @ np.append(local_p, 1.0))[:3]
        world_n = mat[:3, :3] @ local_n
        scene.add_mirrored("arms", _rivet(world_p, world_n, f"gauntlet_rivet_{k}", radius=0.0115))

    # Heavy sculpted dark armored fist-claws.
    scene.add_mirrored(
        "arms",
        superellipsoid(0.078, 0.074, 0.082, e1=0.62, e2=0.70, u_segs=18, v_segs=14, name="fist_palm", material="chitin_dark")
        .rotate_xyz(rx=0.25, ry=-0.40, rz=0.12)
        .translate(fi),
    )
    claws = (
        ((0.034, 0.040, 0.050), (0.016, 0.026, 0.106), (-0.018, -0.006, 0.114)),
        ((0.038, -0.002, 0.056), (0.018, -0.018, 0.112), (-0.018, -0.044, 0.110)),
        ((0.032, -0.044, 0.046), (0.012, -0.060, 0.096), (-0.016, -0.074, 0.092)),
    )
    for c_idx, ctrl in enumerate(claws):
        pts = [fi + np.asarray(c) for c in ctrl]
        scene.add_mirrored("arms", curved_tube(pts, [0.025, 0.021, 0.009], samples_per_seg=4, radial_segs=12, cap_rings=2, name=f"fist_claw_{c_idx}", material="chitin_dark"))
    thumb = [fi + np.array(c) for c in ((-0.046, 0.040, 0.024), (-0.072, 0.026, 0.072), (-0.044, -0.004, 0.102))]
    scene.add_mirrored("arms", curved_tube(thumb, [0.025, 0.021, 0.010], samples_per_seg=4, radial_segs=12, cap_rings=2, name="fist_thumb", material="chitin_dark"))


def _sturdy_foot(name: str) -> MeshPart:
    """Broad sturdy armored dark insect claw-foot resting squarely on Y = 0.0."""
    prof = [(0.000, -0.044), (0.032, -0.032), (0.044, 0.004), (0.038, 0.048), (0.022, 0.084), (0.000, 0.102)]
    foot = lathe(prof, radial_segs=16, cap_bottom=False, cap_top=False, axis="z", name=name, material="chitin_dark").scale(1.12, 0.72, 1.0)
    foot = foot.deform(lambda p: np.column_stack([p[:, 0], np.maximum(p[:, 1], -0.020), p[:, 2]]))
    return foot.translate(0.0, -float(foot.bounds()[0][1]), 0.0)


def _build_leg(scene: SceneBuilder, label: str, hip: Vec3, knee: Vec3, ankle: Vec3, foot_yaw: float) -> None:
    """Sturdy thick smooth-sculpted armored dark insect leg (no toothpick sticks)."""
    p_hip, p_knee, p_ankle = (np.asarray(v, dtype=np.float64) for v in (hip, knee, ankle))
    femur_mid = 0.5 * (p_hip + p_knee) + np.array([0.0, 0.024, 0.0])
    tibia_mid = 0.45 * p_knee + 0.55 * p_ankle + np.array([0.014, 0.0, 0.0])
    parts = [
        ellipsoid(0.066, 0.056, 0.066, u_segs=12, v_segs=8, name=f"coxa_{label}", material="chitin_dark").translate(p_hip),
        curved_tube([p_hip, femur_mid, p_knee], [(0.052, 0.058), (0.062, 0.072), (0.048, 0.054)], samples_per_seg=3, radial_segs=12, cap_rings=2, name=f"femur_{label}", material="chitin_dark"),
        superellipsoid(0.054, 0.058, 0.054, e1=0.66, e2=0.72, u_segs=12, v_segs=8, name=f"knee_{label}", material="chitin_dark").translate(p_knee),
        superellipsoid(0.044, 0.036, 0.032, e1=0.60, e2=0.68, u_segs=12, v_segs=8, name=f"knee_greave_{label}", material="chitin_team").rotate_xyz(rx=0.25, rz=-0.35).translate(p_knee + np.array([0.024, 0.018, 0.018 if label == "mid" else -0.018])),
        curved_tube([p_knee, tibia_mid, p_ankle], [0.050, 0.042, 0.034], samples_per_seg=3, radial_segs=12, cap_rings=2, name=f"tibia_{label}", material="chitin_dark"),
        torus_arc(0.036, 0.0085, u_segs=12, v_segs=6, plane="xz", name=f"ankle_cuff_{label}", material="chitin_dark").translate(p_ankle[0], p_ankle[1] + 0.012, p_ankle[2]),
        _sturdy_foot(f"tarsus_{label}").rotate_xyz(ry=foot_yaw).translate(p_ankle[0], 0.0, p_ankle[2] + 0.006),
    ]
    for part in parts:
        scene.add_mirrored("legs", part)


def _build_legs(scene: SceneBuilder) -> None:
    _build_leg(scene, "mid", hip=(0.152, 0.432, 0.052), knee=(0.288, 0.415, 0.125), ankle=(0.258, 0.044, 0.192), foot_yaw=0.24)
    _build_leg(scene, "rear", hip=(0.148, 0.405, -0.148), knee=(0.292, 0.405, -0.238), ankle=(0.264, 0.044, -0.296), foot_yaw=math.pi - 0.26)


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
    """Conform closed belt spline points cleanly outside the torso shell with zero clipping."""
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
    # Ribbed woven-gold ammo pocket pad wrapping the center of the cartridge (segs=1 keeps bevels crisp & lean).
    loop_pad = rounded_box(0.084, 0.032, 0.042, bevel=0.010, segs=1, name=f"bandolier_pad_{label}", material="gear_gold").transform(mat)
    # Metallic brass bullet casing emerging cleanly on both ends of the woven loop.
    casing = cylinder(0.016, 0.016, 0.114, radial_segs=8, bevel=0.0, axis="x", name=f"bullet_case_{label}", material="gear_gold_metal").translate(-0.006, 0.006, 0.0).transform(mat)
    # Tapered copper-brass bullet tip peeking crisply at the outer edge.
    tip_prof = [(0.000, 0.048), (0.0155, 0.048), (0.000, 0.076)]
    tip = lathe(tip_prof, radial_segs=8, cap_bottom=False, cap_top=False, axis="x", name=f"bullet_tip_{label}", material="gear_brass_trim").translate(0.0, 0.006, 0.0).transform(mat)
    return [loop_pad, casing, tip]


def _build_bandoliers(scene: SceneBuilder, torso_verts: np.ndarray) -> None:
    """Crossed ribbed woven-gold bullet bandoliers over BOTH chest and back with brass X-buckles."""
    belt_ctrl = [
        (0.190, 1.052, -0.085),   # over left shoulder behind the head
        (0.205, 0.948, 0.150),    # upper-left chest
        (0.112, 0.805, 0.242),    # left pectoral
        (0.000, 0.680, 0.268),    # front chest X crossing
        (-0.132, 0.572, 0.232),   # lower-right abdomen
        (-0.228, 0.505, 0.110),   # right front hip
        (-0.248, 0.505, -0.070),  # right side hip
        (-0.182, 0.585, -0.252),  # lower-right back
        (0.000, 0.745, -0.308),   # back X crossing
        (0.150, 0.905, -0.278),   # upper-left back
        (0.200, 1.005, -0.170),   # rear left shoulder
    ]
    pivot = np.array([0.0, 0.715, -0.040])
    crossings = {"front": np.array([0.0, 0.680, 0.300]), "back": np.array([0.0, 0.745, -0.340])}

    for belt_idx, sign in enumerate((1.0, -1.0)):
        raw = np.array([(sign * x, y, z) for x, y, z in belt_ctrl], dtype=np.float64)
        pts = catmull_rom_spline(raw, samples_per_seg=6, closed=True)
        out_n = np.array([_normalize(p - pivot + (np.array([0.0, 0.30, 0.0]) if p[1] > 0.93 else 0.0)) for p in pts])
        pts = _conform_belt_to_torso(pts, out_n, torso_verts, pivot, clearance=0.014)
        if belt_idx == 1:
            pts = pts + (0.032 * np.exp(-((pts[:, 0] / 0.11) ** 2)))[:, None] * out_n
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
            if abs(p[0]) < 0.085 and abs(p[2]) > 0.15:
                continue
            t = _normalize((1.0 - f) * tangents[i] + f * tangents[j])
            o = _normalize((1.0 - f) * surf_n[i] + f * surf_n[j])
            w = _normalize(np.cross(o, t))
            o = np.cross(t, w)
            # Orient brass tips consistently outward across each strap.
            if np.dot(w, np.array([sign, 0.25, 0.0])) < 0.0:
                w = -w
                t = -t
            scene.add("gear_bandolier", _make_brass_bullet_pad(p + 0.021 * o, w, o, t, f"{belt_idx}_{k}"))

    # Riveted brass X-junction buckles on BOTH front chest AND back.
    for tag, pos in crossings.items():
        tilt = 14.0 if tag == "front" else -16.0
        face = 1.0 if tag == "front" else -1.0
        frame = rounded_box(0.154, 0.154, 0.036, bevel=0.012, segs=2, name=f"buckle_frame_{tag}", material="gear_gold_metal").rotate_xyz(rx=tilt, rz=45.0, degrees=True).translate(*pos)
        recess = rounded_box(0.096, 0.096, 0.042, bevel=0.008, segs=1, name=f"buckle_recess_{tag}", material="gear_dark").rotate_xyz(rx=tilt, rz=45.0, degrees=True).translate(*pos)
        boss = superellipsoid(0.040, 0.040, 0.022, e1=0.74, e2=0.74, u_segs=14, v_segs=8, name=f"buckle_boss_{tag}", material="gear_brass_trim").rotate_xyz(rx=tilt, degrees=True).translate(pos[0], pos[1], pos[2] + 0.018 * face)
        scene.add("gear_bandolier", [frame, recess, boss])


# ---------------------------------------------------------------------------
# Signature crimson cloth ribbon bow on the RIGHT (-X) bicep
# ---------------------------------------------------------------------------

def _build_right_bicep_bow(scene: SceneBuilder) -> None:
    """Weathered crimson cloth ribbon tied in a sculpted two-loop bow around the RIGHT (-X) bicep."""
    sh = np.array([-SHOULDER[0], SHOULDER[1], SHOULDER[2]])
    el = np.array([-ELBOW[0], ELBOW[1], ELBOW[2]])
    center = sh + 0.56 * (el - sh)
    t = _normalize(el - sh)
    out_x = _normalize(np.array([-1.0, 0.0, -0.15]) - np.dot(np.array([-1.0, 0.0, -0.15]), t) * t)
    fwd = _normalize(np.cross(t, out_x))
    out_x = _normalize(np.cross(fwd, t))

    band_r = 0.104
    angs = np.linspace(0.0, 2.0 * math.pi, 26, endpoint=False)
    band_out = np.array([math.cos(a) * out_x + math.sin(a) * fwd for a in angs])
    scene.add("gear_bow", _sculpted_ribbon_mesh(center[None, :] + band_r * band_out, band_out, np.full(26, 0.027), np.full(26, 0.010), closed=True, profile_segs=8, name="bicep_ribbon_band", material="gear_accent"))

    knot = center + (band_r + 0.009) * out_x
    scene.add("gear_bow", superellipsoid(0.030, 0.027, 0.030, e1=0.72, e2=0.78, u_segs=12, v_segs=8, name="bow_knot", material="gear_accent").translate(knot))

    up_v, back_v = -t, -fwd
    for sign, tag in ((1.0, "upper"), (-1.0, "lower")):
        loop = [knot, knot + 0.010 * out_x + sign * 0.046 * up_v + 0.036 * back_v, knot + 0.018 * out_x + sign * 0.054 * up_v + 0.010 * back_v, knot + 0.014 * out_x + sign * 0.020 * up_v - 0.026 * back_v, knot + 0.004 * out_x]
        scene.add("gear_bow", curved_tube(loop, [0.013, 0.021, 0.022, 0.018, 0.013], samples_per_seg=3, radial_segs=8, cap_rings=2, name=f"bow_loop_{tag}", material="gear_accent"))

    face = _normalize(0.45 * out_x + 0.78 * fwd + 0.42 * up_v)[None, :]
    for tag, ctrl, widths in (
        ("upper_tail", [knot, knot + 0.008 * out_x + 0.020 * up_v + 0.050 * back_v, knot + 0.012 * out_x + 0.032 * up_v + 0.104 * back_v, knot + 0.012 * out_x + 0.026 * up_v + 0.144 * back_v], [0.013, 0.024, 0.029, 0.014]),
        ("lower_tail", [knot, knot + 0.006 * out_x - 0.032 * up_v + 0.044 * back_v, knot + 0.010 * out_x - 0.064 * up_v + 0.088 * back_v, knot + 0.010 * out_x - 0.088 * up_v + 0.120 * back_v], [0.013, 0.023, 0.027, 0.013]),
    ):
        pts = catmull_rom_spline(ctrl, samples_per_seg=4, closed=False)
        n_t = len(pts)
        w_arr = np.interp(np.linspace(0.0, 1.0, n_t), np.linspace(0.0, 1.0, len(widths)), widths)
        th_arr = np.full(n_t, 0.010)
        th_arr[-1] = 0.006
        scene.add("gear_bow", _sculpted_ribbon_mesh(pts, np.repeat(face, n_t, axis=0), w_arr, th_arr, closed=False, profile_segs=8, name=f"bow_{tag}", material="gear_accent"))


def build_combat(blink: float = 0.0) -> SceneBuilder:
    """Build the smooth high-detail armored Combat Ant (`blink` in [0, 1])."""
    scene = SceneBuilder(name="ant_combat")
    torso_verts = _build_torso(scene)
    _build_head(scene, blink)
    _build_arms(scene)
    _build_legs(scene)
    _build_bandoliers(scene, torso_verts)
    _build_right_bicep_bow(scene)
    scene.ground_to_y(0.0)
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical module entrypoint for `build_all.py`."""
    return build_combat(blink=blink)


__all__ = ["build_ant", "build_combat"]
