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
        samples_per_seg=6,
        radial_segs=16,
        cap_rings=3,
        name=f"{name}_wire",
        material="item_chrome_polished",
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
    """Smooth high-res glossy plastic thumbtack push-pin with mirror-polished steel needle (`313..315:pin1..3`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    # High-res smoothly beveled hourglass plastic push-pin head (zero jagged low-poly facets!).
    prof = [
        (0.000, 0.000),
        (0.095, 0.000),
        (0.118, 0.004),
        (0.126, 0.014),
        (0.125, 0.028),
        (0.114, 0.041),
        (0.088, 0.050),
        (0.066, 0.064),
        (0.056, 0.092),
        (0.054, 0.128),
        (0.058, 0.168),
        (0.068, 0.204),
        (0.086, 0.228),
        (0.100, 0.246),
        (0.105, 0.264),
        (0.099, 0.279),
        (0.084, 0.289),
        (0.052, 0.294),
        (0.000, 0.296),
    ]
    cap = tint_part(
        lathe(prof, radial_segs=48, cap_bottom=True, cap_top=True, axis="y", name=f"{name}_cap", material="item_candy_white"),
        _const_tint(cap_rgb),
    )
    parts.append(cap)

    if bent_needle:
        # Smooth gleaming mirror-polished scimitar-bent steel needle curving cleanly down-left (`314:pin2`)!
        needle = curved_tube(
            [(0.0, 0.006, 0.0), (-0.014, -0.092, +0.008), (-0.048, -0.192, +0.015), (-0.066, -0.292, +0.016), (-0.056, -0.376, +0.011)],
            [0.0185, 0.0175, 0.0145, 0.0095, 0.0018],
            samples_per_seg=10,
            radial_segs=22,
            cap_rings=3,
            name=f"{name}_needle",
            material="item_chrome_polished",
        )
    else:
        # Smooth gleaming tapered stainless steel needle with crisp conical tip (`313:pin1`, `315:pin3`)!
        needle_prof = [
            (0.000, -needle_len),
            (0.0025, -needle_len + 0.012),
            (0.0155, -needle_len + 0.068),
            (0.0185, -0.010),
            (0.0185, +0.006),
            (0.000, +0.006),
        ]
        needle = lathe(needle_prof, radial_segs=24, cap_bottom=True, cap_top=True, axis="y", name=f"{name}_needle", material="item_chrome_polished")
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
    """Upside-down 21-crimp stamped tinplate crown bottle cap with 'YOU WIN!!!' liner (`316:botcap1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_botcap1")

    # Authentic 21-flute rounded crown bottle cap: 168 radial segments (8 smooth samples per crimp, zero sawblade spikes!),
    # compact vertical-ish flared skirt, thin rolled sheet-metal lip, and solid watertight stamped outer bottom.
    n_teeth = 21
    n_seg = n_teeth * 8  # 168 segments for silky rounded U-shaped crown scallops
    r_flat = 0.222
    r_base = 0.246
    r_rim = 0.268
    h_cup = 0.088
    wall_t = 0.0085
    flute_amp = 0.0115  # subtle realistic +/-4.3% rounded steel corrugation (not jagged spikes!)

    # Closed watertight cross-section: outer bottom center -> outer rounded shoulder -> outer fluted skirt ->
    # rolled top metal lip -> inner fluted skirt -> inner floor center!
    # Tuple: (mean_radius, y_height, flute_weight, rim_wave_weight)
    ring_specs = (
        (0.000,            0.000,         0.00, 0.00),
        (r_flat,           0.000,         0.00, 0.00),
        (r_base - 0.008,   0.007,         0.04, 0.00),
        (r_base,           0.020,         0.20, 0.00),
        (r_base + 0.008,   0.044,         0.58, 0.00),
        (r_rim - 0.004,    0.070,         0.92, 0.45),
        (r_rim,            h_cup - 0.004, 1.00, 1.00),
        (r_rim - 0.0025,   h_cup,         1.00, 1.00),
        (r_rim - wall_t + 0.0025, h_cup,  1.00, 1.00),
        (r_rim - wall_t,   h_cup - 0.004, 1.00, 1.00),
        (r_rim - wall_t - 0.004, 0.070,   0.92, 0.45),
        (r_base - wall_t + 0.008, 0.044,  0.58, 0.00),
        (r_base - wall_t,  0.022,         0.20, 0.00),
        (r_flat - wall_t,  0.012,         0.00, 0.00),
        (0.000,            0.012,         0.00, 0.00),
    )
    verts, uvs, cols, tris = [], [], [], []
    for ir, (rad, yv, flute_w, rim_w) in enumerate(ring_specs):
        vf = ir / float(len(ring_specs) - 1)
        for j in range(n_seg + 1):
            uf = j / float(n_seg)
            ang = uf * 2.0 * math.pi
            c_tooth = math.cos(ang * n_teeth)
            r_eff = rad + flute_amp * c_tooth * flute_w if rad > 1e-6 else 0.0
            # Subtle authentic crown-crimp edge wave along the rolled rim
            y_eff = yv - 0.0022 * c_tooth * rim_w
            verts.append((r_eff * math.cos(ang), y_eff, r_eff * math.sin(ang)))
            uvs.append((uf * 2.0, vf))
            # Subtle warm tinplate valley-to-crest AO so every rounded crimp scallop pops naturally
            ao = 0.84 + 0.16 * (0.5 + 0.5 * c_tooth) * flute_w + 0.08 * (1.0 - flute_w)
            cols.append((min(1.0, ao * 0.94), min(1.0, ao * 0.91), min(1.0, ao * 0.86)))
    stride = n_seg + 1
    for ir in range(len(ring_specs) - 1):
        for j in range(n_seg):
            a, b, c, d = ir * stride + j, (ir + 1) * stride + j, (ir + 1) * stride + (j + 1), ir * stride + (j + 1)
            tris.extend(((a, b, c), (a, c, d)))
    pos_c = np.asarray(verts, dtype=np.float32)
    idx_c = np.asarray(tris, dtype=np.uint32).reshape(-1)
    cup = MeshPart(
        "botcap_cup",
        "item_tin_aluminum",
        pos_c,
        recompute_smooth_normals(pos_c, idx_c),
        idx_c,
        uvs=np.asarray(uvs, dtype=np.float32),
        colors=np.asarray(cols, dtype=np.float32),
    )

    # Full-width printed 'YOU WIN!!!' white PVC plastisol liner + thin raised sealing gasket ring inside the cap!
    liner_r = r_base - wall_t - 0.005
    liner = cylinder(liner_r, liner_r, 0.010, radial_segs=48, bevel=0.002, axis="y", name="botcap_liner", material="item_cap_win").translate(0.0, 0.019, 0.0)
    u = np.clip(0.5 + liner.positions[:, 0] / (2.0 * liner_r * 1.02), 0.0, 1.0)
    v = np.clip(0.5 + liner.positions[:, 2] / (2.0 * liner_r * 1.02), 0.0, 1.0)
    liner.uvs = np.column_stack([u, v]).astype(np.float32)
    gasket = torus_arc(liner_r - 0.012, 0.0065, u_segs=48, v_segs=10, plane="xz", name="botcap_gasket", material="item_wireless_shell").translate(0.0, 0.025, 0.0)

    sc.add("botcap1", [cup, liner, gasket])
    return _finalize_item(sc, target_span=0.80)


def _build_hex_nut(name: str, *, rot_deg: Tuple[float, float, float]) -> SceneBuilder:
    """Smooth CNC-machined 6-sided polished steel hex nut with crisp beveled faces & threaded bore (`317:bolt1`, `318:bolt2`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    # Build cleanly separated smooth faces (6 crisp flat hex sidewalls, smooth top/bottom washers + beveled chamfers,
    # and smooth circular inner bore) using mirror-smooth `item_chrome_polished` — ZERO streaky metal lines!
    n_ang = 48  # 8 segments per hex flat for crisp corners + silky-smooth circular bore
    r_out_flat = 0.250
    r_in = 0.118
    h_half = 0.112
    cham = 0.022

    def _hex_r(theta: float) -> float:
        a = ((theta + math.pi / 6.0) % (math.pi / 3.0)) - math.pi / 6.0
        return r_out_flat / max(0.5, math.cos(a))

    def _band_mesh(tag: str, r0_spec: Tuple[str, float, float], r1_spec: Tuple[str, float, float], *, flat_hex: bool = False) -> MeshPart:
        verts: List[Tuple[float, float, float]] = []
        nrms: List[Tuple[float, float, float]] = []
        tris: List[Tuple[int, int, int]] = []
        k0, s0, y0 = r0_spec
        k1, s1, y1 = r1_spec
        if flat_hex:
            # 6 independent flat hex sidewalls with exact crisp planar normals per face!
            for f in range(6):
                a0 = f * (math.pi / 3.0) - math.pi / 6.0
                a1 = a0 + (math.pi / 3.0)
                amid = 0.5 * (a0 + a1)
                fnx, fnz = math.cos(amid), math.sin(amid)
                base = len(verts)
                for ang in (a0, a1):
                    rr0 = _hex_r(ang) * s0
                    rr1 = _hex_r(ang) * s1
                    verts.append((rr0 * math.cos(ang), y0, rr0 * math.sin(ang)))
                    verts.append((rr1 * math.cos(ang), y1, rr1 * math.sin(ang)))
                    nrms.append((fnx, 0.0, fnz))
                    nrms.append((fnx, 0.0, fnz))
                tris.extend(((base, base + 1, base + 3), (base, base + 3, base + 2)))
            pos_a = np.asarray(verts, dtype=np.float32)
            idx_a = np.asarray(tris, dtype=np.uint32).reshape(-1)
            nrm_a = np.asarray(nrms, dtype=np.float32)
            return MeshPart(f"{name}_{tag}", "item_chrome_polished", pos_a, nrm_a, idx_a)

        for ir, (kind, scale_r, yv) in enumerate((r0_spec, r1_spec)):
            for j in range(n_ang + 1):
                ang = (j / float(n_ang)) * 2.0 * math.pi
                rad = (_hex_r(ang) * scale_r) if kind == "hex" else scale_r
                verts.append((rad * math.cos(ang), yv, rad * math.sin(ang)))
        st = n_ang + 1
        for j in range(n_ang):
            a, b, c, d = j, st + j, st + j + 1, j + 1
            tris.extend(((a, b, c), (a, c, d)))
        pos_a = np.asarray(verts, dtype=np.float32)
        idx_a = np.asarray(tris, dtype=np.uint32).reshape(-1)
        return MeshPart(f"{name}_{tag}", "item_chrome_polished", pos_a, recompute_smooth_normals(pos_a, idx_a), idx_a)

    steel_tint = _const_tint((0.86, 0.88, 0.92))
    bands = [
        _band_mesh("bore", ("in", r_in, +h_half - 0.010), ("in", r_in, -h_half + 0.010)),
        _band_mesh("in_ch_b", ("in", r_in, -h_half + 0.010), ("in", r_in + 0.012, -h_half)),
        _band_mesh("bot_face", ("in", r_in + 0.012, -h_half), ("hex", 0.93, -h_half)),
        _band_mesh("bot_ch", ("hex", 0.93, -h_half), ("hex", 1.00, -h_half + cham)),
        _band_mesh("hex_wall", ("hex", 1.00, -h_half + cham), ("hex", 1.00, +h_half - cham), flat_hex=True),
        _band_mesh("top_ch", ("hex", 1.00, +h_half - cham), ("hex", 0.93, +h_half)),
        _band_mesh("top_face", ("hex", 0.93, +h_half), ("in", r_in + 0.012, +h_half)),
        _band_mesh("in_ch_t", ("in", r_in + 0.012, +h_half), ("in", r_in, +h_half - 0.010)),
    ]
    parts.extend(tint_part(b, steel_tint) for b in bands)

    # Crisp machined internal screw thread ridges inside the central bore!
    for k in range(5):
        ty = -0.072 + k * 0.036
        parts.append(torus_arc(r_in - 0.004, 0.0075, u_segs=36, v_segs=8, plane="xz", name=f"{name}_thread_{k}", material="item_steel_dark").translate(0.0, ty, 0.0))

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
    """Long slender countersunk Phillips '+' steel wood screw lying diagonally (`319:screw1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_screw1")
    parts: List[MeshPart] = []

    # Build along Z: sharp tip at -Z (-0.36), flared countersunk Phillips head at +Z (+0.34)!
    shank = cylinder(0.004, 0.044, 0.64, radial_segs=24, axis="z", name="screw1_shank", material="item_chrome_polished").translate(0.0, 0.0, -0.02)
    head = cylinder(0.044, 0.096, 0.072, radial_segs=32, bevel=0.005, axis="z", name="screw1_head", material="item_chrome_polished").translate(0.0, 0.0, +0.325)
    # Crisp recessed Phillips '+' cross-drive on the flat countersunk head!
    cross_a = rounded_box(0.118, 0.022, 0.018, bevel=0.003, segs=2, name="screw1_cross_a", material="item_steel_dark").translate(0.0, 0.0, +0.360)
    cross_b = rounded_box(0.022, 0.118, 0.018, bevel=0.003, segs=2, name="screw1_cross_b", material="item_steel_dark").translate(0.0, 0.0, +0.360)
    cross_core = cylinder(0.024, 0.024, 0.018, radial_segs=16, axis="z", name="screw1_cross_c", material="item_steel_dark").translate(0.0, 0.0, +0.361)
    parts.extend([shank, head, cross_a, cross_b, cross_core])

    # Crisp diagonal machined screw thread coils along the entire tapering shaft!
    n_t = 15
    for k in range(n_t):
        u = k / float(n_t - 1)
        tz = -0.29 + u * 0.54
        tr = 0.014 + 0.036 * (u ** 0.72)
        ring = torus_arc(tr, 0.0085, u_segs=24, v_segs=8, plane="xy", name=f"screw1_th_{k}", material="item_chrome_polished").rotate_xyz(ry=0.14).translate(0.0, 0.0, tz)
        parts.append(ring)

    # Diagonal pose matching `319:screw1`: pointed tip UP-LEFT, flared countersunk Phillips head DOWN-RIGHT!
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
    """Glossy teal-green 4-hole round shirt button with a proud stepped raised outer rim & deep dish (`322:shtbut1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_shtbut1")
    parts: List[MeshPart] = []

    # Classic 4-hole dress-shirt button: proud sculpted raised outer rim (`y=0.098`) stepping cleanly down
    # into a deep recessed central dish (`y=0.044`) with crisp rim bevels so the raised edge reads unmistakably!
    prof = [
        (0.000, 0.000),
        (0.232, 0.000),
        (0.258, 0.012),
        (0.270, 0.034),
        (0.270, 0.068),
        (0.264, 0.088),
        (0.248, 0.098),
        (0.226, 0.098),
        (0.213, 0.088),
        (0.204, 0.056),
        (0.194, 0.048),
        (0.148, 0.046),
        (0.000, 0.044),
    ]
    disc = lathe(prof, radial_segs=64, cap_bottom=False, cap_top=False, axis="y", name="button_disc", material="item_teal_button")

    def _button_rim_ao(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
        r = np.hypot(pos[:, 0], pos[:, 2])
        # Subtle crevice AO tucked right into the inner stepped rim groove (`r ~ 0.194..0.214`), luminous raised rim top!
        groove = np.exp(-((r - 0.200) / 0.014) ** 2) * np.clip((pos[:, 1] - 0.030) / 0.035, 0.0, 1.0) * (1.0 - np.clip((pos[:, 1] - 0.072) / 0.020, 0.0, 1.0))
        rim_crest = np.exp(-((r - 0.238) / 0.026) ** 2) * np.clip((pos[:, 1] - 0.078) / 0.020, 0.0, 1.0)
        shade = np.clip(0.94 - 0.26 * groove + 0.08 * rim_crest + 0.04 * nrm[:, 1], 0.62, 1.05)
        return np.column_stack([shade * 0.96, shade * 1.00, shade * 0.98])

    parts.append(tint_part(disc, _button_rim_ao))

    # Four crisp smooth dark countersunk buttonholes flush inside the recessed center dish (visible top & bottom!).
    for idx, (hx, hz) in enumerate(((-0.074, -0.074), (+0.074, -0.074), (-0.074, +0.074), (+0.074, +0.074))):
        parts.append(cylinder(0.032, 0.033, 0.0465, radial_segs=22, axis="y", name=f"button_hole_{idx}", material="item_hair_black").translate(hx, 0.0230, hz))
        parts.append(torus_arc(0.035, 0.0038, u_segs=22, v_segs=8, plane="xz", name=f"button_chamfer_{idx}", material="item_teal_button").translate(hx, 0.0452, hz))

    sc.add("shtbut1", [p.rotate_xyz(ry=14.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.72)


def _sculpt_barbie_pump(prefix: str) -> List[MeshPart]:
    """Authentic hollow wearable glossy hot-pink stiletto high-heel pump (arched sole, hollow cavity, vamp, heel cup & stiletto!)."""
    # Anatomical shoe centerline u in [0, 1]: u=0 back of heel (x=-0.22), u=1 pointed stiletto toe tip (x=+0.28).
    def _sole_xyw(u: float) -> Tuple[float, float, float]:
        x = -0.220 + 0.500 * u
        # S-curved stiletto arch: elevated heel seat (0.170) -> sweeping graceful arch -> flat ball -> slight toe rocker!
        t_arch = max(0.0, min(1.0, (0.64 - u) / 0.50))
        y_arch = 0.012 + 0.158 * (t_arch * t_arch * (3.0 - 2.0 * t_arch)) + 0.016 * (max(0.0, (u - 0.78) / 0.22) ** 1.6)
        # Anatomical woman's pump half-width: snug rounded heel -> sleek narrow waist -> fuller ball -> pointed almond toe!
        if u < 0.06:
            w = 0.050 * math.sqrt(max(0.0, u / 0.06))
        elif u < 0.68:
            t = (u - 0.06) / 0.62
            w = 0.050 - 0.011 * math.sin(t * math.pi) + 0.018 * (t ** 1.8)
        else:
            t = (u - 0.68) / 0.32
            w = 0.068 * ((1.0 - t) ** 0.74)
        return x, y_arch, max(0.002, w)

    # 1. Anatomical S-curved solid shoe outsole + soft rose-tinted interior footbed insole!
    n_u, n_c = 44, 16
    s_verts: List[Tuple[float, float, float]] = []
    s_tris: List[Tuple[int, int, int]] = []
    for iu in range(n_u + 1):
        u = iu / float(n_u)
        x, y0, hw = _sole_xyw(u)
        thick = 0.014 if u < 0.68 else 0.011 * (1.0 - 0.35 * ((u - 0.68) / 0.32))
        for ic in range(n_c):
            ang = 2.0 * math.pi * ic / float(n_c)
            zz = hw * math.cos(ang)
            yy = y0 + (0.003 * math.sin(ang) if math.sin(ang) >= 0 else thick * math.sin(ang))
            s_verts.append((x, yy, zz))
    for iu in range(n_u):
        for ic in range(n_c):
            icn = (ic + 1) % n_c
            a, b = iu * n_c + ic, (iu + 1) * n_c + ic
            c, d = (iu + 1) * n_c + icn, iu * n_c + icn
            s_tris.extend(((a, c, b), (a, d, c)))
    pos_s = np.asarray(s_verts, dtype=np.float32)
    idx_s = np.asarray(s_tris, dtype=np.uint32).reshape(-1)
    sole = tint_part(
        MeshPart(f"{prefix}_sole", "item_hot_pink", pos_s, recompute_smooth_normals(pos_s, idx_s), idx_s),
        lambda pos, nrm: np.where(
            (nrm[:, 1] > 0.35)[:, None],
            np.array([[0.74, 0.30, 0.48]]),   # shaded cushioned rose insole inside the hollow shoe
            np.array([[0.92, 0.24, 0.56]]),   # glossy hot-pink outer sole & arch
        ),
    )

    # 2. GENUINELY HOLLOW DOUBLE-WALLED UPPER: cupped heel counter + low-cut side quarters + vaulted open-throat toe vamp!
    # Parameterize the perimeter around the shoe sole: `s in [0, 2*pi)` (0=toe tip, pi/2=right side, pi=heel back, 3pi/2=left side)
    n_p = 72
    wall_t = 0.0085
    # 5 cross-section stations from outer sole -> outer upper rim -> rounded top lip -> inner upper rim -> inner sole!
    # Plus for the closed front toe box (`|s| < s_vamp`), arch the top right over the centerline so the toe is a hollow dome!
    u_verts: List[Tuple[float, float, float]] = []
    u_tris: List[Tuple[int, int, int]] = []
    # 2A: Hollow open-top side quarters & sculpted heel counter wrapping cleanly around `u in [0.00, 0.72]`!
    # Build perimeter parameter `tau in [-1, +1]` where `-1` is left front edge (`u=0.66`), `0` is back of heel (`u=0.00`), `+1` is right front edge (`u=0.66`)!
    n_q = 48
    # Cross-section around thin curved wall: outer bottom -> outer mid -> top outer lip -> top inner lip -> inner mid -> inner bottom
    q_prof = (
        (0.00, 0.000, +1.0),
        (0.50, 0.002, +1.0),
        (0.95, 0.001, +1.0),
        (1.00, -wall_t * 0.45, +1.0),
        (0.95, -wall_t, +1.0),
        (0.50, -wall_t, +1.0),
        (0.00, -wall_t, +1.0),
    )
    q_verts: List[Tuple[float, float, float]] = []
    q_tris: List[Tuple[int, int, int]] = []
    for iq in range(n_q + 1):
        tau = -1.0 + 2.0 * (iq / float(n_q))  # -1 left front -> 0 heel back -> +1 right front
        at = abs(tau)
        # Map `at` smoothly around heel semicircle then straight forward along left/right sole edge to `u = 0.68`
        if at < 0.24:
            ang_h = (tau / 0.24) * (math.pi * 0.5)  # -pi/2..+pi/2 around heel back
            u_loc = 0.06 * (1.0 - math.cos(ang_h))
            x0, y0, hw = _sole_xyw(max(0.004, u_loc))
            z_edge = hw * math.sin(ang_h)
            nx_in, nz_in = math.cos(ang_h), -math.sin(ang_h)
        else:
            u_loc = 0.06 + ((at - 0.24) / 0.76) * 0.62
            x0, y0, hw = _sole_xyw(u_loc)
            sgn = 1.0 if tau > 0 else -1.0
            z_edge = sgn * hw
            nx_in, nz_in = 0.0, -sgn
        # Sculpted pump collar silhouette: high cupped heel counter (0.095) swooping smoothly into the vamp without any step!
        heel_cup = 0.092 * math.exp(-((u_loc / 0.22) ** 2))
        vamp_tuck = 1.0 - 0.65 * max(0.0, (u_loc - 0.56) / 0.12)
        h_rim = (0.024 + heel_cup) * max(0.28, vamp_tuck)
        # Subtle inward lean at top of heel counter & quarters so it hugs an ankle realistically!
        for hf, in_off, _ in q_prof:
            lean = 0.010 * (hf ** 1.4) * (0.45 + 0.55 * math.exp(-((u_loc / 0.25) ** 2))) + 0.004 * max(0.0, (u_loc - 0.55) / 0.13)
            d_in = lean - in_off
            px = x0 + nx_in * d_in
            pz = z_edge + nz_in * d_in
            py = y0 - 0.004 + hf * h_rim
            q_verts.append((px, py, pz))
    st_q = len(q_prof)
    for iq in range(n_q):
        for ip in range(st_q - 1):
            a, b = iq * st_q + ip, (iq + 1) * st_q + ip
            c, d = (iq + 1) * st_q + (ip + 1), iq * st_q + (ip + 1)
            q_tris.extend(((a, b, c), (a, c, d)))
    pos_q = np.asarray(q_verts, dtype=np.float32)
    idx_q = np.asarray(q_tris, dtype=np.uint32).reshape(-1)
    quarters = MeshPart(f"{prefix}_quarters", "item_hot_pink", pos_q, recompute_smooth_normals(pos_q, idx_q), idx_q)

    # 2B: Hollow vaulted pointed toe-box vamp hood (`u in [0.58, 1.00]`) with crisp U-shaped open throat neckline!
    n_vu, n_va = 22, 22
    v_verts: List[Tuple[float, float, float]] = []
    v_tris: List[Tuple[int, int, int]] = []
    # Build BOTH outer shell and offset inner shell, stitched at the front throat neckline & sole edges!
    for shell_idx, thick_off in enumerate((0.0, -wall_t)):
        base_s = len(v_verts)
        for iu in range(n_vu + 1):
            t_u = iu / float(n_vu)  # 0 at throat opening, 1 at pointed toe tip
            for ia in range(n_va + 1):
                # `ang` from 0 (right sole edge) over top arch (pi/2) to pi (left sole edge)
                ang = math.pi * (ia / float(n_va))
                sa, ca = math.sin(ang), math.cos(ang)
                # U-shaped low-cut throat neckline sweeps slightly back on the sides (`u=0.58`) and dips forward at top center (`u=0.66`)!
                u_throat = 0.575 + 0.088 * (sa ** 1.6)
                u_loc = u_throat + (1.0 - u_throat) * t_u
                x0, y0, hw = _sole_xyw(min(0.999, u_loc))
                # Sleek arched toe-box roof height tapering gracefully toward pointed toe tip!
                h_vamp = (0.068 * ((1.0 - t_u) ** 0.58) + 0.012 * (1.0 - t_u))
                r_z = max(0.001, hw + thick_off * abs(ca))
                r_y = max(0.001, h_vamp + thick_off * sa)
                px = x0 + thick_off * 0.45 * (t_u ** 2)
                py = y0 - 0.003 + r_y * (sa ** 0.88)
                pz = r_z * ca
                v_verts.append((px, py, pz))
        st_v = n_va + 1
        for iu in range(n_vu):
            for ia in range(n_va):
                a, b = base_s + iu * st_v + ia, base_s + (iu + 1) * st_v + ia
                c, d = base_s + (iu + 1) * st_v + (ia + 1), base_s + iu * st_v + (ia + 1)
                if shell_idx == 0:
                    v_tris.extend(((a, c, b), (a, d, c)))
                else:
                    v_tris.extend(((a, b, c), (a, c, d)))
    # Stitch the crisp rolled throat edge (`iu = 0`) between outer & inner vamp shells so it's 100% solid & smooth!
    st_v = n_va + 1
    inner_off = (n_vu + 1) * st_v
    for ia in range(n_va):
        o0, o1 = ia, ia + 1
        i0, i1 = inner_off + ia, inner_off + ia + 1
        v_tris.extend(((o0, o1, i1), (o0, i1, i0)))
    pos_v = np.asarray(v_verts, dtype=np.float32)
    idx_v = np.asarray(v_tris, dtype=np.uint32).reshape(-1)
    vamp = MeshPart(f"{prefix}_vamp", "item_hot_pink", pos_v, recompute_smooth_normals(pos_v, idx_v), idx_v)

    # 3. Sculpted hourglass stiletto heel flaring organically out of the heel seat + crisp dark heel tap!
    heel_pts = [
        (-0.192, 0.168, 0.0),
        (-0.198, 0.145, 0.0),
        (-0.205, 0.105, 0.0),
        (-0.212, 0.056, 0.0),
        (-0.216, 0.012, 0.0),
    ]
    heel = curved_tube(
        heel_pts,
        [0.036, 0.025, 0.016, 0.0115, 0.010],
        samples_per_seg=4,
        radial_segs=18,
        cap_rings=2,
        name=f"{prefix}_heel",
        material="item_hot_pink",
    )
    heel_tap = cylinder(0.0102, 0.0102, 0.012, radial_segs=16, bevel=0.002, axis="y", name=f"{prefix}_tap", material="item_rubber_dark").translate(-0.216, 0.006, 0.0)
    return [sole, quarters, vamp, heel, heel_tap]


def build_broken1() -> SceneBuilder:
    """Hot-pink Barbie stiletto high-heel pump lying sideways horizontally (`287:broken1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_broken1")
    parts = _sculpt_barbie_pump("pump1")
    # Lying sideways on ground: stiletto spike pointing UP-LEFT, open hollow cavity & curved arch sweeping RIGHT (`287:broken1`)!
    sc.add("broken1", [p.rotate_xyz(rx=+82.0, ry=-6.0, rz=-10.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.90)


def build_broken2() -> SceneBuilder:
    """Hot-pink Barbie stiletto high-heel pump lying vertically with stiletto spike right (`288:broken2`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_broken2")
    parts = _sculpt_barbie_pump("pump2")
    # Vertical side-lie: heel counter TOP-LEFT, stiletto spike jutting RIGHT, hollow opening & sharp toe pointing DOWN (`288:broken2`)!
    sc.add("broken2", [p.rotate_xyz(rx=+82.0, ry=-92.0, rz=+10.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.90)


def build_silver_spoon_handle(name: str, *, length: float = 0.56) -> MeshPart:
    """Gleaming mirror-polished stamped silver spoon paddle handle (`1334:broken3` & `1331..1333:fdcer*` shared geometry!)."""
    # Runs along local X from `-length` (rounded teardrop paddle tip on LEFT) to `0.0` (slender tapered neck join on RIGHT)!
    n_u, n_c = 36, 18
    verts: List[Tuple[float, float, float]] = []
    tris: List[Tuple[int, int, int]] = []
    for iu in range(n_u + 1):
        u = iu / float(n_u)  # 0 at paddle tip (-length), 1 at slender neck (0.0)
        x = -length * (1.0 - u)
        # Authentic stainless teaspoon handle contour: smoothly ROUNDED semicircular paddle tip tapering cleanly to slim neck!
        tip_round = math.sqrt(max(0.0, min(1.0, 1.0 - ((max(0.0, 0.14 - u) / 0.14) ** 2))))
        hw = (0.052 * (1.0 - 0.72 * (u ** 0.72)) + 0.011) * max(0.06, tip_round)
        th = (0.0105 * (1.0 - 0.30 * u)) * max(0.28, tip_round)
        # Graceful classic flatware S-arch rising gently at paddle end and curving down toward the spoon neck
        y_mid = 0.036 * ((1.0 - u) ** 1.4) + 0.012 * math.sin(u * math.pi)
        for ic in range(n_c):
            ang = 2.0 * math.pi * ic / float(n_c)
            ca, sa = math.cos(ang), math.sin(ang)
            zz = hw * ca
            # Subtle stamped recessed center bead along upper surface (`sa > 0`) for crisp mirror highlight!
            dish = -0.0022 * (1.0 - u) * max(0.0, sa) * (1.0 - ca * ca)
            yy = y_mid + th * sa + dish
            verts.append((x, yy, zz))
    for iu in range(n_u):
        for ic in range(n_c):
            icn = (ic + 1) % n_c
            a, b = iu * n_c + ic, (iu + 1) * n_c + ic
            c, d = (iu + 1) * n_c + icn, iu * n_c + icn
            tris.extend(((a, c, b), (a, d, c)))
    # Cap both ends cleanly
    c0, c1 = len(verts), len(verts) + 1
    verts.append((-length, 0.022, 0.0))
    verts.append((0.0, 0.0, 0.0))
    for ic in range(n_c):
        icn = (ic + 1) % n_c
        tris.append((c0, ic, icn))
        base = n_u * n_c
        tris.append((c1, base + icn, base + ic))
    pos_h = np.asarray(verts, dtype=np.float32)
    idx_h = np.asarray(tris, dtype=np.uint32).reshape(-1)
    return tint_part(
        MeshPart(name, "item_chrome_polished", pos_h, recompute_smooth_normals(pos_h, idx_h), idx_h),
        _const_tint((0.92, 0.94, 0.98)),
    )


def build_broken3() -> SceneBuilder:
    """Sleek mirror-silver spoon handle matching the spilled cereal bowl spoon (`1334:broken3`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_broken3")
    handle = build_silver_spoon_handle("spoon_handle", length=0.72).translate(+0.36, 0.012, 0.0)
    sc.add("broken3", handle.rotate_xyz(ry=2.0, degrees=True))
    return _finalize_item(sc, target_span=0.92)


def _wireless_shell_patch(
    x_grid: np.ndarray,
    y_grid: np.ndarray,
    z_grid: np.ndarray,
    y_bot_grid: np.ndarray,
    *,
    name: str,
    material: str,
) -> MeshPart:
    """Build a watertight beveled top-deck panel from a parametric (Nu, Nv) surface grid."""
    nu, nv = x_grid.shape
    p_top = np.stack([x_grid, y_grid, z_grid], axis=2)
    p_bot = np.stack([x_grid * 0.972, y_bot_grid, z_grid * 0.988], axis=2)
    n_top = nu * nv
    pos = np.vstack([p_top.reshape(-1, 3), p_bot.reshape(-1, 3)]).astype(np.float32)

    ii = np.arange(nu - 1, dtype=np.uint32)[:, None]
    jj = np.arange(nv - 1, dtype=np.uint32)[None, :]
    a0 = ii * nv + jj
    a1 = (ii + 1) * nv + jj
    b0 = ii * nv + (jj + 1)
    b1 = (ii + 1) * nv + (jj + 1)
    tris_top = np.stack([a0, a1, b1, a0, b1, b0], axis=2).reshape(-1, 3)
    tris_bot = np.stack([a0 + n_top, b1 + n_top, a1 + n_top, a0 + n_top, b0 + n_top, b1 + n_top], axis=2).reshape(-1, 3)

    j_e = np.arange(nv - 1, dtype=np.uint32)
    i_e = np.arange(nu - 1, dtype=np.uint32)
    f0, f1 = j_e, j_e + 1
    r0, r1 = (nu - 1) * nv + j_e, (nu - 1) * nv + j_e + 1
    l0, l1 = i_e * nv, (i_e + 1) * nv
    g0, g1 = i_e * nv + (nv - 1), (i_e + 1) * nv + (nv - 1)
    walls = np.vstack([
        np.stack([f0, f1, f1 + n_top, f0, f1 + n_top, f0 + n_top], axis=1).reshape(-1, 3),
        np.stack([r0, r1 + n_top, r1, r0, r0 + n_top, r1 + n_top], axis=1).reshape(-1, 3),
        np.stack([l0 + n_top, l1 + n_top, l1, l0 + n_top, l1, l0], axis=1).reshape(-1, 3),
        np.stack([g0, g1, g1 + n_top, g0, g1 + n_top, g0 + n_top], axis=1).reshape(-1, 3),
    ])
    idx = np.vstack([tris_top, tris_bot, walls]).reshape(-1).astype(np.uint32)
    nrm = recompute_smooth_normals(pos, idx)
    return MeshPart(name=name, material=material, positions=pos, normals=nrm, indices=idx)


def build_mouse1() -> SceneBuilder:
    """Sleek cordless ergonomic wireless mouse with sculpted split click buttons, scroll wheel & optical underside (`243:mouse1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_mouse1")
    parts: List[MeshPart] = []
    half_l, half_w0 = 0.42, 0.256

    def _mouse_half_w(u: np.ndarray) -> np.ndarray:
        t = 2.0 * np.clip(u, 0.0, 1.0) - 1.0
        pwr = np.where(t < 0.0, 2.30, 2.00)
        env = np.sqrt(np.maximum(0.0, 1.0 - np.abs(t) ** pwr))
        waist = 0.90 - 0.08 * np.exp(-((u - 0.40) / 0.16) ** 2) + 0.12 * np.exp(-((u - 0.70) / 0.24) ** 2)
        return half_w0 * env * waist

    def _mouse_belt_y(u: np.ndarray) -> np.ndarray:
        return 0.068 + 0.022 * np.sin(math.pi * np.clip(u, 0.0, 1.0))

    def _mouse_top_y(u: np.ndarray, vx: np.ndarray) -> np.ndarray:
        uc = np.clip(u, 0.0, 1.0)
        spine = 0.070 + 0.205 * (np.sin(math.pi * (uc ** 0.80)) ** 0.86)
        yb = _mouse_belt_y(uc)
        crown = np.power(np.maximum(0.0, 1.0 - (0.94 * np.clip(vx, -1.0, 1.0)) ** 2), 0.56)
        btn_mask = np.exp(-((uc - 0.22) / 0.13) ** 2)
        dimple = 0.012 * btn_mask * (np.exp(-((vx + 0.48) / 0.24) ** 2) + np.exp(-((vx - 0.48) / 0.24) ** 2))
        return yb + (spine - yb) * crown - dimple

    # 1. Contoured matte-charcoal lower chassis with subtle ergonomic thumb recess & fine side-grip ribs (backs all button seams!).
    def _chassis_deform(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        u = np.clip((p[:, 2] + half_l) / (2.0 * half_l), 0.0, 1.0)
        hw = np.maximum(_mouse_half_w(u), 0.024)
        nx = np.clip(p[:, 0] / half_w0, -1.0, 1.0)
        yb = _mouse_belt_y(u)
        yt = _mouse_top_y(u, nx) - 0.0055
        bot_taper = np.where(p[:, 1] < 0.0, 0.84 + 0.16 * (1.0 + p[:, 1] / 0.14), 1.0)
        # Subtle ergonomic left-side thumb contour + gentle tactile horizontal grip grooves flush on lower flanks.
        thumb_scoop = np.where(nx < -0.60, 0.014 * np.exp(-((u - 0.46) / 0.20) ** 2) * np.clip((-nx - 0.60) / 0.40, 0.0, 1.0), 0.0)
        side_band = (p[:, 1] < 0.0) & (np.abs(nx) > 0.78) & (u > 0.22) & (u < 0.74)
        grip_rib = np.where(side_band, 0.0022 * np.sin(u * 120.0), 0.0)
        p[:, 0] = nx * (hw * bot_taper + grip_rib) + thumb_scoop
        p[:, 1] = np.where(p[:, 1] >= 0.0, yb + (yt - yb) * np.clip(p[:, 1] / 0.14, 0.0, 1.0), 0.010 + (yb - 0.010) * np.clip(1.0 + p[:, 1] / 0.14, 0.0, 1.0))
        return p

    parts.append(superellipsoid(half_w0, 0.14, half_l, e1=0.68, e2=0.74, u_segs=56, v_segs=28, name="mouse_chassis", material="item_leather_smooth").deform(_chassis_deform))

    # 2. Molded pearl-ivory rear palm dome (u in [0.452, 0.988]) with smooth beveled front parting lip.
    u_palm = np.linspace(0.452, 0.988, 22)
    v_palm = np.linspace(-0.98, +0.98, 28)
    ug, vg = np.meshgrid(u_palm, v_palm, indexing="ij")
    hw_g = np.maximum(_mouse_half_w(ug), 0.022)
    xg, zg = vg * hw_g, -half_l + 2.0 * half_l * ug
    yg = _mouse_top_y(ug, vg) + 0.004 - np.exp(-((ug - 0.452) / 0.016) ** 2) * 0.0045 - np.power(np.abs(vg), 10.0) * 0.006
    parts.append(_wireless_shell_patch(xg, yg, zg, np.maximum(_mouse_belt_y(ug) - 0.004, yg - 0.016), name="mouse_palm", material="item_wireless_shell"))

    # 3. Generous sculpted split Left & Right click paddles (u in [0.012, 0.436]) wrapping cleanly ahead of the recessed scroll well!
    u_btn = np.linspace(0.012, 0.436, 26)
    s_btn = np.linspace(0.0, 1.0, 18)
    ub, sb = np.meshgrid(u_btn, s_btn, indexing="ij")
    hw_b = np.maximum(_mouse_half_w(ub), 0.030)
    # Crisp pill-shaped cutout centered around scroll wheel (u in [0.135, 0.385]); tight 0.009 centerline slit at very front tips!
    well_t = np.clip((ub - 0.260) / 0.132, -1.0, 1.0)
    well_half_x = 0.0048 + 0.0362 * np.power(np.maximum(0.0, 1.0 - well_t * well_t), 0.42)
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        x_in = np.minimum(well_half_x, hw_b * 0.62)
        x_abs = x_in + sb * (hw_b * 0.985 - x_in)
        xb = side * x_abs
        vxb = np.clip(xb / hw_b, -1.0, 1.0)
        zb = -half_l + 2.0 * half_l * ub
        yb_top = _mouse_top_y(ub, vxb) + 0.0045
        edge_drop = 0.0042 * (
            np.exp(-((ub - 0.436) / 0.016) ** 2)
            + np.exp(-((ub - 0.012) / 0.020) ** 2)
            + np.exp(-(sb / 0.08) ** 2)
            + np.power(sb, 10.0)
        )
        yb_top = yb_top - edge_drop
        if side < 0:
            xb, yb_top, zb, ub_use = xb[:, ::-1], yb_top[:, ::-1], zb[:, ::-1], ub[:, ::-1]
        else:
            ub_use = ub
        parts.append(_wireless_shell_patch(xb, yb_top, zb, np.maximum(_mouse_belt_y(ub_use) - 0.004, yb_top - 0.015), name=f"mouse_btn_{tag}", material="item_wireless_shell"))

    # 4. Recessed scroll wheel tray, brushed-chrome & knurled-rubber scroll wheel, sleek DPI pill button & emerald wireless LED!
    wheel_z = -half_l + 2.0 * half_l * 0.245
    wheel_y = float(_mouse_top_y(np.array([0.245]), np.array([0.0]))[0]) - 0.012
    parts.append(rounded_box(0.056, 0.016, 0.148, bevel=0.005, segs=2, name="mouse_wheel_well", material="item_hair_black").rotate_xyz(rx=+0.15).translate(0.0, wheel_y - 0.002, wheel_z + 0.022))
    parts.append(cylinder(0.042, 0.042, 0.036, radial_segs=24, bevel=0.004, axis="x", name="mouse_wheel_hub", material="item_chrome_polished").translate(0.0, wheel_y, wheel_z))
    parts.append(cylinder(0.044, 0.044, 0.026, radial_segs=24, bevel=0.003, axis="x", name="mouse_wheel_tire", material="item_rubber_dark").translate(0.0, wheel_y, wheel_z))
    for ri in range(20):
        ra = ri * (2.0 * math.pi / 20.0)
        parts.append(rounded_box(0.025, 0.0045, 0.0045, bevel=0.001, segs=1, name=f"mouse_wrib_{ri}", material="item_hair_black").rotate_xyz(rx=ra).translate(0.0, wheel_y + 0.044 * math.cos(ra), wheel_z + 0.044 * math.sin(ra)))
    dpi_z = -half_l + 2.0 * half_l * 0.362
    dpi_y = float(_mouse_top_y(np.array([0.362]), np.array([0.0]))[0]) - 0.001
    parts.append(rounded_box(0.026, 0.012, 0.048, bevel=0.004, segs=2, name="mouse_dpi_btn", material="item_chrome_polished").rotate_xyz(rx=+0.11).translate(0.0, dpi_y, dpi_z))
    led_z = -half_l + 2.0 * half_l * 0.492
    led_y = float(_mouse_top_y(np.array([0.492]), np.array([0.0]))[0]) + 0.0030
    parts.append(cylinder(0.0095, 0.0095, 0.006, radial_segs=14, axis="y", name="mouse_led_ring", material="item_hair_black").translate(0.0, led_y, led_z))
    parts.append(sphere(0.0065, u_segs=12, v_segs=8, name="mouse_wireless_led", material="item_led_green").scale(1.0, 0.45, 1.0).translate(0.0, led_y + 0.0022, led_z))

    # 5. Sleek flush-contoured Back & Forward thumb buttons along the ergonomic left ridge!
    for idx_b, bu in enumerate((0.355, 0.485)):
        bz = -half_l + 2.0 * half_l * bu
        bx = -float(_mouse_half_w(np.array([bu]))[0]) + 0.011
        by = float(_mouse_belt_y(np.array([bu]))[0]) + 0.002
        parts.append(rounded_box(0.020, 0.018, 0.076, bevel=0.006, segs=2, name=f"mouse_thumb_{idx_b}", material="item_chrome_polished").rotate_xyz(rz=+0.18).translate(bx, by, bz))

    # 6. Authentic wireless optical underside: tucked elliptical PTFE skates flush on Y=0, optical sensor lens, ON/OFF switch & battery hatch.
    for sz, rx_s, rz_s, tag in ((-0.26, 0.088, 0.036, "front"), (+0.26, 0.095, 0.038, "rear")):
        parts.append(cylinder(1.0, 1.0, 0.010, radial_segs=20, bevel=0.0025, axis="y", name=f"mouse_skate_{tag}", material="item_hair_black").scale(rx_s, 1.0, rz_s).translate(0.0, 0.005, sz))
    parts.append(rounded_box(0.068, 0.012, 0.088, bevel=0.004, segs=2, name="mouse_sensor_well", material="item_hair_black").translate(0.0, 0.009, 0.01))
    parts.append(sphere(0.013, u_segs=12, v_segs=8, name="mouse_sensor_led", material="item_candy_red").scale(1.0, 0.4, 1.0).translate(0.0, 0.009, 0.01))
    parts.append(rounded_box(0.020, 0.012, 0.052, bevel=0.003, segs=1, name="mouse_power_sw", material="item_chrome_polished").translate(+0.072, 0.009, -0.02))
    parts.append(rounded_box(0.135, 0.008, 0.125, bevel=0.004, segs=2, name="mouse_batt_door", material="item_rubber_dark").translate(0.0, 0.008, +0.155))

    sc.add("mouse1", [p.rotate_xyz(ry=40.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.90)


def _hdmi_trapezoid_shell(
    z0: float,
    z1: float,
    y_ctr: float,
    *,
    hw_top: float = 0.128,
    hw_bot: float = 0.098,
    hy: float = 0.042,
    y_step: float = -0.008,
    wall: float = 0.011,
    name: str = "hdmi_shield",
    material: str = "item_hdmi_gold",
) -> MeshPart:
    """Build a watertight hollow open-front trapezoidal HDMI Type-A male metal shield with chamfered bottom corners."""
    outer_xy = np.array([
        [+hw_top, +hy], [+hw_top * 0.94, +hy], [-hw_top * 0.94, +hy], [-hw_top, +hy],
        [-hw_top, y_step], [-hw_bot, -hy], [+hw_bot, -hy], [+hw_top, y_step],
    ], dtype=np.float64)
    # Remove duplicate top corners -> crisp 8-point chamfered HDMI trapezoid (subtle top bevels + iconic bottom 45-deg chamfers).
    outer_xy = np.array([
        [+hw_top - 0.008, +hy], [-hw_top + 0.008, +hy],
        [-hw_top, +hy - 0.008], [-hw_top, y_step],
        [-hw_bot, -hy], [+hw_bot, -hy],
        [+hw_top, y_step], [+hw_top, +hy - 0.008],
    ], dtype=np.float64)
    inner_xy = np.array([
        [+hw_top - wall - 0.005, +hy - wall], [-hw_top + wall + 0.005, +hy - wall],
        [-hw_top + wall, +hy - wall - 0.005], [-hw_top + wall, y_step + 0.004],
        [-hw_bot + wall * 0.9, -hy + wall], [+hw_bot - wall * 0.9, -hy + wall],
        [+hw_top - wall, y_step + 0.004], [+hw_top - wall, +hy - wall - 0.005],
    ], dtype=np.float64)
    n = len(outer_xy)
    verts = []
    # Rings: 0=outer_back(z0), 1=outer_front(z1), 2=inner_front(z1), 3=inner_back(z0+0.012)
    z_in_back = z0 + 0.012
    for xy, z in ((outer_xy, z0), (outer_xy, z1), (inner_xy, z1), (inner_xy, z_in_back)):
        for x, y in xy:
            verts.append((float(x), float(y + y_ctr), float(z)))
    c_in_back = len(verts)
    verts.append((0.0, float(y_ctr), float(z_in_back)))
    c_out_back = len(verts)
    verts.append((0.0, float(y_ctr), float(z0)))

    tris: List[Tuple[int, int, int]] = []
    for j in range(n):
        jn = (j + 1) % n
        ob0, ob1 = j, jn
        of0, of1 = n + j, n + jn
        if0, if1 = 2 * n + j, 2 * n + jn
        ib0, ib1 = 3 * n + j, 3 * n + jn
        # Outer wall (+outward), front bevel rim (+Z), inner cavity wall (-outward)
        tris.extend(((ob0, ob1, of1), (ob0, of1, of0)))
        tris.extend(((of0, of1, if1), (of0, if1, if0)))
        tris.extend(((if0, if1, ib1), (if0, ib1, ib0)))
        # Inner rear floor (+Z facing out of mouth) and outer rear seal (-Z)
        tris.append((c_in_back, ib0, ib1))
        tris.append((c_out_back, ob1, ob0))

    pos = np.asarray(verts, dtype=np.float32)
    idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
    nrm = recompute_smooth_normals(pos, idx)
    return MeshPart(name=name, material=material, positions=pos, normals=nrm, indices=idx)


def build_mouse2() -> SceneBuilder:
    """Modern gold-plated HDMI Type-A male connector plug with matte-charcoal grip hood & ribbed strain-relief cable (`244:mouse2`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_mouse2")
    parts: List[MeshPart] = []
    y_c = 0.064

    # 1. Ergonomic matte-charcoal molded HDMI grip housing with chamfered edges, chrome trim collar & top/bottom finger-grip ribs.
    hood = rounded_box(0.310, 0.126, 0.275, bevel=0.018, segs=3, name="hdmi_hood", material="item_leather_smooth").translate(0.0, y_c, 0.0)
    collar = rounded_box(0.296, 0.114, 0.028, bevel=0.008, segs=2, name="hdmi_collar", material="item_chrome_polished").translate(0.0, y_c, +0.136)
    parts.extend([hood, collar])
    # Sculpted non-slip thumb grip bars centered on top & bottom + recessed side thumb-pinch grip strips.
    for ri in range(4):
        rz = -0.085 + ri * 0.036
        for sy, tag in ((+1.0, "t"), (-1.0, "b")):
            parts.append(rounded_box(0.185, 0.010, 0.015, bevel=0.003, segs=1, name=f"hdmi_rib_{tag}_{ri}", material="item_rubber_dark").translate(0.0, y_c + sy * 0.062, rz))
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        parts.append(rounded_box(0.014, 0.072, 0.175, bevel=0.005, segs=2, name=f"hdmi_sidegrip_{tag}", material="item_rubber_dark").translate(side * 0.153, y_c, -0.01))
    # Subtle crisp silver trapezoid HDMI orientation emblem embossed near the front of the top face.
    parts.append(rounded_box(0.115, 0.006, 0.022, bevel=0.002, segs=1, name="hdmi_badge_bar", material="item_chrome_polished").translate(0.0, y_c + 0.063, +0.086))
    parts.append(rounded_box(0.072, 0.006, 0.014, bevel=0.002, segs=1, name="hdmi_badge_tip", material="item_chrome_polished").translate(0.0, y_c + 0.063, +0.105))

    # 2. Authentic hollow open-front 24k gold-plated HDMI Type-A trapezoidal metal shield + retention detents + 19-pin inner tongue!
    parts.append(_hdmi_trapezoid_shell(z0=+0.140, z1=+0.305, y_ctr=y_c, name="hdmi_shield", material="item_hdmi_gold"))
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        parts.append(rounded_box(0.030, 0.006, 0.034, bevel=0.002, segs=1, name=f"hdmi_latch_{tag}", material="item_hair_black").translate(side * 0.056, y_c + 0.041, +0.232))
    # Dark insulating inner contact carrier wafer + crisp dual-row gold contact pins visible right inside the open mouth!
    parts.append(rounded_box(0.184, 0.014, 0.135, bevel=0.002, segs=1, name="hdmi_tongue", material="item_hair_black").translate(0.0, y_c + 0.003, +0.220))
    for pi in range(9):
        px = -0.076 + pi * 0.019
        parts.append(rounded_box(0.009, 0.004, 0.105, bevel=0.001, segs=1, name=f"hdmi_pin_t_{pi}", material="item_hdmi_gold").translate(px, y_c + 0.011, +0.232))
        parts.append(rounded_box(0.009, 0.004, 0.105, bevel=0.001, segs=1, name=f"hdmi_pin_b_{pi}", material="item_hdmi_gold").translate(px, y_c - 0.005, +0.232))

    # 3. Slotted flexible matte-rubber strain-relief boot + thick matte-black HDMI cable tail curving gently back-left.
    parts.append(cylinder(0.042, 0.056, 0.115, radial_segs=18, bevel=0.005, axis="z", name="hdmi_boot_core", material="item_rubber_dark").translate(0.0, y_c, -0.188))
    for bi in range(4):
        bz = -0.152 - bi * 0.026
        br = 0.062 - bi * 0.0055
        parts.append(cylinder(br, br, 0.014, radial_segs=18, bevel=0.003, axis="z", name=f"hdmi_boot_ring_{bi}", material="item_leather_smooth").translate(0.0, y_c, bz))
    cable = curved_tube(
        [(0.0, y_c, -0.242), (-0.005, y_c - 0.008, -0.320), (-0.025, y_c - 0.018, -0.405), (-0.058, y_c - 0.022, -0.480)],
        [0.036, 0.035, 0.035, 0.035],
        samples_per_seg=4,
        radial_segs=16,
        cap_rings=2,
        name="hdmi_cable_stub",
        material="item_rubber_dark",
    )
    parts.append(cable)

    sc.add("mouse2", [p.rotate_xyz(ry=38.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.90)
