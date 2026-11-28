#!/usr/bin/env python3
"""The names of the computer players against the blocklist (tests/data/bot_names_blocklist.txt: words that no name may be or hold; run by ./run_tests.sh --tools and the CI, with the other python tests of tests/scripts).

The blocklist is a moderation list, so it and this test are kept with the tests of the game server and are not part of the public repository; the tests of the names themselves (the list, the picker, the label, the
reserved-name rule) are tests/scripts/test_bot_names.py and tests/test_ai/test_ai_names.cpp. The list is ONE file (tools/bot_names.txt); tools/gen_bot_names.py makes the C++ table and the JavaScript table from it.
  - TheBlocklist: the file is well formed, the matcher finds what it should (tested on made-up words), and no name of the list, of the C++ table or of the JavaScript table is or holds a word of it.
  - TheJavaScript: tests/scripts/web_bot_names_blocklist_check.js runs the page's own code against the list, and it must FAIL on a copy of the library that holds a blocked name.
The node tests need node; without it they are skipped and say so.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools"))
import gen_bot_names  # noqa: E402

CHECK = os.path.join(REPO, "tests", "scripts", "web_bot_names_blocklist_check.js")
LIB = os.path.join(REPO, "web", "front", "bot_names.js")
LIST = os.path.join(REPO, "tools", "bot_names.txt")
INC = os.path.join(REPO, "src", "ants_ai", "bot_names_data.inc")
BLOCKLIST = os.path.join(REPO, "tests", "data", "bot_names_blocklist.txt")
NODE = shutil.which("node")


def read(path):
    with open(path, encoding="utf-8", newline="") as f:
        return f.read()


def names_of_list():
    return gen_bot_names.read_names(read(LIST))


def names_of_the_cpp_table():
    """the names that the C++ picker serves: the initializers of kBotNames (src/ants_ai/bot_names_data.inc), the comment lines left out"""
    return re.findall(r'"([^"]+)"', "\n".join(line for line in read(INC).splitlines() if not line.startswith("//")))


def blocklist():
    return [w.strip() for w in read(BLOCKLIST).splitlines() if w.strip() and not w.strip().startswith("#")]


def blocked(name, words):
    """The words of the blocklist that a name is, or holds: `=word` only as the whole name, `word` anywhere inside it (case ignored)."""
    low = name.lower()
    return [w for w in words if (low == w[1:] if w.startswith("=") else w in low)]


def run_check(lib=LIB):
    done = subprocess.run([NODE, CHECK, lib, BLOCKLIST], capture_output=True, text=True, timeout=60)
    return done.returncode, done.stdout + done.stderr


class TheBlocklist(unittest.TestCase):
    def test_the_blocklist_file_is_well_formed(self):
        words = blocklist()
        self.assertGreaterEqual(len(words), 100)
        for w in words:
            self.assertRegex(w, r"^=?[a-z]{2,}$", w)
        self.assertEqual(len(set(words)), len(words), "a line twice")
        self.assertTrue({"fuck", "shit", "=ass", "nigg", "hitler", "=admin"} <= set(words))

    def test_the_blocklist_matcher_finds_what_it_should(self):
        words = ["=ass", "=hell", "dick", "nazi"]
        self.assertEqual(blocked("Ass", words), ["=ass"])
        self.assertEqual(blocked("Cassie", words), [])                           # (a whole-name word is not found inside a name)
        self.assertEqual(blocked("Hello", words), [])
        self.assertEqual(blocked("Hell", words), ["=hell"])
        self.assertEqual(blocked("Dickens", words), ["dick"])                    # (a plain word is found anywhere, in any case)
        self.assertEqual(blocked("Panama", words), [])
        self.assertEqual(blocked("Banazit", words), ["nazi"])

    def test_no_name_of_the_list_is_or_holds_a_word_of_the_blocklist(self):
        words = blocklist()
        found = {n: blocked(n, words) for n in names_of_list() if blocked(n, words)}
        self.assertEqual(found, {})

    def test_no_name_of_the_cpp_table_is_or_holds_a_word_of_the_blocklist(self):
        words = blocklist()
        names = names_of_the_cpp_table()
        self.assertEqual(names, names_of_list(), "the C++ table is the list (tests/scripts/test_bot_names.py holds the generator to it)")
        self.assertEqual({n: blocked(n, words) for n in names if blocked(n, words)}, {})


@unittest.skipUnless(NODE, "node is not installed")
class TheJavaScript(unittest.TestCase):
    def test_the_check_passes_on_the_library(self):
        code, out = run_check()
        self.assertEqual(code, 0, out)
        self.assertIn("ok: the blocklist holds", out)

    def test_the_check_notices_a_name_that_is_blocked(self):
        names = names_of_list()
        text = read(LIB)
        a = text.index(gen_bot_names.BEGIN)
        b = text.index(gen_bot_names.END) + len(gen_bot_names.END)
        for what, new in (("a name on the blocklist", "Nazi"), ("a name inside which a blocked word stands", "Dickens"), ("a whole-name word", "Admin"), ("a name that was taken out", "Warden"),
                          ("a name that was taken out", "Atta")):
            with tempfile.TemporaryDirectory() as folder:
                lib = os.path.join(folder, "bot_names.js")
                with open(lib, "w", encoding="utf-8", newline="") as f:
                    f.write(text[:a] + gen_bot_names.render_js_block(names[:-1] + [new]) + text[b:])
                code, out = run_check(lib)
            self.assertNotEqual(code, 0, "the check did not notice " + what)
            self.assertIn("FAIL", out, what)


if __name__ == "__main__":
    unittest.main()
