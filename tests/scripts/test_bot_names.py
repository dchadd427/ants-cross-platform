#!/usr/bin/env python3
"""The names of the computer players (tools/bot_names.txt; docs/BOTS.md, "Names"; run by ./run_tests.sh --tools and the CI, with the other python tests of tests/scripts).

The list is ONE file; tools/gen_bot_names.py makes the C++ table (src/ants_ai/bot_names_data.inc) and the JavaScript table (the generated block of web/front/bot_names.js) from it.
  - TheGenerator: the committed outputs are what the generator makes (`--check`), the generator refuses a list that breaks a rule, and `--check` fails on an output that was edited by hand.
  - TheList: all printable ASCII letters, 3 to 8 of them, unique in any case, none of the blocklist (tests/data/bot_names_blocklist.txt, and the matcher itself is tested on made-up words),
    none of the page's twelve placeholder names, 240 to 260 names.
  - TheScript: web/front/bot_names.js is a plain ES5 script that node and a page can load.
  - TheJavaScript: tests/scripts/web_bot_names_check.js runs it against the tables that the C++ test (tests/test_ai/test_ai_names.cpp) is held to as well (golden vectors, the digests of
    10,000 keys, the cases of the label, the parser and the reserved-name rule); a third, independent implementation of the picker in this file must agree with it on 3,000 keys; and
    the check must FAIL when one thing is changed in a COPY of the library (the list index, the distinctness, a constant of the hash, the suffix check, the parser, ...) or of the list.
The node tests need node; without it they are skipped and say so.
"""
import json
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

CHECK = os.path.join(REPO, "tests", "scripts", "web_bot_names_check.js")
LIB = os.path.join(REPO, "web", "front", "bot_names.js")
LIST = os.path.join(REPO, "tools", "bot_names.txt")
BLOCKLIST = os.path.join(REPO, "tests", "data", "bot_names_blocklist.txt")
RULES = os.path.join(REPO, "web", "front", "lobby_rules.js")
GENERATOR = os.path.join(REPO, "tools", "gen_bot_names.py")
NODE = shutil.which("node")
M64 = (1 << 64) - 1


def read(path):
    with open(path, encoding="utf-8", newline="") as f:
        return f.read()


def names_of_list():
    return gen_bot_names.read_names(read(LIST))


def blocklist():
    return [w.strip() for w in read(BLOCKLIST).splitlines() if w.strip() and not w.strip().startswith("#")]


def blocked(name, words):
    """The words of the blocklist that a name is, or holds: `=word` only as the whole name, `word` anywhere inside it (case ignored)."""
    low = name.lower()
    return [w for w in words if (low == w[1:] if w.startswith("=") else w in low)]


def web_placeholders():
    m = re.search(r"var NAMES = \[([^\]]*)\]", read(RULES))
    return [s.strip().strip("'") for s in m.group(1).split(",")]


def mix64(z):
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
    return z ^ (z >> 31)


def pick(names, key, count):
    """A third implementation of the picker, written from the description in include/ants_ai/bot_names.hpp (the C++ and the JavaScript are the other two)."""
    base = mix64(key ^ 0xB07A11E5B07A11E5)
    out, used, draw = [], set(), 1
    while len(out) < min(count, len(names)):
        index = mix64((base + draw * 0x9E3779B97F4A7C15) & M64) % len(names)
        draw += 1
        if index not in used:
            used.add(index)
            out.append(names[index])
    return out


def run_check(lib=LIB, root=REPO, timeout=60):
    """(exit status, output) of the node check; a library that never ends (a picker that cannot find enough names) is status 124 after `timeout` seconds."""
    try:
        done = subprocess.run([NODE, CHECK, lib, root], capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return 124, "FAIL timeout: the check did not end"
    return done.returncode, done.stdout + done.stderr


class TheGenerator(unittest.TestCase):
    def test_the_committed_files_are_what_the_generator_makes(self):
        done = subprocess.run([sys.executable, GENERATOR, "--check"], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)

    def test_the_outputs_hold_the_names_in_the_order_of_the_list(self):
        names = names_of_list()
        inc = re.findall(r'"([A-Za-z]+)"', gen_bot_names.render_inc(names))
        self.assertEqual(inc, names)
        self.assertEqual(re.findall(r'"([A-Za-z]+)"', read(os.path.join(REPO, "src", "ants_ai", "bot_names_data.inc"))), names)
        block = read(LIB)
        block = block[block.index(gen_bot_names.BEGIN):block.index(gen_bot_names.END)]
        self.assertEqual(re.findall(r'"([A-Za-z]+)"', block), names)

    def test_the_generator_refuses_a_list_that_breaks_a_rule(self):
        good = names_of_list()
        bad_lists = {
            "the same name in another case": good + ["ada"],
            "the same name twice": good + [good[0]],
            "a name with a digit": good[:-1] + ["Ant1"],
            "a name of two letters": good[:-1] + ["Ab"],
            "a name of nine letters": good[:-1] + ["Abcdefghi"],
            "a name of ten letters": good[:-1] + ["Abcdefghij"],
            "a name that starts small": good[:-1] + ["pip"],
            "a name with a blank": good[:-1] + ["Ant Hill"],
            "an accent": good[:-1] + ["Zoë"],
            "too few names": good[:200],
            "too many names": good + ["Zz%s" % chr(97 + k) for k in range(20)],
        }
        for what, names in bad_lists.items():
            with self.assertRaises(ValueError, msg=what):
                gen_bot_names.read_names("\n".join(names))
        self.assertEqual(gen_bot_names.read_names("# a comment\n\n" + "\n".join(["  %s  " % n for n in good]) + "\n"), good)      # (comments, blank lines and blanks around a name are not names)

    def test_check_fails_on_an_output_that_was_edited_by_hand_and_the_generator_repairs_it(self):
        with tempfile.TemporaryDirectory() as root:
            for rel in (gen_bot_names.LIST, gen_bot_names.INC, gen_bot_names.JS):
                os.makedirs(os.path.dirname(os.path.join(root, rel)), exist_ok=True)
                shutil.copy(os.path.join(REPO, rel), os.path.join(root, rel))
            check = [sys.executable, GENERATOR, "--root", root, "--check"]
            self.assertEqual(subprocess.run(check, capture_output=True).returncode, 0)
            inc = os.path.join(root, gen_bot_names.INC)
            text = read(inc)
            with open(inc, "w", encoding="utf-8", newline="") as f:
                f.write(text.replace('"Ada"', '"Adx"', 1))
            self.assertEqual(subprocess.run(check, capture_output=True).returncode, 1, "an edited C++ table")
            js = os.path.join(root, gen_bot_names.JS)
            with open(js, "w", encoding="utf-8", newline="") as f:
                f.write(read(os.path.join(REPO, gen_bot_names.JS)).replace('"Alma"', '"Almx"', 1))
            self.assertEqual(subprocess.run(check, capture_output=True).returncode, 1, "an edited JavaScript table")
            self.assertEqual(subprocess.run([sys.executable, GENERATOR, "--root", root], capture_output=True).returncode, 0)
            self.assertEqual(subprocess.run(check, capture_output=True).returncode, 0, "the generator repaired both")
            self.assertEqual(read(inc), read(os.path.join(REPO, gen_bot_names.INC)))
            # a list that was edited without running the generator
            with open(os.path.join(root, gen_bot_names.LIST), "a", encoding="utf-8", newline="") as f:
                f.write("Zorro\n")
            self.assertEqual(subprocess.run(check, capture_output=True).returncode, 1, "a new name in the list only")


class TheList(unittest.TestCase):
    def test_shape_size_and_uniqueness(self):
        names = names_of_list()
        self.assertTrue(240 <= len(names) <= 260, len(names))
        for n in names:
            self.assertRegex(n, r"^[A-Z][a-z]{2,7}$", n)                         # printable ASCII letters only, 3 to 8 of them
            self.assertTrue(n.isascii() and n.isprintable())
        self.assertEqual(len({n.lower() for n in names}), len(names), "unique in any case")

    def test_none_is_one_of_the_pages_twelve_placeholders(self):
        placeholders = web_placeholders()
        self.assertEqual(len(placeholders), 12)
        self.assertEqual(set(names_of_list()) & set(placeholders), set())

    def test_no_name_is_or_holds_a_word_of_the_blocklist(self):
        words = blocklist()
        self.assertGreaterEqual(len(words), 100)
        found = {n: blocked(n, words) for n in names_of_list() if blocked(n, words)}
        self.assertEqual(found, {})

    def test_the_blocklist_matcher_finds_what_it_should(self):
        words = ["=ass", "=hell", "dick", "nazi"]
        self.assertEqual(blocked("Ass", words), ["=ass"])
        self.assertEqual(blocked("Cassie", words), [])                           # (a whole-name word is not found inside a name)
        self.assertEqual(blocked("Hello", words), [])
        self.assertEqual(blocked("Hell", words), ["=hell"])
        self.assertEqual(blocked("Dickens", words), ["dick"])                    # (a plain word is found anywhere, in any case)
        self.assertEqual(blocked("Panama", words), [])
        self.assertEqual(blocked("Banazit", words), ["nazi"])

    def test_the_blocklist_file_is_well_formed_and_the_list_is_clean_of_it_by_reading_too(self):
        words = blocklist()
        for w in words:
            self.assertRegex(w, r"^=?[a-z]{2,}$", w)
        self.assertEqual(len(set(words)), len(words), "a line twice")
        self.assertTrue({"fuck", "shit", "=ass", "nigg", "hitler", "=admin"} <= set(words))


class TheScript(unittest.TestCase):
    def test_it_is_a_plain_es5_script_with_no_dependency(self):
        text = read(LIB)
        self.assertNotRegex(text, r"(?m)^\s*(import|export)\s")
        self.assertNotRegex(text, r"\brequire\(")
        self.assertIn("module.exports", text)
        self.assertIn("root.AntsBotNames = api", text)
        code = "\n".join(re.sub(r"^\s*//.*$", "", line) for line in text.splitlines())
        code = re.sub(r"'(?:[^'\\\n]|\\.)*'|\"(?:[^\"\\\n]|\\.)*\"", "''", code)
        for what, pattern in (("BigInt", r"BigInt"), ("an arrow", r"=>"), ("const or let", r"\b(const|let)\b"), ("a template string", r"`"), ("a class", r"\bclass\b")):
            self.assertNotRegex(code, pattern, what)

    def test_the_cpp_table_is_read_by_the_cpp_picker_and_the_header_names_the_twin(self):
        self.assertIn('#include "bot_names_data.inc"', read(os.path.join(REPO, "src", "ants_ai", "bot_names.cpp")))
        self.assertIn("bot_names.cpp", read(os.path.join(REPO, "src", "ants_ai", "CMakeLists.txt")))
        self.assertIn("test_ai_names.cpp", read(os.path.join(REPO, "tests", "test_ai", "CMakeLists.txt")))
        for table in ("bot_names_golden.txt", "bot_label_cases.tsv", "bot_names_blocklist.txt"):
            self.assertIn(table, read(os.path.join(REPO, "tests", "test_ai", "test_ai_names.cpp")), "the C++ test reads " + table)
        for table in ("bot_names_golden.txt", "bot_label_cases.tsv", "bot_names_blocklist.txt"):
            self.assertIn(table, read(CHECK), "the JavaScript check reads " + table)

    def test_the_scripts_header_documents_the_key_forms(self):
        self.assertIn("hexadecimal", read(LIB))


@unittest.skipUnless(NODE, "node is not installed")
class TheJavaScript(unittest.TestCase):
    def test_the_check_passes_on_the_library(self):
        code, out = run_check()
        self.assertEqual(code, 0, out)
        self.assertRegex(out, r"ok: \d+ checks, 0 failures")

    def test_the_javascript_picker_agrees_with_an_independent_one_on_3000_keys(self):
        names = names_of_list()
        keys = ["%016x" % (mix64(i) if i % 2 else i) for i in range(3000)] + ["%016x" % (i << 40 | i) for i in range(100)]
        script = ("const B=require(%s);const keys=%s;process.stdout.write(JSON.stringify(keys.map((k,i)=>B.pickBotNames(k,i%%6))));"
                  % (json.dumps(LIB), json.dumps(keys)))
        done = subprocess.run([NODE, "-e", script], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stderr)
        got = json.loads(done.stdout)
        for i, key in enumerate(keys):
            self.assertEqual(got[i], pick(names, int(key, 16), i % 6), key)

    def test_the_javascript_accepts_a_key_as_a_number_and_as_a_pair(self):
        names = names_of_list()
        script = "const B=require(%s);process.stdout.write(JSON.stringify([B.pickBotNames(123456789012,3),B.pickBotNames([0xdeadbeef,0xcafef00d],3),B.pickBotNames('0xDEADBEEFCAFEF00D',3)]));" % json.dumps(LIB)
        a, b, c = json.loads(subprocess.run([NODE, "-e", script], capture_output=True, text=True, timeout=60).stdout)
        self.assertEqual(a, pick(names, 123456789012, 3))
        self.assertEqual(b, pick(names, 0xDEADBEEFCAFEF00D, 3))
        self.assertEqual(c, b)



@unittest.skipUnless(NODE, "node is not installed")
class TheMutants(unittest.TestCase):
    """The check must FAIL when the library, or the list, is wrong in one place (a copy of it is changed; the files of the repository never are)."""

    def mutant_fails(self, old, new, what):
        text = read(LIB)
        self.assertEqual(text.count(old), 1, "the mutant's text must be there once: " + old)
        with tempfile.TemporaryDirectory() as folder:
            lib = os.path.join(folder, "bot_names.js")
            with open(lib, "w", encoding="utf-8", newline="") as f:
                f.write(text.replace(old, new))
            code, out = run_check(lib, timeout=10)
        self.assertNotEqual(code, 0, "the check did not notice: " + what)
        self.assertIn("FAIL", out, what)

    def test_the_check_notices_a_wrong_index(self):
        self.mutant_fails("NAMES.length);\n            if (used[index])", "NAMES.length - 1);\n            if (used[index])", "the index is taken modulo one name too few")
        self.mutant_fails("out.push(NAMES[index]);", "out.push(NAMES[(index + 1) % NAMES.length]);", "the name one after the drawn one")
        self.mutant_fails("PICK_SEED = [0xB07A11E5, 0xB07A11E5]", "PICK_SEED = [0xB07A11E5, 0xB07A11E4]", "a constant of the hash")
        self.mutant_fails("PICK_STEP = [0x9E3779B9, 0x7F4A7C15]", "PICK_STEP = [0x9E3779B9, 0x7F4A7C14]", "the step of the draws")

    def test_the_check_notices_names_that_repeat(self):
        self.mutant_fails("            if (used[index]) continue;\n", "", "no skip of a name that was drawn")
        self.mutant_fails("used[index] = true;", "used[index] = false;", "a drawn name is not remembered")
        self.mutant_fails("Math.min(Math.max(want, 0), NAMES.length)", "Math.max(want, 0)", "no cut of the count to the list")

    def test_the_check_notices_a_wrong_hash(self):
        self.mutant_fails("r[i] = sum % 65536;", "r[i] = sum % 65535;", "the limbs of the 64-bit product")
        self.mutant_fails("carry = Math.floor(sum / 65536);", "carry = 0;", "the carry of the 64-bit product")
        self.mutant_fails("var C1 = [0xBF58476D, 0x1CE4E5B9]", "var C1 = [0xBF58476D, 0x1CE4E5B8]", "a constant of mix64")
        self.mutant_fails("return xor64(z, shr64(z, 31));", "return xor64(z, shr64(z, 30));", "a shift of mix64")
        self.mutant_fails("(lo > 4294967295 ? 1 : 0)", "0", "the carry of the 64-bit sum")
        self.mutant_fails("mod64(a, n) { return ((a[0] % n) * (4294967296 % n) + (a[1] % n)) % n; }", "mod64(a, n) { return (a[1] % n); }", "the high half in the remainder")

    def test_the_check_notices_a_wrong_suffix_or_prefix_rule(self):
        self.mutant_fails("if (inside === 'bot') return true;", "if (inside === 'bots') return true;", "the short suffix (bot) is not reserved")
        self.mutant_fails("return inside.indexOf('bot,') === 0 && inside.length > 4 && inside.indexOf(')') < 0;", "return false;", "the (bot, level) suffix is not reserved")
        self.mutant_fails("return inside.indexOf('bot,') === 0 && inside.length > 4 && inside.indexOf(')') < 0;", "return inside.indexOf('bot,') === 0;", "an empty tail counts")
        self.mutant_fails("if (s.indexOf('bot(') === 0) return true;", "if (s.indexOf('bot(') === 1) return true;", "the legacy prefix")
        self.mutant_fails("asciiLower(text.replace(/[ \\t\\r\\n]/g, ''))", "text.replace(/[ \\t\\r\\n]/g, '')", "case is not ignored")
        self.mutant_fails("asciiLower(text.replace(/[ \\t\\r\\n]/g, ''))", "asciiLower(text.replace(/^ +/, ''))", "blanks inside a name are not ignored")
        self.mutant_fails("var open = s.lastIndexOf('(');", "var open = s.indexOf('(');", "the LAST parenthesis opens the suffix")

    def test_the_check_notices_a_wrong_parser_or_label(self):
        self.mutant_fails("if (inside.slice(0, 2) !== ', ') return null;", "inside = inside.replace(/^, ?/, ', ');", "the blank after the comma may be left out")
        self.mutant_fails("return validLabelName(name) ? { name: name, tag: tag, legacy: false } : null;", "return { name: name, tag: tag, legacy: false };", "the name is checked")
        self.mutant_fails("{ 'Bot (Easy)': 'easy', 'Bot (Medium)': 'medium', 'Bot (Hard)': 'hard', 'Bot (Idle)': 'idle', 'Bot (Worker)': 'worker' }", "{ 'Bot (Easy)': 'easy', 'Bot (Medium)': 'medium', 'Bot (Hard)': 'hard', 'Bot (Idle)': 'idle' }", "a legacy label is not read")
        self.mutant_fails("{ easy: 'Easy', medium: 'Medium', hard: 'Hard', idle: 'idle', worker: 'worker' }", "{ easy: 'Easy', medium: 'Medium', hard: 'Hard', idle: 'Idle', worker: 'worker' }", "a kind in capitals")
        self.mutant_fails("return name + ' (bot, ' + word + ')';", "return name + ' (bot,' + word + ')';", "the blank after the comma")
        self.mutant_fails("name.length <= LABEL_NAME_MAX", "name.length <= 17", "the longest name")
        self.mutant_fails("return p && !p.legacy ? p.name + ' (bot)' : text;", "return p ? p.name + ' (bot)' : text;", "a legacy label is shortened")
        self.mutant_fails("name.charAt(0) !== ' ' && name.charAt(name.length - 1) !== ' ' &&", "name.charAt(0) !== ' ' &&", "a blank at the end of a name")
        self.mutant_fails("\\x2a-\\x7e", "\\x28-\\x7e", "a parenthesis in a name")

    def test_the_check_notices_a_key_that_is_taken_too_easily(self):
        self.mutant_fails("if (key < 0 || key > 9007199254740991)", "if (key < -5 || key > 9007199254740991)", "a negative number is a key")
        self.mutant_fails("if (key < 0 || key > 9007199254740991)", "if (key < 0 || key > 9007199254740992)", "2^53 is a key")
        self.mutant_fails("Math.floor(key) !== key) fail(TypeError, 'a key number", "false) fail(TypeError, 'a key number", "a fractional number is a key")
        self.mutant_fails("if (!m) fail(TypeError, 'a key string must be 1 to 16 hexadecimal digits', key);", "if (!m) return [0, 0];", "a malformed string is key 0")
        self.mutant_fails("return fail(TypeError, 'a key is a hexadecimal string, a whole number or a pair [high, low]', key);", "return [0, 0];", "anything else is key 0")
        self.mutant_fails("v < 0 || v > 4294967295", "v < 0 || v > 4294967296", "a pair half above 2^32 - 1")
        self.mutant_fails("key.length === 2) {\n            var i, v;", "key.length >= 2) {\n            var i, v;", "a longer array is a pair")

    def test_the_check_notices_a_lower_case_that_is_not_ascii(self):
        self.mutant_fails("asciiLower(tag) : '';", "tag.toLowerCase() : '';", "toLowerCase on a tag (the Kelvin sign)")
        self.mutant_fails("replace(/[ \\t\\r\\n]/g, '')", "replace(/ /g, '')", "tabs and line ends are not ignored")

    def test_the_check_notices_a_list_that_breaks_a_rule(self):
        names = names_of_list()
        text = read(LIB)
        a = text.index(gen_bot_names.BEGIN)
        b = text.index(gen_bot_names.END) + len(gen_bot_names.END)

        def with_names(new):
            with tempfile.TemporaryDirectory() as folder:
                lib = os.path.join(folder, "bot_names.js")
                with open(lib, "w", encoding="utf-8", newline="") as f:
                    f.write(text[:a] + gen_bot_names.render_js_block(new) + text[b:])
                return run_check(lib)

        self.assertEqual(with_names(names)[0], 0)
        for what, new in (("a name on the blocklist", names[:-1] + ["Nazi"]), ("a name inside which a blocked word stands", names[:-1] + ["Dickens"]), ("a whole-name word", names[:-1] + ["Admin"]),
                          ("a placeholder of the page", names[:-1] + ["Maple"]), ("the same name in another case", names[:-1] + [names[0].lower()]), ("a name of nine letters", names[:-1] + ["Abcdefghi"]), ("a name that was taken out", names[:-1] + ["Warden"]), ("a name that was taken out", names[:-1] + ["Atta"]),
                          ("too few names", names[:239]), ("a name with a blank", names[:-1] + ["Ant Hill"]), ("a digit", names[:-1] + ["Ant7"])):
            code, out = with_names(new)
            self.assertNotEqual(code, 0, "the check did not notice " + what)

    def test_the_check_notices_a_changed_list_through_the_golden_vectors(self):
        names = names_of_list()
        text = read(LIB)
        a = text.index(gen_bot_names.BEGIN)
        b = text.index(gen_bot_names.END) + len(gen_bot_names.END)
        swapped = names[:]
        swapped[0], swapped[1] = swapped[1], swapped[0]
        with tempfile.TemporaryDirectory() as folder:
            lib = os.path.join(folder, "bot_names.js")
            with open(lib, "w", encoding="utf-8", newline="") as f:
                f.write(text[:a] + gen_bot_names.render_js_block(swapped) + text[b:])
            code, out = run_check(lib)
        self.assertNotEqual(code, 0)
        self.assertIn("golden", out)


if __name__ == "__main__":
    unittest.main()
