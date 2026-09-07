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
    echo "  --all            Run all test suites (libants-assets + libants-sim + libants-app + E2E, default)"
    echo "  --assets         Run only asset decoder tests (test_assets)"
    echo "  --sim            Run only simulation rules tests (test_sim_rules)"
    echo "  --app            Run only application integration tests (test_app_integration)"
    echo "  --e2e            Run only opaque-box E2E test suites (e2e_runner)"
    echo "  --asan           Build and run with AddressSanitizer (build_asan)"
    echo "  --clean          Remove build directories and rebuild before testing"
    echo "  -v, --verbose    Enable verbose assertions output in test suites"
    echo "  -h, --help       Display this help message and exit"
    echo ""
}

# Parse Command Line Options
RUN_ASSETS=1
RUN_SIM=1
RUN_APP=1
RUN_E2E=1
RUN_ASAN=0
CLEAN_BUILD=0
VERBOSE=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --all)
            RUN_ASSETS=1
            RUN_SIM=1
            RUN_APP=1
            RUN_E2E=1
            ;;
        --assets)
            RUN_ASSETS=1
            RUN_SIM=0
            RUN_APP=0
            RUN_E2E=0
            ;;
        --sim)
            RUN_ASSETS=0
            RUN_SIM=1
            RUN_APP=0
            RUN_E2E=0
            ;;
        --app)
            RUN_ASSETS=0
            RUN_SIM=0
            RUN_APP=1
            RUN_E2E=0
            ;;
        --e2e)
            RUN_ASSETS=0
            RUN_SIM=0
            RUN_APP=0
            RUN_E2E=1
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

echo -e "${BOLD}${CYAN}======================================================================${RESET}"
echo -e "${BOLD}${CYAN}           MICROSOFT ANTS ENGINE REMAKE - MASTER TEST RUNNER          ${RESET}"
echo -e "${BOLD}${CYAN}======================================================================${RESET}"

NCPU=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
BUILD_DIR="build"
if [ "$RUN_ASAN" -eq 1 ]; then
    BUILD_DIR="build_asan"
fi

# Clean Build Directories if requested
if [ "$CLEAN_BUILD" -eq 1 ]; then
    echo -e "${YELLOW}[CLEAN] Cleaning build directories...${RESET}"
    rm -rf "$BUILD_DIR" build_e2e
fi

# 1. Build Asset, Simulation, and App Tests (libants-assets, libants-sim, libants-app)
if [ "$RUN_ASSETS" -eq 1 ] || [ "$RUN_SIM" -eq 1 ] || [ "$RUN_APP" -eq 1 ]; then
    if [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
        echo -e "${YELLOW}[BUILD] Configuring ${BUILD_DIR} (CMake)...${RESET}"
        if [ "$RUN_ASAN" -eq 1 ]; then
            cmake -B "$BUILD_DIR" -S . -DENABLE_ASAN=ON >/dev/null
        else
            cmake -B "$BUILD_DIR" -S . >/dev/null
        fi
    fi
    echo -e "${YELLOW}[BUILD] Compiling libraries and test suites (-j${NCPU})...${RESET}"
    cmake --build "$BUILD_DIR" -j"$NCPU"
fi

# 2. Build E2E Runner
if [ "$RUN_E2E" -eq 1 ]; then
    if [ ! -d "build_e2e" ] || [ ! -f "build_e2e/CMakeCache.txt" ]; then
        echo -e "${YELLOW}[BUILD] Configuring build_e2e (CMake)...${RESET}"
        cmake -S tests/e2e -B build_e2e >/dev/null
    fi
    echo -e "${YELLOW}[BUILD] Compiling E2E opaque-box test runner (-j${NCPU})...${RESET}"
    cmake --build build_e2e -j"$NCPU"
fi

# Disable exit-on-error to collect all test results for dashboard
set +e
ASSETS_STATUS=0
SIM_STATUS=0
CHALLENGER_M2_1_STATUS=0
CHALLENGER_M2_2_STATUS=0
APP_STATUS=0
E2E_STATUS=0
START_TIME=$(date +%s)

# 3. Execute Asset Decoder Tests
if [ "$RUN_ASSETS" -eq 1 ]; then
    echo ""
    echo -e "${BOLD}${BLUE}======================================================================${RESET}"
    echo -e "${BOLD}${BLUE}>>> 1. RUNNING ASSET DECODER SUITES (libants-assets)...               ${RESET}"
    echo -e "${BOLD}${BLUE}======================================================================${RESET}"
    "./$BUILD_DIR/tests/test_assets/test_assets"
    ASSETS_STATUS=$?
fi

# 4. Execute Simulation Rules Tests
if [ "$RUN_SIM" -eq 1 ]; then
    echo ""
    echo -e "${BOLD}${BLUE}======================================================================${RESET}"
    echo -e "${BOLD}${BLUE}>>> 2. RUNNING SIMULATION RULES SUITES (libants-sim)...               ${RESET}"
    echo -e "${BOLD}${BLUE}======================================================================${RESET}"
    "./$BUILD_DIR/tests/test_sim/test_sim_rules"
    SIM_STATUS=$?

    echo ""
    echo -e "${BOLD}${BLUE}======================================================================${RESET}"
    echo -e "${BOLD}${BLUE}>>> 2.1 RUNNING CHALLENGER M2_1 (Combat, Hazards, Physics)...         ${RESET}"
    echo -e "${BOLD}${BLUE}======================================================================${RESET}"
    "./$BUILD_DIR/tests/test_sim/test_challenger_m2_1"
    CHALLENGER_M2_1_STATUS=$?

    echo ""
    echo -e "${BOLD}${BLUE}======================================================================${RESET}"
    echo -e "${BOLD}${BLUE}>>> 2.2 RUNNING CHALLENGER M2_2 (Lifecycle, Economy, Alliances)...   ${RESET}"
    echo -e "${BOLD}${BLUE}======================================================================${RESET}"
    "./$BUILD_DIR/tests/test_sim/test_challenger_m2_2"
    CHALLENGER_M2_2_STATUS=$?
fi

# 5. Execute Application Integration Tests
if [ "$RUN_APP" -eq 1 ]; then
    echo ""
    echo -e "${BOLD}${BLUE}======================================================================${RESET}"
    echo -e "${BOLD}${BLUE}>>> 3. RUNNING APPLICATION INTEGRATION SUITES (libants-app)...       ${RESET}"
    echo -e "${BOLD}${BLUE}======================================================================${RESET}"
    "./$BUILD_DIR/tests/test_app/test_app_integration"
    APP_STATUS=$?
fi

# 6. Execute E2E Opaque-Box Tests
if [ "$RUN_E2E" -eq 1 ]; then
    echo ""
    echo -e "${BOLD}${BLUE}======================================================================${RESET}"
    echo -e "${BOLD}${BLUE}>>> 4. RUNNING E2E OPAQUE-BOX VERIFICATION SUITES...                  ${RESET}"
    echo -e "${BOLD}${BLUE}======================================================================${RESET}"
    E2E_ARGS="--all"
    if [ "$VERBOSE" -eq 1 ]; then
        E2E_ARGS="--all -v"
    fi
    ./build_e2e/e2e_runner $E2E_ARGS
    E2E_STATUS=$?
fi

END_TIME=$(date +%s)
ELAPSED_SEC=$((END_TIME - START_TIME))

# 7. Master Summary Dashboard
echo ""
echo -e "${BOLD}${CYAN}======================================================================${RESET}"
echo -e "${BOLD}${CYAN}                     OVERALL TEST RUN SUMMARY                         ${RESET}"
echo -e "${BOLD}${CYAN}======================================================================${RESET}"

TOTAL_FAILED=0

if [ "$RUN_ASSETS" -eq 1 ]; then
    if [ "$ASSETS_STATUS" -eq 0 ]; then
        echo -e " 1. Native Asset Decoder Tests (test_assets):       ${GREEN}PASSED${RESET}"
    else
        echo -e " 1. Native Asset Decoder Tests (test_assets):       ${RED}FAILED (exit code ${ASSETS_STATUS})${RESET}"
        TOTAL_FAILED=$((TOTAL_FAILED + 1))
    fi
fi

if [ "$RUN_SIM" -eq 1 ]; then
    if [ "$SIM_STATUS" -eq 0 ]; then
        echo -e " 2. Simulation Rules Tests (test_sim_rules):        ${GREEN}PASSED${RESET}"
    else
        echo -e " 2. Simulation Rules Tests (test_sim_rules):        ${RED}FAILED (exit code ${SIM_STATUS})${RESET}"
        TOTAL_FAILED=$((TOTAL_FAILED + 1))
    fi

    if [ "$CHALLENGER_M2_1_STATUS" -eq 0 ]; then
        echo -e " 2.1 Challenger M2_1 (test_challenger_m2_1):         ${GREEN}PASSED${RESET}"
    else
        echo -e " 2.1 Challenger M2_1 (test_challenger_m2_1):         ${RED}FAILED (exit code ${CHALLENGER_M2_1_STATUS})${RESET}"
        TOTAL_FAILED=$((TOTAL_FAILED + 1))
    fi

    if [ "$CHALLENGER_M2_2_STATUS" -eq 0 ]; then
        echo -e " 2.2 Challenger M2_2 (test_challenger_m2_2):         ${GREEN}PASSED${RESET}"
    else
        echo -e " 2.2 Challenger M2_2 (test_challenger_m2_2):         ${RED}FAILED (exit code ${CHALLENGER_M2_2_STATUS})${RESET}"
        TOTAL_FAILED=$((TOTAL_FAILED + 1))
    fi
fi

if [ "$RUN_APP" -eq 1 ]; then
    if [ "$APP_STATUS" -eq 0 ]; then
        echo -e " 3. Application Integration Tests (test_app):       ${GREEN}PASSED${RESET}"
    else
        echo -e " 3. Application Integration Tests (test_app):       ${RED}FAILED (exit code ${APP_STATUS})${RESET}"
        TOTAL_FAILED=$((TOTAL_FAILED + 1))
    fi
fi

if [ "$RUN_E2E" -eq 1 ]; then
    if [ "$E2E_STATUS" -eq 0 ]; then
        echo -e " 4. Opaque-Box E2E Tests (e2e_runner):              ${GREEN}PASSED${RESET}"
    else
        echo -e " 4. Opaque-Box E2E Tests (e2e_runner):              ${RED}FAILED (exit code ${E2E_STATUS})${RESET}"
        TOTAL_FAILED=$((TOTAL_FAILED + 1))
    fi
fi

echo -e "${BOLD}${CYAN}----------------------------------------------------------------------${RESET}"
echo -e " Total Test Execution Time: ${ELAPSED_SEC}s"
if [ "$TOTAL_FAILED" -eq 0 ]; then
    echo -e "${BOLD}${GREEN} RESULT: ALL EXECUTED TEST SUITES PASSED CLEANLY!                     ${RESET}"
    echo -e "${BOLD}${CYAN}======================================================================${RESET}"
    exit 0
else
    echo -e "${BOLD}${RED} RESULT: ${TOTAL_FAILED} TEST SUITE(S) FAILED!                               ${RESET}"
    echo -e "${BOLD}${CYAN}======================================================================${RESET}"
    exit 1
fi
