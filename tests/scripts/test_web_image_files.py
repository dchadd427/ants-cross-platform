#!/usr/bin/env python3
"""Every script and stylesheet that a page of web/ loads must be in the nginx image, named by the CI's check of the image, copied by the local web build and standing in the stub root of the routes test
(run by ./run_tests.sh --fast and by the CI, no docker needed).

The Dockerfile copies the web files ONE BY ONE (COPY web/<name> ...), and the routes of the site are checked against the real image (tests/scripts/web_routes_check.py) and against a stub root
(tests/scripts/test_nginx_routes.py). A page that loads a file that none of them knows works on a developer's machine and is broken on the site, which no other quick test sees; this one reads the pages:
  - every local <script src> and <link href> of web/*.html is a file of web/ and is copied by the Dockerfile (a folder copy such as web/front/ covers its files, web/favicon.* the icons);
  - the check of the image in .github/workflows/ci.yml lists it and build_web.sh copies it;
  - every script or stylesheet that web_routes_check.py asks the site for has a stand-in in the stub root of the routes test (the real image is only built by the web job).
"""
import glob
import os
import re
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


# Files that a page loads and that the web build makes (they are not in web/): name -> the line of the Dockerfile that copies it and the text of build_web.sh that copies it
BUILT = {"lvl_core.js": ("COPY --from=builder /src/build_web/lvl_core/lvl_core.js ", "lvl_core/lvl_core.js")}


def loaded_files():
    """{path in web/: [pages that load it]} for every local script and stylesheet of web/*.html"""
    found = {}
    for page in sorted(glob.glob(os.path.join(REPO, "web", "*.html"))):
        text = read("web", os.path.basename(page))
        for m in re.finditer(r"""<(?:script|link)\b[^>]*?\b(?:src|href)=["']([^"']+)["']""", text):
            ref = m.group(1).split("?")[0].split("#")[0]
            if re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:|^//", ref):
                continue
            found.setdefault(ref.lstrip("/"), []).append(os.path.basename(page))
    return found


class ThePagesAndTheImage(unittest.TestCase):
    dockerfile = read("Dockerfile")
    ci = read(".github", "workflows", "ci.yml")
    build = read("build_web.sh")

    def test_the_pages_load_something(self):
        refs = loaded_files()
        for name in ("history_page.js", "match_stats.js", "match_stats.css", "matches.css", "replay_page.js", "lvl_core.js", "lvl_core_api.js", "front/mapmaker_app.js"):
            self.assertIn(name, refs, name + " is loaded by no page: did the scan stop reading the pages?")

    def test_every_file_a_page_loads_is_a_file_of_web_and_is_copied_into_the_image(self):
        for ref, pages in loaded_files().items():
            if ref in BUILT:
                self.assertIn(BUILT[ref][0], self.dockerfile, "%s (loaded by %s) is made by the web build: the image must copy it" % (ref, ", ".join(pages)))
                continue
            self.assertTrue(os.path.isfile(os.path.join(REPO, "web", ref)), "%s (loaded by %s) is not a file of web/" % (ref, ", ".join(pages)))
            if ref.startswith("front/"):
                self.assertIn("COPY web/front/ ", self.dockerfile, ref)
            elif re.match(r"^favicon\.[a-z]+$", ref):
                self.assertIn("COPY web/favicon.* ", self.dockerfile, ref)
            else:
                self.assertRegex(self.dockerfile, r"(?m)^COPY web/%s " % re.escape(ref), "%s (loaded by %s) is not copied into the image: add a COPY line to the Dockerfile" % (ref, ", ".join(pages)))

    def test_the_ci_names_every_top_level_file_in_its_check_of_the_image_and_the_local_build_copies_it(self):
        for ref, pages in loaded_files().items():
            if "/" in ref or ref.startswith("favicon."):
                continue
            self.assertRegex(self.ci, r"ls -l [^\n]*\b%s\b" % re.escape(ref), "%s (loaded by %s) is not in the CI's check of the image" % (ref, ", ".join(pages)))
            self.assertIn(BUILT[ref][1] if ref in BUILT else "web/" + ref, self.build, "%s (loaded by %s) is not copied by build_web.sh" % (ref, ", ".join(pages)))

    def test_the_stub_root_of_the_routes_test_has_every_script_and_stylesheet_that_the_routes_check_asks_for(self):
        asked = set(re.findall(r"""\(\s*["']/([A-Za-z0-9_.-]+\.(?:js|css))["']\s*,""", read("tests", "scripts", "web_routes_check.py")))
        self.assertTrue({"history_page.js", "match_stats.js", "match_stats.css"} <= asked, "the scan of web_routes_check.py found %s" % sorted(asked))
        stub = read("tests", "scripts", "test_nginx_routes.py")
        for name in sorted(asked):
            self.assertRegex(stub, r"""\(\s*["']%s["']\s*,""" % re.escape(name), "web_routes_check.py asks for /%s, which the stub root of test_nginx_routes.py does not have" % name)


if __name__ == "__main__":
    unittest.main()
