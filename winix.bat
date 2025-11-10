@echo off
:: Launch Winix shell from build output

set EXE_PATH=%~dp0build\winix.exe

if not exist "%EXE_PATH%" (
    echo [ERROR] Winix executable not found at "%EXE_PATH%"
    echo Run build.bat first.
    exit /b 1
)

echo [OK] Starting Winix shell...
"%EXE_PATH%"
