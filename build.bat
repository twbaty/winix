@echo off
setlocal enableextensions

REM =========================================
REM   Force WinLibs MinGW-w64 only
REM =========================================

SET MINGW_ROOT=C:\mingw64
SET PATH=%MINGW_ROOT%\bin;%PATH%

echo [TOOLCHAIN] Using WinLibs MinGW-w64 (%MINGW_ROOT%)

REM --- Check gcc ---
where gcc >nul 2>&1
IF ERRORLEVEL 1 (
    echo [ERROR] gcc.exe not found in %MINGW_ROOT%\bin
    exit /b 1
)

echo [CHECK] gcc.exe OK: %MINGW_ROOT%\bin\gcc.exe

REM --- Check mingw32-make ---
where mingw32-make >nul 2>&1
IF ERRORLEVEL 1 (
    echo [ERROR] mingw32-make.exe missing!
    exit /b 1
)

echo [CHECK] mingw32-make.exe OK: %MINGW_ROOT%\bin\mingw32-make.exe

REM --- Check cc1.exe ---
SET CC1_PATH=%MINGW_ROOT%\libexec\gcc\x86_64-w64-mingw32\15.2.0\cc1.exe
IF NOT EXIST "%CC1_PATH%" (
    echo [ERROR] cc1.exe missing at:
    echo        %CC1_PATH%
    exit /b 1
)

echo [CHECK] cc1.exe OK: %CC1_PATH%

REM =========================================
REM   Build Winix
REM =========================================

echo [CLEAN] Removing old CMake cache...
rmdir /s /q build 2>nul
mkdir build
cd build

echo [BUILD] Running CMake...
cmake -G "MinGW Makefiles" ..

IF ERRORLEVEL 1 (
    echo [FAILED] CMake configure failed.
    exit /b 1
)

echo [BUILD] Building Winix...
mingw32-make -j4

IF ERRORLEVEL 1 (
    echo [FAILED] Build failed.
    exit /b 1
)

echo [SUCCESS] Winix built successfully!
exit /b 0
