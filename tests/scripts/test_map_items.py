"""Tests for 3D map item models, pure-stdlib catalog safety, GLB validation, and items viewer JS."""
from __future__ import annotations

import contextlib
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
if REPO not in sys.path:
    sys.path.insert(0, REPO)

try:
    import numpy as _np  # noqa: F401

    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False


class MapItemsTests(unittest.TestCase):
    def test_pure_stdlib_imports_and_material_order_preserved(self):
        from tools.ant_models import materials
        from tools.ant_models.map_items import (
            CATEGORIES,
            MAP_ITEM_SPECS,
            build_items,
            item_materials,
            items_foliage,
            items_food,
            items_plants,
            items_structures,
            items_toys,
            items_world,
        )

        before_order = list(materials.MATERIAL_ORDER)
        item_materials.register_item_materials()
        self.assertEqual(list(materials.MATERIAL_ORDER), before_order)
        self.assertGreater(len(materials.EXTRA_MATERIALS), 35)
        self.assertGreaterEqual(len(MAP_ITEM_SPECS), 68)
        self.assertEqual(CATEGORIES[0][0], "all")
        self.assertTrue(callable(build_items.build_all_items))
        self.assertTrue(callable(items_toys.build_bearhed))
        self.assertTrue(callable(items_food.build_fdburgr1))
        self.assertTrue(callable(items_plants.build_grass1))
        self.assertTrue(callable(items_foliage.build_flower3c))
        self.assertTrue(callable(items_structures.build_wallup04))
        self.assertTrue(callable(items_world.build_greenhill))
        # Plants & Grass (35) and Hills & Gear (19) are 100% covered.
        self.assertEqual(sum(1 for s in MAP_ITEM_SPECS if s.category == "plants"), 35)
        self.assertEqual(sum(1 for s in MAP_ITEM_SPECS if s.category == "structures"), 19)

    def test_catalog_matches_authentic_1998_chd_sprites_and_excludes_tiles_hud_cursors(self):
        from tools.ant_models.map_items import CATEGORIES, MAP_ITEM_SPECS

        valid_cats = {cid for cid, _ in CATEGORIES if cid != "all"}
        chd_path = os.path.join(REPO, "docs", "chd_table4_animations.json")
        with open(chd_path, encoding="utf-8") as f:
            chd = json.load(f)
        by_id = {int(entry["entry_index"]): entry for entry in chd}

        seen_ids = set()
        for spec in MAP_ITEM_SPECS:
            self.assertNotIn(spec.id, seen_ids, f"duplicate item id {spec.id}")
            seen_ids.add(spec.id)
            self.assertIn(spec.category, valid_cats, f"invalid category for {spec.id}: {spec.category}")
            self.assertIn(spec.chd_id, by_id, f"chd_id {spec.chd_id} missing from chd_table4_animations.json")
            entry = by_id[spec.chd_id]
            self.assertEqual(entry.get("name"), spec.chd_name, f"chd_name mismatch for {spec.id}")
            low_name = spec.chd_name.lower()
            for forbidden_prefix in ("cursor", "tile", "hud", "menu", "font"):
                self.assertFalse(low_name.startswith(forbidden_prefix), f"excluded asset leaked into catalog: {spec.id}")
            # Verify first sprite PNG exists on disk in asset_catalog/sprites/.
            subitems = entry.get("subitems", [])
            self.assertTrue(subitems, f"no subitems for {spec.id}")
            frames = subitems[0].get("frames", [])
            self.assertTrue(frames, f"no frames for {spec.id}")
            sid = int(frames[0]["sprite_index"])
            png_path = os.path.join(REPO, "asset_catalog", "sprites", f"sprite_{sid:04d}.png")
            self.assertTrue(os.path.isfile(png_path), f"missing 1998 sprite PNG {png_path} for {spec.id}")

    @unittest.skipUnless(HAS_NUMPY, "numpy is needed to build and validate GLB meshes")
    def test_build_all_wave1_items_valid_grounded_glb_and_parseable(self):
        from tools.ant_models import gltf_writer
        from tools.ant_models.map_items import MAP_ITEM_SPECS, build_items

        with tempfile.TemporaryDirectory(prefix="ants_map_items_") as tmpdir:
            with contextlib.redirect_stdout(io.StringIO()):
                manifest = build_items.build_all_items(Path(tmpdir), only=None)
            self.assertEqual(manifest["built_count"], len(MAP_ITEM_SPECS))
            self.assertEqual(len(manifest["items"]), len(MAP_ITEM_SPECS))

            orig_path = os.path.join(tmpdir, "orig_items.json")
            self.assertTrue(os.path.isfile(orig_path))
            with open(orig_path, encoding="utf-8") as f:
                orig_bundle = json.load(f)
            self.assertEqual(orig_bundle["item_count"], len(MAP_ITEM_SPECS))

            by_id = {it["id"]: it for it in manifest["items"]}
            for spec in MAP_ITEM_SPECS:
                info = by_id[spec.id]
                glb_path = os.path.join(tmpdir, info["file"])
                self.assertTrue(os.path.isfile(glb_path), f"missing {glb_path}")
                with open(glb_path, "rb") as fh:
                    glb_bytes = fh.read()
                gltf_writer.validate_glb(glb_bytes)
                self.assertAlmostEqual(info["min_y"], 0.0, places=3, msg=f"{spec.id} not grounded at Y=0")
                self.assertGreater(info["triangles"], 100, f"{spec.id} too few triangles")
                self.assertTrue(all(dim > 0.05 for dim in info["size"]), f"{spec.id} degenerate size {info['size']}")
                self.assertIn(spec.id, orig_bundle["items"])

            # Animated items (waving anthill flags, Groucho daisy, Jell-O, bombs, fire wall) must carry their Idle clip.
            for anim_id in (
                "fdjelo1", "dflower1",
                "GREENHILL", "REDHILL", "BLUEHILL", "BLACKHILL",
                "redbomb", "greenbomb", "blackbomb", "bluebomb", "wallup04",
            ):
                self.assertIn("Idle", by_id[anim_id]["animations"], f"{anim_id} missing Idle animation")

            # Verify viewer_gltf.js parses a generated map item GLB cleanly under Node if available.
            node = shutil.which("node")
            if node:
                gltf_js = os.path.join(REPO, "tools", "ant_models", "viewer_gltf.js")
                jello_glb = os.path.join(tmpdir, "fdjelo1.glb")
                probe = (
                    "const fs = require('fs'), vm = require('vm');\n"
                    "globalThis.window = {};\n"
                    "globalThis.createImageBitmap = async () => ({ width: 1, height: 1, close() {} });\n"
                    "const gl = new Proxy({}, { get: (_, prop) => prop === 'getParameter' ? () => 0 : () => ({}) });\n"
                    f"vm.runInThisContext(fs.readFileSync({gltf_js!r}, 'utf8'));\n"
                    f"const buf = fs.readFileSync({jello_glb!r});\n"
                    "const ab = buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength);\n"
                    "globalThis.window.__antGltf.parseGlb(ab, gl).then((m) => {\n"
                    "  console.log(JSON.stringify({ calls: m.drawCalls.length, idle: m.idleAnims.map((a) => a.name) }));\n"
                    "});\n"
                )
                run = subprocess.run([node, "-e", probe], capture_output=True, text=True, check=False)
                self.assertEqual(run.returncode, 0, f"viewer_gltf.js failed to parse fdjelo1.glb:\n{run.stderr}")
                parsed = json.loads(run.stdout)
                self.assertGreater(parsed["calls"], 0)
                self.assertIn("Idle", parsed["idle"])

    def test_items_viewer_js_syntax(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("node is not installed")
        for fname in ("compare.js", "items.js", "viewer_render.js"):
            js_path = os.path.join(REPO, "tools", "ant_models", fname)
            run = subprocess.run([node, "--check", js_path], capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 0, f"JS syntax check failed for {fname}:\n{run.stderr}")


if __name__ == "__main__":
    unittest.main()
