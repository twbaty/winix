# Winix Changelog
All notable changes to this project will be documented in this file.

This project adheres to [Semantic Versioning](https://semver.org/) and follows the
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) format.

---

## [Unreleased]
_Changes currently in development and not yet part of a formal release._

### Added
- Initial framework for Winix shell environment.
- Command history persistence across sessions.
- Windows CMD (`cmd.exe`) compatibility confirmed.
- Support for long command recall and edit buffer.

### Changed
- Adjusted input redraw logic for smoother line editing.
- Updated cursor handling for multi-line commands.

### Fixed
- History overflow behavior when session resumes.
- Arrow key handling under Windows terminal variants.

---

## [0.9.3] – 2025-10-25
### Added
- Installer packaging scripts and directory structure.
- Optional SHA-256 and GPG signature verification steps.
- Documentation updates (`Winix Project Overview`, `Developer Quick Start`).

### Fixed
- Cursor positioning bug when editing recalled commands.
- Line overflow issue under DOS prompt compatibility mode.

---

## [0.9.2] – 2025-10-15
### Added
- Persistent command history across Winix sessions.
- Session-aware command buffer with recall navigation.

### Fixed
- History truncation on long command entries.
- Command echo duplication in certain consoles.

---

## [0.9.0] – 2025-09-30
### Added
- Base Winix architecture established.
- Initial command parser and execution handler.
- Default directory scaffolding:
  - `src`, `etc`, `usr`, `var`, `tests`, and `docs`.
- CMake build configuration (`CMakeLists.txt`).
- Basic documentation (`README.md`, `LICENSE`, `Project Charter`).

---

## [1.0.0] – *TBD*
_This will represent the first stable, publicly installable release._

### Added
- Self-extracting installer (`WinixSetup.exe`).
- Verified SHA-256 / GPG integrity system.
- Complete documentation suite.
- Contributor and developer onboarding guides.

### Security
- Signed release artifacts and published verification keys.

---

## [Past Development Notes]
_Informal development history prior to changelog adoption._

- Early source tree commits and experimental builds.
- Environment setup, architecture planning, and repository initialization.

---

