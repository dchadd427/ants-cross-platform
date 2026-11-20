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

# Slightly poised flowing idle arms anchored cleanly at base_ant's shoulder (0.092, 0.476, 0.014).
_WORKER_ARM_LEFT = [
    (0.092, 0.476, 0.014),
    (0.194, 0.436, 0.064),
    (0.172, 0.388, 0.156),
    (0.144, 0.382, 0.190),
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


def _add_carapace_details(scene: SceneBuilder) -> None:
    """Subtle smooth organic chitin accents fitted flush onto the rounded base thorax and limbs."""
    # Smooth curved breastplate crest hugging the ovoid chest carapace (no boxy corners).
    crest = (
        superellipsoid(0.056, 0.062, 0.024, e1=0.80, e2=0.84, u_segs=16, v_segs=12, name="chest_crest", material="chitin_team")
        .rotate_xyz(rx=0.10)
        .translate(0.0, 0.408, 0.096)
    )
    # Sleek dorsal pronotum ridge sitting flush atop the rounded rear dorsal carapace.
    ridge = (
        superellipsoid(0.048, 0.032, 0.046, e1=0.80, e2=0.84, u_segs=16, v_segs=10, name="dorsal_ridge", material="chitin_team")
        .rotate_xyz(rx=-0.16)
        .translate(0.0, 0.448, -0.088)
    )
    scene.add("body", [crest, ridge])

    # Subtle curved team-colored shoulder fairings blending smoothly into the upper arm roots.
    cap = (
        superellipsoid(0.032, 0.024, 0.030, e1=0.80, e2=0.84, u_segs=14, v_segs=10, name="shoulder_cap", material="chitin_team")
        .rotate_xyz(rz=-0.34)
        .translate(0.094, 0.486, 0.014)
    )
    scene.add_mirrored("body", cap)

    # Slim wrist cuffs across each flowing forearm just behind the claw hand.
    wrist, hand = np.asarray(_WORKER_ARM_LEFT[2]), np.asarray(_WORKER_ARM_LEFT[3])
    scene.add_mirrored("arms", _ring_across(wrist + 0.48 * (hand - wrist), hand - wrist, 0.0165, 0.0048, "wrist_cuff"))


def build_worker(blink: float = 0.0) -> SceneBuilder:
    """Build the classic unadorned Worker Ant (`blink` in [0, 1] sets resting eyelid closure)."""
    scene, _ = build_base_ant(
        scene_name="ant_worker",
        include_brows=False,
        left_arm_pose=_WORKER_ARM_LEFT,
        right_arm_pose=_WORKER_ARM_RIGHT,
        blink=blink,
    )
    _add_carapace_details(scene)
    scene.ground_to_y(0.0)
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical module entrypoint for `build_all.py`."""
    return build_worker(blink=blink)


__all__ = ["build_ant", "build_worker"]
