"""Realistic 3D sculptors for 1998 Ants Backyard Hardware & Debris Props (15 items).

Covers:
- `papclp1`, `papclp2` (311–312): Bent silver steel wire paperclips (vertical & diagonal)
- `pin1`, `pin2`, `pin3` (313–315): Colored plastic push-pins (blue upright, yellow bent-needle, green tilted)
- `botcap1` (316): Upside-down crimped silver bottle cap stamped 'YOU WIN!!!'
- `bolt1`, `bolt2` (317–318): Machined hexagonal steel nuts with threaded central bore (flat & tilted)
- `screw1`, `screw2` (319–320): Flat-head wood screw & chunky Phillips '+' machine screw
- `pencal1` (321): Translucent hot-pink Bic ballpoint pen cap with long diagonal pocket clip
- `shtbut1` (322): Glossy teal-green 4-hole round shirt button
- `broken1`, `broken2` (287–288): Hot-pink Barbie stiletto high-heel pumps (sideways & upright)
- `broken3` (1334): Sleek silver metal spoon handle lying horizontally
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
            recompute_smooth_normals,
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
            recompute_smooth_normals,
            rounded_box,
            sphere,
            superellipsoid,
            torus_arc,
        )


def _const_tint(rgb: Tuple[float, float, float]):
    def _fn(pos: np.ndarray, _n: np.ndarray) -> np.ndarray:
        return np.tile(np.asarray(rgb, dtype=np.float64)[None, :], (len(pos), 1))
    return _fn


def _build_paperclip(name: str, yaw_deg: float) -> SceneBuilder:
    """Classic nested 3-turn bent silver wire paperclip (`311:papclp1`, `312:papclp2`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    # Continuous looped spring-steel wire with authentic springy 3D loop overlap (`y > 0.055` normalized thickness)
    pts = [
        (-0.088, 0.016, +0.200),
        (-0.088, 0.016, -0.250),
        (-0.072, 0.017, -0.345),
        (0.000, 0.018, -0.375),
        (+0.078, 0.017, -0.340),
        (+0.095, 0.016, -0.240),
        (+0.095, 0.016, +0.265),
        (+0.074, 0.018, +0.360),
        (+0.005, 0.020, +0.385),
        (-0.056, 0.022, +0.352),
        (-0.064, 0.024, +0.255),
        (-0.064, 0.026, -0.215),
        (-0.046, 0.028, -0.285),
        (+0.004, 0.030, -0.305),
        (+0.050, 0.032, -0.278),
        (+0.058, 0.034, -0.200),
        (+0.058, 0.036, +0.210),
        (+0.042, 0.038, +0.275),
        (+0.002, 0.040, +0.292),
        (-0.032, 0.042, +0.262),
        (-0.032, 0.044, +0.020),
    ]
    wire = curved_tube(
        pts,
        [0.0155] * len(pts),
        samples_per_seg=3,
        radial_segs=10,
        cap_rings=2,
        name=f"{name}_wire",
        material="item_chrome_spring",
    )
    sc.add("paperclip", wire.rotate_xyz(ry=yaw_deg, degrees=True))
    return _finalize_item(sc, target_span=0.88)


def build_papclp1() -> SceneBuilder:
    """Nearly vertical silver steel paperclip (`311:papclp1`)."""
    return _build_paperclip("item_papclp1", yaw_deg=-7.0)


def build_papclp2() -> SceneBuilder:
    """Diagonal silver steel paperclip (`312:papclp2`)."""
    return _build_paperclip("item_papclp2", yaw_deg=38.0)


def _build_pushpin(
    name: str,
    *,
    cap_rgb: Tuple[float, float, float],
    rot_deg: Tuple[float, float, float],
    needle_len: float = 0.28,
    bent_needle: bool = False,
) -> SceneBuilder:
    """Classic glossy hourglass plastic thumbtack push-pin with steel needle (`313..315:pin1..3`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    # Sculpted glossy plastic push-pin body: flared collar base, concave pinched waist, rounded top thumb cap!
    prof = [
        (0.000, 0.000),
        (0.118, 0.000),
        (0.126, 0.016),
        (0.116, 0.038),
        (0.068, 0.056),
        (0.054, 0.115),
        (0.064, 0.195),
        (0.096, 0.236),
        (0.104, 0.264),
        (0.092, 0.284),
        (0.048, 0.292),
        (0.000, 0.294),
    ]
    cap = tint_part(
        lathe(prof, radial_segs=30, cap_bottom=True, cap_top=True, axis="y", name=f"{name}_cap", material="item_candy_white"),
        _const_tint(cap_rgb),
    )
    parts.append(cap)

    if bent_needle:
        # Iconic steep scimitar-bent silver needle curving down-left matching `314:pin2`!
        needle = curved_tube(
            [(0.0, 0.005, 0.0), (-0.015, -0.095, +0.010), (-0.052, -0.195, +0.018), (-0.068, -0.295, +0.018), (-0.055, -0.375, +0.012)],
            [0.019, 0.018, 0.015, 0.010, 0.002],
            samples_per_seg=4,
            radial_segs=10,
            cap_rings=2,
            name=f"{name}_needle",
            material="item_chrome_spring",
        )
    else:
        needle = cylinder(0.002, 0.019, needle_len, radial_segs=12, axis="y", name=f"{name}_needle", material="item_chrome_spring").translate(0.0, -needle_len * 0.5, 0.0)
    parts.append(needle)

    rx, ry, rz = rot_deg
    sc.add("pin", [p.rotate_xyz(rx=rx, ry=ry, rz=rz, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.78)


def build_pin1() -> SceneBuilder:
    """Upright cobalt-blue plastic push-pin (`313:pin1`)."""
    return _build_pushpin("item_pin1", cap_rgb=(0.08, 0.14, 0.94), rot_deg=(4.0, 0.0, -7.0), needle_len=0.22)


def build_pin2() -> SceneBuilder:
    """Tilted golden-yellow push-pin with dramatic bent steel needle (`314:pin2`)."""
    return _build_pushpin("item_pin2", cap_rgb=(0.98, 0.72, 0.02), rot_deg=(6.0, 8.0, -26.0), bent_needle=True)


def build_pin3() -> SceneBuilder:
    """Tilted emerald-green push-pin with long straight needle (`315:pin3`)."""
    return _build_pushpin("item_pin3", cap_rgb=(0.06, 0.64, 0.14), rot_deg=(6.0, -8.0, +16.0), needle_len=0.36)


def build_botcap1() -> SceneBuilder:
    """Upside-down 21-tooth crimped silver bottle cap stamped 'YOU WIN!!!' (`316:botcap1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_botcap1")

    # Build authentic hollow open-top crimped crown cup so we look DOWN INTO the 'YOU WIN!!!' liner!
    n_seg = 63  # multiple of 21 teeth for sharp crisp fluting
    r_base = 0.240
    r_rim = 0.285
    h_cup = 0.095
    wall_t = 0.022

    # Profile around the open cup rim: outer bottom -> outer fluted rim -> top lip -> inner fluted wall -> inner floor!
    ring_specs = (
        (r_base * 0.96, 0.000, 0.00),
        (r_base, 0.018, 0.15),
        (r_rim, h_cup - 0.010, 1.00),
        (r_rim + 0.006, h_cup, 1.00),
        (r_rim - wall_t, h_cup, 0.85),
        (r_base - wall_t, 0.026, 0.15),
        (0.000, 0.024, 0.00),
    )
    verts, uvs, tris = [], [], []
    for ir, (rad, yv, flute_w) in enumerate(ring_specs):
        vf = ir / float(len(ring_specs) - 1)
        for j in range(n_seg + 1):
            uf = j / float(n_seg)
            ang = uf * 2.0 * math.pi
            r_eff = rad * (1.0 + 0.088 * math.cos(ang * 21.0) * flute_w)
            verts.append((r_eff * math.cos(ang), yv, r_eff * math.sin(ang)))
            uvs.append((uf * 2.0, vf))
    stride = n_seg + 1
    for ir in range(len(ring_specs) - 1):
        for j in range(n_seg):
            a, b, c, d = ir * stride + j, (ir + 1) * stride + j, (ir + 1) * stride + (j + 1), ir * stride + (j + 1)
            tris.extend(((a, b, c), (a, c, d)))
    pos_c = np.asarray(verts, dtype=np.float32)
    idx_c = np.asarray(tris, dtype=np.uint32).reshape(-1)
    cup = tint_part(
        MeshPart("botcap_cup", "item_chrome_spring", pos_c, recompute_smooth_normals(pos_c, idx_c), idx_c, uvs=np.asarray(uvs, dtype=np.float32)),
        _const_tint((0.86, 0.84, 0.80)),
    )

    # Recessed printed 'YOU WIN!!!' plastic cap liner disk resting cleanly inside the open crimped rim!
    liner_r = r_base - wall_t * 0.85
    liner = cylinder(liner_r, liner_r, 0.012, radial_segs=36, bevel=0.002, axis="y", name="botcap_liner", material="item_cap_win").translate(0.0, 0.032, 0.0)
    u = np.clip(0.5 + liner.positions[:, 0] / (2.0 * liner_r * 1.04), 0.0, 1.0)
    v = np.clip(0.5 + liner.positions[:, 2] / (2.0 * liner_r * 1.04), 0.0, 1.0)
    liner.uvs = np.column_stack([u, v]).astype(np.float32)

    sc.add("botcap1", [cup, liner])
    return _finalize_item(sc, target_span=0.80)


def _build_hex_nut(name: str, *, rot_deg: Tuple[float, float, float]) -> SceneBuilder:
    """True hollow machined 6-sided steel hex nut with chamfered edges & internal thread rings (`317:bolt1`, `318:bolt2`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    # Explicit closed hollow hexagonal prism with crisp bevelled outer chamfers & circular bore hole!
    n_ang = 36  # 6 segments per hex face for crisp corners + smooth circular bore
    r_out_flat = 0.250
    r_in = 0.118
    h_half = 0.112
    cham = 0.020

    def _hex_r(theta: float) -> float:
        # Exact distance to regular hexagon inscribed with apothem r_out_flat
        a = ((theta + math.pi / 6.0) % (math.pi / 3.0)) - math.pi / 6.0
        return r_out_flat / max(0.5, math.cos(a))

    # Closed loop cross-section around the nut wall: inner bot -> outer bot bevel -> outer wall -> outer top bevel -> inner top -> back to inner bot!
    loop_prof = (
        ("in", r_in, -h_half + 0.008),
        ("in", r_in + 0.010, -h_half),
        ("hex", 0.94, -h_half),
        ("hex", 1.00, -h_half + cham),
        ("hex", 1.00, +h_half - cham),
        ("hex", 0.94, +h_half),
        ("in", r_in + 0.010, +h_half),
        ("in", r_in, +h_half - 0.008),
        ("in", r_in, -h_half + 0.008),
    )
    verts, uvs, tris = [], [], []
    for ir, (kind, scale_r, yv) in enumerate(loop_prof):
        vf = ir / float(len(loop_prof) - 1)
        for j in range(n_ang + 1):
            uf = j / float(n_ang)
            ang = uf * 2.0 * math.pi
            rad = (_hex_r(ang) * scale_r) if kind == "hex" else scale_r
            verts.append((rad * math.cos(ang), yv, rad * math.sin(ang)))
            uvs.append((uf * 2.0, vf * 2.0))
    stride = n_ang + 1
    for ir in range(len(loop_prof) - 1):
        for j in range(n_ang):
            a, b, c, d = ir * stride + j, (ir + 1) * stride + j, (ir + 1) * stride + (j + 1), ir * stride + (j + 1)
            tris.extend(((a, b, c), (a, c, d)))
    pos_n = np.asarray(verts, dtype=np.float32)
    idx_n = np.asarray(tris, dtype=np.uint32).reshape(-1)
    nut_body = tint_part(
        MeshPart(f"{name}_body", "item_chrome_spring", pos_n, recompute_smooth_normals(pos_n, idx_n), idx_n, uvs=np.asarray(uvs, dtype=np.float32)),
        _const_tint((0.80, 0.82, 0.86)),
    )
    parts.append(nut_body)

    # Visible machined internal screw thread ridges inside the central bore!
    for k in range(5):
        ty = -0.072 + k * 0.036
        parts.append(torus_arc(r_in - 0.004, 0.008, u_segs=28, v_segs=6, plane="xz", name=f"{name}_thread_{k}", material="item_steel_dark").translate(0.0, ty, 0.0))

    rx, ry, rz = rot_deg
    sc.add("nut", [p.rotate_xyz(rx=rx, ry=ry, rz=rz, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.76)


def build_bolt1() -> SceneBuilder:
    """Flat hexagonal steel nut lying on the ground (`317:bolt1`)."""
    return _build_hex_nut("item_bolt1", rot_deg=(0.0, 12.0, 0.0))


def build_bolt2() -> SceneBuilder:
    """Tilted upright hexagonal steel nut resting on one hex flat (`318:bolt2`)."""
    return _build_hex_nut("item_bolt2", rot_deg=(68.0, -34.0, 16.0))


def build_screw1() -> SceneBuilder:
    """Long slender countersunk flat-head steel wood screw lying diagonally (`319:screw1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_screw1")
    parts: List[MeshPart] = []

    # Build along Z: sharp tip at -Z (-0.36), flared countersunk flat head at +Z (+0.34)
    shank = cylinder(0.004, 0.044, 0.64, radial_segs=18, axis="z", name="screw1_shank", material="item_chrome_spring").translate(0.0, 0.0, -0.02)
    head = cylinder(0.044, 0.095, 0.072, radial_segs=24, bevel=0.004, axis="z", name="screw1_head", material="item_chrome_spring").translate(0.0, 0.0, +0.325)
    slot = rounded_box(0.145, 0.018, 0.016, bevel=0.003, segs=1, name="screw1_slot", material="item_steel_dark").translate(0.0, 0.0, +0.362)
    parts.extend([shank, head, slot])

    # Crisp diagonal machined screw thread coils along the entire tapering shaft!
    n_t = 15
    for k in range(n_t):
        u = k / float(n_t - 1)
        tz = -0.29 + u * 0.54
        tr = 0.014 + 0.036 * (u ** 0.72)
        ring = torus_arc(tr, 0.0085, u_segs=20, v_segs=6, plane="xy", name=f"screw1_th_{k}", material="item_aluminum").rotate_xyz(ry=0.14).translate(0.0, 0.0, tz)
        parts.append(ring)

    # Diagonal pose matching `319:screw1`: pointed tip UP-LEFT, flared countersunk head DOWN-RIGHT!
    sc.add("screw1", [p.rotate_xyz(ry=22.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.90)


def build_screw2() -> SceneBuilder:
    """Chunky pan-head Phillips '+' machine screw angled upright-front (`320:screw2`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_screw2")
    parts: List[MeshPart] = []

    # Local Y: wide domed pan head at top (+0.20), chunky threaded shank descending to -0.20
    head_prof = [(0.000, 0.255), (0.115, 0.252), (0.172, 0.228), (0.184, 0.185), (0.176, 0.156), (0.088, 0.154)]
    head = lathe(head_prof, radial_segs=32, cap_bottom=True, cap_top=True, axis="y", name="screw2_head", material="item_chrome_spring")
    # Iconic bold recessed Phillips '+' cross slot on top of the screw head (`320:screw2`)!
    cross_a = rounded_box(0.215, 0.022, 0.044, bevel=0.005, segs=2, name="screw2_cross_a", material="item_steel_dark").translate(0.0, 0.254, 0.0)
    cross_b = rounded_box(0.044, 0.022, 0.215, bevel=0.005, segs=2, name="screw2_cross_b", material="item_steel_dark").translate(0.0, 0.254, 0.0)
    shank = cylinder(0.068, 0.082, 0.34, radial_segs=22, bevel=0.008, axis="y", name="screw2_shank", material="item_chrome_spring").translate(0.0, -0.015, 0.0)
    parts.extend([head, cross_a, cross_b, shank])

    for k in range(7):
        ty = -0.155 + k * 0.046
        tr = 0.078 + 0.010 * (k / 6.0)
        parts.append(torus_arc(tr, 0.014, u_segs=24, v_segs=6, plane="xz", name=f"screw2_th_{k}", material="item_aluminum").rotate_xyz(rx=0.10).translate(0.0, ty, 0.0))

    # Tilt so bold '+' pan head faces UP-LEFT & threaded shank points DOWN-LEFT matching `320:screw2`!
    sc.add("screw2", [p.rotate_xyz(rx=18.0, ry=24.0, rz=-22.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.76)


def build_pencal1() -> SceneBuilder:
    """Translucent hot-pink Bic ballpoint pen cap with long diagonal pocket clip (`321:pencal1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_pencal1")
    parts: List[MeshPart] = []

    # Build along Z: open mouth at +Z (+0.08), smooth bullet-tapered closed tip at -Z (-0.42), long clip to +Z (+0.52)!
    barrel = lathe(
        [(0.072, +0.072), (0.064, -0.08), (0.052, -0.24), (0.036, -0.36), (0.018, -0.415), (0.000, -0.422)],
        radial_segs=26,
        cap_bottom=False,
        cap_top=True,
        axis="z",
        name="pencap_barrel",
        material="item_pen_pink",
    ).translate(0.0, 0.074, 0.0)
    # Visible hollow rim & dark open tube cavity at the bottom-left end of the cap!
    mouth_rim = torus_arc(0.068, 0.008, u_segs=24, v_segs=8, plane="xy", name="pencap_rim", material="item_hot_pink").translate(0.0, 0.074, +0.070)
    cavity = cylinder(0.054, 0.054, 0.065, radial_segs=20, axis="z", name="pencap_cavity", material="item_soil_hole").translate(0.0, 0.074, +0.042)
    # Iconic long sleek diagonal Bic pocket clip extending way past the open mouth (`321:pencal1`)!
    clip = curved_tube(
        [
            (-0.042, 0.086, -0.26),
            (-0.074, 0.090, -0.12),
            (-0.092, 0.084, +0.04),
            (-0.096, 0.076, +0.24),
            (-0.090, 0.068, +0.44),
            (-0.082, 0.062, +0.52),
        ],
        [0.020, 0.026, 0.024, 0.020, 0.014, 0.007],
        samples_per_seg=4,
        radial_segs=10,
        cap_rings=2,
        name="pencap_clip",
        material="item_hot_pink",
    )
    clip_bridge = rounded_box(0.044, 0.030, 0.14, bevel=0.006, segs=2, name="pencap_bridge", material="item_hot_pink").translate(-0.052, 0.086, -0.14)
    parts.extend([barrel, mouth_rim, cavity, clip, clip_bridge])

    # Angle ~42 deg: tapered closed tip UP-RIGHT, long pocket clip spear pointing DOWN-LEFT matching `321:pencal1`!
    sc.add("pencal1", [p.rotate_xyz(ry=-40.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.96)


def build_shtbut1() -> SceneBuilder:
    """Glossy teal-green 4-hole round plastic shirt button with beveled rim (`322:shtbut1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_shtbut1")
    parts: List[MeshPart] = []

    # Sculpted round shirt button with raised outer bevel rim & gently concave central dish!
    prof = [
        (0.000, 0.000),
        (0.240, 0.000),
        (0.265, 0.016),
        (0.268, 0.042),
        (0.250, 0.068),
        (0.215, 0.072),
        (0.188, 0.052),
        (0.000, 0.048),
    ]
    disc = lathe(prof, radial_segs=38, cap_bottom=True, cap_top=True, axis="y", name="button_disc", material="item_teal_button")
    parts.append(disc)

    # Four crisp dark recessed buttonhole wells arranged in a classic shirt-button square!
    for idx, (hx, hz) in enumerate(((-0.068, -0.068), (+0.068, -0.068), (-0.068, +0.068), (+0.068, +0.068))):
        parts.append(cylinder(0.026, 0.028, 0.014, radial_segs=16, axis="y", name=f"button_hole_{idx}", material="item_soil_hole").translate(hx, 0.047, hz))
        parts.append(torus_arc(0.028, 0.0045, u_segs=16, v_segs=6, plane="xz", name=f"button_hrim_{idx}", material="item_teal_button").translate(hx, 0.053, hz))

    sc.add("shtbut1", [p.rotate_xyz(ry=14.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.72)


def _sculpt_barbie_pump(prefix: str) -> List[MeshPart]:
    """Solid unified glossy hot-pink Barbie stiletto pump (local +X pointed toe, -X stiletto heel, +Y arch)."""
    # Arch elevation y_arch(x) rising cleanly from flat forefoot (+0.08..+0.28) up to elevated stiletto heel seat (-0.22)!
    def _arch_y(x: np.ndarray) -> np.ndarray:
        t = np.clip((0.12 - x) / 0.34, 0.0, 1.0)
        return 0.024 + 0.145 * (t ** 1.45) + 0.012 * np.clip((x - 0.14) / 0.14, 0.0, 1.0)

    def _pump_shell(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        x = p[:, 0]
        u = np.clip((x + 0.24) / 0.52, 0.0, 1.0)  # 0 heel back, 1 pointed toe tip
        # Sleek shoe width: rounded heel -> slender waist -> wider ball -> sharp pointed stiletto toe!
        width = 0.78 + 0.28 * np.sin(u * math.pi) - 0.18 * np.exp(-(((u - 0.38) / 0.16) ** 2))
        width *= 1.0 - 0.84 * (np.clip((u - 0.65) / 0.35, 0.0, 1.0) ** 1.25)
        p[:, 2] *= np.clip(width, 0.08, 1.15)
        # Sculpt top hollow scoop opening in the middle (leaving closed pointed toe vamp + curved heel counter cup!)
        in_opening = np.exp(-(((u - 0.36) / 0.22) ** 2)) * np.exp(-((p[:, 2] / 0.048) ** 2))
        top_mask = np.clip(p[:, 1] / 0.045, 0.0, 1.0)
        p[:, 1] = np.where(p[:, 1] < 0.0, p[:, 1] * 0.24, p[:, 1] * (0.95 - 0.65 * np.clip((u - 0.62) / 0.38, 0.0, 1.0)))
        # Raise heel counter lip at very back (u < 0.18), scoop deep foot opening cleanly through center!
        p[:, 1] += 0.036 * top_mask * np.exp(-((u / 0.16) ** 2))
        p[:, 1] -= 0.048 * top_mask * in_opening
        p[:, 1] += _arch_y(x)
        return p

    # Paint shaded deep-magenta inner footbed cleanly inside the scooped shoe opening!
    shoe = tint_part(
        superellipsoid(0.26, 0.068, 0.064, e1=0.68, e2=0.76, u_segs=42, v_segs=22, name=f"{prefix}_body", material="item_hot_pink")
        .deform(_pump_shell),
        lambda pos, nrm: np.clip(
            1.0 - 0.58 * np.exp(-(((pos[:, 0] + 0.05) / 0.11) ** 2)) * np.exp(-((pos[:, 2] / 0.036) ** 2)) * np.clip(nrm[:, 1], 0.0, 1.0),
            0.35,
            1.0,
        )[:, None] * np.array([[1.0, 1.0, 1.0]]),
    )
    # Slender tapered glossy hot-pink stiletto spike descending seamlessly from the heel seat!
    spike = curved_tube(
        [(-0.205, 0.172, 0.0), (-0.214, 0.090, 0.0), (-0.220, 0.008, 0.0)],
        [0.025, 0.014, 0.009],
        samples_per_seg=3,
        radial_segs=12,
        cap_rings=2,
        name=f"{prefix}_spike",
        material="item_hot_pink",
    )
    return [shoe, spike]


def build_broken1() -> SceneBuilder:
    """Hot-pink Barbie stiletto high-heel pump lying sideways horizontally (`287:broken1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_broken1")
    parts = _sculpt_barbie_pump("pump1")
    # Lying sideways on ground: stiletto spike pointing UP-LEFT, curved arch sweeping RIGHT (`287:broken1`)!
    sc.add("broken1", [p.rotate_xyz(rx=+82.0, ry=-6.0, rz=-10.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.90)


def build_broken2() -> SceneBuilder:
    """Hot-pink Barbie stiletto high-heel pump lying vertically with stiletto spike right (`288:broken2`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_broken2")
    parts = _sculpt_barbie_pump("pump2")
    # Vertical side-lie: heel counter TOP-LEFT, stiletto spike jutting RIGHT, curved arch & sharp toe pointing DOWN (`288:broken2`)!
    sc.add("broken2", [p.rotate_xyz(rx=+82.0, ry=-92.0, rz=+10.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.90)


def build_broken3() -> SceneBuilder:
    """Sleek silver metal spoon handle / paddle lying horizontally (`1334:broken3`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_broken3")

    # Authentic slender teardrop silver spoon handle: rounded paddle LEFT (-X) tapering cleanly to slim neck RIGHT (+X)!
    def _spoon_shape(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        u = np.clip((p[:, 0] + 0.38) / 0.76, 0.0, 1.0)
        width = 0.24 + 0.80 * np.sin(np.clip(u / 0.28, 0.0, 1.0) * math.pi * 0.5) * (1.0 - 0.72 * (u ** 0.75))
        p[:, 2] *= width
        p[:, 1] *= 0.52 + 0.32 * (1.0 - u)
        p[:, 1] += 0.052 * ((u - 0.35) ** 2) - 0.008 * np.exp(-((p[:, 2] / 0.040) ** 2)) * (1.0 - u)
        return p

    handle = tint_part(
        superellipsoid(0.38, 0.036, 0.072, e1=0.62, e2=0.70, u_segs=38, v_segs=18, name="spoon_handle", material="item_chrome_spring")
        .deform(_spoon_shape)
        .translate(0.0, 0.018, 0.0),
        _const_tint((0.88, 0.90, 0.96)),
    )
    sc.add("broken3", handle.rotate_xyz(ry=3.0, degrees=True))
    return _finalize_item(sc, target_span=0.92)
