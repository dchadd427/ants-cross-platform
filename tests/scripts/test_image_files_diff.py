#!/usr/bin/env python3
"""Tests of tools/image_manifest.py and tools/image_files_diff.sh, the check that the files of the Docker images did not change (run by ./run_tests.sh --fast and by the CI).

The script builds the images from the Dockerfile of a base revision and from this tree's, exports their file systems and compares them. There is no Docker daemon here, so:

  - the manifest: every entry with type, mode, owner, size, link target and sha256; the stamps of the pages (the build time and the build date) are replaced before the hash, in the listed files only
  - the files of two builds of one Dockerfile that are never the same (the linked WebAssembly programs and their JavaScript: CI built the old Dockerfile twice and they differed): only a signature is
    compared (a wasm's data section, its sections and size; the JavaScript with its numbers made one), and a change of what a program holds or says is found
  - the comparison: the same files are the same; a changed byte, a changed mode or owner, a file on one side only, a link that points elsewhere, a stamp that appears or goes, a stamp in a file
    that is not listed, and a changed configuration (user, entrypoint, command, ports, environment) are each a difference with exit status 1; an empty export is refused
  - the CI job: a job of its own (not one of the five required checks, waited for by the deploy like every job, which it passes at once on a push) that runs for a pull request that changes a Dockerfile, the program of the pre-warming or this check
  - the script, run against a stand-in `docker` (a small program that builds nothing and exports the files that the test chose) and a real git repository: the same images pass, a difference of one
    byte in either image fails and names the file, a difference of the configuration fails, a failed build is status 2, and the base is read from git
"""
import gzip
import io
import json
import os
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
import image_manifest as im  # noqa: E402

HTML = "usr/share/nginx/html/"


def tar_of(files, extra=None):
    """The tar of an image: {path: bytes | (bytes, mode, uid)} and extra [(name, kind, linkname)]; padded to enough entries to count as an image."""
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w") as tar:
        entries = dict(files)
        for i in range(im.MINIMUM_ENTRIES):
            entries.setdefault("filler/file%02d" % i, b"filler %d" % i)
        for name in sorted(entries):
            value = entries[name]
            data, mode, uid = value if isinstance(value, tuple) else (value, 0o644, 0)
            info = tarfile.TarInfo(name)
            info.size, info.mode, info.uid, info.mtime = len(data), mode, uid, 12345
            tar.addfile(info, io.BytesIO(data))
        for name, kind, target in (extra or []):
            info = tarfile.TarInfo(name)
            info.type = {"symlink": tarfile.SYMTYPE, "dir": tarfile.DIRTYPE}[kind]
            info.linkname = target
            info.mode = 0o755
            tar.addfile(info)
    return buffer.getvalue()


def manifest(files, extra=None):
    return im.manifest(io.BytesIO(tar_of(files, extra)))


PAGE = (b"<script>var ANTS_BUILD_VER = '%d';</script><script src=\"index.js?v=%d\"></script><span id=\"game-build-date\">(2026-11-11 10:%02d PT)</span>")


def page(seconds, minute):
    return PAGE % (1700000000 + seconds, 1700000000 + seconds + 1, minute)


class TheManifest(unittest.TestCase):
    def test_the_same_files_are_the_same(self):
        files = {HTML + "a.js": b"1", "bin/ants": (b"\x7fELF", 0o755, 0)}
        self.assertEqual(im.differences(manifest(files), manifest(files)), [])

    def test_the_stamps_of_the_game_page_do_not_count_and_nothing_else_of_it(self):
        one = manifest({HTML + "index.html": page(0, 1)})
        two = manifest({HTML + "index.html": page(500, 9)})
        self.assertEqual(im.differences(one, two), [])
        self.assertEqual(one[HTML + "index.html"]["stamps"], 3)
        other = manifest({HTML + "index.html": page(500, 9).replace(b"<script>", b"<script async>")})
        self.assertEqual(len(im.differences(one, other)), 2)                    # (size and sha256)

    def test_a_stamp_in_a_page_that_is_not_listed_is_a_difference(self):
        one = manifest({HTML + "other.html": page(0, 1)})
        two = manifest({HTML + "other.html": page(500, 9)})
        self.assertGreaterEqual(len(im.differences(one, two)), 1)

    def test_a_stamp_that_appears_or_goes_is_a_difference(self):
        with_date = manifest({HTML + "lobby.html": b"<span>(2026-11-11 10:01 PT)</span>"})
        without = manifest({HTML + "lobby.html": b"<span>(<build date>)</span>"})
        self.assertTrue(im.differences(with_date, without), "a page that has the text of the replacement already is not the same as one that has a stamp")
        none = manifest({HTML + "lobby.html": b"<span></span>"})
        self.assertTrue(im.differences(with_date, none))

    def test_each_kind_of_difference_is_found_and_named(self):
        base = manifest({HTML + "a.js": b"1", "bin/x": (b"x", 0o755, 0), "etc/y": b"y"}, [("lib/l", "symlink", "a")])
        for what, new, word in (
            ("content", manifest({HTML + "a.js": b"2", "bin/x": (b"x", 0o755, 0), "etc/y": b"y"}, [("lib/l", "symlink", "a")]), "sha256"),
            ("mode", manifest({HTML + "a.js": b"1", "bin/x": (b"x", 0o700, 0), "etc/y": b"y"}, [("lib/l", "symlink", "a")]), "mode"),
            ("owner", manifest({HTML + "a.js": b"1", "bin/x": (b"x", 0o755, 5), "etc/y": b"y"}, [("lib/l", "symlink", "a")]), "uid"),
            ("link", manifest({HTML + "a.js": b"1", "bin/x": (b"x", 0o755, 0), "etc/y": b"y"}, [("lib/l", "symlink", "b")]), "target"),
            ("a file less", manifest({HTML + "a.js": b"1", "bin/x": (b"x", 0o755, 0)}, [("lib/l", "symlink", "a")]), "only in the base image: etc/y"),
            ("a file more", manifest({HTML + "a.js": b"1", "bin/x": (b"x", 0o755, 0), "etc/y": b"y", "etc/z": b"z"}, [("lib/l", "symlink", "a")]), "only in the new image: etc/z"),
        ):
            with self.subTest(what):
                found = im.differences(base, new)
                self.assertTrue(found)
                self.assertTrue(any(word in line for line in found), found)

    def test_the_modification_time_is_not_compared(self):
        def tarfile_with(mtime):
            buffer = io.BytesIO()
            with tarfile.open(fileobj=buffer, mode="w") as tar:
                for i in range(im.MINIMUM_ENTRIES + 1):
                    info = tarfile.TarInfo("f%d" % i)
                    info.size, info.mtime = 1, mtime
                    tar.addfile(info, io.BytesIO(b"x"))
            return im.manifest(io.BytesIO(buffer.getvalue()))
        self.assertEqual(im.differences(tarfile_with(1), tarfile_with(999999)), [])

    def test_the_list_of_stamped_files_is_short_and_has_only_pages(self):
        self.assertLessEqual(len(im.STAMPED), 8)
        for path in im.STAMPED:
            self.assertTrue(path.startswith(HTML) and path.endswith(".html"), path)

    def test_the_configuration_keeps_what_decides_how_the_image_runs(self):
        inspected = [{"Id": "sha256:1", "Config": {"User": "ants", "Entrypoint": ["/x"], "Cmd": ["--a"], "ExposedPorts": {"80/tcp": {}}, "Env": ["A=1"], "Hostname": "abc", "Image": "sha256:2", "Labels": {"k": "v"}}}]
        kept = im.config(inspected)
        self.assertEqual(kept["User"], "ants")
        self.assertEqual(kept["Entrypoint"], ["/x"])
        self.assertNotIn("Hostname", kept)
        self.assertNotIn("Image", kept)
        changed = json.loads(json.dumps(inspected))
        changed[0]["Config"]["User"] = "root"
        self.assertEqual(len(im.differences(kept, im.config(changed), "web configuration")), 1)
        for key, value in (("Entrypoint", ["/y"]), ("Cmd", []), ("ExposedPorts", {}), ("Env", ["A=2"])):
            changed = json.loads(json.dumps(inspected))
            changed[0]["Config"][key] = value
            self.assertTrue(im.differences(kept, im.config(changed)), key)


def leb128(value):
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        out.append(byte | (0x80 if value else 0))
        if not value:
            return bytes(out)


def wasm(code, data=b"strings of the program v0.16.0", extra_section=None):
    """A small WebAssembly file: a type section, a code section made of `code` (any bytes: the order of the functions) and a data section."""
    def section(number, payload):
        return bytes([number]) + leb128(len(payload)) + payload
    parts = [b"\0asm\1\0\0\0", section(1, b"\1\x60\0\0"), section(10, code), section(11, data)]
    if extra_section:
        parts.append(section(*extra_section))
    return b"".join(parts)


class TheProgramsThatNoBuildRepeats(unittest.TestCase):
    def files(self, **by_name):
        return {HTML + name.replace("lvl_core", "lvl-core").replace("_", ".").replace("lvl-core", "lvl_core"): value for name, value in by_name.items()}

    def test_the_same_program_with_its_functions_in_another_order_is_the_same_and_one_with_other_text_is_not(self):
        one = manifest(self.files(index_wasm=wasm(b"AAAA" + b"B" * 300), lvl_core_wasm=wasm(b"CC")))
        two = manifest(self.files(index_wasm=wasm(b"B" * 300 + b"AAAA" + b"x" * 5), lvl_core_wasm=wasm(b"C" * 40)))
        self.assertEqual(im.differences(one, two), [])
        other = manifest(self.files(index_wasm=wasm(b"B" * 300 + b"AAAA", data=b"strings of the program v0.0.0"), lvl_core_wasm=wasm(b"CC")))
        found = im.differences(one, other)
        self.assertEqual(len(found), 1)
        self.assertIn("index.wasm differs in signature", found[0])

    def test_a_program_that_grew_or_gained_a_section_is_found(self):
        base = manifest(self.files(index_wasm=wasm(b"A" * 100)))
        self.assertTrue(im.differences(base, manifest(self.files(index_wasm=wasm(b"A" * 100 + b"B" * 70)))))
        self.assertTrue(im.differences(base, manifest(self.files(index_wasm=wasm(b"A" * 100, extra_section=(0, b"\4name"))))))

    def test_a_compressed_copy_is_read_through_and_a_file_that_is_no_program_is_not_ignored(self):
        one = manifest(self.files(index_wasm_gz=gzip.compress(wasm(b"AA"), mtime=1), index_js_gz=gzip.compress(b"f(12)", mtime=1)))
        two = manifest(self.files(index_wasm_gz=gzip.compress(wasm(b"A" * 9), mtime=2), index_js_gz=gzip.compress(b"f(99)", mtime=2)))
        self.assertEqual(im.differences(one, two), [])
        broken = manifest(self.files(index_wasm=b"not a program", lvl_core_wasm=wasm(b"A")))
        self.assertTrue(im.differences(broken, manifest(self.files(index_wasm=b"not a program either", lvl_core_wasm=wasm(b"A")))))

    def test_the_javascript_is_the_same_apart_from_its_numbers(self):
        one = manifest(self.files(index_js=b"var a=f(1234,5);g('index.wasm')"))
        self.assertEqual(im.differences(one, manifest(self.files(index_js=b"var a=f(98,76543);g('index.wasm')"))), [])
        self.assertTrue(im.differences(one, manifest(self.files(index_js=b"var a=f(1234,5);g('index.data')"))))
        self.assertTrue(im.differences(one, manifest(self.files(index_js=b"var a=f(1234,5);g('index.wasm');"))), "a longer file is another file")

    def test_the_list_is_short_and_has_only_the_linked_programs_and_their_script(self):
        self.assertEqual(sorted(p[len(HTML):] for p in im.LOOSE), ["index.js", "index.js.gz", "index.wasm", "index.wasm.gz", "lvl_core.js", "lvl_core.wasm"])
        self.assertTrue(set(im.LOOSE).isdisjoint(im.STAMPED))

    def test_the_minified_game_page_is_read_too(self):
        text = b"<script>var ANTS_BUILD_VER=\"%d\";var x='index.js?v=%d'</script><span id=game-build-date>(2026-11-10 %02d:57 PT)</span>"
        one = manifest({HTML + "index.html": text % (1731000001, 1731000002, 21)})
        two = manifest({HTML + "index.html": text % (1731000999, 1731000998, 22)})
        self.assertEqual(im.differences(one, two), [])
        self.assertEqual(one[HTML + "index.html"]["stamps"], 3)


class TheCommandLine(unittest.TestCase):
    def run_tool(self, *args, stdin=b""):
        return subprocess.run([sys.executable, os.path.join(REPO, "tools", "image_manifest.py"), *args], input=stdin, capture_output=True, timeout=60)

    def test_an_export_with_hardly_any_file_is_refused(self):
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode="w") as tar:
            info = tarfile.TarInfo("one")
            tar.addfile(info, io.BytesIO(b""))
        done = self.run_tool("manifest", stdin=buffer.getvalue())
        self.assertEqual(done.returncode, 2)
        self.assertIn(b"only 1 entries", done.stderr)

    def test_compare_exits_with_1_for_a_difference_and_0_for_none(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = []
            for name, data in (("a", b"1"), ("b", b"1"), ("c", b"2")):
                done = self.run_tool("manifest", stdin=tar_of({"x/file": data}))
                self.assertEqual(done.returncode, 0, done.stderr)
                paths.append(os.path.join(tmp, name))
                with open(paths[-1], "wb") as f:
                    f.write(done.stdout)
            same = self.run_tool("compare", paths[0], paths[1])
            self.assertEqual(same.returncode, 0, same.stdout)
            different = self.run_tool("compare", paths[0], paths[2], "--label", "web files")
            self.assertEqual(different.returncode, 1)
            self.assertIn(b"web files: x/file differs in sha256", different.stdout)
            self.assertEqual(self.run_tool("compare", paths[0], os.path.join(tmp, "nothing")).returncode, 2)

    def test_the_usage_is_a_status_2(self):
        self.assertEqual(self.run_tool().returncode, 2)
        self.assertEqual(self.run_tool("manifest", "extra").returncode, 2)


FAKE_DOCKER = r'''#!/usr/bin/env python3
"""A stand-in for docker: builds nothing; `create` names the image, `export` writes the tar that the test chose (FAKE_STATE/<image>.tar), `image inspect` the configuration that it chose."""
import json, os, sys
state = os.environ["FAKE_STATE"]
args = sys.argv[1:]
with open(os.path.join(state, "calls.log"), "a") as log:
    log.write(" ".join(args) + "\n")
if args[0] == "build":
    tag = args[args.index("-t") + 1]
    dockerfile = args[args.index("-f") + 1]
    if os.environ.get("FAKE_FAIL") and os.environ["FAKE_FAIL"] in tag:
        sys.exit(1)
    with open(dockerfile, encoding="utf-8") as f:
        which = f.read().strip()               # the test wrote "base" or "new" plus the image into the Dockerfile
    with open(os.path.join(state, tag + ".which"), "w") as f:
        f.write(which)
elif args[0] == "create":
    print(args[1])
elif args[0] == "export":
    with open(os.path.join(state, args[1] + ".which")) as f:
        which = f.read().strip()
    with open(os.path.join(state, which + ".tar"), "rb") as f:
        sys.stdout.buffer.write(f.read())
elif args[:2] == ["image", "inspect"]:
    with open(os.path.join(state, args[2] + ".which")) as f:
        which = f.read().strip()
    with open(os.path.join(state, which + ".config")) as f:
        print(f.read())
'''


@unittest.skipUnless(shutil.which("git") and shutil.which("bash"), "needs git and bash")
class TheScript(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ants_imgdiff_")
        self.repo = os.path.join(self.tmp, "repo")
        os.makedirs(os.path.join(self.repo, "tools"))
        for name in ("image_files_diff.sh", "image_manifest.py"):
            shutil.copy(os.path.join(REPO, "tools", name), os.path.join(self.repo, "tools", name))
        self.state = os.path.join(self.tmp, "state")
        os.makedirs(self.state)
        bindir = os.path.join(self.tmp, "bin")
        os.makedirs(bindir)
        fake = os.path.join(bindir, "docker")
        with open(fake, "w", encoding="utf-8") as f:
            f.write(FAKE_DOCKER)
        os.chmod(fake, os.stat(fake).st_mode | stat.S_IXUSR)
        self.env = dict(os.environ, PATH=bindir + os.pathsep + os.environ["PATH"], FAKE_STATE=self.state, GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_SYSTEM=os.devnull)
        # a repository whose base has both Dockerfiles ("base" in them, which the stand-in reads to tell the two builds apart) and whose working tree has the new ones
        for name in ("Dockerfile", "Dockerfile.server"):
            with open(os.path.join(self.repo, name), "w", encoding="utf-8") as f:
                f.write("base\n")
        for command in (["init", "-q", "-b", "main"], ["add", "."], ["-c", "user.name=t", "-c", "user.email=t@example.org", "commit", "-q", "-m", "base"]):
            subprocess.run(["git", "-C", self.repo, *command], check=True, env=self.env, capture_output=True)
        for name in ("Dockerfile", "Dockerfile.server"):
            with open(os.path.join(self.repo, name), "w", encoding="utf-8") as f:
                f.write("new\n")
        self.config = json.dumps([{"Config": {"User": "ants", "Entrypoint": ["/x"], "Cmd": ["--a"], "ExposedPorts": {"80/tcp": {}}, "Env": ["A=1"]}}])
        self.write_images()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def write_images(self, base_files=None, new_files=None, new_config=None):
        files = {HTML + "index.html": page(0, 1), "usr/local/bin/ants_server": (b"binary", 0o755, 0)}
        for which, extra, config in (("base", base_files, self.config), ("new", new_files, new_config or self.config)):
            with open(os.path.join(self.state, which + ".tar"), "wb") as f:
                f.write(tar_of(dict(files, **(extra or {}))) if which == "base" else tar_of(dict(files, **(extra or {}), **{HTML + "index.html": page(900, 7)})))
            with open(os.path.join(self.state, which + ".config"), "w", encoding="utf-8") as f:
                f.write(config)

    def run_script(self, *args, **env):
        return subprocess.run(["bash", os.path.join(self.repo, "tools", "image_files_diff.sh"), "--base", "main", *args], cwd=self.repo, env=dict(self.env, **env), capture_output=True, text=True, timeout=120)

    def test_the_same_images_pass_with_stamps_that_differ(self):
        done = self.run_script()
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("the images are the same", done.stdout)
        calls = open(os.path.join(self.state, "calls.log"), encoding="utf-8").read()
        self.assertEqual(calls.count("build -f"), 4, "the base and the new image of both Dockerfiles")
        self.assertEqual(calls.count("--build-arg ANTS_BUILD_ID=image-diff"), 4, "the same build id for all four")
        self.assertIn("build -f Dockerfile.server --build-arg", calls)                                      # (the new one is the working tree's own file, the context is the folder that the script runs in)
        self.assertIn("Dockerfile.server.base --build-arg", calls)                                          # (the base one is a copy of the base revision's file in a scratch folder)
        self.assertEqual(calls.count(" -t ants-image-diff-"), 4)
        self.assertEqual(calls.count("\nimage rm -f "), 1, "the images are removed at the end")

    def test_a_changed_byte_fails_and_names_the_file(self):
        self.write_images(new_files={"usr/local/bin/ants_server": (b"binarz", 0o755, 0)})
        done = self.run_script()
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("usr/local/bin/ants_server differs in sha256", done.stdout)
        self.assertIn("DIFFER", done.stderr)

    def test_a_file_that_is_only_in_one_image_fails(self):
        self.write_images(base_files={"srv/maps/old.lvl": b"x"})
        done = self.run_script("--server")
        self.assertEqual(done.returncode, 1)
        self.assertIn("only in the base image: srv/maps/old.lvl", done.stdout)

    def test_a_changed_configuration_fails(self):
        self.write_images(new_config=self.config.replace('"ants"', '"root"'))
        done = self.run_script("--web")
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("web configuration: User differs", done.stdout)

    def test_one_image_can_be_named(self):
        done = self.run_script("--web")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        calls = open(os.path.join(self.state, "calls.log"), encoding="utf-8").read()
        self.assertEqual(calls.count("build -f"), 2)
        self.assertNotIn("Dockerfile.server", calls)

    def test_a_build_that_fails_is_status_2_and_no_comparison_passes(self):
        done = self.run_script(FAKE_FAIL="-new")
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)
        self.assertNotIn("the images are the same", done.stdout)

    def test_a_base_without_the_dockerfile_is_status_2(self):
        done = subprocess.run(["bash", os.path.join(self.repo, "tools", "image_files_diff.sh"), "--base", "nothing-like-this"], cwd=self.repo, env=self.env, capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 2)

    def test_an_unknown_argument_is_status_2(self):
        done = subprocess.run(["bash", os.path.join(self.repo, "tools", "image_files_diff.sh"), "--nonsense"], cwd=self.repo, env=self.env, capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 2)

    def test_the_script_is_a_bash_script_that_passes_a_syntax_check_and_runs_from_the_repository_root(self):
        self.assertEqual(subprocess.run(["bash", "-n", os.path.join(REPO, "tools", "image_files_diff.sh")]).returncode, 0)
        text = open(os.path.join(REPO, "tools", "image_files_diff.sh"), encoding="utf-8").read()
        self.assertTrue(text.startswith("#!/usr/bin/env bash\n"))
        self.assertIn('git show "$base:$file"', text)
        self.assertIn("docker export", text)


class TheCiJob(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, ".github", "workflows", "ci.yml"), encoding="utf-8") as f:
            text = f.read()
        start = text.index("\n  image_files:\n")
        cls.job = text[start:text.index("\n  deploy:\n", start)]
        cls.text = text

    def test_it_runs_the_script_against_origin_main_when_a_dockerfile_or_the_check_changed(self):
        self.assertIn("bash tools/image_files_diff.sh --base origin/main", self.job)
        self.assertIn("if: steps.decide.outputs.run == 'true'", self.job)
        self.assertIn('[ "$EVENT" = pull_request ]', self.job)
        self.assertIn("fetch-depth: 0", self.job)                  # (origin/main must be there)
        for path in ("Dockerfile", "Dockerfile\\.server", "docker/emsdk_warm\\.cpp", "tools/image_files_diff\\.sh", "tools/image_manifest\\.py"):
            self.assertIn(path, self.job)
        self.assertNotIn("Dockerfile\\.hub", self.job, "the hub's image is not part of the check")

    def test_it_is_not_a_required_check_and_the_deploy_waits_for_it_like_for_every_job(self):
        self.assertIn("name: Image files unchanged by a Dockerfile change", self.job)
        header = self.text[:self.text.index("\njobs:\n")]
        self.assertNotIn("Image files unchanged", header)
        deploy = self.text[self.text.index("\n  deploy:\n"):]
        self.assertIn("web, image_files]", deploy)               # (on a push it only says that nothing changed)
        self.assertIn("timeout-minutes:", self.job)


if __name__ == "__main__":
    unittest.main()
