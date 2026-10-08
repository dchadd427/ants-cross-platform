#!/usr/bin/env bash
# The dedicated server with REAL programs: ants_server (TCP game port, control interface with a bearer secret) and two headless game clients that join a room by its
# code. Checks: the control interface refuses a missing or a wrong secret, makes a room with the right one, the two clients join, the match starts by itself (nobody
# presses START), runs (the referee's first tick comes after the 5 s of the "Get ready to play!" dialog: network protocol 12), and both clients play without an error; a room for four whose leader (the first client to join, --start-when 2: a test hook that
# presses START for a headless client) starts it with the two players who are there; a raw client (no game) that floods the server with valid messages (StartRequests, Pings) is
# dropped within seconds while a match in another room keeps its clock, the control interface answers, and the process neither grows nor stays busy; the server stops cleanly on
# SIGTERM and writes the result file of a room that was closed. The last section is the server that HOLDS the seat of a player whose connection is lost (protocol 10, --reconnect):
# a small TCP proxy (flaky_proxy.py) between a client and the server is cut, the room pauses and names the absent seat, nothing runs while it waits, and at the cap the seat is dropped
# and the match goes on; a client that is stopped (kill -STOP) for 15 s pauses the room after 10 s of silence, and when it wakes up it finds its link closed and comes back by itself (the game's own
# way back: a new link, a Hello with its key). The part ends with the RESTART RECORDS (docs/NETWORK_PORT.md): the server keeps
# a record of a running match in a folder of its own (mode 600 in a folder of mode 700), is stopped with SIGTERM in the middle of it and killed with SIGKILL, and started again over the same folder: the room
# comes back with its code, paused, every seat held, the two real games (stopped meanwhile, so that they do not come back before the room is looked at) wake up and find the room by themselves, and the
# record goes when the owner closes the room; a thousand records of a long match are replayed while the server answers /busy within 2 s of its launch and takes a new room's player at once
# (the restore does not block it); the options of the records are refused or accepted in the options part. The last part, replays, is the matches that the server keeps (docs/REPLAYS.md "On the
# game server"): two real games play 32 seconds in a room, the owner closes it, and the match is kept as a file that the control interface and the public replay door list and give out (with the names
# that the two games typed, and without the room's code), that survives a restart of the server, that replay_tool plays out to the same hashes, and that the owner can delete; the door is off unless it is asked for.
# The sections below are PARTS: they run one after the other (the default), or alone with `--part NAME` (repeatable), each with its own server on its own ports and its own
# scratch folder, so that ./run_tests.sh and the CI can run them at the same time. `--list-parts` prints the names.
PART_NAMES="options rooms secret demo reconnect replays"
PARTS=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --part)
            shift
            case " $PART_NAMES " in *" ${1:-?} "*) PARTS="$PARTS $1" ;; *) echo "usage: test_ants_server.sh [--part NAME ...] | --list-parts   (parts: $PART_NAMES)" >&2; exit 2 ;; esac
            ;;
        --list-parts) for part in $PART_NAMES; do echo "$part"; done; exit 0 ;;
        *) echo "usage: test_ants_server.sh [--part NAME ...] | --list-parts   (parts: $PART_NAMES)" >&2; exit 2 ;;
    esac
    shift
done
part_enabled() {
    if [ -z "$PARTS" ]; then return 0; fi
    case " $PARTS " in *" $1 "*) return 0 ;; esac
    return 1
}
PART_LABEL=""
[ -n "$PARTS" ] && PART_LABEL=" [${PARTS# }]"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${BUILD_DIR:-build}"
# A sanitized build is far slower than a normal one: ANTS_E2E_TIME_SCALE (1 to 999; ./run_tests.sh --asan sets it) gives the restore section that many times the time and records that many times
# shorter; a wait that ends early still ends at once.
TIME_SCALE="${ANTS_E2E_TIME_SCALE:-1}"
case "$TIME_SCALE" in [1-9]|[1-9][0-9]|[1-9][0-9][0-9]) ;; *) TIME_SCALE=1 ;; esac
SERVER="$ROOT/$BUILD/src/ants_server/ants_server"
GAME="$ROOT/$BUILD/src/ants_app/ants"
FAILS=0
CHECKS=0
check() {      # check "what" exit-status
    CHECKS=$((CHECKS + 1))
    if [ "$2" -ne 0 ]; then
        FAILS=$((FAILS + 1))
        echo "  FAIL:${PART_LABEL} $1"            # (the parts run side by side in the CI: a failure names its part)
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
    for p in $CLIENT_PIDS $VICTIM_PIDS $LEAD_PIDS $FILL_PIDS $PLAN_PIDS $CHAT_PIDS $RC_PIDS $RR_PIDS $RP_PIDS; do kill -CONT "$p" 2> /dev/null; kill "$p" 2> /dev/null; done
    [ -n "$PROXY_PID" ] && kill "$PROXY_PID" 2> /dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT
free_port() { python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()'; }
# (used by several parts)
code_of() { curl -s -m 3 -o /dev/null -w '%{http_code}' "$@"; }
stop_server() { kill -TERM "$SERVER_PID" 2> /dev/null; wait "$SERVER_PID" 2> /dev/null; SERVER_PID=""; }
GAME_PORT="$(free_port)"
CTL_PORT="$(free_port)"
SECRET="e2e-$RANDOM-$RANDOM-secret"
CODE="E2E-ROOM-$RANDOM"
CTL="http://127.0.0.1:$CTL_PORT"
# A match opens with the "Get ready to play!" dialog and its simulation waits for it (network protocol 12): the referee seals the first turn 5 s after the match began, so a room that is
# "running" has ticked nothing yet and its `ticks` count from the end of the dialog. ticks_of ROOM [CONTROL-URL] prints the referee's ticks; wait_ticks ROOM [CONTROL-URL [SECONDS]] polls
# until the room has ticked at all and prints the ticks (0 when it had not within SECONDS, 20 by default). Every check that measures the referee's clock, or cuts a link, starts from there.
ticks_of() { curl -s -m 3 -H "Authorization: Bearer $SECRET" "${2:-$CTL}/rooms/$1" | python3 -c 'import sys, json; print(json.load(sys.stdin).get("ticks", 0))' 2> /dev/null; }
wait_ticks() {
    local t
    for _ in $(seq 1 $(( ${3:-20} * 5 ))); do
        t="$(ticks_of "$1" "${2:-$CTL}")"
        if [ "${t:-0}" -gt 0 ] 2> /dev/null; then echo "$t"; return; fi
        sleep 0.2
    done
    echo 0
}

cd "$ROOT"
# ---- part options: the pages' own texts and nginx.conf, and the command-line options of ants_server (a bad option is refused at once, a good one starts it) -------
if part_enabled options; then
# the Play online page tells the players what the room's leader can do (protocol 7), in its setup hint, its join hint and the line under the room's title
check "web/lobby.html says that the first player in the room can start early with START once at least 2 players are in (setup, join and room hints)" "$([ "$(grep -c 'first player in the room can start' "$ROOT/web/lobby.html")" -ge 3 ]; echo $?)"
# bots fill the empty seats (protocol 11, a level for each seat and the room's teams since protocol 13): the front page's one card offers four seats with a group of five buttons each (Friend, Easy, Medium,
# Hard, Nobody), Sit here and a Team 1 and a Team 2 switch, remembers them, and every link of the room carries the plan as ?fill=<plan> (and &start=<people>), validated; the game page turns exactly those texts into --fill-bots,
# --teams and --start-when (and nothing else: an address cannot put another word on the command line)
FOUR_PAGE="$ROOT/web/lobby.html"
SHELL_PAGE="$ROOT/web/shell.html"
FILL_FORM=1
if grep -qF 'id="seat-0-friend"' "$FOUR_PAGE" && grep -qF 'id="seat-1-easy"' "$FOUR_PAGE" && grep -qF 'id="seat-2-medium"' "$FOUR_PAGE" && grep -qF 'id="seat-3-hard"' "$FOUR_PAGE" && grep -qF 'id="seat-3-nobody"' "$FOUR_PAGE" && grep -qF 'id="sit-3"' "$FOUR_PAGE" && grep -qF 'id="team-0-1"' "$FOUR_PAGE" && grep -qF 'id="team-3-2"' "$FOUR_PAGE" \
    && grep -qF "remember(CARD_KEY, cardText(card));" "$FOUR_PAGE" && grep -qF "var card = cardParse(recall(CARD_KEY)) || cardFromOld({" "$FOUR_PAGE"; then FILL_FORM=0; fi
check 'web/lobby.html offers four seats with Friend, Easy, Medium, Hard and Nobody for each, Sit here and the Team 1 and Team 2 switches on its one card, and remembers them' "$FILL_FORM"
FILL_LINKS=1
if grep -qF "if (fill) q += '&fill=' + fill" "$FOUR_PAGE" && grep -qF "if (roomTeams && !teamsInBlock) q += '&teams=' + encodeURIComponent(roomTeams)" "$FOUR_PAGE" && grep -qF "validFillPlan(params.get('fill'))" "$FOUR_PAGE" && grep -qF "validRoomTeams(params.get('teams'))" "$FOUR_PAGE" \
    && grep -qF "(fill ? '&fill=' + fill : '')" "$FOUR_PAGE" && grep -qF "(roomTeams && !teamsInBlock ? '&teams=' + encodeURIComponent(roomTeams) : '')" "$FOUR_PAGE" && grep -qF "(plan ? '&fill=' + plan : '')" "$FOUR_PAGE" \
    && grep -qF "'&start=' + cardPeople(state)" "$FOUR_PAGE" && grep -qF "Starts at once, in this tab. Bots gather food, raid and fight back." "$FOUR_PAGE"; then FILL_LINKS=0; fi
check "web/lobby.html puts the plan into every game link of a room and into its own address as &fill=<plan> (and &teams=A+B for a room whose create block names no teams), the card's START and invitations carry &fill=<plan>&start=<people>, the address is read through validFillPlan and validRoomTeams, and the line under START says what the bots do (gather food, raid and fight back)" "$FILL_LINKS"
# the room's teams are a part of its create block (protocol 15, they were a word of the code in protocol 13): the card puts them into the block of the room that it makes, the page reads the block of an
# address (roomBlockOf), and the links carry &teams= only for a room whose block names none
TEAM_BLOCK=1
if grep -qF "var wantedBlock = roomBlockOf(window.location.search);" "$FOUR_PAGE" && grep -qF "var roomTeam = hostTeam(validRoomTeams(teams), players);" "$FOUR_PAGE" \
    && grep -qF "startRoom(randomCode(), { map: m.key, seats: players, teams: roomTeam === 'ffa' ? '' : roomTeam, leaderStart: false }, play, hostFillText(seats, players), '');" "$FOUR_PAGE" \
    && grep -qF "var shown = shownRoomTeams(named, seatCount);" "$FOUR_PAGE" && grep -qF "roomTeams = teamsInBlock ? shown : (named ? '' : validRoomTeams(teams));" "$FOUR_PAGE" && grep -qF "joinUrl(room, roomBlock, checked.name, fill, teamsInBlock ? '' : roomTeams)" "$FOUR_PAGE" \
    && grep -qF "hostTeam(wantedTeams, wantedBlock.seats).replace('ffa', '')" "$FOUR_PAGE" && grep -qF "roomBlockQuery(cardBlock(state)) + '&seat='" "$FOUR_PAGE"; then TEAM_BLOCK=0; fi
check "web/lobby.html puts the teams that the Team 1 and Team 2 switches make into the room's create block (roommap, roomseats and roomteams in every link; the card's room is always for four seats), reads the block of an address (roomBlockOf), lets the block's teams win over the address's &teams=, and narrows an address's &teams= to the seats that the block names (tests/scripts/web_name_check.js and web_lobby_check.js run it)" "$TEAM_BLOCK"
FILL_SHELL=1
if grep -qF "out.args.push('--fill-bots', fill)" "$SHELL_PAGE" && grep -qF "var fill = antsFillPlanArg(q.get('fill'));" "$SHELL_PAGE" && grep -qF "out.args.push('--teams', teams)" "$SHELL_PAGE" && grep -qF "var teams = antsTeamsArg(q.get('teams'));" "$SHELL_PAGE" \
    && grep -qF "out.args.push('--start-when', start)" "$SHELL_PAGE" && grep -qF "var start = antsStartArg(q.get('start'));" "$SHELL_PAGE"; then FILL_SHELL=0; fi
check "web/shell.html gives the game --fill-bots, --teams and --start-when from antsFillPlanArg's, antsTeamsArg's and antsStartArg's answers and nothing else (the lines that take the address's text and the lines that hand it to the game)" "$FILL_SHELL"
# The validation itself is RUN, not read: the functions are cut out of the pages and given a table of addresses' values (the three words in any case, four words for the seats, a team of two seats; empty,
# other words, spaces, line ends, look-alikes, an argument smuggled behind a word, a very long text, values that are no text): shell.html's antsFillArg, antsFillPlanArg and antsTeamsArg and lobby.html's
# validFill and validFillPlan must answer the lower case text or nothing
if command -v node > /dev/null 2>&1; then
    FILL_RUN="$(node "$ROOT/tests/scripts/web_fill_check.js" "$SHELL_PAGE" "$FOUR_PAGE" 2>&1)"
    FILL_RUN_RC=$?
    [ "$FILL_RUN_RC" -ne 0 ] && echo "$FILL_RUN" | sed 's/^/    /'
    check "the pages' own code for ?fill=, ?teams= and ?start= (web/shell.html antsFillArg, antsFillPlanArg, antsTeamsArg and antsStartArg, web/lobby.html validFill and validFillPlan), run with node on tables of values: the levels in any case and the lists of four give the lower case text, a team of two seats gives A+B, one digit 1 - 4 gives the people to wait for, everything else gives nothing" "$FILL_RUN_RC"
else
    echo "  SKIP: node is not installed: the validation code of ?fill= and ?teams= in web/shell.html and web/lobby.html was NOT run (tests/scripts/web_fill_check.js)"
fi
check "web/lobby.html tells what happens to a hidden or covered window (the match does not wait for it, a lagging notice after 3 s, dropped after 30 s without a sign of life, cannot come back) and no longer says that the match waits for it (it did not since v0.0.94)" "$([ "$(grep -c 'The match does not wait for it' "$ROOT/web/lobby.html")" -eq 1 ] && grep -q 'dropped from the match and cannot come back' "$ROOT/web/lobby.html" && ! grep -q 'and the match waits for it' "$ROOT/web/lobby.html"; echo $?)"
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
# lobby rooms (protocol 16, docs/SERVER.md "Lobby rooms"): --demo-lobbies N needs the public rooms and reconnect and leaves a place for the control interface; without it a server with public rooms offers 200 (fewer where --max-rooms leaves no more room)
log_of() { { perl -e 'alarm 3; exec @ARGV' env ANTS_SERVER_SECRET=x "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$(free_port)" "$@" 2>&1; } 2> /dev/null; }       # (the server's log; the shell's notice of the alarm is not shown)
check "--demo-lobbies without --demo-rooms is refused" "$([ "$(exit_of --demo-lobbies 5)" = "2" ]; echo $?)"
check "--demo-lobbies that is no number is refused" "$([ "$(exit_of --demo-rooms 2 --demo-map TINY.LVL --demo-lobbies many)" = "2" ]; echo $?)"
check "--demo-lobbies below 0 is refused" "$([ "$(exit_of --demo-rooms 2 --demo-map TINY.LVL --demo-lobbies -1)" = "2" ]; echo $?)"
check "--demo-lobbies with --no-reconnect is refused (a lobby holds the seat of a link that dropped)" "$([ "$(exit_of --demo-rooms 2 --demo-map TINY.LVL --no-reconnect --demo-lobbies 5)" = "2" ]; echo $?)"
check "--demo-rooms and --demo-lobbies together as many as --max-rooms are refused (rooms of the control interface keep their places)" "$([ "$(exit_of --demo-rooms 2 --demo-map TINY.LVL --demo-lobbies 3 --max-rooms 5)" = "2" ]; echo $?)"
check "... one fewer starts the server (the alarm ends it: status 142)" "$([ "$(exit_of --demo-rooms 2 --demo-map TINY.LVL --demo-lobbies 2 --max-rooms 5)" = "142" ]; echo $?)"
check "--demo-lobbies 0 needs no public rooms and no reconnect (it switches lobby rooms off)" "$([ "$(exit_of --no-reconnect --demo-lobbies 0)" = "142" ]; echo $?)"
check "--demo-lobbies without a value is refused" "$([ "$(exit_of --demo-rooms 2 --demo-map TINY.LVL --demo-lobbies)" = "2" ]; echo $?)"
check "public rooms alone offer 200 lobby rooms (the log says so)" "$(log_of --demo-rooms 5 --demo-map TINY.LVL | grep -q 'lobby rooms on: up to 200 waiting'; echo $?)"
check "... fewer where --max-rooms leaves no more room (10 rooms, 5 public: 4 lobbies, one place for the control interface)" "$(log_of --demo-rooms 5 --demo-map TINY.LVL --max-rooms 10 | grep -q 'lobby rooms on: up to 4 waiting'; echo $?)"
check "... none where it leaves none, and the log says why" "$(log_of --demo-rooms 9 --demo-map TINY.LVL --max-rooms 10 | grep -q 'lobby rooms off (--max-rooms leaves no place for them beside --demo-rooms)'; echo $?)"
check "... none with --no-reconnect, and the log says why" "$(log_of --demo-rooms 5 --demo-map TINY.LVL --no-reconnect | grep -q 'lobby rooms off (--no-reconnect)'; echo $?)"
check "--demo-lobbies N is the number of the log" "$(log_of --demo-rooms 5 --demo-map TINY.LVL --demo-lobbies 7 | grep -q 'lobby rooms on: up to 7 waiting'; echo $?)"
check "--demo-lobbies 0 switches them off, and the log says so" "$(log_of --demo-rooms 5 --demo-map TINY.LVL --demo-lobbies 0 | grep -q 'lobby rooms off (--demo-lobbies 0)'; echo $?)"
check "--help names --demo-lobbies" "$("$SERVER" --help 2>&1 | grep -q -- '--demo-lobbies N'; echo $?)"
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
# the restart records (docs/NETWORK_PORT.md "Restart records"): the options and their edges
check "--restart-vote-seconds below 30 is refused" "$([ "$(exit_of --restart-vote-seconds 29)" = "2" ]; echo $?)"
check "--restart-vote-seconds above 3600 is refused" "$([ "$(exit_of --restart-vote-seconds 3601)" = "2" ]; echo $?)"
check "--restart-vote-seconds that is no number is refused" "$([ "$(exit_of --restart-vote-seconds soon)" = "2" ]; echo $?)"
check "--restart-vote-seconds without a value is refused" "$([ "$(exit_of --restart-vote-seconds)" = "2" ]; echo $?)"
check "--restart-budget-mb 0 is refused" "$([ "$(exit_of --restart-budget-mb 0)" = "2" ]; echo $?)"
check "--restart-budget-mb above 4096 is refused" "$([ "$(exit_of --restart-budget-mb 4097)" = "2" ]; echo $?)"
check "--restart-budget-mb that is no number is refused" "$([ "$(exit_of --restart-budget-mb lots)" = "2" ]; echo $?)"
check "--restart-dir with an empty folder name is refused" "$([ "$(exit_of --restart-dir '')" = "2" ]; echo $?)"
check "--restart-dir without a value is refused" "$([ "$(exit_of --restart-dir)" = "2" ]; echo $?)"
check "--restart-dir together with --no-restart-records is refused" "$([ "$(exit_of --restart-dir "$WORK/rd" --no-restart-records)" = "2" ]; echo $?)"
echo "not a folder" > "$WORK/a_file"
check "--restart-dir that names a file stops the server (status 1: the operator asked for that folder)" "$([ "$(exit_of --restart-dir "$WORK/a_file")" = "1" ]; echo $?)"
check "--restart-dir that is a folder below a file stops it too" "$([ "$(exit_of --restart-dir "$WORK/a_file/below")" = "1" ]; echo $?)"
check "the edges of the restart options start the server: 30 and 1" "$([ "$(exit_of --restart-vote-seconds 30 --restart-budget-mb 1 --restart-dir "$WORK/rd_a")" = "142" ]; echo $?)"
check "... and 3600 and 4096" "$([ "$(exit_of --restart-vote-seconds 3600 --restart-budget-mb 4096 --no-restart-records)" = "142" ]; echo $?)"
check "--restart-dir makes its folder, for its owner only (mode 700)" "$([ -d "$WORK/rd_a" ] && [ "$(stat -c %a "$WORK/rd_a" 2> /dev/null || stat -f %Lp "$WORK/rd_a")" = "700" ]; echo $?)"
check "--help names the restart options and their ranges" "$("$SERVER" --help 2>&1 | grep -q -- '--restart-dir DIR | --no-restart-records' && "$SERVER" --help 2>&1 | grep -q -- '--restart-vote-seconds 30-3600' && "$SERVER" --help 2>&1 | grep -q -- '--restart-budget-mb 1-4096'; echo $?)"
# the replays (docs/SERVER.md "Replays"): the options and their edges
check "--replays-days 0 is refused" "$([ "$(exit_of --replays-days 0)" = "2" ]; echo $?)"
check "--replays-days above 3650 is refused" "$([ "$(exit_of --replays-days 3651)" = "2" ]; echo $?)"
check "--replays-days that is no number is refused" "$([ "$(exit_of --replays-days soon)" = "2" ]; echo $?)"
check "--replays-days without a value is refused" "$([ "$(exit_of --replays-days)" = "2" ]; echo $?)"
check "--replays-max-mb 0 is refused" "$([ "$(exit_of --replays-max-mb 0)" = "2" ]; echo $?)"
check "--replays-max-mb above 4096 is refused" "$([ "$(exit_of --replays-max-mb 4097)" = "2" ]; echo $?)"
check "--replays-max-mb that is no number is refused" "$([ "$(exit_of --replays-max-mb lots)" = "2" ]; echo $?)"
check "--replay-port that is no port is refused" "$([ "$(exit_of --replay-port web)" = "2" ]; echo $?)"
check "--replay-port above 65535 is refused" "$([ "$(exit_of --replay-port 65536)" = "2" ]; echo $?)"
check "--replay-port without a value is refused" "$([ "$(exit_of --replay-port)" = "2" ]; echo $?)"
check "--replays-dir with an empty folder name is refused" "$([ "$(exit_of --replays-dir '')" = "2" ]; echo $?)"
check "--replays-dir together with --no-replays is refused" "$([ "$(exit_of --replays-dir "$WORK/rp_none" --no-replays)" = "2" ]; echo $?)"
check "--replays-dir that names a file stops the server (status 1: the operator asked for that folder)" "$([ "$(exit_of --replays-dir "$WORK/a_file")" = "1" ]; echo $?)"
check "--replays-dir that is a folder below a file stops it too" "$([ "$(exit_of --replays-dir "$WORK/a_file/below")" = "1" ]; echo $?)"
check "the edges of the replay options start the server: 1 day and 1 MiB" "$([ "$(exit_of --replays-days 1 --replays-max-mb 1 --replays-dir "$WORK/rp_a")" = "142" ]; echo $?)"
check "... and 3650 days and 4096 MiB, with the replays switched off" "$([ "$(exit_of --replays-days 3650 --replays-max-mb 4096 --no-replays)" = "142" ]; echo $?)"
check "... and --replay-port 0 (no public door: the default) starts it" "$([ "$(exit_of --replay-port 0)" = "142" ]; echo $?)"
check "... and so does a public door on a free port of this machine" "$([ "$(exit_of --replay-port "$(free_port)")" = "142" ]; echo $?)"
check "--replays-dir makes its folder (the server's own, it is written to when a match ends)" "$([ -d "$WORK/rp_a" ]; echo $?)"
check "--help names the replay options and their ranges" "$("$SERVER" --help 2>&1 | grep -q -- '--replays-dir DIR | --no-replays' && "$SERVER" --help 2>&1 | grep -q -- '--replays-days 1-3650' && "$SERVER" --help 2>&1 | grep -q -- '--replays-max-mb 1-4096' && "$SERVER" --help 2>&1 | grep -q -- '--replay-port N' && "$SERVER" --help 2>&1 | grep -q -- '--replay-demo'; echo $?)"
mkdir -p "$WORK/maps_odd"
cp "$ROOT/Original-Ants/Maps/TINY.LVL" "$WORK/maps_odd/TINY.LVL"
cp "$ROOT/Original-Ants/Maps/TINY.LVL" "$WORK/maps_odd/A B.LVL"
env ANTS_SERVER_SECRET=x perl -e 'alarm 3; exec @ARGV' "$SERVER" --maps "$WORK/maps_odd" --port "$(free_port)" --demo-rooms 2 --demo-map TINY.LVL --demo-maps "TINY.LVL,A B.LVL" > "$WORK/odd.log" 2>&1
ODD_STATUS=$?
check "a listed map with a blank in its name starts the server (a create block names a map by its file name, as it is: nothing about a map can be unchoosable) and the startup line lists the choices" "$([ "$ODD_STATUS" = "142" ] && grep -q "unless the block names one of TINY.LVL, A B.LVL" "$WORK/odd.log" && ! grep -q "never be chosen" "$WORK/odd.log"; echo $?)"
fi

# ---- part rooms: the control secret, a room by code, the leader, bots that fill the seats, chat, flood control, closing a room, SIGTERM ---------------------------
if part_enabled rooms; then
WS_PORT="$(free_port)"
ANTS_SERVER_SECRET="$SECRET" "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$GAME_PORT" --ws-port "$WS_PORT" --ctl-port "$CTL_PORT" --results-dir "$WORK/results" > "$WORK/server.log" 2>&1 &
SERVER_PID=$!
UP=1
for _ in $(seq 1 50); do
    if curl -s -m 1 "$CTL/healthz" | grep -q '"ok"'; then UP=0; break; fi
    kill -0 "$SERVER_PID" 2> /dev/null || break
    sleep 0.1
done
check "the server is up and answers /healthz without a secret" "$UP"
# the busy answer (GET /busy on the WebSocket port; the site's nginx routes /busy to it): public, read-only, plain counts, GET only; the deploy job of CI waits for matches to be 0
BUSY="http://127.0.0.1:$WS_PORT/busy"
busy_json_ok() {      # busy_json_ok MATCHES PLAYERS: the answer is 200 JSON that nobody may cache, with exactly these two counts and nothing else (no name, no code, no secret)
    local head body
    head="$(curl -s -i -m 3 "$BUSY" | tr -d '\r')"
    body="$(curl -s -m 3 "$BUSY")"
    echo "$head" | head -1 | grep -q '^HTTP/1.1 200 OK$' && echo "$head" | grep -qi '^content-type: application/json$' && echo "$head" | grep -qi '^cache-control: no-store$' &&
        echo "$body" | python3 -c 'import sys, json; d = json.load(sys.stdin); sys.exit(0 if sorted(d) == ["matches", "players"] and d["matches"] == int(sys.argv[1]) and d["players"] == int(sys.argv[2]) else 1)' "$1" "$2"
}
check "GET /busy on the WebSocket port needs no secret and says 200 JSON, no-store, {\"matches\":0,\"players\":0} while nothing runs" "$(busy_json_ok 0 0; echo $?)"
check "the busy answer is GET only (a POST is 405) and takes no parameter (a query is no status request: 426), and no other path answers it" "$([ "$(code_of -X POST "$BUSY")" = "405" ] && [ "$(code_of "$BUSY?x=1")" = "426" ] && [ "$(code_of "http://127.0.0.1:$WS_PORT/busy/")" = "426" ] && [ "$(code_of "http://127.0.0.1:$WS_PORT/rooms")" = "426" ]; echo $?)"
# the site statistics (GET /stats and POST /stats/local on the WebSocket port, which the site's nginx routes like /busy): the games that ended here and the single-player games that browsers report, numbers only
STATS="http://127.0.0.1:$WS_PORT/stats"
LOCAL="http://127.0.0.1:$WS_PORT/stats/local"
stats_check() {      # stats_check ONLINE_DAY ONLINE_TOTAL LOCAL_DAY LOCAL_TOTAL [URL]: 200 JSON that nobody may cache, exactly now / online / local / since, the dates of one shape, and these four numbers
    local head body
    head="$(curl -s -i -m 3 "${5:-$STATS}" | tr -d '\r')"
    body="$(curl -s -m 3 "${5:-$STATS}")"
    echo "$head" | head -1 | grep -q '^HTTP/1.1 200 OK$' && echo "$head" | grep -qi '^content-type: application/json$' && echo "$head" | grep -qi '^cache-control: no-store$' &&
        echo "$body" | python3 -c '
import sys, json, re
d = json.load(sys.stdin)
ok = sorted(d) == ["local", "now", "online", "since"] and sorted(d["now"]) == ["matches", "players"] and sorted(d["online"]) == ["day", "total"] and sorted(d["local"]) == ["day", "total"]
ok = ok and re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}", d["since"]) is not None
ok = ok and [d["online"]["day"], d["online"]["total"], d["local"]["day"], d["local"]["total"]] == [int(a) for a in sys.argv[1:5]]
sys.exit(0 if ok else 1)' "$1" "$2" "$3" "$4"
}
stat_of() { curl -s -m 3 "${3:-$STATS}" | python3 -c 'import sys, json; print(json.load(sys.stdin)[sys.argv[1]][sys.argv[2]])' "$1" "$2" 2> /dev/null; }      # stat_of online|local day|total [URL]
check "GET /stats needs no secret and says 200 JSON, no-store, with exactly now, online, local and since, and nothing counted yet" "$(stats_check 0 0 0 0; echo $?)"
check "GET /stats says what /busy says in its now" "$(python3 -c 'import sys, json; d = json.load(open(sys.argv[1])); b = json.load(open(sys.argv[2])); sys.exit(0 if d["now"] == b else 1)' <(curl -s -m 3 "$STATS") <(curl -s -m 3 "$BUSY"); echo $?)"
POST_OUT="$(curl -s -i -m 3 -X POST "$LOCAL" | tr -d '\r')"
check "POST /stats/local is answered 204 with nothing in it (no body, no Content-Length)" "$(echo "$POST_OUT" | head -1 | grep -q '^HTTP/1.1 204 No Content$' && [ -z "$(echo "$POST_OUT" | sed '1,/^$/d')" ] && ! echo "$POST_OUT" | grep -qi '^content-length'; echo $?)"
check "... and counts one single-player game (a day and in all), and no online one" "$(stats_check 0 0 1 1; echo $?)"
check "a POST with a body is 400, with a query 405 (it is no counting request), a GET of the path 405, a POST of /stats 405, and none of them counts" "$([ "$(code_of -X POST -d 'x' "$LOCAL")" = "400" ] && [ "$(code_of -X POST "$LOCAL?x=1")" = "405" ] && [ "$(code_of "$LOCAL")" = "405" ] && [ "$(code_of -X POST "$STATS")" = "405" ] && [ "$(code_of "$STATS?x=1")" = "426" ] && stats_check 0 0 1 1; echo $?)"
CAP_CODES="$(for _ in $(seq 1 130); do code_of -X POST "$LOCAL"; echo; done | sort | uniq -c | tr -s ' ' | tr '\n' ' ')"
check "the cap: 131 reports in a few seconds are all answered 204 (the cap cannot be seen: $CAP_CODES) and 120 of them are counted (at most 120 a minute)" "$([ "$CAP_CODES" = " 130 204 " ] && stats_check 0 0 120 120; echo $?)"
[ "$UP" -ne 0 ] && { cat "$WORK/server.log"; echo "server e2e: $CHECKS checks, $FAILS failures"; exit 1; }

# the secret
check "no secret: 401" "$([ "$(code_of "$CTL/rooms")" = "401" ]; echo $?)"
check "a wrong secret: 401" "$([ "$(code_of -H "Authorization: Bearer wrong-$SECRET" "$CTL/rooms")" = "401" ]; echo $?)"
check "the secret with a character missing: 401" "$([ "$(code_of -H "Authorization: Bearer ${SECRET%?}" "$CTL/rooms")" = "401" ]; echo $?)"
check "the right secret lists the rooms: 200" "$([ "$(code_of -H "Authorization: Bearer $SECRET" "$CTL/rooms")" = "200" ]; echo $?)"
check "a room cannot be made without the secret: 401" "$([ "$(code_of -X POST -d '{"map":"TINY.LVL"}' "$CTL/rooms")" = "401" ]; echo $?)"

# the room
RESP="$(curl -s -m 3 -X POST -H "Authorization: Bearer $SECRET" -d "{\"map\":\"TINY.LVL\",\"players\":2,\"code\":\"$CODE\",\"seed\":99}" "$CTL/rooms")"
check "the room is made and is waiting" "$(echo "$RESP" | grep -q '"state":"waiting"'; echo $?)"
check "a map outside the folder is refused: 404" "$([ "$(code_of -X POST -H "Authorization: Bearer $SECRET" -d '{"map":"../../etc/passwd"}' "$CTL/rooms")" = "404" ]; echo $?)"

# two real clients (headless: they run until this script stops them; their frames are not paced, so a few thousand would be over before the dialog's 5 s are)
CLIENT_PIDS=""
for i in 1 2; do
    "$GAME" --headless --no-lan --name "Player$i" --join "127.0.0.1:$GAME_PORT" --room "$CODE" --screenshot "$WORK/c$i.png" --frames 4000000 > "$WORK/c$i.log" 2>&1 &
    CLIENT_PIDS="$CLIENT_PIDS $!"
done
RUNNING=1
for _ in $(seq 1 150); do
    STATUS="$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "$CTL/rooms/$CODE")"
    if echo "$STATUS" | grep -q '"state":"running"'; then RUNNING=0; break; fi
    sleep 0.2
done
RUN_SEEN_AT="$(python3 -c 'import time; print(time.time())')"
check "both clients joined and the match started by itself (state running)" "$RUNNING"
check "both players are in the room list" "$(echo "$STATUS" | grep -q 'Player1' && echo "$STATUS" | grep -q 'Player2'; echo $?)"
check "while their match runs /busy says one match and two people" "$(busy_json_ok 1 2; echo $?)"
check "... and /stats says the same in its now" "$(curl -s -m 3 "$STATS" | python3 -c 'import sys, json; sys.exit(0 if json.load(sys.stdin)["now"] == {"matches": 1, "players": 2} else 1)'; echo $?)"
TICKS="$(wait_ticks "$CODE")"
DIALOG_WAIT="$(python3 -c "import time; print(round(time.time() - $RUN_SEEN_AT, 1))")"
check "the referee's clock runs (ticks after the dialog: $TICKS)" "$([ "${TICKS:-0}" -gt 0 ]; echo $?)"
# (the lower bound is the point: nothing ticks during the dialog; the 5 s of it are 4.8 s from the moment this script saw "running", the bound has two seconds of slack for a loaded machine)
check "the referee's clock waits for the \"Get ready to play!\" dialog: its first tick came $DIALOG_WAIT s after the match began (5 s; never under 3, within 15)" "$(python3 -c "print(0 if 3.0 <= $DIALOG_WAIT <= 15.0 else 1)")"
sleep 3      # (a few seconds of play: the clients compare their state hashes every second, a desync would be reported within it)
for p in $CLIENT_PIDS; do kill "$p" 2> /dev/null; done
for p in $CLIENT_PIDS; do wait "$p" 2> /dev/null; done
check "no client reported an error" "$(grep -qiE 'out of sync|failed|error' "$WORK/c1.log" "$WORK/c2.log"; [ $? -ne 0 ]; echo $?)"

# the leader of a room (protocol 7): a room for four, two headless clients; the first one to join leads the room and presses START itself (--start-when 2: a headless client has
# nobody to click) once the second one is in: the match starts with the two of them, the room keeps what was asked for (four) and shows who joined (two)
LEAD="E2E-LEAD-$RANDOM"
RESP="$(curl -s -m 3 -X POST -H "Authorization: Bearer $SECRET" -d "{\"map\":\"TINY.LVL\",\"players\":4,\"code\":\"$LEAD\",\"seed\":7}" "$CTL/rooms")"
check "a room for four is made: it allows an early start and has no leader yet" "$(echo "$RESP" | grep -q '"early_start":true' && echo "$RESP" | grep -q '"leader":null'; echo $?)"
"$GAME" --headless --no-lan --name First --join "127.0.0.1:$GAME_PORT" --room "$LEAD" --start-when 2 --screenshot "$WORK/l1.png" --frames 4000000 > "$WORK/l1.log" 2>&1 &
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
"$GAME" --headless --no-lan --name Second --join "127.0.0.1:$GAME_PORT" --room "$LEAD" --screenshot "$WORK/l2.png" --frames 4000000 > "$WORK/l2.log" 2>&1 &
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
TICKS="$(wait_ticks "$LEAD")"
check "the referee's clock runs for the two of them (ticks after the dialog: $TICKS)" "$([ "${TICKS:-0}" -gt 0 ]; echo $?)"
check "neither client reported an error" "$(grep -qiE 'out of sync|failed|error' "$WORK/l1.log" "$WORK/l2.log"; [ $? -ne 0 ]; echo $?)"
check "the room did not fail (no desync)" "$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "$CTL/rooms/$LEAD" | grep -q '"state":"running"'; echo $?)"
ONLINE_BEFORE="$(stat_of online total)"
LEAD_TICKS="$(ticks_of "$LEAD")"
for p in $LEAD_PIDS; do kill "$p" 2> /dev/null; done
for p in $LEAD_PIDS; do wait "$p" 2> /dev/null; done
code_of -X DELETE -H "Authorization: Bearer $SECRET" "$CTL/rooms/$LEAD" > /dev/null
sleep 0.3
check "the leader's room, whose match ran for seconds only ($LEAD_TICKS ticks, under 600), ended (its players left, then the owner closed it): it counts nothing (a start-and-quit loop cannot pad the number)" "$([ "${LEAD_TICKS:-0}" -gt 0 ] && [ "${LEAD_TICKS:-0}" -lt 600 ] && [ "$(stat_of online total)" = "$ONLINE_BEFORE" ] && [ "$(stat_of online day)" = "$ONLINE_BEFORE" ]; echo $?)"

# bots fill the empty seats (protocol 11): a room for four whose leader (a headless client with --fill-bots medium, --start-when 1: a test hook that presses START once one player is
# in) starts it ALONE: the server seats a "Bot (Medium)" in each of the three empty seats and runs them, the status lists them, the match runs and the client reports no error; two
# clients that chat in the waiting room (--say: a test hook that says a line once two players are in) hear each other
FILL="E2E-FILL-$RANDOM"
curl -s -m 3 -o /dev/null -X POST -H "Authorization: Bearer $SECRET" -d "{\"map\":\"TINY.LVL\",\"players\":4,\"code\":\"$FILL\",\"seed\":13}" "$CTL/rooms"
"$GAME" --headless --no-lan --name Solo --join "127.0.0.1:$GAME_PORT" --room "$FILL" --fill-bots medium --start-when 1 --screenshot "$WORK/f1.png" --frames 4000000 > "$WORK/f1.log" 2>&1 &
FILL_PIDS="$!"
FILL_UP=1
for _ in $(seq 1 150); do
    STATUS="$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "$CTL/rooms/$FILL")"
    if echo "$STATUS" | grep -q '"state":"running"'; then FILL_UP=0; break; fi
    sleep 0.2
done
check "alone with --fill-bots medium the leader starts a room for four: the match runs" "$FILL_UP"
BOTS_OK="$(echo "$STATUS" | python3 -c '
import sys, json
r = json.load(sys.stdin)
bots = r.get("bots", [])
ok = len(bots) == 3 and [b["seat"] for b in bots] == [1, 2, 3] and all(b["bot"] == "medium" and b["name"] == "Bot (Medium)" and b["fill"] for b in bots)
ok = ok and r.get("joined") == 4 and r.get("expected") == 4 and r["players"][0]["name"] == "Solo" and sum(1 for p in r["players"] if p["name"] == "Bot (Medium)") == 3
print(0 if ok else 1)' 2> /dev/null)"
check "the status lists three bots on seats 1 - 3 (medium, named Bot (Medium), seated by the fill), four players, the person on seat 0" "${BOTS_OK:-1}"
FILL_T1="$(wait_ticks "$FILL")"      # (the rate is measured from the first tick: the match's first 5 s are the dialog)
sleep 3
FILL_T2="$(ticks_of "$FILL")"
FILL_RATE="$(python3 -c "print(($FILL_T2 - $FILL_T1) / 3.0)" 2> /dev/null)"
check "the referee runs the match with the bots at 20 ticks a second (measured: ${FILL_RATE:-?})" "$(python3 -c "import sys; r = float('${FILL_RATE:-0}'); sys.exit(0 if 17.0 <= r <= 23.0 else 1)"; echo $?)"
check "the room did not fail (no desync with the bots) and the client reported no error" "$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "$CTL/rooms/$FILL" | grep -q '"state":"running"' && ! grep -qiE 'out of sync|failed|error' "$WORK/f1.log"; echo $?)"
for p in $FILL_PIDS; do kill "$p" 2> /dev/null; done
for p in $FILL_PIDS; do wait "$p" 2> /dev/null; done
code_of -X DELETE -H "Authorization: Bearer $SECRET" "$CTL/rooms/$FILL" > /dev/null

# a level for each seat and the teams (protocol 13): the leader (--fill-bots none,easy,none,hard --teams 0+1, --start-when 1) starts a room for four ALONE: the server seats a "Bot (Easy)" at seat 1 and a
# "Bot (Hard)" at seat 3 and leaves seat 2 empty, and the match starts with Green and Red as a team: the referee and the leader's game both make it before the first tick (a disagreement would be a
# desync, and the room would fail), the match runs at 20 ticks a second and the client reports no error
PLAN="E2E-PLAN-$RANDOM"
curl -s -m 3 -o /dev/null -X POST -H "Authorization: Bearer $SECRET" -d "{\"map\":\"TINY.LVL\",\"players\":4,\"code\":\"$PLAN\",\"seed\":13}" "$CTL/rooms"
"$GAME" --headless --no-lan --name Solo --join "127.0.0.1:$GAME_PORT" --room "$PLAN" --fill-bots none,easy,none,hard --teams 0+1 --start-when 1 --screenshot "$WORK/p1.png" --frames 4000000 > "$WORK/p1.log" 2>&1 &
PLAN_PIDS="$!"
PLAN_UP=1
for _ in $(seq 1 150); do
    STATUS="$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "$CTL/rooms/$PLAN")"
    if echo "$STATUS" | grep -q '"state":"running"'; then PLAN_UP=0; break; fi
    sleep 0.2
done
check "alone with --fill-bots none,easy,none,hard --teams 0+1 the leader starts a room for four: the match runs" "$PLAN_UP"
PLAN_OK="$(echo "$STATUS" | python3 -c '
import sys, json
r = json.load(sys.stdin)
bots = r.get("bots", [])
ok = [b["seat"] for b in bots] == [1, 3] and [b["bot"] for b in bots] == ["easy", "hard"] and [b["name"] for b in bots] == ["Bot (Easy)", "Bot (Hard)"] and all(b["fill"] for b in bots)
ok = ok and r.get("joined") == 3 and {p["seat"]: p["name"] for p in r["players"]} == {0: "Solo", 1: "Bot (Easy)", 3: "Bot (Hard)"}
print(0 if ok else 1)' 2> /dev/null)"
check "the status lists the bots of the seats 1 (easy) and 3 (hard) by their own names, three players, and seat 2 stays empty" "${PLAN_OK:-1}"
PLAN_T1="$(wait_ticks "$PLAN")"
sleep 3
PLAN_T2="$(ticks_of "$PLAN")"
PLAN_RATE="$(python3 -c "print(($PLAN_T2 - $PLAN_T1) / 3.0)" 2> /dev/null)"
check "the referee runs the match with the two bots and the team at 20 ticks a second (measured: ${PLAN_RATE:-?})" "$(python3 -c "import sys; r = float('${PLAN_RATE:-0}'); sys.exit(0 if 17.0 <= r <= 23.0 else 1)"; echo $?)"
check "the room did not fail (the referee and the leader made the same team: no desync) and the client reported no error" "$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "$CTL/rooms/$PLAN" | grep -q '"state":"running"' && ! grep -qiE 'out of sync|failed|error' "$WORK/p1.log"; echo $?)"
for p in $PLAN_PIDS; do kill "$p" 2> /dev/null; done
for p in $PLAN_PIDS; do wait "$p" 2> /dev/null; done
code_of -X DELETE -H "Authorization: Bearer $SECRET" "$CTL/rooms/$PLAN" > /dev/null

# the front page's one card, START with bots only: the game page gives the leader the colour that was picked (--seat 2, Blue), the plan of the card (easy,medium,none,none: a bot at Green and one at Red; none
# for You, a Friend and Nobody) and --start-when 1. A room for four started by its leader alone seats the bots at the seats 0 and 1 (a bot at Green too), the leader holds seat 2, seat 3 stays empty
CARD="E2E-CARD-$RANDOM"
curl -s -m 3 -o /dev/null -X POST -H "Authorization: Bearer $SECRET" -d "{\"map\":\"TINY.LVL\",\"players\":4,\"code\":\"$CARD\",\"seed\":19}" "$CTL/rooms"
"$GAME" --headless --no-lan --name Blue --join "127.0.0.1:$GAME_PORT" --room "$CARD" --seat 2 --fill-bots easy,medium,none,none --start-when 1 --screenshot "$WORK/c1.png" --frames 4000000 > "$WORK/c1.log" 2>&1 &
CARD_PIDS="$!"
CARD_UP=1
for _ in $(seq 1 150); do
    STATUS="$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "$CTL/rooms/$CARD")"
    if echo "$STATUS" | grep -q '"state":"running"'; then CARD_UP=0; break; fi
    sleep 0.2
done
check "alone at Blue with --seat 2 --fill-bots easy,medium,none,none --start-when 1 the leader starts a room for four at once: the match runs" "$CARD_UP"
CARD_OK="$(echo "$STATUS" | python3 -c '
import sys, json
r = json.load(sys.stdin)
bots = r.get("bots", [])
ok = [b["seat"] for b in bots] == [0, 1] and [b["bot"] for b in bots] == ["easy", "medium"] and [b["name"] for b in bots] == ["Bot (Easy)", "Bot (Medium)"] and all(b["fill"] for b in bots)
ok = ok and r.get("joined") == 3 and {p["seat"]: p["name"] for p in r["players"]} == {0: "Bot (Easy)", 1: "Bot (Medium)", 2: "Blue"}
print(0 if ok else 1)' 2> /dev/null)"
check "the leader holds the seat that it asked for (Blue), the bots of the plan are at Green (easy) and Red (medium) by their own names, and seat 3 stays empty" "${CARD_OK:-1}"
check "the room did not fail and the client reported no error" "$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "$CTL/rooms/$CARD" | grep -q '"state":"running"' && ! grep -qiE 'out of sync|failed|error' "$WORK/c1.log"; echo $?)"
for p in $CARD_PIDS; do kill "$p" 2> /dev/null; done
for p in $CARD_PIDS; do wait "$p" 2> /dev/null; done
code_of -X DELETE -H "Authorization: Bearer $SECRET" "$CTL/rooms/$CARD" > /dev/null

CHAT="E2E-CHAT-$RANDOM"
curl -s -m 3 -o /dev/null -X POST -H "Authorization: Bearer $SECRET" -d "{\"map\":\"TINY.LVL\",\"players\":4,\"code\":\"$CHAT\",\"seed\":17}" "$CTL/rooms"
"$GAME" --headless --no-lan --name Ann --join "127.0.0.1:$GAME_PORT" --room "$CHAT" --say "hello from Ann" --screenshot "$WORK/ch1.png" --frames 4000000 > "$WORK/ch1.log" 2>&1 &
CHAT_PIDS="$!"
sleep 1
"$GAME" --headless --no-lan --name Bob --join "127.0.0.1:$GAME_PORT" --room "$CHAT" --say "hello from Bob" --screenshot "$WORK/ch2.png" --frames 4000000 > "$WORK/ch2.log" 2>&1 &
CHAT_PIDS="$CHAT_PIDS $!"
HEARD=1
for _ in $(seq 1 100); do
    if grep -q "Room chat: Bob: hello from Bob" "$WORK/ch1.log" && grep -q "Room chat: Ann: hello from Ann" "$WORK/ch2.log"; then HEARD=0; break; fi
    sleep 0.2
done
check "two clients chat in the waiting room: each hears the other's line from the room, with the sender's name" "$HEARD"
check "each also hears its own line from the room (the room tells the sender that it was heard)" "$(grep -q 'Room chat: Ann: hello from Ann' "$WORK/ch1.log" && grep -q 'Room chat: Bob: hello from Bob' "$WORK/ch2.log"; echo $?)"
check "nobody started the match by chatting: the room still waits" "$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "$CTL/rooms/$CHAT" | grep -q '"state":"waiting"'; echo $?)"
ONLINE_BEFORE="$(stat_of online total)"
for p in $CHAT_PIDS; do kill "$p" 2> /dev/null; done
for p in $CHAT_PIDS; do wait "$p" 2> /dev/null; done
code_of -X DELETE -H "Authorization: Bearer $SECRET" "$CTL/rooms/$CHAT" > /dev/null
sleep 0.3
check "the room in which two players only chatted (no match began) was closed: it counts nothing" "$([ "$(stat_of online total)" = "$ONLINE_BEFORE" ]; echo $?)"

# flood control (v0.0.93): a raw client that is no game sends valid messages as fast as its line allows (a StartRequest that is ignored, a Ping), in a room of its own while two real
# clients play in another. Before the TCP inbox was bounded and the messages counted, the server read and parsed everything into memory (the process grew by gigabytes in seconds,
# one thread busy, the referee of the other room late, the control interface slow) and never dropped the sender; now the sender is dropped after a second's worth at the most.
PROTOCOL="$("$SERVER" --version 2> /dev/null | sed -n 's/.*(network protocol \([0-9][0-9]*\)).*/\1/p')"      # the raw client below says Hello with the protocol of this very server (it said a literal 7 until protocol 8)
check "the server says which network protocol it speaks (--version: $PROTOCOL)" "$([ -n "$PROTOCOL" ]; echo $?)"
rss_kb() { ps -o rss= -p "$SERVER_PID" 2> /dev/null | tr -d ' '; }
cpu_secs() {      # the server's CPU time to the hundredth: Linux's `ps -o time=` has whole seconds only (a 3 s window read 0 or 1, and 1 failed), so /proc there; ps elsewhere (macOS shows hundredths)
    if [ -r "/proc/$SERVER_PID/stat" ]; then
        python3 -c 'import os, sys; f = open(sys.argv[1]).read().rsplit(")", 1)[1].split(); print((int(f[11]) + int(f[12])) / os.sysconf("SC_CLK_TCK"))' "/proc/$SERVER_PID/stat"
        return
    fi
    ps -o time= -p "$SERVER_PID" 2> /dev/null | python3 -c 'import sys; t = sys.stdin.read().strip().replace("-", ":"); s = 0.0
for part in t.split(":"): s = s * 60 + float(part)
print(s)'
}
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
wait_ticks "$VICTIM" > /dev/null      # (its dialog's 5 s first: the rates below are measured while it plays)
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
hello = bytes([1]) + struct.pack('<H', protocol) + str8('Evil') + struct.pack('<H', 0) + bytes([255]) + str8(room) + str8('') + bytes(16) + struct.pack('<I', 0) + bytes([0, 0])     # (protocol 10: no key, no turns; protocol 15: the platform byte, 0 = not told; protocol 16: the client kind, 0 = a game; and no create block)
message = bytes([24, 0, 0, 0, 0, 255, 255]) if kind == 'startreq' else bytes([10]) + struct.pack('<II', 1, 0)       # (protocol 13: a StartRequest is the type, a fill level for each of the four seats, 0 = none, and the two team bytes, 255 = none)
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
        # (the bucket is 1000 a second plus its burst, and it refills while the flood is read: a slow machine reads for longer before the violation, so the upper bound is 1500, not 1300 -
        # a macOS runner of CI got 1305; the point is "about a second's worth, not millions")
        check "ping flood: the server answered a second's worth of pings and no more ($PONGS pongs of millions of pings)" "$([ "${PONGS:-0}" -ge 1000 ] && [ "${PONGS:-0}" -le 1500 ]; echo $?)"
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
# a match is an online game when it has played 600 ticks (30 s) and ended: this one has been playing through the floods, and plays on until it has
VICTIM_TICKS=0
for _ in $(seq 1 400); do
    VICTIM_TICKS="$(ticks_of "$VICTIM")"
    [ "${VICTIM_TICKS:-0}" -ge 600 ] && break
    sleep 0.25
done
ONLINE_BEFORE="$(stat_of online total)"
for p in $VICTIM_PIDS; do kill "$p" 2> /dev/null; done
for p in $VICTIM_PIDS; do wait "$p" 2> /dev/null; done
code_of -X DELETE -H "Authorization: Bearer $SECRET" "$CTL/rooms/$VICTIM" > /dev/null
sleep 0.3
check "the match next to the floods played $VICTIM_TICKS ticks (600 are 30 s) and ended (its players left, then the owner closed it): one online game, counted once (a match that played that long counts, whatever ended it)" "$([ "${VICTIM_TICKS:-0}" -ge 600 ] && [ "$(stat_of online total)" = "$((ONLINE_BEFORE + 1))" ] && [ "$(stat_of online day)" = "$((ONLINE_BEFORE + 1))" ]; echo $?)"

# closing the room writes its result; SIGTERM stops the server
check "the owner closes the room: 200" "$([ "$(code_of -X DELETE -H "Authorization: Bearer $SECRET" "$CTL/rooms/$CODE")" = "200" ]; echo $?)"
check "with every room closed /busy says no match and nobody again" "$(busy_json_ok 0 0; echo $?)"
sleep 0.5
kill -TERM "$SERVER_PID" 2> /dev/null
wait "$SERVER_PID" 2> /dev/null
SERVER_EXIT=$?
SERVER_PID=""
check "the server stops cleanly on SIGTERM" "$([ "$SERVER_EXIT" -eq 0 ]; echo $?)"
check "the room's result file was written" "$([ -s "$WORK/results/$CODE.json" ]; echo $?)"
check "the log never shows the secret" "$(grep -q "$SECRET" "$WORK/server.log"; [ $? -ne 0 ]; echo $?)"
# the counters are in the results folder (numbers and one date only), the stop saved them, the next start reads them, and a file that is no statistics is put aside whole
STATS_FILE="$WORK/results/site-stats.json"
check "the stop saved the counters in the results folder: one line of JSON, the format, the date, and for each counter its total and its hours (numbers only)" "$(python3 -c '
import sys, json, re
d = json.load(open(sys.argv[1]))
ok = sorted(d) == ["format", "local", "online", "since"] and d["format"] == 1 and re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}", d["since"]) is not None
for k in ("online", "local"):
    ok = ok and sorted(d[k]) == ["hours", "total"] and isinstance(d[k]["total"], int) and all(len(h) == 2 and all(isinstance(x, int) for x in h) for h in d[k]["hours"])
ok = ok and d["local"]["total"] == 120 and d["online"]["total"] == 1 and sum(h[1] for h in d["local"]["hours"]) == 120
sys.exit(0 if ok else 1)' "$STATS_FILE"; echo $?)"
check "the log of the first start says that the counters are new, the stop leaves no temporary file" "$(grep -q 'site statistics: .* is new, counting from' "$WORK/server.log" && [ ! -e "$STATS_FILE.tmp" ]; echo $?)"
STATS_BEFORE="$(python3 -c 'import sys, json; d = json.load(open(sys.argv[1])); print(d["since"], d["online"]["total"], d["local"]["total"])' "$STATS_FILE")"
start_stats_server() {      # start_stats_server LOG: a server over the same results folder, on a WebSocket port of its own (WS2); waits for it
    WS2="$(free_port)"
    "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$(free_port)" --ws-port "$WS2" --results-dir "$WORK/results" > "$1" 2>&1 &
    SERVER_PID=$!
    for _ in $(seq 1 50); do
        [ "$(code_of "http://127.0.0.1:$WS2/busy")" = "200" ] && return 0
        kill -0 "$SERVER_PID" 2> /dev/null || return 1
        sleep 0.1
    done
    return 1
}
start_stats_server "$WORK/server2.log"
check "a second start over the same results folder is up" "$?"
STATS2="http://127.0.0.1:$WS2/stats"
STATS_AFTER="$(python3 -c 'import sys, json; d = json.load(open(sys.argv[1])); print(d["since"], d["online"]["total"], d["local"]["total"])' <(curl -s -m 3 "$STATS2") 2> /dev/null)"
check "it reads the counters back: the same totals, the same date ($STATS_BEFORE)" "$([ "$STATS_AFTER" = "$STATS_BEFORE" ]; echo $?)"
check "... the games are still in the last 24 hours, and its log says what it read" "$(stats_check "$(stat_of online total "$STATS2")" "$(stat_of online total "$STATS2")" 120 120 "$STATS2" && grep -q 'site statistics: read .* online and 120 single-player games since' "$WORK/server2.log"; echo $?)"
file_local_total() { python3 -c 'import sys, json; print(json.load(open(sys.argv[1]))["local"]["total"])' "$STATS_FILE" 2> /dev/null; }
check "a report after the restart counts (121)" "$([ "$(code_of -X POST "http://127.0.0.1:$WS2/stats/local")" = "204" ] && [ "$(stat_of local total "$STATS2")" = "121" ]; echo $?)"
FILE_SAVED=1
for _ in $(seq 1 30); do
    [ "$(file_local_total)" = "121" ] && { FILE_SAVED=0; break; }
    sleep 0.1
done
check "the file has it within three seconds while the server runs (a change is written when it is due: the first write after a start is at once)" "$FILE_SAVED"
check "a second report right after it (inside the ten seconds that the next write waits) counts (122) and is not in the file yet" "$([ "$(code_of -X POST "http://127.0.0.1:$WS2/stats/local")" = "204" ] && [ "$(stat_of local total "$STATS2")" = "122" ] && sleep 0.3 && [ "$(file_local_total)" = "121" ]; echo $?)"
stop_server
check "the second server stops cleanly and the stop saved the 122" "$([ "$(file_local_total)" = "122" ]; echo $?)"
printf 'this is no statistics' > "$STATS_FILE"
mkdir "$WORK/results/site-stats.json.tmp"      # (a folder where the counters' temporary file goes: the first write of this start cannot be made)
start_stats_server "$WORK/server3.log"
check "a start over a file that is no statistics is up" "$?"
STATS3="http://127.0.0.1:$WS2/stats"
check "... it puts the file aside whole (named .broken-<time>), says so in its log, and counts again from 0" "$([ "$(ls "$WORK/results" | grep -c '^site-stats.json.broken-')" = "1" ] && [ "$(cat "$WORK"/results/site-stats.json.broken-*)" = "this is no statistics" ] && grep -q 'site statistics: .* cannot be used .* put aside as .*broken-.*counters start at 0' "$WORK/server3.log" && stats_check 0 0 0 0 "$STATS3"; echo $?)"
# a file that cannot be written is said in the log while the server runs, once (the next try is ten seconds on); the stop writes it when the place is free again
SAID=1
for _ in $(seq 1 30); do
    grep -q 'site statistics could not be saved' "$WORK/server3.log" && { SAID=0; break; }
    sleep 0.1
done
check "a file that cannot be written is said in the log while the server runs" "$SAID"
sleep 1.5
check "... once" "$([ "$(grep -c 'site statistics could not be saved' "$WORK/server3.log")" = "1" ]; echo $?)"
rmdir "$WORK/results/site-stats.json.tmp"
stop_server
check "the stop wrote the file when the place was free again, and said so" "$(grep -q 'site statistics are saved again' "$WORK/server3.log" && [ "$(file_local_total)" = "0" ] && [ ! -e "$STATS_FILE.tmp" ]; echo $?)"
check "the good file beside the one that was put aside is a statistics file: format 1, no online game" "$(python3 -c 'import sys, json; d = json.load(open(sys.argv[1])); sys.exit(0 if d["format"] == 1 and d["online"]["total"] == 0 else 1)' "$STATS_FILE"; echo $?)"
fi

# ---- part secret: the control secret that the server makes, keeps, shows and reads from a file --------------------------------------------------------------------
if part_enabled secret; then
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
fi

# ---- part demo: public rooms that choose their map, and the stack's own defaults ---------------------------------------------------------------------------------
if part_enabled demo; then
# public rooms that choose their map (protocol 15): the create block of a Hello (the game's --room-map and --room-seats) makes the room on that map when it is in --demo-maps, on --demo-map when the
# block names another map or none; a Hello with no block makes no room, whatever the code is
PICK_PORT="$(free_port)"
PICK_CTL="$(free_port)"
ANTS_SERVER_SECRET="$SECRET" "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$PICK_PORT" --ctl-port "$PICK_CTL" --demo-rooms 5 --demo-map TINY.LVL --demo-maps TINY.LVL,SMALL.LVL > "$WORK/pick.log" 2>&1 &
SERVER_PID=$!
for _ in $(seq 1 50); do curl -s -m 1 "http://127.0.0.1:$PICK_CTL/healthz" | grep -q '"ok"' && break; sleep 0.1; done
check "the log names the maps that a create block can choose" "$(grep -q 'unless the block names one of TINY.LVL, SMALL.LVL' "$WORK/pick.log"; echo $?)"
PICK_PIDS=""
for pick in "pick-small:--room-map small" "pick-tiny:--room-map tiny" "pick-none:--room-seats 4" "pick-medium:--room-map medium" "pick-small2:--room-map small --room-seats 2"; do
    code="${pick%%:*}"
    # (the options are words without blanks or wildcards: they are split on purpose)
    # shellcheck disable=SC2086
    "$GAME" --headless --no-lan --name Chooser --join "127.0.0.1:$PICK_PORT" --room "$code" ${pick#*:} --frames 400 > "$WORK/pick_$code.log" 2>&1 &
    PICK_PIDS="$PICK_PIDS $!"
done
"$GAME" --headless --no-lan --name Chooser --join "127.0.0.1:$PICK_PORT" --room pick-nobody --frames 400 > "$WORK/pick_pick-nobody.log" 2>&1 &
PICK_NOBODY_PID=$!
map_of_room() { for _ in $(seq 1 60); do M="$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "http://127.0.0.1:$PICK_CTL/rooms/$1" | python3 -c 'import sys, json; print(json.load(sys.stdin).get("map", ""))' 2> /dev/null)"; [ -n "$M" ] && break; sleep 0.2; done; echo "$M"; }
check "a block with --room-map small makes the room on SMALL.LVL" "$([ "$(map_of_room pick-small)" = "SMALL.LVL" ]; echo $?)"
check "a block with --room-map tiny makes the room on TINY.LVL" "$([ "$(map_of_room pick-tiny)" = "TINY.LVL" ]; echo $?)"
check "a block that names no map (only the seats) makes the room on the default map" "$([ "$(map_of_room pick-none)" = "TINY.LVL" ]; echo $?)"
check "a block that names a map that is not in the list (medium) makes the room on the default map" "$([ "$(map_of_room pick-medium)" = "TINY.LVL" ]; echo $?)"
expected_of_room() { curl -s -m 2 -H "Authorization: Bearer $SECRET" "http://127.0.0.1:$PICK_CTL/rooms/$1" | python3 -c 'import sys, json; print(json.load(sys.stdin).get("expected", ""))' 2> /dev/null; }
check "a block with --room-map small --room-seats 2 makes the room on SMALL.LVL for two players, another block for four" "$([ "$(map_of_room pick-small2)" = "SMALL.LVL" ] && [ "$(expected_of_room pick-small2)" = "2" ] && [ "$(expected_of_room pick-small)" = "4" ]; echo $?)"
for _ in $(seq 1 100); do kill -0 "$PICK_NOBODY_PID" 2> /dev/null || break; sleep 0.2; done
check "a Hello with no create block makes no room (the code is only a name: the control interface has no such room, 404)" "$([ "$(code_of -H "Authorization: Bearer $SECRET" "http://127.0.0.1:$PICK_CTL/rooms/pick-nobody")" = "404" ]; echo $?)"
check "... and the server refused that game, and no other (the control interface counts one refused connection)" "$([ "$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "http://127.0.0.1:$PICK_CTL/stats" | python3 -c 'import sys, json; print(json.load(sys.stdin).get("refused", ""))' 2> /dev/null)" = "1" ]; echo $?)"
for p in $PICK_PIDS $PICK_NOBODY_PID; do kill "$p" 2> /dev/null; done
stop_server

# lobby rooms (protocol 16) with a real server: a page's Hello with a lobby block makes a lobby room, a game's Hello with a plain block still makes a public room, a page never makes the one and a game never the other, the pool holds
# --demo-lobbies waiting lobbies, and a lobby is no match for GET /busy. No page speaks the protocol yet: the raw client below says what one will
PROTOCOL16="$("$SERVER" --version 2> /dev/null | sed -n 's/.*(network protocol \([0-9][0-9]*\)).*/\1/p')"
LOBBY_PY="$WORK/lobby_raw.py"
cat > "$LOBBY_PY" <<'PY'
import json, socket, struct, sys, time, urllib.request
port, ws, ctl, secret, protocol = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), sys.argv[4], int(sys.argv[5])
def frame(p): return struct.pack('<I', len(p)) + p
def str8(t):
    b = t.encode()
    return bytes([len(b)]) + b
LOBBY, PLAIN = ('', 4, 255, 255, 3), ('', 4, 255, 255, 1)       # (map, seats, team_a, team_b, flags: bit 0 the leader starts, bit 1 a lobby room)
def hello(name, room, kind, block, key):
    m = bytes([1]) + struct.pack('<H', protocol) + str8(name) + struct.pack('<H', 0) + bytes([255]) + str8(room) + str8('') + key + struct.pack('<I', 0) + bytes([0, kind])
    return m + (str8(block[0]) + bytes(block[1:]) if block else b'')
class Peer:
    def __init__(self, name, room, kind, block, key=bytes(16)):
        self.sock = socket.create_connection(('127.0.0.1', port), timeout=3)
        self.sock.settimeout(0.2)
        self.buf, self.msgs = b'', []
        self.sock.sendall(frame(hello(name, room, kind, block, key)))
    def wait_for(self, types, secs):                                               # the first message of one of the types (a number each), or None when the time is up or the link closed
        end = time.time() + secs
        while True:
            for m in self.msgs:
                if m[0] in types:
                    return m
            if time.time() >= end:
                return None
            try:
                data = self.sock.recv(65536)
                if not data:
                    return None
                self.buf += data
            except socket.timeout:
                pass
            except OSError:
                return None
            while len(self.buf) >= 4 and len(self.buf) - 4 >= struct.unpack_from('<I', self.buf)[0]:
                n = struct.unpack_from('<I', self.buf)[0]
                self.msgs.append(self.buf[4:4 + n])
                self.buf = self.buf[4 + n:]
    def answer(self):                                                              # the server's answer to the Hello: its Welcome (2) or its Reject (3)
        return self.wait_for((2, 3), 3.0)
def welcomed(peer):
    m = peer.answer()
    return m if m is not None and m[0] == 2 else None
def answer_text(peer):
    m = peer.answer()
    return 'none' if m is None else 'welcome' if m[0] == 2 else 'reject-%d' % m[1]
def created(m): return m is not None and (m[19] & 2) != 0                          # (the Welcome: type, player, players, the key, the flags)
def lobby_room(peer):                                                              # the Room message ends with the flags, four plan bytes and the games
    m = peer.wait_for((12,), 3.0)
    return m is not None and (m[-6] & 2) != 0
out = {}
a = Peer('Ada', 'lob00001', 1, LOBBY); wa = welcomed(a)
out['page_makes_lobby'] = 'yes' if wa is not None and created(wa) and wa[1] == 0 and wa[3:19] != bytes(16) else 'no'
out['room_says_lobby'] = 'yes' if lobby_room(a) else 'no'
b = Peer('Bea', 'lob00001', 1, None); wb = welcomed(b)
out['second_page_joins'] = 'yes' if wb is not None and not created(wb) and wb[1] == 1 else 'no'
c = Peer('Cy', 'lob00002', 1, LOBBY); wc = welcomed(c)
out['second_lobby'] = 'yes' if created(wc) else 'no'
d = Peer('Dee', 'lob00003', 1, LOBBY)
out['pool_full'] = answer_text(d)                                                  # every lobby has a person in it: NoSuchRoom (6)
g = Peer('Gus', 'pub00001', 0, PLAIN); wg = welcomed(g)
out['game_makes_public_room'] = 'yes' if created(wg) else 'no'
e = Peer('Eve', 'pub00002', 1, PLAIN)
out['page_plain_block'] = answer_text(e)                                            # BadRequest (5)
f = Peer('Fay', 'lob00004', 0, LOBBY)
out['game_lobby_block'] = answer_text(f)                                            # BadRequest (5)
h = Peer('Hal', 'lob00005', 1, LOBBY, bytes(range(1, 17)))
out['page_key_no_room'] = answer_text(h)                                           # a Hello with a key never makes a room: NoSuchRoom (6)
i = Peer('Ian', 'Lob00006', 1, LOBBY)
out['capital_letter'] = answer_text(i)                                             # a code with a capital is never a visitor's: NoSuchRoom (6)
try:
    request = urllib.request.Request('http://127.0.0.1:%d/rooms/lob00001' % ctl, headers={'Authorization': 'Bearer ' + secret})
    room = json.load(urllib.request.urlopen(request, timeout=3))
    out['status'] = '%s/%s/%s' % (room.get('state'), room.get('map'), room.get('expected'))
    out['joined'] = str(room.get('joined'))
except Exception as error:
    out['status'] = 'error:%s' % error
try:
    busy = json.load(urllib.request.urlopen('http://127.0.0.1:%d/busy' % ws, timeout=3))
    out['busy'] = '%d/%d' % (busy['matches'], busy['players'])
except Exception as error:
    out['busy'] = 'error:%s' % error
for key in sorted(out):
    print('%s=%s' % (key, out[key]))
PY
lobby_value() { echo "$LOBBY_OUT" | sed -n "s/^$1=//p"; }
LOBBY_PORT="$(free_port)"
LOBBY_WS="$(free_port)"
LOBBY_CTL="$(free_port)"
ANTS_SERVER_SECRET="$SECRET" "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$LOBBY_PORT" --ws-port "$LOBBY_WS" --ctl-port "$LOBBY_CTL" --demo-rooms 5 --demo-map TINY.LVL --demo-lobbies 2 > "$WORK/lobby.log" 2>&1 &
SERVER_PID=$!
for _ in $(seq 1 50); do curl -s -m 1 "http://127.0.0.1:$LOBBY_CTL/healthz" | grep -q '"ok"' && break; sleep 0.1; done
LOBBY_OUT="$(python3 "$LOBBY_PY" "$LOBBY_PORT" "$LOBBY_WS" "$LOBBY_CTL" "$SECRET" "$PROTOCOL16" 2>&1)"
check "a lobby page's Hello with a lobby block makes a lobby room (the Welcome says it made it and gives a key)" "$([ "$(lobby_value page_makes_lobby)" = "yes" ]; echo $?)"
check "the Room message of that room says it is a lobby room" "$([ "$(lobby_value room_says_lobby)" = "yes" ]; echo $?)"
check "a second page finds the room (seat 1, not created)" "$([ "$(lobby_value second_page_joins)" = "yes" ]; echo $?)"
check "a second lobby is made while the pool (2) has a place" "$([ "$(lobby_value second_lobby)" = "yes" ]; echo $?)"
check "a third is told NoSuchRoom when every lobby of the pool has a person in it" "$([ "$(lobby_value pool_full)" = "reject-6" ]; echo $?)"
check "a game's Hello with a plain block still makes a public room" "$([ "$(lobby_value game_makes_public_room)" = "yes" ]; echo $?)"
check "a page with a plain block makes no room (BadRequest)" "$([ "$(lobby_value page_plain_block)" = "reject-5" ]; echo $?)"
check "a game with a lobby block makes no room (BadRequest)" "$([ "$(lobby_value game_lobby_block)" = "reject-5" ]; echo $?)"
check "a Hello with a key makes no lobby room (NoSuchRoom)" "$([ "$(lobby_value page_key_no_room)" = "reject-6" ]; echo $?)"
check "a code with an upper-case letter makes no lobby room (NoSuchRoom)" "$([ "$(lobby_value capital_letter)" = "reject-6" ]; echo $?)"
check "the control interface sees the lobby: waiting, on the server's map, for four, with its two pages" "$([ "$(lobby_value status)" = "waiting/TINY.LVL/4" ] && [ "$(lobby_value joined)" = "2" ]; echo $?)"
check "GET /busy counts the people of the lobbies as players and no match (a lobby never holds a deploy back)" "$([ "$(lobby_value busy)" = "0/4" ]; echo $?)"
stop_server
ANTS_SERVER_SECRET="$SECRET" "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$LOBBY_PORT" --ws-port "$LOBBY_WS" --ctl-port "$LOBBY_CTL" --demo-rooms 5 --demo-map TINY.LVL --demo-lobbies 0 > "$WORK/lobby0.log" 2>&1 &
SERVER_PID=$!
for _ in $(seq 1 50); do curl -s -m 1 "http://127.0.0.1:$LOBBY_CTL/healthz" | grep -q '"ok"' && break; sleep 0.1; done
LOBBY_OUT="$(python3 "$LOBBY_PY" "$LOBBY_PORT" "$LOBBY_WS" "$LOBBY_CTL" "$SECRET" "$PROTOCOL16" 2>&1)"
check "with --demo-lobbies 0 a lobby page is told NoSuchRoom, and a game still makes its public room" "$([ "$(lobby_value page_makes_lobby)" = "no" ] && [ "$(lobby_value game_makes_public_room)" = "yes" ]; echo $?)"
stop_server
ANTS_SERVER_SECRET="$SECRET" "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$LOBBY_PORT" --ws-port "$LOBBY_WS" --ctl-port "$LOBBY_CTL" --demo-rooms 5 --demo-map TINY.LVL > "$WORK/lobbyd.log" 2>&1 &
SERVER_PID=$!
for _ in $(seq 1 50); do curl -s -m 1 "http://127.0.0.1:$LOBBY_CTL/healthz" | grep -q '"ok"' && break; sleep 0.1; done
LOBBY_OUT="$(python3 "$LOBBY_PY" "$LOBBY_PORT" "$LOBBY_WS" "$LOBBY_CTL" "$SECRET" "$PROTOCOL16" 2>&1)"
check "without the option a server with public rooms makes lobby rooms (the default pool has a place for the third lobby that the pool of two refused)" "$([ "$(lobby_value page_makes_lobby)" = "yes" ] && [ "$(lobby_value pool_full)" = "welcome" ]; echo $?)"
stop_server

# the stack's own defaults: docker-compose.stack.yml, read as the stack starts it with nothing set in its environment (tests/scripts/stack_command.py), gives the demo options of the
# public site. A block that names no map is made on its --demo-map, TREASURE.LVL (the map that is played most); a block that names another map of the six is made on that one
STACK_OPTS="$(python3 "$ROOT/tests/scripts/stack_command.py" "$ROOT/docker-compose.stack.yml" --demo 2> /dev/null)"
check "docker-compose.stack.yml: the demo options can be read (--demo-rooms, --demo-map and --demo-maps with their defaults)" "$([ -n "$STACK_OPTS" ]; echo $?)"
check "docker-compose.stack.yml: the default --demo-map is TREASURE.LVL" "$(echo "$STACK_OPTS" | grep -q -- '--demo-map TREASURE.LVL '; echo $?)"
STACK_PORT="$(free_port)"
STACK_CTL="$(free_port)"
# (the options are words without blanks or wildcards: they are split on purpose)
# shellcheck disable=SC2086
ANTS_SERVER_SECRET="$SECRET" "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$STACK_PORT" --ctl-port "$STACK_CTL" $STACK_OPTS > "$WORK/stack_demo.log" 2>&1 &
SERVER_PID=$!
STACK_UP=1
for _ in $(seq 1 50); do
    if curl -s -m 1 "http://127.0.0.1:$STACK_CTL/healthz" | grep -q '"ok"'; then STACK_UP=0; break; fi
    kill -0 "$SERVER_PID" 2> /dev/null || break
    sleep 0.1
done
check "a server started with the stack's own demo options runs (the six maps are in the folder)" "$STACK_UP"
STACK_PIDS=""
for pick in "stack-n1:--room-seats 4" "stack-tiny-n2:--room-map tiny" "stack-treasure-n3:--room-map treasure" "stack-islands-n4:--room-map islands --room-seats 2"; do
    code="${pick%%:*}"
    # shellcheck disable=SC2086
    "$GAME" --headless --no-lan --name Chooser --join "127.0.0.1:$STACK_PORT" --room "$code" ${pick#*:} --frames 400 > "$WORK/stack_$code.log" 2>&1 &
    STACK_PIDS="$STACK_PIDS $!"
done
stack_map_of_room() { for _ in $(seq 1 60); do M="$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "http://127.0.0.1:$STACK_CTL/rooms/$1" | python3 -c 'import sys, json; print(json.load(sys.stdin).get("map", ""))' 2> /dev/null)"; [ -n "$M" ] && break; sleep 0.2; done; echo "$M"; }
check "the stack: a block that names no map is made on the default map, TREASURE.LVL" "$([ "$(stack_map_of_room stack-n1)" = "TREASURE.LVL" ]; echo $?)"
check "the stack: a block with --room-map tiny is made on TINY.LVL (a block that names a map still chooses it)" "$([ "$(stack_map_of_room stack-tiny-n2)" = "TINY.LVL" ]; echo $?)"
check "the stack: a block with --room-map treasure is made on TREASURE.LVL" "$([ "$(stack_map_of_room stack-treasure-n3)" = "TREASURE.LVL" ]; echo $?)"
check "the stack: a block with --room-map islands is made on ISLANDS.LVL" "$([ "$(stack_map_of_room stack-islands-n4)" = "ISLANDS.LVL" ]; echo $?)"
# release B turned the switch on: the stack passes no reconnect option and its rooms hold the seat of a player whose connection is lost (the log says so; a public room, and a room that the
# control interface makes without a setting, say reconnect true)
stack_reconnect_of_room() { curl -s -m 2 -H "Authorization: Bearer $SECRET" "http://127.0.0.1:$STACK_CTL/rooms/$1" | python3 -c 'import sys, json; print(str(json.load(sys.stdin).get("reconnect", "")).lower())' 2> /dev/null; }
check "the stack passes no reconnect option (the default is what holds the seats)" "$(echo "$STACK_OPTS" | grep -q -e '--reconnect' -e '--no-reconnect'; [ "$?" -ne 0 ]; echo $?)"
check "the stack: the log says that rooms hold seats, as the default" "$(grep -q 'rooms hold the seat of a player whose connection is lost (the default; --no-reconnect turns it off)' "$WORK/stack_demo.log"; echo $?)"
check "the stack: its public rooms hold seats (reconnect true in their status)" "$([ "$(stack_reconnect_of_room stack-n1)" = "true" ] && [ "$(stack_reconnect_of_room stack-islands-n4)" = "true" ]; echo $?)"
check "the stack: a room that the control interface makes without a setting holds seats, and one that says false does not" "$(curl -s -m 3 -X POST -H "Authorization: Bearer $SECRET" -d '{"map":"TINY.LVL","code":"STACK-CTL-1"}' "http://127.0.0.1:$STACK_CTL/rooms" | grep -q '"reconnect":true' && curl -s -m 3 -X POST -H "Authorization: Bearer $SECRET" -d '{"map":"TINY.LVL","code":"STACK-CTL-2","reconnect":false}' "http://127.0.0.1:$STACK_CTL/rooms" | grep -q '"reconnect":false'; echo $?)"
for p in $STACK_PIDS; do kill "$p" 2> /dev/null; done
stop_server

# the off state stays covered: --no-reconnect, after the stack's own options, makes every room hold no seats (a public room too, and the records say that none is kept), and a room's own "reconnect": true still holds them
NOREC_PORT="$(free_port)"
NOREC_CTL="$(free_port)"
# shellcheck disable=SC2086
ANTS_SERVER_SECRET="$SECRET" "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$NOREC_PORT" --ctl-port "$NOREC_CTL" --results-dir "$WORK/norec_results" $STACK_OPTS --no-reconnect > "$WORK/norec.log" 2>&1 &
SERVER_PID=$!
NOREC_UP=1
for _ in $(seq 1 50); do
    if curl -s -m 1 "http://127.0.0.1:$NOREC_CTL/healthz" | grep -q '"ok"'; then NOREC_UP=0; break; fi
    kill -0 "$SERVER_PID" 2> /dev/null || break
    sleep 0.1
done
check "a server started with the stack's options and --no-reconnect runs" "$NOREC_UP"
"$GAME" --headless --no-lan --name Chooser --join "127.0.0.1:$NOREC_PORT" --room norec-nr1 --room-seats 4 --frames 400 > "$WORK/norec_demo.log" 2>&1 &
NOREC_PID=$!
norec_reconnect_of_room() { for _ in $(seq 1 60); do R="$(curl -s -m 2 -H "Authorization: Bearer $SECRET" "http://127.0.0.1:$NOREC_CTL/rooms/$1" | python3 -c 'import sys, json; print(str(json.load(sys.stdin).get("reconnect", "")).lower())' 2> /dev/null)"; [ -n "$R" ] && break; sleep 0.2; done; echo "$R"; }
check "--no-reconnect: the log says that rooms hold no seats, and that no record is kept" "$(grep -q 'rooms hold no seats (--no-reconnect)' "$WORK/norec.log" && grep -q 'no room holds seats unless its specification says so (--no-reconnect), so none is kept now' "$WORK/norec.log"; echo $?)"
check "--no-reconnect: a public room holds no seats (reconnect false in its status)" "$([ "$(norec_reconnect_of_room norec-nr1)" = "false" ]; echo $?)"
check "--no-reconnect: a room that the control interface makes without a setting holds none, and one that says true holds seats" "$(curl -s -m 3 -X POST -H "Authorization: Bearer $SECRET" -d '{"map":"TINY.LVL","code":"NOREC-CTL-1"}' "http://127.0.0.1:$NOREC_CTL/rooms" | grep -q '"reconnect":false' && curl -s -m 3 -X POST -H "Authorization: Bearer $SECRET" -d '{"map":"TINY.LVL","code":"NOREC-CTL-2","reconnect":true}' "http://127.0.0.1:$NOREC_CTL/rooms" | grep -q '"reconnect":true'; echo $?)"
kill "$NOREC_PID" 2> /dev/null
stop_server
fi

# ---- part reconnect: the server that holds the seat of a player whose connection is lost (protocol 10, --reconnect) -----------------------------------------------
if part_enabled reconnect; then
# What is tested here is the SERVER, with real programs: a client behind a proxy that is cut (and that refuses it for an hour: its way back finds no door), a client that is stopped. The room
# pauses for everybody and names the seat, nothing runs while it waits, the cap (60 s here) drops the seat and the match goes on for the others. The game's own way back is the clients' part: the
# victim keeps trying (it does not leave the match), the stopped client comes back by itself when it wakes up.
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
RC_T0="$(wait_ticks "$RC_CODE" "$RC_URL")"      # (the first turn is sealed 5 s after the match began: the pause below is tested in a match that plays)
check "the status says that nobody waits: not paused, nobody absent, no vote, the log is being kept" "$([ "$(rc_field "$RC_CODE" paused)" = "false" ] && [ "$(rc_field "$RC_CODE" absent)" = "[]" ] && [ "$(rc_field "$RC_CODE" vote)" = "null" ] && [ "$(rc_field "$RC_CODE" log.usable)" = "true" ] && [ "$(rc_field "$RC_CODE" log.turns)" -gt 0 ]; echo $?)"
sleep 2
RC_T1="$(rc_field "$RC_CODE" ticks)"
check "the referee's clock runs before the cut (ticks $RC_T0, then $RC_T1 two seconds later)" "$([ "${RC_T0:-0}" -gt 0 ] && [ "${RC_T1:-0}" -gt "${RC_T0:-0}" ]; echo $?)"

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
wait_ticks "$RC_STOP_CODE" "$RC_URL" > /dev/null      # (its dialog's 5 s first: the client below is stopped in a match that plays)
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
check "the victim's game does not leave the match: it keeps trying to come back (no lost-connection message), though the proxy has refused it since the cut" "$(grep -q 'connection to the other players was lost' "$WORK/hv.log"; [ $? -ne 0 ]; echo $?)"

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
check "the second room holds the seat of the stopped client until it wakes up: paused, running, nobody dropped" "$([ "$(rc_field "$RC_STOP_CODE" paused)" = "true" ] && [ "$(rc_field "$RC_STOP_CODE" state)" = "running" ] && [ "$(rc_field "$RC_STOP_CODE" rejoins)" = "0" ]; echo $?)"
kill -CONT "$RC_FROZEN_PID"
RC_FROZEN_BACK=1
for _ in $(seq 1 150); do
    [ "$(rc_field "$RC_STOP_CODE" rejoins)" = "1" ] && { RC_FROZEN_BACK=0; break; }
    sleep 0.2
done
check "the client that wakes up finds its old link closed and comes back by itself with its key: the second room counts the rejoin" "$RC_FROZEN_BACK"
check "... its game did not leave the match: no lost-connection message" "$(grep -q 'connection to the other players was lost' "$WORK/sf.log"; [ $? -ne 0 ]; echo $?)"
RC_STOP_RESUMED=1
for _ in $(seq 1 150); do
    [ "$(rc_field "$RC_STOP_CODE" paused)" = "false" ] && { RC_STOP_RESUMED=0; break; }
    sleep 0.2
done
check "the second room goes on after its resume countdown (8 s here): not paused, nobody absent, still running, nobody dropped" "$RC_STOP_RESUMED"
check "... and its three players are in it again: nobody absent, no drop by the cap or a vote" "$([ "$(rc_field "$RC_STOP_CODE" absent)" = "[]" ] && [ "$(rc_field "$RC_STOP_CODE" state)" = "running" ] && [ "$(rc_field "$RC_STOP_CODE" drops_by_cap)" = "0" ] && [ "$(rc_field "$RC_STOP_CODE" drops_by_vote)" = "0" ]; echo $?)"
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

# ---- restart records (docs/NETWORK_PORT.md "Restart records"): a match survives a restart of the server ---------------------------------------------------------------
# The real program with real game clients. The server keeps a record of the running room (mode 600 in a folder of mode 700), is stopped with SIGTERM in the middle of the match
# (it exits at once and leaves the record), is started again over the same folder (the room is back, paused, every seat held), is killed with SIGKILL and started once more (the record that the
# restored room went on writing restores again), and when the owner closes the room its record goes. The two games are stopped (kill -STOP) the moment the server is gone, so that they do not
# come back before the restored room has been looked at; when they are let go (kill -CONT) they find the room by themselves, with their keys, and the match goes on (the game's own way back:
# the C++ tests S3.95, S3.96 do the same with machines of the test, RJ1.2 with the NetGame).
RR_PORT="$(free_port)"
RR_CTL="$(free_port)"
RR_CODE="E2E-KEEP-$RANDOM"
RR_URL="http://127.0.0.1:$RR_CTL"
RR_RESULTS="$WORK/rr_results"
RR_DIR="$RR_RESULTS/restart"
rr_field() { curl -s -m 3 -H "Authorization: Bearer $SECRET" "$RR_URL/rooms/$1" | python3 -c 'import sys, json; v = json.load(sys.stdin)
for part in sys.argv[1].split("."):
    v = v.get(part) if isinstance(v, dict) else None
print(json.dumps(v) if isinstance(v, (dict, list)) or v is None else str(v).lower() if isinstance(v, bool) else v)' "$2" 2> /dev/null; }
rr_launch() {      # rr_launch [EXTRA ARGUMENTS]: starts the server of this section over the same folder and does not wait for it
    ANTS_SERVER_SECRET="$SECRET" "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$RR_PORT" --ctl-port "$RR_CTL" --results-dir "$RR_RESULTS" --reconnect --resume-countdown-seconds 0 "$@" >> "$WORK/rr_server.log" 2>&1 &
    SERVER_PID=$!
}
rr_start() {      # rr_start [EXTRA ARGUMENTS]: rr_launch, and it returns when the control interface answers (the restore of the records goes on after that: rr_wait_room)
    rr_launch "$@"
    local up=1
    for _ in $(seq 1 100); do
        if curl -s -m 1 "$RR_URL/healthz" | grep -q '"ok"'; then up=0; break; fi
        kill -0 "$SERVER_PID" 2> /dev/null || break
        sleep 0.1
    done
    return $up
}
# The records are replayed in slices while the server serves (the restore does not block it): a room is back when its replay has ended, a moment after the server answers.
rr_wait_room() {      # rr_wait_room CODE: waits (20 s at the most) until the room is running again; 0 when it is
    for _ in $(seq 1 200); do
        [ "$(rr_field "$1" state)" = "running" ] && return 0
        sleep 0.1
    done
    return 1
}
rr_start
check "the server of the restart section is up" "$?"
check "its log says where the records are kept and that a running match survives a restart" "$(grep -q "restart records in $RR_DIR" "$WORK/rr_server.log" && grep -q 'survives a restart' "$WORK/rr_server.log"; echo $?)"
curl -s -m 3 -o /dev/null -X POST -H "Authorization: Bearer $SECRET" -d "{\"map\":\"TINY.LVL\",\"players\":2,\"code\":\"$RR_CODE\",\"seed\":9}" "$RR_URL/rooms"
RR_PIDS=""
"$GAME" --headless --no-lan --name Keeper1 --join "127.0.0.1:$RR_PORT" --room "$RR_CODE" --screenshot "$WORK/k1.png" --frames 4000000 > "$WORK/k1.log" 2>&1 &
RR_PIDS="$!"
"$GAME" --headless --no-lan --name Keeper2 --join "127.0.0.1:$RR_PORT" --room "$RR_CODE" --screenshot "$WORK/k2.png" --frames 4000000 > "$WORK/k2.log" 2>&1 &
RR_PIDS="$RR_PIDS $!"
RR_RUNNING=1
for _ in $(seq 1 150); do
    [ "$(rr_field "$RR_CODE" state)" = "running" ] && { RR_RUNNING=0; break; }
    sleep 0.2
done
check "the two clients joined the room of the restart section and the match runs" "$RR_RUNNING"
RR_T0="$(wait_ticks "$RR_CODE" "$RR_URL")"
sleep 3
RR_T1="$(rr_field "$RR_CODE" ticks)"
check "the match plays before the stop (ticks $RR_T0, then $RR_T1)" "$([ "${RR_T0:-0}" -gt 0 ] && [ "${RR_T1:-0}" -gt "${RR_T0:-0}" ]; echo $?)"
RR_FILES="$(ls "$RR_DIR" 2> /dev/null | grep -c '\.restart$')"
check "exactly one restart record is on disk" "$([ "$RR_FILES" = "1" ]; echo $?)"
RR_FILE="$(ls "$RR_DIR"/*.restart 2> /dev/null | head -1)"
check "the record is for its owner only (mode 600), in a folder of mode 700" "$([ "$(stat -c %a "$RR_FILE" 2> /dev/null || stat -f %Lp "$RR_FILE")" = "600" ] && [ "$(stat -c %a "$RR_DIR" 2> /dev/null || stat -f %Lp "$RR_DIR")" = "700" ]; echo $?)"
check "the status says that the record is kept, and that the room was not restored" "$([ "$(rr_field "$RR_CODE" record.kept)" = "true" ] && [ "$(rr_field "$RR_CODE" restored)" = "null" ] && [ "$(rr_field "$RR_CODE" record.bytes)" -gt 300 ]; echo $?)"
RR_SEALED="$(rr_field "$RR_CODE" turns)"
# SIGTERM in the middle of the match: the server exits at once (well inside the 15 s that the stack gives docker) and leaves the record
RR_KILL_AT="$(python3 -c 'import time; print(time.time())')"
kill -TERM "$SERVER_PID"
wait "$SERVER_PID" 2> /dev/null
RR_STOP_RC=$?
RR_STOPPED_AFTER="$(python3 -c "import time; print(round(time.time() - $RR_KILL_AT, 2))")"
SERVER_PID=""
check "SIGTERM stops the server with status 0, $RR_STOPPED_AFTER s after the signal (well inside docker's grace)" "$([ "$RR_STOP_RC" = "0" ] && python3 -c "import sys; sys.exit(0 if $RR_STOPPED_AFTER < 3 else 1)"; echo $?)"
check "its log says that the record was made durable and kept" "$(grep -q 'stopped: 1 restart record(s) made durable and kept' "$WORK/rr_server.log"; echo $?)"
check "the record is still there" "$([ "$(ls "$RR_DIR" | grep -c '\.restart$')" = "1" ]; echo $?)"
for p in $RR_PIDS; do kill -STOP "$p" 2> /dev/null; done      # (the games look for the server every two seconds: stopped, they do not come back before the room is looked at)
sleep 1
check "the two games did not leave the match when the server went (no lost-connection message): they are stopped here, waiting for it" "$(grep -q 'connection to the other players was lost' "$WORK/k1.log" "$WORK/k2.log"; [ $? -ne 0 ]; echo $?)"
# started again over the same folder: the room is back, paused, both seats held
rr_start
check "the server started again over the same results folder is up" "$?"
rr_wait_room "$RR_CODE"
check "its log says that the room was restored, with its turns and its replay time and the seats that wait" "$(grep -qE "room $RR_CODE restored: [0-9]+ turns .*2 seat\(s\) waiting" "$WORK/rr_server.log"; echo $?)"
check "the room has its code back: running, restored, paused, both seats absent" "$([ "$(rr_field "$RR_CODE" state)" = "running" ] && [ "$(rr_field "$RR_CODE" paused)" = "true" ] && [ "$(rr_field "$RR_CODE" restored.turns)" -ge "$RR_SEALED" ] && [ "$(rr_field "$RR_CODE" absent | python3 -c 'import sys, json; print(len(json.load(sys.stdin)))')" = "2" ]; echo $?)"
check "... it has no turn that it did not have (the match is held until its players come back)" "$([ "$(rr_field "$RR_CODE" turns)" = "$(rr_field "$RR_CODE" restored.turns)" ]; echo $?)"
check "the status names the players, the map and keeps the record" "$([ "$(rr_field "$RR_CODE" map)" = "TINY.LVL" ] && [ "$(rr_field "$RR_CODE" joined)" = "2" ] && [ "$(rr_field "$RR_CODE" record.kept)" = "true" ]; echo $?)"
RR_RESTORED_TURNS="$(rr_field "$RR_CODE" restored.turns)"
sleep 2
check "nothing advances while the room waits (turns $RR_RESTORED_TURNS)" "$([ "$(rr_field "$RR_CODE" turns)" = "$RR_RESTORED_TURNS" ]; echo $?)"
# SIGKILL and once more: the record that the restored room went on writing restores again
kill -KILL "$SERVER_PID"
wait "$SERVER_PID" 2> /dev/null
SERVER_PID=""
rr_start
check "the server killed with SIGKILL and started again is up" "$?"
rr_wait_room "$RR_CODE"
check "the room is restored a second time from the same record, with the same turns" "$([ "$(rr_field "$RR_CODE" restored.turns)" = "$RR_RESTORED_TURNS" ] && [ "$(rr_field "$RR_CODE" paused)" = "true" ]; echo $?)"
cp "$RR_DIR"/*.restart "$WORK/rr_source.restart"      # (the record of the room that was restored twice: the section at the end makes many of it)
# the two games wake up: each finds its link closed, makes a new one and says Hello with its key; the restored room gives them the match and goes on
for p in $RR_PIDS; do kill -CONT "$p" 2> /dev/null; done
RR_BACK=1
for _ in $(seq 1 200); do
    [ "$(rr_field "$RR_CODE" rejoins)" = "2" ] && [ "$(rr_field "$RR_CODE" paused)" = "false" ] && { RR_BACK=0; break; }
    sleep 0.2
done
check "the two games find the restored room by themselves with their keys: two rejoins, the room is not paused any more" "$RR_BACK"
RR_TE="$(rr_field "$RR_CODE" turns)"
sleep 2
check "the match goes on after the restart: the room seals turns again ($RR_TE, then $(rr_field "$RR_CODE" turns) two seconds later)" "$([ "$(rr_field "$RR_CODE" turns)" -gt "$RR_TE" ]; echo $?)"
check "neither game ended with the lost-connection message, nor reported an error" "$(grep -qiE 'connection to the other players was lost|out of sync|failed|error' "$WORK/k1.log" "$WORK/k2.log"; [ $? -ne 0 ]; echo $?)"
# the owner closes the room: its record goes
check "closing the room (DELETE) answers 200" "$([ "$(code_of -X DELETE -H "Authorization: Bearer $SECRET" "$RR_URL/rooms/$RR_CODE")" = "200" ]; echo $?)"
check "... and its record is gone" "$([ "$(ls "$RR_DIR" | grep -c '\.restart$')" = "0" ]; echo $?)"
check "the status of the closed room says that it is over and keeps no record" "$([ "$(rr_field "$RR_CODE" state)" = "failed" ] && [ "$(rr_field "$RR_CODE" record.kept)" = "false" ]; echo $?)"
for p in $RR_PIDS; do kill "$p" 2> /dev/null; done
kill -TERM "$SERVER_PID" 2> /dev/null
wait "$SERVER_PID" 2> /dev/null
SERVER_PID=""
check "no key is in the server's log (no run of 32 hex digits in it: a key is 16 bytes), and neither is the secret" "$(! grep -qE '[0-9a-f]{32}' "$WORK/rr_server.log" && ! grep -q "$SECRET" "$WORK/rr_server.log"; echo $?)"
check "the result file of the closed room has no key either" "$([ -s "$RR_RESULTS/$RR_CODE.json" ] && ! grep -qE '[0-9a-f]{32}' "$RR_RESULTS/$RR_CODE.json"; echo $?)"

# ---- the restore never blocks the server (docs/NETWORK_PORT.md "Restart records", "The server serves while it restores") ---------------------------------------------------------
# A thousand records of one long match (the match of this section, made as long as TINY allows with turns that nobody played) are on disk when the server starts again: replaying all of them takes
# seconds (about ten here), and a server that did it before its first answer would be silent that long. This one answers GET /busy within 2 s of its launch, counting the matches that wait, and takes
# the Hello of a new room's player at once, while it restores (the clock of the measure starts at the launch, not when the control interface answers).
RR_CLONES=1000
RR_TURNS=$((5000 / TIME_SCALE))
python3 - "$RR_DIR" "$RR_CLONES" "$RR_TURNS" "$WORK/rr_source.restart" <<'PY'
import os, struct, sys, zlib
folder, count, total, source = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
data = open(source, 'rb').read()
magic, pos, frames = data[:8], 8, []
while pos + 9 <= len(data):                                     # (the whole frames: a torn tail goes)
    kind, length = data[pos], struct.unpack_from('<I', data, pos + 1)[0]
    end = pos + 5 + length + 4
    if end > len(data):
        break
    frames.append((kind, data[pos + 5:pos + 5 + length]))
    pos = end
def make_frame(kind, payload):
    body = bytes([kind]) + struct.pack('<I', len(payload)) + payload
    return body + struct.pack('<I', zlib.crc32(body) & 0xFFFFFFFF)
head = frames[0][1]                                             # u16 format, str8 game version, u16 protocol, str8 build id, str8 code, ...
off = 2
off += 1 + head[off]
off += 2
off += 1 + head[off]
code_at, code_len = off, head[off]
turns, rest = 0, b''
for kind, payload in frames[1:]:
    rest += make_frame(kind, payload)
    if kind == 2:
        turns = struct.unpack_from('<I', payload, 0)[0] + struct.unpack_from('<H', payload, 4)[0]
while turns < total:                                            # empty turns after the last one (no checkpoint for them): the match goes on, nobody plays
    n = min(4096, total - turns)
    rest += make_frame(2, struct.pack('<IH', turns, n) + b'\x00\x00' * n)
    turns += n
for i in range(count):
    code = ('CL%d' % i).encode()
    new_head = head[:code_at] + bytes([len(code)]) + code + head[code_at + 1 + code_len:]
    path = os.path.join(folder, 'room-CL%d-00000000.restart' % i)
    with open(path, 'wb') as out:
        out.write(magic + make_frame(1, new_head) + rest)
    os.chmod(path, 0o600)
PY
RR_WS="$(free_port)"
RR_NEWROOM="E2E-NEW-$RANDOM"
RR_PROTOCOL="$("$SERVER" --version 2> /dev/null | sed -n 's/.*(network protocol \([0-9][0-9]*\)).*/\1/p')"
# the program that asks, written to a file first: macOS ships bash 3.2, which cannot read a here-document inside a command substitution
RR_ASK_PY="$WORK/restore_answers.py"
cat > "$RR_ASK_PY" <<'PY'
import json, socket, struct, sys, time, urllib.request
ws, game, ctl, secret, room, protocol, launched, scale = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), sys.argv[4], sys.argv[5], int(sys.argv[6]), float(sys.argv[7]), float(sys.argv[8])
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))       # (the server is on this machine: no proxy)
def get(url, headers=None, data=None, method=None):
    request = urllib.request.Request(url, data=data, headers=headers or {}, method=method)
    with opener.open(request, timeout=1.0 * scale) as r:
        return r.status, json.loads(r.read().decode())
def answered(url):                                                          # (asked again until it answers: the time that it took is counted from the launch)
    while time.time() - launched < 40.0:
        try:
            return get(url)[1]
        except Exception:
            time.sleep(0.01)
    return None
busy = answered('http://127.0.0.1:%d/busy' % ws)
busy_seconds = time.time() - launched
answered('http://127.0.0.1:%d/healthz' % ctl)
made = False
try:
    status, _ = get('http://127.0.0.1:%d/rooms' % ctl, {'Authorization': 'Bearer ' + secret, 'Content-Type': 'application/json'}, json.dumps({'map': 'TINY.LVL', 'players': 2, 'code': room, 'seed': 3}).encode(), 'POST')
    made = status == 201
except Exception:
    pass
def frame(p): return struct.pack('<I', len(p)) + p
def str8(t):
    b = t.encode()
    return bytes([len(b)]) + b
# a Hello (HelloMsg, include/ants_net/protocol.hpp): type 1, protocol, name, listen port 0, any seat (255), the room's code, no token, no key, no turns, the platform byte (protocol 15: 0 = not told), the client kind (protocol 16: 0 = a game); no create block
hello = bytes([1]) + struct.pack('<H', protocol) + str8('Newcomer') + struct.pack('<H', 0) + bytes([255]) + str8(room) + str8('') + bytes(16) + struct.pack('<I', 0) + bytes([0, 0])
hello_seconds, welcome_seconds, welcome = 99.0, 99.0, False
if made:
    asked = time.time()
    sock = socket.create_connection(('127.0.0.1', game), timeout=2.0 * scale)
    sock.sendall(frame(hello))
    received = b''
    while time.time() - asked < 3.0 * scale and len(received) < 5:
        try:
            received += sock.recv(4096)
        except Exception:
            break
    hello_seconds, welcome_seconds = time.time() - asked, time.time() - launched
    welcome = len(received) >= 5 and received[4] == 2           # (a message of type Welcome: the room took the player)
print('%.3f %.3f %.3f %s %d' % (busy_seconds, welcome_seconds, hello_seconds, 'yes' if welcome else 'no', (busy or {}).get('matches', -1)))
PY
RR_T0="$(python3 -c 'import time; print(time.time())')"
rr_launch --ws-port "$RR_WS" --max-rooms 1200
RR_ANSWERS="$(python3 "$RR_ASK_PY" "$RR_WS" "$RR_PORT" "$RR_CTL" "$SECRET" "$RR_NEWROOM" "$RR_PROTOCOL" "$RR_T0" "$TIME_SCALE")"
read -r RR_BUSY_S RR_WELCOME_S RR_HELLO_S RR_WELCOME RR_BUSY_MATCHES <<< "$RR_ANSWERS"
check "while $RR_CLONES records wait for their replay GET /busy answers within $((2 * TIME_SCALE)) s of the server's launch (it did in $RR_BUSY_S s) and counts them (${RR_BUSY_MATCHES:-?} matches)" "$(python3 -c "print(0 if float('${RR_BUSY_S:-99}') < 2.0 * $TIME_SCALE and int('${RR_BUSY_MATCHES:--1}') >= 100 else 1)")"
check "a room that is made then takes its player at once (the Welcome came $RR_HELLO_S s after the Hello and $RR_WELCOME_S s after the launch, while the records are replayed)" "$(python3 -c "print(0 if '${RR_WELCOME:-no}' == 'yes' and float('${RR_HELLO_S:-99}') < 1.0 * $TIME_SCALE and float('${RR_WELCOME_S:-99}') < 4.0 * $TIME_SCALE else 1)")"
rr_running_rooms() { curl -s -m 3 -H "Authorization: Bearer $SECRET" "$RR_URL/rooms" | python3 -c 'import sys, json; print(sum(1 for r in json.load(sys.stdin).get("rooms", []) if r.get("state") == "running"))' 2> /dev/null; }
RR_BACK=1
for _ in $(seq 1 $((900 * TIME_SCALE))); do
    [ "$(rr_running_rooms)" -ge "$RR_CLONES" ] 2> /dev/null && { RR_BACK=0; break; }
    kill -0 "$SERVER_PID" 2> /dev/null || break                                # (a server that died is not waited for)
    sleep 0.2
done
RR_RESTORE_S="$(python3 -c "import time; print(round(time.time() - $RR_T0, 1))")"
# (a count that never comes is not always a restore that is slow: GET /rooms with a thousand rooms is one answer of about a megabyte, and the control interface answers 500 to one of more than 1 MiB)
[ "$RR_BACK" = "0" ] || echo "  [restore e2e] no count of $RR_CLONES running rooms: GET /rooms now answers $(curl -s -m 5 -o /dev/null -w '%{http_code} with %{size_download} bytes' -H "Authorization: Bearer $SECRET" "$RR_URL/rooms") (an answer may have 1048576 bytes at the most); the log has $(grep -c ' restored: ' "$WORK/rr_server.log") restored rooms"
check "all $RR_CLONES records are rooms again (running, restored) within $((3 * TIME_SCALE)) minutes" "$RR_BACK"
check "the log says that the records wait for their replay and that their rooms were restored" "$(grep -q "restore: $RR_CLONES restart record(s) wait for their replay" "$WORK/rr_server.log" && [ "$(grep -c ' restored: ' "$WORK/rr_server.log")" -ge "$RR_CLONES" ]; echo $?)"
kill -TERM "$SERVER_PID" 2> /dev/null
wait "$SERVER_PID" 2> /dev/null
SERVER_PID=""

echo "  [reconnect e2e] the link cut: the room paused after $RC_PAUSED_AFTER s; the cap dropped the seat $RC_CAPPED_AFTER s after the cut (60 s of pause); a client stopped: the room paused $RC_STOP_PAUSED_AFTER s later"
echo "  [restart e2e] SIGTERM $RR_STOPPED_AFTER s to exit with the record kept; the room came back with $RR_RESTORED_TURNS turns, held its two seats, and came back again after a SIGKILL; the two games found it by themselves"
echo "  [restore e2e] $RR_CLONES records of a long match were replayed in $RR_RESTORE_S s of the server's launch; meanwhile /busy answered after $RR_BUSY_S s (${RR_BUSY_MATCHES:-?} matches waited) and a new room's player had its Welcome $RR_WELCOME_S s after the launch"
fi

# ---- part replays: the matches that the server keeps (docs/REPLAYS.md "On the game server", docs/SERVER.md "Replays") ---------------------------------------------
# Two real games play a match on a room of the control interface; after 32 seconds of play (640 ticks: the server keeps a match from 600 turns, 30 seconds, however it ended) the owner closes the room. The match is kept
# as a file in the results folder, and the control interface (with the secret) and the public replay door (without) list it and give it out; the names that the two games typed ("Typed1", "Typed2") are in
# the list and in the file, the room's code and the address are not; replay_tool plays the downloaded file out to the same hashes and shows the names; the file is still there after a restart of the server; the
# door is not there when the server is started without it; the owner deletes the file.
if part_enabled replays; then
RP_PUB="$(free_port)"
RP_RESULTS="$WORK/rp_results"
RP_CODE="RP-ROOM-$RANDOM"
RP_PUBURL="http://127.0.0.1:$RP_PUB"
rp_server() {      # rp_server [options]: the server over the results folder, with the control interface; 0 when it answers
    ANTS_SERVER_SECRET="$SECRET" "$SERVER" --maps "$ROOT/Original-Ants/Maps" --port "$GAME_PORT" --ctl-port "$CTL_PORT" --results-dir "$RP_RESULTS" "$@" >> "$WORK/rp_server.log" 2>&1 &
    SERVER_PID=$!
    for _ in $(seq 1 50); do
        if curl -s -m 1 "$CTL/healthz" | grep -q '"ok"'; then return 0; fi
        kill -0 "$SERVER_PID" 2> /dev/null || return 1
        sleep 0.1
    done
    return 1
}
rp_auth() { curl -s -m 5 -H "Authorization: Bearer $SECRET" "$@"; }
rp_list_ok() {      # rp_list_ok FILE: the control interface's list holds this one replay of TINY.LVL and says the colours with the names that the two games typed (the game whose Hello came first has Green: either order is right), not the room's code
    rp_auth "$CTL/replays" | python3 -c '
import sys, json
d = json.load(sys.stdin)
r = d["replays"][0] if d["replays"] else {}
ok = d["enabled"] is True and d["count"] == 1 and len(d["replays"]) == 1 and d["keep_days"] == 30 and d["max_bytes"] == 100 * 1024 * 1024
ok = ok and r.get("file") == sys.argv[1] and r.get("readable") is True and r.get("map") == "TINY.LVL" and r.get("players") in (["Green (Typed1)", "Red (Typed2)"], ["Green (Typed2)", "Red (Typed1)"]) and r.get("finished") is False
ok = ok and r.get("turns", 0) >= 600 and r.get("bytes", 0) > 0
ok = ok and sys.argv[2] not in json.dumps(d)
sys.exit(0 if ok else 1)' "$1" "$RP_CODE"
}
rp_public_ok() {      # rp_public_ok FILE: the public list holds the same replay, with exactly the keys of the public answer and the same players
    curl -s -m 5 "$RP_PUBURL/replays" | python3 -c '
import sys, json
d = json.load(sys.stdin)
r = d["replays"][0] if d["replays"] else {}
ok = sorted(d) == ["count", "keep_days", "replays", "sim_rules"] and d["sim_rules"] >= 1 and d["count"] == 1 and len(d["replays"]) == 1 and d["keep_days"] == 30
ok = ok and sorted(r) == ["bytes", "ended", "file", "finished", "game", "map", "players", "rules", "seconds", "sim_rules", "turns"]
ok = ok and r["file"] == sys.argv[1] and r["map"] == "TINY.LVL" and r["players"] in (["Green (Typed1)", "Red (Typed2)"], ["Green (Typed2)", "Red (Typed1)"]) and r["finished"] is False and r["turns"] >= 600
ok = ok and sys.argv[2] not in json.dumps(d)
sys.exit(0 if ok else 1)' "$1" "$RP_CODE"
}
# the address of this machine on its network (the one a connection to elsewhere would leave from; empty where there is none): the public door must not answer there unless it is asked to
RP_HOST_IP="$(python3 -c '
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
try:
    s.connect(("192.0.2.1", 9))
    print(s.getsockname()[0])
except OSError:
    pass' 2> /dev/null)"
case "$RP_HOST_IP" in ""|127.*) RP_HOST_IP="" ;; esac
rp_server --replay-port "$RP_PUB"
check "the server is up over a results folder, with the control interface and the public replay door" "$?"
check "its log says where the replays are kept, for how long and how much, that the matches of demo rooms are not kept unless it is asked, and that the public door is open on this machine only" "$(grep -q "replays kept in $RP_RESULTS/replays for 30 days, at most 100 MiB; the matches of demo rooms are not kept (--replay-demo keeps them)" "$WORK/rp_server.log" && grep -q "public replays on port $RP_PUB (this machine only)" "$WORK/rp_server.log"; echo $?)"
check "nothing is kept yet: the control interface says enabled with an empty list" "$(rp_auth "$CTL/replays" | python3 -c 'import sys, json; d = json.load(sys.stdin); sys.exit(0 if d["enabled"] is True and d["count"] == 0 and d["replays"] == [] and d["bytes"] == 0 else 1)'; echo $?)"
check "... and so does the public door (200 JSON that nobody may cache, no secret asked)" "$(curl -s -i -m 5 "$RP_PUBURL/replays" | tr -d '\r' | python3 -c '
import sys, json
text = sys.stdin.read()
head, body = text.split("\n\n", 1)
lines = head.split("\n")
ok = lines[0] == "HTTP/1.1 200 OK" and any(l.lower() == "cache-control: no-store" for l in lines) and any(l.lower() == "content-type: application/json" for l in lines)
d = json.loads(body)
ok = ok and d.pop("sim_rules", 0) >= 1 and d == {"replays": [], "count": 0, "keep_days": 30}
sys.exit(0 if ok else 1)'; echo $?)"
# a match of two real games
curl -s -m 3 -X POST -H "Authorization: Bearer $SECRET" -d "{\"map\":\"TINY.LVL\",\"players\":2,\"code\":\"$RP_CODE\",\"seed\":7}" "$CTL/rooms" > /dev/null
RP_PIDS=""
for i in 1 2; do
    "$GAME" --headless --no-lan --name "Typed$i" --join "127.0.0.1:$GAME_PORT" --room "$RP_CODE" --screenshot "$WORK/rp$i.png" --frames 4000000 > "$WORK/rp$i.log" 2>&1 &
    RP_PIDS="$RP_PIDS $!"
done
RP_RUNNING=1
for _ in $(seq 1 $((150 * TIME_SCALE))); do
    if rp_auth "$CTL/rooms/$RP_CODE" | grep -q '"state":"running"'; then RP_RUNNING=0; break; fi
    sleep 0.2
done
check "two games with typed names joined and the match started by itself" "$RP_RUNNING"
RP_TICKS=0
for _ in $(seq 1 $((500 * TIME_SCALE))); do
    RP_TICKS="$(ticks_of "$RP_CODE")"
    if [ "${RP_TICKS:-0}" -ge 640 ] 2> /dev/null; then break; fi
    sleep 0.2
done
check "the match ran 640 ticks (32 seconds of play, past the 600 turns, 30 seconds, that a match needs to be kept however it ended): $RP_TICKS" "$([ "${RP_TICKS:-0}" -ge 640 ] 2> /dev/null; echo $?)"
check "while the match runs nothing is kept yet and the room says so (replay kept false, note empty)" "$(rp_auth "$CTL/rooms/$RP_CODE" | python3 -c 'import sys, json; r = json.load(sys.stdin)["replay"]; sys.exit(0 if r["kept"] is False and r["file"] == "" and r["note"] == "" else 1)'; echo $?)"
RP_CLOSED="$(rp_auth -X DELETE "$CTL/rooms/$RP_CODE")"
for p in $RP_PIDS; do kill "$p" 2> /dev/null; done
for p in $RP_PIDS; do wait "$p" 2> /dev/null; done
RP_PIDS=""
RP_FILE="$(echo "$RP_CLOSED" | python3 -c 'import sys, json; d = json.load(sys.stdin); print(d["replay"]["file"] if d["state"] == "failed" and d["replay"]["kept"] else "")' 2> /dev/null)"
# (the store does not write a file when the disk would be left with less than 256 MiB free: say so, with the room's own reason, rather than only that the check below failed)
[ -n "$RP_FILE" ] || echo "  [replays e2e] the match was not kept. The room says: $(echo "$RP_CLOSED" | python3 -c 'import sys, json; print(json.load(sys.stdin)["replay"]["note"])' 2> /dev/null). The disk of the work folder has $(df -Pk "$WORK" | awk 'NR == 2 {printf "%d", $4 / 1024}') MiB free (the server keeps a reserve of 256 MiB)"
check "closing the room (state failed, closed by the owner) keeps its match as ants-TINY-<date>-<time>Z.antsrep: the answer says so [$RP_FILE]" "$(echo "$RP_FILE" | grep -qE '^ants-TINY-[0-9]{8}-[0-9]{6}Z\.antsrep$'; echo $?)"
check "the file is in the replays folder of the results folder, and its status in the room list says kept and how big it is" "$([ -s "$RP_RESULTS/replays/$RP_FILE" ] && rp_auth "$CTL/rooms/$RP_CODE" | python3 -c 'import sys, json; r = json.load(sys.stdin)["replay"]; sys.exit(0 if r["kept"] and r["bytes"] > 0 and r["note"] == "" else 1)'; echo $?)"
check "the server's log line of the ended room says kept as that file" "$(grep -q "kept as $RP_FILE" "$WORK/rp_server.log"; echo $?)"
check "the control interface lists it: TINY.LVL, Green and Red with the names that the two games typed (and not the room's code), not finished, 600 turns or more, readable" "$(rp_list_ok "$RP_FILE"; echo $?)"
check "the public door lists the same replay, with the keys of the public answer, the same names and no 'readable'" "$(rp_public_ok "$RP_FILE"; echo $?)"
rp_auth -o "$WORK/rp_control.antsrep" "$CTL/replays/$RP_FILE"
curl -s -m 5 -o "$WORK/rp_public.antsrep" -D "$WORK/rp_public.head" "$RP_PUBURL/replays/$RP_FILE"
check "both give the file: the same bytes as on disk, application/octet-stream and no-store from the public door" "$(cmp -s "$WORK/rp_control.antsrep" "$RP_RESULTS/replays/$RP_FILE" && cmp -s "$WORK/rp_public.antsrep" "$RP_RESULTS/replays/$RP_FILE" && tr -d '\r' < "$WORK/rp_public.head" | grep -qi '^content-type: application/octet-stream$' && tr -d '\r' < "$WORK/rp_public.head" | grep -qi '^cache-control: no-store$'; echo $?)"
check "the file is a replay file (its first eight bytes are the signature), holds the names that the two games typed, and holds neither the room's code nor an address" "$(python3 -c 'import sys; sys.exit(0 if open(sys.argv[1], "rb").read(8) == bytes([0x89, 0x41, 0x52, 0x50, 0x4C, 0x0D, 0x0A, 0x1A]) else 1)' "$WORK/rp_public.antsrep" && grep -qa 'Typed1' "$WORK/rp_public.antsrep" && grep -qa 'Typed2' "$WORK/rp_public.antsrep" && ! grep -qa "$RP_CODE" "$WORK/rp_public.antsrep" && ! grep -qa '127\.0\.0\.1' "$WORK/rp_public.antsrep"; echo $?)"
RT="$ROOT/$BUILD/replay_tool"
if [ -x "$RT" ]; then
    "$RT" info "$WORK/rp_public.antsrep" > "$WORK/rp_info.log" 2>&1
    RP_INFO=$?
    "$RT" verify "$WORK/rp_public.antsrep" --maps-dir "$ROOT/Original-Ants/Maps" > "$WORK/rp_verify.log" 2>&1
    RP_VERIFY=$?
    [ "$RP_VERIFY" -ne 0 ] && sed 's/^/    /' "$WORK/rp_verify.log"
    check "replay_tool info reads the downloaded file (TINY.LVL, the seats with the typed names, no room code) and verify plays the match out to every hash in it (exit 0) and names the players too" "$([ "$RP_INFO" = "0" ] && [ "$RP_VERIFY" = "0" ] && grep -q 'TINY' "$WORK/rp_info.log" && grep -q '^Seats:     Green (Typed[12]), Red (Typed[12])$' "$WORK/rp_info.log" && grep -q 'Typed1' "$WORK/rp_info.log" && grep -q 'Typed2' "$WORK/rp_info.log" && grep -q 'Typed' "$WORK/rp_verify.log" && ! grep -q "$RP_CODE" "$WORK/rp_info.log" "$WORK/rp_verify.log"; echo $?)"
else
    echo "  SKIP: replay_tool is not built ($RT): the downloaded file was NOT played again"
fi
# who may ask for what
check "the control interface asks for the secret: the list and a file are 401 without it" "$([ "$(code_of "$CTL/replays")" = "401" ] && [ "$(code_of "$CTL/replays/$RP_FILE")" = "401" ] && [ "$(code_of -H "Authorization: Bearer wrong-$SECRET" "$CTL/replays")" = "401" ]; echo $?)"
check "the control interface refuses a bad name or query: a path, another name and a parameter that is not limit are 404 / 400, ?limit=1 works, ?limit=0 and ?limit=1001 are 400" "$([ "$(code_of -H "Authorization: Bearer $SECRET" "$CTL/replays/..%2Fcontrol-secret")" = "404" ] && [ "$(code_of -H "Authorization: Bearer $SECRET" "$CTL/replays/ants-TINY-20200101-000000Z.antsrep")" = "404" ] && [ "$(code_of -H "Authorization: Bearer $SECRET" "$CTL/replays?x=1")" = "400" ] && [ "$(code_of -H "Authorization: Bearer $SECRET" "$CTL/replays?limit=1")" = "200" ] && [ "$(code_of -H "Authorization: Bearer $SECRET" "$CTL/replays?limit=0")" = "400" ] && [ "$(code_of -H "Authorization: Bearer $SECRET" "$CTL/replays?limit=1001")" = "400" ]; echo $?)"
check "the public door is read only and answers nothing else: POST and DELETE are 405 (Allow: GET), a query and any other path are 404, a request with a body is 400, and the secret opens nothing there" "$([ "$(code_of -X POST "$RP_PUBURL/replays")" = "405" ] && [ "$(code_of -X DELETE "$RP_PUBURL/replays/$RP_FILE")" = "405" ] && curl -s -i -m 3 -X POST "$RP_PUBURL/replays" | tr -d '\r' | grep -qi '^allow: GET$' && [ "$(code_of "$RP_PUBURL/replays?x=1")" = "404" ] && [ "$(code_of "$RP_PUBURL/replays/$RP_FILE?x=1")" = "404" ] && [ "$(code_of "$RP_PUBURL/rooms")" = "404" ] && [ "$(code_of "$RP_PUBURL/stats")" = "404" ] && [ "$(code_of "$RP_PUBURL/")" = "404" ] && [ "$(code_of -H "Authorization: Bearer $SECRET" "$RP_PUBURL/rooms/$RP_CODE")" = "404" ] && [ "$(code_of --path-as-is "$RP_PUBURL/replays/../control-secret")" = "404" ] && [ "$(code_of "$RP_PUBURL/replays/ants-TINY-20200101-000000Z.antsrep")" = "404" ] && [ "$(code_of "$RP_PUBURL/replays/$RP_FILE.tmp")" = "404" ] && [ "$(code_of -X GET -d 'x' "$RP_PUBURL/replays")" = "400" ] && [ "$(code_of -X GET -d 'x' "$RP_PUBURL/replays/$RP_FILE")" = "400" ] && [ "$(code_of "$RP_PUBURL/healthz")" = "200" ]; echo $?)"
if [ -n "$RP_HOST_IP" ]; then
    check "the public door answers on this machine only: on this machine's network address ($RP_HOST_IP) the connection is refused" "$([ "$(code_of "http://$RP_HOST_IP:$RP_PUB/replays")" = "000" ]; echo $?)"
else
    echo "  SKIP: this machine has no address of its own on a network: the public door was NOT tried from one"
fi
# the files are the store: a restart of the server loses nothing
stop_server
rp_server --replay-port "$RP_PUB"
check "after the server was stopped (SIGTERM) and started again over the same folder, the match is in both lists, with the same bytes" "$(rp_list_ok "$RP_FILE" && rp_public_ok "$RP_FILE" && curl -s -m 5 "$RP_PUBURL/replays/$RP_FILE" | cmp -s - "$RP_RESULTS/replays/$RP_FILE"; echo $?)"
# the door is off unless it is asked for
stop_server
RP_LOG_LINES="$(wc -l < "$WORK/rp_server.log" | tr -d ' ')"      # (this start's lines are the ones after these: the log is one file for all the starts of the part)
rp_server
check "a server started without --replay-port has no public door (the connection is refused) and still lists the replay for its owner" "$([ "$(code_of "$RP_PUBURL/replays")" = "000" ] && rp_list_ok "$RP_FILE" && ! tail -n +$((RP_LOG_LINES + 1)) "$WORK/rp_server.log" | grep -q "public replays on port"; echo $?)"
stop_server
rp_server --no-replays
check "--no-replays keeps nothing and lists nothing (enabled false, 404 for the old file), and the old file is left alone on disk" "$(rp_auth "$CTL/replays" | python3 -c 'import sys, json; d = json.load(sys.stdin); sys.exit(0 if d["enabled"] is False and d["replays"] == [] else 1)' && [ "$(code_of -H "Authorization: Bearer $SECRET" "$CTL/replays/$RP_FILE")" = "404" ] && [ -s "$RP_RESULTS/replays/$RP_FILE" ]; echo $?)"
stop_server
# the owner deletes it
rp_server --replay-port "$RP_PUB"
check "the owner deletes the replay: 200 with the name, then the list is empty, the file is gone from disk and the public door says 404" "$(rp_auth -X DELETE "$CTL/replays/$RP_FILE" | python3 -c 'import sys, json; sys.exit(0 if json.load(sys.stdin) == {"deleted": sys.argv[1]} else 1)' "$RP_FILE" && [ ! -e "$RP_RESULTS/replays/$RP_FILE" ] && [ "$(code_of "$RP_PUBURL/replays/$RP_FILE")" = "404" ] && curl -s -m 5 "$RP_PUBURL/replays" | python3 -c 'import sys, json; d = json.load(sys.stdin); sys.exit(0 if d["count"] == 0 and d["replays"] == [] else 1)'; echo $?)"
check "a second delete of the same name is 404" "$([ "$(code_of -X DELETE -H "Authorization: Bearer $SECRET" "$CTL/replays/$RP_FILE")" = "404" ]; echo $?)"
stop_server
# the settings reach the store: its own folder, 7 days, 5 MiB and the demo option are in the log and in both lists, whatever the defaults are
RP_OTHER="$WORK/rp_other_folder"
rp_server --replay-port "$RP_PUB" --replay-any-interface --replays-dir "$RP_OTHER" --replays-days 7 --replays-max-mb 5 --replay-demo
check "a server with its own replays folder, 7 days, 5 MiB and --replay-demo says so in its log, and makes the folder" "$(grep -q "replays kept in $RP_OTHER for 7 days, at most 5 MiB; the matches of demo rooms are kept too (--replay-demo)" "$WORK/rp_server.log" && [ -d "$RP_OTHER" ]; echo $?)"
check "with --replay-any-interface the log says that the door is open to every interface, and (where the machine has an address) it answers there" "$(grep -q "public replays on port $RP_PUB (all interfaces: the host must restrict it)" "$WORK/rp_server.log" && { [ -z "$RP_HOST_IP" ] || [ "$(code_of "http://$RP_HOST_IP:$RP_PUB/replays")" = "200" ]; }; echo $?)"
check "... and the control interface and the public door both say 7 days (and the first one 5 MiB) for a list that is empty" "$(rp_auth "$CTL/replays" | python3 -c 'import sys, json; d = json.load(sys.stdin); sys.exit(0 if d["enabled"] is True and d["count"] == 0 and d["keep_days"] == 7 and d["max_bytes"] == 5 * 1024 * 1024 else 1)' && curl -s -m 5 "$RP_PUBURL/replays" | python3 -c 'import sys, json; d = json.load(sys.stdin); sys.exit(0 if d.pop("sim_rules", 0) >= 1 and d == {"replays": [], "count": 0, "keep_days": 7} else 1)'; echo $?)"
stop_server
fi

echo "server e2e${PART_LABEL}: $CHECKS checks, $FAILS failures"
[ "$FAILS" -eq 0 ]
