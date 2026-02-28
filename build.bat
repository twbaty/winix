@echo off
setlocal enabledelayedexpansion

:: ==========================================================
:: Save real console modes so MinGW/CMake can't break CMD
:: ==========================================================
for /f "usebackq delims=" %%A in (`powershell -nop -c "(Get-ConsoleMode (Get-StdHandle -11))"`) do set ORIG_OUT_MODE=%%A
for /f "usebackq delims=" %%A in (`powershell -nop -c "(Get-ConsoleMode (Get-StdHandle -10))"`) do set ORIG_IN_MODE=%%A

echo [INFO] Saved console modes: IN=%ORIG_IN_MODE%  OUT=%ORIG_OUT_MODE%
echo.

:: ==========================================================
:: Git Sync
:: ==========================================================
echo [GIT] Pulling latest main branch...
git pull origin main

echo [GIT] Updating submodules...
git submodule update --init --recursive
echo.

:: ==========================================================
:: Toolchain Detection
:: ==========================================================
set MINGW_ROOT=C:\mingw64

if not exist "%MINGW_ROOT%\bin\gcc.exe" (
    echo [ERROR] MinGW-w64 not found at %MINGW_ROOT%
    goto restore_console
)

echo [TOOLCHAIN] Using WinLibs MinGW-w64 (%MINGW_ROOT%)
set PATH=%MINGW_ROOT%\bin;%PATH%

where gcc >nul 2>&1 || (echo [ERROR] gcc.exe missing & goto restore_console)
where mingw32-make >nul 2>&1 || (echo [ERROR] mingw32-make.exe missing & goto restore_console)

set CC1_PATH=%MINGW_ROOT%\libexec\gcc\x86_64-w64-mingw32\15.2.0\cc1.exe
if not exist "%CC1_PATH%" (
    echo [ERROR] cc1.exe missing at:
    echo        %CC1_PATH%
    goto restore_console
)

echo [CHECK] gcc.exe OK
echo [CHECK] mingw32-make.exe OK
echo [CHECK] cc1.exe OK
echo.

:: ==========================================================
:: Build
:: ==========================================================
:: echo [CLEAN] Removing old CMake cache...
:: rmdir /s /q build 2>nul
:: mkdir build
cd build

echo [BUILD] Running CMake...
cmake -G "MinGW Makefiles" .. || (echo [FAILED] CMake configure failed & cd .. & goto restore_console)

echo [BUILD] Compiling Winix...
mingw32-make -j4 || (echo [FAILED] Build failed & cd .. & goto restore_console)

cd ..
echo [SUCCESS] Winix built successfully!
echo.

:: ==========================================================
:: Restore real console modes (prevents arrow-key corruption)
:: ==========================================================
:: Do NOT restore OUT mode — it breaks ANSI colors
:: if defined ORIG_OUT_MODE (
::     powershell -nop -c "Set-ConsoleMode (Get-StdHandle -11) $env:ORIG_OUT_MODE" >nul 2>&1
:: )

:: Only restore input mode (fixes arrow keys)
if defined ORIG_IN_MODE (
    powershell -nop -c "Set-ConsoleMode (Get-StdHandle -10) $env:ORIG_IN_MODE" >nul 2>&1
)

echo [INFO] Console modes restored.
echo.
endlocal
exit /b 0
