#!/usr/bin/env python3
"""The chat hub's wiring (docs/HUB.md "The stack and the site's nginx"): its image, its service in both stacks, the CI step, the deploy filter, the page in the README.

Read as text where docker is not needed (and with `docker compose config` where it is installed): the image is slim, non-root, read-only-ready, healthy by a command that the image has, has
no build id argument and copies hub/ and nothing else; the service is hardened in both stacks, publishes no port, has a volume of its own; the test login cannot reach a deployment; the
code is the standard library and nothing else; the Web job builds and starts the image (and is still the one job, not a sixth check); the deploy filter counts hub/.
"""
import ast
import json
import os
import re
import shutil
import subprocess
import sys
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, REPO)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hub import limits as limitmod  # noqa: E402
import test_staging_stack as stack_tests  # noqa: E402  (its readers of the compose files)

read = stack_tests.read
STACK = stack_tests.STACK
STAGING = stack_tests.STAGING


def service(text, name):
    """The lines of one service of a compose file (from `  name:` to the next service or the end of the services)."""
    lines = text.splitlines()
    start = next(i for i, l in enumerate(lines) if l == "  %s:" % name)
    end = next((i for i in range(start + 1, len(lines)) if re.match(r"^  [a-z]|^\S", lines[i])), len(lines))
    return "\n".join(l for l in lines[start:end] if not l.lstrip().startswith("#"))


def code(text):
    return "\n".join(l for l in text.splitlines() if not l.lstrip().startswith("#"))


class TheImage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = read(os.path.join(REPO, "Dockerfile.hub"))
        cls.code = code(cls.text)

    def test_it_is_slim_python_and_runs_as_a_user_that_is_not_root(self):
        self.assertRegex(self.code, r"(?m)^FROM python:3\.\d+-slim-\w+\s*$")
        self.assertRegex(self.code, r"(?m)^USER hub\s*$")
        self.assertRegex(self.code, r"useradd --system .*--shell /usr/sbin/nologin hub")
        self.assertLess(self.code.index("useradd"), self.code.index("USER hub"))
        self.assertEqual(len(re.findall(r"(?m)^USER ", self.code)), 1)

    def test_the_data_folder_belongs_to_the_user_so_that_a_new_volume_is_writable(self):
        self.assertRegex(self.code, r"mkdir -p /data .*chown hub /data")
        self.assertIn("ANTS_HUB_DB=/data/hub.db", self.code)

    def test_it_copies_the_code_and_nothing_else_and_makes_the_bytecode_while_it_can_write(self):
        copies = re.findall(r"(?m)^COPY (.*)$", self.code)
        self.assertEqual(copies, ["hub/ /app/hub/"])
        self.assertLess(self.code.index("COPY hub/"), self.code.index("compileall"))
        self.assertLess(self.code.index("compileall"), self.code.index("USER hub"))           # (as root: the image is run read only)
        self.assertIn("PYTHONDONTWRITEBYTECODE=1", self.code)

    def test_it_has_no_build_id_so_that_nothing_but_hub_changes_the_image(self):
        self.assertNotIn("ANTS_BUILD_ID", self.code)
        self.assertNotIn("ARG ", self.code)
        self.assertNotIn(".git", self.code)

    def test_the_health_check_uses_a_command_that_the_image_has(self):
        self.assertRegex(self.code, r'HEALTHCHECK .*\\\s*CMD \["python", "-m", "hub\.health"\]')
        self.assertIn("ENTRYPOINT [\"python\", \"-m\", \"hub\"]", self.code)
        self.assertNotIn("curl", self.code)
        self.assertIn("ANTS_HUB_PORT=4040", self.code)
        self.assertIn("ANTS_HUB_HOST=0.0.0.0", self.code)                                  # (the program's own default is the loopback address: only the image opens it to the stack's network)
        self.assertIn("EXPOSE 4040", self.code)

    def test_nothing_secret_or_test_only_is_in_the_image(self):
        for word in ("SECRET", "TOKEN", "PASSWORD", "ANTS_HUB_TEST_LOGIN", "ANTS_HUB_MODS", "DISCORD"):
            self.assertNotIn(word, self.code, word)

    def test_the_context_keeps_bytecode_out(self):
        ignore = read(os.path.join(REPO, ".dockerignore"))
        self.assertIn("**/__pycache__", ignore)
        self.assertIn("**/*.pyc", ignore)
        self.assertNotRegex(ignore, r"(?m)^/?hub/?\s*$")                                  # (the code itself is let through)

    def test_the_other_images_do_not_copy_the_hub(self):
        for name in ("Dockerfile", "Dockerfile.server"):
            self.assertNotIn("hub", " ".join(re.findall(r"(?m)^COPY (.*)$", read(os.path.join(REPO, name)))), name)


class TheServices(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.files = {"stack": read(STACK), "staging": read(STAGING)}

    def test_it_is_hardened_like_the_game_server_and_smaller(self):
        for name, text in self.files.items():
            hub = service(text, "ants-hub")
            for line in ("read_only: true", "cap_drop: [ALL]", 'security_opt: ["no-new-privileges:true"]', "pids_limit: 32", "mem_limit: 192m", "dockerfile: Dockerfile.hub", "restart: unless-stopped"):
                self.assertIn(line, hub, "%s: %s" % (name, line))
            self.assertRegex(hub, r"tmpfs:\s*\n\s+- /tmp\b")

    def test_it_publishes_no_port_and_has_no_build_argument(self):
        for name, text in self.files.items():
            hub = service(text, "ants-hub")
            self.assertNotIn("ports:", hub, name)
            self.assertNotIn("args:", hub, name)
            self.assertNotIn("ANTS_BUILD_ID", hub, name)
            self.assertNotIn("expose:", hub, name)

    def test_its_log_is_kept_small_in_both_stacks(self):
        for name, text in self.files.items():
            hub = service(text, "ants-hub")
            self.assertRegex(hub, r"logging:\s*\n\s+driver: json-file\s*\n\s+options:\s*\n\s+max-size: \"\d+m\"\s*\n\s+max-file: \"\d+\"", name)
            self.assertNotIn("logging:", service(text, "ants-server"), name)          # (the other services are as they were)

    def test_its_volume_and_network_are_its_own(self):
        stack_hub, staging_hub = service(self.files["stack"], "ants-hub"), service(self.files["staging"], "ants-hub")
        self.assertIn("- ants-hub-data:/data", stack_hub)
        self.assertIn("- ants-staging-hub-data:/data", staging_hub)
        self.assertIn("- proxy-network", stack_hub)
        self.assertIn("- staging", staging_hub)
        self.assertIn("container_name: ants-hub\n", stack_hub + "\n")
        self.assertIn("container_name: ants-hub-staging\n", staging_hub + "\n")
        self.assertIn("ants-hub-data", stack_tests.top_level_keys(self.files["stack"], "volumes"))
        self.assertNotIn("ants-hub-data", stack_tests.top_level_keys(self.files["staging"], "volumes"))

    def test_the_settings_it_is_given(self):
        for name, text in self.files.items():
            hub = service(text, "ants-hub")
            self.assertEqual(sorted(re.findall(r"(?m)^      (ANTS_[A-Z_]+):", hub)), ["ANTS_HUB_MODS", "ANTS_HUB_PUBLIC_URL"], name)
        self.assertIn("ANTS_HUB_PUBLIC_URL: ${ANTS_HUB_PUBLIC_URL:-https://beta.playants.org}", service(self.files["stack"], "ants-hub"))
        self.assertIn("ANTS_HUB_PUBLIC_URL: ${ANTS_HUB_PUBLIC_URL:-}", service(self.files["staging"], "ants-hub"))      # (the staging site has an address of its own: no default)

    def test_the_test_login_and_the_secrets_are_in_no_deployment(self):
        for path in ("docker-compose.stack.yml", "docker-compose.staging.yml", "docker-compose.yml", "docker-compose.server.yml", "Dockerfile", "Dockerfile.server", "Dockerfile.hub",
                     os.path.join("docker", "nginx.conf"), os.path.join(".github", "workflows", "ci.yml")):
            text = code(read(os.path.join(REPO, path)))
            self.assertNotIn("ANTS_HUB_TEST_LOGIN", text, path)
            self.assertNotIn("X-Hub-Test-Login", text, path)
            self.assertNotRegex(text, r"ANTS_HUB_DISCORD_CLIENT_SECRET\s*[:=]\s*\S", path)

    def test_the_game_servers_service_is_not_changed_by_the_hub(self):
        for name, text in self.files.items():
            server = service(text, "ants-server")
            self.assertNotIn("HUB", server, name)
            self.assertIn("pids_limit: 64", server, name)

    @unittest.skipUnless(shutil.which("docker"), "docker is needed")
    def test_compose_renders_the_hub_with_a_log_that_rotates(self):
        for path in (STACK, STAGING):
            done = subprocess.run(["docker", "compose", "-f", path, "config", "--format", "json"], capture_output=True, text=True)
            if done.returncode != 0:
                self.skipTest("docker compose is not available: " + done.stderr[:200])
            logging = json.loads(done.stdout)["services"]["ants-hub"]["logging"]
            self.assertEqual(logging["driver"], "json-file")
            self.assertEqual(logging["options"], {"max-size": "10m", "max-file": "3"})

    @unittest.skipUnless(shutil.which("docker"), "docker is needed")
    def test_compose_renders_the_hub_with_no_published_port(self):
        for path in (STACK, STAGING):
            done = subprocess.run(["docker", "compose", "-f", path, "config", "--format", "json"], capture_output=True, text=True)
            if done.returncode != 0:
                self.skipTest("docker compose is not available: " + done.stderr[:200])
            hub = json.loads(done.stdout)["services"]["ants-hub"]
            self.assertNotIn("ports", hub)
            self.assertTrue(hub["read_only"])
            self.assertEqual(hub["cap_drop"], ["ALL"])
            self.assertEqual(hub["pids_limit"], 32)
            self.assertEqual(int(hub["mem_limit"]), 192 * 1024 * 1024)
            self.assertEqual(list(hub["tmpfs"]), ["/tmp"])
            self.assertEqual(hub["build"]["dockerfile"], "Dockerfile.hub")
            self.assertNotIn("args", hub["build"])


class TheCode(unittest.TestCase):
    def test_it_is_the_standard_library_and_itself_and_nothing_else(self):
        if not hasattr(sys, "stdlib_module_names"):
            self.skipTest("this Python does not list its standard library")
        folder = os.path.join(REPO, "hub")
        names = sorted(n for n in os.listdir(folder) if n.endswith(".py"))
        self.assertIn("__main__.py", names)
        for name in names:
            tree = ast.parse(read(os.path.join(folder, name)), name)
            for node in ast.walk(tree):
                if isinstance(node, ast.Import):
                    modules = [a.name for a in node.names]
                elif isinstance(node, ast.ImportFrom):
                    if node.level:
                        continue                                                  # (from . import x: the hub's own)
                    modules = [node.module]
                else:
                    continue
                for module in modules:
                    self.assertIn(module.split(".")[0], sys.stdlib_module_names, "%s imports %s, which is not the standard library" % (name, module))

    def test_the_sources_hold_no_invisible_or_look_alike_character(self):
        # a bidirectional control or a zero-width character in a source file could make the code read differently from what it does; the tricks are written as escapes (\\u202e) and the files are ASCII
        names = ["Dockerfile.hub", os.path.join("docs", "HUB.md")] + [os.path.join("hub", n) for n in os.listdir(os.path.join(REPO, "hub")) if n.endswith(".py")]
        names += [os.path.join("tests", "scripts", n) for n in os.listdir(os.path.join(REPO, "tests", "scripts")) if n.startswith(("test_hub_", "hub_")) or n == "test_nginx_hub.py"]
        for name in names:
            with open(os.path.join(REPO, name), "rb") as f:
                data = f.read()
            bad = [i for i, b in enumerate(data) if b > 127]
            self.assertEqual(bad, [], "%s holds a byte above 127 at %s" % (name, bad[:3]))

    def test_no_dangerous_call_and_the_environment_is_read_in_few_places(self):
        for name in sorted(os.listdir(os.path.join(REPO, "hub"))):
            if not name.endswith(".py"):
                continue
            text = read(os.path.join(REPO, "hub", name))
            if name not in ("config.py", "admin.py", "health.py", "server.py"):
                self.assertNotIn("os.environ", text, name)
            if name not in ("server.py", "admin.py"):
                self.assertNotRegex(text, r"\bprint\(", name)                          # (what is shown goes through hub/log.py, which keeps secrets out)
            for dangerous in ("eval(", "exec(", "pickle", "subprocess", "os.system", "shell=True"):
                self.assertNotIn(dangerous, text, name)

    def test_the_documented_limits_are_the_ones_in_the_code(self):
        doc = read(os.path.join(REPO, "docs", "HUB.md"))
        defaults = limitmod.Limits()
        for needle in ("5 lines a second (a burst of 50)", "64 MB", "2,000 lines", "closed with output still unsent is cut after 5 seconds", "5 seconds to answer", "5 new sockets a second"):
            self.assertIn(needle, doc, needle)
        self.assertEqual((defaults.global_burst, defaults.global_refill_seconds, defaults.db_max_bytes, defaults.purge_chunk, defaults.close_grace_seconds, defaults.identify_seconds,
                          defaults.checkpoint_retry_seconds, defaults.crowded_sockets), (50, 0.2, 64 * 1024 * 1024, 2000, 5.0, 5.0, 10.0, 6))
        for needle in ("2 KB", "200 code points", "800 bytes", "burst of 3", "every 2 seconds", "30 seconds", "20 within a minute", "8 for each client address", "500 in all", "every 25 seconds",
                       "60 seconds", "last 50 lines", "7 days", "128 KB"):
            self.assertIn(needle, doc, needle)
        self.assertEqual((defaults.frame_max, defaults.line_code_points, defaults.line_bytes, defaults.history_lines, defaults.retention_days, defaults.burst, defaults.refill_seconds,
                          defaults.duplicate_seconds, defaults.violations, defaults.violation_window, defaults.sockets_per_address, defaults.sockets_total, defaults.ping_seconds,
                          defaults.silence_seconds, defaults.write_buffer_max),
                         (2048, 200, 800, 50, 7, 3, 2.0, 30.0, 20, 60.0, 8, 500, 25.0, 60.0, 131072))


class TheWiring(unittest.TestCase):
    def test_the_web_job_builds_and_starts_the_image_and_there_is_no_job_for_it(self):
        workflow = read(os.path.join(REPO, ".github", "workflows", "ci.yml"))
        jobs = re.findall(r"(?m)^  ([a-z0-9_-]+):\s*$", workflow[workflow.index("\njobs:"):])
        self.assertEqual(jobs, ["linux", "macos", "windows", "web", "deploy"])
        web = workflow[workflow.index("\n  web:"):workflow.index("\n  deploy:")]
        step = web[web.index("- name: Build the chat hub image"):]
        step = step[:step.index("\n      # The two stacks")]
        for needle in ("docker build -f Dockerfile.hub -t ants-hub .", "--read-only", "--cap-drop ALL", "--security-opt no-new-privileges", "--pids-limit 32", "--memory 192m", "--tmpfs /tmp",
                       "-v ants-hub-check-data:/data", "State.Health.Status", "= healthy", "python -m hub.admin list", "evil.example", "docker rm -f ants-hub-check"):
            self.assertIn(needle, step, needle)

    def test_the_deploy_filter_counts_the_hub(self):
        sys.path.insert(0, os.path.join(REPO, "tools"))
        import deploy_filter as df
        classifier = df.Classifier(REPO, "docker-compose.stack.yml")
        for path in ("hub/server.py", "hub/ws.py", "Dockerfile.hub"):
            self.assertTrue(classifier.verdict(path)[0], path)
        for path in ("docs/HUB.md", "tests/scripts/test_hub_ws.py", "tests/data/hub_mutants.json"):
            self.assertFalse(classifier.verdict(path)[0], path)
        self.assertIn("Dockerfile.hub", df.DEFINITIONS)

    def test_the_page_is_linked_from_the_readme_once(self):
        readme = read(os.path.join(REPO, "README.md"))
        self.assertEqual(len(re.findall(r"\]\(docs/HUB\.md\)", readme)), 1)
        self.assertTrue(os.path.getsize(os.path.join(REPO, "docs", "HUB.md")) < 100 * 1024)

    def test_the_two_pages_that_are_at_their_size_ceiling_do_not_mention_it(self):
        for name in ("TESTING.md", "SERVER.md"):
            self.assertNotIn("HUB.md", read(os.path.join(REPO, "docs", name)), name)


if __name__ == "__main__":
    unittest.main()
