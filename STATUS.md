# Status — Winix
_Reviewed 2026-10-10 by GitHub Copilot_

## What it is
Winix is a Windows program that provides a Unix-like command prompt and a large collection of familiar command-line tools, such as `ls`, `grep`, and `sed`. It is for Windows users who want to run shell commands and scripts without installing WSL or another compatibility layer.

## Where it stands
- The shell and many standalone tools are implemented, including scripting, pipes, redirects, command history, and tab completion.
- It builds and runs on Windows: the latest successful main-branch CI run (2026-09-17) built the project and passed its smoke checks and all 746 functional checks. The checks run on Windows with MinGW; this review environment could not run Windows binaries locally.
- A downloadable Windows x64 v4.5.0 package and installer/uninstaller exist. The release tag and package match the repository’s `VERSION` file; the installer requires administrator access, and the README notes the binaries are not code-signed.
- Some work is optional or unfinished: `wsu`, an interactive elevated shell, is documented as shelved and is not wired into the build. Project notes also lag behind the current release: `NEXT.md` still says 4.1.6, and the testing guide says a test harness is planned even though CI runs `tests/run_tests.py`.

## Path to a usable first version (MVP)
The v4.5.0 package is already installable and useful; the remaining steps are mainly about confidence and clear expectations for new users.

1. Confirm whether the installer should add Windows Defender exclusions automatically (see the decision below).
2. Run one clean-machine acceptance check on supported Windows versions: install the downloaded package, try the documented shell and command basics, then uninstall and check that Winix’s settings are removed.
3. Bring the README, testing guide, and roadmap up to date with the current release, actual test coverage, unsigned status, and supported Windows behavior.
4. Publish a short list of the shell features and commands tested for the first user-ready release, including known Windows differences; keep shelved features out of that promise.

## Decisions needed
### 1. Should the installer add Windows Defender exclusions automatically?
- **Why it matters:** The administrator-run installer excludes the Winix folders from Defender scanning, and the current uninstaller does not remove those exclusions.
- **Options:** A) Keep the current automatic exclusions. B) Remove them by default or ask the user before adding them.
- **Recommended:** B — users should choose whether to reduce scanning for installed programs.
- **Answer:** _(author writes here)_

## ⚠️ Security
- A local pattern scan of tracked text files found no likely plaintext passwords, API keys, access tokens, or private keys. GitHub’s secret-alert list could not be checked in this review because the API denied access.
- Public contact addresses for the bundled zlib authors appear in `src/third_party/zlib/LICENSE:22` and `src/third_party/zlib/zlib.h:23`. They are upstream license attribution, not apparent credentials or accidental user data.

## Can proceed now
- Update stale roadmap and testing documentation to match the current release and the existing test harness.
- Add a Windows acceptance check for installing, upgrading, and uninstalling the release package.
- Expand command documentation and regression coverage for behavior already implemented, without deciding on broader compatibility promises.
