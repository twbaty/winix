# Winix Developer Onboarding Guide

Welcome. This guide gets you from zero to a working Winix development environment in one sitting.

---

## Step 1 — Install Prerequisites

You need three tools. Install them in this order.

### MinGW-w64 (GCC for Windows)

1. Go to https://winlibs.com
2. Download the latest **UCRT x64** release (GCC 15.x, POSIX threads, SEH exceptions)
3. Extract to `C:\mingw64`
4. Add `C:\mingw64\bin` to your system `PATH`

Verify:
```cmd
gcc --version
mingw32-make --version
```

### CMake

1. Download from https://cmake.org/download — Windows x64 installer
2. During install, choose **"Add CMake to system PATH for all users"**

Verify:
```cmd
cmake --version
```

### Git

1. Download from https://git-scm.com
2. Default install options are fine

Verify:
```cmd
git --version
```

---

## Step 2 — Clone and Build

```cmd
git clone https://github.com/twbaty/winix.git
cd winix
cd build
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug ..
mingw32-make -j4
cd ..
```

You should see all binaries appear in `build/` with no errors or warnings.

See `docs/build_instructions.md` for full detail on the build system.

---

## Step 3 — Open in VS Code

1. Install [VS Code](https://code.visualstudio.com) if you haven't
2. Install the **C/C++ extension** (`ms-vscode.cpptools`)
3. Open the repo root: `File → Open Folder → winix/`

IntelliSense should activate immediately — the `.vscode/c_cpp_properties.json` points at your MinGW-w64 install and all project headers.

**Keyboard shortcuts:**
- `Ctrl+Shift+B` — build (runs `mingw32-make -j4`)
- `F5` — debug the winix shell (via GDB)
- When editing a coreutil `.c` file, use the **"Debug: current coreutil"** launch config to build and debug that specific binary

---

## Step 4 — Understand the Layout

```
src/
  shell/       — C++ shell (main.cpp + subsystems)
  coreutils/   — one .c file per utility
  common/      — shared static lib (argparser.c, fileops.c)
docs/          — all project documentation
.github/       — CI workflow
.vscode/       — editor config
CMakeLists.txt — single build definition for everything
```

Read `docs/design_spec.md` for the full architecture walkthrough.

---

## Step 5 — Make a Change

Pick something small to start. A good first change:
- Add a missing flag to an existing coreutil (e.g., `sort -n` numeric sort)
- Improve an error message
- Fix a bug listed in `NEXT.md`

**Workflow:**
1. Edit the `.c` or `.cpp` file
2. `Ctrl+Shift+B` to build (or `cd build && mingw32-make -j4`)
3. Test the binary directly: `build\<name>.exe [args]`
4. Commit with a clear message

**Before committing, always build.** The CI catches build failures, but it's faster to catch them locally.

---

## Step 6 — Coding Conventions

Read `docs/coding_standards.md` before writing any code. The short version:

- C99 for coreutils, C++17 for the shell
- Exit codes: `0` = success, `1` = any error — nothing else
- Error messages: `<utility>: <what>: <why>` to `stderr`
- Stack buffers of 4096 for paths, always `snprintf`
- Flag parsing: combined short flags (`-rfv`), no `getopt`
- Comment the **why**, not the **what**

---

## Step 7 — Adding a New Utility

1. Create `src/coreutils/<name>.c` — look at `touch.c` or `sleep.c` for minimal examples
2. Add to `CMakeLists.txt` (two lines — `add_executable` + `target_link_libraries`)
3. Add to the `install(TARGETS ...)` list
4. Add to the binary check in `.github/workflows/ci.yml`
5. Document in `docs/design_spec.md`

---

## Common Problems

**Squiggles in VS Code on Windows headers** — check that `C:\mingw64` exists and matches the path in `.vscode/c_cpp_properties.json`.

**`mingw32-make: command not found`** — `C:\mingw64\bin` isn't in PATH, or your terminal wasn't restarted after adding it.

**Arrow keys print garbage in the winix shell** — run the shell from a proper Windows Terminal, not a legacy `cmd.exe` prompt. The shell uses ANSI escape sequences for line editing.

**Build succeeds but changes aren't in the installed binary** — `build/` binaries and `C:\Winix\bin` binaries are separate. Either test from `build\<name>.exe` directly, or run `mingw32-make install` (as admin) to push to `C:\Winix\bin`.

---

## Resources

| Document | Location |
|----------|----------|
| Build instructions | `docs/build_instructions.md` |
| Architecture | `docs/design_spec.md` |
| Coding standards | `docs/coding_standards.md` |
| Roadmap | `NEXT.md` |
| Changelog | `docs/CHANGELOG.md` |
| CI status | https://github.com/twbaty/winix/actions |
