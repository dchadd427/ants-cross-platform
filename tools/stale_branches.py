#!/usr/bin/env python3
"""Deletes the branches that nobody works on, so that a repository does not collect them (the weekly job .github/workflows/stale-branches.yml runs it).

usage: stale_branches.py [--days N] [--dry-run] [--keep NAME ...] [--max-delete M] [--api-url URL]

A branch is deleted when ALL of these hold (docs/WORKFLOW.md, "Branches, pull requests and releases"):
  - it is not the default branch, not `staging` and not named by --keep, and it is not protected;
  - no open pull request of this repository has it as its head (a pull request from a fork that happens to use the same branch name does not count);
  - its last commit is more than N days old (30 by default; the commit's date, not the date of the last push).
A branch that GitHub already deletes by itself (the head of a merged pull request) never gets here; this is for the ones that were never merged.

--dry-run only says what would be deleted. At most M branches are deleted in one run (25 by default, the oldest first), so that a mistake of this tool or of the API cannot empty
a repository in one go; the rest wait for the next run. Anything that is not an answer the tool understands (a failed list, a page it cannot read) stops it BEFORE any delete.

The repository is $GITHUB_REPOSITORY (owner/name) and the token $GITHUB_TOKEN (never printed). Branch names come from the repository, so they are data: they are written with
control characters taken out and are always put in a path percent-encoded. The exit status is 0 (done, also when nothing was to be deleted), 1 (a delete failed, or the API did
not answer as expected) or 2 (a wrong argument or no repository / token).
"""
import argparse
import datetime
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request

DEFAULT_DAYS = 30
DEFAULT_MAX_DELETE = 25
ALWAYS_KEEP = ("staging",)       # `staging` holds whatever is being tried and may sit idle for weeks
PAGE = 100
MAX_PAGES = 50                   # 5,000 branches or pull requests: more is no repository of ours
TIMEOUT_SECONDS = 20


class ApiError(Exception):
    pass


def clean(text):
    """A name for the log: printable characters only (a branch name is whatever someone pushed)."""
    return "".join(c if c.isprintable() else "?" for c in str(text))[:200]


def parse_date(text):
    try:
        return datetime.datetime.strptime(text, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=datetime.timezone.utc)
    except (TypeError, ValueError):
        raise ApiError("a commit date that is not a date: %s" % clean(text))


class Api:
    def __init__(self, base, repo, token):
        self.base = base.rstrip("/")
        self.repo = repo
        self.token = token

    def call(self, method, path, query=""):
        url = "%s/repos/%s%s%s" % (self.base, self.repo, path, ("?" + query) if query else "")
        req = urllib.request.Request(url, method=method, headers={
            "Authorization": "Bearer " + self.token, "Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "ants-stale-branches"})
        try:
            with urllib.request.urlopen(req, timeout=TIMEOUT_SECONDS) as r:
                body = r.read(16 * 1024 * 1024)
                return json.loads(body.decode("utf-8")) if body else None
        except urllib.error.HTTPError as e:
            raise ApiError("%s %s: HTTP %d" % (method, clean(path), e.code))
        except (urllib.error.URLError, OSError, ValueError) as e:
            raise ApiError("%s %s: %s" % (method, clean(path), clean(getattr(e, "reason", e))))

    def pages(self, path, extra=""):
        out = []
        for page in range(1, MAX_PAGES + 1):
            part = self.call("GET", path, "per_page=%d&page=%d%s" % (PAGE, page, extra))
            if not isinstance(part, list):
                raise ApiError("GET %s: not a list" % clean(path))
            out.extend(part)
            if len(part) < PAGE:
                return out
        raise ApiError("GET %s: more than %d pages" % (clean(path), MAX_PAGES))


def candidates(api, days, keep, now):
    """The branches to delete, oldest first: [(name, last commit date)]."""
    info = api.call("GET", "")
    default = info.get("default_branch") if isinstance(info, dict) else None
    if not isinstance(default, str) or not default:
        raise ApiError("the repository has no default branch in the answer")
    with_pr = set()
    for pr in api.pages("/pulls", "&state=open"):
        head = pr.get("head") or {}
        if (head.get("repo") or {}).get("full_name") == api.repo and isinstance(head.get("ref"), str):
            with_pr.add(head["ref"])
    limit = now - datetime.timedelta(days=days)
    found = []
    for b in api.pages("/branches"):
        name = b.get("name")
        if not isinstance(name, str) or not name:
            raise ApiError("a branch without a name in the answer")
        if name == default or name in ALWAYS_KEEP or name in keep or b.get("protected") or name in with_pr:
            continue
        sha = (b.get("commit") or {}).get("sha")
        if not isinstance(sha, str) or not sha:
            raise ApiError("branch %s has no commit in the answer" % clean(name))
        commit = api.call("GET", "/commits/" + urllib.parse.quote(sha, safe=""))
        when = parse_date(((commit or {}).get("commit") or {}).get("committer", {}).get("date"))
        if when < limit:
            found.append((name, when))
    found.sort(key=lambda item: (item[1], item[0]))
    return found


def run(api, days, keep, dry_run, max_delete, now, out):
    found = candidates(api, days, keep, now)
    if not found:
        out("no branch is older than %d days without a pull request: nothing to delete" % days)
        return 0
    failed = 0
    for i, (name, when) in enumerate(found):
        age = (now - when).days
        if i >= max_delete:
            out("left for the next run (at most %d a run): %s (%d days)" % (max_delete, clean(name), age))
            continue
        if dry_run:
            out("would delete %s (last commit %s, %d days ago)" % (clean(name), when.strftime("%Y-%m-%d"), age))
            continue
        try:
            api.call("DELETE", "/git/refs/heads/" + urllib.parse.quote(name, safe="/"))
            out("deleted %s (last commit %s, %d days ago)" % (clean(name), when.strftime("%Y-%m-%d"), age))
        except ApiError as e:
            failed += 1
            out("could not delete %s: %s" % (clean(name), e))
    return 1 if failed else 0


def main(argv=None):
    p = argparse.ArgumentParser(description="Delete the branches that nobody works on.")
    p.add_argument("--days", type=int, default=DEFAULT_DAYS)
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--keep", action="append", default=[])
    p.add_argument("--max-delete", type=int, default=DEFAULT_MAX_DELETE)
    p.add_argument("--api-url", default="https://api.github.com")
    try:
        args = p.parse_args(argv)
    except SystemExit:
        return 2
    repo = os.environ.get("GITHUB_REPOSITORY", "")
    token = os.environ.get("GITHUB_TOKEN", "")
    if args.days < 1 or args.max_delete < 0 or repo.count("/") != 1 or not token:
        print("stale_branches: needs --days >= 1, $GITHUB_REPOSITORY (owner/name) and $GITHUB_TOKEN", file=sys.stderr)
        return 2
    api = Api(args.api_url, repo, token)
    try:
        return run(api, args.days, set(args.keep), args.dry_run, args.max_delete, datetime.datetime.now(datetime.timezone.utc), print)
    except ApiError as e:
        print("stale_branches: stopped before any delete that was not done: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
