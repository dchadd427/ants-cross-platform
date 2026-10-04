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
sed -e "s/@@GAME_VERSION@@/${GAME_VERSION}/g" -e "s/@@BUILD_ID@@/${BUILD_ID}/g" -e "s/@@SITE_TITLE@@//g" -e "s/@@SITE_FOOTER@@//g" "${BUILD_DIR}/src/ants_app/index.html" > "${DIST_DIR}/index.html"
cp -f "${DIST_DIR}/index.html" "${DIST_DIR}/play.html"            # the game page at its own path (the games on this computer), as the image has it
sed -e "s/@@GAME_VERSION@@/${GAME_VERSION}/g" -e "s/@@BUILD_ID@@/${BUILD_ID}/g" -e "s/@@SITE_TITLE@@//g" -e "s/@@SITE_FOOTER@@//g" "${SCRIPT_DIR}/web/lobby.html" > "${DIST_DIR}/lobby.html"
cp -f "${BUILD_DIR}/src/ants_app/index.js" "${DIST_DIR}/"
cp -f "${BUILD_DIR}/src/ants_app/index.wasm" "${DIST_DIR}/"
cp -f "${BUILD_DIR}/src/ants_app/index.data" "${DIST_DIR}/"
cp -f "${SCRIPT_DIR}/web"/favicon.* "${DIST_DIR}/"

echo "========================================================"
echo " Build Succeeded! Web distribution ready in: ${DIST_DIR}"
echo " To test locally in your browser:"
echo "   python3 -m http.server 8080 -d dist"
echo " Then navigate to: http://localhost:8080/lobby.html (the front page; the image's nginx serves it at /, python's server serves the game page there)"
echo "========================================================"
