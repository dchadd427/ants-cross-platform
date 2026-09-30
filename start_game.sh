#!/usr/bin/env bash
set -e

# Change directory to the repository root where this script resides
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "======================================================================"
echo "                      ANTS ENGINE REMAKE (macOS)                      "
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
echo " SETUP SCREEN (ON LAUNCH):"
echo "   * [1-6] or the arrow keys pick a map; click START (or press Enter) to begin; Esc leaves."
echo "   * [F] toggles Fog of War. INTRO music plays here; a match plays a random in-game track (Ctrl+M mutes)."
echo ""
echo " MOUSE (the original's pointer model):"
echo "   * Left click an ant: select it (Shift adds your ants). Drag over 4 px: red rubber band selects your ants."
echo "   * With ants selected, a left click acts by the cursor: ground = move, food = harvest,"
echo "     another player's ant = attack, a valid special target = the ant's ability."
echo "   * Right click: the order at the release (move for several ants, workers and combat ants;"
echo "     otherwise the ability of the single ant's type: bomb, fire wall, bridge, raid)."
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
echo "[LAUNCHER] Starting Ants..."
echo ""

exec ./build/src/ants_app/ants "$@"
