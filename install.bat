@echo off
setlocal enabledelayedexpansion

:: ==========================================================
:: Winix Installer — Run as Administrator
:: Installs all binaries to C:\Winix\bin and adds to PATH
:: ==========================================================

:: --- Admin check ---
net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] This script must be run as Administrator.
    echo        Right-click install.bat and choose "Run as administrator".
    pause
    exit /b 1
)

set INSTALL_PREFIX=C:\Winix

:: --- Build check ---
if not exist "build\winix.exe" (
    echo [ERROR] Build output not found. Run build.bat first.
    pause
    exit /b 1
)

:: ==========================================================
:: cmake --install
:: ==========================================================
echo [INSTALL] Installing Winix to %INSTALL_PREFIX%\bin ...
cmake --install build --prefix "%INSTALL_PREFIX%"
if errorlevel 1 (
    echo [FAILED] cmake install failed.
    pause
    exit /b 1
)
echo.

:: ==========================================================
:: Add C:\Winix\bin to the system PATH (Machine scope)
:: Uses PowerShell to avoid the 1024-char setx limit
:: ==========================================================
echo [PATH] Checking system PATH...

powershell -NoProfile -Command ^
    "$p = [Environment]::GetEnvironmentVariable('Path','Machine');" ^
    "if ($p -notlike '*%INSTALL_PREFIX%\bin*') {" ^
    "    [Environment]::SetEnvironmentVariable('Path', $p + ';%INSTALL_PREFIX%\bin', 'Machine');" ^
    "    Write-Host '[PATH] Added %INSTALL_PREFIX%\bin to system PATH.';" ^
    "} else {" ^
    "    Write-Host '[PATH] %INSTALL_PREFIX%\bin is already in system PATH.';" ^
    "}"

if errorlevel 1 (
    echo [WARNING] Could not update system PATH automatically.
    echo          Add %INSTALL_PREFIX%\bin to your PATH manually.
)

:: ==========================================================
:: Done
:: ==========================================================
echo.
echo [SUCCESS] Winix installed to %INSTALL_PREFIX%\bin
echo           Open a new terminal and type: winix
echo.
endlocal
exit /b 0
