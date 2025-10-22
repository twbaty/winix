# ✅ Winix — NEXT Development Steps
**Version:** 0.3 → 0.4 Prep  
**Date:** 2025-10-22  

---

## 🔥 Immediate Next Steps
- [x] Fix argument passing for external commands  
- [ ] Add input sanitization to `cat`, `ls`, `mv`, `pwd`, `echo`  
- [ ] Add basic error codes and uniform exit messages (0/1)  
- [ ] Implement simple command history (↑ / ↓ arrow recall)  
- [ ] Add `set` command (for toggles and shell vars, e.g. `set case=on`)  
- [ ] Add case-sensitivity toggle (`set case=on/off`)  

---

## 🧰 Coreutils Development
- [x] `pwd`, `echo`, `ls`, `cat`, `mv`, `rm`, `mkdir`, `rmdir`  
- [x] `touch`, `head`, `tail`, `date`, `whoami`, `sleep` (functional)  
- [ ] Add argument parsing (e.g., `head -n 10 file.txt`)  
- [ ] Add file error handling (missing, permission, etc.)  
- [ ] Implement: `cp`, `chmod`, `chown`, `stat`, `wc`  

---

## 🧠 Shell Features
- [x] Colored prompt  
- [x] Directory awareness  
- [ ] Customizable prompt strings (PS1-style)  
- [ ] Built-in command `history`  
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
- [ ] Optional *case-sensitive mode* for Unix-like fidelity  
- [ ] Add `alias` and environment support  
- [ ] Implement tab-completion / command prediction  
- [ ] Build modular “Winix Core” API for third-party utilities  
- [ ] Package release as `Winix v0.4-alpha`  

---
---

## 🧮 Version Milestones

| Version | Stage | Highlights |
|----------|--------|-------------|
| **0.3** | Current | Working shell, prompt color, core file ops |
| **0.4** | In Progress | History, input cleanup, `set` command, case toggle |
| **0.5** | Planned | Argument parsing, better error handling, extended `help` |
| **0.6** | Future | Add piping and chaining (`cmd1 | cmd2`), install target |
| **0.7** | Future | Add configuration support (`.winixrc`), environment vars |
| **1.0** | Stable | Windows-native Unix parity — clean, documented, extensible |

---

📌 *Rule of thumb:*  
- Increment **minor** (x.y) when new features appear.  
- Increment **patch** (x.y.z) for fixes or optimizations.  
- Don’t bump **major** until you’re feature-complete and stable.


_Keep this file updated with every commit.  When something ships, tick it off — and feel good about it._
