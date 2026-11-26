"""Realistic 3D sculptors for 1998 Ants Food Wave 4B: Savory Entrees & Parody Condiments (19 items).

Covers:
- `pizzatube` (#389) + `fdpizz1`..`fdpizz4` (#390–#393): 'PIZZA! IN A TUBE' squeeze tube + coiled paste stages
- `steakwhiz` (#377) + `fdsteak1`..`fdsteak4` (#378–#381): 'STEAK WHIZ' aerosol can + star-tip squeezed meat paste
- `fdeggs1`..`fdeggs4` (#385–#388): Twin sunny-side-up fried eggs from crisp white albumen down to bare twin yolks
- `fdfish1`..`fdfish5` (#394–#398): 'CHOCO-FISHY SNAX' chocolate-dipped scaly green candy fish + wrapper stages
"""
from __future__ import annotations

import math
from typing import List, Sequence, Tuple

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
        from .items_toys import _finalize_item
    except ImportError:
        from map_items.item_materials import gradient_y_tint, register_item_materials, tint_part  # type: ignore
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


def _catmull_rom_samples(
    pts: Sequence[Tuple[float, float, float]],
    radii: Sequence[float],
    samples_per_seg: int = 8,
) -> Tuple[np.ndarray, np.ndarray]:
    """Smooth Catmull-Rom interpolation of centerline points and radii."""
    p = np.asarray(pts, dtype=np.float64)
    r = np.asarray(radii, dtype=np.float64)
    p_ext = np.vstack([p[0:1], p, p[-1:]])
    r_ext = np.concatenate([r[0:1], r, r[-1:]])
    out_p: List[np.ndarray] = []
    out_r: List[float] = []
    n_seg = len(p) - 1
    for seg in range(n_seg):
        p0, p1, p2, p3 = p_ext[seg], p_ext[seg + 1], p_ext[seg + 2], p_ext[seg + 3]
        r0, r1, r2, r3 = r_ext[seg], r_ext[seg + 1], r_ext[seg + 2], r_ext[seg + 3]
        steps = samples_per_seg + (1 if seg == n_seg - 1 else 0)
        for s in range(steps):
            t = s / float(samples_per_seg)
            t2, t3 = t * t, t * t * t
            cp = 0.5 * (
                (2.0 * p1)
                + (-p0 + p2) * t
                + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2
                + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3
            )
            cr = 0.5 * (
                (2.0 * r1)
                + (-r0 + r2) * t
                + (2.0 * r0 - 5.0 * r1 + 4.0 * r2 - r3) * t2
                + (-r0 + 3.0 * r1 - 3.0 * r2 + r3) * t3
            )
            out_p.append(cp)
            out_r.append(max(0.004, float(cr)))
    return np.asarray(out_p, dtype=np.float64), np.asarray(out_r, dtype=np.float64)


def extrude_star_piped_ribbon(
    pts: Sequence[Tuple[float, float, float]],
    radii: Sequence[float],
    *,
    n_flutes: int = 10,
    flute_depth: float = 0.46,
    width_ratio: float = 1.62,
    height_ratio: float = 0.58,
    samples_per_seg: int = 8,
    radial_segs: int = 56,
    start_pinch: bool = True,
    end_pinch: bool = True,
    jagged_start: bool = False,
    jagged_end: bool = False,
    valley_rgb: Tuple[float, float, float] = (0.40, 0.27, 0.19),
    mid_rgb: Tuple[float, float, float] = (0.82, 0.72, 0.62),
    crest_rgb: Tuple[float, float, float] = (1.00, 0.96, 0.90),
    y_floor: float = 0.002,
    name: str,
    material: str,
) -> MeshPart:
    """Extrude a true wide flat serrated star-nozzle squeezed paste ribbon with sharp V-grooves & groove AO.

    Uses a world-up parallel frame so the ribbon stays flat and upright (zero hose twist!) with 5 crisp
    parallel upper ridges separated by deep dark V-grooves matching 1998 Steak Whiz & cracker Cheese Whiz!
    """
    centers, rads = _catmull_rom_samples(pts, radii, samples_per_seg=samples_per_seg)
    n_c = len(centers)
    up_w = np.array([0.0, 1.0, 0.0], dtype=np.float64)
    v_col = np.asarray(valley_rgb, dtype=np.float64)
    m_col = np.asarray(mid_rgb, dtype=np.float64)
    c_col = np.asarray(crest_rgb, dtype=np.float64)

    # Compute smooth arc-length parameter s in [0, 1] for crisp longitudinal UV & nozzle ripple
    seg_lens = np.linalg.norm(np.diff(centers, axis=0), axis=1)
    arc = np.concatenate([[0.0], np.cumsum(seg_lens)])
    total_len = max(1e-5, float(arc[-1]))
    s_arr = arc / total_len

    # Smooth horizontal right/up frame along centerline (never flips on bends)
    tangents = np.zeros_like(centers)
    for i in range(n_c):
        t_v = centers[min(n_c - 1, i + 1)] - centers[max(0, i - 1)]
        ln = float(np.linalg.norm(t_v))
        tangents[i] = t_v / ln if ln > 1e-7 else np.array([1.0, 0.0, 0.0])

    verts: List[Tuple[float, float, float]] = []
    uvs: List[Tuple[float, float]] = []
    cols: List[Tuple[float, float, float]] = []
    tris: List[Tuple[int, int, int]] = []

    stride = radial_segs + 1
    for i in range(n_c):
        sf = float(s_arr[i])
        t_v = tangents[i]
        r_v = np.cross(t_v, up_w)
        r_ln = float(np.linalg.norm(r_v))
        if r_ln < 1e-5:
            r_v = np.array([1.0, 0.0, 0.0], dtype=np.float64)
        else:
            r_v /= r_ln
        u_v = np.cross(r_v, t_v)
        u_v /= max(1e-7, float(np.linalg.norm(u_v)))

        # Taper start/end smoothly into authentic blunt squeezed nozzle cuts (no sharp dark needle tips!)
        taper = 1.0
        if start_pinch and sf < 0.075:
            taper *= 0.56 + 0.44 * (math.sin(0.5 * math.pi * (sf / 0.075)) ** 0.70)
        if end_pinch and sf > 0.925:
            taper *= 0.56 + 0.44 * (math.sin(0.5 * math.pi * ((1.0 - sf) / 0.075)) ** 0.70)
        # Subtle organic hand-squeezed flow undulation
        flow = 1.0 + 0.026 * math.sin(sf * 26.0 + 0.4)
        rad = float(rads[i]) * taper * flow
        hw = rad * width_ratio
        hh = rad * height_ratio

        for j in range(stride):
            uf = j / float(radial_segs)
            th = uf * 2.0 * math.pi
            cs, sn = math.cos(th), math.sin(th)
            # Align a razor ridge right along the crown (th = pi/2) + 2 flanking ridges on each upper side!
            phase = (th - 0.5 * math.pi) * (n_flutes / (2.0 * math.pi))
            frac = phase - math.floor(phase)
            tri = abs(frac * 2.0 - 1.0)
            fin = tri ** 1.48  # razor sharp fin peaks + deep narrow V-grooves!
            # Full sharp flutes across top & flanks; flatter pressed belly underneath
            top_w = max(0.0, min(1.0, (sn + 0.22) / 0.48))
            top_w = top_w * top_w * (3.0 - 2.0 * top_w)

            flute_scale = 1.0 - flute_depth * (1.0 - fin) * (0.24 + 0.76 * top_w)
            lx = cs * hw * flute_scale
            ly_base = sn * hh * (0.58 if sn < 0.0 else 1.0)
            # Carve real vertical V-groove channels downward into top arch so overhead camera sees crisp 3D fins!
            ly = ly_base * flute_scale - (flute_depth * 0.30 * hh * (1.0 - fin) * top_w)

            # Slight jagged bite edge if ribbon ends on a chewed bite
            bite_shift = 0.0
            if jagged_start and i <= 2:
                bite_shift = -0.020 * (1.0 - i / 2.5) * (0.5 + 0.5 * math.sin(th * 5.0 + 0.8))
            elif jagged_end and i >= n_c - 3:
                bite_shift = +0.020 * (1.0 - (n_c - 1 - i) / 2.5) * (0.5 + 0.5 * math.cos(th * 5.0 - 0.4))

            pt = centers[i] + r_v * lx + u_v * ly + t_v * bite_shift
            # Keep solid non-degenerate rounded belly cleanly above ground (never squash floor triangles coplanar!)
            pt_y = max(y_floor, float(pt[1]))
            verts.append((float(pt[0]), pt_y, float(pt[2])))
            uvs.append((uf * 2.0, sf * 4.0))

            # Crisp vertex-painted groove ambient occlusion -> warm mid flank -> creamy bright fin crest!
            if fin < 0.52:
                f_t = fin / 0.52
                rgb = v_col * (1.0 - f_t) + m_col * f_t
            else:
                f_t = (fin - 0.52) / 0.48
                rgb = m_col * (1.0 - f_t) + c_col * f_t
            # Grounded underside shadow + subtle hand-piped micro-variation
            under = 0.56 + 0.44 * max(0.0, min(1.0, (sn + 0.55) / 0.95))
            var = 1.0 + 0.028 * math.sin(sf * 34.0 + th * 3.0)
            rgb = np.clip(rgb * under * var, 0.08, 1.0)
            cols.append((float(rgb[0]), float(rgb[1]), float(rgb[2])))

    # Outward CCW tube quads: (a, b, c), (a, c, d) verified right-handed!
    for i in range(n_c - 1):
        for j in range(radial_segs):
            a = i * stride + j
            b = (i + 1) * stride + j
            c = (i + 1) * stride + (j + 1)
            d = i * stride + (j + 1)
            tris.extend(((a, b, c), (a, c, d)))

    # Crisp OUTWARD-FACING solid closed end-caps (fixed winding so sides/ends are 100% solid & watertight from EVERY angle!)
    c0_idx = len(verts)
    t0 = tangents[0]
    p0_tip = centers[0] - t0 * (0.010 if start_pinch else 0.005)
    verts.append((float(p0_tip[0]), max(y_floor + 0.004, float(p0_tip[1])), float(p0_tip[2])))
    uvs.append((0.5, 0.0))
    cap_c = m_col * 0.55 + c_col * 0.45
    cols.append((float(cap_c[0]), float(cap_c[1]), float(cap_c[2])))
    for j in range(radial_segs):
        tris.append((c0_idx, j, j + 1))

    c1_idx = len(verts)
    t1 = tangents[-1]
    p1_tip = centers[-1] + t1 * (0.010 if end_pinch else 0.005)
    verts.append((float(p1_tip[0]), max(y_floor + 0.004, float(p1_tip[1])), float(p1_tip[2])))
    uvs.append((0.5, 4.0))
    cols.append((float(cap_c[0]), float(cap_c[1]), float(cap_c[2])))
    last_off = (n_c - 1) * stride
    for j in range(radial_segs):
        tris.append((c1_idx, last_off + j + 1, last_off + j))

    pos_a = np.asarray(verts, dtype=np.float32)
    idx_a = np.asarray(tris, dtype=np.uint32).reshape(-1)
    nrm_a = recompute_smooth_normals(pos_a, idx_a)
    col_a = np.asarray(cols, dtype=np.float32)
    return MeshPart(name, material, pos_a, nrm_a, idx_a, uvs=np.asarray(uvs, dtype=np.float32), colors=col_a)


def _torn_scrap(
    name: str,
    cx: float,
    cz: float,
    rx: float,
    rz: float,
    height: float,
    *,
    seed: float,
    material: str,
    rgb_lo: Tuple[float, float, float],
    rgb_hi: Tuple[float, float, float],
    rot_y: float = 0.0,
) -> MeshPart:
    """Sculpt a flat irregular asymmetric food scrap/crumb chunk (natural torn blob, zero starfish spikes)."""
    mesh = ellipsoid(rx, height, rz, u_segs=16, v_segs=8, name=name, material=material)
    p = mesh.positions.astype(np.float64)
    ang = np.arctan2(p[:, 2], p[:, 0])
    # Natural asymmetric torn chunk outline (gentle 2- & 3-lobe asymmetry + subtle edge roughness, never pointy star!)
    jag = (
        1.0
        + 0.14 * np.sin(ang * 2.0 + seed * 1.7)
        + 0.10 * np.cos(ang * 3.0 - seed * 2.3)
        - 0.08 * np.maximum(0.0, np.sin(ang * 4.0 + seed * 3.1))
    )
    p[:, 0] *= jag
    p[:, 2] *= jag
    # Flatten underside solidly onto the ground and give gentle organic thickness variation on top
    p[:, 1] = np.where(p[:, 1] < 0.0, p[:, 1] * 0.22, p[:, 1] * (0.88 + 0.16 * np.sin(ang * 2.0 + seed)))
    p[:, 1] += height * 0.26
    mesh.positions = p.astype(np.float32)
    mesh.normals = recompute_smooth_normals(mesh.positions, mesh.indices)
    mesh = gradient_y_tint(mesh, rgb_lo, rgb_hi, noise_amp=0.05)
    return mesh.rotate_xyz(ry=rot_y, degrees=True).translate(cx, 0.002, cz)


# ---------------------------------------------------------------------------
# 1. PIZZA! IN A TUBE (`389:pizzatube`) & Squeezed Tube-Pizza (`390..393:fdpizz1..4`)
# ---------------------------------------------------------------------------

def build_pizzatube() -> SceneBuilder:
    """Squeezed white & royal-blue 'PIZZA! IN A TUBE' toothpaste tube with unscrewed purple cap (`389:pizzatube`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_pizzatube")
    parts: List[MeshPart] = []

    # Build squeezed toothpaste tube along local X (-0.34 crimped tail -> +0.24 shoulder)
    n_x, n_th = 28, 36
    x0, x1 = -0.34, +0.23
    verts, uvs, tris = [], [], []
    for ix in range(n_x + 1):
        t = ix / float(n_x)
        x = x0 + (x1 - x0) * t
        # Cross-section morphs smoothly: flat pinched crimp at t=0 -> plump squeezed oval -> round collar at t=1!
        if t < 0.14:
            # Flat crimped metal/plastic tail
            flat = t / 0.14
            rx_z = 0.158 - 0.006 * flat
            ry_h = 0.016 + 0.022 * (flat ** 1.6)
            y_lift = 0.036 + 0.024 * (1.0 - flat)  # crimped tail curls slightly upward
        elif t < 0.88:
            mid = (t - 0.14) / 0.74
            rx_z = 0.152 - 0.034 * (mid ** 1.3)
            # Plump inflated belly with realistic finger-squeeze indentation around mid=0.45
            squeeze = 1.0 - 0.18 * math.exp(-(((mid - 0.42) / 0.24) ** 2))
            ry_h = (0.038 + 0.068 * math.sin(mid * math.pi * 0.85 + 0.15)) * squeeze
            y_lift = ry_h + 0.004
        else:
            sh = (t - 0.88) / 0.12
            rx_z = 0.118 * (1.0 - sh) + 0.044 * sh
            ry_h = 0.072 * (1.0 - sh) + 0.044 * sh
            y_lift = 0.076 * (1.0 - sh) + 0.066 * sh
        crimp_rib = 0.0045 * math.sin(t * 72.0) if t < 0.11 else 0.0
        for j in range(n_th + 1):
            uf_c = j / float(n_th)
            # Angle 0 at -Z (back edge), pi/2 at +Y (top), pi at +Z (front edge), 3pi/2 at -Y (bottom)
            ang = uf_c * 2.0 * math.pi
            pz = -rx_z * math.cos(ang)
            py = y_lift + (ry_h + crimp_rib) * math.sin(ang)
            verts.append((x, py, pz))
            # Map top face cleanly: u along tube length (0..1), v from -Z (0) to +Z (1)
            tex_u = max(0.0, min(1.0, (t - 0.10) / 0.78))
            tex_v = 0.5 + 0.5 * (pz / max(1e-4, rx_z))
            uvs.append((tex_u, tex_v))
    stride = n_th + 1
    for ix in range(n_x):
        for j in range(n_th):
            a, b, c, d = ix * stride + j, (ix + 1) * stride + j, (ix + 1) * stride + (j + 1), ix * stride + (j + 1)
            tris.extend(((a, b, c), (a, c, d)))
    pos_t = np.asarray(verts, dtype=np.float32)
    idx_t = np.asarray(tris, dtype=np.uint32).reshape(-1)
    tube_body = MeshPart("pizzatube_body", "item_pizza_tube", pos_t, recompute_smooth_normals(pos_t, idx_t), idx_t, uvs=np.asarray(uvs, dtype=np.float32))
    parts.append(tube_body)

    # Rear blue crimped seam seal & rolled crimp cap
    crimp_bar = rounded_box(0.048, 0.022, 0.312, bevel=0.006, segs=2, name="pizzatube_crimp", material="item_plastic_blue").translate(-0.336, 0.058, 0.0)
    parts.append(tint_part(crimp_bar, _const_tint((0.18, 0.28, 0.84))))

    # Conical white plastic threaded nozzle on the right (+X) + subtle dark paste opening
    nozzle_prof = [(0.046, 0.0), (0.042, 0.024), (0.036, 0.052), (0.030, 0.088), (0.024, 0.122), (0.016, 0.128), (0.012, 0.112)]
    nozzle = lathe(nozzle_prof, radial_segs=24, cap_bottom=True, cap_top=True, axis="x", name="pizzatube_nozzle", material="item_candy_white").translate(+0.224, 0.066, 0.0)
    parts.append(nozzle)
    for k in range(3):
        parts.append(
            torus_arc(0.035 - k * 0.003, 0.0045, u_segs=20, v_segs=6, plane="yz", name=f"pizzatube_thread_{k}", material="item_candy_white").translate(+0.252 + k * 0.024, 0.066, 0.0)
        )
    paste_tip = sphere(0.013, u_segs=12, v_segs=8, name="pizzatube_paste_tip", material="item_pizza_sauce").translate(+0.352, 0.066, 0.0)
    parts.append(paste_tip)

    # Unscrewed ribbed purple plastic cap resting just upper-right of the nozzle matching `389:pizzatube`!
    cap = cylinder(0.046, 0.041, 0.088, radial_segs=24, bevel=0.007, axis="x", name="pizzatube_cap", material="item_glasses_purple")
    p_cap = cap.positions.astype(np.float64)
    ang_c = np.arctan2(p_cap[:, 2], p_cap[:, 1])
    rad_c = np.hypot(p_cap[:, 1], p_cap[:, 2])
    rib = 1.0 + 0.06 * np.cos(ang_c * 14.0) * np.clip(rad_c / 0.04, 0.0, 1.0)
    p_cap[:, 1] *= rib
    p_cap[:, 2] *= rib
    cap.positions = p_cap.astype(np.float32)
    cap.normals = recompute_smooth_normals(cap.positions, cap.indices)
    cap = cap.rotate_xyz(ry=52.0, rz=14.0, degrees=True).translate(+0.415, 0.046, -0.055)
    parts.append(cap)

    # Angle upper-left (-X, -Z) to lower-right (+X, +Z) exactly matching `389:pizzatube`!
    sc.add("pizzatube", [p.rotate_xyz(ry=-28.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.92)


def _pepperoni_disc(
    name: str,
    cx: float,
    cy: float,
    cz: float,
    radius: float = 0.068,
    tilt_rx: float = 0.0,
    tilt_rz: float = 0.0,
    *,
    bitten: bool = False,
) -> MeshPart:
    """Seared cupped pepperoni slice with dark crispy curled rim, deep ruby meat & pale fat speckles."""
    disc = cylinder(radius, radius * 0.96, 0.022, radial_segs=24, bevel=0.006, axis="y", name=name, material="item_pizza_sauce")
    p = disc.positions.astype(np.float64)
    r_frac = np.hypot(p[:, 0], p[:, 2]) / max(1e-4, radius)
    # Subtle authentic oven-cupping: edges curl slightly up while center dips gently
    p[:, 1] += 0.007 * (r_frac ** 2.4) * np.where(p[:, 1] > 0.0, 1.0, 0.4)
    if bitten:
        # Jagged half-bitten pepperoni wedge for `fdpizz4`!
        bite_mask = p[:, 0] > radius * 0.15
        p[bite_mask, 0] = radius * 0.15 + 0.008 * np.sin(p[bite_mask, 2] * 52.0)
    disc.positions = p.astype(np.float32)
    disc.normals = recompute_smooth_normals(disc.positions, disc.indices)
    disc = tint_part(
        disc,
        lambda pos, _n: np.clip(
            np.column_stack([
                # Crispy dark charred outer rim -> rich garnet-ruby center + creamy white-pink fat flecks!
                (0.92 - 0.36 * np.clip((np.hypot(pos[:, 0], pos[:, 2]) / max(1e-4, radius) - 0.68) / 0.32, 0.0, 1.0))
                + 0.14 * np.clip(np.sin(pos[:, 0] * 115.0 + pos[:, 2] * 95.0) * np.cos(pos[:, 2] * 85.0 - pos[:, 0] * 70.0), 0.0, 1.0) ** 3,
                (0.14 - 0.07 * np.clip((np.hypot(pos[:, 0], pos[:, 2]) / max(1e-4, radius) - 0.68) / 0.32, 0.0, 1.0))
                + 0.42 * np.clip(np.sin(pos[:, 0] * 115.0 + pos[:, 2] * 95.0) * np.cos(pos[:, 2] * 85.0 - pos[:, 0] * 70.0), 0.0, 1.0) ** 3,
                (0.10 - 0.05 * np.clip((np.hypot(pos[:, 0], pos[:, 2]) / max(1e-4, radius) - 0.68) / 0.32, 0.0, 1.0))
                + 0.32 * np.clip(np.sin(pos[:, 0] * 115.0 + pos[:, 2] * 95.0) * np.cos(pos[:, 2] * 85.0 - pos[:, 0] * 70.0), 0.0, 1.0) ** 3,
            ]),
            0.05,
            1.0,
        ),
    )
    return disc.rotate_xyz(rx=tilt_rx, rz=tilt_rz, degrees=True).translate(cx, cy, cz)


def _pizza_dough_rim(name: str, pts: Sequence[Tuple[float, float, float]], rads: Sequence[float], *, jagged: bool = False) -> MeshPart:
    """Flat, broad creamy-pale extruded dough rim coil (or scalloped ant-bitten crust on stage 2)."""
    rim = curved_tube(list(pts), list(rads), samples_per_seg=6, radial_segs=18, cap_rings=3, name=name, material="item_bun_crumb")
    p = rim.positions.astype(np.float64)
    # Flatten into an authentic squeezed ribbon rim rather than a round hose!
    p[:, 1] = 0.004 + (p[:, 1] - 0.004) * 0.56
    if jagged:
        # Concave inward ant-bite scallops along outer rim (never outward cartoon spikes!)
        ang = np.arctan2(p[:, 2], p[:, 0])
        rad_xz = np.hypot(p[:, 0], p[:, 2])
        outer = np.clip((rad_xz - 0.21) / 0.14, 0.0, 1.0)
        bite = np.zeros_like(ang)
        for ba, bw, bd in ((-1.9, 0.36, 0.044), (-0.65, 0.34, 0.048), (+0.45, 0.38, 0.046), (+1.65, 0.35, 0.042), (+2.55, 0.32, 0.040)):
            da = np.arctan2(np.sin(ang - ba), np.cos(ang - ba))
            bite += bd * np.exp(-((da / bw) ** 2))
        p[:, 0] -= np.cos(ang) * bite * outer
        p[:, 2] -= np.sin(ang) * bite * outer
    rim.positions = p.astype(np.float32)
    rim.normals = recompute_smooth_normals(rim.positions, rim.indices)
    return gradient_y_tint(rim, (0.76, 0.68, 0.48), (0.98, 0.94, 0.78), noise_amp=0.04)


def _pizza_sauce_bed(name: str, rx: float, rz: float, cx: float, cz: float, *, jagged: bool = False) -> MeshPart:
    """Thick glossy crimson tomato sauce pool with organic oregano/herb speckles & wavy edge."""
    sauce = ellipsoid(rx, 0.042, rz, u_segs=30, v_segs=14, name=name, material="item_pizza_sauce").translate(cx, 0.036, cz)
    ps = sauce.positions.astype(np.float64)
    ang = np.arctan2(ps[:, 2] - cz, ps[:, 0] - cx)
    wave = 1.0 + 0.07 * np.sin(ang * 3.0 + 0.6) + 0.04 * np.cos(ang * 5.0 - 0.4)
    if jagged:
        wave += 0.10 * np.sin(ang * 9.0 + 0.3) * np.clip((ps[:, 0] - cx) / max(0.05, rx), 0.0, 1.0)
    ps[:, 0] = cx + (ps[:, 0] - cx) * wave
    ps[:, 2] = cz + (ps[:, 2] - cz) * wave
    ps[:, 1] = np.maximum(ps[:, 1], 0.005)
    sauce.positions = ps.astype(np.float32)
    sauce.normals = recompute_smooth_normals(sauce.positions, sauce.indices)
    # Rich crimson tomato base with subtle dark basil/oregano flecks & darker underside edge!
    return tint_part(
        sauce,
        lambda pos, nrm: np.clip(
            np.column_stack([
                0.86 * (0.68 + 0.32 * np.clip(nrm[:, 1], 0.0, 1.0)) - 0.24 * (np.sin(pos[:, 0] * 120.0) * np.cos(pos[:, 2] * 110.0) > 0.84),
                0.15 * (0.65 + 0.35 * np.clip(nrm[:, 1], 0.0, 1.0)) - 0.04 * (np.sin(pos[:, 0] * 120.0) * np.cos(pos[:, 2] * 110.0) > 0.84),
                0.11 * (0.65 + 0.35 * np.clip(nrm[:, 1], 0.0, 1.0)),
            ]),
            0.05,
            1.0,
        ),
    )


def _build_squeezed_pizza(name: str, stage: int) -> SceneBuilder:
    """Coiled squeezed tube-pizza paste pile with flat dough rim, glossy sauce, swirled cheese & seared pepperoni (`390..393`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    if stage == 1:
        # Broad flat creamy coiled dough rim wrapping lower-left -> bottom -> right -> upper-right (`390:fdpizz1`)
        crust_pts = [
            (-0.27, 0.044, -0.05),
            (-0.29, 0.042, +0.11),
            (-0.18, 0.044, +0.26),
            (+0.02, 0.046, +0.30),
            (+0.22, 0.048, +0.21),
            (+0.29, 0.046, +0.02),
            (+0.21, 0.046, -0.18),
            (+0.03, 0.048, -0.25),
            (-0.13, 0.056, -0.21),
        ]
        parts.append(_pizza_dough_rim(f"{name}_crust", crust_pts, [0.056, 0.068, 0.072, 0.074, 0.072, 0.068, 0.064, 0.056, 0.044]))
        parts.append(_pizza_sauce_bed(f"{name}_sauce", 0.255, 0.248, +0.02, +0.02))

        # Melted golden-yellow cheese pool swirling into an iconic upper-left soft-serve squeeze peak!
        cheese = ellipsoid(0.195, 0.054, 0.205, u_segs=28, v_segs=14, name=f"{name}_cheese", material="item_cheese").translate(-0.04, 0.062, +0.01)
        pc = cheese.positions.astype(np.float64)
        angc = np.arctan2(pc[:, 2], pc[:, 0])
        pc[:, 0] *= 1.0 + 0.16 * np.sin(angc * 3.0 - 0.5)
        pc[:, 2] *= 1.0 + 0.10 * np.cos(angc * 4.0 + 0.4)
        pc[:, 1] += 0.022 * np.exp(-(((pc[:, 0] + 0.12) ** 2 + (pc[:, 2] + 0.12) ** 2) / 0.020))
        cheese.positions = pc.astype(np.float32)
        cheese.normals = recompute_smooth_normals(cheese.positions, cheese.indices)
        parts.append(gradient_y_tint(cheese, (0.90, 0.68, 0.14), (1.00, 0.96, 0.42), noise_amp=0.04))

        # Swirled upper-left soft-serve nozzle squeeze peak pointing upper-left (`390:fdpizz1`)
        peak = curved_tube(
            [(-0.04, 0.086, -0.05), (-0.11, 0.108, -0.13), (-0.16, 0.116, -0.18)],
            [0.062, 0.036, 0.013],
            samples_per_seg=5,
            radial_segs=16,
            cap_rings=3,
            name=f"{name}_peak",
            material="item_cheese",
        )
        parts.append(gradient_y_tint(peak, (0.90, 0.78, 0.38), (1.00, 0.96, 0.68)))

        for k, (px, py, pz, rx, rz) in enumerate((
            (-0.01, 0.112, -0.11, -12.0, -8.0),
            (+0.11, 0.098, -0.04, -6.0, -16.0),
            (+0.01, 0.108, +0.04, 4.0, -4.0),
            (-0.08, 0.100, +0.12, 12.0, 10.0),
            (+0.13, 0.086, +0.11, 10.0, -14.0),
        )):
            parts.append(_pepperoni_disc(f"{name}_pep_{k}", px, py, pz, radius=0.056, tilt_rx=rx, tilt_rz=rz))

    elif stage == 2:
        # Jaggedly bitten pale outer crust clinging around thick sauce + melted cheese + 4 pepperonis (`391:fdpizz2`)
        crust_pts = [
            (-0.24, 0.040, +0.08),
            (-0.16, 0.042, +0.23),
            (+0.05, 0.044, +0.27),
            (+0.23, 0.044, +0.17),
            (+0.26, 0.042, -0.02),
            (+0.17, 0.042, -0.19),
            (-0.02, 0.046, -0.23),
        ]
        parts.append(_pizza_dough_rim(f"{name}_crust", crust_pts, [0.046, 0.052, 0.056, 0.052, 0.048, 0.044, 0.036], jagged=True))
        parts.append(_pizza_sauce_bed(f"{name}_sauce", 0.235, 0.225, +0.02, +0.02, jagged=True))
        cheese = ellipsoid(0.180, 0.052, 0.190, u_segs=24, v_segs=12, name=f"{name}_cheese", material="item_cheese").translate(-0.04, 0.060, +0.01)
        parts.append(gradient_y_tint(cheese, (0.90, 0.68, 0.14), (1.00, 0.96, 0.42), noise_amp=0.04))
        peak = curved_tube([(-0.05, 0.084, -0.06), (-0.11, 0.106, -0.13), (-0.16, 0.112, -0.18)], [0.056, 0.034, 0.013], samples_per_seg=5, radial_segs=14, cap_rings=2, name=f"{name}_peak", material="item_cheese")
        parts.append(gradient_y_tint(peak, (0.90, 0.78, 0.38), (1.00, 0.96, 0.68)))
        for k, (px, py, pz, rx, rz) in enumerate((
            (0.00, 0.108, -0.10, -10.0, -6.0),
            (+0.12, 0.094, -0.02, -4.0, -14.0),
            (-0.01, 0.104, +0.05, 5.0, -2.0),
            (-0.09, 0.096, +0.13, 12.0, 10.0),
        )):
            parts.append(_pepperoni_disc(f"{name}_pep_{k}", px, py, pz, radius=0.055, tilt_rx=rx, tilt_rz=rz))
        parts.append(_torn_scrap(f"{name}_crumb0", -0.27, -0.03, 0.046, 0.034, 0.018, seed=2.1, material="item_bun_crumb", rgb_lo=(0.76, 0.66, 0.46), rgb_hi=(0.96, 0.90, 0.72), rot_y=28.0))
        parts.append(_torn_scrap(f"{name}_crumb1", +0.28, -0.12, 0.034, 0.024, 0.015, seed=3.4, material="item_bun_crumb", rgb_lo=(0.76, 0.66, 0.46), rgb_hi=(0.96, 0.90, 0.72), rot_y=-18.0))

    elif stage == 3:
        # Half-eaten cheese & sauce wedge on left + flat irregular torn sauce/crust scraps on right (`392:fdpizz3`)
        parts.append(_pizza_sauce_bed(f"{name}_sauce", 0.172, 0.200, -0.05, +0.02, jagged=True))
        cheese = ellipsoid(0.150, 0.050, 0.180, u_segs=24, v_segs=12, name=f"{name}_cheese", material="item_cheese").translate(-0.07, 0.058, +0.02)
        pc = cheese.positions.astype(np.float64)
        # Jagged chewed right edge on melted cheese wedge!
        right = pc[:, 0] > 0.04
        pc[right, 0] = 0.04 + 0.022 * np.sin(pc[right, 2] * 32.0 + 0.6)
        cheese.positions = pc.astype(np.float32)
        cheese.normals = recompute_smooth_normals(cheese.positions, cheese.indices)
        parts.append(gradient_y_tint(cheese, (0.88, 0.64, 0.12), (1.00, 0.92, 0.34), noise_amp=0.05))
        peak = curved_tube([(-0.06, 0.082, -0.06), (-0.11, 0.100, -0.12), (-0.16, 0.106, -0.17)], [0.050, 0.030, 0.013], samples_per_seg=4, radial_segs=14, cap_rings=2, name=f"{name}_peak", material="item_cheese")
        parts.append(gradient_y_tint(peak, (0.90, 0.78, 0.38), (1.00, 0.95, 0.65)))
        for k, (px, py, pz) in enumerate(((-0.02, 0.102, -0.10), (+0.06, 0.088, -0.02), (-0.02, 0.098, +0.05), (-0.09, 0.088, +0.13))):
            parts.append(_pepperoni_disc(f"{name}_pep_{k}", px, py, pz, radius=0.050))
        for k, (cx, cz, rx, rz, mat, c_lo, c_hi, ry) in enumerate((
            (+0.18, -0.11, 0.046, 0.032, "item_bun_crumb", (0.74, 0.64, 0.44), (0.94, 0.88, 0.70), 22.0),
            (+0.22, +0.02, 0.052, 0.038, "item_pizza_sauce", (0.68, 0.12, 0.09), (0.90, 0.22, 0.14), -35.0),
            (+0.17, +0.14, 0.044, 0.030, "item_pizza_sauce", (0.68, 0.12, 0.09), (0.90, 0.22, 0.14), 48.0),
            (+0.12, +0.23, 0.048, 0.032, "item_bun_crumb", (0.74, 0.64, 0.44), (0.94, 0.88, 0.70), -14.0),
            (+0.01, +0.24, 0.038, 0.028, "item_cheese", (0.86, 0.64, 0.12), (0.99, 0.90, 0.32), 64.0),
        )):
            parts.append(_torn_scrap(f"{name}_scrap_{k}", cx, cz, rx, rz, 0.017, seed=1.3 + k * 2.1, material=mat, rgb_lo=c_lo, rgb_hi=c_hi, rot_y=ry))

    else:
        # Stage 4: Upper-left warm golden cheese peak + bitten pepperoni remnant + soft torn sauce scraps (`393:fdpizz4`)
        parts.append(_torn_scrap(f"{name}_rem", -0.05, -0.08, 0.135, 0.098, 0.036, seed=4.2, material="item_cheese", rgb_lo=(0.76, 0.54, 0.18), rgb_hi=(0.98, 0.88, 0.36), rot_y=-12.0))
        peak = curved_tube([(-0.07, 0.050, -0.08), (-0.13, 0.076, -0.14), (-0.17, 0.088, -0.18)], [0.048, 0.030, 0.014], samples_per_seg=4, radial_segs=14, cap_rings=3, name=f"{name}_peak", material="item_cheese")
        parts.append(gradient_y_tint(peak, (0.82, 0.66, 0.26), (0.99, 0.92, 0.52)))
        parts.append(_pepperoni_disc(f"{name}_pep_0", +0.06, 0.056, -0.07, radius=0.058, bitten=True))
        for k, (cx, cz, rx, rz, mat, c_lo, c_hi, ry) in enumerate((
            (+0.18, -0.02, 0.044, 0.030, "item_pizza_sauce", (0.68, 0.12, 0.09), (0.90, 0.22, 0.14), 32.0),
            (-0.02, +0.16, 0.058, 0.046, "item_pizza_sauce", (0.66, 0.10, 0.08), (0.88, 0.20, 0.14), -24.0),
            (+0.09, +0.10, 0.034, 0.024, "item_bun_crumb", (0.74, 0.64, 0.44), (0.94, 0.88, 0.70), 54.0),
            (+0.11, +0.20, 0.032, 0.024, "item_pizza_sauce", (0.68, 0.12, 0.09), (0.90, 0.22, 0.14), -48.0),
        )):
            parts.append(_torn_scrap(f"{name}_scrap_{k}", cx, cz, rx, rz, 0.017, seed=5.5 + k * 1.9, material=mat, rgb_lo=c_lo, rgb_hi=c_hi, rot_y=ry))

    sc.add("pizza", parts)
    return _finalize_item(sc, target_span=0.76 if stage <= 2 else (0.68 if stage == 3 else 0.56))


def build_fdpizz1() -> SceneBuilder:
    return _build_squeezed_pizza("item_fdpizz1", 1)


def build_fdpizz2() -> SceneBuilder:
    return _build_squeezed_pizza("item_fdpizz2", 2)


def build_fdpizz3() -> SceneBuilder:
    return _build_squeezed_pizza("item_fdpizz3", 3)


def build_fdpizz4() -> SceneBuilder:
    return _build_squeezed_pizza("item_fdpizz4", 4)


# ---------------------------------------------------------------------------
# 2. STEAK WHIZ (`377:steakwhiz`) & Star-Tip Meat Paste (`378..381:fdsteak1..4`)
# ---------------------------------------------------------------------------

def build_steakwhiz() -> SceneBuilder:
    """Horizontal crimson & silver 'STEAK WHIZ' aerosol spray can with black nozzle on LEFT (`377:steakwhiz`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_steakwhiz")
    parts: List[MeshPart] = []

    # Cylindrical crimson aerosol body along X (-0.23 left shoulder -> +0.34 right bottom rim)
    n_x, n_rad = 20, 44
    x_lo, x_hi = -0.225, +0.335
    rad = 0.168
    verts, uvs, tris = [], [], []
    for ix in range(n_x + 1):
        uf = ix / float(n_x)  # u=0 at left shoulder (-X), u=1 at right bottom (+X)
        x = x_lo + (x_hi - x_lo) * uf
        for j in range(n_rad + 1):
            vf_c = j / float(n_rad)
            # Map v=0.5 squarely to upper-front face (+Y/+Z at ~50 deg above horizon) so label reads cleanly!
            ang = -0.62 * math.pi + vf_c * 2.0 * math.pi
            # ang=0 -> (0, +rad, 0) top; ang=+pi/2 -> (0, 0, +rad) front
            py = rad * math.cos(ang)
            pz = rad * math.sin(ang)
            verts.append((x, py, pz))
            # Map front-top 180 deg arc to full texture v=0..1; wrap cleanly around rear
            tex_v = (vf_c * 1.45 - 0.14) % 1.0
            uvs.append((uf, tex_v))
    stride = n_rad + 1
    for ix in range(n_x):
        for j in range(n_rad):
            a, b, c, d = ix * stride + j, (ix + 1) * stride + j, (ix + 1) * stride + (j + 1), ix * stride + (j + 1)
            tris.extend(((a, c, b), (a, d, c)))
    pos_c = np.asarray(verts, dtype=np.float32)
    idx_c = np.asarray(tris, dtype=np.uint32).reshape(-1)
    can_body = MeshPart("steakwhiz_body", "item_steak_whiz", pos_c, recompute_smooth_normals(pos_c, idx_c), idx_c, uvs=np.asarray(uvs, dtype=np.float32))
    parts.append(can_body)

    # Gleaming metallic aluminum left dome shoulder & rolled crimp rims
    shoulder_prof = [
        (0.168, -0.225),
        (0.176, -0.236),
        (0.166, -0.252),
        (0.148, -0.282),
        (0.114, -0.318),
        (0.072, -0.346),
        (0.056, -0.362),
        (0.052, -0.376),
    ]
    shoulder = lathe(shoulder_prof, radial_segs=38, cap_bottom=False, cap_top=True, axis="x", name="steakwhiz_shoulder", material="item_aluminum")
    parts.append(shoulder)
    # Right silver bottom chime rim + recessed base
    base_prof = [(0.168, +0.335), (0.175, +0.346), (0.162, +0.364), (0.132, +0.368), (0.112, +0.354), (0.000, +0.348)]
    base = lathe(base_prof, radial_segs=38, cap_bottom=False, cap_top=False, axis="x", name="steakwhiz_base", material="item_aluminum")
    parts.append(base)

    # Fluted black spray-cheese/steak nozzle pointing LEFT (`-X`) + crisp white spray tip!
    nozzle_prof = [
        (0.050, -0.374),
        (0.048, -0.398),
        (0.038, -0.430),
        (0.032, -0.470),
        (0.025, -0.515),
        (0.018, -0.542),
    ]
    nozzle = lathe(nozzle_prof, radial_segs=24, cap_bottom=False, cap_top=True, axis="x", name="steakwhiz_nozzle", material="item_rubber_dark")
    parts.append(nozzle)
    spray_tip = cylinder(0.014, 0.019, 0.055, radial_segs=18, bevel=0.004, axis="x", name="steakwhiz_tip", material="item_candy_white").translate(-0.562, 0.0, 0.0)
    parts.append(spray_tip)

    # Slight horizontal counter-clockwise tilt matching `377:steakwhiz` (nozzle pointing left, tilted ~5 deg)
    sc.add("steakwhiz", [p.rotate_xyz(ry=6.0, rz=-3.0, degrees=True) for p in parts])
    return _finalize_item(sc, target_span=0.98)


def _build_steak_paste(name: str, stage: int) -> SceneBuilder:
    """Squeezed wide flat razor-fluted Steak Whiz ribbon stages (`378..381:fdsteak1..4`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    # Authentic 1998 Steak Whiz solid thick 5-ridge serrated ribbon — non-intersecting stacked loops, ZERO hollow gaps!
    ribbon_kw = dict(
        n_flutes=10,
        flute_depth=0.44,
        width_ratio=1.34,
        height_ratio=0.76,
        samples_per_seg=8,
        radial_segs=56,
        valley_rgb=(0.40, 0.25, 0.16),
        mid_rgb=(0.88, 0.74, 0.62),
        crest_rgb=(1.00, 0.96, 0.88),
        material="item_steak_paste",
    )

    if stage == 1:
        # Exact `#378:fdsteak1`: solid thick folded oval Z-ribbon with crisp center eye & upper folded bar resting ON TOP (zero Z-fighting!).
        pts = [
            (+0.22, 0.046, -0.155),
            (+0.01, 0.048, -0.135),
            (-0.20, 0.046, -0.100),
            (-0.27, 0.046, +0.035),
            (-0.14, 0.048, +0.165),
            (+0.07, 0.050, +0.168),
            (+0.25, 0.062, +0.070),
            (+0.25, 0.096, -0.025),
            (+0.07, 0.114, -0.010),
            (-0.14, 0.106, +0.020),
        ]
        rads = [0.054, 0.058, 0.058, 0.056, 0.058, 0.060, 0.058, 0.056, 0.056, 0.050]
        parts.append(extrude_star_piped_ribbon(pts, rads, start_pinch=True, end_pinch=True, name=f"{name}_coil", **ribbon_kw))
    elif stage == 2:
        pts = [
            (+0.18, 0.046, -0.135),
            (+0.00, 0.048, -0.120),
            (-0.20, 0.046, -0.085),
            (-0.26, 0.046, +0.045),
            (-0.13, 0.048, +0.158),
            (+0.07, 0.050, +0.158),
            (+0.24, 0.064, +0.055),
            (+0.19, 0.098, -0.018),
            (-0.02, 0.104, +0.010),
        ]
        rads = [0.054, 0.058, 0.058, 0.055, 0.058, 0.060, 0.056, 0.054, 0.050]
        parts.append(extrude_star_piped_ribbon(pts, rads, start_pinch=False, end_pinch=True, jagged_start=True, name=f"{name}_coil", **ribbon_kw))
    elif stage == 3:
        pts = [
            (-0.02, 0.046, -0.120),
            (-0.20, 0.046, -0.090),
            (-0.25, 0.046, +0.040),
            (-0.12, 0.048, +0.150),
            (+0.07, 0.050, +0.145),
            (+0.23, 0.064, +0.040),
            (+0.10, 0.094, -0.020),
        ]
        rads = [0.052, 0.056, 0.056, 0.058, 0.060, 0.056, 0.052]
        parts.append(extrude_star_piped_ribbon(pts, rads, start_pinch=False, end_pinch=False, jagged_start=True, jagged_end=True, name=f"{name}_coil", **ribbon_kw))
    else:
        pts = [
            (-0.26, 0.050, +0.09),
            (-0.09, 0.054, +0.03),
            (+0.09, 0.054, -0.03),
            (+0.26, 0.050, -0.09),
        ]
        rads = [0.064, 0.068, 0.068, 0.062]
        parts.append(extrude_star_piped_ribbon(pts, rads, start_pinch=False, end_pinch=False, jagged_start=True, jagged_end=True, name=f"{name}_ribbon", **ribbon_kw))

    sc.add("steak_paste", parts)
    return _finalize_item(sc, target_span=0.72 if stage <= 3 else 0.56)


def build_fdsteak1() -> SceneBuilder:
    return _build_steak_paste("item_fdsteak1", 1)


def build_fdsteak2() -> SceneBuilder:
    return _build_steak_paste("item_fdsteak2", 2)


def build_fdsteak3() -> SceneBuilder:
    return _build_steak_paste("item_fdsteak3", 3)


def build_fdsteak4() -> SceneBuilder:
    return _build_steak_paste("item_fdsteak4", 4)


# ---------------------------------------------------------------------------
# 3. TWIN SUNNY-SIDE-UP FRIED EGGS (`385..388:fdeggs1..4`)
# ---------------------------------------------------------------------------

def _egg_yolk(name: str, cx: float, cz: float, r_xz: float, h_y: float, y_base: float, *, bare: bool = False) -> List[MeshPart]:
    """Luminous translucent honey-gold sunny-side-up yolk nestled seamlessly in egg white (zero artificial rings)."""
    yolk = ellipsoid(r_xz, h_y, r_xz * 0.95, u_segs=34, v_segs=20, name=name, material="item_egg_yolk")
    p = yolk.positions.astype(np.float64)
    # Natural liquid-surface tension dome: plump convex top, gently splayed basal skirt (seamless meniscus!)
    below = p[:, 1] < 0.0
    p[below, 1] *= 0.22
    r_frac = np.hypot(p[:, 0], p[:, 2]) / max(1e-4, r_xz)
    if bare:
        # Organic subtle liquid-droplet foot flare right at bottom rim (zero separate dish cylinder!)
        y_n = np.clip((p[:, 1] + h_y * 0.22) / max(1e-4, h_y * 1.22), 0.0, 1.0)
        flare = 1.0 + 0.06 * np.exp(-((y_n / 0.16) ** 2))
        p[:, 0] *= flare
        p[:, 2] *= flare
    p[:, 1] += 0.008 * np.exp(-((r_frac / 0.52) ** 2))
    p[:, 1] += y_base
    p[:, 0] += cx
    p[:, 2] += cz
    yolk.positions = p.astype(np.float32)
    yolk.normals = recompute_smooth_normals(yolk.positions, yolk.indices)
    # Rich translucent yolk shading: warm deep amber-orange base -> luminous glowing sunlit golden crown!
    yolk = tint_part(
        yolk,
        lambda pos, nrm: np.clip(
            np.column_stack([
                0.96 + 0.04 * np.clip(nrm[:, 1], 0.0, 1.0),
                0.56 + 0.39 * (np.clip(nrm[:, 1], 0.0, 1.0) ** 0.68),
                0.02 + 0.26 * (np.clip(nrm[:, 1], 0.0, 1.0) ** 1.60),
            ]),
            0.02,
            1.0,
        ),
    )
    return [yolk]


def _egg_white_sheet(name: str, stage: int) -> MeshPart:
    """Two distinct fused pan-fried sunny-side-up egg whites with center fusion seam, pillowy albumen & crispy skillet rim!"""
    n_rings, n_ang = 26, 96
    verts, uvs, cols, tris = [], [], [], []

    def _twin_egg_sdf(x: float, z: float) -> float:
        # Smooth union (smin) of TWO fused oval fried eggs matching `#385:fdeggs1`!
        # Left egg: fuller top shoulder + big rounded lower-left white apron!
        dx1 = (x + 0.155) / 0.280
        dz1 = (z - 0.020) / 0.270
        d1 = math.hypot(dx1, dz1) - 1.0
        dx1b = (x + 0.185) / 0.190
        dz1b = (z - 0.175) / 0.175
        d1b = math.hypot(dx1b, dz1b) - 1.0
        k_a = 0.28
        h_a = max(0.0, min(1.0, 0.5 + 0.5 * (d1b - d1) / k_a))
        d_left = d1b * (1.0 - h_a) + d1 * h_a - k_a * h_a * (1.0 - h_a)

        # Right egg: fuller oval wrapping neatly around & below the right yolk!
        dx2 = (x - 0.185) / 0.255
        dz2 = (z - 0.005) / 0.225
        d_right = math.hypot(dx2, dz2) - 1.0

        # Smooth pan-fusion seam between left & right egg whites
        k_f = 0.24
        h_f = max(0.0, min(1.0, 0.5 + 0.5 * (d_right - d_left) / k_f))
        return d_right * (1.0 - h_f) + d_left * h_f - k_f * h_f * (1.0 - h_f)

    def _boundary_r_and_bitten(ang: float) -> Tuple[float, float]:
        ca, sa = math.cos(ang), math.sin(ang)
        lo, hi = 0.05, 0.64
        for _ in range(14):
            mid = 0.5 * (lo + hi)
            if _twin_egg_sdf(mid * ca, mid * sa) < 0.0:
                lo = mid
            else:
                hi = mid
        base_r = 0.5 * (lo + hi)
        frill = 1.0 + 0.032 * math.sin(5.0 * ang + 0.4) + 0.020 * math.cos(11.0 * ang - 0.3) + 0.010 * math.sin(17.0 * ang)
        bitten = 0.0
        if stage == 2:
            # Crisp scalloped bite notches matching `#386:fdeggs2` (top-left, top-right, bottom-left rim)
            for ba, bw, bd in (
                (-2.55, 0.22, 0.16),
                (-0.48, 0.22, 0.18),
                (+0.62, 0.24, 0.15),
                (+2.18, 0.24, 0.15),
            ):
                da = math.atan2(math.sin(ang - ba), math.cos(ang - ba))
                w_b = math.exp(-((da / bw) ** 2))
                frill -= bd * w_b
                bitten = max(bitten, w_b)
        elif stage == 3:
            # Exact `#387:fdeggs3`: keep rounded right-side & lower-right white around right yolk;
            # ragged chewed bites along top, far-left, and lower-left apron!
            frill *= 0.88
            frill += 0.048 * math.sin(14.0 * ang + 0.5) + 0.032 * math.cos(21.0 * ang - 0.4)
            for ba, bw, bd in (
                (-1.60, 0.34, 0.28),
                (-2.75, 0.36, 0.30),
                (+2.85, 0.34, 0.26),
                (+1.58, 0.28, 0.18),
                (-0.78, 0.28, 0.24),
            ):
                da = math.atan2(math.sin(ang - ba), math.cos(ang - ba))
                w_b = math.exp(-((da / bw) ** 2))
                frill -= bd * w_b
                bitten = max(bitten, w_b)
            bitten = max(bitten, 0.45)
        return max(0.14, base_r * max(0.42, frill)), bitten

    bound_r = []
    bound_bit = []
    for j in range(n_ang + 1):
        ang = 2.0 * math.pi * j / float(n_ang)
        r_b, bit_b = _boundary_r_and_bitten(ang)
        bound_r.append(r_b)
        bound_bit.append(bit_b)

    def _top_y_and_color(x: float, z: float, rf: float, bitten_w: float) -> Tuple[float, Tuple[float, float, float]]:
        d1 = math.hypot(x + 0.115, z + 0.022)
        d2 = math.hypot(x - 0.158, z + 0.015)
        # Soft pillowy inner albumen cushions cradling each yolk seamlessly!
        cush1 = 0.052 * (1.0 / (1.0 + math.exp((d1 - 0.180) * 24.0)))
        cush2 = 0.044 * (1.0 / (1.0 + math.exp((d2 - 0.155) * 26.0)))
        thick_albumen = max(cush1, cush2)
        # Authentic center skillet fusion valley seam between left & right egg whites
        seam_d = abs(x - 0.032 + 0.14 * z)
        fusion_crease = 0.016 * math.exp(-((seam_d / 0.046) ** 2)) * (1.0 - rf * 0.45)
        blisters = 0.0040 * max(0.0, math.sin(x * 48.0 + z * 34.0) * math.cos(z * 42.0 - x * 28.0)) * (rf ** 1.4)
        base_skirt = 0.016 * ((1.0 - rf ** 3.2) ** 0.42) + 0.006
        rim_curl = 0.0080 * math.exp(-(((rf - 0.94) / 0.065) ** 2)) * (1.0 - 0.55 * bitten_w)
        y = max(0.004, base_skirt + thick_albumen * (1.0 - 0.68 * (rf ** 5)) - fusion_crease + blisters + rim_curl)

        # Snowy glossy cushion -> subtle cool translucent pearl skirt & fusion crease -> delicate golden fried rim!
        thick_w = max(0.0, min(1.0, thick_albumen / 0.044))
        seam_sh = 1.0 - 0.16 * math.exp(-((seam_d / 0.054) ** 2))
        crisp_rim = max(0.0, min(1.0, (rf - 0.88) / 0.12)) * (1.0 - 0.78 * bitten_w)
        r_c = (0.91 + 0.09 * thick_w) * seam_sh
        g_c = (0.93 + 0.07 * thick_w) * seam_sh
        b_c = (0.95 + 0.04 * thick_w) * seam_sh
        r_c = r_c * (1.0 - crisp_rim * 0.14) + 0.86 * (crisp_rim * 0.14)
        g_c = g_c * (1.0 - crisp_rim * 0.40) + 0.58 * (crisp_rim * 0.40)
        b_c = b_c * (1.0 - crisp_rim * 0.66) + 0.24 * (crisp_rim * 0.66)
        return y, (max(0.18, min(1.0, r_c)), max(0.18, min(1.0, g_c)), max(0.16, min(1.0, b_c)))

    ring_specs = [(ir / float(n_rings), True) for ir in range(n_rings + 1)] + [(1.0 - ir / float(n_rings), False) for ir in range(n_rings + 1)]
    for rf, is_top in ring_specs:
        for j in range(n_ang + 1):
            ang = 2.0 * math.pi * j / float(n_ang)
            r_b = bound_r[j]
            x = rf * r_b * math.cos(ang)
            z = rf * r_b * math.sin(ang)
            if is_top:
                y, rgb = _top_y_and_color(x, z, rf, bound_bit[j])
            else:
                y = 0.005 if rf > 0.95 else 0.001
                rgb = (0.78, 0.72, 0.60) if rf > 0.88 else (0.86, 0.88, 0.90)
            verts.append((x, y, z))
            uvs.append((0.5 + x, 0.5 + z))
            cols.append(rgb)
    stride = n_ang + 1
    for ir in range(len(ring_specs) - 1):
        for j in range(n_ang):
            a, b, c, d = ir * stride + j, (ir + 1) * stride + j, (ir + 1) * stride + (j + 1), ir * stride + (j + 1)
            tris.extend(((a, c, b), (a, d, c)))
    pos = np.asarray(verts, dtype=np.float32)
    idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
    return MeshPart(
        name,
        "item_egg_white",
        pos,
        recompute_smooth_normals(pos, idx),
        idx,
        uvs=np.asarray(uvs, dtype=np.float32),
        colors=np.asarray(cols, dtype=np.float32),
    )


def _build_fried_eggs(name: str, stage: int) -> SceneBuilder:
    """Twin sunny-side-up fried eggs progressively eaten from fused skillet whites down to 2 bare yolks (`385..388`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    if stage <= 3:
        parts.append(_egg_white_sheet(f"{name}_white", stage))
        y_base = 0.048
        parts.extend(_egg_yolk(f"{name}_yolk_l", -0.115, -0.022, 0.138, 0.096, y_base, bare=False))
        parts.extend(_egg_yolk(f"{name}_yolk_r", +0.158, -0.015, 0.115, 0.082, y_base - 0.005, bare=False))
        if stage == 3:
            # Tiny torn detached white egg-white scraps off the far-left chewed edge matching `#387:fdeggs3`!
            parts.append(_torn_scrap(f"{name}_bit0", -0.35, -0.06, 0.042, 0.028, 0.015, seed=7.1, material="item_egg_white", rgb_lo=(0.86, 0.88, 0.90), rgb_hi=(0.99, 0.99, 0.98), rot_y=-24.0))
            parts.append(_torn_scrap(f"{name}_bit1", -0.36, +0.11, 0.036, 0.020, 0.014, seed=8.3, material="item_egg_white", rgb_lo=(0.86, 0.88, 0.90), rgb_hi=(0.99, 0.99, 0.98), rot_y=18.0))
    else:
        # Stage 4 (`388:fdeggs4`): two bare gleaming domed golden egg yolks side-by-side!
        parts.extend(_egg_yolk(f"{name}_yolk_l", -0.155, +0.002, 0.148, 0.110, 0.010, bare=True))
        parts.extend(_egg_yolk(f"{name}_yolk_r", +0.165, -0.008, 0.118, 0.090, 0.010, bare=True))

    sc.add("eggs", parts)
    return _finalize_item(sc, target_span=0.92 if stage <= 3 else 0.74)


def build_fdeggs1() -> SceneBuilder:
    return _build_fried_eggs("item_fdeggs1", 1)


def build_fdeggs2() -> SceneBuilder:
    return _build_fried_eggs("item_fdeggs2", 2)


def build_fdeggs3() -> SceneBuilder:
    return _build_fried_eggs("item_fdeggs3", 3)


def build_fdeggs4() -> SceneBuilder:
    return _build_fried_eggs("item_fdeggs4", 4)


# ---------------------------------------------------------------------------
# 4. CHOCO-FISHY SNAX (`394..398:fdfish1..5`)
# ---------------------------------------------------------------------------

def _fish_wrapper_sleeve(name: str, *, empty: bool = False) -> MeshPart:
    """Crisp white-and-navy 'CHOCO-FISHY SNAX' paper sleeve around mid-body (or slightly arched flat empty wrapper)."""
    n_x, n_z = 16, 18
    x_half = 0.145
    z_half = 0.180
    if empty:
        # Gently arched/rolled flattened 3D paper sleeve (guaranteed size_y >= 0.08!)
        verts, uvs, tris = [], [], []
        for top in (True, False):
            for iz in range(n_z + 1):
                vf = iz / float(n_z)
                z = -z_half + 2.0 * z_half * vf
                for ix in range(n_x + 1):
                    uf = ix / float(n_x)
                    x = -x_half + 2.0 * x_half * uf
                    # Natural curved paper arch + crinkled corners so it looks tactile and 3D!
                    arch = 0.042 * math.sin(uf * math.pi) + 0.014 * math.sin(vf * math.pi) + 0.008 * math.sin(uf * 9.0 + vf * 7.0)
                    y = (0.022 + arch) if top else (0.004 + arch * 0.35)
                    verts.append((x, y, z))
                    uvs.append((uf, vf))
        stride = n_x + 1
        off = (n_z + 1) * stride
        for iz in range(n_z):
            for ix in range(n_x):
                a, b, c, d = iz * stride + ix, (iz + 1) * stride + ix, (iz + 1) * stride + (ix + 1), iz * stride + (ix + 1)
                tris.extend(((a, b, c), (a, c, d)))
                a2, b2, c2, d2 = off + a, off + b, off + c, off + d
                tris.extend(((a2, c2, b2), (a2, d2, c2)))
        pos = np.asarray(verts, dtype=np.float32)
        idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
        return MeshPart(name, "item_choco_fishy", pos, recompute_smooth_normals(pos, idx), idx, uvs=np.asarray(uvs, dtype=np.float32))

    # Full 3D oval paper sleeve wrapped snugly OVER the chocolate-dipped fish belly (outward normals!)
    n_u, n_v = 18, 36
    verts, uvs, tris = [], [], []
    for iu in range(n_u + 1):
        uf = iu / float(n_u)
        x = -x_half + 2.0 * x_half * uf
        for iv in range(n_v + 1):
            ang = 2.0 * math.pi * iv / float(n_v)
            # Oval cross-section slightly proud of chocolate coating so 'CHOCO-FISHY SNAX' wraps cleanly on top!
            pz = -0.178 * math.cos(ang)
            py = 0.114 + 0.116 * math.sin(ang)
            verts.append((x, py, pz))
            # Top-facing half maps v=0..1 from -Z (fish back) to +Z (fish belly) so text reads upright!
            tex_v = 0.5 + 0.5 * (pz / 0.178)
            uvs.append((uf, tex_v))
    stride = n_v + 1
    for iu in range(n_u):
        for iv in range(n_v):
            a, b, c, d = iu * stride + iv, (iu + 1) * stride + iv, (iu + 1) * stride + (iv + 1), iu * stride + (iv + 1)
            tris.extend(((a, c, b), (a, d, c)))
    pos = np.asarray(verts, dtype=np.float32)
    idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
    return MeshPart(name, "item_choco_fishy", pos, recompute_smooth_normals(pos, idx), idx, uvs=np.asarray(uvs, dtype=np.float32))


def _fish_fan_fin(
    name: str,
    *,
    root_x: float,
    root_z: float,
    dir_x: float,
    dir_z: float,
    length: float,
    spread_rad: float,
    y_mid: float = 0.092,
    n_rays: int = 7,
    v_notch: float = 0.16,
) -> MeshPart:
    """Authentic ribbed golden-yellow fan-shaped fish fin with crisp dark radial fin rays (`394..395:fdfish1..2`)!"""
    base_ang = math.atan2(dir_z, dir_x)
    n_r, n_a = 12, 24
    verts, cols, tris = [], [], []
    for is_top in (True, False):
        for ir in range(n_r + 1):
            rf = ir / float(n_r)
            for ia in range(n_a + 1):
                af = ia / float(n_a)  # 0..1 across fan spread
                ang = base_ang + (af - 0.5) * spread_rad
                # V-notch in the middle of tail fin or rounded lobe on pectoral fin!
                edge_l = length * (1.0 - v_notch * math.exp(-(((af - 0.5) / 0.24) ** 2)))
                rr = (0.016 + (edge_l - 0.016) * rf)
                px = root_x + rr * math.cos(ang)
                pz = root_z + rr * math.sin(ang)
                # Crisp corrugated radial fin rays!
                ray_w = 0.5 + 0.5 * math.cos(af * math.pi * 2.0 * n_rays)
                thick = (0.018 * (1.0 - 0.60 * rf) + 0.005 * ray_w * rf)
                py = y_mid + (thick if is_top else -thick * 0.7)
                verts.append((px, max(0.008, py), pz))
                # Bright sunny golden-yellow fin rays with crisp warm-brown recessed ray valleys!
                r_c = 0.56 + 0.44 * ray_w
                g_c = 0.36 + 0.56 * ray_w
                b_c = 0.08 + 0.16 * ray_w
                cols.append((r_c, g_c, b_c))
    st = n_a + 1
    off = (n_r + 1) * st
    for ir in range(n_r):
        for ia in range(n_a):
            a, b, c, d = ir * st + ia, (ir + 1) * st + ia, (ir + 1) * st + ia + 1, ir * st + ia + 1
            tris.extend(((a, c, b), (a, d, c)))
            tris.extend(((off + a, off + b, off + c), (off + a, off + c, off + d)))
    # Stitch outer fan margin cleanly
    r_end = n_r * st
    for ia in range(n_a):
        t0, t1, b0, b1 = r_end + ia, r_end + ia + 1, off + r_end + ia, off + r_end + ia + 1
        tris.extend(((t0, t1, b1), (t0, b1, b0)))
    pos = np.asarray(verts, dtype=np.float32)
    idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
    return MeshPart(name, "item_cheese", pos, recompute_smooth_normals(pos, idx), idx, colors=np.asarray(cols, dtype=np.float32))


def _fish_tail_fin(name: str, *, nibbled: bool = False) -> List[MeshPart]:
    """Wide striped golden-yellow fan tail fin protruding LEFT (`-X`) matching `394..395:fdfish1..2`!"""
    ln = 0.18 if nibbled else 0.255
    return [
        _fish_fan_fin(
            f"{name}_fan",
            root_x=-0.225,
            root_z=0.0,
            dir_x=-1.0,
            dir_z=0.0,
            length=ln,
            spread_rad=1.22,
            y_mid=0.104,
            n_rays=7,
            v_notch=0.22,
        )
    ]


def _build_choco_fishy(name: str, stage: int) -> SceneBuilder:
    """'CHOCO-FISHY SNAX' plump chocolate-dipped scaly green fish with mouth, gill & ribbed fan fins (`394..398`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    if stage in (1, 2):
        # 1. Crisp paper sleeve wrapped tightly around plump fish waist (`394..395`)!
        parts.append(_fish_wrapper_sleeve(f"{name}_wrap", empty=False).scale(0.92, 1.04, 1.08))

        # 2. Rich warm reddish-brown MILK-CHOCOLATE dip bulging plumply out BOTH left & right of the wrapper (`394..395`)!
        choco = ellipsoid(0.280, 0.114, 0.198, u_segs=32, v_segs=18, name=f"{name}_choco", material="item_candy_white").translate(+0.018, 0.118, 0.0)
        pc = choco.positions.astype(np.float64)
        drip = np.sin(pc[:, 2] * 22.0 + pc[:, 1] * 12.0) * 0.026
        pc[:, 0] += np.sign(pc[:, 0]) * drip * np.clip(np.abs(pc[:, 0]) / 0.18, 0.0, 1.0)
        choco.positions = pc.astype(np.float32)
        choco.normals = recompute_smooth_normals(choco.positions, choco.indices)
        parts.append(gradient_y_tint(choco, (0.18, 0.035, 0.020), (0.36, 0.075, 0.042), noise_amp=0.02))
        # Glossy wavy milk-chocolate drip collar peeking right in front of the wrapper (`x ~ +0.16..+0.23`) matching `#394`!
        collar = ellipsoid(0.088, 0.108, 0.184, u_segs=24, v_segs=14, name=f"{name}_choco_collar", material="item_candy_white").translate(+0.188, 0.116, 0.0)
        pcol = collar.positions.astype(np.float64)
        pcol[:, 0] += 0.024 * np.sin(pcol[:, 2] * 26.0 + 0.6)
        collar.positions = pcol.astype(np.float32)
        collar.normals = recompute_smooth_normals(collar.positions, collar.indices)
        parts.append(gradient_y_tint(collar, (0.18, 0.035, 0.020), (0.38, 0.080, 0.045), noise_amp=0.02))

        # 3. Wide ribbed golden-yellow fan tail fin on LEFT (`-X`) + bold dorsal fin peeking top-right behind head!
        parts.extend(_fish_tail_fin(f"{name}_tail", nibbled=(stage == 2)))
        dorsal = _fish_fan_fin(f"{name}_dorsal", root_x=+0.175, root_z=-0.160, dir_x=+0.24, dir_z=-1.0, length=0.102, spread_rad=0.78, y_mid=0.116, n_rays=5, v_notch=0.0)
        parts.append(dorsal)

        # 4. Plump vibrant-green SCALY fish head protruding RIGHT (`+X`) with sculpted overlapping scales, gill arch & smiling open mouth!
        head_x = 0.255 if stage == 1 else 0.225
        head_len = 0.225 if stage == 1 else 0.175
        head = ellipsoid(head_len, 0.106, 0.170, u_segs=44, v_segs=24, name=f"{name}_head", material="item_fish_green").translate(head_x, 0.114, 0.0)
        ph = head.positions.astype(np.float64)
        t_snout = np.clip((ph[:, 0] - 0.20) / max(0.08, head_len * 1.20), 0.0, 1.0)
        ph[:, 2] *= 1.0 - 0.46 * (t_snout ** 1.20)
        ph[:, 1] = 0.112 + (ph[:, 1] - 0.112) * (1.0 - 0.34 * (t_snout ** 1.25))
        # Sculpt overlapping imbricated fish scales across cheeks/head + prominent wide horizontal mouth cleft!
        scale_u = ph[:, 0] * 50.0
        scale_v = ph[:, 2] * 50.0 + 0.5 * np.pi * (np.floor(scale_u / math.pi) % 2.0)
        scale_bump = 0.0060 * np.clip(np.sin(scale_u) * np.cos(scale_v), -0.4, 1.0) * (1.0 - 0.60 * t_snout)
        mouth_x = np.clip((ph[:, 0] - 0.345) / 0.12, 0.0, 1.0) if stage == 1 else np.zeros(len(ph))
        mouth_z = np.exp(-(((ph[:, 2] - 0.032) / 0.022) ** 2))
        ph[:, 1] += scale_bump - 0.022 * mouth_x * mouth_z
        if stage == 2:
            nose = ph[:, 0] > 0.365
            ph[nose, 0] = 0.365 + 0.012 * np.sin(ph[nose, 2] * 45.0)
        head.positions = ph.astype(np.float32)
        head.normals = recompute_smooth_normals(head.positions, head.indices)

        def _scaly_fish_tint(pos: np.ndarray, nrm: np.ndarray) -> np.ndarray:
            su = pos[:, 0] * 50.0
            sv = pos[:, 2] * 50.0 + 0.5 * math.pi * (np.floor(su / math.pi) % 2.0)
            sc_v = 0.5 + 0.5 * np.sin(su) * np.cos(sv)
            # Golden-green cheek stripe + crisp dark-green scale edges + bold dark smiling mouth cleft!
            belly = np.exp(-(((pos[:, 2] - 0.044) / 0.068) ** 2)) * np.clip((pos[:, 0] - 0.24) / 0.18, 0.0, 1.0)
            m_line = np.exp(-(((pos[:, 2] - 0.032) / 0.015) ** 2)) * np.clip((pos[:, 0] - 0.335) / 0.045, 0.0, 1.0) if stage == 1 else np.zeros(len(pos))
            hi = np.clip(0.55 + 0.45 * nrm[:, 1], 0.3, 1.0)
            r_c = (0.26 + 0.36 * sc_v + 0.38 * belly) * hi * (1.0 - 0.85 * m_line)
            g_c = (0.64 + 0.34 * sc_v + 0.24 * belly) * hi * (1.0 - 0.88 * m_line)
            b_c = (0.14 + 0.14 * sc_v + 0.12 * belly) * hi * (1.0 - 0.88 * m_line)
            return np.clip(np.column_stack([r_c, g_c, b_c]), 0.04, 1.0)

        parts.append(tint_part(head, _scaly_fish_tint))

        # Sculpted plump fish lips & smiling open mouth cavity right across the lower snout (stage 1)!
        if stage == 1:
            mouth_dark = ellipsoid(0.068, 0.022, 0.022, u_segs=16, v_segs=10, name=f"{name}_mouth_cav", material="item_soil_hole").rotate_xyz(ry=-10.0, degrees=True).translate(+0.425, 0.126, +0.032)
            lip_u = curved_tube([(+0.348, 0.144, +0.016), (+0.415, 0.140, +0.020), (+0.476, 0.126, +0.024)], [0.013, 0.015, 0.012], samples_per_seg=4, radial_segs=12, cap_rings=2, name=f"{name}_lip_u", material="item_fish_green")
            lip_l = curved_tube([(+0.348, 0.130, +0.050), (+0.412, 0.126, +0.052), (+0.468, 0.116, +0.042)], [0.012, 0.014, 0.011], samples_per_seg=4, radial_segs=12, cap_rings=2, name=f"{name}_lip_l", material="item_fish_green")
            parts.extend([mouth_dark, gradient_y_tint(lip_u, (0.48, 0.86, 0.24), (0.80, 0.98, 0.44)), gradient_y_tint(lip_l, (0.42, 0.80, 0.20), (0.76, 0.96, 0.40))])

        # Big bold glossy cartoon fish eye (warm orange-amber outer iris + dark pupil + white catchlight!) matching `#394`!
        eye_x = 0.325 if stage == 1 else 0.305
        eye_ring = tint_part(sphere(0.058, u_segs=20, v_segs=14, name=f"{name}_eye_ring", material="item_candy_white").translate(eye_x, 0.172, -0.030), _const_tint((0.92, 0.44, 0.10)))
        pupil = sphere(0.034, u_segs=16, v_segs=10, name=f"{name}_eye_pupil", material="eye_pupil").translate(eye_x + 0.005, 0.202, -0.030)
        glint = sphere(0.011, u_segs=10, v_segs=8, name=f"{name}_eye_glint", material="eye_catchlight").translate(eye_x - 0.008, 0.228, -0.042)
        parts.extend([eye_ring, pupil, glint])

        # Broad ribbed golden-yellow FAN pectoral fin sweeping down-front (+Z) matching `394..395:fdfish1..2`!
        pect = _fish_fan_fin(f"{name}_pect_fin", root_x=+0.205, root_z=+0.115, dir_x=-0.38, dir_z=+0.92, length=0.175, spread_rad=1.08, y_mid=0.072, n_rays=7, v_notch=0.0)
        parts.append(pect)

        # Curved dark-green gill cover arch behind cheek
        gill = torus_arc(0.095, 0.011, start_ang=-0.95, end_ang=+0.95, u_segs=18, v_segs=8, plane="xz", name=f"{name}_gill", material="item_toy_green_dark").translate(+0.220, 0.170, 0.0)
        parts.append(gill)

        if stage == 2:
            parts.append(ellipsoid(0.032, 0.020, 0.026, u_segs=12, v_segs=8, name=f"{name}_nose_crumb", material="item_fish_green").translate(+0.455, 0.032, +0.02))

    elif stage == 3:
        parts.append(_fish_wrapper_sleeve(f"{name}_wrap", empty=False).scale(0.92, 1.04, 1.08))
        choco = ellipsoid(0.268, 0.108, 0.192, u_segs=28, v_segs=14, name=f"{name}_choco", material="item_candy_white").translate(+0.01, 0.114, 0.0)
        parts.append(gradient_y_tint(choco, (0.18, 0.035, 0.020), (0.36, 0.075, 0.042), noise_amp=0.02))
        parts.append(ellipsoid(0.056, 0.028, 0.062, u_segs=12, v_segs=8, name=f"{name}_tail_nub", material="item_cheese").translate(-0.25, 0.072, +0.01))
        for k, (ang_d, z_off, ln) in enumerate(((-22.0, -0.055, 0.14), (0.0, 0.0, 0.16), (+24.0, +0.055, 0.14), (+45.0, +0.095, 0.11))):
            bone = cylinder(0.010, 0.014, ln, radial_segs=10, bevel=0.003, axis="x", name=f"{name}_rib_{k}", material="item_fish_green").rotate_xyz(ry=ang_d, degrees=True).translate(+0.23, 0.064, z_off)
            parts.append(gradient_y_tint(bone, (0.36, 0.78, 0.18), (0.68, 0.96, 0.34)))
        stalk = cylinder(0.018, 0.022, 0.085, radial_segs=12, axis="x", name=f"{name}_stalk", material="item_chocolate").translate(+0.23, 0.118, -0.042)
        eyeball = sphere(0.062, u_segs=18, v_segs=14, name=f"{name}_pop_eye", material="item_egg_white").translate(+0.305, 0.132, -0.042)
        iris_ring = tint_part(sphere(0.046, u_segs=16, v_segs=12, name=f"{name}_pop_iris", material="item_candy_white").translate(+0.310, 0.158, -0.018), _const_tint((0.88, 0.44, 0.14)))
        pupil = sphere(0.026, u_segs=14, v_segs=10, name=f"{name}_pop_pupil", material="eye_pupil").translate(+0.312, 0.178, -0.004)
        parts.extend([gradient_y_tint(stalk, (0.68, 0.42, 0.30), (1.00, 0.72, 0.56)), eyeball, iris_ring, pupil])

    elif stage == 4:
        parts.append(_fish_wrapper_sleeve(f"{name}_wrap", empty=True))
        for k, (cx, cz, rx, rz) in enumerate((
            (-0.145, -0.04, 0.062, 0.095),
            (-0.155, +0.07, 0.055, 0.075),
            (+0.148, -0.05, 0.064, 0.092),
            (+0.158, +0.06, 0.058, 0.082),
            (0.000, -0.175, 0.110, 0.046),
            (0.010, +0.172, 0.096, 0.044),
        )):
            smear = ellipsoid(rx, 0.026, rz, u_segs=14, v_segs=10, name=f"{name}_smear_{k}", material="item_chocolate").translate(cx, 0.024, cz)
            parts.append(gradient_y_tint(smear, (0.68, 0.42, 0.30), (1.00, 0.72, 0.56), noise_amp=0.03))
    else:
        parts.append(_fish_wrapper_sleeve(f"{name}_wrap", empty=True))

    sc.add("choco_fishy", parts)
    return _finalize_item(sc, target_span=0.96 if stage <= 2 else (0.82 if stage == 3 else 0.66))


def build_fdfish1() -> SceneBuilder:
    return _build_choco_fishy("item_fdfish1", 1)


def build_fdfish2() -> SceneBuilder:
    return _build_choco_fishy("item_fdfish2", 2)


def build_fdfish3() -> SceneBuilder:
    return _build_choco_fishy("item_fdfish3", 3)


def build_fdfish4() -> SceneBuilder:
    return _build_choco_fishy("item_fdfish4", 4)


def build_fdfish5() -> SceneBuilder:
    return _build_choco_fishy("item_fdfish5", 5)
