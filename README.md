# Winix

[![Version](https://img.shields.io/github/v/release/twbaty/winix?label=version&color=blue)](https://github.com/twbaty/winix/releases/latest)
![License](https://img.shields.io/badge/license-MIT-green)
![Platform](https://img.shields.io/badge/platform-Windows-0078D4)
![Language](https://img.shields.io/badge/language-C%2FC%2B%2B-orange)
[![CI](https://github.com/twbaty/winix/actions/workflows/ci.yml/badge.svg)](https://github.com/twbaty/winix/actions/workflows/ci.yml)
[![Download](https://img.shields.io/github/v/release/twbaty/winix?label=download&color=brightgreen)](https://github.com/twbaty/winix/releases/latest)

> The Unix Windows should have had.

**Winix** is a free, open-source Windows shell and GNU coreutils suite — bringing `bash`-style scripting and 130+ Unix command-line tools (`ls`, `grep`, `sed`, `awk`, `cat`, `find`, and more) natively to Windows, with no WSL, no MSYS, and no compatibility layers required.

> On Linux and missing Windows commands? Check out the companion project: [Doshell](https://github.com/twbaty/doshell) — DOS-style aliases for Linux.

---

## Quick Install (Recommended)

**No build tools required — just download and run.**

👉 **[Download Latest Release](https://github.com/twbaty/winix/releases/latest)** (look for `winix-vX.X-windows-x64.zip` under Assets)

1. Download `winix-vX.X-windows-x64.zip` from the link above
2. Extract the zip to any folder
3. Right-click `install.bat` → **Run as administrator**

> **Windows SmartScreen warning?** Click **"More info"** → **"Run anyway"**.
> Winix is open source and built transparently by [GitHub Actions CI](https://github.com/twbaty/winix/actions).
> It is not yet code-signed — this will be resolved in a future release.

That's it. Winix will be available from any terminal immediately.

> **PATH order matters.** The installer adds `C:\Winix\bin` to your system PATH. If it appears *before* `C:\Windows\System32`, GNU tools like `sort` and `find` will shadow their Windows equivalents — usually what you want, but may affect Windows scripts that rely on native behavior. If it appears *after*, Windows built-ins win for any name conflict. Adjust PATH order in **System Properties → Environment Variables** to match your preference.

### What the installer does
- Copies `winix.exe` to `C:\Winix\`
- Copies all coreutils (`ls`, `grep`, `gzip`, etc.) to `C:\Winix\bin\`
- Adds both to the **system PATH**
- Creates a **Start Menu** shortcut
- Adds an **"Open Winix here"** right-click context menu for Explorer
- Associates **`.sh` files** to run with Winix
- Adds a **Windows Terminal** profile (if installed)

### Uninstall
Right-click `uninstall.bat` → **Run as administrator**

---

## What is Winix?

Winix is a native Windows Unix shell and coreutils suite — no WSL, no MSYS, no compatibility layers. Just fast, self-contained `.exe` files that behave like their Linux counterparts.

**The shell (`winix.exe`):**
- Bash-compatible syntax: pipes, redirection, variables, functions, arrays
- Tab completion, command history, PS1 prompts
- Shell scripting: `if/elif/else`, `for`, `while`, `case`, `select`, `read`
- Built-in `man CMD` (shows `--help` through `less`)

**130+ coreutils**, including:

| Category | Commands |
|----------|----------|
| File ops | `ls` `cp` `mv` `rm` `mkdir` `rmdir` `touch` `find` `ln` `chmod` `chown` |
| Text | `cat` `grep` `sed` `awk` `sort` `uniq` `wc` `head` `tail` `cut` `tr` `diff` |
| Archive | `gzip` `gunzip` |
| Hashing | `md5sum` `sha256sum` `sha512sum` `sha1sum` `sha224sum` `sha384sum` `b2sum` |
| System | `ps` `kill` `uname` `uptime` `who` `whoami` `id` `groups` |
| Dev | `bc` `awk` `xxd` `hexdump` `od` `strings` `expr` `dd` |
| Winix-native | `nix` (editor) `wlint` (lint) `wsim` (similarity) |

---

## Build from Source

Requires: [MinGW-w64](https://www.mingw-w64.org/) and [CMake 3.20+](https://cmake.org/)

```bat
git clone https://github.com/twbaty/winix.git
cd winix
build.bat
install.bat   (run as Administrator)
```

---

## Project Overview

Winix aims to:
- Provide a modular suite of native Windows command-line utilities written in C
- Deliver a modern C++ shell that behaves like Bash but integrates cleanly with Windows internals
- Maintain behavioral compatibility with GNU utilities where appropriate, using completely independent, clean-room implementations
- Follow a Unix-style directory structure under `C:\Winix`

**Guiding Principles:**
1. **Native First** — All binaries are compiled for Windows; no MSYS, no WSL
2. **Behavioral Parity, Not Code Parity** — GNU defines the behavioral baseline, not the implementation
3. **Self-Contained** — No runtime dependencies; everything ships in the zip
4. **Simplicity Over Cleverness** — Readable, maintainable code is the goal
5. **Open Source** — MIT License, designed to outlive its creators

| Layer | Language | Description |
|-------|----------|-------------|
| Core Utilities | C | Standalone executables mirroring essential GNU tools |
| Shell | C++ | Interactive REPL with parsing, execution, redirection, and environment management |
| Shared Library | C | Common argument parsing, error handling, and I/O routines |
| Build System | CMake | Unified build and packaging framework |

---

*Copyright (c) 2025 twbaty — MIT License*
