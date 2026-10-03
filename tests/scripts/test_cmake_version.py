#!/usr/bin/env python3
"""CMake configure check of the generated version header (run by ./run_tests.sh --fast and by the CI).

ants_app/version.hpp does not exist in the repository: CMake generates it from the file VERSION into <build folder>/generated. This test configures the
whole project into a build folder whose path has a SPACE in it (as Windows user folders do) with a MULTI-CONFIGURATION generator (the kind Visual Studio is:
"Ninja Multi-Config", else Xcode on a Mac; a single-configuration generator only when neither exists) and asks CMake's File API (codemodel-v2) what the
compiler is told for every source file that includes ants_app/version.hpp, in every configuration:

  - the generated folder is on the include path of that file's target, in every configuration (the include path comes from the target ants_version that the
    target links, not from a global setting that only one generator honours)
  - the header was generated there, says the version of the file VERSION and declares BUILD_ID, and the build id source file exists
  - nothing under the repository's own include/ folder shadows it (no checked-in version.hpp)

It configures only; nothing is compiled (the build itself proves the compile). Without the link to ants_version, or without the generated folder on the
include path, the first check fails and names the target and the configuration.
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


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def pick_generator():
    if shutil.which("ninja"):
        return "Ninja Multi-Config"
    if sys.platform == "darwin" and shutil.which("xcodebuild"):
        return "Xcode"
    return None                                                           # the default generator: one configuration only


def norm(path):
    return os.path.normcase(os.path.normpath(path))


def includes_version_header(path):
    try:
        text = read(path)
    except (OSError, UnicodeDecodeError):
        return False
    return re.search(r'#\s*include\s*[<"]ants_app/version\.hpp[>"]', text) is not None


@unittest.skipUnless(shutil.which("cmake"), "cmake is needed")
class GeneratedVersionHeader(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.build = os.path.join(cls.tmp.name, "build dir with spaces")
        query = os.path.join(cls.build, ".cmake", "api", "v1", "query")
        os.makedirs(query)
        open(os.path.join(query, "codemodel-v2"), "w").close()           # ask for the codemodel
        cls.generator = pick_generator()
        command = ["cmake", "-S", REPO, "-B", cls.build, "-DANTS_USE_CCACHE=OFF"]
        if cls.generator:
            command += ["-G", cls.generator]
        cls.configure = subprocess.run(command, capture_output=True, text=True)
        cls.version = read(os.path.join(REPO, "VERSION")).strip()
        cls.generated = os.path.join(cls.build, "generated")

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def codemodel(self):
        replies = glob.glob(os.path.join(self.build, ".cmake", "api", "v1", "reply", "index-*.json"))
        self.assertTrue(replies, "CMake wrote no File API reply")
        with open(replies[-1], encoding="utf-8") as f:
            index = json.load(f)
        entry = [o for o in index["objects"] if o["kind"] == "codemodel"][0]
        with open(os.path.join(self.build, ".cmake", "api", "v1", "reply", entry["jsonFile"]), encoding="utf-8") as f:
            return json.load(f)

    def test_the_project_configures_with_the_generator(self):
        self.assertEqual(self.configure.returncode, 0, "generator %r:\n%s" % (self.generator, self.configure.stderr[-2000:]))

    def test_the_header_and_the_build_id_source_are_generated(self):
        self.assertEqual(self.configure.returncode, 0)
        header = read(os.path.join(self.generated, "ants_app", "version.hpp"))
        self.assertIn('VERSION_STRING = "v%s"' % self.version, header)
        major, minor, patch = self.version.split(".")
        self.assertIn("VERSION_MAJOR = %s;" % major, header)
        self.assertIn("VERSION_MINOR = %s;" % minor, header)
        self.assertIn("VERSION_PATCH = %s;" % patch, header)
        self.assertIn("extern const std::string_view BUILD_ID;", header)
        self.assertNotIn("@", header, "an unreplaced @VARIABLE@ of the template")
        self.assertTrue(os.path.isfile(os.path.join(self.generated, "build_id.cpp")))
        self.assertIn("BUILD_ID", read(os.path.join(self.generated, "build_id.cpp")))

    def test_no_header_of_the_repository_shadows_the_generated_one(self):
        self.assertFalse(os.path.exists(os.path.join(REPO, "include", "ants_app", "version.hpp")),
                         "include/ants_app/version.hpp is generated from VERSION; it must not be checked in")
        self.assertTrue(os.path.exists(os.path.join(REPO, "include", "ants_app", "version.hpp.in")))

    def test_every_target_that_includes_the_header_gets_the_generated_folder_in_every_configuration(self):
        self.assertEqual(self.configure.returncode, 0)
        model = self.codemodel()
        reply_dir = os.path.join(self.build, ".cmake", "api", "v1", "reply")
        generated = norm(self.generated)
        checked = []                                                       # (configuration, target, source)
        configurations = model["configurations"]
        self.assertTrue(configurations)
        if self.generator in ("Ninja Multi-Config", "Xcode"):
            self.assertGreaterEqual(len(configurations), 2, "a multi-configuration generator has several configurations")
        for configuration in configurations:
            for target_ref in configuration["targets"]:
                with open(os.path.join(reply_dir, target_ref["jsonFile"]), encoding="utf-8") as f:
                    target = json.load(f)
                for group in target.get("compileGroups", []):
                    include_paths = [norm(i["path"]) for i in group.get("includes", [])]
                    for index in group["sourceIndexes"]:
                        source = target["sources"][index]["path"]
                        source_path = source if os.path.isabs(source) else os.path.join(REPO, source)
                        if not includes_version_header(source_path):
                            continue
                        checked.append((configuration["name"], target["name"], source))
                        self.assertIn(generated, include_paths,
                                      "configuration %s: target %s (%s) includes ants_app/version.hpp but the generated folder %s is not on its include path:\n  %s"
                                      % (configuration["name"], target["name"], source, self.generated, "\n  ".join(include_paths)))
        names = {target for _, target, _ in checked}
        for expected in ("ants_app", "ants_server", "test_app_integration"):
            self.assertIn(expected, names, "the target %s includes ants_app/version.hpp and was not found by the check (found: %s)" % (expected, sorted(names)))
        configuration_names = {c for c, _, _ in checked}
        self.assertEqual(configuration_names, {c["name"] for c in configurations})

    def test_the_include_path_comes_from_a_library_target_not_from_a_global_setting(self):
        # the targets reach the folder by LINKING ants_version (usage requirements), which every generator honours; a global include_directories() would not be in the codemodel's
        # target-level includes of a target that has nothing to do with the header
        self.assertEqual(self.configure.returncode, 0)
        model = self.codemodel()
        reply_dir = os.path.join(self.build, ".cmake", "api", "v1", "reply")
        generated = norm(self.generated)
        configuration = model["configurations"][0]
        by_name = {}
        for target_ref in configuration["targets"]:
            with open(os.path.join(reply_dir, target_ref["jsonFile"]), encoding="utf-8") as f:
                by_name[target_ref["name"]] = json.load(f)
        self.assertIn("ants_version", by_name)
        unrelated = by_name["ants_assets"]                                 # the asset decoders never include the header
        for group in unrelated.get("compileGroups", []):
            self.assertNotIn(generated, [norm(i["path"]) for i in group.get("includes", [])])


if __name__ == "__main__":
    unittest.main()
