#!/usr/bin/env python3
"""Tests of tools/image_manifest.py and tools/image_files_diff.sh, the check that the files of the Docker images did not change (run by ./run_tests.sh --fast and by the CI).

The script builds the images from the Dockerfile of a base revision and from this tree's, exports their file systems and compares them. There is no Docker daemon here, so:

  - the manifest: every entry with type, mode, owner, size, link target and sha256; the stamps of the pages (the build time and the build date) are replaced before the hash, in the listed files only
  - the files that a link makes (the WebAssembly programs and their JavaScript: two links of the same objects do not always give the same bytes, which CI showed): the final image keeps only their type,
    mode, owner and a size within one percent; what they are made of is compared as the link's inputs, listed inside the builder stage of both builds (tools/link_inputs.py: objects, archives, libraries,
    link command lines), and, when the recipe holds, as the programs of a link made deterministic (tools/relink_outputs.sh)
  - the comparison: the same files are the same; a changed byte, a changed mode or owner, a file on one side only, a link that points elsewhere, a stamp that appears or goes, a stamp in a file
    that is not listed, and a changed configuration (user, entrypoint, command, ports, environment) are each a difference with exit status 1; an empty export is refused
  - the CI job: a job of its own (not one of the five required checks, and nothing waits for it) that runs for a pull request that changes a Dockerfile, the program of the pre-warming or this check, against the pull request's base,
    cannot be made to pass by `|| true` or `continue-on-error`, and has the trigger that it has
  - the script, run against a stand-in `docker` (a small program that builds nothing and exports the files that the test chose) and a real git repository: the same images pass, a difference of one
    byte in either image fails and names the file, so does a link input or a link command line that differs, a difference of the configuration fails, a failed build is status 2, and the base is read from git
"""
import io
import json
import os
import re
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
import link_inputs as li  # noqa: E402

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

    def test_the_date_of_the_bar_at_the_top_of_a_page_does_not_count_and_nothing_else_of_it(self):
        def bar(minute, build="abc1234"):
            return ('<header class="sb" data-version="v1.2.3" data-build="%s" data-built="2026-11-11 10:%02d PT">' % (build, minute)).encode("ascii")
        for name in ("matches", "match", "live", "legal", "mapmaker", "changelog"):
            with self.subTest(name):
                path = HTML + name + ".html"
                one, two = manifest({path: bar(1)}), manifest({path: bar(9)})
                self.assertEqual(im.differences(one, two), [])
                self.assertEqual(one[path]["stamps"], 1)
                self.assertTrue(im.differences(one, manifest({path: bar(1, "other")})), "the build id is compared")
                self.assertTrue(im.differences(one, manifest({path: b"<header>"})), "a page that loses its stamp differs")

    def test_every_page_that_the_builder_stamps_with_the_build_date_is_listed(self):
        with open(os.path.join(REPO, "Dockerfile"), encoding="utf-8") as handle:
            docker = handle.read()
        line = re.search(r"sed -i \"s/@@BUILD_ID@@[^&\\n]*", docker.replace("\\\n", " ")).group(0)
        pages = {HTML + name + ".html" for name in re.findall(r"/src/web/(\w+)\.html", line)}
        pages.add(HTML + "index.html")
        self.assertGreaterEqual(len(pages), 7)
        for path in sorted(pages):
            self.assertIn(path, im.STAMPED)

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
        self.assertLessEqual(len(im.STAMPED), 10)
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


class TheLinkedFilesAreNotComparedByContent(unittest.TestCase):
    def files(self, **by_name):
        return {HTML + name.replace("lvl_core", "lvl-core").replace("_", ".").replace("lvl-core", "lvl_core"): value for name, value in by_name.items()}

    def test_the_list_is_short_and_has_only_what_a_link_makes(self):
        self.assertEqual(sorted(p[len(HTML):] for p in im.LINKED), ["index.js", "index.js.gz", "index.wasm", "index.wasm.gz", "lvl_core.js", "lvl_core.wasm"])
        self.assertTrue(set(im.LINKED).isdisjoint(im.STAMPED))
        self.assertEqual(im.SIZE_PERCENT, 1)

    def test_the_same_program_in_another_variant_is_the_same_here(self):
        """Three variants of one link differ by a few bytes (3,894,642 / 647 / 653 in CI) and by their content: that is judged on the link's inputs, not here."""
        one = manifest(self.files(index_wasm=b"A" * 3894642, index_js=b"x" * 90000, lvl_core_wasm=b"C" * 200000))
        two = manifest(self.files(index_wasm=b"B" * 3894653, index_js=b"y" * 90006, lvl_core_wasm=b"D" * 200000))
        self.assertEqual(im.differences(one, two), [])

    def test_a_program_that_is_far_bigger_or_smaller_or_gone_is_found(self):
        base = manifest(self.files(index_wasm=b"A" * 100000, index_js=b"x" * 5000))
        self.assertTrue(im.differences(base, manifest(self.files(index_wasm=b"A" * 102000, index_js=b"x" * 5000))))
        self.assertTrue(im.differences(base, manifest(self.files(index_wasm=b"A" * 98000, index_js=b"x" * 5000))))
        self.assertTrue(im.differences(base, manifest(self.files(index_wasm=b"A" * 100000))))
        self.assertTrue(im.differences(base, manifest(self.files(index_wasm=b"A" * 100000, index_js=b"x" * 5000, lvl_core_js=b"z"))))
        self.assertEqual(im.differences(base, manifest(self.files(index_wasm=b"A" * 100900, index_js=b"x" * 5000))), [])

    def test_the_mode_and_the_owner_of_a_linked_file_still_count(self):
        base = manifest(self.files(index_wasm=b"A" * 1000))
        self.assertTrue(im.differences(base, manifest(self.files(index_wasm=(b"A" * 1000, 0o600, 0)))))
        self.assertTrue(im.differences(base, manifest(self.files(index_wasm=(b"A" * 1000, 0o644, 7)))))

    def test_the_files_that_are_not_made_by_a_link_are_compared_byte_for_byte(self):
        base = manifest(self.files(index_data=b"D" * 1000, index_data_gz=b"G" * 100, index_html=b"<p>"))
        self.assertTrue(im.differences(base, manifest(self.files(index_data=b"E" * 1000, index_data_gz=b"G" * 100, index_html=b"<p>"))))
        self.assertTrue(im.differences(base, manifest(self.files(index_data=b"D" * 1000, index_data_gz=b"H" * 100, index_html=b"<p>"))))

    def test_the_minified_game_page_is_read_too(self):
        text = b"<script>var ANTS_BUILD_VER=\"%d\";var x='index.js?v=%d'</script><span id=game-build-date>(2026-11-10 %02d:57 PT)</span>"
        one = manifest({HTML + "index.html": text % (1731000001, 1731000002, 21)})
        two = manifest({HTML + "index.html": text % (1731000999, 1731000998, 22)})
        self.assertEqual(im.differences(one, two), [])
        self.assertEqual(one[HTML + "index.html"]["stamps"], 3)


def hashed(name, character="a"):
    return "file\t%s\t%s" % (name, character * 64)


def inputs_listing(**changes):
    """A listing that has what `listing(..., "inputs")` asks for; `changes` {path: sha character, or None to drop it, or 'link:<text>' for a command line}."""
    lines = {}
    for i in range(45):
        lines["build_web/CMakeFiles/o%02d.o" % i] = "a"
    for name in ("build_web/src/ants_app/libants_app.a", "build_web/lib/libb.a", "build_web/lib/libc.a"):
        lines[name] = "b"
    for name in ("build_web/src/ants_app/CMakeFiles/ants_app.dir/link.txt", "build_web/lvl_core/CMakeFiles/lvl_core.dir/link.txt", "build_web/src/ants_app/CMakeFiles/ants_app.dir/flags.make",
                 "build_web/lvl_core/CMakeFiles/lvl_core.dir/flags.make", "build_web/generated/ants_app/version.hpp"):
        lines[name] = "c"
    for name in ("libSDL2.a", "libSDL2_ttf.a", "libfreetype.a", "libharfbuzz.a", "crt1.o", "crt1_reactor.o"):
        lines["/emsdk/upstream/emscripten/cache/sysroot/lib/wasm32-emscripten/" + name] = "d"
    for name in ("build_web/src/ants_app/CMakeFiles/ants_app.dir/objects1.rsp", "build_web/src/ants_app/CMakeFiles/ants_app.dir/linkLibs.rsp", "build_web/lvl_core/CMakeFiles/lvl_core.dir/objects1.rsp"):
        lines[name] = "e"
    out = []
    links = {"build_web/src/ants_app/CMakeFiles/ants_app.dir/link.txt": "em++ -O3 -o index.html a.o b.a", "build_web/lvl_core/CMakeFiles/lvl_core.dir/link.txt": "em++ -Oz -o lvl_core.js c.o"}
    for path, value in changes.items():
        if isinstance(value, str) and value.startswith("link:"):
            links[path] = value[5:]
        elif value is None:
            lines.pop(path, None)
        else:
            lines[path] = value
    for path in sorted(lines):
        out.append(hashed(path, lines[path]))
    for path in sorted(links):
        out.append("link\t%s\t%s" % (path, links[path]))
    return "\n".join(out) + "\n"


RELINK = "".join(hashed(name, character) + "\n" for name, character in (("index.js", "1"), ("index.wasm", "2"), ("lvl_core.js", "3"), ("lvl_core.wasm", "4")))


class TheListingsOfTheLinksInputsAndOutputs(unittest.TestCase):
    OBJECT = "build_web/CMakeFiles/o07.o"
    LINK = "build_web/src/ants_app/CMakeFiles/ants_app.dir/link.txt"

    def differences(self, **changes):
        return im.listing_differences(im.listing(inputs_listing(), "inputs"), im.listing(inputs_listing(**changes), "inputs"), "web link inputs")

    def test_the_same_listing_is_the_same(self):
        self.assertEqual(self.differences(), [])

    def test_a_changed_object_is_named(self):
        found = self.differences(**{self.OBJECT: "e"})
        self.assertEqual(len(found), 1)
        self.assertIn("web link inputs: " + self.OBJECT + " differs: sha256", found[0])

    def test_an_object_that_is_only_on_one_side_is_found(self):
        self.assertIn("only in the base build: " + self.OBJECT, self.differences(**{self.OBJECT: None})[0])
        self.assertIn("only in the new build: build_web/CMakeFiles/extra.o", self.differences(**{"build_web/CMakeFiles/extra.o": "a"})[0])

    def test_a_changed_library_of_emscriptens_cache_is_found(self):
        self.assertTrue(self.differences(**{"/emsdk/upstream/emscripten/cache/sysroot/lib/wasm32-emscripten/libSDL2.a": "f"}))

    def test_a_changed_link_command_line_is_shown(self):
        found = self.differences(**{self.LINK: "link:em++ -O2 -o index.html a.o b.a"})
        self.assertEqual(len(found), 1)
        self.assertIn("the link command line differs", found[0])
        self.assertIn("-O3", found[0])
        self.assertIn("-O2", found[0])

    def test_the_generated_header_and_the_flags_are_inputs_too(self):
        self.assertTrue(self.differences(**{"build_web/generated/ants_app/version.hpp": "e"}))
        self.assertTrue(self.differences(**{"build_web/src/ants_app/CMakeFiles/ants_app.dir/flags.make": "e"}))

    def test_a_listing_that_is_malformed_or_too_small_is_refused_not_compared(self):
        full = inputs_listing()
        for what, text in (("a line that is none", full + "garbage\n"), ("a hash that is none", full + "file\tbuild_web/x.o\tnothex\n"), ("an empty command line", full + "link\tbuild_web/y/link.txt\t \n"),
                           ("nothing", ""), ("too few objects", "\n".join(l for l in full.splitlines() if "/o0" not in l and "/o1" not in l)),
                           ("no SDL2_ttf", "\n".join(l for l in full.splitlines() if "libSDL2_ttf" not in l)),
                           ("no response files", "\n".join(l for l in full.splitlines() if ".rsp" not in l)), ("two response files only", "\n".join(l for l in full.splitlines() if "linkLibs.rsp" not in l)),
                           ("no crt1.o", "\n".join(l for l in full.splitlines() if "/crt1.o" not in l)), ("no crt1_reactor.o", "\n".join(l for l in full.splitlines() if "crt1_reactor.o" not in l)), ("no link lines", "\n".join(l for l in full.splitlines() if not l.startswith("link\t")))):
            with self.subTest(what):
                with self.assertRaises(ValueError):
                    im.listing(text, "inputs")

    def test_the_outputs_of_the_relink_are_compared_and_the_listing_must_hold_all_four(self):
        base = im.listing(RELINK, "relink")
        self.assertEqual(im.listing_differences(base, im.listing(RELINK, "relink"), "web link outputs"), [])
        other = im.listing(RELINK.replace("2" * 64, "9" * 64), "relink")
        found = im.listing_differences(base, other, "web link outputs")
        self.assertEqual(len(found), 1)
        self.assertIn("index.wasm differs", found[0])
        with self.assertRaises(ValueError):
            im.listing("\n".join(RELINK.splitlines()[:3]), "relink")

    def test_the_command_line_compares_two_listings(self):
        with tempfile.TemporaryDirectory() as tmp:
            for name, text in (("a", inputs_listing()), ("b", inputs_listing()), ("c", inputs_listing(**{self.OBJECT: "e"})), ("small", "file\tx\t" + "a" * 64 + "\n")):
                with open(os.path.join(tmp, name), "w", encoding="utf-8") as f:
                    f.write(text)
            def run(*args):
                return subprocess.run([sys.executable, os.path.join(REPO, "tools", "image_manifest.py"), *args], capture_output=True, text=True, timeout=60)
            self.assertEqual(run("inputs", os.path.join(tmp, "a"), os.path.join(tmp, "b")).returncode, 0)
            different = run("inputs", os.path.join(tmp, "a"), os.path.join(tmp, "c"), "--label", "web link inputs")
            self.assertEqual(different.returncode, 1)
            self.assertIn("web link inputs: " + self.OBJECT + " differs", different.stdout)
            self.assertEqual(run("inputs", os.path.join(tmp, "a"), os.path.join(tmp, "small")).returncode, 2)
            self.assertEqual(run("inputs", os.path.join(tmp, "a"), os.path.join(tmp, "nothing")).returncode, 2)
            self.assertEqual(run("relink", os.path.join(tmp, "a"), os.path.join(tmp, "b")).returncode, 2, "an inputs listing is no listing of programs")


def leb(value):
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        out.append(byte | (0x80 if value else 0))
        if not value:
            return bytes(out)


def section(number, payload):
    return bytes([number]) + leb(len(payload)) + payload


def custom(name, payload):
    return section(0, leb(len(name)) + name.encode("ascii") + payload)


def wasm_object(code, debug, reloc_debug=b"\1"):
    """A small WebAssembly object: a code section and the sections of its debug information (the folder of the compile is in the debug strings), a producers section that is kept."""
    return (b"\0asm\1\0\0\0" + section(10, code) + custom("producers", b"clang 19") + custom("reloc.CODE", b"\0") + custom(".debug_info", debug) + custom(".debug_str", debug)
            + custom("reloc..debug_info", reloc_debug))


def archive(members, long_names=False, symbols=b"symbols"):
    """An `ar` archive (GNU names) of [(name, bytes)] with a symbol table member, as llvm-ar makes it."""
    def member(name, body):
        return ("%-16s%-12s%-6s%-6s%-8s%-10d`\n" % (name, "0", "0", "0", "644", len(body))).encode("ascii") + body + (b"\n" if len(body) & 1 else b"")
    out = b"!<arch>\n" + member("/", symbols)
    table = b""
    entries = []
    for name, body in members:
        if long_names and len(name) > 15:
            entries.append(("/%d" % len(table), body))
            table += name.encode("ascii") + b"/\n"
        else:
            entries.append((name + "/", body))
    if table:
        out += member("//", table)
    return out + b"".join(member(name, body) for name, body in entries)


class TheLibrariesOfEmscriptensCache(unittest.TestCase):
    """The ports are compiled with debug information that holds the folder of their compile: a library is compared without it, and with nothing else left out."""

    def test_two_libraries_that_differ_in_their_debug_information_only_are_the_same(self):
        one = archive([("a.c.o", wasm_object(b"code a", b"debug /warm/aaa")), ("b.c.o", wasm_object(b"code b", b"debug /warm"))], symbols=b"symbols at 100")
        two = archive([("a.c.o", wasm_object(b"code a", b"debug /src/build_web/src/ants_app/aaa", reloc_debug=b"\2\3")), ("b.c.o", wasm_object(b"code b", b"debug /src/build_web/src/ants_app"))], symbols=b"symbols at 140")
        self.assertNotEqual(one, two)
        self.assertEqual(li.library_digest(one), li.library_digest(two))

    def test_an_object_of_the_cache_is_the_same_without_its_debug_information_and_not_the_same_in_its_code(self):
        self.assertEqual(li.object_digest(wasm_object(b"start", b"debug /warm")), li.object_digest(wasm_object(b"start", b"debug /src/build_web", reloc_debug=b"\2\3")))
        self.assertNotEqual(li.object_digest(wasm_object(b"start", b"d")), li.object_digest(wasm_object(b"START", b"d")))

    def test_a_library_that_differs_in_code_or_names_or_members_is_not_the_same(self):
        base = archive([("a.c.o", wasm_object(b"code a", b"d")), ("b.c.o", wasm_object(b"code b", b"d"))])
        for what, other in (("code", archive([("a.c.o", wasm_object(b"code A", b"d")), ("b.c.o", wasm_object(b"code b", b"d"))])),
                            ("a name", archive([("a.c.o", wasm_object(b"code a", b"d")), ("c.c.o", wasm_object(b"code b", b"d"))])),
                            ("a member less", archive([("a.c.o", wasm_object(b"code a", b"d"))])),
                            ("the order", archive([("b.c.o", wasm_object(b"code b", b"d")), ("a.c.o", wasm_object(b"code a", b"d"))]))):
            with self.subTest(what):
                self.assertNotEqual(li.library_digest(base), li.library_digest(other))

    def test_the_producers_section_and_the_linking_data_are_kept(self):
        one = wasm_object(b"code", b"d")
        two = one.replace(b"clang 19", b"clang 20")
        self.assertNotEqual(li.library_digest(archive([("a.o", one)])), li.library_digest(archive([("a.o", two)])))

    def test_long_names_and_objects_that_are_not_webassembly_are_read(self):
        one = archive([("a_long_member_name_of_a_port.c.o", wasm_object(b"c", b"d"))], long_names=True)
        two = archive([("a_long_member_name_of_a_port.c.o", wasm_object(b"c", b"e"))], long_names=True)
        self.assertEqual(li.library_digest(one), li.library_digest(two))
        self.assertEqual([n for n, _ in li.archive_members(one)], ["a_long_member_name_of_a_port.c.o"])
        self.assertNotEqual(li.library_digest(archive([("x.o", b"BC bitcode 1")])), li.library_digest(archive([("x.o", b"BC bitcode 2")])), "other objects are compared as they are")

    def test_a_file_that_is_no_archive_is_hashed_whole(self):
        self.assertNotEqual(li.library_digest(b"not an archive 1"), li.library_digest(b"not an archive 2"))

    def test_the_debug_sections_are_the_only_ones_dropped(self):
        data = wasm_object(b"code", b"debug")
        kept = li.object_without_debug(data)
        self.assertIn(b"producers", kept)
        self.assertIn(b"reloc.CODE", kept)
        self.assertNotIn(b".debug_info", kept)
        self.assertNotIn(b"reloc..debug_info", kept)
        self.assertEqual(li.object_without_debug(b"BC\xc0\xde"), b"BC\xc0\xde")


@unittest.skipUnless(shutil.which("sh") and shutil.which("sha256sum"), "needs sh and sha256sum")
class TheScriptsThatRunInsideTheBuilderStage(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ants_inputs_")
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.src = os.path.join(self.tmp, "src")
        self.cache = os.path.join(self.tmp, "cache")
        self.bin = os.path.join(self.tmp, "bin")
        for path in (self.bin, os.path.join(self.cache, "sysroot", "lib", "wasm32-emscripten", "lto")):
            os.makedirs(path)
        with open(os.path.join(self.bin, "em-config"), "w", encoding="utf-8") as f:
            f.write("#!/bin/sh\necho '%s'\n" % self.cache)
        os.chmod(os.path.join(self.bin, "em-config"), 0o755)
        self.env = dict(os.environ, PATH=self.bin + os.pathsep + os.environ["PATH"], ANTS_SRC=self.src)

    def put(self, path, data):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)

    def tree(self):
        for i in range(3):
            self.put(os.path.join(self.src, "build_web", "src", "ants_app", "CMakeFiles", "ants_app.dir", "f%d.cpp.o" % i), b"object %d" % i)
        self.put(os.path.join(self.src, "build_web", "src", "ants_app", "libants_app.a"), b"archive")
        self.put(os.path.join(self.src, "build_web", "src", "ants_app", "CMakeFiles", "ants_app.dir", "link.txt"), b"em++ -O3 -o index.html f0.o\n")
        self.put(os.path.join(self.src, "build_web", "lvl_core", "CMakeFiles", "lvl_core.dir", "link.txt"), b"em++ -Oz -o lvl_core.js\n")
        self.put(os.path.join(self.src, "build_web", "src", "ants_app", "CMakeFiles", "ants_app.dir", "flags.make"), b"flags")
        self.put(os.path.join(self.src, "build_web", "generated", "ants_app", "version.hpp"), b'"v0.16.0"')
        self.put(os.path.join(self.src, "build_web", "src", "ants_app", "CMakeFiles", "ants_app.dir", "objects1.rsp"), b"f0.cpp.o f1.cpp.o\n")
        self.put(os.path.join(self.src, "build_web", "src", "ants_app", "CMakeFiles", "ants_app.dir", "linkLibs.rsp"), b"-lwebsocket.js libants_app.a\n")
        self.put(os.path.join(self.src, "build_web", "lvl_core", "CMakeFiles", "lvl_core.dir", "objects1.rsp"), b"core.o\n")
        self.put(os.path.join(self.src, "build_web", "src", "ants_app", "index.wasm"), b"the program: not an input")
        self.put(os.path.join(self.cache, "sysroot", "lib", "wasm32-emscripten", "crt1.o"), wasm_object(b"start", b"debug of /warm"))
        self.put(os.path.join(self.cache, "sysroot", "lib", "wasm32-emscripten", "crt1_reactor.o"), wasm_object(b"reactor", b"debug of /warm"))
        self.put(os.path.join(self.cache, "sysroot", "lib", "wasm32-emscripten", "libSDL2.a"), archive([("SDL.c.o", wasm_object(b"code", b"debug of /warm"))]))
        self.put(os.path.join(self.cache, "sysroot", "lib", "wasm32-emscripten", "lto", "libc.a"), archive([("bitcode.o", b"BC\xc0\xde bitcode")]))
        self.put(os.path.join(self.cache, "sysroot", "include", "x.h"), b"header: not an input")

    def run_script(self, name, **env):
        return subprocess.run([sys.executable if name.endswith(".py") else "sh", os.path.join(REPO, "tools", name)], env=dict(self.env, **env), capture_output=True, text=True, timeout=60)

    def test_link_inputs_lists_objects_archives_flags_the_header_and_the_cache_libraries_and_the_command_lines(self):
        self.tree()
        done = self.run_script("link_inputs.py")
        self.assertEqual(done.returncode, 0, done.stderr)
        files = [l.split("\t")[1] for l in done.stdout.splitlines() if l.startswith("file\t")]
        self.assertEqual(files, sorted(files))
        self.assertEqual(len(files), 15)
        for wanted in ("f0.cpp.o", "libants_app.a", "ants_app.dir/link.txt", "lvl_core.dir/link.txt", "flags.make", "version.hpp", "wasm32-emscripten/libSDL2.a", "lto/libc.a", "ants_app.dir/objects1.rsp", "ants_app.dir/linkLibs.rsp", "lvl_core.dir/objects1.rsp", "wasm32-emscripten/crt1.o", "wasm32-emscripten/crt1_reactor.o"):
            self.assertTrue(any(f.endswith(wanted) for f in files), wanted)
        for unwanted in ("index.wasm", "x.h"):
            self.assertFalse(any(f.endswith(unwanted) for f in files), unwanted)
        self.assertEqual(len([l for l in done.stdout.splitlines() if l.startswith("link\t")]), 2)
        self.assertIn("link\tbuild_web/src/ants_app/CMakeFiles/ants_app.dir/link.txt\tem++ -O3 -o index.html f0.o", done.stdout)
        sdl = [l for l in done.stdout.splitlines() if l.endswith("libSDL2.a") or "libSDL2.a\t" in l][0].split("\t")[2]
        self.assertEqual(sdl, li.library_digest(archive([("SDL.c.o", wasm_object(b"code", b"debug of /src/build_web"))])), "a library of the cache is listed without its debug information")
        for line in done.stdout.splitlines():
            if line.startswith("file\t"):
                self.assertRegex(line.split("\t")[2], r"^[0-9a-f]{64}$")

    def test_the_same_tree_gives_the_same_listing_and_a_changed_object_or_command_line_does_not(self):
        self.tree()
        first = self.run_script("link_inputs.py").stdout
        self.assertEqual(first, self.run_script("link_inputs.py").stdout)
        self.put(os.path.join(self.src, "build_web", "src", "ants_app", "CMakeFiles", "ants_app.dir", "f1.cpp.o"), b"object 1, other")
        second = self.run_script("link_inputs.py").stdout
        self.assertNotEqual(first, second)
        self.put(os.path.join(self.src, "build_web", "src", "ants_app", "CMakeFiles", "ants_app.dir", "f1.cpp.o"), b"object 1")
        self.put(os.path.join(self.src, "build_web", "lvl_core", "CMakeFiles", "lvl_core.dir", "link.txt"), b"em++ -O2 -o lvl_core.js\n")
        self.assertNotEqual(first, self.run_script("link_inputs.py").stdout)

    def test_link_inputs_without_a_build_folder_or_a_cache_is_status_2(self):
        self.assertEqual(self.run_script("link_inputs.py").returncode, 2)
        self.tree()
        shutil.rmtree(os.path.join(self.cache, "sysroot"))
        self.assertEqual(self.run_script("link_inputs.py", PATH=self.bin + os.pathsep + os.environ["PATH"]).returncode, 2)

    def fake_cmake(self, makes=True):
        """What cmake does here: an output that is not there is made, one that is there is up to date and left alone."""
        make = "".join("[ -e %s ] || echo %s > %s\n" % (f, name, f) for f, name in (("build_web/src/ants_app/index.js", "js"), ("build_web/src/ants_app/index.wasm", "wasm"), ("build_web/src/ants_app/index.data", "data"),
                                                                                  ("build_web/src/ants_app/index.html", "page"), ("build_web/lvl_core/lvl_core.js", "core"), ("build_web/lvl_core/lvl_core.wasm", "corewasm")))
        self.put(os.path.join(self.bin, "cmake"), ("#!/bin/sh\n" + (make if makes else "exit 0\n")).encode("ascii"))
        os.chmod(os.path.join(self.bin, "cmake"), 0o755)

    def test_relink_removes_the_outputs_links_again_and_prints_the_hashes_of_four_files(self):
        self.tree()
        for name in ("index.html", "index.js", "index.data"):
            self.put(os.path.join(self.src, "build_web", "src", "ants_app", name), b"old")
        self.put(os.path.join(self.src, "build_web", "lvl_core", "lvl_core.js"), b"old")
        self.put(os.path.join(self.src, "build_web", "lvl_core", "lvl_core.wasm"), b"old")
        self.fake_cmake()
        done = self.run_script("relink_outputs.sh")
        self.assertEqual(done.returncode, 0, done.stderr)
        names = [l.split("\t")[1] for l in done.stdout.splitlines()]
        self.assertEqual(names, ["index.js", "index.wasm", "lvl_core.js", "lvl_core.wasm"])
        listing = im.listing(done.stdout, "relink")
        self.assertEqual(len(listing), 4)
        with open(os.path.join(self.src, "build_web", "src", "ants_app", "index.js"), "rb") as f:
            self.assertEqual(f.read(), b"js\n", "the old output was removed and made again")

    def test_relink_that_makes_nothing_or_fails_is_status_2(self):
        self.tree()
        self.fake_cmake(makes=False)
        for name in ("index.js", "index.data", "index.html"):
            self.put(os.path.join(self.src, "build_web", "src", "ants_app", name), b"old")
        self.put(os.path.join(self.src, "build_web", "lvl_core", "lvl_core.js"), b"old")
        done = self.run_script("relink_outputs.sh")
        self.assertEqual(done.returncode, 2, "an output that is not there after the link is not a hash of nothing")
        self.put(os.path.join(self.bin, "cmake"), b"#!/bin/sh\nexit 1\n")
        self.assertEqual(self.run_script("relink_outputs.sh").returncode, 2)
        self.fake_cmake()
        self.put(os.path.join(self.bin, "cmake"), open(os.path.join(self.bin, "cmake"), "rb").read() + b"exit 1\n")      # (a link that made its files and still failed)
        for name in ("index.js", "index.wasm", "index.data", "index.html"):
            os.path.exists(os.path.join(self.src, "build_web", "src", "ants_app", name)) and os.remove(os.path.join(self.src, "build_web", "src", "ants_app", name))
        self.assertEqual(self.run_script("relink_outputs.sh").returncode, 2, "a failed link is a failure even when it left files")

    def test_the_scripts_have_the_modes_and_the_syntax_of_scripts(self):
        self.assertEqual(subprocess.run(["sh", "-n", os.path.join(REPO, "tools", "relink_outputs.sh")]).returncode, 0)
        for name in ("link_inputs.py", "relink_outputs.sh"):
            self.assertTrue(os.stat(os.path.join(REPO, "tools", name)).st_mode & stat.S_IXUSR, name)


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
"""A stand-in for docker: builds nothing; `create` names the image, `export` writes the tar that the test chose (FAKE_STATE/<image>.tar), `image inspect` the configuration that it chose, `run` the listing of the
link's inputs (or, with setarch, of the programs of a link) that it chose for the builder stage of the image."""
import hashlib, io, json, os, sys, tarfile
state = os.environ["FAKE_STATE"]
SIX = ("index.js", "index.js.gz", "index.wasm", "index.wasm.gz", "lvl_core.js", "lvl_core.wasm")


def file_of_image(which, name):
    with tarfile.open(os.path.join(state, which + ".tar")) as t:
        return t.extractfile("usr/share/nginx/html/" + name).read()
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
elif args[0] == "cp":
    cid, path = args[1].split(":", 1)
    if os.environ.get("FAKE_FAIL_CP") and os.environ["FAKE_FAIL_CP"] in path:
        sys.exit(1)
    with open(os.path.join(state, cid + ".which")) as f:
        which = f.read().strip()
    with open(args[2], "wb") as f:
        f.write(file_of_image(which, os.path.basename(path)))
elif args[0] == "export":
    with open(os.path.join(state, args[1] + ".which")) as f:
        which = f.read().strip()
    with open(os.path.join(state, which + ".tar"), "rb") as f:
        sys.stdout.buffer.write(f.read())
elif args[0] == "run":
    tag = [a for a in args if a.startswith("ants-image-diff-")][0]
    if os.environ.get("FAKE_FAIL") and os.environ["FAKE_FAIL"] in tag:
        sys.exit(1)
    with open(os.path.join(state, tag + ".which")) as f:
        which = f.read().strip()
    if args[-2] == "-c" and "sha256sum" in args[-1]:        # the six link outputs of the builder stage (the same bytes as the image's, unless a test says otherwise)
        for name in SIX:
            data = file_of_image(which, name)
            if os.environ.get("FAKE_BUILT_WRONG") == name:
                data += b"!"
            print("%s\t%s" % (name, hashlib.sha256(data).hexdigest()))
        sys.exit(0)
    sys.stdin.read()
    if "setarch" in args:
        with open(os.path.join(state, "calls.log")) as f:
            earlier = sum(1 for line in f if "setarch" in line) - 1
        with open(os.path.join(state, which + ".relink")) as f:
            text = f.read()
        if os.environ.get("FAKE_RELINK_NOISE") and earlier == 2:
            text = text.replace("1", "7", 1)
        sys.stdout.write(text)
    else:
        with open(os.path.join(state, which + ".inputs")) as f:
            sys.stdout.write(f.read())
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
        for name in ("image_files_diff.sh", "image_manifest.py", "link_inputs.py", "relink_outputs.sh"):
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
        self.inputs, self.relink = inputs_listing(), RELINK
        self.config = json.dumps([{"Config": {"User": "ants", "Entrypoint": ["/x"], "Cmd": ["--a"], "ExposedPorts": {"80/tcp": {}}, "Env": ["A=1"]}}])
        self.write_images()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def write_listings(self, base_inputs=None, new_inputs=None, base_relink=None, new_relink=None):
        for name, text in (("base.inputs", self.inputs if base_inputs is None else base_inputs), ("new.inputs", self.inputs if new_inputs is None else new_inputs),
                           ("base.relink", self.relink if base_relink is None else base_relink), ("new.relink", self.relink if new_relink is None else new_relink)):
            with open(os.path.join(self.state, name), "w", encoding="utf-8") as f:
                f.write(text)

    def write_images(self, base_files=None, new_files=None, new_config=None):
        self.write_listings()
        files = {HTML + "index.html": page(0, 1), "usr/local/bin/ants_server": (b"binary", 0o755, 0)}
        for name in ("index.js", "index.js.gz", "index.wasm", "index.wasm.gz", "lvl_core.js", "lvl_core.wasm"):
            files[HTML + name] = (b"linked " + name.encode("ascii")) * 40
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
        self.assertNotIn("::warning", done.stdout)
        self.assertNotIn("NOT compared", done.stdout)
        calls = open(os.path.join(self.state, "calls.log"), encoding="utf-8").read()
        self.assertEqual(calls.count("build -f"), 6, "the base and the new image of both Dockerfiles, and the builder stage of the two web images")
        self.assertEqual(calls.count("--target builder"), 2)
        self.assertEqual(calls.count("--build-arg ANTS_BUILD_ID=image-diff"), 6, "the same build id for all six")
        self.assertIn("build -f Dockerfile.server --build-arg", calls)                                      # (the new one is the working tree's own file, the context is the folder that the script runs in)
        self.assertIn("Dockerfile.server.base --build-arg", calls)                                          # (the base one is a copy of the base revision's file in a scratch folder)
        self.assertEqual(calls.count(" -t ants-image-diff-"), 6)
        self.assertEqual(calls.count("\nrun "), 7, "two listings of the link's inputs, two listings of the link outputs of the builder stage and three deterministic links (the base, the new one and the new one again)")
        self.assertEqual(calls.count("\ncp "), 12, "the six linked files of each web image")
        self.assertEqual(calls.count("setarch x86_64 -R sh -s"), 3)
        self.assertEqual(calls.count("--security-opt seccomp=unconfined -e BINARYEN_CORES=1 -e EMCC_CFLAGS=-Wl,--threads=1"), 3, "the recipe of the deterministic link, in the check only")
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
        self.assertEqual(calls.count("build -f"), 4)
        self.assertNotIn("Dockerfile.server", calls)

    OBJECT = "build_web/CMakeFiles/o07.o"
    LINK = "build_web/src/ants_app/CMakeFiles/ants_app.dir/link.txt"

    def test_programs_that_differ_in_content_but_not_in_what_the_link_read_pass(self):
        """The three variants of one link: the files of the two images differ in bytes (and by a few in size), the inputs and the deterministic link do not."""
        self.write_images(base_files={HTML + "index.wasm": b"A" * 5000, HTML + "index.js": b"x" * 900}, new_files={HTML + "index.wasm": b"B" * 5006, HTML + "index.js": b"y" * 900})
        done = self.run_script("--web")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("web link inputs: ", done.stdout)
        self.assertIn("web link outputs: ", done.stdout)

    def test_a_changed_link_input_fails_and_names_it(self):
        self.write_listings(new_inputs=inputs_listing(**{self.OBJECT: "e"}))
        done = self.run_script("--web")
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("web link inputs: " + self.OBJECT + " differs", done.stdout)
        self.assertIn("DIFFER", done.stderr)

    def test_a_changed_link_command_line_fails_and_shows_both(self):
        self.write_listings(new_inputs=inputs_listing(**{self.LINK: "link:em++ -O2 -o index.html a.o b.a"}))
        done = self.run_script("--web")
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("the link command line differs", done.stdout)
        self.assertIn("-O3", done.stdout)
        self.assertIn("-O2", done.stdout)

    def test_a_link_input_that_is_only_in_one_build_fails(self):
        self.write_listings(base_inputs=inputs_listing(**{"build_web/CMakeFiles/stale_stub.o": "a"}))
        done = self.run_script("--web")
        self.assertEqual(done.returncode, 1)
        self.assertIn("only in the base build: build_web/CMakeFiles/stale_stub.o", done.stdout)

    def test_programs_of_the_deterministic_link_that_differ_fail(self):
        self.write_listings(new_relink=RELINK.replace("2" * 64, "9" * 64))
        done = self.run_script("--web")
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("web link outputs: index.wasm differs", done.stdout)

    def test_a_recipe_that_does_not_hold_on_the_machine_is_said_and_only_the_inputs_count(self):
        """The new image linked twice gives two results: the recipe is no recipe here, and the programs are not compared (the inputs are, and they differ nowhere)."""
        self.write_listings(base_relink=RELINK.replace("2" * 64, "9" * 64))
        done = self.run_script("--web", FAKE_RELINK_NOISE="1")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("WARNING: two deterministic links of one image gave different programs", done.stdout)
        self.assertRegex(done.stdout, r"(?m)^::warning title=The deterministic link did not hold::")           # (an annotation: it shows on the page of the run, not only in the log)
        self.assertIn("link outputs were NOT compared", done.stdout.splitlines()[-1])
        self.assertNotIn("web link outputs: 4 entries compared", done.stdout)
        self.write_listings(new_inputs=inputs_listing(**{self.OBJECT: "e"}))
        done = self.run_script("--web", FAKE_RELINK_NOISE="1")
        self.assertEqual(done.returncode, 1, "the inputs are compared whatever the links do")

    def test_a_linked_file_of_the_image_that_is_not_the_builder_stages_output_fails(self):
        """The six linked files of the final image are the builder stage's own link outputs, byte for byte: a file of about the same size that is not fails the check."""
        for name in ("index.wasm", "index.js.gz", "lvl_core.js"):
            with self.subTest(name):
                done = self.run_script("--web", FAKE_BUILT_WRONG=name)
                self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
                self.assertIn("are not the builder stage's link outputs", done.stdout)
                self.assertIn(name, done.stdout)
        done = self.run_script("--web")
        self.assertIn("the six linked files of the base web image are the builder stage's link outputs, byte for byte", done.stdout)
        self.assertIn("the six linked files of the new web image are the builder stage's link outputs, byte for byte", done.stdout)

    def test_a_linked_file_that_cannot_be_taken_out_of_the_image_is_status_2(self):
        done = self.run_script("--web", FAKE_FAIL_CP="lvl_core.wasm")
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)
        self.assertNotIn("the images are the same", done.stdout)

    def test_no_relink_leaves_out_the_deterministic_link(self):
        self.write_listings(new_relink=RELINK.replace("2" * 64, "9" * 64))
        done = self.run_script("--web", "--no-relink")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("not compared (--no-relink)", done.stdout)
        calls = open(os.path.join(self.state, "calls.log"), encoding="utf-8").read()
        self.assertNotIn("setarch", calls)

    def test_a_builder_stage_or_a_listing_that_cannot_be_made_is_status_2(self):
        done = self.run_script("--web", FAKE_FAIL="-builder-new")
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)
        self.assertNotIn("the images are the same", done.stdout)
        self.write_listings(new_inputs="")
        done = self.run_script("--web")
        self.assertEqual(done.returncode, 2, "an empty listing must not pass for the same listing")
        self.assertNotIn("the images are the same", done.stdout)
        self.write_listings()
        done = self.run_script("--web", FAKE_FAIL="web-builder-new")
        self.assertEqual(done.returncode, 2)

    def test_a_deterministic_link_that_fails_is_status_2(self):
        self.write_listings(new_relink="")
        done = self.run_script("--web")
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)
        self.assertNotIn("the images are the same", done.stdout)

    def test_the_server_image_has_no_link_listing(self):
        done = self.run_script("--server")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertNotIn("the builder stage", done.stdout)
        calls = open(os.path.join(self.state, "calls.log"), encoding="utf-8").read()
        self.assertNotIn("--target builder", calls)

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
        self.assertIn("export DOCKER_BUILDKIT=1\n", text)


class TheCiJob(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, ".github", "workflows", "ci.yml"), encoding="utf-8") as f:
            text = f.read()
        start = text.index("\n  image_files:\n")
        cls.job = text[start:]
        cls.text = text
        cls.lines = cls.job.splitlines()

    def code(self):
        return [l for l in self.lines if not l.lstrip().startswith("#")]

    def test_it_is_the_last_job_and_nothing_waits_for_it(self):
        self.assertEqual(re.findall(r"(?m)^  ([a-z0-9_-]+):\s*$", self.job), ["image_files"], "no job after it")
        needing = [l for l in self.text.splitlines() if re.match(r"^\s+needs:", l)]
        for line in needing:
            self.assertNotIn("image_files", line, "a required check must not wait for the image check")
            self.assertNotIn("deploy", line, "there is no deploy job any more")
        self.assertNotIn("\n  deploy:\n", self.text)

    def test_it_runs_the_script_against_the_base_of_the_pull_request_when_a_dockerfile_or_the_check_changed(self):
        self.assertIn("      - name: Build the images from the pull request's base Dockerfiles and from this tree's, and compare their files\n        if: steps.decide.outputs.run == 'true'\n"
                      "        env:\n          PR_BASE_SHA: ${{ github.event.pull_request.base.sha }}\n        run: bash tools/image_files_diff.sh --base \"$PR_BASE_SHA\" --diagnose\n", self.job + "\n")
        self.assertIn("        env:\n          EVENT: ${{ github.event_name }}\n          PR_BASE_SHA: ${{ github.event.pull_request.base.sha }}\n", self.job)
        self.assertIn('          case "$EVENT" in\n            pull_request)\n', self.job)
        self.assertIn("fetch-depth: 0", self.job)                  # (the base commit must be there)
        self.assertNotIn("origin/main", "\n".join(self.code()), "a pull request into another branch is compared with its own base")

    def test_the_trigger_is_the_whole_list_of_the_files_that_the_images_depend_on(self):
        """The decision is made by git (a pathspec: a failing git fails the step, nothing is matched by a grep whose error would read as "no"), with no `if` line (a gate in the workflow's own test
        must be a plain command)."""
        self.assertIn("              changed=$(git diff --name-only \"$PR_BASE_SHA\" HEAD -- Dockerfile Dockerfile.server docker/emsdk_warm.cpp tools/image_files_diff.sh tools/image_manifest.py tools/link_inputs.py "
                      "tools/relink_outputs.sh)\n", self.job)
        self.assertIn('              [ -z "$changed" ] || run=true\n', self.job)
        self.assertNotIn("Dockerfile.hub", self.job, "the hub's image is not part of the check")
        self.assertNotIn("grep", "\n".join(self.code()), "a grep whose own failure would read as no change")
        self.assertNotRegex("\n".join(self.code()), r"(?m)^\s+(if|elif|while)\b(?!:)")
        self.assertIn('          run=false\n', self.job)
        self.assertIn('          echo "run=$run" >> "$GITHUB_OUTPUT"\n', self.job)

    def test_it_cannot_be_made_to_pass(self):
        code = "\n".join(self.code())
        self.assertNotIn("continue-on-error", code)
        self.assertNotIn("|| true", code)
        self.assertNotIn("|| :", code)
        self.assertNotIn("set +e", code)
        self.assertNotIn("exit 0", code)
        self.assertNotRegex(code, r"(?m)^\s+if: .*(always|failure|cancelled)\(\)")
        runs = [l for l in self.code() if l.strip().startswith("run:")]
        self.assertEqual(len(runs), 2, runs)
        self.assertIn("        run: bash tools/image_files_diff.sh --base \"$PR_BASE_SHA\" --diagnose", self.code())

    def test_it_is_not_a_required_check_and_has_a_time_limit(self):
        self.assertIn("name: Image files unchanged by a Dockerfile change", self.job)
        header = self.text[:self.text.index("\njobs:\n")]
        self.assertNotIn("Image files unchanged", header)
        self.assertIn("    timeout-minutes: 60\n", self.job)
        self.assertIn("    runs-on: ubuntu-latest\n", self.job)

    def test_the_docs_say_what_the_job_is_for_and_that_a_new_copy_line_makes_it_red(self):
        with open(os.path.join(REPO, "docs", "TESTING.md"), encoding="utf-8") as f:
            testing = f.read()
        self.assertIn('A sixth job, "Image files unchanged by a Dockerfile change", is not required', testing)
        self.assertIn("a `COPY` line", testing)
        self.assertIn("red for it by design", testing)


if __name__ == "__main__":
    unittest.main()
