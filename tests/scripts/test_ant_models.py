#!/usr/bin/env python3
"""Unit tests for procedural 3D ant modeling core, glTF 2.0 writer, base ant, and build pipeline.

Pure-stdlib material and .glb validation tests run unconditionally (including on CI
runners without numpy/Pillow). Mesh generation and software preview tests are gated
with @unittest.skipUnless so local runs verify everything and CI never fails on
optional imaging packages.
"""
from __future__ import annotations

import glob
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
if REPO not in sys.path:
    sys.path.insert(0, REPO)

from tools.ant_models import gltf_writer, materials  # noqa: E402

try:
    import numpy as np  # noqa: E402

    HAS_NUMPY = True
except ImportError:
    np = None  # type: ignore
    HAS_NUMPY = False

try:
    from PIL import Image  # noqa: E402

    HAS_PIL = True
except ImportError:
    Image = None  # type: ignore
    HAS_PIL = False

if HAS_NUMPY:
    from tools.ant_models import base_ant, build_all, mesh_core, textures  # noqa: E402


class _TinyPart:
    """Pure-Python MeshPart stand-in for testing gltf_writer without numpy."""

    def __init__(self, name, material, positions, normals, indices):
        self.name = name
        self.material = material
        self.positions = positions
        self.normals = normals
        self.indices = indices


class _TinyScene:
    """Pure-Python SceneBuilder stand-in for testing gltf_writer without numpy."""

    def __init__(self, name, nodes):
        self.name = name
        self._nodes = nodes

    def node_names(self):
        return [k for k, v in self._nodes.items() if v]

    def get_node(self, node_name):
        return list(self._nodes.get(node_name, ()))


def _make_pure_python_tetra_scene():
    # Regular tetrahedron with outward CCW faces and unit vertex normals.
    verts = [
        (0.0, 1.0, 0.0),
        (-0.866025, 0.0, -0.5),
        (0.866025, 0.0, -0.5),
        (0.0, 0.0, 1.0),
    ]
    cx = sum(v[0] for v in verts) / 4.0
    cy = sum(v[1] for v in verts) / 4.0
    cz = sum(v[2] for v in verts) / 4.0
    norms = []
    for x, y, z in verts:
        dx, dy, dz = x - cx, y - cy, z - cz
        d = math.sqrt(dx * dx + dy * dy + dz * dz)
        norms.append((dx / d, dy / d, dz / d))
    indices = [
        0, 1, 3,
        0, 3, 2,
        0, 2, 1,
        1, 2, 3,
    ]
    part = _TinyPart("tetra", "chitin_team", verts, norms, indices)
    return _TinyScene("tiny_ant", {"body": [part]})


class MaterialsContract(unittest.TestCase):
    EXPECTED_NAMES = (
        "chitin_team",
        "chitin_dark",
        "eye_white",
        "eye_pupil",
        "gear_gold",
        "gear_gold_metal",
        "gear_accent",
        "gear_dark",
        "glass_tinted",
        "glass_clear",
        "fuse_white",
        "prop_brown",
        "eye_iris",
        "eye_limbal",
        "eye_catchlight",
        "gear_yellow_plastic",
        "gear_steel",
        "gear_brass_trim",
        "bomb_charge",
        "bomb_cup_trim",
    )

    def test_all_canonical_materials_present(self):
        self.assertEqual(materials.MATERIAL_ORDER, self.EXPECTED_NAMES)
        for name in self.EXPECTED_NAMES:
            spec = materials.get_material(name)
            self.assertEqual(spec.name, name)
            self.assertEqual(len(spec.base_color), 4)
            for c in spec.base_color:
                self.assertTrue(0.0 <= c <= 1.0)
            self.assertTrue(0.0 <= spec.metallic <= 1.0)
            self.assertTrue(0.0 <= spec.roughness <= 1.0)
            gltf = spec.to_gltf_dict()
            self.assertEqual(gltf["name"], name)
            pbr = gltf["pbrMetallicRoughness"]
            self.assertEqual(len(pbr["baseColorFactor"]), 4)

    def test_to_gltf_dict_emits_iec_linear_base_color(self):
        # Verify IEC 61966-2-1 sRGB EOTF on toe, gamma curve, and endpoints.
        self.assertEqual(materials.srgb_to_linear(0.0), 0.0)
        self.assertEqual(materials.srgb_to_linear(1.0), 1.0)
        self.assertAlmostEqual(materials.srgb_to_linear(0.04045), 0.04045 / 12.92, places=7)
        self.assertAlmostEqual(materials.srgb_to_linear(0.5), ((0.5 + 0.055) / 1.055) ** 2.4, places=7)

        # Verify every canonical material converts RGB to linear and keeps alpha linear.
        for name in self.EXPECTED_NAMES:
            spec = materials.get_material(name)
            bcf = spec.to_gltf_dict()["pbrMetallicRoughness"]["baseColorFactor"]
            for k in range(3):
                expected = materials.srgb_to_linear(spec.base_color[k])
                self.assertAlmostEqual(bcf[k], expected, places=5, msg=f"{name}[{k}]")
                if 0.0 < spec.base_color[k] < 1.0:
                    self.assertLess(bcf[k], spec.base_color[k], f"{name}[{k}] should be linear (< sRGB)")
            self.assertAlmostEqual(bcf[3], spec.base_color[3], places=6)

        # Verify custom alpha stays linear (unconverted by EOTF) and enables BLEND.
        translucent = materials.MaterialSpec("test_alpha", (0.5, 0.5, 0.5, 0.5), 0.0, 0.5)
        t_dict = translucent.to_gltf_dict()
        bcf = t_dict["pbrMetallicRoughness"]["baseColorFactor"]
        self.assertAlmostEqual(bcf[0], ((0.5 + 0.055) / 1.055) ** 2.4, places=5)
        self.assertAlmostEqual(bcf[3], 0.5, places=6)
        self.assertEqual(t_dict["alphaMode"], "BLEND")
        self.assertTrue(t_dict["doubleSided"])

        # Verify serialized GLB carries IEC-linear baseColorFactor in its JSON chunk.
        glb = gltf_writer.scene_to_glb_bytes(_make_pure_python_tetra_scene())
        doc, _ = gltf_writer.parse_glb(glb)
        chitin = materials.get_material("chitin_team")
        glb_bcf = doc["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"]
        for k in range(3):
            self.assertAlmostEqual(glb_bcf[k], materials.srgb_to_linear(chitin.base_color[k]), places=5)
        self.assertAlmostEqual(glb_bcf[3], 1.0, places=6)

    def test_glass_materials_emit_blend_alpha_mode(self):
        for name in ("glass_tinted", "glass_clear"):
            spec = materials.get_material(name)
            gltf = spec.to_gltf_dict()
            self.assertEqual(gltf["alphaMode"], "BLEND", name)
            self.assertTrue(gltf["doubleSided"], name)
            alpha = gltf["pbrMetallicRoughness"]["baseColorFactor"][3]
            self.assertGreater(alpha, 0.0, name)
            self.assertLess(alpha, 0.70, name)
        # Opaque chitin remains OPAQUE with alpha == 1.0.
        chitin_gltf = materials.get_material("chitin_team").to_gltf_dict()
        self.assertEqual(chitin_gltf["alphaMode"], "OPAQUE")
        self.assertAlmostEqual(chitin_gltf["pbrMetallicRoughness"]["baseColorFactor"][3], 1.0, places=6)
        self.assertNotIn("emissiveFactor", chitin_gltf)

        # Eye catchlight emits linear emissiveFactor and limbal ring is registered.
        glint_gltf = materials.get_material("eye_catchlight").to_gltf_dict()
        self.assertIn("emissiveFactor", glint_gltf)
        self.assertGreater(glint_gltf["emissiveFactor"][0], 0.14)
        self.assertLessEqual(glint_gltf["pbrMetallicRoughness"]["roughnessFactor"], 0.06)
        self.assertIn("eye_limbal", materials.MATERIALS)
        # Molded smooth yellow beach-toy plastic has no woven cloth texture.
        yp = materials.get_material("gear_yellow_plastic")
        self.assertIsNone(yp.texture_family)
        self.assertLessEqual(yp.roughness, 0.40)

        # Pure-stdlib GLB validator accepts BLEND glass materials cleanly.
        glass_scene = _make_pure_python_tetra_scene()
        glass_scene._nodes["body"][0].material = "glass_tinted"
        stats = gltf_writer.validate_glb(gltf_writer.scene_to_glb_bytes(glass_scene))
        self.assertEqual(stats["materials"], ["glass_tinted"])

    def test_unknown_material_raises(self):
        with self.assertRaises(KeyError):
            materials.get_material("does_not_exist")

    def test_team_palettes_cover_all_four_teams(self):
        for team in ("green", "red", "blue", "black"):
            self.assertIn(team, materials.TEAM_COLORS)
            self.assertIn("chitin_team", materials.TEAM_COLORS[team])
            self.assertIn("chitin_dark", materials.TEAM_COLORS[team])
            c_team = materials.material_color("chitin_team", team=team)
            self.assertEqual(len(c_team), 4)
        self.assertNotEqual(
            materials.material_color("chitin_team", "green"),
            materials.material_color("chitin_team", "red"),
        )


class GltfWriterPureStdlib(unittest.TestCase):
    def test_roundtrip_and_determinism_without_numpy(self):
        scene = _make_pure_python_tetra_scene()
        glb1 = gltf_writer.scene_to_glb_bytes(scene)
        glb2 = gltf_writer.scene_to_glb_bytes(scene)
        self.assertEqual(glb1, glb2)
        self.assertEqual(len(glb1) % 4, 0)

        stats = gltf_writer.validate_glb(glb1)
        self.assertEqual(stats["triangles"], 4)
        self.assertEqual(stats["vertices"], 4)
        self.assertEqual(stats["materials"], ["chitin_team"])
        self.assertIn("tiny_ant", stats["nodes"])
        self.assertIn("body", stats["nodes"])
        self.assertAlmostEqual(stats["bbox_min"][1], 0.0, places=4)
        self.assertAlmostEqual(stats["bbox_max"][1], 1.0, places=4)

        with tempfile.TemporaryDirectory() as tmp:
            out_path = os.path.join(tmp, "tetra.glb")
            file_stats = gltf_writer.write_glb(scene, out_path)
            self.assertEqual(file_stats["triangles"], 4)
            self.assertEqual(gltf_writer.validate_glb(out_path)["byte_length"], len(glb1))

    def test_validator_rejects_corrupted_glb(self):
        good = bytearray(gltf_writer.scene_to_glb_bytes(_make_pure_python_tetra_scene()))

        bad_magic = bytearray(good)
        bad_magic[0:4] = b"NOPE"
        with self.assertRaises(ValueError):
            gltf_writer.validate_glb(bad_magic)

        truncated = bytes(good[:-4])
        with self.assertRaises(ValueError):
            gltf_writer.validate_glb(truncated)

        # Non-unit normal rejected.
        bad_norm_scene = _make_pure_python_tetra_scene()
        bad_norm_scene._nodes["body"][0].normals[0] = (0.0, 5.0, 0.0)
        bad_norm_glb = gltf_writer.scene_to_glb_bytes(bad_norm_scene)
        with self.assertRaises(ValueError):
            gltf_writer.validate_glb(bad_norm_glb)

        # Degenerate triangle (duplicate vertex index) rejected.
        bad_tri_scene = _make_pure_python_tetra_scene()
        bad_tri_scene._nodes["body"][0].indices[1] = bad_tri_scene._nodes["body"][0].indices[0]
        bad_tri_glb = gltf_writer.scene_to_glb_bytes(bad_tri_scene)
        with self.assertRaises(ValueError):
            gltf_writer.validate_glb(bad_tri_glb)

        # NaN coordinate rejected at serialization time.
        nan_scene = _make_pure_python_tetra_scene()
        nan_scene._nodes["body"][0].positions[0] = (float("nan"), 1.0, 0.0)
        with self.assertRaises(ValueError):
            gltf_writer.scene_to_glb_bytes(nan_scene)

        # Corrupted BIN float (NaN injected directly into valid GLB) rejected by validator.
        doc, bin_data = gltf_writer.parse_glb(bytes(good))
        json_len = struct.unpack_from("<I", good, 12)[0]
        bin_start = 12 + 8 + json_len + 8
        nan_glb = bytearray(good)
        struct.pack_into("<f", nan_glb, bin_start, float("nan"))
        with self.assertRaises(ValueError):
            gltf_writer.validate_glb(nan_glb)
        self.assertGreater(len(doc["meshes"]), 0)
        self.assertGreater(len(bin_data), 0)

    def test_hierarchy_parent_child_and_translation_rotation_animations(self):
        """Pure-stdlib verification of parent->child glTF node DAG + translation & rotation clips."""
        base = _make_pure_python_tetra_scene()

        class _StubXf:
            def __init__(self, translation=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0, 1.0), scale=(1.0, 1.0, 1.0)):
                self.translation = translation
                self.rotation = rotation
                self.scale = scale

        class _StubCh:
            def __init__(self, target_node, path, times, values):
                self.target_node = target_node
                self.path = path
                self.times = times
                self.values = values
                self.interpolation = "LINEAR"

        class _StubClip:
            def __init__(self, name, channels):
                self.name = name
                self.channels = channels

        base._nodes["pivot"] = []
        base._parents = {"body": "pivot"}
        base._xfs = {"pivot": _StubXf((0.0, 0.5, 0.0)), "body": _StubXf((0.0, -0.5, 0.0))}
        base.get_node_parent = lambda n: base._parents.get(n)
        base.get_node_transform = lambda n: base._xfs.get(n)
        base.hierarchy_node_names = lambda: ["pivot", "body"]
        s45 = math.sin(0.25)
        c45 = math.cos(0.25)
        base.animations = [
            _StubClip(
                "Idle",
                [
                    _StubCh("pivot", "translation", [0.0, 0.5, 1.0], [(0.0, 0.5, 0.0), (0.0, 0.6, 0.0), (0.0, 0.5, 0.0)]),
                    _StubCh("pivot", "rotation", [0.0, 0.5, 1.0], [(0.0, 0.0, 0.0, 1.0), (s45, 0.0, 0.0, c45), (0.0, 0.0, 0.0, 1.0)]),
                ],
            )
        ]

        glb = gltf_writer.scene_to_glb_bytes(base)
        stats = gltf_writer.validate_glb(glb)
        self.assertIn("pivot", stats["nodes"])
        self.assertIn("body", stats["nodes"])
        self.assertEqual(stats["animations"], ["Idle"])
        # Composed parent (+0.5) + child (-0.5) preserves exact world bounds [0, 1].
        self.assertAlmostEqual(stats["bbox_min"][1], 0.0, places=4)
        self.assertAlmostEqual(stats["bbox_max"][1], 1.0, places=4)

        doc, bin_data = gltf_writer.parse_glb(glb)
        by_name = {n["name"]: (i, n) for i, n in enumerate(doc["nodes"])}
        piv_i, piv_node = by_name["pivot"]
        body_i, _ = by_name["body"]
        self.assertIn(body_i, piv_node.get("children", []))
        self.assertNotIn(body_i, doc["nodes"][0].get("children", []))

        # Validator rejects cyclic node.children graphs.
        doc["nodes"][body_i]["children"] = [piv_i]
        bad_json = gltf_writer._pad4(json.dumps(doc, separators=(",", ":")).encode("utf-8"), b" ")
        bad_bin = gltf_writer._pad4(bin_data, b"\x00")
        total = 12 + 8 + len(bad_json) + 8 + len(bad_bin)
        bad_glb = (
            struct.pack("<III", gltf_writer.GLB_MAGIC, gltf_writer.GLB_VERSION, total)
            + struct.pack("<II", len(bad_json), gltf_writer.CHUNK_JSON)
            + bad_json
            + struct.pack("<II", len(bad_bin), gltf_writer.CHUNK_BIN)
            + bad_bin
        )
        with self.assertRaises(ValueError):
            gltf_writer.validate_glb(bad_glb)

    def test_existing_built_glb_files_validate(self):
        models_dir = os.path.join(REPO, "tools", "ant_models", "models")
        glbs = sorted(glob.glob(os.path.join(models_dir, "*.glb"))) if os.path.isdir(models_dir) else []
        if glbs:
            for glb_path in glbs:
                stats = gltf_writer.validate_glb(glb_path)
                self.assertGreater(stats["triangles"], 0, glb_path)
                self.assertGreaterEqual(stats["bbox_min"][1], -0.02, glb_path)
                self.assertLessEqual(stats["bbox_min"][1], 0.05, glb_path)
            return
        if not HAS_NUMPY:
            self.skipTest("no prebuilt tools/ant_models/models/*.glb and numpy is not installed")
        with tempfile.TemporaryDirectory() as tmp:
            build_all.build_all(out_dir=tmp, quiet=True)
            built_glbs = sorted(glob.glob(os.path.join(tmp, "*.glb")))
            self.assertEqual(len(built_glbs), len(build_all.ANT_CLASSES))
            for glb_path in built_glbs:
                stats = gltf_writer.validate_glb(glb_path)
                self.assertGreater(stats["triangles"], 0, glb_path)
                self.assertGreaterEqual(stats["bbox_min"][1], -0.02, glb_path)
                self.assertLessEqual(stats["bbox_min"][1], 0.05, glb_path)


@unittest.skipUnless(HAS_NUMPY, "numpy is needed")
class MeshCorePrimitives(unittest.TestCase):
    def _assert_valid_closed_mesh(self, part, *, min_tris: int = 8):
        self.assertEqual(part.positions.dtype, np.float32, part.name)
        self.assertEqual(part.normals.dtype, np.float32, part.name)
        self.assertEqual(part.uvs.dtype, np.float32, part.name)
        self.assertEqual(part.colors.dtype, np.float32, part.name)
        self.assertEqual(part.indices.dtype, np.uint32, part.name)
        self.assertEqual(part.uvs.shape, (len(part.positions), 2), part.name)
        self.assertEqual(part.colors.shape, (len(part.positions), 3), part.name)
        self.assertGreaterEqual(part.triangle_count(), min_tris, part.name)
        self.assertTrue(np.all(np.isfinite(part.positions)), part.name)
        self.assertTrue(np.all(np.isfinite(part.normals)), part.name)
        self.assertTrue(np.all(np.isfinite(part.uvs)), part.name)
        self.assertTrue(np.all(np.isfinite(part.colors)), part.name)

        n_lens = np.linalg.norm(part.normals.astype(np.float64), axis=1)
        self.assertTrue(np.allclose(n_lens, 1.0, atol=1e-4), f"{part.name}: non-unit normals")

        idx = part.indices.reshape(-1, 3)
        self.assertTrue(np.all(idx < len(part.positions)), part.name)
        self.assertTrue(np.all(idx[:, 0] != idx[:, 1]), part.name)
        self.assertTrue(np.all(idx[:, 1] != idx[:, 2]), part.name)
        self.assertTrue(np.all(idx[:, 0] != idx[:, 2]), part.name)

        vol = mesh_core.signed_volume(part.positions, part.indices)
        self.assertGreater(vol, 1e-7, f"{part.name}: expected positive outward signed volume, got {vol}")

        # Verify vertex normals point in the same hemisphere as geometric face normals.
        pos64 = part.positions.astype(np.float64)
        v0, v1, v2 = pos64[idx[:, 0]], pos64[idx[:, 1]], pos64[idx[:, 2]]
        face_n = np.cross(v1 - v0, v2 - v0)
        area = np.linalg.norm(face_n, axis=1)
        non_degen = area > 1e-10
        avg_vn = (part.normals[idx[:, 0]] + part.normals[idx[:, 1]] + part.normals[idx[:, 2]]) / 3.0
        dots = np.sum(face_n[non_degen] * avg_vn[non_degen], axis=1)
        self.assertGreater(float(np.mean(dots > 0.0)), 0.98, f"{part.name}: normals disagree with winding")

    def test_all_primitives_have_outward_winding_and_unit_normals(self):
        prims = [
            mesh_core.superellipsoid(0.3, 0.25, 0.28, e1=0.7, e2=0.75, name="vinyl_head"),
            mesh_core.sphere(0.25, name="sphere"),
            mesh_core.ellipsoid(0.2, 0.3, 0.15, name="ellipsoid"),
            mesh_core.swept_tube([(0, 0, 0), (0.1, 0.2, 0.1), (0.2, 0.4, 0.0)], [0.04, 0.03, 0.05], name="open_tube"),
            mesh_core.curved_tube([(0, 0, 0), (0.2, 0.1, 0.1), (0.3, 0.3, -0.1), (0.1, 0.4, 0.2)], 0.03, name="curved_tube"),
            mesh_core.swept_tube(
                [(0.3, 0, 0), (0, 0, 0.3), (-0.3, 0, 0), (0, 0, -0.3)],
                (0.04, 0.025),
                closed=True,
                smooth_subdiv=5,
                name="closed_loop_tube",
            ),
            mesh_core.lathe([(0.0, 0.0), (0.15, 0.1), (0.12, 0.3), (0.0, 0.4)], name="teardrop_lathe"),
            mesh_core.lathe([(0.12, 0.3), (0.15, 0.1), (0.10, 0.0)], cap_bottom=True, cap_top=True, name="reversed_lathe"),
            mesh_core.cylinder(0.10, 0.08, 0.30, bevel=0.02, axis="z", name="beveled_cyl"),
            mesh_core.cone(0.12, 0.25, name="cone"),
            mesh_core.capsule(0.06, 0.20, axis="x", name="capsule_x"),
            mesh_core.torus_arc(0.20, 0.04, plane="xy", name="torus_full"),
            mesh_core.torus_arc(0.20, 0.04, start_ang=0.2, end_ang=2.5, plane="xz", name="torus_open_arc"),
            mesh_core.rounded_box(0.4, 0.3, 0.2, bevel=0.05, segs=3, name="beveled_box"),
            mesh_core.rounded_box(0.3, 0.3, 0.3, bevel=0.0, name="sharp_box"),
            mesh_core.hemisphere_shell(
                0.26,
                0.22,
                0.26,
                thickness=0.03,
                start_phi=-0.1,
                deform_fn=lambda p: np.column_stack([p[:, 0], p[:, 1], p[:, 2] + 0.05 * np.clip(p[:, 2], 0, None)]),
                name="helmet_shell",
            ),
            mesh_core.extrude_polygon(
                [(-0.1, -0.1), (0.1, -0.1), (0.1, 0.0), (0.0, 0.0), (0.0, 0.1), (-0.1, 0.1)],
                depth=0.04,
                name="nonconvex_L",
            ),
        ]
        for p in prims:
            self._assert_valid_closed_mesh(p)

    def test_transforms_mirror_and_winding_repair(self):
        box = mesh_core.rounded_box(0.4, 0.2, 0.3, bevel=0.04)
        bmin, bmax = box.bounds()
        self.assertTrue(np.allclose(bmax - bmin, [0.4, 0.2, 0.3], atol=1e-4))

        moved = box.rotate_xyz(15, 30, 45, degrees=True).scale(1.2, 0.8, 1.0).translate(0.5, 0.2, -0.3)
        self._assert_valid_closed_mesh(moved)

        mirrored = moved.mirror_x("box_r")
        self._assert_valid_closed_mesh(mirrored)
        self.assertAlmostEqual(float(mirrored.bounds()[0][0]), -float(moved.bounds()[1][0]), places=4)

        neg_scaled = box.scale(-1.0, 1.0, 1.0)
        self._assert_valid_closed_mesh(neg_scaled)

        # Deliberately invert winding and confirm ensure_outward restores positive volume.
        flipped = mesh_core.MeshPart(
            "flipped",
            "gear_gold",
            box.positions.copy(),
            -box.normals.copy(),
            box.indices.reshape(-1, 3)[:, [0, 2, 1]].reshape(-1),
        )
        self.assertLess(mesh_core.signed_volume(flipped.positions, flipped.indices), 0.0)
        fixed = flipped.ensure_outward()
        self._assert_valid_closed_mesh(fixed)


@unittest.skipUnless(HAS_NUMPY, "numpy is needed")
class BaseAntAndBuildPipeline(unittest.TestCase):
    REQUIRED_ANCHORS = (
        "head_center",
        "head_top",
        "head_radius",
        "head_radii",
        "brow_y",
        "back_anchor",
        "right_hand",
        "left_hand",
        "ground_y",
        "eye_left",
        "eye_right",
        "eye_center",
    )

    def test_base_ant_grounding_proportions_and_anchors(self):
        scene, anchors = base_ant.build_base_ant("ant_test")
        for key in self.REQUIRED_ANCHORS + (
            "crouch",
            "eyelid_closed_angle",
            "foot_mid_left",
            "foot_mid_right",
            "foot_rear_left",
            "foot_rear_right",
        ):
            self.assertIn(key, anchors)

        self.assertAlmostEqual(anchors["ground_y"], 0.0, places=5)
        self.assertAlmostEqual(anchors["crouch"], 0.0, places=5)
        self.assertGreater(anchors["left_hand"][0], 0.0)
        self.assertLess(anchors["right_hand"][0], 0.0)
        self.assertTrue(0.85 <= anchors["head_top"][1] <= 0.96, f"unexpected head_top {anchors['head_top'][1]}")
        hrx, hry, hrz = anchors["head_radii"]
        self.assertTrue(0.20 <= hrx <= 0.24, f"unexpected iconic 1998 head radius {hrx}")
        self.assertGreater(hrx, hry, "soft-cube head should be slightly wider than tall")
        self.assertGreater(hry, hrz, "head should be narrower front-to-back than tall")
        self.assertGreater(anchors["eyelid_closed_angle"], 0.9)
        # Petite 1998 thorax should be noticeably slimmer than the iconic head (~half head width).
        thorax_w = max(float(p.bounds()[1][0] - p.bounds()[0][0]) for p in scene.get_node("body") if p.name == "thorax_core")
        self.assertLess(thorax_w, hrx * 1.15, f"expected petite thorax narrower than head, got {thorax_w:.3f}")

        glb_bytes = gltf_writer.scene_to_glb_bytes(scene)
        stats = gltf_writer.validate_glb(glb_bytes)
        self.assertGreater(stats["triangles"], 1000)
        self.assertAlmostEqual(stats["bbox_min"][1], 0.0, delta=1e-3)
        self.assertTrue(0.96 <= stats["bbox_max"][1] <= 1.15, f"unexpected height {stats['bbox_max'][1]}")
        width, _, depth = stats["dimensions"]
        self.assertTrue(0.40 <= width <= 0.56, f"unexpected slim width {width}")
        self.assertLessEqual(depth, 0.66, f"expected compact tucked Z depth matching 901 E sprite, got {depth}")
        self.assertGreaterEqual(stats["bbox_min"][2], -0.38, f"gaster protrudes too far back: {stats['bbox_min'][2]}")
        self.assertLessEqual(width / stats["bbox_max"][1], 0.50, "expected upright slim W/H ratio")
        for expected_node in ("ant_test", "head", "eyes", "eyelid_left", "eyelid_right", "antennae", "body", "arms", "legs"):
            self.assertIn(expected_node, stats["nodes"])
        self.assertIn("Blink", stats["animations"])
        self.assertIn("eye_iris", stats["materials"])
        self.assertIn("eye_limbal", stats["materials"])
        self.assertIn("eye_catchlight", stats["materials"])
        self.assertTrue(stats["has_uvs"])
        self.assertTrue(stats["has_ao"])
        self.assertGreaterEqual(stats["textures"], 3)
        self.assertGreaterEqual(stats["images"], 3)
        self.assertLess(stats["byte_length"], 1_800_000)

    def test_pbr_textures_and_baked_crevice_ao(self):
        # Verify all 4 procedural texture families encode valid PNGs and cache deterministically.
        for fam, exp_size in (("chitin", 256), ("cloth", 128), ("metal", 128), ("rubber", 128)):
            p1 = textures.get_texture_pack(fam)
            p2 = textures.get_texture_pack(fam)
            self.assertIs(p1, p2)
            self.assertEqual(p1.size, exp_size)
            for png in (p1.base_color_png, p1.normal_png, p1.metallic_roughness_png):
                self.assertIsNotNone(png)
                self.assertTrue(png.startswith(b"\x89PNG\r\n\x1a\n"))
                iw, ih = struct.unpack_from(">II", png, 16)
                self.assertEqual((iw, ih), (exp_size, exp_size))

        # Verify baked crevice AO creates real depth variation, stays idempotent, and respects floors.
        scene, _ = base_ant.build_base_ant("ao_test")
        mesh_core.bake_scene_ao(scene)
        chitin_ao = np.concatenate([p.colors[:, 0] for p in scene.all_parts() if p.material == "chitin_team"])
        eye_ao = np.concatenate([p.colors[:, 0] for p in scene.all_parts() if p.material == "eye_white"])
        glint_ao = np.concatenate([p.colors[:, 0] for p in scene.all_parts() if p.material == "eye_catchlight"])
        self.assertGreaterEqual(float(chitin_ao.min()), 0.43)
        self.assertLessEqual(float(chitin_ao.max()), 1.001)
        self.assertGreater(float(chitin_ao.max() - chitin_ao.min()), 0.12, "expected real crevice AO contrast")
        self.assertGreaterEqual(float(eye_ao.min()), 0.77, "eyes must stay bright inside sockets")
        self.assertAlmostEqual(float(glint_ao.min()), 1.0, places=5, msg="catchlights must stay pure bright")
        # Calling bake_scene_ao a second time must be idempotent (never double-darkens vertex shading).
        mesh_core.bake_scene_ao(scene)
        chitin_ao_2 = np.concatenate([p.colors[:, 0] for p in scene.all_parts() if p.material == "chitin_team"])
        self.assertTrue(np.allclose(chitin_ao, chitin_ao_2, atol=1e-5))

        # Verify embedded textures, compact U16 indices, and normalized U8 VEC4 COLOR_0 in GLB JSON.
        glb = gltf_writer.scene_to_glb_bytes(scene)
        doc, _ = gltf_writer.parse_glb(glb)
        mats = {m["name"]: m for m in doc["materials"]}
        self.assertIn("baseColorTexture", mats["chitin_team"]["pbrMetallicRoughness"])
        self.assertIn("metallicRoughnessTexture", mats["chitin_team"]["pbrMetallicRoughness"])
        self.assertIn("normalTexture", mats["chitin_team"])
        chitin_ns = float(mats["chitin_team"]["normalTexture"]["scale"])
        self.assertTrue(0.08 <= chitin_ns <= 0.16, f"expected silky subtle chitin normal scale, got {chitin_ns}")
        # Optical eyes remain pure untextured gloss.
        self.assertNotIn("normalTexture", mats["eye_white"])
        self.assertNotIn("baseColorTexture", mats["eye_white"]["pbrMetallicRoughness"])
        # Small primitives pack indices as UNSIGNED_SHORT and COLOR_0 as normalized UBYTE VEC4.
        prim0 = doc["meshes"][0]["primitives"][0]
        idx_acc = doc["accessors"][prim0["indices"]]
        col_acc = doc["accessors"][prim0["attributes"]["COLOR_0"]]
        self.assertEqual(idx_acc["componentType"], gltf_writer.COMPONENT_UNSIGNED_SHORT)
        self.assertEqual(col_acc["componentType"], gltf_writer.COMPONENT_UNSIGNED_BYTE)
        self.assertEqual(col_acc["type"], "VEC4")
        self.assertTrue(col_acc["normalized"])

    def test_blink_eyelid_pivots_and_animation_validation(self):
        s_open, anchors = base_ant.build_base_ant("ant_open", blink=0.0)
        s_closed, _ = base_ant.build_base_ant("ant_closed", blink=1.0)
        doc_open, _ = gltf_writer.parse_glb(gltf_writer.scene_to_glb_bytes(s_open))
        doc_closed, _ = gltf_writer.parse_glb(gltf_writer.scene_to_glb_bytes(s_closed))

        nodes_open = {n["name"]: n for n in doc_open["nodes"]}
        nodes_closed = {n["name"]: n for n in doc_closed["nodes"]}
        self.assertEqual(nodes_open["eyelid_left"]["rotation"], [0.0, 0.0, 0.0, 1.0])
        self.assertAlmostEqual(nodes_open["eyelid_left"]["translation"][0], anchors["eye_left"][0], places=4)
        self.assertAlmostEqual(nodes_open["eyelid_right"]["translation"][0], anchors["eye_right"][0], places=4)
        # Closed blink rotates eyelid nodes around +X (non-zero qx).
        self.assertGreater(nodes_closed["eyelid_left"]["rotation"][0], 0.45)
        self.assertGreater(nodes_closed["eyelid_right"]["rotation"][0], 0.45)

        # Both Blink (2 snappy 135 ms eyelid channels) and default Worker Idle (translation+rotation) clips are exported.
        anims_by_name = {a["name"]: a for a in doc_open["animations"]}
        self.assertIn("Blink", anims_by_name)
        self.assertIn("Idle", anims_by_name)
        self.assertEqual(len(anims_by_name["Blink"]["channels"]), 2)
        blink_clip = next(c for c in s_open.animations if c.name == "Blink")
        blink_ts = blink_clip.channels[0].times
        self.assertAlmostEqual(blink_ts[4] - blink_ts[1], 0.135, places=4, msg=f"expected snappy 135ms blink, got {blink_ts}")
        idle_paths = {ch["target"]["path"] for ch in anims_by_name["Idle"]["channels"]}
        self.assertIn("translation", idle_paths)
        self.assertIn("rotation", idle_paths)

        # Verify hierarchical rig parents (legs stay rooted at Y=0; upper_body -> head_group -> head/eyelids).
        idx_of = {n["name"]: i for i, n in enumerate(doc_open["nodes"])}
        self.assertIn(idx_of["legs"], doc_open["nodes"][0]["children"])
        self.assertIn(idx_of["upper_body"], doc_open["nodes"][0]["children"])
        self.assertIn(idx_of["head_group"], doc_open["nodes"][idx_of["upper_body"]]["children"])
        self.assertIn(idx_of["gaster_group"], doc_open["nodes"][idx_of["upper_body"]]["children"])
        self.assertIn(idx_of["eyelid_left"], doc_open["nodes"][idx_of["head_group"]]["children"])

        # Geometric ellipsoidal eyelid clearance across symmetric, asymmetric, and vertically stretched eyes
        # with non-zero eye_look gaze across blink=0..1, plus zero forehead/temple protrusion at blink=0.
        eye_mats = ("eye_white", "eye_limbal", "eye_iris", "eye_pupil", "eye_catchlight")
        for sl, sr, al, ar, look in (
            (1.0, 1.0, 1.0, 1.0, (0.0, 0.0)),
            (0.92, 1.08, 1.0, 1.0, (-0.15, 0.12)),
            (0.94, 1.05, 0.92, 1.18, (-0.12, 0.10)),
        ):
            for b_val in (0.0, 0.25, 0.5, 0.75, 1.0):
                sc_b, _ = base_ant.build_base_ant(
                    f"clearance_{b_val}",
                    blink=b_val,
                    eye_scale_left=sl,
                    eye_scale_right=sr,
                    eye_aspect_left=al,
                    eye_aspect_right=ar,
                    eye_look=look,
                )
                w_parts = sc_b.world_parts()
                hm_inv = np.linalg.inv(sc_b.get_world_matrix("head_group"))
                c_pos = next(p for p in w_parts if p.name == "cranium").positions.astype(np.float64)
                c_loc = (hm_inv[:3, :3] @ c_pos.T).T + hm_inv[:3, 3]
                hc = 0.5 * (c_loc.min(axis=0) + c_loc.max(axis=0))
                c_rel = c_loc - hc
                c_r = np.linalg.norm(c_rel, axis=1)
                c_dir = c_rel / c_r[:, None]

                for side_sign, lid_node, lid_name, s_side, a_side in (
                    (1.0, "eyelid_left", "eyelid_left_shell", sl, al),
                    (-1.0, "eyelid_right", "eyelid_right_shell", sr, ar),
                ):
                    lid_m = sc_b.get_world_matrix(lid_node)
                    lid_m_inv = np.linalg.inv(lid_m)
                    eye_verts = np.vstack(
                        [
                            p.positions.astype(np.float64)
                            for p in w_parts
                            if p.material in eye_mats
                            and float(p.positions[:, 0].mean()) * side_sign > 0.0
                        ]
                    )
                    # Transform eye vertices into the eyelid's local un-yawed shell frame and verify
                    # every vertex sits strictly inside the inner ellipsoid (r_x - thick, r_yz - thick).
                    ev_lid = (lid_m_inv[:3, :3] @ eye_verts.T).T + lid_m_inv[:3, 3]
                    cy, sy = math.cos(-side_sign * 0.15), math.sin(-side_sign * 0.15)
                    ex1 = ev_lid[:, 0] * cy + ev_lid[:, 2] * sy
                    ey1 = ev_lid[:, 1]
                    ez1 = -ev_lid[:, 0] * sy + ev_lid[:, 2] * cy
                    r_x, r_yz = base_ant._eyelid_radii(s_side, a_side)
                    th = 0.0028 * s_side
                    in_rx, in_ryz = r_x - th, r_yz - th
                    ev_r = np.linalg.norm(ev_lid, axis=1)
                    ell_norm = np.sqrt((ex1 / in_rx) ** 2 + (ey1 / in_ryz) ** 2 + (ez1 / in_ryz) ** 2)
                    min_clr = float((ev_r * (1.0 / ell_norm - 1.0)).min())
                    self.assertGreaterEqual(
                        min_clr,
                        0.0012,
                        f"{lid_name} ({sl}/{sr}, aspect={al}/{ar}, look={look}) at blink={b_val}: clearance {min_clr:.5f} < 0.0012",
                    )
                    if b_val == 0.0:
                        # At blink=0, zero eyelid shell vertices may protrude past the smooth cranium.
                        lid_w = next(p for p in w_parts if p.name == lid_name).positions.astype(np.float64)
                        lid_h = (hm_inv[:3, :3] @ lid_w.T).T + hm_inv[:3, 3]
                        l_rel = lid_h - hc
                        l_r = np.linalg.norm(l_rel, axis=1)
                        l_dir = l_rel / l_r[:, None]
                        protrude = float((l_r - c_r[np.argmax(l_dir @ c_dir.T, axis=1)]).max())
                        self.assertLessEqual(
                            protrude,
                            0.001,
                            f"{lid_name} ({sl}/{sr}, aspect={al}/{ar}) protrudes {protrude:.4f} past cranium at blink=0",
                        )

        # Vertical eye_aspect_right > eye_aspect_left makes the viewer-left (-X / right) sclera noticeably taller.
        sc_tall, anc_tall = base_ant.build_base_ant(
            "aspect_test",
            eye_scale_left=0.94,
            eye_scale_right=1.05,
            eye_aspect_left=0.92,
            eye_aspect_right=1.18,
            head_pitch_deg=0.0,
        )
        scl_l = next(p for p in sc_tall.world_parts() if p.name == "sclera_left")
        scl_r = next(p for p in sc_tall.world_parts() if p.name == "sclera_right")
        h_l = float(scl_l.bounds()[1][1] - scl_l.bounds()[0][1])
        h_r = float(scl_r.bounds()[1][1] - scl_r.bounds()[0][1])
        self.assertGreater(h_r, h_l * 1.30, f"expected taller right (-X) eye, got {h_r:.4f} vs {h_l:.4f}")
        self.assertGreater(anc_tall["eye_right"][1], anc_tall["eye_left"][1] + 0.005)

        # attach_world_node preserves exact rest world bounds when specialists parent headgear to head_group.
        sc_hat, a_hat = base_ant.build_base_ant("hat_attach")
        hat_part = mesh_core.sphere(0.08, name="test_hat", material="gear_gold").translate(0.0, a_hat["head_top"][1] + 0.04, 0.02)
        sc_hat.add("test_hat_node", hat_part)
        b_before = sc_hat.bounds()
        sc_hat.attach_world_node("test_hat_node", "head_group")
        b_after = sc_hat.bounds()
        self.assertTrue(np.allclose(b_before[0], b_after[0], atol=1e-5))
        self.assertTrue(np.allclose(b_before[1], b_after[1], atol=1e-5))
        gltf_writer.validate_glb(gltf_writer.scene_to_glb_bytes(sc_hat))

        # Validator rejects non-unit quaternion in animation track.
        bad_scene, _ = base_ant.build_base_ant("ant_bad_anim")
        bad_scene.animations[0].channels[0].values[2] = (5.0, 0.0, 0.0, 1.0)
        bad_glb = gltf_writer.scene_to_glb_bytes(bad_scene)
        with self.assertRaises(ValueError):
            gltf_writer.validate_glb(bad_glb)

        # Validator rejects non-monotonic timestamps.
        bad_time_scene, _ = base_ant.build_base_ant("ant_bad_time")
        bad_time_scene.animations[0].channels[0].times[3] = bad_time_scene.animations[0].channels[0].times[2]
        bad_time_glb = gltf_writer.scene_to_glb_bytes(bad_time_scene)
        with self.assertRaises(ValueError):
            gltf_writer.validate_glb(bad_time_glb)

    def test_base_ant_crouch_stance(self):
        _, a_up = base_ant.build_base_ant("ant_up", crouch=0.0)
        for c_val in (0.12, 0.18, 0.22):
            sc, anc = base_ant.build_base_ant(f"ant_crouch_{c_val}", crouch=c_val)
            self.assertAlmostEqual(anc["crouch"], c_val, places=5)
            self.assertAlmostEqual(a_up["head_top"][1] - anc["head_top"][1], c_val, delta=1e-3)
            legs_min_y = min(float(p.bounds()[0][1]) for p in sc.world_parts() if p.name.startswith("leg_"))
            gaster_min_y = min(float(p.bounds()[0][1]) for p in sc.get_node("gaster"))
            self.assertAlmostEqual(legs_min_y, 0.0, delta=1e-3, msg=f"feet must stay planted at crouch={c_val}")
            self.assertGreater(gaster_min_y, 0.02, msg=f"gaster must clear ground at crouch={c_val}")
            stats = gltf_writer.validate_glb(gltf_writer.scene_to_glb_bytes(sc))
            self.assertAlmostEqual(stats["bbox_min"][1], 0.0, delta=1e-3)
            self.assertIn("Idle", stats["animations"])
            self.assertIn("Blink", stats["animations"])

    def test_organic_leg_stance_and_grounded_knee_flex(self):
        from tools.ant_models import leg_rig

        sc, anc = base_ant.build_base_ant("legs_natural")
        sym, anc_sym = base_ant.build_base_ant("legs_sym", leg_style="symmetric")
        with self.assertRaises(ValueError):
            base_ant.build_base_ant("legs_bad", leg_style="wobbly")
        # Four individual leg nodes under `legs`, each anchored on its own planted foot.
        for node in leg_rig.LEG_NODES:
            self.assertEqual(sc.get_node_parent(node), "legs")
            foot = anc["foot_" + node[4:]]
            self.assertAlmostEqual(foot[1], 0.0, places=6)
            self.assertLess(float(np.linalg.norm(sc.get_world_matrix(node)[:3, 3][[0, 2]] - np.array(foot)[[0, 2]])), 0.03)
        # Natural stance is not a mirrored copy; the symmetric style still mirrors exactly.
        self.assertGreater(abs(anc["foot_mid_left"][2] - anc["foot_mid_right"][2]), 0.01)
        self.assertAlmostEqual(anc_sym["foot_mid_left"][0], -anc_sym["foot_mid_right"][0], places=5)
        self.assertAlmostEqual(anc_sym["foot_mid_left"][2], anc_sym["foot_mid_right"][2], places=5)

        # Idle drives every leg with scale + rotation + translation, and every keyframe keeps the
        # sole exactly on Y = 0.0 while the dip compresses the leg (knee bends) and the lift extends it.
        clip = next(c for c in sc.animations if c.name == "Idle")
        by_node: dict = {}
        for ch in clip.channels:
            if ch.target_node in leg_rig.LEG_NODES:
                by_node.setdefault(ch.target_node, {})[ch.path] = ch
        self.assertEqual(set(by_node), set(leg_rig.LEG_NODES))
        for node, chans in by_node.items():
            self.assertEqual(set(chans), {"scale", "rotation", "translation"})
            local = np.vstack([p.positions for p in sc.get_node(node)]).astype(np.float64)
            sys_ = [v[1] for v in chans["scale"].values]
            self.assertLess(min(sys_), 0.985, node)
            self.assertGreater(max(sys_), 1.015, node)
            for k in range(len(chans["scale"].times)):
                world = leg_rig._quat_rotate(chans["rotation"].values[k], local * np.array(chans["scale"].values[k]))
                world = world + np.array(chans["translation"].values[k])
                self.assertAlmostEqual(float(world[:, 1].min()), 0.0, delta=1e-5, msg=f"{node} key {k}")
        # Verify non-lockstep per-leg variation: all 4 legs have distinct rotation & scale tracks.
        leg_scale_tracks = {n: np.asarray(ch["scale"].values, dtype=np.float64) for n, ch in by_node.items()}
        leg_rot_tracks = {n: np.asarray(ch["rotation"].values, dtype=np.float64) for n, ch in by_node.items()}
        names_list = list(leg_rig.LEG_NODES)
        for i in range(len(names_list)):
            for j in range(i + 1, len(names_list)):
                na, nb = names_list[i], names_list[j]
                self.assertGreater(
                    float(np.abs(leg_rot_tracks[na] - leg_rot_tracks[nb]).max()),
                    0.02,
                    f"{na} and {nb} rotations are moving in lockstep",
                )
                self.assertGreater(
                    float(np.abs(leg_scale_tracks[na] - leg_scale_tracks[nb]).max()),
                    0.01,
                    f"{na} and {nb} scales are moving in lockstep",
                )

        st = gltf_writer.validate_glb(gltf_writer.scene_to_glb_bytes(sc))
        self.assertAlmostEqual(st["bbox_min"][1], 0.0, delta=1e-4)
        self.assertIn("legs", st["nodes"])

        # Public helper for custom idles: per-key offsets in, matching leg channels out.
        times = [0.0, 0.5, 1.0]
        chans = base_ant.add_leg_flex_channels(sym, times, [0.0, -0.015, 0.0], upper_dxs=[0.0, 0.01, 0.0])
        self.assertEqual(len(chans), 3 * len(leg_rig.LEG_NODES))
        self.assertTrue(all(len(ch.values) == 3 for ch in chans))
        with self.assertRaises(ValueError):
            base_ant.add_leg_flex_channels(sym, times, [0.0, 0.1])

    def test_defined_elbows_and_articulated_arm_idle(self):
        from tools.ant_models import leg_rig

        sc, anc = base_ant.build_base_ant("arm_rig_test", stance_asymmetry=0.0)
        for key in ("shoulder_left", "shoulder_right", "elbow_left", "elbow_right", "left_hand", "right_hand"):
            self.assertIn(key, anc)

        # Articulated hierarchy: upper_body -> arms -> arm_left/right -> forearm_left/right.
        self.assertEqual(sc.get_node_parent("arms"), "upper_body")
        for tag in ("left", "right"):
            self.assertEqual(sc.get_node_parent(f"arm_{tag}"), "arms")
            self.assertEqual(sc.get_node_parent(f"forearm_{tag}"), f"arm_{tag}")
            u_names = {p.name for p in sc.get_node(f"arm_{tag}")}
            f_names = {p.name for p in sc.get_node(f"forearm_{tag}")}
            self.assertIn(f"arm_upper_{tag}", u_names)
            self.assertIn(f"elbow_cap_{tag}", u_names)
            self.assertIn(f"elbow_joint_{tag}", u_names)
            self.assertIn(f"forearm_{tag}", f_names)
            self.assertIn(f"hand_claw_inner_{tag}", f_names)
            self.assertIn(f"hand_claw_outer_{tag}", f_names)
            el_world = sc.get_world_matrix(f"forearm_{tag}")[:3, 3]
            self.assertTrue(np.allclose(el_world, anc[f"elbow_{tag}"], atol=1e-4))
            for part in sc.get_node(f"arm_{tag}") + sc.get_node(f"forearm_{tag}"):
                self.assertGreater(mesh_core.signed_volume(part.positions, part.indices), 1e-9, part.name)

        # Default Idle includes asymmetric, out-of-phase shoulder & forearm rotation tracks on all 4 arm nodes.
        clip = next(c for c in sc.animations if c.name == "Idle")
        arm_chs = {ch.target_node: ch for ch in clip.channels if ch.target_node in leg_rig.ARM_NODES}
        self.assertEqual(set(arm_chs), set(leg_rig.ARM_NODES))
        fl = np.asarray(arm_chs["forearm_left"].values, dtype=np.float64)
        fr = np.asarray(arm_chs["forearm_right"].values, dtype=np.float64)
        self.assertGreater(float(np.abs(fl[:, 0] - fr[:, 0]).max()), 0.04, "left/right forearms should move out of phase")

        # Selective sync_arm_idle_to_clip(right_scale=0.0) keeps right arm fixed for tool-holding classes.
        left_only = base_ant.sync_arm_idle_to_clip(sc, "Idle", left_scale=1.0, right_scale=0.0)
        self.assertEqual({ch.target_node for ch in left_only}, {"arm_left", "forearm_left"})
        gltf_writer.validate_glb(gltf_writer.scene_to_glb_bytes(sc))

    def test_leg_rig_tracks_true_medial_hip_under_crouched_sway_and_heave(self):
        from tools.ant_models import leg_rig

        # Crouched Thief-scale stance where the forward knee sits higher than the medial hip socket.
        sc, _ = base_ant.build_base_ant("hip_track_stress", crouch=0.132, leg_style="thief", include_idle=False)
        info = leg_rig.leg_rig_info(sc)
        self.assertEqual(len(info), 4)
        for node, pivot, hip in info:
            # True medial hip must sit deep near centerline (|x| < 0.04) and below knee peak (~0.25).
            self.assertLess(abs(float(hip[0])), 0.04, f"{node} grabbed knee instead of medial hip: {hip}")
            self.assertLess(float(hip[1]), 0.22, f"{node} hip_y too high (grabbed knee): {hip}")

        # Stress test: Thief-scale +-0.050 sideways shift + +0.040 vertical heave + body roll/pitch.
        xf_ub = sc.get_node_transform("upper_body")
        bx, by, bz = xf_ub.translation
        r0_ub = xf_ub.rotation
        times = [0.0, 0.4, 0.8, 1.2, 1.6, 2.0, 2.4]
        t_vals = [
            (bx, by, bz),
            (bx + 0.050, by - 0.025, bz + 0.015),
            (bx + 0.020, by + 0.040, bz - 0.010),
            (bx, by, bz),
            (bx - 0.050, by - 0.025, bz + 0.015),
            (bx - 0.020, by + 0.040, bz - 0.010),
            (bx, by, bz),
        ]
        r_vals = [
            r0_ub,
            mesh_core.quat_mul(r0_ub, mesh_core.quat_from_euler_xyz(rx=0.06, rz=-0.10)),
            mesh_core.quat_mul(r0_ub, mesh_core.quat_from_euler_xyz(rx=-0.05, rz=-0.04)),
            r0_ub,
            mesh_core.quat_mul(r0_ub, mesh_core.quat_from_euler_xyz(rx=0.06, rz=0.10)),
            mesh_core.quat_mul(r0_ub, mesh_core.quat_from_euler_xyz(rx=-0.05, rz=0.04)),
            r0_ub,
        ]
        sc.add_animation(
            "Idle",
            [
                mesh_core.AnimationChannel("upper_body", "translation", times, t_vals),
                mesh_core.AnimationChannel("upper_body", "rotation", times, r_vals),
            ],
            replace=True,
        )
        leg_rig.sync_leg_flex_to_clip(sc, "Idle")
        clip = next(c for c in sc.animations if c.name == "Idle")
        t_ch = next(ch for ch in clip.channels if ch.target_node == "upper_body" and ch.path == "translation")
        r_ch = next(ch for ch in clip.channels if ch.target_node == "upper_body" and ch.path == "rotation")
        by_node: dict = {}
        for ch in clip.channels:
            if ch.target_node in leg_rig.LEG_NODES:
                by_node.setdefault(ch.target_node, {})[ch.path] = ch
        self.assertEqual(set(by_node), set(leg_rig.LEG_NODES))

        t0_ub = np.asarray(xf_ub.translation, dtype=np.float64)
        for node, pivot, hip in info:
            chans = by_node[node]
            xf = sc.get_node_transform(node)
            r0 = xf.rotation if xf is not None else (0.0, 0.0, 0.0, 1.0)
            t0 = np.asarray(xf.translation if xf is not None else pivot, dtype=np.float64)
            s0 = np.asarray(xf.scale if xf is not None else (1.0, 1.0, 1.0), dtype=np.float64)
            local = np.vstack([p.positions for p in sc.get_node(node)]).astype(np.float64)
            hip_loc = leg_rig._quat_rotate(leg_rig._quat_conj(r0), hip - t0) / s0
            ub_hip_loc = leg_rig._quat_rotate(leg_rig._quat_conj(r0_ub), hip - t0_ub)
            # Verify across BOTH exact keyframes and dense interpolated mid-frames.
            for t_val in np.linspace(0.0, times[-1], 49):
                tt_ub = np.asarray(leg_rig._sample(t_ch.times, t_ch.values, float(t_val), False), dtype=np.float64)
                qq_ub = leg_rig._sample(r_ch.times, r_ch.values, float(t_val), True)
                h_exp = tt_ub + leg_rig._quat_rotate(qq_ub, ub_hip_loc)
                s_v = np.asarray(leg_rig._sample(chans["scale"].times, chans["scale"].values, float(t_val), False), dtype=np.float64)
                q_v = leg_rig._sample(chans["rotation"].times, chans["rotation"].values, float(t_val), True)
                t_v = np.asarray(leg_rig._sample(chans["translation"].times, chans["translation"].values, float(t_val), False), dtype=np.float64)
                h_got = t_v + leg_rig._quat_rotate(q_v, hip_loc * s_v)
                world_verts = t_v + leg_rig._quat_rotate(q_v, local * s_v)
                self.assertLess(
                    float(np.linalg.norm(h_got - h_exp)),
                    0.008,
                    f"{node} at t={t_val:.3f}: hip drifted {np.linalg.norm(h_got - h_exp):.5f}",
                )
                self.assertLess(
                    abs(float(world_verts[:, 1].min())),
                    0.003,
                    f"{node} at t={t_val:.3f}: sole lifted/sank {world_verts[:, 1].min():.5f}",
                )
        gltf_writer.validate_glb(gltf_writer.scene_to_glb_bytes(sc))

    def test_base_ant_character_expression_api(self):
        # All brow styles build cleanly; "none" omits brow caps and invalid styles raise ValueError.
        for style in base_ant.BROW_STYLES:
            sc, _ = base_ant.build_base_ant(f"brow_{style}", brow_style=style)
            has_brow = any(p.name.startswith("brow_cap_") for p in sc.get_node("eyes"))
            self.assertEqual(has_brow, style != "none", style)
            gltf_writer.validate_glb(gltf_writer.scene_to_glb_bytes(sc))
        with self.assertRaises(ValueError):
            base_ant.build_base_ant("bad_brow", brow_style="grumpy_invalid")

        # head_tilt_deg, head_yaw_deg, eye_look, antenna_asymmetry, and stance_asymmetry compose cleanly.
        sc_expr, anc_expr = base_ant.build_base_ant(
            "ant_expressive",
            brow_style="eager",
            head_tilt_deg=5.0,
            head_yaw_deg=-4.5,
            eye_look=(-0.12, 0.09),
            antenna_asymmetry=0.35,
            stance_asymmetry=0.45,
        )
        self.assertGreater(anc_expr["left_hand"][1], anc_expr["right_hand"][1] + 0.01)
        head_xf = sc_expr.get_node_transform("head_group")
        body_xf = sc_expr.get_node_transform("upper_body")
        self.assertIsNotNone(head_xf)
        self.assertIsNotNone(body_xf)
        self.assertGreater(abs(head_xf.rotation[1]) + abs(head_xf.rotation[2]), 0.04)
        self.assertGreater(abs(body_xf.rotation[1]) + abs(body_xf.rotation[2]), 0.01)

        # Asymmetric antennae differ in peak height; antenna_asymmetry=0.0 mirrors symmetrically.
        ant_l_y = max(float(p.bounds()[1][1]) for p in sc_expr.world_parts() if p.name == "antenna_stalk_left")
        ant_r_y = max(float(p.bounds()[1][1]) for p in sc_expr.world_parts() if p.name == "antenna_stalk_right")
        self.assertGreater(abs(ant_l_y - ant_r_y), 0.01)

        sc_sym, _ = base_ant.build_base_ant("ant_sym_ant", antenna_asymmetry=0.0)
        sym_l_y = max(float(p.bounds()[1][1]) for p in sc_sym.world_parts() if p.name == "antenna_stalk_left")
        sym_r_y = max(float(p.bounds()[1][1]) for p in sc_sym.world_parts() if p.name == "antenna_stalk_right")
        self.assertAlmostEqual(sym_l_y, sym_r_y, places=4)

        # attach_world_node preserves exact world bounds on rotated head_group (preserve_world_pose=True)
        # and tilts canonical gear in lockstep when preserve_world_pose=False.
        hat = mesh_core.sphere(0.06, name="expr_hat", material="gear_gold").translate(0.0, anc_expr["head_top"][1] + 0.04, 0.02)
        sc_expr.add("hat_world", hat.copy("hat_world"))
        b_before = sc_expr.bounds()
        sc_expr.attach_world_node("hat_world", "head_group", preserve_world_pose=True)
        b_after = sc_expr.bounds()
        self.assertTrue(np.allclose(b_before[0], b_after[0], atol=1e-4))
        self.assertTrue(np.allclose(b_before[1], b_after[1], atol=1e-4))

        sc_expr.add("hat_canon", hat.copy("hat_canon"))
        sc_expr.attach_world_node("hat_canon", "head_group", preserve_world_pose=False)
        hat_canon_world = next(p for p in sc_expr.world_parts() if p.name == "hat_canon")
        self.assertGreater(abs(float(hat_canon_world.positions[:, 0].mean())), 0.015)

        st_expr = gltf_writer.validate_glb(gltf_writer.scene_to_glb_bytes(sc_expr))
        self.assertAlmostEqual(st_expr["bbox_min"][1], 0.0, delta=1e-3)
        self.assertIn("Idle", st_expr["animations"])
        self.assertIn("Blink", st_expr["animations"])

        # Default chin-up head_pitch_deg (-16.0) lifts pupils & mandibles UP (+Y) toward the 45 deg
        # overhead camera and tilts cranium back (-Z) compared to neutral head_pitch_deg=0.0.
        sc_flat, _ = base_ant.build_base_ant("pitch_flat", head_pitch_deg=0.0)
        sc_up, _ = base_ant.build_base_ant("pitch_up")
        wp_flat, wp_up = sc_flat.world_parts(), sc_up.world_parts()
        eye_y_flat = float(np.mean([p.positions[:, 1].mean() for p in wp_flat if p.name.startswith("pupil_")]))
        eye_y_up = float(np.mean([p.positions[:, 1].mean() for p in wp_up if p.name.startswith("pupil_")]))
        jaw_y_flat = float(np.mean([p.positions[:, 1].mean() for p in wp_flat if p.name.startswith("mandible_claw")]))
        jaw_y_up = float(np.mean([p.positions[:, 1].mean() for p in wp_up if p.name.startswith("mandible_claw")]))
        cran_z_flat = float(next(p for p in wp_flat if p.name == "cranium").positions[:, 2].mean())
        cran_z_up = float(next(p for p in wp_up if p.name == "cranium").positions[:, 2].mean())
        self.assertGreater(eye_y_up, eye_y_flat + 0.02, "negative head_pitch_deg must lift eyes UP toward camera")
        self.assertGreater(jaw_y_up, jaw_y_flat + 0.02, "negative head_pitch_deg must lift mandibles UP")
        self.assertLess(cran_z_up, cran_z_flat - 0.02, "negative head_pitch_deg must tilt skull crown gently back")

    def test_base_ant_variants_and_arm_pose_overrides(self):
        _, a_idle = base_ant.build_base_ant("idle")
        s_tool, a_tool = base_ant.build_base_ant("swimmer_base", right_arm_pose="hold_tool")
        s_high, a_high = base_ant.build_base_ant("fire_base", include_antennae=False, right_arm_pose="hold_high")
        s_custom, a_custom = base_ant.build_base_ant(
            "custom_base",
            left_arm_pose=[(0.25, 0.45, 0.12), (0.30, 0.55, 0.28)],
        )

        self.assertGreater(a_tool["right_hand"][1], a_idle["right_hand"][1] + 0.05)
        self.assertGreater(a_high["right_hand"][1], a_tool["right_hand"][1] + 0.10)
        self.assertNotIn("antennae", s_high.node_names())
        self.assertAlmostEqual(a_custom["left_hand"][1], 0.55, delta=0.01)

        for sc in (s_tool, s_high, s_custom):
            st = gltf_writer.validate_glb(gltf_writer.scene_to_glb_bytes(sc))
            self.assertAlmostEqual(st["bbox_min"][1], 0.0, delta=1e-3)

    def test_build_all_and_every_present_ant_class(self):
        with tempfile.TemporaryDirectory() as tmp:
            manifest, built = build_all.build_all(out_dir=tmp, include_base=True, quiet=True)
            manifest_path = os.path.join(tmp, "manifest.json")
            self.assertTrue(os.path.isfile(manifest_path))
            with open(manifest_path, encoding="utf-8") as f:
                loaded = json.load(f)
            self.assertEqual(loaded, manifest)
            self.assertGreaterEqual(len(built), 1)

            for entry in manifest["models"]:
                glb_path = os.path.join(tmp, entry["file"])
                self.assertTrue(os.path.isfile(glb_path))
                stats = gltf_writer.validate_glb(glb_path)
                self.assertEqual(stats["triangles"], entry["triangles"])
                self.assertAlmostEqual(stats["bbox_min"][1], 0.0, delta=0.02, msg=entry["id"])
                if entry["id"] == "combat":
                    self.assertGreaterEqual(stats["dimensions"][0], 0.90)
                    self.assertGreaterEqual(stats["bbox_max"][1], 1.05)
                else:
                    self.assertTrue(0.64 <= stats["bbox_max"][1] <= 1.45, f"{entry['id']} height {stats['bbox_max'][1]}")

    def test_build_all_cli_out_alias_and_comma_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            rc = build_all.main(["--only", "base,worker", "--out", tmp, "--quiet"])
            self.assertEqual(rc, 0)
            self.assertTrue(os.path.isfile(os.path.join(tmp, "base.glb")))
            manifest_path = os.path.join(tmp, "manifest.json")
            self.assertTrue(os.path.isfile(manifest_path))
            with open(manifest_path, encoding="utf-8") as f:
                m1 = json.load(f)
            self.assertRegex(m1["version"], r"^v-[0-9a-f]{8}$")
            self.assertRegex(m1["build_hash"], r"^[0-9a-f]{16}$")
            self.assertRegex(m1["models_hash"], r"^[0-9a-f]{16}$")
            self.assertEqual(m1["version"], "v-" + m1["build_hash"][:8])
            for entry in m1["models"]:
                self.assertRegex(entry["sha256"], r"^[0-9a-f]{12}$")

            # Seed an untouched fake model entry and confirm `--only base` preserves it.
            fake_glb = os.path.join(tmp, "custom.glb")
            with open(fake_glb, "wb") as f:
                f.write(b"fake-glb-bytes")
            m1["models"].append({"id": "custom", "file": "custom.glb", "sha256": "deadbeef1234"})
            with open(manifest_path, "w", encoding="utf-8") as f:
                json.dump(m1, f)

            m2, _ = build_all.build_all(out_dir=tmp, only=["base"], quiet=True)
            ids = {e["id"] for e in m2["models"]}
            self.assertIn("base", ids)
            self.assertIn("custom", ids)
            self.assertNotEqual(m2["models_hash"], m1["models_hash"])
            self.assertEqual(m2["version"], "v-" + m2["build_hash"][:8])

            # Dynamic viewer file discovery hashes all viewer*/compare* .html/.css/.js files present.
            viewer_files = build_all._discover_viewer_files()
            for fname in viewer_files:
                self.assertTrue(fname.startswith(("viewer", "compare")))
                self.assertTrue(fname.endswith((".html", ".css", ".js")))
            if os.path.isfile(os.path.join(build_all.THIS_DIR, "viewer.html")):
                self.assertIn("viewer.html", viewer_files)


@unittest.skipUnless(HAS_PIL, "Pillow is needed")
class OrigSpritesBundleTests(unittest.TestCase):
    def test_orig_sprites_bundle_covers_all_classes_angles_and_teams(self):
        import base64
        import io
        from tools.ant_models import orig_sprites

        if not orig_sprites.can_generate_orig_sprites():
            self.skipTest("Original-Ants/ants.chd or chd_table4_animations.json not present")

        with tempfile.TemporaryDirectory() as tmp:
            p1 = os.path.join(tmp, "orig_sprites_1.json")
            p2 = os.path.join(tmp, "orig_sprites_2.json")
            bundle = orig_sprites.write_orig_sprites(p1)
            orig_sprites.write_orig_sprites(p2)
            with open(p1, "rb") as f1, open(p2, "rb") as f2:
                self.assertEqual(f1.read(), f2.read(), "orig_sprites.json must be byte-deterministic")

        self.assertEqual(bundle["schema_version"], 1)
        self.assertEqual(tuple(bundle["classes"]), orig_sprites.ANT_CLASSES)
        self.assertEqual(tuple(bundle["angles"]), orig_sprites.CANONICAL_ANGLES)
        self.assertEqual(tuple(bundle["teams"]), orig_sprites.ORIGINAL_TEAMS)

        sheet_count = 0
        for cls in orig_sprites.ANT_CLASSES:
            self.assertIn(cls, bundle["sprites"])
            for angle in orig_sprites.CANONICAL_ANGLES:
                spec = bundle["sprites"][cls][angle]
                self.assertGreater(spec["fw"], 0)
                self.assertGreater(spec["fh"], 0)
                self.assertTrue(0 < spec["foot_y"] <= spec["fh"])
                self.assertTrue(0 < spec["vis_h"] <= spec["fh"])
                self.assertGreater(spec["total_ms"], 0)
                self.assertGreaterEqual(spec["frame_count"], 1)
                self.assertGreaterEqual(len(spec["steps"]), 1)
                for step in spec["steps"]:
                    self.assertTrue(0 <= step["f"] < spec["frame_count"])
                    self.assertGreater(step["ms"], 0)
                for team in orig_sprites.ORIGINAL_TEAMS:
                    uri = spec["sheets"][team]
                    self.assertTrue(uri.startswith("data:image/png;base64,"))
                    sheet_count += 1
        self.assertEqual(sheet_count, 6 * 5 * 4)

        # Spot-check PNG decode dimensions on worker 301 green & combat 301 red.
        for cls, ang, team in (("worker", "301", "green"), ("combat", "301", "red"), ("fire", "901", "blue")):
            spec = bundle["sprites"][cls][ang]
            raw = base64.b64decode(spec["sheets"][team].split(",", 1)[1])
            im = Image.open(io.BytesIO(raw)).convert("RGBA")
            self.assertEqual(im.size, (spec["fw"] * spec["frame_count"], spec["fh"]))


@unittest.skipUnless(HAS_NUMPY and HAS_PIL, "numpy and Pillow are needed")
class SoftwarePreviewRenderer(unittest.TestCase):
    def test_renders_non_blank_contact_sheet(self):
        scene, _ = base_ant.build_base_ant("ant_preview")
        with tempfile.TemporaryDirectory() as tmp:
            out_png = os.path.join(tmp, "sheet.png")
            angles = (("Hero", -28.0, 22.0), ("301 S", 0.0, 50.0))
            build_all.render_preview_sheet([("base", scene)], out_png, team="red", tile_size=64, angles=angles)
            self.assertTrue(os.path.isfile(out_png))
            img = Image.open(out_png).convert("RGB")
            self.assertEqual(img.size, (128, 64 + 26))
            arr = np.asarray(img)
            # Ant pixels should introduce real luminance & color variance beyond flat backdrop.
            self.assertGreater(float(arr.std()), 12.0)

    def test_translucent_glass_reveals_opaque_eye_behind(self):
        sc = mesh_core.SceneBuilder("glass_test")
        sc.add("eye", mesh_core.sphere(0.35, u_segs=16, v_segs=12, name="eye", material="eye_white").translate(0.0, 0.5, 0.0))
        sc.add("lens", mesh_core.rounded_box(0.8, 0.8, 0.04, bevel=0.01, segs=2, name="lens", material="glass_tinted").translate(0.0, 0.5, 0.45))
        img = build_all.render_scene_orthographic(sc, width=64, height=64, yaw_deg=0.0, pitch_deg=0.0, view_span=1.2)
        center_luma = float(img[30:34, 30:34].mean())
        edge_luma = float(img[6:10, 6:10].mean())
        # Bright white eye behind dark tinted glass must shine noticeably brighter than glass-only edges.
        self.assertGreater(center_luma, edge_luma + 35.0)


class All1998ClipsAndViewerTests(unittest.TestCase):
    """Regression coverage for all 88 1998 glTF clips, hidden FX props, CI stdlib guards, and viewer JS."""

    _EXPECTED_CLIPS = {
        "worker": {
            "Blink": 3.00, "Idle": 1.65, "Attack": 0.42, "Hit": 0.80,
            "Walk": 0.60, "Harvest": 0.42, "CantGo": 0.36, "Enter": 1.00, "Hatch": 0.52,
            "BlownBack": 0.90, "BlownUp": 1.15, "Stunned": 3.125, "Drown": 2.37,
        },
        "combat": {
            "Blink": 3.00, "Idle": 7.82, "Attack": 0.54, "Hit": 0.88,
            "Walk": 0.60, "Harvest": 0.36, "CantGo": 0.62, "Enter": 1.20, "Hatch": 0.64,
            "BlownBack": 0.98, "BlownUp": 1.165, "Stunned": 3.00, "Drown": 2.37,
        },
        "thief": {
            "Blink": 3.00, "IdleLookout": 2.52, "Idle": 2.52, "Attack": 0.72, "Hit": 0.95,
            "Walk": 0.60, "Harvest": 0.34, "CantGo": 0.89, "Enter": 1.00, "Hatch": 0.52,
            "BlownBack": 0.91, "BlownUp": 1.15, "Stunned": 2.61, "Drown": 2.37, "Steal": 3.51,
        },
        "bomber": {
            "Blink": 3.00, "Idle": 1.70, "Attack": 0.36, "Hit": 1.00,
            "Walk": 0.60, "Harvest": 0.44, "CantGo": 1.38, "Enter": 1.24, "Hatch": 0.64,
            "BlownBack": 1.06, "BlownUp": 1.61, "Stunned": 3.835, "Drown": 2.38,
            "PlantBomb": 1.36, "DefuseBomb": 1.10,
        },
        "swimmer": {
            "Blink": 3.00, "Idle": 1.80, "Attack": 0.76, "Hit": 1.05,
            "Walk": 0.60, "Harvest": 0.32, "CantGo": 0.96, "Enter": 0.90, "Hatch": 0.48,
            "BlownBack": 1.15, "BlownUp": 1.05, "Stunned": 2.88,
            "Swim": 0.48, "Dive": 1.04, "Surface": 0.42, "Shovel": 0.48, "ShovelWater": 0.50,
        },
        "fire": {
            "Blink": 3.00, "Idle": 0.60, "Attack": 0.36, "Hit": 1.00,
            "Walk": 0.60, "Harvest": 0.40, "CantGo": 0.66, "Enter": 0.50, "Hatch": 0.30,
            "BlownBack": 1.10, "BlownUp": 1.33, "Stunned": 3.00, "Drown": 2.37,
            "StartFire": 1.76, "Extinguish": 1.20,
        },
    }

    _EXPECTED_HIDDEN_FX = {
        "worker": ("stun_stars", "drown_splash"),
        "combat": ("stun_stars", "drown_splash"),
        "thief": (
            "stun_stars", "drown_splash", "thief_loot_crumb",
            "thief_whip_lasso", "thief_whip_crack", "thief_whip_recoil",
        ),
        "bomber": ("stun_stars", "drown_splash", "bomber_held_bomb", "bomber_defuse_puff"),
        "swimmer": (
            "stun_stars", "swimmer_splash_ring", "swimmer_dirt_toss", "swimmer_water_toss",
            "snorkel_water_glob", "snorkel_water_spray_a", "snorkel_water_spray_b",
        ),
        "fire": ("stun_stars", "drown_splash", "fire_mag_glass", "fire_sunbeam", "fire_spark", "fire_extinguisher", "fire_foam"),
    }

    _SCENES_CACHE = None

    @classmethod
    def _build_all_six(cls):
        if cls._SCENES_CACHE is None:
            from tools.ant_models import ant_bomber, ant_combat, ant_fire, ant_swimmer, ant_thief, ant_worker

            cls._SCENES_CACHE = {
                "worker": ant_worker.build_ant(),
                "combat": ant_combat.build_ant(),
                "thief": ant_thief.build_ant(),
                "bomber": ant_bomber.build_ant(),
                "swimmer": ant_swimmer.build_ant(),
                "fire": ant_fire.build_ant(),
            }
        return cls._SCENES_CACHE

    @unittest.skipUnless(HAS_NUMPY, "numpy is needed")
    def test_all_classes_export_complete_1998_clips_and_exact_durations(self):
        scenes = self._build_all_six()
        total_clips = 0
        for cls_name, expected in self._EXPECTED_CLIPS.items():
            sc = scenes[cls_name]
            by_name = {clip.name: clip for clip in sc.animations}
            self.assertEqual(
                set(by_name.keys()),
                set(expected.keys()),
                f"{cls_name}: animation set mismatch",
            )
            if cls_name == "swimmer":
                self.assertNotIn("Drown", by_name)
            total_clips += len(by_name)
            for clip_name, exp_dur in expected.items():
                clip = by_name[clip_name]
                self.assertGreater(len(clip.channels), 0, f"{cls_name}.{clip_name} has no channels")
                clip_dur = 0.0
                for ch in clip.channels:
                    self.assertGreater(len(ch.times), 0, f"{cls_name}.{clip_name}.{ch.target_node} empty times")
                    self.assertEqual(len(ch.times), len(ch.values))
                    prev_t = -1.0
                    for t_val, vec in zip(ch.times, ch.values):
                        self.assertTrue(math.isfinite(t_val) and t_val >= 0.0, f"{cls_name}.{clip_name} bad t={t_val}")
                        self.assertGreaterEqual(t_val, prev_t, f"{cls_name}.{clip_name} non-monotonic t")
                        prev_t = t_val
                        for comp in vec:
                            self.assertTrue(math.isfinite(comp), f"{cls_name}.{clip_name} non-finite value")
                    clip_dur = max(clip_dur, float(ch.times[-1]))
                self.assertAlmostEqual(
                    clip_dur,
                    exp_dur,
                    delta=0.02,
                    msg=f"{cls_name}.{clip_name} duration {clip_dur:.4f}s != CHD {exp_dur:.4f}s",
                )
        self.assertEqual(total_clips, 88)

    @unittest.skipUnless(HAS_NUMPY, "numpy is needed")
    def test_hidden_fx_props_stay_hidden_at_rest_and_ground_min_y_preserved(self):
        scenes = self._build_all_six()
        for cls_name, expected_fx in self._EXPECTED_HIDDEN_FX.items():
            sc = scenes[cls_name]
            for fx_node in expected_fx:
                self.assertIn(fx_node, sc.nodes, f"{cls_name} missing hidden FX node {fx_node!r}")
                xf = sc.get_node_transform(fx_node)
                self.assertIsNotNone(xf, f"{cls_name}.{fx_node} missing rest transform")
                max_scale = max(abs(float(c)) for c in xf.scale)
                self.assertLessEqual(
                    max_scale,
                    1e-3,
                    f"{cls_name}.{fx_node} must start hidden at rest (max scale {max_scale} > 1e-3)",
                )
            lo, hi = sc.bounds()
            self.assertAlmostEqual(float(lo[1]), 0.0, delta=1e-3, msg=f"{cls_name} rest min_y drifted: {lo[1]}")
            for axis in range(3):
                self.assertTrue(math.isfinite(float(lo[axis])) and math.isfinite(float(hi[axis])))
                self.assertGreater(float(hi[axis] - lo[axis]), 0.1, f"{cls_name} degenerate rest axis {axis}")

    def test_wave_clip_modules_and_orig_sprites_import_without_numpy_or_pil(self):
        # Verify CI pure-stdlib safety in an isolated Python process blocking numpy and PIL.
        modules = (
            "tools.ant_models.shared_clips_loco",
            "tools.ant_models.shared_clips_hazards",
            "tools.ant_models.ability_clips_special",
            "tools.ant_models.ability_clips_swimmer",
            "tools.ant_models.ability_clips_fire",
            "tools.ant_models.hit_clips",
            "tools.ant_models.hit_clips_special",
            "tools.ant_models.thief_attack",
            "tools.ant_models.orig_sprites",
        )
        script = (
            "import builtins, importlib, sys\n"
            f"sys.path.insert(0, {REPO!r})\n"
            "_orig = builtins.__import__\n"
            "def _blocked(name, *a, **kw):\n"
            "    if name in ('numpy', 'PIL') or name.startswith(('numpy.', 'PIL.')):\n"
            "        raise ImportError(name)\n"
            "    return _orig(name, *a, **kw)\n"
            "builtins.__import__ = _blocked\n"
            f"for mod_name in {modules!r}:\n"
            "    m = importlib.import_module(mod_name)\n"
            "    assert m is not None\n"
        )
        proc = subprocess.run([sys.executable, "-c", script], capture_output=True, text=True, check=False)
        self.assertEqual(proc.returncode, 0, f"Pure-stdlib import failed:\n{proc.stderr}")

    def test_extra_sprite_specs_match_viewer_actions_registry_and_js_syntax(self):
        from tools.ant_models import orig_sprites

        shared_extra_ids = {spec[0] for spec in orig_sprites.EXTRA_SHARED_SPECS}
        class_extra_by_id = {}
        for cls_name, specs in orig_sprites.EXTRA_CLASS_SPECS.items():
            for spec in specs:
                class_extra_by_id.setdefault(spec[0], []).append(cls_name)
        all_orig_ids = set(orig_sprites.ACTIONS) | shared_extra_ids | set(class_extra_by_id.keys())
        self.assertEqual(len(all_orig_ids), 22)

        js_dir = os.path.join(REPO, "tools", "ant_models")
        js_files = sorted(glob.glob(os.path.join(js_dir, "*.js")))
        self.assertTrue(js_files, f"Expected viewer JS files under {js_dir}")

        node = shutil.which("node")
        if node is None:
            self.skipTest("node is not installed; skipping viewer_actions.js execution and node --check")

        for js_path in js_files:
            chk = subprocess.run([node, "--check", js_path], capture_output=True, text=True, check=False)
            self.assertEqual(chk.returncode, 0, f"node --check failed on {os.path.basename(js_path)}:\n{chk.stderr}")

        # Execute viewer_actions.js in a minimal Node sandbox to cross-check live registry behavior.
        actions_js = os.path.join(js_dir, "viewer_actions.js")
        probe = (
            "const fs = require('fs'), vm = require('vm');\n"
            "const sandbox = { window: {} };\n"
            f"vm.runInNewContext(fs.readFileSync({actions_js!r}, 'utf8'), sandbox);\n"
            "const reg = sandbox.window.__antActions;\n"
            "const near = reg.nearestAvailableAngle('201', ['301', '901', '701']);\n"
            "console.log(JSON.stringify({ actions: reg.ACTIONS, quick: reg.QUICK_ACTIONS, near }));\n"
        )
        run = subprocess.run([node, "-e", probe], capture_output=True, text=True, check=False)
        self.assertEqual(run.returncode, 0, f"viewer_actions.js execution failed:\n{run.stderr}")
        payload = json.loads(run.stdout)
        viewer_actions = payload["actions"]
        viewer_ids = {a["id"] for a in viewer_actions}
        self.assertEqual(viewer_ids, all_orig_ids)
        self.assertEqual(payload["quick"], ["idle", "walk", "attack", "hit"])
        self.assertIn(payload["near"]["angle"], ("301", "901"))
        self.assertFalse(payload["near"]["exact"])

        by_id = {a["id"]: a for a in viewer_actions}
        self.assertNotIn("swimmer", by_id["drown"]["classes"])
        for aid, classes in class_extra_by_id.items():
            self.assertEqual(sorted(by_id[aid]["classes"]), sorted(classes), f"Class list mismatch for {aid!r}")
        for aid in shared_extra_ids:
            self.assertTrue(by_id[aid].get("extra"), f"Shared extra action {aid!r} must set extra=true")

    def test_orig_sprites_catalog_js_fallback_matches_docs_table(self):
        from pathlib import Path
        from tools.ant_models import orig_sprites

        from_docs = orig_sprites._load_animations_by_name(orig_sprites.DEFAULT_ANIM_JSON)
        from_catalog = orig_sprites._load_animations_from_catalog_js(orig_sprites.DEFAULT_CATALOG_JS)
        self.assertEqual(len(from_catalog), len(from_docs))

        # Every ant animation across all 6 classes (idle/attack/hit + all extra actions) must match identically.
        checked = 0
        for cls_name, letter in orig_sprites.CLASS_LETTERS.items():
            prefixes = [
                (f"a{letter}st", [(a, a) for a in orig_sprites.CANONICAL_ANGLES]),
                (f"a{letter}at", [(a, a) for a in orig_sprites.CANONICAL_ANGLES]),
                (f"a{letter}gh", [(a, a) for a in orig_sprites.CANONICAL_ANGLES]),
            ]
            for _, suf, pairs in orig_sprites.EXTRA_SHARED_SPECS:
                prefixes.append((f"a{letter}{suf}", list(pairs)))
            for _, pref, pairs in orig_sprites.EXTRA_CLASS_SPECS.get(cls_name, ()):
                prefixes.append((pref, list(pairs)))
            for pref, pairs in prefixes:
                for _, suf in pairs:
                    name = f"{pref}{suf}"
                    if name not in from_docs:
                        self.assertNotIn(name, from_catalog)
                        continue
                    self.assertIn(name, from_catalog, name)
                    d_entry, c_entry = from_docs[name], from_catalog[name]
                    self.assertEqual(c_entry["total_duration_ms"], d_entry["total_duration_ms"], name)
                    d_subs = [(s["duration_ms"], [(f["sprite_index"], f["dx"], f["dy"]) for f in s["frames"]]) for s in d_entry["subitems"]]
                    c_subs = [(s["duration_ms"], [(f["sprite_index"], f["dx"], f["dy"]) for f in s["frames"]]) for s in c_entry["subitems"]]
                    self.assertEqual(c_subs, d_subs, name)
                    checked += 1
        self.assertGreaterEqual(checked, 200)

        # Fallback activates automatically when DEFAULT_ANIM_JSON is absent (Docker / web_without_docker).
        missing = Path("/nonexistent_docs_chd_table4_animations.json")
        old_default = orig_sprites.DEFAULT_ANIM_JSON
        try:
            orig_sprites.DEFAULT_ANIM_JSON = missing
            loaded = orig_sprites._load_animations_by_name(missing)
            self.assertEqual(len(loaded), len(from_catalog))
            self.assertEqual(loaded["agst301"]["subitems"], from_catalog["agst301"]["subitems"])
        finally:
            orig_sprites.DEFAULT_ANIM_JSON = old_default

    @unittest.skipUnless(HAS_NUMPY, "numpy is needed")
    def test_thief_idle_drives_left_right_eyes_and_viewer_loads_idle_lookout(self):
        thief = self._build_all_six()["thief"]
        idle_clip = next(c for c in thief.animations if c.name == "Idle")
        by_target = {(ch.target_node, ch.path): ch for ch in idle_clip.channels}
        for side in ("left", "right"):
            ch = by_target.get((f"eye_ball_{side}", "rotation"))
            self.assertIsNotNone(ch, f"Thief Idle clip must animate eye_ball_{side} rotation")
            qys = [float(v[1]) for v in ch.values]
            self.assertLess(min(qys), -0.30, f"Thief eye_ball_{side} must look hard left: {qys}")
            self.assertGreater(max(qys), 0.30, f"Thief eye_ball_{side} must look hard right: {qys}")

        node = shutil.which("node")
        if node is None:
            return
        gltf_js = os.path.join(REPO, "tools", "ant_models", "viewer_gltf.js")
        with tempfile.TemporaryDirectory(prefix="ants_thief_idle_") as tmpdir:
            glb_path = os.path.join(tmpdir, "thief.glb")
            with open(glb_path, "wb") as f:
                f.write(gltf_writer.scene_to_glb_bytes(thief))
            probe = (
                "const fs = require('fs'), vm = require('vm');\n"
                "globalThis.window = {};\n"
                "globalThis.createImageBitmap = async () => ({ width: 1, height: 1, close() {} });\n"
                "const gl = new Proxy({}, { get: (_, prop) => prop === 'getParameter' ? () => 0 : () => ({}) });\n"
                f"vm.runInThisContext(fs.readFileSync({gltf_js!r}, 'utf8'));\n"
                f"const buf = fs.readFileSync({glb_path!r});\n"
                "const ab = buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength);\n"
                "globalThis.window.__antGltf.parseGlb(ab, gl).then((m) => {\n"
                "  console.log(JSON.stringify({ idle: m.idleAnims.map((a) => a.name) }));\n"
                "});\n"
            )
            run = subprocess.run([node, "-e", probe], capture_output=True, text=True, check=False)
        self.assertEqual(run.returncode, 0, f"viewer_gltf.js parseGlb failed:\n{run.stderr}")
        parsed = json.loads(run.stdout)
        self.assertEqual(parsed["idle"][0], "Idle")
        self.assertIn("IdleLookout", parsed["idle"])

    @unittest.skipUnless(HAS_NUMPY, "numpy is needed")
    def test_slim_heads_flush_gear_fire_holster_and_ability_swaps(self):
        from tools.ant_models import ant_fire

        scenes = self._build_all_six()
        # 1. Worker & Thief heads stay well-proportioned (not overly wide).
        for cls_name, max_ratio in (("worker", 1.06), ("thief", 1.24)):
            cran = next(p for p in scenes[cls_name].world_parts() if p.name == "cranium")
            b0, b1 = cran.bounds()
            ratio = float((b1[0] - b0[0]) / (b1[1] - b0[1]))
            self.assertLessEqual(ratio, max_ratio, f"{cls_name} cranium W/H {ratio:.3f} > {max_ratio}")

        # 2. Bomber's backpack sits flush against the rear thorax (zero floating air gap).
        bomber_wp = scenes["bomber"].world_parts()
        b_thorax = next(p for p in bomber_wp if p.name == "thorax_core").bounds()
        b_pack = next(p for p in bomber_wp if p.name == "pack_rucksack_body").bounds()
        self.assertGreater(float(b_pack[1][2]), float(b_thorax[0][2]), "Bomber pack must contact rear thorax with zero air gap")

        # 3. Thief's crossbody strap stays tight to thorax_core (no floating shoulder arc).
        thief_wp = scenes["thief"].world_parts()
        t_thorax = next(p for p in thief_wp if p.name == "thorax_core").bounds()
        t_strap = next(p for p in thief_wp if p.name == "rogue_crossbody_strap").bounds()
        self.assertLessEqual(float(t_strap[1][1]), float(t_thorax[1][1]) + 0.015, "Thief strap floats above thorax")

        # 4. Fire Ant wears belt + side-holstered magnifying glass at rest; StartFire swaps belt glass -> hand glass + sunbeam.
        fire = scenes["fire"]
        self.assertIn("fire_belt", fire.nodes)
        self.assertIn("fire_belt_mag_glass", fire.nodes)
        sf_clip = next(c for c in fire.animations if c.name == "StartFire")
        sf_by = {(ch.target_node, ch.path): ch for ch in sf_clip.channels}
        belt_sc = sf_by[("fire_belt_mag_glass", "scale")].values
        hand_sc = sf_by[("fire_mag_glass", "scale")].values
        beam_sc = sf_by[("fire_sunbeam", "scale")].values
        self.assertGreater(belt_sc[0][0], 0.9)
        self.assertGreater(belt_sc[-1][0], 0.9)
        self.assertLess(min(v[0] for v in belt_sc), 1e-3, "belt glass must hide while held during StartFire")
        self.assertGreater(max(v[0] for v in hand_sc), 0.9, "hand glass must appear during StartFire")
        self.assertGreater(max(v[0] for v in beam_sc), 0.9, "sunbeam must activate during StartFire")
        # Never show BOTH holstered belt glass and hand-held glass at the same keyframe!
        for b_v, h_v in zip(belt_sc, hand_sc):
            self.assertFalse(b_v[0] > 0.5 and h_v[0] > 0.5, "duplicate Fire magnifying glasses visible simultaneously")
        # hold_magnifying_glass=True omits belt-holstered copy.
        fire_held = ant_fire.build_fire(hold_magnifying_glass=True)
        self.assertNotIn("fire_belt_mag_glass", fire_held.nodes)

        # 5. Bomber PlantBomb pulls center bomb out of pack and plants it solidly on Y = 0.0.
        pb_clip = next(c for c in scenes["bomber"].animations if c.name == "PlantBomb")
        pb_by = {(ch.target_node, ch.path): ch for ch in pb_clip.channels}
        pack_sc = pb_by[("pack_bomb_center", "scale")].values
        self.assertGreater(pack_sc[0][0], 0.9)
        self.assertLess(min(v[0] for v in pack_sc), 1e-3, "pack_bomb_center must hide while held/planted")
        self.assertIn(("forearm_left", "rotation"), pb_by)
        self.assertIn(("forearm_right", "rotation"), pb_by)

    @unittest.skipUnless(HAS_NUMPY, "numpy is needed")
    def test_back_silhouette_tail_rings_burrow_dive_and_swimmer_flippers(self):
        scenes = self._build_all_six()
        # 1. No protruding dorsal lumps on the back/petiole; concentric tail seams stay present.
        for cls_name, sc in scenes.items():
            part_names = {p.name for p in sc.world_parts()}
            for unwanted in ("dorsal_ridge", "dorsal_plate", "petiole_node"):
                self.assertNotIn(unwanted, part_names, f"{cls_name} should not contain protruding lump {unwanted}")
            self.assertIn("gaster_seam_0", part_names, f"{cls_name} missing gaster_seam_0")
            self.assertIn("gaster_seam_1", part_names, f"{cls_name} missing gaster_seam_1")

        # 2. Enter & Hatch keep every ant 100% full size (no shrink scale channel) and plunge below Y = 0.
        for cls_name, sc in scenes.items():
            for clip_name in ("Enter", "Hatch"):
                clip = next(c for c in sc.animations if c.name == clip_name)
                ub_scale = [ch for ch in clip.channels if ch.target_node == "upper_body" and ch.path == "scale"]
                self.assertEqual(ub_scale, [], f"{cls_name} {clip_name} must stay full size instead of shrinking")
                ub_t = next(ch for ch in clip.channels if ch.target_node == "upper_body" and ch.path == "translation")
                self.assertLess(min(v[1] for v in ub_t.values), 0.0, f"{cls_name} {clip_name} must dive below Y=0")

        # 3. Swimmer's 4 flippers attach directly onto individual `leg_*` nodes (never left grounded in static node).
        swimmer = scenes["swimmer"]
        self.assertNotIn("flippers", swimmer.nodes, "Swimmer flippers must ride on individual leg_* nodes")
        for tag in ("mid_left", "mid_right", "rear_left", "rear_right"):
            leg_parts = {p.name for p in swimmer.nodes[f"leg_{tag}"]}
            self.assertIn(f"flipper_{tag}", leg_parts, f"flipper_{tag} missing from leg_{tag}")

        # 4. Combat Ant's gold bandolier clears the outer thighs with zero intersection.
        combat = scenes["combat"]
        bm = combat.get_world_matrix("gear_bandolier")
        bp = np.vstack([p.positions for p in combat.nodes["gear_bandolier"]])
        bw = (bp @ bm[:3, :3].T) + bm[:3, 3]
        for ln in ("leg_mid_left", "leg_mid_right", "leg_rear_left", "leg_rear_right"):
            lm = combat.get_world_matrix(ln)
            lp = np.vstack([p.positions for p in combat.nodes[ln]])
            lw = (lp @ lm[:3, :3].T) + lm[:3, 3]
            outer = lw[np.abs(lw[:, 0]) > 0.16]
            near = bw[
                (np.sign(bw[:, 0]) == np.sign(outer[0, 0]))
                & (np.abs(bw[:, 0]) > 0.16)
                & (bw[:, 2] >= outer[:, 2].min() - 0.02)
                & (bw[:, 2] <= outer[:, 2].max() + 0.02)
            ]
            if len(near):
                self.assertGreater(
                    float(near[:, 1].min()),
                    float(outer[:, 1].max()) - 0.01,
                    f"Combat bandolier clips outer thigh of {ln}",
                )

        # 5. Thief, Bomber, and Fire stand upright matching 1998 proportions (not over-crouched).
        for cls_name in ("thief", "bomber", "fire"):
            tx = next(p for p in scenes[cls_name].world_parts() if p.name == "thorax_core")
            self.assertGreaterEqual(
                float(tx.bounds()[0][1]),
                0.21,
                f"{cls_name} thorax sits too low (over-crouched): {tx.bounds()[0][1]:.3f}",
            )


if __name__ == "__main__":
    unittest.main()
