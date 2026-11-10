@echo off
rem Updates a level made before colour triggers fired where they are placed.
rem Drop the .tmx on this file, double-click it, or: fix_triggers mylevel
setlocal
cd /d "%~dp0"
call tools\setup_windows.bat
if errorlevel 1 goto end
if "%~1"=="" (
    set /p "LEVEL=Level to fix (file name, or the path to its .tmx): "
    call .tools\python\python.exe tools\fix_trigger_lead.py "%%LEVEL%%"
) else (
    .tools\python\python.exe tools\fix_trigger_lead.py %*
)
:end
if not defined POCKETDASH_NO_PAUSE pause
