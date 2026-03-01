# ✅ Winix — NEXT Development Steps
**Version:** 1.3
**Date:** 2026-03-01

---

## 🔥 Immediate Next Steps
- [x] Fix argument passing for external commands
- [x] Implement simple command history (↑ / ↓ arrow recall)
- [x] Add input sanitization to `cat`, `ls`, `mv`, `pwd`, `echo`
- [x] Add basic error codes and uniform exit messages (0/1)
- [x] Add case-sensitivity toggle (`set case=on/off`) and persist to `.winixrc`

---

## 🧰 Coreutils Development
- [x] `pwd`, `echo`, `ls`, `cat`, `mv`, `rm`, `mkdir`, `rmdir`
- [x] `touch`, `head`, `tail`, `date`, `whoami`, `sleep` (functional)
- [x] `wc` — flag parsing (`-l`, `-w`, `-c`) working
- [x] Add argument parsing (`head -n`, `tail -n`, `sort -ruf`, `cat -n`, `rm -rf`, `mkdir -p`, `mv -fv`, `echo -ne`)
- [x] Add file error handling — uniform exit codes and error messages
- [x] Implement: `cp`, `stat` (done); `chmod`, `chown` stubs exist (see roadmap below)
- [x] `cut` — field/char extraction (`-f`, `-c`, `-b`, `-d`, `-s`, range syntax)
- [x] `tr` — translate/delete/squeeze with POSIX classes, ranges, escape sequences
- [x] `find` — recursive traversal (`-name`, `-type`, `-maxdepth`, `-exec`, `-delete`, etc.)
- [x] `diff` — LCS-based file comparison, normal + unified (`-u`) output, `-i`/`-w`/`-b`/`-q`

---

## 🧠 Shell Features
- [x] Colored prompt
- [x] Directory awareness
- [x] Built-in command `history`
- [x] Tab completion (commands, aliases, filesystem paths and arguments)
- [x] Customizable prompt strings (PS1-style)
- [x] Extended `help` index with grouped categories
- [x] Basic piping (`cmd1 | cmd2`)
- [x] Redirection (`>`, `>>`, `<`, `2>`)
- [x] Command chaining (`&&`, `||`, `;`)
- [x] Tilde expansion in paths (`~/foo`)
- [x] `$?` last exit code
- [x] Shell variable assignment (`VAR=value`, `vars`, `unset`)
- [x] Command substitution `$(cmd)`
- [x] Background jobs (`cmd &`, `jobs`, `fg`)
- [x] Shell scripting: `if/elif/else/fi`, `for/done`, `while/done`
- [x] Function definitions (`name() { ... }`)
- [x] `break`, `continue`, `return`, `exit`
- [x] `source`/`.` to execute script files
- [x] Script file execution: `winix script.sh [args]`
- [x] Positional parameters `$1`–`$9`, `$#`, `$@`
- [x] Arithmetic expansion `$(( expr ))`
- [x] `case/esac` statement
- [x] `read VAR` — read line from stdin into variable
- [x] Here-docs (`<<EOF`)
- [x] `${VAR:-default}`, `${VAR:=val}`, `${#VAR}`, `${VAR:+val}` parameter expansion
- [x] `local VAR` — function-local variables

---

## ⚙️ Build System / Repo
- [x] CMake functional for Windows + MinGW
- [x] Add GitHub Actions CI (auto-build test)
- [x] Add `install` target to copy executables to `C:\Winix\bin`
- [x] Add `.vscode/` config for IntelliSense
- [x] Add `docs/build_instructions.md`
- [ ] Register `.sh` file association in Windows (run with `winix.exe`) — installer step

---

## 📚 Documentation
- [x] Project Charter & Overview
- [x] Add "Winix Design Spec"
- [x] Add "Coding Standards" (naming, style, return codes)
- [x] Add Developer Onboarding Guide
- [x] Add "Testing Guide" for utilities

---

## 🌄 Future Vision
- [x] Add `alias` and environment support
- [x] Implement tab-completion / command prediction
- [x] Optional *case-sensitive mode* for Unix-like fidelity
- [x] Package release as `Winix v0.4-alpha`

---
---

## 🧮 Version Milestones

| Version | Stage | Highlights |
|----------|--------|-------------|
| **0.3** | Done | Working shell, prompt color, core file ops |
| **0.4** | Done | History ✅, tab completion ✅, arg parsing ✅, error handling ✅, case toggle ✅ |
| **0.5** | Done | Aliases ✅, env vars ✅, history persistence ✅, case toggle ✅, cp/stat/chmod/chown stubs ✅ |
| **0.6** | Done | Piping ✅, install target ✅ (`install.bat` → `C:\Winix\bin`, system PATH) |
| **0.7** | Done | PS1 prompts ✅, cp -r ✅, chmod ✅, grep -i ✅, help ✅, install.bat ✅, .winixrc ✅ |
| **0.8** | Done | Redirection ✅, chaining ✅, tilde ✅, $? ✅ — shell fully rounded out |
| **0.9** | Done | Coreutil audit ✅, test harness ✅, CI badge ✅, clear/cls ✅, docs complete ✅ |
| **0.9.1** | Done | Glob expansion ✅, Ctrl+C ✅, case sensitivity wired to coreutils ✅ |
| **1.0** | Done | Windows-native Unix parity — clean, documented, extensible ✅ |
| **1.1** | Done | `nix` editor ✅, `cut` ✅, `tr` ✅, `find` ✅, `diff` ✅ — coreutil suite expanded |
| **1.2** | Done | Shell scripting ✅, md5sum/sha256sum ✅, hexdump ✅, sed ✅, xargs ✅, tac/rev/nl/id/timeout/ln ✅, seq/test/yes/hostname ✅, paste/comm/base64/shuf ✅, bg jobs ✅, $(()) ✅, case/esac ✅, read ✅ |
| **1.3** | Done | Here-docs ✅, nix multi-line clipboard ✅, `${VAR:-default}` expansion ✅, `local VAR` ✅, `.sh` file association |
| **1.4** | Done | `mktemp` ✅, `realpath` ✅, `cmp` ✅, `fold` ✅, `expand`/`unexpand` ✅, `column` ✅, `time` ✅, `wait` ✅, `watch` ✅, `bc` ✅, `awk` ✅ |

---

📌 *Rule of thumb:*
- Increment **minor** (x.y) when new features appear.
- Increment **patch** (x.y.z) for fixes or optimizations.
- Don't bump **major** until you're feature-complete and stable.

# Coreutils Roadmap

## nix (text editor)
- [x] Nano-style editor: title bar, content area, status bar
- [x] Full key bindings: Ctrl+S/Q/X/W/K/U/A/E, arrows, PgUp/Dn, Home/End, Tab, Enter, BS, Del
- [x] File load/save (Unix line endings), new-file support
- [x] Viewport scrolling (horizontal + vertical)
- [x] Find (Ctrl+W) with wrap-around search
- [x] Find next (Ctrl+N) — repeats last pattern without re-prompting
- [x] Undo (Ctrl+Z) — 512-entry ring buffer, all edit ops covered
- [x] Cut/paste single line (Ctrl+K / Ctrl+U)
- [x] Unsaved-changes prompt on quit
- [x] Find + replace (Ctrl+R) — y/n/a/ESC interactive, undo per replacement
- [x] Multi-line clipboard (repeated Ctrl+K cuts accumulate; Ctrl+U pastes all)
- [ ] Syntax highlighting (v1.4+)

## cp
- [x] Basic copy (works)
- [x] Add recursive copy (-r)
- [x] Preserve timestamps (-p)
- [x] Error on directory to file copy

## chmod
- [x] Stub implementation
- [x] Implement SetFileAttributes() mapping (read-only/writable)
- [ ] Add sidecar POSIX permission emulation (.meta)

## chown
- [x] Stub for Windows
- [x] Map usernames using LookupAccountNameA() → SID → SetNamedSecurityInfoA()
- [ ] Integrate future Windows SID translation (ACL inheritance)

## cut
- [x] Field extraction (`-f`) with custom delimiter (`-d`) and suppress (`-s`)
- [x] Character/byte extraction (`-c`, `-b`)
- [x] Range syntax: `N`, `N-M`, `N-`, `-M`

## tr
- [x] Translate (`tr SET1 SET2`), delete (`-d`), squeeze (`-s`)
- [x] POSIX character classes (`[:lower:]`, `[:upper:]`, `[:digit:]`, etc.)
- [x] Ranges (`a-z`), escape sequences (`\n`, `\t`, `\xHH`)
- [x] Complement (`-c`), combined modes (`-ds`)

## find
- [x] Recursive directory traversal with depth control (`-maxdepth`, `-mindepth`)
- [x] Predicates: `-name`, `-iname`, `-type f/d`, `-newer`, `-size`
- [x] Actions: `-print` (default), `-delete`, `-exec CMD {} \;`
- [x] Glob wildcards (`*`, `?`) in `-name`/`-iname`

## diff
- [x] LCS-based line comparison, exit codes 0/1/2
- [x] Normal diff output (`NcN`, `NdN`, `NaN` hunks)
- [x] Unified diff (`-u`, `-U N`) with `@@` hunk headers
- [x] Flags: `-i` (ignore case), `-w` (ignore all whitespace), `-b` (ignore whitespace changes), `-q` (brief)

## v1.4 Coreutils (Planned)
- [x] `awk` — pattern-action text processing (priority)
- [x] `mktemp` — create temp files/dirs (`mktemp`, `mktemp -d`)
- [x] `realpath` — resolve symlinks and relative paths
- [x] `column` — format output into aligned columns (`-t`, `-s`)
- [x] `time` — time a command (`time make`, reports real/user/sys)
- [x] `wait` — wait for background jobs (shell builtin companion to `&`)
- [x] `bc` — arbitrary-precision calculator (floats, unlike `$(())`)
- [x] `watch` — run command periodically (`watch -n 2 cmd`)
- [x] `cmp` — byte-level file comparison
- [x] `fold` — wrap long lines at a given width
- [x] `expand` / `unexpand` — convert tabs to/from spaces

_Keep this file updated with every commit.  When something ships, tick it off — and feel good about it._
