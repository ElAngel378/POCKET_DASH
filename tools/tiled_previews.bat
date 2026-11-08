@echo off
rem F6 in Tiled: redraws the open level's sky and ground from its colours and colour triggers
setlocal
cd /d "%~dp0.."
call tools\setup_windows.bat
if errorlevel 1 exit /b 1
.tools\python\python.exe tools\level_previews.py "%~n1"
if errorlevel 1 exit /b 1
echo Done. If the picture didn't change, press Ctrl+R in Tiled to reload the level.
