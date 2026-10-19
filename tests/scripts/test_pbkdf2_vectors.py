#!/usr/bin/env python3
"""The server's own PBKDF2-HMAC-SHA-256 (src/ants_server/crypto.cpp) against an independent program (Python's hashlib), so that a regression of the password hash is caught:

  - tests/data/pbkdf2_vectors.json (the answers that test_admin_accounts holds the C++ loop to) is made again with hashlib.pbkdf2_hmac and must be the same;
  - with the real ants_server built, the first account is made with the command line and the hash that it stored is made again with hashlib from the password that was printed (the format
    pbkdf2-sha256$rounds$salt$hash, the rounds of docs/MAP_SUBMISSIONS.md, the salt used).
"""
import base64
import hashlib
import json
import os
import re
import sqlite3
import subprocess
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SERVER = os.path.join(REPO, os.environ.get("BUILD_DIR", "build"), "src", "ants_server", "ants_server")


def unb64(text):
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


class Pbkdf2(unittest.TestCase):
    def test_the_vectors_are_what_hashlib_says(self):
        with open(os.path.join(REPO, "tests", "data", "pbkdf2_vectors.json"), encoding="utf-8") as f:
            vectors = json.load(f)["vectors"]
        self.assertGreaterEqual(len(vectors), 3)
        self.assertTrue(any(v["length"] > 32 for v in vectors))                          # (an output of more than one block)
        for v in vectors:
            got = hashlib.pbkdf2_hmac("sha256", bytes.fromhex(v["password"]), bytes.fromhex(v["salt"]), v["rounds"], v["length"]).hex()
            self.assertEqual(got, v["hex"], v)

    @unittest.skipUnless(os.path.exists(SERVER), "ants_server is not built: the stored hash was NOT checked against hashlib")
    def test_the_hash_that_the_server_stores_is_the_one_hashlib_makes(self):
        with tempfile.TemporaryDirectory() as work:
            done = subprocess.run([SERVER, "--admin-create", "owner", "--results-dir", work], capture_output=True, text=True, timeout=120)
            self.assertEqual(done.returncode, 0, done.stderr)
            password = re.search(r"Starting password: (\S{20})\n", done.stdout).group(1)
            stored = sqlite3.connect(os.path.join(work, "ants.db")).execute("select pw_hash from admin_users").fetchone()[0]
            scheme, rounds, salt, key = stored.split("$")
            self.assertEqual((scheme, rounds), ("pbkdf2-sha256", "600000"))
            self.assertEqual(hashlib.pbkdf2_hmac("sha256", password.encode(), unb64(salt), int(rounds), 32), unb64(key))


if __name__ == "__main__":
    unittest.main()
