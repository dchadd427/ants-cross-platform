#!/usr/bin/env python3
"""CMake configure checks (run by ./run_tests.sh --fast and by the CI): the generated version header, and the ccache support.

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


@unittest.skipUnless(shutil.which("cmake") and os.name == "posix", "cmake and a POSIX shell are needed")
class CcacheSupport(unittest.TestCase):
    """The ccache option of the root CMakeLists.txt: what it says and does at configure time (configure only; nothing is compiled).

    ccache is the compiler launcher when it is found (PATH, or ~/.local/bin) and nothing speaks against it; the configure output says so either way.
    """

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.fake_bin = os.path.join(cls.tmp.name, "fake bin")
        os.makedirs(cls.fake_bin)
        fake = os.path.join(cls.fake_bin, "ccache")
        with open(fake, "w", encoding="utf-8") as f:
            f.write('#!/bin/sh\nexec "$@"\n')                              # a launcher that just runs the compiler
        os.chmod(fake, 0o755)
        cls.fake = fake

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def configure(self, name, *args, path_first=None):
        build = os.path.join(self.tmp.name, name)
        env = dict(os.environ)
        env["HOME"] = os.path.join(self.tmp.name, "home")                  # the folder ~/.local/bin is a hint of the search (before PATH): the machine's own ccache must not win here
        os.makedirs(env["HOME"], exist_ok=True)
        if path_first:
            env["PATH"] = path_first + os.pathsep + env.get("PATH", "")
        result = subprocess.run(["cmake", "-S", REPO, "-B", build, "-DBUILD_TESTS=OFF", "-DANTS_BUILD_APP=OFF", *args], capture_output=True, text=True, env=env)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout, build

    def launcher_in_makefiles(self, build):
        """True when the generated build rules run the compiler through a launcher (Makefile generators write it into the rules)."""
        rules = []
        for root, _, files in os.walk(build):
            for name in files:
                if name in ("build.make", "rules.ninja"):
                    rules.append(os.path.join(root, name))
        if not rules:
            return None
        return any(self.fake in read(r) for r in rules)

    def test_the_option_switches_it_off_and_says_so(self):
        out, build = self.configure("off", "-DANTS_USE_CCACHE=OFF", path_first=self.fake_bin)
        self.assertIn("ccache: not used (ANTS_USE_CCACHE=OFF)", out)
        self.assertNotIn("ccache: using", out)
        self.assertIn(self.launcher_in_makefiles(build), (False, None))

    def test_a_launcher_that_is_already_set_wins_and_the_message_says_so(self):
        out, build = self.configure("set", "-DCMAKE_CXX_COMPILER_LAUNCHER=/usr/bin/env", path_first=self.fake_bin)
        self.assertIn("ccache: not used (a compiler launcher is already set", out)
        self.assertNotIn("ccache: using", out)
        self.assertIn(self.launcher_in_makefiles(build), (False, None))

    def test_a_ccache_on_the_path_is_the_launcher_by_default(self):
        out, build = self.configure("found", path_first=self.fake_bin)
        self.assertIn("ccache: using " + self.fake, out)
        found = self.launcher_in_makefiles(build)
        if found is not None:                                              # Makefile and Ninja generators write the launcher into their rules
            self.assertTrue(found, "the compile rules do not run the compiler through ccache")

    def test_ccache_in_the_home_local_bin_folder_is_found_without_being_on_the_path(self):
        home = os.path.join(self.tmp.name, "hinthome")
        local_bin = os.path.join(home, ".local", "bin")
        os.makedirs(local_bin)
        shutil.copy(self.fake, os.path.join(local_bin, "ccache"))
        os.chmod(os.path.join(local_bin, "ccache"), 0o755)
        build = os.path.join(self.tmp.name, "hint")
        env = dict(os.environ)
        env["HOME"] = home
        env["PATH"] = os.pathsep.join(p for p in env.get("PATH", "").split(os.pathsep) if not os.path.exists(os.path.join(p, "ccache")))   # a machine's own ccache is off the path
        result = subprocess.run(["cmake", "-S", REPO, "-B", build, "-DBUILD_TESTS=OFF", "-DANTS_BUILD_APP=OFF", "-DCMAKE_PREFIX_PATH=/nonexistent"], capture_output=True, text=True, env=env)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        found = re.search(r"ccache: using (\S.*)", result.stdout)
        self.assertIsNotNone(found, result.stdout)
        # a ccache in a system folder (/usr/bin, /opt/homebrew/bin) may be found first by CMake's own search; the hint is what this checks: ours when the machine has none
        if not any(os.path.exists(os.path.join(d, "ccache")) for d in ("/usr/bin", "/usr/local/bin", "/opt/homebrew/bin", "/bin")):
            self.assertEqual(found.group(1).strip(), os.path.join(local_bin, "ccache"))

    def test_the_build_id_target_and_the_generated_header_do_not_depend_on_ccache(self):
        out, build = self.configure("with", path_first=self.fake_bin)
        out2, build2 = self.configure("without", "-DANTS_USE_CCACHE=OFF", path_first=self.fake_bin)
        self.assertEqual(read(os.path.join(build, "generated", "ants_app", "version.hpp")), read(os.path.join(build2, "generated", "ants_app", "version.hpp")))


if __name__ == "__main__":
    unittest.main()
