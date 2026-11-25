"""Realistic 3D sculptors for 1998 Ants team anthills, swimmer bridges, power-ups, bombs, and fire wall (Wave 2).

Covers:
- `BLACKHILL` (245), `BLUEHILL` (246), `REDHILL` (247): Remaining 3 team anthills with authentic scavenged decor
- `bridge1`..`bridge4`, `bridge4b` (34–38): Progressive 5-stage swimmer mud & river-stone water bridge
- `pu_comb` (62), `pu_thief` (63), `pu_bomb` (64), `pu_swim` (65), `pu_mason` (66): All 5 class gear power-ups
- `redbomb` (129), `greenbomb` (130), `blackbomb` (131), `bluebomb` (132): All 4 team planted bombs + spark pulse
- `wallup04` (134): Blazing multi-layered 3D Fire Wall flame + animated flicker clip
"""
from __future__ import annotations

import math
from typing import Callable, List, Tuple

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
            cone,
            curved_tube,
            cylinder,
            ellipsoid,
            lathe,
            quat_from_euler_xyz,
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
            cone,
            curved_tube,
            cylinder,
            ellipsoid,
            lathe,
            quat_from_euler_xyz,
            rounded_box,
            sphere,
            superellipsoid,
            torus_arc,
        )


# ---------------------------------------------------------------------------
# Shared sculpted earthen volcano anthill base (with exact surface height query)
# ---------------------------------------------------------------------------

def _build_anthill_base(
    sc: SceneBuilder,
    *,
    soil_mat: str,
    seed: int,
    loam_lo: Tuple[float, float, float],
    loam_hi: Tuple[float, float, float],
) -> Callable[[float, float], float]:
    """Build organic cratered dirt anthill mound + burrow throat + pebbles, returning exact `_surface_y(x, z)`."""
    mound_prof = [
        (0.000, 0.110),
        (0.110, 0.115),
        (0.125, 0.265),
        (0.165, 0.340),
        (0.255, 0.305),
        (0.380, 0.170),
        (0.495, 0.035),
        (0.520, 0.000),
        (0.000, 0.000),
    ]
    slope_stations = [(0.165, 0.340), (0.255, 0.305), (0.380, 0.170), (0.495, 0.035), (0.520, 0.000)]
    ph_a = 0.4 + 0.7 * (seed % 7)
    ph_b = -0.8 + 0.5 * (seed % 5)

    def _wobble(ang: float) -> float:
        return 1.0 + 0.072 * math.sin(ang * 3.0 + ph_a) + 0.044 * math.cos(ang * 5.0 + ph_b)

    def _surface_y(x: float, z: float) -> float:
        ang = math.atan2(z, x)
        r_prof = math.hypot(x, z) / _wobble(ang)
        if r_prof <= slope_stations[0][0]:
            return slope_stations[0][1]
        for (r0, y0), (r1, y1) in zip(slope_stations[:-1], slope_stations[1:]):
            if r_prof <= r1:
                t = (r_prof - r0) / max(1e-5, r1 - r0)
                return y0 * (1.0 - t) + y1 * t
        return 0.0

    def _sculpt(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ang = np.arctan2(p[:, 2], p[:, 0])
        wobble = 1.0 + 0.072 * np.sin(ang * 3.0 + ph_a) + 0.044 * np.cos(ang * 5.0 + ph_b)
        p[:, 0] *= wobble
        p[:, 2] *= wobble
        clods = 0.014 * np.sin(p[:, 0] * 25.0 + p[:, 2] * 21.0 + seed) * np.cos(p[:, 1] * 28.0)
        p[:, 1] = np.maximum(0.0, p[:, 1] + clods * np.clip(p[:, 1] / 0.06, 0.0, 1.0))
        return p

    def _tint(pos: np.ndarray, _nrm: np.ndarray) -> np.ndarray:
        r = np.hypot(pos[:, 0], pos[:, 2])
        hole_dark = np.clip((r - 0.105) / 0.080, 0.10, 1.0)[:, None]
        hw = np.clip(pos[:, 1] / 0.34, 0.0, 1.0)[:, None]
        mottle = 0.06 * np.sin(pos[:, 0] * 22.0 + pos[:, 2] * 19.0 + seed)[:, None]
        lo = np.asarray(loam_lo, dtype=np.float64)[None, :]
        hi = np.asarray(loam_hi, dtype=np.float64)[None, :]
        return np.clip(((lo * (1.0 - hw) + hi * hw) + mottle) * hole_dark, 0.06, 1.0)

    mound = tint_part(
        lathe(mound_prof, radial_segs=48, cap_bottom=False, cap_top=False, axis="y", name="hill_mound", material=soil_mat).deform(_sculpt),
        _tint,
    )
    hole = cylinder(0.118, 0.118, 0.030, radial_segs=24, axis="y", name="hill_hole_dark", material="item_soil_hole").translate(0.0, 0.145, 0.0)
    sc.add("anthill", [mound, hole])

    rng = np.random.default_rng(seed)
    for k in range(16):
        ang = 2.0 * math.pi * k / 16.0 + float(rng.uniform(-0.15, 0.15))
        rad = float(rng.uniform(0.43, 0.56))
        px, pz = rad * math.cos(ang), rad * math.sin(ang)
        pr = float(rng.uniform(0.020, 0.040))
        py = max(pr * 0.68, _surface_y(px, pz) + pr * 0.25)
        peb = ellipsoid(pr * 1.15, pr * 0.64, pr * 0.94, u_segs=10, v_segs=6, name=f"hill_pebble_{k}", material=soil_mat if k % 2 else "item_stone_granite").rotate_xyz(ry=ang).translate(px, py, pz)
        sc.add("anthill", peb)
    return _surface_y


def _crimped_bottle_cap(prefix: str, top_mat: str, *, radius: float = 0.102) -> List[MeshPart]:
    """Reusable realistic 21-tooth crimped metallic soda bottle cap."""
    def _crimp(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ang = np.arctan2(p[:, 2], p[:, 0])
        bot = np.clip(-p[:, 1] / 0.024, 0.0, 1.0)
        flute = 1.0 + 0.11 * np.cos(ang * 21.0) * bot
        p[:, 0] *= flute
        p[:, 2] *= flute
        return p

    metal = cylinder(radius, radius * 1.12, 0.038, radial_segs=42, bevel=0.005, axis="y", name=f"{prefix}_metal", material="item_aluminum").deform(_crimp)
    top = cylinder(radius * 0.86, radius * 0.86, 0.015, radial_segs=28, bevel=0.004, axis="y", name=f"{prefix}_top", material=top_mat).translate(0.0, 0.016, 0.0)
    return [metal, top]


def _paperclip(name: str, material: str = "item_aluminum", scale: float = 1.0) -> MeshPart:
    """Sculpted realistic looped wire paperclip."""
    s = scale
    pts = [
        (-0.075 * s, 0.0, +0.024 * s),
        (+0.085 * s, 0.0, +0.024 * s),
        (+0.112 * s, 0.0, 0.0),
        (+0.085 * s, 0.0, -0.024 * s),
        (-0.085 * s, 0.0, -0.024 * s),
        (-0.110 * s, 0.0, -0.004 * s),
        (-0.082 * s, 0.0, +0.014 * s),
        (+0.055 * s, 0.0, +0.014 * s),
    ]
    return curved_tube(pts, [0.008 * s] * len(pts), samples_per_seg=3, radial_segs=8, cap_rings=2, name=name, material=material)


def build_blackhill() -> SceneBuilder:
    """Black Team Anthill with bent nail, Phillips screws, hex nut, metal washers & bolt (`245:BLACKHILL`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_blackhill")
    surf_y = _build_anthill_base(sc, soil_mat="item_soil_grey", seed=245, loam_lo=(0.74, 0.72, 0.68), loam_hi=(1.00, 0.98, 0.92))

    # 1. Giant bent steel carpentry nail stuck boldly into the upper-left crater rim (`245:BLACKHILL`)!
    nx, nz = -0.22, -0.14
    ny = surf_y(nx, nz) + 0.015
    nail_shaft = curved_tube(
        [(nx, ny - 0.04, nz), (nx - 0.08, ny + 0.12, nz - 0.04), (nx - 0.16, ny + 0.24, nz - 0.08)],
        [0.020, 0.022, 0.024],
        samples_per_seg=4,
        radial_segs=12,
        name="bh_nail_shaft",
        material="item_steel_dark",
    )
    nail_head = cylinder(0.068, 0.064, 0.022, radial_segs=18, bevel=0.004, axis="y", name="bh_nail_head", material="item_aluminum").rotate_xyz(rx=-0.42, rz=0.58).translate(nx - 0.17, ny + 0.25, nz - 0.08)
    sc.add("anthill", [nail_shaft, nail_head])

    # 2. Scattered metallic Phillips screws & flat dark washers across front-left & center skirt!
    screw_specs = (
        (-0.24, +0.34, 0.062, 0.45, 0.40),
        (-0.08, +0.42, 0.056, 0.55, 0.18),
        (+0.06, +0.36, 0.072, 0.35, -0.15),
    )
    for idx, (sx, sz, rad, rx, rz) in enumerate(screw_specs):
        sy = max(0.035, surf_y(sx, sz) + 0.024)
        head = cylinder(rad, rad * 0.94, 0.034, radial_segs=18, bevel=0.005, axis="y", name=f"bh_screw_h_{idx}", material="item_aluminum").rotate_xyz(rx=rx, rz=rz).translate(sx, sy, sz)
        slot_a = rounded_box(rad * 1.15, 0.010, 0.014, bevel=0.002, segs=1, name=f"bh_slot_a_{idx}", material="item_steel_dark").rotate_xyz(rx=rx, rz=rz).translate(sx, sy + 0.018, sz)
        slot_b = rounded_box(0.014, 0.010, rad * 1.15, bevel=0.002, segs=1, name=f"bh_slot_b_{idx}", material="item_steel_dark").rotate_xyz(rx=rx, rz=rz).translate(sx, sy + 0.018, sz)
        sc.add("anthill", [head, slot_a, slot_b])

    # 3. Dark black flat hardware washers & long threaded bolt along front-right edge!
    for idx, (wx, wz) in enumerate(((-0.26, +0.40), (-0.16, +0.46))):
        wy = max(0.024, surf_y(wx, wz) + 0.016)
        sc.add("anthill", torus_arc(0.056, 0.018, u_segs=20, v_segs=8, plane="xz", name=f"bh_washer_{idx}", material="item_steel_dark").rotate_xyz(rx=0.25).translate(wx, wy, wz))

    bolt = cylinder(0.026, 0.026, 0.34, radial_segs=14, bevel=0.004, axis="z", name="bh_bolt", material="item_steel_dark").rotate_xyz(rx=0.14, ry=1.08).translate(+0.22, max(0.032, surf_y(+0.22, +0.34) + 0.024), +0.34)
    sc.add("anthill", bolt)

    # 4. Scavenged silver hex nut & crumpled foil/gear dome on the right slope (`245:BLACKHILL`)!
    gx, gz = +0.36, +0.06
    gy = surf_y(gx, gz) + 0.044
    nut = cylinder(0.095, 0.098, 0.058, radial_segs=6, bevel=0.008, axis="y", name="bh_hex_nut", material="item_aluminum").rotate_xyz(rx=0.22, rz=-0.42).translate(gx, gy, gz)
    nut_hole = cylinder(0.044, 0.044, 0.064, radial_segs=14, axis="y", name="bh_hex_hole", material="item_steel_dark").rotate_xyz(rx=0.22, rz=-0.42).translate(gx, gy, gz)
    sc.add("anthill", [nut, nut_hole])
    return _finalize_item(sc, target_span=1.00)


def build_bluehill() -> SceneBuilder:
    """Blue Team Anthill with blue flag pennant, thumbtacks, blue hose, paperclips & H2O bottle cap (`246:BLUEHILL`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_bluehill")
    surf_y = _build_anthill_base(sc, soil_mat="item_soil_mound", seed=246, loam_lo=(0.76, 0.72, 0.66), loam_hi=(1.02, 0.96, 0.88))

    # 1. Blue folded triangle flag/pennant stuck right beside the crater crown (`246:BLUEHILL`)!
    fx, fz = +0.08, +0.08
    fy = surf_y(fx, fz) + 0.035
    flag_cone = cone(0.088, 0.28, radial_segs=4, name="bl_flag", material="item_plastic_blue").rotate_xyz(rx=0.35, ry=0.45, rz=-0.88).translate(fx + 0.04, fy + 0.06, fz + 0.06)
    sc.add("anthill", flag_cone)

    # 2. Curving dark-blue rubber gasket / strap hugging the front dirt skirt (`246:BLUEHILL`)!
    hose_pts = [
        (-0.38, max(0.025, surf_y(-0.38, +0.16) + 0.025), +0.16),
        (-0.24, max(0.030, surf_y(-0.24, +0.36) + 0.025), +0.36),
        (+0.02, max(0.030, surf_y(+0.02, +0.44) + 0.025), +0.44),
        (+0.28, max(0.028, surf_y(+0.28, +0.34) + 0.025), +0.34),
        (+0.38, max(0.025, surf_y(+0.38, +0.16) + 0.025), +0.16),
    ]
    hose = curved_tube(hose_pts, [0.026, 0.030, 0.032, 0.028, 0.024], samples_per_seg=5, radial_segs=14, cap_rings=3, name="bl_hose", material="item_bomb_blue")
    sc.add("anthill", hose)

    # 3. Scavenged crimped silver-and-blue "H2O" soda bottle cap on the right slope (`246:BLUEHILL`)!
    cx, cz = +0.36, +0.02
    cy = surf_y(cx, cz) + 0.036
    for p in _crimped_bottle_cap("bl_h2o_cap", "item_plastic_blue", radius=0.098):
        sc.add("anthill", p.rotate_xyz(rx=0.22, rz=-0.48).translate(cx, cy, cz))

    # 4. Colorful red & blue pushpins/thumbtacks + silver paperclips jammed into the mound!
    for idx, (tx, tz, mat, rz) in enumerate((
        (-0.34, +0.14, "item_plastic_red", +0.85),
        (-0.26, -0.22, "item_plastic_blue", +0.65),
        (+0.26, -0.18, "item_plastic_blue", -0.70),
    )):
        ty = surf_y(tx, tz) + 0.032
        pin = cylinder(0.008, 0.005, 0.09, radial_segs=8, axis="y", name=f"bl_pin_s_{idx}", material="item_aluminum").rotate_xyz(rz=rz).translate(tx, ty, tz)
        knob = cylinder(0.036, 0.030, 0.024, radial_segs=14, bevel=0.005, axis="y", name=f"bl_pin_h_{idx}", material=mat).rotate_xyz(rz=rz).translate(tx - 0.04 * math.sin(rz), ty + 0.04 * math.cos(rz), tz)
        sc.add("anthill", [pin, knob])

    for idx, (px, pz, ry) in enumerate(((-0.10, +0.38, 0.25), (+0.16, +0.34, -0.45))):
        py = max(0.032, surf_y(px, pz) + 0.032)
        sc.add("anthill", _paperclip(f"bl_clip_{idx}", "item_aluminum", scale=0.95).rotate_xyz(rx=0.35, ry=ry).translate(px, py, pz))

    return _finalize_item(sc, target_span=1.00)


def build_redhill() -> SceneBuilder:
    """Red Team Anthill with warm clay mound, three crimson soda caps, red ribbon & silver paperclip (`247:REDHILL`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_redhill")
    surf_y = _build_anthill_base(sc, soil_mat="item_soil_pink", seed=247, loam_lo=(0.82, 0.72, 0.68), loam_hi=(1.04, 0.94, 0.90))

    # 1. Three iconic crimson-red crimped "Soda" bottle caps clustered on lower-left, top-left & right (`247:REDHILL`)!
    cap_specs = (
        (-0.28, +0.32, 0.102, 0.42, +0.46),
        (-0.12, +0.40, 0.106, 0.48, +0.20),
        (+0.38, +0.10, 0.104, 0.28, -0.52),
        (-0.26, -0.32, 0.092, -0.35, +0.35),
    )
    for idx, (cx, cz, rad, rx, rz) in enumerate(cap_specs):
        cy = max(0.036, surf_y(cx, cz) + 0.032)
        for p in _crimped_bottle_cap(f"rd_cap_{idx}", "item_can_red", radius=rad):
            sc.add("anthill", p.rotate_xyz(rx=rx, rz=rz).translate(cx, cy, cz))

    # 2. Bright curved crimson plastic straw/ribbon tucked into the right crater rim (`247:REDHILL`)!
    ribbon_pts = [
        (+0.10, surf_y(+0.10, +0.02) + 0.020, +0.02),
        (+0.22, surf_y(+0.22, -0.06) + 0.040, -0.06),
        (+0.32, surf_y(+0.32, -0.18) + 0.055, -0.18),
    ]
    ribbon = curved_tube(ribbon_pts, [0.026, 0.032, 0.016], samples_per_seg=4, radial_segs=12, cap_rings=2, name="rd_ribbon", material="item_plastic_red")
    sc.add("anthill", ribbon)

    # 3. Prominent shiny silver paperclip resting cleanly on the front-right dirt slope (`247:REDHILL`)!
    px, pz = +0.16, +0.34
    py = max(0.032, surf_y(px, pz) + 0.028)
    clip = _paperclip("rd_paperclip", "item_aluminum", scale=1.35).rotate_xyz(rx=0.45, ry=0.68).translate(px, py, pz)
    sc.add("anthill", clip)

    # 4. Small silver pull-tab resting on the upper-right slope
    tab = torus_arc(0.044, 0.012, u_segs=18, v_segs=8, plane="xz", name="rd_pulltab", material="item_aluminum").rotate_xyz(rx=-0.25, rz=-0.40).translate(+0.28, surf_y(+0.28, -0.22) + 0.028, -0.22)
    sc.add("anthill", tab)
    return _finalize_item(sc, target_span=1.00)


# ---------------------------------------------------------------------------
# Swimmer-built progressive mud/river-stone water bridges (`34..38`)
# ---------------------------------------------------------------------------

def _build_bridge_stage(name: str, *, blobs: List[Tuple[float, float, float, float, float]], has_water: bool, seed: int) -> SceneBuilder:
    """Build one stage of the swimmer's mud-and-cobble water bridge (`bridge1`..`bridge4b`)."""
    register_item_materials()
    sc = SceneBuilder(name=name)

    if has_water:
        def _ripple(pos: np.ndarray) -> np.ndarray:
            p = pos.copy()
            ang = np.arctan2(p[:, 2], p[:, 0])
            edge = 1.0 + 0.14 * np.sin(ang * 4.0 + seed) + 0.09 * np.cos(ang * 7.0 - seed)
            p[:, 0] *= edge
            p[:, 2] *= edge
            return p

        water = cylinder(0.36, 0.36, 0.018, radial_segs=32, bevel=0.004, axis="y", name=f"{name}_water", material="item_water_ripple").deform(_ripple).translate(0.0, 0.010, 0.0)
        sc.add("bridge", water)

    for idx, (bx, bz, rx, ry, rz) in enumerate(blobs):
        def _mud_lump(pos: np.ndarray, k=idx) -> np.ndarray:
            p = pos.copy()
            r = np.maximum(1e-5, np.linalg.norm(p, axis=1, keepdims=True))
            n = p / r
            lumpy = 0.016 * np.sin(p[:, 0] * 18.0 + k * 1.7 + seed) * np.cos(p[:, 2] * 17.0 - k)
            p = p + lumpy[:, None] * n
            p[:, 1] = np.where(p[:, 1] < -ry * 0.45, -ry * 0.45 + 0.12 * (p[:, 1] + ry * 0.45), p[:, 1])
            return p

        lump = gradient_y_tint(
            superellipsoid(rx, ry, rz, e1=0.68, e2=0.82, u_segs=26, v_segs=16, name=f"{name}_mud_{idx}", material="item_soil_mud")
            .deform(_mud_lump)
            .translate(bx, ry * 0.55 + (0.012 if has_water else 0.0), bz),
            (0.62, 0.58, 0.54),
            (1.04, 0.98, 0.90),
            noise_amp=0.06,
        )
        sc.add("bridge", lump)

    return _finalize_item(sc, target_span=0.90)


def build_bridge1() -> SceneBuilder:
    """Bridge stage 1: tiny emerging mud shoal inside blue water ripple (`34:bridge1`)."""
    return _build_bridge_stage("item_bridge1", blobs=[(0.00, 0.00, 0.14, 0.055, 0.12)], has_water=True, seed=34)


def build_bridge2() -> SceneBuilder:
    """Bridge stage 2: wet clustered river stones emerging from water (`35:bridge2`)."""
    return _build_bridge_stage(
        "item_bridge2",
        blobs=[(-0.08, -0.04, 0.17, 0.075, 0.15), (+0.10, +0.06, 0.15, 0.068, 0.14), (+0.02, -0.10, 0.12, 0.058, 0.11)],
        has_water=True,
        seed=35,
    )


def build_bridge3() -> SceneBuilder:
    """Bridge stage 3: three broad overlapping dark cobble mud mounds (`36:bridge3`)."""
    return _build_bridge_stage(
        "item_bridge3",
        blobs=[(-0.14, -0.06, 0.22, 0.095, 0.18), (+0.14, -0.08, 0.21, 0.092, 0.19), (-0.02, +0.16, 0.20, 0.088, 0.17)],
        has_water=True,
        seed=36,
    )


def build_bridge4() -> SceneBuilder:
    """Bridge stage 4: solid packed walkable mud-bridge crossing pad (`37:bridge4`)."""
    return _build_bridge_stage(
        "item_bridge4",
        blobs=[
            (0.00, 0.00, 0.38, 0.105, 0.38),
            (-0.18, -0.18, 0.22, 0.098, 0.22),
            (+0.18, -0.16, 0.21, 0.095, 0.21),
            (-0.16, +0.18, 0.21, 0.096, 0.21),
            (+0.18, +0.18, 0.22, 0.098, 0.22),
        ],
        has_water=False,
        seed=37,
    )


def build_bridge4b() -> SceneBuilder:
    """Bridge stage 4 variant: sculpted scalloped mud-bridge pad (`38:bridge4b`)."""
    return _build_bridge_stage(
        "item_bridge4b",
        blobs=[
            (0.00, 0.00, 0.36, 0.108, 0.36),
            (-0.20, -0.10, 0.23, 0.100, 0.24),
            (+0.16, -0.20, 0.22, 0.096, 0.22),
            (+0.20, +0.14, 0.24, 0.102, 0.23),
            (-0.12, +0.20, 0.22, 0.096, 0.21),
        ],
        has_water=False,
        seed=38,
    )


# ---------------------------------------------------------------------------
# All 5 Class Gear Power-Up Pickups (`62..66`)
# ---------------------------------------------------------------------------

def build_pu_comb() -> SceneBuilder:
    """Combat Ant Power-Up: bold crossed golden-yellow bullet bandolier belts (`62:pu_comb`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_pu_comb")

    for ang_deg, y_off, tag in ((+38.0, 0.0, "a"), (-38.0, 0.040, "b")):
        belt = rounded_box(0.78, 0.048, 0.195, bevel=0.014, segs=2, name=f"pu_belt_{tag}", material="item_gear_gold").translate(0.0, 0.040 + y_off, 0.0)
        parts: List[MeshPart] = [belt]
        # Chunky row of bright 3D brass cartridges slotted boldly across each bandolier!
        for k in range(7):
            bx = -0.30 + k * (0.60 / 6.0)
            casing = cylinder(0.025, 0.025, 0.135, radial_segs=12, bevel=0.004, axis="z", name=f"pu_shell_{tag}_{k}", material="item_gear_gold").translate(bx, 0.078 + y_off, -0.010)
            tip = cone(0.024, 0.044, radial_segs=12, name=f"pu_tip_{tag}_{k}", material="item_aluminum").rotate_xyz(rx=90.0, degrees=True).translate(bx, 0.078 + y_off, +0.074)
            parts.extend([casing, tip])
        for p in parts:
            sc.add("pu_comb", p.rotate_xyz(ry=ang_deg, degrees=True))
    return _finalize_item(sc, target_span=0.94)


def build_pu_thief() -> SceneBuilder:
    """Thief Ant Power-Up: rolled golden bandit mask headband with eye slots & fluttering knot tails (`63:pu_thief`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_pu_thief")

    # Cylindrical golden bandit headband loop with sculpted dark eye cutouts on the front face!
    band_prof = [
        (0.220, 0.000),
        (0.242, 0.008),
        (0.246, 0.140),
        (0.236, 0.156),
        (0.214, 0.150),
        (0.210, 0.012),
        (0.220, 0.000),
    ]
    band = lathe(band_prof, radial_segs=38, cap_bottom=False, cap_top=False, axis="y", name="thief_mask_band", material="item_gear_gold")
    sc.add("pu_thief", band)

    # Dark recessed eye cutouts on the front-left of the band (`63:pu_thief`)
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        ang = side * 0.34
        hole = (
            ellipsoid(0.044, 0.034, 0.022, u_segs=16, v_segs=10, name=f"thief_eye_slot_{tag}", material="item_dark_stitch")
            .rotate_xyz(ry=ang)
            .translate(0.238 * math.sin(ang), 0.080, 0.238 * math.cos(ang))
        )
        sc.add("pu_thief", hole)

    # Tied cloth knot + twin perky fluttering mask tails sweeping out the back-right (`63:pu_thief`)!
    knot = ellipsoid(0.045, 0.042, 0.048, u_segs=14, v_segs=10, name="thief_knot", material="item_gear_gold").translate(+0.255, 0.092, -0.06)
    tail_top = curved_tube(
        [(+0.255, 0.095, -0.06), (+0.36, 0.165, -0.10), (+0.46, 0.235, -0.13)],
        [(0.022, 0.032), (0.018, 0.038), (0.008, 0.014)],
        samples_per_seg=4,
        radial_segs=12,
        cap_rings=2,
        name="thief_tail_top",
        material="item_gear_gold",
    )
    tail_bot = curved_tube(
        [(+0.255, 0.088, -0.06), (+0.36, 0.085, -0.13), (+0.45, 0.095, -0.20)],
        [(0.020, 0.028), (0.016, 0.032), (0.007, 0.012)],
        samples_per_seg=4,
        radial_segs=12,
        cap_rings=2,
        name="thief_tail_bot",
        material="item_gear_gold",
    )
    sc.add("pu_thief", [knot, tail_top, tail_bot])
    sc.nodes["pu_thief"] = [p.rotate_xyz(ry=-24.0, degrees=True) for p in sc.nodes["pu_thief"]]
    return _finalize_item(sc, target_span=0.92)


def build_pu_bomb() -> SceneBuilder:
    """Bomber Ant Power-Up: brass steampunk goggles wrapped around a sizzling dynamite bomb (`64:pu_bomb`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_pu_bomb")

    # Upright red-and-purple dynamite stick with curving white fuse & glowing yellow spark!
    canister = cylinder(0.095, 0.092, 0.28, radial_segs=24, bevel=0.008, axis="y", name="pubomb_body", material="item_bomb_red").translate(0.0, 0.24, -0.04)
    collar = cylinder(0.098, 0.098, 0.055, radial_segs=24, bevel=0.006, axis="y", name="pubomb_collar", material="item_bomb_purple").translate(0.0, 0.16, -0.04)
    fuse = curved_tube(
        [(0.0, 0.37, -0.04), (0.02, 0.45, -0.03), (0.07, 0.49, 0.00), (0.10, 0.47, +0.02)],
        [0.012, 0.011, 0.010, 0.009],
        samples_per_seg=4,
        radial_segs=10,
        name="pubomb_fuse",
        material="item_fuse_cord",
    )
    spark = sphere(0.026, u_segs=12, v_segs=10, name="pubomb_spark", material="item_flame_core").translate(0.10, 0.47, +0.02)
    sc.add("pu_bomb", [canister, collar, fuse, spark])

    # Brass steampunk goggle strap wrapping around base + twin thick brass eyecups with dark lenses!
    strap = torus_arc(0.185, 0.034, u_segs=28, v_segs=10, plane="xz", name="pubomb_strap", material="item_gear_brass").translate(0.0, 0.075, -0.02)
    sc.add("pu_bomb", strap)
    for side, tag in ((-1.0, "l"), (+1.0, "r")):
        gx = side * 0.115
        cup = cylinder(0.092, 0.092, 0.100, radial_segs=24, bevel=0.012, axis="z", name=f"pubomb_cup_{tag}", material="item_gear_gold").rotate_xyz(ry=side * 0.24).translate(gx, 0.095, +0.165)
        lens = cylinder(0.068, 0.068, 0.022, radial_segs=22, axis="z", name=f"pubomb_lens_{tag}", material="item_aluminum").rotate_xyz(ry=side * 0.24).translate(gx + side * 0.012, 0.095, +0.210)
        sc.add("pu_bomb", [cup, lens])

    sc.nodes["pu_bomb"] = [p.rotate_xyz(ry=18.0, degrees=True) for p in sc.nodes["pu_bomb"]]
    return _finalize_item(sc, target_span=0.92)


def build_pu_swim() -> SceneBuilder:
    """Swimmer Ant Power-Up: glossy dark rubber scuba mask with golden oval rim & snorkel tube (`65:pu_swim`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_pu_swim")

    # Dark rubber swim-mask hood/skirt + rear strap
    hood = superellipsoid(0.20, 0.19, 0.22, e1=0.82, e2=0.88, u_segs=28, v_segs=18, name="swim_hood", material="item_rubber_dark").translate(0.0, 0.20, -0.04)
    # Bold golden oval faceplate bezel + glossy blue-tinted glass viewport!
    bezel = torus_arc(0.155, 0.034, u_segs=32, v_segs=12, plane="xy", name="swim_bezel", material="item_gear_gold").scale(1.12, 0.92, 1.0).translate(0.0, 0.18, +0.155)
    glass = ellipsoid(0.150, 0.125, 0.022, u_segs=24, v_segs=14, name="swim_glass", material="eye_pupil").translate(0.0, 0.18, +0.150)
    # Curved golden-yellow snorkel breathing tube sweeping down & left from the mask cheek (`65:pu_swim`)!
    snorkel = curved_tube(
        [(-0.15, 0.13, +0.12), (-0.23, 0.08, +0.16), (-0.28, 0.05, +0.22), (-0.34, 0.05, +0.24)],
        [0.028, 0.026, 0.026, 0.030],
        samples_per_seg=4,
        radial_segs=14,
        cap_rings=2,
        name="swim_snorkel",
        material="item_gear_gold",
    )
    sc.add("pu_swim", [hood, bezel, glass, snorkel])
    sc.nodes["pu_swim"] = [p.rotate_xyz(ry=24.0, degrees=True) for p in sc.nodes["pu_swim"]]
    return _finalize_item(sc, target_span=0.92)


def build_pu_mason() -> SceneBuilder:
    """Fire / Builder Power-Up: glossy ribbed yellow safety helmet with crimson visor stripe (`66:pu_mason`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_pu_mason")

    def _helmet_dome(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        p[:, 1] = np.where(p[:, 1] < 0.0, p[:, 1] * 0.12, p[:, 1])
        # Signature hard-hat top stiffening ribs (`66:pu_mason`)!
        rib = 0.022 * np.exp(-((p[:, 0] / 0.036) ** 2)) + 0.014 * np.exp(-(((np.abs(p[:, 0]) - 0.10) / 0.028) ** 2))
        p[:, 1] += rib * np.clip(p[:, 1] / 0.16, 0.0, 1.0)
        return p

    shell = gradient_y_tint(
        superellipsoid(0.26, 0.22, 0.30, e1=0.80, e2=0.92, u_segs=34, v_segs=20, name="mason_dome", material="item_gear_gold")
        .deform(_helmet_dome)
        .translate(0.0, 0.06, 0.0),
        (0.84, 0.68, 0.24),
        (1.04, 1.00, 0.88),
    )
    # Flared brim + extended front safety bill with crimson reflective visor accent!
    brim = cylinder(0.31, 0.32, 0.028, radial_segs=34, bevel=0.008, axis="y", name="mason_brim", material="item_gear_gold").scale(0.95, 1.0, 1.15).translate(0.0, 0.048, +0.02)
    visor = rounded_box(0.24, 0.038, 0.10, bevel=0.012, segs=2, name="mason_visor", material="item_bomb_red").rotate_xyz(rx=0.18).translate(0.0, 0.072, +0.285)
    sc.add("pu_mason", [shell, brim, visor])
    sc.nodes["pu_mason"] = [p.rotate_xyz(ry=32.0, degrees=True) for p in sc.nodes["pu_mason"]]
    return _finalize_item(sc, target_span=0.92)


# ---------------------------------------------------------------------------
# All 4 Team Planted Bombs (`129..132`) & Blazing Wall Fire (`134:wallup04`)
# ---------------------------------------------------------------------------

def _build_team_bomb(name: str, body_mat: str, accent_mat: str) -> SceneBuilder:
    """Team-colored cylindrical planted bomb with beveled purple collar, curving fuse & live animated spark."""
    register_item_materials()
    sc = SceneBuilder(name=name)

    # Tapered cartoon dynamite canister with bold team stripes & dark metal bottom cap
    base_cap = cylinder(0.118, 0.106, 0.055, radial_segs=26, bevel=0.008, axis="y", name=f"{name}_base", material=accent_mat).translate(0.0, 0.028, 0.0)
    canister = cylinder(0.132, 0.118, 0.270, radial_segs=28, bevel=0.010, axis="y", name=f"{name}_body", material=body_mat).translate(0.0, 0.185, 0.0)
    # Iconic wide purple top bevel flange (`129..132`)!
    collar = cylinder(0.154, 0.140, 0.088, radial_segs=28, bevel=0.014, axis="y", name=f"{name}_collar", material="item_bomb_purple").translate(0.0, 0.340, 0.0)
    plug = cylinder(0.042, 0.046, 0.034, radial_segs=16, bevel=0.005, axis="y", name=f"{name}_plug", material="item_steel_dark").translate(0.0, 0.396, 0.0)
    fuse = curved_tube(
        [(0.0, 0.405, 0.0), (0.015, 0.485, 0.01), (0.055, 0.545, 0.02), (0.105, 0.525, 0.01)],
        [0.014, 0.013, 0.012, 0.010],
        samples_per_seg=4,
        radial_segs=10,
        name=f"{name}_fuse",
        material="item_fuse_cord",
    )
    sc.add("bomb_root", [base_cap, canister, collar, plug, fuse])

    # Sizzling animated yellow-orange spark starburst mounted on its own child node for pulsing!
    spark_core = sphere(0.032, u_segs=14, v_segs=10, name=f"{name}_spark_core", material="item_flame_core")
    spark_rays: List[MeshPart] = [spark_core]
    for k in range(6):
        ang = 2.0 * math.pi * k / 6.0
        ray = cone(0.012, 0.055, radial_segs=8, name=f"{name}_ray_{k}", material="item_flame_outer").rotate_xyz(rx=math.pi * 0.5, rz=ang).translate(0.032 * math.sin(ang), 0.032 * math.cos(ang), 0.0)
        spark_rays.append(ray)
    sc.add("bomb_spark", spark_rays)
    sc.set_node_parent("bomb_spark", "bomb_root")
    sc.set_node_transform("bomb_spark", translation=(0.105, 0.525, 0.01))

    # Crisp 2-frame authentic sizzling spark flicker matching `129..132`!
    times = [0.00, 0.12, 0.24, 0.36]
    sc.add_animation(
        "Idle",
        [
            AnimationChannel("bomb_spark", "scale", times, [(0.88, 0.88, 0.88), (1.28, 1.28, 1.28), (0.94, 0.94, 0.94), (0.88, 0.88, 0.88)]),
            AnimationChannel("bomb_spark", "rotation", times, [quat_from_euler_xyz(rz=a) for a in (0.0, 0.55, 1.10, 0.0)]),
        ],
    )
    return _finalize_item(sc, target_span=0.92)


def build_redbomb() -> SceneBuilder:
    return _build_team_bomb("item_redbomb", "item_bomb_red", "item_steel_dark")


def build_greenbomb() -> SceneBuilder:
    return _build_team_bomb("item_greenbomb", "item_bomb_green", "item_bomb_red")


def build_blackbomb() -> SceneBuilder:
    return _build_team_bomb("item_blackbomb", "item_bomb_black", "item_bomb_red")


def build_bluebomb() -> SceneBuilder:
    return _build_team_bomb("item_bluebomb", "item_bomb_blue", "item_bomb_red")


def build_wallup04() -> SceneBuilder:
    """Blazing multi-tongue sculpted 3D Wall Fire flame with glowing core & live flicker clip (`134:wallup04`)."""
    register_item_materials()
    sc = SceneBuilder(name="item_wallup04")

    def _flame_tongue(name: str, *, radius: float, height: float, bend_x: float, bend_z: float, mat: str) -> MeshPart:
        def _sculpt_tongue(pos: np.ndarray) -> np.ndarray:
            p = pos.copy()
            u = np.clip((p[:, 1] + height * 0.5) / max(1e-5, height), 0.0, 1.0)
            # Bulbous glowing base tapering into a sharp licking S-curved flame tip!
            taper = (math.sin(0.25) + 0.95 * np.sin(u * math.pi * 0.88)) * (1.0 - 0.88 * (u ** 1.55))
            p[:, 0] *= np.maximum(0.02, taper)
            p[:, 2] *= np.maximum(0.02, taper)
            p[:, 0] += bend_x * (u ** 1.7) + 0.028 * np.sin(u * math.pi * 2.0) * u
            p[:, 2] += bend_z * (u ** 1.7)
            p[:, 1] = u * height
            return p

        return ellipsoid(radius, height * 0.5, radius * 0.86, u_segs=22, v_segs=18, name=name, material=mat).deform(_sculpt_tongue)

    # Incandescent bright white-gold inner fire heart gleaming boldly through the orange tongues (`134:wallup04`)!
    sc.add("flame_core", [
        _flame_tongue("flame_heart_c", radius=0.175, height=0.58, bend_x=-0.02, bend_z=0.02, mat="item_flame_core").translate(0.0, 0.0, +0.03),
        _flame_tongue("flame_heart_l", radius=0.120, height=0.44, bend_x=-0.11, bend_z=0.04, mat="item_flame_core").translate(-0.08, 0.0, +0.03),
        _flame_tongue("flame_heart_r", radius=0.120, height=0.46, bend_x=+0.11, bend_z=0.02, mat="item_flame_core").translate(+0.08, 0.0, +0.02),
    ])

    # Vivid dancing orange-amber outer flame crown tongues (`134:wallup04`)!
    outer_specs = (
        ("c", 0.00, 0.00, 0.21, 0.68, +0.03, -0.02),
        ("l1", -0.16, +0.04, 0.14, 0.50, -0.14, +0.04),
        ("r1", +0.16, +0.03, 0.14, 0.52, +0.15, +0.03),
        ("l2", -0.10, -0.09, 0.13, 0.44, -0.10, -0.10),
        ("r2", +0.10, -0.08, 0.13, 0.46, +0.11, -0.09),
        ("f", 0.00, +0.12, 0.12, 0.36, -0.04, +0.10),
    )
    outer_parts = [
        _flame_tongue(f"flame_outer_{tag}", radius=rad, height=h, bend_x=bx, bend_z=bz, mat="item_flame_outer").translate(x, 0.0, z)
        for tag, x, z, rad, h, bx, bz in outer_specs
    ]
    sc.add("flame_outer", outer_parts)

    # Lively dancing flame flicker animation!
    times = [0.00, 0.10, 0.20, 0.30, 0.40]
    sc.add_animation(
        "Idle",
        [
            AnimationChannel("flame_outer", "scale", times, [(1.00, 1.00, 1.00), (1.06, 1.12, 1.04), (0.95, 0.92, 0.96), (1.04, 1.08, 1.02), (1.00, 1.00, 1.00)]),
            AnimationChannel("flame_outer", "rotation", times, [quat_from_euler_xyz(rz=a, ry=b) for a, b in ((0.0, 0.0), (0.05, 0.10), (-0.05, -0.08), (0.03, 0.06), (0.0, 0.0))]),
            AnimationChannel("flame_core", "scale", times, [(1.00, 1.00, 1.00), (0.94, 1.08, 0.94), (1.05, 0.94, 1.05), (0.97, 1.06, 0.97), (1.00, 1.00, 1.00)]),
        ],
    )
    return _finalize_item(sc, target_span=0.92)
