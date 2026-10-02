#!/usr/bin/env bash
# The dedicated server with REAL programs: ants_server (TCP game port, control interface with a bearer secret) and two headless game clients that join a room by its
# code. Checks: the control interface refuses a missing or a wrong secret, makes a room with the right one, the two clients join, the match starts by itself (nobody
# presses START), runs, and both clients finish their frames without an error; a room for four whose leader (the first client to join, --start-when 2: a test hook that
# presses START for a headless client) starts it with the two players who are there; a raw client (no game) that floods the server with valid messages (StartRequests, Pings) is
# dropped within seconds while a match in another room keeps its clock, the control interface answers, and the process neither grows nor stays busy; the server stops cleanly on
# SIGTERM and writes the result file of a room that was closed.
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
    for p in $CLIENT_PIDS $VICTIM_PIDS $LEAD_PIDS; do kill "$p" 2> /dev/null; done
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

echo "server e2e: $CHECKS checks, $FAILS failures"
[ "$FAILS" -eq 0 ]
