@echo off
rem F5 in Tiled: builds the game and starts the level you have open
setlocal
cd /d "%~dp0.."
call tools\setup_windows.bat
if errorlevel 1 exit /b 1
.tools\python\python.exe tools\build.py "%~n1"
