"""The size numbers of documents, code files and web downloads, in one place. Data only: tools/check_hygiene.py and the tests that need a limit import it.

Documents and code files have a soft policy. A base size, and three fractions of it:
  target  (75%)   what a file aims at; only `check_hygiene.py --list` shows the files over it, and documents get a warning from here
  split   (90%)   a warning on the pull request: split the file by topic now
  cap     (125%)  the safety cap: the only fraction that fails the check
A document is measured in bytes, a code file in lines. Nothing is a ledger: no file has a number of its own, and a file is never trimmed or joined to fit (AGENTS.md, rule 10).
"""

TARGET_PERCENT = 75
SPLIT_PERCENT = 90
CAP_PERCENT = 125

# why 100 KiB: a page that a person reads in one sitting and an agent reads in one file read (about 25,000 tokens of this prose)
DOC_BASE_BYTES = 100 * 1024
# why 1,500 and 2,500: a file that one person can hold in mind; a test file is longer because it is a list of small cases
PRODUCTION_BASE_LINES = 1500
TEST_BASE_LINES = 2500
# code files warn from the split line only (from the target, about 50 files would be listed); documents warn from the target
CODE_WARNS_FROM_PERCENT = SPLIT_PERCENT

# GitHub shows at most 10 warning annotations of one step: the hygiene check prints at most this many (the worst files first)
ANNOTATION_LIMIT = 10

# why 2,000: agent readers may cut a line longer than this; a long line is a warning (reflow it in a change that moves no text)
DOC_LINE_WARN_CHARACTERS = 2000

# Files over the safety cap that wait for their split. Names only: no number. They warn instead of failing; a name fails when its file is gone or no longer over the cap,
# and each split removes its name. The set and the code that reads it are deleted when it is empty.
SPLIT_PENDING = frozenset({
    "docs/GAME_REVERSE_ENGINEERING.md",
    "docs/NETWORK_PORT.md",
    "docs/BOTS.md",
    "web/shell.html",
    "tools/map_sweep.cpp",
})
