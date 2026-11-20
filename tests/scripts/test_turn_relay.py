#!/usr/bin/env python3
"""The relay of the players' channels (docs/SERVER.md "The relay for the players' channels"): its entry point, its configuration and its place in the stack.

What is checked:
  - the entry point: the port that coturn is told to listen on is ANTS_TURN_PORT (3478 when it is not set) and nothing else, a port that is not one is refused, a start without the secret, the
    address or the name is refused, a name that carries a port is refused (the port is one number), and `--health` asks for a STUN binding on that same port; it is run for real, with the image's
    own entry point and the STUN client replaced by programs that write down what they were given;
  - the configuration: the relay binds one listening port and its relay range and no other port that coturn knows (an alternative listener, the TLS ports, the command-line interface, the web admin,
    the metrics), neither in docker/turnserver.conf nor on the command line of the stack, so that a second TURN server on the machine meets none of its ports; the range is the one that the documents
    tell the operator to open;
  - the stack: the one variable ANTS_TURN_PORT reaches the relay and the game server (which tells the players) and the number 3478 is written nowhere in the stack file, the health check is the entry
    point's, and the operator's steps name every variable that the relay reads.
"""
import json
import os
import re
import shutil
import stat
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ENTRY = os.path.join(REPO, "docker", "turn-entrypoint.sh")
CONF = os.path.join(REPO, "docker", "turnserver.conf")
STACK = os.path.join(REPO, "docker-compose.stack.yml")
DOCKERFILE = os.path.join(REPO, "Dockerfile.turn")
SERVER_DOC = os.path.join(REPO, "docs", "SERVER.md")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def code(text):
    return "\n".join(l for l in text.splitlines() if not l.lstrip().startswith("#"))


def service(text, name):
    lines = text.splitlines()
    start = next(i for i, l in enumerate(lines) if l == "  %s:" % name)
    end = next((i for i in range(start + 1, len(lines)) if re.match(r"^  [a-z]|^\S", lines[i])), len(lines))
    return "\n".join(l for l in lines[start:end] if not l.lstrip().startswith("#"))


def options(text):
    """The options of a coturn configuration file: {name: value}, a switch with no value is {name: ''}; comment and empty lines are not options."""
    found = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, _, value = line.partition("=")
        found[name.strip()] = value.strip()
    return found


# The ports that coturn would bind if an option were set; none of these options may be set (the relay binds its listening port, which comes from the entry point, and its relay range).
OTHER_PORT_OPTIONS = ("listening-port", "alt-listening-port", "tls-listening-port", "alt-tls-listening-port", "tcp-proxy-port", "dtls", "cli", "cli-port", "web-admin", "web-admin-port", "prometheus",
                      "prometheus-port", "prometheus-tls", "rfc5780")


class Sandbox:
    """The entry point as it runs in the image, with the image's own entry point and turnutils_stunclient made into programs that write down their arguments."""

    def __init__(self):
        self.dir = tempfile.mkdtemp(prefix="turn-entry-")
        self.record = os.path.join(self.dir, "record")
        bin_dir = os.path.join(self.dir, "bin")
        os.makedirs(bin_dir)
        self.image_entry = os.path.join(self.dir, "docker-entrypoint.sh")
        self._program(self.image_entry, 'printf "%s\\n" "$@" > "$RECORD"\n')
        self._program(os.path.join(bin_dir, "turnutils_stunclient"), 'printf "%s\\n" "$@" > "$RECORD"\nexit "${FAKE_STUN_EXIT:-0}"\n')
        text = read(ENTRY)
        self.script = os.path.join(self.dir, "turn-entrypoint.sh")
        replaced, count = re.subn(r"/usr/local/bin/docker-entrypoint\.sh", self.image_entry, text)
        assert count == 1, "the entry point hands over to the image's own entry point exactly once"
        with open(self.script, "w", encoding="utf-8") as f:
            f.write(replaced)
        self.path = bin_dir + os.pathsep + os.environ.get("PATH", "")

    def _program(self, path, body):
        with open(path, "w", encoding="utf-8") as f:
            f.write("#!/bin/sh\n" + body)
        os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)

    def run(self, env=None, args=()):
        full = {"PATH": self.path, "RECORD": self.record}
        full.update(env or {})
        if os.path.exists(self.record):
            os.remove(self.record)
        done = subprocess.run(["sh", self.script] + list(args), capture_output=True, text=True, env=full)
        called = read(self.record).splitlines() if os.path.exists(self.record) else None
        return done.returncode, done.stderr, called

    def close(self):
        shutil.rmtree(self.dir, ignore_errors=True)


GOOD = {"ANTS_TURN_SECRET": "a-made-up-secret", "ANTS_TURN_EXTERNAL_IP": "203.0.113.9", "ANTS_TURN_HOST": "turn.example.org"}
ARGS = ["-c", "/etc/coturn/turnserver.conf", "--static-auth-secret=$ANTS_TURN_SECRET", "--realm=$ANTS_TURN_HOST"]


class TheEntryPoint(unittest.TestCase):
    def setUp(self):
        self.box = Sandbox()
        self.addCleanup(self.box.close)

    def test_coturn_listens_on_the_port_of_the_variable_and_3478_without_one(self):
        code_, _, called = self.box.run(GOOD, ARGS)
        self.assertEqual(code_, 0)
        self.assertEqual(called, ARGS + ["--listening-port=3478"], "the arguments of the stack reach the image's entry point as they are (the `$` is the image's to expand), and the port is added")
        for value, expected in (("3480", "3480"), ("1", "1"), ("65535", "65535"), ("", "3478")):
            code_, _, called = self.box.run(dict(GOOD, ANTS_TURN_PORT=value), ARGS)
            self.assertEqual(code_, 0, value)
            self.assertEqual(called, ARGS + ["--listening-port=" + expected], value)

    def test_a_port_that_is_not_one_is_refused_and_nothing_starts(self):
        for bad in ("abc", "0", "65536", "99999999999", "34 78", "3480x", "-1", "3478.5", "3478:3479", "0x10", " "):
            code_, err, called = self.box.run(dict(GOOD, ANTS_TURN_PORT=bad), ARGS)
            self.assertEqual(code_, 1, repr(bad))
            self.assertIsNone(called, "nothing was started for %r" % bad)
            self.assertIn("ANTS_TURN_PORT", err)

    def test_a_start_without_the_secret_the_address_or_the_name_is_refused(self):
        for missing in GOOD:
            env = {k: v for k, v in GOOD.items() if k != missing}
            code_, err, called = self.box.run(env, ARGS)
            self.assertEqual(code_, 1, missing)
            self.assertIsNone(called, missing)
            self.assertIn(missing, err)
            code_, _, called = self.box.run(dict(GOOD, **{missing: ""}), ARGS)
            self.assertEqual((code_, called), (1, None), "empty " + missing)

    def test_the_name_carries_no_port_so_that_the_number_is_written_once(self):
        for host in ("turn.example.org:3480", "turn.example.org:3478", "turn.example.org:"):
            code_, err, called = self.box.run(dict(GOOD, ANTS_TURN_HOST=host), ARGS)
            self.assertEqual(code_, 1, host)
            self.assertIsNone(called, host)
            self.assertIn("ANTS_TURN_PORT", err)

    def test_the_health_check_asks_for_a_stun_binding_on_the_same_port(self):
        code_, _, called = self.box.run({}, ["--health"])                                   # (no secret, no name: the check needs none of them)
        self.assertEqual((code_, called), (0, ["-p", "3478", "127.0.0.1"]))
        code_, _, called = self.box.run({"ANTS_TURN_PORT": "3480"}, ["--health"])
        self.assertEqual((code_, called), (0, ["-p", "3480", "127.0.0.1"]))
        code_, _, called = self.box.run({"ANTS_TURN_PORT": "3480", "FAKE_STUN_EXIT": "3"}, ["--health"])
        self.assertEqual(code_, 3, "a relay that does not answer is unhealthy")
        code_, _, called = self.box.run({"ANTS_TURN_PORT": "70000"}, ["--health"])
        self.assertEqual((code_, called), (1, None))


class TheConfiguration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.conf = read(CONF)
        cls.opts = options(cls.conf)
        cls.stack = read(STACK)

    def test_it_binds_no_port_but_its_listening_port_and_its_range(self):
        for name in OTHER_PORT_OPTIONS:
            self.assertNotIn(name, self.opts, "%s would make coturn bind a port that this relay does not own (the listening port comes from ANTS_TURN_PORT through the entry point)" % name)
        self.assertIn("no-tls", self.opts, "no TLS or DTLS listener (5349, 5350)")
        self.assertIn("no-rfc5780", self.opts, "no alternative listener (the listening port + 1)")
        self.assertNotIn("no-cli", self.opts, "the old switch is deprecated and logs an error; the command-line interface is off unless `cli` is set")
        command = service(self.stack, "ants-turn")
        for name in OTHER_PORT_OPTIONS:
            self.assertNotRegex(command, r"--%s\b" % re.escape(name), "the stack's command line sets no port option: %s" % name)

    def test_the_relay_range_is_200_ports_that_hold_no_other_port_of_the_relay(self):
        low, high = int(self.opts["min-port"]), int(self.opts["max-port"])
        self.assertEqual(high - low + 1, 200)
        self.assertGreaterEqual(low, 1024)
        self.assertLessEqual(high, 65535)
        for port in (3478, 3479, 5349, 5350, 5766, 8080, 9641):
            self.assertFalse(low <= port <= high, port)

    def test_the_range_that_the_documents_tell_the_operator_to_open_is_the_configurations(self):
        rng = "%s-%s" % (self.opts["min-port"], self.opts["max-port"])
        section = read(SERVER_DOC)
        section = section[section.index("## The relay for the players' channels"):]
        section = section[:section.index("\n## ", 5)]
        self.assertIn("UDP **%s**" % rng, section)
        self.assertIn(rng, self.stack)


class TheStack(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stack = read(STACK)
        cls.relay = service(cls.stack, "ants-turn")
        cls.server = service(cls.stack, "ants-server")

    def test_the_one_variable_reaches_the_relay_and_the_game_server_and_the_number_is_written_nowhere(self):
        line = "ANTS_TURN_PORT: ${ANTS_TURN_PORT:-}"
        self.assertIn(line, self.relay)
        self.assertIn(line, self.server)
        self.assertNotIn("3478", code(self.stack), "the default port is the entry point's and the server's own; the stack file has no second home for it")
        self.assertNotIn("--listening-port", self.relay, "the entry point adds the port from the variable")
        self.assertNotIn("turnutils_stunclient", self.relay, "the health check is the entry point's, which knows the port")

    def test_the_health_check_runs_the_entry_point_that_the_image_has(self):
        dockerfile = read(DOCKERFILE)
        entry = re.search(r'(?m)^ENTRYPOINT \["([^"]+)"\]', dockerfile).group(1)
        self.assertEqual(re.search(r'test: \["CMD", "([^"]+)", "--health"\]', self.relay).group(1), entry)
        self.assertIn(entry, dockerfile.split("COPY --chmod=0755 docker/turn-entrypoint.sh")[1].split("\n")[0])

    def test_the_operators_steps_name_every_variable_that_the_relay_reads(self):
        doc = read(SERVER_DOC)
        steps = doc[doc.index("**What the operator sets up**"):]
        steps = steps[:steps.index("4. To check it")]
        names = set(re.findall(r"\b(ANTS_TURN_[A-Z_]+)\b", self.relay))
        self.assertTrue({"ANTS_TURN_SECRET", "ANTS_TURN_EXTERNAL_IP", "ANTS_TURN_HOST", "ANTS_TURN_PORT"} <= names)
        for name in sorted(names):
            self.assertIn(name, steps, name)
        self.assertIn("not proxied", steps)
        self.assertIn("ANTS_TURN_PORT=3480", steps, "the example for a machine whose port 3478 is taken")

    @unittest.skipUnless(shutil.which("docker"), "docker is needed")
    def test_compose_gives_the_variable_to_both_services_and_nothing_when_it_is_not_set(self):
        for value in ("3480", None):
            env = {k: v for k, v in os.environ.items() if not k.startswith("ANTS_")}
            env.update({"ANTS_TURN_HOST": "turn.example.org", "ANTS_TURN_SECRET": "a-made-up-secret", "ANTS_TURN_EXTERNAL_IP": "203.0.113.9"})
            if value:
                env["ANTS_TURN_PORT"] = value
            done = subprocess.run(["docker", "compose", "-f", STACK, "--profile", "relay", "config", "--format", "json"], capture_output=True, text=True, env=env)
            if done.returncode != 0:
                self.skipTest("docker compose is not available: " + done.stderr[:200])
            services = json.loads(done.stdout)["services"]
            self.assertEqual(services["ants-turn"]["environment"]["ANTS_TURN_PORT"], value or "")
            self.assertEqual(services["ants-server"]["environment"]["ANTS_TURN_PORT"], value or "")
            self.assertEqual(services["ants-server"]["environment"]["ANTS_TURN_HOST"], "turn.example.org", "the name carries no port")


if __name__ == "__main__":
    unittest.main()
