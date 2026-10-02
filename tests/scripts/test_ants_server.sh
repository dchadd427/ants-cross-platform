#!/usr/bin/env bash
# The dedicated server with REAL programs: ants_server (TCP game port, control interface with a bearer secret) and two headless game clients that join a room by its
# code. Checks: the control interface refuses a missing or a wrong secret, makes a room with the right one, the two clients join, the match starts by itself (nobody
# presses START), runs, and both clients finish their frames without an error; a room for four whose leader (the first client to join, --start-when 2: a test hook that
# presses START for a headless client) starts it with the two players who are there; a raw client (no game) that floods the server with valid messages (StartRequests, Pings) is
# dropped within seconds while a match in another room keeps its clock, the control interface answers, and the process neither grows nor stays busy; the server stops cleanly on
# SIGTERM and writes the result file of a room that was closed. The last section is the server that HOLDS the seat of a player whose connection is lost (protocol 10, --reconnect):
# a small TCP proxy (flaky_proxy.py) between a client and the server is cut, the room pauses and names the absent seat, nothing runs while it waits, and at the cap the seat is dropped
# and the match goes on; a client that is stopped (kill -STOP) for 15 s pauses the room after 10 s of silence and finds its link closed when it wakes up.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${BUILD_DIR:-build}"
SERVER="$ROOT/$BUILD/src/ants_server/ants_server"
GAME="$ROOT/$BUILD/src/ants_app/ants"
FAILS=0
CHECKS=0
check() {      # check "what" exit-status
    CHECKS=$((CHECKS + 1))
    if [ "$2" -ne 0 ]; then
        FAILS=$((FAILS + 1))
        echo "  FAIL: $1"
    fi
}
echo "[server e2e] ants_server and two headless clients: control secret, a room by code, an automatic start"
if [ ! -x "$SERVER" ] || [ ! -x "$GAME" ]; then
    echo "  SKIP: the binaries are not built ($SERVER, $GAME)"
    exit 0
fi
if ! command -v curl > /dev/null 2>&1; then
    echo "  SKIP: curl is not installed"
    exit 0
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/ants_e2e.XXXXXX")"
cleanup() {
    [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2> /dev/null
    for p in $CLIENT_PIDS $VICTIM_PIDS $LEAD_PIDS $RC_PIDS; do kill -CONT "$p" 2> /dev/null; kill "$p" 2> /dev/null; done
    [ -n "$PROXY_PID" ] && kill "$PROXY_PID" 2> /dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT
free_port() { python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()'; }
GAME_PORT="$(free_port)"
CTL_PORT="$(free_port)"
SECRET="e2e-$RANDOM-$RANDOM-secret"
CODE="E2E-ROOM-$RANDOM"
CTL="http://127.0.0.1:$CTL_PORT"

cd "$ROOT"
# the Play online page tells the players what the room's leader can do (protocol 7), in its setup hint, its join hint and the line under the room's title
check "web/four.html says that the first player in the room can start early with START once at least 2 players are in (setup, join and room hints)" "$([ "$(grep -c 'first player in the room can start' "$ROOT/web/four.html")" -ge 3 ]; echo $?)"
check "web/four.html tells what happens to a hidden or covered window (the match does not wait for it, a lagging notice after 3 s, dropped after 30 s without a sign of life, cannot come back) and no longer says that the match waits for it (it did not since v0.0.94)" "$([ "$(grep -c 'The match does not wait for it' "$ROOT/web/four.html")" -eq 1 ] && grep -q 'dropped from the match and cannot come back' "$ROOT/web/four.html" && ! grep -q 'and the match waits for it' "$ROOT/web/four.html"; echo $?)"
# AGENTS.md rule 6: the game files of the beta site (.wasm, .data, .html, .css, .js) are revalidated, not stored away and not downloaded again: exactly Cache-Control "no-cache, must-revalidate",
# ETags on (no `etag off`), no `no-store` (a response that may not be stored cannot be revalidated: every reload fetched 9 MB) and no `expires -1` (nginx would add a second Cache-Control
# line and an Expires date); the cross-origin headers stay in each of the three blocks. (A real answer from the built web image is checked by hand: curl -I twice, the second with If-None-Match: 304.)
nginx_blocks_ok() {
    python3 - "$ROOT/docker/nginx.conf" <<'PY'
import re, sys
text = open(sys.argv[1], encoding='utf-8').read()
problems = []
for name, pattern in (('.wasm', r'location ~\* \\\.wasm\$ \{'), ('.data', r'location ~\* \\\.data\$ \{'), ('.html/.css/.js', r'location ~\* \\\.\(html\|css\|js\)\$ \{')):
    m = re.search(pattern, text)
    if not m:
        problems.append(name + ': no block')
        continue
    end = text.index('\n    }', m.end())
    block = '\n'.join(l for l in text[m.end():end].split('\n') if not l.strip().startswith('#'))
    if block.count('add_header Cache-Control') != 1 or 'add_header Cache-Control "no-cache, must-revalidate" always;' not in block:
        problems.append(name + ': Cache-Control is not exactly "no-cache, must-revalidate"')
    for bad in ('etag off', 'no-store', 'expires'):
        if bad in block:
            problems.append(name + ': has ' + bad)
    for needed in ('Cross-Origin-Opener-Policy', 'Cross-Origin-Embedder-Policy'):
        if needed not in block:
            problems.append(name + ': lost ' + needed)
print('; '.join(problems) if problems else 'ok')
PY
}
check "docker/nginx.conf: .wasm, .data and .html / .css / .js carry exactly 'Cache-Control: no-cache, must-revalidate' with ETags on (no etag off, no no-store, no expires -1) and keep COOP / COEP: $(nginx_blocks_ok)" "$([ "$(nginx_blocks_ok)" = "ok" ]; echo $?)"
# the command line: a bad demo-room option is refused at once with status 2 (a server that did start would be stopped by the alarm: status 142)
exit_of() { perl -e 'alarm 5; exec @ARGV' env ANTS_SERVER_SECRET=x "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$(free_port)" "$@" > /dev/null 2>&1; echo $?; }
check "--demo-rooms without --demo-map is refused" "$([ "$(exit_of --demo-rooms 2)" = "2" ]; echo $?)"
check "--demo-rooms with a map that is not in the folder is refused" "$([ "$(exit_of --demo-rooms 2 --demo-map NO-SUCH-MAP.LVL)" = "2" ]; echo $?)"
check "--demo-rooms 0 is refused" "$([ "$(exit_of --demo-rooms 0 --demo-map TINY.LVL)" = "2" ]; echo $?)"
check "--demo-rooms that is no number is refused" "$([ "$(exit_of --demo-rooms many --demo-map TINY.LVL)" = "2" ]; echo $?)"
check "--demo-rooms as many as --max-rooms is refused (rooms of the control interface keep their places)" "$([ "$(exit_of --demo-rooms 5 --max-rooms 5 --demo-map TINY.LVL)" = "2" ]; echo $?)"
check "--demo-maps without --demo-rooms is refused" "$([ "$(exit_of --demo-maps TINY.LVL)" = "2" ]; echo $?)"
check "--demo-maps with a map that is not in the folder is refused" "$([ "$(exit_of --demo-rooms 2 --demo-map TINY.LVL --demo-maps TINY.LVL,NO-SUCH-MAP.LVL)" = "2" ]; echo $?)"
check "--demo-maps with an empty name is refused" "$([ "$(exit_of --demo-rooms 2 --demo-map TINY.LVL --demo-maps TINY.LVL,,SMALL.LVL)" = "2" ]; echo $?)"
check "--demo-maps with a good list starts the server (the alarm ends it: status 142)" "$([ "$(exit_of --demo-rooms 2 --demo-map TINY.LVL --demo-maps TINY.LVL,SMALL.LVL)" = "142" ]; echo $?)"
check "--demo-maps drops blanks around the names (\"TINY.LVL, SMALL.LVL\" starts the server)" "$([ "$(exit_of --demo-rooms 2 --demo-map TINY.LVL --demo-maps 'TINY.LVL, SMALL.LVL ')" = "142" ]; echo $?)"
check "--hold-vote-seconds below 5 is refused" "$([ "$(exit_of --hold-vote-seconds 4)" = "2" ]; echo $?)"
check "--hold-vote-seconds above 3600 is refused" "$([ "$(exit_of --hold-vote-seconds 3601)" = "2" ]; echo $?)"
check "--hold-vote-seconds that is no number is refused" "$([ "$(exit_of --hold-vote-seconds soon)" = "2" ]; echo $?)"
check "--max-pause-seconds below 60 is refused" "$([ "$(exit_of --max-pause-seconds 59)" = "2" ]; echo $?)"
check "--max-pause-seconds above 86400 is refused" "$([ "$(exit_of --max-pause-seconds 86401)" = "2" ]; echo $?)"
check "--max-pause-seconds that is no number is refused" "$([ "$(exit_of --max-pause-seconds 30min)" = "2" ]; echo $?)"
check "--max-catch-up-seconds below 10 is refused" "$([ "$(exit_of --max-catch-up-seconds 9)" = "2" ]; echo $?)"
check "--max-catch-up-seconds above 3600 is refused" "$([ "$(exit_of --max-catch-up-seconds 3601)" = "2" ]; echo $?)"
check "--max-catch-up-seconds that is no number is refused" "$([ "$(exit_of --max-catch-up-seconds 5min)" = "2" ]; echo $?)"
check "--resume-countdown-seconds above 60 is refused" "$([ "$(exit_of --resume-countdown-seconds 61)" = "2" ]; echo $?)"
check "--resume-countdown-seconds below 0 is refused" "$([ "$(exit_of --resume-countdown-seconds -1)" = "2" ]; echo $?)"
check "--resume-countdown-seconds that is no number is refused" "$([ "$(exit_of --resume-countdown-seconds ten)" = "2" ]; echo $?)"
check "--resume-countdown-seconds without a value is refused" "$([ "$(exit_of --resume-countdown-seconds)" = "2" ]; echo $?)"
check "--log-mb 0 is refused" "$([ "$(exit_of --log-mb 0)" = "2" ]; echo $?)"
check "--log-mb above 256 is refused" "$([ "$(exit_of --log-mb 257)" = "2" ]; echo $?)"
check "--log-mb that is no number is refused" "$([ "$(exit_of --log-mb big)" = "2" ]; echo $?)"
check "--hold-vote-seconds without a value is refused" "$([ "$(exit_of --hold-vote-seconds)" = "2" ]; echo $?)"
check "good reconnect options start the server (the alarm ends it: status 142)" "$([ "$(exit_of --reconnect --hold-vote-seconds 5 --max-pause-seconds 86400 --log-mb 256)" = "142" ]; echo $?)"
check "the edges of the other side start it too" "$([ "$(exit_of --no-reconnect --hold-vote-seconds 3600 --max-pause-seconds 60 --log-mb 1)" = "142" ]; echo $?)"
check "the new options' edges start it: 10 and 0" "$([ "$(exit_of --reconnect --max-catch-up-seconds 10 --resume-countdown-seconds 0)" = "142" ]; echo $?)"
check "the new options' edges start it: 3600 and 60" "$([ "$(exit_of --reconnect --max-catch-up-seconds 3600 --resume-countdown-seconds 60)" = "142" ]; echo $?)"
check "--help names the reconnect options and their ranges" "$("$SERVER" --help 2>&1 | grep -q -- '--reconnect | --no-reconnect' && "$SERVER" --help 2>&1 | grep -q -- '--hold-vote-seconds 5-3600' && "$SERVER" --help 2>&1 | grep -q -- '--max-pause-seconds 60-86400' && "$SERVER" --help 2>&1 | grep -q -- '--max-catch-up-seconds 10-3600' && "$SERVER" --help 2>&1 | grep -q -- '--resume-countdown-seconds 0-60' && "$SERVER" --help 2>&1 | grep -q -- '--log-mb 1-256'; echo $?)"
mkdir -p "$WORK/maps_odd"
cp "$ROOT/Original-Ants/Maps/TINY.LVL" "$WORK/maps_odd/TINY.LVL"
cp "$ROOT/Original-Ants/Maps/TINY.LVL" "$WORK/maps_odd/A B.LVL"
env ANTS_SERVER_SECRET=x perl -e 'alarm 3; exec @ARGV' "$SERVER" --maps "$WORK/maps_odd" --port "$(free_port)" --demo-rooms 2 --demo-map TINY.LVL --demo-maps "TINY.LVL,A B.LVL" > "$WORK/odd.log" 2>&1
ODD_STATUS=$?
check "a listed map that no room code can name is a warning at startup, not an error" "$([ "$ODD_STATUS" = "142" ] && grep -q "'A B.LVL' can never be chosen" "$WORK/odd.log" && ! grep -q "'TINY.LVL' can never be chosen" "$WORK/odd.log"; echo $?)"

ANTS_SERVER_SECRET="$SECRET" "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$GAME_PORT" --ctl-port "$CTL_PORT" --results-dir "$WORK/results" > "$WORK/server.log" 2>&1 &
SERVER_PID=$!
UP=1
for _ in $(seq 1 50); do
    if curl -s -m 1 "$CTL/healthz" | grep -q '"ok"'; then UP=0; break; fi
    kill -0 "$SERVER_PID" 2> /dev/null || break
    sleep 0.1
done
check "the server is up and answers /healthz without a secret" "$UP"
[ "$UP" -ne 0 ] && { cat "$WORK/server.log"; echo "server e2e: $CHECKS checks, $FAILS failures"; exit 1; }

# the secret
code_of() { curl -s -m 3 -o /dev/null -w '%{http_code}' "$@"; }
check "no secret: 401" "$([ "$(code_of "$CTL/rooms")" = "401" ]; echo $?)"
check "a wrong secret: 401" "$([ "$(code_of -H "Authorization: Bearer wrong-$SECRET" "$CTL/rooms")" = "401" ]; echo $?)"
check "the secret with a character missing: 401" "$([ "$(code_of -H "Authorization: Bearer ${SECRET%?}" "$CTL/rooms")" = "401" ]; echo $?)"
check "the right secret lists the rooms: 200" "$([ "$(code_of -H "Authorization: Bearer $SECRET" "$CTL/rooms")" = "200" ]; echo $?)"
check "a room cannot be made without the secret: 401" "$([ "$(code_of -X POST -d '{"map":"TINY.LVL"}' "$CTL/rooms")" = "401" ]; echo $?)"

# the room
RESP="$(curl -s -m 3 -X POST -H "Authorization: Bearer $SECRET" -d "{\"map\":\"TINY.LVL\",\"players\":2,\"code\":\"$CODE\",\"seed\":99}" "$CTL/rooms")"
check "the room is made and is waiting" "$(echo "$RESP" | grep -q '"state":"waiting"'; echo $?)"
check "a map outside the folder is refused: 404" "$([ "$(code_of -X POST -H "Authorization: Bearer $SECRET" -d '{"map":"../../etc/passwd"}' "$CTL/rooms")" = "404" ]; echo $?)"

# two real clients (headless: they run a number of frames, then end)
CLIENT_PIDS=""
for i in 1 2; do
    "$GAME" --headless --no-lan --name "Player$i" --join "127.0.0.1:$GAME_PORT" --room "$CODE" --screenshot "$WORK/c$i.png" --frames 4000 > "$WORK/c$i.log" 2>&1 &
    CLIENT_PIDS="$CLIENT_PIDS $!"
done
RUNNING=1
for _ in $(seq 1 150); do
    STATUS="$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "$CTL/rooms/$CODE")"
    if echo "$STATUS" | grep -q '"state":"running"'; then RUNNING=0; break; fi
    sleep 0.2
done
check "both clients joined and the match started by itself (state running)" "$RUNNING"
check "both players are in the room list" "$(echo "$STATUS" | grep -q 'Player1' && echo "$STATUS" | grep -q 'Player2'; echo $?)"
sleep 1
TICKS="$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "$CTL/rooms/$CODE" | python3 -c 'import sys, json; print(json.load(sys.stdin).get("ticks", 0))')"
check "the referee's clock runs" "$([ "${TICKS:-0}" -gt 0 ]; echo $?)"
for p in $CLIENT_PIDS; do wait "$p" 2> /dev/null; done
check "no client reported an error" "$(grep -qiE 'out of sync|failed|error' "$WORK/c1.log" "$WORK/c2.log"; [ $? -ne 0 ]; echo $?)"

# the leader of a room (protocol 7): a room for four, two headless clients; the first one to join leads the room and presses START itself (--start-when 2: a headless client has
# nobody to click) once the second one is in: the match starts with the two of them, the room keeps what was asked for (four) and shows who joined (two)
LEAD="E2E-LEAD-$RANDOM"
RESP="$(curl -s -m 3 -X POST -H "Authorization: Bearer $SECRET" -d "{\"map\":\"TINY.LVL\",\"players\":4,\"code\":\"$LEAD\",\"seed\":7}" "$CTL/rooms")"
check "a room for four is made: it allows an early start and has no leader yet" "$(echo "$RESP" | grep -q '"early_start":true' && echo "$RESP" | grep -q '"leader":null'; echo $?)"
"$GAME" --headless --no-lan --name First --join "127.0.0.1:$GAME_PORT" --room "$LEAD" --start-when 2 --screenshot "$WORK/l1.png" --frames 4000 > "$WORK/l1.log" 2>&1 &
LEAD_PIDS="$!"
LED=1
for _ in $(seq 1 100); do
    STATUS="$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "$CTL/rooms/$LEAD")"
    if echo "$STATUS" | grep -q '"leader":0'; then LED=0; break; fi
    sleep 0.1
done
check "the first player to join is the leader of the room (seat 0)" "$LED"
sleep 2
check "alone in a room for four the leader starts nothing" "$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "$CTL/rooms/$LEAD" | grep -q '"state":"waiting"'; echo $?)"
"$GAME" --headless --no-lan --name Second --join "127.0.0.1:$GAME_PORT" --room "$LEAD" --screenshot "$WORK/l2.png" --frames 4000 > "$WORK/l2.log" 2>&1 &
LEAD_PIDS="$LEAD_PIDS $!"
STARTED=1
for _ in $(seq 1 150); do
    STATUS="$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "$CTL/rooms/$LEAD")"
    if echo "$STATUS" | grep -q '"state":"running"'; then STARTED=0; break; fi
    sleep 0.2
done
check "the leader pressed START when the second player was in: the match runs (it was not started by the room: two of four)" "$STARTED"
check "the room keeps what was asked for (4 players) and shows who joined (2: First and Second)" "$(echo "$STATUS" | grep -q '"expected":4' && echo "$STATUS" | grep -q '"joined":2' && echo "$STATUS" | grep -q 'First' && echo "$STATUS" | grep -q 'Second'; echo $?)"
check "the leader's request was honoured (none ignored)" "$(echo "$STATUS" | grep -q '"ignored_start_requests":0'; echo $?)"
sleep 1
TICKS="$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "$CTL/rooms/$LEAD" | python3 -c 'import sys, json; print(json.load(sys.stdin).get("ticks", 0))')"
check "the referee's clock runs for the two of them" "$([ "${TICKS:-0}" -gt 0 ]; echo $?)"
check "neither client reported an error" "$(grep -qiE 'out of sync|failed|error' "$WORK/l1.log" "$WORK/l2.log"; [ $? -ne 0 ]; echo $?)"
check "the room did not fail (no desync)" "$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "$CTL/rooms/$LEAD" | grep -q '"state":"running"'; echo $?)"
for p in $LEAD_PIDS; do kill "$p" 2> /dev/null; done
for p in $LEAD_PIDS; do wait "$p" 2> /dev/null; done
code_of -X DELETE -H "Authorization: Bearer $SECRET" "$CTL/rooms/$LEAD" > /dev/null

# flood control (v0.0.93): a raw client that is no game sends valid messages as fast as its line allows (a StartRequest that is ignored, a Ping), in a room of its own while two real
# clients play in another. Before the TCP inbox was bounded and the messages counted, the server read and parsed everything into memory (the process grew by gigabytes in seconds,
# one thread busy, the referee of the other room late, the control interface slow) and never dropped the sender; now the sender is dropped after a second's worth at the most.
PROTOCOL="$("$SERVER" --version 2> /dev/null | sed -n 's/.*(network protocol \([0-9][0-9]*\)).*/\1/p')"      # the raw client below says Hello with the protocol of this very server (it said a literal 7 until protocol 8)
check "the server says which network protocol it speaks (--version: $PROTOCOL)" "$([ -n "$PROTOCOL" ]; echo $?)"
rss_kb() { ps -o rss= -p "$SERVER_PID" 2> /dev/null | tr -d ' '; }
cpu_secs() { ps -o time= -p "$SERVER_PID" 2> /dev/null | python3 -c 'import sys; t = sys.stdin.read().strip().replace("-", ":"); s = 0.0
for part in t.split(":"): s = s * 60 + float(part)
print(s)'; }
ticks_of() { curl -s -m 3 -H "Authorization: Bearer $SECRET" "$CTL/rooms/$1" | python3 -c 'import sys, json; print(json.load(sys.stdin).get("ticks", 0))' 2> /dev/null; }
field_of() { curl -s -m 3 -H "Authorization: Bearer $SECRET" "$CTL/rooms/$1" | python3 -c 'import sys, json; print(json.load(sys.stdin).get(sys.argv[1], ""))' "$2" 2> /dev/null; }
VICTIM="E2E-VICTIM-$RANDOM"
curl -s -m 3 -o /dev/null -X POST -H "Authorization: Bearer $SECRET" -d "{\"map\":\"TINY.LVL\",\"players\":2,\"code\":\"$VICTIM\",\"seed\":11}" "$CTL/rooms"
VICTIM_PIDS=""
for i in 1 2; do
    "$GAME" --headless --no-lan --name "Victim$i" --join "127.0.0.1:$GAME_PORT" --room "$VICTIM" --screenshot "$WORK/v$i.png" --frames 4000000 > "$WORK/v$i.log" 2>&1 &
    VICTIM_PIDS="$VICTIM_PIDS $!"
done
VICTIM_UP=1
for _ in $(seq 1 100); do
    [ "$(field_of "$VICTIM" state)" = "running" ] && { VICTIM_UP=0; break; }
    sleep 0.2
done
check "two clients play in a room next to the flood room (the match runs)" "$VICTIM_UP"
rate_of_victim() { local a b; a="$(ticks_of "$VICTIM")"; sleep "$1"; b="$(ticks_of "$VICTIM")"; python3 -c "print(($b - $a) / $1)"; }
QUIET_RATE="$(rate_of_victim 3)"
check "the referee of that match ticks about 20 times a second without a flood ($QUIET_RATE)" "$(python3 -c "print(0 if 15 < $QUIET_RATE < 25 else 1)")"
flood() {      # flood KIND ROOM SECONDS: a raw client says Hello for ROOM, then writes frames of KIND as fast as it can for SECONDS; prints how many Pongs it got back
    python3 - "$GAME_PORT" "$2" "$1" "$3" "${PROTOCOL:-0}" <<'PY'
import socket, struct, sys, time
port, room, kind, secs, protocol = int(sys.argv[1]), sys.argv[2], sys.argv[3], float(sys.argv[4]), int(sys.argv[5])
def frame(p): return struct.pack('<I', len(p)) + p
def str8(t):
    b = t.encode()
    return bytes([len(b)]) + b
hello = bytes([1]) + struct.pack('<H', protocol) + str8('Evil') + struct.pack('<H', 0) + bytes([255]) + str8(room) + str8('') + bytes(16) + struct.pack('<I', 0)     # (protocol 10: no key, no turns)
message = bytes([24]) if kind == 'startreq' else bytes([10]) + struct.pack('<II', 1, 0)
sock = socket.create_connection(('127.0.0.1', port), timeout=5)
sock.sendall(frame(hello))
sock.setblocking(False)
blob = frame(message) * 20000
offset = 0
received = b''
pongs = 0
end = time.time() + secs
while time.time() < end:
    try:
        offset = (offset + sock.send(blob[offset:])) % len(blob)           # (frames stay whole: it goes on where it stopped)
    except BlockingIOError:
        time.sleep(0.001)
    except OSError:
        break
    try:
        data = sock.recv(65536)
        if not data:
            break
        received += data
    except BlockingIOError:
        pass
    except OSError:
        break
    pos = 0
    while len(received) - pos >= 4:
        n = struct.unpack_from('<I', received, pos)[0]
        if len(received) - pos - 4 < n:
            break
        if n >= 1 and received[pos + 4] == 11:
            pongs += 1
        pos += 4 + n
    received = received[pos:]
print(pongs)
PY
}
for KIND in startreq ping; do
    FLOODROOM="E2E-FLOOD-$KIND-$RANDOM"
    curl -s -m 3 -o /dev/null -X POST -H "Authorization: Bearer $SECRET" -d "{\"map\":\"TINY.LVL\",\"players\":4,\"code\":\"$FLOODROOM\",\"seed\":5}" "$CTL/rooms"
    RSS_BEFORE="$(rss_kb)"
    TICKS_A="$(ticks_of "$VICTIM")"
    FLOOD_START="$(python3 -c 'import time; print(time.time())')"
    flood "$KIND" "$FLOODROOM" 4 > "$WORK/flood_$KIND.out" &
    FLOOD_PID=$!
    WORST=0
    JOINED_AT_2S=""
    for I in $(seq 1 8); do                                  # the control interface answers while the flood goes on
        T="$(curl -s -o /dev/null -w '%{time_total}' -m 5 -H "Authorization: Bearer $SECRET" "$CTL/rooms")"
        WORST="$(python3 -c "print(max($WORST, $T))")"
        [ "$I" = "6" ] && JOINED_AT_2S="$(field_of "$FLOODROOM" joined)"       # about two seconds into the flood, which still goes on
        sleep 0.4
    done
    wait "$FLOOD_PID"
    FLOOD_SECS="$(python3 -c "import time; print(time.time() - $FLOOD_START)")"
    TICKS_B="$(ticks_of "$VICTIM")"
    RSS_AFTER="$(rss_kb)"
    check "$KIND flood: about two seconds into the flood, which still goes on, the flooder is no longer in its room (the server dropped it), the room is still open" "$([ "$JOINED_AT_2S" = "0" ] && [ "$(field_of "$FLOODROOM" state)" = "waiting" ]; echo $?)"
    if [ "$KIND" = "startreq" ]; then
        check "startreq flood: it had a seat and was out at its 24th request (16 are free, each of the next eight is a violation): the rest was never read" "$([ "$(field_of "$FLOODROOM" ignored_start_requests)" = "24" ]; echo $?)"
    else
        PONGS="$(cat "$WORK/flood_$KIND.out")"
        check "ping flood: the server answered a second's worth of pings and no more ($PONGS pongs of millions of pings)" "$([ "${PONGS:-0}" -ge 1000 ] && [ "${PONGS:-0}" -le 1300 ]; echo $?)"
    fi
    check "$KIND flood: the control interface answered within a second all the time (worst: ${WORST} s)" "$(python3 -c "print(0 if $WORST < 1.0 else 1)")"
    check "$KIND flood: the match in the other room kept its clock (ticks a second: $(python3 -c "print(round(($TICKS_B - $TICKS_A) / $FLOOD_SECS, 1))"); it was 8.6 on a loaded machine before)" "$(python3 -c "print(0 if ($TICKS_B - $TICKS_A) / $FLOOD_SECS > 14 else 1)")"
    check "$KIND flood: the server did not grow by more than 200 MB (it grew by gigabytes: $(( (RSS_AFTER - RSS_BEFORE) / 1024 )) MB)" "$([ $(( (RSS_AFTER - RSS_BEFORE) / 1024 )) -lt 200 ]; echo $?)"
    CPU_A="$(cpu_secs)"
    sleep 3
    CPU_B="$(cpu_secs)"
    check "$KIND flood: afterwards the server is idle again (CPU used in the next 3 s: $(python3 -c "print(round($CPU_B - $CPU_A, 2))") s)" "$(python3 -c "print(0 if $CPU_B - $CPU_A < 1.0 else 1)")"
    code_of -X DELETE -H "Authorization: Bearer $SECRET" "$CTL/rooms/$FLOODROOM" > /dev/null
done
check "the match next to the floods is still running and its clients saw no error" "$([ "$(field_of "$VICTIM" state)" = "running" ] && ! grep -qiE 'out of sync|failed|error' "$WORK/v1.log" "$WORK/v2.log"; echo $?)"
for p in $VICTIM_PIDS; do kill "$p" 2> /dev/null; done
for p in $VICTIM_PIDS; do wait "$p" 2> /dev/null; done
code_of -X DELETE -H "Authorization: Bearer $SECRET" "$CTL/rooms/$VICTIM" > /dev/null

# closing the room writes its result; SIGTERM stops the server
check "the owner closes the room: 200" "$([ "$(code_of -X DELETE -H "Authorization: Bearer $SECRET" "$CTL/rooms/$CODE")" = "200" ]; echo $?)"
sleep 0.5
kill -TERM "$SERVER_PID" 2> /dev/null
wait "$SERVER_PID" 2> /dev/null
SERVER_EXIT=$?
SERVER_PID=""
check "the server stops cleanly on SIGTERM" "$([ "$SERVER_EXIT" -eq 0 ]; echo $?)"
check "the room's result file was written" "$([ -s "$WORK/results/$CODE.json" ]; echo $?)"
check "the log never shows the secret" "$(grep -q "$SECRET" "$WORK/server.log"; [ $? -ne 0 ]; echo $?)"

# no secret in the environment: the server makes one the first time (owner-only file, shown in the log once, the moment it is made) and uses the same one at every later start;
# a secret in the environment wins; a file that is no secret stops the server and is left alone; nowhere to keep one is an error
mode_of() { stat -c %a "$1" 2> /dev/null || stat -f %Lp "$1"; }
SECRET_ENV=(-u ANTS_SERVER_SECRET)          # how the next server sees the variable: not set at all (the empty value: SECRET_ENV=(ANTS_SERVER_SECRET=), what the compose files give)
start_nosecret() {       # start_nosecret LOG CTL_PORT [more options]: the server without a secret in the environment; waits until it answers
    local log="$1" ctl="$2"
    shift 2
    # umask 0: the mode of the secret file is then the mode that the server asked for (a strict umask would hide a wrong one); exec: SERVER_PID is the server
    ( umask 0; exec env "${SECRET_ENV[@]}" "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$(free_port)" --ctl-port "$ctl" "$@" ) > "$log" 2>&1 &
    SERVER_PID=$!
    for _ in $(seq 1 50); do
        curl -s -m 1 "http://127.0.0.1:$ctl/healthz" | grep -q '"ok"' && return 0
        kill -0 "$SERVER_PID" 2> /dev/null || return 1
        sleep 0.1
    done
    return 1
}
stop_server() { kill -TERM "$SERVER_PID" 2> /dev/null; wait "$SERVER_PID" 2> /dev/null; SERVER_PID=""; }
exit_nosecret() { env -u ANTS_SERVER_SECRET perl -e 'alarm 5; exec @ARGV' "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$(free_port)" --ctl-port "$(free_port)" "$@" > "$WORK/exit.log" 2>&1; echo $?; }
GEN_DIR="$WORK/gen"
GEN_CTL1="$(free_port)"
start_nosecret "$WORK/gen1.log" "$GEN_CTL1" --results-dir "$GEN_DIR"
check "no ANTS_SERVER_SECRET but a results folder: the server starts and makes a secret" "$?"
GEN_SECRET="$(tr -d '\n' < "$GEN_DIR/control-secret" 2> /dev/null)"
check "the secret file holds 64 hex digits" "$(echo "$GEN_SECRET" | grep -qE '^[0-9a-f]{64}$'; echo $?)"
check "the secret file can be read by its owner only (mode 600, made under umask 0)" "$([ "$(mode_of "$GEN_DIR/control-secret")" = "600" ]; echo $?)"
check "the log shows the new secret once" "$([ "$(grep -c "$GEN_SECRET" "$WORK/gen1.log")" = "1" ]; echo $?)"
check "the generated secret opens the control interface: 200" "$([ "$(code_of -H "Authorization: Bearer $GEN_SECRET" "http://127.0.0.1:$GEN_CTL1/rooms")" = "200" ]; echo $?)"
check "a wrong secret and no secret: 401" "$([ "$(code_of -H "Authorization: Bearer ${GEN_SECRET%?}" "http://127.0.0.1:$GEN_CTL1/rooms")" = "401" ] && [ "$(code_of "http://127.0.0.1:$GEN_CTL1/rooms")" = "401" ]; echo $?)"
stop_server
cp "$GEN_DIR/control-secret" "$WORK/secret.before"
GEN_CTL2="$(free_port)"
start_nosecret "$WORK/gen2.log" "$GEN_CTL2" --results-dir "$GEN_DIR"
check "a second start finds the file" "$?"
check "the second start uses the same secret: 200" "$([ "$(code_of -H "Authorization: Bearer $GEN_SECRET" "http://127.0.0.1:$GEN_CTL2/rooms")" = "200" ]; echo $?)"
check "the second start does not write the file again and does not show the secret" "$(cmp -s "$GEN_DIR/control-secret" "$WORK/secret.before" && ! grep -q "$GEN_SECRET" "$WORK/gen2.log"; echo $?)"
stop_server
# a symbolic link to a good file is followed (a mounted secret is often a link)
ln -s "$GEN_DIR/control-secret" "$WORK/linked-secret"
LINK_CTL="$(free_port)"
start_nosecret "$WORK/gen-link.log" "$LINK_CTL" --secret-file "$WORK/linked-secret"
check "a --secret-file that is a symbolic link to a good file is followed: the same secret, 200" "$([ "$?" = "0" ] && [ "$(code_of -H "Authorization: Bearer $GEN_SECRET" "http://127.0.0.1:$LINK_CTL/rooms")" = "200" ]; echo $?)"
stop_server
# a symbolic link to nothing: the truth, status 2, nothing made behind it
ln -s "$WORK/nowhere" "$WORK/dangling-secret"
check "a --secret-file that is a symbolic link to nothing stops the server with status 2" "$([ "$(exit_nosecret --secret-file "$WORK/dangling-secret")" = "2" ]; echo $?)"
check "and the message says so, and nothing was made behind the link" "$(grep -q 'symbolic link to nothing' "$WORK/exit.log" && [ ! -e "$WORK/nowhere" ]; echo $?)"
GEN_CTL3="$(free_port)"
env ANTS_SERVER_SECRET="env-wins-0123456789-$RANDOM" "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$(free_port)" --ctl-port "$GEN_CTL3" --results-dir "$GEN_DIR" > "$WORK/gen3.log" 2>&1 &
SERVER_PID=$!
for _ in $(seq 1 50); do curl -s -m 1 "http://127.0.0.1:$GEN_CTL3/healthz" | grep -q '"ok"' && break; sleep 0.1; done
check "a secret in the environment wins over the file: the file's secret is refused (401)" "$([ "$(code_of -H "Authorization: Bearer $GEN_SECRET" "http://127.0.0.1:$GEN_CTL3/rooms")" = "401" ]; echo $?)"
stop_server
# a first start that cannot go on (a demo map that is not in the maps folder) has made the file: the log must already show the secret, for the next start only reads it
FIRST_FAIL_DIR="$WORK/firstfail"
check "a first start that fails after it made the secret (demo map missing) stops with status 2" "$([ "$(exit_nosecret --results-dir "$FIRST_FAIL_DIR" --demo-rooms 2 --demo-map NO-SUCH-MAP.LVL)" = "2" ]; echo $?)"
FIRST_FAIL_SECRET="$(tr -d '\n' < "$FIRST_FAIL_DIR/control-secret" 2> /dev/null)"
check "and the log shows the secret that it stored, once (it is shown the moment it is made)" "$(echo "$FIRST_FAIL_SECRET" | grep -qE '^[0-9a-f]{64}$' && [ "$(grep -c "$FIRST_FAIL_SECRET" "$WORK/exit.log")" = "1" ]; echo $?)"
# ANTS_SERVER_SECRET set but empty (what the compose files give when nothing is set) counts as not set
SECRET_ENV=(ANTS_SERVER_SECRET=)
EMPTY_CTL="$(free_port)"
start_nosecret "$WORK/gen-empty.log" "$EMPTY_CTL" --results-dir "$WORK/empty-var"
EMPTY_UP="$?"
SECRET_ENV=(-u ANTS_SERVER_SECRET)
EMPTY_SECRET="$(tr -d '\n' < "$WORK/empty-var/control-secret" 2> /dev/null)"
check "ANTS_SERVER_SECRET set but empty counts as not set: the server makes a secret, and it opens the control interface" "$([ "$EMPTY_UP" = "0" ] && echo "$EMPTY_SECRET" | grep -qE '^[0-9a-f]{64}$' && [ "$(code_of -H "Authorization: Bearer $EMPTY_SECRET" "http://127.0.0.1:$EMPTY_CTL/rooms")" = "200" ]; echo $?)"
stop_server
echo "not a secret" > "$GEN_DIR/control-secret"
check "a secret file that is no secret stops the server with status 2" "$([ "$(exit_nosecret --results-dir "$GEN_DIR")" = "2" ]; echo $?)"
check "and the file is left as it was" "$([ "$(cat "$GEN_DIR/control-secret")" = "not a secret" ]; echo $?)"
check "and the message says what to do" "$(grep -q 'nothing was changed' "$WORK/exit.log"; echo $?)"
check "no secret and nowhere to keep one: status 2 (as before)" "$([ "$(exit_nosecret)" = "2" ]; echo $?)"
check "--secret-file puts the generated secret where it is told" "$(start_nosecret "$WORK/gen4.log" "$(free_port)" --secret-file "$WORK/elsewhere/key"; r=$?; stop_server; [ "$r" = "0" ] && [ -s "$WORK/elsewhere/key" ]; echo $?)"
# the Docker image always passes --results-dir: an explicit --secret-file must win over the default file in that folder (which then is not made)
check "--secret-file together with --results-dir: the secret goes to the file that is named (owner-only), the default file in the results folder is not made" "$(start_nosecret "$WORK/gen5.log" "$(free_port)" --results-dir "$WORK/res5" --secret-file "$WORK/elsewhere5/key"; r=$?; stop_server; [ "$r" = "0" ] && [ -s "$WORK/elsewhere5/key" ] && [ "$(mode_of "$WORK/elsewhere5/key")" = "600" ] && [ ! -e "$WORK/res5/control-secret" ]; echo $?)"

# demo rooms that choose their map: "demo-<map>-..." makes the room on that map when it is in --demo-maps, any other code on --demo-map
PICK_PORT="$(free_port)"
PICK_CTL="$(free_port)"
ANTS_SERVER_SECRET="$SECRET" "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$PICK_PORT" --ctl-port "$PICK_CTL" --demo-rooms 5 --demo-map TINY.LVL --demo-maps TINY.LVL,SMALL.LVL > "$WORK/pick.log" 2>&1 &
SERVER_PID=$!
for _ in $(seq 1 50); do curl -s -m 1 "http://127.0.0.1:$PICK_CTL/healthz" | grep -q '"ok"' && break; sleep 0.1; done
check "the log names the maps that a demo code can choose" "$(grep -q 'chooses one of TINY.LVL, SMALL.LVL' "$WORK/pick.log"; echo $?)"
PICK_PIDS=""
for code in demo-small-t1 demo-tiny-t2 demo-t3 demo-medium-t4 demo-small-2p-t5; do
    "$GAME" --headless --no-lan --name Chooser --join "127.0.0.1:$PICK_PORT" --room "$code" --frames 400 > "$WORK/pick_$code.log" 2>&1 &
    PICK_PIDS="$PICK_PIDS $!"
done
map_of_room() { for _ in $(seq 1 60); do M="$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "http://127.0.0.1:$PICK_CTL/rooms/$1" | python3 -c 'import sys, json; print(json.load(sys.stdin).get("map", ""))' 2> /dev/null)"; [ -n "$M" ] && break; sleep 0.2; done; echo "$M"; }
check "demo-small-t1 is made on SMALL.LVL" "$([ "$(map_of_room demo-small-t1)" = "SMALL.LVL" ]; echo $?)"
check "demo-tiny-t2 is made on TINY.LVL" "$([ "$(map_of_room demo-tiny-t2)" = "TINY.LVL" ]; echo $?)"
check "demo-t3 (no map in the code) is made on the default map" "$([ "$(map_of_room demo-t3)" = "TINY.LVL" ]; echo $?)"
check "demo-medium-t4 (a map that is not in the list) is made on the default map" "$([ "$(map_of_room demo-medium-t4)" = "TINY.LVL" ]; echo $?)"
expected_of_room() { curl -s -m 2 -H "Authorization: Bearer $SECRET" "http://127.0.0.1:$PICK_CTL/rooms/$1" | python3 -c 'import sys, json; print(json.load(sys.stdin).get("expected", ""))' 2> /dev/null; }
check "demo-small-2p-t5 is made on SMALL.LVL for two players" "$([ "$(map_of_room demo-small-2p-t5)" = "SMALL.LVL" ] && [ "$(expected_of_room demo-small-2p-t5)" = "2" ] && [ "$(expected_of_room demo-small-t1)" = "4" ]; echo $?)"
for p in $PICK_PIDS; do kill "$p" 2> /dev/null; done
stop_server

# ---- the server that holds the seat of a player whose connection is lost (protocol 10, --reconnect) --------------------------------------------------------------
# The game's own clients do not come back yet (that is release B: a native client whose link is cut is lost, the message below says so). What is tested here is the SERVER, with
# real programs: a client behind a proxy that is cut, a client that is stopped. The room pauses for everybody and names the seat, nothing runs while it waits, the cap (60 s here)
# drops the seat and the match goes on for the others.
RC_PORT="$(free_port)"
RC_CTL="$(free_port)"
RC_PROXY_PORT="$(free_port)"
RC_CODE="E2E-HOLD-$RANDOM"
RC_STOP_CODE="E2E-STOP-$RANDOM"
RC_URL="http://127.0.0.1:$RC_CTL"
rc_field() { curl -s -m 3 -H "Authorization: Bearer $SECRET" "$RC_URL/rooms/$1" | python3 -c 'import sys, json; v = json.load(sys.stdin)
for part in sys.argv[1].split("."):
    v = v.get(part) if isinstance(v, dict) else None
print(json.dumps(v) if isinstance(v, (dict, list)) or v is None else str(v).lower() if isinstance(v, bool) else v)' "$2" 2> /dev/null; }
ANTS_SERVER_SECRET="$SECRET" "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$RC_PORT" --ctl-port "$RC_CTL" --results-dir "$WORK/rc_results" --reconnect --max-pause-seconds 60 --resume-countdown-seconds 8 > "$WORK/rc_server.log" 2>&1 &
SERVER_PID=$!
RC_UP=1
for _ in $(seq 1 50); do
    if curl -s -m 1 "$RC_URL/healthz" | grep -q '"ok"'; then RC_UP=0; break; fi
    kill -0 "$SERVER_PID" 2> /dev/null || break
    sleep 0.1
done
check "the reconnect server is up" "$RC_UP"
check "the log says that rooms hold the seat of a player whose connection is lost, with the vote and the cap" "$(grep -q 'rooms hold the seat of a player whose connection is lost' "$WORK/rc_server.log" && grep -q 'capped at 60 s' "$WORK/rc_server.log"; echo $?)"
RC_BODY="{\"map\":\"TINY.LVL\",\"players\":3,\"code\":\"$RC_CODE\",\"seed\":5}"      # (made in an assignment: a JSON body inside "$( )" in an argument is mangled by the brace expansion of macOS's bash 3.2)
RC_RESP="$(curl -s -m 3 -X POST -H "Authorization: Bearer $SECRET" -d "$RC_BODY" "$RC_URL/rooms")"
check "a room that says nothing takes the server's setting: reconnect on, the cap 60 s, the vote 30 s" "$(echo "$RC_RESP" | grep -q '"reconnect":true' && [ "$(rc_field "$RC_CODE" max_pause_seconds)" = "60" ] && [ "$(rc_field "$RC_CODE" hold_vote_seconds)" = "30" ]; echo $?)"
check "a vote time of 4 s is refused by the control interface: 400" "$([ "$(code_of -X POST -H "Authorization: Bearer $SECRET" -d '{"map":"TINY.LVL","hold_vote_seconds":4}' "$RC_URL/rooms")" = "400" ]; echo $?)"
check "a pause cap of 59 s is refused by the control interface: 400" "$([ "$(code_of -X POST -H "Authorization: Bearer $SECRET" -d '{"map":"TINY.LVL","max_pause_seconds":59}' "$RC_URL/rooms")" = "400" ]; echo $?)"
curl -s -m 3 -o /dev/null -X POST -H "Authorization: Bearer $SECRET" -d "{\"map\":\"TINY.LVL\",\"players\":3,\"code\":\"$RC_STOP_CODE\",\"seed\":6}" "$RC_URL/rooms"
python3 "$ROOT/tests/scripts/flaky_proxy.py" "$RC_PROXY_PORT" "$RC_PORT" 3600 > "$WORK/proxy.log" 2>&1 &
PROXY_PID=$!
for _ in $(seq 1 50); do grep -q listening "$WORK/proxy.log" 2> /dev/null && break; sleep 0.1; done
RC_PIDS=""
"$GAME" --headless --no-lan --name Holder1 --join "127.0.0.1:$RC_PORT" --room "$RC_CODE" --screenshot "$WORK/h1.png" --frames 4000000 > "$WORK/h1.log" 2>&1 &
RC_PIDS="$!"
"$GAME" --headless --no-lan --name Holder2 --join "127.0.0.1:$RC_PORT" --room "$RC_CODE" --screenshot "$WORK/h2.png" --frames 4000000 > "$WORK/h2.log" 2>&1 &
RC_PIDS="$RC_PIDS $!"
"$GAME" --headless --no-lan --name Victim --join "127.0.0.1:$RC_PROXY_PORT" --room "$RC_CODE" --screenshot "$WORK/hv.png" --frames 4000000 > "$WORK/hv.log" 2>&1 &
RC_VICTIM_PID="$!"
RC_PIDS="$RC_PIDS $RC_VICTIM_PID"
RC_RUNNING=1
for _ in $(seq 1 150); do
    [ "$(rc_field "$RC_CODE" state)" = "running" ] && { RC_RUNNING=0; break; }
    sleep 0.2
done
check "three clients (one of them behind the proxy) joined and the match runs" "$RC_RUNNING"
check "the status says that nobody waits: not paused, nobody absent, no vote, the log is being kept" "$([ "$(rc_field "$RC_CODE" paused)" = "false" ] && [ "$(rc_field "$RC_CODE" absent)" = "[]" ] && [ "$(rc_field "$RC_CODE" vote)" = "null" ] && [ "$(rc_field "$RC_CODE" log.usable)" = "true" ] && [ "$(rc_field "$RC_CODE" log.turns)" -gt 0 ]; echo $?)"
sleep 2
RC_T0="$(rc_field "$RC_CODE" ticks)"
check "the referee's clock runs before the cut" "$([ "${RC_T0:-0}" -gt 0 ]; echo $?)"

# the second room: a client that is stopped (a machine that hangs: its link stays open and says nothing)
"$GAME" --headless --no-lan --name Steady1 --join "127.0.0.1:$RC_PORT" --room "$RC_STOP_CODE" --screenshot "$WORK/s1.png" --frames 4000000 > "$WORK/s1.log" 2>&1 &
RC_PIDS="$RC_PIDS $!"
"$GAME" --headless --no-lan --name Steady2 --join "127.0.0.1:$RC_PORT" --room "$RC_STOP_CODE" --screenshot "$WORK/s2.png" --frames 4000000 > "$WORK/s2.log" 2>&1 &
RC_PIDS="$RC_PIDS $!"
"$GAME" --headless --no-lan --name Frozen --join "127.0.0.1:$RC_PORT" --room "$RC_STOP_CODE" --screenshot "$WORK/sf.png" --frames 4000000 > "$WORK/sf.log" 2>&1 &
RC_FROZEN_PID="$!"
RC_PIDS="$RC_PIDS $RC_FROZEN_PID"
RC_STOP_RUNNING=1
for _ in $(seq 1 150); do
    [ "$(rc_field "$RC_STOP_CODE" state)" = "running" ] && { RC_STOP_RUNNING=0; break; }
    sleep 0.2
done
check "the second room runs with its three clients" "$RC_STOP_RUNNING"
sleep 2

# cut the proxy: the victim's link dies, the room pauses (within a few seconds), and says who is missing
kill -USR1 "$PROXY_PID"
RC_CUT_AT="$(python3 -c 'import time; print(time.time())')"
RC_PAUSED=1
for _ in $(seq 1 50); do
    [ "$(rc_field "$RC_CODE" paused)" = "true" ] && { RC_PAUSED=0; break; }
    sleep 0.1
done
RC_PAUSED_AFTER="$(python3 -c "import time; print(round(time.time() - $RC_CUT_AT, 1))")"
check "cutting the link pauses the room (status: paused, after $RC_PAUSED_AFTER s)" "$RC_PAUSED"
check "the status names the absent seat: Victim, state absent" "$(rc_field "$RC_CODE" absent | python3 -c 'import sys, json; a = json.load(sys.stdin); sys.exit(0 if len(a) == 1 and a[0]["name"] == "Victim" and a[0]["state"] == "absent" else 1)'; echo $?)"
RC_TA="$(rc_field "$RC_CODE" ticks)"
sleep 6
RC_TB="$(rc_field "$RC_CODE" ticks)"
check "nothing advances while the room waits (ticks $RC_TA, $RC_TB six seconds later)" "$([ "$((RC_TB - RC_TA))" -le 2 ]; echo $?)"
check "the others are still in the match, paused, not dropped: the room is running and still holds the seat" "$([ "$(rc_field "$RC_CODE" state)" = "running" ] && [ "$(rc_field "$RC_CODE" paused)" = "true" ] && [ "$(rc_field "$RC_CODE" drops_by_cap)" = "0" ]; echo $?)"
check "the paused time grows (paused_seconds at least 5)" "$([ "$(rc_field "$RC_CODE" paused_seconds)" -ge 5 ]; echo $?)"
check "the victim's game says that the connection was lost (a native client does not rejoin yet: release B)" "$(grep -q 'connection to the other players was lost' "$WORK/hv.log"; echo $?)"

# the second room: stop a client for 15 s
kill -STOP "$RC_FROZEN_PID"
RC_STOP_AT="$(python3 -c 'import time; print(time.time())')"
sleep 8
check "8 s of silence is no loss: the second room is not paused yet" "$([ "$(rc_field "$RC_STOP_CODE" paused)" = "false" ]; echo $?)"
RC_STOP_PAUSED=1
for _ in $(seq 1 70); do
    [ "$(rc_field "$RC_STOP_CODE" paused)" = "true" ] && { RC_STOP_PAUSED=0; break; }
    sleep 0.1
done
RC_STOP_PAUSED_AFTER="$(python3 -c "import time; print(round(time.time() - $RC_STOP_AT, 1))")"
check "10 s of silence is a loss: the second room pauses, $RC_STOP_PAUSED_AFTER s after the client was stopped" "$RC_STOP_PAUSED"
check "... between 9.5 and 13 s after it (its last word, and the check every pass)" "$(python3 -c "print(0 if 9.5 <= $RC_STOP_PAUSED_AFTER <= 13 else 1)")"
check "the absent seat is the stopped client (Frozen), absent and not lagging" "$(rc_field "$RC_STOP_CODE" absent | python3 -c 'import sys, json; a = json.load(sys.stdin); sys.exit(0 if len(a) == 1 and a[0]["name"] == "Frozen" and a[0]["state"] == "absent" else 1)'; echo $?)"
sleep 5
kill -CONT "$RC_FROZEN_PID"
RC_FROZEN_LOST=1
for _ in $(seq 1 100); do
    grep -q 'connection to the other players was lost' "$WORK/sf.log" && { RC_FROZEN_LOST=0; break; }
    sleep 0.2
done
check "the client that wakes up finds its old link closed and ends with the lost connection message (release B will make it rejoin)" "$RC_FROZEN_LOST"
check "the second room is still paused and holds the seat" "$([ "$(rc_field "$RC_STOP_CODE" paused)" = "true" ] && [ "$(rc_field "$RC_STOP_CODE" state)" = "running" ]; echo $?)"
code_of -X DELETE -H "Authorization: Bearer $SECRET" "$RC_URL/rooms/$RC_STOP_CODE" > /dev/null

# the cap: 60 s after the cut the absent seat is dropped, the match goes on
RC_CAPPED=1
for _ in $(seq 1 300); do
    [ "$(rc_field "$RC_CODE" drops_by_cap)" = "1" ] && { RC_CAPPED=0; break; }
    sleep 0.2
done
RC_CAPPED_AFTER="$(python3 -c "import time; print(round(time.time() - $RC_CUT_AT, 1))")"
check "the cap drops the absent seat ($RC_CAPPED_AFTER s after the cut: 60 s of pause)" "$RC_CAPPED"
check "... about 60 s after the cut (59 - 64 s)" "$(python3 -c "print(0 if 59 <= $RC_CAPPED_AFTER <= 64 else 1)")"
sleep 2
check "the room is held for the resume countdown (8 s here) after the cap dropped the last seat that was missing: paused, nobody absent, the seconds left are told" "$([ "$(rc_field "$RC_CODE" paused)" = "true" ] && [ "$(rc_field "$RC_CODE" absent)" = "[]" ] && [ "$(rc_field "$RC_CODE" resume_seconds)" -ge 1 ] && [ "$(rc_field "$RC_CODE" resume_seconds)" -le 8 ] && [ "$(rc_field "$RC_CODE" resume_countdown_seconds)" = "8" ]; echo $?)"
RC_TC="$(rc_field "$RC_CODE" ticks)"
sleep 2
check "nothing advances during the countdown" "$([ "$(( $(rc_field "$RC_CODE" ticks) - RC_TC ))" -le 2 ]; echo $?)"
RC_RESUMED=1
for _ in $(seq 1 100); do
    [ "$(rc_field "$RC_CODE" paused)" = "false" ] && { RC_RESUMED=0; break; }
    sleep 0.2
done
check "the countdown ends and the match goes on" "$RC_RESUMED"
sleep 1
check "the match goes on without it: not paused, nobody absent" "$([ "$(rc_field "$RC_CODE" paused)" = "false" ] && [ "$(rc_field "$RC_CODE" absent)" = "[]" ] && [ "$(rc_field "$RC_CODE" state)" = "running" ] && [ "$(rc_field "$RC_CODE" resume_seconds)" = "0" ]; echo $?)"
RC_TC="$(rc_field "$RC_CODE" ticks)"
sleep 3
RC_TD="$(rc_field "$RC_CODE" ticks)"
check "the referee's clock runs again (ticks $RC_TC, $RC_TD three seconds later)" "$([ "$((RC_TD - RC_TC))" -ge 40 ]; echo $?)"
check "the status keeps the counters: the pause lasted about 60 s, nobody came back, one seat dropped by the cap, none by a vote" "$([ "$(rc_field "$RC_CODE" paused_seconds)" -ge 59 ] && [ "$(rc_field "$RC_CODE" paused_seconds)" -le 65 ] && [ "$(rc_field "$RC_CODE" rejoins)" = "0" ] && [ "$(rc_field "$RC_CODE" drops_by_vote)" = "0" ]; echo $?)"
check "the two clients that stayed saw no error" "$(grep -qiE 'out of sync|failed|error' "$WORK/h1.log" "$WORK/h2.log"; [ $? -ne 0 ]; echo $?)"
check "the second room's two steady clients saw no error either" "$(grep -qiE 'out of sync|failed|error' "$WORK/s1.log" "$WORK/s2.log"; [ $? -ne 0 ]; echo $?)"
for p in $RC_PIDS; do kill -CONT "$p" 2> /dev/null; kill "$p" 2> /dev/null; done
for p in $RC_PIDS; do wait "$p" 2> /dev/null; done
RC_PIDS=""
code_of -X DELETE -H "Authorization: Bearer $SECRET" "$RC_URL/rooms/$RC_CODE" > /dev/null
sleep 0.5
kill -TERM "$SERVER_PID" 2> /dev/null
wait "$SERVER_PID" 2> /dev/null
SERVER_PID=""
kill "$PROXY_PID" 2> /dev/null
wait "$PROXY_PID" 2> /dev/null
PROXY_PID=""
check "the result file of the room that held a seat keeps the counters (drops_by_cap 1, log) and no secret or key" "$([ -s "$WORK/rc_results/$RC_CODE.json" ] && grep -q '"drops_by_cap":1' "$WORK/rc_results/$RC_CODE.json" && grep -q '"log":{' "$WORK/rc_results/$RC_CODE.json" && ! grep -q "$SECRET" "$WORK/rc_results/$RC_CODE.json"; echo $?)"
check "the server's log names the rooms' ends with what the pause came to, and never the secret" "$(grep -q "room $RC_CODE" "$WORK/rc_server.log" && grep -q 'by the cap' "$WORK/rc_server.log" && ! grep -q "$SECRET" "$WORK/rc_server.log"; echo $?)"

echo "  [reconnect e2e] the link cut: the room paused after $RC_PAUSED_AFTER s; the cap dropped the seat $RC_CAPPED_AFTER s after the cut (60 s of pause); a client stopped: the room paused $RC_STOP_PAUSED_AFTER s later"
echo "server e2e: $CHECKS checks, $FAILS failures"
[ "$FAILS" -eq 0 ]
