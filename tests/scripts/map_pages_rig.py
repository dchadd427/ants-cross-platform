#!/usr/bin/env python3
"""Puts the map pages into the folders of a web server that a test starts, as the image's build does with tools/map_pages.py (docs/MAP_PAGES.md): the pages and the list into the document root, the other
names of the maps into the folder that docker/nginx.conf includes (/etc/nginx/ants-map-pages), and the index's style and script beside them (the Dockerfile copies those two from web/)."""
import os
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOOL = os.path.join(REPO, "tools", "map_pages.py")


def make(out, library=None, originals=None, origin="https://site.example"):
    """Runs the tool into `out` with stamps that fill every slot; the real library unless `library` and `originals` say another."""
    command = [sys.executable, TOOL, origin, out, "--version", "v0.0.0", "--build-id", "test", "--build-date", "today"]
    if library:
        command += ["--library", library, "--ledger", os.path.join(out, "no_such_ledger.json")]       # (a library that a test made up has no addresses fixed: the ledger of the real one is not its)
    if originals:
        command += ["--originals", originals]
    done = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if done.returncode != 0:
        raise AssertionError("tools/map_pages.py failed: " + done.stderr)


def put_pages(docroot, nginx_dir, library=None, originals=None):
    """The pages into `docroot` and the redirects into `nginx_dir` (made if it is not there)."""
    out = tempfile.mkdtemp(prefix="ants_mp_rig.")
    try:
        make(out, library, originals)
        shutil.copytree(os.path.join(out, "html"), docroot, dirs_exist_ok=True)
        os.makedirs(nginx_dir, exist_ok=True)
        for name in os.listdir(os.path.join(out, "nginx")):
            shutil.copyfile(os.path.join(out, "nginx", name), os.path.join(nginx_dir, name))
        for name in ("map_pages.css", "map_library.js", "map_plays.js"):
            shutil.copyfile(os.path.join(REPO, "web", name), os.path.join(docroot, name))
        done = subprocess.run([sys.executable, os.path.join(REPO, "tools", "seo_files.py"), "https://site.example", docroot, "--pages", os.path.join(out, "addresses.txt")], capture_output=True, text=True, timeout=60)
        if done.returncode != 0:
            raise AssertionError("tools/seo_files.py failed: " + done.stderr)                                   # (the sitemap of the map pages: robots.txt and sitemap.xml are written again, the same)
    finally:
        shutil.rmtree(out, ignore_errors=True)
