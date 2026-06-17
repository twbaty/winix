@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

:: ==========================================================
:: Winix Uninstaller - Run as Administrator
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
echo.
echo  IMPORTANT: Close all Winix windows before continuing.
echo  If winix.exe is running, Windows will lock the file and
echo  it cannot be deleted until you reboot.
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
echo [1/6] Removing "Open Winix here" context menu...
reg delete "HKLM\SOFTWARE\Classes\Directory\shell\OpenWinixHere"             /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Classes\Directory\Background\shell\OpenWinixHere" /f >nul 2>&1
echo [CONTEXT] Done.

:: ==========================================================
:: 2. Remove .sh file association
:: ==========================================================
echo [2/6] Removing .sh file association...
reg delete "HKLM\SOFTWARE\Classes\.sh"         /f >nul 2>&1
reg delete "HKLM\SOFTWARE\Classes\WinixScript" /f >nul 2>&1
echo [SH] Done.

:: ==========================================================
:: 3. Remove Start Menu shortcut
:: ==========================================================
echo [3/6] Removing Start Menu shortcut...
set SM_DIR=%ProgramData%\Microsoft\Windows\Start Menu\Programs\Winix
if exist "!SM_DIR!" (
    rmdir /s /q "!SM_DIR!"
    echo [SHORTCUT] Removed.
) else (
    echo [SHORTCUT] Not found, skipping.
)

:: ==========================================================
:: 4. Remove C:\Winix and C:\Winix\bin from system PATH
:: ==========================================================
echo [4/6] Removing %INSTALL_PREFIX% and %INSTALL_PREFIX%\bin from system PATH...
powershell -NoProfile -Command ^
    "$p = [Environment]::GetEnvironmentVariable('Path','Machine');" ^
    "$parts = $p -split ';' | Where-Object { $_ -ne '%INSTALL_PREFIX%' -and $_ -ne '%INSTALL_PREFIX%\bin' -and $_ -ne '' };" ^
    "$newp = $parts -join ';';" ^
    "if ($newp -ne $p) {" ^
    "    [Environment]::SetEnvironmentVariable('Path', $newp, 'Machine');" ^
    "    Write-Host '[PATH] Removed Winix entries from system PATH.';" ^
    "} else {" ^
    "    Write-Host '[PATH] Entries not found in PATH, skipping.';" ^
    "}"

:: ==========================================================
:: 5. Remove Apps & Features entry and Windows Terminal profile
:: ==========================================================
echo [5/6] Removing Apps ^& Features entry and Windows Terminal profile...

reg delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Winix" /f >nul 2>&1
echo [ARP] Done.

set WT_GUID={b5e89994-a1b8-4e6f-8e37-5e3e4e4e4e4e}
powershell -NoProfile -NonInteractive -Command ^
    "$guid = '%WT_GUID%';" ^
    "$wtPaths = @(" ^
    "  \"$env:LOCALAPPDATA\Packages\Microsoft.WindowsTerminal_8wekyb3d8bbwe\LocalState\settings.json\"," ^
    "  \"$env:LOCALAPPDATA\Packages\Microsoft.WindowsTerminalPreview_8wekyb3d8bbwe\LocalState\settings.json\"," ^
    "  \"$env:LOCALAPPDATA\Microsoft\Windows Terminal\settings.json\"" ^
    ");" ^
    "$wtPath = $null; $wtJson = $null;" ^
    "foreach ($p in $wtPaths) { if (Test-Path $p) { $wtPath = $p; $wtJson = Get-Content $p -Raw | ConvertFrom-Json; break } };" ^
    "if ($wtPath -eq $null) { Write-Host '[WT] Windows Terminal not found.'; exit 0 };" ^
    "$before = $wtJson.profiles.list.Count;" ^
    "$wtJson.profiles.list = @($wtJson.profiles.list | Where-Object { $_.guid -ne $guid });" ^
    "if ($wtJson.defaultProfile -eq $guid) { $wtJson.defaultProfile = '' };" ^
    "if ($wtJson.profiles.list.Count -lt $before) {" ^
    "  $wtJson | ConvertTo-Json -Depth 20 | Set-Content $wtPath -Encoding UTF8;" ^
    "  Write-Host '[WT] Winix profile removed.';" ^
    "} else { Write-Host '[WT] Winix profile not found.' }"
echo.

:: ==========================================================
:: 6. Delete C:\Winix directory
:: ==========================================================
echo [6/6] Removing %INSTALL_PREFIX%...
if exist "%INSTALL_PREFIX%" (
    rmdir /s /q "%INSTALL_PREFIX%"
)

:: Check if anything remains (locked files left behind)
if exist "%INSTALL_PREFIX%" (
    echo [FILES] Some files are locked by a running process.
    echo [FILES] Scheduling remaining files for deletion on next reboot...
    powershell -NoProfile -Command ^
        "Add-Type -Name MFE -Namespace Win32 -MemberDefinition '[DllImport(\"kernel32.dll\", SetLastError=true, CharSet=CharSet.Unicode)] public static extern bool MoveFileEx(string e, string n, uint f);';" ^
        "$DELAY = 4;" ^
        "Get-ChildItem -Path '%INSTALL_PREFIX%' -Recurse -Force | Sort-Object FullName -Descending | ForEach-Object {" ^
        "    [Win32.MFE]::MoveFileEx($_.FullName, $null, $DELAY) | Out-Null;" ^
        "    Write-Host ('[REBOOT-DELETE] ' + $_.FullName);" ^
        "};" ^
        "[Win32.MFE]::MoveFileEx('%INSTALL_PREFIX%', $null, $DELAY) | Out-Null;" ^
        "Write-Host '[REBOOT-DELETE] %INSTALL_PREFIX%';"
    echo.
    echo [FILES] A reboot is required to complete removal of winix.exe.
) else (
    echo [FILES] %INSTALL_PREFIX% removed.
)

:: ==========================================================
:: Done
:: ==========================================================
echo.
echo =========================================================
echo  Winix has been removed from your system.
echo =========================================================
echo.
endlocal
exit /b 0
