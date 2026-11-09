#!/usr/bin/env python3
"""A short random-mutation run of the native replay reader and map reader on files that someone could hand to the page that opens a replay FILE of your own (opt-in; see tests/scripts/web_replay_open_fuzz.sh
and docs/REPLAYS.md "A file of your own").

Why: the page hands the bytes of a file to the game's own reader, and a file that CARRIES its map (the chunk tmap, at most 1 MiB) makes the game's map reader parse bytes that a stranger wrote (until now only
maps that the server had vetted came that way). The game runs inside the WebAssembly sandbox, and a game that does not start gives a card (the start watchdog), but the reader should refuse every file
cleanly. This run makes files that pass the container's own checks (every checksum is made again, the head's hash is the hash of the mutated map), so that the reader and not the checksums decides:

  map        a map (the six originals and a few of the players') with 1 - 3 changes: bytes flipped, a number of 16 or 32 bits set to 0, 1, 255, 65,535, 2^31 - 1, 2^32 - 1 ..., a cut, an extension, a range zeroed,
             swapped or doubled, the first bytes (the header's sizes and counts) most often; carried as the tmap of a short recording
  head       the head's bytes changed (the checksum made again)
  commands   the bytes of a chunk of orders changed (the checksum made again)
  container  chunks cut, doubled, dropped, put in another order, or a length that claims more than the file holds

Every file goes to `replay_tool info` and `replay_tool verify` with a limit of memory and of time. A run that ends by itself with its own words (status 0 or 1) is the reader doing its job. A run that is killed by
a signal is a CRASH (SIGSEGV, SIGBUS, SIGILL, SIGFPE), one that ends in SIGABRT an ABORT (an uncaught exception or an assertion: the program is gone, memory is safe), one that outlives the time a HANG. The files of
every one are kept in --keep. Exit status 0: no crash and no hang (aborts are listed); 1: a crash or a hang; 3: the check could not be made (no replay_tool); 2: a bad command line.
"""
import argparse
import os
import random
import resource
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
MAGIC = bytes([0x89, 0x41, 0x52, 0x50, 0x4C, 0x0D, 0x0A, 0x1A])
FIXTURE = os.path.join(REPO, "tests", "data", "replay_bots_tiny.antsrep")
INTERESTING = [0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128, 255, 256, 511, 512, 1023, 1024, 4095, 4096, 32767, 32768, 65535, 65536, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF]
MAX_EMBEDDED = 1 << 20


def crc(b, seed=0):
    return zlib.crc32(b, seed) & 0xFFFFFFFF


def fnv64(b):
    h = 0xCBF29CE484222325
    for x in b:
        h = ((h ^ x) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def parse(data):
    """[(tag, payload)] of a whole file (it must be one that this script made or the fixture)"""
    out, pos = [], len(MAGIC)
    while pos + 12 <= len(data):
        tag, n = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        out.append((tag, data[pos + 8:pos + 8 + n]))
        pos += 12 + n
    return out


def frame(tag, payload, bad_crc=False, length=None):
    c = crc(payload, crc(tag))
    return tag + struct.pack("<I", len(payload) if length is None else length) + payload + struct.pack("<I", (c ^ 1) if bad_crc else c)


def build(chunks):
    return MAGIC + b"".join(frame(t, p) for t, p in chunks)


def varint_read(buf, pos):
    v, shift = 0, 0
    while True:
        b = buf[pos]
        pos += 1
        v |= (b & 127) << shift
        if not b & 128:
            return v, pos
        shift += 7


def varint_write(n):
    out = bytearray()
    while n >= 128:
        out.append((n & 127) | 128)
        n >>= 7
    out.append(n)
    return bytes(out)


def head_fields(payload):
    pos, fields = 4, []
    while pos < len(payload):
        i, pos = varint_read(payload, pos)
        n, pos = varint_read(payload, pos)
        fields.append((i, payload[pos:pos + n]))
        pos += n
    return payload[:4], fields


def head_bytes(prefix, fields):
    return prefix + b"".join(varint_write(i) + varint_write(len(b)) + b for i, b in fields)


def with_map(base, name, map_bytes):
    """the recording `base` (chunks) as if played on `map_bytes` under `name`, carrying them as its tmap chunk"""
    prefix, fields = head_fields(base[0][1])
    fields = [(i, b) for i, b in fields if i not in (3, 4)] + [(3, name.encode()), (4, struct.pack("<Q", fnv64(map_bytes)))]
    fields.sort(key=lambda f: f[0])
    return [(b"HEAD", head_bytes(prefix, fields)), (b"tmap", map_bytes)] + base[1:]


def mutate_bytes(rng, b, header_bias=True):
    """1 - 3 changes of a byte string"""
    b = bytearray(b)
    for _ in range(rng.randint(1, 3)):
        if not b:
            b = bytearray(rng.randbytes(rng.randint(1, 64)))
        kind = rng.choice(["flip", "flip", "set32", "set16", "set8", "cut", "extend", "zero", "swap", "double"])
        near = lambda: min(len(b) - 1, rng.randrange(0, 256)) if header_bias and rng.random() < 0.6 else rng.randrange(0, len(b))
        if kind == "flip":
            for _ in range(rng.randint(1, 12)):
                at = near()
                b[at] ^= 1 << rng.randrange(8)
        elif kind in ("set32", "set16", "set8"):
            width = {"set32": 4, "set16": 2, "set8": 1}[kind]
            at = near()
            v = rng.choice(INTERESTING) if rng.random() < 0.8 else rng.getrandbits(8 * width)
            b[at:at + width] = (v & ((1 << (8 * width)) - 1)).to_bytes(width, "little")
        elif kind == "cut":
            b = b[:max(1, near())]
        elif kind == "extend":
            b += rng.randbytes(rng.randint(1, 4096))
        elif kind == "zero":
            at = near()
            n = rng.randint(1, 512)
            b[at:at + n] = bytes(len(b[at:at + n]))
        elif kind == "swap" and len(b) > 64:
            a, c = rng.randrange(0, len(b) - 32), rng.randrange(0, len(b) - 32)
            n = rng.randint(1, 32)
            b[a:a + n], b[c:c + n] = b[c:c + n], b[a:a + n]
        elif kind == "double":
            at = near()
            n = rng.randint(1, 256)
            b[at:at] = b[at:at + n]
    return bytes(b[:MAX_EMBEDDED])


def make_files(rng, fixture_chunks, maps):
    """one file for each strategy and draw: yields (strategy, bytes)"""
    short = [fixture_chunks[0], fixture_chunks[1]]                                            # the head and 30 seconds of orders: the match plays a moment, the map is the point
    while True:
        s = rng.choice(["map", "map", "map", "head", "commands", "container"])
        if s == "map":
            name, data = rng.choice(maps)
            yield s, build(with_map(short, "Fuzz.lvl", mutate_bytes(rng, data)))
        elif s == "head":
            prefix, fields = head_fields(fixture_chunks[0][1])
            payload = mutate_bytes(rng, head_bytes(prefix, fields), header_bias=False)[:4096]
            yield s, build([(b"HEAD", payload)] + short[1:])
        elif s == "commands":
            chunks = [list(c) for c in fixture_chunks[:6]]
            at = rng.randrange(1, len(chunks))
            chunks[at][1] = mutate_bytes(rng, chunks[at][1], header_bias=False)
            yield s, build([tuple(c) for c in chunks])
        else:
            chunks = list(fixture_chunks)
            for _ in range(rng.randint(1, 3)):
                k = rng.choice(["drop", "double", "swap", "claim", "cut", "badcrc"])
                if k == "drop" and len(chunks) > 2:
                    del chunks[rng.randrange(0, len(chunks))]
                elif k == "double":
                    at = rng.randrange(0, len(chunks))
                    chunks.insert(at, chunks[at])
                elif k == "swap" and len(chunks) > 2:
                    a, c = rng.randrange(0, len(chunks)), rng.randrange(0, len(chunks))
                    chunks[a], chunks[c] = chunks[c], chunks[a]
            data = MAGIC + b"".join(frame(t, p, bad_crc=(rng.random() < 0.05), length=(rng.choice(INTERESTING) if rng.random() < 0.08 else None)) for t, p in chunks)
            if rng.random() < 0.3:
                data = data[:rng.randrange(8, len(data))]
            yield s, data


def run(tool, args, timeout, limit_bytes):
    def limits():
        if limit_bytes:                                                                      # (a sanitizer needs the whole address space: --memory-mb 0)
            resource.setrlimit(resource.RLIMIT_AS, (limit_bytes, limit_bytes))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    started = time.time()
    try:
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:abort_on_error=0:handle_abort=0", UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1")
        done = subprocess.run([tool] + args, capture_output=True, timeout=timeout, preexec_fn=limits, env=env)
        return done.returncode, done.stdout + done.stderr, time.time() - started
    except subprocess.TimeoutExpired:
        return "hang", b"", time.time() - started


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tool", default=os.environ.get("REPLAY_TOOL", ""), help="the replay_tool program (default: build/replay_tool of this tree)")
    ap.add_argument("--maps-dir", default=os.path.join(REPO, "Original-Ants", "Maps"))
    ap.add_argument("--runs", type=int, default=300, help="how many files (default 300)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--timeout", type=float, default=30.0, help="seconds for one run of the tool")
    ap.add_argument("--memory-mb", type=int, default=3072, help="the address space that one run may take (a file that makes the reader ask for more is an abort, not an out of memory machine); 0: no limit (a sanitizer build)")
    ap.add_argument("--keep", default="", help="a folder for the files that crashed, aborted or hung")
    args = ap.parse_args()
    tool = args.tool or os.path.join(REPO, "build", "replay_tool")
    if not os.path.isfile(tool):
        print("  SKIP: no replay_tool at %s (build the target replay_tool, or name one with --tool or REPLAY_TOOL)" % tool)
        return 3
    with open(FIXTURE, "rb") as f:
        fixture = parse(f.read())
    maps = []
    for name in sorted(os.listdir(args.maps_dir)):
        path = os.path.join(args.maps_dir, name)
        if name.lower().endswith(".lvl") and os.path.getsize(path) <= MAX_EMBEDDED:
            with open(path, "rb") as f:
                maps.append((name, f.read()))
    community = os.path.join(REPO, "Community-Maps")
    if os.path.isdir(community):
        for name in sorted(os.listdir(community))[:400:57]:                                   # (a few of the players' maps, the extended format among them)
            path = os.path.join(community, name)
            if name.lower().endswith(".lvl") and 0 < os.path.getsize(path) <= MAX_EMBEDDED:
                with open(path, "rb") as f:
                    maps.append((name, f.read()))
    if not maps:
        print("  SKIP: no maps in " + args.maps_dir)
        return 3
    rng = random.Random(args.seed)
    gen = make_files(rng, fixture, maps)
    limit = args.memory_mb * 1024 * 1024 if args.memory_mb > 0 else 0
    folder = tempfile.mkdtemp(prefix="ants_fuzz.")
    counts, bad, slowest = {}, [], (0.0, "")
    print("[web replay open fuzz] %d files through %s (seed %d, %d maps as the base, %s and %.0f s for a run)" % (args.runs, os.path.basename(tool), args.seed, len(maps), ("%d MB" % args.memory_mb) if limit else "no memory limit", args.timeout))
    try:
        for i in range(args.runs):
            strategy, data = next(gen)
            path = os.path.join(folder, "f.antsrep")
            with open(path, "wb") as f:
                f.write(data)
            for verb, extra in (("info", []), ("verify", ["--maps-dir", args.maps_dir])):
                status, output, took = run(tool, [verb, path] + extra, args.timeout, limit)
                slowest = max(slowest, (took, "%s %s" % (strategy, verb)))
                if status == "hang":
                    kind = "HANG"
                elif isinstance(status, int) and status < 0:
                    kind = "ABORT" if -status == signal.SIGABRT else "CRASH (%s)" % signal.Signals(-status).name
                elif b"AddressSanitizer" in output or b"runtime error:" in output:
                    kind = "SANITIZER"
                else:
                    kind = "ok"
                key = (strategy, kind)
                counts[key] = counts.get(key, 0) + 1
                if kind != "ok":
                    bad.append((i, strategy, verb, kind, output[-300:].decode("latin-1")))
                    if args.keep:
                        os.makedirs(args.keep, exist_ok=True)
                        shutil.copy(path, os.path.join(args.keep, "%s-%04d-%s-%s.antsrep" % (strategy, i, verb, kind.split()[0].lower())))
    finally:
        shutil.rmtree(folder, ignore_errors=True)
    for (strategy, kind), n in sorted(counts.items()):
        print("  %-10s %-18s %d" % (strategy, kind, n))
    print("  slowest run: %.2f s (%s)" % slowest)
    hard = [b for b in bad if b[3] in ("HANG", "SANITIZER") or b[3].startswith("CRASH")]
    for i, strategy, verb, kind, tail in bad[:12]:
        print("  %s file %d (%s, %s): %s" % (kind, i, strategy, verb, tail.strip().splitlines()[-1] if tail.strip() else ""))
    print("  %d runs, %d crashes or hangs, %d aborts" % (args.runs * 2, len(hard), len([b for b in bad if b[3] == "ABORT"])))
    return 1 if hard else 0


if __name__ == "__main__":
    sys.exit(main())
