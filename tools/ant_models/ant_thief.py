"""Sculpted high-detail 3D model for the iconic Rogue Bandit Thief Ant.

Built on `base_ant` with a wide sleek oval-shield head, unified watertight ray-cast
woven-gold domino mask (`gear_gold`) with upward-sweeping V-fan cloth tails, extreme
shifty lookout eyes (`IdleLookout`), crossbody leather strap + canvas loot pouch, and
authentic 1998 `atst` crouched weight-shift (`Idle`) + `atat` bullwhip (`Attack`) clips.

Coordinate system (glTF 2.0 right-handed):
  +Y = up (grounded at Y = 0.0), +Z = front (South 301), +X = ant's left.
"""
from __future__ import annotations

import math
from typing import Dict, List, Sequence, Tuple

import numpy as np

try:
    from .base_ant import EYELID_CLOSED_ANGLE, build_base_ant, sync_arm_idle_to_clip, sync_leg_flex_to_clip
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
    from .ability_clips_special import add_thief_abilities
    from .hit_clips_special import add_thief_hit_animation
    from .thief_attack import add_thief_attack_animation, add_thief_whip_meshes, compact_thief_legs, scale_thief_head_assembly
except ImportError:
    from ability_clips_special import add_thief_abilities  # type: ignore
    from base_ant import EYELID_CLOSED_ANGLE, build_base_ant, sync_arm_idle_to_clip, sync_leg_flex_to_clip  # type: ignore
    from hit_clips_special import add_thief_hit_animation  # type: ignore
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
    from thief_attack import add_thief_attack_animation, add_thief_whip_meshes, compact_thief_legs, scale_thief_head_assembly  # type: ignore

# Hard shifty lookout gaze (+-0.72 rad = +-41.2 deg): black pupils tuck ALL THE WAY flush
# into the far-left & far-right eye corners (zero white sliver past the pupil on the gaze side,
# matching `atst301` #0 & #3) while staying strictly on the spherical sclera with zero clipping.
GAZE_ANGLE = 0.72

# Clean balanced resting eyelid angle (symmetrical, zero crooked roll); tucked neatly under
# the top rim of the domino mask at rest and sealing 100% cleanly on Blink.
_LID_REST_DROOP = 0.0
_THIEF_BLINK_ANGLE = EYELID_CLOSED_ANGLE + 0.04

# Compact athletic sneaking stance (`0.138`, paired with `_THIEF_HEAD_SCALE = 0.81` and
# `_THIEF_UNIFORM_SCALE = 0.96` so Thief matches `ORIG atst901`/`atst701`/`atst301` head-to-body
# ratio and compact tucked legs at ~0.78 world height!).
THIEF_CROUCH = 0.138
_THIEF_HEAD_SCALE = 0.81
_THIEF_UNIFORM_SCALE = 1.032
_KNOT_THETA = math.radians(78.0)

# Nimble forward-reaching sneaky rogue arms matching `ORIG atst901`/`atst301`, cleanly above knees.
_THIEF_ARM_LEFT = [
    (0.078, 0.456 - THIEF_CROUCH, 0.014),
    (0.112, 0.454 - THIEF_CROUCH, 0.084),
    (0.094, 0.454 - THIEF_CROUCH, 0.156),
    (0.070, 0.462 - THIEF_CROUCH, 0.184),
]
_THIEF_ARM_RIGHT = [
    (-0.078, 0.456 - THIEF_CROUCH, 0.014),
    (-0.114, 0.450 - THIEF_CROUCH, 0.074),
    (-0.096, 0.440 - THIEF_CROUCH, 0.148),
    (-0.072, 0.446 - THIEF_CROUCH, 0.176),
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
    """Sculpt Thief's sleek, wide, lower-crowned oval-shield head (`atst301`/`atst901`).

    Avoids both a tall round sphere and a boxy square cube: wide low horizontal oval crown,
    deeply recessed eye sockets, and a clean tapered sneaky V-jaw.
    """
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    ax = np.abs(x)
    front_mask = np.clip((z + 0.01) / 0.13, 0.0, 1.0)

    # 1. Gentle crown flattening so the top reads as a sleek wide oval (not a tall marble dome).
    crown = np.clip((y - 0.080) / 0.068, 0.0, 1.0)
    p[:, 1] -= 0.010 * (crown ** 1.45)

    # 2. Gentle brow ridge, subtle forehead Y-suture, and deeply recessed orbital sockets
    #    so Thief's compact 0.82x eyes sit snug and flush inside the domino mask (zero frog-eye bulge!).
    brow_y = 0.048 + 0.044 * np.minimum(ax, 0.18)
    brow_shelf = np.exp(-(((y - brow_y) / 0.025) ** 2)) * np.clip((z - 0.02) / 0.14, 0.0, 1.0)
    p[:, 2] += 0.009 * brow_shelf

    socket = (
        np.exp(-(((ax - 0.084) / 0.056) ** 2 + ((y + 0.012) / 0.050) ** 2))
        * np.clip((z - 0.02) / 0.13, 0.0, 1.0)
    )
    p[:, 2] -= 0.042 * socket

    seam_stem = np.exp(-((ax / 0.014) ** 2)) * np.exp(-(((y - 0.050) / 0.042) ** 2)) * front_mask
    branch_x = 0.014 + np.clip(y - 0.054, 0.0, 0.08) * 0.88
    seam_branch = (
        np.exp(-(((ax - branch_x) / 0.014) ** 2))
        * np.clip((y - 0.050) / 0.020, 0.0, 1.0)
        * np.clip((0.120 - y) / 0.030, 0.0, 1.0)
        * np.clip((z + 0.02) / 0.14, 0.0, 1.0)
    )
    radial = p / np.maximum(1e-6, np.linalg.norm(p, axis=1, keepdims=True))
    p -= (0.0044 * np.maximum(seam_stem, 0.65 * seam_branch))[:, None] * radial

    # 3. Sleek wide cheekbones tapering cleanly into a sharp sneaky lower jaw.
    cheek = (
        np.exp(-(((ax - 0.138) / 0.054) ** 2 + ((y + 0.022) / 0.038) ** 2))
        * np.clip((z + 0.02) / 0.14, 0.0, 1.0)
    )
    p[:, 0] += np.sign(x) * 0.007 * cheek
    p[:, 2] += 0.005 * cheek

    lower = np.clip((-y - 0.012) / 0.136, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.28 * (lower ** 1.28)
    p[:, 2] *= 1.0 - 0.16 * (lower ** 1.32)

    # Clean, smooth lower chin (zero bulbous beak or double-chin lump).
    clypeus = np.exp(-((x / 0.046) ** 2)) * np.exp(-(((y + 0.084) / 0.034) ** 2)) * front_mask
    p[:, 2] += 0.005 * clypeus

    # 4. Crisp aerodynamic occipital taper behind the lower skull into the neck (`atst901` profile).
    rear_low = np.clip((-z - 0.01) / 0.14, 0.0, 1.0) * np.clip((-y + 0.02) / 0.14, 0.0, 1.0)
    p[:, 0] *= 1.0 - 0.18 * rear_low
    p[:, 2] += 0.024 * rear_low
    p[:, 1] += 0.015 * rear_low
    return p


def _paint_thief_cranium_colors(cranium: MeshPart) -> MeshPart:
    """Paint warm anatomical face shading onto Thief's sleek cranium (works across all teams)."""
    p = cranium.positions.astype(np.float64)
    x, y, z = p[:, 0], p[:, 1], p[:, 2]
    ax = np.abs(x)
    front_mask = np.clip((z - 0.01) / 0.14, 0.0, 1.0)
    socket_ring = (
        np.exp(-(((ax - 0.084) / 0.056) ** 2 + ((y + 0.012) / 0.054) ** 2))
        * np.clip((z - 0.02) / 0.12, 0.0, 1.0)
    )
    seam_stem = np.exp(-((ax / 0.016) ** 1.5)) * np.exp(-(((y - 0.058) / 0.048) ** 2)) * front_mask
    mouth_shadow = np.exp(-((ax / 0.064) ** 2 + ((y + 0.118) / 0.040) ** 2)) * front_mask
    cheek_hi = np.exp(-(((ax - 0.118) / 0.056) ** 2 + ((y + 0.036) / 0.042) ** 2)) * front_mask
    brow_hi = np.exp(-(((ax - 0.084) / 0.052) ** 2 + ((y - 0.064) / 0.026) ** 2)) * front_mask
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
    """Replace `base_ant`'s head with Thief's bigger rounded low-crowned roguish skull (`#366`),
    scale tusks to 0.78x flush under jaw, sweep antennae back, and recess 0.82x eyes deeply flush.
    """
    hx, hy, hz = anchors["head_center"]
    # Sleek wide low-crowned oval-shield skull (`rx=0.216, ry=0.146, rz=0.178`, `e1=0.72, e2=0.88`)
    # — organic oval planform (never boxy) with flatter wide profile (never a tall round marble!).
    cranium = (
        superellipsoid(0.216, 0.146, 0.178, e1=0.72, e2=0.88, u_segs=38, v_segs=30, name="cranium", material="chitin_team")
        .deform(_thief_head_deform)
    )
    cranium = _paint_thief_cranium_colors(cranium).translate(hx, hy, hz)

    # Swap cranium inside `head`: stout, plump, shorter bean-mandibles (`1.04x W, 0.78x H, 0.80x L`)
    # seated cleanly into the smooth lower cheek below eyes & bandana (`#412`), with a delicate mouth cleft (`#413`).
    jaw_dy, jaw_dz = -0.018, +0.008
    head_parts = scene.get_node("head")
    new_head_parts: List[MeshPart] = []
    for p in head_parts:
        if p.name == "cranium":
            new_head_parts.append(cranium)
        elif p.name == "mouth_cleft":
            mc = p.positions.mean(axis=0)
            new_head_parts.append(
                p.translate(-float(mc[0]), -float(mc[1]), -float(mc[2]))
                .scale(0.64, 0.58, 0.62)
                .translate(float(mc[0]), float(mc[1]) + jaw_dy + 0.006, float(mc[2]) + jaw_dz - 0.003)
            )
        else:
            new_head_parts.append(p)
    scene.nodes["head"] = new_head_parts

    mand_parts = scene.get_node("mandibles")
    if mand_parts:
        scene.nodes["mandibles"] = [p.scale(1.04, 0.78, 0.80).rotate_xyz(rx=4.0, degrees=True) for p in mand_parts]
    mand_xf = scene.get_node_transform("mandibles")
    if mand_xf is not None:
        mx, my, mz = mand_xf.translation
        scene.set_node_transform(
            "mandibles",
            translation=(mx, my + jaw_dy, mz + jaw_dz),
            rotation=mand_xf.rotation,
            scale=mand_xf.scale,
        )

    # Rebuild Thief's antennae boldly larger (~1.34x longer, ~1.30x thicker shafts + chunky rounded clubbed
    # tips) rising proudly UP-OUT-AND-BACK (`+Y/-Z` V silhouette matching `atst301`/`atst201`/`atst901`/`atst701`)
    # so they stay bold and unmistakable even after `0.81x` head assembly scaling!
    neck_vec = np.asarray(anchors["neck_center"], dtype=np.float64)
    ant_rads = [
        (0.026, 0.026),
        (0.022, 0.023),
        (0.021, 0.022),
        (0.026, 0.028),
        (0.036, 0.039),
        (0.043, 0.046),
        (0.032, 0.035),
    ]
    ant_defs = (
        (
            "antenna_left",
            "left",
            (+0.088, hy + 0.120, hz + 0.024),
            [
                (-0.005, -0.024, 0.002),
                (0.000, 0.000, 0.000),
                (+0.032, +0.084, -0.018),
                (+0.062, +0.162, -0.048),
                (+0.084, +0.214, -0.086),
                (+0.096, +0.238, -0.114),
                (+0.102, +0.232, -0.134),
            ],
        ),
        (
            "antenna_right",
            "right",
            (-0.088, hy + 0.120, hz + 0.022),
            [
                (+0.005, -0.024, 0.002),
                (0.000, 0.000, 0.000),
                (-0.026, +0.088, -0.022),
                (-0.048, +0.170, -0.056),
                (-0.064, +0.224, -0.096),
                (-0.074, +0.246, -0.124),
                (-0.078, +0.238, -0.144),
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
        socket = ellipsoid(0.034, 0.019, 0.034, u_segs=12, v_segs=8, name=f"antenna_socket_{tag}", material="chitin_dark").translate(0.0, -0.004, 0.0)
        scene.nodes[node_name] = [stalk, socket]
        rel = np.asarray(base_w, dtype=np.float64) - neck_vec
        scene.set_node_transform(node_name, translation=(float(rel[0]), float(rel[1]), float(rel[2])))

    # Recess Thief's smaller 0.82x eyes DEEPLY and flush inside the deep orbital hollows (`z = hz + 0.122`
    # on `rz = 0.178` skull -> sclera front at `0.174`, completely flush with the domino mask, zero bulge!).
    eye_y = hy - 0.012
    eye_z = hz + 0.122
    anchors["eye_left"] = (0.084, eye_y, eye_z)
    anchors["eye_right"] = (-0.084, eye_y, eye_z)
    anchors["eye_center"] = (0.0, eye_y, eye_z + 0.010)
    return cranium


def _mask_v_limits(theta: float) -> Tuple[float, float]:
    """Outer bottom/top elevation bounds for Thief's sleek, high-riding domino mask + headband."""
    a = abs(math.atan2(math.sin(theta), math.cos(theta)))
    v_top = 0.485 + 0.080 * math.exp(-((a / 1.15) ** 2))
    cheek_dip = -0.320 * math.exp(-(((a - 0.54) / 0.36) ** 2))
    nose_dip = -0.155 * math.exp(-((a / 0.24) ** 2))
    v_bot = -0.095 + cheek_dip + nose_dip
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
    target_clear = lid_outer_r + 0.0018
    hole_s = np.zeros(m_ring, dtype=np.float64)
    ray_du = np.zeros(m_ring, dtype=np.float64)
    ray_dv = np.zeros(m_ring, dtype=np.float64)
    for k in range(m_ring):
        du = perim_u[k] - u_eye
        dv = perim_v[k] - v_eye
        dist_perim = max(1e-6, math.hypot(du, dv))
        du_n, dv_n = du / dist_perim, dv / dist_perim
        ray_du[k], ray_dv[k] = du_n, dv_n

        # Binary search smallest angular radius clearing the compact 3D eyelid sphere cleanly.
        lo, hi = 0.16, min(0.52, dist_perim * 0.74)
        for _ in range(10):
            mid = 0.5 * (lo + hi)
            uu, vv = u_eye + mid * du_n, v_eye + mid * dv_n
            d = _direction(uu, vv)
            p_surf = hc + d * (caster.radius(d) + 0.0085)
            if float(np.linalg.norm(p_surf - eye_l)) < target_clear:
                lo = mid
            else:
                hi = mid
        # Keep continuous nose bridge (>= 0.110 rad) and under-eye cheek strip cleanly clear.
        max_s = dist_perim * 0.76
        if du_n < -1e-4:
            max_s = min(max_s, (u_eye - 0.110) / (-du_n))
        hole_s[k] = min(hi, max(0.16, max_s))

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

    # Taut tied-cloth bridging (8 passes) so the fabric spans naturally across brow/cheek/temple
    # hollows like real woven cloth instead of vacuum-suctioning into every cranium dimple!
    adj: List[List[int]] = [[] for _ in range(n_surf)]
    for a, b, c in surf_tris:
        adj[a].extend((b, c))
        adj[b].extend((a, c))
        adj[c].extend((a, b))
    r_bridged = r_raw.copy()
    for _ in range(8):
        nxt = r_bridged.copy()
        for i in range(n_surf):
            if adj[i]:
                nxt[i] = max(r_raw[i] + 0.0038, 0.55 * r_bridged[i] + 0.45 * float(np.mean(r_bridged[adj[i]])))
        r_bridged = nxt

    # Build proud folded tied-cloth shell (`0.012-0.017` stand-off) with crisp hemmed top/bottom rims
    # flaring subtly off the skull and 3 gathered fabric tension folds pinching into the +X side knot!
    theta_knot = _KNOT_THETA
    pos_out = np.zeros((n_surf, 3), dtype=np.float64)
    pos_in = np.zeros((n_surf, 3), dtype=np.float64)
    fold_shade = np.zeros(n_surf, dtype=np.float64)
    for i, (u, v) in enumerate(uv_list):
        vb, vt = _mask_v_limits(u)
        v_norm = max(0.0, min(1.0, (v - vb) / max(1e-5, vt - vb)))
        # Detect top/bottom outer bandana hems (separate from inner eye cutout boundaries!).
        hem_dist = min(v_norm, 1.0 - v_norm)
        outer_hem = max(0.0, 1.0 - hem_dist / 0.14)
        # Subtle outward cloth-hem lift & rolled folded border so top/bottom edges cast real shadow!
        hem_bead = 0.0034 * math.sin(min(1.0, hem_dist / 0.14) * 0.5 * math.pi) * outer_hem
        hem_flare = 0.0040 * (outer_hem ** 1.6)
        # Keep eye cutout openings neatly trimmed (`is_boundary_hint == 0` while away from outer hems).
        eye_rim_w = 1.0 - max(0.0, min(1.0, is_boundary_hint[i])) if hem_dist > 0.12 else 0.0

        # 3 soft gathered horizontal tension folds wrapping around the temples & pinching into +X knot.
        du_knot = math.atan2(math.sin(u - theta_knot), math.cos(u - theta_knot))
        knot_prox = 0.42 + 0.58 * math.exp(-((du_knot / 0.95) ** 2))
        bridge_calm = 1.0 - 0.55 * math.exp(-((u / 0.34) ** 2))
        # Subtle cinch pulling the vertical tension lines gently toward the side knot center!
        v_cinch = v_norm + 0.12 * (0.5 - v_norm) * math.exp(-((du_knot / 0.50) ** 2))
        wave1 = math.sin(v_cinch * 3.0 * math.pi + 0.50 * math.sin(u - theta_knot))
        wave2 = math.cos(v_cinch * 5.0 * math.pi - 0.35 * u)
        fold_amp = (0.0038 * wave1 + 0.0017 * wave2) * knot_prox * bridge_calm * (1.0 - 0.65 * eye_rim_w)
        fold_shade[i] = (0.65 * wave1 + 0.35 * wave2) * knot_prox * bridge_calm - 0.45 * outer_hem

        base_standoff = 0.0128 - 0.0024 * eye_rim_w
        thick_out = base_standoff + hem_bead + hem_flare + fold_amp
        # Inner surface hugs just inside/at the skull (`+0.0015` under outer hems -> real cloth edge shadow!).
        in_offset = -0.0038 + 0.0050 * outer_hem
        pos_out[i] = hc + dirs[i] * (r_bridged[i] + thick_out)
        pos_in[i] = hc + dirs[i] * (r_bridged[i] + in_offset)

    # Guarantee EVERY single outer-surface triangle faces strictly outward along +dirs.
    t_arr = np.asarray(surf_tris, dtype=np.int32)
    tri_n = np.cross(pos_out[t_arr[:, 1]] - pos_out[t_arr[:, 0]], pos_out[t_arr[:, 2]] - pos_out[t_arr[:, 0]])
    tri_d = dirs[t_arr[:, 0]] + dirs[t_arr[:, 1]] + dirs[t_arr[:, 2]]
    flipped = np.einsum("ij,ij->i", tri_n, tri_d) < 0.0
    if np.any(flipped):
        t_arr[flipped] = t_arr[flipped][:, [0, 2, 1]]

    # Automatically discover ALL open boundary half-edges (top hem, bottom hem, left/right eye cutouts)
    # and seal them with a crisp folded cloth bevel mid-ring into one 100% watertight solid cloth shell!
    edge_counts: Dict[Tuple[int, int], int] = {}
    dir_edges: List[Tuple[int, int]] = []
    for a, b, c in t_arr:
        for e0, e1 in ((int(a), int(b)), (int(b), int(c)), (int(c), int(a))):
            key = (e0, e1) if e0 < e1 else (e1, e0)
            edge_counts[key] = edge_counts.get(key, 0) + 1
            dir_edges.append((e0, e1))
    boundary_edges = [(e0, e1) for e0, e1 in dir_edges if edge_counts[(e0, e1) if e0 < e1 else (e1, e0)] == 1]

    # Build crisp folded-fabric edge ring for every boundary vertex (proud cloth hem, zero eye shrink).
    b_verts = sorted({v for e in boundary_edges for v in e})
    mid_idx_map: Dict[int, int] = {}
    mid_pts: List[np.ndarray] = []
    mid_lums: List[float] = []
    for v_i in b_verts:
        mid_idx_map[v_i] = 2 * n_surf + len(mid_pts)
        mid_pts.append(0.58 * pos_out[v_i] + 0.42 * pos_in[v_i] + 0.0022 * dirs[v_i])
        mid_lums.append(0.84)

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
    # Subtle baked cloth-fold AO shading (warmer shadow in fold valleys & hem edges, sunlit ridges).
    out_lum = np.clip(0.92 + 0.08 * fold_shade, 0.78, 1.0)
    in_lum = np.full(n_surf, 0.72, dtype=np.float64)
    all_lum = np.concatenate([out_lum, in_lum, np.asarray(mid_lums, dtype=np.float64)]).astype(np.float32)
    mask.colors = np.column_stack([all_lum, all_lum, all_lum]).astype(np.float32)
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
    """Build the crisp left-temple knot and two wide V-fanned upward-sweeping cloth tails."""
    theta = _KNOT_THETA
    e_b, e_t = _mask_v_limits(theta)
    d = _direction(theta, 0.64 * (e_b + e_t) + 0.06)
    knot = caster.center + d * (caster.radius(d) + 0.022)

    scene.add(
        "gear_bandana",
        superellipsoid(0.036, 0.032, 0.034, e1=0.72, e2=0.76, u_segs=18, v_segs=14, name="bandana_knot", material="gear_gold")
        .rotate_xyz(rx=0.10, ry=-0.16, rz=0.22)
        .translate(knot),
    )
    scene.add(
        "gear_bandana",
        torus_arc(0.027, 0.0082, u_segs=18, v_segs=8, plane="xy", name="knot_cinch_fold", material="gear_gold")
        .rotate_xyz(rx=0.14, ry=1.42, rz=0.22)
        .translate(knot + 0.007 * d),
    )

    up = np.array([0.0, 1.0, 0.0])
    out = np.array([1.0, 0.0, 0.0])
    fwd = np.array([0.0, 0.0, 1.0])

    # Crisp open V-fan with tall upward-forward crescent upper plume & wide out-right lower flap:
    # flares in an open V in 301 S & 701 N, curls tall UP-RIGHT in 901 E, AND lifts cleanly UP-LEFT in W!
    upper = [
        knot - 0.006 * out + 0.008 * up,
        knot + 0.044 * out + 0.058 * up + 0.016 * fwd,
        knot + 0.078 * out + 0.124 * up + 0.036 * fwd,
        knot + 0.092 * out + 0.178 * up + 0.058 * fwd,
        knot + 0.094 * out + 0.212 * up + 0.076 * fwd,
    ]
    lower = [
        knot - 0.006 * out - 0.006 * up,
        knot + 0.056 * out + 0.006 * up + 0.018 * fwd,
        knot + 0.114 * out + 0.022 * up + 0.038 * fwd,
        knot + 0.156 * out + 0.042 * up + 0.056 * fwd,
        knot + 0.180 * out + 0.058 * up + 0.072 * fwd,
    ]
    tail_u = _pleated_cloth_tail(
        upper,
        half_widths=[0.020, 0.046, 0.060, 0.048, 0.014],
        half_thicknesses=[0.0085, 0.0088, 0.0080, 0.0066, 0.0046],
        face_hint=(0.56, 0.18, 0.81),
        twist_deg=(-12.0, 18.0),
        name="bandana_tail_upper",
    ).translate(-knot)
    tail_l = _pleated_cloth_tail(
        lower,
        half_widths=[0.020, 0.046, 0.062, 0.050, 0.014],
        half_thicknesses=[0.0085, 0.0088, 0.0080, 0.0066, 0.0046],
        face_hint=(0.36, 0.52, 0.77),
        twist_deg=(8.0, -14.0),
        name="bandana_tail_lower",
    ).translate(-knot)
    knot_t = tuple(float(c) for c in knot)
    scene.add("bandana_tails", tail_u)
    scene.set_node_transform("bandana_tails", translation=knot_t)
    scene.add("bandana_tail_lower", tail_l)
    scene.set_node_transform("bandana_tail_lower", translation=knot_t)


def _build_spherical_eyelid(name: str, r_outer: float, thickness: float, yaw_y: float) -> MeshPart:
    """Sleek symmetrical spherical chitin_team upper eyelid tucked cleanly under the mask brow."""
    lid = hemisphere_shell(r_outer, r_outer, r_outer, thickness=thickness, start_phi=-0.14, u_segs=28, v_segs=12, name=name, material="chitin_team")
    return lid.rotate_xyz(rx=-1.14, ry=yaw_y)


def _add_shifty_eyes(scene: SceneBuilder, anchors: dict, blink: float, gaze: float) -> float:
    """Build Thief's compact (~0.82x), deeply recessed 1998-style shifty spherical eyes + sleek lids.

    Uses a compact uniform bright-white sclera (`r = 0.052`) seated flush inside the deep orbital
    sockets (zero frog-eye bulge!) with a crisp glossy jet-black dot pupil and ONE soft upper-left
    specular glint, tracking HARD left/right `+-0.72 rad` in `IdleLookout` with full blink clearance.
    """
    sclera_r = 0.052
    lid_outer_r = 0.065
    for side, sign in (("left", 1.0), ("right", -1.0)):
        eye = np.asarray(anchors[f"eye_{side}"], dtype=np.float64)
        yaw = 0.10 * sign

        # Subtle dark orbital socket rim framing the recessed compact spherical eye cleanly.
        scene.add(
            "eyes",
            torus_arc(0.0545, 0.0044, u_segs=26, v_segs=8, plane="xy", name=f"eye_socket_rim_{side}", material="chitin_dark")
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
            ang_x=0.51,
            ang_y=0.53,
            thickness=0.0010,
            u_segs=26,
            v_segs=6,
            name=f"limbal_{side}",
            material="eye_limbal",
        ).rotate_xyz(rx=-0.06)
        pupil = _spherical_eye_cap(
            sclera_r + 0.0008,
            ang_x=0.48,
            ang_y=0.50,
            thickness=0.0012,
            u_segs=24,
            v_segs=6,
            name=f"pupil_{side}",
            material="eye_pupil",
        ).rotate_xyz(rx=-0.06)
        # Single soft upper-left (-X, +Y) catchlight dot aligned consistently on BOTH eyes.
        glint_main = (
            ellipsoid(0.0054, 0.0060, 0.0016, u_segs=14, v_segs=10, name=f"catchlight_main_{side}", material="eye_catchlight")
            .rotate_xyz(rx=-0.20, ry=-0.20)
            .translate(-0.0080, 0.0082, sclera_r + 0.0004)
            .rotate_xyz(rx=-0.06)
        )
        scene.add(ball, [limbal, pupil, glint_main])
        gaze_clamped = max(-1.0, min(1.0, float(gaze)))
        scene.set_node_transform(ball, translation=eye, rotation=quat_from_axis_angle((0.0, 1.0, 0.0), gaze_clamped * GAZE_ANGLE))

        # Sleek symmetrical spherical eyelid: inner radius = r_eye_max + 0.0072, thickness = 0.0030
        # so zero sclera/limbal/pupil/catchlight vertex ever penetrates at any blink or gaze angle.
        eye_local_pts = np.vstack([p.positions for p in (sclera_local, limbal, pupil, glint_main)]).astype(np.float64)
        r_eye_max = float(np.max(np.linalg.norm(eye_local_pts, axis=1)))
        lid_thick = 0.0030
        lid_outer_r = r_eye_max + 0.0072 + lid_thick
        lid = f"eyelid_{side}"
        shell = _build_spherical_eyelid(f"{lid}_shell", lid_outer_r, lid_thick, yaw_y=yaw).rotate_xyz(rx=_LID_REST_DROOP)
        scene.add(lid, shell)
        remaining = _THIEF_BLINK_ANGLE - _LID_REST_DROOP
        scene.set_node_transform(lid, translation=eye, rotation=quat_from_axis_angle((1.0, 0.0, 0.0), max(0.0, min(1.0, float(blink))) * remaining))

    # Shared snappy 1998-calibrated 135ms "Blink" clip timing (matching mesh_core.add_blink_animation).
    times = [0.0, 2.40, 2.435, 2.490, 2.535, 3.00]
    q_open = (0.0, 0.0, 0.0, 1.0)
    q_closed = quat_from_axis_angle((1.0, 0.0, 0.0), _THIEF_BLINK_ANGLE - _LID_REST_DROOP)
    blink_channels = [
        AnimationChannel(f"eyelid_{side}", "rotation", list(times), [q_open, q_open, q_closed, q_closed, q_open, q_open])
        for side in ("left", "right")
    ]
    scene.add_animation("Blink", blink_channels)

    # Authentic `atst301` lookout loop (2.52s): dart HARD left (-GAZE_ANGLE) & HARD right (+GAZE_ANGLE) in sync with Idle!
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
    pouch_ctr = np.array([-0.084, 0.418 + dy_c, -0.104])
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
    # Authentic `atst301` crouched weight-shift: slide +-0.042 in X (proportional to the
    # shorter crouched legs) with a coiled -0.006 hold settle, +0.020 center arc-hop lift,
    # and natural lean roll into the planted left/right leg (feet stay glued at Y = 0.0).
    bx, by, bz = _rest_t("upper_body")
    body_dxs = [-0.040, -0.042, -0.040, 0.000, +0.040, +0.042, +0.042, +0.040, 0.000, -0.040]
    body_dys = [0.000, -0.006, 0.000, 0.014, 0.002, 0.000, -0.006, 0.000, 0.014, 0.000]
    body_rolls = [+0.086, +0.090, +0.086, 0.000, -0.086, -0.090, -0.090, -0.086, 0.000, +0.086]
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

    # Lively, expressive out-of-phase silk-cloth flutter on BOTH tied bandana tails (`#415`):
    # snappy +-0.26..0.32 rad trailing whip on each 160ms hop + damped silk overshoot + staggered breeze ripple!
    tail_tracks = {
        "bandana_tails": [
            (+0.04, +0.06, -0.05),
            (-0.07, -0.09, +0.08),
            (+0.03, +0.04, -0.03),
            (-0.14, -0.28, +0.24),
            (-0.10, -0.32, +0.20),
            (+0.09, +0.18, -0.15),
            (-0.05, -0.08, +0.09),
            (+0.04, +0.06, -0.05),
            (-0.13, +0.26, -0.24),
            (+0.04, +0.06, -0.05),
        ],
        "bandana_tail_lower": [
            (-0.05, -0.07, +0.06),
            (+0.08, +0.09, -0.09),
            (-0.04, -0.05, +0.04),
            (-0.10, -0.22, +0.18),
            (-0.16, -0.34, +0.28),
            (+0.11, +0.21, -0.18),
            (+0.07, +0.09, -0.08),
            (-0.04, -0.06, +0.05),
            (-0.15, +0.22, -0.28),
            (-0.05, -0.07, +0.06),
        ],
    }
    for tail_node, poses in tail_tracks.items():
        if tail_node in scene.nodes and scene.get_node(tail_node):
            tr0 = _rest_r(tail_node)
            t_vals = [quat_mul(tr0, quat_from_euler_xyz(rx=rx, ry=ry, rz=rz)) for rx, ry, rz in poses]
            t_vals[-1] = t_vals[0]
            channels.append(AnimationChannel(tail_node, "rotation", list(times), t_vals))

    # Lively inertia-swaying antennae (`#365`): trail opposite each 160ms sideways hop (+-0.22 rad)
    # then whip with a springy overshoot settle and subtle left-vs-right phase individuality!
    # Keyframes: [0.00 holdL, 0.55 holdL, 1.10 preHopR, 1.18 midHopR, 1.26 landR, 1.38 settleR,
    #             1.81 holdR, 2.36 preHopL, 2.44 midHopL, 2.52 landL]
    ant_tracks = {
        "antenna_left": [
            (+0.05, -0.06, +0.10),
            (-0.02, -0.02, -0.02),
            (+0.02, -0.03, +0.03),
            (-0.11, -0.14, +0.23),
            (-0.05, -0.08, +0.14),
            (+0.07, +0.11, -0.18),
            (-0.02, +0.02, +0.03),
            (+0.02, +0.03, -0.03),
            (-0.11, +0.14, -0.23),
            (+0.05, -0.06, +0.10),
        ],
        "antenna_right": [
            (+0.03, -0.04, +0.08),
            (+0.02, +0.02, -0.04),
            (-0.01, -0.02, +0.02),
            (-0.09, -0.11, +0.19),
            (-0.07, -0.12, +0.21),
            (+0.06, +0.09, -0.15),
            (+0.02, -0.02, +0.04),
            (-0.01, +0.02, -0.02),
            (-0.09, +0.12, -0.20),
            (+0.03, -0.04, +0.08),
        ],
    }
    for node_name, track in ant_tracks.items():
        if node_name in scene.nodes and scene.get_node(node_name):
            r0 = _rest_r(node_name)
            ant_vals = [quat_mul(r0, quat_from_euler_xyz(rx=rx, ry=ry, rz=rz)) for rx, ry, rz in track]
            ant_vals[-1] = ant_vals[0]
            channels.append(AnimationChannel(node_name, "rotation", list(times), ant_vals))
    if "gaster_group" in scene.nodes:
        g0 = _rest_r("gaster_group")
        g_vals = [quat_mul(g0, quat_from_euler_xyz(rx=-1.2 * dy, rz=-0.45 * rl)) for dy, rl in zip(body_dys, body_rolls)]
        g_vals[-1] = g_vals[0]
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), g_vals))

    # Synced shifty left <-> right lookout eyes directly inside `"Idle"` (matching `"IdleLookout"`),
    # so Thief's eyes dart left/right in every glTF viewer even when only `"Idle"` is played.
    q_look_l = quat_from_axis_angle((0.0, 1.0, 0.0), -GAZE_ANGLE)
    q_look_r = quat_from_axis_angle((0.0, 1.0, 0.0), +GAZE_ANGLE)
    look_times = [0.0, 1.10, 1.26, 2.36, 2.52]
    look_vals = [q_look_l, q_look_l, q_look_r, q_look_r, q_look_l]
    for side in ("left", "right"):
        ball_node = f"eye_ball_{side}"
        if ball_node in scene.nodes:
            channels.append(AnimationChannel(ball_node, "rotation", list(look_times), list(look_vals)))

    scene.add_animation("Idle", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Idle")
    sync_arm_idle_to_clip(scene, "Idle")


def build_thief(blink: float = 0.0, gaze: float = -1.0) -> SceneBuilder:
    """Build the sly, mischievous Rogue Bandit Thief Ant.

    `blink` in [0, 1] sets resting eyelid closure; `gaze` in [-1, 1] turns
    the shifty lookout eyes HARD left (-1) to HARD right (+1)
    (defaults to `-1.0` matching `atst301` frame #0 and `IdleLookout` t=0).
    """
    scene, anchors = build_base_ant(
        scene_name="ant_thief",
        include_eyes=False,
        include_brows=False,
        include_idle=False,
        brow_style="sneaky",
        head_pitch_deg=-16.0,
        head_tilt_deg=-4.5,
        head_yaw_deg=4.0,
        antenna_asymmetry=0.40,
        stance_asymmetry=0.45,
        leg_style="thief",
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

    # Parent eyes under `head_group` and worn bandana under `bandana_worn_group` (pivoted inside
    # the opaque cranium at `head_center` so `Attack` can cleanly whip the bandana off & back on!).
    hc_vec = np.asarray(anchors["head_center"], dtype=np.float64)
    for head_child in ("eyes", "eye_ball_left", "eye_ball_right", "eyelid_left", "eyelid_right"):
        cur_xf = scene.get_node_transform(head_child)
        cur_t = np.asarray(cur_xf.translation, dtype=np.float64) if cur_xf is not None else np.zeros(3)
        scene.attach_world_node(head_child, "head_group", preserve_world_pose=False)
        xf = scene.get_node_transform(head_child)
        scene.set_node_transform(head_child, translation=tuple(float(c) for c in (cur_t - neck_vec)), rotation=xf.rotation, scale=xf.scale)
    scene.set_node_parent("bandana_worn_group", "head_group")
    scene.set_node_transform("bandana_worn_group", translation=tuple(float(c) for c in (hc_vec - neck_vec)))
    for worn in ("gear_bandana", "bandana_tails", "bandana_tail_lower"):
        cur_xf = scene.get_node_transform(worn)
        cur_t = np.asarray(cur_xf.translation, dtype=np.float64) if cur_xf is not None else np.zeros(3)
        scene.set_node_parent(worn, "bandana_worn_group")
        scene.set_node_transform(
            worn,
            translation=tuple(float(c) for c in (cur_t - hc_vec)),
            rotation=cur_xf.rotation if cur_xf is not None else (0.0, 0.0, 0.0, 1.0),
            scale=cur_xf.scale if cur_xf is not None else (1.0, 1.0, 1.0),
        )
    scene.attach_world_node("gear_rogue", "upper_body", preserve_world_pose=False)
    scale_thief_head_assembly(scene, _THIEF_HEAD_SCALE)
    add_thief_whip_meshes(scene, anchors, head_scale=_THIEF_HEAD_SCALE)

    # Sleek horizontal pointed gaster matching `ORIG atst901`'s crisp rearward dart silhouette.
    gp = anchors["gaster_pivot"]
    if "gaster" in scene.nodes:
        scene.nodes["gaster"] = [
            p.translate(-gp[0], -gp[1], -gp[2]).scale(0.88, 0.88, 1.07).translate(gp[0], gp[1], gp[2])
            for p in scene.get_node("gaster")
        ]
    gg_xf = scene.get_node_transform("gaster_group")
    if gg_xf is not None:
        scene.set_node_transform(
            "gaster_group",
            translation=gg_xf.translation,
            rotation=quat_mul(gg_xf.rotation, quat_from_euler_xyz(rx=+0.15)),
            scale=gg_xf.scale,
        )

    # Clean athletic sneaky forward lean (+0.16 rad) so Thief sits poised & reveals both shifty eyes at 45 deg!
    ub_xf = scene.get_node_transform("upper_body")
    if ub_xf is not None:
        scene.set_node_transform(
            "upper_body",
            translation=ub_xf.translation,
            rotation=quat_mul(ub_xf.rotation, quat_from_euler_xyz(rx=0.16)),
            scale=ub_xf.scale,
        )

    # Tuck legs compactly underneath torso, then scale uniformly so final height lands ~0.78!
    compact_thief_legs(scene)
    scene.scale_all(_THIEF_UNIFORM_SCALE)
    scene.ground_to_y(0.0)
    _add_thief_idle_animation(scene)
    add_thief_attack_animation(scene, GAZE_ANGLE)
    add_thief_hit_animation(scene)
    add_thief_abilities(scene)
    return scene


def build_ant(blink: float = 0.0, gaze: float = -1.0) -> SceneBuilder:
    """Canonical module entrypoint for `build_all.py`."""
    return build_thief(blink=blink, gaze=gaze)


__all__ = ["build_ant", "build_thief"]
