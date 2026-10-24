#!/usr/bin/env bash
set -e

echo "========================================================"
echo "    Building Ants (1998) WebAssembly / HTML5 Port       "
echo "========================================================"

if ! command -v emcmake &> /dev/null || ! command -v emcc &> /dev/null; then
    if [ -f "${EMSDK}/emsdk_env.sh" ]; then
        source "${EMSDK}/emsdk_env.sh" > /dev/null 2>&1
    elif [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
        source "$HOME/emsdk/emsdk_env.sh" > /dev/null 2>&1
    fi
fi

if ! command -v emcmake &> /dev/null || ! command -v emcc &> /dev/null; then
    echo "Error: Emscripten (emcmake / emcc) is not in your PATH."
    echo ""
    echo "To install Emscripten on macOS:"
    echo "  brew install emscripten"
    echo ""
    echo "Or via the official emsdk:"
    echo "  git clone https://github.com/emscripten-core/emsdk.git"
    echo "  cd emsdk && ./emsdk install latest && ./emsdk activate latest"
    echo "  source ./emsdk_env.sh"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build_web"
DIST_DIR="${SCRIPT_DIR}/dist"

echo "==> Configuring CMake with Emscripten..."
emcmake cmake -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF

echo "==> Building WebAssembly target (index.html, index.js, index.wasm, index.data)..."
cmake --build "${BUILD_DIR}" -j4

echo "==> Packaging into ${DIST_DIR}..."
mkdir -p "${DIST_DIR}"
# The page's footer names the version (the file VERSION) and the build (ANTS_BUILD_ID, else the commit of this checkout, else the time), as the Docker image does
GAME_VERSION="v$(head -n 1 "${SCRIPT_DIR}/VERSION" | tr -d '[:space:]')"
BUILD_ID="$(sh "${SCRIPT_DIR}/docker/resolve_build_id.sh" "${ANTS_BUILD_ID:-}" "${SCRIPT_DIR}/.git" 2>/dev/null)"
BUILD_DATE="$(TZ=PST8PDT,M3.2.0,M11.1.0 date '+%Y-%m-%d %H:%M PT')"
sed -e "s/@@GAME_VERSION@@/${GAME_VERSION}/g" -e "s/@@BUILD_ID@@/${BUILD_ID}/g" -e "s/@@BUILD_DATE@@/${BUILD_DATE}/g" -e "s/@@SITE_TITLE@@//g" -e "s/@@SITE_FOOTER@@//g" "${BUILD_DIR}/src/ants_app/index.html" > "${DIST_DIR}/index.html"
cp -f "${DIST_DIR}/index.html" "${DIST_DIR}/play.html"            # the game page at its own path (the games on this computer), as the image has it
sed -e "s/@@GAME_VERSION@@/${GAME_VERSION}/g" -e "s/@@BUILD_ID@@/${BUILD_ID}/g" -e "s/@@BUILD_DATE@@/${BUILD_DATE}/g" -e "s/@@SITE_TITLE@@//g" -e "s/@@SITE_FOOTER@@//g" "${SCRIPT_DIR}/web/lobby.html" > "${DIST_DIR}/lobby.html"
for PAGE in matches match live; do                                # the pages of the match history: the list, one match, the matches that run now
    sed -e "s/@@GAME_VERSION@@/${GAME_VERSION}/g" -e "s/@@BUILD_ID@@/${BUILD_ID}/g" -e "s/@@BUILD_DATE@@/${BUILD_DATE}/g" -e "s/@@SITE_TITLE@@//g" -e "s/@@SITE_FOOTER@@//g" "${SCRIPT_DIR}/web/${PAGE}.html" > "${DIST_DIR}/${PAGE}.html"
done
cp -f "${SCRIPT_DIR}/web/matches.css" "${DIST_DIR}/"                                                                                                                                                # ... their stylesheet
cp -f "${SCRIPT_DIR}/web/replay_page.js" "${DIST_DIR}/"                                                                                                                                             # ... and the helpers they share with the game page
cp -f "${SCRIPT_DIR}/web/history_page.js" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/web/match_stats.js" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/web/match_stats.css" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/web/death_map.js" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/web/death_map.css" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/web/replay_stats.css" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/web/shapes.js" "${DIST_DIR}/"                                                                                                                                           # the pages' one table of the picture's shapes
cp -f "${SCRIPT_DIR}/web/map_files.js" "${DIST_DIR}/"                                                                                                                                             # ... and the helpers it shares with the game page
cp -f "${SCRIPT_DIR}/web/countdown_button.js" "${DIST_DIR}/"                                                                                                                                             # ... and the countdown's Fullscreen button
cp -f "${SCRIPT_DIR}/web/game_menu.js" "${SCRIPT_DIR}/web/game_menu.css" "${DIST_DIR}/"                                                                                  # ... the settings menu's panel (loaded by the game page once the game runs)
cp -f "${BUILD_DIR}/lvl_core/lvl_core.js" "${BUILD_DIR}/lvl_core/lvl_core.wasm" "${DIST_DIR}/"                                                                                    # the map core for the browser (docs/MAP_FORMAT.md, "In the browser")
cp -f "${SCRIPT_DIR}/web/lvl_core_api.js" "${DIST_DIR}/"
MAPMAKER_VERSION="v$(head -n 1 "${SCRIPT_DIR}/web/MAPMAKER_VERSION" | tr -d '[:space:]')" && grep -q "id=\"mapmaker-version\">${MAPMAKER_VERSION}<" "${SCRIPT_DIR}/web/mapmaker.html"
sed -e "s|<span id=\"mapmaker-build\">local</span>|<span id=\"mapmaker-build\">build <span id=\"game-build-id\">${BUILD_ID}</span> <span id=\"game-build-date\">(${BUILD_DATE})</span></span>|" "${SCRIPT_DIR}/web/mapmaker.html" > "${DIST_DIR}/mapmaker.html"   # the map maker (docs/MAP_EDITOR.md); its scripts are in front/
cp -f "${SCRIPT_DIR}/web/admin.html" "${SCRIPT_DIR}/web/admin.css" "${SCRIPT_DIR}/web/admin_page.js" "${DIST_DIR}/"   # the owner's page (docs/MAP_SUBMISSIONS.md): typed by hand, nothing links to it
cp -f "${BUILD_DIR}/src/ants_app/index.js" "${DIST_DIR}/"
cp -f "${BUILD_DIR}/src/ants_app/index.wasm" "${DIST_DIR}/"
cp -f "${BUILD_DIR}/src/ants_app/index.data" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/web"/favicon.* "${DIST_DIR}/"
mkdir -p "${DIST_DIR}/front"
cp -f "${SCRIPT_DIR}/web/front"/* "${DIST_DIR}/front/"            # the front page's pictures and font, served at /front/
python3 "${SCRIPT_DIR}/tools/map_atlas.py" "${DIST_DIR}/mapmaker_atlas"        # the map maker's sprite atlas (docs/MAP_EDITOR.md, "Sprites"); the few big toys that it fetches one by one come from /asset_catalog/sprites/, which the image has and this folder does not

echo "========================================================"
echo " Build Succeeded! Web distribution ready in: ${DIST_DIR}"
echo " To test locally in your browser:"
echo "   python3 -m http.server 8080 -d dist"
echo " Then navigate to: http://localhost:8080/lobby.html (the front page; the image's nginx serves it at /, python's server serves the game page there)"
echo "========================================================"
