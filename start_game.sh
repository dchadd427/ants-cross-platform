#!/usr/bin/env bash
set -e

# Change directory to the repository root where this script resides
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "======================================================================"
echo "                MICROSOFT ANTS ENGINE REMAKE (macOS)                  "
echo "======================================================================"

# 1. Ensure build directory and binary exist
if [ ! -f "build/src/ants_app/ants" ]; then
    echo "[LAUNCHER] Game binary not found. Building release binary now..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --target ants -j"$(sysctl -n hw.ncpu || echo 4)"
fi

echo ""
echo "----------------------------------------------------------------------"
echo "                      AUTHENTIC CONTROL SCHEME                        "
echo "----------------------------------------------------------------------"
echo " MAP SELECTION SCREEN (ON LAUNCH):"
echo "   * INTRO.MID loops exclusively during battlefield selection."
echo "   * Use [1-6] or [Up/Down] arrows to highlight maps (Treasure, Small, etc)."
echo "   * Double-click map, click [START GAME], or press [ENTER] to begin match."
echo "   * INTRO.MID immediately stops when match begins (in-game music is silent)."
echo ""
echo " MOUSE CONTROLS:"
echo "   * Left Click on Ant:         Select single ant"
echo "   * Left Click on Ground:      Issue Move order to selected ant(s)"
echo "   * Left Click on Enemy Ant:   Issue Attack order to selected ant(s)"
echo "   * Left Click & Drag (>4px):  Marquee rectangle selection box"
echo "   * Left Click on Minimap:     Instantly jump camera to location"
echo "   * Right Click on Field:      Execute Special Ability directly:"
echo "                                  - Bomber Ant:  Plant / Defuse bomb"
echo "                                  - Fire Ant:    Ignite / Extinguish fire"
echo "                                  - Swimmer Ant: Build bridge across water"
echo "                                  - Thief Ant:   Infiltrate enemy base"
echo "                                (Cancels armed order if one is active)"
echo ""
echo " CAMERA & NAVIGATION:"
echo "   * Edge Pan Scrolling:        Moving mouse within 24px of window edge"
echo "                                scrolls camera freely (C&C / LoL style)"
echo "   * Arrow Keys / WASD:         Pan camera viewport freely"
echo "   * Spacebar:                  Center camera on selected ant / home base"
echo "   * H:                         Center camera on Home Anthill base"
echo ""
echo " HOTKEYS:"
echo "   * L / Ctrl+L:                Toggle Overhead Unit Health Display (ON/OFF)"
echo "   * A / Ctrl+A:                Select all friendly ants"
echo "   * N / P:                     Cycle to Next / Previous friendly ant"
echo "   * M:                         Toggle background music"
echo "   * Esc / C:                   Clear selection / Cancel order / Return to Menu"
echo "----------------------------------------------------------------------"
echo ""
echo "[LAUNCHER] Starting Microsoft Ants..."
echo ""

exec ./build/src/ants_app/ants "$@"
