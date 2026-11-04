#!/usr/bin/env python3
"""A test that fails on purpose (throwaway probe, never merged): it is a page-test file by its name, so the pull request that adds it gets the light checks, and the python slice that holds it
must fail, and with it the job behind the check "Linux (GCC)"."""
import unittest


class TheProbe(unittest.TestCase):
    def test_this_fails_on_purpose(self):
        self.fail("probe: this test fails on purpose, to see that the check Linux (GCC) fails when one python slice fails")


if __name__ == "__main__":
    unittest.main()
