#!/usr/bin/env python3
"""Tests of tools/deploy_webhook.sh, the call that the deploy job of .github/workflows/ci.yml makes (run by ./run_tests.sh --fast and by the CI).

A small web server on the loopback address plays the Portainer webhook. The address that the script is given (WEBHOOK_URL) is a SECRET: whatever happens, neither the address
nor the host nor the port nor the secret part of the path may appear in anything that the script prints (the log of a public repository's workflow is public). Checked:

  - a webhook that answers is called with a POST, once, and the script says the status
  - a webhook that fails a few times and then answers is called again (the retries), and the script succeeds
  - one that never answers, or that is not there, makes the script fail with the status and nothing else
  - no address: "deploy secret not set: skipped", success, and nothing is called; an address that is not http(s), or that holds a blank, a quote or a backslash, is refused
  - the address is read from the environment, never from an argument, and is not on the command line of the curl that makes the call
"""
import http.server
import os
import shutil
import subprocess
import threading
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.path.join(REPO, "tools", "deploy_webhook.sh")
SECRET_PATH = "/api/stacks/webhooks/5f1c0d3e-secret-token-8a7b"


class Webhook:
    """A loopback server that answers POSTs with the given status codes in turn (the last one repeats) and records every request."""

    def __init__(self, codes):
        self.codes = list(codes)
        self.requests = []
        outer = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                length = int(self.headers.get("Content-Length") or 0)
                body = self.rfile.read(length) if length else b""
                outer.requests.append((self.command, self.path, body))
                code = outer.codes.pop(0) if len(outer.codes) > 1 else outer.codes[0]
                self.send_response(code)
                self.end_headers()

            def log_message(self, *args):
                pass

        self.server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def url(self):
        return "http://127.0.0.1:%d%s" % (self.port, SECRET_PATH)

    def close(self):
        self.server.shutdown()
        self.server.server_close()


# A port where a connection is refused, on Linux and macOS alike: nothing listens on port 1 of the loopback, and a test that runs beside this one cannot take
# it (below 1024). A free port that was closed and given back was taken once by such a test (its server answered the POST with 501), and a port held by a
# socket that never listens is refused on Linux but makes macOS wait for the timeout.
REFUSING_PORT = 1


@unittest.skipUnless(shutil.which("curl") and shutil.which("bash"), "curl and bash are needed")
class DeployWebhook(unittest.TestCase):
    def call(self, url, *args, retry_delay="1"):
        env = dict(os.environ, DEPLOY_RETRY_DELAY=retry_delay)
        env.pop("WEBHOOK_URL", None)
        if url is not None:
            env["WEBHOOK_URL"] = url
        return subprocess.run(["bash", SCRIPT, *args], capture_output=True, text=True, env=env, timeout=120)

    def assertNoSecret(self, result, url):
        text = result.stdout + result.stderr
        for part in (url, SECRET_PATH, "secret-token", "127.0.0.1", "localhost"):
            self.assertNotIn(part, text, "the output names a part of the secret address: %r" % part)
        port = url.split(":")[2].split("/")[0] if url.count(":") >= 2 else None
        if port:
            self.assertNotIn(port, text, "the output names the port of the secret address")

    def test_a_webhook_that_answers_is_called_once_with_a_post(self):
        hook = Webhook([204])
        self.addCleanup(hook.close)
        result = self.call(hook.url(), "production")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.strip(), "deploy webhook (production): called, HTTP 204")
        self.assertEqual([(m, p) for m, p, _ in hook.requests], [("POST", SECRET_PATH)])
        self.assertEqual(hook.requests[0][2], b"")                                      # no body: the address is the whole message
        self.assertNoSecret(result, hook.url())

    def test_a_webhook_that_fails_a_few_times_is_called_again(self):
        hook = Webhook([500, 502, 204])
        self.addCleanup(hook.close)
        result = self.call(hook.url(), "staging")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(len(hook.requests), 3)
        self.assertEqual(result.stdout.strip(), "deploy webhook (staging): called, HTTP 204")
        self.assertNoSecret(result, hook.url())

    def test_a_webhook_that_always_fails_makes_the_script_fail_after_the_retries(self):
        hook = Webhook([503])
        self.addCleanup(hook.close)
        result = self.call(hook.url(), "production")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(len(hook.requests), 4)                                         # the first call and three retries
        self.assertIn("FAILED", result.stdout)
        self.assertIn("HTTP 503", result.stdout)
        self.assertNoSecret(result, hook.url())

    def test_an_address_that_is_not_there_fails_without_naming_it(self):
        url = "http://127.0.0.1:%d%s" % (REFUSING_PORT, SECRET_PATH)
        result = self.call(url, "production")
        self.assertEqual(result.returncode, 1)
        self.assertIn("FAILED (curl exit status 7", result.stdout)
        self.assertNoSecret(result, url)

    def test_without_an_address_it_says_so_and_succeeds(self):
        for url in (None, ""):
            result = self.call(url)
            self.assertEqual(result.returncode, 0)
            self.assertEqual(result.stdout.strip(), "deploy secret not set: skipped")

    def test_an_address_that_cannot_be_used_is_refused_and_not_printed(self):
        for url in ("ftp://example.org/hook/abc", "example.org/hook/abc", "https://example.org/a b", 'https://example.org/a"b', "https://example.org/a\\b"):
            result = self.call(url, "production")
            self.assertEqual(result.returncode, 1, url)
            self.assertIn("not called", result.stdout)
            self.assertNotIn("example.org", result.stdout + result.stderr)
            self.assertNotIn("abc", result.stdout + result.stderr)

    def test_the_address_is_not_an_argument_and_not_on_the_command_line_of_curl(self):
        with open(SCRIPT, encoding="utf-8") as f:
            text = f.read()
        code_lines = [l for l in text.splitlines() if not l.lstrip().startswith("#")]
        for line in code_lines:
            if "curl " in line:
                after_curl = line[line.index("curl "):]
                self.assertNotIn("WEBHOOK_URL", after_curl, "the address must not be on curl's command line (a process list shows it): " + line)
        self.assertNotIn("/usr/bin/printf", text)                                       # (the built-in printf hands it to curl through a pipe, no process has it as an argument)
        self.assertIn("--config -", text)
        self.assertNotIn("--show-error", text)                                          # curl's own messages name the host
        self.assertNotIn("set -x", text)
        hook = Webhook([204])
        self.addCleanup(hook.close)
        result = self.call(hook.url(), "production")
        self.assertEqual(result.returncode, 0)
        self.assertNoSecret(result, hook.url())


if __name__ == "__main__":
    unittest.main()
