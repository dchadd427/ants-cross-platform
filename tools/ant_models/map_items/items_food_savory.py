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


def _star_ribbon(
    pts: Sequence[Tuple[float, float, float]],
    radii: Sequence[float],
    *,
    flutes: int = 6,
    flute_depth: float = 0.22,
    samples_per_seg: int = 5,
    radial_segs: int = 18,
    name: str,
    material: str,
) -> MeshPart:
    """Extrude a fluted star-tip pastry/squeeze ribbon along a 3D spline (authentic Steak Whiz / Cheese Whiz)."""
    part = curved_tube(
        list(pts),
        list(radii),
        samples_per_seg=samples_per_seg,
        radial_segs=radial_segs,
        cap_rings=3,
        name=name,
        material=material,
    )
    # Apply crisp longitudinal star-nozzle ridges & slight ribbon flattening matching `378..381:fdsteak1..4`!
    if part.uvs is not None and len(part.uvs) == len(part.positions):
        pos = part.positions.astype(np.float64)
        nrm = part.normals.astype(np.float64)
        u = part.uvs[:, 0].astype(np.float64)
        v = part.uvs[:, 1].astype(np.float64)
        end_fade = np.sin(np.clip(v, 0.0, 1.0) * math.pi) ** 0.45
        ridge = np.cos(u * 2.0 * math.pi * flutes) * flute_depth * end_fade
        r_est = float(np.mean(radii))
        pos = pos + nrm * (ridge[:, None] * r_est)
        # Flatten ribbon into a broad flat squeezed star-paste strip rather than a round sausage!
        y_mid = float(np.median(pos[:, 1]))
        pos[:, 1] = y_mid * 0.72 + (pos[:, 1] - y_mid) * 0.58
        pos[:, 1] = np.maximum(pos[:, 1], 0.002)
        part.positions = pos.astype(np.float32)
        part.normals = recompute_smooth_normals(part.positions, part.indices)
    return part


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


def _pepperoni_disc(name: str, cx: float, cy: float, cz: float, radius: float = 0.068, tilt_rx: float = 0.0, tilt_rz: float = 0.0) -> MeshPart:
    disc = cylinder(radius, radius * 0.96, 0.022, radial_segs=20, bevel=0.006, axis="y", name=name, material="item_pizza_sauce")
    disc = tint_part(
        disc,
        lambda pos, _n: np.clip(
            np.column_stack([
                0.86 + 0.10 * np.sin(pos[:, 0] * 70.0) * np.cos(pos[:, 2] * 70.0),
                0.10 + 0.14 * np.clip(np.sin(pos[:, 0] * 110.0 + pos[:, 2] * 90.0), 0.0, 1.0) ** 3,
                0.08 + 0.08 * np.clip(np.cos(pos[:, 0] * 95.0 - pos[:, 2] * 85.0), 0.0, 1.0) ** 3,
            ]),
            0.06,
            1.0,
        ),
    )
    return disc.rotate_xyz(rx=tilt_rx, rz=tilt_rz, degrees=True).translate(cx, cy, cz)


def _build_squeezed_pizza(name: str, stage: int) -> SceneBuilder:
    """Coiled squeezed tube-pizza paste pile with crust rim, tomato sauce, melted cheese & pepperoni (`390..393`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    if stage == 1:
        # Full circular coiled dough paste rim wrapping bottom-left -> bottom -> right -> upper-right!
        crust_pts = [
            (-0.27, 0.044, -0.06),
            (-0.29, 0.042, +0.10),
            (-0.18, 0.044, +0.26),
            (+0.02, 0.046, +0.30),
            (+0.22, 0.048, +0.20),
            (+0.28, 0.046, +0.02),
            (+0.20, 0.046, -0.18),
            (+0.02, 0.048, -0.25),
            (-0.14, 0.056, -0.21),
        ]
        crust = curved_tube(crust_pts, [0.052, 0.062, 0.066, 0.068, 0.066, 0.064, 0.060, 0.054, 0.044], samples_per_seg=4, radial_segs=14, cap_rings=3, name=f"{name}_crust", material="item_bun_crumb")
        parts.append(gradient_y_tint(crust, (0.86, 0.78, 0.56), (0.99, 0.96, 0.80), noise_amp=0.03))

        # Thick glossy crimson tomato sauce swirl tucked inside the crust coil
        sauce = ellipsoid(0.245, 0.054, 0.240, u_segs=26, v_segs=14, name=f"{name}_sauce", material="item_pizza_sauce").translate(+0.01, 0.046, +0.02)
        ps = sauce.positions.astype(np.float64)
        ang = np.arctan2(ps[:, 2], ps[:, 0])
        ps[:, 0] *= 1.0 + 0.08 * np.sin(ang * 3.0 + 0.6)
        ps[:, 2] *= 1.0 + 0.08 * np.cos(ang * 2.0 - 0.3)
        ps[:, 1] = np.maximum(ps[:, 1], 0.008)
        sauce.positions = ps.astype(np.float32)
        sauce.normals = recompute_smooth_normals(sauce.positions, sauce.indices)
        parts.append(sauce)

        # Melted golden-yellow cheese swirl + soft squeezed swirl peak pointing upper-left!
        cheese = ellipsoid(0.185, 0.062, 0.195, u_segs=24, v_segs=14, name=f"{name}_cheese", material="item_cheese").translate(-0.04, 0.072, +0.01)
        pc = cheese.positions.astype(np.float64)
        angc = np.arctan2(pc[:, 2], pc[:, 0])
        pc[:, 0] *= 1.0 + 0.14 * np.sin(angc * 3.0 - 0.5)
        pc[:, 1] += 0.018 * np.exp(-(((pc[:, 0] + 0.12) ** 2 + (pc[:, 2] + 0.12) ** 2) / 0.018))
        cheese.positions = pc.astype(np.float32)
        cheese.normals = recompute_smooth_normals(cheese.positions, cheese.indices)
        parts.append(gradient_y_tint(cheese, (0.92, 0.74, 0.16), (1.00, 0.95, 0.36), noise_amp=0.03))

        # Short blended cheese-and-dough squeeze peak curling up-left (`390:fdpizz1`)
        peak = curved_tube(
            [(-0.05, 0.095, -0.05), (-0.11, 0.112, -0.12), (-0.15, 0.118, -0.17)],
            [0.060, 0.038, 0.016],
            samples_per_seg=4,
            radial_segs=14,
            cap_rings=3,
            name=f"{name}_peak",
            material="item_cheese",
        )
        parts.append(gradient_y_tint(peak, (0.94, 0.82, 0.42), (0.99, 0.95, 0.62)))

        for k, (px, py, pz, rx, rz) in enumerate((
            (-0.01, 0.128, -0.11, -12.0, -8.0),
            (+0.11, 0.112, -0.04, -6.0, -16.0),
            (+0.01, 0.124, +0.04, 4.0, -4.0),
            (-0.08, 0.114, +0.12, 12.0, 10.0),
            (+0.13, 0.098, +0.11, 10.0, -14.0),
        )):
            parts.append(_pepperoni_disc(f"{name}_pep_{k}", px, py, pz, radius=0.056, tilt_rx=rx, tilt_rz=rz))

    elif stage == 2:
        # Nibbled outer crust rim, intact melted cheese + sauce bed + 4 pepperonis + small bitten crumbs
        crust_pts = [
            (-0.22, 0.040, +0.10),
            (-0.14, 0.042, +0.23),
            (+0.05, 0.044, +0.26),
            (+0.22, 0.044, +0.16),
            (+0.24, 0.042, -0.02),
            (+0.15, 0.042, -0.18),
            (-0.02, 0.046, -0.22),
        ]
        crust = curved_tube(crust_pts, [0.042, 0.048, 0.052, 0.048, 0.046, 0.042, 0.036], samples_per_seg=4, radial_segs=12, cap_rings=2, name=f"{name}_crust", material="item_bun_crumb")
        parts.append(gradient_y_tint(crust, (0.86, 0.78, 0.56), (0.99, 0.96, 0.80), noise_amp=0.04))
        sauce = ellipsoid(0.225, 0.052, 0.215, u_segs=24, v_segs=12, name=f"{name}_sauce", material="item_pizza_sauce").translate(+0.02, 0.044, +0.02)
        parts.append(sauce)
        cheese = ellipsoid(0.175, 0.060, 0.185, u_segs=22, v_segs=12, name=f"{name}_cheese", material="item_cheese").translate(-0.04, 0.070, +0.01)
        parts.append(gradient_y_tint(cheese, (0.92, 0.74, 0.16), (1.00, 0.95, 0.36), noise_amp=0.03))
        peak = curved_tube([(-0.05, 0.092, -0.06), (-0.11, 0.110, -0.13), (-0.15, 0.114, -0.17)], [0.056, 0.036, 0.015], samples_per_seg=4, radial_segs=12, cap_rings=2, name=f"{name}_peak", material="item_cheese")
        parts.append(gradient_y_tint(peak, (0.94, 0.82, 0.42), (0.99, 0.95, 0.62)))
        for k, (px, py, pz, rx, rz) in enumerate((
            (0.00, 0.124, -0.10, -10.0, -6.0),
            (+0.12, 0.106, -0.02, -4.0, -14.0),
            (-0.01, 0.120, +0.05, 5.0, -2.0),
            (-0.09, 0.108, +0.13, 12.0, 10.0),
        )):
            parts.append(_pepperoni_disc(f"{name}_pep_{k}", px, py, pz, radius=0.055, tilt_rx=rx, tilt_rz=rz))
        parts.append(ellipsoid(0.036, 0.024, 0.028, u_segs=10, v_segs=8, name=f"{name}_crumb0", material="item_pizza_dough").translate(-0.27, 0.020, -0.04))

    elif stage == 3:
        # Half-eaten cheese & sauce wedge on left + scattered crust/sauce crumbs on right (`392:fdpizz3`)
        sauce = ellipsoid(0.165, 0.048, 0.195, u_segs=20, v_segs=12, name=f"{name}_sauce", material="item_pizza_sauce").translate(-0.04, 0.042, +0.02)
        parts.append(sauce)
        cheese = ellipsoid(0.145, 0.058, 0.175, u_segs=20, v_segs=12, name=f"{name}_cheese", material="item_cheese").translate(-0.06, 0.066, +0.02)
        parts.append(gradient_y_tint(cheese, (0.90, 0.68, 0.12), (1.00, 0.90, 0.28), noise_amp=0.05))
        peak = curved_tube([(-0.06, 0.090, -0.06), (-0.11, 0.106, -0.12), (-0.15, 0.110, -0.16)], [0.050, 0.032, 0.014], samples_per_seg=4, radial_segs=12, cap_rings=2, name=f"{name}_peak", material="item_cheese")
        parts.append(gradient_y_tint(peak, (0.94, 0.82, 0.42), (0.99, 0.95, 0.62)))
        for k, (px, py, pz) in enumerate(((-0.02, 0.118, -0.10), (+0.07, 0.102, -0.02), (-0.02, 0.114, +0.05), (-0.09, 0.102, +0.13))):
            parts.append(_pepperoni_disc(f"{name}_pep_{k}", px, py, pz, radius=0.050))
        for k, (cx, cz, rx, rz, mat) in enumerate((
            (+0.18, -0.10, 0.040, 0.032, "item_pizza_dough"),
            (+0.22, +0.04, 0.044, 0.034, "item_pizza_sauce"),
            (+0.16, +0.14, 0.036, 0.028, "item_pizza_sauce"),
            (+0.12, +0.23, 0.042, 0.030, "item_pizza_dough"),
            (+0.02, +0.24, 0.034, 0.028, "item_cheese"),
        )):
            parts.append(ellipsoid(rx, 0.026, rz, u_segs=10, v_segs=8, name=f"{name}_crumb_{k}", material=mat).translate(cx, 0.024, cz))

    else:
        # Stage 4: Tiny upper-left squeeze peak + 1 pepperoni remnant + bottom-center & right sauce crumbs (`393:fdpizz4`)
        rem = ellipsoid(0.115, 0.046, 0.088, u_segs=16, v_segs=10, name=f"{name}_rem", material="item_cheese").translate(-0.06, 0.042, -0.09)
        parts.append(gradient_y_tint(rem, (0.86, 0.66, 0.24), (0.99, 0.88, 0.36)))
        peak = curved_tube([(-0.09, 0.066, -0.08), (-0.15, 0.098, -0.15), (-0.19, 0.114, -0.20)], [0.046, 0.032, 0.015], samples_per_seg=3, radial_segs=10, cap_rings=2, name=f"{name}_peak", material="item_pizza_dough")
        parts.append(peak)
        parts.append(_pepperoni_disc(f"{name}_pep_0", +0.05, 0.072, -0.07, radius=0.056))
        for k, (cx, cz, rx, rz, mat) in enumerate((
            (+0.16, -0.02, 0.042, 0.032, "item_pizza_sauce"),
            (-0.02, +0.16, 0.056, 0.046, "item_pizza_sauce"),
            (+0.08, +0.11, 0.032, 0.026, "item_pizza_dough"),
            (+0.10, +0.20, 0.028, 0.024, "item_pizza_sauce"),
        )):
            parts.append(ellipsoid(rx, 0.026, rz, u_segs=10, v_segs=8, name=f"{name}_crumb_{k}", material=mat).translate(cx, 0.024, cz))

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
    """Squeezed star-tip extruded Steak Whiz brown meat-paste ribbon stages (`378..381:fdsteak1..4`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    if stage == 1:
        # Iconic wide star-fluted Z/U zigzag paste ribbon with small center gap (`378:fdsteak1`)
        pts = [
            (+0.22, 0.062, -0.17),
            (+0.02, 0.072, -0.13),
            (-0.22, 0.066, -0.09),
            (-0.27, 0.058, +0.05),
            (-0.14, 0.062, +0.16),
            (+0.06, 0.068, +0.17),
            (+0.26, 0.062, +0.07),
            (+0.16, 0.074, -0.01),
            (-0.04, 0.072, -0.02),
        ]
        rads = [0.058, 0.068, 0.068, 0.064, 0.066, 0.068, 0.064, 0.058, 0.046]
        parts.append(_star_ribbon(pts, rads, flutes=6, flute_depth=0.34, name=f"{name}_coil", material="item_steak_paste"))
    elif stage == 2:
        pts = [
            (+0.22, 0.058, -0.13),
            (+0.02, 0.066, -0.11),
            (-0.22, 0.062, -0.07),
            (-0.26, 0.056, +0.05),
            (-0.13, 0.062, +0.16),
            (+0.08, 0.068, +0.15),
            (+0.26, 0.058, +0.04),
            (+0.14, 0.070, -0.02),
        ]
        rads = [0.052, 0.064, 0.064, 0.060, 0.062, 0.064, 0.058, 0.046]
        parts.append(_star_ribbon(pts, rads, flutes=6, flute_depth=0.34, name=f"{name}_coil", material="item_steak_paste"))
    elif stage == 3:
        pts = [
            (-0.04, 0.058, -0.11),
            (-0.23, 0.056, -0.08),
            (-0.26, 0.054, +0.04),
            (-0.14, 0.062, +0.15),
            (+0.08, 0.066, +0.14),
            (+0.26, 0.056, +0.02),
            (+0.14, 0.058, -0.06),
        ]
        rads = [0.046, 0.058, 0.060, 0.062, 0.062, 0.054, 0.044]
        parts.append(_star_ribbon(pts, rads, flutes=6, flute_depth=0.34, name=f"{name}_coil", material="item_steak_paste"))
    else:
        pts = [
            (-0.24, 0.058, +0.08),
            (-0.08, 0.066, +0.02),
            (+0.10, 0.068, -0.03),
            (+0.24, 0.060, -0.08),
        ]
        rads = [0.058, 0.076, 0.076, 0.056]
        parts.append(_star_ribbon(pts, rads, flutes=6, flute_depth=0.34, name=f"{name}_ribbon", material="item_steak_paste"))

    # Authentic savory tan-brown roasted steak-paste palette matching `378..381`!
    parts = [gradient_y_tint(p, (0.72, 0.56, 0.44), (1.00, 0.90, 0.78), noise_amp=0.04) for p in parts]
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

def _egg_yolk(name: str, cx: float, cz: float, r_xz: float, h_y: float, y_base: float) -> MeshPart:
    yolk = ellipsoid(r_xz, h_y, r_xz * 0.96, u_segs=26, v_segs=16, name=name, material="item_egg_yolk")
    p = yolk.positions.astype(np.float64)
    # Dome top smoothly and flatten base onto the egg white bed
    below = p[:, 1] < 0.0
    p[below, 1] *= 0.25
    p[:, 1] += y_base
    p[:, 0] += cx
    p[:, 2] += cz
    yolk.positions = p.astype(np.float32)
    yolk.normals = recompute_smooth_normals(yolk.positions, yolk.indices)
    return gradient_y_tint(yolk, (0.94, 0.54, 0.02), (1.00, 0.88, 0.14), noise_amp=0.015)


def _egg_white_sheet(name: str, stage: int) -> MeshPart:
    """Organic scalloped sunny-side-up egg white sheet with authentic ant-bite notches for stages 1..3."""
    n_rings, n_ang = 18, 64
    verts, uvs, tris = [], [], []

    def _boundary_r(ang: float) -> Tuple[float, float]:
        # Elongated kidney/oval albumen surrounding left (-0.15, -0.02) and right (+0.17, -0.01) yolks
        # Base organic shape: slightly fuller under left yolk (+Z lobe on lower-left)
        rx = 0.42 + 0.03 * math.cos(2.0 * ang - 0.5)
        rz = 0.28 + 0.05 * math.sin(ang + 0.4) - 0.03 * math.sin(2.0 * ang)
        # Subtle natural frying-pan frill waves
        frill = 1.0 + 0.045 * math.sin(5.0 * ang + 0.3) + 0.025 * math.cos(9.0 * ang)
        if stage == 2:
            # 4 crisp scalloped bite notches along outer edge (`386:fdeggs2`)
            for ba, bw, bd in ((-0.45, 0.28, 0.18), (+0.55, 0.32, 0.16), (+2.15, 0.30, 0.15), (-2.30, 0.26, 0.14)):
                da = math.atan2(math.sin(ang - ba), math.cos(ang - ba))
                frill -= bd * math.exp(-((da / bw) ** 2))
        elif stage == 3:
            # Heavily eaten — narrow ragged white halo + leftover lower-left tattered apron (`387:fdeggs3`)
            frill *= 0.72 + 0.18 * math.exp(-(((math.atan2(math.sin(ang - 2.35), math.cos(ang - 2.35))) / 0.55) ** 2))
            for ba, bw, bd in ((-0.35, 0.36, 0.22), (+0.75, 0.42, 0.24), (+1.55, 0.34, 0.20), (-1.55, 0.38, 0.22), (+2.95, 0.28, 0.18)):
                da = math.atan2(math.sin(ang - ba), math.cos(ang - ba))
                frill -= bd * math.exp(-((da / bw) ** 2))
        return rx * max(0.36, frill), rz * max(0.36, frill)

    # Top and bottom surfaces joined cleanly at the crispy fried rim
    # Build as a closed heightfield disc (top rings 0..n_rings, rim, bottom rings n_rings..0)
    def _pt_xy(rf: float, ang: float) -> Tuple[float, float]:
        rx, rz = _boundary_r(ang)
        x = rf * rx * math.cos(ang) - 0.01
        z = rf * rz * math.sin(ang) + 0.03 * (1.0 - rf * 0.5)
        return x, z

    def _top_y(x: float, z: float, rf: float) -> float:
        # Soft pillowy white ring swelling around both yolks + thin delicate crispy outer edge!
        d1 = math.hypot(x + 0.12, z + 0.02)
        d2 = math.hypot(x - 0.16, z + 0.01)
        thick_white = 0.044 * math.exp(-((d1 / 0.18) ** 2)) + 0.038 * math.exp(-((d2 / 0.16) ** 2))
        base = 0.022 * ((1.0 - rf ** 2.6) ** 0.55) + 0.008
        rim_curl = 0.007 * math.exp(-(((rf - 0.94) / 0.08) ** 2))
        return base + thick_white * (1.0 - rf ** 4) + rim_curl

    # Top surface rings (r=0..1) then bottom surface rings (r=1..0)
    ring_specs = [(ir / float(n_rings), True) for ir in range(n_rings + 1)] + [(1.0 - ir / float(n_rings), False) for ir in range(n_rings + 1)]
    for rf, is_top in ring_specs:
        for j in range(n_ang + 1):
            ang = 2.0 * math.pi * j / float(n_ang)
            x, z = _pt_xy(rf, ang)
            y = _top_y(x, z, rf) if is_top else (0.006 if rf > 0.96 else 0.001)
            verts.append((x, y, z))
            uvs.append((0.5 + x, 0.5 + z))
    stride = n_ang + 1
    for ir in range(len(ring_specs) - 1):
        for j in range(n_ang):
            a, b, c, d = ir * stride + j, (ir + 1) * stride + j, (ir + 1) * stride + (j + 1), ir * stride + (j + 1)
            tris.extend(((a, c, b), (a, d, c)))
    pos = np.asarray(verts, dtype=np.float32)
    idx = np.asarray(tris, dtype=np.uint32).reshape(-1)
    sheet = MeshPart(name, "item_egg_white", pos, recompute_smooth_normals(pos, idx), idx, uvs=np.asarray(uvs, dtype=np.float32))
    # Subtle golden-tan crisped frying-pan edge + snowy glossy white interior!
    return tint_part(
        sheet,
        lambda p, _n: np.clip(
            np.column_stack([
                0.98 - 0.06 * np.clip((np.hypot(p[:, 0] / 0.42, p[:, 2] / 0.28) - 0.80) / 0.24, 0.0, 1.0),
                0.97 - 0.14 * np.clip((np.hypot(p[:, 0] / 0.42, p[:, 2] / 0.28) - 0.80) / 0.24, 0.0, 1.0),
                0.95 - 0.24 * np.clip((np.hypot(p[:, 0] / 0.42, p[:, 2] / 0.28) - 0.80) / 0.24, 0.0, 1.0),
            ]),
            0.20,
            1.0,
        ),
    )


def _build_fried_eggs(name: str, stage: int) -> SceneBuilder:
    """Twin sunny-side-up fried eggs progressively eaten from full albumen down to 2 bare yolks (`385..388`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    if stage <= 3:
        parts.append(_egg_white_sheet(f"{name}_white", stage))
        y_base = 0.046
        parts.append(_egg_yolk(f"{name}_yolk_l", -0.105, -0.022, 0.136, 0.098, y_base))
        parts.append(_egg_yolk(f"{name}_yolk_r", +0.150, -0.014, 0.114, 0.084, y_base - 0.004))
    else:
        # Stage 4 (`388:fdeggs4`): two bare gleaming domed golden egg yolks side-by-side!
        parts.append(_egg_yolk(f"{name}_yolk_l", -0.155, 0.0, 0.145, 0.110, 0.012))
        parts.append(_egg_yolk(f"{name}_yolk_r", +0.165, -0.008, 0.118, 0.092, 0.012))

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


def _fish_tail_fin(name: str, *, nibbled: bool = False) -> List[MeshPart]:
    """Forked yellow-and-brown striped fish tail fin protruding LEFT (`-X`) from chocolate dip."""
    parts: List[MeshPart] = []
    # Upper & lower swept tail lobes
    scale = 0.72 if nibbled else 1.0
    for sgn, tag in ((-1.0, "top"), (+1.0, "bot")):
        lobe = curved_tube(
            [
                (-0.22, 0.102, sgn * 0.030),
                (-0.34 * scale - 0.05, 0.092, sgn * 0.088 * scale),
                (-0.46 * scale - 0.04, 0.072, sgn * 0.135 * scale),
            ],
            [0.048, 0.038 * scale, 0.016],
            samples_per_seg=4,
            radial_segs=12,
            cap_rings=2,
            name=f"{name}_{tag}",
            material="item_cheese",
        )
        # Flatten Y slightly into a real fin blade & paint yellow/dark-brown stripes!
        p = lobe.positions.astype(np.float64)
        p[:, 1] = 0.086 + (p[:, 1] - 0.086) * 0.45
        lobe.positions = p.astype(np.float32)
        lobe.normals = recompute_smooth_normals(lobe.positions, lobe.indices)
        lobe = tint_part(
            lobe,
            lambda pos, _n: np.clip(
                np.where(
                    (np.sin(pos[:, 2] * 58.0) > 0.0)[:, None],
                    np.array([[0.98, 0.86, 0.20]]),
                    np.array([[0.46, 0.30, 0.12]]),
                ),
                0.10,
                1.0,
            ),
        )
        parts.append(lobe)
    # Center tail webbing connecting upper & lower lobes
    web = ellipsoid(0.11 * scale, 0.022, 0.075 * scale, u_segs=14, v_segs=8, name=f"{name}_web", material="item_cheese").translate(-0.32 * scale - 0.05, 0.084, 0.0)
    parts.append(tint_part(web, _const_tint((0.88, 0.74, 0.22))))
    return parts


def _build_choco_fishy(name: str, stage: int) -> SceneBuilder:
    """'CHOCO-FISHY SNAX' chocolate-dipped scaly green fish through 5 authentic stages (`394..398`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)
    parts: List[MeshPart] = []

    if stage in (1, 2):
        # Full or slightly bitten green scaly candy fish dipped in glossy brown chocolate + paper sleeve!
        parts.append(_fish_wrapper_sleeve(f"{name}_wrap", empty=False))

        # Thick glossy milk-chocolate dip protruding out both left & right edges of paper wrapper!
        choco = ellipsoid(0.246, 0.096, 0.164, u_segs=26, v_segs=14, name=f"{name}_choco", material="item_steak_paste").translate(0.0, 0.114, 0.0)
        pc = choco.positions.astype(np.float64)
        # Organic wavy melted chocolate drip scallops at left & right edges
        drip = np.sin(pc[:, 2] * 26.0 + pc[:, 1] * 14.0) * 0.018
        pc[:, 0] += np.sign(pc[:, 0]) * drip * np.clip(np.abs(pc[:, 0]) / 0.18, 0.0, 1.0)
        choco.positions = pc.astype(np.float32)
        choco.normals = recompute_smooth_normals(choco.positions, choco.indices)
        parts.append(gradient_y_tint(choco, (0.58, 0.24, 0.14), (0.88, 0.42, 0.26), noise_amp=0.03))

        # Striped yellow fish tail on LEFT
        parts.extend(_fish_tail_fin(f"{name}_tail", nibbled=(stage == 2)))

        # Scaly bright-green cartoon fish head protruding RIGHT (`+X`)
        head_x = 0.24 if stage == 1 else 0.21
        head_len = 0.215 if stage == 1 else 0.175
        head = ellipsoid(head_len, 0.095, 0.148, u_segs=26, v_segs=16, name=f"{name}_head", material="item_fish_green").translate(head_x, 0.106, 0.0)
        ph = head.positions.astype(np.float64)
        # Taper snout cleanly toward +X into classic fish profile
        t_snout = np.clip((ph[:, 0] - 0.16) / max(0.08, head_len * 1.4), 0.0, 1.0)
        ph[:, 2] *= 1.0 - 0.56 * (t_snout ** 1.35)
        ph[:, 1] = 0.102 + (ph[:, 1] - 0.102) * (1.0 - 0.42 * (t_snout ** 1.4))
        if stage == 2:
            # Jagged bite notch out of the nose tip!
            nose = ph[:, 0] > 0.35
            ph[nose, 0] = 0.35 + 0.012 * np.sin(ph[nose, 2] * 45.0)
        head.positions = ph.astype(np.float32)
        head.normals = recompute_smooth_normals(head.positions, head.indices)
        parts.append(gradient_y_tint(head, (0.18, 0.54, 0.14), (0.58, 0.94, 0.26), noise_amp=0.05))

        # Big expressive cartoon fish eye (yellow iris ring + white sclera + dark pupil) on top-right of head!
        eye_x = 0.305 if stage == 1 else 0.285
        iris = sphere(0.036, u_segs=16, v_segs=12, name=f"{name}_eye_iris", material="item_cheese").translate(eye_x, 0.176, -0.022)
        pupil = sphere(0.022, u_segs=14, v_segs=10, name=f"{name}_eye_pupil", material="item_soil_hole").translate(eye_x + 0.008, 0.194, -0.022)
        parts.extend([iris, pupil])

        # Striped yellow-gold pectoral fin sweeping down-front (+Z) from behind the gill!
        pect = curved_tube(
            [(+0.21, 0.088, +0.105), (+0.16, 0.068, +0.188), (+0.11, 0.044, +0.232)],
            [0.048, 0.042, 0.020],
            samples_per_seg=4,
            radial_segs=12,
            cap_rings=2,
            name=f"{name}_pect_fin",
            material="item_cheese",
        )
        parts.append(tint_part(pect, _const_tint((0.96, 0.84, 0.22))))

        # Gill crease accent behind head
        gill = torus_arc(0.075, 0.010, start_ang=-0.9, end_ang=+0.9, u_segs=14, v_segs=6, plane="xz", name=f"{name}_gill", material="item_toy_green_dark").translate(+0.22, 0.168, 0.0)
        parts.append(gill)

        if stage == 2:
            # Tiny detached chewed green fish nose crumb floating right (`395:fdfish2`)
            parts.append(ellipsoid(0.030, 0.020, 0.024, u_segs=10, v_segs=8, name=f"{name}_nose_crumb", material="item_fish_green").translate(+0.43, 0.032, +0.02))

    elif stage == 3:
        # Half-eaten comic fish: chocolate-smeared wrapper + popped eyeball & green fish bones on right (`396:fdfish3`)!
        parts.append(_fish_wrapper_sleeve(f"{name}_wrap", empty=False))
        choco = ellipsoid(0.242, 0.092, 0.160, u_segs=24, v_segs=12, name=f"{name}_choco", material="item_steak_paste").translate(+0.01, 0.110, 0.0)
        parts.append(gradient_y_tint(choco, (0.58, 0.24, 0.14), (0.88, 0.42, 0.26), noise_amp=0.03))
        # Tiny chewed tail nub on left
        parts.append(ellipsoid(0.052, 0.026, 0.056, u_segs=12, v_segs=8, name=f"{name}_tail_nub", material="item_cheese").translate(-0.24, 0.068, +0.01))
        # Chewed green fish rib-bones & lower jaw scrap on right
        for k, (ang_d, z_off, ln) in enumerate(((-22.0, -0.055, 0.14), (0.0, 0.0, 0.16), (+24.0, +0.055, 0.14), (+45.0, +0.095, 0.11))):
            bone = cylinder(0.010, 0.014, ln, radial_segs=10, bevel=0.003, axis="x", name=f"{name}_rib_{k}", material="item_fish_green").rotate_xyz(ry=ang_d, degrees=True).translate(+0.23, 0.062, z_off)
            parts.append(gradient_y_tint(bone, (0.36, 0.78, 0.18), (0.68, 0.96, 0.34)))
        # Comic popped round fish eyeball facing UP-FRONT toward camera (`396:fdfish3`)!
        stalk = cylinder(0.018, 0.022, 0.085, radial_segs=12, axis="x", name=f"{name}_stalk", material="item_steak_paste").translate(+0.23, 0.118, -0.042)
        eyeball = sphere(0.062, u_segs=18, v_segs=14, name=f"{name}_pop_eye", material="item_egg_white").translate(+0.305, 0.132, -0.042)
        iris_ring = sphere(0.046, u_segs=16, v_segs=12, name=f"{name}_pop_iris", material="item_cheese").translate(+0.310, 0.158, -0.018)
        pupil = sphere(0.026, u_segs=14, v_segs=10, name=f"{name}_pop_pupil", material="item_soil_hole").translate(+0.312, 0.178, -0.004)
        parts.extend([gradient_y_tint(stalk, (0.58, 0.24, 0.14), (0.86, 0.40, 0.24)), eyeball, iris_ring, pupil])

    elif stage == 4:
        # Chocolate-smeared empty 'CHOCO-FISHY SNAX' wrapper (`397:fdfish4`)
        parts.append(_fish_wrapper_sleeve(f"{name}_wrap", empty=True))
        # Melted glossy milk-chocolate smears oozing out left, right & underside of wrapper!
        for k, (cx, cz, rx, rz) in enumerate((
            (-0.145, -0.04, 0.062, 0.095),
            (-0.155, +0.07, 0.055, 0.075),
            (+0.148, -0.05, 0.064, 0.092),
            (+0.158, +0.06, 0.058, 0.082),
            (0.000, -0.175, 0.110, 0.046),
            (0.010, +0.172, 0.096, 0.044),
        )):
            smear = ellipsoid(rx, 0.026, rz, u_segs=14, v_segs=10, name=f"{name}_smear_{k}", material="item_steak_paste").translate(cx, 0.024, cz)
            parts.append(gradient_y_tint(smear, (0.58, 0.24, 0.14), (0.88, 0.42, 0.26), noise_amp=0.03))

    else:
        # Clean empty slightly curled 'CHOCO-FISHY SNAX' paper wrapper (`398:fdfish5`)
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
