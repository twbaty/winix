@echo off
setlocal enabledelayedexpansion

:: ==========================================================
:: Winix Installer — Run as Administrator
:: Installs winix.exe to C:\Winix\ and coreutils to C:\Winix\bin\
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

:: ==========================================================
:: 1. Copy files — detect zip layout vs dev build layout
:: ==========================================================
echo [1/5] Installing Winix to %INSTALL_PREFIX% ...

if exist "winix.exe" (
    :: ---- ZIP / pre-built layout: winix.exe is right here ----
    if not exist "%INSTALL_PREFIX%" mkdir "%INSTALL_PREFIX%"
    if not exist "%INSTALL_PREFIX%\bin" mkdir "%INSTALL_PREFIX%\bin"
    copy /y "winix.exe" "%INSTALL_PREFIX%\winix.exe" >nul
    if exist "bin\" (
        xcopy /y /q "bin\*" "%INSTALL_PREFIX%\bin\" >nul
    )
    echo [FILES] Copied from zip layout.
) else if exist "build\winix.exe" (
    :: ---- Developer build: use cmake --install ----
    cmake --install build --prefix "%INSTALL_PREFIX%"
    if errorlevel 1 (
        echo [FAILED] cmake install failed.
        pause
        exit /b 1
    )
    echo [FILES] Installed from build directory.
) else (
    echo [ERROR] Cannot find winix.exe — download the zip from:
    echo         https://github.com/twbaty/winix/releases/latest
    pause
    exit /b 1
)
echo.

:: ==========================================================
:: 2. Add C:\Winix and C:\Winix\bin to the system PATH
:: ==========================================================
echo [2/5] Checking system PATH...
powershell -NoProfile -Command ^
    "$p = [Environment]::GetEnvironmentVariable('Path','Machine');" ^
    "$changed = $false;" ^
    "if ($p -notlike '*%INSTALL_PREFIX%;*' -and $p -notlike '*%INSTALL_PREFIX%') {" ^
    "    $p = $p + ';%INSTALL_PREFIX%';" ^
    "    $changed = $true;" ^
    "    Write-Host '[PATH] Added %INSTALL_PREFIX% to system PATH.';" ^
    "} else { Write-Host '[PATH] %INSTALL_PREFIX% already in system PATH.'; };" ^
    "if ($p -notlike '*%INSTALL_PREFIX%\bin*') {" ^
    "    $p = $p + ';%INSTALL_PREFIX%\bin';" ^
    "    $changed = $true;" ^
    "    Write-Host '[PATH] Added %INSTALL_PREFIX%\bin to system PATH.';" ^
    "} else { Write-Host '[PATH] %INSTALL_PREFIX%\bin already in system PATH.'; };" ^
    "if ($changed) { [Environment]::SetEnvironmentVariable('Path', $p, 'Machine'); }"
echo.

:: ==========================================================
:: 3. Start Menu shortcut
:: ==========================================================
echo [3/5] Creating Start Menu shortcut...
powershell -NoProfile -Command ^
    "$ws  = New-Object -ComObject WScript.Shell;" ^
    "$dir = '%ProgramData%\Microsoft\Windows\Start Menu\Programs\Winix';" ^
    "New-Item -ItemType Directory -Force $dir | Out-Null;" ^
    "$lnk = $ws.CreateShortcut($dir + '\Winix.lnk');" ^
    "$lnk.TargetPath      = '%INSTALL_PREFIX%\winix.exe';" ^
    "$lnk.WorkingDirectory= '%USERPROFILE%';" ^
    "$lnk.Description     = 'Winix Unix Shell for Windows';" ^
    "$lnk.Save();" ^
    "Write-Host '[SHORTCUT] Start Menu shortcut created — right-click it to Pin to Taskbar.'"
echo.

:: ==========================================================
:: 4. "Open Winix here" Explorer context menu
:: ==========================================================
echo [4/5] Registering "Open Winix here" context menu...

reg add "HKLM\SOFTWARE\Classes\Directory\shell\OpenWinixHere"          /ve /d "Open Winix here"                                                    /f >nul
reg add "HKLM\SOFTWARE\Classes\Directory\shell\OpenWinixHere"          /v Icon /t REG_SZ /d "%INSTALL_PREFIX%\winix.exe"                           /f >nul
reg add "HKLM\SOFTWARE\Classes\Directory\shell\OpenWinixHere\command"  /ve /d "\"%INSTALL_PREFIX%\winix.exe\" -C \"%%1\""                          /f >nul

reg add "HKLM\SOFTWARE\Classes\Directory\Background\shell\OpenWinixHere"          /ve /d "Open Winix here"                                         /f >nul
reg add "HKLM\SOFTWARE\Classes\Directory\Background\shell\OpenWinixHere"          /v Icon /t REG_SZ /d "%INSTALL_PREFIX%\winix.exe"                /f >nul
reg add "HKLM\SOFTWARE\Classes\Directory\Background\shell\OpenWinixHere\command"  /ve /d "\"%INSTALL_PREFIX%\winix.exe\" -C \"%%V\""               /f >nul

echo [CONTEXT] Done.
echo.

:: ==========================================================
:: 5. .sh file association
:: ==========================================================
echo [5/6] Registering .sh file association...
reg add "HKLM\SOFTWARE\Classes\.sh"                               /ve /d "WinixScript"                                          /f >nul
reg add "HKLM\SOFTWARE\Classes\WinixScript"                       /ve /d "Winix Shell Script"                                   /f >nul
reg add "HKLM\SOFTWARE\Classes\WinixScript\DefaultIcon"           /ve /d "%INSTALL_PREFIX%\winix.exe,0"                         /f >nul
reg add "HKLM\SOFTWARE\Classes\WinixScript\shell\open\command"    /ve /d "\"%INSTALL_PREFIX%\winix.exe\" \"%%1\""               /f >nul
echo [SH] Done. Double-clicking .sh files will now run them with Winix.
echo.

:: ==========================================================
:: 6. Windows Terminal profile
:: ==========================================================
echo [6/6] Adding Windows Terminal profile...
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
    "  commandline      = 'C:\Winix\winix.exe';" ^
    "  startingDirectory= '%USERPROFILE%';" ^
    "  icon             = 'C:\Winix\winix.exe';" ^
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
echo  * Shell     : %INSTALL_PREFIX%\winix.exe
echo  * Coreutils : %INSTALL_PREFIX%\bin
echo  * To launch : type  winix  in any terminal
echo  * Start Menu: Search "Winix" — right-click to Pin to Taskbar
echo  * Explorer  : Right-click any folder -> "Open Winix here"
echo  * .sh files : double-click to execute with Winix
echo  * To remove : run uninstall.bat as Administrator
echo =========================================================
echo.
endlocal
exit /b 0
