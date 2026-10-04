#!/usr/bin/env bash
# Checks start_game.sh without starting anything: its --dry-run prints the command line of every window.
# A bare start is one plain game (the start menu). The rig (--players N): window i is player i and colour i (0 green, 1 red, 2 blue, 3 black), window 0 hosts on this machine only, the others join and ask for their seat,
# the windows lie in a 2 x 2 grid (2 x 1 for two), every name is different and random, nothing grabs the pointer, only the focused window has sound.
# Where each window lies is the owner's layout (the same as the games on web/lobby.html, and the way the hills lie on the Small and Treasure maps):
# black top left, green top right, red bottom left, blue bottom right.
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

cells_of() {   # the --cell of every window of a dry run, in window order: "1 2 3 0"
    local cells="" line
    while IFS= read -r line; do cells="$cells $(word_after "$line" --cell)"; done <<< "$1"
    echo "${cells# }"
}

echo "[start script] dry runs: seats, colours, cells, names, grid, host and guests, the options that keep the pointer free"

# a bare start is one plain game: the bare program, no mode flag, so the desktop start menu appears (Single player, Join, Host, Quit)
OUT="$("$SCRIPT" --dry-run)"
check "a bare start is one window, the bare program: the start menu appears" "$([ "$OUT" = "./build/src/ants_app/ants" ]; echo $?)"
OUT="$("$SCRIPT" --dry-run --name Bob)"
check "a bare start passes the arguments to the one game" "$([ "$OUT" = "./build/src/ants_app/ants --name Bob" ]; echo $?)"

OUT="$("$SCRIPT" --dry-run --players 4)"
check "--players 4: four windows" "$([ "$(echo "$OUT" | wc -l | tr -d ' ')" -eq 4 ]; echo $?)"
COLOURS=(Green Red Blue Black)
# The owner's layout: green top right, red bottom left, blue bottom right, black top left, the way the four hills lie on the Small and Treasure maps (the games
# on web/lobby.html lie the same way). --cell counts row by row (0 top left, 1 top right, 2 bottom left, 3 bottom right), so by seat (window n is seat n):
# green 1, red 2, blue 3, black 0. This check used to say that window n sits in cell n (green top left, red top right, blue bottom left, black bottom right),
# the order of the seats; the owner's layout replaces it, so it is rewritten (each window's cell is still checked exactly, nothing is weakened).
CELLS=(1 2 3 0)
PLACES=("top right" "bottom left" "bottom right" "top left")
NAMES_SEEN=""
n=0
while IFS= read -r line; do
    cell="$(word_after "$line" --cell)"
    check "window $n (${COLOURS[$n]}) sits in cell ${CELLS[$n]}, ${PLACES[$n]} (the owner's layout), not in cell $n" "$([ "$cell" = "${CELLS[$n]}" ]; echo $?)"
    has "$line" "--grid 2x2"; check "window $n: 2 x 2 grid" $?
    has "$line" "--audio-focus"; check "window $n: sound only with the focus" $?
    has "$line" "--aspect"; [ $? -ne 0 ]; check "window $n: no --aspect, so the window has the game's default shape (16:9) and its cell's largest rectangle of it" $?
    has "$line" "--window-size"; [ $? -ne 0 ]; check "window $n: no --window-size: the grid's cell decides" $?
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
    # the desktop start menu shows in a native game that has no mode on its command line: every window of the rig has one (--host or --join), so no window shows the menu
    { has "$line" "--host" || has "$line" "--join"; }; check "window $n carries a mode flag (--host or --join): the start menu never shows in the four-window rig" $?
    n=$((n + 1))
done <<< "$OUT"
check "the cells of the four windows, by seat, are 1 2 3 0 (green, red, blue, black)" "$([ "$(cells_of "$OUT")" = "1 2 3 0" ]; echo $?)"
SORTED="$(cells_of "$OUT" | tr ' ' '\n' | sort | tr '\n' ' ')"
check "the four windows fill the four cells of the grid, each once (none lies on another)" "$([ "$SORTED" = "0 1 2 3 " ]; echo $?)"

# the names are random, and repeatable on request
A="$(ANTS_NAMES_SEED=11 "$SCRIPT" --dry-run --players 4)"
B="$(ANTS_NAMES_SEED=11 "$SCRIPT" --dry-run --players 4)"
check "the same seed gives the same names" "$([ "$A" = "$B" ]; echo $?)"
DIFFERENT=1
for seed in 1 2 3 4 5 6 7 8; do
    C="$(ANTS_NAMES_SEED=$seed "$SCRIPT" --dry-run --players 4)"
    if [ "$C" != "$A" ]; then DIFFERENT=0; fi
done
check "other seeds give other names" "$DIFFERENT"
R1="$("$SCRIPT" --dry-run --players 4)"; R2="$("$SCRIPT" --dry-run --players 4)"; R3="$("$SCRIPT" --dry-run --players 4)"
check "without a seed the names change from run to run" "$([ "$R1" != "$R2" ] || [ "$R2" != "$R3" ]; echo $?)"

# fewer windows, another port, extra arguments
OUT="$("$SCRIPT" --dry-run --players 2)"
check "two windows" "$([ "$(echo "$OUT" | wc -l | tr -d ' ')" -eq 2 ]; echo $?)"
has "$OUT" "--grid 2x1"; check "two windows sit side by side" $?
# fewer windows keep the order of the colours (black, green, red, blue) without holes, as the games on web/lobby.html do: nobody is black, so the top left cell
# goes to green; two windows are green left (cell 0), red right (cell 1)
check "two windows: green left (cell 0), red right (cell 1)" "$([ "$(cells_of "$OUT")" = "0 1" ]; echo $?)"
OUT="$("$SCRIPT" --players 3 --dry-run)"
check "three windows" "$([ "$(echo "$OUT" | wc -l | tr -d ' ')" -eq 3 ]; echo $?)"
has "$OUT" "--seat 2"; check "the third window asks for seat 2 (blue)" $?
has "$OUT" "--seat 3"; [ $? -ne 0 ]; check "there is no fourth window" $?
has "$OUT" "--grid 2x2"; check "three windows lie in the 2 x 2 grid" $?
check "three windows: green, red, blue in the cells 0, 1, 2 (black is missing, the colours keep their order without a hole)" "$([ "$(cells_of "$OUT")" = "0 1 2" ]; echo $?)"
OUT="$(ANTS_PORT=5123 "$SCRIPT" --dry-run --players 4 --fog-test)"
has "$OUT" "--host 5123 --loopback"; check "ANTS_PORT moves the room" $?
has "$OUT" "--join 127.0.0.1:5123"; check "... and the guests follow" $?
check "an extra argument reaches every window" "$([ "$(echo "$OUT" | grep -c -- '--fog-test')" -eq 4 ]; echo $?)"
OUT="$("$SCRIPT" --single --dry-run --map X)"
check "--single: one window" "$([ "$(echo "$OUT" | wc -l | tr -d ' ')" -eq 1 ]; echo $?)"
has "$OUT" "--host"; [ $? -ne 0 ]; check "--single: no room" $?
has "$OUT" "--map X"; check "--single: the arguments still reach the game" $?
OUT="$("$SCRIPT" --players 1 --dry-run)"
check "--players 1 is --single" "$([ "$(echo "$OUT" | wc -l | tr -d ' ')" -eq 1 ]; echo $?)"
# a plain game has no mode flag at all: that is where the desktop start menu appears (add --map-select to start on the setup screen as before)
check "--single is the bare program, with no mode flag: the start menu appears" "$([ "$OUT" = "./build/src/ants_app/ants" ]; echo $?)"
OUT="$("$SCRIPT" --single --dry-run --map-select)"
check "--single --map-select: the setup screen as before (the option skips the menu)" "$([ "$OUT" = "./build/src/ants_app/ants --map-select" ]; echo $?)"
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

# the banner of the setup screen says what that screen takes (MapSelectScreen::handle_key_down: Up, Down, Enter, S, Q, X; the fog is a pair of buttons) and nothing that it does not
for f in start_game.sh start_game.bat; do
    banner="$(grep -F -A2 'SETUP SCREEN:' "$ROOT/$f")"
    check "$f: the setup screen's banner names Up / Down, Enter or S, Q or X" "$({ has "$banner" 'Up / Down' && has "$banner" 'Enter or S' && has "$banner" 'Q or X'; } ; echo $?)"
    check "$f: the setup screen's banner has no digit keys" "$({ has "$banner" '[1-6]'; } ; [ $? -ne 0 ]; echo $?)"
    check "$f: the setup screen's banner has no F key for the fog (it is two buttons)" "$({ has "$banner" '[F] toggles'; } ; [ $? -ne 0 ]; echo $?)"
    check "$f: the setup screen's banner does not say that Esc leaves (Esc does nothing there)" "$({ has "$banner" 'Esc leaves'; } ; [ $? -ne 0 ]; echo $?)"
done

# The build step. The script used to build only when the game's binary was missing, so a binary that an older checkout had left in ./build was launched for ever and a game that had changed
# (it opened in 4:3 after the 16:9 change) never showed it. Now every start makes sure that the game is built, and a failed build stops the launch. The script runs here from a COPY in a
# temporary folder with a fake game (a script that notes that it was started) and a fake cmake (ANTS_CMAKE: a script that notes its arguments and fails when it is told to), so nothing is built
# or started for real.
echo "[start script] the build step: on every start, never a game from an older build after a failure, nothing with --dry-run"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/ants_start_test.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/build/src/ants_app"
cp "$SCRIPT" "$WORK/start_game.sh"
printf '#!/bin/sh\necho "$@" >> "%s/game.ran"\n' "$WORK" > "$WORK/build/src/ants_app/ants"
cat > "$WORK/fake_cmake" <<'FAKE'
#!/bin/sh
echo "$*" >> "$FAKE_CMAKE_LOG"
case "$*" in
    *--build*)
        if [ -n "$FAKE_BUILD_FAILS" ]; then echo "fake cmake: the build failed" >&2; exit 1; fi
        ;;
    *)
        if [ -n "$FAKE_CONFIGURE_FAILS" ]; then echo "fake cmake: the configure failed" >&2; exit 1; fi
        mkdir -p build
        : > build/CMakeCache.txt
        ;;
esac
exit 0
FAKE
chmod +x "$WORK/fake_cmake" "$WORK/build/src/ants_app/ants" "$WORK/start_game.sh"
OLD_GAME_RAN() { [ -f "$WORK/game.ran" ] && wc -l < "$WORK/game.ran" | tr -d ' ' || echo 0; }
run_copy() {   # run_copy ARGS...: runs the copy of the script with the fake cmake; prints nothing, sets STATUS and OUTPUT
    : > "$WORK/cmake.log"
    OUTPUT="$(cd "$WORK" && FAKE_CMAKE_LOG="$WORK/cmake.log" ANTS_CMAKE="$WORK/fake_cmake" ANTS_PORT=45899 ./start_game.sh "$@" 2>&1)"
    STATUS=$?
}
# the game is there already (an old build) and ./build has no cache: the script configures and builds anyway
run_copy --single
check "a start with an old game in ./build still builds: the configure and then the build of the target" "$([ "$(wc -l < "$WORK/cmake.log" | tr -d ' ')" -eq 2 ] && head -1 "$WORK/cmake.log" | grep -q -- '-B build -DCMAKE_BUILD_TYPE=Release' && tail -1 "$WORK/cmake.log" | grep -q -- '--build build --target ants'; echo $?)"
check "... and then starts the game (it ran once)" "$([ "$STATUS" -eq 0 ] && [ "$(OLD_GAME_RAN)" = "1" ]; echo $?)"
# the next start: configured, so only the incremental build, and the game again
run_copy --single
check "the next start does not configure again (a cache is there) and builds the target again, game or no game" "$([ "$(wc -l < "$WORK/cmake.log" | tr -d ' ')" -eq 1 ] && grep -q -- '--build build --target ants' "$WORK/cmake.log"; echo $?)"
check "... and starts the game again" "$([ "$STATUS" -eq 0 ] && [ "$(OLD_GAME_RAN)" = "2" ]; echo $?)"
# --dry-run builds nothing and starts nothing
run_copy --single --dry-run
check "--dry-run runs no cmake at all" "$([ ! -s "$WORK/cmake.log" ]; echo $?)"
check "--dry-run starts no game and prints the command line" "$([ "$STATUS" -eq 0 ] && [ "$(OLD_GAME_RAN)" = "2" ] && [ "$OUTPUT" = "./build/src/ants_app/ants" ]; echo $?)"
OUT_DRY_RIG="$(cd "$WORK" && FAKE_CMAKE_LOG="$WORK/cmake.log" ANTS_CMAKE="$WORK/fake_cmake" ./start_game.sh --dry-run --players 2 2>&1)"
check "--dry-run of the rig runs no cmake either" "$([ ! -s "$WORK/cmake.log" ] && [ "$(echo "$OUT_DRY_RIG" | wc -l | tr -d ' ')" -eq 2 ]; echo $?)"
# a failed build stops the launch: the game that is in ./build (from an older build) is NOT started
before="$(OLD_GAME_RAN)"
FAKE_BUILD_FAILS=1 run_copy --single
check "a failed build stops the script with a failure status" "$([ "$STATUS" -eq 1 ]; echo $?)"
check "... without starting the game that an older build left in ./build" "$([ "$(OLD_GAME_RAN)" = "$before" ]; echo $?)"
has "$OUTPUT" "build failed"; check "... and says that the build failed (and that nothing was started)" $?
has "$OUTPUT" "Nothing was started"; check "... in words that say that nothing was started" $?
FAKE_BUILD_FAILS=1 run_copy --players 4
check "the four-window rig stops too, with no window started" "$([ "$STATUS" -eq 1 ] && [ "$(OLD_GAME_RAN)" = "$before" ]; echo $?)"
# a failed configure stops the launch before the build
rm -f "$WORK/build/CMakeCache.txt"
FAKE_CONFIGURE_FAILS=1 run_copy --single
check "a failed configure stops the script (status 1) before any build and starts nothing" "$([ "$STATUS" -eq 1 ] && [ "$(wc -l < "$WORK/cmake.log" | tr -d ' ')" -eq 1 ] && ! grep -q -- '--build' "$WORK/cmake.log" && [ "$(OLD_GAME_RAN)" = "$before" ]; echo $?)"
has "$OUTPUT" "could not be configured"; check "... and says that the build could not be configured" $?
# a build that succeeds but makes no game is not launched either (a configuration without the game)
rm -f "$WORK/build/src/ants_app/ants" "$WORK/build/CMakeCache.txt"
run_copy --single
check "a build that made no game stops the script (status 1) with a message" "$([ "$STATUS" -eq 1 ] && has "$OUTPUT" "made no game"; echo $?)"
# the four windows of the rig are built for first as well (the fake game ends at once, so the script ends with the host window gone: only the build line matters here)
printf '#!/bin/sh\necho "$@" >> "%s/game.ran"\n' "$WORK" > "$WORK/build/src/ants_app/ants"
chmod +x "$WORK/build/src/ants_app/ants"
run_copy --players 2
check "the rig builds before it starts a window" "$(grep -q -- '--build build --target ants' "$WORK/cmake.log"; echo $?)"
rm -rf "$WORK"
trap - EXIT

echo "start script: $CHECKS checks, $FAILS failures"
[ "$FAILS" -eq 0 ]
