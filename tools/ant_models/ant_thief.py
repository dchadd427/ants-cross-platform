"""Sculpted high-detail 3D model for the iconic Rogue Bandit Thief Ant.

Built on `base_ant` (keeping its broad heart-shield head, chunky lobster-claw
mandibles, and flowing ball-free legs) with Thief's signature full domino
bandit mask, rogue gear, and extreme shifty lookout eyes:
- One single unified continuous sculpted woven-gold domino mask + headband
  shell (`gear_gold`) ray-cast flush across the forehead, nose bridge, upper
  cheeks, temples, and back, with two clean softly-beveled oval eye cutouts
  punched directly out of the single watertight cloth surface (zero separate
  bolted-on strips or seam cracks)
- Plump side knot on the left temple (`+X`) with two large, sweeping, pleated
  cloth tails flaring boldly back-left across every camera angle
- Extreme shifty lookout eyes: trimmed spherical sclera (`#148`) with huge
  curved spherical-cap olive irises (~72%) and dark glossy pupils (~46%) on
  pivoted `eye_ball_left` / `eye_ball_right` nodes rotating `+-0.62 rad` in
  the `"IdleLookout"` glTF 2.0 clip, plus spherical eyelids with guaranteed
  `>= +0.007` radial clearance across all blink and gaze frames
- Rogue utility gear: dark leather crossbody strap (`gear_dark`) with brass
  chest buckle (`gear_gold_metal` / `gear_brass_trim`) carrying a compact
  cinched brown canvas loot pouch (`prop_brown` + `gear_gold` drawstring +
  brass clasp) slung neatly at the lower right-back hip

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet grounded cleanly at Y = 0.0)
  +Z = front (South / canonical camera 301)
  +X = ant's left (viewer's right from front), -X = ant's right
"""
from __future__ import annotations

import math
from typing import Dict, List, Sequence, Tuple

import numpy as np

try:
    from .base_ant import EYELID_CLOSED_ANGLE, build_base_ant
    from .mesh_core import (
        AnimationChannel,
        MeshPart,
        SceneBuilder,
        catmull_rom_spline,
        curved_tube,
        ellipsoid,
        hemisphere_shell,
        lathe,
        quat_from_axis_angle,
        quat_from_euler_xyz,
        quat_mul,
        recompute_smooth_normals,
        rounded_box,
        sphere,
        superellipsoid,
        swept_tube,
        torus_arc,
    )
except ImportError:
    from base_ant import EYELID_CLOSED_ANGLE, build_base_ant  # type: ignore
    from mesh_core import (  # type: ignore
        AnimationChannel,
        MeshPart,
        SceneBuilder,
        catmull_rom_spline,
        curved_tube,
        ellipsoid,
        hemisphere_shell,
        lathe,
        quat_from_axis_angle,
        quat_from_euler_xyz,
        quat_mul,
        recompute_smooth_normals,
        rounded_box,
        sphere,
        superellipsoid,
        swept_tube,
        torus_arc,
    )

# Calibrated clean lookout gaze (+-19.5 deg): both eyeballs track harmoniously together across
# the full IdleLookout sweep with 100% of iris/pupil/catchlights visible on the spherical sclera
# and zero clipping into the skull, socket rim, or domino mask nose bridge.
GAZE_ANGLE = 0.34

# Clean balanced resting eyelid angle (symmetrical, zero crooked roll); tucked neatly under
# the top rim of the domino mask at rest and sealing 100% cleanly on Blink.
_LID_REST_DROOP = 0.0
_THIEF_BLINK_ANGLE = EYELID_CLOSED_ANGLE + 0.04

# Stealthy low hiding crouch (`#162`) tuned with `core`'s 1998 anatomy & Thief's sleek skull (H ~ 0.86).
THIEF_CROUCH = 0.138

# Nimble asymmetric rogue arm pose anchored cleanly at `core`'s narrow petite shoulders (±0.078, 0.456 - c, 0.014).
_THIEF_ARM_LEFT = [
    (0.078, 0.456 - THIEF_CROUCH, 0.014),
    (0.148, 0.422 - THIEF_CROUCH, 0.072),
    (0.132, 0.426 - THIEF_CROUCH, 0.160),
    (0.104, 0.438 - THIEF_CROUCH, 0.192),
]
_THIEF_ARM_RIGHT = [
    (-0.078, 0.456 - THIEF_CROUCH, 0.014),
    (-0.156, 0.424 - THIEF_CROUCH, 0.054),
    (-0.142, 0.370 - THIEF_CROUCH, 0.132),
    (-0.118, 0.362 - THIEF_CROUCH, 0.164),
]

_U_SEGS = 72
_V_SEGS = 10


def _normalize(v: np.ndarray, fallback: Tuple[float, float, float] = (0.0, 1.0, 0.0)) -> np.ndarray:
    n = float(np.linalg.norm(v))
    return v / n if n > 1e-10 else np.asarray(fallback, dtype=np.float64)


def _direction(theta: float, elev: float) -> np.ndarray:
    """Unit direction from head center (`theta=0` is +Z front, `+pi/2` is +X ant's left)."""
    return np.array([math.sin(theta) * math.cos(elev), math.sin(elev), math.cos(theta) * math.cos(elev)])


class _CraniumCaster:
    """Ray-cast the base cranium mesh from head center so mask/headband always hugs flush."""

    def __init__(self, cranium: MeshPart, center: Sequence[float]):
        tri = cranium.positions.astype(np.float64)[cranium.indices.reshape(-1, 3)]
        self.center = np.asarray(center, dtype=np.float64)
        self.v0 = tri[:, 0] - self.center
        self.e1 = tri[:, 1] - tri[:, 0]
        self.e2 = tri[:, 2] - tri[:, 0]

    def radius(self, d: np.ndarray) -> float:
        h = np.cross(d[None, :], self.e2)
        a = np.einsum("ij,ij->i", self.e1, h)
        ok = np.abs(a) > 1e-12
        f = np.where(ok, 1.0 / np.where(ok, a, 1.0), 0.0)
        s = -self.v0
        u = f * np.einsum("ij,ij->i", s, h)
        q = np.cross(s, self.e1)
        v = f * (q @ d)
        t = f * np.einsum("ij,ij->i", self.e2, q)
        hit = ok & (u >= -1e-6) & (v >= -1e-6) & (u + v <= 1.0 + 1e-6) & (t > 0.0)
        return float(np.max(t[hit])) if np.any(hit) else 0.23


def _thief_head_deform(pos: np.ndarray) -> np.ndarray:
    """Sculpt Thief's sleek, angular roguish ant head (neither a round ball nor a flat pancake).

    Subtly lowers the crown into a taut organic dome, sharpens the temple planes and wide
    roguish cheekbones, and sweeps down into a sleek tapered insectoid jaw framing the
    lobster-claw mandibles (avoiding both bowling-ball roundness and a pointy alien chin).
    """
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    ax = np.abs(x)
    front_mask = np.clip((z + 0.01) / 0.13, 0.0, 1.0)

    # 1. Sleek lowered crown (gentle natural arch — removes the tall round dome without pancaking).
    crown = np.clip((y - 0.082) / 0.080, 0.0, 1.0)
    p[:, 1] -= 0.007 * (crown ** 1.65)
    p[:, 0] *= 1.0 - 0.055 * (crown ** 1.75)
    p[:, 2] *= 1.0 - 0.055 * (crown ** 1.75)

    # 2. Wider roguish brow/cheekbones + crisper, subtly flatter side temple planes.
    brow_flare = np.exp(-(((y - 0.016) / 0.064) ** 2)) * np.clip((ax - 0.07) / 0.11, 0.0, 1.0)
    p[:, 0] += np.sign(x) * 0.009 * brow_flare
    temple_flat = np.clip((ax - 0.140) / 0.055, 0.0, 1.0) * np.exp(-(((y - 0.032) / 0.075) ** 2))
    p[:, 0] -= np.sign(x) * 0.009 * temple_flat

    # 3. Sleek angled roguish brow ridge, subtle forehead Y-suture, and deeper recessed eye sockets.
    brow_y = 0.058 + 0.065 * np.minimum(ax, 0.18)
    brow_shelf = np.exp(-(((y - brow_y) / 0.028) ** 2)) * np.clip((z - 0.02) / 0.12, 0.0, 1.0)
    p[:, 2] += 0.012 * brow_shelf

    socket = (
        np.exp(-(((ax - 0.083) / 0.064) ** 2 + ((y + 0.004) / 0.058) ** 2))
        * np.clip((z - 0.02) / 0.12, 0.0, 1.0)
    )
    p[:, 2] -= 0.028 * socket

    seam_stem = np.exp(-((ax / 0.014) ** 2)) * np.exp(-(((y - 0.060) / 0.048) ** 2)) * front_mask
    branch_x = 0.014 + np.clip(y - 0.064, 0.0, 0.09) * 0.92
    seam_branch = (
        np.exp(-(((ax - branch_x) / 0.014) ** 2))
        * np.clip((y - 0.060) / 0.020, 0.0, 1.0)
        * np.clip((0.145 - y) / 0.036, 0.0, 1.0)
        * np.clip((z + 0.02) / 0.12, 0.0, 1.0)
    )
    radial = p / np.maximum(1e-6, np.linalg.norm(p, axis=1, keepdims=True))
    p -= (0.0052 * np.maximum(seam_stem, 0.68 * seam_branch))[:, None] * radial

    # 4. Crisp roguish cheekbones tapering cleanly into a sleek angled ant jaw (zero round cheeks, zero alien chin!).
    cheek = (
        np.exp(-(((ax - 0.124) / 0.052) ** 2 + ((y + 0.028) / 0.040) ** 2))
        * np.clip((z + 0.02) / 0.12, 0.0, 1.0)
    )
    p[:, 0] += np.sign(x) * 0.0085 * cheek
    p[:, 2] += 0.007 * cheek

    lower = np.clip((-y - 0.020) / 0.142, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.25 * (lower ** 1.38)
    p[:, 2] *= 1.0 - 0.14 * (lower ** 1.38)

    clypeus = np.exp(-((x / 0.042) ** 2)) * np.exp(-(((y + 0.088) / 0.038) ** 2)) * front_mask
    p[:, 2] += 0.011 * clypeus

    # 5. Clean aerodynamic occipital taper behind the lower skull into the neck (sleek 901 E side profile!).
    rear_low = np.clip((-z - 0.01) / 0.14, 0.0, 1.0) * np.clip((-y + 0.03) / 0.17, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.16 * rear_low
    p[:, 2] += 0.020 * rear_low
    p[:, 1] += 0.014 * rear_low
    return p


def _paint_thief_cranium_colors(cranium: MeshPart) -> MeshPart:
    """Paint warm anatomical face shading onto Thief's sleek cranium (works across all teams)."""
    p = cranium.positions.astype(np.float64)
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    ax = np.abs(x)
    front_mask = np.clip((z - 0.01) / 0.12, 0.0, 1.0)
    socket_ring = (
        np.exp(-(((ax - 0.083) / 0.064) ** 2 + ((y + 0.004) / 0.060) ** 2))
        * np.clip((z - 0.02) / 0.11, 0.0, 1.0)
    )
    seam_stem = np.exp(-((ax / 0.016) ** 1.5)) * np.exp(-(((y - 0.060) / 0.048) ** 2)) * front_mask
    mouth_shadow = np.exp(-((ax / 0.064) ** 2 + ((y + 0.118) / 0.040) ** 2)) * front_mask
    cheek_hi = np.exp(-(((ax - 0.116) / 0.056) ** 2 + ((y + 0.036) / 0.042) ** 2)) * front_mask
    brow_hi = np.exp(-(((ax - 0.083) / 0.054) ** 2 + ((y - 0.066) / 0.026) ** 2)) * front_mask
    lum = (
        0.89
        + 0.09 * cheek_hi
        + 0.08 * brow_hi
        - 0.24 * socket_ring
        - 0.14 * seam_stem
        - 0.14 * mouth_shadow
    )
    lum = np.clip(lum, 0.62, 1.0).astype(np.float32)
    cranium.colors = np.column_stack([lum, lum, lum]).astype(np.float32)
    return cranium


def _refit_thief_head(scene: SceneBuilder, anchors: dict) -> MeshPart:
    """Replace `base_ant`'s rounder cranium with Thief's sleek tapered roguish skull, refit sockets,
    and sweep antennae cleanly UP-AND-BACKWARD (`-Z`) matching `atst901`/`atst801`!
    """
    hx, hy, hz = anchors["head_center"]
    s_h = 1.12
    cranium = (
        superellipsoid(0.194, 0.161, 0.156, e1=0.68, e2=0.68, u_segs=38, v_segs=30, name="cranium", material="chitin_team")
        .deform(_thief_head_deform)
    )
    cranium = _paint_thief_cranium_colors(cranium).scale(s_h, s_h, s_h).translate(hx, hy, hz)

    # Swap cranium inside `head` (vertices are in canonical neutral space; `head` transform applies -neck)
    # and seat `core`'s enlarged plump mandibles flush beneath Thief's sleek tapered cheeks.
    jaw_dy, jaw_dz = 0.019, -0.019
    head_parts = scene.get_node("head")
    new_head_parts: List[MeshPart] = []
    for p in head_parts:
        if p.name == "cranium":
            new_head_parts.append(cranium)
        elif p.name == "mouth_cleft":
            new_head_parts.append(p.translate(0.0, jaw_dy, jaw_dz))
        else:
            new_head_parts.append(p)
    scene.nodes["head"] = new_head_parts

    mand_xf = scene.get_node_transform("mandibles")
    if mand_xf is not None:
        mx, my, mz = mand_xf.translation
        scene.set_node_transform(
            "mandibles",
            translation=(mx, my + jaw_dy, mz + jaw_dz),
            rotation=mand_xf.rotation,
            scale=mand_xf.scale,
        )

    # Rebuild Thief's antennae sweeping cleanly UP-AND-BACKWARD (`-Z`, streamlined stealth ears
    # with chunky clubbed distal tips matching `atst901`/`atst801`/`atst201`) on Thief's sleek crown!
    neck_vec = np.asarray(anchors["neck_center"], dtype=np.float64)
    ant_rads = [
        (0.020, 0.020),
        (0.016, 0.017),
        (0.015, 0.016),
        (0.020, 0.022),
        (0.028, 0.030),
        (0.034, 0.036),
        (0.024, 0.026),
    ]
    ant_defs = (
        (
            "antenna_left",
            "left",
            (+0.096, hy + 0.148, hz + 0.026),
            [
                (-0.004, -0.018, 0.002),
                (0.000, 0.000, 0.000),
                (+0.024, +0.072, -0.032),
                (+0.044, +0.136, -0.078),
                (+0.054, +0.172, -0.126),
                (+0.058, +0.176, -0.164),
                (+0.060, +0.160, -0.184),
            ],
        ),
        (
            "antenna_right",
            "right",
            (-0.096, hy + 0.148, hz + 0.024),
            [
                (+0.004, -0.018, 0.002),
                (0.000, 0.000, 0.000),
                (-0.028, +0.064, -0.040),
                (-0.052, +0.122, -0.094),
                (-0.064, +0.150, -0.144),
                (-0.068, +0.148, -0.178),
                (-0.070, +0.132, -0.196),
            ],
        ),
    )
    for node_name, tag, base_w, local_pts in ant_defs:
        stalk = swept_tube(
            local_pts,
            ant_rads,
            radial_segs=14,
            smooth_subdiv=3,
            cap_rings=4,
            name=f"antenna_stalk_{tag}",
            material="chitin_dark",
        )
        socket = ellipsoid(0.028, 0.015, 0.028, u_segs=12, v_segs=8, name=f"antenna_socket_{tag}", material="chitin_dark").translate(0.0, -0.004, 0.0)
        scene.nodes[node_name] = [stalk, socket]
        rel = np.asarray(base_w, dtype=np.float64) - neck_vec
        scene.set_node_transform(node_name, translation=(float(rel[0]), float(rel[1]), float(rel[2])))

    # Refit eye anchors cleanly forward of the scaled sleek orbital hollow so the spherical eyes
    # sit proudly inside the domino mask with zero cranium or nose-bridge clipping at any gaze angle.
    eye_y = hy - 0.005
    eye_z = hz + 0.138
    anchors["eye_left"] = (0.094, eye_y, eye_z)
    anchors["eye_right"] = (-0.094, eye_y, eye_z)
    anchors["eye_center"] = (0.0, eye_y, eye_z + 0.013)
    return cranium


def _mask_v_limits(theta: float) -> Tuple[float, float]:
    """Outer bottom/top elevation bounds for Thief's sleek, tailored domino mask + headband.

    Wraps cleanly across the lower brow (revealing Thief's sleek tapered green ant crown
    above), sweeps cleanly beneath both eyes with a tailored domino cheek dip clearing the
    mandibles, and tapers smoothly into a fitted wrap around the temples and back.
    """
    a = abs(math.atan2(math.sin(theta), math.cos(theta)))
    v_top = 0.425 + 0.075 * math.exp(-((a / 1.05) ** 2))
    cheek_dip = -0.295 * math.exp(-(((a - 0.58) / 0.38) ** 2))
    nose_dip = -0.130 * math.exp(-((a / 0.25) ** 2))
    v_bot = -0.068 + cheek_dip + nose_dip
    return v_bot, v_top


def _spherical_eye_cap(
    radius: float,
    ang_x: float,
    ang_y: float,
    thickness: float,
    *,
    u_segs: int = 24,
    v_segs: int = 6,
    name: str,
    material: str,
) -> MeshPart:
    """Closed curved elliptical cap hugging a sphere of `radius` around +Z (zero flat rim poke-out)."""
    phis = np.linspace(0.0, 1.0, v_segs + 1)[1:]
    thetas = np.linspace(0.0, 2.0 * math.pi, u_segs, endpoint=False)
    r_out = radius + thickness
    r_in = max(0.002, radius - 0.003)

    def _cap_pts(r_surf: float) -> List[np.ndarray]:
        pts = [np.array([0.0, 0.0, r_surf])]
        for s in phis:
            for th in thetas:
                ax = s * ang_x * math.cos(th)
                ay = s * ang_y * math.sin(th)
                ang = math.hypot(ax, ay)
                sa = math.sin(ang) / max(1e-8, ang)
                pts.append(np.array([r_surf * sa * ax, r_surf * sa * ay, r_surf * math.cos(ang)]))
        return pts

    outer = _cap_pts(r_out)
    inner = _cap_pts(r_in)
    n_cap = len(outer)
    positions = np.asarray(outer + inner, dtype=np.float32)

    tris: List[Tuple[int, int, int]] = []
    # Outer surface fan + rings (+Z outward)
    for k in range(u_segs):
        nk = (k + 1) % u_segs
        tris.append((0, 1 + k, 1 + nk))
    for r in range(v_segs - 1):
        b0, b1 = 1 + r * u_segs, 1 + (r + 1) * u_segs
        for k in range(u_segs):
            nk = (k + 1) % u_segs
            tris.append((b0 + k, b1 + k, b1 + nk))
            tris.append((b0 + k, b1 + nk, b0 + nk))
    # Inner back surface (reversed winding)
    for k in range(u_segs):
        nk = (k + 1) % u_segs
        tris.append((n_cap, n_cap + 1 + nk, n_cap + 1 + k))
    for r in range(v_segs - 1):
        b0, b1 = n_cap + 1 + r * u_segs, n_cap + 1 + (r + 1) * u_segs
        for k in range(u_segs):
            nk = (k + 1) % u_segs
            tris.append((b0 + k, b1 + nk, b1 + k))
            tris.append((b0 + k, b0 + nk, b1 + nk))
    # Outer rim seam closing the cap into a watertight solid
    bo = 1 + (v_segs - 1) * u_segs
    bi = n_cap + 1 + (v_segs - 1) * u_segs
    for k in range(u_segs):
        nk = (k + 1) % u_segs
        tris.append((bo + k, bi + k, bi + nk))
        tris.append((bo + k, bi + nk, bo + nk))

    indices = np.asarray(tris, dtype=np.uint32).reshape(-1)
    return MeshPart(name, material, positions, recompute_smooth_normals(positions, indices), indices).ensure_outward()


def _build_unified_domino_mask(
    scene: SceneBuilder,
    caster: _CraniumCaster,
    anchors: dict,
    lid_outer_r: float,
) -> None:
    """Build ONE single continuous watertight woven-gold domino mask + headband shell.

    Uses an exact 3-patch vertex-welded topology (left eye patch + mirrored right eye patch +
    rear wrap band) with two smooth oval eye cutouts punched directly out of the continuous
    cloth surface and soft rolled cloth bevels around all 4 rims (zero bolted-on pieces!).
    """
    hc = caster.center
    eye_l = np.asarray(anchors["eye_left"], dtype=np.float64)
    rel_eye = eye_l - hc
    u_eye = math.atan2(float(rel_eye[0]), float(rel_eye[2]))
    v_eye = math.atan2(float(rel_eye[1]), math.hypot(float(rel_eye[0]), float(rel_eye[2])))

    u_outer = 1.18
    n_side = 12
    m_ring = 4 * n_side
    n_rad = 6
    n_back = 36

    # Build ordered CCW perimeter around Left Eye Patch [0, u_outer] x [v_bot(u), v_top(u)].
    perim_u = np.zeros(m_ring, dtype=np.float64)
    perim_v = np.zeros(m_ring, dtype=np.float64)
    vb_out, vt_out = _mask_v_limits(u_outer)
    vb_mid, vt_mid = _mask_v_limits(0.0)
    for k in range(n_side):
        f = k / n_side
        # Side 0: outer temple u = u_outer, rising bottom -> top
        perim_u[k] = u_outer
        perim_v[k] = vb_out + f * (vt_out - vb_out)
        # Side 1: top brow hem, sweeping u_outer -> 0
        u1 = u_outer * (1.0 - f)
        perim_u[n_side + k] = u1
        perim_v[n_side + k] = _mask_v_limits(u1)[1]
        # Side 2: center nose bridge u = 0, falling top -> bottom
        perim_u[2 * n_side + k] = 0.0
        perim_v[2 * n_side + k] = vt_mid + f * (vb_mid - vt_mid)
        # Side 3: bottom cheek hem, sweeping 0 -> u_outer
        u3 = u_outer * f
        perim_u[3 * n_side + k] = u3
        perim_v[3 * n_side + k] = _mask_v_limits(u3)[0]

    # Solve exact smooth 3D clearance curve around the left eye + spherical eyelid shell.
    target_clear = lid_outer_r + 0.0025
    hole_s = np.zeros(m_ring, dtype=np.float64)
    ray_du = np.zeros(m_ring, dtype=np.float64)
    ray_dv = np.zeros(m_ring, dtype=np.float64)
    for k in range(m_ring):
        du = perim_u[k] - u_eye
        dv = perim_v[k] - v_eye
        dist_perim = max(1e-6, math.hypot(du, dv))
        du_n, dv_n = du / dist_perim, dv / dist_perim
        ray_du[k], ray_dv[k] = du_n, dv_n

        # Binary search smallest angular radius clearing the 3D eyelid sphere cleanly.
        lo, hi = 0.24, min(0.56, dist_perim * 0.74)
        for _ in range(10):
            mid = 0.5 * (lo + hi)
            uu, vv = u_eye + mid * du_n, v_eye + mid * dv_n
            d = _direction(uu, vv)
            p_surf = hc + d * (caster.radius(d) + 0.0085)
            if float(np.linalg.norm(p_surf - eye_l)) < target_clear:
                lo = mid
            else:
                hi = mid
        # Keep lush continuous bridge (>= 0.125 rad) and under-eye cheek strip cleanly clear.
        max_s = dist_perim * 0.76
        if du_n < -1e-4:
            max_s = min(max_s, (u_eye - 0.125) / (-du_n))
        hole_s[k] = min(hi, max(0.22, max_s))

    # Light circular smoothing so the punched oval eye cutout is silky-smooth with zero kinks.
    for _ in range(3):
        hole_s = 0.25 * np.roll(hole_s, 1) + 0.50 * hole_s + 0.25 * np.roll(hole_s, -1)
    hole_u = u_eye + hole_s * ray_du
    hole_v = v_eye + hole_s * ray_dv

    # Weld all 3 surface regions into one shared vertex/triangle table via exact quantized (u, v) keys.
    vert_map: Dict[Tuple[int, int], int] = {}
    uv_list: List[Tuple[float, float]] = []
    is_boundary_hint: List[float] = []  # 0 at rim, 1 in cloth interior (for plush edge rounding)

    def _get_vert(u: float, v: float, interior: float) -> int:
        # Wrap periodic u cleanly into (-pi, pi] so u = +u_outer and u = -u_outer match seamlessly.
        uw = math.atan2(math.sin(u), math.cos(u))
        key = (int(round(uw * 100000.0)), int(round(v * 100000.0)))
        idx = vert_map.get(key)
        if idx is None:
            idx = len(uv_list)
            vert_map[key] = idx
            uv_list.append((uw, v))
            is_boundary_hint.append(interior)
        else:
            is_boundary_hint[idx] = min(is_boundary_hint[idx], interior)
        return idx

    surf_tris: List[Tuple[int, int, int]] = []

    def _add_eye_patch(mirror: bool) -> None:
        grid = np.zeros((n_rad + 1, m_ring), dtype=np.int32)
        for r in range(n_rad + 1):
            t = r / n_rad
            # Smooth radial easing gives denser quads right around the rolled eye opening.
            te = 0.55 * t + 0.45 * (t * t)
            for k in range(m_ring):
                u = (1.0 - te) * hole_u[k] + te * perim_u[k]
                v = (1.0 - te) * hole_v[k] + te * perim_v[k]
                # Interior metric: 0 on eye cutout & top/bottom rims, 1 across bridge/temple interiors.
                on_top_bot = (n_side <= k <= 2 * n_side) or (k >= 3 * n_side) or (k == 0)
                rim_dist = min(t, (1.0 - t) if on_top_bot else 1.0)
                grid[r, k] = _get_vert(-u if mirror else u, v, min(1.0, rim_dist * 3.0))
        for r in range(n_rad):
            for k in range(m_ring):
                nk = (k + 1) % m_ring
                a, b, c, d_i = int(grid[r, k]), int(grid[r, nk]), int(grid[r + 1, nk]), int(grid[r + 1, k])
                # In (u, v), k sweeps CCW while r sweeps outward, so (a, c, b) faces strictly outward (+d).
                if mirror:
                    surf_tris.append((a, b, c))
                    surf_tris.append((a, c, d_i))
                else:
                    surf_tris.append((a, c, b))
                    surf_tris.append((a, d_i, c))

    _add_eye_patch(mirror=False)
    _add_eye_patch(mirror=True)

    # Back & outer-temple wrap band seamlessly welding u = +u_outer around back to u = 2*pi - u_outer.
    back_us = np.linspace(u_outer, 2.0 * math.pi - u_outer, n_back + 1)
    back_grid = np.zeros((n_back + 1, n_side + 1), dtype=np.int32)
    for ci, u in enumerate(back_us):
        vb, vt = _mask_v_limits(float(u))
        for ri in range(n_side + 1):
            f = ri / n_side
            v = vb + f * (vt - vb)
            back_grid[ci, ri] = _get_vert(float(u), v, min(1.0, min(f, 1.0 - f) * 3.5))
    for ci in range(n_back):
        for ri in range(n_side):
            a = int(back_grid[ci, ri])
            b = int(back_grid[ci + 1, ri])
            c = int(back_grid[ci + 1, ri + 1])
            d_i = int(back_grid[ci, ri + 1])
            surf_tris.append((a, b, c))
            surf_tris.append((a, c, d_i))

    # Project every unique mask vertex onto the cranium + bridge forehead crease for taut cloth drape.
    n_surf = len(uv_list)
    dirs = np.zeros((n_surf, 3), dtype=np.float64)
    r_raw = np.zeros(n_surf, dtype=np.float64)
    for i, (u, v) in enumerate(uv_list):
        d = _direction(u, v)
        dirs[i] = d
        r_raw[i] = caster.radius(d)

    # Light taut cloth bridging (2 passes) so the fitted mask hugs & reveals the sleeker skull contours!
    adj: List[List[int]] = [[] for _ in range(n_surf)]
    for a, b, c in surf_tris:
        adj[a].extend((b, c))
        adj[b].extend((a, c))
        adj[c].extend((a, b))
    r_bridged = r_raw.copy()
    for _ in range(2):
        nxt = r_bridged.copy()
        for i in range(n_surf):
            if adj[i]:
                nxt[i] = max(r_raw[i] + 0.0012, 0.65 * r_bridged[i] + 0.35 * float(np.mean(r_bridged[adj[i]])))
        r_bridged = nxt

    pos_out = np.zeros((n_surf, 3), dtype=np.float64)
    pos_in = np.zeros((n_surf, 3), dtype=np.float64)
    for i, (u, v) in enumerate(uv_list):
        edge_w = max(0.0, min(1.0, is_boundary_hint[i])) ** 0.55
        fold = 0.0014 * math.sin(v * 14.0 + 0.6 * math.cos(u)) * edge_w
        thick = 0.0062 + 0.0040 * edge_w + fold
        pos_out[i] = hc + dirs[i] * (r_bridged[i] + thick)
        pos_in[i] = hc + dirs[i] * (r_bridged[i] - 0.0065)

    # Guarantee EVERY single outer-surface triangle faces strictly outward along +dirs.
    t_arr = np.asarray(surf_tris, dtype=np.int32)
    tri_n = np.cross(pos_out[t_arr[:, 1]] - pos_out[t_arr[:, 0]], pos_out[t_arr[:, 2]] - pos_out[t_arr[:, 0]])
    tri_d = dirs[t_arr[:, 0]] + dirs[t_arr[:, 1]] + dirs[t_arr[:, 2]]
    flipped = np.einsum("ij,ij->i", tri_n, tri_d) < 0.0
    if np.any(flipped):
        t_arr[flipped] = t_arr[flipped][:, [0, 2, 1]]

    # Automatically discover ALL open boundary half-edges (top hem, bottom hem, left/right eye cutouts)
    # and seal them with a soft rounded bevel mid-ring into one 100% watertight solid cloth shell!
    edge_counts: Dict[Tuple[int, int], int] = {}
    dir_edges: List[Tuple[int, int]] = []
    for a, b, c in t_arr:
        for e0, e1 in ((int(a), int(b)), (int(b), int(c)), (int(c), int(a))):
            key = (e0, e1) if e0 < e1 else (e1, e0)
            edge_counts[key] = edge_counts.get(key, 0) + 1
            dir_edges.append((e0, e1))
    boundary_edges = [(e0, e1) for e0, e1 in dir_edges if edge_counts[(e0, e1) if e0 < e1 else (e1, e0)] == 1]

    # Build soft rounded bevel ring for every boundary vertex (flush cushion, zero inward eye shrink).
    b_verts = sorted({v for e in boundary_edges for v in e})
    mid_idx_map: Dict[int, int] = {}
    mid_pts: List[np.ndarray] = []
    for v_i in b_verts:
        mid_idx_map[v_i] = 2 * n_surf + len(mid_pts)
        # Crisp tailored cloth bevel midpoint along the mask hems (no puffy donut lip).
        mid_pts.append(0.56 * pos_out[v_i] + 0.44 * pos_in[v_i] + 0.0010 * dirs[v_i])

    all_pos = np.vstack([pos_out, pos_in, np.asarray(mid_pts, dtype=np.float64)]).astype(np.float32)
    all_tris: List[Tuple[int, int, int]] = [(int(a), int(b), int(c)) for a, b, c in t_arr]
    # Reversed inner shell
    all_tris.extend((n_surf + int(a), n_surf + int(c), n_surf + int(b)) for a, b, c in t_arr)
    # Rolled bevel wall stitching outer -> mid -> inner along every boundary edge
    for a, b in boundary_edges:
        ma, mb = mid_idx_map[a], mid_idx_map[b]
        ia, ib = n_surf + a, n_surf + b
        all_tris.append((a, mb, b))
        all_tris.append((a, ma, mb))
        all_tris.append((ma, ib, mb))
        all_tris.append((ma, ia, ib))

    indices = np.asarray(all_tris, dtype=np.uint32).reshape(-1)
    mask = MeshPart("bandit_mask", "gear_gold", all_pos, recompute_smooth_normals(all_pos, indices), indices).ensure_outward()
    scene.add("gear_bandana", mask)


def _pleated_cloth_tail(
    control_pts: Sequence[np.ndarray],
    half_widths: Sequence[float],
    half_thicknesses: Sequence[float],
    face_hint: Sequence[float],
    twist_deg: Tuple[float, float],
    *,
    name: str,
) -> MeshPart:
    """Build a thick, sweeping, gracefully pleated flowing cloth banner tail with closed caps."""
    pts = catmull_rom_spline(control_pts, samples_per_seg=7, closed=False)
    n = len(pts)
    u = np.linspace(0.0, 1.0, n)
    w_arr = np.interp(u, np.linspace(0.0, 1.0, len(half_widths)), half_widths)
    t_arr = np.interp(u, np.linspace(0.0, 1.0, len(half_thicknesses)), half_thicknesses)
    twists = np.radians(np.linspace(twist_deg[0], twist_deg[1], n))
    tangents = np.array([_normalize(pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]) for i in range(n)])

    segs = 16
    angs = np.linspace(0.0, 2.0 * math.pi, segs, endpoint=False)
    cu = np.sign(np.cos(angs)) * (np.abs(np.cos(angs)) ** 0.72)
    # Subtle longitudinal cloth pleat crease along the broad ribbon faces.
    su = np.sign(np.sin(angs)) * (np.abs(np.sin(angs)) ** 0.72) + 0.14 * np.cos(2.0 * angs) * np.sin(angs)
    hint = _normalize(np.asarray(face_hint, dtype=np.float64))

    frames_w, frames_o, rings = [], [], []
    for i in range(n):
        tg = tangents[i]
        o0 = _normalize(hint - np.dot(hint, tg) * tg, (0.0, 0.0, 1.0))
        w0 = _normalize(np.cross(o0, tg))
        o0 = np.cross(tg, w0)
        c, s = math.cos(twists[i]), math.sin(twists[i])
        o, w = c * o0 + s * w0, -s * o0 + c * w0
        frames_w.append(w)
        frames_o.append(o)
        rings.append(pts[i][None, :] + (w_arr[i] * cu)[:, None] * w[None, :] + (t_arr[i] * su)[:, None] * o[None, :])

    caps = 3
    start_r = []
    for k in range(caps - 1, 0, -1):
        ph = (k / caps) * (0.5 * math.pi)
        ctr = pts[0] - t_arr[0] * math.sin(ph) * tangents[0]
        start_r.append(ctr[None, :] + (w_arr[0] * math.cos(ph) * cu)[:, None] * frames_w[0][None, :] + (t_arr[0] * math.cos(ph) * su)[:, None] * frames_o[0][None, :])
    end_r = []
    for k in range(1, caps):
        ph = (k / caps) * (0.5 * math.pi)
        ctr = pts[-1] + 1.4 * t_arr[-1] * math.sin(ph) * tangents[-1]
        end_r.append(ctr[None, :] + (w_arr[-1] * math.cos(ph) * cu)[:, None] * frames_w[-1][None, :] + (t_arr[-1] * math.cos(ph) * su)[:, None] * frames_o[-1][None, :])

    all_rings = start_r + rings + end_r
    apex0 = pts[0] - t_arr[0] * tangents[0]
    apex1 = pts[-1] + 1.4 * t_arr[-1] * tangents[-1]
    positions = np.vstack([apex0[None, :]] + all_rings + [apex1[None, :]]).astype(np.float32)

    nr = len(all_rings)
    end_idx = 1 + nr * segs
    tris: List[Tuple[int, int, int]] = [(0, 1 + (k + 1) % segs, 1 + k) for k in range(segs)]
    for r in range(nr - 1):
        b0, b1 = 1 + r * segs, 1 + (r + 1) * segs
        for k in range(segs):
            nk = (k + 1) % segs
            tris.append((b0 + k, b0 + nk, b1 + nk))
            tris.append((b0 + k, b1 + nk, b1 + k))
    b_last = 1 + (nr - 1) * segs
    tris.extend((end_idx, b_last + k, b_last + (k + 1) % segs) for k in range(segs))
    indices = np.asarray(tris, dtype=np.uint32).reshape(-1)
    return MeshPart(name, "gear_gold", positions, recompute_smooth_normals(positions, indices), indices).ensure_outward()


def _add_knot_and_tails(scene: SceneBuilder, caster: _CraniumCaster) -> None:
    """Build the plump folded left-temple knot and two large sweeping pleated cloth tails."""
    theta = math.radians(104.0)
    e_b, e_t = _mask_v_limits(theta)
    d = _direction(theta, 0.48 * (e_b + e_t) + 0.02)
    knot = caster.center + d * (caster.radius(d) + 0.019)

    scene.add(
        "gear_bandana",
        superellipsoid(0.043, 0.038, 0.040, e1=0.72, e2=0.76, u_segs=18, v_segs=14, name="bandana_knot", material="gear_gold")
        .rotate_xyz(rx=0.14, ry=-0.34, rz=0.18)
        .translate(knot),
    )
    scene.add(
        "gear_bandana",
        torus_arc(0.031, 0.0095, u_segs=18, v_segs=8, plane="xy", name="knot_cinch_fold", material="gear_gold")
        .rotate_xyz(rx=0.20, ry=1.26, rz=0.18)
        .translate(knot + 0.008 * d),
    )

    up = np.array([0.0, 1.0, 0.0])
    out = _normalize(np.array([1.0, 0.0, -0.34]))
    back = _normalize(np.array([0.24, 0.0, -1.0]))

    # Compact, perky twin pleated bandana tails (~38% shorter per #191, matching `atst301`/`atst901`).
    upper = [
        knot - 0.008 * out + 0.003 * up,
        knot + 0.044 * out + 0.014 * up + 0.018 * back,
        knot + 0.095 * out + 0.026 * up + 0.046 * back,
        knot + 0.135 * out + 0.033 * up + 0.072 * back,
        knot + 0.156 * out + 0.032 * up + 0.088 * back,
    ]
    lower = [
        knot - 0.006 * out - 0.006 * up,
        knot + 0.047 * out - 0.024 * up + 0.024 * back,
        knot + 0.102 * out - 0.046 * up + 0.060 * back,
        knot + 0.144 * out - 0.050 * up + 0.094 * back,
        knot + 0.166 * out - 0.042 * up + 0.114 * back,
    ]
    tail_u = _pleated_cloth_tail(
        upper,
        half_widths=[0.021, 0.042, 0.052, 0.036, 0.010],
        half_thicknesses=[0.017, 0.018, 0.016, 0.013, 0.008],
        face_hint=(-0.38, 0.34, 0.86),
        twist_deg=(-8.0, 22.0),
        name="bandana_tail_upper",
    ).translate(-knot)
    tail_l = _pleated_cloth_tail(
        lower,
        half_widths=[0.021, 0.039, 0.048, 0.033, 0.010],
        half_thicknesses=[0.016, 0.017, 0.015, 0.012, 0.008],
        face_hint=(-0.22, 0.58, 0.78),
        twist_deg=(10.0, -18.0),
        name="bandana_tail_lower",
    ).translate(-knot)
    scene.add("bandana_tails", [tail_u, tail_l])
    scene.set_node_transform("bandana_tails", translation=tuple(float(c) for c in knot))


def _build_spherical_eyelid(name: str, r_outer: float, thickness: float, yaw_y: float) -> MeshPart:
    """Sleek symmetrical spherical chitin_team upper eyelid tucked cleanly under the mask brow."""
    lid = hemisphere_shell(r_outer, r_outer, r_outer, thickness=thickness, start_phi=-0.14, u_segs=28, v_segs=12, name=name, material="chitin_team")
    return lid.rotate_xyz(rx=-1.14, ry=yaw_y)


def _add_shifty_eyes(scene: SceneBuilder, anchors: dict, blink: float, gaze: float) -> float:
    """Build Thief's cute, simple 1998-style harmoniously tracking spherical eyes + sleek lids.

    Uses a true uniform sphere bright-white sclera (`r = 0.068`, zero Y-stretch) with a big
    cute glossy jet-black dot pupil and ONE small soft upper-left specular glint (zero busy
    double-glint or piercing iris ring), tracking cleanly `+-0.34 rad` in `IdleLookout`.
    """
    sclera_r = 0.068
    lid_outer_r = 0.081
    for side, sign in (("left", 1.0), ("right", -1.0)):
        eye = np.asarray(anchors[f"eye_{side}"], dtype=np.float64)
        yaw = 0.12 * sign

        # Subtle dark orbital socket rim framing the bright white spherical eye cleanly.
        scene.add(
            "eyes",
            torus_arc(0.0705, 0.0056, u_segs=26, v_segs=8, plane="xy", name=f"eye_socket_rim_{side}", material="chitin_dark")
            .scale(0.98, 1.00, 0.86)
            .translate(0.0, 0.0, -0.002)
            .rotate_xyz(rx=-0.06, ry=yaw)
            .translate(eye),
        )
        # True uniform sphere bright warm-white sclera (rotationally invariant — zero stretch mismatch!).
        sclera_local = sphere(sclera_r, u_segs=26, v_segs=18, name=f"sclera_{side}", material="eye_white")
        scene.add("eyes", sclera_local.translate(eye))

        # Harmoniously forward-aligned pivoted eyeball carrying simple cute 1998 black-dot eyes:
        # soft charcoal halo + big cute glossy jet-black dot pupil + ONE small soft upper-left glint.
        ball = f"eye_ball_{side}"
        limbal = _spherical_eye_cap(
            sclera_r,
            ang_x=0.48,
            ang_y=0.50,
            thickness=0.0012,
            u_segs=26,
            v_segs=6,
            name=f"limbal_{side}",
            material="eye_limbal",
        ).rotate_xyz(rx=-0.06)
        pupil = _spherical_eye_cap(
            sclera_r + 0.0010,
            ang_x=0.45,
            ang_y=0.47,
            thickness=0.0014,
            u_segs=24,
            v_segs=6,
            name=f"pupil_{side}",
            material="eye_pupil",
        ).rotate_xyz(rx=-0.06)
        # Single soft upper-left (-X, +Y) catchlight dot aligned consistently on BOTH eyes.
        glint_main = (
            ellipsoid(0.0070, 0.0078, 0.0020, u_segs=14, v_segs=10, name=f"catchlight_main_{side}", material="eye_catchlight")
            .rotate_xyz(rx=-0.20, ry=-0.20)
            .translate(-0.0105, 0.0110, sclera_r + 0.0005)
            .rotate_xyz(rx=-0.06)
        )
        scene.add(ball, [limbal, pupil, glint_main])
        gaze_clamped = max(-1.0, min(1.0, float(gaze)))
        scene.set_node_transform(ball, translation=eye, rotation=quat_from_axis_angle((0.0, 1.0, 0.0), gaze_clamped * GAZE_ANGLE))

        # Sleek symmetrical spherical eyelid: inner radius = r_eye_max + 0.0072, thickness = 0.0034
        # so zero sclera/limbal/pupil/catchlight vertex ever penetrates at any blink or gaze angle.
        eye_local_pts = np.vstack([p.positions for p in (sclera_local, limbal, pupil, glint_main)]).astype(np.float64)
        r_eye_max = float(np.max(np.linalg.norm(eye_local_pts, axis=1)))
        lid_thick = 0.0034
        lid_outer_r = r_eye_max + 0.0072 + lid_thick
        lid = f"eyelid_{side}"
        shell = _build_spherical_eyelid(f"{lid}_shell", lid_outer_r, lid_thick, yaw_y=yaw).rotate_xyz(rx=_LID_REST_DROOP)
        scene.add(lid, shell)
        remaining = _THIEF_BLINK_ANGLE - _LID_REST_DROOP
        scene.set_node_transform(lid, translation=eye, rotation=quat_from_axis_angle((1.0, 0.0, 0.0), max(0.0, min(1.0, float(blink))) * remaining))

    # Shared "Blink" clip timing (both symmetrical lids sweep smoothly down and seal 100% cleanly).
    times = [0.0, 2.40, 2.48, 2.56, 2.68, 3.00]
    q_open = (0.0, 0.0, 0.0, 1.0)
    q_closed = quat_from_axis_angle((1.0, 0.0, 0.0), _THIEF_BLINK_ANGLE - _LID_REST_DROOP)
    blink_channels = [
        AnimationChannel(f"eyelid_{side}", "rotation", list(times), [q_open, q_open, q_closed, q_closed, q_open, q_open])
        for side in ("left", "right")
    ]
    scene.add_animation("Blink", blink_channels)

    # Authentic `atst301` lookout loop (2.52s): dart crisply left (-0.34) and right (+0.34) in sync with Idle!
    q_left = quat_from_axis_angle((0.0, 1.0, 0.0), -GAZE_ANGLE)
    q_right = quat_from_axis_angle((0.0, 1.0, 0.0), +GAZE_ANGLE)
    look_times = [0.0, 1.10, 1.26, 2.36, 2.52]
    look_vals = [q_left, q_left, q_right, q_right, q_left]
    scene.add_animation("IdleLookout", [AnimationChannel(f"eye_ball_{s}", "rotation", list(look_times), list(look_vals)) for s in ("left", "right")])
    return lid_outer_r


def _sculpted_strap_loop(pts: np.ndarray, out_normals: np.ndarray, half_w: float, half_t: float, name: str) -> MeshPart:
    """Sweep a flat beveled closed leather crossbody strap flush to `out_normals`."""
    n = len(pts)
    segs = 10
    angs = np.linspace(0.0, 2.0 * math.pi, segs, endpoint=False)
    cu = np.sign(np.cos(angs)) * (np.abs(np.cos(angs)) ** 0.65)
    su = np.sign(np.sin(angs)) * (np.abs(np.sin(angs)) ** 0.65)
    rings = []
    for i in range(n):
        t = _normalize(pts[(i + 1) % n] - pts[(i - 1) % n])
        o = _normalize(out_normals[i] - np.dot(out_normals[i], t) * t, (0.0, 0.0, 1.0))
        w = _normalize(np.cross(o, t))
        o = np.cross(t, w)
        rings.append(pts[i][None, :] + (half_w * cu)[:, None] * w[None, :] + (half_t * su)[:, None] * o[None, :])
    positions = np.vstack(rings).astype(np.float32)
    tris: List[Tuple[int, int, int]] = []
    for r in range(n):
        b0, b1 = r * segs, ((r + 1) % n) * segs
        for k in range(segs):
            nk = (k + 1) % segs
            tris.append((b0 + k, b0 + nk, b1 + nk))
            tris.append((b0 + k, b1 + nk, b1 + k))
    indices = np.asarray(tris, dtype=np.uint32).reshape(-1)
    return MeshPart(name, "gear_dark", positions, recompute_smooth_normals(positions, indices), indices).ensure_outward()


def _add_rogue_strap_and_loot_pouch(scene: SceneBuilder, anchors: dict) -> None:
    """Add dark leather crossbody utility strap, brass chest buckle, and cinched brown loot pouch."""
    # Anchor strap and pouch cleanly onto `core`'s petite slim thorax (`rx~0.078`, `z~[-0.096,+0.096]`).
    tc = np.asarray(anchors["thorax_center"], dtype=np.float64)
    dy_c = float(tc[1]) - 0.384
    strap_ctrl = [
        (-0.062, 0.464 + dy_c, 0.006),   # over right shoulder beside neck
        (-0.036, 0.430 + dy_c, 0.088),   # upper-right chest
        (0.016, 0.384 + dy_c, 0.094),    # center-left chest
        (0.066, 0.338 + dy_c, 0.048),    # left waist front
        (0.066, 0.340 + dy_c, -0.046),   # left rear hip
        (0.016, 0.392 + dy_c, -0.096),   # mid back
        (-0.052, 0.440 + dy_c, -0.086),  # upper-right back
    ]
    pivot = np.array([0.0, 0.384 + dy_c, -0.006])
    pts = catmull_rom_spline(strap_ctrl, samples_per_seg=6, closed=True)
    out_n = np.array([_normalize(p - pivot + (np.array([0.0, 0.14, 0.0]) if p[1] > 0.435 + dy_c else 0.0)) for p in pts])

    # Push strap cleanly outside ONLY the neutral canonical thorax meshes (already in world coords).
    thorax_parts = [
        p for p in scene.get_node("body")
        if not p.name.startswith(("gaster", "petiole"))
    ]
    body_verts = np.vstack([p.positions for p in thorax_parts]).astype(np.float64)
    v_rel = body_verts - pivot[None, :]
    shifts = np.zeros(len(pts))
    for i, (p, o) in enumerate(zip(pts, out_n)):
        sub = body_verts[(v_rel @ o) > 0.015]
        if len(sub) == 0:
            continue
        diff = sub - p[None, :]
        proj = diff @ o
        perp = diff - proj[:, None] * o[None, :]
        near = np.sum(perp * perp, axis=1) < (0.030 ** 2)
        if np.any(near):
            shifts[i] = float(np.max(proj[near])) + 0.005
    for _ in range(2):
        shifts = 0.25 * np.roll(shifts, 1) + 0.50 * shifts + 0.25 * np.roll(shifts, -1)
    pts = pts + shifts[:, None] * out_n
    scene.add("gear_rogue", _sculpted_strap_loop(pts, out_n, half_w=0.015, half_t=0.005, name="rogue_crossbody_strap"))

    # Crisp brass chest buckle + keeper bar seated right on the front strap segment.
    front_idx = int(np.argmax(pts[:, 2]))
    b_pos = pts[front_idx] + 0.006 * out_n[front_idx]
    buckle_frame = rounded_box(0.036, 0.032, 0.012, bevel=0.0035, segs=2, name="strap_buckle_frame", material="gear_gold_metal").rotate_xyz(rx=10.0, rz=-42.0, degrees=True).translate(b_pos)
    buckle_inner = rounded_box(0.020, 0.016, 0.015, bevel=0.0025, segs=2, name="strap_buckle_cut", material="gear_dark").rotate_xyz(rx=10.0, rz=-42.0, degrees=True).translate(b_pos)
    buckle_pin = rounded_box(0.0065, 0.029, 0.015, bevel=0.0025, segs=2, name="strap_buckle_pin", material="gear_brass_trim").rotate_xyz(rx=10.0, rz=-42.0, degrees=True).translate(b_pos + np.array([0.0, 0.0, 0.0025]))
    scene.add("gear_rogue", [buckle_frame, buckle_inner, buckle_pin])

    # Compact cinched canvas/brown loot pouch slung neatly on the slim right-back flank (-X, -Z)
    # clear above the crouched gaster and digitigrade backwards-knee rear leg.
    pouch_ctr = np.array([-0.084, 0.392 + dy_c, -0.104])
    pouch_prof = [
        (0.000, -0.052),
        (0.028, -0.048),
        (0.046, -0.033),
        (0.053, -0.008),
        (0.047, +0.017),
        (0.030, +0.033),
        (0.019, +0.042),  # cinched drawstring neck
        (0.030, +0.057),  # gathered ruffled sack top
        (0.022, +0.063),
        (0.000, +0.058),
    ]
    def _sack_pleat(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        ang = np.arctan2(p[:, 0], p[:, 2])
        r = np.hypot(p[:, 0], p[:, 2])
        pleat = 1.0 + 0.07 * np.sin(ang * 5.0) * np.clip((p[:, 1] + 0.030) / 0.085, 0.0, 1.0)
        scale = np.where(r > 1e-5, pleat, 1.0)
        p[:, 0] *= scale
        p[:, 2] *= scale
        return p

    sack = (
        lathe(pouch_prof, radial_segs=20, cap_bottom=False, cap_top=False, axis="y", name="loot_pouch_sack", material="prop_brown")
        .deform(_sack_pleat)
        .scale(0.94, 1.0, 1.04)
        .rotate_xyz(rx=-0.26, ry=0.34, rz=-0.34)
        .translate(pouch_ctr)
    )
    scene.add("gear_rogue", sack)

    # Golden woven drawstring cord cinched around the sack neck + two tiny dangling cord ends.
    neck_local = np.array([0.0, 0.042, 0.0])
    neck_ring = (
        torus_arc(0.022, 0.0058, u_segs=18, v_segs=8, plane="xz", name="pouch_drawstring", material="gear_gold")
        .translate(neck_local)
        .rotate_xyz(rx=-0.26, ry=0.34, rz=-0.34)
        .translate(pouch_ctr)
    )
    scene.add("gear_rogue", neck_ring)

    # Tiny dangling golden drawstring tassels + brass clasp anchoring pouch straight to the strap.
    for t_i, dx in enumerate((-0.008, 0.010)):
        tassel = curved_tube(
            [
                pouch_ctr + np.array([0.014 + dx, 0.036, -0.015]),
                pouch_ctr + np.array([0.007 + dx * 1.4, 0.014, -0.032]),
                pouch_ctr + np.array([0.003 + dx * 1.8, -0.004, -0.037]),
            ],
            [0.0048, 0.0044, 0.0060],
            samples_per_seg=3,
            radial_segs=8,
            cap_rings=2,
            name=f"pouch_tassel_{t_i}",
            material="gear_gold",
        )
        scene.add("gear_rogue", tassel)

    # Find nearest rear-right strap point and bridge solidly with a leather hanger + brass clasp.
    rear_right = np.argmin(np.linalg.norm(pts - (pouch_ctr + np.array([0.020, 0.060, 0.030]))[None, :], axis=1))
    strap_anchor = pts[int(rear_right)]
    pouch_neck_world = pouch_ctr + np.array([0.015, 0.037, 0.012])
    hanger = curved_tube(
        [strap_anchor, 0.5 * (strap_anchor + pouch_neck_world) + np.array([-0.004, 0.008, -0.004]), pouch_neck_world],
        [0.0085, 0.0078, 0.0078],
        samples_per_seg=3,
        radial_segs=8,
        cap_rings=2,
        name="pouch_hanger",
        material="gear_dark",
    )
    clasp = sphere(0.010, u_segs=12, v_segs=8, name="pouch_clasp", material="gear_gold_metal").translate(0.5 * (strap_anchor + pouch_neck_world) + np.array([-0.003, 0.007, -0.003]))
    scene.add("gear_rogue", [hanger, clasp])


def _add_thief_idle_animation(scene: SceneBuilder) -> None:
    """Attach authentic `atst301` 2.52s crouched left <-> right weight-pivot `"Idle"` clip.

    Synced directly to `"IdleLookout"` eye darts (hold weight left 1.10s -> 160ms arc-hop
    slide across center -> hold weight right 1.10s -> 160ms arc-hop back). The head stays
    facing forward with only subtle weight-shift lean/inertia, bandana tails trail the hops,
    and all 4 legs stay planted solidly at Y = 0.0 with hips seamlessly nested inside pelvis.
    """
    times = [0.00, 0.55, 1.10, 1.18, 1.26, 1.38, 1.81, 2.36, 2.44, 2.52]

    def _rest_t(node: str) -> Tuple[float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.translation if xf is not None else (0.0, 0.0, 0.0)

    def _rest_r(node: str) -> Tuple[float, float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)

    channels: List[AnimationChannel] = []
    # Authentic `atst301` crouched weight-shift: slide +-0.050 in X with +0.022 center arc-hop lift
    # and natural lean roll into the planted left/right leg (legs stay grounded at Y = 0.0).
    bx, by, bz = _rest_t("upper_body")
    body_dxs = [-0.048, -0.050, -0.048, 0.000, +0.048, +0.050, +0.050, +0.048, 0.000, -0.048]
    body_dys = [0.000, 0.007, 0.000, 0.022, 0.003, 0.000, 0.007, 0.000, 0.022, 0.000]
    body_rolls = [+0.095, +0.100, +0.095, 0.000, -0.095, -0.100, -0.100, -0.095, 0.000, +0.095]
    body_yaws = [-0.030, -0.032, -0.030, 0.000, +0.030, +0.032, +0.032, +0.030, 0.000, -0.030]
    body_r0 = _rest_r("upper_body")
    channels.append(
        AnimationChannel(
            "upper_body",
            "translation",
            list(times),
            [(bx + dx, by + dy, bz) for dx, dy in zip(body_dxs, body_dys)],
        )
    )
    channels.append(
        AnimationChannel(
            "upper_body",
            "rotation",
            list(times),
            [
                quat_mul(body_r0, quat_from_euler_xyz(rx=-0.022 if abs(dx) < 1e-4 else 0.018, ry=yw, rz=rl))
                for dx, yw, rl in zip(body_dxs, body_yaws, body_rolls)
            ],
        )
    )

    # Head stays facing FORWARD (`|ry| <= 0.055 rad`, NO wide turn!): subtle counter-stabilizing
    # lean & crisp inertia nod during each 160ms arc-hop weight shift.
    head_poses = [
        (+0.015, -0.050, -0.045),
        (+0.006, -0.055, -0.048),
        (+0.015, -0.050, -0.045),
        (-0.028, 0.000, 0.000),
        (+0.012, +0.055, +0.048),
        (+0.018, +0.050, +0.045),
        (+0.006, +0.055, +0.048),
        (+0.015, +0.050, +0.045),
        (-0.028, 0.000, 0.000),
        (+0.015, -0.050, -0.045),
    ]
    head_r0 = _rest_r("head_group")
    channels.append(
        AnimationChannel(
            "head_group",
            "rotation",
            list(times),
            [quat_mul(head_r0, quat_from_euler_xyz(rx=rx, ry=ry, rz=rz)) for rx, ry, rz in head_poses],
        )
    )

    # Pleated bandana tails trail & flutter naturally with each snappy 160ms sideways weight shift.
    tail_poses = [
        (0.00, 0.00, 0.00),
        (0.01, -0.02, +0.02),
        (0.00, 0.00, 0.00),
        (-0.03, -0.10, -0.07),
        (-0.02, -0.08, -0.05),
        (0.02, +0.05, +0.04),
        (0.01, +0.02, -0.02),
        (0.00, 0.00, 0.00),
        (-0.03, +0.10, +0.07),
        (0.00, 0.00, 0.00),
    ]
    tail_r0 = _rest_r("bandana_tails")
    channels.append(
        AnimationChannel(
            "bandana_tails",
            "rotation",
            list(times),
            [quat_mul(tail_r0, quat_from_euler_xyz(rx=rx, ry=ry, rz=rz)) for rx, ry, rz in tail_poses],
        )
    )

    # Perky antenna bounce on each arc-hop & gentle gaster counterbalance sway opposite the lean.
    for node_name, sign in (("antenna_left", 1.0), ("antenna_right", -1.0)):
        if node_name in scene.nodes and scene.get_node(node_name):
            r0 = _rest_r(node_name)
            ant_vals = [
                quat_mul(r0, quat_from_euler_xyz(rx=-0.05 if i in (3, 8) else 0.02 * math.sin(i), rz=sign * 0.04 * math.cos(i)))
                for i in range(len(times))
            ]
            ant_vals[-1] = ant_vals[0]
            channels.append(AnimationChannel(node_name, "rotation", list(times), ant_vals))
    if "gaster_group" in scene.nodes:
        g0 = _rest_r("gaster_group")
        g_vals = [quat_mul(g0, quat_from_euler_xyz(rx=-1.2 * dy, rz=-0.45 * rl)) for dy, rl in zip(body_dys, body_rolls)]
        g_vals[-1] = g_vals[0]
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), g_vals))

    scene.add_animation("Idle", channels, replace=True)


def build_thief(blink: float = 0.0, gaze: float = -0.22) -> SceneBuilder:
    """Build the sly, mischievous Rogue Bandit Thief Ant.

    `blink` in [0, 1] sets resting eyelid closure; `gaze` in [-1, 1] turns
    the shifty lookout eyes cleanly left (-1) to right (+1)
    (defaults to `-0.22` for a subtle harmonious sideways rest glance).
    """
    scene, anchors = build_base_ant(
        scene_name="ant_thief",
        include_eyes=False,
        include_brows=False,
        include_idle=False,
        brow_style="sneaky",
        head_pitch_deg=-15.5,
        head_tilt_deg=-4.5,
        head_yaw_deg=4.0,
        antenna_asymmetry=0.40,
        stance_asymmetry=0.45,
        crouch=THIEF_CROUCH,
        left_arm_pose=_THIEF_ARM_LEFT,
        right_arm_pose=_THIEF_ARM_RIGHT,
    )
    # Sculpt Thief's sleek, wider, lower-crowned roguish ant skull and refit sockets before eyes/mask.
    cranium = _refit_thief_head(scene, anchors)
    lid_outer_r = _add_shifty_eyes(scene, anchors, blink, gaze)
    # Ray-cast directly against canonical neutral `cranium` (already at `head_center`) so mask hugs tight!
    neck_vec = np.asarray(anchors["neck_center"], dtype=np.float64)
    caster = _CraniumCaster(cranium, anchors["head_center"])
    _build_unified_domino_mask(scene, caster, anchors, lid_outer_r)
    _add_knot_and_tails(scene, caster)
    _add_rogue_strap_and_loot_pouch(scene, anchors)

    # Parent Thief's canonical neutral head gear & eyes in 1:1 lockstep under tilted `head_group`
    # (matching `head`'s exact `-neck_center` local origin so zero drift occurs under stance_asymmetry!).
    for head_child in ("eyes", "eye_ball_left", "eye_ball_right", "eyelid_left", "eyelid_right", "gear_bandana", "bandana_tails"):
        cur_xf = scene.get_node_transform(head_child)
        cur_t = np.asarray(cur_xf.translation, dtype=np.float64) if cur_xf is not None else np.zeros(3)
        scene.attach_world_node(head_child, "head_group", preserve_world_pose=False)
        xf = scene.get_node_transform(head_child)
        local_t = tuple(float(c) for c in (cur_t - neck_vec))
        scene.set_node_transform(head_child, translation=local_t, rotation=xf.rotation, scale=xf.scale)
    scene.attach_world_node("gear_rogue", "upper_body", preserve_world_pose=False)

    scene.ground_to_y(0.0)
    _add_thief_idle_animation(scene)
    return scene


def build_ant(blink: float = 0.0, gaze: float = -0.22) -> SceneBuilder:
    """Canonical module entrypoint for `build_all.py`."""
    return build_thief(blink=blink, gaze=gaze)


__all__ = ["build_ant", "build_thief"]
