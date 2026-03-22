# ✅ Winix — NEXT Development Steps
**Version:** 4.0
**Date:** 2026-03-08

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
- [x] Register `.sh` file association in Windows (run with `winix.exe`) — installer step

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
| **1.5** | Done | Start Menu shortcut ✅, Pin to Taskbar ✅, Windows Terminal profile ✅, "Open Winix here" context menu ✅, `.sh` file association ✅, `uninstall.bat` ✅ |
| **1.6** | Done | `wlint` ✅ — filesystem lint detector (duplicates, empty files/dirs, SHA-256 via BCrypt, JSON/CSV output, quarantine mode) |
| **1.7** | Done | `wlint` v1.2 ✅ — glob filtering (`--include`/`--exclude`/`--ext`), `--max-size`, `--stats` block, deterministic within-group ordering, stats in JSON |
| **1.8** | Done | `wlint` v1.3 ✅ — `--scan-json FILE` raw file inventory for wsim (path, size, mtime, ext, basename) |
| **1.9** | Done | `wsim` v0.1 ✅ — similarity scorer (basename/ext/size/mtime scoring, blocking, union-find grouping, JSON output) |
| **2.0** | Done | `apropos` ✅ — search command descriptions by keyword; substring + whole-word modes |
| **2.1** | Done | `wlint` v1.4 ✅ — `--log FILE` operational JSON log (summary, options, elapsed_ms); `wsim` v0.2 ✅ — `--pretty` human-readable output, `--recommend-keep newest\|oldest\|path-shortest` |
| **2.2** | Done | `wlint` v1.5 ✅ — 3-phase hashing: 1 MiB quick-hash eliminates large-file false candidates before full SHA-256; 1 MiB I/O buffer (was 64 KB); `--stats` shows quick-hash ops vs full-hash ops |
| **2.3** | Done | `wlint` v1.6 ✅ — parallel hashing: `--threads N` worker pool (default 2, max 64); Windows thread pool with `CRITICAL_SECTION` work queue; overlapped I/O across files simultaneously |
| **2.4** | Done | `wlint` v1.7 ✅ — `--temp`/`-t` temp/junk file detection (.tmp .bak .swp .cache .crdownload .part .dmp ~$ etc.); `wsim` v0.3 ✅ — `--csv FILE` output, `--min-score` default raised 0.40→0.65 |
| **2.5** | Done | `chmod` ✅ — sidecar POSIX permission emulation (`<file>.winixmeta`), full octal/symbolic mode tracking; `chown` ✅ — ACL inheritance: DACL updated with `GENERIC_ALL` + inherit flags for new owner |
| **2.6** | Done | `nix` v1.1 ✅ — syntax highlighting: C/C++ (keywords, strings, comments, numbers, preprocessor), Shell, Python, JSON; ANSI colors, block-comment state threading, language detection by extension |
| **2.7** | Done | `tee` ✅ v1.0 — rewritten: multiple files, `-a` append, binary-safe; `strings` ✅ — extract printable runs (`-n`, `-t d/o/x`); `xxd` ✅ — hex dump + reverse mode (`-n/-s/-c/-g/-u/-p/-r`); `od` ✅ — octal/hex/char dump (`-t/-A/-N/-j/-w`); `split` ✅ — split by bytes/lines/chunks (`-b/-l/-n/-d/-a`) |
| **2.8** | Done | `expr` ✅ — arithmetic/comparison/logic/string ops; `nproc` ✅ — processor count (`--all`, `--ignore=N`); `truncate` ✅ — resize files (`-s [+/-]SIZE`); `numfmt` ✅ — SI/IEC number formatting; `readlink` ✅ — symlink resolution (`-f/-e/-m`); `cksum` ✅ — CRC-32 + byte count; `factor` ✅ — prime factorization |
| **2.9** | Done | `sha1sum` ✅ — SHA-1 checksums (pure C, check mode, BSD tag); `sha512sum` ✅ — SHA-512 checksums (pure C FIPS 180-4); `join` ✅ — join sorted files on common field (`-1/-2/-j/-t/-i/-a/-v/-e/-o`); `tsort` ✅ — topological sort (Kahn's algorithm, cycle detection); `tty` ✅ — print terminal name; `logname` ✅ — print login name; `printenv` ✅ — print environment variables (`--null`); `fmt` ✅ — reformat paragraphs (`-w/-s/-u`) |
| **3.0** | Done | GNU coreutils parity sprint: `sha224sum` ✅ SHA-224; `sha384sum` ✅ SHA-384; `unlink` ✅ raw delete; `link` ✅ hard link; `sync` ✅ flush buffers; `pathchk` ✅ validate filenames; `base32` ✅ RFC 4648; `shred` ✅ secure overwrite (`-n/-z/-u/-v`); `dd` ✅ block copy (`if/of/bs/count/skip/seek/conv`); `nice` ✅ priority classes; `nohup` ✅ hangup-immune; `groups` ✅ group memberships; `who` ✅ logged-on sessions; `users` ✅ logged-on names |
| **3.1** | Done | GNU coreutils final niche tools: `csplit` ✅ — context split on regex/line patterns (`-f/-n/-b/-k/-z`); `pr` ✅ — paginate/columnate for printing (`-N cols/-l/-w/-h/-t/-d/-o/-m`); `stdbuf` ✅ — buffering wrapper (passthrough with Windows caveat note); `b2sum` ✅ — BLAKE2b checksums (pure C RFC 7693, `-l BITS`, check mode, `--tag`) |
| **3.2** | Done | Shebang script execution ✅ — `./foo.sh` and `./foo` run in-process; shebang `#!/usr/bin/env winix` (or `sh`/`bash`) uses built-in interpreter; foreign shebangs (`python3`, `node`, etc.) spawn the interpreter from PATH; `name.sh` without `./` searches PATH only (Linux behaviour); no shebang defaults to winix |
| **3.3** | Done | Shell arrays ✅ (`arr=(a b c)`, `${arr[@]}`, `${arr[N]}`, `${#arr[@]}`); `select` statement ✅; `$*` vs `$@` distinction ✅ (positional param quoting fixed); `man` builtin ✅ (passthrough to `--help \| less`); `winix.exe` moved to `C:\Winix\` root ✅ |
| **3.4** | Done | `gzip`/`gunzip` ✅ — bundled zlib 1.3.1, full RFC 1952 support, flags `-1`–`-9`/`-d`/`-k`/`-c`/`-f`/`-v`/`-l`/`-t`, stdin/stdout pipe, argv[0] detection |
| **3.5** | Done | GNU compat sprint: `grep` ✅ regex (`-E`/`-G`/`-F`/`-i`/`-v`/`-w`/`-x`/`-o`/`-c`/`-n`/`-l`/`-r`/`-m`/`--color`); `sort` ✅ (`-n`/`-k`/`-t`/`-s` stable merge sort); `tail` ✅ (`-f`/`-F`/`-c`/`+N`/`-q`/`-v`); `less`/`more` ✅ ANSI passthrough fix |
| **3.6** | Done | Shell scripting sprint: process substitution ✅ `<(cmd)`/`>(cmd)`; brace expansion ✅ `{a,b,c}`/`{1..5}`/nested/step; `getopts` ✅ POSIX option parsing; `trap EXIT` ✅ cleanup handlers; `printf` ✅ promoted to in-process builtin |
| **4.0** | Done | Release milestone: `wzip`/`wunzip` ✅ zstd 1.5.7 native compression (`.wz` format); `man` pages ✅ structured docs for 20 commands + upgraded builtin; cppcheck ✅ zero warnings with suppression file; install verified ✅ clean layout at `C:\Winix\` |

---

## 🗺️ Forward Roadmap

### Next Up (v3.7 targets)

## 🎉 v4.0 — Feature Complete

Winix has reached its planned feature set. All roadmap items are done.

### What's in v4.0
- **wzip / wunzip** — Winix-native zstd compression, `.wz` format
- **man pages** — 20 structured pages for top commands; `man` builtin upgraded
- **cppcheck** — zero warnings, documented suppression file
- **Install verified** — `C:\Winix\winix.exe`, `bin\`, `man\` all correct

### Going Forward
This is a stable, feature-complete release. Future work (if any) would be:
- Expand man pages to remaining commands
- `man -k KEYWORD` search (builds on `apropos`)
- Additional wlint / wsim improvements as needs arise
- **install.bat uses `copy /y` / `xcopy /y` — overwrites existing files but does not remove
  stale coreutils if a binary is ever dropped from a release. Fix if a coreutil is ever removed:
  wipe `C:\Winix\bin\` before copying, or diff against a manifest.**

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
- [x] Syntax highlighting (v1.1) — C/C++, Shell, Python, JSON

## cp
- [x] Basic copy (works)
- [x] Add recursive copy (-r)
- [x] Preserve timestamps (-p)
- [x] Error on directory to file copy

## chmod
- [x] Stub implementation
- [x] Implement SetFileAttributes() mapping (read-only/writable)
- [x] Add sidecar POSIX permission emulation (.meta)

## chown
- [x] Stub for Windows
- [x] Map usernames using LookupAccountNameA() → SID → SetNamedSecurityInfoA()
- [x] Integrate future Windows SID translation (ACL inheritance)

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

## wsim (v0.1)
- [x] Read wlint `--scan-json` inventory
- [x] Name normalization: lowercase, strip ext, underscore/hyphen → spaces, copy markers, version markers, collapse whitespace
- [x] Candidate blocking: same ext + size ±20% + shared first token
- [x] Scoring: basename similarity (50%), ext match (15%), size similarity (25%), mtime proximity (10%)
- [x] Union-find clustering of similar pairs
- [x] JSON output (stdout or `--out FILE`), sorted by score descending
- [x] `--min-score N` threshold, `--verbose`, `--version`, `--help`
- [x] Exit codes: 0=no candidates, 1=candidates found, 2=error
- [x] `--pretty` human-readable output
- [x] `--min-score` default tuning after real-world testing (raised to 0.65)
- [x] `--recommend-keep newest|oldest|path-shortest`
- [x] `--csv` output

## wlint (v1.7)
- [x] Three-phase duplicate detection: size grouping → SHA-256 (Windows CNG BCrypt) → optional byte-verify (`--verify`)
- [x] Empty file and empty directory detection (`--empty`)
- [x] Keep policies: `newest` (default), `oldest`, `first`
- [x] Output: human-readable (ANSI color), `--json FILE`, `--csv FILE`
- [x] Quarantine mode: `--quarantine DIR` — moves non-kept duplicates, writes `wlint_moves.json`
- [x] Unicode path support (wchar_t internally, UTF-8 output)
- [x] Junction point loop prevention
- [x] Exit codes: 0=clean, 1=lint found, 2=error
- [x] Glob filtering: `--include PAT`, `--exclude PAT` (repeatable, case-insensitive)
- [x] Extension shortcut: `--ext .jpg,.pdf` (compiles to include patterns)
- [x] `--max-size BYTES` — skip files larger than limit
- [x] `--stats` — elapsed time, bytes in pool, SHA-256 ops, verify ops
- [x] Stats always present in JSON output (`stats` key)
- [x] Deterministic within-group ordering (non-kept files sorted lex by path)
- [x] `--scan-json FILE` — raw file inventory JSON for wsim (path, size, mtime, ext, basename)
- [x] `--log FILE` — operational JSON log (files_scanned, dup groups, bytes_reclaimable, elapsed_ms, options)
- [x] Temp file detection (common temp extensions) — `--temp` / `-t`
- [x] Syntax highlighting in `nix` editor — C/C++, Shell, Python, JSON

_Keep this file updated with every commit.  When something ships, tick it off — and feel good about it._
