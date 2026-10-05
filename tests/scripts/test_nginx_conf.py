#!/usr/bin/env python3
"""docker/nginx.conf routes the game server's public status, GET /busy, safely (run by ./run_tests.sh --fast and by the CI).

The deploy job of the CI polls https://<site>/busy ({"matches": N, "players": M}) and restarts the site when no match runs. The page's own nginx forwards the path to the WebSocket
port of the game server, like /ws, but as a plain GET and nothing else. The syntax of the file is checked by the web job (`nginx -t` in the image); these are static checks of what
the block promises, read from the real files, so that a change that opens the door wider, or points it at another port, fails here:

  - /busy is an exact location, the only one of its name, and the address the workflow polls (and the one that ants_server answers)
  - only a GET without a query goes through (405 and 404 otherwise), no body and no upgrade is passed on, the connection is not kept
  - it goes to the same server and port as /ws, and that port is the --ws-port of both stack files
  - it is rate limited (the zone is declared outside the server block, the rate lets a poll a minute through), has short timeouts and adds no header or cache of its own
"""
import os
import re
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def strip_comments(text):
    return "\n".join(line.split("#", 1)[0] for line in text.splitlines())


def block_of(text, header):
    """The text between the braces of the block that starts with `header {` (nested braces counted)."""
    start = text.index(header)
    open_at = text.index("{", start)
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_at + 1:i]
    raise AssertionError("unbalanced braces after " + header)


def block_or_empty(text, header):
    try:
        return block_of(text, header)
    except ValueError:                                                                      # (a missing block fails the tests that look at it, with their own messages)
        return ""


CONF = strip_comments(read("docker", "nginx.conf"))
BUSY = block_or_empty(CONF, "location = /busy")
WS = block_or_empty(CONF, "location = /ws")


class TheLocation(unittest.TestCase):
    def test_busy_is_an_exact_location_and_the_only_one_of_its_name(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M)
        busy = [l for l in locations if "busy" in l]
        self.assertEqual(busy, ["= /busy"])                                                # no prefix or regular-expression location could also catch /busy/x or /busyness
        self.assertEqual(len(re.findall(r"location\s+= /busy\s*\{", CONF)), 1)

    def test_it_is_the_address_the_workflow_polls_and_the_server_answers(self):
        workflow = read(".github", "workflows", "ci.yml")
        site = re.search(r"'(https://[a-z0-9.-]+)/busy'", workflow)
        self.assertIsNotNone(site, "the deploy job has no default busy address")
        self.assertRegex(CONF, r"server_name\s+[^;]*\b%s\b" % re.escape(site.group(1).split("//", 1)[1]))
        self.assertIn('ws->set_status("/busy"', read("src", "ants_server", "main.cpp"))

    def test_only_a_plain_get_without_a_query_goes_through(self):
        self.assertIn("if ($request_method != GET) { return 405; }", BUSY)
        self.assertIn("if ($is_args) { return 404; }", BUSY)
        self.assertLess(BUSY.index("$request_method"), BUSY.index("proxy_pass"))             # the checks come before anything is passed on
        self.assertLess(BUSY.index("$is_args"), BUSY.index("proxy_pass"))
        self.assertLess(BUSY.index("limit_req "), BUSY.index("proxy_pass"))

    def test_no_body_and_no_upgrade_is_passed_on_and_the_connection_is_not_kept(self):
        self.assertIn("proxy_pass_request_body off;", BUSY)
        self.assertIn('proxy_set_header Content-Length "";', BUSY)
        self.assertIn('proxy_set_header Connection "close";', BUSY)
        self.assertNotRegex(BUSY, r"(?i)proxy_set_header\s+Upgrade")
        self.assertNotIn("upgrade", BUSY.lower())
        self.assertIn("proxy_set_header Upgrade $http_upgrade;", WS)                        # (the other door does: this is what tells them apart)

    def test_it_goes_to_the_same_server_and_port_as_the_websocket_door(self):
        target = re.search(r"set \$ants_server ([a-z0-9.-]+):(\d+);", BUSY)
        door = re.search(r"set \$ants_server ([a-z0-9.-]+):(\d+);", WS)
        self.assertIsNotNone(target)
        self.assertEqual(target.groups(), door.groups())
        self.assertIn("proxy_pass http://$ants_server;", BUSY)                              # (a variable: the name is resolved per request, like /ws, so the page starts without the server)
        self.assertIn("resolver 127.0.0.11", BUSY)
        for stack in ("docker-compose.stack.yml", "docker-compose.staging.yml"):
            command = re.search(r'command: \[.*?"--ws-port", "(\d+)"', read(stack))
            self.assertIsNotNone(command, stack)
            self.assertEqual(command.group(1), target.group(2), stack)

    def test_it_is_rate_limited_and_the_zone_is_declared_outside_the_server(self):
        zone = re.search(r"limit_req_zone \$binary_remote_addr zone=ants_busy:(\d+)m rate=(\d+)r/([sm]);", CONF)
        self.assertIsNotNone(zone)
        self.assertLess(CONF.index(zone.group(0)), CONF.index("server {"))                  # http context: before the server block
        per_second = int(zone.group(2)) / (1.0 if zone.group(3) == "s" else 60.0)
        self.assertGreaterEqual(per_second, 1 / 60.0)                                       # the deploy polls once a minute
        self.assertLessEqual(per_second, 10.0)                                              # and a flood is not let through
        self.assertRegex(BUSY, r"limit_req zone=ants_busy burst=\d+ nodelay;")
        self.assertIn("limit_req_status 503;", BUSY)

    def test_it_has_short_timeouts_and_adds_nothing_of_its_own(self):
        for directive, limit in (("proxy_connect_timeout", 5), ("proxy_read_timeout", 10), ("proxy_send_timeout", 10)):
            value = re.search(r"%s (\d+)s;" % directive, BUSY)
            self.assertIsNotNone(value, directive)
            self.assertLessEqual(int(value.group(1)), limit, directive)
        for forbidden in ("add_header", "expires", "proxy_cache", "proxy_ignore_headers", "alias", "root", "try_files", "rewrite", "sub_filter"):
            self.assertNotRegex(BUSY, r"\b%s\b" % forbidden, forbidden)                     # (the server's own Cache-Control: no-store is the answer's header)
        self.assertIn("proxy_hide_header X-Content-Type-Options;", BUSY)                     # (the server-level nosniff line applies once)

    def test_the_block_is_what_the_documents_say(self):
        text = read("docs", "WORKFLOW.md") + read("docs", "NETWORK_PORT.md") + read("docs", "SERVER.md")
        for needle in ("/busy", "\"matches\"", "\"players\""):
            self.assertIn(needle, text)


if __name__ == "__main__":
    unittest.main()
