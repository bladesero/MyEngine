@echo off
REM build_vulkan.bat — Generate VS 2022 solution with the Vulkan backend enabled.
REM
REM Usage:
REM   build_vulkan.bat                  — debug + release
REM   build_vulkan.bat debug            — debug only
REM   build_vulkan.bat release          — release only
REM
REM After generation, open vs2022\vsxmake2022\MyEngine.sln in Visual Studio
REM and run with --backend vulkan (e.g. set as command arg in VS debug settings).

setlocal enabledelayedexpansion

set "MODES=debug,release"

if not "%~1"=="" (
    set "MODES=%~1"
)

echo ==^> Configuring xmake (%MODES%) with Vulkan ...
call xmake f -m %MODES% --vulkan=y
if %ERRORLEVEL% neq 0 (
    echo [ERROR] xmake configure failed.
    exit /b %ERRORLEVEL%
)

echo ==^> Generating VS 2022 solution ...
call xmake project -k vsxmake2022 -m "%MODES%" -y vs2022
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Solution generation failed.
    exit /b %ERRORLEVEL%
)

echo.
echo === Solution generated (Vulkan enabled, modes: %MODES%) ===
echo.
echo Open: vs2022\vsxmake2022\MyEngine.sln
echo.
