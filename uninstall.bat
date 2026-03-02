@echo off
setlocal enabledelayedexpansion

:: ==========================================================
:: Winix Uninstaller — Run as Administrator
:: ==========================================================

:: --- Admin check ---
net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] This script must be run as Administrator.
    echo        Right-click uninstall.bat and choose "Run as administrator".
    pause
    exit /b 1
)

set INSTALL_PREFIX=C:\Winix

echo Winix Uninstaller
echo =================
echo This will remove Winix from your system.
echo.
set /p CONFIRM=Type YES to continue:
if /i not "!CONFIRM!"=="YES" (
    echo Aborted.
    exit /b 0
)
echo.

:: ==========================================================
:: 1. Remove "Open Winix here" context menu
:: ==========================================================
echo [1/4] Removing "Open Winix here" context menu...
reg delete "HKLM\SOFTWARE\Classes\Directory\shell\OpenWinixHere"             /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Classes\Directory\Background\shell\OpenWinixHere" /f >nul 2>&1
echo [CONTEXT] Done.

:: ==========================================================
:: 2. Remove Start Menu shortcut
:: ==========================================================
echo [2/4] Removing Start Menu shortcut...
set SM_DIR=%ProgramData%\Microsoft\Windows\Start Menu\Programs\Winix
if exist "!SM_DIR!" (
    rmdir /s /q "!SM_DIR!"
    echo [SHORTCUT] Removed.
) else (
    echo [SHORTCUT] Not found, skipping.
)

:: ==========================================================
:: 3. Remove C:\Winix\bin from system PATH
:: ==========================================================
echo [3/4] Removing %INSTALL_PREFIX%\bin from system PATH...
powershell -NoProfile -Command ^
    "$p = [Environment]::GetEnvironmentVariable('Path','Machine');" ^
    "$parts = $p -split ';' | Where-Object { $_ -ne '%INSTALL_PREFIX%\bin' -and $_ -ne '' };" ^
    "$newp = $parts -join ';';" ^
    "if ($newp -ne $p) {" ^
    "    [Environment]::SetEnvironmentVariable('Path', $newp, 'Machine');" ^
    "    Write-Host '[PATH] Removed %INSTALL_PREFIX%\bin from system PATH.';" ^
    "} else {" ^
    "    Write-Host '[PATH] Entry not found in PATH, skipping.';" ^
    "}"

:: ==========================================================
:: 4. Delete C:\Winix directory
:: ==========================================================
echo [4/4] Removing %INSTALL_PREFIX%...
if exist "%INSTALL_PREFIX%" (
    rmdir /s /q "%INSTALL_PREFIX%"
    echo [FILES] %INSTALL_PREFIX% removed.
) else (
    echo [FILES] %INSTALL_PREFIX% not found, skipping.
)

:: ==========================================================
:: Done
:: ==========================================================
echo.
echo =========================================================
echo  Winix has been removed from your system.
echo  Note: Windows Terminal profile (if added) must be
echo        removed manually from Windows Terminal Settings.
echo =========================================================
echo.
endlocal
exit /b 0
