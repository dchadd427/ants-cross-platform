#!/usr/bin/env bash
set -eo pipefail

# ANSI Color Codes (auto-disabled if output is not a terminal or NO_COLOR is set)
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    BOLD="\033[1m"
    RED="\033[1;31m"
    GREEN="\033[1;32m"
    YELLOW="\033[1;33m"
    BLUE="\033[1;34m"
    MAGENTA="\033[1;35m"
    CYAN="\033[1;36m"
    RESET="\033[0m"
else
    BOLD=""
    RED=""
    GREEN=""
    YELLOW=""
    BLUE=""
    MAGENTA=""
    CYAN=""
    RESET=""
fi

# Locate Project Root (traverse upwards if invoked from subdirectories)
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
while [ "$PROJECT_ROOT" != "/" ] && [ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]; do
    PROJECT_ROOT="$(dirname "$PROJECT_ROOT")"
done
if [ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]; then
    echo -e "${RED}Error: Could not locate project root containing CMakeLists.txt${RESET}" >&2
    exit 1
fi
cd "$PROJECT_ROOT"

print_usage() {
    echo "Usage: ./run_tests.sh [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --fast           The quick tier, for every change: the asset, simulation, network-core and application MODEL suites that need no window and finish in seconds,"
    echo "                   plus the repository checks (version / changelog consistency, tool and script tests). No E2E, no script suites that start the game, no"
    echo "                   sanitizer, none of the slow suites (lock-step soak, server, worker bot, network application): CI runs everything for every push"
    echo "  --all            Run all test suites (libants-assets + libants-sim + libants-app + E2E + repository checks, default)"
    echo "  --assets         Run only asset decoder tests (test_assets)"
    echo "  --sim            Run only simulation rules tests (test_sim_rules, and the network, bot (test_ai, bot_arena --selftest, test_ai_worker) and server suites)"
    echo "  --app            Run only application integration tests (test_app_integration)"
    echo "  --e2e            Run only opaque-box E2E test suites (e2e_runner)"
    echo "  --tools          Run only the repository checks (tools/check_version_consistency.py, the python tests of tests/scripts)"
    echo "  --asan           Build and run with AddressSanitizer (build_asan)"
    echo "  --clean          Remove build directories and rebuild before testing"
    echo "  --list           Print the suites that the other options select (id, tier, quick or full) and exit; nothing is built or run"
    echo "  -v, --verbose    Enable verbose assertions output in test suites"
    echo "  -h, --help       Display this help message and exit"
    echo ""
    echo "--fast filters whatever the tier options select to the quick suites (./run_tests.sh --sim --fast: only the quick simulation suites)."
    echo "The summary prints the time of every suite and the slowest ones. Tiers and when each runs: docs/WORKFLOW.md."
    echo ""
}

# Parse Command Line Options
RUN_ASSETS=1
RUN_SIM=1
RUN_APP=1
RUN_E2E=1
RUN_TOOLS=1
RUN_ASAN=0
FAST=0
LIST_ONLY=0
CLEAN_BUILD=0
VERBOSE=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --all)
            RUN_ASSETS=1
            RUN_SIM=1
            RUN_APP=1
            RUN_E2E=1
            RUN_TOOLS=1
            ;;
        --assets)
            RUN_ASSETS=1
            RUN_SIM=0
            RUN_APP=0
            RUN_E2E=0
            RUN_TOOLS=0
            ;;
        --sim)
            RUN_ASSETS=0
            RUN_SIM=1
            RUN_APP=0
            RUN_E2E=0
            RUN_TOOLS=0
            ;;
        --app)
            RUN_ASSETS=0
            RUN_SIM=0
            RUN_APP=1
            RUN_E2E=0
            RUN_TOOLS=0
            ;;
        --e2e)
            RUN_ASSETS=0
            RUN_SIM=0
            RUN_APP=0
            RUN_E2E=1
            RUN_TOOLS=0
            ;;
        --tools)
            RUN_ASSETS=0
            RUN_SIM=0
            RUN_APP=0
            RUN_E2E=0
            RUN_TOOLS=1
            ;;
        --fast)
            FAST=1
            ;;
        --list)
            LIST_ONLY=1
            ;;
        --asan)
            RUN_ASAN=1
            ;;
        --clean|--rebuild)
            CLEAN_BUILD=1
            ;;
        -v|--verbose)
            VERBOSE=1
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${RESET}"
            print_usage
            exit 1
            ;;
    esac
    shift
done

NCPU=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
BUILD_DIR="build"
if [ "$RUN_ASAN" -eq 1 ]; then
    BUILD_DIR="build_asan"
fi
E2E_ARGS="--all"
if [ "$VERBOSE" -eq 1 ]; then
    E2E_ARGS="--all -v"
fi

# ----------------------------------------------------------------------------------------------------------------------------------------------------------------
# The suites, in the order they run. One `suite` line per suite:
#   suite ID TIER QUICK TARGETS LABEL TITLE COMMAND
#     ID       the number the summary prints (1 asset decoders, 2.x simulation / network / bots / server, 3.x application, 4 E2E, 5.x repository checks)
#     TIER     assets | sim | app | e2e | tools: which option selects it (--assets, --sim, --app, --e2e, --tools)
#     QUICK    1: part of --fast (finishes in seconds and covers what a typical change can break), 0: only the full run
#     TARGETS  the CMake targets the suite needs (--fast builds exactly these; "-" for a suite that builds nothing)
#     LABEL    the summary line; TITLE the heading printed before the suite runs
#     COMMAND  run with eval ($BUILD_DIR is the build folder); its exit status is the suite's result
# Measured on a Mac (Release, 10 cores): the quick suites together take about 60 s of test time (the whole of ./run_tests.sh --fast about 65 s with the build check and
# the python tests). What --fast leaves out, and what each costs: 3.9 the server end-to-end script 143 s, 2.11 test_lockstep 58 s, 3.6 test_network_app 45 s,
# 2.19 test_server 29 s, 2.22 test_ai_worker 18 s, 2.18 map_sweep 9 s, 3.12 test_start_menu_app 8 s, 2.13.1 test_ctl 7 s, the E2E runner and 3.8 the start script. The summary of every
# run prints the real time of every suite.
# ----------------------------------------------------------------------------------------------------------------------------------------------------------------
SUITE_IDS=()
SUITE_TIERS=()
SUITE_QUICK=()
SUITE_TARGETS=()
SUITE_LABELS=()
SUITE_TITLES=()
SUITE_CMDS=()

suite() {
    SUITE_IDS+=("$1")
    SUITE_TIERS+=("$2")
    SUITE_QUICK+=("$3")
    SUITE_TARGETS+=("$4")
    SUITE_LABELS+=("$5")
    SUITE_TITLES+=("$6")
    SUITE_CMDS+=("$7")
}

# The worker bot's pinned table is left out of the sanitizer pass. The test filters of a developer (W_ONLY, W_SKIP, ANTS_TEST_FILTER) must not leak into the master run:
# a forgotten W_ONLY would run one test and print PASSED. Under ASan + UBSan (unoptimised) the 18-row pinned table (AI3.9, AI3.12) is about four fifths of the run time and
# checks numbers, not memory: the sanitizer pass leaves those two out (docs/audit/B3_notes.md).
run_worker_bot_suite() {
    if [ "$RUN_ASAN" -eq 1 ]; then
        env -u W_ONLY -u ANTS_TEST_FILTER W_SKIP=AI3.9,AI3.12 "./$BUILD_DIR/tests/test_ai/test_ai_worker"
    else
        env -u W_ONLY -u W_SKIP -u ANTS_TEST_FILTER "./$BUILD_DIR/tests/test_ai/test_ai_worker"
    fi
}

define_suites() {
    # 1. asset decoders
    suite "1"      assets 1 "test_assets"                "Native Asset Decoder Tests (test_assets)"                  "ASSET DECODER SUITES (libants-assets)"                                             '"./$BUILD_DIR/tests/test_assets/test_assets"'
    suite "1.1"    assets 1 "test_movement_tables"       "Movement Table Parity (test_movement_tables)"              "MOVEMENT TABLE PARITY (generated tables vs Ants.exe or its pinned digests / ants.chd)" '"./$BUILD_DIR/tests/test_assets/test_movement_tables"'
    suite "1.2"    assets 1 "test_challenger_m1_1"       "Challenger M1_1 (test_challenger_m1_1)"                    "CHALLENGER M1_1 (adversarial asset decoding)"                                      '"./$BUILD_DIR/tests/test_assets/test_challenger_m1_1"'
    suite "1.3"    assets 1 "test_challenger_m1_2"       "Challenger M1_2 (test_challenger_m1_2)"                    "CHALLENGER M1_2 (asset decoding, second pass)"                                     '"./$BUILD_DIR/tests/test_assets/test_challenger_m1_2"'
    suite "1.4"    assets 1 "test_challenger_m1_it2"     "Challenger M1_IT2 (test_challenger_m1_it2)"                "CHALLENGER M1_IT2 (asset interface contract)"                                      '"./$BUILD_DIR/tests/test_assets/test_challenger_m1_it2"'
    suite "1.5"    assets 1 "test_challenger_m1_it2_2"   "Challenger M1_IT2_2 (test_challenger_m1_it2_2)"            "CHALLENGER M1_IT2_2 (asset interface contract, deep)"                              '"./$BUILD_DIR/tests/test_assets/test_challenger_m1_it2_2"'

    # 2. simulation rules, network core, bots, server
    suite "2"      sim    1 "test_sim_rules"             "Simulation Rules Tests (test_sim_rules)"                   "SIMULATION RULES SUITES (libants-sim)"                                             '"./$BUILD_DIR/tests/test_sim/test_sim_rules"'
    suite "2.1"    sim    1 "test_challenger_m2_1"       "Challenger M2_1 (test_challenger_m2_1)"                    "CHALLENGER M2_1 (Combat, Hazards, Physics)"                                        '"./$BUILD_DIR/tests/test_sim/test_challenger_m2_1"'
    suite "2.2"    sim    1 "test_challenger_m2_2"       "Challenger M2_2 (test_challenger_m2_2)"                    "CHALLENGER M2_2 (Lifecycle, Economy, Alliances)"                                   '"./$BUILD_DIR/tests/test_sim/test_challenger_m2_2"'
    suite "2.3"    sim    1 "test_path_planner"          "Path Planner (test_path_planner)"                          "ORIGINAL PATH PLANNER (PATHMGR A*) SUITE"                                          '"./$BUILD_DIR/tests/test_sim/test_path_planner"'
    suite "2.4"    sim    1 "test_movement_golden"       "Movement Golden (test_movement_golden)"                    "ORIGINAL MOVEMENT GOLDEN SUITE (frame-exact locomotion)"                           '"./$BUILD_DIR/tests/test_sim/test_movement_golden"'
    suite "2.5"    sim    1 "test_hill_actions"          "Hill Actions (test_hill_actions)"                          "ORIGINAL HILL ACTIONS SUITE (enter, ring, hatch, raid)"                            '"./$BUILD_DIR/tests/test_sim/test_hill_actions"'
    suite "2.6"    sim    1 "test_combat_actions"        "Combat Actions (test_combat_actions)"                      "ORIGINAL COMBAT ACTIONS SUITE (contact, flights, blasts, death)"                   '"./$BUILD_DIR/tests/test_sim/test_combat_actions"'
    suite "2.7"    sim    1 "test_ability_actions"       "Ability Actions (test_ability_actions)"                    "ORIGINAL ABILITY ACTIONS SUITE (bombs, fire walls, bridges)"                       '"./$BUILD_DIR/tests/test_sim/test_ability_actions"'
    suite "2.8"    sim    1 "test_powerup_actions"       "Power-Up Actions (test_powerup_actions)"                   "ORIGINAL POWER-UP ACTIONS SUITE (pick-up, cancel window, immunity)"                '"./$BUILD_DIR/tests/test_sim/test_powerup_actions"'
    suite "2.9"    sim    1 "test_food_actions"          "Food Actions (test_food_actions)"                          "ORIGINAL FOOD ACTIONS SUITE (food objects, grab clip, bite, stages)"               '"./$BUILD_DIR/tests/test_sim/test_food_actions"'
    suite "2.9.1"  sim    1 "test_level_defaults"        "Level Defaults (test_level_defaults)"                      "LEVEL DEFAULTS SUITE (default ant type of a level, power-ups and droppers by tile id)" '"./$BUILD_DIR/tests/test_sim/test_level_defaults"'
    suite "2.10"   sim    1 "test_commands"              "Commands + State Hash (test_commands)"                     "COMMAND LAYER SUITE (commands, validation, lock-step state hash)"                  '"./$BUILD_DIR/tests/test_sim/test_commands"'
    suite "2.11"   sim    0 "test_lockstep"              "Lock-Step Network Core (test_lockstep)"                    "LOCK-STEP NETWORK CORE SUITE (protocol, sequencer, sessions, matches)"             'env -u ANTS_TEST_FILTER "./$BUILD_DIR/tests/test_net/test_lockstep"'
    suite "2.12"   sim    1 "test_lobby"                 "Room / Start Barrier (test_lobby)"                         "ROOM SUITE (joining, roster, start barrier)"                                       'env -u ANTS_TEST_FILTER "./$BUILD_DIR/tests/test_net/test_lobby"'
    suite "2.13"   sim    1 "test_tcp"                   "TCP Transport (test_tcp)"                                  "TCP TRANSPORT SUITE (framing, hostile frames, real-socket match)"                  '"./$BUILD_DIR/tests/test_net/test_tcp"'
    suite "2.13.1" sim    0 "test_ctl"                   "Control Interface (test_ctl)"                              "CONTROL INTERFACE SUITE (strict JSON, authenticated HTTP over real sockets)"       '"./$BUILD_DIR/tests/test_ctl/test_ctl"'
    suite "2.14"   sim    1 "test_netgame"               "NetGame (test_netgame)"                                    "NETGAME SUITE (room, start barrier, matches over real sockets)"                    'env -u ANTS_TEST_FILTER "./$BUILD_DIR/tests/test_net/test_netgame"'
    suite "2.15"   sim    1 "test_movement_differential" "Movement Differential (test_movement_differential)"        "MOVEMENT DIFFERENTIAL SUITE (independent walk and A* models)"                      '"./$BUILD_DIR/tests/test_sim/test_movement_differential"'
    suite "2.16"   sim    1 "test_lan"                   "LAN Discovery (test_lan)"                                  "LAN DISCOVERY SUITE (room announcements, browser, datagram codec)"                 '"./$BUILD_DIR/tests/test_net/test_lan"'
    suite "2.17"   sim    1 "test_ws"                    "WebSocket Transport (test_ws)"                             "WEBSOCKET TRANSPORT SUITE (RFC 6455 codec, handshake, real-socket echo)"           '"./$BUILD_DIR/tests/test_net/test_ws"'
    suite "2.18"   sim    0 "map_sweep"                  "Map Sweep (map_sweep --selftest)"                          "MAP SWEEP SELF-TEST (six shipped maps: loads, determinism, faults)"                '"./$BUILD_DIR/map_sweep" --selftest'
    suite "2.19"   sim    0 "test_server"                "Dedicated Server (test_server)"                            "DEDICATED SERVER SUITE (map store, rooms, the door, control calls, real sockets)"  'env -u ANTS_TEST_FILTER "./$BUILD_DIR/tests/test_server/test_server"'
    suite "2.20"   sim    1 "test_ai"                    "Computer Players (test_ai)"                                "COMPUTER PLAYERS SUITE (ants_ai controller, idle bot, bot seats in rooms)"         'env -u ANTS_TEST_FILTER "./$BUILD_DIR/tests/test_ai/test_ai"'
    suite "2.21"   sim    1 "bot_arena"                  "Bot Arena (bot_arena --selftest)"                          "BOT ARENA SELF-TEST (headless matches: determinism, replay without a bot, report, threads)" '"./$BUILD_DIR/bot_arena" --selftest'
    suite "2.22"   sim    0 "test_ai_worker"             "Worker Bot (test_ai_worker)"                               "WORKER BOT SUITE (the economy on every shipped map, learning, endgame, budget, pinned baselines)" 'run_worker_bot_suite'
    suite "2.23"   sim    1 "test_latency"               "Ping, Delay, Waiting (test_latency)"                       "PING, DELAY AND WAITING SUITE (the meters, a simulated link, the buffer after a stall)" '"./$BUILD_DIR/tests/test_net/test_latency"'
    suite "2.24"   sim    1 "test_jitter"                "Jitter Buffer (test_jitter)"                               "JITTER BUFFER SUITE (the rule, the runner's speed, stalls, hitches, hidden windows)" '"./$BUILD_DIR/tests/test_net/test_jitter"'

    # 3. application
    suite "3"      app    1 "test_app_integration"       "Application Integration Tests (test_app)"                  "APPLICATION INTEGRATION SUITES (libants-app)"                                      '"./$BUILD_DIR/tests/test_app/test_app_integration"'
    suite "3.1"    app    1 "test_render_parity"         "Render Parity (test_render_parity)"                        "RENDER PARITY SUITE (renderer vs original draw rules)"                             '"./$BUILD_DIR/tests/test_app/test_render_parity"'
    suite "3.2"    app    1 "test_hud_layout"            "HUD Layout (test_hud_layout)"                              "HUD LAYOUT SUITE (draw calls vs original coordinates)"                             '"./$BUILD_DIR/tests/test_app/test_hud_layout"'
    suite "3.3"    app    1 "test_status_messages"       "Status Messages (test_status_messages)"                    "STATUS MESSAGES SUITE (status line, selection / order texts)"                      '"./$BUILD_DIR/tests/test_app/test_status_messages"'
    suite "3.4"    app    1 "test_input_model"           "Input Model (test_input_model)"                            "INPUT MODEL SUITE (edge scrolling, minimap drag)"                                  '"./$BUILD_DIR/tests/test_app/test_input_model"'
    suite "3.5"    app    1 "test_pointer_model"         "Pointer Model (test_pointer_model)"                        "POINTER MODEL SUITE (cursor table, clicks, band, pedestals)"                       '"./$BUILD_DIR/tests/test_app/test_pointer_model"'
    suite "3.6"    app    0 "test_network_app"           "Network Application (test_network_app)"                    "NETWORK APPLICATION SUITE (names, room, thumbs, start, matches)"                   'env -u ANTS_TEST_FILTER "./$BUILD_DIR/tests/test_app/test_network_app"'
    suite "3.7"    app    1 "test_options"               "Options (test_options)"                                    "OPTIONS SUITE (slider, switches, edit fields, settings)"                           '"./$BUILD_DIR/tests/test_app/test_options"'
    suite "3.8"    app    0 "ants"                       "Start Script (start_game.sh --dry-run)"                    "START SCRIPT SUITE (start_game.sh --dry-run: seats, colours, grid)"                '"./tests/scripts/test_start_game.sh"'
    suite "3.9"    app    0 "ants ants_server"           "Server End-To-End (ants_server + 2 clients)"               "SERVER END-TO-END (ants_server + two headless clients: secret, room by code, automatic start)" 'BUILD_DIR="$BUILD_DIR" "./tests/scripts/test_ants_server.sh"'
    suite "3.10"   app    1 "test_view_fingerprint"      "View Fingerprint (test_view_fingerprint)"                  "VIEW FINGERPRINT SUITE (the classic 640 x 480 picture and pointer pinned: draw calls, pixels, every pixel's cursor, scroll and click)" '"./$BUILD_DIR/tests/test_app/test_view_fingerprint"'
    suite "3.11"   app    1 "test_start_menu"            "Start Menu Model (test_start_menu)"                        "START MENU MODEL SUITE (keys, mouse, fields, seats, servers, codes, settings, layout, drawing, command-line skip rules)" 'env -u ANTS_TEST_FILTER "./$BUILD_DIR/tests/test_app/test_start_menu"'
    suite "3.12"   app    0 "test_start_menu_app"        "Start Menu Application (test_start_menu_app)"              "START MENU APPLICATION SUITE (single player, bots, join, host, every failure, cancel, back to the menu; a real room manager over TCP)" 'env -u ANTS_TEST_FILTER "./$BUILD_DIR/tests/test_app/test_start_menu_app"'
    suite "3.13"   app    1 "test_screen_layout"         "Screen Layout (test_screen_layout)"                        "SCREEN LAYOUT SUITE (the picture's geometry as numbers and every consumer of it: HUD, scroll, pointer, plate, renderer)" '"./$BUILD_DIR/tests/test_app/test_screen_layout"'
    suite "3.14"   app    1 "test_canvas_layout"         "Canvas Layout (test_canvas_layout)"                        "CANVAS LAYOUT SUITE (the 16:9 canvas in a window, --aspect, Alt+Enter, the screenshot)" '"./$BUILD_DIR/tests/test_app/test_canvas_layout"'
    suite "3.15"   app    1 "test_wide_hud"              "Wide HUD (test_wide_hud)"                                  "WIDE HUD SUITE (the 16:9 match screen: the frame's pieces and cuts, the HUD at 960 x 540, the view, the pages and dialogs, the picture per screen, the default)" '"./$BUILD_DIR/tests/test_app/test_wide_hud"'
    suite "3.16"   app    1 "test_wide_setup"            "Wide Setup (test_wide_setup)"                              "WIDE SETUP SUITE (the 16:9 setup screen with its map preview: layout, seams, the mock-ups pixel for pixel, the preview, the pointer, fingerprints)" '"./$BUILD_DIR/tests/test_app/test_wide_setup"'
    suite "3.17"   app    1 "test_map_preview"           "Map Preview (test_map_preview)"                            "MAP PREVIEW SUITE (the preview is the game's own render of the map: the area filter against an oracle, the fit, the render, the cache, the fallback, the six maps)" '"./$BUILD_DIR/tests/test_app/test_map_preview"'
    suite "3.18"   app    1 "test_zoom_model"            "Zoom Model (test_zoom_model)"                              "ZOOM MODEL SUITE (the wheel zoom's levels, anchoring, clamps, camera, edge scroll in screen pixels, start view, wheel accumulation, settings key)" '"./$BUILD_DIR/tests/test_app/test_zoom_model"'
    suite "3.19"   app    1 "test_zoom_view"             "Zoom View (test_zoom_view)"                                "ZOOM VIEW SUITE (the world pass against the direct pass, the HUD at a zoom, the wheel, fairness, settings, a network match with a zoom)" '"./$BUILD_DIR/tests/test_app/test_zoom_view"'
    suite "3.20"   app    1 "test_zoom_fingerprint"      "Zoom Fingerprint (test_zoom_fingerprint)"                  "ZOOM FINGERPRINT SUITE (the pictures and the pointer pinned at the zoom 0.5 and 2, classic and wide)" '"./$BUILD_DIR/tests/test_app/test_zoom_fingerprint"'

    # 4. opaque-box E2E (its own build folder)
    suite "4"      e2e    0 "-"                          "Opaque-Box E2E Tests (e2e_runner)"                         "E2E OPAQUE-BOX VERIFICATION SUITES"                                                './build_e2e/e2e_runner $E2E_ARGS'

    # 5. repository checks (python3; no build)
    suite "5.1"    tools  1 "-"                          "Version / Changelog / Status / README Consistency"         "VERSION CONSISTENCY (the file VERSION against CHANGELOG.md, STATUS.md and README.md)" 'python3 tools/check_version_consistency.py'
    suite "5.2"    tools  1 "-"                          "Tool and Script Tests (python: tests/scripts/test_*.py)"   "TOOL AND SCRIPT TESTS (changelog pages, version tools, the generated header, the tiers of this script)" 'python3 -m unittest discover -s tests/scripts -p "test_*.py"'
}

# The suites come from define_suites, unless a test of this script gives its own table (tests/scripts/test_run_tests.py: ANTS_RUN_TESTS_TABLE=<file> that calls `suite`
# the same way); such a table builds nothing.
NO_BUILD=0
if [ -n "${ANTS_RUN_TESTS_TABLE:-}" ]; then
    NO_BUILD=1
    # shellcheck disable=SC1090
    . "$ANTS_RUN_TESTS_TABLE"
else
    define_suites
fi

# Which suites do the options select?
SELECTED=()
for ((i = 0; i < ${#SUITE_IDS[@]}; i++)); do
    case "${SUITE_TIERS[$i]}" in
        assets) [ "$RUN_ASSETS" -eq 1 ] || continue ;;
        sim)    [ "$RUN_SIM" -eq 1 ] || continue ;;
        app)    [ "$RUN_APP" -eq 1 ] || continue ;;
        e2e)    [ "$RUN_E2E" -eq 1 ] || continue ;;
        tools)  [ "$RUN_TOOLS" -eq 1 ] || continue ;;
    esac
    if [ "$FAST" -eq 1 ] && [ "${SUITE_QUICK[$i]}" -ne 1 ]; then
        continue
    fi
    SELECTED+=("$i")
done

if [ "$LIST_ONLY" -eq 1 ]; then
    for i in ${SELECTED[@]+"${SELECTED[@]}"}; do
        quick="full"
        [ "${SUITE_QUICK[$i]}" -eq 1 ] && quick="quick"
        printf '%s\t%s\t%s\t%s\n' "${SUITE_IDS[$i]}" "${SUITE_TIERS[$i]}" "$quick" "${SUITE_LABELS[$i]}"
    done
    exit 0
fi

echo -e "${BOLD}${CYAN}======================================================================${RESET}"
echo -e "${BOLD}${CYAN}                 ANTS ENGINE REMAKE - MASTER TEST RUNNER              ${RESET}"
echo -e "${BOLD}${CYAN}======================================================================${RESET}"
if [ "$FAST" -eq 1 ]; then
    echo -e "${YELLOW}[FAST] The quick tier only: ${#SELECTED[@]} suites (the full run is ./run_tests.sh; tiers: docs/WORKFLOW.md)${RESET}"
fi

# Clean Build Directories if requested
if [ "$CLEAN_BUILD" -eq 1 ]; then
    echo -e "${YELLOW}[CLEAN] Cleaning build directories...${RESET}"
    rm -rf "$BUILD_DIR" build_e2e
fi

# Which builds do the selected suites need?
NEED_MAIN=0
NEED_SIM=0
NEED_E2E=0
FAST_TARGETS=""
for i in ${SELECTED[@]+"${SELECTED[@]}"}; do
    case "${SUITE_TIERS[$i]}" in
        assets|app) NEED_MAIN=1 ;;
        sim) NEED_MAIN=1; NEED_SIM=1 ;;
        e2e) NEED_E2E=1 ;;
    esac
    for target in ${SUITE_TARGETS[$i]}; do
        [ "$target" = "-" ] && continue
        case " $FAST_TARGETS " in
            *" $target "*) ;;
            *) FAST_TARGETS="$FAST_TARGETS $target" ;;
        esac
    done
done

# 1. Build Asset, Simulation, and App Tests (libants-assets, libants-sim, libants-app)
if [ "$NO_BUILD" -eq 0 ] && [ "$NEED_MAIN" -eq 1 ]; then
    if [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
        echo -e "${YELLOW}[BUILD] Configuring ${BUILD_DIR} (CMake)...${RESET}"
        if [ "$RUN_ASAN" -eq 1 ]; then
            cmake -B "$BUILD_DIR" -S . -DENABLE_ASAN=ON -DANTS_WERROR=ON >/dev/null
        else
            cmake -B "$BUILD_DIR" -S . -DANTS_WERROR=ON -DCMAKE_BUILD_TYPE=Release >/dev/null
        fi
    fi
    if [ "$FAST" -eq 1 ]; then
        # only what the quick suites run (an incremental build of the changed files); map_sweep and bot_arena are optional targets, named here like the rest
        echo -e "${YELLOW}[BUILD] Compiling the targets of the quick suites (-j${NCPU})...${RESET}"
        # shellcheck disable=SC2086
        cmake --build "$BUILD_DIR" -j"$NCPU" --target $FAST_TARGETS
    else
        echo -e "${YELLOW}[BUILD] Compiling libraries and test suites (-j${NCPU})...${RESET}"
        cmake --build "$BUILD_DIR" -j"$NCPU"
        if [ "$NEED_SIM" -eq 1 ]; then
            # the map sweep and the bot arena are optional targets (not in the default build); their --selftest is run with the simulation suites
            cmake --build "$BUILD_DIR" -j"$NCPU" --target map_sweep bot_arena
        fi
    fi
fi

# 2. Build E2E Runner
if [ "$NO_BUILD" -eq 0 ] && [ "$NEED_E2E" -eq 1 ]; then
    if [ ! -d "build_e2e" ] || [ ! -f "build_e2e/CMakeCache.txt" ]; then
        echo -e "${YELLOW}[BUILD] Configuring build_e2e (CMake)...${RESET}"
        cmake -S tests/e2e -B build_e2e >/dev/null
    fi
    echo -e "${YELLOW}[BUILD] Compiling E2E opaque-box test runner (-j${NCPU})...${RESET}"
    cmake --build build_e2e -j"$NCPU"
fi

# Disable exit-on-error to collect all test results for dashboard
set +e

now_ms() {
    if command -v perl >/dev/null 2>&1; then
        perl -MTime::HiRes=time -e 'printf "%d", time() * 1000'
    else
        echo $(( $(date +%s) * 1000 ))
    fi
}

format_ms() {
    # 12345 -> 12.3s
    local ms="$1"
    printf '%d.%ds' $((ms / 1000)) $(((ms % 1000) / 100))
}

SUITE_STATUS=()
SUITE_MS=()
START_TIME=$(date +%s)

# 3. Execute the selected suites, in order, and time each one
for i in ${SELECTED[@]+"${SELECTED[@]}"}; do
    echo ""
    echo -e "${BOLD}${BLUE}======================================================================${RESET}"
    echo -e "${BOLD}${BLUE}>>> ${SUITE_IDS[$i]}. RUNNING ${SUITE_TITLES[$i]}...${RESET}"
    echo -e "${BOLD}${BLUE}======================================================================${RESET}"
    suite_start=$(now_ms)
    ( eval "${SUITE_CMDS[$i]}" )      # a subshell: a command that exits (or fails) ends the suite, never this script
    SUITE_STATUS[$i]=$?
    suite_end=$(now_ms)
    SUITE_MS[$i]=$((suite_end - suite_start))
done

END_TIME=$(date +%s)
ELAPSED_SEC=$((END_TIME - START_TIME))

# 4. Master Summary Dashboard
echo ""
echo -e "${BOLD}${CYAN}======================================================================${RESET}"
echo -e "${BOLD}${CYAN}                     OVERALL TEST RUN SUMMARY                         ${RESET}"
echo -e "${BOLD}${CYAN}======================================================================${RESET}"

TOTAL_FAILED=0
TOTAL_RUN=0
SLOWEST_FILE=$(mktemp "${TMPDIR:-/tmp}/ants_suite_times.XXXXXX")
for i in ${SELECTED[@]+"${SELECTED[@]}"}; do
    TOTAL_RUN=$((TOTAL_RUN + 1))
    secs=$(format_ms "${SUITE_MS[$i]}")
    printf '%s\t%s %s\n' "${SUITE_MS[$i]}" "${SUITE_IDS[$i]}" "${SUITE_LABELS[$i]}" >> "$SLOWEST_FILE"
    if [ "${SUITE_STATUS[$i]}" -eq 0 ]; then
        printf ' %-7s %-62s ' "${SUITE_IDS[$i]}" "${SUITE_LABELS[$i]}:"
        echo -e "${GREEN}PASSED${RESET}  ${secs}"
    else
        printf ' %-7s %-62s ' "${SUITE_IDS[$i]}" "${SUITE_LABELS[$i]}:"
        echo -e "${RED}FAILED (exit code ${SUITE_STATUS[$i]})${RESET}  ${secs}"
        TOTAL_FAILED=$((TOTAL_FAILED + 1))
    fi
done

echo -e "${BOLD}${CYAN}----------------------------------------------------------------------${RESET}"
if [ "$TOTAL_RUN" -gt 0 ]; then
    echo " Slowest suites:"
    sort -t "$(printf '\t')" -k1,1 -rn "$SLOWEST_FILE" | head -5 | while IFS="$(printf '\t')" read -r ms name; do
        printf '   %8s  %s\n' "$(format_ms "$ms")" "$name"
    done
fi
rm -f "$SLOWEST_FILE"
echo -e " Suites run: ${TOTAL_RUN}, failed: ${TOTAL_FAILED}"
echo -e " Total Test Execution Time: ${ELAPSED_SEC}s"
if [ "$TOTAL_RUN" -eq 0 ]; then
    echo -e "${BOLD}${RED} RESULT: NO TEST SUITE WAS SELECTED!                                  ${RESET}"
    echo -e "${BOLD}${CYAN}======================================================================${RESET}"
    exit 1
elif [ "$TOTAL_FAILED" -eq 0 ]; then
    echo -e "${BOLD}${GREEN} RESULT: ALL EXECUTED TEST SUITES PASSED CLEANLY!                     ${RESET}"
    echo -e "${BOLD}${CYAN}======================================================================${RESET}"
    exit 0
else
    echo -e "${BOLD}${RED} RESULT: ${TOTAL_FAILED} TEST SUITE(S) FAILED!                               ${RESET}"
    echo -e "${BOLD}${CYAN}======================================================================${RESET}"
    exit 1
fi
