@echo off
rem F5 in Tiled: builds the game and starts the level you have open
setlocal
cd /d "%~dp0.."
call tools\setup_windows.bat
if errorlevel 1 exit /b 1
rem Tiled gives the path with forward slashes: batch files need backslashes to get the file name
set "MAP=%~1"
set "MAP=%MAP:/=\%"
for %%I in ("%MAP%") do set "LEVEL=%%~nI"
.tools\python\python.exe tools\build.py "%LEVEL%"
