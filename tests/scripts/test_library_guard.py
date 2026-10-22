#!/usr/bin/env python3
"""The owner's rule for the tools: none of them changes, re-saves, repairs, re-renders or deletes a map of the library, or writes over the file it reads
(tools/library_guard.py and library_guard.hpp; docs/WORKFLOW.md, "Tools never touch the map library"; run by ./run_tests.sh --fast and by the CI).

Everything here runs on a TEMPORARY COPY of the layout (Original-Ants/Maps, Community-Maps with its maps.json and previews): the real library is never given to a tool, and the
last test reads it only to say that its bytes are the ones this file was written for. What is checked, for the python guard and for every python tool that writes a path it is given
(community_maps repair and build, map_previews, map_atlas, make_art, mutate): the library is refused by every spelling (the plain path, a new file in it, another case, `..`, a
relative path, a symbolic link to the folder and to a file, a second name of a map), a folder is found by what it is whatever its name, the file that is read is never the file that is
written, reading stays fine, an ordinary folder stays fine, and after every refusal the copy of the library is byte for byte and file for file what it was.
(The C++ tools, lvl_tool, map_sweep and bot_arena, test the same rule in their --selftest.)
"""
import contextlib
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
import community_maps  # noqa: E402
import library_guard  # noqa: E402
import map_previews  # noqa: E402

TINY = os.path.join(REPO, "Original-Ants", "Maps", "TINY.LVL")


def snapshot(folder):
    """{relative path: bytes} of every file under `folder`, and the empty folders (as None)."""
    out = {}
    for root, dirs, files in os.walk(folder):
        for name in files:
            path = os.path.join(root, name)
            with open(path, "rb") as f:
                out[os.path.relpath(path, folder)] = f.read()
        if not dirs and not files:
            out[os.path.relpath(root, folder)] = None
    return out


class Layout(unittest.TestCase):
    """A copy of the layout of the checkout in a scratch folder."""

    def setUp(self):
        self.base = tempfile.mkdtemp(prefix="library_guard_")
        self.addCleanup(shutil.rmtree, self.base, True)
        self.layout = os.path.join(self.base, "layout")
        self.originals = os.path.join(self.layout, "Original-Ants", "Maps")
        self.community = os.path.join(self.layout, "Community-Maps")
        self.work = os.path.join(self.layout, "work")
        for folder in (self.originals, os.path.join(self.community, "previews"), self.work):
            os.makedirs(folder)
        shutil.copyfile(TINY, os.path.join(self.originals, "TINY.LVL"))
        shutil.copyfile(TINY, os.path.join(self.community, "a map.lvl"))
        with open(os.path.join(self.community, "maps.json"), "w", encoding="utf-8") as f:
            json.dump({"format": 1, "maps": []}, f)
        with open(os.path.join(self.community, "previews", "a map.webp"), "wb") as f:
            f.write(b"RIFF....WEBP")
        self.before = snapshot(self.layout)

    def assertUntouched(self, what=""):
        self.assertEqual(snapshot(self.layout), self.before, "the copy of the library changed: " + what)

    def spellings(self):
        """Every way to name something in the library, as (what it is, path)."""
        return [
            ("a map", os.path.join(self.community, "a map.lvl")),
            ("an original", os.path.join(self.originals, "TINY.LVL")),
            ("the list", os.path.join(self.community, "maps.json")),
            ("a picture", os.path.join(self.community, "previews", "a map.webp")),
            ("a new file", os.path.join(self.community, "new.lvl")),
            ("a new file in a new folder", os.path.join(self.community, "no such folder", "x.lvl")),
            ("a new original", os.path.join(self.originals, "NEW.LVL")),
            ("another case", os.path.join(self.layout, "COMMUNITY-MAPS", "a map.lvl")),
            ("another case of the originals", os.path.join(self.layout, "original-ants", "Maps", "TINY.LVL")),
            ("with ..", os.path.join(self.work, "..", "Community-Maps", "a map.lvl")),
            ("with .. and back", os.path.join(self.community, "..", "Community-Maps", "x.lvl")),
            ("with . and //", self.community + os.sep + "." + os.sep + os.sep + "x.lvl"),
        ]


class TheGuard(Layout):
    def test_every_spelling_of_the_library_is_refused(self):
        for what, path in self.spellings():
            why = library_guard.write_refusal(path)
            self.assertIn("map library", why, what)
            self.assertIn("Original-Ants or Community-Maps", why, what)
            with self.assertRaises(library_guard.LibraryRefused, msg=what):
                library_guard.require_writable(path)
        self.assertUntouched()

    def test_a_relative_path_is_refused(self):
        was = os.getcwd()
        self.addCleanup(os.chdir, was)
        os.chdir(self.work)
        for path in ("../Community-Maps/a map.lvl", "./../Original-Ants/Maps/NEW.LVL", "../COMMUNITY-MAPS/x.lvl", os.path.join("..", "Community-Maps")):
            self.assertIn("map library", library_guard.write_refusal(path), path)
        os.chdir(self.community)
        for path in ("x.lvl", "./x.lvl", "../Community-Maps/x.lvl", "previews/x.webp"):
            self.assertIn("map library", library_guard.write_refusal(path), path)
        self.assertEqual(library_guard.write_refusal("../work/report.json"), "")
        os.chdir(was)
        self.assertUntouched()

    def test_a_symbolic_link_is_followed(self):
        link_dir = os.path.join(self.base, "link_to_folder")
        link_file = os.path.join(self.base, "link_to_file.lvl")
        try:
            os.symlink(self.community, link_dir, target_is_directory=True)
            os.symlink(os.path.join(self.community, "a map.lvl"), link_file)
        except (OSError, NotImplementedError):
            self.skipTest("this system does not allow symbolic links to this user")
        for path in (os.path.join(link_dir, "a map.lvl"), os.path.join(link_dir, "fresh.lvl"), link_file):
            self.assertIn("map library", library_guard.write_refusal(path), path)
        # a link to a folder that is no library is no library (and is written through)
        elsewhere = os.path.join(self.base, "elsewhere")
        os.makedirs(elsewhere)
        link_plain = os.path.join(self.base, "link_to_elsewhere")
        os.symlink(elsewhere, link_plain, target_is_directory=True)
        self.assertEqual(library_guard.write_refusal(os.path.join(link_plain, "x.lvl")), "")
        self.assertUntouched()

    def test_a_second_name_of_a_library_map_is_refused(self):
        second = os.path.join(self.base, "second name.lvl")
        try:
            os.link(os.path.join(self.community, "a map.lvl"), second)
        except (OSError, NotImplementedError):
            self.skipTest("this system does not allow hard links here")
        self.assertIn("more than one name", library_guard.write_refusal(second))
        os.remove(second)
        plain = os.path.join(self.base, "plain.lvl")
        shutil.copyfile(TINY, plain)
        self.assertEqual(library_guard.write_refusal(plain), "")
        self.assertUntouched()

    def test_a_folder_is_found_by_what_it_is_whatever_its_name(self):
        stash = os.path.join(self.base, "stash")
        os.makedirs(os.path.join(stash, "deeper"))
        self.assertIn("map library", library_guard.write_refusal(os.path.join(stash, "deeper", "x.lvl"), library=[stash]))
        self.assertIn("map library", library_guard.write_refusal(os.path.join(stash, "x.lvl"), library=[stash, os.path.join(self.base, "not there")]))
        self.assertEqual(library_guard.write_refusal(os.path.join(self.base, "elsewhere.lvl"), library=[stash]), "")
        # this checkout's own library folders are the default
        self.assertEqual([os.path.basename(p) for p in library_guard.checkout_library()], ["Original-Ants", "Community-Maps"])
        self.assertIn("map library", library_guard.write_refusal(os.path.join(REPO, "Community-Maps", "never_written.lvl")))
        self.assertIn("map library", library_guard.write_refusal(os.path.join(REPO, "Original-Ants", "Maps", "NEVER.LVL")))
        self.assertFalse(os.path.exists(os.path.join(REPO, "Community-Maps", "never_written.lvl")))

    def test_the_file_that_is_read_is_never_written(self):
        source = os.path.join(self.work, "in.lvl")
        shutil.copyfile(TINY, source)
        self.assertIn("own input", library_guard.write_refusal(source, [source]))
        self.assertIn("own input", library_guard.write_refusal(os.path.join(self.work, ".", "in.lvl"), [source]))
        self.assertIn("own input", library_guard.write_refusal(os.path.join(self.work, "..", "work", "in.lvl"), [source]))
        self.assertEqual(library_guard.write_refusal(os.path.join(self.work, "out.lvl"), [source]), "")
        self.assertEqual(library_guard.write_refusal(os.path.join(self.work, "out.lvl"), [os.path.join(self.work, "not there")]), "")
        self.assertEqual(library_guard.write_refusal(source, []), "", "(without an input to compare, an ordinary file is fine)")
        self.assertEqual(library_guard.write_refusal(source, [""]), "")
        # the folder it reads is never the folder it writes
        self.assertIn("own input", library_guard.write_refusal(self.work, [self.work]))

    def test_what_stays_allowed(self):
        for path in (os.path.join(self.base, "x.lvl"), os.path.join(self.work, "report.json"), os.path.join(self.base, "Community-Maps-old", "x.lvl"),
                     os.path.join(self.base, "My Original-Ants copy", "x.lvl"), os.path.join(self.base, "Community Maps", "x.lvl"), os.path.join(self.base, "ants.Community-Maps", "x"), ""):
            self.assertEqual(library_guard.write_refusal(path), "", path)
        # reading is not the guard's business: the library copy is read and a copy of a map may be written beside it
        with open(os.path.join(self.community, "a map.lvl"), "rb") as f:
            data = f.read()
        copy = os.path.join(self.work, "copy.lvl")
        with open(copy, "wb") as f:
            f.write(data)
        self.assertEqual(library_guard.write_refusal(copy, [os.path.join(self.community, "a map.lvl")]), "")
        os.remove(copy)
        self.assertUntouched()


class ThePythonTools(Layout):
    def report(self, name="report.json"):
        path = os.path.join(self.work, name)
        with open(path, "w", encoding="utf-8") as f:
            json.dump({"maps": []}, f)
        self.before = snapshot(self.layout)          # (the report is part of the scratch layout, not of the library)
        return path

    def test_community_maps_build_and_repair_refuse_the_library(self):
        source = os.path.join(self.work, "source")
        os.makedirs(source)
        report = self.report()
        for what, path in self.spellings():
            for command in ("build", "repair"):
                out, err = io.StringIO(), io.StringIO()
                with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                    status = community_maps.main(["community_maps.py", command, source, report, path])
                self.assertEqual(status, 2, "%s into %s" % (command, what))
                self.assertIn("map library", err.getvalue(), what)
                self.assertIn("nothing written", err.getvalue(), what)
                self.assertEqual(out.getvalue(), "", what)
            with self.assertRaises(library_guard.LibraryRefused, msg=what):
                community_maps.build(source, [], path, originals={})
            with self.assertRaises(library_guard.LibraryRefused, msg=what):
                community_maps.repair(source, [], path, originals={})
        self.assertUntouched()

    def test_community_maps_never_write_into_the_folder_they_read(self):
        source = os.path.join(self.work, "source")
        os.makedirs(source)
        report = self.report()
        for command in ("build", "repair"):
            err = io.StringIO()
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(err):
                self.assertEqual(community_maps.main(["community_maps.py", command, source, report, source]), 2, command)
            self.assertIn("own input", err.getvalue())
        repaired = os.path.join(self.work, "repaired")
        os.makedirs(repaired)
        with self.assertRaises(library_guard.LibraryRefused):
            community_maps.build(source, [], repaired, originals={}, repaired=(repaired, []))
        self.assertEqual(os.listdir(source), [])

    def test_community_maps_still_write_an_ordinary_folder(self):
        source = os.path.join(self.work, "source")
        os.makedirs(source)
        report = self.report()
        for command, name in (("build", "maps.json"), ("repair", "repairs.json")):
            dest = os.path.join(self.base, "out_" + command)
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(community_maps.main(["community_maps.py", command, source, report, dest]), 0, command)
            self.assertTrue(os.path.isfile(os.path.join(dest, name)), command)
        self.assertUntouched()

    def test_map_previews_refuse_the_library(self):
        def never_called(path):
            raise AssertionError("a picture was drawn")
        for folder in (self.community, os.path.join(self.layout, "COMMUNITY-MAPS"), os.path.join(self.work, "..", "Community-Maps")):
            with self.assertRaises(library_guard.LibraryRefused, msg=folder):
                map_previews.update(folder, os.path.join(folder, "previews"), never_called)
        # the pictures in an ordinary folder, the maps in the library: the index would go beside the maps, so that is refused too
        with self.assertRaises(library_guard.LibraryRefused):
            map_previews.update(self.community, os.path.join(self.work, "pictures"), never_called)
        self.assertFalse(os.path.exists(os.path.join(self.work, "pictures")))
        # and the command: the default folder is the library itself
        done = subprocess.run([sys.executable, os.path.join(REPO, "tools", "map_previews.py"), self.community, "--ants", sys.executable], capture_output=True, text=True, timeout=120)
        if "Pillow" not in done.stderr and "PIL" not in done.stderr:
            self.assertNotEqual(done.returncode, 0)
            self.assertIn("map library", done.stderr)
        self.assertUntouched()

    def test_map_atlas_refuses_the_library(self):
        for what, path in self.spellings()[:12]:
            done = subprocess.run([sys.executable, os.path.join(REPO, "tools", "map_atlas.py"), path], capture_output=True, text=True, timeout=120)
            self.assertNotEqual(done.returncode, 0, what)
            self.assertIn("map library", done.stderr, what)
            self.assertIn("nothing written", done.stderr, what)
        self.assertUntouched()

    def test_make_art_refuses_the_library(self):
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("Pillow is not installed (the pictures tool needs it)")
        for what, path in self.spellings():
            done = subprocess.run([sys.executable, os.path.join(REPO, "tools", "front_page_art", "make_art.py"), "--out", path], capture_output=True, text=True, timeout=120)
            self.assertNotEqual(done.returncode, 0, what)
            self.assertIn("map library", done.stderr, what)
        self.assertUntouched()

    def spec(self, file, **extra):
        path = os.path.join(self.work, "spec.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump({"mutants": [dict({"id": "m", "file": file, "old": "RIFF", "new": "RAFF", "test": "true"}, **extra)]}, f)
        self.before = snapshot(self.layout)
        return path

    def mutate(self, *args):
        return subprocess.run([sys.executable, os.path.join(REPO, "tools", "mutate.py")] + list(args), capture_output=True, text=True, timeout=120)

    def test_mutate_never_changes_a_library_file(self):
        for file in ("Community-Maps/previews/a map.webp", "Original-Ants/Maps/TINY.LVL", "Community-Maps/maps.json", "COMMUNITY-MAPS/a map.lvl", "Community-Maps/./a map.lvl"):
            done = self.mutate(self.spec(file), "--root", self.layout)
            self.assertEqual(done.returncode, 2, file)
            self.assertIn("map library", done.stderr, file)
            self.assertIn("nothing changed", done.stderr, file)
            done = self.mutate(self.spec(file), "--root", self.layout, "--check")
            self.assertEqual(done.returncode, 2, file)
            self.assertUntouched(file)
        # the report is a file too
        done = self.mutate(self.spec("work/in.txt"), "--root", self.layout, "--check", "--report", os.path.join(self.community, "r.json"))
        self.assertEqual(done.returncode, 2)
        self.assertIn("the report", done.stderr)
        self.assertUntouched()

    def test_mutate_still_works_on_an_ordinary_file(self):
        with open(os.path.join(self.work, "in.txt"), "w", encoding="utf-8") as f:
            f.write("RIFF")
        done = self.mutate(self.spec("work/in.txt"), "--root", self.layout, "--check")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("1 mutants, 0 that cannot be applied", done.stdout)


class TheCppTools(Layout):
    """bot_arena (its --selftest has no room for this: tools/check_hygiene.py holds the file to its length), through the built program when there is one; lvl_tool and map_sweep test themselves."""

    def program(self, name):
        for build in (os.environ.get("ANTS_BUILD_DIR", "build"), "build"):
            for suffix in ("", ".exe"):
                path = os.path.join(REPO, build, name + suffix)
                if os.path.isfile(path):
                    return path
        self.skipTest("%s is not built" % name)

    def test_bot_arena_writes_no_report_and_no_replay_into_the_library(self):
        arena = self.program("bot_arena")
        played = os.path.join(self.work, "played.lvl")
        shutil.copyfile(TINY, played)
        before = snapshot(self.layout)
        base = [arena, "--map", played, "--ticks", "100", "--quiet"]
        refused = [("--out", path) for _, path in self.spellings()] + [("--save-replays", os.path.join(self.community, "replays")), ("--save-replays", os.path.join(self.originals, "r")), ("--out", played)]
        for option, path in refused:
            done = subprocess.run(base + [option, path], capture_output=True, text=True, timeout=120)
            self.assertEqual(done.returncode, 2, option + " " + path)
            self.assertIn(option, done.stderr)
            self.assertIn("nothing written", done.stderr)
            self.assertNotIn("match(es)", done.stdout)
        self.assertEqual(snapshot(self.layout), before)
        ok = subprocess.run(base + ["--out", os.path.join(self.work, "report.json")], capture_output=True, text=True, timeout=300)
        self.assertEqual(ok.returncode, 0, ok.stdout + ok.stderr)
        self.assertTrue(os.path.isfile(os.path.join(self.work, "report.json")))


class TheRealLibrary(unittest.TestCase):
    def test_the_tests_above_never_needed_it(self):
        """The real library is only read, to say it is there: 540 maps (6 originals, the rest players') and no file made by these tests."""
        with open(os.path.join(REPO, "Community-Maps", "maps.json"), encoding="utf-8") as f:
            listed = json.load(f)["maps"]
        self.assertEqual(len(listed) + len([n for n in os.listdir(os.path.join(REPO, "Original-Ants", "Maps")) if n.lower().endswith(".lvl")]), 540)
        self.assertFalse(os.path.exists(os.path.join(REPO, "Community-Maps", "never_written.lvl")))
        self.assertFalse(os.path.exists(os.path.join(REPO, "Original-Ants", "Maps", "NEVER.LVL")))


if __name__ == "__main__":
    unittest.main()
