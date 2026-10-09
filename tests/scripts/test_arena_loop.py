#!/usr/bin/env python3
"""The bot arena of the stack (docker/arena_loop.sh, the stage `arena` of Dockerfile.server, the service ants-arena of docker-compose.stack.yml; docs/BOTS.md "On the game server").

The script is run with a stand-in for bot_arena that only writes down its arguments (no build is needed), so the choices of the loop are checked: a match of two to four standard
bots on two to four of the four seats and one of the six maps, every map, seat and level coming up (no colour is always in the match or always gets the Hard bot), the same seed
giving the same match, the folder and the maps folder it passes, the exit code that ONCE returns, the clamped interval, and a stop on SIGTERM that does not wait for the end of the pause.
The Dockerfile, the stack file and the CI are read as text: the server stays the last stage of the Dockerfile (a build without
--target must make the server), the arena has no network and the security options of the server, and the CI builds the stage and plays a match with it.
"""
import os
import re
import signal
import stat
import subprocess
import tempfile
import time
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCRIPT = os.path.join(REPO, "docker", "arena_loop.sh")
MAPS = {"TINY", "SMALL", "MEDIUM", "GAUNTLET", "TREASURE", "ISLANDS"}


def read(*parts):
    with open(os.path.join(REPO, *parts), encoding="utf-8") as f:
        return f.read()


def code_lines(text):
    return [l for l in text.splitlines() if not l.lstrip().startswith("#")]


class Loop(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.record = os.path.join(self.tmp.name, "args.txt")
        self.fake = os.path.join(self.tmp.name, "bot_arena")
        self.write_fake(0)

    def write_fake(self, code):
        with open(self.fake, "w") as f:
            f.write('#!/bin/sh\nfor a in "$@"; do printf "%%s\\n" "$a"; done >> "%s"\nprintf -- "--\\n" >> "%s"\nexit %d\n' % (self.record, self.record, code))
        os.chmod(self.fake, 0o755)

    def env(self, **extra):
        env = {k: v for k, v in os.environ.items() if not k.startswith("ANTS_ARENA_")}
        env.update(ANTS_ARENA_BIN=self.fake, ANTS_ARENA_DIR="/out/replays", ANTS_ARENA_MAPS_DIR="/the/maps")
        env.update(extra)
        return env

    def once(self, seed, **extra):
        done = subprocess.run(["bash", SCRIPT], env=self.env(ANTS_ARENA_ONCE="1", ANTS_ARENA_SEED=str(seed), **extra), capture_output=True, text=True, timeout=30)
        return done

    def last_args(self):
        with open(self.record) as f:
            runs = [r for r in f.read().split("--\n") if r]
        return runs[-1].split("\n")[:-1]

    def many(self, seeds):
        """The arguments of one match for each seed: [(map, [(seat, level), ...]), ...]."""
        matches = []
        for seed in seeds:
            done = self.once(seed)
            self.assertEqual(done.returncode, 0, done.stderr)
            args = self.last_args()
            seats = args[6:-3]
            self.assertEqual(seats[0::2], ["--seat"] * (len(seats) // 2))
            matches.append((args[3], [tuple(spec.split("=standard:")) for spec in seats[1::2]]))
        return matches

    def test_the_script_is_executable_and_valid_bash(self):
        self.assertTrue(os.stat(SCRIPT).st_mode & stat.S_IXUSR, "docker/arena_loop.sh must be executable (the image runs it as its entrypoint)")
        self.assertEqual(subprocess.run(["bash", "-n", SCRIPT]).returncode, 0)

    def test_one_match_of_two_to_four_standard_bots_on_a_shipped_map(self):
        seen_maps, seen_seats = set(), set()
        for seed in range(1, 41):
            done = self.once(seed)
            self.assertEqual(done.returncode, 0, done.stderr)
            args = self.last_args()
            self.assertEqual(args[0:2], ["--maps-dir", "/the/maps"])
            self.assertEqual(args[2], "--map")
            self.assertIn(args[3], MAPS)
            self.assertEqual(args[4], "--seeds")
            self.assertRegex(args[5], r"^[1-9][0-9]{0,9}$")
            self.assertLess(int(args[5]), 2 ** 31)
            self.assertEqual(args[-3:], ["--save-replays", "/out/replays", "--quiet"])
            seats = args[6:-3]
            self.assertEqual(seats[0::2], ["--seat"] * (len(seats) // 2))
            self.assertIn(len(seats) // 2, (2, 3, 4))
            numbers = []
            for spec in seats[1::2]:
                self.assertRegex(spec, r"^[0-3]=standard:(medium|hard)$")
                numbers.append(int(spec[0]))
            self.assertEqual(numbers, sorted(set(numbers)), "each seat once, in the order of the seats")
            seen_maps.add(args[3])
            seen_seats.add(len(seats) // 2)
            self.assertIn("match: --map " + args[3] + " --seeds " + args[5], done.stdout)         # (the log says how to play it again)
        # (which maps and how many seats come up depends on the random numbers of the bash that runs the script: only that there is variety is claimed)
        self.assertGreaterEqual(len(seen_maps), 3)
        self.assertGreaterEqual(len(seen_seats), 2)

    def test_no_colour_is_always_in_the_match_or_always_gets_the_hard_bot(self):
        # (240 fixed seeds, spaced apart: in bash 4.4 and 5.0 the first number after RANDOM=s is 16807 * s mod 32768, so consecutive seeds give nearly the same map. Each claim fails with a
        # chance below one in a hundred million if the choices are fair, so what the bash of the machine draws does not matter)
        matches = self.many([1 + 271 * i for i in range(240)])
        levels = {seat: set() for seat in "0123"}
        absent = {seat: 0 for seat in "0123"}
        for _, seats in matches:
            for seat, level in seats:
                levels[seat].add(level)
            for seat in "0123":
                if seat not in [s for s, _ in seats]:
                    absent[seat] += 1
        for seat in "0123":
            self.assertEqual(levels[seat], {"medium", "hard"}, "seat %s gets both levels" % seat)
            self.assertGreater(absent[seat], 0, "seat %s is left out of some matches (Green too)" % seat)
            self.assertLess(absent[seat], len(matches), "seat %s plays in some matches" % seat)
        self.assertEqual({len(seats) for _, seats in matches}, {2, 3, 4})
        self.assertEqual({m for m, _ in matches}, MAPS)
        pairs = {tuple(s for s, _ in seats) for _, seats in matches if len(seats) == 2}
        self.assertGreaterEqual(len(pairs), 5, "the two bots of a small match sit on many different pairs of seats, not always Green and Red: %s" % sorted(pairs))
        self.assertGreaterEqual(len({tuple(level for _, level in seats) for _, seats in matches if len(seats) == 2}), 3, "the levels of two bots are not one for both: at least three of the four mixes come up")

    def test_the_same_seed_is_the_same_match_and_another_seed_another(self):
        self.once(7)
        first = self.last_args()
        self.once(7)
        self.assertEqual(self.last_args(), first)
        different = set()
        for seed in (8, 9, 10, 11):
            self.once(seed)
            different.add(tuple(self.last_args()))
        self.assertGreater(len(different), 1)

    def test_once_returns_the_exit_code_of_the_arena(self):
        self.write_fake(1)
        done = self.once(3)
        self.assertEqual(done.returncode, 1)
        self.assertIn("bot_arena exited with code 1", done.stdout)
        self.write_fake(2)
        self.assertEqual(self.once(3).returncode, 2)

    def loop(self, **extra):
        return subprocess.Popen(["bash", SCRIPT], env=self.env(ANTS_ARENA_SEED="1", **extra), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    def played(self):
        try:
            with open(self.record) as f:
                return f.read().count("--\n")
        except FileNotFoundError:
            return 0

    def stop(self, proc, matches=1):
        deadline = time.time() + 30                    # (the first match is played at once, after the trap is set: wait for the stand-in to have run, so that SIGTERM is not early)
        while self.played() < matches and time.time() < deadline:
            time.sleep(0.05)
        self.assertGreaterEqual(self.played(), matches)
        time.sleep(0.2)                                # (the loop then logs and goes to sleep: a SIGTERM within these moments is as good)
        started = time.time()
        proc.send_signal(signal.SIGTERM)
        out, _ = proc.communicate(timeout=30)
        self.assertLess(time.time() - started, 15, "SIGTERM must end the pause, not wait for it")
        self.assertEqual(proc.returncode, 0)
        self.assertIn("arena: stopped", out)
        return out

    def test_the_loop_plays_at_once_then_waits_and_stops_on_sigterm(self):
        proc = self.loop(ANTS_ARENA_EVERY_MIN="60")
        out = self.stop(proc)
        self.assertIn("starting: a match every 60 minutes into /out/replays (random choices from seed 1)", out)
        self.assertIn("match done", out)
        with open(self.record) as f:
            self.assertEqual(f.read().count("--\n"), 1)                      # (one match, the next one in an hour)

    def test_the_interval_is_clamped_and_never_ends_the_container(self):
        for given, used, why in (("1", 5, "less than 5"), ("0", 5, "less than 5"), ("99999", 1440, "more than 1440"), ("007", 7, None), ("abc", 60, "not a whole number"), ("", 60, None),
                                 ("-5", 60, "not a whole number"), ("2.5", 60, "not a whole number"),
                                 ("99999999999999999999", 1440, "more than 1440"), ("9223372036854775808", 1440, "more than 1440")):
            self.write_fake(0)
            before = self.played()
            out = self.stop(self.loop(ANTS_ARENA_EVERY_MIN=given), matches=before + 1)
            self.assertIn("a match every %d minutes" % used, out, given)
            if why:
                self.assertIn(why, out, given)

    def test_a_failing_arena_does_not_stop_the_loop(self):
        self.write_fake(1)
        out = self.stop(self.loop(ANTS_ARENA_EVERY_MIN="60"))
        self.assertIn("bot_arena exited with code 1", out)

    def test_a_seed_that_is_not_a_number_takes_the_clock(self):
        for bad in ("x1", "99999999999999999999"):
            done = self.once(bad)
            self.assertEqual(done.returncode, 0)
            self.assertIn("ANTS_ARENA_SEED='%s' is not a whole number" % bad, done.stdout)


class BuiltFromTheRepository(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dockerfile = read("Dockerfile.server")
        cls.stack = read("docker-compose.stack.yml")
        cls.ci = read(".github", "workflows", "ci.yml")

    def test_the_server_stays_the_last_stage_and_the_arena_stands_before_it(self):
        stages = re.findall(r"(?m)^FROM\s+\S+\s+AS\s+(\S+)\s*$", self.dockerfile)
        self.assertEqual(stages[-1], "runner", "a build without --target must make the game server")
        self.assertEqual(stages, ["builder", "arena-builder", "arena", "runner"])

    def test_the_arena_image_is_small_and_runs_the_loop_as_an_unprivileged_user(self):
        block = self.dockerfile.split("AS arena\n", 1)[1].split("AS runner", 1)[0]
        self.assertIn("COPY --from=arena-builder /src/build/bot_arena /usr/local/bin/bot_arena", block)
        self.assertIn("COPY docker/arena_loop.sh /usr/local/bin/arena_loop.sh", block)
        self.assertIn("USER ants", block)
        self.assertIn('ENTRYPOINT ["/usr/local/bin/arena_loop.sh"]', block)
        self.assertNotIn("EXPOSE", block)
        useradd = lambda text: re.search(r"RUN useradd [^\n]*\\\n\s+mkdir[^\n]*", text).group(0)
        self.assertEqual(useradd(block), useradd(self.dockerfile.split("AS runner", 1)[1]), "the same user in both images: they share the volume of the results")

    def test_the_header_that_the_arena_needs_is_a_stand_in_in_the_image_and_the_names_are_those_of_the_real_one(self):
        # the tests folder is not in the build context (.dockerignore): the image writes a header with the three names that the real one has
        self.assertIn("tests\n", read(".dockerignore"))
        self.assertNotIn("tests/common", read(".dockerignore"))
        real = read("tests", "common", "ants_test_paths.hpp")
        for name in re.findall(r'#define (\w+) "/nonexistent"', self.dockerfile):
            self.assertIn("#ifndef " + name, real)
        self.assertEqual(len(re.findall(r'#define \w+ "/nonexistent"', self.dockerfile)), len(re.findall(r"#ifndef \w+", real)))

    def service(self):
        match = re.search(r"(?ms)^  ants-arena:\n(.*?)(?=^  \S|^\S)", self.stack)
        self.assertTrue(match, "docker-compose.stack.yml has no service ants-arena")
        return "\n".join(code_lines(match.group(1)))

    def test_the_arena_service_has_no_network_no_ports_and_the_security_options_of_the_server(self):
        service = self.service()
        for needle in ("target: arena", "dockerfile: Dockerfile.server", "network_mode: none", "read_only: true", "cap_drop: [ALL]", 'security_opt: ["no-new-privileges:true"]',
                       "pids_limit: 32", "mem_limit: 256m", "cpus: 0.5", "restart: unless-stopped", "- ants-server"):
            self.assertIn(needle, service)
        self.assertNotIn("ports:", service)
        self.assertNotIn("networks:", service)
        self.assertNotIn("ANTS_SERVER_SECRET", service)

    def test_the_arena_writes_where_the_server_keeps_its_replays(self):
        service = self.service()
        self.assertIn("- ants-server-results:/results", service)
        self.assertIn(":/maps:ro", service)
        self.assertIn("ANTS_ARENA_EVERY_MIN: ${ANTS_ARENA_EVERY_MIN:-60}", service)
        # the script's default folder is the server's (/results/replays: --results-dir /results of the server's entrypoint, and its folder "replays")
        self.assertIn("ANTS_ARENA_DIR:-/results/replays", read("docker", "arena_loop.sh"))
        self.assertIn("--results-dir\", \"/results\"", self.dockerfile)

    def test_the_variable_is_documented_in_the_header_of_the_stack(self):
        self.assertRegex(self.stack, r"(?m)^#   ANTS_ARENA_EVERY_MIN=60 ")

    def test_the_arena_is_not_started_by_default_because_the_server_plays_its_own_matches(self):
        # the headless arena plays its match in a second and nobody can watch it live: the game server's own match (--bot-match-every-min) replaces it, and the service waits behind a profile
        self.assertIn('profiles: ["arena"]', self.service())
        self.assertRegex(self.stack, r'"--bot-match-every-min", "\$\{ANTS_BOT_MATCH_EVERY_MIN:-60\}"')
        self.assertRegex(self.stack, r"(?m)^#   ANTS_BOT_MATCH_EVERY_MIN=60 ")
        self.assertIn("`ANTS_BOT_MATCH_EVERY_MIN`", read("docs", "SERVER.md"))

    def test_the_ci_builds_the_stage_and_plays_one_match_with_it(self):
        for needle in ("docker build -f Dockerfile.server --target arena -t ants-arena .", "ANTS_ARENA_ONCE=1", "ANTS_ARENA_SEED=1", "*.antsrep"):
            self.assertIn(needle, self.ci)


if __name__ == "__main__":
    unittest.main()
