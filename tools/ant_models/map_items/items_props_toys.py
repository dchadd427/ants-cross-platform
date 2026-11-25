"""Realistic 3D sculptors for 1998 Ants Toys & Backyard Props Wave 3A (21 Hero Toys, Crayons & Jacks).

Covers:
- `glasses`  (236): Teal-and-navy cat-eye eyeglasses with clear glass lenses
- `bobber`   (237): Red-and-white round plastic fishing bobber with thumb plunger
- `can2`     (239): Horizontal crushed Critter Cola soda can (pull-tab lid on left)
- `yoyo`     (240): Cream-white vintage yo-yo with red racing stripes & looped white cotton string
- `brush`    (241): Retro airbrush / water squirt pistol (black grip, red trigger & rocket nozzle)
- `slinky`   (242): Arched gunmetal spring-steel Slinky coiled in a C-bend
- `mouse1`   (243): Classic 1998 beige 2-button desktop computer mouse with tangled grey cord
- `mouse2`   (244): 9-pin DB9 serial computer connector plug with thumbscrews
- `buglass`  (335): Purple novelty bug-eye glasses with twin chrome coil springs & bloodshot eyeballs
- `jacks1`..`jacks4` (336–339): Six-pronged cast-metal game jacks in 4 resting orientations
- `ball`     (344): Glossy cherry-red rubber jacks bounce ball
- `chicken`  (347): Stretched yellow rubber chicken with splayed red webbed feet & screaming beak
- `sling`    (350): Wooden Y-slingshot with crimson elastic bands & curved leather ammo pouch
- `pinkcray`, `grncray`, `yelcray`, `blucray` (352–355): 'Krayawn' wax crayons (`grncray` snapped in half!)
- `lunchbox` (356): Miniature yellow-lidded chestnut-brown metal lunchbox
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
        from .item_materials import gradient_y_tint, register_item_materials, tint_part
        from .items_foliage import _sculpt_bark_branch
        from .items_toys import _finalize_item
    except ImportError:
        from map_items.item_materials import gradient_y_tint, register_item_materials, tint_part  # type: ignore
        from map_items.items_foliage import _sculpt_bark_branch  # type: ignore
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


def _cat_eye_rim(name: str, side: float, *, mat: str = "item_glasses_teal") -> List[MeshPart]:
    """Sculpt one upswept cat-eye eyeglass rim loop + curved clear glass lens (`236:glasses`)."""
    pts: List[Tuple[float, float, float]] = []
    n = 28
    for i in range(n + 1):
        a = 2.0 * math.pi * (i % n) / n
        # Sharp upswept retro cat-eye outer wing corners matching `236:glasses`!
        out_up = max(0.0, math.sin(a)) * max(0.0, side * math.cos(a))
        lx = 0.142 * math.cos(a) + side * 0.042 * (out_up ** 1.3)
        ly = 0.082 * math.sin(a) + 0.046 * (out_up ** 1.2)
        lz = -0.020 * (lx * lx * 16.0)
        pts.append((side * 0.168 + lx, ly, lz))
    rim = curved_tube(pts, [0.0155] * len(pts), samples_per_seg=2, radial_segs=10, cap_start=False, cap_end=False, name=f"{name}_rim", material=mat)
    # Deep emerald-teal & navy speckled tortoiseshell matching `236:glasses`!
    rim = tint_part(
        rim,
        lambda pos, _n: np.clip(
            np.column_stack([
                0.28 + 0.34 * (np.sin(pos[:, 0] * 48.0 + pos[:, 1] * 34.0) > 0.25),
                0.46 + 0.50 * (np.cos(pos[:, 0] * 42.0 - pos[:, 1] * 38.0) > 0.10),
                0.68 + 0.28 * np.sin(pos[:, 0] * 30.0),
            ]),
            0.16,
            1.0,
        ),
    )
    lens = ellipsoid(0.130, 0.078, 0.008, u_segs=20, v_segs=10, name=f"{name}_lens", material="item_lens_clear").translate(side * 0.168, 0.010, -0.004)
    return [rim, lens]


def build_glasses() -> SceneBuilder:
    """Teal-and-navy cat-eye eyeglasses with clear lenses & folded temple arms (`236:glasses`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_glasses")
    parts: List[MeshPart] = []
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        parts.extend(_cat_eye_rim(f"glasses_{tag}", side))
        hinge = rounded_box(0.036, 0.022, 0.022, bevel=0.004, segs=2, name=f"glasses_hinge_{tag}", material="item_glasses_teal").translate(side * 0.320, 0.046, -0.024)
        # Left temple folded slightly inward toward center-right, right temple angled open (`236:glasses`)!
        inward = +0.16 if side < 0 else -0.02
        arm = curved_tube(
            [
                (side * 0.326, 0.046, -0.024),
                (side * 0.318 + inward * 0.35, 0.040, -0.22),
                (side * 0.295 + inward * 0.85, 0.028, -0.44),
                (side * 0.275 + inward * 1.10, -0.014, -0.53),
                (side * 0.262 + inward * 1.18, -0.058, -0.56),
            ],
            [0.011, 0.010, 0.009, 0.0085, 0.007],
            samples_per_seg=4,
            radial_segs=10,
            cap_rings=2,
            name=f"glasses_arm_{tag}",
            material="item_rubber_blue",
        )
        pad = ellipsoid(0.012, 0.022, 0.008, u_segs=10, v_segs=8, name=f"glasses_pad_{tag}", material="item_candy_white").translate(side * 0.044, -0.010, -0.024)
        parts.extend([hinge, arm, pad])

    bridge = curved_tube([(-0.042, 0.024, -0.002), (0.0, 0.036, +0.008), (+0.042, 0.024, -0.002)], [0.013, 0.012, 0.013], samples_per_seg=3, radial_segs=10, name="glasses_bridge", material="item_glasses_teal")
    parts.append(bridge)
    sc.add("glasses", [p.rotate_xyz(rx=8.0, ry=-32.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.96)


def build_bobber() -> SceneBuilder:
    """Classic glossy red-and-white round plastic fishing bobber with chunky red plunger (`237:bobber`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_bobber")
    n_lat, n_lon = 16, 36
    # Smooth studio-glossy painted plastic halves (zero bumpy noise) matching `237:bobber`!
    for is_top, mat, tag in ((True, "item_candy_white", "white"), (False, "item_candy_red", "red")):
        a0, a1 = (0.0, math.pi * 0.5) if is_top else (-math.pi * 0.5, 0.0)
        profile = [(0.245 * math.cos(a0 + (a1 - a0) * i / n_lat), 0.245 * math.sin(a0 + (a1 - a0) * i / n_lat)) for i in range(n_lat + 1)]
        sc.add("bobber", lathe(profile, radial_segs=n_lon, cap_bottom=not is_top, cap_top=is_top, axis="y", name=f"bobber_{tag}", material=mat))

    # Chunkier red push-button thumb plunger cylinder & chrome spring clip matching `237:bobber`!
    collar = cylinder(0.095, 0.105, 0.068, radial_segs=26, bevel=0.008, axis="y", name="bobber_collar", material="item_candy_red").translate(0.0, 0.246, 0.0)
    plunger = cylinder(0.082, 0.084, 0.105, radial_segs=26, bevel=0.012, axis="y", name="bobber_plunger", material="item_candy_red").translate(0.0, 0.312, 0.0)
    cap_groove = torus_arc(0.076, 0.008, u_segs=24, v_segs=8, plane="xz", name="bobber_groove", material="item_candy_white").translate(0.0, 0.354, 0.0)
    top_hook = torus_arc(0.025, 0.006, u_segs=18, v_segs=6, plane="xy", name="bobber_hook_top", material="item_chrome_spring").translate(0.0, 0.376, 0.0)
    bot_hook = torus_arc(0.024, 0.006, u_segs=18, v_segs=6, plane="xy", name="bobber_hook_bot", material="item_chrome_spring").translate(0.0, -0.258, 0.0)
    sc.add("bobber", [collar, plunger, cap_groove, top_hook, bot_hook])
    sc.nodes["bobber"] = [p.rotate_xyz(rx=14.0, ry=-10.0, rz=-42.0, degrees=True) for p in sc.nodes["bobber"]]
    return _finalize_item(sc, target_span=0.88)


def build_can2() -> SceneBuilder:
    """Horizontal crushed Critter Cola soda can: silver pull-tab lid on LEFT, upside-down logo in front (`239:can2`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_can2")

    def _crush_horiz(pos: np.ndarray) -> np.ndarray:
        """Horizontal stepped-on saddle crush: keeps left lid & right base intact, subtle dent across mid-belly (`239:can2`)."""
        p = pos.copy()
        z = p[:, 2]
        mid_w = np.exp(-(((z - 0.01) / 0.16) ** 2))
        end_guard = np.clip((0.285 - np.abs(z)) / 0.06, 0.0, 1.0)
        crush = mid_w * end_guard
        top_w = np.clip((p[:, 1] + 0.06) / 0.20, 0.0, 1.0)
        crease = 1.0 - np.abs(np.sin(p[:, 0] * 13.0 + z * 10.0))
        p[:, 1] -= 0.088 * crush * top_w * (0.72 + 0.28 * crease)
        p[:, 0] *= 1.0 + 0.16 * crush
        # Very slight realistic settle — NO dramatic banana bow!
        p[:, 1] += 0.014 * ((z / 0.28) ** 2) - 0.012 * crush
        return p

    n_rings, n_rad = 26, 52
    z_lo, z_hi = -0.285, +0.295
    verts, uvs, tris = [], [], []
    for ir in range(n_rings + 1):
        vf = ir / float(n_rings)
        z = z_lo + (z_hi - z_lo) * vf
        if vf < 0.08:
            rad = 0.156 + 0.012 * (vf / 0.08)
        elif vf > 0.90:
            rad = 0.168 - 0.024 * (((vf - 0.90) / 0.10) ** 1.35)
        else:
            rad = 0.168
        tex_v = 1.0 - vf  # v=0 near lid (+Z), v=1 near base (-Z)
        for j in range(n_rad + 1):
            uf = j / float(n_rad)
            # Rotate label roll so upside-down 'Critter Cola' sits front-center when lid points left-front!
            ang = -math.pi * 0.5 + uf * 2.0 * math.pi + 4.45
            verts.append((rad * math.cos(ang), rad * math.sin(ang), z))
            uvs.append((uf, tex_v))
    stride = n_rad + 1
    for ir in range(n_rings):
        for j in range(n_rad):
            a, b, c, d = ir * stride + j, (ir + 1) * stride + j, (ir + 1) * stride + (j + 1), ir * stride + (j + 1)
            tris.extend(((a, c, b), (a, d, c)))
    pos_b = _crush_horiz(np.asarray(verts, dtype=np.float32))
    idx_b = np.asarray(tris, dtype=np.uint32).reshape(-1)
    body_shell = MeshPart("can2_label_wall", "item_cola_label", pos_b, recompute_smooth_normals(pos_b, idx_b), idx_b, uvs=np.asarray(uvs, dtype=np.float32))

    top_end = lathe([(0.144, +0.295), (0.150, +0.312), (0.145, +0.326), (0.132, +0.328), (0.125, +0.306), (0.110, +0.304), (0.000, +0.303)], radial_segs=42, cap_bottom=False, cap_top=False, axis="z", name="can2_top_lid", material="item_aluminum").deform(_crush_horiz)
    bot_end = lathe([(0.000, -0.308), (0.098, -0.306), (0.128, -0.324), (0.146, -0.318), (0.156, -0.285)], radial_segs=38, cap_bottom=False, cap_top=False, axis="z", name="can2_bottom", material="item_aluminum").deform(_crush_horiz)
    dy_l = 0.014 * ((0.305 / 0.28) ** 2)
    score = torus_arc(0.112, 0.004, u_segs=34, v_segs=6, plane="xy", name="can2_score", material="item_steel_dark").translate(0.0, dy_l, +0.307)
    hole = superellipsoid(0.048, 0.056, 0.010, e1=0.72, e2=0.80, u_segs=20, v_segs=8, name="can2_hole", material="item_soil_hole").translate(+0.018, dy_l + 0.044, +0.306)
    hole_rim = torus_arc(0.050, 0.005, u_segs=22, v_segs=6, plane="xy", name="can2_hole_rim", material="item_aluminum").translate(+0.018, dy_l + 0.044, +0.309)
    rivet = cylinder(0.015, 0.015, 0.014, radial_segs=14, axis="z", name="can2_rivet", material="item_aluminum").translate(-0.008, dy_l - 0.006, +0.312)
    tab_p = rounded_box(0.052, 0.072, 0.009, bevel=0.003, segs=2, name="can2_tab_bridge", material="item_aluminum").rotate_xyz(rz=0.25).translate(-0.022, dy_l - 0.036, +0.314)
    tab_l = torus_arc(0.030, 0.0085, u_segs=22, v_segs=8, plane="xy", name="can2_pull_loop", material="item_aluminum").rotate_xyz(rz=0.25).translate(-0.034, dy_l - 0.072, +0.318)
    tab_h = cylinder(0.018, 0.018, 0.006, radial_segs=16, axis="z", name="can2_tab_hole", material="item_steel_dark").translate(-0.034, dy_l - 0.072, +0.310)
    parts = [body_shell, top_end, bot_end, score, hole, hole_rim, rivet, tab_p, tab_l, tab_h]
    # Orient horizontally: gleaming pull-tab lid on LEFT facing front-left, crushed belly gently rising right (`239:can2`)!
    sc.add("can", [p.rotate_xyz(rx=-6.0, ry=-58.0, rz=8.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.96)


def build_yoyo() -> SceneBuilder:
    """Classic smooth glossy ivory-cream vintage yo-yo with twin crimson pin-stripes & looped string (`240:yoyo`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_yoyo")
    parts: List[MeshPart] = []

    shell_prof = [(0.000, 0.145), (0.095, 0.142), (0.175, 0.122), (0.228, 0.082), (0.236, 0.045), (0.210, 0.020), (0.052, 0.018)]
    ivory = _const_tint((0.93, 0.90, 0.84))
    top_half = tint_part(lathe(shell_prof, radial_segs=42, cap_bottom=True, cap_top=True, axis="y", name="yoyo_half_top", material="item_candy_white"), ivory)
    bot_prof = [(r, -y) for r, y in reversed(shell_prof)]
    bot_half = tint_part(lathe(bot_prof, radial_segs=42, cap_bottom=True, cap_top=True, axis="y", name="yoyo_half_bot", material="item_candy_white"), ivory)
    axle = cylinder(0.055, 0.055, 0.044, radial_segs=20, axis="y", name="yoyo_axle", material="item_wood_stick")
    # Crisp thin crimson racing pin-stripes inset along each glossy dome shoulder + flush red/gold starburst emblem!
    ring_t = torus_arc(0.214, 0.0055, u_segs=42, v_segs=8, plane="xz", name="yoyo_stripe_top", material="item_candy_red").translate(0.0, +0.090, 0.0)
    ring_b = torus_arc(0.214, 0.0055, u_segs=42, v_segs=8, plane="xz", name="yoyo_stripe_bot", material="item_candy_red").translate(0.0, -0.090, 0.0)
    emblem_r = cylinder(0.072, 0.072, 0.004, radial_segs=26, bevel=0.001, axis="y", name="yoyo_emblem_red", material="item_candy_red").translate(0.0, +0.144, 0.0)
    emblem_g = cylinder(0.048, 0.048, 0.005, radial_segs=22, bevel=0.001, axis="y", name="yoyo_emblem_gold", material="item_pollen_gold").translate(0.0, +0.145, 0.0)
    parts.extend([top_half, bot_half, axle, ring_t, ring_b, emblem_r, emblem_g])

    parts = [p.rotate_xyz(rx=48.0, ry=-32.0, rz=52.0, degrees=True).translate(-0.08, 0.18, -0.02) for p in parts]

    # Looped white cotton yo-yo string trailing right-bottom + wrapping lightly under left belly (`240:yoyo`)!
    str_a = curved_tube(
        [
            (+0.02, 0.15, +0.02), (+0.16, 0.06, -0.10), (+0.30, 0.02, -0.08), (+0.36, 0.018, +0.04),
            (+0.31, 0.020, +0.17), (+0.20, 0.020, +0.20), (+0.14, 0.020, +0.12), (+0.24, 0.024, +0.06),
            (+0.38, 0.020, +0.10), (+0.40, 0.018, +0.22), (+0.27, 0.018, +0.28), (+0.10, 0.018, +0.22),
            (-0.08, 0.022, +0.20), (-0.23, 0.030, +0.14), (-0.28, 0.055, +0.05),
        ],
        [0.010] * 15,
        samples_per_seg=4,
        radial_segs=10,
        cap_rings=2,
        name="yoyo_string",
        material="item_rubber_cream",
    )
    parts.append(str_a)
    sc.add("yoyo", parts)
    return _finalize_item(sc, target_span=0.92)


def build_brush() -> SceneBuilder:
    """Retro airbrush / water squirt pistol (`241:brush`: chunky ribbed black grip, full 3-finger red trigger & finned rocket nose)."""
    register_item_materials()
    sc = SceneBuilder(name="item_brush")
    parts: List[MeshPart] = []

    # 1. Chunky sculpted glossy-black pistol receiver + rounded backstrap bulge + curved pistol handle (`241:brush`)!
    receiver = rounded_box(0.28, 0.175, 0.115, bevel=0.042, segs=4, name="brush_receiver", material="item_steel_dark").translate(+0.06, 0.025, 0.0)
    backstrap = ellipsoid(0.088, 0.095, 0.058, u_segs=18, v_segs=12, name="brush_backstrap", material="item_steel_dark").rotate_xyz(rz=-0.42).translate(+0.185, -0.045, 0.0)
    handle = (
        rounded_box(0.155, 0.34, 0.108, bevel=0.044, segs=4, name="brush_handle", material="item_steel_dark")
        .rotate_xyz(rz=-0.22)
        .translate(+0.085, -0.155, 0.0)
    )
    # 4 bold horizontal molded grip ribs across handle + 3 angled receiver ribs matching `241:brush`!
    for k in range(4):
        rib = rounded_box(0.095, 0.018, 0.120, bevel=0.006, segs=2, name=f"brush_grip_rib_{k}", material="item_rubber_dark").rotate_xyz(rz=-0.22).translate(+0.075 + k * 0.010, -0.075 - k * 0.054, 0.0)
        parts.append(rib)
    for k in range(3):
        top_rib = rounded_box(0.130, 0.016, 0.122, bevel=0.005, segs=2, name=f"brush_top_rib_{k}", material="item_rubber_dark").rotate_xyz(rz=-0.10).translate(+0.095 + k * 0.018, +0.065 - k * 0.028, 0.0)
        parts.append(top_rib)

    # 2. FULL-LENGTH scalloped bright-red 3-finger squeeze trigger spanning the entire front of the handle (`241:brush`)!
    trig_bar = rounded_box(0.078, 0.295, 0.042, bevel=0.014, segs=3, name="brush_trigger", material="item_candy_red").rotate_xyz(rz=-0.22).translate(-0.040, -0.145, 0.0)
    for k in range(3):
        lobe = ellipsoid(0.032, 0.042, 0.022, u_segs=14, v_segs=10, name=f"brush_trig_lobe_{k}", material="item_candy_red").rotate_xyz(rz=-0.22).translate(-0.066 + k * 0.016, -0.062 - k * 0.082, 0.0)
        parts.append(lobe)
    trig_guard = rounded_box(0.048, 0.040, 0.040, bevel=0.010, segs=2, name="brush_trig_bot", material="item_candy_red").rotate_xyz(rz=-0.22).translate(-0.018, -0.275, 0.0)

    # 3. Short gleaming chrome barrel + fluted cylindrical red rocket shroud, wide swept fins & stubby squirt spout (`241:brush`)!
    barrel = cylinder(0.036, 0.038, 0.155, radial_segs=22, bevel=0.004, axis="x", name="brush_barrel", material="item_aluminum").translate(-0.120, 0.050, 0.0)
    shroud = cylinder(0.056, 0.064, 0.165, radial_segs=24, bevel=0.008, axis="x", name="brush_shroud", material="item_candy_red").translate(-0.255, 0.050, 0.0)
    # Fluted longitudinal ribs around the red nozzle shroud
    for fi in range(6):
        fa = fi * math.pi / 3.0
        flute = cylinder(0.009, 0.009, 0.152, radial_segs=8, axis="x", name=f"brush_flute_{fi}", material="item_rubber_red").translate(-0.255, 0.050 + 0.058 * math.cos(fa), 0.058 * math.sin(fa))
        parts.append(flute)
    nozzle_tip = cylinder(0.020, 0.028, 0.105, radial_segs=18, bevel=0.004, axis="x", name="brush_tip", material="item_candy_red").translate(-0.375, 0.050, 0.0)
    # Broad swept dorsal rocket fin (matching the big upper-right fin on `241:brush`) + smaller ventral fin!
    fin_top = rounded_box(0.125, 0.095, 0.020, bevel=0.006, segs=2, name="brush_fin_top", material="item_candy_red").rotate_xyz(rz=+0.56).translate(-0.215, 0.118, 0.0)
    fin_bot = rounded_box(0.095, 0.062, 0.020, bevel=0.006, segs=2, name="brush_fin_bot", material="item_candy_red").rotate_xyz(rz=-0.46).translate(-0.225, -0.008, 0.0)
    parts.extend([receiver, backstrap, handle, trig_bar, trig_guard, barrel, shroud, nozzle_tip, fin_top, fin_bot])
    sc.add("brush", [p.rotate_xyz(rx=-90.0, ry=-44.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.96)


def build_slinky() -> SceneBuilder:
    """Classic gunmetal spring-steel Slinky arched in a wide flat-ribbon C-bend (`242:slinky`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_slinky")
    n_turns, pts_per_turn = 26, 14
    total_pts = n_turns * pts_per_turn
    arc_rad, coil_rad = 0.295, 0.172
    pts: List[Tuple[float, float, float]] = []
    for k in range(total_pts + 1):
        u = k / float(total_pts)
        ang_c = math.radians(-156.0 + 158.0 * u)
        cx = arc_rad * math.cos(ang_c)
        cz = arc_rad * math.sin(ang_c) + 0.10
        rx, rz = math.cos(ang_c), math.sin(ang_c)
        th = 2.0 * math.pi * n_turns * u
        pts.append((cx + rx * (coil_rad * math.cos(th)), coil_rad * math.sin(th) + coil_rad + 0.012, cz + rz * (coil_rad * math.cos(th))))
    coil = curved_tube(pts, [0.0105] * len(pts), samples_per_seg=1, radial_segs=6, cap_rings=1, name="slinky_spring", material="item_slinky_steel")
    # Warm pewter-gunmetal metallic sheen matching `242:slinky`!
    coil = tint_part(
        coil,
        lambda pos, nrm: np.clip(
            (0.68 + 0.26 * np.clip(nrm[:, 1] + 0.35, 0.0, 1.0) + 0.08 * np.sin(pos[:, 0] * 18.0))[:, None] * np.array([[0.98, 0.95, 0.90]]),
            0.34,
            1.0,
        ),
    )
    sc.add("slinky", coil.rotate_xyz(ry=18.0, degrees=True))
    return _finalize_item(sc, target_span=0.96)


def build_mouse1() -> SceneBuilder:
    """Classic 1998 beige 2-button desktop computer mouse with crisp T-seam buttons & vertical cord loops (`243:mouse1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_mouse1")
    parts: List[MeshPart] = []

    # Authentic 1990s ergonomic desktop mouse shell: plump rounded palm hump (+Z), squared 2-button front nose (-Z)!
    def _mouse_hull(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        u = np.clip((p[:, 2] + 0.24) / 0.48, 0.0, 1.0)  # 0 front button nose, 1 rear palm
        p[:, 1] = np.where(p[:, 1] < 0.0, p[:, 1] * 0.10, p[:, 1] * (0.58 + 0.56 * np.sin((u ** 0.85) * math.pi * 0.82)))
        # Subtle ergonomic hourglass waist & wider rounded palm dome at back
        waist = 0.84 + 0.26 * (u ** 1.15) - 0.08 * np.sin(u * math.pi)
        p[:, 0] *= waist
        # Slightly flatten front button slope
        p[:, 1] *= 1.0 - 0.18 * np.clip((0.36 - u) / 0.36, 0.0, 1.0)
        return p

    ivory_mouse = _const_tint((0.94, 0.91, 0.86))
    body = tint_part(
        superellipsoid(0.155, 0.112, 0.242, e1=0.66, e2=0.72, u_segs=42, v_segs=22, name="mouse_body", material="item_candy_white")
        .deform(_mouse_hull)
        .translate(0.0, 0.018, 0.0),
        ivory_mouse,
    )
    # Sculpted twin rectangular front mouse button keycaps with crisp dark T-divider seams (`243:mouse1`)!
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        btn = tint_part(
            rounded_box(0.118, 0.024, 0.178, bevel=0.008, segs=3, name=f"mouse_btn_{tag}", material="item_candy_white")
            .rotate_xyz(rx=+0.15)
            .translate(side * 0.064, 0.074, -0.145),
            _const_tint((0.96, 0.93, 0.88)),
        )
        parts.append(btn)
    seam_center = curved_tube(
        [(0.0, 0.076, -0.232), (0.0, 0.116, -0.145), (0.0, 0.144, -0.052)],
        [0.0042, 0.0042, 0.0042],
        samples_per_seg=3,
        radial_segs=6,
        name="mouse_seam_c",
        material="item_rubber_dark",
    )
    seam_cross = curved_tube(
        [(-0.124, 0.113, -0.052), (-0.070, 0.141, -0.052), (0.0, 0.144, -0.052), (+0.070, 0.141, -0.052), (+0.124, 0.113, -0.052)],
        [0.0040] * 5,
        samples_per_seg=3,
        radial_segs=6,
        name="mouse_seam_h",
        material="item_rubber_dark",
    )
    boot = cylinder(0.015, 0.022, 0.044, radial_segs=14, axis="z", name="mouse_boot", material="item_cord_grey").translate(0.0, 0.036, -0.245)
    parts.extend([body, seam_center, seam_cross, boot])

    # Buttons facing UP-LEFT, plump palm heel resting BOTTOM-RIGHT matching `243:mouse1`!
    parts = [p.rotate_xyz(ry=42.0, degrees=True).translate(+0.16, 0.0, +0.02) for p in parts]

    # Tall vertical looped grey serial cord hugging the LEFT side + tail dropping straight down (`243:mouse1`)!
    cord_pts = [
        (-0.01, 0.036, -0.16), (-0.11, 0.024, -0.22), (-0.18, 0.016, -0.14), (-0.16, 0.016, +0.06),
        (-0.22, 0.016, +0.18), (-0.28, 0.016, +0.06), (-0.31, 0.016, -0.16), (-0.38, 0.016, -0.18),
        (-0.42, 0.016, -0.02), (-0.34, 0.016, +0.14), (-0.26, 0.016, +0.04), (-0.25, 0.018, -0.14),
        (-0.22, 0.016, +0.12), (-0.18, 0.016, +0.30), (-0.12, 0.016, +0.40),
    ]
    cord = curved_tube(cord_pts, [0.0095] * len(cord_pts), samples_per_seg=4, radial_segs=8, cap_rings=2, name="mouse_cord", material="item_cord_grey")
    parts.append(cord)
    sc.add("mouse1", parts)
    return _finalize_item(sc, target_span=0.96)


def build_mouse2() -> SceneBuilder:
    """9-pin DB9 serial computer plug connector with smooth grey ribbed hood & wide silver flange (`244:mouse2`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_mouse2")
    parts: List[MeshPart] = []

    grey_cool = _const_tint((0.76, 0.77, 0.78))
    hood = tint_part(rounded_box(0.26, 0.095, 0.24, bevel=0.018, segs=3, name="db9_hood", material="item_mouse_beige").translate(0.0, 0.052, 0.0), grey_cool)
    flange = rounded_box(0.35, 0.080, 0.042, bevel=0.010, segs=2, name="db9_flange", material="item_chrome_spring").translate(0.0, 0.052, +0.128)
    for k in range(3):
        rib = tint_part(rounded_box(0.18, 0.106, 0.018, bevel=0.004, segs=2, name=f"db9_rib_{k}", material="item_mouse_beige").translate(0.0, 0.052, -0.05 + k * 0.042), _const_tint((0.88, 0.89, 0.90)))
        parts.append(rib)
    boot = tint_part(cylinder(0.034, 0.048, 0.11, radial_segs=16, axis="z", name="db9_boot", material="item_mouse_beige").translate(0.0, 0.052, -0.165), grey_cool)
    stub = cylinder(0.020, 0.020, 0.095, radial_segs=12, axis="z", name="db9_cord_stub", material="item_mouse_beige").translate(0.0, 0.052, -0.255)
    shield = rounded_box(0.22, 0.070, 0.078, bevel=0.012, segs=2, name="db9_shield", material="item_chrome_spring").translate(0.0, 0.052, +0.175)
    recess = rounded_box(0.17, 0.046, 0.020, bevel=0.004, segs=2, name="db9_recess", material="item_steel_dark").translate(0.0, 0.052, +0.208)
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        screw_shaft = cylinder(0.015, 0.015, 0.12, radial_segs=12, axis="z", name=f"db9_shaft_{tag}", material="item_chrome_spring").translate(side * 0.142, 0.052, +0.12)
        screw_knob = cylinder(0.028, 0.028, 0.055, radial_segs=14, bevel=0.004, axis="z", name=f"db9_knob_{tag}", material="item_mouse_beige").translate(side * 0.142, 0.052, +0.02)
        parts.extend([screw_shaft, screw_knob])

    parts.extend([hood, flange, boot, stub, shield, recess])
    sc.add("mouse2", [p.rotate_xyz(rx=6.0, ry=38.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.88)


def _build_spring_eyeball(prefix: str, *, origin: Tuple[float, float, float], axis_dir: Tuple[float, float, float], length: float, turns: int) -> List[MeshPart]:
    """One chrome helical spring + bulging bloodshot blue-eyed eyeball (`335:buglass`)."""
    ox, oy, oz = origin
    dx, dy, dz = axis_dir
    dlen = max(1e-5, math.sqrt(dx * dx + dy * dy + dz * dz))
    dx, dy, dz = dx / dlen, dy / dlen, dz / dlen
    ref = (0.0, 1.0, 0.0) if abs(dy) < 0.85 else (1.0, 0.0, 0.0)
    ux, uy, uz = dy * ref[2] - dz * ref[1], dz * ref[0] - dx * ref[2], dx * ref[1] - dy * ref[0]
    ul = max(1e-5, math.sqrt(ux * ux + uy * uy + uz * uz))
    ux, uy, uz = ux / ul, uy / ul, uz / ul
    vx, vy, vz = dy * uz - dz * uy, dz * ux - dx * uz, dx * uy - dy * ux

    pts: List[Tuple[float, float, float]] = []
    n_tot = turns * 12
    for k in range(n_tot + 1):
        u = k / float(n_tot)
        th = 2.0 * math.pi * turns * u
        rad = 0.044 * (0.86 + 0.18 * math.sin(u * math.pi))
        sag = -0.018 * math.sin(u * math.pi)
        pts.append((ox + dx * (u * length) + ux * (rad * math.cos(th)) + vx * (rad * math.sin(th)), oy + dy * (u * length) + uy * (rad * math.cos(th)) + vy * (rad * math.sin(th)) + sag, oz + dz * (u * length) + uz * (rad * math.cos(th)) + vz * (rad * math.sin(th))))
    spring = curved_tube(pts, [0.0055] * len(pts), samples_per_seg=1, radial_segs=6, name=f"{prefix}_spring", material="item_chrome_spring")

    # Bulging glossy white eyeball with bold crimson bloodshot veins radiating cleanly from the back (`335:buglass`)!
    ex, ey, ez = ox + dx * (length + 0.046), oy + dy * (length + 0.046), oz + dz * (length + 0.046)
    ball = sphere(0.072, u_segs=26, v_segs=18, name=f"{prefix}_eyeball", material="eye_white")
    eye_local: List[MeshPart] = [ball]
    for vi in range(6):
        va = vi * (2.0 * math.pi / 6.0) + 0.3
        vpts = []
        for s in range(6):
            lat = -0.35 + s * 0.25
            ang = va + 0.16 * math.sin(s * 2.4 + vi)
            r_v = 0.0728
            vpts.append((r_v * math.cos(lat) * math.cos(ang), r_v * math.cos(lat) * math.sin(ang), r_v * math.sin(lat)))
        eye_local.append(curved_tube(vpts, [0.0024, 0.0020, 0.0018, 0.0016, 0.0013, 0.0010], samples_per_seg=2, radial_segs=5, name=f"{prefix}_vein_{vi}", material="item_candy_red"))
    # Larger vivid sky-blue iris + black pupil + bright catchlight aimed slightly upward-front toward camera!
    iris = ellipsoid(0.042, 0.042, 0.018, u_segs=20, v_segs=10, name=f"{prefix}_iris", material="item_plastic_blue").translate(0.0, 0.0, +0.062)
    pupil = ellipsoid(0.022, 0.022, 0.014, u_segs=16, v_segs=8, name=f"{prefix}_pupil", material="eye_pupil").translate(0.0, 0.0, +0.071)
    glint = sphere(0.0085, u_segs=8, v_segs=6, name=f"{prefix}_glint", material="eye_catchlight").translate(-0.012, +0.014, +0.079)
    eye_local.extend([iris, pupil, glint])
    yaw_e = math.atan2(dx, dz)
    pitch_e = -math.asin(max(-0.99, min(0.99, dy))) - 0.30
    eye_parts = [p.rotate_xyz(rx=pitch_e, ry=yaw_e).translate(ex, ey, ez) for p in eye_local]
    return [spring, *eye_parts]


def build_buglass() -> SceneBuilder:
    """Purple novelty bug-eye glasses with twin gleaming chrome coil springs & bloodshot eyeballs (`335:buglass`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_buglass")
    parts: List[MeshPart] = []

    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        rim = torus_arc(0.088, 0.014, u_segs=28, v_segs=10, plane="xy", name=f"bug_rim_{tag}", material="item_glasses_purple").scale(1.12, 0.88, 1.0).translate(side * 0.115, 0.088, 0.0)
        arm = curved_tube(
            [(side * 0.210, 0.105, -0.008), (side * 0.214, 0.095, -0.22), (side * 0.205, 0.082, -0.44), (side * 0.194, 0.032, -0.49)],
            [0.011, 0.010, 0.009, 0.008],
            samples_per_seg=4,
            radial_segs=8,
            cap_rings=2,
            name=f"bug_arm_{tag}",
            material="item_glasses_purple",
        )
        parts.extend([rim, arm])
    bridge = curved_tube([(-0.025, 0.098, 0.0), (0.0, 0.110, 0.008), (+0.025, 0.098, 0.0)], [0.012, 0.011, 0.012], samples_per_seg=3, radial_segs=8, name="bug_bridge", material="item_glasses_purple")
    parts.append(bridge)

    parts.extend(_build_spring_eyeball("bug_eye_l", origin=(-0.115, 0.088, +0.010), axis_dir=(-0.05, -0.04, +1.00), length=0.40, turns=11))
    parts.extend(_build_spring_eyeball("bug_eye_r", origin=(+0.115, 0.088, +0.010), axis_dir=(+0.08, -0.02, +1.00), length=0.22, turns=7))

    sc.add("buglass", [p.rotate_xyz(ry=26.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.96)


def build_ball() -> SceneBuilder:
    """Glossy cherry-red rubber jacks bounce ball (`344:ball`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_ball")
    b = tint_part(
        sphere(0.28, u_segs=36, v_segs=24, name="jacks_ball", material="item_candy_red").translate(0.0, 0.28, 0.0),
        lambda pos, nrm: np.clip(
            np.column_stack([
                0.96 + 0.04 * nrm[:, 1],
                0.86 + 0.14 * np.clip(nrm[:, 1] + 0.3, 0.0, 1.0),
                0.84 + 0.16 * np.clip(nrm[:, 1] + 0.3, 0.0, 1.0),
            ]),
            0.55,
            1.0,
        ),
    )
    sc.add("ball", b)
    return _finalize_item(sc, target_span=0.80)


def build_chicken() -> SceneBuilder:
    """Iconic yellow rubber chicken stretched diagonally with splayed pink webbed feet & screaming beak (`347:chicken`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_chicken")
    parts: List[MeshPart] = []

    # 1. Plump pebbled hollow yellow rubber torso + tucked little folded wings
    torso = (
        superellipsoid(0.118, 0.082, 0.180, e1=0.82, e2=0.92, u_segs=32, v_segs=18, name="chicken_torso", material="item_duck_yellow")
        .deform(lambda p: p + 0.0028 * np.sin(p * 65.0) * np.cos(np.roll(p, 1, axis=1) * 60.0))
        .translate(0.0, 0.088, -0.05)
    )
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        wing = ellipsoid(0.036, 0.032, 0.108, u_segs=16, v_segs=10, name=f"chicken_wing_{tag}", material="item_duck_yellow").rotate_xyz(rx=0.14, ry=side * 0.24).translate(side * 0.106, 0.095, -0.05)
        parts.append(wing)

    # 2. Plump drumstick thighs tapering to splayed yellow legs + broad hot-pink webbed 3-toed feet (`347:chicken`)!
    for side, tag, spread_x, end_z in ((-1.0, "l", -0.112, -0.35), (+1.0, "r", +0.096, -0.37)):
        thigh = ellipsoid(0.048, 0.042, 0.082, u_segs=14, v_segs=10, name=f"chicken_thigh_{tag}", material="item_duck_yellow").rotate_xyz(ry=side * 0.26).translate(side * 0.068, 0.074, -0.195)
        leg = curved_tube(
            [(side * 0.062, 0.072, -0.19), (side * 0.088, 0.054, -0.27), (spread_x, 0.032, end_z)],
            [0.038, 0.022, 0.014],
            samples_per_seg=4,
            radial_segs=12,
            name=f"chicken_leg_{tag}",
            material="item_duck_yellow",
        )
        foot_pad = ellipsoid(0.040, 0.013, 0.046, u_segs=14, v_segs=8, name=f"chicken_footpad_{tag}", material="item_hot_pink").rotate_xyz(ry=-side * 0.22).translate(spread_x + side * 0.010, 0.022, end_z - 0.035)
        parts.extend([thigh, leg, foot_pad])
        for ti, tang in enumerate((-0.55, 0.0, +0.55)):
            tx = spread_x + math.sin(tang + side * 0.22) * 0.105
            tz = end_z - math.cos(tang + side * 0.22) * 0.105
            toe = curved_tube(
                [(spread_x, 0.026, end_z), (0.5 * (spread_x + tx), 0.022, 0.5 * (end_z + tz)), (tx, 0.015, tz)],
                [0.016, 0.013, 0.008],
                samples_per_seg=2,
                radial_segs=8,
                cap_rings=2,
                name=f"chicken_toe_{tag}_{ti}",
                material="item_hot_pink",
            )
            parts.append(toe)

    # 3. Long outstretched rubber neck + screaming yellow-beak head, oversized dramatic red comb & wattle (`347:chicken`)!
    neck = curved_tube(
        [(0.0, 0.092, +0.08), (+0.012, 0.086, +0.25), (-0.010, 0.092, +0.43), (0.0, 0.112, +0.56)],
        [0.055, 0.034, 0.028, 0.038],
        samples_per_seg=5,
        radial_segs=16,
        name="chicken_neck",
        material="item_duck_yellow",
    )
    head = ellipsoid(0.054, 0.052, 0.068, u_segs=20, v_segs=14, name="chicken_head", material="item_duck_yellow").translate(0.0, 0.122, +0.590)
    # Wide-open yelling golden-yellow beak jaws + dark open throat + bold crimson rooster comb & wattle (`347:chicken`)!
    beak_u = ellipsoid(0.026, 0.018, 0.068, u_segs=14, v_segs=10, name="chicken_beak_u", material="item_pollen_gold").rotate_xyz(rx=-0.42).translate(0.0, 0.150, +0.678)
    beak_l = ellipsoid(0.024, 0.016, 0.062, u_segs=14, v_segs=10, name="chicken_beak_l", material="item_pollen_gold").rotate_xyz(rx=+0.45).translate(0.0, 0.096, +0.668)
    mouth_in = sphere(0.030, u_segs=10, v_segs=8, name="chicken_mouth", material="item_dark_stitch").translate(0.0, 0.120, +0.642)
    # Big dramatic fanned 4-lobe hot-red comb sweeping up-left off the crown + large red wattle below jaw (`347:chicken`)!
    for k, (cx, cy, cz, cr, rz_c) in enumerate((
        (+0.044, 0.152, +0.555, 0.044, -0.62),
        (+0.058, 0.180, +0.585, 0.048, -0.42),
        (+0.048, 0.196, +0.620, 0.044, -0.20),
        (+0.026, 0.198, +0.648, 0.036, -0.06),
    )):
        parts.append(ellipsoid(0.018, cr, cr * 0.72, u_segs=14, v_segs=10, name=f"chicken_comb_{k}", material="item_hot_pink").rotate_xyz(rz=rz_c).translate(cx, cy, cz))
    wattle = ellipsoid(0.038, 0.044, 0.036, u_segs=14, v_segs=10, name="chicken_wattle", material="item_hot_pink").translate(-0.034, 0.088, +0.608)
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        parts.append(sphere(0.017, u_segs=12, v_segs=8, name=f"chicken_eye_b_{tag}", material="item_plastic_blue").translate(side * 0.044, 0.140, +0.612))
        parts.append(sphere(0.009, u_segs=8, v_segs=6, name=f"chicken_eye_p_{tag}", material="eye_pupil").translate(side * 0.054, 0.142, +0.618))

    parts.extend([torso, neck, head, beak_u, beak_l, mouth_in, wattle])
    sc.add("chicken", [p.rotate_xyz(ry=136.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.98)


def build_sling() -> SceneBuilder:
    """Wooden Y-slingshot with crimson surgical bands weaving across dark charcoal curved leather pouch (`350:sling`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_sling")
    parts: List[MeshPart] = []

    handle = _sculpt_bark_branch(
        [(+0.27, 0.024, +0.34), (+0.21, 0.028, +0.24), (+0.14, 0.032, +0.13), (+0.11, 0.034, +0.08)],
        [0.030, 0.032, 0.035, 0.034],
        name="sling_handle",
        material="item_twig_bark",
        radial_segs=18,
        samples_per_seg=4,
        seed=2.4,
        cap_start=True,
        cap_end=True,
    )
    fork_l = _sculpt_bark_branch(
        [(+0.11, 0.034, +0.08), (+0.02, 0.036, +0.09), (-0.06, 0.038, +0.06)],
        [0.030, 0.026, 0.024],
        name="sling_fork_l",
        material="item_twig_bark",
        radial_segs=16,
        samples_per_seg=4,
        seed=4.1,
        cap_start=True,
        cap_end=True,
    )
    fork_r = _sculpt_bark_branch(
        [(+0.11, 0.034, +0.08), (+0.14, 0.036, -0.02), (+0.15, 0.038, -0.12)],
        [0.030, 0.026, 0.024],
        name="sling_fork_r",
        material="item_twig_bark",
        radial_segs=16,
        samples_per_seg=4,
        seed=5.7,
        cap_start=True,
        cap_end=True,
    )
    # Little broken twig nub jutting off the upper-right fork just like `350:sling`!
    nub = _sculpt_bark_branch(
        [(+0.14, 0.036, -0.02), (+0.162, 0.039, -0.038), (+0.185, 0.042, -0.055)],
        [0.012, 0.0085, 0.005],
        name="sling_nub",
        material="item_twig_bark",
        radial_segs=10,
        samples_per_seg=2,
        seed=7.2,
        cap_start=True,
        cap_end=True,
    )
    parts.extend([handle, fork_l, fork_r, nub])

    wrap_l = cylinder(0.032, 0.032, 0.045, radial_segs=16, bevel=0.005, axis="x", name="sling_wrap_l", material="item_candy_red").rotate_xyz(ry=-0.32).translate(-0.055, 0.038, +0.062)
    wrap_r = cylinder(0.032, 0.032, 0.045, radial_segs=16, bevel=0.005, axis="z", name="sling_wrap_r", material="item_candy_red").translate(+0.148, 0.038, -0.115)
    parts.extend([wrap_l, wrap_r])

    # Curved dark charcoal-slate leather ammo pouch (`350:sling`) with two dark punched eyelet holes!
    pouch = tint_part(
        rounded_box(0.32, 0.020, 0.22, bevel=0.007, segs=3, name="sling_pouch", material="item_rubber_dark")
        .deform(lambda p: np.column_stack([p[:, 0], p[:, 1] + 0.052 * ((p[:, 0] / 0.16) ** 2) - 0.016 * ((p[:, 2] / 0.11) ** 2), p[:, 2]]))
        .translate(-0.07, 0.030, -0.22),
        lambda pos, nrm: np.clip(
            (0.85 + 0.35 * np.clip(nrm[:, 1], 0.0, 1.0) + 0.28 * np.exp(-(((pos[:, 0] + 0.01) / 0.05) ** 2)))[:, None]
            * np.array([[0.92, 0.94, 0.98]]),
            0.35,
            1.0,
        ),
    )
    for ex, tag in ((-0.19, "l"), (+0.05, "r")):
        parts.append(cylinder(0.020, 0.020, 0.034, radial_segs=14, axis="y", name=f"sling_hole_{tag}", material="item_soil_hole").translate(ex, 0.068, -0.22))

    # Red surgical rubber band looping from left fork -> left pouch hole -> WAVING ACROSS FRONT OF POUCH -> right hole -> right fork (`350:sling`)!
    band_full = curved_tube(
        [
            (-0.06, 0.038, +0.06), (-0.19, 0.024, +0.10), (-0.32, 0.020, +0.05),
            (-0.36, 0.024, -0.07), (-0.29, 0.048, -0.19), (-0.19, 0.084, -0.22),
            (-0.12, 0.084, -0.242), (-0.06, 0.080, -0.200), (+0.00, 0.084, -0.220), (+0.05, 0.086, -0.22),
            (+0.16, 0.052, -0.25), (+0.21, 0.034, -0.20), (+0.15, 0.038, -0.12),
        ],
        [0.0115] * 13,
        samples_per_seg=5,
        radial_segs=10,
        cap_rings=2,
        name="sling_band",
        material="item_candy_red",
    )
    parts.extend([pouch, band_full])
    sc.add("sling", parts)
    return _finalize_item(sc, target_span=0.96)


def build_lunchbox() -> SceneBuilder:
    """Miniature upright retro yellow-domed chestnut-brown enameled metal lunchbox (`356:lunchbox`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_lunchbox")
    # Smooth painted enamel metal box (zero burlap cloth texture) matching `356:lunchbox`!
    box_body = tint_part(
        rounded_box(0.28, 0.29, 0.21, bevel=0.024, segs=3, name="lunchbox_body", material="item_candy_white").translate(0.0, 0.145, 0.0),
        _const_tint((0.36, 0.14, 0.06)),
    )
    lid = tint_part(
        superellipsoid(0.150, 0.115, 0.116, e1=0.55, e2=0.55, u_segs=28, v_segs=16, name="lunchbox_lid", material="item_candy_white")
        .deform(lambda p: np.column_stack([p[:, 0], np.where(p[:, 1] < 0.0, p[:, 1] * 0.25, p[:, 1]), p[:, 2]]))
        .translate(0.0, 0.305, 0.0),
        _const_tint((0.98, 0.78, 0.04)),
    )
    clasp = rounded_box(0.058, 0.056, 0.022, bevel=0.005, segs=2, name="lunchbox_clasp", material="item_gear_brass").translate(0.0, 0.275, +0.110)
    handle = curved_tube(
        [(-0.072, 0.395, 0.0), (-0.068, 0.460, 0.0), (0.0, 0.478, 0.0), (+0.068, 0.460, 0.0), (+0.072, 0.395, 0.0)],
        [0.013, 0.013, 0.014, 0.013, 0.013],
        samples_per_seg=6,
        radial_segs=10,
        cap_rings=2,
        name="lunchbox_handle",
        material="item_rubber_dark",
    )
    sc.add("lunchbox", [p.rotate_xyz(ry=-24.0, degrees=True) for p in (box_body, lid, clasp, handle)])
    return _finalize_item(sc, target_span=0.72)


def _remap_crayon_wrapper_uv(part: MeshPart, z_min: float, z_max: float, v_lo: float = 0.0, v_hi: float = 1.0) -> MeshPart:
    """Map cylindrical UVs cleanly so `v` runs 0..1 along length (bands at ends!) & `u=0.25` ('Krayawn') faces straight UP!"""
    pos = part.positions
    ang = np.arctan2(pos[:, 0], pos[:, 1] - 0.047)  # 0 at top (+Y)
    u = (ang / (2.0 * math.pi) + 0.25) % 1.0
    v = v_lo + (v_hi - v_lo) * np.clip((pos[:, 2] - z_min) / max(1e-5, z_max - z_min), 0.0, 1.0)
    part.uvs = np.column_stack([u, v]).astype(np.float32)
    return part


def _build_crayon(
    name: str,
    *,
    wax_mat: str,
    paper_tint: Tuple[float, float, float],
    yaw_deg: float,
    snapped: bool = False,
) -> SceneBuilder:
    """Build realistic 'Krayawn' wax crayon with tapered cone tip & properly UV-mapped paper wrapper (`352..355`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []
    ptint = _const_tint(paper_tint)

    if snapped:
        def _jagged_end(pos: np.ndarray, end_z: float, sign: float) -> np.ndarray:
            p = pos.copy()
            near = np.exp(-(((p[:, 2] - end_z) / 0.030) ** 2))
            ang = np.arctan2(p[:, 1] - 0.046, p[:, 0])
            p[:, 2] += sign * near * (0.018 * np.sin(ang * 3.0 + 0.5) + 0.011 * np.cos(ang * 5.0))
            return p

        tip = cylinder(0.015, 0.044, 0.135, radial_segs=22, bevel=0.004, axis="z", name=f"{name}_tip", material=wax_mat).translate(0.0, 0.046, -0.375)
        neck = cylinder(0.044, 0.046, 0.040, radial_segs=22, axis="z", name=f"{name}_neck", material=wax_mat).translate(0.0, 0.046, -0.295)
        core_l = cylinder(0.045, 0.045, 0.375, radial_segs=24, axis="z", name=f"{name}_core_l", material=wax_mat).translate(0.0, 0.046, -0.090).deform(lambda p: _jagged_end(p, +0.097, +1.0))
        wrap_l = cylinder(0.0478, 0.0478, 0.345, radial_segs=28, axis="z", name=f"{name}_wrap_l", material="item_crayon_paper").translate(0.0, 0.046, -0.100)
        wrap_l = tint_part(_remap_crayon_wrapper_uv(wrap_l, -0.2725, +0.0725, 0.0, 0.68).deform(lambda p: _jagged_end(p, +0.0725, +1.0)), ptint)

        core_r = cylinder(0.045, 0.045, 0.195, radial_segs=24, bevel=0.004, axis="z", name=f"{name}_core_r", material=wax_mat).translate(0.0, 0.046, +0.250).deform(lambda p: _jagged_end(p, +0.152, -1.0))
        wrap_r = cylinder(0.0478, 0.0478, 0.125, radial_segs=28, axis="z", name=f"{name}_wrap_r", material="item_crayon_paper").translate(0.0, 0.046, +0.228)
        wrap_r = tint_part(_remap_crayon_wrapper_uv(wrap_r, +0.1655, +0.2905, 0.72, 1.00).deform(lambda p: _jagged_end(p, +0.1655, -1.0)), ptint)
        r_pieces = [p.rotate_xyz(ry=5.5, rz=-2.5, degrees=True).translate(+0.008, 0.0, +0.015) for p in (core_r, wrap_r)]
        parts.extend([tip, neck, core_l, wrap_l, *r_pieces])
    else:
        tip = cylinder(0.014, 0.045, 0.145, radial_segs=24, bevel=0.004, axis="z", name=f"{name}_tip", material=wax_mat).translate(0.0, 0.047, -0.395)
        wax_core = cylinder(0.045, 0.045, 0.720, radial_segs=24, bevel=0.004, axis="z", name=f"{name}_core", material=wax_mat).translate(0.0, 0.047, +0.035)
        wrapper = cylinder(0.048, 0.048, 0.610, radial_segs=28, axis="z", name=f"{name}_wrapper", material="item_crayon_paper").translate(0.0, 0.047, +0.025)
        wrapper = tint_part(_remap_crayon_wrapper_uv(wrapper, -0.280, +0.330, 0.0, 1.0), ptint)
        parts.extend([tip, wax_core, wrapper])

    sc.add("crayon", [p.rotate_xyz(ry=yaw_deg, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.96)


def build_pinkcray() -> SceneBuilder:
    """Pink 'Krayawn' wax crayon leaning slightly left (`352:pinkcray`)."""
    return _build_crayon("item_pinkcray", wax_mat="item_wax_pink", paper_tint=(1.00, 0.78, 0.90), yaw_deg=+14.0)


def build_grncray() -> SceneBuilder:
    """Snapped-in-half horizontal green 'Krayawn' crayon (`353:grncray`)."""
    return _build_crayon("item_grncray", wax_mat="item_wax_green", paper_tint=(0.88, 1.00, 0.88), yaw_deg=+90.0, snapped=True)


def build_yelcray() -> SceneBuilder:
    """Diagonal yellow 'Krayawn' crayon pointing down-left (`354:yelcray`)."""
    return _build_crayon("item_yelcray", wax_mat="item_wax_yellow", paper_tint=(1.00, 0.94, 0.72), yaw_deg=+152.0)


def build_blucray() -> SceneBuilder:
    """Diagonal blue 'Krayawn' crayon pointing up-right (`355:blucray`)."""
    return _build_crayon("item_blucray", wax_mat="item_wax_blue", paper_tint=(0.68, 0.82, 1.00), yaw_deg=-52.0)


def _build_metal_jack(name: str, *, rot_deg: Tuple[float, float, float]) -> SceneBuilder:
    """Six-pronged warm pewter-steel game jack (4 sleek ball-tipped arms + 2 sharp pointed prongs) (`336..339`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    pewter = _const_tint((0.86, 0.83, 0.78))
    parts: List[MeshPart] = [tint_part(sphere(0.036, u_segs=14, v_segs=10, name=f"{name}_hub", material="item_chrome_spring"), pewter)]
    # X and Z axes: 4 slender classic flared arms capped with neat cast-metal spheres
    for axis, tag in (("x", "x"), ("z", "z")):
        parts.append(tint_part(cylinder(0.017, 0.017, 0.56, radial_segs=12, axis=axis, name=f"{name}_bar_{tag}", material="item_chrome_spring"), pewter))
        for sgn in (-1.0, +1.0):
            pos = (sgn * 0.28, 0.0, 0.0) if axis == "x" else (0.0, 0.0, sgn * 0.28)
            parts.append(tint_part(sphere(0.034, u_segs=14, v_segs=10, name=f"{name}_ball_{tag}_{int(sgn > 0)}", material="item_chrome_spring").translate(*pos), pewter))
    # Y axis: 2 classic sharp conical tapered pointed metal prongs! (cylinder takes r_bot then r_top)
    for sgn, tag in ((+1.0, "up"), (-1.0, "dn")):
        prong = cylinder(0.022 if sgn > 0 else 0.002, 0.002 if sgn > 0 else 0.022, 0.27, radial_segs=12, bevel=0.001, axis="y", name=f"{name}_prong_{tag}", material="item_chrome_spring").translate(0.0, sgn * 0.135, 0.0)
        parts.append(tint_part(prong, pewter))

    rx, ry, rz = rot_deg
    sc.add("jack", [p.rotate_xyz(rx=rx, ry=ry, rz=rz, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.84)


def build_jacks1() -> SceneBuilder:
    return _build_metal_jack("item_jacks1", rot_deg=(28.0, 18.0, 82.0))


def build_jacks2() -> SceneBuilder:
    return _build_metal_jack("item_jacks2", rot_deg=(48.0, 82.0, -18.0))


def build_jacks3() -> SceneBuilder:
    return _build_metal_jack("item_jacks3", rot_deg=(-34.0, 122.0, 32.0))


def build_jacks4() -> SceneBuilder:
    return _build_metal_jack("item_jacks4", rot_deg=(58.0, -42.0, -34.0))
