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


def _plush_tint(part: MeshPart, warm_hi: Tuple[float, float, float] = (1.04, 0.98, 0.90), cool_lo: Tuple[float, float, float] = (0.74, 0.66, 0.58)) -> MeshPart:
    def _fn(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
        up = np.clip(0.5 + 0.5 * nrm[:, 1], 0.0, 1.0)[:, None]
        mottle = 0.06 * np.sin(pos[:, 0] * 26.0 + pos[:, 2] * 22.0) * np.cos(pos[:, 1] * 29.0)
        lo = np.asarray(cool_lo, dtype=np.float64)[None, :]
        hi = np.asarray(warm_hi, dtype=np.float64)[None, :]
        return lo * (1.0 - up) + hi * up + mottle[:, None]

    return tint_part(part, _fn)


def build_bearhed() -> SceneBuilder:
    """Popped-off plush brown teddy bear head with X-stitch eye, tan muzzle & cotton stuffing (`345:bearhed`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_bearhed")

    # Plump, slightly squashed plush teddy bear cranium tilted playfully face-up/forward like `345:bearhed`.
    def _sculpt_head(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        cheek = 0.032 * np.exp(-((p[:, 1] + 0.07) / 0.14) ** 2) * (p[:, 0] ** 2 / max(1e-4, 0.09))
        seam_groove = -0.008 * np.exp(-((p[:, 0] / 0.022) ** 2)) * np.clip(p[:, 1] / 0.24, 0.0, 1.0)
        fuzz = 0.007 * np.sin(p[:, 0] * 36.0 + p[:, 2] * 31.0) * np.cos(p[:, 1] * 34.0)
        p[:, 0] *= 1.0 + 0.10 * np.exp(-((p[:, 1] + 0.06) / 0.16) ** 2)
        p[:, 2] += cheek + seam_groove + fuzz
        p[:, 1] += seam_groove + fuzz * 0.6
        return p

    cranium = _plush_tint(
        superellipsoid(0.30, 0.27, 0.28, e1=0.92, e2=0.94, u_segs=32, v_segs=22, name="bear_cranium", material="item_bear_fur")
        .deform(_sculpt_head)
    )
    sc.add("bear_head", cranium)

    # Rounded plush bear ears with recessed lighter inner-ear pads.
    for side, tag in ((+1.0, "left"), (-1.0, "right")):
        ear = _plush_tint(
            ellipsoid(0.105, 0.102, 0.062, u_segs=20, v_segs=14, name=f"bear_ear_{tag}", material="item_bear_fur")
            .deform(_fur_bump(side * 2.1, 0.006))
            .rotate_xyz(rx=-0.18, ry=side * 0.28, rz=-side * 0.26)
            .translate(side * 0.235, 0.215, -0.020)
        )
        inner = _plush_tint(
            ellipsoid(0.062, 0.060, 0.028, u_segs=16, v_segs=10, name=f"bear_ear_in_{tag}", material="item_bear_muzzle")
            .rotate_xyz(rx=-0.18, ry=side * 0.28, rz=-side * 0.26)
            .translate(side * 0.232, 0.210, +0.024),
            warm_hi=(1.02, 0.96, 0.88),
            cool_lo=(0.82, 0.74, 0.64),
        )
        sc.add("bear_head", [ear, inner])

    # Lush cream-tan split plush cheeks + plump chin puff matching `345:bearhed`'s big smiling teddy muzzle!
    for side, tag in ((+1.0, "l"), (-1.0, "r")):
        cheek = _plush_tint(
            superellipsoid(0.118, 0.122, 0.118, e1=0.88, e2=0.92, u_segs=24, v_segs=16, name=f"bear_cheek_{tag}", material="item_bear_muzzle")
            .deform(_fur_bump(side * 3.1, 0.005))
            .rotate_xyz(ry=side * 0.24, rz=-side * 0.14)
            .translate(side * 0.092, -0.052, 0.238),
            warm_hi=(1.06, 1.01, 0.92),
            cool_lo=(0.82, 0.74, 0.62),
        )
        sc.add("bear_head", cheek)
    chin_puff = _plush_tint(
        superellipsoid(0.105, 0.082, 0.095, e1=0.88, e2=0.90, u_segs=20, v_segs=14, name="bear_chin", material="item_bear_muzzle")
        .deform(_fur_bump(4.7, 0.005))
        .translate(0.0, -0.142, 0.228),
        warm_hi=(1.05, 1.00, 0.90),
        cool_lo=(0.80, 0.72, 0.60),
    )
    sc.add("bear_head", chin_puff)

    # Big velvety black stitched plush nose resting high between the plush cheeks + dark cleft crease.
    nose = (
        superellipsoid(0.064, 0.050, 0.044, e1=0.72, e2=0.78, u_segs=18, v_segs=12, name="bear_nose", material="item_dark_stitch")
        .translate(0.0, 0.062, 0.326)
    )
    mouth_stem = curved_tube(
        [(0.0, 0.045, 0.334), (0.0, -0.032, 0.348), (0.0, -0.098, 0.332)],
        [0.011, 0.012, 0.009],
        samples_per_seg=3,
        radial_segs=10,
        name="bear_mouth_cleft",
        material="item_dark_stitch",
    )
    sc.add("bear_head", [nose, mouth_stem])

    # Iconic mismatched bear eyes from `345:bearhed`: stitched X-thread on left, glossy black button eye on right!
    for ang_deg, tag in ((+44.0, "a"), (-44.0, "b")):
        x_bar = (
            rounded_box(0.074, 0.018, 0.020, bevel=0.005, segs=2, name=f"bear_x_eye_{tag}", material="item_dark_stitch")
            .rotate_xyz(rz=ang_deg, degrees=True)
            .rotate_xyz(ry=-0.22)
            .translate(-0.088, 0.138, 0.258)
        )
        sc.add("bear_head", x_bar)
    right_eye = sphere(0.036, u_segs=16, v_segs=12, name="bear_button_eye", material="eye_pupil").translate(+0.072, 0.140, 0.272)
    right_glint = sphere(0.010, u_segs=10, v_segs=8, name="bear_eye_glint", material="eye_catchlight").translate(+0.080, 0.152, 0.304)
    sc.add("bear_head", [right_eye, right_glint])

    # Generous billowing white cotton stuffing tufts pouring out of the torn lower neck base (`345:bearhed`!).
    fluff_specs = (
        (0.00, -0.275, +0.06, 0.105, 0.072, 0.095),
        (-0.09, -0.282, +0.10, 0.082, 0.060, 0.076),
        (+0.09, -0.276, +0.08, 0.080, 0.062, 0.078),
        (-0.03, -0.305, +0.14, 0.072, 0.052, 0.068),
        (+0.06, -0.298, +0.14, 0.068, 0.048, 0.064),
        (-0.11, -0.258, -0.02, 0.062, 0.048, 0.058),
    )
    for idx, (fx, fy, fz, rx, ry, rz) in enumerate(fluff_specs):
        tuft = _plush_tint(
            ellipsoid(rx, ry, rz, u_segs=14, v_segs=10, name=f"bear_stuffing_{idx}", material="item_cotton_fluff")
            .deform(_fur_bump(idx * 1.7, 0.010))
            .translate(fx, fy, fz),
            warm_hi=(1.00, 1.00, 0.99),
            cool_lo=(0.82, 0.81, 0.79),
        )
        sc.add("bear_head", tuft)

    # Rest the popped-off head tilted back face-upward on the ground matching `345:bearhed`.
    sc.nodes["bear_head"] = [p.rotate_xyz(rx=-44.0, ry=-12.0, rz=-22.0, degrees=True) for p in sc.nodes["bear_head"]]
    return _finalize_item(sc, target_span=0.90)


def build_bearbod() -> SceneBuilder:
    """Sprawled headless plush teddy bear body with tan oval belly & torn neck stuffing (`346:bearbod`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_bearbod")

    # Plump horizontal sprawled plush bear torso resting flat on its back (matching `346:bearbod`).
    torso = _plush_tint(
        superellipsoid(0.24, 0.145, 0.30, e1=0.88, e2=0.90, u_segs=28, v_segs=20, name="bear_torso", material="item_bear_fur")
        .deform(_fur_bump(1.1, 0.007))
        .translate(0.0, 0.145, 0.0)
    )
    belly = _plush_tint(
        superellipsoid(0.155, 0.052, 0.205, e1=0.86, e2=0.88, u_segs=24, v_segs=14, name="bear_belly_patch", material="item_bear_belly")
        .deform(_fur_bump(2.3, 0.004))
        .translate(0.0, 0.256, -0.010),
        warm_hi=(1.05, 1.00, 0.92),
        cool_lo=(0.82, 0.76, 0.66),
    )
    sc.add("bear_body", [torso, belly])

    # Four chubby plush arms and legs splayed outward like a floppy stuffed animal on the ground.
    limb_specs = (
        ("arm_l", (+0.18, 0.14, -0.14), (+0.38, 0.11, -0.22), 0.092, 0.082),
        ("arm_r", (-0.18, 0.14, -0.14), (-0.38, 0.11, -0.20), 0.092, 0.082),
        ("leg_l", (+0.16, 0.12, +0.20), (+0.32, 0.095, +0.42), 0.098, 0.092),
        ("leg_r", (-0.16, 0.12, +0.20), (-0.32, 0.095, +0.42), 0.098, 0.092),
    )
    for idx, (tag, p0, p1, r0, r1) in enumerate(limb_specs):
        mid = tuple(0.5 * (a + b) for a, b in zip(p0, p1))
        limb = _plush_tint(
            curved_tube(
                [p0, (mid[0], mid[1] + 0.015, mid[2]), p1],
                [r0, 0.5 * (r0 + r1) * 1.04, r1],
                samples_per_seg=4,
                radial_segs=16,
                cap_rings=4,
                name=f"bear_{tag}",
                material="item_bear_fur",
            ).deform(_fur_bump(idx * 1.9, 0.006))
        )
        # Lighter plush paw pad facing upward on the feet & hands.
        pad = _plush_tint(
            ellipsoid(r1 * 0.62, r1 * 0.34, r1 * 0.68, u_segs=14, v_segs=10, name=f"bear_pad_{tag}", material="item_bear_belly")
            .translate(p1[0], p1[1] + r1 * 0.72, p1[2]),
            warm_hi=(1.04, 0.98, 0.90),
            cool_lo=(0.82, 0.74, 0.64),
        )
        sc.add("bear_body", [limb, pad])

    # Torn collar rim + billowing white cotton stuffing bursting out of the headless neck (`-Z` top).
    neck_ring = torus_arc(0.092, 0.022, u_segs=20, v_segs=10, plane="xy", name="bear_torn_collar", material="item_bear_fur").translate(0.0, 0.165, -0.285)
    sc.add("bear_body", _plush_tint(neck_ring))
    for idx, (fx, fy, fz, rad) in enumerate((
        (0.00, 0.175, -0.34, 0.068),
        (-0.055, 0.200, -0.36, 0.052),
        (+0.055, 0.185, -0.37, 0.050),
        (0.00, 0.215, -0.33, 0.048),
        (+0.03, 0.145, -0.35, 0.046),
    )):
        sc.add(
            "bear_body",
            _plush_tint(
                ellipsoid(rad, rad * 0.85, rad * 1.05, u_segs=14, v_segs=10, name=f"bear_neck_fluff_{idx}", material="item_cotton_fluff")
                .deform(_fur_bump(idx * 2.5, 0.008))
                .translate(fx, fy, fz),
                warm_hi=(1.00, 1.00, 0.99),
                cool_lo=(0.84, 0.83, 0.80),
            ),
        )

    return _finalize_item(sc, target_span=0.95)


def _build_rubber_ducky(name: str, *, yaw_deg: float, head_yaw_deg: float, head_pitch_deg: float) -> SceneBuilder:
    """Glossy sculpted yellow vinyl rubber bath ducky matching `348:ducky1` & `349:ducky2`."""
    register_item_materials()
    sc = SceneBuilder(name=name)

    # Plump bath-ducky hull with weighted base, rounded breast, and dramatic upturned rubber-ducky tail!
    def _sculpt_duck_body(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        # Sweep rear (-Z) sharply upward and narrow to a sculpted perky tail point like `348:ducky1` / `349:ducky2`.
        rear = np.clip(-p[:, 2] / 0.36, 0.0, 1.0)
        p[:, 1] += 0.255 * (rear ** 2.0)
        p[:, 2] -= 0.065 * (rear ** 2.4)
        p[:, 0] *= 1.0 - 0.74 * (rear ** 1.55)
        front = np.clip(p[:, 2] / 0.34, 0.0, 1.0)
        p[:, 0] *= 1.0 + 0.08 * front * (1.0 - front)
        p[:, 1] = np.where(p[:, 1] < -0.115, -0.115 + 0.18 * (p[:, 1] + 0.115), p[:, 1])
        return p

    body = gradient_y_tint(
        superellipsoid(0.225, 0.168, 0.365, e1=0.90, e2=0.92, u_segs=38, v_segs=26, name="duck_body", material="item_duck_yellow")
        .deform(_sculpt_duck_body)
        .translate(0.0, 0.160, 0.0),
        (0.94, 0.88, 0.72),
        (1.00, 1.00, 0.98),
    )
    sc.add("ducky", body)

    # Molded tucked bath-ducky wings sculpted smoothly onto both flanks with scalloped feather tips.
    for side, tag in ((+1.0, "left"), (-1.0, "right")):
        def _sculpt_wing(pos: np.ndarray, s=side) -> np.ndarray:
            p = pos.copy()
            u = np.clip((-p[:, 2] + 0.16) / 0.32, 0.0, 1.0)
            p[:, 1] *= 1.0 - 0.35 * (u ** 1.4)
            p[:, 1] += 0.036 * (u ** 1.8)
            scallop = 0.010 * np.sin(u * math.pi * 3.0) * (p[:, 1] < 0.0)
            p[:, 1] += scallop
            p[:, 0] *= 0.65 + 0.35 * (1.0 - u)
            return p

        wing = gradient_y_tint(
            superellipsoid(0.048, 0.105, 0.165, e1=0.78, e2=0.84, u_segs=22, v_segs=14, name=f"duck_wing_{tag}", material="item_duck_yellow")
            .deform(_sculpt_wing)
            .rotate_xyz(rx=0.14, ry=side * 0.18, rz=-side * 0.16)
            .translate(side * 0.212, 0.205, -0.025),
            (0.90, 0.84, 0.70),
            (1.00, 1.00, 0.96),
        )
        sc.add("ducky", wing)

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

    # Head assembly (pivoted at top of neck so `ducky1` & `ducky2` have distinct lively poses!).
    head_parts: List[MeshPart] = []
    cranium = gradient_y_tint(
        superellipsoid(0.162, 0.168, 0.172, e1=0.94, e2=0.96, u_segs=32, v_segs=22, name="duck_head", material="item_duck_yellow")
        .deform(lambda p: np.column_stack([p[:, 0] * (1.0 + 0.06 * np.exp(-((p[:, 1] + 0.04) / 0.09) ** 2)), p[:, 1], p[:, 2]])),
        (0.94, 0.90, 0.78),
        (1.00, 1.00, 0.98),
    )
    head_parts.append(cranium)
    # Cute little molded top crest tuft.
    crest = (
        ellipsoid(0.028, 0.044, 0.056, u_segs=14, v_segs=10, name="duck_crest", material="item_duck_yellow")
        .rotate_xyz(rx=-0.45)
        .translate(0.0, 0.168, -0.018)
    )
    head_parts.append(crest)

    # Classic wide smiling glossy orange-red duck bill (upper bill + tucked lower bill).
    def _sculpt_bill(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        u = np.clip((p[:, 2] + 0.09) / 0.18, 0.0, 1.0)
        p[:, 0] *= 0.84 + 0.32 * np.sin(u * math.pi * 0.78)
        p[:, 1] *= 1.0 - 0.45 * (u ** 1.3)
        p[:, 1] += 0.016 * (u ** 2.0)  # cheerful upturned duck smile!
        return p

    upper_bill = (
        superellipsoid(0.095, 0.032, 0.098, e1=0.76, e2=0.82, u_segs=24, v_segs=14, name="duck_bill_upper", material="item_duck_bill")
        .deform(_sculpt_bill)
        .rotate_xyz(rx=-0.08)
        .translate(0.0, -0.012, 0.198)
    )
    lower_bill = (
        superellipsoid(0.080, 0.024, 0.082, e1=0.78, e2=0.84, u_segs=20, v_segs=12, name="duck_bill_lower", material="item_duck_bill")
        .rotate_xyz(rx=0.28)
        .translate(0.0, -0.056, 0.180)
    )
    head_parts.extend([upper_bill, lower_bill])

    # Glossy painted cartoon duck eyes with crisp white sclera, black pupil, and bright catchlight.
    for side, tag in ((+1.0, "left"), (-1.0, "right")):
        ex, ey, ez = side * 0.105, 0.052, 0.118
        sclera = ellipsoid(0.036, 0.044, 0.028, u_segs=16, v_segs=12, name=f"duck_eye_w_{tag}", material="eye_white").rotate_xyz(ry=side * 0.52).translate(ex, ey, ez)
        pupil = ellipsoid(0.022, 0.028, 0.018, u_segs=14, v_segs=10, name=f"duck_eye_p_{tag}", material="eye_pupil").rotate_xyz(ry=side * 0.52).translate(ex + side * 0.012, ey, ez + 0.016)
        glint = sphere(0.007, u_segs=10, v_segs=8, name=f"duck_eye_g_{tag}", material="eye_catchlight").translate(ex + side * 0.020, ey + 0.012, ez + 0.026)
        head_parts.extend([sclera, pupil, glint])

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
    """Molded glossy green plastic toy Stegosaurus with long neck, sweeping tail & bold rows of plates (`351:dino`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_dino")

    # Elongated agile Stegosaurus silhouette matching `351:dino`: slender arching neck -> stout arched body -> sweeping pointed tail!
    spine_pts = [
        (0.0, 0.440, +0.580),  # narrow beak/snout tip
        (0.0, 0.460, +0.495),  # small sculpted head crown
        (0.0, 0.405, +0.390),  # upper neck
        (0.0, 0.300, +0.265),  # neck base
        (0.0, 0.255, +0.135),  # shoulders
        (0.0, 0.295, -0.025),  # arched back crown
        (0.0, 0.245, -0.185),  # hips
        (0.0, 0.170, -0.355),  # thick tail mid
        (0.0, 0.115, -0.510),  # slender tail sweep
        (0.0, 0.075, -0.640),  # pointed tail tip
    ]
    spine_rads = [
        (0.026, 0.022),
        (0.044, 0.040),
        (0.042, 0.044),
        (0.062, 0.064),
        (0.106, 0.112),
        (0.135, 0.148),
        (0.112, 0.118),
        (0.064, 0.064),
        (0.036, 0.036),
        (0.012, 0.012),
    ]
    body = gradient_y_tint(
        curved_tube(spine_pts, spine_rads, samples_per_seg=5, radial_segs=20, cap_rings=4, name="dino_body", material="item_toy_green"),
        (0.66, 0.86, 0.78),
        (0.98, 1.00, 0.94),
        noise_amp=0.05,
    )
    sc.add("dino", body)

    # Four sturdy molded vinyl dinosaur legs splayed outward slightly like a classic plastic toy.
    for tag, sx, sz, top_y, rad in (
        ("fl", +0.115, +0.140, 0.21, 0.048),
        ("fr", -0.115, +0.140, 0.21, 0.048),
        ("rl", +0.125, -0.165, 0.22, 0.056),
        ("rr", -0.125, -0.165, 0.22, 0.056),
    ):
        leg = curved_tube(
            [(sx * 0.72, top_y, sz), (sx * 1.04, top_y * 0.50, sz + 0.012), (sx * 1.18, 0.028, sz + 0.022)],
            [rad * 1.04, rad * 0.84, rad * 0.94],
            samples_per_seg=3,
            radial_segs=14,
            cap_rings=3,
            name=f"dino_leg_{tag}",
            material="item_toy_green",
        ).deform(lambda p: np.column_stack([p[:, 0], np.maximum(p[:, 1], 0.0), p[:, 2]]))
        sc.add("dino", leg)

    # TWO dramatic alternating rows of sharp triangular/diamond dorsal plates running neck-to-tail (`351:dino`)!
    def _diamond_plate(ph: float, pl: float, name: str) -> MeshPart:
        # Pinch top into crisp triangular Stegosaurus crest tip!
        def _pinch(pos: np.ndarray) -> np.ndarray:
            p = pos.copy()
            u = np.clip((p[:, 1] + ph) / (2.0 * ph), 0.0, 1.0)
            p[:, 2] *= 1.0 - 0.88 * (u ** 1.35)
            p[:, 0] *= 1.0 - 0.72 * (u ** 1.2)
            return p

        return superellipsoid(0.018, ph, pl * 0.58, e1=0.55, e2=0.55, u_segs=14, v_segs=10, name=name, material="item_toy_green_dark").deform(_pinch)

    plate_specs = (
        (+0.018, 0.445, +0.345, 0.068, 0.068, +0.42),
        (-0.020, 0.395, +0.255, 0.095, 0.095, +0.25),
        (+0.024, 0.420, +0.145, 0.125, 0.120, +0.08),
        (-0.026, 0.460, +0.025, 0.145, 0.135, -0.04),
        (+0.026, 0.450, -0.095, 0.138, 0.128, -0.14),
        (-0.022, 0.385, -0.215, 0.118, 0.112, -0.28),
        (+0.018, 0.305, -0.325, 0.095, 0.092, -0.38),
        (-0.014, 0.220, -0.430, 0.072, 0.070, -0.46),
        (+0.012, 0.155, -0.525, 0.048, 0.048, -0.52),
    )
    for idx, (px, py, pz, ph, pl, pitch) in enumerate(plate_specs):
        plate = (
            _diamond_plate(ph, pl, f"dino_plate_{idx}")
            .rotate_xyz(rx=pitch, rz=0.18 if px > 0 else -0.18)
            .translate(px, py, pz)
        )
        sc.add("dino", plate)

    # Small glossy toy eyes on the sculpted snout.
    for side, tag in ((+1.0, "l"), (-1.0, "r")):
        sc.add("dino", sphere(0.012, u_segs=12, v_segs=8, name=f"dino_eye_{tag}", material="eye_pupil").translate(side * 0.036, 0.466, +0.512))

    # Angle matching `351:dino`: neck pointing upper-left, pointed tail sweeping lower-right!
    sc.nodes["dino"] = [p.rotate_xyz(ry=-145.0, degrees=True) for p in sc.nodes["dino"]]
    return _finalize_item(sc, target_span=0.96)


def build_can1() -> SceneBuilder:
    """Crushed metallic purple/red/white Critter Cola soda can with aluminum lid & pull-tab facing front-right (`238:can1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_can1")

    def _crush_can(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        mid_w = np.exp(-((p[:, 2] / 0.14) ** 2))
        # Realistic stepped-on diagonal pinch crease across the top-center of the aluminum barrel!
        top_mask = np.clip((p[:, 1] + 0.04) / 0.20, 0.0, 1.0)
        crease = 0.072 * mid_w * top_mask * (0.72 + 0.28 * np.cos(p[:, 0] * 14.0 + p[:, 2] * 11.0))
        p[:, 1] -= crease
        p[:, 0] *= 1.0 + 0.18 * mid_w * top_mask
        return p

    # Subdivided axial rings along the aluminum barrel so purple/red/white/gold label bands & crush dent render razor-sharp!
    barrel_prof = [(0.158, -0.285)] + [
        (0.165, -0.240 + 0.475 * (i / 18.0)) for i in range(19)
    ] + [(0.145, +0.295)]
    body_shell = lathe(
        barrel_prof,
        radial_segs=48,
        cap_bottom=False,
        cap_top=False,
        axis="z",
        name="can_label_wall",
        material="item_can_white",
    ).deform(_crush_can)

    def _cola_label_tint(pos: np.ndarray, _nrm: np.ndarray) -> np.ndarray:
        ang = np.arctan2(pos[:, 1], pos[:, 0])
        z = pos[:, 2]
        # Base royal-purple soda label with vivid crimson-red bottom & side swoosh + crisp white "Cola" center emblem!
        purple = np.array([[0.38, 0.22, 0.86]], dtype=np.float64)
        crimson = np.array([[0.92, 0.12, 0.22]], dtype=np.float64)
        white = np.array([[0.98, 0.98, 1.00]], dtype=np.float64)
        gold = np.array([[0.98, 0.80, 0.22]], dtype=np.float64)

        # Red accent rim bands near lid & base + diagonal splash wave
        rim_red = np.clip((np.abs(z) - 0.185) * 24.0, 0.0, 1.0)[:, None]
        wave_red = np.clip((np.sin(ang * 2.0 + z * 9.0) - 0.35) * 4.5, 0.0, 1.0)[:, None]
        col = purple * (1.0 - np.maximum(rim_red, wave_red)) + crimson * np.maximum(rim_red, wave_red)

        # Crisp diagonal white logo ribbon ("Cola" emblem stripe across the top flank)
        ribbon_dist = np.abs(z - 0.02 - 0.03 * np.sin(ang * 2.0))
        ribbon = np.clip((0.048 - ribbon_dist) * 28.0, 0.0, 1.0)[:, None] * (pos[:, 1:2] > -0.02)
        col = col * (1.0 - ribbon) + white * ribbon

        # Bright golden sparkle stripes flanking the white logo stripe
        sparkle = np.clip((0.016 - np.abs(ribbon_dist - 0.056)) * 45.0, 0.0, 1.0)[:, None] * (pos[:, 1:2] > -0.02)
        col = col * (1.0 - sparkle) + gold * sparkle
        return np.clip(col, 0.08, 1.0)

    sc.add("can", tint_part(body_shell, _cola_label_tint))

    # Brushed metallic silver top & bottom aluminum ends.
    top_end = lathe(
        [(0.145, +0.295), (0.148, +0.320), (0.130, +0.322), (0.126, +0.304), (0.000, +0.302)],
        radial_segs=38,
        cap_bottom=False,
        cap_top=False,
        axis="z",
        name="can_top_lid",
        material="item_aluminum",
    ).deform(_crush_can)
    bot_end = lathe(
        [(0.000, -0.320), (0.115, -0.312), (0.135, -0.322), (0.158, -0.285)],
        radial_segs=38,
        cap_bottom=False,
        cap_top=False,
        axis="z",
        name="can_bottom",
        material="item_aluminum",
    ).deform(_crush_can)
    # Dark punched drink opening + raised silver pull-tab ring prominently visible on the aluminum lid (`238:can1`)!
    drink_hole = ellipsoid(0.042, 0.052, 0.010, u_segs=18, v_segs=8, name="can_hole", material="gear_dark").translate(0.0, +0.054, +0.304)
    tab_ring = torus_arc(0.036, 0.008, u_segs=20, v_segs=8, plane="xy", name="can_pull_tab", material="item_aluminum").rotate_xyz(rx=-0.38).translate(0.0, -0.022, +0.326)
    tab_rivet = cylinder(0.014, 0.014, 0.012, radial_segs=12, axis="z", name="can_rivet", material="item_aluminum").translate(0.0, 0.008, +0.310)
    sc.add("can", [top_end, bot_end, drink_hole, tab_ring, tab_rivet])

    # Lay crushed can diagonally with the open aluminum pull-tab lid facing forward-right just like `238:can1`!
    sc.nodes["can"] = [p.rotate_xyz(rx=-8.0, ry=46.0, rz=-10.0, degrees=True) for p in sc.nodes["can"]]
    return _finalize_item(sc, target_span=0.96)
