"""Authentic 1998 Fire Ant ability animations (`StartFire` `afsf` 1.76 s & `Extinguish` `afxf` 1.20 s).

Pure-stdlib safe on import (mirrors `hit_clips.py` / `thief_attack.py`) and keeps `ant_fire.py`
compact while leaving `Idle`, `Attack`, `Hit`, `Blink`, and rest bounding box 100% unchanged:
- `StartFire` (`afsf`, 22f, 1.76 s): pulls out Fire Ant's brass magnifying glass (`fire_mag_glass`,
  reused directly from `ant_fire.build_magnifying_glass`), aims the horizontal lens forward while
  leaning in squinting at the focused sunbeam & glowing ember (`fire_spark`), ignites a bright
  flame burst on the ground, then stows the glass cleanly away.
- `Extinguish` (`afxf`, 12f, 1.20 s): crouches to grab a handheld crimson/steel fire extinguisher
  (`fire_extinguisher`), braces back heroically on planted legs blasting a billowing frothy white
  foam plume (`fire_foam`) forward onto the fire, then stows it cleanly back to rest.

All temporary prop/FX nodes (`fire_mag_glass`, `fire_spark`, `fire_extinguisher`, `fire_foam`) rest
hidden at `_HIDE_SCALE = (1e-4, 1e-4, 1e-4)` so rest/Idle/Attack/Hit bounds change by 0.000 mm.
"""
from __future__ import annotations

import math
from typing import Callable, Dict, List, Sequence, Tuple

try:
    import numpy as np
except ImportError:  # pragma: no cover - pure-stdlib import safety for CI
    np = None  # type: ignore

if np is not None:
    try:
        from .base_ant import EYELID_CLOSED_ANGLE, sync_leg_flex_to_clip
        from .leg_rig import _quat_conj
        from .mesh_core import (
            AnimationChannel,
            AnimationClip,
            MeshPart,
            SceneBuilder,
            cone,
            cylinder,
            ellipsoid,
            quat_from_euler_xyz,
            quat_mul,
            sphere,
            torus_arc,
        )
    except ImportError:
        from base_ant import EYELID_CLOSED_ANGLE, sync_leg_flex_to_clip  # type: ignore
        from leg_rig import _quat_conj  # type: ignore
        from mesh_core import (  # type: ignore
            AnimationChannel,
            AnimationClip,
            MeshPart,
            SceneBuilder,
            cone,
            cylinder,
            ellipsoid,
            quat_from_euler_xyz,
            quat_mul,
            sphere,
            torus_arc,
        )

Vec3 = Tuple[float, float, float]
Quat = Tuple[float, float, float, float]

_HIDE_SCALE: Vec3 = (1e-4, 1e-4, 1e-4)
_HIDE_POS: Vec3 = (0.0, 0.35, 0.0)


def _rest_t(scene: SceneBuilder, node: str) -> Vec3:
    xf = scene.get_node_transform(node)
    return xf.translation if xf is not None else (0.0, 0.0, 0.0)


def _rest_r(scene: SceneBuilder, node: str) -> Quat:
    xf = scene.get_node_transform(node)
    return xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)


def _align_quats(qs: Sequence[Quat], *, close_loop: bool = True) -> List[Quat]:
    out: List[Quat] = [qs[0]]
    for q in qs[1:]:
        if sum(a * b for a, b in zip(out[-1], q)) < 0.0:
            q = (-q[0], -q[1], -q[2], -q[3])
        out.append(q)
    if close_loop and len(out) >= 2:
        q_end = out[0]
        if sum(a * b for a, b in zip(out[-2], q_end)) < 0.0:
            q_end = (-q_end[0], -q_end[1], -q_end[2], -q_end[3])
        out[-1] = q_end
    return out


def _blink_closed_angle(scene: SceneBuilder) -> float:
    blink = next((c for c in scene.animations if c.name == "Blink"), None)
    if blink is not None:
        for ch in blink.channels:
            if ch.target_node == "eyelid_left" and ch.path == "rotation" and ch.values:
                inv0 = _quat_conj(_rest_r(scene, "eyelid_left"))
                best = 0.0
                for q in ch.values:
                    dq = quat_mul(inv0, q)
                    ang = 2.0 * math.atan2(float(dq[0]), max(1e-9, abs(float(dq[3]))))
                    if ang > best:
                        best = ang
                if best > 0.1:
                    return best
    return float(EYELID_CLOSED_ANGLE)


def _eyelid_channels(scene: SceneBuilder, times: Sequence[float], squints: Sequence[float]) -> List[AnimationChannel]:
    ang_max = _blink_closed_angle(scene)
    out: List[AnimationChannel] = []
    for lid_n in ("eyelid_left", "eyelid_right"):
        if lid_n not in scene.nodes or not scene.get_node(lid_n):
            continue
        r0 = _rest_r(scene, lid_n)
        qs = _align_quats([quat_mul(r0, quat_from_euler_xyz(rx=ang_max * float(sq))) for sq in squints])
        out.append(AnimationChannel(lid_n, "rotation", list(times), qs))
    return out


def _attach_fire_mag_glass(
    scene: SceneBuilder,
    anchors: Dict[str, Tuple[float, ...]],
    mag_builder: Callable[..., None],
) -> Tuple[str, Vec3]:
    """Build hidden `fire_mag_glass` on `forearm_right` by reusing `ant_fire.build_magnifying_glass`."""
    tmp = SceneBuilder(name="mag_tmp")
    mag_builder(tmp, (0.0, 0.0, 0.0), (0.0, 0.0, 1.0))
    raw_parts = tmp.nodes.get("magnifying_glass", [])
    # Reorient cleanly in forearm_right space so extending the arm forward holds the brass rim
    # flat horizontally (lens facing UP/DOWN) out over the ignition spot, matching 1998 `afsf901`!
    placed_parts = [
        MeshPart(
            f"fire_{p.name}",
            p.material,
            p.positions.copy(),
            p.normals.copy(),
            p.indices.copy(),
        )
        .scale(1.16, 1.16, 1.16)
        .rotate_xyz(rx=78.0, ry=8.0, rz=-14.0, degrees=True)
        .translate(0.0, 0.0, 0.055)
        for p in raw_parts
    ]
    rh = anchors["right_hand"]
    er = anchors["elbow_right"]
    hand_local: Vec3 = (float(rh[0] - er[0]), float(rh[1] - er[1]), float(rh[2] - er[2]))
    scene.add("fire_mag_glass", placed_parts, parent="forearm_right")
    scene.set_node_transform("fire_mag_glass", translation=hand_local, scale=_HIDE_SCALE)
    return "fire_mag_glass", hand_local


def _attach_fire_sunbeam(scene: SceneBuilder) -> str:
    """Attach hidden focused golden-white sunbeam column + ground hotspot + rising smoke (`afsf f7-f17`)."""
    # Built in local coords with ground hotspot at Y = 0.0 and beam rising cleanly up to the held lens (`Y = 0.402`).
    parts: List[MeshPart] = [
        # Bright glowing white-hot solar inner core ray shooting straight down from lens center to ground.
        cylinder(0.011, 0.014, 0.402, radial_segs=14, bevel=0.003, axis="y", name="sunbeam_core", material="eye_catchlight")
        .translate(0.0, 0.204, 0.0),
        # Warm golden sunlight shaft wrapping the white-hot core (`afsf901` iconic yellow-white ray!).
        cylinder(0.019, 0.024, 0.396, radial_segs=16, bevel=0.003, axis="y", name="sunbeam_gold_shaft", material="gear_yellow_plastic")
        .translate(0.0, 0.201, 0.0),
        # Translucent outer focus beam halo tapering cleanly down from lens to focal spot.
        cylinder(0.028, 0.056, 0.402, radial_segs=18, bevel=0.004, axis="y", name="sunbeam_cone", material="glass_clear")
        .translate(0.0, 0.204, 0.0),
        # Glowing golden solar hotspot ember disc + bright center spark right on the ground (`Y = 0.006`).
        ellipsoid(0.056, 0.014, 0.056, u_segs=16, v_segs=10, name="sunbeam_hotspot", material="gear_yellow_plastic")
        .translate(0.0, 0.014, 0.0),
        ellipsoid(0.030, 0.018, 0.030, u_segs=12, v_segs=8, name="sunbeam_flash_core", material="eye_catchlight")
        .translate(0.0, 0.018, 0.0),
        # Delicate curling wisps of white smoke rising beside the beam toward the magnifying lens (`afsf f8-f16`).
        ellipsoid(0.015, 0.022, 0.015, u_segs=10, v_segs=8, name="sunbeam_smoke_0", material="eye_white")
        .translate(-0.028, 0.105, -0.014),
        ellipsoid(0.019, 0.026, 0.019, u_segs=10, v_segs=8, name="sunbeam_smoke_1", material="eye_white")
        .translate(+0.022, 0.225, +0.016),
        ellipsoid(0.022, 0.024, 0.022, u_segs=10, v_segs=8, name="sunbeam_smoke_2", material="eye_white")
        .translate(-0.018, 0.335, -0.018),
    ]
    scene.add("fire_sunbeam", parts)
    scene.set_node_transform("fire_sunbeam", translation=_HIDE_POS, scale=_HIDE_SCALE)
    return "fire_sunbeam"


def _attach_fire_spark(scene: SceneBuilder) -> str:
    """Attach hidden roaring layered campfire flame + billowing ignition smoke cloud (`afsf f17-f21`)."""
    # Built with flame base seated cleanly at local Y = 0.0 (`~0.38m` tall roaring flame matching `afsf901`!).
    parts: List[MeshPart] = [
        # Hot glowing white-yellow heart + tall vibrant golden flame spire.
        ellipsoid(0.076, 0.084, 0.076, u_segs=16, v_segs=12, name="fire_ember_core", material="eye_catchlight")
        .translate(0.0, 0.076, 0.0),
        ellipsoid(0.112, 0.128, 0.112, u_segs=16, v_segs=12, name="fire_inner_body", material="gear_yellow_plastic")
        .translate(0.0, 0.116, 0.0),
        cone(0.098, 0.37, radial_segs=14, name="fire_tongue_center", material="gear_yellow_plastic")
        .translate(0.0, 0.210, 0.0),
    ]
    # Ring of 7 tall dynamic outer flame tongues (alternating crimson-orange & bright gold) flaring outward!
    for i in range(7):
        az = 2.0 * math.pi * i / 7.0
        tall = 0.32 if i % 2 == 0 else 0.26
        tilt = 0.38 if i % 2 == 0 else 0.28
        mat = "gear_accent" if i % 2 == 0 else "gear_gold"
        rad = 0.070 if i % 2 == 0 else 0.056
        parts.append(
            cone(rad, tall, radial_segs=12, name=f"fire_tongue_{i}", material=mat)
            .rotate_xyz(rx=tilt)
            .rotate_xyz(ry=az)
            .translate(0.082 * math.sin(az), 0.140, 0.082 * math.cos(az))
        )
    # Soft wide billowing white smoke cloud crowning the freshly ignited flame (`afsf901 f18-f19`).
    for s_idx, (sx, sy, sz, rx, ry, rz) in enumerate((
        (0.000, 0.385, 0.000, 0.096, 0.054, 0.092),
        (+0.072, 0.398, +0.028, 0.068, 0.044, 0.064),
        (-0.070, 0.402, -0.024, 0.066, 0.046, 0.064),
        (+0.014, 0.432, -0.022, 0.064, 0.042, 0.060),
    )):
        parts.append(
            ellipsoid(rx, ry, rz, u_segs=14, v_segs=10, name=f"fire_smoke_puff_{s_idx}", material="eye_white")
            .translate(sx, sy, sz)
        )
    scene.add("fire_spark", parts)
    scene.set_node_transform("fire_spark", translation=_HIDE_POS, scale=_HIDE_SCALE)
    return "fire_spark"


def _attach_fire_extinguisher_and_foam(scene: SceneBuilder) -> Tuple[str, str]:
    """Attach hidden handheld crimson fire extinguisher (`fire_extinguisher`) & billowing foam (`fire_foam`)."""
    ext_parts: List[MeshPart] = [
        # Compact crimson pressurized tank gripped horizontally/angled across hands (`afxf301`/`901 f4-f7`).
        cylinder(0.052, 0.052, 0.185, radial_segs=18, bevel=0.012, axis="z", name="ext_tank", material="gear_accent"),
        sphere(0.052, u_segs=14, v_segs=10, name="ext_dome_front", material="gear_accent").scale(1.0, 1.0, 0.65).translate(0.0, 0.0, +0.092),
        sphere(0.052, u_segs=14, v_segs=10, name="ext_dome_back", material="gear_accent").scale(1.0, 1.0, 0.55).translate(0.0, 0.0, -0.092),
        # Brass valve collar & steel trigger lever + flared discharge horn pointing straight +Z.
        cylinder(0.022, 0.026, 0.042, radial_segs=14, bevel=0.004, axis="z", name="ext_valve", material="gear_gold_metal").translate(0.0, 0.0, +0.125),
        torus_arc(0.032, 0.008, u_segs=16, v_segs=8, plane="yz", name="ext_handle", material="gear_steel").translate(0.0, +0.042, +0.070),
        cone(0.042, 0.095, radial_segs=14, name="ext_horn", material="gear_steel").rotate_xyz(rx=-90.0, degrees=True).translate(0.0, 0.0, +0.175),
    ]
    scene.add("fire_extinguisher", ext_parts, parent="upper_body")
    scene.set_node_transform("fire_extinguisher", translation=(0.0, 0.05, 0.0), scale=_HIDE_SCALE)

    # Frothy multi-puff billowing white foam blast (`afxf301`/`901 f4-f7`), rooted at nozzle origin (0, 0, 0).
    foam_specs = (
        (0.000, 0.000, +0.065, 0.044, 0.042, 0.058, "eye_white"),
        (+0.032, +0.018, +0.145, 0.064, 0.058, 0.072, "fuse_white"),
        (-0.036, -0.012, +0.165, 0.062, 0.056, 0.070, "eye_white"),
        (+0.014, -0.020, +0.245, 0.088, 0.078, 0.096, "eye_white"),
        (-0.048, +0.024, +0.265, 0.078, 0.072, 0.082, "fuse_white"),
        (+0.056, +0.008, +0.325, 0.084, 0.076, 0.088, "eye_white"),
        (-0.018, -0.015, +0.355, 0.096, 0.082, 0.102, "fuse_white"),
    )
    foam_parts: List[MeshPart] = [
        ellipsoid(rx, ry, rz, u_segs=12, v_segs=8, name=f"ext_foam_{idx}", material=mat).translate(cx, cy, cz)
        for idx, (cx, cy, cz, rx, ry, rz, mat) in enumerate(foam_specs)
    ]
    scene.add("fire_foam", foam_parts)
    scene.set_node_transform("fire_foam", translation=_HIDE_POS, scale=_HIDE_SCALE)
    return "fire_extinguisher", "fire_foam"


def _add_start_fire(scene: SceneBuilder, hand_local: Vec3) -> AnimationClip:
    """Authentic `afsf` 1.76 s (22f): draw glass from belt -> focus golden sunbeam -> roar flame -> re-holster."""
    # Exact 22f cadence = 1.76 s:
    # f0..f2 (0.00-0.24s) reach to right-hip belt holster & draw magnifying glass (`fire_belt_mag_glass` hides)
    # f3     (0.28-0.40s) flourish swing glass up-and-forward (`afsf f3`)
    # f4..f6 (0.44-0.68s) lean forward holding lens horizontally level straight ahead (`afsf f4-f6`)
    # f7..f16(0.70-1.26s) golden vertical sunbeam focuses onto ground hotspot with curling smoke (`afsf f7-f16`)
    # f17    (1.30-1.36s) bright flash ignition at ground spot (`afsf f17`)
    # f18..f21(1.38-1.76s) roaring campfire flame + smoke cloud, recoil & re-holster glass onto belt!
    # (t, dy, dz, ub_rx, ub_ry, head_rx, head_ry, gaster_rx, r_arm_rx, r_arm_ry, r_arm_rz, r_fore_rx, l_arm_rx, l_arm_rz, squint, belt_s, mag_s, beam_s, spark_s, spark_ry)
    rows = [
        (0.00, +0.000, +0.000, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, 0.00, 1.00, 1e-4, 1e-4, 1e-4, 0.00),
        (0.14, -0.010, +0.004, +0.16, -0.34, +0.10, -0.22, +0.06, +0.52, -0.44, +0.28, -0.42, -0.06, +0.18, 0.08, 1.00, 1e-4, 1e-4, 1e-4, 0.00),  # f1: reach right hand to hip belt
        (0.22, -0.012, +0.008, +0.18, -0.30, +0.08, -0.18, +0.06, +0.34, -0.32, +0.18, -0.36, -0.08, +0.22, 0.10, 1e-4, 0.92, 1e-4, 1e-4, 0.00),  # f2: unclip glass from belt!
        (0.34, -0.008, +0.018, +0.12, -0.06, +0.04, -0.04, +0.04, -0.82, -0.18, -0.38, -0.16, -0.14, +0.28, 0.16, 1e-4, 1.04, 1e-4, 1e-4, 0.00),  # f3: flourish swing high & forward!
        (0.48, -0.016, +0.030, +0.28, +0.10, +0.16, +0.06, +0.10, -0.58, +0.34, +0.12, -0.06, -0.20, +0.34, 0.36, 1e-4, 1.06, 1e-4, 1e-4, 0.00),  # f4-f5: level lens over target
        (0.68, -0.018, +0.034, +0.30, +0.12, +0.18, +0.06, +0.12, -0.60, +0.36, +0.12, -0.06, -0.22, +0.36, 0.48, 1e-4, 1.06, 0.65, 1e-4, 0.00),  # f7: sunbeam strikes ground!
        (0.92, -0.020, +0.036, +0.32, +0.10, +0.20, +0.04, +0.12, -0.58, +0.34, +0.14, -0.05, -0.22, +0.38, 0.56, 1e-4, 1.06, 1.04, 1e-4, 0.40),  # f11: focused golden beam + smoke
        (1.16, -0.022, +0.038, +0.34, +0.12, +0.22, +0.06, +0.14, -0.60, +0.36, +0.12, -0.06, -0.24, +0.40, 0.62, 1e-4, 1.06, 1.12, 1e-4, 1.20),  # f15: glowing hot ember!
        (1.30, -0.018, +0.034, +0.28, +0.08, +0.16, +0.04, +0.10, -0.56, +0.34, +0.12, -0.08, -0.28, +0.46, 0.40, 1e-4, 1.04, 1.32, 0.48, 2.20),  # f17: IGNITION FLASH!
        (1.40, +0.014, -0.016, -0.22, -0.18, -0.14, -0.10, -0.12, -1.18, -0.34, -0.56, -0.36, -0.36, +0.58, 0.00, 1e-4, 0.96, 1e-4, 1.16, 3.30),  # f18: WHOOSH! whip glass HIGH & BACK away from flame!
        (1.50, +0.008, -0.010, -0.06, -0.36, -0.04, -0.22, -0.02, -0.42, -0.62, -0.32, -0.42, -0.20, +0.34, 0.00, 1e-4, 0.88, 1e-4, 1.12, 4.30),  # f19: swing glass cleanly around outside to hip
        (1.58, +0.004, -0.004, +0.06, -0.26, +0.02, -0.14, +0.02, +0.34, -0.36, +0.18, -0.32, -0.10, +0.18, 0.00, 1.00, 1e-4, 1e-4, 1.08, 5.15),  # f20: glass clipped back on belt!
        (1.70, +0.002, -0.002, -0.02, -0.04, -0.02, -0.02, +0.00, +0.06, -0.06, +0.04, -0.06, -0.04, +0.06, 0.00, 1.00, 1e-4, 1e-4, 0.82, 5.95),  # f21: admire burning fire
        (1.76, +0.000, +0.000, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, 0.00, 1.00, 1e-4, 1e-4, 1e-4, 6.2832),
    ]
    times = [round(r[0], 4) for r in rows]
    bx, by, bz = _rest_t(scene, "upper_body")
    r_ub0 = _rest_r(scene, "upper_body")
    r_hd0 = _rest_r(scene, "head_group")
    r_gs0 = _rest_r(scene, "gaster_group")
    r_ar0 = _rest_r(scene, "arm_right")
    r_fr0 = _rest_r(scene, "forearm_right")
    r_al0 = _rest_r(scene, "arm_left")
    r_fl0 = _rest_r(scene, "forearm_left")

    # Target ground ignition spot mathematically aligned dead-center beneath `fire_mag_lens` during f7-f17!
    spot_pos: Vec3 = (0.008, 0.002, 0.546)

    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), [(bx, by + r[1], bz + r[2]) for r in rows]),
        AnimationChannel("upper_body", "rotation", list(times), _align_quats([quat_mul(r_ub0, quat_from_euler_xyz(rx=r[3], ry=r[4])) for r in rows])),
        AnimationChannel("head_group", "rotation", list(times), _align_quats([quat_mul(r_hd0, quat_from_euler_xyz(rx=r[5], ry=r[6])) for r in rows])),
        AnimationChannel("gaster_group", "rotation", list(times), _align_quats([quat_mul(r_gs0, quat_from_euler_xyz(rx=r[7])) for r in rows])),
        AnimationChannel("arm_right", "rotation", list(times), _align_quats([quat_mul(r_ar0, quat_from_euler_xyz(rx=r[8], ry=r[9], rz=r[10])) for r in rows])),
        AnimationChannel("forearm_right", "rotation", list(times), _align_quats([quat_mul(r_fr0, quat_from_euler_xyz(rx=r[11])) for r in rows])),
        AnimationChannel("arm_left", "rotation", list(times), _align_quats([quat_mul(r_al0, quat_from_euler_xyz(rx=r[12], rz=r[13])) for r in rows])),
        AnimationChannel("forearm_left", "rotation", list(times), _align_quats([quat_mul(r_fl0, quat_from_euler_xyz(rx=-0.18 * (r[13] > 0.2))) for r in rows])),
        AnimationChannel("fire_mag_glass", "translation", list(times), [hand_local for _ in rows]),
        AnimationChannel("fire_mag_glass", "scale", list(times), [(r[16], r[16], r[16]) for r in rows]),
        AnimationChannel("fire_sunbeam", "translation", list(times), [spot_pos if r[17] > 1e-3 else _HIDE_POS for r in rows]),
        AnimationChannel("fire_sunbeam", "rotation", list(times), _align_quats([quat_from_euler_xyz(ry=r[19] * 1.5) for r in rows])),
        AnimationChannel("fire_sunbeam", "scale", list(times), [(r[17], 1.0 if r[17] > 1e-3 else 1e-4, r[17]) for r in rows]),
        AnimationChannel("fire_spark", "translation", list(times), [spot_pos if r[18] > 1e-3 else _HIDE_POS for r in rows]),
        AnimationChannel("fire_spark", "rotation", list(times), _align_quats([quat_from_euler_xyz(ry=r[19]) for r in rows])),
        AnimationChannel("fire_spark", "scale", list(times), [(r[18], r[18], r[18]) for r in rows]),
    ]
    if "fire_belt_mag_glass" in scene.nodes:
        channels.append(
            AnimationChannel("fire_belt_mag_glass", "scale", list(times), [(r[15], r[15], r[15]) for r in rows])
        )
    channels.extend(_eyelid_channels(scene, times, [r[14] for r in rows]))
    clip = scene.add_animation("StartFire", channels, replace=True)
    sync_leg_flex_to_clip(scene, "StartFire", swivel=0.0)
    return clip


def _add_extinguish(scene: SceneBuilder) -> AnimationClip:
    """Authentic `afxf` 1.20 s (12f [100x12]ms) extinguisher draw -> billowing white foam blast -> stow."""
    # Exact 12f [100x12]ms = 1.20s matching `afxf301`/`afxf901`: bend/grab f0-f2 -> foam blast f3-f7 -> stow f8-f11.
    # (t, dy, dz, ub_rx, head_rx, gaster_rx, arm_rx, arm_ry, arm_rz, fore_rx, squint, ext_s, foam_s_xz, foam_s_z, foam_ry)
    rows = [
        (0.00, +0.000, +0.000, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, 0.00, 1e-4, 1e-4, 1e-4, 0.00),
        (0.12, -0.022, +0.022, +0.44, +0.16, +0.14, +0.26, -0.18, +0.28, -0.26, 0.15, 1e-4, 1e-4, 1e-4, 0.00),  # f1: bend to grab nozzle
        (0.24, -0.028, +0.028, +0.58, +0.18, +0.18, +0.12, -0.34, +0.22, -0.36, 0.25, 0.88, 1e-4, 1e-4, 0.00),  # f2: lift extinguisher
        (0.36, -0.004, -0.012, -0.06, -0.04, -0.06, -0.48, -0.42, +0.18, -0.34, 0.35, 1.00, 0.72, 0.65, +0.08),  # f3-f4: PSHHH start!
        (0.48, +0.012, -0.026, -0.18, -0.08, -0.12, -0.56, -0.46, +0.22, -0.38, 0.48, 1.02, 1.15, 1.22, -0.10),  # f5: peak heroic foam blast!
        (0.60, +0.006, -0.020, -0.12, -0.06, -0.08, -0.52, -0.44, +0.20, -0.36, 0.42, 1.00, 0.96, 1.04, +0.12),  # f6: sweeping foam jet
        (0.72, +0.014, -0.028, -0.20, -0.10, -0.14, -0.58, -0.46, +0.24, -0.38, 0.45, 1.02, 1.12, 1.18, -0.08),  # f7: second blast pulse!
        (0.84, -0.014, +0.014, +0.26, +0.10, +0.08, -0.16, -0.20, +0.22, -0.22, 0.18, 0.78, 0.48, 0.45, +0.04),  # f8: foam fading
        (0.98, -0.020, +0.020, +0.36, +0.14, +0.12, +0.14, -0.10, +0.18, -0.18, 0.08, 1e-4, 1e-4, 1e-4, 0.00),  # f9-f10: stow extinguisher
        (1.10, -0.006, +0.006, +0.10, +0.04, +0.04, +0.04, -0.04, +0.06, -0.06, 0.00, 1e-4, 1e-4, 1e-4, 0.00),  # f11: rising
        (1.20, +0.000, +0.000, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, +0.00, 0.00, 1e-4, 1e-4, 1e-4, 0.00),
    ]
    times = [round(r[0], 4) for r in rows]
    bx, by, bz = _rest_t(scene, "upper_body")
    r_ub0 = _rest_r(scene, "upper_body")
    r_hd0 = _rest_r(scene, "head_group")
    r_gs0 = _rest_r(scene, "gaster_group")

    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), [(bx, by + r[1], bz + r[2]) for r in rows]),
        AnimationChannel("upper_body", "rotation", list(times), _align_quats([quat_mul(r_ub0, quat_from_euler_xyz(rx=r[3])) for r in rows])),
        AnimationChannel("head_group", "rotation", list(times), _align_quats([quat_mul(r_hd0, quat_from_euler_xyz(rx=r[4])) for r in rows])),
        AnimationChannel("gaster_group", "rotation", list(times), _align_quats([quat_mul(r_gs0, quat_from_euler_xyz(rx=r[5])) for r in rows])),
        # Handheld extinguisher sits tucked cleanly in front of Fire's gripped hands during f2..f8!
        AnimationChannel("fire_extinguisher", "translation", list(times), [(0.0, 0.02, 0.22) if r[11] > 1e-3 else (0.0, 0.05, 0.0) for r in rows]),
        AnimationChannel("fire_extinguisher", "rotation", list(times), _align_quats([quat_from_euler_xyz(rx=+0.22, ry=r[14]) for r in rows])),
        AnimationChannel("fire_extinguisher", "scale", list(times), [(r[11], r[11], r[11]) for r in rows]),
        # Frothy white foam blast shooting forward & downward toward the fire wall (`min_y >= 0.04`!).
        AnimationChannel("fire_foam", "translation", list(times), [(0.0, 0.30, 0.34) if r[12] > 1e-3 else _HIDE_POS for r in rows]),
        AnimationChannel("fire_foam", "rotation", list(times), _align_quats([quat_from_euler_xyz(rx=+0.34, ry=r[14]) for r in rows])),
        AnimationChannel("fire_foam", "scale", list(times), [(r[12], r[12], r[13]) for r in rows]),
    ]
    for side_tag, s in (("left", +1.0), ("right", -1.0)):
        arm_n, fore_n = f"arm_{side_tag}", f"forearm_{side_tag}"
        if arm_n in scene.nodes:
            r0_a = _rest_r(scene, arm_n)
            channels.append(
                AnimationChannel(
                    arm_n,
                    "rotation",
                    list(times),
                    _align_quats([quat_mul(r0_a, quat_from_euler_xyz(rx=r[6], ry=s * r[7], rz=s * r[8])) for r in rows]),
                )
            )
        if fore_n in scene.nodes:
            r0_f = _rest_r(scene, fore_n)
            channels.append(
                AnimationChannel(
                    fore_n,
                    "rotation",
                    list(times),
                    _align_quats([quat_mul(r0_f, quat_from_euler_xyz(rx=r[9])) for r in rows]),
                )
            )
    channels.extend(_eyelid_channels(scene, times, [r[10] for r in rows]))
    clip = scene.add_animation("Extinguish", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Extinguish", swivel=0.0)
    return clip


def add_fire_abilities(
    scene: SceneBuilder,
    anchors: Dict[str, Tuple[float, ...]],
    mag_builder: Callable[..., None],
) -> List[AnimationClip]:
    """Attach hidden Fire Ant ability props & both authentic 1998 clips (`StartFire`, `Extinguish`)."""
    _, hand_local = _attach_fire_mag_glass(scene, anchors, mag_builder)
    _attach_fire_sunbeam(scene)
    _attach_fire_spark(scene)
    _attach_fire_extinguisher_and_foam(scene)
    return [
        _add_start_fire(scene, hand_local),
        _add_extinguish(scene),
    ]


__all__ = ["add_fire_abilities"]
