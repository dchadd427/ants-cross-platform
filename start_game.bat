@echo off
rem Starts Ants (Windows).
rem
rem   start_game.bat                 one plain game: the desktop start menu comes first (Single player, Join, Host, Quit), then the original's screens as always
rem                                  (add --map-select to start on the setup screen at once)
rem   start_game.bat --players N     the test rig: N games on this machine (2 - 4), one player each, playing one networked match together: window 0 hosts
rem                                  (green), the others join (red, blue, black), every player has a random name. Four lie in a 2 x 2 grid by colour, the way
rem                                  the four hills lie on the Small and Treasure maps (the same as the games on web/lobby.html): black top
rem                                  left, green top right, red bottom left, blue bottom right; two sit side by side (green left, red right), three are green,
rem                                  red, blue in the first three cells of the grid. The windows of the rig never show the menu: each has --host or --join.
rem                                  --players 1 is the plain game
rem   start_game.bat --single        the plain game (the default; the same as --players 1)
rem   start_game.bat --dry-run ...   print the command line of every window and stop (nothing is built or started)
rem   every other argument goes to every window (the game's own options, see docs/COMMAND_LINE.md)
rem   --host, --join, --bot, --lan-list, --headless, --screenshot and --map are options of one game: given without --players they make this a single game,
rem   so that `start_game.bat --host --name Alice` and `start_game.bat --join 192.168.1.20` still do what docs/COMMAND_LINE.md says. --bot cannot be combined with
rem   --players (the other windows are guests, and a guest runs no bots: a game against bots is one window; use --host --bot and let others join it)
rem
rem Environment: ANTS_PORT (the room's TCP port, default 4001).
rem
rem The windows do not grab the pointer: the game's own cursor shows only inside a window, the edge of the screen scrolls only the window the pointer is in,
rem and only the window that has the focus makes sound (--audio-focus), so four games on one machine can be played one after the other.
setlocal enabledelayedexpansion

cd /d "%~dp0"

set "PLAYERS=1"
set "GIVEN=0"
set "DRYRUN=0"
set "PASS="
:parse
if "%~1"=="" goto parsed
if /i "%~1"=="--players" (
    set "PLAYERS=%~2"
    set "GIVEN=1"
    shift
    shift
    goto parse
)
if /i "%~1"=="--single" (
    set "PLAYERS=1"
    set "GIVEN=1"
    shift
    goto parse
)
if /i "%~1"=="--dry-run" (
    set "DRYRUN=1"
    shift
    goto parse
)
set PASS=!PASS! %1
shift
goto parse
:parsed
if "%GIVEN%"=="0" (
    for %%A in (!PASS!) do (
        for %%O in (--host --join --bot --lan-list --headless --screenshot --map) do if /i "%%~A"=="%%O" set "PLAYERS=1"
    )
)
if not "%PLAYERS%"=="1" if not "%PLAYERS%"=="2" if not "%PLAYERS%"=="3" if not "%PLAYERS%"=="4" (
    echo [LAUNCHER] --players takes a number from 1 to 4.
    exit /b 2
)
if not "%PLAYERS%"=="1" (
    for %%A in (!PASS!) do (
        if /i "%%~A"=="--bot" (
            echo [LAUNCHER] --bot cannot be combined with --players: the extra windows are guests and a guest runs no bots. Use start_game.bat --bot 1:medium for a game against a bot, or --host --bot 2:medium to let others join.
            exit /b 2
        )
    )
)

set "PORT=%ANTS_PORT%"
if "%PORT%"=="" set "PORT=4001"
set "BIN=build\src\ants_app\Release\ants.exe"

if "%DRYRUN%"=="1" goto plan

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
echo  START MENU (start_game.bat; the test rig of start_game.bat --players 4 starts in its room):
echo    * Single player (with computer players per seat), Join with a code, Host an online match, Quit.
echo    * Up / Down and Enter, or the mouse; Esc goes back (on the first panel: quits).
echo.
echo  SETUP SCREEN:
echo    * Up / Down pick a map; click START (or press Enter or S) to begin; Q or X (or the Leave button) leaves; Esc does nothing here.
echo    * Fog of War is the On / Off pair of buttons (mouse). INTRO music plays here; a match plays a random in-game track (Ctrl+M mutes).
echo.
echo  MOUSE (the original's pointer model):
echo    * Left click an ant: select it. Shift + click adds an ant of yours to the selection or takes it out again.
echo    * Drag over 4 px: a red rubber band selects your ants (with Shift it adds them).
echo    * With ants selected, a left click acts by the cursor: ground = move, food = harvest, another player's ant = attack.
echo      One selected ant also has its ability: bomb (plant / defuse), fire wall, bridge (dig), raid.
echo    * Right click: the same order without the cursor check (a move for several ants, workers and combat ants).
echo    * Several ants - also one ant that was added with Shift - are a group: a click is always a move. A group of
echo      bombers therefore walks onto a bomb of its own team and sets it off (how to get off the island).
echo    * The Move and ability pedestals latch (Stop stops and deselects). Hold left on the minimap to scroll.
echo.
echo  CAMERA:
echo    * Move the pointer to the screen edge to scroll (the wheel zooms; no arrow or letter key scrolls, only Ctrl+N / Ctrl+P bring an ant into view).
echo.
echo  KEYS (the original's keyboard; the chat box is always active):
echo    * F1 help, F9 - F12 quick chat, Enter sends chat, Esc deselects.
echo    * Ctrl+A select all, Ctrl+H home hill, Ctrl+N / Ctrl+P next / previous ant, Ctrl+S stop,
echo      Ctrl+O options, Ctrl+Q quit, Ctrl+L hit point digits.
echo    * Developer shortcuts: Ctrl+T tile grid, Ctrl+M mute music, Ctrl+1..4 switch team, Shift+F12 screenshot.
echo ----------------------------------------------------------------------
echo.

:plan
if "%PLAYERS%"=="1" (
    if "%DRYRUN%"=="1" (
        echo %BIN%!PASS!
        exit /b 0
    )
    echo [LAUNCHER] Starting Ants...
    echo.
    "%BIN%"!PASS!
    exit /b !errorlevel!
)

rem four different names, drawn at random
set "NAMES=Antonio Buzz Clover Dot Ember Flick Granite Hazel Inky Juniper Kip Lumen Mortimer Nettle Oakley Pebble Quill Rusty Sprig Tango Umber Velvet Wren Zest"
set /a COUNT=0
for %%N in (%NAMES%) do (
    set /a COUNT+=1
    set "NAME!COUNT!=%%N"
)
set /a PICKED=0
:pick
if !PICKED! geq 4 goto picked
set /a IDX=!RANDOM! %% !COUNT! + 1
if defined USED!IDX! goto pick
set "USED!IDX!=1"
for %%I in (!IDX!) do set "PICK!PICKED!=!NAME%%I!"
set /a PICKED+=1
goto pick
:picked

set "COLOUR0=Green"
set "COLOUR1=Red"
set "COLOUR2=Blue"
set "COLOUR3=Black"
set "GRID=2x2"
if "%PLAYERS%"=="2" set "GRID=2x1"

rem The cell of window N (= seat N) in the grid; --cell counts row by row, 0 = top left (1 top right, 2 bottom left, 3 bottom right of the 2 x 2 grid).
rem Four windows lie by colour, the way the four hills lie on the Small and Treasure maps (the same as the games of web/lobby.html):
rem black (seat 3) top left, green (seat 0) top right, red (seat 1) bottom left, blue (seat 2) bottom right.
rem (Other maps put the hills elsewhere; the layout is the same for every map.)
rem Fewer windows keep that order of the colours (black, green, red, blue) without holes, as on the page: two are green left, red right (2 x 1),
rem three are green, red, blue in cells 0, 1, 2 (nobody is black, so the top left cell goes to green).
set "CELL0=0"
set "CELL1=1"
set "CELL2=2"
set "CELL3=3"
if "%PLAYERS%"=="4" (
    set "CELL0=1"
    set "CELL1=2"
    set "CELL2=3"
    set "CELL3=0"
)

if "%DRYRUN%"=="1" (
    for /l %%N in (0,1,%PLAYERS%) do if %%N lss %PLAYERS% call :window %%N echo
    exit /b 0
)

echo [LAUNCHER] %PLAYERS% players in one match on this machine (room on port %PORT%):
for /l %%N in (0,1,%PLAYERS%) do if %%N lss %PLAYERS% echo    window %%N: !COLOUR%%N!, !PICK%%N!
echo    (window 0 is the host: choose the map and press START there)
echo.

call :window 0 start
rem the guests connect when the room is open: the host needs a moment to make its window and listen
ping -n 3 127.0.0.1 >nul
for /l %%N in (1,1,%PLAYERS%) do if %%N lss %PLAYERS% call :window %%N start
exit /b 0

rem :window N echo|start - the command line of window N: window 0 opens the room on this machine only, the others join it and ask for their own colour
:window
set "WN=%~1"
set "WNAME=!PICK%WN%!"
set "WCOLOUR=!COLOUR%WN%!"
set "WCELL=!CELL%WN%!"
set "WARGS=--name !WNAME! --title "Ants - !WCOLOUR! (!WNAME!)" --grid %GRID% --cell !WCELL! --audio-focus --no-lan"
if "%WN%"=="0" (
    set "WARGS=!WARGS! --host %PORT% --loopback"
) else (
    set "WARGS=!WARGS! --join 127.0.0.1:%PORT% --seat %WN%"
)
set "WARGS=!WARGS!!PASS!"
if "%~2"=="echo" (
    echo %BIN% !WARGS!
) else (
    start "Ants - !WCOLOUR!" "%BIN%" !WARGS!
)
exit /b 0
