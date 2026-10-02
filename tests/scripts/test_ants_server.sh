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
