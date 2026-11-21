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

import numpy as np

try:
    from .base_ant import build_base_ant
    from .mesh_core import SceneBuilder, superellipsoid
except ImportError:
    from base_ant import build_base_ant  # type: ignore
    from mesh_core import SceneBuilder, superellipsoid  # type: ignore


def _add_carapace_details(scene: SceneBuilder, anchors: dict) -> None:
    """Subtle smooth organic chitin accents fitted flush onto Worker's slimmer 0.80x thorax."""
    # Petite curved breastplate crest hugging the slimmer chest carapace (`z_max ~ 0.076`).
    crest = (
        superellipsoid(0.034, 0.046, 0.015, e1=0.80, e2=0.84, u_segs=16, v_segs=12, name="chest_crest", material="chitin_team")
        .rotate_xyz(rx=0.10)
        .translate(0.0, 0.402, 0.062)
    )
    # Sleek dorsal pronotum ridge sitting flush atop the slimmer rear dorsal carapace (`z_min ~ -0.078`).
    ridge = (
        superellipsoid(0.029, 0.022, 0.028, e1=0.80, e2=0.84, u_segs=16, v_segs=10, name="dorsal_ridge", material="chitin_team")
        .rotate_xyz(rx=-0.16)
        .translate(0.0, 0.432, -0.058)
    )
    scene.add("worker_carapace", [crest, ridge])

    # Subtle curved team-colored shoulder fairings anchored cleanly onto the slimmer shoulders.
    sh_l = anchors["shoulder_left"]
    cap = (
        superellipsoid(0.020, 0.016, 0.019, e1=0.80, e2=0.84, u_segs=14, v_segs=10, name="shoulder_cap", material="chitin_team")
        .rotate_xyz(rz=-0.34)
        .translate(sh_l[0] * 0.82, sh_l[1] + 0.006, sh_l[2] * 0.82)
    )
    scene.add_mirrored("worker_carapace", cap)

    # Parent canonical world-authored accents under upper_body in lockstep with stance contrapposto & Idle.
    # (Wrist cuffs now ride natively on `base_ant`'s articulated `forearm_left`/`forearm_right` nodes!)
    scene.attach_world_node("worker_carapace", "upper_body", preserve_world_pose=False)


def _slim_worker_torso_and_tail(scene: SceneBuilder, anchors: dict) -> None:
    """Slim Worker's thorax (~0.80x X/Z) and pointed gaster (~0.76x radial thickness) to match
    `ORIG agst901`/`agst301`'s slender torso & tapered tail while keeping neck, petiole, arms,
    and all 4 leg hip roots seamlessly embedded with zero gaps!
    """
    tc = np.asarray(anchors["thorax_center"], dtype=np.float64)
    # 1. Slim thorax shell parts cleanly around `thorax_center` (keeping neck_joint & petiole seamless).
    slim_names = {"thorax_core", "chest_plate", "dorsal_plate", "thorax_seam"}
    new_body = []
    for p in scene.get_node("body"):
        if p.name in slim_names:
            new_body.append(
                p.translate(-tc[0], -tc[1], -tc[2])
                .scale(0.80, 1.0, 0.80)
                .translate(tc[0], tc[1], tc[2])
            )
        elif p.name == "neck_collar":
            new_body.append(
                p.translate(-tc[0], -tc[1], -tc[2])
                .scale(0.88, 1.0, 0.88)
                .translate(tc[0], tc[1], tc[2])
            )
        else:
            new_body.append(p)
    scene.nodes["body"] = new_body

    # Tuck shoulder arm pivots gently inward (-12 mm X) so upper arms emerge flush from slimmer thorax.
    for arm_node, sign in (("arm_left", +1.0), ("arm_right", -1.0)):
        xf = scene.get_node_transform(arm_node)
        if xf is not None:
            tx, ty, tz = xf.translation
            scene.set_node_transform(
                arm_node,
                translation=(tx - sign * 0.012, ty, tz),
                rotation=xf.rotation,
                scale=xf.scale,
            )

    # 2. Slim gaster radially (~0.76x cross-section) around its tilted axis from the petiole hinge,
    #    keeping full pointed length & seamless petiole junction (`agst901`).
    gp = (0.0, 0.278, -0.112)
    g_tilt = -0.24
    if "gaster" in scene.nodes:
        scene.nodes["gaster"] = [
            p.translate(-gp[0], -gp[1], -gp[2])
            .rotate_xyz(rx=-g_tilt)
            .scale(0.76, 0.76, 1.00)
            .rotate_xyz(rx=g_tilt)
            .translate(gp[0], gp[1], gp[2])
            for p in scene.get_node("gaster")
        ]


def _smoothstep(edge0: float, edge1: float, val: np.ndarray) -> np.ndarray:
    t = np.clip((val - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _sculpt_worker_head_profile(scene: SceneBuilder, anchors: dict) -> None:
    """Refine Worker's head (`agst301`/`agst901`): slim width-wise (`0.82x` X so eyes span right
    to the outer edges of the head with zero wide cheek walls), slim front-to-back (`0.86x` Z),
    compact smoothly rounded rear occiput, and flush-seated eyes, concentric eyelids, & mandibles.
    """
    hc = np.asarray(anchors["head_center"], dtype=np.float64)
    nc = np.asarray(anchors["neck_center"], dtype=np.float64)
    x_scale = 0.84
    z_scale = 0.86
    face_dz = -0.022  # keeps front facial features flush on the 0.86x compressed cranium face
    eye_in_left = 0.012
    eye_in_right = 0.016  # slightly extra tuck on bigger viewer-left eye for <= 0 mm eyelid protrusion

    def _deform_head(pos: np.ndarray) -> np.ndarray:
        p = pos.copy()
        rx = p[:, 0] - hc[0]
        ry = p[:, 1] - hc[1]
        rz = p[:, 2] - hc[2]
        # 1. Slim width-wise (`0.84x` X -> eyes span full head width like `ORIG agst301`) & depth-wise (`0.86x` Z).
        p[:, 0] = hc[0] + x_scale * rx
        p[:, 2] = hc[2] + z_scale * rz
        # 2. Smoothly lower the tall forehead (~38 mm less crown height above the eyes, keeping a
        #    cute rounded top arch — never flat) and gently round the upper-back occiput corner.
        crown_t = np.clip((ry - 0.045) / 0.143, 0.0, 1.0)
        corner_w = _smoothstep(0.030, 0.145, ry) * _smoothstep(-0.020, -0.135, rz)
        p[:, 1] -= 0.038 * (crown_t ** 1.45) + 0.005 * corner_w
        p[:, 2] += 0.014 * corner_w
        return p

    head_parts = scene.get_node("head")
    new_head = []
    for p in head_parts:
        if p.name == "cranium":
            new_head.append(p.deform(_deform_head))
        elif p.name == "mouth_cleft":
            new_head.append(p.scale(0.86, 1.0, 1.0).translate(0.0, 0.0, face_dz))
        else:
            new_head.append(p)
    scene.nodes["head"] = new_head

    # Shift eyes & concentric Blink eyelids cleanly backward (`face_dz`) and subtly inward
    # so both eyes span right to the outer edges of the thin head with zero protruding cheek/eyelid!
    if "eyes" in scene.nodes:
        new_eyes = []
        for p in scene.get_node("eyes"):
            sx = -eye_in_left if p.name.endswith("_left") else (+eye_in_right if p.name.endswith("_right") else 0.0)
            new_eyes.append(p.translate(sx, 0.0, face_dz))
        scene.nodes["eyes"] = new_eyes
    for node_name, sx, x_trim in (("eyelid_left", -eye_in_left, 1.0), ("eyelid_right", +eye_in_right, 0.96)):
        if x_trim != 1.0 and node_name in scene.nodes:
            scene.nodes[node_name] = [p.scale(x_trim, 1.0, 1.0) for p in scene.get_node(node_name)]
        xf = scene.get_node_transform(node_name)
        if xf is not None:
            tx, ty, tz = xf.translation
            scene.set_node_transform(
                node_name,
                translation=(tx + sx, ty, tz + face_dz),
                rotation=xf.rotation,
                scale=xf.scale,
            )

    # Frame mandibles cleanly onto the slimmer 0.82x lower jaw.
    if "mandibles" in scene.nodes:
        scene.nodes["mandibles"] = [p.scale(0.88, 1.0, 1.0) for p in scene.get_node("mandibles")]
    m_xf = scene.get_node_transform("mandibles")
    if m_xf is not None:
        tx, ty, tz = m_xf.translation
        scene.set_node_transform(
            "mandibles",
            translation=(tx, ty, tz + face_dz),
            rotation=m_xf.rotation,
            scale=m_xf.scale,
        )

    # Bring antenna bases inward onto the narrower 0.82x crown and nudge by exact local crown delta.
    for ant_node in ("antenna_left", "antenna_right"):
        xf = scene.get_node_transform(ant_node)
        if xf is None:
            continue
        base_canon = np.asarray(xf.translation, dtype=np.float64) + nc
        sample = np.array([[base_canon[0], hc[1] + 0.185, base_canon[2]]], dtype=np.float64)
        delta = _deform_head(sample)[0] - sample[0]
        tx, ty, tz = xf.translation
        scene.set_node_transform(
            ant_node,
            translation=(tx * 0.86, ty + float(delta[1]), tz + float(delta[2])),
            rotation=xf.rotation,
            scale=xf.scale,
        )


def build_worker(blink: float = 0.0) -> SceneBuilder:
    """Build the curious, charming classic Worker Ant (`blink` in [0, 1] sets resting eyelid closure)."""
    scene, anchors = build_base_ant(
        scene_name="ant_worker",
        include_brows=False,
        brow_style="none",
        head_pitch_deg=-13.5,
        head_tilt_deg=4.5,
        head_yaw_deg=-3.5,
        eye_look=(0.06, 0.05),
        eye_scale_left=0.94,
        eye_scale_right=1.05,
        eye_aspect_left=0.92,
        eye_aspect_right=1.18,
        antenna_asymmetry=0.35,
        stance_asymmetry=0.35,
        leg_style="worker",
        left_arm_pose="idle",
        right_arm_pose="idle",
        blink=blink,
    )
    _sculpt_worker_head_profile(scene, anchors)
    _slim_worker_torso_and_tail(scene, anchors)
    _add_carapace_details(scene, anchors)
    scene.ground_to_y(0.0)
    return scene


def build_ant(blink: float = 0.0) -> SceneBuilder:
    """Canonical module entrypoint for `build_all.py`."""
    return build_worker(blink=blink)


__all__ = ["build_ant", "build_worker"]
