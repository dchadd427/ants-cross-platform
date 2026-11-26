"""Realistic 3D sculptors for 1998 Ants Toys & Backyard Props Wave 3A (21 Hero Toys, Crayons & Jacks).

Covers:
- `glasses`  (236): Teal-and-navy cat-eye eyeglasses with clear glass lenses
- `bobber`   (237): Red-and-white round plastic fishing bobber with thumb plunger
- `can2`     (239): Horizontal crushed Critter Cola soda can (pull-tab lid on left)
- `yoyo`     (240): Cream-white vintage yo-yo with red racing stripes & looped white cotton string
- `brush`    (241): Retro airbrush / water squirt pistol (black grip, red trigger & rocket nozzle)
- `slinky`   (242): Standing upright gunmetal flat-ribbon spring-steel Slinky in a half-circle arch
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
        from .items_toys import _finalize_item, build_soda_can_lid_parts
    except ImportError:
        from map_items.item_materials import gradient_y_tint, register_item_materials, tint_part  # type: ignore
        from map_items.items_foliage import _sculpt_bark_branch  # type: ignore
        from map_items.items_toys import _finalize_item, build_soda_can_lid_parts  # type: ignore
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
    """Sculpt one sharp upswept cat-eye rim with emerald/navy checkered brow & curved clear lens (`236:glasses`)."""
    pts: List[Tuple[float, float, float]] = []
    rads: List[float] = []
    n = 36
    for i in range(n + 1):
        a = 2.0 * math.pi * (i % n) / n
        ca, sa = math.cos(a), math.sin(a)
        # Sharp upswept retro cat-eye outer wing horn & sleek tapered almond lower loop matching `sprite_0370.png`!
        out_up = max(0.0, sa + 0.15) * max(0.0, side * ca + 0.10)
        lx = 0.138 * ca + side * 0.068 * (out_up ** 1.35)
        ly = 0.072 * sa + 0.062 * (out_up ** 1.15) - 0.010 * max(0.0, -sa)
        lz = -0.024 * (lx * lx * 14.0)
        pts.append((side * 0.168 + lx, ly, lz))
        # Thicker sculpted top brow & wing tips, slimmer dark lower eye-wire!
        rads.append(0.0175 if sa > 0.08 else 0.0115)
    rim = curved_tube(pts, rads, samples_per_seg=2, radial_segs=10, cap_start=False, cap_end=False, name=f"{name}_rim", material=mat)

    def _brow_check_tint(pos: np.ndarray, _n: np.ndarray) -> np.ndarray:
        # Authentic crisp emerald-green & deep midnight-navy checkered mosaic on top brow/wings,
        # transitioning to glossy solid midnight-navy on the lower rims (`sprite_0370.png`)!
        is_brow = np.clip((pos[:, 1] + 0.008) / 0.032, 0.0, 1.0)
        cx = np.floor(pos[:, 0] * 34.0)
        cy = np.floor((pos[:, 1] + pos[:, 2] * 0.4) * 34.0)
        checker = ((cx + cy) % 2.0 > 0.5).astype(np.float64)
        green = np.array([0.14, 0.88, 0.28], dtype=np.float64)
        navy = np.array([0.10, 0.16, 0.56], dtype=np.float64)
        brow_col = navy[None, :] * (1.0 - checker[:, None]) + green[None, :] * checker[:, None]
        return np.clip(navy[None, :] * (1.0 - is_brow[:, None]) + brow_col * is_brow[:, None], 0.08, 1.0)

    rim = tint_part(rim, _brow_check_tint)
    # Curved cat-eye lens shaped to match the upswept aperture!
    lens = ellipsoid(0.132, 0.068, 0.007, u_segs=22, v_segs=10, name=f"{name}_lens", material="item_lens_clear").deform(
        lambda p: np.column_stack([
            p[:, 0] + side * 0.045 * np.clip(p[:, 1] / 0.07, 0.0, 1.0) * np.clip(side * p[:, 0] / 0.13, 0.0, 1.0),
            p[:, 1] + 0.035 * np.clip(p[:, 1] / 0.07, 0.0, 1.0) * np.clip(side * p[:, 0] / 0.13, 0.0, 1.0),
            p[:, 2] - 0.020 * ((p[:, 0] / 0.13) ** 2),
        ])
    ).translate(side * 0.168, 0.006, -0.005)
    return [rim, lens]


def _temple_arm_ribbon(name: str, pts: List[Tuple[float, float, float]], heights: List[float], thick: float = 0.0065) -> MeshPart:
    """Sculpt a sleek flat vertical-ribbon acetate temple arm with flared curved paddle ear-tip (`sprite_0370.png`)."""
    # Build smooth spine via Catmull-Rom-like dense interpolation and sweep an elliptical flat ribbon cross-section.
    arr = np.asarray(pts, dtype=np.float64)
    hs = np.asarray(heights, dtype=np.float64)
    dense_p: List[np.ndarray] = []
    dense_h: List[float] = []
    subs = 6
    for i in range(len(arr) - 1):
        p0 = arr[max(0, i - 1)]
        p1 = arr[i]
        p2 = arr[i + 1]
        p3 = arr[min(len(arr) - 1, i + 2)]
        h1, h2 = hs[i], hs[i + 1]
        for s in range(subs if i < len(arr) - 2 else subs + 1):
            t = s / float(subs)
            t2, t3 = t * t, t * t * t
            pos = 0.5 * ((2.0 * p1) + (-p0 + p2) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3)
            dense_p.append(pos)
            dense_h.append(float(h1 + (h2 - h1) * t))

    n_rings = len(dense_p)
    n_seg = 10
    verts: List[Tuple[float, float, float]] = []
    tris: List[Tuple[int, int, int]] = []
    up_ref = np.array([0.0, 1.0, 0.0], dtype=np.float64)
    for ir in range(n_rings):
        p = dense_p[ir]
        tan = dense_p[min(n_rings - 1, ir + 1)] - dense_p[max(0, ir - 1)]
        tan /= max(1e-8, float(np.linalg.norm(tan)))
        side_v = np.cross(tan, up_ref)
        sn = float(np.linalg.norm(side_v))
        if sn < 1e-6:
            side_v = np.array([1.0, 0.0, 0.0], dtype=np.float64)
        else:
            side_v /= sn
        up_v = np.cross(side_v, tan)
        up_v /= max(1e-8, float(np.linalg.norm(up_v)))
        hh = dense_h[ir]
        for j in range(n_seg):
            ang = 2.0 * math.pi * j / n_seg
            vtx = p + side_v * (thick * math.cos(ang)) + up_v * (hh * math.sin(ang))
            verts.append((float(vtx[0]), float(vtx[1]), float(vtx[2])))
    for ir in range(n_rings - 1):
        for j in range(n_seg):
            jn = (j + 1) % n_seg
            a, b = ir * n_seg + j, (ir + 1) * n_seg + j
            c, d = (ir + 1) * n_seg + jn, ir * n_seg + jn
            tris.extend(((a, c, b), (a, d, c)))
    # Cap both ends cleanly so the temple arm is completely watertight.
    c0_idx = len(verts)
    verts.append((float(dense_p[0][0]), float(dense_p[0][1]), float(dense_p[0][2])))
    c1_idx = len(verts)
    verts.append((float(dense_p[-1][0]), float(dense_p[-1][1]), float(dense_p[-1][2])))
    for j in range(n_seg):
        jn = (j + 1) % n_seg
        tris.append((c0_idx, jn, j))
        base = (n_rings - 1) * n_seg
        tris.append((c1_idx, base + j, base + jn))
    pos_a = np.asarray(verts, dtype=np.float32)
    idx_a = np.asarray(tris, dtype=np.uint32).reshape(-1)
    arm_mesh = MeshPart(name, "item_rubber_blue", pos_a, recompute_smooth_normals(pos_a, idx_a), idx_a)
    return tint_part(arm_mesh, _const_tint((0.14, 0.20, 0.56)))


def build_glasses() -> SceneBuilder:
    """Emerald-checkered & navy cat-eye eyeglasses with folded left temple arm (`236:glasses`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_glasses")
    parts: List[MeshPart] = []
    navy_tint = _const_tint((0.14, 0.20, 0.56))
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        parts.extend(_cat_eye_rim(f"glasses_{tag}", side))
        hinge = tint_part(
            rounded_box(0.036, 0.026, 0.020, bevel=0.004, segs=2, name=f"glasses_hinge_{tag}", material="item_rubber_blue").translate(side * 0.344, 0.068, -0.028),
            navy_tint,
        )
        if side < 0:
            # LEFT temple arm folded sharply inward (~52°!) across behind the lenses with flattened paddle ear-hook (`sprite_0370.png`)!
            arm = _temple_arm_ribbon(
                f"glasses_arm_{tag}",
                [
                    (-0.346, 0.068, -0.032),
                    (-0.185, 0.062, -0.155),
                    (-0.005, 0.052, -0.268),
                    (+0.135, 0.042, -0.340),
                    (+0.205, 0.014, -0.365),
                    (+0.225, -0.042, -0.372),
                ],
                [0.017, 0.0135, 0.0115, 0.0125, 0.0185, 0.020],
                thick=0.0062,
            )
        else:
            # RIGHT temple arm swung wide open (~96°), running sleekly straight back-right (`sprite_0370.png`)!
            arm = _temple_arm_ribbon(
                f"glasses_arm_{tag}",
                [
                    (+0.346, 0.068, -0.032),
                    (+0.362, 0.062, -0.220),
                    (+0.378, 0.052, -0.430),
                    (+0.388, 0.024, -0.560),
                    (+0.392, -0.032, -0.605),
                ],
                [0.016, 0.0125, 0.0105, 0.0135, 0.016],
                thick=0.0062,
            )
        pad = ellipsoid(0.011, 0.020, 0.007, u_segs=10, v_segs=8, name=f"glasses_pad_{tag}", material="item_candy_white").translate(side * 0.042, -0.012, -0.022)
        parts.extend([hinge, arm, pad])

    bridge = tint_part(
        curved_tube([(-0.044, 0.028, -0.002), (0.0, 0.042, +0.008), (+0.044, 0.028, -0.002)], [0.0145, 0.0135, 0.0145], samples_per_seg=3, radial_segs=10, name="glasses_bridge", material="item_glasses_teal"),
        lambda pos, _n: np.where(
            (((np.floor(pos[:, 0] * 34.0) + np.floor(pos[:, 1] * 34.0)) % 2.0) > 0.5)[:, None],
            np.array([[0.14, 0.88, 0.28]]),
            np.array([[0.10, 0.16, 0.56]]),
        ),
    )
    parts.append(bridge)
    sc.add("glasses", [p.rotate_xyz(rx=10.0, ry=-36.0, rz=-4.0, degrees=True) for p in parts])
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
    sc.nodes["bobber"] = [p.rotate_xyz(rx=14.0, ry=-10.0, rz=-66.0, degrees=True) for p in sc.nodes["bobber"]]
    return _finalize_item(sc, target_span=0.88)


def build_can2() -> SceneBuilder:
    """Heavily flat-crushed Critter Cola can: round silver pull-tab lid on LEFT, flattened oval base on RIGHT (`239:can2`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_can2")

    def _crush_flat(pos: np.ndarray) -> np.ndarray:
        """Heavy pancake stomp crush from right behind the round left lid all the way through the right bottom rim (`sprite_0373.png`)."""
        p = pos.copy()
        z = p[:, 2]
        # Keep the open silver top lid (+Z > +0.23) round and intact, then rapidly stomp-flatten the entire barrel & base!
        s = np.clip((0.235 - z) / 0.28, 0.0, 1.0)
        t_flat = s * s * (3.0 - 2.0 * s)
        # Deep collapsed neck pinch right behind the round lid shoulder (`z ~ +0.08`)!
        neck_pinch = np.exp(-(((z - 0.075) / 0.115) ** 2))
        end_flare = np.clip((0.08 - z) / 0.38, 0.0, 1.0)

        # Compress front-to-back (local X) ~68% flat across mid-barrel & right bottom rim!
        x_scale = 1.0 - 0.68 * t_flat
        # Flare vertical height (local Y) up to ~1.22x at the stomped right rim while pinching tightly at the neck waist!
        y_scale = 1.0 + 0.20 * end_flare - 0.16 * neck_pinch
        p[:, 0] *= x_scale
        p[:, 1] *= y_scale

        # Sharp metallic angular diagonal fold creases across the crushed front & rear aluminum sheets!
        crease_a = 1.0 - np.abs(np.sin(p[:, 1] * 14.5 + z * 11.5 - 0.4))
        crease_b = 1.0 - np.abs(np.sin(p[:, 1] * 19.0 - z * 16.0 + 1.1))
        body_mid = np.clip((0.21 - z) / 0.10, 0.0, 1.0) * np.clip((z + 0.26) / 0.09, 0.0, 1.0)
        front_sign = np.where(p[:, 0] >= 0.0, 1.0, -1.0)
        p[:, 0] -= front_sign * 0.022 * body_mid * (0.45 + 0.35 * crease_a + 0.20 * crease_b)
        # Level horizontal body silhouette rising only slightly toward the tall squashed right rim (`sprite_0373.png`)!
        p[:, 1] += 0.014 * (end_flare ** 1.15) - 0.016 * neck_pinch
        p[:, 0] -= 0.020 * neck_pinch
        return p

    n_rings, n_rad = 44, 72
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
        tex_v = 1.0 - vf  # v=0 near open lid (+Z), v=1 near crushed bottom rim (-Z)
        for j in range(n_rad + 1):
            uf = j / float(n_rad)
            # Align upside-down diagonal 'Critter Cola' squarely across the flat front crushed sheet (`+X` faces camera!)
            # with spiral 'a' upper-left near lid, 'C' lower-right, 'Critter' near bottom red shoulder (`sprite_0373.png`)!
            ang = -math.pi * 0.5 + uf * 2.0 * math.pi - 0.96
            verts.append((rad * math.cos(ang), rad * math.sin(ang), z))
            uvs.append((uf, tex_v))
    stride = n_rad + 1
    for ir in range(n_rings):
        for j in range(n_rad):
            a, b, c, d = ir * stride + j, (ir + 1) * stride + j, (ir + 1) * stride + (j + 1), ir * stride + (j + 1)
            tris.extend(((a, c, b), (a, d, c)))
    pos_b = _crush_flat(np.asarray(verts, dtype=np.float32))
    idx_b = np.asarray(tris, dtype=np.uint32).reshape(-1)
    body_shell = MeshPart("can2_label_wall", "item_cola_label", pos_b, recompute_smooth_normals(pos_b, idx_b), idx_b, uvs=np.asarray(uvs, dtype=np.float32))

    top_end = lathe([(0.144, +0.295), (0.151, +0.312), (0.146, +0.327), (0.132, +0.329), (0.125, +0.307), (0.110, +0.305), (0.000, +0.304)], radial_segs=48, cap_bottom=False, cap_top=False, axis="z", name="can2_top_lid", material="item_aluminum").deform(_crush_flat)
    bot_end = lathe([(0.000, -0.308), (0.098, -0.306), (0.128, -0.324), (0.146, -0.318), (0.156, -0.285)], radial_segs=44, cap_bottom=False, cap_top=False, axis="z", name="can2_bottom", material="item_aluminum").deform(_crush_flat)
    # Orient dark punched drink mouth at upper-right of the left lid & figure-8 pull-tab sweeping down-left (`sprite_0373.png`)!
    lid_parts = build_soda_can_lid_parts("can2", lid_dx=0.0, lid_dy=0.0, lid_z=+0.306, tab_angle_deg=-116.0)
    parts = [body_shell, top_end, bot_end, *lid_parts]
    # Lay horizontally: round pull-tab lid on LEFT facing front-left, tall stomped silver rim level on RIGHT (`sprite_0373.png`)!
    sc.add("can", [p.rotate_xyz(rx=-10.0, ry=-52.0, rz=-11.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.96)


def _yoyo_sunburst_emblem(name: str, y_pole: float, dome_r: float = 0.248, dome_h: float = 0.226) -> List[MeshPart]:
    """Flush curved 8-ray alternating cherry-red & smooth golden-yellow sunburst cheek decal hugging the plump yo-yo dome (`sprite_0374.png`)."""
    out: List[MeshPart] = []
    n_rays = 8
    r_max = 0.118
    gold_tint = _const_tint((0.98, 0.80, 0.12))
    for i in range(n_rays):
        a0 = 2.0 * math.pi * i / n_rays
        a1 = 2.0 * math.pi * (i + 1) / n_rays
        mat = "item_candy_red" if (i % 2 == 0) else "item_candy_white"
        verts: List[Tuple[float, float, float]] = []
        tris: List[Tuple[int, int, int]] = []
        n_rad, n_ang = 5, 4
        for ir in range(n_rad + 1):
            rr = r_max * (ir / float(n_rad))
            # Match plump elliptical dome height `(rr/dome_r)^2 + (yy/dome_h)^2 = 1` + tiny 0.0022 proud offset!
            yy = dome_h * math.sqrt(max(0.0, 1.0 - (rr / dome_r) ** 2)) + 0.0022
            for ia in range(n_ang + 1):
                ang = a0 + (a1 - a0) * (ia / float(n_ang))
                verts.append((rr * math.cos(ang), math.copysign(yy, y_pole), rr * math.sin(ang)))
        st = n_ang + 1
        for ir in range(n_rad):
            for ia in range(n_ang):
                v0, v1, v2, v3 = ir * st + ia, (ir + 1) * st + ia, (ir + 1) * st + ia + 1, ir * st + ia + 1
                if y_pole > 0:
                    tris.extend(((v0, v2, v1), (v0, v3, v2)))
                else:
                    tris.extend(((v0, v1, v2), (v0, v2, v3)))
        pos_e = np.asarray(verts, dtype=np.float32)
        idx_e = np.asarray(tris, dtype=np.uint32).reshape(-1)
        ray = MeshPart(f"{name}_ray{i}", mat, pos_e, recompute_smooth_normals(pos_e, idx_e), idx_e)
        out.append(ray if (i % 2 == 0) else tint_part(ray, gold_tint))
    return out


def build_yoyo() -> SceneBuilder:
    """Plump glossy pearl-ivory imperial yo-yo with narrow string groove, twin crimson pin-stripes & sunburst emblem (`240:yoyo`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_yoyo")
    parts: List[MeshPart] = []

    # Generous almost-spherical classic imperial hemispheres (`half_thick = 0.226`, `max_r = 0.248` -> plump ~0.91 ratio!)
    # with a tight, crisp narrow center V-groove string channel (`gap ~ 0.016`) matching `sprite_0374.png`!
    dome_r, dome_h = 0.248, 0.226
    shell_prof: List[Tuple[float, float]] = []
    n_arc = 14
    for i in range(n_arc + 1):
        th = (math.pi * 0.445) * (i / float(n_arc))
        rr = dome_r * math.sin(th)
        yy = dome_h * math.cos(th)
        shell_prof.append((rr, yy))
    # Crisp rounded lip turning sharply into the tight narrow center string groove!
    shell_prof.extend([(0.246, 0.028), (0.236, 0.018), (0.195, 0.011), (0.048, 0.008)])

    ivory = _const_tint((0.86, 0.83, 0.78))
    top_half = tint_part(lathe(shell_prof, radial_segs=52, cap_bottom=True, cap_top=True, axis="y", name="yoyo_half_top", material="item_candy_white"), ivory)
    bot_prof = [(r, -y) for r, y in reversed(shell_prof)]
    bot_half = tint_part(lathe(bot_prof, radial_segs=52, cap_bottom=True, cap_top=True, axis="y", name="yoyo_half_bot", material="item_candy_white"), ivory)
    axle = cylinder(0.050, 0.050, 0.022, radial_segs=20, axis="y", name="yoyo_axle", material="item_wood_stick")
    # Crisp dark shadowed inner string groove channel right between the plump ivory halves (`sprite_0374.png`)!
    groove_slot = torus_arc(0.224, 0.0085, u_segs=52, v_segs=8, plane="xz", name="yoyo_groove", material="item_steel_dark")

    # Twin crisp thin crimson racing pin-stripes positioned closely beside the center string groove (`sprite_0374.png`)!
    stripe_y = 0.052
    stripe_r = dome_r * math.sqrt(max(0.0, 1.0 - (stripe_y / dome_h) ** 2)) + 0.0012
    ring_t = torus_arc(stripe_r, 0.0046, u_segs=52, v_segs=8, plane="xz", name="yoyo_stripe_top", material="item_candy_red").translate(0.0, +stripe_y, 0.0)
    ring_b = torus_arc(stripe_r, 0.0046, u_segs=52, v_segs=8, plane="xz", name="yoyo_stripe_bot", material="item_candy_red").translate(0.0, -stripe_y, 0.0)
    emblems = _yoyo_sunburst_emblem("yoyo_emblem_t", +1.0, dome_r=dome_r, dome_h=dome_h) + _yoyo_sunburst_emblem("yoyo_emblem_b", -1.0, dome_r=dome_r, dome_h=dome_h)
    parts.extend([top_half, bot_half, axle, groove_slot, ring_t, ring_b, *emblems])

    parts = [p.rotate_xyz(rx=40.0, ry=-20.0, rz=42.0, degrees=True).translate(-0.09, 0.22, -0.02) for p in parts]

    # Braided warm grey-white cotton string exiting smoothly from the narrow center groove into the upper-right arc,
    # lower-right figure-8 slip knot, and peeking neatly beneath the lower-left cheek (`sprite_0374.png`)!
    str_a = curved_tube(
        [
            (+0.03, 0.14, +0.04),
            (+0.16, 0.06, -0.14),
            (+0.30, 0.022, -0.14),
            (+0.37, 0.018, -0.01),
            (+0.34, 0.018, +0.13),
            (+0.22, 0.020, +0.18),
            (+0.16, 0.022, +0.09),
            (+0.27, 0.026, +0.04),
            (+0.39, 0.020, +0.11),
            (+0.37, 0.018, +0.23),
            (+0.24, 0.018, +0.28),
            (+0.11, 0.018, +0.21),
            (-0.08, 0.020, +0.19),
            (-0.24, 0.028, +0.13),
            (-0.31, 0.062, +0.02),
            (-0.26, 0.110, -0.04),
        ],
        [0.011] * 16,
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


def _slinky_flat_ribbon_arch(
    arch_rad: float = 0.272,
    coil_rad: float = 0.174,
    n_arch_turns: float = 61.5,
    n_foot_turns: float = 1.5,
    pts_per_turn: int = 18,
    ribbon_hw: float = 0.0125,
    ribbon_ht: float = 0.0018,
    material: str = "item_slinky_steel",
    name: str = "slinky_spring",
) -> MeshPart:
    """Build a continuous flat-wire steel Slinky standing upright in a 180-degree vertical half-circle arch.

    Both circular ends rest flush on Y = 0 and ease smoothly (C1) from flat stacked foot coils into
    the circular arch so the spring stands stably on its own with zero transition kinks.
    """
    n_total = n_arch_turns + 2.0 * n_foot_turns
    total_steps = int(round(n_total * pts_per_turn))
    t_ax = 2.0 * ribbon_ht
    dz_foot = 1.12 * t_ax
    th0 = -0.5 * math.pi

    def _smoothstep(x: float) -> float:
        x = max(0.0, min(1.0, x))
        return x * x * (3.0 - 2.0 * x)

    # Smooth C1 arch curvature weight w(u) in [0, 1]: 0 on flat grounded feet, smoothly easing to 1 in arch.
    u_arr = np.linspace(0.0, n_total, total_steps + 1, dtype=np.float64)
    ramp_turns = 5.0
    w_arr = np.zeros_like(u_arr)
    for k, u_t in enumerate(u_arr):
        if u_t <= n_foot_turns or u_t >= n_total - n_foot_turns:
            w_arr[k] = 0.0
        else:
            d_start = (u_t - n_foot_turns) / ramp_turns
            d_end = (n_total - n_foot_turns - u_t) / ramp_turns
            w_arr[k] = _smoothstep(min(d_start, d_end))

    # Integrate alpha smoothly from 0 to pi, and symmetric vertical foot-stack height y_base.
    du = n_total / float(total_steps)
    w_mid = 0.5 * (w_arr[:-1] + w_arr[1:])
    alpha_cum = np.concatenate([[0.0], np.cumsum(w_mid)])
    alphas = math.pi * (alpha_cum / max(float(alpha_cum[-1]), 1e-12))

    # On each half, stack vertically when w < 1, while keeping the outer 0.5 turn at each foot flat on Y = 0.
    y_bases = np.full(total_steps + 1, ribbon_ht, dtype=np.float64)
    half_k = total_steps // 2
    for k in range(1, half_k + 1):
        u_m = 0.5 * (u_arr[k - 1] + u_arr[k])
        if u_m <= 0.5:
            dy = 0.0
        elif u_m <= 1.0:
            dy = 2.0 * dz_foot * du
        else:
            dy = (1.0 - w_mid[k - 1]) * dz_foot * du
        y_bases[k] = y_bases[k - 1] + dy
    # Exact mirror symmetry for the second half so both feet finish identically flush on Y = 0.
    for k in range(half_k + 1, total_steps + 1):
        y_bases[k] = y_bases[total_steps - k]

    thetas = th0 + 2.0 * math.pi * u_arr
    ca, sa = np.cos(alphas), np.sin(alphas)
    ct, st = np.cos(thetas), np.sin(thetas)
    # Arch frame in vertical XY plane: N_a = (ca, sa, 0), T_a = (-sa, ca, 0), B_a = (0, 0, 1).
    rx, ry, rz = ca * ct, sa * ct, st
    centers = np.column_stack([arch_rad * ca + coil_rad * rx, y_bases + arch_rad * sa + coil_rad * ry, coil_rad * rz])
    rad_dirs = np.column_stack([rx, ry, rz])
    ax_dirs = np.column_stack([-sa, ca, np.zeros_like(ca)])

    # Compute smooth orthonormal ribbon frame (E_rad, E_ax_perp, T_curve) at every ring.
    tangents = np.zeros_like(centers)
    tangents[1:-1] = centers[2:] - centers[:-2]
    tangents[0] = centers[1] - centers[0]
    tangents[-1] = centers[-1] - centers[-2]
    tangents /= np.maximum(np.linalg.norm(tangents, axis=1, keepdims=True), 1e-12)
    e_rad = rad_dirs - np.sum(rad_dirs * tangents, axis=1, keepdims=True) * tangents
    e_rad /= np.maximum(np.linalg.norm(e_rad, axis=1, keepdims=True), 1e-12)
    e_ax = np.cross(tangents, e_rad)
    e_ax /= np.maximum(np.linalg.norm(e_ax, axis=1, keepdims=True), 1e-12)
    # Keep axial vector aligned with +T_a so winding stays strictly outward everywhere.
    flip = np.sum(e_ax * ax_dirs, axis=1, keepdims=True) < 0.0
    e_ax = np.where(flip, -e_ax, e_ax)

    # 8-vertex beveled flat spring-steel ribbon cross-section (counterclockwise in (e_rad, e_ax)).
    hw, ht = ribbon_hw, ribbon_ht
    b_r, b_a = 0.0014, 0.00050
    profile = np.array(
        [
            [+hw,       -ht + b_a, +0.97, -0.24],
            [+hw,       +ht - b_a, +0.97, +0.24],
            [+hw - b_r, +ht,       +0.24, +0.97],
            [-hw + b_r, +ht,       -0.24, +0.97],
            [-hw,       +ht - b_a, -0.97, +0.24],
            [-hw,       -ht + b_a, -0.97, -0.24],
            [-hw + b_r, -ht,       -0.24, -0.97],
            [+hw - b_r, -ht,       +0.24, -0.97],
        ],
        dtype=np.float64,
    )
    n_prof = len(profile)
    n_rings = total_steps + 1

    # Vectorized swept ribbon positions, analytic normals, and UVs.
    p_r = profile[None, :, 0:1]
    p_a = profile[None, :, 1:2]
    n_r = profile[None, :, 2:3]
    n_a = profile[None, :, 3:4]
    pos_grid = centers[:, None, :] + p_r * e_rad[:, None, :] + p_a * e_ax[:, None, :]
    nrm_grid = n_r * e_rad[:, None, :] + n_a * e_ax[:, None, :]
    nrm_grid /= np.maximum(np.linalg.norm(nrm_grid, axis=2, keepdims=True), 1e-12)

    u_ring = np.linspace(0.0, n_total, n_rings, dtype=np.float32)[:, None]
    v_prof = np.linspace(0.0, 1.0, n_prof, endpoint=False, dtype=np.float32)[None, :]
    uv_grid = np.stack([np.broadcast_to(u_ring, (n_rings, n_prof)), np.broadcast_to(v_prof, (n_rings, n_prof))], axis=2)

    # Subtle crevice ambient occlusion on tight inner arch belly & coil interior, gleaming rim outside.
    ct_ring = np.cos(thetas)[:, None]
    arch_mid = np.sin(alphas)[:, None]
    outer_bias = np.clip(0.5 + 0.5 * ct_ring, 0.0, 1.0)
    radial_side = np.clip(0.5 + 0.5 * (profile[None, :, 0] / hw), 0.0, 1.0)
    ao = 0.62 + 0.25 * outer_bias + 0.13 * radial_side - 0.15 * arch_mid * (1.0 - outer_bias)
    ao = np.clip(ao, 0.44, 1.00)
    # Warm-neutral dark pewter spring-steel tint matching `242:slinky` (offsets cool sky reflection).
    col_grid = ao[:, :, None] * np.array([[[1.00, 0.98, 0.93]]], dtype=np.float64)

    positions = pos_grid.reshape(-1, 3)
    normals = nrm_grid.reshape(-1, 3)
    uvs = uv_grid.reshape(-1, 2)
    colors = col_grid.reshape(-1, 3)

    # Side quad triangles with guaranteed outward winding: (a0, a1, b1), (a0, b1, b0).
    ri = np.arange(n_rings - 1, dtype=np.uint32)[:, None]
    pj = np.arange(n_prof, dtype=np.uint32)[None, :]
    pj_next = (pj + 1) % n_prof
    a0 = ri * n_prof + pj
    a1 = ri * n_prof + pj_next
    b0 = (ri + 1) * n_prof + pj
    b1 = (ri + 1) * n_prof + pj_next
    tris_side = np.stack([a0, a1, b1, a0, b1, b0], axis=2).reshape(-1, 3)

    # Watertight flat cut steel ends at both start (k=0) and end (k=n_rings-1).
    cap_pts = []
    cap_nrms = []
    cap_uvs = []
    cap_cols = []
    cap_tris = []
    base_v = len(positions)
    for is_end, ring_idx, sign in ((False, 0, -1.0), (True, n_rings - 1, +1.0)):
        c_pos = centers[ring_idx]
        c_nrm = tangents[ring_idx] * sign
        ring_p = pos_grid[ring_idx]
        c_idx = base_v
        cap_pts.append(c_pos)
        cap_nrms.append(c_nrm)
        cap_uvs.append((0.5, 0.5))
        cap_cols.append((0.90, 0.88, 0.84))
        for j in range(n_prof):
            cap_pts.append(ring_p[j])
            cap_nrms.append(c_nrm)
            cap_uvs.append((0.5, 0.5))
            cap_cols.append((0.90, 0.88, 0.84))
        for j in range(n_prof):
            v1 = c_idx + 1 + j
            v2 = c_idx + 1 + ((j + 1) % n_prof)
            if is_end:
                cap_tris.append((c_idx, v1, v2))
            else:
                cap_tris.append((c_idx, v2, v1))
        base_v += 1 + n_prof

    all_pos = np.vstack([positions, np.asarray(cap_pts, dtype=np.float64)]).astype(np.float32)
    all_nrm = np.vstack([normals, np.asarray(cap_nrms, dtype=np.float64)]).astype(np.float32)
    all_uv = np.vstack([uvs, np.asarray(cap_uvs, dtype=np.float64)]).astype(np.float32)
    all_col = np.vstack([colors, np.asarray(cap_cols, dtype=np.float64)]).astype(np.float32)
    all_idx = np.vstack([tris_side, np.asarray(cap_tris, dtype=np.uint32)]).reshape(-1).astype(np.uint32)
    return MeshPart(name=name, material=material, positions=all_pos, normals=all_nrm, indices=all_idx, uvs=all_uv, colors=all_col)


def build_slinky() -> SceneBuilder:
    """Classic gunmetal flat-ribbon spring-steel Slinky standing upright in a half-circle arch (`242:slinky`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_slinky")
    coil = _slinky_flat_ribbon_arch(
        arch_rad=0.272,
        coil_rad=0.174,
        n_arch_turns=61.5,
        n_foot_turns=1.5,
        pts_per_turn=18,
        ribbon_hw=0.0125,
        ribbon_ht=0.0018,
        material="item_slinky_steel",
        name="slinky_spring",
    )
    # Rotate vertical arch so front-left open circular foot faces lower-left and back-right foot rests upper-right (`242:slinky`).
    sc.add("slinky", coil.rotate_xyz(ry=28.0, degrees=True))
    return _finalize_item(sc, target_span=0.96)


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


def _build_perforated_leather_pouch(name: str) -> MeshPart:
    """Smooth curved dark-slate leather slingshot pouch with TWO REAL open punched-through eyelet holes (`350:sling`)."""
    # Build a high-res curved leather saddle plate (38x26 grid) and punch clean circular holes around both eyelets
    # at local `(hx_l=-0.115, hz=0.0)` and `(hx_r=+0.115, hz=0.0)`, stitching top, bottom, outer edges & inner hole walls!
    nx, nz = 40, 24
    hx, hz_h, thick = 0.165, 0.108, 0.011
    hole_r = 0.020
    holes = (-0.112, +0.112)

    def _pouch_y(x: float, z: float) -> float:
        # Authentic curved leather cup dipping gently in the middle and rising slightly at left/right strap ends!
        return 0.044 + 0.030 * ((x / hx) ** 2) - 0.014 * ((z / hz_h) ** 2)

    xs = np.linspace(-hx, +hx, nx + 1)
    zs = np.linspace(-hz_h, +hz_h, nz + 1)
    # Mark grid cells whose center falls outside BOTH punched circular eyelet holes
    solid = np.ones((nz, nx), dtype=bool)
    for iz in range(nz):
        cz = 0.5 * (zs[iz] + zs[iz + 1])
        for ix in range(nx):
            cx = 0.5 * (xs[ix] + xs[ix + 1])
            for h_cx in holes:
                if math.hypot(cx - h_cx, cz) < hole_r:
                    solid[iz, ix] = False

    # Snap grid vertices close to each hole boundary onto the exact smooth circle so holes are crisp & round!
    gx, gz = np.meshgrid(xs, zs)
    gx_c, gz_c = gx.copy(), gz.copy()
    for h_cx in holes:
        dx = gx_c - h_cx
        dz = gz_c
        dist = np.hypot(dx, dz)
        ring = (dist > hole_r * 0.65) & (dist < hole_r * 1.42)
        if np.any(ring):
            ang = np.arctan2(dz[ring], dx[ring])
            # Pull vertices right on the perimeter cleanly to hole_r
            near = (dist > hole_r * 0.80) & (dist < hole_r * 1.18)
            if np.any(near):
                ang_n = np.arctan2(dz[near], dx[near])
                gx_c[near] = h_cx + hole_r * np.cos(ang_n)
                gz_c[near] = hole_r * np.sin(ang_n)

    verts: List[Tuple[float, float, float]] = []
    tris: List[Tuple[int, int, int]] = []
    top_idx = np.full((nz + 1, nx + 1), -1, dtype=np.int32)
    bot_idx = np.full((nz + 1, nx + 1), -1, dtype=np.int32)

    def _get_v(iz: int, ix: int, is_top: bool) -> int:
        tbl = top_idx if is_top else bot_idx
        if tbl[iz, ix] < 0:
            x, z = float(gx_c[iz, ix]), float(gz_c[iz, ix])
            y0 = _pouch_y(x, z)
            tbl[iz, ix] = len(verts)
            verts.append((x, y0 + (thick * 0.5 if is_top else -thick * 0.5), z))
        return int(tbl[iz, ix])

    for iz in range(nz):
        for ix in range(nx):
            if not solid[iz, ix]:
                continue
            # Top & bottom faces
            t00, t10, t11, t01 = _get_v(iz, ix, True), _get_v(iz, ix + 1, True), _get_v(iz + 1, ix + 1, True), _get_v(iz + 1, ix, True)
            b00, b10, b11, b01 = _get_v(iz, ix, False), _get_v(iz, ix + 1, False), _get_v(iz + 1, ix + 1, False), _get_v(iz + 1, ix, False)
            tris.extend(((t00, t01, t11), (t00, t11, t10)))
            tris.extend(((b00, b11, b01), (b00, b10, b11)))
            # Side walls wherever a neighbor cell is empty (both outer perimeter AND inner punched eyelet walls!)
            if iz == 0 or not solid[iz - 1, ix]:
                tris.extend(((t00, t10, b10), (t00, b10, b00)))
            if iz == nz - 1 or not solid[iz + 1, ix]:
                tris.extend(((t11, t01, b01), (t11, b01, b11)))
            if ix == 0 or not solid[iz, ix - 1]:
                tris.extend(((t01, t00, b00), (t01, b00, b01)))
            if ix == nx - 1 or not solid[iz, ix + 1]:
                tris.extend(((t10, t11, b11), (t10, b11, b10)))

    pos_p = np.asarray(verts, dtype=np.float32)
    idx_p = np.asarray(tris, dtype=np.uint32).reshape(-1)
    pouch = MeshPart(name, "item_leather_smooth", pos_p, recompute_smooth_normals(pos_p, idx_p), idx_p)
    pouch = pouch.translate(-0.075, 0.0, -0.220)
    # Soft satin slate-charcoal leather sheen with realistic subtle highlight across the center ridge (`350:sling`)!
    return tint_part(
        pouch,
        lambda pos, nrm: np.clip(
            (0.72 + 0.34 * np.clip(nrm[:, 1], 0.0, 1.0) + 0.24 * np.exp(-(((pos[:, 0] + 0.025) / 0.055) ** 2)))[:, None]
            * np.array([[0.90, 0.93, 0.98]]),
            0.32,
            1.0,
        ),
    )


def build_sling() -> SceneBuilder:
    """Wooden Y-slingshot with rich natural wood branch fork & crimson rubber band genuinely threaded through leather pouch (`350:sling`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_sling")
    parts: List[MeshPart] = []

    # Rich warm walnut/chestnut carved natural wood branch fork with organic bark tint & seamless Y-crotch collar!
    def _wood_tint(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
        grain = 0.5 + 0.5 * np.sin((pos[:, 0] * 18.0 + pos[:, 2] * 26.0) + 1.8 * np.sin(pos[:, 1] * 45.0))
        top_hi = np.clip(nrm[:, 1] * 0.5 + 0.5, 0.0, 1.0)
        return np.clip(
            np.column_stack([
                0.78 + 0.18 * grain + 0.10 * top_hi,
                0.56 + 0.14 * grain + 0.08 * top_hi,
                0.38 + 0.10 * grain + 0.05 * top_hi,
            ]),
            0.36,
            1.0,
        )

    handle = tint_part(
        _sculpt_bark_branch(
            [(+0.275, 0.026, +0.345), (+0.225, 0.028, +0.260), (+0.165, 0.031, +0.165), (+0.112, 0.034, +0.082)],
            [0.028, 0.031, 0.034, 0.035],
            name="sling_handle",
            material="item_wood_stick",
            radial_segs=22,
            samples_per_seg=5,
            seed=2.4,
            cap_start=True,
            cap_end=True,
        ),
        _wood_tint,
    )
    fork_l = tint_part(
        _sculpt_bark_branch(
            [(+0.112, 0.034, +0.082), (+0.060, 0.035, +0.095), (-0.005, 0.037, +0.086), (-0.062, 0.038, +0.062)],
            [0.031, 0.027, 0.025, 0.023],
            name="sling_fork_l",
            material="item_wood_stick",
            radial_segs=20,
            samples_per_seg=5,
            seed=4.1,
            cap_start=True,
            cap_end=True,
        ),
        _wood_tint,
    )
    fork_r = tint_part(
        _sculpt_bark_branch(
            [(+0.112, 0.034, +0.082), (+0.130, 0.035, +0.020), (+0.144, 0.037, -0.048), (+0.150, 0.038, -0.120)],
            [0.031, 0.027, 0.025, 0.023],
            name="sling_fork_r",
            material="item_wood_stick",
            radial_segs=20,
            samples_per_seg=5,
            seed=5.7,
            cap_start=True,
            cap_end=True,
        ),
        _wood_tint,
    )
    # Smooth wooden crotch knot fairing so the Y-branch union reads like one solid naturally forked stick!
    crotch = tint_part(
        ellipsoid(0.042, 0.035, 0.046, u_segs=18, v_segs=12, name="sling_crotch", material="item_wood_stick").rotate_xyz(ry=-0.45).translate(+0.112, 0.034, +0.082),
        _wood_tint,
    )
    # Little broken twig nub jutting cleanly off the upper-right fork just like `350:sling`!
    nub = tint_part(
        _sculpt_bark_branch(
            [(+0.142, 0.036, -0.025), (+0.165, 0.039, -0.042), (+0.188, 0.042, -0.058)],
            [0.0115, 0.0080, 0.0045],
            name="sling_nub",
            material="item_wood_stick",
            radial_segs=12,
            samples_per_seg=3,
            seed=7.2,
            cap_start=True,
            cap_end=True,
        ),
        _wood_tint,
    )
    parts.extend([handle, fork_l, fork_r, crotch, nub])

    wrap_l = cylinder(0.031, 0.031, 0.046, radial_segs=20, bevel=0.006, axis="x", name="sling_wrap_l", material="item_candy_red").rotate_xyz(ry=-0.34).translate(-0.056, 0.038, +0.062)
    wrap_r = cylinder(0.031, 0.031, 0.046, radial_segs=20, bevel=0.006, axis="z", name="sling_wrap_r", material="item_candy_red").translate(+0.149, 0.038, -0.114)
    parts.extend([wrap_l, wrap_r])

    pouch = _build_perforated_leather_pouch("sling_pouch")

    # Crimson rubber band GENUINELY THREADED THROUGH BOTH EYELET HOLES of the leather pouch (`350:sling`):
    # Left fork -> passes UNDER left edge of pouch -> threads UP THROUGH left hole (-0.187, -0.220) ->
    # waves gracefully across the TOP/FRONT face of the leather -> dives DOWN THROUGH right hole (+0.037, -0.220) ->
    # exits UNDER right edge -> loops cleanly into right fork!
    band_full = curved_tube(
        [
            (-0.062, 0.038, +0.062),
            (-0.185, 0.024, +0.105),
            (-0.315, 0.020, +0.045),
            (-0.355, 0.022, -0.085),
            (-0.290, 0.024, -0.195),
            (-0.232, 0.030, -0.220),   # passing underneath left leather flap
            (-0.187, 0.058, -0.220),   # threading UP straight through the open left eyelet hole!
            (-0.142, 0.072, -0.232),   # resting cleanly across front/top of leather pouch
            (-0.075, 0.060, -0.200),   # graceful S-wave across center of pouch (`350:sling`)
            (-0.012, 0.070, -0.226),
            (+0.037, 0.058, -0.220),   # diving DOWN straight through the open right eyelet hole!
            (+0.088, 0.032, -0.224),   # passing underneath right leather flap
            (+0.165, 0.034, -0.235),
            (+0.205, 0.036, -0.185),
            (+0.150, 0.038, -0.116),
        ],
        [0.0115] * 15,
        samples_per_seg=6,
        radial_segs=14,
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
    """Smooth mirror-polished metallic 6-pronged game jack (4 sleek ball-tipped arms + 2 tapered prongs) (`336..339`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    pewter = _const_tint((0.94, 0.91, 0.87))
    parts: List[MeshPart] = [tint_part(sphere(0.038, u_segs=22, v_segs=14, name=f"{name}_hub", material="item_chrome_polished"), pewter)]
    # X and Z axes: 4 smoothly lathed tapered cast-metal arms flowing seamlessly into gleaming spherical tips!
    arm_prof = [(0.000, -0.285), (0.026, -0.280), (0.036, -0.262), (0.034, -0.242), (0.018, -0.228), (0.014, -0.140), (0.021, -0.025), (0.024, 0.000), (0.021, +0.025), (0.014, +0.140), (0.018, +0.228), (0.034, +0.242), (0.036, +0.262), (0.026, +0.280), (0.000, +0.285)]
    for axis, tag in (("x", "x"), ("z", "z")):
        parts.append(tint_part(lathe(arm_prof, radial_segs=24, cap_bottom=True, cap_top=True, axis=axis, name=f"{name}_bar_{tag}", material="item_chrome_polished"), pewter))
    # Y axis: 2 classic smooth conical tapered metal prongs!
    prong_prof = [(0.000, -0.275), (0.004, -0.268), (0.011, -0.175), (0.022, -0.045), (0.026, 0.000), (0.022, +0.045), (0.011, +0.175), (0.004, +0.268), (0.000, +0.275)]
    parts.append(tint_part(lathe(prong_prof, radial_segs=24, cap_bottom=True, cap_top=True, axis="y", name=f"{name}_prongs", material="item_chrome_polished"), pewter))

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
