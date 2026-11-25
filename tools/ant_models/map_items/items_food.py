"""Realistic 3D sculptors for 1998 Ants food items & progressive eaten stages (Wave 1).

Covers:
- `fdburgr1`..`fdburgr3` (253–255): Sesame-seed cheeseburger (full -> half-eaten -> veggie remnants)
- `fdcornd1` (361): Golden cornmeal-crusted corn dog on wood stick with zigzag yellow mustard
- `fdjelo1`  (366): Translucent ruby-red fluted Jell-O dome with suspended banana slices + gentle wobble clip
- `fdchick1` (399): Crispy golden-brown fried chicken drumstick with exposed knuckle bone
- `fdsuckr1` (256): Glossy red-and-white swirled peppermint lollipop on curved paper stick
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
            AnimationChannel,
            MeshPart,
            SceneBuilder,
            curved_tube,
            cylinder,
            ellipsoid,
            ensure_outward_winding,
            lathe,
            quat_from_euler_xyz,
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
            AnimationChannel,
            MeshPart,
            SceneBuilder,
            curved_tube,
            cylinder,
            ellipsoid,
            ensure_outward_winding,
            lathe,
            quat_from_euler_xyz,
            recompute_smooth_normals,
            rounded_box,
            sphere,
            superellipsoid,
            torus_arc,
        )


def _carve_bite(pos: np.ndarray, bite_centers: List[Tuple[float, float, float]]) -> np.ndarray:
    """Push vertices smoothly inward around circular tooth-bite scoops to sculpt eaten food stages."""
    p = pos.copy()
    for bx, bz, br in bite_centers:
        dx = p[:, 0] - bx
        dz = p[:, 2] - bz
        dist = np.hypot(dx, dz)
        inside = dist < br
        if np.any(inside):
            # Pull bitten vertices cleanly across the bite arc into a slightly scalloped concave edge.
            nx = dx[inside] / np.maximum(1e-5, dist[inside])
            nz = dz[inside] / np.maximum(1e-5, dist[inside])
            push = (br - dist[inside]) * 1.02
            p[inside, 0] -= nx * push * 0.25 + (bx - p[inside, 0]) * 0.0
            # Project onto the outer boundary of the bite circle toward the remaining side (-X).
            ang = np.arctan2(dz[inside], dx[inside])
            # Move point to edge of bite circle on the remaining half.
            target_x = bx - np.sqrt(np.maximum(0.0, br * br - dz[inside] * dz[inside]))
            p[inside, 0] = np.minimum(p[inside, 0], target_x + 0.010 * np.sin(ang * 5.0))
    return p


def _build_lettuce_leaf(name: str, angle_rad: float, radius: float, y_base: float, scale: float = 1.0) -> MeshPart:
    """One curly ruffled fresh green lettuce leaf projecting out from a burger or lying on the ground."""
    def _ruffle(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        r = np.hypot(p[:, 0], p[:, 2])
        a = np.arctan2(p[:, 2], p[:, 0])
        edge = np.clip(r / (0.16 * scale), 0.0, 1.0)
        p[:, 1] += (0.022 * np.sin(a * 7.0 + angle_rad) + 0.012 * np.cos(a * 11.0)) * (edge ** 1.4)
        p[:, 1] += 0.018 * (edge ** 2.0)
        return p

    leaf = (
        ellipsoid(0.165 * scale, 0.010, 0.125 * scale, u_segs=22, v_segs=8, name=name, material="item_lettuce")
        .deform(_ruffle)
        .rotate_xyz(ry=angle_rad)
        .translate(radius * math.cos(angle_rad), y_base, radius * math.sin(angle_rad))
    )
    return tint_part(
        leaf,
        lambda pos, _n: np.clip(
            np.column_stack([
                0.86 + 0.14 * np.clip(np.hypot(pos[:, 0], pos[:, 2]) / 0.28, 0.0, 1.0),
                0.92 + 0.08 * np.sin(pos[:, 0] * 24.0),
                0.76 + 0.20 * np.clip(np.hypot(pos[:, 0], pos[:, 2]) / 0.28, 0.0, 1.0),
            ]),
            0.5,
            1.0,
        ),
    )


def _build_burger(stage: int) -> SceneBuilder:
    """Build realistic cheeseburger (`fdburgr1` full, `fdburgr2` bitten, `fdburgr3` scattered veggies)."""
    register_item_materials()
    sc = SceneBuilder(name=f"item_fdburgr{stage}")

    if stage == 3:
        # Final stage (`255:fdburgr3`): torn sesame bun corner, ruffled lettuce, sliced tomato ring, crisp onion rings & beef chunks!
        # Torn upper bun wedge matching top of `255:fdburgr3`
        def _torn_wedge(pos: np.ndarray) -> np.ndarray:
            p = pos.copy()
            p[:, 1] = np.where(p[:, 1] < 0.0, p[:, 1] * 0.25, p[:, 1])
            # Jagged bitten front edge along +Z
            jagged = 0.025 * np.sin(p[:, 0] * 26.0) + 0.016 * np.cos(p[:, 0] * 48.0)
            p[:, 2] = np.minimum(p[:, 2], 0.04 + jagged)
            return p

        torn_bun = gradient_y_tint(
            superellipsoid(0.24, 0.135, 0.19, e1=0.82, e2=0.94, u_segs=28, v_segs=16, name="bun_remnant", material="item_bun_golden")
            .deform(_torn_wedge)
            .rotate_xyz(rx=-0.10, ry=-0.18)
            .translate(+0.03, 0.075, -0.14),
            (0.98, 0.90, 0.74),
            (0.84, 0.68, 0.50),
        )
        sc.add("burger", torn_bun)
        for k in range(10):
            th = -0.3 + k * 0.62
            sx = 0.03 + 0.14 * math.cos(th)
            sz = -0.17 + 0.08 * math.sin(th)
            sc.add("burger", ellipsoid(0.0085, 0.0045, 0.015, u_segs=8, v_segs=6, name=f"rem_sesame_{k}", material="item_sesame").translate(sx, 0.188 - 0.02 * abs(math.cos(th)), sz))

        for k, (ang, rad, s) in enumerate(((0.4, 0.14, 1.15), (1.7, 0.13, 1.08), (2.9, 0.14, 1.05), (4.5, 0.12, 0.96), (5.6, 0.13, 0.94))):
            sc.add("burger", _build_lettuce_leaf(f"lettuce_rem_{k}", ang, rad, 0.024 + 0.008 * (k % 2), scale=s))
        # Red tomato slice ring + inner tomato wedge on bottom-left like `255:fdburgr3`
        tom_ring = torus_arc(0.115, 0.024, u_segs=26, v_segs=10, plane="xz", name="tomato_rem_ring", material="item_tomato").rotate_xyz(rx=0.16, rz=-0.20).translate(-0.14, 0.044, +0.08)
        tom_slice = cylinder(0.095, 0.092, 0.020, radial_segs=22, bevel=0.004, axis="y", name="tomato_rem_disc", material="item_tomato").rotate_xyz(rx=-0.12, rz=0.16).translate(+0.08, 0.038, +0.02)
        sc.add("burger", [tom_ring, tom_slice])
        # Crisp layered white onion rings in foreground
        onion_a = torus_arc(0.092, 0.015, u_segs=26, v_segs=10, plane="xz", name="onion_rem_a", material="item_onion").rotate_xyz(rx=0.22, rz=-0.16).translate(+0.01, 0.062, +0.12)
        onion_b = torus_arc(0.062, 0.012, u_segs=22, v_segs=10, plane="xz", name="onion_rem_b", material="item_onion").rotate_xyz(rx=0.18, rz=0.12).translate(+0.05, 0.052, +0.14)
        sc.add("burger", [onion_a, onion_b])
        for idx, (cx, cz, cr) in enumerate(((-0.02, -0.02, 0.072), (+0.11, +0.07, 0.052), (-0.11, -0.05, 0.048))):
            scrap = (
                ellipsoid(cr, cr * 0.55, cr * 0.85, u_segs=14, v_segs=10, name=f"patty_crumb_{idx}", material="item_beef_patty")
                .deform(lambda p, k=idx: p + 0.008 * np.sin(p * 44.0 + k))
                .translate(cx, cr * 0.55 + 0.01, cz)
            )
            sc.add("burger", scrap)
        return _finalize_item(sc, target_span=0.92)

    bites = [(+0.26, -0.04, 0.24), (+0.22, +0.15, 0.19)] if stage == 2 else []

    def _maybe_bite(part: MeshPart) -> MeshPart:
        if not bites:
            return part
        def _clip_clean(pos: np.ndarray) -> np.ndarray:
            p = _carve_bite(pos, bites)
            # Clamp any overhanging corners cleanly behind the bite plane so cheese never pokes into air!
            max_x = 0.06 + 0.045 * np.sin(p[:, 2] * 9.0) + 0.06 * ((p[:, 2] / 0.30) ** 2)
            p[:, 0] = np.minimum(p[:, 0], max_x)
            return p
        return part.deform(_clip_clean)

    # 1. Toasted bottom bun heel.
    bot_bun = _maybe_bite(
        gradient_y_tint(
            superellipsoid(0.31, 0.068, 0.31, e1=0.62, e2=0.96, u_segs=32, v_segs=14, name="bun_bottom", material="item_bun_golden")
            .translate(0.0, 0.064, 0.0),
            (0.86, 0.78, 0.64),
            (1.02, 0.98, 0.90),
        )
    )
    sc.add("burger", bot_bun)

    # 2. Thick char-seared dimpled beef patty.
    patty = _maybe_bite(
        tint_part(
            superellipsoid(0.315, 0.056, 0.315, e1=0.58, e2=0.98, u_segs=34, v_segs=16, name="beef_patty", material="item_beef_patty")
            .deform(lambda p: p + 0.007 * np.sin(p * 36.0) * np.cos(np.roll(p, 1, axis=1) * 32.0))
            .translate(0.0, 0.148, 0.0),
            lambda pos, _n: np.clip(0.84 + 0.16 * np.sin(pos[:, 0] * 28.0 + pos[:, 2] * 24.0)[:, None], 0.65, 1.0) * np.ones((1, 3)),
        )
    )
    sc.add("burger", patty)

    # 3. Melted square cheddar cheese slice draping its 4 corners warmly over the patty edge!
    def _drape_cheese(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        r = np.hypot(p[:, 0], p[:, 2])
        over = np.clip((r - 0.25) / 0.14, 0.0, 1.0)
        p[:, 1] -= 0.052 * (over ** 1.6)
        return p

    cheese = _maybe_bite(
        rounded_box(0.56, 0.018, 0.56, bevel=0.006, segs=3, name="cheese_slice", material="item_cheese")
        .rotate_xyz(ry=26.0, degrees=True)
        .deform(_drape_cheese)
        .translate(0.0, 0.198, 0.0)
    )
    sc.add("burger", cheese)

    # 4. Juicy red tomato slices + crisp white onion ring peeking visibly from under the top bun (`253:fdburgr1`).
    for idx, (tx, tz) in enumerate(((-0.13, +0.19), (+0.11, +0.17), (-0.04, -0.15))):
        if stage == 2 and tx > 0.03:
            continue
        sc.add(
            "burger",
            _maybe_bite(cylinder(0.142, 0.138, 0.030, radial_segs=24, bevel=0.005, axis="y", name=f"tomato_{idx}", material="item_tomato").translate(tx, 0.216, tz)),
        )
    onion_peek = _maybe_bite(
        torus_arc(0.115, 0.016, u_segs=26, v_segs=10, plane="xz", name="burger_onion_peek", material="item_onion")
        .translate(-0.04, 0.232, +0.20)
    )
    sc.add("burger", onion_peek)

    # 5. Crown of 7 fresh ruffled green lettuce leaves fanning around the burger equator!
    for k in range(7):
        ang = 2.0 * math.pi * k / 7.0 + 0.15
        if stage == 2 and math.cos(ang) > 0.35:
            continue
        sc.add("burger", _build_lettuce_leaf(f"lettuce_{k}", ang, 0.220, 0.242 + 0.006 * (k % 2), scale=1.0))

    # 6. Plump golden-brown domed sesame-seed top bun with toasted crown gradient.
    def _dome_bun(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        p[:, 1] = np.where(p[:, 1] < 0.0, p[:, 1] * 0.22, p[:, 1])
        p[:, 1] += 0.004 * np.sin(p[:, 0] * 26.0) * np.cos(p[:, 2] * 26.0)
        return p

    top_bun = _maybe_bite(
        gradient_y_tint(
            superellipsoid(0.325, 0.175, 0.325, e1=0.84, e2=0.96, u_segs=36, v_segs=22, name="bun_top", material="item_bun_golden")
            .deform(_dome_bun)
            .translate(0.0, 0.270, 0.0),
            (0.98, 0.90, 0.74),  # pale warm lower crust
            (0.84, 0.68, 0.50),  # rich toasted golden-brown crown!
        )
    )
    sc.add("burger", top_bun)

    # 7. Crisp individual ivory sesame seeds spread evenly across the full toasted dome (sits proud on superellipsoid)!
    golden_angle = 2.39996323
    for i in range(42):
        f = (i + 0.5) / 42.0
        r_frac = 0.84 * math.sqrt(f)
        th = i * golden_angle
        sx = 0.316 * r_frac * math.cos(th)
        sz = 0.316 * r_frac * math.sin(th)
        if stage == 2 and sx > 0.02:
            continue
        # Match superellipsoid e1=0.84 surface height precisely so seeds sit right on top of the crust!
        sy = 0.270 + 0.178 * ((max(0.01, 1.0 - (r_frac ** (2.0 / 0.84)))) ** (0.84 * 0.5)) + 0.004
        seed = (
            ellipsoid(0.010, 0.0055, 0.018, u_segs=8, v_segs=6, name=f"sesame_{i}", material="item_sesame")
            .rotate_xyz(rx=0.55 * r_frac * math.sin(th), ry=th + 0.8 * i, rz=-0.55 * r_frac * math.cos(th))
            .translate(sx, sy, sz)
        )
        sc.add("burger", seed)

    # Exposed interior bread crumb & grilled beef face along the bite edge on Stage 2.
    if stage == 2:
        crumb_fill = (
            ellipsoid(0.048, 0.056, 0.145, u_segs=14, v_segs=10, name="bun_crumb_edge", material="item_bun_crumb")
            .translate(0.045, 0.315, 0.01)
        )
        sc.add("burger", crumb_fill)

    return _finalize_item(sc, target_span=0.92)


def build_fdburgr1() -> SceneBuilder:
    return _build_burger(1)


def build_fdburgr2() -> SceneBuilder:
    return _build_burger(2)


def build_fdburgr3() -> SceneBuilder:
    return _build_burger(3)


def build_fdcornd1() -> SceneBuilder:
    """Golden fried cornmeal corn dog with slanted bitten crust & a visible chomped bite in the hot dog itself (`361:fdcornd1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdcornd1")

    # Plump deep-fried golden cornmeal batter cylinder with craggy crispy bubbles + slanted open bitten front hood!
    def _fry_and_bite_crust(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        bump = (
            0.010 * np.sin(p[:, 0] * 38.0 + p[:, 2] * 34.0) * np.cos(p[:, 1] * 36.0)
            + 0.006 * np.sin(p[:, 2] * 58.0 - p[:, 0] * 42.0)
            + 0.004 * np.cos(p[:, 1] * 64.0 + p[:, 2] * 52.0)
        )
        r = np.maximum(1e-5, np.hypot(p[:, 0], p[:, 1] - 0.128))
        p[:, 0] += bump * (p[:, 0] / r)
        p[:, 1] += bump * ((p[:, 1] - 0.128) / r)
        # Slanted bite opening across the front (+Z) end of the fried crust exposing the protruding hot dog!
        scallop = 0.012 * np.cos(p[:, 0] * 26.0)
        z_bite_limit = 0.255 - 0.32 * (p[:, 1] - 0.128) + scallop
        p[:, 2] = np.minimum(p[:, 2], z_bite_limit)
        return p

    batter = gradient_y_tint(
        curved_tube(
            [(0.0, 0.125, -0.24), (0.0, 0.128, -0.06), (0.0, 0.128, +0.16), (0.0, 0.126, +0.34)],
            [(0.066, 0.064), (0.118, 0.112), (0.118, 0.112), (0.106, 0.102)],
            samples_per_seg=7,
            radial_segs=30,
            cap_rings=5,
            name="corndog_batter",
            material="item_corndog_crust",
        ).deform(_fry_and_bite_crust),
        (0.74, 0.62, 0.46),
        (1.06, 0.96, 0.78),
        noise_amp=0.07,
    )

    # Slim HOLLOW pale-yellow spongy cornmeal cross-section lip ringing the batter mouth (keeps the hot dog 100% exposed!)
    crumb_ring = (
        torus_arc(0.084, 0.022, u_segs=32, v_segs=12, plane="xy", name="corndog_crumb_ring", material="item_bun_crumb")
        .deform(lambda p: p + 0.004 * np.sin(p * 52.0))
        .rotate_xyz(rx=0.30)
        .translate(0.0, 0.128, +0.246)
    )

    # Prominent protruding pink-brown hot dog frankfurter WITH A HUGE UNMISTAKABLE CURVED BITE CHOMPED OUT OF ITS TIP!
    def _bite_hotdog(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        # Carve a deep bold U-shaped semicircular tooth-bite directly out of the upper-front end of the frankfurter!
        # Center of bite arc sits at (x=0, z=+0.46), biting cleanly back to z=+0.35 in the center while leaving rounded
        # casing cheeks on left/right and an angled bottom lip just like a real human bite out of a hot dog!
        dx = p[:, 0]
        in_front = p[:, 2] > 0.28
        u_bite = 0.048 * np.exp(-((dx / 0.048) ** 2))
        z_cut = 0.422 - 0.52 * (p[:, 1] - 0.120) - u_bite
        p[:, 2] = np.where(in_front, np.minimum(p[:, 2], z_cut), p[:, 2])
        # Also scoop slightly down into the top crown of the frankfurter where the upper teeth bit down!
        top_scoop = 0.028 * np.exp(-((dx / 0.046) ** 2)) * np.clip((p[:, 2] - 0.30) / 0.10, 0.0, 1.0)
        p[:, 1] = np.where(p[:, 1] > 0.125, p[:, 1] - top_scoop, p[:, 1])
        return p

    hotdog = gradient_y_tint(
        curved_tube(
            [(0.0, 0.128, +0.16), (0.0, 0.130, +0.30), (0.0, 0.132, +0.445)],
            [(0.068, 0.066), (0.070, 0.068), (0.066, 0.064)],
            samples_per_seg=8,
            radial_segs=30,
            cap_rings=6,
            name="corndog_hotdog",
            material="item_hotdog_pink",
        ).deform(_bite_hotdog),
        (0.76, 0.64, 0.56),
        (1.04, 0.94, 0.86),
        noise_amp=0.05,
    )

    # Bright pale-pink concave bitten inner meat cross-section face seated right inside the chomped bite of the hot dog!
    def _cut_meat_scoop(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        p[:, 2] -= 0.026 * np.exp(-((p[:, 0] / 0.046) ** 2))
        return p

    hotdog_bite_face = (
        cylinder(0.060, 0.058, 0.016, radial_segs=24, bevel=0.004, axis="z", name="corndog_hotdog_bite", material="item_hotdog_inside")
        .deform(_cut_meat_scoop)
        .rotate_xyz(rx=0.66)
        .translate(0.0, 0.140, +0.382)
    )

    # Textured wooden skewer stick protruding cleanly from the rear end.
    stick = (
        cylinder(0.017, 0.015, 0.36, radial_segs=14, bevel=0.004, axis="z", name="corndog_stick", material="item_wood_stick")
        .translate(0.0, 0.115, -0.37)
    )

    # Thick plump bright yellow squeezed mustard zigzag ribbon draped across the fried batter behind the bite!
    mustard_pts: List[Tuple[float, float, float]] = []
    n_m = 32
    for i in range(n_m + 1):
        u = i / float(n_m)
        mz = -0.18 + 0.37 * u
        mx = 0.060 * math.sin(u * math.pi * 5.8)
        r_surf = 0.116 * math.sqrt(max(0.18, 1.0 - ((mx / 0.120) ** 2)))
        end_w = math.sin(u * math.pi) ** 0.22
        my = 0.128 + r_surf * (0.86 + 0.14 * end_w) + 0.010
        mustard_pts.append((mx, my, mz))
    mustard = curved_tube(
        mustard_pts,
        [0.0155] * len(mustard_pts),
        samples_per_seg=2,
        radial_segs=12,
        cap_rings=2,
        name="corndog_mustard",
        material="item_mustard",
    )
    sc.add("corndog", [batter, crumb_ring, hotdog, hotdog_bite_face, stick, mustard])
    # Angle top-left (bitten hot dog tip) -> bottom-right (wooden stick) matching `361:fdcornd1`!
    sc.nodes["corndog"] = [p.rotate_xyz(ry=-135.0, degrees=True) for p in sc.nodes["corndog"]]
    return _finalize_item(sc, target_span=0.96)


def build_fdjelo1() -> SceneBuilder:
    """Translucent ruby-red fluted molded Jell-O dome with visible banana coins & gentle wobble clip (`366:fdjelo1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdjelo1")

    # Classic deep 12-fluted scalloped gelatin Bundt mold dome!
    prof = [
        (0.000, 0.000),
        (0.325, 0.000),
        (0.334, 0.042),
        (0.308, 0.140),
        (0.262, 0.265),
        (0.200, 0.352),
        (0.136, 0.388),
        (0.060, 0.384),
        (0.000, 0.370),
    ]

    def _flute_mold(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ang = np.arctan2(p[:, 2], p[:, 0])
        h_frac = np.clip(p[:, 1] / 0.388, 0.0, 1.0)
        flute = 1.0 + 0.165 * np.cos(ang * 12.0) * np.sin(h_frac * math.pi * 0.94)
        p[:, 0] *= flute
        p[:, 2] *= flute
        return p

    # Chunky creamy banana coin slices pressed close to the translucent outer shell (with delicate seed center)!
    banana_specs = (
        (+0.18, 0.125, +0.16, +0.75, -0.45),
        (-0.18, 0.120, +0.15, +0.72, +0.48),
        (+0.02, 0.115, +0.23, +0.92, +0.08),
        (+0.16, 0.240, +0.08, +0.48, -0.58),
        (-0.15, 0.245, +0.09, +0.46, +0.55),
        (+0.00, 0.290, +0.11, +0.38, -0.05),
        (+0.19, 0.145, -0.14, -0.68, -0.50),
        (-0.19, 0.150, -0.13, -0.65, +0.52),
        (+0.02, 0.220, -0.18, -0.55, +0.10),
        (-0.04, 0.310, -0.06, -0.22, +0.18),
    )
    for idx, (bx, by, bz, rx, rz) in enumerate(banana_specs):
        sl = (
            cylinder(0.064, 0.064, 0.022, radial_segs=18, bevel=0.005, axis="y", name=f"jello_banana_{idx}", material="item_banana_slice")
            .rotate_xyz(rx=rx, rz=rz)
            .translate(bx, by, bz)
        )
        # Delicate tan banana pith dot in the center (subtle realistic detail, not a dark ring!)
        core = (
            cylinder(0.008, 0.008, 0.024, radial_segs=10, axis="y", name=f"jello_banana_core_{idx}", material="item_wood_stick")
            .rotate_xyz(rx=rx, rz=rz)
            .translate(bx, by, bz)
        )
        sc.add("jello_body", [sl, core])

    jelly_dome = lathe(prof, radial_segs=72, cap_bottom=False, cap_top=False, axis="y", name="jello_dome", material="item_jello_ruby").deform(_flute_mold)
    sc.add("jello_body", jelly_dome)

    # Subtle authentic 1998 Jell-O wobble idle animation (7-frame elastic squash/shear jiggle!).
    times = [0.00, 0.12, 0.24, 0.36, 0.48, 0.60, 0.72]
    scales = [
        (1.00, 1.00, 1.00),
        (1.045, 0.94, 1.035),
        (0.965, 1.055, 0.975),
        (1.030, 0.965, 1.025),
        (0.980, 1.030, 0.985),
        (1.012, 0.990, 1.010),
        (1.00, 1.00, 1.00),
    ]
    rots = [
        quat_from_euler_xyz(rz=a, rx=b)
        for a, b in ((0.0, 0.0), (0.045, 0.02), (-0.042, -0.018), (0.028, 0.014), (-0.020, -0.010), (0.010, 0.005), (0.0, 0.0))
    ]
    sc.add_animation(
        "Idle",
        [
            AnimationChannel("jello_body", "scale", times, scales),
            AnimationChannel("jello_body", "rotation", times, rots),
        ],
    )
    return _finalize_item(sc, target_span=0.88)


def build_fdchick1() -> SceneBuilder:
    """Crispy golden-amber fried chicken drumstick with craggy fried breading & exposed bone (`399:fdchick1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdchick1")

    def _crispy_breading(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        r = np.maximum(1e-5, np.hypot(p[:, 0], p[:, 1]))
        crag = (
            0.020 * np.sin(p[:, 0] * 32.0 + p[:, 2] * 28.0) * np.cos(p[:, 1] * 34.0)
            + 0.014 * np.sin(p[:, 2] * 54.0 - p[:, 1] * 46.0)
            + 0.009 * np.cos(p[:, 0] * 68.0 + p[:, 2] * 60.0)
        )
        p[:, 0] += crag * (p[:, 0] / r)
        p[:, 1] += crag * (p[:, 1] / r)
        return p

    meat = tint_part(
        curved_tube(
            [(0.0, 0.11, -0.18), (0.0, 0.13, -0.04), (0.0, 0.15, +0.14), (0.0, 0.14, +0.30), (0.0, 0.12, +0.38)],
            [(0.044, 0.042), (0.090, 0.084), (0.154, 0.138), (0.136, 0.122), (0.058, 0.054)],
            samples_per_seg=6,
            radial_segs=28,
            cap_rings=5,
            name="chicken_meat",
            material="item_chicken_crust",
        ).deform(_crispy_breading),
        lambda pos, nrm: np.clip(
            np.column_stack([
                0.86 + 0.14 * np.clip(nrm[:, 1], 0.0, 1.0) + 0.12 * np.sin(pos[:, 2] * 32.0 + pos[:, 0] * 36.0),
                0.68 + 0.24 * np.clip(nrm[:, 1], 0.0, 1.0) + 0.10 * np.sin(pos[:, 2] * 32.0 + pos[:, 0] * 36.0),
                0.44 + 0.22 * np.clip(nrm[:, 1], 0.0, 1.0),
            ]),
            0.38,
            1.0,
        ),
    )
    bone_shaft = cylinder(0.025, 0.031, 0.22, radial_segs=14, bevel=0.004, axis="z", name="chicken_bone", material="item_bone_white").translate(0.0, 0.105, -0.25)
    knuckle_l = ellipsoid(0.032, 0.030, 0.034, u_segs=14, v_segs=10, name="chicken_knuckle_l", material="item_bone_white").translate(+0.022, 0.105, -0.355)
    knuckle_r = ellipsoid(0.030, 0.028, 0.034, u_segs=14, v_segs=10, name="chicken_knuckle_r", material="item_bone_white").translate(-0.022, 0.105, -0.355)
    sc.add("chicken", [meat, bone_shaft, knuckle_l, knuckle_r])
    # Orient bone bottom-left -> plump golden drumstick top-right matching `399:fdchick1`!
    sc.nodes["chicken"] = [p.rotate_xyz(ry=136.0, degrees=True) for p in sc.nodes["chicken"]]
    return _finalize_item(sc, target_span=0.95)


def build_fdsuckr1() -> SceneBuilder:
    """Glossy vivid crimson-red & snow-white swirled peppermint lollipop on crooked paper stick (`256:fdsuckr1`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_fdsuckr1")

    # Plump glossy candy disc with razor-sharp 8-arm red-and-white peppermint pinwheel swirl!
    disc = superellipsoid(0.22, 0.068, 0.22, e1=0.68, e2=0.98, u_segs=64, v_segs=28, name="lollipop_candy", material="item_candy_white").translate(0.0, 0.068, +0.18)

    def _peppermint_swirl(pos: np.ndarray, _nrm: np.ndarray) -> np.ndarray:
        dx = pos[:, 0]
        dz = pos[:, 2] - 0.18
        r = np.hypot(dx, dz)
        ang = np.arctan2(dz, dx)
        swirl = np.sin(8.0 * ang + r * 22.0)
        red_mask = np.clip(swirl * 10.0 + 0.5, 0.0, 1.0)[:, None]
        white = np.array([[0.99, 0.98, 0.97]], dtype=np.float64)
        red = np.array([[0.96, 0.06, 0.08]], dtype=np.float64)
        return white * (1.0 - red_mask) + red * red_mask

    sc.add("lollipop", tint_part(disc, _peppermint_swirl))

    # Slightly bent white/pink-tinted paper lollipop stick pointing down-right matching `256:fdsuckr1`.
    stick = tint_part(
        curved_tube(
            [(0.0, 0.056, +0.02), (0.012, 0.044, -0.14), (-0.018, 0.032, -0.32), (0.014, 0.022, -0.50)],
            [0.016, 0.0155, 0.015, 0.0145],
            samples_per_seg=4,
            radial_segs=14,
            cap_rings=3,
            name="lollipop_stick",
            material="item_candy_white",
        ),
        lambda pos, _n: np.column_stack([
            np.full(len(pos), 0.98),
            0.78 + 0.16 * np.clip((-pos[:, 2]) / 0.50, 0.0, 1.0),
            0.80 + 0.14 * np.clip((-pos[:, 2]) / 0.50, 0.0, 1.0),
        ]),
    )
    sc.add("lollipop", stick)
    # Orient peppermint head upper-left -> crooked paper stick lower-right matching `256:fdsuckr1`!
    sc.nodes["lollipop"] = [p.rotate_xyz(ry=-132.0, degrees=True) for p in sc.nodes["lollipop"]]
    return _finalize_item(sc, target_span=0.95)
