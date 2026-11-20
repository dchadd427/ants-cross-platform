"""Sculpted 3D model for the classic Worker Ant.

Built directly on `base_ant` so it keeps the iconic unadorned slim upright
Worker silhouette (no headwear or tools), with a few subtle sculpted chitin
finishing touches fitted to the base anatomy: a faceted chest carapace crest, a
dorsal pronotum ridge, shoulder caps, and wrist cuffs. The base supplies the 3-part eyes, toothed mandibles,
clubbed antennae, and the pivoted `eyelid_left` / `eyelid_right` nodes with the
glTF 2.0 "Blink" clip.

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

# Slightly poised idle arms (shoulder, elbow, wrist, hand) that keep the chest crest clear.
_WORKER_ARM_LEFT = [
    (0.106, 0.440, 0.020),
    (0.218, 0.400, 0.078),
    (0.196, 0.354, 0.184),
    (0.164, 0.348, 0.222),
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
    """Subtle sculpted chitin plates fitted flush onto the base thorax and limbs."""
    # Faceted breastplate crest over the chest plate, and a dorsal pronotum ridge on the back.
    crest = superellipsoid(0.056, 0.058, 0.022, e1=0.40, e2=0.46, u_segs=14, v_segs=10, name="chest_crest", material="chitin_team").rotate_xyz(rx=0.18).translate(0.0, 0.380, 0.104)
    ridge = superellipsoid(0.050, 0.028, 0.056, e1=0.40, e2=0.46, u_segs=14, v_segs=10, name="dorsal_ridge", material="chitin_team").rotate_xyz(rx=-0.22).translate(0.0, 0.452, -0.074)
    scene.add("body", [crest, ridge])

    # Team-colored shoulder caps over each arm socket.
    cap = superellipsoid(0.036, 0.028, 0.034, e1=0.60, e2=0.70, u_segs=14, v_segs=10, name="shoulder_cap", material="chitin_team").rotate_xyz(rz=-0.32).translate(0.106, 0.458, 0.020)
    scene.add_mirrored("body", cap)

    # Slim wrist cuffs across each forearm just behind the claw hand.
    wrist, hand = np.asarray(_WORKER_ARM_LEFT[2]), np.asarray(_WORKER_ARM_LEFT[3])
    scene.add_mirrored("arms", _ring_across(wrist + 0.45 * (hand - wrist), hand - wrist, 0.019, 0.0055, "wrist_cuff"))


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
