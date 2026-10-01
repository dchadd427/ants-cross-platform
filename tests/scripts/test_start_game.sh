#!/usr/bin/env bash
# Checks start_game.sh without starting anything: its --dry-run prints the command line of every window.
# The rig: window i is player i and colour i (0 green, 1 red, 2 blue, 3 black), window 0 hosts on this machine only, the others join and ask for their seat,
# the windows lie in a 2 x 2 grid (2 x 1 for two), every name is different and random, nothing grabs the pointer, only the focused window has sound.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="$ROOT/start_game.sh"
FAILS=0
CHECKS=0

check() {      # check "what" condition-exit-status
    CHECKS=$((CHECKS + 1))
    if [ "$2" -ne 0 ]; then
        FAILS=$((FAILS + 1))
        echo "  FAIL: $1"
    fi
}
has() { case "$1" in *"$2"*) return 0 ;; *) return 1 ;; esac; }
word_after() { # the word after an option in a dry-run line: word_after "line" --name  (names have no spaces)
    local prev=""
    for w in $1; do
        if [ "$prev" = "$2" ]; then echo "$w"; return; fi
        prev="$w"
    done
}

echo "[start script] dry runs: seats, colours, names, grid, host and guests, the options that keep the pointer free"

OUT="$("$SCRIPT" --dry-run)"
check "four windows" "$([ "$(echo "$OUT" | wc -l | tr -d ' ')" -eq 4 ]; echo $?)"
COLOURS=(Green Red Blue Black)
NAMES_SEEN=""
n=0
while IFS= read -r line; do
    has "$line" "--cell $n "; check "window $n sits in cell $n" $?
    has "$line" "--grid 2x2"; check "window $n: 2 x 2 grid" $?
    has "$line" "--audio-focus"; check "window $n: sound only with the focus" $?
    has "$line" "--no-lan"; check "window $n: no announcements on the network" $?
    has "$line" "Ants\\ -\\ ${COLOURS[$n]}\\ \\("; check "window $n: title names the colour ${COLOURS[$n]}" $?
    name="$(word_after "$line" --name)"
    check "window $n has a name" "$([ -n "$name" ]; echo $?)"
    has "$NAMES_SEEN" " $name "; [ $? -ne 0 ]; check "window $n: the name $name is not used twice" $?
    NAMES_SEEN="$NAMES_SEEN $name "
    has "$line" "($name)" || has "$line" "\\($name\\)"; check "window $n: the title carries the name" $?
    if [ "$n" -eq 0 ]; then
        has "$line" "--host 4001 --loopback"; check "window 0 hosts on port 4001, this machine only" $?
        has "$line" "--join"; [ $? -ne 0 ]; check "window 0 does not join" $?
    else
        has "$line" "--join 127.0.0.1:4001 --seat $n"; check "window $n joins 127.0.0.1:4001 and asks for seat $n" $?
        has "$line" "--host"; [ $? -ne 0 ]; check "window $n does not host" $?
    fi
    has "$line" "--fullscreen"; [ $? -ne 0 ]; check "window $n: not fullscreen" $?
    n=$((n + 1))
done <<< "$OUT"

# the names are random, and repeatable on request
A="$(ANTS_NAMES_SEED=11 "$SCRIPT" --dry-run)"
B="$(ANTS_NAMES_SEED=11 "$SCRIPT" --dry-run)"
check "the same seed gives the same names" "$([ "$A" = "$B" ]; echo $?)"
DIFFERENT=1
for seed in 1 2 3 4 5 6 7 8; do
    C="$(ANTS_NAMES_SEED=$seed "$SCRIPT" --dry-run)"
    if [ "$C" != "$A" ]; then DIFFERENT=0; fi
done
check "other seeds give other names" "$DIFFERENT"
R1="$("$SCRIPT" --dry-run)"; R2="$("$SCRIPT" --dry-run)"; R3="$("$SCRIPT" --dry-run)"
check "without a seed the names change from run to run" "$([ "$R1" != "$R2" ] || [ "$R2" != "$R3" ]; echo $?)"

# fewer windows, another port, extra arguments
OUT="$("$SCRIPT" --dry-run --players 2)"
check "two windows" "$([ "$(echo "$OUT" | wc -l | tr -d ' ')" -eq 2 ]; echo $?)"
has "$OUT" "--grid 2x1"; check "two windows sit side by side" $?
OUT="$("$SCRIPT" --players 3 --dry-run)"
check "three windows" "$([ "$(echo "$OUT" | wc -l | tr -d ' ')" -eq 3 ]; echo $?)"
has "$OUT" "--seat 2"; check "the third window asks for seat 2 (blue)" $?
has "$OUT" "--seat 3"; [ $? -ne 0 ]; check "there is no fourth window" $?
OUT="$(ANTS_PORT=5123 "$SCRIPT" --dry-run --fog-test)"
has "$OUT" "--host 5123 --loopback"; check "ANTS_PORT moves the room" $?
has "$OUT" "--join 127.0.0.1:5123"; check "... and the guests follow" $?
check "an extra argument reaches every window" "$([ "$(echo "$OUT" | grep -c -- '--fog-test')" -eq 4 ]; echo $?)"
OUT="$("$SCRIPT" --single --dry-run --map X)"
check "--single: one window" "$([ "$(echo "$OUT" | wc -l | tr -d ' ')" -eq 1 ]; echo $?)"
has "$OUT" "--host"; [ $? -ne 0 ]; check "--single: no room" $?
has "$OUT" "--map X"; check "--single: the arguments still reach the game" $?
OUT="$("$SCRIPT" --players 1 --dry-run)"
check "--players 1 is --single" "$([ "$(echo "$OUT" | wc -l | tr -d ' ')" -eq 1 ]; echo $?)"
# the options of one game make it a single game (what the README shows), unless --players is given
for one in "--host" "--join 10.0.0.5" "--bot 1:medium" "--lan-list" "--headless" "--screenshot x.png" "--map X.LVL"; do
    OUT="$("$SCRIPT" --dry-run $one)"
    check "$one alone: one window, as before" "$([ "$(echo "$OUT" | wc -l | tr -d ' ')" -eq 1 ]; echo $?)"
    has "$OUT" "--grid"; [ $? -ne 0 ]; check "$one alone: no grid, no seat, nothing added" $?
done
OUT="$("$SCRIPT" --dry-run --host --name Alice --loopback)"
check "a one-off host keeps its own arguments and adds none" "$([ "$OUT" = "./build/src/ants_app/ants --host --name Alice --loopback" ]; echo $?)"
# --bot is an option of one game: it reaches the game as it is, and cannot be combined with extra windows (a guest runs no bots)
OUT="$("$SCRIPT" --dry-run --bot 1:medium --name Alice)"
check "--bot reaches the one game unchanged" "$([ "$OUT" = "./build/src/ants_app/ants --bot 1:medium --name Alice" ]; echo $?)"
OUT="$("$SCRIPT" --dry-run --host --bot 2:hard --name Alice)"
check "a host with a bot: one window, the arguments as given" "$([ "$OUT" = "./build/src/ants_app/ants --host --bot 2:hard --name Alice" ]; echo $?)"
for n in 2 3 4; do
    OUT="$("$SCRIPT" --dry-run --players "$n" --bot 1:medium 2>/dev/null)"; st=$?
    check "--players $n with --bot is refused (exit status 2)" "$([ "$st" -eq 2 ]; echo $?)"
    check "--players $n with --bot starts nothing" "$([ -z "$OUT" ]; echo $?)"
done
OUT="$("$SCRIPT" --dry-run --players 1 --bot 1:medium)"
check "--players 1 with --bot is the one game" "$([ "$(echo "$OUT" | wc -l | tr -d ' ')" -eq 1 ]; echo $?)"
OUT="$("$SCRIPT" --dry-run --players 2 --headless)"
check "--players wins over the one-game options" "$([ "$(echo "$OUT" | wc -l | tr -d ' ')" -eq 2 ]; echo $?)"
for bad in 0 5 x ""; do
    OUT="$("$SCRIPT" --players "$bad" --dry-run 2>/dev/null)"; st=$?
    check "--players '$bad' is refused (exit status 2)" "$([ "$st" -eq 2 ]; echo $?)"
    check "--players '$bad' starts nothing" "$([ -z "$OUT" ]; echo $?)"
done

echo "start script: $CHECKS checks, $FAILS failures"
[ "$FAILS" -eq 0 ]
