#!/usr/bin/env python3
"""No C++ source uses a GCC/Clang builtin (run by ./run_tests.sh --fast and by the CI).

MSVC has no `__builtin_popcount` and its kin, and both Windows jobs build every test with -DANTS_WERROR=ON, so one such call in a test breaks the Windows build while the
Linux and macOS builds pass. The standard has what is needed (`std::bitset<N>::count()`, a loop). This reads the sources of the program, the libraries, the tests and the tools;
the vendored dr_mp3.h guards its own use of a builtin with the compiler's macros and is left out.
"""
import os
import re
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FOLDERS = ["src", "include", "tests", "tools"]
EXTENSIONS = (".cpp", ".hpp", ".h", ".cc", ".c", ".mm")
VENDORED = {"include/ants_app/dr_mp3.h"}
BUILTIN = re.compile(r"\b__builtin_\w+")


def sources():
    for folder in FOLDERS:
        for root, dirs, files in os.walk(os.path.join(REPO, folder)):
            dirs[:] = [d for d in dirs if d not in ("build", "__pycache__")]
            for name in sorted(files):
                path = os.path.join(root, name)
                relative = os.path.relpath(path, REPO).replace(os.sep, "/")
                if name.endswith(EXTENSIONS) and relative not in VENDORED:
                    yield relative, path


def builtins_used(text):
    """[(line number, the builtin)] of the code: a comment's mention is not a use."""
    found = []
    in_block_comment = False
    for number, line in enumerate(text.splitlines(), 1):
        code = line
        if in_block_comment:
            end = code.find("*/")
            if end < 0:
                continue
            code = code[end + 2:]
            in_block_comment = False
        while True:
            start = code.find("/*")
            if start < 0:
                break
            end = code.find("*/", start + 2)
            if end < 0:
                code = code[:start]
                in_block_comment = True
                break
            code = code[:start] + " " + code[end + 2:]
        code = code.split("//", 1)[0]
        found += [(number, m.group(0)) for m in BUILTIN.finditer(code)]
    return found


class NoCompilerBuiltins(unittest.TestCase):
    def test_the_sources_are_found(self):
        names = [relative for relative, _ in sources()]
        self.assertIn("tests/test_net/test_lockstep.cpp", names)
        self.assertIn("src/ants_server/room.cpp", names)
        self.assertNotIn("include/ants_app/dr_mp3.h", names)

    def test_no_source_uses_a_builtin(self):
        used = []
        for relative, path in sources():
            with open(path, encoding="utf-8", errors="replace") as f:
                used += ["%s:%d %s" % (relative, number, name) for number, name in builtins_used(f.read())]
        self.assertEqual(used, [], "MSVC has no such builtin: use the standard library (std::bitset<N>::count(), <algorithm>) or a loop")

    def test_what_counts_as_a_use(self):
        self.assertEqual(builtins_used("int n = __builtin_popcount(x);"), [(1, "__builtin_popcount")])
        self.assertEqual(builtins_used("if (__builtin_expect(a, 1)) {}\nreturn __builtin_clz(b);"), [(1, "__builtin_expect"), (2, "__builtin_clz")])
        self.assertEqual(builtins_used("// __builtin_popcount is not used\nint a;"), [])
        self.assertEqual(builtins_used("int a; /* __builtin_ctz */ int b;"), [])
        self.assertEqual(builtins_used("/* one\n __builtin_ctz\n two */ int b = __builtin_ffs(1);"), [(3, "__builtin_ffs")])
        self.assertEqual(builtins_used("int my__builtin_x = 1;"), [])


if __name__ == "__main__":
    unittest.main()
