#!/usr/bin/env python3
"""Tests of tools/deploy_filter.py, the file filter of the deploy job (run by ./run_tests.sh --fast and by the CI).

The deploy job redeploys the site (which restarts it and ends the matches that are running) only when a push changed something that an image contains or the stack
file; documents, .github/, tests/ and the tools that no image runs do not. The list comes from the real Dockerfiles and .dockerignore, so this test reads the real files
and holds the filter to the list of docs/WORKFLOW.md:

  - the table of paths below: each one deploys or is skipped, as the list says
  - a push that has one file that counts deploys, whatever else it holds; a push of documents only is skipped; nothing changed is skipped
  - the stack file of the other site (staging) does not deploy this one, its own does
  - everything that a Dockerfile copies from the context and .dockerignore lets through is "deploy" (derived from the real files, so a new COPY is noticed)
  - the .dockerignore matcher follows Docker's rules (a pattern keeps out a whole folder, `!` lets a path back in, `*` stays inside a folder name)
  - a change that cannot be listed deploys (to be safe); the tool writes deploy=true|false for a workflow step
"""
import os
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOOL = os.path.join(REPO, "tools", "deploy_filter.py")
sys.path.insert(0, os.path.join(REPO, "tools"))
import deploy_filter as df     # noqa: E402

DEPLOYS = [
    "VERSION", "CMakeLists.txt", "cmake/ants_stamp_build_id.cmake", "cmake/ants_test_paths.cpp.in", "include/ants_sim/sim_engine.hpp", "src/ants_sim/sim_engine.cpp",
    "src/ants_app/application.cpp", "web/shell.html", "web/four.html", "web/favicon.png", "docker/nginx.conf", "docker/resolve_build_id.sh",
    "Dockerfile", "Dockerfile.server", ".dockerignore", "docker-compose.stack.yml",
    "CHANGELOG.md", "docs/CHANGELOG_ARCHIVE.md", "tools/changelog_to_html.py",
    "Original-Ants/ants.chd", "Original-Ants/Maps/TINY.LVL", "Original-Ants/INTRO.mp3", "asset_catalog/index.html", "asset_catalog/sprites/s1.png",
]
SKIPS = [
    "STATUS.md", "README.md", "AGENTS.md", "THIRD_PARTY_NOTICES.md", "LICENSE", "docs/WORKFLOW.md", "docs/BOTS.md", "docs/NETWORK_PORT.md", "docs/audit/B3_notes.md",
    "docs/reverse_engineering/notes.txt", ".github/workflows/ci.yml", ".github/dependabot.yml",
    "tests/test_sim/test_sim_rules.cpp", "tests/scripts/test_run_tests.py", "tests/data/edge_scroll_samples.csv", "tests/common/ants_test_paths.hpp", "tests/TEST_INFRA.md",
    "tools/check_version_consistency.py", "tools/release.py", "tools/mutate.py", "tools/map_sweep.cpp", "tools/deploy_filter.py",
    "run_tests.sh", "run_tests.bat", "start_game.sh", "start_game.bat", "build_web.sh", ".editorconfig", ".gitignore", ".gitattributes",
    "docker-compose.yml", "docker-compose.server.yml", "docker-compose.staging.yml",
    "Original-Ants/ddraw.ini", "Original-Ants/Ants.exe", "Original-Ants/chat.txt", "Original-Ants/Shaders/x.glsl",
]


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def write(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def run_tool(*args, stdin=None, root=REPO):
    return subprocess.run([sys.executable, TOOL, "--root", root, *args], input=stdin, capture_output=True, text=True)


class TheList(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.classifier = df.Classifier(REPO, "docker-compose.stack.yml")

    def test_what_counts(self):
        for path in DEPLOYS:
            counts, why = self.classifier.verdict(path)
            self.assertTrue(counts, "%s must deploy the site (%s)" % (path, why))

    def test_what_does_not(self):
        for path in SKIPS:
            counts, why = self.classifier.verdict(path)
            self.assertFalse(counts, "%s must not deploy the site (%s)" % (path, why))

    def test_every_file_of_the_repository_has_a_verdict_that_follows_its_folder(self):
        tracked = subprocess.run(["git", "-C", REPO, "ls-files"], capture_output=True, text=True)
        if tracked.returncode != 0:
            self.skipTest("not a git checkout")
        for path in tracked.stdout.splitlines():
            counts, why = self.classifier.verdict(path)
            top = path.split("/")[0]
            if top in ("src", "include", "cmake", "web", "asset_catalog"):
                self.assertTrue(counts, "%s is under %s/, which the images copy (%s)" % (path, top, why))
            elif top in ("tests", ".github"):
                self.assertFalse(counts, "%s: tests and CI files are in no image (%s)" % (path, why))
            elif top == "docs":
                self.assertEqual(counts, path == "docs/CHANGELOG_ARCHIVE.md", "%s (%s)" % (path, why))
            elif path.endswith(".md") and "/" not in path:
                self.assertEqual(counts, path == "CHANGELOG.md", "%s (%s)" % (path, why))
            elif top == "tools":
                self.assertEqual(counts, path == "tools/changelog_to_html.py", "%s (%s)" % (path, why))

    def test_the_sources_of_the_dockerfiles_are_derived_from_the_real_files(self):
        sources = [s for s, _ in self.classifier.sources]
        for expected in ("src/", "include/", "cmake/", "web/", "Original-Ants/", "Original-Ants/Maps/", "tools/", "asset_catalog/", "CHANGELOG.md", "docs/CHANGELOG_ARCHIVE.md",
                         "docker/nginx.conf", "docker/resolve_build_id.sh", "web/favicon.*", "CMakeLists.txt", "VERSION"):
            self.assertIn(expected, sources)
        self.assertNotIn("/src/build_web/src/ants_app/index.*", sources)                  # COPY --from=builder reads another stage, not the context

    def test_a_new_copy_line_is_noticed(self):
        with tempfile.TemporaryDirectory() as root:
            for name in ("Dockerfile", "Dockerfile.server", ".dockerignore"):
                text = read(os.path.join(REPO, name))
                if name == "Dockerfile":
                    text += "\nCOPY brand-new-folder/ /usr/share/nginx/html/new/\n"
                write(os.path.join(root, name), text)
            classifier = df.Classifier(root, "docker-compose.stack.yml")
            self.assertTrue(classifier.verdict("brand-new-folder/page.html")[0])
            self.assertFalse(df.Classifier(REPO, "docker-compose.stack.yml").verdict("brand-new-folder/page.html")[0])


class Pushes(unittest.TestCase):
    def verdict(self, files, *args):
        result = run_tool("--files", *args, stdin="\n".join(files) + "\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.strip()

    def test_documents_only_is_skipped(self):
        out = self.verdict(["STATUS.md", "README.md", "docs/WORKFLOW.md", "AGENTS.md", ".github/workflows/ci.yml", "tests/scripts/x.py"])
        self.assertTrue(out.startswith("skip:"), out)
        self.assertIn("6 file(s)", out)

    def test_one_file_that_counts_deploys_the_whole_push(self):
        out = self.verdict(["STATUS.md", "docs/BOTS.md", "src/ants_ai/bot.cpp", "tests/test_ai/x.cpp"])
        self.assertTrue(out.startswith("deploy: src/ants_ai/bot.cpp changed"), out)
        self.assertIn("copied into an image", out)

    def test_a_count_of_the_others_is_given(self):
        out = self.verdict(["src/a.cpp", "web/shell.html", "STATUS.md"])
        self.assertIn("and 1 more file(s) count", out)

    def test_nothing_changed_is_skipped(self):
        out = run_tool("--files", stdin="\n\n").stdout.strip()
        self.assertEqual(out, "skip: no file changed")

    def test_the_changelog_and_its_archive_are_the_documents_that_deploy(self):
        self.assertTrue(self.verdict(["CHANGELOG.md"]).startswith("deploy:"))
        self.assertTrue(self.verdict(["docs/CHANGELOG_ARCHIVE.md"]).startswith("deploy:"))
        self.assertTrue(self.verdict(["docs/AUDIT_ONE_TO_ONE.md"]).startswith("skip:"))

    def test_each_site_has_its_own_stack_file(self):
        self.assertTrue(self.verdict(["docker-compose.stack.yml"]).startswith("deploy:"))
        self.assertTrue(self.verdict(["docker-compose.staging.yml"]).startswith("skip:"))
        self.assertTrue(self.verdict(["docker-compose.staging.yml"], "--compose", "docker-compose.staging.yml").startswith("deploy:"))
        self.assertTrue(self.verdict(["docker-compose.stack.yml"], "--compose", "docker-compose.staging.yml").startswith("skip:"))

    def test_explain_names_every_file(self):
        result = run_tool("--files", "--explain", stdin="STATUS.md\nsrc/a.cpp\n")
        self.assertIn("skip    STATUS.md", result.stdout)
        self.assertIn("DEPLOY  src/a.cpp", result.stdout)

    def test_windows_separators_and_dot_slash_are_understood(self):
        self.assertTrue(self.verdict(["src\\ants_sim\\a.cpp"]).startswith("deploy:"))
        self.assertTrue(self.verdict(["./src/a.cpp"]).startswith("deploy:"))
        self.assertTrue(self.verdict(["./docs/x.md"]).startswith("skip:"))

    def test_github_output_gets_deploy_true_or_false(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "out.txt")
            run_tool("--files", "--github-output", out, stdin="src/a.cpp\n")
            run_tool("--files", "--github-output", out, stdin="README.md\n")
            lines = read(out).splitlines()
            self.assertEqual([l for l in lines if l.startswith("deploy=")], ["deploy=true", "deploy=false"])
            self.assertTrue(all(l.startswith(("deploy=", "reason=")) for l in lines))

    def test_the_arguments_are_checked(self):
        self.assertEqual(run_tool().returncode, 2)
        self.assertEqual(run_tool("--base", "abc").returncode, 2)


class FromGit(unittest.TestCase):
    def git(self, root, *args):
        return subprocess.run(["git", "-C", root, "-c", "user.name=t", "-c", "user.email=t@example.org", *args], capture_output=True, text=True, check=True).stdout.strip()

    def test_the_files_between_two_revisions(self):
        with tempfile.TemporaryDirectory() as root:
            for name in ("Dockerfile", "Dockerfile.server", ".dockerignore"):
                write(os.path.join(root, name), read(os.path.join(REPO, name)))
            self.git(root, "init", "-q")
            os.makedirs(os.path.join(root, "src"))
            os.makedirs(os.path.join(root, "docs"))
            write(os.path.join(root, "src", "a.cpp"), "a\n")
            write(os.path.join(root, "docs", "n.md"), "n\n")
            self.git(root, "add", "-A")
            self.git(root, "commit", "-q", "-m", "one")
            one = self.git(root, "rev-parse", "HEAD")
            write(os.path.join(root, "docs", "n.md"), "changed\n")
            self.git(root, "commit", "-q", "-am", "docs only")
            two = self.git(root, "rev-parse", "HEAD")
            write(os.path.join(root, "src", "a.cpp"), "changed\n")
            self.git(root, "commit", "-q", "-am", "code")
            three = self.git(root, "rev-parse", "HEAD")
            self.assertTrue(run_tool("--base", one, "--head", two, root=root).stdout.startswith("skip:"))
            self.assertTrue(run_tool("--base", two, "--head", three, root=root).stdout.startswith("deploy: src/a.cpp"))
            # the whole range counts: a push that holds an earlier code change and a later documents-only commit deploys
            self.assertTrue(run_tool("--base", one, "--head", three, root=root).stdout.startswith("deploy: src/a.cpp"))
            # a revision that is not there (a force-pushed branch, a first push): deploy
            unknown = run_tool("--base", "0" * 40, "--head", three, root=root)
            self.assertEqual(unknown.returncode, 0)
            self.assertTrue(unknown.stdout.startswith("deploy: the changes between"), unknown.stdout)
            # a deleted file counts through its old name (no rename detection)
            os.remove(os.path.join(root, "src", "a.cpp"))
            self.git(root, "commit", "-q", "-am", "removed")
            self.assertTrue(run_tool("--base", three, "--head", self.git(root, "rev-parse", "HEAD"), root=root).stdout.startswith("deploy: src/a.cpp"))

    def test_an_unreadable_image_definition_deploys(self):
        with tempfile.TemporaryDirectory() as root:
            out = run_tool("--files", root=root, stdin="README.md\n").stdout
            self.assertTrue(out.startswith("deploy: the images' files cannot be read"), out)


class DockerIgnoreRules(unittest.TestCase):
    def ignored(self, text, path):
        return df.DockerIgnore(text).ignored(path)

    def test_a_folder_keeps_out_everything_below_it(self):
        self.assertTrue(self.ignored("docs\n", "docs/a/b.md"))
        self.assertFalse(self.ignored("docs\n", "docsx/a.md"))

    def test_a_bang_lets_a_path_back_in_and_the_last_rule_wins(self):
        text = "docs\n!docs/KEEP.md\n"
        self.assertFalse(self.ignored(text, "docs/KEEP.md"))
        self.assertTrue(self.ignored(text, "docs/OTHER.md"))
        self.assertTrue(self.ignored("!docs/KEEP.md\ndocs\n", "docs/KEEP.md"))                     # an earlier bang is overruled by a later pattern

    def test_a_star_stays_inside_a_folder_name(self):
        self.assertTrue(self.ignored("*.md\n", "README.md"))
        self.assertFalse(self.ignored("*.md\n", "web/page.md"))
        self.assertTrue(self.ignored("*.sh\n", "run_tests.sh"))
        self.assertFalse(self.ignored("*.sh\n", "docker/resolve_build_id.sh"))
        self.assertTrue(self.ignored("Original-Ants/*.exe\n", "Original-Ants/Ants.exe"))
        self.assertFalse(self.ignored("Original-Ants/*.exe\n", "Original-Ants/Maps/a.exe"))

    def test_a_double_star_crosses_folders(self):
        self.assertTrue(self.ignored("**/.DS_Store\n", ".DS_Store"))
        self.assertTrue(self.ignored("**/.DS_Store\n", "a/b/.DS_Store"))
        self.assertTrue(self.ignored("a/**/z\n", "a/b/c/z"))
        self.assertTrue(self.ignored("a/**/z\n", "a/z"))

    def test_comments_and_blank_lines_are_skipped_and_a_prefix_glob_matches_a_family(self):
        text = "# a comment\n\nbuild_*\n"
        self.assertTrue(self.ignored(text, "build_web/x.o"))
        self.assertTrue(self.ignored(text, "build_e2e"))
        self.assertFalse(self.ignored(text, "build.sh"))


class CopyLines(unittest.TestCase):
    def test_sources_flags_continuations_stages_and_urls(self):
        text = ("FROM a AS b\nCOPY CMakeLists.txt VERSION ./\nCOPY --chown=1:1 src/ ./src/\ncopy VERSION .git* /x/\n"
                "COPY --from=b /out/app /app\nCOPY a.txt \\\n  b.txt /dest/\nADD https://example.org/x.tgz /x\nCOPY [\"with space.txt\", \"/d/\"]\nRUN echo COPY not-a-copy /x\n")
        self.assertEqual(df.copy_sources(text), ["CMakeLists.txt", "VERSION", "src/", "VERSION", ".git*", "a.txt", "b.txt", "with space.txt"])

    def test_a_heredoc_copy_is_refused(self):
        with self.assertRaises(ValueError):
            df.copy_sources("COPY <<EOF /x\nhi\nEOF\n")

    def test_a_dot_source_copies_everything_the_ignore_file_lets_through(self):
        with tempfile.TemporaryDirectory() as root:
            write(os.path.join(root, "Dockerfile"), "COPY . /src/\n")
            write(os.path.join(root, "Dockerfile.server"), "FROM x\n")
            write(os.path.join(root, ".dockerignore"), "tests\n")
            classifier = df.Classifier(root, "docker-compose.stack.yml")
            self.assertTrue(classifier.verdict("anything/at/all.txt")[0])
            self.assertFalse(classifier.verdict("tests/a.cpp")[0])


if __name__ == "__main__":
    unittest.main()
