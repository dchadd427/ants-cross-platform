"""Stylized tabletop/vinyl 3D model for the hulking Combat Ant.

Built directly with `mesh_core.py` (independent of `base_ant`) to capture the
iconic massive gorilla-proportioned brawler silhouette (~1.34 wide x ~1.26 tall):
- Towering hunched trapezius, broad barrel chest, and compact pointed gaster
- Thick curved team-colored gorilla arms ending in chunky dark brawler fists
- Pinched fierce head tucked low/forward with angry brow, squinted eyes,
  clenched mandibles, and stubby dark antennae
- Sturdy wide-planted dark muscular legs with flat tabletop feet at Y = 0.0
- Crossed brass bullet bandoliers over both chest AND back with individual
  metallic-brass cartridges and front/back X-junction buckles
- Signature vivid crimson ribbon bow wrapped around the right bicep (-X side)

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet grounded cleanly at Y = 0.0)
  +Z = front (South / canonical camera 301)
  +X = ant's left (viewer's right from front), -X = ant's right
"""
from __future__ import annotations

import math
from typing import List, Tuple

import numpy as np

try:
    from .mesh_core import (
        MeshPart,
        SceneBuilder,
        catmull_rom_spline,
        curved_tube,
        cylinder,
        ellipsoid,
        lathe,
        recompute_smooth_normals,
        rounded_box,
        sphere,
        superellipsoid,
        torus_arc,
    )
except ImportError:
    from mesh_core import (  # type: ignore
        MeshPart,
        SceneBuilder,
        catmull_rom_spline,
        curved_tube,
        cylinder,
        ellipsoid,
        lathe,
        recompute_smooth_normals,
        rounded_box,
        sphere,
        superellipsoid,
        torus_arc,
    )


def _normalize(v: np.ndarray, fallback: Tuple[float, float, float] = (0.0, 1.0, 0.0)) -> np.ndarray:
    n = float(np.linalg.norm(v))
    if n > 1e-10:
        return v / n
    fb = np.asarray(fallback, dtype=np.float64)
    return fb / float(np.linalg.norm(fb))


def _flat_brawler_foot(name: str) -> MeshPart:
    """Heavy broad tabletop foot pad with a flat sole sitting exactly at Y = 0.0."""
    def flatten(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        p[:, 1] = np.maximum(p[:, 1], -0.036)
        sole = np.clip((-p[:, 1] + 0.012) / 0.048, 0.0, 1.0)
        p[:, 0] *= 1.0 + 0.18 * sole
        p[:, 2] *= 1.0 + 0.14 * sole
        return p

    pad = superellipsoid(0.082, 0.052, 0.106, e1=0.68, e2=0.78, u_segs=18, v_segs=12, name=name, material="chitin_dark")
    pad = pad.deform(flatten)
    bmin, _ = pad.bounds()
    return pad.translate(0.0, -float(bmin[1]), 0.0)


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
    """Sweep a plump beveled vinyl ribbon oriented flush to `out_normals`."""
    n = len(pts)
    tangents = np.zeros((n, 3), dtype=np.float64)
    for i in range(n):
        if closed:
            d = pts[(i + 1) % n] - pts[(i - 1) % n]
        elif i == 0:
            d = pts[1] - pts[0]
        elif i == n - 1:
            d = pts[-1] - pts[-2]
        else:
            d = pts[i + 1] - pts[i - 1]
        tangents[i] = _normalize(d)

    angs = np.linspace(0.0, 2.0 * math.pi, profile_segs, endpoint=False)
    # Superelliptical pill profile: flat broad ribbon faces with soft beveled edges.
    cu = np.sign(np.cos(angs)) * (np.abs(np.cos(angs)) ** 0.65)
    su = np.sign(np.sin(angs)) * (np.abs(np.sin(angs)) ** 0.65)

    rings: List[np.ndarray] = []
    for i in range(n):
        t = tangents[i]
        o = out_normals[i] - np.dot(out_normals[i], t) * t
        o = _normalize(o, (0.0, 0.0, 1.0))
        w = _normalize(np.cross(o, t), (1.0, 0.0, 0.0))
        o = _normalize(np.cross(t, w), (0.0, 1.0, 0.0))
        ring = (
            pts[i][None, :]
            + (half_widths[i] * cu)[:, None] * w[None, :]
            + (half_thicknesses[i] * su)[:, None] * o[None, :]
        )
        rings.append(ring)

    blocks: List[np.ndarray] = []
    start_cap_idx = -1
    end_cap_idx = -1
    if not closed:
        start_cap_idx = 0
        blocks.append((pts[0] - half_thicknesses[0] * tangents[0])[None, :])
    ring_off = len(blocks)
    blocks.extend(rings)
    if not closed:
        end_cap_idx = ring_off + n * profile_segs
        blocks.append((pts[-1] + half_thicknesses[-1] * tangents[-1])[None, :])

    positions = np.vstack(blocks).astype(np.float32)
    tris: List[Tuple[int, int, int]] = []
    if not closed:
        for k in range(profile_segs):
            nk = (k + 1) % profile_segs
            tris.append((start_cap_idx, ring_off + nk, ring_off + k))
    seg_count = n if closed else n - 1
    for r in range(seg_count):
        b0 = ring_off + r * profile_segs
        b1 = ring_off + ((r + 1) % n) * profile_segs
        for k in range(profile_segs):
            nk = (k + 1) % profile_segs
            tris.append((b0 + k, b0 + nk, b1 + nk))
            tris.append((b0 + k, b1 + nk, b1 + k))
    if not closed:
        b_last = ring_off + (n - 1) * profile_segs
        for k in range(profile_segs):
            nk = (k + 1) % profile_segs
            tris.append((end_cap_idx, b_last + k, b_last + nk))

    indices = np.asarray(tris, dtype=np.uint32).reshape(-1)
    part = MeshPart(name, material, positions, recompute_smooth_normals(positions, indices), indices)
    return part.ensure_outward()


def _hunch_deform(pos: np.ndarray) -> np.ndarray:
    """Sculpt broad muscular trapezius shoulders tapering cleanly down the back."""
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    # Broaden upper shoulder shelves slightly and taper lower back smoothly.
    upper = np.clip((y + 0.05) / 0.30, 0.0, 1.0)
    lower = np.clip((-y) / 0.30, 0.0, 1.0)
    p[:, 0] = x * (1.0 + 0.10 * upper - 0.16 * (lower ** 1.4))
    # Subtle arched hunch crest along upper-rear spine (smooth back contour for bandoliers).
    crest = np.exp(-(x / 0.30) ** 2) * np.clip((-z + 0.06) / 0.35, 0.0, 1.0) * upper
    p[:, 1] = y + 0.042 * crest
    p[:, 2] = z - 0.010 * crest
    return p


def _build_torso_and_gaster(scene: SceneBuilder) -> np.ndarray:
    """Build the massive hunched gorilla barrel torso and tucked pointed gaster.

    Returns the concatenated vertex positions of the torso shell so the crossed
    bandoliers can conform flush over chest, shoulders, and back without clipping.
    """
    # Deep muscular dark core filling chest underside, ribs, and waist solidly.
    core = (
        superellipsoid(0.320, 0.270, 0.260, e1=0.78, e2=0.80, u_segs=24, v_segs=16, name="torso_core", material="chitin_dark")
        .translate(0.0, 0.590, -0.020)
    )
    scene.add("body", core)

    # Towering broad trapezius & hunched upper-back armor carapace (chitin_team).
    trapezius = (
        superellipsoid(0.382, 0.270, 0.295, e1=0.72, e2=0.76, u_segs=28, v_segs=20, name="trapezius_hump", material="chitin_team")
        .deform(_hunch_deform)
        .rotate_xyz(rx=0.14)
        .translate(0.0, 0.815, -0.050)
    )
    scene.add("body", trapezius)

    # Broad sculpted pectoral chest armor plates fusing cleanly across the barrel chest.
    chest_main = (
        superellipsoid(0.326, 0.218, 0.172, e1=0.70, e2=0.74, u_segs=24, v_segs=16, name="barrel_chest", material="chitin_team")
        .rotate_xyz(rx=0.10)
        .translate(0.0, 0.655, 0.125)
    )
    scene.add("body", chest_main)

    pec_left = (
        superellipsoid(0.166, 0.138, 0.100, e1=0.74, e2=0.78, u_segs=16, v_segs=12, name="pectoral", material="chitin_team")
        .rotate_xyz(rx=0.16, ry=0.22, rz=-0.08)
        .translate(0.146, 0.690, 0.194)
    )
    pec_l, pec_r = scene.add_mirrored("body", pec_left)

    # Muscular lower abdomen / waist plating (chitin_dark) anchoring hips cleanly.
    pelvis = (
        superellipsoid(0.242, 0.155, 0.212, e1=0.80, e2=0.84, u_segs=20, v_segs=14, name="pelvis_core", material="chitin_dark")
        .translate(0.0, 0.365, -0.090)
    )
    scene.add("body", pelvis)

    # Thick dark muscular neck collar linking hunched shoulders to the tucked head.
    neck = (
        superellipsoid(0.160, 0.130, 0.145, e1=0.82, e2=0.84, u_segs=16, v_segs=12, name="neck_collar", material="chitin_dark")
        .rotate_xyz(rx=0.35)
        .translate(0.0, 0.825, 0.160)
    )
    scene.add("body", neck)

    # Compact pointed team-colored gaster tucked low behind the hips (matching acst701/901).
    gaster_profile = [
        (0.000, 0.000),  # crisp softly rounded point facing -Z
        (0.048, 0.028),
        (0.102, 0.080),
        (0.148, 0.152),
        (0.172, 0.232),
        (0.165, 0.308),
        (0.125, 0.370),
        (0.000, 0.405),
    ]
    gaster = (
        lathe(gaster_profile, radial_segs=22, cap_bottom=False, cap_top=False, axis="z", name="gaster", material="chitin_team")
        .scale(1.06, 0.96, 1.00)
        .rotate_xyz(rx=-0.22)
        .translate(0.0, 0.235, -0.585)
    )
    scene.add("body", gaster)

    # Flush dark chitin segmentation rings on the compact gaster.
    for idx, (z_ring, maj_r) in enumerate([(-0.375, 0.164), (-0.455, 0.130)]):
        dz = z_ring - (-0.585)
        y_ring = 0.235 + dz * math.sin(0.22)
        ring = (
            torus_arc(maj_r, 0.011, u_segs=22, v_segs=8, plane="xy", name=f"gaster_band_{idx}", material="chitin_dark")
            .scale(1.06, 0.96, 1.00)
            .rotate_xyz(rx=-0.22)
            .translate(0.0, y_ring, z_ring)
        )
        scene.add("body", ring)

    torso_verts = np.vstack([
        core.positions,
        trapezius.positions,
        chest_main.positions,
        pec_l.positions,
        pec_r.positions,
        pelvis.positions,
    ]).astype(np.float64)
    return torso_verts


def _fierce_head_deform(pos: np.ndarray) -> np.ndarray:
    """Sculpt a pinched, bulldog-like fierce head with heavy brow shelf and broad jaw."""
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    # Heavy overhanging brow shelf across the top-front.
    brow = np.exp(-((y - 0.085) / 0.068) ** 2) * np.clip((z + 0.02) / 0.22, 0.0, 1.0)
    p[:, 2] = z + 0.036 * brow
    p[:, 0] = x * (1.0 + 0.06 * brow)
    # Broad muscular lower jaw/jowls.
    jaw = np.exp(-((y + 0.085) / 0.085) ** 2) * np.clip((z + 0.05) / 0.24, 0.0, 1.0)
    p[:, 0] = p[:, 0] * (1.0 + 0.12 * jaw)
    p[:, 2] = p[:, 2] + 0.030 * jaw
    # Slightly pinched crown taper so the head sits mean and compact between towering shoulders.
    crown = np.clip((y - 0.10) / 0.12, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.14 * (crown ** 1.5)
    return p


def _build_head_and_face(scene: SceneBuilder) -> None:
    """Build the pinched fierce head tucked low/forward with furrowed brow and stubby antennae."""
    hx, hy, hz = 0.0, 0.870, 0.305

    cranium = (
        superellipsoid(0.222, 0.200, 0.212, e1=0.68, e2=0.74, u_segs=26, v_segs=18, name="cranium", material="chitin_team")
        .deform(_fierce_head_deform)
        .translate(hx, hy, hz)
    )
    scene.add("head", cranium)

    # Heavy angry furrowed brow ridge plates angled sharply inward in a fierce scowl.
    brow_plate = (
        superellipsoid(0.118, 0.036, 0.056, e1=0.70, e2=0.75, u_segs=16, v_segs=10, name="brow_ridge", material="chitin_team")
        .rotate_xyz(rx=-0.06, ry=0.18, rz=0.28)
        .translate(0.090, hy + 0.082, hz + 0.198)
    )
    scene.add_mirrored("head", brow_plate)

    # Thick dark furrowed angry eyebrows right along the scowling ridge (clear above eyes at 50 deg).
    furrow_pts = [
        (0.016, hy + 0.044, hz + 0.238),
        (0.098, hy + 0.078, hz + 0.234),
        (0.178, hy + 0.094, hz + 0.182),
    ]
    brow_bar = curved_tube(
        furrow_pts,
        [0.020, 0.023, 0.017],
        samples_per_seg=4,
        radial_segs=10,
        cap_rings=2,
        name="angry_brow",
        material="chitin_dark",
    )
    scene.add_mirrored("eyes", brow_bar)

    # Expressive squinted brawler eyes tilted slightly upward so they gleam crisply from 50-deg overhead.
    eye_x = 0.102
    eye_y = hy + 0.008
    eye_z = hz + 0.205
    sclera = (
        superellipsoid(0.078, 0.060, 0.052, e1=0.82, e2=0.86, u_segs=18, v_segs=14, name="sclera", material="eye_white")
        .rotate_xyz(rx=-0.26, ry=0.22, rz=0.16)
        .translate(eye_x, eye_y, eye_z)
    )
    scene.add_mirrored("eyes", sclera)

    pupil = (
        ellipsoid(0.038, 0.038, 0.028, u_segs=14, v_segs=10, name="pupil", material="eye_pupil")
        .rotate_xyz(rx=-0.22, ry=0.14)
        .translate(eye_x - 0.009, eye_y + 0.005, eye_z + 0.040)
    )
    scene.add_mirrored("eyes", pupil)

    # Subtle muscular lower-cheek rim accentuating the fierce squinted glare.
    cheek_lid = (
        curved_tube(
            [
                (0.035, eye_y - 0.056, eye_z + 0.024),
                (0.108, eye_y - 0.060, eye_z + 0.022),
                (0.176, eye_y - 0.032, eye_z - 0.016),
            ],
            [0.014, 0.018, 0.014],
            samples_per_seg=3,
            radial_segs=8,
            cap_rings=2,
            name="cheek_rim",
            material="chitin_team",
        )
    )
    scene.add_mirrored("head", cheek_lid)

    # Stout muscular jaws + clenched brawler mandibles.
    jaw_lobe = (
        superellipsoid(0.074, 0.062, 0.078, e1=0.76, e2=0.80, u_segs=14, v_segs=10, name="jaw_lobe", material="chitin_team")
        .rotate_xyz(rx=0.16, ry=-0.18, rz=-0.10)
        .translate(0.098, hy - 0.136, hz + 0.162)
    )
    scene.add_mirrored("head", jaw_lobe)

    mouth = (
        rounded_box(0.130, 0.044, 0.072, bevel=0.016, segs=2, name="mouth_cleft", material="chitin_dark")
        .translate(0.0, hy - 0.140, hz + 0.180)
    )
    scene.add("head", mouth)

    mandible_pts = [
        (0.110, hy - 0.148, hz + 0.182),
        (0.092, hy - 0.172, hz + 0.238),
        (0.034, hy - 0.168, hz + 0.260),
    ]
    mandible = curved_tube(
        mandible_pts,
        [0.035, 0.031, 0.021],
        samples_per_seg=4,
        radial_segs=10,
        cap_rings=3,
        name="mandible_tusk",
        material="chitin_dark",
    )
    scene.add_mirrored("head", mandible)

    # Short, thick, stubby dark antennae rising cleanly from the upper crown.
    ant_base = (0.116, hy + 0.158, hz + 0.015)
    ant_pts = [
        ant_base,
        (0.158, hy + 0.275, hz + 0.025),
        (0.184, hy + 0.352, hz + 0.068),
    ]
    antenna = curved_tube(
        ant_pts,
        [0.034, 0.038, 0.045],
        samples_per_seg=4,
        radial_segs=12,
        cap_rings=3,
        name="stubby_antenna",
        material="chitin_dark",
    )
    scene.add_mirrored("antennae", antenna)

    socket = (
        ellipsoid(0.048, 0.032, 0.048, u_segs=12, v_segs=8, name="antenna_socket", material="chitin_dark")
        .rotate_xyz(rz=-0.25)
        .translate(*ant_base)
    )
    scene.add_mirrored("antennae", socket)


def _build_gorilla_arms(scene: SceneBuilder) -> Tuple[np.ndarray, np.ndarray, float]:
    """Build massive curved team-colored gorilla arms + dark brawler fists.

    Returns `(right_bicep_center, right_bicep_tangent, right_bicep_radius)` for
    mounting the signature right-arm crimson ribbon bow cleanly on the `-X` arm.
    """
    # Left (+X) arm sweep: huge deltoid -> bulging curved bicep -> oversized armored forearm -> wrist.
    arm_pts_left = [
        (0.340, 0.775, 0.000),
        (0.500, 0.650, 0.062),
        (0.528, 0.480, 0.135),
        (0.470, 0.345, 0.250),
        (0.402, 0.290, 0.335),
    ]
    arm_rads = [0.132, 0.130, 0.116, 0.134, 0.095]

    arm_left = curved_tube(
        arm_pts_left,
        arm_rads,
        samples_per_seg=5,
        radial_segs=16,
        cap_rings=4,
        name="gorilla_arm",
        material="chitin_team",
    )
    scene.add_mirrored("arms", arm_left)

    # Massive rounded deltoid shoulder dome bridging trapezius to upper arm.
    deltoid = (
        superellipsoid(0.172, 0.164, 0.164, e1=0.76, e2=0.80, u_segs=20, v_segs=14, name="deltoid", material="chitin_team")
        .rotate_xyz(rz=-0.28)
        .translate(0.382, 0.788, 0.012)
    )
    scene.add_mirrored("arms", deltoid)

    # Bulging armored forearm gauntlet cap for iconic Popeye/gorilla brawler taper.
    forearm_bulge = (
        superellipsoid(0.132, 0.128, 0.150, e1=0.76, e2=0.80, u_segs=18, v_segs=14, name="forearm_gauntlet", material="chitin_team")
        .rotate_xyz(rx=0.55, ry=-0.35, rz=0.18)
        .translate(0.478, 0.360, 0.242)
    )
    scene.add_mirrored("arms", forearm_bulge)

    # Dark inner armpit & elbow muscle crease pads for clean collector-toy contrast.
    armpit = (
        ellipsoid(0.095, 0.098, 0.095, u_segs=14, v_segs=10, name="armpit_joint", material="chitin_dark")
        .translate(0.320, 0.650, 0.020)
    )
    scene.add_mirrored("arms", armpit)

    wrist_cuff = (
        torus_arc(0.096, 0.020, u_segs=20, v_segs=10, plane="xy", name="wrist_cuff", material="chitin_dark")
        .rotate_xyz(rx=0.45, ry=-0.55)
        .translate(0.408, 0.294, 0.326)
    )
    scene.add_mirrored("arms", wrist_cuff)

    # Big sculpted dark fist-claws held wide in a menacing brawler stance (clear above Y = 0).
    fist_ctr = np.array([0.382, 0.268, 0.370], dtype=np.float64)
    palm = (
        superellipsoid(0.104, 0.098, 0.106, e1=0.68, e2=0.72, u_segs=18, v_segs=14, name="fist_palm", material="chitin_dark")
        .rotate_xyz(rx=0.25, ry=-0.45, rz=0.15)
        .translate(fist_ctr)
    )
    scene.add_mirrored("arms", palm)

    # Three chunky curved brawler knuckle fingers + inward-clenched thumb claw.
    finger_offsets = [
        ((0.042, 0.048, 0.068), (0.015, 0.028, 0.118), (-0.030, -0.008, 0.096)),
        ((0.044, -0.004, 0.072), (0.016, -0.022, 0.124), (-0.032, -0.044, 0.098)),
        ((0.036, -0.052, 0.062), (0.012, -0.068, 0.106), (-0.028, -0.074, 0.082)),
    ]
    for f_idx, (p0, p1, p2) in enumerate(finger_offsets):
        f_pts = [fist_ctr + np.array(p0), fist_ctr + np.array(p1), fist_ctr + np.array(p2)]
        finger = curved_tube(
            f_pts,
            [0.033, 0.030, 0.023],
            samples_per_seg=3,
            radial_segs=10,
            cap_rings=2,
            name=f"fist_claw_{f_idx}",
            material="chitin_dark",
        )
        scene.add_mirrored("arms", finger)

    thumb_pts = [
        fist_ctr + np.array([-0.055, 0.052, 0.032]),
        fist_ctr + np.array([-0.084, 0.036, 0.082]),
        fist_ctr + np.array([-0.052, 0.002, 0.116]),
    ]
    thumb = curved_tube(
        thumb_pts,
        [0.034, 0.030, 0.024],
        samples_per_seg=3,
        radial_segs=10,
        cap_rings=2,
        name="fist_thumb",
        material="chitin_dark",
    )
    scene.add_mirrored("arms", thumb)

    # Sample the smooth arm spline at mid-bicep on the RIGHT (-X) arm for the ribbon bow.
    sampled_left = catmull_rom_spline(arm_pts_left, samples_per_seg=10)
    # Index ~14 sits smoothly between deltoid/bicep peak (pt 1) and elbow (pt 2).
    idx = 14
    p_l = sampled_left[idx]
    t_l = _normalize(sampled_left[idx + 1] - sampled_left[idx - 1])
    right_center = np.array([-p_l[0], p_l[1], p_l[2]], dtype=np.float64)
    right_tangent = _normalize(np.array([-t_l[0], t_l[1], t_l[2]], dtype=np.float64))
    right_radius = 0.125
    return right_center, right_tangent, right_radius


def _build_legs(scene: SceneBuilder) -> None:
    """Build four sturdy, wide-planted dark muscular legs with flat tabletop feet."""
    # Front/mid sturdy legs (+X mirrored to -X), tucked inside the sweeping gorilla arms.
    mid_pts = [
        (0.205, 0.420, 0.048),
        (0.320, 0.315, 0.130),
        (0.315, 0.160, 0.165),
        (0.295, 0.062, 0.180),
    ]
    mid_leg = curved_tube(
        mid_pts,
        [0.084, 0.070, 0.060, 0.056],
        samples_per_seg=4,
        radial_segs=14,
        cap_rings=3,
        name="leg_mid",
        material="chitin_dark",
    )
    scene.add_mirrored("legs", mid_leg)
    scene.add_mirrored("legs", sphere(0.088, u_segs=14, v_segs=10, name="hip_mid", material="chitin_dark").translate(*mid_pts[0]))
    scene.add_mirrored("legs", sphere(0.074, u_segs=14, v_segs=10, name="knee_mid", material="chitin_dark").translate(*mid_pts[1]))
    mid_foot = _flat_brawler_foot("foot_mid").rotate_xyz(ry=0.18).translate(0.295, 0.0, 0.200)
    scene.add_mirrored("legs", mid_foot)

    # Rear sturdy legs (+X mirrored to -X) bracing the hunched bulk from behind.
    rear_pts = [
        (0.195, 0.385, -0.145),
        (0.340, 0.305, -0.245),
        (0.330, 0.155, -0.295),
        (0.310, 0.062, -0.315),
    ]
    rear_leg = curved_tube(
        rear_pts,
        [0.086, 0.072, 0.060, 0.056],
        samples_per_seg=4,
        radial_segs=14,
        cap_rings=3,
        name="leg_rear",
        material="chitin_dark",
    )
    scene.add_mirrored("legs", rear_leg)
    scene.add_mirrored("legs", sphere(0.088, u_segs=14, v_segs=10, name="hip_rear", material="chitin_dark").translate(*rear_pts[0]))
    scene.add_mirrored("legs", sphere(0.076, u_segs=14, v_segs=10, name="knee_rear", material="chitin_dark").translate(*rear_pts[1]))
    rear_foot = _flat_brawler_foot("foot_rear").rotate_xyz(ry=-0.22).translate(0.310, 0.0, -0.332)
    scene.add_mirrored("legs", rear_foot)


def _make_bullet_cartridge(
    center: np.ndarray,
    axis_dir: np.ndarray,
    out_normal: np.ndarray,
    idx_label: str,
) -> List[MeshPart]:
    """Build one chunky sculpted metallic-brass bullet cartridge with gold tip and dark loop."""
    a = _normalize(axis_dir)
    o = _normalize(out_normal - np.dot(out_normal, a) * a)
    b = _normalize(np.cross(o, a))

    # Build orthonormal 4x4 transform mapping local +Y -> bullet axis `a`, +Z -> `o`.
    mat = np.eye(4, dtype=np.float64)
    mat[:3, 0] = b
    mat[:3, 1] = a
    mat[:3, 2] = o
    mat[:3, 3] = center

    # Chunky metallic brass cartridge casing with subtle base rim flange.
    casing_prof = [(0.0, -0.058), (0.027, -0.058), (0.027, -0.048), (0.024, -0.046), (0.024, 0.020), (0.0, 0.026)]
    casing = lathe(casing_prof, radial_segs=10, cap_bottom=False, cap_top=False, axis="y", name=f"bullet_case_{idx_label}", material="gear_gold_metal").transform(mat)

    # Tapered warm-gold ogive bullet tip protruding brightly from the brass casing.
    tip_prof = [(0.0, 0.018), (0.022, 0.018), (0.020, 0.040), (0.011, 0.062), (0.0, 0.072)]
    tip = lathe(tip_prof, radial_segs=10, cap_bottom=False, cap_top=False, axis="y", name=f"bullet_tip_{idx_label}", material="gear_gold").transform(mat)

    # Slim dark leather retention band so gleaming brass dominates the silhouette.
    loop = cylinder(0.026, 0.026, 0.016, radial_segs=10, bevel=0.002, axis="y", name=f"bullet_loop_{idx_label}", material="gear_dark").translate(0.0, -0.018, 0.0).transform(mat)
    return [casing, tip, loop]


def _conform_belt_to_torso(
    pts: np.ndarray,
    out_normals: np.ndarray,
    torso_verts: np.ndarray,
    torso_pivot: np.ndarray,
    clearance: float = 0.014,
) -> np.ndarray:
    """Conform closed belt spline points cleanly outside the torso shell with zero clipping."""
    n = len(pts)
    shifts = np.zeros(n, dtype=np.float64)
    v_rel_piv = torso_verts - torso_pivot[None, :]

    for i in range(n):
        p = pts[i]
        o = out_normals[i]
        # Only inspect torso vertices on the same outward hemisphere as this belt point.
        same_side = (v_rel_piv @ o) > 0.05
        sub = torso_verts[same_side]
        if len(sub) == 0:
            continue
        diff = sub - p[None, :]
        proj = diff @ o
        perp = diff - proj[:, None] * o[None, :]
        perp_d2 = np.sum(perp * perp, axis=1)
        near = perp_d2 < (0.095 ** 2)
        if np.any(near):
            max_out = float(np.max(proj[near]))
            shifts[i] = max_out + clearance

    # Smooth radial adjustments gently around the closed loop so the leather drapes naturally.
    for _ in range(3):
        shifts = 0.25 * np.roll(shifts, 1) + 0.50 * shifts + 0.25 * np.roll(shifts, -1)
    # Re-enforce minimum clearance over any local hump peak after smoothing.
    for i in range(n):
        p = pts[i] + shifts[i] * out_normals[i]
        o = out_normals[i]
        same_side = (v_rel_piv @ o) > 0.05
        sub = torso_verts[same_side]
        if len(sub) == 0:
            continue
        diff = sub - p[None, :]
        proj = diff @ o
        perp = diff - proj[:, None] * o[None, :]
        near = np.sum(perp * perp, axis=1) < (0.080 ** 2)
        if np.any(near):
            need = float(np.max(proj[near])) + clearance * 0.85
            if need > 0.0:
                shifts[i] += need

    return pts + shifts[:, None] * out_normals


def _build_bandoliers(scene: SceneBuilder, torso_verts: np.ndarray) -> None:
    """Build crossed brass bullet bandoliers over BOTH chest and back with X-junction buckles."""
    # Closed loop wrapping from left shoulder (+X) -> chest X-center -> right lower hip (-X) -> back X-center.
    belt_a_ctrl = [
        (0.270, 1.065, 0.020),   # over left shoulder crest
        (0.265, 0.880, 0.260),   # upper-left chest
        (0.145, 0.725, 0.318),   # mid-left pectoral
        (0.000, 0.585, 0.328),   # front chest X crossing
        (-0.195, 0.478, 0.258),  # lower-right chest
        (-0.340, 0.435, 0.095),  # right front-hip
        (-0.348, 0.445, -0.105), # right rear-hip
        (-0.228, 0.545, -0.325), # lower-right back
        (0.000, 0.700, -0.395),  # back X crossing (cleanly centered on broad back)
        (0.230, 0.875, -0.365),  # upper-left back over hump
        (0.278, 1.025, -0.195),  # rear left shoulder slope
    ]
    torso_pivot = np.array([0.0, 0.655, -0.035], dtype=np.float64)
    front_cross_pt = np.array([0.0, 0.585, 0.345], dtype=np.float64)
    back_cross_pt = np.array([0.0, 0.700, -0.405], dtype=np.float64)

    for belt_idx, mirror_sign in enumerate((+1.0, -1.0)):
        raw = np.array([(mirror_sign * x, y, z) for x, y, z in belt_a_ctrl], dtype=np.float64)
        pts = catmull_rom_spline(raw, samples_per_seg=6, closed=True)

        # Outward surface normal pointing cleanly away from barrel torso core.
        out_n = np.zeros_like(pts)
        for i in range(len(pts)):
            v = pts[i] - torso_pivot
            if pts[i, 1] > 0.94:
                v = v + np.array([0.0, 0.32, 0.0])
            out_n[i] = _normalize(v)

        pts = _conform_belt_to_torso(pts, out_n, torso_verts, torso_pivot, clearance=0.015)

        # Layer Belt B cleanly just outside Belt A at the front and back X crossings.
        if belt_idx == 1:
            cross_bump = 0.013 * np.exp(-((pts[:, 0] / 0.12) ** 2))
            pts = pts + cross_bump[:, None] * out_n

        # Record actual conformed crossing positions so X-junction buckles seat flush.
        if belt_idx == 1:
            front_mask = pts[:, 2] > 0.15
            if np.any(front_mask):
                f_sub = pts[front_mask]
                front_cross_pt = f_sub[int(np.argmin(np.abs(f_sub[:, 0])))] + np.array([0.0, 0.0, 0.014])
            back_mask = pts[:, 2] < -0.15
            if np.any(back_mask):
                b_sub = pts[back_mask]
                back_cross_pt = b_sub[int(np.argmin(np.abs(b_sub[:, 0])))] + np.array([0.0, 0.0, -0.014])

        n_pts = len(pts)
        half_w = np.full(n_pts, 0.075, dtype=np.float64)
        half_t = np.full(n_pts, 0.014, dtype=np.float64)

        # Thick sculpted dark leather backing strap.
        strap = _sculpted_ribbon_mesh(
            pts,
            out_n,
            half_w,
            half_t,
            closed=True,
            profile_segs=10,
            name=f"bandolier_strap_{belt_idx}",
            material="gear_dark",
        )
        scene.add("gear_bandolier", strap)

        # Compute clean frame vectors for edge rails and bullet cartridges.
        width_dirs = np.zeros_like(pts)
        surf_n = np.zeros_like(pts)
        for i in range(n_pts):
            t = _normalize(pts[(i + 1) % n_pts] - pts[(i - 1) % n_pts])
            o = _normalize(out_n[i] - np.dot(out_n[i], t) * t)
            w = _normalize(np.cross(o, t))
            o = _normalize(np.cross(t, w))
            surf_n[i], width_dirs[i] = o, w

        # Bold golden trim rails framing both edges of each leather strap.
        for edge_sign, edge_tag in ((-1.0, "inner"), (+1.0, "outer")):
            rail_pts = pts + (edge_sign * 0.068) * width_dirs + 0.011 * surf_n
            rail = curved_tube(
                rail_pts[::2],
                0.0125,
                samples_per_seg=2,
                radial_segs=8,
                closed=True,
                name=f"bandolier_rail_{belt_idx}_{edge_tag}",
                material="gear_gold",
            )
            scene.add("gear_bandolier", rail)

        # Distribute chunky gleaming brass bullet cartridges densely by arc length around the full loop.
        diffs = np.roll(pts, -1, axis=0) - pts
        seg_lens = np.linalg.norm(diffs, axis=1)
        cum = np.concatenate([[0.0], np.cumsum(seg_lens)])
        total_len = float(cum[-1])
        bullet_count = 28
        for b_i in range(bullet_count):
            s_target = (b_i + 0.5) * (total_len / bullet_count)
            seg_i = int(np.searchsorted(cum, s_target, side="right") - 1) % n_pts
            s0 = cum[seg_i]
            frac = (s_target - s0) / max(1e-8, float(seg_lens[seg_i]))
            ni = (seg_i + 1) % n_pts
            p = (1.0 - frac) * pts[seg_i] + frac * pts[ni]
            o = _normalize((1.0 - frac) * surf_n[seg_i] + frac * surf_n[ni])
            w = _normalize((1.0 - frac) * width_dirs[seg_i] + frac * width_dirs[ni])

            # Skip directly over the front/back X-junction buckle medallions.
            if abs(p[0]) < 0.072 and (p[2] > 0.20 or p[2] < -0.24):
                continue

            tip_dir = w if (mirror_sign * w[1] + 0.35 * abs(w[0])) >= 0.0 else -w
            if abs(p[1] - 1.0) < 0.16:
                tip_dir = w if np.dot(w, np.array([mirror_sign, 0.2, 0.0])) >= 0.0 else -w

            b_ctr = p + 0.026 * o
            scene.add(
                "gear_bandolier",
                _make_bullet_cartridge(b_ctr, tip_dir, o, f"{belt_idx}_{b_i}"),
            )

    # Sculpted metallic-brass X-junction buckles on BOTH front chest AND back (matching acst701).
    for tag, pos, rx_deg in (
        ("front", (0.0, float(front_cross_pt[1]), float(front_cross_pt[2])), 14.0),
        ("back", (0.0, float(back_cross_pt[1]), float(back_cross_pt[2])), -16.0),
    ):
        outer_plate = (
            rounded_box(0.155, 0.155, 0.038, bevel=0.012, segs=3, name=f"buckle_frame_{tag}", material="gear_gold_metal")
            .rotate_xyz(rx=rx_deg, rz=45.0, degrees=True)
            .translate(*pos)
        )
        inner_recess = (
            rounded_box(0.098, 0.098, 0.044, bevel=0.008, segs=2, name=f"buckle_recess_{tag}", material="gear_dark")
            .rotate_xyz(rx=rx_deg, rz=45.0, degrees=True)
            .translate(*pos)
        )
        z_boss = pos[2] + (0.018 if tag == "front" else -0.018)
        center_boss = (
            superellipsoid(0.042, 0.042, 0.024, e1=0.75, e2=0.75, u_segs=14, v_segs=10, name=f"buckle_boss_{tag}", material="gear_gold")
            .rotate_xyz(rx=rx_deg, degrees=True)
            .translate(pos[0], pos[1], z_boss)
        )
        scene.add("gear_bandolier", [outer_plate, inner_recess, center_boss])


def _build_right_bicep_bow(
    scene: SceneBuilder,
    bicep_center: np.ndarray,
    bicep_tangent: np.ndarray,
    bicep_radius: float,
) -> None:
    """Build the quirky signature crimson ribbon bow wrapped around the RIGHT (-X) bicep."""
    t = _normalize(bicep_tangent)
    lat_hint = np.array([-1.0, 0.0, -0.18], dtype=np.float64)
    out_x = _normalize(lat_hint - np.dot(lat_hint, t) * t)
    fwd_z = _normalize(np.cross(t, out_x))
    out_x = _normalize(np.cross(fwd_z, t))

    # 1. Snug sculpted crimson ribbon band wrapping cleanly around the right bicep.
    n_band = 32
    angs = np.linspace(0.0, 2.0 * math.pi, n_band, endpoint=False)
    band_r = bicep_radius + 0.009
    band_out = np.array([math.cos(a) * out_x + math.sin(a) * fwd_z for a in angs], dtype=np.float64)
    band_pts = bicep_center[None, :] + band_r * band_out
    band_mesh = _sculpted_ribbon_mesh(
        band_pts, band_out, np.full(n_band, 0.024), np.full(n_band, 0.011),
        closed=True, profile_segs=10, name="bicep_ribbon_band", material="gear_accent",
    )
    scene.add("gear_bow", band_mesh)

    # 2. Plump sculpted center knot anchored on the outer-lateral (-X) side of the right bicep.
    knot_pos = bicep_center + (band_r + 0.009) * out_x
    knot = superellipsoid(0.031, 0.029, 0.031, e1=0.76, e2=0.78, u_segs=14, v_segs=10, name="bow_knot", material="gear_accent").translate(knot_pos)
    scene.add("gear_bow", knot)

    up_v, back_v = -t, -fwd_z

    # 3. Neat crisp two-loop sculpted vinyl bow ears fanning symmetrically above and below the knot.
    for sign, tag in ((+1.0, "upper"), (-1.0, "lower")):
        loop_pts = [
            knot_pos,
            knot_pos + 0.026 * out_x + sign * 0.054 * up_v + 0.028 * back_v,
            knot_pos + 0.048 * out_x + sign * 0.062 * up_v + 0.016 * back_v,
            knot_pos + 0.040 * out_x + sign * 0.016 * up_v - 0.016 * back_v,
            knot_pos + 0.006 * out_x,
        ]
        scene.add(
            "gear_bow",
            curved_tube(loop_pts, [0.014, 0.022, 0.023, 0.019, 0.014], samples_per_seg=4, radial_segs=10, cap_rings=2, name=f"bow_loop_{tag}", material="gear_accent"),
        )

    # 4. Two compact perky trailing ribbon tails flaring back-outward (keeps total width ~1.37).
    tail_specs = (
        (
            "upper_tail",
            [
                knot_pos,
                knot_pos + 0.022 * out_x + 0.026 * up_v + 0.048 * back_v,
                knot_pos + 0.042 * out_x + 0.056 * up_v + 0.098 * back_v,
                knot_pos + 0.054 * out_x + 0.068 * up_v + 0.136 * back_v,
            ],
            [0.014, 0.025, 0.029, 0.010],
        ),
        (
            "lower_tail",
            [
                knot_pos,
                knot_pos + 0.020 * out_x - 0.032 * up_v + 0.044 * back_v,
                knot_pos + 0.040 * out_x - 0.068 * up_v + 0.092 * back_v,
                knot_pos + 0.050 * out_x - 0.088 * up_v + 0.128 * back_v,
            ],
            [0.014, 0.024, 0.027, 0.010],
        ),
    )
    for tag, ctrl, widths in tail_specs:
        pts = catmull_rom_spline(ctrl, samples_per_seg=5, closed=False)
        n_t = len(pts)
        w_arr = np.interp(np.linspace(0.0, 1.0, n_t), np.linspace(0.0, 1.0, len(widths)), widths)
        th_arr = np.full(n_t, 0.0105, dtype=np.float64)
        th_arr[-1] = 0.006
        o_arr = np.tile(_normalize(0.45 * out_x + 0.78 * fwd_z + 0.42 * up_v)[None, :], (n_t, 1))
        scene.add("gear_bow", _sculpted_ribbon_mesh(pts, o_arr, w_arr, th_arr, closed=False, profile_segs=10, name=f"bow_{tag}", material="gear_accent"))


def build_combat() -> SceneBuilder:
    """Build the hulking gorilla-proportioned Combat Ant."""
    scene = SceneBuilder(name="ant_combat")
    torso_verts = _build_torso_and_gaster(scene)
    _build_head_and_face(scene)
    right_bicep_center, right_bicep_tangent, right_bicep_radius = _build_gorilla_arms(scene)
    _build_legs(scene)
    _build_bandoliers(scene, torso_verts)
    _build_right_bicep_bow(scene, right_bicep_center, right_bicep_tangent, right_bicep_radius)
    scene.ground_to_y(0.0)
    return scene


def build_ant() -> SceneBuilder:
    """Canonical module entrypoint for `build_all.py`."""
    return build_combat()


__all__ = ["build_ant", "build_combat"]
