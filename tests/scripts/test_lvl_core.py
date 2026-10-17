#!/usr/bin/env python3
"""The map core in the browser (docs/MAP_FORMAT.md, "In the browser"): where it is built and shipped, and its Node test.

  - Wiring: the CMake target lvl_core exports exactly the functions of src/ants_assets/lvl_core_c.cpp, the web image copies the module and its wrapper, the CI's check of the image lists them and
    runs the Node test on them (with --require), the local web build copies them, and the Node test's expectation (tests/data/lvl_core/) is there for every file it names.
  - The Node test (tests/scripts/lvl_core_check.js) is RUN when node and a built module are there: the folder of $ANTS_LVL_CORE_DIR, else build_web/lvl_core of the checkout (cmake --build build_web
    --target lvl_core, or tools/web_without_docker.py). Without a module it is skipped and says so: the module is then NOT tested here (the web job of the CI runs it from the image, with --require).
"""
import os
import re
import shutil
import subprocess
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
NODE = shutil.which("node")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def module_dir():
    for candidate in (os.environ.get("ANTS_LVL_CORE_DIR", ""), os.path.join(REPO, "build_web", "lvl_core")):
        if candidate and os.path.isfile(os.path.join(candidate, "lvl_core.js")) and os.path.isfile(os.path.join(candidate, "lvl_core.wasm")):
            return candidate
    return ""


class TheWiring(unittest.TestCase):
    def test_the_module_exports_what_the_c_interface_defines_and_nothing_else(self):
        defined = set(re.findall(r"(?m)^LVL_CORE_EXPORT\s+[\w\s\*]+?\b(lvl_\w+)\(", read("src", "ants_assets", "lvl_core_c.cpp")))
        listed = re.search(r'set\(ANTS_LVL_CORE_EXPORTS "([^"]+)"\)', read("CMakeLists.txt"))
        self.assertTrue(listed)
        exported = {name.lstrip("_") for name in listed.group(1).split(",")}
        self.assertEqual(exported, defined)
        self.assertEqual(len(defined), 11)

    def test_the_module_is_a_target_of_the_web_build_only_and_has_no_game_code(self):
        text = read("CMakeLists.txt")
        block = text[text.index("add_executable(lvl_core"):]
        block = block[:block.index("endif()")]
        self.assertIn("-fwasm-exceptions", block)
        self.assertIn("-sFILESYSTEM=0", block)
        self.assertNotIn("SDL", block)
        for source in re.findall(r"src/ants_assets/(\w+\.cpp)", block.split("target_include_directories")[0]):
            self.assertTrue(os.path.isfile(os.path.join(REPO, "src", "ants_assets", source)), source)
        self.assertRegex(text[:text.index("add_executable(lvl_core")][-400:], r"(?s)if\(EMSCRIPTEN\)\s*$")

    def test_the_image_the_ci_and_the_local_build_carry_the_module_and_its_wrapper(self):
        docker, ci, build = read("Dockerfile"), read(".github", "workflows", "ci.yml"), read("build_web.sh")
        self.assertIn("COPY --from=builder /src/build_web/lvl_core/lvl_core.js /src/build_web/lvl_core/lvl_core.wasm /usr/share/nginx/html/", docker)
        self.assertRegex(docker, r"(?m)^COPY web/lvl_core_api\.js ")
        self.assertIn("lvl_core.js lvl_core.wasm lvl_core_api.js", ci)
        self.assertIn("node tests/scripts/lvl_core_check.js --module lvl_core_from_image --require", ci)
        self.assertIn("lvl_core/lvl_core.js", build)
        self.assertIn("web/lvl_core_api.js", build)

    def test_the_native_test_is_registered_and_run_by_the_runner(self):
        self.assertIn("add_test(NAME test_lvl_core COMMAND test_lvl_core)", read("tests", "test_assets", "CMakeLists.txt"))
        self.assertIn('"test_lvl_core"', read("run_tests.sh"))

    def test_the_expectation_the_node_test_reads_is_complete(self):
        folder = os.path.join(REPO, "tests", "data", "lvl_core")
        ids = [line.split("\t")[0] for line in read("tests", "data", "lvl_core", "index.txt").splitlines() if line]
        ids += [line.split("\t")[0] for line in read("tests", "data", "lvl_core", "hostile_index.txt").splitlines() if line]
        self.assertGreaterEqual(len(ids), 20)
        for i in ids:
            for suffix in (".read.json", ".check.json"):
                self.assertTrue(os.path.isfile(os.path.join(folder, i + suffix)), i + suffix)
        for line in read("tests", "data", "lvl_core", "index.txt").splitlines():
            if line:
                i, path = line.split("\t")[:2]
                self.assertTrue(os.path.isfile(os.path.join(REPO, path)), path)
                self.assertTrue(os.path.isfile(os.path.join(folder, i + ".lvl_tool.txt")), i)

    def test_the_wrapper_is_a_plain_script_with_no_storage_or_network(self):
        text = read("web", "lvl_core_api.js")
        for word in ("fetch(", "XMLHttpRequest", "localStorage", "WebSocket", "eval("):
            self.assertNotIn(word, text)


@unittest.skipUnless(NODE, "node is not installed: the map core's module was NOT run (tests/scripts/lvl_core_check.js)")
@unittest.skipUnless(module_dir(), "no build_web/lvl_core/lvl_core.wasm (and no $ANTS_LVL_CORE_DIR): the map core's module was NOT run (tests/scripts/lvl_core_check.js; the web job of the CI runs it from the image)")
class TheModuleInNode(unittest.TestCase):
    def test_the_module_answers_as_the_native_core_does(self):
        command = [NODE, os.path.join(REPO, "tests", "scripts", "lvl_core_check.js"), "--module", module_dir(), "--require"]
        tool = os.environ.get("ANTS_LVL_TOOL", os.path.join(REPO, "build", "lvl_tool"))
        if os.path.isfile(tool):
            command += ["--lvl-tool", tool]
        done = subprocess.run(command, capture_output=True, text=True, timeout=300)
        self.assertEqual(done.returncode, 0, done.stdout[-3000:] + done.stderr[-1000:])
        self.assertIn(" 0 failures", done.stdout)


if __name__ == "__main__":
    unittest.main()
