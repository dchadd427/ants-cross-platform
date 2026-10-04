#!/usr/bin/env python3
"""No compile command carries the folder of the checkout (run by ./run_tests.sh --fast and by the CI).

ccache reuses an object only when the compile command is the same, so a definition like -DORIGINAL_ASSETS_DIR="/home/me/ants/Original-Ants" made every test source
that has one miss the cache in a second worktree or a fresh clone. The test and tool sources take the folders from tests/common/ants_test_paths.hpp now: the library
ants_test_paths (one tiny source that CMake generates per build folder) returns them. This test configures the whole project and asks CMake's File API
(codemodel-v2) what every target's compiler is told: no definition and no flag may contain the checkout's folder, and the generated source has the right folders.
(Include paths are not looked at: ccache's base_dir rewrites those, docs/WORKFLOW.md.) It configures only; nothing is compiled.
"""
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CMAKE = shutil.which("cmake")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def spellings(path):
    """The folder as a compile command could spell it: with slashes, with backslashes, and as the real path."""
    forms = {path.replace("\\", "/"), path.replace("/", "\\"), os.path.realpath(path).replace("\\", "/")}
    return {f.lower() if os.name == "nt" else f for f in forms}


@unittest.skipUnless(CMAKE, "cmake is needed")
class CompileCommandsAreFolderIndependent(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.build = os.path.join(cls.tmp.name, "build dir with spaces")
        query = os.path.join(cls.build, ".cmake", "api", "v1", "query")
        os.makedirs(query)
        open(os.path.join(query, "codemodel-v2"), "w").close()
        generator = ["-G", "Ninja"] if shutil.which("ninja") else []
        cls.configure = subprocess.run([CMAKE, "-S", REPO, "-B", cls.build, "-DANTS_USE_CCACHE=OFF", *generator], capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def targets(self):
        reply_dir = os.path.join(self.build, ".cmake", "api", "v1", "reply")
        index_file = sorted(glob.glob(os.path.join(reply_dir, "index-*.json")))[-1]
        index = json.loads(read(index_file))
        model = json.loads(read(os.path.join(reply_dir, [o for o in index["objects"] if o["kind"] == "codemodel"][0]["jsonFile"])))
        found = {}
        for target_ref in model["configurations"][0]["targets"]:
            found[target_ref["name"]] = json.loads(read(os.path.join(reply_dir, target_ref["jsonFile"])))
        return found

    def test_the_project_configures(self):
        self.assertEqual(self.configure.returncode, 0, self.configure.stderr[-2000:])

    def test_no_definition_and_no_flag_contains_the_folder_of_the_checkout(self):
        self.assertEqual(self.configure.returncode, 0, self.configure.stderr[-2000:])
        forms = spellings(REPO)
        offenders = []
        for name, target in sorted(self.targets().items()):
            for group in target.get("compileGroups", []):
                texts = [d["define"] for d in group.get("defines", [])] + [f["fragment"] for f in group.get("compileCommandFragments", [])]
                for text in texts:
                    probe = text.lower() if os.name == "nt" else text
                    if any(form in probe for form in forms):
                        offenders.append("%s: %s" % (name, text))
        self.assertEqual(offenders, [], "a compile command that carries the folder of the checkout cannot be reused by ccache in another folder; "
                         "take the folder from tests/common/ants_test_paths.hpp (link the library ants_test_paths) instead of a definition:\n  " + "\n  ".join(offenders))

    def test_the_test_programs_and_the_tools_link_the_paths_library(self):
        self.assertEqual(self.configure.returncode, 0)
        targets = self.targets()
        self.assertIn("ants_test_paths", targets)
        for expected in ("test_sim_rules", "test_app_integration", "test_ai", "test_assets", "test_server", "map_sweep", "bot_arena"):
            self.assertIn(expected, targets)
            linked = [d["id"] for d in targets[expected].get("dependencies", [])]
            self.assertTrue(any(i.startswith("ants_test_paths::") for i in linked), "%s does not link ants_test_paths (dependencies: %s)" % (expected, linked))

    def test_the_generated_source_names_the_folders(self):
        self.assertEqual(self.configure.returncode, 0)
        text = read(os.path.join(self.build, "generated", "ants_test_paths.cpp"))
        root = REPO.replace("\\", "/")
        self.assertIn('original_assets_dir() { return "%s/Original-Ants"; }' % root, text)
        self.assertIn('test_data_dir() { return "%s/tests/data"; }' % root, text)
        self.assertIn('source_dir() { return "%s"; }' % root, text)
        self.assertNotIn("@", text, "an unreplaced @VARIABLE@ of the template")

    def test_the_folders_it_names_exist(self):
        root = REPO
        self.assertTrue(os.path.isfile(os.path.join(root, "Original-Ants", "ants.chd")))
        self.assertTrue(os.path.isdir(os.path.join(root, "tests", "data")))
        self.assertTrue(os.path.isfile(os.path.join(root, "web", "lobby.html")))


if __name__ == "__main__":
    unittest.main()
