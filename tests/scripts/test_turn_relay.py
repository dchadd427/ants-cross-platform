#!/usr/bin/env python3
"""The relay of the players' channels (docs/SERVER.md "The relay for the players' channels"): its entry point, its configuration and its place in the stack.

What is checked:
  - the entry point: the port that coturn is told to listen on is ANTS_TURN_PORT (3478 when it is not set) and nothing else, the address that it listens on and sends from is ANTS_TURN_EXTERNAL_IP and
    nothing else (an address that is not an IPv4 address of the machine is refused), a port that is not one is refused, a start without the secret, the address or the name is refused, a name that
    carries a port is refused (the port is one number), and `--health` asks for a STUN binding on that same address and port; it is run for real, with the image's own entry point and the STUN client
    replaced by programs that write down what they were given, and the kernel's list of the machine's addresses by a file of its own;
  - the configuration: the relay binds one listening port and its relay range and no other port that coturn knows (an alternative listener, the TLS ports, the command-line interface, the web admin,
    the metrics), neither in docker/turnserver.conf nor on the command line of the stack, so that a second TURN server on the machine meets none of its ports; the range is the one that the documents
    tell the operator to open;
  - the one file that the relay writes (its process id file) is on a memory file system that the stack mounts, since the rest of the container is read-only;
  - the stack: the one variable ANTS_TURN_PORT reaches the relay and the game server (which tells the players) and the number 3478 is written nowhere in the stack file, the health check is the entry
    point's, and the operator's steps name every variable that the relay reads;
  - who the relay refuses: peers in every range that is not a player's public address are denied (probed with addresses of each range and of IPv4-mapped forms), no player's public address is, and neither
    is the relay's own address (two players' relay sessions are on it: denying it would end every relayed connection), and each credential holds what a four-player match needs and a retry, no more.
"""
import ipaddress
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


def all_values(text, name):
    """Every value of an option that may stand on several lines (denied-peer-ip), in order."""
    values = []
    for line in text.splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            key, _, value = line.partition("=")
            if key.strip() == name:
                values.append(value.strip())
    return values


def denied(address, values):
    """Is `address` in one of the ranges ("a-b") or single addresses of a denied-peer-ip list? An IPv4-mapped IPv6 address counts as its IPv4 address, as coturn reads it."""
    ip = ipaddress.ip_address(address)
    if ip.version == 6 and ip.ipv4_mapped is not None:
        ip = ip.ipv4_mapped
    for value in values:
        low, _, high = value.partition("-")
        low, high = ipaddress.ip_address(low), ipaddress.ip_address(high or low)
        if low.version == ip.version and low <= ip <= high:
            return True
    return False


# The ports that coturn would bind if an option were set; none of these options may be set (the relay binds its listening port, which comes from the entry point, and its relay range).
OTHER_PORT_OPTIONS = ("listening-port", "alt-listening-port", "tls-listening-port", "alt-tls-listening-port", "tcp-proxy-port", "dtls", "cli", "cli-port", "web-admin", "web-admin-port", "prometheus",
                      "prometheus-port", "prometheus-tls", "rfc5780")


def fib_trie(local, other=()):
    """The kernel's list of a machine's addresses (/proc/net/fib_trie) as `local` addresses are in it, and `other` addresses as a broadcast or a network address, which are not the machine's own."""
    lines = ["Local:", "  +-- 0.0.0.0/0 3 0 5"]
    for address in local:
        lines += ["     |-- " + address, "        /32 host LOCAL"]
    for address in other:
        lines += ["     |-- " + address, "        /32 link BROADCAST", "        /24 link UNICAST"]
    return "\n".join(lines) + "\n"


# the addresses of the test machine: the one of GOOD below, a loopback address and others that start or end like it; and a broadcast address that is not the machine's own
FIB_TRIE = fib_trie(["127.0.0.1", "203.0.113.9", "198.51.100.77", "203.0.113.200"], ["203.0.113.255", "203.0.113.99"])


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
        self.addresses = os.path.join(self.dir, "fib_trie")
        with open(self.addresses, "w", encoding="utf-8") as f:
            f.write(FIB_TRIE)
        text = read(ENTRY)
        self.script = os.path.join(self.dir, "turn-entrypoint.sh")
        replaced, count = re.subn(r"/usr/local/bin/docker-entrypoint\.sh", self.image_entry, text)
        assert count == 1, "the entry point hands over to the image's own entry point exactly once"
        replaced, count = re.subn(r"/proc/net/fib_trie", self.addresses, replaced)
        assert count == 1, "the entry point reads the machine's addresses from the kernel's list, once"
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
ADDRESS_ARGS = ["--listening-ip=" + GOOD["ANTS_TURN_EXTERNAL_IP"], "--relay-ip=" + GOOD["ANTS_TURN_EXTERNAL_IP"]]       # what the entry point adds for the address: where the relay listens and where its sessions send from


class TheEntryPoint(unittest.TestCase):
    def setUp(self):
        self.box = Sandbox()
        self.addCleanup(self.box.close)

    def test_coturn_listens_on_the_port_of_the_variable_and_3478_without_one(self):
        code_, _, called = self.box.run(GOOD, ARGS)
        self.assertEqual(code_, 0)
        self.assertEqual(called, ARGS + ["--listening-port=3478"] + ADDRESS_ARGS, "the arguments of the stack reach the image's entry point as they are (the `$` is the image's to expand), and the port and the address are added")
        for value, expected in (("3480", "3480"), ("1", "1"), ("65535", "65535"), ("", "3478")):
            code_, _, called = self.box.run(dict(GOOD, ANTS_TURN_PORT=value), ARGS)
            self.assertEqual(code_, 0, value)
            self.assertEqual(called, ARGS + ["--listening-port=" + expected] + ADDRESS_ARGS, value)

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

    def test_the_health_check_asks_for_a_stun_binding_on_the_same_address_and_port(self):
        address = {"ANTS_TURN_EXTERNAL_IP": GOOD["ANTS_TURN_EXTERNAL_IP"]}                  # (no secret, no name: the check needs none of them)
        code_, _, called = self.box.run(address, ["--health"])
        self.assertEqual((code_, called), (0, ["-p", "3478", GOOD["ANTS_TURN_EXTERNAL_IP"]]), "the relay listens on its address only, so that is where it is asked (not on the loopback address)")
        code_, _, called = self.box.run(dict(address, ANTS_TURN_PORT="3480"), ["--health"])
        self.assertEqual((code_, called), (0, ["-p", "3480", GOOD["ANTS_TURN_EXTERNAL_IP"]]))
        code_, _, called = self.box.run(dict(address, ANTS_TURN_PORT="3480", FAKE_STUN_EXIT="3"), ["--health"])
        self.assertEqual(code_, 3, "a relay that does not answer is unhealthy")
        code_, _, called = self.box.run(dict(address, ANTS_TURN_PORT="70000"), ["--health"])
        self.assertEqual((code_, called), (1, None))
        for bad in (None, "", "127.0.0.1", "203.0.113.99"):                                 # no address, the loopback address (the relay is not on it) and an address that is not the machine's own
            code_, _, called = self.box.run({} if bad is None else {"ANTS_TURN_EXTERNAL_IP": bad}, ["--health"])
            self.assertEqual((code_, called), (1, None), repr(bad) + ": a relay that cannot be on that address is unhealthy and nothing is asked")

    def test_the_relay_listens_and_sends_from_the_one_address_of_the_variable_and_nothing_else_is_accepted(self):
        for good in ("203.0.113.9", "198.51.100.77", "203.0.113.200"):
            code_, _, called = self.box.run(dict(GOOD, ANTS_TURN_EXTERNAL_IP=good), ARGS)
            self.assertEqual(code_, 0, good)
            self.assertEqual(called[-2:], ["--listening-ip=" + good, "--relay-ip=" + good], good)
        # not an IPv4 address, the address of any interface, the loopback address, an address with a leading zero, an address that only starts or ends like one of the machine's (203.0.113.9 and 98.51.100.77 are not 203.0.113.99 or
        # 198.51.100.7), a broadcast address, an address of another machine, and text that a shell would run
        for bad in ("203.0.113", "203.0.113.9.1", "203.0.113.256", "203.0.113.09", "0.0.0.0", "0.1.2.3", "::1", "2001:db8::9", "turn.example.org", "203.0.113.9 ", " 203.0.113.9", "203.0.113.", ".203.0.113.9",
                    "203.0.113..9", "203.0.113.9;touch pwned", "$(touch pwned)", "203.0.113.9\n127.0.0.1", "203.0.113.99", "203.0.113.255", "203.0.113.20", "98.51.100.77", "198.51.100.7", "03.0.113.200", "3.0.113.200",
                    "203.0.113.2", "127.0.0.1", "127.1.2.3", "-1.2.3.4", "1e3.0.0.1", "1.2.3.4/32"):
            code_, err, called = self.box.run(dict(GOOD, ANTS_TURN_EXTERNAL_IP=bad), ARGS)
            self.assertEqual(code_, 1, repr(bad))
            self.assertIsNone(called, "nothing was started for %r" % bad)
            self.assertIn("ANTS_TURN_EXTERNAL_IP", err)
        self.assertFalse(os.path.exists(os.path.join(os.getcwd(), "pwned")))


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

    def test_it_binds_no_address_of_its_own_the_entry_point_gives_the_one_address(self):
        for name in ("listening-ip", "relay-ip", "aux-server", "relay-device"):
            self.assertNotIn(name, self.opts, "%s would put the relay on an address that the entry point does not name (it adds the address of ANTS_TURN_EXTERNAL_IP)" % name)
        command = service(self.stack, "ants-turn")
        for name in ("listening-ip", "relay-ip", "aux-server", "relay-device"):
            self.assertNotRegex(command, r"--%s\b" % re.escape(name), "the stack's command line names no address: the entry point adds the one of the variable")

    def test_the_one_file_that_the_relay_writes_is_on_a_memory_file_system_of_the_stack(self):
        """Defect caught: a process id file in a place that the read-only container cannot write ends in a warning at every start (coturn goes on without it), a file system that is writable for everything
        would let the relay write anywhere. The stack says read-only and mounts a small memory file system on the folder of the file."""
        pidfile = self.opts["pidfile"]
        folder = os.path.dirname(pidfile)
        relay = service(self.stack, "ants-turn")
        self.assertIn("read_only: true", relay)
        mounts = re.findall(r'tmpfs: \[([^\]]*)\]', relay)
        self.assertEqual(len(mounts), 1)
        paths = [m.split(":")[0] for m in re.findall(r'"([^"]*)"', mounts[0])]
        self.assertEqual(paths, [folder], "the stack mounts a memory file system on the folder of the process id file, and nowhere else")
        self.assertIn("size=", mounts[0], "a small one")

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

    def test_the_relay_refuses_every_peer_that_is_not_a_players_public_address(self):
        """Defect caught: a range dropped from or cut short in `denied-peer-ip` would make the relay carry packets to the machine's private networks, the cloud's metadata address or multicast. No run of this
        suite can show that (it needs the TURN server itself, probed by hand: error 403 on the permission), so the list is read as ranges and probed with addresses of each."""
        values = all_values(self.conf, "denied-peer-ip")
        for probe in ("0.0.0.5", "0.255.255.255", "10.0.0.1", "10.255.255.254", "100.64.0.1", "100.127.255.254", "127.0.0.1", "127.255.255.254", "169.254.169.254", "172.16.0.1", "172.31.255.254",
                      "192.0.0.5", "192.168.0.1", "192.168.255.254", "198.18.0.1", "198.19.255.254", "224.0.0.1", "239.1.1.1", "255.255.255.255", "::1", "fc00::1", "fd12:3456::1", "fe80::1",
                      "::ffff:10.1.2.3", "::ffff:127.0.0.1", "::ffff:169.254.169.254", "::ffff:192.168.1.5"):
            self.assertTrue(denied(probe, values), "%s is not a player's public address and the relay must refuse it as a peer" % probe)
        for public in ("198.51.100.7", "203.0.113.9", "11.1.2.3", "45.67.89.10", "100.63.255.255", "100.128.0.1", "172.15.255.255", "172.32.0.1", "192.0.1.1", "198.17.255.255", "198.20.0.1"):
            self.assertFalse(denied(public, values), "%s may be a player's address (or its neighbor): the relay must not refuse it" % public)

    def test_each_credential_holds_what_a_four_player_match_needs_and_no_more(self):
        """Defect caught: a per-credential quota that is raised or removed would let one credential take a large share of the relay's sessions, and one that is cut would leave a player of a four-player match
        without a relay for some of its connections. No run of this suite can show it (the TURN server itself counts the sessions), so the number is read. A browser asks for two sessions (UDP and TCP) for
        each connection, a four-player match has three connections for each player, and a connection that is tried again was closed first."""
        quota = int(self.opts["user-quota"])
        self.assertEqual(quota, 3 * 2)
        doc = read(SERVER_DOC)
        self.assertIn("each credential may hold %d sessions" % quota, doc, "the operator's document names the quota of the configuration")

    def test_the_relays_own_address_is_not_refused_as_a_peer(self):
        """Defect caught: a relay that refuses its own public address as a peer ends every relayed connection, because the relay sessions of both players of a pair are on that address and each player's
        session sends to the other's (probed with turnutils_uclient -y against the TURN server itself: error 403 on the channel bind). No run of this suite can show it, so the denied addresses, those of the
        configuration and those that the entry point adds on the command line, are read."""
        box = Sandbox()
        self.addCleanup(box.close)
        code_, _, called = box.run(GOOD, ARGS)
        self.assertEqual(code_, 0)
        values = all_values(self.conf, "denied-peer-ip") + [a.partition("=")[2] for a in called if a.startswith("--denied-peer-ip=")]
        self.assertFalse(denied(GOOD["ANTS_TURN_EXTERNAL_IP"], values))


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
            self.assertEqual([m.split(":")[0] for m in services["ants-turn"]["tmpfs"]], ["/tmp"], "the memory file system of the process id file")


if __name__ == "__main__":
    unittest.main()
