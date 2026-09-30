@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    for /f "usebackq tokens=*" %%V in (`dir /b /s "%ProgramFiles(x86)%\*vswhere.exe" 2^>nul`) do (
        for /f "usebackq tokens=*" %%C in (`call "%%V" -latest -products * -find "**\cmake.exe" 2^>nul`) do (
            set "PATH=%%~dpC;!PATH!"
        )
    )
)

echo ======================================================================
echo                       ANTS ENGINE REMAKE (PC)
echo ======================================================================

if not exist "build" (
    cmake -B build
)
echo [LAUNCHER] Ensuring Release binary is up to date...
cmake --build build --config Release --target ants -j8
if errorlevel 1 (
    echo [LAUNCHER] Build failed. Please check compiler errors above.
    pause
    exit /b %errorlevel%
)

echo.
echo ----------------------------------------------------------------------
echo                       AUTHENTIC CONTROL SCHEME
echo ----------------------------------------------------------------------
echo  SETUP SCREEN (ON LAUNCH):
echo    * [1-6] or the arrow keys pick a map; click START (or press Enter) to begin; Esc leaves.
echo    * [F] toggles Fog of War. INTRO music plays here; a match plays a random in-game track (Ctrl+M mutes).
echo.
echo  MOUSE (the original's pointer model):
echo    * Left click an ant: select it (Shift adds your ants). Drag over 4 px: red rubber band selects your ants.
echo    * With ants selected, a left click acts by the cursor: ground = move, food = harvest,
echo      another player's ant = attack, a valid special target = the ant's ability.
echo    * Right click: the order at the release (move for several ants, workers and combat ants;
echo      otherwise the ability of the single ant's type: bomb, fire wall, bridge, raid).
echo    * The Move and ability pedestals latch (Stop stops and deselects). Hold left on the minimap to scroll.
echo.
echo  CAMERA:
echo    * Move the pointer to the screen edge to scroll (nothing scrolls with the keyboard or the wheel).
echo.
echo  KEYS (the original's keyboard; the chat box is always active):
echo    * F1 help, F9 - F12 quick chat, Enter sends chat, Esc deselects.
echo    * Ctrl+A select all, Ctrl+H home hill, Ctrl+N / Ctrl+P next / previous ant, Ctrl+S stop,
echo      Ctrl+O options, Ctrl+Q quit, Ctrl+L hit point digits.
echo    * Developer shortcuts: Ctrl+T tile grid, Ctrl+M mute music, Ctrl+1..4 switch team, Shift+F12 screenshot.
echo ----------------------------------------------------------------------
echo.
echo [LAUNCHER] Starting Ants...
echo.

"build\src\ants_app\Release\ants.exe" %*
