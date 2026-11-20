"""Stylized tabletop/vinyl 3D model for the classic Worker Ant.

Built directly on `base_ant` so it preserves the iconic unadorned Worker
silhouette (no specialist headwear or weapons), elevated with subtle designer
vinyl finishing touches: glossy sculpted antenna club tips, inner mandible
pincer nibs, a beveled thoracic collar and chest carapace trim, and clean
ankle/wrist joint cuffs.

Coordinate system (glTF 2.0 right-handed):
  +Y = up (feet grounded cleanly at Y = 0.0)
  +Z = front (South / canonical camera 301)
  +X = ant's left (viewer's right from front)
"""
from __future__ import annotations

from typing import Tuple

try:
    from .base_ant import build_base_ant
    from .mesh_core import (
        SceneBuilder,
        curved_tube,
        ellipsoid,
        superellipsoid,
        torus_arc,
    )
except ImportError:
    from base_ant import build_base_ant  # type: ignore
    from mesh_core import (  # type: ignore
        SceneBuilder,
        curved_tube,
        ellipsoid,
        superellipsoid,
        torus_arc,
    )

# Slightly poised, expressive idle arm stance that keeps the chest carapace clear.
_WORKER_ARM_LEFT = [
    (0.145, 0.432, 0.028),
    (0.270, 0.394, 0.088),
    (0.244, 0.352, 0.198),
    (0.210, 0.346, 0.246),
]

_WORKER_ARM_RIGHT = [(-x, y, z) for x, y, z in _WORKER_ARM_LEFT]


def _add_antenna_finishing(scene: SceneBuilder, head_center: Tuple[float, float, float]) -> None:
    """Add glossy clubbed vinyl tips and mid-bend beads matching original Worker sprites."""
    _, hy, hz = head_center

    # Smooth glossy club tip seated cleanly at the end of each antenna without raising max Y.
    tip_club = (
        superellipsoid(0.036, 0.034, 0.048, e1=0.86, e2=0.88, u_segs=16, v_segs=12, name="antenna_club", material="chitin_dark")
        .rotate_xyz(rx=0.38, ry=-0.10)
        .translate(0.177, hy + 0.342, hz + 0.214)
    )
    scene.add_mirrored("antennae", tip_club)

    # Subtle molded joint collar right at the mid-antenna elbow bend.
    mid_ring = (
        torus_arc(0.026, 0.0065, u_segs=16, v_segs=8, plane="xz", name="antenna_joint", material="chitin_dark")
        .rotate_xyz(rx=0.35, rz=-0.35)
        .translate(0.155, hy + 0.278, hz + 0.068)
    )
    scene.add_mirrored("antennae", mid_ring)


def _add_mandible_details(scene: SceneBuilder, head_center: Tuple[float, float, float]) -> None:
    """Add cute articulated inner pincer nibs tucked neatly beneath the plump mandibles."""
    _, hy, hz = head_center

    pincer_pts = [
        (0.070, hy - 0.178, hz + 0.205),
        (0.056, hy - 0.194, hz + 0.236),
        (0.024, hy - 0.196, hz + 0.248),
    ]
    pincer_left = curved_tube(
        pincer_pts,
        [0.020, 0.017, 0.012],
        samples_per_seg=4,
        radial_segs=10,
        cap_rings=3,
        name="inner_pincer",
        material="chitin_dark",
    )
    scene.add_mirrored("head", pincer_left)

    # Tiny glossy cheek-chin dimple rim linking the two plump mandibles.
    chin_trim = (
        ellipsoid(0.048, 0.024, 0.034, u_segs=14, v_segs=10, name="chin_trim", material="chitin_team")
        .rotate_xyz(rx=0.25)
        .translate(0.0, hy - 0.192, hz + 0.168)
    )
    scene.add("head", chin_trim)


def _add_thorax_and_gaster_craft(scene: SceneBuilder) -> None:
    """Add subtle collector-vinyl collar bevel, chest carapace trim, and joint cuffs."""
    # Molded glossy collar rim hugging the upper thorax around the neck base.
    collar = (
        torus_arc(0.104, 0.020, u_segs=26, v_segs=10, plane="xz", name="thorax_collar", material="chitin_team")
        .scale(1.06, 0.85, 1.00)
        .rotate_xyz(rx=0.18)
        .translate(0.0, 0.448, 0.010)
    )
    scene.add("body", collar)

    # Flush sculpted breastplate shield contour over the front chest plate.
    chest_crest = (
        superellipsoid(0.088, 0.090, 0.042, e1=0.72, e2=0.78, u_segs=18, v_segs=12, name="chest_crest", material="chitin_team")
        .rotate_xyz(rx=0.16)
        .translate(0.0, 0.342, 0.118)
    )
    scene.add("body", chest_crest)

    # Sleek dorsal pronotum ridge sitting neatly atop the rear thorax saddle.
    dorsal_ridge = (
        superellipsoid(0.072, 0.044, 0.076, e1=0.78, e2=0.82, u_segs=16, v_segs=12, name="dorsal_ridge", material="chitin_team")
        .rotate_xyz(rx=-0.22)
        .translate(0.0, 0.415, -0.084)
    )
    scene.add("body", dorsal_ridge)

    # Subtle glossy team-colored shoulder cap fairing at each upper arm socket.
    shoulder_cap = (
        superellipsoid(0.046, 0.040, 0.044, e1=0.82, e2=0.84, u_segs=14, v_segs=10, name="shoulder_cap", material="chitin_team")
        .rotate_xyz(rz=-0.28)
        .translate(0.136, 0.442, 0.026)
    )
    scene.add_mirrored("body", shoulder_cap)

    # Subtle smooth wrist cuff beads aligned to each forearm right behind the mitten hand.
    wrist_cuff = (
        torus_arc(0.031, 0.0065, u_segs=14, v_segs=8, plane="xy", name="wrist_cuff", material="chitin_dark")
        .rotate_xyz(rx=0.12, ry=-0.62)
        .translate(0.232, 0.349, 0.214)
    )
    scene.add_mirrored("arms", wrist_cuff)

    # Subtle ankle cuff rings seating each leg cleanly into its tabletop foot pad.
    mid_ankle = (
        torus_arc(0.030, 0.007, u_segs=14, v_segs=8, plane="xz", name="ankle_mid", material="chitin_dark")
        .translate(0.236, 0.052, 0.155)
    )
    scene.add_mirrored("legs", mid_ankle)

    rear_ankle = (
        torus_arc(0.030, 0.007, u_segs=14, v_segs=8, plane="xz", name="ankle_rear", material="chitin_dark")
        .translate(0.232, 0.052, -0.200)
    )
    scene.add_mirrored("legs", rear_ankle)

    # Subtle petiole waist ring for clean collector-toy segmentation.
    waist_ring = (
        torus_arc(0.064, 0.008, u_segs=18, v_segs=8, plane="xy", name="waist_ring", material="chitin_dark")
        .rotate_xyz(rx=-0.12)
        .translate(0.0, 0.242, -0.142)
    )
    scene.add("body", waist_ring)


def build_worker() -> SceneBuilder:
    """Build the classic unadorned stylized vinyl Worker Ant."""
    scene, anchors = build_base_ant(
        scene_name="ant_worker",
        include_antennae=True,
        include_eyes=True,
        include_brows=True,
        left_arm_pose=_WORKER_ARM_LEFT,
        right_arm_pose=_WORKER_ARM_RIGHT,
        head_tilt_x=0.0,
        scale=1.0,
    )
    head_center = anchors["head_center"]
    _add_antenna_finishing(scene, head_center)
    _add_mandible_details(scene, head_center)
    _add_thorax_and_gaster_craft(scene)
    scene.ground_to_y(0.0)
    return scene


def build_ant() -> SceneBuilder:
    """Canonical module entrypoint for `build_all.py`."""
    return build_worker()


__all__ = ["build_ant", "build_worker"]
