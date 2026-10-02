#!/usr/bin/env bash
# Starts Ants (macOS / Linux).
#
#   ./start_game.sh                 four games on this machine, one player each, in a 2 x 2 grid, playing one networked match together:
#                                   window 0 hosts (green), windows 1 - 3 join (red, blue, black), every player has a random name.
#                                   The windows lie by colour, the way the four hills lie on the Small and Treasure maps (the owner's layout, the
#                                   same as the games on web/four.html): black top left, green top right, red bottom left, blue bottom right
#   ./start_game.sh --players N     N windows (1 - 4); 2 windows sit side by side (green left, red right), 3 are green, red, blue in the first three
#                                   cells of the grid (black, green, red, blue keep their order without holes), 1 is the plain single game
#   ./start_game.sh --single        the same as --players 1 (one plain game: the desktop start menu comes first, then the original's screens as always; add --map-select
#                                   to start on the setup screen at once). The four windows of the default rig never show the menu: each has --host or --join
#   ./start_game.sh --dry-run ...   print the command line of every window and stop (nothing is built or started)
#   every other argument goes to every window (the game's own options, see README.md)
#   --host, --join, --bot, --lan-list, --headless, --screenshot and --map are options of one game: given without --players they make this a single game,
#   so that `./start_game.sh --host --name Alice` and `./start_game.sh --join 192.168.1.20` still do what the README says. --bot cannot be combined with --players
#   (the other windows are guests, and a guest runs no bots: a game against bots is one window; use --host --bot and let others join it)
#
# Environment: ANTS_PORT (the room's TCP port, default 4001), ANTS_NAMES_SEED (makes the random names repeatable).
#
# The windows do not grab the pointer: the game's own cursor shows only inside a window, the edge of the screen scrolls only the window the pointer is in,
# and only the window that has the focus makes sound (--audio-focus), so four games on one machine can be played one after the other.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PLAYERS=4
GIVEN=0
DRY_RUN=0
PASS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --players) PLAYERS="${2:-}"; GIVEN=1; shift; [ $# -gt 0 ] && shift ;;
        --single) PLAYERS=1; GIVEN=1; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        *) PASS+=("$1"); shift ;;
    esac
done
if [ "$GIVEN" -eq 0 ] && [ "${#PASS[@]}" -gt 0 ]; then
    for arg in "${PASS[@]}"; do
        case "$arg" in
            --host|--join|--bot|--lan-list|--headless|--screenshot|--map) PLAYERS=1 ;;
        esac
    done
fi
case "$PLAYERS" in
    1|2|3|4) ;;
    *) echo "[LAUNCHER] --players takes a number from 1 to 4." >&2; exit 2 ;;
esac
if [ "$PLAYERS" -gt 1 ] && [ "${#PASS[@]}" -gt 0 ]; then
    for arg in "${PASS[@]}"; do
        if [ "$arg" = "--bot" ]; then
            echo "[LAUNCHER] --bot cannot be combined with --players: the extra windows are guests and a guest runs no bots. Use ./start_game.sh --bot 1:medium for a game against a bot, or --host --bot 2:medium to let others join." >&2
            exit 2
        fi
    done
fi

PORT="${ANTS_PORT:-4001}"
BIN="./build/src/ants_app/ants"
COLOURS=(Green Red Blue Black)
NAMES=(Antonio Buzz Clover Dot Ember Flick Granite Hazel Inky Juniper Kip Lumen Mortimer Nettle Oakley Pebble Quill Rusty Sprig Tango Umber Velvet Wren Zest)

# four different names, drawn at random (a shuffle of the list)
if [ -n "${ANTS_NAMES_SEED:-}" ]; then RANDOM="$ANTS_NAMES_SEED"; fi
pool=("${NAMES[@]}")
i=${#pool[@]}
while [ "$i" -gt 1 ]; do
    i=$((i - 1))
    j=$((RANDOM % (i + 1)))
    tmp="${pool[$i]}"
    pool[$i]="${pool[$j]}"
    pool[$j]="$tmp"
done

if [ "$DRY_RUN" -eq 0 ]; then
    echo "======================================================================"
    echo "                      ANTS ENGINE REMAKE (macOS)                      "
    echo "======================================================================"
    if [ ! -f "$BIN" ]; then
        echo "[LAUNCHER] Game binary not found. Building release binary now..."
        cmake -B build -DCMAKE_BUILD_TYPE=Release
        cmake --build build --target ants -j"$( (sysctl -n hw.ncpu || nproc) 2>/dev/null || echo 4)"
    fi

    echo ""
    echo "----------------------------------------------------------------------"
    echo "                      AUTHENTIC CONTROL SCHEME                        "
    echo "----------------------------------------------------------------------"
    echo " START MENU (A PLAIN GAME, ./start_game.sh --single; the four-window rig starts in its room):"
    echo "   * Single player (with computer players per seat), Join with a code, Host an online match, Quit."
    echo "   * Up / Down and Enter, or the mouse; Esc goes back (on the first panel: quits)."
    echo ""
    echo " SETUP SCREEN:"
    echo "   * [1-6] or the arrow keys pick a map; click START (or press Enter) to begin; Esc leaves."
    echo "   * [F] toggles Fog of War. INTRO music plays here; a match plays a random in-game track (Ctrl+M mutes)."
    echo ""
    echo " MOUSE (the original's pointer model):"
    echo "   * Left click an ant: select it. Shift + click adds an ant of yours to the selection or takes it out again."
    echo "   * Drag over 4 px: a red rubber band selects your ants (with Shift it adds them)."
    echo "   * With ants selected, a left click acts by the cursor: ground = move, food = harvest, another player's ant = attack."
    echo "     One selected ant also has its ability: bomb (plant / defuse), fire wall, bridge (dig), raid."
    echo "   * Right click: the same order without the cursor check (a move for several ants, workers and combat ants)."
    echo "   * Several ants - also one ant that was added with Shift - are a group: a click is always a move. A group of"
    echo "     bombers therefore walks onto a bomb of its own team and sets it off (how to get off the island)."
    echo "   * The Move and ability pedestals latch (Stop stops and deselects). Hold left on the minimap to scroll."
    echo ""
    echo " CAMERA:"
    echo "   * Move the pointer to the screen edge to scroll (nothing scrolls with the keyboard or the wheel)."
    echo ""
    echo " KEYS (the original's keyboard; the chat box is always active):"
    echo "   * F1 help, F9 - F12 quick chat, Enter sends chat, Esc deselects."
    echo "   * Ctrl+A select all, Ctrl+H home hill, Ctrl+N / Ctrl+P next / previous ant, Ctrl+S stop,"
    echo "     Ctrl+O options, Ctrl+Q quit, Ctrl+L hit point digits."
    echo "   * Developer shortcuts: Ctrl+T tile grid, Ctrl+M mute music, Ctrl+1..4 switch team, Shift+F12 screenshot."
    echo "----------------------------------------------------------------------"
    echo ""
fi

if [ "$PLAYERS" -eq 1 ]; then
    if [ "$DRY_RUN" -ne 0 ]; then
        printf '%q' "$BIN"
        [ "${#PASS[@]}" -gt 0 ] && printf ' %q' "${PASS[@]}"
        printf '\n'
        exit 0
    fi
    echo "[LAUNCHER] Starting Ants..."
    echo ""
    exec "$BIN" "${PASS[@]}"
fi

# a 2 x 2 grid for three and four windows, side by side for two
GRID="2x2"
[ "$PLAYERS" -eq 2 ] && GRID="2x1"

# The cell of window n (= seat n) in the grid; --cell counts row by row, 0 = top left (1 top right, 2 bottom left, 3 bottom right of the 2 x 2 grid).
# Four windows lie by colour, the way the four hills lie on the Small and Treasure maps (the owner's layout, the same as the games of web/four.html):
#   black (seat 3) top left, green (seat 0) top right, red (seat 1) bottom left, blue (seat 2) bottom right.
# (Other maps put the hills elsewhere; the layout is the same for every map.)
# Fewer windows keep that order of the colours (black, green, red, blue) without holes, as on the page: two are green left, red right (2 x 1),
# three are green, red, blue in cells 0, 1, 2 (nobody is black, so the top left cell goes to green).
FOUR_CELLS=(1 2 3 0)        # by seat: green 1, red 2, blue 3, black 0

# the command line of window $1: window 0 opens the room on this machine only, the others join it and ask for their own colour
window_args() {
    local n="$1"
    local name="${pool[$n]}"
    local cell="$n"
    if [ "$PLAYERS" -eq 4 ]; then cell="${FOUR_CELLS[$n]}"; fi
    WINDOW_ARGS=(--name "$name" --title "Ants - ${COLOURS[$n]} ($name)" --grid "$GRID" --cell "$cell" --audio-focus --no-lan)
    if [ "$n" -eq 0 ]; then
        WINDOW_ARGS+=(--host "$PORT" --loopback)
    else
        WINDOW_ARGS+=(--join "127.0.0.1:$PORT" --seat "$n")
    fi
    [ "${#PASS[@]}" -gt 0 ] && WINDOW_ARGS+=("${PASS[@]}")
    return 0
}

if [ "$DRY_RUN" -ne 0 ]; then
    for ((n = 0; n < PLAYERS; n++)); do
        window_args "$n"
        printf '%q' "$BIN"
        printf ' %q' "${WINDOW_ARGS[@]}"
        printf '\n'
    done
    exit 0
fi

echo "[LAUNCHER] $PLAYERS players in one match on this machine (room on port $PORT):"
for ((n = 0; n < PLAYERS; n++)); do
    echo "   window $n: ${COLOURS[$n]}, ${pool[$n]}$([ "$n" -eq 0 ] && echo ' (host: choose the map and press START here)')"
done
echo ""

PIDS=()
cleanup() {
    trap - INT TERM EXIT
    for pid in "${PIDS[@]}"; do kill "$pid" 2>/dev/null || true; done
}
trap cleanup INT TERM EXIT

window_args 0
"$BIN" "${WINDOW_ARGS[@]}" &
PIDS+=($!)

# the guests connect when the room is open (it takes the host a moment to make its window and listen)
for _ in $(seq 1 50); do
    if (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then break; fi
    kill -0 "${PIDS[0]}" 2>/dev/null || { echo "[LAUNCHER] The host window closed before the room was open." >&2; exit 1; }
    sleep 0.2
done
sleep 0.3

for ((n = 1; n < PLAYERS; n++)); do
    window_args "$n"
    "$BIN" "${WINDOW_ARGS[@]}" &
    PIDS+=($!)
done

wait
