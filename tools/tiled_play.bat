@echo off
rem F5 in Tiled: builds the game and starts the level you have open.
rem Tiled runs commands with no window, so this opens one to show that the build is happening.
setlocal
cd /d "%~dp0.."
if defined POCKETDASH_F5_LEVEL goto build

rem Tiled gives the path with forward slashes: batch files need backslashes to get the file name
set "MAP=%~1"
set "MAP=%MAP:/=\%"
for %%I in ("%MAP%") do set "POCKETDASH_F5_LEVEL=%%~nI"
start "Building test ROM..." cmd /c "%~f0"
exit /b 0

:build
echo Building test ROM for %POCKETDASH_F5_LEVEL%...
echo This window closes by itself when the game opens.
echo.
call tools\setup_windows.bat
if errorlevel 1 goto stopped
.tools\python\python.exe tools\build.py "%POCKETDASH_F5_LEVEL%"
rem 2 means another build is already running: nothing to read, so don't wait for a key
if errorlevel 2 goto already_running
if errorlevel 1 goto stopped
exit /b 0

:already_running
timeout /t 3 >nul
exit /b 0

:stopped
title Pocket Dash build stopped
echo.
pause
