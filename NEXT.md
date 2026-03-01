# ✅ Winix — NEXT Development Steps
**Version:** 1.0
**Date:** 2026-02-28

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

---

## ⚙️ Build System / Repo
- [x] CMake functional for Windows + MinGW  
- [x] Add GitHub Actions CI (auto-build test)
- [x] Add `install` target to copy executables to `C:\Winix\bin`
- [x] Add `.vscode/` config for IntelliSense
- [x] Add `docs/build_instructions.md`

---

## 📚 Documentation
- [x] Project Charter & Overview
- [x] Add “Winix Design Spec”
- [x] Add “Coding Standards” (naming, style, return codes)
- [x] Add Developer Onboarding Guide
- [x] Add “Testing Guide” for utilities

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
| **1.0** | **Stable** | Windows-native Unix parity — clean, documented, extensible ✅ |

---

📌 *Rule of thumb:*  
- Increment **minor** (x.y) when new features appear.  
- Increment **patch** (x.y.z) for fixes or optimizations.  
- Don’t bump **major** until you’re feature-complete and stable.

# Coreutils Roadmap

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

_Keep this file updated with every commit.  When something ships, tick it off — and feel good about it._
