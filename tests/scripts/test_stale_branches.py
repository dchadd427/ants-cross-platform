#!/usr/bin/env python3
"""Tests of tools/stale_branches.py and of its weekly job .github/workflows/stale-branches.yml (run by ./run_tests.sh --fast and by the CI).

The tool deletes the branches that nobody works on: not the default branch, not `staging`, not a protected or a kept one, not one that is the head of an open pull request of this
repository, and only when the last commit is older than the limit. The tests run it against a stand-in for the GitHub API on this machine:

  - the decisions: an old branch goes; a young one, one with an open pull request, a protected one, the default branch, `staging` and a kept one stay; a pull request from a fork
    with the same branch name protects nothing; the age is the commit's date
  - the safety: a dry run deletes nothing; at most the cap of branches go in a run, the oldest first; a failed list, a page that is no list, a commit date that is no date or a
    branch without a commit stops the tool before ANY delete; one failed delete is told and the others go on (exit status 1)
  - the API: more than one page of branches and of pull requests, names with a slash or odd characters sent percent-encoded, the token in the header and never in the output,
    control characters of a name never in the log
  - the workflow: weekly, a manual run that is a dry run by default, the token's rights, the one tool call
"""
import datetime
import http.server
import json
import os
import re
import subprocess
import sys
import threading
import unittest
import urllib.parse

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOOL = os.path.join(REPO, "tools", "stale_branches.py")
WORKFLOW = os.path.join(REPO, ".github", "workflows", "stale-branches.yml")
sys.path.insert(0, os.path.join(REPO, "tools"))
sys.path.insert(0, os.path.join(REPO, "tests", "scripts"))
import quick_http  # noqa: E402   (the stdlib server asks the network for a name when it binds: tests/scripts/quick_http.py)
import stale_branches  # noqa: E402

NOW = datetime.datetime.now(datetime.timezone.utc)
SLUG = "owner/name"
TOKEN = "tok-secret-1234"


def days_ago(n):
    return (NOW - datetime.timedelta(days=n)).strftime("%Y-%m-%dT%H:%M:%SZ")


class FakeGitHub:
    """A repository: branches {name: (days old, protected)}, open pull requests [(head ref, head repo)], a default branch, and what the tool did to it."""

    def __init__(self, branches, prs=(), default="main", fail_list=None, fail_delete=(), bad_date=(), no_commit=()):
        self.branches = dict(branches)
        self.prs = list(prs)
        self.default = default
        self.fail_list = fail_list
        self.fail_delete = set(fail_delete)
        self.bad_date = set(bad_date)
        self.no_commit = set(no_commit)
        self.deleted = []
        self.requests = []
        self.auth = set()
        self.page_size = 100
        self.server = None

    def handler(fake):
        class H(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def send(self, code, obj=None):
                body = json.dumps(obj).encode() if obj is not None else b""
                self.send_response(code)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_GET(self):
                u = urllib.parse.urlsplit(self.path)
                q = urllib.parse.parse_qs(u.query)
                fake.requests.append(("GET", u.path))
                fake.auth.add(self.headers.get("Authorization"))
                base = "/repos/" + SLUG
                if u.path == base:
                    return self.send(200, {"default_branch": fake.default})
                page, per = int(q.get("page", ["1"])[0]), int(q.get("per_page", ["30"])[0])
                per = min(per, fake.page_size)
                if u.path == base + "/pulls":
                    rows = [{"head": {"ref": ref, "repo": {"full_name": repo}}} for ref, repo in fake.prs]
                    return self.send(200, rows[(page - 1) * per:page * per])
                if u.path == base + "/branches":
                    if fake.fail_list == "http":
                        return self.send(500, {"message": "boom"})
                    if fake.fail_list == "notlist":
                        return self.send(200, {"branches": []})
                    names = sorted(fake.branches)
                    rows = []
                    for n in names:
                        age, protected = fake.branches[n]
                        row = {"name": n, "protected": protected, "commit": {"sha": "sha-" + n.encode().hex()}}
                        if n in fake.no_commit:
                            row = {"name": n, "protected": protected}
                        rows.append(row)
                    return self.send(200, rows[(page - 1) * per:page * per])
                m = re.match(re.escape(base) + r"/commits/sha-([0-9a-f]+)$", u.path)
                if m:
                    name = bytes.fromhex(m.group(1)).decode()
                    date = "yesterday" if name in fake.bad_date else days_ago(fake.branches[name][0])
                    return self.send(200, {"commit": {"committer": {"date": date}}})
                self.send(404, {"message": "no"})

            def do_DELETE(self):
                u = urllib.parse.urlsplit(self.path)
                fake.requests.append(("DELETE", u.path))
                fake.auth.add(self.headers.get("Authorization"))
                prefix = "/repos/" + SLUG + "/git/refs/heads/"
                if not u.path.startswith(prefix):
                    return self.send(404, {"message": "no"})
                name = urllib.parse.unquote(u.path[len(prefix):])
                if name in fake.fail_delete:
                    return self.send(422, {"message": "no"})
                fake.deleted.append(name)
                fake.branches.pop(name, None)
                self.send(204)
        return H

    def __enter__(self):
        self.server = quick_http.QuickThreadingHTTPServer(("127.0.0.1", 0), self.handler())
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.url = "http://127.0.0.1:%d" % self.server.server_address[1]
        return self

    def __exit__(self, *a):
        self.server.shutdown()
        self.server.server_close()


def run_tool(fake, *args):
    env = dict(os.environ, GITHUB_REPOSITORY=SLUG, GITHUB_TOKEN=TOKEN)
    p = subprocess.run([sys.executable, "-I", TOOL, "--api-url", fake.url] + list(args), capture_output=True, text=True, env=env, timeout=60)
    return p.returncode, p.stdout, p.stderr


BASE = {"main": (0, True), "staging": (90, False), "old-one": (45, False), "old-two": (31, False), "young": (3, False), "borderline": (29, False),
        "has-pr": (80, False), "protected-old": (80, True), "kept": (80, False)}


class Decisions(unittest.TestCase):
    def test_only_the_old_unprotected_branches_without_a_pr_go(self):
        with FakeGitHub(BASE, prs=[("has-pr", SLUG)]) as f:
            code, out, err = run_tool(f, "--keep", "kept")
            self.assertEqual(code, 0, out + err)
            self.assertEqual(sorted(f.deleted), ["old-one", "old-two"])
            self.assertIn("deleted old-one", out)

    def test_a_pull_request_from_a_fork_protects_nothing(self):
        with FakeGitHub({"main": (0, True), "same-name": (60, False)}, prs=[("same-name", "someone/fork")]) as f:
            code, out, err = run_tool(f)
            self.assertEqual(code, 0, out + err)
            self.assertEqual(f.deleted, ["same-name"])

    def test_the_default_branch_is_whatever_the_repository_says(self):
        with FakeGitHub({"trunk": (400, False), "main": (400, False)}, default="trunk") as f:
            code, out, err = run_tool(f)
            self.assertEqual(code, 0, out + err)
            self.assertEqual(f.deleted, ["main"])

    def test_the_limit_is_an_argument(self):
        with FakeGitHub({"main": (0, True), "a": (10, False), "b": (5, False)}) as f:
            code, out, err = run_tool(f, "--days", "7")
            self.assertEqual(code, 0, out + err)
            self.assertEqual(f.deleted, ["a"])

    def test_nothing_to_do_says_so(self):
        with FakeGitHub({"main": (0, True), "young": (1, False)}) as f:
            code, out, err = run_tool(f)
            self.assertEqual((code, f.deleted), (0, []))
            self.assertIn("nothing to delete", out)


class Safety(unittest.TestCase):
    def test_a_dry_run_deletes_nothing(self):
        with FakeGitHub(BASE) as f:
            code, out, err = run_tool(f, "--dry-run")
            self.assertEqual(code, 0, out + err)
            self.assertEqual(f.deleted, [])
            self.assertFalse([r for r in f.requests if r[0] == "DELETE"])
            self.assertIn("would delete old-one", out)

    def test_the_cap_deletes_the_oldest_first_and_names_the_rest(self):
        branches = {"main": (0, True)}
        branches.update({"b%d" % i: (40 + i, False) for i in range(6)})
        with FakeGitHub(branches) as f:
            code, out, err = run_tool(f, "--max-delete", "2")
            self.assertEqual(code, 0, out + err)
            self.assertEqual(f.deleted, ["b5", "b4"])
            self.assertEqual(out.count("left for the next run"), 4)

    def test_a_failed_list_stops_before_any_delete(self):
        for how in ("http", "notlist"):
            with self.subTest(how=how), FakeGitHub(BASE, fail_list=how) as f:
                code, out, err = run_tool(f)
                self.assertEqual(code, 1, out + err)
                self.assertEqual(f.deleted, [])

    def test_a_bad_commit_date_or_a_branch_without_a_commit_stops_before_any_delete(self):
        for kw in ({"bad_date": ["young"]}, {"no_commit": ["young"]}):
            with self.subTest(kw=kw), FakeGitHub(BASE, **kw) as f:
                code, out, err = run_tool(f)
                self.assertEqual(code, 1, out + err)
                self.assertEqual(f.deleted, [])

    def test_one_failed_delete_is_told_and_the_others_go_on(self):
        with FakeGitHub(BASE, prs=[("has-pr", SLUG)], fail_delete=["old-one"]) as f:
            code, out, err = run_tool(f, "--keep", "kept")
            self.assertEqual(code, 1)
            self.assertEqual(f.deleted, ["old-two"])
            self.assertIn("could not delete old-one", out)

    def test_wrong_arguments_and_a_missing_token_are_status_2(self):
        with FakeGitHub(BASE) as f:
            self.assertEqual(run_tool(f, "--days", "0")[0], 2)
            self.assertEqual(run_tool(f, "--nope")[0], 2)
            env = {k: v for k, v in os.environ.items() if k != "GITHUB_TOKEN"}
            env["GITHUB_REPOSITORY"] = SLUG
            p = subprocess.run([sys.executable, "-I", TOOL, "--api-url", f.url], capture_output=True, text=True, env=env, timeout=60)
            self.assertEqual(p.returncode, 2)
            self.assertEqual(f.deleted, [])


class Api(unittest.TestCase):
    def test_several_pages_of_branches_and_of_pull_requests(self):
        branches = {"main": (0, True)}
        branches.update({"n%03d" % i: (60, False) for i in range(230)})
        prs = [("n%03d" % i, SLUG) for i in range(0, 230, 2)]
        with FakeGitHub(branches, prs=prs) as f:
            f.page_size = 100
            code, out, err = run_tool(f, "--max-delete", "500")
            self.assertEqual(code, 0, out + err)
            self.assertEqual(sorted(f.deleted), ["n%03d" % i for i in range(1, 230, 2)])

    def test_names_are_sent_percent_encoded_and_logged_without_control_characters(self):
        odd = "feature/a b#c%d\x07x"
        with FakeGitHub({"main": (0, True), odd: (50, False)}) as f:
            code, out, err = run_tool(f)
            self.assertEqual(code, 0, out + err)
            self.assertEqual(f.deleted, [odd])
            self.assertNotIn("\x07", out)
            self.assertTrue(any(m == "DELETE" and "feature/a%20b%23c%25d%07x" in p for m, p in f.requests), f.requests)

    def test_the_token_is_sent_and_never_printed(self):
        with FakeGitHub(BASE) as f:
            code, out, err = run_tool(f)
            self.assertEqual(f.auth, {"Bearer " + TOKEN})
            self.assertNotIn(TOKEN, out + err)

    def test_a_failing_api_does_not_print_the_token_either(self):
        with FakeGitHub(BASE, fail_list="http") as f:
            code, out, err = run_tool(f)
            self.assertNotIn(TOKEN, out + err)


class Dates(unittest.TestCase):
    def test_parse_date_refuses_what_is_no_date(self):
        for bad in (None, "", "yesterday", "2026-10-13", 5):
            with self.subTest(bad=bad), self.assertRaises(stale_branches.ApiError):
                stale_branches.parse_date(bad)
        self.assertEqual(stale_branches.parse_date("2026-10-13T05:06:07Z").year, 2026)


class Workflow(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(WORKFLOW, encoding="utf-8") as f:
            cls.text = f.read()

    def test_it_runs_weekly_and_by_hand_and_a_manual_run_is_a_dry_run_unless_told(self):
        self.assertRegex(self.text, r"schedule:\s*\n\s*- cron: '\d+ \d+ \* \* \d'")
        self.assertNotRegex(self.text, r"cron: '0 ")           # not on the hour (all the jobs of the world start there)
        self.assertIn("workflow_dispatch:", self.text)
        self.assertRegex(self.text, r"dry_run:[\s\S]*?default: true")

    def test_it_holds_only_the_rights_it_needs_and_calls_the_one_tool(self):
        self.assertRegex(self.text, r"permissions:\s*\n\s*contents: write\s*\n\s*pull-requests: read")
        self.assertIn("tools/stale_branches.py", self.text)
        self.assertIn("GITHUB_TOKEN: ${{ github.token }}", self.text)
        self.assertNotIn("secrets.", self.text)

    def test_the_scheduled_run_deletes_and_does_not_run_on_a_fork(self):
        self.assertIn("github.repository == 'dchadd427/ants-cross-platform'", self.text)
        self.assertRegex(self.text, r"--days 30")


if __name__ == "__main__":
    unittest.main()
