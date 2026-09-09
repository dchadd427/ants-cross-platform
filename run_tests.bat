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
echo                  ANTS ENGINE REMAKE - MASTER TEST RUNNER [PC]
echo ======================================================================

if not exist "build" (
    echo [BUILD] Configuring build directory...
    cmake -B build
)
echo [BUILD] Compiling libraries and test suites...
cmake --build build --config Release -j8
if errorlevel 1 exit /b %errorlevel%

if not exist "build_e2e" (
    echo [BUILD] Configuring build_e2e directory...
    cmake -S tests/e2e -B build_e2e
)
echo [BUILD] Compiling E2E test runner...
cmake --build build_e2e --config Release -j8
if errorlevel 1 exit /b %errorlevel%

echo.
echo ======================================================================
echo --- 1. RUNNING ASSET DECODER SUITES [libants-assets]
echo ======================================================================
build\tests\test_assets\Release\test_assets.exe
if errorlevel 1 exit /b %errorlevel%
build\tests\test_assets\Release\test_challenger_m1_1.exe
if errorlevel 1 exit /b %errorlevel%
build\tests\test_assets\Release\test_challenger_m1_2.exe
if errorlevel 1 exit /b %errorlevel%
build\tests\test_assets\Release\test_challenger_m1_it2.exe
if errorlevel 1 exit /b %errorlevel%
build\tests\test_assets\Release\test_challenger_m1_it2_2.exe
if errorlevel 1 exit /b %errorlevel%

echo.
echo ======================================================================
echo --- 2. RUNNING SIMULATION RULES SUITES [libants-sim]
echo ======================================================================
build\tests\test_sim\Release\test_sim_rules.exe
if errorlevel 1 exit /b %errorlevel%
build\tests\test_sim\Release\test_challenger_m2_1.exe
if errorlevel 1 exit /b %errorlevel%
build\tests\test_sim\Release\test_challenger_m2_2.exe
if errorlevel 1 exit /b %errorlevel%

echo.
echo ======================================================================
echo --- 3. RUNNING APPLICATION INTEGRATION SUITES [libants-app]
echo ======================================================================
build\tests\test_app\Release\test_app_integration.exe
if errorlevel 1 exit /b %errorlevel%

echo.
echo ======================================================================
echo --- 4. RUNNING OPAQUE-BOX E2E TEST SUITES [506 tests]
echo ======================================================================
build_e2e\Release\e2e_runner.exe --all
if errorlevel 1 exit /b %errorlevel%

echo.
echo ======================================================================
echo *** ALL PC TESTS COMPLETED SUCCESSFULLY! ***
echo ======================================================================
