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


def resolve_tools():
    """The programs that a CMake configure of this project needs, found ONCE, with the environment as the test run started (before any test replaces PATH or HOME):
    cmake, the build programs of the generators (make, ninja) and the C++ compiler. Where they live differs from machine to machine (/usr/bin on a Linux runner,
    /opt/homebrew/bin or /usr/local/bin on a Mac): no test may assume a folder, and a test that restricts PATH builds it from the folders of these programs."""
    found = {}
    for name in ("cmake", "make", "ninja"):
        path = shutil.which(name)
        if path:
            found[name] = path
    candidates = []
    if os.environ.get("CXX"):
        candidates.append(os.environ["CXX"].split()[0])
    candidates += ["c++", "g++", "clang++"]
    for name in candidates:
        path = shutil.which(name)
        if path:
            found["c++"] = path
            break
    return found


TOOLS = resolve_tools()
CMAKE = TOOLS.get("cmake")


def tool_folders(tools=TOOLS):
    """The folders of the resolved tools, in order, without repeats: the PATH that is enough to configure and nothing more."""
    folders = []
    for path in tools.values():
        folder = os.path.dirname(path)
        if folder not in folders:
            folders.append(folder)
    return folders


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


@unittest.skipUnless(CMAKE, "cmake is needed")
class GeneratedVersionHeader(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.build = os.path.join(cls.tmp.name, "build dir with spaces")
        query = os.path.join(cls.build, ".cmake", "api", "v1", "query")
        os.makedirs(query)
        open(os.path.join(query, "codemodel-v2"), "w").close()           # ask for the codemodel
        cls.generator = pick_generator()
        command = [CMAKE, "-S", REPO, "-B", cls.build, "-DANTS_USE_CCACHE=OFF"]
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


@unittest.skipUnless(CMAKE and os.name == "posix", "cmake and a POSIX shell are needed")
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
        result = subprocess.run([CMAKE, "-S", REPO, "-B", build, "-DBUILD_TESTS=OFF", "-DANTS_BUILD_APP=OFF", *args], capture_output=True, text=True, env=env)
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

    def restricted_environment(self, home):
        """HOME as given and a PATH of exactly the folders of the tools that a configure needs (cmake, make / ninja, the compiler: resolved before the environment was
        touched, see resolve_tools): nothing else is on it, so a ccache of this machine is on it only if it lives in one of those folders (/usr/bin of a runner that
        installed it), where it has to stay."""
        env = dict(os.environ)
        env["HOME"] = home
        env["PATH"] = os.pathsep.join(tool_folders())
        return env

    def test_ccache_in_the_home_local_bin_folder_is_found_by_the_hint_and_chosen_before_the_path(self):
        self.assertIn("c++", TOOLS, "no C++ compiler was found to configure with")
        home = os.path.join(self.tmp.name, "hinthome")
        local_bin = os.path.join(home, ".local", "bin")
        os.makedirs(local_bin)
        hinted = os.path.join(local_bin, "ccache")
        shutil.copy(self.fake, hinted)
        os.chmod(hinted, 0o755)
        env = self.restricted_environment(home)
        self.assertNotIn(local_bin, env["PATH"].split(os.pathsep))        # the fake is NOT on the path: only the hint ~/.local/bin can lead to it
        build = os.path.join(self.tmp.name, "hint")
        result = subprocess.run([CMAKE, "-S", REPO, "-B", build, "-DBUILD_TESTS=OFF", "-DANTS_BUILD_APP=OFF", "-DCMAKE_CXX_COMPILER=" + TOOLS["c++"]],
                                capture_output=True, text=True, env=env)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        found = re.search(r"ccache: using (\S.*)", result.stdout)
        self.assertIsNotNone(found, result.stdout)
        # CMake searches the HINTS before PATH and before the system folders, so the hinted one is the one chosen whether or not the machine has a real ccache in a folder of
        # the restricted PATH (a runner that installed it in /usr/bin: that folder must stay, it holds cmake, make and the compiler)
        self.assertEqual(found.group(1).strip(), hinted)

    def machine_has_a_ccache(self):
        """A real ccache anywhere that CMake's search could find it without a hint (on the PATH this test run started with, or a standard folder)."""
        if shutil.which("ccache"):
            return True
        return any(os.path.exists(os.path.join(d, "ccache")) for d in ("/usr/bin", "/usr/local/bin", "/opt/homebrew/bin", "/bin", "/opt/local/bin"))

    def test_without_any_ccache_the_message_says_not_found_and_nothing_launches_the_compiler(self):
        if self.machine_has_a_ccache():
            self.skipTest("this machine has a ccache that CMake's own search would find")
        home = os.path.join(self.tmp.name, "emptyhome")
        os.makedirs(home)
        env = self.restricted_environment(home)
        build = os.path.join(self.tmp.name, "absent")
        result = subprocess.run([CMAKE, "-S", REPO, "-B", build, "-DBUILD_TESTS=OFF", "-DANTS_BUILD_APP=OFF", "-DCMAKE_CXX_COMPILER=" + TOOLS["c++"]],
                                capture_output=True, text=True, env=env)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("ccache: not used (not found)", result.stdout)
        self.assertNotIn("ccache: using", result.stdout)

    def test_the_build_id_target_and_the_generated_header_do_not_depend_on_ccache(self):
        out, build = self.configure("with", path_first=self.fake_bin)
        out2, build2 = self.configure("without", "-DANTS_USE_CCACHE=OFF", path_first=self.fake_bin)
        self.assertEqual(read(os.path.join(build, "generated", "ants_app", "version.hpp")), read(os.path.join(build2, "generated", "ants_app", "version.hpp")))


if __name__ == "__main__":
    unittest.main()
