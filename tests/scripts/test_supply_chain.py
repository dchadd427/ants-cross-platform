#!/usr/bin/env python3
"""What the images and the converter are built from is pinned, and something watches the pins (run by ./run_tests.sh --fast and by the CI).

A tag can be moved to another image, and a package index can serve another file under the same version; a build that takes what the name says today can take something else tomorrow, on the
host that builds the production stack. These are settings with nothing to run (the build itself is run by the CI, which builds every image), so the files are read as text, and each check is
shown to fail on a copy that breaks it:

  - every Dockerfile: each base image from outside (not an earlier stage) names a tag and a digest, `name:tag@sha256:...`
  - .github/dependabot.yml: the converter's packages, the Dockerfiles' base images and the workflows' actions are updated weekly, grouped, against home-next, and every Dockerfile of the
    repository is in a folder that the file watches
"""
import os
import re
import unittest

try:
    import yaml
except ImportError:                                                                        # (macOS runners have no PyYAML: the text checks are the guard there)
    yaml = None

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DIGEST = r"sha256:[0-9a-f]{64}"


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def dockerfiles():
    """The paths (relative to the repository) of every Dockerfile in it, outside the vendored code and the build folders."""
    found = []
    for folder, names, files in os.walk(REPO):
        names[:] = [n for n in names if n not in (".git", "third_party", "node_modules") and not n.startswith("build")]
        found += [os.path.relpath(os.path.join(folder, f), REPO) for f in files if re.match(r"(?i)dockerfile", f)]
    return sorted(found)


def unpinned_images(text):
    """The base images of a Dockerfile that come from outside (a `FROM` that does not name an earlier stage) and have no tag or no digest."""
    stages, found = set(), []
    for line in text.splitlines():
        match = re.match(r"(?i)^FROM\s+(?:--\S+\s+)*(\S+)(?:\s+AS\s+(\S+))?\s*$", line)
        if not match:
            continue
        image, stage = match.group(1), match.group(2)
        if image not in stages and not re.fullmatch(r"[a-z0-9._/-]+:[A-Za-z0-9._-]+@" + DIGEST, image):
            found.append(image)
        if stage:
            stages.add(stage)
    return found


def problems_of_dependabot(config, dockerfile_paths=None):
    """What the Dependabot file lacks: an ecosystem, the weekly grouped updates against home-next, a folder that exists, or a Dockerfile in a folder that no entry watches."""
    problems = []
    entries = {e["package-ecosystem"]: e for e in config.get("updates", [])}
    for ecosystem in ("pip", "docker", "github-actions"):
        entry = entries.get(ecosystem)
        if entry is None:
            problems.append("no %s entry" % ecosystem)
            continue
        if entry.get("target-branch") != "home-next":
            problems.append("%s: not against home-next" % ecosystem)
        if entry.get("schedule", {}).get("interval") != "weekly":
            problems.append("%s: not weekly" % ecosystem)
        if not entry.get("groups"):
            problems.append("%s: not grouped" % ecosystem)
    if "pip" in entries and not os.path.isfile(os.path.join(REPO, entries["pip"]["directory"].strip("/"), "requirements.txt")):
        problems.append("pip: no requirements.txt in its folder")
    if "github-actions" in entries and not os.path.isdir(os.path.join(REPO, ".github", "workflows")):
        problems.append("github-actions: no workflows")
    watched = {entries["docker"]["directory"].strip("/")} if "docker" in entries else set()
    for path in (dockerfile_paths if dockerfile_paths is not None else dockerfiles()):
        if os.path.dirname(path) not in watched:
            problems.append("%s is in a folder that is not watched" % path)
    return problems


class TheBaseImages(unittest.TestCase):
    def test_every_base_image_of_every_dockerfile_is_pinned_by_tag_and_digest(self):
        paths = dockerfiles()
        self.assertGreaterEqual(len(paths), 5)
        for path in paths:
            self.assertEqual(unpinned_images(read(path)), [], path)

    def test_the_check_finds_an_image_without_a_digest_or_a_tag_and_leaves_the_stages_alone(self):
        pinned = "FROM nginx:alpine@sha256:" + "a" * 64 + " AS runner\n"
        self.assertEqual(unpinned_images(pinned), [])
        self.assertEqual(unpinned_images("FROM nginx:alpine AS runner\n"), ["nginx:alpine"])
        self.assertEqual(unpinned_images("FROM nginx@sha256:" + "a" * 64 + "\n"), ["nginx@sha256:" + "a" * 64])
        self.assertEqual(unpinned_images("FROM nginx:alpine@sha256:" + "a" * 63 + "\n"), ["nginx:alpine@sha256:" + "a" * 63])
        self.assertEqual(unpinned_images(pinned + "FROM runner AS second\nFROM second\n"), [], "the name of an earlier stage is not an image")
        self.assertEqual(unpinned_images(pinned + "FROM later AS second\n"), ["later"], "a name that no stage has yet is an image")


@unittest.skipUnless(yaml, "PyYAML is needed")
class TheUpdates(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = yaml.safe_load(read(".github", "dependabot.yml"))

    def test_the_file_watches_the_packages_the_images_and_the_actions_weekly_against_home_next(self):
        self.assertEqual(problems_of_dependabot(self.config), [])
        self.assertEqual(self.config["version"], 2)

    def test_the_check_finds_a_missing_entry_a_wrong_branch_a_missing_group_and_a_folder_that_is_not_watched(self):
        def without(key, ecosystem):
            copy = {"updates": [dict(e) for e in self.config["updates"]]}
            for e in copy["updates"]:
                if e["package-ecosystem"] == ecosystem:
                    e.pop(key)
            return copy
        self.assertEqual(problems_of_dependabot({"updates": [e for e in self.config["updates"] if e["package-ecosystem"] != "docker"]}, dockerfile_paths=[]), ["no docker entry"])
        self.assertEqual(problems_of_dependabot(without("target-branch", "pip")), ["pip: not against home-next"])
        self.assertEqual(problems_of_dependabot(without("groups", "github-actions")), ["github-actions: not grouped"])
        self.assertEqual(problems_of_dependabot(without("schedule", "docker")), ["docker: not weekly"])
        self.assertEqual(problems_of_dependabot(self.config, dockerfile_paths=["Dockerfile", "images/Dockerfile.extra"]), ["images/Dockerfile.extra is in a folder that is not watched"])


class TheCiMachinesHavePyYAML(unittest.TestCase):
    def test_the_linux_jobs_of_the_ci_check_the_dependabot_file(self):
        """The checks of the Dependabot file are skipped without PyYAML: on the Linux machines of the CI it must be there (the job installs python3-yaml)."""
        if os.environ.get("GITHUB_ACTIONS") == "true" and os.environ.get("RUNNER_OS") == "Linux":
            self.assertIsNotNone(yaml, "PyYAML is missing on a Linux machine of the CI: .github/dependabot.yml is not checked")


if __name__ == "__main__":
    unittest.main()
