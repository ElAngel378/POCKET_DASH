@echo off
rem F6 in Tiled: redraws the open level's sky and ground from its colours and colour triggers
setlocal
cd /d "%~dp0.."
call tools\setup_windows.bat
if errorlevel 1 exit /b 1
rem Tiled gives the path with forward slashes: batch files need backslashes to get the file name
set "MAP=%~1"
set "MAP=%MAP:/=\%"
for %%I in ("%MAP%") do set "LEVEL=%%~nI"
.tools\python\python.exe tools\level_previews.py "%LEVEL%"
if errorlevel 1 exit /b 1
echo Done. If the picture didn't change, press Ctrl+R in Tiled to reload the level.
