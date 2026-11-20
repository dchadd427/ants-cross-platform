"""Sculpted 3D Fire Ant (Option A: 1998 Wide Billboard yellow plastic helmet + empty idle hands).

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet grounded solidly at Y = 0.0)
  +Z = front (South / canonical camera 301)
  +X = ant's left (viewer's right from front), -X = ant's right
"""
from __future__ import annotations

import math
from typing import List, Tuple

import numpy as np

try:
    from .base_ant import build_base_ant
    from .mesh_core import (
        AnimationChannel,
        AnimationClip,
        MeshPart,
        SceneBuilder,
        curved_tube,
        cylinder,
        ellipsoid,
        hemisphere_shell,
        lathe,
        quat_from_euler_xyz,
        quat_mul,
        rounded_box,
        sphere,
        swept_tube,
        torus_arc,
    )
except ImportError:
    from base_ant import build_base_ant  # type: ignore
    from mesh_core import (  # type: ignore
        AnimationChannel,
        AnimationClip,
        MeshPart,
        SceneBuilder,
        curved_tube,
        cylinder,
        ellipsoid,
        hemisphere_shell,
        lathe,
        quat_from_euler_xyz,
        quat_mul,
        rounded_box,
        sphere,
        swept_tube,
        torus_arc,
    )

_HAT_MAT = "gear_yellow_plastic"

# Optional right-arm pose when `hold_magnifying_glass=True` (kept for Fire's fire-starting animation).
_FIRE_RIGHT_ARM: List[Tuple[float, float, float]] = [
    (-0.078, 0.456, 0.014),
    (-0.184, 0.422, 0.068),
    (-0.194, 0.464, 0.168),
    (-0.182, 0.494, 0.222),
]


def _brim_radial_and_y(theta: np.ndarray) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Compute Option A wide flat horizontal front/side brim + sweeping downward rear ducktail neck guard."""
    cos_t = np.cos(theta)
    sin_t = np.sin(theta)
    front_w = np.clip(cos_t, 0.0, 1.0) ** 1.35
    side_w = sin_t * sin_t
    rear_w = (0.5 * (1.0 - cos_t)) ** 1.45

    # Crisp wide flat horizontal brim around front & sides, transitioning smoothly into a generous
    # downward-sweeping rear ducktail neck guard matching Option A's 3/4 and side profile.
    x_flare = 0.26 * front_w + 0.24 * side_w + 0.30 * rear_w
    z_flare = 0.36 * front_w + 0.22 * side_w + 0.94 * rear_w
    y_shift = 0.008 * front_w + 0.002 * side_w - 0.132 * rear_w
    return x_flare, z_flare, y_shift


def _helmet_shell_deform(pos: np.ndarray) -> np.ndarray:
    """Sculpt Option A rounded yellow plastic dome + flat horizontal front/side brim + rear ducktail."""
    p = pos.copy()
    x, y, z = p[:, 0], p[:, 1], p[:, 2]

    y_min = float(y.min())
    y_max = float(y.max())
    h = np.clip((y - y_min) / max(1e-6, y_max - y_min), 0.0, 1.0)

    brim_t = np.clip(1.0 - h / 0.24, 0.0, 1.0)
    brim = brim_t ** 1.60

    theta = np.arctan2(x, z)
    x_flare, z_flare, y_shift = _brim_radial_and_y(theta)

    p[:, 0] = x * (1.0 + brim * x_flare)
    p[:, 2] = z * (1.0 + brim * z_flare)

    # Lift the front/side brim shelf horizontally flat while letting the rear ducktail sweep down smoothly.
    rear_w = (0.5 * (1.0 - np.cos(theta))) ** 1.45
    flatten_lift = 0.044 * (1.0 - 0.30 * rear_w) * (brim_t ** 1.35)
    edge_mask = np.clip(1.0 - h / 0.06, 0.0, 1.0) ** 2
    lip_curl = 0.007 * (0.5 + 0.5 * np.cos(theta)) * edge_mask
    p[:, 1] = y + flatten_lift + brim * y_shift + lip_curl
    return p


def _build_option_a_billboard(
    billboard_w: float,
    billboard_h: float,
    curve_depth: float,
) -> MeshPart:
    """Build Option A's wide, gently-curved vertical yellow plastic front billboard crown face."""
    def _sculpt_billboard(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        # u_y: 0.0 at brim base -> 1.0 at gently arched wide top crest.
        u_y = np.clip((p[:, 1] + 0.5 * billboard_h) / max(1e-6, billboard_h), 0.0, 1.0)
        # Gentle trapezoidal taper toward top so the wide billboard blends cleanly into the crown dome.
        taper = 1.0 - 0.18 * (u_y ** 1.35)
        p[:, 0] *= taper
        nx = p[:, 0] / max(1e-6, 0.5 * billboard_w)
        # Broad gentle arch across the top edge (not pointed, not square).
        top_mask = np.clip((u_y - 0.50) / 0.50, 0.0, 1.0)
        p[:, 1] -= 0.022 * (nx * nx) * top_mask
        # Smooth horizontal wrap AND gentle upper crown sweep back so top/sides melt seamlessly into the dome.
        p[:, 2] -= curve_depth * (nx * nx) + 0.024 * (u_y ** 1.75)
        return p

    return rounded_box(
        billboard_w,
        billboard_h,
        0.116,
        bevel=0.020,
        segs=5,
        name="helmet_front_billboard",
        material=_HAT_MAT,
    ).deform(_sculpt_billboard)


def _build_giant_letter_a(billboard_w: float, billboard_h: float, curve_depth: float) -> List[MeshPart]:
    """Build a giant, bold, clean sans-serif crimson 'A' curving smoothly onto the yellow billboard.

    Sits directly on the bright yellow billboard face with ZERO dark backing plaque or shield.
    """
    parts: List[MeshPart] = []
    thickness = 0.024
    stroke_w = 0.036
    leg_len = 0.168
    tilt_deg = 20.5

    left_leg = (
        rounded_box(stroke_w, leg_len, thickness, bevel=0.0055, segs=3, name="letter_a_left", material="gear_accent")
        .rotate_xyz(rz=-tilt_deg, degrees=True)
        .translate(-0.032, 0.0, 0.0)
    )
    right_leg = (
        rounded_box(stroke_w, leg_len, thickness, bevel=0.0055, segs=3, name="letter_a_right", material="gear_accent")
        .rotate_xyz(rz=+tilt_deg, degrees=True)
        .translate(+0.032, 0.0, 0.0)
    )
    apex = (
        rounded_box(0.044, 0.028, thickness, bevel=0.0055, segs=2, name="letter_a_apex", material="gear_accent")
        .translate(0.0, 0.066, 0.0)
    )
    crossbar = (
        rounded_box(0.096, 0.029, thickness, bevel=0.005, segs=2, name="letter_a_bar", material="gear_accent")
        .translate(0.0, -0.022, 0.0)
    )
    raw_parts = [left_leg, right_leg, apex, crossbar]

    def _curve_onto_billboard(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        u_y = np.clip((p[:, 1] - 0.004 + 0.5 * billboard_h) / max(1e-6, billboard_h), 0.0, 1.0)
        nx = p[:, 0] / max(1e-6, 0.5 * billboard_w)
        p[:, 2] -= curve_depth * (nx * nx) + 0.024 * (u_y ** 1.75)
        return p

    for part in raw_parts:
        parts.append(part.deform(_curve_onto_billboard))
    return parts


def _measure_eyelid_max_y(
    scene: SceneBuilder,
    eye_left: Tuple[float, float, float],
    closed_angle: float,
) -> float:
    """Measure the highest world Y reached by eyelid_left across full blink=0..1."""
    parts = scene.nodes.get("eyelid_left", [])
    if not parts:
        return eye_left[1] + 0.092
    verts = np.vstack([p.positions.astype(np.float64) for p in parts])
    ey = float(eye_left[1])
    max_y = ey
    for frac in (0.0, 0.25, 0.50, 0.75, 1.0):
        ang = frac * closed_angle
        ca, sa = math.cos(ang), math.sin(ang)
        r_blink = np.array([[1.0, 0.0, 0.0], [0.0, ca, -sa], [0.0, sa, ca]], dtype=np.float64)
        p_y = (verts @ r_blink.T)[:, 1] + ey
        max_y = max(max_y, float(p_y.max()))
    return max_y


def _build_fire_helmet(
    scene: SceneBuilder,
    head_center: Tuple[float, float, float],
    head_radii: Tuple[float, float, float],
    head_top: Tuple[float, float, float],
    eye_left: Tuple[float, float, float],
    closed_angle: float,
) -> None:
    """Build Option A: 1998 Wide Billboard smooth yellow plastic firefighter helmet."""
    hx, _, hz = head_center
    hrx, _, hrz = head_radii
    eyelid_top_y = _measure_eyelid_max_y(scene, eye_left, closed_angle)

    # Seat crisp flat front/side brim snugly right on the brow above the cute eyes.
    shell_y = max(eyelid_top_y - 0.022, head_top[1] - 0.132)
    shell_z = hz - 0.008
    dome_rx = hrx + 0.016
    dome_ry = max(0.268, (head_top[1] - shell_y) + 0.148)
    dome_rz = hrz + 0.018

    # 1. Smooth sunny-yellow plastic dome + crisp flat front/side brim + sweeping downward rear ducktail.
    dome = (
        hemisphere_shell(
            dome_rx,
            dome_ry,
            dome_rz,
            thickness=0.020,
            start_phi=-0.04,
            u_segs=56,
            v_segs=26,
            e1=0.92,
            e2=0.92,
            deform_fn=_helmet_shell_deform,
            name="helmet_dome",
            material=_HAT_MAT,
        )
        .translate(hx, shell_y, shell_z)
    )
    scene.add("fire_helmet", dome)

    # Smooth rolled yellow plastic outer brim edge bead hugging the exact perimeter.
    n_lip = 64
    lip_pts: List[Tuple[float, float, float]] = []
    start_phi = -0.04
    c_phi = math.cos(start_phi)
    s_phi = math.sin(start_phi)
    y_base = dome_ry * s_phi
    for i in range(n_lip):
        ang = (i / n_lip) * 2.0 * math.pi
        bx = dome_rx * c_phi * math.sin(ang)
        bz = dome_rz * c_phi * math.cos(ang)
        xf, zf, ys = _brim_radial_and_y(np.array([ang]))
        cos_a = math.cos(ang)
        rw = (0.5 * (1.0 - cos_a)) ** 1.45
        lift = 0.044 * (1.0 - 0.30 * rw)
        curl = 0.007 * (0.5 + 0.5 * cos_a)
        lx = hx + bx * (1.0 + float(xf[0]))
        ly = shell_y + y_base + lift + float(ys[0]) + curl - 0.003
        lz = shell_z + bz * (1.0 + float(zf[0]))
        lip_pts.append((lx, ly, lz))

    brim_lip = swept_tube(
        lip_pts,
        0.013,
        radial_segs=10,
        closed=True,
        name="helmet_brim_lip",
        material=_HAT_MAT,
    )
    scene.add("fire_helmet", brim_lip)

    # Dark inner head-liner band tucked cleanly inside the yellow plastic shell.
    liner = (
        cylinder(hrx * 0.94, hrx * 0.97, 0.036, radial_segs=26, bevel=0.006, axis="y", name="helmet_liner", material="gear_dark")
        .scale(1.0, 1.0, hrz / max(1e-6, hrx))
        .translate(hx, shell_y + 0.056, hz)
    )
    scene.add("fire_helmet", liner)

    # 2. Clean subtle molded ribs along the side & back dome behind the wide front billboard.
    rib_specs = (
        (180.0, 0.22, 0.019, "back"),
        (86.0, 0.24, 0.016, "left"),
        (-86.0, 0.24, 0.016, "right"),
        (132.0, 0.22, 0.016, "bl"),
        (-132.0, 0.22, 0.016, "br"),
    )
    for az_deg, phi_end, peak_rad, tag in rib_specs:
        az = math.radians(az_deg)
        sa, ca = math.sin(az), math.cos(az)
        phis = np.linspace(0.5 * math.pi, phi_end, 6)
        r_pts: List[Tuple[float, float, float]] = []
        r_rads: List[float] = []
        for k, phi in enumerate(phis):
            cp = float(abs(math.cos(phi)) ** 0.92)
            sp = float(abs(math.sin(phi)) ** 0.92)
            t_arc = k / (len(phis) - 1)
            lift = 0.010 * (1.0 - 0.55 * (t_arc * t_arc))
            px = hx + (dome_rx + lift) * cp * sa
            py = shell_y + (dome_ry + lift) * sp
            pz = shell_z + (dome_rz + lift) * cp * ca
            r_pts.append((px, py, pz))
            r_rads.append(peak_rad * (0.95 - 0.35 * t_arc))
        rib = curved_tube(
            r_pts,
            r_rads,
            samples_per_seg=4,
            radial_segs=10,
            cap_rings=2,
            name=f"helmet_rib_{tag}",
            material=_HAT_MAT,
        )
        scene.add("fire_helmet", rib)

    # Subtle top crown spine ridge running smoothly from the billboard top back into the rear dome.
    crest_pts = [
        (hx, shell_y + dome_ry + 0.002, shell_z + dome_rz * 0.45),
        (hx, shell_y + dome_ry + 0.011, shell_z + 0.010),
        (hx, shell_y + dome_ry - 0.004, shell_z - dome_rz * 0.42),
    ]
    crest = curved_tube(
        crest_pts,
        [0.018, 0.022, 0.018],
        samples_per_seg=4,
        radial_segs=10,
        cap_rings=2,
        name="helmet_crown_crest",
        material=_HAT_MAT,
    )
    scene.add("fire_helmet", crest)

    # 3. Option A's wide, gently-curved vertical yellow front billboard face + giant red 'A' directly on it.
    billboard_w = dome_rx * 1.38  # Wide iconic 1998 front billboard (~0.298 wide)
    billboard_h = dome_ry * 0.84  # Tall proud vertical front panel (~0.226 tall)
    curve_depth = 0.064
    billboard_tilt_x = -11.5
    # Seat billboard deep into front dome so rear & top edges merge seamlessly with zero step in 201/901.
    billboard_pos = (hx, shell_y + 0.150, shell_z + dome_rz - 0.036)

    billboard = (
        _build_option_a_billboard(billboard_w, billboard_h, curve_depth)
        .rotate_xyz(rx=billboard_tilt_x, degrees=True)
        .translate(*billboard_pos)
    )
    scene.add("fire_helmet", billboard)

    # Giant bold crimson 'A' sitting cleanly and directly on the wide yellow front billboard (zero plaque!).
    for part in _build_giant_letter_a(billboard_w, billboard_h, curve_depth):
        placed = (
            part.translate(0.0, -0.004, 0.054)
            .rotate_xyz(rx=billboard_tilt_x, degrees=True)
            .translate(*billboard_pos)
        )
        scene.add("fire_helmet", placed)


def build_magnifying_glass(
    scene: SceneBuilder,
    right_hand: Tuple[float, float, float],
    right_hand_dir: Tuple[float, float, float],
) -> None:
    """Build sleek realistic see-through brass magnifying glass gripped upright in right hand (-X)."""
    hx, hy, hz = right_hand
    dx, dy, dz = right_hand_dir
    grip_x = hx + 0.022 * dx
    grip_y = hy + 0.022 * dy
    grip_z = hz + 0.022 * dz
    parts: List[MeshPart] = []

    handle_profile = [
        (0.000, -0.142),
        (0.017, -0.136),
        (0.023, -0.115),
        (0.021, -0.072),
        (0.017, -0.014),
        (0.018, +0.034),
        (0.021, +0.060),
        (0.000, +0.064),
    ]
    handle = lathe(
        handle_profile,
        radial_segs=20,
        cap_bottom=False,
        cap_top=False,
        axis="y",
        name="mag_handle",
        material="gear_accent",
    )
    parts.append(handle)

    pommel_cap = (
        sphere(0.015, u_segs=12, v_segs=10, name="mag_pommel_cap", material="gear_gold_metal")
        .scale(1.0, 0.72, 1.0)
        .translate(0.0, -0.138, 0.0)
    )
    parts.append(pommel_cap)

    ferrule_profile = [
        (0.022, +0.056),
        (0.026, +0.064),
        (0.021, +0.078),
        (0.017, +0.094),
        (0.023, +0.110),
        (0.019, +0.116),
    ]
    ferrule = lathe(
        ferrule_profile,
        radial_segs=20,
        cap_bottom=True,
        cap_top=True,
        axis="y",
        name="mag_ferrule",
        material="gear_gold_metal",
    )
    parts.append(ferrule)

    lens_r = 0.090
    lens_cy = 0.112 + lens_r  # +0.202

    outer_rim = (
        torus_arc(lens_r + 0.004, 0.0125, u_segs=34, v_segs=12, plane="xy", name="mag_rim_outer", material="gear_gold_metal")
        .scale(1.0, 1.0, 0.88)
        .translate(0.0, lens_cy, 0.0)
    )
    parts.append(outer_rim)

    for z_sign, tag in ((+1.0, "front"), (-1.0, "back")):
        bezel = (
            torus_arc(lens_r - 0.004, 0.0058, u_segs=30, v_segs=8, plane="xy", name=f"mag_bezel_{tag}", material="gear_brass_trim")
            .translate(0.0, lens_cy, z_sign * 0.008)
        )
        parts.append(bezel)

    lens = (
        ellipsoid(lens_r - 0.004, lens_r - 0.004, 0.012, u_segs=28, v_segs=16, name="mag_lens", material="glass_clear")
        .translate(0.0, lens_cy, 0.0)
    )
    parts.append(lens)

    for part in parts:
        placed = part.rotate_xyz(rx=18.0, ry=-12.0, rz=10.0, degrees=True)
        scene.add("magnifying_glass", placed)
    scene.set_node_transform("magnifying_glass", translation=(grip_x, grip_y, grip_z))


_build_magnifying_glass = build_magnifying_glass


def _add_fire_idle(scene: SceneBuilder) -> AnimationClip:
    """Attach Fire's calm, composed 1.50s breathing bounce + gentle chin-up helmet nod Idle clip."""
    dur = 1.50
    n_steps = 16
    times = [round(dur * k / n_steps, 5) for k in range(n_steps + 1)]

    def _rest_t(node: str) -> Tuple[float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.translation if xf is not None else (0.0, 0.0, 0.0)

    def _rest_r(node: str) -> Tuple[float, float, float, float]:
        xf = scene.get_node_transform(node)
        return xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)

    channels: List[AnimationChannel] = []
    pumps = [0.5 * (1.0 - math.cos(2.0 * math.pi * (k / n_steps))) for k in range(n_steps + 1)]
    pumps[-1] = pumps[0]

    bx, by, bz = _rest_t("upper_body")
    body_t = [(bx, by + 0.023 * p, bz - 0.003 * p) for p in pumps]
    body_t[-1] = body_t[0]
    channels.append(AnimationChannel("upper_body", "translation", list(times), body_t))

    r0_b = _rest_r("upper_body")
    body_r = [quat_mul(r0_b, quat_from_euler_xyz(rx=-0.024 * p)) for p in pumps]
    body_r[-1] = body_r[0]
    channels.append(AnimationChannel("upper_body", "rotation", list(times), body_r))

    r0_h = _rest_r("head_group")
    head_r = [
        quat_mul(
            r0_h,
            quat_from_euler_xyz(
                rx=-0.064 * pumps[k],
                rz=0.012 * math.sin(2.0 * math.pi * (k / n_steps)),
            ),
        )
        for k in range(n_steps + 1)
    ]
    head_r[-1] = head_r[0]
    channels.append(AnimationChannel("head_group", "rotation", list(times), head_r))

    if "magnifying_glass" in scene.nodes and scene.get_node("magnifying_glass"):
        r0_m = _rest_r("magnifying_glass")
        mag_r = [quat_mul(r0_m, quat_from_euler_xyz(rx=-0.028 * p, rz=-0.015 * p)) for p in pumps]
        mag_r[-1] = mag_r[0]
        channels.append(AnimationChannel("magnifying_glass", "rotation", list(times), mag_r))

    if "gaster_group" in scene.nodes:
        r0_g = _rest_r("gaster_group")
        g_r = [quat_mul(r0_g, quat_from_euler_xyz(rx=0.034 * p)) for p in pumps]
        g_r[-1] = g_r[0]
        channels.append(AnimationChannel("gaster_group", "rotation", list(times), g_r))

    if "mandibles" in scene.nodes and scene.get_node("mandibles"):
        r0_j = _rest_r("mandibles")
        j_r = [quat_mul(r0_j, quat_from_euler_xyz(rx=0.018 * p)) for p in pumps]
        j_r[-1] = j_r[0]
        channels.append(AnimationChannel("mandibles", "rotation", list(times), j_r))

    return scene.add_animation("Idle", channels, replace=True)


def build_fire(blink: float = 0.0, *, hold_magnifying_glass: bool = False) -> SceneBuilder:
    """Build the sculpted 3D Fire Ant SceneBuilder (Option A Wide Billboard hat, empty idle hands)."""
    scene, anchors = build_base_ant(
        scene_name="ant_fire",
        include_antennae=False,
        include_eyes=True,
        include_brows=False,
        brow_style="plucky",
        head_tilt_deg=3.5,
        head_yaw_deg=-3.0,
        head_pitch_deg=-15.5,
        eye_look=(0.05, 0.04),
        stance_asymmetry=0.35,
        include_idle=False,
        left_arm_pose="idle",
        right_arm_pose=_FIRE_RIGHT_ARM if hold_magnifying_glass else "idle",
        blink=blink,
    )
    _build_fire_helmet(
        scene,
        anchors["head_center"],
        anchors["head_radii"],
        anchors["head_top"],
        anchors["eye_left"],
        float(anchors.get("eyelid_closed_angle", 1.80)),
    )
    scene.attach_world_node("fire_helmet", "head_group", preserve_world_pose=False)
    head_xf = scene.get_node_transform("head")
    if head_xf is not None:
        scene.set_node_transform("fire_helmet", translation=head_xf.translation)

    if hold_magnifying_glass:
        build_magnifying_glass(scene, anchors["right_hand"], anchors["right_hand_dir"])
        scene.attach_world_node("magnifying_glass", "upper_body", preserve_world_pose=False)

    _add_fire_idle(scene)
    return scene


def build_ant(blink: float = 0.0, *, hold_magnifying_glass: bool = False) -> SceneBuilder:
    """Canonical entry point discovered by build_all.py."""
    return build_fire(blink=blink, hold_magnifying_glass=hold_magnifying_glass)
