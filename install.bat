@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

:: ==========================================================
:: Winix Installer - Run as Administrator
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
:: 1. Copy files - detect zip layout vs dev build layout
:: ==========================================================
echo [1/8] Installing Winix to %INSTALL_PREFIX% ...

if exist "winix.exe" (
    :: ---- ZIP / pre-built layout: winix.exe is right here ----
    if not exist "%INSTALL_PREFIX%" mkdir "%INSTALL_PREFIX%"
    if not exist "%INSTALL_PREFIX%\bin" mkdir "%INSTALL_PREFIX%\bin"
    copy /y "winix.exe" "%INSTALL_PREFIX%\winix.exe" >nul
    if exist "bin\" (
        xcopy /y /q /I "bin\*" "%INSTALL_PREFIX%\bin\" >nul
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
    echo [ERROR] Cannot find winix.exe - download the zip from:
    echo         https://github.com/twbaty/winix/releases/latest
    pause
    exit /b 1
)
echo.

:: ==========================================================
:: 2. Add C:\Winix and C:\Winix\bin to the system PATH
:: ==========================================================
echo [2/8] Checking system PATH...
powershell -NoProfile -NonInteractive -Command ^
    "$r='HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment';" ^
    "$p=(Get-ItemProperty $r).Path; $e=$p -split ';'; $c=$false;" ^
    "if($e -notcontains '%INSTALL_PREFIX%'){$p=$p.TrimEnd(';')+';%INSTALL_PREFIX%';$c=$true;Write-Host '[PATH] Added %INSTALL_PREFIX%'}else{Write-Host '[PATH] %INSTALL_PREFIX% already present'};" ^
    "if($e -notcontains '%INSTALL_PREFIX%\bin'){$p=$p.TrimEnd(';')+';%INSTALL_PREFIX%\bin';$c=$true;Write-Host '[PATH] Added %INSTALL_PREFIX%\bin'}else{Write-Host '[PATH] %INSTALL_PREFIX%\bin already present'};" ^
    "if($c){Set-ItemProperty $r -Name Path -Value $p;" ^
    "  try{$q=[char]34;$sig='[DllImport('+$q+'user32.dll'+$q+',CharSet=CharSet.Auto)] public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,UIntPtr w,string l,uint f,uint t,out UIntPtr r);';" ^
    "  $t=Add-Type -MemberDefinition $sig -Name WEB -Namespace WEB -PassThru -EA Stop;" ^
    "  $rv=[UIntPtr]::Zero;$null=$t::SendMessageTimeout([IntPtr]0xffff,0x1A,[UIntPtr]::Zero,'Environment',2,5000,[ref]$rv);Write-Host '[PATH] Change broadcast to open windows.'}catch{};" ^
    "  Write-Host '[PATH] Done. Open a new terminal to apply.';}"
if errorlevel 1 echo [WARN] PATH update reported an error. Run install.bat as Administrator.
echo.

:: ==========================================================
:: 3. Start Menu shortcut
:: ==========================================================
echo [3/8] Creating Start Menu shortcut...
powershell -NoProfile -NonInteractive -Command ^
    "$ws  = New-Object -ComObject WScript.Shell;" ^
    "$dir = '%ProgramData%\Microsoft\Windows\Start Menu\Programs\Winix';" ^
    "New-Item -ItemType Directory -Force $dir | Out-Null;" ^
    "$lnk = $ws.CreateShortcut($dir + '\Winix.lnk');" ^
    "$lnk.TargetPath      = '%INSTALL_PREFIX%\winix.exe';" ^
    "$lnk.WorkingDirectory= '%USERPROFILE%';" ^
    "$lnk.Description     = 'Winix Unix Shell for Windows';" ^
    "$lnk.Save();" ^
    "Write-Host '[SHORTCUT] Start Menu shortcut created - right-click it to Pin to Taskbar.'"
echo.

:: ==========================================================
:: 4. "Open Winix here" Explorer context menu
:: ==========================================================
echo [4/8] Registering "Open Winix here" context menu...

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
echo [5/8] Registering .sh file association...
reg add "HKLM\SOFTWARE\Classes\.sh"                               /ve /d "WinixScript"                                          /f >nul
reg add "HKLM\SOFTWARE\Classes\WinixScript"                       /ve /d "Winix Shell Script"                                   /f >nul
reg add "HKLM\SOFTWARE\Classes\WinixScript\DefaultIcon"           /ve /d "%INSTALL_PREFIX%\winix.exe,0"                         /f >nul
reg add "HKLM\SOFTWARE\Classes\WinixScript\shell\open\command"    /ve /d "\"%INSTALL_PREFIX%\winix.exe\" \"%%1\""               /f >nul
echo [SH] Done. Double-clicking .sh files will now run them with Winix.
echo.

:: ==========================================================
:: 6. Register with Add / Remove Programs (Apps & Features)
:: ==========================================================
echo [6/8] Registering with Apps ^& Features...

:: Read version from VERSION file if present, otherwise leave blank
set WINIX_VER=
if exist "%~dp0VERSION" (
    for /f "usebackq tokens=*" %%V in ("%~dp0VERSION") do (
        if "!WINIX_VER!"=="" set WINIX_VER=%%V
    )
)
if "!WINIX_VER!"=="" set WINIX_VER=current

reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Winix" /v "DisplayName"     /t REG_SZ    /d "Winix"                                              /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Winix" /v "DisplayVersion"  /t REG_SZ    /d "!WINIX_VER!"                                        /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Winix" /v "Publisher"       /t REG_SZ    /d "Tom Baty"                                           /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Winix" /v "InstallLocation" /t REG_SZ    /d "%INSTALL_PREFIX%"                                   /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Winix" /v "DisplayIcon"     /t REG_SZ    /d "%INSTALL_PREFIX%\winix.exe,0"                       /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Winix" /v "UninstallString" /t REG_SZ    /d "cmd.exe /c \"%INSTALL_PREFIX%\uninstall.bat\""      /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Winix" /v "URLInfoAbout"    /t REG_SZ    /d "https://github.com/twbaty/winix"                    /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Winix" /v "NoModify"        /t REG_DWORD /d 1                                                    /f >nul
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Winix" /v "NoRepair"        /t REG_DWORD /d 1                                                    /f >nul
echo [ARP] Winix !WINIX_VER! will appear in Apps ^& Features.
echo.

:: ==========================================================
:: 7. Windows Terminal profile
:: ==========================================================
echo [7/8] Adding Windows Terminal profile...
powershell -NoProfile -NonInteractive -Command ^
    "$wtPaths = @(" ^
    "  \"$env:LOCALAPPDATA\Packages\Microsoft.WindowsTerminal_8wekyb3d8bbwe\LocalState\settings.json\"," ^
    "  \"$env:LOCALAPPDATA\Packages\Microsoft.WindowsTerminalPreview_8wekyb3d8bbwe\LocalState\settings.json\"," ^
    "  \"$env:LOCALAPPDATA\Microsoft\Windows Terminal\settings.json\"" ^
    ");" ^
    "$wtPath = $null; $wtJson = $null;" ^
    "foreach ($p in $wtPaths) { if (Test-Path $p) { $wtPath = $p; $wtJson = Get-Content $p -Raw | ConvertFrom-Json; break } };" ^
    "if ($wtPath -eq $null) { Write-Host '[WT] Windows Terminal not found - skipping.'; exit 0 };" ^
    "$guid = '{b5e89994-a1b8-4e6f-8e37-5e3e4e4e4e4e}';" ^
    "$exists = $wtJson.profiles.list | Where-Object { $_.guid -eq $guid };" ^
    "if ($exists) { Write-Host '[WT] Winix profile already present.' } else {" ^
    "  $prof = [PSCustomObject]@{" ^
    "    guid             = $guid;" ^
    "    name             = 'Winix';" ^
    "    commandline      = 'C:\Winix\winix.exe';" ^
    "    startingDirectory= '%USERPROFILE%';" ^
    "    icon             = 'C:\Winix\winix.exe';" ^
    "    colorScheme      = 'One Half Dark';" ^
    "    hidden           = $false" ^
    "  };" ^
    "  $wtJson.profiles.list += $prof;" ^
    "  Write-Host '[WT] Winix profile added.';" ^
    "};" ^
    "if ($wtJson.defaultProfile -ne $guid) {" ^
    "  $wtJson.defaultProfile = $guid;" ^
    "  Write-Host '[WT] Winix set as default profile.';" ^
    "} else { Write-Host '[WT] Winix is already the default profile.' };" ^
    "$wtJson | ConvertTo-Json -Depth 20 | Set-Content $wtPath -Encoding UTF8;" ^
    "Write-Host '[WT] Restart Windows Terminal to apply changes.'"
echo.

:: ==========================================================
:: 7. Windows Defender exclusions
:: ==========================================================
echo [8/8] Adding Windows Defender exclusions...
powershell -NoProfile -NonInteractive -Command ^
    "try {" ^
    "  Add-MpPreference -ExclusionPath '%INSTALL_PREFIX%' -ErrorAction Stop;" ^
    "  Add-MpPreference -ExclusionPath '%INSTALL_PREFIX%\bin' -ErrorAction Stop;" ^
    "  Write-Host '[DEFENDER] Exclusions added for %INSTALL_PREFIX% and %INSTALL_PREFIX%\bin.';" ^
    "} catch {" ^
    "  Write-Host '[DEFENDER] Could not add exclusions (Defender may not be active - that is OK).';" ^
    "}"
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
echo  * Start Menu: Search "Winix" - right-click to Pin to Taskbar
echo  * Explorer  : Right-click any folder -> "Open Winix here"
echo  * .sh files : double-click to execute with Winix
echo  * To remove : run uninstall.bat as Administrator
echo =========================================================
echo.
endlocal
exit /b 0
