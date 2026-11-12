"""Which mutants of a list one unit of the python tests tries (used by the mutation classes that tests/scripts/run_python_tests.py runs as several units; not a test file).

The variable ANTS_MUTANT_SHARD is "K/N" (K from 1) for the K-th of N units: it tries the mutants at the positions K-1, K-1+N, K-1+2N, ... of the list, in the order of the list. Unset or empty it is
"1/1": every mutant. Anything else is an error and never a smaller list: a unit that tried fewer mutants than it should would pass without having looked. test_run_python_tests checks that the
units together are the whole list, each mutant once, for every list of the two classes.
"""
import os
import re

ENV = "ANTS_MUTANT_SHARD"


def parse(value):
    """(K, N) of "K/N"; (1, 1) for None or an empty text; ValueError for anything else"""
    if value is None or value == "":
        return 1, 1
    picked = re.fullmatch(r"(\d+)/(\d+)", value)
    if not picked or not 1 <= int(picked.group(1)) <= int(picked.group(2)):
        raise ValueError("%s=%r is not K/N with 1 <= K <= N" % (ENV, value))
    return int(picked.group(1)), int(picked.group(2))


def pick(items, shard):
    """the items that unit K of N tries (shard is (K, N)), in the order of the list"""
    k, n = shard
    return [item for i, item in enumerate(items) if i % n == k - 1]


def from_environment(items, environ=None):
    """the items that this process tries, by the variable ENV"""
    chosen = pick(items, parse((os.environ if environ is None else environ).get(ENV)))
    if items and not chosen:
        raise ValueError("%s: this unit would try none of the %d mutants" % (ENV, len(items)))
    return chosen
