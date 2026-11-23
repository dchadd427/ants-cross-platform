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
# The footers of the game page and the front page, and the Menu of every page with the bar at the top, name the version (the file VERSION) and the build (ANTS_BUILD_ID, else the commit of this checkout, else the time), as the Docker image does
GAME_VERSION="v$(head -n 1 "${SCRIPT_DIR}/VERSION" | tr -d '[:space:]')"
BUILD_ID="$(sh "${SCRIPT_DIR}/docker/resolve_build_id.sh" "${ANTS_BUILD_ID:-}" "${SCRIPT_DIR}/.git" 2>/dev/null)"
BUILD_DATE="$(TZ=PST8PDT,M3.2.0,M11.1.0 date '+%Y-%m-%d %H:%M PT')"
# The address that the pages name themselves by (web/SITE_ORIGIN: their canonical links and share cards; docs/BUILD_AND_RUN.md "Search engines and share cards"); robots.txt and sitemap.xml are made with it too
SITE_ORIGIN="$(head -n 1 "${SCRIPT_DIR}/web/SITE_ORIGIN" | tr -d '[:space:]')"
python3 "${SCRIPT_DIR}/tools/seo_files.py" "${SITE_ORIGIN}" "${DIST_DIR}"
sed -e "s/@@GAME_VERSION@@/${GAME_VERSION}/g" -e "s/@@BUILD_ID@@/${BUILD_ID}/g" -e "s/@@BUILD_DATE@@/${BUILD_DATE}/g" -e "s/@@SITE_TITLE@@//g" -e "s/@@SITE_FOOTER@@//g" -e "s|@@SITE_ORIGIN@@|${SITE_ORIGIN}|g" "${BUILD_DIR}/src/ants_app/index.html" > "${DIST_DIR}/index.html"
cp -f "${DIST_DIR}/index.html" "${DIST_DIR}/play.html"            # the game page at its own path (the games on this computer), as the image has it
sed -e "s/@@GAME_VERSION@@/${GAME_VERSION}/g" -e "s/@@BUILD_ID@@/${BUILD_ID}/g" -e "s/@@BUILD_DATE@@/${BUILD_DATE}/g" -e "s/@@SITE_TITLE@@//g" -e "s/@@SITE_FOOTER@@//g" -e "s|@@SITE_ORIGIN@@|${SITE_ORIGIN}|g" "${SCRIPT_DIR}/web/lobby.html" > "${DIST_DIR}/lobby.html"
for PAGE in matches match legal; do                              # the pages of the match history (the list, one match) and the Legal page
    sed -e "s/@@GAME_VERSION@@/${GAME_VERSION}/g" -e "s/@@BUILD_ID@@/${BUILD_ID}/g" -e "s/@@BUILD_DATE@@/${BUILD_DATE}/g" -e "s/@@SITE_TITLE@@//g" -e "s/@@SITE_FOOTER@@//g" -e "s|@@SITE_ORIGIN@@|${SITE_ORIGIN}|g" "${SCRIPT_DIR}/web/${PAGE}.html" > "${DIST_DIR}/${PAGE}.html"
done
cp -f "${SCRIPT_DIR}/web/matches.css" "${DIST_DIR}/"                                                                                                                                                # ... their stylesheet
cp -f "${SCRIPT_DIR}/web/site_nav.css" "${DIST_DIR}/"                                                                                                                                              # the bar at the top of every page (its style and its Menu)
cp -f "${SCRIPT_DIR}/web/site_nav.js" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/web/update_notice.css" "${SCRIPT_DIR}/web/update_notice.js" "${DIST_DIR}/"                                                                           # the strip and the window of a new version (the image also has /whats_new.json, made from CHANGELOG.md: this folder has none, so the pages stay silent)
cp -f "${SCRIPT_DIR}/web/game_bar.css" "${SCRIPT_DIR}/web/game_bar.js" "${DIST_DIR}/"                                                                                    # the game page's part of the bar (its rules, the links that ask first, the keys)
cp -f "${SCRIPT_DIR}/web/replay_page.js" "${DIST_DIR}/"                                                                                                                                             # ... and the helpers they share with the game page
cp -f "${SCRIPT_DIR}/web/history_page.js" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/web/match_stats.js" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/web/match_stats.css" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/web/death_map.js" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/web/death_map.css" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/web/replay_stats.css" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/web/rejoin_key.js" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/web/hub_here.js" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/web/shapes.js" "${DIST_DIR}/"                                                                                                                                           # the pages' one table of the picture's shapes
cp -f "${SCRIPT_DIR}/web/shape_picker.js" "${DIST_DIR}/"                                                                                                                                     # ... and its control, a button and a list that the page makes (a native list does not open under the fullscreen mouse lock)
cp -f "${SCRIPT_DIR}/web/map_files.js" "${DIST_DIR}/"                                                                                                                                             # ... and the helpers it shares with the game page
cp -f "${SCRIPT_DIR}/web/countdown_button.js" "${DIST_DIR}/"                                                                                                                                             # ... and the countdown's Fullscreen button
cp -f "${SCRIPT_DIR}/web/alert_core.js" "${DIST_DIR}/"                                                                                                                                                   # the alerts that need no permission: the switches, the tab alert, the sound table (docs/HOME_SCREEN.md "Alerts")
cp -f "${SCRIPT_DIR}/web/hud_fullscreen.js" "${DIST_DIR}/"                                                                                                                                                # the HUD's own Fullscreen button
cp -f "${SCRIPT_DIR}/web/wake_lock.js" "${DIST_DIR}/"                                                                                                                                                    # ... and the screen that stays on while a match is played or a recording plays
cp -f "${SCRIPT_DIR}/web/p2p.js" "${DIST_DIR}/"                                                                                                                                      # the data channels to the other players (docs/P2P_HINTS.md)
cp -f "${SCRIPT_DIR}/web/touch_keys.js" "${SCRIPT_DIR}/web/touch_keys.css" "${DIST_DIR}/"                                                                                  # ... the hotkey buttons of a touch screen (made by the game page)
cp -f "${SCRIPT_DIR}/web/game_menu.js" "${SCRIPT_DIR}/web/game_menu.css" "${DIST_DIR}/"                                                                                  # ... the settings menu's panel (loaded by the game page once the game runs)
cp -f "${BUILD_DIR}/lvl_core/lvl_core.js" "${BUILD_DIR}/lvl_core/lvl_core.wasm" "${DIST_DIR}/"                                                                                    # the map core for the browser (docs/MAP_FORMAT.md, "In the browser")
cp -f "${SCRIPT_DIR}/web/lvl_core_api.js" "${DIST_DIR}/"
MAPMAKER_VERSION="v$(head -n 1 "${SCRIPT_DIR}/web/MAPMAKER_VERSION" | tr -d '[:space:]')" && grep -q "data-maker=\"${MAPMAKER_VERSION}\"" "${SCRIPT_DIR}/web/mapmaker.html"
sed -e "s/@@GAME_VERSION@@/${GAME_VERSION}/g" -e "s/@@BUILD_ID@@/${BUILD_ID}/g" -e "s/@@BUILD_DATE@@/${BUILD_DATE}/g" -e "s|@@SITE_ORIGIN@@|${SITE_ORIGIN}|g" "${SCRIPT_DIR}/web/mapmaker.html" > "${DIST_DIR}/mapmaker.html"   # the map maker (docs/MAP_EDITOR.md); its scripts are in front/
sed -e "s/@@GAME_VERSION@@/${GAME_VERSION}/g" -e "s/@@BUILD_ID@@/${BUILD_ID}/g" -e "s/@@BUILD_DATE@@/${BUILD_DATE}/g" "${SCRIPT_DIR}/web/admin.html" > "${DIST_DIR}/admin.html"   # the admin page (docs/MAP_SUBMISSIONS.md): typed by hand, with the bar and its stamps
cp -f "${SCRIPT_DIR}/web/admin.css" "${SCRIPT_DIR}/web/admin_map_pick.js" "${SCRIPT_DIR}/web/admin_bots.js" "${SCRIPT_DIR}/web/admin_page.js" "${DIST_DIR}/"
cp -f "${BUILD_DIR}/src/ants_app/index.js" "${DIST_DIR}/"
cp -f "${BUILD_DIR}/src/ants_app/index.wasm" "${DIST_DIR}/"
cp -f "${BUILD_DIR}/src/ants_app/index.data" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/web"/favicon.* "${DIST_DIR}/"
mkdir -p "${DIST_DIR}/front" "${DIST_DIR}/home" "${DIST_DIR}/app"
cp -f "${SCRIPT_DIR}/web/front"/* "${DIST_DIR}/front/"            # the front page's pictures and font, served at /front/
cp -f "${SCRIPT_DIR}/web/home"/* "${DIST_DIR}/home/"              # the front page's Home and Room screens (docs/HOME_SCREEN.md), served at /home/
cp -f "${SCRIPT_DIR}/web/manifest.webmanifest" "${DIST_DIR}/"             # the installable web app: its manifest (served at /manifest.webmanifest) ...
cp -f "${SCRIPT_DIR}/web/app"/* "${DIST_DIR}/app/"                  # ... and its icons (tools/make_app_icons.py), served at /app/
python3 "${SCRIPT_DIR}/tools/map_atlas.py" "${DIST_DIR}/mapmaker_atlas"        # the map maker's sprite atlas (docs/MAP_EDITOR.md, "Sprites"); the few big toys that it fetches one by one come from /asset_catalog/sprites/, which the image has and this folder does not

echo "========================================================"
echo " Build Succeeded! Web distribution ready in: ${DIST_DIR}"
echo " To test locally in your browser:"
echo "   python3 -m http.server 8080 -d dist"
echo " Then navigate to: http://localhost:8080/lobby.html (the front page; the image's nginx serves it at /, python's server serves the game page there)"
echo " (the clean page addresses /play, /matches, /match, /legal, /changelog and /mapmaker are docker/nginx.conf's: python's server knows the .html files only; tools/web_without_docker.py serves the site with nginx)"
echo "========================================================"
