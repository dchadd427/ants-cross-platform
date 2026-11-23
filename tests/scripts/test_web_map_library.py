#!/usr/bin/env python3
"""The browser side of the map pages (web/map_library.js, web/map_pages.css, the Map Maker's address; docs/MAP_PAGES.md): run by ./run_tests.sh --fast and by the CI;
tests/data/map_library_mutants.json breaks one rule at a time, for tools/mutate.py.

  - TheIndexScript      the real script runs on a fake of the DOM (tests/scripts/web_map_library_check.js, node): the page stays until a control is used, the search and the filters, the order (Most played
                        asks /maps once), 48 to a page with a pager, the page back when everything is cleared, and every text of a map put in as text
  - TheMapMaker         "Open in Map maker": the page's own function that opens the library map named in the address runs on fakes (tests/scripts/web_mapmaker_address_check.js)
  - TheFiles            the byte budgets of the files and of the pages the tool makes (tools/size_limits.py); plain ES5; the script never puts a text of a map in as markup; the image and build_web.sh carry the
                        files and make the pages before the sitemap that lists them (read as text: no run builds the image)
"""
import gzip
import os
import re
import shutil
import subprocess
import sys
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
import map_pages as mp  # noqa: E402
import size_limits as limits  # noqa: E402
import web_without_docker as wd  # noqa: E402  (the Dockerfile read as steps)

NODE = shutil.which("node")


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def run_node(script):
    done = subprocess.run([NODE, os.path.join(REPO, "tests", "scripts", script)], capture_output=True, text=True, timeout=120)
    return done


@unittest.skipUnless(NODE, "node is needed")
class TheIndexScript(unittest.TestCase):
    def test_the_script_passes_its_checks_on_a_fake_dom(self):
        done = run_node("web_map_library_check.js")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failed", done.stdout)
        for part in ("A the page stays as it is", "B the search and the filters", "C the order", "D more than 48, the text of a player", "E no list, no page"):
            self.assertIn("RUNNING: %s ... done" % part, done.stdout)


@unittest.skipUnless(NODE, "node is needed")
class TheMapMaker(unittest.TestCase):
    def test_the_map_maker_opens_the_library_map_that_its_address_names(self):
        done = run_node("web_mapmaker_address_check.js")
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn("0 failed", done.stdout)
        for part in ("A a map named in the address", "B what is not a map", "C an old browser"):
            self.assertIn("RUNNING: %s ... done" % part, done.stdout)


class TheFiles(unittest.TestCase):
    def test_the_style_and_the_script_are_within_their_budgets(self):
        for name, raw, packed in (("map_pages.css", limits.MAP_PAGES_BUDGET, limits.MAP_PAGES_GZIP_BUDGET), ("map_library.js", limits.MAP_LIBRARY_BUDGET, limits.MAP_LIBRARY_GZIP_BUDGET)):
            data = read("web", name).encode("utf-8")
            self.assertLess(len(data), raw, "%s is %d bytes" % (name, len(data)))
            self.assertLess(len(gzip.compress(data, 9)), packed, "%s is %d bytes gzipped" % (name, len(gzip.compress(data, 9))))

    def test_what_the_tool_makes_is_within_its_budgets(self):
        maps = mp.read_library()
        files = mp.build("https://beta.playants.org", read("web", "map_pages", "page.html"), maps)
        data = files["html/map_library_data.js"].encode("utf-8")
        self.assertLess(len(data), limits.MAP_DATA_BUDGET, "the list is %d bytes" % len(data))
        self.assertLess(len(gzip.compress(data, 9)), limits.MAP_DATA_GZIP_BUDGET)
        biggest = max(len(t.encode("utf-8")) for p, t in files.items() if "/map/" in p)
        self.assertLess(biggest, limits.MAP_PAGE_BUDGET, "the biggest map page is %d bytes" % biggest)
        index = max(len(t.encode("utf-8")) for p, t in files.items() if "/library/" in p)
        self.assertLess(index, limits.MAP_INDEX_PAGE_BUDGET, "the biggest page of the index is %d bytes" % index)

    def test_the_script_is_plain_es5_and_never_puts_what_came_with_the_list_in_as_markup(self):
        # (an older browser must run it: nothing a run of the real script shows. And the one place that sets markup puts back the page's own text, saved when the script began; no run of a text of a map reaches it)
        source = read("web", "map_library.js")
        code = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
        for bad in ("=>", "let ", "const ", "`", "class ", "async ", "await ", "document.write", "eval(", "new Function", "XMLHttpRequest", "localStorage", "cookie", "insertAdjacentHTML", "outerHTML"):
            self.assertNotIn(bad, code, bad)
        assignments = re.findall(r"innerHTML\s*=\s*([^;,]*)", code)
        self.assertGreaterEqual(len(assignments), 2)
        for value in assignments:
            self.assertTrue(value.strip().startswith("still."), value)

    def test_the_script_asks_the_site_for_one_thing_and_only_when_most_played_is_chosen(self):
        code = re.sub(r"/\*.*?\*/", "", read("web", "map_library.js"), flags=re.S)
        self.assertEqual(re.findall(r"fetch\(([^,)]*)", code), ["'/maps'"])

    def test_the_image_and_build_web_sh_carry_the_files_and_make_the_pages_before_the_sitemap_that_lists_them(self):
        steps = wd.parse_dockerfile(read("Dockerfile"))
        builder = [s for s in steps if s.op == "RUN" and "tools/map_pages.py" in s.text]
        sitemap = [s for s in steps if s.op == "RUN" and "tools/seo_files.py" in s.text]
        self.assertEqual((len(builder), len(sitemap)), (1, 1))
        self.assertLess(steps.index(builder[0]), steps.index(sitemap[0]))
        self.assertIn("--pages /src/map_site/addresses.txt", sitemap[0].text)
        copies = [w for s in steps if s.op == "COPY" for w in s.words[:-1]]
        for needed in ("web/map_pages.css", "web/map_library.js", "tools/map_pages.py", "tools/library_guard.py", "tools/seo_files.py", "Community-Maps/maps.json"):
            self.assertIn(needed, copies, needed)
        self.assertEqual(len([s for s in steps if s.op == "COPY" and s.words[-1] == "/etc/nginx/ants-map-pages/"]), 1)
        build = read("build_web.sh")
        self.assertLess(build.index("tools/map_pages.py"), build.index("tools/seo_files.py"))
        self.assertIn("--pages", build)
        for name in ("map_pages.css", "map_library.js"):
            self.assertIn(name, build)

    @unittest.skipUnless(hasattr(sys, "stdlib_module_names"), "needs Python 3.10")
    def test_the_tool_needs_nothing_but_the_standard_library_and_the_two_tools_that_the_image_copies_beside_it(self):
        imported = set(re.findall(r"^(?:import|from) ([a-z_]+)", read("tools", "map_pages.py"), re.M))
        self.assertEqual(imported - set(sys.stdlib_module_names), {"library_guard", "seo_files"})


if __name__ == "__main__":
    unittest.main()
