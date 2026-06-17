# Winix

Winix is a Windows-native shell and GNU-style utilities project.

## Repo map
- /src/core = shell core, parser, executor
- /src/cmds = built-in commands
- /tests = unit and integration tests
- /docs = design and release notes

## Hard rules

- Preserve Windows-native behavior first.
- Prefer small, surgical edits over broad rewrites.
- Do not casually break CLI flags, exit codes, or stdout/stderr behavior.

- **Winix-first rule**  
  Prefer implementing functionality inside Winix unless a battle-hardened
  library clearly solves the problem better (decades of edge cases,
  security fixes, and platform testing).  
  In those cases, bundle the library rather than re-implementing it.

- Save Winix-first effort for tools that benefit from custom design
  (examples: wlint, wsim, nix, wzip).

## Project-specific guidance

When modifying:
- parser
- executor
- built-in commands
- command architecture
- release-sensitive behavior

load and follow the `winix-architecture` skill.

## Change discipline
- Inspect only the files needed for the task.
- Prefer minimal patches over exploratory refactors.
- Do not scan or summarize unrelated parts of the repository.

## Version discipline

Winix versioning uses a single source of truth: `VERSION` in the repo root.

Rules:
- Always read `VERSION` before discussing current release state.
- Do not infer the current version from tags, GitHub Releases, workflow runs, roadmap notes, or prior conversation state.
- Treat tags, GitHub Releases, release artifacts, and docs as derived from `VERSION`.
- If any of those disagree with `VERSION`, report the mismatch explicitly.
- Do not say a release is complete until tag, GitHub Release, and release artifacts all match `VERSION`.

### Version bump law — do not over-increment

Bloated version numbers are a code smell. Default to the smallest valid bump.

| Bump | When |
|------|------|
| **Patch** x.y.**Z** | Bug fixes, missing behaviors, installer/tooling changes, small QoL improvements |
| **Minor** x.**Y**.0 | A meaningful cluster of user-visible features — group work, don't release every fix separately |
| **Major** **X**.0.0 | Architectural shift only (new subsystem, breaking change, landmark release) |

**Default to patch. Push back on minor bumps unless there is a clear feature story.**  
Never propose a minor or major bump for a single small fix.