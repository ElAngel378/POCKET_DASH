@echo off
rem Makes a new level:  new_level mylevel "My Level"
setlocal
cd /d "%~dp0"
call tools\setup_windows.bat
if errorlevel 1 goto end
if "%~1"=="" (
    set /p "LEVEL=Name for the file (letters, numbers and _): "
    set /p "TITLE=Title shown in the game: "
    call .tools\python\python.exe tools\new_level.py "%%LEVEL%%" "%%TITLE%%"
) else (
    .tools\python\python.exe tools\new_level.py %*
)
:end
if not defined POCKETDASH_NO_PAUSE pause
