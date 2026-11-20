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
import struct
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
            self.assertLess(alpha, 0.5, name)
        # Opaque chitin remains OPAQUE with alpha == 1.0.
        chitin_gltf = materials.get_material("chitin_team").to_gltf_dict()
        self.assertEqual(chitin_gltf["alphaMode"], "OPAQUE")
        self.assertAlmostEqual(chitin_gltf["pbrMetallicRoughness"]["baseColorFactor"][3], 1.0, places=6)

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

    def test_existing_built_glb_files_validate(self):
        models_dir = os.path.join(REPO, "tools", "ant_models", "models")
        if not os.path.isdir(models_dir):
            return
        for glb_path in sorted(glob.glob(os.path.join(models_dir, "*.glb"))):
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
            "eyelid_closed_angle",
            "foot_mid_left",
            "foot_mid_right",
            "foot_rear_left",
            "foot_rear_right",
        ):
            self.assertIn(key, anchors)

        self.assertAlmostEqual(anchors["ground_y"], 0.0, places=5)
        self.assertGreater(anchors["left_hand"][0], 0.0)
        self.assertLess(anchors["right_hand"][0], 0.0)
        self.assertTrue(0.90 <= anchors["head_top"][1] <= 1.02, f"unexpected head_top {anchors['head_top'][1]}")
        hrx, hry, hrz = anchors["head_radii"]
        self.assertGreater(hrx, hry, "heart-shield head should be wider across temples than tall")
        self.assertGreater(hry, hrz, "head should be narrower front-to-back than tall")
        self.assertGreater(anchors["eyelid_closed_angle"], 0.9)

        glb_bytes = gltf_writer.scene_to_glb_bytes(scene)
        stats = gltf_writer.validate_glb(glb_bytes)
        self.assertGreater(stats["triangles"], 1000)
        self.assertAlmostEqual(stats["bbox_min"][1], 0.0, delta=1e-3)
        self.assertTrue(1.00 <= stats["bbox_max"][1] <= 1.20, f"unexpected height {stats['bbox_max'][1]}")
        width, _, depth = stats["dimensions"]
        self.assertTrue(0.40 <= width <= 0.56, f"unexpected slim width {width}")
        self.assertLessEqual(depth, 0.66, f"expected compact tucked Z depth matching 901 E sprite, got {depth}")
        self.assertGreaterEqual(stats["bbox_min"][2], -0.38, f"gaster protrudes too far back: {stats['bbox_min'][2]}")
        self.assertLessEqual(width / stats["bbox_max"][1], 0.50, "expected upright slim W/H ratio")
        for expected_node in ("ant_test", "head", "eyes", "eyelid_left", "eyelid_right", "antennae", "body", "arms", "legs"):
            self.assertIn(expected_node, stats["nodes"])
        self.assertIn("Blink", stats["animations"])
        self.assertIn("eye_iris", stats["materials"])
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

        # Verify baked crevice AO creates real depth variation while respecting floors.
        scene, _ = base_ant.build_base_ant("ao_test")
        mesh_core.bake_scene_ao(scene)
        chitin_ao = np.concatenate([p.colors[:, 0] for p in scene.all_parts() if p.material == "chitin_team"])
        eye_ao = np.concatenate([p.colors[:, 0] for p in scene.all_parts() if p.material == "eye_white"])
        self.assertGreaterEqual(float(chitin_ao.min()), 0.43)
        self.assertLessEqual(float(chitin_ao.max()), 1.001)
        self.assertGreater(float(chitin_ao.max() - chitin_ao.min()), 0.12, "expected real crevice AO contrast")
        self.assertGreaterEqual(float(eye_ao.min()), 0.77, "eyes must stay bright inside sockets")

        # Verify embedded textures, compact U16 indices, and normalized U8 VEC4 COLOR_0 in GLB JSON.
        glb = gltf_writer.scene_to_glb_bytes(scene)
        doc, _ = gltf_writer.parse_glb(glb)
        mats = {m["name"]: m for m in doc["materials"]}
        self.assertIn("baseColorTexture", mats["chitin_team"]["pbrMetallicRoughness"])
        self.assertIn("metallicRoughnessTexture", mats["chitin_team"]["pbrMetallicRoughness"])
        self.assertIn("normalTexture", mats["chitin_team"])
        chitin_ns = float(mats["chitin_team"]["normalTexture"]["scale"])
        self.assertTrue(0.18 <= chitin_ns <= 0.30, f"expected subtle chitin normal scale, got {chitin_ns}")
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

        # Blink animation targets both eyelids with 6 keyframes.
        self.assertEqual(len(doc_open["animations"]), 1)
        blink_anim = doc_open["animations"][0]
        self.assertEqual(blink_anim["name"], "Blink")
        self.assertEqual(len(blink_anim["channels"]), 2)

        # Geometric eyelid radial clearance: inner eyelid strictly clears sclera+iris+pupil across blink=0..1.
        for b_val in (0.0, 0.25, 0.5, 0.75, 1.0):
            sc_b, a_b = base_ant.build_base_ant(f"clearance_{b_val}", blink=b_val)
            w_parts = sc_b.world_parts()
            for side_sign, eye_key, lid_name in ((1.0, "eye_left", "eyelid_left_shell"), (-1.0, "eye_right", "eyelid_right_shell")):
                pivot = np.asarray(a_b[eye_key], dtype=np.float64)
                eye_verts = np.vstack(
                    [
                        p.positions.astype(np.float64)
                        for p in sc_b.get_node("eyes")
                        if p.material in ("eye_white", "eye_iris", "eye_pupil")
                        and float(p.positions[:, 0].mean()) * side_sign > 0.0
                    ]
                )
                r_eye_max = float(np.linalg.norm(eye_verts - pivot[None, :], axis=1).max())
                lid_verts = np.vstack([p.positions.astype(np.float64) for p in w_parts if p.name == lid_name])
                r_lid_inner = float(np.linalg.norm(lid_verts - pivot[None, :], axis=1).min())
                self.assertGreaterEqual(
                    r_lid_inner - r_eye_max,
                    0.006,
                    f"{lid_name} at blink={b_val}: clearance {r_lid_inner - r_eye_max:.5f} < 0.006",
                )

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
                    self.assertTrue(0.85 <= stats["bbox_max"][1] <= 1.45, f"{entry['id']} height {stats['bbox_max'][1]}")

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

            # Dynamic viewer file discovery hashes all viewer*.html/.css/.js files present.
            viewer_files = build_all._discover_viewer_files()
            for fname in viewer_files:
                self.assertTrue(fname.startswith("viewer"))
                self.assertTrue(fname.endswith((".html", ".css", ".js")))
            if os.path.isfile(os.path.join(build_all.THIS_DIR, "viewer.html")):
                self.assertIn("viewer.html", viewer_files)


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


if __name__ == "__main__":
    unittest.main()
