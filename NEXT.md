# ✅ Winix — NEXT Development Steps
**Version:** 0.4 (in progress)
**Date:** 2026-02-28

---

## 🔥 Immediate Next Steps
- [x] Fix argument passing for external commands
- [x] Implement simple command history (↑ / ↓ arrow recall)
- [ ] Add input sanitization to `cat`, `ls`, `mv`, `pwd`, `echo`
- [ ] Add basic error codes and uniform exit messages (0/1)
- [ ] Add `set` command (for toggles and shell vars, e.g. `set case=on`)
- [ ] Add case-sensitivity toggle (`set case=on/off`)

---

## 🧰 Coreutils Development
- [x] `pwd`, `echo`, `ls`, `cat`, `mv`, `rm`, `mkdir`, `rmdir`  
- [x] `touch`, `head`, `tail`, `date`, `whoami`, `sleep` (functional)
- [x] `wc` — flag parsing (`-l`, `-w`, `-c`) working
- [x] Add argument parsing (`head -n`, `tail -n`, `sort -ruf`, `cat -n`, `rm -rf`, `mkdir -p`, `mv -fv`, `echo -ne`)
- [ ] Add file error handling (missing, permission, etc.)
- [ ] Implement: `cp`, `chmod`, `chown`, `stat`

---

## 🧠 Shell Features
- [x] Colored prompt  
- [x] Directory awareness  
- [x] Built-in command `history`
- [x] Tab completion (commands, aliases, filesystem paths and arguments)
- [ ] Customizable prompt strings (PS1-style)
- [ ] Extended `help` index with grouped categories
- [ ] Basic piping (`cmd1 | cmd2`)

---

## ⚙️ Build System / Repo
- [x] CMake functional for Windows + MinGW  
- [ ] Add GitHub Actions CI (auto-build test)  
- [ ] Add `install` target to copy executables to `C:\Winix\bin`  
- [ ] Add `.vscode/` config for IntelliSense  
- [ ] Add `docs/build_instructions.md`  

---

## 📚 Documentation
- [x] Project Charter & Overview  
- [ ] Add “Winix Design Spec”  
- [ ] Add “Coding Standards” (naming, style, return codes)  
- [ ] Add Developer Onboarding Guide  
- [ ] Add “Testing Guide” for utilities  

---

## 🌄 Future Vision
- [x] Add `alias` and environment support
- [x] Implement tab-completion / command prediction
- [ ] Optional *case-sensitive mode* for Unix-like fidelity
- [ ] Build modular “Winix Core” API for third-party utilities
- [ ] Package release as `Winix v0.4-alpha`

---
---

## 🧮 Version Milestones

| Version | Stage | Highlights |
|----------|--------|-------------|
| **0.3** | Done | Working shell, prompt color, core file ops |
| **0.4** | In Progress | History ✅, tab completion ✅, arg pass-through ✅, `wc` ✅, coreutils arg parsing ✅ |
| **0.5** | Planned | Argument parsing, better error handling, extended `help` |
| **0.6** | Future | Add piping and chaining (`cmd1 | cmd2`), install target |
| **0.7** | Future | Add configuration support (`.winixrc`), environment vars |
| **1.0** | Stable | Windows-native Unix parity — clean, documented, extensible |

---

📌 *Rule of thumb:*  
- Increment **minor** (x.y) when new features appear.  
- Increment **patch** (x.y.z) for fixes or optimizations.  
- Don’t bump **major** until you’re feature-complete and stable.

# Coreutils Roadmap

## cp
- [x] Basic copy (works)
- [ ] Add recursive copy (-r)
- [ ] Preserve timestamps (-p)
- [ ] Error on directory to file copy

## chmod
- [x] Stub implementation
- [ ] Implement SetFileAttributes() mapping (read-only/hidden)
- [ ] Add sidecar POSIX permission emulation (.meta)

## chown
- [ ] Stub for Windows
- [ ] Map usernames using GetUserNameEx()
- [ ] Integrate future Windows SID translation

_Keep this file updated with every commit.  When something ships, tick it off — and feel good about it._
