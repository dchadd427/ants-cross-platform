#!/usr/bin/env python3
"""The chat hub's wiring (docs/HUB.md "The stack and the site's nginx"): its image, its service in the stack, the CI step, the page in the README.

Read as text where docker is not needed (and with `docker compose config` where it is installed): the image is slim, non-root, read-only-ready, healthy by a command that the image has, has
no build id argument and copies hub/ and nothing else; the service is hardened in the stack, publishes no port, has a volume of its own; the test login cannot reach a deployment; the
code is the standard library and nothing else; the Web job builds and starts the image (and is still the one job, not a sixth check).
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
from hub import limits as limitmod, store as storemod  # noqa: E402

STACK = os.path.join(REPO, "docker-compose.stack.yml")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def top_level_keys(text, section):
    """The names under a top-level `volumes:` or `networks:` of a compose file."""
    keys = []
    inside = False
    for line in text.splitlines():
        if line.lstrip().startswith("#"):
            continue
        if re.match(r"^\S", line):
            inside = line.startswith(section + ":")
            continue
        m = re.match(r"^  ([A-Za-z0-9_.-]+):", line)
        if inside and m:
            keys.append(m.group(1))
    return keys


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
        self.assertRegex(self.code, r"(?m)^FROM python:3\.\d+-slim-\w+(@sha256:[0-9a-f]{64})?\s*$")
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
        for word in ("SECRET", "TOKEN", "PASSWORD", "ANTS_HUB_TEST_LOGIN", "ANTS_HUB_MODS", "ANTS_HUB_OWNER", "DISCORD"):
            self.assertNotIn(word, self.code, word)

    def test_the_context_keeps_bytecode_out(self):
        ignore = read(os.path.join(REPO, ".dockerignore"))
        self.assertIn("**/__pycache__", ignore)
        self.assertIn("**/*.pyc", ignore)
        self.assertNotRegex(ignore, r"(?m)^/?hub/?\s*$")                                  # (the code itself is let through)

    def test_the_other_images_do_not_copy_the_hub(self):
        for name in ("Dockerfile", "Dockerfile.server"):
            copied = " ".join(re.findall(r"(?m)^COPY (.*)$", read(os.path.join(REPO, name))))
            self.assertNotIn("hub", copied.replace("web/hub_here.js /usr/share/nginx/html/hub_here.js", ""), name)       # (the game page's small script that talks to the hub is a page file, not the hub)


class TheServices(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.files = {"stack": read(STACK)}

    def test_it_is_hardened_like_the_game_server_and_smaller(self):
        for name, text in self.files.items():
            hub = service(text, "ants-hub")
            for line in ("read_only: true", "cap_drop: [ALL]", 'security_opt: ["no-new-privileges:true"]', "pids_limit: 32", "mem_limit: 256m", "dockerfile: Dockerfile.hub", "restart: unless-stopped"):
                self.assertIn(line, hub, "%s: %s" % (name, line))
            self.assertRegex(hub, r"tmpfs:\s*\n\s+- /tmp\b")

    def test_it_publishes_no_port_and_has_no_build_argument(self):
        for name, text in self.files.items():
            hub = service(text, "ants-hub")
            self.assertNotIn("ports:", hub, name)
            self.assertNotIn("args:", hub, name)
            self.assertNotIn("ANTS_BUILD_ID", hub, name)
            self.assertNotIn("expose:", hub, name)

    def test_its_log_is_kept_small_in_the_stack(self):
        for name, text in self.files.items():
            hub = service(text, "ants-hub")
            self.assertRegex(hub, r"logging:\s*\n\s+driver: json-file\s*\n\s+options:\s*\n\s+max-size: \"\d+m\"\s*\n\s+max-file: \"\d+\"", name)
            self.assertNotIn("logging:", service(text, "ants-server"), name)          # (the other services are as they were)

    def test_its_volume_and_network_are_its_own(self):
        stack_hub = service(self.files["stack"], "ants-hub")
        self.assertIn("- ants-hub-data:/data", stack_hub)
        self.assertIn("- proxy-network", stack_hub)
        self.assertIn("- img-network", stack_hub)
        self.assertIn("- ants-hub-images:/images", stack_hub)
        self.assertIn("container_name: ants-hub\n", stack_hub + "\n")
        self.assertIn("ants-hub-data", top_level_keys(self.files["stack"], "volumes"))

    def test_the_settings_it_is_given(self):
        for name, text in self.files.items():
            hub = service(text, "ants-hub")
            self.assertEqual(sorted(re.findall(r"(?m)^      (ANTS_[A-Z_]+):", hub)), ["ANTS_ADMIN_GATE_SECRET", "ANTS_HUB_ADMIN_GATE_SECRET", "ANTS_HUB_DISCORD_BOT_TOKEN", "ANTS_HUB_DISCORD_CLIENT_ID", "ANTS_HUB_DISCORD_CLIENT_SECRET",
                                                                                     "ANTS_HUB_DISCORD_GUILD", "ANTS_HUB_GAME_ADMIN_URL", "ANTS_HUB_IMAGE_CONVERTER_URL", "ANTS_HUB_IMAGE_DIR", "ANTS_HUB_MIN_ACCOUNT_DAYS", "ANTS_HUB_OWNER", "ANTS_HUB_PUBLIC_URL",
                                                                                     "ANTS_HUB_ROLE_ADMIN", "ANTS_HUB_ROLE_IMAGES", "ANTS_HUB_ROLE_MOD", "ANTS_HUB_ROLE_RECHECK_SECONDS"], name)
            for secret in ("ANTS_HUB_DISCORD_BOT_TOKEN", "ANTS_HUB_DISCORD_GUILD", "ANTS_HUB_ROLE_ADMIN", "ANTS_HUB_ROLE_MOD", "ANTS_HUB_ROLE_IMAGES", "ANTS_HUB_OWNER", "ANTS_HUB_ADMIN_GATE_SECRET", "ANTS_ADMIN_GATE_SECRET"):
                self.assertIn("%s: ${%s:-}\n" % (secret, secret), hub + "\n", name)                  # (the bot's token, the server and the roles: from the stack's environment only, no value in the file)
            self.assertIn("ANTS_HUB_ROLE_RECHECK_SECONDS: ${ANTS_HUB_ROLE_RECHECK_SECONDS:-180}\n", hub + "\n", name)
            self.assertNotIn("ANTS_HUB_MODS", hub, name)
            for other in ("ants-server", "ants-beta"):                                              # (the roles' settings are the hub's alone)
                if re.search(r"(?m)^  %s:" % other, text):
                    self.assertNotRegex(service(text, other), r"ANTS_HUB_(?:DISCORD|ROLE|OWNER)", "%s: %s" % (name, other))
            self.assertIn("ANTS_HUB_DISCORD_CLIENT_ID: ${ANTS_HUB_DISCORD_CLIENT_ID:-}\n", hub + "\n", name)           # (from the stack's environment, and nothing in the file: no default, no value)
            self.assertIn("ANTS_HUB_DISCORD_CLIENT_SECRET: ${ANTS_HUB_DISCORD_CLIENT_SECRET:-}\n", hub + "\n", name)
            self.assertIn("ANTS_HUB_MIN_ACCOUNT_DAYS: ${ANTS_HUB_MIN_ACCOUNT_DAYS:-7}\n", hub + "\n", name)
            self.assertNotIn("ANTS_HUB_TEST", hub, name)
        self.assertIn("ANTS_HUB_PUBLIC_URL: ${ANTS_HUB_PUBLIC_URL:-https://beta.playants.org}", service(self.files["stack"], "ants-hub"))

    def test_the_test_login_and_the_secrets_are_in_no_deployment(self):
        for path in ("docker-compose.stack.yml", "docker-compose.yml", "docker-compose.server.yml", "Dockerfile", "Dockerfile.server", "Dockerfile.hub",
                     os.path.join("docker", "nginx.conf"), os.path.join(".github", "workflows", "ci.yml")):
            text = code(read(os.path.join(REPO, path)))
            self.assertNotIn("ANTS_HUB_TEST_LOGIN", text, path)
            self.assertNotIn("ANTS_HUB_TEST_DISCORD_URL", text, path)                       # (the fake Discord of the tests: no deployment can point the login at it)
            self.assertNotIn("X-Hub-Test-Login", text, path)
            for name in ("ANTS_HUB_DISCORD_CLIENT_SECRET", "ANTS_HUB_DISCORD_CLIENT_ID"):
                for line in text.splitlines():
                    if name in line:                                                          # (a stack file takes the secret and the id from the stack's environment, with no value of its own)
                        self.assertEqual(line.strip(), "%s: ${%s:-}" % (name, name), path)
                        self.assertTrue(path.startswith("docker-compose.st"), path)

    def test_the_stack_has_these_services_and_only_the_relay_is_behind_a_profile(self):
        text = self.files["stack"]
        self.assertEqual(top_level_keys(text, "services"), ["ants-beta", "ants-server", "ants-hub", "ants-img", "ants-turn"])
        for name in ("ants-beta", "ants-server", "ants-hub"):
            self.assertNotIn("profiles:", code(service(text, name)), name)
        self.assertIn('profiles: ["relay"]', code(service(text, "ants-turn")), "the relay is optional: it starts only with the profile, so that the stack deploys without its three variables")
        for dockerfile in ("Dockerfile", "Dockerfile.server", "Dockerfile.hub"):
            self.assertIn("dockerfile: %s\n" % dockerfile, text)
        for line in ("read_only: true", "cap_drop: [ALL]", 'security_opt: ["no-new-privileges:true"]', "pids_limit: 64"):
            self.assertIn(line, service(text, "ants-server"))
        ports = [int(m.group(1) or m.group(2)) for m in re.finditer(r'(?m)^\s+- "(?:\$\{ANTS_WEB_BIND_PREFIX:-\})?(?:127\.0\.0\.1:)?(?:\$\{[A-Z_]+:-(\d+)\}|(\d+)):\d+"\s*$', code(text))]
        self.assertEqual(sorted(ports), [4001, 4002, 4010, 19980])

    def test_the_game_servers_service_is_not_changed_by_the_hub(self):
        for name, text in self.files.items():
            server = service(text, "ants-server")
            self.assertNotIn("HUB", server, name)
            self.assertIn("pids_limit: 64", server, name)

    @unittest.skipUnless(shutil.which("docker"), "docker is needed")
    def test_compose_renders_the_hub_with_a_log_that_rotates(self):
        for path in (STACK,):
            done = subprocess.run(["docker", "compose", "-f", path, "config", "--format", "json"], capture_output=True, text=True)
            if done.returncode != 0:
                self.skipTest("docker compose is not available: " + done.stderr[:200])
            logging = json.loads(done.stdout)["services"]["ants-hub"]["logging"]
            self.assertEqual(logging["driver"], "json-file")
            self.assertEqual(logging["options"], {"max-size": "10m", "max-file": "3"})

    @unittest.skipUnless(shutil.which("docker"), "docker is needed")
    def test_compose_hands_the_one_admin_key_to_the_hub_and_to_the_game_server(self):
        """One variable is enough: the stack gives it to both services under the same name; a stack that still sets the hub's old name too keeps both (the hub then reads the shared one first)."""
        key = "made-up-stack-key-4e7b2a9c1d0f8365-x"
        for label, extra in (("the one variable", {"ANTS_ADMIN_GATE_SECRET": key}), ("both names", {"ANTS_ADMIN_GATE_SECRET": key, "ANTS_HUB_ADMIN_GATE_SECRET": key})):
            env = {k: v for k, v in os.environ.items() if not k.startswith("ANTS_")}
            env.update(extra)
            done = subprocess.run(["docker", "compose", "-f", STACK, "config", "--format", "json"], capture_output=True, text=True, env=env)
            if done.returncode != 0:
                self.skipTest("docker compose is not available: " + done.stderr[:200])
            services = json.loads(done.stdout)["services"]
            self.assertEqual(services["ants-hub"]["environment"]["ANTS_ADMIN_GATE_SECRET"], key, label)
            self.assertEqual(services["ants-server"]["environment"]["ANTS_ADMIN_GATE_SECRET"], key, label)
            self.assertEqual(services["ants-hub"]["environment"]["ANTS_HUB_ADMIN_GATE_SECRET"], extra.get("ANTS_HUB_ADMIN_GATE_SECRET", ""), label)
            self.assertNotIn("ANTS_HUB_ADMIN_GATE_SECRET", services["ants-server"]["environment"], label)

    @unittest.skipUnless(shutil.which("docker"), "docker is needed")
    def test_compose_renders_the_hub_with_no_published_port(self):
        for path in (STACK,):
            done = subprocess.run(["docker", "compose", "-f", path, "config", "--format", "json"], capture_output=True, text=True)
            if done.returncode != 0:
                self.skipTest("docker compose is not available: " + done.stderr[:200])
            hub = json.loads(done.stdout)["services"]["ants-hub"]
            self.assertNotIn("ports", hub)
            self.assertTrue(hub["read_only"])
            self.assertEqual(hub["cap_drop"], ["ALL"])
            self.assertEqual(hub["pids_limit"], 32)
            self.assertEqual(int(hub["mem_limit"]), 256 * 1024 * 1024)
            self.assertEqual(list(hub["tmpfs"]), ["/tmp"])
            self.assertEqual(hub["build"]["dockerfile"], "Dockerfile.hub")
            self.assertNotIn("args", hub["build"])


class TheConverterService(unittest.TestCase):
    """The image converter's container (docs/HUB_IMAGES.md, "The stack"): walled in, holding nothing, and the one network that the hub shares with it has no way out."""

    @classmethod
    def setUpClass(cls):
        cls.text = read(STACK)
        cls.img = service(cls.text, "ants-img")

    def test_it_is_hardened_and_small(self):
        for line in ("read_only: true", "cap_drop: [ALL]", 'security_opt: ["no-new-privileges:true"]', "pids_limit: 16", "mem_limit: 768m", "cpus: 1", "dockerfile: Dockerfile.img", "restart: unless-stopped"):
            self.assertIn(line, self.img, line)
        self.assertRegex(self.img, r"tmpfs:\s*\n\s+- /tmp\b")

    def test_it_holds_no_secret_and_mounts_nothing_and_publishes_nothing(self):
        for word in ("ports:", "expose:", "volumes:", "environment:", "env_file", "args:", "secrets:", "network_mode", "privileged", "cap_add", "devices", "extra_hosts"):
            self.assertNotIn(word, self.img, word)

    def test_its_network_is_internal_and_has_the_hub_and_nobody_else(self):
        self.assertRegex(self.img, r"networks:\s*\n\s+- img-network\s*(?:\n\s+(?!-)|$)")
        self.assertNotIn("proxy-network", self.img)
        self.assertIn("img-network", top_level_keys(self.text, "networks"))
        block = self.text[self.text.index("  img-network:"):]
        self.assertRegex(block, r"^  img-network:\s*\n    internal: true", "a network with no way out")
        members = [name for name in ("ants-beta", "ants-server", "ants-hub", "ants-img", "ants-turn") if "img-network" in service(self.text, name)]
        self.assertEqual(members, ["ants-hub", "ants-img"])

    def test_the_site_reads_the_images_and_the_hub_writes_them_and_nothing_else_has_them(self):
        self.assertIn("- ants-hub-images:/srv/hub-img:ro", service(self.text, "ants-beta"))
        self.assertIn("- ants-hub-images:/images\n", service(self.text, "ants-hub") + "\n")
        for name in ("ants-server", "ants-img", "ants-turn"):
            self.assertNotIn("ants-hub-images", service(self.text, name), name)
        self.assertIn("ants-hub-images", top_level_keys(self.text, "volumes"))

    def test_the_hub_reaches_it_by_its_name_and_may_be_told_that_there_is_none(self):
        self.assertIn("ANTS_HUB_IMAGE_DIR: /images", service(self.text, "ants-hub"))

    def test_an_empty_address_switches_images_off_and_only_an_unset_one_takes_the_default(self):
        """The stack file rendered by docker compose, three times: the address not set (the default: images on), set and empty (off: the hub is given an empty address, not the default), set to another."""
        seen = {}
        for label, given in (("unset", None), ("empty", ""), ("other", "http://converter.test:4050")):
            env = {k: v for k, v in os.environ.items() if k != "ANTS_HUB_IMAGE_CONVERTER_URL"}
            if given is not None:
                env["ANTS_HUB_IMAGE_CONVERTER_URL"] = given
            try:
                done = subprocess.run(["docker", "compose", "-f", STACK, "config", "--format", "json"], capture_output=True, text=True, env=env)
            except OSError as e:                                                                                       # (no docker program at all: a machine that has none)
                self.skipTest("docker is not available: %s" % e)
            if done.returncode != 0:
                self.skipTest("docker compose is not available: " + done.stderr[:200])
            seen[label] = json.loads(done.stdout)["services"]["ants-hub"]["environment"].get("ANTS_HUB_IMAGE_CONVERTER_URL")
        self.assertEqual(seen, {"unset": "http://ants-img:4050", "empty": "", "other": "http://converter.test:4050"})

    def test_a_conversion_cannot_take_what_the_container_has(self):
        """The most that a conversion may take (imgconv/server.py WORKING_BYTES, on top of the address space that its process starts with) leaves the converter's container a quarter of a gigabyte at least for the
        server, the interpreter and the libraries: the kernel's OOM killer is never the one that ends a big image."""
        working = eval(re.search(r"(?m)^WORKING_BYTES = ([0-9 *]+)", read(os.path.join(REPO, "imgconv", "server.py"))).group(1))     # (digits and stars only: the regular expression says so)
        limit = re.search(r"(?m)^    mem_limit: ([0-9]+)m$", self.img).group(1)
        self.assertGreaterEqual(int(limit) * 1024 * 1024, working + 256 * 1024 * 1024)

    def test_the_image_is_made_from_its_own_folder_and_runs_as_nobody_in_particular(self):
        docker = read(os.path.join(REPO, "Dockerfile.img"))
        copied = " ".join(re.findall(r"(?m)^COPY (.*)$", docker))
        self.assertEqual(sorted(copied.split()), sorted(["imgconv/requirements.txt", "/app/requirements.txt", "imgconv/", "/app/imgconv/"]))
        self.assertRegex(docker, r"(?m)^USER img$")
        self.assertIn("HEALTHCHECK", docker)
        self.assertNotIn("ANTS_BUILD_ID", "\n".join(l for l in docker.splitlines() if not l.lstrip().startswith("#")))
        self.assertRegex(read(os.path.join(REPO, "imgconv", "requirements.txt")), r"(?m)^Pillow==[0-9]+\.[0-9]+\.[0-9]+ \\$")

    def test_the_hub_does_not_import_the_converter_or_pillow(self):
        for name in os.listdir(os.path.join(REPO, "hub")):
            if name.endswith(".py"):
                text = read(os.path.join(REPO, "hub", name))
                self.assertNotRegex(text, r"(?m)^\s*(?:import|from)\s+(?:PIL|imgconv|convert)\b", name)

    def test_the_web_job_builds_and_starts_the_converter_too(self):
        workflow = read(os.path.join(REPO, ".github", "workflows", "ci.yml"))
        step = workflow[workflow.index("- name: Build the image converter and start it with the stack's hardening"):]
        step = step[:step.index("# The stack file")]
        for needle in ("docker buildx build --load -f Dockerfile.img -t ants-img .", "--read-only", "--cap-drop ALL", "--security-opt no-new-privileges", "--pids-limit 16", "--memory 768m", "--cpus 1", "--tmpfs /tmp",
                       "State.Health.Status", "= healthy", "/convert", "docker rm -f ants-img-check", "4898, 4898", "State.OOMKilled"):
            self.assertIn(needle, step, needle)
        self.assertIn("python3-pil", workflow)
        self.assertIn('ANTS_REQUIRE_PILLOW: "1"', workflow)


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
        names = ["Dockerfile.hub", os.path.join("docs", "HUB.md"), os.path.join("docs", "HUB_COMMANDS.md"), os.path.join("docs", "HUB_PICTURES.md"), os.path.join("docs", "HUB_IMAGES.md"), "Dockerfile.img"] + [os.path.join("hub", n) for n in os.listdir(os.path.join(REPO, "hub")) if n.endswith(".py")]
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
        for needle in ("10 lines a second (a burst of 100)", "64 MB", "2,000 lines", "closed with output still unsent is cut after 5 seconds", "5 seconds to answer", "5 new sockets a second"):
            self.assertIn(needle, doc, needle)
        self.assertEqual((defaults.global_burst, defaults.global_refill_seconds, defaults.db_max_bytes, defaults.purge_chunk, defaults.close_grace_seconds, defaults.identify_seconds,
                          defaults.checkpoint_retry_seconds, defaults.crowded_sockets), (100, 0.1, 64 * 1024 * 1024, 2000, 5.0, 5.0, 10.0, 6))
        for needle in ("16 KB", "1,000 code points", "4,000 bytes", "burst of 10, then one line every half second", "30 seconds", "20 within a minute", "8 for each client address", "300 in all", "every 25 seconds",
                       "60 seconds", "last 50 lines", "7 days", "256 KB"):
            self.assertIn(needle, doc, needle)
        self.assertEqual((defaults.frame_max, defaults.line_code_points, defaults.line_bytes, defaults.history_lines, defaults.retention_days, defaults.burst, defaults.refill_seconds,
                          defaults.duplicate_seconds, defaults.violations, defaults.violation_window, defaults.sockets_per_address, defaults.sockets_total, defaults.ping_seconds,
                          defaults.silence_seconds, defaults.write_buffer_max),
                         (16384, 1000, 4000, 50, 7, 10, 0.5, 30.0, 20, 60.0, 8, 300, 25.0, 60.0, 262144))

    def test_the_documented_limits_of_the_pictures_are_the_ones_in_the_code(self):
        doc = read(os.path.join(REPO, "docs", "HUB_PICTURES.md"))
        defaults = limitmod.Limits()
        for needle in ("one fetch at a time for an account, 2 at a time and 32 waiting, a burst of 5 and then one a second for the hub", "3 seconds to connect and 6 in all", "49,152 bytes read and 32,768 kept",
                       "2,000 pictures and 8 MiB", "20 a second for an address (a burst of 240, in nginx and in the hub)", "remembered 400 days",
                       "a burst of 3, then one every 30 seconds, for an account across all its tabs"):
            self.assertIn(needle, doc, needle)
        self.assertEqual((defaults.picture_inflight, defaults.picture_waiting, defaults.picture_burst, defaults.picture_refill_seconds, defaults.discord_connect_seconds,
                          defaults.discord_seconds, defaults.picture_read_max, defaults.picture_bytes_max, defaults.pictures_rows_max, defaults.pictures_bytes_max, defaults.picture_serve_burst,
                          defaults.picture_serve_refill_seconds),
                         (2, 32, 5, 1.0, 3.0, 6.0, 49152, 32768, 2000, 8 * 1024 * 1024, 240, 0.05))
        self.assertEqual((defaults.pics_burst, defaults.pics_refill_seconds, defaults.pick_burst, defaults.pick_refill_seconds), (3, 30.0, 3, 30.0))
        self.assertEqual(storemod.PICTURE_DAYS, 400)


class TheWiring(unittest.TestCase):
    def test_the_web_job_builds_and_starts_the_image_and_there_is_no_job_for_it(self):
        workflow = read(os.path.join(REPO, ".github", "workflows", "ci.yml"))
        jobs = re.findall(r"(?m)^  ([a-z0-9_-]+):\s*$", workflow[workflow.index("\njobs:"):])
        self.assertEqual(jobs, ["changes", "linux_tests", "linux", "macos_tests", "macos", "windows_tests", "windows_2022", "windows_2026", "web_image", "web_stack", "web", "image_files"])
        web = workflow[workflow.index("\n  web_stack:"):workflow.index("\n  image_files:")]
        step = web[web.index("- name: Build the chat hub image"):]
        step = step[:step.index("\n      # The stack file")]
        for needle in ("docker buildx build --load -f Dockerfile.hub -t ants-hub .", "--read-only", "--cap-drop ALL", "--security-opt no-new-privileges", "--pids-limit 32", "--memory 256m", "--tmpfs /tmp",
                       "-v ants-hub-check-data:/data", "State.Health.Status", "= healthy", "python -m hub.admin list", "evil.example", "docker rm -f ants-hub-check"):
            self.assertIn(needle, step, needle)

    def test_the_page_is_linked_from_the_readme_once(self):
        readme = read(os.path.join(REPO, "README.md"))
        self.assertEqual(len(re.findall(r"\]\(docs/HUB\.md\)", readme)), 1)

    def test_the_two_pages_that_are_at_their_size_ceiling_do_not_mention_it(self):
        for name in ("TESTING.md", "SERVER.md"):
            self.assertNotIn("HUB.md", read(os.path.join(REPO, "docs", name)), name)


if __name__ == "__main__":
    unittest.main()
