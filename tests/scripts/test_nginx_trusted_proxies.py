#!/usr/bin/env python3
"""The list of reverse proxies that the web image ships (docker/trusted-proxies.conf), and the way docker/nginx.conf and the stack files use it (run by ./run_tests.sh --fast and by the CI; docs/SERVER.md "Behind a reverse proxy").

Behind a reverse proxy the only address that nginx sees is the proxy's. Every limit for one visitor (the zones of docker/nginx.conf, which are keyed on the address, and the X-Client-Address that the
chat hub and the game server count by) is then one limit for the whole site, unless the list names the proxy: the visitor is then the right-most entry of X-Forwarded-For that is not a proxy itself. What
is tested here:

  - the text of the shipped list: the six private and internal ranges and nothing else, X-Forwarded-For as the header, recursive; read with the ipaddress module against private, boundary and public
    addresses (no range of the Internet, nothing that matches everything). On every machine;
  - the wiring: the list is included once in the http block (the default of the site) and the operator's own folder once in the server block (a list there replaces the default); every address that a door
    is told or that a zone counts comes from $remote_addr and never from a header; the image has the list; the three compose files mount ANTS_TRUSTED_PROXIES_FILE as a FULL path whose default is
    /dev/null (a path of the repository does not work in a Portainer repository stack: its files are not on the host), into the folder that nginx includes;
  - where the program nginx is installed (a Linux machine has all of 127.0.0.0/8): the REAL file and the REAL docker/nginx.conf in front of stand-in doors and a real chat hub. Connections from different
    internal addresses, each with other X-Forwarded-For headers, are different visitors for the zones and for the X-Client-Address; a value that a visitor wrote himself, or in a header of another name, is
    skipped; a connection from an address outside the list keeps its own whatever it sends; the operator's file replaces the list; and with the OLD list (a file of comments) the same requests are
    one visitor (the "before" of the change). The ranges of IPv6 are read through the header, by nginx's own matching;
  - opt-in (ANTS_TEST_ADDRESS_ALIASES=1; Linux, root, `ip`): the same through connections that really come from 10.x, 172.16-31.x, 192.168.x and public addresses (aliases on the loopback device, removed after).

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
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO)
sys.path.insert(0, HERE)
import hub_client as hc  # noqa: E402
import hub_rig  # noqa: E402
import test_nginx_submissions as sub  # noqa: E402

read = sub.maps.read
strip_comments = sub.maps.strip_comments
LIST = read("docker", "trusted-proxies.conf")
CONF = sub.CONF
DIRECTIVES = [l.strip() for l in strip_comments(LIST).splitlines() if l.strip()]
RANGES = [re.fullmatch(r"set_real_ip_from (\S+);", l).group(1) for l in DIRECTIVES if l.startswith("set_real_ip_from ")]
COMPOSE_FILES = ("docker-compose.stack.yml", "docker-compose.staging.yml", "docker-compose.yml")
MOUNT = "${ANTS_TRUSTED_PROXIES_FILE:-/dev/null}:/etc/nginx/ants-trusted-proxies.d/own.conf:ro"

TRUSTED = ["127.0.0.1", "127.0.0.2", "127.255.255.255", "10.0.0.0", "10.1.2.3", "10.255.255.255", "172.16.0.0", "172.17.0.1", "172.18.0.5", "172.31.255.255", "192.168.0.0", "192.168.1.1", "192.168.255.255",
           "::1", "fc00::", "fd00::7", "fdff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"]
OUTSIDE = ["0.0.0.0", "1.1.1.1", "8.8.8.8", "9.255.255.255", "11.0.0.0", "100.64.0.1", "126.255.255.255", "128.0.0.0", "169.254.1.1", "172.15.255.255", "172.32.0.0", "192.0.2.1", "192.167.255.255",
           "192.169.0.0", "198.51.100.7", "203.0.113.9", "224.0.0.1", "255.255.255.255", "::", "::2", "2001:db8::1", "2606:4700::1111", "2a00:1450::5", "fbff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", "fe00::",
           "fe80::1", "ff02::1"]
PRIVATE = ("127.0.0.0/8", "10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "::1/128", "fc00::/7")


def in_list(address, ranges=None):
    """Whether an address is inside one of the ranges (the ipaddress module's reading of them)."""
    ip = ipaddress.ip_address(address)
    return any(ip in ipaddress.ip_network(r) for r in (RANGES if ranges is None else ranges))


def service(text, name):
    """The lines of one service of a compose file, comments dropped."""
    lines = text.splitlines()
    start = next(i for i, l in enumerate(lines) if l == "  %s:" % name)
    end = next((i for i in range(start + 1, len(lines)) if re.match(r"^  \S|^\S", lines[i])), len(lines))
    return [l for l in lines[start:end] if not l.lstrip().startswith("#")]


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

    def test_the_comment_says_what_it_assumes(self):
        comment = "\n".join(l for l in LIST.splitlines() if l.lstrip().startswith("#"))
        for words in ("X-Forwarded-For itself", "PUBLIC address", "Do not publish the port", "Every container on the network of the proxy", "ANTS_TRUSTED_PROXIES_FILE", "replaces this list"):
            self.assertIn(words, comment, words)
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
        for variable in ("$http_x_forwarded_for", "$http_x_real_ip", "$http_x_client_address", "$http_forwarded", "$http_client_ip", "$http_cf_connecting_ip", "$http_true_client_ip",
                         "$proxy_add_x_forwarded_for", "$realip_remote_addr"):
            self.assertNotIn(variable, CONF, variable)
        keys = set(re.findall(r"limit_(?:req|conn)_zone\s+(\S+)\s+zone=", CONF))
        self.assertEqual(keys - {"$binary_remote_addr"}, {"$history_plain_key", "$history_query_key", "$submit_post_key", "$admin_login_key", "$test_map_post_key"})
        for key in keys - {"$binary_remote_addr"}:
            self.assertLessEqual(map_values(key[1:]), {"$binary_remote_addr", '""'}, key)         # (a key is the address, or nothing)

    def test_the_three_compose_files_mount_the_operators_list_by_a_full_path_with_nothing_as_the_default(self):
        folder = re.search(r"include (/etc/nginx/ants-trusted-proxies\.d)/\*\.conf;", CONF).group(1)
        for name in COMPOSE_FILES:
            with open(os.path.join(REPO, name), encoding="utf-8") as f:
                text = f.read()
            mounts = [l.strip() for l in service(text, "ants-beta") if "ANTS_TRUSTED_PROXIES_FILE" in l]
            self.assertEqual(mounts, ["- " + MOUNT], name)
            source, target, mode = re.fullmatch(r"- (\S+?\}):(/\S+):(\w+)", mounts[0]).groups()
            default = re.fullmatch(r"\$\{ANTS_TRUSTED_PROXIES_FILE:-([^}]*)\}", source).group(1)
            self.assertTrue(default.startswith("/"), "%s: the default is a full path (a path of the repository, or a name, is no file on the host of a Portainer repository stack): %s" % (name, default))
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
        for path in (("docker", "nginx.conf"), ("Dockerfile",), ("docker-compose.stack.yml",), ("docker-compose.staging.yml",), ("docs", "HUB.md"), ("docs", "SERVER.md"), ("docs", "MAP_SUBMISSIONS.md"),
                     ("docs", "WORKFLOW.md")):
            text = read(*path)
            for old in ("image's empty file", "image's one file is all comments", "the image's file is all comments", "mount a file over /etc/nginx/ants-trusted-proxies/proxies.conf",
                        "none: the address of the connection counts"):
                self.assertNotIn(old, text, "/".join(path))
        server = read("docs", "SERVER.md")
        for words in ("### Behind a reverse proxy", "`ANTS_TRUSTED_PROXIES_FILE` | none |", "**full path of the copy on the host**", "no use in a Portainer repository stack", "**only for a connection from a private or internal address**",
                      "Do not publish the web port on a public interface", "(`127.0.0.0/8`, `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`, `::1`, `fc00::/7`:"):
            self.assertIn(words, server, words)
        for name in ("HUB.md", "MAP_SUBMISSIONS.md", "WORKFLOW.md"):
            self.assertIn("SERVER.md#behind-a-reverse-proxy", read("docs", name), name)           # (the one home of the list, its assumptions and the override)


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

    def test_with_the_old_list_every_visitor_is_the_proxy(self):
        rig = sub.Rig(trusted="")                                                  # (the file of the image before: all comments)
        self.addCleanup(rig.stop)
        for visitor in ("198.51.100.5", "203.0.113.9", "2001:db8:1234::5"):
            self.assertEqual(get_from(rig, "127.0.0.2", [visitor]), "127.0.0.2", visitor)

    def test_a_visitor_has_limits_of_his_own_and_cannot_use_up_those_of_the_others(self):
        for trusted, expected in ((LIST, 201), ("", 503)):                          # (with the old list the second visitor is the first one: the limit of everybody)
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

    def test_with_the_old_list_the_eight_are_the_whole_sites(self):
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

    def test_the_old_list_believes_none_of_them(self):
        rig = sub.Rig(trusted="")
        self.addCleanup(rig.stop)
        for address in self.added:
            self.assertEqual(get_from(rig, address, ["198.51.100.5"]), address, address)


if __name__ == "__main__":
    unittest.main()
