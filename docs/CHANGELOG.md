# Winix Changelog

All notable changes to this project will be documented in this file.
Follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) format.

---

## [Unreleased]
_Changes in development, not yet in a formal release._

---

## [4.1.6] – 2026-04-30
### Fixed
- **Tab completion with spaces in path**: completing a folder whose name contains
  spaces (e.g. `@Working on it`) inserted the raw path into the buffer, causing
  the shell to word-split it on Enter. Completions that contain spaces are now
  automatically wrapped in double quotes. The word-boundary scanner also handles
  an already-open quote, so Tab continues to work correctly inside a partial
  quoted path.

---

## [4.1.5] – 2026-04-22
### Fixed
- **Line-editor redraw corruption**: pasting a long path that wrapped across
  terminal rows left stale characters on screen. The redraw routine now clears
  to end-of-line at each wrapped row before repainting.

---

## [4.1.4] – 2026-04-08
### Fixed
- **`ver` builtin**: `ver` was falling through to the Windows system `ver` command
  and displaying the Windows version instead of the Winix version. It is now
  handled as a shell builtin and outputs `Winix Shell <version>`.

---

## [4.1.3] – 2026-04-06
### Fixed
- **Argument quoting**: mixed-quoted args like `--flag="value"` were passed to
  child processes with literal quote characters in the value. `glob_expand` now
  calls `unquote()` on all tokens, not just fully-quoted ones, so the child
  receives the correct unquoted string.
- **Command re-execution loop**: certain child process runtimes (e.g. GHC/Haskell,
  used by pandoc) leave synthetic KEY_EVENTS in the Windows console input buffer
  on exit. Winix now calls `FlushConsoleInputBuffer` after each synchronous child
  exits, preventing the shell from re-executing the last command in a loop.

---

## [4.1.2] – 2026-04-06
### Added
- Windows Defender exclusion added during install to reduce launch latency.

### Changed
- External commands found on the system PATH now bypass `cmd.exe` and are
  spawned directly via `spawn_direct`, eliminating the overhead of a `cmd.exe /C`
  wrapper for every non-builtin command.

---

## [4.1.1] – 2026-03-26
### Fixed
- Statically linked the MinGW runtime into `winix.exe`, eliminating the
  `libwinpthread-1.dll` dependency and making the binary fully self-contained.

---

## [4.1.0] – 2026-03-26
### Added
- `wfetch` — neofetch-style system information display (OS, CPU, RAM, shell,
  uptime, Winix version).

---

## [4.0.2] – 2026-03-21
### Fixed
- Trailing semicolons in `for`-loop item lists were incorrectly included in the
  last element; they are now stripped.
- Variables whose values contain `;` or spaces (e.g. PATH-style vars) are now
  automatically quoted on expansion to prevent word-splitting.

---

## [4.0.1] – 2026-03-09
### Added
- `winix.exe` now embeds a custom `>_` icon resource.

### Fixed
- `install.bat` / helper scripts: replaced em-dashes with hyphens to fix
  execution in non-UTF-8 environments.
- `install.bat`: `cd` to the script's own directory so "Run as Administrator"
  right-click works from any location.

---

## [4.0] – 2026-03-08
### Added
- `wzip` / `wunzip` — Winix-native zstd compression tool (`.wz` format).
- Man page system: structured man pages under `bin/man/`; `man <cmd>` builtin
  renders them through `less`.
- Zero-warning cppcheck pass with documented suppressions.

---

## [0.8] – 2026-02-28
### Added
- Shell redirection: `>`, `>>`, `<`, `2>` via inheritable Windows handles
- Command chaining: `&&`, `||`, `;` with exit-code gating
- Tilde expansion (`~/path`) in all path arguments
- `$?` last exit code variable
- `cp -p` preserve timestamps (atime/mtime via `utime()`)
- Real `chown` using `LookupAccountNameA` + `SetNamedSecurityInfoA` (advapi32)
- GitHub Actions CI workflow (MSYS2/MinGW-w64, windows-latest)
- `.vscode/` IntelliSense, build tasks, and GDB debug launch config
- `docs/build_instructions.md`

### Fixed
- Input sanitization for `cat`, `mv`, `pwd`, `echo`

---

## [0.7] – 2026-02-28
### Added
- PS1-style customizable prompt strings (`\u`, `\h`, `\w`, `\W`, `\$`, `\t`, `\d`, `\e`)
- `set PS1=...` builtin, persisted to `.winixrc`
- Real `chmod` using `SetFileAttributes()` (octal and symbolic modes, `-R`, `-v`)
- `cp -r` recursive directory copy (`-r`/`-R`/`-f`/`-v` flags)
- CMake `install` target — copies all binaries to `C:\Winix\bin`
- `install.bat` — admin check, cmake install, system PATH update
- `grep -i` case-insensitive search with color highlight on matched text
- Extended `help` index with grouped command categories

### Fixed
- `ls` error message format and path buffer (1024 → 4096)
- Alias loading for bash-style `alias name=value` format

---

## [0.6] – 2025-11-10
### Added
- Basic piping (`cmd1 | cmd2`)
- Tab completion for commands, aliases, and filesystem paths
- Uniform exit codes (0/1) across all coreutils
- `wc` flag parsing (`-l`, `-w`, `-c`)
- Argument parsing for `head -n`, `tail -n`, `sort -ruf`, `cat -n`, `rm -rf`, `mkdir -p`, `mv -fv`, `echo -ne`

---

## [0.5] – 2025-10-26
### Added
- `alias` and environment variable support
- Command history persistence (`↑`/`↓` recall across sessions)
- `case` sensitivity toggle (`set case=on/off`) persisted to `.winixrc`
- `cp`, `stat` coreutils (initial)
- `chmod`, `chown` stubs

---

## [0.4-alpha] – 2025-10-22
### Added
- Colored prompt and directory awareness
- Built-in `history` command
- Core file operation utilities: `pwd`, `echo`, `ls`, `cat`, `mv`, `rm`, `mkdir`, `rmdir`
- Extended utilities: `touch`, `head`, `tail`, `date`, `whoami`, `sleep`
- CMake build system (MinGW-w64 / Windows)
- Initial repository structure (`src/`, `docs/`, `etc/`, `usr/`, `var/`, `tests/`)

---

## [1.0] – *TBD*
_First stable, fully documented, publicly installable release._

### Planned
- Self-contained installer
- Complete documentation suite (Design Spec, Coding Standards, Testing Guide)
- Windows-native Unix parity across all core utilities
