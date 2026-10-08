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
#   ANTS_ARENA_SEED=N      the random choices (map, number of bots, levels, the match seed) come from N, so that the same N plays the same match; not set: from the clock
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
esac
EVERY=$((10#$EVERY))
if (( EVERY < 5 )); then log "ANTS_ARENA_EVERY_MIN=$EVERY is less than 5: using 5"; EVERY=5; fi
if (( EVERY > 1440 )); then log "ANTS_ARENA_EVERY_MIN=$EVERY is more than 1440: using 1440"; EVERY=1440; fi

case ${ANTS_ARENA_SEED:-} in
    '') RANDOM=$(( $(date +%s) & 32767 )) ;;
    *[!0-9]*) log "ANTS_ARENA_SEED='${ANTS_ARENA_SEED}' is not a whole number: taking the clock"; RANDOM=$(( $(date +%s) & 32767 )) ;;
    *) RANDOM=$((10#${ANTS_ARENA_SEED} & 32767)) ;;
esac

MAPS=(TINY SMALL MEDIUM GAUNTLET TREASURE ISLANDS)       # the six maps of the original game
LEVELS=(medium hard)                                     # (a match of two to four standard bots; two easy bots do hardly anything worth watching)

# One match: a map, two to four bots on seats 0 to 3, a seed. Returns the program's exit code.
play_one() {
    local map seats seed i level code
    local -a args=()
    map=${MAPS[RANDOM % ${#MAPS[@]}]}
    seats=$((2 + RANDOM % 3))
    seed=$(( ((RANDOM << 15) | RANDOM) + 1 ))
    for ((i = 0; i < seats; i++)); do
        level=${LEVELS[RANDOM % ${#LEVELS[@]}]}
        args+=(--seat "$i=standard:$level")
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
log "starting: a match every $EVERY minutes into $DIR"
while (( stop == 0 )); do
    play_one
    (( stop )) && break
    sleep $((EVERY * 60)) &
    sleeper=$!
    wait "$sleeper" 2>/dev/null
    sleeper=0
done
log "stopped"
exit 0
