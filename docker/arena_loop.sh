#!/bin/bash
# The bot arena of the stack (docker-compose.stack.yml, service ants-arena; docs/BOTS.md "On the game server"): every ANTS_ARENA_EVERY_MIN minutes it plays ONE match of computer
# players against each other, headless, and keeps it as a replay in the folder where the game server keeps its own (/results/replays), so that the site's list (web/watch.html)
# shows it as "Bot (Level)" next to the matches of players. The server looks at its folder again every 30 seconds (docs/REPLAYS.md "Matches of computer players").
#
#   ANTS_ARENA_EVERY_MIN   minutes between two matches: 5 to 1440 (default 60); a value out of range or not a number is replaced by the nearest one (the container keeps running)
#   ANTS_ARENA_DIR         where the replays go (default /results/replays: the folder of the game server in the volume ants-server-results)
#   ANTS_ARENA_MAPS_DIR    where the maps are (default /maps)
#   ANTS_ARENA_BIN         the arena program (default /usr/local/bin/bot_arena)
#   ANTS_ARENA_ONCE=1      play one match and exit with the program's exit code (the check of the CI, or a cron job on a host)
#   ANTS_ARENA_SEED=N      the random choices (map, number of bots, the seats they sit on, levels, the match seed) come from N, so that the same N plays the same matches; not set: from the
#                          clock (in the loop, the start line of the log says which N that was, so that the run can be played again with the same bash; the "match:" line of each match is
#                          the portable way to play it again)
#
# Every match is logged with its map, seats and seed: the same arguments played again give the same match bit for bit (bot_arena --map MAP --seeds SEED --seat ...).
# The arena's own store deletes nothing; the game server's store purges its folder (ANTS_REPLAY_DAYS, ANTS_REPLAY_MAX_MB, the oldest first), these files with the others.
set -u

BIN=${ANTS_ARENA_BIN:-/usr/local/bin/bot_arena}
DIR=${ANTS_ARENA_DIR:-/results/replays}
MAPS_DIR=${ANTS_ARENA_MAPS_DIR:-/maps}
ONCE=${ANTS_ARENA_ONCE:-0}
EVERY=${ANTS_ARENA_EVERY_MIN:-60}

log() { printf '%s arena: %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*"; }

case $EVERY in
    ''|*[!0-9]*) log "ANTS_ARENA_EVERY_MIN='$EVERY' is not a whole number: using 60"; EVERY=60 ;;
    ??????????*) log "ANTS_ARENA_EVERY_MIN='$EVERY' is more than 1440: using 1440"; EVERY=1440 ;;       # (ten digits or more: too long for the arithmetic below, which would wrap around)
esac
EVERY=$((10#$EVERY))
if (( EVERY < 5 )); then log "ANTS_ARENA_EVERY_MIN=$EVERY is less than 5: using 5"; EVERY=5; fi
if (( EVERY > 1440 )); then log "ANTS_ARENA_EVERY_MIN=$EVERY is more than 1440: using 1440"; EVERY=1440; fi

case ${ANTS_ARENA_SEED:-} in
    '') STREAM=$(( $(date +%s) & 32767 )) ;;
    *[!0-9]*|??????????*) log "ANTS_ARENA_SEED='${ANTS_ARENA_SEED}' is not a whole number of nine digits or less: taking the clock"; STREAM=$(( $(date +%s) & 32767 )) ;;
    *) STREAM=$((10#${ANTS_ARENA_SEED} & 32767)) ;;
esac
RANDOM=$STREAM

MAPS=(TINY SMALL MEDIUM GAUNTLET TREASURE ISLANDS)       # the six maps of the original game (each has four hills: any seat can play on any of them)
LEVELS=(medium hard)                                     # (a match of two to four standard bots; two easy bots do hardly anything worth watching)

# One match: a map, two to four bots on seats chosen from 0 to 3 (green, red, blue, black), a level for each, a seed. Returns the program's exit code.
# Every choice is uniform and independent of the others: Green is not always in the match, and no seat is more likely than another to get a Hard bot.
play_one() {
    local map count need seat seed level code
    local -a args=()
    map=${MAPS[RANDOM % ${#MAPS[@]}]}
    count=$((2 + RANDOM % 3))
    seed=$(( ((RANDOM << 15) | RANDOM) + 1 ))
    need=$count
    for ((seat = 0; seat < 4; seat++)); do               # (a seat is taken with the chance need / seats left: every set of `count` seats is as likely as any other)
        if (( RANDOM % (4 - seat) < need )); then
            level=${LEVELS[RANDOM % ${#LEVELS[@]}]}
            args+=(--seat "$seat=standard:$level")
            need=$((need - 1))
        fi
    done
    log "match: --map $map --seeds $seed ${args[*]}"
    "$BIN" --maps-dir "$MAPS_DIR" --map "$map" --seeds "$seed" "${args[@]}" --save-replays "$DIR" --quiet
    code=$?
    if (( code == 0 )); then log "match done"; else log "bot_arena exited with code $code"; fi
    return $code
}

if [ "$ONCE" = 1 ]; then
    play_one
    exit $?
fi

stop=0
sleeper=0
trap 'stop=1; [ "$sleeper" -gt 0 ] && kill "$sleeper" 2>/dev/null' TERM INT
log "starting: a match every $EVERY minutes into $DIR (random choices from seed $STREAM)"
while (( stop == 0 )); do
    play_one
    (( stop )) && break
    sleep $((EVERY * 60)) &
    sleeper=$!
    (( stop )) && kill "$sleeper" 2>/dev/null        # (a SIGTERM between the check above and the line before this one would have found no sleeper to end)
    wait "$sleeper" 2>/dev/null
    sleeper=0
done
log "stopped"
exit 0
