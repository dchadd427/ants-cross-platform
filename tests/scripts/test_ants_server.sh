#!/usr/bin/env bash
# The dedicated server with REAL programs: ants_server (TCP game port, control interface with a bearer secret) and two headless game clients that join a room by its
# code. Checks: the control interface refuses a missing or a wrong secret, makes a room with the right one, the two clients join, the match starts by itself (nobody
# presses START), runs, and both clients finish their frames without an error; the server stops cleanly on SIGTERM and writes the result file of a room that was closed.
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
    for p in $CLIENT_PIDS; do kill "$p" 2> /dev/null; done
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
# the command line: a bad demo-room option is refused at once with status 2 (a server that did start would be stopped by the alarm: status 142)
exit_of() { perl -e 'alarm 5; exec @ARGV' env ANTS_SERVER_SECRET=x "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$(free_port)" "$@" > /dev/null 2>&1; echo $?; }
check "--demo-rooms without --demo-map is refused" "$([ "$(exit_of --demo-rooms 2)" = "2" ]; echo $?)"
check "--demo-rooms with a map that is not in the folder is refused" "$([ "$(exit_of --demo-rooms 2 --demo-map NO-SUCH-MAP.LVL)" = "2" ]; echo $?)"
check "--demo-rooms 0 is refused" "$([ "$(exit_of --demo-rooms 0 --demo-map TINY.LVL)" = "2" ]; echo $?)"
check "--demo-rooms that is no number is refused" "$([ "$(exit_of --demo-rooms many --demo-map TINY.LVL)" = "2" ]; echo $?)"
check "--demo-rooms as many as --max-rooms is refused (rooms of the control interface keep their places)" "$([ "$(exit_of --demo-rooms 5 --max-rooms 5 --demo-map TINY.LVL)" = "2" ]; echo $?)"

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

echo "server e2e: $CHECKS checks, $FAILS failures"
[ "$FAILS" -eq 0 ]
