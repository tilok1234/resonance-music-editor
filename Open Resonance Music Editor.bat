@echo off
setlocal
set "PROJECT_ROOT=%~dp0"
set "EDITOR=%PROJECT_ROOT%bin\ResonanceMusicEditor.exe"

if not exist "%EDITOR%" (
    echo ResonanceMusicEditor.exe has not been built yet.
    echo Run scripts\build.ps1 from this project, then try again.
    pause
    exit /b 1
)

cd /d "%PROJECT_ROOT%"
start "" "%EDITOR%"
exit /b 0
