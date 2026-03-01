# Winix Coding Standards

> Follow these conventions for all code contributed to Winix.
> The goal is readable, auditable, maintainable C/C++ — not clever code.

---

## 1. Language Standards

| Layer | Standard |
|-------|----------|
| Coreutils | C99 (`-std=c99`) |
| Shell | C++17 (`-std=c++17`) |
| Shared library | C99 |

No C++ exceptions. No RTTI. No STL containers in hot paths unless clearly justified.

---

## 2. File Layout

### Coreutils
Each utility is a single `.c` file in `src/coreutils/`. No headers, no cross-file dependencies beyond `winixcommon`.

Structure within a coreutil file:
```c
// 1. Standard library includes
// 2. Platform includes (#ifdef _WIN32)
// 3. Static global flags (verbose, force, recursive, etc.)
// 4. Helper functions (static)
// 5. int main(int argc, char *argv[])
```

### Shell
Shell code lives in `src/shell/`. Each logical subsystem gets a `.cpp`/`.hpp` pair:
- `main.cpp` — REPL, builtins, dispatch
- `aliases.cpp/.hpp` — alias management
- `completion.cpp/.hpp` — tab completion
- `line_editor.cpp/.hpp` — raw input and history

---

## 3. Naming

| Element | Convention | Example |
|---------|-----------|---------|
| Functions | `snake_case` | `copy_file`, `parse_redirects` |
| Variables | `snake_case` | `src_path`, `dst_is_dir` |
| Constants / macros | `UPPER_SNAKE` | `make_dir`, `MAX_PATH_LEN` |
| Structs | `snake_case` with `_t` optional | `struct utimbuf`, `Redirects` |
| C++ classes | `PascalCase` | `LineEditor`, `Completion` |
| C++ members | `snake_case` | `last_exit`, `ps1` |

---

## 4. Flag Parsing

All coreutils parse flags the same way — combined short flags, stopped by `--` or a non-flag argument:

```c
static int verbose = 0, force = 0, recursive = 0;

int argi = 1;
while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
    for (const char *p = argv[argi] + 1; *p; p++) {
        if      (*p == 'v') verbose   = 1;
        else if (*p == 'f') force     = 1;
        else if (*p == 'r' || *p == 'R') recursive = 1;
        else {
            fprintf(stderr, "<util>: invalid option -- '%c'\n", *p);
            return 1;
        }
    }
    argi++;
}
```

Never use `getopt` — it's not reliably available on Windows without MSYS.

---

## 5. Exit Codes

| Code | Meaning |
|------|---------|
| `0` | Success |
| `1` | Any error |

No other exit codes. Use `return 1` from `main()` on error, `return 0` on success. Never `exit()` from inside helper functions — return an `int` and propagate it up.

---

## 6. Error Messages

Format: `<utility>: <what failed>: <reason>`

```c
// Good
fprintf(stderr, "cp: cannot open '%s': %s\n", src, strerror(errno));
fprintf(stderr, "chmod: cannot change permissions of '%s': error %lu\n", path, GetLastError());

// Bad
perror("cp");           // no context
fprintf(stderr, "Error opening file\n");  // no utility name, no filename
```

Always print errors to `stderr`. Never print errors to `stdout`.

---

## 7. Memory and Buffers

- Use stack-allocated path buffers of `4096` bytes: `char buf[4096]`.
- Always `snprintf`, never `sprintf` or `strcpy` into fixed buffers.
- Do not `malloc` for temporary strings unless the size is genuinely dynamic.
- Free everything you allocate. Winix utilities are short-lived processes, but leaks are still bad practice.

```c
// Good
char dst_path[4096];
snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, base);

// Bad
char dst_path[256];   // too small for real paths
sprintf(dst_path, "%s/%s", dst, base);  // no bounds check
```

---

## 8. Platform Guards

Windows-specific code goes inside `#ifdef _WIN32`:

```c
#ifdef _WIN32
#include <windows.h>
#define make_dir(p) _mkdir(p)
#else
#define make_dir(p) mkdir(p, 0755)
#endif
```

Keep platform-specific blocks as narrow as possible. Prefer abstracting into a helper or macro rather than scattering `#ifdef` throughout logic code.

---

## 9. Comments

Comment the **why**, not the **what**:

```c
// Good — explains the non-obvious decision
/* Windows only exposes one meaningful permission bit: FILE_ATTRIBUTE_READONLY.
 * We map all POSIX write-bit operations to it. */

// Bad — restates the code
/* loop through arguments */
for (int i = argi; i < argc; i++) {
```

Use `/* */` block comments for function-level documentation. Use `//` for inline notes.

Do not leave trailing `\\` at the end of `//` comments — on MSVC/MinGW this can act as a line continuation and silently swallow the next line.

---

## 10. Formatting

- **Indent:** 4 spaces. No tabs.
- **Braces:** opening brace on the same line as the control statement.
- **Line length:** soft limit of 100 characters.
- **Blank lines:** one blank line between functions; none inside short functions.

```c
// Good
static int copy_file(const char *src, const char *dst, const struct stat *st) {
    if (!force) {
        FILE *chk = fopen(dst, "rb");
        if (chk) {
            fclose(chk);
            fprintf(stderr, "cp: '%s' already exists (use -f to overwrite)\n", dst);
            return 1;
        }
    }
    // ...
    return 0;
}
```

---

## 11. Adding a New Coreutil

1. Create `src/coreutils/<name>.c`
2. Add to `CMakeLists.txt`:
   ```cmake
   add_executable(<name> src/coreutils/<name>.c)
   target_link_libraries(<name> winixcommon)
   ```
3. Add `<name>` to the `install(TARGETS ...)` list in `CMakeLists.txt`
4. Add `<name>.exe` to the binary check list in `.github/workflows/ci.yml`
5. Document it in `docs/design_spec.md` (coreutils table)
6. Tick it off in `NEXT.md`
