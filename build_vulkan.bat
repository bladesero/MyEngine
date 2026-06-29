@echo off
setlocal

set "MODE=debug"
if not "%~1"=="" set "MODE=%~1"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\smoke.ps1" -Mode "%MODE%" -Vulkan
exit /b %ERRORLEVEL%
