"""Sculpted 3D model for the classic Worker Ant.

Built directly on `base_ant` so it keeps the iconic unadorned slim upright
Worker silhouette (no headwear or tools), with subtle smooth sculpted chitin
finishing touches fitted flush to the base anatomy: a curved chest carapace
crest, a dorsal pronotum ridge, sleek shoulder fairings, and slim wrist cuffs.
The base supplies the broad-crowned heart-shield head, huge 3-part alert eyes,
plump lobster-claw mandibles, flowing ball-free insect legs, clubbed antennae,
and the pivoted `eyelid_left` / `eyelid_right` nodes with the glTF 2.0 "Blink"
clip.

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet grounded cleanly at Y = 0.0)
  +Z = front (South / canonical camera 301)
  +X = ant's left (viewer's right from front)
"""
from __future__ import annotations

from typing import Sequence

import numpy as np

try:
    from .base_ant import build_base_ant
    from .mesh_core import SceneBuilder, superellipsoid, torus_arc
except ImportError:
    from base_ant import build_base_ant  # type: ignore
    from mesh_core import SceneBuilder, superellipsoid, torus_arc  # type: ignore

# Slightly poised flowing idle arms anchored cleanly at base_ant's shoulder (0.106, 0.478, 0.016).
_WORKER_ARM_LEFT = [
    (0.106, 0.478, 0.016),
    (0.204, 0.436, 0.068),
    (0.178, 0.388, 0.158),
    (0.146, 0.382, 0.190),
]
_WORKER_ARM_RIGHT = [(-x, y, z) for x, y, z in _WORKER_ARM_LEFT]


def _ring_across(center: Sequence[float], axis: Sequence[float], major_r: float, minor_r: float, name: str):
    """Torus ring centered at `center` whose hole faces along `axis`."""
    a = np.asarray(axis, dtype=np.float64)
    a /= float(np.linalg.norm(a))
    ref = np.array([0.0, 0.0, 1.0]) if abs(a[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
    x = np.cross(a, ref)
    x /= float(np.linalg.norm(x))
    z = np.cross(x, a)
    mat = np.eye(4)
    mat[:3, 0], mat[:3, 1], mat[:3, 2], mat[:3, 3] = x, a, z, np.asarray(center, dtype=np.float64)
    return torus_arc(major_r, minor_r, u_segs=16, v_segs=8, plane="xz", name=name, material="chitin_dark").transform(mat)


def _add_carapace_details(scene: SceneBuilder, anchors: dict) -> None:
    """Subtle smooth organic chitin accents fitted flush onto the petite slim base thorax and limbs."""
    # Petite curved breastplate crest hugging the slim chest carapace (`z_max ~ 0.096`).
    crest = (
        superellipsoid(0.042, 0.048, 0.018, e1=0.80, e2=0.84, u_segs=16, v_segs=12, name="chest_crest", material="chitin_team")
        .rotate_xyz(rx=0.10)
        .translate(0.0, 0.402, 0.080)
    )
    # Sleek dorsal pronotum ridge sitting flush atop the slim rear dorsal carapace (`z_min ~ -0.096`).
    ridge = (
        superellipsoid(0.036, 0.024, 0.034, e1=0.80, e2=0.84, u_segs=16, v_segs=10, name="dorsal_ridge", material="chitin_team")
        .rotate_xyz(rx=-0.16)
        .translate(0.0, 0.432, -0.072)
    )
    scene.add("worker_carapace", [crest, ridge])

    # Subtle curved team-colored shoulder fairings anchored dynamically onto the narrow shoulders.
    sh_l = anchors["shoulder_left"]
    cap = (
        superellipsoid(0.024, 0.018, 0.022, e1=0.80, e2=0.84, u_segs=14, v_segs=10, name="shoulder_cap", material="chitin_team")
        .rotate_xyz(rz=-0.34)
        .translate(sh_l[0], sh_l[1] + 0.008, sh_l[2])
    )
    scene.add_mirrored("worker_carapace", cap)

    # Slim wrist cuffs anchored cleanly to each staggered contrapposto forearm right behind the claw hand.
    for side in ("left", "right"):
        hand = np.asarray(anchors[f"{side}_hand"], dtype=np.float64)
        hdir = np.asarray(anchors[f"{side}_hand_dir"], dtype=np.float64)
        scene.add("worker_cuffs", _ring_across(hand - 0.022 * hdir, hdir, 0.0145, 0.0044, f"wrist_cuff_{side}"))

    # Parent canonical world-authored accents under upper_body in lockstep with stance contrapposto & Idle.
    scene.attach_world_node("worker_carapace", "upper_body", preserve_world_pose=False)
    scene.attach_world_node("worker_cuffs", "upper_body", preserve_world_pose=False)


def build_worker(blink: float = 0.0) -> SceneBuilder:
    """Build the curious, charming classic Worker Ant (`blink` in [0, 1] sets resting eyelid closure)."""
    scene, anchors = build_base_ant(
        scene_name="ant_worker",
        include_brows=False,
        brow_style="curious",
        head_pitch_deg=-18.0,
        head_tilt_deg=4.5,
        head_yaw_deg=-3.5,
        eye_look=(0.06, 0.05),
        eye_scale_left=0.94,
        eye_scale_right=1.05,
        eye_aspect_left=0.92,
        eye_aspect_right=1.18,
        antenna_asymmetry=0.35,
        stance_asymmetry=0.35,
        left_arm_pose="idle",
        right_arm_pose="idle",
        blink=blink,
    )
    _add_carapace_details(scene, anchors)
    scene.ground_to_y(0.0)
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical module entrypoint for `build_all.py`."""
    return build_worker(blink=blink)


__all__ = ["build_ant", "build_worker"]
