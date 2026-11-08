@echo off
rem Puts a private copy of Python and Pillow in .tools\python, the first time only.
rem Nothing is installed on the computer itself: delete the .tools folder to remove it all.
setlocal
set "HERE=%~dp0.."
set "PY=%HERE%\.tools\python"
if exist "%PY%\python.exe" if exist "%PY%\Lib\site-packages\PIL" exit /b 0

echo Setting up Python for Pocket Dash. This only happens once...
if not exist "%PY%" mkdir "%PY%"
set "VER=3.12.10"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; [Net.ServicePointManager]::SecurityProtocol='Tls12'; $ProgressPreference='SilentlyContinue'; Invoke-WebRequest 'https://www.python.org/ftp/python/%VER%/python-%VER%-embed-amd64.zip' -OutFile '%PY%\python.zip'; Expand-Archive -Force '%PY%\python.zip' '%PY%'; Remove-Item '%PY%\python.zip'; $p=Join-Path '%PY%' 'python312._pth'; (Get-Content $p) -replace '#import site','import site' | Set-Content $p; Add-Content $p 'Lib\site-packages'; Add-Content $p '..\..\tools'; New-Item -ItemType Directory -Force '%PY%\Lib\site-packages' | Out-Null; Invoke-WebRequest 'https://bootstrap.pypa.io/get-pip.py' -OutFile '%PY%\get-pip.py'"
if errorlevel 1 goto failed
"%PY%\python.exe" "%PY%\get-pip.py" --quiet --no-warn-script-location
if errorlevel 1 goto failed
"%PY%\python.exe" -m pip install --quiet --no-warn-script-location pillow
if errorlevel 1 goto failed
del "%PY%\get-pip.py"
exit /b 0

:failed
echo.
echo Couldn't download Python. Check your internet connection, then run this again.
echo If it keeps failing, delete the .tools folder and try once more.
exit /b 1
