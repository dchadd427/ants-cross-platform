"""Realistic 3D sculptors for 1998 Ants backyard toys & props (Wave 1).

Covers:
- `bearhed` (345): Popped-off plush teddy bear head with X-button eye & torn cotton stuffing
- `bearbod` (346): Sprawled plush teddy bear body with tan belly & torn neck stuffing
- `ducky1`  (348): Glossy yellow vinyl rubber bath ducky (3/4 view pose)
- `ducky2`  (349): Glossy yellow vinyl rubber bath ducky (perked side profile)
- `dino`    (351): Molded glossy green plastic Stegosaurus toy dinosaur
- `can1`    (238): Crushed metallic purple/red Critter Cola aluminum soda can
"""
from __future__ import annotations

import math
from typing import List, Tuple

try:
    import numpy as np
except ImportError:  # pragma: no cover - pure-stdlib import safety for CI
    np = None  # type: ignore

if np is not None:
    try:
        from ..mesh_core import (
            MeshPart,
            SceneBuilder,
            curved_tube,
            cylinder,
            ellipsoid,
            ensure_outward_winding,
            lathe,
            recompute_smooth_normals,
            rounded_box,
            sphere,
            superellipsoid,
            swept_tube,
            torus_arc,
        )
        from .item_materials import gradient_y_tint, register_item_materials, tint_part
    except ImportError:
        from map_items.item_materials import gradient_y_tint, register_item_materials, tint_part  # type: ignore
        from mesh_core import (  # type: ignore
            MeshPart,
            SceneBuilder,
            curved_tube,
            cylinder,
            ellipsoid,
            ensure_outward_winding,
            lathe,
            recompute_smooth_normals,
            rounded_box,
            sphere,
            superellipsoid,
            swept_tube,
            torus_arc,
        )


def _finalize_item(scene: SceneBuilder, target_span: float = 0.92) -> SceneBuilder:
    """Normalize an item so its largest horizontal/vertical extent fits nicely and its base sits at Y = 0.0."""
    bmin, bmax = scene.bounds()
    span = float(np.max(bmax - bmin))
    if span > 1e-6:
        s = target_span / span
        scene.scale_all(s)
    bmin, bmax = scene.bounds()
    cx = 0.5 * float(bmin[0] + bmax[0])
    cz = 0.5 * float(bmin[2] + bmax[2])
    scene.translate_all(-cx, -float(bmin[1]), -cz)
    return scene


def _fur_bump(seed: float = 0.0, amp: float = 0.008):
    def _deform(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        r = np.maximum(1e-5, np.linalg.norm(p, axis=1, keepdims=True))
        n = p / r
        ripple = (
            np.sin(p[:, 0] * 34.0 + seed) * np.cos(p[:, 1] * 30.0 - seed)
            + 0.55 * np.sin(p[:, 2] * 42.0 + p[:, 0] * 24.0)
        )
        return p + (amp * ripple)[:, None] * n

    return _deform


def _cotton_bump(seed: float = 0.0, amp: float = 0.015):
    def _deform(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        r = np.maximum(1e-5, np.linalg.norm(p, axis=1, keepdims=True))
        n = p / r
        wisp = (
            np.sin(p[:, 0] * 28.0 + seed) * np.cos(p[:, 1] * 25.0 - seed * 0.7)
            + 0.65 * np.sin(p[:, 2] * 35.0 - p[:, 0] * 21.0 + seed * 1.3)
            + 0.40 * np.cos(p[:, 1] * 48.0 + p[:, 2] * 39.0 - seed * 2.1)
        )
        return p + (amp * wisp)[:, None] * n

    return _deform


def _plush_tint(part: MeshPart, warm_hi: Tuple[float, float, float] = (1.04, 0.98, 0.90), cool_lo: Tuple[float, float, float] = (0.74, 0.66, 0.58)) -> MeshPart:
    def _fn(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
        up = np.clip(0.5 + 0.5 * nrm[:, 1], 0.0, 1.0)[:, None]
        mottle = 0.06 * np.sin(pos[:, 0] * 26.0 + pos[:, 2] * 22.0) * np.cos(pos[:, 1] * 29.0)
        lo = np.asarray(cool_lo, dtype=np.float64)[None, :]
        hi = np.asarray(warm_hi, dtype=np.float64)[None, :]
        return lo * (1.0 - up) + hi * up + mottle[:, None]

    return tint_part(part, _fn)


def _add_ripped_neck_details(
    sc: SceneBuilder,
    node: str,
    prefix: str,
    *,
    center: Tuple[float, float, float],
    axis_out: Tuple[float, float, float],
    radius_u: float,
    radius_v: float,
    flap_count: int = 11,
    thread_count: int = 6,
    seed: float = 0.0,
) -> None:
    """Build a dark hollow ripped fabric opening, jagged outward-curling torn plush flaps, and dangling snapped threads."""
    cx, cy, cz = center
    ax = np.asarray(axis_out, dtype=np.float64)
    ax = ax / max(1e-6, float(np.linalg.norm(ax)))
    ref = np.array([0.0, 1.0, 0.0], dtype=np.float64) if abs(ax[1]) < 0.85 else np.array([0.0, 0.0, 1.0], dtype=np.float64)
    u_vec = np.cross(ref, ax)
    u_vec /= max(1e-6, float(np.linalg.norm(u_vec)))
    v_vec = np.cross(ax, u_vec)
    c_vec = np.array([cx, cy, cz], dtype=np.float64)

    def _to_world(p_local: np.ndarray) -> np.ndarray:
        return c_vec[None, :] + p_local[:, 0:1] * u_vec[None, :] + p_local[:, 1:2] * v_vec[None, :] + p_local[:, 2:3] * ax[None, :]

    # Deep dark recessed hollow interior cavity inside the ripped-off plush shell.
    cavity = ellipsoid(radius_u * 0.94, radius_v * 0.94, 0.036, u_segs=20, v_segs=10, name=f"{prefix}_cavity", material="item_soil_hole").deform(
        lambda p: _to_world(p - np.array([0.0, 0.0, 0.014], dtype=np.float64))
    )
    sc.add(node, cavity)

    # Jagged, uneven outward-curling torn brown plush fabric flaps framing the raw rip.
    for k in range(flap_count):
        ang = (2.0 * math.pi * k / flap_count) + 0.14 * math.sin(k * 2.7 + seed)
        ca, sa = math.cos(ang), math.sin(ang)
        rad_dir = u_vec * ca + v_vec * sa
        tan_dir = -u_vec * sa + v_vec * ca
        base_pt = c_vec + u_vec * (radius_u * 0.96 * ca) + v_vec * (radius_v * 0.96 * sa) - ax * 0.012
        flen = 0.044 + 0.034 * (0.5 + 0.5 * math.sin(k * 3.9 + seed * 1.7))
        flare = 0.022 + 0.026 * (0.5 + 0.5 * math.cos(k * 2.3 + seed))
        skew = 0.014 * math.sin(k * 4.5 - seed)
        p0 = base_pt
        p1 = base_pt + ax * (flen * 0.56) + rad_dir * (flare * 0.45) + tan_dir * (skew * 0.5)
        p2 = base_pt + ax * flen + rad_dir * flare + tan_dir * skew
        w0 = 0.026 + 0.010 * (0.5 + 0.5 * math.cos(k * 5.1 + seed))

        def _flatten_flap(pos: np.ndarray, _mid: np.ndarray = p1, _rdir: np.ndarray = rad_dir) -> np.ndarray:
            rel = pos - _mid[None, :]
            r_proj = np.sum(rel * _rdir[None, :], axis=1, keepdims=True)
            return pos - 0.52 * r_proj * _rdir[None, :]

        flap = curved_tube(
            [tuple(p0), tuple(p1), tuple(p2)],
            [w0, w0 * 0.72, w0 * 0.24],
            samples_per_seg=3,
            radial_segs=8,
            cap_rings=2,
            name=f"{prefix}_flap_{k}",
            material="item_bear_fur",
        ).deform(_flatten_flap)
        sc.add(node, _plush_tint(flap, warm_hi=(0.94, 0.86, 0.76), cool_lo=(0.56, 0.48, 0.40)))

    # Loose snapped dark sewing threads curling out from the ripped seam.
    for k in range(thread_count):
        ang = (2.0 * math.pi * (k + 0.35) / thread_count) + 0.22 * math.cos(k * 3.1 + seed)
        ca, sa = math.cos(ang), math.sin(ang)
        rad_dir = u_vec * ca + v_vec * sa
        tan_dir = -u_vec * sa + v_vec * ca
        base_pt = c_vec + u_vec * (radius_u * 0.90 * ca) + v_vec * (radius_v * 0.90 * sa) + ax * 0.004
        tlen = 0.055 + 0.042 * (0.5 + 0.5 * math.sin(k * 2.9 + seed * 2.1))
        curl_sign = 1.0 if (k % 2 == 0) else -1.0
        pts = [
            tuple(base_pt),
            tuple(base_pt + ax * (tlen * 0.42) + rad_dir * 0.024 + tan_dir * (0.018 * curl_sign)),
            tuple(base_pt + ax * (tlen * 0.82) + rad_dir * 0.042 + tan_dir * (0.036 * curl_sign)),
            tuple(base_pt + ax * (tlen * 0.96) + rad_dir * 0.026 + tan_dir * (0.052 * curl_sign)),
        ]
        thread = curved_tube(
            pts,
            [0.0046, 0.0040, 0.0032, 0.0020],
            samples_per_seg=3,
            radial_segs=6,
            cap_rings=1,
            name=f"{prefix}_thread_{k}",
            material="item_dark_stitch",
        )
        sc.add(node, thread)


def build_bearhed() -> SceneBuilder:
    """Fuzzy ripped-off plush teddy bear head with X-stitch eye, glossy button eye & billowing neck cotton (`345:bearhed`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_bearhed")

    # Plump, soft plush teddy bear cranium with flattened/recessed bottom tear socket (`y < -0.21`).
    def _sculpt_head(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        cheek = 0.034 * np.exp(-((p[:, 1] + 0.07) / 0.14) ** 2) * (p[:, 0] ** 2 / max(1e-4, 0.09))
        fuzz = 0.007 * np.sin(p[:, 0] * 34.0 + p[:, 2] * 29.0) * np.cos(p[:, 1] * 32.0)
        p[:, 0] *= 1.0 + 0.11 * np.exp(-((p[:, 1] + 0.06) / 0.16) ** 2)
        p[:, 2] += cheek + fuzz
        p[:, 1] += fuzz * 0.55
        # Softly indent the bottom neck zone so the ripped socket & pouring cotton seat authentically.
        neck_r = np.hypot(p[:, 0] / 0.16, (p[:, 2] - 0.02) / 0.15)
        bottom_mask = np.clip((-p[:, 1] - 0.17) / 0.10, 0.0, 1.0) * np.clip(1.0 - neck_r * 0.72, 0.0, 1.0)
        p[:, 1] += 0.042 * bottom_mask
        return p

    cranium = _plush_tint(
        superellipsoid(0.305, 0.275, 0.285, e1=0.92, e2=0.94, u_segs=32, v_segs=22, name="bear_cranium", material="item_bear_fur")
        .deform(_sculpt_head),
        warm_hi=(1.06, 0.99, 0.90),
        cool_lo=(0.72, 0.63, 0.54),
    )
    sc.add("bear_head", cranium)

    # Rounded furry plush bear ears with gently cupped darker brown inner hollows (matching `345:bearhed`, zero yellow stickers!).
    for side, tag in ((+1.0, "right"), (-1.0, "left")):
        def _cup_ear(pos: np.ndarray, _s: float = side) -> np.ndarray:
            p = pos.copy()
            r2 = (p[:, 0] / 0.068) ** 2 + (p[:, 1] / 0.066) ** 2
            front = np.clip(p[:, 2] / 0.04, 0.0, 1.0)
            p[:, 2] -= 0.022 * front * np.exp(-r2 * 1.4)
            return p

        ear = _plush_tint(
            ellipsoid(0.112, 0.108, 0.066, u_segs=22, v_segs=14, name=f"bear_ear_{tag}", material="item_bear_fur")
            .deform(_cup_ear)
            .deform(_fur_bump(side * 2.1, 0.006))
            .rotate_xyz(rx=-0.16, ry=side * 0.26, rz=-side * 0.24)
            .translate(side * 0.242, 0.218, -0.018),
            warm_hi=(1.08, 1.00, 0.90),
            cool_lo=(0.68, 0.58, 0.48),
        )
        # Soft shaded fuzzy inner ear hollow (same fur material, warmer/deeper plush tone).
        inner = _plush_tint(
            ellipsoid(0.060, 0.056, 0.024, u_segs=14, v_segs=10, name=f"bear_ear_in_{tag}", material="item_bear_fur")
            .rotate_xyz(rx=-0.16, ry=side * 0.26, rz=-side * 0.24)
            .translate(side * 0.238, 0.214, +0.022),
            warm_hi=(0.76, 0.66, 0.54),
            cool_lo=(0.54, 0.44, 0.34),
        )
        sc.add("bear_head", [ear, inner])

    # Lush velvety cream-tan split plush cheeks + plump chin puff matching `345:bearhed`'s iconic smiling muzzle.
    for side, tag in ((+1.0, "r"), (-1.0, "l")):
        cheek = _plush_tint(
            superellipsoid(0.132, 0.134, 0.124, e1=0.88, e2=0.92, u_segs=24, v_segs=16, name=f"bear_cheek_{tag}", material="item_bear_muzzle")
            .deform(_fur_bump(side * 3.1, 0.005))
            .rotate_xyz(ry=side * 0.22, rz=-side * 0.14)
            .translate(side * 0.092, -0.044, 0.236),
            warm_hi=(1.06, 1.03, 0.94),
            cool_lo=(0.82, 0.75, 0.64),
        )
        sc.add("bear_head", cheek)
    chin_puff = _plush_tint(
        superellipsoid(0.114, 0.094, 0.102, e1=0.88, e2=0.90, u_segs=20, v_segs=14, name="bear_chin", material="item_bear_muzzle")
        .deform(_fur_bump(4.7, 0.005))
        .translate(0.0, -0.142, 0.232),
        warm_hi=(1.05, 1.01, 0.92),
        cool_lo=(0.80, 0.73, 0.62),
    )
    sc.add("bear_head", chin_puff)

    # Glossy rounded-triangular black bear nose nestled high between cheeks + crisp catchlight + tucked subtle smile crease.
    def _sculpt_nose(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        taper = np.clip((0.050 - p[:, 1]) / 0.100, 0.0, 1.0)
        p[:, 0] *= 1.08 - 0.34 * (taper ** 1.2)
        return p

    nose = (
        superellipsoid(0.062, 0.052, 0.046, e1=0.74, e2=0.80, u_segs=20, v_segs=14, name="bear_nose", material="eye_pupil")
        .deform(_sculpt_nose)
        .translate(0.0, 0.068, 0.316)
    )
    nose_glint = sphere(0.011, u_segs=10, v_segs=8, name="bear_nose_glint", material="eye_catchlight").translate(-0.014, 0.074, 0.356)
    # Softly tucked dark cleft right inside the cheek seam (no protruding black stick!).
    mouth_cleft = curved_tube(
        [(0.0, 0.036, 0.312), (0.0, -0.022, 0.314), (0.0, -0.078, 0.298)],
        [0.0075, 0.0080, 0.0060],
        samples_per_seg=3,
        radial_segs=8,
        name="bear_mouth_cleft",
        material="item_dark_stitch",
    )
    sc.add("bear_head", [nose, nose_glint, mouth_cleft])

    # Iconic mismatched eyes from `345:bearhed`: plump embroidered X-thread on left, big glossy black button eye on right!
    x_center = np.array([-0.094, 0.144, 0.256], dtype=np.float64)
    for ang_deg, tag in ((+44.0, "a"), (-44.0, "b")):
        rad = math.radians(ang_deg)
        dx, dy = 0.040 * math.cos(rad), 0.040 * math.sin(rad)
        pts = [
            (x_center[0] - dx, x_center[1] - dy, x_center[2] - 0.008),
            (x_center[0], x_center[1], x_center[2] + 0.006),
            (x_center[0] + dx, x_center[1] + dy, x_center[2] - 0.008),
        ]
        x_bar = curved_tube(pts, [0.0085, 0.0105, 0.0085], samples_per_seg=3, radial_segs=8, cap_rings=2, name=f"bear_x_eye_{tag}", material="item_dark_stitch")
        sc.add("bear_head", x_bar)
    right_eye = sphere(0.044, u_segs=18, v_segs=14, name="bear_button_eye", material="eye_pupil").translate(+0.064, 0.148, 0.264)
    right_glint = sphere(0.011, u_segs=10, v_segs=8, name="bear_eye_glint", material="eye_catchlight").translate(+0.072, 0.158, 0.304)
    sc.add("bear_head", [right_eye, right_glint])

    # Ripped-off bottom neck opening: dark hollow interior cavity, jagged outward-flaring torn plush flaps, and dangling threads!
    _add_ripped_neck_details(
        sc,
        "bear_head",
        "bear_head_rip",
        center=(0.015, -0.240, +0.025),
        axis_out=(0.08, -0.96, 0.26),
        radius_u=0.128,
        radius_v=0.118,
        flap_count=12,
        thread_count=7,
        seed=1.3,
    )

    # Sculpted irregular multi-cluster billowing white cotton stuffing spilling out of the ripped bottom (`345:bearhed`!).
    head_cotton_puffs = (
        # Core dense cotton bursting right out of the dark neck socket (leaving jagged brown torn collar flaps visible around rim!)
        (+0.012, -0.266, +0.036, 0.084, 0.064, 0.078),
        (-0.046, -0.278, +0.076, 0.068, 0.054, 0.064),
        (+0.076, -0.274, +0.068, 0.072, 0.058, 0.068),
        # Three distinct uneven trailing tufts matching `345:bearhed`'s bursting bottom-right cotton silhouette
        (+0.008, -0.304, +0.106, 0.068, 0.054, 0.066),
        (+0.102, -0.304, +0.104, 0.066, 0.050, 0.062),
        (-0.042, -0.312, +0.126, 0.056, 0.044, 0.054),
        (+0.048, -0.328, +0.136, 0.056, 0.042, 0.052),
        (+0.136, -0.284, +0.056, 0.052, 0.042, 0.050),
        (-0.082, -0.264, +0.006, 0.054, 0.044, 0.052),
        (+0.014, -0.278, -0.042, 0.060, 0.046, 0.056),
    )
    for idx, (fx, fy, fz, rx, ry, rz) in enumerate(head_cotton_puffs):
        tuft = _plush_tint(
            ellipsoid(rx, ry, rz, u_segs=16, v_segs=12, name=f"bear_stuffing_{idx}", material="item_cotton_fluff")
            .deform(_cotton_bump(idx * 1.9 + 0.4, 0.014))
            .translate(fx, fy, fz),
            warm_hi=(1.00, 1.00, 0.99),
            cool_lo=(0.82, 0.84, 0.87),
        )
        sc.add("bear_head", tuft)

    # Pulled-apart wispy cotton tendrils stretching out from the ripped neck stuffing.
    head_wisps = (
        [(+0.02, -0.27, +0.07), (+0.06, -0.32, +0.13), (+0.09, -0.35, +0.17)],
        [(+0.07, -0.27, +0.05), (+0.13, -0.31, +0.09), (+0.17, -0.32, +0.10)],
        [(-0.03, -0.28, +0.08), (-0.07, -0.33, +0.13), (-0.10, -0.35, +0.16)],
        [(+0.01, -0.27, -0.01), (+0.03, -0.32, -0.07), (+0.06, -0.34, -0.11)],
    )
    for idx, pts in enumerate(head_wisps):
        wisp = _plush_tint(
            curved_tube(pts, [0.028, 0.020, 0.009], samples_per_seg=3, radial_segs=10, cap_rings=2, name=f"bear_wisp_{idx}", material="item_cotton_fluff")
            .deform(_cotton_bump(idx * 3.1 + 7.2, 0.007)),
            warm_hi=(1.00, 1.00, 0.99),
            cool_lo=(0.84, 0.85, 0.88),
        )
        sc.add("bear_head", wisp)

    # Rest the popped-off head tilted face-upward & counter-clockwise matching `345:bearhed` (right ear high, cotton spilling bottom-right!).
    sc.nodes["bear_head"] = [p.rotate_xyz(rx=-50.0, ry=+8.0, rz=+20.0, degrees=True) for p in sc.nodes["bear_head"]]
    return _finalize_item(sc, target_span=0.92)


def build_bearbod() -> SceneBuilder:
    """Fuzzy sprawled headless plush teddy bear body with bright cream belly & ripped-neck cotton burst (`346:bearbod`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_bearbod")

    # Plump, soft pear-shaped plush bear torso resting flat on its back, tapering into a ripped neck stump at `-Z`.
    def _sculpt_torso(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        neck_t = np.clip((-p[:, 2] - 0.12) / 0.22, 0.0, 1.0)
        p[:, 0] *= 1.0 - 0.30 * (neck_t ** 1.35)
        p[:, 1] *= 1.0 - 0.14 * (neck_t ** 1.5)
        # Flatten the very front of the ripped neck stump so the hollow socket & billowing cotton emerge organically.
        p[:, 2] = np.where(p[:, 2] < -0.295, -0.295 + 0.22 * (p[:, 2] + 0.295), p[:, 2])
        return p

    torso = _plush_tint(
        superellipsoid(0.275, 0.165, 0.315, e1=0.88, e2=0.90, u_segs=30, v_segs=20, name="bear_torso", material="item_bear_fur")
        .deform(_sculpt_torso)
        .deform(_fur_bump(1.1, 0.007))
        .translate(0.0, 0.165, 0.0),
        warm_hi=(1.06, 0.99, 0.90),
        cool_lo=(0.72, 0.63, 0.54),
    )
    # Lush bright sunny creamy-yellow furry oval belly patch matching `346:bearbod` (zero black stitch bricks!).
    belly = _plush_tint(
        superellipsoid(0.184, 0.068, 0.228, e1=0.86, e2=0.88, u_segs=26, v_segs=16, name="bear_belly_patch", material="item_bear_belly")
        .deform(_fur_bump(2.3, 0.004))
        .translate(0.0, 0.280, 0.012),
        warm_hi=(1.06, 1.04, 0.88),
        cool_lo=(0.82, 0.78, 0.62),
    )
    sc.add("bear_body", [torso, belly])

    # Four chubby, thick, shaggy furry plush arms & legs splayed playfully outward matching `346:bearbod` (zero yellow paw stickers!).
    limb_specs = (
        ("arm_l", (-0.19, 0.155, -0.11), (-0.33, 0.140, -0.11), (-0.45, 0.122, -0.05), 0.114, 0.108, 0.098),
        ("arm_r", (+0.19, 0.155, -0.12), (+0.34, 0.140, -0.14), (+0.45, 0.122, -0.10), 0.114, 0.108, 0.098),
        ("leg_l", (-0.16, 0.138, +0.20), (-0.27, 0.125, +0.34), (-0.35, 0.112, +0.46), 0.120, 0.114, 0.105),
        ("leg_r", (+0.16, 0.138, +0.20), (+0.29, 0.125, +0.32), (+0.39, 0.112, +0.43), 0.120, 0.114, 0.105),
    )
    for idx, (tag, p0, p1, p2, r0, r1, r2) in enumerate(limb_specs):
        limb = _plush_tint(
            curved_tube(
                [p0, p1, p2],
                [r0, r1, r2],
                samples_per_seg=4,
                radial_segs=18,
                cap_rings=5,
                name=f"bear_{tag}",
                material="item_bear_fur",
            ).deform(_fur_bump(idx * 1.9, 0.007)),
            warm_hi=(1.06, 0.99, 0.90),
            cool_lo=(0.70, 0.61, 0.52),
        )
        sc.add("bear_body", limb)

    # Ripped-open top neck (`-Z`): dark hollow torn fabric cavity, jagged peeling plush collar flaps, and dangling snapped threads!
    _add_ripped_neck_details(
        sc,
        "bear_body",
        "bear_bod_rip",
        center=(-0.012, 0.180, -0.290),
        axis_out=(-0.04, 0.24, -0.97),
        radius_u=0.122,
        radius_v=0.105,
        flap_count=12,
        thread_count=7,
        seed=4.1,
    )

    # Iconic sculptural double-winged billowing white cotton burst out of the torn neck (`346:bearbod` — distinct V-valley between wings!).
    bod_cotton_puffs = (
        # Core dense cotton wedged right inside the dark torn neck opening (keeps torn flaps & threads clearly visible!)
        (-0.010, 0.192, -0.326, 0.072, 0.062, 0.066),
        (-0.042, 0.174, -0.312, 0.056, 0.048, 0.056),
        (+0.036, 0.182, -0.324, 0.058, 0.050, 0.056),
        # Left pulled-out cotton wing + lower-left puff spilling gently over the ripped collar edge (like `346:bearbod`!)
        (-0.088, 0.200, -0.362, 0.068, 0.056, 0.066),
        (-0.112, 0.214, -0.398, 0.058, 0.048, 0.058),
        (-0.098, 0.186, -0.286, 0.052, 0.046, 0.054),
        # Right lofty double-lobed cotton wing bursting up-right matching `346:bearbod`'s dramatic right plume!
        (+0.068, 0.206, -0.382, 0.072, 0.060, 0.072),
        (+0.104, 0.220, -0.422, 0.064, 0.054, 0.064),
        (+0.046, 0.226, -0.440, 0.058, 0.050, 0.058),
        (+0.108, 0.184, -0.362, 0.052, 0.044, 0.052),
        # Underside fullness so 360-degree side/underneath inspection shows torn cotton stuffing all around the neck rip
        (-0.018, 0.140, -0.356, 0.058, 0.046, 0.056),
        (+0.042, 0.144, -0.368, 0.056, 0.044, 0.054),
    )
    for idx, (fx, fy, fz, rx, ry, rz) in enumerate(bod_cotton_puffs):
        sc.add(
            "bear_body",
            _plush_tint(
                ellipsoid(rx, ry, rz, u_segs=16, v_segs=12, name=f"bear_neck_fluff_{idx}", material="item_cotton_fluff")
                .deform(_cotton_bump(idx * 2.3 + 1.1, 0.014))
                .translate(fx, fy, fz),
                warm_hi=(1.00, 1.00, 0.99),
                cool_lo=(0.82, 0.84, 0.87),
            ),
        )

    # Pulled-apart wispy cotton tendrils trailing off the left & right bursting neck lobes.
    bod_wisps = (
        [(-0.07, 0.20, -0.35), (-0.12, 0.22, -0.41), (-0.15, 0.21, -0.44)],
        [(+0.06, 0.21, -0.38), (+0.11, 0.24, -0.45), (+0.14, 0.23, -0.49)],
        [(+0.03, 0.22, -0.40), (+0.05, 0.25, -0.47), (+0.06, 0.24, -0.51)],
        [(-0.06, 0.19, -0.31), (-0.11, 0.20, -0.25), (-0.13, 0.18, -0.22)],
    )
    for idx, pts in enumerate(bod_wisps):
        wisp = _plush_tint(
            curved_tube(pts, [0.028, 0.020, 0.009], samples_per_seg=3, radial_segs=10, cap_rings=2, name=f"bear_bod_wisp_{idx}", material="item_cotton_fluff")
            .deform(_cotton_bump(idx * 2.9 + 5.4, 0.007)),
            warm_hi=(1.00, 1.00, 0.99),
            cool_lo=(0.84, 0.85, 0.88),
        )
        sc.add("bear_body", wisp)

    # Slightly tilt the sprawled headless body counter-clockwise matching `346:bearbod`.
    sc.nodes["bear_body"] = [p.rotate_xyz(ry=+7.0, degrees=True) for p in sc.nodes["bear_body"]]
    return _finalize_item(sc, target_span=0.96)


def _build_rubber_ducky(name: str, *, yaw_deg: float, head_yaw_deg: float, head_pitch_deg: float) -> SceneBuilder:
    """Tactile satin-matte yellow rubber bath ducky with layered wing feathers & sculpted bill (`348:ducky1` & `349:ducky2`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)

    # Plump bath-ducky hull with weighted base, rounded breast, molded bath seam & perky upturned tail!
    def _sculpt_duck_body(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        rear = np.clip(-p[:, 2] / 0.36, 0.0, 1.0)
        p[:, 1] += 0.255 * (rear ** 2.0)
        p[:, 2] -= 0.065 * (rear ** 2.4)
        p[:, 0] *= 1.0 - 0.74 * (rear ** 1.55)
        front = np.clip(p[:, 2] / 0.34, 0.0, 1.0)
        p[:, 0] *= 1.0 + 0.09 * front * (1.0 - front)
        # Subtle molded rubber seam ridge along center dorsal/tail line for authentic bath-toy realism.
        p[:, 1] += 0.004 * np.exp(-((p[:, 0] / 0.014) ** 2)) * (p[:, 1] > 0.0)
        p[:, 1] = np.where(p[:, 1] < -0.115, -0.115 + 0.18 * (p[:, 1] + 0.115), p[:, 1])
        return p

    body = gradient_y_tint(
        superellipsoid(0.228, 0.170, 0.368, e1=0.90, e2=0.92, u_segs=40, v_segs=28, name="duck_body", material="item_duck_yellow")
        .deform(_sculpt_duck_body)
        .translate(0.0, 0.160, 0.0),
        (0.90, 0.84, 0.68),
        (1.00, 1.00, 0.96),
        noise_amp=0.025,
    )
    sc.add("ducky", body)

    # Layered sculpted bath-ducky wings with 3 bold overlapping curved feather tiers (`348:ducky1`!)
    for side, tag in ((+1.0, "left"), (-1.0, "right")):
        def _sculpt_wing(pos: np.ndarray, s=side) -> np.ndarray:
            p = pos.copy()
            u = np.clip((-p[:, 2] + 0.17) / 0.34, 0.0, 1.0)
            p[:, 1] *= 1.0 - 0.32 * (u ** 1.4)
            p[:, 1] += 0.040 * (u ** 1.8)
            # Crisp molded feather step-grooves & scalloped trailing feather tips!
            feather_groove = 0.012 * np.sin(u * math.pi * 4.0) * np.clip((0.06 - p[:, 1]) / 0.12, 0.0, 1.0)
            p[:, 0] += s * feather_groove * 0.65
            p[:, 1] += 0.011 * np.sin(u * math.pi * 4.0) * (p[:, 1] < 0.01)
            p[:, 0] *= 0.62 + 0.38 * (1.0 - u)
            return p

        wing = gradient_y_tint(
            superellipsoid(0.052, 0.112, 0.175, e1=0.76, e2=0.82, u_segs=26, v_segs=16, name=f"duck_wing_{tag}", material="item_duck_yellow")
            .deform(_sculpt_wing)
            .rotate_xyz(rx=0.14, ry=side * 0.18, rz=-side * 0.16)
            .translate(side * 0.214, 0.205, -0.025),
            (0.86, 0.80, 0.64),
            (1.00, 1.00, 0.96),
        )
        sc.add("ducky", wing)
        # Sculpted overlapping feather-tip curls along the rear edge of each wing (iconic detail in `348:ducky1`!).
        for fk in range(3):
            fz = 0.02 - fk * 0.072
            fy = 0.188 + fk * 0.024
            f_tip = (
                ellipsoid(0.026, 0.036, 0.062, u_segs=14, v_segs=10, name=f"duck_feather_{tag}_{fk}", material="item_duck_yellow")
                .rotate_xyz(rx=0.22 + fk * 0.08, ry=side * 0.22, rz=-side * 0.18)
                .translate(side * (0.226 - fk * 0.020), fy, fz)
            )
            sc.add("ducky", f_tip)

    # Smooth graceful neck bridging chest to round rubber-ducky head.
    neck = curved_tube(
        [(0.0, 0.23, 0.145), (0.0, 0.31, 0.165), (0.0, 0.39, 0.158)],
        [(0.118, 0.115), (0.095, 0.096), (0.105, 0.105)],
        samples_per_seg=4,
        radial_segs=22,
        name="duck_neck",
        material="item_duck_yellow",
    )
    sc.add("ducky", neck)

    # Head assembly with soft cheek/brow contours, molded crest tuft, bill nostrils & smile seam.
    head_parts: List[MeshPart] = []
    cranium = gradient_y_tint(
        superellipsoid(0.164, 0.170, 0.174, e1=0.94, e2=0.96, u_segs=34, v_segs=24, name="duck_head", material="item_duck_yellow")
        .deform(lambda p: np.column_stack([
            p[:, 0] * (1.0 + 0.07 * np.exp(-((p[:, 1] + 0.04) / 0.09) ** 2)),
            p[:, 1] + 0.008 * np.exp(-((p[:, 1] - 0.09) / 0.04) ** 2) * np.clip(p[:, 2] / 0.12, 0.0, 1.0),
            p[:, 2],
        ])),
        (0.92, 0.88, 0.74),
        (1.00, 1.00, 0.98),
    )
    head_parts.append(cranium)
    # Cute little molded top crest tuft (twin soft locks).
    for idx, (cx, cz, rx) in enumerate(((0.0, -0.018, -0.45), (0.014, -0.042, -0.68))):
        crest = (
            ellipsoid(0.026 - 0.004 * idx, 0.044 - 0.006 * idx, 0.056 - 0.008 * idx, u_segs=14, v_segs=10, name=f"duck_crest_{idx}", material="item_duck_yellow")
            .rotate_xyz(rx=rx)
            .translate(cx, 0.168 - 0.008 * idx, cz)
        )
        head_parts.append(crest)

    # Classic wide smiling coral-orange duck bill with subtle bill ridge, nostrils & smile crease.
    def _sculpt_bill(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        u = np.clip((p[:, 2] + 0.09) / 0.18, 0.0, 1.0)
        p[:, 0] *= 0.84 + 0.34 * np.sin(u * math.pi * 0.78)
        p[:, 1] *= 1.0 - 0.44 * (u ** 1.3)
        p[:, 1] += 0.018 * (u ** 2.0)  # cheerful upturned duck smile!
        # Subtle indented bill nostrils near the base!
        for sx in (-0.026, +0.026):
            d_nos = np.hypot(p[:, 0] - sx, p[:, 2] + 0.022)
            p[:, 1] -= 0.006 * np.exp(-((d_nos / 0.012) ** 2)) * (p[:, 1] > 0.0)
        return p

    upper_bill = gradient_y_tint(
        superellipsoid(0.096, 0.034, 0.100, e1=0.76, e2=0.82, u_segs=26, v_segs=16, name="duck_bill_upper", material="item_duck_bill")
        .deform(_sculpt_bill)
        .rotate_xyz(rx=-0.08)
        .translate(0.0, -0.012, 0.198),
        (0.86, 0.76, 0.70),
        (1.04, 0.98, 0.94),
    )
    lower_bill = (
        superellipsoid(0.080, 0.024, 0.082, e1=0.78, e2=0.84, u_segs=20, v_segs=12, name="duck_bill_lower", material="item_duck_bill")
        .rotate_xyz(rx=0.28)
        .translate(0.0, -0.056, 0.180)
    )
    head_parts.extend([upper_bill, lower_bill])

    # Expressive cartoon duck eyes with soft brow rim, crisp white sclera, black pupil, and bright catchlight.
    for side, tag in ((+1.0, "left"), (-1.0, "right")):
        ex, ey, ez = side * 0.105, 0.052, 0.118
        brow = ellipsoid(0.038, 0.016, 0.024, u_segs=14, v_segs=8, name=f"duck_brow_{tag}", material="item_duck_yellow").rotate_xyz(ry=side * 0.52, rz=-side * 0.18).translate(ex * 0.98, ey + 0.044, ez - 0.002)
        sclera = ellipsoid(0.036, 0.044, 0.028, u_segs=16, v_segs=12, name=f"duck_eye_w_{tag}", material="eye_white").rotate_xyz(ry=side * 0.52).translate(ex, ey, ez)
        pupil = ellipsoid(0.022, 0.028, 0.018, u_segs=14, v_segs=10, name=f"duck_eye_p_{tag}", material="eye_pupil").rotate_xyz(ry=side * 0.52).translate(ex + side * 0.012, ey, ez + 0.016)
        glint = sphere(0.007, u_segs=10, v_segs=8, name=f"duck_eye_g_{tag}", material="eye_catchlight").translate(ex + side * 0.020, ey + 0.012, ez + 0.026)
        head_parts.extend([brow, sclera, pupil, glint])

    hx, hy, hz = 0.0, 0.445, 0.158
    for p in head_parts:
        sc.add("ducky", p.rotate_xyz(rx=head_pitch_deg, ry=head_yaw_deg, degrees=True).translate(hx, hy, hz))

    sc.nodes["ducky"] = [p.rotate_xyz(ry=yaw_deg, degrees=True) for p in sc.nodes["ducky"]]
    return _finalize_item(sc, target_span=0.92)


def build_ducky1() -> SceneBuilder:
    return _build_rubber_ducky("item_ducky1", yaw_deg=-42.0, head_yaw_deg=-10.0, head_pitch_deg=4.0)


def build_ducky2() -> SceneBuilder:
    return _build_rubber_ducky("item_ducky2", yaw_deg=-84.0, head_yaw_deg=+14.0, head_pitch_deg=-6.0)


def build_dino() -> SceneBuilder:
    """Textured pebbled-skin Stegosaurus toy dinosaur with ribbed belly, clawed feet, keeled plates & tail spikes (`351:dino`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_dino")

    # Elongated agile Stegosaurus silhouette matching `351:dino`: slender arching neck -> stout arched torso -> sweeping tail!
    spine_pts = [
        (0.0, 0.435, +0.585),  # narrow beak/snout tip
        (0.0, 0.462, +0.495),  # small sculpted head crown
        (0.0, 0.405, +0.390),  # upper neck
        (0.0, 0.300, +0.265),  # neck base
        (0.0, 0.258, +0.135),  # shoulders
        (0.0, 0.300, -0.025),  # arched back crown
        (0.0, 0.248, -0.185),  # hips
        (0.0, 0.172, -0.355),  # thick tail mid
        (0.0, 0.115, -0.510),  # slender tail sweep
        (0.0, 0.072, -0.650),  # pointed tail tip
    ]
    spine_rads = [
        (0.025, 0.022),
        (0.046, 0.042),
        (0.044, 0.046),
        (0.064, 0.066),
        (0.110, 0.116),
        (0.140, 0.154),
        (0.116, 0.122),
        (0.066, 0.066),
        (0.038, 0.038),
        (0.012, 0.012),
    ]

    def _dermal_bump(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        # Subtle reptilian flank osteoderm ripples & neck skin folds so body reads organic, never smooth plastic!
        fold = 0.005 * np.sin(p[:, 2] * 38.0) * np.exp(-((p[:, 0] / 0.12) ** 2))
        peb = 0.004 * np.sin(p[:, 0] * 48.0 + p[:, 2] * 44.0) * np.cos(p[:, 1] * 46.0)
        p[:, 0] += peb * np.sign(p[:, 0])
        p[:, 1] += fold + peb * 0.6
        return p

    body = gradient_y_tint(
        curved_tube(spine_pts, spine_rads, samples_per_seg=5, radial_segs=24, cap_rings=4, name="dino_body", material="item_toy_green")
        .deform(_dermal_bump),
        (0.62, 0.84, 0.72),
        (1.00, 1.00, 0.94),
        noise_amp=0.06,
    )
    sc.add("dino", body)

    # Pale ribbed reptilian underbelly running under chest, torso & upper tail (`item_dino_belly`)!
    belly_pts = [
        (0.0, 0.242, +0.245),
        (0.0, 0.175, +0.120),
        (0.0, 0.172, -0.025),
        (0.0, 0.165, -0.175),
        (0.0, 0.130, -0.320),
    ]
    belly_rads = [(0.044, 0.026), (0.088, 0.042), (0.106, 0.046), (0.084, 0.038), (0.042, 0.022)]
    belly = (
        curved_tube(belly_pts, belly_rads, samples_per_seg=4, radial_segs=16, cap_rings=2, name="dino_belly", material="item_dino_belly")
        .deform(lambda p: np.column_stack([p[:, 0], p[:, 1] - 0.006 * np.sin(p[:, 2] * 55.0), p[:, 2]]))
    )
    sc.add("dino", belly)

    # Sculpted lower jaw, brow ridges & tiny flared nostrils on the snout!
    jaw = ellipsoid(0.028, 0.016, 0.056, u_segs=14, v_segs=10, name="dino_jaw", material="item_dino_belly").rotate_xyz(rx=0.14).translate(0.0, 0.422, +0.538)
    sc.add("dino", jaw)
    for side, tag in ((+1.0, "l"), (-1.0, "r")):
        sc.add("dino", [
            sphere(0.013, u_segs=12, v_segs=8, name=f"dino_eye_{tag}", material="eye_pupil").translate(side * 0.038, 0.468, +0.512),
            sphere(0.004, u_segs=8, v_segs=6, name=f"dino_glint_{tag}", material="eye_catchlight").translate(side * 0.046, 0.474, +0.520),
            ellipsoid(0.010, 0.008, 0.018, u_segs=10, v_segs=6, name=f"dino_brow_{tag}", material="item_toy_green_dark").translate(side * 0.034, 0.482, +0.510),
            sphere(0.006, u_segs=8, v_segs=6, name=f"dino_nostril_{tag}", material="item_toy_green_dark").translate(side * 0.016, 0.446, +0.578),
        ])

    # Four sturdy elephantine dinosaur legs with flared ankle pads & 3 sculpted ivory toenails/claws per foot!
    for tag, sx, sz, top_y, rad in (
        ("fl", +0.118, +0.140, 0.22, 0.052),
        ("fr", -0.118, +0.140, 0.22, 0.052),
        ("rl", +0.128, -0.165, 0.23, 0.060),
        ("rr", -0.128, -0.165, 0.23, 0.060),
    ):
        fx, fz = sx * 1.18, sz + 0.022
        leg = curved_tube(
            [(sx * 0.72, top_y, sz), (sx * 1.04, top_y * 0.50, sz + 0.012), (fx, 0.032, fz)],
            [rad * 1.08, rad * 0.86, rad * 1.02],
            samples_per_seg=3,
            radial_segs=16,
            cap_rings=3,
            name=f"dino_leg_{tag}",
            material="item_toy_green",
        ).deform(lambda p: np.column_stack([p[:, 0], np.maximum(p[:, 1], 0.0), p[:, 2]]))
        sc.add("dino", leg)
        # 3 short blunt dinosaur toenails hugging the front edge of each foot pad!
        for tk, da in enumerate((-0.42, 0.0, +0.42)):
            cx = fx + (rad * 0.85) * math.sin(da)
            cz = fz + (rad * 0.92) * math.cos(da)
            toe = ellipsoid(0.014, 0.014, 0.020, u_segs=10, v_segs=6, name=f"dino_claw_{tag}_{tk}", material="item_dino_claw").rotate_xyz(ry=da).translate(cx, 0.016, cz)
            sc.add("dino", toe)

    # Row of sculpted dermal osteoderm scale nodules running down both flanks!
    for side, tag in ((+1.0, "l"), (-1.0, "r")):
        for k, (bz, by, br) in enumerate(((+0.12, 0.27, 0.112), (+0.02, 0.30, 0.136), (-0.08, 0.29, 0.132), (-0.18, 0.25, 0.114), (-0.28, 0.20, 0.088))):
            sc.add("dino", ellipsoid(0.016, 0.018, 0.022, u_segs=10, v_segs=8, name=f"dino_scute_{tag}_{k}", material="item_toy_green_dark").translate(side * br, by, bz))

    # TWO dramatic alternating rows of oversized keeled diamond dorsal plates (`351:dino`)!
    def _diamond_plate(ph: float, pl: float, name: str) -> MeshPart:
        def _pinch(pos: np.ndarray) -> np.ndarray:
            p = pos.copy()
            u = np.clip((p[:, 1] + ph) / (2.0 * ph), 0.0, 1.0)
            p[:, 2] *= 1.0 - 0.88 * (u ** 1.32)
            # Raised vertical central bony keel ridge along each plate!
            keel = 1.0 + 0.35 * np.exp(-((p[:, 2] / (pl * 0.18)) ** 2)) * (1.0 - u)
            p[:, 0] *= (1.0 - 0.72 * (u ** 1.18)) * keel
            return p

        return gradient_y_tint(
            superellipsoid(0.022, ph, pl * 0.62, e1=0.52, e2=0.54, u_segs=16, v_segs=12, name=name, material="item_toy_green_dark").deform(_pinch),
            (0.76, 0.88, 0.80),
            (1.06, 1.06, 0.96),
        )

    plate_specs = (
        (+0.018, 0.445, +0.345, 0.072, 0.074, +0.42),
        (-0.020, 0.398, +0.255, 0.102, 0.104, +0.25),
        (+0.024, 0.425, +0.145, 0.136, 0.132, +0.08),
        (-0.026, 0.468, +0.025, 0.158, 0.148, -0.04),
        (+0.026, 0.458, -0.095, 0.150, 0.140, -0.14),
        (-0.022, 0.390, -0.215, 0.128, 0.122, -0.28),
        (+0.018, 0.308, -0.325, 0.102, 0.098, -0.38),
        (-0.014, 0.224, -0.430, 0.076, 0.074, -0.46),
        (+0.012, 0.156, -0.525, 0.052, 0.052, -0.52),
    )
    for idx, (px, py, pz, ph, pl, pitch) in enumerate(plate_specs):
        plate = (
            _diamond_plate(ph, pl, f"dino_plate_{idx}")
            .rotate_xyz(rx=pitch, rz=0.18 if px > 0 else -0.18)
            .translate(px, py, pz)
        )
        sc.add("dino", plate)

    # Iconic Stegosaurus paired tail spikes (thagomizer) near the tail tip!
    for side, tag in ((+1.0, "l"), (-1.0, "r")):
        for idx, (tz, ty, slen) in enumerate(((-0.565, 0.098, 0.078), (-0.615, 0.082, 0.062))):
            spike = (
                superellipsoid(0.012, slen * 0.5, 0.012, e1=0.6, e2=0.8, u_segs=10, v_segs=8, name=f"dino_spike_{tag}_{idx}", material="item_dino_claw")
                .deform(lambda p, sl=slen: np.column_stack([p[:, 0] * (1.0 - 0.82 * np.clip((p[:, 1] + sl * 0.5) / sl, 0.0, 1.0)), p[:, 1], p[:, 2] * (1.0 - 0.82 * np.clip((p[:, 1] + sl * 0.5) / sl, 0.0, 1.0))]))
                .rotate_xyz(rx=-0.65, rz=-side * 1.05)
                .translate(side * 0.036, ty + 0.012, tz)
            )
            sc.add("dino", spike)

    # Angle matching `351:dino`: neck pointing upper-left, pointed tail sweeping lower-right!
    sc.nodes["dino"] = [p.rotate_xyz(ry=-145.0, degrees=True) for p in sc.nodes["dino"]]
    return _finalize_item(sc, target_span=0.96)


def build_soda_can_lid_parts(
    prefix: str,
    *,
    lid_dx: float = 0.0,
    lid_dy: float = 0.0,
    lid_z: float = 0.305,
    tab_angle_deg: float = -22.0,
) -> List[MeshPart]:
    """High-detail open aluminum soda can lid: stamped score ring, big dark teardrop drink hole & figure-8 pull-tab."""
    rad = math.radians(tab_angle_deg)
    parts: List[MeshPart] = []
    # Recessed inner countersink score ring + subtle stamped outer chime groove.
    score_ring = torus_arc(0.114, 0.0038, u_segs=42, v_segs=6, plane="xy", name=f"{prefix}_score_ring", material="item_steel_dark")
    inner_bevel = torus_arc(0.124, 0.0055, u_segs=44, v_segs=8, plane="xy", name=f"{prefix}_inner_bevel", material="item_aluminum")
    # Large authentic punched teardrop/rounded-D drink opening (~1/3 of lid diameter!) with raised lip & dark recess.
    hole_dark = superellipsoid(0.056, 0.044, 0.011, e1=0.62, e2=0.72, u_segs=24, v_segs=10, name=f"{prefix}_hole", material="item_soil_hole").translate(-0.054, 0.000, -0.002)
    # Pushed-in aluminum lid flap glinting deep inside the punched mouth.
    flap_in = rounded_box(0.046, 0.042, 0.005, bevel=0.002, segs=2, name=f"{prefix}_flap", material="item_steel_dark").rotate_xyz(ry=0.45).translate(-0.058, 0.000, -0.009)
    # Gleaming raised rolled rim framing the punched drink mouth (built as sculpted closed tube for oval teardrop fit).
    lip_pts = [
        (-0.054 + 0.058 * math.cos(2.0 * math.pi * i / 24), 0.046 * math.sin(2.0 * math.pi * i / 24), +0.003)
        for i in range(25)
    ]
    hole_rim = curved_tube(lip_pts, [0.0052] * len(lip_pts), samples_per_seg=1, radial_segs=8, cap_start=False, cap_end=False, name=f"{prefix}_hole_rim", material="item_aluminum")

    # Contoured stay-on figure-8 aluminum pull-tab: central rivet + stamped twin rails + wide D-shaped finger loop!
    rivet_base = cylinder(0.022, 0.024, 0.008, radial_segs=18, bevel=0.002, axis="z", name=f"{prefix}_rivet_base", material="item_aluminum").translate(+0.004, 0.0, +0.006)
    rivet_pin = cylinder(0.012, 0.013, 0.013, radial_segs=16, bevel=0.002, axis="z", name=f"{prefix}_rivet_pin", material="item_chrome_spring").translate(+0.004, 0.0, +0.009)
    # Front arched nose of the pull-tab pressing over the edge of the punched drink hole.
    nose_pts = [
        (+0.006, +0.026, +0.009),
        (-0.018, +0.022, +0.010),
        (-0.028, 0.000, +0.010),
        (-0.018, -0.022, +0.010),
        (+0.006, -0.026, +0.009),
    ]
    tab_nose = curved_tube(nose_pts, [0.0075] * len(nose_pts), samples_per_seg=3, radial_segs=8, cap_rings=1, name=f"{prefix}_tab_nose", material="item_aluminum")
    # Central stamped twin bridge rails framing the rivet cutout.
    bridge_top = rounded_box(0.064, 0.013, 0.007, bevel=0.002, segs=2, name=f"{prefix}_tab_rail_t", material="item_aluminum").translate(+0.030, +0.025, +0.010)
    bridge_bot = rounded_box(0.064, 0.013, 0.007, bevel=0.002, segs=2, name=f"{prefix}_tab_rail_b", material="item_aluminum").translate(+0.030, -0.025, +0.010)
    bridge_mid = rounded_box(0.022, 0.056, 0.0065, bevel=0.002, segs=2, name=f"{prefix}_tab_mid", material="item_aluminum").translate(+0.046, 0.0, +0.010)
    # Wide rounded-D rear finger-pull loop with crisp open center window!
    loop_pts = [
        (+0.048, +0.028, +0.011),
        (+0.078, +0.036, +0.013),
        (+0.104, +0.022, +0.015),
        (+0.110, 0.000, +0.016),
        (+0.104, -0.022, +0.015),
        (+0.078, -0.036, +0.013),
        (+0.048, -0.028, +0.011),
    ]
    tab_loop = curved_tube(loop_pts, [0.0082] * len(loop_pts), samples_per_seg=3, radial_segs=8, cap_rings=1, name=f"{prefix}_pull_loop", material="item_aluminum")
    # Dark shadow disks directly under the rivet & finger-pull cutouts so the openings read razor-sharp!
    cut_riv = cylinder(0.016, 0.016, 0.004, radial_segs=16, axis="z", name=f"{prefix}_cut_riv", material="item_steel_dark").translate(+0.024, 0.0, +0.002)
    cut_finger = superellipsoid(0.024, 0.022, 0.004, e1=0.75, e2=0.75, u_segs=18, v_segs=6, name=f"{prefix}_cut_finger", material="item_steel_dark").translate(+0.076, 0.0, +0.002)

    local_parts = [score_ring, inner_bevel, hole_dark, flap_in, hole_rim, rivet_base, rivet_pin, tab_nose, bridge_top, bridge_bot, bridge_mid, tab_loop, cut_riv, cut_finger]
    for p in local_parts:
        parts.append(p.rotate_xyz(rz=rad).translate(lid_dx, lid_dy, lid_z))
    return parts


def build_can1() -> SceneBuilder:
    """Authentically crushed Critter Cola aluminum can with 512px printed label & figure-8 pull-tab (`238:can1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_can1")

    def _crush_can(pos: np.ndarray) -> np.ndarray:
        """Curved C-arch stepped-on soda can: smooth outward-bowed front belly + sharp upper-back dent (`238:can1`)."""
        p = pos.copy()
        z = p[:, 2]
        mid_w = np.exp(-(((z + 0.005) / 0.165) ** 2))
        end_guard = np.clip((0.292 - np.abs(z)) / 0.055, 0.0, 1.0)
        crush = mid_w * end_guard

        # Localized sharp stomp collapse along the upper-back right edge right behind 'Critter' (`x > 0, y > 0`),
        # leaving the lower-front belly smoothly curved outward like a subtle arched can (`sprite_0372.png`)!
        back_top = np.clip((p[:, 1] + 0.05) / 0.20, 0.0, 1.0) * np.clip((p[:, 0] + 0.08) / 0.22, 0.0, 1.0)
        crease_a = 1.0 - np.abs(np.sin(p[:, 0] * 13.5 + z * 12.0))
        crease_b = 1.0 - np.abs(np.sin(p[:, 0] * 18.0 - z * 14.0 + 0.7))
        dent = 0.068 * crush * back_top * (0.70 + 0.18 * crease_a + 0.12 * crease_b)
        p[:, 1] -= dent
        p[:, 0] -= 0.024 * crush * back_top * (0.72 + 0.28 * crease_a)

        # Gentle realistic C-bow (NOT extreme macaroni bend!) keeping both aluminum rims crisp & round (`sprite_0372.png`)!
        p[:, 0] *= 1.0 + 0.09 * crush
        p[:, 1] += 0.022 * ((z / 0.285) ** 2) - 0.008 * crush
        p[:, 0] += 0.026 * ((z / 0.285) ** 2) - 0.018 * crush
        return p

    # High-resolution seam-free cylindrical barrel mapped cleanly to 512x512 `cola_label` texture!
    n_rings = 40
    n_rad = 72
    z_lo, z_hi = -0.285, +0.295
    verts: List[Tuple[float, float, float]] = []
    uvs: List[Tuple[float, float]] = []
    tris: List[Tuple[int, int, int]] = []
    for ir in range(n_rings + 1):
        vf = ir / float(n_rings)
        z = z_lo + (z_hi - z_lo) * vf
        if vf < 0.08:
            rad = 0.156 + 0.012 * (vf / 0.08)
        elif vf > 0.90:
            t_neck = (vf - 0.90) / 0.10
            rad = 0.168 - 0.024 * (t_neck ** 1.35)
        else:
            rad = 0.168
        tex_v = 1.0 - vf  # v = 0 near open pull-tab lid (+Z), v = 1 near silver bottom rim (-Z)
        for j in range(n_rad + 1):
            uf = j / float(n_rad)
            # Align 'Critter Cola' diagonally across top-front flank, bugs & white barcode on left (`sprite_0372.png`)!
            ang = -math.pi * 0.5 + uf * 2.0 * math.pi + 0.56
            verts.append((rad * math.cos(ang), rad * math.sin(ang), z))
            uvs.append((uf, tex_v))
    stride = n_rad + 1
    for ir in range(n_rings):
        for j in range(n_rad):
            a = ir * stride + j
            b = (ir + 1) * stride + j
            c = (ir + 1) * stride + (j + 1)
            d = ir * stride + (j + 1)
            tris.append((a, c, b))
            tris.append((a, d, c))
    pos_b = _crush_can(np.asarray(verts, dtype=np.float32))
    idx_b = np.asarray(tris, dtype=np.uint32).reshape(-1)
    nrm_b = recompute_smooth_normals(pos_b, idx_b)
    body_shell = MeshPart("can_label_wall", "item_cola_label", pos_b, nrm_b, idx_b, uvs=np.asarray(uvs, dtype=np.float32))
    sc.add("can", body_shell)

    # Brushed metallic silver double-rolled aluminum top lid & concave bottom heel.
    top_end = lathe(
        [
            (0.144, +0.295),
            (0.151, +0.312),
            (0.146, +0.327),
            (0.132, +0.329),
            (0.125, +0.307),
            (0.110, +0.305),
            (0.000, +0.304),
        ],
        radial_segs=48,
        cap_bottom=False,
        cap_top=False,
        axis="z",
        name="can_top_lid",
        material="item_aluminum",
    ).deform(_crush_can)
    bot_end = lathe(
        [(0.000, -0.308), (0.098, -0.306), (0.128, -0.324), (0.146, -0.318), (0.156, -0.285)],
        radial_segs=44,
        cap_bottom=False,
        cap_top=False,
        axis="z",
        name="can_bottom",
        material="item_aluminum",
    ).deform(_crush_can)
    lid_dy = 0.022 * ((0.305 / 0.285) ** 2)
    lid_dx = 0.026 * ((0.305 / 0.285) ** 2)
    lid_parts = build_soda_can_lid_parts("can", lid_dx=lid_dx, lid_dy=lid_dy, lid_z=+0.306, tab_angle_deg=-16.0)
    sc.add("can", [top_end, bot_end, *lid_parts])

    # Angle crushed can diagonally with the open pull-tab lid facing prominently forward-right like `sprite_0372.png`!
    sc.nodes["can"] = [p.rotate_xyz(rx=-8.0, ry=40.0, rz=-14.0, degrees=True) for p in sc.nodes["can"]]
    return _finalize_item(sc, target_span=0.96)
