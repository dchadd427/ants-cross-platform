"""Realistic 3D sculptors for 1998 Ants Barbie Doll Parts & Easter-Egg Characters (9 items).

Covers:
- `barbie`   (330): Headless/armless Barbie torso in hot-pink mini-dress & pink/blue beaded waist belt
- `leg1`, `leg2` (331–332): Detached smooth doll legs with hip swivel joint & arched tiptoe foot
- `arm1`, `arm2` (333–334): Detached smooth doll arms (straight horizontal & gracefully bent elbow)
- `tad`      (427): Tad the Space Frog lying flat spread-eagle on his back smiling in bubble space helmet
- `tad2`     (428): Tad the Space Frog sitting upright on a glossy blue water puddle holding a fishing lure
- `santahed` (429): Santa Claus head with red fur-trimmed Santa cap, white pom-pom & billowing beard
- `elvished` (430): Elvis head with sculpted glossy jet-black pompadour wave, sideburns & smirk
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
            lathe,
            rounded_box,
            sphere,
            superellipsoid,
            torus_arc,
        )
        from .item_materials import register_item_materials, tint_part
        from .items_toys import _finalize_item
    except ImportError:
        from map_items.item_materials import register_item_materials, tint_part  # type: ignore
        from map_items.items_toys import _finalize_item  # type: ignore
        from mesh_core import (  # type: ignore
            MeshPart,
            SceneBuilder,
            curved_tube,
            cylinder,
            ellipsoid,
            lathe,
            rounded_box,
            sphere,
            superellipsoid,
            torus_arc,
        )


def _const_tint(rgb: Tuple[float, float, float]):
    def _fn(pos: np.ndarray, _n: np.ndarray) -> np.ndarray:
        return np.tile(np.asarray(rgb, dtype=np.float64)[None, :], (len(pos), 1))
    return _fn


def build_barbie() -> SceneBuilder:
    """Headless/armless Barbie torso in glossy hot-pink strapless mini-dress & beaded waist belt (`330:barbie`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_barbie")
    parts: List[MeshPart] = []

    # 1. Sculpted glossy hot-pink high-neck mini-dress (`330:barbie`: covers shoulders cleanly up to neck collar!)
    dress_prof = [
        (0.000, 0.000),
        (0.145, 0.004),
        (0.148, 0.032),
        (0.128, 0.145),
        (0.114, 0.255),
        (0.088, 0.340),
        (0.108, 0.435),
        (0.122, 0.520),
        (0.112, 0.595),
        (0.072, 0.630),
        (0.000, 0.634),
    ]
    dress = (
        lathe(dress_prof, radial_segs=36, cap_bottom=True, cap_top=True, axis="y", name="barbie_dress", material="item_hot_pink")
        .deform(lambda p: np.column_stack([
            p[:, 0] * (1.0 + 0.10 * np.exp(-(((p[:, 1] - 0.14) / 0.14) ** 2))),
            p[:, 1],
            p[:, 2] * (0.78 + 0.24 * np.exp(-(((p[:, 1] - 0.50) / 0.09) ** 2)) * (p[:, 2] > 0))
            + 0.026 * np.exp(-(((p[:, 1] - 0.50) / 0.08) ** 2)) * np.clip(p[:, 2] / 0.10, 0.0, 1.0),
        ]))
    )
    parts.append(dress)

    # 2. Iconic alternating hot-pink & sky-blue spherical beaded waist belt (`330:barbie`)!
    n_beads = 16
    for k in range(n_beads):
        ang = 2.0 * math.pi * k / n_beads
        bx = 0.098 * math.cos(ang)
        bz = 0.082 * math.sin(ang)
        mat_b = "item_plastic_blue" if (k % 2 == 0) else "item_hot_pink"
        parts.append(sphere(0.022, u_segs=12, v_segs=8, name=f"barbie_bead_{k}", material=mat_b).translate(bx, 0.335, bz))

    # 3. Exposed doll neck post & hollow circular shoulder arm-sockets (`330:barbie`)!
    neck_base = cylinder(0.038, 0.032, 0.068, radial_segs=20, axis="y", name="barbie_neck", material="item_doll_skin").translate(0.0, 0.655, -0.006)
    neck_peg = cylinder(0.020, 0.018, 0.075, radial_segs=16, bevel=0.004, axis="y", name="barbie_neck_peg", material="item_doll_skin").translate(0.0, 0.715, -0.006)
    neck_ring = torus_arc(0.028, 0.008, u_segs=20, v_segs=8, plane="xz", name="barbie_neck_ball", material="item_doll_skin").translate(0.0, 0.688, -0.006)
    parts.extend([neck_base, neck_peg, neck_ring])

    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        sock_rim = torus_arc(0.036, 0.010, u_segs=18, v_segs=8, plane="yz", name=f"barbie_sock_rim_{tag}", material="item_doll_skin").translate(side * 0.110, 0.568, -0.008)
        sock_hole = cylinder(0.028, 0.028, 0.020, radial_segs=16, axis="x", name=f"barbie_sock_hole_{tag}", material="item_soil_hole").translate(side * 0.104, 0.568, -0.008)
        parts.extend([sock_rim, sock_hole])

    sc.add("barbie", [p.rotate_xyz(ry=-48.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.94)


def _sculpt_doll_leg(prefix: str) -> List[MeshPart]:
    """Smooth sculpted Barbie plastic doll leg with slanted hip swivel joint & arched tiptoe foot."""
    leg_tube = curved_tube(
        [
            (+0.018, +0.415, 0.0),
            (+0.010, +0.260, 0.0),
            (-0.004, +0.060, 0.0),
            (+0.008, -0.100, 0.0),
            (-0.014, -0.310, 0.0),
            (-0.032, -0.382, 0.0),
            (-0.098, -0.412, 0.0),
        ],
        [0.056, 0.062, 0.044, 0.048, 0.025, 0.023, 0.013],
        samples_per_seg=4,
        radial_segs=18,
        cap_rings=3,
        name=f"{prefix}_limb",
        material="item_doll_skin",
    )
    hip_ball = sphere(0.046, u_segs=18, v_segs=12, name=f"{prefix}_hip_ball", material="item_doll_skin").translate(+0.032, +0.435, 0.0)
    hip_peg = cylinder(0.024, 0.022, 0.095, radial_segs=16, bevel=0.004, axis="y", name=f"{prefix}_hip_peg", material="item_doll_skin").rotate_xyz(rz=-0.38).translate(+0.055, +0.495, 0.0)
    heel = ellipsoid(0.024, 0.022, 0.022, u_segs=12, v_segs=8, name=f"{prefix}_heel", material="item_doll_skin").translate(-0.018, -0.392, 0.0)
    return [leg_tube, hip_ball, hip_peg, heel]


def build_leg1() -> SceneBuilder:
    """Upright detached Barbie doll leg with hip swivel peg & arched tiptoe foot pointing left (`331:leg1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_leg1")
    sc.add("leg1", [p.rotate_xyz(rx=-84.0, ry=4.0, degrees=True) for p in _sculpt_doll_leg("leg1")])
    return _finalize_item(sc, target_span=0.94)


def build_leg2() -> SceneBuilder:
    """Diagonal detached Barbie doll leg pointing top-left (hip) to bottom-right (foot) (`332:leg2`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_leg2")
    sc.add("leg2", [p.rotate_xyz(rx=-84.0, ry=28.0, degrees=True) for p in _sculpt_doll_leg("leg2")])
    return _finalize_item(sc, target_span=0.94)


def _sculpt_doll_hand(prefix: str) -> List[MeshPart]:
    """Dainty anatomical 5-digit fashion doll hand (local +X fingers pointing forward, +Y back of hand, -Z thumb)."""
    parts: List[MeshPart] = []
    # Contoured flat tapered palm + subtle knuckle ridge
    palm = ellipsoid(0.030, 0.011, 0.022, u_segs=16, v_segs=10, name=f"{prefix}_palm", material="item_doll_skin").translate(0.026, 0.0, 0.0)
    parts.append(palm)
    # 4 distinct sculpted gracefully tapered fingers fanning slightly & curling gently upward at the tips!
    for fi, (fz, flen, frad, yaw) in enumerate((
        (-0.014, 0.044, 0.0052, +0.16),   # index
        (-0.004, 0.050, 0.0054, +0.04),   # middle
        (+0.006, 0.046, 0.0050, -0.05),   # ring
        (+0.015, 0.036, 0.0044, -0.16),   # pinky
    )):
        dx, dz = math.cos(yaw), math.sin(yaw)
        finger = curved_tube(
            [
                (0.046, 0.000, fz),
                (0.046 + dx * flen * 0.55, 0.002, fz + dz * flen * 0.55),
                (0.046 + dx * flen, 0.009, fz + dz * flen),
            ],
            [frad, frad * 0.85, frad * 0.58],
            samples_per_seg=3,
            radial_segs=8,
            cap_rings=2,
            name=f"{prefix}_f{fi}",
            material="item_doll_skin",
        )
        parts.append(finger)
    # Distinct opposable thumb angling naturally outward on -Z side!
    thumb = curved_tube(
        [(0.016, -0.002, -0.016), (0.034, 0.001, -0.030), (0.050, 0.006, -0.036)],
        [0.0068, 0.0058, 0.0044],
        samples_per_seg=3,
        radial_segs=8,
        cap_rings=2,
        name=f"{prefix}_thumb",
        material="item_doll_skin",
    )
    parts.append(thumb)
    return parts


def _sculpt_doll_arm(prefix: str, *, bent: bool = False) -> List[MeshPart]:
    """Anatomical vintage Barbie plastic doll arm with shoulder ball-joint, crisp elbow bend & 5-finger hand (`333:arm1`, `334:arm2`)."""
    parts: List[MeshPart] = []
    if not bent:
        # Straight horizontal anatomical doll arm (`333:arm1`): shoulder peg LEFT (-X) -> seamless upper arm -> subtle elbow -> tapered forearm -> wrist -> 5-digit hand RIGHT (+X)!
        peg = cylinder(0.017, 0.019, 0.068, radial_segs=18, bevel=0.004, axis="x", name=f"{prefix}_peg", material="item_doll_skin").translate(-0.402, 0.036, 0.0)
        arm = curved_tube(
            [
                (-0.365, 0.036, 0.0),
                (-0.335, 0.036, 0.0),
                (-0.215, 0.036, 0.0),
                (-0.045, 0.034, +0.003),
                (+0.115, 0.033, 0.0),
                (+0.255, 0.032, 0.0),
                (+0.300, 0.032, 0.0),
            ],
            [0.024, 0.036, 0.033, 0.0265, 0.0245, 0.0175, 0.0150],
            samples_per_seg=4,
            radial_segs=20,
            cap_rings=3,
            name=f"{prefix}_arm",
            material="item_doll_skin",
        )
        hand = [p.translate(+0.295, 0.032, 0.0) for p in _sculpt_doll_hand(prefix)]
        parts.extend([peg, arm, *hand])
    else:
        # Authentic hinged-elbow Barbie doll arm (`334:arm2`): seamless continuous arm with STRAIGHT upper arm, crisp anatomical
        # ~122° elbow bend at bottom-center, and STRAIGHT tapered forearm reaching up-left to 5-digit hand!
        p_sh = (+0.325, 0.036, +0.010)
        p_el = (+0.035, 0.034, +0.125)
        p_wr = (-0.255, 0.034, -0.085)
        peg = cylinder(0.017, 0.019, 0.066, radial_segs=18, bevel=0.004, axis="x", name=f"{prefix}_peg", material="item_doll_skin").rotate_xyz(ry=+0.34).translate(p_sh[0] + 0.052, p_sh[1], p_sh[2] - 0.018)
        arm = curved_tube(
            [
                (p_sh[0] + 0.024, 0.036, p_sh[2] - 0.008),
                p_sh,
                (0.55 * p_sh[0] + 0.45 * p_el[0], 0.035, 0.55 * p_sh[2] + 0.45 * p_el[2]),
                (0.14 * p_sh[0] + 0.86 * p_el[0], 0.034, 0.14 * p_sh[2] + 0.86 * p_el[2]),
                p_el,
                (0.86 * p_el[0] + 0.14 * p_wr[0], 0.034, 0.86 * p_el[2] + 0.14 * p_wr[2]),
                (0.45 * p_el[0] + 0.55 * p_wr[0], 0.034, 0.45 * p_el[2] + 0.55 * p_wr[2]),
                (0.12 * p_el[0] + 0.88 * p_wr[0], 0.034, 0.12 * p_el[2] + 0.88 * p_wr[2]),
                p_wr,
            ],
            [0.023, 0.035, 0.032, 0.027, 0.0275, 0.0265, 0.0235, 0.017, 0.015],
            samples_per_seg=4,
            radial_segs=20,
            cap_rings=3,
            name=f"{prefix}_arm",
            material="item_doll_skin",
        )
        fa_yaw = math.atan2(-(p_wr[2] - p_el[2]), p_wr[0] - p_el[0])
        hand = [p.rotate_xyz(ry=fa_yaw).translate(*p_wr) for p in _sculpt_doll_hand(prefix)]
        parts.extend([peg, arm, *hand])
    return parts


def build_arm1() -> SceneBuilder:
    """Straight horizontal detached Barbie doll arm (`333:arm1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_arm1")
    sc.add("arm1", _sculpt_doll_arm("arm1", bent=False))
    return _finalize_item(sc, target_span=0.94)


def build_arm2() -> SceneBuilder:
    """Bent-elbow detached Barbie doll arm curving up-left (`334:arm2`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_arm2")
    sc.add("arm2", _sculpt_doll_arm("arm2", bent=True))
    return _finalize_item(sc, target_span=0.92)


def _frog_webbed_foot(prefix: str, ankle: Tuple[float, float, float], dir_xz: Tuple[float, float], scale: float = 1.0) -> List[MeshPart]:
    """Cute 3-toed webbed cartoon green frog foot with rounded suction toe-pads (`427:tad`, `428:tad2`)."""
    ax, ay, az = ankle
    dx, dz = dir_xz
    dl = max(1e-5, math.hypot(dx, dz))
    dx, dz = dx / dl, dz / dl
    base_ang = math.atan2(dx, dz)
    parts: List[MeshPart] = []
    # Flat triangular green webbing fan connecting the 3 splayed frog toes
    web = ellipsoid(0.068 * scale, 0.010 * scale, 0.066 * scale, u_segs=14, v_segs=8, name=f"{prefix}_web", material="item_frog_green").rotate_xyz(ry=base_ang).translate(ax + dx * 0.055 * scale, ay - 0.004, az + dz * 0.055 * scale)
    parts.append(web)
    for ti, off in enumerate((-0.48, 0.0, +0.48)):
        ta = base_ang + off
        tlen = (0.135 if ti == 1 else 0.115) * scale
        tx, tz = ax + math.sin(ta) * tlen, az + math.cos(ta) * tlen
        toe = curved_tube(
            [(ax, ay, az), (0.5 * (ax + tx), ay - 0.002, 0.5 * (az + tz)), (tx, ay - 0.004, tz)],
            [0.016 * scale, 0.012 * scale, 0.010 * scale],
            samples_per_seg=2,
            radial_segs=8,
            cap_rings=2,
            name=f"{prefix}_toe_{ti}",
            material="item_frog_green",
        )
        pad = sphere(0.016 * scale, u_segs=10, v_segs=8, name=f"{prefix}_pad_{ti}", material="item_frog_green").translate(tx, ay - 0.003, tz)
        parts.extend([toe, pad])
    return parts


def _frog_space_head(prefix: str, *, grinning: bool) -> List[MeshPart]:
    """Tad the Space Frog's cartoon green head, twin bulging eyes, neck collar & clear bubble space helmet!"""
    parts: List[MeshPart] = []
    head = ellipsoid(0.118, 0.095, 0.112, u_segs=26, v_segs=16, name=f"{prefix}_head", material="item_frog_green")
    snout = ellipsoid(0.110, 0.062, 0.088, u_segs=22, v_segs=14, name=f"{prefix}_snout", material="item_frog_green").translate(0.0, -0.020, +0.055)
    parts.extend([head, snout])

    # Iconic big bulging frog eye domes on top of crown + vivid expressive eyes & warm chestnut irises!
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        dome = sphere(0.054, u_segs=18, v_segs=12, name=f"{prefix}_edome_{tag}", material="item_frog_green").translate(side * 0.058, +0.072, +0.016)
        eye_w = ellipsoid(0.038, 0.046, 0.028, u_segs=16, v_segs=10, name=f"{prefix}_eyew_{tag}", material="eye_white").translate(side * 0.056, +0.076, +0.052)
        iris = sphere(0.024, u_segs=14, v_segs=10, name=f"{prefix}_eyei_{tag}", material="item_chocolate").translate(side * 0.052, +0.075, +0.070)
        pupil = sphere(0.015, u_segs=12, v_segs=8, name=f"{prefix}_eyep_{tag}", material="eye_pupil").translate(side * 0.051, +0.075, +0.083)
        glint = sphere(0.0065, u_segs=8, v_segs=6, name=f"{prefix}_eyeg_{tag}", material="eye_catchlight").translate(side * 0.043, +0.084, +0.094)
        parts.extend([dome, eye_w, iris, pupil, glint])

    # Wide cheerful frog smile (big white teeth grin clearly visible across front of snout on `427:tad`!)
    if grinning:
        mouth_bg = ellipsoid(0.095, 0.038, 0.052, u_segs=18, v_segs=10, name=f"{prefix}_mouth", material="item_dark_stitch").translate(0.0, -0.010, +0.112)
        teeth = ellipsoid(0.086, 0.025, 0.046, u_segs=18, v_segs=8, name=f"{prefix}_teeth", material="item_candy_white").translate(0.0, -0.006, +0.120)
        parts.extend([mouth_bg, teeth])
    else:
        smile = curved_tube(
            [(-0.072, -0.025, +0.118), (0.0, -0.038, +0.136), (+0.072, -0.025, +0.118)],
            [0.006, 0.007, 0.006],
            samples_per_seg=3,
            radial_segs=6,
            name=f"{prefix}_smile",
            material="item_dark_stitch",
        )
        parts.append(smile)

    # Dark metallic astronaut neck collar ring + sky-blue communicator button + silver helmet ear-pods + clear glass bubble helmet!
    collar = torus_arc(0.095, 0.020, u_segs=24, v_segs=10, plane="xz", name=f"{prefix}_collar", material="item_steel_dark").translate(0.0, -0.085, 0.0)
    clasp = sphere(0.020, u_segs=12, v_segs=8, name=f"{prefix}_clasp", material="item_plastic_blue").translate(0.0, -0.085, +0.105)
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        pod = cylinder(0.026, 0.022, 0.042, radial_segs=16, bevel=0.004, axis="x", name=f"{prefix}_pod_{tag}", material="item_chrome_spring").translate(side * 0.145, +0.025, -0.015)
        parts.append(pod)
    crest = ellipsoid(0.024, 0.028, 0.048, u_segs=14, v_segs=8, name=f"{prefix}_crest", material="item_chrome_spring").translate(0.0, +0.145, -0.020)
    bubble = sphere(0.152, u_segs=28, v_segs=18, name=f"{prefix}_helmet", material="item_lens_clear").translate(0.0, +0.024, -0.005)
    parts.extend([collar, clasp, crest, bubble])
    return parts


def build_tad() -> SceneBuilder:
    """Tad the Space Frog lying flat spread-eagle on his back smiling in bubble space helmet (`427:tad`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_tad")
    parts: List[MeshPart] = []

    # Build in local space: lying on back (+Y belly facing up, head toward +Z, feet toward -Z), then rotate diagonally!
    # 1. Plump green frog body + sunny yellow oval belly patch + white space vest with pink shoulder patches & belt!
    torso = ellipsoid(0.125, 0.082, 0.175, u_segs=26, v_segs=16, name="tad_torso", material="item_frog_green").translate(0.0, 0.085, -0.02)
    belly = tint_part(
        ellipsoid(0.095, 0.036, 0.132, u_segs=22, v_segs=12, name="tad_belly", material="item_candy_white").translate(0.0, 0.148, -0.04),
        _const_tint((0.98, 0.86, 0.18)),
    )
    vest_l = rounded_box(0.082, 0.092, 0.165, bevel=0.024, segs=3, name="tad_vest_l", material="item_candy_white").translate(-0.082, 0.118, +0.045)
    vest_r = rounded_box(0.082, 0.092, 0.165, bevel=0.024, segs=3, name="tad_vest_r", material="item_candy_white").translate(+0.082, 0.118, +0.045)
    belt = torus_arc(0.105, 0.015, u_segs=24, v_segs=8, plane="xy", name="tad_belt", material="item_leather_brown").scale(1.18, 0.78, 1.0).translate(0.0, 0.098, -0.015)
    pouch = rounded_box(0.045, 0.038, 0.056, bevel=0.010, segs=2, name="tad_pouch", material="item_gear_brass").translate(+0.115, 0.095, -0.045)
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        patch = ellipsoid(0.028, 0.014, 0.028, u_segs=12, v_segs=8, name=f"tad_patch_{tag}", material="item_hot_pink").translate(side * 0.105, 0.164, +0.068)
        parts.append(patch)
    parts.extend([torso, belly, vest_l, vest_r, belt, pouch])

    # 2. Smiling helmeted frog head upright on back: neck at collar (-Z), crown top-right (+Z), grin & eyes UP (+Y)!
    head_parts = _frog_space_head("tad", grinning=True)
    parts.extend([p.rotate_xyz(ry=180.0, degrees=True).rotate_xyz(rx=+78.0, degrees=True).translate(0.0, 0.132, +0.236) for p in head_parts])

    # 3. Wide spread-eagle green frog arms & 3-fingered hands!
    for side, tag, elbow, hand_p in (
        (-1.0, "l", (-0.21, 0.078, +0.14), (-0.32, 0.072, +0.22)),
        (+1.0, "r", (+0.22, 0.078, +0.04), (+0.34, 0.070, -0.04)),
    ):
        arm = curved_tube(
            [(side * 0.10, 0.095, +0.08), elbow, hand_p],
            [0.036, 0.028, 0.024],
            samples_per_seg=4,
            radial_segs=12,
            name=f"tad_arm_{tag}",
            material="item_frog_green",
        )
        parts.append(arm)
        hx, hy, hz = hand_p
        dx, dz = hx - elbow[0], hz - elbow[2]
        dl = max(1e-5, math.hypot(dx, dz))
        ba = math.atan2(dx / dl, dz / dl)
        for fi, off in enumerate((-0.55, 0.0, +0.55)):
            fx, fz = hx + math.sin(ba + off) * 0.075, hz + math.cos(ba + off) * 0.075
            parts.append(curved_tube([(hx, hy, hz), (0.5 * (hx + fx), hy, 0.5 * (hz + fz)), (fx, hy, fz)], [0.014, 0.011, 0.009], samples_per_seg=2, radial_segs=8, cap_rings=2, name=f"tad_fin_{tag}_{fi}", material="item_frog_green"))
            parts.append(sphere(0.015, u_segs=8, v_segs=6, name=f"tad_fpad_{tag}_{fi}", material="item_frog_green").translate(fx, hy, fz))

    # 4. Long frog legs stretching out bottom-left (`left leg bent wide, right leg stretched long`) + big 3-toed webbed feet!
    leg_l = curved_tube(
        [(-0.075, 0.075, -0.15), (-0.18, 0.068, -0.25), (-0.24, 0.048, -0.34)],
        [0.046, 0.036, 0.026],
        samples_per_seg=4,
        radial_segs=12,
        name="tad_leg_l",
        material="item_frog_green",
    )
    leg_r = curved_tube(
        [(+0.070, 0.075, -0.16), (+0.095, 0.066, -0.30), (+0.075, 0.044, -0.45)],
        [0.046, 0.034, 0.024],
        samples_per_seg=4,
        radial_segs=12,
        name="tad_leg_r",
        material="item_frog_green",
    )
    parts.extend([leg_l, leg_r])
    parts.extend(_frog_webbed_foot("tad_foot_l", (-0.24, 0.044, -0.34), (-0.85, -0.50), scale=1.05))
    parts.extend(_frog_webbed_foot("tad_foot_r", (+0.075, 0.040, -0.45), (+0.15, -0.98), scale=1.18))

    # Rotate diagonally: head TOP-RIGHT, webbed feet BOTTOM-LEFT matching `427:tad`!
    sc.add("tad", [p.rotate_xyz(ry=142.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.98)


def build_tad2() -> SceneBuilder:
    """Tad the Space Frog sitting upright on a glossy blue water puddle holding a glowing pink fishing lure (`428:tad2`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_tad2")
    parts: List[MeshPart] = []

    # 1. Organic glossy sky-blue water puddle base with bright cyan shoreline rim (`428:tad2`)!
    puddle = (
        cylinder(0.285, 0.280, 0.022, radial_segs=42, bevel=0.008, axis="y", name="tad2_puddle", material="item_water_puddle")
        .deform(lambda p: np.column_stack([
            p[:, 0] * (1.0 + 0.16 * np.sin(np.arctan2(p[:, 2], p[:, 0]) * 3.0 + 0.6) + 0.09 * np.cos(np.arctan2(p[:, 2], p[:, 0]) * 5.0)),
            p[:, 1],
            p[:, 2] * (1.12 + 0.14 * np.cos(np.arctan2(p[:, 2], p[:, 0]) * 2.0 - 0.5)),
        ]))
        .translate(-0.02, 0.012, +0.06)
    )
    parts.append(puddle)

    # 2. Upright crouching green frog torso + yellow belly + white space vest with pink/purple shoulder patches!
    torso = ellipsoid(0.118, 0.125, 0.110, u_segs=24, v_segs=16, name="tad2_torso", material="item_frog_green").translate(0.0, 0.165, -0.02)
    belly = tint_part(
        ellipsoid(0.088, 0.092, 0.068, u_segs=20, v_segs=12, name="tad2_belly", material="item_candy_white").translate(0.0, 0.160, +0.056),
        _const_tint((0.98, 0.86, 0.18)),
    )
    vest = rounded_box(0.235, 0.125, 0.175, bevel=0.036, segs=3, name="tad2_vest", material="item_candy_white").translate(0.0, 0.215, -0.025)
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        parts.append(ellipsoid(0.032, 0.022, 0.028, u_segs=12, v_segs=8, name=f"tad2_patch_{tag}", material="item_hot_pink").translate(side * 0.112, 0.268, -0.010))
    parts.extend([torso, belly, vest])

    # 3. Big expressive upright helmeted frog head looking forward-front!
    head_parts = _frog_space_head("tad2", grinning=False)
    parts.extend([p.rotate_xyz(rx=12.0, ry=-6.0, degrees=True).translate(0.0, 0.365, +0.005) for p in head_parts])

    # 4. Seated crouched frog legs & big splayed webbed green feet planted cleanly on the water puddle!
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        thigh = ellipsoid(0.058, 0.072, 0.095, u_segs=16, v_segs=12, name=f"tad2_thigh_{tag}", material="item_frog_green").rotate_xyz(rx=0.35, ry=side * 0.42).translate(side * 0.125, 0.112, +0.015)
        shin = curved_tube(
            [(side * 0.155, 0.110, +0.065), (side * 0.145, 0.065, +0.105), (side * 0.125, 0.034, +0.135)],
            [0.034, 0.026, 0.022],
            samples_per_seg=3,
            radial_segs=10,
            name=f"tad2_shin_{tag}",
            material="item_frog_green",
        )
        parts.extend([thigh, shin])
        parts.extend(_frog_webbed_foot(f"tad2_foot_{tag}", (side * 0.125, 0.034, +0.135), (side * 0.35, +0.92), scale=0.95))

    # 5. Green frog arms + cute red/pink glowing fishing lure dangling down onto the left side of the blue puddle (`428:tad2`)!
    arm_l = curved_tube([(-0.105, 0.215, +0.02), (-0.175, 0.175, +0.08), (-0.155, 0.145, +0.14)], [0.028, 0.024, 0.022], samples_per_seg=3, radial_segs=10, name="tad2_arm_l", material="item_frog_green")
    arm_r = curved_tube([(+0.105, 0.215, +0.02), (+0.165, 0.165, +0.07), (+0.145, 0.115, +0.12)], [0.028, 0.024, 0.022], samples_per_seg=3, radial_segs=10, name="tad2_arm_r", material="item_frog_green")
    bob_bead = sphere(0.022, u_segs=12, v_segs=8, name="tad2_bobber", material="item_candy_red").translate(-0.155, 0.148, +0.155)
    lure = curved_tube(
        [(-0.155, 0.142, +0.155), (-0.168, 0.095, +0.178), (-0.145, 0.055, +0.198), (-0.158, 0.032, +0.220)],
        [0.011, 0.013, 0.015, 0.022],
        samples_per_seg=4,
        radial_segs=10,
        cap_rings=2,
        name="tad2_lure",
        material="item_hot_pink",
    )
    parts.extend([arm_l, arm_r, bob_bead, lure])

    sc.add("tad2", [p.rotate_xyz(ry=-8.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.92)


def build_santahed() -> SceneBuilder:
    """Santa Claus head with clear rosy face, button nose, matte white beard & red fur-trimmed Santa cap (`429:santahed`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_santahed")
    parts: List[MeshPart] = []
    snow = _const_tint((1.00, 1.00, 1.00))

    # 1. Warm plump rosy-cheeked Santa face + round rosy nose + merry dark eyes (zero fur-shell occlusion!)
    head = sphere(0.168, u_segs=28, v_segs=20, name="santa_head", material="item_doll_skin").translate(0.0, 0.245, 0.0)
    nose = tint_part(
        sphere(0.044, u_segs=16, v_segs=12, name="santa_nose", material="item_doll_skin").translate(0.0, 0.235, +0.168),
        _const_tint((0.98, 0.58, 0.52)),
    )
    mouth = tint_part(
        ellipsoid(0.036, 0.022, 0.022, u_segs=14, v_segs=8, name="santa_mouth", material="item_doll_skin").translate(0.0, 0.162, +0.158),
        _const_tint((0.92, 0.44, 0.44)),
    )
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        cheek = tint_part(
            sphere(0.046, u_segs=14, v_segs=10, name=f"santa_cheek_{tag}", material="item_doll_skin").translate(side * 0.088, 0.222, +0.128),
            _const_tint((0.98, 0.66, 0.60)),
        )
        eye = sphere(0.022, u_segs=14, v_segs=10, name=f"santa_eye_{tag}", material="eye_pupil").translate(side * 0.056, 0.278, +0.152)
        glint = sphere(0.007, u_segs=8, v_segs=6, name=f"santa_glint_{tag}", material="eye_catchlight").translate(side * 0.049, 0.285, +0.169)
        parts.extend([cheek, eye, glint])
    parts.extend([head, nose, mouth])

    # 2. Billowing sculpted velvety-white Santa beard & tucked mustache (`item_bun_crumb` snow-white, zero fur cloud!)
    beard = tint_part(
        superellipsoid(0.165, 0.168, 0.128, e1=0.80, e2=0.86, u_segs=28, v_segs=18, name="santa_beard", material="item_bun_crumb")
        .deform(lambda p: np.column_stack([
            p[:, 0] * (1.0 - 0.30 * np.clip((-p[:, 1] - 0.02) / 0.16, 0.0, 1.0)) + 0.042 * np.clip((-p[:, 1]) / 0.16, 0.0, 1.0),
            p[:, 1] + 0.009 * np.sin(p[:, 0] * 28.0) * np.cos(p[:, 1] * 22.0),
            p[:, 2] + 0.012 * np.cos(p[:, 0] * 18.0),
        ]))
        .translate(0.022, 0.095, +0.048),
        snow,
    )
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        stache = tint_part(
            ellipsoid(0.068, 0.026, 0.032, u_segs=16, v_segs=10, name=f"santa_stache_{tag}", material="item_bun_crumb")
            .rotate_xyz(rz=-side * 0.24)
            .translate(side * 0.058, 0.184, +0.154),
            snow,
        )
        parts.append(stache)
    parts.append(beard)

    # 3. Classic crimson-red slouchy Santa cap drooping right + plush velvety white faux-fur trim band + white pom-pom!
    fur_brim = tint_part(
        torus_arc(0.168, 0.040, u_segs=28, v_segs=12, plane="xz", name="santa_fur_brim", material="item_bun_crumb")
        .rotate_xyz(rx=-0.22, rz=-0.10)
        .translate(0.0, 0.358, -0.010),
        snow,
    )
    hat = curved_tube(
        [(0.0, 0.362, -0.018), (+0.038, 0.448, -0.028), (+0.132, 0.478, -0.022), (+0.222, 0.432, +0.005), (+0.252, 0.362, +0.028)],
        [0.152, 0.120, 0.076, 0.035, 0.016],
        samples_per_seg=4,
        radial_segs=18,
        cap_rings=2,
        name="santa_hat",
        material="item_candy_red",
    )
    pom = tint_part(sphere(0.046, u_segs=16, v_segs=12, name="santa_pom", material="item_bun_crumb").translate(+0.258, 0.342, +0.036), snow)
    parts.extend([fur_brim, hat, pom])

    sc.add("santahed", [p.rotate_xyz(rx=-10.0, ry=-12.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.82)


def build_elvished() -> SceneBuilder:
    """Elvis head with sleek sculpted jet-black pompadour wave, swept sideburns & confident smirk (`430:elvished`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_elvished")
    parts: List[MeshPart] = []

    # 1. Sculpted chiseled oval tan/peach Elvis face turned right-front matching `430:elvished`!
    head = (
        superellipsoid(0.138, 0.205, 0.150, e1=0.84, e2=0.88, u_segs=28, v_segs=20, name="elvis_head", material="item_doll_skin")
        .deform(lambda p: np.column_stack([
            p[:, 0] * (1.0 - 0.26 * np.clip((-p[:, 1] - 0.05) / 0.16, 0.0, 1.0)),
            p[:, 1],
            p[:, 2] + 0.028 * np.exp(-(((p[:, 1] + 0.13) / 0.055) ** 2)) * (p[:, 2] > 0)
            + 0.014 * np.exp(-(((p[:, 1] - 0.06) / 0.04) ** 2)) * (p[:, 2] > 0),
        ]))
        .translate(0.0, 0.215, 0.0)
    )
    nose = (
        ellipsoid(0.030, 0.058, 0.048, u_segs=16, v_segs=10, name="elvis_nose", material="item_doll_skin")
        .rotate_xyz(rx=-0.18)
        .translate(0.0, 0.212, +0.154)
    )
    # Iconic crooked Elvis sneer/smirk + dark expressive eyes & sculpted brow ridge!
    lips = tint_part(
        curved_tube(
            [(-0.045, 0.125, +0.142), (-0.010, 0.122, +0.156), (+0.025, 0.128, +0.154), (+0.052, 0.140, +0.138)],
            [0.010, 0.014, 0.014, 0.009],
            samples_per_seg=3,
            radial_segs=8,
            cap_rings=2,
            name="elvis_lips",
            material="item_doll_skin",
        ),
        _const_tint((0.84, 0.44, 0.40)),
    )
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        eye = ellipsoid(0.025, 0.022, 0.020, u_segs=14, v_segs=10, name=f"elvis_eye_{tag}", material="eye_pupil").translate(side * 0.052, 0.264, +0.148)
        glint = sphere(0.0065, u_segs=8, v_segs=6, name=f"elvis_glint_{tag}", material="eye_catchlight").translate(side * 0.045, 0.271, +0.165)
        brow = rounded_box(0.062, 0.015, 0.018, bevel=0.004, segs=2, name=f"elvis_brow_{tag}", material="item_hair_black").rotate_xyz(rz=side * (-0.14 if side < 0 else +0.16)).translate(side * 0.054, 0.302, +0.150)
        parts.extend([eye, glint, brow])
    parts.extend([head, nose, lips])

    # 2. Sleek scalp-hugging glossy jet-black Elvis pompadour + swept sideburns curving cleanly along cheeks (`430:elvished`)!
    hair_crown = ellipsoid(0.154, 0.165, 0.165, u_segs=28, v_segs=18, name="elvis_hair_crown", material="item_hair_black").translate(0.0, 0.278, -0.025)
    pomp = (
        superellipsoid(0.144, 0.072, 0.138, e1=0.72, e2=0.78, u_segs=28, v_segs=16, name="elvis_pompadour", material="item_hair_black")
        .rotate_xyz(rx=-0.16, rz=+0.08)
        .translate(+0.006, 0.372, +0.032)
    )
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        sideburn = curved_tube(
            [(side * 0.138, 0.335, +0.025), (side * 0.142, 0.245, +0.022), (side * 0.130, 0.165, +0.052), (side * 0.105, 0.125, +0.075)],
            [0.036, 0.038, 0.030, 0.018],
            samples_per_seg=3,
            radial_segs=10,
            cap_rings=2,
            name=f"elvis_sideburn_{tag}",
            material="item_hair_black",
        )
        parts.append(sideburn)
    parts.extend([hair_crown, pomp])

    # Turned slightly RIGHT-FRONT matching `430:elvished`!
    sc.add("elvished", [p.rotate_xyz(rx=-8.0, ry=+18.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.82)
