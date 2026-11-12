#!/usr/bin/env python3
"""The list of reverse proxies that the web image ships (docker/trusted-proxies.conf), and the way docker/nginx.conf and the stack files use it (run by ./run_tests.sh --fast and by the CI; docs/SERVER.md "Behind a reverse proxy").

The list tells nginx which addresses are reverse proxies: the visitor is the right-most entry of X-Forwarded-For that is not a proxy itself, and the limits for one visitor (the zones of docker/nginx.conf,
which are keyed on the address, and the X-Client-Address that the chat hub and the game server count by) count that address. What is tested here:

  - the text of the shipped list: the six private and internal ranges and nothing else, X-Forwarded-For as the header, recursive; read with the ipaddress module against private, boundary and public
    addresses (no range of the Internet, nothing that matches everything). On every machine;
  - the wiring: the list is included once in the http block (the default of the site) and the operator's own folder once in the server block (a list there replaces the default); every address that a door
    is told or that a zone counts comes from $remote_addr and never from a header; the image has the list; the compose files mount ANTS_TRUSTED_PROXIES_FILE as a FULL path whose default is
    /dev/null (a path of the repository does not work in a stack that a deployment tool builds from a repository: its files are not on the host), into the folder that nginx includes;
  - where the program nginx is installed (a Linux machine has all of 127.0.0.0/8): the REAL file and the REAL docker/nginx.conf in front of stand-in doors and a real chat hub. Connections from different
    internal addresses, each with other X-Forwarded-For headers, are different visitors for the zones and for the X-Client-Address; a value that a visitor wrote himself, or in a header of another name, is
    skipped; a connection from an address outside the list keeps its own whatever it sends; the operator's file replaces the list; with an empty list (a file of comments) the address of the
    connection counts. The ranges of IPv6 are read through the header, by nginx's own matching;
  - opt-in (ANTS_TEST_ADDRESS_ALIASES=1; Linux, root, `ip`): the same through connections that really come from 10.x, 172.16-31.x, 192.168.x and public addresses (aliases on the loopback device, removed after);
  - the web port of the stack files: ANTS_WEB_BIND_PREFIX stands in front of it (empty by default, and then `docker compose config` renders exactly what the port line rendered before; `127.0.0.1:` publishes
    on the loopback address only), no other port line has it; the documents and the comments state what the list does and what the operator may set (who may reach the web container, a path that names an
    existing FILE) and nothing else.

The broken copies of tests/data/trusted_proxies_mutants.json (run them with tools/mutate.py) must each fail one of these tests.
"""
import http.client
import ipaddress
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
from hub import limits as limitmod  # noqa: E402
import test_nginx_submissions as sub  # noqa: E402

read = sub.maps.read
strip_comments = sub.maps.strip_comments
LIST = read("docker", "trusted-proxies.conf")
CONF = sub.CONF
DIRECTIVES = [l.strip() for l in strip_comments(LIST).splitlines() if l.strip()]
RANGES = [re.fullmatch(r"set_real_ip_from (\S+);", l).group(1) for l in DIRECTIVES if l.startswith("set_real_ip_from ")]
COMPOSE_FILES = ("docker-compose.stack.yml", "docker-compose.yml")
MOUNT = "${ANTS_TRUSTED_PROXIES_FILE:-/dev/null}:/etc/nginx/ants-trusted-proxies.d/own.conf:ro"

TRUSTED = ["127.0.0.1", "127.0.0.2", "127.255.255.255", "10.0.0.0", "10.1.2.3", "10.255.255.255", "172.16.0.0", "172.17.0.1", "172.18.0.5", "172.31.255.255", "192.168.0.0", "192.168.1.1", "192.168.255.255",
           "::1", "fc00::", "fd00::7", "fdff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"]
OUTSIDE = ["0.0.0.0", "1.1.1.1", "8.8.8.8", "9.255.255.255", "11.0.0.0", "100.64.0.1", "126.255.255.255", "128.0.0.0", "169.254.1.1", "172.15.255.255", "172.32.0.0", "192.0.2.1", "192.167.255.255",
           "192.169.0.0", "198.51.100.7", "203.0.113.9", "224.0.0.1", "255.255.255.255", "::", "::2", "2001:db8::1", "2606:4700::1111", "2a00:1450::5", "fbff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", "fe00::",
           "fe80::1", "ff02::1"]
PRIVATE = ("127.0.0.0/8", "10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "::1/128", "fc00::/7")


WEB_PORTS = (("docker-compose.stack.yml", "ANTS_PORT", "19980"), ("docker-compose.yml", "ANTS_PORT", "19980"))


def server_section():
    """The section "Behind a reverse proxy" of docs/SERVER.md (the last of the page), empty when it is gone."""
    text = read("docs", "SERVER.md")
    return text[text.index("### Behind a reverse proxy"):] if "### Behind a reverse proxy" in text else ""


def comment_words(text):
    """The comment lines of a file as one run of words (a comment broken over lines reads as one)."""
    return " ".join(re.sub(r"(?m)^\s*#\s?", "", "\n".join(l for l in text.splitlines() if l.lstrip().startswith("#"))).split())


def comment_above(text, service_name, key):
    """The comment lines right above `key:` in a service, as words."""
    lines = service_lines(text, service_name)
    at = next(i for i, l in enumerate(lines) if l == "    %s:" % key)
    block = []
    for l in reversed(lines[:at]):
        if not l.lstrip().startswith("#"):
            break
        block.append(l)
    return comment_words("\n".join(reversed(block)))


def header_entries(text, names):
    """The entries of a stack file's header (the comment before `services:`) that name one of the variables, with their continuation lines, as words."""
    lines = text.split("\nservices:")[0].splitlines()
    found = []
    for i, line in enumerate(lines):
        if any(name in line for name in names) and re.match(r"#   [A-Z]", line):
            found.append(line)
            for more in lines[i + 1:]:
                if not re.match(r"#\s{30,}\S", more):
                    break
                found.append(more)
    return comment_words("\n".join(found))


def what_this_change_wrote(name):
    """The comments of a compose file that belong to ANTS_TRUSTED_PROXIES_FILE and ANTS_WEB_BIND_PREFIX, as words."""
    text = read(name)
    return " ".join([comment_above(text, "ants-beta", "volumes"), comment_above(text, "ants-beta", "ports"), header_entries(text, ("ANTS_TRUSTED_PROXIES_FILE", "ANTS_WEB_BIND_PREFIX"))])


def render(path, env=None, project=None):
    """`docker compose config` as JSON for a compose file, without ANTS_TRUSTED_PROXIES_FILE and ANTS_WEB_BIND_PREFIX in the environment unless `env` gives them; a skip where compose is not there."""
    full = {k: v for k, v in os.environ.items() if k not in ("ANTS_TRUSTED_PROXIES_FILE", "ANTS_WEB_BIND_PREFIX")}
    full.update(env or {})
    command = ["docker", "compose", "-f", path] + (["--project-directory", project] if project else []) + ["config", "--format", "json"]
    done = subprocess.run(command, capture_output=True, text=True, env=full)
    if done.returncode != 0:
        raise unittest.SkipTest("docker compose is not available: " + done.stderr[:200])
    return json.loads(done.stdout)


def in_list(address, ranges=None):
    """Whether an address is inside one of the ranges (the ipaddress module's reading of them)."""
    ip = ipaddress.ip_address(address)
    return any(ip in ipaddress.ip_network(r) for r in (RANGES if ranges is None else ranges))


def service_lines(text, name):
    """The lines of one service of a compose file, comments among them."""
    lines = text.splitlines()
    start = next(i for i, l in enumerate(lines) if l == "  %s:" % name)
    end = next((i for i in range(start + 1, len(lines)) if re.match(r"^  \S|^\S", lines[i])), len(lines))
    return lines[start:end]


def service(text, name):
    """The lines of one service of a compose file, comments dropped."""
    return [l for l in service_lines(text, name) if not l.lstrip().startswith("#")]


def map_values(name):
    """The values of the nginx `map` that makes the variable $name (what a key can be: the address, or nothing)."""
    header = re.search(r"map\s+\S+\s+\$%s\s*\{" % name, CONF).group(0)
    return set(re.findall(r"(\S+);", sub.block_of(CONF, header)))


# ------------------------------------------------------------------------------------------------------------------------------------------------------------ the text, on every machine
class TheShippedList(unittest.TestCase):
    def test_it_is_the_private_ranges_the_header_of_the_proxy_and_recursion_and_nothing_else(self):
        self.assertEqual(DIRECTIVES, ["set_real_ip_from 127.0.0.0/8;", "set_real_ip_from 10.0.0.0/8;", "set_real_ip_from 172.16.0.0/12;", "set_real_ip_from 192.168.0.0/16;", "set_real_ip_from ::1;",
                                      "set_real_ip_from fc00::/7;", "real_ip_header X-Forwarded-For;", "real_ip_recursive on;"])

    def test_the_private_and_internal_addresses_are_in_it_and_no_address_of_the_internet(self):
        for address in TRUSTED:
            self.assertTrue(in_list(address), address)
        for address in OUTSIDE:
            self.assertFalse(in_list(address), address)

    def test_nothing_matches_everything_and_no_range_is_wider_than_the_private_one_it_names(self):
        for word in ("0.0.0.0/0", "::/0", " all;", "unix:"):
            self.assertNotIn(word, "\n".join(DIRECTIVES), word)
        for r in RANGES:
            net = ipaddress.ip_network(r)
            self.assertTrue(any(net.version == ipaddress.ip_network(p).version and net.subnet_of(ipaddress.ip_network(p)) for p in PRIVATE), r)

    def test_the_header_is_the_one_that_a_proxy_writes_and_the_chain_is_read_from_the_right(self):
        self.assertEqual([l for l in DIRECTIVES if l.startswith("real_ip_header")], ["real_ip_header X-Forwarded-For;"])
        self.assertEqual([l for l in DIRECTIVES if l.startswith("real_ip_recursive")], ["real_ip_recursive on;"])

    def test_the_comment_states_what_the_list_assumes_as_requirements(self):
        comment = comment_words(LIST)
        for words in ("X-Forwarded-For itself", "Only the proxy can reach this container from an address in the list", "so that only the proxy can connect to it (ANTS_WEB_BIND_PREFIX in the stack files)",
                      "keep no container that you do not trust on the network of the proxy", "PUBLIC address", "ANTS_TRUSTED_PROXIES_FILE in the stack files: the full path of an existing file", "replaces this list"):
            self.assertIn(words, comment, words)
        for word in ("gateway", "above all", "IPv6 above", "owner", "userland"):
            self.assertNotIn(word, comment, word)
        self.assertTrue(all(ord(c) < 128 for c in LIST))


class TheWiring(unittest.TestCase):
    def test_the_list_is_the_default_of_the_site_and_the_operators_folder_replaces_it(self):
        head, server = CONF[:CONF.index("server {")], CONF[CONF.index("server {"):]
        self.assertEqual(head.count("include /etc/nginx/ants-trusted-proxies/*.conf;"), 1)
        self.assertNotIn("ants-trusted-proxies.d", head)
        self.assertNotIn("ants-trusted-proxies/", server)
        self.assertEqual(server.count("include /etc/nginx/ants-trusted-proxies.d/*.conf;"), 1)
        self.assertLess(server.index("include /etc/nginx/ants-trusted-proxies.d/*.conf;"), server.index("location "))
        for directive in ("set_real_ip_from", "real_ip_header", "real_ip_recursive"):
            self.assertNotIn(directive, CONF, directive + ": the list is in its file and nowhere else")

    def test_the_image_has_the_list(self):
        with open(os.path.join(REPO, "Dockerfile"), encoding="utf-8") as f:
            self.assertIn("COPY docker/trusted-proxies.conf /etc/nginx/ants-trusted-proxies/proxies.conf", f.read())

    def test_a_zone_counts_and_a_door_is_told_the_address_nginx_settled_on_and_never_a_header(self):
        told = re.findall(r"proxy_set_header\s+X-Client-Address\s+(\S+);", CONF)
        self.assertGreaterEqual(len(told), 10)
        self.assertEqual(set(told), {"$remote_addr"})
        self.assertEqual(len(re.findall(r"X-Client-Address", CONF)), len(told))                 # (no other use of the name: nothing reads a header that a visitor wrote)
        keys = set(re.findall(r"limit_(?:req|conn)_zone\s+(\S+)\s+zone=", CONF))
        self.assertEqual(keys - {"$binary_remote_addr"}, {"$history_plain_key", "$history_query_key", "$submit_post_key", "$admin_login_key", "$test_map_post_key"})
        for key in keys - {"$binary_remote_addr"}:
            self.assertLessEqual(map_values(key[1:]), {"$binary_remote_addr", '""'}, key)         # (a key is the address, or nothing)

    def test_the_compose_files_mount_the_operators_list_by_a_full_path_with_nothing_as_the_default(self):
        folder = re.search(r"include (/etc/nginx/ants-trusted-proxies\.d)/\*\.conf;", CONF).group(1)
        for name in COMPOSE_FILES:
            with open(os.path.join(REPO, name), encoding="utf-8") as f:
                text = f.read()
            mounts = [l.strip() for l in service(text, "ants-beta") if "ANTS_TRUSTED_PROXIES_FILE" in l]
            self.assertEqual(mounts, ["- " + MOUNT], name)
            source, target, mode = re.fullmatch(r"- (\S+?\}):(/\S+):(\w+)", mounts[0]).groups()
            default = re.fullmatch(r"\$\{ANTS_TRUSTED_PROXIES_FILE:-([^}]*)\}", source).group(1)
            self.assertTrue(default.startswith("/"), "%s: the default is a full path (a path of the repository, or a name, is no file on the host of a stack that a deployment tool builds from a repository): %s" % (name, default))
            self.assertEqual(os.path.dirname(target), folder, name)                             # (the folder that nginx includes in the server block: in the image's own folder /dev/null would blank the list)
            self.assertEqual(mode, "ro")
            self.assertIn("    volumes:", service(text, "ants-beta"), name)

    @unittest.skipUnless(shutil.which("docker"), "docker is needed")
    def test_compose_renders_the_mount_with_the_default_and_with_a_path_of_the_operators(self):
        for name in COMPOSE_FILES:
            for given, source in ((None, "/dev/null"), ("/srv/ants/proxies.conf", "/srv/ants/proxies.conf")):
                env = {k: v for k, v in os.environ.items() if k != "ANTS_TRUSTED_PROXIES_FILE"}
                if given:
                    env["ANTS_TRUSTED_PROXIES_FILE"] = given
                done = subprocess.run(["docker", "compose", "-f", os.path.join(REPO, name), "config", "--format", "json"], capture_output=True, text=True, env=env)
                if done.returncode != 0:
                    self.skipTest("docker compose is not available: " + done.stderr[:200])
                mounts = json.loads(done.stdout)["services"]["ants-beta"]["volumes"]
                self.assertEqual([(m["type"], m["source"], m["target"], m.get("read_only")) for m in mounts], [("bind", source, "/etc/nginx/ants-trusted-proxies.d/own.conf", True)], name)

    def test_the_documents_and_the_comments_do_not_say_that_the_list_is_empty_or_has_to_be_mounted(self):
        for path in (("docker", "nginx.conf"), ("Dockerfile",), ("docker-compose.stack.yml",), ("docs", "HUB.md"), ("docs", "SERVER.md"), ("docs", "MAP_SUBMISSIONS.md")):
            text = read(*path)
            for old in ("image's empty file", "image's one file is all comments", "the image's file is all comments", "mount a file over /etc/nginx/ants-trusted-proxies/proxies.conf",
                        "none: the address of the connection counts"):
                self.assertNotIn(old, text, "/".join(path))
        for name in ("HUB.md", "MAP_SUBMISSIONS.md"):
            self.assertIn("SERVER.md#behind-a-reverse-proxy", read("docs", name), name)           # (the one home of the list, its assumptions and the override)

    def test_server_md_states_the_requirements_and_what_the_operator_may_set(self):
        for words in ("`ANTS_TRUSTED_PROXIES_FILE` | none |", "`ANTS_WEB_BIND_PREFIX` | empty |"):                 # (the table of the variables)
            self.assertIn(words, read("docs", "SERVER.md"), words)
        section = server_section()
        for words in ("### Behind a reverse proxy", "**full path of the copy on the host**", "it must name an existing FILE", "nginx stops with \"Is a directory\"", "no use in a stack that a deployment tool builds from a repository",
                      "**only for a connection from a private or internal address**", "**Only the reverse proxy can reach the web container from an address in the list.**",
                      "Publish the web port so that only the proxy can connect to it", "`127.0.0.0/8`, `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`, `::1`, `fc00::/7`",
                      "an address of the machine and a colon, such as `127.0.0.1:`", "would no longer reach it", "once over IPv4 and once over IPv6 if the machine has both"):
            self.assertIn(words, section, words)

    def test_the_documents_and_comments_state_only_what_the_list_does(self):
        texts = {"docs/SERVER.md, the section": server_section(), "docker/trusted-proxies.conf": comment_words(LIST)}
        for name in COMPOSE_FILES:
            texts[name] = what_this_change_wrote(name)
            self.assertIn("ANTS_TRUSTED_PROXIES_FILE", texts[name], name)
        for name, text in texts.items():
            for hint in ("one limit for the whole site", "the whole site's", "count the proxy", "counts as that proxy", "counts as the proxy", "has the proxy's address", "choose his address", "name any address",
                         "can use up", "gateway", "above all", "userland", "owner", "locks everybody"):
                self.assertNotIn(hint, text, "%s: %s" % (name, hint))
        conf = read("docker", "nginx.conf")                                     # (the whole file, its comments too)
        for phrase in ("proxy's address", "whole site", "whole-site", "sized to hold", "sized for", "every visitor", "may have the", "not small", "shares these", "ONE allowance", "can use up"):
            self.assertNotIn(phrase, conf, phrase)
        for name in ("MAP_SUBMISSIONS.md", "HUB.md"):
            for line in read("docs", name).splitlines():
                if "trusted-proxies" in line:
                    self.assertNotIn("owner", line, name)

    def test_the_compose_files_say_that_the_path_must_name_an_existing_file(self):
        for name in COMPOSE_FILES:
            text = read(name)
            above = comment_above(text, "ants-beta", "volumes")
            for words in ("ANTS_TRUSTED_PROXIES_FILE", "must name an existing FILE", "Docker makes a directory there", "nginx stops with \"Is a directory\""):
                self.assertIn(words, above, "%s: %s" % (name, words))
        header = comment_words(read("docker-compose.stack.yml").split("\nservices:")[0])
        for words in ("Give the FULL path of a file on the host", "The path must name an existing FILE", "nginx stops with \"Is a directory\"", "ANTS_WEB_BIND_PREFIX=127.0.0.1:"):
            self.assertIn(words, header, words)


class ThePublishedWebPort(unittest.TestCase):
    """ANTS_WEB_BIND_PREFIX stands in front of the host port of the web page: empty (the default) the port is published as it always was, `127.0.0.1:` publishes it on the loopback address only."""

    def test_the_prefix_is_in_front_of_the_web_ports_own_variable_with_nothing_as_its_default_and_in_no_other_line(self):
        for name, variable, default in WEB_PORTS:
            text = read(name)
            ports = [l.strip() for l in service(text, "ants-beta") if l.strip().startswith('- "')]
            self.assertEqual(ports, ['- "${ANTS_WEB_BIND_PREFIX:-}${%s:-%s}:80"' % (variable, default)], name)        # (not "0.0.0.0:" as the default: that publishes on IPv4 addresses only)
            code = "\n".join(l for l in text.splitlines() if not l.lstrip().startswith("#"))
            self.assertEqual(code.count("ANTS_WEB_BIND_PREFIX"), 1, name)
            self.assertNotIn("0.0.0.0", code, name)

    @unittest.skipUnless(shutil.which("docker"), "docker is needed")
    def test_compose_renders_the_old_port_when_the_prefix_is_empty_and_the_address_when_it_is_set(self):
        for name, variable, default in WEB_PORTS:
            text = read(name)
            before = text.replace('"${ANTS_WEB_BIND_PREFIX:-}${%s:-%s}:80"' % (variable, default), '"${%s:-%s}:80"' % (variable, default))
            self.assertNotEqual(before, text, name)
            with tempfile.TemporaryDirectory() as tmp:
                old_file = os.path.join(tmp, "compose.yml")
                with open(old_file, "w", encoding="utf-8") as f:
                    f.write(before)
                old = render(old_file, project=REPO)["services"]["ants-beta"]["ports"]
            unset = render(os.path.join(REPO, name))["services"]["ants-beta"]["ports"]
            self.assertEqual(unset, old, name)                                                  # (the file that has the prefix, with none given, is the file that had not: byte for byte what compose makes of it)
            self.assertNotIn("host_ip", unset[0])
            loopback = render(os.path.join(REPO, name), {"ANTS_WEB_BIND_PREFIX": "127.0.0.1:"})["services"]["ants-beta"]["ports"]
            self.assertEqual(loopback, [dict(old[0], host_ip="127.0.0.1")], name)
            moved = render(os.path.join(REPO, name), {"ANTS_WEB_BIND_PREFIX": "172.17.0.1:", variable: "20000"})["services"]["ants-beta"]["ports"]
            self.assertEqual((moved[0]["host_ip"], str(moved[0]["published"]), moved[0]["target"]), ("172.17.0.1", "20000", 80), name)


# ------------------------------------------------------------------------------------------------------------------------------------------------------------ nginx, run
def peer_available(address):
    try:
        with socket.socket() as s:
            s.bind((address, 0))
        return True
    except OSError:
        return False


def get_from(rig, source, lines=(), extra=None, path="/admin/api/me"):
    """The address that the stand-in door was told (its X-Client-Address) for one GET from the address `source`, with these lines of X-Forwarded-For (a header may come more than once) and these other headers."""
    with rig.door.lock:
        before = len(rig.door.seen)
    conn = http.client.HTTPConnection("127.0.0.1", rig.port, timeout=20, source_address=(source, 0))
    try:
        conn.putrequest("GET", path)
        for line in lines:
            conn.putheader("X-Forwarded-For", line)
        for name, value in (extra or {}).items():
            conn.putheader(name, value)
        conn.endheaders()
        conn.getresponse().read()
    finally:
        conn.close()
    return sub.seen_after(rig.door, before)[2].get("x-client-address")


def without_peer(text, address):
    """The shipped list without the ranges that hold `address` (the test's way to make a connection from that address one from outside the list)."""
    keep = [l for l in text.splitlines() if not (l.startswith("set_real_ip_from ") and ipaddress.ip_address(address) in ipaddress.ip_network(l.split()[1].rstrip(";")))]
    assert len(keep) < len(text.splitlines())
    return "\n".join(keep) + "\n"


def needs_loopback_addresses():
    if not (peer_available("127.0.0.2") and peer_available("127.0.0.77")):
        raise unittest.SkipTest("this machine has no loopback addresses besides 127.0.0.1 (a Linux machine has all of 127.0.0.0/8)")


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the shipped list was NOT run in front of docker/nginx.conf (tests/scripts/test_nginx_trusted_proxies.py)")
class TheListRuns(unittest.TestCase):
    """The real file in the real docker/nginx.conf: the doors are told the address that nginx settled on."""

    @classmethod
    def setUpClass(cls):
        needs_loopback_addresses()
        cls.rig = sub.Rig(trusted=LIST)

    @classmethod
    def tearDownClass(cls):
        cls.rig.stop()

    def test_connections_from_different_internal_addresses_are_told_the_visitors_they_name(self):
        for peer in ("127.0.0.1", "127.0.0.2", "127.0.0.77"):
            for visitor in ("198.51.100.5", "203.0.113.9", "2001:db8:1234::5"):
                self.assertEqual(get_from(self.rig, peer, [visitor]), visitor, (peer, visitor))
            self.assertEqual(get_from(self.rig, peer), peer, "no X-Forwarded-For: the address of the connection")

    def test_a_value_that_the_visitor_wrote_himself_is_skipped_and_so_are_the_proxies_after_it(self):
        for lines, expected in ((["6.6.6.6, 198.51.100.5"], "198.51.100.5"), (["1.1.1.1, 6.6.6.6, 198.51.100.5"], "198.51.100.5"), (["6.6.6.6", "198.51.100.5"], "198.51.100.5"),
                                (["198.51.100.5, 10.0.0.2, 172.20.0.3"], "198.51.100.5"), (["198.51.100.5, 10.0.0.2", "172.20.0.3"], "198.51.100.5"), (["198.51.100.5", "10.9.9.9"], "198.51.100.5"),
                                (["6.6.6.6, 198.51.100.5:51000"], "198.51.100.5"), (["6.6.6.6, 2001:db8::5"], "2001:db8::5"), (["6.6.6.6, [2001:db8::5]:4000"], "2001:db8::5")):
            self.assertEqual(get_from(self.rig, "127.0.0.2", lines), expected, lines)

    def test_an_entry_in_the_list_is_skipped_and_the_entry_to_its_left_is_taken(self):
        """The rule is the same for every entry of the header: one that is in the list is a proxy, whoever wrote it, and the visitor is the next one to its left (docs/SERVER.md, the first assumption)."""
        self.assertEqual(get_from(self.rig, "127.0.0.2", ["6.6.6.6, 192.168.1.50"]), "6.6.6.6")
        self.assertEqual(get_from(self.rig, "127.0.0.2", ["192.168.1.50"]), "192.168.1.50", "and the only entry is the visitor, whatever it is")

    def test_the_text_that_nginx_gives_is_the_address_that_the_hub_folds(self):
        """The hub counts an IPv6 visitor by his /64 and an IPv4-mapped one as the IPv4 address (hub/limits.py); the game server does the same (src/ants_server/upload_common.cpp): nginx hands both the text of the address."""
        for visitor, key in (("2001:db8:1234:5678:9abc:def0:1234:5678", "2001:db8:1234:5678::/64"), ("2001:db8:1234:5678::1", "2001:db8:1234:5678::/64"), ("::ffff:198.51.100.5", "198.51.100.5"), ("198.51.100.5", "198.51.100.5")):
            told = get_from(self.rig, "127.0.0.2", [visitor])
            self.assertEqual(limitmod.address_key(told, "127.0.0.1"), key, (visitor, told))
        self.assertNotEqual(limitmod.address_key(get_from(self.rig, "127.0.0.2", ["198.51.100.5"]), "127.0.0.1"), limitmod.address_key(get_from(self.rig, "127.0.0.2", ["198.51.100.6"]), "127.0.0.1"))

    def test_a_header_of_another_name_is_no_address_at_all(self):
        extra = {"X-Client-Address": "9.9.9.9", "X-Real-IP": "8.8.8.8", "Forwarded": "for=7.7.7.7", "X-Forwarded-Host": "evil.example", "Client-IP": "6.6.6.6", "CF-Connecting-IP": "5.5.5.5", "True-Client-IP": "4.4.4.4"}
        self.assertEqual(get_from(self.rig, "127.0.0.2", ["198.51.100.5"], extra), "198.51.100.5")
        self.assertEqual(get_from(self.rig, "127.0.0.2", [], extra), "127.0.0.2")
        self.assertEqual(get_from(self.rig, "127.0.0.2", [], {"X_Forwarded_For": "9.9.9.9"}), "127.0.0.2", "a name with underscores is dropped by nginx")

    def test_what_is_no_address_leaves_the_address_of_the_connection(self):
        for lines in (["garbage"], ["unknown"], [""], ["   "], [","], ["198.51.100.5, garbage"], ["6.6.6.6", "garbage"], ["1.2.3.4.5"], ["198.51.100.5:99999"]):
            self.assertEqual(get_from(self.rig, "127.0.0.2", lines), "127.0.0.2", lines)

    def test_the_ranges_of_ipv6_are_read_by_nginx_as_the_file_says(self):
        """The visitor is 2001:db8::5 and the last entry of the header is the 'proxy' under test: a proxy in the list is skipped, so the visitor shows; one outside the list is the visitor."""
        for proxy in ("::1", "fc00::", "fd00::7", "fdff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", "[fd00::7]:5000", "::ffff:10.0.0.1", "10.1.2.3", "172.31.255.255", "192.168.0.1", "127.9.9.9"):
            self.assertEqual(get_from(self.rig, "127.0.0.2", ["2001:db8::5, " + proxy]), "2001:db8::5", proxy)
        for proxy in ("::2", "fe00::", "fbff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", "fe80::1", "2a00:1450::5", "::ffff:198.51.100.9", "172.15.255.255", "172.32.0.0", "192.169.0.1", "11.0.0.1", "100.64.0.1"):
            self.assertEqual(get_from(self.rig, "127.0.0.2", ["2001:db8::5, " + proxy]), proxy, proxy)

    def test_a_connection_from_outside_the_list_keeps_its_own_address_whatever_it_sends(self):
        rig = sub.Rig(trusted=without_peer(LIST, "127.0.0.1"))                   # (127.0.0.1 is now an address of the Internet: the list is the shipped one without the ranges that hold it)
        self.addCleanup(rig.stop)
        self.assertEqual(get_from(self.rig, "127.0.0.1", ["198.51.100.5"]), "198.51.100.5", "the shipped list believes it")
        for lines in (["198.51.100.5"], ["6.6.6.6, 198.51.100.5"], ["6.6.6.6", "198.51.100.5"], ["10.1.2.3"]):
            self.assertEqual(get_from(rig, "127.0.0.1", lines, {"X-Client-Address": "9.9.9.9", "X-Real-IP": "8.8.8.8"}), "127.0.0.1", lines)

    def test_an_empty_list_leaves_the_address_of_the_connection(self):
        rig = sub.Rig(trusted="")                                                  # (an empty list: a file of comments)
        self.addCleanup(rig.stop)
        for visitor in ("198.51.100.5", "203.0.113.9", "2001:db8:1234::5"):
            self.assertEqual(get_from(rig, "127.0.0.2", [visitor]), "127.0.0.2", visitor)

    def test_the_limits_count_each_visitor_apart(self):
        for trusted, expected in ((LIST, 201), ("", 503)):                          # (an empty list: the two visitors are one address)
            rig = sub.Rig(trusted=trusted, relaxed=False)
            self.addCleanup(rig.stop)
            flood = [rig.ask("POST", "/admin/api/login", {"X-Forwarded-For": "198.51.100.10", "Content-Type": "application/json"}, b"{}")[0] for _ in range(9)]
            self.assertEqual(flood, [201] * 6 + [503] * 3, "six a minute for one visitor (a burst of five more)")
            self.assertEqual(rig.ask("POST", "/admin/api/login", {"X-Forwarded-For": "198.51.100.11", "Content-Type": "application/json"}, b"{}")[0], expected, "another visitor, right after")
            sends = [rig.ask("POST", "/submissions?kind=classic&name=Abc&maker=Mia", {"X-Forwarded-For": "198.51.100.12", "Content-Type": "application/octet-stream"}, b"m" * 100)[0] for _ in range(8)]
            self.assertEqual(sends, [201] * 4 + [503] * 4)
            self.assertEqual(rig.ask("POST", "/submissions?kind=classic&name=Abc&maker=Mia", {"X-Forwarded-For": "198.51.100.13", "Content-Type": "application/octet-stream"}, b"m" * 100)[0], expected)


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the operator's list was NOT run in front of docker/nginx.conf (tests/scripts/test_nginx_trusted_proxies.py)")
class TheOperatorsFileRuns(unittest.TestCase):
    """The file that the stack files mount (ANTS_TRUSTED_PROXIES_FILE) replaces the list for the site, and an empty one changes nothing."""

    @classmethod
    def setUpClass(cls):
        needs_loopback_addresses()

    def rig(self, own):
        rig = sub.Rig(trusted=LIST, own=own)
        self.addCleanup(rig.stop)
        return rig

    def test_the_default_mount_changes_nothing(self):
        for own in (sub.DEVNULL, "", "# nothing of the operator's\n"):
            rig = self.rig(own)
            self.assertEqual(get_from(rig, "127.0.0.2", ["6.6.6.6, 198.51.100.5"]), "198.51.100.5", repr(own))
            self.assertEqual(get_from(rig, "127.0.0.2", []), "127.0.0.2")

    def test_the_operators_list_replaces_the_shipped_one(self):
        rig = self.rig("set_real_ip_from 127.0.0.2;\n")
        self.assertEqual(get_from(rig, "127.0.0.2", ["198.51.100.5"]), "198.51.100.5")
        self.assertEqual(get_from(rig, "127.0.0.1", ["198.51.100.5"]), "127.0.0.1", "127.0.0.1 is in the shipped list but not in the operator's")
        self.assertEqual(get_from(rig, "127.0.0.77", ["198.51.100.5"]), "127.0.0.77")
        self.assertEqual(get_from(rig, "127.0.0.2", ["198.51.100.5, 127.0.0.2"]), "198.51.100.5", "the header and the recursion are the shipped ones")

    def test_the_operators_file_may_name_the_header_and_the_recursion_too_and_may_leave_the_ranges_alone(self):
        rig = self.rig("set_real_ip_from 127.0.0.0/8;\nreal_ip_header X-Real-IP;\nreal_ip_recursive off;\n")          # (no 'directive is duplicate': it is another level)
        self.assertEqual(get_from(rig, "127.0.0.2", ["1.1.1.1"], {"X-Real-IP": "198.51.100.9"}), "198.51.100.9")
        self.assertEqual(get_from(rig, "127.0.0.2", ["198.51.100.5"]), "127.0.0.2", "X-Forwarded-For is not the header any more")
        rig = self.rig("real_ip_header X-Real-IP;\n")
        self.assertEqual(get_from(rig, "127.0.0.2", ["1.1.1.1"], {"X-Real-IP": "198.51.100.9"}), "198.51.100.9", "the shipped ranges stay")

    def test_a_proxy_added_to_the_shipped_list_is_believed_and_skipped_in_a_chain(self):
        """The operator's file is the shipped list, with the ranges of loopback taken out and one range of it put back (here 127.0.0.64/26, standing for the addresses of a CDN)."""
        rig = self.rig(without_peer(LIST, "127.0.0.1") + "set_real_ip_from 127.0.0.64/26;\n")
        self.assertEqual(get_from(rig, "127.0.0.77", ["198.51.100.5"]), "198.51.100.5")
        self.assertEqual(get_from(rig, "127.0.0.2", ["198.51.100.5"]), "127.0.0.2", "outside the operator's list")
        self.assertEqual(get_from(rig, "127.0.0.77", ["198.51.100.5, 127.0.0.70"]), "198.51.100.5", "a proxy behind the added one is skipped")
        self.assertEqual(get_from(rig, "127.0.0.77", ["198.51.100.5, 10.0.0.2"]), "198.51.100.5", "and one of the shipped ranges that the file kept")


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the shipped list was NOT run in front of the chat hub (tests/scripts/test_nginx_trusted_proxies.py)")
class TheHubRuns(unittest.TestCase):
    """The real hub (eight sockets for each address) behind the real docker/nginx.conf: the address that it counts by is the one that nginx settled on."""

    def sockets(self, trusted, visitors):
        """The status of one chat socket for each of the visitors (an X-Forwarded-For each) through a proxy at 127.0.0.1; the sockets stay open until the end of the test."""
        hub = hub_rig.Rig()
        hub.__enter__()
        self.addCleanup(hub.__exit__, None, None, None)
        rig = sub.Rig(trusted=trusted, hub_port=hub.port)
        self.addCleanup(rig.stop)
        clients = []
        self.addCleanup(lambda: [c.close() for c in clients])
        statuses = []
        for visitor in visitors:
            clients.append(hc.Client(rig.port, headers={"X-Forwarded-For": visitor}))
            statuses.append(clients[-1].status.split()[1])
        return statuses

    def test_ten_visitors_have_a_socket_each(self):
        self.assertEqual(self.sockets(LIST, ["198.51.100.%d" % (100 + i) for i in range(10)]), ["101"] * 10)

    def test_one_visitor_still_has_eight(self):
        self.assertEqual(self.sockets(LIST, ["198.51.100.200"] * 10), ["101"] * 8 + ["429"] * 2)

    def test_an_empty_list_counts_the_address_of_the_connection(self):
        self.assertEqual(self.sockets("", ["198.51.100.%d" % (100 + i) for i in range(10)]), ["101"] * 8 + ["429"] * 2)


# ------------------------------------------------------------------------------------------------------------------------------------------------------------ real addresses, opt-in
ALIASES = {"10.77.0.5": True, "10.0.0.1": True, "10.255.255.254": True, "172.16.0.9": True, "172.18.0.1": True, "172.20.0.7": True, "172.31.255.1": True, "192.168.0.9": True, "192.168.77.5": True,
           "9.255.255.9": False, "11.0.0.9": False, "172.15.255.9": False, "172.32.0.9": False, "192.167.255.9": False, "192.169.0.9": False, "100.64.0.9": False, "169.254.1.9": False,
           "198.51.100.77": False, "203.0.113.9": False, "8.8.4.4": False}


def can_add_addresses():
    return (os.environ.get("ANTS_TEST_ADDRESS_ALIASES") == "1" and sys.platform.startswith("linux") and hasattr(os, "geteuid") and os.geteuid() == 0 and shutil.which("ip") is not None
            and shutil.which("nginx") is not None)


@unittest.skipUnless(can_add_addresses(), "opt-in: ANTS_TEST_ADDRESS_ALIASES=1 on Linux as root with `ip` and nginx (adds addresses to the loopback device while it runs, removes them after)")
class TheAddressesRun(unittest.TestCase):
    """Connections that really come from private and from public addresses: only the private ones (the six ranges) are believed."""

    @classmethod
    def setUpClass(cls):
        cls.added = []
        for address in ALIASES:
            done = subprocess.run(["ip", "addr", "add", address + "/32", "dev", "lo"], capture_output=True, text=True)
            if done.returncode == 0:
                cls.added.append(address)                                         # (an address that is there already is somebody else's: not used, not removed)
        cls.rig = sub.Rig(trusted=LIST)

    @classmethod
    def tearDownClass(cls):
        cls.rig.stop()
        for address in cls.added:
            subprocess.run(["ip", "addr", "del", address + "/32", "dev", "lo"], capture_output=True)

    def test_only_the_private_addresses_are_believed(self):
        self.assertGreaterEqual(len(self.added), len(ALIASES) - 2)
        for address in self.added:
            for lines in (["198.51.100.5"], ["6.6.6.6, 198.51.100.5"]):
                self.assertEqual(get_from(self.rig, address, lines, {"X-Client-Address": "9.9.9.9"}), "198.51.100.5" if ALIASES[address] else address, (address, lines))
            self.assertEqual(in_list(address), ALIASES[address], address)           # (the table of this file agrees with what nginx did)

    def test_an_empty_list_believes_none_of_them(self):
        rig = sub.Rig(trusted="")
        self.addCleanup(rig.stop)
        for address in self.added:
            self.assertEqual(get_from(rig, address, ["198.51.100.5"]), address, address)


if __name__ == "__main__":
    unittest.main()
