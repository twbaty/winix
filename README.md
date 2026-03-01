# Winix

![Version](https://img.shields.io/badge/version-1.0-blue)
![License](https://img.shields.io/badge/license-MIT-green)
![Platform](https://img.shields.io/badge/platform-Windows-0078D4)
![Language](https://img.shields.io/badge/language-C%2FC%2B%2B-orange)
[![CI](https://github.com/twbaty/winix/actions/workflows/ci.yml/badge.svg)](https://github.com/twbaty/winix/actions/workflows/ci.yml)

> The Unix Windows should have had.

---

## Project Overview
Mission
Winix is an open-source initiative to build a native, fast, Unix-like command environment for Windows — delivering the familiarity and power of GNU utilities without relying on compatibility layers like MSYS, MinGW, or WSL.
Our mission is simple:
Bring the clarity and strength of Linux command-line tooling to Windows — natively, cleanly, and permanently.
________________________________________
Purpose & Direction
Winix aims to:
- Provide a modular suite of native Windows command-line utilities written in C.
- Deliver a modern C++ shell that behaves like Bash but integrates cleanly with Windows internals.
- Maintain behavioral compatibility with GNU utilities where appropriate, while using completely independent, clean-room implementations.
- Follow a Unix-style directory structure under C:\Winix:
  - `/usr/bin` – core utilities
  - `/usr/lib` – shared libraries
  - `/etc` – configuration
  - `/var` – logs, runtime data

> The end goal: a self-contained, first-class CLI ecosystem that feels like Unix but runs purely on Windows.
________________________________________
Guiding Principles
1.	Native First: All binaries are compiled for Windows — no MSYS, no WSL.
2.	Behavioral Parity, Not Code Parity: GNU defines the behavioral baseline, not the implementation.
3.	Simplicity Over Cleverness: Readable, maintainable code is the goal.
4.	Open Collaboration, Single Direction: Contributions are welcome — divergence is not.
5.	Transparency & Longevity: Everything is open-source (MIT License) and designed to outlive its creators.
________________________________________
Governance & Collaboration
- Core Team: Defines architecture, approves merges, and maintains project integrity.
- Contributors: Submit code, tests, or documentation aligned with the unified roadmap.
- RFC Model: Major feature changes or design shifts require an open proposal and discussion before approval.
All contributions must align with the project’s primary goal — a cohesive, native Unix-like experience for Windows users.
________________________________________
Initial Technical Direction
Layer	Language	Description
Core Utilities	C	Standalone executables mirroring essential GNU tools (ls, cp, mv, rm, cat, etc.)
Shell	C++	Interactive REPL with parsing, execution, redirection, and environment management
Shared Library	C	Common argument parsing, error handling, and I/O routines
Build System	CMake	Unified cross-platform build and packaging framework
________________________________________

---

*Copyright (c) 2025 twbaty — MIT License*
