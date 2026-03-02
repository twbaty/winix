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
:: 1. cmake --install
:: ==========================================================
echo [1/5] Installing Winix binaries to %INSTALL_PREFIX%\bin ...
cmake --install build --prefix "%INSTALL_PREFIX%"
if errorlevel 1 (
    echo [FAILED] cmake install failed.
    pause
    exit /b 1
)
echo.

:: ==========================================================
:: 2. Add C:\Winix\bin to the system PATH
:: ==========================================================
echo [2/5] Checking system PATH...
powershell -NoProfile -Command ^
    "$p = [Environment]::GetEnvironmentVariable('Path','Machine');" ^
    "if ($p -notlike '*%INSTALL_PREFIX%\bin*') {" ^
    "    [Environment]::SetEnvironmentVariable('Path', $p + ';%INSTALL_PREFIX%\bin', 'Machine');" ^
    "    Write-Host '[PATH] Added %INSTALL_PREFIX%\bin to system PATH.';" ^
    "} else {" ^
    "    Write-Host '[PATH] %INSTALL_PREFIX%\bin already in system PATH.';" ^
    "}"
echo.

:: ==========================================================
:: 3. Start Menu shortcut
::    Lets the user pin Winix to the Taskbar via right-click.
:: ==========================================================
echo [3/5] Creating Start Menu shortcut...
powershell -NoProfile -Command ^
    "$ws  = New-Object -ComObject WScript.Shell;" ^
    "$dir = '%ProgramData%\Microsoft\Windows\Start Menu\Programs\Winix';" ^
    "New-Item -ItemType Directory -Force $dir | Out-Null;" ^
    "$lnk = $ws.CreateShortcut($dir + '\Winix.lnk');" ^
    "$lnk.TargetPath      = '%INSTALL_PREFIX%\bin\winix.exe';" ^
    "$lnk.WorkingDirectory= '%USERPROFILE%';" ^
    "$lnk.Description     = 'Winix Unix Shell for Windows';" ^
    "$lnk.Save();" ^
    "Write-Host '[SHORTCUT] Start Menu shortcut created — right-click it to Pin to Taskbar.'"
echo.

:: ==========================================================
:: 4. "Open Winix here" Explorer context menu
::    Right-click a folder  → "Open Winix here"
::    Right-click folder BG → "Open Winix here"
:: ==========================================================
echo [4/5] Registering "Open Winix here" context menu...

:: On a folder icon
reg add "HKLM\SOFTWARE\Classes\Directory\shell\OpenWinixHere"          /ve /d "Open Winix here"                                                    /f >nul
reg add "HKLM\SOFTWARE\Classes\Directory\shell\OpenWinixHere"          /v Icon /t REG_SZ /d "%INSTALL_PREFIX%\bin\winix.exe"                       /f >nul
reg add "HKLM\SOFTWARE\Classes\Directory\shell\OpenWinixHere\command"  /ve /d "\"%INSTALL_PREFIX%\bin\winix.exe\" -C \"%%1\""                      /f >nul

:: In the background of an open folder window
reg add "HKLM\SOFTWARE\Classes\Directory\Background\shell\OpenWinixHere"          /ve /d "Open Winix here"                                         /f >nul
reg add "HKLM\SOFTWARE\Classes\Directory\Background\shell\OpenWinixHere"          /v Icon /t REG_SZ /d "%INSTALL_PREFIX%\bin\winix.exe"            /f >nul
reg add "HKLM\SOFTWARE\Classes\Directory\Background\shell\OpenWinixHere\command"  /ve /d "\"%INSTALL_PREFIX%\bin\winix.exe\" -C \"%%V\""            /f >nul

echo [CONTEXT] Done.
echo.

:: ==========================================================
:: 5. Windows Terminal profile (optional — skipped if WT not installed)
:: ==========================================================
echo [5/5] Adding Windows Terminal profile...
powershell -NoProfile -Command ^
    "$wtPaths = @(" ^
    "  \"$env:LOCALAPPDATA\Packages\Microsoft.WindowsTerminal_8wekyb3d8bbwe\LocalState\settings.json\"," ^
    "  \"$env:LOCALAPPDATA\Packages\Microsoft.WindowsTerminalPreview_8wekyb3d8bbwe\LocalState\settings.json\"," ^
    "  \"$env:LOCALAPPDATA\Microsoft\Windows Terminal\settings.json\"" ^
    ");" ^
    "$wtPath = $null; $wtJson = $null;" ^
    "foreach ($p in $wtPaths) { if (Test-Path $p) { $wtPath = $p; $wtJson = Get-Content $p -Raw | ConvertFrom-Json; break } };" ^
    "if ($wtPath -eq $null) { Write-Host '[WT] Windows Terminal not found — skipping.'; exit 0 };" ^
    "$guid = '{b5e89994-a1b8-4e6f-8e37-5e3e4e4e4e4e}';" ^
    "$exists = $wtJson.profiles.list | Where-Object { $_.guid -eq $guid };" ^
    "if ($exists) { Write-Host '[WT] Winix profile already present.'; exit 0 };" ^
    "$prof = [PSCustomObject]@{" ^
    "  guid             = $guid;" ^
    "  name             = 'Winix';" ^
    "  commandline      = 'C:\Winix\bin\winix.exe';" ^
    "  startingDirectory= '%USERPROFILE%';" ^
    "  icon             = 'C:\Winix\bin\winix.exe';" ^
    "  colorScheme      = 'One Half Dark';" ^
    "  hidden           = $false" ^
    "};" ^
    "$wtJson.profiles.list += $prof;" ^
    "$wtJson | ConvertTo-Json -Depth 20 | Set-Content $wtPath -Encoding UTF8;" ^
    "Write-Host '[WT] Winix profile added. Restart Windows Terminal to see it.'"
echo.

:: ==========================================================
:: Done
:: ==========================================================
echo =========================================================
echo  Winix installed successfully!
echo.
echo  * Binaries : %INSTALL_PREFIX%\bin
echo  * To launch : type  winix  in any terminal
echo  * Start Menu: Search "Winix" — right-click to Pin to Taskbar
echo  * Explorer  : Right-click any folder → "Open Winix here"
echo  * To remove : run uninstall.bat as Administrator
echo =========================================================
echo.
endlocal
exit /b 0
