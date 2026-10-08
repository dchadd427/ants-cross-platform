#!/usr/bin/env python3
"""The staging stack (docker-compose.staging.yml) and the site label of the pages (run by ./run_tests.sh --fast and by the CI).

The staging stack is a second copy of the site for the owner to try finished work (docs/WORKFLOW.md). Both stacks must run on one machine at the same time, so nothing may be
shared: container names, host ports, volumes and the network are the stack's own. The compose files are read as text (no docker needed), and with `docker compose config`
where docker is installed. The page says "staging" in its title and footer when the build argument ANTS_SITE_LABEL says so; the RUN step of the Dockerfile that does it is
cut out of the real Dockerfile and run on copies of the real pages, with a label and without one (empty: the pages must be byte for byte the pages of the repository).
"""
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
STACK = os.path.join(REPO, "docker-compose.stack.yml")
STAGING = os.path.join(REPO, "docker-compose.staging.yml")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def code_lines(text):
    return [l for l in text.splitlines() if not l.lstrip().startswith("#")]


def container_names(text):
    return re.findall(r"^\s+container_name:\s*(\S+)\s*$", "\n".join(code_lines(text)), re.M)


def host_ports(text):
    """The host ports of the `ports:` entries, the ${NAME:-default} replaced by its default."""
    ports = []
    for line in code_lines(text):
        m = re.match(r'^\s+- "(?:127\.0\.0\.1:)?(?:\$\{[A-Z_]+:-(\d+)\}|(\d+)):\d+"\s*$', line)
        if m:
            ports.append(int(m.group(1) or m.group(2)))
    return ports


def top_level_keys(text, section):
    """The names under a top-level `volumes:` or `networks:`."""
    keys = []
    inside = False
    for line in code_lines(text):
        if re.match(r"^\S", line):
            inside = line.startswith(section + ":")
            continue
        m = re.match(r"^  ([A-Za-z0-9_.-]+):", line)
        if inside and m:
            keys.append(m.group(1))
    return keys


class ComposeFilesAsText(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stack = read(STACK)
        cls.staging = read(STAGING)

    def test_container_names_are_its_own(self):
        self.assertEqual(container_names(self.staging), ["ants-beta-staging", "ants-server-staging"])
        self.assertEqual(set(container_names(self.stack)) & set(container_names(self.staging)), set())

    def test_host_ports_are_its_own_and_the_server_ports_stay_on_loopback(self):
        staging_ports = host_ports(self.staging)
        self.assertEqual(sorted(staging_ports), [4003, 4004, 4011, 19981])
        self.assertEqual(set(staging_ports) & set(host_ports(self.stack)), set())
        self.assertEqual(sorted(host_ports(self.stack)), [4001, 4002, 4010, 19980])         # (so that a change of the production ports is noticed here)
        lines = [l for l in code_lines(self.staging) if "ANTS_STAGING_SERVER_WS_PORT" in l or "ANTS_STAGING_SERVER_CTL_PORT" in l]
        self.assertEqual(len(lines), 2)
        for line in lines:
            self.assertIn('"127.0.0.1:', line, "the WebSocket and control ports are for this machine only: " + line)

    def test_volumes_and_the_network_are_its_own_and_not_the_production_network(self):
        self.assertEqual(sorted(top_level_keys(self.staging, "volumes")), ["ants-staging-maps", "ants-staging-results"])
        self.assertEqual(set(top_level_keys(self.stack, "volumes")) & set(top_level_keys(self.staging, "volumes")), set())
        self.assertNotIn("proxy-network", self.staging.replace("docker-compose.stack.yml", ""))      # the page's nginx must not find the production server by the name ants-server
        self.assertNotIn("external: true", "\n".join(code_lines(self.staging)))
        self.assertEqual(top_level_keys(self.staging, "networks"), ["staging"])
        self.assertIn("name: ants-staging-network", self.staging)
        self.assertNotIn("ants-staging", self.stack)

    def test_the_services_are_the_same_two_with_the_same_images_and_the_server_command(self):
        for text in (self.stack, self.staging):
            self.assertIn("dockerfile: Dockerfile\n", text)
            self.assertIn("dockerfile: Dockerfile.server\n", text)
            self.assertRegex(text, r"(?m)^  ants-beta:\s*$")
            self.assertRegex(text, r"(?m)^  ants-server:\s*$")
        command = lambda text: [l.strip() for l in code_lines(text) if l.strip().startswith("command:")]
        self.assertEqual(len(command(self.staging)), 1)
        self.assertEqual(command(self.staging), command(self.stack))                          # the same server options, the demo rooms and TREASURE.LVL as the default map

    def test_only_the_staging_stack_has_the_label_and_the_dockerfile_takes_it(self):
        self.assertIn("ANTS_SITE_LABEL: staging", self.staging)
        self.assertNotIn("ANTS_SITE_LABEL", self.stack)
        self.assertIn("ARG ANTS_SITE_LABEL=\n", read(os.path.join(REPO, "Dockerfile")))
        self.assertNotIn("ANTS_SITE_LABEL", read(os.path.join(REPO, "Dockerfile.server")))    # (the server has no page)

    def test_the_security_options_of_the_server_are_those_of_production(self):
        for line in ("read_only: true", "cap_drop: [ALL]", 'security_opt: ["no-new-privileges:true"]', "pids_limit: 64"):
            self.assertIn(line, self.staging)


@unittest.skipUnless(shutil.which("docker"), "docker is needed")
class ComposeConfig(unittest.TestCase):
    """`docker compose config` renders both files (no build, no daemon needed) and the resolved configurations share nothing."""

    @classmethod
    def setUpClass(cls):
        cls.configs = {}
        for name, path in (("stack", STACK), ("staging", STAGING)):
            done = subprocess.run(["docker", "compose", "-f", path, "config", "--format", "json"], capture_output=True, text=True)
            cls.rendered = done.returncode == 0
            if done.returncode != 0:
                cls.error = done.stderr
                return
            cls.configs[name] = json.loads(done.stdout)

    def setUp(self):
        if not self.rendered:
            self.skipTest("docker compose is not available: " + self.error[:200])

    def test_both_files_render(self):
        self.assertEqual(sorted(self.configs["staging"]["services"]), ["ants-beta", "ants-server"])

    def test_nothing_that_matters_is_shared(self):
        stack, staging = self.configs["stack"], self.configs["staging"]
        names = lambda c: {s.get("container_name") for s in c["services"].values()}
        self.assertEqual(names(stack) & names(staging), set())
        published = lambda c: {int(p["published"]) for s in c["services"].values() for p in s.get("ports", [])}
        self.assertEqual(published(stack) & published(staging), set())
        self.assertEqual(set(stack.get("volumes", {})) & set(staging.get("volumes", {})), set())
        self.assertEqual({n.get("name") for n in staging.get("networks", {}).values()} & {n.get("name") for n in stack.get("networks", {}).values()}, set())
        self.assertEqual(staging["services"]["ants-beta"]["build"]["args"]["ANTS_SITE_LABEL"], "staging")
        self.assertNotIn("ANTS_SITE_LABEL", stack["services"]["ants-beta"]["build"].get("args", {}))


class SiteLabelInThePages(unittest.TestCase):
    """The Dockerfile's step that puts the label into the pages, run on copies of the real pages."""

    @classmethod
    def setUpClass(cls):
        lines = read(os.path.join(REPO, "Dockerfile")).splitlines()
        start = next(i for i, l in enumerate(lines) if l.startswith('RUN SITE_LABEL="${ANTS_SITE_LABEL}"'))
        end = start
        while lines[end].rstrip().endswith("\\"):
            end += 1
        cls.script = "\n".join(l.rstrip().rstrip("\\") for l in lines[start:end + 1]).replace("RUN ", "", 1)

    def run_step(self, label):
        with tempfile.TemporaryDirectory() as tmp:
            os.makedirs(os.path.join(tmp, "src", "web"))
            os.makedirs(os.path.join(tmp, "src", "build_web", "src", "ants_app"))
            shutil.copyfile(os.path.join(REPO, "web", "lobby.html"), os.path.join(tmp, "src", "web", "lobby.html"))
            shutil.copyfile(os.path.join(REPO, "web", "watch.html"), os.path.join(tmp, "src", "web", "watch.html"))
            shutil.copyfile(os.path.join(REPO, "web", "shell.html"), os.path.join(tmp, "src", "build_web", "src", "ants_app", "index.html"))
            script = re.sub(r"(?<![A-Za-z0-9_./])/src/", lambda m: tmp + "/src/", self.script).replace("sed -i ", "sed -i.bak ")      # (GNU sed in the image; -i.bak is BSD sed's too)
            env = dict(os.environ, ANTS_SITE_LABEL=label)
            done = subprocess.run(["sh", "-c", script], capture_output=True, text=True, env=env)
            pages = {}
            if done.returncode == 0:
                pages = {"index": read(os.path.join(tmp, "src", "build_web", "src", "ants_app", "index.html")), "lobby": read(os.path.join(tmp, "src", "lobby.html")), "watch": read(os.path.join(tmp, "src", "watch.html"))}
            return done, pages

    def test_a_label_goes_into_the_title_and_the_footer_of_both_pages(self):
        done, pages = self.run_step("staging")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertIn("<title>Ants (1998) — beta.playants.org (staging)</title>", pages["index"])
        self.assertIn("<title>Ants (1998) (staging)</title>", pages["lobby"])
        self.assertIn("<title>Watch replays — Ants (1998) (staging)</title>", pages["watch"])
        # the footer of both pages is the emerald bar: the label stands in front of the version (and the build), in the same box
        for name in ("index", "lobby", "watch"):
            self.assertRegex(pages[name], r'<footer class="bar">\s*<div class="bar-in">\s*<span><strong id="site-label">staging</strong>&#8197;&bull;&#8197;<span class="ver" id="game-version-line">')
        for page in pages.values():
            self.assertNotIn("@@SITE_", page)

    def test_no_label_leaves_the_pages_as_they_are_in_the_repository(self):
        done, pages = self.run_step("")
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(pages["lobby"], read(os.path.join(REPO, "web", "lobby.html")).replace("@@SITE_TITLE@@", "").replace("@@SITE_FOOTER@@", ""))
        self.assertEqual(pages["index"], read(os.path.join(REPO, "web", "shell.html")).replace("@@SITE_TITLE@@", "").replace("@@SITE_FOOTER@@", ""))
        self.assertEqual(pages["watch"], read(os.path.join(REPO, "web", "watch.html")).replace("@@SITE_TITLE@@", "").replace("@@SITE_FOOTER@@", ""))
        self.assertNotIn("staging", pages["index"].lower())
        self.assertNotIn("site-label", pages["lobby"])

    def test_the_placeholders_are_in_the_pages_of_the_repository_once_each(self):
        for name in ("shell.html", "lobby.html", "watch.html"):
            text = read(os.path.join(REPO, "web", name))
            self.assertEqual(text.count("@@SITE_TITLE@@"), 1, name)
            self.assertEqual(text.count("@@SITE_FOOTER@@"), 1, name)
        self.assertIn('"s/@@SITE_TITLE@@//g"', read(os.path.join(REPO, "build_web.sh")))      # the local web build (no Docker) fills them with nothing too

    def test_a_label_that_could_break_the_build_is_refused(self):
        for label in ("a b", "x|y", "a&b", "<b>", 'q"q', "semi;colon", "$(id)"):
            done, _ = self.run_step(label)
            self.assertNotEqual(done.returncode, 0, "the label %r was accepted" % label)
            self.assertIn("ANTS_SITE_LABEL may hold", done.stderr)
        for label in ("staging", "stage-2", "Test_1.0"):
            self.assertEqual(self.run_step(label)[0].returncode, 0, label)


if __name__ == "__main__":
    unittest.main()
