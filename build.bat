@echo off
rem Double-click to build Pocket Dash. Or: build mylevel    builds and starts that level.
setlocal
cd /d "%~dp0"
call tools\setup_windows.bat
if errorlevel 1 goto end
.tools\python\python.exe tools\build.py %*
:end
if not defined POCKETDASH_NO_PAUSE pause
