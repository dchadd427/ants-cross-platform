"""Authentic 1998 class-specific ability animations for Thief and Bomber.

Split into a dedicated module to keep `ant_thief.py` and `ant_bomber.py` well under
per-file line limits while staying pure-stdlib safe on import (CI without numpy).

Exact 1998 CHD ability animations (`docs/chd_table4_animations.json`):
- Thief  `"Steal"`      (`atcr501`, 33 frames, 3.51 s):
    low sneaky lookout -> crouch & pry anthill -> scoop stolen golden food loot -> triumphant pop-up
- Bomber `"PlantBomb"`  (`absb`, 17 frames, 1.36 s):
    reach behind shoulder -> pull cup-bomb forward & inspect -> crouch & plant on ground -> hop upright
- Bomber `"DefuseBomb"` (`abdb`, 12 frames, 1.10 s):
    peer cautiously at ground bomb -> crouch & pinch/snuff fuse (squinting eyes) -> puff recoil -> brow wipe
"""
from __future__ import annotations

import math
from typing import Dict, List, Sequence, Tuple

try:
    import numpy as np
except ImportError:  # pragma: no cover - pure-stdlib import safety for CI
    np = None  # type: ignore

if np is not None:
    try:
        from .base_ant import sync_leg_flex_to_clip
        from .mesh_core import (
            AnimationChannel,
            AnimationClip,
            MeshPart,
            SceneBuilder,
            curved_tube,
            cylinder,
            ellipsoid,
            quat_from_axis_angle,
            quat_from_euler_xyz,
            quat_mul,
            sphere,
            torus_arc,
        )
    except ImportError:
        from base_ant import sync_leg_flex_to_clip  # type: ignore
        from mesh_core import (  # type: ignore
            AnimationChannel,
            AnimationClip,
            MeshPart,
            SceneBuilder,
            curved_tube,
            cylinder,
            ellipsoid,
            quat_from_axis_angle,
            quat_from_euler_xyz,
            quat_mul,
            sphere,
            torus_arc,
        )

Vec3 = Tuple[float, float, float]
Quat = Tuple[float, float, float, float]
_TINY: Vec3 = (1e-4, 1e-4, 1e-4)
_ONE: Vec3 = (1.0, 1.0, 1.0)


def ensure_ability_root(scene: SceneBuilder) -> str:
    """Ensure an identity `"root"` node wraps all top-level nodes without altering rest pose."""
    if "root" not in scene.nodes:
        top_nodes = [n for n in list(scene.nodes.keys()) if scene.get_node_parent(n) is None]
        scene.nodes["root"] = []
        scene.set_node_transform(
            "root",
            translation=(0.0, 0.0, 0.0),
            rotation=(0.0, 0.0, 0.0, 1.0),
            scale=(1.0, 1.0, 1.0),
        )
        for n in top_nodes:
            scene.set_node_parent(n, "root")
    return "root"


def rest_t(scene: SceneBuilder, node: str) -> Vec3:
    xf = scene.get_node_transform(node)
    return xf.translation if xf is not None else (0.0, 0.0, 0.0)


def rest_r(scene: SceneBuilder, node: str) -> Quat:
    xf = scene.get_node_transform(node)
    return xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)


def quat_track(r0: Quat, deltas: Sequence[Vec3], *, close_loop: bool = True) -> List[Quat]:
    """Build a hemisphere-continuous quaternion list from Euler `(rx, ry, rz)` deltas."""
    out: List[Quat] = []
    for rx, ry, rz in deltas:
        q = quat_mul(r0, quat_from_euler_xyz(rx=rx, ry=ry, rz=rz))
        if out and sum(a * b for a, b in zip(out[-1], q)) < 0.0:
            q = (-q[0], -q[1], -q[2], -q[3])
        out.append(q)
    if close_loop and len(out) >= 2:
        q_end = out[0]
        if sum(a * b for a, b in zip(out[-2], q_end)) < 0.0:
            q_end = (-q_end[0], -q_end[1], -q_end[2], -q_end[3])
        out[-1] = q_end
    return out


def _add_thief_loot_prop(scene: SceneBuilder) -> Tuple[str, Vec3]:
    """Add a small golden food-crumb loot prop hidden inside thorax (`scale=1e-4`) at rest."""
    node = "thief_loot_crumb"
    parts: List[MeshPart] = [
        ellipsoid(0.056, 0.044, 0.050, u_segs=14, v_segs=10, name="loot_crumb_main", material="gear_gold"),
        ellipsoid(0.036, 0.030, 0.034, u_segs=12, v_segs=8, name="loot_crumb_lobe_a", material="gear_gold").translate(+0.030, +0.014, +0.012),
        ellipsoid(0.032, 0.028, 0.030, u_segs=12, v_segs=8, name="loot_crumb_lobe_b", material="gear_gold").translate(-0.028, -0.010, +0.016),
        sphere(0.016, u_segs=10, v_segs=8, name="loot_crumb_gleam", material="gear_gold_metal").translate(+0.012, +0.028, +0.022),
    ]
    for p in parts:
        scene.add(node, p)
    # Parent to upper_body at (0, 0, 0) inside the opaque thorax so rest/Idle/Attack/Hit bounds change by 0.000 mm!
    scene.set_node_parent(node, "upper_body")
    origin: Vec3 = (0.0, 0.0, 0.0)
    scene.set_node_transform(node, translation=origin, scale=_TINY)
    return node, origin


def _add_thief_steal(scene: SceneBuilder) -> AnimationClip:
    """Attach Thief's authentic 1998 3.51s (`atcr501`, 33f) sneaky anthill-steal `"Steal"` clip."""
    ensure_ability_root(scene)
    loot_node, _ = _add_thief_loot_prop(scene)

    # Keyframes aligned to the 3.51 s `atcr501` story beats with exact loop closure at 3.51 s:
    # 0.00 rest -> 0.22..0.88 sly left/right lookout -> 1.12..1.98 crouch & pry anthill ->
    # 2.25..2.58 reach deep & scoop golden loot -> 2.86..3.18 triumphant pop-up -> 3.36..3.51 stash & rest.
    times = [
        0.00, 0.22, 0.48, 0.72, 0.92,
        1.16, 1.40, 1.66, 1.92,
        2.16, 2.40, 2.62,
        2.86, 3.04, 3.22, 3.38, 3.51,
    ]
    n_kf = len(times)
    bx, by, bz = rest_t(scene, "upper_body")

    ub_d: List[Vec3] = [
        (0.000,  0.000,  0.000),  # 0.00 rest
        (0.000, -0.022, +0.010),  # 0.22 sly crouch, glance left
        (0.000, -0.026, +0.012),  # 0.48 glance sharp right
        (0.000, -0.024, +0.012),  # 0.72 quick left check
        (0.000, -0.036, +0.018),  # 0.92 duck down toward burrow
        (0.000, -0.052, +0.028),  # 1.16 crouch & pry #1
        (0.000, -0.046, +0.024),  # 1.40 check over right shoulder
        (0.000, -0.056, +0.030),  # 1.66 crouch & pry #2
        (0.000, -0.048, +0.026),  # 1.92 check over left shoulder
        (0.000, -0.020, +0.008),  # 2.16 hatch pops open recoil!
        (0.000, -0.060, +0.036),  # 2.40 reach deep into burrow
        (0.000, -0.056, +0.034),  # 2.62 grab golden food crumb
        (0.000, +0.012, -0.006),  # 2.86 triumphant upright pop-up!
        (0.000, +0.016, -0.008),  # 3.04 hold loot high & proud
        (0.000, -0.008, +0.004),  # 3.22 tuck loot into belt pouch
        (0.000, -0.004, +0.002),  # 3.38 settling down
        (0.000,  0.000,  0.000),  # 3.51 exact rest wrap
    ]
    ub_eulers: List[Vec3] = [
        ( 0.00,  0.00,  0.00),
        (+0.18, +0.16, -0.06),
        (+0.22, -0.18, +0.06),
        (+0.20, +0.14, -0.04),
        (+0.36,  0.00,  0.00),
        (+0.56, +0.10, -0.05),
        (+0.48, -0.14, +0.06),
        (+0.62, -0.10, +0.05),
        (+0.50, +0.14, -0.06),
        (+0.18,  0.00,  0.00),
        (+0.72,  0.00,  0.00),
        (+0.66,  0.00,  0.00),
        (-0.14, +0.08, -0.04),
        (-0.16, -0.08, +0.04),
        (+0.06,  0.00,  0.00),
        (+0.02,  0.00,  0.00),
        ( 0.00,  0.00,  0.00),
    ]
    head_eulers: List[Vec3] = [
        ( 0.00,  0.00,  0.00),
        (-0.08, +0.52, -0.08),  # sharp look left
        (-0.08, -0.56, +0.08),  # sharp look right
        (-0.06, +0.44, -0.06),  # quick check left
        (+0.10,  0.00,  0.00),
        (+0.14, -0.12, +0.06),
        (-0.10, -0.46, +0.10),  # peek over shoulder
        (+0.16, +0.12, -0.06),
        (-0.10, +0.46, -0.10),  # peek other shoulder
        (-0.18,  0.00,  0.00),  # startled pop
        (+0.18,  0.00,  0.00),  # eyes locked on loot
        (+0.10,  0.00,  0.00),
        (-0.14, +0.14, -0.08),  # proud grin!
        (-0.16, -0.14, +0.08),
        (+0.04,  0.00,  0.00),
        ( 0.00,  0.00,  0.00),
        ( 0.00,  0.00,  0.00),
    ]
    gaster_rx = [0.00, -0.10, -0.12, -0.10, -0.18, -0.28, -0.24, -0.32, -0.24, +0.06, -0.36, -0.32, +0.14, +0.16, -0.04, -0.02, 0.00]

    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), [(bx + dx, by + dy, bz + dz) for dx, dy, dz in ub_d]),
        AnimationChannel("upper_body", "rotation", list(times), quat_track(rest_r(scene, "upper_body"), ub_eulers)),
        AnimationChannel("head_group", "rotation", list(times), quat_track(rest_r(scene, "head_group"), head_eulers)),
    ]
    if "gaster_group" in scene.nodes:
        channels.append(
            AnimationChannel("gaster_group", "rotation", list(times), quat_track(rest_r(scene, "gaster_group"), [(rx, 0.0, 0.0) for rx in gaster_rx]))
        )

    # Celebratory little vertical hop on root when Thief pops up with the golden loot (2.86..3.04 s).
    root_t: List[Vec3] = [(0.0, 0.0, 0.0)] * n_kf
    root_t[12] = (0.0, 0.042, 0.0)
    root_t[13] = (0.0, 0.032, 0.0)
    channels.append(AnimationChannel("root", "translation", list(times), root_t))

    # Expressive sneaking, prying, scooping, and triumphant loot-lifting arms.
    for side, tag in ((+1.0, "left"), (-1.0, "right")):
        arm_n = f"arm_{tag}"
        fore_n = f"forearm_{tag}"
        if arm_n in scene.nodes:
            a_del = [
                ( 0.00,  0.00,  0.00),
                (-0.14, +side * 0.14, +side * 0.16),
                (-0.14, -side * 0.14, +side * 0.18),
                (-0.16, +side * 0.10, +side * 0.16),
                (-0.24, +side * 0.18, +side * 0.12),
                (-0.36, +side * 0.28, -side * 0.06),  # pry #1
                (-0.24, +side * 0.18, +side * 0.14),
                (-0.42, +side * 0.30, -side * 0.08),  # pry #2
                (-0.26, +side * 0.18, +side * 0.14),
                (-0.32, -side * 0.20, +side * 0.44),  # startle wide
                (-0.48, +side * 0.32, -side * 0.12),  # scoop deep
                (-0.42, +side * 0.34, -side * 0.14),  # clasp loot
                (-0.78, +side * 0.28, +side * 0.12),  # lift loot high!
                (-0.82, +side * 0.28, +side * 0.14),  # show off loot!
                (-0.18, +side * 0.20, -side * 0.08),  # stash in pouch
                (-0.06, +side * 0.06,  0.00),
                ( 0.00,  0.00,  0.00),
            ]
            channels.append(AnimationChannel(arm_n, "rotation", list(times), quat_track(rest_r(scene, arm_n), a_del)))
        if fore_n in scene.nodes:
            f_del = [
                ( 0.00, 0.00, 0.00),
                (-0.10, -side * 0.08, 0.00),
                (-0.10, +side * 0.08, 0.00),
                (-0.10, -side * 0.06, 0.00),
                (-0.18, -side * 0.12, 0.00),
                (-0.28, -side * 0.20, 0.00),
                (-0.16, -side * 0.10, 0.00),
                (-0.32, -side * 0.22, 0.00),
                (-0.16, -side * 0.10, 0.00),
                (-0.20, +side * 0.14, 0.00),
                (-0.36, -side * 0.26, 0.00),
                (-0.34, -side * 0.28, 0.00),
                (-0.46, -side * 0.24, 0.00),
                (-0.48, -side * 0.24, 0.00),
                (-0.14, -side * 0.12, 0.00),
                (-0.04, -side * 0.04, 0.00),
                ( 0.00, 0.00, 0.00),
            ]
            channels.append(AnimationChannel(fore_n, "rotation", list(times), quat_track(rest_r(scene, fore_n), f_del)))

    # Lively bandana tail sway & antenna twitch tracking Thief's glances & triumphant pop.
    for node_n, amp in (("bandana_tails", 1.0), ("bandana_tail_lower", 1.2)):
        if node_n in scene.nodes:
            b_del = [
                (0.0, -0.35 * amp * hy, +0.25 * amp * hy)
                for _, hy, _ in head_eulers
            ]
            channels.append(AnimationChannel(node_n, "rotation", list(times), quat_track(rest_r(scene, node_n), b_del)))
    for node_n, side in (("antenna_left", +1.0), ("antenna_right", -1.0)):
        if node_n in scene.nodes and scene.get_node(node_n):
            ant_del = [
                (-0.22 * urx, 0.0, side * 0.14 * hy)
                for (urx, _, _), (_, hy, _) in zip(ub_eulers, head_eulers)
            ]
            channels.append(AnimationChannel(node_n, "rotation", list(times), quat_track(rest_r(scene, node_n), ant_del)))

    # Reveal golden food loot crumb ONLY between scoop (2.40 s) and stash (3.22 s); hidden at 1e-4 otherwise!
    loot_pos: List[Vec3] = [
        (0.0, 0.000, 0.000),  # 0.00 hidden inside thorax
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
        (0.0, -0.020, +0.185),  # 2.40 emerging at hands in burrow
        (0.0, +0.020, +0.205),  # 2.62 clasped firmly in hands
        (0.0, +0.145, +0.195),  # 2.86 hoisted triumphantly in front of chest!
        (0.0, +0.155, +0.190),  # 3.04 held high!
        (0.0, +0.015, +0.105),  # 3.22 tucked toward belt pouch
        (0.0, 0.000, 0.000),
        (0.0, 0.000, 0.000),
    ]
    loot_sc: List[Vec3] = [
        _TINY, _TINY, _TINY, _TINY, _TINY,
        _TINY, _TINY, _TINY, _TINY,
        _TINY, (0.55, 0.55, 0.55), _ONE,
        (1.08, 1.08, 1.08), (1.06, 1.06, 1.06), (0.35, 0.35, 0.35), _TINY, _TINY,
    ]
    channels.append(AnimationChannel(loot_node, "translation", list(times), loot_pos))
    channels.append(AnimationChannel(loot_node, "scale", list(times), loot_sc))

    clip = scene.add_animation("Steal", channels, replace=True)
    sync_leg_flex_to_clip(scene, "Steal", knee_bow=1.05, splay=0.25, swivel=0.0)
    return clip


def _add_bomber_ability_props(scene: SceneBuilder) -> Tuple[str, str, Vec3]:
    """Add Bomber's hidden held/ground cup-bomb & defuse-puff props (`scale=1e-4` inside thorax at rest)."""
    bx, by, bz = rest_t(scene, "upper_body")
    hide_origin: Vec3 = (bx, by, bz)

    bomb_node = "bomber_held_bomb"
    h = 0.112
    # Compact faithful cup-bomb resting with bottom at local Y = 0.0 so placing at Y = 0.0 sits flush on ground!
    b_parts: List[MeshPart] = [
        cylinder(0.50 * h, 0.54 * h, 0.18 * h, radial_segs=18, bevel=0.008 * h, axis="y", name="hb_bot", material="bomb_cup_trim").translate(0.0, 0.09 * h, 0.0),
        cylinder(0.55 * h, 0.62 * h, 0.66 * h, radial_segs=18, bevel=0.012 * h, axis="y", name="hb_wall", material="chitin_team").translate(0.0, 0.49 * h, 0.0),
        cylinder(0.62 * h, 0.64 * h, 0.16 * h, radial_segs=18, bevel=0.008 * h, axis="y", name="hb_top", material="bomb_cup_trim").translate(0.0, 0.90 * h, 0.0),
        torus_arc(0.63 * h, 0.068 * h, u_segs=20, v_segs=8, plane="xz", name="hb_rim", material="gear_dark").translate(0.0, 1.00 * h, 0.0),
        cylinder(0.53 * h, 0.53 * h, 0.86 * h, radial_segs=20, bevel=0.032 * h, axis="y", name="hb_drum", material="bomb_charge").translate(0.0, 1.18 * h, 0.0),
        cylinder(0.095 * h, 0.088 * h, 0.044 * h, radial_segs=12, bevel=0.008 * h, axis="y", name="hb_sock", material="gear_dark").translate(0.0, 1.62 * h, 0.0),
        curved_tube(
            [
                (0.0, 1.62 * h, 0.0),
                (0.08 * h, 2.02 * h, -0.06 * h),
                (0.32 * h, 2.38 * h, -0.10 * h),
                (0.58 * h, 2.54 * h, -0.04 * h),
            ],
            [0.052 * h, 0.048 * h, 0.044 * h, 0.038 * h],
            samples_per_seg=3,
            radial_segs=8,
            cap_rings=2,
            name="hb_fuse",
            material="fuse_white",
        ),
        sphere(0.096 * h, u_segs=10, v_segs=8, name="hb_spark", material="gear_gold").translate(0.58 * h, 2.54 * h, -0.04 * h),
    ]
    for p in b_parts:
        scene.add(bomb_node, p)
    scene.set_node_parent(bomb_node, "root")
    scene.set_node_transform(bomb_node, translation=hide_origin, scale=_TINY)

    # Tiny cartoon defuse fizzle-puff (only visible during DefuseBomb f6-f8, matching `abdb301`/`abdb901`!).
    puff_node = "bomber_defuse_puff"
    p_parts: List[MeshPart] = [
        sphere(0.052, u_segs=12, v_segs=8, name="puff_core", material="gear_gold"),
        sphere(0.040, u_segs=10, v_segs=8, name="puff_a", material="fuse_white").translate(+0.052, +0.024, +0.012),
        sphere(0.038, u_segs=10, v_segs=8, name="puff_b", material="fuse_white").translate(-0.048, +0.028, +0.016),
        sphere(0.036, u_segs=10, v_segs=8, name="puff_c", material="fuse_white").translate(+0.014, +0.052, -0.024),
        sphere(0.034, u_segs=10, v_segs=8, name="puff_d", material="gear_gold").translate(-0.018, -0.024, +0.042),
    ]
    for p in p_parts:
        scene.add(puff_node, p)
    scene.set_node_parent(puff_node, "root")
    scene.set_node_transform(puff_node, translation=hide_origin, scale=_TINY)
    return bomb_node, puff_node, hide_origin


def _add_bomber_plant_bomb(scene: SceneBuilder, bomb_node: str, hide_origin: Vec3) -> AnimationClip:
    """Attach Bomber's authentic 1998 1.36s (`absb`, 17f) bomb-planting `"PlantBomb"` clip."""
    # Exact 1998 absb 17-frame total duration = 1.36 s with key story beats:
    # f0-f2 (0.00-0.18s) reach back to backpack -> f3-f8 (0.24-0.84s) hold cup-bomb & arm/light fuse ->
    # f9-f13 (0.90-1.14s) crouch & plant solidly on ground -> f14-f16 (1.20-1.36s) hop upright & wrap.
    times = [0.00, 0.12, 0.24, 0.44, 0.64, 0.84, 0.96, 1.08, 1.18, 1.28, 1.34, 1.36]
    bx, by, bz = rest_t(scene, "upper_body")

    ub_d: List[Vec3] = [
        (0.0,  0.000,  0.000),  # 0.00 rest
        (0.0, +0.008, -0.006),  # 0.12 reach back toward pack
        (0.0, -0.006, +0.008),  # 0.24 pull bomb forward
        (0.0, -0.010, +0.012),  # 0.44 hold & inspect left
        (0.0, -0.012, +0.012),  # 0.64 arm / flick spark fuse
        (0.0, -0.018, +0.016),  # 0.84 tilt to lower bomb
        (0.0, -0.050, +0.036),  # 0.96 crouch & set bomb on ground!
        (0.0, -0.056, +0.040),  # 1.08 press bomb firmly upright on Y=0
        (0.0, -0.016, +0.010),  # 1.18 push back off ground
        (0.0, +0.014, -0.008),  # 1.28 celebratory hop back!
        (0.0, +0.004, -0.002),  # 1.34 settle
        (0.0,  0.000,  0.000),  # 1.36 exact rest wrap
    ]
    ub_eulers: List[Vec3] = [
        ( 0.00,  0.00,  0.00),
        (-0.14, -0.22, +0.08),
        (+0.10, +0.08, -0.04),
        (+0.16, +0.10, -0.06),
        (+0.18, -0.10, +0.06),
        (+0.32,  0.00,  0.00),
        (+0.68,  0.00,  0.00),
        (+0.74,  0.00,  0.00),
        (+0.22,  0.00,  0.00),
        (-0.12,  0.00,  0.00),
        (-0.04,  0.00,  0.00),
        ( 0.00,  0.00,  0.00),
    ]
    head_eulers: List[Vec3] = [
        ( 0.00,  0.00,  0.00),
        (-0.08, -0.24, +0.08),
        (+0.16,  0.00,  0.00),
        (+0.20, +0.18, -0.12),
        (+0.22, -0.18, +0.12),
        (+0.26,  0.00,  0.00),
        (+0.22,  0.00,  0.00),
        (+0.20,  0.00,  0.00),
        (+0.06,  0.00,  0.00),
        (-0.10,  0.00,  0.00),
        (-0.04,  0.00,  0.00),
        ( 0.00,  0.00,  0.00),
    ]
    gaster_rx = [0.00, +0.10, -0.06, -0.08, -0.10, -0.16, -0.34, -0.38, -0.12, +0.10, +0.04, 0.00]

    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), [(bx + dx, by + dy, bz + dz) for dx, dy, dz in ub_d]),
        AnimationChannel("upper_body", "rotation", list(times), quat_track(rest_r(scene, "upper_body"), ub_eulers)),
        AnimationChannel("head_group", "rotation", list(times), quat_track(rest_r(scene, "head_group"), head_eulers)),
    ]
    if "gaster_group" in scene.nodes:
        channels.append(
            AnimationChannel("gaster_group", "rotation", list(times), quat_track(rest_r(scene, "gaster_group"), [(rx, 0.0, 0.0) for rx in gaster_rx]))
        )

    # Tiny spry hop back onto ready stance at f15 (1.28 s), grounded Y=0 everywhere else!
    root_t: List[Vec3] = [(0.0, 0.0, 0.0)] * len(times)
    root_t[9] = (0.0, 0.036, -0.024)
    root_t[10] = (0.0, 0.006, -0.006)
    channels.append(AnimationChannel("root", "translation", list(times), root_t))

    # Right arm reaches back over shoulder to grab bomb, both arms cradle & plant it on Y=0, then hop clear!
    arm_left_d = [
        ( 0.00,  0.00,  0.00),
        (-0.18, -0.14, +0.26),  # balance while right reaches back
        (-0.36, +0.34, +0.10),  # catch bomb in front
        (-0.42, +0.38, +0.08),  # cradle & inspect
        (-0.38, +0.36, +0.10),  # steady while lighting fuse
        (-0.34, +0.34, +0.06),  # lowering bomb
        (-0.24, +0.30, +0.04),  # setting on ground
        (-0.20, +0.28, +0.04),  # patting upright
        (-0.30, -0.20, +0.38),  # pull hands clear
        (-0.36, -0.26, +0.52),  # spry hop wide arms
        (-0.12, -0.08, +0.18),
        ( 0.00,  0.00,  0.00),
    ]
    arm_right_d = [
        ( 0.00,  0.00,  0.00),
        (-1.15, +0.42, -0.45),  # reach back over shoulder toward 3-bomb pack!
        (-0.38, -0.34, -0.10),  # bring bomb forward
        (-0.42, -0.38, -0.08),  # hold & inspect
        (-0.56, -0.26, -0.16),  # flick/light spark fuse!
        (-0.34, -0.34, -0.06),  # lowering bomb
        (-0.24, -0.30, -0.04),  # setting on ground
        (-0.20, -0.28, -0.04),  # patting upright
        (-0.30, +0.20, -0.38),  # pull hands clear
        (-0.36, +0.26, -0.52),  # spry hop wide arms
        (-0.12, +0.08, -0.18),
        ( 0.00,  0.00,  0.00),
    ]
    if "arm_left" in scene.nodes:
        channels.append(AnimationChannel("arm_left", "rotation", list(times), quat_track(rest_r(scene, "arm_left"), arm_left_d)))
    if "arm_right" in scene.nodes:
        channels.append(AnimationChannel("arm_right", "rotation", list(times), quat_track(rest_r(scene, "arm_right"), arm_right_d)))
    for node_n, side in (("antenna_left", +1.0), ("antenna_right", -1.0)):
        if node_n in scene.nodes and scene.get_node(node_n):
            ant_del = [(-0.25 * urx, 0.0, side * 0.10 * hrz) for (urx, _, _), (_, _, hrz) in zip(ub_eulers, head_eulers)]
            channels.append(AnimationChannel(node_n, "rotation", list(times), quat_track(rest_r(scene, node_n), ant_del)))

    # Held cup-bomb path: emerges from shoulder -> held cleanly in front of hands -> planted on Y=0.0 -> sits armed -> wraps.
    bomb_pos: List[Vec3] = [
        hide_origin,           # 0.00 hidden
        (-0.14, 0.56, -0.10),  # 0.12 grabbed from pack
        ( 0.00, 0.34, +0.28),  # 0.24 pulled forward between hands
        ( 0.00, 0.33, +0.30),  # 0.44 held & inspected
        ( 0.00, 0.32, +0.30),  # 0.64 fuse lit!
        ( 0.00, 0.22, +0.32),  # 0.84 lowering to ground
        ( 0.00, 0.01, +0.34),  # 0.96 touching down
        ( 0.00, 0.00, +0.34),  # 1.08 planted solidly on Y=0.0!
        ( 0.00, 0.00, +0.34),  # 1.18 sitting armed while Bomber rises!
        ( 0.00, 0.00, +0.36),  # 1.28 sitting armed on ground as Bomber hops back!
        ( 0.00, 0.00, +0.34),  # 1.34 quick fade-out before wrap
        hide_origin,           # 1.36 hidden wrap
    ]
    bomb_rot = quat_track(
        (0.0, 0.0, 0.0, 1.0),
        [
            (0.0, 0.0, 0.0),
            (-0.35, 0.0, +0.30),
            (+0.18, 0.0, +0.06),
            (+0.14, 0.0, -0.08),
            (+0.16, 0.0, +0.06),
            (+0.10, 0.0, 0.0),
            (0.00, 0.0, 0.0),
            (0.00, 0.0, 0.0),
            (0.00, 0.0, 0.0),
            (0.00, 0.0, 0.0),
            (0.00, 0.0, 0.0),
            (0.00, 0.0, 0.0),
        ],
    )
    bomb_sc: List[Vec3] = [
        _TINY, (0.75, 0.75, 0.75), _ONE, _ONE, _ONE,
        _ONE, _ONE, _ONE, _ONE, _ONE, _TINY, _TINY,
    ]
    channels.append(AnimationChannel(bomb_node, "translation", list(times), bomb_pos))
    channels.append(AnimationChannel(bomb_node, "rotation", list(times), bomb_rot))
    channels.append(AnimationChannel(bomb_node, "scale", list(times), bomb_sc))

    clip = scene.add_animation("PlantBomb", channels, replace=True)
    sync_leg_flex_to_clip(scene, "PlantBomb", knee_bow=1.05, splay=0.25, swivel=0.0)
    return clip


def _add_bomber_defuse_bomb(scene: SceneBuilder, bomb_node: str, puff_node: str, hide_origin: Vec3) -> AnimationClip:
    """Attach Bomber's authentic 1998 1.10s (`abdb`, 12f) bomb-defusal `"DefuseBomb"` clip."""
    # Exact 1998 abdb 12-frame total duration = 1.10 s:
    # f0-f2 (0.00-0.22s) cautious lean & goggle peer -> f3-f5 (0.32-0.62s) crouch & pinch/snuff fuse
    # while squinting -> f6-f8 (0.70-0.90s) safe puff fizzle & startled recoil -> f9-f11 (0.98-1.10s) brow wipe & rest!
    times = [0.00, 0.06, 0.16, 0.28, 0.44, 0.58, 0.68, 0.78, 0.88, 0.98, 1.04, 1.10]
    bx, by, bz = rest_t(scene, "upper_body")

    ub_d: List[Vec3] = [
        (0.0,  0.000,  0.000),  # 0.00 rest
        (0.0, -0.012, +0.010),  # 0.06 cautious lean forward
        (0.0, -0.022, +0.016),  # 0.16 tilt & peer at fuse
        (0.0, -0.044, +0.026),  # 0.28 crouch over ground bomb
        (0.0, -0.048, +0.022),  # 0.44 pinching fuse cautiously (leaning back slightly!)
        (0.0, -0.046, +0.020),  # 0.58 snuffing spark!
        (0.0, -0.018, -0.014),  # 0.68 POOF! startled recoil back!
        (0.0, -0.012, -0.018),  # 0.78 holding breath as smoke clears
        (0.0, -0.006, -0.008),  # 0.88 phew! safe!
        (0.0, +0.004,  0.000),  # 0.98 relieved brow wipe
        (0.0, +0.002,  0.000),  # 1.04 settle
        (0.0,  0.000,  0.000),  # 1.10 exact rest wrap
    ]
    ub_eulers: List[Vec3] = [
        ( 0.00,  0.00,  0.00),
        (+0.22,  0.00, +0.12),
        (+0.34,  0.00, -0.14),
        (+0.58,  0.00,  0.00),
        (+0.50,  0.00, +0.05),
        (+0.46,  0.00, -0.04),
        (-0.16,  0.00,  0.00),
        (-0.12,  0.00, +0.06),
        (+0.04,  0.00, -0.04),
        (-0.04, +0.08, +0.06),
        (-0.02,  0.00,  0.00),
        ( 0.00,  0.00,  0.00),
    ]
    head_eulers: List[Vec3] = [
        ( 0.00,  0.00,  0.00),
        (+0.18, +0.10, +0.18),
        (+0.24, -0.10, -0.18),
        (+0.22,  0.00,  0.00),
        (+0.12, -0.12, +0.10),  # leaning head slightly back while pinching!
        (+0.08, +0.10, -0.08),
        (-0.22,  0.00,  0.00),  # jolt back on puff!
        (-0.14,  0.00,  0.08),
        (+0.06, +0.14, -0.08),  # relieved sigh shake
        (-0.06, -0.14, +0.08),  # brow wipe
        (-0.02,  0.00,  0.00),
        ( 0.00,  0.00,  0.00),
    ]
    gaster_rx = [0.00, -0.10, -0.16, -0.28, -0.24, -0.22, +0.14, +0.10, -0.02, +0.04, 0.00, 0.00]

    channels: List[AnimationChannel] = [
        AnimationChannel("upper_body", "translation", list(times), [(bx + dx, by + dy, bz + dz) for dx, dy, dz in ub_d]),
        AnimationChannel("upper_body", "rotation", list(times), quat_track(rest_r(scene, "upper_body"), ub_eulers)),
        AnimationChannel("head_group", "rotation", list(times), quat_track(rest_r(scene, "head_group"), head_eulers)),
    ]
    if "gaster_group" in scene.nodes:
        channels.append(
            AnimationChannel("gaster_group", "rotation", list(times), quat_track(rest_r(scene, "gaster_group"), [(rx, 0.0, 0.0) for rx in gaster_rx]))
        )

    # Cautious two-handed fuse-pinching arms -> recoil wide on puff -> relieved right-hand brow wipe!
    arm_left_d = [
        ( 0.00,  0.00,  0.00),
        (-0.16, +0.14, +0.14),
        (-0.22, +0.20, +0.12),
        (-0.28, +0.34, -0.06),  # hands converging on bomb fuse
        (-0.30, +0.38, -0.08),  # pinching fuse!
        (-0.32, +0.40, -0.10),  # snuffing spark!
        (-0.44, -0.34, +0.62),  # startled fling wide!
        (-0.32, -0.24, +0.44),
        (-0.14, -0.08, +0.18),
        (-0.08,  0.00, +0.10),
        (-0.03,  0.00, +0.04),
        ( 0.00,  0.00,  0.00),
    ]
    arm_right_d = [
        ( 0.00,  0.00,  0.00),
        (-0.16, -0.14, -0.14),
        (-0.22, -0.20, -0.12),
        (-0.28, -0.34, +0.06),
        (-0.30, -0.38, +0.08),
        (-0.32, -0.40, +0.10),
        (-0.44, +0.34, -0.62),
        (-0.32, +0.24, -0.44),
        (-0.68, -0.26, -0.24),  # raise hand to wipe forehead!
        (-0.74, -0.12, -0.36),  # brow-wipe sweep!
        (-0.22, -0.06, -0.10),
        ( 0.00,  0.00,  0.00),
    ]
    if "arm_left" in scene.nodes:
        channels.append(AnimationChannel("arm_left", "rotation", list(times), quat_track(rest_r(scene, "arm_left"), arm_left_d)))
    if "arm_right" in scene.nodes:
        channels.append(AnimationChannel("arm_right", "rotation", list(times), quat_track(rest_r(scene, "arm_right"), arm_right_d)))

    # Nervous squinting eyelids during fuse pinch & puff (f3-f7), reopening cleanly!
    for lid_n in ("eyelid_left", "eyelid_right"):
        if lid_n in scene.nodes:
            r0_lid = rest_r(scene, lid_n)
            squints = [0.0, 0.0, 0.18, 0.55, 0.78, 0.86, 0.98, 0.65, 0.15, 0.0, 0.0, 0.0]
            q_lid = [quat_mul(r0_lid, quat_from_axis_angle((1.0, 0.0, 0.0), sq)) for sq in squints]
            q_lid[-1] = q_lid[0]
            channels.append(AnimationChannel(lid_n, "rotation", list(times), q_lid))

    for node_n, side in (("antenna_left", +1.0), ("antenna_right", -1.0)):
        if node_n in scene.nodes and scene.get_node(node_n):
            ant_del = [(-0.20 * urx, 0.0, side * 0.12 * hrz) for (urx, _, _), (_, _, hrz) in zip(ub_eulers, head_eulers)]
            channels.append(AnimationChannel(node_n, "rotation", list(times), quat_track(rest_r(scene, node_n), ant_del)))

    # Armed ground bomb sits at (0, 0, +0.32) for f0-f5, fizzles away cleanly at f6 as the defuse puff pops!
    b_gnd: Vec3 = (0.0, 0.0, +0.32)
    bomb_pos = [hide_origin, b_gnd, b_gnd, b_gnd, b_gnd, b_gnd, hide_origin, hide_origin, hide_origin, hide_origin, hide_origin, hide_origin]
    bomb_sc = [_TINY, _ONE, _ONE, _ONE, _ONE, (0.94, 0.94, 0.94), _TINY, _TINY, _TINY, _TINY, _TINY, _TINY]
    channels.append(AnimationChannel(bomb_node, "translation", list(times), bomb_pos))
    channels.append(AnimationChannel(bomb_node, "scale", list(times), bomb_sc))

    p_loc: Vec3 = (0.0, 0.22, +0.32)
    puff_pos = [hide_origin, hide_origin, hide_origin, hide_origin, hide_origin, hide_origin, p_loc, (0.0, 0.27, +0.33), (0.0, 0.31, +0.34), hide_origin, hide_origin, hide_origin]
    puff_sc = [_TINY, _TINY, _TINY, _TINY, _TINY, _TINY, (1.15, 1.15, 1.15), (1.42, 1.32, 1.42), (0.55, 0.55, 0.55), _TINY, _TINY, _TINY]
    channels.append(AnimationChannel(puff_node, "translation", list(times), puff_pos))
    channels.append(AnimationChannel(puff_node, "scale", list(times), puff_sc))

    clip = scene.add_animation("DefuseBomb", channels, replace=True)
    sync_leg_flex_to_clip(scene, "DefuseBomb", knee_bow=1.05, splay=0.25, swivel=0.0)
    return clip


def add_thief_abilities(scene: SceneBuilder) -> AnimationClip:
    """Attach Thief's class-specific 1998 ability clip (`"Steal"`)."""
    return _add_thief_steal(scene)


def add_bomber_abilities(scene: SceneBuilder) -> Tuple[AnimationClip, AnimationClip]:
    """Attach Bomber's class-specific 1998 ability clips (`"PlantBomb"`, `"DefuseBomb"`)."""
    ensure_ability_root(scene)
    bomb_node, puff_node, hide_origin = _add_bomber_ability_props(scene)
    c_plant = _add_bomber_plant_bomb(scene, bomb_node, hide_origin)
    c_defuse = _add_bomber_defuse_bomb(scene, bomb_node, puff_node, hide_origin)
    return c_plant, c_defuse


__all__ = [
    "add_bomber_abilities",
    "add_thief_abilities",
    "ensure_ability_root",
    "quat_track",
    "rest_r",
    "rest_t",
]
