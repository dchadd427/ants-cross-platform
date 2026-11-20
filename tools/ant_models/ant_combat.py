"""Sculpted 3D model for the Combat Ant: a tough armored insect commando.

Built directly with `mesh_core.py` (independent of `base_ant`) to match the
original's broad brawler silhouette (~2.2x Worker width, ~1.25x Worker height):
- Faceted armored head with a vertical skull seam, a heavy angled V brow
  ledge, deep dark orbital sockets, 3-part eyes, and toothed pincer mandibles
- Pivoted `eyelid_left` / `eyelid_right` nodes with the shared glTF 2.0 "Blink"
  clip (same convention as `base_ant`)
- Riveted lamellar pauldrons, segmented bracers, heavy plated gauntlets, dark
  clawed fists, and sturdy angular dark legs grounded at Y = 0.0
- Crossed ribbed padded gold bandoliers over chest AND back with brass buckles
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

HEAD_CENTER: Vec3 = (0.0, 0.985, 0.185)
HEAD_SCALE = 1.15  # the head is sculpted at unit scale, then enlarged about HEAD_CENTER
EYE_OFFSET: Vec3 = (0.086, -0.012, 0.164)  # left eye relative to head center (before HEAD_SCALE)

# Left (+X) arm joints; the right arm mirrors them.
SHOULDER: Vec3 = (0.330, 0.865, -0.010)
ELBOW: Vec3 = (0.462, 0.590, -0.020)
WRIST: Vec3 = (0.428, 0.425, 0.155)
FIST: Vec3 = (0.410, 0.382, 0.225)


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
    """Return a 4x4 frame mapping local +Y onto segment p0->p1 (origin at p0) and its length."""
    a, b = np.asarray(p0, dtype=np.float64), np.asarray(p1, dtype=np.float64)
    y = _normalize(b - a)
    ref = np.array([0.0, 0.0, 1.0]) if abs(y[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
    x = _normalize(np.cross(y, ref))
    z = np.cross(x, y)
    return _frame(a, x, y, z), float(np.linalg.norm(b - a))


def _segment_lathe(p0: Sequence[float], p1: Sequence[float], radii_at: Sequence[Tuple[float, float]], *, name: str, material: str, segs: int = 18) -> MeshPart:
    """Lathe a profile of `(radius, fraction_along_segment)` pairs along p0 -> p1."""
    mat, length = _axis_frame(p0, p1)
    prof = [(r, f * length) for r, f in radii_at]
    return lathe(prof, radial_segs=segs, cap_bottom=True, cap_top=True, axis="y", name=name, material=material).transform(mat)


def _ring_on_segment(p0: Sequence[float], p1: Sequence[float], frac: float, major_r: float, minor_r: float, *, name: str, material: str) -> MeshPart:
    mat, length = _axis_frame(p0, p1)
    ring = torus_arc(major_r, minor_r, u_segs=22, v_segs=8, plane="xz", name=name, material=material)
    return ring.translate(0.0, frac * length, 0.0).transform(mat)


def _rivet(pos: np.ndarray, normal: np.ndarray, name: str, radius: float = 0.013) -> MeshPart:
    n = _normalize(normal)
    ref = np.array([1.0, 0.0, 0.0]) if abs(n[0]) < 0.9 else np.array([0.0, 0.0, 1.0])
    x = _normalize(np.cross(ref, n))
    z = np.cross(x, n)
    dome = ellipsoid(radius, 0.55 * radius, radius, u_segs=10, v_segs=6, name=name, material="gear_steel")
    return dome.transform(_frame(pos, x, n, z))


def _facet(part: MeshPart) -> MeshPart:
    """Hard-surface shading: unweld every triangle and give it its flat face normal."""
    tri = part.positions.astype(np.float64)[part.indices.reshape(-1, 3)]
    fn = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    lens = np.linalg.norm(fn, axis=1)
    keep = lens > 1e-12
    tri, fn = tri[keep], fn[keep] / lens[keep, None]
    pos = tri.reshape(-1, 3).astype(np.float32)
    nrm = np.repeat(fn, 3, axis=0).astype(np.float32)
    return MeshPart(part.name, part.material, pos, nrm, np.arange(len(pos), dtype=np.uint32))


def _super_top(rx: float, ry: float, rz: float, e1: float, e2: float, lx: float, lz: float) -> float:
    """Height of a superellipsoid's upper surface above local (lx, lz)."""
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
    """Sweep a beveled flat ribbon (strap, cloth band) oriented flush to `out_normals`."""
    n = len(pts)
    tangents = np.zeros((n, 3), dtype=np.float64)
    for i in range(n):
        if closed:
            d = pts[(i + 1) % n] - pts[(i - 1) % n]
        else:
            d = pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]
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
# Torso, pauldrons, gaster
# ---------------------------------------------------------------------------

def _hunch_deform(pos: np.ndarray) -> np.ndarray:
    """Broaden the upper back into muscular trapezius shelves and taper the waist."""
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    upper = np.clip((y + 0.05) / 0.26, 0.0, 1.0)
    lower = np.clip(-y / 0.24, 0.0, 1.0)
    p[:, 0] = x * (1.0 + 0.08 * upper - 0.18 * lower ** 1.4)
    crest = np.exp(-(x / 0.26) ** 2) * np.clip((-z + 0.04) / 0.30, 0.0, 1.0) * upper
    p[:, 1] = y + 0.030 * crest
    return p


def _build_torso(scene: SceneBuilder) -> np.ndarray:
    """Build the armored thorax, abdominal plates, pauldrons, and gaster.

    Returns the torso shell vertices the bandoliers conform over.
    """
    core = superellipsoid(0.225, 0.240, 0.192, e1=0.60, e2=0.62, u_segs=24, v_segs=16, name="thorax_core", material="chitin_dark").translate(0.0, 0.715, -0.030)
    dorsal = (
        superellipsoid(0.262, 0.212, 0.200, e1=0.34, e2=0.42, u_segs=28, v_segs=18, name="dorsal_carapace", material="chitin_team")
        .deform(_hunch_deform)
        .rotate_xyz(rx=0.12)
        .translate(0.0, 0.800, -0.070)
    )
    # Chamfered breastplate: flat front plane, crisp bevels, plus angled pectoral plates.
    chest = superellipsoid(0.228, 0.186, 0.124, e1=0.28, e2=0.36, u_segs=24, v_segs=16, name="breastplate", material="chitin_team").rotate_xyz(rx=0.10).translate(0.0, 0.735, 0.092)
    pec_l = _facet(superellipsoid(0.114, 0.100, 0.060, e1=0.30, e2=0.34, u_segs=8, v_segs=6, name="pectoral_plate", material="chitin_team").rotate_xyz(rx=0.14, ry=0.30, rz=-0.06).translate(0.112, 0.778, 0.170))
    left_pec, right_pec = scene.add_mirrored("body", pec_l)
    scene.add("body", [core, dorsal, chest])

    # Stacked segmented abdominal plates with dark seams, pinching into a narrow waist.
    abs_parts = []
    for i, (y, rx, rz, z) in enumerate(((0.572, 0.160, 0.104, 0.088), (0.506, 0.130, 0.090, 0.066))):
        abs_parts.append(_facet(superellipsoid(rx, 0.036, rz, e1=0.30, e2=0.34, u_segs=10, v_segs=6, name=f"ab_plate_{i}", material="chitin_team").rotate_xyz(rx=0.12).translate(0.0, y, z)))
    scene.add("body", abs_parts)
    pelvis = superellipsoid(0.150, 0.092, 0.142, e1=0.55, e2=0.60, u_segs=20, v_segs=12, name="pelvis_core", material="chitin_dark").translate(0.0, 0.468, -0.060)
    petiole = _facet(superellipsoid(0.070, 0.062, 0.070, e1=0.45, e2=0.50, u_segs=8, v_segs=6, name="petiole", material="chitin_dark").translate(0.0, 0.452, -0.205))
    neck = superellipsoid(0.124, 0.082, 0.114, e1=0.60, e2=0.64, u_segs=16, v_segs=10, name="neck_collar", material="chitin_dark").rotate_xyz(rx=0.30).translate(0.0, 0.875, 0.100)
    scene.add("body", [pelvis, petiole, neck])

    # Riveted faceted lamellar pauldrons cascading outward, each with a raised outer flange.
    pauldron_specs = (
        ((0.285, 0.960, -0.015), (0.160, 0.070, 0.160), -0.40),
        ((0.372, 0.885, -0.015), (0.128, 0.058, 0.138), -0.80),
    )
    plate_parts: List[MeshPart] = []
    for idx, (ctr, (rx, ry, rz), roll) in enumerate(pauldron_specs):
        e1, e2 = 0.30, 0.42
        plate = _facet(superellipsoid(rx, ry, rz, e1=e1, e2=e2, u_segs=16, v_segs=8, name=f"pauldron_{idx}", material="chitin_team").rotate_xyz(rz=roll).translate(*ctr))
        plate_parts.append(plate)
        rot = np.array([[math.cos(roll), -math.sin(roll), 0.0], [math.sin(roll), math.cos(roll), 0.0], [0.0, 0.0, 1.0]])
        flange = _facet(superellipsoid(0.030, 0.75 * ry, 0.96 * rz, e1=0.30, e2=0.36, u_segs=10, v_segs=6, name=f"pauldron_flange_{idx}", material="chitin_team"))
        scene.add_mirrored("body", flange.rotate_xyz(rz=roll - 0.35).translate(*(rot @ np.array([0.92 * rx, 0.10 * ry, 0.0]) + np.asarray(ctr))))
        for r_i, (lx, lz) in enumerate(((0.55 * rx, 0.0), (0.30 * rx, 0.62 * rz), (0.30 * rx, -0.62 * rz))):
            local = np.array([lx, _super_top(rx, ry, rz, e1, e2, lx, lz) - 0.002, lz])
            normal = rot @ np.array([0.0, 1.0, 0.0])
            scene.add_mirrored("body", _rivet(rot @ local + np.asarray(ctr), normal, f"pauldron_rivet_{idx}_{r_i}"))
        trim = torus_arc(0.90 * rz, 0.008, u_segs=24, v_segs=6, plane="xz", name=f"pauldron_seam_{idx}", material="chitin_dark").scale(rx / rz, 1.0, 1.0).translate(0.0, -0.45 * ry, 0.0).rotate_xyz(rz=roll).translate(*ctr)
        scene.add_mirrored("body", trim)
    for plate in plate_parts:
        scene.add_mirrored("body", plate)

    # Compact pointed gaster with crisp overlapping stepped tergite plates (faceted, 12-sided).
    gaster_profile = [
        (0.000, 0.000), (0.040, 0.030), (0.080, 0.072), (0.068, 0.080),
        (0.110, 0.112), (0.124, 0.152), (0.110, 0.160),
        (0.142, 0.192), (0.152, 0.232), (0.138, 0.242),
        (0.150, 0.264), (0.148, 0.300), (0.112, 0.345), (0.000, 0.372),
    ]
    tilt = -0.26
    gaster = _facet(lathe(gaster_profile, radial_segs=12, cap_bottom=False, cap_top=False, axis="z", name="gaster", material="chitin_team").scale(1.04, 0.92, 1.0).rotate_xyz(rx=tilt).translate(0.0, 0.452, -0.555))
    scene.add("body", gaster)

    shell = [core, dorsal, chest, left_pec, right_pec, pelvis] + abs_parts + plate_parts + [p.mirror_x() for p in plate_parts]
    return np.vstack([p.positions for p in shell]).astype(np.float64)


# ---------------------------------------------------------------------------
# Head, eyes, eyelids, mandibles, antennae
# ---------------------------------------------------------------------------

def _armored_head_deform(pos: np.ndarray) -> np.ndarray:
    """Facet the cranium: flat crown plateau, center skull seam, deep sockets, wedge jaw."""
    p = pos.copy()
    x, y, z = pos[:, 0], pos[:, 1], pos[:, 2]
    ax = np.abs(x)
    crown_y = 0.138 - 0.036 * np.clip((z + 0.06) / 0.26, 0.0, 1.0)
    p[:, 1] = y - 0.55 * np.maximum(0.0, y - crown_y)
    seam = np.exp(-(x / 0.016) ** 2) * np.clip((y + 0.010) / 0.080, 0.0, 1.0)
    radial = p / np.maximum(1e-6, np.linalg.norm(p, axis=1, keepdims=True))
    p -= (0.015 * seam)[:, None] * radial
    socket = np.exp(-(((ax - EYE_OFFSET[0]) / 0.056) ** 2 + ((y - EYE_OFFSET[1]) / 0.050) ** 2)) * np.clip((z - 0.08) / 0.12, 0.0, 1.0)
    p[:, 2] -= 0.030 * socket
    lower = np.clip((-y - 0.030) / 0.160, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.20 * lower ** 1.4
    p[:, 2] *= 1.0 - 0.06 * lower ** 1.4
    return p


def _build_eyelid_shell(name: str, yaw_y: float) -> MeshPart:
    """Curved upper lid in eye-local space; +X rotation by EYELID_CLOSED_ANGLE seals the eye."""
    lid = hemisphere_shell(0.064, 0.064, 0.062, thickness=0.007, start_phi=-0.06, u_segs=22, v_segs=10, name=name, material="chitin_team")
    return lid.rotate_xyz(rx=-0.82, ry=yaw_y)


def _build_head(scene: SceneBuilder, blink: float) -> None:
    """Sculpt the head at unit scale around HEAD_CENTER, enlarge it by HEAD_SCALE, then merge."""
    head = SceneBuilder(name="combat_head")
    _sculpt_head(head, blink)
    head.translate_all(-HEAD_CENTER[0], -HEAD_CENTER[1], -HEAD_CENTER[2])
    head.scale_all(HEAD_SCALE)
    head.translate_all(*HEAD_CENTER)
    scene.merge_from(head)


def _sculpt_head(scene: SceneBuilder, blink: float) -> None:
    hx, hy, hz = HEAD_CENTER
    cranium = superellipsoid(0.205, 0.180, 0.192, e1=0.40, e2=0.48, u_segs=34, v_segs=24, name="cranium", material="chitin_team").deform(_armored_head_deform).translate(hx, hy, hz)
    scene.add("head", cranium)

    # Heavy angled V brow ledge overhanging the eyes (sculpted chitin, no painted brows).
    brow = _facet(superellipsoid(0.102, 0.026, 0.064, e1=0.22, e2=0.30, u_segs=12, v_segs=6, name="brow_ledge", material="chitin_team").rotate_xyz(rx=0.24, ry=0.42, rz=0.38).translate(0.088, hy + 0.054, hz + 0.168))
    scene.add_mirrored("head", brow)

    # Deep dark orbital sockets and 3-part eyes (cream sclera, olive-gold iris, obsidian pupil).
    ex, ey, ez = EYE_OFFSET
    eye = np.array([ex, hy + ey, hz + ez])
    scene.add_mirrored("eyes", ellipsoid(0.066, 0.060, 0.042, u_segs=18, v_segs=12, name="orbital_socket", material="chitin_dark").rotate_xyz(rx=-0.15, ry=0.20).translate(eye + np.array([0.0, 0.0, -0.012])))
    scene.add_mirrored("eyes", superellipsoid(0.054, 0.050, 0.042, e1=0.88, e2=0.90, u_segs=20, v_segs=14, name="sclera", material="eye_white").rotate_xyz(rx=-0.15, ry=0.20).translate(eye))
    scene.add_mirrored("eyes", ellipsoid(0.031, 0.032, 0.016, u_segs=16, v_segs=10, name="iris", material="eye_iris").rotate_xyz(rx=-0.13, ry=0.15).translate(eye + np.array([-0.005, -0.004, 0.031])))
    scene.add_mirrored("eyes", ellipsoid(0.016, 0.018, 0.011, u_segs=12, v_segs=8, name="pupil", material="eye_pupil").rotate_xyz(rx=-0.13, ry=0.13).translate(eye + np.array([-0.007, -0.004, 0.038])))

    rest = quat_from_axis_angle((1.0, 0.0, 0.0), max(0.0, min(1.0, float(blink))) * EYELID_CLOSED_ANGLE)
    for node, sign in (("eyelid_left", 1.0), ("eyelid_right", -1.0)):
        scene.add(node, _build_eyelid_shell(f"{node}_shell", yaw_y=0.20 * sign))
        scene.set_node_transform(node, translation=(sign * eye[0], eye[1], eye[2]), rotation=rest)
    scene.add_blink_animation(("eyelid_left", "eyelid_right"), closed_angle=EYELID_CLOSED_ANGLE, name="Blink")

    # Clypeus plate and stout inward-curving toothed pincer mandibles.
    scene.add("head", _facet(superellipsoid(0.060, 0.030, 0.040, e1=0.30, e2=0.36, u_segs=10, v_segs=6, name="clypeus", material="chitin_team").rotate_xyz(rx=0.35).translate(0.0, hy - 0.098, hz + 0.178)))
    scene.add("head", ellipsoid(0.058, 0.032, 0.046, u_segs=14, v_segs=10, name="mouth_cleft", material="chitin_dark").translate(0.0, hy - 0.142, hz + 0.170))
    jaw = curved_tube(
        [(0.076, hy - 0.118, hz + 0.118), (0.104, hy - 0.150, hz + 0.180), (0.078, hy - 0.168, hz + 0.240), (0.024, hy - 0.174, hz + 0.262)],
        [(0.034, 0.038), (0.040, 0.044), (0.031, 0.034), (0.013, 0.015)],
        samples_per_seg=4, radial_segs=12, cap_rings=3, name="mandible_jaw", material="chitin_team",
    )
    scene.add_mirrored("head", jaw)
    for t_idx, (tx, ty, tz, rad, hgt, rz_deg) in enumerate(((0.078, hy - 0.164, hz + 0.222, 0.012, 0.030, 66.0), (0.058, hy - 0.172, hz + 0.244, 0.011, 0.028, 76.0), (0.038, hy - 0.175, hz + 0.256, 0.009, 0.022, 86.0))):
        tooth = cone(rad, hgt, radial_segs=10, name=f"mandible_tooth_{t_idx}", material="chitin_team").rotate_xyz(rx=12.0, ry=-18.0, rz=rz_deg, degrees=True).translate(tx, ty, tz)
        scene.add_mirrored("head", tooth)

    # Slender kinked 2-segment dark antennae with subtle clubbed tips.
    base = (0.082, hy + 0.140, hz + 0.040)
    elbow = (0.114, hy + 0.262, hz + 0.044)
    scene.add_mirrored("antennae", swept_tube([base, (0.096, hy + 0.202, hz + 0.038), elbow], [0.019, 0.015, 0.017], radial_segs=10, smooth_subdiv=2, cap_rings=2, name="antenna_scape", material="chitin_dark"))
    scene.add_mirrored("antennae", sphere(0.019, u_segs=10, v_segs=8, name="antenna_joint", material="chitin_dark").translate(*elbow))
    scene.add_mirrored("antennae", curved_tube([elbow, (0.146, hy + 0.288, hz + 0.104), (0.162, hy + 0.258, hz + 0.164)], [0.015, 0.020, 0.025], samples_per_seg=4, radial_segs=10, cap_rings=3, name="antenna_club", material="chitin_dark"))
    scene.add_mirrored("antennae", ellipsoid(0.032, 0.018, 0.032, u_segs=12, v_segs=8, name="antenna_socket", material="chitin_dark").rotate_xyz(rx=0.20, rz=-0.30).translate(*base))


# ---------------------------------------------------------------------------
# Arms, fists, legs
# ---------------------------------------------------------------------------

def _build_arms(scene: SceneBuilder) -> None:
    """Build segmented armored arms, plated gauntlets, and dark clawed fists (left, mirrored)."""
    sh, el, wr, fi = (np.asarray(v, dtype=np.float64) for v in (SHOULDER, ELBOW, WRIST, FIST))
    oct_ = 8  # octagonal hard-surface armor segments
    parts: List[MeshPart] = [
        sphere(0.088, u_segs=16, v_segs=12, name="shoulder_joint", material="chitin_dark").translate(sh),
        _facet(_segment_lathe(sh, el, [(0.070, 0.0), (0.084, 0.10), (0.090, 0.55), (0.078, 1.0)], name="upper_arm", material="chitin_team", segs=oct_)),
        _facet(_segment_lathe(sh + 0.20 * (el - sh), sh + 0.78 * (el - sh), [(0.090, 0.0), (0.100, 0.10), (0.100, 0.72), (0.094, 1.0)], name="bicep_bracer", material="chitin_team", segs=oct_)),
        _facet(_segment_lathe(sh + 0.40 * (el - sh), sh + 0.44 * (el - sh), [(0.102, 0.0), (0.102, 1.0)], name="bracer_seam", material="chitin_dark", segs=oct_)),
        sphere(0.072, u_segs=14, v_segs=10, name="elbow_joint", material="chitin_dark").translate(el),
        _facet(superellipsoid(0.066, 0.052, 0.040, e1=0.28, e2=0.34, u_segs=10, v_segs=6, name="elbow_cop", material="chitin_team").rotate_xyz(rx=0.5, rz=-0.3).translate(el + np.array([0.018, 0.006, -0.060]))),
        # Angular chamfered gauntlet: flared octagonal plates with dark seam bands and a cuff.
        _facet(_segment_lathe(el, wr, [(0.064, 0.0), (0.090, 0.08), (0.108, 0.40), (0.104, 0.80), (0.086, 1.0)], name="gauntlet", material="chitin_team", segs=oct_)),
        _facet(_segment_lathe(el + 0.33 * (wr - el), el + 0.37 * (wr - el), [(0.1095, 0.0), (0.1095, 1.0)], name="gauntlet_seam_0", material="chitin_dark", segs=oct_)),
        _facet(_segment_lathe(el + 0.64 * (wr - el), el + 0.68 * (wr - el), [(0.108, 0.0), (0.108, 1.0)], name="gauntlet_seam_1", material="chitin_dark", segs=oct_)),
        _facet(_segment_lathe(el + 0.94 * (wr - el), el + 1.02 * (wr - el), [(0.092, 0.0), (0.094, 0.5), (0.088, 1.0)], name="gauntlet_cuff", material="chitin_dark", segs=oct_)),
    ]
    for part in parts:
        scene.add_mirrored("arms", part)

    # Steel rivets centered on alternate flats of the octagonal gauntlet near the cuff.
    mat, length = _axis_frame(el, wr)
    for k in range(4):
        ang = (2 * k + 0.5) * math.pi / 4.0
        local_n = np.array([math.sin(ang), 0.0, math.cos(ang)])
        local_p = local_n * (0.1035 * math.cos(math.pi / 8.0)) + np.array([0.0, 0.82 * length, 0.0])
        world_p = (mat @ np.append(local_p, 1.0))[:3]
        world_n = mat[:3, :3] @ local_n
        scene.add_mirrored("arms", _rivet(world_p, world_n, f"gauntlet_rivet_{k}", radius=0.012))

    # Heavy dark clawed fist: blocky palm, three sharp curled claws, and a thumb claw.
    scene.add_mirrored("arms", superellipsoid(0.076, 0.072, 0.080, e1=0.58, e2=0.68, u_segs=16, v_segs=12, name="fist_palm", material="chitin_dark").rotate_xyz(rx=0.25, ry=-0.40, rz=0.12).translate(fi))
    claws = (
        ((0.034, 0.040, 0.050), (0.016, 0.026, 0.104), (-0.018, -0.006, 0.112)),
        ((0.038, -0.002, 0.056), (0.018, -0.018, 0.110), (-0.018, -0.044, 0.108)),
        ((0.032, -0.044, 0.046), (0.012, -0.060, 0.094), (-0.016, -0.074, 0.090)),
    )
    for c_idx, ctrl in enumerate(claws):
        pts = [fi + np.asarray(c) for c in ctrl]
        scene.add_mirrored("arms", curved_tube(pts, [0.024, 0.020, 0.008], samples_per_seg=4, radial_segs=10, cap_rings=2, name=f"fist_claw_{c_idx}", material="chitin_dark"))
    thumb = [fi + np.array(c) for c in ((-0.046, 0.040, 0.024), (-0.070, 0.026, 0.070), (-0.044, -0.004, 0.100))]
    scene.add_mirrored("arms", curved_tube(thumb, [0.024, 0.020, 0.009], samples_per_seg=4, radial_segs=10, cap_rings=2, name="fist_thumb", material="chitin_dark"))


def _tarsus_foot(name: str) -> MeshPart:
    prof = [(0.000, -0.034), (0.022, -0.022), (0.028, 0.006), (0.022, 0.044), (0.012, 0.074), (0.000, 0.090)]
    foot = lathe(prof, radial_segs=6, cap_bottom=False, cap_top=False, axis="z", name=name, material="chitin_dark").scale(1.05, 0.72, 1.0)
    foot = foot.deform(lambda p: np.column_stack([p[:, 0], np.maximum(p[:, 1], -0.015), p[:, 2]]))
    return _facet(foot.translate(0.0, -float(foot.bounds()[0][1]), 0.0))


def _build_leg(scene: SceneBuilder, label: str, hip: Vec3, knee: Vec3, ankle: Vec3, foot_yaw: float) -> None:
    """Sturdy angular dark insect leg: coxa, muscular femur, knee cap, tapered tibia, claw foot."""
    p_hip, p_knee, p_ankle = (np.asarray(v, dtype=np.float64) for v in (hip, knee, ankle))
    femur_mid = 0.5 * (p_hip + p_knee) + np.array([0.0, 0.022, 0.0])
    tibia_mid = 0.45 * p_knee + 0.55 * p_ankle + np.array([0.012, 0.0, 0.0])
    # Knee spur points up-outward, away from the body.
    spur_dir = _normalize(np.array([0.55, 0.75, 0.0]) + 0.35 * _normalize(p_knee - p_hip))
    parts = [
        ellipsoid(0.058, 0.050, 0.058, u_segs=12, v_segs=8, name=f"coxa_{label}", material="chitin_dark").translate(p_hip),
        _facet(swept_tube([p_hip, femur_mid, p_knee], [(0.040, 0.046), (0.050, 0.060), (0.038, 0.042)], radial_segs=6, cap_rings=2, name=f"femur_{label}", material="chitin_dark")),
        _facet(superellipsoid(0.044, 0.048, 0.044, e1=0.40, e2=0.45, u_segs=8, v_segs=6, name=f"knee_{label}", material="chitin_dark").translate(p_knee)),
        _facet(cone(0.022, 0.060, radial_segs=6, name=f"knee_spur_{label}", material="chitin_dark").rotate_axis(np.cross([0.0, 1.0, 0.0], spur_dir), math.acos(float(np.clip(spur_dir[1], -1.0, 1.0)))).translate(p_knee + 0.050 * spur_dir)),
        _facet(swept_tube([p_knee, tibia_mid, p_ankle], [0.040, 0.031, 0.021], radial_segs=6, cap_rings=2, name=f"tibia_{label}", material="chitin_dark")),
        sphere(0.024, u_segs=10, v_segs=6, name=f"ankle_{label}", material="chitin_dark").translate(p_ankle),
        _tarsus_foot(f"tarsus_{label}").rotate_xyz(ry=foot_yaw).translate(p_ankle[0], 0.0, p_ankle[2] + 0.004),
    ]
    for part in parts:
        scene.add_mirrored("legs", part)


def _build_legs(scene: SceneBuilder) -> None:
    _build_leg(scene, "mid", hip=(0.150, 0.430, 0.050), knee=(0.290, 0.420, 0.120), ankle=(0.255, 0.034, 0.190), foot_yaw=0.26)
    _build_leg(scene, "rear", hip=(0.145, 0.405, -0.150), knee=(0.292, 0.410, -0.240), ankle=(0.262, 0.034, -0.300), foot_yaw=math.pi - 0.28)


# ---------------------------------------------------------------------------
# Padded bandoliers
# ---------------------------------------------------------------------------

def _conform_belt_to_torso(pts: np.ndarray, out_normals: np.ndarray, torso_verts: np.ndarray, torso_pivot: np.ndarray, clearance: float = 0.014) -> np.ndarray:
    """Snap closed belt points to sit `clearance` outside the torso shell (zero clipping)."""
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


def _build_bandoliers(scene: SceneBuilder, torso_verts: np.ndarray) -> None:
    """Crossed ribbed padded gold bandoliers over chest AND back with brass X-buckles."""
    belt_ctrl = [
        (0.190, 1.050, -0.085),   # over left shoulder, behind the head
        (0.205, 0.945, 0.150),    # upper-left chest
        (0.112, 0.805, 0.240),    # left pectoral
        (0.000, 0.680, 0.265),    # front X crossing
        (-0.132, 0.572, 0.230),   # lower-right abdomen
        (-0.228, 0.505, 0.110),   # right front hip
        (-0.248, 0.505, -0.070),  # right side hip
        (-0.182, 0.585, -0.250),  # lower-right back
        (0.000, 0.745, -0.305),   # back X crossing
        (0.150, 0.905, -0.275),   # upper-left back
        (0.200, 1.005, -0.165),   # rear left shoulder
    ]
    pivot = np.array([0.0, 0.715, -0.040])
    crossings = {"front": np.array([0.0, 0.680, 0.300]), "back": np.array([0.0, 0.745, -0.340])}

    for belt_idx, sign in enumerate((1.0, -1.0)):
        raw = np.array([(sign * x, y, z) for x, y, z in belt_ctrl], dtype=np.float64)
        pts = catmull_rom_spline(raw, samples_per_seg=6, closed=True)
        out_n = np.array([_normalize(p - pivot + (np.array([0.0, 0.30, 0.0]) if p[1] > 0.93 else 0.0)) for p in pts])
        pts = _conform_belt_to_torso(pts, out_n, torso_verts, pivot, clearance=0.014)
        if belt_idx == 1:
            # Layer the second belt just outside the first where they cross.
            pts = pts + (0.034 * np.exp(-((pts[:, 0] / 0.11) ** 2)))[:, None] * out_n
            for tag, mask in (("front", pts[:, 2] > 0.15), ("back", pts[:, 2] < -0.15)):
                sub = pts[mask]
                hit = sub[int(np.argmin(np.abs(sub[:, 0])))]
                crossings[tag] = hit + np.array([0.0, 0.0, 0.020 if tag == "front" else -0.020])

        n_pts = len(pts)
        scene.add("gear_bandolier", _sculpted_ribbon_mesh(pts, out_n, np.full(n_pts, 0.070), np.full(n_pts, 0.012), closed=True, profile_segs=10, name=f"bandolier_strap_{belt_idx}", material="gear_dark"))

        tangents = np.array([_normalize(pts[(i + 1) % n_pts] - pts[(i - 1) % n_pts]) for i in range(n_pts)])
        surf_n = np.array([_normalize(o - np.dot(o, t) * t) for o, t in zip(out_n, tangents)])
        width_dirs = np.array([_normalize(np.cross(o, t)) for o, t in zip(surf_n, tangents)])

        # Ribbed padded segments along the arc, leaving dark gaps between pads.
        seg_lens = np.linalg.norm(np.roll(pts, -1, axis=0) - pts, axis=1)
        cum = np.concatenate([[0.0], np.cumsum(seg_lens)])
        total = float(cum[-1])
        count = int(total / 0.050)
        for k in range(count):
            s = (k + 0.5) * total / count
            i = int(np.searchsorted(cum, s, side="right") - 1) % n_pts
            j = (i + 1) % n_pts
            f = (s - cum[i]) / max(1e-8, float(seg_lens[i]))
            p = (1.0 - f) * pts[i] + f * pts[j]
            if abs(p[0]) < 0.085 and abs(p[2]) > 0.15:
                continue  # leave room for the X-buckles
            t = _normalize((1.0 - f) * tangents[i] + f * tangents[j])
            o = _normalize((1.0 - f) * surf_n[i] + f * surf_n[j])
            w = _normalize(np.cross(o, t))
            o = np.cross(t, w)
            pad = rounded_box(0.128, 0.034, 0.040, bevel=0.012, segs=2, name=f"bandolier_pad_{belt_idx}_{k}", material="gear_gold")
            scene.add("gear_bandolier", pad.transform(_frame(p + 0.020 * o, w, o, t)))
            if k % 4 == 0:
                scene.add("gear_bandolier", _rivet(p + 0.037 * o, o, f"bandolier_stud_{belt_idx}_{k}", radius=0.011).copy(material="gear_gold_metal"))

    for tag, pos in crossings.items():
        tilt = 14.0 if tag == "front" else -16.0
        face = 1.0 if tag == "front" else -1.0
        frame = rounded_box(0.150, 0.150, 0.034, bevel=0.012, segs=2, name=f"buckle_frame_{tag}", material="gear_gold_metal").rotate_xyz(rx=tilt, rz=45.0, degrees=True).translate(*pos)
        recess = rounded_box(0.094, 0.094, 0.040, bevel=0.008, segs=2, name=f"buckle_recess_{tag}", material="gear_dark").rotate_xyz(rx=tilt, rz=45.0, degrees=True).translate(*pos)
        boss = superellipsoid(0.040, 0.040, 0.022, e1=0.75, e2=0.75, u_segs=14, v_segs=10, name=f"buckle_boss_{tag}", material="gear_brass_trim").rotate_xyz(rx=tilt, degrees=True).translate(pos[0], pos[1], pos[2] + 0.017 * face)
        scene.add("gear_bandolier", [frame, recess, boss])


# ---------------------------------------------------------------------------
# Right-bicep ribbon bow
# ---------------------------------------------------------------------------

def _build_right_bicep_bow(scene: SceneBuilder) -> None:
    """Weathered crimson cloth ribbon tied in a two-loop bow around the RIGHT (-X) bicep."""
    sh, el = np.array([-SHOULDER[0], SHOULDER[1], SHOULDER[2]]), np.array([-ELBOW[0], ELBOW[1], ELBOW[2]])
    center = sh + 0.56 * (el - sh)
    t = _normalize(el - sh)
    out_x = _normalize(np.array([-1.0, 0.0, -0.15]) - np.dot(np.array([-1.0, 0.0, -0.15]), t) * t)
    fwd = _normalize(np.cross(t, out_x))
    out_x = _normalize(np.cross(fwd, t))

    band_r = 0.100  # snug on the octagonal bracer flats
    angs = np.linspace(0.0, 2.0 * math.pi, 32, endpoint=False)
    band_out = np.array([math.cos(a) * out_x + math.sin(a) * fwd for a in angs])
    scene.add("gear_bow", _sculpted_ribbon_mesh(center[None, :] + band_r * band_out, band_out, np.full(32, 0.026), np.full(32, 0.010), closed=True, profile_segs=10, name="bicep_ribbon_band", material="gear_accent"))

    knot = center + (band_r + 0.008) * out_x
    scene.add("gear_bow", superellipsoid(0.028, 0.026, 0.028, e1=0.70, e2=0.76, u_segs=14, v_segs=10, name="bow_knot", material="gear_accent").translate(knot))

    # Loops fan up/down and around the arm (front/back) rather than outward, keeping the width tight.
    up_v, back_v = -t, -fwd
    for sign, tag in ((1.0, "upper"), (-1.0, "lower")):
        loop = [knot, knot + 0.010 * out_x + sign * 0.044 * up_v + 0.036 * back_v, knot + 0.016 * out_x + sign * 0.052 * up_v + 0.010 * back_v, knot + 0.012 * out_x + sign * 0.020 * up_v - 0.026 * back_v, knot + 0.004 * out_x]
        scene.add("gear_bow", curved_tube(loop, [0.012, 0.020, 0.021, 0.017, 0.012], samples_per_seg=4, radial_segs=10, cap_rings=2, name=f"bow_loop_{tag}", material="gear_accent"))

    face = _normalize(0.45 * out_x + 0.78 * fwd + 0.42 * up_v)[None, :]
    for tag, ctrl, widths in (
        ("upper_tail", [knot, knot + 0.006 * out_x + 0.020 * up_v + 0.050 * back_v, knot + 0.010 * out_x + 0.030 * up_v + 0.104 * back_v, knot + 0.010 * out_x + 0.024 * up_v + 0.144 * back_v], [0.013, 0.024, 0.028, 0.014]),
        ("lower_tail", [knot, knot + 0.004 * out_x - 0.030 * up_v + 0.044 * back_v, knot + 0.006 * out_x - 0.062 * up_v + 0.088 * back_v, knot + 0.006 * out_x - 0.084 * up_v + 0.120 * back_v], [0.013, 0.023, 0.026, 0.013]),
    ):
        pts = catmull_rom_spline(ctrl, samples_per_seg=5, closed=False)
        n_t = len(pts)
        w_arr = np.interp(np.linspace(0.0, 1.0, n_t), np.linspace(0.0, 1.0, len(widths)), widths)
        th_arr = np.full(n_t, 0.0095)
        # Notched cloth ends: thin the last sample so the tail tip reads as a cut ribbon.
        th_arr[-1] = 0.006
        scene.add("gear_bow", _sculpted_ribbon_mesh(pts, np.repeat(face, n_t, axis=0), w_arr, th_arr, closed=False, profile_segs=10, name=f"bow_{tag}", material="gear_accent"))


def build_combat(blink: float = 0.0) -> SceneBuilder:
    """Build the armored Combat Ant (`blink` in [0, 1] sets the resting eyelid closure)."""
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
