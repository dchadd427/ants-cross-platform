#!/usr/bin/env python3
"""docker/nginx.conf's location for the skills that teach a person's AI to make maps (/ai/skills/; docs/MAP_AI.md, "The skills"). Run by ./run_tests.sh --fast and by the CI; the block is RUN where the program
nginx is installed.

  - the text: one prefix location (^~, so that no regular expression takes it) and no other that names the path; a GET or HEAD only and no query before anything is read; the files of the site's own folder, read
    only (no listing, nothing outside it); the four types of the files and no other; revalidated at every use; the headers that a location that sets one must repeat (nosniff, HSTS, the cross-origin pair), a policy that
    lets a file run nothing, no CORS header;
  - the real block over the tree that tools/make_ai_skills.py builds: every file of it is served with its type, its charset where it is text, its no-cache rule and no sniffing, HEAD too; a name that is not there, a
    folder, a listing, a query, another method, a path that climbs out, an encoded slash and a file of the site's root are all refused and none of them shows anything but a status; the tree that the build made is
    the only thing that is reachable.
"""
import json
import os
import re
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO, "tools"))
import make_ai_skills as tool  # noqa: E402
import test_nginx_maps as maps  # noqa: E402  (start_nginx)
import test_nginx_stats as stats  # noqa: E402

CONF = stats.strip_comments(stats.read("docker", "nginx.conf"))
BLOCK = stats.block_of(CONF, "location ^~ /ai/skills/ ")
EXPECTED = {"zip": "application/zip", "md": "text/markdown", "json": "application/json"}


class TheText(unittest.TestCase):
    def test_it_is_the_one_location_that_names_the_path_and_a_prefix_one(self):
        locations = re.findall(r"^\s*location\s+(.*?)\s*\{", CONF, re.M)
        self.assertEqual([l for l in locations if "/ai/" in l and "/hub/ai" not in l], ["^~ /ai/skills/"])
        self.assertTrue(BLOCK)
        self.assertNotRegex(CONF, r"location\s+~\*?\s+[^\n]*/ai/")

    def test_the_checks_come_before_a_file_is_looked_for(self):
        self.assertIn('if ($request_method !~ "^(GET|HEAD)\\z") { return 405; }', BLOCK)
        self.assertIn("if ($is_args) { return 404; }", BLOCK)
        self.assertLess(BLOCK.index("$request_method"), BLOCK.index("try_files"))
        self.assertLess(BLOCK.index("$is_args"), BLOCK.index("try_files"))
        for line in ("root /usr/share/nginx/html;", "try_files $uri =404;", "autoindex off;"):
            self.assertIn(line, BLOCK)
        for forbidden in ("alias", "proxy_pass", "rewrite", "Access-Control", "expires", "gzip", "autoindex on", "set $"):
            self.assertNotIn(forbidden, BLOCK, forbidden)

    def test_the_types_are_the_four_of_the_files_and_text_says_its_charset(self):
        types = re.search(r"types \{([^}]*)\}", BLOCK).group(1)
        self.assertEqual(sorted(re.findall(r"([a-z/]+) ([a-z]+);", types)), sorted((v, k) for k, v in EXPECTED.items()) + [("text/plain", "txt")])
        self.assertIn("default_type application/octet-stream;", BLOCK)
        self.assertIn("charset utf-8;", BLOCK)
        self.assertIn("charset_types text/markdown application/json text/plain;", BLOCK)

    def test_the_headers_are_the_ones_a_static_location_repeats_and_a_file_is_revalidated(self):
        headers = dict(re.findall(r'add_header ([A-Za-z-]+) "([^"]*)" always;', BLOCK))
        self.assertEqual(headers["Cache-Control"], "no-cache, must-revalidate")
        self.assertEqual(headers["X-Content-Type-Options"], "nosniff")
        self.assertEqual(headers["Strict-Transport-Security"], "max-age=604800")
        self.assertEqual(headers["Cross-Origin-Opener-Policy"], "same-origin")
        self.assertEqual(headers["Cross-Origin-Embedder-Policy"], "require-corp")
        self.assertEqual(headers["Content-Security-Policy"], "default-src 'none'; sandbox")
        self.assertEqual(headers["Cross-Origin-Resource-Policy"], "same-origin")
        self.assertEqual(headers["Referrer-Policy"], "no-referrer")
        self.assertEqual(len(re.findall(r"add_header ", BLOCK)), len(headers), "every header is written once")

    def test_the_location_names_the_folder_that_the_image_carries_the_files_to(self):
        docker = stats.read("Dockerfile")
        self.assertIn("COPY --from=builder /src/ai_site/ai/ /usr/share/nginx/html/ai/", docker)
        self.assertIn("/usr/share/nginx/html/ai/skills/", stats.read("docker", "nginx.conf"))


@unittest.skipUnless(shutil.which("nginx"), "nginx is not installed: the block for /ai/skills/ was NOT run (tests/scripts/test_nginx_ai_skills.py)")
class TheBlockRuns(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.mkdtemp(prefix="ants_nginx_skills_")
        tool.build(os.path.join(cls.work, "html"))
        with open(os.path.join(cls.work, "html", "secret.txt"), "w") as f:
            f.write("a file of the site's root that this location must not give")
        os.makedirs(os.path.join(cls.work, "tmp"), exist_ok=True)
        text = "pid %s/nginx.pid;\nerror_log %s/error.log;\nevents { worker_connections 64; }\nhttp {\n access_log off;\n client_body_temp_path %s/tmp/b;\n proxy_temp_path %s/tmp/p;\n fastcgi_temp_path %s/tmp/f;\n" % ((cls.work,) * 5)
        text += " uwsgi_temp_path %s/tmp/u;\n scgi_temp_path %s/tmp/s;\n server {\n  listen 80;\n  root %s/html;\n" % (cls.work, cls.work, cls.work)
        text += "  add_header X-Server-Level \"yes\" always;\n  location ^~ /ai/skills/ {%s}\n }\n}\n" % BLOCK.replace("/usr/share/nginx/html", cls.work + "/html")
        full = (("user root;\n" if hasattr(os, "geteuid") and os.geteuid() == 0 else "") + text)

        def write_conf(port):
            conf = os.path.join(cls.work, "nginx.conf")
            with open(conf, "w") as f:
                f.write(full.replace("listen 80;", "listen 127.0.0.1:%d;" % port))
            return conf
        cls.proc, cls.port = maps.start_nginx(cls.work, write_conf)

    @classmethod
    def tearDownClass(cls):
        cls.proc.terminate()
        cls.proc.wait(timeout=10)
        shutil.rmtree(cls.work, ignore_errors=True)

    def ask(self, method, path, headers=None):
        import http.client
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        try:
            conn.putrequest(method, path, skip_accept_encoding=True)
            for k, v in (headers or {}).items():
                conn.putheader(k, v)
            conn.endheaders()
            r = conn.getresponse()
            return r.status, {k.lower(): v for k, v in r.getheaders()}, r.read()
        finally:
            conn.close()

    def files(self):
        root = os.path.join(self.work, "html", "ai", "skills")
        return sorted(os.path.relpath(os.path.join(b, f), root).replace(os.sep, "/") for b, _, fs in os.walk(root) for f in fs)

    def test_every_file_of_the_tree_is_served_with_its_type_and_the_headers_get_and_head(self):
        for rel in self.files():
            ext = rel.rsplit(".", 1)[1]
            want = EXPECTED[ext]
            for method in ("GET", "HEAD"):
                status, headers, body = self.ask(method, "/ai/skills/" + rel)
                self.assertEqual(status, 200, (method, rel))
                self.assertEqual(headers["content-type"], want + ("; charset=utf-8" if ext != "zip" else ""), rel)
                self.assertEqual(headers["cache-control"], "no-cache, must-revalidate")
                self.assertEqual(headers["x-content-type-options"], "nosniff")
                self.assertEqual(headers["content-security-policy"], "default-src 'none'; sandbox")
                self.assertNotIn("x-server-level", headers, "a location that sets a header does not inherit the server's: its own are all here")
                self.assertFalse([k for k in headers if k.startswith("access-control")])
                if method == "GET":
                    with open(os.path.join(self.work, "html", "ai", "skills", rel), "rb") as f:
                        self.assertEqual(body, f.read(), rel)
            self.assertIn("etag", self.ask("GET", "/ai/skills/" + rel)[1])
        index = json.loads(self.ask("GET", "/ai/skills/index.json")[2])
        self.assertEqual(len(index["skills"]), 6)

    def test_a_validator_gets_304_and_a_file_is_not_cached_without_it(self):
        status, headers, _ = self.ask("GET", "/ai/skills/index.json")
        self.assertEqual(self.ask("GET", "/ai/skills/index.json", {"If-None-Match": headers["etag"]})[0], 304)

    def test_nothing_else_is_reachable_through_the_prefix(self):
        for path in ("/ai/skills/", "/ai/skills", "/ai/skills/nothing.md", "/ai/skills/ants-map-guide/", "/ai/skills/ants-map-guide/nothing.md", "/ai/skills/../secret.txt", "/ai/skills/%2e%2e/secret.txt",
                     "/ai/skills/..%2fsecret.txt", "/ai/skills/ants-map-guide/..%2f..%2fsecret.txt", "/ai/skills//index.json.bak", "/ai/skills/.hidden", "/ai/skills/index.json/"):
            status, headers, body = self.ask("GET", path)
            self.assertIn(status, (301, 400, 403, 404), path)
            self.assertNotIn(b"must not give", body, path)
            self.assertNotIn(b"index of", body.lower(), path)

    def test_a_query_and_another_method_are_refused(self):
        self.assertEqual(self.ask("GET", "/ai/skills/index.json?x=1")[0], 404)
        self.assertEqual(self.ask("GET", "/ai/skills/index.json?")[0], 200, "an empty query is no query")
        for method in ("POST", "PUT", "DELETE", "PATCH", "OPTIONS"):
            status, headers, _ = self.ask(method, "/ai/skills/index.json", {"Content-Length": "0"})
            self.assertEqual(status, 405, method)
            self.assertFalse([k for k in headers if k.startswith("access-control")], "no preflight is answered")


if __name__ == "__main__":
    unittest.main()
