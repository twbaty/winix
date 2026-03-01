# Winix Design Specification

**Version:** 0.8
**Status:** Living document — updated with each minor release.

---

## 1. Mission

Winix is a native Windows Unix-like command environment: a suite of C coreutils and a C++ interactive shell that behave like their GNU counterparts but compile and run purely on Windows — no MSYS, no WSL, no Cygwin.

---

## 2. Architecture Overview

```
winix/
├── src/
│   ├── shell/          # C++ interactive shell (winix.exe)
│   │   ├── main.cpp        — REPL, builtins, piping, redirection, chaining
│   │   ├── aliases.cpp/.hpp — alias storage and lookup
│   │   ├── completion.cpp/.hpp — tab completion engine
│   │   └── line_editor.cpp/.hpp — raw input, history, arrow keys
│   ├── coreutils/      # Standalone C executables (one .c → one .exe)
│   └── common/         # Shared static library (winixcommon)
│       ├── argparser.c     — flag/argument parsing helpers
│       └── fileops.c       — common file operation helpers
├── docs/               # Project documentation
├── .github/workflows/  # CI (GitHub Actions)
├── .vscode/            # IntelliSense, build tasks, debug config
├── CMakeLists.txt      # Unified build definition
├── install.bat         # One-command system install
└── build.bat           # Local build script
```

---

## 3. Shell (`src/shell/`)

### 3.1 REPL Loop

The shell (`winix.exe`) runs a read-eval-print loop:

1. **Render prompt** — expand PS1 format string (`\u`, `\h`, `\w`, `\W`, `\$`, `\t`, `\d`, `\e`, `\n`, `\\`)
2. **Read input** — `line_editor` handles raw console input, arrow-key history recall, and tab completion
3. **Expand variables** — tilde (`~/…`), `$VAR`, `$?` (last exit code)
4. **Split chains** — tokenise on `&&`, `||`, `;` respecting quoted strings
5. **For each segment:**
   - Parse redirects (`>`, `>>`, `<`, `2>`) and strip them from the command string
   - Split on `|` for piping
   - Dispatch: builtin → alias → external executable
6. **Track exit code** — store last exit in `$?`

### 3.2 Builtins

| Command | Behaviour |
|---------|-----------|
| `cd` | Change directory; updates `$PWD` |
| `exit` / `quit` | Terminate the shell |
| `history` | Print session + persisted history |
| `alias` | List or define aliases |
| `export` | Set environment variables |
| `set` | Set shell config (`PS1`, `case`, etc.) |
| `help` | Display grouped command reference |
| `echo` | Delegated to `echo.exe` (external) |

### 3.3 Piping

Pipes (`cmd1 | cmd2`) are implemented via Windows anonymous pipes. Each stage gets inheritable read/write handles passed through `CreateProcess`. Handles are closed in the parent after each child starts.

### 3.4 Redirection

Redirects are parsed from the command string before execution:

| Syntax | Behaviour |
|--------|-----------|
| `> file` | Stdout → file (create/truncate) |
| `>> file` | Stdout → file (append) |
| `< file` | Stdin ← file |
| `2> file` | Stderr → file |

Handles are opened via `CreateFileA` with `SECURITY_ATTRIBUTES` set for inheritance, then passed to `CreateProcess`.

### 3.5 Command Chaining

| Operator | Behaviour |
|----------|-----------|
| `cmd1 && cmd2` | Run `cmd2` only if `cmd1` exits 0 |
| `cmd1 \|\| cmd2` | Run `cmd2` only if `cmd1` exits non-zero |
| `cmd1 ; cmd2` | Run `cmd2` unconditionally |

### 3.6 Configuration (`.winixrc`)

Stored at `%USERPROFILE%\.winixrc`. Key-value pairs, one per line:

```
PS1=\[\e[32m\][Winix] \w >\[\e[0m\]
case=off
alias ll=ls -la
alias gs=git status
```

Loaded at shell startup. Written by `set` and `alias` builtins.

### 3.7 Tab Completion

`completion.cpp` handles:
- **Command completion** — matches against `PATH` executables and builtins
- **Alias completion** — matches defined aliases
- **Path completion** — matches filesystem entries relative to the current token

### 3.8 History

`line_editor.cpp` maintains a session history ring. On exit, history is appended to `winix_history.txt` in the user's home directory. On startup, prior history is loaded back.

---

## 4. Coreutils (`src/coreutils/`)

Each utility is a self-contained `.c` file that compiles to a standalone `.exe`. They follow these conventions:

### 4.1 File Utilities

| Utility | Key Flags | Notes |
|---------|-----------|-------|
| `ls` | `-l -a -h -r -t` | Long listing, hidden files, human sizes |
| `cp` | `-r -f -v -p` | Recursive, force, verbose, preserve timestamps |
| `mv` | `-f -v` | Force, verbose |
| `rm` | `-r -f` | Recursive, force |
| `mkdir` | `-p` | Create parents |
| `rmdir` | | Directory must be empty |
| `touch` | | Create or update timestamps |
| `stat` | | File metadata |
| `chmod` | `-R -v` | Maps to `SetFileAttributes` (read-only bit) |
| `chown` | `-R -v` | Uses `LookupAccountNameA` + `SetNamedSecurityInfoA` |

### 4.2 Text Utilities

| Utility | Key Flags | Notes |
|---------|-----------|-------|
| `cat` | `-n` | Line numbers |
| `head` | `-n N` | First N lines (default 10) |
| `tail` | `-n N` | Last N lines (default 10) |
| `grep` | `-i -n -r -v -c -l` | Case-insensitive, recursive, invert, count |
| `sort` | `-r -u -f` | Reverse, unique, case-fold |
| `uniq` | `-c -d -u` | Count, duplicates only, unique only |
| `wc` | `-l -w -c` | Lines, words, bytes |
| `tee` | `-a` | Append mode |

### 4.3 System Utilities

| Utility | Notes |
|---------|-------|
| `echo` | `-n -e` (no newline, escape sequences) |
| `pwd` | Prints working directory |
| `env` | Print environment |
| `export` | Set env var |
| `whoami` | Current username |
| `date` | Current date/time |
| `uname` | System info (`-a` for all) |
| `uptime` | System uptime |
| `sleep` | Sleep N seconds |
| `ps` | Process list |
| `kill` | Send signal to PID |
| `df` | Disk free |
| `du` | Disk usage |
| `which` | Locate executable on PATH |
| `true` / `false` | Exit 0 / exit 1 |
| `printf` | Formatted output |
| `basename` / `dirname` | Path component extraction |
| `ver` | Winix version string |
| `history` | Print command history |
| `alias` | List/define aliases |
| `more` / `less` | Paged output |

### 4.4 Windows Permission Mapping

Winix maps POSIX permission concepts to Windows where possible:

| POSIX concept | Windows implementation |
|---------------|----------------------|
| Write bit (`+w`/`-w`) | `FILE_ATTRIBUTE_READONLY` via `SetFileAttributes` |
| File ownership | SID-based via `SetNamedSecurityInfoA` (advapi32) |
| Execute bit | No Windows equivalent — silently ignored |
| Read bit | Always readable on Windows — silently ignored |

---

## 5. Build System

CMake (`CMakeLists.txt`) defines:
- `winixcommon` — static library (`argparser.c`, `fileops.c`)
- `winix` — shell executable, links `winixcommon`
- One `add_executable` per coreutil, all linking `winixcommon`
- `chown` additionally links `advapi32`
- `install()` target copies all binaries to `${CMAKE_INSTALL_PREFIX}/bin`

**Default install prefix:** `C:\Winix`

---

## 6. Design Constraints

1. **Native only** — no MSYS, no WSL, no Cygwin dependency at runtime.
2. **One file per utility** — each coreutil is a single `.c` file for simplicity and auditability.
3. **C99 for coreutils, C++17 for the shell** — no exceptions (literally — no C++ exceptions used).
4. **Exit codes** — always `0` for success, `1` for any error. No other values.
5. **Error messages** — format: `<utility>: <description>: <strerror or Windows error>`.
6. **No dynamic allocation where avoidable** — prefer stack buffers (4096 bytes for paths).
7. **Behavioral parity with GNU, not code parity** — we match observable behavior, not implementation.
