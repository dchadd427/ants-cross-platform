#!/usr/bin/env python3
"""Tests of the version tools (run by ./run_tests.sh --fast and by the CI):

  tools/check_version_consistency.py   CHANGELOG.md, STATUS.md and README.md name the release that the file VERSION names
  cmake/ants_stamp_build_id.cmake      writes the one source file that holds ants::BUILD_ID (explicit id, else the git commit, else "unknown")
  docker/resolve_build_id.sh           the build id of a Docker image build (build argument, else a clone's HEAD / refs files, else the build time)

Each test fails when the code it covers is missing or wrong (the deliberately wrong CHANGELOG heading and STATUS version below are the proof for the check).
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHECK = os.path.join(REPO, "tools", "check_version_consistency.py")
STAMP = os.path.join(REPO, "cmake", "ants_stamp_build_id.cmake")
RESOLVE = os.path.join(REPO, "docker", "resolve_build_id.sh")


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def make_tree(root, version="1.2.3", changelog=None, status=None, readme=None, version_file=None):
    write(os.path.join(root, "VERSION"), version_file if version_file is not None else version + "\n")
    write(os.path.join(root, "CHANGELOG.md"), changelog if changelog is not None else
          "# Changelog\n\nIntro.\n\n## Unreleased - next\n\n**For players:**\n- x\n\n## v%s - 2026-01-01 - Title\n\n**For players:**\n- y\n\n## v0.0.1 - 2025-01-01 - Old\n" % version)
    write(os.path.join(root, "STATUS.md"), status if status is not None else
          "# Status\n\n_Updated 2026-01-01 00:00 PDT · current release **v%s** · details: [CHANGELOG](CHANGELOG.md)_\n" % version)
    write(os.path.join(root, "README.md"), readme if readme is not None else
          "# Title\n\n**Current version: v%s** (shown on screen)\n" % version)


def run_check(root):
    return subprocess.run([sys.executable, CHECK, "--root", root], capture_output=True, text=True)


class ConsistencyCheck(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def test_the_real_repository_is_consistent(self):
        result = subprocess.run([sys.executable, CHECK], capture_output=True, text=True, cwd=REPO)
        self.assertEqual(result.returncode, 0, result.stderr)
        with open(os.path.join(REPO, "VERSION"), encoding="utf-8") as f:
            version = f.readline().strip()
        self.assertIn("v" + version, result.stdout)

    def test_a_consistent_tree_passes(self):
        make_tree(self.root)
        result = run_check(self.root)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("v1.2.3", result.stdout)

    def test_a_wrong_changelog_heading_fails_and_names_the_file(self):
        make_tree(self.root, changelog="# Changelog\n\n## v1.2.2 - 2026-01-01 - Behind\n\n**For players:**\n- y\n")
        result = run_check(self.root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("CHANGELOG.md", result.stderr)
        self.assertIn("v1.2.2", result.stderr)
        self.assertIn("1.2.3", result.stderr)
        self.assertNotIn("STATUS.md:", result.stderr)
        self.assertNotIn("README.md:", result.stderr)

    def test_only_the_top_release_heading_counts(self):
        # the newest entry is a release behind VERSION although an older heading equals it: still wrong (the file is newest first)
        make_tree(self.root, changelog="# Changelog\n\n## v1.2.2 - 2026-01-02 - Newest\n\n## v1.2.3 - 2026-01-01 - Older\n")
        self.assertEqual(run_check(self.root).returncode, 1)

    def test_an_unreleased_section_above_the_release_is_fine(self):
        make_tree(self.root, changelog="# Changelog\n\n## Unreleased\n\n- x\n\n## v1.2.3 - 2026-01-01 - T\n")
        self.assertEqual(run_check(self.root).returncode, 0)

    def test_a_wrong_status_version_fails_and_names_the_file(self):
        make_tree(self.root, status="# Status\n\n_Updated 2026-01-01 00:00 PDT · current release **v1.0.0** · details_\n")
        result = run_check(self.root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("STATUS.md", result.stderr)
        self.assertIn("v1.0.0", result.stderr)
        self.assertNotIn("CHANGELOG.md:", result.stderr)

    def test_a_wrong_readme_version_fails_and_names_the_file(self):
        make_tree(self.root, readme="# T\n\n**Current version: v9.9.9** (shown)\n")
        result = run_check(self.root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("README.md", result.stderr)
        self.assertIn("v9.9.9", result.stderr)

    def test_every_disagreeing_file_is_named_at_once(self):
        make_tree(self.root, changelog="## v0.0.9 - x - y\n", status="current release **v0.0.8**\n", readme="Current version: v0.0.7\n")
        result = run_check(self.root)
        self.assertEqual(result.returncode, 1)
        for name in ("CHANGELOG.md", "STATUS.md", "README.md"):
            self.assertIn(name, result.stderr)

    def test_a_missing_version_text_fails(self):
        make_tree(self.root, status="# Status\n\nnothing here\n")
        result = run_check(self.root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("STATUS.md", result.stderr)
        self.assertIn("current release", result.stderr)

    def test_a_missing_file_fails(self):
        make_tree(self.root)
        os.remove(os.path.join(self.root, "README.md"))
        result = run_check(self.root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("README.md: the file is missing", result.stderr)

    def test_a_bad_version_file_fails(self):
        for bad in ("1.2\n", "v1.2.3\n", "1.2.3.4\n", "\n", "one.two.three\n", "01.2.3x\n"):
            make_tree(self.root, version_file=bad)
            result = run_check(self.root)
            self.assertEqual(result.returncode, 1, repr(bad))
            self.assertIn("VERSION", result.stderr)

    def test_a_missing_version_file_fails(self):
        make_tree(self.root)
        os.remove(os.path.join(self.root, "VERSION"))
        result = run_check(self.root)
        self.assertEqual(result.returncode, 1)
        self.assertIn("VERSION: the file is missing", result.stderr)


def have(tool):
    return shutil.which(tool) is not None


# the programs are found once, with the environment as the run started (a test that changes PATH or HOME must not lose them)
CMAKE = shutil.which("cmake")
GIT = shutil.which("git")


def git(root, *args):
    return subprocess.run([GIT, "-C", root, *args], capture_output=True, text=True, check=True).stdout.strip()


def make_repo(root):
    git(root, "init", "-q", "-b", "main", ".")
    git(root, "config", "user.email", "test@example.invalid")
    git(root, "config", "user.name", "Test")
    git(root, "config", "commit.gpgsign", "false")
    write(os.path.join(root, "a.txt"), "a\n")
    git(root, "add", "a.txt")
    git(root, "commit", "-q", "-m", "first")
    return git(root, "rev-parse", "HEAD")


@unittest.skipUnless(have("cmake"), "cmake is needed")
class StampScript(unittest.TestCase):
    """cmake/ants_stamp_build_id.cmake: the file that defines ants::BUILD_ID."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.src = os.path.join(self.tmp.name, "src tree")           # a space in the path, as Windows user folders have
        self.out = os.path.join(self.tmp.name, "out dir", "generated", "build_id.cpp")
        os.makedirs(self.src)

    def tearDown(self):
        self.tmp.cleanup()

    def stamp(self, build_id="", git_exe="__git__"):
        if git_exe == "__git__":
            git_exe = GIT or ""
        args = [CMAKE, "-DSOURCE_DIR=" + self.src, "-DOUT_FILE=" + self.out, "-DGIT_EXECUTABLE=" + git_exe, "-DANTS_BUILD_ID=" + build_id, "-P", STAMP]
        result = subprocess.run(args, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return read(self.out)

    def test_an_explicit_id_is_used(self):
        text = self.stamp("abc1234")
        self.assertIn('const std::string_view BUILD_ID = "abc1234";', text)
        self.assertIn('#include "ants_app/version.hpp"', text)

    def test_an_explicit_id_wins_over_git(self):
        if not have("git"):
            self.skipTest("git is needed")
        make_repo(self.src)
        self.assertIn('BUILD_ID = "release-7";', self.stamp("release-7"))

    @unittest.skipUnless(have("git"), "git is needed")
    def test_the_short_git_commit_without_an_explicit_id(self):
        commit = make_repo(self.src)
        text = self.stamp()
        found = re.search(r'BUILD_ID = "([0-9a-f]+)";', text)
        self.assertIsNotNone(found, text)
        self.assertTrue(commit.startswith(found.group(1)))
        self.assertTrue(7 <= len(found.group(1)) <= 12)
        self.assertNotIn("dirty", text)

    @unittest.skipUnless(have("git"), "git is needed")
    def test_a_working_tree_with_changes_has_no_dirty_suffix(self):
        make_repo(self.src)
        write(os.path.join(self.src, "a.txt"), "changed\n")
        self.assertNotIn("dirty", self.stamp())

    @unittest.skipUnless(have("git"), "git is needed")
    def test_a_new_commit_changes_the_file_and_the_same_commit_does_not_touch_it(self):
        make_repo(self.src)
        first = self.stamp()
        mtime = os.stat(self.out).st_mtime_ns
        self.assertEqual(self.stamp(), first)
        self.assertEqual(os.stat(self.out).st_mtime_ns, mtime, "an unchanged id must not rewrite the file (it would recompile)")
        write(os.path.join(self.src, "b.txt"), "b\n")
        git(self.src, "add", "b.txt")
        git(self.src, "commit", "-q", "-m", "second")
        self.assertNotEqual(self.stamp(), first)

    def test_unknown_without_git_and_without_an_id(self):
        self.assertIn('BUILD_ID = "unknown";', self.stamp(git_exe=""))

    @unittest.skipUnless(have("git"), "git is needed")
    def test_a_folder_that_is_not_a_checkout_is_unknown(self):
        self.assertIn('BUILD_ID = "unknown";', self.stamp())

    def test_an_id_that_cannot_sit_in_a_string_literal_is_unknown(self):
        for bad in ('has"quote', "has space", "back\\slash", "x" * 41, "a;b"):
            self.assertIn('BUILD_ID = "unknown";', self.stamp(bad, git_exe=""), bad)


@unittest.skipUnless(have("sh"), "a POSIX sh is needed")
class ResolveBuildId(unittest.TestCase):
    """docker/resolve_build_id.sh: the build id of the Docker images."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.tmp.cleanup()

    def resolve(self, explicit="", info_dir=""):
        result = subprocess.run(["sh", RESOLVE, explicit, info_dir], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.strip(), result.stderr

    def flat_copy(self, git_dir):
        """A copy of HEAD, packed-refs and the CONTENTS of refs/ side by side (heads/, tags/), the layout that the script also accepts."""
        flat = os.path.join(self.tmp.name, "gitinfo")
        os.makedirs(flat)
        shutil.copy(os.path.join(git_dir, "HEAD"), flat)
        if os.path.exists(os.path.join(git_dir, "packed-refs")):
            shutil.copy(os.path.join(git_dir, "packed-refs"), flat)
        for entry in os.listdir(os.path.join(git_dir, "refs")):
            src = os.path.join(git_dir, "refs", entry)
            if os.path.isdir(src):
                shutil.copytree(src, os.path.join(flat, entry))
            else:
                shutil.copy(src, flat)
        return flat

    def test_the_build_argument_wins(self):
        out, why = self.resolve("abc1234", self.tmp.name)
        self.assertEqual(out, "abc1234")
        self.assertIn("build argument", why)

    def test_an_invalid_argument_is_ignored(self):
        for bad in ("has space", "a;rm -rf", "x" * 41, "quote'"):
            out, why = self.resolve(bad, "")
            self.assertNotEqual(out, bad)
            self.assertRegex(out, r"^[0-9]{8}-[0-9]{4}$")
            self.assertIn("build date", why)

    def test_no_argument_and_no_git_information_is_the_utc_build_time(self):
        out, why = self.resolve("", os.path.join(self.tmp.name, "does-not-exist"))
        self.assertRegex(out, r"^[0-9]{8}-[0-9]{4}$")
        self.assertIn("build date", why)

    @unittest.skipUnless(have("git"), "git is needed")
    def test_a_clones_git_folder_gives_the_short_commit(self):
        repo = os.path.join(self.tmp.name, "clone")
        os.makedirs(repo)
        commit = make_repo(repo)
        out, why = self.resolve("", os.path.join(repo, ".git"))
        self.assertEqual(out, commit[:7])
        self.assertIn("git commit", why)

    def docker_copy(self, git_dir):
        """What the Dockerfiles' `COPY VERSION .git* /gitinfo/` makes of a clone, given .dockerignore (.git, !.git/HEAD, !.git/packed-refs, !.git/refs): HEAD, packed-refs (when
        there is one) and refs/ as they are in .git; nothing else of .git is in the build context."""
        copy = os.path.join(self.tmp.name, "gitinfo-docker")
        os.makedirs(copy)
        for name in ("HEAD", "packed-refs"):
            if os.path.exists(os.path.join(git_dir, name)):
                shutil.copy(os.path.join(git_dir, name), copy)
        shutil.copytree(os.path.join(git_dir, "refs"), os.path.join(copy, "refs"))
        return copy

    @unittest.skipUnless(have("git"), "git is needed")
    def test_the_copy_the_dockerfiles_make_gives_the_short_commit(self):
        repo = os.path.join(self.tmp.name, "clone")
        os.makedirs(repo)
        commit = make_repo(repo)
        out, _ = self.resolve("", self.docker_copy(os.path.join(repo, ".git")))
        self.assertEqual(out, commit[:7])
        shutil.rmtree(os.path.join(self.tmp.name, "gitinfo-docker"))
        out, _ = self.resolve("", self.flat_copy(os.path.join(repo, ".git")))
        self.assertEqual(out, commit[:7])

    @unittest.skipUnless(have("git"), "git is needed")
    def test_packed_refs_are_read_too(self):
        repo = os.path.join(self.tmp.name, "clone")
        os.makedirs(repo)
        commit = make_repo(repo)
        git(repo, "pack-refs", "--all")
        self.assertFalse(os.path.exists(os.path.join(repo, ".git", "refs", "heads", "main")))
        out, _ = self.resolve("", os.path.join(repo, ".git"))
        self.assertEqual(out, commit[:7])
        out, _ = self.resolve("", self.docker_copy(os.path.join(repo, ".git")))
        self.assertEqual(out, commit[:7])

    @unittest.skipUnless(have("git"), "git is needed")
    def test_a_detached_head_names_the_commit_itself(self):
        repo = os.path.join(self.tmp.name, "clone")
        os.makedirs(repo)
        commit = make_repo(repo)
        git(repo, "checkout", "-q", "--detach")
        out, _ = self.resolve("", os.path.join(repo, ".git"))
        self.assertEqual(out, commit[:7])

    def test_a_head_that_points_nowhere_falls_back_to_the_build_time(self):
        info = os.path.join(self.tmp.name, "info")
        write(os.path.join(info, "HEAD"), "ref: refs/heads/gone\n")
        out, why = self.resolve("", info)
        self.assertRegex(out, r"^[0-9]{8}-[0-9]{4}$")
        self.assertIn("build date", why)

    def test_garbage_in_head_falls_back_to_the_build_time(self):
        info = os.path.join(self.tmp.name, "info")
        write(os.path.join(info, "HEAD"), "not a commit\n")
        out, _ = self.resolve("", info)
        self.assertRegex(out, r"^[0-9]{8}-[0-9]{4}$")

    def test_docker_ignore_lets_only_head_and_refs_of_git_through(self):
        ignore = read(os.path.join(REPO, ".dockerignore")).splitlines()
        self.assertIn(".git", ignore)
        self.assertIn("!.git/HEAD", ignore)
        self.assertIn("!.git/refs", ignore)
        self.assertIn("!.git/packed-refs", ignore)
        self.assertNotIn("!.git/config", ignore)                        # .git/config can hold a password in a remote's address
        self.assertFalse([l for l in ignore if l.startswith("!.git/") and l not in ("!.git/HEAD", "!.git/refs", "!.git/packed-refs")])


if __name__ == "__main__":
    unittest.main()
