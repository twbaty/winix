# Winix Changelog

All notable changes to this project will be documented in this file.
Follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) format.

---

## [Unreleased]
_Changes in development, not yet in a formal release._

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
