# Winix — Build Instructions

> Tested on Windows 10/11 with MinGW-w64 15.x and CMake 3.20+.

---

## Prerequisites

| Tool | Version | Download |
|------|---------|----------|
| **MinGW-w64 (GCC)** | 15.x (WinLibs) | https://winlibs.com — grab the UCRT x64 release |
| **CMake** | 3.20+ | https://cmake.org/download |
| **Git** | any | https://git-scm.com |

Install MinGW-w64 to `C:\mingw64`. After installing, add `C:\mingw64\bin` to your system `PATH`.

Verify your environment:

```cmd
gcc --version
cmake --version
mingw32-make --version
```

All three should print version info without errors.

---

## Clone the Repository

```cmd
git clone https://github.com/twbaty/winix.git
cd winix
```

---

## Configure the Build

From the repo root, create and configure the `build/` directory:

```cmd
cd build
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ..
```

This generates MinGW-compatible Makefiles targeting `C:\Winix` as the install prefix.
Override the prefix at configure time if needed:

```cmd
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=D:\MyWinix ..
```

---

## Build

From inside `build/`:

```cmd
mingw32-make -j4
```

Use `-j$(nproc)` (or `-j8`, etc.) to match your core count. A full build takes under a minute on modern hardware.

All binaries are placed directly in `build/` as `.exe` files.

---

## Install

Run as **Administrator** from `build/`:

```cmd
mingw32-make install
```

This copies all binaries to `C:\Winix\bin`. You can also use the provided script from the repo root:

```cmd
install.bat
```

`install.bat` checks for admin rights, runs `cmake --install`, and adds `C:\Winix\bin` to your system `PATH` automatically.

---

## Verify the Install

Open a new terminal (so PATH is refreshed) and run:

```cmd
winix --version
ls --version
echo hello
```

---

## Automated Build Script

`build.bat` in the repo root handles the full cycle — git pull, CMake configure, and compile:

```cmd
build.bat
```

It validates that `gcc.exe`, `mingw32-make.exe`, and `cc1.exe` are present before proceeding.

---

## VS Code

Open the repo root in VS Code. The `.vscode/` directory provides:

- **IntelliSense** — C99/C++17 with all project include paths and MinGW-w64 headers resolved
- **Build task** — `Ctrl+Shift+B` runs `mingw32-make -j4`; errors appear inline in the Problems panel
- **Debug** — F5 to debug `winix.exe` or the currently open coreutil via GDB

If you see red squiggles on Windows headers, confirm your MinGW-w64 is at `C:\mingw64`. If it's elsewhere, update `compilerPath` in `.vscode/c_cpp_properties.json`.

---

## CI

Every push to `main` runs the GitHub Actions workflow (`.github/workflows/ci.yml`), which:

1. Installs MinGW-w64 via MSYS2 on a `windows-latest` runner
2. Configures and builds the full project
3. Verifies all 45 binaries are present
4. Runs basic smoke tests (`echo`, `pwd`, `true`, `false`)
5. Uploads `.exe` artifacts (7-day retention)

Build status: [![CI](https://github.com/twbaty/winix/actions/workflows/ci.yml/badge.svg)](https://github.com/twbaty/winix/actions/workflows/ci.yml)

---

## Troubleshooting

**`cc1.exe not found`** — CMake found the wrong GCC. Make sure `C:\mingw64\bin` is first in your PATH, before any other GCC installs (MSYS2, Git's bundled GCC, etc.).

**`mingw32-make: command not found`** — `C:\mingw64\bin` isn't in PATH, or the shell wasn't restarted after adding it.

**`cmake: command not found`** — CMake installer may not have added itself to PATH. Add `C:\Program Files\CMake\bin` manually.

**ANSI colors broken after build** — run `build.bat` rather than bare `cmake`/`make`; it restores the Windows console input mode that MinGW can corrupt.

**Compile error on `chown.c`** — `chown` links `advapi32`. If you see linker errors, confirm your CMakeLists.txt has `target_link_libraries(chown winixcommon advapi32)`.
