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

    # Subtle embroidered dark stitch dashes along the plush cranium center seam for authentic stuffed-toy detail.
    for k in range(5):
        sy = 0.215 - k * 0.036
        sz = 0.175 + k * 0.025
        stitch = rounded_box(0.022, 0.008, 0.008, bevel=0.002, segs=1, name=f"bear_seam_st_{k}", material="item_dark_stitch").rotate_xyz(rx=0.45 - 0.10 * k).translate(0.0, sy, sz)
        sc.add("bear_head", stitch)

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

    # Subtle plush seam stitches running around the tan belly patch oval.
    for k in range(10):
        ang = 2.0 * math.pi * k / 10.0
        st = rounded_box(0.018, 0.008, 0.008, bevel=0.002, segs=1, name=f"bear_belly_st_{k}", material="item_dark_stitch").rotate_xyz(ry=ang).translate(0.150 * math.cos(ang), 0.278, -0.010 + 0.198 * math.sin(ang))
        sc.add("bear_body", st)

    return _finalize_item(sc, target_span=0.95)


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


def build_can1() -> SceneBuilder:
    """Authentically crushed Critter Cola aluminum can with crisp 256px printed label & figure-8 pull-tab (`238:can1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_can1")

    def _crush_can(pos: np.ndarray) -> np.ndarray:
        """Stepped-on crushed aluminum soda can: deeply pinched & bent middle waist + sharp metallic buckle creases!"""
        p = pos.copy()
        z = p[:, 2]
        # Asymmetric waist crush envelope peaked near middle-rear (`z ~ -0.02`) leaving top & bottom aluminum rims intact!
        mid_w = np.exp(-(((z + 0.015) / 0.155) ** 2))
        end_guard = np.clip((0.285 - np.abs(z)) / 0.055, 0.0, 1.0)
        crush = mid_w * end_guard

        # Deep top-down foot stomp flattening the upper half ~54% inward with crisp metallic buckle creases!
        top_w = np.clip((p[:, 1] + 0.07) / 0.22, 0.0, 1.0)
        # Sharp metallic diagonal fold V-creases across the caved-in roof of the barrel!
        crease_a = 1.0 - np.abs(np.sin(p[:, 0] * 12.5 + z * 11.5))
        crease_b = 1.0 - np.abs(np.sin(p[:, 0] * 17.0 - z * 15.0 + 0.8))
        sharp_dent = 0.118 * crush * top_w * (0.68 + 0.22 * crease_a + 0.16 * crease_b)
        p[:, 1] -= sharp_dent

        # Subtle bottom flattening where the crushed can rests on the ground + lateral bulge out to the sides!
        bot_flat = np.clip((-p[:, 1] - 0.08) / 0.10, 0.0, 1.0)
        p[:, 1] += 0.026 * crush * bot_flat
        p[:, 0] *= 1.0 + 0.28 * crush * (0.55 + 0.45 * top_w)
        # Authentic stepped-on banana bow: rear & front rims cock slightly upward while waist kinks down!
        p[:, 1] += 0.062 * ((z / 0.28) ** 2) - 0.022 * crush
        p[:, 0] -= 0.048 * ((z / 0.28) ** 2)
        return p

    # Build explicit seam-free cylindrical barrel with clean (u, v) mapping directly to 256x256 `cola_label` texture!
    n_rings = 28
    n_rad = 56
    z_lo, z_hi = -0.285, +0.295
    verts: List[Tuple[float, float, float]] = []
    uvs: List[Tuple[float, float]] = []
    tris: List[Tuple[int, int, int]] = []
    for ir in range(n_rings + 1):
        vf = ir / float(n_rings)
        z = z_lo + (z_hi - z_lo) * vf
        # Taper cleanly at the upper neck & bottom heel like a real 12 oz aluminum soda can!
        if vf < 0.08:
            rad = 0.156 + 0.012 * (vf / 0.08)
        elif vf > 0.90:
            t_neck = (vf - 0.90) / 0.10
            rad = 0.168 - 0.024 * (t_neck ** 1.35)
        else:
            rad = 0.168
        # v = 0 near the open lid (+Z) and v = 1 near bottom (-Z) so 'Critter' sits near the lid & 'Cola' reads cleanly!
        tex_v = 1.0 - vf
        for j in range(n_rad + 1):
            uf = j / float(n_rad)
            # Wrap smoothly from underneath (-Y) over back-top -> front-left so 'Critter Cola' sits dead-center
            # on the top-front flank, non-mirrored & oriented 1:1 with `238:can1`!
            ang = -math.pi * 0.5 + uf * 2.0 * math.pi + 0.42
            vx = rad * math.cos(ang)
            vy = rad * math.sin(ang)
            verts.append((vx, vy, z))
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
            (0.150, +0.312),
            (0.145, +0.326),
            (0.132, +0.328),
            (0.125, +0.306),
            (0.110, +0.304),
            (0.000, +0.303),
        ],
        radial_segs=44,
        cap_bottom=False,
        cap_top=False,
        axis="z",
        name="can_top_lid",
        material="item_aluminum",
    ).deform(_crush_can)
    bot_end = lathe(
        [(0.000, -0.308), (0.098, -0.306), (0.128, -0.324), (0.146, -0.318), (0.156, -0.285)],
        radial_segs=40,
        cap_bottom=False,
        cap_top=False,
        axis="z",
        name="can_bottom",
        material="item_aluminum",
    ).deform(_crush_can)
    # Recessed lid score groove ring + dark D-shaped punched drink mouth + detailed figure-8 aluminum pull-tab (`238:can1`)!
    lid_dy = 0.062 * ((0.305 / 0.28) ** 2)
    lid_dx = -0.048 * ((0.305 / 0.28) ** 2)
    score_ring = torus_arc(0.112, 0.004, u_segs=36, v_segs=6, plane="xy", name="can_score_ring", material="item_steel_dark").translate(lid_dx, lid_dy, +0.307)
    drink_hole = (
        superellipsoid(0.046, 0.052, 0.010, e1=0.72, e2=0.80, u_segs=20, v_segs=8, name="can_hole", material="item_soil_hole")
        .translate(lid_dx - 0.050, lid_dy + 0.012, +0.306)
    )
    hole_rim = torus_arc(0.048, 0.005, u_segs=22, v_segs=6, plane="xy", name="can_hole_rim", material="item_aluminum").translate(lid_dx - 0.050, lid_dy + 0.012, +0.309)
    # Stay-on figure-8 aluminum pull-tab lying across center-right of the lid!
    tab_rivet = cylinder(0.015, 0.015, 0.014, radial_segs=14, axis="z", name="can_rivet", material="item_aluminum").translate(lid_dx + 0.006, lid_dy - 0.004, +0.312)
    tab_plate = rounded_box(0.068, 0.046, 0.009, bevel=0.003, segs=2, name="can_tab_bridge", material="item_aluminum").rotate_xyz(rz=-0.28).translate(lid_dx + 0.036, lid_dy - 0.014, +0.314)
    tab_loop = torus_arc(0.028, 0.008, u_segs=22, v_segs=8, plane="xy", name="can_pull_loop", material="item_aluminum").rotate_xyz(ry=-0.16, rz=-0.28).translate(lid_dx + 0.074, lid_dy - 0.026, +0.318)
    tab_hole_dark = cylinder(0.018, 0.018, 0.006, radial_segs=16, axis="z", name="can_tab_hole", material="item_steel_dark").translate(lid_dx + 0.074, lid_dy - 0.026, +0.310)
    sc.add("can", [top_end, bot_end, score_ring, drink_hole, hole_rim, tab_rivet, tab_plate, tab_loop, tab_hole_dark])

    # Lay crushed can diagonally with the gleaming open pull-tab lid facing forward-right just like `238:can1`!
    sc.nodes["can"] = [p.rotate_xyz(rx=-6.0, ry=48.0, rz=-12.0, degrees=True) for p in sc.nodes["can"]]
    return _finalize_item(sc, target_span=0.96)
