@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo ======================================================================
echo                       ANTS ENGINE REMAKE (PC)
echo ======================================================================

if not exist "build\src\ants_app\Release\ants.exe" (
    echo [LAUNCHER] Game binary not found. Building release binary now...
    cmake -B build
    cmake --build build --config Release --target ants -j8
)

echo.
echo ----------------------------------------------------------------------
echo                       AUTHENTIC CONTROL SCHEME
echo ----------------------------------------------------------------------
echo  MAP SELECTION SCREEN (ON LAUNCH):
echo    * INTRO.MID loops exclusively during battlefield selection.
echo    * Use [1-6] or [Up/Down] arrows to highlight maps (Treasure, Small, etc).
echo    * Double-click map, click [START GAME], or press [ENTER] to begin match.
echo    * INTRO.MID immediately stops when match begins (in-game music is silent).
echo.
echo  MOUSE CONTROLS:
echo    * Left Click on Ant:         Select single ant
echo    * Left Click on Ground:      Issue Move order to selected ant(s)
echo    * Left Click on Enemy Ant:   Issue Attack order to selected ant(s)
echo    * Left Click ^& Drag (^>4px):  Marquee rectangle selection box
echo    * Left Click on Minimap:     Instantly jump camera to location
echo    * Right Click on Field:      Execute Special Ability directly:
echo                                   - Bomber Ant:  Plant / Defuse bomb
echo                                   - Fire Ant:    Ignite / Extinguish fire
echo                                   - Swimmer Ant: Build bridge across water
echo                                   - Thief Ant:   Infiltrate enemy base
echo                                 (Cancels armed order if one is active)
echo.
echo  CAMERA ^& NAVIGATION:
echo    * Edge Pan Scrolling:        Moving mouse within 24px of window edge
echo                                 scrolls camera freely (C^&C / LoL style)
echo    * Arrow Keys / WASD:         Pan camera viewport freely
echo    * Spacebar:                  Center camera on selected ant / home base
echo    * H:                         Center camera on Home Anthill base
echo.
echo  HOTKEYS:
echo    * L / Ctrl+L:                Toggle Overhead Unit Health Display (ON/OFF)
echo    * A / Ctrl+A:                Select all friendly ants
echo    * N / P:                     Cycle to Next / Previous friendly ant
echo    * M:                         Toggle background music
echo    * Esc / C:                   Clear selection / Cancel order / Return to Menu
echo ----------------------------------------------------------------------
echo.
echo [LAUNCHER] Starting Ants...
echo.

"build\src\ants_app\Release\ants.exe" %*
